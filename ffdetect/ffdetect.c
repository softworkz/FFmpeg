/*
 * Copyright (C) 2018 - softworkz for Emby Llc.
 * All rights reserved.
 *
 * This code is not yet published under any license.
 *
 */

 /**
  * @file
  * simple hardware device and capabilities detector
  */

#include "config.h"
#include "libavutil/ffversion.h"

#include <string.h>

#include "libavutil/opt.h"

#if CONFIG_CUVID
#include "ffdetect_nv.h"
#endif

#if CONFIG_LIBMFX
#include "ffdetect_qsv.h"
#endif

#if CONFIG_AMF
#include "ffdetect_amf.h"
#endif

#if CONFIG_VAAPI && HAVE_VAAPI_DRM
#include "ffdetect_vaapi.h"
#endif

#include "detectutils.h"
#include "outputwriters.h"

#include "libavutil/thread.h"

#if !HAVE_THREADS
#  ifdef pthread_mutex_lock
#    undef pthread_mutex_lock
#  endif
#  define pthread_mutex_lock(a) do{}while(0)
#  ifdef pthread_mutex_unlock
#    undef pthread_mutex_unlock
#  endif
#  define pthread_mutex_unlock(a) do{}while(0)
#endif

const char program_name[] = "ffdetect";
const int program_birth_year = 2018;

static int do_show_log = 0;
static int do_show_error = 1;
static int disable_dx11 = 0;
static int hide_banner = 0;

static int do_show_program_version = 0;

static char *print_format;

static const OptionDef *options;

static const char *hw_api;

#if HAVE_THREADS
pthread_mutex_t log_mutex;
#endif

typedef struct LogBuffer {
    char *context_name;
    int log_level;
    char *log_message;
    AVClassCategory category;
    char *parent_name;
    AVClassCategory parent_category;
}LogBuffer;

static LogBuffer *log_buffer;
static int log_buffer_size;

static int detect_api(AVTextFormatContext *wctx, const char *hwapi)
{
    int ret = 0;

    if (0) {
        ;
    }

#if CONFIG_CUVID
    else if (!strcmp(hwapi, "nvdec")) {
        ret = DetectNV(wctx, NV_API_NVDEC);
    }
    else if (!strcmp(hwapi, "nvenc")) {
        ret = DetectNV(wctx, NV_API_NVENC);
    }
    else if (!strcmp(hwapi, "nvencdec")) {
        ret = DetectNV(wctx, NV_API_NVENC | NV_API_NVDEC);
    }
#endif

#if CONFIG_LIBMFX
    else if (!strcmp(hwapi, "qsvdec")) {
        ret = DetectQsv(wctx, QSV_API_QSVDEC, disable_dx11);
    }
    else if (!strcmp(hwapi, "qsvenc")) {
        ret = DetectQsv(wctx, QSV_API_QSVENC, disable_dx11);
    }
    else if (!strcmp(hwapi, "qsvencdec")) {
        ret = DetectQsv(wctx, QSV_API_QSVENC | QSV_API_QSVDEC, disable_dx11);
    }
#endif

#if CONFIG_AMF
    else if (!strcmp(hwapi, "amfenc")) {
        ret = DetectAmf(wctx, AMF_API_AMFENC, disable_dx11);
    }
#endif

#if CONFIG_VAAPI && HAVE_VAAPI_DRM
    else if (!strcmp(hwapi, "vadec")) {
        ret = DetectVaapi(wctx, VAAPI_ENC);
    }
    else if (!strcmp(hwapi, "vaenc")) {
        ret = DetectVaapi(wctx, VAAPI_DEC);
    }
    else if (!strcmp(hwapi, "vaencdec")) {
        ret = DetectVaapi(wctx, VAAPI_ENC | VAAPI_DEC);
    }
#endif

    else {
        av_log(NULL, AV_LOG_ERROR, "Unknown or unsupported hardware api type '%s'\n", hwapi);
        ret = AVERROR(EINVAL);
    }

    return ret;
}

