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

float4 get_luma_src_4(float4 r_4, float4 g_4, float4 b_4) {
    return r_4 * luma_src.x + g_4 * luma_src.y + b_4 * luma_src.z;
}

float4 get_luma_dst_4(float4 r_4, float4 g_4, float4 b_4) {
    return r_4 * luma_dst.x + g_4 * luma_dst.y + b_4 * luma_dst.z;
}

float3 lbgr2lrgb(float3 bgr) {
#ifdef RGB2RGB_PASSTHROUGH
    return (float3)(bgr.s2, bgr.s1, bgr.s0);
#else
    float3 rgb = rgb2rgb[0] * bgr.z + rgb2rgb[1] * bgr.y + rgb2rgb[2] * bgr.x;
    return rgb;
#endif
}

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
        float4 coeff = native_divide(fmax(sig - 0.18f, 1e-6f), fmax(sig, 1e-6f));
        coeff = native_powr(coeff, native_divide(10.0f, desat_param));
        *r_4 = mix(*r_4, luma, coeff);
        *g_4 = mix(*g_4, luma, coeff);
        *b_4 = mix(*b_4, luma, coeff);
        sig = mix(sig, luma, coeff);
    }

    sig = (float4)(TONE_FUNC(sig.x, peak), TONE_FUNC(sig.y, peak), TONE_FUNC(sig.z, peak), TONE_FUNC(sig.w, peak));

    sig = fmin(sig, 1.0f);

    float4 factor = native_divide(sig, sig_old);

    *r_4 = *r_4 * factor;
    *g_4 = *g_4 * factor;
    *b_4 = *b_4 * factor;
}

float3 eotf_st2084_uchar(uchar4 rgb_i) {

    return (float3)(lin_lut[rgb_i.x], lin_lut[rgb_i.y], lin_lut[rgb_i.z]);
}

float4 inverse_eotf_bt1886(float4 c) {

    return c < 0.0f ? 0.0f : native_powr(c, 0.4166666666666f);
}

float3 map_to_dst_space_from_yuv(uint px_i, float peak) {

    uchar4 bgr_i = as_uchar4(px_i);
    float3 bgr = eotf_st2084_uchar(bgr_i);
    float3 rgb = lbgr2lrgb(bgr);
    return rgb;
}

void process_four_pixels(uint4* px_i, float peak) {

    float3 c0 = map_to_dst_space_from_yuv((*px_i).s0, peak);
    float3 c1 = map_to_dst_space_from_yuv((*px_i).s1, peak);
    float3 c2 = map_to_dst_space_from_yuv((*px_i).s2, peak);
    float3 c3 = map_to_dst_space_from_yuv((*px_i).s3, peak);

    float4 r_4 = (float4)(c0.x, c1.x, c2.x, c3.x);
    float4 g_4 = (float4)(c0.y, c1.y, c2.y, c3.y);
    float4 b_4 = (float4)(c0.z, c1.z, c2.z, c3.z);

    map_four_pixels_rgb(&r_4, &g_4, &b_4, peak, 1.0f);

    r_4 = inverse_eotf_bt1886(r_4);
    g_4 = inverse_eotf_bt1886(g_4);
    b_4 = inverse_eotf_bt1886(b_4);

    uchar4 r = convert_uchar4_sat_rte(r_4 * 255.0f);
    uchar4 g = convert_uchar4_sat_rte(g_4 * 255.0f);
    uchar4 b = convert_uchar4_sat_rte(b_4 * 255.0f);

    uchar16 bgr16 = (uchar16)(b.s0, g.s0, r.s0, 0,
                              b.s1, g.s1, r.s1, 0,
                              b.s2, g.s2, r.s2, 0,
                              b.s3, g.s3, r.s3, 0);

    *px_i = as_uint4(bgr16);
}

__kernel
void tonemap(__write_only image2d_t dst1,
             __read_only  image2d_t src1,
             float peak
            )
{
    int xi = get_global_id(0);
    int yi = get_global_id(1);
    int2 image_size = get_image_dim(dst1);

    if (xi >= image_size.x || yi * 8 >= image_size.y)
        return;

    int x = 2 * xi;
    int y = 8 * yi;

    int work_group_byte_offset = get_group_id(0) * get_enqueued_local_size(0);
    int sub_group_byte_offset = work_group_byte_offset + get_sub_group_id() * get_max_sub_group_size();

    uint8 px = intel_sub_group_block_read8(src1, (int2)(sub_group_byte_offset * 4, y));

    uint4 px4_0 = px.lo;
    uint4 px4_1 = px.hi;

    process_four_pixels(&px4_0, peak);
    process_four_pixels(&px4_1, peak);

    px.lo = px4_0;
    px.hi = px4_1;

    intel_sub_group_block_write_ui8(dst1, (int2)(sub_group_byte_offset * 4, y), px);
}
