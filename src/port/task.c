/*
 * The thread pool and the CPU budget behind task.h; the design and its
 * reasons are doc/quality/WORK-scheduler.md.
 *
 * One mutex guards everything that decides who runs: the open loops that
 * still want helpers, the job queue, the idle stack, the running count.  It
 * is taken at a loop's start and end, at a job's start and end, and around
 * blocking regions: never per item (items are handed out by each loop's
 * atomic index).  Work is assigned to a thread under the mutex before the
 * thread is woken, and the running count goes up at assignment, so a wake
 * is never wasted and the budget is never overshot by pool threads.
 *
 * Order of work for a thread that becomes free: the newest open loop first
 * (a nested loop is on the path of the item that started it), then the
 * oldest queued job.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE	/* sched_getaffinity, CPU_COUNT */
#endif
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

#include "port/task.h"

#define VTASK_MAX_THREADS 512

typedef struct vtask_loop {
	vtask_item_fn *fn;
	void *arg;
	size_t n;
	atomic_size_t next;
	int want;			/* helpers still wanted (under mu) */
	int active;			/* helpers assigned and not done (under mu) */
	pthread_cond_t done;		/* active reached 0 */
	struct vtask_loop *next_open;	/* in open_loops while want > 0 */
} vtask_loop;

typedef struct vtask_thread {
	pthread_cond_t wake;
	struct vtask_thread *next_idle;
	int index;
	bool assigned;		/* work below is set; the thread is counted in running */
	vtask_loop *loop;	/* help this loop, or */
	vtask_fn *job_fn;	/* run this job */
	void *job_arg;
} vtask_thread;

typedef struct vtask_job {
	vtask_fn *fn;
	void *arg;
} vtask_job;

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t idle_jobs = PTHREAD_COND_INITIALIZER;	/* no job queued or running */
static vtask_loop *open_loops;
static vtask_thread *idle_top;
static vtask_thread *threads[VTASK_MAX_THREADS];
static int nthreads, budget, running, blocked, nidle, jobs_running;
static unsigned long nwakes, njobs;
static vtask_job *jobq;
static size_t jobq_head, jobq_tail, jobq_cap;	/* ring, cap a power of two */
static size_t stack_size;
static bool stack_fixed;

static _Thread_local vtask_thread *self;
static _Thread_local int counted;		/* this thread is in running */
static _Thread_local int blocking_depth;
static _Thread_local bool blocking_released;	/* the outermost region gave the slot back */

/* ---------------- the machine ---------------- */

#ifdef __linux__
/* the cgroup's CPU quota rounded up, 0 if none: v2 cpu.max, then v1 */
static int cgroup_quota(void)
{
	FILE *f;
	long long q = -1, p = 0;

	if ((f = fopen("/sys/fs/cgroup/cpu.max", "r"))) {
		char word[32];

		if (fscanf(f, "%31s %lld", word, &p) == 2 && strcmp(word, "max"))
			q = atoll(word);
		fclose(f);
	} else {
		static const char *const dirs[] = { "/sys/fs/cgroup/cpu", "/sys/fs/cgroup/cpu,cpuacct" };

		for (size_t d = 0; d < sizeof dirs / sizeof *dirs && q < 0; d++) {
			char path[96];

			snprintf(path, sizeof path, "%s/cpu.cfs_quota_us", dirs[d]);
			if (!(f = fopen(path, "r")))
				continue;
			if (fscanf(f, "%lld", &q) != 1)
				q = -1;
			fclose(f);
			snprintf(path, sizeof path, "%s/cpu.cfs_period_us", dirs[d]);
			if ((f = fopen(path, "r"))) {
				if (fscanf(f, "%lld", &p) != 1)
					p = 0;
				fclose(f);
			}
		}
	}
	if (q <= 0 || p <= 0)
		return 0;
	return (int)((q + p - 1) / p);
}
#endif

