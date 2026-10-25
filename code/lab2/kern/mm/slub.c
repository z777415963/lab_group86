#include <assert.h>
#include <defs.h>
#include <pmm.h>
#include <slub.h>
#include <stdio.h>

/*
 * Simplified SLUB allocator.
 *
 * The allocator has two layers:
 *
 *   1. Slab pages: when a cache has no free object, it asks the page allocator
 *      (kalloc(), i.e. the PMM) for one 4 KiB page.
 *   2. Objects: the page is cut into equal-sized objects.  Because the free
 *      objects need to be linked, the first pointer-sized word of every free
 *      object stores the address of the next free object.  This is the
 *      "in-object free list" used by SLUB/SLAB: no external node array is
 *      needed and a free object costs no extra memory.
 *
 * Object layout and alignment strategy:
 *
 *      +-----------------------+-------------------------------+-----------+
 *      | struct slab (header)  | object 0 | object 1 | ...     | tail pad  |
 *      +-----------------------+-------------------------------+-----------+
 *      ^ page base               ^ start (multiple of size)
 *
 *   - A requested size is raised to at least sizeof(void *) so that an object
 *     can always hold the free-list pointer, then rounded up to
 *     sizeof(void *).  This keeps every pointer load/store naturally aligned.
 *   - The first object starts at the first address >= page + sizeof(struct
 *     slab) that is a multiple of the (rounded) object size; every following
 *     object is exactly size bytes away.  Therefore each returned object is
 *     aligned to its object size (for the common sizes that are multiples of
 *     sizeof(void *), this is exactly the requested size).
 *   - The slab header lives at the very beginning of the page, so given an
 *     object pointer we can find its slab with page_base = obj & ~(PGSIZE-1).
 *
 * Allocation policy: scan the cache's slab list for a slab with a non-empty
 * free list; if none exists, create a new slab.  Free policy: push the object
 * back to its slab's free list; when a slab becomes completely empty it is
 * returned to the PMM, except one empty slab per cache, which is kept to avoid
 * allocating/freeing a page on every alloc/free pair.
 */

#define SLUB_ALIGN          ((size_t)sizeof(void *))
#define SLUB_MAX_CACHES     32
#define SLUB_MAGIC          0x534C55424D414731ULL /* "SLUBMAG1" */

/* One slab is exactly one page; its header sits at the beginning of the page. */
struct slab {
    uint64_t magic;          /* sanity check when an object is freed          */
    struct slab *next;       /* next slab of the same kmem_cache              */
    void *freelist;          /* head of the in-object free list               */
    void *start;             /* address of the first object                   */
    struct kmem_cache *owner;/* cache this slab belongs to                    */
    unsigned int total;      /* number of objects carved out of this page     */
    unsigned int inuse;      /* number of objects currently allocated         */
};

struct kmem_cache {
    const char *name;        /* cache name (not copied, use a static string)  */
    size_t size;             /* aligned object size in bytes                  */
    unsigned int slab_count; /* number of slabs attached to this cache        */
    struct slab *slabs;      /* slab page list                                */
    struct kmem_cache *next; /* next cache in the global cache list           */
    bool used;               /* whether this pool entry is occupied           */
};

/* The cache descriptors themselves are kept in a small static pool: creating
 * a cache must not recursively allocate from a cache. */
static struct kmem_cache slub_caches[SLUB_MAX_CACHES];
static struct kmem_cache *slub_cache_list;

/* Round addr up to the next multiple of align (align > 0). */
static uintptr_t
slub_align_up(uintptr_t addr, size_t align) {
    return (addr + align - 1) / align * align;
}

/*
 * Create a new slab page for cache, carve it into objects and link all of them
 * into the slab's free list (the lowest object is at the head).
 */
static struct slab *
slab_create(struct kmem_cache *cache) {
    void *page = kalloc();
    uintptr_t base, start;
    unsigned int i, n;
    struct slab *slab;

    if (page == NULL) {
        return NULL;
    }

    base = (uintptr_t)page;
    start = slub_align_up(base + sizeof(struct slab), cache->size);
    if (start + cache->size > base + PGSIZE) {
        /* The page cannot hold even one aligned object: give it back. */
        kfree(page);
        return NULL;
    }

    n = (unsigned int)((base + PGSIZE - start) / cache->size);
    if (n == 0) {
        kfree(page);
        return NULL;
    }

    slab = (struct slab *)page;
    slab->magic = SLUB_MAGIC;
    slab->start = (void *)start;
    slab->owner = cache;
    slab->total = n;
    slab->inuse = 0;
    slab->freelist = NULL;

    /* Build the free list in reverse order so that the first object (lowest
     * address) becomes the list head; allocation is therefore LIFO. */
    for (i = n; i > 0; i--) {
        void *obj = (void *)(start + (uintptr_t)(i - 1) * cache->size);
        *(void **)obj = slab->freelist;
        slab->freelist = obj;
    }

    slab->next = cache->slabs;
    cache->slabs = slab;
    cache->slab_count++;
    return slab;
}

