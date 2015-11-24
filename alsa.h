/*
 * Copyright (C) 2015 Arnaud Mouiche <arnaud.mouiche@invoxia.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef __alsa_h__
#define __alsa_h__


#include <alsa/asoundlib.h>


struct alsa_config {

    char device[64];

    unsigned int channels;
    unsigned int rate;
    snd_pcm_format_t format;

    unsigned int period;
    unsigned int buffer_period_count;

    /* set to 1 to open the capture and playback in linked mode */
    unsigned linking_capture_playback;


    /*
     * scheduler priority to use
     * fifo,N  => SCHED_FIFO, priority N
     * rr,N    => SCHED_RR, priority N
     * other,N => SCHED_OTHER, priority N
     *
     * *empty* => no change of the scheduling priority
     */
    char priority[32];

};



/*
 * setup the config according to the default values:
 * by order:
 * - check the presence of a file $(pwd)/atest.conf, ~/.atest.conf, /etc/atest.conf (if config_path is not provided)
 * - otherwise use static defaults:
 *    channels = 2
 *    rate = 48000
 *    period = 960  (20ms)
 *    buffer_period_count = 2
 *    format = S16_LE
 *
 *    linking_capture_playback = 0
 *
 *
 */
void alsa_config_init( struct alsa_config *config, const char *config_path );



void alsa_config_dump( struct alsa_config *config );



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
