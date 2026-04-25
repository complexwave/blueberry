/*
 * tgmemlib_strategy_probe.c — probe-based address-tagged mmap strategy.
 * Included directly by tgmemlib.c; not compiled separately.
 * Only compiled when TG_ADDR_TAG is set.
 *
 * Alloc+free a probe page to learn where the OS likes to give memory, then
 * mmap arenas at computed addresses so the type tag is encoded in the pointer
 * (bits [ARENA_SHIFT + TAG_BITS - 1 : ARENA_SHIFT]).  tg_ptr_tag() becomes a
 * pure bitop — no memory access.
 *
 * Layout: base + stripe * STRIPE_SIZE + tag * ARENA_SIZE
 * Falls back to tg_strategy_simple() if probe or test-page fails.
 */

typedef struct {
	uintptr_t base;
	int       stripe_next[PTR_MAX_TYPES];
	int       max_stripes;
} tg_probe_ctx_t;

static void *probe_arena_mmap(void *ctx, uint8_t tag) {
	tg_probe_ctx_t *pc = (tg_probe_ctx_t *)ctx;
	int stripe = pc->stripe_next[tag];

	while (stripe < pc->max_stripes) {
		uintptr_t addr = pc->base
			+ (uintptr_t)stripe * STRIPE_SIZE
			+ (uintptr_t)tag * ARENA_SIZE;

		pc->stripe_next[tag] = stripe + 1;

#ifdef MAP_FIXED_NOREPLACE
		void *p = mmap((void *)addr, ARENA_SIZE, PROT_READ | PROT_WRITE,
		               MAP_FIXED_NOREPLACE | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (p == (void *)addr)
			return p;
		/* EEXIST — something already there, try next stripe */
#else
		void *p = mmap((void *)addr, ARENA_SIZE, PROT_READ | PROT_WRITE,
		               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (p == (void *)addr)
			return p;
		if (p != MAP_FAILED)
			munmap(p, ARENA_SIZE);
#endif
		stripe = pc->stripe_next[tag];
	}
	return NULL;
}

static void probe_arena_munmap(void *ctx, void *ptr) {
	(void)ctx;
	munmap(ptr, ARENA_SIZE);
}

static void probe_destroy(void *ctx) {
	free(ctx);
}

tg_mmap_strategy_t tg_strategy_probe(void) {
	void *probe = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
	                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (probe == MAP_FAILED)
		return tg_strategy_simple();

	uintptr_t hint = (uintptr_t)probe;
	munmap(probe, 4096);

	/* align up to stripe boundary, skip one stripe past the probe region */
	uintptr_t base = ((hint + STRIPE_SIZE - 1) & ~((uintptr_t)STRIPE_SIZE - 1))
	                 + STRIPE_SIZE;

	/* verify the OS will honor a fixed address in this neighborhood */
	void *test = mmap((void *)base, 4096, PROT_READ | PROT_WRITE,
	                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (test != (void *)base) {
		if (test != MAP_FAILED) munmap(test, 4096);
		return tg_strategy_simple();
	}
	munmap(test, 4096);

	tg_probe_ctx_t *pc = calloc(1, sizeof(*pc));
	if (!pc) return tg_strategy_simple();

	pc->base        = base;
	pc->max_stripes = 256; /* ~1 GiB range */

	return (tg_mmap_strategy_t){
		.arena_mmap   = probe_arena_mmap,
		.arena_munmap = probe_arena_munmap,
		.destroy      = probe_destroy,
		.ctx          = pc,
	};
}
