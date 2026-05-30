/*
 * blueberry_vm/lib/gc.c — GC tracking / instrumentation
 *
 * Script API:
 *   gc.trace_start()   — snapshot all live objects into a map
 *   gc.trace_print()   — print only NEW objects (not in snapshot) with refcounts
 *
 * Requires -DTGMEMLIB_TRACKING at compile time.
 */

#ifdef TGMEMLIB_TRACKING

static ci_map *gc_trace_snapshot;
static uint64_t gc_trace_alloc_base;
static uint64_t gc_trace_free_base;

/* callback: add each live object to the snapshot map */
static void gc_trace_snapshot_cb(void *obj, tg_arena_t *ar, void *ctx) {
	(void)ar;
	ci_map *snap = (ci_map *)ctx;
	ci_map_set(snap, (ci_ptr)obj, CI_BOOL(1));
}

static ci_ptr bb_gc_trace_start(bb_coro_arg *c, ci_ptr_arg self,
                                ci_ptr_arg a0, ci_ptr_arg a1, ci_ptr_arg a2) {
	/* free old snapshot if any */
	if (gc_trace_snapshot) {
		ci_free(gc_trace_snapshot);
		gc_trace_snapshot = NULL;
	}

	gc_trace_alloc_base = ci_alloc->track_alloc_total;
	gc_trace_free_base  = ci_alloc->track_free_total;

	/* snapshot all currently live objects */
	gc_trace_snapshot = ci_map_new(512);
	tg_debug_walk_all_live(ci_alloc, gc_trace_snapshot_cb, gc_trace_snapshot);

	return NULL;
}

static ci_ptr bb_gc_trace_print(bb_coro_arg *c, ci_ptr_arg self,
                                ci_ptr_arg a0, ci_ptr_arg a1, ci_ptr_arg a2) {
	uint64_t allocs = ci_alloc->track_alloc_total - gc_trace_alloc_base;
	uint64_t frees  = ci_alloc->track_free_total  - gc_trace_free_base;

	fprintf(stderr, "\n── gc.trace_print ──\n");
	fprintf(stderr, "  since trace_start:  alloc: %" PRIu64 "  free: %" PRIu64 "  delta: %" PRIu64 "\n\n",
	        allocs, frees, allocs - frees);

	int new_count = 0;

	for (int tag = 0; tag < MAX_TYPES; tag++) {
		if (!ci_alloc->heads[tag]) continue;

		int type_new = 0;
		int is_rc = (tag & CI_REFCOUNTABLE);

		/* first pass: count new objects for this type */
		for (tg_arena_t *ar = ci_alloc->heads[tag]; ar; ar = ar->next) {
			char *base = ARENA_DATA(ar);
			int touched = (int)(ar->bump - base) / ar->obj_size;

			for (int i = 0; i < touched; i++) {
				void *slot = base + (size_t)i * ar->obj_size;
				if (tg_debug_is_free(ar->freelist, slot)) continue;
				if (gc_trace_snapshot && ci_map_get(gc_trace_snapshot, (ci_ptr)slot)) continue;
				type_new++;
			}
		}

		if (type_new == 0) continue;

		char namebuf[40];
		tg_debug_fmt_type(namebuf, sizeof(namebuf), (uint16_t)tag);
		fprintf(stderr, "── %s  new: %d ──\n", namebuf, type_new);

		/* second pass: print them */
		for (tg_arena_t *ar = ci_alloc->heads[tag]; ar; ar = ar->next) {
			char *base = ARENA_DATA(ar);
			int touched = (int)(ar->bump - base) / ar->obj_size;

			for (int i = 0; i < touched; i++) {
				void *slot = base + (size_t)i * ar->obj_size;
				if (tg_debug_is_free(ar->freelist, slot)) continue;
				if (gc_trace_snapshot && ci_map_get(gc_trace_snapshot, (ci_ptr)slot)) continue;

				if (is_rc) {
					uint16_t rc = ((ci_gchdr *)slot)->refcnt;
					const char *status = "";
					if (rc == 0xFFFF) status = " (saturated)";
					else if (rc == 0) status = " ** RC=0 NOT FREED **";
					fprintf(stderr, "    %p  rc: %-5u%s\n", slot, rc, status);
				} else {
					fprintf(stderr, "    %p\n", slot);
				}
			}
		}

		new_count += type_new;
		fprintf(stderr, "\n");
	}

	fprintf(stderr, "  total new live objects: %d\n\n", new_count);
	return NULL;
}

#else /* !TGMEMLIB_TRACKING */

static ci_ptr bb_gc_trace_start(bb_coro_arg *c, ci_ptr_arg self,
                                ci_ptr_arg a0, ci_ptr_arg a1, ci_ptr_arg a2) {
	return NULL;
}

static ci_ptr bb_gc_trace_print(bb_coro_arg *c, ci_ptr_arg self,
                                ci_ptr_arg a0, ci_ptr_arg a1, ci_ptr_arg a2) {
	fprintf(stderr, "gc.trace_print: compile with -DTGMEMLIB_TRACKING\n");
	return NULL;
}

#endif /* TGMEMLIB_TRACKING */

static ci_ptr bb_gc_print_refcnt(bb_coro_arg *c, ci_ptr_arg self,
                                ci_ptr_arg a0, ci_ptr_arg a1, ci_ptr_arg a2) {
	if (!a0) {
		fprintf(stderr, "gc.print_refcnt: null\n");
	} else if (!CI_IS_REFCOUNTABLE(a0)) {
		fprintf(stderr, "gc.print_refcnt: not refcountable\n");
	} else {
		uint16_t rc = ((ci_gchdr *)a0)->refcnt;
		fprintf(stderr, "gc.print_refcnt: %p  rc=%u\n", (void *)a0, rc);
	}
	return NULL;
}

static void bb_lib_gc_init(bb_vm *vm) {
	ci_map *ns = ci_map_new(4);

	static const bb_cfunc gc_lib[] = {
		{ "trace_start", bb_gc_trace_start, 0 },
		{ "trace_print", bb_gc_trace_print, 0 },
		{ "print_refcnt", bb_gc_print_refcnt, 0 },
	};

	bb_func2map(vm, ns, gc_lib, sizeof(gc_lib) / sizeof(gc_lib[0]));

	ci_map_put(vm->globals, bb_vm_istring(vm, "gc", 2), (ci_ptr)ns);
}
