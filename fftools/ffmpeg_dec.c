/*
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

#include <stdbit.h>

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/dict.h"
#include "libavutil/error.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"
#include "libavutil/stereo3d.h"
#include "libavutil/time.h"
#include "libavutil/timestamp.h"

#include "libavcodec/avcodec.h"
#include "libavcodec/codec.h"

#include "ffmpeg.h"

typedef struct DecoderPriv {
    Decoder             dec;

    AVCodecContext     *dec_ctx;

    AVFrame            *frame;
    AVFrame            *frame_tmp_ref;
    AVPacket           *pkt;

    // override output video sample aspect ratio with this value
    AVRational          sar_override;

    AVRational          framerate_in;

    // a combination of DECODER_FLAG_*, provided to dec_open()
    int                 flags;
    int                 apply_cropping;

    enum AVPixelFormat  hwaccel_pix_fmt;
    enum HWAccelID      hwaccel_id;
    enum AVHWDeviceType hwaccel_device_type;
    enum AVPixelFormat  hwaccel_output_format;

    // pts/estimated duration of the last decoded frame
    // * in decoder timebase for video,
    // * in last_frame_tb (may change during decoding) for audio
    int64_t             last_frame_pts;
    int64_t             last_frame_duration_est;
    AVRational          last_frame_tb;
    int64_t             last_filter_in_rescale_delta;
    int                 last_frame_sample_rate;

    Scheduler          *sch;
    unsigned            sch_idx;

    // this decoder's index in decoders or -1
    int                 index;
    void               *log_parent;
    char                log_name[32];
    char               *parent_name;

    // user specified decoder multiview options manually
    int                 multiview_user_config;

    int                 fix_sub_duration;

    struct {
        ViewSpecifier   vs;
        unsigned        out_idx;
    }                  *views_requested;
    int              nb_views_requested;

    /* A map of view ID to decoder outputs.
     * MUST NOT be accessed outside of get_format()/get_buffer() */
    struct {
        unsigned        id;
        uintptr_t       out_mask;
    }                  *view_map;
    int              nb_view_map;

    struct {
        AVDictionary       *opts;
        const AVCodec      *codec;
    } standalone_init;

    struct { /* previous decoded subtitle and related variables */
        int got_output;
        int ret;
        AVFrame *subtitle;
    } prev_sub;
} DecoderPriv;

static DecoderPriv *dp_from_dec(Decoder *d)
{
    return (DecoderPriv*)d;
}

// data that is local to the decoder thread and not visible outside of it
typedef struct DecThreadContext {
    AVFrame         *frame;
    AVPacket        *pkt;
} DecThreadContext;

void dec_free(Decoder **pdec)
{
    Decoder *dec = *pdec;
    DecoderPriv *dp;

    if (!dec)
        return;
    dp = dp_from_dec(dec);

    avcodec_free_context(&dp->dec_ctx);

    av_frame_free(&dp->frame);
    av_frame_free(&dp->frame_tmp_ref);
    av_packet_free(&dp->pkt);

    av_dict_free(&dp->standalone_init.opts);

    av_freep(&dp->parent_name);

    av_freep(&dp->views_requested);
    av_freep(&dp->view_map);

    av_freep(pdec);
}

static const char *dec_item_name(void *obj)
{
    const DecoderPriv *dp = obj;

    return dp->log_name;
}

static const AVClass dec_class = {
    .class_name                = "Decoder",
    .version                   = LIBAVUTIL_VERSION_INT,
    .parent_log_context_offset = offsetof(DecoderPriv, log_parent),
    .item_name                 = dec_item_name,
};

static int decoder_thread(void *arg);

static int dec_alloc(DecoderPriv **pdec, Scheduler *sch, int send_end_ts)
{
    DecoderPriv *dp;
    int ret = 0;

    *pdec = NULL;

    dp = av_mallocz(sizeof(*dp));
    if (!dp)
        return AVERROR(ENOMEM);

    dp->frame = av_frame_alloc();
    if (!dp->frame)
        goto fail;

    dp->pkt = av_packet_alloc();
    if (!dp->pkt)
        goto fail;

    dp->index                        = -1;
    dp->dec.class                    = &dec_class;
    dp->last_filter_in_rescale_delta = AV_NOPTS_VALUE;
    dp->last_frame_pts               = AV_NOPTS_VALUE;
    dp->last_frame_tb                = (AVRational){ 1, 1 };
    dp->hwaccel_pix_fmt              = AV_PIX_FMT_NONE;

    ret = sch_add_dec(sch, decoder_thread, dp, send_end_ts);
    if (ret < 0)
        goto fail;
    dp->sch     = sch;
    dp->sch_idx = ret;

    *pdec = dp;

    return 0;
fail:
    dec_free((Decoder**)&dp);
    return ret >= 0 ? AVERROR(ENOMEM) : ret;
}

static AVRational audio_samplerate_update(DecoderPriv *dp,
                                          const AVFrame *frame)
{
    const int prev = dp->last_frame_tb.den;
    const int sr   = frame->sample_rate;

    AVRational tb_new;
    int64_t gcd;

    if (frame->sample_rate == dp->last_frame_sample_rate)
        goto finish;

    gcd  = av_gcd(prev, sr);

    if (prev / gcd >= INT_MAX / sr) {
        av_log(dp, AV_LOG_WARNING,
               "Audio timestamps cannot be represented exactly after "
               "sample rate change: %d -> %d\n", prev, sr);

        // LCM of 192000, 44100, allows to represent all common samplerates
        tb_new = (AVRational){ 1, 28224000 };
    } else
        tb_new = (AVRational){ 1, prev / gcd * sr };

    // keep the frame timebase if it is strictly better than
    // the samplerate-defined one
    if (frame->time_base.num == 1 && frame->time_base.den > tb_new.den &&
        !(frame->time_base.den % tb_new.den))
        tb_new = frame->time_base;

    if (dp->last_frame_pts != AV_NOPTS_VALUE)
        dp->last_frame_pts = av_rescale_q(dp->last_frame_pts,
                                          dp->last_frame_tb, tb_new);
    dp->last_frame_duration_est = av_rescale_q(dp->last_frame_duration_est,
                                               dp->last_frame_tb, tb_new);

    dp->last_frame_tb          = tb_new;
    dp->last_frame_sample_rate = frame->sample_rate;

finish:
    return dp->last_frame_tb;
}

static void audio_ts_process(DecoderPriv *dp, AVFrame *frame)
{
    AVRational tb_filter = (AVRational){1, frame->sample_rate};
    AVRational tb;
    int64_t pts_pred;

    // on samplerate change, choose a new internal timebase for timestamp
    // generation that can represent timestamps from all the samplerates
    // seen so far
    tb = audio_samplerate_update(dp, frame);
    pts_pred = dp->last_frame_pts == AV_NOPTS_VALUE ? 0 :
               dp->last_frame_pts + dp->last_frame_duration_est;

    if (frame->pts == AV_NOPTS_VALUE) {
        frame->pts = pts_pred;
        frame->time_base = tb;
    } else if (dp->last_frame_pts != AV_NOPTS_VALUE &&
               frame->pts > av_rescale_q_rnd(pts_pred, tb, frame->time_base,
                                             AV_ROUND_UP)) {
        // there was a gap in timestamps, reset conversion state
        dp->last_filter_in_rescale_delta = AV_NOPTS_VALUE;
    }

    frame->pts = av_rescale_delta(frame->time_base, frame->pts,
                                  tb, frame->nb_samples,
                                  &dp->last_filter_in_rescale_delta, tb);

    dp->last_frame_pts          = frame->pts;
    dp->last_frame_duration_est = av_rescale_q(frame->nb_samples,
                                               tb_filter, tb);

    // finally convert to filtering timebase
    frame->pts       = av_rescale_q(frame->pts, tb, tb_filter);
    frame->duration  = frame->nb_samples;
    frame->time_base = tb_filter;
}

