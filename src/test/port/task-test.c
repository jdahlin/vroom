/*
 * Unit test of port/task.c (vtask.h): every index once, nested loops, loops
 * from many outside threads at once, width and budget limits, budget
 * changes at run time, the small-items probe, spawn order and wait, the
 * blocking hand-off, the cpu probe.  Exits 0 and prints "task-test: ok".
 *
 *   make -C src task-test && build/src/task-test
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "port/task.h"

static int failures;

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "task-test:%d: %s\n", __LINE__, #c); failures++; } } while (0)

/* ---- every index exactly once ---- */

typedef struct counts {
	size_t n;
	_Atomic int *seen;
	_Atomic int threads_seen[1024];
	_Atomic int concurrent, max_concurrent;
	unsigned spin;
} counts;

static void spin(unsigned k)
{
	for (volatile unsigned i = 0; i < k; i++)
		;
}

static void count_item(void *arg, size_t i)
{
	counts *c = arg;
	int now = atomic_fetch_add(&c->concurrent, 1) + 1, m;

	while ((m = atomic_load(&c->max_concurrent)) < now && !atomic_compare_exchange_weak(&c->max_concurrent, &m, now))
		;
	atomic_fetch_add(&c->seen[i], 1);
	atomic_fetch_add(&c->threads_seen[vtask_worker_index() + 1], 1);
	spin(c->spin);
	atomic_fetch_sub(&c->concurrent, 1);
}

static counts *counts_new(size_t n, unsigned spin_k)
{
	counts *c = calloc(1, sizeof(*c));

	c->n = n;
	c->seen = calloc(n ? n : 1, sizeof(*c->seen));
	c->spin = spin_k;
	return c;
}

static void counts_check_once(counts *c)
{
	for (size_t i = 0; i < c->n; i++)
		if (atomic_load(&c->seen[i]) != 1) {
			fprintf(stderr, "task-test: index %zu of %zu run %d times\n", i, c->n, atomic_load(&c->seen[i]));
			failures++;
			return;
		}
}

static void counts_free(counts *c)
{
	free(c->seen);
	free(c);
}

static void test_once(void)
{
	static const size_t sizes[] = { 0, 1, 2, 7, 100, 10000, 100000 };

	for (size_t k = 0; k < sizeof sizes / sizeof *sizes; k++)
		for (int width = -1; width <= 9; width += 2)
			for (unsigned flags = 0; flags <= VTASK_SMALL_ITEMS; flags++) {
				counts *c = counts_new(sizes[k], 50);

				vtask_parallel_for(c->n, width, count_item, c, flags);
				counts_check_once(c);
				if (width >= 1)
					CHECK(atomic_load(&c->max_concurrent) <= width);
				counts_free(c);
			}
}

/* ---- the budget caps helpers; width 1 and budget 1 are serial ---- */

static void test_limits(void)
{
	counts *c;

	vtask_budget_set(3);
	c = counts_new(2000, 20000);
	vtask_parallel_for(c->n, 8, count_item, c, 0);
	counts_check_once(c);
	CHECK(atomic_load(&c->max_concurrent) <= 3);
	counts_free(c);

	vtask_budget_set(1);
	c = counts_new(500, 2000);
	vtask_parallel_for(c->n, 8, count_item, c, 0);
	counts_check_once(c);
	CHECK(atomic_load(&c->max_concurrent) == 1);
	CHECK(atomic_load(&c->threads_seen[0]) == 500);	/* all on the caller */
	counts_free(c);

	vtask_budget_set(4);
	c = counts_new(500, 2000);
	vtask_parallel_for(c->n, 1, count_item, c, 0);
	CHECK(atomic_load(&c->max_concurrent) == 1);
	counts_free(c);

	/* scale up: more helpers once the budget allows */
	vtask_budget_set(6);
	c = counts_new(3000, 20000);
	vtask_parallel_for(c->n, 0, count_item, c, 0);
	counts_check_once(c);
	CHECK(atomic_load(&c->max_concurrent) <= 6);
	CHECK(atomic_load(&c->max_concurrent) >= 2);
	counts_free(c);
	CHECK(vtask_stats_get().running == 0);
}

/* ---- small items: tiny loops wake nobody ---- */

static void test_small_items(void)
{
	unsigned long w0;
	counts *c;

	vtask_budget_set(4);
	w0 = vtask_stats_get().wakes;
	for (int r = 0; r < 200; r++) {
		c = counts_new(64, 0);
		vtask_parallel_for(c->n, 4, count_item, c, VTASK_SMALL_ITEMS);
		counts_check_once(c);
		CHECK(atomic_load(&c->threads_seen[0]) == 64);
		counts_free(c);
	}
	CHECK(vtask_stats_get().wakes == w0);
}

/* ---- nesting: a loop inside an item, outer x inner indices once ---- */

typedef struct nested {
	_Atomic int seen[64][64];
	int width;
} nested;

typedef struct inner_arg {
	nested *n;
	size_t outer;
} inner_arg;

static void inner_item(void *arg, size_t j)
{
	inner_arg *a = arg;

	atomic_fetch_add(&a->n->seen[a->outer][j], 1);
	spin(2000);
}

static void outer_item(void *arg, size_t i)
{
	nested *n = arg;
	inner_arg a = { n, i };

	vtask_parallel_for(64, n->width, inner_item, &a, 0);
}

