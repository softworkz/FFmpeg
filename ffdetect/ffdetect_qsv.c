/*
 * Copyright (C) 2018 - softworkz for Emby Llc.
 * All rights reserved.
 *
 * This code is not yet published under any license.
 *
 */

#include "config.h"

#if CONFIG_DXVA2

#include <windows.h>
#define COBJMACROS
#include <initguid.h>

#endif

#if CONFIG_VAAPI
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <fcntl.h>
#include <va/va.h>
#if HAVE_VAAPI_DRM
#include <va/va_drm.h>
#endif
#include "libavutil/hwcontext_vaapi.h"
#endif

#include "libavutil/pixdesc.h"
#include "libavcodec/avcodec.h"

#include "ffdetect_qsv.h"

#include "libavcodec/qsv_internal.h"

#include "outputwriters.h"

#include <mfxvp8.h>
#if !QSV_ONEVPL
#include <mfxplugin.h>
#endif

#if CONFIG_DXVA2

#include <d3d9.h>

#if CONFIG_D3D11VA
#include <d3d11.h>
#include <dxgi1_2.h>
#endif

#include "compat/w32dlfcn.h"

typedef IDirect3D9* WINAPI pDirect3DCreate9(UINT);

#if CONFIG_D3D11VA
typedef HRESULT(WINAPI* PFN_CREATE_DXGI_FACTORY)(REFIID riid, void **ppFactory);
#endif

#endif

#if CONFIG_VAAPI

#include "ffdetect_vaapi.h"

#endif


static const struct
{
    // actual implementation
    mfxIMPL impl;
    // adapter's number
    mfxU32 adapterID;

} implTypes[] = {
    {MFX_IMPL_HARDWARE, 0},
    {MFX_IMPL_HARDWARE2, 1},
    {MFX_IMPL_HARDWARE3, 2},
    {MFX_IMPL_HARDWARE4, 3}
};

static const struct {
    enum AVCodecID codecId;
    unsigned mfxCodec;
    const char* codecName;
    int maxWidth;
    int maxHeight;
} qsvdec_codecs[] = {
    { AV_CODEC_ID_MPEG2VIDEO, MFX_CODEC_MPEG2, "mpeg2video", 4096, 4096 },
    { AV_CODEC_ID_VC1,        MFX_CODEC_VC1,   "vc1", 16384, 16384 },
    { AV_CODEC_ID_H264,       MFX_CODEC_AVC,  "h264", 16384, 16384 },
    { AV_CODEC_ID_HEVC,       MFX_CODEC_HEVC,  "hevc", 16384,16384 },
    { AV_CODEC_ID_VP8,        MFX_CODEC_VP8,   "vp8", 4096, 2304 },
    { AV_CODEC_ID_VP9,        MFX_CODEC_VP9,   "vp9", 4096, 2304 },
    { AV_CODEC_ID_AV1,        MFX_CODEC_AV1,   "av1", 16384, 16384 }
};

static const struct {
    enum AVCodecID codecId;
    int mfxCodec;
    const char* codecName;
    int maxWidth;
    int maxHeight;
} qsvenc_codecs[] = {
    { AV_CODEC_ID_MPEG2VIDEO, MFX_CODEC_MPEG2, "mpeg2video", 0, 0 },
    { AV_CODEC_ID_H264,       MFX_CODEC_AVC,  "h264", 4096, 4096 },
    { AV_CODEC_ID_HEVC,       MFX_CODEC_HEVC,  "hevc", 0,0 },
    { AV_CODEC_ID_VP9,        MFX_CODEC_VP9,   "vp9", 0, 0 },
    { AV_CODEC_ID_AV1,        MFX_CODEC_AV1,   "av1", 0, 0 }
};

static const struct {
    int width;
    int height;
} resolutions[] = {
    {  176,  144 },
    {  352,  288 },
    {  352,  576 },
    {  720,  576 },
    {  960,  540 },
    { 1024,  576 },
    { 1280,  720 },
    { 1920, 1080 },
    { 1920, 1088 },
    { 2048, 1080 },
    { 2048, 1088 },
    { 3680, 1536 },
    { 3840, 2160 },
    { 4096, 2048 },
    { 4096, 2160 },
    { 4096, 2176 },
    { 4096, 4096 },
    { 7680, 4320 },
    { 8192, 4320 },
    { 8192, 8192 }
};

