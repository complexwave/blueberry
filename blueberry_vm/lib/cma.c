/*
 * blueberry_vm/lib/cma.c — LPeg-style PEG pattern matching library
 *
 * Script API (cma.* namespace):
 *
 *   Primitives (return cma pattern):
 *     cma.P(str)            literal string match
 *     cma.Pi(str)           case-insensitive literal
 *     cma.R(spec)           character range, e.g. "a-zA-Z0-9"
 *     cma.S(chars)          character set from individual chars in string
 *     cma.any(n)            match any n bytes
 *     cma.endl()            match end of input
 *
 *   Combinators (take pattern args, return pattern):
 *     cma.seq(arr)          sequence (AND) — array of patterns
 *     cma.alt(arr)          ordered choice (OR) — array of patterns
 *     cma.neg(p)            negation (consumes nothing)
 *     cma.ahead(p)          positive lookahead
 *     cma.rep(min, max, p)  bounded repetition
 *     cma.cap(p)            simple capture
 *     cma.capn(p, name)     named capture
 *
 *   Matching:
 *     cma.match(pat, str)   false | true (no captures) | [cap_strings...]
 */

/* cma/cma.h is already included via encoder.c → bytecode.c → parser.c chain */

/* ================================================================
 *  bb_cma_op — GC-managed wrapper around a cma_op tree node
 * ================================================================ */

#define CI_BB_CMA_OP ((uint16_t)(CI_FAMILY_ENTRY(CI_VM_FAMILY, 4) | CI_REFCOUNTABLE))

#define bb_is_cma_op(p) CI_CHECK_MASK_FAMILY(p, CI_BB_CMA_OP, CI_VM_FAMILY)

#define BB_CHECK_CMA_OP(a) do { \
	if (!bb_is_cma_op(a)) { bb_coro_error(c, "%s: expected cma pattern", __func__); } \
} while(0)

typedef struct {
	CI_GC_HDR;
	uint16_t op_type;
	cma_op *op;
	ci_array *children;
} bb_cma_op;

/* ---- destructor ---- */

static void bb_cma_op_destructor(void *ptr, tg_arena_t *arena) {
	(void)arena;
	bb_cma_op *w = (bb_cma_op *)ptr;

	if (w->children) {
		ci_dec(w->children);
		w->children = NULL;
	}

	if (!w->op)
		return;

	switch (w->op_type) {
	case CMA_STR: {
		cma_op_str *s = (cma_op_str *)w->op;
		free((void *)s->buf);
		break;
	}
	case CMA_AND:
	case CMA_OR: {
		cma_op_seq *sq = (cma_op_seq *)w->op;
		free(sq->name);
		break;
	}
	case CMA_CAP: {
		cma_op_cap *cap = (cma_op_cap *)w->op;
		if (cap->base.flags & CMA_CAP_NAMED)
			ci_dec((ci_ptr)cap->name);  /* name is a ci_ptr stored as char* */
		break;
	}
	default:
		break;
	}

	free(w->op);
}

/* ---- registration ---- */

static void bb_cma_types_register(bb_vm *vm) {
	ci_map *proto = bb_proto_register(vm, "cma");

	tg_arena_ops cma_ops = { .destructor = bb_cma_op_destructor };
	cma_ops.prototype = proto;

	ci_register_ops(CI_BB_CMA_OP, sizeof(bb_cma_op), &cma_ops);
}

/* ---- alloc helper ---- */

static bb_cma_op *bb_cma_new(uint16_t op_type, cma_op *op) {
	bb_cma_op *w = ci_new(CI_BB_CMA_OP);
	if (!w)
		bb_error("cma: out of memory");
	w->op_type = op_type;
	w->op = op;
	w->children = NULL;
	return w;
}

static bb_cma_op *bb_cma_new_with_children(uint16_t op_type, cma_op *op, ci_array *children) {
	bb_cma_op *w = bb_cma_new(op_type, op);
	w->children = children;
	ci_inc(children);
	return w;
}

/* ---- helper: null-terminated copy from ci_str ---- */

static uint8_t *bb_cma_strdup(ci_ptr s, size_t *out_len) {
	size_t len = ci_str_len(s);
	uint8_t *buf = b_malloc(len + 1);
	memcpy(buf, ci_str_head(s), len);
	buf[len] = '\0';
	if (out_len) *out_len = len;
	return buf;
}

/* ================================================================
 *  Primitives
 * ================================================================ */

/* cma.P(str) — literal string match */
static ci_ptr bb_cma_P(bb_coro_arg *c, ci_ptr_arg self, ci_ptr a0) {
	BB_CHECK_STRING(a0);

	size_t len;
	uint8_t *buf = bb_cma_strdup(a0, &len);

	cma_op_str *raw = b_malloc(sizeof(cma_op_str));
	memset(raw, 0, sizeof(cma_op_str));
	cma_init_str(raw, (const char *)buf, 0);
	raw->len = len;

	return (ci_ptr)bb_cma_new(CMA_STR, (cma_op *)raw);
}

