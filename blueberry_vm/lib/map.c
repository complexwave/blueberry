/*
 * blueberry_vm/lib/map.c — map namespace (map.new, map.ordered, etc.)
 */

/* map.new(size?) — create a regular hash map */
static ci_ptr bb_map_new(bb_coro_arg *c, ci_ptr self, ci_ptr size_arg) {
	(void)c; (void)self;
	uint32_t sz = 16;
	if (CI_IS_INT(size_arg))
		sz = (uint32_t)CI_INT(size_arg);
	ci_map *m = ci_map_ident_new(sz);
	printf("map\n");
	return (ci_ptr)m;
}

/* map.ordered() — create an ordered map */
static ci_ptr bb_map_ordered(bb_coro_arg *c, ci_ptr self) {
	(void)c; (void)self;
	ci_tree *t = ci_tree_new(NULL, NULL);
	return (ci_ptr)t;
}

/* map.len(m) — length of any map type */
static ci_ptr bb_map_lib_len(bb_coro_arg *c, ci_ptr self, ci_ptr m) {
	(void)self;
	if (CI_IS_MAP(m))
		return CI_PACKINT(ci_map_len((ci_map *)m));
	if (CI_IS_ORDERED_MAP(m))
		return CI_PACKINT(ci_tree_len((ci_tree *)m));
	bb_coro_error(c, "map.len: expected map");
	return NULL;
}

/* map.keys(m) — return array of keys */
static ci_ptr bb_map_keys(bb_coro *c, ci_ptr self, ci_ptr m) {
	(void)self;
	BB_CHECK_MAP(m);
	ci_map *map = (ci_map *)m;
	uint32_t len = ci_map_len(map);
	ci_array *arr = ci_arr_new(len);
	ci_ptr *out = arr->data;

	uint32_t cursor = 0;
	ci_map_kv *kv;
	while ((kv = ci_map_next(map, &cursor)) != NULL) {
		*out++ = kv->key;
	}
	arr->length = len;

	return (ci_ptr)arr;
}

/* map.values(m) — return array of values */
static ci_ptr bb_map_values(bb_coro *c, ci_ptr self, ci_ptr m) {
	(void)self;
	BB_CHECK_MAP(m);
	ci_map *map = (ci_map *)m;
	uint32_t len = ci_map_len(map);
	ci_array *arr = ci_arr_new(len);
	ci_ptr *out = arr->data;

	uint32_t cursor = 0;
	ci_map_kv *kv;
	while ((kv = ci_map_next(map, &cursor)) != NULL) {
		*out++ = kv->val;
	}
	arr->length = len;

	return (ci_ptr)arr;
}

/* ---- registration ---- */

static void bb_lib_map_init(bb_vm *vm) {
	ci_map *ns = ci_map_new(8);

	static const bb_cfunc map_lib[] = {
		{ "new",     bb_map_new,     0 },
		{ "ordered", bb_map_ordered, 0 },
		{ "len",     bb_map_lib_len, 0 },
		{ "keys",    bb_map_keys,    0 },
		{ "values",  bb_map_values,  0 },
	};

	bb_func2map(vm, ns, map_lib, sizeof(map_lib) / sizeof(map_lib[0]));

	ci_map_put(vm->globals, bb_vm_istring(vm, "map", 3), (ci_ptr)ns);
}