static void log_callback(void *ptr, int level, const char *fmt, va_list vl)
{
    AVClass* avc = ptr ? *(AVClass **)ptr : NULL;
    va_list vl2;
    char line[1024];
    static int print_prefix = 1;
    void *new_log_buffer;

    va_copy(vl2, vl);
    av_log_default_callback(ptr, level, fmt, vl);
    av_log_format_line(ptr, level, fmt, vl2, line, sizeof(line), &print_prefix);
    va_end(vl2);

#if HAVE_THREADS
    pthread_mutex_lock(&log_mutex);

    new_log_buffer = av_realloc_array(log_buffer, log_buffer_size + 1, sizeof(*log_buffer));
    if (new_log_buffer) {
        char *msg;
        int i;

        log_buffer = new_log_buffer;
        memset(&log_buffer[log_buffer_size], 0, sizeof(log_buffer[log_buffer_size]));
        log_buffer[log_buffer_size].context_name = avc ? av_strdup(avc->item_name(ptr)) : NULL;
        if (avc) {
            if (avc->get_category) log_buffer[log_buffer_size].category = avc->get_category(ptr);
            else                   log_buffer[log_buffer_size].category = avc->category;
        }
        log_buffer[log_buffer_size].log_level = level;
        msg = log_buffer[log_buffer_size].log_message = av_strdup(line);
        for (i = strlen(msg) - 1; i >= 0 && msg[i] == '\n'; i--) {
            msg[i] = 0;
        }
        if (avc && avc->parent_log_context_offset) {
            AVClass** parent = *(AVClass ***)(((uint8_t *)ptr) +
                avc->parent_log_context_offset);
            if (parent && *parent) {
                log_buffer[log_buffer_size].parent_name = av_strdup((*parent)->item_name(parent));
                log_buffer[log_buffer_size].parent_category =
                    (*parent)->get_category ? (*parent)->get_category(parent) : (*parent)->category;
            }
        }
        log_buffer_size++;
    }

    pthread_mutex_unlock(&log_mutex);
#endif
}

static void ffdetect_cleanup(int ret)
{
#if HAVE_THREADS
    pthread_mutex_destroy(&log_mutex);
#endif
}


static void clear_log(void)
{
    int i;

    for (i = 0; i < log_buffer_size; i++) {
        av_freep(&log_buffer[i].context_name);
        av_freep(&log_buffer[i].parent_name);
        av_freep(&log_buffer[i].log_message);
    }
    log_buffer_size = 0;
}

static int show_log(AVTextFormatContext *w, int section_ids, int section_id, int log_level)
{
    int i;
    avtext_print_section_header(w, NULL, section_ids);

    for (i = 0; i < log_buffer_size; i++) {
        if (log_buffer[i].log_level <= log_level) {
            avtext_print_section_header(w, NULL, section_id);
            print_str("Context", log_buffer[i].context_name);
            print_int("Level", log_buffer[i].log_level);
            print_int("Category", log_buffer[i].category);
            if (log_buffer[i].parent_name) {
                print_str("ParentContext", log_buffer[i].parent_name);
                print_int("ParentCategory", log_buffer[i].parent_category);
            }
            else {
                print_str_opt("ParentContext", "N/A");
                print_str_opt("ParentCategory", "N/A");
            }
            print_str("Message", log_buffer[i].log_message);
            avtext_print_section_footer(w);
        }
    }
    clear_log();

    avtext_print_section_footer(w);

    return 0;
}

static void show_usage(void)
{
    const char* availableApis = ""
#if CONFIG_CUVID
        "nvenc nvdec nvencdec "
#endif
#if CONFIG_LIBMFX
        "qsvenc qsvdec qsvencdec "
#endif
#if CONFIG_AMF
        "amfenc "
#endif
#if CONFIG_VAAPI && HAVE_VAAPI_DRM
        "vaenc vadec vaencdec "
#endif
        "";

    av_log(NULL, AV_LOG_INFO, "\nHardware capabilities detector\n");
    av_log(NULL, AV_LOG_INFO, "usage: %s [OPTIONS] [HW_API]\n", program_name);
    av_log(NULL, AV_LOG_INFO, "\nHW_API can be one of: %s\n", availableApis);
    av_log(NULL, AV_LOG_INFO, "\n");
}

static void ffdetect_show_program_version(AVTextFormatContext *w)
{
    AVBPrint pbuf;
    av_bprint_init(&pbuf, 1, AV_BPRINT_SIZE_UNLIMITED);

    avtext_print_section_header(w, NULL, SECTION_ID_PROGRAM_VERSION);
    print_str("Version", FFMPEG_VERSION);
    print_fmt("Copyright", "Copyright (c) %d-%d softworkz for Emby Llc",
        program_birth_year, CONFIG_THIS_YEAR);
    print_str("Compiler", CC_IDENT);
    print_str("Configuration", FFMPEG_CONFIGURATION);
    avtext_print_section_footer(w);

    av_bprint_finalize(&pbuf, NULL);
}

