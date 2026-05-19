/*
 * ci_tree.c — Citrin ordered map (B-tree)
 *
 * Sorted key-value store using ci_map_kv pairs.
 * Based on btree.c by Joshua J Baker (MIT license).
 * Rewritten and specialized for ci_ptr key/value pairs.
 *
 * Include-style — do not compile separately.
 */

#include "ci_tree.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * COW support (optional, enable with CI_TREE_USE_COW)
 * ============================================================ */

#ifdef CI_TREE_USE_COW

#include <stdatomic.h>
typedef atomic_int ci_tree_rc_t;

static int ci_tree_rc_load(ci_tree_rc_t *p) {
	return atomic_load(p);
}

static int ci_tree_rc_add(ci_tree_rc_t *p, int v) {
	return atomic_fetch_add(p, v);
}

static int ci_tree_rc_sub(ci_tree_rc_t *p, int v) {
	return atomic_fetch_sub(p, v);
}

#else

typedef int ci_tree_rc_t;
#define ci_tree_rc_load(p)    (0)
#define ci_tree_rc_add(p, v)  ((void)(p), 0)
#define ci_tree_rc_sub(p, v)  ((void)(p), 0)

#endif /* CI_TREE_USE_COW */

/* ============================================================
 * Node
 * ============================================================ */

struct ci_tree_node {
	ci_tree_rc_t rc;
	uint16_t     nitems;
	uint8_t      leaf;
	ci_map_kv    items[CI_TREE_MAX_ITEMS];
	struct ci_tree_node *children[];
};

/* leaf nodes: no children[] array at all
 * branch nodes: children[CI_TREE_MAX_ITEMS + 1] */

static size_t ci_tree_node_size(int leaf) {
	size_t sz = sizeof(ci_tree_node);
	if (!leaf)
		sz += sizeof(ci_tree_node *) * (CI_TREE_MAX_ITEMS + 1);
	return sz;
}

static ci_tree_node *ci_tree_node_new(int leaf) {
	size_t sz = ci_tree_node_size(leaf);
	ci_tree_node *n = malloc(sz);
	if (!n)
		return NULL;
	memset(n, 0, sz);
	n->leaf = leaf;
	return n;
}

static void ci_tree_node_free(ci_tree_node *node) {
#ifdef CI_TREE_USE_COW
	if (ci_tree_rc_sub(&node->rc, 1) > 0)
		return;
#endif
	if (!node->leaf) {
		for (int i = 0; i <= node->nitems; i++)
			ci_tree_node_free(node->children[i]);
	}
	free(node);
}

#ifdef CI_TREE_USE_COW
static ci_tree_node *ci_tree_node_copy(ci_tree_node *node) {
	ci_tree_node *n2 = ci_tree_node_new(node->leaf);
	if (!n2)
		return NULL;
	n2->nitems = node->nitems;
	memcpy(n2->items, node->items, node->nitems * sizeof(ci_map_kv));
	if (!n2->leaf) {
		for (int i = 0; i <= n2->nitems; i++) {
			n2->children[i] = node->children[i];
			ci_tree_rc_add(&n2->children[i]->rc, 1);
		}
	}
	return n2;
}

#define ci_tree_cow(bnode, fail) do { \
	if (ci_tree_rc_load(&(bnode)->rc) > 0) { \
		ci_tree_node *_copy = ci_tree_node_copy(bnode); \
		if (!_copy) { fail; } \
		ci_tree_node_free(bnode); \
		(bnode) = _copy; \
	} \
} while (0)

#else
#define ci_tree_cow(bnode, fail) ((void)0)
#endif

/* ============================================================
 * Node helpers
 * ============================================================ */

static void ci_tree_node_shift_right(ci_tree_node *node, int index) {
	int n = node->nitems - index;
	memmove(&node->items[index + 1], &node->items[index],
		n * sizeof(ci_map_kv));
	if (!node->leaf) {
		memmove(&node->children[index + 1], &node->children[index],
			(n + 1) * sizeof(ci_tree_node *));
	}
	node->nitems++;
}

static void ci_tree_node_shift_left(ci_tree_node *node, int index,
	int for_merge)
{
	int n = node->nitems - index - 1;
	memmove(&node->items[index], &node->items[index + 1],
		n * sizeof(ci_map_kv));
	if (!node->leaf) {
		if (for_merge) {
			index++;
			n--;
		}
		memmove(&node->children[index], &node->children[index + 1],
			(n + 1) * sizeof(ci_tree_node *));
	}
	node->nitems--;
}

