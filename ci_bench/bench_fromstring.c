/*
 * bench_fromstring.c — benchmark ci_number_fromstring with real allocation.
 */
#define CI_STRING_TEST
#define CI_NUMBER_ALWAYS_BOX
#include "ciobj.c"

#include <stdio.h>
#include <time.h>

#define ITERS 100000000

static inline double elapsed_ns(struct timespec *a, struct timespec *b) {
	return (b->tv_sec - a->tv_sec) * 1e9 + (b->tv_nsec - a->tv_nsec);
}

#define DONT_OPTIMIZE(val) __asm__ volatile("" : "+r"(val))

int main(void) {
	ci_init();
	ci_str_register();
	ci_arr_register();
	ci_map_register();
	ci_number_register();

	volatile uint8_t small_buf[] = "123456";
	size_t small_len = 6;

	volatile uint8_t huge_buf[] = "99999999999999999999999999999999999999";
	size_t huge_len = 38;

	struct timespec t0, t1;

	/* bench small (6 digits) */
	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (int i = 0; i < ITERS; i++) {
		small_buf[3] = '0' + (i % 10);
		ci_ptr r = ci_number_fromstring((const uint8_t *)small_buf, small_len);
		ci_dec(r);
	}
	clock_gettime(CLOCK_MONOTONIC, &t1);

	double ns_small = elapsed_ns(&t0, &t1);
	printf("small (6 digits):  %5.1f ns/op   %.3f s / %dM iters\n",
		ns_small / ITERS, ns_small / 1e9, ITERS / 1000000);

	/* bench huge (38 digits) */
	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (int i = 0; i < ITERS; i++) {
		huge_buf[20] = '0' + (i % 10);
		ci_ptr r = ci_number_fromstring((const uint8_t *)huge_buf, huge_len);
		ci_dec(r);
	}
	clock_gettime(CLOCK_MONOTONIC, &t1);

	double ns_huge = elapsed_ns(&t0, &t1);
	printf("huge (38 digits):  %5.1f ns/op   %.3f s / %dM iters\n",
		ns_huge / ITERS, ns_huge / 1e9, ITERS / 1000000);

	ci_shutdown();
	return 0;
}
