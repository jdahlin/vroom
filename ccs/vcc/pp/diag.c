/* vcc preprocessor: diagnostics in clang's text format */
#include "ppimpl.h"
#include "../lib/buf.h"
#include <dlfcn.h>

/* ---------------- diagnostics ---------------- */

VPRIV const fent *loc_fent(const pp *p, uint32_t loc)
{
	uint32_t lo = 0, hi = p->nfe;

	while (hi - lo > 1) {
		uint32_t mid = (lo + hi) / 2;

		if (p->fe[mid].base <= loc)
			lo = mid;
		else
			hi = mid;
	}
	return p->nfe ? &p->fe[lo] : NULL;
}

VPRIV uint32_t loc_line(const pp *p, uint32_t loc)
{
	const fent *e = loc_fent(p, loc);
	uint32_t l, fe;

	if (!e)
		return 0;
	l = file_line(e->f, e->fi, loc - e->base);
	fe = (uint32_t)(e - p->fe);
	for (uint32_t i = p->nlm; i-- > 0;)
		if (p->lm[i].fe == fe && p->lm[i].off <= loc - e->base)
			return p->lm[i].line + (l - p->lm[i].pline);
	return l;
}

/* the presumed file name at loc in file context fc (#line can change it) */
VPRIV const char *presumed_name(const pp *p, uint32_t fe, uint32_t loc, uint32_t *len)
{
	const fent *e = &p->fe[fe];

	for (uint32_t i = p->nlm; i-- > 0;)
		if (p->lm[i].fe == fe && p->lm[i].off <= loc - e->base && p->lm[i].name) {
			*len = p->lm[i].namelen;
			return p->lm[i].name;
		}
	*len = e->namelen;
	return e->name;
}

void pp_vdiag(pp *p, uint32_t loc, const char *what, const char *fmt, va_list ap);

__attribute__((format(printf, 4, 5)))
VPRIV void pp_diag(pp *p, uint32_t loc, const char *what, const char *fmt, ...)
{
	pp_diag_pc = __builtin_return_address(0);
	va_list ap;

	va_start(ap, fmt);
	pp_vdiag(p, loc, what, fmt, ap);
	va_end(ap);
}

void pp_diagx(pp *p, const pp_dinfo *d, const char *fmt, ...)
{
	pp_diag_pc = __builtin_return_address(0);
	va_list ap;

	va_start(ap, fmt);
	pp_vdiagx(p, d, fmt, ap);
	va_end(ap);
}

/* ---------------- exported for the parser ---------------- */

int pp_in_system_header(const pp *p, uint32_t loc)
{
	const fent *e = loc_fent(p, loc);

	return e && loc - e->base <= e->f->size && e->sys;
}

int pp_in_main_file(const pp *p, uint32_t loc)
{
	const fent *e = loc_fent(p, loc);

	return e && e->f == p->fe[0].f && loc - e->base <= e->f->size;
}

