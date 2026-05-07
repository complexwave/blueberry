/*
 * blueberry.c — BlueBerryVM runtime
 *
 * Build:
 *   make blueberry
 *
 * Usage:
 *   ./blueberry <file.cbc>
 *
 * Loads and executes Citrin bytecode files produced by ./encoder.
 */

#include <stdarg.h>

#ifndef BB_CBC_ONLY
#define BYTECODE_NO_MAIN
#define ENCODER_NO_MAIN
#include "encoder.c"
#else
#include "ciobj.c"
#include "ci_number.c"
#endif



#include "blueberry.h"

/* #define BB_VM_DEBUG */
#ifdef BB_VM_DEBUG
#  define VM_DBG(...) printf(__VA_ARGS__)
#else
#  define VM_DBG(...) ((void)0)
#endif

static void bb_vm_execute(bb_coro *c);
bb_cached_op* bb_function_ops(bb_function *fn);
static void bb_coro_dump_stack(bb_coro *c, int dumpregs);
static void bb_coro_error(bb_coro *c, const char *fmt, ...);
static void bb_coro_destructor(void *ptr, tg_arena_t *arena);
static bb_unit *bb_vm_loadbytecode(bb_vm *vm, const uint8_t *buf, uint32_t len);
#ifndef BB_CBC_ONLY
static uint8_t *bb_compile_ci_file(const char *ci_path, uint32_t *out_len);
#endif

/* ================================================================
 *  Falsy / error
 * ================================================================ */


#define bb_error(...) do { \
	fprintf(stderr, "vm error: " __VA_ARGS__); \
	fputc('\n', stderr); \
	exit(1); \
} while(0)

__attribute__((noreturn))
static void bb_vm_error(bb_vm *vm, const char *msg) {
	(void)vm;
	fprintf(stderr, "vm error: %s\n", msg);
	exit(1);
}


/* ================================================================
 *  VM lifecycle
 * ================================================================ */

static void bb_vm_destructor(void *ptr, tg_arena_t *arena) {
	(void)arena;
	(void)ptr;
	/* TODO: teardown strings/globals/units */
}


static void bb_vm_types_register(void) {
	static const tg_arena_ops vm_ops   = { .destructor = bb_vm_destructor };
	static const tg_arena_ops coro_ops = { .destructor = bb_coro_destructor };
	ci_register_ops(CI_BB_VM,      sizeof(bb_vm),      &vm_ops);
	ci_register_ops(CI_BB_CORO,    sizeof(bb_coro),    &coro_ops);
	ci_register    (CI_BB_CLOSURE, sizeof(bb_closure));
}

static bb_vm *bb_vm_new(void) {
	bb_vm *vm = ci_new(CI_BB_VM);
	if (!vm)
		bb_error("bb_vm_new: out of memory");
	vm->strings    = ci_map_ident_new(64);
	vm->globals    = ci_map_ident_new(16);
	vm->units      = ci_arr_new(4);
	vm->prototypes = ci_map_ident_new(16);
	return vm;
}

static bb_closure *bb_vm_closure(bb_vm *vm, bb_function *fn) {
	bb_closure *cl = ci_new(CI_BB_CLOSURE);
	if (!cl)
		bb_vm_error(vm, "closure: out of memory");
	cl->fn = fn;
	cl->self = NULL;
	return cl;
}


static inline ci_ptr bb_closure_self(bb_closure *cl) {
	return cl->self ? cl->self : cl;
}

static void bb_vm_free(bb_vm *vm) {
	/* TODO: proper teardown of units/functions/coros */
	(void)vm;
}

static ci_ptr bb_vm_istring(bb_vm *vm, const char *s, uint32_t len) {
	ci_str *tmp = ci_str_new(len);
	if (!tmp)
		bb_vm_error(vm, "istring: out of memory");
	ci_str_append(tmp, s, len);

	ci_ptr existing = ci_map_find(vm->strings, tmp);
	if (existing) {
		ci_dec(tmp);
		return existing;
	}

	ci_map_put(vm->strings, tmp, tmp);
	ci_nocnt(tmp);
	return tmp;
}


#include "blueberry_vm/intern_str.c"
#include "blueberry_vm/types.c"

// calude: refactor this to
// *bb_vm_native_function(vm, name, cfn, flags)
// annd then those 2 variants are static inline calls to it

