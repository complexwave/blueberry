static inline ci_ptr bb_op_loadnull(bb_coro *c, ci_ptr a, ci_ptr b) {
	(void)a; (void)b;
	
	return NULL;
}

#define bb_metam(name) offsetof(bb_metaproto, name)

static inline bb_op_fn bb_proto_get_metamethod(ci_map *proto, size_t offset) {
	return *(bb_op_fn *)((uint8_t *)proto + offset);
}

static inline ci_ptr bb_meta_dispatch(bb_coro *c, ci_ptr a, ci_ptr b, size_t offset, const char *errmsg) {
	if (CI_IS_OBJECT(a)) {
		ci_map *proto = bb_obj_arena_prototype(a);
		if (CI_IS_MAGIC_PROTO(proto)) {
			bb_op_fn fn = bb_proto_get_metamethod(proto, offset);
			if (fn) return fn(c, a, b);
		}
	}

	if (CI_IS_OBJECT(b)) {
		ci_map *proto = bb_obj_arena_prototype(b);
		if (CI_IS_MAGIC_PROTO(proto)) {
			bb_op_fn fn = bb_proto_get_metamethod(proto, offset);
			if (fn) return fn(c, a, b);
		}
	}

	bb_coro_error(c, "%s", errmsg);
	return NULL;
}

#define BB_META_DISPATCH(c, a, b, name, msg) \
	return bb_meta_dispatch(c, a, b, bb_metam(name), msg)

static inline ci_ptr bb_op_add(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b)) {
		intptr_t r;
		
		if (!__builtin_add_overflow((intptr_t)a, (intptr_t)b, &r))
			return (ci_ptr)(r - 1);
		
		return (ci_ptr)ci_number_add(a, b);
	}
	BB_META_DISPATCH(c, a, b, op_add, "ADD: type error");
}

static inline ci_ptr bb_op_sub(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b)) {
		intptr_t r;
		
		if (!__builtin_sub_overflow((intptr_t)a, (intptr_t)b, &r))
			return (ci_ptr)(r + 1);
		
		return (ci_ptr)ci_number_sub(a, b);
	}
	BB_META_DISPATCH(c, a, b, op_sub, "SUB: type error");
}

static inline ci_ptr bb_op_mul(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b)) {
		intptr_t va = CI_INT(a), vb = CI_INT(b);
		intptr_t r;
		
		if (!__builtin_mul_overflow(va, vb, &r))
			return CI_PACKINT(r);
		
		return (ci_ptr)ci_number_mul(a, b);
	}
	BB_META_DISPATCH(c, a, b, op_mul, "MUL: type error");
}

static inline ci_ptr bb_op_div(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b)) {
		intptr_t bv = CI_INT(b);
		
		if (bv == 0)
			bb_coro_error(c, "DIV: division by zero");
		
		return CI_PACKINT(CI_INT(a) / bv);
	}
	BB_META_DISPATCH(c, a, b, op_div, "DIV: type error");
}

static inline ci_ptr bb_op_mod(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b)) {
		intptr_t bv = CI_INT(b);
		
		if (bv == 0){
			bb_coro_error(c, "MOD: division by zero");
		}
		
		return CI_PACKINT(CI_INT(a) % bv);
	}
	BB_META_DISPATCH(c, a, b, op_mod, "MOD: type error");
}

static inline ci_ptr bb_op_pow(bb_coro *c, ci_ptr a, ci_ptr b) {
	(void)c;
	return ci_number_pow(a, b);
}

static inline ci_ptr bb_op_neg(bb_coro *c, ci_ptr a, ci_ptr b) {
	(void)b;
	if (CI_IS_INT(a))
		return CI_PACKINT(-CI_INT(a));
	BB_META_DISPATCH(c, a, NULL, op_neg, "NEG: type error");
}

static inline ci_ptr bb_op_not(bb_coro *c, ci_ptr a, ci_ptr b) {
	(void)c; (void)b;
	return CI_IS_FALSY(a) ? CI_BOOL(1) : CI_BOOL(0);
}

static inline ci_ptr bb_op_bin_inv(bb_coro *c, ci_ptr a, ci_ptr b) {
	(void)b;
	if (CI_IS_INT(a))
		return CI_PACKINT(~CI_INT(a));
	BB_META_DISPATCH(c, a, NULL, op_bin_inv, "BIN_INV: type error");
}

static inline ci_ptr bb_op_bin_or(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b))
		return CI_PACKINT(CI_INT(a) | CI_INT(b));
	BB_META_DISPATCH(c, a, b, op_bin_or, "BIN_OR: type error");
}

static inline ci_ptr bb_op_bin_and(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b))
		return CI_PACKINT(CI_INT(a) & CI_INT(b));
	BB_META_DISPATCH(c, a, b, op_bin_and, "BIN_AND: type error");
}

static inline ci_ptr bb_op_bin_xor(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b))
		return CI_PACKINT(CI_INT(a) ^ CI_INT(b));
	BB_META_DISPATCH(c, a, b, op_bin_xor, "BIN_XOR: type error");
}

static inline ci_ptr bb_op_bin_lshift(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b))
		return CI_PACKINT(CI_INT(a) << CI_INT(b));
	BB_META_DISPATCH(c, a, b, op_bin_lshift, "BIN_LSHIFT: type error");
}

static inline ci_ptr bb_op_bin_rshift(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b))
		return CI_PACKINT(CI_INT(a) >> CI_INT(b));
	BB_META_DISPATCH(c, a, b, op_bin_rshift, "BIN_RSHIFT: type error");
}

static inline ci_ptr bb_cmp_dispatch(bb_coro *c, ci_ptr a, ci_ptr b) {
	ci_ptr r = bb_meta_dispatch(c, a, b, bb_metam(op_cmp), "CMP: type error");
	return r;
}

static inline ci_ptr bb_op_eq(bb_coro *c, ci_ptr a, ci_ptr b) {
	bool eq = (a == b);
	if (!eq && (CI_IS_NUMBER(a) || CI_IS_NUMBER(b))) {
		intptr_t r = CI_INT(bb_cmp_dispatch(c, a, b));
		eq = (r == 0);
	}
	return CI_BOOL(eq);
}

static inline ci_ptr bb_op_neq(bb_coro *c, ci_ptr a, ci_ptr b) {
	return CI_BOOL_INV(bb_op_eq(c, a, b));
}

static inline ci_ptr bb_op_gt(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b))
		return CI_BOOL(CI_INT(a) > CI_INT(b));
	intptr_t r = CI_INT(bb_cmp_dispatch(c, a, b));
	return CI_BOOL(r > 0);
}

static inline ci_ptr bb_op_lt(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b))
		return CI_BOOL(CI_INT(a) < CI_INT(b));
	intptr_t r = CI_INT(bb_cmp_dispatch(c, a, b));
	return CI_BOOL(r < 0);
}

static inline ci_ptr bb_op_gt_eq(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b))
		return CI_BOOL(CI_INT(a) >= CI_INT(b));
	intptr_t r = CI_INT(bb_cmp_dispatch(c, a, b));
	return CI_BOOL(r >= 0);
}

static inline ci_ptr bb_op_lt_eq(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b))
		return CI_BOOL(CI_INT(a) <= CI_INT(b));
	intptr_t r = CI_INT(bb_cmp_dispatch(c, a, b));
	return CI_BOOL(r <= 0);
}

static inline ci_ptr bb_op_notnull(bb_coro *c, ci_ptr a, ci_ptr b) {
	(void)c; (void)b;
	return CI_BOOL(a != NULL);
}
