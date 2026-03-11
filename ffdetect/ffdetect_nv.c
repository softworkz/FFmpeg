/*
 * Copyright (C) 2018 - softworkz for Emby Llc. 
 * All rights reserved.
 *
 * This code is not yet published under any license.
 *
 */

#include "libavutil/pixdesc.h"

//#include "ffmpeg.h"
#include "libavcodec/avcodec.h"
#include "ffdetect_nv.h"
#include "outputwriters.h"

static const struct {
    enum CUdevice_attribute_enum attribute;
    const char* attributeName;
} device_attributes[] = {
    { CU_DEVICE_ATTRIBUTE_CLOCK_RATE,               "ClockRate" },
    { CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT,     "MultiprocessorCount" },
    { CU_DEVICE_ATTRIBUTE_INTEGRATED,               "Integrated" },
    { CU_DEVICE_ATTRIBUTE_CAN_MAP_HOST_MEMORY,      "CanMapHostMemory" },
    { CU_DEVICE_ATTRIBUTE_COMPUTE_MODE,             "ComputeMode" },
    { CU_DEVICE_ATTRIBUTE_CONCURRENT_KERNELS,       "ConcurrentKernels" },
    { CU_DEVICE_ATTRIBUTE_PCI_BUS_ID,               "PciBusId" },
    { CU_DEVICE_ATTRIBUTE_PCI_DEVICE_ID,            "PciDeviceId" },
    { CU_DEVICE_ATTRIBUTE_TCC_DRIVER,               "TccDriver" },
    { CU_DEVICE_ATTRIBUTE_MEMORY_CLOCK_RATE,        "MemoryClockRate" },
    { CU_DEVICE_ATTRIBUTE_GLOBAL_MEMORY_BUS_WIDTH,  "GlobalMemoryBusWidth" },
    { CU_DEVICE_ATTRIBUTE_ASYNC_ENGINE_COUNT,       "AsyncEngineCount" },
    { CU_DEVICE_ATTRIBUTE_UNIFIED_ADDRESSING,       "UnifiedAddressing" },
    { CU_DEVICE_ATTRIBUTE_PCI_DOMAIN_ID,            "PciDomainId" },
    { CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, "ComputeCapabilityMajor" },
    { CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, "ComputeCapabilityMinor" },
    { CU_DEVICE_ATTRIBUTE_MANAGED_MEMORY,           "ManagedMemory" },
    { CU_DEVICE_ATTRIBUTE_MULTI_GPU_BOARD,          "MultiGpuBoard" },
    { CU_DEVICE_ATTRIBUTE_MULTI_GPU_BOARD_GROUP_ID, "MultiGpuBoardGroupId" },
};

static const struct {
    enum AVCodecID codecId;
    int bitDepth;
    cudaVideoCodec codec;
    const char* codecName;
} nvdec_codecs[] = {
    { AV_CODEC_ID_MPEG1VIDEO,  8, cudaVideoCodec_MPEG1, "mpeg1video" },
    { AV_CODEC_ID_MPEG2VIDEO,  8, cudaVideoCodec_MPEG2, "mpeg2video" },
    { AV_CODEC_ID_MPEG4,       8, cudaVideoCodec_MPEG4, "mp4" },
    { AV_CODEC_ID_VC1,         8, cudaVideoCodec_VC1,   "vc1" },
    { AV_CODEC_ID_H264,        8, cudaVideoCodec_H264,  "h264" },
    { AV_CODEC_ID_H264,       10, cudaVideoCodec_H264,  "h264" },
    { AV_CODEC_ID_HEVC,        8, cudaVideoCodec_HEVC,  "hevc" },
    { AV_CODEC_ID_HEVC,       10, cudaVideoCodec_HEVC,  "hevc" },
    { AV_CODEC_ID_HEVC,       12, cudaVideoCodec_HEVC,  "hevc" },
    { AV_CODEC_ID_VP8,         8, cudaVideoCodec_VP8,   "vp8" },
    { AV_CODEC_ID_VP9,         8, cudaVideoCodec_VP9,   "vp9" },
    { AV_CODEC_ID_VP9,        10, cudaVideoCodec_VP9,   "vp9" },
    { AV_CODEC_ID_VP9,        12, cudaVideoCodec_VP9,   "vp9" },
    { AV_CODEC_ID_AV1,         8, cudaVideoCodec_AV1,   "av1" },
    { AV_CODEC_ID_AV1,        10, cudaVideoCodec_AV1,   "av1" },
    { AV_CODEC_ID_AV1,        12, cudaVideoCodec_AV1,   "av1" },
};

