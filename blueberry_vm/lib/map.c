/*
 * blueberry_vm/lib/map.c — map namespace (map.new, map.len, map.keys, etc.)
 *
 * All functions that take a map also accept an ordered_map,
 * redirecting to the tree prototype methods.
 */

#define BB_MAP_OR_TREE(m, tree_fn, tree_args) do { \
	if (!bb_is_map(m)) { \
		if (CI_IS_ORDERED_MAP(m)) return tree_fn tree_args; \
		bb_coro_error(c, "%s: expected map", __func__); \
	} \
} while(0)

/* map.new(size?) — create a regular hash map */
static ci_ptr bb_map_new(bb_coro_arg *c, ci_ptr_arg self, ci_ptr size_arg) {
	uint32_t sz = 16;

	if (CI_IS_INT(size_arg))
		sz = (uint32_t)CI_INT(size_arg);

	ci_map *m = ci_map_ident_new(sz);
	BB_RETURN_NOINC(m);
}

/* map.len(m) — length of map or ordered_map */
static ci_ptr bb_map_lib_len(bb_coro_arg *c, ci_ptr_arg self, ci_ptr m) {
	BB_MAP_OR_TREE(m, bb_tree_len, (c, m));

	return CI_PACKINT(ci_map_len((ci_map *)m));  /* scalar */
}

/* map.keys(m) — array of keys */
static ci_ptr bb_map_keys(bb_coro_arg *c, ci_ptr_arg self, ci_ptr m) {
	BB_MAP_OR_TREE(m, bb_tree_keys, (c, m));

	ci_map *map = (ci_map *)m;
	uint32_t len = ci_map_len(map);
	ci_array *arr = ci_arr_new(len);
	ci_ptr *out = arr->data;

	uint32_t cursor = 0;
	ci_map_kv *kv;

	while ((kv = ci_map_next(map, &cursor)) != NULL)
		*out++ = kv->key;

	arr->length = len;
	BB_RETURN_NOINC(arr);
}

/* map.values(m) — array of values */
static ci_ptr bb_map_values(bb_coro_arg *c, ci_ptr_arg self, ci_ptr m) {
	BB_MAP_OR_TREE(m, bb_tree_values, (c, m));

	ci_map *map = (ci_map *)m;
	uint32_t len = ci_map_len(map);
	ci_array *arr = ci_arr_new(len);
	ci_ptr *out = arr->data;

	uint32_t cursor = 0;
	ci_map_kv *kv;

	while ((kv = ci_map_next(map, &cursor)) != NULL)
		*out++ = kv->val;

	arr->length = len;
	BB_RETURN_NOINC(arr);
}

/* map.delete(m, key) — delete a key, returns bool */
static ci_ptr bb_map_lib_delete(bb_coro_arg *c, ci_ptr_arg self, ci_ptr m, ci_ptr key) {
	BB_MAP_OR_TREE(m, bb_tree_delete, (c, m, key));

	int removed = ci_map_delete((ci_map *)m, key);
	return CI_BOOL(removed);  /* scalar */
}

/* map.exists(m, key) — true if key exists (distinguishes null from missing) */
static ci_ptr bb_map_lib_exists(bb_coro_arg *c, ci_ptr_arg self, ci_ptr m, ci_ptr key) {
	BB_MAP_OR_TREE(m, bb_tree_exists, (c, m, key));

	ci_map_kv *kv = ci_map_find_kv((ci_map *)m, key);
	return CI_BOOL(kv != NULL);  /* scalar */
}

/* map.size(m, newsize?) — get/set allocated bucket count. tree: returns length, ignores setter */
static ci_ptr bb_map_lib_size(bb_coro_arg *c, ci_ptr_arg self, ci_ptr m, ci_ptr size_arg) {
	BB_MAP_OR_TREE(m, bb_tree_len, (c, m));

	if (CI_IS_INT(size_arg))
		ci_map_ensure_space((ci_map *)m, (uint32_t)CI_INT(size_arg));

	return CI_PACKINT(ci_map_buckets((ci_map *)m));  /* scalar */
}

/* map._merge(dst, src) — shallow merge src into dst, maps only */
static ci_ptr bb_map_lib_merge(bb_coro_arg *c, ci_ptr_arg self, ci_ptr dst, ci_ptr src) {
	BB_CHECK_MAP(dst);
	BB_CHECK_MAP(src);

	ci_map *d = (ci_map *)dst;
	ci_map *s = (ci_map *)src;

	ci_map_ensure_space(d, ci_map_len(s));

	uint32_t cursor = 0;
	ci_map_kv *kv;

	while ((kv = ci_map_next(s, &cursor)) != NULL) {
		ci_inc(kv->val);
		ci_map_put(d, kv->key, kv->val);
	}

	BB_RETURN(dst);
}

/* ---- registration ---- */

static void bb_lib_map_init(bb_vm *vm) {
	ci_map *ns = ci_map_new(8);

	static const bb_cfunc map_lib[] = {
		{ "new",     bb_map_new,     0 },
		{ "len",     bb_map_lib_len, 0 },
		{ "keys",    bb_map_keys,    0 },
		{ "values",  bb_map_values,  0 },
		{ "delete",  bb_map_lib_delete, 0 },
		{ "exists",  bb_map_lib_exists, 0 },
		{ "size",    bb_map_lib_size, 0 },
		{ "_merge",  bb_map_lib_merge, 0 },
	};

	bb_func2map(vm, ns, map_lib, sizeof(map_lib) / sizeof(map_lib[0]));

	ci_map_put(vm->globals, bb_vm_istring(vm, "map", 3), (ci_ptr)ns);
}
