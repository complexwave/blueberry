/*
 * blueberry_vm/lib/math.c — math namespace (math.sin, math.pi, etc.)
 */

#ifndef M_PI
#define M_PI   3.14159265358979323846
#endif
#ifndef M_E
#define M_E    2.71828182845904523536
#endif

/* ---- macros for repetitive math functions ---- */

/* single-arg double→double: math.sin(x) etc.
 * signature is (c, self, a, b, c_arg) — b,c_arg ignored for 1-arg ops */
#define BB_MATH_F1(name, cfn) \
static ci_ptr bb_math_##name(bb_coro_arg *c, ci_ptr_arg self, ci_ptr a) { \
	BB_CHECK_NUMBER(a); \
	return (ci_ptr)ci_number_floating(cfn(ci_number_to_double(a))); \
}

/* two-arg double,double→double: math.atan(y, x), math.pow(b, e) */
#define BB_MATH_F2(name, cfn) \
static ci_ptr bb_math_##name(bb_coro_arg *c, ci_ptr_arg self, ci_ptr a, ci_ptr b) { \
	BB_CHECK_NUMBER(a); BB_CHECK_NUMBER(b); \
	return (ci_ptr)ci_number_floating(cfn(ci_number_to_double(a), ci_number_to_double(b))); \
}

/* ---- tier 1: core ---- */

BB_MATH_F1(sin,   sin)
BB_MATH_F1(cos,   cos)
BB_MATH_F1(tan,   tan)
BB_MATH_F1(asin,  asin)
BB_MATH_F1(acos,  acos)
BB_MATH_F1(sqrt,  sqrt)
BB_MATH_F1(exp,   exp)
BB_MATH_F1(log,   log)
BB_MATH_F1(floor, floor)
BB_MATH_F1(ceil,  ceil)
BB_MATH_F1(abs,   fabs)

/* atan(y) or atan(y, x) — x defaults to 1.0 */
static ci_ptr bb_math_atan(bb_coro_arg *c, ci_ptr_arg self, ci_ptr a, ci_ptr b) {
	BB_CHECK_NUMBER(a);
	double y = ci_number_to_double(a);
	double x = (b && bb_is_number(b)) ? ci_number_to_double(b) : 1.0;
	return (ci_ptr)ci_number_floating(atan2(y, x));
}

/* ---- tier 2: common ---- */

BB_MATH_F1(log2,  log2)
BB_MATH_F1(log10, log10)
BB_MATH_F1(cbrt,  cbrt)
BB_MATH_F1(exp2,  exp2)
BB_MATH_F1(trunc, trunc)
BB_MATH_F1(round, round)
BB_MATH_F2(hypot, hypot)
BB_MATH_F2(pow,   pow)
BB_MATH_F2(fmod,  fmod)

static ci_ptr bb_math_sign(bb_coro_arg *c, ci_ptr_arg self, ci_ptr a) {
	BB_CHECK_NUMBER(a);
	double v = ci_number_to_double(a);
	int s = (v > 0.0) ? 1 : (v < 0.0) ? -1 : 0;
	return CI_PACKINT(s);
}

static ci_ptr bb_math_deg(bb_coro_arg *c, ci_ptr_arg self, ci_ptr a) {
	BB_CHECK_NUMBER(a);
	return (ci_ptr)ci_number_floating(ci_number_to_double(a) * (180.0 / M_PI));
}

static ci_ptr bb_math_rad(bb_coro_arg *c, ci_ptr_arg self, ci_ptr a) {
	BB_CHECK_NUMBER(a);
	return (ci_ptr)ci_number_floating(ci_number_to_double(a) * (M_PI / 180.0));
}

/* ---- tier 3: predicates & clamp ---- */

static ci_ptr bb_math_isnan(bb_coro_arg *c, ci_ptr_arg self, ci_ptr a) {
	BB_CHECK_NUMBER(a);
	return CI_BOOL(isnan(ci_number_to_double(a)));
}

static ci_ptr bb_math_isinf(bb_coro_arg *c, ci_ptr_arg self, ci_ptr a) {
	BB_CHECK_NUMBER(a);
	return CI_BOOL(isinf(ci_number_to_double(a)));
}