/* cma.Pi(str) — case-insensitive literal */
static ci_ptr bb_cma_Pi(bb_coro_arg *c, ci_ptr_arg self, ci_ptr a0) {
	BB_CHECK_STRING(a0);

	size_t len;
	uint8_t *buf = bb_cma_strdup(a0, &len);

	cma_op_str *raw = b_malloc(sizeof(cma_op_str));
	memset(raw, 0, sizeof(cma_op_str));
	cma_init_str(raw, (const char *)buf, CMA_STR_INSENSITIVE);
	raw->len = len;

	return (ci_ptr)bb_cma_new(CMA_STR, (cma_op *)raw);
}

/* cma.R(spec) — character range, e.g. "a-zA-Z0-9" */
static ci_ptr bb_cma_R(bb_coro_arg *c, ci_ptr_arg self, ci_ptr a0) {
	BB_CHECK_STRING(a0);

	char pbuf[256];
	size_t len = ci_str_len(a0);
	if (len >= sizeof(pbuf)) len = sizeof(pbuf) - 1;
	memcpy(pbuf, ci_str_head(a0), len);
	pbuf[len] = '\0';

	cma_op_set *raw = b_malloc(sizeof(cma_op_set));
	memset(raw, 0, sizeof(cma_op_set));
	cma_init_set(raw);
	cma_set_fill_str(raw, pbuf);

	return (ci_ptr)bb_cma_new(CMA_SET, (cma_op *)raw);
}

/* cma.S(chars) — character set from individual chars in string */
static ci_ptr bb_cma_S(bb_coro_arg *c, ci_ptr_arg self, ci_ptr a0) {
	BB_CHECK_STRING(a0);

	cma_op_set *raw = b_malloc(sizeof(cma_op_set));
	memset(raw, 0, sizeof(cma_op_set));
	cma_init_set(raw);
	cma_set_fill_chrs(raw, ci_str_head(a0), ci_str_len(a0));

	return (ci_ptr)bb_cma_new(CMA_SET, (cma_op *)raw);
}

/* cma.any(n) — match any n bytes */
static ci_ptr bb_cma_any(bb_coro_arg *c, ci_ptr_arg self, ci_ptr a0) {
	BB_CHECK_INT(a0);

	uint32_t n = (uint32_t)CI_INT(a0);

	/* any(n) = rep(n, n, set(0-255)) */
	cma_op_set *set = b_malloc(sizeof(cma_op_set));
	memset(set, 0, sizeof(cma_op_set));
	cma_init_set(set);
	cma_set_fill_range(set, 0, 255);

	cma_op_rep *rep = b_malloc(sizeof(cma_op_rep));
	memset(rep, 0, sizeof(cma_op_rep));
	cma_init_rep(rep, n, n, (cma_op *)set);

	bb_cma_op *w = bb_cma_new(CMA_REP, (cma_op *)rep);
	w->children = NULL;

	return (ci_ptr)w;
}

/* cma.endl() — match end of input */
static ci_ptr bb_cma_endl(bb_coro_arg *c, ci_ptr_arg self) {

	cma_op *raw = b_malloc(sizeof(cma_op));
	memset(raw, 0, sizeof(cma_op));
	raw->fn = cma_match_endl;
	raw->type = CMA_ENDL;

	return (ci_ptr)bb_cma_new(CMA_ENDL, raw);
}

/* ================================================================
 *  Combinators
 * ================================================================ */

/* helper: build cma_op_seq + children array from a flat ci_ptr* of bb_cma_op */
static bb_cma_op *bb_cma_build_seq(bb_coro *c, uint16_t type, size_t n, ci_ptr *ops_in) {
	cma_op **ops = b_malloc(n * sizeof(cma_op *));
	ci_array *children = ci_arr_new((uint32_t)n);

	for (size_t i = 0; i < n; i++) {
		if (!bb_is_cma_op(ops_in[i])) {
			free(ops);
			ci_dec(children);
			bb_coro_error(c, "cma: element %zu is not a cma pattern", i);
		}
		bb_cma_op *child = (bb_cma_op *)ops_in[i];
		ops[i] = child->op;
		ci_inc(ops_in[i]);
		ci_arr_push(children, ops_in[i]);
	}

	cma_op_seq *raw = b_malloc(sizeof(cma_op_seq) + (n + 1) * sizeof(cma_op *));
	memset(raw, 0, sizeof(cma_op_seq));
	cma_init_seq(raw, type, NULL, (uint32_t)n, ops);
	free(ops);

	bb_cma_op *w = bb_cma_new_with_children(type, (cma_op *)raw, children);
	ci_dec(children);
	return w;
}

