/*
 * blueberry_vm/lib/string.c — string namespace (string.new, string.format)
 */

/* string.new(size?) — create a new preallocated string */
static ci_ptr bb_string_new(bb_coro_arg *c, ci_ptr_arg self, ci_ptr size_arg) {
	size_t sz = 64;

	if (CI_IS_INT(size_arg))
		sz = (size_t)CI_INT(size_arg);

	ci_str *s = ci_str_new(sz);
	if (!s) bb_coro_error(c, "string.new: out of memory");
	return (ci_ptr)s;
}

/* ---- registration ---- */

static void bb_lib_string_init(bb_vm *vm) {
	ci_map *ns = ci_map_new(8);

	static const bb_cfunc string_lib[] = {
		{ "new",    bb_string_new,  0 },
		{ "format", bb_str_format,  BB_FN_NATIVE_VAR },
		{ "printf", bb_str_format,  BB_FN_NATIVE_VAR },
	};

	bb_func2map(vm, ns, string_lib, sizeof(string_lib) / sizeof(string_lib[0]));

	ci_map_put(vm->globals, bb_vm_istring(vm, "string", 6), (ci_ptr)ns);
}
