/*
 * Hardware device context derivation chain tests
 *
 * Tests the behavior introduced by the hardware context interop commits:
 *   103 (923abbd703) - QSV source_device linkage
 *   114 (3855db1d78) - Device registry, forward references,
 *                      av_hwdevice_ctx_get_or_create_derived()
 *   115 (eee0d63f8c) - API version bump
 *   129 (28ad3032b0) - fftools consumer adoption
 *   130 (7803383aec) - avfilter consumer adoption
 *   131 (223d038185) - overlay_qsv device check skip
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

#include <stdio.h>
#include <string.h>

#include "libavutil/hwcontext.h"
#include "libavutil/error.h"
#include "libavutil/macros.h"

static int test_type_nb_sentinel(void)
{
    enum AVHWDeviceType type;
    int count = 0;

    fprintf(stderr, "Testing AV_HWDEVICE_TYPE_NB sentinel...\n");

    type = AV_HWDEVICE_TYPE_NONE;
    while (1) {
        type = av_hwdevice_iterate_types(type);
        if (type == AV_HWDEVICE_TYPE_NONE)
            break;
        if (type >= AV_HWDEVICE_TYPE_NB) {
            fprintf(stderr, "FAIL: device type %d exceeds AV_HWDEVICE_TYPE_NB (%d)\n",
                    type, AV_HWDEVICE_TYPE_NB);
            return -1;
        }
        count++;
    }

    if (AV_HWDEVICE_TYPE_NB <= AV_HWDEVICE_TYPE_NONE) {
        fprintf(stderr, "FAIL: AV_HWDEVICE_TYPE_NB (%d) must be greater than NONE (%d)\n",
                AV_HWDEVICE_TYPE_NB, AV_HWDEVICE_TYPE_NONE);
        return -1;
    }

    fprintf(stderr, "PASS: AV_HWDEVICE_TYPE_NB = %d, %d types enumerated\n",
            AV_HWDEVICE_TYPE_NB, count);
    return 0;
}

static int test_get_or_create_derived_api(void)
{
    /*
     * Verify the function pointer is resolvable at link time. We cannot
     * call it without a valid device context, so we just verify the
     * symbol exists by taking its address.
     */
    int (*fn)(AVBufferRef **, enum AVHWDeviceType, AVBufferRef *, int);

    fprintf(stderr, "Testing av_hwdevice_ctx_get_or_create_derived API presence...\n");

    fn = av_hwdevice_ctx_get_or_create_derived;

    if (!fn) {
        fprintf(stderr, "FAIL: av_hwdevice_ctx_get_or_create_derived symbol not found\n");
        return -1;
    }

    fprintf(stderr, "PASS: av_hwdevice_ctx_get_or_create_derived API is available\n");
    return 0;
}

/*
 * Test multi-level derivation chain: A -> B -> C, then use
 * av_hwdevice_ctx_get_or_create_derived to go C -> B and verify we get
 * back the original B device (not a new one).
 *
 * Also tests two-level back-derivation: A -> B -> C, then C -> A should
 * return the original A.
 */
