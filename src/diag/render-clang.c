/* Clang's text style: colours, include stack, snippet and the text of a record. */
#include "../frontend/pp/pp-internal.h"
#include "common/buffer.h"
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

VPRIV void diag_text_char_repeat(vbuffer *b, char c, int n)
{
	while (n-- > 0)
		vbuffer_append(b, &c, 1);
}

/* clang's colours (TextDiagnostic.cpp) */
#define DIAG_COLOR_RESET "\033[0m"
#define DIAG_COLOR_BOLD "\033[1m"
static const char *dcolor(const char *what)
{
	switch (what[0]) {
	case 'e': case 'f': return "\033[0;1;31m";	/* bold red */
	case 'w': return "\033[0;1;35m";		/* bold magenta */
	case 'r': return "\033[0;1;34m";		/* remark: bold blue */
	default: return "\033[0;1;30m";			/* note: bold black */
	}
}
#define DIAG_COLOR_CARET "\033[0;1;32m"	/* bold green */
#define DIAG_COLOR_FIXIT "\033[0;32m"	/* green */

/* "In file included from" for each level of loc's include stack, outermost first */
/* pp_location_describe with the file's path as spelled (clang's diagnostics ignore -fmacro-prefix-map) */
VPRIV int diag_location_presumed(const pp *p, uint32_t loc, const char **name, uint32_t *nl, uint32_t *line, uint32_t *col)
{
	int r = pp_location_describe(p, loc, name, nl, line, col);
	const pp_file_entry *e = pp_location_file_entry(p, loc);

	if (r && e && e->spath && loc - e->base <= e->f->size && *name == e->name)	/* (not a #line name) */
		*name = e->spath, *nl = (uint32_t)strlen(e->spath);
	return r;
}

VPRIV void diag_include_stack_render(pp *p, vbuffer *b, const pp_file_entry *e)
{
	const pp_file_entry *chain[64];
	int n = 0;

	for (const pp_file_entry *x = e; x && x->from && n < 64; ) {
		const pp_file_entry *y = pp_location_file_entry(p, x->from);

		chain[n++] = x;
		x = y;
	}
	for (int i = n; i-- > 0;) {
		const char *name;
		uint32_t nl, line, col;

		if (diag_location_presumed(p, chain[i]->from, &name, &nl, &line, &col))
			vbuffer_format_append(b, "In file included from %.*s:%u:\n", (int)nl, name, line);
	}
}

/* an insertion at x widens clang's caret line: no blank on either side of x */
VPRIV bool diag_snippet_is_padded_at(const char *s, uint32_t ls, uint32_t le, uint32_t x)
{
	return (x == ls || (s[x - 1] != ' ' && s[x - 1] != '\t')) && (x >= le || (s[x] != ' ' && s[x] != '\t'));
}

/* line start of offset o (in s) */
static uint32_t diag_snippet_line_start(const char *s, uint32_t o)
{
	while (o > 0 && s[o - 1] != '\n')
		o--;
	return o;
}

/* one source line [ls, le) with its marker line: ~ under the ranges' parts on it, ^ at off if
   caret; the marker line only when it has a mark (clang) */
