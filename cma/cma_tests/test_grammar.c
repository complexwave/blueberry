#include "../cma.h"
#include <stdio.h>
#include <assert.h>

#define TEST(name) printf("  %-40s ", #name); fflush(stdout); name(); printf("OK\n");

// ================================================================
// Recursive grammars - requires REF for forward references
// ================================================================

void test_recursive_parens_base() {
	// expr = "(" expr ")" | digit
	// Match: digit
	cma_op *expr = NULL;
	cma_op *atom  = R("0-9");
	cma_op *paren = A(S("("), REF(&expr), S(")"));
	expr = O(paren, atom);

	assert(cma_run(expr, "3", 1) == 1);
}

void test_recursive_parens_one_level() {
	// expr = "(" expr ")" | digit
	// Match: (digit)
	cma_op *expr = NULL;
	cma_op *atom  = R("0-9");
	cma_op *paren = A(S("("), REF(&expr), S(")"));
	expr = O(paren, atom);

	assert(cma_run(expr, "(3)", 3) == 3);
}

void test_recursive_parens_two_levels() {
	// expr = "(" expr ")" | digit
	// Match: ((digit))
	cma_op *expr = NULL;
	cma_op *atom  = R("0-9");
	cma_op *paren = A(S("("), REF(&expr), S(")"));
	expr = O(paren, atom);

	assert(cma_run(expr, "((3))", 5) == 5);
}

void test_recursive_parens_three_levels() {
	cma_op *expr = NULL;
	cma_op *atom  = R("0-9");
	cma_op *paren = A(S("("), REF(&expr), S(")"));
	expr = O(paren, atom);

	assert(cma_run(expr, "(((7)))", 7) == 7);
}

void test_recursive_parens_unmatched() {
	cma_op *expr = NULL;
	cma_op *atom  = R("0-9");
	cma_op *paren = A(S("("), REF(&expr), S(")"));
	expr = O(paren, atom);

	assert(cma_run(expr, "(((", 3) == -1);
}

void test_recursive_parens_extra_close() {
	cma_op *expr = NULL;
	cma_op *atom  = R("0-9");
	cma_op *paren = A(S("("), REF(&expr), S(")"));
	expr = O(paren, atom);

	assert(cma_run(expr, "(3))", 4) == 3);  // matches (3), rest unconsumed
}

// ================================================================
// Mutual recursion - two grammars that reference each other
// ================================================================

void test_mutual_recursion_base() {
	// a = "x" b | "x"
	// b = "y" a
	cma_op *a = NULL, *b = NULL;
	a = O(A(S("x"), REF(&b)), S("x"));
	b = A(S("y"), REF(&a));

	assert(cma_run(a, "x", 1) == 1);
}

void test_mutual_recursion_one_level() {
	// a = "x" b | "x"
	// b = "y" a
	// Match: xyx
	cma_op *a = NULL, *b = NULL;
	a = O(A(S("x"), REF(&b)), S("x"));
	b = A(S("y"), REF(&a));

	assert(cma_run(a, "xyx", 3) == 3);
}

void test_mutual_recursion_two_levels() {
	cma_op *a = NULL, *b = NULL;
	a = O(A(S("x"), REF(&b)), S("x"));
	b = A(S("y"), REF(&a));

	assert(cma_run(a, "xyxyx", 5) == 5);
}

void test_mutual_recursion_b_base() {
	cma_op *a = NULL, *b = NULL;
	a = O(A(S("x"), REF(&b)), S("x"));
	b = A(S("y"), REF(&a));

	// Start with b: "yx"
	assert(cma_run(b, "yx", 2) == 2);
}

// ================================================================
// More complex recursive patterns
// ================================================================

void test_recursive_sequence() {
	// s = "a" | "a" s
	// This matches one or more 'a's
	cma_op *s = NULL;
	cma_op *single = S("a");
	cma_op *sequence = A(S("a"), REF(&s));
	s = O(sequence, single);

	assert(cma_run(s, "a", 1) == 1);
	assert(cma_run(s, "aaa", 3) == 3);
}

void test_recursive_alternation() {
	// n = "0" | "1" | ... | "9" | "(" n ")"
	cma_op *n = NULL;
	cma_op *digit = R("0-9");
	cma_op *parens = A(S("("), REF(&n), S(")"));
	n = O(parens, digit);

	assert(cma_run(n, "5", 1) == 1);
	assert(cma_run(n, "(7)", 3) == 3);
}

void test_recursive_optional() {
	// e = "x" | "x" "(" e ")"
	cma_op *e = NULL;
	cma_op *simple = S("x");
	cma_op *complex = A(S("x"), S("("), REF(&e), S(")"));
	e = O(complex, simple);

	assert(cma_run(e, "x", 1) == 1);
	assert(cma_run(e, "x(x)", 4) == 4);
	assert(cma_run(e, "x(x(x))", 7) == 7);
}

