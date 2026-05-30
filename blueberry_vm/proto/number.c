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

/*
 * op_tostring(a, b)
 *   a = number, b = string to append to (or NULL → new string)
 */
static ci_ptr bb_number_meta_tostring(bb_coro *c, ci_ptr a, ci_ptr b) {
	int maxlen = ci_number_stringmax(a);
	ci_str *s;

	if (b) {
		s = (ci_str *)b;
	} else {
		s = ci_str_new((size_t)maxlen);
		if (!s) return NULL;
	}

	uint8_t *tail = ci_str_ensure_tail(s, (size_t)maxlen);
	
	int n = ci_number_tostring(
		a, tail, 
		CI_NUMBER_PRINT_MAX_INT_DIGITS_SCI, 
		CI_NUMBER_PRINT_MAX_FLOAT_DIGITS_SCI
	);
	
	ci_str_put_tail(s, (size_t)n);
	
	return (ci_ptr)s;
}

/* number(x) — parse a string into a number, or return existing numbers as-is */
static ci_ptr bb_native_number(bb_coro_arg *c, ci_ptr_arg self, ci_ptr a, ci_ptr_arg b, ci_ptr_arg _) {
	if (CI_IS_NUMBER(a))
		return a;

	if (CI_IS_INT(a))
		return a;

	if (CI_IS_ANY_STR(a)) {
		ci_ptr r = ci_number_fromstring(ci_str_head(a), ci_str_len(a));
		return r;  /* NULL if parse fails */
	}

	return NULL;
}

#define float_tolower(c) ((uint8_t)((c) | 0x20))

/*
 * Try to match a keyword (case-insensitive) after skipping whitespace.
 * Returns pointer past the keyword, or NULL on mismatch.
 */
static const uint8_t *bb_float_match(const uint8_t *s, const uint8_t *end, const char *kw) {
	while (*kw) {
		if (s >= end || float_tolower(*s) != float_tolower(*kw)) return NULL;
		s++; kw++;
	}
	return s;
}

/* float(x) — like number(x) but also accepts inf, +inf, -inf, NaN */
static ci_ptr bb_native_float(bb_coro_arg *c, ci_ptr_arg self, ci_ptr a, ci_ptr_arg b, ci_ptr_arg _) {
	if (CI_IS_NUMBER(a))
		return a;

	if (CI_IS_INT(a)) {
		ci_number *n = ci_number_new(CI_NUM_I128);
		n->i128 = (__int128)CI_INT(a);
		return (ci_ptr)n;
	}

	if (CI_IS_ANY_STR(a)) {
		const uint8_t *src = ci_str_head(a);
		size_t len = ci_str_len(a);
		const uint8_t *end = src + len;

		/* skip leading whitespace */
		while (src < end && (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n'))
			src++;

		if (src < end) {
			const uint8_t *tail = NULL;
			double val = 0;

			uint8_t lo = float_tolower(*src);

			if (lo == 'n') {
				tail = bb_float_match(src, end, "nan");
				if (tail) val = NAN;
			} else if (lo == 'i') {
				tail = bb_float_match(src, end, "inf");
				if (tail) val = INFINITY;
			} else if (*src == '+') {
				tail = bb_float_match(src, end, "+inf");
				if (tail) val = INFINITY;
			} else if (*src == '-') {
				tail = bb_float_match(src, end, "-inf");
				if (tail) val = -INFINITY;
			}

			if (tail) {
				/* skip trailing whitespace */
				while (tail < end && (*tail == ' ' || *tail == '\t' || *tail == '\r' || *tail == '\n'))
					tail++;

				if (tail == end)
					return (ci_ptr)ci_number_floating(val);

				return NULL;
			}
		}

		/* fall through to normal number parsing */
		ci_ptr r = ci_number_fromstring(ci_str_head(a), ci_str_len(a));
		return r;
	}

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
	mp->op_tostring = bb_number_meta_tostring;

	bb_set_arena_prototype(CI_NUMBER, &mp->map);

	bb_closure *cl = bb_vm_native(vm, "number", bb_native_number);
	ci_map_put(vm->globals, cl->fn->name, (ci_ptr)cl);

	bb_closure *fl = bb_vm_native(vm, "float", bb_native_float);
	ci_map_put(vm->globals, fl->fn->name, (ci_ptr)fl);
}
