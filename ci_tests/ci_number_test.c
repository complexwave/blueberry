/*
 * ci_number_test.c — test suite for ci_number
 *
 * Include chain:
 *   ci_number_test.c
 *     -> ciobj.c  (tgmemlib.c + ciobj.h + ci_string.c + ci_array.c + ci_map.c + ci_tree.c + ci_number.c)
 */
#define CI_STRING_TEST  /* suppress ciobj.c test main() */
#define CI_NUMBER_ALWAYS_BOX
#include "ciobj.c"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <float.h>

static int g_pass, g_fail;

#define CHECK(expr) do { \
	if (!(expr)) { \
		fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
		g_fail++; \
	} else { \
		g_pass++; \
	} \
} while (0)

#define SECTION(name) printf("  -- %s\n", name)

/* ================================================================
 * Allocation & subtypes
 * ================================================================ */

static void test_alloc(void) {
	SECTION("alloc & subtypes");

	ci_number *n;

	n = ci_number_new(CI_NUM_I128);
	CHECK(n != NULL);
	CHECK(CI_IS_NUMBER(n));
	CHECK(!CI_NUMBER_IS_DOUBLE(n));
	n->i128 = 42;
	CHECK(n->i128 == 42);
	ci_dec(n);

	n = ci_number_new(CI_NUM_F64);
	CHECK(n != NULL);
	CHECK(CI_NUMBER_IS_DOUBLE(n));
	n->f64 = 3.14;
	CHECK(n->f64 == 3.14);
	ci_dec(n);
}

/* ================================================================
 * Conversion (in-place)
 * ================================================================ */

static void test_convert(void) {
	SECTION("in-place conversion");

	ci_number *n;

	/* int → double */
	n = ci_number_new(CI_NUM_I128);
	n->i128 = 100;
	ci_number_convert_double(n);
	CHECK(CI_NUMBER_IS_DOUBLE(n));
	CHECK(n->f64 == 100.0);
	ci_dec(n);

	/* double → int */
	n = ci_number_new(CI_NUM_F64);
	n->f64 = 42.9;
	ci_number_convert_int(n);
	CHECK(!CI_NUMBER_IS_DOUBLE(n));
	CHECK(n->i128 == 42);  /* truncates */
	ci_dec(n);

	/* double already double → nop */
	n = ci_number_new(CI_NUM_F64);
	n->f64 = 1.5;
	ci_number_convert_double(n);
	CHECK(n->f64 == 1.5);
	ci_dec(n);

	/* int → convert_int is nop */
	n = ci_number_new(CI_NUM_I128);
	n->i128 = -77;
	ci_number_convert_int(n);
	CHECK(n->i128 == -77);
	ci_dec(n);
}

/* ================================================================
 * To-double / to-int (non-destructive, works on ci_ptr)
 * ================================================================ */

static void test_to_accessors(void) {
	SECTION("to_double / to_int");

	/* tagged int */
	ci_ptr p = CI_PACKINT(42);
	CHECK(ci_number_to_double(p) == 42.0);
	CHECK(ci_number_to_int(p) == 42);

	p = CI_PACKINT(-100);
	CHECK(ci_number_to_double(p) == -100.0);
	CHECK(ci_number_to_int(p) == -100);

	/* boxed i128 */
	ci_number *n = ci_number_new(CI_NUM_I128);
	n->i128 = -999;
	CHECK(ci_number_to_double(n) == -999.0);
	CHECK(ci_number_to_int(n) == -999);
	ci_dec(n);

	/* boxed double */
	n = ci_number_new(CI_NUM_F64);
	n->f64 = 3.14;
	CHECK(ci_number_to_double(n) == 3.14);
	CHECK(ci_number_to_int(n) == 3);
	ci_dec(n);
}

/* ================================================================
 * Arithmetic: add
 * ================================================================ */

