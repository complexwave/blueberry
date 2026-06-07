/*
 * ci_printf_test.c — test suite for ci_printf format parser
 */
#include "ci_printf.c"
#include <stdio.h>

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

/* run ci_printf on a C string, return output in static buf */
static uint8_t g_buf[1024];

static int run(const char *fmt) {
	return ci_printf(g_buf, sizeof g_buf, (const uint8_t *)fmt, strlen(fmt));
}

static int buf_eq(const char *s) {
	return strcmp((const char *)g_buf, s) == 0;
}

/* check a parsed spec by index */
#define CHECK_SPEC(idx, _spec, _fl, _w, _p) do { \
	CHECK(ci_fmt_nargs > (idx)); \
	CHECK(ci_fmt_args[idx].spec == (_spec)); \
	CHECK(ci_fmt_args[idx].fl == (_fl)); \
	CHECK(ci_fmt_args[idx].w == (_w)); \
	CHECK(ci_fmt_args[idx].p == (_p)); \
} while(0)

#define CHECK_SPEC_STYLE(idx, s) do { \
	CHECK(ci_fmt_args[idx].style != NULL); \
	CHECK(ci_fmt_args[idx].style_len == strlen(s)); \
	CHECK(memcmp(ci_fmt_args[idx].style, s, strlen(s)) == 0); \
} while(0)

/* ================================================================
 * Literals
 * ================================================================ */

static void test_literal(void) {
	SECTION("literal passthrough");

	int n;

	n = run("hello world");
	CHECK(n == 11);
	CHECK(buf_eq("hello world"));

	n = run("");
	CHECK(n == 0);
	CHECK(buf_eq(""));

	n = run("abc");
	CHECK(n == 3);
	CHECK(buf_eq("abc"));
}

/* ================================================================
 * Escapes
 * ================================================================ */

static void test_escapes(void) {
	SECTION("escapes");

	run("100%%");
	CHECK(buf_eq("100%"));

	run("a%%b%%c");
	CHECK(buf_eq("a%b%c"));

	run("%]");
	CHECK(buf_eq("]"));

	run("a%]b");
	CHECK(buf_eq("a]b"));

	/* trailing % */
	run("x%");
	CHECK(buf_eq("x%"));
}

/* ================================================================
 * Format spec — specifier parsed correctly
 * ================================================================ */

static void test_specifiers(void) {
	SECTION("specifiers");

	/* basic: spec consumed, only literals remain */
	run("x%dy");
	CHECK(buf_eq("xy"));
	CHECK(ci_fmt_nargs == 1);
	CHECK(ci_fmt_args[0].spec == 'd');

	/* all specifiers */
	const char *specs = "dxXobfeEgGsc?";
	for (int i = 0; specs[i]; i++) {
		char fmt[4] = { '%', specs[i], 0 };
		run(fmt);
		CHECK(ci_fmt_nargs == 1);
		CHECK(ci_fmt_args[0].spec == (uint8_t)specs[i]);
	}

	/* multiple specs */
	run("%d%s%f");
	CHECK(ci_fmt_nargs == 3);
	CHECK(ci_fmt_args[0].spec == 'd');
	CHECK(ci_fmt_args[1].spec == 's');
	CHECK(ci_fmt_args[2].spec == 'f');
}

/* ================================================================
 * Flags
 * ================================================================ */

static void test_flags(void) {
	SECTION("flags");

	run("%-d");
	CHECK_SPEC(0, 'd', CI_FMT_LEFT_ADJ, -1, -1);

	run("%+d");
	CHECK_SPEC(0, 'd', CI_FMT_MARK_POS, -1, -1);

	run("%0d");
	CHECK_SPEC(0, 'd', CI_FMT_ZERO_PAD, -1, -1);

	run("% d");
	CHECK_SPEC(0, 'd', CI_FMT_PAD_POS, -1, -1);

	/* combined */
	run("%-+0d");
	CHECK_SPEC(0, 'd', CI_FMT_LEFT_ADJ | CI_FMT_MARK_POS | CI_FMT_ZERO_PAD, -1, -1);
}

/* ================================================================
 * Width & precision
 * ================================================================ */

