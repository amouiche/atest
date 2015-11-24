/*
 * Copyright (C) 2015 Arnaud Mouiche <arnaud.mouiche@invoxia.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */



#ifndef __seq_h__
#define __seq_h__

/* total number of sequence errors detected among every sequence checkers */
extern unsigned seq_errors_total;

enum seq_stat_e {
    NULL_FRAME = 0,
    INVALID_FRAME,
    VALID_FRAME,
} state;


struct seq_info {
    unsigned channels;
    snd_pcm_format_t format;

    /*
     * fill:
     *   next frame sequence number to use
     * check:
     *   current number of NULL frames (W4_NON_NULL_FRAME)
     *   current number of invalid frames (INVALID_FRAMES)
     *   next expected frame sequence (VALID_FRAMES)
     *
     */
    unsigned frame_num;
    unsigned frame_num_mask;

    enum seq_stat_e state;
    enum seq_stat_e prev_state;
    unsigned error_count;
};


void seq_init( struct seq_info *seq, unsigned channels, snd_pcm_format_t format );

/*
 * each sample of the frame sequence #N has the expected value
 * (channel & 15) | (N << 4)
 *
 * seq_fill_frames() generates 'frame_count' frames with this expected sequence
 *
 * seq_check_frames() check the content of the received frames
 *    - frames filled with 0x00 or 0xFF are not consider as errors. only a warning is
 *      printed with the number of such frames detected.
 *    - if one sample is different frome 0x00 or 0xFF in a frame, every other samples of the frames must be valid
 *
 *
 *    - return 0 when no error is detected
 *      otherwise return 1
 */


void seq_fill_frames( struct seq_info *seq, void *buff, int frame_count );
int seq_check_frames( struct seq_info *seq, const void *buff, int frame_count );



#endif //__seq_h__
