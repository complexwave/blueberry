/*
 * test_stress.c — randomized alloc/dealloc hammering
 *
 * Uses a simple LCG PRNG for determinism. No rand().
 */
#include "tgmemlib.c"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define PASS(name)          do { printf("PASS: %s\n", (name)); } while(0)
#define FAIL(name, reason)  do { printf("FAIL: %s — %s\n", (name), (reason)); return 1; } while(0)

/* LCG: same params as glibc rand */
static uint64_t lcg_state = 12345;
static uint32_t lcg_next(void) {
    lcg_state = lcg_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (uint32_t)(lcg_state >> 33);
}

/* 8-type sizes for multi-type test */
static const uint16_t TYPE_SIZES[8] = {8, 16, 24, 32, 48, 64, 128, 256};
#define NUM_TYPES 8

/* Pool entry: pointer + expected fill byte + type tag */
typedef struct {
    void   *ptr;
    uint8_t fill;
    uint8_t tag;
    int     obj_size;
} pool_entry_t;

static int verify_fill(void *ptr, uint8_t fill, int obj_size) {
    unsigned char *p = (unsigned char *)ptr;
    for (int i = 0; i < obj_size; i++) {
        if (p[i] != fill) return 0;
    }
    return 1;
}

/* ---- test 1: random alloc/free mix, single type ---- */
static int test_random_alloc_free(void) {
    lcg_state = 0xdeadbeef;
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 32);

#define POOL_SZ 1000
    pool_entry_t pool[POOL_SZ];
    int live[POOL_SZ];   /* indices of live entries */
    int nlive = 0;
    int free_pool[POOL_SZ];
    int nfree_pool = 0;
    for (int i = 0; i < POOL_SZ; i++) free_pool[i] = i;
    nfree_pool = POOL_SZ;

#define OPS 500000
    for (int op = 0; op < OPS; op++) {
        int do_alloc = (nlive == 0) || (nfree_pool > 0 && (lcg_next() % 10) < 6);
        if (do_alloc) {
            int slot = free_pool[--nfree_pool];
            void *p = tg_alloc(alloc, 0);
            if (!p) { tg_allocator_destroy(alloc); FAIL("random_alloc_free", "alloc returned NULL"); }
            uint8_t fill = (uint8_t)(lcg_next() & 0xFF);
            memset(p, fill, 32);
            pool[slot].ptr      = p;
            pool[slot].fill     = fill;
            pool[slot].tag      = 0;
            pool[slot].obj_size = 32;
            live[nlive++] = slot;
        } else {
            /* pick random live entry */
            int idx = (int)(lcg_next() % (uint32_t)nlive);
            int slot = live[idx];
            pool_entry_t *e = &pool[slot];
            if (!verify_fill(e->ptr, e->fill, e->obj_size)) {
                tg_allocator_destroy(alloc);
                FAIL("random_alloc_free", "fill corrupted before free");
            }
            tg_free(e->ptr);
            free_pool[nfree_pool++] = slot;
            live[idx] = live[--nlive];
        }
    }
    tg_allocator_destroy(alloc);
    PASS("random_alloc_free");
    return 0;
#undef POOL_SZ
#undef OPS
}

/* ---- test 2: burst alloc then bulk free ---- */
static int test_burst_bulk_free(void) {
    lcg_state = 0xc0ffee42;
    tg_allocator_t *alloc = tg_allocator_new();
    tg_allocator_register_type(alloc, 0, 16);

#define BURST 5000
    void *ptrs[BURST];
    uint8_t fills[BURST];

    for (int round = 0; round < 2; round++) {
        /* alloc burst */
        for (int i = 0; i < BURST; i++) {
            ptrs[i] = tg_alloc(alloc, 0);
            if (!ptrs[i]) { tg_allocator_destroy(alloc); FAIL("burst_bulk_free", "alloc NULL"); }
            fills[i] = (uint8_t)(lcg_next() & 0xFF);
            memset(ptrs[i], fills[i], 16);
        }
        /* verify */
        for (int i = 0; i < BURST; i++) {
            if (!verify_fill(ptrs[i], fills[i], 16)) {
                tg_allocator_destroy(alloc);
                FAIL("burst_bulk_free", "fill mismatch after burst");
            }
        }
        /* free all */
        for (int i = 0; i < BURST; i++) tg_free(ptrs[i]);
    }
    tg_allocator_destroy(alloc);
    PASS("burst_bulk_free");
    return 0;
#undef BURST
}

/* ---- test 3: multiple types interleaved ---- */
static int test_multi_type_interleaved(void) {
    lcg_state = 0xfeedface;
    tg_allocator_t *alloc = tg_allocator_new();
    for (int t = 0; t < NUM_TYPES; t++) {
        tg_allocator_register_type(alloc, (uint8_t)t, TYPE_SIZES[t]);
    }

#define POOL_SZ 800
    pool_entry_t pool[POOL_SZ];
    int live[POOL_SZ];
    int nlive = 0;
    int free_pool[POOL_SZ];
    int nfree_pool = POOL_SZ;
    for (int i = 0; i < POOL_SZ; i++) free_pool[i] = i;

#define OPS 200000
    for (int op = 0; op < OPS; op++) {
        int do_alloc = (nlive == 0) || (nfree_pool > 0 && (lcg_next() % 10) < 6);
        if (do_alloc) {
            int slot    = free_pool[--nfree_pool];
            uint8_t tag = (uint8_t)(lcg_next() % NUM_TYPES);
            int obj_size = TYPE_SIZES[tag];
            void *p = tg_alloc(alloc, tag);
            if (!p) { tg_allocator_destroy(alloc); FAIL("multi_type", "alloc NULL"); }
            /* fill byte encodes tag to catch cross-type contamination */
            uint8_t fill = (uint8_t)((0x10 * tag + 0x05) & 0xFF);
            memset(p, fill, obj_size);
            pool[slot].ptr      = p;
            pool[slot].fill     = fill;
            pool[slot].tag      = tag;
            pool[slot].obj_size = obj_size;
            live[nlive++] = slot;
        } else {
            int idx  = (int)(lcg_next() % (uint32_t)nlive);
            int slot = live[idx];
            pool_entry_t *e = &pool[slot];
            /* verify arena tag matches expected type */
            tg_arena_t *ar = tg_ptr_arena(e->ptr);
            if (ar->type_tag != e->tag) {
                tg_allocator_destroy(alloc);
                FAIL("multi_type", "arena type_tag mismatch (cross-type contamination)");
            }
            if (!verify_fill(e->ptr, e->fill, e->obj_size)) {
                tg_allocator_destroy(alloc);
                FAIL("multi_type", "fill corrupted (cross-type contamination)");
            }
            tg_free(e->ptr);
            free_pool[nfree_pool++] = slot;
            live[idx] = live[--nlive];
        }
    }
    tg_allocator_destroy(alloc);
    PASS("multi_type_interleaved");
    return 0;
#undef POOL_SZ
#undef OPS
}

int main(void) {
    int r = 0;
    r |= test_random_alloc_free();
    r |= test_burst_bulk_free();
    r |= test_multi_type_interleaved();
    return r;
}
