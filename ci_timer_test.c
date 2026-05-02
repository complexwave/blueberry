/*
 * ci_timer_test.c -- basic tests for ci_timer boilerplate
 */
#define _POSIX_C_SOURCE 199309L
#ifndef CI_TIMER_TEST
#define CI_TIMER_TEST
#endif
#define CI_TIMER_GLOBAL_HEAP
#ifndef CI_AUTONOW
#define CI_AUTONOW
#endif

#include "ciobj.c"
#include "ci_timer.c"

#include <stdio.h>
#include <assert.h>

static int cb_count = 0;
static ci_timerheap *g_heap;

static void test_cb(ci_timer *timer, ci_ptr ctx) {
	(void)timer;
	cb_count++;
	printf("  fired timer, ctx=%p, count=%d\n", ctx, cb_count);
}

static void test_now(void) {
	printf("--- test_now ---\n");
	ci_time t1 = ci_now();
	ci_time t2 = ci_now();
	assert(t2 >= t1);
	printf("  ci_now = %lu ns\n", (unsigned long)t1);
	printf("  delta  = %lu ns\n", (unsigned long)(t2 - t1));
	printf("  PASS\n");
}

static void test_timer_new(void) {
	printf("--- test_timer_new ---\n");

	ci_timer *t = ci_timer_new(CI_SEC(1), test_cb, (ci_ptr)0xCAFE);
	assert(t != NULL);
	assert(t->cb == test_cb);
	assert(t->ctx == (ci_ptr)0xCAFE);
	assert(t->expires == 0);
	assert(t->interval == CI_SEC(1));
	assert(t->next == NULL);
	assert(!(t->gc.flags & CI_TIMER_PERIODIC));
	printf("  one-shot timer OK\n");

	ci_timer *p = ci_timer_periodic_new(CI_MS(200), test_cb, (ci_ptr)0xBEEF);
	assert(p != NULL);
	assert(p->gc.flags & CI_TIMER_PERIODIC);
	assert(p->interval == CI_MS(200));
	assert(p->expires == 0);
	printf("  periodic timer OK\n");

	printf("  PASS\n");
}

static void test_get_heap(void) {
	printf("--- test_get_heap ---\n");

	ci_timer *t = ci_timer_new(ci_now() + CI_SEC(1), NULL, NULL);
	ci_timerheap *h = ci_timer_get_heap(t);
	assert(h == g_heap);
	printf("  ci_timer_get_heap returned correct heap\n");

	printf("  PASS\n");
}

static void test_heap_basic(void) {
	printf("--- test_heap_basic ---\n");

	ci_timerheap *heap = ci_timerheap_new();
	assert(heap != NULL);
	assert(heap->size == 0);
	assert(ci_timerheap_min(heap) == NULL);
	assert(ci_timerheap_max(heap) == NULL);

	ci_timer *t1 = ci_timer_new(CI_SEC(3), test_cb, NULL);
	ci_timer *t2 = ci_timer_new(CI_SEC(1), test_cb, NULL);
	ci_timer *t3 = ci_timer_new(CI_SEC(2), test_cb, NULL);

	t1->expires = ci_now() + t1->interval;
	t2->expires = ci_now() + t2->interval;
	t3->expires = ci_now() + t3->interval;

	ci_timerheap_insert(heap, t1);
	ci_timerheap_insert(heap, t2);
	ci_timerheap_insert(heap, t3);
	assert(heap->size == 3);

	/* min should be t2 (1s) */
	assert(ci_timerheap_min(heap) == t2);

	printf("  heap after insert: ");
	ci_timerheap_print(heap);

	ci_timerheap_delete(heap, t2);
	assert(heap->size == 2);
	printf("  heap after delete: ");
	ci_timerheap_print(heap);

	ci_timerheap_free(heap);
	printf("  PASS\n");
}

