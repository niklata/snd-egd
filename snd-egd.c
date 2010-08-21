/*
 * Simple program to reseed kernel random number generator using
 * data read from soundcard.
 *
 * Copyright 2008-2010 Nicholas Kain <nicholas aatt kain.us>
 * Copyright 2000-2009 by Folkert van Heusden <folkert@vanheusden.com>
 * Copyright 1999 Damien Miller <djm@mindrot.org>
 *
 * This code is licensed under the GNU Public License version 2
 * Please see the file COPYING for more details.
 *
 */

#include <stdlib.h>
#include <math.h>
#include <getopt.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/time.h>

#include <asm/types.h>
#include <linux/random.h>
#include <errno.h>

#include <sys/capability.h>
#include <sys/prctl.h>
#include <grp.h>


#include "defines.h"
#include "log.h"
#include "util.h"
#include "amls.h"
#include "sound.h"
#include "rb.h"
#include "vn.h"

ring_buffer_t rb;

unsigned int stats[2][256];

struct pool_buffer_t {
    struct rand_pool_info info;
    char buf[POOL_BUFFER_SIZE];
};

unsigned char max_bit = DEFAULT_MAX_BIT;

static void main_loop(int random_fd, int max_bits);
static void usage(void);

static void get_random_data(int process_samples);
static int random_max_bits(int random_fd);
static unsigned int ioc_rndaddentropy(struct pool_buffer_t *poolbuf,
                                      int handle, int wanted_bits);
static void drop_privs(int uid, int gid);

int main(int argc, char **argv)
{
    int c, random_fd = -1, uid = -1, gid = -1;
    struct option long_options[] = {
        {"device",  1, NULL, 'd'},
        {"port", 1, NULL, 'i'},
        {"max-bit", 1, NULL, 'b'},
        {"do-not-fork", 1, NULL, 'n'},
        {"sample-rate", 1, NULL, 'r'},
        {"skip-bytes", 1, NULL, 's'},
        {"pid-file", 1, NULL, 'p'},
        {"uid", 1, NULL, 'u'},
        {"gid", 1, NULL, 'g'},
        {"verbose", 0, NULL, 'v'},
        {"help", 0, NULL, 'h'},
        {NULL, 0, NULL, 0 }
    };

    /* Process commandline options */
    while (1) {
        int t;

        c = getopt_long (argc, argv, "d:i:b:nr:s:p:u:g:vh",
                         long_options, NULL);
        if (c == -1)
            break;

        switch(c) {
            case 'd':
                sound_set_device(optarg);
                break;

            case 'i':
                sound_set_port(optarg);
                break;

            case 'b':
                t = atoi(optarg);
                if (t > 0 && t <= 16)
                    max_bit = t;
                else
                    max_bit = DEFAULT_MAX_BIT;
                break;

            case 'n':
                gflags_detach = 0;
                break;

            case 's':
                t = atoi(optarg);
                sound_set_skip_bytes(t);
                break;

            case 'r':
                t = atoi(optarg);
                sound_set_sample_rate(t);
                break;

            case 'p':
                pidfile_path = strdup(optarg);
                break;

            case 'u':
                uid = atoi(optarg);
                break;

            case 'g':
                gid = atoi(optarg);
                break;

            case 'v':
                gflags_debug = 1;
                break;

            case 'h':
                usage();
                exit(0);

            case '?':
            default:
                log_line(LOG_NOTICE, "fatal: invalid command line options");
            usage();
            exit(1);
        }
    }

    signal(SIGPIPE, SIG_IGN);
    signal(SIGHUP, gracefully_exit);
    signal(SIGINT, gracefully_exit);
    signal(SIGTERM, gracefully_exit);
    signal(SIGUSR1, logging_handler);
    signal(SIGUSR2, logging_handler);

    log_line(LOG_NOTICE, "snd-egd starting up");

    /* Open kernel random device */
    random_fd = open(RANDOM_DEVICE, O_RDWR);
    if (random_fd == -1)
        suicide("Couldn't open random device: %m");

    /* Find out the kernel entropy pool size */
    int max_bits = random_max_bits(random_fd);

    if (mlockall(MCL_FUTURE | MCL_CURRENT) == -1)
        suicide("mlockall failed");

    if (gflags_detach)
        daemonize();

    if (uid != -1 && gid != -1)
        drop_privs(uid, gid);

    memset(stats, 0, sizeof stats);

    main_loop(random_fd, max_bits);

    exit(0);
}