static const struct {
    enum AVCodecID codecId;
    unsigned short profile;
    const char* profileName;
} qsv_codecprofiles[] = {
    { AV_CODEC_ID_H264,       MFX_PROFILE_AVC_BASELINE,               "MFX_PROFILE_AVC_BASELINE"             },
    { AV_CODEC_ID_H264,       MFX_PROFILE_AVC_MAIN,                   "MFX_PROFILE_AVC_MAIN"                 },
    { AV_CODEC_ID_H264,       MFX_PROFILE_AVC_EXTENDED,               "MFX_PROFILE_AVC_EXTENDED"             },
    { AV_CODEC_ID_H264,       MFX_PROFILE_AVC_HIGH,                   "MFX_PROFILE_AVC_HIGH"                 },
    { AV_CODEC_ID_H264,       MFX_PROFILE_AVC_HIGH_422,               "MFX_PROFILE_AVC_HIGH_422"             },
    { AV_CODEC_ID_H264,       MFX_PROFILE_AVC_CONSTRAINED_BASELINE,   "MFX_PROFILE_AVC_CONSTRAINED_BASELINE" },
    { AV_CODEC_ID_H264,       MFX_PROFILE_AVC_CONSTRAINED_HIGH,       "MFX_PROFILE_AVC_CONSTRAINED_HIGH"     },
    { AV_CODEC_ID_H264,       MFX_PROFILE_AVC_PROGRESSIVE_HIGH,       "MFX_PROFILE_AVC_PROGRESSIVE_HIGH"     },

    { AV_CODEC_ID_HEVC,       MFX_PROFILE_HEVC_MAIN,                  "MFX_PROFILE_HEVC_MAIN"       },
    { AV_CODEC_ID_HEVC,       MFX_PROFILE_HEVC_MAIN10,                "MFX_PROFILE_HEVC_MAIN10"     },
    { AV_CODEC_ID_HEVC,       MFX_PROFILE_HEVC_MAINSP,                "MFX_PROFILE_HEVC_MAINSP"     },
    { AV_CODEC_ID_HEVC,       MFX_PROFILE_HEVC_REXT,                  "MFX_PROFILE_HEVC_REXT"       },

    { AV_CODEC_ID_MPEG2VIDEO, MFX_PROFILE_MPEG2_SIMPLE,               "MFX_PROFILE_MPEG2_SIMPLE" },
    { AV_CODEC_ID_MPEG2VIDEO, MFX_PROFILE_MPEG2_MAIN,                 "MFX_PROFILE_MPEG2_MAIN"   },
    { AV_CODEC_ID_MPEG2VIDEO, MFX_PROFILE_MPEG2_HIGH,                 "MFX_PROFILE_MPEG2_HIGH"   },

    { AV_CODEC_ID_VC1,        MFX_PROFILE_VC1_SIMPLE,                 "MFX_PROFILE_VC1_SIMPLE"   },
    { AV_CODEC_ID_VC1,        MFX_PROFILE_VC1_MAIN,                   "MFX_PROFILE_VC1_MAIN"     },
    { AV_CODEC_ID_VC1,        MFX_PROFILE_VC1_ADVANCED,               "MFX_PROFILE_VC1_ADVANCED" },

    { AV_CODEC_ID_VP8,        MFX_PROFILE_VP8_0,                      "MFX_PROFILE_VP8_0" },
    { AV_CODEC_ID_VP8,        MFX_PROFILE_VP8_1,                      "MFX_PROFILE_VP8_1" },
    { AV_CODEC_ID_VP8,        MFX_PROFILE_VP8_2,                      "MFX_PROFILE_VP8_2" },
    { AV_CODEC_ID_VP8,        MFX_PROFILE_VP8_3,                      "MFX_PROFILE_VP8_3" },

    { AV_CODEC_ID_VP9,        MFX_PROFILE_VP9_0,                      "MFX_PROFILE_VP9_0" },
    { AV_CODEC_ID_VP9,        MFX_PROFILE_VP9_1,                      "MFX_PROFILE_VP9_1" },
    { AV_CODEC_ID_VP9,        MFX_PROFILE_VP9_2,                      "MFX_PROFILE_VP9_2" },
    { AV_CODEC_ID_VP9,        MFX_PROFILE_VP9_3,                      "MFX_PROFILE_VP9_3" },

    { AV_CODEC_ID_AV1,        MFX_PROFILE_AV1_MAIN,                   "MFX_PROFILE_AV1_MAIN" },
    { AV_CODEC_ID_AV1,        MFX_PROFILE_AV1_HIGH,                   "MFX_PROFILE_AV1_HIGH" },
    { AV_CODEC_ID_AV1,        MFX_PROFILE_AV1_PRO,                     "MFX_PROFILE_AV1_PRO" },
};