static void test_width_precision(void) {
	SECTION("width & precision");

	run("%10d");
	CHECK_SPEC(0, 'd', 0, 10, -1);

	run("%.5f");
	CHECK_SPEC(0, 'f', 0, -1, 5);

	run("%10.5f");
	CHECK_SPEC(0, 'f', 0, 10, 5);

	run("%.0f");
	CHECK_SPEC(0, 'f', 0, -1, 0);

	/* flags + width + precision */
	run("%+08.2f");
	CHECK_SPEC(0, 'f', CI_FMT_MARK_POS | CI_FMT_ZERO_PAD, 8, 2);

	run("%-20s");
	CHECK_SPEC(0, 's', CI_FMT_LEFT_ADJ, 20, -1);

	/* large width */
	run("%999d");
	CHECK_SPEC(0, 'd', 0, 999, -1);
}

/* ================================================================
 * Short style: %:style:spec
 * ================================================================ */

static void test_short_style(void) {
	SECTION("short style");

	run("%:r:d");
	CHECK(ci_fmt_nargs == 1);
	CHECK_SPEC(0, 'd', 0, -1, -1);
	CHECK_SPEC_STYLE(0, "r");

	run("%:red:d");
	CHECK_SPEC(0, 'd', 0, -1, -1);
	CHECK_SPEC_STYLE(0, "red");

	run("%:bold:+10d");
	CHECK_SPEC(0, 'd', CI_FMT_MARK_POS, 10, -1);
	CHECK_SPEC_STYLE(0, "bold");

	/* short style between literals */
	run("x=%:red:d!");
	CHECK(buf_eq("x=!"));
	CHECK(ci_fmt_nargs == 1);
	CHECK_SPEC_STYLE(0, "red");

	/* plain spec has no style */
	run("%d");
	CHECK(ci_fmt_args[0].style == NULL);
	CHECK(ci_fmt_args[0].style_len == 0);
}

/* ================================================================
 * Full style: %[style: ... ]
 * ================================================================ */

static void test_full_style(void) {
	SECTION("full style");

	/* literal content passes through, style markers consumed */
	run("%[red: hello ]");
	CHECK(buf_eq(" hello "));

	/* format spec inside style block — two spaces survive (around consumed %d) */
	run("%[red,bold: %d ]");
	CHECK(buf_eq("  "));
	CHECK(ci_fmt_nargs == 1);
	CHECK(ci_fmt_args[0].spec == 'd');

	/* escaped ] inside style */
	run("%[b: x%]y ]");
	CHECK(buf_eq(" x]y "));

	/* empty style block */
	run("%[dim:]");
	CHECK(buf_eq(""));

	/* text after style block */
	run("%[r: hi ] done");
	CHECK(buf_eq(" hi  done"));
}

/* ================================================================
 * Mixed
 * ================================================================ */

static void test_mixed(void) {
	SECTION("mixed");

	run("name=%s age=%d%%");
	CHECK(buf_eq("name= age=%"));
	CHECK(ci_fmt_nargs == 2);
	CHECK(ci_fmt_args[0].spec == 's');
	CHECK(ci_fmt_args[1].spec == 'd');

	/* multiple specs with width/precision */
	run("[%10d|%-20s|%+.5f]");
	CHECK(buf_eq("[||]"));
	CHECK(ci_fmt_nargs == 3);
	CHECK_SPEC(0, 'd', 0, 10, -1);
	CHECK_SPEC(1, 's', CI_FMT_LEFT_ADJ, 20, -1);
	CHECK_SPEC(2, 'f', CI_FMT_MARK_POS, -1, 5);
}

/* ================================================================
 * ] outside style is plain text
 * ================================================================ */

static void test_bracket_outside(void) {
	SECTION("] outside style");

	run("a]b");
	CHECK(buf_eq("a]b"));

	run("]]]");
	CHECK(buf_eq("]]]"));
}

/* ================================================================
 * main
 * ================================================================ */

int main(void) {
	printf("ci_printf tests:\n");

	test_literal();
	test_escapes();
	test_specifiers();
	test_flags();
	test_width_precision();
	test_short_style();
	test_full_style();
	test_mixed();
	test_bracket_outside();

	printf("\nci_printf: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
