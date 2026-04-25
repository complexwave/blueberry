/*
 * tgmemlib_strategy_pool.c — virtual reserve + on-demand physical commit.
 * Included directly by tgmemlib.c; not compiled separately.
 *
 * Init:    reserve pool_size bytes as PROT_NONE — no physical pages yet.
 * Alloc:   MAP_FIXED PROT_RW over the chosen slot — OS commits physical pages
 *          on first write (demand paging).
 * Free:    MAP_FIXED PROT_NONE — releases physical pages back to OS, keeps
 *          the virtual slot in a separate free stack for reuse.
 * Destroy: single munmap of the entire reservation.
 *
 * Free stack is a separate array (cannot store intrusive pointers in
 * PROT_NONE memory).  Capacity = pool_size / ARENA_SIZE (~16K for 1 GiB).
 */

typedef struct {
	char   *mmap_base;   /* raw mmap base (may be unaligned) */
	size_t  mmap_size;   /* total reserved bytes (for destroy) */
	char   *bump;        /* next untouched ARENA_SIZE-aligned slot */
	char   *pool_end;    /* one past last usable byte */
	void  **free_stack;  /* stack of decommitted-but-reserved slots */
	int     free_top;    /* number of entries in free_stack */
} tg_pool_ctx_t;

static void *pool_arena_mmap(void *ctx, uint8_t tag) {
	(void)tag;
	tg_pool_ctx_t *pc = (tg_pool_ctx_t *)ctx;
	char *addr;
	if (pc->free_top > 0) {
		addr = (char *)pc->free_stack[--pc->free_top];
	} else {
		if (pc->bump + ARENA_SIZE > pc->pool_end) return NULL;
		addr     = pc->bump;
		pc->bump += ARENA_SIZE;
	}
	/* commit physical pages for this slot */
	void *p = mmap(addr, ARENA_SIZE, PROT_READ | PROT_WRITE,
	               MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED) return NULL;
	return p;
}

static void pool_arena_munmap(void *ctx, void *ptr) {
	tg_pool_ctx_t *pc = (tg_pool_ctx_t *)ctx;
	/* decommit: revert to PROT_NONE, releasing physical pages to OS */
	mmap(ptr, ARENA_SIZE, PROT_NONE,
	     MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	pc->free_stack[pc->free_top++] = ptr;
}

static void pool_destroy(void *ctx) {
	tg_pool_ctx_t *pc = (tg_pool_ctx_t *)ctx;
	munmap(pc->mmap_base, pc->mmap_size);
	free(pc->free_stack);
	free(pc);
}

tg_mmap_strategy_t tg_strategy_pool(size_t pool_size) {
	if (pool_size == 0) pool_size = 1024ULL * 1024 * 1024; /* 1 GiB default */

	/* round up to ARENA_SIZE multiple */
	pool_size = (pool_size + ARENA_SIZE - 1) & ~(size_t)(ARENA_SIZE - 1);

	/* reserve pool_size + ARENA_SIZE to guarantee ARENA_SIZE alignment */
	size_t mmap_size = pool_size + ARENA_SIZE;
	void *raw = mmap(NULL, mmap_size, PROT_NONE,
	                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (raw == MAP_FAILED) return tg_strategy_simple();

	uintptr_t aligned = ((uintptr_t)raw + ARENA_SIZE - 1) & ARENA_MASK;

	int max_arenas = (int)(pool_size / ARENA_SIZE);
	void **free_stack = malloc((size_t)max_arenas * sizeof(void *));
	if (!free_stack) { munmap(raw, mmap_size); return tg_strategy_simple(); }

	tg_pool_ctx_t *pc = calloc(1, sizeof(*pc));
	if (!pc) { free(free_stack); munmap(raw, mmap_size); return tg_strategy_simple(); }

	pc->mmap_base  = raw;
	pc->mmap_size  = mmap_size;
	pc->bump       = (char *)aligned;
	pc->pool_end   = (char *)aligned + pool_size;
	pc->free_stack = free_stack;
	pc->free_top   = 0;

	return (tg_mmap_strategy_t){
		.arena_mmap   = pool_arena_mmap,
		.arena_munmap = pool_arena_munmap,
		.destroy      = pool_destroy,
		.ctx          = pc,
	};
}
