/*
 * blueberry_vm/proto_btree.c — ordered map built-in prototype methods
 *
 * All methods receive self (the ci_tree) as first arg.
 */

#define BB_CHECK_ORDERED_MAP(a) do { \
	if (!CI_IS_ORDERED_MAP(a)) { bb_coro_error(c, "%s: expected ordered map", __func__); } \
} while(0)

static ci_ptr bb_tree_set(bb_coro_arg *c, ci_ptr self, ci_ptr key, ci_ptr val) {
	BB_CHECK_ORDERED_MAP(self);
	ci_tree *t = (ci_tree *)self;
	t->cmpctx = (ci_ptr)c;
	ci_tree_set(t, key, val);
	return NULL;
}

static ci_ptr bb_tree_get(bb_coro_arg *c, ci_ptr self, ci_ptr key) {
	BB_CHECK_ORDERED_MAP(self);
	ci_tree *t = (ci_tree *)self;
	BB_RETURN(ci_tree_get(t, key));
}

static ci_ptr bb_tree_delete(bb_coro_arg *c, ci_ptr self, ci_ptr key) {
	BB_CHECK_ORDERED_MAP(self);
	ci_tree *t = (ci_tree *)self;
	t->cmpctx = (ci_ptr)c;
	int removed = ci_tree_delete(t, key);
	return CI_BOOL(removed);
}

static ci_ptr bb_tree_len(bb_coro_arg *c, ci_ptr self) {
	BB_CHECK_ORDERED_MAP(self);
	return CI_PACKINT(ci_tree_len((ci_tree *)self));
}

static ci_ptr bb_tree_clear(bb_coro_arg *c, ci_ptr self) {
	BB_CHECK_ORDERED_MAP(self);
	ci_tree_clear((ci_tree *)self);
	return NULL;
}

static ci_ptr bb_tree_keys(bb_coro_arg *c, ci_ptr self) {
	BB_CHECK_ORDERED_MAP(self);

	ci_tree *t = (ci_tree *)self;
	uint32_t len = ci_tree_len(t);
	ci_array *arr = ci_arr_new(len);

	ci_tree_iter *it = ci_tree_iter_new(t);
	ci_map_kv *kv;
	uint32_t i = 0;

	while ((kv = ci_tree_iter_next(it)) != NULL)
		arr->data[i++] = kv->key;

	arr->length = len;
	BB_RETURN_NOINC(arr);
}

static ci_ptr bb_tree_values(bb_coro_arg *c, ci_ptr self) {
	BB_CHECK_ORDERED_MAP(self);

	ci_tree *t = (ci_tree *)self;
	uint32_t len = ci_tree_len(t);
	ci_array *arr = ci_arr_new(len);

	ci_tree_iter *it = ci_tree_iter_new(t);
	ci_map_kv *kv;
	uint32_t i = 0;

	while ((kv = ci_tree_iter_next(it)) != NULL)
		arr->data[i++] = kv->val;

	arr->length = len;
	BB_RETURN_NOINC(arr);
}

static ci_ptr bb_tree_exists(bb_coro_arg *c, ci_ptr self, ci_ptr key) {
	BB_CHECK_ORDERED_MAP(self);

	ci_map_kv *kv = ci_tree_find_kv((ci_tree *)self, key);
	return CI_BOOL(kv != NULL);
}

/* ---- metamethods for [] access ---- */

static ci_ptr bb_tree_meta_index_get(bb_coro *c, ci_ptr self, ci_ptr key) {
	ci_tree *t = (ci_tree *)self;
	BB_RETURN(ci_tree_get(t, key));
}

static void bb_tree_meta_index_set(bb_coro *c, ci_ptr self, ci_ptr key, ci_ptr val) {
	ci_tree *t = (ci_tree *)self;
	t->cmpctx = (ci_ptr)c;
	ci_tree_set(t, key, val);
}

/* ---- registration ---- */

static void bb_proto_btree_init(bb_vm *vm) {
	static const bb_cfunc tree_lib[] = {
		{ "set",    bb_tree_set,    0 },
		{ "get",    bb_tree_get,    0 },
		{ "delete", bb_tree_delete, 0 },
		{ "len",    bb_tree_len,    0 },
		{ "clear",  bb_tree_clear,  0 },
		{ "keys",   bb_tree_keys,   0 },
		{ "values", bb_tree_values, 0 },
		{ "exists", bb_tree_exists, 0 },
		{ "size",   bb_tree_len,    0 },
	};

	bb_metaproto *mp = bb_proto_register_meta(vm, "tree");
	mp->index_get = bb_tree_meta_index_get;
	mp->index_set = bb_tree_meta_index_set;

	bb_func2map(vm, &mp->map, tree_lib, sizeof(tree_lib) / sizeof(tree_lib[0]));
	bb_set_arena_prototype(CI_ORDERED_MAP, &mp->map);
}