/* cma.seq / cma.and / cma.SEQ — sequence; accepts array arg or varargs */
static bb_var_ret bb_cma_seq(bb_coro *c, ci_ptr self, size_t nargs, ci_ptr *args, size_t nrets) {
	(void)self; (void)nrets;
	BB_VARARG_OR_ARRAY;
	if (nargs == 0)
		bb_coro_error(c, "cma.seq: need at least one pattern");
	if (nargs == 1) {
		BB_CHECK_CMA_OP(args[0]);
		ci_inc(args[0]);
		BB_PUSH_RET(args[0]);
		return 1;
	}
	ci_ptr result = (ci_ptr)bb_cma_build_seq(c, CMA_AND, nargs, args);
	BB_PUSH_RET(result);
	return 1;
}

/* cma.alt / cma.or / cma.ALT — ordered choice; accepts array arg or varargs */
static bb_var_ret bb_cma_alt(bb_coro *c, ci_ptr self, size_t nargs, ci_ptr *args, size_t nrets) {
	(void)self; (void)nrets;
	BB_VARARG_OR_ARRAY;
	if (nargs == 0)
		bb_coro_error(c, "cma.alt: need at least one pattern");
	if (nargs == 1) {
		BB_CHECK_CMA_OP(args[0]);
		ci_inc(args[0]);
		BB_PUSH_RET(args[0]);
		return 1;
	}
	ci_ptr result = (ci_ptr)bb_cma_build_seq(c, CMA_OR, nargs, args);
	BB_PUSH_RET(result);
	return 1;
}

/* cma.neg(p) — negation, consumes nothing */
static ci_ptr bb_cma_neg(bb_coro_arg *c, ci_ptr_arg self, ci_ptr a0) {
	BB_CHECK_CMA_OP(a0);

	cma_op_wrap *raw = b_malloc(sizeof(cma_op_wrap));
	memset(raw, 0, sizeof(cma_op_wrap));
	cma_init_not(raw, ((bb_cma_op *)a0)->op);

	ci_array *children = ci_arr_new(1);
	ci_inc(a0);
	ci_arr_push(children, a0);

	bb_cma_op *w = bb_cma_new_with_children(CMA_NOT, (cma_op *)raw, children);
	ci_dec(children);
	return (ci_ptr)w;
}

/* cma.ahead(p) — positive lookahead, consumes nothing */
static ci_ptr bb_cma_ahead(bb_coro_arg *c, ci_ptr_arg self, ci_ptr a0) {
	BB_CHECK_CMA_OP(a0);

	cma_op_wrap *raw = b_malloc(sizeof(cma_op_wrap));
	memset(raw, 0, sizeof(cma_op_wrap));
	cma_init_ahead(raw, ((bb_cma_op *)a0)->op);

	ci_array *children = ci_arr_new(1);
	ci_inc(a0);
	ci_arr_push(children, a0);

	bb_cma_op *w = bb_cma_new_with_children(CMA_AHEAD, (cma_op *)raw, children);
	ci_dec(children);
	return (ci_ptr)w;
}

/* cma.rep(min, max, p) — bounded repetition; vararg to fit 3 args past self */
static bb_var_ret bb_cma_rep(bb_coro *c, ci_ptr self, size_t nargs, ci_ptr *args, size_t nrets) {
	(void)self; (void)nrets;
	if (nargs != 3)
		bb_coro_error(c, "cma.rep: expected (min, max, pattern)");
	BB_CHECK_INT(args[0]);
	if (!CI_IS_INT(args[1]) && args[1] != CI_BOOL(1))
		bb_coro_error(c, "cma.rep: max must be int or true (infinite)");
	BB_CHECK_CMA_OP(args[2]);

	uint32_t mn  = (uint32_t)CI_INT(args[0]);
	uint32_t mx  = (args[1] == CI_BOOL(1)) ? CMA_INF : (uint32_t)CI_INT(args[1]);
	bb_cma_op *child = (bb_cma_op *)args[2];

	cma_op_rep *raw = b_malloc(sizeof(cma_op_rep));
	memset(raw, 0, sizeof(cma_op_rep));
	cma_init_rep(raw, mn, mx, child->op);

	ci_array *children = ci_arr_new(1);
	ci_inc(args[2]);
	ci_arr_push(children, args[2]);

	bb_cma_op *w = bb_cma_new_with_children(CMA_REP, (cma_op *)raw, children);
	ci_dec(children);
	BB_PUSH_RET((ci_ptr)w);
	return nargs;
}