static void test_add(void) {
	SECTION("add");

	ci_number *r;

	/* packed + packed → i128 */
	r = ci_number_add(CI_PACKINT(10), CI_PACKINT(20));
	CHECK(r != NULL);
	CHECK(ci_number_to_int(r) == 30);
	CHECK(!CI_NUMBER_IS_DOUBLE(r));
	ci_dec(r);

	/* packed + packed negative */
	r = ci_number_add(CI_PACKINT(-5), CI_PACKINT(3));
	CHECK(ci_number_to_int(r) == -2);
	ci_dec(r);

	/* boxed i128 + packed */
	ci_number *a = ci_number_new(CI_NUM_I128);
	a->i128 = 1000;
	r = ci_number_add(a, CI_PACKINT(234));
	CHECK(ci_number_to_int(r) == 1234);
	ci_dec(r);
	ci_dec(a);

	/* double + int → double */
	ci_number *d = ci_number_new(CI_NUM_F64);
	d->f64 = 1.5;
	r = ci_number_add(d, CI_PACKINT(2));
	CHECK(CI_NUMBER_IS_DOUBLE(r));
	CHECK(r->f64 == 3.5);
	ci_dec(r);
	ci_dec(d);

	/* double + double */
	ci_number *d1 = ci_number_new(CI_NUM_F64);
	ci_number *d2 = ci_number_new(CI_NUM_F64);
	d1->f64 = 1.1;
	d2->f64 = 2.2;
	r = ci_number_add(d1, d2);
	CHECK(CI_NUMBER_IS_DOUBLE(r));
	CHECK(fabs(r->f64 - 3.3) < 1e-10);
	ci_dec(r);
	ci_dec(d1);
	ci_dec(d2);
}

/* ================================================================
 * Arithmetic: sub
 * ================================================================ */

static void test_sub(void) {
	SECTION("sub");

	ci_number *r;

	r = ci_number_sub(CI_PACKINT(50), CI_PACKINT(30));
	CHECK(ci_number_to_int(r) == 20);
	ci_dec(r);

	r = ci_number_sub(CI_PACKINT(10), CI_PACKINT(30));
	CHECK(ci_number_to_int(r) == -20);
	ci_dec(r);

	ci_number *d = ci_number_new(CI_NUM_F64);
	d->f64 = 10.0;
	r = ci_number_sub(d, CI_PACKINT(3));
	CHECK(CI_NUMBER_IS_DOUBLE(r));
	CHECK(r->f64 == 7.0);
	ci_dec(r);
	ci_dec(d);
}

/* ================================================================
 * Arithmetic: mul
 * ================================================================ */

static void test_mul(void) {
	SECTION("mul");

	ci_number *r;

	r = ci_number_mul(CI_PACKINT(6), CI_PACKINT(7));
	CHECK(ci_number_to_int(r) == 42);
	ci_dec(r);

	r = ci_number_mul(CI_PACKINT(-3), CI_PACKINT(4));
	CHECK(ci_number_to_int(r) == -12);
	ci_dec(r);

	ci_number *d = ci_number_new(CI_NUM_F64);
	d->f64 = 2.5;
	r = ci_number_mul(d, CI_PACKINT(4));
	CHECK(CI_NUMBER_IS_DOUBLE(r));
	CHECK(r->f64 == 10.0);
	ci_dec(r);
	ci_dec(d);
}

/* ================================================================
 * Arithmetic: div
 * ================================================================ */

static void test_div(void) {
	SECTION("div");

	ci_number *r;

	/* integer division */
	r = ci_number_div(CI_PACKINT(20), CI_PACKINT(3));
	CHECK(ci_number_to_int(r) == 6);  /* truncates */
	ci_dec(r);

	/* div by zero → infinity */
	r = ci_number_div(CI_PACKINT(5), CI_PACKINT(0));
	CHECK(CI_NUMBER_IS_DOUBLE(r));
	CHECK(isinf(r->f64));
	CHECK(r->f64 > 0);
	ci_dec(r);

	/* negative div by zero */
	r = ci_number_div(CI_PACKINT(-5), CI_PACKINT(0));
	CHECK(CI_NUMBER_IS_DOUBLE(r));
	CHECK(isinf(r->f64));
	CHECK(r->f64 < 0);
	ci_dec(r);

	/* double division */
	ci_number *d = ci_number_new(CI_NUM_F64);
	d->f64 = 7.0;
	r = ci_number_div(d, CI_PACKINT(2));
	CHECK(CI_NUMBER_IS_DOUBLE(r));
	CHECK(r->f64 == 3.5);
	ci_dec(r);

	/* double div by zero → infinity */
	ci_number *z = ci_number_new(CI_NUM_F64);
	z->f64 = 0.0;
	r = ci_number_div(d, z);
	CHECK(CI_NUMBER_IS_DOUBLE(r));
	CHECK(isinf(r->f64));
	CHECK(r->f64 > 0);
	ci_dec(r);

	/* negative double div by zero → -infinity */
	ci_number *nd = ci_number_new(CI_NUM_F64);
	nd->f64 = -3.0;
	r = ci_number_div(nd, z);
	CHECK(CI_NUMBER_IS_DOUBLE(r));
	CHECK(isinf(r->f64));
	CHECK(r->f64 < 0);
	ci_dec(r);

	/* 0.0 / 0.0 → NaN */
	ci_number *z2 = ci_number_new(CI_NUM_F64);
	z2->f64 = 0.0;
	r = ci_number_div(z2, z);
	CHECK(CI_NUMBER_IS_DOUBLE(r));
	CHECK(isnan(r->f64));
	ci_dec(r);

	ci_dec(d);
	ci_dec(z);
	ci_dec(nd);
	ci_dec(z2);
}