int pp_where(const pp *p, uint32_t loc, const char **name, uint32_t *namelen, uint32_t *line, uint32_t *col)
{
	const fent *e = loc_fent(p, loc);
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

VPRIV void db_c(vbuf *b, char c, int n)
{
	while (n-- > 0)
		vbuf_put(b, &c, 1);
}

/* clang's colours (TextDiagnostic.cpp) */
#define DC_RESET "\033[0m"
#define DC_BOLD "\033[1m"
static const char *dcolor(const char *what)
{
	switch (what[0]) {
	case 'e': case 'f': return "\033[0;1;31m";	/* bold red */
	case 'w': return "\033[0;1;35m";		/* bold magenta */
	case 'r': return "\033[0;1;34m";		/* remark: bold blue */
	default: return "\033[0;1;30m";			/* note: bold black */
	}
}
#define DC_CARET "\033[0;1;32m"	/* bold green */
#define DC_FIXIT "\033[0;32m"	/* green */

VPRIV bool loc_ok(const pp *p, uint32_t loc, const fent **e)
{
	*e = loc_fent(p, loc);
	return *e && loc >= (*e)->base && loc - (*e)->base <= (*e)->f->size;
}

int pp_error_limit(const pp *p) { return p->o->error_limit; }
int pp_spell_limit(const pp *p) { return p->o->spell_limit; }

uint32_t pp_main_eof(const pp *p)
{
	const fent *e = &p->fe[0];
	uint32_t n = e->f->size;

	return e->base + (n && e->f->src[n - 1] == '\n' ? n - 1 : n);
}

/* where an #include for a diagnostic at loc goes: the start of the line after the last
   #include before loc in loc's file, else the file's first line; UINT32_MAX: not in a file */
uint32_t pp_include_point(const pp *p, uint32_t loc)
{
	const fent *e;
	const char *s;
	uint32_t end, at = 0, i = 0;

	if (!loc_ok(p, loc, &e))
		return UINT32_MAX;
	s = e->f->src;
	end = loc - e->base;
	while (i < end) {
		uint32_t j = i;

		while (j < end && (s[j] == ' ' || s[j] == '\t'))
			j++;
		if (j < end && s[j] == '#') {
			j++;
			while (j < end && (s[j] == ' ' || s[j] == '\t'))
				j++;
			if (end - j > 7 && !memcmp(s + j, "include", 7)) {
				const char *nl = memchr(s + j, '\n', end - j);

				if (!nl)
					break;
				at = (uint32_t)(nl - s) + 1;
			}
		}
		{
			const char *nl = memchr(s + i, '\n', end - i);

			if (!nl)
				break;
			i = (uint32_t)(nl - s) + 1;
		}
	}
	return e->base + at;
}

/* ---------------- #pragma clang/GCC diagnostic ---------------- */

enum { PD_IGNORED = 1, PD_WARNING, PD_ERROR };

/* a new state at loc (the current one + node n, or a popped one) */
static void dtrans(pp *p, uint32_t st, uint32_t loc)
{
	if (!p->dtr)
		p->dtr = varena_alloc(&p->perm, 4096 * sizeof(*p->dtr));
	if (p->ndtr < 4096)
		p->dtr[p->ndtr].loc = loc, p->dtr[p->ndtr++].st = st;
	p->dcur = st;
}

/* "push", "pop", "ignored|warning|error|fatal \"-Wflag\"" (after "clang diagnostic") */
VPRIV void diag_pragma(pp *p, const char *q, const char *e, uint32_t loc)
{
	static const char *const acts[] = { "ignored", "warning", "error", "fatal" };
	size_t n;
	int act = 0;

	while (q < e && (*q == ' ' || *q == '\t'))
		q++;
	for (n = 0; q + n < e && ((q[n] >= 'a' && q[n] <= 'z') || q[n] == '_'); n++)
		;
	if (n == 4 && !memcmp(q, "push", 4)) {
		if (p->ndstk < 64)
			p->dstk[p->ndstk++] = p->dcur;
		return;
	}
	if (n == 3 && !memcmp(q, "pop", 3)) {
		if (p->ndstk)	/* (clang: "pragma diagnostic pop could not pop, no matching push") */
			dtrans(p, p->dstk[--p->ndstk], loc);
		return;
	}
	for (int i = 0; i < 4; i++)
		if (strlen(acts[i]) == n && !memcmp(q, acts[i], n))
			act = i < 2 ? i + 1 : PD_ERROR;
	if (!act)
		return;
	q += n;
	while (q < e && (*q == ' ' || *q == '\t'))
		q++;
	if (q + 3 >= e || q[0] != '"' || q[1] != '-' || q[2] != 'W')
		return;
	q += 3;
	for (n = 0; q + n < e && q[n] != '"'; n++)
		;
	if (n >= sizeof(p->dn->name) || (n > 3 && !memcmp(q, "no-", 3)))
		return;
	if (!p->dn) {
		p->dn = varena_alloc(&p->perm, 4096 * sizeof(*p->dn));
		memset(&p->dn[0], 0, sizeof(*p->dn));
		p->ndn = 1;
	}
	if (p->ndn == 4096)
		return;
	p->dn[p->ndn].prev = p->dcur;
	p->dn[p->ndn].act = (uint8_t)act;
	memcpy(p->dn[p->ndn].name, q, n);
	p->dn[p->ndn].name[n] = 0;
	dtrans(p, p->ndn++, loc);
}

/* the state at loc: the last transition before it in its file, else the one its file was
   entered in (clang's DiagStateMap) */
static uint32_t dstate_at(const pp *p, uint32_t loc)
{
	const fent *e = loc_fent(p, loc);

	if (!e || loc - e->base > e->f->size)
		return p->dcur;
	for (uint32_t i = p->ndtr; i-- > 0;)
		if (p->dtr[i].loc >= e->base && p->dtr[i].loc < loc)
			return p->dtr[i].st;
	return e->dstate;
}

/* leaving file fe: a state it changed holds in the includer from the #include on (clang) */
VPRIV void diag_file_end(pp *p, uint32_t fe)
{
	const fent *e = &p->fe[fe];

	if (e->from && p->dcur != dstate_at(p, e->from + 1))
		dtrans(p, p->dcur, e->from + 1);
}

/* the command line's st (0 off, 1 warning, 2 error) under the pragmas in force at loc;
   clang: "warning" is a warning, also under -Werror and -Werror=flag */
static int dpragma(const pp *p, int st, const char *flag, const char *const *groups, uint32_t loc)
{
	for (uint32_t s = dstate_at(p, loc); s; s = p->dn[s].prev) {
		const char *n = p->dn[s].name;
		bool hit = !strcmp(n, flag) || !strcmp(n, "everything");

		for (int g = 0; !hit && groups && groups[g]; g++)
			hit = !strcmp(n, groups[g]);
		if (hit)
			return p->dn[s].act == PD_IGNORED ? 0 : p->dn[s].act == PD_ERROR ? 2 : 1;
	}
	return st;
}

int pp_diag_npragma(const pp *p)
{
	return (int)p->ndtr;
}

/* the command line's state of flag; *w: -w (no pragma turns it on) */
static int wstate(const pp *p, const char *flag, bool on, const char *const *groups, bool *w)
{
	bool err = false, allerr = false, noerr = false;

	*w = false;

	for (int i = 0; i < p->o->nwargs; i++) {
		const char *a = p->o->wargs[i];
		bool no = !strncmp(a, "-Wno-", 5);
		const char *n = a + (no ? 5 : 2);
		bool hit;

		if (!strcmp(a, "-w")) {
			on = false;
			continue;
		}
		if (!strcmp(a, "-Werror")) { allerr = true; continue; }
		if (!strcmp(a, "-Wno-error")) { allerr = false; continue; }
		if (!strncmp(n, "error=", 6)) {
			if (!strcmp(n + 6, flag)) {
				if (no)
					noerr = true, err = false;
				else
					err = true, on = true, noerr = false;
			}
			continue;
		}
		hit = !strcmp(n, flag) || !strcmp(n, "everything");
		for (int g = 0; !hit && groups && groups[g]; g++)
			hit = !strcmp(n, groups[g]);
		if (hit)
			on = !no;
	}
	for (int i = 0; i < p->o->nwargs; i++)
		if (!strcmp(p->o->wargs[i], "-w")) {
			*w = true;
			return 0;
		}
	if (!on)
		return 0;
	return err || (allerr && !noerr) ? 2 : 1;
}

int pp_wstate(const pp *p, const char *flag, bool on, const char *const *groups)
{
	bool w;

	return wstate(p, flag, on, groups, &w);
}

/* the level a warning of flag is reported at at loc: VDL_WARNING, VDL_ERROR (-Werror), or -1 (off) */
int pp_diag_wlevel(pp *p, const char *flag, bool on, const char *const *groups, uint32_t loc)
{
	bool w;
	int st = wstate(p, flag, on, groups, &w);

	if (unlikely(p->ndtr) && loc != UINT32_MAX && !w)
		st = dpragma(p, st, flag, groups, loc);
	if (!st || (loc != UINT32_MAX && pp_in_system_header(p, loc)))
		return -1;
	return st == 2 ? VDL_ERROR : VDL_WARNING;
}

bool pp_warnx(pp *p, const char *flag, bool on, const char *const *groups, pp_dinfo *di, const char *fmt, ...)
{
	pp_diag_pc = __builtin_return_address(0);
	int lv = pp_diag_wlevel(p, flag, on, groups, di->loc);
	vdiag *d;
	va_list ap;

	if (lv < 0)
		return false;
	va_start(ap, fmt);
	d = pp_diag_vbegin(p, lv, di->loc, fmt, ap);
	va_end(ap);
	pp_diag_flag(d, flag);
	d->werror = lv == VDL_ERROR;
	for (int i = 0; i < di->nr; i++)
		pp_diag_label(p, d, di->r[i].b, di->r[i].e, NULL);
	for (int i = 0; i < di->nfx; i++)
		pp_diag_fix(p, d, di->fx[i].b, di->fx[i].e, di->fx[i].s, VDA_UNSPEC);
	pp_diag_macro(d, di->ms, di->mlen);
	pp_diag_emit(p, d);
	return true;
}

const char *pp_src(const pp *p, uint32_t b, uint32_t e, size_t *len)
{
	const fent *f;

	if (e < b || !loc_ok(p, b, &f) || e - f->base > f->f->size)
		return NULL;
	*len = e - b;
	return f->f->src + (b - f->base);
}

const char *pp_src_rest(const pp *p, uint32_t b, size_t *len)
{
	const fent *f;

	if (!loc_ok(p, b, &f))
		return NULL;
	*len = f->f->size - (b - f->base);
	return f->f->src + (b - f->base);
}

/* the range [*b, *e) widened by the '(' and ')' around it (blanks between), when
   both are there in the same file: clang's ParenExpr range, found on the error path */
void pp_widen_parens(const pp *p, uint32_t *b, uint32_t *e)
{
	const fent *f;
	const char *s;
	uint32_t i, j, n;

	if (!loc_ok(p, *b, &f) || *e < *b || *e - f->base > f->f->size)
		return;
	s = f->f->src, n = f->f->size, i = *b - f->base, j = *e - f->base;
	while (i && (s[i - 1] == ' ' || s[i - 1] == '\t' || s[i - 1] == '\n'))
		i--;
	while (j < n && (s[j] == ' ' || s[j] == '\t' || s[j] == '\n'))
		j++;
	if (i && s[i - 1] == '(' && j < n && s[j] == ')')
		*b = f->base + i - 1, *e = f->base + j + 1;
}

uint32_t pp_tok_end(const pp *p, uint32_t loc)
{
	const fent *e;
	const char *s, *end;
	uint32_t off;

	if (!loc_ok(p, loc, &e))
		return loc + 1;
	s = e->f->src, off = loc - e->base, end = s + e->f->size;
	if (s + off >= end)
		return loc + 1;
	{
		const char *q = s + off;
		unsigned char ch = (unsigned char)*q;

		if (ch == '_' || ch == '$' || ch >= 0x80 || (ch | 0x20) - 'a' < 26u || ch - '0' < 10u) {
			/* identifier or pp-number (1e+5, 0x1p-3) */
			bool num = ch - '0' < 10u;

			for (q++; q < end; q++) {
				unsigned char d = (unsigned char)*q;

				if (d == '_' || d == '$' || d >= 0x80 || (d | 0x20) - 'a' < 26u || d - '0' < 10u || (num && d == '.'))
					continue;
				if (num && (d == '+' || d == '-') && strchr("eEpP", q[-1]))
					continue;
				break;
			}
			return loc + (uint32_t)(q - (s + off));
		}
		if (ch == '"' || ch == '\'') {
			for (q++; q < end && *q != (char)ch && *q != '\n'; q++)
				if (*q == '\\')
					q++;
			return loc + (uint32_t)(q + 1 - (s + off));
		}
		{
			static const char *const ops[] = { "<<=", ">>=", "...", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=",
							   "&&", "||", "*=", "/=", "%=", "+=", "-=", "&=", "^=", "|=", "##" };

			for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
				size_t l = strlen(ops[i]);

				if ((size_t)(end - q) >= l && !memcmp(q, ops[i], l))
					return loc + (uint32_t)l;
			}
		}
	}
	return loc + 1;
}