static bb_closure *bb_vm_native(bb_vm *vm, const char *name, bb_cfn cfn) {
	bb_function *fn = b_malloc(sizeof(bb_function));
	memset(fn, 0, sizeof(bb_function));
	fn->flags = BB_FN_NATIVE;
	fn->name  = bb_vm_istring(vm, name, (uint32_t)strlen(name));
	fn->cfn   = cfn;

	bb_closure *cl = ci_new(CI_BB_CLOSURE);
	if (!cl)
		bb_vm_error(vm, "native: out of memory");
	cl->fn = fn;
	return cl;
}

static bb_closure *bb_vm_native_var(bb_vm *vm, const char *name, bb_cfn_var cfn) {
	bb_function *fn = b_malloc(sizeof(bb_function));
	memset(fn, 0, sizeof(bb_function));
	fn->flags   = BB_FN_NATIVE | BB_FN_NATIVE_VAR;
	fn->name    = bb_vm_istring(vm, name, (uint32_t)strlen(name));
	fn->cfn_var = cfn;

	bb_closure *cl = ci_new(CI_BB_CLOSURE);
	if (!cl)
		bb_vm_error(vm, "native: out of memory");
	cl->fn = fn;
	return cl;
}

/* ================================================================
 *  Prototype-chain lookup
 * ================================================================ */

static inline ci_ptr bb_map_proto_find(bb_vm *vm, const ci_map *m, ci_ptr key) {
	(void)vm;
	for (const ci_map *cur = m; cur; cur = (const ci_map *)cur->prototype) {
		ci_ptr val = ci_map_find(cur, key);
		if (val)
			return val;
	}
	return NULL;
}

/* generic prototype lookup: NULL→null, map→walk proto chain, all others→arena ops */
static inline ci_ptr bb_proto_find(bb_vm *vm, ci_ptr obj, ci_ptr key) {
	(void)vm;

	if (!obj)
		return NULL;

	if (CI_IS_MAP(obj))
		return bb_map_proto_find(vm, (const ci_map *)obj, key);

	if (CI_IS_OBJECT(obj)) {
		ci_map *proto = bb_obj_arena_prototype(obj);
		if (proto)
			return bb_map_proto_find(vm, proto, key);
	}

	return NULL;
}

/* ================================================================
 *  Native functions
 * ================================================================ */

static void bb_print_val(ci_ptr v) {
	if (!v)
		printf("null");
	else if (CI_IS_INT(v))
		printf("%ld", CI_INT(v));
	else if (CI_IS_BOOL(v))
		printf("%s", v == CI_BOOL(1) ? "true" : "false");
	else if (CI_IS_ANY_STR(v))
		printf("%.*s", (int)ci_str_len(v), (char *)ci_str_head(v));
	else if (CI_IS_NUMBER(v)) {
		printf("<number:%p:", (void *)v);
		ci_number_print(v);
		printf(">");
	}
	else
		printf("<obj:%p>", (void *)v);
}

static ci_ptr bb_native_print(bb_coro *c, ci_ptr self, size_t n, ci_ptr *args) {
	for(size_t i = 0; i < n; i++){
		bb_print_val(args[i]);
		printf(" ");
	}
	printf("\n");
	return NULL;
}


static ci_ptr bb_native_require(bb_coro_arg *c, ci_ptr a0, ci_ptr a1, ci_ptr a2) {
	(void)a1; (void)a2;
	if (!CI_IS_ANY_STR(a0))
		bb_coro_error(c, "require: argument must be a string");

	char path[1024];
	size_t len = ci_str_len(a0);
	if (len >= sizeof(path))
		bb_coro_error(c, "require: path too long");
	memcpy(path, ci_str_head(a0), len);
	path[len] = '\0';

#ifndef BB_CBC_ONLY
	uint32_t blen;
	uint8_t *buf = bb_compile_ci_file(path, &blen);
	if (!buf)
		bb_coro_error(c, "require: cannot compile '%s'", path);

	bb_unit *unit = bb_vm_loadbytecode(c->vm, buf, blen);
	free(buf);

	bb_function *main_fn = (bb_function *)ci_arr_index(unit->functions, 0);
	bb_closure *cl = bb_vm_closure(c->vm, main_fn);
	return (ci_ptr)cl;
#else
	bb_coro_error(c, "require: not supported in cbc-only mode");
#endif
}

static ci_ptr bb_native_stacktrace(bb_coro *c, ci_ptr a0, ci_ptr a1, ci_ptr a2) {
	(void)a1; (void)a2;
	int dumpregs = 0;
	if (CI_IS_INT(a0))
		dumpregs = (int)CI_INT(a0);
	bb_coro_dump_stack(c, dumpregs);
	return NULL;
}


