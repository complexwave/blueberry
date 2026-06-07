#define CI_STRING_TEST
#include "ciobj.c"

#include <stdio.h>
#include <time.h>
#include <stdlib.h>

/*
 * Branchless refcount inc/dec using a global sentinel.
 *
 * ci_inc: sentinel absorbs harmless increments (refcnt wraps, who cares).
 * ci_dec: sentinel is saturated (0xFFFF) so it hits the sticky check
 *         and falls through — no free, no decrement.
 */

static ci_gchdr ci_sentinel_hdr = { .refcnt = 0x0FFF, .flags = 0 };

/*
 * The idea: no tag check at all. Every GC object has a refcnt.
 * Non-refcountable objects get refcnt=0xFFFF (saturated) at birth.
 * Saturated refcnt: inc wraps to 0 then gets or'd back to 0xFFFF.
 *                   dec sees 0xFFFF and skips.
 * Only non-pointers (ints, bools, NULL) need the sentinel redirect.
 */

#define REFCOUNTABLE_TAG CI_TAG_MASK(CI_REFCOUNTABLE)

static inline ci_gchdr *ci_select_hdr(void *ptr) {
	uintptr_t p = (uintptr_t)ptr;
	uintptr_t is_int = !!((p & (REFCOUNTABLE_TAG | 0x01)) != REFCOUNTABLE_TAG);
	ci_gchdr stk_sentinel = { .refcnt = 0xFFFF, .flags = 0 };
	ci_gchdr *hdr = (ci_gchdr *)(is_int ? (uintptr_t)&stk_sentinel : p);

	return hdr;
}

static inline void ci_inc_branchless(void *ptr) {
	ci_gchdr *hdr = ci_select_hdr(ptr);

	uint16_t rc = hdr->refcnt + 1;
	rc |= -(uint16_t)(rc == 0);   /* 0xFFFF+1=0 → back to 0xFFFF */
	hdr->refcnt = rc;
}

static inline void ci_inc_asm(void *ptr) {
	__asm__ __volatile__ (
		"movl $0, (%%rsp)\n\t"           /* 4-byte write to prevent store-forward stall */
		"lea (%%rsp), %%rcx\n\t"         /* rcx = stack scratch (value irrelevant for inc) */
		"mov %[ptr], %%rax\n\t"
		"and $0x20001, %%eax\n\t"
		"cmp $0x20000, %%eax\n\t"
		"cmoveq %[ptr], %%rcx\n\t"
		"movzwl (%%rcx), %%eax\n\t"
		"addw $1, %%ax\n\t"
		"sbbw $0, %%ax\n\t"
		"movw %%ax, (%%rcx)\n\t"
		:
		: [ptr] "r" ((uintptr_t)ptr)
		: "rax", "rcx", "cc", "memory"
	);
}

#define MIN_SATURATED (0xFFFF-1)

static inline int ci_dec_branchless(void *ptr) {
	ci_gchdr stk = { .refcnt = 0xFFFF, .flags = 0 };
	ci_gchdr *hdr;
	uint16_t rc;
	__asm__ __volatile__ (
		"lea %[sentinel], %[hdr]\n\t"
		"mov %[ptr], %%rax\n\t"
		"and $0x20001, %%eax\n\t"
		"cmp $0x20000, %%eax\n\t"
		"cmoveq %[ptr], %[hdr]\n\t"
		"movzwl (%[hdr]), %%eax\n\t"
		"xor %%edx, %%edx\n\t"
		"cmp $0xFFFF, %%ax\n\t"
		"setne %%dl\n\t"
		"sub %%edx, %%eax\n\t"
		"movw %%ax, (%[hdr])\n\t"
		: [hdr] "=&r" (hdr), [rc] "=a" (rc)
		: [ptr] "r" ((uintptr_t)ptr),
		  [sentinel] "m" (stk)
		: "rdx", "cc", "memory"
	);
	if (__builtin_expect(rc == 0, 0)) {
		tg_free(ptr);
		return 1;
	}
	return 0;
}