static void ci_tree_node_join(ci_tree_node *left, ci_tree_node *right) {
	memcpy(&left->items[left->nitems], right->items,
		right->nitems * sizeof(ci_map_kv));
	if (!left->leaf) {
		memcpy(&left->children[left->nitems], &right->children[0],
			(right->nitems + 1) * sizeof(ci_tree_node *));
	}
	left->nitems += right->nitems;
}

/* ============================================================
 * Binary search
 * ============================================================ */

static int ci_tree_bsearch(const ci_tree *t, ci_tree_node *node,
	ci_ptr key, int *found)
{
	int lo = 0;
	int hi = node->nitems;
	while (lo < hi) {
		int mid = (lo + hi) >> 1;
		int cmp = t->cmp(key, node->items[mid].key, t->cmpctx);
		if (cmp == 0) {
			*found = 1;
			return mid;
		}
		if (cmp < 0)
			hi = mid;
		else
			lo = mid + 1;
	}
	*found = 0;
	return lo;
}

static int ci_tree_bsearch_hint(const ci_tree *t, ci_tree_node *node,
	ci_ptr key, int *found, uint64_t *hint, int depth)
{
	int lo = 0;
	int hi = node->nitems - 1;

	if (depth < 8) {
		int idx = (int)((uint8_t *)hint)[depth];
		if (idx > hi)
			idx = hi;
		if (idx > 0) {
			int cmp = t->cmp(key, node->items[idx].key, t->cmpctx);
			if (cmp == 0) {
				*found = 1;
				return idx;
			}
			if (cmp > 0)
				lo = idx + 1;
			else
				hi = idx - 1;
		}
	}

	int index;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		int cmp = t->cmp(key, node->items[mid].key, t->cmpctx);
		if (cmp == 0) {
			*found = 1;
			index = mid;
			goto done;
		}
		if (cmp < 0)
			hi = mid - 1;
		else
			lo = mid + 1;
	}
	*found = 0;
	index = lo;
done:
	if (depth < 8)
		((uint8_t *)hint)[depth] = (uint8_t)index;
	return index;
}

static int ci_tree_search(ci_tree *t, ci_tree_node *node,
	ci_ptr key, int *found, int depth)
{
	return ci_tree_bsearch_hint(t, node, key, found, &t->hint, depth);
}

/* ============================================================
 * Default compare
 * ============================================================ */

int ci_tree_default_cmp(ci_ptr a, ci_ptr b, ci_ptr ctx) {
	(void)ctx;

	int a_int = CI_IS_INT(a);
	int b_int = CI_IS_INT(b);
	int a_str = !a_int && CI_IS_ANY_STR(a);
	int b_str = !b_int && CI_IS_ANY_STR(b);

	if (a_int && b_int) {
		intptr_t av = CI_INT(a);
		intptr_t bv = CI_INT(b);
		return (av > bv) - (av < bv);
	}

	if (a_str && b_str) {
		size_t alen = ci_str_len(a);
		size_t blen = ci_str_len(b);
		size_t minlen = alen < blen ? alen : blen;
		int r = memcmp(ci_str_head(a), ci_str_head(b), minlen);
		if (r != 0)
			return r;
		return (alen > blen) - (alen < blen);
	}

	/* string > int */
	if (a_str && b_int) return 1;
	if (a_int && b_str) return -1;

	/* everything else compares equal */
	return 0;
}

/* ============================================================
 * Split / rebalance
 * ============================================================ */

static ci_tree_node *ci_tree_node_split(ci_tree_node *node,
	ci_map_kv *median_out)
{
	int mid = CI_TREE_MAX_ITEMS / 2;
	ci_tree_node *right = ci_tree_node_new(node->leaf);
	if (!right)
		return NULL;

	*median_out = node->items[mid];
	right->nitems = node->nitems - (mid + 1);
	memcpy(right->items, &node->items[mid + 1],
		right->nitems * sizeof(ci_map_kv));
	if (!node->leaf) {
		memcpy(right->children, &node->children[mid + 1],
			(right->nitems + 1) * sizeof(ci_tree_node *));
	}
	node->nitems = mid;
	return right;
}

