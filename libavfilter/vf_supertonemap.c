/*
 * Copyright (c) 2017 Vittorio Giovara <vittorio.giovara@gmail.com>
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
  * tonemap algorithms
  */

#include <float.h>

#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "colorspace.h"
#include "filters.h"
#include "formats.h"
#include "supertonemap.h"
#include "video.h"
#include "libavutil/avassert.h"

#if ARCH_X86_64
#include <immintrin.h>
#endif

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_GBRPF32,
    AV_PIX_FMT_GBRAPF32,
    AV_PIX_FMT_P010,
    AV_PIX_FMT_NONE,
};

typedef struct ThreadData {
    AVFrame *in, *out;
    const AVPixFmtDescriptor *desc, *odesc;
    float peak;
} ThreadData;

static int query_formats(AVFilterContext *ctx) {
    AVFilterFormats *formats = ff_make_format_list(pix_fmts);
    int res = ff_formats_ref(formats, &ctx->inputs[0]->outcfg.formats);
    if (res < 0)
        return res;
    formats = NULL;
    res = ff_add_format(&formats, AV_PIX_FMT_NV12);
    if (res < 0)
        return res;
    return ff_formats_ref(formats, &ctx->outputs[0]->incfg.formats);
}

static av_cold int init(AVFilterContext *ctx) {
    TonemapContext *s = ctx->priv;

    switch (s->tonemap) {
    case TONEMAP_GAMMA:
        if (isnan(s->param))
            s->param = 1.8f;
        break;
    case TONEMAP_REINHARD:
        if (!isnan(s->param))
            s->param = (1.0f - s->param) / s->param;
        break;
    case TONEMAP_MOBIUS:
        if (isnan(s->param))
            s->param = 0.3f;
        break;
    }

    if (isnan(s->param))
        s->param = 1.0f;

    s->tonemap_frame_p010_nv12 = ff_tonemap_frame_p010_nv12_c;

    return 0;
}

static float hable(float in) {
    const float a = 0.15f, b = 0.50f, c = 0.10f, d = 0.20f, e = 0.02f, f = 0.30f;
    return (in * (in * a + b * c) + d * e) / (in * (in * a + b) + d * f) - e / f;
}

static float mobius(float in, float j, float peak) {
    float a, b;

    if (in <= j)
        return in;

    a = -j * j * (peak - 1.0f) / (j * j - 2.0f * j + peak);
    b = (j * j - 2.0f * j * peak + peak) / FFMAX(peak - 1.0f, 1e-6);

    return (b * b + 2.0f * b * j + j * j) / (b - a) * (in + a) / (in + b);
}

static float eotf_st2084(float x) {
#define ST2084_MAX_LUMINANCE 10000.0f
#define REFERENCE_WHITE 100.0f
#define ST2084_M1 0.1593017578125f
#define ST2084_M2 78.84375f
#define ST2084_C1 0.8359375f
#define ST2084_C2 18.8515625f
#define ST2084_C3 18.6875f
    const float p = powf(x, 1.0f / ST2084_M2);
    const float a = FFMAX(p - ST2084_C1, 0.0f);
    const float b = FFMAX(ST2084_C2 - ST2084_C3 * p, 1e-6f);
    const float c = powf(a / b, 1.0f / ST2084_M1);
    return x > 0.0f ? c * ST2084_MAX_LUMINANCE / REFERENCE_WHITE : 0.0f;
}

static float inverse_eotf_st2084(float x) {
    x *= REFERENCE_WHITE / ST2084_MAX_LUMINANCE;
    x = powf(x, ST2084_M1);
    x = (ST2084_C1 + ST2084_C2 * x) / (1.0f + ST2084_C3 * x);
    return powf(x, ST2084_M2);
}

static float bt2390(float sig_orig, float peak) {
    float sig_pq = sig_orig / peak;
    const float maxLum = 0.751829f / peak; // SDR peak in PQ
    const float ks = 1.5f * maxLum - 0.5f;
    const float tb = (sig_pq - ks) / (1.0f - ks);
    const float tb2 = tb * tb;
    const float tb3 = tb2 * tb;
    float pb = (2.0f * tb3 - 3.0f * tb2 + 1.0f) * ks +
        (tb3 - 2.0f * tb2 + tb) * (1.0f - ks) +
        (-2.0f * tb3 + 3.0f * tb2) * maxLum;
    const float sig = (sig_pq < ks) ? sig_pq : pb;
    return eotf_st2084(sig * peak);
}

static float mapsig(const enum TonemapAlgorithm alg, float sig, float peak, float param) {
    switch (alg) {
    default:
    case TONEMAP_NONE:
        // do nothing
        break;
    case TONEMAP_LINEAR:
        sig = sig * param / peak;
        break;
    case TONEMAP_GAMMA:
        sig = sig > 0.05f ? powf(sig / peak, 1.0f / param)
            : sig * powf(0.05f / peak, 1.0f / param) / 0.05f;
        break;
    case TONEMAP_CLIP:
        sig = av_clipf(sig * param, 0, 1.0f);
        break;
    case TONEMAP_HABLE:
        sig = hable(sig) / hable(peak);
        break;
    case TONEMAP_REINHARD:
        sig = sig / (sig + param) * (peak + param) / peak;
        break;
    case TONEMAP_MOBIUS:
        sig = mobius(sig, param, peak);
        break;
    case TONEMAP_BT2390:
        sig = bt2390(sig, peak);
        break;
    }

    return sig;
}

#define MIX(x,y,a) (x) * (1 - (a)) + (y) * (a)
static void tonemap(float r_in, float g_in, float b_in,
    float *r_out, float *g_out, float *b_out,
    TonemapContext *s, float peak) {
    float sig, sig_orig;

    /* load values */
    *r_out = r_in;
    *g_out = g_in;
    *b_out = b_in;

    /* desaturate to prevent unnatural colors */
    if (s->desat > 0) {
        const float luma = s->coeffs->cr * r_in + s->coeffs->cg * g_in + s->coeffs->cb * b_in;
        const float overbright = FFMAX(luma - s->desat, 1e-6f) / FFMAX(luma, 1e-6f);
        *r_out = MIX(r_in, luma, overbright);
        *g_out = MIX(g_in, luma, overbright);
        *b_out = MIX(b_in, luma, overbright);
    }

    /* pick the brightest component, reducing the value range as necessary
     * to keep the entire signal in range and preventing discoloration due to
     * out-of-bounds clipping */
    sig = FFMAX(FFMAX3(*r_out, *g_out, *b_out), 1e-6f);
    sig_orig = sig;

    sig = mapsig(s->tonemap, sig, peak, s->param);

    /* apply the computed scale factor to the color,
     * linearly to prevent discoloration */
    *r_out *= sig / sig_orig;
    *g_out *= sig / sig_orig;
    *b_out *= sig / sig_orig;
}

#if ARCH_X86_64

#define clip_cond_uint8_c(a) (((a) & ~0xFF) ? (~(a)>>31) : (a))

