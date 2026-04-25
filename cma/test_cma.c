#include "cma.h"
#include <stdio.h>
#include <assert.h>

#define TEST(name) printf("  %-30s ", #name); name(); printf("OK\n");

void test_str_match() {
    cma_op *p = S("hello");
    assert(cma_run(p, "hello world", 11) == 5);
    assert(cma_run(p, "hell", 4) == -1);
    assert(cma_run(p, "hello", 5) == 5);
}

void test_str_nomatch() {
    cma_op *p = S("abc");
    assert(cma_run(p, "xyz", 3) == -1);
    assert(cma_run(p, "ab", 2) == -1);
}

void test_set_single() {
    cma_op *p = R("abc");
    assert(cma_run(p, "a", 1) == 1);
    assert(cma_run(p, "b", 1) == 1);
    assert(cma_run(p, "c", 1) == 1);
    assert(cma_run(p, "d", 1) == -1);
}

void test_set_range() {
    cma_op *p = R("a-z");
    assert(cma_run(p, "m", 1) == 1);
    assert(cma_run(p, "A", 1) == -1);
    assert(cma_run(p, "0", 1) == -1);
}

void test_set_mixed() {
    cma_op *p = R("a-zA-Z_");
    assert(cma_run(p, "g", 1) == 1);
    assert(cma_run(p, "Z", 1) == 1);
    assert(cma_run(p, "_", 1) == 1);
    assert(cma_run(p, "5", 1) == -1);
}

void test_and() {
    // match "foo" then a digit
    cma_op *p = A(S("foo"), R("0-9"));
    assert(cma_run(p, "foo3bar", 7) == 4);
    assert(cma_run(p, "foobar", 6) == -1);
    assert(cma_run(p, "foo", 3) == -1);
}

void test_or() {
    cma_op *p = O(S("cat"), S("dog"), S("rat"));
    assert(cma_run(p, "cat!", 4) == 3);
    assert(cma_run(p, "dog!", 4) == 3);
    assert(cma_run(p, "rat!", 4) == 3);
    assert(cma_run(p, "bat!", 4) == -1);
}

void test_not() {
    // match a letter that is NOT followed by "xx"
    // NOT(S("xx")) succeeds if "xx" doesn't match at current pos (consumes nothing)
    // then R("a-z") consumes one letter
    cma_op *p = A(NOT(S("xx")), R("a-z"));
    assert(cma_run(p, "ab", 2) == 1);
    assert(cma_run(p, "xx", 2) == -1);
}

void test_rep_star() {
    // zero or more digits
    cma_op *p = REP(0, CMA_INF, R("0-9"));
    assert(cma_run(p, "123abc", 6) == 3);
    assert(cma_run(p, "abc", 3) == 0);  // zero matches is OK
}

void test_rep_plus() {
    // one or more digits
    cma_op *p = MIN(1, R("0-9"));
    assert(cma_run(p, "123abc", 6) == 3);
    assert(cma_run(p, "abc", 3) == -1);
}

void test_rep_exact() {
    // exactly 3 letters
    cma_op *p = REP(3, 3, R("a-z"));
    assert(cma_run(p, "abcdef", 6) == 3);
    assert(cma_run(p, "ab", 2) == -1);
}

void test_rep_max() {
    cma_op *p = MAX(2, R("a-z"));
    assert(cma_run(p, "xyz", 3) == 2);  // greedy, eats 2
    assert(cma_run(p, "x", 1) == 1);
    assert(cma_run(p, "123", 3) == 0);  // zero is fine
}

void test_combo_identifier() {
    // C identifier: [a-zA-Z_][a-zA-Z0-9_]*
    cma_op *head = R("a-zA-Z_");
    cma_op *tail = REP(0, CMA_INF, R("a-zA-Z0-9_"));
    cma_op *ident = A(head, tail);

    assert(cma_run(ident, "foo_bar123 ", 11) == 10);
    assert(cma_run(ident, "_x", 2) == 2);
    assert(cma_run(ident, "123", 3) == -1);
}

void test_combo_integer() {
    // optional sign, then digits
    cma_op *sign = MAX(1, R("+-"));
    cma_op *digits = MIN(1, R("0-9"));
    cma_op *integer = A(sign, digits);

    assert(cma_run(integer, "42", 2) == 2);
    assert(cma_run(integer, "-7x", 3) == 2);
    assert(cma_run(integer, "+123", 4) == 4);
    assert(cma_run(integer, "abc", 3) == -1);
}

void test_dump() {
    printf("\n--- dump test ---\n");
    cma_op *ident = A(R("a-zA-Z_"), REP(0, CMA_INF, R("a-zA-Z0-9_")));
    cma_dump(ident, 0);
    printf("--- end dump ---\n\n");
}

void test_cap_basic() {
    // capture an identifier
    cma_op *ident = CAP(R("a-zA-Z_"), REP(0, CMA_INF, R("a-zA-Z0-9_")));
    cma_state s;
    const char *input = "foo_bar rest";
    cma_init(&s, input, strlen(input));
    int r = cma_match(&s, ident);
	printf("%d\n", r);
    assert(r == 7);
    assert(cma_cap_count(&s) == 1);
    cma_capture *c = cma_cap_get(&s, 0);
    assert(cma_cap_len(c) == 7);
    assert(memcmp(cma_cap_start(c), "foo_bar", 7) == 0);
    assert(cma_cap_nested(c) == 0);
    cma_free(&s);
}

