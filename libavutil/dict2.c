/*
 * AVDictionary2 implementation using hash table for improved performance
 * Copyright (c) 2025 FFmpeg Team
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

#include <string.h>
#include <stddef.h>
#include <inttypes.h>
#include <stdio.h>

#include "dict2.h"
#include "mem.h"
#include "error.h"
#include "avstring.h"

/* Dictionary entry */
typedef struct DictEntry {
    struct DictEntry *next;  // For collision chains 
    char *key;
    char *value;
} DictEntry;

/* Dictionary implementation */
struct AVDictionary2 {
    DictEntry **entries;
    int table_size;    // Size of hash table
    int count;         // Number of entries
    int flags;         // Dictionary flags
};

/* Initial table size and resizing constants */
#define DICT_INITIAL_SIZE 64
#define DICT_LOAD_FACTOR 0.75  // Resize when count > table_size * load_factor

/* Basic hash function */
static unsigned int dict_hash(const char *key, int case_sensitive) {
    unsigned int hash = 0;
    const unsigned char *p;
    
    for (p = (const unsigned char *)key; *p; p++) {
        hash = hash * 31 + (case_sensitive ? *p : av_toupper(*p));
    }
    return hash;
}

/* Set a dictionary entry */
int av_dict2_set(AVDictionary2 **pm, const char *key, const char *value, int flags) {
    AVDictionary2 *m;
    DictEntry *entry;
    unsigned int hash;
    int table_idx;
    
    if (!key)
        return AVERROR(EINVAL);
    
    // Create dictionary if it doesn't exist
    if (!*pm) {
        *pm = av_mallocz(sizeof(AVDictionary2));
        if (!*pm)
            return AVERROR(ENOMEM);
        
        (*pm)->table_size = DICT_INITIAL_SIZE;  // Larger initial size
        (*pm)->entries = av_mallocz(sizeof(DictEntry*) * (*pm)->table_size);
        if (!(*pm)->entries) {
            av_freep(pm);
            return AVERROR(ENOMEM);
        }
        
        // Set flags once at creation
        (*pm)->flags = flags & AV_DICT2_MATCH_CASE;
    }
    
    m = *pm;
    
    // Get hash index
    hash = dict_hash(key, m->flags & AV_DICT2_MATCH_CASE);
    table_idx = hash % m->table_size;
    
    // Check if key already exists
    for (entry = m->entries[table_idx]; entry; entry = entry->next) {
        if ((m->flags & AV_DICT2_MATCH_CASE ? 
             !strcmp(entry->key, key) : 
             !av_strcasecmp(entry->key, key))) {
            
            // Don't overwrite if flag is set
            if (flags & AV_DICT2_DONT_OVERWRITE)
                return 0;
            
            // Replace value
            av_free(entry->value);
            entry->value = av_strdup(value ? value : "");
            if (!entry->value)
                return AVERROR(ENOMEM);
            
            return 0;
        }
    }
    
    // Create new entry
    entry = av_mallocz(sizeof(DictEntry));
    if (!entry)
        return AVERROR(ENOMEM);
    
    entry->key = av_strdup(key);
    if (!entry->key) {
        av_freep(&entry);
        return AVERROR(ENOMEM);
    }
    
    entry->value = av_strdup(value ? value : "");
    if (!entry->value) {
        av_freep(&entry->key);
        av_freep(&entry);
        return AVERROR(ENOMEM);
    }
    
    // Insert at head of chain
    entry->next = m->entries[table_idx];
    m->entries[table_idx] = entry;
    m->count++;
    
    // Check if we need to resize the hash table
    if (m->count > m->table_size * DICT_LOAD_FACTOR) {
        // Resize hash table
        int new_size = m->table_size * 2;
        DictEntry **new_entries = av_mallocz(sizeof(DictEntry*) * new_size);
        if (!new_entries) {
            // Continue with current table if resize fails
            return 0;
        }
        
        // Rehash all entries
        for (int i = 0; i < m->table_size; i++) {
            DictEntry *current = m->entries[i];
            while (current) {
                DictEntry *next = current->next;
                
                // Compute new hash index
                unsigned int new_hash = dict_hash(current->key, m->flags & AV_DICT2_MATCH_CASE);
                int new_idx = new_hash % new_size;
                
                // Insert at head of new chain
                current->next = new_entries[new_idx];
                new_entries[new_idx] = current;
                
                current = next;
            }
        }
        
        // Replace old table with new one
        av_freep(&m->entries);
        m->entries = new_entries;
        m->table_size = new_size;
    }
    
    return 0;
}