/* Remove slab from cache->slabs without freeing the page. */
static bool
slab_unlink(struct kmem_cache *cache, struct slab *slab) {
    struct slab **pp = &cache->slabs;

    while (*pp != NULL && *pp != slab) {
        pp = &(*pp)->next;
    }
    if (*pp == slab) {
        *pp = slab->next;
        cache->slab_count--;
        return 1;
    }
    return 0;
}

/* Initialize the simplified SLUB allocator.  Call it once, after page_init(). */
void
slub_init(void) {
    int i;

    for (i = 0; i < SLUB_MAX_CACHES; i++) {
        slub_caches[i].name = NULL;
        slub_caches[i].size = 0;
        slub_caches[i].slab_count = 0;
        slub_caches[i].slabs = NULL;
        slub_caches[i].next = NULL;
        slub_caches[i].used = 0;
    }
    slub_cache_list = NULL;
    cprintf("slub: initialized, object granularity = %d bytes\n",
            (int)SLUB_ALIGN);
}

/*
 * Create a cache of objects whose (aligned) size is at least `size`.
 * Returns NULL when size is invalid or the cache pool is full.
 */
struct kmem_cache *
kmem_cache_create(const char *name, size_t size) {
    size_t objsize;
    struct kmem_cache *cache = NULL;
    int i;

    if (size == 0) {
        return NULL;
    }

    /* An object must be able to store the free-list pointer. */
    objsize = size;
    if (objsize < SLUB_ALIGN) {
        objsize = SLUB_ALIGN;
    }
    objsize = slub_align_up(objsize, SLUB_ALIGN);

    /* The page must at least leave room for the slab header.  Whether a
     * page with this object size can really hold an object also depends on
     * its alignment; slab_create() checks that and returns NULL gracefully. */
    if (objsize > PGSIZE - sizeof(struct slab)) {
        return NULL;
    }

    for (i = 0; i < SLUB_MAX_CACHES; i++) {
        if (!slub_caches[i].used) {
            cache = &slub_caches[i];
            break;
        }
    }
    if (cache == NULL) {
        return NULL;
    }

    cache->name = (name != NULL) ? name : "(unnamed)";
    cache->size = objsize;
    cache->slab_count = 0;
    cache->slabs = NULL;
    cache->used = 1;
    cache->next = slub_cache_list;
    slub_cache_list = cache;
    return cache;
}

/*
 * Allocate one object from cache.
 *
 * Case 1 (no free object in any existing slab): ask kalloc() for a new page,
 * carve it into objects (slab_create) and attach it to the cache's slab list.
 * Case 2: pop the first object of the free list of a slab that has free
 * objects.
 */
void *
kmem_cache_alloc(struct kmem_cache *cache) {
    struct slab *slab;
    void *obj;

    if (cache == NULL || !cache->used) {
        return NULL;
    }

    for (slab = cache->slabs; slab != NULL; slab = slab->next) {
        if (slab->freelist != NULL) {
            break;
        }
    }
    if (slab == NULL) {
        slab = slab_create(cache);
        if (slab == NULL) {
            return NULL;
        }
    }

    obj = slab->freelist;
    slab->freelist = *(void **)obj;
    slab->inuse++;
    return obj;
}

/*
 * Return one object to its cache.
 *
 * Case 3: push the object back to the free list of its slab.  When the slab
 * becomes completely empty, return the page to the PMM, except for the last
 * slab of the cache (keeping one empty slab avoids page thrashing).
 */
void
kmem_cache_free(struct kmem_cache *cache, void *obj) {
    struct slab *slab;
    uintptr_t page, offset;

    if (cache == NULL || obj == NULL || !cache->used) {
        return;
    }

    /* The slab header is at the beginning of the page that contains obj. */
    page = (uintptr_t)obj & ~((uintptr_t)PGSIZE - 1);
    slab = (struct slab *)page;

    if (slab->magic != SLUB_MAGIC || slab->owner != cache) {
        panic("kmem_cache_free: object does not belong to this cache");
    }

    /* Check that obj is one of the objects carved out of this slab. */
    offset = (uintptr_t)obj - (uintptr_t)slab->start;
    if ((uintptr_t)obj < (uintptr_t)slab->start ||
        offset % cache->size != 0 ||
        offset / cache->size >= (uintptr_t)slab->total) {
        panic("kmem_cache_free: invalid object address");
    }

    *(void **)obj = slab->freelist;
    slab->freelist = obj;
    if (slab->inuse != 0) {
        slab->inuse--;
    }

    if (slab->inuse == 0 && cache->slab_count > 1 && slab_unlink(cache, slab)) {
        slab->magic = 0;
        kfree((void *)page);
    }
}
