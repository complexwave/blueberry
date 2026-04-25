#include "../cma.h"
#include <stdio.h>
#include <assert.h>

#define TEST(name) printf("  %-40s ", #name); fflush(stdout); name(); printf("OK\n");

// ================================================================
// String matching edge cases and advanced scenarios
// ================================================================

void test_str_same_length_wrong_bytes() {
	cma_op *p = S("hello");
	assert(cma_run(p, "world", 5) == -1);
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

void test_str_single_byte_miss() {
	cma_op *p = S("a");
	assert(cma_run(p, "b", 1) == -1);
}

void test_str_five_bytes() {
	// 5-byte pattern, exercises memcmp path (no u32 fast-reject)
	cma_op *p = S("abcde");
	assert(cma_run(p, "abcde", 5) == 5);
}

void test_str_long_pattern() {
	cma_op *p = S("hello world");
	assert(cma_run(p, "hello world!", 12) == 11);
}

void test_str_repeated_substring() {
	cma_op *p = S("aa");
	assert(cma_run(p, "aaaa", 4) == 2);
}

void test_str_pattern_in_middle() {
	cma_op *p = S("middle");
	assert(cma_run(p, "startmiddleend", 14) == -1);  // doesn't match at pos 0
}

void test_str_multiple_occurrences() {
	cma_op *p = S("ab");
	assert(cma_run(p, "abcab", 5) == 2);  // matches first occurrence
}

void test_str_special_chars() {
	cma_op *p = S("!@#");
	assert(cma_run(p, "!@#$", 4) == 3);
}

void test_str_whitespace() {
	cma_op *p = S("a b");
	assert(cma_run(p, "a b c", 5) == 3);
}

void test_str_digits_in_string() {
	cma_op *p = S("123");
	assert(cma_run(p, "123abc", 6) == 3);
}

// ================================================================
// Set matching edge cases and ranges
// ================================================================

void test_set_range_boundaries() {
	cma_op *p = R("a-z");
	assert(cma_run(p, "a", 1) == 1);
	assert(cma_run(p, "z", 1) == 1);
	assert(cma_run(p, "m", 1) == 1);
}

void test_set_reversed_range() {
	// z-a reversed, internally swapped
	cma_op *p = R("z-a");
	assert(cma_run(p, "z", 1) == 1);
	assert(cma_run(p, "a", 1) == 1);
	assert(cma_run(p, "m", 1) == 1);
}

void test_set_multiple_ranges() {
	cma_op *p = R("a-zA-Z");
	assert(cma_run(p, "a", 1) == 1);
	assert(cma_run(p, "Z", 1) == 1);
	assert(cma_run(p, "5", 1) == -1);
}

void test_set_literal_and_range() {
	cma_op *p = R("0-9x");
	assert(cma_run(p, "5", 1) == 1);
	assert(cma_run(p, "x", 1) == 1);
	assert(cma_run(p, "a", 1) == -1);
}

void test_set_ascii_range_hex() {
	cma_op *p = R("\x41-\x5a");  // A-Z in hex
	assert(cma_run(p, "\x41", 1) == 1);  // A
	assert(cma_run(p, "\x5a", 1) == 1);  // Z
}

void test_set_boundary_byte() {
	cma_op *p = R("\x01");
	assert(cma_run(p, "\x01", 1) == 1);
}

void test_set_empty_input() {
	cma_op *p = R("abc");
	assert(cma_run(p, "", 0) == -1);
}

// ================================================================
// Complex string + set combinations
// ================================================================

void test_str_then_set() {
	cma_op *p = A(S("v"), R("0-9"));
	assert(cma_run(p, "v1", 2) == 2);
	assert(cma_run(p, "vx", 2) == -1);
}

void test_set_then_str() {
	cma_op *p = A(R("a-z"), S("bc"));
	assert(cma_run(p, "abc", 3) == 3);
	assert(cma_run(p, "xbc", 3) == 3);  // x matches R("a-z"), bc matches S("bc")
}

void test_str_set_str_sequence() {
	cma_op *p = A(S("a"), R("0-9"), S("x"));
	assert(cma_run(p, "a5x", 3) == 3);
	assert(cma_run(p, "axx", 3) == -1);
}

void test_or_different_string_lengths() {
	cma_op *p = O(S("long"), S("lo"), S("l"));
	assert(cma_run(p, "longword", 8) == 4);  // first match wins
	assert(cma_run(p, "lo", 2) == 2);
	assert(cma_run(p, "l", 1) == 1);
}

void test_or_same_start_different_end() {
	cma_op *p = O(S("hello"), S("help"));
	assert(cma_run(p, "hello", 5) == 5);
	assert(cma_run(p, "help", 4) == 4);  // first fails, second succeeds
}

// ================================================================
// String with repetition
// ================================================================

void test_rep_string_pattern() {
	cma_op *p = REP(2, CMA_INF, S("ab"));
	assert(cma_run(p, "ababab", 6) == 6);
	assert(cma_run(p, "ab", 2) == -1);
}

void test_rep_string_exact() {
	cma_op *p = REP(2, 2, S("x"));
	assert(cma_run(p, "xx", 2) == 2);
	assert(cma_run(p, "xxx", 3) == 2);
}

void test_rep_set_many_digits() {
	cma_op *p = MIN(3, R("0-9"));
	assert(cma_run(p, "123456", 6) == 6);
	assert(cma_run(p, "12", 2) == -1);
}

void test_rep_set_range_limited() {
	cma_op *p = MAX(3, R("a-z"));
	assert(cma_run(p, "abcde", 5) == 3);
	assert(cma_run(p, "ab", 2) == 2);
}

void test_rep_set_exact_count() {
	cma_op *p = REP(4, 4, R("a-z"));
	assert(cma_run(p, "abcd", 4) == 4);
	assert(cma_run(p, "abc", 3) == -1);
}

// ================================================================
// String ending with ENDL
// ================================================================

void test_str_then_endl() {
	cma_op *p = A(S("test"), ENDL());
	assert(cma_run(p, "test", 4) == 4);
	assert(cma_run(p, "test2", 5) == -1);
}

void test_multiple_strs_then_endl() {
	cma_op *p = A(S("a"), S("b"), S("c"), ENDL());
	assert(cma_run(p, "abc", 3) == 3);
	assert(cma_run(p, "abcd", 4) == -1);
}

void test_set_rep_endl() {
	cma_op *p = A(MIN(1, R("0-9")), ENDL());
	assert(cma_run(p, "123", 3) == 3);
	assert(cma_run(p, "123x", 4) == -1);
}

// ================================================================
// Complex NOT and AHEAD with strings
// ================================================================

void test_not_string() {
	cma_op *p = A(NOT(S("bad")), R("a-z"));
	assert(cma_run(p, "good", 4) == 1);
	assert(cma_run(p, "bad", 3) == -1);
}

void test_ahead_string() {
	cma_op *p = A(AHEAD(S("start")), S("start"));
	assert(cma_run(p, "startmiddle", 11) == 5);
}

void test_not_ahead_combination() {
	cma_op *p = A(AHEAD(S("ok")), NOT(S("bad")), S("ok"));
	assert(cma_run(p, "ok", 2) == 2);
}

// ================================================================
// Main
// ================================================================

int main() {
	printf("CMA string matching tests:\n\n");

	printf("Advanced string tests:\n");
	TEST(test_str_same_length_wrong_bytes);
	TEST(test_str_u32_fast_reject);
	TEST(test_str_u32_pass_memcmp_fail);
	TEST(test_str_single_byte_miss);
	TEST(test_str_five_bytes);
	TEST(test_str_long_pattern);
	TEST(test_str_repeated_substring);
	TEST(test_str_pattern_in_middle);
	TEST(test_str_multiple_occurrences);
	TEST(test_str_special_chars);
	TEST(test_str_whitespace);
	TEST(test_str_digits_in_string);

	printf("\nAdvanced set tests:\n");
	TEST(test_set_range_boundaries);
	TEST(test_set_reversed_range);
	TEST(test_set_multiple_ranges);
	TEST(test_set_literal_and_range);
	TEST(test_set_ascii_range_hex);
	TEST(test_set_boundary_byte);
	TEST(test_set_empty_input);

	printf("\nString + set combinations:\n");
	TEST(test_str_then_set);
	TEST(test_set_then_str);
	TEST(test_str_set_str_sequence);
	TEST(test_or_different_string_lengths);
	TEST(test_or_same_start_different_end);

	printf("\nString with repetition:\n");
	TEST(test_rep_string_pattern);
	TEST(test_rep_string_exact);
	TEST(test_rep_set_many_digits);
	TEST(test_rep_set_range_limited);
	TEST(test_rep_set_exact_count);

	printf("\nString with ENDL:\n");
	TEST(test_str_then_endl);
	TEST(test_multiple_strs_then_endl);
	TEST(test_set_rep_endl);

	printf("\nComplex NOT/AHEAD with strings:\n");
	TEST(test_not_string);
	TEST(test_ahead_string);
	TEST(test_not_ahead_combination);

	printf("\n✓ All string tests passed!\n");
	return 0;
}
