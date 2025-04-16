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
 * AVDictionary vs AVDictionary2 vs AVMap Benchmark
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

#ifdef _WIN32
#include <windows.h>
#else
#include <sched.h>
#endif

static void pin_to_core(int core_id) {
#ifdef _WIN32
    SetThreadAffinityMask(GetCurrentThread(), 1ULL << core_id);
#else
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
#endif
}
#include "../libavutil/timer.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sched.h>
#endif

#define RAND_STR_LEN 16
#define TEST_ITERATIONS 1000
#define NUM_RUNS 5000       // Number of benchmark runs for statistical significance
#define NUM_RUNS_INSERT  20      // Number of benchmark runs for statistical significance

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
        av_map_add(map, data[i].key, strlen(data[i].key) + 1, 
                   data[i].val, strlen(data[i].val) + 1, 0);
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

static int tmpcmp(const void *s1, const void *s2)
{
    return av_strcasecmp((const char *)s1, (const char *)s2);
}

#include <inttypes.h>

/* Structure to hold benchmark stats */
typedef struct {
    uint64_t min_cycles;
    uint64_t max_cycles;
    uint64_t total_cycles;
    uint64_t avg_cycles;
} BenchStats;

/* Initialize benchmark stats */
static void init_stats(BenchStats *stats) {
    stats->min_cycles = UINT64_MAX;
    stats->max_cycles = 0;
    stats->total_cycles = 0;
    stats->avg_cycles = 0;
}

/* Update benchmark stats with a new timing value */
static void update_stats(BenchStats *stats, uint64_t cycles) {
    if (cycles < stats->min_cycles) stats->min_cycles = cycles;
    if (cycles > stats->max_cycles) stats->max_cycles = cycles;
    stats->total_cycles += cycles;
}

/* Calculate final stats */
static void finalize_stats(BenchStats *stats, int num_runs) {
    stats->avg_cycles = stats->total_cycles / num_runs;
}

/* Print benchmark stats */
static void print_stats(const char *prefix, BenchStats *stats, BenchStats *baseline) {
    printf("   %s: avg %" PRIu64 " cycles (min: %" PRIu64 ", max: %" PRIu64 ")", 
           prefix, stats->avg_cycles, stats->min_cycles, stats->max_cycles);
    if (baseline) {
        double perc = (double)stats->avg_cycles * 100.0 / baseline->avg_cycles;
        printf(" (%.1f%% of baseline)\n", perc);
    } else {
        printf("\n");
    }
}

