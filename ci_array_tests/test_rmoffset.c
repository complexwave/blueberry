/* test_rmoffset.c — ci_arr_rmoffset: linearization correctness at various offsets */
#include "ciobj.c"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

static void setup(void)    { ci_init(); ci_str_register(); ci_arr_register(); }
static void teardown(void) { ci_shutdown(); }

#define N 1024

/* Deterministic value for logical index i — Knuth multiplicative hash.
 * Avoids trivial patterns like PTR(i%32) that wouldn't catch transposition. */
static ci_ptr elem_val(uint32_t i) {
	return (ci_ptr)(uintptr_t)(((uint64_t)i + 1) * 2654435761u);
}

/*
 * check_rmoffset: allocate a fresh 1024-element array, manually set offset,
 * fill all 1024 physical slots so logical element i = elem_val(i),
 * call ci_arr_rmoffset into a separate dst buffer, verify dst[i] == elem_val(i).
 */
static void check_rmoffset(uint32_t offset) {
	ci_array *a = ci_arr_new(N);
	assert(a != NULL);
	assert(ci_arr_size(a) == N);

	/* manually set offset and length — bypass push/pop */
	a->offset = offset;
	a->length = N;

	/* fill logical slots via ci_arr_set */
	for (uint32_t i = 0; i < N; i++) {
		ci_arr_set(a, i, elem_val(i));
	}

	/* verify logical access before linearization */
	for (uint32_t i = 0; i < N; i++) {
		assert(ci_arr_index(a, i) == elem_val(i));
	}

	/* linearize into dst */
	ci_ptr *dst = malloc(N * sizeof(ci_ptr));
	assert(dst != NULL);

	ci_arr_rmoffset(a, dst);

	/* verify linearized order */
	for (uint32_t i = 0; i < N; i++) {
		assert(dst[i] == elem_val(i));
	}

	free(dst);
	/* restore length to 0 so destructor doesn't trip */
	a->length = 0;
	a->offset = 0;
	ci_free(a);

	printf("  offset=%-4u  OK\n", offset);
}

int main(void) {
	setup();

	printf("test_rmoffset: checking offsets...\n");
	check_rmoffset(0);    /* no wrap — single memmove path */
	check_rmoffset(1);    /* minimal wrap */
	check_rmoffset(3);    /* small wrap */
	check_rmoffset(512);  /* exactly half */
	check_rmoffset(1023); /* maximal wrap: seg1=1, seg2=1023 */

	teardown();
	printf("test_rmoffset: PASSED\n");
	return 0;
}
