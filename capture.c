/*
 * Copyright (C) 2015 Arnaud Mouiche <arnaud.mouiche@invoxia.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "capture.h"
#include "log.h"


static int capture_start(struct test *t) {
    dbg("capture_start");
    struct test_capture *tp = (struct test_capture *)t;
    int r = snd_pcm_start( tp->pcm );
    if (r < 0) {
        warn("%s: capture start failed: %s", tp->t.device, snd_strerror(r));
        return -1;
    } else {
        ev_io_start( loop, &tp->io_watcher );
        if (tp->opts.xrun) {
            dbg("%s: will simulate xrun every %d ms", tp->t.device, tp->opts.xrun);
            tp->timer_state = CT_W4_XRUN;
            ev_timer_set( &tp->timer, tp->opts.xrun * 1e-3, 0);
            ev_timer_start( loop, &tp->timer );
        } else if (tp->opts.restart_play_time && tp->opts.restart_pause_time) {
            dbg("%s: will stop every %d ms during %d ms", tp->t.device, tp->opts.restart_play_time, tp->opts.restart_pause_time);
            tp->timer_state = CT_W4_STOP;
            ev_timer_set( &tp->timer, tp->opts.restart_play_time * 1e-3, 0);
            ev_timer_start( loop, &tp->timer );
        }
    }

    return 0;
}



static void capture_timer( struct ev_loop *loop, struct ev_timer *w, int revents) {
    struct test_capture *tp = (struct test_capture *)(w->data);

    switch (tp->timer_state) {
    case CT_IDLE:
        break;
    case CT_W4_XRUN:
        warn("%s: force capture xrun", tp->t.device);
        /* simply stop handling the pcm handler during few ms */
        ev_io_stop( loop, &tp->io_watcher );
        tp->timer_state = CT_W4_XRUN_END;
        ev_timer_set( &tp->timer, 0.5, 0);
        ev_timer_start( loop, &tp->timer );
        break;

    case CT_W4_XRUN_END:
        warn("%s: CT_W4_XRUN_END", tp->t.device);
        ev_io_start( loop, &tp->io_watcher );
        tp->timer_state = CT_W4_XRUN;
        ev_timer_set( &tp->timer, tp->opts.xrun*1e-3, 0);
        ev_timer_start( loop, &tp->timer );
        break;

    case CT_W4_STOP:
        warn("%s: CT_W4_STOP", tp->t.device);
        snd_pcm_drop( tp->pcm );
        ev_io_stop( loop, &tp->io_watcher );
        tp->timer_state = CT_W4_RESTART;
        ev_timer_set( &tp->timer, tp->opts.restart_pause_time * 1e-3, 0);
        ev_timer_start( loop, &tp->timer );
        break;

    case CT_W4_RESTART: {
        int r;
        warn("%s: CT_W4_RESTART", tp->t.device);
        seq_check_jump_notify( &tp->seq );
        snd_pcm_prepare(tp->pcm);
        r = snd_pcm_start( tp->pcm );
        if (r >= 0) {
            ev_io_start( loop, &tp->io_watcher );
            tp->timer_state = CT_W4_STOP;
            ev_timer_set( &tp->timer, tp->opts.restart_play_time * 1e-3, 0);
            ev_timer_start( loop, &tp->timer );
        } else {
            err("%s: capture restart failure (%s)", tp->t.device, snd_strerror(r));
            ev_unloop(loop, EVUNLOOP_ALL);
        }
    } break;
    }

}



static void capture_io_job( struct ev_loop *loop, struct ev_io *w, int revents ) {

    struct test_capture *tp = (struct test_capture *)(w->data);
    snd_pcm_sframes_t frames;

    frames = snd_pcm_readi(tp->pcm, tp->periof_buff, tp->t.config.period);
    if (frames < 0) {
        int r;
        warn("%s: capture read failed: %s", tp->t.device, snd_strerror(frames));
        if (frames == -EBADFD) {
            err("unrecoverable alsa error");
            ev_unloop(loop, EVUNLOOP_ALL);
            return;
        }
        r = snd_pcm_recover(tp->pcm, frames, 0);
        if (r < 0) {
            err("%s: capture recover failed: %s", tp->t.device, snd_strerror(frames));
        }
        r = snd_pcm_start( tp->pcm );
        if (r < 0) {
            warn("%s: capture start failed after recover: %s", tp->t.device, snd_strerror(r));
            ev_unloop(loop, EVUNLOOP_ALL);
            return;
        }
        seq_check_jump_notify( &tp->seq );

    } else if (frames != tp->t.config.period) {
        err("%s: capture read less than the expected period size: %ld / %u", tp->t.device, frames, tp->t.config.period);

    } else {
        /* check the sequence */
        seq_check_frames( &tp->seq, tp->periof_buff, tp->t.config.period );
    }
}



static int capture_close(struct test *t) {
    struct test_capture *tp = (struct test_capture *)t;

    ev_io_stop(loop, &tp->io_watcher);
    snd_pcm_close( tp->pcm );

    free( tp->periof_buff );
    free( tp );
    return 0;
}



const struct test_ops capture_ops = {
        .start = capture_start,
        .close = capture_close,
};

/*
 * do a capture test as specified by 'pt'
 * Stop the capture on stdin EPIPE or on signal
 */
struct test *capture_create(struct alsa_config *config, struct capture_create_opts *opts) {
    struct test_capture *tp = calloc( 1, sizeof(*tp));
    int r;

    if (!tp) return NULL;

    tp->t.name = "capture";
    memcpy( &tp->t.config, config, sizeof(*config));
    memcpy( tp->t.device, config->device, sizeof(tp->t.device) );
    tp->opts = *opts;

    r = alsa_device_open( tp->t.config.device, &tp->t.config, &tp->pcm, NULL );
    if (r) goto failed1;

    seq_init( &tp->seq, tp->t.config.channels, tp->t.config.format );
    tp->periof_buff = malloc( snd_pcm_frames_to_bytes( tp->pcm, tp->t.config.period ));
    if (!tp->periof_buff) goto failed;

    r = snd_pcm_poll_descriptors_count(tp->pcm);
    if (r != 1) {
        err("capture_create: expect only 1 fd to monitor (snd_pcm_poll_descriptors_count)");
        goto failed;
    }

    r = snd_pcm_poll_descriptors(tp->pcm, &tp->pollfd, 1);
    dbg("snd_pcm_poll_descriptors %d", r);
    if (r < 0) {
        err("%s: snd_pcm_poll_descriptors failed", tp->t.device);
        goto failed;
    }

    ev_io_init( &tp->io_watcher, capture_io_job,
            tp->pollfd.fd,
            ((tp->pollfd.events & POLLIN) ? EV_READ : 0) |
            ((tp->pollfd.events & POLLOUT) ? EV_WRITE : 0)
            );
    tp->io_watcher.data = tp;
    ev_timer_init( &tp->timer, capture_timer, 0, 0 );
    tp->timer.data = tp;

    tp->t.ops = &capture_ops;

    return &tp->t;

failed:
    snd_pcm_close( tp->pcm );
    free(tp->periof_buff);
failed1:
    free(tp);
    return NULL;
}