static int64_t video_duration_estimate(const DecoderPriv *dp, const AVFrame *frame)
{
    const int  ts_unreliable = dp->flags & DECODER_FLAG_TS_UNRELIABLE;
    const int      fr_forced = dp->flags & DECODER_FLAG_FRAMERATE_FORCED;
    int64_t codec_duration = 0;
    // difference between this and last frame's timestamps
    const int64_t ts_diff =
        (frame->pts != AV_NOPTS_VALUE && dp->last_frame_pts != AV_NOPTS_VALUE) ?
        frame->pts - dp->last_frame_pts : -1;

    // XXX lavf currently makes up frame durations when they are not provided by
    // the container. As there is no way to reliably distinguish real container
    // durations from the fake made-up ones, we use heuristics based on whether
    // the container has timestamps. Eventually lavf should stop making up
    // durations, then this should be simplified.

    // frame duration is unreliable (typically guessed by lavf) when it is equal
    // to 1 and the actual duration of the last frame is more than 2x larger
    const int duration_unreliable = frame->duration == 1 && ts_diff > 2 * frame->duration;

    // prefer frame duration for containers with timestamps
    if (fr_forced ||
        (frame->duration > 0 && !ts_unreliable && !duration_unreliable))
        return frame->duration;

    if (dp->dec_ctx->framerate.den && dp->dec_ctx->framerate.num) {
        int fields = frame->repeat_pict + 2;
        AVRational field_rate = av_mul_q(dp->dec_ctx->framerate,
                                         (AVRational){ 2, 1 });
        codec_duration = av_rescale_q(fields, av_inv_q(field_rate),
                                      frame->time_base);
    }

    // prefer codec-layer duration for containers without timestamps
    if (codec_duration > 0 && ts_unreliable)
        return codec_duration;

    // when timestamps are available, repeat last frame's actual duration
    // (i.e. pts difference between this and last frame)
    if (ts_diff > 0)
        return ts_diff;

    // try frame/codec duration
    if (frame->duration > 0)
        return frame->duration;
    if (codec_duration > 0)
        return codec_duration;

    // try average framerate
    if (dp->framerate_in.num && dp->framerate_in.den) {
        int64_t d = av_rescale_q(1, av_inv_q(dp->framerate_in),
                                 frame->time_base);
        if (d > 0)
            return d;
    }

    // last resort is last frame's estimated duration, and 1
    return FFMAX(dp->last_frame_duration_est, 1);
}

static int hwaccel_retrieve_data(AVCodecContext *avctx, AVFrame *input)
{
    DecoderPriv *dp = avctx->opaque;
    AVFrame *output = NULL;
    enum AVPixelFormat output_format = dp->hwaccel_output_format;
    int err;

    if (input->format == output_format) {
        // Nothing to do.
        return 0;
    }

    output = av_frame_alloc();
    if (!output)
        return AVERROR(ENOMEM);

    output->format = output_format;

    err = av_hwframe_transfer_data(output, input, 0);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to transfer data to "
               "output frame: %d.\n", err);
        goto fail;
    }

    err = av_frame_copy_props(output, input);
    if (err < 0) {
        av_frame_unref(output);
        goto fail;
    }

    av_frame_unref(input);
    av_frame_move_ref(input, output);
    av_frame_free(&output);

    return 0;

fail:
    av_frame_free(&output);
    return err;
}

static int video_frame_process(DecoderPriv *dp, AVFrame *frame,
                               unsigned *outputs_mask)
{
#if FFMPEG_OPT_TOP
    if (dp->flags & DECODER_FLAG_TOP_FIELD_FIRST) {
        av_log(dp, AV_LOG_WARNING, "-top is deprecated, use the setfield filter instead\n");
        frame->flags |= AV_FRAME_FLAG_TOP_FIELD_FIRST;
    }
#endif

    if (frame->format == dp->hwaccel_pix_fmt) {
        int err = hwaccel_retrieve_data(dp->dec_ctx, frame);
        if (err < 0)
            return err;
    }

    frame->pts = frame->best_effort_timestamp;

    // forced fixed framerate
    if (dp->flags & DECODER_FLAG_FRAMERATE_FORCED) {
        frame->pts       = AV_NOPTS_VALUE;
        frame->duration  = 1;
        frame->time_base = av_inv_q(dp->framerate_in);
    }

    // no timestamp available - extrapolate from previous frame duration
    if (frame->pts == AV_NOPTS_VALUE)
        frame->pts = dp->last_frame_pts == AV_NOPTS_VALUE ? 0 :
                     dp->last_frame_pts + dp->last_frame_duration_est;

    // update timestamp history
    dp->last_frame_duration_est = video_duration_estimate(dp, frame);
    dp->last_frame_pts          = frame->pts;
    dp->last_frame_tb           = frame->time_base;

    if (debug_ts) {
        av_log(dp, AV_LOG_INFO,
               "decoder -> pts:%s pts_time:%s "
               "pkt_dts:%s pkt_dts_time:%s "
               "duration:%s duration_time:%s "
               "keyframe:%d frame_type:%d time_base:%d/%d\n",
               av_ts2str(frame->pts),
               av_ts2timestr(frame->pts, &frame->time_base),
               av_ts2str(frame->pkt_dts),
               av_ts2timestr(frame->pkt_dts, &frame->time_base),
               av_ts2str(frame->duration),
               av_ts2timestr(frame->duration, &frame->time_base),
               !!(frame->flags & AV_FRAME_FLAG_KEY), frame->pict_type,
               frame->time_base.num, frame->time_base.den);
    }

    if (dp->sar_override.num)
        frame->sample_aspect_ratio = dp->sar_override;

    if (dp->apply_cropping) {
        // lavfi does not require aligned frame data
        int ret = av_frame_apply_cropping(frame, AV_FRAME_CROP_UNALIGNED);
        if (ret < 0) {
            av_log(dp, AV_LOG_ERROR, "Error applying decoder cropping\n");
            return ret;
        }
    }

    if (frame->opaque)
        *outputs_mask = (uintptr_t)frame->opaque;

    return 0;
}

static int decode_subtitles_decode(AVCodecContext *avctx, AVFrame *frame, int *got_frame, const AVPacket *pkt)
{
    int ret;

    *got_frame = 0;

    if (pkt) {
        ret = avcodec_send_packet(avctx, pkt);
        // In particular, we don't expect AVERROR(EAGAIN), because we read all
        // decoded frames with avcodec_receive_frame() until done.
        if (ret < 0 && ret != AVERROR_EOF)
            return ret;
    }

    ret = avcodec_receive_frame(avctx, frame);
    if (ret < 0 && ret != AVERROR(EAGAIN))
        return ret;
    if (ret >= 0)
        *got_frame = 1;

    return 0;
}

