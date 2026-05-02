#ifndef CI_TREE_H
#define CI_TREE_H
/*
 * ci_tree.h — Citrin ordered map (B-tree)
 *
 * Sorted key-value store using ci_map_kv pairs.
 * Keys compared via pluggable ci_tree_cmp function.
 *
 * Based on btree.c by Joshua J Baker (MIT license).
 * Rewritten and specialized for ci_ptr key/value pairs.
 *
 * ---------------------------------------------------------------------------
 * Example:
 *
 *   ci_tree *t = ci_tree_new(NULL, NULL);   // default compare
 *   ci_tree_set(t, key, val);
 *   ci_ptr v = ci_tree_get(t, key);
 *   ci_tree_delete(t, key);
 *
 * Iteration:
 *
 *   ci_tree_iter *it = ci_tree_iter_new(t);
 *   ci_map_kv *kv;
 *   while ((kv = ci_tree_iter_next(it)) != NULL) { ... }
 *   ci_dec(it);
 * ---------------------------------------------------------------------------
 */

#include "ci_map.h"

/* ============================================================
 * Compare function
 * ============================================================ */

/*
 * ci_tree_cmp — compare two keys.
 *   Returns <0 if a < b, 0 if a == b, >0 if a > b.
 *   ctx is caller-managed (e.g. bb_coro* for VM-level compare).
 */
typedef int (*ci_tree_cmp)(ci_ptr a, ci_ptr b, ci_ptr ctx);

/* ============================================================
 * Node (internal, forward-declared for iter)
 * ============================================================ */

typedef struct ci_tree_node ci_tree_node;

/* ============================================================
 * ci_tree — ordered map
 * ============================================================ */

typedef struct {
	CI_GC_HDR;
	ci_tree_node *root;
	ci_tree_cmp   cmp;
	ci_ptr        cmpctx;
	uint32_t      count;
	uint16_t      height;
	uint64_t      hint;
} ci_tree;

/* ============================================================
 * ci_tree_iter — GC-managed iterator
 * ============================================================ */

#define CI_TREE_MAX_HEIGHT 16

typedef struct {
	ci_tree_node *node;
	int16_t       index;
} ci_tree_iter_entry;

typedef struct {
	CI_GC_HDR;
	ci_tree *tree;
	uint8_t  done;
	uint8_t  started;
	int16_t  nstack;
	ci_tree_iter_entry stack[CI_TREE_MAX_HEIGHT];
} ci_tree_iter;

/* ============================================================
 * Tag definitions
 * ============================================================ */

#define CI_ORDERED_MAP  ((uint16_t)(CI_FAMILY_ENTRY(CI_MAP_FAMILY, 1) | CI_OBJECT | CI_REFCOUNTABLE))
#define CI_TREE_ITER    ((uint16_t)(CI_FAMILY_ENTRY(CI_MAP_FAMILY, 2) | CI_OBJECT | CI_REFCOUNTABLE))

#define CI_IS_ORDERED_MAP(ptr)  CI_CHECK_MASK_FAMILY(ptr, \
	CI_FAMILY_ENTRY(CI_MAP_FAMILY, 1) | CI_OBJECT, CI_MAP_FAMILY)

#define CI_IS_TREE_ITER(ptr)    CI_CHECK_MASK_FAMILY(ptr, \
	CI_FAMILY_ENTRY(CI_MAP_FAMILY, 2) | CI_OBJECT, CI_MAP_FAMILY)

/* ============================================================
 * B-tree parameters
 * ============================================================ */

#ifndef CI_TREE_MAX_ITEMS
#define CI_TREE_MAX_ITEMS 64
#endif

#define CI_TREE_MIN_ITEMS (CI_TREE_MAX_ITEMS / 2)

/* ============================================================
 * Lifecycle
 * ============================================================ */

void ci_tree_register(void);

/*
 * ci_tree_new — allocate new ordered map.
 *   cmp = NULL uses ci_tree_default_cmp.
 *   cmpctx is stored as-is, not refcounted.
 */
ci_tree *ci_tree_new(ci_tree_cmp cmp, ci_ptr cmpctx);

/* ============================================================
 * Operations
 * ============================================================ */

/* Insert or replace. Returns 1 on success, 0 on OOM. */
int ci_tree_set(ci_tree *t, ci_ptr key, ci_ptr val);

/* Lookup by key. Returns value or NULL. */
ci_ptr ci_tree_get(const ci_tree *t, ci_ptr key);

/* Delete by key. Returns 1 if removed, 0 if absent. */
int ci_tree_delete(ci_tree *t, ci_ptr key);

static inline uint32_t ci_tree_len(const ci_tree *t) {
	return t->count;
}

/* Remove all entries. */
void ci_tree_clear(ci_tree *t);

/* ============================================================
 * Default compare
 * ============================================================ */

/*
 * ci_tree_default_cmp — compare ci_ptr values.
 *   Ints compared by value.
 *   Strings compared by memcmp.
 *   String > any int.
 *   All other objects compare equal (returns 0).
 */
int ci_tree_default_cmp(ci_ptr a, ci_ptr b, ci_ptr ctx);

/* ============================================================
 * Iterator
 * ============================================================ */

/* Allocate iterator, ci_inc(tree). First call to _next returns first item. */
ci_tree_iter *ci_tree_iter_new(ci_tree *t);

/* Returns next kv pair or NULL when done. Pointer valid until next call. */
ci_map_kv *ci_tree_iter_next(ci_tree_iter *it);

#endif /* CI_TREE_H */
