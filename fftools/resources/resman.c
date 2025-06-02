/*
 * Copyright (c) 2025 - softworkz
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

/**
 * @file
 * output writers for filtergraph details
 */

#include "config.h"

#include <stddef.h>

#if CONFIG_RESOURCE_COMPRESSION
#include <zlib.h>
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/thread.h"
#endif

#include "resman.h"
#include "libavutil/avassert.h"
#include "libavutil/intreadwrite.h"

extern const unsigned char ff_graph_html_data[];
extern const unsigned char ff_graph_css_data[];

static const FFResourceDefinition resource_definitions[] = {
    [FF_RESOURCE_GRAPH_CSS]   = { FF_RESOURCE_GRAPH_CSS,   ff_graph_css_data  },
    [FF_RESOURCE_GRAPH_HTML]  = { FF_RESOURCE_GRAPH_HTML,  ff_graph_html_data },
};

#if CONFIG_RESOURCE_COMPRESSION
static const AVClass resman_class = {
    .class_name = "ResourceManager",
};

typedef struct ResourceManagerContext {
    const AVClass *class;
    char *resources[FF_ARRAY_ELEMS(resource_definitions)];
} ResourceManagerContext;

static AVMutex mutex = AV_MUTEX_INITIALIZER;

static ResourceManagerContext resman_ctx = { .class = &resman_class };


static int decompress_zlib(ResourceManagerContext *ctx, const uint8_t *in, char **out)
{
    // Allocate output buffer with extra byte for null termination
    uint32_t uncompressed_size = AV_RN32A(in);
    uint8_t *buf = av_malloc(uncompressed_size + 1);
    if (!buf) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate decompression buffer\n");
        return AVERROR(ENOMEM);
    }
    uLongf buf_size = uncompressed_size;
    int ret = uncompress(buf, &buf_size, in + 8, AV_RN32A(in + 4));
    if (ret != Z_OK || uncompressed_size != buf_size) {
        av_log(ctx, AV_LOG_ERROR, "Error uncompressing resource. zlib returned %d\n", ret);
        av_free(buf);
        return AVERROR_EXTERNAL;
    }

    buf[uncompressed_size] = 0; // Ensure null termination

    *out = (char *)buf;
    return 0;
}
#endif

void ff_resman_uninit(void)
{
#if CONFIG_RESOURCE_COMPRESSION
    ff_mutex_lock(&mutex);

    for (size_t i = 0; i < FF_ARRAY_ELEMS(resman_ctx.resources); ++i)
        av_freep(&resman_ctx.resources[i]);

    ff_mutex_unlock(&mutex);
#endif
}


const char *ff_resman_get_string(FFResourceId resource_id)
{
    const FFResourceDefinition *resource_definition = NULL;
    const char *res = NULL;
    av_unused unsigned idx;

    for (unsigned i = 0; i < FF_ARRAY_ELEMS(resource_definitions); ++i) {
        const FFResourceDefinition *def = &resource_definitions[i];
        if (def->resource_id == resource_id) {
            resource_definition = def;
            idx = i;
            break;
        }
    }

    av_assert1(resource_definition);

#if CONFIG_RESOURCE_COMPRESSION
    ff_mutex_lock(&mutex);

    ResourceManagerContext *ctx = &resman_ctx;

    if (!ctx->resources[idx]) {
        int ret = decompress_zlib(ctx, resource_definition->data, &ctx->resources[idx]);
        if (ret) {
            av_log(ctx, AV_LOG_ERROR, "Unable to decompress the resource with ID %d\n", resource_id);
            goto end;
        }
    }
    res = ctx->resources[idx];
end:
    ff_mutex_unlock(&mutex);
#else
    res = resource_definition->data;
#endif
    return res;
}
