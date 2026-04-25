#include "../cma.h"
#include <stdio.h>
#include <assert.h>

#define TEST(name) printf("  %-40s ", #name); fflush(stdout); name(); printf("OK\n");

// ================================================================
// Complex combinator interactions
// ================================================================

void test_not_ahead_equivalence() {
	cma_op *p = A(AHEAD(S("ab")), NOT(S("xy")));
	assert(cma_run(p, "ab", 2) == 0);
}

void test_double_negation() {
	cma_op *p = A(NOT(NOT(S("ab"))));
	assert(cma_run(p, "ab", 2) == 0);
}

void test_rep_of_and() {
	cma_op *p = MIN(1, A(S("a"), S("b")));
	assert(cma_run(p, "abababc", 7) == 6);
}

void test_rep_of_and_mismatch() {
	cma_op *p = MIN(1, A(S("a"), S("b")));
	assert(cma_run(p, "ababac", 6) == 4);
}

void test_rep_of_or() {
	cma_op *p = MIN(1, O(S("a"), S("b")));
	assert(cma_run(p, "ababab", 6) == 6);
}

void test_or_different_lengths() {
	cma_op *p = O(S("long"), S("lo"));
	assert(cma_run(p, "longword", 8) == 4);
}

void test_or_different_lengths_fallback() {
	cma_op *p = O(S("long"), S("lo"));
	assert(cma_run(p, "lo", 2) == 2);
}

void test_endl_after_rep() {
	cma_op *p = A(MIN(1, R("0-9")), ENDL());
	assert(cma_run(p, "123", 3) == 3);
}

void test_endl_after_rep_trailing() {
	cma_op *p = A(MIN(1, R("0-9")), ENDL());
	assert(cma_run(p, "123x", 4) == -1);
}

void test_ref_inside_rep() {
	cma_op *p_inner = S("a");
	cma_op *p = MIN(1, REF(&p_inner));
	assert(cma_run(p, "aaa", 3) == 3);
}

void test_not_at_start() {
	cma_op *p = A(NOT(S("bad")), S("good"));
	assert(cma_run(p, "goodday", 7) == 4);
	assert(cma_run(p, "bad", 3) == -1);
}

void test_ahead_at_start() {
	cma_op *p = A(AHEAD(S("start")), S("start"));
	assert(cma_run(p, "startend", 8) == 5);
}

void test_and_with_not() {
	cma_op *p = A(S("a"), NOT(S("b")), R("a-z"));
	assert(cma_run(p, "ac", 2) == 2);
	assert(cma_run(p, "ab", 2) == -1);
}

void test_and_with_ahead() {
	cma_op *p = A(S("a"), AHEAD(S("b")), S("b"));
	assert(cma_run(p, "ab", 2) == 2);
	assert(cma_run(p, "ac", 2) == -1);
}

void test_or_with_not() {
	cma_op *p = O(NOT(S("bad")), S("good"));
	assert(cma_run(p, "good", 4) == 0);  // NOT matches at pos 0
}

void test_rep_of_not() {
	// Star of NOT — succeeds zero times (NOT succeeds but advances nothing)
	cma_op *p = REP(0, CMA_INF, NOT(S("end")));
	assert(cma_run(p, "start", 5) == 0);  // zero-progress guard
}

void test_rep_of_ahead() {
	// Star of AHEAD — succeeds zero times (AHEAD succeeds but advances nothing)
	cma_op *p = REP(0, CMA_INF, AHEAD(S("x")));
	assert(cma_run(p, "x", 1) == 0);  // zero-progress guard
}

void test_not_of_and() {
	cma_op *p = NOT(A(S("a"), S("b")));
	assert(cma_run(p, "abc", 3) == -1);
	assert(cma_run(p, "axc", 3) == 0);
}

void test_ahead_of_or() {
	cma_op *p = AHEAD(O(S("a"), S("b")));
	assert(cma_run(p, "a", 1) == 0);
	assert(cma_run(p, "b", 1) == 0);
	assert(cma_run(p, "c", 1) == -1);
}

void test_and_of_rep() {
	cma_op *p = A(MIN(2, R("a-z")), R("0-9"));
	assert(cma_run(p, "ab5", 3) == 3);
	assert(cma_run(p, "a5", 2) == -1);
}

void test_or_of_and() {
	cma_op *p = O(A(S("x"), S("y")), A(S("a"), S("b")));
	assert(cma_run(p, "xy", 2) == 2);
	assert(cma_run(p, "ab", 2) == 2);
}

