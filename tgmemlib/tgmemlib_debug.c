/*
 * tgmemlib_debug.c — arena debug dumper
 *
 * #include this after tgmemlib.c.  Provides human-readable dumps of
 * per-type arena chains: live objects with [%p rc:%d], free list,
 * utilisation bars, and per-arena / per-type statistics.
 *
 * Designed as the inspection foundation for a future tracing GC.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

/* ---- optional type name registry ---- */

#define TG_DEBUG_MAX_NAMES 256

typedef struct {
	uint16_t    tag;
	const char *name;
} tg_debug_name_entry;

static tg_debug_name_entry tg_debug_names[TG_DEBUG_MAX_NAMES];
static int                 tg_debug_name_count;

/*
 * tg_debug_register_name — associate a human-readable name with a tag.
 * Call once per type at init.  Names are optional; unknown tags print as
 * "tag:0x%04X".
 */
void tg_debug_register_name(uint16_t tag, const char *name) {
	if (tg_debug_name_count >= TG_DEBUG_MAX_NAMES) return;

	tg_debug_name_entry *e = &tg_debug_names[tg_debug_name_count++];
	e->tag  = tag;
	e->name = name;
}

static const char *tg_debug_lookup_name(uint16_t tag) {
	for (int i = 0; i < tg_debug_name_count; i++) {
		if (tg_debug_names[i].tag == tag)
			return tg_debug_names[i].name;
	}
	return NULL;
}

/* format type name into buf, fixed width, returns buf */
static char *tg_debug_fmt_type(char *buf, size_t bufsz, uint16_t tag) {
	const char *name = tg_debug_lookup_name(tag);
	if (name)
		snprintf(buf, bufsz, "%-12s 0x%04X", name, tag);
	else
		snprintf(buf, bufsz, "             0x%04X", tag);
	return buf;
}

/* ---- internal helpers ---- */

/* count nodes in an intrusive freelist */
static int tg_debug_freelist_len(void *fl) {
	int n = 0;
	void *p = fl;
	while (p) {
		n++;
		p = *(void **)p;
	}
	return n;
}

/* check if ptr is on the freelist (O(n) scan, debug only) */
static int tg_debug_is_free(void *fl, void *ptr) {
#ifdef TG_FREELIST_DISABLED
	(void)fl;
#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
	return __asan_address_is_poisoned(ptr);
#else
	(void)ptr;
	return 1;
#endif
#else
	void *p = fl;
	while (p) {
		if (p == ptr) return 1;
		p = *(void **)p;
	}
	return 0;
#endif
}

/* print a bar graph: filled/total, width chars */
static void tg_debug_bar(FILE *f, int filled, int total, int width) {
	if (total == 0) { fprintf(f, "[empty]"); return; }

	int full = (filled * width + total - 1) / total;
	if (full > width) full = width;

	fputc('[', f);
	for (int i = 0; i < width; i++)
		fputc(i < full ? '#' : '.', f);
	fputc(']', f);
}

/* ---- per-arena dump ---- */

/*
 * tg_debug_dump_arena — dump one arena in detail.
 *
 *   flags:
 *     TG_DEBUG_OBJECTS   — list every slot (live/free)
 *     TG_DEBUG_FREELIST  — list freelist chain
 *     TG_DEBUG_BRIEF     — header + bar only
 */
#define TG_DEBUG_OBJECTS   (1 << 0)
#define TG_DEBUG_FREELIST  (1 << 1)
#define TG_DEBUG_BRIEF     (1 << 2)

/* optional callback: called for each live object during dump.
 * return a short static string to append, or NULL. */
typedef const char *(*tg_debug_obj_fmt_fn)(void *obj, tg_arena_t *ar);