static inline __m128i m_packus_epi32_SSE2( __m128i a, __m128i b )
{
    const __m128i val_32 = _mm_set1_epi32( 0x8000 );
    const __m128i val_16 = _mm_set1_epi16( 0x8000 );

    a = _mm_sub_epi32( a, val_32 );
    b = _mm_sub_epi32( b, val_32 );
    a = _mm_packs_epi32( a, b );
    a = _mm_add_epi16( a, val_16 );
    return a;
}

static inline __m128i muly(const __m128i *a, const __m128i *b)
{
#ifdef __SSE4_1__
    return _mm_mullo_epi32(*a, *b);
#else
    const __m128i tmp1 = _mm_mul_epu32(*a,*b); /* mul 2,0*/
    const __m128i tmp2 = _mm_mul_epu32( _mm_srli_si128(*a,4), _mm_srli_si128(*b,4)); /* mul 3,1 */
    return _mm_unpacklo_epi32(_mm_shuffle_epi32(tmp1, _MM_SHUFFLE (0,0,2,0)), _mm_shuffle_epi32(tmp2, _MM_SHUFFLE (0,0,2,0))); /* shuffle results to [63..0] and pack */
#endif
}

void ff_tonemap_frame_p010_nv12_c(uint8_t *dsty, uint8_t *dstuv, const uint16_t *srcy, const uint16_t *srcuv, const int *dstlinesize, const int *srclinesize, int width, int height, const struct TonemapIntParams *params) {
    const int in_depth = 10;
    const int in_uv_offset = 128 << (in_depth - 8);
    const int in_sh = in_depth - 1;
    const int in_rnd = 1 << (in_sh - 1);
    const int in_sh2 = 16 - in_depth;
    const int out_depth = 8;
    const int out_uv_offset = 128 << (out_depth - 8);
    const int out_sh = 29 - out_depth;
    const int out_rnd = 1 << (out_sh - 1);
    __m128i out_rnd4 = _mm_set1_epi32( out_rnd );
    __m128i out_yuv_off4 = _mm_set1_epi32( params->out_yuv_off );

    __m128i cy4 = _mm_set1_epi32( (*params->yuv2rgb_coeffs)[0][0][0] );
    const int crv = (*params->yuv2rgb_coeffs)[0][2][0];
    const int cgu = (*params->yuv2rgb_coeffs)[1][1][0];
    const int cgv = (*params->yuv2rgb_coeffs)[1][2][0];
    const int cbu = (*params->yuv2rgb_coeffs)[2][1][0];

    const int cru = (*params->rgb2yuv_coeffs)[1][0][0];
    const int ocgu = (*params->rgb2yuv_coeffs)[1][1][0];
    const int cburv = (*params->rgb2yuv_coeffs)[1][2][0];
    const int crvu = (*params->rgb2yuv_coeffs)[2][0][0];
    const int ocgv = (*params->rgb2yuv_coeffs)[2][1][0];
    const int cbv = (*params->rgb2yuv_coeffs)[2][2][0];

    __m128 cr4 = _mm_set1_ps( params->coeffs ? params->coeffs->cr : 0 );
    __m128 cg4 = _mm_set1_ps( params->coeffs ? params->coeffs->cg : 0 );
    __m128 cb4 = _mm_set1_ps( params->coeffs ? params->coeffs->cb : 0 );
    __m128 minfloat4 = _mm_set1_ps( 1e-6f );
    __m128 desat4 = _mm_set1_ps( params->desat );
    __m128 one4 = _mm_set1_ps( 1.0f );

    __m128 rgb2rgb4[3][3];

    for ( int n=0 ; n < 3 ; n++ )
    {
        for ( int m=0 ; m < 3 ; m++ )
        {
            const float coeff = (*params->rgb2rgb_coeffs)[n][m];
            rgb2rgb4[n][m] = _mm_load_ps1( &coeff );
        }
    }

    __m128i crgby_r = _mm_set1_epi32( (*params->rgb2yuv_coeffs)[0][0][0] );
    __m128i crgby_g = _mm_set1_epi32( (*params->rgb2yuv_coeffs)[0][1][0] );
    __m128i crgby_b = _mm_set1_epi32( (*params->rgb2yuv_coeffs)[0][2][0] );

    const int dstlinesize0 = dstlinesize[0];
    const int dstyStep = dstlinesize[0] * 2;
    const int srclinesize0Half = srclinesize[0] / 2;
    int x;

    __m128i zero = _mm_set1_epi32( 0x00000000 );
    __m128i in_yuv_off = _mm_set1_epi32( params->in_yuv_off );

    for (; height > 1; height -= 2,
        dsty += dstyStep,
        dstuv += dstlinesize[1],
        srcy += srclinesize[0], srcuv += srclinesize[1] / 2) {

        const uint16_t *srcy0 = srcy;
        const uint16_t *srcy1 = srcy + srclinesize0Half;

        for (x = 0; x < width; x += 2) {

            __m128i y0 = _mm_loadl_epi64((__m128i const*)srcy0); srcy0++; srcy0++;
            __m128i y1 = _mm_loadl_epi64((__m128i const*)srcy1); srcy1++; srcy1++;

            y0 = _mm_unpacklo_epi16(y0, zero);
            y1 = _mm_unpacklo_epi16(y1, zero);

            __m128i y_x_cy = _mm_unpacklo_epi64(y0, y1);

            y_x_cy = _mm_sub_epi32(_mm_srli_epi32(y_x_cy, in_sh2), in_yuv_off);
            y_x_cy = muly(&y_x_cy, &cy4);

            const int u = (srcuv[x] >> in_sh2) - in_uv_offset;
            const int v = (srcuv[x + 1] >> in_sh2) - in_uv_offset;
            const int crv_x_V = crv * v;
            const int cgu_x_u = cgu * u;
            const int cgv_x_v = cgv * v;
            const int cbu_x_u = cbu * u;

            __m128i val1 = _mm_set1_epi32( crv_x_V + in_rnd );
            __m128i val2 = _mm_set1_epi32( cgu_x_u + cgv_x_v + in_rnd );
            __m128i val3 = _mm_set1_epi32( cbu_x_u + in_rnd );

            __m128i y_x_cy_r = _mm_add_epi32(y_x_cy, val1);
            y_x_cy_r = _mm_srli_epi32(y_x_cy_r, in_sh);
            y_x_cy_r = m_packus_epi32_SSE2(y_x_cy_r, zero);

            __m128i y_x_cy_g = _mm_add_epi32(y_x_cy, val2);
            y_x_cy_g = _mm_srli_epi32(y_x_cy_g, in_sh);
            y_x_cy_g = m_packus_epi32_SSE2(y_x_cy_g, zero);

            __m128i y_x_cy_b = _mm_add_epi32(y_x_cy, val3);
            y_x_cy_b = _mm_srli_epi32(y_x_cy_b, in_sh);
            y_x_cy_b = m_packus_epi32_SSE2(y_x_cy_b, zero);

            // *****************************************************************************

            float* tlut = params->tonemap_lut;
            float* llut = params->lin_lut;
            uint16_t* dlut = params->delin_lut;
            const __m128i c_2048 = _mm_set1_epi16( 2048 );
            const __m128 c_32767 = _mm_set1_ps( 32767.0f );
            const __m128i c_32767_8 = _mm_set1_epi16( 32767 );
            const __m128 c_05 = _mm_set1_ps( 0.5f );

            __m128i sig4 = _mm_max_epi16(y_x_cy_r, _mm_max_epi16(y_x_cy_g, y_x_cy_b));
            sig4 = _mm_max_epi16(_mm_adds_epu16(sig4, c_2048), zero);

            int16_t* sig4_p = (int16_t*)&sig4;
            __m128 mapval4 = _mm_set_ps(*(tlut + sig4_p[3]), *(tlut + sig4_p[2]), *(tlut + sig4_p[1]), *(tlut + sig4_p[0]));

            y_x_cy_r = _mm_max_epi16(_mm_adds_epu16(y_x_cy_r, c_2048), zero);
            y_x_cy_g = _mm_max_epi16(_mm_adds_epu16(y_x_cy_g, c_2048), zero);
            y_x_cy_b = _mm_max_epi16(_mm_adds_epu16(y_x_cy_b, c_2048), zero);

            int16_t* y_x_cy_r_p = (int16_t*)&y_x_cy_r;
            int16_t* y_x_cy_g_p = (int16_t*)&y_x_cy_g;
            int16_t* y_x_cy_b_p = (int16_t*)&y_x_cy_b;

            __m128 rgb_lin_r = _mm_set_ps(*(llut + y_x_cy_r_p[3]), *(llut + y_x_cy_r_p[2]), *(llut + y_x_cy_r_p[1]), *(llut + y_x_cy_r_p[0]));
            __m128 rgb_lin_g = _mm_set_ps(*(llut + y_x_cy_g_p[3]), *(llut + y_x_cy_g_p[2]), *(llut + y_x_cy_g_p[1]), *(llut + y_x_cy_g_p[0]));
            __m128 rgb_lin_b = _mm_set_ps(*(llut + y_x_cy_b_p[3]), *(llut + y_x_cy_b_p[2]), *(llut + y_x_cy_b_p[1]), *(llut + y_x_cy_b_p[0]));

            /* desaturate to prevent unnatural colors */
            if (params->desat > 0) {
                __m128 luma4 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(rgb_lin_r, cr4), _mm_mul_ps(rgb_lin_g, cg4)), _mm_mul_ps(rgb_lin_b, cb4));
                __m128 overbright4 = _mm_max_ps(_mm_sub_ps(luma4, desat4), minfloat4 );
                overbright4 = _mm_div_ps(overbright4, _mm_max_ps(luma4, minfloat4));

                __m128 lumaXoverbright4 = _mm_mul_ps(luma4, overbright4 );

                overbright4 = _mm_sub_ps ( one4, overbright4 );

                rgb_lin_r = _mm_add_ps( _mm_mul_ps ( rgb_lin_r, overbright4), lumaXoverbright4);
                rgb_lin_g = _mm_add_ps( _mm_mul_ps ( rgb_lin_g, overbright4), lumaXoverbright4);
                rgb_lin_b = _mm_add_ps( _mm_mul_ps ( rgb_lin_b, overbright4), lumaXoverbright4);
            }

            rgb_lin_r = _mm_mul_ps(rgb_lin_r, mapval4);
            rgb_lin_g = _mm_mul_ps(rgb_lin_g, mapval4);
            rgb_lin_b = _mm_mul_ps(rgb_lin_b, mapval4);

            __m128 rgb_lin_r2 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(rgb2rgb4[0][0], rgb_lin_r), _mm_mul_ps(rgb2rgb4[0][1], rgb_lin_g)), _mm_mul_ps(rgb2rgb4[0][2], rgb_lin_b));
            __m128 rgb_lin_g2 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(rgb2rgb4[1][0], rgb_lin_r), _mm_mul_ps(rgb2rgb4[1][1], rgb_lin_g)), _mm_mul_ps(rgb2rgb4[1][2], rgb_lin_b));
            __m128 rgb_lin_b2 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(rgb2rgb4[2][0], rgb_lin_r), _mm_mul_ps(rgb2rgb4[2][1], rgb_lin_g)), _mm_mul_ps(rgb2rgb4[2][2], rgb_lin_b));

            rgb_lin_r = _mm_add_ps(_mm_mul_ps(rgb_lin_r2, c_32767), c_05);
            rgb_lin_g = _mm_add_ps(_mm_mul_ps(rgb_lin_g2, c_32767), c_05);
            rgb_lin_b = _mm_add_ps(_mm_mul_ps(rgb_lin_b2, c_32767), c_05);

            __m128i rgb_lin_r_i = _mm_min_epi16(_mm_max_epi16(_mm_packs_epi32(_mm_cvtps_epi32(rgb_lin_r), zero), zero), c_32767_8);
            __m128i rgb_lin_g_i = _mm_min_epi16(_mm_max_epi16(_mm_packs_epi32(_mm_cvtps_epi32(rgb_lin_g), zero), zero), c_32767_8);
            __m128i rgb_lin_b_i = _mm_min_epi16(_mm_max_epi16(_mm_packs_epi32(_mm_cvtps_epi32(rgb_lin_b), zero), zero), c_32767_8);

            int16_t* rgb_lin_r_i_p = (int16_t*)&rgb_lin_r_i;
            int16_t* rgb_lin_g_i_p = (int16_t*)&rgb_lin_g_i;
            int16_t* rgb_lin_b_i_p = (int16_t*)&rgb_lin_b_i;

            y_x_cy_r = _mm_set_epi16(0, 0, 0, 0, *(dlut + rgb_lin_r_i_p[3]), *(dlut + rgb_lin_r_i_p[2]), *(dlut + rgb_lin_r_i_p[1]), *(dlut + rgb_lin_r_i_p[0]));
            y_x_cy_g = _mm_set_epi16(0, 0, 0, 0, *(dlut + rgb_lin_g_i_p[3]), *(dlut + rgb_lin_g_i_p[2]), *(dlut + rgb_lin_g_i_p[1]), *(dlut + rgb_lin_g_i_p[0]));
            y_x_cy_b = _mm_set_epi16(0, 0, 0, 0, *(dlut + rgb_lin_b_i_p[3]), *(dlut + rgb_lin_b_i_p[2]), *(dlut + rgb_lin_b_i_p[1]), *(dlut + rgb_lin_b_i_p[0]));

            // *****************************************************************************

            __m128i sign = _mm_srai_epi16 (y_x_cy_r, 15);
            __m128i y_x_32_r = _mm_unpacklo_epi16 ( y_x_cy_r, sign );

            sign = _mm_srai_epi16 (y_x_cy_g, 15);
            __m128i y_x_32_g = _mm_unpacklo_epi16 ( y_x_cy_g, sign );

            sign = _mm_srai_epi16 (y_x_cy_b, 15);
            __m128i y_x_32_b = _mm_unpacklo_epi16 ( y_x_cy_b, sign );

            y_x_32_r = muly(&y_x_32_r, &crgby_r);
            y_x_32_g = muly(&y_x_32_g, &crgby_g);
            y_x_32_b = muly(&y_x_32_b, &crgby_b);

            __m128i temp = _mm_add_epi32 (_mm_add_epi32 (_mm_add_epi32 (y_x_32_r, y_x_32_g ), y_x_32_b), out_rnd4);
            temp = _mm_srai_epi32(temp, out_sh);
            temp = _mm_add_epi32(temp, out_yuv_off4);
            temp = _mm_packus_epi16(_mm_packs_epi32(temp, zero), zero);

            uint8_t* temp_p = (uint8_t*)&temp;
            dsty[x] = temp_p[0];
            dsty[x + 1] = temp_p[1];
            dsty[x + dstlinesize0] = temp_p[2];
            dsty[x + dstlinesize0 + 1] = temp_p[3];

            int16_t* temp_r_p = (int16_t*)&y_x_cy_r;
            int16_t* temp_g_p = (int16_t*)&y_x_cy_g;
            int16_t* temp_b_p = (int16_t*)&y_x_cy_b;

            int avg_r = (temp_r_p[0] + temp_r_p[1] + temp_r_p[2] + temp_r_p[3]) >> 2;
            int avg_g = (temp_g_p[0] + temp_g_p[1] + temp_g_p[2] + temp_g_p[3]) >> 2;
            int avg_b = (temp_b_p[0] + temp_b_p[1] + temp_b_p[2] + temp_b_p[3]) >> 2;

            dstuv[x] = clip_cond_uint8_c(out_uv_offset +
                ((avg_r * cru +
                    avg_g * ocgu +
                    avg_b * cburv + out_rnd) >> out_sh));

            dstuv[x + 1] = clip_cond_uint8_c(out_uv_offset +
                ((avg_r * crvu +
                    avg_g * ocgv +
                    avg_b * cbv + out_rnd) >> out_sh));
        }
    }
}


