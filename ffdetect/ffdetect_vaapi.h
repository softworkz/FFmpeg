/*
 * Copyright (C) 2018 - softworkz for Emby Llc.
 * All rights reserved.
 *
 * This code is not yet published under any license.
 *
 */

#ifndef FFDETECT_FFDETECT_VAAPI_H
#define FFDETECT_FFDETECT_VAAPI_H

#include <stdint.h>

#include "config.h"
#include "libavutil/avutil.h"

#include "libavcodec/avcodec.h"
#include "libavutil/bprint.h"
#include "outputwriters.h"

#include <va/va.h>

typedef struct
{
    int deviceIndex;
    int vendor_id;
    int device_id;
    int vendor_id_sub;
    int device_id_sub;
    int enable;
    int boot_vga;
    char* device_path;
    char* vendor_name;
    char* device_name;
    char* vendor_name_sub;
    char* device_name_sub;
    char* drm_card;
    char* drm_render;
} VaAdapterInfo;

static const struct {
    enum AVCodecID codec_id;
    int codec_profile;
    const char* codec_name;
    const char* profile_name;
    VAProfile va_profile;
} vaapi_profile_map[] = {
#define MAP(c, p, v) { AV_CODEC_ID_ ## c, AV_PROFILE_ ## p, #c, #p, VAProfile ## v }
    MAP(MPEG2VIDEO,  MPEG2_SIMPLE,    MPEG2Simple),
    MAP(MPEG2VIDEO,  MPEG2_MAIN,      MPEG2Main),
    MAP(H263,        UNKNOWN,         H263Baseline),
    MAP(MPEG4,       MPEG4_SIMPLE,    MPEG4Simple),
    MAP(MPEG4,       MPEG4_ADVANCED_SIMPLE,
                               MPEG4AdvancedSimple),
    MAP(MPEG4,       MPEG4_MAIN,      MPEG4Main),
    MAP(H264,        H264_CONSTRAINED_BASELINE,
                           H264ConstrainedBaseline),
    MAP(H264,        H264_MAIN,       H264Main),
    MAP(H264,        H264_HIGH,       H264High),
    MAP(HEVC,        HEVC_MAIN,       HEVCMain),
    MAP(HEVC,        HEVC_MAIN_10,    HEVCMain10),
    MAP(HEVC,        HEVC_REXT,       HEVCMain12),
    MAP(HEVC,        HEVC_REXT,       HEVCMain422_10),
    MAP(HEVC,        HEVC_REXT,       HEVCMain422_12),
    MAP(HEVC,        HEVC_REXT,       HEVCMain444),
    MAP(HEVC,        HEVC_REXT,       HEVCMain444_10),
    MAP(HEVC,        HEVC_REXT,       HEVCMain444_12),
    MAP(MJPEG,       MJPEG_HUFFMAN_BASELINE_DCT,
                                      JPEGBaseline),
    MAP(WMV3,        VC1_SIMPLE,      VC1Simple),
    MAP(WMV3,        VC1_MAIN,        VC1Main),
    MAP(WMV3,        VC1_COMPLEX,     VC1Advanced),
    MAP(WMV3,        VC1_ADVANCED,    VC1Advanced),
    MAP(VC1,         VC1_SIMPLE,      VC1Simple),
    MAP(VC1,         VC1_MAIN,        VC1Main),
    MAP(VC1,         VC1_COMPLEX,     VC1Advanced),
    MAP(VC1,         VC1_ADVANCED,    VC1Advanced),
    MAP(VP8,         UNKNOWN,       VP8Version0_3),
    MAP(VP9,         VP9_0,           VP9Profile0),
    MAP(VP9,         VP9_1,           VP9Profile1),
    MAP(VP9,         VP9_2,           VP9Profile2),
    MAP(VP9,         VP9_3,           VP9Profile3),
    MAP(AV1,         AV1_MAIN,        AV1Profile0),
    MAP(AV1,         AV1_HIGH,        AV1Profile1),
#undef MAP
};


