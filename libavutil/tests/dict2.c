/*
 * AVDictionary2 test utility
 * This file is part of FFmpeg.
 */

#include "libavutil/dict2.h"
#include "libavutil/dict.h"
#include "libavutil/time.h"
#include "libavutil/avassert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static void basic_functionality_test(void)
{
    printf("\n=== Basic Functionality Test ===\n");
    
    AVDictionary2 *dict = NULL;
    AVDictionaryEntry2 *entry;
    int ret;
    
    // Test setting keys
    ret = av_dict2_set(&dict, "key1", "value1", 0);
    printf("Adding key1: %s\n", ret >= 0 ? "OK" : "FAILED");
    av_assert0(ret >= 0);
    
    ret = av_dict2_set(&dict, "key2", "value2", 0);
    printf("Adding key2: %s\n", ret >= 0 ? "OK" : "FAILED");
    av_assert0(ret >= 0);
    
    // Test lookup
    entry = av_dict2_get(dict, "key1", NULL, 0);
    printf("Lookup key1: %s (value: %s)\n", 
           entry ? "OK" : "FAILED",
           entry ? entry->value : "NULL");
    av_assert0(entry && !strcmp(entry->value, "value1"));
    
    // Test count
    int count = av_dict2_count(dict);
    printf("Dictionary count: %d (expected 2)\n", count);
    av_assert0(count == 2);
    
    // Test iteration
    printf("Dictionary contents:\n");
    const AVDictionaryEntry2 *iter = NULL;
    while ((iter = av_dict2_iterate(dict, iter))) {
        printf("  %s: %s\n", iter->key, iter->value);
    }
    
    // Free dictionary
    av_dict2_free(&dict);
    printf("Dictionary freed successfully\n");
}

static void overwrite_test(void)
{
    printf("\n=== Overwrite Test ===\n");
    
    AVDictionary2 *dict = NULL;
    AVDictionaryEntry2 *entry;
    
    // Test normal overwrite
    av_dict2_set(&dict, "key", "value1", 0);
    av_dict2_set(&dict, "key", "value2", 0);
    
    entry = av_dict2_get(dict, "key", NULL, 0);
    printf("Overwrite test: %s (value: %s, expected: value2)\n", 
           entry && !strcmp(entry->value, "value2") ? "OK" : "FAILED",
           entry ? entry->value : "NULL");
    av_assert0(entry && !strcmp(entry->value, "value2"));
    
    // Test DONT_OVERWRITE flag
    av_dict2_set(&dict, "key", "value3", AV_DICT2_DONT_OVERWRITE);
    
    entry = av_dict2_get(dict, "key", NULL, 0);
    printf("DONT_OVERWRITE flag test: %s (value: %s, expected: value2)\n", 
           entry && !strcmp(entry->value, "value2") ? "OK" : "FAILED",
           entry ? entry->value : "NULL");
    av_assert0(entry && !strcmp(entry->value, "value2"));
    
    av_dict2_free(&dict);
}

static void case_sensitivity_test(void)
{
    printf("\n=== Case Sensitivity Test ===\n");
    
    // Test case-sensitive dictionary with AV_DICT2_MATCH_CASE flag
    AVDictionary2 *dict1 = NULL;
    av_dict2_set(&dict1, "Key", "value1", AV_DICT2_MATCH_CASE);
    
    AVDictionaryEntry2 *entry1 = av_dict2_get(dict1, "key", NULL, AV_DICT2_MATCH_CASE);
    printf("Case-sensitive lookup: %s (expected NULL)\n", 
           entry1 ? "FAILED" : "OK");
    av_assert0(entry1 == NULL);
    
    // Test case-insensitive dictionary (default behavior)
    AVDictionary2 *dict2 = NULL;
    av_dict2_set(&dict2, "Key", "value1", 0); 
    
    AVDictionaryEntry2 *entry2 = av_dict2_get(dict2, "key", NULL, 0);
    printf("Case-insensitive lookup: %s (value: %s)\n", 
           entry2 ? "OK" : "FAILED",
           entry2 ? entry2->value : "NULL");
    av_assert0(entry2 && !strcmp(entry2->value, "value1"));
    
    av_dict2_free(&dict1);
    av_dict2_free(&dict2);
}

static void stress_test(void)
{
    printf("\n=== Stress Test ===\n");
    
    AVDictionary2 *dict = NULL;
    char key[32], value[32];
    int i, count, lookup_successful = 0;
    int64_t start_time, elapsed;
    
    // Create a large number of entries
    const int num_entries = 10000;
    printf("Creating %d entries...\n", num_entries);
    
    start_time = av_gettime();
    for (i = 0; i < num_entries; i++) {
        sprintf(key, "key%d", i);
        sprintf(value, "value%d", i);
        av_dict2_set(&dict, key, value, 0);
    }
    elapsed = av_gettime() - start_time;
    printf("Insertion time: %" PRId64 " us (%.2f us per entry)\n", 
           elapsed, (double)elapsed / num_entries);
    
    // Test lookup of all keys
    printf("Looking up all keys...\n");
    start_time = av_gettime();
    for (i = 0; i < num_entries; i++) {
        sprintf(key, "key%d", i);
        AVDictionaryEntry2 *entry = av_dict2_get(dict, key, NULL, 0);
        if (entry) lookup_successful++;
    }
    elapsed = av_gettime() - start_time;
    printf("Lookup time: %" PRId64 " us (%.2f us per lookup)\n", 
           elapsed, (double)elapsed / num_entries);
    printf("Found %d of %d entries\n", lookup_successful, num_entries);
    av_assert0(lookup_successful == num_entries);
    
    // Check count
    count = av_dict2_count(dict);
    printf("Dictionary count: %d (expected %d)\n", count, num_entries);
    av_assert0(count == num_entries);
    
    // Free dictionary and measure cleanup time
    start_time = av_gettime();
    av_dict2_free(&dict);
    elapsed = av_gettime() - start_time;
    printf("Cleanup time: %" PRId64 " us\n", elapsed);
    printf("Stress test completed successfully\n");
}

int main(int argc, char **argv)
{
    printf("AVDictionary2 Test Suite\n");
    printf("========================\n");
    
    // Check if specific test is requested
    int run_stress = 0;
    if (argc >= 2 && !strcmp(argv[1], "stress")) {
        run_stress = 1;
    }
    
    // Always run basic tests
    basic_functionality_test();
    overwrite_test();
    case_sensitivity_test();
    
    // Run stress test if requested
    if (run_stress) {
        stress_test();
    }

    printf("\nAll tests PASSED!\n");
    return 0;
}