static const struct {
    NV_ENC_CAPS capability;
    const char* key;
    const char* description;
} nvenc_caps[] = {
    {NV_ENC_CAPS_NUM_MAX_BFRAMES,                   "MaxBFrames",                              "Maximum number of B-Frames supported."},
    {NV_ENC_CAPS_SUPPORTED_RATECONTROL_MODES,       "RateControlModes",                        "Rate control modes supported."},
    {NV_ENC_CAPS_SUPPORT_FIELD_ENCODING,            "SupportsFieldMode",                       "Indicates HW support for field mode encoding."},
    {NV_ENC_CAPS_SUPPORT_MONOCHROME,                "SupportsMonochrome",                      "Indicates HW support for monochrome mode encoding."},
    {NV_ENC_CAPS_SUPPORT_FMO,                       "SupportsFmo",                             "Indicates HW support for FMO."},
    {NV_ENC_CAPS_SUPPORT_QPELMV,                    "SupportsQpMotion",                        "Indicates HW capability for Quarter pel motion estimation."},
    {NV_ENC_CAPS_SUPPORT_BDIRECT_MODE,              "SupportsBiDirect",                        "H.264 specific. Indicates HW support for BDirect modes."},
    {NV_ENC_CAPS_SUPPORT_CABAC,                     "SupportsCabac",                           "H264 specific. Indicates HW support for CABAC entropy coding mode."},
    {NV_ENC_CAPS_SUPPORT_ADAPTIVE_TRANSFORM,        "SupportsAdaptiveTransform",               "Indicates HW support for Adaptive Transform."},
    {NV_ENC_CAPS_NUM_MAX_TEMPORAL_LAYERS,           "SupportsTemporalLayers",                  "Indicates HW support for encoding Temporal layers."},
    {NV_ENC_CAPS_SUPPORT_HIERARCHICAL_PFRAMES,      "SupportsHierarchicalPFrames",             "Indicates HW support for Hierarchical P frames."},
    {NV_ENC_CAPS_SUPPORT_HIERARCHICAL_BFRAMES,      "SupportsHierarchicalBFrames",             "Indicates HW support for Hierarchical B frames."},
    {NV_ENC_CAPS_LEVEL_MAX,                         "MaxLevel",                                "Maximum Encoding level supported (See ::NV_ENC_LEVEL for details)."},
    {NV_ENC_CAPS_LEVEL_MIN,                         "MinLevel",                                "Minimum Encoding level supported (See ::NV_ENC_LEVEL for details)."},
    {NV_ENC_CAPS_SEPARATE_COLOUR_PLANE,             "SupportsSeparateColourPlane",             "Indicates HW support for separate colour plane encoding."},
    {NV_ENC_CAPS_WIDTH_MAX,                         "MaxWidth",                                "Maximum output width supported."},
    {NV_ENC_CAPS_HEIGHT_MAX,                        "MaxHeight",                               "Maximum output height supported."},
    {NV_ENC_CAPS_SUPPORT_TEMPORAL_SVC,              "SupportsTemporalScaling",                 "Indicates Temporal Scalability Support."},
    {NV_ENC_CAPS_SUPPORT_DYN_RES_CHANGE,            "SupportsResolutionChange",                "Indicates Dynamic Encode Resolution Change Support."},
    {NV_ENC_CAPS_SUPPORT_DYN_BITRATE_CHANGE,        "SupportsBitrateChange",                   "Indicates Dynamic Encode Bitrate Change Support."},
    {NV_ENC_CAPS_SUPPORT_DYN_FORCE_CONSTQP,         "ConstantQ",                               "Indicates Forcing Constant QP On The Fly Support."},
    {NV_ENC_CAPS_SUPPORT_DYN_RCMODE_CHANGE,         "SupportsDynamicRateControl",              "Indicates Dynamic rate control mode Change Support."},
    {NV_ENC_CAPS_SUPPORT_SUBFRAME_READBACK,         "SupportsSubframeReadback",                "Indicates Subframe readback support for slice-based encoding."},
    {NV_ENC_CAPS_SUPPORT_CONSTRAINED_ENCODING,      "SupportsConstrainedEncoding",             "Indicates Constrained Encoding mode support."},
    {NV_ENC_CAPS_SUPPORT_INTRA_REFRESH,             "SupportsIntraRefreshMode",                "Indicates Intra Refresh Mode Support."},
    {NV_ENC_CAPS_SUPPORT_CUSTOM_VBV_BUF_SIZE,       "SupportsCustomVbvBufferSize",             "Indicates Custom VBV Bufer Size support. It can be used for capping frame size."},
    {NV_ENC_CAPS_SUPPORT_DYNAMIC_SLICE_MODE,        "SupportsDynamicSlizeMode",                "Indicates Dynamic Slice Mode Support."},
    {NV_ENC_CAPS_SUPPORT_REF_PIC_INVALIDATION,      "SupportsRefPicInvalidation",              "Indicates Reference Picture Invalidation Support."},
    {NV_ENC_CAPS_PREPROC_SUPPORT,                   "SupportsPreProcessing",                   "Indicates support for PreProcessing."},
    {NV_ENC_CAPS_ASYNC_ENCODE_SUPPORT,              "SupportsAsyncMode",                       "Indicates support Async mode."},
    {NV_ENC_CAPS_MB_NUM_MAX,                        "MaximumMbsPerFrame",                      "Maximum MBs per frame supported."},
    {NV_ENC_CAPS_MB_PER_SEC_MAX,                    "MaximumThroughputMbs",                    "Maximum aggregate throughput in MBs per sec."},
    {NV_ENC_CAPS_SUPPORT_YUV444_ENCODE,             "SupportsYuv444",                          "Indicates HW support for YUV444 mode encoding."},
    {NV_ENC_CAPS_SUPPORT_LOSSLESS_ENCODE,           "SupportsLossless",                        "Indicates HW support for lossless encoding."},
    {NV_ENC_CAPS_SUPPORT_SAO,                       "SupportsAdaptiveOffset",                  "Indicates HW support for Sample Adaptive Offset."},
    {NV_ENC_CAPS_SUPPORT_MEONLY_MODE,               "SupportsMeOnlyMode",                      "Indicates HW support for MEOnly Mode."},
    {NV_ENC_CAPS_SUPPORT_LOOKAHEAD,                 "SupportsLookahead",                       "Indicates HW support for lookahead encoding (enableLookahead=1)."},
    {NV_ENC_CAPS_SUPPORT_TEMPORAL_AQ,               "SupportsTemporalAQ",                      "Indicates HW support for temporal AQ encoding (enableTemporalAQ=1)."},
    {NV_ENC_CAPS_SUPPORT_10BIT_ENCODE,              "Supports10Bit",                           "Indicates HW support for 10 bit encoding."},
    {NV_ENC_CAPS_NUM_MAX_LTR_FRAMES,                "MaxLtrFrames",                            "Maximum number of Long Term Reference frames supported"},
    {NV_ENC_CAPS_SUPPORT_WEIGHTED_PREDICTION,       "SupportsWeightedPrediction",              "Indicates HW support for Weighted Predicition."},
    {NV_ENC_CAPS_SUPPORT_BFRAME_REF_MODE,           "SupportsBAsReference",                    "Indicates B as refererence support."}
};

