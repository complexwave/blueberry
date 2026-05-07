/*
 * ci_number.c — Citrin boxed number type (ints for maths)
 *
 * Slowpath for precision: tagged pointer ints (CI_PACKINT) handle the
 * common case.  ci_number boxes values that overflow 63 bits, or that
 * need double representation.
 *
 * Two subtypes only:
 *   CI_NUM_I128  — signed 128-bit integer (the one integer type)
 *   CI_NUM_F64   — double precision float
 *
 * Native-width overflow semantics (32/64-bit wrapping) are deliberately
 * excluded — that belongs in FFI / native-type territory, not general maths.
 * Users who need 64-bit bitmask behavior should mask explicitly.
 *
 * Bitwise ops convert doubles to int in-place before operating.
 * shift(n, amount): positive=left, negative=right. Clamped to 128 bits.
 * Right shift is always arithmetic (sign-extending).
 *
 * Single arena tag (VM family e6), subtype stored in gc.flags lower 8 bits.
 * Include-style — do not compile separately.
 */

#include "ciobj.h"
#include <stdio.h>
#include <math.h>

/* ============================================================
 * Global comparison precision for relational operators
 * ============================================================ */

static double ci_number_cmp_precision = 1e-9;

static inline double ci_number_get_cmp_precision(void) {
	return ci_number_cmp_precision;
}

static inline void ci_number_set_cmp_precision(double eps) {
	ci_number_cmp_precision = eps;
}

/* ============================================================
 * Tag
 * ============================================================ */

#define CI_VM_NUMBER_FAMILY  CI_O_FAMILY_8

#ifndef CI_VM_FAMILY
#define CI_VM_FAMILY  CI_O_FAMILY_8
#endif

#define CI_NUMBER  ((uint16_t)(CI_FAMILY_ENTRY(CI_VM_FAMILY, 6) | CI_OBJECT | CI_REFCOUNTABLE))

#define CI_IS_NUMBER(p)  CI_CHECK_MASK_FAMILY(p, CI_NUMBER, CI_VM_FAMILY)

/* ============================================================
 * Subtype flags (gc.flags lower 8 bits)
 * ============================================================ */

#define CI_NUM_DOUBLE  (1 << 3)
#define CI_NUM_I128    0           /* default: signed 128-bit integer */
#define CI_NUM_F64     CI_NUM_DOUBLE

#define CI_NUMBER_FLAGS(p, mask)  (((ci_number *)(p))->gc.flags & (mask))

#define CI_NUMBER_IS_DOUBLE(p)  CI_NUMBER_FLAGS(p, CI_NUM_DOUBLE)

/* ============================================================
 * Struct
 * ============================================================ */

typedef struct {
	CI_GC_HDR;
	union {
		__int128 i128;
		double   f64;
	};
} ci_number;

/* ============================================================
 * Registration
 * ============================================================ */

static void ci_number_destructor(void *ptr, tg_arena_t *arena) {
	(void)ptr;
	(void)arena;
	/* no heap allocs to free */
}

void ci_number_register(void) {
	tg_arena_ops ops = { ci_number_destructor, NULL, NULL };
	ci_register_ops(CI_NUMBER, sizeof(ci_number), &ops);
}

/* ============================================================
 * Allocation
 * ============================================================ */

static inline ci_number *ci_number_new(uint16_t type) {
	ci_number *n = ci_new(CI_NUMBER);
	if (!n) return NULL;
	n->gc.flags = (uint16_t)(type & 0xFF);
	n->i128 = 0;
	return n;
}

static inline ci_number *ci_number_floating(double v) {
	ci_number *n = ci_number_new(CI_NUM_F64);
	if (!n) return NULL;
	n->f64 = v;
	return n;
}

/*
 * ci_number_int — return __int128 as tagged int if it fits, boxed otherwise.
 * Tagged ints are 63-bit signed (CI_PACKINT shifts left by 1).
 */
static inline ci_ptr ci_number_int(__int128 v) {
	if (v == (intptr_t)v)
		return CI_PACKINT((intptr_t)v);

	ci_number *dst = ci_number_new(CI_NUM_I128);
	if (!dst) return NULL;
	dst->i128 = v;
	return (ci_ptr)dst;
}

/* ============================================================
 * Conversion (in-place)
 * ============================================================ */

/* convert whatever is inside to double, in-place */
static inline void ci_number_convert_double(ci_number *n) {
	if (CI_NUMBER_FLAGS(n, CI_NUM_DOUBLE)) return;
	n->f64 = (double)n->i128;
	n->gc.flags = CI_NUM_DOUBLE;
}

/* convert whatever is inside to int, in-place */
static inline void ci_number_convert_int(ci_number *n) {
	if (!CI_NUMBER_FLAGS(n, CI_NUM_DOUBLE)) return;
	n->i128 = (__int128)n->f64;
	n->gc.flags = CI_NUM_I128;
}

/* ============================================================
 * To-double (non-destructive, works on ci_ptr)
 * ============================================================ */

