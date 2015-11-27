/*
 * Copyright (C) 2015 Arnaud Mouiche <arnaud.mouiche@invoxia.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */


#ifndef __loopback_delay_h__
#define __loopback_delay_h__

#include <poll.h>
#include <ev.h>

#include "test.h"
#include "seq.h"

struct loopback_delay_create_opts {

    enum start_sync_mode_e {
        /*
         * both stream are prepared first, then the loopback_delay is start first
         * followed by the playback
         */
        LSM_PREPARE_CAPTURE_PLAYBACK = 0,

        /*
         * both stream are prepared first, then the playback is start first
         * followed by the loopback_delay.
         */
        LSM_PREPARE_PLAYBACK_CAPTURE,

        /*
         * use snd_pcm_link()
         */
        LSM_LINK,
    } start_sync_mode;

    /*
     * if assert_delay is not zero, a measured delay different from
     * expected_delay is considered as an error
     */
    int assert_delay;
    int expected_delay;

};


struct test_loopback_delay {
    struct test t;

    snd_pcm_t *pcm_p;
    snd_pcm_t *pcm_c;
    struct seq_info seq_p;
    struct seq_info seq_c;
    void *periof_buff;

    int delay_detected; /* true we have detected the delay */
    int measured_delay; /* valid if delay_detected is true */
    int exit_status;

    struct pollfd pollfd_p;
    struct pollfd pollfd_c;
    struct ev_io io_watcher_p;
    struct ev_io io_watcher_c;

    struct loopback_delay_create_opts opts;
};

struct test *loopback_delay_create(struct alsa_config *config, struct loopback_delay_create_opts *opts);

#endif //__loopback_delay_h__
