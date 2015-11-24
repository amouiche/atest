
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <poll.h>
#include <getopt.h>
#include <sched.h>
#include <ev.h>


#include "alsa.h"
#include "log.h"
#include "seq.h"



static struct ev_loop *loop;


struct test;

struct test_ops {
    int (*start)(struct test *t);
    int (*close)(struct test *t);
};

struct test {
    const char *name;
    char device[64];
    struct alsa_config config;

    const struct test_ops *ops;
};



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








/*
 * feed the PCM with new samples
 */
void playback_io_job( struct ev_loop *loop, struct ev_io *w, int revents ) {

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


void playback_timer( struct ev_loop *loop, struct ev_timer *w, int revents) {
    struct test_playback *tp = (struct test_playback *)(w->data);

    switch (tp->timer_state) {
    case IDLE:
        break;
    case W4_XRUN:
        warn("%s: force playback xrun", tp->t.device);
        /* simply stop handling the pcm handler during few ms */
        ev_io_stop( loop, &tp->io_watcher );
        tp->timer_state = W4_XRUN_END;
        ev_timer_set( &tp->timer, 0.1, 0);
        ev_timer_start( loop, &tp->timer );
        break;

    case W4_XRUN_END:
        warn("%s: W4_XRUN_END", tp->t.device);
        ev_io_start( loop, &tp->io_watcher );
        tp->timer_state = W4_XRUN;
        ev_timer_set( &tp->timer, tp->opts.xrun*1e-3, 0);
        ev_timer_start( loop, &tp->timer );
        break;

    case W4_STOP:
        warn("%s: W4_STOP", tp->t.device);
        snd_pcm_drop( tp->pcm );
        ev_io_stop( loop, &tp->io_watcher );
        tp->timer_state = W4_RESTART;
        ev_timer_set( &tp->timer, tp->opts.restart_pause_time * 1e-3, 0);
        ev_timer_start( loop, &tp->timer );
        break;

    case W4_RESTART: {
        warn("%s: W4_RESTART", tp->t.device);
        /* simply fill a first period */
        seq_fill_frames( &tp->seq, tp->periof_buff, tp->t.config.period );
        snd_pcm_prepare(tp->pcm);
        snd_pcm_sframes_t frames = snd_pcm_writei(tp->pcm, tp->periof_buff, tp->t.config.period);
        if (frames > 0) {
            ev_io_start( loop, &tp->io_watcher );
            tp->timer_state = W4_STOP;
            ev_timer_set( &tp->timer, tp->opts.restart_play_time * 1e-3, 0);
            ev_timer_start( loop, &tp->timer );
        } else {
            err("%s: playback restart failure (%s)", tp->t.device, snd_strerror(frames));
            ev_unloop(loop, EVUNLOOP_ALL);
        }
    } break;
    }

}

int playback_start(struct test *t) {
    struct test_playback *tp = (struct test_playback *)t;
    /* simply fill a first period */
    seq_fill_frames( &tp->seq, tp->periof_buff, tp->t.config.period );
    snd_pcm_sframes_t frames = snd_pcm_writei(tp->pcm, tp->periof_buff, tp->t.config.period);

    if (frames > 0) {
        ev_io_start( loop, &tp->io_watcher );
        if (tp->opts.xrun) {
            dbg("%s: will simulate xrun every %d ms", tp->t.device, tp->opts.xrun);
            tp->timer_state = W4_XRUN;
            ev_timer_set( &tp->timer, tp->opts.xrun * 1e-3, 0);
            ev_timer_start( loop, &tp->timer );
        } else if (tp->opts.restart_play_time && tp->opts.restart_pause_time) {
            dbg("%s: will stop every %d ms during %d ms", tp->t.device, tp->opts.restart_play_time, tp->opts.restart_pause_time);
            tp->timer_state = W4_STOP;
            ev_timer_set( &tp->timer, tp->opts.restart_play_time * 1e-3, 0);
            ev_timer_start( loop, &tp->timer );
        }

    } else {
        err("%s: playback_start failure (%s)", tp->t.device, snd_strerror(frames));
        ev_unloop(loop, EVUNLOOP_ALL);
    }


    return frames > 0 ? 0 : -1;
}

int playback_close(struct test *t) {
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
    dbg("snd_pcm_poll_descriptors %d", r);
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




struct test_capture {
    struct test t;
    snd_pcm_t *pcm;
    struct seq_info seq;
    void *periof_buff;

    struct pollfd pollfd;
    struct ev_io io_watcher;
    struct ev_timer timer;
};


enum capture_test {
    CT_CONTINUOUS,
    CT_PERIODIC,
    CST_XRUN,
};






int capture_start(struct test *t) {
    dbg("capture_start");
    struct test_capture *tp = (struct test_capture *)t;
    int r = snd_pcm_start( tp->pcm );
    if (r < 0) {
        warn("%s: capture start failed: %s", tp->t.device, snd_strerror(r));
        return -1;
    } else {
        ev_io_start( loop, &tp->io_watcher );
    }

    return 0;
}


void capture_io_job( struct ev_loop *loop, struct ev_io *w, int revents ) {

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
    } else if (frames != tp->t.config.period) {
        err("%s: capture read less than the expected period size: %ld / %u", tp->t.device, frames, tp->t.config.period);

    } else {
        /* check the sequence */
        int r = seq_check_frames( &tp->seq, tp->periof_buff, tp->t.config.period );
    }
}



int capture_close(struct test *t) {
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
struct test *capture_create(struct alsa_config *config) {
    struct test_capture *tp = calloc( 1, sizeof(*tp));
    int r;

    if (!tp) return NULL;

    tp->t.name = "capture";
    memcpy( &tp->t.config, config, sizeof(*config));
    memcpy( tp->t.device, config->device, sizeof(tp->t.device) );

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

    tp->t.ops = &capture_ops;

    return &tp->t;

failed:
    snd_pcm_close( tp->pcm );
    free(tp->periof_buff);
failed1:
    free(tp);
    return NULL;
}





/*
 * something to read from stdin
 * read one char at a time, even if this is not efficient.
 */
void on_stdin( struct ev_loop *loop, struct ev_io *w, int revents ) {
    static char pipecmd[128] = {0};
    char c;
    int r;
    r = read( 0, &c, 1 );
    if (r < 0) {
        /* read failed on stdin. time to exit */
        ev_unloop( loop, EVUNLOOP_ALL);
        return;
    }
    if (c != '\n') {
        /* queue into cmdpipe */
        int l = strlen(pipecmd);
        if (l < sizeof(pipecmd)-2) {
            pipecmd[l] = c;
            pipecmd[l+1] = '\0';
        }
    } else {
        /* execute the command */
        dbg("pipecmd: '%s'", pipecmd);
        if (!strcmp(pipecmd, "q")) {
            warn("quit");
            ev_unloop( loop, EVUNLOOP_ALL);
            return;
        }


        /* reset the pipecmd */
        pipecmd[0] = '\0';
    }

}









void usage(void) {
    puts(
        "usage: atest OPTIONS -- TEST [test options] ...\n"
        "OPTIONS:\n"
        "-r, --rate=#            sample rate\n"
        "-c, --channels=#        channels\n"
        "-D, --device=NAME       select PCM by name\n"
        "-C, --config=FILE       use this particular config file\n"
        "-P, --priority=PRIORITY process priority to set ('fifo,N' 'rr,N' 'other,N')\n"
        "\n"
        "TEST\n"
        "  play      continuously generate the sequence steam\n"
        "     options:  -x N    generate a xrun every N ms\n"
        "               -r N,M    stop after N ms of playback,  and restart after M ms\n"
        "\n"
        "  capture.continous\n"
        "  capture.periodic\n"
        "  capture.xrun\n"

        ""
        );
    exit(1);

}


const struct option options[] = {
    { "rate", 1, NULL, 'r' },
    { "channels", 1, NULL, 'c' },
    { "device", 1, NULL, 'D' },
    { "config", 1, NULL, 'C' },
    { "priority", 1, NULL, 'P' },
    { NULL, 0, NULL, 0 }
};

int main(int argc, char * const argv[]) {

    int result,i,r;
    int opt_index;
    int opt_rate = -1;
    int opt_channels = -1;
    const char *opt_device = NULL;
    const char *opt_config = NULL;
    const char *opt_priority = NULL;
    struct alsa_config config;
    struct ev_io stdin_watcher;

    loop = ev_default_loop(0);

    while (1) {
        if ((result = getopt_long( argc, argv, "r:c:D:C:P:", options, &opt_index )) == EOF) break;
        switch (result) {
        case '?':
            usage();
            break;
        case 'r':
            opt_rate = atoi(optarg);
            break;
        case 'c':
            opt_channels = atoi(optarg);
            break;
        case 'D':
            opt_device = optarg;
            break;
        case 'C':
            opt_config = optarg;
            break;
        case 'P':
            opt_priority = optarg;
            break;
        }
    }

    /* generate the config */
    alsa_config_init( &config, opt_config );
    if (opt_rate > 0) config.rate = opt_rate;
    if (opt_channels > 0) config.channels = opt_channels;
    if (opt_device) { strncpy( config.device, opt_device, sizeof(config.device)-1 ); config.device[ sizeof(config.device)-1 ] = '\0'; }
    if (opt_priority) { strncpy( config.priority, opt_priority, sizeof(config.priority)-1 ); config.priority[ sizeof(config.priority)-1 ] = '\0'; }

    /* check if the config is valid */
    if (config.device[0] == '\0') {
        printf("Undefined device.\n");
        exit(1);
    }

    dbg("dev: '%s'", config.device);

#define MAX_TESTS 2
    struct test *tests[MAX_TESTS];
    int tests_count = 0;

    /* build the tests objects */
    argc -= optind;
    argv += optind;

    while (argc) {
        struct test *t = NULL;
        if (!strcmp( argv[0], "play" )) {
            struct playback_create_opts opts = {0};
            optind = 1;
            while (1) {
                if ((result = getopt( argc, argv, "x:r:" )) == EOF) break;
                switch (result) {
                case '?':
                    printf("invalid option '%s' for test 'play'\n", optarg);
                    usage();
                    break;
                case 'x':
                    opts.xrun = atoi(optarg);
                    break;
                case 'r':
                    if (sscanf(optarg, "%d,%d", &opts.restart_play_time, &opts.restart_pause_time) != 2) {
                        printf("invalid value '%s' for test 'play' option '-r'\n", optarg);
                        usage();
                    }
                    dbg("%d,%d", opts.restart_play_time, opts.restart_pause_time);
                    break;
                }
            }
            argc -= optind-1;
            argv += optind-1;
            t = playback_create( &config, &opts );
            if (!t) {
                err("failed to create a playback test");
                exit(1);
            }
        } else if (!strcmp( argv[0], "capture" )) {
            t = capture_create( &config );
            if (!t) {
                err("failed to create a capture test");
                exit(1);
            }
        }

        if (t) {
            if (tests_count >= MAX_TESTS) {
                err("too many tests defined.");
                exit(1);
            }
            tests[tests_count++] = t;
        } else {
            printf("undefined test '%s'.\n", argv[0]);
            usage();
        }
        argc--;
        argv++;
    }
    if (tests_count == 0) {
        printf("no tests specified.\n");
        exit(1);
    }

    /* change the scheduling priority is required */
    if (config.priority[0]) {
        int p;
        if (sscanf(config.priority, "fifo,%d", &p )==1) {
            struct sched_param param;
            param.sched_priority = p;

            dbg("priority: fifo,%d", p);
            if (sched_setscheduler(0, SCHED_FIFO, &param))
               err("sched_setscheduler");
        } else if (sscanf(config.priority, "rr,%d", &p )==1) {
            struct sched_param param;
            param.sched_priority = p;

            dbg("priority: rr,%d", p);
            if (sched_setscheduler(0, SCHED_RR, &param))
                err("sched_setscheduler");
        } else if (sscanf(config.priority, "other,%d", &p )==1) {
            struct sched_param param;
            param.sched_priority = p;

            dbg("priority: other,%d", p);
            if (sched_setscheduler(0, SCHED_OTHER, &param))
                err("sched_setscheduler");
        } else {
            printf("Invalid priority '%s'\n", config.priority);
        }
    }


    /* start the various tests */
    for (i=0; i < tests_count; i++) {
        struct test *t = tests[i];
        r = t->ops->start( t );
        if (r < 0) {
            err("starting test %s failed", t->name );
            exit(1);
        }
    }

    ev_io_init(&stdin_watcher, on_stdin, 0, EV_READ);
    ev_io_start( loop, &stdin_watcher );

    ev_run( loop, 0 );

    for (i=0; i < tests_count; i++) {
        struct test *t = tests[i];
        t->ops->close( t );
    }

    return 0;
}