/* ================================================================
 * Arithmetic: mod
 * ================================================================ */

static void test_mod(void) {
	SECTION("mod");

	ci_number *r;

	r = ci_number_mod(CI_PACKINT(17), CI_PACKINT(5));
	CHECK(ci_number_to_int(r) == 2);
	ci_dec(r);

	/* mod by zero → NaN */
	r = ci_number_mod(CI_PACKINT(5), CI_PACKINT(0));
	CHECK(CI_NUMBER_IS_DOUBLE(r));
	CHECK(isnan(r->f64));
	ci_dec(r);

	/* double mod */
	ci_number *d = ci_number_new(CI_NUM_F64);
	d->f64 = 7.5;
	r = ci_number_mod(d, CI_PACKINT(2));
	CHECK(CI_NUMBER_IS_DOUBLE(r));
	CHECK(fabs(r->f64 - 1.5) < 1e-10);
	ci_dec(r);

	/* double mod by zero → NaN */
	ci_number *z = ci_number_new(CI_NUM_F64);
	z->f64 = 0.0;
	r = ci_number_mod(d, z);
	CHECK(CI_NUMBER_IS_DOUBLE(r));
	CHECK(isnan(r->f64));
	ci_dec(r);

	ci_dec(d);
	ci_dec(z);
}

/* ================================================================
 * Negate
 * ================================================================ */

static void test_neg(void) {
	SECTION("neg");

	ci_number *r;

	r = ci_number_neg(CI_PACKINT(42));
	CHECK(ci_number_to_int(r) == -42);
	ci_dec(r);

	r = ci_number_neg(CI_PACKINT(-10));
	CHECK(ci_number_to_int(r) == 10);
	ci_dec(r);

	ci_number *d = ci_number_new(CI_NUM_F64);
	d->f64 = 3.14;
	r = ci_number_neg(d);
	CHECK(CI_NUMBER_IS_DOUBLE(r));
	CHECK(r->f64 == -3.14);
	ci_dec(r);
	ci_dec(d);
}

/* ================================================================
 * Comparison
 * ================================================================ */

static void test_cmp(void) {
	SECTION("cmp");

	CHECK(ci_number_cmp(CI_PACKINT(10), CI_PACKINT(20)) == -1);
	CHECK(ci_number_cmp(CI_PACKINT(20), CI_PACKINT(10)) == 1);
	CHECK(ci_number_cmp(CI_PACKINT(5), CI_PACKINT(5)) == 0);

	ci_number *a = ci_number_new(CI_NUM_I128);
	a->i128 = 100;
	CHECK(ci_number_cmp(a, CI_PACKINT(50)) == 1);
	CHECK(ci_number_cmp(CI_PACKINT(50), a) == -1);
	CHECK(ci_number_cmp(a, CI_PACKINT(100)) == 0);
	ci_dec(a);

	/* double cmp */
	ci_number *d1 = ci_number_new(CI_NUM_F64);
	ci_number *d2 = ci_number_new(CI_NUM_F64);
	d1->f64 = 1.5;
	d2->f64 = 2.5;
	CHECK(ci_number_cmp(d1, d2) == -1);
	CHECK(ci_number_cmp(d2, d1) == 1);
	d2->f64 = 1.5;
	CHECK(ci_number_cmp(d1, d2) == 0);
	ci_dec(d1);
	ci_dec(d2);

	/* cross-type: int vs double */
	ci_number *di = ci_number_new(CI_NUM_F64);
	di->f64 = 42.0;
	CHECK(ci_number_cmp(CI_PACKINT(42), di) == 0);
	CHECK(ci_number_cmp(CI_PACKINT(41), di) == -1);
	ci_dec(di);
}