static vtask_cpus cpus_probe(void)
{
	vtask_cpus c = { 0, 0, 0 };
#ifdef __APPLE__
	int v;
	size_t len = sizeof v;

	if (!sysctlbyname("hw.perflevel0.physicalcpu", &v, &len, NULL, 0) && v > 0)
		c.performance = v;
	len = sizeof v;
	if (!sysctlbyname("hw.perflevel1.physicalcpu", &v, &len, NULL, 0) && v > 0)
		c.efficiency = v;
	len = sizeof v;
	if (!sysctlbyname("hw.activecpu", &v, &len, NULL, 0) && v > 0)
		c.granted = v;
#elif defined(__linux__)
	{
		cpu_set_t set;
		int quota = cgroup_quota();

		if (!sched_getaffinity(0, sizeof set, &set))
			c.granted = CPU_COUNT(&set);
		if (quota > 0 && (c.granted <= 0 || quota < c.granted))
			c.granted = quota;
	}
#endif
	if (c.granted <= 0)
		c.granted = (int)sysconf(_SC_NPROCESSORS_ONLN);
	if (c.granted <= 0)
		c.granted = 4;
	if (c.efficiency >= c.granted)
		c.efficiency = 0;
	if (c.performance <= 0 || c.performance > c.granted)
		c.performance = c.granted - c.efficiency;
	return c;
}

static vtask_cpus cpus;
static pthread_once_t cpus_once = PTHREAD_ONCE_INIT;

static void cpus_init(void)
{
	cpus = cpus_probe();
}

vtask_cpus vtask_cpus_probe(void)
{
	pthread_once(&cpus_once, cpus_init);
	return cpus;
}

/* ---------------- the budget ---------------- */

/* (under mu) */
static int budget_locked(void)
{
	if (budget <= 0)
		budget = vtask_cpus_probe().granted;
	return budget;
}

int vtask_budget(void)
{
	pthread_mutex_lock(&mu);
	int b = budget_locked();
	pthread_mutex_unlock(&mu);
	return b;
}

static void dispatch_locked(void);

void vtask_budget_set(int n)
{
	pthread_mutex_lock(&mu);
	budget = n > 0 ? n : vtask_cpus_probe().granted;
	dispatch_locked();
	pthread_mutex_unlock(&mu);
}

int vtask_jobs_from_environment(int fallback)
{
	const char *s = getenv("VCC_JOBS");
	char *end;
	long n;

	if (!s || !*s)
		return fallback;
	n = strtol(s, &end, 10);
	return *end || n <= 0 || n > 4096 ? fallback : (int)n;
}

void vtask_stack_size_set(size_t bytes)
{
	pthread_mutex_lock(&mu);
	if (!stack_fixed)
		stack_size = bytes;
	pthread_mutex_unlock(&mu);
}

/* the main thread's stack: its rlimit (8 MB by default), at least 1 MB */
static size_t main_stack_size(void)
{
	struct rlimit rl;
	size_t s = (size_t)8 << 20;

	if (!getrlimit(RLIMIT_STACK, &rl) && rl.rlim_cur != RLIM_INFINITY && rl.rlim_cur > 0)
		s = rl.rlim_cur > ((rlim_t)1 << 30) ? (size_t)1 << 30 : (size_t)rl.rlim_cur;
	return s < ((size_t)1 << 20) ? (size_t)1 << 20 : s;
}

/* ---------------- threads ---------------- */

static void *thread_main(void *arg);

/* a new pool thread, asleep with nothing assigned; NULL at the cap or if
   the system refuses (under mu) */