static void tg_debug_dump_arena(FILE *f, tg_arena_t *ar, int arena_idx,
                                int flags, tg_debug_obj_fmt_fn obj_fmt) {
	int cap       = tg_arena_capacity(ar);
	int live      = ar->live_count;
	int free_cnt  = tg_debug_freelist_len(ar->freelist);
	int bump_left = (int)(ar->end - ar->bump) / ar->obj_size;

	fprintf(f, "  arena %-3d  %14p  size:%-5u  cap:%-5d  live:%-5d  free:%-5d  bump:%-5d\n",
	        arena_idx, (void *)ar, ar->obj_size, cap, live, free_cnt, bump_left);

	/* utilisation bar */
	fprintf(f, "             ");
	tg_debug_bar(f, live, cap, 30);
	fprintf(f, "  %5d/%-5d  %5.1f%%\n", live, cap,
	        cap ? (100.0 * live / cap) : 0.0);

	/* freelist dump */
	if (flags & TG_DEBUG_FREELIST) {
		fprintf(f, "             freelist:");
		void *p = ar->freelist;
		int shown = 0;
		while (p && shown < 32) {
			fprintf(f, " %p", p);
			p = *(void **)p;
			shown++;
		}
		if (p) fprintf(f, " ... (+%d more)", free_cnt - shown);
		fputc('\n', f);
	}

	/* per-slot object dump */
	if (flags & TG_DEBUG_OBJECTS) {
		char *base = ARENA_DATA(ar);
		/* only dump slots up to bump (untouched slots past bump are not interesting) */
		int used_slots = (int)(ar->bump - base) / ar->obj_size;

		fprintf(f, "             objects (%d touched):\n", used_slots);
		for (int i = 0; i < used_slots; i++) {
			void *slot = base + (size_t)i * ar->obj_size;

			if (tg_debug_is_free(ar->freelist, slot)) {
				fprintf(f, "               [%14p  FREE]\n", slot);
			} else {
				/* live object — try to show refcount if refcountable */
				uint16_t rc = *(uint16_t *)slot;  /* first 2 bytes = gc.refcnt if CI_GC_HDR */
				const char *extra = obj_fmt ? obj_fmt(slot, ar) : NULL;
				fprintf(f, "               [%14p  rc:%-5u%s%s]\n",
				        slot, rc,
				        extra ? "  " : "",
				        extra ? extra : "");
			}
		}
	}
}

/* ---- per-type dump ---- */

typedef struct {
	int arena_count;
	int total_capacity;
	int total_live;
	int total_free;
	int total_bump_avail;
	size_t total_memory;   /* bytes mmap'd */
} tg_debug_type_stats;

static tg_debug_type_stats tg_debug_collect_stats(tg_arena_t *head) {
	tg_debug_type_stats st = {0};

	for (tg_arena_t *ar = head; ar; ar = ar->next) {
		st.arena_count++;
		int cap = tg_arena_capacity(ar);
		st.total_capacity   += cap;
		st.total_live       += ar->live_count;
		st.total_free       += tg_debug_freelist_len(ar->freelist);
		st.total_bump_avail += (int)(ar->end - ar->bump) / ar->obj_size;
		st.total_memory     += ARENA_SIZE;
	}
	return st;
}

/*
 * tg_debug_dump_type — dump all arenas for one type tag.
 */
void tg_debug_dump_type(FILE *f, tg_allocator_t *alloc, uint16_t tag,
                        int flags, tg_debug_obj_fmt_fn obj_fmt) {
	tg_arena_t *head = alloc->heads[tag];
	if (!head) return;

	char namebuf[40];
	tg_debug_fmt_type(namebuf, sizeof(namebuf), tag);
	fprintf(f, "── %s  ptrtag:0x%02X  obj_size:%-5u  dtor:%-3s  visit:%-3s ──\n",
	        namebuf,
	        tag & (PTR_MAX_TYPES - 1),
	        alloc->obj_sizes[tag],
	        alloc->ops[tag].destructor ? "yes" : "no",
	        alloc->ops[tag].visitor    ? "yes" : "no");

	/* per-arena detail */
	int idx = 0;
	for (tg_arena_t *ar = head; ar; ar = ar->next, idx++) {
		tg_debug_dump_arena(f, ar, idx, flags, obj_fmt);
	}

	/* type totals */
	tg_debug_type_stats st = tg_debug_collect_stats(head);
	fprintf(f, "  ── totals   %d arena(s)  cap:%-5d  live:%-5d  free:%-5d  bump:%-5d  %3zuK\n",
	        st.arena_count, st.total_capacity, st.total_live,
	        st.total_free, st.total_bump_avail, st.total_memory / 1024);

	fprintf(f, "             ");
	tg_debug_bar(f, st.total_live, st.total_capacity, 30);
	fprintf(f, "  %5.1f%% utilisation\n\n",
	        st.total_capacity ? (100.0 * st.total_live / st.total_capacity) : 0.0);
}