void test_recursive_prefix_suffix() {
	// p = "a" | "a" p "a"
	// This matches patterns like "a", "aaa" (a+a+a), "aaaaa" (a+aaa+a)
	// but due to alternation trying wrap first, "aaaaa" matches as "a"+p("aa")+"a"
	// where p("aa") matches "a" (base), leaving one "a" unconsumed, so returns 3
	cma_op *p = NULL;
	cma_op *base = S("a");
	cma_op *wrap = A(S("a"), REF(&p), S("a"));
	p = O(wrap, base);

	assert(cma_run(p, "a", 1) == 1);
	assert(cma_run(p, "aaa", 3) == 3);
	assert(cma_run(p, "aaaaa", 5) == 3);  // matches first 3: a + p("aa") + a
}

// ================================================================
// Grammar with lists/repetition
// ================================================================

void test_grammar_digit_list() {
	// list = digit ("," digit)*
	cma_op *digit = R("0-9");
	cma_op *comma_digit = A(S(","), digit);
	cma_op *list = A(digit, REP(0, CMA_INF, comma_digit));

	assert(cma_run(list, "5", 1) == 1);
	assert(cma_run(list, "1,2,3", 5) == 5);
}

void test_grammar_word_sequence() {
	// word = [a-z]+
	// seq = word (" " word)*
	cma_op *word = MIN(1, R("a-z"));
	cma_op *space_word = A(S(" "), word);
	cma_op *seq = A(word, REP(0, CMA_INF, space_word));

	assert(cma_run(seq, "hello", 5) == 5);
	assert(cma_run(seq, "hello world test", 16) == 16);
}

void test_grammar_balanced_braces() {
	// braces = "{" braces "}" | "{"
	// Note: due to ordered choice, "{" is tried first in pair, so matches just "{"
	cma_op *braces = NULL;
	cma_op *open = S("{");
	cma_op *pair = A(S("{"), REF(&braces), S("}"));
	braces = O(pair, open);

	assert(cma_run(braces, "{", 1) == 1);  // matches "{"
	assert(cma_run(braces, "{}", 2) == 1);  // matches first "{", pair fails
	assert(cma_run(braces, "{{}}", 4) == 3);  // matches first "{", then pair on "{}" matches inner "{", then "}"
}

// ================================================================
// Complex grammar structures
// ================================================================

void test_grammar_expression_like() {
	// Simple arithmetic-like: term = "(" expr ")" | number
	// This shows nested precedence
	cma_op *expr = NULL;
	cma_op *number = MIN(1, R("0-9"));
	cma_op *paren_expr = A(S("("), REF(&expr), S(")"));
	expr = O(paren_expr, number);

	assert(cma_run(expr, "42", 2) == 2);
	assert(cma_run(expr, "(123)", 5) == 5);
}

void test_grammar_nested_calls() {
	// a = REF b
	// b = REF c
	// c = "x"
	cma_op *c = S("x");
	cma_op *b = REF(&c);
	cma_op *a = REF(&b);

	assert(cma_run(a, "x", 1) == 1);
}

void test_grammar_with_repetition_inside() {
	// expr = "[" items "]"
	// items = item | item "," items
	cma_op *item = R("a-z");
	cma_op *items = A(item, REP(0, CMA_INF, A(S(","), item)));
	cma_op *expr = A(S("["), items, S("]"));

	assert(cma_run(expr, "[a]", 3) == 3);
	assert(cma_run(expr, "[a,b,c]", 7) == 7);
}

void test_grammar_alternation_with_recursion() {
	// This should match either a number or a parenthesized expression
	cma_op *e = NULL;
	cma_op *num = MIN(1, R("0-9"));
	cma_op *paren = A(S("("), REF(&e), S(")"));
	e = O(paren, num);

	assert(cma_run(e, "7", 1) == 1);
	assert(cma_run(e, "(99)", 4) == 4);
	assert(cma_run(e, "((8))", 5) == 5);
}

// ================================================================
// Main
// ================================================================

int main() {
	printf("CMA grammar tests:\n\n");

	printf("Recursive parentheses:\n");
	TEST(test_recursive_parens_base);
	TEST(test_recursive_parens_one_level);
	TEST(test_recursive_parens_two_levels);
	TEST(test_recursive_parens_three_levels);
	TEST(test_recursive_parens_unmatched);
	TEST(test_recursive_parens_extra_close);

	printf("\nMutual recursion:\n");
	TEST(test_mutual_recursion_base);
	TEST(test_mutual_recursion_one_level);
	TEST(test_mutual_recursion_two_levels);
	TEST(test_mutual_recursion_b_base);

	printf("\nMore recursive patterns:\n");
	TEST(test_recursive_sequence);
	TEST(test_recursive_alternation);
	TEST(test_recursive_optional);
	TEST(test_recursive_prefix_suffix);

	printf("\nGrammar with lists:\n");
	TEST(test_grammar_digit_list);
	TEST(test_grammar_word_sequence);
	TEST(test_grammar_balanced_braces);

	printf("\nComplex grammar structures:\n");
	TEST(test_grammar_expression_like);
	TEST(test_grammar_nested_calls);
	TEST(test_grammar_with_repetition_inside);
	TEST(test_grammar_alternation_with_recursion);

	printf("\n✓ All grammar tests passed!\n");
	return 0;
}