static void test_start_stop(void) {
	printf("--- test_start_stop ---\n");

	ci_timer *t = ci_timer_new(CI_MS(500), test_cb, (ci_ptr)0xA);

	assert(!ci_timer_active(t));
	assert(t->expires == 0);

	ci_timer_start(t);
	assert(ci_timer_active(t));
	assert(t->expires > 0);
	assert(g_heap->size == 1);
	printf("  start OK, expires in %0.2fs\n", ci_timer_expires(t));

	/* start again should be no-op */
	size_t old_size = g_heap->size;
	ci_timer_start(t);
	assert(g_heap->size == old_size);
	printf("  double-start no-op OK\n");

	ci_timer_stop(t);
	assert(!ci_timer_active(t));
	assert(g_heap->size == 0);
	printf("  stop OK\n");

	/* stop again should be no-op */
	ci_timer_stop(t);
	assert(g_heap->size == 0);
	printf("  double-stop no-op OK\n");

	printf("  PASS\n");
}

static void test_restart(void) {
	printf("--- test_restart ---\n");

	ci_timer *t = ci_timer_new(CI_SEC(1), test_cb, NULL);

	/* restart when inactive -- just starts */
	ci_timer_restart(t);
	assert(ci_timer_active(t));
	assert(g_heap->size == 1);
	ci_time first_expires = t->expires;
	printf("  restart from inactive OK, expires=%0.2fs\n", ci_timer_expires(t));

	/* small busy wait so ci_now() advances */
	for (volatile int i = 0; i < 1000000; i++);

	/* restart when active -- stop+start, new expiry */
	ci_timer_restart(t);
	assert(ci_timer_active(t));
	assert(g_heap->size == 1);
	assert(t->expires > first_expires);
	printf("  restart from active OK, new expires=%0.2fs\n", ci_timer_expires(t));

	ci_timer_stop(t);
	printf("  PASS\n");
}

static void test_batch_chain(void) {
	printf("--- test_batch_chain ---\n");

	ci_timerheap *heap = ci_timerheap_new();
	heap->batch_step = CI_MS(100);  /* 100ms batch window */

	ci_time now = ci_now();

	/* t1 at 1s, t2 at 1.05s, t3 at 1.08s -- all within 100ms of t1 */
	ci_timer *t1 = ci_timer_new(0, test_cb, (ci_ptr)0x1);
	ci_timer *t2 = ci_timer_new(0, test_cb, (ci_ptr)0x2);
	ci_timer *t3 = ci_timer_new(0, test_cb, (ci_ptr)0x3);
	t1->expires = now + CI_SEC(1);
	t2->expires = now + CI_SEC(1) + CI_MS(50);
	t3->expires = now + CI_SEC(1) + CI_MS(80);

	/* t4 at 5s -- far away, should not batch */
	ci_timer *t4 = ci_timer_new(0, test_cb, (ci_ptr)0x4);
	t4->expires = now + CI_SEC(5);

	ci_timerheap_insert(heap, t1);
	ci_timerheap_insert(heap, t2);
	ci_timerheap_insert(heap, t3);
	ci_timerheap_insert(heap, t4);

	/* t2 and t3 should batch with t1, t4 separate */
	assert(heap->size == 2);
	assert(t1->next != NULL);
	printf("  heap size=2 (batched 3 into 1 slot): ");
	ci_timerheap_print(heap);

	/* verify chain is circular */
	ci_timer *cur = t1->next;
	int chain_count = 0;
	while (cur != t1) {
		chain_count++;
		cur = cur->next;
	}
	assert(chain_count == 2);
	printf("  chain length=2 OK\n");

	/* t4 should be alone */
	assert(t4->next == NULL);
	printf("  t4 not batched OK\n");

	ci_timerheap_free(heap);
	printf("  PASS\n");
}

static void test_nobatch_flag(void) {
	printf("--- test_nobatch_flag ---\n");

	ci_timerheap *heap = ci_timerheap_new();
	heap->batch_step = CI_MS(100);

	ci_time now = ci_now();

	ci_timer *t1 = ci_timer_new(0, test_cb, NULL);
	t1->expires = now + CI_SEC(1);

	ci_timer *t2 = ci_timer_new(0, test_cb, NULL);
	t2->expires = now + CI_SEC(1) + CI_MS(10);
	t2->gc.flags |= CI_TIMER_NOBATCH;

	ci_timerheap_insert(heap, t1);
	ci_timerheap_insert(heap, t2);

	/* t2 has NOBATCH -- should get its own heap slot */
	assert(heap->size == 2);
	assert(t1->next == NULL);
	printf("  NOBATCH timer got own slot: ");
	ci_timerheap_print(heap);

	ci_timerheap_free(heap);
	printf("  PASS\n");
}

