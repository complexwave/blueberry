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
#include "lib/uscale/uscale_min.c"

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

#define CI_NUMBER  ((uint16_t)(CI_FAMILY_ENTRY(CI_VM_FAMILY, 6) | CI_REFCOUNTABLE))

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
#ifndef CI_NUMBER_ALWAYS_BOX
	if (v == (intptr_t)v)
		return CI_PACKINT((intptr_t)v);
#endif

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

static ci_ptr ci_number_add(ci_ptr a, ci_ptr b) {
	if (ci_number_is_float_op(a, b))
		return (ci_ptr)ci_number_floating(
			ci_number_to_double(a) + ci_number_to_double(b));
	__int128 ia = ci_number_to_int(a);
	__int128 ib = ci_number_to_int(b);
	__int128 r;
	
	if (__builtin_add_overflow(ia, ib, &r))
		return (ci_ptr)ci_number_floating(
			ci_number_to_double(a) + ci_number_to_double(b));
		
	return ci_number_int(r);
}

static ci_ptr ci_number_sub(ci_ptr a, ci_ptr b) {
	if (ci_number_is_float_op(a, b))
		return (ci_ptr)ci_number_floating(
			ci_number_to_double(a) - ci_number_to_double(b));
		
	__int128 ia = ci_number_to_int(a);
	__int128 ib = ci_number_to_int(b);
	__int128 r;
	
	if (__builtin_sub_overflow(ia, ib, &r))
		return (ci_ptr)ci_number_floating(
			ci_number_to_double(a) - ci_number_to_double(b));
		
	return ci_number_int(r);
}

static ci_ptr ci_number_mul(ci_ptr a, ci_ptr b) {
	if (ci_number_is_float_op(a, b))
		return (ci_ptr)ci_number_floating(
			ci_number_to_double(a) * ci_number_to_double(b));
		
	__int128 ia = ci_number_to_int(a);
	__int128 ib = ci_number_to_int(b);
	__int128 r;
	
	if (__builtin_mul_overflow(ia, ib, &r))
		return (ci_ptr)ci_number_floating(
			ci_number_to_double(a) * ci_number_to_double(b));
		
	return ci_number_int(r);
}

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

	if (!CI_IS_NUMBER(a)) 
		return NULL;
	
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
		if (exp & 1) {
			if (__builtin_mul_overflow(result, base, &result))
				goto use_fpow;
		}
		if (exp > 1) {
			if (__builtin_mul_overflow(base, base, &base))
				goto use_fpow;
		}
		exp >>= 1;
	}

	return ci_number_int(result);

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
		if (da == db) return 0;
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

/* ============================================================
 * Integer/Float to string (using uscale)
 * ============================================================ */

#define CI_NUMBER_PRINT_MAX_INT_DIGITS_SCI  10
#define CI_NUMBER_PRINT_MAX_FLOAT_DIGITS_SCI -6
#define CI_NUMBER_BUF_INT128  42  /* 39 digits + sign + null + spare */
#define CI_NUMBER_BUF_DEFAULT 25  /* 64-bit int (20) or double (24) + null */

/*
 * ci_number_stringmax — return required buffer size for printing.
 */
static inline int ci_number_stringmax(ci_ptr p) {
	if (CI_IS_INT(p))
		return CI_NUMBER_BUF_DEFAULT;
	if (CI_IS_NUMBER(p) && !CI_NUMBER_IS_DOUBLE(p))
		return CI_NUMBER_BUF_INT128;
	return CI_NUMBER_BUF_DEFAULT;
}

/*
 * ci_number_itoa — uint64 to decimal string.
 * Writes digits into buf, returns number of bytes written.
 */
static inline int ci_number_itoa(uint8_t * restrict buf, uint64_t v) {
	int n = 0;
	do {
		buf[n++] = '0' + (v % 10);
		v /= 10;
	} while (v);
	for (int i = 0, j = n - 1; i < j; i++, j--) {
		uint8_t c = buf[i]; buf[i] = buf[j]; buf[j] = c;
	}
	return n;
}

/*
 * ci_number_itoa128 — unsigned 128-bit int to decimal string.
 * Writes digits into buf, returns number of bytes written.
 */
static int ci_number_itoa128(uint8_t * restrict buf, unsigned __int128 v) {
	if (v <= UINT64_MAX)
		return ci_number_itoa(buf, (uint64_t)v);

	/* split: v = hi * 10^19 + lo */
	const uint64_t div19 = 10000000000000000000ULL; /* 1e19 */
	uint64_t lo = (uint64_t)(v % div19);
	uint64_t hi = (uint64_t)(v / div19);

	int n = ci_number_itoa(buf, hi);

	/* lo must be zero-padded to 19 digits */
	uint8_t tmp[20];
	int ln = ci_number_itoa(tmp, lo);
	for (int i = 0; i < 19 - ln; i++)
		buf[n++] = '0';
	for (int i = 0; i < ln; i++)
		buf[n++] = tmp[i];

	return n;
}