/* ================================================================
 *  Bytecode loader
 *
 *  .cbc file format (little-endian):
 *    "CIBC"              4 B  magic
 *    version             u16  = 1
 *    string_count        u16
 *    function_count      u16
 *    --- string table ---
 *    [len u16][bytes]  × string_count          (no null terminator)
 *    --- functions ---
 *    [name_idx u16][arg_count u8][reg_count u8]
 *    [local_count u16]                          (0 for now)
 *    [op_count u16]
 *    [...binary opcodes: op_count × 4 bytes...]
 *
 *  Opcode wire format — first byte: [subtype:2][opnum:6]
 *    subtype 00  RRR    [op][dst][src1][src2]   4 B
 *    subtype 01  RRU8   [op][dst][src1][imm8]   4 B
 *    subtype 10  RU16   [op][dst][imm_lo][imm_hi] 4 B (LE)
 *    subtype 11  VAR    [op][b1][extra_words][0] + extra_words × 4 B
 * ================================================================ */

#include "blueberry_vm/bytecode_load.c"

#include "blueberry_vm/coro.c"

/* ================================================================
 *  Opcode handlers
 * ================================================================ */

#include "blueberry_vm/api.c"
#include "blueberry_vm/proto/array.c"
#include "blueberry_vm/proto/string.c"
#include "blueberry_vm/proto/btree.c"
#include "blueberry_vm/proto/coro.c"
#include "blueberry_vm/proto/number.c"
#include "blueberry_vm/opcodes.c"





/* ================================================================
 *  Dump — reuses encoder.c decode pattern for roundtrip verification
 * ================================================================ */

#include "blueberry_vm/dump.c"

/* ================================================================
 *  Execute — non-recursive, loop-based
 * ================================================================ */


/* ================================================================
 *  Fast dispatch — precached {fnptr, ctx} array
 * ================================================================ */



/*
 * ctx layout (b0 stripped — fnptr already knows the op):
 *   RRR:  [d:8][a:8][b:8][0:8]
 *   RI:   [d:8][a:8][imm16:16]   — unified for both RRI8 and RI16
 *
 * During precache, RRI8 imm8 is widened to imm16 in the same slot.
 */

#define VM_OP __attribute__((preserve_none))

#define VM_OP_ACCESS_STACK       ci_ptr *sk = c->fast_stack;
#define VM_OP_STACK(idx)         sk[idx]
#define VM_OP_SET_STACK(idx, val) ci_dec(VM_OP_STACK(idx)); VM_OP_STACK(idx) = val;

#define BB_DISPATCH_NEXT(c) \
	bb_cached_op *op = c->pc++;\
	[[clang::musttail]] return op->fn(c, op->a, op->b, op->c);\



#define BB_ST_RRR  (0 << 6)
#define BB_ST_RRI  (1 << 6)
#define BB_ST_RRS  (2 << 6)
#define BB_ST_VAR  (3 << 6)

#define VM_FAST_RRR(label, impl) \
VM_OP static void __vmop_##label##_rrr(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, vm_dipatch_arg _c) { \
	VM_OP_ACCESS_STACK; \
	ci_ptr arg_b = VM_OP_STACK(b); \
	ci_ptr arg_c = VM_OP_STACK(_c); \
	ci_ptr r = impl(c, arg_b, arg_c); \
	ci_inc(r); \
	VM_OP_SET_STACK(a, r); \
	BB_DISPATCH_NEXT(c);\
}

#define VM_FAST_RRI(label, impl) \
VM_OP static void __vmop_##label##_rri(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, vm_dipatch_arg _c) { \
	VM_OP_ACCESS_STACK; \
	ci_ptr arg_b = VM_OP_STACK(b); \
	ci_ptr arg_imm = CI_PACKINT((int32_t)_c); \
	ci_ptr r = impl(c, arg_b, arg_imm); \
	ci_inc(r); \
	VM_OP_SET_STACK(a, r); \
	BB_DISPATCH_NEXT(c);\
}

#define VM_FAST_RRS(label, impl) \
VM_OP static void __vmop_##label##_rrs(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, vm_dipatch_arg _c) { \
	VM_OP_ACCESS_STACK; \
	ci_ptr arg_b = VM_OP_STACK(b); \
	bb_function *fn = bb_coro_frame_function(bb_coro_frame_top(c)); \
	ci_ptr arg_key = fn->unit->str2intern[_c]; \
	ci_ptr r = impl(c, arg_b, arg_key); \
	ci_inc(r); \
	VM_OP_SET_STACK(a, r); \
	BB_DISPATCH_NEXT(c);\
}

