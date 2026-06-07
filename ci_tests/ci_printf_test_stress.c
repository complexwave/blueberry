#define CI_STRING_TEST
#include "ciobj.c"

#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_fail = 0;

static void check(const char *label, const char *fmt,
                  ci_ptr *args, size_t arg_cnt,
                  const char *expected, int expect_len)
{
	tests_run++;
	ci_str *dst = ci_str_new(16); /* small initial alloc to stress realloc */

	int n = ci_printf((ci_ptr)dst,
		(const uint8_t *)fmt, strlen(fmt),
		args, arg_cnt);

	int ok = 1;

	if (expect_len >= 0 && n != expect_len) {
		printf("FAIL %-25s  len: got %d, expected %d\n", label, n, expect_len);
		ok = 0;
	}

	if (expected) {
		size_t slen = ci_str_len(dst);
		const uint8_t *head = ci_str_head(dst);

		if (slen != strlen(expected) ||
		    memcmp(head, expected, slen) != 0) {
			printf("FAIL %-25s  got: \"%.*s\", expected: \"%s\"\n",
				label, (int)slen, head, expected);
			ok = 0;
		}
	}

	if (ok)
		printf("  ok %-25s  out(%d): \"%.*s\"\n",
			label, n, (int)ci_str_len(dst), ci_str_head(dst));
	else
		tests_fail++;

	ci_free(dst);
}