#define N_STRINGS  10000
#define N_INTS      5000
static inline int ci_dec_asm(void *ptr) {
	ci_gchdr stk = { .refcnt = 0xFFFF, .flags = 0 };
	ci_gchdr *hdr;
	uint16_t rc;
	__asm__ __volatile__ (
		"lea %[sentinel], %[hdr]\n\t"
		"mov %[ptr], %%rax\n\t"
		"and $0x20001, %%eax\n\t"
		"cmp $0x20000, %%eax\n\t"
		"cmoveq %[ptr], %[hdr]\n\t"
		"movzwl (%[hdr]), %%eax\n\t"
		"xor %%edx, %%edx\n\t"
		"cmp $0xFFFF, %%ax\n\t"
		"setne %%dl\n\t"
		"sub %%edx, %%eax\n\t"
		"movw %%ax, (%[hdr])\n\t"
		: [hdr] "=&r" (hdr), [rc] "=a" (rc)
		: [ptr] "r" ((uintptr_t)ptr),
		  [sentinel] "m" (stk)
		: "rdx", "cc", "memory"
	);
	if (__builtin_expect(rc == 0, 0)) {
		tg_free(ptr);
		return 1;
	}
	return 0;
}

#define ARRAY_SIZE 100000
#define ITERS      100000000

static double elapsed(struct timespec *a, struct timespec *b) {
	return (b->tv_sec - a->tv_sec) + (b->tv_nsec - a->tv_nsec) * 1e-9;
}

static void build_array(ci_ptr *rc_pool, int n_rc,
                         ci_ptr *norc_pool, int n_norc,
                         ci_ptr *arr, int arr_size,
                         int rc_pct, uint32_t seed)
{
	uint32_t rng = seed;
	for (int i = 0; i < arr_size; i++) {
		rng ^= rng << 13;
		rng ^= rng >> 17;
		rng ^= rng << 5;

		int use_rc = (int)(rng % 100) < rc_pct;
		rng ^= rng << 13;
		rng ^= rng >> 17;
		rng ^= rng << 5;

		if (use_rc)
			arr[i] = rc_pool[rng % n_rc];
		else
			arr[i] = norc_pool[rng % n_norc];
	}
}

/* 10x unrolled to kill loop overhead */
#define INC10(fn, p) fn(p[0]);fn(p[1]);fn(p[2]);fn(p[3]);fn(p[4]);\
                     fn(p[5]);fn(p[6]);fn(p[7]);fn(p[8]);fn(p[9])

#define DEC10(fn, p) fn(p[0]);fn(p[1]);fn(p[2]);fn(p[3]);fn(p[4]);\
                     fn(p[5]);fn(p[6]);fn(p[7]);fn(p[8]);fn(p[9])

