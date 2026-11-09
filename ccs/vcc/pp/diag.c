/* vcc preprocessor: diagnostics in clang's text format */
#include "ppimpl.h"

/* ---------------- diagnostics ---------------- */

static const fent *loc_fent(const pp *p, uint32_t loc)
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
	va_list ap;

	va_start(ap, fmt);
	pp_vdiag(p, loc, what, fmt, ap);
	va_end(ap);
}

void pp_diagx(pp *p, const pp_dinfo *d, const char *fmt, ...)
{
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

typedef struct dbuf {
	char *p;
	size_t n, cap;
} dbuf;

static void db_put(dbuf *b, const char *s, size_t n)
{
	if (b->n + n > b->cap) {
		size_t c = b->cap ? b->cap * 2 : 4096;

		while (c < b->n + n)
			c *= 2;
		b->p = realloc(b->p, c);
		b->cap = c;
	}
	memcpy(b->p + b->n, s, n);
	b->n += n;
}

static void db_s(dbuf *b, const char *s) { db_put(b, s, strlen(s)); }

__attribute__((format(printf, 2, 3)))
static void db_f(dbuf *b, const char *fmt, ...)
{
	char t[1024];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(t, sizeof(t), fmt, ap);
	va_end(ap);
	db_put(b, t, n < (int)sizeof(t) ? (size_t)n : sizeof(t) - 1);
}

static void db_c(dbuf *b, char c, int n)
{
	while (n-- > 0)
		db_put(b, &c, 1);
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

static bool loc_ok(const pp *p, uint32_t loc, const fent **e)
{
	*e = loc_fent(p, loc);
	return *e && loc >= (*e)->base && loc - (*e)->base <= (*e)->f->size;
}

int pp_error_limit(const pp *p) { return p->o->error_limit; }

int pp_wstate(const pp *p, const char *flag, bool on, const char *const *groups)
{
	bool err = false, allerr = false, noerr = false;

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
		if (!strcmp(p->o->wargs[i], "-w"))
			return 0;
	if (!on)
		return 0;
	return err || (allerr && !noerr) ? 2 : 1;
}

bool pp_warnx(pp *p, const char *flag, bool on, const char *const *groups, pp_dinfo *d, const char *fmt, ...)
{
	int st = pp_wstate(p, flag, on, groups);
	char msg[1024];
	va_list ap;

	if (!st || (d->loc != UINT32_MAX && pp_in_system_header(p, d->loc)))
		return false;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	d->what = st == 2 ? "error" : "warning";
	pp_diagx(p, d, "%s [%s-W%s]", msg, st == 2 ? "-Werror," : "", flag);
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

/* display columns of src[b, e): tabs to the next multiple of 8, UTF-8 continuation bytes take none */
static uint32_t dcols(const char *s, uint32_t b, uint32_t e, uint32_t col)
{
	for (uint32_t i = b; i < e; i++) {
		unsigned char ch = (unsigned char)s[i];

		if (ch == '\t')
			col = (col / 8 + 1) * 8;
		else if ((ch & 0xc0) != 0x80)
			col++;
	}
	return col;
}

/* "In file included from" for each level of loc's include stack, outermost first */
/* pp_where with the file's path as spelled (clang's diagnostics ignore -fmacro-prefix-map) */
static int diag_where(const pp *p, uint32_t loc, const char **name, uint32_t *nl, uint32_t *line, uint32_t *col)
{
	int r = pp_where(p, loc, name, nl, line, col);
	const fent *e = loc_fent(p, loc);

	if (r && e && e->spath && loc - e->base <= e->f->size)
		*name = e->spath, *nl = (uint32_t)strlen(e->spath);
	return r;
}

static void include_stack(pp *p, dbuf *b, const fent *e)
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
			db_f(b, "In file included from %.*s:%u:\n", (int)nl, name, line);
	}
}

static void snippet(pp *p, dbuf *b, const fent *e, const pp_dinfo *d)
{
	const char *s = e->f->src;
	uint32_t off = d->loc - e->base, size = e->f->size, ls, le, line, width, nd;
	char num[16];
	dbuf cl = { 0 }, fl = { 0 };
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
	db_s(b, num);
	db_s(b, " | ");
	for (uint32_t i = ls, col = 0; i < le; i++) {
		if (s[i] == '\t') {
			uint32_t to = (col / 8 + 1) * 8;

			db_c(b, ' ', (int)(to - col));
			col = to;
		} else {
			db_put(b, s + i, 1);
			if (((unsigned char)s[i] & 0xc0) != 0x80)
				col++;
		}
	}
	db_s(b, "\n");
	/* the caret line: ~ under the ranges, ^ at loc */
	ccol = dcols(s, ls, off, 0);
	{
		uint32_t lw = dcols(s, ls, le, 0) + 1;
		char *m = calloc(lw + 1, 1);

		memset(m, ' ', lw);
		for (int i = 0; i < d->nr; i++) {
			uint32_t rb = d->r[i].b, re = d->r[i].e;

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
		if (ccol < lw)
			m[ccol] = '^';
		{
			uint32_t n = lw;

			while (n > 0 && m[n - 1] == ' ')
				n--;
			db_put(&cl, m, n);
			/* clang pads the caret line out to the last fix-it's column */
			for (int i = 0; i < d->nfx; i++) {
				uint32_t fb = d->fx[i].b, c0;

				if (fb < e->base || fb - e->base < ls || fb - e->base > le)
					continue;
				c0 = dcols(s, ls, fb - e->base, 0);
				if (c0 > cl.n)
					db_c(&cl, ' ', (int)(c0 - cl.n));
			}
		}
		free(m);
	}
	db_c(b, ' ', (int)width + 2);
	db_s(b, "| ");
	if (p->color)
		db_s(b, DC_CARET);
	db_put(b, cl.p, cl.n);
	if (p->color)
		db_s(b, DC_RESET);
	db_s(b, "\n");
	/* fix-it hints on this line */
	for (int i = 0; i < d->nfx; i++) {
		uint32_t fb = d->fx[i].b;
		uint32_t c0, have;

		if (fb < e->base || fb - e->base < ls || fb - e->base > le)
			continue;
		c0 = dcols(s, ls, fb - e->base, 0);
		have = (uint32_t)fl.n;
		if (c0 < have)
			c0 = have;
		db_c(&fl, ' ', (int)(c0 - have));
		db_s(&fl, d->fx[i].s);
		anyfix = true;
	}
	if (anyfix) {
		db_c(b, ' ', (int)width + 2);
		db_s(b, "| ");
		if (p->color)
			db_s(b, DC_FIXIT);
		db_put(b, fl.p, fl.n);
		if (p->color)
			db_s(b, DC_RESET);
		db_s(b, "\n");
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

static void vdiagx_(pp *p, const pp_dinfo *d, const char *fmt, va_list ap);

/* "expanded from macro 'M'" at the body token spelled like the diagnosed one (the last #define before) */
static void expansion_note(pp *p, const pmacro *m, uint32_t before, const char *ms, uint32_t mlen)
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
		pp_span r = { loc, loc + len };
		pp_dinfo d = { "note", loc, &r, len > 1, NULL, 0, NULL, 0 };

		pp_diagx(p, &d, "expanded from macro '%.*s'", (int)vintern_ents[m->name].len, vintern_ents[m->name].s);
	}
}

void pp_vdiagx(pp *p, const pp_dinfo *d, const char *fmt, va_list ap)
{
	const pmacro *xm;
	uint32_t b, ee;

	if (d->ms && d->loc != UINT32_MAX && (xm = expansion_at(p, d->loc, &b, &ee))) {
		/* clang: the caret at the invocation, a range at the expansion over all of it */
		pp_dinfo md = *d;
		pp_span mr[8];

		md.loc = b;
		md.nr = d->nr > 8 ? 8 : d->nr;
		for (int i = 0; i < md.nr; i++) {
			mr[i] = d->r[i];
			if (mr[i].b >= b && mr[i].b <= ee)
				mr[i].b = b, mr[i].e = ee;
		}
		if (!md.nr)
			mr[0].b = b, mr[0].e = ee, md.nr = 1;
		md.r = mr;
		vdiagx_(p, &md, fmt, ap);
		expansion_note(p, xm, b, d->ms, d->mlen);
		return;
	}
	vdiagx_(p, d, fmt, ap);
}

static void vdiagx_(pp *p, const pp_dinfo *d, const char *fmt, va_list ap)
{
	int fd = p->o->errfd ? p->o->errfd : 2;
	const char *name, *what = d->what;
	uint32_t nl, line, col;
	const fent *e;
	bool has = d->loc != UINT32_MAX && loc_ok(p, d->loc, &e);	/* UINT32_MAX: no location */
	bool note = what[0] == 'n';
	char msg[2048];
	dbuf b = { 0 };

	vsnprintf(msg, sizeof(msg), fmt, ap);
	if (has && what[0] != 'n' && (uint32_t)(e - p->fe) + 1 != p->diag_fe)
		include_stack(p, &b, e);
	if (has && what[0] != 'n')
		p->diag_fe = (uint32_t)(e - p->fe) + 1;
	if (p->color)
		db_s(&b, DC_BOLD);
	if (has && diag_where(p, d->loc, &name, &nl, &line, &col))
		db_f(&b, "%.*s:%u:%u: ", (int)nl, name, line, col);
	else if (d->loc == UINT32_MAX)
		;	/* "fatal error: too many errors emitted" has no location */
	else if (!has)
		db_s(&b, "vcc: ");
	if (p->color)
		db_s(&b, dcolor(what));
	db_s(&b, what);
	db_s(&b, ": ");
	if (p->color)
		db_s(&b, note ? DC_RESET : DC_RESET DC_BOLD);
	db_s(&b, msg);
	if (p->color)
		db_s(&b, DC_RESET);
	db_s(&b, "\n");
	if (has && !p->o->diag_nocaret && d->loc - e->base < e->f->size)
		snippet(p, &b, e, d);
	(void)!write(fd, b.p, b.n);
	free(b.p);
	if (what[0] == 'e' || what[0] == 'f')
		p->errors++;
	else if (what[0] == 'w')
		p->warns++;
	if (what[0] == 'f') {
		if (p->fatal_jb)
			longjmp(*p->fatal_jb, 1);
		exit(1);
	}
}

void pp_vdiag(pp *p, uint32_t loc, const char *what, const char *fmt, va_list ap)
{
	pp_span r;
	pp_dinfo d = { what, loc, &r, 0, NULL, 0, NULL, 0 };

	pp_vdiagx(p, &d, fmt, ap);
}

int pp_warnings(const pp *p) { return p->warns; }

void pp_diag_summary(pp *p)
{
	int fd = p->o->errfd ? p->o->errfd : 2;
	char b[128];
	int n = 0;

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