static void diag_snippet_line_render(pp *p, vbuffer *b, const pp_file_entry *e, const diag_record *d, uint32_t ls, uint32_t le, uint32_t line,
		    uint32_t width, bool caret, uint32_t off, vbuffer *cl)
{
	const char *s = e->f->src;
	char num[16];
	uint32_t nd = (uint32_t)snprintf(num, sizeof(num), "%u", line);

	diag_text_char_repeat(b, ' ', (int)(width - nd + 1));
	vbuffer_string_append(b, num);
	vbuffer_string_append(b, " | ");
	for (uint32_t i = ls, col = 0; i < le; i++) {
		if (s[i] == '\t') {
			uint32_t to = (col / 8 + 1) * 8;

			diag_text_char_repeat(b, ' ', (int)(to - col));
			col = to;
		} else {
			vbuffer_append(b, s + i, 1);
			if (((unsigned char)s[i] & 0xc0) != 0x80)
				col++;
		}
	}
	vbuffer_string_append(b, "\n");
	{
		uint32_t lw = diag_columns_width(s, ls, le, 0) + 1, ccol = caret ? diag_columns_width(s, ls, off, 0) : UINT32_MAX;
		char *m = calloc(lw + 1, 1);

		memset(m, ' ', lw);
		for (uint32_t i = 0; i < d->nspan; i++) {
			uint32_t rb = d->span[i].b, re = d->span[i].e;

			if (rb < e->base || re < rb || d->span[i].label)	/* (a label: vcc's style only) */
				continue;
			rb -= e->base, re -= e->base;
			if (re <= ls || rb >= le || (re == ls && rb < ls))
				continue;
			if (rb < ls)
				rb = ls;
			if (re > le)
				re = le;
			/* clang highlights from the first to the last non-blank column */
			while (rb < re && (s[rb] == ' ' || s[rb] == '\t'))
				rb++;
			while (re > rb && (s[re - 1] == ' ' || s[re - 1] == '\t'))
				re--;
			for (uint32_t c0 = diag_columns_width(s, ls, rb, 0), c1 = diag_columns_width(s, ls, re, 0); c0 < c1 && c0 < lw; c0++)
				m[c0] = '~';
		}
		/* clang: a fix-it's replaced range is highlighted too, without its blanks at either end */
		for (uint32_t i = 0; i < d->nfix; i++) {
			uint32_t rb = d->fix[i].b, re = d->fix[i].e;

			if (rb < e->base || re <= rb)
				continue;
			rb -= e->base, re -= e->base;
			if (rb < ls || rb >= le)
				continue;
			if (re > le)
				re = le;
			while (rb < re && (s[rb] == ' ' || s[rb] == '\t'))
				rb++;
			while (re > rb && (s[re - 1] == ' ' || s[re - 1] == '\t'))
				re--;
			for (uint32_t c0 = diag_columns_width(s, ls, rb, 0), c1 = diag_columns_width(s, ls, re, 0); c0 < c1 && c0 < lw; c0++)
				if (m[c0] == ' ')
					m[c0] = '~';
		}
		if (ccol < lw)
			m[ccol] = '^';
		{
			uint32_t n = lw;

			while (n > 0 && m[n - 1] == ' ')
				n--;
			cl->n = 0;
			vbuffer_append(cl, m, n);
			/* clang highlights an insertion's empty range too: the caret line reaches its
			   column unless a blank is next to it (highlightRange trims blanks) */
			for (uint32_t i = 0; caret && i < d->nfix; i++) {
				uint32_t fb = d->fix[i].b, c0;

				if (fb < e->base || fb - e->base < ls || fb - e->base > le || !*d->fix[i].s ||
				    d->fix[i].e != fb || !diag_snippet_is_padded_at(s, ls, le, fb - e->base))
					continue;
				c0 = diag_columns_width(s, ls, fb - e->base, 0);
				if (c0 > cl->n)
					diag_text_char_repeat(cl, ' ', (int)(c0 - cl->n));
			}
		}
		free(m);
	}
	if (cl->n) {
		diag_text_char_repeat(b, ' ', (int)width + 2);
		vbuffer_string_append(b, "| ");
		if (p->color)
			vbuffer_string_append(b, DIAG_COLOR_CARET);
		vbuffer_append(b, cl->p, cl->n);
		if (p->color)
			vbuffer_string_append(b, DIAG_COLOR_RESET);
		vbuffer_string_append(b, "\n");
	}
}