/* cma.cap(p [, name]) — capture; optional name is any ci_ptr, stored directly (ci_inc'd) */
static bb_var_ret bb_cma_cap(bb_coro *c, ci_ptr self, size_t nargs, ci_ptr *args, size_t nrets) {
	(void)self; (void)nrets;
	if (nargs < 1 || nargs > 2)
		bb_coro_error(c, "cma.cap: expected (pattern [, name])");
	BB_CHECK_CMA_OP(args[0]);

	int named = nargs == 2;
	if (named) ci_inc(args[1]);

	cma_op_cap *raw = b_malloc(sizeof(cma_op_cap));
	memset(raw, 0, sizeof(cma_op_cap));
	cma_init_cap(raw, ((bb_cma_op *)args[0])->op,
	             named ? CMA_CAP_NAMED : 0,
	             named ? (char *)args[1] : NULL);

	ci_array *children = ci_arr_new(1);
	ci_inc(args[0]);
	ci_arr_push(children, args[0]);

	bb_cma_op *w = bb_cma_new_with_children(CMA_CAP, (cma_op *)raw, children);
	ci_dec(children);
	BB_PUSH_RET((ci_ptr)w);
	return nargs;
}

/* ================================================================
 *  Matching
 * ================================================================ */

/* cma.match(pat, str) → false | true | [ci_str_slice...]
 * Each slice points into the source string. Named captures have ctx set
 * to the ci_ptr name stored in the pattern (via capn). */
static ci_ptr bb_cma_match(bb_coro_arg *c, ci_ptr_arg self, ci_ptr a0, ci_ptr a1) {
	BB_CHECK_CMA_OP(a0);
	BB_CHECK_STRING(a1);

	bb_cma_op *pat = (bb_cma_op *)a0;
	const char *input = (const char *)ci_str_head(a1);
	size_t len = ci_str_len(a1);

	cma_state st;
	cma_init(&st, input, len);
	int result = cma_match(&st, pat->op);

	if (result < 0) {
		cma_free(&st);
		return CI_BOOL(0);
	}

	if (st.cap_len == 0) {
		cma_free(&st);
		return CI_BOOL(1);
	}

	/* build array of ci_str_slice objects pointing into the source string */
	ci_array *caps = ci_arr_new(st.cap_len);
	for (uint32_t i = 0; i < st.cap_len; i++) {
		cma_capture *cap = &st.caps[i];
		ci_str_slice *sl = (ci_str_slice *)ci_str_slice_new(
			cma_cap_start(cap), cma_cap_len(cap), a1);

		char *name = cma_cap_name(cap);  /* reads cap->parent->name */
		if (name) {
			ci_ptr name_val = (ci_ptr)name;
			ci_inc(name_val);
			sl->ctx = name_val;
		}

		ci_arr_push(caps, (ci_ptr)sl);
	}

	cma_free(&st);
	return (ci_ptr)caps;
}

/* ================================================================
 *  Registration — builds the `cma` namespace map
 * ================================================================ */

static void bb_lib_cma_init(bb_vm *vm) {
	bb_cma_types_register(vm);

	ci_map *ns = ci_map_new(16);

	static const bb_cfunc cma_lib[] = {
		/* primitives — method (flags=0): self=cma map, a0=first user arg */
		{ "P",     bb_cma_P,     0 },
		{ "Pi",    bb_cma_Pi,    0 },
		{ "R",     bb_cma_R,     0 },
		{ "S",     bb_cma_S,     0 },
		{ "any",   bb_cma_any,   0 },
		{ "endl",  bb_cma_endl,  0 },
		/* combinators — vararg: array or spread args both work via BB_VARARG_OR_ARRAY */
		{ "seq",   bb_cma_seq,   BB_FN_NATIVE_VAR },
		{ "and",   bb_cma_seq,   BB_FN_NATIVE_VAR },
		{ "SEQ",   bb_cma_seq,   BB_FN_NATIVE_VAR },
		{ "alt",   bb_cma_alt,   BB_FN_NATIVE_VAR },
		{ "or",    bb_cma_alt,   BB_FN_NATIVE_VAR },
		{ "ALT",   bb_cma_alt,   BB_FN_NATIVE_VAR },
		{ "rep",   bb_cma_rep,   BB_FN_NATIVE_VAR },
		/* single-pattern combinators — method */
		{ "neg",   bb_cma_neg,   0 },
		{ "ahead", bb_cma_ahead, 0 },
		{ "cap",   bb_cma_cap,   BB_FN_NATIVE_VAR },
		/* matching — method */
		{ "match", bb_cma_match, 0 },
	};

	bb_func2map(vm, ns, cma_lib, sizeof(cma_lib) / sizeof(cma_lib[0]));

	ci_map_put(vm->globals, bb_vm_istring(vm, "cma", 3), (ci_ptr)ns);
}