/* ================================================================
 * Large values (128-bit)
 * ================================================================ */

static void test_large_values(void) {
	SECTION("large 128-bit values");

	ci_number *a = ci_number_new(CI_NUM_I128);
	ci_number *b = ci_number_new(CI_NUM_I128);

	/* values beyond 64-bit range */
	a->i128 = (__int128)1 << 100;
	b->i128 = (__int128)1 << 100;

	ci_number *r = ci_number_add(a, b);
	CHECK(r->i128 == ((__int128)1 << 101));
	ci_dec(r);

	r = ci_number_sub(a, b);
	CHECK(r->i128 == 0);
	ci_dec(r);

	ci_dec(a);
	ci_dec(b);
}

/* ================================================================
 * Overflow promotion to double
 * ================================================================ */

static void test_overflow_promotion(void) {
	SECTION("overflow promotion to double");

	/* Build max positive i128: 2^127 - 1 */
	ci_number *max = ci_number_new(CI_NUM_I128);
	max->i128 = ((__int128)1 << 126) | (((__int128)1 << 126) - 1);

	/* Build min negative i128: -2^127 */
	ci_number *min = ci_number_new(CI_NUM_I128);
	min->i128 = (__int128)-1 << 127;

	ci_number *one = ci_number_new(CI_NUM_I128);
	one->i128 = 1;

	ci_number *r;

	/* add: max + 1 overflows → double */
	r = ci_number_add(max, one);
	CHECK(CI_NUMBER_IS_DOUBLE(r));
	CHECK(r->f64 > 0);
	ci_dec(r);

	/* add: min + (-1) overflows → double */
	ci_number *neg1 = ci_number_new(CI_NUM_I128);
	neg1->i128 = -1;
	r = ci_number_add(min, neg1);
	CHECK(CI_NUMBER_IS_DOUBLE(r));
	CHECK(r->f64 < 0);
	ci_dec(r);
	ci_dec(neg1);

	/* sub: min - 1 overflows → double */
	r = ci_number_sub(min, one);
	CHECK(CI_NUMBER_IS_DOUBLE(r));
	CHECK(r->f64 < 0);
	ci_dec(r);

	/* sub: max - (-1) = max + 1, overflows → double */
	ci_number *neg1b = ci_number_new(CI_NUM_I128);
	neg1b->i128 = -1;
	r = ci_number_sub(max, neg1b);
	CHECK(CI_NUMBER_IS_DOUBLE(r));
	CHECK(r->f64 > 0);
	ci_dec(r);
	ci_dec(neg1b);

	/* mul: max * 2 overflows → double */
	ci_number *two = ci_number_new(CI_NUM_I128);
	two->i128 = 2;
	r = ci_number_mul(max, two);
	CHECK(CI_NUMBER_IS_DOUBLE(r));
	CHECK(r->f64 > 0);
	ci_dec(r);
	ci_dec(two);

	/* mul: min * 2 overflows → double */
	ci_number *two2 = ci_number_new(CI_NUM_I128);
	two2->i128 = 2;
	r = ci_number_mul(min, two2);
	CHECK(CI_NUMBER_IS_DOUBLE(r));
	CHECK(r->f64 < 0);
	ci_dec(r);
	ci_dec(two2);

	/* pow: 2^127 overflows → double */
	r = ci_number_pow(CI_PACKINT(2), CI_PACKINT(127));
	CHECK(CI_NUMBER_IS_DOUBLE(r));
	CHECK(r->f64 > 0);
	ci_dec(r);

	/* pow: 2^126 does NOT overflow → stays int */
	r = ci_number_pow(CI_PACKINT(2), CI_PACKINT(126));
	CHECK(!CI_NUMBER_IS_DOUBLE(r));
	CHECK(r->i128 == ((__int128)1 << 126));
	ci_dec(r);

	/* non-overflow ops still return int */
	r = ci_number_add(CI_PACKINT(10), CI_PACKINT(20));
	CHECK(!CI_NUMBER_IS_DOUBLE(r));
	CHECK(ci_number_to_int(r) == 30);
	ci_dec(r);

	r = ci_number_mul(CI_PACKINT(100), CI_PACKINT(100));
	CHECK(!CI_NUMBER_IS_DOUBLE(r));
	CHECK(ci_number_to_int(r) == 10000);
	ci_dec(r);

	ci_dec(max);
	ci_dec(min);
	ci_dec(one);
}

