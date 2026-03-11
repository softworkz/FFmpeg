/*
 * Copyright (C) 2018 - softworkz for Emby Llc.
 * All rights reserved.
 *
 * This code is not yet published under any license.
 *
 */


#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "device_lookup.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"

struct hash_bucket {
    struct hash_bucket *next;
    unsigned int full;
};

#ifdef __GNUC__
#define BUCKET_ALIGNMENT __alignof__(struct hash_bucket)
#else
union record_align {
    struct hash_bucket *next;
    unsigned int full;
};
#define BUCKET_ALIGNMENT sizeof(union record_align)
#endif
#define BUCKET_ALIGN(n) ((n)+BUCKET_ALIGNMENT-(n)%BUCKET_ALIGNMENT)

struct device_record **record_set;
struct hash_bucket *current_hash_bucket;

static int dbLoadStatus;

static struct {
    int idx;
    const char* path;
} db_paths[] = {
    {0, "./pci.ids"},
    {1, "/usr/share/misc/pci.ids"},
    {2, "/usr/share/hwdata/pci.ids"},
    {3, "/var/lib/pciutils/pci.ids"},
    {4, "/usr/share/pci.ids"},
};

static void* alloc_record(unsigned int size)
{
    struct hash_bucket *buck = current_hash_bucket;
    unsigned int pos;

    if (!record_set) {
        record_set = av_mallocz(sizeof(struct device_record *) * HASH_LENGTH);
        memset(record_set, 0, sizeof(struct device_record *) * HASH_LENGTH);
    }

    if (!buck || buck->full + size > BUCKET_LENGTH) {
        buck = av_mallocz(BUCKET_LENGTH);
        buck->next = current_hash_bucket;
        current_hash_bucket = buck;
        buck->full = BUCKET_ALIGN(sizeof(struct hash_bucket));
    }
    pos = buck->full;
    buck->full = BUCKET_ALIGN(buck->full + size);
    return (uint8_t *)buck + pos;
}

static inline uint32_t create_tuple(unsigned int x, unsigned int y)
{
    return ((x << 16) | y);
}

static inline unsigned int create_hash(int type, uint32_t tuple1, uint32_t tuple2)
{
    unsigned int h;

    h = tuple1 ^ (tuple2 << 3) ^ (type << 5);
    return h % HASH_LENGTH;
}

static int insert_record(int type, int id1, int id2, int id3, int id4, char *text)
{
    uint32_t tuple1 = create_tuple(id1, id2);
    uint32_t tuple2 = create_tuple(id3, id4);

    unsigned int h = create_hash(type, tuple1, tuple2);

    struct device_record *n = record_set ? record_set[h] : NULL;
    int len = strlen(text);

    while (n && (n->tuple1 != tuple1 || n->tuple2 != tuple2 || n->type != type))
        n = n->next;
    if (n)
        return 1;
    n = alloc_record(sizeof(struct device_record) + len);
    n->tuple1 = tuple1;
    n->tuple2 = tuple2;
    n->type = type;
    memcpy(n->name, text, len + 1);
    n->next = record_set[h];
    record_set[h] = n;
    return 0;
}

static char* find_record(enum record_type recordType, int id1, int id2, int id3, int id4)
{
    struct device_record *n, *result;
    uint32_t tuple1 = create_tuple(id1, id2);
    uint32_t tuple2 = create_tuple(id3, id4);

    if (record_set) {
        n = record_set[create_hash(recordType, tuple1, tuple2)];
        result = NULL;
        for (; n; n = n->next) {
            if (n->tuple1 != tuple1 || n->tuple2 != tuple2 || n->type != recordType)
                continue;
            if (!result)
                result = n;
        }

        if (result) {
            av_log(NULL, AV_LOG_TRACE, "find_record: found: %s\n", result->name);
            return result->name;
        }

        av_log(NULL, AV_LOG_TRACE, "find_record: no record found\n");
    }
    else {
        av_log(NULL, AV_LOG_WARNING, "find_record record_set not set up\n");
    }

    return NULL;
}

static inline int is_whitespace(int c)
{
    return (c == ' ') || (c == '\t');
}

