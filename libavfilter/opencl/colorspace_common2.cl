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

#define ST2084_MAX_LUMINANCE 10000.0f
#define REFERENCE_WHITE 100.0f

#if chroma_loc == 1
    #define chroma_sample(a,b,c,d) (((a) + (c)) * 0.5f)
#elif chroma_loc == 3
    #define chroma_sample(a,b,c,d) (a)
#elif chroma_loc == 4
    #define chroma_sample(a,b,c,d) (((a) + (b)) * 0.5f)
#elif chroma_loc == 5
    #define chroma_sample(a,b,c,d) (c)
#elif chroma_loc == 6
    #define chroma_sample(a,b,c,d) (((c) + (d)) * 0.5f)
#else
    #define chroma_sample(a,b,c,d) (((a) + (b) + (c) + (d)) * 0.25f)
#endif

__constant const float ST2084_M1 = 0.1593017578125f;
__constant const float ST2084_M2 = 78.84375f;
__constant const float ST2084_C1 = 0.8359375f;
__constant const float ST2084_C2 = 18.8515625f;
__constant const float ST2084_C3 = 18.6875f;

__constant const float ONE_BY_ST2084_M1 = 6.2773946360153f;
__constant const float ONE_BY_ST2084_M2 = 0.0126833135156f;
__constant const float ST2084_MAX_LUMINANCE_BY_REF_WHITE = 100.0f;
__constant const float HLG_A = 0.17883277f;
__constant const float HLG_B = 0.28466892f;
__constant const float HLG_C = 0.55991073f;
__constant const float3 yuv_offset = (float3)(0.0f, 0.5f, 0.5f);
__constant const float3 yuvc1 = (float3)(255.0f, 255.0f, 255.0f);
__constant const float3 yuvc2 = (float3)(16.0f, 128.0f, 128.0f);
__constant const float3 yuvc3 = (float3)(219.0f, 224.0f, 224.0f);


float get_luma_dst(float3 c) {
    return luma_dst.x * c.x + luma_dst.y * c.y + luma_dst.z * c.z;
}

float4 get_luma_dst_4(float4 r_4, float4 g_4, float4 b_4) {
    return r_4 * luma_dst.x + g_4 * luma_dst.y + b_4 * luma_dst.z;
}

float get_luma_src(float3 c) {
    return luma_src.x * c.x + luma_src.y * c.y + luma_src.z * c.z;
}

float4 get_luma_src_4(float4 r_4, float4 g_4, float4 b_4) {
    return r_4 * luma_src.x + g_4 * luma_src.y + b_4 * luma_src.z;
}

float3 get_chroma_sample(float3 a, float3 b, float3 c, float3 d) {
    return chroma_sample(a, b, c, d);
}

float3 eotf_st2084(float3 x) {

    uint3 rgb_i = convert_uint3_rte((x) * 1023.f);
    rgb_i = clamp(rgb_i, 0, 1023);

    return (float3)(lin_lut[rgb_i.x], lin_lut[rgb_i.y], lin_lut[rgb_i.z]);
}

// linearizer for HLG
float3 inverse_oetf_hlg(float3 x) {
    float3 a = x * x * 4.0f;
    float3 b = native_exp(native_divide(x - HLG_C, HLG_A)) + HLG_B;
    return x < 0.5f ? a : b;
}

// delinearizer for HLG
float3 oetf_hlg(float3 x) {
    float3 a = native_sqrt(x) * 0.5f;
    float3 b = native_log(x - HLG_B) * HLG_A + HLG_C;
    return x <= 1.0f ? a : b;
}

float3 ootf_hlg(float3 c, float peak) {
    float luma = get_luma_src(c);
    float gamma =  1.2f + 0.42f * native_log10(peak * REFERENCE_WHITE / 1000.0f);
    gamma = fmax(1.0f, gamma);
    float factor = peak * native_powr(luma, gamma - 1.0f) / native_powr(12.0f, gamma);
    return c * factor;
}

float3 inverse_ootf_hlg(float3 c, float peak) {
    float gamma = 1.2f + 0.42f * native_log10(peak * REFERENCE_WHITE / 1000.0f);
    c *=  native_powr(12.0f, gamma) / peak;
    c /= native_powr(get_luma_dst(c), (gamma - 1.0f) / gamma);
    return c;
}

float3 inverse_eotf_bt1886(float3 c) {
    return c < 0.0f ? 0.0f : native_powr(c, 0.4166666666666f);
}

float3 oetf_bt709(float3 c) {
    c = c < 0.0f ? 0.0f : c;
    float3 r1 = c * 4.5f;
    float3 r2 = native_powr(c, 0.45f) * 1.099f - 0.099f;
    return c < 0.018f ? r1 : r2;
}

float3 inverse_oetf_bt709(float3 c) {
    float3 r1 = native_divide(c, 4.5f);
    float3 r2 = native_powr(native_divide((c + 0.099f), 1.099f), native_divide(1.0f, 0.45f));
    return c < 0.081f ? r1 : r2;
}

float3 yuv2rgb(float3 yuv) {
#ifdef FULL_RANGE_IN
    yuv -= yuv_offset;
#else
    yuv = native_divide((yuv * yuvc1 - yuvc2), yuvc3);
#endif

    float3 rgb = rgb_matrix[0] * yuv.x +  rgb_matrix[1] * yuv.y +  rgb_matrix[2] * yuv.z;
    return rgb;
}

float3 yuv2lrgb(float3 yuv) {
    float3 rgb = yuv2rgb(yuv);
#ifdef linearize
    rgb = eotf_st2084(rgb);
    return rgb;
#else
    return rgb;
#endif
}

float3 rgb2yuv(float3 rgb) {
    float3 yuv = yuv_matrix[0] * rgb.x + yuv_matrix[1] * rgb.y + yuv_matrix[2] * rgb.z;
#ifdef FULL_RANGE_OUT
    yuv += yuv_offset;
#else
    yuv = native_divide(mad(yuvc3, yuv, yuvc2) , yuvc1);
#endif
    return yuv;
}

float rgb2y(float3 rgb) {
    float y = rgb.x * yuv_matrix[0].x + rgb.y * yuv_matrix[1].x + rgb.z * yuv_matrix[2].x;
    y = native_divide((219.0f * y + 16.0f), 255.0f);
    return y;
}

float3 lrgb2yuv(float3 c) {
#ifdef delinearize
    float3 rgb = inverse_eotf_bt1886(c);
    return rgb2yuv(rgb);
#else
    return rgb2yuv(c);
#endif
}

float lrgb2y(float3 c) {
#ifdef delinearize
    float3 rgb = inverse_eotf_bt1886(c);
    return rgb2y(rgb);
#else
    return rgb2y(c);
#endif
}

float3 lrgb2lrgb(float3 c) {
#ifdef RGB2RGB_PASSTHROUGH
    return c;
#else
    float3 rgb = rgb2rgb[0] * c.x + rgb2rgb[1] * c.y + rgb2rgb[2] * c.z;
    return rgb;
#endif
}

float3 ootf(float3 c, float peak) {
#ifdef ootf_impl
    return ootf_impl(c, peak);
#else
    return c;
#endif
}

float3 inverse_ootf(float3 c, float peak) {
#ifdef inverse_ootf_impl
    return inverse_ootf_impl(c, peak);
#else
    return c;
#endif
}