// The map fourcc <-> pix_fmt isn't bijective because of the annoying U/V
// plane swap cases.  The frame handling below tries to hide these.
static const struct {
    unsigned int fourcc;
    unsigned int rt_format;
    enum AVPixelFormat pix_fmt;
    const char* pix_fmt_name;
} vaapi_format_map[] = {
#define MAP(va, rt, av) { VA_FOURCC_ ## va, VA_RT_FORMAT_ ## rt, AV_PIX_FMT_ ## av, #av }
    MAP(NV12, YUV420,  NV12),
    MAP(YV12, YUV420,  YUV420P), // With U/V planes swapped.
    MAP(IYUV, YUV420,  YUV420P),
#ifdef VA_FOURCC_I420
    MAP(I420, YUV420,  YUV420P),
#endif
#ifdef VA_FOURCC_YV16
    MAP(YV16, YUV422,  YUV422P), // With U/V planes swapped.
#endif
    MAP(422H, YUV422,  YUV422P),
    MAP(UYVY, YUV422,  UYVY422),
    MAP(YUY2, YUV422,  YUYV422),
    MAP(411P, YUV411,  YUV411P),
    MAP(422V, YUV422,  YUV440P),
    MAP(444P, YUV444,  YUV444P),
    MAP(Y800, YUV400,  GRAY8),
#ifdef VA_FOURCC_P010
    MAP(P010, YUV420_10BPP, P010),
#endif
    MAP(BGRA, RGB32,   BGRA),
    MAP(BGRX, RGB32,   BGR0),
    MAP(RGBA, RGB32,   RGBA),
    MAP(RGBX, RGB32,   RGB0),
#ifdef VA_FOURCC_ABGR
    MAP(ABGR, RGB32,   ABGR),
    MAP(XBGR, RGB32,   0BGR),
#endif
    MAP(ARGB, RGB32,   ARGB),
    MAP(XRGB, RGB32,   0RGB),
#undef MAP
};

#if VA_CHECK_VERSION(1, 1, 0)

#define MAP(a) { VAConfigAttrib ##a, #a }

static const struct {
    VAConfigAttribType type;
    const char* attribute_name;
} config_attribute_list[] = {
    MAP(RTFormat),
    MAP(SpatialResidual),
    MAP(SpatialClipping),
    MAP(IntraResidual),
    MAP(Encryption),
    MAP(RateControl),
    MAP(DecSliceMode),
    MAP(DecJPEG),
    MAP(DecProcessing),
    MAP(EncPackedHeaders),
    MAP(EncInterlaced),
    MAP(EncMaxRefFrames),
    MAP(EncMaxSlices),
    MAP(EncSliceStructure),
    MAP(EncMacroblockInfo),
    MAP(MaxPictureWidth),
    MAP(MaxPictureHeight),
    MAP(EncJPEG),
    MAP(EncQualityRange),
    MAP(EncQuantization),
    MAP(EncIntraRefresh),
    MAP(EncSkipFrame),
    MAP(EncROI),
    MAP(EncRateControlExt),
    MAP(ProcessingRate),
    MAP(EncDirtyRect),
    MAP(EncParallelRateControl),
    MAP(EncDynamicScaling),
    MAP(FrameSizeToleranceSupport),
    MAP(FEIFunctionType),
    MAP(FEIMVPredictors),
    MAP(Stats),
    MAP(EncTileSupport),
    MAP(CustomRoundingControl),
    MAP(QPBlockSize),
};
#undef MAP

#endif

enum VaApiType {
    VAAPI_ENC = 1,
    VAAPI_DEC = 2,
    VAAPI_ENCDEC = VAAPI_ENC | VAAPI_DEC
};

int AddVaDeviceInfo(AVTextFormatContext *w, VaAdapterInfo *adapter_info);
int GetVaAdapterInfo(AVTextFormatContext *w, VaAdapterInfo **p_adapters);

int DetectVaapi(AVTextFormatContext *wctx, enum VaApiType apiType);


#endif /* FFDETECT_FFDETECT_VAAPI_H */