static inline double ci_number_to_double(ci_ptr p) {
	if (CI_IS_INT(p))
		return (double)CI_INT(p);

	if (!CI_IS_NUMBER(p)) {
		fprintf(stderr, "ci_number_to_double: not a number\n");
		return 0.0;
	}

	ci_number *n = (ci_number *)p;
	if (CI_NUMBER_FLAGS(n, CI_NUM_DOUBLE))
		return n->f64;
	return (double)n->i128;
}

/* ============================================================
 * To-int128 (non-destructive, works on ci_ptr)
 * ============================================================ */

static inline __int128 ci_number_to_int(ci_ptr p) {
	if (CI_IS_INT(p))
		return (__int128)CI_INT(p);

	if (!CI_IS_NUMBER(p)) {
		fprintf(stderr, "ci_number_to_int: not a number\n");
		return 0;
	}

	ci_number *n = (ci_number *)p;
	if (CI_NUMBER_FLAGS(n, CI_NUM_DOUBLE))
		return (__int128)n->f64;
	return n->i128;
}

/* ============================================================
 * Resolve type for binary op
 *
 * If either operand is double, result is double.
 * Otherwise result is i128.
 * ============================================================ */

static inline int ci_number_is_float_op(ci_ptr a, ci_ptr b) {
	if (CI_IS_NUMBER(a) && CI_NUMBER_IS_DOUBLE(a))
		return 1;
	if (CI_IS_NUMBER(b) && CI_NUMBER_IS_DOUBLE(b))
		return 1;
	return 0;
}

/* ============================================================
 * Arithmetic ops
 *
 * Float path: always allocate ci_number f64.
 * Int path: compute into __int128, try to return tagged int
 * via ci_number_int(), only box if it doesn't fit.
 * ============================================================ */

#define CI_NUMBER_BINOP(name, double_op) \
static ci_ptr ci_number_##name(ci_ptr a, ci_ptr b) { \
	if (ci_number_is_float_op(a, b)) \
		return (ci_ptr)ci_number_floating( \
			ci_number_to_double(a) double_op ci_number_to_double(b)); \
	return ci_number_int(ci_number_to_int(a) double_op ci_number_to_int(b)); \
}

CI_NUMBER_BINOP(add, +)
CI_NUMBER_BINOP(sub, -)
CI_NUMBER_BINOP(mul, *)

/* div is special: int div-by-zero → double infinity */
static ci_ptr ci_number_div(ci_ptr a, ci_ptr b) {
	if (ci_number_is_float_op(a, b)) {
		double da = ci_number_to_double(a);
		double db = ci_number_to_double(b);
		if (db == 0.0)
			return (ci_ptr)ci_number_floating(
				(da == 0.0) ? NAN : ((da < 0.0) ? -INFINITY : INFINITY));
		return (ci_ptr)ci_number_floating(da / db);
	}

	__int128 ia = ci_number_to_int(a);
	__int128 ib = ci_number_to_int(b);
	if (ib == 0)
		return (ci_ptr)ci_number_floating((ia < 0) ? -INFINITY : INFINITY);
	return ci_number_int(ia / ib);
}

static ci_ptr ci_number_mod(ci_ptr a, ci_ptr b) {
	if (ci_number_is_float_op(a, b)) {
		double db = ci_number_to_double(b);
		if (db == 0.0)
			return (ci_ptr)ci_number_floating(NAN);
		return (ci_ptr)ci_number_floating(fmod(ci_number_to_double(a), db));
	}

	__int128 ia = ci_number_to_int(a);
	__int128 ib = ci_number_to_int(b);
	if (ib == 0)
		return (ci_ptr)ci_number_floating(NAN);
	return ci_number_int(ia % ib);
}

/* unary negate */
static ci_ptr ci_number_neg(ci_ptr a) {
	if (CI_IS_INT(a))
		return ci_number_int(-(__int128)CI_INT(a));

	if (!CI_IS_NUMBER(a)) return NULL;
	ci_number *src = (ci_number *)a;

	if (CI_NUMBER_IS_DOUBLE(src))
		return (ci_ptr)ci_number_floating(-src->f64);

	return ci_number_int(-src->i128);
}

/* integer power — exponentiation by squaring */
static ci_ptr ci_number_pow(ci_ptr a, ci_ptr b) {
	/* non-int or negative exp → fpow */
	if (CI_IS_NUMBER(b) && CI_NUMBER_IS_DOUBLE(b))
		goto use_fpow;

	int32_t exp = (int32_t)ci_number_to_int(b);

	if (exp < 0)
		goto use_fpow;

	/* either operand is double → fpow */
	if (CI_IS_NUMBER(a) && CI_NUMBER_IS_DOUBLE(a))
		goto use_fpow;

	__int128 base;
	if (CI_IS_INT(a))
		base = (__int128)CI_INT(a);
	else if (CI_IS_NUMBER(a))
		base = ((ci_number *)a)->i128;
	else
		return NULL;

	__int128 result = 1;
	while (exp > 0) {
		if (exp & 1)
			result *= base;
		base *= base;
		exp >>= 1;
	}

	/* try to return tagged int */
	if (result >= INT32_MIN && result <= INT32_MAX)
		return CI_PACKINT((intptr_t)result);

	ci_number *dst = ci_number_new(CI_NUM_I128);
	if (!dst) return NULL;
	dst->i128 = result;
	return (ci_ptr)dst;

use_fpow:;
	double da = ci_number_to_double(a);
	double db = ci_number_to_double(b);
	ci_number *dst2 = ci_number_new(CI_NUM_F64);
	if (!dst2) return NULL;
	dst2->f64 = pow(da, db);
	return (ci_ptr)dst2;
}

