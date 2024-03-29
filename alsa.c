// Copyright 2008-2014 Nicholas J. Kain <njkain at gmail dot com>
// SPDX-License-Identifier: MIT
#include <stdbool.h>
#include <alsa/asoundlib.h>
#include <linux/soundcard.h>
#include "nk/log.h"
#include "defines.h"
#include "sound.h"

extern bool gflags_debug;

static char *cdevice = DEFAULT_HW_DEVICE;
static const char *cdev_id = DEFAULT_HW_ITEM;
static unsigned int sample_rate = DEFAULT_SAMPLE_RATE;
static snd_pcm_t *pcm_handle = 0;
static size_t pcm_bytes_per_frame;
static int snd_format = -1;
static unsigned int skip_bytes = DEFAULT_SKIP_BYTES;
static int pcm_can_pause;

void sound_open(void)
{
    char buf[PAGE_SIZE];
    int err;
    snd_pcm_hw_params_t *ct_params;

    if ((err = snd_pcm_open(&pcm_handle, cdevice, SND_PCM_STREAM_CAPTURE, 0)) < 0)
        suicide("Error opening PCM device %s: %s\n", cdevice, snd_strerror(err));

    snd_pcm_hw_params_alloca(&ct_params);

    err = snd_pcm_hw_params_any(pcm_handle, ct_params);
    if (err < 0)
        suicide("Broken configuration for %s PCM: no configurations available: %s\n",
                   cdev_id, snd_strerror(err));

    /* Disable rate resampling */
    err = snd_pcm_hw_params_set_rate_resample(pcm_handle, ct_params, 0);
    if (err < 0)
        suicide("Could not disable rate resampling: %s\n", snd_strerror(err));

    /* Set access to SND_PCM_ACCESS_RW_INTERLEAVED -- NONINTERLEAVED would
     * be preferable, but it's uncommon on sound cards.*/
    err = snd_pcm_hw_params_set_access(pcm_handle, ct_params,
                                       SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err < 0)
        suicide("Could not set access to SND_PCM_ACCESS_RW_INTERLEAVED: %s\n",
                   snd_strerror(err));

    /* Choose rate nearest to our target rate */
    err = snd_pcm_hw_params_set_rate_near(pcm_handle, ct_params, &sample_rate, 0);
    if (err < 0)
        suicide("Rate %iHz not available for %s: %s\n",
                   sample_rate, cdev_id, snd_strerror(err));

    /* Set sample format -- prefer endianness equal to that of the CPU */
#ifdef HOST_ENDIAN_BE
    snd_format = SND_PCM_FORMAT_S16_BE;
#else
    snd_format = SND_PCM_FORMAT_S16_LE;
#endif
    err = snd_pcm_hw_params_set_format(pcm_handle, ct_params, snd_format);
    if (err < 0) {
#ifdef HOST_ENDIAN_BE
        snd_format = SND_PCM_FORMAT_S16_LE;
#else
        snd_format = SND_PCM_FORMAT_S16_BE;
#endif
        err = snd_pcm_hw_params_set_format(pcm_handle, ct_params, snd_format);
    }
    if (err < 0)
        suicide("Sample format (SND_PCM_FORMAT_S16_BE and _LE) not available for %s: %s\n",
                   cdev_id, snd_strerror(err));

    /* Set stereo for faster sampling. */
    err = snd_pcm_hw_params_set_channels(pcm_handle, ct_params, 2);
    if (err < 0)
        suicide("Channels count (%i) not available for %s: %s\n",
                   2, cdev_id, snd_strerror(err));

    /* Apply settings to sound device */
    err = snd_pcm_hw_params(pcm_handle, ct_params);
    if (err < 0)
        suicide("Could not apply settings to sound device!\n");

    ssize_t tbpf = snd_pcm_frames_to_bytes(pcm_handle, 1);
    if (tbpf > 0)
        pcm_bytes_per_frame = (size_t)tbpf;
    else
        suicide("pcm_bytes_per_frame would be zero or negative!\n");
    if (gflags_debug) log_line("bytes-per-frame: %zu\n", pcm_bytes_per_frame);
    pcm_can_pause = snd_pcm_hw_params_can_pause(ct_params);

    /* Discard the initial data; it may be a click or something else odd. */
    size_t got_bytes = 0;
    while (got_bytes < skip_bytes)
        got_bytes += sound_read(buf, sizeof buf);
    log_line("discarded first %zu bytes of pcm input\n", got_bytes);

    if (pcm_can_pause) {
        sound_stop();
        if (gflags_debug) log_line("alsa device supports pcm pause\n");
    }
}

size_t sound_bytes_per_frame(void)
{
    return pcm_bytes_per_frame;
}

unsigned sound_read(void *buf, size_t size)
{
    snd_pcm_sframes_t fr;

    fr = snd_pcm_readi(pcm_handle, buf, size / pcm_bytes_per_frame);
    /* Make sure we aren't hitting a disconnect/suspend case */
    if (fr < 0)
        fr = snd_pcm_recover(pcm_handle, fr, 0);
    /* Nope, something else is wrong. Bail. */
    if (fr < 0 || (fr == -1 && errno != EINTR))
        suicide("get_random_data(): Read error: %s\n", strerror(errno));
    return (unsigned)fr;
}

void sound_start(void)
{
    if (pcm_can_pause)
        snd_pcm_pause(pcm_handle, 0);
}

void sound_stop(void)
{
    if (pcm_can_pause)
        snd_pcm_pause(pcm_handle, 1);
}

void sound_close(void)
{
    snd_pcm_close(pcm_handle);
    pcm_handle = (snd_pcm_t *)0;
}

int sound_is_le(void)
{
    if (snd_format == SND_PCM_FORMAT_S16_BE)
        return 0;
    return 1;
}

int sound_is_be(void)
{
    if (snd_format == SND_PCM_FORMAT_S16_LE)
        return 0;
    return 1;
}

void sound_set_device(char *str)
{
    cdevice = strdup(str);
}

void sound_set_port(char *str)
{
    cdev_id = strdup(str);
}

void sound_set_sample_rate(int rate)
{
    if (rate > 0)
        sample_rate = (unsigned)rate;
    else
        sample_rate = DEFAULT_SAMPLE_RATE;
}

void sound_set_skip_bytes(int sb)
{
    if (sb > 0)
        skip_bytes = (unsigned)sb;
    else
        skip_bytes = DEFAULT_SKIP_BYTES;
}

