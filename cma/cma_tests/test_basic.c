#include "../cma.h"
#include <stdio.h>
#include <assert.h>

#define TEST(name) printf("  %-40s ", #name); fflush(stdout); name(); printf("OK\n");

// ================================================================
// STR — S("literal") - basic tests
// ================================================================

void test_str_exact_match() {
	cma_op *p = S("hello");
	assert(cma_run(p, "hello", 5) == 5);
}

void test_str_prefix_match() {
	cma_op *p = S("hello");
	assert(cma_run(p, "hello world", 11) == 5);
}

void test_str_input_shorter() {
	cma_op *p = S("hello");
	assert(cma_run(p, "hell", 4) == -1);
}

void test_str_empty_input() {
	cma_op *p = S("hello");
	assert(cma_run(p, "", 0) == -1);
}

void test_str_case_mismatch() {
	cma_op *p = S("hello");
	assert(cma_run(p, "HELLO", 5) == -1);
}

void test_str_single_byte() {
	cma_op *p = S("a");
	assert(cma_run(p, "a", 1) == 1);
}

// ================================================================
// SET — R("spec") - basic tests
// ================================================================

void test_set_single_char() {
	cma_op *p = R("abc");
	assert(cma_run(p, "a", 1) == 1);
	assert(cma_run(p, "d", 1) == -1);
}

void test_set_range() {
	cma_op *p = R("a-z");
	assert(cma_run(p, "m", 1) == 1);
	assert(cma_run(p, "A", 1) == -1);
}

void test_set_mixed() {
	cma_op *p = R("a-zA-Z_");
	assert(cma_run(p, "Z", 1) == 1);
	assert(cma_run(p, "5", 1) == -1);
}

// ================================================================
// AND — A(p1, p2, ...) - basic tests
// ================================================================

void test_and_sequence() {
	cma_op *p = A(S("foo"), R("0-9"));
	assert(cma_run(p, "foo3", 4) == 4);
	assert(cma_run(p, "foobar", 6) == -1);
}

void test_and_two_strings() {
	cma_op *p = A(S("a"), S("b"));
	assert(cma_run(p, "ab", 2) == 2);
}

void test_and_three_way() {
	cma_op *p = A(S("a"), S("b"), S("c"));
	assert(cma_run(p, "abc", 3) == 3);
}

// ================================================================
// OR — O(p1, p2, ...) - basic tests
// ================================================================

void test_or_alternatives() {
	cma_op *p = O(S("cat"), S("dog"));
	assert(cma_run(p, "cat", 3) == 3);
	assert(cma_run(p, "dog", 3) == 3);
	assert(cma_run(p, "rat", 3) == -1);
}

void test_or_ordered_choice() {
	cma_op *p = O(S("cat"), S("ca"));
	assert(cma_run(p, "cat", 3) == 3);  // first match wins
}

void test_or_three_alternatives() {
	cma_op *p = O(S("a"), S("b"), S("c"));
	assert(cma_run(p, "b", 1) == 1);
}

// ================================================================
// NOT — NOT(p) - basic tests
// ================================================================

void test_not_negation() {
	cma_op *p = A(NOT(S("xx")), R("a-z"));
	assert(cma_run(p, "ab", 2) == 1);
	assert(cma_run(p, "xx", 2) == -1);
}

void test_not_zero_width() {
	cma_op *p = NOT(S("ab"));
	assert(cma_run(p, "ab", 2) == -1);
	assert(cma_run(p, "cd", 2) == 0);
}

// ================================================================
// AHEAD — AHEAD(p) - basic tests
// ================================================================

void test_ahead_lookahead() {
	cma_op *p = A(AHEAD(S("foo")), S("foo"));
	assert(cma_run(p, "foobar", 6) == 3);
}

void test_ahead_consumes_zero() {
	cma_op *p = AHEAD(S("foo"));
	assert(cma_run(p, "foo", 3) == 0);
	assert(cma_run(p, "bar", 3) == -1);
}

