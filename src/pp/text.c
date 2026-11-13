/* vcc preprocessor: -E text output */
#include "ppimpl.h"

/* ---------------- -E text ---------------- */

/* the -E buffer of this thread's last pp, kept for the next one (no unmap, no page faults) */
_Thread_local char *out_spare;
_Thread_local size_t out_spare_cap;

VPRIV void out_reserve(pp *p, size_t n)
{
	size_t nc;
	char *np;

	if (likely(p->nout + n <= p->capout))
		return;
	if (!p->out && out_spare && out_spare_cap >= n) {
		p->out = out_spare, p->capout = out_spare_cap;
		out_spare = NULL, out_spare_cap = 0;
		return;
	}
	nc = p->capout ? p->capout * 2 : 1u << 24;
	while (nc < p->nout + n)
		nc *= 2;
	np = vm(nc);
	if (p->out) {
		memcpy(np, p->out, p->nout);
		munmap(p->out, p->capout);
	}
	p->out = np;
	p->capout = nc;
}
