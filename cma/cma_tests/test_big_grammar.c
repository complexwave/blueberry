#include "../cma.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

#define TEST(name) printf("  %-40s ", #name); fflush(stdout); name(); printf("OK\n");

// ================================================================
// Grammar 1: C-style function signature
// ================================================================

void test_c_function_signature() {
	// type name ( params )
	// where type = [a-z_][a-z0-9_]*
	//       name = [a-z_][a-z0-9_]*
	//       params = anything in parens (simplified)

	cma_op *identifier = A(R("a-z_"), REP(0, CMA_INF, R("a-z0-9_")));
	cma_op *type = identifier;
	cma_op *name = identifier;
	cma_op *lparen = S("(");
	cma_op *rparen = S(")");
	cma_op *sig = A(type, S(" "), name, lparen, rparen);

	assert(cma_run(sig, "int main()", 10) == 10);
	assert(cma_run(sig, "void foo()", 10) == 10);
	assert(cma_run(sig, "unsigned_int bar()", 18) == 18);
}

void test_c_function_signature_with_space() {
	cma_op *identifier = A(R("a-z_"), REP(0, CMA_INF, R("a-z0-9_")));
	cma_op *type = identifier;
	cma_op *name = identifier;
	cma_op *lparen = S("(");
	cma_op *rparen = S(")");
	cma_op *sig = A(type, S(" "), name, lparen, rparen);

	assert(cma_run(sig, "char *  ptr( ", 12) == -1);  // extra spaces, rparen missing
}

// ================================================================
// Grammar 2: Simple math expression (no operator precedence)
// ================================================================

void test_simple_math_expr() {
	// expr = digit (op digit)*
	// op = + | - | * | /

	cma_op *digit = R("0-9");
	cma_op *op = R("+-*/");
	cma_op *op_digit = A(op, digit);
	cma_op *expr = A(digit, REP(0, CMA_INF, op_digit));

	assert(cma_run(expr, "5", 1) == 1);
	assert(cma_run(expr, "3+4", 3) == 3);
	assert(cma_run(expr, "7*2+1", 5) == 5);
}

void test_simple_math_expr_invalid() {
	cma_op *digit = R("0-9");
	cma_op *op = R("+-*/");
	cma_op *op_digit = A(op, digit);
	cma_op *expr = A(digit, REP(0, CMA_INF, op_digit));

	assert(cma_run(expr, "5++", 3) == 1);  // matches "5", then op_digit fails on "++", REP ends
	assert(cma_run(expr, "+5", 2) == -1);  // doesn't start with digit
}

// ================================================================
// Grammar 3: CSV line (simple version)
// ================================================================

void test_csv_line() {
	// field (,field)*
	// field = [a-z0-9]*

	cma_op *field = REP(0, CMA_INF, R("a-z0-9"));
	cma_op *comma_field = A(S(","), field);
	cma_op *line = A(field, REP(0, CMA_INF, comma_field));

	assert(cma_run(line, "alice,bob,charlie", 17) == 17);
	assert(cma_run(line, "1,2,3", 5) == 5);
	assert(cma_run(line, "field1,,field3", 14) == 14);
	assert(cma_run(line, "", 0) == 0);
}

void test_csv_line_with_spaces() {
	cma_op *field = REP(0, CMA_INF, R("a-z0-9"));
	cma_op *comma_field = A(S(","), field);
	cma_op *line = A(field, REP(0, CMA_INF, comma_field));

	// "alice, bob" matches: "alice" + "," + "" (space breaks the field)
	// Total: 6 chars ("alice,")
	assert(cma_run(line, "alice, bob", 10) == 6);
}

// ================================================================
// Grammar 4: IPv4-like (simplified)
// ================================================================

void test_ipv4_like() {
	// octet . octet . octet . octet
	// octet = [0-9]{1,3}

	cma_op *octet = REP(1, 3, R("0-9"));
	cma_op *dot = S(".");
	cma_op *ip = A(octet, dot, octet, dot, octet, dot, octet);

	assert(cma_run(ip, "192.168.1.1", 11) == 11);
	assert(cma_run(ip, "0.0.0.0", 7) == 7);
	assert(cma_run(ip, "255.255.255.255", 15) == 15);
}

void test_ipv4_like_incomplete() {
	cma_op *octet = REP(1, 3, R("0-9"));
	cma_op *dot = S(".");
	cma_op *ip = A(octet, dot, octet, dot, octet, dot, octet);

	assert(cma_run(ip, "192.168.1", 9) == -1);
	assert(cma_run(ip, "192.168.1.1.1", 13) == 11);  // matches first 4 octets
}

