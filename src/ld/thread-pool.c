/* The worker threads and pfor(), the parallel loop the linker's passes run on. */

/* ---------------- thread pool ---------------- */

typedef void (*pfn)(void *arg, size_t i);
static int nthreads = 1;
static int pool_started;
static pthread_t pool[64];
static pthread_mutex_t plock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pcv = PTHREAD_COND_INITIALIZER, pdone = PTHREAD_COND_INITIALIZER;
static struct {
	pfn fn;
	void *arg;
	size_t n;
	atomic_size_t next;
	int tickets;	/* workers still to join the current job */
	int active;	/* workers that joined and are not done */
} job;

static void run_job(void)
{
	size_t i;
	while ((i = atomic_fetch_add_explicit(&job.next, 1, memory_order_relaxed)) < job.n)
		job.fn(job.arg, i);
}

static void *worker(void *unused)
{
	(void)unused;
	for (;;) {
		{
			VLOCK(plock);
			while (job.tickets == 0)
				pthread_cond_wait(&pcv, &plock);
			job.tickets--;
		}
		run_job();
		{
			VLOCK(plock);
			if (--job.active == 0)
				pthread_cond_signal(&pdone);
		}
	}
	return NULL;
}

static uint64_t pf_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

/* Waking a worker costs about as much as 50 us of work, and a call whose items
   are all tiny (a link of four small objects) must not pay for the pool at
   all: the caller runs items itself for PF_PROBE_NS first, which also measures
   the cost of an item; only if work is left then, it wakes as many workers as
   the remaining work keeps busy for PF_WORKER_NS each (and never more than
   there are items left). */
#define PF_PROBE_NS 30000u
#define PF_WORKER_NS 60000u

/* fn(arg, i) for i in [0, n), dynamically scheduled over the pool */
static void pfor(size_t n, pfn fn, void *arg)
{
	size_t i = 0;
	uint64_t t0, el = 0;
	int nw;

	if (n == 0)
		return;
	if (nthreads <= 1 || n == 1) {
		for (; i < n; i++)
			fn(arg, i);
		return;
	}
	t0 = pf_ns();
	do {
		fn(arg, i++);
		el = pf_ns() - t0;
	} while (i < n && el < PF_PROBE_NS);
	if (i == n)
		return;
	{	/* the cost of the items left, by the cost of those run */
		uint64_t left = (uint64_t)(n - i) * (el / i + 1);
		uint64_t w = left / PF_WORKER_NS;

		nw = w > (uint64_t)(nthreads - 1) ? nthreads - 1 : (int)w;
		if ((size_t)nw > n - i - 1)
			nw = (int)(n - i - 1);
	}
	if (nw <= 0) {
		for (; i < n; i++)
			fn(arg, i);
		return;
	}
	if (!pool_started) {
		for (int t = 0; t < nthreads - 1; t++)
			pthread_create(&pool[t], NULL, worker, NULL);
		pool_started = nthreads;
	}
	{
		VLOCK(plock);
		job.fn = fn;
		job.arg = arg;
		job.n = n;
		atomic_store(&job.next, i);
		job.active = job.tickets = nw;
		if (nw >= pool_started - 1)
			pthread_cond_broadcast(&pcv);
		else
			for (int t = 0; t < nw; t++)
				pthread_cond_signal(&pcv);
	}
	run_job();
	VLOCK(plock);
	job.active -= job.tickets;	/* workers that never woke: do not wait for them */
	job.tickets = 0;
	while (job.active)
		pthread_cond_wait(&pdone, &plock);
}
