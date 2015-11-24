/*
 * Copyright (C) 2015 Arnaud Mouiche <arnaud.mouiche@invoxia.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */


#ifndef __capture_h__
#define __capture_h__

#include <poll.h>
#include <ev.h>

#include "test.h"
#include "seq.h"

struct capture_create_opts {
    int xrun;
    int restart_play_time;
    int restart_pause_time;
};


struct test_capture {
    struct test t;
    snd_pcm_t *pcm;
    struct seq_info seq;
    void *periof_buff;

    struct pollfd pollfd;
    struct ev_io io_watcher;
    struct ev_timer timer;

    struct capture_create_opts opts;
    enum capture_timer_state_e {
        CT_IDLE = 0,
        CT_W4_XRUN,
        CT_W4_XRUN_END,

        CT_W4_STOP,
        CT_W4_RESTART
    } timer_state;

};

struct test *capture_create(struct alsa_config *config, struct capture_create_opts *opts);

#endif //__capture_h__
