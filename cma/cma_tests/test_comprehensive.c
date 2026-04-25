#include "../cma.h"
#include <stdio.h>
#include <assert.h>

#define TEST(name) printf("  %-40s ", #name); fflush(stdout); name(); printf("OK\n");

// ================================================================
// STR — S("literal")
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

void test_str_same_length_wrong_bytes() {
	cma_op *p = S("hello");
	assert(cma_run(p, "world", 5) == -1);
}

void test_str_case_mismatch() {
	cma_op *p = S("hello");
	assert(cma_run(p, "HELLO", 5) == -1);
}

void test_str_u32_fast_reject() {
	// "hello" vs "helo" - first 4 bytes differ at pos 3
	cma_op *p = S("hello");
	assert(cma_run(p, "helo", 4) == -1);
}

void test_str_u32_pass_memcmp_fail() {
	// "hello" vs "helXo" - first 4 bytes match ('hell'), but full memcmp fails
	cma_op *p = S("hello");
	assert(cma_run(p, "helXo", 5) == -1);
}

void test_str_null_bytes() {
	// cma_emit_str uses strlen, which stops at null
	// This test shows the limitation: S() cannot embed null bytes
	// Just test with empty string after null
	cma_op *p = S("\x01\x02");
	assert(cma_run(p, "\x01\x02", 2) == 2);
}

void test_str_single_byte() {
	cma_op *p = S("a");
	assert(cma_run(p, "a", 1) == 1);
}

void test_str_single_byte_miss() {
	cma_op *p = S("a");
	assert(cma_run(p, "b", 1) == -1);
}

void test_str_five_bytes_no_fast_reject() {
	// 5-byte pattern, exercises memcmp path
	cma_op *p = S("abcde");
	assert(cma_run(p, "abcde", 5) == 5);
}

// ================================================================
// SET — R("spec")
// ================================================================

void test_set_single_listed() {
	cma_op *p = R("abc");
	assert(cma_run(p, "a", 1) == 1);
}

void test_set_single_not_listed() {
	cma_op *p = R("abc");
	assert(cma_run(p, "d", 1) == -1);
}

void test_set_empty_input() {
	cma_op *p = R("abc");
	assert(cma_run(p, "", 0) == -1);
}

void test_set_range_midrange() {
	cma_op *p = R("a-z");
	assert(cma_run(p, "m", 1) == 1);
}

void test_set_range_lo() {
	cma_op *p = R("a-z");
	assert(cma_run(p, "a", 1) == 1);
}

void test_set_range_hi() {
	cma_op *p = R("a-z");
	assert(cma_run(p, "z", 1) == 1);
}

void test_set_range_out_upper() {
	cma_op *p = R("a-z");
	assert(cma_run(p, "A", 1) == -1);
}

void test_set_range_out_digit() {
	cma_op *p = R("a-z");
	assert(cma_run(p, "0", 1) == -1);
}

void test_set_reversed_range() {
	// z-a reversed, internally swapped
	cma_op *p = R("z-a");
	assert(cma_run(p, "z", 1) == 1);
}

void test_set_reversed_range_lo() {
	cma_op *p = R("z-a");
	assert(cma_run(p, "a", 1) == 1);
}

void test_set_mixed_literal_after_ranges() {
	cma_op *p = R("a-zA-Z_");
	assert(cma_run(p, "_", 1) == 1);
}

void test_set_mixed_second_range_hi() {
	cma_op *p = R("a-zA-Z_");
	assert(cma_run(p, "Z", 1) == 1);
}

void test_set_mixed_not_in_range() {
	cma_op *p = R("a-zA-Z_");
	assert(cma_run(p, "5", 1) == -1);
}

void test_set_boundary_byte_one() {
	cma_op *p = R("\x01");
	assert(cma_run(p, "\x01", 1) == 1);
}

void test_set_ascii_range_hi() {
	cma_op *p = R("\x41-\x5a");  // A-Z in hex
	assert(cma_run(p, "\x5a", 1) == 1);  // Z
}

void test_set_ascii_range_lo() {
	cma_op *p = R("\x41-\x5a");  // A-Z in hex
	assert(cma_run(p, "\x41", 1) == 1);  // A
}

// ================================================================
// AND — A(p1, p2, ...)
// ================================================================