static int decode_subtitles(DecoderPriv *dp, const AVPacket *pkt, AVFrame *frame)
{
    AVCodecContext *ctx = dp->dec_ctx;
    AVPacket *flush_pkt = NULL;
    int got_output = 0;
    int ret;

    if (pkt && (intptr_t)pkt->opaque == PKT_OPAQUE_SUBTITLE_KICKOFF) {
        frame->type = AVMEDIA_TYPE_SUBTITLE;
        frame->format = (uint16_t)ctx->subtitle_type;
        frame->pts = pkt->pts;
        frame->pkt_dts = pkt->dts;
        frame->best_effort_timestamp = pkt->pts;
        frame->time_base = pkt->time_base;
        frame->width  = ctx->width;
        frame->height = ctx->height;
        frame->subtitle_timing.start_pts = av_rescale_q(pkt->pts, pkt->time_base, AV_TIME_BASE_Q);
        frame->subtitle_timing.duration  = 1;

        ret = av_frame_get_buffer2(frame, 0);
        if (ret < 0)
            return ret;

        dp->dec.subtitle_kickoff->last_pts = frame->pts;

        ret = sch_dec_send(dp->sch, dp->sch_idx, 0, frame);
        if (ret < 0)
            av_frame_unref(frame);

        return ret == AVERROR_EOF ? AVERROR_EXIT : ret;
    }

    ////if (pkt && (intptr_t)pkt->opaque == PKT_OPAQUE_SUB_HEARTBEAT) {
    ////    frame->pts       = pkt->pts;
    ////    frame->time_base = pkt->time_base;
    ////    frame->opaque    = (void*)(intptr_t)FRAME_OPAQUE_SUB_HEARTBEAT;

    ////    ret = sch_dec_send(dp->sch, dp->sch_idx, 0, frame);
    ////    return ret == AVERROR_EOF ? AVERROR_EXIT : ret;
    ////} else if (pkt && (intptr_t)pkt->opaque == PKT_OPAQUE_FIX_SUB_DURATION) {
    ////    return fix_sub_duration_heartbeat(dp, av_rescale_q(pkt->pts, pkt->time_base,
    ////                                                       AV_TIME_BASE_Q));
    ////}

    frame->type = AVMEDIA_TYPE_SUBTITLE;
    frame->format = (uint16_t)ctx->subtitle_type;

    if (!ctx->ass_header && ctx->subtitle_header && ctx->subtitle_header_size > 0) {
        char *subtitle_header =  av_strdup((char*)ctx->subtitle_header);
        if (!subtitle_header)
            return AVERROR(ENOMEM);

        ctx->ass_header = av_buffer_create((uint8_t*)subtitle_header, strlen(subtitle_header) + 1, NULL, NULL, 0);
        if (!ctx->ass_header) {
            av_free(subtitle_header);
            return AVERROR(ENOMEM);
        }
    }

    if (!pkt) {
        flush_pkt = av_packet_alloc();
        if (!flush_pkt)
            return AVERROR(ENOMEM);
    }

    ////ret = avcodec_decode_subtitle2(ctx, &subtitle, &got_output, pkt ? pkt : flush_pkt);

    ret = decode_subtitles_decode(ctx, frame, &got_output, pkt ? pkt : flush_pkt);

    av_packet_free(&flush_pkt);

    ////if (ret < 0) {
    ////    av_log(dp, AV_LOG_ERROR, "Error decoding subtitles: %s\n", av_err2str(ret));
    ////    dp->dec.decode_errors++;
    ////    return exit_on_error ? ret : 0;
    ////}

    ////if (ret != AVERROR_EOF)
    ////    check_decode_result(NULL, got_output, ret);


    if (!got_output)
        return pkt ? 0 : AVERROR_EOF;

    if (dp->fix_sub_duration) {
        int64_t end = 1;
        if (dp->prev_sub.got_output && dp->prev_sub.subtitle) {

            const int64_t duration = dp->prev_sub.subtitle->subtitle_timing.duration;
            end = frame->subtitle_timing.start_pts - dp->prev_sub.subtitle->subtitle_timing.start_pts;

            if (end < duration) {
                av_log(ctx, AV_LOG_DEBUG, "Subtitle duration reduced from %"PRId64" to %"PRId64"%s\n",
                    duration, end, end <= 0 ? ", dropping it" : "");
                dp->prev_sub.subtitle->subtitle_timing.duration = end;
            }
        }
        FFSWAP(int,        got_output,        dp->prev_sub.got_output);
        FFSWAP(int,        ret,                dp->prev_sub.ret);
        FFSWAP(AVFrame*,   frame, dp->prev_sub.subtitle);
        frame = dp->frame;
        if (end <= 0)
            return (int)end;
    }

    if (!got_output || !frame)
        return ret;

    frame->type = AVMEDIA_TYPE_SUBTITLE;

    if (frame->format == AV_SUBTITLE_FMT_UNKNOWN)
        frame->format = (uint16_t)ctx->subtitle_type;

    if ((ret = av_buffer_replace(&frame->subtitle_header, ctx->ass_header)) < 0)
        return ret;

    int64_t pts = av_rescale_q(frame->subtitle_timing.start_pts, AV_TIME_BASE_Q, ctx->pkt_timebase);
    if (dp->dec.subtitle_kickoff->last_pts > 0 && pts <= dp->dec.subtitle_kickoff->last_pts)
        pts = dp->dec.subtitle_kickoff->last_pts + 1;

    dp->dec.subtitle_kickoff->last_pts = frame->pts = pts;

    dp->dec.frames_decoded++;

    frame->width  = ctx->width;
    frame->height = ctx->height;
    frame->format = ctx->subtitle_type;

    av_log(ctx, AV_LOG_DEBUG, "decode_subtitles: pts: %"PRId64"  frame->pts: %"PRId64" duration: %"PRId64" size: %dx%d type: %d\n",
           frame->subtitle_timing.start_pts, frame->pts, frame->subtitle_timing.duration, ctx->width, ctx->height,  ctx->subtitle_type);

    ret = sch_dec_send(dp->sch, dp->sch_idx, 0, frame);
    if (ret < 0)
        av_frame_unref(frame);

    return ret == AVERROR_EOF ? AVERROR_EXIT : ret;
}

