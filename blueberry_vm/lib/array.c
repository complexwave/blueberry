/*
 * blueberry_vm/lib/array.c — array namespace (array.new)
 */

/* array.new(size?) — create a new array */
static ci_ptr bb_array_new(bb_coro_arg *c, ci_ptr_arg self, ci_ptr size_arg) {
	uint32_t sz = 16;

	if (CI_IS_INT(size_arg))
		sz = (uint32_t)CI_INT(size_arg);

	ci_array *a = ci_arr_new(sz);
	return (ci_ptr)a;
}

/* ---- registration ---- */

static void bb_lib_array_init(bb_vm *vm) {
	ci_map *ns = ci_map_new(8);

	static const bb_cfunc array_lib[] = {
		{ "new", bb_array_new, 0 },
	};

	bb_func2map(vm, ns, array_lib, sizeof(array_lib) / sizeof(array_lib[0]));

	ci_map_put(vm->globals, bb_vm_istring(vm, "array", 5), (ci_ptr)ns);
}
