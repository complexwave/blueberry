/*
 * test_debug_dump.c — allocate strings + arrays, dump arena state,
 *                     exercise newalloc stack with cleanup watermark.
 */

#define CI_STRING_TEST  /* suppress ciobj.c main */
#define TG_NEWALLOC_CLEANUP_OVERRIDE

static int cleanup_count;

#include "../ciobj.c"

void tg_newalloc_cleanup(tg_allocator_t *alloc) {
	cleanup_count++;
	fprintf(stderr, "  >> newalloc_cleanup #%d  (pos=%d / lo=%d / size=%d)\n",
	        cleanup_count, alloc->newalloc_pos, alloc->newalloc_lo, alloc->newalloc_size);
	alloc->newalloc_pos = 0;
}

int main(void) {
	ci_init();
	ci_str_register();
	ci_arr_register();

	/* names registered automatically by ci_init -> ci_debug_register_names */

	/* ---- enable newalloc stack (small: 25 entries so we hit lo=17) ---- */
	tg_newalloc_resize(ci_alloc, 25);
	fprintf(stderr, "newalloc: size=%d  lo=%d  hi=%d\n\n",
	        ci_alloc->newalloc_size, ci_alloc->newalloc_lo, ci_alloc->newalloc_hi);

	/* ---- allocate 10 strings ---- */
	ci_str *strings[10];
	for (int i = 0; i < 10; i++) {
		char buf[64];
		snprintf(buf, sizeof(buf), "hello string #%d", i);
		strings[i] = ci_str_from_cstr(buf);
		assert(strings[i]);
	}

	/* ---- allocate 10 arrays ---- */
	ci_array *arrays[10];
	for (int i = 0; i < 10; i++) {
		arrays[i] = ci_arr_new(4);
		assert(arrays[i]);
	}

	fprintf(stderr, "\n==== after 10 strings + 10 arrays ====\n");
	fprintf(stderr, "cleanup fired %d time(s)\n\n", cleanup_count);

	/* dump newalloc stack contents */
	fprintf(stderr, "newalloc stack (pos=%d):\n", ci_alloc->newalloc_pos);
	for (int i = 0; i < ci_alloc->newalloc_pos; i++) {
		void *p = ci_alloc->newalloc[i];
		tg_arena_t *ar = tg_ptr_arena(p);
		fprintf(stderr, "  [%2d] %p  tag:0x%04X  obj_size:%u\n",
		        i, p, ar->type_tag, ar->obj_size);
	}

	/* ---- full arena dump ---- */
	fprintf(stderr, "\n");
	tg_debug_dump_all(stderr, ci_alloc,
	                  TG_DEBUG_OBJECTS | TG_DEBUG_FREELIST, NULL);

	/* ---- free some objects, dump again ---- */
	for (int i = 0; i < 5; i++) {
		ci_free(strings[i]);
		strings[i] = NULL;
	}
	for (int i = 0; i < 3; i++) {
		ci_free(arrays[i]);
		arrays[i] = NULL;
	}

	fprintf(stderr, "\n==== after freeing 5 strings + 3 arrays ====\n\n");
	tg_debug_dump_all(stderr, ci_alloc, TG_DEBUG_OBJECTS, NULL);

	/* ---- walk live objects ---- */
	/* just count — no-op callback */

	/* cleanup */
	for (int i = 5; i < 10; i++) ci_free(strings[i]);
	for (int i = 3; i < 10; i++) ci_free(arrays[i]);

	ci_shutdown();
	printf("test_debug_dump: PASSED (cleanup fired %d times)\n", cleanup_count);
	return 0;
}