#define VM_FAST_VAR(label, impl) \
VM_OP static void __vmop_##label##_var(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, vm_dipatch_arg _c) { \
	impl(c, a, b, (uint8_t *)_c); \
	BB_DISPATCH_NEXT(c);\
}


#include "blueberry_vm/advanced_opcodes.c"



VM_FAST_RRR(add, bb_op_add)       VM_FAST_RRI(add, bb_op_add)
VM_FAST_RRR(sub, bb_op_sub)       VM_FAST_RRI(sub, bb_op_sub)
VM_FAST_RRR(mul, bb_op_mul)       VM_FAST_RRI(mul, bb_op_mul)
VM_FAST_RRR(div, bb_op_div)       VM_FAST_RRI(div, bb_op_div)
VM_FAST_RRR(mod, bb_op_mod)       VM_FAST_RRI(mod, bb_op_mod)
VM_FAST_RRR(pow, bb_op_pow)       VM_FAST_RRI(pow, bb_op_pow)
VM_FAST_RRR(neg, bb_op_neg)
VM_FAST_RRR(op_not, bb_op_not)
VM_FAST_RRR(bin_inv, bb_op_bin_inv)
VM_FAST_RRR(bin_or, bb_op_bin_or)       VM_FAST_RRI(bin_or, bb_op_bin_or)
VM_FAST_RRR(bin_and, bb_op_bin_and)     VM_FAST_RRI(bin_and, bb_op_bin_and)
VM_FAST_RRR(bin_xor, bb_op_bin_xor)     VM_FAST_RRI(bin_xor, bb_op_bin_xor)
VM_FAST_RRR(bin_lshift, bb_op_bin_lshift) VM_FAST_RRI(bin_lshift, bb_op_bin_lshift)
VM_FAST_RRR(bin_rshift, bb_op_bin_rshift) VM_FAST_RRI(bin_rshift, bb_op_bin_rshift)
VM_FAST_RRR(eq, bb_op_eq)         VM_FAST_RRI(eq, bb_op_eq)
VM_FAST_RRR(neq, bb_op_neq)       VM_FAST_RRI(neq, bb_op_neq)
VM_FAST_RRR(gt, bb_op_gt)         VM_FAST_RRI(gt, bb_op_gt)
VM_FAST_RRR(lt, bb_op_lt)         VM_FAST_RRI(lt, bb_op_lt)
VM_FAST_RRR(gt_eq, bb_op_gt_eq)   VM_FAST_RRI(gt_eq, bb_op_gt_eq)
VM_FAST_RRR(lt_eq, bb_op_lt_eq)   VM_FAST_RRI(lt_eq, bb_op_lt_eq)
VM_FAST_RRR(notnull, bb_op_notnull) VM_FAST_RRI(notnull, bb_op_notnull)
VM_FAST_RRR(move, bb_op_move)
VM_FAST_RRR(hashaccess, bb_op_hashaccess_rrr)  VM_FAST_RRS(hashaccess, bb_op_hashaccess_rrr)
VM_FAST_RRR(mapaccess, bb_op_mapaccess)
VM_FAST_RRR(arraccess, bb_op_arraccess)
/* iterinit/iterstep are custom handlers in advanced_opcodes.c */
VM_FAST_RRR(methodbind, bb_op_methodbind)

VM_FAST_RRR(loadnull, bb_op_loadnull)
VM_FAST_RRR(loadtrue, bb_op_loadtrue)
VM_FAST_RRR(loadfalse, bb_op_loadfalse)

/* --- VAR ops --- */
VM_FAST_VAR(newmap,     bb_op_newmap_var)
VM_FAST_VAR(newarray,   bb_op_newarray_var)
VM_FAST_VAR(hashaccess, bb_op_hashaccess_var)
VM_FAST_VAR(loadnull,   bb_op_loadnull_var)
VM_FAST_VAR(moveto,     bb_op_moveto_var)
VM_FAST_VAR(movefrom,   bb_op_movefrom_var)

/* --- VAR loadint (64-bit, boxed) --- */

VM_OP static void __vmop_loadint_var(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, vm_dipatch_arg _c) {
	(void)b;
	VM_OP_ACCESS_STACK;
	uint8_t *payload = (uint8_t *)_c;
	uint64_t u;
	memcpy(&u, payload, 8);
	ci_number *n = ci_number_new(CI_NUM_I128);
	n->i128 = (__int128)(int64_t)u;
	VM_OP_SET_STACK(a, (ci_ptr)n);
	BB_DISPATCH_NEXT(c);
}

/* --- VAR loaddouble (64-bit float) --- */