/* ================================================================
 * Print (visual check)
 * ================================================================ */

static void test_print(void) {
	SECTION("print (visual)");

	printf("    packed 42: "); ci_number_print(CI_PACKINT(42)); printf("\n");
	printf("    packed -7: "); ci_number_print(CI_PACKINT(-7)); printf("\n");

	ci_number *n;

	n = ci_number_new(CI_NUM_I128);
	n->i128 = -12345;
	printf("    i128 -12345: "); ci_number_print(n); printf("\n");
	ci_dec(n);

	n = ci_number_new(CI_NUM_F64);
	n->f64 = 3.14159;
	printf("    f64 3.14159: "); ci_number_print(n); printf("\n");
	ci_dec(n);

	n = ci_number_new(CI_NUM_I128);
	n->i128 = (__int128)1 << 100;
	printf("    i128 2^100: "); ci_number_print(n); printf("\n");
	ci_dec(n);
}

/* ================================================================
 * Bitwise: and, or, xor, not
 * ================================================================ */

static void test_bitwise(void) {
	SECTION("bitwise and/or/xor/not");

	ci_number *r;

	/* and */
	r = ci_number_and(CI_PACKINT(0xFF), CI_PACKINT(0x0F));
	CHECK(ci_number_to_int(r) == 0x0F);
	ci_dec(r);

	/* or */
	r = ci_number_or(CI_PACKINT(0xF0), CI_PACKINT(0x0F));
	CHECK(ci_number_to_int(r) == 0xFF);
	ci_dec(r);

	/* xor */
	r = ci_number_xor(CI_PACKINT(0xFF), CI_PACKINT(0x0F));
	CHECK(ci_number_to_int(r) == 0xF0);
	ci_dec(r);

	/* not */
	r = ci_number_not(CI_PACKINT(0));
	CHECK(ci_number_to_int(r) == -1);  /* ~0 = all ones = -1 signed */
	ci_dec(r);

	r = ci_number_not(CI_PACKINT(-1));
	CHECK(ci_number_to_int(r) == 0);
	ci_dec(r);

	/* boxed i128 */
	ci_number *a = ci_number_new(CI_NUM_I128);
	ci_number *b = ci_number_new(CI_NUM_I128);
	a->i128 = (__int128)0xFF << 64;
	b->i128 = (__int128)0x0F << 64;
	r = ci_number_and(a, b);
	CHECK(r->i128 == ((__int128)0x0F << 64));
	ci_dec(r);
	ci_dec(a);
	ci_dec(b);

	/* double converted to int for bitops */
	ci_number *d = ci_number_new(CI_NUM_F64);
	d->f64 = 255.9;
	r = ci_number_and(d, CI_PACKINT(0x0F));
	CHECK(ci_number_to_int(r) == 0x0F);  /* 255 & 0x0F */
	ci_dec(r);
	ci_dec(d);
}

/* ================================================================
 * Shift
 * ================================================================ */

static void test_shift(void) {
	SECTION("shift");

	ci_number *r;

	/* left shift */
	r = ci_number_shift(CI_PACKINT(1), 10);
	CHECK(ci_number_to_int(r) == 1024);
	ci_dec(r);

	/* right shift (arithmetic) */
	r = ci_number_shift(CI_PACKINT(1024), -3);
	CHECK(ci_number_to_int(r) == 128);
	ci_dec(r);

	/* right shift sign-extends */
	r = ci_number_shift(CI_PACKINT(-1), -1);
	CHECK(ci_number_to_int(r) == -1);  /* sign-extending */
	ci_dec(r);

	r = ci_number_shift(CI_PACKINT(-128), -3);
	CHECK(ci_number_to_int(r) == -16);
	ci_dec(r);

	/* lshift/rshift wrappers */
	r = ci_number_lshift(CI_PACKINT(1), 8);
	CHECK(ci_number_to_int(r) == 256);
	ci_dec(r);

	r = ci_number_rshift(CI_PACKINT(256), 8);
	CHECK(ci_number_to_int(r) == 1);
	ci_dec(r);

	/* shift by 0 = nop */
	r = ci_number_shift(CI_PACKINT(42), 0);
	CHECK(ci_number_to_int(r) == 42);
	ci_dec(r);

	/* large shift left → 0 */
	r = ci_number_shift(CI_PACKINT(1), 128);
	CHECK(ci_number_to_int(r) == 0);
	ci_dec(r);

	r = ci_number_shift(CI_PACKINT(1), 200);
	CHECK(ci_number_to_int(r) == 0);
	ci_dec(r);

	/* large shift right → sign fill */
	r = ci_number_shift(CI_PACKINT(-1), -128);
	CHECK(ci_number_to_int(r) == -1);
	ci_dec(r);

	r = ci_number_shift(CI_PACKINT(100), -128);
	CHECK(ci_number_to_int(r) == 0);
	ci_dec(r);

	/* 128-bit shift */
	ci_number *big = ci_number_new(CI_NUM_I128);
	big->i128 = 1;
	r = ci_number_shift(big, 100);
	CHECK(r->i128 == ((__int128)1 << 100));
	ci_dec(r);

	r = ci_number_shift(big, 127);
	/* 1 << 127 overflows into sign bit, that's fine — defined behavior */
	CHECK(r != NULL);
	ci_dec(r);
	ci_dec(big);

	/* double gets converted to int for shift */
	ci_number *d = ci_number_new(CI_NUM_F64);
	d->f64 = 8.7;
	r = ci_number_shift(d, 2);
	CHECK(ci_number_to_int(r) == 32);  /* (int)8.7 = 8, 8 << 2 = 32 */
	ci_dec(r);
	ci_dec(d);
}