#else

#define clip_cond_uintp2_15(a) (((a) & 0xFFFF8000) ? (~(a) >> 31 & 0x7FFF) : (a))
#define clip_cond_uint16(a) ((((a) + 0x8000U) & ~0xFFFF) ? ((a) >> 31) ^ 0x7FFF : (a))
#define clip_cond_uint8_c(a) (((a) & ~0xFF) ? (~(a)>>31) : (a))

static void tonemap_int16(int16_t(*__restrict rgb)[12], const struct TonemapIntParams *params, float(*__restrict rgb2rgb)[3][3]) {
    /* pick the brightest component, reducing the value range as necessary
     * to keep the entire signal in range and preventing discoloration due to
     * out-of-bounds clipping */
    float rgb_lin[12];
    float mapval[12];
    int index;
    int i;
    int t;
    float* tdst;
    const float* tsrc;
    int16_t* i16dst;

    #pragma GCC ivdep
    for (index=0; index < 12; index += 3)
    {
        const int16_t sig = FFMAX3((*rgb)[index + 0], (*rgb)[index + 1], (*rgb)[index + 2]);
        mapval[index] = mapval[index + 1] = mapval[index + 2] = params->tonemap_lut[clip_cond_uintp2_15(sig + 2048)];
    }

    #pragma GCC ivdep
    for (t = 0; t < 12; t++) {
        rgb_lin[t] = params->lin_lut[clip_cond_uintp2_15((*rgb)[t] + 2048)];
    }

    /* desaturate to prevent unnatural colors */
    if (params->desat > 0) {
        float overbright[12];
        float lumaXoverbright[12];

        for (index = 0; index < 12; index += 3) {
            const float luma = params->coeffs->cr * rgb_lin[index + 0] + params->coeffs->cg * rgb_lin[index + 1] + params->coeffs->cb * rgb_lin[index + 2];
            overbright[index] = overbright[index + 1] = overbright[index + 2] = FFMAX(luma - params->desat, 1e-6f) / FFMAX(luma, 1e-6f);
            lumaXoverbright[index] = lumaXoverbright[index + 1] = lumaXoverbright[index + 2] = luma * overbright[index];
        }

        for (t = 0; t < 12; t++) {
            rgb_lin[t] = rgb_lin[t] * (1.0f - overbright[t]) + lumaXoverbright[t];
        }
    }

    #pragma GCC ivdep
    for (t = 0; t < 12; t++) {
        rgb_lin[t] = rgb_lin[t] * mapval[t];
    }

    for (i=0; i < 3; i++)
    {
        for (index=0; index < 12; index += 3)
        {
            rgb_lin[index + i] = (*rgb2rgb)[i][0] * rgb_lin[index + 0] + (*rgb2rgb)[i][1] * rgb_lin[index + 1] + (*rgb2rgb)[i][2] * rgb_lin[index + 2];
        }
    }

    i16dst = &(*rgb)[0];
    tdst = &rgb_lin[0];

    #pragma GCC ivdep
    for (t = 0; t < 12; t++) {
        int tmp_val = (int)(*tdst++ * 32767.0f + 0.5f);
        tmp_val = clip_cond_uintp2_15(tmp_val);
        *i16dst++ = params->delin_lut[tmp_val];
    }
}

