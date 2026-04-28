/*
 * blueberry_vm/api.c — C API helpers for native functions
 */

/* ---- type check synonyms ---- */

#define bb_is_string(p)  CI_IS_ANY_STR(p)
#define bb_is_array(p)   CI_IS_ANY_ARR(p)
#define bb_is_int(p)     CI_IS_INT(p)
#define bb_is_closure(p) CI_IS_CLOSURE(p)
#define bb_is_map(p)     CI_IS_MAP(p)
#define bb_is_bool(p)    CI_IS_BOOL(p)

/* ---- CHECK macros (return NULL on type mismatch) ---- */

#define BB_CHECK_STRING(a) do { \
	if (!bb_is_string(a)) { bb_coro_error(c, "%s: expected string", __func__); } \
} while(0)

#define BB_CHECK_STRING_WRITABLE(a) do { \
	if (!bb_is_string(a)) { bb_coro_error(c, "%s: expected string", __func__); } \
	if (CI_IS_READONLY(a)) { bb_coro_error(c, "%s: string is readonly", __func__); } \
	ci_str_reset_hash(a); \
} while(0)

#define BB_CHECK_ARRAY(a) do { \
	if (!bb_is_array(a)) { bb_coro_error(c, "%s: expected array", __func__); } \
} while(0)

#define BB_CHECK_INT(a) do { \
	if (!bb_is_int(a)) { bb_coro_error(c, "%s: expected int", __func__); } \
} while(0)

#define BB_CHECK_CLOSURE(a) do { \
	if (!bb_is_closure(a)) { bb_coro_error(c, "%s: expected closure", __func__); } \
} while(0)

#define BB_CHECK_MAP(a) do { \
	if (!bb_is_map(a)) { bb_coro_error(c, "%s: expected map", __func__); } \
} while(0)

#define BB_CHECK_BOOL(a) do { \
	if (!bb_is_bool(a)) { bb_coro_error(c, "%s: expected bool", __func__); } \
} while(0)

/* BB_VARARG_OR_ARRAY — if called with a single array arg, unpack it in-place.
 * Uses ci_arr_head() — valid for non-wrapped arrays (push-only, typical case). */
#define BB_VARARG_OR_ARRAY do { \
	if (nargs == 1 && bb_is_array(args[0])) { \
		ci_array *_voa = (ci_array *)args[0]; \
		nargs = ci_arr_len(_voa); \
		args  = ci_arr_head(_voa); \
	} \
} while(0)

/* ---- native method flag ---- */

#define BB_FN_NATIVE_METHOD (1u << 2)

/* ---- cfunc descriptor ---- */

typedef struct {
	const char *name;
	void       *fn;     /* bb_cfn */
	uint32_t    flags;  /* BB_FN_NATIVE_METHOD, BB_FN_NATIVE_VAR, etc */
} bb_cfunc;

/* create a closure from a cfunc descriptor.
 * flags == 0: BB_FN_NATIVE_METHOD (method, self prepended as a0).
 * flags != 0: use exactly as specified (BB_FN_NATIVE_VAR, etc.). */
static bb_closure *bb_vm_cfunc(bb_vm *vm, const bb_cfunc *desc) {
	bb_function *fn = b_malloc(sizeof(bb_function));
	memset(fn, 0, sizeof(bb_function));

	uint32_t flags = desc->flags ? desc->flags : BB_FN_NATIVE_METHOD;
	fn->flags = BB_FN_NATIVE | flags;
	fn->name  = bb_vm_istring(vm, desc->name, (uint32_t)strlen(desc->name));

	if (flags & BB_FN_NATIVE_VAR)
		fn->cfn_var = (bb_cfn_var)desc->fn;
	else
		fn->cfn = (bb_cfn)desc->fn;

	bb_closure *cl = ci_new(CI_BB_CLOSURE);
	if (!cl)
		bb_vm_error(vm, "cfunc: out of memory");
	cl->fn   = fn;
	cl->self = NULL;
	return cl;
}

/* register an array of cfuncs into a map */
static void bb_func2map(bb_vm *vm, ci_map *map, const bb_cfunc *lib, uint32_t count) {
	for (uint32_t i = 0; i < count; i++) {
		bb_closure *cl = bb_vm_cfunc(vm, &lib[i]);
		ci_map_put(map, cl->fn->name, (ci_ptr)cl);
	}
}
