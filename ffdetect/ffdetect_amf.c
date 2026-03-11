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

#include "libavutil/pixdesc.h"
#include "libavcodec/avcodec.h"

#include "ffdetect_amf.h"
#include "outputwriters.h"

#include <AMF/core/Factory.h>

#include <AMF/components/VideoEncoderVCE.h>
#include <AMF/components/VideoEncoderHEVC.h>
#include <AMF/components/VideoEncoderAV1.h>

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

#define FF_D3DCREATE_FLAGS (D3DCREATE_SOFTWARE_VERTEXPROCESSING | \
                            D3DCREATE_MULTITHREADED | \
                            D3DCREATE_FPU_PRESERVE)

static const D3DPRESENT_PARAMETERS dxva2_present_params = {
    .Windowed = TRUE,
    .BackBufferWidth = 640,
    .BackBufferHeight = 480,
    .BackBufferCount = 0,
    .SwapEffect = D3DSWAPEFFECT_DISCARD,
    .Flags = D3DPRESENTFLAG_VIDEO,
};

#endif

static const char* AccelTypeToString(AMF_ACCELERATION_TYPE accelType)
{
    const char* strValue;
    switch (accelType) {
    case AMF_ACCEL_NOT_SUPPORTED:
        strValue = "AMF_ACCEL_NOT_SUPPORTED";
        break;
    case AMF_ACCEL_HARDWARE:
        strValue = "AMF_ACCEL_HARDWARE";
        break;
    case AMF_ACCEL_GPU:
        strValue = "AMF_ACCEL_GPU";
        break;
    case AMF_ACCEL_SOFTWARE:
        strValue = "AMF_ACCEL_SOFTWARE";
        break;
    default:
        strValue = "unknown";
    }
    return strValue;
}