void ff_tonemap_frame_p010_nv12_c(uint8_t *dsty, uint8_t *dstuv, const uint16_t *srcy, const uint16_t *srcuv, const int *dstlinesize, const int *srclinesize, int width, int height, const struct TonemapIntParams *params) {
    const int in_depth = 10;
    const int in_uv_offset = 128 << (in_depth - 8);
    const int in_sh = in_depth - 1;
    const int in_rnd = 1 << (in_sh - 1);
    const int in_sh2 = 16 - in_depth;
    const int out_depth = 8;
    const int out_uv_offset = 128 << (out_depth - 8);
    const int out_sh = 29 - out_depth;
    const int out_rnd = 1 << (out_sh - 1);
    const int cy = (*params->yuv2rgb_coeffs)[0][0][0];
    const int crv = (*params->yuv2rgb_coeffs)[0][2][0];
    const int cgu = (*params->yuv2rgb_coeffs)[1][1][0];
    const int cgv = (*params->yuv2rgb_coeffs)[1][2][0];
    const int cbu = (*params->yuv2rgb_coeffs)[2][1][0];

    const int cru = (*params->rgb2yuv_coeffs)[1][0][0];
    const int ocgu = (*params->rgb2yuv_coeffs)[1][1][0];
    const int cburv = (*params->rgb2yuv_coeffs)[1][2][0];
    const int ocgv = (*params->rgb2yuv_coeffs)[2][1][0];
    const int cbv = (*params->rgb2yuv_coeffs)[2][2][0];

    const int crgby[12] = {
        (*params->rgb2yuv_coeffs)[0][0][0],
        (*params->rgb2yuv_coeffs)[0][1][0],
        (*params->rgb2yuv_coeffs)[0][2][0],
        (*params->rgb2yuv_coeffs)[0][0][0],
        (*params->rgb2yuv_coeffs)[0][1][0],
        (*params->rgb2yuv_coeffs)[0][2][0],
        (*params->rgb2yuv_coeffs)[0][0][0],
        (*params->rgb2yuv_coeffs)[0][1][0],
        (*params->rgb2yuv_coeffs)[0][2][0],
        (*params->rgb2yuv_coeffs)[0][0][0],
        (*params->rgb2yuv_coeffs)[0][1][0],
        (*params->rgb2yuv_coeffs)[0][2][0],
    };

    int16_t rgb[12];
    int rgb_int[12];
    int tmp_avg[3];
    const int dstlinesize0 = dstlinesize[0];
    const int dstyStep = dstlinesize[0] * 2;
    int x, t;
    int16_t* p_rgb;
    int* p_rgb_int;
    const int* p_crgby;

    for (; height > 1; height -= 2,
        dsty += dstyStep,
        dstuv += dstlinesize[1],
        srcy += srclinesize[0], srcuv += srclinesize[1] / 2) {

        for (x = 0; x < width; x += 2) {

            const int y00_x_cy = ((srcy[x] >> in_sh2) - params->in_yuv_off) * cy;
            const int y01_x_cy = ((srcy[x + 1] >> in_sh2) - params->in_yuv_off) * cy;
            const int y10_x_cy = ((srcy[srclinesize[0] / 2 + x] >> in_sh2) - params->in_yuv_off) * cy;
            const int y11_x_cy = ((srcy[srclinesize[0] / 2 + x + 1] >> in_sh2) - params->in_yuv_off) * cy;
            const int u = (srcuv[x] >> in_sh2) - in_uv_offset;
            const int v = (srcuv[x + 1] >> in_sh2) - in_uv_offset;
            const int crv_x_V = crv * v;
            const int cgu_x_u = cgu * u;
            const int cgv_x_v = cgv * v;
            const int cbu_x_u = cbu * u;

            const int val1 = crv_x_V + in_rnd;
            const int val2 = cgu_x_u + cgv_x_v + in_rnd;
            const int val3 = cbu_x_u + in_rnd;

            rgb[0] = clip_cond_uint16((y00_x_cy + val1) >> in_sh);
            rgb[3] = clip_cond_uint16((y01_x_cy + val1) >> in_sh);
            rgb[6] = clip_cond_uint16((y10_x_cy + val1) >> in_sh);
            rgb[9] = clip_cond_uint16((y11_x_cy + val1) >> in_sh);

            rgb[1] =  clip_cond_uint16((y00_x_cy + val2) >> in_sh);
            rgb[4] =  clip_cond_uint16((y01_x_cy + val2) >> in_sh);
            rgb[7] =  clip_cond_uint16((y10_x_cy + val2) >> in_sh);
            rgb[10] = clip_cond_uint16((y11_x_cy + val2) >> in_sh);

            rgb[2] =  clip_cond_uint16((y00_x_cy + val3) >> in_sh);
            rgb[5] =  clip_cond_uint16((y01_x_cy + val3) >> in_sh);
            rgb[8] =  clip_cond_uint16((y10_x_cy + val3) >> in_sh);
            rgb[11] = clip_cond_uint16((y11_x_cy + val3) >> in_sh);

            tonemap_int16(&rgb, params, params->rgb2rgb_coeffs);

            p_rgb = &rgb[0];
            p_rgb_int = &rgb_int[0];
            p_crgby = &crgby[0];

            #pragma GCC ivdep
            for (t = 0; t < 12; t++) {
                *p_rgb_int++ = *p_rgb++ * *p_crgby++;
            }

            dsty[x] = clip_cond_uint8_c(params->out_yuv_off +
                ((rgb_int[0] + rgb_int[1] + rgb_int[2] + out_rnd) >> out_sh));

            dsty[x + 1] = clip_cond_uint8_c(params->out_yuv_off +
                ((rgb_int[3] + rgb_int[4] + rgb_int[5] + out_rnd) >> out_sh));

            dsty[x + dstlinesize0] = clip_cond_uint8_c(params->out_yuv_off +
                ((rgb_int[6] + rgb_int[7] + rgb_int[8] + out_rnd) >> out_sh));

            dsty[x + dstlinesize0 + 1] = clip_cond_uint8_c(params->out_yuv_off +
                ((rgb_int[9] + rgb_int[10] + rgb_int[11] + out_rnd) >> out_sh));

            for (int i = 0; i < 3; i++) {
                tmp_avg[i] = (rgb[i] + rgb[3 + i] + rgb[6 + i] + rgb[9 + i] + 2) >> 2;
            }

            dstuv[x] = clip_cond_uint8_c(out_uv_offset +
                ((tmp_avg[0] * cru +
                    tmp_avg[1] * ocgu +
                    tmp_avg[2] * cburv + out_rnd) >> out_sh));

            dstuv[x + 1] = clip_cond_uint8_c(out_uv_offset +
                ((tmp_avg[0] * cburv +
                    tmp_avg[1] * ocgv +
                    tmp_avg[2] * cbv + out_rnd) >> out_sh));
        }
    }
}

