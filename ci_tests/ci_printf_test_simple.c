#define CI_STRING_TEST
#include "ciobj.c"

#include <stdio.h>

static void test(const char *label, const char *fmt,
                 ci_ptr *args, size_t arg_cnt)
{
	ci_str *dst = ci_str_new(256);

	int n = ci_printf((ci_ptr)dst,
		(const uint8_t *)fmt, strlen(fmt),
		args, arg_cnt);

	printf("%-20s  fmt: %-35s  out(%d): \"%.*s\"\n",
		label, fmt, n, (int)ci_str_len(dst), ci_str_head(dst));

	ci_free(dst);
}

int main(void) {
	ci_init();
	ci_str_register();
	ci_arr_register();
	ci_map_register();
	ci_number_register();

	/* 1: plain literal */
	test("literal", "hello world", NULL, 0);

	/* 2: escapes */
	test("escapes", "100%% done %] end", NULL, 0);

	/* 3: %s with a ci_str */
	ci_str *name = ci_str_from_cstr("User");
	ci_ptr args3[] = { (ci_ptr)name };
	test("string %s", "hello %s!", args3, 1);
	ci_free(name);

	/* 4: %d with a number */
	ci_ptr num = ci_number_int(42);
	ci_ptr args4[] = { num };
	test("number %d", "val=%d end", args4, 1);
	ci_dec(num);

	/* 5: mixed %s and %d */
	ci_str *thing = ci_str_from_cstr("apples");
	ci_ptr count = ci_number_int(1234);
	ci_ptr args5[] = { count, (ci_ptr)thing };
	test("mixed", "I have %d %s", args5, 2);
	ci_free(thing);
	ci_dec(count);

	/* 6: string with width */
	ci_str *hi = ci_str_from_cstr("hi");
	ci_ptr args6[] = { (ci_ptr)hi };
	test("str width", "[%10s]", args6, 1);
	ci_free(hi);

	/* 7: float with precision */
	ci_ptr pi = (ci_ptr)ci_number_floating(3.14159265);
	ci_ptr args7[] = { pi };
	test("float .2", "pi=%0.2f", args7, 1);
	ci_dec(pi);

	/* 8: float with width + precision + zero-pad */
	ci_ptr fval = (ci_ptr)ci_number_floating(3.14);
	ci_ptr args8[] = { fval };
	test("float 08.2", "x=%08.2f!", args8, 1);
	ci_dec(fval);

	/* 9: int zero-padded width */
	ci_ptr n9 = ci_number_int(42);
	ci_ptr args9[] = { n9 };
	test("int 08d", "id=%08d", args9, 1);
	ci_dec(n9);

	/* 10: negative int zero-padded */
	ci_ptr n10 = ci_number_int(-7);
	ci_ptr args10[] = { n10 };
	test("neg 06d", "v=%06d", args10, 1);
	ci_dec(n10);

	/* 11: no arg */
	test("noarg", "val=%d oops", NULL, 0);

	/* 12: block style */
	ci_ptr n12 = ci_number_int(99);
	ci_ptr args12[] = { n12 };
	test("block style", "%[green: score=%d ]", args12, 1);
	ci_dec(n12);

	/* 13: short style */
	ci_ptr n13 = ci_number_int(7);
	ci_ptr args13[] = { n13 };
	test("short style", "x=%:red:d!", args13, 1);
	ci_dec(n13);

	/* 14: %e scientific */
	ci_ptr n14e = (ci_ptr)ci_number_floating(12345.6789);
	ci_ptr args14e[] = { n14e };
	test("scientific %e", "%e", args14e, 1);
	ci_dec(n14e);

	/* 14b: %e small number */
	ci_ptr n14e2 = (ci_ptr)ci_number_floating(0.00042);
	ci_ptr args14e2[] = { n14e2 };
	test("scientific small", "%e", args14e2, 1);
	ci_dec(n14e2);

	/* 14c: %g auto (large) */
	ci_ptr n14g = (ci_ptr)ci_number_floating(99999999999.0);
	ci_ptr args14g[] = { n14g };
	test("auto %g large", "%g", args14g, 1);
	ci_dec(n14g);

	/* 14d: %g auto (normal) */
	ci_ptr n14g2 = (ci_ptr)ci_number_floating(42.5);
	ci_ptr args14g2[] = { n14g2 };
	test("auto %g normal", "%g", args14g2, 1);
	ci_dec(n14g2);

	/* 14e: %g auto (tiny) */
	ci_ptr n14g3 = (ci_ptr)ci_number_floating(0.0000001);
	ci_ptr args14g3[] = { n14g3 };
	test("auto %g tiny", "%g", args14g3, 1);
	ci_dec(n14g3);

	/* 14f: %d on float (truncates) */
	ci_ptr n14d = (ci_ptr)ci_number_floating(11.7);
	ci_ptr args14d[] = { n14d };
	test("float %d", "%d", args14d, 1);
	ci_dec(n14d);

	/* 14g: %d on negative float */
	ci_ptr n14d2 = (ci_ptr)ci_number_floating(-3.9);
	ci_ptr args14d2[] = { n14d2 };
	test("neg float %d", "%d", args14d2, 1);
	ci_dec(n14d2);

	/* 14h: %g int (should print clean) */
	ci_ptr n14gi = ci_number_int(1234);
	ci_ptr args14gi[] = { n14gi };
	test("int %g", "%g", args14gi, 1);
	ci_dec(n14gi);

	/* 15: %x lowercase */
	ci_ptr n14 = ci_number_int(0xDEAD);
	ci_ptr args14[] = { n14 };
	test("hex lower", "%x", args14, 1);
	ci_dec(n14);

	/* 15: %X uppercase */
	ci_ptr n15 = ci_number_int(0xBEEF);
	ci_ptr args15[] = { n15 };
	test("hex upper", "%X", args15, 1);
	ci_dec(n15);

	/* 16: %x negative (two's complement) */
	ci_ptr n16 = ci_number_int(-1);
	ci_ptr args16[] = { n16 };
	test("hex neg", "%x", args16, 1);
	ci_dec(n16);

	/* 17: %x float (raw IEEE bits) */
	ci_ptr n17 = (ci_ptr)ci_number_floating(3.14);
	ci_ptr args17[] = { n17 };
	test("hex float", "%X", args17, 1);
	ci_dec(n17);

	/* 18: %x zero-padded width */
	ci_ptr n18 = ci_number_int(0xFF);
	ci_ptr args18[] = { n18 };
	test("hex pad", "%08x", args18, 1);
	ci_dec(n18);

	/* 19: %b binary */
	ci_ptr n19 = ci_number_int(42);
	ci_ptr args19[] = { n19 };
	test("binary", "%b", args19, 1);
	ci_dec(n19);

	/* 20: %b float (raw IEEE bits) */
	ci_ptr n20 = (ci_ptr)ci_number_floating(1.0);
	ci_ptr args20[] = { n20 };
	test("bin float", "%b", args20, 1);
	ci_dec(n20);

	/* 21: %o octal */
	ci_ptr n21 = ci_number_int(511);
	ci_ptr args21[] = { n21 };
	test("octal", "%o", args21, 1);
	ci_dec(n21);

	/* 22: %p pointer */
	ci_str *pstr = ci_str_from_cstr("test");
	ci_ptr args22[] = { (ci_ptr)pstr };
	test("pointer %p", "%p", args22, 1);
	ci_free(pstr);

	/* 23: %c ASCII */
	ci_ptr c23 = ci_number_int(65);
	ci_ptr args23[] = { c23 };
	test("char A", "%c", args23, 1);
	ci_dec(c23);

	/* 24: %c emoji (U+1F600 😀) */
	ci_ptr c24 = ci_number_int(0x1F600);
	ci_ptr args24[] = { c24 };
	test("char emoji", "%c", args24, 1);
	ci_dec(c24);

	/* 25: %c cyrillic (U+0416 Ж) */
	ci_ptr c25 = ci_number_int(0x0416);
	ci_ptr args25[] = { c25 };
	test("char cyrillic", "%c", args25, 1);
	ci_dec(c25);

	/* 26: %-10s left-align string */
	ci_str *lstr = ci_str_from_cstr("hi");
	ci_ptr args26[] = { (ci_ptr)lstr };
	test("left str", "[%-10s]", args26, 1);
	ci_free(lstr);

	/* 27: %-8d left-align int */
	ci_ptr n27 = ci_number_int(42);
	ci_ptr args27[] = { n27 };
	test("left int", "[%-8d]", args27, 1);
	ci_dec(n27);

	/* 28: %-10x left-align hex */
	ci_ptr n28 = ci_number_int(0xFF);
	ci_ptr args28[] = { n28 };
	test("left hex", "[%-10x]", args28, 1);
	ci_dec(n28);

	return 0;
}
