/* Vcc's own text style and its flush. */
#include "../frontend/pp/ppimpl.h"
#include "common/buf.h"
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
 * A record is rendered when the next one starts, or at the end (diag_text_flush):
 * its notes are reported after it.  bench/diag-render.py rebuilds this text from
 * the JSON records byte for byte (test layer 5a): change both together.
 *
 * -fdiagnostics-style=narrated says the same in words, one fact per line
 * (miette's narratable mode, for screen readers); =clang is clang's text.
 */

#define DC_RESET "\033[0m"
#define DC_MARK "\033[0;1;32m"	/* bold green, clang's caret */
#define DC_NOTE "\033[0;1;34m"	/* bold blue: a note's mark and label */
#define DC_FIXIT "\033[0;32m"	/* green */

enum { MAXLINES = 12, MAXMARKS = 32, CONTEXT = 5 };

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
static uint32_t lnext(const char *s, uint32_t size, uint32_t ls)
{
	while (ls < size && s[ls] != '\n')
		ls++;
	return ls < size ? ls + 1 : size;
}

/* lines from a to b (starts), a <= b */
static uint32_t lcount(const char *s, uint32_t a, uint32_t b)
{
	uint32_t n = 0;

	for (uint32_t i = a; i < b; i++)
		n += s[i] == '\n';
	return n;
}

static void add_line(uint32_t *ln, int *n, uint32_t ls)
{
	for (int i = 0; i < *n; i++)
		if (ln[i] == ls)
			return;
	if (*n < MAXLINES)
		ln[(*n)++] = ls;
}

static int cmp_line(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;

	return x < y ? -1 : x > y;
}

/* a range's mark: in the file of e, else none */
static bool mk(const fent *e, uint32_t b, uint32_t en, char ch, const char *label, mark *m)
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
static bool foldable(const fent *e, const vdiag *k, uint32_t pls)
{
	const char *s = e->f->src;
	uint32_t off, ls;

	if (k->level != VDL_NOTE || k->nfix || k->help || k->kids || k->loc == UINT32_MAX || k->loc < e->base ||
	    k->loc - e->base >= e->f->size || (k->id && !strcmp(k->id, "note_macro_expansion_here")))
		return false;	/* (a macro's body is not this code: its own snippet) */
	off = k->loc - e->base;
	ls = lstart(s, off);
	for (uint32_t i = 0; i < k->nspan; i++)
		if (k->span[i].b < e->base || k->span[i].e < k->span[i].b || k->span[i].e - e->base > e->f->size ||
		    lstart(s, k->span[i].b - e->base) != ls || k->span[i].label)
			return false;
	return (ls <= pls ? lcount(s, ls, pls) : lcount(s, pls, ls)) <= CONTEXT;
}

static void gutter(pp *p, vbuf *b, uint32_t width, const char *num)
{
	uint32_t nd = (uint32_t)strlen(num);

	db_c(b, ' ', (int)(width - nd + 1));
	vbuf_s(b, num);
	vbuf_s(b, p->o->diag_unicode ? " \xe2\x94\x82 " : " | ");
}

static void bar(pp *p, vbuf *b, uint32_t width)
{
	db_c(b, ' ', (int)width + 2);
	vbuf_s(b, p->o->diag_unicode ? "\xe2\x94\x82 " : "| ");
}

/* one source line with its marks, labels and fix-its */
static void one_line(pp *p, vbuf *b, const fent *e, const vdiag *d, const mark *m, int nm, uint32_t ls,
		     uint32_t width)
{
	const char *s = e->f->src;
	uint32_t size = e->f->size, le = lend(s, size, ls), lw = dcols(s, ls, le, 0) + 1;
	char num[16], *row;
	int nlab = 0, lab[MAXMARKS];
	uint32_t anc[MAXMARKS], maxc0 = 0;
	vbuf fl = { 0 };
	bool anyfix = false;
	uint32_t rn;
	bool inl;

	snprintf(num, sizeof(num), "%u", loc_line(p, e->base + ls));
	gutter(p, b, width, num);
	for (uint32_t i = ls, col = 0; i < le; i++) {
		if (s[i] == '\t') {
			uint32_t to = (col / 8 + 1) * 8;

			db_c(b, ' ', (int)(to - col));
			col = to;
		} else {
			uint32_t c1 = dcols(s, i, i + 1, col);

			vbuf_put(b, s + i, 1);
			col = c1;
		}
	}
	vbuf_s(b, "\n");
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
			c0 = dcols(s, ls, rb, 0);
			c1 = re > rb ? dcols(s, ls, re, 0) : c0 + 1;
			if (c1 <= c0)
				c1 = c0 + 1;
			for (uint32_t c = c0; c < c1 && c <= lw; c++)
				if (m[i].pri > pri[c])
					row[c] = m[i].ch, pri[c] = m[i].pri;
			if (c0 > maxc0)
				maxc0 = c0;
			/* a label on its mark's last line, at the mark's first column there */
			if (m[i].label && m[i].le == ls && nlab < MAXMARKS) {
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
	/* fix-its with text get a line; an insertion widens the caret line as clang's (pp_pad_at) */
	for (uint32_t i = 0; i < d->nfix; i++) {
		uint32_t fb = d->fix[i].b, c0;

		if (fb < e->base || fb - e->base < ls || fb - e->base > le || !*d->fix[i].s)	/* a removal: no text line */
			continue;
		c0 = dcols(s, ls, fb - e->base, 0);
		if (c0 > rn && c0 <= lw && d->fix[i].e == fb && pp_pad_at(s, ls, le, fb - e->base))
			rn = c0;
		anyfix = true;
	}
	if (!rn && !anyfix) {	/* a line of context between two shown ones: no marks */
		free(row);
		return;
	}
	bar(p, b, width);
	if (p->color)
		vbuf_s(b, DC_MARK);
	vbuf_put(b, row, rn);
	inl = nlab && rn > 0 && anc[0] >= maxc0;
	if (inl) {
		/* the rightmost label after the marks, when its mark is the rightmost */
		const mark *x = &m[lab[0]];

		if (p->color && x->ch == '-')
			vbuf_s(b, DC_NOTE);
		vbuf_s(b, " ");
		vbuf_s(b, x->label);
	}
	if (p->color)
		vbuf_s(b, DC_RESET);
	vbuf_s(b, "\n");
	/* the others below, rightmost first: a line of '|' and the label's own line */
	for (int k = inl; k < nlab; k++) {
		for (int pass = 0; pass < 2; pass++) {
			vbuf r = { 0 };
			uint32_t col = 0;

			for (int j = nlab - 1; j >= k; j--) {
				uint32_t c = anc[j];

				if (col > c || (j > k && c == anc[k]))	/* two labels at one column: one '|' */
					continue;
				db_c(&r, ' ', (int)(c - col));
				if (pass == 1 && j == k) {
					vbuf_s(&r, m[lab[j]].label);
					break;
				}
				vbuf_s(&r, p->o->diag_unicode ? "\xe2\x94\x82" : "|");
				col = c + 1;
			}
			bar(p, b, width);
			if (p->color)
				vbuf_s(b, m[lab[k]].ch == '-' ? DC_NOTE : DC_MARK);
			vbuf_put(b, r.p, r.n);
			if (p->color)
				vbuf_s(b, DC_RESET);
			vbuf_s(b, "\n");
			free(r.p);
		}
	}
	/* fix-it hints on this line (as clang) */
	for (uint32_t i = 0; i < d->nfix; i++) {
		uint32_t fb = d->fix[i].b, c0, have;

		if (fb < e->base || fb - e->base < ls || fb - e->base > le || !*d->fix[i].s)
			continue;
		c0 = dcols(s, ls, fb - e->base, 0);
		have = (uint32_t)fl.n;
		if (c0 < have)
			c0 = have;
		db_c(&fl, ' ', (int)(c0 - have));
		vbuf_s(&fl, d->fix[i].s);
	}
	if (anyfix) {
		bar(p, b, width);
		if (p->color)
			vbuf_s(b, DC_FIXIT);
		vbuf_put(b, fl.p, fl.n);
		if (p->color)
			vbuf_s(b, DC_RESET);
		vbuf_s(b, "\n");
	}
	free(fl.p);
	free(row);
}

/* the snippet of d (in file e) with the notes in fold[] as labels */
static void snippet_vcc(pp *p, vbuf *b, const fent *e, const vdiag *d, vdiag *const *fold, int nfold)
{
	const char *s = e->f->src;
	uint32_t size = e->f->size, off = d->loc - e->base, pls = lstart(s, off);
	mark m[MAXMARKS];
	uint32_t ln[MAXLINES], width, maxl = 0;
	int nm = 0, n = 0;
	char num[16];

	mk(e, d->loc, d->loc + 1, '^', d->label, &m[nm++]);
	for (uint32_t i = 0; i < d->nspan && nm < MAXMARKS; i++)
		if (mk(e, d->span[i].b, d->span[i].e, '~', d->span[i].label, &m[nm]))
			nm++;
	for (uint32_t i = 0; i < d->nfix && nm < MAXMARKS; i++) {	/* a fix-it's replaced range, blanks trimmed */
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
	for (int i = 0; i < nfold && nm < MAXMARKS; i++) {
		const vdiag *k = fold[i];

		if (mk(e, k->loc, k->loc + 1, '-', k->msg, &m[nm]))
			nm++;
		for (uint32_t j = 0; j < k->nspan && nm < MAXMARKS; j++)
			if (mk(e, k->span[j].b, k->span[j].e, '-', NULL, &m[nm]))
				nm++;
	}
	/* the lines: the location's first, then each mark's (a long range: its first two and its last) */
	add_line(ln, &n, pls);
	for (int i = 0; i < nm; i++) {
		uint32_t nl = lcount(s, m[i].lb, m[i].le), x = m[i].lb;

		if (lcount(s, m[i].lb < pls ? m[i].lb : pls, m[i].lb < pls ? pls : m[i].lb) > 4 * CONTEXT &&
		    lcount(s, m[i].le < pls ? m[i].le : pls, m[i].le < pls ? pls : m[i].le) > 4 * CONTEXT)
			continue;	/* far away: not drawn */
		for (uint32_t k = 0; k <= nl && k < 4; k++, x = lnext(s, size, x))
			if (nl < 4 || k < 2)
				add_line(ln, &n, x);
		add_line(ln, &n, m[i].le);
	}
	qsort(ln, (size_t)n, sizeof(ln[0]), cmp_line);
	for (int i = 0; i < n; i++) {
		uint32_t l = loc_line(p, e->base + ln[i]);

		if (l > maxl)
			maxl = l;
	}
	width = (uint32_t)snprintf(num, sizeof(num), "%u", maxl + 16);
	if (width < 4)
		width = 4;
	for (int i = 0; i < n; i++) {
		if (i) {
			uint32_t gap = lcount(s, ln[i - 1], ln[i]);

			if (gap == 2) {	/* one line between: shown, not "..." */
				one_line(p, b, e, d, m, nm, lnext(s, size, ln[i - 1]), width);
			} else if (gap > 2) {
				db_c(b, ' ', (int)width - 2);
				vbuf_s(b, "...\n");
			}
		}
		one_line(p, b, e, d, m, nm, ln[i], width);
	}
	for (const char *a = d->art, *z; a && *a; a = *z ? z + 1 : z) {
		z = strchr(a, '\n');
		if (!z)
			z = a + strlen(a);
		db_c(b, ' ', (int)width + 2);
		vbuf_put(b, a, (size_t)(z - a));
		vbuf_s(b, "\n");
	}
	if (d->help) {
		db_c(b, ' ', (int)width + 2);
		vbuf_s(b, "= help: ");
		vbuf_s(b, d->help);
		vbuf_s(b, "\n");
	}
	/* whole lines a fix-it inserts away from the shown ones (an #include): as a patch line,
	   "  2 + #include <stddef.h>" (rustc's "consider importing") */
	for (uint32_t i = 0; i < d->nfix; i++) {
		const vdfix *f = &d->fix[i];
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
		snprintf(num, sizeof(num), "%u", loc_line(p, f->b));
		db_c(b, ' ', (int)width - (int)strlen(num));
		vbuf_s(b, num);
		vbuf_s(b, " + ");
		if (p->color)
			vbuf_s(b, DC_FIXIT);
		vbuf_put(b, f->s, fl - 1);
		if (p->color)
			vbuf_s(b, DC_RESET);
		vbuf_s(b, "\n");
	}
}

/* a record without a source line: its help still shows */
static void help_only(vbuf *b, const vdiag *d)
{
	if (d->help) {
		vbuf_s(b, "      = help: ");
		vbuf_s(b, d->help);
		vbuf_s(b, "\n");
	}
}

static bool is_macro_note(const vdiag *k)
{
	return k->id && !strcmp(k->id, "note_macro_expansion_here");
}

static bool is_skip_note(const vdiag *k)
{
	return k->level == VDL_NOTE && k->loc == UINT32_MAX && k->msg && !strncmp(k->msg, "(skipping ", 10);
}

/* a macro backtrace of three frames or more (a run of "expanded from macro" notes, clang's
   "skipping" note among them) starting at k: its last note, else NULL.  The frames between
   keep their first line (tools read it) but not their snippets */
static const vdiag *macro_run_last(const vdiag *k)
{
	const vdiag *q, *last = k;
	int nm = 0;

	if (!is_macro_note(k))
		return NULL;
	for (q = k; q && (is_macro_note(q) || is_skip_note(q)); q = q->next)
		if (is_macro_note(q))
			nm++, last = q;
	return nm >= 3 ? last : NULL;
}

static void render_vcc(pp *p, vbuf *b, vdiag *top)
{
	const fent *e;
	vdiag *fold[MAXMARKS];
	const vdiag *run_first = NULL, *run_last = NULL;
	int nfold = 0;

	if (render_head(p, b, top, &e)) {
		uint32_t pls = lstart(e->f->src, top->loc - e->base);

		for (vdiag *k = top->kids; k && nfold < MAXMARKS; k = k->next)
			if (!k->emitted && foldable(e, k, pls))
				fold[nfold++] = k;
		snippet_vcc(p, b, e, top, fold, nfold);
		parseable_fixits(p, b, top);
		for (int i = 0; i < nfold; i++)
			parseable_fixits(p, b, fold[i]);
	} else {
		help_only(b, top);
	}
	top->emitted = 1;
	for (vdiag *k = top->kids; k; k = k->next) {
		bool folded = false;

		if (k->emitted)
			continue;
		for (int i = 0; i < nfold; i++)
			folded |= fold[i] == k;
		k->emitted = 1;
		if (!run_last && !folded && (run_last = macro_run_last(k)))
			run_first = k;
		if (render_head(p, b, k, &e) && !folded && (!run_last || k == run_first || k == run_last)) {
			snippet_vcc(p, b, e, k, NULL, 0);
			parseable_fixits(p, b, k);
		} else if (!folded)
			help_only(b, k);
		if (k == run_last)
			run_last = NULL;
	}
}

/* ---------------- narrated: the same facts in words ---------------- */

VPRIV void diag_text_flush(pp *p)
{
	int fd = p->o->errfd ? p->o->errfd : 2;
	vdiag *d = p->tpend;
	vbuf b = { 0 };

	if (!d)
		return;
	p->tpend = NULL;
	render_vcc(p, &b, d);
	(void)!write(fd, b.p, b.n);
	free(b.p);
}

/* d and its notes as the text output prints them, for JSON's "rendered" (nothing is
   marked emitted: JSON and SARIF print no text) */
VPRIV void diag_render_rec(pp *p, vbuf *b, vdiag *d)
{
	if (p->o->diag_style == PP_DSTYLE_VCC) {
		render_vcc(p, b, d);
		d->emitted = 0;
		for (vdiag *k = d->kids; k; k = k->next)
			k->emitted = 0;
		return;
	}
	for (vdiag *k = d; k; k = k == d ? d->kids : k->next)
		if (p->o->diag_style == PP_DSTYLE_NARRATED)
			render_narr(p, b, k);
		else
			render_text(p, b, k);
}

/* d and its notes in the vcc style with colour codes, whatever the text style (the HTML page) */
VPRIV void diag_render_color(pp *p, vbuf *b, vdiag *d)
{
	int color = p->color;

	d->emitted = 0;	/* (the text output has printed them) */
	for (vdiag *k = d->kids; k; k = k->next)
		k->emitted = 0;
	p->color = 1;
	render_vcc(p, b, d);
	p->color = color;
	d->emitted = 0;
	for (vdiag *k = d->kids; k; k = k->next)
		k->emitted = 0;
}

/* the narrated style renders at once (no folding) */
