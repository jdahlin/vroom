/* The source map: a location's file entry, presumed file and line, system-header and main-file tests. */
#include "../pp/pp-internal.h"
#include "common/buffer.h"
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

VPRIV const pp_file_entry *loc_fent(const pp *p, uint32_t loc)
{
	uint32_t lo = 0, hi = p->nfe;

	while (hi - lo > 1) {
		uint32_t mid = (lo + hi) / 2;

		if (p->fe[mid].base <= loc)
			lo = mid;
		else
			hi = mid;
	}
	while (lo > 0 && !p->fe[lo].f)	/* a dependency-only entry (pp_dependency_add) */
		lo--;
	return p->nfe ? &p->fe[lo] : NULL;
}

VPRIV uint32_t loc_line(const pp *p, uint32_t loc)
{
	const pp_file_entry *e = loc_fent(p, loc);
	uint32_t l, fe;

	if (!e)
		return 0;
	l = pp_file_line_of_offset(e->f, e->fi, loc - e->base);
	fe = (uint32_t)(e - p->fe);
	for (uint32_t i = p->nlm; i-- > 0;)
		if (p->lm[i].fe == fe && p->lm[i].off <= loc - e->base)
			return p->lm[i].line + (l - p->lm[i].pline);
	return l;
}

/* the presumed file name at loc in file context fc (#line can change it) */
VPRIV const char *presumed_name(const pp *p, uint32_t fe, uint32_t loc, uint32_t *len)
{
	const pp_file_entry *e = &p->fe[fe];

	for (uint32_t i = p->nlm; i-- > 0;)
		if (p->lm[i].fe == fe && p->lm[i].off <= loc - e->base && p->lm[i].name) {
			*len = p->lm[i].namelen;
			return p->lm[i].name;
		}
	*len = e->namelen;
	return e->name;
}

int pp_in_system_header(const pp *p, uint32_t loc)
{
	const pp_file_entry *e = loc_fent(p, loc);

	return e && loc - e->base <= e->f->size && e->sys;
}

int pp_in_main_file(const pp *p, uint32_t loc)
{
	const pp_file_entry *e = loc_fent(p, loc);

	return e && e->f == p->fe[0].f && loc - e->base <= e->f->size;
}

int pp_where(const pp *p, uint32_t loc, const char **name, uint32_t *namelen, uint32_t *line, uint32_t *col)
{
	const pp_file_entry *e = loc_fent(p, loc);
	uint32_t off, b;

	if (!e || loc - e->base > e->f->size) {
		*name = "<built-in>", *namelen = 10, *line = *col = 0;
		return 0;
	}
	off = loc - e->base;
	for (b = off; b > 0 && e->f->src[b - 1] != '\n'; b--)
		;
	*line = loc_line(p, loc);
	*col = off - b + 1;
	*name = presumed_name(p, (uint32_t)(e - p->fe), loc, namelen);
	return 1;
}

/* ---------------- diagnostics in clang's text format ---------------- */

VPRIV bool loc_ok(const pp *p, uint32_t loc, const pp_file_entry **e)
{
	*e = loc_fent(p, loc);
	return *e && loc >= (*e)->base && loc - (*e)->base <= (*e)->f->size;
}