static int CheckAddEncoder(AVTextFormatContext * w, AMFFactory *factory, AMFContext* context, const char* codecName, const char* codecIdStr, int codecType)
{
    AMFCaps* encoderCaps;
    AMFComponent* pEncoder;
    AMF_RESULT res;
    AMF_ACCELERATION_TYPE accelType;
    AMFIOCaps* inputCaps;
    AMFIOCaps* outputCaps;
    AMFVariantStruct var = { 0 };

    if (codecType == 1) {
        factory->pVtbl->CreateComponent(factory, context, AMFVideoEncoder_HEVC, &pEncoder);
    }
    else if (codecType == 2) {
        factory->pVtbl->CreateComponent(factory, context, AMFVideoEncoder_AV1, &pEncoder);
    }
    else {
        factory->pVtbl->CreateComponent(factory, context, AMFVideoEncoderVCE_AVC, &pEncoder);
    }

    if (pEncoder == NULL) {
        av_log(NULL, AV_LOG_VERBOSE, "AddEncoder - Codec not supported: %s\n", codecIdStr);
        return false;
    }

    res = pEncoder->pVtbl->GetCaps(pEncoder, &encoderCaps);
    if (res != AMF_OK) {
        av_log(NULL, AV_LOG_VERBOSE, "AddEncoder - GetCaps failed: %s\n", codecIdStr);
        return false;
    }

    avtext_print_section_header(w, NULL, SECTION_ID_ENCODER);

    print_str("CodecName", codecName);
    print_str("CodecComponentId", codecIdStr);

    accelType = encoderCaps->pVtbl->GetAccelerationType(encoderCaps);
    print_str("AccelerationType", AccelTypeToString(accelType));
    print_int("AccelerationTypeInt", accelType);

    if (codecType == 1) {
        if (AMF_OK == encoderCaps->pVtbl->GetProperty(encoderCaps, AMF_VIDEO_ENCODER_HEVC_CAP_MAX_PROFILE, &var))
            print_int("MaxProfile", var.int64Value);

        if (AMF_OK == encoderCaps->pVtbl->GetProperty(encoderCaps, AMF_VIDEO_ENCODER_HEVC_CAP_MAX_LEVEL, &var))
            print_int("MaxLevel", var.int64Value);

        if (AMF_OK == encoderCaps->pVtbl->GetProperty(encoderCaps, AMF_VIDEO_ENCODER_HEVC_CAP_NUM_OF_STREAMS, &var))
            print_int("MaxNumOfStreams", var.int64Value);

        if (AMF_OK == encoderCaps->pVtbl->GetProperty(encoderCaps, AMF_VIDEO_ENCODER_HEVC_CAP_MAX_BITRATE, &var))
            print_int("MaxBitRate", var.int64Value);
    }
    else if (codecType == 2) {
        if (AMF_OK == encoderCaps->pVtbl->GetProperty(encoderCaps, AMF_VIDEO_ENCODER_AV1_CAP_MAX_PROFILE, &var))
            print_int("MaxProfile", var.int64Value);

        if (AMF_OK == encoderCaps->pVtbl->GetProperty(encoderCaps, AMF_VIDEO_ENCODER_AV1_CAP_MAX_LEVEL, &var))
            print_int("MaxLevel", var.int64Value);

        if (AMF_OK == encoderCaps->pVtbl->GetProperty(encoderCaps, AMF_VIDEO_ENCODER_AV1_CAP_MAX_BITRATE, &var))
            print_int("MaxBitRate", var.int64Value);
    }
    else {
        if (AMF_OK == encoderCaps->pVtbl->GetProperty(encoderCaps, AMF_VIDEO_ENCODER_CAP_MAX_PROFILE, &var))
            print_int("MaxProfile", var.int64Value);

        if (AMF_OK == encoderCaps->pVtbl->GetProperty(encoderCaps, AMF_VIDEO_ENCODER_CAP_MAX_LEVEL, &var))
            print_int("MaxLevel", var.int64Value);

        if (AMF_OK == encoderCaps->pVtbl->GetProperty(encoderCaps, AMF_VIDEO_ENCODER_CAP_NUM_OF_STREAMS, &var))
            print_int("MaxNumOfStreams", var.int64Value);

        if (AMF_OK == encoderCaps->pVtbl->GetProperty(encoderCaps, AMF_VIDEO_ENCODER_CAP_MAX_BITRATE, &var))
            print_int("MaxBitRate", var.int64Value);
    }

    if (codecType == 1) {

        if (AMF_OK == encoderCaps->pVtbl->GetProperty(encoderCaps, AMF_VIDEO_ENCODER_HEVC_CAP_MAX_TIER, &var))
            print_int("MaxTier", var.int64Value);

        print_int("MaxInstanceCount", 1);
    }
    else if (codecType == 2) {
        if (AMF_OK == encoderCaps->pVtbl->GetProperty(encoderCaps, AMF_VIDEO_ENCODER_AV1_CAP_NUM_OF_HW_INSTANCES, &var))
            print_int("MaxInstanceCount", var.int64Value);
    }
    else {
        if (AMF_OK == encoderCaps->pVtbl->GetProperty(encoderCaps, AMF_VIDEO_ENCODER_CAP_MAX_TEMPORAL_LAYERS, &var))
            print_int("MaxTemporalLayers", var.int64Value);

        if (AMF_OK == encoderCaps->pVtbl->GetProperty(encoderCaps, AMF_VIDEO_ENCODER_CAP_NUM_OF_HW_INSTANCES, &var))
            print_int("MaxInstanceCount", var.int64Value);
    }

    if (encoderCaps->pVtbl->GetInputCaps(encoderCaps, &inputCaps) == AMF_OK) {

        amf_int32 minWidth, maxWidth, minHeight, maxHeight, vertAlign;
        amf_bool interlacedSupport;

        inputCaps->pVtbl->GetWidthRange(inputCaps, &minWidth, &maxWidth);
        inputCaps->pVtbl->GetHeightRange(inputCaps, &minHeight, &maxHeight);
        vertAlign = inputCaps->pVtbl->GetVertAlign(inputCaps);

        print_int("Input_MinWidth", minWidth);
        print_int("Input_MaxWidth", maxWidth);
        print_int("Input_MinHeight", minHeight);
        print_int("Input_MaxHeight", maxHeight);
        print_int("Input_WidthAlignment", 1);
        print_int("Input_HeightAlignment", vertAlign);

        interlacedSupport = inputCaps->pVtbl->IsInterlacedSupported(inputCaps);
        print_int("Input_SupportsInterlaced", interlacedSupport);
    }

    if (encoderCaps->pVtbl->GetOutputCaps(encoderCaps, &outputCaps) == AMF_OK) {

        amf_int32 minWidth, maxWidth, minHeight, maxHeight, vertAlign;
        amf_bool interlacedSupport;

        inputCaps->pVtbl->GetWidthRange(inputCaps, &minWidth, &maxWidth);
        inputCaps->pVtbl->GetHeightRange(inputCaps, &minHeight, &maxHeight);
        vertAlign = inputCaps->pVtbl->GetVertAlign(inputCaps);

        print_int("MinWidth", minWidth);
        print_int("MaxWidth", maxWidth);
        print_int("MinHeight", minHeight);
        print_int("MaxHeight", maxHeight);
        print_int("WidthAlignment", 1);
        print_int("HeightAlignment", vertAlign);

        interlacedSupport = inputCaps->pVtbl->IsInterlacedSupported(inputCaps);
        print_int("SupportsInterlaced", interlacedSupport);
    }

    avtext_print_section_footer(w); // SECTION_ID_ENCODER

    if (pEncoder) {
        pEncoder->pVtbl->Release(pEncoder);
        pEncoder = NULL;
    }

    return 0;
}