static int packet_decode(DecoderPriv *dp, AVPacket *pkt, AVFrame *frame)
{
    AVCodecContext *dec = dp->dec_ctx;
    const char *type_desc = av_get_media_type_string(dec->codec_type);
    int ret;

    if (dec->codec_type == AVMEDIA_TYPE_SUBTITLE)
        return decode_subtitles(dp, pkt, frame);

    // With fate-indeo3-2, we're getting 0-sized packets before EOF for some
    // reason. This seems like a semi-critical bug. Don't trigger EOF, and
    // skip the packet.
    if (pkt && pkt->size == 0)
        return 0;

    if (pkt && (dp->flags & DECODER_FLAG_TS_UNRELIABLE)) {
        pkt->pts = AV_NOPTS_VALUE;
        pkt->dts = AV_NOPTS_VALUE;
    }

    if (pkt) {
        FrameData *fd = packet_data(pkt);
        if (!fd)
            return AVERROR(ENOMEM);
        fd->wallclock[LATENCY_PROBE_DEC_PRE] = av_gettime_relative();
    }

    ret = avcodec_send_packet(dec, pkt);
    if (ret < 0 && !(ret == AVERROR_EOF && !pkt)) {
        // In particular, we don't expect AVERROR(EAGAIN), because we read all
        // decoded frames with avcodec_receive_frame() until done.
        if (ret == AVERROR(EAGAIN)) {
            av_log(dp, AV_LOG_FATAL, "A decoder returned an unexpected error code. "
                                     "This is a bug, please report it.\n");
            return AVERROR_BUG;
        }
        av_log(dp, AV_LOG_ERROR, "Error submitting %s to decoder: %s\n",
               pkt ? "packet" : "EOF", av_err2str(ret));

        if (ret == AVERROR_EOF)
            return ret;

        dp->dec.decode_errors++;
        if (exit_on_error)
            return ret;
    }

    while (1) {
        FrameData *fd;
        unsigned outputs_mask = 1;

        av_frame_unref(frame);

        update_benchmark(NULL);
        ret = avcodec_receive_frame(dec, frame);
        update_benchmark("decode_%s %s", type_desc, dp->parent_name);

        if (ret == AVERROR(EAGAIN)) {
            av_assert0(pkt); // should never happen during flushing
            return 0;
        } else if (ret == AVERROR_EOF) {
            return ret;
        } else if (ret < 0) {
            av_log(dp, AV_LOG_ERROR, "Decoding error: %s\n", av_err2str(ret));
            dp->dec.decode_errors++;

            if (exit_on_error)
                return ret;

            continue;
        }

        if (frame->decode_error_flags || (frame->flags & AV_FRAME_FLAG_CORRUPT)) {
            av_log(dp, exit_on_error ? AV_LOG_FATAL : AV_LOG_WARNING,
                   "corrupt decoded frame\n");
            if (exit_on_error)
                return AVERROR_INVALIDDATA;
        }

        fd      = frame_data(frame);
        if (!fd) {
            av_frame_unref(frame);
            return AVERROR(ENOMEM);
        }
        fd->dec.pts                 = frame->pts;
        fd->dec.tb                  = dec->pkt_timebase;
        fd->dec.frame_num           = dec->frame_num - 1;
        fd->bits_per_raw_sample     = dec->bits_per_raw_sample;

        fd->wallclock[LATENCY_PROBE_DEC_POST] = av_gettime_relative();

        frame->time_base = dec->pkt_timebase;

        if (dec->codec_type == AVMEDIA_TYPE_AUDIO) {
            dp->dec.samples_decoded += frame->nb_samples;

            audio_ts_process(dp, frame);
        } else if (dec->codec_type == AVMEDIA_TYPE_VIDEO) {
            ret = video_frame_process(dp, frame, &outputs_mask);
            if (ret < 0) {
                av_log(dp, AV_LOG_FATAL,
                       "Error while processing the decoded data\n");
                return ret;
            }
        }

        dp->dec.frames_decoded++;

        for (int i = 0; i < stdc_count_ones(outputs_mask); i++) {
            AVFrame *to_send = frame;
            int pos;

            av_assert0(outputs_mask);
            pos = stdc_trailing_zeros(outputs_mask);
            outputs_mask &= ~(1U << pos);

            // this is not the last output and sch_dec_send() consumes the frame
            // given to it, so make a temporary reference
            if (outputs_mask) {
                to_send = dp->frame_tmp_ref;
                ret = av_frame_ref(to_send, frame);
                if (ret < 0)
                    return ret;
            }

            ret = sch_dec_send(dp->sch, dp->sch_idx, pos, to_send);
            if (ret < 0) {
                av_frame_unref(to_send);
                return ret == AVERROR_EOF ? AVERROR_EXIT : ret;
            }
        }
    }
}

static int dec_open(DecoderPriv *dp, AVDictionary **dec_opts,
                    const DecoderOpts *o, AVFrame *param_out);

static int dec_standalone_open(DecoderPriv *dp, const AVPacket *pkt)
{
    DecoderOpts o;
    const FrameData *fd;
    char name[16];

    if (!pkt->opaque_ref)
        return AVERROR_BUG;
    fd = (FrameData *)pkt->opaque_ref->data;

    if (!fd->par_enc)
        return AVERROR_BUG;

    memset(&o, 0, sizeof(o));

    o.par       = fd->par_enc;
    o.time_base = pkt->time_base;

    o.codec = dp->standalone_init.codec;
    if (!o.codec)
        o.codec = avcodec_find_decoder(o.par->codec_id);
    if (!o.codec) {
        const AVCodecDescriptor *desc = avcodec_descriptor_get(o.par->codec_id);

        av_log(dp, AV_LOG_ERROR, "Cannot find a decoder for codec ID '%s'\n",
               desc ? desc->name : "?");
        return AVERROR_DECODER_NOT_FOUND;
    }

    snprintf(name, sizeof(name), "dec%d", dp->index);
    o.name = name;

    return dec_open(dp, &dp->standalone_init.opts, &o, NULL);
}

static void dec_thread_set_name(const DecoderPriv *dp)
{
    char name[16] = "dec";

    if (dp->index >= 0)
        av_strlcatf(name, sizeof(name), "%d", dp->index);
    else if (dp->parent_name)
        av_strlcat(name, dp->parent_name, sizeof(name));

    if (dp->dec_ctx)
        av_strlcatf(name, sizeof(name), ":%s", dp->dec_ctx->codec->name);

    ff_thread_setname(name);
}

static void dec_thread_uninit(DecThreadContext *dt)
{
    av_packet_free(&dt->pkt);
    av_frame_free(&dt->frame);

    memset(dt, 0, sizeof(*dt));
}

static int dec_thread_init(DecThreadContext *dt)
{
    memset(dt, 0, sizeof(*dt));

    dt->frame = av_frame_alloc();
    if (!dt->frame)
        goto fail;

    dt->pkt = av_packet_alloc();
    if (!dt->pkt)
        goto fail;

    return 0;

fail:
    dec_thread_uninit(dt);
    return AVERROR(ENOMEM);
}