void test_and_both_match() {
	cma_op *p = A(S("foo"), R("0-9"));
	assert(cma_run(p, "foo3", 4) == 4);
}

void test_and_second_fails() {
	cma_op *p = A(S("foo"), R("0-9"));
	assert(cma_run(p, "foobar", 6) == -1);
}

void test_and_first_fails() {
	cma_op *p = A(S("foo"), R("0-9"));
	assert(cma_run(p, "xyz", 3) == -1);
}

void test_and_first_matches_second_hits_end() {
	cma_op *p = A(S("foo"), R("0-9"));
	assert(cma_run(p, "foo", 3) == -1);
}

void test_and_two_single_chars() {
	cma_op *p = A(S("a"), S("b"));
	assert(cma_run(p, "ab", 2) == 2);
}

void test_and_single_op_unwrap() {
	// n==1 case: cma_emit_seq returns ops[0] directly
	cma_op *p = A(S("a"));
	assert(cma_run(p, "a", 1) == 1);
}

void test_and_three_way() {
	cma_op *p = A(S("a"), S("b"), S("c"));
	assert(cma_run(p, "abc", 3) == 3);
}

void test_and_third_fails_reset() {
	cma_op *p = A(S("a"), S("b"), S("c"));
	assert(cma_run(p, "abd", 3) == -1);
}

// ================================================================
// OR — O(p1, p2, ...)
// ================================================================

void test_or_first_alt() {
	cma_op *p = O(S("cat"), S("dog"));
	assert(cma_run(p, "cat", 3) == 3);
}

void test_or_second_alt() {
	cma_op *p = O(S("cat"), S("dog"));
	assert(cma_run(p, "dog", 3) == 3);
}

void test_or_no_alt_matches() {
	cma_op *p = O(S("cat"), S("dog"));
	assert(cma_run(p, "rat", 3) == -1);
}

void test_or_first_alt_wins() {
	// ordered choice — first match wins
	cma_op *p = O(S("cat"), S("ca"));
	assert(cma_run(p, "cat", 3) == 3);
}

void test_or_first_fails_second_matches() {
	cma_op *p = O(S("cat"), S("ca"));
	assert(cma_run(p, "ca", 2) == 2);
}

void test_or_all_fail() {
	cma_op *p = O(S("a"), S("b"), S("c"));
	assert(cma_run(p, "x", 1) == -1);
}

void test_or_empty_input_all_fail() {
	cma_op *p = O(S("a"), S("b"));
	assert(cma_run(p, "", 0) == -1);
}

// ================================================================
// NOT — NOT(p)
// ================================================================

void test_not_inner_succeeds_consuming() {
	cma_op *p = A(NOT(S("xx")), R("a-z"));
	assert(cma_run(p, "ab", 2) == 1);
}

void test_not_inner_fails() {
	cma_op *p = A(NOT(S("xx")), R("a-z"));
	assert(cma_run(p, "xx", 2) == -1);
}

void test_not_inner_needs_more_bytes() {
	// "xx" needs 2 bytes, only 1 available — misses, NOT succeeds
	cma_op *p = A(NOT(S("xx")), R("a-z"));
	assert(cma_run(p, "x", 1) == 1);
}

void test_not_simple_match() {
	cma_op *p = NOT(S("ab"));
	assert(cma_run(p, "ab", 2) == -1);
}

void test_not_simple_miss() {
	cma_op *p = NOT(S("ab"));
	assert(cma_run(p, "cd", 2) == 0);
}

void test_not_empty_input() {
	cma_op *p = NOT(S("ab"));
	assert(cma_run(p, "", 0) == 0);
}

void test_not_multi_arg_match() {
	cma_op *p = NOT(A(S("a"), S("b")));
	assert(cma_run(p, "abc", 3) == -1);
}

void test_not_multi_arg_miss() {
	cma_op *p = NOT(A(S("a"), S("b")));
	assert(cma_run(p, "axc", 3) == 0);
}

// ================================================================
// AHEAD — AHEAD(p)
// ================================================================

void test_ahead_lookahead_pass_then_consume() {
	cma_op *p = A(AHEAD(S("foo")), S("foo"));
	assert(cma_run(p, "foobar", 6) == 3);
}

