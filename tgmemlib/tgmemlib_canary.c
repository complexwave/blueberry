/*
 * tgmemlib_canary.c — padding canary for buffer overrun detection.
 * Included directly by tgmemlib.c; not compiled separately.
 *
 * Each slot is inflated by TG_PAD_CANARY_SIZE bytes. The tail bytes
 * are filled with a fixed pattern on alloc. On free, the pattern is
 * validated to detect overruns. Neighbor slots are also checked.
 *
 * Defines:
 *   TG_PAD_CANARY            — enable (validate self + ±2 neighbors)
 *   TG_PAD_CANARY_AGGRESSIVE — validate entire arena on every alloc/free
 */

#ifdef TG_PAD_CANARY

#include <stdio.h>

#ifndef TG_PAD_CANARY_SIZE
#define TG_PAD_CANARY_SIZE 16
#endif
#define TG_PAD_CANARY_BYTE   0xCA
#define TG_PAD_CANARY_LINKED 0xCB

static void tg_canary_write(void *ptr, uint16_t obj_size) {
	memset((char *)ptr + obj_size - TG_PAD_CANARY_SIZE,
	       TG_PAD_CANARY_BYTE, TG_PAD_CANARY_SIZE);
}

static int tg_canary_intact(void *ptr, uint16_t obj_size) {
	unsigned char *c = (unsigned char *)ptr + obj_size - TG_PAD_CANARY_SIZE;
	if (c[0] == TG_PAD_CANARY_LINKED) return 1; /* inner linked slot — skip */
	for (int i = 0; i < TG_PAD_CANARY_SIZE; i++)
		if (c[i] != TG_PAD_CANARY_BYTE) return 0;
	return 1;
}

static void tg_canary_fail(void *ptr, tg_arena_t *ar, const char *ctx) {
	unsigned char *c = (unsigned char *)ptr + ar->obj_size - TG_PAD_CANARY_SIZE;
	fprintf(stderr,
		"tgmemlib: pad canary CORRUPTED at %p (tag=%u, %s)\n"
		"  canary bytes:",
		ptr, ar->type_tag, ctx);
	for (int i = 0; i < TG_PAD_CANARY_SIZE; i++)
		fprintf(stderr, " %02x", c[i]);
	fprintf(stderr, " (expected all 0x%02x)\n", TG_PAD_CANARY_BYTE);
	assert(0 && "tgmemlib: pad canary corrupted — buffer overrun");
}

static void tg_canary_check(void *ptr, tg_arena_t *ar, const char *ctx) {
	TG_ASAN_UNPOISON((char *)ptr + ar->obj_size - TG_PAD_CANARY_SIZE,
	                  TG_PAD_CANARY_SIZE);
	if (!tg_canary_intact(ptr, ar->obj_size))
		tg_canary_fail(ptr, ar, ctx);
	TG_ASAN_POISON((char *)ptr + ar->obj_size - TG_PAD_CANARY_SIZE,
	               TG_PAD_CANARY_SIZE);
}

static void tg_canary_validate_nearby(void *ptr, tg_arena_t *ar, int range) {
	char *data = ARENA_DATA(ar);
	int idx    = (int)((char *)ptr - data) / ar->obj_size;
	int used   = (int)(ar->bump - data) / ar->obj_size;

	for (int d = -range; d <= range; d++) {
		int i = idx + d;
		if (i < 0 || i >= used) continue;
		void *slot = data + (size_t)i * ar->obj_size;
		tg_canary_check(slot, ar, d == 0 ? "self" : "neighbor");
	}
}

static void tg_canary_validate_arena(tg_arena_t *ar) {
	char *data = ARENA_DATA(ar);
	int used   = (int)(ar->bump - data) / ar->obj_size;

	for (int i = 0; i < used; i++) {
		void *slot = data + (size_t)i * ar->obj_size;
		tg_canary_check(slot, ar, "arena-scan");
	}
}

static void tg_canary_on_alloc(void *ptr, tg_arena_t *ar) {
	tg_canary_write(ptr, ar->obj_size);
#ifdef TG_PAD_CANARY_AGGRESSIVE
	tg_canary_validate_arena(ar);
#else
	tg_canary_validate_nearby(ptr, ar, 2);
#endif
}

static void tg_canary_on_free(void *ptr, tg_arena_t *ar) {
#ifdef TG_PAD_CANARY_AGGRESSIVE
	tg_canary_validate_arena(ar);
#else
	tg_canary_validate_nearby(ptr, ar, 2);
#endif
}

static void tg_canary_on_alloc_linked(void *ptr, tg_arena_t *ar, int count) {
	for (int i = 0; i < count - 1; i++) {
		char *s = (char *)ptr + (size_t)i * ar->obj_size;
		memset(s + ar->obj_size - TG_PAD_CANARY_SIZE,
		       TG_PAD_CANARY_LINKED, TG_PAD_CANARY_SIZE);
	}
	char *last = (char *)ptr + (size_t)(count - 1) * ar->obj_size;
	tg_canary_write(last, ar->obj_size);
	TG_ASAN_POISON(last + ar->obj_size - TG_PAD_CANARY_SIZE,
	               TG_PAD_CANARY_SIZE);
}

static inline uint16_t tg_canary_pad_size(void) { return TG_PAD_CANARY_SIZE; }

#else /* !TG_PAD_CANARY */

#define tg_canary_on_alloc(ptr, ar)             ((void)0)
#define tg_canary_on_free(ptr, ar)              ((void)0)
#define tg_canary_on_alloc_linked(ptr, ar, cnt) ((void)0)
static inline uint16_t tg_canary_pad_size(void) { return 0; }

#endif /* TG_PAD_CANARY */