/* the display width of code point c: 2 for East Asian wide and emoji, 0 for combining marks */
static uint32_t cp_width(uint32_t c)
{
	if ((c >= 0x300 && c <= 0x36f) || (c >= 0x200b && c <= 0x200f) || (c >= 0xfe00 && c <= 0xfe0f))
		return 0;
	if ((c >= 0x1100 && c <= 0x115f) || (c >= 0x2e80 && c <= 0xa4cf && c != 0x303f) || (c >= 0xac00 && c <= 0xd7a3) ||
	    (c >= 0xf900 && c <= 0xfaff) || (c >= 0xfe30 && c <= 0xfe4f) || (c >= 0xff00 && c <= 0xff60) ||
	    (c >= 0xffe0 && c <= 0xffe6) || (c >= 0x1f300 && c <= 0x1f64f) || (c >= 0x1f900 && c <= 0x1f9ff) ||
	    (c >= 0x20000 && c <= 0x3fffd))
		return 2;
	return 1;
}

/* display columns of src[b, e) from col: tabs to the next multiple of 8, UTF-8 by its
   code point's width (a malformed byte is one column) */
VPRIV uint32_t dcols(const char *s, uint32_t b, uint32_t e, uint32_t col)
{
	for (uint32_t i = b; i < e; i++) {
		unsigned char ch = (unsigned char)s[i];

		if (ch == '\t') {
			col = (col / 8 + 1) * 8;
		} else if (ch < 0xc0) {
			col += (ch & 0xc0) != 0x80;
		} else {
			int n = ch >= 0xf0 ? 3 : ch >= 0xe0 ? 2 : 1;
			uint32_t c = ch & (0x3f >> n);
			int k;

			for (k = 0; k < n && i + 1 + k < e && ((unsigned char)s[i + 1 + k] & 0xc0) == 0x80; k++)
				c = c << 6 | ((unsigned char)s[i + 1 + k] & 0x3f);
			col += k == n ? cp_width(c) : 1;
			i += (uint32_t)k;
		}
	}
	return col;
}

