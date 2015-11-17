

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <alsa/asoundlib.h>

#include "seq.h"
#include "log.h"

void seq_init( struct seq_info *seq, unsigned channels, snd_pcm_format_t format )
{
    memset( seq, 0, sizeof(*seq));
    seq->channels = channels;
    seq->format = format;
    seq->frame_num_mask = 0xFFF;
}


void seq_fill_frames( struct seq_info *seq, void *buff, int frame_count ) {
    int16_t *s16;

    switch (seq->format) {
    case SND_PCM_FORMAT_S16_LE:
        s16 = (int16_t *)buff;
        while (frame_count--) {
            int ch;
            for (ch = 0; ch < seq->channels; ch++) {
                *s16++ = (ch & 15) | ((seq->frame_num << 4) & 0xFFF);
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
        if (*b != 0 || *b != 0xFF) return 0;
        b++;
    }
    return 1;
}



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
    }
    log( level, "%s", line);
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
            current_frame_seq = (*s16 >> 4);
            for (ch=0; ch < seq->channels; ch++) {
                if (((*s & 15) != ch) || (current_frame_seq != (*s >> 4))) {
                    next_state = INVALID_FRAME;
                    break;
                }
                s++;
            }
        }

        if (seq->state == next_state) {
            switch (seq->state) {
            case NULL_FRAME:
            case INVALID_FRAME:
                /* simply increase the record count of those frames */
                seq->frame_num++;
                break;
            case VALID_FRAME:
                /* check the frame sequence to see if there is no jump */
                if (seq->frame_num != current_frame_seq) {
                    err("frame 0x%04x received instead of 0x%04x", current_frame_seq, seq->frame_num);
                    errors++;
                }
                seq->frame_num = (current_frame_seq + 1) & seq->frame_num_mask;
                break;
            }
        } else {
            switch (next_state) {
            case INVALID_FRAME:
                if (seq->state == VALID_FRAME) {
                    err("invalid frame while expecting frame 0x%04x", seq->frame_num);
                } else {
                    err("invalid frame after %u null frames", seq->frame_num);
                }
                log_frame( LOG_ERR, seq, s16 );
                seq->frame_num = 1;
                break;

            case NULL_FRAME:
                if (seq->state == VALID_FRAME) {
                    warn("Null frame (%02X) while expecting frame 0x%04x", (*s16 & 0xFF), seq->frame_num);
                } else {
                    warn("Null frame (%02X) after %u invalid frames", (*s16 & 0xFF), seq->frame_num);
                }
                seq->frame_num = 1;
                break;

            case VALID_FRAME:
                if (seq->state == NULL_FRAME) {
                    warn("Valid frame after %u null frames", seq->frame_num);
                } else {
                    warn("Valid frame after %u invalid frames", seq->frame_num);
                }
                log_frame( LOG_WARN, seq, s16 );
                seq->frame_num = (current_frame_seq + 1) & seq->frame_num_mask;
                break;
            }
            seq->state = next_state;
        }
        s16 += seq->channels;
    }
    return errors;
}