static void ci_tree_node_rebalance(ci_tree_node *node, int i) {
	if (i == node->nitems)
		i--;

	ci_tree_node *left  = node->children[i];
	ci_tree_node *right = node->children[i + 1];

	if (left->nitems + right->nitems < CI_TREE_MAX_ITEMS) {
		/* merge */
		left->items[left->nitems] = node->items[i];
		left->nitems++;
		ci_tree_node_join(left, right);
		free(right);
		ci_tree_node_shift_left(node, i, 1);
	}
	else if (left->nitems > right->nitems) {
		/* rotate left -> right */
		ci_tree_node_shift_right(right, 0);
		right->items[0] = node->items[i];
		if (!left->leaf)
			right->children[0] = left->children[left->nitems];
		node->items[i] = left->items[left->nitems - 1];
		left->nitems--;
	}
	else {
		/* rotate right -> left */
		left->items[left->nitems] = node->items[i];
		if (!left->leaf)
			left->children[left->nitems + 1] = right->children[0];
		left->nitems++;
		node->items[i] = right->items[0];
		ci_tree_node_shift_left(right, 0, 0);
	}
}

/* ============================================================
 * Insert
 * ============================================================ */

enum ci_tree_mut {
	CI_TREE_NOCHANGE,
	CI_TREE_NOMEM,
	CI_TREE_MUST_SPLIT,
	CI_TREE_INSERTED,
	CI_TREE_REPLACED,
	CI_TREE_DELETED,
};

static ci_map_kv ci_tree_spare; /* scratch for replaced/deleted items */

static enum ci_tree_mut ci_tree_node_set(ci_tree *t, ci_tree_node *node,
	ci_map_kv item, int depth)
{
	int found = 0;
	int i = ci_tree_search(t, node, item.key, &found, depth);

	if (found) {
		ci_tree_spare = node->items[i];
		node->items[i] = item;
		return CI_TREE_REPLACED;
	}

	if (node->leaf) {
		if (node->nitems == CI_TREE_MAX_ITEMS)
			return CI_TREE_MUST_SPLIT;
		ci_tree_node_shift_right(node, i);
		node->items[i] = item;
		return CI_TREE_INSERTED;
	}

	ci_tree_cow(node->children[i], return CI_TREE_NOMEM);
	enum ci_tree_mut result = ci_tree_node_set(t, node->children[i], item, depth + 1);
	if (result == CI_TREE_INSERTED || result == CI_TREE_REPLACED)
		return result;
	if (result == CI_TREE_NOMEM)
		return CI_TREE_NOMEM;

	/* child needs split */
	if (node->nitems == CI_TREE_MAX_ITEMS)
		return CI_TREE_MUST_SPLIT;

	ci_map_kv median;
	ci_tree_node *right = ci_tree_node_split(node->children[i], &median);
	if (!right)
		return CI_TREE_NOMEM;

	ci_tree_node_shift_right(node, i);
	node->items[i] = median;
	node->children[i + 1] = right;

	return ci_tree_node_set(t, node, item, depth);
}

int ci_tree_set(ci_tree *t, ci_ptr key, ci_ptr val) {
	ci_map_kv item = { key, val };

	if (!t->root) {
		t->root = ci_tree_node_new(1);
		if (!t->root)
			return 0;
		t->root->items[0] = item;
		t->root->nitems = 1;
		t->count++;
		t->height++;
		return 1;
	}

	ci_tree_cow(t->root, return 0);

	enum ci_tree_mut result;
set:
	result = ci_tree_node_set(t, t->root, item, 0);
	if (result == CI_TREE_REPLACED) {
		return 1;
	}
	if (result == CI_TREE_INSERTED) {
		t->count++;
		return 1;
	}
	if (result == CI_TREE_NOMEM)
		return 0;

	/* root needs split */
	ci_tree_node *old_root = t->root;
	ci_tree_node *new_root = ci_tree_node_new(0);
	if (!new_root)
		return 0;

	ci_map_kv median;
	ci_tree_node *right = ci_tree_node_split(old_root, &median);
	if (!right) {
		free(new_root);
		return 0;
	}

	t->root = new_root;
	t->root->children[0] = old_root;
	t->root->items[0] = median;
	t->root->children[1] = right;
	t->root->nitems = 1;
	t->height++;
	goto set;
}

/* ============================================================
 * Get
 * ============================================================ */

static inline ci_map_kv *ci_tree_find_kv(const ci_tree *t, ci_ptr key) {
	ci_tree_node *node = t->root;

	if (!node)
		return NULL;

	int depth = 0;
	while (1) {
		int found;
		int i = ci_tree_bsearch_hint(t, node, key, &found,
			(uint64_t *)&t->hint, depth);

		if (found)
			return &node->items[i];

		if (node->leaf)
			return NULL;

		node = node->children[i];
		depth++;
	}
}

