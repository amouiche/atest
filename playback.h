/*
 * Copyright (C) 2015 Arnaud Mouiche <arnaud.mouiche@invoxia.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef __playback_h__
#define __playback_h__

#include <poll.h>
#include <ev.h>

#include "test.h"
#include "seq.h"

struct playback_create_opts {
    int xrun;
    int restart_play_time;
    int restart_pause_time;
};



struct test_playback {
    struct test t;
    snd_pcm_t *pcm;
    struct seq_info seq;
    void *periof_buff;

    struct pollfd pollfd;
    struct ev_io io_watcher;
    struct ev_timer timer;

    struct playback_create_opts opts;
    enum playback_timer_state_e {
        PT_IDLE = 0,
        PT_W4_XRUN,
        PT_W4_XRUN_END,

        PT_W4_STOP,
        PT_W4_RESTART
    } timer_state;
};

struct test *playback_create(struct alsa_config *config, struct playback_create_opts *opts);

#endif //__playback_h__