int main(int argc, char *argv[])
{
    int count = 1000; // Default dictionary size
    int i, run;
    KeyValuePair *test_data;
    char **lookup_keys_100, **lookup_keys_50;
    BenchStats dict_insert_stats, dict2_insert_stats, map_insert_stats;
    BenchStats dict_lookup100_stats, dict2_lookup100_stats, map_lookup100_stats;
    BenchStats dict_lookup50_stats, dict2_lookup50_stats, map_lookup50_stats;
    BenchStats dict_iter_stats, dict2_iter_stats, map_iter_stats;

    // Parse command line for count
    if (argc > 1) {
        count = atoi(argv[1]);
        if (count <= 0) count = 1000;
    }

    printf("Benchmarking AVDictionary vs AVDictionary2 vs AVMap with %d entries\n\n", count);

    srand(1234); // Fixed seed for reproducibility
    pin_to_core(0); // Pin to first core for consistent cycle counts
    // Warm up CPU with dummy reads to stabilize
    for (int w = 0; w < 1000; w++) {
        volatile uint64_t dummy = read_time();
        (void)dummy;
    }

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

    // Setup dictionaries that will be used for all lookup tests
    AVDictionary *dict1 = NULL;
    AVDictionary2 *dict2 = NULL;
    AVMap *map = av_map_new(tmpcmp, NULL, NULL);
    
    // Fill dictionaries with test data
    fill_dict(&dict1, test_data, count);
    fill_dict2(&dict2, test_data, count);
    fill_map(map, test_data, count);

    // Use a single dictionary for all tests
    // This simulates real-world usage where dictionaries are built once and looked up many times
    
    // Initialize stats for insertion
    init_stats(&dict_insert_stats);
    init_stats(&dict2_insert_stats);
    init_stats(&map_insert_stats);
    
    // Benchmark 1: Insertion
    printf("1. Insertion Performance:\n");
    
    // Insertion test with multiple runs
    for (run = 0; run < NUM_RUNS_INSERT; run++) {
        AVDictionary *d1 = NULL;
        AVDictionary2 *d2 = NULL;
        AVMap *m = av_map_new(tmpcmp, NULL, NULL);
        uint64_t start, end;
        
        // Time AVDictionary insertion
        start = read_time();
        fill_dict(&d1, test_data, count);
        end = read_time();
        update_stats(&dict_insert_stats, end - start);
        
        // Time AVDictionary2 insertion
        start = read_time();
        fill_dict2(&d2, test_data, count);
        end = read_time();
        update_stats(&dict2_insert_stats, end - start);
        
        // Time AVMap insertion
        start = read_time();
        fill_map(m, test_data, count);
        end = read_time();
        update_stats(&map_insert_stats, end - start);
        
        // Only for insertion tests, we cleanup each time
        av_dict_free(&d1);
        av_dict2_free(&d2);
        av_map_free(&m);
    }
    
    // Calculate final stats
    finalize_stats(&dict_insert_stats, NUM_RUNS_INSERT);
    finalize_stats(&dict2_insert_stats, NUM_RUNS_INSERT);
    finalize_stats(&map_insert_stats, NUM_RUNS_INSERT);
    
    // Print stats
    print_stats("AVDictionary ", &dict_insert_stats, NULL);
    print_stats("AVDictionary2", &dict2_insert_stats, &dict_insert_stats);
    print_stats("AVMap        ", &map_insert_stats, &dict_insert_stats);

    // Initialize stats for lookups with 100% hit rate
    init_stats(&dict_lookup100_stats);
    init_stats(&dict2_lookup100_stats);
    init_stats(&map_lookup100_stats);
    
    // Benchmark 2: Lookup (existing keys - 100% hit rate)
    printf("\n2. Lookup Performance (100%% existing keys, %d runs):\n", NUM_RUNS);
    
    for (run = 0; run < NUM_RUNS; run++) {
        // For each run, we'll reuse the existing filled dictionaries
        
        // Prevent compiler from optimizing away the lookups
        volatile int dummy = 0;
        
        uint64_t start, end;
        
        // Time AVDictionary lookup (100% hits)
        start = read_time();
        for (i = 0; i < TEST_ITERATIONS; i++) {
            AVDictionaryEntry *entry = av_dict_get(dict1, lookup_keys_100[i], NULL, 0);
               
            if (!entry) printf("\n AVDictionary: No item!!\n");
            if (entry) dummy += entry->key[0]; // Force the lookup to be used
        }
        end = read_time();
        update_stats(&dict_lookup100_stats, end - start);
        
        // Time AVDictionary2 lookup (100% hits)
        start = read_time();
        for (i = 0; i < TEST_ITERATIONS; i++) {
            AVDictionaryEntry2 *entry = av_dict2_get(dict2, lookup_keys_100[i], NULL, 0);
             
            if (!entry) printf("\n AVDictionary2: No item!!\n");
            if (entry) dummy += entry->key[0]; // Force the lookup to be used
        }
        end = read_time();
        update_stats(&dict2_lookup100_stats, end - start);
        
        // Time AVMap lookup (100% hits)
        start = read_time();
        for (i = 0; i < TEST_ITERATIONS; i++) {
            const AVMapEntry *entry = av_map_get(map, lookup_keys_100[i], tmpcmp);
            
            if (!entry) printf("\n MAP: No item!!\n");
            if (entry) dummy += ((char *)entry->key)[0]; // Force the lookup to be used
        }
        end = read_time();
        update_stats(&map_lookup100_stats, end - start);
    }
    
    // Calculate final stats
    finalize_stats(&dict_lookup100_stats, NUM_RUNS);
    finalize_stats(&dict2_lookup100_stats, NUM_RUNS);
    finalize_stats(&map_lookup100_stats, NUM_RUNS);
    
    // Print stats
    print_stats("AVDictionary ", &dict_lookup100_stats, NULL);
    print_stats("AVDictionary2", &dict2_lookup100_stats, &dict_lookup100_stats);
    print_stats("AVMap        ", &map_lookup100_stats, &dict_lookup100_stats);

    // Initialize stats for lookups with 50% hit rate
    init_stats(&dict_lookup50_stats);
    init_stats(&dict2_lookup50_stats);
    init_stats(&map_lookup50_stats);
    
    // Benchmark 3: Lookup (mixed keys - 50% hit rate)
    printf("\n3. Lookup Performance (50%% existing keys, %d runs):\n", NUM_RUNS);
    
    for (run = 0; run < NUM_RUNS; run++) {
        // Prevent compiler from optimizing away the lookups
        volatile int dummy = 0;
        
        uint64_t start, end;
        
        // Time AVDictionary lookup (50% hits)
        start = read_time();
        for (i = 0; i < TEST_ITERATIONS; i++) {
            AVDictionaryEntry *entry = av_dict_get(dict1, lookup_keys_50[i], NULL, 0);
            if (entry) dummy += entry->key[0]; // Force the lookup to be used
            if (!entry) dummy += lookup_keys_50[i][0]; // Perform a replacement action
        }
        end = read_time();
        update_stats(&dict_lookup50_stats, end - start);
        
        // Time AVDictionary2 lookup (50% hits)
        start = read_time();
        for (i = 0; i < TEST_ITERATIONS; i++) {
            AVDictionaryEntry2 *entry = av_dict2_get(dict2, lookup_keys_50[i], NULL, 0);
            if (entry) dummy += entry->key[0]; // Force the lookup to be used
            if (!entry) dummy += lookup_keys_50[i][0]; // Perform a replacement action
    }
        end = read_time();
        update_stats(&dict2_lookup50_stats, end - start);
        
        // Time AVMap lookup (50% hits)
        start = read_time();
        for (i = 0; i < TEST_ITERATIONS; i++) {
            const AVMapEntry *entry = av_map_get(map, lookup_keys_50[i], tmpcmp);
            if (entry) dummy += ((char *)entry->key)[0]; // Force the lookup to be used
            if (!entry) dummy += lookup_keys_50[i][0]; // Perform a replacement action
        }
        end = read_time();
        update_stats(&map_lookup50_stats, end - start);
    }
    
    // Calculate final stats
    finalize_stats(&dict_lookup50_stats, NUM_RUNS);
    finalize_stats(&dict2_lookup50_stats, NUM_RUNS);
    finalize_stats(&map_lookup50_stats, NUM_RUNS);
    
    // Print stats
    print_stats("AVDictionary ", &dict_lookup50_stats, NULL);
    print_stats("AVDictionary2", &dict2_lookup50_stats, &dict_lookup50_stats);
    print_stats("AVMap        ", &map_lookup50_stats, &dict_lookup50_stats);
    
    // Initialize stats for iteration
    init_stats(&dict_iter_stats);
    init_stats(&dict2_iter_stats);
    init_stats(&map_iter_stats);
    
    // Benchmark 4: Iteration
    printf("\n4. Iteration Performance (%d runs):\n", NUM_RUNS);
    
    for (run = 0; run < NUM_RUNS; run++) {
        // Prevent compiler from optimizing away iteration
        volatile int dummy = 0;
        
        uint64_t start, end;
        
        // Time AVDictionary iteration
        start = read_time();
        AVDictionaryEntry *entry = NULL;
        while ((entry = av_dict_get(dict1, "", entry, AV_DICT_IGNORE_SUFFIX))) {
            dummy += entry->key[0]; // Force the iteration to be used
        }
        end = read_time();
        update_stats(&dict_iter_stats, end - start);
        
        // Time AVDictionary2 iteration
        start = read_time();
        const AVDictionaryEntry2 *entry2 = NULL;
        while ((entry2 = av_dict2_iterate(dict2, entry2))) {
            dummy += entry2->key[0]; // Force the iteration to be used
        }
        end = read_time();
        update_stats(&dict2_iter_stats, end - start);
        
        // Time AVMap iteration
        start = read_time();
        const AVMapEntry *entry3 = NULL;
        while ((entry3 = av_map_iterate(map, entry3))) {
            dummy += ((char *)entry3->key)[0]; // Force the iteration to be used
        }
        end = read_time();
        update_stats(&map_iter_stats, end - start);
    }
    
    // Calculate final stats
    finalize_stats(&dict_iter_stats, NUM_RUNS);
    finalize_stats(&dict2_iter_stats, NUM_RUNS);
    finalize_stats(&map_iter_stats, NUM_RUNS);
    
    // Print stats
    print_stats("AVDictionary ", &dict_iter_stats, NULL);
    print_stats("AVDictionary2", &dict2_iter_stats, &dict_iter_stats);
    print_stats("AVMap        ", &map_iter_stats, &dict_iter_stats);


    // Cleanup
    av_dict_free(&dict1);
    av_dict2_free(&dict2);
    av_map_free(&map);
    
    // Free all test data
    free_lookup_keys(lookup_keys_100, TEST_ITERATIONS);
    free_lookup_keys(lookup_keys_50, TEST_ITERATIONS);
    free(test_data);

    printf("\nBenchmark completed successfully\n");
    return 0;
}