#endif

static float inverse_eotf_bt1886(float c) {
    return c < 0.0f ? 0.0f : powf(c, 1.0f / 2.4f);
}

static int comput_trc_luts(TonemapContext *s, enum AVColorTransferCharacteristic in,
    enum AVColorTransferCharacteristic out) {
    int i;

    if (!s->lin_lut && !(s->lin_lut = av_calloc(32768, sizeof(float))))
        return AVERROR(ENOMEM);
    if (!s->delin_lut && !(s->delin_lut = av_calloc(32768, sizeof(uint16_t))))
        return AVERROR(ENOMEM);

    for (i = 0; i < 32768; i++) {
        const float v1 = (i - 2048.0) / 28672.0;
        const float v2 = i / 32767.0;
        s->lin_lut[i] = FFMAX(eotf_st2084(v1), 0);
        s->delin_lut[i] = av_clip_int16(lrint(inverse_eotf_bt1886(v2) * 28672.0f));
    }

    return 0;
}

static int compute_tonemap_lut(TonemapContext *s) {
    int i;
    float peak = (float)s->lut_peak;

    if (!s->tonemap_lut && !(s->tonemap_lut = av_calloc(32768, sizeof(float))))
        return AVERROR(ENOMEM);

    if (s->tonemap == TONEMAP_BT2390)
        peak = inverse_eotf_st2084(peak);

    for (i = 0; i < 32768; i++) {
        float v = (i - 2048.0) / 28672.0;
        float lin = FFMAX(eotf_st2084(v), 0);
        const float mapval = s->tonemap == TONEMAP_BT2390 ? v : lin;
        const float mapped = mapsig(s->tonemap, mapval, peak, (float)s->param);
        s->tonemap_lut[i] = (lin > 0 && mapped > 0) ? mapped / lin : 0;
    }

    return 0;
}

static int get_range_off(int *off, int *y_rng, int *uv_rng,
                     enum AVColorRange rng, int depth)
{
    switch (rng) {
    case AVCOL_RANGE_UNSPECIFIED:
    case AVCOL_RANGE_MPEG:
        *off = 16 << (depth - 8);
        *y_rng = 219 << (depth - 8);
        *uv_rng = 224 << (depth - 8);
        break;
    case AVCOL_RANGE_JPEG:
        *off = 0;
        *y_rng = *uv_rng = (256 << (depth - 8)) - 1;
        break;
    default:
        return AVERROR(EINVAL);
    }

    return 0;
}

