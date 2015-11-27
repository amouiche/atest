/*
 * Copyright (C) 2015 Arnaud Mouiche <arnaud.mouiche@invoxia.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include "loopback_delay.h"
#include "log.h"


static int loopback_delay_start(struct test *t) {
    struct test_loopback_delay *tp = (struct test_loopback_delay *)t;
    int r;
    dbg("%s: loopback_delay_start", tp->t.device);

    /* first, prepare both streams at once */
    tp->delay_detected = 0;
    tp->measured_delay = 0;
    tp->exit_status = 1; /* consider the test as failed until the first valid frame is received */
    r = snd_pcm_prepare(tp->pcm_c);
    if (r < 0) {
        warn("%s: loopback_delay capture prepare failed: %s", tp->t.device, snd_strerror(r));
    }
    r = snd_pcm_prepare(tp->pcm_p);
    if (r < 0) {
        warn("%s: loopback_delay playback prepare failed: %s", tp->t.device, snd_strerror(r));
    }

    seq_fill_frames( &tp->seq_p, tp->periof_buff, tp->t.config.period );
    switch (tp->opts.start_sync_mode) {
    case LSM_PREPARE_CAPTURE_PLAYBACK:
        /* start the capture explicitly */
        dbg("start capture");
        r = snd_pcm_start( tp->pcm_c );
        if (r < 0) {
            warn("%s: loopback_delay start capture failed: %s", tp->t.device, snd_strerror(r));
            return -1;
        }
        /* playback is start by writing the first period */
        dbg("start playback");
        snd_pcm_sframes_t frames = snd_pcm_writei(tp->pcm_p, tp->periof_buff, tp->t.config.period);
        if (frames < 0) {
            warn("%s: loopback_delay start playback failed: %s", tp->t.device, snd_strerror(r));
            return -1;
        }
        break;

    case LSM_PREPARE_PLAYBACK_CAPTURE:
    case LSM_LINK:
    {
        /*
         * Start the playback first.
         * In link case, the capture should also start at once without extra operations.
         * Otherwise, explicit capture startup is required.
         */
        /* playback is start by writing the first period */
        dbg("start playback");
        snd_pcm_sframes_t frames = snd_pcm_writei(tp->pcm_p, tp->periof_buff, tp->t.config.period);
        if (frames < 0) {
            warn("%s: loopback_delay start playback failed: %s", tp->t.device, snd_strerror(r));
            return -1;
        }
        dbg("start capture");
        r = snd_pcm_start( tp->pcm_c );
        if (r < 0) {
            if (tp->opts.start_sync_mode == LSM_PREPARE_PLAYBACK_CAPTURE) {
                warn("%s: loopback_delay start capture failed: %s", tp->t.device, snd_strerror(r));
                return -1;
            } else {
                dbg("%s: loopback_delay start capture returns reason '%s' as expected",
                        tp->t.device, snd_strerror(r));
            }
        }
    } break;
    }

    ev_io_start( loop, &tp->io_watcher_p );
    ev_io_start( loop, &tp->io_watcher_c );
    return 0;
}




/*
 * feed the PCM with new samples
 */
static void loopback_delay_play_job( struct ev_loop *loop, struct ev_io *w, int revents ) {

    struct test_loopback_delay *tp = (struct test_loopback_delay *)(w->data);

    /* simply fill a first period */
    seq_fill_frames( &tp->seq_p, tp->periof_buff, tp->t.config.period );
    snd_pcm_sframes_t frames = snd_pcm_writei(tp->pcm_p, tp->periof_buff, tp->t.config.period);

    if (frames < 0) {
        warn("%s: loopback_delay write failed: %s", tp->t.device, snd_strerror(frames));
        if (frames == -EBADFD) {
            err("unrecoverable alsa error");
            ev_unloop(loop, EVUNLOOP_ALL);
            return;
        }
        snd_pcm_recover(tp->pcm_p, frames, 0);

        /* write again the period to start the stream again */
        frames = snd_pcm_writei(tp->pcm_p, tp->periof_buff, tp->t.config.period);
        if (frames < 0) {
            err("%s: loopback_delay write failed after recover: %s", tp->t.device, snd_strerror(frames));
            ev_unloop(loop, EVUNLOOP_ALL);
            return;
        }
    } else if (frames != tp->t.config.period) {
        err("%s: loopback_delay write less than the expected period size: %ld / %u", tp->t.device, frames, tp->t.config.period);

    }
    return;
}