/* ================================================================
 * tostring
 * ================================================================ */

#define CHECK_STR(p, max_ints, fdmax, expected) do { \
	uint8_t _buf[64]; \
	int _n = ci_number_tostring((p), _buf, (max_ints), (fdmax)); \
	_buf[_n] = 0; \
	if (_n != (int)strlen(expected) || memcmp(_buf, expected, _n) != 0) { \
		fprintf(stderr, "  FAIL %s:%d: got \"%s\" (%d) want \"%s\"\n", \
			__FILE__, __LINE__, _buf, _n, expected); \
		g_fail++; \
	} else { \
		g_pass++; \
	} \
} while(0)

static void test_tostring(void) {
	SECTION("tostring");

	/* tagged int (63-bit) */
	CHECK_STR(CI_PACKINT(0), 10, -6, "0");
	CHECK_STR(CI_PACKINT(1), 10, -6, "1");
	CHECK_STR(CI_PACKINT(42), 10, -6, "42");
	CHECK_STR(CI_PACKINT(123456789), 10, -6, "123456789");
	CHECK_STR(CI_PACKINT(-1), 10, -6, "-1");
	CHECK_STR(CI_PACKINT(-99), 10, -6, "-99");

	/* uint64 max range via boxed i128 that fits 64-bit */
	ci_number *n64 = ci_number_new(CI_NUM_I128);
	n64->i128 = 18446744073709551615ULL;
	CHECK_STR(n64, 10, -6, "18446744073709551615");
	ci_dec(n64);

	/* boxed i128 large */
	ci_number *big = ci_number_new(CI_NUM_I128);
	big->i128 = (__int128)1000000000000000000LL * 100;  /* 10^20 */
	CHECK_STR(big, 10, -6, "100000000000000000000");
	ci_dec(big);

	/* negative i128 */
	ci_number *neg = ci_number_new(CI_NUM_I128);
	neg->i128 = -(__int128)1000000000000000000LL * 100;
	CHECK_STR(neg, 10, -6, "-100000000000000000000");
	ci_dec(neg);

	/* 2^127-1 */
	ci_number *max128 = ci_number_new(CI_NUM_I128);
	max128->i128 = ((__int128)1 << 126) | (((__int128)1 << 126) - 1);
	CHECK_STR(max128, 10, -6, "170141183460469231731687303715884105727");
	ci_dec(max128);

	/* double — fd0 (default, scientific for leading zeros) */
	ci_number *d;

	d = ci_number_new(CI_NUM_F64);
	d->f64 = 3.14;
	CHECK_STR(d, 10, 0, "3.14");
	ci_dec(d);

	d = ci_number_new(CI_NUM_F64);
	d->f64 = 100.0;
	CHECK_STR(d, 10, 0, "100");
	ci_dec(d);

	d = ci_number_new(CI_NUM_F64);
	d->f64 = 0.5;
	CHECK_STR(d, 10, 0, "0.5");
	ci_dec(d);

	d = ci_number_new(CI_NUM_F64);
	d->f64 = 0.001;
	CHECK_STR(d, 10, 0, "1e-3");
	ci_dec(d);

	/* double — fd2 (%.2f) */
	d = ci_number_new(CI_NUM_F64);
	d->f64 = 3.14159;
	CHECK_STR(d, 10, 2, "3.14");
	ci_dec(d);

	d = ci_number_new(CI_NUM_F64);
	d->f64 = 1.005;
	CHECK_STR(d, 10, 2, "1.00");
	ci_dec(d);

	/* double — fd-6 (scientific threshold) */
	d = ci_number_new(CI_NUM_F64);
	d->f64 = 12345.6789;
	CHECK_STR(d, 10, -6, "12345.6789");
	ci_dec(d);

	d = ci_number_new(CI_NUM_F64);
	d->f64 = -42.5;
	CHECK_STR(d, 10, -6, "-42.5");
	ci_dec(d);

	/* double — large int, scientific */
	d = ci_number_new(CI_NUM_F64);
	d->f64 = 1e15;
	CHECK_STR(d, 10, -6, "1e15");
	ci_dec(d);
}

