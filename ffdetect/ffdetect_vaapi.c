/*
 * Copyright (C) 2018 - softworkz for Emby Llc.
 * All rights reserved.
 *
 * This code is not yet published under any license.
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "ffdetect_vaapi.h"

#if HAVE_DIRENT_H
#include <dirent.h>
#else

 // This is only for suppresing intellisense errors in VS..
typedef unsigned long int __ino_t;	/* Type of file serial numbers.  */
typedef long int __off_t;	/* Type of file sizes and offsets.  */

struct dirent
{
#ifndef __USE_FILE_OFFSET64
    __ino_t d_ino;
    __off_t d_off;
#else
    __ino64_t d_ino;
    __off64_t d_off;
#endif
    unsigned short int d_reclen;
    unsigned char d_type;
    char d_name[256];		/* We must not include limits.h! */
};

extern int alphasort(const struct dirent **__e1, const struct dirent **__e2);

#endif

#include "libavcodec/avcodec.h"
#include "outputwriters.h"
#include "device_lookup.h"
//#include "libavutil/hwcontext_vaapi.h"

#if HAVE_VAAPI_DRM
#ifdef IN_LIBVA
# include "va/drm/va_drm.h"
#else
# include <va/va_drm.h>
#endif
#endif

#include "libavutil/avutil.h"

#define PCI_DIR "/sys/bus/pci/devices"
#define PCI_DISPLAY_CONTROLLER_CLASS 0x03

static int directory_filter(const struct dirent* dir_ent) {
    if (!dir_ent) return 0;
    if (!strcmp(dir_ent->d_name, ".")) return 0;
    if (!strcmp(dir_ent->d_name, "..")) return 0;
    return 1;
}

typedef int(*fsort)(const struct dirent**, const struct dirent**);

static int find_drm_nodes(AVTextFormatContext *w, VaAdapterInfo* adapter_info) {
    int nodes_count = 0;
    int i = 0;
    struct dirent** dir_entries = NULL;
    int entries_num;
    char drm_path[300] = { 0 };

    (void)w;

    av_log(NULL, AV_LOG_DEBUG, "Begin get_nodes\n");

    adapter_info->drm_card = NULL;
    adapter_info->drm_render = NULL;

    snprintf(drm_path, sizeof(drm_path) / sizeof(drm_path[0]), "%s/drm", adapter_info->device_path);

    entries_num = scandir(drm_path, &dir_entries, directory_filter, (fsort)alphasort);

    av_log(NULL, AV_LOG_DEBUG, "Found %d drm entries\n", entries_num);

    for (i = 0; i < entries_num; ++i) {

        char node_path[300] = { 0 };

        if (!dir_entries[i]) {
            av_log(NULL, AV_LOG_DEBUG, "Empty drm entry at index %d\n", i);
            continue;
        }

        snprintf(node_path, sizeof(node_path) / sizeof(node_path[0]), "/dev/dri/%s", dir_entries[i]->d_name);

        if (strncmp(dir_entries[i]->d_name, "cardX", 4) == 0) {
            adapter_info->drm_card = av_strdup(node_path);
        }

        if (strncmp(dir_entries[i]->d_name, "renderX", 6) == 0) {
            adapter_info->drm_render = av_strdup(node_path);
        }

        av_log(NULL, AV_LOG_TRACE, "Found drm node '%s'\n", node_path);

        free(dir_entries[i]);
    }

    if (entries_num)
        free(dir_entries);

    return nodes_count;
}

static int get_device_value_int(char* devicePath, const char* valueName, int radix) {
    char file_name[300] = { 0 };
    FILE* file = NULL;
    int res = 0;
    char str[16] = { 0 };

    snprintf(file_name, sizeof(file_name) / sizeof(file_name[0]), "%s/%s", devicePath, valueName);
    file = fopen(file_name, "r");
    if (file) {
        if (fgets(str, sizeof(str), file)) {
            res = strtol(str, NULL, radix);
        }
        fclose(file);
    }

    return res;
}