/* Get a dictionary entry */
AVDictionaryEntry2 *av_dict2_get(const AVDictionary2 *m, const char *key,
                               const AVDictionaryEntry2 *prev, int flags) {
    unsigned int hash;
    int table_idx;
    DictEntry *entry;
    
    static AVDictionaryEntry2 de;  // Return value - holds pointers to internal data
    
    if (!m || !key)
        return NULL;
        
    if (prev)
        return NULL;  // 'prev' functionality not implemented
        
    // Get hash index
    hash = dict_hash(key, m->flags & AV_DICT2_MATCH_CASE);
    table_idx = hash % m->table_size;
    
    // Search in chain
    for (entry = m->entries[table_idx]; entry; entry = entry->next) {
        if ((m->flags & AV_DICT2_MATCH_CASE ? 
             !strcmp(entry->key, key) : 
             !av_strcasecmp(entry->key, key))) {
            
            // Found match
            de.key = entry->key;
            de.value = entry->value;
            return &de;
        }
    }
    
    return NULL;  // Not found
}

/* Count dictionary entries */
int av_dict2_count(const AVDictionary2 *m) {
    return m ? m->count : 0;
}

/* Free dictionary */
void av_dict2_free(AVDictionary2 **pm) {
    AVDictionary2 *m;
    int i;
    
    if (!pm || !*pm)
        return;
        
    m = *pm;
    
    // Free all entries
    for (i = 0; i < m->table_size; i++) {
        DictEntry *entry = m->entries[i];
        while (entry) {
            DictEntry *next = entry->next;
            av_freep(&entry->key);
            av_freep(&entry->value);
            av_freep(&entry);
            entry = next;
        }
    }
    
    av_freep(&m->entries);
    av_freep(pm);
}

/* Dictionary iterator state */
typedef struct {
    const AVDictionary2 *dict;
    int table_idx;
    DictEntry *entry;
    AVDictionaryEntry2 de;
} DictIter;

static DictIter iter_state;  // Single static iterator state

/* Iterate through dictionary */
const AVDictionaryEntry2 *av_dict2_iterate(const AVDictionary2 *m,
                                        const AVDictionaryEntry2 *prev) {
    int i;
    
    if (!m || !m->count)
        return NULL;
        
    // Initialize iterator or move to next entry
    if (!prev) {
        // Start from beginning
        iter_state.dict = m;
        iter_state.table_idx = 0;
        iter_state.entry = NULL;
        
        // Find first entry
        for (i = 0; i < m->table_size; i++) {
            if (m->entries[i]) {
                iter_state.table_idx = i;
                iter_state.entry = m->entries[i];
                break;
            }
        }
    } else {
        // Ensure iterator belongs to this dictionary
        if (iter_state.dict != m)
            return NULL;
            
        // Move to next entry in current chain
        if (iter_state.entry && iter_state.entry->next) {
            iter_state.entry = iter_state.entry->next;
        } else {
            // Move to next chain
            iter_state.entry = NULL;
            for (i = iter_state.table_idx + 1; i < m->table_size; i++) {
                if (m->entries[i]) {
                    iter_state.table_idx = i;
                    iter_state.entry = m->entries[i];
                    break;
                }
            }
        }
    }
    
    // Return current entry or NULL if done
    if (iter_state.entry) {
        iter_state.de.key = iter_state.entry->key;
        iter_state.de.value = iter_state.entry->value;
        return &iter_state.de;
    }
    
    return NULL;
}

/* Set integer value */
int av_dict2_set_int(AVDictionary2 **pm, const char *key, int64_t value, int flags) {
    char valuestr[22];  // Enough for INT64_MIN
    snprintf(valuestr, sizeof(valuestr), "%"PRId64, value);
    return av_dict2_set(pm, key, valuestr, flags);
}

/* Copy dictionary */
int av_dict2_copy(AVDictionary2 **dst, const AVDictionary2 *src, int flags) {
    const AVDictionaryEntry2 *entry = NULL;
    int ret;
    
    if (!src)
        return 0;
        
    while ((entry = av_dict2_iterate(src, entry))) {
        ret = av_dict2_set(dst, entry->key, entry->value, flags);
        if (ret < 0)
            return ret;
    }
    
    return 0;
}

/* Parse a string of key-value pairs */
int av_dict2_parse_string(AVDictionary2 **pm, const char *str,
                        const char *key_val_sep, const char *pairs_sep,
                        int flags) {
    // Stub implementation - not implemented yet
    return AVERROR(ENOSYS);
}