VM_OP static void __vmop_loaddouble_var(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, vm_dipatch_arg _c) {
	(void)b;
	VM_OP_ACCESS_STACK;
	uint8_t *payload = (uint8_t *)_c;
	ci_number *n = ci_number_new(CI_NUM_F64);
	memcpy(&n->f64, payload, sizeof(double));
	VM_OP_SET_STACK(a, (ci_ptr)n);
	BB_DISPATCH_NEXT(c);
}

/* --- RRI-only ops (no src reg, just dst + imm32) --- */

VM_OP static void __vmop_loadint(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, vm_dipatch_arg _c) {
	(void)b;
	VM_OP_ACCESS_STACK;
	VM_OP_SET_STACK(a, CI_PACKINT((int32_t)_c));

	BB_DISPATCH_NEXT(c);
}

VM_OP static void __vmop_loadstr(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, vm_dipatch_arg _c) {
	(void)b;
	VM_OP_ACCESS_STACK;
	bb_function *fn = bb_coro_frame_function(bb_coro_frame_top(c));
	VM_OP_SET_STACK(a, fn->unit->str2intern[_c]);

	BB_DISPATCH_NEXT(c);
}

VM_OP static void __vmop_loadfn(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, vm_dipatch_arg _c) {
	(void)b;
	VM_OP_ACCESS_STACK;
	bb_function *fn = bb_coro_frame_function(bb_coro_frame_top(c));
	bb_function *f = ci_arr_index(fn->unit->functions, _c);
	bb_closure *cl = bb_vm_closure(c->vm, f);
	VM_OP_SET_STACK(a, (ci_ptr)cl);

	BB_DISPATCH_NEXT(c);
}

VM_OP static void __vmop_jmp(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, vm_dipatch_arg _c) {
	(void)a;
	(void)b;
	c->pc = c->ops_base + _c;

	BB_DISPATCH_NEXT(c);
}

VM_OP static void __vmop_jmpf(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, vm_dipatch_arg _c) {
	(void)b;
	VM_OP_ACCESS_STACK;
	if (CI_IS_FALSY(VM_OP_STACK(a)))
		c->pc = c->ops_base + _c;

	BB_DISPATCH_NEXT(c);
}

VM_OP static void __vmop_jmpt(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, vm_dipatch_arg _c) {
	(void)b;
	VM_OP_ACCESS_STACK;
	if (!CI_IS_FALSY(VM_OP_STACK(a)))
		c->pc = c->ops_base + _c;

	BB_DISPATCH_NEXT(c);
}

/* --- special / sentinel --- */

VM_OP static void __vmop_arraystore(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, vm_dipatch_arg _c) {
	VM_OP_ACCESS_STACK;
	bb_op_arraystore(c, VM_OP_STACK(a), VM_OP_STACK(b), VM_OP_STACK(_c));

	BB_DISPATCH_NEXT(c);
}

VM_OP static void __vmop_hashstore(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, vm_dipatch_arg _c) {
	VM_OP_ACCESS_STACK;
	bb_op_hashstore(c, VM_OP_STACK(a), VM_OP_STACK(b), VM_OP_STACK(_c));

	BB_DISPATCH_NEXT(c);
}

VM_OP static void __vmop_hashstore_rrs(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, vm_dipatch_arg _c) {
	VM_OP_ACCESS_STACK;
	bb_function *fn = bb_coro_frame_function(bb_coro_frame_top(c));
	ci_ptr key = fn->unit->str2intern[_c];
	bb_op_hashstore(c, VM_OP_STACK(a), key, VM_OP_STACK(b));

	BB_DISPATCH_NEXT(c);
}

VM_OP static void __vmop_nop(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, vm_dipatch_arg _c) {
	(void)a; (void)b; (void)_c;

	BB_DISPATCH_NEXT(c);
}

VM_OP static void __vmop_opcode_error(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, vm_dipatch_arg _c) {
	(void)a; (void)b; (void)_c;

	printf("Codegen error: no opcode\n");
	exit(1);
}


VM_OP static void bb_vm_end_dispatch(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, vm_dipatch_arg _c) {
	(void)a; (void)b; (void)_c;

	return;
}

VM_OP static void __vmop_exit(bb_coro *c, vm_dipatch_arg a, vm_dipatch_arg b, vm_dipatch_arg _c) {
	(void)c; (void)a; (void)b; (void)_c;
	exit(0);
	BB_DISPATCH_NEXT(c);
}

/* --- dispatch table: 256 entries indexed by raw first byte --- */

static bb_fast_fn bb_fast_table[256];

