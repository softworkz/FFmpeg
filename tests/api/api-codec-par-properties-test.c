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
#include "libavcodec/codec_par.h"
#include "libavutil/log.h"

static int test_from_context(void)
{
    AVCodecContext *ctx = NULL;
    AVCodecParameters *par = NULL;
    int ret = 0;

    ctx = avcodec_alloc_context3(NULL);
    if (!ctx) {
        av_log(NULL, AV_LOG_ERROR, "Failed to allocate AVCodecContext\n");
        return AVERROR(ENOMEM);
    }

    par = avcodec_parameters_alloc();
    if (!par) {
        av_log(NULL, AV_LOG_ERROR, "Failed to allocate AVCodecParameters\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    ctx->codec_type = AVMEDIA_TYPE_VIDEO;
    ctx->codec_id = AV_CODEC_ID_H264;
    ctx->properties = FF_CODEC_PROPERTY_CLOSED_CAPTIONS | FF_CODEC_PROPERTY_FILM_GRAIN;

    ret = avcodec_parameters_from_context(par, ctx);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "avcodec_parameters_from_context failed\n");
        goto end;
    }

    if (par->properties != ctx->properties) {
        av_log(NULL, AV_LOG_ERROR,
               "from_context: properties mismatch: par=0x%x ctx=0x%x\n",
               par->properties, ctx->properties);
        ret = AVERROR(EINVAL);
        goto end;
    }

    if (!(par->properties & FF_CODEC_PROPERTY_CLOSED_CAPTIONS)) {
        av_log(NULL, AV_LOG_ERROR,
               "from_context: CLOSED_CAPTIONS flag not set in par\n");
        ret = AVERROR(EINVAL);
        goto end;
    }

    if (!(par->properties & FF_CODEC_PROPERTY_FILM_GRAIN)) {
        av_log(NULL, AV_LOG_ERROR,
               "from_context: FILM_GRAIN flag not set in par\n");
        ret = AVERROR(EINVAL);
        goto end;
    }

    av_log(NULL, AV_LOG_INFO, "from_context: properties correctly copied (0x%x)\n",
           par->properties);

end:
    avcodec_parameters_free(&par);
    avcodec_free_context(&ctx);
    return ret;
}

static int test_to_context(void)
{
    AVCodecContext *ctx = NULL;
    AVCodecParameters *par = NULL;
    int ret = 0;

    ctx = avcodec_alloc_context3(NULL);
    if (!ctx) {
        av_log(NULL, AV_LOG_ERROR, "Failed to allocate AVCodecContext\n");
        return AVERROR(ENOMEM);
    }

    par = avcodec_parameters_alloc();
    if (!par) {
        av_log(NULL, AV_LOG_ERROR, "Failed to allocate AVCodecParameters\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    par->codec_type = AVMEDIA_TYPE_VIDEO;
    par->codec_id = AV_CODEC_ID_H264;
    par->properties = FF_CODEC_PROPERTY_CLOSED_CAPTIONS;

    ret = avcodec_parameters_to_context(ctx, par);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "avcodec_parameters_to_context failed\n");
        goto end;
    }

    if (ctx->properties != par->properties) {
        av_log(NULL, AV_LOG_ERROR,
               "to_context: properties mismatch: ctx=0x%x par=0x%x\n",
               ctx->properties, par->properties);
        ret = AVERROR(EINVAL);
        goto end;
    }

    if (!(ctx->properties & FF_CODEC_PROPERTY_CLOSED_CAPTIONS)) {
        av_log(NULL, AV_LOG_ERROR,
               "to_context: CLOSED_CAPTIONS flag not set in ctx\n");
        ret = AVERROR(EINVAL);
        goto end;
    }

    av_log(NULL, AV_LOG_INFO, "to_context: properties correctly copied (0x%x)\n",
           ctx->properties);

end:
    avcodec_parameters_free(&par);
    avcodec_free_context(&ctx);
    return ret;
}

static int test_roundtrip(void)
{
    AVCodecContext *ctx = NULL;
    AVCodecContext *ctx2 = NULL;
    AVCodecParameters *par = NULL;
    int ret = 0;

    ctx = avcodec_alloc_context3(NULL);
    ctx2 = avcodec_alloc_context3(NULL);
    par = avcodec_parameters_alloc();
    if (!ctx || !ctx2 || !par) {
        av_log(NULL, AV_LOG_ERROR, "Failed to allocate test structures\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    ctx->codec_type = AVMEDIA_TYPE_VIDEO;
    ctx->codec_id = AV_CODEC_ID_H264;
    ctx->properties = FF_CODEC_PROPERTY_CLOSED_CAPTIONS | FF_CODEC_PROPERTY_FILM_GRAIN |
                      FF_CODEC_PROPERTY_LOSSLESS;

    ret = avcodec_parameters_from_context(par, ctx);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "roundtrip: from_context failed\n");
        goto end;
    }

    ret = avcodec_parameters_to_context(ctx2, par);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "roundtrip: to_context failed\n");
        goto end;
    }

    if (ctx2->properties != ctx->properties) {
        av_log(NULL, AV_LOG_ERROR,
               "roundtrip: properties mismatch: original=0x%x roundtripped=0x%x\n",
               ctx->properties, ctx2->properties);
        ret = AVERROR(EINVAL);
        goto end;
    }

    av_log(NULL, AV_LOG_INFO, "roundtrip: properties correctly preserved (0x%x)\n",
           ctx2->properties);

end:
    avcodec_parameters_free(&par);
    avcodec_free_context(&ctx);
    avcodec_free_context(&ctx2);
    return ret;
}

int main(void)
{
    int ret;

    ret = test_from_context();
    if (ret < 0)
        return 1;

    ret = test_to_context();
    if (ret < 0)
        return 1;

    ret = test_roundtrip();
    if (ret < 0)
        return 1;

    return 0;
}