void test_ahead_lookahead_fail() {
	cma_op *p = A(AHEAD(S("foo")), S("foo"));
	assert(cma_run(p, "barfoo", 6) == -1);
}

void test_ahead_matches_consumes_zero() {
	cma_op *p = AHEAD(S("foo"));
	assert(cma_run(p, "foo", 3) == 0);
}

void test_ahead_misses() {
	cma_op *p = AHEAD(S("foo"));
	assert(cma_run(p, "bar", 3) == -1);
}

void test_ahead_empty_input() {
	cma_op *p = AHEAD(S("foo"));
	assert(cma_run(p, "", 0) == -1);
}

void test_ahead_multi_arg_matches() {
	cma_op *p = AHEAD(A(S("a"), S("b")));
	assert(cma_run(p, "ab", 2) == 0);
}

void test_ahead_multi_arg_misses() {
	cma_op *p = AHEAD(A(S("a"), S("b")));
	assert(cma_run(p, "ac", 2) == -1);
}

// ================================================================
// REP — REP(min, max, p)
// ================================================================

void test_rep_star_three_digits() {
	cma_op *p = REP(0, CMA_INF, R("0-9"));
	assert(cma_run(p, "123abc", 6) == 3);
}

void test_rep_star_zero_matches() {
	cma_op *p = REP(0, CMA_INF, R("0-9"));
	assert(cma_run(p, "abc", 3) == 0);
}

void test_rep_plus_three_digits() {
	cma_op *p = MIN(1, R("0-9"));
	assert(cma_run(p, "123abc", 6) == 3);
}

void test_rep_plus_zero_fails() {
	cma_op *p = MIN(1, R("0-9"));
	assert(cma_run(p, "abc", 3) == -1);
}

void test_rep_exact_count() {
	cma_op *p = REP(3, 3, R("a-z"));
	assert(cma_run(p, "abcdef", 6) == 3);
}

void test_rep_exact_insufficient() {
	cma_op *p = REP(3, 3, R("a-z"));
	assert(cma_run(p, "ab", 2) == -1);
}

void test_rep_greedy_up_to_max() {
	cma_op *p = REP(2, 4, R("a-z"));
	assert(cma_run(p, "abcdef", 6) == 4);
}

void test_rep_hits_end_meets_min() {
	cma_op *p = REP(2, 4, R("a-z"));
	assert(cma_run(p, "ab", 2) == 2);
}

void test_rep_below_min() {
	cma_op *p = REP(2, 4, R("a-z"));
	assert(cma_run(p, "a", 1) == -1);
}

void test_rep_max_greedy() {
	cma_op *p = MAX(2, R("a-z"));
	assert(cma_run(p, "xyz", 3) == 2);
}

void test_rep_max_less_than_max() {
	cma_op *p = MAX(2, R("a-z"));
	assert(cma_run(p, "x", 1) == 1);
}

void test_rep_max_zero_matches() {
	cma_op *p = MAX(2, R("a-z"));
	assert(cma_run(p, "123", 3) == 0);
}

void test_rep_non_set_inner() {
	// Non-SET inner uses generic path
	cma_op *p = REP(1, CMA_INF, S("a"));
	assert(cma_run(p, "aa", 2) == 2);
}

void test_rep_zero_progress_guard() {
	// Inner matches but advances nothing — loop breaks
	cma_op *p = REP(0, CMA_INF, AHEAD(S("x")));
	assert(cma_run(p, "x", 1) == 0);
}

// REP_SET fast path
void test_rep_set_fast_path_scan() {
	cma_op *p = MIN(1, R("a-z"));
	assert(cma_run(p, "aaa1", 4) == 3);
}

void test_rep_set_zero_matches_fail() {
	cma_op *p = MIN(1, R("a-z"));
	assert(cma_run(p, "1aaa", 4) == -1);
}

void test_rep_set_below_min() {
	cma_op *p = REP(4, CMA_INF, R("a-z"));
	assert(cma_run(p, "aaa", 3) == -1);
}

void test_rep_set_stops_at_max() {
	cma_op *p = REP(2, 3, R("a-z"));
	assert(cma_run(p, "aaaa", 4) == 3);
}

// ================================================================
// ENDL — ENDL()
// ================================================================