static void bb_fast_table_init(void) {
	memset(bb_fast_table, 0, sizeof(bb_fast_table));

	#define FT_RRR(op, label) bb_fast_table[BB_ST_RRR | (op)] = __vmop_##label##_rrr
	#define FT_RRI(op, label) bb_fast_table[BB_ST_RRI | (op)] = __vmop_##label##_rri
	#define FT_RRS(op, label) bb_fast_table[BB_ST_RRS | (op)] = __vmop_##label##_rrs
	#define FT_RRI_ONLY(op, label) bb_fast_table[BB_ST_RRI | (op)] = __vmop_##label
	#define FT_VAR(op, label) bb_fast_table[BB_ST_VAR | (op)] = __vmop_##label##_var

	FT_RRR(B_ADD, add);       FT_RRI(B_ADD, add);
	FT_RRR(B_SUB, sub);       FT_RRI(B_SUB, sub);
	FT_RRR(B_MUL, mul);       FT_RRI(B_MUL, mul);
	FT_RRR(B_DIV, div);       FT_RRI(B_DIV, div);
	FT_RRR(B_MOD, mod);       FT_RRI(B_MOD, mod);
	FT_RRR(B_POW, pow);       FT_RRI(B_POW, pow);
	FT_RRR(B_NEG, neg);
	FT_RRR(B_NOT, op_not);
	FT_RRR(B_BIN_INV, bin_inv);
	FT_RRR(B_BIN_OR, bin_or);       FT_RRI(B_BIN_OR, bin_or);
	FT_RRR(B_BIN_AND, bin_and);     FT_RRI(B_BIN_AND, bin_and);
	FT_RRR(B_BIN_XOR, bin_xor);     FT_RRI(B_BIN_XOR, bin_xor);
	FT_RRR(B_BIN_LSHIFT, bin_lshift); FT_RRI(B_BIN_LSHIFT, bin_lshift);
	FT_RRR(B_BIN_RSHIFT, bin_rshift); FT_RRI(B_BIN_RSHIFT, bin_rshift);
	FT_RRR(B_EQ, eq);         FT_RRI(B_EQ, eq);
	FT_RRR(B_NEQ, neq);       FT_RRI(B_NEQ, neq);
	FT_RRR(B_GT, gt);         FT_RRI(B_GT, gt);
	FT_RRR(B_LT, lt);         FT_RRI(B_LT, lt);
	FT_RRR(B_GT_EQ, gt_eq);   FT_RRI(B_GT_EQ, gt_eq);
	FT_RRR(B_LT_EQ, lt_eq);   FT_RRI(B_LT_EQ, lt_eq);
	FT_RRR(B_NOTNULL, notnull); FT_RRI(B_NOTNULL, notnull); 
	FT_RRR(B_MOVE, move);
	FT_RRR(B_HASHACCESS, hashaccess);  FT_RRS(B_HASHACCESS, hashaccess);
	FT_RRR(B_MAPACCESS, mapaccess);
	FT_RRR(B_ARRACCESS, arraccess);
	bb_fast_table[BB_ST_RRR | B_ITERINIT] = __vmop_iterinit;
	bb_fast_table[BB_ST_RRI | B_ITERSTEP] = __vmop_iterstep;
	FT_RRR(B_METHODBIND, methodbind);
	FT_RRR(B_LOADNULL, loadnull);
	FT_RRR(B_LOADTRUE, loadtrue);
	FT_RRR(B_LOADFALSE, loadfalse);
	
	
	FT_RRI_ONLY(B_LOADINT, loadint);
	FT_RRI_ONLY(B_LOADSTR, loadstr);
	FT_RRI_ONLY(B_LOADFN, loadfn);
	FT_RRI_ONLY(B_JMP, jmp);
	FT_RRI_ONLY(B_JMPF, jmpf);
	FT_RRI_ONLY(B_JMPT, jmpt);

	bb_fast_table[BB_ST_RRR | B_ARRAYSTORE] = __vmop_arraystore;
	bb_fast_table[BB_ST_RRI | B_ARRAYSTORE] = __vmop_arraystore;
	bb_fast_table[BB_ST_RRR | B_HASHSTORE]  = __vmop_hashstore;
	bb_fast_table[BB_ST_RRI | B_HASHSTORE]  = __vmop_hashstore;
	bb_fast_table[BB_ST_RRS | B_HASHSTORE]  = __vmop_hashstore_rrs;

	bb_fast_table[BB_ST_VAR | B_RETURN]  = bb_op_return_var;
	bb_fast_table[BB_ST_RRR | B_CALL]    = __vmop_call;
	FT_VAR(B_MOVETO,     moveto);
	FT_VAR(B_MOVEFROM,   movefrom);
	
	FT_VAR(B_NEWMAP,      newmap);
	FT_VAR(B_NEWARRAY,    newarray);
	FT_VAR(B_HASHACCESS,  hashaccess);
	FT_VAR(B_LOADNULL,    loadnull);

	bb_fast_table[BB_ST_VAR | B_LOADINT]    = __vmop_loadint_var;
	bb_fast_table[BB_ST_VAR | B_LOADDOUBLE] = __vmop_loaddouble_var;

	#undef FT_RRR
	#undef FT_RRI
	#undef FT_RRS
	#undef FT_RRI_ONLY
	#undef FT_VAR
}