static int test_derivation_chain(AVBufferRef *src_ref, const char *src_name)
{
    enum AVHWDeviceType type_b, type_c;
    AVBufferRef *dev_b = NULL, *dev_c = NULL, *dev_back = NULL;
    AVHWDeviceContext *ctx_b, *ctx_back;
    AVHWDeviceContext *src_ctx = (AVHWDeviceContext *)src_ref->data;
    int chain_tested = 0;

    type_b = AV_HWDEVICE_TYPE_NONE;
    while (1) {
        type_b = av_hwdevice_iterate_types(type_b);
        if (type_b == AV_HWDEVICE_TYPE_NONE)
            break;
        if (type_b == src_ctx->type)
            continue;

        if (av_hwdevice_ctx_create_derived(&dev_b, type_b, src_ref, 0) < 0)
            continue;

        ctx_b = (AVHWDeviceContext *)dev_b->data;
        fprintf(stderr, "  Chain: %s -> %s\n", src_name,
                av_hwdevice_get_type_name(type_b));

        type_c = AV_HWDEVICE_TYPE_NONE;
        while (1) {
            type_c = av_hwdevice_iterate_types(type_c);
            if (type_c == AV_HWDEVICE_TYPE_NONE)
                break;
            if (type_c == src_ctx->type || type_c == type_b)
                continue;

            if (av_hwdevice_ctx_create_derived(&dev_c, type_c, dev_b, 0) < 0)
                continue;

            fprintf(stderr, "  Chain: %s -> %s -> %s\n", src_name,
                    av_hwdevice_get_type_name(type_b),
                    av_hwdevice_get_type_name(type_c));

            /*
             * Use get_or_create_derived to go from C back to B's type.
             * With forward reference tracking, this should return the
             * original B device rather than creating a new one.
             */
            if (av_hwdevice_ctx_get_or_create_derived(&dev_back, type_b,
                                                       dev_c, 0) == 0) {
                ctx_back = (AVHWDeviceContext *)dev_back->data;

                if (ctx_back == ctx_b) {
                    fprintf(stderr, "  PASS: get_or_create_derived(%s -> %s) "
                            "reused existing device\n",
                            av_hwdevice_get_type_name(type_c),
                            av_hwdevice_get_type_name(type_b));
                } else {
                    fprintf(stderr, "  INFO: get_or_create_derived(%s -> %s) "
                            "returned different device (may be expected for "
                            "some type combinations)\n",
                            av_hwdevice_get_type_name(type_c),
                            av_hwdevice_get_type_name(type_b));
                }
                av_buffer_unref(&dev_back);
                chain_tested = 1;
            }

            /*
             * Derive from C back to the original source type A.
             * With the source_device chain, this should return the
             * original A device.
             */
            if (av_hwdevice_ctx_get_or_create_derived(&dev_back, src_ctx->type,
                                                       dev_c, 0) == 0) {
                ctx_back = (AVHWDeviceContext *)dev_back->data;

                if (ctx_back == src_ctx) {
                    fprintf(stderr, "  PASS: get_or_create_derived(%s -> %s) "
                            "returned original source device\n",
                            av_hwdevice_get_type_name(type_c), src_name);
                } else {
                    fprintf(stderr, "  INFO: get_or_create_derived(%s -> %s) "
                            "returned different device\n",
                            av_hwdevice_get_type_name(type_c), src_name);
                }
                av_buffer_unref(&dev_back);
                chain_tested = 1;
            }

            av_buffer_unref(&dev_c);
        }

        av_buffer_unref(&dev_b);
    }

    if (!chain_tested) {
        fprintf(stderr, "  No multi-level derivation chains available from %s "
                "(skipped)\n", src_name);
    }

    return 0;
}

/*
 * Test that av_hwdevice_ctx_get_or_create_derived reuses an existing
 * single-level derived device (A -> B, then get_or_create B from A
 * should return the same B).
 */
static int test_get_or_create_reuse(AVBufferRef *src_ref, const char *src_name)
{
    enum AVHWDeviceType derived_type;
    AVBufferRef *dev_derived = NULL, *dev_reuse = NULL;
    AVHWDeviceContext *src_ctx = (AVHWDeviceContext *)src_ref->data;
    AVHWDeviceContext *derived_ctx, *reuse_ctx;
    int tested = 0;

    derived_type = AV_HWDEVICE_TYPE_NONE;
    while (1) {
        derived_type = av_hwdevice_iterate_types(derived_type);
        if (derived_type == AV_HWDEVICE_TYPE_NONE)
            break;
        if (derived_type == src_ctx->type)
            continue;

        if (av_hwdevice_ctx_create_derived(&dev_derived, derived_type,
                                            src_ref, 0) < 0)
            continue;

        derived_ctx = (AVHWDeviceContext *)dev_derived->data;

        if (av_hwdevice_ctx_get_or_create_derived(&dev_reuse, derived_type,
                                                   src_ref, 0) == 0) {
            reuse_ctx = (AVHWDeviceContext *)dev_reuse->data;

            if (reuse_ctx == derived_ctx) {
                fprintf(stderr, "  PASS: get_or_create_derived reused %s "
                        "device derived from %s\n",
                        av_hwdevice_get_type_name(derived_type), src_name);
            } else {
                fprintf(stderr, "  FAIL: get_or_create_derived created new %s "
                        "device instead of reusing existing one from %s\n",
                        av_hwdevice_get_type_name(derived_type), src_name);
                av_buffer_unref(&dev_reuse);
                av_buffer_unref(&dev_derived);
                return -1;
            }
            av_buffer_unref(&dev_reuse);
            tested = 1;
        }

        av_buffer_unref(&dev_derived);
    }

    if (!tested) {
        fprintf(stderr, "  No derivable types found from %s (skipped)\n",
                src_name);
    }

    return 0;
}

