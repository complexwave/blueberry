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

#define CI_BB_CMA_OP ((uint16_t)(CI_FAMILY_ENTRY(CI_VM_FAMILY, 4) | CI_OBJECT | CI_REFCOUNTABLE))

#define bb_is_cma_op(p) CI_CHECK_MASK_FAMILY(p, CI_BB_CMA_OP, CI_VM_FAMILY)

#define BB_CHECK_CMA_OP(a) do { \
	if (!bb_is_cma_op(a)) { bb_error("%s: expected cma pattern", __func__); return NULL; } \
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
			free(cap->name);
		break;
	}
	default:
		break;
	}

	free(w->op);
}

/* ---- registration ---- */

static void bb_cma_types_register(void) {
	static const tg_arena_ops cma_ops = { .destructor = bb_cma_op_destructor };
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
static ci_ptr bb_cma_P(bb_vm_arg *vm, ci_ptr a0, ci_ptr a1, ci_ptr a2) {
	(void)vm; (void)a1; (void)a2;
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
static ci_ptr bb_cma_Pi(bb_vm_arg *vm, ci_ptr a0, ci_ptr a1, ci_ptr a2) {
	(void)vm; (void)a1; (void)a2;
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
static ci_ptr bb_cma_R(bb_vm_arg *vm, ci_ptr a0, ci_ptr a1, ci_ptr a2) {
	(void)vm; (void)a1; (void)a2;
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
static ci_ptr bb_cma_S(bb_vm_arg *vm, ci_ptr a0, ci_ptr a1, ci_ptr a2) {
	(void)vm; (void)a1; (void)a2;
	BB_CHECK_STRING(a0);

	cma_op_set *raw = b_malloc(sizeof(cma_op_set));
	memset(raw, 0, sizeof(cma_op_set));
	cma_init_set(raw);
	cma_set_fill_chrs(raw, ci_str_head(a0), ci_str_len(a0));

	return (ci_ptr)bb_cma_new(CMA_SET, (cma_op *)raw);
}

/* cma.any(n) — match any n bytes */
static ci_ptr bb_cma_any(bb_vm_arg *vm, ci_ptr a0, ci_ptr a1, ci_ptr a2) {
	(void)vm; (void)a1; (void)a2;
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

	/* set is owned by the rep wrapper — no separate GC object */
	bb_cma_op *w = bb_cma_new(CMA_REP, (cma_op *)rep);
	/* stash the inner set for freeing */
	w->children = NULL;

	return (ci_ptr)w;
}

/* cma.endl() — match end of input */
static ci_ptr bb_cma_endl(bb_vm_arg *vm, ci_ptr a0, ci_ptr a1, ci_ptr a2) {
	(void)vm; (void)a0; (void)a1; (void)a2;

	cma_op *raw = b_malloc(sizeof(cma_op));
	memset(raw, 0, sizeof(cma_op));
	raw->fn = cma_match_endl;
	raw->type = CMA_ENDL;

	return (ci_ptr)bb_cma_new(CMA_ENDL, raw);
}

/* ================================================================
 *  Combinators
 * ================================================================ */

/* helper: extract cma_op* array + build children ref array from ci_array of bb_cma_op */
static cma_op_seq *bb_cma_build_seq(uint16_t type, ci_array *patterns, ci_array **out_children) {
	uint32_t n = ci_arr_len(patterns);
	if (n == 0)
		bb_error("cma.seq/alt: empty pattern array");

	cma_op **ops = b_malloc(n * sizeof(cma_op *));
	ci_array *children = ci_arr_new(n);

	for (uint32_t i = 0; i < n; i++) {
		ci_ptr elem = ci_arr_index(patterns, i);
		if (!bb_is_cma_op(elem)) {
			free(ops);
			ci_dec(children);
			bb_error("cma.seq/alt: element %u is not a cma pattern", i);
		}
		bb_cma_op *child = (bb_cma_op *)elem;
		ops[i] = child->op;
		ci_inc(elem);
		ci_arr_push(children, elem);
	}

	cma_op_seq *raw = b_malloc(sizeof(cma_op_seq) + (n + 1) * sizeof(cma_op *));
	memset(raw, 0, sizeof(cma_op_seq));
	cma_init_seq(raw, type, NULL, n, ops);

	free(ops);
	*out_children = children;
	return raw;
}

/* cma.seq(arr) / cma.and(arr) — sequence */
static ci_ptr bb_cma_seq(bb_vm_arg *vm, ci_ptr a0, ci_ptr a1, ci_ptr a2) {
	(void)vm; (void)a1; (void)a2;
	BB_CHECK_ARRAY(a0);

	/* single-element: return it directly */
	if (ci_arr_len((ci_array *)a0) == 1) {
		ci_ptr elem = ci_arr_index((ci_array *)a0, 0);
		BB_CHECK_CMA_OP(elem);
		ci_inc(elem);
		return elem;
	}

	ci_array *children = NULL;
	cma_op_seq *raw = bb_cma_build_seq(CMA_AND, (ci_array *)a0, &children);
	bb_cma_op *w = bb_cma_new_with_children(CMA_AND, (cma_op *)raw, children);
	ci_dec(children);
	return (ci_ptr)w;
}

/* cma.alt(arr) / cma.or(arr) — ordered choice */
static ci_ptr bb_cma_alt(bb_vm_arg *vm, ci_ptr a0, ci_ptr a1, ci_ptr a2) {
	(void)vm; (void)a1; (void)a2;
	BB_CHECK_ARRAY(a0);

	if (ci_arr_len((ci_array *)a0) == 1) {
		ci_ptr elem = ci_arr_index((ci_array *)a0, 0);
		BB_CHECK_CMA_OP(elem);
		ci_inc(elem);
		return elem;
	}

	ci_array *children = NULL;
	cma_op_seq *raw = bb_cma_build_seq(CMA_OR, (ci_array *)a0, &children);
	bb_cma_op *w = bb_cma_new_with_children(CMA_OR, (cma_op *)raw, children);
	ci_dec(children);
	return (ci_ptr)w;
}

/* cma.neg(p) — negation, consumes nothing */
static ci_ptr bb_cma_neg(bb_vm_arg *vm, ci_ptr a0, ci_ptr a1, ci_ptr a2) {
	(void)vm; (void)a1; (void)a2;
	BB_CHECK_CMA_OP(a0);

	bb_cma_op *child = (bb_cma_op *)a0;

	cma_op_wrap *raw = b_malloc(sizeof(cma_op_wrap));
	memset(raw, 0, sizeof(cma_op_wrap));
	cma_init_not(raw, child->op);

	ci_array *children = ci_arr_new(1);
	ci_inc(a0);
	ci_arr_push(children, a0);

	bb_cma_op *w = bb_cma_new_with_children(CMA_NOT, (cma_op *)raw, children);
	ci_dec(children);
	return (ci_ptr)w;
}

/* cma.ahead(p) — positive lookahead, consumes nothing */
static ci_ptr bb_cma_ahead(bb_vm_arg *vm, ci_ptr a0, ci_ptr a1, ci_ptr a2) {
	(void)vm; (void)a1; (void)a2;
	BB_CHECK_CMA_OP(a0);

	bb_cma_op *child = (bb_cma_op *)a0;

	cma_op_wrap *raw = b_malloc(sizeof(cma_op_wrap));
	memset(raw, 0, sizeof(cma_op_wrap));
	cma_init_ahead(raw, child->op);

	ci_array *children = ci_arr_new(1);
	ci_inc(a0);
	ci_arr_push(children, a0);

	bb_cma_op *w = bb_cma_new_with_children(CMA_AHEAD, (cma_op *)raw, children);
	ci_dec(children);
	return (ci_ptr)w;
}

/* cma.rep(min, max, p) — bounded repetition */
static ci_ptr bb_cma_rep(bb_vm_arg *vm, ci_ptr a0, ci_ptr a1, ci_ptr a2) {
	(void)vm;
	BB_CHECK_INT(a0);
	BB_CHECK_INT(a1);
	BB_CHECK_CMA_OP(a2);

	uint32_t min = (uint32_t)CI_INT(a0);
	uint32_t max = (uint32_t)CI_INT(a1);
	bb_cma_op *child = (bb_cma_op *)a2;

	cma_op_rep *raw = b_malloc(sizeof(cma_op_rep));
	memset(raw, 0, sizeof(cma_op_rep));
	cma_init_rep(raw, min, max, child->op);

	ci_array *children = ci_arr_new(1);
	ci_inc(a2);
	ci_arr_push(children, a2);

	bb_cma_op *w = bb_cma_new_with_children(CMA_REP, (cma_op *)raw, children);
	ci_dec(children);
	return (ci_ptr)w;
}

/* cma.cap(p) — simple capture */
static ci_ptr bb_cma_cap(bb_vm_arg *vm, ci_ptr a0, ci_ptr a1, ci_ptr a2) {
	(void)vm; (void)a1; (void)a2;
	BB_CHECK_CMA_OP(a0);

	bb_cma_op *child = (bb_cma_op *)a0;

	cma_op_cap *raw = b_malloc(sizeof(cma_op_cap));
	memset(raw, 0, sizeof(cma_op_cap));
	cma_init_cap(raw, child->op, 0, NULL);

	ci_array *children = ci_arr_new(1);
	ci_inc(a0);
	ci_arr_push(children, a0);

	bb_cma_op *w = bb_cma_new_with_children(CMA_CAP, (cma_op *)raw, children);
	ci_dec(children);
	return (ci_ptr)w;
}

/* cma.capn(p, name) — named capture */
static ci_ptr bb_cma_capn(bb_vm_arg *vm, ci_ptr a0, ci_ptr a1, ci_ptr a2) {
	(void)vm; (void)a2;
	BB_CHECK_CMA_OP(a0);
	BB_CHECK_STRING(a1);

	bb_cma_op *child = (bb_cma_op *)a0;

	size_t nlen;
	char *name = (char *)bb_cma_strdup(a1, &nlen);

	cma_op_cap *raw = b_malloc(sizeof(cma_op_cap));
	memset(raw, 0, sizeof(cma_op_cap));
	cma_init_cap(raw, child->op, CMA_CAP_NAMED, name);

	ci_array *children = ci_arr_new(1);
	ci_inc(a0);
	ci_arr_push(children, a0);

	bb_cma_op *w = bb_cma_new_with_children(CMA_CAP, (cma_op *)raw, children);
	ci_dec(children);
	return (ci_ptr)w;
}

/* ================================================================
 *  Matching
 * ================================================================ */

/* cma.match(pat, str) → false | true | [cap_strings...] */
static ci_ptr bb_cma_match(bb_vm_arg *vm, ci_ptr a0, ci_ptr a1, ci_ptr a2) {
	(void)vm; (void)a2;
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

	/* build flat array of capture strings */
	ci_array *caps = ci_arr_new(st.cap_len);
	for (uint32_t i = 0; i < st.cap_len; i++) {
		cma_capture *c = &st.caps[i];
		size_t clen = cma_cap_len(c);
		ci_str *s = ci_str_new(clen);
		ci_str_append(s, cma_cap_start(c), clen);
		ci_arr_push(caps, (ci_ptr)s);
	}

	cma_free(&st);
	return (ci_ptr)caps;
}

/* ================================================================
 *  Varargs combinators (commented out — varargs calling convention
 *  not yet working in VM. Uncomment when bb_cfn_var dispatch is fixed.)
 * ================================================================ */

#if 0
/* cma.SEQ(p1, p2, ...) — varargs sequence */
static ci_ptr bb_cma_seq_var(bb_vm *vm, uint8_t nargs, ci_ptr *args) {
	(void)vm;
	if (nargs == 0)
		bb_error("cma.SEQ: need at least one pattern");
	if (nargs == 1) {
		BB_CHECK_CMA_OP(args[0]);
		ci_inc(args[0]);
		return args[0];
	}

	cma_op **ops = b_malloc(nargs * sizeof(cma_op *));
	ci_array *children = ci_arr_new(nargs);

	for (uint8_t i = 0; i < nargs; i++) {
		if (!bb_is_cma_op(args[i])) {
			free(ops);
			ci_dec(children);
			bb_error("cma.SEQ: arg %u is not a cma pattern", i);
		}
		bb_cma_op *child = (bb_cma_op *)args[i];
		ops[i] = child->op;
		ci_inc(args[i]);
		ci_arr_push(children, args[i]);
	}

	cma_op_seq *raw = b_malloc(sizeof(cma_op_seq) + (nargs + 1) * sizeof(cma_op *));
	memset(raw, 0, sizeof(cma_op_seq));
	cma_init_seq(raw, CMA_AND, NULL, nargs, ops);

	free(ops);
	bb_cma_op *w = bb_cma_new_with_children(CMA_AND, (cma_op *)raw, children);
	ci_dec(children);
	return (ci_ptr)w;
}

/* cma.ALT(p1, p2, ...) — varargs ordered choice */
static ci_ptr bb_cma_alt_var(bb_vm *vm, uint8_t nargs, ci_ptr *args) {
	(void)vm;
	if (nargs == 0)
		bb_error("cma.ALT: need at least one pattern");
	if (nargs == 1) {
		BB_CHECK_CMA_OP(args[0]);
		ci_inc(args[0]);
		return args[0];
	}

	cma_op **ops = b_malloc(nargs * sizeof(cma_op *));
	ci_array *children = ci_arr_new(nargs);

	for (uint8_t i = 0; i < nargs; i++) {
		if (!bb_is_cma_op(args[i])) {
			free(ops);
			ci_dec(children);
			bb_error("cma.ALT: arg %u is not a cma pattern", i);
		}
		bb_cma_op *child = (bb_cma_op *)args[i];
		ops[i] = child->op;
		ci_inc(args[i]);
		ci_arr_push(children, args[i]);
	}

	cma_op_seq *raw = b_malloc(sizeof(cma_op_seq) + (nargs + 1) * sizeof(cma_op *));
	memset(raw, 0, sizeof(cma_op_seq));
	cma_init_seq(raw, CMA_OR, NULL, nargs, ops);

	free(ops);
	bb_cma_op *w = bb_cma_new_with_children(CMA_OR, (cma_op *)raw, children);
	ci_dec(children);
	return (ci_ptr)w;
}
#endif

/* ================================================================
 *  Registration — builds the `cma` namespace map
 * ================================================================ */

static void bb_lib_cma_init(bb_vm *vm) {
	bb_cma_types_register();

	ci_map *ns = ci_map_new(16);

	static const struct { const char *name; bb_cfn fn; } cma_lib[] = {
		/* primitives */
		{ "P",     (bb_cfn)bb_cma_P     },
		{ "Pi",    (bb_cfn)bb_cma_Pi    },
		{ "R",     (bb_cfn)bb_cma_R     },
		{ "S",     (bb_cfn)bb_cma_S     },
		{ "any",   (bb_cfn)bb_cma_any   },
		{ "endl",  (bb_cfn)bb_cma_endl  },
		/* combinators */
		{ "seq",   (bb_cfn)bb_cma_seq   },
		{ "and",   (bb_cfn)bb_cma_seq   },  /* synonym */
		{ "alt",   (bb_cfn)bb_cma_alt   },
		{ "or",    (bb_cfn)bb_cma_alt   },  /* synonym */
		{ "neg",   (bb_cfn)bb_cma_neg   },
		{ "ahead", (bb_cfn)bb_cma_ahead },
		{ "rep",   (bb_cfn)bb_cma_rep   },
		{ "cap",   (bb_cfn)bb_cma_cap   },
		{ "capn",  (bb_cfn)bb_cma_capn  },
		/* matching */
		{ "match", (bb_cfn)bb_cma_match },
	};

	for (size_t i = 0; i < sizeof(cma_lib) / sizeof(cma_lib[0]); i++) {
		bb_closure *cl = bb_vm_native(vm, cma_lib[i].name, cma_lib[i].fn);
		ci_ptr key = bb_vm_istring(vm, cma_lib[i].name, (uint32_t)strlen(cma_lib[i].name));
		ci_map_put(ns, key, (ci_ptr)cl);
	}

	/*
	 * Varargs versions — uncomment when VM varargs dispatch is fixed:
	 *
	 * bb_closure *seq_v = bb_vm_native_var(vm, "SEQ", (bb_cfn_var)bb_cma_seq_var);
	 * ci_map_put(ns, bb_vm_istring(vm, "SEQ", 3), (ci_ptr)seq_v);
	 *
	 * bb_closure *alt_v = bb_vm_native_var(vm, "ALT", (bb_cfn_var)bb_cma_alt_var);
	 * ci_map_put(ns, bb_vm_istring(vm, "ALT", 3), (ci_ptr)alt_v);
	 */

	ci_ptr cma_key = bb_vm_istring(vm, "cma", 3);
	ci_map_put(vm->globals, cma_key, (ci_ptr)ns);
}
