/*
 * blueberry_vm/lib/tree.c — tree namespace (tree.new)
 */

/* tree.new() — create an ordered map (B-tree) */
static ci_ptr bb_tree_new(bb_coro_arg *c, ci_ptr self) {
	(void)c; (void)self;
	ci_tree *t = ci_tree_new(NULL, NULL);
	return (ci_ptr)t;
}

/* ---- registration ---- */

static void bb_lib_tree_init(bb_vm *vm) {
	ci_map *ns = ci_map_new(4);

	static const bb_cfunc tree_lib[] = {
		{ "new", bb_tree_new, 0 },
	};

	bb_func2map(vm, ns, tree_lib, sizeof(tree_lib) / sizeof(tree_lib[0]));

	ci_map_put(vm->globals, bb_vm_istring(vm, "tree", 4), (ci_ptr)ns);
}