static int AddEncoders(AVTextFormatContext * w, AMFFactory *factory, AMFContext* pContext)
{
    avtext_print_section_header(w, NULL, SECTION_ID_ENCODERS);

    CheckAddEncoder(w, factory, pContext, "h264", "AMFVideoEncoderVCE_AVC", 0);
    CheckAddEncoder(w, factory, pContext, "hevc", "AMFVideoEncoder_HEVC", 1);
    CheckAddEncoder(w, factory, pContext, "av1",  "AMFVideoEncoder_AV1",  2);

    avtext_print_section_footer(w); // SECTION_ID_ENCODERS

    return 0;
}

#if CONFIG_D3D11VA
static int CheckDeviceDx11(AVTextFormatContext *w, ID3D11Device* ppDevice, AMFFactory *factory, enum AmfApiType apiType)
{
    AMFContext * context = NULL;
    AMF_RESULT aRes;

    // Init AMF
    aRes = factory->pVtbl->CreateContext(factory, &context);
    if (aRes != AMF_OK) {
        write_error_fmt(w, aRes, "CreateContext() failed with error %d", aRes);
        goto cleanup;
    }

    aRes = context->pVtbl->InitDX11(context, ppDevice, AMF_DX11_1);
    if (aRes != AMF_OK) {
        write_error_fmt(w, aRes, "AMF initialisation failed via DX11: error %d.\n", aRes);
        goto cleanup;
    }

    if (apiType & AMF_API_AMFENC) { 
        av_log(NULL, AV_LOG_VERBOSE, "Start CheckDeviceDx11.AddEncoders.\n");
        AddEncoders(w, factory, context);
        av_log(NULL, AV_LOG_VERBOSE, "End CheckDeviceDx11.AddEncoders.\n");
    }

cleanup:

    if (context) {
        av_log(NULL, AV_LOG_VERBOSE, "Start CheckDeviceDx11.Cleanup.\n");
        context->pVtbl->Terminate(context);
        av_log(NULL, AV_LOG_VERBOSE, "End CheckDeviceDx11.TerminateContext.\n");
        context->pVtbl->Release(context);
        av_log(NULL, AV_LOG_VERBOSE, "End CheckDeviceDx11.ReleaseContext.\n");
        context = NULL;
    }

    return aRes;
}