int GetVaAdapterInfo(AVTextFormatContext *w, VaAdapterInfo** p_adapters) {
    int adapters_num = 0;
    int i = 0;
    VaAdapterInfo* adapters = NULL;
    struct dirent** dir_entries = NULL;
    int entries_num;

    // sizeof(PCI_DIR) = 20, sizeof(dirent::d_name) <= 256, sizeof("class"|"vendor"|"device") = 6
    char file_name[300] = { 0 };
    // sizeof("0xzzzzzz") = 8
    char str[16] = { 0 };
    FILE* file = NULL;

    putenv(av_strdup("LIBVA_MESSAGING_LEVEL=0"));

    av_log(NULL, AV_LOG_DEBUG, "Begin GetVaAdapterInfo\n");

    entries_num = scandir(PCI_DIR, &dir_entries, directory_filter, (fsort)alphasort);

    av_log(NULL, AV_LOG_VERBOSE, "Found %d device entries\n", entries_num);

    for (i = 0; i < entries_num; ++i) {

        long int class_id = 0, vendor_id = 0, device_id = 0, vendor_id_sub = 0, device_id_sub = 0;
        char* devicePath;

        if (!dir_entries[i]) {
            av_log(NULL, AV_LOG_WARNING, "Empty dir entry at index %d\n", i);
            continue;
        }

        // obtaining device class id
        snprintf(file_name, sizeof(file_name) / sizeof(file_name[0]), "%s/%s/%s", PCI_DIR, dir_entries[i]->d_name, "class");

        av_log(NULL, AV_LOG_TRACE, "Check device at index %d: %s\n", i, file_name);

        file = fopen(file_name, "r");

        if (file) {

            if (fgets(str, sizeof(str), file)) {
                class_id = strtol(str, NULL, 16);
            }
            fclose(file);

            if (PCI_DISPLAY_CONTROLLER_CLASS == (class_id >> 16)) {

                snprintf(file_name, sizeof(file_name) / sizeof(file_name[0]), "%s/%s", PCI_DIR, dir_entries[i]->d_name);
                devicePath = av_strdup(file_name);

                // obtaining device vendor id
                vendor_id = get_device_value_int(devicePath, "vendor", 16);
                device_id = get_device_value_int(devicePath, "device", 16);
                vendor_id_sub = get_device_value_int(devicePath, "subsystem_vendor", 16);
                device_id_sub = get_device_value_int(devicePath, "subsystem_device", 16);

                // adding valid adaptor to the list
                if (vendor_id && device_id) {
                    char nameBuf[300] = { 0 };
                    const char* name;

                    av_log(NULL, AV_LOG_TRACE, "Adding adapter '%s' - VendorId: %ld DeviceId: %ld - Subsystem: V: %ld, D: %ld\n", file_name, vendor_id, device_id, vendor_id_sub, device_id_sub);

                    // Resize array
                    adapters = (VaAdapterInfo*)realloc(adapters, (adapters_num + 1) * sizeof(VaAdapterInfo));

                    adapters[adapters_num].vendor_id = vendor_id;
                    adapters[adapters_num].device_id = device_id;
                    adapters[adapters_num].device_path = devicePath;
                    adapters[adapters_num].enable = get_device_value_int(devicePath, "enable", 10);
                    adapters[adapters_num].boot_vga = get_device_value_int(devicePath, "boot_vga", 10);
                    adapters[adapters_num].vendor_id_sub = vendor_id_sub;
                    adapters[adapters_num].device_id_sub = device_id_sub;


                    // Add vendor name
                    name = GetDeviceName(nameBuf, sizeof(nameBuf), ID_VENDOR, vendor_id);
                    av_log(NULL, AV_LOG_TRACE, "       adapter '%s' - VendorName: %s\n", file_name, name);
                    adapters[adapters_num].vendor_name = av_strdup(name);

                    // Add device name
                    name = GetDeviceName(nameBuf, sizeof(nameBuf), ID_DEVICE, vendor_id, device_id);
                    av_log(NULL, AV_LOG_TRACE, "       adapter '%s' - DeviceName: %s\n", file_name, name);
                    adapters[adapters_num].device_name = av_strdup(name);

                    // Add subsystem vendor name
                    name = GetDeviceName(nameBuf, sizeof(nameBuf), ID_VENDOR, adapters[adapters_num].vendor_id_sub);
                    av_log(NULL, AV_LOG_TRACE, "       adapter '%s' - Subsystem VendorName: %s\n", file_name, name);
                    adapters[adapters_num].vendor_name_sub = av_strdup(name);

                    // Add subsystem device name
                    name = GetDeviceName(nameBuf, sizeof(nameBuf), ID_SUBSYSTEM, vendor_id, device_id, vendor_id_sub, device_id_sub);
                    av_log(NULL, AV_LOG_TRACE, "       adapter '%s' - Subsystem DeviceName: %s\n", file_name, name);
                    adapters[adapters_num].device_name_sub = av_strdup(name);

                    find_drm_nodes(w, &adapters[adapters_num]);

                    ++adapters_num;
                }
            }
            else {
                av_log(NULL, AV_LOG_TRACE, "Device is not a display controller: %s\n", file_name);
            }
        }
        else {
            av_log(NULL, AV_LOG_WARNING, "Failed to open device at index %d: %s\n", i, file_name);
        }

        free(dir_entries[i]);
    }

    if (entries_num)
        free(dir_entries);

    if (p_adapters)
        *p_adapters = adapters;

    av_log(NULL, AV_LOG_DEBUG, "End GetVaAdapterInfo\n");

    return adapters_num;
}


