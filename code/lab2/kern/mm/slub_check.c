#include <defs.h>
#include <pmm.h>
#include <slub.h>
#include <stdio.h>

/*
 * Tests for the simplified SLUB allocator.
 *
 * slub_basic_check() :
 *   - creates a small (16-byte) and a large (1024-byte) cache,
 *   - checks allocation, size alignment, distinctness and data integrity,
 *   - frees everything and checks that the objects can be allocated again,
 *   - alternates the two caches to verify that they do not interfere.
 *
 * slub_stress_check() :
 *   - allocates more 512-byte objects than one 4 KiB page can hold,
 *   - verifies that several slab pages were used and that no object overlaps,
 *   - frees the objects in a scrambled order and allocates them again,
 *   - verifies that the allocator still works and that several pages are used.
 */

#define BASIC_OBJ_N       5
#define STRESS_OBJ_SIZE   512
#define STRESS_OBJ_N      20

/* Address of the page that contains obj. */
static uintptr_t
object_page(void *obj) {
    return (uintptr_t)obj & ~((uintptr_t)PGSIZE - 1);
}

/* Check that obj is non-NULL and aligned to align bytes. */
static void
check_object(void *obj, size_t align, const char *what) {
    if (obj == NULL) {
        panic("%s: kmem_cache_alloc() returned NULL", what);
    }
    if (((uintptr_t)obj % align) != 0) {
        panic("%s: object is not aligned to %d bytes", what, (int)align);
    }
}

/* Check that all n objects are different. */
static void
check_distinct(void **objs, int n, const char *what) {
    int i, j;

    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n; j++) {
            if (objs[i] == objs[j]) {
                panic("%s: the same object was returned twice", what);
            }
        }
    }
}

/* Write a deterministic pattern into the whole object. */
static void
fill_pattern(void *obj, size_t size, unsigned char seed) {
    size_t i;

    for (i = 0; i < size; i++) {
        ((unsigned char *)obj)[i] = (unsigned char)(seed + i * 7);
    }
}

/* Verify that the object still holds the pattern written by fill_pattern(). */
static void
check_pattern(void *obj, size_t size, unsigned char seed, const char *what) {
    size_t i;

    for (i = 0; i < size; i++) {
        if (((unsigned char *)obj)[i] != (unsigned char)(seed + i * 7)) {
            panic("%s: object contents were overwritten", what);
        }
    }
}

/* Number of distinct pages that contain the given objects. */
static int
count_pages(void **objs, int n) {
    int count = 0, i, j;

    for (i = 0; i < n; i++) {
        int seen = 0;
        for (j = 0; j < i; j++) {
            if (object_page(objs[j]) == object_page(objs[i])) {
                seen = 1;
                break;
            }
        }
        if (!seen) {
            count++;
        }
    }
    return count;
}

void
slub_basic_check(void) {
    struct kmem_cache *small_cache, *large_cache;
    void *small[BASIC_OBJ_N], *large[BASIC_OBJ_N];
    void *old_small[BASIC_OBJ_N], *old_large[BASIC_OBJ_N];
    int i, j, reused;

    cprintf("slub_basic_check:\n");

    small_cache = kmem_cache_create("check16", 16);
    large_cache = kmem_cache_create("check1024", 1024);
    if (small_cache == NULL || large_cache == NULL) {
        panic("slub_basic_check: kmem_cache_create() failed");
    }

    /* Case 1: allocate five objects from each cache. */
    for (i = 0; i < BASIC_OBJ_N; i++) {
        small[i] = kmem_cache_alloc(small_cache);
        check_object(small[i], 16, "16-byte cache object");
        large[i] = kmem_cache_alloc(large_cache);
        check_object(large[i], 1024, "1024-byte cache object");
    }
    check_distinct(small, BASIC_OBJ_N, "16-byte cache objects");
    check_distinct(large, BASIC_OBJ_N, "1024-byte cache objects");

    /* Different caches must never hand out objects from the same page. */
    if (object_page(small[0]) == object_page(large[0])) {
        panic("slub_basic_check: different caches returned objects from one page");
    }

    /* No object may overlap another one: fill and verify every byte. */
    for (i = 0; i < BASIC_OBJ_N; i++) {
        fill_pattern(small[i], 16, (unsigned char)i);
        fill_pattern(large[i], 1024, (unsigned char)(i + 100));
    }
    for (i = 0; i < BASIC_OBJ_N; i++) {
        check_pattern(small[i], 16, (unsigned char)i, "16-byte cache object");
        check_pattern(large[i], 1024, (unsigned char)(i + 100),
                      "1024-byte cache object");
    }

    /* Case 2: free everything, then allocate again. */
    for (i = 0; i < BASIC_OBJ_N; i++) {
        old_small[i] = small[i];
        old_large[i] = large[i];
        kmem_cache_free(small_cache, small[i]);
        kmem_cache_free(large_cache, large[i]);
    }

    reused = 0;
    for (i = 0; i < BASIC_OBJ_N; i++) {
        small[i] = kmem_cache_alloc(small_cache);
        check_object(small[i], 16, "16-byte cache object after free");
        large[i] = kmem_cache_alloc(large_cache);
        check_object(large[i], 1024, "1024-byte cache object after free");
        for (j = 0; j < BASIC_OBJ_N; j++) {
            if (small[i] == old_small[j]) {
                reused++;
            }
            if (large[i] == old_large[j]) {
                reused++;
            }
        }
    }
    if (reused == 0) {
        panic("slub_basic_check: freed objects were never reused");
    }

    for (i = 0; i < BASIC_OBJ_N; i++) {
        kmem_cache_free(small_cache, small[i]);
        kmem_cache_free(large_cache, large[i]);
    }

    /* Alternate the two caches: they must not interfere with each other. */
    for (i = 0; i < 3; i++) {
        void *s = kmem_cache_alloc(small_cache);
        void *l = kmem_cache_alloc(large_cache);
        check_object(s, 16, "16-byte cache object (alternating)");
        check_object(l, 1024, "1024-byte cache object (alternating)");
        if (object_page(s) == object_page(l)) {
            panic("slub_basic_check: caches interfered with each other");
        }
        kmem_cache_free(small_cache, s);
        kmem_cache_free(large_cache, l);
    }

    cprintf("slub_basic_check() succeeded!\n");
}