void test_cap_named() {
    cma_op *p = A(
        CAPn(MIN(1, R("a-zA-Z_")), "key"),
        S("="),
        CAPn(MIN(1, R("0-9")), "val")
    );
    cma_state s;
    const char *input = "count=42";
    cma_init(&s, input, strlen(input));
    int r = cma_match(&s, p);
    assert(r == 8);
    assert(cma_cap_count(&s) == 2);

    cma_capture *k = cma_cap_get(&s, 0);
    assert(cma_cap_len(k) == 5);
    assert(memcmp(cma_cap_start(k), "count", 5) == 0);
    assert(strcmp(cma_cap_name(k), "key") == 0);

    cma_capture *v = cma_cap_get(&s, 1);
    assert(cma_cap_len(v) == 2);
    assert(memcmp(cma_cap_start(v), "42", 2) == 0);
    assert(strcmp(cma_cap_name(v), "val") == 0);
    cma_free(&s);
}

void test_cap_nested() {
    // outer captures whole "key=val", inner captures key and val separately
    cma_op *inner = A(
        CAPn(MIN(1, R("a-zA-Z_")), "key"),
        S("="),
        CAPn(MIN(1, R("0-9")), "val")
    );
    cma_op *outer = CAPn(inner, "pair");

    cma_state s;
    const char *input = "count=42";
    cma_init(&s, input, strlen(input));
    int r = cma_match(&s, outer);
    assert(r == 8);
    assert(cma_cap_count(&s) == 3);

    // cap[0] = outer "pair"
    cma_capture *pair = cma_cap_get(&s, 0);
    assert(cma_cap_len(pair) == 8);
    assert(cma_cap_nested(pair) == 2);
    assert(strcmp(cma_cap_name(pair), "pair") == 0);

    // cap[1] = inner "key"
    cma_capture *k = cma_cap_get(&s, 1);
    assert(cma_cap_len(k) == 5);
    assert(cma_cap_nested(k) == 0);

    // cap[2] = inner "val"
    cma_capture *v = cma_cap_get(&s, 2);
    assert(cma_cap_len(v) == 2);
    assert(cma_cap_nested(v) == 0);

    printf("\n--- capture dump ---\n");
    cma_dump_captures(&s);
    printf("--- end capture dump ---\n\n");

    cma_free(&s);
}

void test_cap_backtrack() {
    // first alternative has a capture but fails, should not leave captures behind
    cma_op *p = O(
        A(CAPn(S("abc"), "wrong"), S("XXX")),   // will fail at XXX
        CAPn(S("abcdef"), "right")
    );
    cma_state s;
    const char *input = "abcdef";
    cma_init(&s, input, strlen(input));
    int r = cma_match(&s, p);
    assert(r == 6);
    assert(cma_cap_count(&s) == 1);
    cma_capture *c = cma_cap_get(&s, 0);
    assert(strcmp(cma_cap_name(c), "right") == 0);
    assert(cma_cap_len(c) == 6);
    cma_free(&s);
}

void test_cap_deep_nested() {
    // list: (item,item,item) where each item is captured, and the whole list is captured
    cma_op *ws = REP(0, CMA_INF, R(" "));
    cma_op *word = CAPn(MIN(1, R("a-zA-Z")), "item");
    cma_op *sep = A(ws, S(","), ws);
    cma_op *list = CAPn(
        A(S("("), word, REP(0, CMA_INF, A(sep, word)), S(")")),
        "list"
    );

    cma_state s;
    const char *input = "(foo,bar,baz)";
    cma_init(&s, input, strlen(input));
    int r = cma_match(&s, list);
    assert(r == 13);

    printf("\n--- deep nested capture dump ---\n");
    cma_dump_captures(&s);
    printf("--- end ---\n\n");

    // list capture has 3 nested item captures
    cma_capture *list_cap = cma_cap_get(&s, 0);
    assert(strcmp(cma_cap_name(list_cap), "list") == 0);
    assert(cma_cap_nested(list_cap) == 3);

    assert(cma_cap_count(&s) == 4); // 1 list + 3 items
    cma_free(&s);
}

int main() {
    printf("cma tests:\n");

    TEST(test_str_match);
    TEST(test_str_nomatch);
    TEST(test_set_single);
    TEST(test_set_range);
    TEST(test_set_mixed);
    TEST(test_and);
    TEST(test_or);
    TEST(test_not);
    TEST(test_rep_star);
    TEST(test_rep_plus);
    TEST(test_rep_exact);
    TEST(test_rep_max);
    TEST(test_combo_identifier);
    TEST(test_combo_integer);
    TEST(test_dump);
    TEST(test_cap_basic);
    TEST(test_cap_named);
    TEST(test_cap_nested);
    TEST(test_cap_backtrack);
    TEST(test_cap_deep_nested);

    printf("all passed!\n");
    return 0;
}