void test_nested_rep() {
	// REP with AND that has REP
	cma_op *p = MIN(1, A(MIN(1, R("a-z")), R("0-9")));
	assert(cma_run(p, "a5b6c7", 6) == 6);  // matches "a5" then "b6" then "c7"
}

void test_deeply_nested_and() {
	cma_op *p = A(A(A(S("a"))));
	assert(cma_run(p, "a", 1) == 1);
}

void test_deeply_nested_or() {
	cma_op *p = O(O(O(S("a"))));
	assert(cma_run(p, "a", 1) == 1);
}

void test_alternating_and_or() {
	cma_op *p = O(A(S("a"), S("b")), A(S("c"), S("d")));
	assert(cma_run(p, "ab", 2) == 2);
	assert(cma_run(p, "cd", 2) == 2);
}

void test_alternating_or_and() {
	cma_op *p = A(O(S("a"), S("b")), O(R("0-9"), R("x-z")));
	assert(cma_run(p, "a5", 2) == 2);
	assert(cma_run(p, "bz", 2) == 2);
}

void test_rep_with_not_and_ahead() {
	cma_op *p = MIN(1, A(NOT(S("no")), AHEAD(R("a-z")), R("a-z")));
	assert(cma_run(p, "yes", 3) == 3);
	assert(cma_run(p, "no", 2) == -1);
}

void test_triple_negation() {
	cma_op *p = NOT(NOT(NOT(S("a"))));
	assert(cma_run(p, "a", 1) == -1);
	assert(cma_run(p, "b", 1) == 0);
}

void test_or_with_overlapping_matches() {
	cma_op *p = O(S("abc"), S("abcd"));
	assert(cma_run(p, "abcd", 4) == 3);  // first match wins
}

void test_and_backtrack_on_last() {
	cma_op *p = A(S("a"), S("b"), S("c"), S("d"), S("e"));
	assert(cma_run(p, "abcde", 5) == 5);
	assert(cma_run(p, "abcdx", 5) == -1);  // last fails, all backtrack
}

void test_rep_greedy_vs_exact() {
	cma_op *p1 = REP(1, CMA_INF, R("a"));
	cma_op *p2 = REP(3, 3, R("a"));
	assert(cma_run(p1, "aaaa", 4) == 4);  // greedy
	assert(cma_run(p2, "aaaa", 4) == 3);  // exact
}

void test_not_consuming_after_ahead() {
	cma_op *p = A(AHEAD(S("x")), NOT(S("y")));
	assert(cma_run(p, "x", 1) == 0);
}

void test_ahead_not_interleaved() {
	cma_op *p = A(AHEAD(NOT(S("bad"))), R("a-z"));
	assert(cma_run(p, "good", 4) == 1);
}

// ================================================================
// Main
// ================================================================

int main() {
	printf("CMA combinator interaction tests:\n\n");

	printf("Combinator interactions:\n");
	TEST(test_not_ahead_equivalence);
	TEST(test_double_negation);
	TEST(test_rep_of_and);
	TEST(test_rep_of_and_mismatch);
	TEST(test_rep_of_or);
	TEST(test_or_different_lengths);
	TEST(test_or_different_lengths_fallback);
	TEST(test_endl_after_rep);
	TEST(test_endl_after_rep_trailing);
	TEST(test_ref_inside_rep);

	printf("\nNOT and AHEAD combinations:\n");
	TEST(test_not_at_start);
	TEST(test_ahead_at_start);
	TEST(test_and_with_not);
	TEST(test_and_with_ahead);
	TEST(test_or_with_not);
	TEST(test_rep_of_not);
	TEST(test_rep_of_ahead);

	printf("\nComplex nesting:\n");
	TEST(test_not_of_and);
	TEST(test_ahead_of_or);
	TEST(test_and_of_rep);
	TEST(test_or_of_and);
	TEST(test_nested_rep);
	TEST(test_deeply_nested_and);
	TEST(test_deeply_nested_or);
	TEST(test_alternating_and_or);
	TEST(test_alternating_or_and);

	printf("\nAdvanced combinations:\n");
	TEST(test_rep_with_not_and_ahead);
	TEST(test_triple_negation);
	TEST(test_or_with_overlapping_matches);
	TEST(test_and_backtrack_on_last);
	TEST(test_rep_greedy_vs_exact);
	TEST(test_not_consuming_after_ahead);
	TEST(test_ahead_not_interleaved);

	printf("\n✓ All combinator tests passed!\n");
	return 0;
}
