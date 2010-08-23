/*
 * Copyright (C) 2008-2010 Nicholas J. Kain <nicholas aatt kain.us>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/random.h>
#include <sys/capability.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <grp.h>

#include "defines.h"
#include "log.h"
#include "util.h"
#include "sound.h"
#include "rb.h"
#include "getrandom.h"

ring_buffer_t rb;

struct pool_buffer_t {
    struct rand_pool_info info;
    char buf[POOL_BUFFER_SIZE];
};

unsigned char max_bit = DEFAULT_MAX_BIT;

static char *chroot_path;

static void main_loop(int random_fd, int max_bits);
static void usage(void);
static void copyright();

static int random_max_bits(int random_fd);
static unsigned int ioc_rndaddentropy(struct pool_buffer_t *poolbuf,
                                      int handle, int wanted_bits);
static void drop_privs(int uid, int gid);

static void exit_cleanup(int signum)
{
    if (munlockall() == -1)
        suicide("problem unlocking pages");
    unlink(pidfile_path);
    sound_close();
    log_line(LOG_NOTICE, "snd-egd stopping due to signal %d", signum);
    print_random_stats();
    exit(0);
}

static void sighandler(int signum)
{
    int t;
    switch (signum) {
        case SIGHUP:
        case SIGINT:
        case SIGTERM:
            exit_cleanup(signum);
            break;
        case SIGUSR1:
            t = gflags_debug;
            gflags_debug = 1;
            print_random_stats();
            gflags_debug = t;
            break;
        case SIGUSR2:
            gflags_debug = !gflags_debug;
            break;
    }
}

int main(int argc, char **argv)
{
    int c, random_fd = -1, uid = -1, gid = -1;
    struct option long_options[] = {
        {"device",  1, NULL, 'd'},
        {"port", 1, NULL, 'i'},
        {"max-bit", 1, NULL, 'b'},
        {"nodetach", 1, NULL, 'n'},
        {"sample-rate", 1, NULL, 'r'},
        {"skip-bytes", 1, NULL, 's'},
        {"pid-file", 1, NULL, 'p'},
        {"user", 1, NULL, 'u'},
        {"group", 1, NULL, 'g'},
        {"chroot", 1, NULL, 'c'},
        {"verbose", 0, NULL, 'v'},
        {"help", 0, NULL, 'h'},
        {NULL, 0, NULL, 0 }
    };

    /* Process commandline options */
    while (1) {
        int t;

        c = getopt_long(argc, argv, "d:i:b:nr:s:p:u:g:c:vh",
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
                uid = parse_user(optarg, &gid);
                break;

            case 'g':
                gid = parse_group(optarg);
                break;

            case 'c':
                chroot_path = strdup(optarg);
                break;

            case 'v':
                gflags_debug = 1;
                break;

            case 'h':
            case '?':
            default:
                copyright();
                usage();
                exit(1);
        }
    }

    signal(SIGPIPE, SIG_IGN);
    signal(SIGHUP, sighandler);
    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);
    signal(SIGUSR1, sighandler);
    signal(SIGUSR2, sighandler);

    log_line(LOG_NOTICE, "snd-egd starting up");

    /* Open kernel random device */
    random_fd = open(RANDOM_DEVICE, O_RDWR);
    if (random_fd == -1)
        suicide("Couldn't open random device: %m");

    /* Find out the kernel entropy pool size */
    int max_bits = random_max_bits(random_fd);

    sound_open();

    if (gflags_detach)
        daemonize();

    if (chroot_path) {
        if (chdir(chroot_path))
            suicide("chdir chroot failed");
        if (chroot(chroot_path))
            suicide("chroot failed");
    }

    if (uid != -1 && gid != -1)
        drop_privs(uid, gid);

    if (mlockall(MCL_FUTURE))
        suicide("mlockall failed");

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


static void usage(void)
{
    printf("Collect entropy from a sound card and feed it into the kernel random pool.\n");
    printf("Usage: snd-egd [options]\n\n");
    printf("--device,       -d []  Sound device used (default %s)\n", DEFAULT_HW_DEVICE);
    printf("--item,         -i []  Sound device item used (default %s)\n", DEFAULT_HW_ITEM);
    printf("--max-bit       -b []  Maximum significance used in samples. (default %d)\n", DEFAULT_MAX_BIT);
    printf("--sample-rate,  -r []  Audio sampling rate. (default %i)\n", DEFAULT_SAMPLE_RATE);
    printf("--skip-bytes,   -s []  Ignore first N audio bytes (default %i)\n", DEFAULT_SKIP_BYTES);
    printf("--pid-file,     -p []  PID file path (default %s)\n", DEFAULT_PID_FILE);
    printf("--user          -u []  User name or id to change to after dropping privileges.\n");
    printf("--group         -g []  Group name or id to change to after dropping privileges.\n");
    printf("--chroot        -c []  Directory to use as the chroot jail.\n");
    printf("--nodetach      -n     Do not fork.\n");
    printf("--verbose,      -v     Be verbose.\n");
    printf("--help,         -h     This help.\n");
}

static void copyright()
{
    printf(
        "snd-egd %s Copyright (C) 2008-2010 Nicholas J. Kain\n"
        "This program is free software: you can redistribute it and/or modify\n"
        "it under the terms of the GNU General Public License as published by\n"
        "the Free Software Foundation, either version 3 of the License, or\n"
        "(at your option) any later version.\n\n", SNDEGD_VERSION);
    printf(
        "This program is distributed in the hope that it will be useful,\n"
        "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
        "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
        "GNU General Public License for more details.\n\n"

        "You should have received a copy of the GNU General Public License\n"
        "along with this program.  If not, see <http://www.gnu.org/licenses/>.\n\n");
}