static int parse_hex(char *p, int cnt)
{
    int x = 0;
    while (cnt--) {
        x <<= 4;
        if (*p >= '0' && *p <= '9')
            x += (*p - '0');
        else if (*p >= 'a' && *p <= 'f')
            x += (*p - 'a' + 10);
        else if (*p >= 'A' && *p <= 'F')
            x += (*p - 'A' + 10);
        else
            return -1;
        p++;
    }
    return x;
}

static const char *read_file(FILE* f, int *lino)
{
    char line[1024];
    char *p;
    int id1 = 0, id2 = 0, id3 = 0, id4 = 0;
    int type = -1;
    int nest;
    //static const char parse_error[] = "Error parsing file";

    *lino = 0;
    while (fgets(line, sizeof(line), f)) {
        (*lino)++;
        p = line;
        while (*p && *p != '\n' && *p != '\r')
            p++;
        if (!*p && !feof(f))
            return "Line too long";
        *p = 0;
        if (p > line && (p[-1] == ' ' || p[-1] == '\t'))
            *--p = 0;

        p = line;
        while (is_whitespace(*p))
            p++;
        if (!*p || *p == '#')
            continue;

        p = line;
        while (*p == '\t')
            p++;
        nest = p - line;

        if (!nest) {
            if (p[0] == 'C' && p[1] == ' ') {
                if ((id1 = parse_hex(p + 2, 2)) < 0 || !is_whitespace(p[4]))
                    return "parse_error 1";
                type = ID_CLASS;
                p += 5;
            }
            else if (p[0] == 'S' && p[1] == ' ') {
                if ((id1 = parse_hex(p + 2, 4)) < 0 || p[6])
                    return "parse_error 2";
                type = ID_GEN_SUBSYSTEM;
                continue;
            }
            else if (p[0] >= 'A' && p[0] <= 'Z' && p[1] == ' ') {
                type = ID_UNKNOWN;
                continue;
            }
            else {
                if ((id1 = parse_hex(p, 4)) < 0 || !is_whitespace(p[4]))
                    return "parse_error 3";
                type = ID_VENDOR;
                p += 5;
            }
            id2 = id3 = id4 = 0;
        }
        else if (type == ID_UNKNOWN)
            continue;
        else if (nest == 1)
            switch (type) {
            case ID_VENDOR:
            case ID_DEVICE:
            case ID_SUBSYSTEM:
                if ((id2 = parse_hex(p, 4)) < 0 || !is_whitespace(p[4]))
                    return "parse_error 4";
                p += 5;
                type = ID_DEVICE;
                id3 = id4 = 0;
                break;
            case ID_GEN_SUBSYSTEM:
                if ((id2 = parse_hex(p, 4)) < 0 || !is_whitespace(p[4]))
                    return "parse_error 5";
                p += 5;
                id3 = id4 = 0;
                break;
            case ID_CLASS:
                if ((id2 = parse_hex(p, 2)) < 0 || !is_whitespace(p[2]))
                    return "parse_error 6";
                p += 3;
                id3 = id4 = 0;
                break;
            default:
                return "parse_error 7";
            }
        else if (nest == 2)
            switch (type) {
            case ID_DEVICE:
            case ID_SUBSYSTEM:
                if ((id3 = parse_hex(p, 4)) < 0 || !is_whitespace(p[4]) || (id4 = parse_hex(p + 5, 4)) < 0 || !is_whitespace(p[9]))
                    return "parse_error 8";
                p += 10;
                type = ID_SUBSYSTEM;
                break;
            case ID_CLASS:
                if ((id3 = parse_hex(p, 2)) < 0 || !is_whitespace(p[2]))
                    return "parse_error 9";
                p += 3;
                id4 = 0;
                break;
            default:
                return "parse_error 10";
            }
        else
            return "parse_error 11";
        while (is_whitespace(*p))
            p++;
        if (!*p)
            return "parse_error 12";

        insert_record(type, id1, id2, id3, id4, p);
        //if (insert_record(type, id1, id2, id3, id4, p))
        //    return "Entry already exsists";
    }
    return NULL;
}