static int decoder_thread(void *arg)
{
    DecoderPriv  *dp = arg;
    DecThreadContext dt;
    int ret = 0, input_status = 0;

    ret = dec_thread_init(&dt);
    if (ret < 0)
        goto finish;

    dec_thread_set_name(dp);

    while (!input_status) {
        int flush_buffers, have_data;

        input_status  = sch_dec_receive(dp->sch, dp->sch_idx, dt.pkt);
        have_data     = input_status >= 0 &&
            (dt.pkt->buf || dt.pkt->side_data_elems);
             ////(intptr_t)dt.pkt->opaque == PKT_OPAQUE_SUB_HEARTBEAT ||
             ////(intptr_t)dt.pkt->opaque == PKT_OPAQUE_FIX_SUB_DURATION);
        flush_buffers = input_status >= 0 && !have_data;
        if (!have_data)
            av_log(dp, AV_LOG_VERBOSE, "Decoder thread received %s packet\n",
                   flush_buffers ? "flush" : "EOF");

        // this is a standalone decoder that has not been initialized yet
        if (!dp->dec_ctx) {
            if (flush_buffers)
                continue;
            if (input_status < 0) {
                av_log(dp, AV_LOG_ERROR,
                       "Cannot initialize a standalone decoder\n");
                ret = input_status;
                goto finish;
            }

            ret = dec_standalone_open(dp, dt.pkt);
            if (ret < 0)
                goto finish;
        }

        ret = packet_decode(dp, have_data ? dt.pkt : NULL, dt.frame);

        av_packet_unref(dt.pkt);
        av_frame_unref(dt.frame);

        // AVERROR_EOF  - EOF from the decoder
        // AVERROR_EXIT - EOF from the scheduler
        // we treat them differently when flushing
        if (ret == AVERROR_EXIT) {
            ret = AVERROR_EOF;
            flush_buffers = 0;
        }

        if (ret == AVERROR_EOF) {
            av_log(dp, AV_LOG_VERBOSE, "Decoder returned EOF, %s\n",
                   flush_buffers ? "resetting" : "finishing");

            if (!flush_buffers)
                break;

            /* report last frame duration to the scheduler */
            if (dp->dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
                dt.pkt->pts       = dp->last_frame_pts + dp->last_frame_duration_est;
                dt.pkt->time_base = dp->last_frame_tb;
            }

            avcodec_flush_buffers(dp->dec_ctx);
        } else if (ret < 0) {
            av_log(dp, AV_LOG_ERROR, "Error processing packet in decoder: %s\n",
                   av_err2str(ret));
            break;
        }
    }

    // EOF is normal thread termination
    if (ret == AVERROR_EOF)
        ret = 0;

    // on success send EOF timestamp to our downstreams
    if (ret >= 0) {
        float err_rate;

        av_frame_unref(dt.frame);

        dt.frame->opaque    = (void*)(intptr_t)FRAME_OPAQUE_EOF;
        dt.frame->pts       = dp->last_frame_pts == AV_NOPTS_VALUE ? AV_NOPTS_VALUE :
                              dp->last_frame_pts + dp->last_frame_duration_est;
        dt.frame->time_base = dp->last_frame_tb;

        ret = sch_dec_send(dp->sch, dp->sch_idx, 0, dt.frame);
        if (ret < 0 && ret != AVERROR_EOF) {
            av_log(dp, AV_LOG_FATAL,
                   "Error signalling EOF timestamp: %s\n", av_err2str(ret));
            goto finish;
        }
        ret = 0;

        err_rate = (dp->dec.frames_decoded || dp->dec.decode_errors) ?
                   dp->dec.decode_errors / (dp->dec.frames_decoded + dp->dec.decode_errors) : 0.f;
        if (err_rate > max_error_rate) {
            av_log(dp, AV_LOG_FATAL, "Decode error rate %g exceeds maximum %g\n",
                   err_rate, max_error_rate);
            ret = FFMPEG_ERROR_RATE_EXCEEDED;
        } else if (err_rate)
            av_log(dp, AV_LOG_VERBOSE, "Decode error rate %g\n", err_rate);
    }

finish:
    dec_thread_uninit(&dt);

    return ret;
}

int dec_request_view(Decoder *d, const ViewSpecifier *vs,
                     SchedulerNode *src)
{
    DecoderPriv *dp = dp_from_dec(d);
    unsigned out_idx = 0;
    int ret;

    if (dp->multiview_user_config) {
        if (!vs || vs->type == VIEW_SPECIFIER_TYPE_NONE) {
            *src = SCH_DEC_OUT(dp->sch_idx, 0);
            return 0;
        }

        av_log(dp, AV_LOG_ERROR,
               "Manually selecting views with -view_ids cannot be combined "
               "with view selection via stream specifiers. It is strongly "
               "recommended you always use stream specifiers only.\n");
        return AVERROR(EINVAL);
    }

    // when multiview_user_config is not set, NONE specifier is treated
    // as requesting the base view
    vs = (vs && vs->type != VIEW_SPECIFIER_TYPE_NONE) ? vs :
         &(ViewSpecifier){ .type = VIEW_SPECIFIER_TYPE_IDX, .val = 0 };

    // check if the specifier matches an already-existing one
    for (int i = 0; i < dp->nb_views_requested; i++) {
        const ViewSpecifier *vs1 = &dp->views_requested[i].vs;

        if (vs->type == vs1->type &&
            (vs->type == VIEW_SPECIFIER_TYPE_ALL || vs->val == vs1->val)) {
            *src = SCH_DEC_OUT(dp->sch_idx, dp->views_requested[i].out_idx);
            return 0;
        }
    }

    // we use a bitmask to map view IDs to decoder outputs, which
    // limits the number of outputs allowed
    if (dp->nb_views_requested >= sizeof(dp->view_map[0].out_mask) * 8) {
        av_log(dp, AV_LOG_ERROR, "Too many view specifiers\n");
        return AVERROR(ENOSYS);
    }

    ret = GROW_ARRAY(dp->views_requested, dp->nb_views_requested);
    if (ret < 0)
        return ret;

    if (dp->nb_views_requested > 1) {
        ret = sch_add_dec_output(dp->sch, dp->sch_idx);
        if (ret < 0)
            return ret;
        out_idx = ret;
    }

    dp->views_requested[dp->nb_views_requested - 1].out_idx = out_idx;
    dp->views_requested[dp->nb_views_requested - 1].vs      = *vs;

    *src = SCH_DEC_OUT(dp->sch_idx,
                       dp->views_requested[dp->nb_views_requested - 1].out_idx);

    return 0;
}