/* ---- full allocator dump ---- */

typedef struct {
	int    type_count;
	int    total_arenas;
	int    total_live;
	int    total_capacity;
	size_t total_memory;
} tg_debug_global_stats;

/*
 * tg_debug_dump_all — dump every registered type.
 *
 * Prints a per-type section, then a global summary.
 */
void tg_debug_dump_all(FILE *f, tg_allocator_t *alloc, int flags,
                       tg_debug_obj_fmt_fn obj_fmt) {
	fprintf(f, "╔═══════════════════════════════════════════════════════════════════════════════╗\n");
	fprintf(f, "║  tgmemlib arena dump                                                        ║\n");
	fprintf(f, "╠═══════════════════════════════════════════════════════════════════════════════╣\n");

	fprintf(f, "  addr_tagged:  %-3s    arena_size: %uK    header: %zu bytes\n\n",
	        tg_allocator_addr_tagged(alloc) ? "yes" : "no",
	        ARENA_SIZE / 1024,
	        (size_t)ARENA_HDR_SIZE);

	tg_debug_global_stats gs = {0};

	for (int tag = 0; tag < MAX_TYPES; tag++) {
		if (!alloc->heads[tag]) continue;

		tg_debug_dump_type(f, alloc, (uint16_t)tag, flags, obj_fmt);

		tg_debug_type_stats st = tg_debug_collect_stats(alloc->heads[tag]);
		gs.type_count++;
		gs.total_arenas   += st.arena_count;
		gs.total_live     += st.total_live;
		gs.total_capacity += st.total_capacity;
		gs.total_memory   += st.total_memory;
	}

	fprintf(f, "╠═══════════════════════════════════════════════════════════════════════════════╣\n");
	fprintf(f, "║  SUMMARY                                                                    ║\n");
	fprintf(f, "╠═══════════════════════════════════════════════════════════════════════════════╣\n");
	fprintf(f, "  types:    %-6d  arenas:   %-6d  memory:   %zuK (%.1f MiB)\n",
	        gs.type_count, gs.total_arenas,
	        gs.total_memory / 1024,
	        (double)gs.total_memory / (1024.0 * 1024.0));
	fprintf(f, "  live:     %-6d  capacity: %-6d  ",
	        gs.total_live, gs.total_capacity);

	if (gs.total_capacity > 0) {
		tg_debug_bar(f, gs.total_live, gs.total_capacity, 30);
		fprintf(f, "  %5.1f%%",
		        100.0 * gs.total_live / gs.total_capacity);
	}

	fprintf(f, "\n╚═══════════════════════════════════════════════════════════════════════════════╝\n");
}

/* ---- convenience: dump one type to stderr ---- */

void tg_debug_dump_type_stderr(tg_allocator_t *alloc, uint16_t tag) {
	tg_debug_dump_type(stderr, alloc, tag, TG_DEBUG_OBJECTS | TG_DEBUG_FREELIST, NULL);
}

/* ---- convenience: dump everything to stderr ---- */

void tg_debug_dump_stderr(tg_allocator_t *alloc) {
	tg_debug_dump_all(stderr, alloc, TG_DEBUG_OBJECTS | TG_DEBUG_FREELIST, NULL);
}

/* ---- convenience: brief summary to stderr (no per-object listing) ---- */

void tg_debug_summary_stderr(tg_allocator_t *alloc) {
	tg_debug_dump_all(stderr, alloc, TG_DEBUG_BRIEF, NULL);
}

/* ---- GC preparation: walk all live objects ---- */

/*
 * tg_debug_walk_live — call `cb` for every live object of a given type.
 * Skips freelist slots.  Foundation for mark phase.
 *
 * Returns total live count visited.
 */
