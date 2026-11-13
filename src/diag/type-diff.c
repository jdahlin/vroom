/* The type diff: two types with the differing parts marked. */
#include "../frontend/pp/ppimpl.h"
#include "common/buf.h"
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define DC_OLD "\033[0;1;32m"	/* type diff: the part only the first type has */
#define DC_NEW "\033[0;1;35m"	/* ... and the second */

/* ---------------- type diff (colour only) ---------------- */

/* the message with the parts in which its two quoted types differ highlighted
   (GCC 15's contrasting colours): the tokens of the first that are not in the
   second, and the other way round, after their common prefix and suffix */
VPRIV void diag_typediff(vbuf *b, const vdiag *d, const char *plain)
{
	const char *m = d->msg, *q[4];
	int nq = 0;

	for (const char *x = m; *x && nq < 5; x++)
		if (*x == '\'') {
			if (nq == 4) {
				nq = 5;
				break;
			}
			q[nq++] = x;
		}
	if (nq != 4 || !d->fmt || !strstr(d->fmt, "'%s'") || (!strstr(d->fmt, "type") && !strstr(d->fmt, "incompatible"))) {
		vbuf_s(b, m);
		return;
	}
	{
		const char *a = q[0] + 1, *ae = q[1], *c = q[2] + 1, *ce = q[3];
		size_t pre = 0, suf = 0, na = (size_t)(ae - a), nc = (size_t)(ce - c);

		if (na == nc && !memcmp(a, c, na)) {
			vbuf_s(b, m);
			return;
		}
		while (pre < na && pre < nc && a[pre] == c[pre])
			pre++;
		while (pre && (isalnum((unsigned char)a[pre - 1]) || a[pre - 1] == '_'))	/* whole words */
			pre--;
		while (suf < na - pre && suf < nc - pre && a[na - 1 - suf] == c[nc - 1 - suf])
			suf++;
		while (suf && (isalnum((unsigned char)a[na - suf]) || a[na - suf] == '_'))
			suf--;
		vbuf_put(b, m, (size_t)(a - m) + pre);
		vbuf_s(b, DC_OLD);
		vbuf_put(b, a + pre, na - pre - suf);
		vbuf_s(b, plain);
		vbuf_put(b, a + na - suf, (size_t)(c - (a + na - suf)) + pre);
		vbuf_s(b, DC_NEW);
		vbuf_put(b, c + pre, nc - pre - suf);
		vbuf_s(b, plain);
		vbuf_s(b, c + nc - suf);
	}
}