static int AddEncoders(AVTextFormatContext * w, mfxSession session)
{
    mfxStatus ret;

    avtext_print_section_header(w, NULL, SECTION_ID_ENCODERS);

    for (unsigned i = 0; i < FF_ARRAY_ELEMS(qsvenc_codecs); i++) {

        unsigned resolutionCount = FF_ARRAY_ELEMS(resolutions);
        int maxWidth = 0;
        int maxHeight = 0;

        mfxVideoParam outParams = { .mfx.CodecId = qsvenc_codecs[i].mfxCodec };

        ret = MFXVideoENCODE_Query(session, NULL, &outParams);
        if (ret == MFX_ERR_UNSUPPORTED) {
            av_log(NULL, AV_LOG_VERBOSE, "AddEncoders - Codec not supported: %s\n", qsvenc_codecs[i].codecName);
            continue;
        }
        if (ret != MFX_ERR_NONE) {
            write_mfx_error(w, ret, "Error in MFXVideoENCODE_Query");
            continue;
        }

        if (outParams.mfx.CodecId == 0) {
            av_log(NULL, AV_LOG_VERBOSE, "AddEncoders - Codec not supported. Out codec id:  %s\n", qsvenc_codecs[i].codecName);
            continue;
        }

        // Check Resolutions
        for (unsigned n = 0; n < resolutionCount; n++) {

            mfxVideoParam in = { .mfx.CodecId = qsvenc_codecs[i].mfxCodec };
            mfxVideoParam out = { .mfx.CodecId = qsvenc_codecs[i].mfxCodec };

            // Create Simple params, we're just testing the profile here
            in.mfx.CodecProfile = MFX_PROFILE_UNKNOWN;
            in.mfx.CodecLevel = MFX_LEVEL_UNKNOWN;
            in.mfx.FrameInfo.Width = (unsigned short)resolutions[n].width;
            in.mfx.FrameInfo.Height = (unsigned short)resolutions[n].height;
            in.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
            in.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;

            ret = MFXVideoENCODE_Query(session, &in, &out);
            if (ret < MFX_ERR_NONE) {
                const char *errDesc;
                mfx_map_error(ret, &errDesc);

                av_log(NULL, AV_LOG_VERBOSE, "CheckEncoderResolutions: %s- Resolution not supported (1) %dx%d (err: %d - %s)\n",
                    qsvenc_codecs[i].codecName, resolutions[n].width, resolutions[n].height, ret, errDesc);
            }
            else if (out.mfx.FrameInfo.Width != resolutions[n].width || out.mfx.FrameInfo.Height != resolutions[n].height) {
                av_log(NULL, AV_LOG_VERBOSE, "CheckEncoderResolutions - Resolution not supported (2) %dx%d\n",
                    resolutions[n].width, resolutions[n].height);
            }
            else {
                maxWidth = resolutions[n].width;
                maxHeight = resolutions[n].height;
            }
        }

        if (maxWidth == 0 || maxHeight == 0) {
            continue;
        }

        avtext_print_section_header(w, NULL, SECTION_ID_ENCODER);

        print_str("CodecName", qsvenc_codecs[i].codecName);
        print_int("CodecId", qsvenc_codecs[i].mfxCodec);

        print_int("MinWidth", 32);
        print_int("MinHeight", 32);
        print_int("WidthAlignment", 16);
        print_int("HeightAlignment", 16);

        print_int("MaxWidth", maxWidth);
        print_int("MaxHeight", maxHeight);

        // Check profiles
        avtext_print_section_header(w, NULL, SECTION_ID_PROFILES);

        for (unsigned n = 0; n < FF_ARRAY_ELEMS(qsv_codecprofiles); n++) {

            if (qsv_codecprofiles[n].codecId == qsvenc_codecs[i].codecId) {

                mfxVideoParam in = { .mfx.CodecId = qsvenc_codecs[i].mfxCodec };
                mfxVideoParam out = { .mfx.CodecId = qsvenc_codecs[i].mfxCodec };

                // Create Simple params, we're just testing the profile here
                in.mfx.CodecProfile = (unsigned short)qsv_codecprofiles[n].profile;
                in.mfx.CodecLevel = MFX_LEVEL_UNKNOWN;
                in.mfx.FrameInfo.Width = 256;
                in.mfx.FrameInfo.Height = 256;
                in.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
                in.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;

                ret = MFXVideoENCODE_Query(session, &in, &out);
                if (ret < MFX_ERR_NONE) {
                    av_log(NULL, AV_LOG_VERBOSE, "AddEncoderProfiles - Profile not supported (1): %s\n", qsv_codecprofiles[n].profileName);
                }
                else if (out.mfx.CodecProfile != qsv_codecprofiles[n].profile
                    && qsvenc_codecs[i].codecId != AV_CODEC_ID_VP8
                    && qsvenc_codecs[i].codecId != AV_CODEC_ID_VP9) {
                    // Workaround a bug with VP8/9
                    av_log(NULL, AV_LOG_VERBOSE, "AddEnoderProfiles - Profile not supported (2): %s\n", qsv_codecprofiles[n].profileName);
                }
                else {
                    avtext_print_section_header(w, NULL, SECTION_ID_PROFILE);
                    print_int("ProfileId", qsv_codecprofiles[n].profile);
                    print_str("ProfileName", qsv_codecprofiles[n].profileName);
                    avtext_print_section_footer(w); // SECTION_ID_PROFILE
                }
            }
        }

        avtext_print_section_footer(w); // SECTION_ID_PROFILES

        avtext_print_section_footer(w); // SECTION_ID_ENCODER
    }

    avtext_print_section_footer(w); // SECTION_ID_ENCODERS

    return 0;
}

