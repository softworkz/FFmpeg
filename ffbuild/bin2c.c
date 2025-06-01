/*
 * This file is part of FFmpeg.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "config.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "libavutil/attributes.h"
#include "libavutil/macros.h"

#if CONFIG_PTX_COMPRESSION || CONFIG_RESOURCE_COMPRESSION
#include <zlib.h>

#define MAX_BUF_SIZE UINT32_MAX

static int read_file_and_compress(unsigned char **compressed_datap, uint32_t *compressed_sizep,
                                  FILE *input)
{
    z_stream zstream = { 0 };
    int ret, zret = deflateInit(&zstream, 9);

    if (zret != Z_OK)
        return -1;

    uint32_t buffer_size = 0, compressed_size = 0;
    unsigned char *compressed_data = NULL;
    int flush = Z_NO_FLUSH;

    do {
        unsigned char tmp[4096];
        size_t read = fread(tmp, 1, sizeof(tmp), input);
        if (read < sizeof(tmp))
            flush = Z_FINISH;

        zstream.next_in  = tmp;
        zstream.avail_in = read;

        do {
            if (compressed_size >= buffer_size) {
                if (buffer_size == MAX_BUF_SIZE) {
                    ret = -1;
                    goto fail;
                }

                buffer_size = buffer_size ? FFMIN(compressed_size * 2ll, MAX_BUF_SIZE) : 4096;
                void *tmp_ptr = realloc(compressed_data, buffer_size);
                if (!tmp_ptr) {
                    ret = -1;
                    goto fail;
                }
                compressed_data = tmp_ptr;
            }

            zstream.next_out  = compressed_data + compressed_size;
            zstream.avail_out = buffer_size - compressed_size;

            zret = deflate(&zstream, flush);
            compressed_size = buffer_size - zstream.avail_out;
            if (zret == Z_STREAM_END)
                break;
            if (zret != Z_OK) {
                ret = -1;
                goto fail;
            }
        } while (zstream.avail_in > 0 || zstream.avail_out == 0);
    } while (flush != Z_FINISH);

    deflateEnd(&zstream);
    *compressed_datap = compressed_data;
    *compressed_sizep = compressed_size;

    return 0;
fail:
    free(compressed_data);
    deflateEnd(&zstream);

    return ret;
}

static int handle_compressed_file(FILE *input, FILE *output, unsigned *compressed_sizep)
{
    unsigned char *compressed_data;
    uint32_t compressed_size;

    int err = read_file_and_compress(&compressed_data, &compressed_size, input);
    if (err)
        return err;

    *compressed_sizep = compressed_size;

    for (unsigned i = 0; i < compressed_size; ++i)
        fprintf(output, "0x%02x, ", compressed_data[i]);

    free(compressed_data);

    return 0;
}
#endif

int main(int argc, char **argv)
{
    const char *name;
    FILE *input, *output;
    unsigned int length = 0;
    unsigned char data;
    av_unused int compression = 0;
    int arg_idx = 1;

    if (argc < 3)
        return 1;

    if (!strcmp(argv[arg_idx], "--compress")) {
#if !CONFIG_PTX_COMPRESSION && !CONFIG_RESOURCE_COMPRESSION
        fprintf(stderr, "Compression unsupported in this configuration. "
                        "This is a bug. Please report it.\n");
        return -1;
#endif
        compression = 1;
        ++arg_idx;
    }

    if (argc - arg_idx > 3)
        return 1;

    char *input_name = argv[arg_idx++];
    input = fopen(input_name, "rb");
    if (!input)
        return -1;

    output = fopen(argv[arg_idx++], "wb");
    if (!output) {
        fclose(input);
        return -1;
    }

    if (arg_idx < argc) {
        name = argv[arg_idx++];
    } else {
        size_t arglen = strlen(input_name);
        name = input_name;

        for (int i = 0; i < arglen; i++) {
            if (input_name[i] == '.')
                input_name[i] = '_';
            else if (input_name[i] == '/')
                name = &input_name[i+1];
        }
    }

    fprintf(output, "const unsigned char ff_%s_data[] = { ", name);

#if CONFIG_PTX_COMPRESSION || CONFIG_RESOURCE_COMPRESSION
    if (compression) {
        int err = handle_compressed_file(input, output, &length);
        if (err) {
            fclose(input);
            fclose(output);
            return err;
        }
    } else
#endif
    {
        while (fread(&data, 1, 1, input) > 0) {
            fprintf(output, "0x%02x, ", data);
            length++;
        }
    }

    fprintf(output, "0x00 };\n");
    fprintf(output, "const unsigned int ff_%s_len = %u;\n", name, length);

    fclose(output);

    if (ferror(input) || !feof(input)) {
        fclose(input);
        return -1;
    }

    fclose(input);

    return 0;
}