static void test_nested(void)
{
	for (int b = 1; b <= 8; b *= 2) {
		nested *n = calloc(1, sizeof(*n));

		vtask_budget_set(b);
		n->width = 4;
		vtask_parallel_for(64, 0, outer_item, n, 0);
		for (int i = 0; i < 64; i++)
			for (int j = 0; j < 64; j++)
				if (atomic_load(&n->seen[i][j]) != 1) {
					fprintf(stderr, "task-test: nested (%d,%d) at budget %d: %d\n", i, j, b,
						atomic_load(&n->seen[i][j]));
					failures++;
					i = j = 64;
				}
		free(n);
		CHECK(vtask_stats_get().running == 0);
	}
}

/* ---- many outside threads, each with its loops ---- */

static void *outside_main(void *arg)
{
	(void)arg;
	for (int r = 0; r < 30; r++) {
		counts *c = counts_new(997, 300);

		vtask_parallel_for(c->n, 4, count_item, c, r & 1 ? VTASK_SMALL_ITEMS : 0);
		counts_check_once(c);
		counts_free(c);
	}
	return NULL;
}

static void test_outside_threads(void)
{
	pthread_t th[12];

	vtask_budget_set(4);
	for (int i = 0; i < 12; i++)
		pthread_create(&th[i], NULL, outside_main, NULL);
	for (int i = 0; i < 12; i++)
		pthread_join(th[i], NULL);
	CHECK(vtask_stats_get().running == 0);
}

/* ---- jobs: FIFO start, budget, wait_idle, nested loops in jobs ---- */

static _Atomic int job_order[400], job_next, job_concurrent, job_max;

static void job_fn(void *arg)
{
	int k = (int)(intptr_t)arg, now = atomic_fetch_add(&job_concurrent, 1) + 1, m;

	while ((m = atomic_load(&job_max)) < now && !atomic_compare_exchange_weak(&job_max, &m, now))
		;
	atomic_store(&job_order[atomic_fetch_add(&job_next, 1)], k);
	if (k % 5 == 0) {
		counts *c = counts_new(300, 1000);

		vtask_parallel_for(c->n, 0, count_item, c, 0);
		counts_check_once(c);
		counts_free(c);
	} else {
		spin(20000);
	}
	atomic_fetch_sub(&job_concurrent, 1);
}

static void test_jobs(void)
{
	vtask_budget_set(3);
	for (int k = 0; k < 400; k++)
		vtask_spawn(job_fn, (void *)(intptr_t)k);
	vtask_wait_idle();
	CHECK(atomic_load(&job_next) == 400);
	CHECK(atomic_load(&job_max) <= 3);
	{	/* started in order, give or take the threads racing to record it */
		int late = 0;

		for (int i = 0; i < 400; i++)
			late += abs(atomic_load(&job_order[i]) - i) > 3;
		CHECK(late == 0);
	}
	CHECK(vtask_stats_get().running == 0);
}

/* ---- blocking: a blocked job gives its slot to the next ---- */

static pthread_mutex_t gate_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_cv = PTHREAD_COND_INITIALIZER;
static int gate_open;
static _Atomic int second_ran;

static void blocked_job(void *arg)
{
	(void)arg;
	vtask_blocking_begin();
	pthread_mutex_lock(&gate_mu);
	while (!gate_open)
		pthread_cond_wait(&gate_cv, &gate_mu);
	pthread_mutex_unlock(&gate_mu);
	vtask_blocking_end();
}

static void second_job(void *arg)
{
	(void)arg;
	atomic_store(&second_ran, 1);
	/* the first job waits for this one: it may open the gate */
	pthread_mutex_lock(&gate_mu);
	gate_open = 1;
	pthread_cond_broadcast(&gate_cv);
	pthread_mutex_unlock(&gate_mu);
}

static void test_blocking(void)
{
	vtask_budget_set(1);
	vtask_spawn(blocked_job, NULL);
	vtask_spawn(second_job, NULL);	/* with budget 1: only runs because the first blocks */
	vtask_wait_idle();
	CHECK(atomic_load(&second_ran));
	CHECK(vtask_stats_get().running == 0);
	CHECK(vtask_stats_get().blocked == 0);
	/* nested regions and regions on threads the budget does not count */
	vtask_blocking_begin();
	vtask_blocking_begin();
	vtask_blocking_end();
	vtask_blocking_end();
	CHECK(vtask_stats_get().running == 0);
}

int main(void)
{
	vtask_cpus c = vtask_cpus_probe();

	CHECK(c.granted >= 1);
	CHECK(c.performance >= 1 && c.performance <= c.granted);
	CHECK(c.efficiency >= 0 && c.performance + c.efficiency <= c.granted + c.efficiency);
	CHECK(vtask_worker_index() == -1);
	setenv("VCC_JOBS", "5", 1);
	CHECK(vtask_jobs_from_environment(2) == 5);
	setenv("VCC_JOBS", "x", 1);
	CHECK(vtask_jobs_from_environment(2) == 2);
	unsetenv("VCC_JOBS");
	CHECK(vtask_jobs_from_environment(7) == 7);

	test_once();
	test_limits();
	test_small_items();
	test_nested();
	test_outside_threads();
	test_jobs();
	test_blocking();
	vtask_budget_set(0);
	CHECK(vtask_budget() == c.granted);
	if (failures) {
		fprintf(stderr, "task-test: %d failures\n", failures);
		return 1;
	}
	printf("task-test: ok (%d cores: %d P, %d E; %d pool threads)\n", c.granted, c.performance, c.efficiency,
	       vtask_stats_get().threads);
	return 0;
}
