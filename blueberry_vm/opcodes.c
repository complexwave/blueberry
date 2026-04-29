static inline ci_ptr bb_op_loadnull(bb_coro *c, ci_ptr a, ci_ptr b) {
	(void)a; (void)b;
	
	return NULL;
}

static inline ci_ptr bb_op_add(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b))
		return CI_PACKINT(CI_INT(a) + CI_INT(b));
	bb_coro_error(c, "ADD: type error");
	return NULL;
}

static inline ci_ptr bb_op_sub(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b))
		return CI_PACKINT(CI_INT(a) - CI_INT(b));
	bb_coro_error(c, "SUB: type error");
	return NULL;
}

static inline ci_ptr bb_op_mul(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b))
		return CI_PACKINT(CI_INT(a) * CI_INT(b));
	bb_coro_error(c, "MUL: type error");
	return NULL;
}

static inline ci_ptr bb_op_div(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b)) {
		intptr_t bv = CI_INT(b);
		if (bv == 0)
			bb_coro_error(c, "DIV: division by zero");
		return CI_PACKINT(CI_INT(a) / bv);
	}
	bb_coro_error(c, "DIV: type error");
	return NULL;
}

static inline ci_ptr bb_op_mod(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b)) {
		intptr_t bv = CI_INT(b);
		if (bv == 0)
			bb_coro_error(c, "MOD: division by zero");
		return CI_PACKINT(CI_INT(a) % bv);
	}
	bb_coro_error(c, "MOD: type error");
	return NULL;
}

static inline ci_ptr bb_op_pow(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b)) {
		intptr_t base = CI_INT(a);
		intptr_t exp  = CI_INT(b);
		if (exp < 0)
			bb_coro_error(c, "POW: negative exponent");
		intptr_t result = 1;
		for (intptr_t i = 0; i < exp; i++)
			result *= base;
		return CI_PACKINT(result);
	}
	bb_coro_error(c, "POW: type error");
	return NULL;
}

static inline ci_ptr bb_op_neg(bb_coro *c, ci_ptr a, ci_ptr b) {
	(void)c; (void)b;
	if (CI_IS_INT(a))
		return CI_PACKINT(-CI_INT(a));
	bb_coro_error(c, "NEG: type error");
	return NULL;
}

static inline ci_ptr bb_op_not(bb_coro *c, ci_ptr a, ci_ptr b) {
	(void)c; (void)b;
	return CI_IS_FALSY(a) ? CI_BOOL(1) : CI_BOOL(0);
}

static inline ci_ptr bb_op_bin_inv(bb_coro *c, ci_ptr a, ci_ptr b) {
	(void)c; (void)b;
	if (CI_IS_INT(a))
		return CI_PACKINT(~CI_INT(a));
	bb_coro_error(c, "BIN_INV: type error");
	return NULL;
}

static inline ci_ptr bb_op_bin_or(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b))
		return CI_PACKINT(CI_INT(a) | CI_INT(b));
	bb_coro_error(c, "BIN_OR: type error");
	return NULL;
}

static inline ci_ptr bb_op_bin_and(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b))
		return CI_PACKINT(CI_INT(a) & CI_INT(b));
	bb_coro_error(c, "BIN_AND: type error");
	return NULL;
}

static inline ci_ptr bb_op_bin_xor(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b))
		return CI_PACKINT(CI_INT(a) ^ CI_INT(b));
	bb_coro_error(c, "BIN_XOR: type error");
	return NULL;
}

static inline ci_ptr bb_op_bin_lshift(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b))
		return CI_PACKINT(CI_INT(a) << CI_INT(b));
	bb_coro_error(c, "BIN_LSHIFT: type error");
	return NULL;
}

static inline ci_ptr bb_op_bin_rshift(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b))
		return CI_PACKINT(CI_INT(a) >> CI_INT(b));
	bb_coro_error(c, "BIN_RSHIFT: type error");
	return NULL;
}

static inline ci_ptr bb_op_eq(bb_coro *c, ci_ptr a, ci_ptr b) {
	(void)c;
	return CI_BOOL(a == b);
}

static inline ci_ptr bb_op_neq(bb_coro *c, ci_ptr a, ci_ptr b) {
	(void)c;
	return CI_BOOL(a != b);
}

static inline ci_ptr bb_op_gt(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b))
		return CI_BOOL(CI_INT(a) > CI_INT(b));
	bb_coro_error(c, "GT: type error");
	return NULL;
}

static inline ci_ptr bb_op_lt(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b))
		return CI_BOOL(CI_INT(a) < CI_INT(b));
	bb_coro_error(c, "LT: type error");
	return NULL;
}

static inline ci_ptr bb_op_gt_eq(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b))
		return CI_BOOL(CI_INT(a) >= CI_INT(b));
	bb_coro_error(c, "GT_EQ: type error");
	return NULL;
}

static inline ci_ptr bb_op_lt_eq(bb_coro *c, ci_ptr a, ci_ptr b) {
	if (CI_IS_INT(a) && CI_IS_INT(b))
		return CI_BOOL(CI_INT(a) <= CI_INT(b));
	bb_coro_error(c, "LT_EQ: type error");
	return NULL;
}

static inline ci_ptr bb_op_notnull(bb_coro *c, ci_ptr a, ci_ptr b) {
	(void)c;
	return a ? a : b;
}
