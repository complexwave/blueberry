#ifndef CI_TIMER_C
#define CI_TIMER_C
/*
 * ci_timer.c -- Citrin timer implementation
 *
 * ci_timerheap is plain malloc'd (not a ciobj).
 * Stored in timer arena ops.ctx, or as a global if CI_TIMER_GLOBAL_HEAP.
 * Heap insert/delete are stubs (user implements).
 * Poll uses heap min to fire expired timers.
 * Print shows [expires_after_ns] for each timer in the heap array.
 */

#include "ci_timer.h"
#include <stdlib.h>
#include <stdio.h>

/* ============================================================
 * Cached now
 * ============================================================ */

ci_time ci_now__cached = 0;

/* ============================================================
 * Global heap (when CI_TIMER_GLOBAL_HEAP defined)
 * ============================================================ */

#ifdef CI_TIMER_GLOBAL_HEAP
ci_timerheap *ci_timer__global_heap = NULL;
#endif

/* ============================================================
 * Destructor
 * ============================================================ */

static void ci_timer_destructor(void *ptr, tg_arena_t *arena) {
	(void)arena;
	(void)ptr;
}

/* ============================================================
 * Registration
 * ============================================================ */

void ci_timer_register(ci_timerheap *heap) {
	tg_arena_ops ops = { ci_timer_destructor, NULL, .ctx = heap };
	ci_register_ops(CI_TIMER, sizeof(ci_timer), &ops);
#ifdef CI_TIMER_GLOBAL_HEAP
	ci_timer__global_heap = heap;
#endif
}

/* ============================================================
 * ci_timerheap lifecycle (plain malloc)
 * ============================================================ */

ci_timerheap *ci_timerheap_new(void) {
	ci_timerheap *heap = calloc(1, sizeof(ci_timerheap));
	return heap;
}

void ci_timerheap_free(ci_timerheap *heap) {
	if (!heap) return;
	free(heap->timers);
	free(heap);
}

/* ============================================================
 * Constructors
 * ============================================================ */

ci_timer *ci_timer_new(ci_time delay, ci_timer_cb cb, ci_ptr ctx) {
	ci_timer *t = ci_new(CI_TIMER);
	if (!t) return NULL;

	t->cb = cb;
	t->ctx = ctx;
	t->expires = 0;
	t->prev = NULL;
	t->next = NULL;
	t->interval = delay;
	return t;
}

ci_timer *ci_timer_periodic_new(ci_time interval, ci_timer_cb cb, ci_ptr ctx) {
	ci_timer *t = ci_new(CI_TIMER);
	if (!t) return NULL;

	t->gc.flags |= CI_TIMER_PERIODIC;
	t->cb = cb;
	t->ctx = ctx;
	t->expires = 0;
	t->prev = NULL;
	t->next = NULL;
	t->interval = interval;
	return t;
}

void ci_timer_start(ci_timer *timer) {
	if (ci_timer_active(timer))
		return;

	timer->expires = ci_now() + timer->interval;
	timer->gc.flags |= CI_TIMER_ACTIVE;
	ci_timerheap *heap = ci_timer_get_heap(timer);
	ci_timerheap_insert(heap, timer);
}

void ci_timer_stop(ci_timer *timer) {
	if (!ci_timer_active(timer))
		return;

	timer->gc.flags &= ~CI_TIMER_ACTIVE;
	ci_timerheap *heap = ci_timer_get_heap(timer);
	ci_timerheap_delete(heap, timer);
}

void ci_timer_restart(ci_timer *timer) {
	if (ci_timer_active(timer)) {
		ci_timer_stop(timer);
	}
	ci_timer_start(timer);
}

/* ============================================================
 * Heap ensure capacity
 * ============================================================ */

void ci_timerheap_ensure(ci_timerheap *heap, size_t needed) {
	if (needed <= heap->capacity)
		return;

	size_t newcap = heap->capacity ? heap->capacity * 2 : 16;
	while (newcap < needed)
		newcap *= 2;

	ci_timer **newbuf = realloc(heap->timers, newcap * sizeof(ci_timer *));
	if (!newbuf)
		return;

	heap->timers = newbuf;
	heap->capacity = newcap;
}

/* ============================================================
 * Heap insert/delete -- stubs
 * ============================================================ */


/* ============================================================
 * Chain -- circular linked list for batched timers
 * ============================================================
 *
 * Head timer lives in the heap array.
 * Chained timers hang off head->next in a circular list.
 * Insert after head for O(1).
 */