static int multiview_setup(DecoderPriv *dp, AVCodecContext *dec_ctx)
{
    unsigned views_wanted = 0;

    unsigned nb_view_ids_av, nb_view_ids;
    unsigned *view_ids_av = NULL, *view_pos_av = NULL;
    int      *view_ids    = NULL;
    int ret;

    // no views/only base view were requested - do nothing
    if (!dp->nb_views_requested ||
        (dp->nb_views_requested == 1                               &&
         dp->views_requested[0].vs.type == VIEW_SPECIFIER_TYPE_IDX &&
         dp->views_requested[0].vs.val  == 0))
        return 0;

    av_freep(&dp->view_map);
    dp->nb_view_map = 0;

    // retrieve views available in current CVS
    ret = av_opt_get_array_size(dec_ctx, "view_ids_available",
                                AV_OPT_SEARCH_CHILDREN, &nb_view_ids_av);
    if (ret < 0) {
        av_log(dp, AV_LOG_ERROR,
               "Multiview decoding requested, but decoder '%s' does not "
               "support it\n", dec_ctx->codec->name);
        return AVERROR(ENOSYS);
    }

    if (nb_view_ids_av) {
        unsigned nb_view_pos_av;

        if (nb_view_ids_av >= sizeof(views_wanted) * 8) {
            av_log(dp, AV_LOG_ERROR, "Too many views in video: %u\n", nb_view_ids_av);
            ret = AVERROR(ENOSYS);
            goto fail;
        }

        view_ids_av = av_calloc(nb_view_ids_av, sizeof(*view_ids_av));
        if (!view_ids_av) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        ret = av_opt_get_array(dec_ctx, "view_ids_available",
                               AV_OPT_SEARCH_CHILDREN, 0, nb_view_ids_av,
                               AV_OPT_TYPE_UINT, view_ids_av);
        if (ret < 0)
            goto fail;

        ret = av_opt_get_array_size(dec_ctx, "view_pos_available",
                                    AV_OPT_SEARCH_CHILDREN, &nb_view_pos_av);
        if (ret >= 0 && nb_view_pos_av == nb_view_ids_av) {
            view_pos_av = av_calloc(nb_view_ids_av, sizeof(*view_pos_av));
            if (!view_pos_av) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }

            ret = av_opt_get_array(dec_ctx, "view_pos_available",
                                   AV_OPT_SEARCH_CHILDREN, 0, nb_view_ids_av,
                                   AV_OPT_TYPE_UINT, view_pos_av);
            if (ret < 0)
                goto fail;
        }
    } else {
        // assume there is a single view with ID=0
        nb_view_ids_av = 1;
        view_ids_av = av_calloc(nb_view_ids_av, sizeof(*view_ids_av));
        view_pos_av = av_calloc(nb_view_ids_av, sizeof(*view_pos_av));
        if (!view_ids_av || !view_pos_av) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        view_pos_av[0] = AV_STEREO3D_VIEW_UNSPEC;
    }

    dp->view_map = av_calloc(nb_view_ids_av, sizeof(*dp->view_map));
    if (!dp->view_map) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    dp->nb_view_map = nb_view_ids_av;

    for (int i = 0; i < dp->nb_view_map; i++)
        dp->view_map[i].id = view_ids_av[i];

    // figure out which views should go to which output
    for (int i = 0; i < dp->nb_views_requested; i++) {
        const ViewSpecifier *vs = &dp->views_requested[i].vs;

        switch (vs->type) {
        case VIEW_SPECIFIER_TYPE_IDX:
            if (vs->val >= nb_view_ids_av) {
                av_log(dp, exit_on_error ? AV_LOG_ERROR : AV_LOG_WARNING,
                       "View with index %u requested, but only %u views available "
                       "in current video sequence (more views may or may not be "
                       "available in later sequences).\n",
                       vs->val, nb_view_ids_av);
                if (exit_on_error) {
                    ret = AVERROR(EINVAL);
                    goto fail;
                }

                continue;
            }
            views_wanted                   |= 1U   << vs->val;
            dp->view_map[vs->val].out_mask |= 1ULL << i;

            break;
        case VIEW_SPECIFIER_TYPE_ID: {
            int view_idx = -1;

            for (unsigned j = 0; j < nb_view_ids_av; j++) {
                if (view_ids_av[j] == vs->val) {
                    view_idx = j;
                    break;
                }
            }
            if (view_idx < 0) {
                av_log(dp, exit_on_error ? AV_LOG_ERROR : AV_LOG_WARNING,
                       "View with ID %u requested, but is not available "
                       "in the video sequence\n", vs->val);
                if (exit_on_error) {
                    ret = AVERROR(EINVAL);
                    goto fail;
                }

                continue;
            }
            views_wanted                    |= 1U   << view_idx;
            dp->view_map[view_idx].out_mask |= 1ULL << i;

            break;
            }
        case VIEW_SPECIFIER_TYPE_POS: {
            int view_idx = -1;

            for (unsigned j = 0; view_pos_av && j < nb_view_ids_av; j++) {
                if (view_pos_av[j] == vs->val) {
                    view_idx = j;
                    break;
                }
            }
            if (view_idx < 0) {
                av_log(dp, exit_on_error ? AV_LOG_ERROR : AV_LOG_WARNING,
                       "View position '%s' requested, but is not available "
                       "in the video sequence\n", av_stereo3d_view_name(vs->val));
                if (exit_on_error) {
                    ret = AVERROR(EINVAL);
                    goto fail;
                }

                continue;
            }
            views_wanted                    |= 1U   << view_idx;
            dp->view_map[view_idx].out_mask |= 1ULL << i;

            break;
            }
        case VIEW_SPECIFIER_TYPE_ALL:
            views_wanted |= (1U << nb_view_ids_av) - 1;

            for (int j = 0; j < dp->nb_view_map; j++)
                dp->view_map[j].out_mask |= 1ULL << i;

            break;
        }
    }
    if (!views_wanted) {
        av_log(dp, AV_LOG_ERROR, "No views were selected for decoding\n");
        ret = AVERROR(EINVAL);
        goto fail;
    }

    // signal to decoder which views we want
    nb_view_ids = stdc_count_ones(views_wanted);
    view_ids = av_malloc_array(nb_view_ids, sizeof(*view_ids));
    if (!view_ids) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    for (unsigned i = 0; i < nb_view_ids; i++) {
        int pos;

        av_assert0(views_wanted);
        pos = stdc_trailing_zeros(views_wanted);
        views_wanted &= ~(1U << pos);

        view_ids[i] = view_ids_av[pos];
    }

    // unset view_ids in case we set it earlier
    av_opt_set(dec_ctx, "view_ids", NULL, AV_OPT_SEARCH_CHILDREN);

    ret = av_opt_set_array(dec_ctx, "view_ids", AV_OPT_SEARCH_CHILDREN,
                           0, nb_view_ids, AV_OPT_TYPE_INT, view_ids);
    if (ret < 0)
        goto fail;

    if (!dp->frame_tmp_ref) {
        dp->frame_tmp_ref = av_frame_alloc();
        if (!dp->frame_tmp_ref) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

fail:
    av_freep(&view_ids_av);
    av_freep(&view_pos_av);
    av_freep(&view_ids);

    return ret;
}

static void multiview_check_manual(DecoderPriv *dp, const AVDictionary *dec_opts)
{
    if (av_dict_get(dec_opts, "view_ids", NULL, 0)) {
        av_log(dp, AV_LOG_WARNING, "Manually selecting views with -view_ids "
               "is not recommended, use view specifiers instead\n");
        dp->multiview_user_config = 1;
    }
}

static void check_options(DecoderPriv *dp, const AVDictionary *dec_opts)
{
    AVDictionaryEntry *dict_entry = av_dict_get(dec_opts, "fix_sub_duration", NULL, 0);

    if (dict_entry) {
        dp->fix_sub_duration = !!dict_entry->value;
    }
}

static enum AVPixelFormat get_format(AVCodecContext *s, const enum AVPixelFormat *pix_fmts)
{
    DecoderPriv  *dp = s->opaque;
    const enum AVPixelFormat *p;
    int ret;

    ret = multiview_setup(dp, s);
    if (ret < 0) {
        av_log(dp, AV_LOG_ERROR, "Error setting up multiview decoding: %s\n",
               av_err2str(ret));
        return AV_PIX_FMT_NONE;
    }

    for (p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(*p);
        const AVCodecHWConfig  *config = NULL;

        if (!(desc->flags & AV_PIX_FMT_FLAG_HWACCEL))
            break;

        if (dp->hwaccel_id == HWACCEL_GENERIC ||
            dp->hwaccel_id == HWACCEL_AUTO) {
            for (int i = 0;; i++) {
                config = avcodec_get_hw_config(s->codec, i);
                if (!config)
                    break;
                if (!(config->methods &
                      AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX))
                    continue;
                if (config->pix_fmt == *p)
                    break;
            }
        }
        if (config && config->device_type == dp->hwaccel_device_type) {
            dp->hwaccel_pix_fmt = *p;
            break;
        }
    }