static vtask_thread *thread_create_locked(void)
{
	vtask_thread *t;
	pthread_attr_t at;
	pthread_t th;

	if (nthreads >= VTASK_MAX_THREADS || !(t = calloc(1, sizeof(*t))))
		return NULL;
	if (!stack_fixed) {
		stack_fixed = true;
		if (!stack_size)
			stack_size = main_stack_size();
	}
	pthread_cond_init(&t->wake, NULL);
	t->index = nthreads;
	pthread_attr_init(&at);
	pthread_attr_setstacksize(&at, stack_size);
	pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&th, &at, thread_main, t)) {
		pthread_attr_destroy(&at);
		pthread_cond_destroy(&t->wake);
		free(t);
		return NULL;
	}
	pthread_attr_destroy(&at);
	threads[nthreads++] = t;
	return t;
}

/* is there work a free thread could take? (under mu) */
static bool demand_locked(void)
{
	return open_loops || jobq_head != jobq_tail;
}

/* give t the most urgent work: the newest open loop, else the oldest job;
   t is then counted in running (under mu, demand_locked() true) */
static void assign_locked(vtask_thread *t)
{
	vtask_loop *l = open_loops;

	if (l) {
		t->loop = l;
		l->active++;
		if (--l->want == 0)
			open_loops = l->next_open;
	} else {
		vtask_job *j = &jobq[jobq_head++ & (jobq_cap - 1)];

		t->job_fn = j->fn;
		t->job_arg = j->arg;
		jobs_running++;
	}
	t->assigned = true;
	running++;
}

/* wake (or create) threads for waiting work while the budget allows (under mu) */
static void dispatch_locked(void)
{
	while (demand_locked() && running < budget_locked()) {
		vtask_thread *t = idle_top;

		if (t) {
			idle_top = t->next_idle;
			nidle--;
		} else if (nthreads >= budget + blocked || !(t = thread_create_locked())) {
			return;
		}
		assign_locked(t);
		nwakes++;
		pthread_cond_signal(&t->wake);
	}
}

static void loop_run(vtask_loop *l)
{
	size_t i;

	while ((i = atomic_fetch_add_explicit(&l->next, 1, memory_order_relaxed)) < l->n)
		l->fn(l->arg, i);
}

static void *thread_main(void *arg)
{
	vtask_thread *t = arg;

	self = t;
	pthread_mutex_lock(&mu);
	for (;;) {
		vtask_loop *l;
		vtask_fn *fn;
		void *fa;

		while (!t->assigned)
			pthread_cond_wait(&t->wake, &mu);
		l = t->loop;
		fn = t->job_fn;
		fa = t->job_arg;
		t->loop = NULL;
		t->job_fn = NULL;
		t->assigned = false;
		pthread_mutex_unlock(&mu);
		counted = 1;
		if (l)
			loop_run(l);
		else
			fn(fa);
		counted = 0;
		pthread_mutex_lock(&mu);
		if (l) {
			if (--l->active == 0)
				pthread_cond_signal(&l->done);
		} else if (--jobs_running == 0 && jobq_head == jobq_tail) {
			pthread_cond_broadcast(&idle_jobs);
		}
		running--;
		if (demand_locked() && running < budget_locked()) {
			assign_locked(t);	/* straight on, without sleeping */
			continue;
		}
		t->next_idle = idle_top;
		idle_top = t;
		nidle++;
	}
	return NULL;
}

/* ---------------- loops ---------------- */

static uint64_t clock_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

/* VTASK_SMALL_ITEMS: waking a thread costs about as much as 50 us of work */
#define VTASK_PROBE_NS 30000u
#define VTASK_HELPER_NS 60000u

