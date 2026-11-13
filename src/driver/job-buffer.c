/* The job's growing text buffer (predefines, depfile text): the first 16 MB come from a per-thread region. */
#include "driver-internal.h"

#define BUF_REG (1u << 24)
vjob_region_pool bufpool = VJOB_REGION_POOL_INIT(4);
_Thread_local vjob_region *bufr;	/* this job's (ppd_run) */

VPRIV void bput(buf *b, const char *s, size_t n)
{
	if (b->n + n + 256 > b->cap) {
		size_t nc = b->cap ? b->cap * 2 : BUF_REG;
		char *np;

		while (nc < b->n + n + 256)
			nc *= 2;
		if (!b->cap && b->r && nc == BUF_REG) {
			b->p = vjob_region_acquire(b->r, BUF_REG, "ppdriver.buf");
			b->cap = BUF_REG;
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

VPRIV void define_arg(buf *b, const char *d)
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

VPRIV void bfree(buf *b)
{
	if (b->p && b->r)
		vjob_region_release(b->r, b->n + 256);
	else if (b->p)
		munmap(b->p, b->cap);
	b->p = NULL;
	b->n = b->cap = 0;
}
