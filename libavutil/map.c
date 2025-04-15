/*
 * Copyright (c) 2025 Michael Niedermayer
 *
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

#include <inttypes.h>
#include <string.h>

#include "avassert.h"
#include "avstring.h"
#include "error.h"
#include "mem.h"
#include "map.h"

#include "tree_internal.h" // For improved readability with AVTreeNode, do NOT touch AVTreeNode internals

typedef struct{
    AVMapEntry map_entry;
    uint8_t treenode_and_keyvalue[0];
} AVMapInternalEntry;

struct AVMap{
    AVMapCompareFunc cmp_keyvalue;
    AVMapCopyFunc copy;
    AVMapFreeFunc freef;
    int count;
    int deleted;
    int next;                   ///< index of entry in root array after all used entries
    unsigned internal_entries_len;
    AVTreeNode *tree_root;
    AVMapInternalEntry *internal_entries;
};

static uint8_t deleted_entry;

static inline int internal_entry_len(AVMapInternalEntry *I) {
    return (I->map_entry.keylen + I->map_entry.valuelen + sizeof (*I) + sizeof(AVTreeNode) - 1) / sizeof (*I) + 1;
}

static inline AVTreeNode * internal_treenode(AVMapInternalEntry *I)
{
    return (AVTreeNode *)I->treenode_and_keyvalue;
}

static inline uint8_t * internal_key(AVMapInternalEntry *I)
{
    return I->treenode_and_keyvalue + sizeof(AVTreeNode);
}

static inline uint8_t * internal_value(AVMapInternalEntry *I)
{
    return I->treenode_and_keyvalue + sizeof(AVTreeNode) + I->map_entry.keylen;
}

static inline AVMapInternalEntry * keyvalue2internal(const uint8_t *keyvalue)
{
    return (AVMapInternalEntry*)(keyvalue - offsetof(AVMapInternalEntry, treenode_and_keyvalue) - sizeof(AVTreeNode));
}

int av_map_strcmp_keyvalue(const char *a, const char *b)
{
    int v = strcmp(a,b);
    if(!v)
        v = strcmp(a + strlen(a) + 1, b + strlen(a) + 1); // please optimize this dear compiler, we know the strlen after strcmp()
    return v;
}

int av_map_supercmp_key(const char *a, const char *b)
{
    int v = av_strcasecmp(a,b);
    if (!v)
        v = strcmp(a,b);

    return v;
}

int av_map_supercmp_keyvalue(const char *a, const char *b)
{
    int v = av_map_supercmp_key(a,b);
    if (!v)
        v = strcmp(a + strlen(a) + 1, b + strlen(a) + 1);

    return v;
}

AVMap *av_map_new(AVMapCompareFunc cmp_keyvalue, AVMapCopyFunc copy, AVMapFreeFunc freef)
{
    AVMap *s = av_mallocz(sizeof(*s));
    if (!s)
        return NULL;

    s->cmp_keyvalue  = cmp_keyvalue;
    s->copy          = copy;
    s->freef         = freef;

    return s;
}

const AVMapEntry *av_map_get_multiple(const AVMap *s, const AVMapEntry *prev, const char *keyvalue, int (*cmp)(const void *keyvalue, const void *b))
{
    if (prev) {
        void *next_node[2] = { NULL, NULL };
        void *prev_keyvalue = av_tree_find2(s->tree_root, prev->key, s->cmp_keyvalue, next_node, 2);
        av_assert2(prev_keyvalue);
        if (!next_node[1] || cmp(next_node[1], keyvalue))
            return NULL;

        keyvalue = next_node[1];
    } else {
        void *next_node[4] = { NULL, NULL, NULL, NULL };
        keyvalue = av_tree_find2(s->tree_root, keyvalue, cmp, next_node, 4);
        if (next_node[2]) // If we have a leftmost equal keyvalue, use it instead
            keyvalue = next_node[2];
    }

    if (!keyvalue)
        return NULL;

    return &keyvalue2internal(keyvalue)->map_entry;
}

const AVMapEntry *av_map_get(const AVMap *s, const char *keyvalue, int (*cmp)(const void *keyvalue, const void *b))
{
    keyvalue = av_tree_find2(s->tree_root, keyvalue, cmp, NULL, 0);

    if (!keyvalue)
        return NULL;

    return &keyvalue2internal(keyvalue)->map_entry;
}

int av_map_realloc(AVMap *s, int extra_elements, int extra_space) {
    int64_t advance = extra_elements + (extra_space + (int64_t)extra_elements*(sizeof(*s->internal_entries) + sizeof(AVTreeNode) - 1)) / sizeof(*s->internal_entries);

    if (advance > (INT32_MAX - s->next) / sizeof(AVMapInternalEntry))
        return AVERROR(ENOMEM);

    AVMapInternalEntry *new_root = av_fast_realloc(s->internal_entries, &s->internal_entries_len, (s->next + advance) * sizeof(AVMapInternalEntry));

    if (!new_root)
        return AVERROR(ENOMEM);

    if (new_root != s->internal_entries) {
        if (s->tree_root) {
            AVTreeNode *new_tree_root = s->tree_root - internal_treenode(s->internal_entries) + internal_treenode(new_root);
            av_tree_move(new_tree_root, s->tree_root, new_root, s->internal_entries);
            s->tree_root = new_tree_root;
        }

        for(int i = 0; i<s->next; i++) {
            if (new_root[i].map_entry.key != &deleted_entry) {
                new_root[i].map_entry.key   = internal_key  (new_root + i);
                new_root[i].map_entry.value = internal_value(new_root + i);
            }
            i += internal_entry_len(new_root + i) - 1;
        }
        s->internal_entries = new_root;
    }
    return advance;
}

int av_map_add(AVMap *s, const char *key, int keylen, const char *value, int valuelen, int flags)
{
    av_assert2(keylen || valuelen); // patch welcome but how would the compare function compare a len=0 element without knowing it is a len 0 element

    int advance = av_map_realloc(s, 1, keylen + valuelen);
    if (advance < 0)
        return advance;

    AVMapEntry *entry = &s->internal_entries[s->next].map_entry;
    AVTreeNode *next = internal_treenode(s->internal_entries + s->next);
    memset(next, 0, sizeof(*next));
    entry->keylen  = keylen;
    entry->valuelen= valuelen;
    entry->key     = internal_key  (s->internal_entries + s->next);
    entry->value   = internal_value(s->internal_entries + s->next);
    memcpy(entry->key  , key  , keylen);
    memcpy(entry->value, value, valuelen);

    void *elem = av_tree_insert(&s->tree_root, entry->key, s->cmp_keyvalue, &next);
    int ret = 1;
    if (elem != entry->key && elem) {
        av_assert2(next);
        //we assume that new entries are more common than replacements
        if (flags & AV_MAP_REPLACE) {
            ret = av_map_del(s, entry->key, s->cmp_keyvalue, flags);
            av_assert2(ret == 1);
            elem = av_tree_insert(&s->tree_root, entry->key, s->cmp_keyvalue, &next);
            av_assert2(elem == entry->key || !elem);
            ret = 2;
        } else
            return 0; //entry already in the map
    }
    av_assert2(!next);
    av_assert2(s->tree_root);
    s->next += advance;
    s->count++;

    return ret;
}

int av_map_del(AVMap *s, const char *keyvalue, int (*cmp)(const void *keyvalue, const void *b), int flags)
{
    uint8_t *old_keyvalue;
    AVTreeNode *next = NULL;

    if (cmp != s->cmp_keyvalue) {
        // The user asks us to remove a entry with a compare function different from the one used to build the map
        // we need to do 2 calls here, first with the users compare to find the entry she wants to remove
        // and then to remove it while maintaining the correct order within the map
        old_keyvalue = av_tree_find2(s->tree_root, keyvalue, cmp, NULL, 0);
        if (!old_keyvalue)
            return 0;

        av_tree_insert(&s->tree_root, old_keyvalue, s->cmp_keyvalue, &next);
        av_assert2(next);
    } else {
        av_tree_insert(&s->tree_root, (char*)keyvalue, s->cmp_keyvalue, &next);
        if (!next)
            return 0;
        old_keyvalue = next->elem; //TODO add a API to av_tree() to return the elem of a AVTreeNode

    }
    AVMapInternalEntry *internal_entry = keyvalue2internal(old_keyvalue);
    internal_entry->map_entry.key = &deleted_entry;

    s->count--;
    s->deleted++;

    if ((flags & AV_MAP_ALLOW_REBUILD) && s->deleted > s->count) {
        AVMap *news = av_map_new(s->cmp_keyvalue, s->copy, s->freef);
        if(news) {
            int ret = av_map_copy(news, s);
            if (ret < 0) {
                av_map_free(&news);
            } else {
                if (s->freef)
                    for (int i=0; i<s->count; i++)
                        s->freef(&s->internal_entries[i].map_entry);
                av_freep(&s->internal_entries);
                memcpy(s, news, sizeof(*s));
            }
        }
    }

    return 1;
}

const AVMapEntry *av_map_iterate(const AVMap *s,
                                 const AVMapEntry *prev)
{
    AVMapInternalEntry *I;
    if (prev) {
        I = (AVMapInternalEntry*)((uint8_t*)prev - offsetof(AVMapInternalEntry, map_entry));
        I += internal_entry_len(I);
    } else {
        I = s->internal_entries;
    }
    while (I < s->internal_entries + s->next && I->map_entry.key == &deleted_entry)
        I += internal_entry_len(I);

    if (I == s->internal_entries + s->next)
        return NULL;

    return &I->map_entry;
}

int av_map_count(const AVMap *s)
{
    return s->count;
}

void av_map_free(AVMap **sp)
{
    AVMap *s = *sp;

    for (int i=0; i<s->count; i++) {
        if (s->freef)
            s->freef(&s->internal_entries[i].map_entry);
    }
    av_freep(&s->internal_entries);
    s->next =
    s->internal_entries_len =
    s->count = 0;
    av_freep(sp);
}

int av_map_copy(AVMap *dst, const AVMap *src)
{
    const AVMapEntry *t = NULL;
    AVMap *bak = av_memdup(dst, sizeof(*dst));
    if (!bak)
        return AVERROR(ENOMEM);
    bak->internal_entries = av_memdup(bak->internal_entries, bak->internal_entries_len);

    while ((t = av_map_iterate(src, t))) {
        int ret = av_map_add(dst, t->key, t->keylen, t->value, t->valuelen, 0);

        if (ret < 0) {
            av_free(dst->internal_entries);
            memcpy(dst, bak, sizeof(*dst));
            return ret;
        }
    }
    av_freep(&bak->internal_entries);
    av_free(bak);

    return 0;
}

AVMap *av_map_clone(AVMap *s)
{
    AVMap *dst = av_memdup(s, sizeof(AVMap));

    if (!dst)
        return NULL;

    dst->internal_entries = av_memdup(s->internal_entries, s->internal_entries_len);

    if (!dst->internal_entries)
        goto err;

    if (s->tree_root) {
        dst->tree_root = s->tree_root - internal_treenode(s->internal_entries) + internal_treenode(dst->internal_entries);
        av_tree_move(dst->tree_root, s->tree_root, dst->internal_entries, s->internal_entries);
    }

    //TODO We could attempt to compact free space
    for(int i = 0; i<s->next; i++) {
        if (dst->internal_entries[i].map_entry.key != &deleted_entry) {
            dst->internal_entries[i].map_entry.key   = internal_key  (dst->internal_entries + i);
            dst->internal_entries[i].map_entry.value = internal_value(dst->internal_entries + i);
        }
        i += internal_entry_len(dst->internal_entries + i) - 1;
    }

    return dst;
err:
    if (dst) {
        av_freep(&dst->internal_entries);
    }
    av_free(dst);
    return NULL;
}