void vtask_parallel_for(size_t n, int width, vtask_item_fn *fn, void *arg, unsigned flags)
{
	vtask_loop l;
	size_t i = 0;
	int helpers;
	bool outside;

	if (n == 0)
		return;
	if (width <= 0)
		width = vtask_budget();
	if (width <= 1 || n == 1) {
		for (; i < n; i++)
			fn(arg, i);
		return;
	}
	if (flags & VTASK_SMALL_ITEMS) {
		uint64_t t0 = clock_ns(), el, left, w;

		do {
			fn(arg, i++);
			el = clock_ns() - t0;
		} while (i < n && el < VTASK_PROBE_NS);
		if (i == n)
			return;
		/* the cost of the items left, by the cost of those run */
		left = (uint64_t)(n - i) * (el / i + 1);
		w = left / VTASK_HELPER_NS;
		helpers = w > (uint64_t)(width - 1) ? width - 1 : (int)w;
	} else {
		helpers = width - 1;
	}
	if ((size_t)helpers > n - i - 1)
		helpers = (int)(n - i - 1);
	if (helpers <= 0) {
		for (; i < n; i++)
			fn(arg, i);
		return;
	}
	l.fn = fn;
	l.arg = arg;
	l.n = n;
	atomic_init(&l.next, i);
	l.want = helpers;
	l.active = 0;
	pthread_cond_init(&l.done, NULL);
	pthread_mutex_lock(&mu);
	outside = !counted;
	if (outside) {
		running++;
		counted = 1;
	}
	l.next_open = open_loops;
	open_loops = &l;
	dispatch_locked();
	pthread_mutex_unlock(&mu);

	loop_run(&l);

	pthread_mutex_lock(&mu);
	if (l.want > 0) {	/* helpers that never came: not waited for */
		vtask_loop **p = &open_loops;

		while (*p != &l)
			p = &(*p)->next_open;
		*p = l.next_open;
		l.want = 0;
	}
	while (l.active)
		pthread_cond_wait(&l.done, &mu);
	if (outside) {
		running--;
		counted = 0;
		dispatch_locked();
	}
	pthread_mutex_unlock(&mu);
	pthread_cond_destroy(&l.done);
}

/* ---------------- jobs ---------------- */

void vtask_spawn(vtask_fn *fn, void *arg)
{
	pthread_mutex_lock(&mu);
	if (jobq_tail - jobq_head == jobq_cap) {
		size_t cap = jobq_cap ? 2 * jobq_cap : 256;
		vtask_job *q = malloc(cap * sizeof(*q));

		if (!q) {
			fputs("vtask: out of memory\n", stderr);
			abort();
		}
		for (size_t k = 0; k < jobq_tail - jobq_head; k++)
			q[k] = jobq[(jobq_head + k) & (jobq_cap - 1)];
		jobq_tail -= jobq_head;
		jobq_head = 0;
		free(jobq);
		jobq = q;
		jobq_cap = cap;
	}
	jobq[jobq_tail++ & (jobq_cap - 1)] = (vtask_job){ fn, arg };
	njobs++;
	dispatch_locked();
	pthread_mutex_unlock(&mu);
}

void vtask_wait_idle(void)
{
	pthread_mutex_lock(&mu);
	while (jobq_head != jobq_tail || jobs_running)
		pthread_cond_wait(&idle_jobs, &mu);
	pthread_mutex_unlock(&mu);
}

/* ---------------- blocking ---------------- */

void vtask_blocking_begin(void)
{
	if (blocking_depth++ || !counted)
		return;
	pthread_mutex_lock(&mu);
	running--;
	blocked++;
	blocking_released = true;
	dispatch_locked();
	pthread_mutex_unlock(&mu);
}

void vtask_blocking_end(void)
{
	if (--blocking_depth || !blocking_released)
		return;
	blocking_released = false;
	pthread_mutex_lock(&mu);
	running++;
	blocked--;
	pthread_mutex_unlock(&mu);
}

/* ---------------- the calling thread, counters ---------------- */

int vtask_worker_index(void)
{
	return self ? self->index : -1;
}

vtask_stats vtask_stats_get(void)
{
	vtask_stats s;

	pthread_mutex_lock(&mu);
	s.threads = nthreads;
	s.running = running;
	s.blocked = blocked;
	s.idle = nidle;
	s.wakes = nwakes;
	s.jobs = njobs;
	pthread_mutex_unlock(&mu);
	return s;
}
