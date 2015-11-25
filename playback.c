/*
 * Copyright (C) 2015 Arnaud Mouiche <arnaud.mouiche@invoxia.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */


#include "playback.h"
#include "log.h"


/*
 * feed the PCM with new samples
 */
static void playback_io_job( struct ev_loop *loop, struct ev_io *w, int revents ) {

    struct test_playback *tp = (struct test_playback *)(w->data);

    /* simply fill a first period */
    seq_fill_frames( &tp->seq, tp->periof_buff, tp->t.config.period );
    snd_pcm_sframes_t frames = snd_pcm_writei(tp->pcm, tp->periof_buff, tp->t.config.period);

    if (frames < 0) {
        warn("%s: playback write failed: %s", tp->t.device, snd_strerror(frames));
        if (frames == -EBADFD) {
            err("unrecoverable alsa error");
            ev_unloop(loop, EVUNLOOP_ALL);
            return;
        }
        snd_pcm_recover(tp->pcm, frames, 0);

        /* write again the period to start the stream again */
        frames = snd_pcm_writei(tp->pcm, tp->periof_buff, tp->t.config.period);
        if (frames < 0) {
            err("%s: playback write failed after recover: %s", tp->t.device, snd_strerror(frames));
            ev_unloop(loop, EVUNLOOP_ALL);
            return;
        }
    } else if (frames != tp->t.config.period) {
        err("%s: playback write less than the expected period size: %ld / %u", tp->t.device, frames, tp->t.config.period);

    }
    return;
}


static void playback_timer( struct ev_loop *loop, struct ev_timer *w, int revents) {
    struct test_playback *tp = (struct test_playback *)(w->data);

    switch (tp->timer_state) {
    case PT_IDLE:
        break;
    case PT_W4_XRUN:
        warn("%s: force playback xrun", tp->t.device);
        /* simply stop handling the pcm handler during few ms */
        ev_io_stop( loop, &tp->io_watcher );
        tp->timer_state = PT_W4_XRUN_END;
        ev_timer_set( &tp->timer, 0.5, 0);
        ev_timer_start( loop, &tp->timer );
        break;

    case PT_W4_XRUN_END:
        warn("%s: PT_W4_XRUN_END", tp->t.device);
        ev_io_start( loop, &tp->io_watcher );
        tp->timer_state = PT_W4_XRUN;
        ev_timer_set( &tp->timer, tp->opts.xrun*1e-3, 0);
        ev_timer_start( loop, &tp->timer );
        break;

    case PT_W4_STOP:
        warn("%s: PT_W4_STOP", tp->t.device);
        snd_pcm_drop( tp->pcm );
        ev_io_stop( loop, &tp->io_watcher );
        tp->timer_state = PT_W4_RESTART;
        ev_timer_set( &tp->timer, tp->opts.restart_pause_time * 1e-3, 0);
        ev_timer_start( loop, &tp->timer );
        break;

    case PT_W4_RESTART: {
        warn("%s: PT_W4_RESTART", tp->t.device);
        /* simply fill a first period */
        seq_fill_frames( &tp->seq, tp->periof_buff, tp->t.config.period );
        snd_pcm_prepare(tp->pcm);
        snd_pcm_sframes_t frames = snd_pcm_writei(tp->pcm, tp->periof_buff, tp->t.config.period);
        if (frames > 0) {
            ev_io_start( loop, &tp->io_watcher );
            tp->timer_state = PT_W4_STOP;
            ev_timer_set( &tp->timer, tp->opts.restart_play_time * 1e-3, 0);
            ev_timer_start( loop, &tp->timer );
        } else {
            err("%s: playback restart failure (%s)", tp->t.device, snd_strerror(frames));
            ev_unloop(loop, EVUNLOOP_ALL);
        }
    } break;
    }

}

static int playback_start(struct test *t) {
    struct test_playback *tp = (struct test_playback *)t;
    /* simply fill a first period */
    dbg("%s: playback_start", tp->t.device);
    seq_fill_frames( &tp->seq, tp->periof_buff, tp->t.config.period );
    snd_pcm_sframes_t frames = snd_pcm_writei(tp->pcm, tp->periof_buff, tp->t.config.period);

    if (frames > 0) {
        ev_io_start( loop, &tp->io_watcher );
        if (tp->opts.xrun) {
            dbg("%s: will simulate xrun every %d ms", tp->t.device, tp->opts.xrun);
            tp->timer_state = PT_W4_XRUN;
            ev_timer_set( &tp->timer, tp->opts.xrun * 1e-3, 0);
            ev_timer_start( loop, &tp->timer );
        } else if (tp->opts.restart_play_time && tp->opts.restart_pause_time) {
            dbg("%s: will stop every %d ms during %d ms", tp->t.device, tp->opts.restart_play_time, tp->opts.restart_pause_time);
            tp->timer_state = PT_W4_STOP;
            ev_timer_set( &tp->timer, tp->opts.restart_play_time * 1e-3, 0);
            ev_timer_start( loop, &tp->timer );
        }

    } else {
        err("%s: playback_start failure (%s)", tp->t.device, snd_strerror(frames));
        ev_unloop(loop, EVUNLOOP_ALL);
    }


    return frames > 0 ? 0 : -1;
}

static int playback_close(struct test *t) {
    struct test_playback *tp = (struct test_playback *)t;

    ev_io_stop( loop, &tp->io_watcher );
    ev_timer_stop( loop, &tp->timer );
    snd_pcm_close( tp->pcm );

    free( tp->periof_buff );
    free( tp );
    return 0;
}



const struct test_ops playback_ops = {
        .start = playback_start,
        .close = playback_close,
};


/*
 * do a playback test as specified by 'pt'
 * Stop the playback on stdin EPIPE or on signal
 */
struct test *playback_create(struct alsa_config *config, struct playback_create_opts *opts) {
    struct test_playback *tp = calloc( 1, sizeof(*tp));
    int r;

    if (!tp) return NULL;

    tp->t.name = "playback";
    tp->opts = *opts;
    memcpy( &tp->t.config, config, sizeof(*config));
    memcpy( tp->t.device, config->device, sizeof(tp->t.device) );

    r = alsa_device_open( tp->t.config.device, &tp->t.config, NULL, &tp->pcm );
    if (r) goto failed1;

    seq_init( &tp->seq, tp->t.config.channels, tp->t.config.format );
    tp->periof_buff = malloc( snd_pcm_frames_to_bytes( tp->pcm, tp->t.config.period ));
    if (!tp->periof_buff) goto failed;

    r = snd_pcm_poll_descriptors_count(tp->pcm);
    if (r != 1) {
        err("playback_create: expect only 1 fd to monitor (snd_pcm_poll_descriptors_count)");
        goto failed;
    }

    r = snd_pcm_poll_descriptors(tp->pcm, &tp->pollfd, 1);
    if (r < 0) {
        err("%s: snd_pcm_poll_descriptors failed", tp->t.device);
        goto failed;
    }

    ev_io_init( &tp->io_watcher, playback_io_job,
            tp->pollfd.fd,
            ((tp->pollfd.events & POLLIN) ? EV_READ : 0) |
            ((tp->pollfd.events & POLLOUT) ? EV_WRITE : 0)
            );
    tp->io_watcher.data = tp;
    ev_timer_init( &tp->timer, playback_timer, 0, 0 );
    tp->timer.data = tp;

    tp->t.ops = &playback_ops;

    return &tp->t;

failed:
    snd_pcm_close( tp->pcm );
    free(tp->periof_buff);
failed1:
    free(tp);
    return NULL;
}

