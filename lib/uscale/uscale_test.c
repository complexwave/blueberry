/*
 * uscale_test.c -- roundtrip tests and benchmarks for uscale.
 *
 * Build: make -C lib/uscale test
 * Run:   lib/uscale/uscale_test
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "uscale.c"

/* ============================================================
 * Test helpers
 * ============================================================ */

static int tests_run = 0;
static int tests_fail = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
	printf("  %-40s", #name); \
	int _f = tests_fail; \
	name(); \
	tests_run++; \
	printf("%s\n", tests_fail == _f ? "ok" : "FAIL"); \
} while(0)

#define ASSERT_EQ_F64(got, want) do { \
	double _g = (got), _w = (want); \
	uint64_t _gb, _wb; \
	memcpy(&_gb, &_g, 8); memcpy(&_wb, &_w, 8); \
	if (_gb != _wb) { \
		printf("\n    ASSERT_EQ_F64: got %.17g (0x%016llx) want %.17g (0x%016llx) ", \
			_g, (unsigned long long)_gb, _w, (unsigned long long)_wb); \
		tests_fail++; \
	} \
} while(0)

#define ASSERT_EQ_STR(got, want) do { \
	if (strcmp((got), (want)) != 0) { \
		printf("\n    ASSERT_EQ_STR: got \"%s\" want \"%s\" ", (got), (want)); \
		tests_fail++; \
	} \
} while(0)

#define ASSERT_EQ_U64(got, want) do { \
	if ((got) != (want)) { \
		printf("\n    ASSERT_EQ_U64: got %llu want %llu ", \
			(unsigned long long)(got), (unsigned long long)(want)); \
		tests_fail++; \
	} \
} while(0)

#define ASSERT_EQ_INT(got, want) do { \
	if ((got) != (want)) { \
		printf("\n    ASSERT_EQ_INT: got %d want %d ", (got), (want)); \
		tests_fail++; \
	} \
} while(0)

/* ============================================================
 * Roundtrip: dtoa_short -> atod must recover original bits
 * ============================================================ */

TEST(test_roundtrip_short) {
	double cases[] = {
		1.0, -0.0, 0.1, 0.2, 0.3,
		3.14159265358979323846,
		1e-10, 1e10, 1e-300, 1e300,
		2.2250738585072014e-308,  /* smallest normal */
		1.7976931348623157e+308,  /* largest finite */
		2.2204460492503131e-16,   /* machine epsilon */
		123456789.0,
		0.000123456789,
	};
	int n = sizeof cases / sizeof cases[0];
	for (int i = 0; i < n; i++) {
		double f = cases[i];
		if (f == 0.0) continue; /* skip signed zeros, atod can't represent */
		uscale_digits r = uscale_dtoa_short(f);
		double got = uscale_atod(r.d, r.p);
		ASSERT_EQ_F64(got, f);
	}
}

/* ============================================================
 * Roundtrip: dtoa_fixed -> atod
 * ============================================================ */

TEST(test_roundtrip_fixed) {
	double cases[] = {
		1.0, 3.14159265358979323846, 1e-10, 1e10, 123456789.0,
		2.718281828459045,
		0.000123456789,
	};
	int n = sizeof cases / sizeof cases[0];
	for (int i = 0; i < n; i++) {
		for (int digits = 1; digits <= 17; digits++) {
			uscale_digits r = uscale_dtoa_fixed(cases[i], digits);
			/* fixed doesn't necessarily roundtrip perfectly at low digit counts,
			 * but at 17 digits it must. */
			if (digits == 17) {
				double got = uscale_atod(r.d, r.p);
				ASSERT_EQ_F64(got, cases[i]);
			}
		}
	}
}

/* ============================================================
 * Format: dtoa_short -> format -> string sanity check
 * ============================================================ */

TEST(test_format_short) {
	char buf[32];
	uscale_digits r;

	r = uscale_dtoa_short(1.0);
	int nd = uscale_digits_count(r.d);
	uscale_format(buf, r.d, r.p, nd);
	ASSERT_EQ_STR(buf, "1e+00");

	r = uscale_dtoa_short(3.14159265358979323846);
	nd = uscale_digits_count(r.d);
	uscale_format(buf, r.d, r.p, nd);
	/* verify it parses back */
	double got = uscale_atod_text(buf, strlen(buf));
	ASSERT_EQ_F64(got, 3.14159265358979323846);
}

/* ============================================================
 * atod_text: valid inputs
 * ============================================================ */

