/*
 * uscale_print.c -- compare libc vs uscale formatting for reference.
 *
 * Build: gcc -O2 -std=gnu11 -o uscale_print uscale_print.c -lm
 * Run:   ./uscale_print
 */

#include <stdio.h>
#include <math.h>

#include "uscale.c"

static void show(double f) {
	char ubuf[32] = {0};
	uint64_t rd = 0;
	int rp = 0, rnd = 0;

	/* uscale shortest */
	if (f != 0.0 && !isinf(f) && !isnan(f)) {
		uscale_digits r = uscale_dtoa_short(f);
		rd = r.d; rp = r.p;
		rnd = uscale_digits_count(r.d);
		uscale_format(ubuf, r.d, r.p, rnd);
	} else {
		snprintf(ubuf, sizeof ubuf, "(special)");
	}

	char p5[64] = {0}, p0[64] = {0}, p2[64] = {0};
	if (f != 0.0 && !isinf(f) && !isnan(f)) {
		uscale_dtoa_pretty(p5, f, 15, -5);
		uscale_dtoa_pretty(p0, f, 15, 0);
		uscale_dtoa_pretty(p2, f, 15, 2);
	} else {
		snprintf(p5, sizeof p5, "(special)");
		snprintf(p0, sizeof p0, "(special)");
		snprintf(p2, sizeof p2, "(special)");
	}

	printf("  %-24.17g  fd-5=%-26s  fd0=%-26s  fd2=%-14s  d=%-20llu p=%-4d nd=%d\n",
		f, p5, p0, p2, (unsigned long long)rd, rp, rnd);
}

int main(void) {
	double cases[] = {
		/* integers */
		0.0,
		1.0,
		-1.0,
		2.0,
		10.0,
		100.0,
		1000.0,
		123456789.0,

		/* mixed int.frac */
		12345.12345,
		99999.2,
		25.5435435345,
		1.0001,
		9999.9999,
		100.001,
		7.0,
		-42.5,
		99999999999.999999999,
		1234.999,
		11111111111111.1,
		9999999999.00001,
		1234567890.12345,
		0.99999999999999,
		10000000000000.0,
		99999999999999.9,

		/* simple fractions */
		0.1,
		0.5,
		0.05,
		0.005,
		1.5,
		0.123456789,
		3.14159265358979323846,

		/* small */
		1e-1,
		1e-5,
		1e-10,
		1e-15,
		1e-100,
		1e-300,
		5e-324,  /* smallest subnormal */
		2.2250738585072014e-308, /* smallest normal */

		/* large */
		1e5,
		1e10,
		1e15,
		1e100,
		1e300,
		1.7976931348623157e+308, /* largest finite */

		/* misc */
		2.718281828459045,
		0.000123456789,
		999999999999999.0,
		1000000000000000.0,
		0.30000000000000004, /* classic 0.1+0.2 */

		/* specials */
		INFINITY,
		-INFINITY,
		NAN,
	};

	int n = sizeof cases / sizeof cases[0];
	printf("%-26s  %-28s  %-28s  %-16s  %-22s  %s\n",
		"value", "fd-5", "fd0", "fd2", "d", "p");
	printf("%-26s  %-28s  %-28s  %-16s  %-22s  %s\n",
		"--------------------------",
		"----------------------------",
		"----------------------------",
		"----------------",
		"----------------------",
		"----------");
	for (int i = 0; i < n; i++)
		show(cases[i]);

	return 0;
}
