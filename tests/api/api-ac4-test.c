/*
 * Copyright (c) 2023 softworkz for Emby LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "libavcodec/avcodec.h"
#include "libavcodec/codec.h"
#include "libavcodec/codec_par.h"
#include "libavcodec/codec_id.h"
#include "libavutil/log.h"

static int test_frame_duration(void)
{
    AVCodecParameters *par = NULL;
    int duration;
    int ret = 0;

    par = avcodec_parameters_alloc();
    if (!par) {
        av_log(NULL, AV_LOG_ERROR, "Failed to allocate AVCodecParameters\n");
        return AVERROR(ENOMEM);
    }

    par->codec_type = AVMEDIA_TYPE_AUDIO;
    par->codec_id = AV_CODEC_ID_AC4;
    par->sample_rate = 48000;
    par->ch_layout.nb_channels = 2;

    duration = av_get_audio_frame_duration2(par, 0);
    if (duration != 1536) {
        av_log(NULL, AV_LOG_ERROR,
               "frame_duration: expected 1536, got %d\n", duration);
        ret = AVERROR(EINVAL);
        goto end;
    }

    av_log(NULL, AV_LOG_INFO, "frame_duration: AC-4 returns %d samples (correct)\n",
           duration);

end:
    avcodec_parameters_free(&par);
    return ret;
}

static int test_decoder_registration(void)
{
    const AVCodec *dec;

    dec = avcodec_find_decoder(AV_CODEC_ID_AC4);
    if (!dec) {
        av_log(NULL, AV_LOG_ERROR,
               "decoder_registration: AC-4 decoder not found\n");
        return AVERROR(EINVAL);
    }

    if (strcmp(dec->name, "ac4") != 0) {
        av_log(NULL, AV_LOG_ERROR,
               "decoder_registration: unexpected name '%s'\n", dec->name);
        return AVERROR(EINVAL);
    }

    if (dec->type != AVMEDIA_TYPE_AUDIO) {
        av_log(NULL, AV_LOG_ERROR,
               "decoder_registration: unexpected type %d\n", dec->type);
        return AVERROR(EINVAL);
    }

    if (dec->id != AV_CODEC_ID_AC4) {
        av_log(NULL, AV_LOG_ERROR,
               "decoder_registration: unexpected id %d\n", dec->id);
        return AVERROR(EINVAL);
    }

    av_log(NULL, AV_LOG_INFO,
           "decoder_registration: AC-4 decoder found (name=%s, type=audio)\n",
           dec->name);
    return 0;
}

static int test_decoder_capabilities(void)
{
    const AVCodec *dec;

    dec = avcodec_find_decoder(AV_CODEC_ID_AC4);
    if (!dec) {
        av_log(NULL, AV_LOG_ERROR,
               "decoder_caps: AC-4 decoder not found\n");
        return AVERROR(EINVAL);
    }

    if (!(dec->capabilities & AV_CODEC_CAP_DR1)) {
        av_log(NULL, AV_LOG_ERROR,
               "decoder_caps: AV_CODEC_CAP_DR1 not set\n");
        return AVERROR(EINVAL);
    }

    if (!(dec->capabilities & AV_CODEC_CAP_CHANNEL_CONF)) {
        av_log(NULL, AV_LOG_ERROR,
               "decoder_caps: AV_CODEC_CAP_CHANNEL_CONF not set\n");
        return AVERROR(EINVAL);
    }

    if (!dec->sample_fmts) {
        av_log(NULL, AV_LOG_ERROR,
               "decoder_caps: sample_fmts is NULL\n");
        return AVERROR(EINVAL);
    }

    if (dec->sample_fmts[0] != AV_SAMPLE_FMT_FLTP) {
        av_log(NULL, AV_LOG_ERROR,
               "decoder_caps: expected FLTP sample format, got %d\n",
               dec->sample_fmts[0]);
        return AVERROR(EINVAL);
    }

    av_log(NULL, AV_LOG_INFO,
           "decoder_caps: DR1=%d CHANNEL_CONF=%d sample_fmt=fltp (correct)\n",
           !!(dec->capabilities & AV_CODEC_CAP_DR1),
           !!(dec->capabilities & AV_CODEC_CAP_CHANNEL_CONF));
    return 0;
}

int main(void)
{
    int ret;

    ret = test_frame_duration();
    if (ret < 0)
        return 1;

    ret = test_decoder_registration();
    if (ret < 0)
        return 1;

    ret = test_decoder_capabilities();
    if (ret < 0)
        return 1;

    return 0;
}