/* ================================================================
 * fromstring
 * ================================================================ */

#define CHECK_PARSE(str, expect_null) do { \
	ci_ptr _r = ci_number_fromstring((const uint8_t *)(str), strlen(str)); \
	if (expect_null) { \
		if (_r != NULL) { \
			fprintf(stderr, "  FAIL %s:%d: \"%s\" expected NULL, got non-NULL\n", \
				__FILE__, __LINE__, str); \
			ci_dec(_r); \
			g_fail++; \
		} else { g_pass++; } \
	} else { \
		if (_r == NULL) { \
			fprintf(stderr, "  FAIL %s:%d: \"%s\" expected value, got NULL\n", \
				__FILE__, __LINE__, str); \
			g_fail++; \
		} else { g_pass++; ci_dec(_r); } \
	} \
} while(0)

#define CHECK_PARSE_INT(str, expected) do { \
	ci_ptr _r = ci_number_fromstring((const uint8_t *)(str), strlen(str)); \
	if (_r == NULL) { \
		fprintf(stderr, "  FAIL %s:%d: \"%s\" returned NULL\n", \
			__FILE__, __LINE__, str); \
		g_fail++; \
	} else { \
		__int128 _v = ci_number_to_int(_r); \
		if (_v != (__int128)(expected)) { \
			fprintf(stderr, "  FAIL %s:%d: \"%s\" got wrong int value\n", \
				__FILE__, __LINE__, str); \
			g_fail++; \
		} else { g_pass++; } \
		ci_dec(_r); \
	} \
} while(0)

#define CHECK_PARSE_DOUBLE(str, expected) do { \
	ci_ptr _r = ci_number_fromstring((const uint8_t *)(str), strlen(str)); \
	if (_r == NULL) { \
		fprintf(stderr, "  FAIL %s:%d: \"%s\" returned NULL\n", \
			__FILE__, __LINE__, str); \
		g_fail++; \
	} else { \
		double _v = ci_number_to_double(_r); \
		if (fabs(_v - (expected)) > 1e-10 * fabs(expected) && fabs(_v - (expected)) > 1e-15) { \
			fprintf(stderr, "  FAIL %s:%d: \"%s\" got %g want %g\n", \
				__FILE__, __LINE__, str, _v, (double)(expected)); \
			g_fail++; \
		} else { g_pass++; } \
		ci_dec(_r); \
	} \
} while(0)