TEST(test_atod_text_valid) {
	ASSERT_EQ_F64(uscale_atod_text("1", 1), 1.0);
	ASSERT_EQ_F64(uscale_atod_text("123", 3), 123.0);
	ASSERT_EQ_F64(uscale_atod_text("1.5", 3), 1.5);
	ASSERT_EQ_F64(uscale_atod_text("1.5e2", 5), 150.0);
	ASSERT_EQ_F64(uscale_atod_text("1.5E2", 5), 150.0);
	ASSERT_EQ_F64(uscale_atod_text("1.5e+2", 6), 150.0);
	ASSERT_EQ_F64(uscale_atod_text("1.5e-2", 6), 0.015);
	ASSERT_EQ_F64(uscale_atod_text("314159265358979324", 18), 314159265358979324.0);
}

/* ============================================================
 * atod_text: empty / no digits
 * ============================================================ */

TEST(test_atod_text_empty) {
	ASSERT_EQ_F64(uscale_atod_text("", 0), 0.0);
	ASSERT_EQ_F64(uscale_atod_text(".", 1), 0.0);
	ASSERT_EQ_F64(uscale_atod_text("e5", 2), 0.0);
}

/* ============================================================
 * atod_text: too many digits
 * ============================================================ */

TEST(test_atod_text_huge) {
	/* 20 digits -- exceeds maxDigits=19 */
	ASSERT_EQ_F64(uscale_atod_text("12345678901234567890", 20), 0.0);
	/* 19 integer + fractional overflow */
	ASSERT_EQ_F64(uscale_atod_text("1234567890.1234567890", 21), 0.0);
}

/* ============================================================
 * atod_text: trailing garbage
 * ============================================================ */

TEST(test_atod_text_trailing_garbage) {
	/* trailing junk after valid number */
	ASSERT_EQ_F64(uscale_atod_text("123abc", 6), 0.0);
	ASSERT_EQ_F64(uscale_atod_text("1.5e2xyz", 8), 0.0);
	ASSERT_EQ_F64(uscale_atod_text("1.5 ", 4), 0.0);
}

/* ============================================================
 * atod_text: leading garbage
 * ============================================================ */

TEST(test_atod_text_leading_garbage) {
	ASSERT_EQ_F64(uscale_atod_text("abc123", 6), 0.0);
	ASSERT_EQ_F64(uscale_atod_text(" 123", 4), 0.0);
	ASSERT_EQ_F64(uscale_atod_text("+1.5", 4), 0.0);
}

/* ============================================================
 * atod_text: bad exponents
 * ============================================================ */

TEST(test_atod_text_bad_exponent) {
	/* exponent with no digits after */
	ASSERT_EQ_F64(uscale_atod_text("1e", 2), 0.0);
	/* exponent too large (>3 digits) */
	ASSERT_EQ_F64(uscale_atod_text("1e1234", 6), 0.0);
}

/* ============================================================
 * digits_count
 * ============================================================ */

TEST(test_digits_count) {
	ASSERT_EQ_INT(uscale_digits_count(1), 1);
	ASSERT_EQ_INT(uscale_digits_count(9), 1);
	ASSERT_EQ_INT(uscale_digits_count(10), 2);
	ASSERT_EQ_INT(uscale_digits_count(99), 2);
	ASSERT_EQ_INT(uscale_digits_count(100), 3);
	ASSERT_EQ_INT(uscale_digits_count(999999999999999999ULL), 18);
	ASSERT_EQ_INT(uscale_digits_count(1000000000000000000ULL), 19);
}

/* ============================================================
 * Bench
 * ============================================================ */

static double bench_values[] = {
	3.14159265358979323846,
	1e-10,
	1e10,
	2.718281828459045,
	0.000123456789,
	1.7976931348623157e+308,
	2.2250738585072014e-308,
	123456789.0,
};
static int bench_nvalues = sizeof bench_values / sizeof bench_values[0];

static void bench_dtoa_short(int iters) {
	volatile uint64_t sink_d;
	volatile int sink_p;
	for (int i = 0; i < iters; i++) {
		double f = bench_values[i % bench_nvalues];
		uscale_digits r = uscale_dtoa_short(f);
		sink_d = r.d;
		sink_p = r.p;
	}
	(void)sink_d; (void)sink_p;
}

static void bench_atod(int iters) {
	/* pre-compute digits for each value */
	uscale_digits pre[8];
	for (int i = 0; i < bench_nvalues; i++)
		pre[i] = uscale_dtoa_short(bench_values[i]);

	volatile double sink;
	for (int i = 0; i < iters; i++) {
		uscale_digits *r = &pre[i % bench_nvalues];
		sink = uscale_atod(r->d, r->p);
	}
	(void)sink;
}