static int AddDecoders(AVTextFormatContext * w, mfxSession session)
{
    mfxStatus ret;

    avtext_print_section_header(w, NULL, SECTION_ID_DECODERS);

    for (unsigned i = 0; i < FF_ARRAY_ELEMS(qsvdec_codecs); i++) {

        mfxVideoParam outParams = { .mfx.CodecId = qsvdec_codecs[i].mfxCodec };

        ret = MFXVideoDECODE_Query(session, NULL, &outParams);
        if (ret == MFX_ERR_UNSUPPORTED) {
            av_log(NULL, AV_LOG_VERBOSE, "AddDecoders - Codec not supported: %s\n", qsvdec_codecs[i].codecName);
            continue;
        }
        if (ret != MFX_ERR_NONE) {
            write_mfx_error(w, ret, "Error in MFXVideoDECODE_Query");
            continue;
        }

        if (outParams.mfx.CodecId != qsvdec_codecs[i].mfxCodec) {
            av_log(NULL, AV_LOG_VERBOSE, "AddDecoders - Codec not supported. Out codec id:  %d\n", outParams.mfx.CodecId);
            continue;
        }

#if !QSV_ONEVPL
        if (qsvdec_codecs[i].codecId == AV_CODEC_ID_HEVC) {

            ret = MFXVideoUSER_Load(session, &MFX_PLUGINID_HEVCD_HW, 1);
            if (ret != MFX_ERR_NONE) {
                write_mfx_error(w, ret, "HEVC hardware decoding plugin cannot be loaded.");
                continue;
            }
        }
#endif

        avtext_print_section_header(w, NULL, SECTION_ID_DECODER);

        print_str("CodecName", qsvdec_codecs[i].codecName);
        print_int("CodecId", outParams.mfx.CodecId);

        print_int("MinWidth", outParams.mfx.FrameInfo.Width);
        print_int("MinHeight", outParams.mfx.FrameInfo.Height);
        print_int("MaxWidth", qsvdec_codecs[i].maxWidth);
        print_int("MaxHeight", qsvdec_codecs[i].maxHeight);
        print_int("WidthAlignment", 16);
        print_int("HeightAlignment", 16);

        // Check profiles
        avtext_print_section_header(w, NULL, SECTION_ID_PROFILES);

        for (unsigned n = 0; n < FF_ARRAY_ELEMS(qsv_codecprofiles); n++) {

            if (qsv_codecprofiles[n].codecId == qsvdec_codecs[i].codecId) {

                mfxVideoParam in = { .mfx.CodecId = qsvdec_codecs[i].mfxCodec };
                mfxVideoParam out = { .mfx.CodecId = qsvdec_codecs[i].mfxCodec };

                // Create Simple params, we're just testing the profile here
                in.mfx.CodecProfile = qsv_codecprofiles[n].profile;
                in.mfx.CodecLevel = MFX_LEVEL_UNKNOWN;
                in.mfx.FrameInfo.Width = 256;
                in.mfx.FrameInfo.Height = 256;
                in.mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
                in.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;

                ret = MFXVideoDECODE_Query(session, &in, &out);
                if (ret != MFX_ERR_NONE) {
                    av_log(NULL, AV_LOG_VERBOSE, "AddDecoderProfiles - Profile not supported (1): %s\n", qsv_codecprofiles[n].profileName);
                }
                else if (out.mfx.CodecProfile != qsv_codecprofiles[n].profile
                    && qsvdec_codecs[i].codecId != AV_CODEC_ID_VP8
                    && qsvdec_codecs[i].codecId != AV_CODEC_ID_VP9) {
                    // Workaround a bug with VP8/9
                    av_log(NULL, AV_LOG_VERBOSE, "AddDecoderProfiles - Profile not supported (2): %s\n", qsv_codecprofiles[n].profileName);
                }
                else {
                    avtext_print_section_header(w, NULL, SECTION_ID_PROFILE);
                    print_int("ProfileId", qsv_codecprofiles[n].profile);
                    print_str("ProfileName", qsv_codecprofiles[n].profileName);
                    avtext_print_section_footer(w); // SECTION_ID_PROFILE
                }
            }
        }

        avtext_print_section_footer(w); // SECTION_ID_PROFILES


        avtext_print_section_footer(w); // SECTION_ID_DECODER
    }

    avtext_print_section_footer(w); // SECTION_ID_DECODERS

    return 0;
}