static int DetectAmfDx11(AVTextFormatContext *w, AMFFactory *factory, enum AmfApiType apiType)
{
    HANDLE d3dlib;
    HANDLE dxgilib = 0;
    int hRes;
    PFN_CREATE_DXGI_FACTORY mCreateDXGIFactory;
    PFN_D3D11_CREATE_DEVICE mD3D11CreateDevice;
    IDXGIFactory2 *pDXGIFactory = NULL;
    IDXGIAdapter* pAdapter = NULL;
    ID3D11Device* ppDevice = NULL;
    int success = 0;
    int adapterNum = 0;
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
        av_log(NULL, AV_LOG_ERROR, "CreateDXGIFactory failed: %d.\n", hRes);
        goto cleanup;
    }

    hRes = IDXGIFactory2_EnumAdapters(pDXGIFactory, adapterNum, &pAdapter);
    if (hRes < 0) {
        av_log(NULL, AV_LOG_ERROR, "IDXGIFactory2_EnumAdapters failed: %d.\n", hRes);
        goto cleanup;
    }

    avtext_print_section_header(w, NULL, SECTION_ID_DEVICES);

    while (IDXGIFactory2_EnumAdapters(pDXGIFactory, adapterNum, &pAdapter) >= 0) {

        DXGI_ADAPTER_DESC desc;
        hRes = IDXGIAdapter_GetDesc(pAdapter, &desc);
        if (hRes < 0) {
            av_log(NULL, AV_LOG_ERROR, "IDXGIAdapter_GetDesc failed: %d.\n", hRes);
            continue;
        }

        if (desc.VendorId == 5140 && desc.DeviceId == 140) {
            // Skip Microsoft Render Device
           if (pAdapter) {
              av_log(NULL, AV_LOG_VERBOSE, "Start DetectAmfDx11.ReleaseAdapter_0.\n");
              IDXGIAdapter_Release(pAdapter);
              av_log(NULL, AV_LOG_VERBOSE, "End DetectAmfDx11.ReleaseAdapter_0.\n");
              pAdapter = NULL;
           }

           adapterNum++;
           continue;
        }

        avtext_print_section_header(w, NULL, SECTION_ID_DEVICE);
        print_int("DeviceIndex", adapterNum);

        avtext_print_section_header(w, NULL, SECTION_ID_DEVICEINFO);

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

        avtext_print_section_footer(w); // SECTION_ID_DEVICEINFO

        hRes = mD3D11CreateDevice(pAdapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, D3D11_CREATE_DEVICE_VIDEO_SUPPORT, NULL, 0,
            D3D11_SDK_VERSION, &ppDevice, NULL, NULL);
        if (hRes < 0 || ppDevice == NULL) {
            av_log(NULL, AV_LOG_ERROR, "Failed creating the D3D11 device: %d.\n", hRes);
        }
        else {
            success = 1;

            if (apiType & AMF_API_AMFENC) {
                av_log(NULL, AV_LOG_VERBOSE, "Start DetectAmfDx11.CheckDeviceDx11.\n");
                CheckDeviceDx11(w, ppDevice, factory, apiType);
                av_log(NULL, AV_LOG_VERBOSE, "End DetectAmfDx11.CheckDeviceDx11.\n");
            }
        }

        avtext_print_section_footer(w); // SECTION_ID_DEVICE

        if (ppDevice) {
            av_log(NULL, AV_LOG_VERBOSE, "Start DetectAmfDx11.ReleaseDevice.\n");
            ID3D11Device_Release(ppDevice);
            av_log(NULL, AV_LOG_VERBOSE, "End DetectAmfDx11.ReleaseDevice.\n");
            ppDevice = NULL;
        }


        if (pAdapter) {
            av_log(NULL, AV_LOG_VERBOSE, "Start DetectAmfDx11.ReleaseAdapter.\n");
            IDXGIAdapter_Release(pAdapter);
            av_log(NULL, AV_LOG_VERBOSE, "End DetectAmfDx11.ReleaseAdapter.\n");
            pAdapter = NULL;
        }

        adapterNum++;
    }

    avtext_print_section_footer(w); // SECTION_ID_DEVICES