static void AddDeviceAttribute(AVTextFormatContext * w, CudaFunctions * cuda_dl, CUdevice cu_device, enum CUdevice_attribute_enum attribute, const char * attributeName)
{
    int pi = 0;
    int ret;

    ret = cuda_dl->cuDeviceGetAttribute(&pi, attribute, cu_device);

    if (ret == CUDA_SUCCESS) {
      //avtext_print_section_header(w, NULL, SECTION_ID_PROPERTY);
      print_int(attributeName, pi);
      //avtext_print_section_footer(w); // SECTION_ID_PROPERTY
    }
}

static int AddDeviceInfo(AVTextFormatContext * w, CudaFunctions * cuda_dl, int deviceIndex, CUdevice cu_device, int * computeCapMajor, int * computeCapMinor)
{
    char name[128] = { 0 };
    int ret;

    avtext_print_section_header(w, NULL, SECTION_ID_DEVICEINFO);

    ret = cuda_dl->cuDeviceGetName(name, sizeof(name), cu_device);
    if (ret != CUDA_SUCCESS) {
        av_log(NULL, AV_LOG_ERROR, "cuDeviceGetName failed on device %d\n", deviceIndex);
        return -1;
    }

    ret = cuda_dl->cuDeviceComputeCapability(computeCapMajor, computeCapMinor, cu_device);
    if (ret != CUDA_SUCCESS) {
        av_log(NULL, AV_LOG_ERROR, "cuDeviceComputeCapability failed on device %d\n", deviceIndex);
        return -1;
    }

    print_str("Name", name);
    print_str("Description", name);
    print_int("ComputeCapMajor", *computeCapMajor);
    print_int("ComputeCapMinor", *computeCapMinor);

    avtext_print_section_header(w, NULL, SECTION_ID_PROPERTIES);

    for (int n = 0; n < FF_ARRAY_ELEMS(device_attributes); n++) {

        AddDeviceAttribute(w, cuda_dl, cu_device, device_attributes[n].attribute, device_attributes[n].attributeName);
    }

    avtext_print_section_footer(w); // SECTION_ID_PROPERTIES

    avtext_print_section_footer(w); // SECTION_ID_DEVICEINFO

    return 0;
}

static void AddProfiles(AVTextFormatContext * w, NV_ENCODE_API_FUNCTION_LIST * nvenc_funcs, void * nvencoder, GUID  codecGuid)
{
    GUID *profile_guids;
    uint32_t profileCount;
    int ret;

    ret = nvenc_funcs->nvEncGetEncodeProfileGUIDCount(nvencoder, codecGuid, &profileCount);
    if (ret != NV_ENC_SUCCESS) {
        write_nvenc_error(w, ret, "GetEncodeProfileGUIDCount failed");
        return;
    }

    avtext_print_section_header(w, NULL, SECTION_ID_PROFILES);

    profile_guids = av_malloc(profileCount * sizeof(GUID));
    if (!profile_guids) {
        ret = AVERROR(ENOMEM);
        write_error_fmt(w, ret, "av_malloc failed");
        goto end;
    }

    ret = nvenc_funcs->nvEncGetEncodeProfileGUIDs(nvencoder, codecGuid, profile_guids, profileCount, &profileCount);
    if (ret != NV_ENC_SUCCESS) {
        write_nvenc_error(w, ret, "GetEncodeProfileGUIDs failed");
        goto end;
    }

    for (int n = 0; n < profileCount; n++) {
        avtext_print_section_header(w, NULL, SECTION_ID_PROFILE);
        print_guid("ProfileGuid", &profile_guids[n]);
        avtext_print_section_footer(w); // SECTION_ID_PROFILE
    }

end:
    avtext_print_section_footer(w); // SECTION_ID_PROFILES
}

