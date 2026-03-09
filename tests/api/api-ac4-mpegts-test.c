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

/*
 * Tests that the MPEG-TS muxer writes the AC-4 registration descriptor
 * ('AC-4') in the PMT when an AC-4 audio stream is present (commit 136).
 * Also verifies the MPEG-TS demuxer correctly identifies the AC-4 codec
 * from the registration descriptor (commit 082).
 */

#include <stdio.h>
#include <string.h>
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/packet.h"
#include "libavutil/log.h"

static const uint8_t ac4_regd_tag[4] = { 'A', 'C', '-', '4' };

static int find_pattern(const uint8_t *buf, int buf_size,
                        const uint8_t *pattern, int pattern_size)
{
    for (int i = 0; i <= buf_size - pattern_size; i++) {
        if (memcmp(buf + i, pattern, pattern_size) == 0)
            return i;
    }
    return -1;
}

static int test_mpegts_pmt_descriptor(const char *filename)
{
    AVFormatContext *ofmt_ctx = NULL;
    AVStream *st = NULL;
    AVPacket *pkt = NULL;
    FILE *f = NULL;
    uint8_t *file_buf = NULL;
    long file_size;
    int ret, pos;

    ret = avformat_alloc_output_context2(&ofmt_ctx, NULL, "mpegts", filename);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed to allocate MPEG-TS output context\n");
        return ret;
    }

    st = avformat_new_stream(ofmt_ctx, NULL);
    if (!st) {
        av_log(NULL, AV_LOG_ERROR, "Failed to create stream\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id = AV_CODEC_ID_AC4;
    st->codecpar->sample_rate = 48000;
    st->codecpar->ch_layout.nb_channels = 2;
    st->time_base = (AVRational){1, 90000};

    ret = avio_open(&ofmt_ctx->pb, filename, AVIO_FLAG_WRITE);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed to open output file: %d\n", ret);
        goto end;
    }

    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed to write header: %d\n", ret);
        goto end;
    }

    pkt = av_packet_alloc();
    if (!pkt) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* Write a minimal dummy packet to force PMT output */
    ret = av_new_packet(pkt, 8);
    if (ret < 0)
        goto end;
    memset(pkt->data, 0, 8);
    pkt->stream_index = 0;
    pkt->pts = 0;
    pkt->dts = 0;
    ret = av_write_frame(ofmt_ctx, pkt);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed to write packet: %d\n", ret);
        goto end;
    }

    av_write_trailer(ofmt_ctx);
    avio_closep(&ofmt_ctx->pb);

    f = fopen(filename, "rb");
    if (!f) {
        av_log(NULL, AV_LOG_ERROR, "Failed to reopen file for reading\n");
        ret = AVERROR(EIO);
        goto end;
    }

    fseek(f, 0, SEEK_END);
    file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    file_buf = av_malloc(file_size);
    if (!file_buf) {
        ret = AVERROR(ENOMEM);
        fclose(f);
        goto end;
    }

    if (fread(file_buf, 1, file_size, f) != (size_t)file_size) {
        av_log(NULL, AV_LOG_ERROR, "Failed to read file\n");
        ret = AVERROR(EIO);
        fclose(f);
        goto end;
    }
    fclose(f);

    pos = find_pattern(file_buf, file_size, ac4_regd_tag, sizeof(ac4_regd_tag));
    if (pos < 0) {
        av_log(NULL, AV_LOG_ERROR,
               "mpegts_pmt: AC-4 registration descriptor 'AC-4' not found in output\n");
        ret = AVERROR(EINVAL);
        goto end;
    }

    av_log(NULL, AV_LOG_INFO,
           "mpegts_pmt: AC-4 registration descriptor found at offset %d (correct)\n",
           pos);
    ret = 0;

end:
    av_packet_free(&pkt);
    av_free(file_buf);
    if (ofmt_ctx && ofmt_ctx->pb)
        avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);
    return ret;
}

static int test_mpegts_demux_ac4(const char *filename)
{
    AVFormatContext *ifmt_ctx = NULL;
    int ret;

    ret = avformat_open_input(&ifmt_ctx, filename, av_find_input_format("mpegts"), NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "mpegts_demux: failed to open: %d\n", ret);
        return ret;
    }

    ret = avformat_find_stream_info(ifmt_ctx, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "mpegts_demux: failed to find stream info: %d\n", ret);
        goto end;
    }

    if (ifmt_ctx->nb_streams < 1) {
        av_log(NULL, AV_LOG_ERROR, "mpegts_demux: no streams found\n");
        ret = AVERROR(EINVAL);
        goto end;
    }

    if (ifmt_ctx->streams[0]->codecpar->codec_id != AV_CODEC_ID_AC4) {
        av_log(NULL, AV_LOG_ERROR,
               "mpegts_demux: expected AC4 codec_id, got %d\n",
               ifmt_ctx->streams[0]->codecpar->codec_id);
        ret = AVERROR(EINVAL);
        goto end;
    }

    av_log(NULL, AV_LOG_INFO,
           "mpegts_demux: AC-4 codec correctly identified from MPEG-TS (correct)\n");
    ret = 0;

end:
    avformat_close_input(&ifmt_ctx);
    return ret;
}

int main(void)
{
    const char *tmpfile = "tests/data/emby-ac4-mpegts-test.ts";
    int ret;

    ret = test_mpegts_pmt_descriptor(tmpfile);
    if (ret < 0)
        return 1;

    ret = test_mpegts_demux_ac4(tmpfile);
    if (ret < 0)
        return 1;

    remove(tmpfile);
    return 0;
}