cleanup:

    if (pDXGIFactory) {
       av_log(NULL, AV_LOG_VERBOSE, "Start DetectAmfDx11.ReleaseFactory.\n");
       IDXGIFactory2_Release(pDXGIFactory);
       av_log(NULL, AV_LOG_VERBOSE, "End DetectAmfDx11.ReleaseFactory.\n");
    }

    if (dxgilib) {
       av_log(NULL, AV_LOG_VERBOSE, "Start DetectAmfDx11.CloseDxgi.\n");
       dlclose(dxgilib);
       av_log(NULL, AV_LOG_VERBOSE, "End DetectAmfDx11.CloseDxgi.\n");
    }

    if (d3dlib) {
       av_log(NULL, AV_LOG_VERBOSE, "Start DetectAmfDx11.CloseD3D.\n");
       dlclose(d3dlib);
       av_log(NULL, AV_LOG_VERBOSE, "End DetectAmfDx11.CloseD3D.\n");
    }

    return success;
}
#endif

#if CONFIG_DXVA2
static int CheckDeviceD3D9(AVTextFormatContext *w, int deviceIndex, IDirect3D9* d3d9, AMFFactory *factory, enum AmfApiType apiType)
{
    D3DADAPTER_IDENTIFIER9 adapterIdent;
    D3DPRESENT_PARAMETERS d3dpp = dxva2_present_params;
    HRESULT hRes;
    IDirect3DDevice9* d3d9device = NULL;;
    AMFContext * context = NULL;
    AMF_RESULT aRes;

    avtext_print_section_header(w, NULL, SECTION_ID_DEVICE);

    print_int("DeviceIndex", deviceIndex);

    hRes = IDirect3D9_GetAdapterIdentifier(d3d9, deviceIndex, 0, &adapterIdent);
    if (hRes != D3D_OK) {
        write_error_fmt(w, hRes, "GetAdapterIdentifier failed");
        return hRes;
    }

    hRes = IDirect3D9_CreateDevice(d3d9, deviceIndex, D3DDEVTYPE_HAL, NULL, FF_D3DCREATE_FLAGS, &d3dpp, &d3d9device);

    if (hRes < 0) {
        write_error_fmt(w, hRes, "Failed to create Direct3D device");
        return hRes;
    }

    // Init AMF
    aRes = factory->pVtbl->CreateContext(factory, &context);
    if (aRes != AMF_OK) {
        write_error_fmt(w, aRes, "CreateContext() failed with error %d", aRes);
        goto cleanup;
    }

    aRes = context->pVtbl->InitDX9(context, d3d9device);
    if (aRes != AMF_OK) {
        write_error_fmt(w, aRes, "AMF initialisation failed via D3D9: error %d.\n", aRes);
        goto cleanup;
    }

    avtext_print_section_header(w, NULL, SECTION_ID_DEVICEINFO);

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

    avtext_print_section_footer(w); // SECTION_ID_DEVICEINFO

    //if (apiType & AMF_API_AMFDEC) {
    //    AddDecoders(w, session);
    //}

    if (apiType & AMF_API_AMFENC) {
        AddEncoders(w, factory, context);
    }

cleanup:
    avtext_print_section_footer(w); // SECTION_ID_DEVICE

    if (context) {
        context->pVtbl->Terminate(context);
        context->pVtbl->Release(context);
        context = NULL;
    }

    if (d3d9device)
        IDirect3DDevice9_Release(d3d9device);

    return aRes;
}

