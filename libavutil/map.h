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

/**
 * @file
 * Public map API.
 */

#ifndef AVUTIL_MAP_H
#define AVUTIL_MAP_H

#include <stdint.h>

#include "tree.h"

/**
 * compared to AVDictionary this has
 * clone is O(n) instead of O(n²)
 * copy is O(n*log n) instead of O(n²)
 * O(log n) malloc() calls by default and O(1) if av_map_realloc() is used instead of O(n)
 * get/add/delete is O(log n)
 *
 * You can add (if memory is realloced before) and remove entries while a iterator stays valid
 * copy is atomic, a failure means the dst is unchanged
 *
 * there are restrictions on what compare function can be used on get depending on how the Map was created
 * you can mix case sensitive and case insensitive compare with av_map_supercmp_*
 * Supports binary objects, not just strings
 */

enum {
    AV_MAP_ALLOW_REBUILD  = 1,   ///< when removing entries rebuild the map to reduce memory consumption, note, this invalidates previously retrieved elements and iterate state.
    AV_MAP_REPLACE        = 2,   ///< replace keyvalue if already in the map
};

typedef struct AVMapEntry {
    uint8_t *key;
    uint8_t *value;
    int keylen;
    int valuelen;
} AVMapEntry;

typedef struct AVMap AVMap;
typedef void (* AVMapFreeFunc)(AVMapEntry *c);
typedef void (* AVMapCopyFunc)(AVMapEntry *dst, const AVMapEntry *src, size_t len);
typedef int  (* AVMapCompareFunc)(const void *keyvalue, const void *b);

/**
 * like strcmp() but compares concatenated keyvalues.
 *
 * A map initialized with this will allow duplicate keys as long as their values differ.
 */
int av_map_strcmp_keyvalue(const char *a, const char *b);

/**
 * like av_map_strcmp_keyvalue() but is compatible with av_strcasecmp() and av_map_supercmp_key.
 *
 * A map initialized with this will allow duplicate keys as long as their values differ.
 */
int av_map_supercmp_keyvalue(const char *a, const char *b);

/**
 * like strcmp() but is compatible with av_strcasecmp().
 *
 * A map initialized with this will not allow duplicate keys.
 */
int av_map_supercmp_key(const char *a, const char *b);


/**
 *
 * @param keyvalue_cmp compare function, will be passed the key + value concatenated.
 *                     it must form a strict total order on all elements you want to store. each key-value pair
 *                     can only occur once. Though there can be multiple values for the same key. IF this function
 *                     treats them as different.
 *
 * @param freef receives a AVMapEntry and should free any resources except the AVMapEntry->key/value pointer itself
 *              for flat structs like strings, this is simply NULL
 *
 *                          Key     Value   compaibility
 * av_map_supercmp_keyvalue X!=x    X!=x    av_map_supercmp_key, av_strcasecmp, (trucated av_strcasecmp)
 * av_map_supercmp_key      X!=x            av_strcasecmp, (trucated av_strcasecmp)
 * av_strcasecmp            X==x            truncation
 *
 * av_map_strcmp_keyvalue   X!=x    X!=x    strcmp, truncation
 * strcmp                   X!=x            truncation
 *
 *
 */
AVMap *av_map_new(AVMapCompareFunc keyvalue_cmp, AVMapCopyFunc clone, AVMapFreeFunc freef);

/**
 * realloc internal space to accomodate the specified new elements
 *
 * This can be used to avoid repeated memory reallocation.
 *
 * @param extra_elements number of new elements to be added
 * @param extra_space    sum of keylen and valuelen of all to be added elements
 *
 * @return          <0 on error
 */
int av_map_realloc(AVMap *s, int extra_elements, int extra_space);

/**
 * Add the given entry into a AVMap.
 *
 * @param s         Pointer AVMap struct.
 * @param value     Entry value to add to *s
 * @param valuelen  length of value
 * @param flags     0, AV_MAP_ALLOW_REBUILD, AV_MAP_REPLACE
 *
 * @return          1 if the entry was added, 0 if it was already in the map, 2 if it was replaced
 *                  otherwise an error code <0
 */
int av_map_add(AVMap *s, const char *key, int keylen, const char *value, int valuelen, int flags);

/**
 * Delete the given entry from a AVMap.
 *
 * @param s         Pointer AVMap struct.
 * @param keyvalue  key or concatenated key+value
 * @param cmp       compatible compare function that comapres key or keyvalues
 * @param flags     AV_MAP_ALLOW_REBUILD or 0
 *
 * @return          1 if the entry was deleted, 0 if it was not found in the map
 *                  otherwise an error code <0
 */
int av_map_del(AVMap *s, const char *keyvalue, int (*cmp)(const void *keyvalue, const void *b), int flags);

/**
 * Iterate over possibly multiple matching map entries.
 *
 * The returned entry must not be changed, or it will
 * cause undefined behavior.
 *
 * @param prev  Set to the previous matching element to find the next.
 *              If set to NULL the first matching element is returned.
 * @param keyvalue Matching key or key + value
 * @param cmp   compare function, this will be passed keyvalue and the concatenated key+value
 *              it must form a total order on all elements, that is a key can occur more than once.
 *              But cmp2 must be a refinement of the cmp order, any disagreement of the 2 compares
 *              must be by cmp returning equal. If this only reads the key part of keyvalue
 *              then keyvalue can be just a key
 *
 * @return      Found entry or NULL in case no matching entry was found in the dictionary
 */
const AVMapEntry *av_map_get_multiple(const AVMap *s, const AVMapEntry *prev, const char *keyvalue, int (*cmp)(const void *keyvalue, const void *b));

/**
 * Like av_map_get_multiple() but only returns one matching entry
 *
 * The returned entry cannot be used as initial prev entry for av_map_get_multiple()
 */
const AVMapEntry *av_map_get(const AVMap *s, const char *keyvalue, int (*cmp)(const void *keyvalue, const void *b));

/**
 * Iterate over a map
 *
 * Iterates through all entries in the map.
 *
 * @warning If you call any function with AV_SET_ALLOW_REBUILD set, then the iterator is
 * invalidated, and must not be used anymore. Otherwise av_map_add() (without realloc) and av_map_del()
 * can saftely be called during iteration.
 *
 * Typical usage:
 * @code
 * const AVMapEntry *e = NULL;
 * while ((e = av_map_iterate(m, e))) {
 *     // ...
 * }
 * @endcode
 *
 * @param s     The map to iterate over
 * @param prev  Pointer to the previous AVMapEntry, NULL initially
 *
 * @retval AVMapEntry* The next element in the map
 * @retval NULL        No more elements in the map
 */
const AVMapEntry *av_map_iterate(const AVMap *s,
                                 const AVMapEntry *prev);

/**
 * Get number of entries in map.
 *
 * @param s map
 * @return  number of entries in map
 */
int av_map_count(const AVMap *s);

/**
 * Free all the memory allocated for an AVMap struct
 * and all values.
 */
void av_map_free(AVMap **s);

AVMap *av_map_clone(AVMap *s);

/**
 * Copy entries from one AVMap struct into another.
 *
 * @param dst   Pointer to a pointer to a AVMap struct to copy into. If *dst is NULL,
 *              this function will allocate a struct for you and put it in *dst
 * @param src   Pointer to the source AVMap struct to copy items from.
 * @param flags Flags to use when setting entries in *dst
 *
 * @see when the initial dst map is empty use av_map_clone() as its faster
 *
 * @return 0 on success, negative AVERROR code on failure.
 */

int av_map_copy(AVMap *dst, const AVMap *src);

#endif /* AVUTIL_MAP_H */