static void get_yuv_coeffs(int16_t out[3][3][8], double (*table)[3],
                       int depth, int y_rng, int uv_rng, int yuv2rgb)
{
#define N (yuv2rgb ? m : n)
#define M (yuv2rgb ? n : m)
    int rng, n, m, o;
    int bits = 1 << (yuv2rgb ? (depth - 1) : (29 - depth));
    for (rng = y_rng, n = 0; n < 3; n++, rng = uv_rng) {
        for (m = 0; m < 3; m++) {
            out[N][M][0] = lrint(bits * (yuv2rgb ? 28672 : rng) * table[N][M] / (yuv2rgb ? rng : 28672));
            for (o = 1; o < 8; o++)
                out[N][M][o] = out[N][M][0];
        }
    }

    if (yuv2rgb) {
        av_assert2(out[0][1][0] == 0);
        av_assert2(out[2][2][0] == 0);
        av_assert2(out[0][0][0] == out[1][0][0]);
        av_assert2(out[0][0][0] == out[2][0][0]);
    } else {
        av_assert2(out[1][2][0] == out[2][0][0]);
    }
}

static const double ycgco_matrix[3][3] =
{
    {  0.25, 0.5,  0.25 },
    { -0.25, 0.5, -0.25 },
    {  0.5,  0,   -0.5  },
};

static const double gbr_matrix[3][3] =
{
    { 0,    1,   0   },
    { 0,   -0.5, 0.5 },
    { 0.5, -0.5, 0   },
};

static void fill_rgb2yuv_table(const struct LumaCoefficients *coeffs,
                           double rgb2yuv[3][3])
{
    double bscale, rscale;

    // special ycgco matrix
    if (coeffs->cr == 0.25 && coeffs->cg == 0.5 && coeffs->cb == 0.25) {
        memcpy(rgb2yuv, ycgco_matrix, sizeof(double) * 9);
        return;
    } else if (coeffs->cr == 1 && coeffs->cg == 1 && coeffs->cb == 1) {
        memcpy(rgb2yuv, gbr_matrix, sizeof(double) * 9);
        return;
    }

    rgb2yuv[0][0] = coeffs->cr;
    rgb2yuv[0][1] = coeffs->cg;
    rgb2yuv[0][2] = coeffs->cb;
    bscale = 0.5 / (coeffs->cb - 1.0);
    rscale = 0.5 / (coeffs->cr - 1.0);
    rgb2yuv[1][0] = bscale * coeffs->cr;
    rgb2yuv[1][1] = bscale * coeffs->cg;
    rgb2yuv[1][2] = 0.5;
    rgb2yuv[2][0] = 0.5;
    rgb2yuv[2][1] = rscale * coeffs->cg;
    rgb2yuv[2][2] = rscale * coeffs->cb;
}

static void matrix_invert_3x3(const double in[3][3], double out[3][3])
{
    double m00 = in[0][0], m01 = in[0][1], m02 = in[0][2],
           m10 = in[1][0], m11 = in[1][1], m12 = in[1][2],
           m20 = in[2][0], m21 = in[2][1], m22 = in[2][2];
    int i, j;
    double det;

    out[0][0] =  (m11 * m22 - m21 * m12);
    out[0][1] = -(m01 * m22 - m21 * m02);
    out[0][2] =  (m01 * m12 - m11 * m02);
    out[1][0] = -(m10 * m22 - m20 * m12);
    out[1][1] =  (m00 * m22 - m20 * m02);
    out[1][2] = -(m00 * m12 - m10 * m02);
    out[2][0] =  (m10 * m21 - m20 * m11);
    out[2][1] = -(m00 * m21 - m20 * m01);
    out[2][2] =  (m00 * m11 - m10 * m01);

    det = m00 * out[0][0] + m10 * out[0][1] + m20 * out[0][2];
    det = 1.0 / det;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++)
            out[i][j] *= det;
    }
}

static int compute_yuv_coeffs(TonemapContext *s,
    const struct LumaCoefficients *coeffs,
    const struct LumaCoefficients *ocoeffs,
    const AVPixFmtDescriptor *idesc,
    const AVPixFmtDescriptor *odesc,
    enum AVColorRange irng,
    enum AVColorRange orng) {
    double rgb2yuv[3][3], yuv2rgb[3][3];
    int res;
    int y_rng, uv_rng;

    res = get_range_off(&s->in_yuv_off, &y_rng, &uv_rng,
        irng, idesc->comp[0].depth);
    if (res < 0) {
        av_log(s, AV_LOG_ERROR,
            "Unsupported input color range %d (%s)\n",
            irng, av_color_range_name(irng));
        return res;
    }

    fill_rgb2yuv_table(coeffs, rgb2yuv);
    matrix_invert_3x3(rgb2yuv, yuv2rgb);
    fill_rgb2yuv_table(ocoeffs, rgb2yuv);

    get_yuv_coeffs(s->yuv2rgb_coeffs, yuv2rgb, idesc->comp[0].depth,
        y_rng, uv_rng, 1);

    res = get_range_off(&s->out_yuv_off, &y_rng, &uv_rng,
        orng, odesc->comp[0].depth);
    if (res < 0) {
        av_log(s, AV_LOG_ERROR,
            "Unsupported output color range %d (%s)\n",
            orng, av_color_range_name(orng));
        return res;
    }

    get_yuv_coeffs(s->rgb2yuv_coeffs, rgb2yuv, odesc->comp[0].depth,
        y_rng, uv_rng, 0);

    return 0;
}

static const struct PrimaryCoefficients color_primaries[AVCOL_PRI_NB] = {
    [AVCOL_PRI_BT709]     = { 0.640, 0.330, 0.300, 0.600, 0.150, 0.060 },
    [AVCOL_PRI_BT470M]    = { 0.670, 0.330, 0.210, 0.710, 0.140, 0.080 },
    [AVCOL_PRI_BT470BG]   = { 0.640, 0.330, 0.290, 0.600, 0.150, 0.060 },
    [AVCOL_PRI_SMPTE170M] = { 0.630, 0.340, 0.310, 0.595, 0.155, 0.070 },
    [AVCOL_PRI_SMPTE240M] = { 0.630, 0.340, 0.310, 0.595, 0.155, 0.070 },
    [AVCOL_PRI_SMPTE428]  = { 0.735, 0.265, 0.274, 0.718, 0.167, 0.009 },
    [AVCOL_PRI_SMPTE431]  = { 0.680, 0.320, 0.265, 0.690, 0.150, 0.060 },
    [AVCOL_PRI_SMPTE432]  = { 0.680, 0.320, 0.265, 0.690, 0.150, 0.060 },
    [AVCOL_PRI_FILM]      = { 0.681, 0.319, 0.243, 0.692, 0.145, 0.049 },
    [AVCOL_PRI_BT2020]    = { 0.708, 0.292, 0.170, 0.797, 0.131, 0.046 },
    [AVCOL_PRI_JEDEC_P22] = { 0.630, 0.340, 0.295, 0.605, 0.155, 0.077 },
};

static const struct PrimaryCoefficients *get_color_primaries(enum AVColorPrimaries prm)
{
    const struct PrimaryCoefficients *p;

    if (prm >= AVCOL_PRI_NB)
        return NULL;
    p = &color_primaries[prm];
    if (!p->xr)
        return NULL;

    return p;
}