/*
 * ci_number_dtoa — double to string (pretty format).
 * Returns number of bytes written.
 */
#define CI_DTOA_COPY while (src < end) *out++ = *src++;
#define CI_DTOA_CH(ch) *out++ = ch;

static int ci_number_dtoa(uint8_t * restrict dst, double f, int max_ints, int fdmax) {
	uint8_t *out = dst;

	if (f < 0) {
		*out++ = '-';
		f = -f;
	}

	/* TODO: handle 0, inf, nan */

	uscale_digits r = uscale_dtoa_short(f);

	uint8_t digits[32];
	int int_digits = ci_number_itoa(digits, r.d);

	int p = r.p;
	uint8_t *src = digits;
	uint8_t *end = src + int_digits;

	if (p >= 0) {
		int trailing_zeros = p;
		int int_print_length = int_digits + p;

		if (int_print_length > max_ints) {
			if (p) {
				goto print_normalized;
			}
		} else {
			CI_DTOA_COPY;

			while(trailing_zeros--)
				CI_DTOA_CH('0');

			goto finalize;
		}
	}

	int float_total_print_digits = -p;
	int int_print_length = int_digits - float_total_print_digits;
	if (int_print_length < 0) int_print_length = 0;

	int float_significant_digits = int_digits - int_print_length;

	int leading_zeroes = float_total_print_digits - float_significant_digits;

	if (fdmax < 0) {
		fdmax = -fdmax;

		if (float_total_print_digits >= fdmax) {
			goto print_normalized;
		}
	}

	if (!fdmax && leading_zeroes) {
		goto print_normalized;
	}

	if (int_print_length > 0) {
		if (int_print_length > max_ints)
			goto print_normalized;

		while (int_print_length--)
			*out++ = *src++;

		CI_DTOA_CH('.');
	} else {
		CI_DTOA_CH('0');
		CI_DTOA_CH('.');
	}

	if (fdmax) {
		while (leading_zeroes-- && fdmax) {
			CI_DTOA_CH('0');
			fdmax--;
		}

		while (fdmax-- && (src < end))
			*out++ = *src++;

		goto finalize;
	}

	CI_DTOA_COPY;
	goto finalize;

print_normalized:
	p = int_digits - 1 + r.p;

	src = digits;
	*out++ = *src++;

	if (src < end) {
		*out++ = '.';
		CI_DTOA_COPY;
	}

	if (p) {
		*out++ = 'e';

		if (p < 0) {
			*out++ = '-';
			p = -p;
		}

		int exponent_length = ci_number_itoa(digits, p);
		src = digits;
		end = src + exponent_length;

		CI_DTOA_COPY;
	}

finalize:
	*out = 0;
	return out - dst;
}

#undef CI_DTOA_COPY
#undef CI_DTOA_CH

/*
 * ci_number_fromstring — parse number from string.
 *
 * Fast path: accumulate into int64 (up to 17 digits safe).
 * Overflow: promote to __int128 (up to 37 digits safe).
 * Float: if '.' or 'e' encountered, use uscale_atod.
 * Trailing junk after the number → return NULL.
 */

#define CI_ATOD_MAX_INT64_DIGITS   17  /* 10^17 < INT64_MAX */
#define CI_ATOD_MAX_I128_DIGITS    37  /* 10^37 < INT128_MAX */
#define CI_ATOD_MAX_FLOAT_DIGITS   19  /* uscale handles up to 19 */

#define CI_IS_DIGIT(c)  ((unsigned)((c) - '0') <= 9)
#define CI_IS_WS(c)     ((c) == ' ' || (c) == '\t' || (c) == '\r' || (c) == '\n')
#define CI_IS_EXP(c)    ((c) == 'e' || (c) == 'E')