static void AddPresets(AVTextFormatContext * w, NV_ENCODE_API_FUNCTION_LIST * nvenc_funcs, void * nvencoder, GUID  codecGuid)
{
    GUID *preset_guids;
    uint32_t presetCount;
    int ret;

    ret = nvenc_funcs->nvEncGetEncodePresetCount(nvencoder, codecGuid, &presetCount);
    if (ret != NV_ENC_SUCCESS) {
        write_nvenc_error(w, ret, "GetEncodePresetCount failed");
        return;
    }

    avtext_print_section_header(w, NULL, SECTION_ID_PRESETS);

    preset_guids = av_malloc(presetCount * sizeof(GUID));
    if (!preset_guids) {
        ret = AVERROR(ENOMEM);
        write_error_fmt(w, ret, "av_malloc failed");
        goto end;
    }

    ret = nvenc_funcs->nvEncGetEncodePresetGUIDs(nvencoder, codecGuid, preset_guids, presetCount, &presetCount);
    if (ret != NV_ENC_SUCCESS) {
        write_nvenc_error(w, ret, "GetEncodePresetGUIDs failed");
        goto end;
    }

    for (int n = 0; n < presetCount; n++) {

        NV_ENC_PRESET_CONFIG preset_config = { 0 };

        avtext_print_section_header(w, NULL, SECTION_ID_PRESET);
        print_guid("PresetGuid", &preset_guids[n]);

        preset_config.version = NV_ENC_PRESET_CONFIG_VER;
        preset_config.presetCfg.version = NV_ENC_CONFIG_VER;

        ret = nvenc_funcs->nvEncGetEncodePresetConfig(nvencoder, codecGuid, preset_guids[n], &preset_config);
        if (ret == NV_ENC_SUCCESS) {

            print_guid("ProfileGuid", &preset_config.presetCfg.profileGUID);
            print_int("GopLength", preset_config.presetCfg.gopLength);
            print_int("FrameIntervalPattern", preset_config.presetCfg.frameIntervalP);
            print_int("FrameFieldMode", preset_config.presetCfg.frameFieldMode);
            print_int("MotionVectorPrecision", preset_config.presetCfg.mvPrecision);


            if (IsEqualGUID(&codecGuid, &NV_ENC_CODEC_H264_GUID)) {
                // int
                print_int("EnableTemporalSVC", preset_config.presetCfg.encodeCodecConfig.h264Config.enableTemporalSVC);
                print_int("EnableStereoMVC", preset_config.presetCfg.encodeCodecConfig.h264Config.enableStereoMVC);
                print_int("HierarchicalPFrames", preset_config.presetCfg.encodeCodecConfig.h264Config.hierarchicalPFrames);
                print_int("HierarchicalBFrames", preset_config.presetCfg.encodeCodecConfig.h264Config.hierarchicalBFrames);
                print_int("OutputBufferingPeriodSEI", preset_config.presetCfg.encodeCodecConfig.h264Config.outputBufferingPeriodSEI);
                print_int("OutputPictureTimingSEI", preset_config.presetCfg.encodeCodecConfig.h264Config.outputPictureTimingSEI);
                print_int("OutputAUD", preset_config.presetCfg.encodeCodecConfig.h264Config.outputAUD);
                print_int("DisableSPSPPS", preset_config.presetCfg.encodeCodecConfig.h264Config.disableSPSPPS);
                print_int("OutputFramePackingSEI", preset_config.presetCfg.encodeCodecConfig.h264Config.outputFramePackingSEI);
                print_int("OutputRecoveryPointSEI", preset_config.presetCfg.encodeCodecConfig.h264Config.outputRecoveryPointSEI);
                print_int("EnableIntraRefresh", preset_config.presetCfg.encodeCodecConfig.h264Config.enableIntraRefresh);
                print_int("EnableConstrainedEncoding", preset_config.presetCfg.encodeCodecConfig.h264Config.enableConstrainedEncoding);
                print_int("RepeatSPSPPS", preset_config.presetCfg.encodeCodecConfig.h264Config.repeatSPSPPS);
                print_int("EnableVFR", preset_config.presetCfg.encodeCodecConfig.h264Config.enableVFR);
                print_int("EnableLTR", preset_config.presetCfg.encodeCodecConfig.h264Config.enableLTR);
                print_int("QpPrimeYZeroTransformBypassFlag", preset_config.presetCfg.encodeCodecConfig.h264Config.qpPrimeYZeroTransformBypassFlag);
                print_int("UseConstrainedIntraPred", preset_config.presetCfg.encodeCodecConfig.h264Config.useConstrainedIntraPred);
                print_int("ReservedBitFields", preset_config.presetCfg.encodeCodecConfig.h264Config.reservedBitFields);
                print_int("Level", preset_config.presetCfg.encodeCodecConfig.h264Config.level);
                print_int("IdrPeriod", preset_config.presetCfg.encodeCodecConfig.h264Config.idrPeriod);
                print_int("SeparateColourPlaneFlag", preset_config.presetCfg.encodeCodecConfig.h264Config.separateColourPlaneFlag);
                print_int("DisableDeblockingFilterIDC", preset_config.presetCfg.encodeCodecConfig.h264Config.disableDeblockingFilterIDC);
                print_int("NumTemporalLayers", preset_config.presetCfg.encodeCodecConfig.h264Config.numTemporalLayers);
                print_int("SpsId", preset_config.presetCfg.encodeCodecConfig.h264Config.spsId);
                print_int("PpsId", preset_config.presetCfg.encodeCodecConfig.h264Config.ppsId);
                print_int("IntraRefreshPeriod", preset_config.presetCfg.encodeCodecConfig.h264Config.intraRefreshPeriod);
                print_int("IntraRefreshCnt", preset_config.presetCfg.encodeCodecConfig.h264Config.intraRefreshCnt);
                print_int("MaxNumRefFrames", preset_config.presetCfg.encodeCodecConfig.h264Config.maxNumRefFrames);
                print_int("SliceMode", preset_config.presetCfg.encodeCodecConfig.h264Config.sliceMode);
                print_int("SliceModeData", preset_config.presetCfg.encodeCodecConfig.h264Config.sliceModeData);
                print_int("LtrNumFrames", preset_config.presetCfg.encodeCodecConfig.h264Config.ltrNumFrames);
                print_int("LtrTrustMode", preset_config.presetCfg.encodeCodecConfig.h264Config.ltrTrustMode);
                print_int("ChromaFormatIDC", preset_config.presetCfg.encodeCodecConfig.h264Config.chromaFormatIDC);
                print_int("MaxTemporalLayers", preset_config.presetCfg.encodeCodecConfig.h264Config.maxTemporalLayers);

                // enum
                print_int("AdaptiveTransformMode", preset_config.presetCfg.encodeCodecConfig.h264Config.adaptiveTransformMode);
                print_int("FmoMode", preset_config.presetCfg.encodeCodecConfig.h264Config.fmoMode);
                print_int("BdirectMode", preset_config.presetCfg.encodeCodecConfig.h264Config.bdirectMode);
                print_int("EntropyCodingMode", preset_config.presetCfg.encodeCodecConfig.h264Config.entropyCodingMode);
                print_int("StereoMode", preset_config.presetCfg.encodeCodecConfig.h264Config.stereoMode);
                print_int("UseBFramesAsRef", preset_config.presetCfg.encodeCodecConfig.h264Config.useBFramesAsRef);
            }

            if (IsEqualGUID(&codecGuid, &NV_ENC_CODEC_HEVC_GUID)) {
                // int
                print_int("Level", preset_config.presetCfg.encodeCodecConfig.hevcConfig.level);
                print_int("Tier", preset_config.presetCfg.encodeCodecConfig.hevcConfig.tier);
                print_int("UseConstrainedIntraPred", preset_config.presetCfg.encodeCodecConfig.hevcConfig.useConstrainedIntraPred);
                print_int("DisableDeblockAcrossSliceBoundary", preset_config.presetCfg.encodeCodecConfig.hevcConfig.disableDeblockAcrossSliceBoundary);
                print_int("OutputBufferingPeriodSEI", preset_config.presetCfg.encodeCodecConfig.hevcConfig.outputBufferingPeriodSEI);
                print_int("OutputPictureTimingSEI", preset_config.presetCfg.encodeCodecConfig.hevcConfig.outputPictureTimingSEI);
                print_int("OutputAUD", preset_config.presetCfg.encodeCodecConfig.hevcConfig.outputAUD);
                print_int("EnableLTR", preset_config.presetCfg.encodeCodecConfig.hevcConfig.enableLTR);
                print_int("DisableSPSPPS", preset_config.presetCfg.encodeCodecConfig.hevcConfig.disableSPSPPS);
                print_int("RepeatSPSPPS", preset_config.presetCfg.encodeCodecConfig.hevcConfig.repeatSPSPPS);
                print_int("EnableIntraRefresh", preset_config.presetCfg.encodeCodecConfig.hevcConfig.enableIntraRefresh);
                print_int("ChromaFormatIDC", preset_config.presetCfg.encodeCodecConfig.hevcConfig.chromaFormatIDC);
                print_int("Reserved", preset_config.presetCfg.encodeCodecConfig.hevcConfig.reserved);
                print_int("IdrPeriod", preset_config.presetCfg.encodeCodecConfig.hevcConfig.idrPeriod);
                print_int("IntraRefreshPeriod", preset_config.presetCfg.encodeCodecConfig.hevcConfig.intraRefreshPeriod);
                print_int("IntraRefreshCnt", preset_config.presetCfg.encodeCodecConfig.hevcConfig.intraRefreshCnt);
                print_int("MaxNumRefFramesInDPB", preset_config.presetCfg.encodeCodecConfig.hevcConfig.maxNumRefFramesInDPB);
                print_int("LtrNumFrames", preset_config.presetCfg.encodeCodecConfig.hevcConfig.ltrNumFrames);
                print_int("VpsId", preset_config.presetCfg.encodeCodecConfig.hevcConfig.vpsId);
                print_int("SpsId", preset_config.presetCfg.encodeCodecConfig.hevcConfig.spsId);
                print_int("PpsId", preset_config.presetCfg.encodeCodecConfig.hevcConfig.ppsId);
                print_int("SliceMode", preset_config.presetCfg.encodeCodecConfig.hevcConfig.sliceMode);
                print_int("SliceModeData", preset_config.presetCfg.encodeCodecConfig.hevcConfig.sliceModeData);
                print_int("MaxTemporalLayersMinus1", preset_config.presetCfg.encodeCodecConfig.hevcConfig.maxTemporalLayersMinus1);
                print_int("LtrTrustMode", preset_config.presetCfg.encodeCodecConfig.hevcConfig.ltrTrustMode);

                // enum
                print_int("MinCUSize", preset_config.presetCfg.encodeCodecConfig.hevcConfig.minCUSize);
                print_int("MaxCUSize", preset_config.presetCfg.encodeCodecConfig.hevcConfig.maxCUSize);
            }
        }

        avtext_print_section_footer(w); // SECTION_ID_PRESET
    }

end:
    avtext_print_section_footer(w); // SECTION_ID_PRESETS
}

