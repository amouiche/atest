/*
 * Copyright (C) 2015 Arnaud Mouiche <arnaud.mouiche@invoxia.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <alsa/asoundlib.h>

#include "seq.h"
#include "log.h"

unsigned seq_errors_total = 0;
void (*seq_error_notify)(void) = NULL;
unsigned seq_consecutive_invalid_frames_log = 1;
unsigned seq_max_consecutive_invalid_frames_before_null_warning = 4;

#define FRAME_NUM_MASK   0x7FF
#define FRAME_NUM_SHIFT  5
#define CHANNEL_MASK     0x1F  /* up to 32 channels */

void seq_init( struct seq_info *seq, unsigned channels, snd_pcm_format_t format )
{
    memset( seq, 0, sizeof(*seq));
    seq->channels = channels;
    seq->format = format;
    seq->frame_num = 1; /* start at 1 to avoid sending 0000 as a first sample */
}


void seq_fill_frames( struct seq_info *seq, void *buff, int frame_count ) {
    int16_t *s16;

    switch (seq->format) {
    case SND_PCM_FORMAT_S16_LE:
        s16 = (int16_t *)buff;
        while (frame_count--) {
            int ch;
            for (ch = 0; ch < seq->channels; ch++) {
                *s16++ = (ch & CHANNEL_MASK) | ((seq->frame_num & FRAME_NUM_MASK) << FRAME_NUM_SHIFT);
            }
            seq->frame_num++;
        }
        break;

    default:
        /* format not implemented yet */
        break;
    }
}

/*
 * compare the frame with a Null frame (full of 0x00 or 0xFF)
 * return 1 if this is the case, 0 otherwise
 */
static int is_null_frame( const void *frame, int byte_size ) {
    const unsigned char *b = (const unsigned char *)frame;
    while (byte_size-- > 0) {
        if ((*b != 0) && (*b != 0xFF)) return 0;
        b++;
    }
    return 1;
}


/*
 * log the frame content
 */
static void log_frame( enum log_level level, struct seq_info *seq, const void *frame ) {
    const int16_t *s16 = (const int16_t *)frame;
    int ch;
    char line[16*10];
    int pos;

    strcpy( line, "  "); /* indentation */
    pos = strlen(line);
    for (ch = 0; ch < seq->channels; ch++) {
        if (pos < sizeof(line)-1)
            pos += snprintf(line + pos, sizeof(line) - pos - 1, "%04x ", (unsigned)(*s16) & 0xFFFF);
        s16++;
    }
    log( level, "%s", line);
}


void seq_check_jump_notify( struct seq_info *seq ) {
    seq->state = NULL_FRAME;
    seq->frame_num = 0;
}

int seq_check_frames( struct seq_info *seq, const void *buff, int frame_count ) {
    const int16_t *s16;
    s16 = (const int16_t *)buff;

    int frame_byte_size = seq->channels * sizeof(int16_t);
    unsigned current_frame_seq;
    int errors = 0;

    while (frame_count--) {
        /* what kind of frame is it */
        enum seq_stat_e next_state;
        if (is_null_frame( s16, frame_byte_size )) {
            next_state = NULL_FRAME;
        } else {
            int ch;
            const int16_t *s = s16;
            /* check samples one by one */
            next_state = VALID_FRAME;
            current_frame_seq = (*s16 >> FRAME_NUM_SHIFT) & FRAME_NUM_MASK;
            for (ch=0; ch < seq->channels; ch++) {
                if (((*s & CHANNEL_MASK) != ch) || (current_frame_seq != ((*s >> FRAME_NUM_SHIFT) & FRAME_NUM_MASK))) {
                    next_state = INVALID_FRAME;
                    break;
                }
                s++;
            }
        }

        if (seq->state == next_state) {
            switch (seq->state) {
            case NULL_FRAME:
                /* simply increase the record count of those frames */
                seq->frame_num++;
                break;

            case INVALID_FRAME:
                /* simply increase the record count of those frames */
                seq->frame_num++;
                if ((seq->frame_num <= seq_max_consecutive_invalid_frames_before_null_warning) && (seq->prev_state == VALID_FRAME)) {
                    log_frame( LOG_WARN, seq, s16 );
                } else {
                    if (seq->frame_num <= (seq_consecutive_invalid_frames_log+1)) {
                        log_frame( LOG_ERR, seq, s16 );
                    }
                    errors++;
                    seq->error_count++;
                    seq_errors_total++;
                }
                break;
            case VALID_FRAME:
                /* check the frame sequence to see if there is no jump */
                if (seq->frame_num != current_frame_seq) {
                    err("frame 0x%04x received instead of 0x%04x", current_frame_seq, seq->frame_num);
                    errors++;
                    seq->error_count++;
                    seq_errors_total++;
                }
                seq->frame_num = (current_frame_seq + 1) & FRAME_NUM_MASK;
                break;
            }
        } else {
            switch (next_state) {
            case INVALID_FRAME:
                if (seq->state == VALID_FRAME) {
                    /* this may not be an error if the stream is stopped on remote side
                     * in this case we should receive only a short number of invalid frames
                     * followed by some null frames
                     */
                    warn("first invalid frame while expecting frame 0x%04x", seq->frame_num);
                    log_frame( LOG_WARN, seq, s16 );
                } else {
                    err("invalid frame after %u null frames", seq->frame_num);
                    log_frame( LOG_ERR, seq, s16 );
                    errors++;
                    seq->error_count++;
                    seq_errors_total++;
                }
                seq->frame_num = 1;
                break;

            case NULL_FRAME:
                if (seq->state == VALID_FRAME) {
                    warn("Null frame (%02X) while expecting frame 0x%04x", (*s16 & 0xFF), seq->frame_num);
                } else {
                    if (seq->frame_num > seq_max_consecutive_invalid_frames_before_null_warning) {
                        err("Null frame (%02X) after %u invalid frames", (*s16 & 0xFF), seq->frame_num);
                        errors++;
                        seq->error_count++;
                        seq_errors_total++;
                    } else {
                        warn("Null frame (%02X) after %u invalid frames", (*s16 & 0xFF), seq->frame_num);
                    }
                }
                seq->frame_num = 1;
                break;

            case VALID_FRAME:
                if (seq->state == NULL_FRAME) {
                    if (seq->frame_num > 0)
                        warn("Valid frame after %u null frames", seq->frame_num);
                    else
                        warn("First valid frame");
                } else {
                    warn("Valid frame after %u invalid frames", seq->frame_num);
                }
                log_frame( LOG_WARN, seq, s16 );
                seq->frame_num = (current_frame_seq + 1) & FRAME_NUM_MASK;
                break;
            }
            seq->prev_state = seq->state;
            seq->state = next_state;
        }
        s16 += seq->channels;
    }
    if (errors && seq_error_notify) seq_error_notify();
    return errors;
}