void ci_timer_chain(ci_timer *head, ci_timer *timer) {
	if (!head->next) {
		head->next = timer;
		timer->next = head;
		timer->prev = head;
	} else {
		ci_timer *second = head->next;
		head->next = timer;
		timer->next = second;
		timer->prev = head;
		second->prev = timer;
	}
}

void ci_timer_unchain(ci_timer *timer) {
	ci_timer *p = timer->prev;
	ci_timer *n = timer->next;

	if (p == n) {
		/* only chained timer -- p and n are the head */
		p->next = NULL;
	} else {
		p->next = n;
		if (!(n->gc.flags & CI_TIMER_HEAP)) {
			n->prev = p;
		}
	}

	timer->prev = NULL;
	timer->next = NULL;
}

/* ============================================================
 * Heap insert with batch collapsing
 * ============================================================ */

#define _expires(i) (heap[i]->expires)
#define _SWAP(a, b) do { \
	ci_timer *_tmp = heap[a]; \
	heap[a] = heap[b]; \
	heap[b] = _tmp; \
	heap[a]->heap_idx = (a); \
	heap[b]->heap_idx = (b); \
} while(0)

void ci_timerheap_insert(ci_timerheap *timerheap, ci_timer *timer) {
	ci_time batch = (timer->gc.flags & CI_TIMER_NOBATCH)
		? 0
		: timerheap->batch_step;

	ci_timerheap_ensure(timerheap, timerheap->size + 1);

	ci_timer **heap = timerheap->timers;
	size_t pos = timerheap->size;

	heap[pos] = timer;
	timer->heap_idx = pos;
	timer->gc.flags |= CI_TIMER_HEAP;
	timerheap->size++;

	while (pos >= 1) {
		size_t parent = (pos - 1) / 2;

		/* batch: close enough to parent, chain instead of sifting */
		if (batch > 0) {
			
			{
				size_t check_idx = parent;
				ci_time a = _expires(pos);
				ci_time b = _expires(check_idx);
				ci_time diff = a > b ? a - b : b - a;
				if (diff <= batch) {
					/* remove self from heap slot */
					timerheap->size--;
					timer->gc.flags &= ~CI_TIMER_HEAP;
					if (pos < timerheap->size) {
						heap[pos] = heap[timerheap->size];
						heap[pos]->heap_idx = pos;
					}
					ci_timer_chain(heap[check_idx], timer);
					return;
				}
			};
			
			if(  pos > 4 ){
				size_t check_idx = pos-1;
				ci_time a = _expires(pos);
				ci_time b = _expires(check_idx);
				ci_time diff = a > b ? a - b : b - a;
				if (diff <= batch) {
					/* remove self from heap slot */
					timerheap->size--;
					timer->gc.flags &= ~CI_TIMER_HEAP;
					if (pos < timerheap->size) {
						heap[pos] = heap[timerheap->size];
						heap[pos]->heap_idx = pos;
					}
					ci_timer_chain(heap[check_idx], timer);
					return;
				}
				
				
				
				{
				size_t check_idx = pos-2;
				ci_time a = _expires(pos);
				ci_time b = _expires(check_idx);
				ci_time diff = a > b ? a - b : b - a;
				if (diff <= batch) {
					/* remove self from heap slot */
					timerheap->size--;
					timer->gc.flags &= ~CI_TIMER_HEAP;
					if (pos < timerheap->size) {
						heap[pos] = heap[timerheap->size];
						heap[pos]->heap_idx = pos;
					}
					ci_timer_chain(heap[check_idx], timer);
					return;
				}
				
				{
					size_t check_idx = pos-3;
					ci_time a = _expires(pos);
					ci_time b = _expires(check_idx);
					ci_time diff = a > b ? a - b : b - a;
					if (diff <= batch) {
						/* remove self from heap slot */
						timerheap->size--;
						timer->gc.flags &= ~CI_TIMER_HEAP;
						if (pos < timerheap->size) {
							heap[pos] = heap[timerheap->size];
							heap[pos]->heap_idx = pos;
						}
						ci_timer_chain(heap[check_idx], timer);
						return;
					}
				}
				
				
			
			}
			
			
			}
			
			
			
			
		}

		if (_expires(pos) < _expires(parent)) {
			_SWAP(pos, parent);
			pos = parent;
			continue;
		}

		break;
	}

	/* right child smaller than left -- swap siblings */
	if (!((pos + 1) % 2)) {
		size_t left = pos - 1;

		if (_expires(pos) < _expires(left)) {
			_SWAP(pos, left);
		}
	}
}

void ci_timerheap_delete(ci_timerheap *heap, ci_timer *timer) {
	size_t i = timer->heap_idx;
	timer->gc.flags &= ~CI_TIMER_HEAP;
	heap->size--;

	if (i < heap->size) {
		heap->timers[i] = heap->timers[heap->size];
		heap->timers[i]->heap_idx = i;
		/* TODO: sift to restore heap property */
	}
}