static void AddEncoderCap(AVTextFormatContext * w, NV_ENCODE_API_FUNCTION_LIST * nvenc_funcs, void * nvencoder, GUID  guid, const char * key, NV_ENC_CAPS cap)
{
    NV_ENC_CAPS_PARAM capsParam = { NV_ENC_CAPS_PARAM_VER, cap, { 0 } };
    int v, r;

    capsParam.capsToQuery = cap;
    r = nvenc_funcs->nvEncGetEncodeCaps(nvencoder, guid, &capsParam, &v);
    if (r == NV_ENC_SUCCESS) {
        print_int(key, v);
    }
}

static int AddDecoders(AVTextFormatContext * w, CuvidFunctions * cvdl)
{
    avtext_print_section_header(w, NULL, SECTION_ID_DECODERS);

    for (int i = 0; i < FF_ARRAY_ELEMS(nvdec_codecs); i++) {

        CUVIDDECODECAPS decodeCaps = { 0 };
        int ret;

        decodeCaps.eCodecType = nvdec_codecs[i].codec;
        decodeCaps.eChromaFormat = cudaVideoChromaFormat_420;
        decodeCaps.nBitDepthMinus8 = nvdec_codecs[i].bitDepth - 8;

        ret = cvdl->cuvidGetDecoderCaps(&decodeCaps);

        avtext_print_section_header(w, NULL, SECTION_ID_DECODER);

        print_str("CodecName", nvdec_codecs[i].codecName);
        print_int("CodecId", nvdec_codecs[i].codec);
        print_int("BitDepth", nvdec_codecs[i].bitDepth);

        if (ret != CUDA_SUCCESS) {
            write_error_fmt(w, ret, "Failed getting decoder capabilities for %s %dbit", nvdec_codecs[i].codecName, nvdec_codecs[i].bitDepth);
            print_int("IsSupported", 0);
        }
        else {

            print_int("IsSupported", (int)decodeCaps.bIsSupported);
            print_int("MinWidth", decodeCaps.nMinWidth);
            print_int("MinHeight", decodeCaps.nMinHeight);
            print_int("MaxWidth", decodeCaps.nMaxWidth);
            print_int("MaxHeight", decodeCaps.nMaxHeight);
            print_int("MaxMacroBlocks", decodeCaps.nMaxMBCount);
        }
        avtext_print_section_footer(w); // SECTION_ID_DECODER
    }

    avtext_print_section_footer(w); // SECTION_ID_DECODERS

    return 0;
}