static ci_ptr ci_number_fromstring(const uint8_t *src, size_t len) {
	const uint8_t *end = src + len;
	ci_ptr ret;

	#define HAS_DATA  (src < end)
	#define PEEK      (*src)
	#define NEXT      (*src++)
	#define IS_FLOAT  (HAS_DATA && (PEEK == '.' || CI_IS_EXP(PEEK)))

	/* skip leading whitespace */
	while (HAS_DATA && CI_IS_WS(PEEK)) src++;
	if (!HAS_DATA) return NULL;

	/* sign */
	int neg = 0;
	if (PEEK == '-')      { neg = 1; src++; }
	else if (PEEK == '+') { src++; }

	if (!HAS_DATA || (!CI_IS_DIGIT(PEEK) && PEEK != '.'))
		return NULL;

	/* int64 fast path */
	int64_t v64 = 0;
	int digits = 0;

	while (HAS_DATA && CI_IS_DIGIT(PEEK)) {
		v64 = v64 * 10 + (NEXT - '0');
		digits++;
		if (digits >= CI_ATOD_MAX_INT64_DIGITS)
			goto read_i128;
	}

	if(!digits) return NULL;
	
	if (IS_FLOAT) goto read_float;

	ret = ci_number_int(neg ? -(__int128)v64 : (__int128)v64);
	goto consume_tail;

	read_i128:;
	{
		__int128 v128 = (__int128)v64;

		while (HAS_DATA && CI_IS_DIGIT(PEEK)) {
			v128 = v128 * 10 + (NEXT - '0');
			digits++;
			if (digits >= CI_ATOD_MAX_I128_DIGITS) {
				
				static const __int128 i128_max = ((unsigned __int128)1 << 127) - 1;
				ret = ci_number_int(neg ? ~i128_max : i128_max);
				
				goto consume_tail;
			}
		}

		if (IS_FLOAT) goto read_float;

		ret = ci_number_int(neg ? -v128 : v128);
		
		goto expected_none;
	}

	read_float:;
	{
		uint64_t mant = (uint64_t)v64;
		int frac_digits = 0;
		int sig_digits = digits;

		if (HAS_DATA && PEEK == '.') {
			src++;
			while (HAS_DATA && CI_IS_DIGIT(PEEK)) {
				if (sig_digits < CI_ATOD_MAX_FLOAT_DIGITS) {
					mant = mant * 10 + (PEEK - '0');
					sig_digits++;
				}
				frac_digits++;
				src++;
			}
		}

		int exp = 0;
		if (HAS_DATA && CI_IS_EXP(PEEK)) {
			src++;
			int exp_neg = 0;
			if (HAS_DATA && PEEK == '-')      { exp_neg = 1; src++; }
			else if (HAS_DATA && PEEK == '+') { src++; }
			while (HAS_DATA && CI_IS_DIGIT(PEEK)) {
				exp = exp * 10 + (NEXT - '0');
				if (exp > 999) { exp = 999; break; }
			}
			while (HAS_DATA && CI_IS_DIGIT(PEEK)) src++;
			if (exp_neg) exp = -exp;
		}

		double fval = uscale_atod(mant, exp - frac_digits);
		if (neg) fval = -fval;
		ret = (ci_ptr)ci_number_floating(fval);
		goto expected_none;
	}

	consume_tail:
	if (!HAS_DATA) return ret;

	// whitespace*
	while (HAS_DATA && CI_IS_WS(PEEK)) src++;
	
	//\d*
	while (HAS_DATA && CI_IS_DIGIT(PEEK)) src++;
	
	//.\d*
	if (HAS_DATA && PEEK == '.') {
		src++;
		while (HAS_DATA && CI_IS_DIGIT(PEEK)) src++;
	}
	
	//e[+-]?\d*
	if (HAS_DATA && CI_IS_EXP(PEEK)) {
		src++;
		if (HAS_DATA && (PEEK == '+' || PEEK == '-')) src++;
		while (HAS_DATA && CI_IS_DIGIT(PEEK)) src++;
	}
	
	expected_none:
	if (!HAS_DATA) return ret;
	
	// whitespace*
	while (HAS_DATA && CI_IS_WS(PEEK)) src++;
	if (!HAS_DATA) return ret;
	
	ci_dec(ret);
	return NULL;

	#undef HAS_DATA
	#undef PEEK
	#undef NEXT
	#undef IS_FLOAT
}


/*
 * ci_number_tostring — number to string with formatting control.
 * max_ints: max integer digits before scientific (use CI_NUMBER_PRINT_MAX_INT_DIGITS_SCI)
 * fdmax:    >0 = max fractional digits, 0 = all, <0 = scientific threshold
 * Returns number of bytes written (no null terminator added).
 * Caller must preallocate buf via ci_number_stringmax().
 */
static int ci_number_tostring(ci_ptr p, uint8_t * restrict buf, int max_ints, int fdmax) {
	if (CI_IS_INT(p)) {
		intptr_t v = CI_INT(p);
		if (v < 0) {
			*buf = '-';
			return 1 + ci_number_itoa(buf + 1, (uint64_t)(-v));
		}
		return ci_number_itoa(buf, (uint64_t)v);
	}

	if (!CI_IS_NUMBER(p))
		return 0;

	ci_number *n = (ci_number *)p;

	if (CI_NUMBER_IS_DOUBLE(n))
		return ci_number_dtoa(buf, n->f64, max_ints, fdmax);

	/* i128 */
	__int128 v = n->i128;
	if (v < 0) {
		*buf = '-';
		return 1 + ci_number_itoa128(buf + 1, (unsigned __int128)(-v));
	}
	return ci_number_itoa128(buf, (unsigned __int128)v);
}
