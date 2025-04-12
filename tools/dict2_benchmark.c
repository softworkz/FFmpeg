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
 * AVDictionary vs AVDictionary2 Benchmark
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "libavutil/dict.h"
#include "libavutil/dict2.h"
#include "libavutil/time.h"

#define RAND_STR_LEN 16
#define TEST_ITERATIONS 5000

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

/* Fill a dictionary with random key-value pairs */
static void fill_dict(AVDictionary **dict, int count) {
    int i;
    char key[RAND_STR_LEN];
    char val[RAND_STR_LEN];

    for (i = 0; i < count; i++) {
        gen_random_str(key, RAND_STR_LEN);
        gen_random_str(val, RAND_STR_LEN);
        av_dict_set(dict, key, val, 0);
    }
}

/* Fill a dictionary2 with random key-value pairs */
static void fill_dict2(AVDictionary2 **dict, int count) {
    int i;
    char key[RAND_STR_LEN];
    char val[RAND_STR_LEN];

    for (i = 0; i < count; i++) {
        gen_random_str(key, RAND_STR_LEN);
        gen_random_str(val, RAND_STR_LEN);
        av_dict2_set(dict, key, val, 0);
    }
}

/* Generate lookup keys: some existing and some new */
static char **gen_lookup_keys(int count, AVDictionary *dict, int hit_ratio_percent) {
    int i, hits = count * hit_ratio_percent / 100;
    char **keys = malloc(count * sizeof(char *));
    if (!keys) return NULL;

    // First add some keys that exist in the dictionary
    AVDictionaryEntry *entry = NULL;
    for (i = 0; i < hits && i < count; i++) {
        entry = av_dict_get(dict, "", entry, AV_DICT_IGNORE_SUFFIX);
        if (!entry) break; // Not enough entries

        keys[i] = malloc(RAND_STR_LEN);
        if (!keys[i]) {
            while (--i >= 0) free(keys[i]);
            free(keys);
            return NULL;
        }
        strcpy(keys[i], entry->key);
    }

    // Fill the rest with random keys (likely misses)
    for (; i < count; i++) {
        keys[i] = malloc(RAND_STR_LEN);
        if (!keys[i]) {
            while (--i >= 0) free(keys[i]);
            free(keys);
            return NULL;
        }
        gen_random_str(keys[i], RAND_STR_LEN);
    }

    return keys;
}

/* Free lookup keys */
static void free_lookup_keys(char **keys, int count) {
    int i;
    for (i = 0; i < count; i++) {
        free(keys[i]);
    }
    free(keys);
}

int main(int argc, char *argv[])
{
    int count = 1000; // Default dictionary size
    double time_start, time_end, time_dict, time_dict2;

    // Parse command line for count
    if (argc > 1) {
        count = atoi(argv[1]);
        if (count <= 0) count = 1000;
    }

    printf("Benchmarking AVDictionary vs AVDictionary2 with %d entries\n\n", count);

    srand(1234); // Fixed seed for reproducibility

    // Setup dictionaries for insertion test
    AVDictionary *dict1 = NULL;
    AVDictionary2 *dict2 = NULL;

    // Benchmark 1: Insertion
    printf("1. Insertion Performance:\n");

    time_start = av_gettime_relative() / 1000.0;
    fill_dict(&dict1, count);
    time_end = av_gettime_relative() / 1000.0;
    time_dict = time_end - time_start;
    printf("   AVDictionary:  %.3f ms\n", time_dict);

    time_start = av_gettime_relative() / 1000.0;
    fill_dict2(&dict2, count);
    time_end = av_gettime_relative() / 1000.0;
    time_dict2 = time_end - time_start;
    printf("   AVDictionary2: %.3f ms (%.1f%% of original time)\n", 
           time_dict2, time_dict2*100.0/time_dict);

    // Benchmark 2: Lookup (existing keys - 100% hit rate)
    printf("\n2. Lookup Performance (100%% existing keys):\n");

    char **lookup_keys = gen_lookup_keys(TEST_ITERATIONS, dict1, 100);
    if (!lookup_keys) {
        fprintf(stderr, "Failed to generate lookup keys\n");
        return 1;
    }

    time_start = av_gettime_relative() / 1000.0;
    for (int i = 0; i < TEST_ITERATIONS; i++) {
        av_dict_get(dict1, lookup_keys[i], NULL, 0);
    }
    time_end = av_gettime_relative() / 1000.0;
    time_dict = time_end - time_start;
    printf("   AVDictionary:  %.3f ms\n", time_dict);

    time_start = av_gettime_relative() / 1000.0;
    for (int i = 0; i < TEST_ITERATIONS; i++) {
        av_dict2_get(dict2, lookup_keys[i], NULL, 0);
    }
    time_end = av_gettime_relative() / 1000.0;
    time_dict2 = time_end - time_start;
    printf("   AVDictionary2: %.3f ms (%.1f%% of original time)\n", 
           time_dict2, time_dict2*100.0/time_dict);

    free_lookup_keys(lookup_keys, TEST_ITERATIONS);

    // Benchmark 3: Lookup (mixed keys - 50% hit rate)
    printf("\n3. Lookup Performance (50%% existing keys):\n");

    lookup_keys = gen_lookup_keys(TEST_ITERATIONS, dict1, 50);
    if (!lookup_keys) {
        fprintf(stderr, "Failed to generate lookup keys\n");
        return 1;
    }

    time_start = av_gettime_relative() / 1000.0;
    for (int i = 0; i < TEST_ITERATIONS; i++) {
        av_dict_get(dict1, lookup_keys[i], NULL, 0);
    }
    time_end = av_gettime_relative() / 1000.0;
    time_dict = time_end - time_start;
    printf("   AVDictionary:  %.3f ms\n", time_dict);

    time_start = av_gettime_relative() / 1000.0;
    for (int i = 0; i < TEST_ITERATIONS; i++) {
        av_dict2_get(dict2, lookup_keys[i], NULL, 0);
    }
    time_end = av_gettime_relative() / 1000.0;
    time_dict2 = time_end - time_start;
    printf("   AVDictionary2: %.3f ms (%.1f%% of original time)\n", 
           time_dict2, time_dict2*100.0/time_dict);

    free_lookup_keys(lookup_keys, TEST_ITERATIONS);

    // Benchmark 4: Iteration
    printf("\n4. Iteration Performance:\n");

    time_start = av_gettime_relative() / 1000.0;
    AVDictionaryEntry *entry = NULL;
    while ((entry = av_dict_get(dict1, "", entry, AV_DICT_IGNORE_SUFFIX))) {
        // Just iterate
    }
    time_end = av_gettime_relative() / 1000.0;
    time_dict = time_end - time_start;
    printf("   AVDictionary:  %.3f ms\n", time_dict);

    time_start = av_gettime_relative() / 1000.0;
    const AVDictionaryEntry2 *entry2 = NULL;
    while ((entry2 = av_dict2_iterate(dict2, entry2))) {
        // Just iterate
    }
    time_end = av_gettime_relative() / 1000.0;
    time_dict2 = time_end - time_start;
    printf("   AVDictionary2: %.3f ms (%.1f%% of original time)\n", 
           time_dict2, time_dict2*100.0/time_dict);

    // Cleanup
    av_dict_free(&dict1);
    av_dict2_free(&dict2);

    printf("\nBenchmark completed successfully\n");
    return 0;
}