static int AddConfigAttributes(AVTextFormatContext * w, VADisplay va_dpy, VAProfile va_profile, VAEntrypoint va_entrypoint, const char* profile_name) {
#if VA_CHECK_VERSION(1, 1, 0)

    VAStatus vas;
    int i;
    int attr_count = FF_ARRAY_ELEMS(config_attribute_list);
    VAConfigAttrib attr[100];

    for (i = 0; i < attr_count; i++) {
        attr[i].type = config_attribute_list[i].type;
    }

    vas = vaGetConfigAttributes(va_dpy, va_profile, va_entrypoint, attr, attr_count);
    if (vas != VA_STATUS_SUCCESS) {
        write_error_fmt(w, vas, "Failed to fetch config attributes for profile %s: %s", profile_name, vaErrorStr(vas));
        return AVERROR(EINVAL);
    }

    for (i = 0; i < attr_count; i++) {

        if (attr[i].value == VA_ATTRIB_NOT_SUPPORTED) {
            continue;
        }

        print_int(config_attribute_list[i].attribute_name, attr[i].value);
    }
#endif

    return 0;
}

static int AddProcessingRate(AVTextFormatContext * w, VADisplay va_dpy, VAConfigID config_id, const char* profile_name) {
    VAStatus vas;
    unsigned int processing_rate;

#if VA_CHECK_VERSION(1, 1, 0)

    vas = vaQueryProcessingRate(va_dpy, config_id, NULL, &processing_rate);
    if (vas == VA_STATUS_SUCCESS) {

        print_int("ProcessingRate", processing_rate);
    }

#endif

    return 0;
}

////static int AddConfigAttributes2(AVTextFormatContext * w, VADisplay va_dpy, VAConfigID config_id, const char* profile_name)
////{
////    VAStatus vas;
////    int i;
////    int attr_count = 100;
////    VAConfigAttrib attr[100];
////    VAProfile profile;
////    VAEntrypoint entrypoint;
////
////    vas = vaQueryConfigAttributes(va_dpy, config_id, &profile, &entrypoint, attr, &attr_count);
////    if (vas != VA_STATUS_SUCCESS) {
////        write_error_fmt(w, vas, "Failed to query config attributes for profile %s: %s", profile_name, vaErrorStr(vas));
////        return AVERROR(EINVAL);
////    }
////
////    av_log(NULL, AV_LOG_INFO, "\nAddConfigAttributes2 returned: %d\n", attr_count);
////
////    for (i = 0; i < attr_count; i++) {
////
////        if (attr[i].value == VA_ATTRIB_NOT_SUPPORTED) {
////            continue;
////        }
////
////        av_log(NULL, AV_LOG_INFO, "ATTR2: %d: %d\n", attr[i].type, attr[i].value);
////    }
////
////    return 0;
////}