static void drop_privs(int uid, int gid)
{
    cap_t caps;
    prctl(PR_SET_KEEPCAPS, 1);
    caps = cap_from_text("cap_sys_admin=ep");
    if (!caps)
        suicide("cap_from_text failed");
    if (setgroups(0, NULL) == -1)
        suicide("setgroups failed");
    if (setegid(18) == -1 || seteuid(130) == -1)
        suicide("dropping privs failed");
    if (cap_set_proc(caps) == -1)
        suicide("cap_set_proc failed");
    cap_free(caps);
}

static void wait_for_watermark(int random_fd)
{
    fd_set write_fd;
    FD_ZERO(&write_fd);
    FD_SET(random_fd, &write_fd);

    for (;;) {
        /* Wait for krng to fall below entropy watermark */
        if (select(random_fd + 1, NULL, &write_fd, NULL, NULL) >= 0)
            break;
        if (errno != EINTR)
            suicide("Select error: %m");
    }
}

static int random_max_bits(int random_fd)
{
    int ret, fd;
    char buf[11] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    fd = open(DEFAULT_POOLSIZE_FN, O_RDONLY);
    if (!fd)
        suicide("couldn't open poolsize procfs file");
    if (read(fd, buf, sizeof buf - 1) == -1)
        suicide("failed to read poolsize procfs file");
    ret = atoi(buf);
    if (ret < 1)
        suicide("poolsize can never be less than 1");
    return ret;
}

static int random_cur_bits(int random_fd)
{
    int ret;

    if (ioctl(random_fd, RNDGETENTCNT, &ret) == -1)
        suicide("Couldn't query entropy-level from kernel");
    return ret;
}

static void main_loop(int random_fd, int max_bits)
{
    int i, before, wanted_bits;
    struct pool_buffer_t poolbuf;

    rb_init(&rb);

    sound_open();

    /* Prefill entropy buffer */
    get_random_data(rb.size - rb.bytes);

    for(;;) {
        wait_for_watermark(random_fd);
        log_line(LOG_DEBUG, "woke up due to low entropy state");

        /* Find out how many bits to add */
        before = random_cur_bits(random_fd);
        wanted_bits = max_bits - before;
        log_line(LOG_DEBUG, "max_bits: %d, wanted_bits: %d",
                 max_bits, wanted_bits);

        /*
         * Loop until the buffer is full: we do not check the number of
         * bits currently in the buffer on each iteration, since it
         * might cause snd-egd to run constantly if there are
         * a lot of bytes being consumed from the random device.
         */
        for (i = 0; i < wanted_bits;)
            i += ioc_rndaddentropy(&poolbuf, random_fd, wanted_bits - i);

        get_random_data(rb.size - rb.bytes);
    }
    sound_close();
}

/*
 * Loads the kernel random number generator with data from the output data
 * arrays.
 * @return number of bits that were loaded to the KRNG
 */