/* "In file included from" for each level of loc's include stack, outermost first */
/* pp_where with the file's path as spelled (clang's diagnostics ignore -fmacro-prefix-map) */
VPRIV int diag_where(const pp *p, uint32_t loc, const char **name, uint32_t *nl, uint32_t *line, uint32_t *col)
{
	int r = pp_where(p, loc, name, nl, line, col);
	const fent *e = loc_fent(p, loc);

	if (r && e && e->spath && loc - e->base <= e->f->size)
		*name = e->spath, *nl = (uint32_t)strlen(e->spath);
	return r;
}

VPRIV void include_stack(pp *p, vbuf *b, const fent *e)
{
	const fent *chain[64];
	int n = 0;

	for (const fent *x = e; x && x->from && n < 64; ) {
		const fent *y = loc_fent(p, x->from);

		chain[n++] = x;
		x = y;
	}
	for (int i = n; i-- > 0;) {
		const char *name;
		uint32_t nl, line, col;

		if (diag_where(p, chain[i]->from, &name, &nl, &line, &col))
			vbuf_f(b, "In file included from %.*s:%u:\n", (int)nl, name, line);
	}
}

/* an insertion at x widens clang's caret line: no blank on either side of x */
VPRIV bool pp_pad_at(const char *s, uint32_t ls, uint32_t le, uint32_t x)
{
	return (x == ls || (s[x - 1] != ' ' && s[x - 1] != '\t')) && (x >= le || (s[x] != ' ' && s[x] != '\t'));
}