static void diag_snippet_render(pp *p, vbuffer *b, const pp_file_entry *e, const diag_record *d)
{
	const char *s = e->f->src;
	uint32_t off = d->loc - e->base, size = e->f->size, ls, le, line, width, nd;
	char num[16];
	vbuffer cl = { 0 }, fl = { 0 };
	bool anyfix = false;
	uint32_t l0, l1;	/* the lines shown (clang: grown to the ranges, at most 16) */

	for (ls = off; ls > 0 && s[ls - 1] != '\n'; ls--)
		;
	for (le = off; le < size && s[le] != '\n'; le++)
		;
	if (le > ls && s[le - 1] == '\r')
		le--;
	line = pp_location_line(p, d->loc);
	nd = (uint32_t)snprintf(num, sizeof(num), "%u", line + 16);
	width = nd > 4 ? nd : 4;
	l0 = l1 = line;
	for (uint32_t i = 0; i < d->nspan; i++) {
		uint32_t rb = d->span[i].b, re = d->span[i].e, a, z;

		if (rb < e->base || re < rb || re - e->base > size || d->span[i].label)	/* (a label: not a clang range) */
			continue;
		a = pp_location_line(p, rb);
		z = pp_location_line(p, re > rb && s[re - e->base - 1] == '\n' ? re - 1 : re);
		/* clang's maybeAddRange (MaxLines 16) */
		{
			uint32_t mn = a < l0 ? a : l0, mx = z > l1 ? z : l1;

			if (mx - mn + 1 <= 16)
				l0 = mn, l1 = mx;
		}
	}
	{
		/* walk back from the caret line to l0, then print l0 .. l1 */
		uint32_t st = ls;

		for (uint32_t k = line; k > l0 && st > 0; k--)
			st = diag_snippet_line_start(s, st - 1);
		for (uint32_t k = l0; k <= l1 && st <= size; k++) {
			uint32_t en = st;

			while (en < size && s[en] != '\n')
				en++;
			uint32_t ee = en > st && s[en - 1] == '\r' ? en - 1 : en;

			diag_snippet_line_render(p, b, e, d, st, ee, k, width, k == line, off, &cl);
			if (k == line && d->nfix)
				goto fixits;
		after_fix:
			if (en >= size)
				break;
			st = en + 1;
			continue;
		fixits:
			/* fix-it hints on this line */
			for (uint32_t i = 0; i < d->nfix; i++) {
				uint32_t fb = d->fix[i].b;
				uint32_t c0, have;

				if (fb < e->base || fb - e->base < ls || fb - e->base > le || !*d->fix[i].s)	/* a removal: no text line */
					continue;
				c0 = diag_columns_width(s, ls, fb - e->base, 0);
				have = (uint32_t)fl.n;
				if (c0 < have)
					c0 = have;
				diag_text_char_repeat(&fl, ' ', (int)(c0 - have));
				vbuffer_string_append(&fl, d->fix[i].s);
				anyfix = true;
			}
			if (anyfix) {
				diag_text_char_repeat(b, ' ', (int)width + 2);
				vbuffer_string_append(b, "| ");
				if (p->color)
					vbuffer_string_append(b, DIAG_COLOR_FIXIT);
				vbuffer_append(b, fl.p, fl.n);
				if (p->color)
					vbuffer_string_append(b, DIAG_COLOR_RESET);
				vbuffer_string_append(b, "\n");
			}
			goto after_fix;
		}
	}
	free(cl.p);
	free(fl.p);
}

/* a record's first line (clang's, in every style); true and *ep when it has a source line */
VPRIV bool diag_head_render(pp *p, vbuffer *b, const diag_record *d, const pp_file_entry **ep)
{
	const char *name, *what = diag_level_name(d->level);
	uint32_t nl, line, col;
	const pp_file_entry *e = NULL;
	bool has = d->loc != UINT32_MAX && pp_location_is_valid(p, d->loc, &e);	/* UINT32_MAX: no location */
	bool note = d->level == DIAG_LEVEL_NOTE;

	if (has && !note && (uint32_t)(e - p->fe) + 1 != p->diagnostic_last_file)
		diag_include_stack_render(p, b, e);
	if (has && !note)
		p->diagnostic_last_file = (uint32_t)(e - p->fe) + 1;
	if (p->color)
		vbuffer_string_append(b, DIAG_COLOR_BOLD);
	if (has && diag_location_presumed(p, d->loc, &name, &nl, &line, &col))
		vbuffer_format_append(b, "%.*s:%u:%u: ", (int)nl, name, line, col);
	else if (d->loc == UINT32_MAX)
		;	/* "fatal error: too many errors emitted" has no location */
	else if (!has)
		vbuffer_string_append(b, "vcc: ");
	if (p->color)
		vbuffer_string_append(b, dcolor(what));
	vbuffer_string_append(b, what);
	vbuffer_string_append(b, ": ");
	if (p->color)
		vbuffer_string_append(b, note ? DIAG_COLOR_RESET : DIAG_COLOR_RESET DIAG_COLOR_BOLD);
	if (p->color && p->o->diag_style != DIAG_STYLE_CLANG)
		diag_type_diff_render(b, d, note ? DIAG_COLOR_RESET : DIAG_COLOR_RESET DIAG_COLOR_BOLD);
	else
		vbuffer_string_append(b, d->msg);
	if (d->flag && p->color)	/* OSC 8: the flag links to clang's reference */
		vbuffer_format_append(b, " [%s\033]8;;https://clang.llvm.org/doc/DiagnosticsReference.html#w%s\033\\-W%s\033]8;;\033\\]",
		       d->werror ? "-Werror," : "", d->flag, d->flag);
	else if (d->flag)
		vbuffer_format_append(b, " [%s-W%s]", d->werror ? "-Werror," : "", d->flag);
	if (p->o->diag_show_id) {
		const char *docs = getenv("VCC_DIAG_DOCS");	/* where `vcc --explain` pages are (D6) */
		const char *id = d->id ? d->id : "vcc_unknown";

		if (p->color && docs && *docs)
			vbuffer_format_append(b, " [\033]8;;%s/%s.md\033\\%s\033]8;;\033\\]", docs, id, id);
		else
			vbuffer_format_append(b, " [%s]", id);
	}
	if (p->diagnostic_track)
		diag_report_site_text(b, d->track);
	if (p->color)
		vbuffer_string_append(b, DIAG_COLOR_RESET);
	vbuffer_string_append(b, "\n");
	*ep = e;
	return has && !p->o->diag_nocaret && d->loc - e->base < e->f->size;
}