static void run_bench(ci_ptr *arr, int arr_size, int total_iters, const char *label) {
	int rc_count = 0;
	for (int i = 0; i < arr_size; i++)
		if (CI_IS_REFCOUNTABLE(arr[i])) rc_count++;

	printf("=== %s (%d%% refcountable, %d%% not) ===\n",
		label, rc_count * 100 / arr_size,
		(arr_size - rc_count) * 100 / arr_size);

	int loops = total_iters / arr_size;
	int total = loops * arr_size;

	int iter, i;

	/* --- ci_inc --- */

	{
		struct timespec t0, t1;
		clock_gettime(CLOCK_MONOTONIC, &t0);
		for (iter = 0; iter < loops; iter++)
			for (i = 0; i < arr_size; i += 10) {
				ci_ptr *p = arr+i;
				INC10(ci_inc, p);
			}
		clock_gettime(CLOCK_MONOTONIC, &t1);
		double s = elapsed(&t0, &t1);
		printf("  ci_inc (branched)      %.2f ns/op\n", s * 1e9 / total);
	}

	{
		struct timespec t0, t1;
		clock_gettime(CLOCK_MONOTONIC, &t0);
		for (iter = 0; iter < loops; iter++)
			for (i = 0; i < arr_size; i += 10) {
				ci_ptr *p = arr+i;
				INC10(ci_inc_branchless, p);
			}
		clock_gettime(CLOCK_MONOTONIC, &t1);
		double s = elapsed(&t0, &t1);
		printf("  ci_inc (branchless)    %.2f ns/op\n", s * 1e9 / total);
	}

	{
		struct timespec t0, t1;
		clock_gettime(CLOCK_MONOTONIC, &t0);
		for (iter = 0; iter < loops; iter++)
			for (i = 0; i < arr_size; i += 10) {
				ci_ptr *p = arr+i;
				INC10(ci_inc_asm, p);
			}
		clock_gettime(CLOCK_MONOTONIC, &t1);
		double s = elapsed(&t0, &t1);
		printf("  ci_inc (asm)           %.2f ns/op\n", s * 1e9 / total);
	}

	/* --- ci_dec --- */

	{
		struct timespec t0, t1;
		clock_gettime(CLOCK_MONOTONIC, &t0);
		for (iter = 0; iter < loops; iter++)
			for (i = 0; i < arr_size; i += 10) {
				ci_ptr *p = arr+i;
				DEC10(ci_dec, p);
			}
		clock_gettime(CLOCK_MONOTONIC, &t1);
		double s = elapsed(&t0, &t1);
		printf("  ci_dec (branched)      %.2f ns/op\n", s * 1e9 / total);
	}

	{
		struct timespec t0, t1;
		clock_gettime(CLOCK_MONOTONIC, &t0);
		for (iter = 0; iter < loops; iter++)
			for (i = 0; i < arr_size; i += 10) {
				ci_ptr *p = arr+i;
				DEC10(ci_dec_branchless, p);
			}
		clock_gettime(CLOCK_MONOTONIC, &t1);
		double s = elapsed(&t0, &t1);
		printf("  ci_dec (branchless)    %.2f ns/op\n", s * 1e9 / total);
	}

	{
		struct timespec t0, t1;
		clock_gettime(CLOCK_MONOTONIC, &t0);
		for (iter = 0; iter < loops; iter++)
			for (i = 0; i < arr_size; i += 10) {
				ci_ptr *p = arr+i;
				DEC10(ci_dec_asm, p);
			}
		clock_gettime(CLOCK_MONOTONIC, &t1);
		double s = elapsed(&t0, &t1);
		printf("  ci_dec (asm)           %.2f ns/op\n", s * 1e9 / total);
	}

	printf("\n");
}

int main(void) {
	ci_init();
	ci_str_register();

	/* --- build pools --- */

	ci_ptr rc_pool[N_STRINGS];
	ci_ptr norc_pool[N_INTS];
	int n_rc = 0, n_norc = 0;

	for (int i = 0; i < N_STRINGS; i++) {
		char buf[20];
		snprintf(buf, sizeof(buf), "str_%05d", i);
		ci_str *s = ci_str_from_cstr(buf);
		if (!s) { fprintf(stderr, "alloc fail at %d\n", i); return 1; }
		rc_pool[n_rc++] = (ci_ptr)s;
	}

	for (int i = 0; i < N_INTS; i++)
		norc_pool[n_norc++] = CI_PACKINT(i * 7 + 1);

	/* high refcnt but NOT saturated — real program behavior */
	for (int i = 0; i < n_rc; i++)
		((ci_gchdr *)rc_pool[i])->refcnt = 0x0FFF;

	ci_ptr *arr = malloc(ARRAY_SIZE * sizeof(ci_ptr));
	if (!arr) { perror("malloc"); return 1; }

	printf("%d calls per bench (%.0fM), 10x unrolled\n\n", ITERS, (double)ITERS / 1e6);

	/* table 1: ~33% refcountable */
	build_array(rc_pool, n_rc, norc_pool, n_norc, arr, ARRAY_SIZE, 33, 0xDEADBEEF);
	run_bench(arr, ARRAY_SIZE, ITERS, "33/67 mix");

	/* table 2: ~80% refcountable */
	build_array(rc_pool, n_rc, norc_pool, n_norc, arr, ARRAY_SIZE, 80, 0xCAFEBABE);
	run_bench(arr, ARRAY_SIZE, ITERS, "80/20 mix");

	free(arr);
	return 0;
}