static void loopback_delay_capture_job( struct ev_loop *loop, struct ev_io *w, int revents ) {

    struct test_loopback_delay *tp = (struct test_loopback_delay *)(w->data);
    snd_pcm_sframes_t frames;

    frames = snd_pcm_readi(tp->pcm_c, tp->periof_buff, tp->t.config.period);
    if (frames < 0) {
        int r;
        warn("%s: loopback_delay read failed: %s", tp->t.device, snd_strerror(frames));
        if (frames == -EBADFD) {
            err("unrecoverable alsa error");
            ev_unloop(loop, EVUNLOOP_ALL);
            return;
        }
        r = snd_pcm_recover(tp->pcm_c, frames, 0);
        if (r < 0) {
            err("%s: loopback_delay recover failed: %s", tp->t.device, snd_strerror(frames));
        }
        r = snd_pcm_start( tp->pcm_c );
        if (r < 0) {
            warn("%s: loopback_delay start failed after recover: %s", tp->t.device, snd_strerror(r));
            ev_unloop(loop, EVUNLOOP_ALL);
            return;
        }
        seq_check_jump_notify( &tp->seq_c );

    } else if (frames != tp->t.config.period) {
        err("%s: loopback_delay read less than the expected period size: %ld / %u", tp->t.device, frames, tp->t.config.period);

    } else {
        /* check the sequence */
        seq_check_frames( &tp->seq_c, tp->periof_buff, tp->t.config.period );
        if (!tp->delay_detected) {
            switch (tp->seq_c.state) {
            case NULL_FRAME:
                /* we received a full NULL frame. add period_size to the measured delay */
                tp->measured_delay += tp->t.config.period;
                break;
            case VALID_FRAME:
                /*
                 * this is the first period with valid frames received.
                 * seq_check_frames has been done and tp->seq_c.frame_num (A) now hold the
                 * expected number of the first frame we will receive in the future period.
                 *
                 * in a zero delay scenario, the first period sent in playback is equal to the
                 * first period received. Since the first frame of the first period has number #0
                 * the first of the second period will be #period_size, equal to (A)
                 *
                 * if the period is late, A < #period_size.
                 * For example, in case of a "period_size-1" delay, we have A=1 since only the frame #0
                 * will be receive at the end of the first period
                 */
                dbg("tp->seq_c.frame_num: %d", tp->seq_c.frame_num);
                tp->measured_delay += tp->t.config.period - tp->seq_c.frame_num;
                tp->delay_detected = 1;
                warn("measured_delay: %d", tp->measured_delay);
                if (tp->opts.assert_delay) {
                    if (tp->measured_delay != tp->opts.expected_delay) {
                        err("assert: delay %d doesn't match the expected one %d", tp->measured_delay, tp->opts.expected_delay);
                        tp->exit_status = 1;
                    } else {
                        warn("good loopback delay");
                        tp->exit_status = 0;
                    }
                } else {
                    tp->exit_status = 0;
                }
                break;
            case INVALID_FRAME:
                /* log for this frame was already generated by seq_check_frames() */
                tp->exit_status = 1;
                break;
            }
        }
    }
}



static int loopback_delay_close(struct test *t) {
    struct test_loopback_delay *tp = (struct test_loopback_delay *)t;
    int exit_status = tp->exit_status;

    ev_io_stop(loop, &tp->io_watcher_c);
    ev_io_stop(loop, &tp->io_watcher_p);
    snd_pcm_close( tp->pcm_c );
    snd_pcm_close( tp->pcm_p );

    free( tp->periof_buff );
    free( tp );
    return exit_status;
}



const struct test_ops loopback_delay_ops = {
        .start = loopback_delay_start,
        .close = loopback_delay_close,
};

/*
 * do a loopback_delay test:
 * - open capture and playback at once where is PCM BUS TX and RX should
 *   linked together.
 * - generate a sequence at the playback side,
 * - check the sequence at the received side and measure the loopback delay
 *   (how many frames the received side is in advanced or late.
 */
struct test *loopback_delay_create(struct alsa_config *config, struct loopback_delay_create_opts *opts) {
    struct test_loopback_delay *tp = calloc( 1, sizeof(*tp));
    int r;

    if (!tp) return NULL;

    tp->t.name = "loopback_delay";
    memcpy( &tp->t.config, config, sizeof(*config));
    memcpy( tp->t.device, config->device, sizeof(tp->t.device) );
    tp->opts = *opts;

    r = alsa_device_open( tp->t.config.device, &tp->t.config, &tp->pcm_c, &tp->pcm_p );
    if (r) goto failed1;

    seq_init( &tp->seq_c, tp->t.config.channels, tp->t.config.format );
    seq_init( &tp->seq_p, tp->t.config.channels, tp->t.config.format );
    tp->periof_buff = malloc( snd_pcm_frames_to_bytes( tp->pcm_c, tp->t.config.period ));
    if (!tp->periof_buff) goto failed;

    r = snd_pcm_poll_descriptors_count(tp->pcm_c);
    if (r != 1) {
        err("loopback_delay_create: expect only 1 fd to monitor (snd_pcm_poll_descriptors_count)");
        goto failed;
    }
    r = snd_pcm_poll_descriptors_count(tp->pcm_p);
    if (r != 1) {
        err("loopback_delay_create: expect only 1 fd to monitor (snd_pcm_poll_descriptors_count)");
        goto failed;
    }

    r = snd_pcm_poll_descriptors(tp->pcm_c, &tp->pollfd_c, 1);
    if (r < 0) {
        err("%s: snd_pcm_poll_descriptors (c) failed", tp->t.device);
        goto failed;
    }
    r = snd_pcm_poll_descriptors(tp->pcm_p, &tp->pollfd_p, 1);
    if (r < 0) {
        err("%s: snd_pcm_poll_descriptors (p) failed", tp->t.device);
        goto failed;
    }

    ev_io_init( &tp->io_watcher_c, loopback_delay_capture_job,
            tp->pollfd_c.fd,
            ((tp->pollfd_c.events & POLLIN) ? EV_READ : 0) |
            ((tp->pollfd_c.events & POLLOUT) ? EV_WRITE : 0)
            );
    tp->io_watcher_c.data = tp;

    ev_io_init( &tp->io_watcher_p, loopback_delay_play_job,
            tp->pollfd_p.fd,
            ((tp->pollfd_p.events & POLLIN) ? EV_READ : 0) |
            ((tp->pollfd_p.events & POLLOUT) ? EV_WRITE : 0)
            );
    tp->io_watcher_p.data = tp;

    tp->t.ops = &loopback_delay_ops;

    return &tp->t;

failed:
    snd_pcm_close( tp->pcm_p );
    snd_pcm_close( tp->pcm_c );
    free(tp->periof_buff);
failed1:
    free(tp);
    return NULL;
}