static void matrix_mul_3x3float(float dst[3][3],
               const double src1[3][3], const double src2[3][3])
{
    int m, n;

    for (m = 0; m < 3; m++)
        for (n = 0; n < 3; n++)
            dst[m][n] = src2[m][0] * src1[0][n] +
                        src2[m][1] * src1[1][n] +
                        src2[m][2] * src1[2][n];
}

static void fill_rgb2xyz_table(const struct PrimaryCoefficients *coeffs,
                           const struct WhitepointCoefficients *wp,
                           double rgb2xyz[3][3])
{
    double i[3][3], sr, sg, sb, zw;

    rgb2xyz[0][0] = coeffs->xr / coeffs->yr;
    rgb2xyz[0][1] = coeffs->xg / coeffs->yg;
    rgb2xyz[0][2] = coeffs->xb / coeffs->yb;
    rgb2xyz[1][0] = rgb2xyz[1][1] = rgb2xyz[1][2] = 1.0;
    rgb2xyz[2][0] = (1.0 - coeffs->xr - coeffs->yr) / coeffs->yr;
    rgb2xyz[2][1] = (1.0 - coeffs->xg - coeffs->yg) / coeffs->yg;
    rgb2xyz[2][2] = (1.0 - coeffs->xb - coeffs->yb) / coeffs->yb;
    matrix_invert_3x3(rgb2xyz, i);
    zw = 1.0 - wp->xw - wp->yw;
    sr = i[0][0] * wp->xw + i[0][1] * wp->yw + i[0][2] * zw;
    sg = i[1][0] * wp->xw + i[1][1] * wp->yw + i[1][2] * zw;
    sb = i[2][0] * wp->xw + i[2][1] * wp->yw + i[2][2] * zw;
    rgb2xyz[0][0] *= sr;
    rgb2xyz[0][1] *= sg;
    rgb2xyz[0][2] *= sb;
    rgb2xyz[1][0] *= sr;
    rgb2xyz[1][1] *= sg;
    rgb2xyz[1][2] *= sb;
    rgb2xyz[2][0] *= sr;
    rgb2xyz[2][1] *= sg;
    rgb2xyz[2][2] *= sb;
}

static int compute_rgb_coeffs(TonemapContext *s,
    enum AVColorPrimaries iprm,
    enum AVColorPrimaries oprm) {
    double rgb2xyz[3][3], xyz2rgb[3][3];
    const struct WhitepointCoefficients wp = { 0.3127, 0.3290 };
    const struct PrimaryCoefficients *icoeff = get_color_primaries(iprm);
    const struct PrimaryCoefficients *ocoeff = get_color_primaries(oprm);

    if (!icoeff) {
        av_log(s, AV_LOG_ERROR,
            "Unsupported input color primaries %d (%s)\n",
            iprm, av_color_primaries_name(iprm));
        return AVERROR(EINVAL);
    }
    if (!ocoeff) {
        av_log(s, AV_LOG_ERROR,
            "Unsupported output color primaries %d (%s)\n",
            oprm, av_color_primaries_name(oprm));
        return AVERROR(EINVAL);
    }

    fill_rgb2xyz_table(ocoeff, &wp, rgb2xyz);
    matrix_invert_3x3(rgb2xyz, xyz2rgb);
    fill_rgb2xyz_table(icoeff, &wp, rgb2xyz);
    matrix_mul_3x3float(s->rgb2rgb_coeffs, rgb2xyz, xyz2rgb);

    return 0;
}

static int filter_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs) {
    TonemapContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const AVPixFmtDescriptor *desc = td->desc;
    const AVPixFmtDescriptor *odesc = td->odesc;
    const int ss = 1 << FFMAX(desc->log2_chroma_h, odesc->log2_chroma_h);
    const int slice_start = (in->height / ss * jobnr) / nb_jobs * ss;
    const int slice_end = (in->height / ss * (jobnr + 1)) / nb_jobs * ss;
    int y, x;

    if (desc->flags & AV_PIX_FMT_FLAG_FLOAT) {
        /* do the tone map */
#define COMP(frame, c) ((float*)(frame->data[c] + x * desc->comp[c].step + y * frame->linesize[c]))
        for (y = slice_start; y < slice_end; y++) {
            for (x = 0; x < out->width; x++) {
                tonemap(*COMP(in, 0), *COMP(in, 1), *COMP(in, 2),
                    COMP(out, 0), COMP(out, 1), COMP(out, 2),
                    s, td->peak);
            }
        }
    }
    else {
        TonemapIntParams params = {
            .lut_peak = s->lut_peak,
            .lin_lut = s->lin_lut,
            .tonemap_lut = s->tonemap_lut,
            .delin_lut = s->delin_lut,
            .in_yuv_off = s->in_yuv_off,
            .out_yuv_off = s->out_yuv_off,
            .yuv2rgb_coeffs = &s->yuv2rgb_coeffs,
            .rgb2yuv_coeffs = &s->rgb2yuv_coeffs,
            .rgb2rgb_coeffs = &s->rgb2rgb_coeffs,
            .coeffs = s->coeffs,
            .ocoeffs = s->ocoeffs,
            .desat = s->desat,
        };
        s->tonemap_frame_p010_nv12(out->data[0] + out->linesize[0] * slice_start,
            out->data[1] + out->linesize[1] * AV_CEIL_RSHIFT(slice_start, desc->log2_chroma_h),
            (void*)(in->data[0] + in->linesize[0] * slice_start),
            (void*)(in->data[1] + in->linesize[1] * AV_CEIL_RSHIFT(slice_start, odesc->log2_chroma_h)),
            out->linesize, in->linesize, out->width,
            slice_end - slice_start,
            &params);
    }

    /* copy/generate alpha if needed */
    if (desc->flags & AV_PIX_FMT_FLAG_ALPHA && odesc->flags & AV_PIX_FMT_FLAG_ALPHA) {
        av_image_copy_plane(out->data[3] + out->linesize[3] * slice_start, out->linesize[3],
            in->data[3] + in->linesize[3] * slice_start, in->linesize[3],
            out->linesize[3], slice_end - slice_start);
    }
    else if (odesc->flags & AV_PIX_FMT_FLAG_ALPHA) {
        for (y = slice_start; y < slice_end; y++) {
            for (x = 0; x < out->width; x++) {
                AV_WN32(out->data[3] + x * odesc->comp[3].step + y * out->linesize[3],
                    av_float2int(1.0f));
            }
        }
    }

    return 0;
}

static const struct LumaCoefficients luma_coefficients[AVCOL_SPC_NB] = {
    [AVCOL_SPC_FCC]        = { 0.30,   0.59,   0.11   },
    [AVCOL_SPC_BT470BG]    = { 0.299,  0.587,  0.114  },
    [AVCOL_SPC_SMPTE170M]  = { 0.299,  0.587,  0.114  },
    [AVCOL_SPC_BT709]      = { 0.2126, 0.7152, 0.0722 },
    [AVCOL_SPC_SMPTE240M]  = { 0.212,  0.701,  0.087  },
    [AVCOL_SPC_YCOCG]      = { 0.25,   0.5,    0.25   },
    [AVCOL_SPC_RGB]        = { 1,      1,      1      },
    [AVCOL_SPC_BT2020_NCL] = { 0.2627, 0.6780, 0.0593 },
    [AVCOL_SPC_BT2020_CL]  = { 0.2627, 0.6780, 0.0593 },
};