static int AddEncoders(AVTextFormatContext * w, CUcontext cuContext, NV_ENCODE_API_FUNCTION_LIST * nvenc_funcs, int computeCapMajor, int computeCapMinor)
{
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS params = { 0 };
    NVENCSTATUS ret = 0;
    GUID *guids;
    void *nvencoder = NULL;
    uint32_t codecCount;

    avtext_print_section_header(w, NULL, SECTION_ID_ENCODERS);

    if (((computeCapMajor << 4) | computeCapMinor) < 0x30) {
        write_error_fmt(w, -1, "Device compute caps %d.%d (below 3.0) does not support NVENC", computeCapMajor, computeCapMinor);
        goto fail;
    }

    params.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    params.apiVersion = NVENCAPI_VERSION;
    params.device = cuContext;
    params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;

    ret = nvenc_funcs->nvEncOpenEncodeSessionEx(&params, &nvencoder);
    if (ret != NV_ENC_SUCCESS) {
        write_nvenc_error(w, ret, "OpenEncodeSessionEx failed");
        goto fail;
    }

    ret = nvenc_funcs->nvEncGetEncodeGUIDCount(nvencoder, &codecCount);
    if (ret != NV_ENC_SUCCESS) {
        write_nvenc_error(w, ret, "GetEncodeGUIDCount failed");
        goto fail;
    }

    guids = av_malloc(codecCount * sizeof(GUID));
    if (!guids) {
        ret = AVERROR(ENOMEM);
        write_error_fmt(w, ret, "av_malloc failed");
        goto fail;
    }

    ret = nvenc_funcs->nvEncGetEncodeGUIDs(nvencoder, guids, codecCount, &codecCount);
    if (ret != NV_ENC_SUCCESS) {
        write_nvenc_error(w, ret, "GetEncodeGUIDs failed");
        goto fail;
    }

    for (int i = 0; i < codecCount; i++) {

        GUID codecGuid = guids[i];

        avtext_print_section_header(w, NULL, SECTION_ID_ENCODER);

        print_guid("CodecGuid", &codecGuid);

        if (IsEqualGUID(&codecGuid, &NV_ENC_CODEC_H264_GUID)) {
            print_str("CodecName", "h264");
            print_int("CodecId", AV_CODEC_ID_H264);
        }
        else if (IsEqualGUID(&codecGuid, &NV_ENC_CODEC_HEVC_GUID)) {
            print_str("CodecName", "hevc");
            print_int("CodecId", AV_CODEC_ID_HEVC);
        }
        else {
            print_str("CodecName", "unknown");
            print_int("CodecId", AV_CODEC_ID_NONE);
        }

        for (int n = 0; n < FF_ARRAY_ELEMS(nvenc_caps); n++) {

            AddEncoderCap(w, nvenc_funcs, nvencoder, codecGuid, nvenc_caps[n].key, nvenc_caps[n].capability);
        }

        // Presets
        AddPresets(w, nvenc_funcs, nvencoder, codecGuid);

        // Pofiles
        AddProfiles(w, nvenc_funcs, nvencoder, codecGuid);

        avtext_print_section_footer(w); // SECTION_ID_ENCODER
    }

fail:
    avtext_print_section_footer(w); // SECTION_ID_ENCODERS

    if (nvencoder != NULL) {
       nvenc_funcs->nvEncDestroyEncoder(nvencoder);
    }

    return ret;
}