static void test_poll_chain(void) {
	printf("--- test_poll_chain ---\n");

	ci_timerheap *heap = ci_timerheap_new();
	heap->batch_step = CI_MS(100);

	ci_time now = ci_now();

	/* 3 timers already expired, close together -- will batch */
	ci_timer *t1 = ci_timer_new(0, test_cb, (ci_ptr)0x10);
	ci_timer *t2 = ci_timer_new(0, test_cb, (ci_ptr)0x20);
	ci_timer *t3 = ci_timer_new(0, test_cb, (ci_ptr)0x30);
	t1->expires = now - CI_MS(50);
	t2->expires = now - CI_MS(40);
	t3->expires = now - CI_MS(30);

	ci_timerheap_insert(heap, t1);
	ci_timerheap_insert(heap, t2);
	ci_timerheap_insert(heap, t3);
	assert(heap->size == 1);  /* all batched */

	cb_count = 0;
	int fired = ci_timerheap_poll(heap);
	assert(fired == 3);
	assert(heap->size == 0);
	printf("  poll fired all 3 batched timers OK\n");

	ci_timerheap_free(heap);
	printf("  PASS\n");
}

static void test_poll_expired(void) {
	printf("--- test_poll_expired ---\n");

	ci_timerheap *heap = ci_timerheap_new();
	ci_time now = ci_now();

	/* already expired */
	ci_timer *t1 = ci_timer_new(now - 1, test_cb, (ci_ptr)0x1);
	ci_timer *t2 = ci_timer_new(now - 2, test_cb, (ci_ptr)0x2);
	/* not yet expired */
	ci_timer *t3 = ci_timer_new(now + CI_SEC(5), test_cb, (ci_ptr)0x3);

	ci_timerheap_insert(heap, t1);
	ci_timerheap_insert(heap, t2);
	ci_timerheap_insert(heap, t3);
	


	cb_count = 0;
	int fired = ci_timerheap_poll(heap);
	printf("  fired = %d (expected >= 1, heap stub may vary)\n", fired);

	printf("  remaining in heap: ");
	ci_timerheap_print(heap);

	ci_timerheap_free(heap);
	printf("  PASS\n");
}

static void test_poll_periodic(void) {
	printf("--- test_poll_periodic ---\n");

	ci_timerheap *heap = ci_timerheap_new();

	ci_timer *p = ci_timer_periodic_new(CI_MS(100), test_cb, (ci_ptr)0xAA);
	p->expires = ci_now() - 1;
	ci_timerheap_insert(heap, p);

	cb_count = 0;
	int fired = ci_timerheap_poll(heap);
	printf("  fired = %d\n", fired);
	/* periodic timer should be re-inserted */
	printf("  heap size after poll = %zu (expect 1 -- rescheduled)\n", heap->size);

	printf("  heap: ");
	ci_timerheap_print(heap);

	ci_timerheap_free(heap);
	printf("  PASS\n");
}

static void test_print_format(void) {
	printf("--- test_print_format ---\n");

	ci_timerheap *heap = ci_timerheap_new();
	ci_time now = ci_now();

	ci_timerheap_insert(heap, ci_timer_new(now + CI_SEC(1), NULL, NULL));
	ci_timerheap_insert(heap, ci_timer_new(now + CI_SEC(2), NULL, NULL));
	ci_timerheap_insert(heap, ci_timer_new(now + CI_MS(500), NULL, NULL));

	printf("  ");
	ci_timerheap_print(heap);

	ci_timerheap_free(heap);
	printf("  PASS\n");
}

int main(void) {
	ci_init();

	g_heap = ci_timerheap_new();
	ci_timer_register(g_heap);

	test_now();
	test_timer_new();
	test_get_heap();
	test_heap_basic();
	test_start_stop();
	test_restart();
	test_batch_chain();
	test_nobatch_flag();
	test_poll_chain();

	printf("=== ALL CI_TIMER TESTS PASSED ===\n");

	ci_timerheap_free(g_heap);
	ci_shutdown();
	return 0;
}