static int AddSurfaceAttributes(AVTextFormatContext * w, VADisplay va_dpy, VAConfigID va_config, const char* profile_name) {
    VASurfaceAttrib *attr_list = NULL;
    VAStatus vas;
    unsigned int fourcc;
    int err, i, t;
    unsigned int attr_count;
    char pix_fmt_str[500] = { 0 };

    attr_count = 0;
    vas = vaQuerySurfaceAttributes(va_dpy, va_config, 0, &attr_count);
    if (vas != VA_STATUS_SUCCESS) {
        write_error_fmt(w, vas, "Failed to query surface attributes for profile %s: %s", profile_name, vaErrorStr(vas));
        err = AVERROR(ENOSYS);
        goto fail;
    }

    attr_list = av_malloc(attr_count * sizeof(*attr_list));
    if (!attr_list) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    vas = vaQuerySurfaceAttributes(va_dpy, va_config, attr_list, &attr_count);
    if (vas != VA_STATUS_SUCCESS) {
        write_error_fmt(w, vas, "Failed to query surface attributes for profile %s: %s", profile_name, vaErrorStr(vas));
        err = AVERROR(ENOSYS);
        goto fail;
    }

    for (i = 0; i < attr_count; i++) {

        switch (attr_list[i].type) {

        case VASurfaceAttribPixelFormat:

            fourcc = attr_list[i].value.value.i;

            for (t = 0; t < FF_ARRAY_ELEMS(vaapi_format_map); t++) {
                if (vaapi_format_map[t].fourcc == fourcc) {

                    av_strlcat(pix_fmt_str, vaapi_format_map[t].pix_fmt_name, sizeof(pix_fmt_str) / sizeof(pix_fmt_str[0]));
                    av_strlcat(pix_fmt_str, " ", sizeof(pix_fmt_str) / sizeof(pix_fmt_str[0]));
                }
            }

            break;

        case VASurfaceAttribMinWidth:
            print_int("MinWidth", attr_list[i].value.value.i);
            break;
        case VASurfaceAttribMinHeight:
            print_int("MinHeight", attr_list[i].value.value.i);
            break;
        case VASurfaceAttribMaxWidth:
            print_int("MaxWidth", attr_list[i].value.value.i);
            break;
        case VASurfaceAttribMaxHeight:
            print_int("MaxHeight", attr_list[i].value.value.i);
            break;
        }
    }

    print_str("ColorFormats", pix_fmt_str);

    err = 0;
fail:
    av_freep(&attr_list);
    return err;
}

static int AddCodecs(AVTextFormatContext * w, VADisplay va_dpy, enum VaApiType apiType) {
    VAStatus vas;
    int i, j, n;
    VAProfile *profile_list = NULL;
    VAEntrypoint *entrypoint_list = NULL;
    VAEntrypoint target_entrypoint = VAEntrypointVLD;
    int profile_count;
    int entrypoint_count, max_entrypoint_count;
    SectionID section_id;

    profile_count = vaMaxNumProfiles(va_dpy);

    profile_list = av_malloc_array(profile_count, sizeof(VAProfile));
    if (!profile_list) {
        write_error_fmt(w, AVERROR(ENOMEM), "Failed alloc profile array");
        return AVERROR(ENOMEM);
    }

    max_entrypoint_count = vaMaxNumEntrypoints(va_dpy);
    entrypoint_list = malloc(max_entrypoint_count * sizeof(VAEntrypoint));
    if (!entrypoint_list) {
        write_error_fmt(w, AVERROR(ENOMEM), "Failed alloc entrypoint array");
        return AVERROR(ENOMEM);
    }

    vas = vaQueryConfigProfiles(va_dpy, profile_list, &profile_count);
    if (vas != VA_STATUS_SUCCESS) {
        write_error_fmt(w, vas, "Failed to query profiles: %s", vaErrorStr(vas));
        goto fail;
    }

    if (apiType & VAAPI_DEC) {
        target_entrypoint = VAEntrypointVLD;
        section_id = SECTION_ID_DECODER;
    }
    else {
        target_entrypoint = VAEntrypointEncSlice;
        section_id = SECTION_ID_ENCODER;
    }

    for (n = 0; n < profile_count; n++) {

        for (i = 0; i < FF_ARRAY_ELEMS(vaapi_profile_map); i++) {

            if (profile_list[n] == vaapi_profile_map[i].va_profile) {

                entrypoint_count = max_entrypoint_count;

                vas = vaQueryConfigEntrypoints(va_dpy, profile_list[n], entrypoint_list, &entrypoint_count);
                if (vas != VA_STATUS_SUCCESS) {
                    write_error_fmt(w, vas, "Failed to query entrypoints for profile %s: %s", vaapi_profile_map[i].profile_name, vaErrorStr(vas));
                }
                else {

                    for (j = 0; j < entrypoint_count; j++) {

                        if (entrypoint_list[j] == target_entrypoint) {
                            VAConfigID va_config = VA_INVALID_ID;


                            avtext_print_section_header(w, NULL, section_id);

                            print_str("CodecName", vaapi_profile_map[i].codec_name);
                            print_str("CodecProfile", vaapi_profile_map[i].profile_name);
                            print_int("CodecId", vaapi_profile_map[i].codec_id);

                            vas = vaCreateConfig(va_dpy, profile_list[n], target_entrypoint, NULL, 0, &va_config);
                            if (vas != VA_STATUS_SUCCESS) {
                                write_error_fmt(w, vas, "Failed to create configuration for profile %s: %s", vaapi_profile_map[i].profile_name, vaErrorStr(vas));
                            }
                            else {

                                AddSurfaceAttributes(w, va_dpy, va_config, vaapi_profile_map[i].profile_name);
                                AddConfigAttributes(w, va_dpy, vaapi_profile_map[i].va_profile, target_entrypoint, vaapi_profile_map[i].profile_name);
                                AddProcessingRate(w, va_dpy, va_config, vaapi_profile_map[i].profile_name);

                                vaDestroyConfig(va_dpy, va_config);
                            }

                            avtext_print_section_footer(w);

                        }
                    }
                }
            }
        }
    }

    av_freep(&profile_list);

fail:
    av_freep(&profile_list);
    return vas;
}