static unsigned int ioc_rndaddentropy(struct pool_buffer_t *poolbuf,
                                      int handle, int wanted_bits)
{
    unsigned int total_cur_bytes;
    unsigned int wanted_bytes;

    wanted_bytes = wanted_bits / 8;
    if (wanted_bits & 7)
        ++wanted_bytes;

    total_cur_bytes = rb_num_bytes(&rb);

    if (total_cur_bytes < wanted_bytes)
        wanted_bytes = total_cur_bytes;

    wanted_bytes = MIN(wanted_bytes, POOL_BUFFER_SIZE);
    poolbuf->info.entropy_count = wanted_bytes * 8;
    poolbuf->info.buf_size = wanted_bytes;
    if (rb_move(&rb, poolbuf->buf, wanted_bytes) == -1)
        suicide("rb_move() failed");

    if (ioctl(handle, RNDADDENTROPY, poolbuf) == -1)
        suicide("RNDADDENTROPY failed!");

    log_line(LOG_DEBUG, "%d bits requested, %d bits stored, %d bits added, %d bits remain",
             wanted_bits, total_cur_bytes * 8, wanted_bytes / 8, rb_num_bytes(&rb) * 8);

    return wanted_bytes * 8;
}

/* target = desired bytes of entropy that should be retrieved */
static void get_random_data(int target)
{
    union frame_t {
        int16_t s16[2];
        int32_t s32;
    };
    int total_in = 0, total_out = 0, frames = 0, framesize = 0, i;
    union frame_t buf[PAGE_SIZE / 4];
    uint16_t leftbuf[PAGE_SIZE / 2], rightbuf[PAGE_SIZE / 2];
    vn_renorm_state_t leftstate, rightstate;

    vn_renorm_init(&leftstate);
    vn_renorm_init(&rightstate);

    log_line(LOG_DEBUG, "get_random_data(%d)", target);

    target = MIN(sizeof buf, target);

    sound_start();
    while (total_out < target) {
        frames = sound_read(buf, target);
        framesize = sound_bytes_per_frame();
        total_in += frames * framesize;
        log_line(LOG_DEBUG, "total_in = %d, frames = %d", total_in, frames);
        for (i = 0; i < frames - 1; ++i) {
            leftbuf[i] = abs(buf[i+1].s16[0] - buf[i].s16[0]);
            rightbuf[i] = abs(buf[i+1].s16[1] - buf[i].s16[1]);
        }

        if (frames > 0) {
            leftstate.stats = 0;
            total_out += vn_renorm_buf(leftbuf, frames, &leftstate);
            rightstate.stats = 1;
            total_out += vn_renorm_buf(rightbuf, frames, &rightstate);
        }
        log_line(LOG_DEBUG, "total_out = %d", total_out);
    }
    sound_stop();

    log_line(LOG_DEBUG, "get_random_data(): in->out bytes = %d->%d, eff = %f",
            total_in, total_out, (float)total_out / (float)total_in);
}

static void usage(void)
{
    log_line(LOG_NOTICE, "Usage: snd-egd [options]\n");
    log_line(LOG_NOTICE, "Collect entropy from a soundcard and feed it into the kernel random pool.");
    log_line(LOG_NOTICE, "Options:");
    log_line(LOG_NOTICE, "--device,       -d []  Specify sound device to use. (Default %s)", DEFAULT_HW_DEVICE);
    log_line(LOG_NOTICE, "--item,         -i []  Specify item on the device that we sample from. (Default %s)", DEFAULT_HW_ITEM);
    log_line(LOG_NOTICE, "--max-bit       -b []  Maximum significance of a bit that will be used in a sample. (Default %d)", DEFAULT_MAX_BIT);
    log_line(LOG_NOTICE, "--sample-rate,  -r []  Audio sampling rate. (default %i)", DEFAULT_SAMPLE_RATE);
    log_line(LOG_NOTICE, "--skip-bytes, -s []  Ignore the first N audio bytes after opening device. (default %i)", DEFAULT_SKIP_BYTES);
    log_line(LOG_NOTICE, "--pid-file,     -p []  Path where the PID file will be created. (default %s)", DEFAULT_PID_FILE);
    log_line(LOG_NOTICE, "--do-not-fork   -n     Do not fork.");
    log_line(LOG_NOTICE, "--verbose,      -v     Be verbose.");
    log_line(LOG_NOTICE, "--help,         -h     This help.");
}

