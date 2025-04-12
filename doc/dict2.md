# AVDictionary2 - High Performance Dictionary Implementation

AVDictionary2 is a hash table-based key-value dictionary implementation that provides significant performance improvements over the original AVDictionary implementation.

## Overview

The implementation uses:

- Hash table with chaining for collision resolution
- Automatic table resizing when load factor exceeds 0.75
- Optimized key/value storage management
- Efficient iteration through entries

## Performance

### Time Complexity
AVDictionary2 offers substantial time complexity improvements:

| Operation | AVDictionary (Linked List) | AVDictionary2 (Hash Table) |
|-----------|----------------------------|----------------------------|
| Insert    | O(n)*                      | O(1) avg, O(n) worst       |
| Lookup    | O(n)                       | O(1) avg, O(n) worst       |
| Iteration | O(n)                       | O(n)                       |

*Where n is current dictionary size due to duplicate checking

### Memory Characteristics

**Original AVDictionary (dict.c)**
- 2 allocations per entry (key + value string duplicates)
- Dynamic array with O(log n) reallocations
- Total: ~2n + log₂(n) allocations for n entries

**AVDictionary2 (dict2.c)** 
- 3 allocations per entry (struct + key + value duplicates)
- Hash table with O(log n) bucket table reallocations
- 2 initial allocations (dict struct + initial table)
- Total: ~3n + 2 + log₂(n) allocations for n entries

**Key Differences:**
1. AVDictionary2 has faster O(1) average case operations despite 50% more allocations
2. Both handle growth with logarithmic reallocations but with different base structures
3. Real-world benchmarks show dramatic speed improvements outweigh allocation costs

