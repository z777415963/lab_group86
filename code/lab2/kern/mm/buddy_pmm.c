#include <pmm.h>
#include <list.h>
#include <stdio.h>
#include <buddy_pmm.h>

// lab2 task3 2413498
#define MAX_BUDDY_ORDER 15

static list_entry_t free_lists[MAX_BUDDY_ORDER + 1];
static size_t nr_free;
static struct Page *managed_base;
static size_t managed_pages;

static size_t
order_size(unsigned int order) {
    return (size_t)1 << order;
}

static bool
is_power_of_two(size_t n) {
    return n != 0 && (n & (n - 1)) == 0;
}

static unsigned int
ceil_order(size_t n) {
    unsigned int order = 0;
    size_t size = 1;
    while (size < n && order < MAX_BUDDY_ORDER) {
        size <<= 1;
        order++;
    }
    return order;
}

static unsigned int
floor_order(size_t n) {
    unsigned int order = 0;
    while (order < MAX_BUDDY_ORDER &&
           order_size(order + 1) <= n) {
        order++;
    }
    return order;
}

static void
add_free_block(struct Page *base, unsigned int order) {
    size_t size = order_size(order);

    base->property = size;
    SetPageProperty(base);
    list_add(&free_lists[order], &base->page_link);
}

static void
remove_free_block(struct Page *base) {
    list_del(&base->page_link);
    ClearPageProperty(base);
}

static void
buddy_init(void) {
    for (unsigned int order = 0;
         order <= MAX_BUDDY_ORDER; order++) {
        list_init(&free_lists[order]);
    }

    nr_free = 0;
    managed_base = NULL;
    managed_pages = 0;
}

static void
buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    assert(managed_base == NULL);

    managed_base = base;
    managed_pages = n;

    for (struct Page *p = base; p < base + n; p++) {
        assert(PageReserved(p));
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
        list_init(&p->page_link);
    }

    size_t offset = 0;
    while (offset < n) {
        size_t remaining = n - offset;
        unsigned int order = floor_order(remaining);

        while (order > 0 &&
               (offset & (order_size(order) - 1)) != 0) {
            order--;
        }

        add_free_block(base + offset, order);
        offset += order_size(order);
    }

    nr_free = n;
}

static struct Page *
buddy_alloc_pages(size_t n) {
    assert(n > 0);

    if (n > nr_free) {
        return NULL;
    }

    unsigned int wanted = ceil_order(n);
    if (order_size(wanted) < n) {
        return NULL;
    }

    unsigned int found = wanted;
    while (found <= MAX_BUDDY_ORDER &&
           list_empty(&free_lists[found])) {
        found++;
    }

    if (found > MAX_BUDDY_ORDER) {
        return NULL;
    }

    list_entry_t *entry = list_next(&free_lists[found]);
    struct Page *block = le2page(entry, page_link);
    remove_free_block(block);

    while (found > wanted) {
        found--;
        struct Page *right =
            block + order_size(found);
        add_free_block(right, found);
    }

    size_t allocated = order_size(wanted);
    for (struct Page *p = block;
         p < block + allocated; p++) {
        ClearPageProperty(p);
        p->property = 0;
        set_page_ref(p, 0);
    }

    /*
     * PageProperty remains clear, so the block is allocated.
     * property records the real power-of-two block size for free_pages.
     */
    block->property = allocated;
    nr_free -= allocated;
    return block;
}

static void
buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    assert(managed_base != NULL);
    assert(base >= managed_base);
    assert(base < managed_base + managed_pages);

    size_t size = base->property;
    if (!is_power_of_two(size) || size < n) {
        size = order_size(ceil_order(n));
    }

    assert(base + size <= managed_base + managed_pages);

    size_t offset = base - managed_base;
    assert((offset & (size - 1)) == 0);

    for (struct Page *p = base; p < base + size; p++) {
        assert(!PageReserved(p));
        assert(!PageProperty(p));
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
    }

    nr_free += size;
    unsigned int order = ceil_order(size);

    while (order < MAX_BUDDY_ORDER) {
        size_t buddy_offset = offset ^ size;

        if (buddy_offset + size > managed_pages) {
            break;
        }

        struct Page *buddy = managed_base + buddy_offset;

        if (!PageProperty(buddy) ||
            buddy->property != size) {
            break;
        }

        remove_free_block(buddy);
        buddy->property = 0;

        if (buddy_offset < offset) {
            base = buddy;
            offset = buddy_offset;
        }

        size <<= 1;
        order++;
    }

    add_free_block(base, order);
}

static size_t
buddy_nr_free_pages(void) {
    return nr_free;
}

static void
buddy_check(void) {
    size_t before = nr_free_pages();

    struct Page *p1 = alloc_pages(1);
    struct Page *p2 = alloc_pages(2);
    struct Page *p3 = alloc_pages(3);

    assert(p1 != NULL && p2 != NULL && p3 != NULL);
    assert(p1 != p2 && p1 != p3 && p2 != p3);

    /* 3 pages must be rounded up to a 4-page buddy block. */
    assert(p1->property == 1);
    assert(p2->property == 2);
    assert(p3->property == 4);
    assert(nr_free_pages() == before - 7);

    free_pages(p1, 1);
    free_pages(p2, 2);
    free_pages(p3, 3);
    assert(nr_free_pages() == before);

    /* Allocation larger than all free memory must fail safely. */
    assert(alloc_pages(before + 1) == NULL);
    assert(nr_free_pages() == before);

    /* Check an exact power-of-two allocation and its release. */
    struct Page *p8 = alloc_pages(8);
    assert(p8 != NULL);
    assert(p8->property == 8);
    free_pages(p8, 8);
    assert(nr_free_pages() == before);

    /*
     * Two one-page allocations exercise splitting.
     * Releasing both exercises recursive buddy merging.
     */
    p1 = alloc_pages(1);
    p2 = alloc_pages(1);
    assert(p1 != NULL && p2 != NULL);
    assert(p1 + 1 == p2 || p2 + 1 == p1);

    free_pages(p1, 1);
    free_pages(p2, 1);
    assert(nr_free_pages() == before);

    cprintf("buddy_check() succeeded!\n");
}

// lab2 task3 2413498
const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
