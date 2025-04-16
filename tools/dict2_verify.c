/*
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

/*
 * AVDictionary vs AVDictionary2 vs AVMap Correctness Verification
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../libavutil/dict.h"
#include "../libavutil/dict2.h"
#include "../libavutil/map.h"
#include "../libavutil/time.h"
#include "../libavutil/avstring.h"

#define RAND_STR_LEN 16
#define TEST_ITERATIONS 100

/* Structure to hold key-value pairs */
typedef struct {
    char key[RAND_STR_LEN];
    char val[RAND_STR_LEN];
} KeyValuePair;

/* Generate random string */
static void gen_random_str(char *s, int len) {
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    int i;

    for (i = 0; i < len - 1; i++) {
        s[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
    }
    s[len - 1] = '\0';
}

/* Fill a dictionary with pre-generated key-value pairs */
static void fill_dict(AVDictionary **dict, KeyValuePair *data, int count) {
    int i;

    for (i = 0; i < count; i++) {
        av_dict_set(dict, data[i].key, data[i].val, 0);
    }
}

/* Fill a dictionary2 with pre-generated key-value pairs */
static void fill_dict2(AVDictionary2 **dict, KeyValuePair *data, int count) {
    int i;

    for (i = 0; i < count; i++) {
        av_dict2_set(dict, data[i].key, data[i].val, 0);
    }
}

/* Fill an AVMap with pre-generated key-value pairs */
static void fill_map(AVMap *map, KeyValuePair *data, int count) {
    int i;

    for (i = 0; i < count; i++) {
        av_map_add(map, data[i].key, strlen(data[i].key), 
                   data[i].val, strlen(data[i].val), 0);
    }
}

/* Free lookup keys */
static void free_lookup_keys(char **keys, int count) {
    int i;
    for (i = 0; i < count; i++) {
        free(keys[i]);
    }
    free(keys);
}

/* Wrapper for av_strcasecmp to match AVMapCompareFunc signature */
static int map_strcasecmp(const void *a, const void *b)
{
    /* Cast void pointers to char pointers as expected by av_strcasecmp */
    return av_strcasecmp((const char *)a, (const char *)b);
}

int main(int argc, char *argv[])
{
    int count = 1000; // Default dictionary size
    int i, errors = 0;
    KeyValuePair *test_data;
    char **lookup_keys_100, **lookup_keys_50;

    // Parse command line for count
    if (argc > 1) {
        count = atoi(argv[1]);
        if (count <= 0) count = 1000;
    }

    printf("Verifying correctness of AVDictionary vs AVDictionary2 vs AVMap with %d entries\n\n", count);

    srand(1234); // Fixed seed for reproducibility

    // Pre-generate all test data
    test_data = malloc(count * sizeof(KeyValuePair));
    if (!test_data) {
        fprintf(stderr, "Failed to allocate test data\n");
        return 1;
    }

    // Generate key-value pairs
    for (i = 0; i < count; i++) {
        gen_random_str(test_data[i].key, RAND_STR_LEN);
        gen_random_str(test_data[i].val, RAND_STR_LEN);
    }

    // Prepare lookup keys (100% hits)
    lookup_keys_100 = malloc(TEST_ITERATIONS * sizeof(char*));
    if (!lookup_keys_100) {
        fprintf(stderr, "Failed to allocate lookup keys\n");
        free(test_data);
        return 1;
    }

    for (i = 0; i < TEST_ITERATIONS; i++) {
        lookup_keys_100[i] = malloc(RAND_STR_LEN);
        if (!lookup_keys_100[i]) {
            // Cleanup
            while (--i >= 0)
                free(lookup_keys_100[i]);
            free(lookup_keys_100);
            free(test_data);
            return 1;
        }
        strcpy(lookup_keys_100[i], test_data[i % count].key);
    }

    // Prepare lookup keys (50% hits, 50% misses)
    lookup_keys_50 = malloc(TEST_ITERATIONS * sizeof(char*));
    if (!lookup_keys_50) {
        fprintf(stderr, "Failed to allocate lookup keys\n");
        free_lookup_keys(lookup_keys_100, TEST_ITERATIONS);
        free(test_data);
        return 1;
    }

    // First half: existing keys
    for (i = 0; i < TEST_ITERATIONS/2; i++) {
        lookup_keys_50[i] = malloc(RAND_STR_LEN);
        if (!lookup_keys_50[i]) {
            // Cleanup
            while (--i >= 0)
                free(lookup_keys_50[i]);
            free(lookup_keys_50);
            free_lookup_keys(lookup_keys_100, TEST_ITERATIONS);
            free(test_data);
            return 1;
        }
        strcpy(lookup_keys_50[i], test_data[i % count].key);
    }

    // Second half: random keys (likely misses)
    for (; i < TEST_ITERATIONS; i++) {
        lookup_keys_50[i] = malloc(RAND_STR_LEN);
        if (!lookup_keys_50[i]) {
            // Cleanup
            while (--i >= 0)
                free(lookup_keys_50[i]);
            free(lookup_keys_50);
            free_lookup_keys(lookup_keys_100, TEST_ITERATIONS);
            free(test_data);
            return 1;
        }
        gen_random_str(lookup_keys_50[i], RAND_STR_LEN);
    }

    // Setup dictionaries
    AVDictionary *dict1 = NULL;
    AVDictionary2 *dict2 = NULL;
    // Use a simple strcmp as the comparison function
    AVMap *map = av_map_new((AVMapCompareFunc)strcmp, NULL, NULL);

    // Fill dictionaries with the same data
    fill_dict(&dict1, test_data, count);
    fill_dict2(&dict2, test_data, count);
    fill_map(map, test_data, count);

    printf("Dictionaries filled, starting verification...\n\n");

    // Test 1: Lookup verification (100% existing keys)
    printf("Test 1: Lookup verification with 100%% existing keys\n");
    errors = 0;

    // Verify AVDictionary lookups
    for (i = 0; i < TEST_ITERATIONS; i++) {
        int idx = i % count;
        AVDictionaryEntry *entry = av_dict_get(dict1, lookup_keys_100[i], NULL, 0);
        
        if (!entry) {
            printf("Error: AVDictionary couldn't find key '%s' which should exist\n", 
                   lookup_keys_100[i]);
            errors++;
            continue;
        }
        
        if (strcmp(entry->value, test_data[idx].val) != 0) {
            printf("Error: AVDictionary returned wrong value for key '%s'\n", 
                   lookup_keys_100[i]);
            printf("  Expected: '%s'\n", test_data[idx].val);
            printf("  Got: '%s'\n", entry->value);
            errors++;
        }
    }
    printf("AVDictionary result: %d errors\n\n", errors);

    // Verify AVDictionary2 lookups
    errors = 0;
    for (i = 0; i < TEST_ITERATIONS; i++) {
        int idx = i % count;
        AVDictionaryEntry2 *entry = av_dict2_get(dict2, lookup_keys_100[i], NULL, 0);
        
        if (!entry) {
            printf("Error: AVDictionary2 couldn't find key '%s' which should exist\n", 
                   lookup_keys_100[i]);
            errors++;
            continue;
        }
        
        if (strcmp(entry->value, test_data[idx].val) != 0) {
            printf("Error: AVDictionary2 returned wrong value for key '%s'\n", 
                   lookup_keys_100[i]);
            printf("  Expected: '%s'\n", test_data[idx].val);
            printf("  Got: '%s'\n", entry->value);
            errors++;
        }
    }
    printf("AVDictionary2 result: %d errors\n\n", errors);

    // Verify AVMap lookups
    errors = 0;
    for (i = 0; i < TEST_ITERATIONS; i++) {
        int idx = i % count;
        const AVMapEntry *entry = av_map_get(map, lookup_keys_100[i], map_strcasecmp);
        
        if (!entry) {
            printf("Error: AVMap couldn't find key '%s' which should exist\n", 
                   lookup_keys_100[i]);
            errors++;
            continue;
        }
        
        if (entry->valuelen != strlen(test_data[idx].val) || 
            memcmp(entry->value, test_data[idx].val, entry->valuelen) != 0) {
            printf("Error: AVMap returned wrong value for key '%s'\n", 
                   lookup_keys_100[i]);
            printf("  Expected: '%s'\n", test_data[idx].val);
            printf("  Got: '");
            fwrite(entry->value, 1, entry->valuelen, stdout);
            printf("'\n");
            errors++;
        }
    }
    printf("AVMap result: %d errors\n\n", errors);

    // Test 2: Lookup verification (50% existing keys, 50% misses)
    printf("Test 2: Lookup verification with 50%% existing keys\n");
    
    // Verify AVDictionary mixed lookups
    errors = 0;
    for (i = 0; i < TEST_ITERATIONS; i++) {
        AVDictionaryEntry *entry = av_dict_get(dict1, lookup_keys_50[i], NULL, 0);
        
        if (i < TEST_ITERATIONS/2) {
            // Should be a hit
            int idx = i % count;
            
            if (!entry) {
                printf("Error: AVDictionary couldn't find key '%s' which should exist\n", 
                       lookup_keys_50[i]);
                errors++;
                continue;
            }
            
            if (strcmp(entry->value, test_data[idx].val) != 0) {
                printf("Error: AVDictionary returned wrong value for key '%s'\n", 
                       lookup_keys_50[i]);
                printf("  Expected: '%s'\n", test_data[idx].val);
                printf("  Got: '%s'\n", entry->value);
                errors++;
            }
        } else {
            // Should be a miss
            if (entry) {
                printf("Error: AVDictionary found key '%s' which should NOT exist\n", 
                       lookup_keys_50[i]);
                errors++;
            }
        }
    }
    printf("AVDictionary result: %d errors\n\n", errors);

    // Verify AVDictionary2 mixed lookups
    errors = 0;
    for (i = 0; i < TEST_ITERATIONS; i++) {
        AVDictionaryEntry2 *entry = av_dict2_get(dict2, lookup_keys_50[i], NULL, 0);
        
        if (i < TEST_ITERATIONS/2) {
            // Should be a hit
            int idx = i % count;
            
            if (!entry) {
                printf("Error: AVDictionary2 couldn't find key '%s' which should exist\n", 
                       lookup_keys_50[i]);
                errors++;
                continue;
            }
            
            if (strcmp(entry->value, test_data[idx].val) != 0) {
                printf("Error: AVDictionary2 returned wrong value for key '%s'\n", 
                       lookup_keys_50[i]);
                printf("  Expected: '%s'\n", test_data[idx].val);
                printf("  Got: '%s'\n", entry->value);
                errors++;
            }
        } else {
            // Should be a miss
            if (entry) {
                printf("Error: AVDictionary2 found key '%s' which should NOT exist\n", 
                       lookup_keys_50[i]);
                errors++;
            }
        }
    }
    printf("AVDictionary2 result: %d errors\n\n", errors);

    // Verify AVMap mixed lookups
    errors = 0;
    for (i = 0; i < TEST_ITERATIONS; i++) {
        const AVMapEntry *entry = av_map_get(map, lookup_keys_50[i], map_strcasecmp);
        
        if (i < TEST_ITERATIONS/2) {
            // Should be a hit
            int idx = i % count;
            
            if (!entry) {
                printf("Error: AVMap couldn't find key '%s' which should exist\n", 
                       lookup_keys_50[i]);
                errors++;
                continue;
            }
            
            if (entry->valuelen != strlen(test_data[idx].val) || 
                memcmp(entry->value, test_data[idx].val, entry->valuelen) != 0) {
                printf("Error: AVMap returned wrong value for key '%s'\n", 
                       lookup_keys_50[i]);
                printf("  Expected: '%s'\n", test_data[idx].val);
                printf("  Got: '");
                fwrite(entry->value, 1, entry->valuelen, stdout);
                printf("'\n");
                errors++;
            }
        } else {
            // Should be a miss
            if (entry) {
                printf("Error: AVMap found key '%s' which should NOT exist\n", 
                       lookup_keys_50[i]);
                errors++;
            }
        }
    }
    printf("AVMap result: %d errors\n\n", errors);

    // Cleanup
    av_dict_free(&dict1);
    av_dict2_free(&dict2);
    av_map_free(&map);
    
    // Free all test data
    free_lookup_keys(lookup_keys_100, TEST_ITERATIONS);
    free_lookup_keys(lookup_keys_50, TEST_ITERATIONS);
    free(test_data);

    printf("Verification completed.\n");
    return 0;
}