/* ============================================================
 * Accessors -- stubs (assume valid heap)
 * ============================================================ */

ci_timer *ci_timerheap_min(ci_timerheap *heap) {
	if (heap->size == 0)
		return NULL;
	return heap->timers[0];
	/* assumes min-heap: root is minimum */
}

ci_timer *ci_timerheap_max(ci_timerheap *heap) {
	if (heap->size == 0)
		return NULL;
	/* in a min-heap, max is among the leaves (size/2 .. size-1) */
	/* stub: linear scan */
	ci_timer *max = heap->timers[heap->size / 2];
	for (size_t i = heap->size / 2 + 1; i < heap->size; i++) {
		if (heap->timers[i]->expires > max->expires)
			max = heap->timers[i];
	}
	return max;
}

/* ============================================================
 * Poll -- fire expired timers using heap min
 * ============================================================ */

int ci_timerheap_poll(ci_timerheap *heap) {
	ci_time now = ci_now();
	int fired = 0;

	while (heap->size > 0) {
		ci_timer *min = ci_timerheap_min(heap);
		if (!min || min->expires > now)
			break;

		ci_timerheap_delete(heap, min);

		/* fire the head */
		if (min->cb)
			min->cb(min, min->ctx);
		fired++;

		/* fire the chain */
		if (min->next) {
			ci_timer *cur = min->next;
			while (cur != min) {
				if (cur->cb)
					cur->cb(cur, cur->ctx);
				fired++;
				ci_timer *nxt = cur->next;
				cur->next = NULL;
				cur = nxt;
			}
			min->next = NULL;
		}

		if (min->gc.flags & CI_TIMER_PERIODIC) {
			min->expires += min->interval;
			ci_timerheap_insert(heap, min);
		}
	}

	return fired;
}

/* ============================================================
 * Print -- [expires_after] [expires_after] ...
 * ============================================================ */

void ci_timerheap_print(ci_timerheap *heap) {
	printf("heap(%zu):", heap->size);

	for (size_t i = 0; i < heap->size; i++) {
		ci_timer *t = heap->timers[i];
		int chain_len = 0;

		if (t->next) {
			ci_timer *cur = t->next;
			while (cur != t) {
				chain_len++;
				cur = cur->next;
			}
		}

		printf(" [%0.2fs", ci_timer_expires(t));
		if (chain_len > 0) {
			printf("+%d", chain_len);
		}
		printf("]");

		if (!((i + 1) % 2))
			printf(" |");
	}

	printf("\n");
}

/* ============================================================
 * Print timer chain -- follows next pointers from first timer
 * ============================================================ */

void ci_timer_print_chain(ci_timer *timer) {
	ci_time now = ci_now();
	int i = 0;

	while (timer) {
		int64_t delta = (int64_t)(timer->expires - now);
		if (i > 0)
			printf(" -> ");
		printf("[%ld]", (long)delta);
		timer = timer->next;
		i++;
	}
	printf("\n");
}

/* ============================================================
 * Debug stats -- walk heap + chains
 * ============================================================ */

void ci_timerheap_debug_stats(ci_timerheap *heap) {
	size_t batched_heads = 0;
	size_t total_chained = 0;
	size_t chain_min = 0;
	size_t chain_max = 0;

	for (size_t i = 0; i < heap->size; i++) {
		ci_timer *t = heap->timers[i];
		if (!t->next)
			continue;

		size_t len = 0;
		ci_timer *cur = t->next;
		while (cur != t) {
			len++;
			cur = cur->next;
		}

		batched_heads++;
		total_chained += len;

		if (batched_heads == 1) {
			chain_min = len;
			chain_max = len;
		} else {
			if (len < chain_min) chain_min = len;
			if (len > chain_max) chain_max = len;
		}
	}

	printf("--- timerheap stats ---\n");
	printf("  heap slots:    %zu\n", heap->size);
	printf("  batched heads: %zu\n", batched_heads);
	printf("  total chained: %zu\n", total_chained);
	printf("  total timers:  %zu\n", heap->size + total_chained);

	if (batched_heads > 0) {
		printf("  chain min:     %zu\n", chain_min);
		printf("  chain max:     %zu\n", chain_max);
		printf("  chain mean:    %.1f\n", (double)total_chained / batched_heads);
	}

	printf("  batch_step:    %0.1fms\n", (double)heap->batch_step / 1000000.0);
	printf("---\n");
}

#endif /* CI_TIMER_C */