/* --- build cached ops from bytecode --- */

static bb_cached_op *bb_build_cached(bb_function *fn) {
	/* one cached_op per instruction; instructions are at least 8 bytes */
	uint32_t max_wc = fn->code_length / 8 + 4;
	bb_cached_op *ops = b_malloc(max_wc * sizeof(bb_cached_op));

	uint32_t bi = 0;   /* byte cursor into fn->code */
	uint32_t wi = 0;   /* word index into ops[]     */

	while (bi < fn->code_length) {
		uint8_t  b0      = fn->code[bi];
		uint8_t  b1      = fn->code[bi + 1];
		uint8_t  b2      = fn->code[bi + 2];
		uint8_t  b3      = fn->code[bi + 3];
		uint32_t imm     = (uint32_t)fn->code[bi+4]
		                 | ((uint32_t)fn->code[bi+5] << 8)
		                 | ((uint32_t)fn->code[bi+6] << 16)
		                 | ((uint32_t)fn->code[bi+7] << 24);
		uint8_t  subtype = b0 >> 6;

		bb_fast_fn f  = bb_fast_table[b0];
		if(!f){
			//bb_error("error endoing opcode - fn not found");

			printf("error endoing opcode - fn not found");
			ops[wi].fn    = __vmop_opcode_error;
			
			wi++;
			break;
		}
		ops[wi].fn    = f ? f : __vmop_nop;

		switch (subtype) {
		case 0: /* RRR: a=r1, b=r2, c=r3 */
			ops[wi].a = b1;
			ops[wi].b = b2;
			ops[wi].c = b3;
			bi += 8; wi++;
			break;
		case 1: /* RRI: a=r1, b=r2, c=imm32 */
			ops[wi].a = b1;
			ops[wi].b = b2;
			ops[wi].c = imm;
			bi += 8; wi++;
			break;
		case 2: /* RRS: a=r1, b=r2, c=sid (imm) */
			ops[wi].a = b1;
			ops[wi].b = b2;
			ops[wi].c = imm;
			bi += 8; wi++;
			break;
		case 3: { /* VAR: a=r1, b=r2, c=ptr to payload (first word after header) */
			uint8_t nwords = b3;
			ops[wi].a = b1;
			ops[wi].b = b2;
			ops[wi].c = (vm_dipatch_arg)(fn->code + bi + 8);
			bi += 8 + nwords * 4;
			wi++;
			break;
		}
		}
	}

	ops[wi].fn = bb_op_return_var;
	ops[wi].a = ops[wi].b = ops[wi].c = 0;
	
	return ops;
}

#include "blueberry_vm/function.c"

static void bb_vm_execute(bb_coro *c) {
	bb_cached_op *op = c->pc++;
	op->fn(c, op->a, op->b, op->c);
}

/* ================================================================
 *  Coro call API
 * ================================================================ */


/* varargs call macros */
#define BB_CORO_VCALL(c, cl, args, nargs) \
	ci_ptr __bb_vrets[32] = {}; \
	uint32_t __bb_vret_cnt = 0; \
	do { \
		__bb_vret_cnt = 32; \
		bb_coro_call_var(c, cl, args, nargs, __bb_vrets, __bb_vret_cnt); \
		__bb_vret_cnt = (c)->lastreturn_cnt < 32 ? (c)->lastreturn_cnt : 32; \
	} while(0)

#define BB_VRET_CNT  __bb_vret_cnt
#define BB_VRET(i)   __bb_vrets[i]
#define BB_VRET_FINALIZE ci_dec_multi(__bb_vrets, 32)


/* ================================================================
 *  File I/O
 * ================================================================ */