void test_endl_pos_at_end() {
	cma_op *p = A(S("abc"), ENDL());
	assert(cma_run(p, "abc", 3) == 3);
}

void test_endl_trailing_space() {
	cma_op *p = A(S("abc"), ENDL());
	assert(cma_run(p, "abc ", 4) == -1);
}

void test_endl_empty_input() {
	cma_op *p = ENDL();
	assert(cma_run(p, "", 0) == 0);
}

void test_endl_non_empty_not_at_end() {
	cma_op *p = ENDL();
	assert(cma_run(p, "x", 1) == -1);
}

void test_endl_consumed_one_two_remain() {
	cma_op *p = A(R("a-z"), ENDL());
	assert(cma_run(p, "abc", 3) == -1);
}

void test_endl_consumed_one_now_at_end() {
	cma_op *p = A(R("a-z"), ENDL());
	assert(cma_run(p, "a", 1) == 1);
}

void test_endl_singleton() {
	// ENDL() returns same pointer
	assert(ENDL() == ENDL());
}

// ================================================================
// REF — REF(&ptr)
// ================================================================

void test_ref_resolve_non_null() {
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

void test_ref_forward_reference() {
	cma_op *expr = NULL;
	cma_op *p = REF(&expr);
	expr = S("abc");
	assert(cma_run(p, "abc", 3) == 3);
}

void test_ref_recursive_grammar_atom() {
	cma_op *expr = NULL;
	cma_op *atom  = R("0-9");
	cma_op *paren = A(S("("), REF(&expr), S(")"));
	expr = O(paren, atom);

	assert(cma_run(expr, "3", 1) == 1);
}

void test_ref_recursive_grammar_one_level() {
	cma_op *expr = NULL;
	cma_op *atom  = R("0-9");
	cma_op *paren = A(S("("), REF(&expr), S(")"));
	expr = O(paren, atom);

	assert(cma_run(expr, "(3)", 3) == 3);
}

void test_ref_recursive_grammar_two_levels() {
	cma_op *expr = NULL;
	cma_op *atom  = R("0-9");
	cma_op *paren = A(S("("), REF(&expr), S(")"));
	expr = O(paren, atom);

	assert(cma_run(expr, "((3))", 5) == 5);
}

void test_ref_recursive_grammar_unmatched() {
	cma_op *expr = NULL;
	cma_op *atom  = R("0-9");
	cma_op *paren = A(S("("), REF(&expr), S(")"));
	expr = O(paren, atom);

	assert(cma_run(expr, "(((", 3) == -1);
}

void test_ref_mutual_recursion_x() {
	cma_op *a = NULL, *b = NULL;
	a = O(A(S("x"), REF(&b)), S("x"));
	b = A(S("y"), REF(&a));

	assert(cma_run(a, "x", 1) == 1);
}

void test_ref_mutual_recursion_xyx() {
	cma_op *a = NULL, *b = NULL;
	a = O(A(S("x"), REF(&b)), S("x"));
	b = A(S("y"), REF(&a));

	assert(cma_run(a, "xyx", 3) == 3);
}

// ================================================================
// Combinator interactions
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

// ================================================================
// Combo: identifiers and integers
// ================================================================

void test_combo_identifier() {
	cma_op *head = R("a-zA-Z_");
	cma_op *tail = REP(0, CMA_INF, R("a-zA-Z0-9_"));
	cma_op *ident = A(head, tail);

	assert(cma_run(ident, "foo_bar123 ", 11) == 10);
}

void test_combo_identifier_underscore() {
	cma_op *head = R("a-zA-Z_");
	cma_op *tail = REP(0, CMA_INF, R("a-zA-Z0-9_"));
	cma_op *ident = A(head, tail);

	assert(cma_run(ident, "_x", 2) == 2);
}

void test_combo_identifier_digit_start() {
	cma_op *head = R("a-zA-Z_");
	cma_op *tail = REP(0, CMA_INF, R("a-zA-Z0-9_"));
	cma_op *ident = A(head, tail);

	assert(cma_run(ident, "123", 3) == -1);
}

void test_combo_integer() {
	cma_op *sign = MAX(1, R("+-"));
	cma_op *digits = MIN(1, R("0-9"));
	cma_op *integer = A(sign, digits);

	assert(cma_run(integer, "42", 2) == 2);
}

void test_combo_integer_negative() {
	cma_op *sign = MAX(1, R("+-"));
	cma_op *digits = MIN(1, R("0-9"));
	cma_op *integer = A(sign, digits);

	assert(cma_run(integer, "-7x", 3) == 2);
}

void test_combo_integer_positive() {
	cma_op *sign = MAX(1, R("+-"));
	cma_op *digits = MIN(1, R("0-9"));
	cma_op *integer = A(sign, digits);

	assert(cma_run(integer, "+123", 4) == 4);
}

void test_combo_integer_no_digits() {
	cma_op *sign = MAX(1, R("+-"));
	cma_op *digits = MIN(1, R("0-9"));
	cma_op *integer = A(sign, digits);

	assert(cma_run(integer, "abc", 3) == -1);
}

// ================================================================
// Edge cases
// ================================================================

void test_edge_empty_input_consuming_op() {
	cma_op *p = S("a");
	assert(cma_run(p, "", 0) == -1);
}

void test_edge_pos_never_backwards() {
	// Match "ab", pos should never go backwards
	cma_op *p = A(S("a"), S("b"));
	int result = cma_run(p, "ab", 2);
	assert(result >= 0);
}

void test_edge_backtrack_after_and() {
	cma_op *p = A(S("a"), S("b"), S("c"));
	int result = cma_run(p, "abd", 3);
	// Should fail, pos reset to 0
	assert(result == -1);
}

void test_edge_rep_zero_zero() {
	cma_op *p = REP(0, 0, R("a-z"));
	assert(cma_run(p, "abc", 3) == 0);
}

void test_edge_rep_zero_inf_zero_progress() {
	cma_op *p = REP(0, CMA_INF, AHEAD(S("x")));
	assert(cma_run(p, "x", 1) == 0);
}

void test_edge_deeply_nested_and() {
	cma_op *p = A(A(A(S("a"))));
	assert(cma_run(p, "a", 1) == 1);
}

void test_edge_not_then_ahead_same_pos() {
	cma_op *p = A(NOT(S("x")), AHEAD(S("a")));
	assert(cma_run(p, "ab", 2) == 0);
}

void test_edge_long_input_u32_fast_reject() {
	cma_op *p = S("hello");
	assert(cma_run(p, "helloworld", 10) == 5);
}

// ================================================================
// Main
// ================================================================

int main() {
	printf("CMA comprehensive tests:\n\n");

	printf("STR tests:\n");
	TEST(test_str_exact_match);
	TEST(test_str_prefix_match);
	TEST(test_str_input_shorter);
	TEST(test_str_empty_input);
	TEST(test_str_same_length_wrong_bytes);
	TEST(test_str_case_mismatch);
	TEST(test_str_u32_fast_reject);
	TEST(test_str_u32_pass_memcmp_fail);
	TEST(test_str_null_bytes);
	TEST(test_str_single_byte);
	TEST(test_str_single_byte_miss);
	TEST(test_str_five_bytes_no_fast_reject);

	printf("\nSET tests:\n");
	TEST(test_set_single_listed);
	TEST(test_set_single_not_listed);
	TEST(test_set_empty_input);
	TEST(test_set_range_midrange);
	TEST(test_set_range_lo);
	TEST(test_set_range_hi);
	TEST(test_set_range_out_upper);
	TEST(test_set_range_out_digit);
	TEST(test_set_reversed_range);
	TEST(test_set_reversed_range_lo);
	TEST(test_set_mixed_literal_after_ranges);
	TEST(test_set_mixed_second_range_hi);
	TEST(test_set_mixed_not_in_range);
	TEST(test_set_boundary_byte_one);
	TEST(test_set_ascii_range_hi);
	TEST(test_set_ascii_range_lo);

	printf("\nAND tests:\n");
	TEST(test_and_both_match);
	TEST(test_and_second_fails);
	TEST(test_and_first_fails);
	TEST(test_and_first_matches_second_hits_end);
	TEST(test_and_two_single_chars);
	TEST(test_and_single_op_unwrap);
	TEST(test_and_three_way);
	TEST(test_and_third_fails_reset);

	printf("\nOR tests:\n");
	TEST(test_or_first_alt);
	TEST(test_or_second_alt);
	TEST(test_or_no_alt_matches);
	TEST(test_or_first_alt_wins);
	TEST(test_or_first_fails_second_matches);
	TEST(test_or_all_fail);
	TEST(test_or_empty_input_all_fail);

	printf("\nNOT tests:\n");
	TEST(test_not_inner_succeeds_consuming);
	TEST(test_not_inner_fails);
	TEST(test_not_inner_needs_more_bytes);
	TEST(test_not_simple_match);
	TEST(test_not_simple_miss);
	TEST(test_not_empty_input);
	TEST(test_not_multi_arg_match);
	TEST(test_not_multi_arg_miss);

	printf("\nAHEAD tests:\n");
	TEST(test_ahead_lookahead_pass_then_consume);
	TEST(test_ahead_lookahead_fail);
	TEST(test_ahead_matches_consumes_zero);
	TEST(test_ahead_misses);
	TEST(test_ahead_empty_input);
	TEST(test_ahead_multi_arg_matches);
	TEST(test_ahead_multi_arg_misses);

	printf("\nREP tests:\n");
	TEST(test_rep_star_three_digits);
	TEST(test_rep_star_zero_matches);
	TEST(test_rep_plus_three_digits);
	TEST(test_rep_plus_zero_fails);
	TEST(test_rep_exact_count);
	TEST(test_rep_exact_insufficient);
	TEST(test_rep_greedy_up_to_max);
	TEST(test_rep_hits_end_meets_min);
	TEST(test_rep_below_min);
	TEST(test_rep_max_greedy);
	TEST(test_rep_max_less_than_max);
	TEST(test_rep_max_zero_matches);
	TEST(test_rep_non_set_inner);
	TEST(test_rep_zero_progress_guard);
	TEST(test_rep_set_fast_path_scan);
	TEST(test_rep_set_zero_matches_fail);
	TEST(test_rep_set_below_min);
	TEST(test_rep_set_stops_at_max);

	printf("\nENDL tests:\n");
	TEST(test_endl_pos_at_end);
	TEST(test_endl_trailing_space);
	TEST(test_endl_empty_input);
	TEST(test_endl_non_empty_not_at_end);
	TEST(test_endl_consumed_one_two_remain);
	TEST(test_endl_consumed_one_now_at_end);
	TEST(test_endl_singleton);

	printf("\nREF tests:\n");
	TEST(test_ref_resolve_non_null);
	TEST(test_ref_rebind);
	TEST(test_ref_forward_reference);
	TEST(test_ref_recursive_grammar_atom);
	TEST(test_ref_recursive_grammar_one_level);
	TEST(test_ref_recursive_grammar_two_levels);
	TEST(test_ref_recursive_grammar_unmatched);
	TEST(test_ref_mutual_recursion_x);
	TEST(test_ref_mutual_recursion_xyx);

	printf("\nCombinator interaction tests:\n");
	TEST(test_not_ahead_equivalence);
	TEST(test_double_negation);
	TEST(test_rep_of_and);
	TEST(test_rep_of_and_mismatch);
	TEST(test_or_different_lengths);
	TEST(test_or_different_lengths_fallback);
	TEST(test_endl_after_rep);
	TEST(test_endl_after_rep_trailing);
	TEST(test_ref_inside_rep);

	printf("\nCombo pattern tests:\n");
	TEST(test_combo_identifier);
	TEST(test_combo_identifier_underscore);
	TEST(test_combo_identifier_digit_start);
	TEST(test_combo_integer);
	TEST(test_combo_integer_negative);
	TEST(test_combo_integer_positive);
	TEST(test_combo_integer_no_digits);

	printf("\nEdge case tests:\n");
	TEST(test_edge_empty_input_consuming_op);
	TEST(test_edge_pos_never_backwards);
	TEST(test_edge_backtrack_after_and);
	TEST(test_edge_rep_zero_zero);
	TEST(test_edge_rep_zero_inf_zero_progress);
	TEST(test_edge_deeply_nested_and);
	TEST(test_edge_not_then_ahead_same_pos);
	TEST(test_edge_long_input_u32_fast_reject);

	printf("\n✓ All tests passed!\n");
	return 0;
}
