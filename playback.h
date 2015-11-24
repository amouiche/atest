
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
    enum timer_state_e {
        IDLE = 0,
        W4_XRUN,
        W4_XRUN_END,

        W4_STOP,
        W4_RESTART
    } timer_state;
};

struct test *playback_create(struct alsa_config *config, struct playback_create_opts *opts);

#endif //__playback_h__