void
slub_stress_check(void) {
    struct kmem_cache *cache;
    void *objs[STRESS_OBJ_N];
    int i, pages;

    cprintf("slub_stress_check:\n");

    cache = kmem_cache_create("check512", STRESS_OBJ_SIZE);
    if (cache == NULL) {
        panic("slub_stress_check: kmem_cache_create() failed");
    }

    /* Case 1: 20 objects of 512 bytes cannot fit in one slab page. */
    for (i = 0; i < STRESS_OBJ_N; i++) {
        objs[i] = kmem_cache_alloc(cache);
        check_object(objs[i], STRESS_OBJ_SIZE, "512-byte cache object");
    }
    check_distinct(objs, STRESS_OBJ_N, "512-byte cache objects");

    pages = count_pages(objs, STRESS_OBJ_N);
    if (pages < 2) {
        panic("slub_stress_check: objects did not span multiple slab pages");
    }
    cprintf("  %d objects span %d pages\n", STRESS_OBJ_N, pages);

    for (i = 0; i < STRESS_OBJ_N; i++) {
        fill_pattern(objs[i], STRESS_OBJ_SIZE, (unsigned char)(i * 13 + 7));
    }
    for (i = 0; i < STRESS_OBJ_N; i++) {
        check_pattern(objs[i], STRESS_OBJ_SIZE,
                      (unsigned char)(i * 13 + 7), "512-byte cache object");
    }

    /* Case 2: free the objects in a scrambled order (7 and 20 are coprime). */
    for (i = 0; i < STRESS_OBJ_N; i++) {
        int idx = (i * 7) % STRESS_OBJ_N;
        kmem_cache_free(cache, objs[idx]);
    }

    for (i = 0; i < STRESS_OBJ_N; i++) {
        objs[i] = kmem_cache_alloc(cache);
        check_object(objs[i], STRESS_OBJ_SIZE,
                     "512-byte cache object after scrambled free");
    }
    check_distinct(objs, STRESS_OBJ_N,
                   "512-byte cache objects after scrambled free");

    pages = count_pages(objs, STRESS_OBJ_N);
    if (pages < 2) {
        panic("slub_stress_check: allocator did not use multiple pages");
    }

    for (i = 0; i < STRESS_OBJ_N; i++) {
        fill_pattern(objs[i], STRESS_OBJ_SIZE, (unsigned char)(i * 5 + 3));
    }
    for (i = 0; i < STRESS_OBJ_N; i++) {
        check_pattern(objs[i], STRESS_OBJ_SIZE,
                      (unsigned char)(i * 5 + 3),
                      "512-byte cache object after reuse");
    }
    for (i = 0; i < STRESS_OBJ_N; i++) {
        kmem_cache_free(cache, objs[i]);
    }

    cprintf("slub_stress_check() succeeded!\n");
}

/* Run all SLUB tests. */
void
check_slub(void) {
    slub_basic_check();
    slub_stress_check();
    cprintf("check_slub() succeeded!\n");
}