static void snippet(pp *p, vbuf *b, const fent *e, const vdiag *d)
{
	const char *s = e->f->src;
	uint32_t off = d->loc - e->base, size = e->f->size, ls, le, line, width, nd;
	char num[16];
	vbuf cl = { 0 }, fl = { 0 };
	uint32_t ccol;
	bool anyfix = false;

	for (ls = off; ls > 0 && s[ls - 1] != '\n'; ls--)
		;
	for (le = off; le < size && s[le] != '\n'; le++)
		;
	if (le > ls && s[le - 1] == '\r')
		le--;
	line = loc_line(p, d->loc);
	nd = (uint32_t)snprintf(num, sizeof(num), "%u", line + 16);
	width = nd > 4 ? nd : 4;
	nd = (uint32_t)snprintf(num, sizeof(num), "%u", line);
	/* the source line, tabs expanded */
	db_c(b, ' ', (int)(width - nd + 1));
	vbuf_s(b, num);
	vbuf_s(b, " | ");
	for (uint32_t i = ls, col = 0; i < le; i++) {
		if (s[i] == '\t') {
			uint32_t to = (col / 8 + 1) * 8;

			db_c(b, ' ', (int)(to - col));
			col = to;
		} else {
			vbuf_put(b, s + i, 1);
			if (((unsigned char)s[i] & 0xc0) != 0x80)
				col++;
		}
	}
	vbuf_s(b, "\n");
	/* the caret line: ~ under the ranges, ^ at loc */
	ccol = dcols(s, ls, off, 0);
	{
		uint32_t lw = dcols(s, ls, le, 0) + 1;
		char *m = calloc(lw + 1, 1);

		memset(m, ' ', lw);
		for (uint32_t i = 0; i < d->nspan; i++) {
			uint32_t rb = d->span[i].b, re = d->span[i].e;

			if (rb < e->base || re < rb)
				continue;
			rb -= e->base, re -= e->base;
			if (re <= ls || rb >= le)
				continue;
			if (rb < ls)
				rb = ls;
			if (re > le)
				re = le;
			for (uint32_t c0 = dcols(s, ls, rb, 0), c1 = dcols(s, ls, re, 0); c0 < c1 && c0 < lw; c0++)
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
			for (uint32_t c0 = dcols(s, ls, rb, 0), c1 = dcols(s, ls, re, 0); c0 < c1 && c0 < lw; c0++)
				if (m[c0] == ' ')
					m[c0] = '~';
		}
		if (ccol < lw)
			m[ccol] = '^';
		{
			uint32_t n = lw;

			while (n > 0 && m[n - 1] == ' ')
				n--;
			vbuf_put(&cl, m, n);
			/* clang highlights an insertion's empty range too: the caret line reaches its
			   column unless a blank is next to it (highlightRange trims blanks) */
			for (uint32_t i = 0; i < d->nfix; i++) {
				uint32_t fb = d->fix[i].b, c0;

				if (fb < e->base || fb - e->base < ls || fb - e->base > le || !*d->fix[i].s ||
				    d->fix[i].e != fb || !pp_pad_at(s, ls, le, fb - e->base))
					continue;
				c0 = dcols(s, ls, fb - e->base, 0);
				if (c0 > cl.n)
					db_c(&cl, ' ', (int)(c0 - cl.n));
			}
		}
		free(m);
	}
	db_c(b, ' ', (int)width + 2);
	vbuf_s(b, "| ");
	if (p->color)
		vbuf_s(b, DC_CARET);
	vbuf_put(b, cl.p, cl.n);
	if (p->color)
		vbuf_s(b, DC_RESET);
	vbuf_s(b, "\n");
	/* fix-it hints on this line */
	for (uint32_t i = 0; i < d->nfix; i++) {
		uint32_t fb = d->fix[i].b;
		uint32_t c0, have;

		if (fb < e->base || fb - e->base < ls || fb - e->base > le || !*d->fix[i].s)	/* a removal: no text line */
			continue;
		c0 = dcols(s, ls, fb - e->base, 0);
		have = (uint32_t)fl.n;
		if (c0 < have)
			c0 = have;
		db_c(&fl, ' ', (int)(c0 - have));
		vbuf_s(&fl, d->fix[i].s);
		anyfix = true;
	}
	if (anyfix) {
		db_c(b, ' ', (int)width + 2);
		vbuf_s(b, "| ");
		if (p->color)
			vbuf_s(b, DC_FIXIT);
		vbuf_put(b, fl.p, fl.n);
		if (p->color)
			vbuf_s(b, DC_RESET);
		vbuf_s(b, "\n");
	}
	free(cl.p);
	free(fl.p);
}

/* the token of file entry e at offset off, or NULL */
static const vtok *tok_at(const fent *e, uint32_t off, uint32_t *ix)
{
	const vlex_file *f = e->f;
	uint32_t n = f && f->tok ? f->ntok : 0, lo = 0, hi = n;

	while (lo < hi) {
		uint32_t mid = (lo + hi) / 2;

		if (f->tok[mid].off < off)
			lo = mid + 1;
		else
			hi = mid;
	}
	if (lo < n && f->tok[lo].off == off) {
		*ix = lo;
		return &f->tok[lo];
	}
	return NULL;
}

/*
 * A token from a macro expansion carries the expansion's location: the
 * macro's name (object-like) or the invocation's ')' (function-like).  Find
 * the macro and the invocation's range [*b, *ee).
 */
static const pmacro *expansion_at(pp *p, uint32_t loc, uint32_t *b, uint32_t *ee)
{
	const fent *e;
	const vtok *t;
	uint32_t i;
	uintptr_t m;

	if (!loc_ok(p, loc, &e) || !(t = tok_at(e, loc - e->base, &i)))
		return NULL;
	*ee = loc + t->len;
	if (t->kind == T_RPAREN) {
		int depth = 0;

		for (;; i--) {
			if (e->f->tok[i].kind == T_RPAREN)
				depth++;
			else if (e->f->tok[i].kind == T_LPAREN && !--depth)
				break;
			if (!i)
				return NULL;
		}
		if (!i)
			return NULL;
		t = &e->f->tok[i - 1];
	}
	if (t->kind != T_IDENT || !(m = mget(p, t->id) & ~(uintptr_t)1))
		return NULL;
	*b = e->base + t->off;
	return (const pmacro *)m;
}

/* "expanded from macro 'M'" at the body token spelled like the diagnosed one (the last #define before) */
static void kid_after(vdiag *top, vdiag *after, vdiag *n);

