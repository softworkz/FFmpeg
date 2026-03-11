/*
 * Copyright (C) 2018 - softworkz for Emby Llc.
 * All rights reserved.
 *
 * This code is not yet published under any license.
 *
 */

#ifndef FFDETECT_OUTPUTWRITERS_H
#define FFDETECT_OUTPUTWRITERS_H

#include <stdint.h>
#include <stdarg.h>

#include "config.h"
#include "libavutil/avutil.h"
#include "libavutil/bprint.h"
#include "libavutil/avstring.h"
#include "fftools/textformat/avtextformat.h"

#ifndef GUID_DEFINED
#define GUID_DEFINED
typedef struct _GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[ 8 ];
} GUID;

#define IsEqualGUID(rguid1, rguid2) (!memcmp(rguid1, rguid2, sizeof(GUID)))

#endif

typedef enum {
    SECTION_ID_NONE = -1,
    SECTION_ID_ROOT,
    SECTION_ID_PROGRAM_VERSION,
    SECTION_ID_DEVICES,
    SECTION_ID_DEVICE,
    SECTION_ID_DEVICEINFODX11,
    SECTION_ID_DEVICEINFO,
    SECTION_ID_DECODERS,
    SECTION_ID_DECODER,
    SECTION_ID_ENCODERS,
    SECTION_ID_ENCODER,
    SECTION_ID_PRESETS,
    SECTION_ID_PRESET,
    SECTION_ID_PROFILES,
    SECTION_ID_PROFILE,
    SECTION_ID_FILTERS,
    SECTION_ID_FILTER,

    SECTION_ID_PROPERTIES,
    SECTION_ID_PROPERTY,

    SECTION_ID_ERROR,
    SECTION_ID_LOG,
    SECTION_ID_LOGS,

} SectionID;

#define SECTION_ENTRY(_id, _name, _flags, ...) \
    { .id = _id, .name = _name, .flags = _flags, .children_ids = __VA_ARGS__, .show_all_entries = 1 }

static const AVTextFormatSection sections[] = {
    [SECTION_ID_ROOT] =               SECTION_ENTRY(SECTION_ID_ROOT, "DetectResult", AV_TEXTFORMAT_SECTION_FLAG_IS_WRAPPER,
                                        { SECTION_ID_ERROR, SECTION_ID_PROGRAM_VERSION, SECTION_ID_DEVICES, SECTION_ID_LOGS, -1}),
    [SECTION_ID_PROGRAM_VERSION] =    SECTION_ENTRY(SECTION_ID_PROGRAM_VERSION, "ProgramVersion", 0, { -1 }),

    [SECTION_ID_DEVICES] =            SECTION_ENTRY(SECTION_ID_DEVICES, "Devices", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_DEVICE, -1 }),
    [SECTION_ID_DEVICE] =             SECTION_ENTRY(SECTION_ID_DEVICE, "Device", 0, { SECTION_ID_DEVICEINFO, SECTION_ID_DEVICEINFODX11, SECTION_ID_DECODERS, SECTION_ID_ENCODERS, SECTION_ID_ERROR, -1 }),
    [SECTION_ID_DEVICEINFO] =         SECTION_ENTRY(SECTION_ID_DEVICE, "DeviceInfo", 0, { SECTION_ID_PROPERTIES, SECTION_ID_ERROR, -1 }),
    [SECTION_ID_DEVICEINFODX11] =     SECTION_ENTRY(SECTION_ID_DEVICE, "DeviceInfoDx11", 0, { SECTION_ID_PROPERTIES, SECTION_ID_ERROR, -1 }),

    [SECTION_ID_DECODERS] =           SECTION_ENTRY(SECTION_ID_DECODERS, "Decoders", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_DECODER, SECTION_ID_ERROR, -1 }),
    [SECTION_ID_DECODER] =            SECTION_ENTRY(SECTION_ID_DECODER, "Decoder", 0, { SECTION_ID_PROFILES, SECTION_ID_ERROR, -1 }),

    [SECTION_ID_ENCODERS] =           SECTION_ENTRY(SECTION_ID_ENCODERS, "Encoders", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_ENCODER, SECTION_ID_ERROR, -1 }),
    [SECTION_ID_ENCODER] =            SECTION_ENTRY(SECTION_ID_ENCODER, "Encoder", 0, { SECTION_ID_PRESETS, SECTION_ID_PROFILES, SECTION_ID_ERROR, -1 }),

    [SECTION_ID_FILTERS] =            SECTION_ENTRY(SECTION_ID_FILTERS, "Filters", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_FILTER, SECTION_ID_ERROR, -1 }),
    [SECTION_ID_FILTER] =             SECTION_ENTRY(SECTION_ID_FILTER, "Filter", 0, { SECTION_ID_PROPERTIES, -1 }),

    [SECTION_ID_PRESETS] =            SECTION_ENTRY(SECTION_ID_PRESETS, "Presets", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_PRESET, -1 }),
    [SECTION_ID_PRESET] =             SECTION_ENTRY(SECTION_ID_PRESET, "Preset", 0, { SECTION_ID_ERROR, -1 }),

    [SECTION_ID_PROFILES] =           SECTION_ENTRY(SECTION_ID_PROFILES, "Profiles", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_PROFILE, -1 }),
    [SECTION_ID_PROFILE] =            SECTION_ENTRY(SECTION_ID_PROFILE, "Profile", 0, { -1 }),

    [SECTION_ID_PROPERTIES] =         SECTION_ENTRY(SECTION_ID_PROPERTIES, "Properties", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_PROPERTY, -1 }),
    [SECTION_ID_PROPERTY] =           SECTION_ENTRY(SECTION_ID_PROPERTY, "Property", 0, { -1 }),

    [SECTION_ID_ERROR] =              SECTION_ENTRY(SECTION_ID_ERROR, "Error", 0, { -1 }),
    [SECTION_ID_LOGS] =               SECTION_ENTRY(SECTION_ID_LOGS, "Log", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_LOG, -1 }),
    [SECTION_ID_LOG] =                SECTION_ENTRY(SECTION_ID_LOG, "LogEntry", 0, { -1 }),
};

