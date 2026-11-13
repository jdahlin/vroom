/*
 * job-buffer.c: the growing text buffer (struct buf) the job builds its predefines, command-line and
 * include lists and dependency text in.
 *
 * A buffer starts in the job's per-thread region (4 slots in driver_buffer_pool, taken by driver_job_run_one, 16 MB each,
 * driver_buffer_region[0..3]) and moves to an anonymous mapping when it outgrows it; driver_buffer_free gives back whichever it
 * holds.  A buffer is only ever used by the job's own thread.  Allocation failure aborts: a job has
 * no way to go on without its text.
 */
#include "driver-internal.h"

#define DRIVER_BUFFER_REGION_SIZE (1u << 24)
vjob_region_pool driver_buffer_pool = VJOB_REGION_POOL_INIT(4);
_Thread_local vjob_region *driver_buffer_region;	/* this job's (driver_job_run) */

VPRIV void bput(buf *b, const char *s, size_t n)
{
	if (b->n + n + 256 > b->cap) {
		size_t nc = b->cap ? b->cap * 2 : DRIVER_BUFFER_REGION_SIZE;
		char *np;

		while (nc < b->n + n + 256)
			nc *= 2;
		if (!b->cap && b->r && nc == DRIVER_BUFFER_REGION_SIZE) {
			b->p = vjob_region_acquire(b->r, DRIVER_BUFFER_REGION_SIZE, "ppdriver.buf");
			b->cap = DRIVER_BUFFER_REGION_SIZE;
			goto put;
		}
		np = mmap(NULL, nc, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
		if (np == MAP_FAILED)
			abort();
		if (b->p) {
			memcpy(np, b->p, b->n);
			if (b->r)
				vjob_region_release(b->r, b->n);
			else
				munmap(b->p, b->cap);
		}
		b->r = NULL;
		b->p = np;
		b->cap = nc;
	}
put:
	memcpy(b->p + b->n, s, n);
	b->n += n;
}

VPRIV void bstr(buf *b, const char *s) { bput(b, s, strlen(s)); }

VPRIV void driver_buffer_append_define(buf *b, const char *d)
{
	const char *eq = strchr(d, '=');

	bstr(b, "#define ");
	if (eq) {
		bput(b, d, (size_t)(eq - d));
		bstr(b, " ");
		bstr(b, eq + 1);
	} else {
		bstr(b, d);
		bstr(b, " 1");
	}
	bstr(b, "\n");
}

VPRIV void driver_buffer_free(buf *b)
{
	if (b->p && b->r)
		vjob_region_release(b->r, b->n + 256);
	else if (b->p)
		munmap(b->p, b->cap);
	b->p = NULL;
	b->n = b->cap = 0;
}