static inline ci_ptr ci_tree_get(const ci_tree *t, ci_ptr key) {
	ci_map_kv *kv = ci_tree_find_kv(t, key);

	if (kv) return kv->val;

	return NULL;
}

/* ============================================================
 * Delete
 * ============================================================ */

enum ci_tree_delact {
	CI_TREE_DELKEY,
	CI_TREE_POPMAX,
	CI_TREE_POPFRONT,
	CI_TREE_POPBACK,
};

static enum ci_tree_mut ci_tree_node_delete(ci_tree *t, ci_tree_node *node,
	enum ci_tree_delact act, ci_ptr key, ci_map_kv *prev, int depth)
{
	int i = 0;
	int found = 0;

	if (act == CI_TREE_DELKEY) {
		i = ci_tree_search(t, node, key, &found, depth);
	}
	else if (act == CI_TREE_POPMAX) {
		i = node->nitems - 1;
		found = 1;
	}
	else if (act == CI_TREE_POPFRONT) {
		i = 0;
		found = node->leaf;
	}
	else if (act == CI_TREE_POPBACK) {
		if (!node->leaf) {
			i = node->nitems;
			found = 0;
		}
		else {
			i = node->nitems - 1;
			found = 1;
		}
	}

	if (node->leaf) {
		if (found) {
			*prev = node->items[i];
			ci_tree_node_shift_left(node, i, 0);
			return CI_TREE_DELETED;
		}
		return CI_TREE_NOCHANGE;
	}

	enum ci_tree_mut result;
	if (found) {
		if (act == CI_TREE_POPMAX) {
			i++;
			ci_tree_cow(node->children[i], return CI_TREE_NOMEM);
			ci_tree_cow(node->children[i == node->nitems ? i - 1 : i + 1],
				return CI_TREE_NOMEM);
			result = ci_tree_node_delete(t, node->children[i],
				CI_TREE_POPMAX, NULL, prev, depth + 1);
			if (result == CI_TREE_NOMEM)
				return CI_TREE_NOMEM;
			result = CI_TREE_DELETED;
		}
		else {
			*prev = node->items[i];
			ci_tree_cow(node->children[i], return CI_TREE_NOMEM);
			ci_tree_cow(node->children[i == node->nitems ? i - 1 : i + 1],
				return CI_TREE_NOMEM);
			ci_map_kv popmax_spare;
			result = ci_tree_node_delete(t, node->children[i],
				CI_TREE_POPMAX, NULL, &popmax_spare, depth + 1);
			if (result == CI_TREE_NOMEM)
				return CI_TREE_NOMEM;
			node->items[i] = popmax_spare;
			result = CI_TREE_DELETED;
		}
	}
	else {
		ci_tree_cow(node->children[i], return CI_TREE_NOMEM);
		ci_tree_cow(node->children[i == node->nitems ? i - 1 : i + 1],
			return CI_TREE_NOMEM);
		result = ci_tree_node_delete(t, node->children[i], act, key, prev, depth + 1);
	}

	if (result != CI_TREE_DELETED)
		return result;

	if (node->children[i]->nitems < CI_TREE_MIN_ITEMS)
		ci_tree_node_rebalance(node, i);

	return CI_TREE_DELETED;
}

int ci_tree_delete(ci_tree *t, ci_ptr key) {
	if (!t->root)
		return 0;

	ci_tree_cow(t->root, return 0);
	ci_map_kv deleted;
	enum ci_tree_mut result = ci_tree_node_delete(t, t->root,
		CI_TREE_DELKEY, key, &deleted, 0);

	if (result == CI_TREE_NOCHANGE || result == CI_TREE_NOMEM)
		return 0;

	if (t->root->nitems == 0) {
		ci_tree_node *old = t->root;
		if (!t->root->leaf)
			t->root = t->root->children[0];
		else
			t->root = NULL;
		free(old);
		t->height--;
	}
	t->count--;
	return 1;
}

/* ============================================================
 * Clear
 * ============================================================ */

void ci_tree_clear(ci_tree *t) {
	if (t->root) {
		ci_tree_node_free(t->root);
		t->root = NULL;
	}
	t->count = 0;
	t->height = 0;
}

/* ============================================================
 * Registration + lifecycle
 * ============================================================ */