// ================================================================
// Grammar 5: Email-like
// ================================================================

void test_email_like() {
	// local @ domain . tld
	// local = [a-z0-9_]{1,}
	// domain = [a-z0-9]{1,}
	// tld = [a-z]{2,}

	cma_op *local = MIN(1, R("a-z0-9_"));
	cma_op *domain = MIN(1, R("a-z0-9"));
	cma_op *tld = REP(2, CMA_INF, R("a-z"));
	cma_op *email = A(local, S("@"), domain, S("."), tld);

	assert(cma_run(email, "user@example.com", 16) == 16);
	assert(cma_run(email, "a@b.co", 6) == 6);
	assert(cma_run(email, "test_123@domain.org", 19) == 19);
}

void test_email_like_invalid() {
	cma_op *local = MIN(1, R("a-z0-9_"));
	cma_op *domain = MIN(1, R("a-z0-9"));
	cma_op *tld = REP(2, CMA_INF, R("a-z"));
	cma_op *email = A(local, S("@"), domain, S("."), tld);

	assert(cma_run(email, "@example.com", 12) == -1);  // no local
	assert(cma_run(email, "user@.com", 9) == -1);  // no domain
	assert(cma_run(email, "user@domain.c", 13) == -1);  // tld too short
}

// ================================================================
// Grammar 6: Nested parentheses/brackets with alternation
// ================================================================

void test_nested_brackets() {
	// expr = "[" list "]" | digit
	// list = expr ("," expr)*

	cma_op *expr = NULL;
	cma_op *digit = R("0-9");

	cma_op *comma_expr = A(S(","), REF(&expr));
	cma_op *list = A(REF(&expr), REP(0, CMA_INF, comma_expr));
	cma_op *bracketed = A(S("["), list, S("]"));

	expr = O(bracketed, digit);

	assert(cma_run(expr, "5", 1) == 1);
	assert(cma_run(expr, "[5]", 3) == 3);
	assert(cma_run(expr, "[1,2,3]", 7) == 7);
	assert(cma_run(expr, "[[1,2],3]", 9) == 9);
}

void test_nested_brackets_complex() {
	cma_op *expr = NULL;
	cma_op *digit = R("0-9");

	cma_op *comma_expr = A(S(","), REF(&expr));
	cma_op *list = A(REF(&expr), REP(0, CMA_INF, comma_expr));
	cma_op *bracketed = A(S("["), list, S("]"));

	expr = O(bracketed, digit);

	assert(cma_run(expr, "[[[9]]]", 7) == 7);
	assert(cma_run(expr, "[1,[2,3],4]", 11) == 11);
}

void test_nested_brackets_invalid() {
	cma_op *expr = NULL;
	cma_op *digit = R("0-9");

	cma_op *comma_expr = A(S(","), REF(&expr));
	cma_op *list = A(REF(&expr), REP(0, CMA_INF, comma_expr));
	cma_op *bracketed = A(S("["), list, S("]"));

	expr = O(bracketed, digit);

	assert(cma_run(expr, "[1,2", 4) == -1);  // missing close bracket
	assert(cma_run(expr, "1,2]", 4) == 1);   // matches digit "1", rest unconsumed
}

// ================================================================
// Grammar 7: URL-like (simplified)
// ================================================================

void test_url_like() {
	// scheme : // host / path
	// scheme = [a-z]+
	// host = [a-z0-9.-]+
	// path = [a-z0-9/_-]*

	cma_op *scheme = MIN(1, R("a-z"));
	cma_op *host = MIN(1, R("a-z0-9.-"));
	cma_op *path = REP(0, CMA_INF, R("a-z0-9/_-"));
	cma_op *url = A(scheme, S("://"), host, S("/"), path);

	assert(cma_run(url, "http://example.com/path", 23) == 23);
	assert(cma_run(url, "ftp://host.org/file-1", 21) == 21);
}

void test_url_like_minimal() {
	cma_op *scheme = MIN(1, R("a-z"));
	cma_op *host = MIN(1, R("a-z0-9.-"));
	cma_op *path = REP(0, CMA_INF, R("a-z0-9/_-"));
	cma_op *url = A(scheme, S("://"), host, S("/"), path);

	assert(cma_run(url, "https://x.y/", 12) == 12);
}

// ================================================================
// Grammar 8: Configuration key=value pairs
// ================================================================

void test_config_line() {
	// key = value
	// key = [a-z_][a-z0-9_]*
	// value = [a-z0-9.,_-]+

	cma_op *key = A(R("a-z_"), REP(0, CMA_INF, R("a-z0-9_")));
	cma_op *value = MIN(1, R("a-z0-9.,_-"));
	cma_op *line = A(key, S("="), value);

	assert(cma_run(line, "timeout=30", 10) == 10);
	assert(cma_run(line, "max_retries=3", 13) == 13);
	assert(cma_run(line, "host=example.com", 16) == 16);
}