static int DetectAmfD3D9(AVTextFormatContext *w, AMFFactory *factory, enum AmfApiType apiType, int disable_dx11)
{
    HMODULE d3dlib = 0;
    pDirect3DCreate9 *createD3D = NULL;
    IDirect3D9* d3d9 = NULL;
    int numAdapters;
    int successDx11 = 0;

#if CONFIG_D3D11VA
    if (!disable_dx11) {
        successDx11 = DetectAmfDx11(w, factory, apiType);

        if (successDx11) {
            goto cleanup;
        }

        av_log(NULL, AV_LOG_INFO, "DX11 initialization failed. Trying D3D9\n");
    }
#endif

    // Init D3D
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
    if (numAdapters == 0) {
        av_log(NULL, AV_LOG_ERROR, "No D3D adapters available.\n");
        goto cleanup;
    }

    avtext_print_section_header(w, NULL, SECTION_ID_DEVICES);

    for (int deviceIndex = 0; deviceIndex < numAdapters; deviceIndex++) {

        CheckDeviceD3D9(w, deviceIndex, d3d9, factory, apiType);
    }

    avtext_print_section_footer(w); // SECTION_ID_DEVICES

cleanup:

    if (d3d9)
        IDirect3D9_Release(d3d9);

    if (d3dlib)
        dlclose(d3dlib);

    return 0;
}

int DetectAmf(AVTextFormatContext *w, enum AmfApiType apiType, int disable_dx11)
{
    amf_handle         library; ///< handle to DLL library
    AMFFactory         *factory; ///< pointer to AMF factory
    ////AMFDebug           *debug;   ///< pointer to AMF debug interface
    ////AMFTrace           *trace;   ///< pointer to AMF trace interface
    AMFInit_Fn         init_fun;
    AMFQueryVersion_Fn version_fun;
    AMF_RESULT         res;
    amf_uint64         version; ///< version of AMF runtime
    int ret = 0;

    library = dlopen(AMF_DLL_NAMEA, RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        write_error_fmt(w, -1, "Failed to load AMF library dll");
        goto cleanup;
    }

    init_fun = (AMFInit_Fn)dlsym(library, AMF_INIT_FUNCTION_NAME);
    if (!init_fun) {
        write_error_fmt(w, -1, "DLL %s failed to find function %s", AMF_DLL_NAMEA, AMF_INIT_FUNCTION_NAME);
        goto cleanup;
    }

    version_fun = (AMFQueryVersion_Fn)dlsym(library, AMF_QUERY_VERSION_FUNCTION_NAME);
    if (!version_fun) {
        write_error_fmt(w, -1, "DLL %s failed to find function %s", AMF_DLL_NAMEA, AMF_QUERY_VERSION_FUNCTION_NAME);
        goto cleanup;
    }

    res = version_fun(&version);
    if (res != AMF_OK) {
        write_error_fmt(w, -1, "%s failed with error %d", AMF_QUERY_VERSION_FUNCTION_NAME, res);
        goto cleanup;
    }

    res = init_fun(AMF_FULL_VERSION, &factory);
    if (res != AMF_OK) {
        write_error_fmt(w, -1, "%s failed with error %d", AMF_INIT_FUNCTION_NAME, res);
        goto cleanup;
    }

    //res = factory->pVtbl->GetTrace(factory, &trace);
    //AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_UNKNOWN, "GetTrace() failed with error %d\n", res);
    //res = factory->pVtbl->GetDebug(factory, &debug);
    //AMF_RETURN_IF_FALSE(ctx, res == AMF_OK, AVERROR_UNKNOWN, "GetDebug() failed with error %d\n", res);

#if CONFIG_DXVA2
    ret = DetectAmfD3D9(w, factory, apiType, disable_dx11);
#endif

cleanup:

    if (library) {
        dlclose(library);
    }

    return ret;
}

#else

int DetectAmf(AVTextFormatContext *w, enum AmfApiType apiType, int disable_dx11)
{
    return 0;
}
#endif