static void ci_tree_destructor(void *ptr, tg_arena_t *arena) {
	(void)arena;
	ci_tree *t = ptr;
	ci_tree_clear(t);
}

static void ci_tree_iter_destructor(void *ptr, tg_arena_t *arena) {
	(void)arena;
	ci_tree_iter *it = ptr;
	if (it->tree) {
		ci_dec(it->tree);
		it->tree = NULL;
	}
}

void ci_tree_register(void) {
	tg_arena_ops tree_ops = { ci_tree_destructor, NULL, NULL };
	ci_register_ops(CI_ORDERED_MAP, sizeof(ci_tree), &tree_ops);

	tg_arena_ops iter_ops = { ci_tree_iter_destructor, NULL, NULL };
	ci_register_ops(CI_TREE_ITER, sizeof(ci_tree_iter), &iter_ops);
}

ci_tree *ci_tree_new(ci_tree_cmp cmp, ci_ptr cmpctx) {
	ci_tree *t = ci_new(CI_ORDERED_MAP);
	if (!t)
		return NULL;
	t->root = NULL;
	t->cmp = cmp ? cmp : ci_tree_default_cmp;
	t->cmpctx = cmpctx;
	t->count = 0;
	t->height = 0;
	t->hint = 0;
	return t;
}

/* ============================================================
 * Iterator
 * ============================================================ */

static ci_tree_iter *ci_tree_iter_alloc(ci_tree *t, ci_tree_iter_step step) {
	ci_tree_iter *it = ci_new(CI_TREE_ITER);
	if (!it)
		return NULL;
	it->tree = t;
	it->step = step;
	ci_inc(t);
	it->done = 0;
	it->started = 0;
	it->nstack = 0;
	return it;
}

/* --- forward --- */

static void ci_tree_iter_push_left(ci_tree_iter *it, ci_tree_node *node) {
	while (node) {
		it->stack[it->nstack++] = (ci_tree_iter_entry){ node, 0 };
		if (node->leaf)
			break;
		node = node->children[0];
	}
}

static ci_map_kv *ci_tree_iter_forward(ci_tree_iter *it) {
	if (it->done)
		return NULL;

	if (!it->started) {
		it->started = 1;
		if (!it->tree->root) {
			it->done = 1;
			return NULL;
		}
		ci_tree_iter_push_left(it, it->tree->root);
	}

	while (it->nstack > 0) {
		ci_tree_iter_entry *top = &it->stack[it->nstack - 1];
		ci_tree_node *node = top->node;
		int idx = top->index;

		if (node->leaf) {
			if (idx < node->nitems) {
				top->index++;
				return &node->items[idx];
			}
			it->nstack--;
			continue;
		}

		if (idx < node->nitems) {
			top->index++;
			ci_map_kv *result = &node->items[idx];
			ci_tree_iter_push_left(it, node->children[idx + 1]);
			return result;
		}

		it->nstack--;
	}

	it->done = 1;
	return NULL;
}

/* --- backward --- */

static void ci_tree_iter_push_right(ci_tree_iter *it, ci_tree_node *node) {
	while (node) {
		it->stack[it->nstack++] = (ci_tree_iter_entry){ node, node->nitems - 1 };
		if (node->leaf)
			break;
		node = node->children[node->nitems];
	}
}

static ci_map_kv *ci_tree_iter_backward(ci_tree_iter *it) {
	if (it->done)
		return NULL;

	if (!it->started) {
		it->started = 1;
		if (!it->tree->root) {
			it->done = 1;
			return NULL;
		}
		ci_tree_iter_push_right(it, it->tree->root);
	}

	while (it->nstack > 0) {
		ci_tree_iter_entry *top = &it->stack[it->nstack - 1];
		ci_tree_node *node = top->node;
		int idx = top->index;

		if (node->leaf) {
			if (idx >= 0) {
				top->index--;
				return &node->items[idx];
			}
			it->nstack--;
			continue;
		}

		if (idx >= 0) {
			top->index--;
			ci_map_kv *result = &node->items[idx];
			ci_tree_iter_push_right(it, node->children[idx]);
			return result;
		}

		it->nstack--;
	}

	it->done = 1;
	return NULL;
}

/* --- constructors --- */

ci_tree_iter *ci_tree_iter_new(ci_tree *t) {
	return ci_tree_iter_alloc(t, ci_tree_iter_forward);
}

ci_tree_iter *ci_tree_iter_new_reverse(ci_tree *t) {
	return ci_tree_iter_alloc(t, ci_tree_iter_backward);
}