#define NVENCAPI_CHECK_VERSION(major, minor) \
    ((major) < NVENCAPI_MAJOR_VERSION || ((major) == NVENCAPI_MAJOR_VERSION && (minor) <= NVENCAPI_MINOR_VERSION))

static void nvenc_print_driver_requirement(void)
{
#if NVENCAPI_CHECK_VERSION(8, 1)
# if defined(_WIN32) || defined(__CYGWIN__)
    const char *minver = "390.77";
# else
    const char *minver = "390.25";
# endif
#else
# if defined(_WIN32) || defined(__CYGWIN__)
    const char *minver = "378.66";
# else
    const char *minver = "378.13";
# endif
#endif
    av_log(NULL, AV_LOG_ERROR, "The minimum required Nvidia driver for nvenc is %s or newer\n", minver);
}

static void CheckDevice(AVTextFormatContext * w, int deviceIndex, CudaFunctions * cuda_dl, enum NvApiType apiType, CuvidFunctions * cvdl, NV_ENCODE_API_FUNCTION_LIST * p_nvenc_funcs)
{
    CUdevice cu_device;
    CUcontext cuContext = NULL;
    int computeCapMajor, computeCapMinor;
    int ret;

    avtext_print_section_header(w, NULL, SECTION_ID_DEVICE);

    print_int("DeviceIndex", deviceIndex);

    ret = cuda_dl->cuDeviceGet(&cu_device, deviceIndex);
    if (ret != CUDA_SUCCESS) {
        write_error_fmt(w, ret, "Cannot access the CUDA device %d", deviceIndex);
    }
    else {

        AddDeviceInfo(w, cuda_dl, deviceIndex, cu_device, &computeCapMajor, &computeCapMinor);

        ret = cuda_dl->cuCtxCreate(&cuContext, 0, cu_device);
        if (ret != CUDA_SUCCESS) {
            write_error_fmt(w, ret, "Failed creating CUDA context for device %d", deviceIndex);
        }
        else {

            if (apiType & NV_API_NVDEC) {
                AddDecoders(w, cvdl);
            }

            if (apiType & NV_API_NVENC) {
                AddEncoders(w, cuContext, p_nvenc_funcs, computeCapMajor, computeCapMinor);
            }

            cuda_dl->cuCtxDestroy(cuContext);
        }
    }

    avtext_print_section_footer(w); // SECTION_ID_DEVICE
}

