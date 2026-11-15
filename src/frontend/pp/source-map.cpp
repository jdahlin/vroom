/* The source map: a location's file entry, presumed file and line, system-header and main-file tests. */
#include "../pp/pp-internal.h"
#include "common/buffer.h"
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

VPRIV const pp_file_entry *pp_location_file_entry(const pp *p, uint32_t loc)
{
	uint32_t lo = 0, hi = p->file_count;

	while (hi - lo > 1) {
		uint32_t mid = (lo + hi) / 2;

		if (p->files[mid].location_base <= loc)
			lo = mid;
		else
			hi = mid;
	}
	while (lo > 0 && !p->files[lo].lex_file)	/* a dependency-only entry (pp_dependency_add) */
		lo--;
	return p->file_count ? &p->files[lo] : NULL;
}

VPRIV uint32_t pp_location_line(const pp *p, uint32_t loc)
{
	const pp_file_entry *e = pp_location_file_entry(p, loc);
	uint32_t l, fe;

	if (!e)
		return 0;
	l = pp_file_line_of_offset(e->lex_file, e->skeleton, loc - e->location_base);
	fe = (uint32_t)(e - p->files);
	for (uint32_t i = p->line_mark_count; i-- > 0;)
		if (p->line_marks[i].file == fe && p->line_marks[i].offset <= loc - e->location_base)
			return p->line_marks[i].line + (l - p->line_marks[i].physical_line);
	return l;
}

/* the presumed file name at loc in file context fc (#line can change it) */
VPRIV const char *pp_location_presumed_name(const pp *p, uint32_t fe, uint32_t loc, uint32_t *len)
{
	const pp_file_entry *e = &p->files[fe];

	for (uint32_t i = p->line_mark_count; i-- > 0;)
		if (p->line_marks[i].file == fe && p->line_marks[i].offset <= loc - e->location_base && p->line_marks[i].name) {
			*len = p->line_marks[i].name_length;
			return p->line_marks[i].name;
		}
	*len = e->name_length;
	return e->name;
}

int pp_location_is_in_system_header(const pp *p, uint32_t loc)
{
	const pp_file_entry *e = pp_location_file_entry(p, loc);

	return e && loc - e->location_base <= e->lex_file->size && e->is_system;
}

int pp_location_is_in_main_file(const pp *p, uint32_t loc)
{
	const pp_file_entry *e = pp_location_file_entry(p, loc);

	return e && e->lex_file == p->files[0].lex_file && loc - e->location_base <= e->lex_file->size;
}

int pp_location_describe(const pp *p, uint32_t loc, const char **name, uint32_t *namelen, uint32_t *line, uint32_t *col)
{
	const pp_file_entry *e = pp_location_file_entry(p, loc);
	uint32_t off, b;

	if (!e || loc - e->location_base > e->lex_file->size) {
		*name = "<built-in>", *namelen = 10, *line = *col = 0;
		return 0;
	}
	off = loc - e->location_base;
	for (b = off; b > 0 && e->lex_file->src[b - 1] != '\n'; b--)
		;
	*line = pp_location_line(p, loc);
	*col = off - b + 1;
	*name = pp_location_presumed_name(p, (uint32_t)(e - p->files), loc, namelen);
	return 1;
}

/* ---------------- diagnostics in clang's text format ---------------- */

VPRIV bool pp_location_is_valid(const pp *p, uint32_t loc, const pp_file_entry **e)
{
	*e = pp_location_file_entry(p, loc);
	return *e && loc >= (*e)->location_base && loc - (*e)->location_base <= (*e)->lex_file->size;
}
