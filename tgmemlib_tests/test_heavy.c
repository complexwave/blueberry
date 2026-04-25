/*
 * test_heavy.c — heavy stress: ~512 MB total allocations
 *
 * Hammers alloc/free randomly with two extra bulk ops:
 *   arena_fill  — alloc exactly one arena's capacity of objects in one shot
 *   arena_drain — free every live object belonging to a chosen arena
 *
 * Both ops are chosen ~10% of the time each; the rest is single alloc/free.
 * Runs until cumulative bytes allocated >= 512 MB.
 */
#include "tgmemlib.c"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define PASS(name)         do { printf("PASS: %s\n", (name)); } while(0)
#define FAIL(name, reason) do { printf("FAIL: %s — %s\n", (name), (reason)); return 1; } while(0)

/* LCG */
static uint64_t lcg_state = 0xdeadbeefcafebabe;
static uint32_t lcg_next(void) {
    lcg_state = lcg_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (uint32_t)(lcg_state >> 33);
}

/* ---- types ---- */

static const uint16_t SIZES[] = {8, 16, 32, 64, 128, 256};
#define NTYPES 6

/* ---- pool ---- */

typedef struct {
    void    *ptr;
    uint8_t  fill;
    uint8_t  tag;
    uint16_t obj_size;
} entry_t;

#define POOL_CAP 500000

static entry_t pool[POOL_CAP];
static int     live_idx[POOL_CAP];    /* pool slots that are live */
static int     free_slots[POOL_CAP];  /* available pool slots (stack) */
static int     nlive;
static int     nfree_slots;

static void pool_init(void) {
    nlive       = 0;
    nfree_slots = POOL_CAP;
    for (int i = 0; i < POOL_CAP; i++) free_slots[i] = i;
}

static void pool_push(void *ptr, uint8_t fill, uint8_t tag, uint16_t obj_size) {
    int slot            = free_slots[--nfree_slots];
    pool[slot].ptr      = ptr;
    pool[slot].fill     = fill;
    pool[slot].tag      = tag;
    pool[slot].obj_size = obj_size;
    live_idx[nlive++]   = slot;
}

/* swap-remove live[i], return the slot for reuse */
static int pool_evict(int i) {
    int slot            = live_idx[i];
    live_idx[i]         = live_idx[--nlive];
    free_slots[nfree_slots++] = slot;
    return slot;
}

/* ---- helpers ---- */

static int verify_fill(void *ptr, uint8_t fill, int obj_size) {
    unsigned char *p = (unsigned char *)ptr;
    for (int i = 0; i < obj_size; i++)
        if (p[i] != fill) return 0;
    return 1;
}

static int capacity_for(uint8_t tag) {
    return (int)((ARENA_SIZE - ARENA_HDR_SIZE) / SIZES[tag]);
}

/* ---- single ops ---- */

/* returns 1 ok, 0 pool full, -1 mmap fail */
static int do_alloc(tg_allocator_t *alloc, uint8_t tag, uint64_t *total) {
    if (nfree_slots == 0) return 0;
    uint16_t sz = SIZES[tag];
    void *p = tg_alloc(alloc, tag);
    if (!p) return -1;
    uint8_t fill = (uint8_t)(lcg_next() & 0xFF);
    memset(p, fill, sz);
    pool_push(p, fill, tag, sz);
    *total += sz;
    return 1;
}

/* returns 0 ok, -1 corruption */
static int do_free_idx(int idx) {
    entry_t *e = &pool[live_idx[idx]];
    if (!verify_fill(e->ptr, e->fill, e->obj_size)) return -1;
    tg_free(e->ptr);
    pool_evict(idx);
    return 0;
}

/* ---- bulk ops ---- */

/* alloc one arena's worth of objects of tag; returns count or -1 on fail */
static int do_arena_fill(tg_allocator_t *alloc, uint8_t tag, uint64_t *total) {
    int cap = capacity_for(tag);
    if (nfree_slots < cap) return 0;
    for (int i = 0; i < cap; i++) {
        if (do_alloc(alloc, tag, total) <= 0) return -1;
    }
    return cap;
}

/* free every live object in target_arena; returns count or -1 on corruption */
static int do_arena_drain(tg_arena_t *target) {
    int drained = 0, i = 0;
    while (i < nlive) {
        if (tg_ptr_arena(pool[live_idx[i]].ptr) == target) {
            if (do_free_idx(i) < 0) return -1;
            drained++;
            /* i stays: swap brought a new entry here */
        } else {
            i++;
        }
    }
    return drained;
}

/* ---- main ---- */

int main(void) {
    const uint64_t TARGET_BYTES = 512ULL * 1024 * 1024;
    uint64_t total = 0, ops = 0;
    uint64_t last_report = 0;

    tg_allocator_t *alloc = tg_allocator_new();
    for (int t = 0; t < NTYPES; t++)
        tg_allocator_register_type(alloc, (uint8_t)t, SIZES[t]);

    pool_init();
    printf("heavy_stress: targeting %llu MB...\n",
           (unsigned long long)(TARGET_BYTES >> 20));

    while (total < TARGET_BYTES) {
        uint32_t r = lcg_next() % 100;

        if (r < 10 && nlive > 0) {
            /* arena drain: pick a random live object, drain its whole arena */
            int idx = (int)(lcg_next() % (uint32_t)nlive);
            tg_arena_t *ar = tg_ptr_arena(pool[live_idx[idx]].ptr);
            int rc = do_arena_drain(ar);
            if (rc < 0) {
                tg_allocator_destroy(alloc);
                FAIL("heavy_stress", "fill corrupted during arena_drain");
            }

        } else if (r < 20) {
            /* arena fill: alloc a full arena of one type in one shot */
            uint8_t tag = (uint8_t)(lcg_next() % NTYPES);
            int rc = do_arena_fill(alloc, tag, &total);
            if (rc < 0) {
                tg_allocator_destroy(alloc);
                FAIL("heavy_stress", "alloc NULL during arena_fill");
            }

        } else if (r < 65 || nlive == 0) {
            /* single alloc */
            if (nfree_slots > 0) {
                uint8_t tag = (uint8_t)(lcg_next() % NTYPES);
                int rc = do_alloc(alloc, tag, &total);
                if (rc < 0) {
                    tg_allocator_destroy(alloc);
                    FAIL("heavy_stress", "alloc returned NULL");
                }
            }

        } else {
            /* single free */
            int idx = (int)(lcg_next() % (uint32_t)nlive);
            if (do_free_idx(idx) < 0) {
                tg_allocator_destroy(alloc);
                FAIL("heavy_stress", "fill corrupted during single free");
            }
        }

        ops++;

        /* progress every 64 MB */
        if (total - last_report >= 64ULL * 1024 * 1024) {
            printf("  ... %4llu MB allocated (%llu ops, %d live)\n",
                   (unsigned long long)(total >> 20),
                   (unsigned long long)ops,
                   nlive);
            last_report = total;
        }
    }

    printf("  done: %llu MB allocated, %llu ops, %d objects still live\n",
           (unsigned long long)(total >> 20),
           (unsigned long long)ops,
           nlive);

    tg_allocator_destroy(alloc);
    PASS("heavy_stress");
    return 0;
}
