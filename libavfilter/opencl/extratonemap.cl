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

#define REFERENCE_WHITE 100.0f
extern float3 lrgb2yuv(float3);
extern float  lrgb2y(float3);
extern float3 yuv2lrgb(float3);
extern float3 lrgb2lrgb(float3);
extern float  get_luma_src(float3);
extern float  get_luma_dst(float3);
extern float4 get_luma_dst_4(float4 r_4, float4 g_4, float4 b_4);
extern float4 get_luma_src_4(float4 r_4, float4 g_4, float4 b_4);
extern float3 ootf(float3 c, float peak);
extern float3 inverse_ootf(float3 c, float peak);
extern float3 get_chroma_sample(float3, float3, float3, float3);

float hable_f(float in) {
    const float a = 0.15f, b = 0.50f, c = 0.10f, d = 0.20f, e = 0.02f, f = 0.30f;
    return native_divide((mad(in, (mad(in, a, b * c)), d * e)), (in * (mad(in, a, b)) + d * f)) - native_divide(e, f);
}

float direct(float s, float peak) {
    return s;
}

float linear(float s, float peak) {
    return native_divide(s * tone_param, peak);
}

float gamma(float s, float peak) {
    float p = s > 0.05f ? native_divide(s, peak) : native_divide(0.05f, peak);
    float v = native_powr(p, native_divide(1.0f, tone_param));
    return s > 0.05f ? v : native_divide(s * v, 0.05f);
}

float clip(float s, float peak) {
    return clamp(s * tone_param, 0.0f, 1.0f);
}

float reinhard(float s, float peak) {
    return native_divide(native_divide(s, (s + tone_param)) * (peak + tone_param), peak);
}

float hable(float s, float peak) {
    return native_divide(hable_f(s), hable_f(peak));
}

float mobius(float s, float peak) {
    float j = tone_param;
    float a, b;

    if (s <= j)
        return s;

    a = native_divide(-j * j * (peak - 1.0f), (j * j - 2.0f * j + peak));
    b = native_divide((j * j - 2.0f * j * peak + peak), fmax(peak - 1.0f, 1e-6f));

    return native_divide(native_divide((b * b + 2.0f * b * j + j * j), (b - a)) * (s + a), (s + b));
}

void map_four_pixels_rgb(float4* r_4, float4* g_4, float4* b_4, float peak, float average) {

    float4 sig = fmax(fmax(*r_4, fmax(*g_4, *b_4)), 1e-6f);

    // Rescale the variables in order to bring it into a representation where
    // 1.0 represents the dst_peak. This is because all of the tone mapping
    // algorithms are defined in such a way that they map to the range [0.0, 1.0].
    if (target_peak > 1.0f) {
        sig *= native_recip(target_peak);
        peak *= native_recip(target_peak);
    }

    float4 sig_old = sig;

    // Desaturate the color using a coefficient dependent on the signal level
    if (desat_param > 0.0f) {
        float4 luma = get_luma_src_4(*r_4, *g_4, *b_4);
        float4 overbright = fmax(luma - desat_param, 1e-6f);
        overbright = native_divide(overbright , fmax(luma, 1e-6f));

        float4 lumaXoverbright = luma * overbright;
        overbright = -overbright + 1.0f;

        *r_4 = mad(*r_4, overbright, lumaXoverbright);
        *g_4 = mad(*g_4, overbright, lumaXoverbright);
        *b_4 = mad(*b_4, overbright, lumaXoverbright);
        sig = mad(sig, overbright, lumaXoverbright);
    }

    sig = (float4)(TONE_FUNC(sig.x, peak), TONE_FUNC(sig.y, peak), TONE_FUNC(sig.z, peak), TONE_FUNC(sig.w, peak));

    float4 factor = native_divide(sig, sig_old);

    *r_4 = *r_4 * factor;
    *g_4 = *g_4 * factor;
    *b_4 = *b_4 * factor;
}

// map from source space YUV to destination space RGB
float3 map_to_dst_space_from_yuv(float3 yuv, float peak) {
    float3 c = yuv2lrgb(yuv);
    c = ootf(c, peak);
    c = lrgb2lrgb(c);
    return c;
}