static const struct LumaCoefficients *get_luma_coefficients(enum AVColorSpace csp)
{
    const struct LumaCoefficients *coeffs;

    if (csp >= AVCOL_SPC_NB)
        return NULL;
    coeffs = &luma_coefficients[csp];
    if (!coeffs->cr)
        return NULL;

    return coeffs;
}

static int filter_frame(AVFilterLink *link, AVFrame *in) {
    AVFilterContext *ctx = link->dst;
    TonemapContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(link->format);
    const AVPixFmtDescriptor *odesc = av_pix_fmt_desc_get(outlink->format);
    int ret;
    double peak = s->peak;
    const struct LumaCoefficients *coeffs = get_luma_coefficients(in->colorspace);
    ThreadData td;

    if (!desc || !odesc) {
        av_frame_free(&in);
        return AVERROR_BUG;
    }

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }

    if ((ret = av_frame_copy_props(out, in)) < 0)
        goto fail;

    /* read peak from side data if not passed in */
    if (peak < 1e-6) {
        peak = ff_determine_signal_peak(in);
    }

    /* input and output transfer will be linear */
    if (desc->flags & AV_PIX_FMT_FLAG_FLOAT) {
        if (in->color_trc == AVCOL_TRC_UNSPECIFIED) {
            av_log(s, AV_LOG_WARNING, "Untagged transfer, assuming linear light\n");
            out->color_trc = AVCOL_TRC_LINEAR;
        }
        else if (in->color_trc != AVCOL_TRC_LINEAR)
            av_log(s, AV_LOG_WARNING, "Tonemapping works on linear light only\n");
    }
    else {
        if (in->color_trc == AVCOL_TRC_UNSPECIFIED) {
            av_log(s, AV_LOG_WARNING, "Untagged transfer, assuming PQ\n");
            out->color_trc = AVCOL_TRC_SMPTEST2084;
        }
        else if (in->color_trc != AVCOL_TRC_SMPTEST2084)
            av_log(s, AV_LOG_WARNING, "Tonemapping works on PQ only\n");

        out->color_trc = AVCOL_TRC_BT709;
        out->colorspace = AVCOL_SPC_BT709;
        out->color_primaries = AVCOL_PRI_BT709;

        if (!s->lin_lut || !s->delin_lut) {
            if ((ret = comput_trc_luts(s, in->color_trc, out->color_trc)) < 0)
                goto fail;
        }

        if (!s->tonemap_lut || s->lut_peak != peak) {
            s->lut_peak = peak;
            if ((ret = compute_tonemap_lut(s)) < 0)
                goto fail;
        }

        if (s->coeffs != coeffs) {
            enum AVColorPrimaries iprm = in->color_primaries;
            s->ocoeffs = get_luma_coefficients(out->colorspace);
            if ((ret = compute_yuv_coeffs(s, coeffs, s->ocoeffs, desc, odesc,
                in->color_range, out->color_range)) < 0)
                goto fail;
            if (iprm == AVCOL_PRI_UNSPECIFIED)
                iprm = AVCOL_PRI_BT2020;
            if ((ret = compute_rgb_coeffs(s, iprm, out->color_primaries)) < 0)
                goto fail;
        }
    }

    /* load original color space even if pixel format is RGB to compute overbrights */
    s->coeffs = coeffs;
    if (s->desat > 0 && !s->coeffs) {
        if (in->colorspace == AVCOL_SPC_UNSPECIFIED)
            av_log(s, AV_LOG_WARNING, "Missing color space information, ");
        else if (!s->coeffs)
            av_log(s, AV_LOG_WARNING, "Unsupported color space '%s', ",
                av_color_space_name(in->colorspace));
        av_log(s, AV_LOG_WARNING, "desaturation is disabled\n");
        s->desat = 0;
    }

    td.in = in;
    td.out = out;
    td.desc = desc;
    td.odesc = odesc;
    td.peak = (float)peak;
    ff_filter_execute(ctx, filter_slice, &td, NULL, FFMIN(outlink->h >> FFMAX(desc->log2_chroma_h, odesc->log2_chroma_h), ctx->graph->nb_threads));

    av_frame_free(&in);

    ff_update_hdr_metadata(out, peak);

    return ff_filter_frame(outlink, out);
fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return ret;
}

static void uninit(AVFilterContext *ctx) {
    TonemapContext *s = ctx->priv;

    av_freep(&s->lin_lut);
    av_freep(&s->delin_lut);
    av_freep(&s->tonemap_lut);
}

#define OFFSET(x) offsetof(TonemapContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM
static const AVOption tonemap_options[] = {
    { "tonemap",      "tonemap algorithm selection", OFFSET(tonemap), AV_OPT_TYPE_INT, {.i64 = TONEMAP_BT2390}, TONEMAP_NONE, TONEMAP_MAX - 1, FLAGS, .unit = "tonemap" },
    {     "none",     0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_NONE},              0, 0, FLAGS, .unit = "tonemap" },
    {     "linear",   0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_LINEAR},            0, 0, FLAGS, .unit = "tonemap" },
    {     "gamma",    0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_GAMMA},             0, 0, FLAGS, .unit = "tonemap" },
    {     "clip",     0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_CLIP},              0, 0, FLAGS, .unit = "tonemap" },
    {     "reinhard", 0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_REINHARD},          0, 0, FLAGS, .unit = "tonemap" },
    {     "hable",    0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_HABLE},             0, 0, FLAGS, .unit = "tonemap" },
    {     "mobius",   0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_MOBIUS},            0, 0, FLAGS, .unit = "tonemap" },
    {     "bt2390",   0, 0, AV_OPT_TYPE_CONST, {.i64 = TONEMAP_BT2390},            0, 0, FLAGS, .unit = "tonemap" },
    { "param",        "tonemap parameter", OFFSET(param), AV_OPT_TYPE_DOUBLE, {.dbl = NAN}, DBL_MIN, DBL_MAX, FLAGS },
    { "desat",        "desaturation strength", OFFSET(desat), AV_OPT_TYPE_DOUBLE, {.dbl = 2.0}, 0, DBL_MAX, FLAGS },
    { "peak",         "signal peak override", OFFSET(peak), AV_OPT_TYPE_DOUBLE, {.dbl = 0}, 0, DBL_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(tonemap);

static const AVFilterPad tonemap_inputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

const FFFilter ff_vf_supertonemap = {
    .p.name        = "supertonemap",
    .p.description = NULL_IF_CONFIG_SMALL("Fast conversion to/from different dynamic ranges."),
    .p.priv_class  = &tonemap_class,
    .p.flags       = AVFILTER_FLAG_SLICE_THREADS,
    .init          = init,
    .uninit        = uninit,
    .priv_size     = sizeof(TonemapContext),
    FILTER_INPUTS(tonemap_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_QUERY_FUNC(query_formats),
};