#if CONFIG_DXVA2

static void AddD3dDeviceInfo(AVTextFormatContext *w, unsigned adapterNum)
{
    D3DADAPTER_IDENTIFIER9 adapterIdent;
    HMODULE d3dlib;
    HRESULT hRes;
    pDirect3DCreate9 *createD3D;
    IDirect3D9* d3d9 = NULL;
    unsigned numAdapters;

    d3dlib = dlopen("d3d9.dll", 0);
    if (!d3dlib) {
        av_log(NULL, AV_LOG_ERROR, "Failed to load D3D9 library\n");
        goto cleanup;
    }

    createD3D = (pDirect3DCreate9 *)dlsym(d3dlib, "Direct3DCreate9");
    if (!createD3D) {
        av_log(NULL, AV_LOG_ERROR, "Failed to locate Direct3DCreate9\n");
        goto cleanup;
    }

    d3d9 = createD3D(D3D_SDK_VERSION);
    if (!d3d9) {
        av_log(NULL, AV_LOG_ERROR, "Failed to create IDirect3D object\n");
        goto cleanup;
    }

    numAdapters = IDirect3D9_GetAdapterCount(d3d9);
    if (adapterNum >= numAdapters) {
        av_log(NULL, AV_LOG_ERROR, "Adapter number %d outside of range. %d adapters available.\n", adapterNum, numAdapters);
        goto cleanup;
    }

    hRes = IDirect3D9_GetAdapterIdentifier(d3d9, adapterNum, 0, &adapterIdent);
    if (hRes != D3D_OK) {
        av_log(NULL, AV_LOG_ERROR, "GetAdapterIdentifier failed: %ld.\n", hRes);
        goto cleanup;
    }

    print_str("DeviceName", adapterIdent.DeviceName);
    print_str("DirectXType", "DX9");
    print_str("Description", adapterIdent.Description);
    print_str("Driver", adapterIdent.Driver);

    print_int("DeviceId", adapterIdent.DeviceId);
    print_int("VendorId", adapterIdent.VendorId);
    print_int("DriverVersionMajor", adapterIdent.DriverVersion.HighPart);
    print_int("DriverVersionMinor", adapterIdent.DriverVersion.LowPart);
    print_int("SubSysId", adapterIdent.SubSysId);
    print_guid("DeviceGuid", &adapterIdent.DeviceIdentifier);

cleanup:

    if (d3d9)
        IDirect3D9_Release(d3d9);

    if (d3dlib)
        dlclose(d3dlib);
}