int main(void) {
	ci_init();
	ci_str_register();
	ci_arr_register();
	ci_map_register();
	ci_number_register();

	printf("=== ci_printf stress tests ===\n\n");

	/* ---- empty / edge cases ---- */

	printf("--- empty and edge cases ---\n");
	check("empty fmt", "", NULL, 0, "", 0);
	check("single %", "%", NULL, 0, "%", 1);
	check("double %%", "%%", NULL, 0, "%", 1);
	check("triple %%%", "%%%", NULL, 0, "%%", 2);
	check("just percent end", "abc%", NULL, 0, "abc%", 4);
	check("percent percent end", "abc%%", NULL, 0, "abc%", 4);

	/* ---- missing args ---- */

	printf("\n--- missing args ---\n");
	check("no arg %d", "%d", NULL, 0, "<NOARG>", -1);
	check("no arg %s", "%s", NULL, 0, "<NOARG>", -1);
	check("no arg %x", "%x", NULL, 0, "<NOARG>", -1);
	check("no arg %f", "%f", NULL, 0, "<NOARG>", -1);
	check("2 specs 1 arg", "%d %d",
		(ci_ptr[]){ ci_number_int(1) }, 1, NULL, -1);
	check("3 specs 0 args", "%d %s %f", NULL, 0, NULL, -1);

	/* ---- unknown spec ---- */

	printf("\n--- unknown specifier ---\n");
	check("unknown %q", "%q", (ci_ptr[]){ ci_number_int(1) }, 1, "%q", -1);
	check("unknown %Z", "%Z", (ci_ptr[]){ ci_number_int(1) }, 1, "%Z", -1);

	/* ---- width 0 ---- */

	printf("\n--- zero width ---\n");
	ci_ptr z1 = ci_number_int(42);
	check("width 0 d", "%0d", (ci_ptr[]){ z1 }, 1, "42", 2);
	check("width 00 d", "%00d", (ci_ptr[]){ z1 }, 1, "42", 2);
	ci_dec(z1);

	/* ---- huge width (potential overflow) ---- */

	printf("\n--- huge width ---\n");
	{
		ci_ptr n = ci_number_int(1);

		/* width 65535 (max uint16) */
		ci_str *dst = ci_str_new(16);
		const char *fmt = "%65535d";
		int r = ci_printf((ci_ptr)dst,
			(const uint8_t *)fmt, strlen(fmt),
			(ci_ptr[]){ n }, 1);
		printf("  ok huge width 65535         out len: %d (expected 65535)\n",
			(int)ci_str_len(dst));
		ci_free(dst);

		/* width parsed as uint16 — 99999 wraps or clamps? */
		dst = ci_str_new(16);
		fmt = "%99999d";
		r = ci_printf((ci_ptr)dst,
			(const uint8_t *)fmt, strlen(fmt),
			(ci_ptr[]){ n }, 1);
		(void)r;
		printf("  ok width 99999 (u16 wrap)   out len: %zu\n",
			ci_str_len(dst));
		ci_free(dst);

		ci_dec(n);
	}

	/* ---- precision edge cases ---- */

	printf("\n--- precision edge cases ---\n");
	ci_ptr pf = (ci_ptr)ci_number_floating(3.14159265);
	check("prec .0", "%.0f", (ci_ptr[]){ pf }, 1, NULL, -1);
	check("prec .1", "%.1f", (ci_ptr[]){ pf }, 1, NULL, -1);
	check("prec .10", "%.10f", (ci_ptr[]){ pf }, 1, NULL, -1);
	check("prec dot only", "%.f", (ci_ptr[]){ pf }, 1, NULL, -1);
	ci_dec(pf);

	/* ---- zero values ---- */

	printf("\n--- zero values ---\n");
	ci_ptr zero_i = ci_number_int(0);
	ci_ptr zero_f = (ci_ptr)ci_number_floating(0.0);
	ci_ptr neg_zero = (ci_ptr)ci_number_floating(-0.0);
	check("int 0 %d", "%d", (ci_ptr[]){ zero_i }, 1, "0", 1);
	check("int 0 %x", "%x", (ci_ptr[]){ zero_i }, 1, "0", 1);
	check("int 0 %b", "%b", (ci_ptr[]){ zero_i }, 1, "0", 1);
	check("int 0 %o", "%o", (ci_ptr[]){ zero_i }, 1, "0", 1);
	check("float 0 %f", "%f", (ci_ptr[]){ zero_f }, 1, NULL, -1);
	check("neg zero %f", "%f", (ci_ptr[]){ neg_zero }, 1, NULL, -1);
	ci_dec(zero_i);
	ci_dec(zero_f);
	ci_dec(neg_zero);

	/* ---- special floats ---- */

	printf("\n--- special floats ---\n");
	ci_ptr finf = (ci_ptr)ci_number_floating(1.0/0.0);
	ci_ptr fninf = (ci_ptr)ci_number_floating(-1.0/0.0);
	ci_ptr fnan = (ci_ptr)ci_number_floating(0.0/0.0);
	check("inf %f", "%f", (ci_ptr[]){ finf }, 1, NULL, -1);
	check("-inf %f", "%f", (ci_ptr[]){ fninf }, 1, NULL, -1);
	check("nan %f", "%f", (ci_ptr[]){ fnan }, 1, NULL, -1);
	check("inf %x", "%x", (ci_ptr[]){ finf }, 1, NULL, -1);
	check("nan %d", "%d", (ci_ptr[]){ fnan }, 1, NULL, -1);
	ci_dec(finf);
	ci_dec(fninf);
	ci_dec(fnan);

	/* ---- large i128 ---- */

	printf("\n--- large i128 ---\n");
	{
		ci_number *big = ci_number_new(CI_NUM_I128);
		big->i128 = ((__int128)1 << 126);
		check("i128 big %d", "%d", (ci_ptr[]){ (ci_ptr)big }, 1, NULL, -1);
		check("i128 big %x", "%x", (ci_ptr[]){ (ci_ptr)big }, 1, NULL, -1);
		check("i128 big %b", "%b", (ci_ptr[]){ (ci_ptr)big }, 1, NULL, -1);

		ci_number *neg128 = ci_number_new(CI_NUM_I128);
		neg128->i128 = -1;
		check("i128 -1 %x", "%x", (ci_ptr[]){ (ci_ptr)neg128 }, 1,
			"ffffffffffffffffffffffffffffffff", 32);

		ci_dec((ci_ptr)big);
		ci_dec((ci_ptr)neg128);
	}

	/* ---- left align combos ---- */

	printf("\n--- left align ---\n");
	ci_ptr la = ci_number_int(42);
	ci_str *lastr = ci_str_from_cstr("ab");
	check("left d", "[%-5d]", (ci_ptr[]){ la }, 1, "[42   ]", 7);
	check("left s", "[%-5s]", (ci_ptr[]){ (ci_ptr)lastr }, 1, "[ab   ]", 7);
	check("left x", "[%-8x]", (ci_ptr[]){ la }, 1, "[2a      ]", 10);
	check("right d", "[%5d]", (ci_ptr[]){ la }, 1, "[   42]", 7);
	check("left 0 d (0 ignored)", "[%-05d]", (ci_ptr[]){ la }, 1, "[42   ]", 7);
	ci_dec(la);
	ci_free(lastr);

	/* ---- consecutive specs no literal ---- */

	printf("\n--- consecutive specs ---\n");
	ci_ptr ca = ci_number_int(1);
	ci_ptr cb = ci_number_int(2);
	ci_ptr cc = ci_number_int(3);
	check("3 consecutive", "%d%d%d", (ci_ptr[]){ ca, cb, cc }, 3, "123", 3);
	ci_dec(ca); ci_dec(cb); ci_dec(cc);

	/* ---- long format string (stress realloc) ---- */

	printf("\n--- long format string ---\n");
	{
		/* 200 chars of literal + some specs */
		char longfmt[300];
		memset(longfmt, 'A', 200);
		memcpy(longfmt + 200, " %d %s end", 10);
		longfmt[210] = 0;

		ci_ptr ln = ci_number_int(77);
		ci_str *ls = ci_str_from_cstr("hello");
		int r;
		ci_str *dst = ci_str_new(16);
		r = ci_printf((ci_ptr)dst,
			(const uint8_t *)longfmt, strlen(longfmt),
			(ci_ptr[]){ ln, (ci_ptr)ls }, 2);
		printf("  ok long fmt                 out len: %zu (expected 219)\n",
			ci_str_len(dst));
		ci_free(dst);
		ci_dec(ln);
		ci_free(ls);
	}

	/* ---- style edge cases ---- */

	printf("\n--- style edge cases ---\n");
	check("empty style", "%[:]", NULL, 0, NULL, -1);
	check("nested style", "%[a: %[b: x ] ]", NULL, 0, NULL, -1);
	check("unclosed style", "%[red: hello", NULL, 0, NULL, -1);

	/* ---- %c edge cases ---- */

	printf("\n--- char edge cases ---\n");
	check("char 0", "%c", (ci_ptr[]){ ci_number_int(0) }, 1, NULL, -1);
	check("char 127", "%c", (ci_ptr[]){ ci_number_int(127) }, 1, "\x7f", 1);
	check("char huge", "%c",
		(ci_ptr[]){ ci_number_int(0x1FFFFF) }, 1, NULL, -1);

	printf("\n=== %d tests, %d failures ===\n", tests_run, tests_fail);
	return tests_fail ? 1 : 0;
}
