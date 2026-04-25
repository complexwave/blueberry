#include "../cma.h"
#include <stdio.h>
#include <assert.h>

#define TEST(name) printf("  %-40s ", #name); fflush(stdout); name(); printf("OK\n");

// ================================================================
// C-style patterns
// ================================================================

void test_identifier() {
	// [a-zA-Z_][a-zA-Z0-9_]*
	cma_op *head = R("a-zA-Z_");
	cma_op *tail = REP(0, CMA_INF, R("a-zA-Z0-9_"));
	cma_op *ident = A(head, tail);

	assert(cma_run(ident, "foo_bar123 ", 11) == 10);
	assert(cma_run(ident, "_x", 2) == 2);
	assert(cma_run(ident, "_123", 4) == 4);
	assert(cma_run(ident, "123", 3) == -1);
}

void test_integer() {
	// [+-]? [0-9]+
	cma_op *sign = MAX(1, R("+-"));
	cma_op *digits = MIN(1, R("0-9"));
	cma_op *integer = A(sign, digits);

	assert(cma_run(integer, "42", 2) == 2);
	assert(cma_run(integer, "-7x", 3) == 2);
	assert(cma_run(integer, "+123", 4) == 4);
	assert(cma_run(integer, "abc", 3) == -1);
}

void test_hex_digit() {
	// [0-9a-fA-F]
	cma_op *hex = R("0-9a-fA-F");
	assert(cma_run(hex, "a", 1) == 1);
	assert(cma_run(hex, "F", 1) == 1);
	assert(cma_run(hex, "5", 1) == 1);
	assert(cma_run(hex, "g", 1) == -1);
}

void test_hex_literal() {
	// 0x[0-9a-fA-F]+
	cma_op *hex_lit = A(S("0x"), MIN(1, R("0-9a-fA-F")));
	assert(cma_run(hex_lit, "0xdead", 6) == 6);
	assert(cma_run(hex_lit, "0xBEEF", 6) == 6);
	assert(cma_run(hex_lit, "0x", 2) == -1);
}

void test_float_simple() {
	// [0-9]+ . [0-9]+
	cma_op *int_part = MIN(1, R("0-9"));
	cma_op *frac_part = MIN(1, R("0-9"));
	cma_op *float_lit = A(int_part, S("."), frac_part);

	assert(cma_run(float_lit, "3.14", 4) == 4);
	assert(cma_run(float_lit, "42.0", 4) == 4);
	assert(cma_run(float_lit, "3.", 2) == -1);  // no fractional part
}

void test_string_literal() {
	// "\"" [^"]* "\""
	cma_op *quote = S("\"");
	cma_op *not_quote = NOT(S("\""));
	cma_op *content = REP(0, CMA_INF, A(not_quote, R("\x01-\xff")));  // any non-quote
	cma_op *string = A(quote, content, quote);

	assert(cma_run(string, "\"hello\"", 7) == 7);
	assert(cma_run(string, "\"\"", 2) == 2);
}

void test_whitespace() {
	// [ \t\n\r]+
	cma_op *ws = MIN(1, R(" \t\n\r"));
	assert(cma_run(ws, "  ", 2) == 2);
	assert(cma_run(ws, "\t\n", 2) == 2);
	assert(cma_run(ws, "a", 1) == -1);
}

void test_url_scheme() {
	// [a-z]+ :
	cma_op *scheme = A(MIN(1, R("a-z")), S(":"));
	assert(cma_run(scheme, "http:", 5) == 5);
	assert(cma_run(scheme, "ftp:", 4) == 4);
	assert(cma_run(scheme, "123:", 4) == -1);
}

void test_domain() {
	// [a-z0-9]+ ( . [a-z0-9]+ )*
	cma_op *label = MIN(1, R("a-z0-9"));
	cma_op *dot_label = A(S("."), label);
	cma_op *domain = A(label, REP(0, CMA_INF, dot_label));

	assert(cma_run(domain, "example.com", 11) == 11);
	assert(cma_run(domain, "a.b.c.d.e", 9) == 9);
	assert(cma_run(domain, "example", 7) == 7);
}

void test_email_simple() {
	// [a-z0-9]+ @ [a-z0-9]+ . [a-z]+
	cma_op *user = MIN(1, R("a-z0-9_"));
	cma_op *at = S("@");
	cma_op *domain = MIN(1, R("a-z0-9"));
	cma_op *tld = MIN(1, R("a-z"));
	cma_op *email = A(user, at, domain, S("."), tld);

	assert(cma_run(email, "user@example.com", 16) == 16);
	assert(cma_run(email, "a@b.c", 5) == 5);
}

// ================================================================
// Programming language patterns
// ================================================================

void test_keyword_vs_identifier() {
	// if | while | for | identifier
	cma_op *p = O(S("if"), S("while"), S("for"), MIN(1, R("a-zA-Z_")));
	assert(cma_run(p, "if", 2) == 2);
	assert(cma_run(p, "while", 5) == 5);
	assert(cma_run(p, "myvar", 5) == 5);
}

void test_operator_precedence() {
	// ++ | += | +
	cma_op *p = O(S("++"), S("+="), S("+"));
	assert(cma_run(p, "++x", 3) == 2);
	assert(cma_run(p, "+= 5", 4) == 2);
	assert(cma_run(p, "+ y", 3) == 1);
}