void process_four_pixels(float4 y, float2 uv, uchar4* y_out, uchar2* uv_out, float peak) {

    float3 c0 = map_to_dst_space_from_yuv((float3)(y.s0, uv.x, uv.y), peak);
    float3 c1 = map_to_dst_space_from_yuv((float3)(y.s2, uv.x, uv.y), peak);
    float3 c2 = map_to_dst_space_from_yuv((float3)(y.s1, uv.x, uv.y), peak);
    float3 c3 = map_to_dst_space_from_yuv((float3)(y.s3, uv.x, uv.y), peak);

    float4 r_4 = (float4)(c0.x, c1.x, c2.x, c3.x);
    float4 g_4 = (float4)(c0.y, c1.y, c2.y, c3.y);
    float4 b_4 = (float4)(c0.z, c1.z, c2.z, c3.z);

    map_four_pixels_rgb(&r_4, &g_4, &b_4, peak, 1.0f);

    c0 = (float3)(r_4.x, g_4.x, b_4.x);
    c1 = (float3)(r_4.y, g_4.y, b_4.y);
    c2 = (float3)(r_4.z, g_4.z, b_4.z);
    c3 = (float3)(r_4.w, g_4.w, b_4.w);

    c0 = inverse_ootf(c0, target_peak);
    c1 = inverse_ootf(c1, target_peak);
    c2 = inverse_ootf(c2, target_peak);
    c3 = inverse_ootf(c3, target_peak);

    float3 chroma_c = get_chroma_sample(c0, c1, c2, c3);
    float3 chroma = lrgb2yuv(chroma_c);

#if chroma_loc == 3
    y = (float4)(chroma.x, lrgb2y(c2), lrgb2y(c1), lrgb2y(c3));
#elif chroma_loc == 5
    y = (float4)(lrgb2y(c0), lrgb2y(c2), chroma.x, lrgb2y(c3));
#else
    y = (float4)(lrgb2y(c0), lrgb2y(c2), lrgb2y(c1), lrgb2y(c3));
#endif

    *y_out = convert_uchar4_sat_rte(y * 255.0f);

    uchar3 chroma_uc = convert_uchar3_sat_rte(chroma * 255.0f);
    *uv_out = (uchar2)(chroma_uc.y, chroma_uc.z);
}

__kernel __attribute__((reqd_work_group_size(32, 1, 1)))
void tonemap(__write_only image2d_t dst1,
                __read_only  image2d_t src1,
                __write_only image2d_t dst2,
                __read_only  image2d_t src2,
                float peak
                )
{
    int xi = get_global_id(0);
    int yi = get_global_id(1);
    int2 image_size = get_image_dim(dst2);

    if (xi >= image_size.x || yi * 4 >= image_size.y)
        return;

    // each work item processes 2xfour pixels
    int x = 2 * xi;
    int y = 8 * yi;

    int work_group_byte_offset = get_group_id(0) * get_enqueued_local_size(0);
    int sub_group_byte_offset = work_group_byte_offset + get_sub_group_id() * get_max_sub_group_size();

    ushort16 y_us1 = as_ushort16(intel_sub_group_block_read8(src1, (int2)(sub_group_byte_offset * 4,     y)));

    ushort8 uv_s1 = as_ushort8(intel_sub_group_block_read4(src2, (int2)(sub_group_byte_offset * 4,     yi * 4)));

    float16 y_f1 = convert_float16(y_us1 >> 6) / 1023.0f;
    float8 uv_f1 = convert_float8(uv_s1 >> 6) / 1023.0f;

    uchar4 y_out1, y_out2, y_out3, y_out4;
    uchar2 uv_out1, uv_out2, uv_out3, uv_out4;

    process_four_pixels(y_f1.lo.lo, uv_f1.lo.lo, &y_out1, &uv_out1, peak);
    process_four_pixels(y_f1.lo.hi, uv_f1.lo.hi, &y_out2, &uv_out2, peak);
    process_four_pixels(y_f1.hi.lo, uv_f1.hi.lo, &y_out3, &uv_out3, peak);
    process_four_pixels(y_f1.hi.hi, uv_f1.hi.hi, &y_out4, &uv_out4, peak);

    ushort c1 = as_ushort((uchar2)(y_out1.s0, y_out1.s1));
    ushort c2 = as_ushort((uchar2)(y_out1.s2, y_out1.s3));
    ushort c3 = as_ushort((uchar2)(y_out2.s0, y_out2.s1));
    ushort c4 = as_ushort((uchar2)(y_out2.s2, y_out2.s3));
    ushort c5 = as_ushort((uchar2)(y_out3.s0, y_out3.s1));
    ushort c6 = as_ushort((uchar2)(y_out3.s2, y_out3.s3));
    ushort c7 = as_ushort((uchar2)(y_out4.s0, y_out4.s1));
    ushort c8 = as_ushort((uchar2)(y_out4.s2, y_out4.s3));
    ushort8 c = (ushort8)(c1, c2, c3, c4, c5, c6, c7, c8);

    intel_sub_group_block_write_us8(dst1, (int2)(sub_group_byte_offset * 2, y), c);

    ushort d1 = as_ushort((uchar2)(uv_out1.s0, uv_out1.s1));
    ushort d2 = as_ushort((uchar2)(uv_out2.s0, uv_out2.s1));
    ushort d3 = as_ushort((uchar2)(uv_out3.s0, uv_out3.s1));
    ushort d4 = as_ushort((uchar2)(uv_out4.s0, uv_out4.s1));
    ushort4 d = (ushort4)(d1, d2, d3, d4);

    intel_sub_group_block_write_us4(dst2, (int2)(sub_group_byte_offset * 2, yi * 4), d);
}