static void test_fromstring(void) {
	SECTION("fromstring — trailing junk → NULL");

	CHECK_PARSE("1234junk", 1);
	CHECK_PARSE("1234.245junk", 1);
	CHECK_PARSE("1.5e3junk", 1);
	CHECK_PARSE("1e10abc", 1);
	CHECK_PARSE("42 abc", 1);
	CHECK_PARSE("123#", 1);
	CHECK_PARSE("99.9.", 1);
	CHECK_PARSE("1e2e3", 1);

	SECTION("fromstring — valid integers");

	CHECK_PARSE_INT("0", 0);
	CHECK_PARSE_INT("1", 1);
	CHECK_PARSE_INT("-1", -1);
	CHECK_PARSE_INT("123456", 123456);
	CHECK_PARSE_INT("-999", -999);
	CHECK_PARSE_INT("  42  ", 42);
	CHECK_PARSE_INT("+7", 7);

	SECTION("fromstring — valid floats");

	CHECK_PARSE_DOUBLE("3.14", 3.14);
	CHECK_PARSE_DOUBLE("-0.5", -0.5);
	CHECK_PARSE_DOUBLE("1.0", 1.0);
	CHECK_PARSE(".5", 1);  /* no leading digit = invalid */

	SECTION("fromstring — scientific");

	CHECK_PARSE_DOUBLE("1e3", 1000.0);
	CHECK_PARSE_DOUBLE("1.5e2", 150.0);
	CHECK_PARSE_DOUBLE("2.5E-1", 0.25);
	CHECK_PARSE_DOUBLE("1e+3", 1000.0);
	CHECK_PARSE_DOUBLE("-1.5e2", -150.0);

	SECTION("fromstring — saturation");

	{
		/* 40 nines — way beyond i128, should saturate to max/min */
		ci_ptr rp = ci_number_fromstring((const uint8_t *)"9999999999999999999999999999999999999999", 40);
		CHECK(rp != NULL);
		__int128 i128_max = ((unsigned __int128)1 << 127) - 1;
		CHECK(ci_number_to_int(rp) == i128_max);
		ci_dec(rp);

		ci_ptr rn = ci_number_fromstring((const uint8_t *)"-9999999999999999999999999999999999999999", 41);
		CHECK(rn != NULL);
		__int128 i128_min = ~i128_max;
		CHECK(ci_number_to_int(rn) == i128_min);
		ci_dec(rn);
	}

	SECTION("fromstring — weird tails");

	/* tail consumes trailing numeric junk after a valid parse */
	CHECK_PARSE("1.5e3333333333333333eeeeeeeeeeee", 1);  /* multiple e's = junk */
	CHECK_PARSE("123.456.789", 1);       /* second dot after non-numeric */
	/* float path → expected_none (strict: only trailing ws) */
	CHECK_PARSE("1e2.3", 1);             /* float parsed 1e2, .3 is junk */
	CHECK_PARSE("1.0e10.5", 1);          /* float parsed 1.0e10, .5 is junk */

	/* int path → consume_tail (lenient: eats \d*.\d*e[+-]?\d*) */
	CHECK_PARSE("42e", 0);               /* int 42, tail eats bare e */
	CHECK_PARSE("42e+", 0);              /* int 42, tail eats e+ */
	CHECK_PARSE("42e-", 0);              /* int 42, tail eats e- */
	CHECK_PARSE("42.", 0);               /* int 42, tail eats . */
	CHECK_PARSE("42.e5", 0);             /* int 42, tail eats .e5 */
	CHECK_PARSE("100 200", 0);           /* int 100, tail eats ws+digits */
	CHECK_PARSE("123\t456", 0);          /* int 123, tail eats tab+digits */

	/* junk after tail */
	CHECK_PARSE("100abc", 1);            /* alpha after digits */
	CHECK_PARSE("999999999999999999999999999999999999999999999xyz", 1);  /* huge + alpha */
	CHECK_PARSE("1.5e3333333333333333eeeeeeeeeeee", 1);  /* multiple e's = junk */
	CHECK_PARSE("123.456.789", 1);       /* second dot outside tail pattern */

	/* valid edge cases */
	CHECK_PARSE("999999999999999999999999999999999999999999999", 0);  /* huge saturates */
	CHECK_PARSE("1e999999999999", 0);    /* absurd exponent */
	CHECK_PARSE("0000000", 0);           /* leading zeros */
	CHECK_PARSE("-0", 0);                /* negative zero */

	SECTION("fromstring — empty/garbage → NULL");

	CHECK_PARSE("", 1);
	CHECK_PARSE("   ", 1);
	CHECK_PARSE("abc", 1);
	CHECK_PARSE("-", 1);
	CHECK_PARSE("+", 1);
	CHECK_PARSE(".", 1);
}

/* ================================================================
 * main
 * ================================================================ */

int main(void) {
	ci_init();
	ci_str_register();
	ci_arr_register();
	ci_map_register();
	ci_number_register();

	printf("ci_number tests:\n");

	test_alloc();
	test_convert();
	test_to_accessors();
	test_add();
	test_sub();
	test_mul();
	test_div();
	test_mod();
	test_neg();
	test_cmp();
	test_large_values();
	test_overflow_promotion();
	test_bitwise();
	test_shift();
	test_print();
	test_tostring();
	test_fromstring();

	printf("\nci_number: %d passed, %d failed\n", g_pass, g_fail);

	ci_shutdown();
	return g_fail ? 1 : 0;
}