static void expansion_note(pp *p, vdiag *top, vdiag *after, const pmacro *m, uint32_t before, const char *ms, uint32_t mlen)
{
	uint32_t loc = 0, len = 0;

	for (uint32_t i = 0; i < p->nfe; i++) {
		const vlex_file *f = p->fe[i].f;
		const vtok *q;

		if (!f || !f->tok)
			continue;
		q = f->tok;
		for (uint32_t k = 2; k < f->ntok; k++) {
			uint32_t b = k + 1, pb = 0, pe = 0, hit = 0, first = 0;

			if (!(q[k].kind == T_IDENT && q[k].id == m->name && q[k - 1].id == KW_define &&
			      q[k - 2].kind == T_HASH && (q[k - 2].flags & TF_SOL) && p->fe[i].base + q[k].off < before))
				continue;
			if (b < f->ntok && q[b].kind == T_LPAREN && !(q[b].flags & TF_SPACE) && !(q[b].flags & TF_SOL)) {
				pb = b + 1;
				while (b < f->ntok && q[b].kind != T_RPAREN && !(q[b].flags & TF_SOL))
					b++;
				pe = b++;
			}
			for (uint32_t t = b; t < f->ntok && !(q[t].flags & TF_SOL); t++) {
				bool param = false;

				if (!first)
					first = t + 1;
				for (uint32_t a = pb; a < pe && q[t].kind == T_IDENT; a++)
					param |= q[a].id == q[t].id;
				if (!param && q[t].len == mlen && !memcmp(f->src + q[t].off, ms, mlen)) {
					hit = t + 1;
					break;
				}
			}
			if (hit || first) {
				uint32_t t = (hit ? hit : first) - 1;

				loc = p->fe[i].base + q[t].off, len = q[t].len;
			}
		}
	}
	if (loc) {
		vdiag *n = pp_diag_begin(p, VDL_NOTE, loc, "expanded from macro '%.*s'",
					 (int)vintern_ents[m->name].len, vintern_ents[m->name].s);

		if (len > 1)
			pp_diag_label(p, n, loc, loc + len, NULL);
		n->id = "note_macro_expansion_here";
		kid_after(top, after, n);
	}
}

/* ---------------- records ---------------- */

/* vcc's formats and their ids (bench/gen-diagids.py) */
static const struct dfmt { const char *fmt, *id; } dfmts[] = {
#define DID(n, cls, grp)
#define DFMT(f, n) { f, #n },
#include "diagids.inc"
#undef DID
#undef DFMT
};

