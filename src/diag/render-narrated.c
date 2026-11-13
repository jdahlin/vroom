/* The narrated style: each record told as sentences. */
#include "../frontend/pp/ppimpl.h"
#include "common/buf.h"
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static void say_place(pp *p, vbuf *b, const char *what, uint32_t loc)
{
	const char *name;
	uint32_t nl, line, col;

	if (diag_where(p, loc, &name, &nl, &line, &col))
		vbuf_f(b, "%s line %u, column %u", what, line, col);
}

VPRIV void render_narr(pp *p, vbuf *b, const vdiag *d)
{
	const fent *e;

	if (render_head(p, b, d, &e)) {
		const char *s = e->f->src;
		uint32_t ls = lstart(s, d->loc - e->base), le = lend(s, e->f->size, ls);

		vbuf_f(b, "  the source line is: ");
		while (ls < le && (s[ls] == ' ' || s[ls] == '\t'))
			ls++;
		for (uint32_t i = ls; i < le; i++)
			vbuf_put(b, s[i] == '\t' ? " " : s + i, 1);
		vbuf_s(b, "\n");
		for (uint32_t i = 0; i < d->nspan; i++) {
			if (!d->span[i].label)
				continue;
			vbuf_s(b, "  ");
			say_place(p, b, "at", d->span[i].b);
			vbuf_f(b, ": %s\n", d->span[i].label);
		}
		if (d->label) {
			vbuf_s(b, "  ");
			say_place(p, b, "at", d->loc);
			vbuf_f(b, ": %s\n", d->label);
		}
	}
	for (uint32_t i = 0; i < d->nfix; i++) {
		const vdfix *f = &d->fix[i];

		vbuf_s(b, "  fix: ");
		if (f->b == f->e)
			vbuf_f(b, "insert '%s' ", f->s);
		else if (!*f->s)
			vbuf_s(b, "remove the text ");
		else
			vbuf_f(b, "replace the text with '%s' ", f->s);
		say_place(p, b, "at", f->b);
		vbuf_s(b, "\n");
	}
	if (d->help)
		vbuf_f(b, "  help: %s\n", d->help);
}

/* the pending record (and its notes) to the error stream */

VPRIV void diag_narrate(pp *p, vbuf *b, vdiag *d)
{
	render_narr(p, b, d);
}