/* -fdiagnostics-parseable-fixits: a line per fix-it after the record's snippet, as clang
   (TextDiagnostic::emitParseableFixits): 1-based byte columns, the end exclusive, the file
   name and the text escaped as LLVM's write_escaped */
static void esc(vbuffer *b, const char *s, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		unsigned char ch = (unsigned char)s[i];

		if (ch == '\\' || ch == '"')
			vbuffer_format_append(b, "\\%c", ch);
		else if (ch == '\t')
			vbuffer_string_append(b, "\\t");
		else if (ch == '\n')
			vbuffer_string_append(b, "\\n");
		else if (ch < 0x20 || ch >= 0x7f)
			vbuffer_format_append(b, "\\%03o", ch);
		else
			vbuffer_append(b, s + i, 1);
	}
}

VPRIV void diag_fixits_parseable_render(pp *p, vbuffer *b, const diag_record *d)
{
	if (likely(!p->o->diag_parseable))
		return;
	for (uint32_t i = 0; i < d->nfix; i++) {
		const char *nb, *ne;
		uint32_t lb, le, l1, c1, l2, c2;

		if (!diag_location_presumed(p, d->fix[i].b, &nb, &lb, &l1, &c1) || !diag_location_presumed(p, d->fix[i].e, &ne, &le, &l2, &c2) ||
		    lb != le || memcmp(nb, ne, lb))
			continue;
		vbuffer_string_append(b, "fix-it:\"");
		esc(b, nb, lb);
		vbuffer_format_append(b, "\":{%u:%u-%u:%u}:\"", l1, c1, l2, c2);
		esc(b, d->fix[i].s, strlen(d->fix[i].s));
		vbuffer_string_append(b, "\"\n");
	}
}

/* one record in clang's text format */
VPRIV void diag_clang_record_render(pp *p, vbuffer *b, const diag_record *d)
{
	const pp_file_entry *e;
	/* clang (DiagnosticRenderer): no snippet again for a record at the previous one's
	   location without ranges or fix-its, unless it follows a note and is no note */
	bool same = d->loc + 1 == p->diagnostic_previous_location && !d->nspan && !d->nfix && !(p->diagnostic_previous_level == DIAG_LEVEL_NOTE && d->level != DIAG_LEVEL_NOTE);

	p->diagnostic_previous_location = d->loc + 1, p->diagnostic_previous_level = d->level;
	if (diag_head_render(p, b, d, &e) && !same) {
		diag_snippet_render(p, b, e, d);
		diag_fixits_parseable_render(p, b, d);
	}
}

VPRIV void diag_clang_render(pp *p, vbuffer *b, diag_record *d) { diag_clang_record_render(p, b, d); }