void test_string_escape() {
	// \"  (   \\\\ | [^\\"] )*  \"
	// Note: Using explicit printable chars instead of full range
	cma_op *quote = S("\"");
	cma_op *escaped = S("\\\\");
	cma_op *not_quote = NOT(S("\""));
	cma_op *char_in_string = O(escaped, A(not_quote, R("a-zA-Z0-9 ")));
	cma_op *string = A(quote, REP(0, CMA_INF, char_in_string), quote);

	assert(cma_run(string, "\"hello\"", 7) == 7);
	assert(cma_run(string, "\"hello world\"", 13) == 13);
}

void test_comment_line() {
	// // [a-zA-Z0-9 ]* \n
	cma_op *simple_comment = A(S("//"), REP(0, CMA_INF, R("a-zA-Z0-9 ")), S("\n"));

	assert(cma_run(simple_comment, "// comment\n", 11) == 11);
}

void test_block_comment_start() {
	// /* [^*]* \* /
	// Simplified: just match the start
	cma_op *p = S("/*");
	assert(cma_run(p, "/* comment */", 13) == 2);
}

// ================================================================
// Data format patterns
// ================================================================

void test_csv_field() {
	// digits ( , digits )*  where digits = [0-9]+
	cma_op *digits = MIN(1, R("0-9"));
	cma_op *comma_digits = A(S(","), digits);
	cma_op *field = A(digits, REP(0, CMA_INF, comma_digits));
	assert(cma_run(field, "1,2,3", 5) == 5);
	assert(cma_run(field, "42", 2) == 2);
	assert(cma_run(field, "123,456,789", 11) == 11);
}

void test_phone_simple() {
	// [0-9]{3} - [0-9]{3} - [0-9]{4}
	cma_op *area = REP(3, 3, R("0-9"));
	cma_op *exchange = REP(3, 3, R("0-9"));
	cma_op *line = REP(4, 4, R("0-9"));
	cma_op *phone = A(area, S("-"), exchange, S("-"), line);

	assert(cma_run(phone, "555-123-4567", 12) == 12);
	assert(cma_run(phone, "555-123-456", 11) == -1);
}

void test_date_simple() {
	// [0-9]{4} - [0-9]{2} - [0-9]{2}
	cma_op *year = REP(4, 4, R("0-9"));
	cma_op *month = REP(2, 2, R("0-9"));
	cma_op *day = REP(2, 2, R("0-9"));
	cma_op *date = A(year, S("-"), month, S("-"), day);

	assert(cma_run(date, "2024-03-13", 10) == 10);
	assert(cma_run(date, "2024-3-13", 9) == -1);
}

void test_json_number() {
	// -? [0-9]+ ( . [0-9]+ )? ( [eE] [+-]? [0-9]+ )?
	cma_op *sign = MAX(1, S("-"));
	cma_op *digits = MIN(1, R("0-9"));
	cma_op *frac = A(S("."), digits);
	cma_op *exp = A(R("eE"), MAX(1, R("+-")), digits);
	cma_op *json_num = A(sign, digits, MAX(1, frac), MAX(1, exp));

	assert(cma_run(json_num, "123", 3) == 3);
	assert(cma_run(json_num, "-456", 4) == 4);
	assert(cma_run(json_num, "3.14", 4) == 4);
	assert(cma_run(json_num, "1e10", 4) == 4);
}

void test_ipv4_octet() {
	// [0-9]{1,3}
	cma_op *octet = REP(1, 3, R("0-9"));
	assert(cma_run(octet, "255", 3) == 3);
	assert(cma_run(octet, "0", 1) == 1);
	assert(cma_run(octet, "192", 3) == 3);
}

void test_base64_char() {
	// [A-Za-z0-9+/=]
	cma_op *b64 = R("A-Za-z0-9+/=");
	assert(cma_run(b64, "A", 1) == 1);
	assert(cma_run(b64, "+", 1) == 1);
	assert(cma_run(b64, "=", 1) == 1);
	assert(cma_run(b64, "-", 1) == -1);
}

// ================================================================
// Main
// ================================================================

int main() {
	printf("CMA practical pattern tests:\n\n");

	printf("C-style patterns:\n");
	TEST(test_identifier);
	TEST(test_integer);
	TEST(test_hex_digit);
	TEST(test_hex_literal);
	TEST(test_float_simple);
	TEST(test_string_literal);
	TEST(test_whitespace);

	printf("\nURL and domain patterns:\n");
	TEST(test_url_scheme);
	TEST(test_domain);
	TEST(test_email_simple);

	printf("\nProgramming language patterns:\n");
	TEST(test_keyword_vs_identifier);
	TEST(test_operator_precedence);
	TEST(test_string_escape);
	TEST(test_comment_line);
	TEST(test_block_comment_start);

	printf("\nData format patterns:\n");
	TEST(test_csv_field);
	TEST(test_phone_simple);
	TEST(test_date_simple);
	TEST(test_json_number);
	TEST(test_ipv4_octet);
	TEST(test_base64_char);

	printf("\n✓ All pattern tests passed!\n");
	return 0;
}