int tg_debug_walk_live(tg_allocator_t *alloc, uint16_t tag,
                       void (*cb)(void *obj, tg_arena_t *ar, void *ctx),
                       void *ctx) {
	int visited = 0;

	for (tg_arena_t *ar = alloc->heads[tag]; ar; ar = ar->next) {
		char *base = ARENA_DATA(ar);
		int touched = (int)(ar->bump - base) / ar->obj_size;

		for (int i = 0; i < touched; i++) {
			void *slot = base + (size_t)i * ar->obj_size;

			if (!tg_debug_is_free(ar->freelist, slot)) {
				cb(slot, ar, ctx);
				visited++;
			}
		}
	}
	return visited;
}

/*
 * tg_debug_walk_all_live — walk every live object across all registered types.
 */
int tg_debug_walk_all_live(tg_allocator_t *alloc,
                           void (*cb)(void *obj, tg_arena_t *ar, void *ctx),
                           void *ctx) {
	int total = 0;
	for (int tag = 0; tag < MAX_TYPES; tag++) {
		if (!alloc->heads[tag]) continue;
		total += tg_debug_walk_live(alloc, (uint16_t)tag, cb, ctx);
	}
	return total;
}

/* ==== TGMEMLIB_TRACKING: leak report ==== */

#ifdef TGMEMLIB_TRACKING

void tg_track_report(FILE *f, tg_allocator_t *alloc) {
	fprintf(f, "\n╔═══════════════════════════════════════════════════════════════════════════════╗\n");
	fprintf(f, "║  tgmemlib tracking report                                                   ║\n");
	fprintf(f, "╠═══════════════════════════════════════════════════════════════════════════════╣\n");
	fprintf(f, "  alloc total: %" PRIu64 "    free total: %" PRIu64 "    leaked: %" PRIu64 "\n\n",
	        alloc->track_alloc_total,
	        alloc->track_free_total,
	        alloc->track_alloc_total - alloc->track_free_total);

	int total_leaked = 0;

	for (int tag = 0; tag < MAX_TYPES; tag++) {
		if (!alloc->heads[tag]) continue;

		/* collect live count across all arenas for this type */
		int type_live = 0;
		for (tg_arena_t *ar = alloc->heads[tag]; ar; ar = ar->next)
			type_live += ar->live_count;

		if (type_live == 0) continue;

		char namebuf[40];
		tg_debug_fmt_type(namebuf, sizeof(namebuf), (uint16_t)tag);

		int is_rc = (tag & 0x02);  /* CI_REFCOUNTABLE bit */
		fprintf(f, "── %s  live: %d  %s ──\n", namebuf, type_live,
		        is_rc ? "(refcounted)" : "(no-rc)");

		/* walk each arena, list live objects */
		for (tg_arena_t *ar = alloc->heads[tag]; ar; ar = ar->next) {
			char *base = ARENA_DATA(ar);
			int touched = (int)(ar->bump - base) / ar->obj_size;

			for (int i = 0; i < touched; i++) {
				void *slot = base + (size_t)i * ar->obj_size;

				if (tg_debug_is_free(ar->freelist, slot))
					continue;

				uint16_t rc = *(uint16_t *)slot;  /* gc.refcnt */

				if (is_rc) {
					const char *rc_status = "";
					if (rc == 0xFFFF) rc_status = " (saturated)";
					else if (rc == 0) rc_status = " ** RC=0 NOT FREED **";

					fprintf(f, "    %p  rc: %-5u%s\n", slot, rc, rc_status);
				} else {
					fprintf(f, "    %p\n", slot);
				}
			}
		}

		total_leaked += type_live;
		fprintf(f, "\n");
	}

	fprintf(f, "╠═══════════════════════════════════════════════════════════════════════════════╣\n");
	fprintf(f, "  total live objects at report time: %d\n", total_leaked);
	fprintf(f, "╚═══════════════════════════════════════════════════════════════════════════════╝\n");
}

void tg_track_report_stderr(tg_allocator_t *alloc) {
	tg_track_report(stderr, alloc);
}

#endif /* TGMEMLIB_TRACKING */
