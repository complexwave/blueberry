#ifndef CI_TIMER_H
#define CI_TIMER_H
/*
 * ci_timer.h -- Citrin timer type (libev-style)
 *
 * Two timer modes:
 *   one-shot:  fires once at `expires`, then done.
 *   periodic:  fires at `expires`, then reschedules at `expires + interval`.
 *              CI_TIMER_PERIODIC flag in gc.flags.
 *
 * ci_timerheap -- plain malloc'd min-heap of timers ordered by `expires`.
 *   Not a ciobj (not exposed to scripting lang).
 *   Heap operations (insert/delete/sift) are stubs -- user implements.
 *   Poll checks heap min, fires expired callbacks, reschedules periodic.
 *
 * Heap access:
 *   By default, heap is stored in timer arena's ops.ctx.
 *   ci_timer_get_heap(timer) recovers it from the arena.
 *   ci_timer_register(heap) stores the heap in ops.ctx.
 *
 *   If CI_TIMER_GLOBAL_HEAP is defined, ci_timer_get_heap() returns
 *   a static global instead (no arena indirection).
 *
 * Tags:
 *   ci_timer uses VM family e5 (ptrtag 0x2C | OBJ | RC = 0x2F).
 *
 * ---------------------------------------------------------------------------
 * Example:
 *
 *   ci_timerheap *heap = ci_timerheap_new();
 *   ci_timer_register(heap);
 *   ci_timer *t = ci_timer_new(ci_now() + 1000000000ULL, my_cb, my_ctx);
 *   ci_timerheap_insert(heap, t);
 *   ci_timerheap_poll(heap);
 *   ci_timerheap_print(heap);
 * ---------------------------------------------------------------------------
 */

#include "ciobj.h"
#include <stdint.h>
#include <time.h>

/* ============================================================
 * Time
 * ============================================================ */

typedef uint64_t ci_time;

#define CI_MS(n) ((ci_time)(n) * 1000000ULL)
#define CI_SEC(n) ((ci_time)(n) * 1000000000ULL)

extern ci_time ci_now__cached;

static inline ci_time ci_now_real(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (ci_time)ts.tv_sec * 1000000000ULL + (ci_time)ts.tv_nsec;
}

static inline ci_time ci_now_update(void) {
	ci_now__cached = ci_now_real();
	return ci_now__cached;
}

static inline void ci_now_set(ci_time t) {
	ci_now__cached = t;
}

#ifdef CI_AUTONOW
#define ci_now() ci_now_update()
#else
#define ci_now() ci_now__cached
#endif

/* ============================================================
 * Tag definitions
 * ============================================================
 *
 * VM family e5: CI_FAMILY_ENTRY(CI_O_FAMILY_8, 5) = 0x2C
 *   ci_timer: ptrtag 0x2C | CI_OBJECT | CI_REFCOUNTABLE = 0x2F
 */

#define CI_TIMER_FAMILY  CI_O_FAMILY_8
#define CI_TIMER_TAG     CI_FAMILY_ENTRY(CI_TIMER_FAMILY, 5)

#define CI_TIMER  ((uint16_t)(CI_TIMER_TAG | CI_OBJECT | CI_REFCOUNTABLE))

/* ============================================================
 * GC flag bits
 * ============================================================ */

#define CI_TIMER_HEAP     (1 << 0)
#define CI_TIMER_PERIODIC (1 << 1)
#define CI_TIMER_NOBATCH  (1 << 2)
#define CI_TIMER_ACTIVE   (1 << 3)

/* ============================================================
 * Forward declarations
 * ============================================================ */

typedef struct ci_timer ci_timer;
typedef struct ci_timerheap ci_timerheap;

/* ============================================================
 * Callback type
 * ============================================================ */

typedef void (*ci_timer_cb)(ci_timer *timer, ci_ptr ctx);

/* ============================================================
 * ci_timer
 * ============================================================ */

struct ci_timer {
	CI_GC_HDR;
	ci_timer_cb cb;
	ci_ptr ctx;
	ci_time expires;
	
	union {
		ci_timer *prev;
		size_t heap_idx;
	};
	
	ci_timer *next;
	ci_time interval;
};

/* ============================================================
 * ci_timerheap (plain malloc'd, not a ciobj)
 * ============================================================ */

struct ci_timerheap {
	size_t size;
	size_t capacity;
	ci_timer **timers;
	ci_time batch_step;
};

/* ============================================================
 * Heap access from timer
 * ============================================================ */

#ifdef CI_TIMER_GLOBAL_HEAP
extern ci_timerheap *ci_timer__global_heap;
static inline ci_timerheap *ci_timer_get_heap(ci_timer *t) {
	(void)t;
	return ci_timer__global_heap;
}
#else
static inline ci_timerheap *ci_timer_get_heap(ci_timer *t) {
	return (ci_timerheap *)tg_ptr_arena(t)->ops.ctx;
}
#endif

static double ci_timer_expires(ci_timer* timer){
	uint64_t delta = timer->expires - ci_now();
	return (double)delta/1000000000;
}


/* ============================================================
 * Registration
 * ============================================================ */

void ci_timer_register(ci_timerheap *heap);

/* ============================================================
 * Constructors
 * ============================================================ */

ci_timer *ci_timer_new(ci_time delay, ci_timer_cb cb, ci_ptr ctx);
ci_timer *ci_timer_periodic_new(ci_time interval, ci_timer_cb cb, ci_ptr ctx);
static inline int ci_timer_active(ci_timer *t) {
	return t->gc.flags & CI_TIMER_ACTIVE;
}

void ci_timer_start(ci_timer *timer);
void ci_timer_stop(ci_timer *timer);
void ci_timer_restart(ci_timer *timer);

ci_timerheap *ci_timerheap_new(void);
void ci_timerheap_free(ci_timerheap *heap);

/* ============================================================
 * Heap operations (stubs -- user implements internals)
 * ============================================================ */

void ci_timerheap_ensure(ci_timerheap *heap, size_t needed);
void ci_timer_chain(ci_timer *head, ci_timer *timer);
void ci_timer_unchain(ci_timer *timer);
void ci_timerheap_insert(ci_timerheap *heap, ci_timer *timer);
void ci_timerheap_delete(ci_timerheap *heap, ci_timer *timer);

/* ============================================================
 * Accessors
 * ============================================================ */

ci_timer *ci_timerheap_min(ci_timerheap *heap);
ci_timer *ci_timerheap_max(ci_timerheap *heap);

/* ============================================================
 * Poll and print
 * ============================================================ */

int ci_timerheap_poll(ci_timerheap *heap);
void ci_timerheap_print(ci_timerheap *heap);
void ci_timer_print_chain(ci_timer *timer);
void ci_timerheap_debug_stats(ci_timerheap *heap);

#endif /* CI_TIMER_H */