    return *p;
}

static int get_buffer(AVCodecContext *dec_ctx, AVFrame *frame, int flags)
{
    DecoderPriv *dp = dec_ctx->opaque;

    // for multiview video, store the output mask in frame opaque
    if (dp->nb_view_map) {
        const AVFrameSideData *sd = av_frame_get_side_data(frame, AV_FRAME_DATA_VIEW_ID);
        int view_id = sd ? *(int*)sd->data : 0;

        for (int i = 0; i < dp->nb_view_map; i++) {
            if (dp->view_map[i].id == view_id) {
                frame->opaque = (void*)dp->view_map[i].out_mask;
                break;
            }
        }
    }

    return avcodec_default_get_buffer2(dec_ctx, frame, flags);
}

static HWDevice *hw_device_match_by_codec(const AVCodec *codec)
{
    const AVCodecHWConfig *config;
    HWDevice *dev;
    for (int i = 0;; i++) {
        config = avcodec_get_hw_config(codec, i);
        if (!config)
            return NULL;
        if (!(config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX))
            continue;
        dev = hw_device_get_by_type(config->device_type);
        if (dev)
            return dev;
    }
}

static int hw_device_setup_for_decode(DecoderPriv *dp,
                                      const AVCodec *codec,
                                      const char *hwaccel_device)
{
    const AVCodecHWConfig *config;
    enum AVHWDeviceType type;
    HWDevice *dev = NULL;
    int err, auto_device = 0;

    if (hwaccel_device) {
        dev = hw_device_get_by_name(hwaccel_device);
        if (!dev) {
            if (dp->hwaccel_id == HWACCEL_AUTO) {
                auto_device = 1;
            } else if (dp->hwaccel_id == HWACCEL_GENERIC) {
                type = dp->hwaccel_device_type;
                err = hw_device_init_from_type(type, hwaccel_device,
                                               &dev);
            } else {
                // This will be dealt with by API-specific initialisation
                // (using hwaccel_device), so nothing further needed here.
                return 0;
            }
        } else {
            if (dp->hwaccel_id == HWACCEL_AUTO) {
                dp->hwaccel_device_type = dev->type;
            } else if (dp->hwaccel_device_type != dev->type) {
                av_log(dp, AV_LOG_ERROR, "Invalid hwaccel device "
                       "specified for decoder: device %s of type %s is not "
                       "usable with hwaccel %s.\n", dev->name,
                       av_hwdevice_get_type_name(dev->type),
                       av_hwdevice_get_type_name(dp->hwaccel_device_type));
                return AVERROR(EINVAL);
            }
        }
    } else {
        if (dp->hwaccel_id == HWACCEL_AUTO) {
            auto_device = 1;
        } else if (dp->hwaccel_id == HWACCEL_GENERIC) {
            type = dp->hwaccel_device_type;
            dev = hw_device_get_by_type(type);

            // When "-qsv_device device" is used, an internal QSV device named
            // as "__qsv_device" is created. Another QSV device is created too
            // if "-init_hw_device qsv=name:device" is used. There are 2 QSV devices
            // if both "-qsv_device device" and "-init_hw_device qsv=name:device"
            // are used, hw_device_get_by_type(AV_HWDEVICE_TYPE_QSV) returns NULL.
            // To keep back-compatibility with the removed ad-hoc libmfx setup code,
            // call hw_device_get_by_name("__qsv_device") to select the internal QSV
            // device.
            if (!dev && type == AV_HWDEVICE_TYPE_QSV)
                dev = hw_device_get_by_name("__qsv_device");

            if (!dev)
                err = hw_device_init_from_type(type, NULL, &dev);
        } else {
            dev = hw_device_match_by_codec(codec);
            if (!dev) {
                // No device for this codec, but not using generic hwaccel
                // and therefore may well not need one - ignore.
                return 0;
            }
        }
    }

    if (auto_device) {
        if (!avcodec_get_hw_config(codec, 0)) {
            // Decoder does not support any hardware devices.
            return 0;
        }
        for (int i = 0; !dev; i++) {
            config = avcodec_get_hw_config(codec, i);
            if (!config)
                break;
            type = config->device_type;
            dev = hw_device_get_by_type(type);
            if (dev) {
                av_log(dp, AV_LOG_INFO, "Using auto "
                       "hwaccel type %s with existing device %s.\n",
                       av_hwdevice_get_type_name(type), dev->name);
            }
        }
        for (int i = 0; !dev; i++) {
            config = avcodec_get_hw_config(codec, i);
            if (!config)
                break;
            type = config->device_type;
            // Try to make a new device of this type.
            err = hw_device_init_from_type(type, hwaccel_device,
                                           &dev);
            if (err < 0) {
                // Can't make a device of this type.
                continue;
            }
            if (hwaccel_device) {
                av_log(dp, AV_LOG_INFO, "Using auto "
                       "hwaccel type %s with new device created "
                       "from %s.\n", av_hwdevice_get_type_name(type),
                       hwaccel_device);
            } else {
                av_log(dp, AV_LOG_INFO, "Using auto "
                       "hwaccel type %s with new default device.\n",
                       av_hwdevice_get_type_name(type));
            }
        }
        if (dev) {
            dp->hwaccel_device_type = type;
        } else {
            av_log(dp, AV_LOG_INFO, "Auto hwaccel "
                   "disabled: no device found.\n");
            dp->hwaccel_id = HWACCEL_NONE;
            return 0;
        }
    }

    if (!dev) {
        av_log(dp, AV_LOG_ERROR, "No device available "
               "for decoder: device type %s needed for codec %s.\n",
               av_hwdevice_get_type_name(type), codec->name);
        return err;
    }

    dp->dec_ctx->hw_device_ctx = av_buffer_ref(dev->device_ref);
    if (!dp->dec_ctx->hw_device_ctx)
        return AVERROR(ENOMEM);

    return 0;
}