static int AddFilters(AVTextFormatContext * w, VADisplay va_dpy) {
    VAStatus vas;
    int i, j, n;
    VAEntrypoint *entrypoint_list = NULL;
    int entrypoint_count, max_entrypoint_count;
    VAConfigID va_config = VA_INVALID_ID;
    VAContextID va_context = VA_INVALID_ID;
    VAProcFilterType filters[VAProcFilterCount];
    int num_filters = VAProcFilterCount;

    max_entrypoint_count = vaMaxNumEntrypoints(va_dpy);
    entrypoint_list = malloc(max_entrypoint_count * sizeof(VAEntrypoint));
    if (!entrypoint_list) {
        write_error_fmt(w, AVERROR(ENOMEM), "Failed alloc entrypoint array");
        return AVERROR(ENOMEM);
    }

    vas = vaQueryConfigEntrypoints(va_dpy, VAProfileNone, entrypoint_list, &entrypoint_count);
    if (vas != VA_STATUS_SUCCESS) {
        write_error_fmt(w, vas, "Failed to query entrypoints for VAProfileNone: %s", vaErrorStr(vas));
        goto fail;
    }

    for (j = 0; j < entrypoint_count; j++) {

        if (entrypoint_list[j] == VAEntrypointVideoProc) {
            break;
        }
    }

    if (j >= entrypoint_count) {
        write_error_fmt(w, vas, "Entry point VAEntrypointVideoProc is not available.");
        goto fail;
    }

    vas = vaCreateConfig(va_dpy, VAProfileNone, VAEntrypointVideoProc, NULL, 0, &va_config);
    if (vas != VA_STATUS_SUCCESS) {
        write_error_fmt(w, vas, "Failed to create config for VAEntrypointVideoProc: %s", vaErrorStr(vas));
        goto fail;
    }

    vas = vaCreateContext(va_dpy, va_config, 1920, 1080, VA_PROGRESSIVE, 0, 0, &va_context);
    if (vas != VA_STATUS_SUCCESS) {
        write_error_fmt(w, vas, "Failed to create context for VAEntrypointVideoProc: %s", vaErrorStr(vas));
        goto fail;
    }

    vas = vaQueryVideoProcFilters(va_dpy, va_context, filters, &num_filters);
    if (vas != VA_STATUS_SUCCESS) {
        write_error_fmt(w, vas, "Failed to execute vaQueryVideoProcFilters: %s", vaErrorStr(vas));
        goto fail;
    }

    for (int filtersIndx = 0; filtersIndx < num_filters; filtersIndx++) {
        if (filters[filtersIndx]) {
            char *filter_name = NULL;
            int is_supported = 0;

            avtext_print_section_header(w, NULL, SECTION_ID_FILTER);

            switch (filters[filtersIndx]) {
                case VAProcFilterNoiseReduction:
                    filter_name = "Noise reduction filter";
                    break;
                case VAProcFilterDeinterlacing:
                    filter_name = "Deinterlacing filter";
                    break;
                case VAProcFilterSharpening:
                    filter_name = "Sharpening filter";
                    break;
                case VAProcFilterColorBalance:
                    filter_name = "Color balance parameters";
                    break;
                case VAProcFilterSkinToneEnhancement:
                    filter_name = "Skin Tone Enhancement";
                    break;
                case VAProcFilterTotalColorCorrection:
                    filter_name = "Total Color Correction";
                    break;
                case VAProcFilterHVSNoiseReduction:
                    filter_name = "Human Vision System(HVS) Noise reduction filter";
                    break;
                case VAProcFilterHighDynamicRangeToneMapping:
                    filter_name = "High Dynamic Range Tone Mapping";
                    break;
                default:
                    filter_name = "Unknown Filter";
                    break;
            }

            print_int("FilterId", filters[filtersIndx]);
            print_str("FilterName", filter_name);

            switch (filters[filtersIndx]) {
                case VAProcFilterNoiseReduction:
                case VAProcFilterSharpening:
                case VAProcFilterSkinToneEnhancement:
                case VAProcFilterHVSNoiseReduction:
                    {
                        VAProcFilterCap cap[16];
                        int num_query_caps = 16;

                        vas = vaQueryVideoProcFilterCaps(va_dpy, va_context,
                                                         filters[filtersIndx], &cap, &num_query_caps);
                        if (vas != VA_STATUS_SUCCESS) {
                            write_error_fmt(w, vas, "Failed to query vaQueryVideoProcFilterCaps: %s", vaErrorStr(vas));
                        }
                        else if (num_query_caps > 0) {

                            is_supported = 1;
                            avtext_print_section_header(w, NULL, SECTION_ID_PROPERTIES);

                            for (i = 0; i < num_query_caps; i++) {

                                avtext_print_section_header(w, NULL, SECTION_ID_PROPERTY);

                                print_int("ParamIndex", i);
                                print_double("MinValue", cap[i].range.min_value);
                                print_double("MaxValue", cap[i].range.max_value);
                                print_double("DefaultValue", cap[i].range.default_value);
                                print_double("Step", cap[i].range.step);

                                avtext_print_section_footer(w); // SECTION_ID_PROPERTY
                            }

                            avtext_print_section_footer(w); // SECTION_ID_PROPERTIES
                        }

                        break;
                    }

                case VAProcFilterDeinterlacing:
                    {
                        VAProcFilterCapDeinterlacing cap[16];
                        int num_query_caps = 16;

                        vas = vaQueryVideoProcFilterCaps(va_dpy, va_context,
                                                         filters[filtersIndx], &cap, &num_query_caps);
                        if (vas != VA_STATUS_SUCCESS) {
                            write_error_fmt(w, vas, "Failed to query vaQueryVideoProcFilterCaps: %s", vaErrorStr(vas));
                        }
                        else if (num_query_caps > 0) {

                            is_supported = 1;
                            avtext_print_section_header(w, NULL, SECTION_ID_PROPERTIES);

                            for (i = 0; i < num_query_caps; i++) {

                                avtext_print_section_header(w, NULL, SECTION_ID_PROPERTY);

                                print_int("DeinterlacingType", cap[i].type);

                                avtext_print_section_footer(w); // SECTION_ID_PROPERTY
                            }

                            avtext_print_section_footer(w); // SECTION_ID_PROPERTIES
                        }

                        break;
                    }
                case VAProcFilterColorBalance:
                    {
                        VAProcFilterCapColorBalance cap[16];
                        int num_query_caps = 16;

                        vas = vaQueryVideoProcFilterCaps(va_dpy, va_context,
                                                         filters[filtersIndx], &cap, &num_query_caps);
                        if (vas != VA_STATUS_SUCCESS) {
                            write_error_fmt(w, vas, "Failed to query vaQueryVideoProcFilterCaps: %s", vaErrorStr(vas));
                        }
                        else if (num_query_caps > 0) {

                            is_supported = 1;
                            avtext_print_section_header(w, NULL, SECTION_ID_PROPERTIES);

                            for (i = 0; i < num_query_caps; i++) {

                                avtext_print_section_header(w, NULL, SECTION_ID_PROPERTY);

                                print_int("ColorBalanceType", cap[i].type);
                                print_double("MinValue", cap[i].range.min_value);
                                print_double("MaxValue", cap[i].range.max_value);
                                print_double("DefaultValue", cap[i].range.default_value);
                                print_double("Step", cap[i].range.step);

                                avtext_print_section_footer(w); // SECTION_ID_PROPERTY
                            }

                            avtext_print_section_footer(w); // SECTION_ID_PROPERTIES
                        }

                        break;
                    }
                case VAProcFilterTotalColorCorrection:
                    {
                        VAProcFilterCapTotalColorCorrection cap[16];
                        int num_query_caps = 16;

                        vas = vaQueryVideoProcFilterCaps(va_dpy, va_context,
                                                         filters[filtersIndx], &cap, &num_query_caps);
                        if (vas != VA_STATUS_SUCCESS) {
                            write_error_fmt(w, vas, "Failed to query vaQueryVideoProcFilterCaps: %s", vaErrorStr(vas));
                        }
                        else if (num_query_caps > 0) {

                            is_supported = 1;
                            avtext_print_section_header(w, NULL, SECTION_ID_PROPERTIES);

                            for (i = 0; i < num_query_caps; i++) {

                                avtext_print_section_header(w, NULL, SECTION_ID_PROPERTY);

                                print_int("ColorCorrectionType", cap[i].type);
                                print_double("MinValue", cap[i].range.min_value);
                                print_double("MaxValue", cap[i].range.max_value);
                                print_double("DefaultValue", cap[i].range.default_value);
                                print_double("Step", cap[i].range.step);

                                avtext_print_section_footer(w); // SECTION_ID_PROPERTY
                            }

                            avtext_print_section_footer(w); // SECTION_ID_PROPERTIES
                        }

                        break;
                    }
                case VAProcFilterHighDynamicRangeToneMapping:
                    {
                        VAProcFilterCapHighDynamicRange cap[16];
                        int num_query_caps = 16;

                        vas = vaQueryVideoProcFilterCaps(va_dpy, va_context,
                                                         VAProcFilterHighDynamicRangeToneMapping,
                                                         &cap, &num_query_caps);
                        if (vas != VA_STATUS_SUCCESS) {
                            write_error_fmt(w, vas, "Failed to query vaQueryVideoProcFilterCaps: %s", vaErrorStr(vas));
                        }
                        else if (num_query_caps > 0) {

                            avtext_print_section_header(w, NULL, SECTION_ID_PROPERTIES);

                            for (i = 0; i < num_query_caps; i++) {
                                if (cap[i].metadata_type != VAProcHighDynamicRangeMetadataNone)
                                {
                                    is_supported = 1;
                                    avtext_print_section_header(w, NULL, SECTION_ID_PROPERTY);

                                    print_int("MatadataType", cap[i].metadata_type);
                                    print_int("CapFlags", cap[i].caps_flag);

                                    avtext_print_section_footer(w); // SECTION_ID_PROPERTY
                                }
                            }

                            avtext_print_section_footer(w); // SECTION_ID_PROPERTIES
                        }

                        break;
                    }
            }

            print_int("IsSupported", is_supported);

            avtext_print_section_footer(w);
        }
    }

    return 0;
fail:
    return -1;
}

