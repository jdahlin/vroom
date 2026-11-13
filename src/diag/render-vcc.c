/* Vcc's own text style and its flush. */
#include "../frontend/pp/pp-internal.h"
#include "common/buffer.h"
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * vcc's text diagnostics (PLAN-diag.md D4): the default look.  The first line of
 * each record (and of each note) is clang's, for the tools that parse it; below it:
 *
 *  - every line the record touches (a range over several lines, a label on another
 *    line), "..." for a gap, and the line numbers' gutter as wide as the largest;
 *  - labeled ranges in rustc's layout: '^' the location, '~' a range, '-' a note's
 *    place; the rightmost label after the marks, the others below on '|' lines;
 *  - a note on a line already shown (clang's "to match this '('") folds into the
 *    snippet as a label, and its own snippet is left out (its first line stays);
 *  - "= help: ..." for an action in words;
 *  - tabs and wide characters at their display width (dcols).
 *
 * A record is rendered when the next one starts, or at the end (diag_vcc_flush):
 * its notes are reported after it.  bench/diag-render.py rebuilds this text from
 * the JSON records byte for byte (test layer 5a): change both together.
 *
 * -fdiagnostics-style=narrated says the same in words, one fact per line
 * (miette's narratable mode, for screen readers); =clang is clang's text.
 */

#define DIAG_COLOR_RESET "\033[0m"
#define DIAG_COLOR_MARK "\033[0;1;32m"	/* bold green, clang's caret */
#define DIAG_COLOR_NOTE "\033[0;1;34m"	/* bold blue: a note's mark and label */
#define DIAG_COLOR_FIXIT "\033[0;32m"	/* green */

enum { DIAG_SNIPPET_LINES_MAX = 12, DIAG_MARKS_MAX = 32, DIAG_CONTEXT_LINES = 5 };

typedef struct mark {
	uint32_t b, e;		/* offsets in the file */
	uint32_t lb, le;	/* the starts of its first and last lines */
	const char *label;
	char ch;		/* '^', '~', '-' */
	uint8_t pri;		/* who wins a column: '^' 3, '~' 2, '-' 1 */
} mark;

VPRIV uint32_t lstart(const char *s, uint32_t off)
{
	while (off > 0 && s[off - 1] != '\n')
		off--;
	return off;
}

VPRIV uint32_t lend(const char *s, uint32_t size, uint32_t ls)
{
	uint32_t le = ls;

	while (le < size && s[le] != '\n')
		le++;
	if (le > ls && s[le - 1] == '\r')
		le--;
	return le;
}

/* the start of the line after the one at ls, or size */
static uint32_t diag_line_next_find(const char *s, uint32_t size, uint32_t ls)
{
	while (ls < size && s[ls] != '\n')
		ls++;
	return ls < size ? ls + 1 : size;
}

/* lines from a to b (starts), a <= b */
static uint32_t diag_lines_count(const char *s, uint32_t a, uint32_t b)
{
	uint32_t n = 0;

	for (uint32_t i = a; i < b; i++)
		n += s[i] == '\n';
	return n;
}

static void diag_snippet_line_add(uint32_t *ln, int *n, uint32_t ls)
{
	for (int i = 0; i < *n; i++)
		if (ln[i] == ls)
			return;
	if (*n < DIAG_SNIPPET_LINES_MAX)
		ln[(*n)++] = ls;
}

static int diag_snippet_line_compare(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;

	return x < y ? -1 : x > y;
}

/* a range's mark: in the file of e, else none */
static bool mk(const pp_file_entry *e, uint32_t b, uint32_t en, char ch, const char *label, mark *m)
{
	const char *s = e->f->src;
	uint32_t size = e->f->size;

	if (b < e->base || en < b || b - e->base > size)
		return false;
	b -= e->base, en -= e->base;
	if (en > size)
		en = size;
	if (en == b)
		en = b + 1;	/* a point */
	*m = (mark){ b, en, lstart(s, b), lstart(s, en - 1 < size ? en - 1 : (size ? size - 1 : 0)), label, ch,
		     (uint8_t)(ch == '^' ? 3 : ch == '~' ? 2 : 1) };
	if (m->le < m->lb)
		m->le = m->lb;
	return true;
}

/* a note that can fold into its record's snippet: one place in the same file, near */
static bool foldable(const pp_file_entry *e, const diag_record *k, uint32_t pls)
{
	const char *s = e->f->src;
	uint32_t off, ls;

	if (k->level != DIAG_LEVEL_NOTE || k->nfix || k->help || k->kids || k->loc == UINT32_MAX || k->loc < e->base ||
	    k->loc - e->base >= e->f->size || (k->id && !strcmp(k->id, "note_macro_expansion_here")))
		return false;	/* (a macro's body is not this code: its own snippet) */
	off = k->loc - e->base;
	ls = lstart(s, off);
	for (uint32_t i = 0; i < k->nspan; i++)
		if (k->span[i].b < e->base || k->span[i].e < k->span[i].b || k->span[i].e - e->base > e->f->size ||
		    lstart(s, k->span[i].b - e->base) != ls || k->span[i].label)
			return false;
	return (ls <= pls ? diag_lines_count(s, ls, pls) : diag_lines_count(s, pls, ls)) <= DIAG_CONTEXT_LINES;
}

static void diag_gutter_render(pp *p, vbuffer *b, uint32_t width, const char *num)
{
	uint32_t nd = (uint32_t)strlen(num);

	diag_text_char_repeat(b, ' ', (int)(width - nd + 1));
	vbuffer_string_append(b, num);
	vbuffer_string_append(b, p->o->diag_unicode ? " \xe2\x94\x82 " : " | ");
}

static void bar(pp *p, vbuffer *b, uint32_t width)
{
	diag_text_char_repeat(b, ' ', (int)width + 2);
	vbuffer_string_append(b, p->o->diag_unicode ? "\xe2\x94\x82 " : "| ");
}

/* one source line with its marks, labels and fix-its */
static void diag_vcc_snippet_line_render(pp *p, vbuffer *b, const pp_file_entry *e, const diag_record *d, const mark *m, int nm, uint32_t ls,
		     uint32_t width)
{
	const char *s = e->f->src;
	uint32_t size = e->f->size, le = lend(s, size, ls), lw = diag_columns_width(s, ls, le, 0) + 1;
	char num[16], *row;
	int nlab = 0, lab[DIAG_MARKS_MAX];
	uint32_t anc[DIAG_MARKS_MAX], maxc0 = 0;
	vbuffer fl = { 0 };
	bool anyfix = false;
	uint32_t rn;
	bool inl;

	snprintf(num, sizeof(num), "%u", pp_location_line(p, e->base + ls));
	diag_gutter_render(p, b, width, num);
	for (uint32_t i = ls, col = 0; i < le; i++) {
		if (s[i] == '\t') {
			uint32_t to = (col / 8 + 1) * 8;

			diag_text_char_repeat(b, ' ', (int)(to - col));
			col = to;
		} else {
			uint32_t c1 = diag_columns_width(s, i, i + 1, col);

			vbuffer_append(b, s + i, 1);
			col = c1;
		}
	}
	vbuffer_string_append(b, "\n");
	/* the marks: each column to the mark that wins it */
	row = calloc(lw + 2, 1);
	{
		uint8_t *pri = calloc(lw + 2, 1);

		memset(row, ' ', lw + 1);
		for (int i = 0; i < nm; i++) {
			uint32_t rb, re, c0, c1;

			if (m[i].le < ls || m[i].lb > ls)
				continue;
			rb = m[i].b > ls ? m[i].b : ls;
			re = m[i].e < le ? m[i].e : le;
			c0 = diag_columns_width(s, ls, rb, 0);
			c1 = re > rb ? diag_columns_width(s, ls, re, 0) : c0 + 1;
			if (c1 <= c0)
				c1 = c0 + 1;
			for (uint32_t c = c0; c < c1 && c <= lw; c++)
				if (m[i].pri > pri[c])
					row[c] = m[i].ch, pri[c] = m[i].pri;
			if (c0 > maxc0)
				maxc0 = c0;
			/* a label on its mark's last line, at the mark's first column there */
			if (m[i].label && m[i].le == ls && nlab < DIAG_MARKS_MAX) {
				int j = nlab++;

				while (j > 0 && anc[j - 1] < c0)	/* by column, rightmost first */
					anc[j] = anc[j - 1], lab[j] = lab[j - 1], j--;
				anc[j] = c0, lab[j] = i;
			}
		}
		free(pri);
	}
	rn = lw + 1;
	while (rn > 0 && row[rn - 1] == ' ')
		rn--;
	/* fix-its with text get a line; an insertion widens the caret line as clang's (diag_snippet_is_padded_at) */
	for (uint32_t i = 0; i < d->nfix; i++) {
		uint32_t fb = d->fix[i].b, c0;

		if (fb < e->base || fb - e->base < ls || fb - e->base > le || !*d->fix[i].s)	/* a removal: no text line */
			continue;
		c0 = diag_columns_width(s, ls, fb - e->base, 0);
		if (c0 > rn && c0 <= lw && d->fix[i].e == fb && diag_snippet_is_padded_at(s, ls, le, fb - e->base))
			rn = c0;
		anyfix = true;
	}
	if (!rn && !anyfix) {	/* a line of context between two shown ones: no marks */
		free(row);
		return;
	}
	bar(p, b, width);
	if (p->color)
		vbuffer_string_append(b, DIAG_COLOR_MARK);
	vbuffer_append(b, row, rn);
	inl = nlab && rn > 0 && anc[0] >= maxc0;
	if (inl) {
		/* the rightmost label after the marks, when its mark is the rightmost */
		const mark *x = &m[lab[0]];

		if (p->color && x->ch == '-')
			vbuffer_string_append(b, DIAG_COLOR_NOTE);
		vbuffer_string_append(b, " ");
		vbuffer_string_append(b, x->label);
	}
	if (p->color)
		vbuffer_string_append(b, DIAG_COLOR_RESET);
	vbuffer_string_append(b, "\n");
	/* the others below, rightmost first: a line of '|' and the label's own line */
	for (int k = inl; k < nlab; k++) {
		for (int pass = 0; pass < 2; pass++) {
			vbuffer r = { 0 };
			uint32_t col = 0;

			for (int j = nlab - 1; j >= k; j--) {
				uint32_t c = anc[j];

				if (col > c || (j > k && c == anc[k]))	/* two labels at one column: one '|' */
					continue;
				diag_text_char_repeat(&r, ' ', (int)(c - col));
				if (pass == 1 && j == k) {
					vbuffer_string_append(&r, m[lab[j]].label);
					break;
				}
				vbuffer_string_append(&r, p->o->diag_unicode ? "\xe2\x94\x82" : "|");
				col = c + 1;
			}
			bar(p, b, width);
			if (p->color)
				vbuffer_string_append(b, m[lab[k]].ch == '-' ? DIAG_COLOR_NOTE : DIAG_COLOR_MARK);
			vbuffer_append(b, r.p, r.n);
			if (p->color)
				vbuffer_string_append(b, DIAG_COLOR_RESET);
			vbuffer_string_append(b, "\n");
			free(r.p);
		}
	}
	/* fix-it hints on this line (as clang) */
	for (uint32_t i = 0; i < d->nfix; i++) {
		uint32_t fb = d->fix[i].b, c0, have;

		if (fb < e->base || fb - e->base < ls || fb - e->base > le || !*d->fix[i].s)
			continue;
		c0 = diag_columns_width(s, ls, fb - e->base, 0);
		have = (uint32_t)fl.n;
		if (c0 < have)
			c0 = have;
		diag_text_char_repeat(&fl, ' ', (int)(c0 - have));
		vbuffer_string_append(&fl, d->fix[i].s);
	}
	if (anyfix) {
		bar(p, b, width);
		if (p->color)
			vbuffer_string_append(b, DIAG_COLOR_FIXIT);
		vbuffer_append(b, fl.p, fl.n);
		if (p->color)
			vbuffer_string_append(b, DIAG_COLOR_RESET);
		vbuffer_string_append(b, "\n");
	}
	free(fl.p);
	free(row);
}

/* the snippet of d (in file e) with the notes in fold[] as labels */
static void diag_vcc_snippet_render(pp *p, vbuffer *b, const pp_file_entry *e, const diag_record *d, diag_record *const *fold, int nfold)
{
	const char *s = e->f->src;
	uint32_t size = e->f->size, off = d->loc - e->base, pls = lstart(s, off);
	mark m[DIAG_MARKS_MAX];
	uint32_t ln[DIAG_SNIPPET_LINES_MAX], width, maxl = 0;
	int nm = 0, n = 0;
	char num[16];

	mk(e, d->loc, d->loc + 1, '^', d->label, &m[nm++]);
	for (uint32_t i = 0; i < d->nspan && nm < DIAG_MARKS_MAX; i++)
		if (mk(e, d->span[i].b, d->span[i].e, '~', d->span[i].label, &m[nm]))
			nm++;
	for (uint32_t i = 0; i < d->nfix && nm < DIAG_MARKS_MAX; i++) {	/* a fix-it's replaced range, blanks trimmed */
		uint32_t rb = d->fix[i].b, re = d->fix[i].e;

		if (rb < e->base || re <= rb || re - e->base > size)
			continue;
		while (rb < re && (s[rb - e->base] == ' ' || s[rb - e->base] == '\t'))
			rb++;
		while (re > rb && (s[re - 1 - e->base] == ' ' || s[re - 1 - e->base] == '\t'))
			re--;
		if (re > rb && mk(e, rb, re, '~', NULL, &m[nm]))
			m[nm].pri = 1, nm++;
	}
	for (int i = 0; i < nfold && nm < DIAG_MARKS_MAX; i++) {
		const diag_record *k = fold[i];

		if (mk(e, k->loc, k->loc + 1, '-', k->msg, &m[nm]))
			nm++;
		for (uint32_t j = 0; j < k->nspan && nm < DIAG_MARKS_MAX; j++)
			if (mk(e, k->span[j].b, k->span[j].e, '-', NULL, &m[nm]))
				nm++;
	}
	/* the lines: the location's first, then each mark's (a long range: its first two and its last) */
	diag_snippet_line_add(ln, &n, pls);
	for (int i = 0; i < nm; i++) {
		uint32_t nl = diag_lines_count(s, m[i].lb, m[i].le), x = m[i].lb;

		if (diag_lines_count(s, m[i].lb < pls ? m[i].lb : pls, m[i].lb < pls ? pls : m[i].lb) > 4 * DIAG_CONTEXT_LINES &&
		    diag_lines_count(s, m[i].le < pls ? m[i].le : pls, m[i].le < pls ? pls : m[i].le) > 4 * DIAG_CONTEXT_LINES)
			continue;	/* far away: not drawn */
		for (uint32_t k = 0; k <= nl && k < 4; k++, x = diag_line_next_find(s, size, x))
			if (nl < 4 || k < 2)
				diag_snippet_line_add(ln, &n, x);
		diag_snippet_line_add(ln, &n, m[i].le);
	}
	qsort(ln, (size_t)n, sizeof(ln[0]), diag_snippet_line_compare);
	for (int i = 0; i < n; i++) {
		uint32_t l = pp_location_line(p, e->base + ln[i]);

		if (l > maxl)
			maxl = l;
	}
	width = (uint32_t)snprintf(num, sizeof(num), "%u", maxl + 16);
	if (width < 4)
		width = 4;
	for (int i = 0; i < n; i++) {
		if (i) {
			uint32_t gap = diag_lines_count(s, ln[i - 1], ln[i]);

			if (gap == 2) {	/* one line between: shown, not "..." */
				diag_vcc_snippet_line_render(p, b, e, d, m, nm, diag_line_next_find(s, size, ln[i - 1]), width);
			} else if (gap > 2) {
				diag_text_char_repeat(b, ' ', (int)width - 2);
				vbuffer_string_append(b, "...\n");
			}
		}
		diag_vcc_snippet_line_render(p, b, e, d, m, nm, ln[i], width);
	}
	for (const char *a = d->art, *z; a && *a; a = *z ? z + 1 : z) {
		z = strchr(a, '\n');
		if (!z)
			z = a + strlen(a);
		diag_text_char_repeat(b, ' ', (int)width + 2);
		vbuffer_append(b, a, (size_t)(z - a));
		vbuffer_string_append(b, "\n");
	}
	if (d->help) {
		diag_text_char_repeat(b, ' ', (int)width + 2);
		vbuffer_string_append(b, "= help: ");
		vbuffer_string_append(b, d->help);
		vbuffer_string_append(b, "\n");
	}
	/* whole lines a fix-it inserts away from the shown ones (an #include): as a patch line,
	   "  2 + #include <stddef.h>" (rustc's "consider importing") */
	for (uint32_t i = 0; i < d->nfix; i++) {
		const diag_record_fixit *f = &d->fix[i];
		uint32_t fo, fl = 0;
		bool shown = false;

		if (f->b != f->e || f->b < e->base || f->b - e->base > size || !(fl = (uint32_t)strlen(f->s)) ||
		    f->s[fl - 1] != '\n' || memchr(f->s, '\n', fl - 1) || lstart(s, f->b - e->base) != f->b - e->base)
			continue;
		fo = f->b - e->base;
		for (int j = 0; j < n; j++)
			shown |= ln[j] == fo;
		if (shown)
			continue;
		snprintf(num, sizeof(num), "%u", pp_location_line(p, f->b));
		diag_text_char_repeat(b, ' ', (int)width - (int)strlen(num));
		vbuffer_string_append(b, num);
		vbuffer_string_append(b, " + ");
		if (p->color)
			vbuffer_string_append(b, DIAG_COLOR_FIXIT);
		vbuffer_append(b, f->s, fl - 1);
		if (p->color)
			vbuffer_string_append(b, DIAG_COLOR_RESET);
		vbuffer_string_append(b, "\n");
	}
}

/* a record without a source line: its help still shows */
static void diag_vcc_help_render(vbuffer *b, const diag_record *d)
{
	if (d->help) {
		vbuffer_string_append(b, "      = help: ");
		vbuffer_string_append(b, d->help);
		vbuffer_string_append(b, "\n");
	}
}

static bool diag_note_is_macro(const diag_record *k)
{
	return k->id && !strcmp(k->id, "note_macro_expansion_here");
}

static bool diag_note_is_skipped(const diag_record *k)
{
	return k->level == DIAG_LEVEL_NOTE && k->loc == UINT32_MAX && k->msg && !strncmp(k->msg, "(skipping ", 10);
}

/* a macro backtrace of three frames or more (a run of "expanded from macro" notes, clang's
   "skipping" note among them) starting at k: its last note, else NULL.  The frames between
   keep their first line (tools read it) but not their snippets */
static const diag_record *diag_note_macro_run_last(const diag_record *k)
{
	const diag_record *q, *last = k;
	int nm = 0;

	if (!diag_note_is_macro(k))
		return NULL;
	for (q = k; q && (diag_note_is_macro(q) || diag_note_is_skipped(q)); q = q->next)
		if (diag_note_is_macro(q))
			nm++, last = q;
	return nm >= 3 ? last : NULL;
}

static void diag_vcc_record_render(pp *p, vbuffer *b, diag_record *top)
{
	const pp_file_entry *e;
	diag_record *fold[DIAG_MARKS_MAX];
	const diag_record *run_first = NULL, *run_last = NULL;
	int nfold = 0;

	if (diag_head_render(p, b, top, &e)) {
		uint32_t pls = lstart(e->f->src, top->loc - e->base);

		for (diag_record *k = top->kids; k && nfold < DIAG_MARKS_MAX; k = k->next)
			if (!k->emitted && foldable(e, k, pls))
				fold[nfold++] = k;
		diag_vcc_snippet_render(p, b, e, top, fold, nfold);
		diag_fixits_parseable_render(p, b, top);
		for (int i = 0; i < nfold; i++)
			diag_fixits_parseable_render(p, b, fold[i]);
	} else {
		diag_vcc_help_render(b, top);
	}
	top->emitted = 1;
	for (diag_record *k = top->kids; k; k = k->next) {
		bool folded = false;

		if (k->emitted)
			continue;
		for (int i = 0; i < nfold; i++)
			folded |= fold[i] == k;
		k->emitted = 1;
		if (!run_last && !folded && (run_last = diag_note_macro_run_last(k)))
			run_first = k;
		if (diag_head_render(p, b, k, &e) && !folded && (!run_last || k == run_first || k == run_last)) {
			diag_vcc_snippet_render(p, b, e, k, NULL, 0);
			diag_fixits_parseable_render(p, b, k);
		} else if (!folded)
			diag_vcc_help_render(b, k);
		if (k == run_last)
			run_last = NULL;
	}
}

/* ---------------- narrated: the same facts in words ---------------- */

VPRIV void diag_vcc_flush(pp *p)
{
	int fd = p->o->errfd ? p->o->errfd : 2;
	diag_record *d = p->diagnostic_pending_text;
	vbuffer b = { 0 };

	if (!d)
		return;
	p->diagnostic_pending_text = NULL;
	diag_vcc_record_render(p, &b, d);
	(void)!write(fd, b.p, b.n);
	free(b.p);
}

/* d and its notes as the text output prints them, for JSON's "rendered" (nothing is
   marked emitted: JSON and SARIF print no text) */
VPRIV void diag_record_render_plain(pp *p, vbuffer *b, diag_record *d)
{
	if (p->o->diag_style == DIAG_STYLE_VCC) {
		diag_vcc_record_render(p, b, d);
		d->emitted = 0;
		for (diag_record *k = d->kids; k; k = k->next)
			k->emitted = 0;
		return;
	}
	for (diag_record *k = d; k; k = k == d ? d->kids : k->next)
		if (p->o->diag_style == DIAG_STYLE_NARRATED)
			diag_narrated_record_render(p, b, k);
		else
			diag_clang_record_render(p, b, k);
}

/* d and its notes in the vcc style with colour codes, whatever the text style (the HTML page) */
VPRIV void diag_record_render_color(pp *p, vbuffer *b, diag_record *d)
{
	int color = p->color;

	d->emitted = 0;	/* (the text output has printed them) */
	for (diag_record *k = d->kids; k; k = k->next)
		k->emitted = 0;
	p->color = 1;
	diag_vcc_record_render(p, b, d);
	p->color = color;
	d->emitted = 0;
	for (diag_record *k = d->kids; k; k = k->next)
		k->emitted = 0;
}

/* the narrated style renders at once (no folding) */
