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

ring_buffer_t *rb;

static unsigned char max_bit = DEFAULT_MAX_BIT;

static void main_loop(int random_fd, int max_bits);
static void usage(void);
static int vn_renorm_buf(char *buf8, size_t buf8size);
static void get_random_data(int process_samples);
static int random_max_bits(int random_fd);
static unsigned int ioc_rndaddentropy(int handle, int wanted_bits);
static void drop_privs(int uid, int gid);

int main(int argc, char **argv)
{
    int c, random_fd = -1, uid = -1, gid = -1;
    struct option long_options[] = {
            {"device",  1, NULL, 'd' },
            {"port", 1, NULL, 'i' },
            {"max-bit", 1, NULL, 'b' },
            {"do-not-fork", 1, NULL, 'n' },
            {"sample-rate", 1, NULL, 'r' },
            {"skip-bytes", 1, NULL, 's' },
            {"pid-file", 1, NULL, 'p' },
            {"uid", 1, NULL, 'u'},
            {"gid", 1, NULL, 'g'},
            {"verbose", 0, NULL, 'v' },
            {"help",    0, NULL, 'h' },
            {NULL,      0, NULL, 0   }
        };

    /* Process commandline options */
    while (1) {
        int t;

        c = getopt_long (argc, argv, "i:d:b:nr:s:p:u:g:vh",
                         long_options, NULL);
        if (c == -1)
            break;

        switch(c) {
            case 'i':
                sound_set_port(optarg);
                break;

            case 'd':
                sound_set_device(optarg);
                break;

            case 'n':
                gflags_detach = 0;
                break;

            case 'b':
                t = atoi(optarg);
                if (t > 0 && t <= 16)
                    max_bit = t;
                else
                    max_bit = DEFAULT_MAX_BIT;
                break;

            case 'r':
                t = atoi(optarg);
                sound_set_sample_rate(t);
                break;

            case 's':
                t = atoi(optarg);
                sound_set_skip_bytes(t);
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

    rb = rb_new(RB_SIZE);

    sound_open();

    /* Prefill entropy buffer */
    get_random_data(rb->size - rb->bytes);

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
            i += ioc_rndaddentropy(random_fd, wanted_bits - i);

        get_random_data(rb->size - rb->bytes);
    }
    sound_close();
}

/*
 * Loads the kernel random number generator with data from the output data
 * arrays.
 * @return number of bits that were loaded to the KRNG
 */
static unsigned int ioc_rndaddentropy(int handle, int wanted_bits)
{
    unsigned int total_cur_bytes;
    unsigned int wanted_bytes;
    struct rand_pool_info *output;

    wanted_bytes = wanted_bits / 8;
    if (wanted_bits & 7)
        ++wanted_bytes;

    total_cur_bytes = rb_num_bytes(rb);

    if (total_cur_bytes < wanted_bytes)
        wanted_bytes = total_cur_bytes;

    output = (struct rand_pool_info *)xmalloc(sizeof(struct rand_pool_info)
                                             + wanted_bytes);

    output->entropy_count = wanted_bytes * 8;
    output->buf_size      = wanted_bytes;
    if (rb_move(rb, (char *)output->buf, wanted_bytes) == -1)
        suicide("rb_move() failed");

    if (ioctl(handle, RNDADDENTROPY, output) == -1)
        suicide("RNDADDENTROPY failed!");

    free(output);

    log_line(LOG_DEBUG, "%d bits requested, %d bits stored, %d bits added, %d bits remain",
             wanted_bits, total_cur_bytes * 8, wanted_bytes * 8, rb_num_bytes(rb) * 8);

    return wanted_bytes * 8;
}

typedef struct {
    int bits_out, topbit, total_out;
    char prev_bits[16];
    unsigned char byte_out;
} vn_renorm_state_t;

static void vn_renorm_init(vn_renorm_state_t *state, int topbit)
{
    int j;

    if (!state)
        return;
    state->bits_out = 0;
    state->byte_out = 0;
    state->total_out = 0;
    state->topbit = topbit;
    for (j = 0; j < 16; ++j)
        state->prev_bits[j] = -1;
}

static int vn_renorm(vn_renorm_state_t *state, int i)
{
    int j, new = -1;

    /* process bits */
    for (j = 0; j < state->topbit; ++j) {
        /* Select the bit of given significance. */
        new = (i >> j) & 1;

        /* We've not yet collected two bits; move on. */
        if (state->prev_bits[j] == -1) {
            state->prev_bits[j] = new;
            continue;
        }

        /* If the bits are equal, discard both. */
        if (state->prev_bits[j] == new) {
            state->prev_bits[j] = -1;
            continue;
        }

        /* If 10, mark the bit as 1.  Otherwise, it's 01 and the bit
         * is already marked as 0. */
        if (state->prev_bits[j])
            state->byte_out |= 1 << state->bits_out;
        state->bits_out++;
        state->prev_bits[j] = -1;

        /* See if we've collected an entire byte.  If so, then copy
         * it into the output buffer. */
        if (state->bits_out == 8) {
            state->total_out += rb_store_byte_xor(rb, state->byte_out);

            state->bits_out = 0;
            state->byte_out = 0;

            if (rb_is_full(rb))
                return 1;
        }
    }
    return 0;
}

/*
 * We assume that the chance of a given bit in a sample being a 0 or 1 is not
 * equal.  It is thus a statistically unfair 'coin'.  We can nevertheless use
 * this sample as if it were a fair 'coin' if we use a special procedure:
 *
 * 1. Take a pair of bits of fixed significance in the output
 * 2. If 01, treat as a zero bit.
 * 3. If 10, treat as a one bit.
 * 4. Otherwise, discard as no result.
 *
 * @return number of bytes that were added to the entropy buffer
 */
static int vn_renorm_buf(char *buf8, size_t buf8size)
{
    int16_t *buf = (int16_t *)buf8;
    size_t i, bufsize = buf8size / 2;
    int topbit;
    vn_renorm_state_t state_L, state_R;

    if (!buf || !bufsize)
        suicide("vn_renorm_buf received a NULL arg");

    topbit = MIN(max_bit, 16);
    vn_renorm_init(&state_L, topbit);
    vn_renorm_init(&state_R, topbit);

#ifdef HOST_ENDIAN_BE
    if (sound_is_le()) {
#else
    if (sound_is_be()) {
#endif
        for (i = 0; i < (bufsize / 2); ++i) {
            endian_swap16(buf + 2*i);
            endian_swap16(buf + 2*i + 1);
        }
    }

    /* Step through each 16-bit sample in the buffer one at a time. */
    for (i = 0; i < (bufsize / 2); ++i) {
        if (vn_renorm(&state_L, buf[2*i]))
            break;
        if (vn_renorm(&state_R, buf[2*i+1]))
            break;
    }
    return state_L.total_out + state_R.total_out;
}

#ifdef USE_AMLS
static size_t amls_renorm_buf(char *buf8, size_t buf8size)
{
    int16_t *buf = (int16_t *)buf8;
    char *in, *out, *outp;
    size_t i, j;
    size_t bufsize = buf8size / 2, insize = 0, amls_out = 0, total_out = 0;
    int topbit, bits_out = 0;
    char prev = -1;
    unsigned char byte_out = 0;

    if (!buf || !bufsize)
        suicide("amls_renorm_buf received a NULL arg");

    topbit = MIN(max_bit, 16);

    in = alloca(bufsize * topbit);

#ifdef HOST_ENDIAN_BE
    if (sound_is_le()) {
#else
    if (sound_is_be()) {
#endif
        for (i = 0; i < (bufsize / 2); ++i) {
            endian_swap16(buf + 2*i);
            endian_swap16(buf + 2*i + 1);
        }
    }

    for (i = 0; i < topbit; ++i) {
        for (j = 0; j < (bufsize / 2); ++j) {
            in[insize++] = (buf[2*j] >> i) & 1;
            in[insize++] = (buf[2*j+1] >> i) & 1;
        }
    }

    out = alloca(insize);
    outp = out;
    memset(out, '\0', insize);
    amls_round(in, in + insize, &outp);
    amls_out = outp - out;

    for (i = 0; i < amls_out; ++i) {
        if (prev == -1) {
            prev = out[i];
            continue;
        }

        if (prev == out[i]) {
            prev = -1;
            continue;
        }

        if (prev)
            byte_out |= 1 << bits_out;
        bits_out++;
        prev = -1;

        /* See if we've collected an entire byte.  If so, then copy
         * it into the output buffer. */
        if (bits_out == 8) {
            total_out += rb_store_byte_xor(rb, byte_out);
            bits_out = 0;
            byte_out = 0;

            if (rb_is_full(rb))
                return total_out;
        }
    }
    return total_out;
}
#endif

/* target = desired bytes of entropy that should be retrieved */
static void get_random_data(int target)
{
    int total_in = 0, total_out = 0;
    char buf[PAGE_SIZE];

    log_line(LOG_DEBUG, "get_random_data(%d)", target);

    target = MIN(sizeof buf, target);

    sound_start();
    while (total_out < target) {
        sound_read(buf, sizeof buf);
        total_in += sizeof buf;
#ifdef USE_AMLS
        total_out += amls_renorm_buf(buf, sizeof buf);
#else
        total_out += vn_renorm_buf(buf, sizeof buf);
#endif
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

