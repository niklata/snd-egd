#include <alsa/asoundlib.h>
#include <linux/soundcard.h>

#include "defines.h"
#include "log.h"

static char *cdevice = DEFAULT_HW_DEVICE;
static const char *cdev_id = DEFAULT_HW_ITEM;
static unsigned int sample_rate = DEFAULT_SAMPLE_RATE;
static snd_pcm_t *pcm_handle;
static size_t pcm_bytes_per_frame;
static int snd_format = -1;

void sound_open(void)
{
    int err;
    snd_pcm_hw_params_t *ct_params;


    if ((err = snd_pcm_open(&pcm_handle, cdevice, SND_PCM_STREAM_CAPTURE, 0)) < 0)
        suicide("Error opening PCM device %s: %s", cdevice, snd_strerror(err));

    snd_pcm_hw_params_alloca(&ct_params);

    err = snd_pcm_hw_params_any(pcm_handle, ct_params);
    if (err < 0)
        suicide("Broken configuration for %s PCM: no configurations available: %s",
                   cdev_id, snd_strerror(err));

    /* Disable rate resampling */
    err = snd_pcm_hw_params_set_rate_resample(pcm_handle, ct_params, 0);
    if (err < 0)
        suicide("Could not disable rate resampling: %s", snd_strerror(err));

    /* Set access to SND_PCM_ACCESS_RW_INTERLEAVED */
    err = snd_pcm_hw_params_set_access(pcm_handle, ct_params,
                                       SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err < 0)
        suicide("Could not set access to SND_PCM_ACCESS_RW_INTERLEAVED: %s",
                   snd_strerror(err));

    /* Choose rate nearest to our target rate */
    err = snd_pcm_hw_params_set_rate_near(pcm_handle, ct_params, &sample_rate, 0);
    if (err < 0)
        suicide("Rate %iHz not available for %s: %s",
                   sample_rate, cdev_id, snd_strerror(err));

    /* Set sample format */
#ifdef HOST_ENDIAN_LE
    snd_format = SND_PCM_FORMAT_S16_LE;
#else
    snd_format = SND_PCM_FORMAT_S16_BE;
#endif
    err = snd_pcm_hw_params_set_format(pcm_handle, ct_params, snd_format);
    if (err < 0) {
#ifdef HOST_ENDIAN_LE
        snd_format = SND_PCM_FORMAT_S16_BE;
#else
        snd_format = SND_PCM_FORMAT_S16_LE;
#endif
        err = snd_pcm_hw_params_set_format(pcm_handle, ct_params, snd_format);
    }
    if (err < 0)
        suicide("Sample format (SND_PCM_FORMAT_S16_BE and _LE) not available for %s: %s",
                   cdev_id, snd_strerror(err));

    /* Set stereo */
    err = snd_pcm_hw_params_set_channels(pcm_handle, ct_params, 2);
    if (err < 0)
        suicide("Channels count (%i) not available for %s: %s",
                   2, cdev_id, snd_strerror(err));

    /* Apply settings to sound device */
    err = snd_pcm_hw_params(pcm_handle, ct_params);
    if (err < 0)
        suicide("Could not apply settings to sound device!");

    pcm_bytes_per_frame = snd_pcm_frames_to_bytes(pcm_handle, 1);
}

void sound_read(char *buf, size_t size)
{
    snd_pcm_sframes_t fr;

    fr = snd_pcm_readi(pcm_handle, buf, size / pcm_bytes_per_frame);
    /* Make sure we aren't hitting a disconnect/suspend case */
    if (fr < 0)
        fr = snd_pcm_recover(pcm_handle, fr, 0);
    /* Nope, something else is wrong. Bail. */
    if (fr < 0 || (fr == -1 && errno != EINTR))
        suicide("get_random_data(): Read error: %m");
}

void sound_close(void)
{
    snd_pcm_close(pcm_handle);
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
    cdevice = strdup(optarg);
}

void sound_set_port(char *str)
{
    cdev_id = strdup(optarg);
}

void sound_set_sample_rate(int rate)
{
    if (rate > 0)
        sample_rate = rate;
    else
        sample_rate = DEFAULT_SAMPLE_RATE;
}