#define print_fmt(k, f, ...) do {              \
    av_bprint_clear(&pbuf);                    \
    av_bprintf(&pbuf, f, __VA_ARGS__);         \
    avtext_print_string(w, k, pbuf.str, 0);    \
} while (0)

#define print_int(k, v)         avtext_print_integer(w, k, v, 0)
#define print_q(k, v, s)        avtext_print_rational(w, k, v, s)
#define print_str(k, v)         avtext_print_string(w, k, v, 0)
#define print_str_opt(k, v)     avtext_print_string(w, k, v, AV_TEXTFORMAT_PRINT_STRING_OPTIONAL)
#define print_str_validate(k, v) avtext_print_string(w, k, v, AV_TEXTFORMAT_PRINT_STRING_VALIDATE)
#define print_time(k, v, tb)    avtext_print_time(w, k, v, tb, 0)
#define print_ts(k, v)          avtext_print_ts(w, k, v, 0)
#define print_duration_time(k, v, tb) avtext_print_time(w, k, v, tb, 1)
#define print_duration_ts(k, v)       avtext_print_ts(w, k, v, 1)
#define print_val(k, v, u)      avtext_print_unit_integer(w, k, v, u)

static inline void avtext_print_double(AVTextFormatContext *tctx, const char *key, double val)
{
    AVBPrint buf;
    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprintf(&buf, "%.3f", val);
    avtext_print_string(tctx, key, buf.str, 0);
    av_bprint_finalize(&buf, NULL);
}

#define print_double(k, v)      avtext_print_double(w, k, v)

static inline void avtext_print_guid(AVTextFormatContext *tctx, const char *key, GUID *guid)
{
    AVBPrint buf;
    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_AUTOMATIC);
    av_bprintf(&buf, "{%8.8x-%4.4x-%4.4x-%2.2x%2.2x-%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x}",
             (unsigned) guid->Data1, guid->Data2, guid->Data3,
             guid->Data4[0], guid->Data4[1],
             guid->Data4[2], guid->Data4[3],
             guid->Data4[4], guid->Data4[5],
             guid->Data4[6], guid->Data4[7]);
    avtext_print_string(tctx, key, buf.str, 0);
    av_bprint_finalize(&buf, NULL);
}

#define print_guid(k, v)        avtext_print_guid(w, k, v)

void write_error(AVTextFormatContext *w, int err);
void write_error_msg(AVTextFormatContext *w, int err, const char *msg);
void write_error_fmt(AVTextFormatContext *w, int err, const char *fmt, ...);

#endif /* FFDETECT_OUTPUTWRITERS_H */