void test_config_line_invalid() {
	cma_op *key = A(R("a-z_"), REP(0, CMA_INF, R("a-z0-9_")));
	cma_op *value = MIN(1, R("a-z0-9.,_-"));
	cma_op *line = A(key, S("="), value);

	assert(cma_run(line, "=value", 6) == -1);  // no key
	assert(cma_run(line, "key=", 4) == -1);    // no value
}

// ================================================================
// Grammar 9: Multi-part with alternation
// ================================================================

void test_multi_part_grammar() {
	// Matches: identifier, integer, or simple string
	// identifier = [a-z_][a-z0-9_]*
	// integer = [+-]?[0-9]+
	// string = "..." (simplified: quote then printable chars then quote)

	cma_op *ident = A(R("a-z_"), REP(0, CMA_INF, R("a-z0-9_")));
	cma_op *integer = A(MAX(1, R("+-")), MIN(1, R("0-9")));
	// Use printable chars instead of NOT pattern for string content
	cma_op *string = A(S("\""), REP(0, CMA_INF, R("a-zA-Z0-9 ")), S("\""));
	cma_op *value = O(string, integer, ident);

	assert(cma_run(value, "variable_name", 13) == 13);
	assert(cma_run(value, "42", 2) == 2);
	assert(cma_run(value, "-999", 4) == 4);
	assert(cma_run(value, "\"hello\"", 7) == 7);
}

void test_multi_part_grammar_priority() {
	cma_op *ident = A(R("a-z_"), REP(0, CMA_INF, R("a-z0-9_")));
	cma_op *integer = A(MAX(1, R("+-")), MIN(1, R("0-9")));
	cma_op *string = A(S("\""), REP(0, CMA_INF, R("a-zA-Z0-9 ")), S("\""));
	cma_op *value = O(string, integer, ident);

	// String has priority (matches first in O)
	assert(cma_run(value, "\"123\"", 5) == 5);
}

// ================================================================
// Grammar 10: Version number (semantic versioning-like)
// ================================================================

void test_version_string() {
	// version = digit+ . digit+ . digit+
	// e.g., 1.2.3 or 10.20.30

	cma_op *number = MIN(1, R("0-9"));
	cma_op *dot = S(".");
	cma_op *version = A(number, dot, number, dot, number);

	assert(cma_run(version, "1.0.0", 5) == 5);
	assert(cma_run(version, "2.14.3", 6) == 6);
	assert(cma_run(version, "10.20.30", 8) == 8);
}

void test_version_string_incomplete() {
	cma_op *number = MIN(1, R("0-9"));
	cma_op *dot = S(".");
	cma_op *version = A(number, dot, number, dot, number);

	assert(cma_run(version, "1.2", 3) == -1);
	assert(cma_run(version, "1.2.3.4", 7) == 5);  // matches first 3 parts
}

// ================================================================
// Main
// ================================================================

int main() {
	printf("CMA big grammar tests:\n\n");

	printf("Grammar 1: C-style function signature\n");
	TEST(test_c_function_signature);
	TEST(test_c_function_signature_with_space);

	printf("\nGrammar 2: Simple math expression\n");
	TEST(test_simple_math_expr);
	TEST(test_simple_math_expr_invalid);

	printf("\nGrammar 3: CSV line\n");
	TEST(test_csv_line);
	TEST(test_csv_line_with_spaces);

	printf("\nGrammar 4: IPv4-like\n");
	TEST(test_ipv4_like);
	TEST(test_ipv4_like_incomplete);

	printf("\nGrammar 5: Email-like\n");
	TEST(test_email_like);
	TEST(test_email_like_invalid);

	printf("\nGrammar 6: Nested brackets\n");
	TEST(test_nested_brackets);
	TEST(test_nested_brackets_complex);
	TEST(test_nested_brackets_invalid);

	printf("\nGrammar 7: URL-like\n");
	TEST(test_url_like);
	TEST(test_url_like_minimal);

	printf("\nGrammar 8: Configuration key=value\n");
	TEST(test_config_line);
	TEST(test_config_line_invalid);

	printf("\nGrammar 9: Multi-part with alternation\n");
	TEST(test_multi_part_grammar);
	TEST(test_multi_part_grammar_priority);

	printf("\nGrammar 10: Version string\n");
	TEST(test_version_string);
	TEST(test_version_string_incomplete);

	printf("\n✓ All big grammar tests passed!\n");
	return 0;
}
