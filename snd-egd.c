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
#include <stdio.h>
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

#include <alsa/asoundlib.h>
#include <linux/soundcard.h>
#include <asm/types.h>
#include <linux/random.h>
#include <errno.h>

#include "defines.h"
#include "log.h"
#include "util.h"
#include "rb.h"
#include "error.h"

ring_buffer_t *rb;

static char *cdevice = DEFAULT_HW_DEVICE;
static const char *cdev_id = DEFAULT_HW_ITEM;
static unsigned int sample_rate = DEFAULT_SAMPLE_RATE;
static unsigned char max_bit = DEFAULT_MAX_BIT;
static int snd_format = -1;

static unsigned int skip_samples = 0;

static void main_loop(const char *cdevice);
static int alsa_setparams(snd_pcm_t *chandle);
static void usage(void);
static int vn_renorm_buf(char *buf8, size_t buf8size);
static void get_random_data(int process_samples);
static unsigned int ioc_rndaddentropy(int handle, int wanted_bits);

int main(int argc, char **argv)
{
    int c;
    struct option long_options[] = {
            {"device",  1, NULL, 'd' },
            {"port", 1, NULL, 'i' },
            {"max-bit", 1, NULL, 'b' },
            {"do-not-fork", 1, NULL, 'n' },
            {"sample-rate", 1, NULL, 'r' },
            {"skip-samples", 1, NULL, 's' },
            {"pid-file", 1, NULL, 'p' },
            {"verbose", 0, NULL, 'v' },
            {"help",    0, NULL, 'h' },
            {NULL,      0, NULL, 0   }
        };

    /* Process commandline options */
    while (1) {
        int t;

        c = getopt_long (argc, argv, "i:d:b:nr:s:p:vh", long_options, NULL);
        if (c == -1)
            break;

        switch(c) {
            case 'i':
                cdev_id = strdup(optarg);
                break;

            case 'd':
                cdevice = strdup(optarg);
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
                if (t > 0)
                    sample_rate = t;
                else
                    sample_rate = DEFAULT_SAMPLE_RATE;
                break;

            case 's':
                t = atoi(optarg);
                if (t > 0)
                    skip_samples = t;
                else
                    skip_samples = DEFAULT_SKIP_SAMPLES;
                break;

            case 'p':
                pidfile_path = strdup(optarg);
                break;

            case 'v':
                gflags_debug = 1;
                break;

            case 'h':
                usage();
                exit(0);

            case '?':
            default:
                fprintf(stderr, "Invalid commandline options.\n\n");
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

    if (mlockall(MCL_FUTURE | MCL_CURRENT) == -1)
        suicide("mlockall failed");

    if (gflags_detach)
        daemonize();

    main_loop(cdevice);

    exit(0);
}

static int alsa_setparams(snd_pcm_t *chandle)
{
    int err;
    snd_pcm_hw_params_t *ct_params;
    snd_pcm_hw_params_alloca(&ct_params);

    err = snd_pcm_hw_params_any(chandle, ct_params);
    if (err < 0)
        suicide("Broken configuration for %s PCM: no configurations available: %s",
                   cdev_id, snd_strerror(err));

    /* Disable rate resampling */
    err = snd_pcm_hw_params_set_rate_resample(chandle, ct_params, 0);
    if (err < 0)
        suicide("Could not disable rate resampling: %s", snd_strerror(err));

    /* Set access to SND_PCM_ACCESS_RW_INTERLEAVED */
    err = snd_pcm_hw_params_set_access(chandle, ct_params,
                                       SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err < 0)
        suicide("Could not set access to SND_PCM_ACCESS_RW_INTERLEAVED: %s",
                   snd_strerror(err));

    /* Choose rate nearest to our target rate */
    err = snd_pcm_hw_params_set_rate_near(chandle, ct_params, &sample_rate, 0);
    if (err < 0)
        suicide("Rate %iHz not available for %s: %s",
                   sample_rate, cdev_id, snd_strerror(err));

    /* Set sample format */
#ifdef HOST_ENDIAN_LE
    snd_format = SND_PCM_FORMAT_S16_LE;
#else
    snd_format = SND_PCM_FORMAT_S16_BE;
#endif
    err = snd_pcm_hw_params_set_format(chandle, ct_params, snd_format);
    if (err < 0) {
#ifdef HOST_ENDIAN_LE
        snd_format = SND_PCM_FORMAT_S16_BE;
#else
        snd_format = SND_PCM_FORMAT_S16_LE;
#endif
        err = snd_pcm_hw_params_set_format(chandle, ct_params, snd_format);
    }
    if (err < 0)
        suicide("Sample format (SND_PCM_FORMAT_S16_BE and _LE) not available for %s: %s",
                   cdev_id, snd_strerror(err));

    /* Set stereo */
    err = snd_pcm_hw_params_set_channels(chandle, ct_params, 2);
    if (err < 0)
        suicide("Channels count (%i) not available for %s: %s",
                   2, cdev_id, snd_strerror(err));

    /* Apply settings to sound device */
    err = snd_pcm_hw_params(chandle, ct_params);
    if (err < 0)
        suicide("Could not apply settings to sound device!");

    return 0;
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
    int ret;
    FILE *poolsize_fh;

    poolsize_fh = fopen(DEFAULT_POOLSIZE_FN, "rb");
    if (!poolsize_fh)
        suicide("Couldn't open poolsize file: %m");
    if (fscanf(poolsize_fh, "%d", &ret) != 1)
        suicide("Failed to read from poolsize file!");
    fclose(poolsize_fh);
    return ret;
}

static int random_cur_bits(int random_fd)
{
    int ret;

    if (ioctl(random_fd, RNDGETENTCNT, &ret) == -1)
        suicide("Couldn't query entropy-level from kernel");
    return ret;
}

static void main_loop(const char *cdevice)
{
    int random_fd = -1, max_bits;
    int i, before, wanted_bits;

    rb = rb_new(RB_SIZE);

    /* Open kernel random device */
    random_fd = open(RANDOM_DEVICE, O_RDWR);
    if (random_fd == -1)
        suicide("Couldn't open random device: %m");

    /* Find out the kernel entropy pool size */
    max_bits = random_max_bits(random_fd);

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
    size_t i, j, bufsize = buf8size / 2;
    int total_out = 0, bits_out = 0, topbit, new = -1, prev[16];
    unsigned char byte_out = 0;

    if (!buf || !bufsize)
        suicide("vn_renorm_buf received a NULL arg");

    for (j = 0; j < 16; ++j)
        prev[j] = -1;
    topbit = MIN(max_bit, 16);

    /* Step through each 16-bit sample in the buffer one at a time. */
    for (i = 0; i < bufsize; ++i) {
#ifdef HOST_ENDIAN_LE
        if (snd_format == SND_PCM_FORMAT_S16_BE)
            endian_swap16(buf + i);
#else
        if (snd_format == SND_PCM_FORMAT_S16_LE)
            endian_swap16(buf + i);
#endif

        /* process bits */
        for (j = 0; j < topbit; ++j) {
            /* Select the bit of given significance. */
            new = (buf[i] >> j) & 1;

            /* We've not yet collected two bits; move on. */
            if (prev[j] == -1) {
                prev[j] = new;
                continue;
            }

            /* If the bits are equal, discard both. */
            if (prev[j] == new) {
                prev[j] = -1;
                continue;
            }

            /* If 10, mark the bit as 1.  Otherwise, it's 01 and the bit
             * is already marked as 0. */
            if (prev[j])
                byte_out |= 1 << bits_out;
            bits_out++;
            prev[j] = -1;

            /* See if we've collected an entire byte.  If so, then copy
             * it into the output buffer. */
            if (bits_out == 8) {
                total_out += rb_store_byte_xor(rb, byte_out);

                bits_out = 0;
                byte_out = 0;

                if (rb_is_full(rb))
                    goto done;
            }
        }
    }
 done:
    return total_out;
}

/* target = desired bytes of entropy that should be retrieved */
static void get_random_data(int target)
{
    int total_in = 0, total_out = 0, err, i;
    size_t bytes_per_frame;
    char buf[PAGE_SIZE];
    snd_pcm_t *chandle;
    snd_pcm_sframes_t garbage_frames;

    if ((err = snd_pcm_open(&chandle, cdevice, SND_PCM_STREAM_CAPTURE, 0)) < 0)
        suicide("Record open error: %s", snd_strerror(err));

    /* Open and set up ALSA device for reading */
    alsa_setparams(chandle);

    log_line(LOG_DEBUG, "get_random_data(%d)", target);

    bytes_per_frame = snd_pcm_frames_to_bytes(chandle, 1);
    target = MIN(sizeof buf, target);

    for (i = skip_samples; i > 0; --i) {
        /* Discard the first data read it often contains weird looking
         * data - probably a click from driver loading or card initialization.
         */
        garbage_frames = snd_pcm_readi(chandle, buf,
                                       (sizeof buf) / bytes_per_frame);

        /* Make sure we aren't hitting a disconnect/suspend case */
        if (garbage_frames < 0)
            snd_pcm_recover(chandle, garbage_frames, 0);
        /* Nope, something else is wrong. Bail. */
        if (garbage_frames < 0)
            suicide("get_random_data(): read error: %m");
    }

    while (total_out < target) {
        snd_pcm_sframes_t frames_read;
        frames_read = snd_pcm_readi(chandle, buf,
                                    (sizeof buf) / bytes_per_frame);
        /* Make sure we aren't hitting a disconnect/suspend case */
        if (frames_read < 0)
            frames_read = snd_pcm_recover(chandle, frames_read, 0);
        /* Nope, something else is wrong. Bail. */
        if (frames_read < 0 || (frames_read == -1 && errno != EINTR))
            suicide("get_random_data(): Read error: %m");
        total_in += sizeof buf;
        total_out += vn_renorm_buf(buf, sizeof buf);
        log_line(LOG_DEBUG, "total_out = %d", total_out);
    }

    snd_pcm_close(chandle);

    log_line(LOG_DEBUG, "get_random_data(): in->out bytes = %d->%d, eff = %f",
            total_in, total_out, (float)total_out / (float)total_in);
}

static void usage(void)
{
    fprintf(stderr, "Usage: snd-egd [options]\n\n");
    fprintf(stderr, "Collect entropy from a soundcard and feed it into the kernel random pool.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "--device,       -d []  Specify sound device to use. (Default %s)\n", DEFAULT_HW_DEVICE);
    fprintf(stderr, "--item,         -i []  Specify item on the device that we sample from. (Default %s)\n", DEFAULT_HW_ITEM);
    fprintf(stderr, "--max-bit       -b []  Maximum significance of a bit that will be used in a sample. (Default %d)\n", DEFAULT_MAX_BIT);
    fprintf(stderr, "--sample-rate,  -r []  Audio sampling rate. (default %i)\n", DEFAULT_SAMPLE_RATE);
    fprintf(stderr, "--skip-samples, -s []  Ignore the first N audio samples. (default %i)\n", DEFAULT_SKIP_SAMPLES);
    fprintf(stderr, "--pid-file,     -p []  Path where the PID file will be created. (default %s)\n", DEFAULT_PID_FILE);
    fprintf(stderr, "--do-not-fork   -n     Do not fork.\n");
    fprintf(stderr, "--verbose,      -v     Be verbose.\n");
    fprintf(stderr, "--help,         -h     This help.\n");
    fprintf(stderr, "\n");
}