int DetectNV(AVTextFormatContext *w, enum NvApiType apiType) {

    CudaFunctions *cuda_dl = NULL;
    CuvidFunctions *cvdl = NULL;
    NvencFunctions *enc_dl = NULL;
    NV_ENCODE_API_FUNCTION_LIST nvenc_funcs;

    int deviceCount = 0;
    uint32_t nvenc_max_ver;

    int ret = cuda_load_functions(&cuda_dl, NULL);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error loading CUDA functions\n");
        return ret;
    }

    if (apiType & NV_API_NVDEC) {

        ret = cuvid_load_functions(&cvdl, NULL);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Failed loading nvcuvid functions.\n");
            apiType &= ~NV_API_NVDEC;
        }
    }

    if (apiType & NV_API_NVENC) {
        ret = nvenc_load_functions(&enc_dl, NULL);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Failed loading nvenc functions.\n");
            nvenc_print_driver_requirement();
            apiType &= ~NV_API_NVENC;
        }
        else {
            ret = enc_dl->NvEncodeAPIGetMaxSupportedVersion(&nvenc_max_ver);
            if (ret != NV_ENC_SUCCESS) {
                nvenc_print_error(NULL, ret, "Failed to query nvenc max version");
                nvenc_print_driver_requirement();
                apiType &= ~NV_API_NVENC;
            }
            else {

                if ((NVENCAPI_MAJOR_VERSION << 4 | NVENCAPI_MINOR_VERSION) > nvenc_max_ver) {
                    av_log(NULL, AV_LOG_ERROR, "Driver does not support the required nvenc API version. "
                        "Required: %d.%d Found: %d.%d\n",
                        NVENCAPI_MAJOR_VERSION, NVENCAPI_MINOR_VERSION,
                        nvenc_max_ver >> 4, nvenc_max_ver & 0xf);
                    nvenc_print_driver_requirement();
                    apiType &= ~NV_API_NVENC;
                }
                else {
                    nvenc_funcs.version = NV_ENCODE_API_FUNCTION_LIST_VER;

                    ret = enc_dl->NvEncodeAPICreateInstance(&nvenc_funcs);
                    if (ret != NV_ENC_SUCCESS) {
                        nvenc_print_error(NULL, ret, "Failed to create nvenc instance");
                        apiType &= ~NV_API_NVENC;
                    }
                    else {
                        av_log(NULL, AV_LOG_VERBOSE, "Loaded Nvenc version %d.%d\n", nvenc_max_ver >> 4, nvenc_max_ver & 0xf);
                    }
                }
            }
        }
    }

    if (!apiType) {
        // No api could be loaded
        return ret;
    }

    ret = cuda_dl->cuInit(0);
    if (ret != CUDA_SUCCESS) {
        av_log(NULL, AV_LOG_ERROR, "Cannot init CUDA\n");
        return ret;
    }

    ret = cuda_dl->cuDeviceGetCount(&deviceCount);
    if (ret != CUDA_SUCCESS) {
        av_log(NULL, AV_LOG_ERROR, "Cannot enumerate the CUDA devices\n");
        return ret;
    }

    if (!deviceCount) {
        av_log(NULL, AV_LOG_ERROR, "No CUDA capable devices found\n");
        return -1;
    }

    av_log(NULL, AV_LOG_VERBOSE, "%d CUDA capable devices found\n", deviceCount);

    avtext_print_section_header(w, NULL, SECTION_ID_DEVICES);

    for (int deviceIndex = 0; deviceIndex < deviceCount; deviceIndex++) {

        NV_ENCODE_API_FUNCTION_LIST* p_nvenc_funcs = &nvenc_funcs;
        CheckDevice(w, deviceIndex, cuda_dl, apiType, cvdl, p_nvenc_funcs);
    }

    avtext_print_section_footer(w); // SECTION_ID_DEVICES

    return 0;
}

int nvenc_map_error(NVENCSTATUS err, const char **desc)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(nvenc_errors); i++) {
        if (nvenc_errors[i].nverr == err) {
            if (desc)
                *desc = nvenc_errors[i].desc;
            return nvenc_errors[i].averr;
        }
    }
    if (desc)
        *desc = "unknown error";
    return AVERROR_UNKNOWN;
}

int nvenc_print_error(void *log_ctx, NVENCSTATUS err, const char *error_string)
{
    const char *desc;
    int ret;
    ret = nvenc_map_error(err, &desc);
    av_log(log_ctx, AV_LOG_ERROR, "%s: %s (%d)\n", error_string, desc, err);
    return ret;
}

void write_nvenc_error(AVTextFormatContext * w, int ret, const char * msg)
{
    const char *desc;
    nvenc_map_error(ret, &desc);
    write_error_fmt(w, ret, "%s: %s", msg, desc);
}