static int dec_open(DecoderPriv *dp, AVDictionary **dec_opts,
                    const DecoderOpts *o, AVFrame *param_out)
{
    const AVCodec *codec = o->codec;
    int ret;

    dp->flags      = o->flags;
    dp->log_parent = o->log_parent;

    dp->dec.type                = codec->type;
    dp->framerate_in            = o->framerate;

    dp->hwaccel_id              = o->hwaccel_id;
    dp->hwaccel_device_type     = o->hwaccel_device_type;
    dp->hwaccel_output_format   = o->hwaccel_output_format;

    snprintf(dp->log_name, sizeof(dp->log_name), "dec:%s", codec->name);

    dp->parent_name = av_strdup(o->name ? o->name : "");
    if (!dp->parent_name)
        return AVERROR(ENOMEM);

    dp->sar_override = o->par->sample_aspect_ratio;

    dp->dec_ctx = avcodec_alloc_context3(codec);
    if (!dp->dec_ctx)
        return AVERROR(ENOMEM);

    ret = avcodec_parameters_to_context(dp->dec_ctx, o->par);
    if (ret < 0) {
        av_log(dp, AV_LOG_ERROR, "Error initializing the decoder context.\n");
        return ret;
    }

    dp->dec_ctx->opaque                = dp;
    dp->dec_ctx->get_format            = get_format;
    dp->dec_ctx->get_buffer2           = get_buffer;
    dp->dec_ctx->pkt_timebase          = o->time_base;

    if (!av_dict_get(*dec_opts, "threads", NULL, 0))
        av_dict_set(dec_opts, "threads", "auto", 0);

    ret = hw_device_setup_for_decode(dp, codec, o->hwaccel_device);
    if (ret < 0) {
        av_log(dp, AV_LOG_ERROR,
               "Hardware device setup failed for decoder: %s\n",
               av_err2str(ret));
        return ret;
    }

    ret = av_opt_set_dict2(dp->dec_ctx, dec_opts, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(dp, AV_LOG_ERROR, "Error applying decoder options: %s\n",
               av_err2str(ret));
        return ret;
    }
    ret = check_avoptions(*dec_opts);
    if (ret < 0)
        return ret;

    dp->dec_ctx->flags |= AV_CODEC_FLAG_COPY_OPAQUE;
    if (o->flags & DECODER_FLAG_BITEXACT)
        dp->dec_ctx->flags |= AV_CODEC_FLAG_BITEXACT;

    // we apply cropping ourselves
    dp->apply_cropping          = dp->dec_ctx->apply_cropping;
    dp->dec_ctx->apply_cropping = 0;

    if ((ret = avcodec_open2(dp->dec_ctx, codec, NULL)) < 0) {
        av_log(dp, AV_LOG_ERROR, "Error while opening decoder: %s\n",
               av_err2str(ret));
        return ret;
    }

    if (dp->dec_ctx->hw_device_ctx) {
        // Update decoder extra_hw_frames option to account for the
        // frames held in queues inside the ffmpeg utility.  This is
        // called after avcodec_open2() because the user-set value of
        // extra_hw_frames becomes valid in there, and we need to add
        // this on top of it.
        int extra_frames = DEFAULT_FRAME_THREAD_QUEUE_SIZE;
        if (dp->dec_ctx->extra_hw_frames >= 0)
            dp->dec_ctx->extra_hw_frames += extra_frames;
        else
            dp->dec_ctx->extra_hw_frames = extra_frames;
    }

    dp->dec.subtitle_header      = dp->dec_ctx->subtitle_header;
    dp->dec.subtitle_header_size = dp->dec_ctx->subtitle_header_size;

    if (param_out) {
        if (dp->dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
            param_out->format               = dp->dec_ctx->sample_fmt;
            param_out->sample_rate          = dp->dec_ctx->sample_rate;

            ret = av_channel_layout_copy(&param_out->ch_layout, &dp->dec_ctx->ch_layout);
            if (ret < 0)
                return ret;
        } else if (dp->dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
            param_out->format               = dp->dec_ctx->pix_fmt;
            param_out->width                = dp->dec_ctx->width;
            param_out->height               = dp->dec_ctx->height;
            param_out->sample_aspect_ratio  = dp->dec_ctx->sample_aspect_ratio;
            param_out->colorspace           = dp->dec_ctx->colorspace;
            param_out->color_range          = dp->dec_ctx->color_range;
        }

        av_frame_side_data_free(&param_out->side_data, &param_out->nb_side_data);
        ret = clone_side_data(&param_out->side_data, &param_out->nb_side_data,
                              dp->dec_ctx->decoded_side_data, dp->dec_ctx->nb_decoded_side_data, 0);
        if (ret < 0)
            return ret;
        param_out->time_base = dp->dec_ctx->pkt_timebase;
    }

    return 0;
}

int dec_init(Decoder **pdec, Scheduler *sch,
             AVDictionary **dec_opts, const DecoderOpts *o,
             AVFrame *param_out)
{
    DecoderPriv *dp;
    int ret;

    *pdec = NULL;

    ret = dec_alloc(&dp, sch, !!(o->flags & DECODER_FLAG_SEND_END_TS));
    if (ret < 0)
        return ret;

    multiview_check_manual(dp, *dec_opts);
    check_options(dp, *dec_opts);

    ret = dec_open(dp, dec_opts, o, param_out);
    if (ret < 0)
        goto fail;

    *pdec = &dp->dec;

    return dp->sch_idx;
fail:
    dec_free((Decoder**)&dp);
    return ret;
}

int dec_create(const OptionsContext *o, const char *arg, Scheduler *sch)
{
    DecoderPriv *dp;

    OutputFile      *of;
    OutputStream    *ost;
    int of_index, ost_index;
    char *p;

    unsigned enc_idx;
    int ret;

    ret = dec_alloc(&dp, sch, 0);
    if (ret < 0)
        return ret;

    dp->index = nb_decoders;

    ret = GROW_ARRAY(decoders, nb_decoders);
    if (ret < 0) {
        dec_free((Decoder **)&dp);
        return ret;
    }

    decoders[nb_decoders - 1] = (Decoder *)dp;

    of_index = strtol(arg, &p, 0);
    if (of_index < 0 || of_index >= nb_output_files) {
        av_log(dp, AV_LOG_ERROR, "Invalid output file index '%d' in %s\n", of_index, arg);
        return AVERROR(EINVAL);
    }
    of = output_files[of_index];

    ost_index = strtol(p + 1, NULL, 0);
    if (ost_index < 0 || ost_index >= of->nb_streams) {
        av_log(dp, AV_LOG_ERROR, "Invalid output stream index '%d' in %s\n", ost_index, arg);
        return AVERROR(EINVAL);
    }
    ost = of->streams[ost_index];

    if (!ost->enc) {
        av_log(dp, AV_LOG_ERROR, "Output stream %s has no encoder\n", arg);
        return AVERROR(EINVAL);
    }

    dp->dec.type = ost->type;

    ret = enc_loopback(ost->enc);
    if (ret < 0)
        return ret;
    enc_idx = ret;

    ret = sch_connect(sch, SCH_ENC(enc_idx), SCH_DEC_IN(dp->sch_idx));
    if (ret < 0)
        return ret;

    ret = av_dict_copy(&dp->standalone_init.opts, o->g->codec_opts, 0);
    if (ret < 0)
        return ret;

    multiview_check_manual(dp, dp->standalone_init.opts);
    check_options(dp, dp->standalone_init.opts);

    if (o->codec_names.nb_opt) {
        const char *name = o->codec_names.opt[o->codec_names.nb_opt - 1].u.str;
        dp->standalone_init.codec = avcodec_find_decoder_by_name(name);
        if (!dp->standalone_init.codec) {
            av_log(dp, AV_LOG_ERROR, "No such decoder: %s\n", name);
            return AVERROR_DECODER_NOT_FOUND;
        }
    }

    return 0;
}

int dec_filter_add(Decoder *d, InputFilter *ifilter, InputFilterOptions *opts,
                   const ViewSpecifier *vs, SchedulerNode *src)
{
    DecoderPriv *dp = dp_from_dec(d);
    char name[16];

    snprintf(name, sizeof(name), "dec%d", dp->index);
    opts->name = av_strdup(name);
    if (!opts->name)
        return AVERROR(ENOMEM);

    return dec_request_view(d, vs, src);
}
