#define CI_STRING_TEST
#include "ciobj.c"

#include <stdio.h>
#include <time.h>

#define ITERS 1000000

static double elapsed(struct timespec *a, struct timespec *b) {
	return (b->tv_sec - a->tv_sec) + (b->tv_nsec - a->tv_nsec) * 1e-9;
}

#define BENCH(label, body) do { \
	struct timespec t0, t1; \
	clock_gettime(CLOCK_MONOTONIC, &t0); \
	for (int _i = 0; _i < ITERS; _i++) { body; } \
	clock_gettime(CLOCK_MONOTONIC, &t1); \
	double ms = elapsed(&t0, &t1) * 1000.0; \
	printf("%-30s  %8.2f ms  (%d iters, %.1f ns/op)\n", \
		label, ms, ITERS, ms * 1e6 / ITERS); \
} while(0)

int main(void) {
	ci_init();
	ci_str_register();
	ci_arr_register();
	ci_map_register();
	ci_number_register();

	/* ---- setup ---- */

	/* 50-byte string arg */
	ci_str *s50 = ci_str_from_cstr("The quick brown fox jumps over the lazy dog nearby");
	/* shorter strings */
	ci_str *s_name = ci_str_from_cstr("blueberry");
	ci_str *s_path = ci_str_from_cstr("/usr/local/bin");

	/* numbers */
	ci_ptr i1 = ci_number_int(42);
	ci_ptr i2 = ci_number_int(1234567);
	ci_ptr i3 = ci_number_int(-999);
	ci_ptr f1 = (ci_ptr)ci_number_floating(3.14159265);
	ci_ptr f2 = (ci_ptr)ci_number_floating(-0.00123);
	ci_ptr f3 = (ci_ptr)ci_number_floating(99999.5);

	/* reusable output */
	ci_str *dst = ci_str_new(512);
	char sbuf[512];

	printf("ci_printf vs sprintf benchmark (%d iterations)\n", ITERS);
	printf("================================================================\n\n");

	/* ============================================================
	 * 1: strings only — 3 strings, width padding
	 * ============================================================ */

	const char *fmt1 = "name=%s path=%10s desc=%s end";
	ci_ptr args1[] = { (ci_ptr)s_name, (ci_ptr)s_path, (ci_ptr)s50 };

	printf("--- strings only (3 args, ~80 byte output) ---\n");

	BENCH("ci_printf strings", {
		ci_str_clear(dst);
		ci_printf((ci_ptr)dst, (const uint8_t *)fmt1, strlen(fmt1), args1, 3);
	});

	BENCH("sprintf strings", {
		sprintf(sbuf, "name=%s path=%10s desc=%s end",
			"blueberry", "/usr/local/bin",
			"The quick brown fox jumps over the lazy dog nearby");
	});

	printf("\n");

	/* ============================================================
	 * 2: mixed string + int — 3 strings, 3 ints, with formats
	 * ============================================================ */

	const char *fmt2 = "[%s] count=%d path=%10s err=%06d total=%d label=%s";
	ci_ptr args2[] = { (ci_ptr)s_name, i1, (ci_ptr)s_path, i3, i2, (ci_ptr)s50 };

	printf("--- mixed strings+ints (6 args, ~100 byte output) ---\n");

	BENCH("ci_printf mixed s+i", {
		ci_str_clear(dst);
		ci_printf((ci_ptr)dst, (const uint8_t *)fmt2, strlen(fmt2), args2, 6);
	});

	BENCH("sprintf mixed s+i", {
		sprintf(sbuf, "[%s] count=%ld path=%10s err=%06ld total=%ld label=%s",
			"blueberry", 42L, "/usr/local/bin", -999L, 1234567L,
			"The quick brown fox jumps over the lazy dog nearby");
	});

	printf("\n");

	/* ============================================================
	 * 3: mixed string + int + float — full mix
	 * ============================================================ */

	const char *fmt3 = "%s: val=%08.2f n=%d price=%f err=%e total=%06d";
	ci_ptr args3[] = { (ci_ptr)s_name, f1, i1, f3, f2, i2 };

	printf("--- mixed s+i+f (6 args, floats with precision) ---\n");

	BENCH("ci_printf mixed all", {
		ci_str_clear(dst);
		ci_printf((ci_ptr)dst, (const uint8_t *)fmt3, strlen(fmt3), args3, 6);
	});

	BENCH("sprintf mixed all", {
		sprintf(sbuf, "%s: val=%08.2f n=%ld price=%g err=%e total=%06ld",
			"blueberry", 3.14159265, 42L, 99999.5, -0.00123, 1234567L);
	});

	printf("\n");

	/* ============================================================
	 * 4: float formatting only — 4 floats, various formats
	 * ============================================================ */

	const char *fmt4 = "a=%f b=%.4f c=%e d=%g";
	ci_ptr args4[] = { f1, f3, f2, f1 };

	printf("--- floats only (4 args, mixed formats) ---\n");

	BENCH("ci_printf floats", {
		ci_str_clear(dst);
		ci_printf((ci_ptr)dst, (const uint8_t *)fmt4, strlen(fmt4), args4, 4);
	});

	BENCH("sprintf floats", {
		sprintf(sbuf, "a=%g b=%.4f c=%e d=%g",
			3.14159265, 99999.5, -0.00123, 3.14159265);
	});

	printf("\n");

	/* ============================================================
	 * 5: heavy float — single float, high precision
	 * ============================================================ */

	const char *fmt5 = "%f";
	ci_ptr args5[] = { f1 };

	printf("--- single float (no width, raw speed) ---\n");

	BENCH("ci_printf 1 float", {
		ci_str_clear(dst);
		ci_printf((ci_ptr)dst, (const uint8_t *)fmt5, strlen(fmt5), args5, 1);
	});

	BENCH("sprintf 1 float", {
		sprintf(sbuf, "%g", 3.14159265);
	});

	printf("\n");

	/* ---- cleanup ---- */
	ci_free(s50);
	ci_free(s_name);
	ci_free(s_path);
	ci_dec(i1); ci_dec(i2); ci_dec(i3);
	ci_dec(f1); ci_dec(f2); ci_dec(f3);
	ci_free(dst);

	return 0;
}
