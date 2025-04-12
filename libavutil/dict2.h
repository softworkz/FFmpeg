/*
 * copyright (c) 2025 FFmpeg Team
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

#ifndef AVUTIL_DICT2_H
#define AVUTIL_DICT2_H

#include <stdint.h>
#include "dict.h"

/**
 * @file
 * Public dictionary API with improved performance
 *
 * @author FFmpeg Team
 */

/**
 * @addtogroup lavu_dict AVDictionary2
 * @ingroup lavu_data
 *
 * @brief Optimized key-value store
 *
 * AVDictionary2 is a hash table-based key-value store with improved lookup and
 * memory usage compared to AVDictionary.
 *
 * This API provides the same functionality as AVDictionary with better performance.
 * The implementation uses a hash table with chaining for collision resolution,
 * resulting in O(1) average-case lookups and reduced memory allocations.
 *
 * @{
 */

/**
 * Flag defining case-sensitivity of dictionary keys
 */
#define AV_DICT2_MATCH_CASE      AV_DICT_MATCH_CASE

/**
 * Flag preventing overwriting existing entries
 */
#define AV_DICT2_DONT_OVERWRITE  AV_DICT_DONT_OVERWRITE

/**
 * Opaque dictionary type
 */
typedef struct AVDictionary2 AVDictionary2;

/**
 * Dictionary entry
 */
typedef struct AVDictionaryEntry2 {
    const char *key;   /**< key string */
    const char *value; /**< value string */
} AVDictionaryEntry2;

/**
 * Get a dictionary entry with matching key.
 *
 * @param m        dictionary to search
 * @param key      key to search for
 * @param prev     previous matched entry or NULL
 * @param flags    search flags: AV_DICT2_MATCH_CASE
 * @return         found entry or NULL if no such entry exists
 */
AVDictionaryEntry2 *av_dict2_get(const AVDictionary2 *m, const char *key,
                                const AVDictionaryEntry2 *prev, int flags);

/**
 * Set the given entry in a dictionary.
 *
 * @param pm       pointer to dictionary
 * @param key      entry key to add
 * @param value    entry value to add
 * @param flags    see AV_DICT2_* flags
 * @return         0 on success, negative error code on failure
 *
 * @note  The dictionary's case sensitivity is determined by the first call
 *        to this function. Subsequent calls will use the dictionary's stored
 *        flag values.
 */
int av_dict2_set(AVDictionary2 **pm, const char *key, const char *value, int flags);

/**
 * Set the given entry in a dictionary using an integer value.
 *
 * @param pm       pointer to dictionary
 * @param key      entry key to add
 * @param value    entry value to add
 * @param flags    see AV_DICT2_* flags
 * @return         0 on success, negative error code on failure
 */
int av_dict2_set_int(AVDictionary2 **pm, const char *key, int64_t value, int flags);

/**
 * Parse a string of key value pairs separated with specified separator.
 *
 * @param pm           pointer to a pointer to a dictionary
 * @param str          string to parse
 * @param key_val_sep  key-value separator character(s)
 * @param pairs_sep    pairs separator character(s)
 * @param flags        flags to use while adding to dictionary
 * @return             0 on success, negative AVERROR code on failure
 */
int av_dict2_parse_string(AVDictionary2 **pm, const char *str,
                         const char *key_val_sep, const char *pairs_sep,
                         int flags);

/**
 * Copy entries from one dictionary into another.
 *
 * @param dst      pointer to the destination dictionary
 * @param src      source dictionary
 * @param flags    flags to use while setting entries in the destination dictionary
 * @return         0 on success, negative AVERROR code on failure
 */
int av_dict2_copy(AVDictionary2 **dst, const AVDictionary2 *src, int flags);

/**
 * Free all memory allocated for a dictionary.
 *
 * @param pm pointer to dictionary pointer
 */
void av_dict2_free(AVDictionary2 **pm);

/**
 * Get number of entries in dictionary.
 *
 * @param m dictionary
 * @return  number of entries in dictionary
 */
int av_dict2_count(const AVDictionary2 *m);

/**
 * Iterate through a dictionary.
 *
 * @param m      dictionary to iterate through
 * @param prev   previous entry or NULL to get the first entry
 * @return       next entry or NULL when the end is reached
 *
 * @note Entries are enumerated in no particular order due to hash table structure
 * @note The returned entry should not be freed manually
 */
const AVDictionaryEntry2 *av_dict2_iterate(const AVDictionary2 *m,
                                          const AVDictionaryEntry2 *prev);

/**
 * @}
 */

#endif /* AVUTIL_DICT2_H */
