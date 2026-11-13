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
	int active;
	unsigned gen;
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
	unsigned seen = 0;
	for (;;) {
		{
			VLOCK(plock);
			while (job.gen == seen)
				pthread_cond_wait(&pcv, &plock);
			seen = job.gen;
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

/* fn(arg, i) for i in [0, n), dynamically scheduled over the pool */
static void pfor(size_t n, pfn fn, void *arg)
{
	if (n == 0)
		return;
	if (nthreads <= 1 || n == 1) {
		for (size_t i = 0; i < n; i++)
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
		atomic_store(&job.next, 0);
		job.active = pool_started - 1;
		job.gen++;
		pthread_cond_broadcast(&pcv);
	}
	run_job();
	VLOCK(plock);
	while (job.active)
		pthread_cond_wait(&pdone, &plock);
}