void show_help_default(const char *opt, const char *arg)
{
    av_log_set_callback(log_callback_help);
    show_usage();
    show_help_options(options, "Main options:", 0, 0, 0);
    printf("\n");
}

static void opt_hw_api(void *optctx, const char *arg)
{
    if (hw_api) {
        av_log(NULL, AV_LOG_ERROR,
            "Argument '%s' provided as hardware api, but '%s' was already specified.\n",
            arg, hw_api);
        exit_program(1);
    }

    hw_api = arg;
}

static const OptionDef real_options[] = {
    CMDUTILS_COMMON_OPTIONS
    { "hide_banner", OPT_BOOL | OPT_EXPERT, {&hide_banner},     "do not show program banner", "hide_banner" },
    { "print_format", OPT_STRING | HAS_ARG, {(void*)&print_format},
    "set the output printing format (available formats are: default, json)", "format" },
    { "show_program_version",  OPT_BOOL, {&do_show_program_version},  "show ffdetect version", NULL },
    { "show_error",            OPT_BOOL, {&do_show_error},  "show detection errors", NULL },
#if HAVE_THREADS
    { "show_log", OPT_INT | HAS_ARG, {(void*)&do_show_log}, "show log", NULL },
#endif
    { "disable_dx11", OPT_BOOL, {&disable_dx11},  "do not use DirectX 11 for detection", NULL },
   { NULL, 0,{ NULL } , NULL, NULL }
};

int main(int argc, char **argv)
{
    const AVTextFormatter *formatter;
    AVTextFormatContext *tctx;
    AVTextWriterContext *wctx;
    char *buf;
    char *w_name = NULL, *w_args = NULL;
    int ret;

    init_dynload();

#if HAVE_THREADS
    ret = pthread_mutex_init(&log_mutex, NULL);
    if (ret != 0) {
        goto end;
    }
#endif

    setvbuf(stderr,NULL,_IONBF,0); /* win32 runtime needs this */

    register_exit(ffdetect_cleanup);

    options = real_options;
    parse_loglevel(argc, argv, options);
    init_opts();

    if (!hide_banner) {
        show_banner(argc, argv, options);
    }

    parse_options(NULL, argc, argv, options, opt_hw_api);

    if (do_show_log)
        av_log_set_callback(log_callback);

    if (!print_format)
        print_format = av_strdup("default");
    if (!print_format) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    w_name = av_strtok(print_format, "=", &buf);
    if (!w_name) {
        av_log(NULL, AV_LOG_ERROR, "No name specified for the output format\n");
        ret = AVERROR(EINVAL);
        goto end;
    }
    w_args = buf;

    formatter = avtext_get_formatter_by_name(w_name);
    if (!formatter) {
        av_log(NULL, AV_LOG_ERROR, "Unknown output format with name '%s'\n", w_name);
        ret = AVERROR(EINVAL);
        goto end;
    }

    ret = avtextwriter_create_stdout(&wctx);
    if (ret < 0)
        goto end;

    {
        AVTextFormatOptions tf_options = { .show_optional_fields = 1 };

        if ((ret = avtext_context_open(&tctx, formatter, wctx, w_args,
                sections, FF_ARRAY_ELEMS(sections), tf_options, NULL)) >= 0) {
            avtext_print_section_header(tctx, NULL, SECTION_ID_ROOT);

            if (do_show_program_version)
                ffdetect_show_program_version(tctx);

            if (!hw_api && !do_show_program_version) {
                show_usage();
                av_log(NULL, AV_LOG_ERROR, "You have to specify a hardware api.\n");
                av_log(NULL, AV_LOG_ERROR, "Use -h to get full help.\n");
                ret = AVERROR(EINVAL);
            }
            else if (hw_api) {
                ret = detect_api(tctx, hw_api);
                if (ret < 0 && do_show_error)
                    write_error(tctx, ret);
            }

            if (do_show_log)
                show_log(tctx, SECTION_ID_LOGS, SECTION_ID_LOG, do_show_log);

            avtext_print_section_footer(tctx);
            avtext_context_close(&tctx);
        }

        avtextwriter_context_close(&wctx);
    }

end:
    av_freep(&print_format);

    uninit_opts();

    return ret < 0;
}
