

#ifndef __alsa_h__
#define __alsa_h__




struct alsa_config {

    unsigned int channels;
    unsigned int rate;

    unsigned int period;
    unsigned int buffer_period_count;

    /* set to 1 to open the capture and playback in linked mode */
    unsigned linking_capture_playback;

};





/*
 * Open an alsa device for capture and/or playback.
 * - if capture_handle is not NULL, open for capture and fill *capture_handle with a valid PCM handle.
 * - if playback_handle is not NULL, open for playback and fill *playback_handle with a valid PCM handle.
 *
 * use 'config' and try to use the provided parameters to setup the streams.
 * Parameters (rate, period) can be modified to match the possibilities of the hardware.
 *
 * return 0 on success
 */
int alsa_device_open( const char *device, struct alsa_config *config,
        snd_pcm_t **capture_handle, snd_pcm_t **playback_handle );



#endif //__alsa_h__
