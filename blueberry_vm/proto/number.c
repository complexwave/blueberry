/*
 * blueberry_vm/proto/number.c — number built-in prototype (metaproto)
 *
 * Registers CI_NUMBER with metamethods for add/sub/mul/div.
 * Exposes global number() constructor that boxes ints into ci_number.
 */

static ci_ptr bb_number_meta_add(bb_coro *c, ci_ptr a, ci_ptr b) {
	return (ci_ptr)ci_number_add(a, b);
}

static ci_ptr bb_number_meta_sub(bb_coro *c, ci_ptr a, ci_ptr b) {
	return (ci_ptr)ci_number_sub(a, b);
}

static ci_ptr bb_number_meta_mul(bb_coro *c, ci_ptr a, ci_ptr b) {
	return (ci_ptr)ci_number_mul(a, b);
}

static ci_ptr bb_number_meta_div(bb_coro *c, ci_ptr a, ci_ptr b) {
	return (ci_ptr)ci_number_div(a, b);
}

static ci_ptr bb_number_meta_mod(bb_coro *c, ci_ptr a, ci_ptr b) {
	return (ci_ptr)ci_number_mod(a, b);
}

static ci_ptr bb_number_meta_bin_and(bb_coro *c, ci_ptr a, ci_ptr b) {
	return (ci_ptr)ci_number_and(a, b);
}

static ci_ptr bb_number_meta_bin_or(bb_coro *c, ci_ptr a, ci_ptr b) {
	return (ci_ptr)ci_number_or(a, b);
}

static ci_ptr bb_number_meta_bin_xor(bb_coro *c, ci_ptr a, ci_ptr b) {
	return (ci_ptr)ci_number_xor(a, b);
}

static ci_ptr bb_number_meta_bin_lshift(bb_coro *c, ci_ptr a, ci_ptr b) {
	int amount = CI_IS_INT(b) ? (int)CI_INT(b) : (int)ci_number_to_int(b);
	return ci_number_lshift(a, amount);
}

static ci_ptr bb_number_meta_bin_rshift(bb_coro *c, ci_ptr a, ci_ptr b) {
	int amount = CI_IS_INT(b) ? (int)CI_INT(b) : (int)ci_number_to_int(b);
	return ci_number_rshift(a, amount);
}

static ci_ptr bb_number_meta_bin_inv(bb_coro *c, ci_ptr a, ci_ptr b) {
	return (ci_ptr)ci_number_not(a);
}

static ci_ptr bb_number_meta_neg(bb_coro *c, ci_ptr a, ci_ptr b) {
	return (ci_ptr)ci_number_neg(a);
}

static ci_ptr bb_number_meta_cmp(bb_coro *c, ci_ptr a, ci_ptr b) {
	return CI_PACKINT(ci_number_cmp(a, b));
}

/* number(x) — box a tagged int or return existing ci_number as-is */
static ci_ptr bb_native_number(bb_coro_arg *c, ci_ptr a, ci_ptr b, ci_ptr _) {
	if (CI_IS_NUMBER(a))
		return a;

	if (CI_IS_INT(a)) {
		ci_number *n = ci_number_new(CI_NUM_I128);
		n->i128 = (__int128)CI_INT(a);
		return (ci_ptr)n;
	}

	bb_coro_error(c, "number(): expected int or number");
	return NULL;
}

static void bb_proto_number_init(bb_vm *vm) {
	bb_metaproto *mp = bb_proto_register_meta(vm, "number");

	mp->op_add = bb_number_meta_add;
	mp->op_sub = bb_number_meta_sub;
	mp->op_mul = bb_number_meta_mul;
	mp->op_div = bb_number_meta_div;
	mp->op_mod = bb_number_meta_mod;
	mp->op_bin_and = bb_number_meta_bin_and;
	mp->op_bin_or = bb_number_meta_bin_or;
	mp->op_bin_xor = bb_number_meta_bin_xor;
	mp->op_bin_lshift = bb_number_meta_bin_lshift;
	mp->op_bin_rshift = bb_number_meta_bin_rshift;
	mp->op_bin_inv = bb_number_meta_bin_inv;
	mp->op_neg = bb_number_meta_neg;
	mp->op_cmp = bb_number_meta_cmp;

	bb_set_arena_prototype(CI_NUMBER, &mp->map);

	bb_closure *cl = bb_vm_native(vm, "number", bb_native_number);
	ci_map_put(vm->globals, cl->fn->name, (ci_ptr)cl);
}
