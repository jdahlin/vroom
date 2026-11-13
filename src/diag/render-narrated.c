/* The narrated style: each record told as sentences. */
#include "../frontend/pp/ppimpl.h"
#include "common/buffer.h"
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static void say_place(pp *p, vbuffer *b, const char *what, uint32_t loc)
{
	const char *name;
	uint32_t nl, line, col;

	if (diag_where(p, loc, &name, &nl, &line, &col))
		vbuffer_format_append(b, "%s line %u, column %u", what, line, col);
}

VPRIV void render_narr(pp *p, vbuffer *b, const vdiag *d)
{
	const fent *e;

	if (render_head(p, b, d, &e)) {
		const char *s = e->f->src;
		uint32_t ls = lstart(s, d->loc - e->base), le = lend(s, e->f->size, ls);

		vbuffer_format_append(b, "  the source line is: ");
		while (ls < le && (s[ls] == ' ' || s[ls] == '\t'))
			ls++;
		for (uint32_t i = ls; i < le; i++)
			vbuffer_append(b, s[i] == '\t' ? " " : s + i, 1);
		vbuffer_string_append(b, "\n");
		for (uint32_t i = 0; i < d->nspan; i++) {
			if (!d->span[i].label)
				continue;
			vbuffer_string_append(b, "  ");
			say_place(p, b, "at", d->span[i].b);
			vbuffer_format_append(b, ": %s\n", d->span[i].label);
		}
		if (d->label) {
			vbuffer_string_append(b, "  ");
			say_place(p, b, "at", d->loc);
			vbuffer_format_append(b, ": %s\n", d->label);
		}
	}
	for (uint32_t i = 0; i < d->nfix; i++) {
		const vdfix *f = &d->fix[i];

		vbuffer_string_append(b, "  fix: ");
		if (f->b == f->e)
			vbuffer_format_append(b, "insert '%s' ", f->s);
		else if (!*f->s)
			vbuffer_string_append(b, "remove the text ");
		else
			vbuffer_format_append(b, "replace the text with '%s' ", f->s);
		say_place(p, b, "at", f->b);
		vbuffer_string_append(b, "\n");
	}
	if (d->help)
		vbuffer_format_append(b, "  help: %s\n", d->help);
}

/* the pending record (and its notes) to the error stream */

VPRIV void diag_narrate(pp *p, vbuffer *b, vdiag *d)
{
	render_narr(p, b, d);
}