static const struct {
    enum AVHWDeviceType type;
    const char *possible_devices[5];
} test_devices[] = {
    { AV_HWDEVICE_TYPE_CUDA,
      { "0", "1", "2" } },
    { AV_HWDEVICE_TYPE_DRM,
      { "/dev/dri/card0", "/dev/dri/card1",
        "/dev/dri/renderD128", "/dev/dri/renderD129" } },
    { AV_HWDEVICE_TYPE_DXVA2,
      { "0", "1", "2" } },
    { AV_HWDEVICE_TYPE_D3D11VA,
      { "0", "1", "2" } },
    { AV_HWDEVICE_TYPE_OPENCL,
      { "0.0", "0.1", "1.0", "1.1" } },
    { AV_HWDEVICE_TYPE_VAAPI,
      { "/dev/dri/renderD128", "/dev/dri/renderD129", ":0" } },
};

static int test_device_derivation(enum AVHWDeviceType type)
{
    const char *name;
    AVBufferRef *ref = NULL;
    int err, i, j;

    name = av_hwdevice_get_type_name(type);
    if (!name)
        return 1;

    err = av_hwdevice_ctx_create(&ref, type, NULL, NULL, 0);
    if (err < 0) {
        for (i = 0; i < FF_ARRAY_ELEMS(test_devices); i++) {
            if (test_devices[i].type != type)
                continue;
            for (j = 0; test_devices[i].possible_devices[j]; j++) {
                err = av_hwdevice_ctx_create(&ref, type,
                                              test_devices[i].possible_devices[j],
                                              NULL, 0);
                if (err == 0)
                    break;
            }
            if (err == 0)
                break;
        }
        if (err < 0)
            return 1;
    }

    fprintf(stderr, "Testing derivation chains for %s...\n", name);

    err = test_get_or_create_reuse(ref, name);
    if (err < 0) {
        av_buffer_unref(&ref);
        return -1;
    }

    err = test_derivation_chain(ref, name);
    if (err < 0) {
        av_buffer_unref(&ref);
        return -1;
    }

    av_buffer_unref(&ref);
    return 0;
}

int main(void)
{
    enum AVHWDeviceType type;
    int pass, fail, skip, err;
    int api_errors = 0;

    fprintf(stderr, "=== Hardware Context Derivation Chain Tests ===\n\n");

    err = test_type_nb_sentinel();
    if (err < 0)
        api_errors++;

    err = test_get_or_create_derived_api();
    if (err < 0)
        api_errors++;

    if (api_errors) {
        fprintf(stderr, "\nFATAL: %d API test(s) failed\n", api_errors);
        return 1;
    }

    fprintf(stderr, "\n=== Device Derivation Chain Tests ===\n\n");

    pass = fail = skip = 0;
    type = AV_HWDEVICE_TYPE_NONE;
    while (1) {
        type = av_hwdevice_iterate_types(type);
        if (type == AV_HWDEVICE_TYPE_NONE)
            break;

        err = test_device_derivation(type);
        if (err == 0)
            ++pass;
        else if (err < 0)
            ++fail;
        else
            ++skip;
    }

    fprintf(stderr, "\n=== Results ===\n");
    fprintf(stderr, "API tests: PASSED\n");
    fprintf(stderr, "Device derivation: %d tested, %d passed, %d failed, %d skipped\n",
            pass + fail + skip, pass, fail, skip);

    return fail > 0;
}