int AddVaDeviceInfo(AVTextFormatContext *w, VaAdapterInfo* adapter_info) {
    print_str("VendorName", adapter_info->vendor_name);
    print_str("DeviceName", adapter_info->device_name);
    print_str("SubsytemVendorName", adapter_info->vendor_name_sub);
    print_str("SubsytemDeviceName", adapter_info->device_name_sub);
    print_int("VendorId", adapter_info->vendor_id);
    print_int("DeviceId", adapter_info->device_id);
    print_int("SubsytemVendorId", adapter_info->vendor_id_sub);
    print_int("SubsytemDeviceId", adapter_info->device_id_sub);

    print_str("DevPath", adapter_info->device_path);
    print_str("DrmCard", adapter_info->drm_card);
    print_str("DrmRender", adapter_info->drm_render);

    print_int("IsEnabled", adapter_info->enable);
    print_int("IsBootVga", adapter_info->boot_vga);

    return 0;
}

static int CheckDevice(AVTextFormatContext *w, int deviceIndex, VaAdapterInfo* adapter_info, enum VaApiType apiType) {
    VADisplay va_dpy = NULL;
    VAStatus va_status = VA_STATUS_SUCCESS;
    const char *driver;
    int major_version, minor_version;
    int drm_fd = -1;

    avtext_print_section_header(w, NULL, SECTION_ID_DEVICE);

    print_int("DeviceIndex", deviceIndex);

    avtext_print_section_header(w, NULL, SECTION_ID_DEVICEINFO);

    AddVaDeviceInfo(w, adapter_info);

    drm_fd = open(adapter_info->drm_render, O_RDWR);
    if (drm_fd < 0) {
        write_error_fmt(w, -1, "Failed to open the drm device %s", adapter_info->drm_render);
        avtext_print_section_footer(w); // SECTION_ID_DEVICEINFO
        goto end;
    }

    va_dpy = vaGetDisplayDRM(drm_fd);
    if (!va_dpy) {
        write_error_fmt(w, -1, "Failed to get drm display %s", adapter_info->drm_render);
        avtext_print_section_footer(w); // SECTION_ID_DEVICEINFO
        goto end;
    }

    va_status = vaInitialize(va_dpy, &major_version, &minor_version);
    if (va_status != VA_STATUS_SUCCESS) {
        write_error_fmt(w, va_status, "Failed to initialize VA %s. Error %d", adapter_info->drm_render, va_status);
        avtext_print_section_footer(w); // SECTION_ID_DEVICEINFO
        goto end;
    }

    av_log(NULL, AV_LOG_VERBOSE, "%s: VA-API version: %d.%d (libva %s)\n", adapter_info->drm_render, major_version, minor_version, VA_VERSION_S);

    print_int("ApiVersionMajor", major_version);
    print_int("ApiVersionMinor", minor_version);

    driver = vaQueryVendorString(va_dpy);
    if (driver) {
        av_log(NULL, AV_LOG_VERBOSE, "%s: Driver version: %s\n", adapter_info->drm_render, driver ? driver : "<unknown>");
        print_str("Driver", driver);
    }

    avtext_print_section_footer(w); // SECTION_ID_DEVICEINFO

    if (apiType & VAAPI_DEC) {
        avtext_print_section_header(w, NULL, SECTION_ID_DECODERS);
        AddCodecs(w, va_dpy, VAAPI_DEC);
        avtext_print_section_footer(w); // SECTION_ID_DECODERS
    }

    if (apiType & VAAPI_ENC) {
        avtext_print_section_header(w, NULL, SECTION_ID_ENCODERS);
        AddCodecs(w, va_dpy, VAAPI_ENC);
        avtext_print_section_footer(w); // SECTION_ID_ENCODERS
    }

    avtext_print_section_header(w, NULL, SECTION_ID_FILTERS);
    AddFilters(w, va_dpy);
    avtext_print_section_footer(w); // SECTION_ID_FILTERS

    vaTerminate(va_dpy);
    close(drm_fd);

end:
    if (drm_fd > 0) {
        close(drm_fd);
    }

    avtext_print_section_footer(w); // SECTION_ID_DEVICE
    return va_status;
}

int DetectVaapi(AVTextFormatContext *w, enum VaApiType apiType) {
    VaAdapterInfo* adapters;
    int device_count;

    avtext_print_section_header(w, NULL, SECTION_ID_DEVICES);

    device_count = GetVaAdapterInfo(w, &adapters);

    for (int deviceIndex = 0; deviceIndex < device_count; deviceIndex++) {

        CheckDevice(w, deviceIndex, &adapters[deviceIndex], apiType);
    }

    avtext_print_section_footer(w); // SECTION_ID_DEVICES

    return 0;
}
