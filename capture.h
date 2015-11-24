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


struct test_capture {
    struct test t;
    snd_pcm_t *pcm;
    struct seq_info seq;
    void *periof_buff;

    struct pollfd pollfd;
    struct ev_io io_watcher;
    struct ev_timer timer;
};

struct test *capture_create(struct alsa_config *config);

#endif //__capture_h__