static ci_ptr bb_math_isfinite(bb_coro_arg *c, ci_ptr_arg self, ci_ptr a) {
	BB_CHECK_NUMBER(a);
	return CI_BOOL(isfinite(ci_number_to_double(a)));
}

/* math.clamp(x, lo, hi) */
static bb_var_ret bb_math_clamp(bb_coro_arg *c, ci_ptr self, size_t nargs, ci_ptr *args, size_t nrets) {
	(void)self; (void)nrets;
	if (nargs < 3)
		bb_coro_error(c, "math.clamp: need 3 args (x, lo, hi)");
	BB_CHECK_NUMBER(args[0]);
	BB_CHECK_NUMBER(args[1]);
	BB_CHECK_NUMBER(args[2]);
	double x  = ci_number_to_double(args[0]);
	double lo = ci_number_to_double(args[1]);
	double hi = ci_number_to_double(args[2]);
	if (x < lo) x = lo;
	if (x > hi) x = hi;
	BB_PUSH_RET((ci_ptr)ci_number_floating(x));
	return nargs;
}

/* math.min(...) / math.max(...) — vararg */
static bb_var_ret bb_math_min(bb_coro_arg *c, ci_ptr self, size_t nargs, ci_ptr *args, size_t nrets) {
	(void)self; (void)nrets;
	if (nargs < 1)
		bb_coro_error(c, "math.min: need at least 1 arg");
	BB_CHECK_NUMBER(args[0]);
	double best = ci_number_to_double(args[0]);
	for (size_t i = 1; i < nargs; i++) {
		BB_CHECK_NUMBER(args[i]);
		double v = ci_number_to_double(args[i]);
		if (v < best) best = v;
	}
	BB_PUSH_RET((ci_ptr)ci_number_floating(best));
	return nargs;
}

static bb_var_ret bb_math_max(bb_coro_arg *c, ci_ptr self, size_t nargs, ci_ptr *args, size_t nrets) {
	(void)self; (void)nrets;
	if (nargs < 1)
		bb_coro_error(c, "math.max: need at least 1 arg");
	BB_CHECK_NUMBER(args[0]);
	double best = ci_number_to_double(args[0]);
	for (size_t i = 1; i < nargs; i++) {
		BB_CHECK_NUMBER(args[i]);
		double v = ci_number_to_double(args[i]);
		if (v > best) best = v;
	}
	BB_PUSH_RET((ci_ptr)ci_number_floating(best));
	return nargs;
}

/* math.type(x) — "int" for tagged/boxed int, "float" for boxed double, null otherwise */
static ci_ptr bb_math_type(bb_coro_arg *c, ci_ptr_arg self, ci_ptr x) {
	if (CI_IS_INT(x))
		return BB_CSTR(c->vm, "int");
	if (CI_IS_NUMBER(x))
		return CI_NUMBER_IS_DOUBLE(x) ? BB_CSTR(c->vm, "float") : BB_CSTR(c->vm, "int");
	return NULL;
}

/* ---- existing functions ---- */

/* math.boxed(x) — true if x is a boxed ci_number, false if tagged int */
static ci_ptr bb_math_boxed(bb_coro_arg *c, ci_ptr_arg self, ci_ptr x) {
	if (CI_IS_NUMBER(x))
		return CI_BOOL(1);
	if (CI_IS_INT(x))
		return CI_BOOL(0);
	return NULL;
}

#define BB_MATH_DEFAULT_EPS 1e-9

/* math.cmp(a, b, eps?) — compare numbers, returns -1/0/1 */
static bb_var_ret bb_math_cmp(bb_coro_arg *c, ci_ptr self, size_t nargs, ci_ptr *args, size_t nrets) {
	(void)self; (void)nrets;
	if (nargs < 2)
		bb_coro_error(c, "math.cmp: need at least 2 args");

	ci_ptr a = args[0];
	ci_ptr b = args[1];
	double eps = ci_number_get_cmp_precision();

	if (nargs >= 3 && bb_is_number(args[2]))
		eps = ci_number_to_double(args[2]);

	int r = ci_number_cmp_eps(a, b, eps);
	BB_PUSH_RET(CI_PACKINT(r));
	return nargs;
}

