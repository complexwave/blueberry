/*
 * ci_timer_sim.c -- simulate networking timeouts
 *
 * Simulated time, advanced 1-50ms per tick.
 * Each tick starts 1000 timers with timeouts from {100, 1000, 5000, 30000}ms.
 * Polls expired timers each tick.
 */
#define _POSIX_C_SOURCE 199309L
#ifndef CI_TIMER_TEST
#define CI_TIMER_TEST
#endif
#define CI_TIMER_GLOBAL_HEAP

#include "ciobj.c"
#include "ci_timer.c"

#include <stdio.h>
#include <stdlib.h>

static int total_fired = 0;
static int total_started = 0;

static void sim_cb(ci_timer *timer, ci_ptr ctx) {
	(void)timer;
	(void)ctx;
	total_fired++;
}

static const ci_time timeouts[] = {
	CI_MS(100),
	CI_MS(1000),
	CI_MS(5000),
	CI_MS(30000),
};
#define N_TIMEOUTS (sizeof(timeouts) / sizeof(timeouts[0]))

static uint32_t rng_state = 12345;

static uint32_t rng_next(void) {
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}

int main(void) {
	ci_init();

	ci_timerheap *heap = ci_timerheap_new();
	ci_timer_register(heap);
	heap->batch_step = 1 * 1000 * 1000;

	ci_now_set(CI_SEC(1));

	int n_ticks = 200;
	int timers_per_tick = 100;

	printf("sim: %d ticks, %d timers/tick, batch_step=50ms\n",
		n_ticks, timers_per_tick);
	printf("timeouts: 100ms, 1s, 5s, 30s\n\n");

	for (int tick = 0; tick < n_ticks; tick++) {
		/* advance time 1-50ms */
		ci_time step = CI_MS(1 + (rng_next() % 50));
		ci_now_set(ci_now() + step);

		/* start timers */
		for (int i = 0; i < timers_per_tick; i++) {
			ci_time timeout = timeouts[rng_next() % N_TIMEOUTS];
			ci_timer *t = ci_timer_new(timeout, sim_cb, NULL);
			ci_timer_start(t);
			total_started++;
		}

		/* poll */
		int fired = ci_timerheap_poll(heap);

		if (tick % 50 == 0 || tick == n_ticks - 1) {
			printf("tick %3d  now=%0.3fs  started=%d  fired_this_tick=%d  total_fired=%d\n",
				tick,
				(double)ci_now() / 1000000000.0,
				total_started, fired, total_fired);
			ci_timerheap_debug_stats(heap);
			printf("\n");
		}
	}

	/* drain: advance time past all timers */
	printf("--- draining remaining timers ---\n");
	ci_now_set(ci_now() + CI_SEC(60));
	int drained = ci_timerheap_poll(heap);
	total_fired += drained;

	printf("drained:       %d\n", drained);
	printf("total started: %d\n", total_started);
	printf("total fired:   %d\n", total_fired);
	printf("heap remaining: %zu\n", heap->size);

	ci_timerheap_debug_stats(heap);

	ci_timerheap_free(heap);
	ci_shutdown();
	return 0;
}