// ================================================================
// REP — REP(min, max, p) / MIN(n, p) / MAX(n, p) - basic tests
// ================================================================

void test_rep_star() {
	cma_op *p = REP(0, CMA_INF, R("0-9"));
	assert(cma_run(p, "123abc", 6) == 3);
	assert(cma_run(p, "abc", 3) == 0);
}

void test_rep_plus() {
	cma_op *p = MIN(1, R("0-9"));
	assert(cma_run(p, "123abc", 6) == 3);
	assert(cma_run(p, "abc", 3) == -1);
}

void test_rep_exact() {
	cma_op *p = REP(3, 3, R("a-z"));
	assert(cma_run(p, "abcdef", 6) == 3);
	assert(cma_run(p, "ab", 2) == -1);
}

void test_rep_max() {
	cma_op *p = MAX(2, R("a-z"));
	assert(cma_run(p, "xyz", 3) == 2);
	assert(cma_run(p, "123", 3) == 0);
}

// ================================================================
// ENDL — ENDL() - basic tests
// ================================================================

void test_endl_at_end() {
	cma_op *p = A(S("abc"), ENDL());
	assert(cma_run(p, "abc", 3) == 3);
}

void test_endl_not_at_end() {
	cma_op *p = A(S("abc"), ENDL());
	assert(cma_run(p, "abc ", 4) == -1);
}

void test_endl_empty() {
	cma_op *p = ENDL();
	assert(cma_run(p, "", 0) == 0);
}

void test_endl_singleton() {
	assert(ENDL() == ENDL());
}

// ================================================================
// REF — REF(&ptr) - basic tests
// ================================================================

void test_ref_resolve() {
	cma_op *inner = R("a-z");
	cma_op *p = REF(&inner);
	assert(cma_run(p, "a", 1) == 1);
}

void test_ref_rebind() {
	cma_op *inner = R("0-9");
	cma_op *p = REF(&inner);
	assert(cma_run(p, "5", 1) == 1);
	inner = R("a-z");
	assert(cma_run(p, "a", 1) == 1);
}

void test_ref_forward() {
	cma_op *expr = NULL;
	cma_op *p = REF(&expr);
	expr = S("abc");
	assert(cma_run(p, "abc", 3) == 3);
}

// ================================================================
// Main
// ================================================================

int main() {
	printf("CMA basic tests:\n\n");

	printf("STR tests:\n");
	TEST(test_str_exact_match);
	TEST(test_str_prefix_match);
	TEST(test_str_input_shorter);
	TEST(test_str_empty_input);
	TEST(test_str_case_mismatch);
	TEST(test_str_single_byte);

	printf("\nSET tests:\n");
	TEST(test_set_single_char);
	TEST(test_set_range);
	TEST(test_set_mixed);

	printf("\nAND tests:\n");
	TEST(test_and_sequence);
	TEST(test_and_two_strings);
	TEST(test_and_three_way);

	printf("\nOR tests:\n");
	TEST(test_or_alternatives);
	TEST(test_or_ordered_choice);
	TEST(test_or_three_alternatives);

	printf("\nNOT tests:\n");
	TEST(test_not_negation);
	TEST(test_not_zero_width);

	printf("\nAHEAD tests:\n");
	TEST(test_ahead_lookahead);
	TEST(test_ahead_consumes_zero);

	printf("\nREP tests:\n");
	TEST(test_rep_star);
	TEST(test_rep_plus);
	TEST(test_rep_exact);
	TEST(test_rep_max);

	printf("\nENDL tests:\n");
	TEST(test_endl_at_end);
	TEST(test_endl_not_at_end);
	TEST(test_endl_empty);
	TEST(test_endl_singleton);

	printf("\nREF tests:\n");
	TEST(test_ref_resolve);
	TEST(test_ref_rebind);
	TEST(test_ref_forward);

	printf("\n✓ All basic tests passed!\n");
	return 0;
}