/* math.eq(a, b, eps?) — convenience: math.cmp(a, b, eps) == 0 */
static bb_var_ret bb_math_eq(bb_coro_arg *c, ci_ptr self, size_t nargs, ci_ptr *args, size_t nrets) {
	(void)self; (void)nrets;
	if (nargs < 2)
		bb_coro_error(c, "math.eq: need at least 2 args");

	double eps = ci_number_get_cmp_precision();
	if (nargs >= 3 && bb_is_number(args[2]))
		eps = ci_number_to_double(args[2]);

	BB_PUSH_RET(CI_BOOL(ci_number_cmp_eps(args[0], args[1], eps) == 0));
	return nargs;
}

/* math.precision() — get; math.precision(eps) — set; returns current value */
static ci_ptr bb_math_precision(bb_coro_arg *c, ci_ptr_arg self, ci_ptr x) {
	double old = ci_number_get_cmp_precision();
	if (x && bb_is_number(x))
		ci_number_set_cmp_precision(ci_number_to_double(x));
	return (ci_ptr)ci_number_floating(old);
}

/* ---- registration ---- */

static void bb_lib_math_init(bb_vm *vm) {
	ci_map *ns = ci_map_new(48);

	static const bb_cfunc math_lib[] = {
		/* tier 1 — core */
		{ "sin",       bb_math_sin,       0 },
		{ "cos",       bb_math_cos,       0 },
		{ "tan",       bb_math_tan,       0 },
		{ "asin",      bb_math_asin,      0 },
		{ "acos",      bb_math_acos,      0 },
		{ "atan",      bb_math_atan,      0 },
		{ "sqrt",      bb_math_sqrt,      0 },
		{ "exp",       bb_math_exp,       0 },
		{ "log",       bb_math_log,       0 },
		{ "floor",     bb_math_floor,     0 },
		{ "ceil",      bb_math_ceil,      0 },
		{ "abs",       bb_math_abs,       0 },
		/* tier 2 — common */
		{ "log2",      bb_math_log2,      0 },
		{ "log10",     bb_math_log10,     0 },
		{ "cbrt",      bb_math_cbrt,      0 },
		{ "exp2",      bb_math_exp2,      0 },
		{ "trunc",     bb_math_trunc,     0 },
		{ "round",     bb_math_round,     0 },
		{ "hypot",     bb_math_hypot,     0 },
		{ "pow",       bb_math_pow,       0 },
		{ "fmod",      bb_math_fmod,      0 },
		{ "sign",      bb_math_sign,      0 },
		{ "deg",       bb_math_deg,       0 },
		{ "rad",       bb_math_rad,       0 },
		/* tier 3 — predicates & util */
		{ "isnan",     bb_math_isnan,     0 },
		{ "isinf",     bb_math_isinf,     0 },
		{ "isfinite",  bb_math_isfinite,  0 },
		{ "clamp",     bb_math_clamp,     BB_FN_NATIVE_VAR },
		{ "min",       bb_math_min,       BB_FN_NATIVE_VAR },
		{ "max",       bb_math_max,       BB_FN_NATIVE_VAR },

		
		{ "type",      bb_math_type,      0 },
		{ "boxed",     bb_math_boxed,     0 },
		{ "cmp",       bb_math_cmp,       BB_FN_NATIVE_VAR },
		{ "eq",        bb_math_eq,        BB_FN_NATIVE_VAR },
		{ "precision", bb_math_precision, 0 },
	};

	bb_func2map(vm, ns, math_lib, sizeof(math_lib) / sizeof(math_lib[0]));

	/* constants — stored as boxed ci_number values directly in namespace */
#define _DEF_FLOAT_CONSTANT(map, name, value) \
	ci_map_put(map, BB_CSTR(vm, name), (ci_ptr)ci_number_floating(value))

	_DEF_FLOAT_CONSTANT(ns, "pi",   M_PI);
	_DEF_FLOAT_CONSTANT(ns, "e",    M_E);
	_DEF_FLOAT_CONSTANT(ns, "inf",  INFINITY);
	_DEF_FLOAT_CONSTANT(ns, "nan",  NAN);
	_DEF_FLOAT_CONSTANT(ns, "huge", HUGE_VAL);

#undef _DEF_FLOAT_CONSTANT

	ci_map_put(vm->globals, bb_vm_istring(vm, "math", 4), (ci_ptr)ns);
}
