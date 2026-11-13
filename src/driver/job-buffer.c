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

VPRIV void driver_buffer_append(driver_buffer *b, const char *s, size_t n)
{
	if (b->length + n + 256 > b->capacity) {
		size_t nc = b->capacity ? b->capacity * 2 : DRIVER_BUFFER_REGION_SIZE;
		char *np;

		while (nc < b->length + n + 256)
			nc *= 2;
		if (!b->capacity && b->region && nc == DRIVER_BUFFER_REGION_SIZE) {
			b->data = vjob_region_acquire(b->region, DRIVER_BUFFER_REGION_SIZE, "ppdriver.buf");
			b->capacity = DRIVER_BUFFER_REGION_SIZE;
			goto put;
		}
		np = mmap(NULL, nc, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
		if (np == MAP_FAILED)
			abort();
		if (b->data) {
			memcpy(np, b->data, b->length);
			if (b->region)
				vjob_region_release(b->region, b->length);
			else
				munmap(b->data, b->capacity);
		}
		b->region = NULL;
		b->data = np;
		b->capacity = nc;
	}
put:
	memcpy(b->data + b->length, s, n);
	b->length += n;
}

VPRIV void driver_buffer_append_string(driver_buffer *b, const char *s) { driver_buffer_append(b, s, strlen(s)); }

VPRIV void driver_buffer_append_define(driver_buffer *b, const char *d)
{
	const char *eq = strchr(d, '=');

	driver_buffer_append_string(b, "#define ");
	if (eq) {
		driver_buffer_append(b, d, (size_t)(eq - d));
		driver_buffer_append_string(b, " ");
		driver_buffer_append_string(b, eq + 1);
	} else {
		driver_buffer_append_string(b, d);
		driver_buffer_append_string(b, " 1");
	}
	driver_buffer_append_string(b, "\n");
}

VPRIV void driver_buffer_free(driver_buffer *b)
{
	if (b->data && b->region)
		vjob_region_release(b->region, b->length + 256);
	else if (b->data)
		munmap(b->data, b->capacity);
	b->data = NULL;
	b->length = b->capacity = 0;
}