static void free_all_records(void)
{
    if (record_set) {
        free(record_set);
        record_set = NULL;
    }

    while (current_hash_bucket) {
        struct hash_bucket *buck = current_hash_bucket;
        current_hash_bucket = buck->next;
        free(buck);
    }
}

static int load_from_file(void)
{
    FILE* f = NULL;
    int lino, i;
    const char *err;
    char* env_path = NULL;

    free_all_records();

    env_path =  getenv("PCI_IDS_PATH");

    if (env_path) {
        f = fopen(env_path, "r");
    }

    for (i = 0; i < FF_ARRAY_ELEMS(db_paths) && f == NULL; i++) {
        f = fopen(db_paths[i].path, "r");
    }

    if (!f) {
        av_log(NULL, AV_LOG_WARNING, "GetDeviceName: unable to open pci.ids file \n");
        return -1;
    }

    err = read_file(f, &lino);

    //if (!err && ferror(f))
    //    err = "Read error";

    fclose(f);

    if (err) {
        av_log(NULL, AV_LOG_WARNING, "GetDeviceName: unable to parse file: %s\n", err);
        return -2;
    }

    return 0;
}

static const char* format_name(char *buf, int size, int flags, char *name, char *num, const char *unknown)
{
    int res;

    if (!name)
        return NULL;
    else if (!name)
        res = snprintf(buf, size, "%s %s", unknown, num);
    else
        res = snprintf(buf, size, "%s", name);

    if (res >= size && size >= 4)
        buf[size - 2] = buf[size - 3] = buf[size - 4] = '.';
    else if (res < 0 || res >= size)
        return "[buffer too small]";
    return buf;
}

static char* id_lookup_subsys(int flags, int iv, int id, int isv, int isd)
{
    char *d = NULL;
    if (iv > 0 && id > 0)						/* Per-device lookup */
        d = find_record(ID_SUBSYSTEM, iv, id, isv, isd);
    if (!d)							/* Generic lookup */
        d = find_record(ID_GEN_SUBSYSTEM, isv, isd, 0, 0);
    if (!d && iv == isv && id == isd)				/* Check for subsystem == device */
        d = find_record(ID_DEVICE, iv, id, 0, 0);
    return d;
}

const char* GetDeviceName(char *buf, int size, enum record_type recordType, ...)
{
    va_list args;
    int iv, id, isv, isd;
    char numbuf[16];
    int res;

    if (dbLoadStatus == -1) {
        // previous load attempt has already failed
        return NULL;
    }

    if (dbLoadStatus == 0) {

        // No previous load attempt, try load
        res = load_from_file();
        if (res != 0) {
            dbLoadStatus = -1;
            av_log(NULL, AV_LOG_WARNING, "GetDeviceName unable to load pci.ids file\n");
            return NULL;
        }

        dbLoadStatus = 1;
    }

    av_log(NULL, AV_LOG_TRACE, "GetDeviceName - Start\n");

    va_start(args, recordType);

    switch (recordType) {
    case ID_VENDOR:
        iv = va_arg(args, int);
        sprintf(numbuf, "%04x", iv);
        va_end(args);
        return format_name(buf, size, recordType, find_record(ID_VENDOR, iv, 0, 0, 0), numbuf, "Vendor");
    case ID_DEVICE:
        iv = va_arg(args, int);
        id = va_arg(args, int);
        sprintf(numbuf, "%04x", id);
        va_end(args);
        return format_name(buf, size, recordType, find_record(ID_DEVICE, iv, id, 0, 0), numbuf, "Device");
    case ID_SUBSYSTEM:
        iv = va_arg(args, int);
        id = va_arg(args, int);
        isv = va_arg(args, int);
        isd = va_arg(args, int);
        sprintf(numbuf, "%04x", isd);
        va_end(args);
    return format_name(buf, size, recordType, id_lookup_subsys(ID_SUBSYSTEM, iv, id, isv, isd), numbuf, "Device");    default:
        va_end(args);
        return NULL;
        //return "<GetDeviceName: invalid request>";
    }
}