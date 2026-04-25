/*
 * tgmemlib_strategy_reserve.c — PROT_NONE reservation + MAP_FIXED commit,
 * with tag-encoded arena addresses.
 * Included directly by tgmemlib.c; not compiled separately.
 * Only compiled when TG_ADDR_TAG is set.
 *
 * Reserves (max_stripes + 1) stripes as PROT_NONE upfront, then commits each
 * arena with MAP_FIXED PROT_RW.  On free, reverts to PROT_NONE.
 *
 * Address layout (same as probe): base + stripe * STRIPE_SIZE + tag * ARENA_SIZE
 * This encodes the type tag in pointer bits [ARENA_SHIFT + TAG_BITS - 1 : ARENA_SHIFT].
 *
 * Note: stripe_next[tag] is monotonically increasing; freed arenas are
 * decommitted (PROT_NONE) but their stripe slots are not reclaimed.
 * Use tg_strategy_pool for high-churn workloads.
 */

typedef struct {
	void     *base;
	size_t    reserve_size;
	uintptr_t aligned;
	int       stripe_next[PTR_MAX_TYPES];
	int       max_stripes;
} tg_reserve_ctx_t;

static void *reserve_arena_mmap(void *ctx, uint8_t tag) {
	tg_reserve_ctx_t *rc = (tg_reserve_ctx_t *)ctx;
	int stripe = rc->stripe_next[tag];
	if (stripe >= rc->max_stripes)
		return NULL;

	uintptr_t addr = rc->aligned
		+ (uintptr_t)stripe * STRIPE_SIZE
		+ (uintptr_t)tag * ARENA_SIZE;

	rc->stripe_next[tag] = stripe + 1;

	void *p = mmap((void *)addr, ARENA_SIZE, PROT_READ | PROT_WRITE,
	               MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED) return NULL;
	return p;
}

static void reserve_arena_munmap(void *ctx, void *ptr) {
	(void)ctx;
	/* revert to PROT_NONE — keeps reservation, releases physical pages */
	mmap(ptr, ARENA_SIZE, PROT_NONE,
	     MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

static void reserve_destroy(void *ctx) {
	tg_reserve_ctx_t *rc = (tg_reserve_ctx_t *)ctx;
	munmap(rc->base, rc->reserve_size);
	free(rc);
}

tg_mmap_strategy_t tg_strategy_reserve(int max_stripes) {
	if (max_stripes <= 0) max_stripes = 64;

	/* extra stripe for alignment headroom */
	size_t reserve = (size_t)(max_stripes + 1) * STRIPE_SIZE;
	void *base = mmap(NULL, reserve, PROT_NONE,
	                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (base == MAP_FAILED)
		return tg_strategy_simple();

	uintptr_t aligned = ((uintptr_t)base + STRIPE_SIZE - 1)
	                    & ~((uintptr_t)STRIPE_SIZE - 1);

	tg_reserve_ctx_t *rc = calloc(1, sizeof(*rc));
	if (!rc) { munmap(base, reserve); return tg_strategy_simple(); }

	rc->base         = base;
	rc->reserve_size = reserve;
	rc->aligned      = aligned;
	rc->max_stripes  = max_stripes;

	return (tg_mmap_strategy_t){
		.arena_mmap   = reserve_arena_mmap,
		.arena_munmap = reserve_arena_munmap,
		.destroy      = reserve_destroy,
		.ctx          = rc,
	};
}