#include "blueberry_vm/util.c"
#include "blueberry_vm/lib/io.c"
#include "blueberry_vm/lib/cma.c"
#include "blueberry_vm/lib/map.c"
#include "blueberry_vm/lib/proto.c"
#include "blueberry_vm/lib/callapi.c"
#include "blueberry_vm/lib/math.c"

/* ================================================================
 *  Compile .ci to .cbc
 * ================================================================ */

#ifndef BB_CBC_ONLY
#include "blueberry_vm/compile.c"
#endif

/* ================================================================
 *  Main
 * ================================================================ */

#ifndef BB_NO_MAIN
int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: blueberry [-d] <file.cbc|file.ci>\n");
		return 1;
	}

	int dump = 0;
	int file_start = 1;
	if (argc > 2 && strcmp(argv[1], "-d") == 0) {
		dump = 1;
		file_start = 2;
	}

	ci_init();
	ci_str_register();
	ci_arr_register();
	ci_map_register();
	ci_tree_register();
	ci_number_register();
	bb_vm_types_register();
	bb_fast_table_init();

	const char *path = argv[file_start];
	uint32_t len;
	uint8_t *buf = NULL;

	/* detect extension and handle accordingly */
	const char *ext = strrchr(path, '.');
#ifndef BB_CBC_ONLY
	if (ext && strcmp(ext, ".ci") == 0) {
		/* compile .ci to bytecode */
		buf = bb_compile_ci_file(path, &len);
		if (!buf)
			goto shutdown;
	} else
#endif
	{
		/* load .cbc file directly */
		buf = bb_read_file(path, &len);
		if (!buf) {
			fprintf(stderr, "error: cannot read '%s'\n", path);
			goto shutdown;
		}
	}

	if (dump)
		printf("=== %s (%u bytes) ===\n\n", path, len);

	bb_vm *vm = bb_vm_new();

	/* register built-in native functions */
	{
		bb_closure *print_cl = bb_vm_native_var(vm, "print", bb_native_print);
		ci_map_put(vm->globals, print_cl->fn->name, (ci_ptr)print_cl);

		bb_closure *stacktrace_cl = bb_vm_native(vm, "stacktrace", bb_native_stacktrace);
		ci_map_put(vm->globals, stacktrace_cl->fn->name, (ci_ptr)stacktrace_cl);

		bb_closure *require_cl = bb_vm_native(vm, "require", bb_native_require);
		ci_map_put(vm->globals, require_cl->fn->name, (ci_ptr)require_cl);

		bb_closure *type_cl = bb_vm_native(vm, "type", bb_native_type);
		ci_map_put(vm->globals, type_cl->fn->name, (ci_ptr)type_cl);
	}

	/* init built-in prototypes */
	bb_proto_array_init(vm);
	bb_proto_string_init(vm);
	bb_proto_btree_init(vm);
	bb_proto_number_init(vm);

	/* init stdlib */
	bb_lib_io_init(vm);
	bb_lib_cma_init(vm);
	bb_lib_map_init(vm);
	bb_lib_proto_init(vm);
	bb_lib_callapi_init(vm);
	bb_lib_coro_init(vm);
	bb_lib_math_init(vm);

	/* expose script arguments as global argv array */
	{
		int nargs = argc - file_start - 1;
		ci_array *args = ci_arr_new(nargs > 0 ? (uint32_t)nargs : 1);
		for (int j = file_start + 1; j < argc; j++) {
			ci_ptr s = (ci_ptr)ci_str_from_cstr(argv[j]);
			ci_arr_push(args, s);
			ci_dec(s);
		}
		ci_ptr argv_key = bb_vm_istring(vm, "argv", 4);
		ci_map_put(vm->globals, argv_key, (ci_ptr)args);
		ci_dec((ci_ptr)args);
	}

	bb_unit *unit = bb_vm_loadbytecode(vm, buf, len);
	free(buf);

	if (dump)
		bb_dump_unit(unit);

	if (ci_arr_len(unit->functions) == 0) {
		fprintf(stderr, "error: no functions in unit\n");
		bb_vm_free(vm);
		goto shutdown;
	}

	/* Execute the first function (main/global setup) */
	bb_function *main_fn = (bb_function *)ci_arr_index(unit->functions, 0);
	bb_closure *main_cl = bb_vm_closure(vm, main_fn);
	bb_coro *c = bb_coro_new(vm);

	if (dump)
		printf("\n=== execute ===\n");

	bb_coro_call(c, main_cl, NULL, NULL, NULL);
	if (dump)
		bb_coro_dump_stack(c, 1);

	bb_vm_free(vm);
	
	shutdown:

	ci_shutdown();
	return 0;
}
#endif /* BB_NO_MAIN */