static void bench_roundtrip(int iters) {
	volatile double sink;
	for (int i = 0; i < iters; i++) {
		double f = bench_values[i % bench_nvalues];
		uscale_digits r = uscale_dtoa_short(f);
		sink = uscale_atod(r.d, r.p);
	}
	(void)sink;
}

static void bench_format(int iters) {
	char buf[32];
	uscale_digits pre[8];
	int nds[8];
	for (int i = 0; i < bench_nvalues; i++) {
		pre[i] = uscale_dtoa_short(bench_values[i]);
		nds[i] = uscale_digits_count(pre[i].d);
	}
	volatile char sink;
	for (int i = 0; i < iters; i++) {
		int j = i % bench_nvalues;
		uscale_format(buf, pre[j].d, pre[j].p, nds[j]);
		sink = buf[0];
	}
	(void)sink;
}

static void bench_format_base10(int iters) {
	char buf[32];
	uscale_digits pre[8];
	int nds[8];
	for (int i = 0; i < bench_nvalues; i++) {
		pre[i] = uscale_dtoa_short(bench_values[i]);
		nds[i] = uscale_digits_count(pre[i].d);
	}
	volatile char sink;
	for (int i = 0; i < iters; i++) {
		int j = i % bench_nvalues;
		uscale_formatBase10(buf, pre[j].d, nds[j]);
		sink = buf[0];
	}
	(void)sink;
}

static void bench_pretty(int iters) {
	char buf[64];
	volatile char sink;
	for (int i = 0; i < iters; i++) {
		volatile double f = bench_values[i % bench_nvalues];
		uscale_dtoa_pretty(buf, f, 15, 0);
		sink = buf[0];
	}
	(void)sink;
}

static void bench_printdecnumber(int iters) {
	char buf[32];
	uscale_digits pre[8];
	for (int i = 0; i < bench_nvalues; i++)
		pre[i] = uscale_dtoa_short(bench_values[i]);
	volatile char sink;
	for (int i = 0; i < iters; i++) {
		int j = i % bench_nvalues;
		int n = uscale_printdecnumber(buf, pre[j].d);
		sink = buf[0];
		(void)n;
	}
	(void)sink;
}

static void bench_libc_sprintf(int iters) {
	char buf[32];
	volatile int sink;
	for (int i = 0; i < iters; i++) {
		double f = bench_values[i % bench_nvalues];
		sink = snprintf(buf, sizeof buf, "%.17g", f);
	}
	(void)sink;
}

static void bench_libc_strtod(int iters) {
	/* pre-format strings */
	char strs[8][32];
	for (int i = 0; i < bench_nvalues; i++)
		snprintf(strs[i], sizeof strs[i], "%.17g", bench_values[i]);

	volatile double sink;
	for (int i = 0; i < iters; i++) {
		sink = strtod(strs[i % bench_nvalues], NULL);
	}
	(void)sink;
}

static void bench_libc_roundtrip(int iters) {
	char buf[32];
	volatile double sink;
	for (int i = 0; i < iters; i++) {
		double f = bench_values[i % bench_nvalues];
		snprintf(buf, sizeof buf, "%.17g", f);
		sink = strtod(buf, NULL);
	}
	(void)sink;
}

static void run_bench(const char *name, void (*fn)(int), int iters) {
	struct timespec t0, t1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	fn(iters);
	clock_gettime(CLOCK_MONOTONIC, &t1);
	double ns = (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
	printf("  %-24s %10d iters  %6.1f ns/op\n", name, iters, ns / iters);
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
	printf("tests:\n");
	RUN(test_roundtrip_short);
	RUN(test_roundtrip_fixed);
	RUN(test_format_short);
	RUN(test_atod_text_valid);
	RUN(test_atod_text_empty);
	RUN(test_atod_text_huge);
	RUN(test_atod_text_trailing_garbage);
	RUN(test_atod_text_leading_garbage);
	RUN(test_atod_text_bad_exponent);
	RUN(test_digits_count);

	printf("\n%d tests, %d failures\n", tests_run, tests_fail);

	if (tests_fail) return 1;

	int N = 10000000;
	printf("\nbench (%d iters):\n", N);
	run_bench("dtoa_short", bench_dtoa_short, N);
	run_bench("atod", bench_atod, N);
	run_bench("roundtrip", bench_roundtrip, N);
	run_bench("formatBase10", bench_format_base10, N);
	run_bench("printdecnumber", bench_printdecnumber, N);
	run_bench("format (e-notation)", bench_format, N);
	run_bench("pretty (fd0)", bench_pretty, N);
	run_bench("libc sprintf", bench_libc_sprintf, N);
	run_bench("libc strtod", bench_libc_strtod, N);
	run_bench("libc roundtrip", bench_libc_roundtrip, N);

	return 0;
}
