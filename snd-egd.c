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

static unsigned int sample_size = MAX_SAMPLE_SIZE;
static unsigned int skip_samples = 0;

static unsigned int predict_sample(void);
static void main_loop(const char *cdevice);
static int alsa_setparams(snd_pcm_t *chandle);
static void usage(void);
static int process_input(char *buf, char *bufend);
static void get_random_data(int process_samples);
static unsigned int ioc_rndaddentropy(int handle, int wanted_bits);

/* scale our sampling rate according to demand */
static unsigned int predict_sample(void) {
    unsigned int bits_stored = rb_num_bytes(rb) * 8;

    if (bits_stored < (rb->size * 1))
        sample_size *= 2;
    else if (bits_stored < (rb->size * 2)) {
        sample_size *= 4;
        sample_size /= 3;
    } else if (bits_stored < (rb->size * 3)) {
        sample_size *= 8;
        sample_size /= 7;
    } else if (bits_stored < (rb->size * 4)) {
        sample_size *= 16;
        sample_size /= 15;
    } else if (bits_stored > (rb->size * 4)) {
        sample_size *= 15;
        sample_size /= 16;
    } else if (bits_stored < (rb->size * 5)) {
        sample_size *= 7;
        sample_size /= 8;
    } else if (bits_stored > (rb->size * 6)) {
        sample_size *= 3;
        sample_size /= 4;
    } else if (bits_stored > (rb->size * 7)) {
        sample_size /= 2;
    }

    sample_size = MIN(MAX_SAMPLE_SIZE, sample_size);
    sample_size = MAX(MAX_SAMPLE_SIZE / 16, sample_size);
    return sample_size;
}

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
    snd_format = SND_PCM_FORMAT_S16_LE;
    err = snd_pcm_hw_params_set_format(chandle, ct_params, snd_format);
    if (err < 0) {
        snd_format = SND_PCM_FORMAT_S16_BE;
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

static void main_loop(const char *cdevice)
{
    int random_fd = -1, max_bits;
    FILE *poolsize_fh;

    rb = rb_new(RB_SIZE);

    /* Open kernel random device */
    random_fd = open(RANDOM_DEVICE, O_RDWR);
    if (random_fd == -1)
        suicide("Couldn't open random device: %m");

    /* Find out the kernel entropy pool size */
    poolsize_fh = fopen(DEFAULT_POOLSIZE_FN, "rb");
    if (!poolsize_fh)
        suicide("Couldn't open poolsize file: %m");
    if (fscanf(poolsize_fh, "%d", &max_bits) != 1)
        suicide("Failed to read from poolsize file!");
    fclose(poolsize_fh);

    /*
     * First, get some data so that we can immediately submit something when
     * the kernel entropy-buffer gets below the limit.
     */
    get_random_data(sample_size);

    /* Main read loop */
    for(;;) {
        int i, total_added = 0, before, after;
        int wanted_bits;

        wait_for_watermark(random_fd);

        /* Find out how many bits to add */
        if (ioctl(random_fd, RNDGETENTCNT, &before) == -1)
            suicide("Couldn't query entropy-level from kernel");

        log_line(LOG_DEBUG, "woke up due to low entropy state (%d bits left)",
                 before);

        /*
         * Loop until the buffer is full: we do not check the number of
         * bits currently in the buffer on each iteration, since it
         * might cause snd-egd to run constantly if there are
         * a lot of bytes being consumed from the random device.
         */
        wanted_bits = max_bits - before;
        log_line(LOG_DEBUG, "max_bits: %d, wanted_bits: %d",
                 max_bits, wanted_bits);
        for (i = 0; i < wanted_bits;) {
            unsigned int ba = 0;

            ba = ioc_rndaddentropy(random_fd, wanted_bits - i);

            total_added += ba;
            i += ba;

            /* Get number of bits in KRNG after credit */
            if (ioctl(random_fd, RNDGETENTCNT, &after) == -1)
                suicide("Coundn't query entropy-level from kernel: %m");

            if (after < max_bits)
                log_line(LOG_DEBUG, "minimum level not reached: %d < %d",
                         after, max_bits);
        }

        get_random_data(predict_sample());

        log_line(LOG_INFO, "Entropy credit of %i bits made (%i bits before, %i bits after)",
                 total_added, before, after);
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

    log_line(LOG_DEBUG, "%d were requested, %d bits of data were stored, %d bits usable were added",
             wanted_bits, total_cur_bytes * 8, wanted_bytes * 8);

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
 */
static int process_input(char *buf, char *bufend)
{
    size_t i, j;
    int total_out = 0, bits_out = 0;
    int new = -1, prev_u[8], prev_l[8], u_src_byte, l_src_byte;
    int max_u, max_l;
    unsigned char byte_out = 0;

    if (!buf || !bufend)
        suicide("process_input received a NULL arg");

    for (j = 0; j < 8; ++j) {
        prev_l[j] = -1;
        prev_u[j] = -1;
    }

    max_l = MIN(max_bit, 8);
    max_u = MAX((max_bit - 8), 0);
    max_u = MIN(max_u, 8);

    /* Step through each 16-bit sample in the buffer one at a time. */
    for (i = 0; i < (bufend - buf) / 2; ++i) {

        l_src_byte = buf[i*2];
        u_src_byte = buf[i*2+1];
        if (snd_format == SND_PCM_FORMAT_S16_BE) {
            l_src_byte = buf[i*2+1];
            u_src_byte = buf[i*2];
        }

        /* Process lower bits */
        for (j = 0; j < max_l; ++j) {
            /* Select the bit of given significance. */
            new = (l_src_byte >> j) & 1;

            /* We've not yet collected two bits; move on. */
            if (prev_l[j] == -1) {
                prev_l[j] = new;
                continue;
            }

            /* If the bits are equal, discard both. */
            if (prev_l[j] == new) {
                prev_l[j] = -1;
                continue;
            }

            /* If 10, mark the bit as 1.  Otherwise, it's 01 and the bit
             * is already marked as 0. */
            if (prev_l[j])
                byte_out |= 1 << bits_out;
            bits_out++;
            prev_l[j] = -1;

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

        /* Process upper bits if necessary. */
        for (j = 0; j < max_u; ++j) {
            /* Select the bit of given significance. */
            new = (u_src_byte >> j) & 1;

            /* We've not yet collected two bits; move on. */
            if (prev_u[j] == -1) {
                prev_u[j] = new;
                continue;
            }

            /* If the bits are equal, discard both. */
            if (prev_u[j] == new) {
                prev_u[j] = -1;
                continue;
            }

            /* If 10, mark the bit as 1.  Otherwise, it's 01 and the bit
             * is already marked as 0. */
            if (prev_u[j])
                byte_out |= 1 << bits_out;
            bits_out++;
            prev_u[j] = -1;

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

static void get_random_data(int process_samples)
{
    int n_to_do, total_in = 0, total_out = 0, bufsize, err;
    char *buf, *bufp;
    snd_pcm_t *chandle;
    snd_pcm_sframes_t garbage_frames;

    if ((err = snd_pcm_open(&chandle, cdevice, SND_PCM_STREAM_CAPTURE, 0)) < 0)
        suicide("Record open error: %s", snd_strerror(err));

    /* Open and set up ALSA device for reading */
    alsa_setparams(chandle);

    log_line(LOG_DEBUG, "get_random_data(%d)", process_samples);

    bufsize = snd_pcm_frames_to_bytes(chandle,
                                      MAX(skip_samples, process_samples));
    buf = xmalloc(bufsize);
    log_line(LOG_DEBUG, "Input buffer size: %d bytes", bufsize);

    if (skip_samples) {
        /* Discard the first data read it often contains weird looking
         * data - probably a click from driver loading or card initialization.
         */
        garbage_frames = snd_pcm_readi(chandle, buf, skip_samples);

        /* Make sure we aren't hitting a disconnect/suspend case */
        if (garbage_frames < 0)
            snd_pcm_recover(chandle, garbage_frames, 0);
        /* Nope, something else is wrong. Bail. */
        if (garbage_frames < 0)
            suicide("Get random data: read error: %m");
    }

    /* Read a buffer of audio */
    n_to_do = process_samples;
    bufp = buf;
    while (n_to_do > 0) {
        snd_pcm_sframes_t frames_read = snd_pcm_readi(chandle, bufp, n_to_do);
        /* Make sure we aren't hitting a disconnect/suspend case */
        if (frames_read < 0)
            frames_read = snd_pcm_recover(chandle, frames_read, 0);
        /* Nope, something else is wrong. Bail. */
        if (frames_read < 0)
            suicide("Read error: %m");
        if (frames_read == -1) {
            if (errno != EINTR)
                suicide("Read error: %m");
        }
        else {
            n_to_do -= frames_read;
            bufp += frames_read;
        }
    }

    snd_pcm_close(chandle);

    total_in = snd_pcm_frames_to_bytes(chandle, (bufp - buf));
    total_out = process_input(buf, bufp);

    log_line(LOG_DEBUG, "Input bytes: %d ; output bytes: %d", total_in,
             total_out);
    log_line(LOG_DEBUG, "get_random_data() finished");

    free(buf);
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

