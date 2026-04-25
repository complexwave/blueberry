#include "../cma.h"
#include <stdio.h>
#include <assert.h>

#define TEST(name) printf("  %-40s ", #name); fflush(stdout); name(); printf("OK\n");

// ================================================================
// Edge cases and boundary conditions
// ================================================================

void test_empty_input_consuming_op() {
	cma_op *p = S("a");
	assert(cma_run(p, "", 0) == -1);
}

void test_pos_never_backwards() {
	// Match "ab", pos should never go backwards after match
	cma_op *p = A(S("a"), S("b"));
	int result = cma_run(p, "ab", 2);
	assert(result == 2);
}

void test_backtrack_after_and_partial() {
	cma_op *p = A(S("a"), S("b"), S("c"));
	int result = cma_run(p, "abd", 3);
	assert(result == -1);  // pos reset to 0
}

void test_rep_zero_zero() {
	cma_op *p = REP(0, 0, R("a-z"));
	assert(cma_run(p, "abc", 3) == 0);
}

void test_rep_zero_inf_zero_progress() {
	cma_op *p = REP(0, CMA_INF, AHEAD(S("x")));
	assert(cma_run(p, "x", 1) == 0);  // zero-progress guard
}

void test_deeply_nested_and_five_levels() {
	cma_op *p = A(A(A(A(A(S("a"))))));
	assert(cma_run(p, "a", 1) == 1);
}

void test_not_then_ahead_same_pos() {
	cma_op *p = A(NOT(S("x")), AHEAD(S("a")));
	assert(cma_run(p, "ab", 2) == 0);
}

void test_long_input_u32_fast_reject() {
	cma_op *p = S("hello");
	assert(cma_run(p, "helloworld", 10) == 5);
}

void test_single_char_match() {
	cma_op *p = R("a");
	assert(cma_run(p, "a", 1) == 1);
}

void test_single_char_miss() {
	cma_op *p = R("a");
	assert(cma_run(p, "b", 1) == -1);
}

void test_set_single_char_set() {
	cma_op *p = R("x");
	assert(cma_run(p, "x", 1) == 1);
	assert(cma_run(p, "y", 1) == -1);
}

void test_rep_min_greater_than_max() {
	// This shouldn't happen in practice, but REP doesn't validate
	cma_op *p = REP(5, 2, R("a"));  // min > max
	assert(cma_run(p, "aaaa", 4) == -1);  // always fails since count < min
}

void test_or_single_option() {
	cma_op *p = O(S("a"));
	assert(cma_run(p, "a", 1) == 1);
}

void test_and_single_option() {
	cma_op *p = A(S("a"));
	assert(cma_run(p, "a", 1) == 1);
}

void test_not_empty_input() {
	cma_op *p = NOT(S("ab"));
	assert(cma_run(p, "", 0) == 0);
}

void test_ahead_empty_input() {
	cma_op *p = AHEAD(S("ab"));
	assert(cma_run(p, "", 0) == -1);
}

// ================================================================
// Backtracking edge cases
// ================================================================

void test_and_second_fails_backtrack() {
	cma_op *p = A(S("foo"), R("0-9"));
	assert(cma_run(p, "foobar", 6) == -1);
}

void test_and_third_fails_full_reset() {
	cma_op *p = A(S("a"), S("b"), S("c"));
	assert(cma_run(p, "abd", 3) == -1);
	// pos should be 0, not 2
}

void test_rep_min_not_met_backtrack() {
	cma_op *p = REP(3, CMA_INF, R("a"));
	assert(cma_run(p, "aa", 2) == -1);
}

void test_not_backtracks_position() {
	cma_op *p = NOT(S("test"));
	int result = cma_run(p, "test", 4);
	assert(result == -1);
}

void test_ahead_backtracks_position() {
	cma_op *p = AHEAD(S("ok"));
	assert(cma_run(p, "ok", 2) == 0);
	// pos should be unchanged (at 0)
}

// ================================================================
// Multiple backtrack layers
// ================================================================

void test_and_of_and_backtrack() {
	cma_op *inner = A(S("a"), S("b"));
	cma_op *outer = A(inner, S("c"));
	assert(cma_run(outer, "abx", 3) == -1);
}

void test_or_with_and_backtrack() {
	cma_op *p1 = A(S("a"), S("b"));
	cma_op *p2 = S("a");
	cma_op *p = O(p1, p2);
	assert(cma_run(p, "a", 1) == 1);  // first fails, second wins
}

void test_rep_of_and_partial_match() {
	cma_op *inner = A(S("a"), S("b"));
	cma_op *rep = MIN(1, inner);
	assert(cma_run(rep, "ababac", 6) == 4);  // third 'a' has no 'b'
}

// ================================================================
// Greedy vs limits
// ================================================================

void test_rep_greedy_to_limit() {
	cma_op *p = MAX(3, R("a"));
	assert(cma_run(p, "aaaaa", 5) == 3);
}

void test_rep_stop_before_limit_at_boundary() {
	cma_op *p = MAX(3, R("a"));
	assert(cma_run(p, "aaa1", 4) == 3);
}

void test_rep_exact_match() {
	cma_op *p = REP(3, 3, R("a"));
	assert(cma_run(p, "aaa", 3) == 3);
	assert(cma_run(p, "aaaa", 4) == 3);
	assert(cma_run(p, "aa", 2) == -1);
}

