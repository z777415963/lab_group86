#ifndef __KERN_MM_SLUB_H__
#define __KERN_MM_SLUB_H__

#include <defs.h>

/*
 * A simplified SLUB allocator.
 *
 * The cache object is opaque to its users: they only keep a pointer returned
 * by kmem_cache_create() and pass it back to alloc/free.
 */
struct kmem_cache;

void slub_init(void);
struct kmem_cache *kmem_cache_create(const char *name, size_t size);
void *kmem_cache_alloc(struct kmem_cache *cache);
void kmem_cache_free(struct kmem_cache *cache, void *obj);

/* tests for the simplified SLUB allocator (kern/mm/slub_check.c) */
void slub_basic_check(void);
void slub_stress_check(void);
void check_slub(void);

#endif /* !__KERN_MM_SLUB_H__ */
