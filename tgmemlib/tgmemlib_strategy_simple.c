/*
 * tgmemlib_strategy_simple.c — 2x-alloc-and-trim aligned mmap strategy.
 * Included directly by tgmemlib.c; not compiled separately.
 */

static void *simple_arena_mmap(void *ctx, uint8_t tag) {
	(void)ctx; (void)tag;
	void *p = mmap(NULL, ARENA_SIZE * 2,
	               PROT_READ | PROT_WRITE,
	               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED) return NULL;

	uintptr_t addr    = (uintptr_t)p;
	uintptr_t aligned = (addr + ARENA_SIZE - 1) & ARENA_MASK;
	size_t    prefix  = (size_t)(aligned - addr);
	size_t    suffix  = ARENA_SIZE - prefix;

	if (prefix > 0) munmap(p, prefix);
	if (suffix > 0) munmap((char *)aligned + ARENA_SIZE, suffix);

	return (void *)aligned;
}

static void simple_arena_munmap(void *ctx, void *ptr) {
	(void)ctx;
	munmap(ptr, ARENA_SIZE);
}

tg_mmap_strategy_t tg_strategy_simple(void) {
	return (tg_mmap_strategy_t){
		.arena_mmap   = simple_arena_mmap,
		.arena_munmap = simple_arena_munmap,
		.destroy      = NULL,
		.ctx          = NULL,
	};
}