#if CONFIG_D3D11VA

static void AddD3d11DeviceInfo(AVTextFormatContext *w, unsigned adapterNum)
{
    HMODULE d3dlib;
    HMODULE dxgilib = 0;
    HRESULT hRes;
    PFN_CREATE_DXGI_FACTORY mCreateDXGIFactory;
    PFN_D3D11_CREATE_DEVICE mD3D11CreateDevice;
    IDXGIFactory2 *pDXGIFactory = NULL;
    IDXGIAdapter* pAdapter = NULL;
    ID3D11Device* ppDevice = NULL;
    DXGI_ADAPTER_DESC desc;
    AVBPrint pbuf;

    d3dlib = dlopen("d3d11.dll", 0);
    if (!d3dlib) {
        av_log(NULL, AV_LOG_ERROR, "Failed to load D3D11 library\n");
        goto cleanup;
    }

    dxgilib = dlopen("dxgi.dll", 0);
    if (!dxgilib) {
        av_log(NULL, AV_LOG_ERROR, "Failed to load DSGI library\n");
        goto cleanup;
    }

    mD3D11CreateDevice = (PFN_D3D11_CREATE_DEVICE)GetProcAddress(d3dlib, "D3D11CreateDevice");
    mCreateDXGIFactory = (PFN_CREATE_DXGI_FACTORY)GetProcAddress(dxgilib, "CreateDXGIFactory");

    if (!mD3D11CreateDevice || !mCreateDXGIFactory) {
        av_log(NULL, AV_LOG_ERROR, "Failed to load D3D11 library or its functions\n");
        goto cleanup;
    }

    hRes = mCreateDXGIFactory(&IID_IDXGIFactory2, (void **)&pDXGIFactory);
    if (hRes < 0) {
        av_log(NULL, AV_LOG_ERROR, "CreateDXGIFactory failed: %ld.\n", hRes);
        goto cleanup;
    }

    hRes = IDXGIFactory2_EnumAdapters(pDXGIFactory, adapterNum, &pAdapter);
    if (hRes < 0) {
        av_log(NULL, AV_LOG_ERROR, "IDXGIFactory2_EnumAdapters failed: %ld.\n", hRes);
        goto cleanup;
    }

    hRes = IDXGIAdapter_GetDesc(pAdapter, &desc);
    if (hRes < 0) {
        av_log(NULL, AV_LOG_ERROR, "IDXGIAdapter_GetDesc failed: %ld.\n", hRes);
        goto cleanup;
    }

    // Convert wchar-to-char
    av_bprint_init(&pbuf, 1, AV_BPRINT_SIZE_UNLIMITED);
    print_fmt("Description", "%ls", desc.Description);
    av_bprint_finalize(&pbuf, NULL);

    print_str("DirectXType", "DX11_1");
    print_int("DeviceId", desc.DeviceId);
    print_int("VendorId", desc.VendorId);
    print_int("SubSysId", desc.SubSysId);
    print_int("AdapterLuidHigh", desc.AdapterLuid.HighPart);
    print_int("AdapterLuidLow", desc.AdapterLuid.LowPart);
    print_int("DedicatedSystemMemory", desc.DedicatedSystemMemory);
    print_int("DedicatedVideoMemory", desc.DedicatedVideoMemory);
    print_int("SharedSystemMemory", desc.SharedSystemMemory);

    hRes = mD3D11CreateDevice(pAdapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, D3D11_CREATE_DEVICE_VIDEO_SUPPORT, NULL, 0,
        D3D11_SDK_VERSION, &ppDevice, NULL, NULL);
    if (hRes < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed creating the D3D11 device: %ld.\n", hRes);
    }

cleanup:

    if (ppDevice)
        ID3D11Device_Release(ppDevice);

    if (pAdapter)
        IDXGIAdapter_Release(pAdapter);

    if (pDXGIFactory)
        IDXGIFactory2_Release(pDXGIFactory);

    if (dxgilib)
        dlclose(dxgilib);

    if (d3dlib)
        dlclose(d3dlib);
}

