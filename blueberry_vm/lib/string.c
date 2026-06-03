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

/* string.eq(a, b) — global wrapper; null if either is not a string */
static ci_ptr bb_string_eq(bb_coro_arg *c, ci_ptr_arg self, ci_ptr a, ci_ptr b) {
	(void)self;
	return bb_str_eq(c, a, b);
}

/* string.cmp(a, b) — global wrapper; null if either is not a string */
static ci_ptr bb_string_cmp(bb_coro_arg *c, ci_ptr_arg self, ci_ptr a, ci_ptr b) {
	(void)self;
	return bb_str_cmp(c, a, b);
}

/* str(x) / string.from(x) — convert any value to a string.
 *   null/bool     → "null" / "true" / "false"
 *   string        → returned as-is
 *   number        → ci_printf "%g"  (42.0→"42", 3.14→"3.14")
 *   anything else → ci_printf "[%s]" on typename + hex address
 */
static ci_ptr bb_str_from(bb_coro_arg *c, ci_ptr_arg self, ci_ptr x, ci_ptr_arg a1, ci_ptr_arg a2) {
	(void)a2;
	bb_vm *vm = ((bb_coro *)c)->vm;

	if (!x)                return BB_CSTR(vm, "null");
	if (x == CI_BOOL(1))   return BB_CSTR(vm, "true");
	if (x == CI_BOOL(0))   return BB_CSTR(vm, "false");
	if (CI_IS_ANY_STR(x))  return (ci_ptr)ci_str_copy((ci_str *)x, 0);

	if (CI_IS_ANY_NUMBER(x)) {
		ci_str *dst = ci_str_new(32);
		if (!dst) bb_coro_error(c, "str: out of memory");
		
		ci_printf((ci_ptr)dst, (const uint8_t *)"%g", strlen("%g"), &x, 1);
		
		return (ci_ptr)dst;
	}

	/* everything else: [typename 0xADDR] */
	
	ci_ptr tname = bb_typeof(vm, x);
	ci_ptr fargs_obj[] = { tname, x };
	
	ci_str *dst = ci_str_new(64);
	if (!dst) bb_coro_error(c, "str: out of memory");
	ci_printf((ci_ptr)dst, (const uint8_t *)"[%s 0x%p]", strlen("[%s 0x%p]"), fargs_obj, 2);
	
	return (ci_ptr)dst;
}

/* ---- registration ---- */

static void bb_lib_string_init(bb_vm *vm) {
	ci_map *ns = ci_map_new(8);

	static const bb_cfunc string_lib[] = {
		{ "new",    bb_string_new,  0 },
		{ "format", bb_str_format,  BB_FN_NATIVE_VAR },
		{ "printf", bb_str_format,  BB_FN_NATIVE_VAR },
		{ "eq",     bb_string_eq,   0 },
		{ "cmp",    bb_string_cmp,  0 },
		{ "from",   bb_str_from,    0 },
	};

	bb_func2map(vm, ns, string_lib, sizeof(string_lib) / sizeof(string_lib[0]));

	ci_map_put(vm->globals, bb_vm_istring(vm, "string", 6), (ci_ptr)ns);

	/* global str() */
	bb_func2global(vm, "str", bb_str_from, 0);
}
