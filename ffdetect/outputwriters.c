/*
 * Copyright (C) 2018 - softworkz for Emby Llc.
 * All rights reserved.
 *
 * This code is not yet published under any license.
 *
 */

#include "config.h"

#include <string.h>
#include <stdarg.h>

#include "libavutil/avstring.h"
#include "libavutil/error.h"
#include "outputwriters.h"

void write_error(AVTextFormatContext *w, int err)
{
    char errbuf[128];
    const char *errbuf_ptr = errbuf;

    if (av_strerror(err, errbuf, sizeof(errbuf)) < 0)
        errbuf_ptr = strerror(AVUNERROR(err));

    avtext_print_section_header(w, NULL, SECTION_ID_ERROR);
    print_int("Number", err);
    print_str("Message", errbuf_ptr);
    avtext_print_section_footer(w);
}

void write_error_msg(AVTextFormatContext *w, int err, const char *msg)
{
    avtext_print_section_header(w, NULL, SECTION_ID_ERROR);
    print_int("Number", err);
    print_str("Message", msg);
    avtext_print_section_footer(w);
}

void write_error_fmt(AVTextFormatContext *w, int err, const char *fmt, ...)
{
    AVBPrint pbuf;
    va_list vl;
    va_start(vl, fmt);

    avtext_print_section_header(w, NULL, SECTION_ID_ERROR);
    print_int("Number", err);

    av_bprint_init(&pbuf, 1, AV_BPRINT_SIZE_UNLIMITED);
    av_vbprintf(&pbuf, fmt, vl);
    va_end(vl);

    print_str("Message", pbuf.str);
    av_bprint_finalize(&pbuf, NULL);

    avtext_print_section_footer(w);
}
