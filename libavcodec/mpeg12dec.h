/*
 * MPEG-1/2 decoder header
 * Copyright (c) 2007 Aurelien Jacobs <aurel@gnuage.org>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_MPEG12DEC_H
#define AVCODEC_MPEG12DEC_H

#include "libavutil/buffer.h"
#include "libavutil/mem_internal.h"
#include "libavutil/stereo3d.h"

#include "get_bits.h"
#include "mpeg12vlc.h"
#include "mpegvideo.h"

#define MB_TYPE_ZERO_MV   MB_TYPE_CODEC_SPECIFIC

static inline int decode_dc(GetBitContext *gb, int component)
{
    int code, diff;

    if (component == 0) {
        code = get_vlc2(gb, ff_dc_lum_vlc, DC_VLC_BITS, 2);
    } else {
        code = get_vlc2(gb, ff_dc_chroma_vlc, DC_VLC_BITS, 2);
    }
    if (code == 0) {
        diff = 0;
    } else {
        diff = get_xbits(gb, code);
    }
    return diff;
}

int ff_mpeg1_decode_block_intra(GetBitContext *gb,
                                const uint16_t *quant_matrix,
                                const uint8_t *scantable, int last_dc[3],
                                int16_t *block, int index, int qscale);

enum Mpeg2ClosedCaptionsFormat {
    CC_FORMAT_AUTO,
    CC_FORMAT_A53_PART4,
    CC_FORMAT_SCTE20,
    CC_FORMAT_DVD,
    CC_FORMAT_DISH
};

typedef struct Mpeg12SliceContext {
    MPVContext c;
    GetBitContext gb;

    DECLARE_ALIGNED_32(int16_t, block)[12][64];
} Mpeg12SliceContext;

typedef struct Mpeg1Context {
    Mpeg12SliceContext slice;
    AVPanScan pan_scan;         /* some temporary storage for the panscan */
    enum AVStereo3DType stereo3d_type;
    int has_stereo3d;
    AVBufferRef *a53_buf_ref;
    enum Mpeg2ClosedCaptionsFormat cc_format;
    uint8_t afd;
    int has_afd;
    int slice_count;
    unsigned aspect_ratio_info;
    int save_progressive_seq, save_chroma_format;
    AVRational frame_rate_ext;  /* MPEG-2 specific framerate modificator */
    unsigned frame_rate_index;
    int sync;                   /* Did we reach a sync point like a GOP/SEQ/KEYFrame? */
    int closed_gop;
    int tmpgexs;
    int first_slice;
    int extradata_decoded;
    int vbv_delay;
    int64_t bit_rate;
    int64_t timecode_frame_start;  /*< GOP timecode frame start number, in non drop frame format */
} Mpeg1Context;

void ff_mpeg_decode_user_data(AVCodecContext *avctx, Mpeg1Context *s1,
                              const uint8_t *p, int buf_size);

#endif /* AVCODEC_MPEG12DEC_H */
