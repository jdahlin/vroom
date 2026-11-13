/* The narrated style: each record told as sentences. */
#include "../frontend/pp/pp-internal.h"
#include "common/buffer.h"
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static void diag_narrated_place_say(pp *p, vbuffer *b, const char *what, uint32_t loc)
{
	const char *name;
	uint32_t nl, line, col;

	if (diag_location_presumed(p, loc, &name, &nl, &line, &col))
		vbuffer_format_append(b, "%s line %u, column %u", what, line, col);
}

VPRIV void diag_narrated_record_render(pp *p, vbuffer *b, const diag_record *d)
{
	const pp_file_entry *e;

	if (diag_head_render(p, b, d, &e)) {
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
			diag_narrated_place_say(p, b, "at", d->span[i].b);
			vbuffer_format_append(b, ": %s\n", d->span[i].label);
		}
		if (d->label) {
			vbuffer_string_append(b, "  ");
			diag_narrated_place_say(p, b, "at", d->loc);
			vbuffer_format_append(b, ": %s\n", d->label);
		}
	}
	for (uint32_t i = 0; i < d->nfix; i++) {
		const diag_record_fixit *f = &d->fix[i];

		vbuffer_string_append(b, "  fix: ");
		if (f->b == f->e)
			vbuffer_format_append(b, "insert '%s' ", f->s);
		else if (!*f->s)
			vbuffer_string_append(b, "remove the text ");
		else
			vbuffer_format_append(b, "replace the text with '%s' ", f->s);
		diag_narrated_place_say(p, b, "at", f->b);
		vbuffer_string_append(b, "\n");
	}
	if (d->help)
		vbuffer_format_append(b, "  help: %s\n", d->help);
}

/* the pending record (and its notes) to the error stream */

VPRIV void diag_narrated_render(pp *p, vbuffer *b, diag_record *d)
{
	diag_narrated_record_render(p, b, d);
}