/* ============================================================
 * Comparison — returns -1, 0, 1
 * ============================================================ */

/*
 * ci_number_cmp_eps — compare with explicit epsilon for floats.
 * If either operand is a double, uses: |a-b| <= eps * max(|a|,|b|)
 * For pure ints, eps is ignored (exact compare).
 */
static inline int ci_number_cmp_eps(ci_ptr a, ci_ptr b, double eps) {
	if (ci_number_is_float_op(a, b)) {
		double da = ci_number_to_double(a);
		double db = ci_number_to_double(b);
		double diff = da - db;
		if (diff < 0) diff = -diff;
		double mag = fabs(da);
		double mb  = fabs(db);
		if (mb > mag) mag = mb;
		if (mag == 0.0 || diff <= eps * mag)
			return 0;
		return (da < db) ? -1 : 1;
	}

	__int128 ia = ci_number_to_int(a);
	__int128 ib = ci_number_to_int(b);
	if (ia < ib) return -1;
	if (ia > ib) return 1;
	return 0;
}

static inline int ci_number_cmp(ci_ptr a, ci_ptr b) {
	return ci_number_cmp_eps(a, b, ci_number_cmp_precision);
}

/* ============================================================
 * Bitwise ops — convert doubles to int first
 * ============================================================ */

/* coerce ci_ptr to __int128 for bitops, converting double in-place */
static inline __int128 ci_number_as_int(ci_ptr p) {
	if (CI_IS_INT(p))
		return (__int128)CI_INT(p);
	if (CI_IS_NUMBER(p) && CI_NUMBER_IS_DOUBLE(p))
		ci_number_convert_int((ci_number *)p);
	return ci_number_to_int(p);
}

static ci_number *ci_number_and(ci_ptr a, ci_ptr b) {
	ci_number *dst = ci_number_new(CI_NUM_I128);
	if (!dst) return NULL;
	dst->i128 = ci_number_as_int(a) & ci_number_as_int(b);
	return dst;
}

static ci_number *ci_number_or(ci_ptr a, ci_ptr b) {
	ci_number *dst = ci_number_new(CI_NUM_I128);
	if (!dst) return NULL;
	dst->i128 = ci_number_as_int(a) | ci_number_as_int(b);
	return dst;
}

static ci_number *ci_number_xor(ci_ptr a, ci_ptr b) {
	ci_number *dst = ci_number_new(CI_NUM_I128);
	if (!dst) return NULL;
	dst->i128 = ci_number_as_int(a) ^ ci_number_as_int(b);
	return dst;
}

static ci_number *ci_number_not(ci_ptr a) {
	ci_number *dst = ci_number_new(CI_NUM_I128);
	if (!dst) return NULL;
	dst->i128 = ~ci_number_as_int(a);
	return dst;
}

/*
 * ci_number_shift(a, amount)
 *   amount > 0 → left shift
 *   amount < 0 → right shift (arithmetic, sign-extending)
 *   |amount| >= 128 → 0 for left, 0 or -1 for right (sign-fill)
 */
static ci_ptr ci_number_shift(ci_ptr a, int amount) {
	__int128 v = ci_number_as_int(a);
	__int128 r;

	if (amount >= 128) {
		r = 0;
	} else if (amount > 0) {
		r = v << amount;
	} else if (amount <= -128) {
		r = (v < 0) ? -1 : 0;
	} else {
		/* arithmetic right shift — C guarantees this for signed types
		 * on virtually all implementations, and __int128 follows suit */
		r = v >> (-amount);
	}
	return ci_number_int(r);
}

/* convenience wrappers */
static inline ci_ptr ci_number_lshift(ci_ptr a, int amount) {
	return ci_number_shift(a, amount);
}

static inline ci_ptr ci_number_rshift(ci_ptr a, int amount) {
	return ci_number_shift(a, -amount);
}

/* ============================================================
 * Debug print
 * ============================================================ */

static void ci_number_print(ci_ptr p) {
	if (CI_IS_INT(p)) {
		printf("%ld", (long)CI_INT(p));
		return;
	}
	if (!CI_IS_NUMBER(p)) {
		printf("<not-a-number>");
		return;
	}

	ci_number *n = (ci_number *)p;
	if (CI_NUMBER_FLAGS(n, CI_NUM_DOUBLE)) {
		printf("%g", n->f64);
	} else {
		__int128 v = n->i128;
		if (v < 0) {
			printf("-");
			v = -v;
		}
		uint64_t hi = (uint64_t)((unsigned __int128)v >> 64);
		uint64_t lo = (uint64_t)v;
		if (hi)
			printf("0x%lx%016lx", hi, lo);
		else
			printf("%lu", lo);
	}
}