/* the id of a format (NULL: not in the table) */
static const char *fmt_id(const char *fmt)
{
	size_t lo = 0, hi = sizeof(dfmts) / sizeof(dfmts[0]);

	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		int r = strcmp(dfmts[mid].fmt, fmt);

		if (!r)
			return dfmts[mid].id;
		if (r < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return NULL;
}

_Thread_local const void *pp_diag_pc;

static const char *const lvname[] = { "note", "remark", "warning", "error", "fatal error" };

const char *pp_diag_level(int level) { return lvname[level]; }

static int level_of(const char *what)
{
	switch (what[0]) {
	case 'e': return VDL_ERROR;
	case 'f': return VDL_FATAL;
	case 'w': return VDL_WARNING;
	case 'r': return VDL_REMARK;
	default: return VDL_NOTE;
	}
}

static char *dstr(pp *p, const char *s, size_t n)
{
	char *r = varena_alloc(&p->perm, n + 1);

	memcpy(r, s, n);
	r[n] = 0;
	return r;
}

vdiag *pp_diag_vbegin(pp *p, int level, uint32_t loc, const char *fmt, va_list ap)
{
	vdiag *d = varena_alloc(&p->perm, sizeof(*d));
	char msg[2048];
	int n = vsnprintf(msg, sizeof(msg), fmt, ap);

	memset(d, 0, sizeof(*d));
	d->level = (uint8_t)level;
	d->loc = loc;
	d->fmt = fmt;
	d->id = fmt_id(fmt);
	d->track = pp_diag_pc;
	pp_diag_pc = NULL;
	d->msg = dstr(p, msg, n < 0 ? 0 : (size_t)n < sizeof(msg) ? (size_t)n : sizeof(msg) - 1);
	return d;
}

vdiag *pp_diag_begin(pp *p, int level, uint32_t loc, const char *fmt, ...)
{
	pp_diag_pc = __builtin_return_address(0);
	va_list ap;
	vdiag *d;

	va_start(ap, fmt);
	d = pp_diag_vbegin(p, level, loc, fmt, ap);
	va_end(ap);
	return d;
}

void pp_diag_label(pp *p, vdiag *d, uint32_t b, uint32_t e, const char *label)
{
	if (d->nspan == d->cspan) {
		uint32_t c = d->cspan ? d->cspan * 2 : 4;
		vdspan *n = varena_alloc(&p->perm, c * sizeof(*n));

		if (d->nspan)
			memcpy(n, d->span, d->nspan * sizeof(*n));
		d->span = n, d->cspan = c;
	}
	d->span[d->nspan++] = (vdspan){ b, e, label ? dstr(p, label, strlen(label)) : NULL };
}

void pp_diag_help(pp *p, vdiag *d, const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n >= 0)
		d->help = dstr(p, buf, (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1);
}

void pp_diag_fix(pp *p, vdiag *d, uint32_t b, uint32_t e, const char *s, int app)
{
	if (d->nfix == d->cfix) {
		uint32_t c = d->cfix ? d->cfix * 2 : 4;
		vdfix *n = varena_alloc(&p->perm, c * sizeof(*n));

		if (d->nfix)
			memcpy(n, d->fix, d->nfix * sizeof(*n));
		d->fix = n, d->cfix = c;
	}
	d->fix[d->nfix++] = (vdfix){ b, e, dstr(p, s, strlen(s)), (uint8_t)app };
}

void pp_diag_flag(vdiag *d, const char *flag)
{
	d->flag = flag;
}

void pp_diag_macro(vdiag *d, const char *ms, uint32_t mlen)
{
	d->ms = ms, d->mlen = mlen;
}

static void kid_add(vdiag *d, vdiag *k)
{
	if (d->klast)
		d->klast->next = k;
	else
		d->kids = k;
	d->klast = k;
}

/* n among top's notes right after after (top itself: first) */
static void kid_after(vdiag *top, vdiag *after, vdiag *n)
{
	if (after == top) {
		n->next = top->kids, top->kids = n;
		if (!top->klast)
			top->klast = n;
		return;
	}
	n->next = after->next, after->next = n;
	if (top->klast == after)
		top->klast = n;
}

vdiag *pp_diag_note(pp *p, vdiag *d, uint32_t loc, const char *fmt, ...)
{
	pp_diag_pc = __builtin_return_address(0);
	va_list ap;
	vdiag *k;

	va_start(ap, fmt);
	k = pp_diag_vbegin(p, VDL_NOTE, loc, fmt, ap);
	va_end(ap);
	kid_add(d, k);
	return k;
}

const vdiag *pp_diag_records(const pp *p) { return p->dhead; }

/* clang: a token from a macro expansion has the caret at the invocation and
   a range over all of it, and a note at the macro's body */
static void macro_adjust(pp *p, vdiag *top, vdiag *d)
{
	const pmacro *xm;
	uint32_t b, ee;
	bool any = false;

	if (!d->ms || d->loc == UINT32_MAX || !(xm = expansion_at(p, d->loc, &b, &ee)))
		return;
	d->loc = b;
	if (d->nspan > 8)
		d->nspan = 8;
	for (uint32_t i = 0; i < d->nspan; i++) {
		if (d->span[i].b >= b && d->span[i].b <= ee)
			d->span[i].b = b, d->span[i].e = ee;
		any = true;
	}
	if (!any)
		pp_diag_label(p, d, b, ee, NULL);
	expansion_note(p, top, d, xm, b, d->ms, d->mlen);
}

/* " {vcc file.c:123 function}": where vcc reported it (VCC_TRACK_DIAG; symbolized by atos) */
static void track_str(vbuf *b, const void *pc)
{
	Dl_info di;
	char cmd[1200], out[512];
	FILE *f = NULL;
	const char *l, *r;

	vbuf_s(b, " {vcc ");
	if (!pc || !dladdr(pc, &di) || strlen(di.dli_fname) > 1024 || strchr(di.dli_fname, '\'')) {
		vbuf_s(b, "?}");
		return;
	}
	snprintf(cmd, sizeof(cmd), "atos -o '%s' -l %p %p 2>/dev/null", di.dli_fname, di.dli_fbase, (const char *)pc - 1);
	/* "function (in vpp) (expr.c:123)" with debug info, else "function (in vpp) + 6304" */
	if ((f = popen(cmd, "r")) && fgets(out, sizeof(out), f) && (l = strstr(out, " (in "))) {
		vbuf_put(b, out, (size_t)(l - out));
		if ((l = strrchr(out, '(')) && (r = strchr(l, ')')) && memchr(l, ':', (size_t)(r - l))) {
			vbuf_s(b, " ");
			vbuf_put(b, l + 1, (size_t)(r - l - 1));
		} else if ((l = strstr(out, ") + "))) {
			vbuf_s(b, "+");
			vbuf_put(b, l + 4, strcspn(l + 4, "\n"));
		}
	} else {
		const char *base = strrchr(di.dli_fname, '/');

		vbuf_f(b, "%s+%#lx", base ? base + 1 : di.dli_fname, (unsigned long)((const char *)pc - (const char *)di.dli_fbase));
	}
	if (f)
		pclose(f);
	vbuf_s(b, "}");
}

/* a record's first line (clang's, in every style); true and *ep when it has a source line */
VPRIV bool render_head(pp *p, vbuf *b, const vdiag *d, const fent **ep)
{
	const char *name, *what = lvname[d->level];
	uint32_t nl, line, col;
	const fent *e = NULL;
	bool has = d->loc != UINT32_MAX && loc_ok(p, d->loc, &e);	/* UINT32_MAX: no location */
	bool note = d->level == VDL_NOTE;

	if (has && !note && (uint32_t)(e - p->fe) + 1 != p->diag_fe)
		include_stack(p, b, e);
	if (has && !note)
		p->diag_fe = (uint32_t)(e - p->fe) + 1;
	if (p->color)
		vbuf_s(b, DC_BOLD);
	if (has && diag_where(p, d->loc, &name, &nl, &line, &col))
		vbuf_f(b, "%.*s:%u:%u: ", (int)nl, name, line, col);
	else if (d->loc == UINT32_MAX)
		;	/* "fatal error: too many errors emitted" has no location */
	else if (!has)
		vbuf_s(b, "vcc: ");
	if (p->color)
		vbuf_s(b, dcolor(what));
	vbuf_s(b, what);
	vbuf_s(b, ": ");
	if (p->color)
		vbuf_s(b, note ? DC_RESET : DC_RESET DC_BOLD);
	if (p->color && p->o->diag_style != PP_DSTYLE_CLANG)
		diag_typediff(b, d, note ? DC_RESET : DC_RESET DC_BOLD);
	else
		vbuf_s(b, d->msg);
	if (d->flag && p->color)	/* OSC 8: the flag links to clang's reference */
		vbuf_f(b, " [%s\033]8;;https://clang.llvm.org/docs/DiagnosticsReference.html#w%s\033\\-W%s\033]8;;\033\\]",
		       d->werror ? "-Werror," : "", d->flag, d->flag);
	else if (d->flag)
		vbuf_f(b, " [%s-W%s]", d->werror ? "-Werror," : "", d->flag);
	if (p->o->diag_show_id) {
		const char *docs = getenv("VCC_DIAG_DOCS");	/* where `vcc --explain` pages are (D6) */
		const char *id = d->id ? d->id : "vcc_unknown";

		if (p->color && docs && *docs)
			vbuf_f(b, " [\033]8;;%s/%s.md\033\\%s\033]8;;\033\\]", docs, id, id);
		else
			vbuf_f(b, " [%s]", id);
	}
	if (p->dtrack)
		track_str(b, d->track);
	if (p->color)
		vbuf_s(b, DC_RESET);
	vbuf_s(b, "\n");
	*ep = e;
	return has && !p->o->diag_nocaret && d->loc - e->base < e->f->size;
}

/* one record in clang's text format */
VPRIV void render_text(pp *p, vbuf *b, const vdiag *d)
{
	const fent *e;
	/* clang (DiagnosticRenderer): no snippet again for a record at the previous one's
	   location without ranges or fix-its, unless it follows a note and is no note */
	bool same = d->loc + 1 == p->dlast1 && !d->nspan && !d->nfix && !(p->dlastlv == VDL_NOTE && d->level != VDL_NOTE);

	p->dlast1 = d->loc + 1, p->dlastlv = d->level;
	if (render_head(p, b, d, &e) && !same)
		snippet(p, b, e, d);
}

static void render_clang(pp *p, vbuf *b, vdiag *d) { render_text(p, b, d); }

void pp_diag_emit(pp *p, vdiag *d)
{
	int fd = p->o->errfd ? p->o->errfd : 2;
	vdiag *top = d;
	vbuf b = { 0 };

	if (d->level == VDL_NOTE && p->dtail)
		kid_add(top = p->dtail, d);	/* a note reported after its diagnostic */
	else {
		if (p->dtail)
			p->dtail->next = d;
		else
			p->dhead = d;
		p->dtail = d;
	}
	macro_adjust(p, top, d);
	if (p->o->diag_format == PP_DIAG_JSON) {
		/* a record is written when it is complete: at the next one or at the end */
		if (d == top)
			diag_json_flush(p), p->dpend = d;
	} else if (p->o->diag_format == PP_DIAG_TEXT && p->o->diag_style == PP_DSTYLE_VCC) {
		/* rendered when the next record starts (or at the end): its notes come first */
		if (d == top) {
			diag_text_flush(p);
			p->tpend = d;
		}
	} else if (p->o->diag_format != PP_DIAG_SARIF) {
		void (*r)(pp *, vbuf *, vdiag *) = p->o->diag_style == PP_DSTYLE_NARRATED ? diag_narrate : render_clang;

		if (!top->emitted)
			r(p, &b, top), top->emitted = 1;
		for (vdiag *k = top->kids; k; k = k->next)
			if (!k->emitted)
				r(p, &b, k), k->emitted = 1;
		(void)!write(fd, b.p, b.n);
		free(b.p);
	}
	if (d->level >= VDL_ERROR)
		p->errors++;
	else if (d->level == VDL_WARNING)
		p->warns++;
	if (d->level == VDL_FATAL && !p->o->fatal_continue) {
		pp_diag_finish(p);
		if (p->fatal_jb)
			longjmp(*p->fatal_jb, 1);
		exit(1);
	}
}

/* ---------------- the pp_dinfo wrappers ---------------- */

void pp_vdiagx(pp *p, const pp_dinfo *di, const char *fmt, va_list ap)
{
	vdiag *d = pp_diag_vbegin(p, level_of(di->what), di->loc, fmt, ap);

	for (int i = 0; i < di->nr; i++)
		pp_diag_label(p, d, di->r[i].b, di->r[i].e, NULL);
	for (int i = 0; i < di->nfx; i++)
		pp_diag_fix(p, d, di->fx[i].b, di->fx[i].e, di->fx[i].s, VDA_UNSPEC);
	pp_diag_macro(d, di->ms, di->mlen);
	pp_diag_emit(p, d);
}

void pp_vdiag(pp *p, uint32_t loc, const char *what, const char *fmt, va_list ap)
{
	pp_diag_emit(p, pp_diag_vbegin(p, level_of(what), loc, fmt, ap));
}

int pp_warnings(const pp *p) { return p->warns; }

void pp_diag_summary(pp *p)
{
	int fd = p->o->errfd ? p->o->errfd : 2;
	char b[128];
	int n = 0;

	pp_diag_finish(p);
	if (p->o->diag_format == PP_DIAG_JSON || p->o->diag_format == PP_DIAG_SARIF)
		return;	/* the records say it */
	if (p->warns && p->errors)
		n = snprintf(b, sizeof(b), "%d warning%s and %d error%s generated.\n", p->warns, p->warns > 1 ? "s" : "",
			     p->errors, p->errors > 1 ? "s" : "");
	else if (p->errors)
		n = snprintf(b, sizeof(b), "%d error%s generated.\n", p->errors, p->errors > 1 ? "s" : "");
	else if (p->warns)
		n = snprintf(b, sizeof(b), "%d warning%s generated.\n", p->warns, p->warns > 1 ? "s" : "");
	if (n)
		(void)!write(fd, b, (size_t)n);
}