void test_rep_range_uses_min() {
	cma_op *p = REP(2, 5, R("a"));
	assert(cma_run(p, "a", 1) == -1);  // less than min
}

void test_rep_range_uses_max() {
	cma_op *p = REP(2, 5, R("a"));
	assert(cma_run(p, "aaaaaa", 6) == 5);  // stops at max
}

// ================================================================
// Zero-width operations
// ================================================================

void test_not_zero_width() {
	cma_op *p = NOT(S("x"));
	assert(cma_run(p, "y", 1) == 0);  // zero-width
}

void test_ahead_zero_width() {
	cma_op *p = AHEAD(S("x"));
	assert(cma_run(p, "x", 1) == 0);  // zero-width
}

void test_rep_zero_min_zero_max_always_zero() {
	cma_op *p = REP(0, 0, S("x"));
	assert(cma_run(p, "xxx", 3) == 0);
}

void test_multiple_zero_width() {
	cma_op *p = A(AHEAD(S("a")), NOT(S("b")), AHEAD(S("a")));
	assert(cma_run(p, "a", 1) == 0);
}

// ================================================================
// String boundary cases
// ================================================================

void test_str_at_end_of_input() {
	cma_op *p = S("end");
	assert(cma_run(p, "end", 3) == 3);
}

void test_str_partial_at_end() {
	cma_op *p = S("hello");
	assert(cma_run(p, "hel", 3) == -1);
}

void test_set_boundary_lo() {
	cma_op *p = R("a-z");
	assert(cma_run(p, "a", 1) == 1);
}

void test_set_boundary_hi() {
	cma_op *p = R("a-z");
	assert(cma_run(p, "z", 1) == 1);
}

void test_set_outside_lo() {
	cma_op *p = R("a-z");
	assert(cma_run(p, "`", 1) == -1);  // one before 'a'
}

void test_set_outside_hi() {
	cma_op *p = R("a-z");
	assert(cma_run(p, "{", 1) == -1);  // one after 'z'
}

// ================================================================
// Large inputs
// ================================================================

void test_large_digit_sequence() {
	cma_op *p = MIN(1, R("0-9"));
	char buffer[101];
	for (int i = 0; i < 100; i++) {
		buffer[i] = '5';
	}
	assert(cma_run(p, buffer, 100) == 100);
}

void test_large_set_repetition() {
	cma_op *p = REP(50, 100, R("a-z"));
	char buffer[100];
	for (int i = 0; i < 75; i++) {
		buffer[i] = 'x';
	}
	assert(cma_run(p, buffer, 75) == 75);
}

void test_large_string_pattern() {
	cma_op *p = S("pattern");
	char buffer[200];
	strcpy(buffer, "pattern");
	strcat(buffer, "suffix");
	assert(cma_run(p, buffer, strlen(buffer)) == 7);
}

// ================================================================
// Main
// ================================================================

int main() {
	printf("CMA edge case tests:\n\n");

	printf("Basic edge cases:\n");
	TEST(test_empty_input_consuming_op);
	TEST(test_pos_never_backwards);
	TEST(test_backtrack_after_and_partial);
	TEST(test_rep_zero_zero);
	TEST(test_rep_zero_inf_zero_progress);
	TEST(test_deeply_nested_and_five_levels);
	TEST(test_not_then_ahead_same_pos);
	TEST(test_long_input_u32_fast_reject);

	printf("\nSingle character operations:\n");
	TEST(test_single_char_match);
	TEST(test_single_char_miss);
	TEST(test_set_single_char_set);

	printf("\nSingle operation variants:\n");
	TEST(test_rep_min_greater_than_max);
	TEST(test_or_single_option);
	TEST(test_and_single_option);
	TEST(test_not_empty_input);
	TEST(test_ahead_empty_input);

	printf("\nBacktracking edge cases:\n");
	TEST(test_and_second_fails_backtrack);
	TEST(test_and_third_fails_full_reset);
	TEST(test_rep_min_not_met_backtrack);
	TEST(test_not_backtracks_position);
	TEST(test_ahead_backtracks_position);

	printf("\nMultiple backtrack layers:\n");
	TEST(test_and_of_and_backtrack);
	TEST(test_or_with_and_backtrack);
	TEST(test_rep_of_and_partial_match);

	printf("\nGreedy vs limits:\n");
	TEST(test_rep_greedy_to_limit);
	TEST(test_rep_stop_before_limit_at_boundary);
	TEST(test_rep_exact_match);
	TEST(test_rep_range_uses_min);
	TEST(test_rep_range_uses_max);

	printf("\nZero-width operations:\n");
	TEST(test_not_zero_width);
	TEST(test_ahead_zero_width);
	TEST(test_rep_zero_min_zero_max_always_zero);
	TEST(test_multiple_zero_width);

	printf("\nString boundary cases:\n");
	TEST(test_str_at_end_of_input);
	TEST(test_str_partial_at_end);
	TEST(test_set_boundary_lo);
	TEST(test_set_boundary_hi);
	TEST(test_set_outside_lo);
	TEST(test_set_outside_hi);

	printf("\nLarge inputs:\n");
	TEST(test_large_digit_sequence);
	TEST(test_large_set_repetition);
	TEST(test_large_string_pattern);

	printf("\n✓ All edge case tests passed!\n");
	return 0;
}