#endif
#endif

static int CheckDevice(AVTextFormatContext *w, unsigned deviceIndex, mfxIMPL impl, enum QsvApiType apiType, int disable_dx11)
{
    mfxStatus ret;
    mfxSession session;
    mfxIMPL actualImpl;
    mfxIMPL baseImpl;
    mfxIMPL initImpl = impl;
    mfxVersion ver = { { QSV_VERSION_MINOR, QSV_VERSION_MAJOR } };
    mfxInitParam init_par = { MFX_IMPL_AUTO_ANY };

#if CONFIG_VAAPI && HAVE_VAAPI_DRM
    VADisplay va_dpy = NULL;
    VAStatus va_status = VA_STATUS_SUCCESS;
    int major_version, minor_version;
    int drm_fd = -1;
#endif


    avtext_print_section_header(w, NULL, SECTION_ID_DEVICE);

    print_int("DeviceIndex", deviceIndex);

    avtext_print_section_header(w, NULL, SECTION_ID_DEVICEINFO);

#if CONFIG_DXVA2
#if CONFIG_D3D11VA
    if (!disable_dx11) {
        initImpl |= MFX_IMPL_VIA_D3D11;
        AddD3d11DeviceInfo(w, deviceIndex);
    }
#else
    disable_dx11 = 1;
#endif

    if (disable_dx11) {
        initImpl |= MFX_IMPL_VIA_D3D9;
        AddD3dDeviceInfo(w, deviceIndex);
    }
#endif

#if CONFIG_VAAPI && HAVE_VAAPI_DRM
    initImpl |= MFX_IMPL_VIA_VAAPI;

    VaAdapterInfo* adapters;
    VaAdapterInfo* adapter;
    int device_count;

    device_count = GetVaAdapterInfo(w, &adapters);

    if (deviceIndex < device_count) {
        adapter = &adapters[deviceIndex];
        AddVaDeviceInfo(w, adapter);
    }
    else {
        avtext_print_section_footer(w); // SECTION_ID_DEVICEINFO
        write_error_fmt(w, -1, "No VA device found at index %d", deviceIndex);
        avtext_print_section_footer(w); // SECTION_ID_DEVICE
        return -1;
    }

    drm_fd = open(adapter->drm_render, O_RDWR);
    if (drm_fd < 0) {
        avtext_print_section_footer(w); // SECTION_ID_DEVICEINFO
        write_error_fmt(w, -1, "Failed to open the drm device %s", adapter->drm_render);
        avtext_print_section_footer(w); // SECTION_ID_DEVICE
        return -1;
    }

    va_dpy = vaGetDisplayDRM(drm_fd);
    if (!va_dpy) {
        avtext_print_section_footer(w); // SECTION_ID_DEVICEINFO
        write_error_fmt(w, -1, "Failed to get drm display %s", adapter->drm_render);
        avtext_print_section_footer(w); // SECTION_ID_DEVICE
        return -1;
    }

    va_status = vaInitialize(va_dpy, &major_version, &minor_version);
    if (va_status != VA_STATUS_SUCCESS) {
        avtext_print_section_footer(w); // SECTION_ID_DEVICEINFO
        write_error_fmt(w, va_status, "Failed to initialize VA %s. Error %d", adapter->drm_render, va_status);
        avtext_print_section_footer(w); // SECTION_ID_DEVICE
        return va_status;
    }

#endif

    init_par.Implementation = initImpl;
    init_par.Version        = ver;

    ret = MFXInitEx(init_par, &session);
    if (ret < MFX_ERR_NONE) {
        avtext_print_section_footer(w); // SECTION_ID_DEVICEINFO
        write_mfx_error(w, ret, "Error initializing an MFX session");
        avtext_print_section_footer(w); // SECTION_ID_DEVICE
        #if CONFIG_VAAPI && HAVE_VAAPI_DRM
            vaTerminate(va_dpy);
            close(drm_fd);
        #endif
        return ret;
    }

#if CONFIG_VAAPI && HAVE_VAAPI_DRM
        ret = MFXVideoCORE_SetHandle(session, (mfxHandleType)MFX_HANDLE_VA_DISPLAY, (mfxHDL)va_dpy);
        if (ret < 0) {
            avtext_print_section_footer(w); // SECTION_ID_DEVICEINFO
            write_mfx_error(w, ret, "Error setting VAAPI device handle");
            avtext_print_section_footer(w); // SECTION_ID_DEVICE
            return ret;
        }
#endif

    ret = MFXQueryIMPL(session, &actualImpl);
    if (ret < MFX_ERR_NONE) {
        avtext_print_section_footer(w); // SECTION_ID_DEVICEINFO
        write_mfx_error(w, ret, "Error in MFXQueryIMPL");
        goto end;
    }

    // extract the base implementation type
    baseImpl = MFX_IMPL_BASETYPE(actualImpl);

    if (baseImpl != impl) {
        avtext_print_section_footer(w); // SECTION_ID_DEVICEINFO
        write_error_fmt(w, ret, "Requested impl (%d) doesn't match returned impl (%d)", impl, baseImpl);
        goto end;
    }

    ret = MFXQueryVersion(session, &ver);
    if (ret < MFX_ERR_NONE) {
        avtext_print_section_footer(w); // SECTION_ID_DEVICEINFO
        write_mfx_error(w, ret, "Error querying MFX session version");
        goto end;
    }

    av_log(NULL, AV_LOG_VERBOSE, "Initialize MFX session: API version is %d.%d, implementation version is %d.%d\n",
        MFX_VERSION_MAJOR, MFX_VERSION_MINOR, ver.Major, ver.Minor);

    print_int("ApiVersionMajor", ver.Major);
    print_int("ApiVersionMinor", ver.Minor);

    avtext_print_section_footer(w); // SECTION_ID_DEVICEINFO

    if (apiType & QSV_API_QSVDEC) {
        AddDecoders(w, session);
    }

    if (apiType & QSV_API_QSVENC) {
        AddEncoders(w, session);
    }

end:
    avtext_print_section_footer(w); // SECTION_ID_DEVICE
    MFXClose(session);
#if CONFIG_VAAPI && HAVE_VAAPI_DRM
    vaTerminate(va_dpy);
    close(drm_fd);
#endif

    return ret;
}

int DetectQsv(AVTextFormatContext *w, enum QsvApiType apiType, int disable_dx11)
{
    avtext_print_section_header(w, NULL, SECTION_ID_DEVICES);

    for (unsigned deviceIndex = 0; deviceIndex < FF_ARRAY_ELEMS(implTypes); deviceIndex++) {

        CheckDevice(w, deviceIndex, implTypes[deviceIndex].impl, apiType, disable_dx11);
    }

    avtext_print_section_footer(w); // SECTION_ID_DEVICES

    return 0;
}


void mfx_map_error(mfxStatus err, const char **desc)
{
    unsigned i;
    for (i = 0; i < FF_ARRAY_ELEMS(mfx_errors); i++) {
        if (mfx_errors[i].status == err) {
            if (desc)
                *desc = mfx_errors[i].desc;
            return;
        }
    }

    if (desc)
        *desc = "unknown error";
}

void write_mfx_error(AVTextFormatContext * w, mfxStatus ret, const char * msg)
{
    const char *desc;
    mfx_map_error(ret, &desc);
    write_error_fmt(w, ret, "%s: %s", msg, desc);
}
