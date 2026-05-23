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
#define bb_is_coro(p)    CI_IS_CORO(p)
#define bb_is_number(p)  (CI_IS_INT(p) || CI_IS_NUMBER(p))

#define BB_NO_OVERFLOW_ADD(a, b, r) do { \
	if (__builtin_add_overflow((a), (b), &(r))) \
		bb_coro_error(c, "%s: int add overflow", __func__); \
} while(0)

#define BB_NO_OVERFLOW_SUB(a, b, r) do { \
	if (__builtin_sub_overflow((a), (b), &(r))) \
		bb_coro_error(c, "%s: int sub overflow", __func__); \
} while(0)

#define BB_NO_OVERFLOW_MUL(a, b, r) do { \
	if (__builtin_mul_overflow((a), (b), &(r))) \
		bb_coro_error(c, "%s: int mul overflow", __func__); \
} while(0)


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

#define BB_CHECK_NUMBER(a) do { \
	if (!bb_is_number(a)) { bb_coro_error(c, "%s: expected number", __func__); } \
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

#define BB_CHECK_CORO(a) do { \
	if (!bb_is_coro(a)) { bb_coro_error(c, "%s: expected coroutine", __func__); } \
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

/* ---- metamethod dispatch ---- */

#define bb_metam(name) offsetof(bb_metaproto, name)

static inline void *bb_proto_get_metamethod(ci_map *proto, size_t offset) {
	return *(void **)((uint8_t *)proto + offset);
}

/* try to find a metamethod on a or b, return the fn pointer (or NULL) */
static inline void *bb_meta_find(bb_coro *c, ci_ptr a, ci_ptr b, size_t offset) {
	if (CI_IS_PTR(a)) {
		ci_map *proto = bb_obj_arena_prototype(a);

		if (CI_IS_MAGIC_PROTO(proto)) {
			void *fn = bb_proto_get_metamethod(proto, offset);
			if (fn) return fn;
		}
	}

	if (CI_IS_PTR(b)) {
		ci_map *proto = bb_obj_arena_prototype(b);

		if (CI_IS_MAGIC_PROTO(proto)) {
			void *fn = bb_proto_get_metamethod(proto, offset);
			if (fn) return fn;
		}
	}

	return NULL;
}

#define BB_META_DISPATCH(c, a, b, name, msg) do { \
	bb_op_fn _fn = (bb_op_fn)bb_meta_find(c, a, b, bb_metam(name)); \
	if (_fn) return _fn(c, a, b); \
	bb_coro_error(c, "%s", msg); \
	__builtin_unreachable(); \
} while(0)

#define BB_META_DISPATCH_INDEX_GET(c, obj, key, msg) do { \
	bb_op_fn _fn = (bb_op_fn)bb_meta_find(c, obj, NULL, bb_metam(index_get)); \
	if (_fn) return _fn(c, obj, key); \
	bb_coro_error(c, "%s", msg); \
} while(0)

#define BB_META_DISPATCH_INDEX_SET(c, obj, key, val, msg) do { \
	bb_op_fn_ext _fn = (bb_op_fn_ext)bb_meta_find(c, obj, NULL, bb_metam(index_set)); \
	if (_fn) { _fn(c, obj, key, val); return; } \
	bb_coro_error(c, "%s", msg); \
} while(0)

/* ---- cfunc descriptor ---- */

typedef struct {
	const char *name;
	void       *fn;     /* bb_cfn or bb_cfn_var */
	uint32_t    flags;  /* 0 = default (bb_cfn), BB_FN_NATIVE_VAR, etc */
} bb_cfunc;

/* create a closure from a cfunc descriptor. */
static bb_closure *bb_vm_cfunc(bb_vm *vm, const bb_cfunc *desc) {
	bb_function *fn = b_malloc(sizeof(bb_function));
	memset(fn, 0, sizeof(bb_function));

	fn->flags = BB_FN_NATIVE | desc->flags;
	fn->name  = bb_vm_istring(vm, desc->name, (uint32_t)strlen(desc->name));

	if (desc->flags & BB_FN_NATIVE_VAR)
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
