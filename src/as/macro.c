/* macro.c: .macro, its arguments and templates, .rept, .irp and .exitm: expansion as text, as MC does. */
#include "asimpl.h"

/* ---------------------------------------------------------------- macros */

VPRIV Macro *find_macro(As *a, const char *s, size_t n)
{
	uint32_t h = hsv(s, n) & a->mmask;
	for (Macro *m = a->mtab[h]; m; m = m->next)
		if (m->name.n == n && !memcmp(m->name.s, s, n)) return m;
	return 0;
}

/* first word of a statement */
VPRIV Sv first_word(const char *p, const char *e)
{
	p = ws(p, e);
	const char *s = p;
	while (p < e && isidc((unsigned char)*p)) p++;
	return (Sv){ s, (uint32_t)(p - s) };
}

static int is_loop_dir(Sv w)
{
	return sveq(w, ".rept") || sveq(w, ".irp") || sveq(w, ".irpc") || sveq(w, ".rep");
}

/* Collect a macro-like body from the current frame up to the matching
 * .endm (macro = 1) or .endr.  Returns the raw text. */
static int collect_body(As *a, int macro, Sv *body)
{
	Frame *f = &a->fr[a->nfr - 1];
	const char *start = f->p;
	int depth = 0;
	while (f->p < f->e) {
		const char *raw, *s;
		size_t n = next_stmt(a, f, &raw, &s);
		const char *t = ws(s, s + n);
		if (t == s + n || *t != '.') continue;
		Sv w = first_word(t, s + n);
		if (macro) {
			if (sveq(w, ".endm") || sveq(w, ".endmacro")) {
				if (!depth) { *body = (Sv){ start, (uint32_t)(raw - start) }; return 0; }
				depth--;
			} else if (sveq(w, ".macro")) depth++;
		} else {
			if (sveq(w, ".endr")) {
				if (!depth) { *body = (Sv){ start, (uint32_t)(raw - start) }; return 0; }
				depth--;
			} else if (is_loop_dir(w)) depth++;
		}
	}
	return err(a, macro ? "no matching '.endmacro' in definition" : "no matching '.endr' in definition");
}

/* parseMacroArgument: one argument as MC tokenises it, written as the text
 * the expansion will insert (strings unquoted unless vararg). */
static int is_op_start(const char *p, const char *e)
{
	char c = *p;
	switch (c) {
	case '+': case '-': case '~': case '/': case '*': case '=': case '|': case '^':
	case '&': case '!': case '<': case '>':
		return 1;
	case '.':
		return !(p + 1 < e && isidc((unsigned char)p[1]));
	}
	return 0;
}
static int op_len(const char *p, const char *e)
{
	char c = p[0], d = p + 1 < e ? p[1] : 0;
	if ((c == '=' && d == '=') || (c == '|' && d == '|') || (c == '&' && d == '&') || (c == '!' && d == '=') ||
	    (c == '<' && (d == '=' || d == '<' || d == '>')) || (c == '>' && (d == '=' || d == '>')))
		return 2;
	return 1;
}

/* macro_arg when the argument is a contiguous piece of the text (no
 * string, no blanks around an operator, no error): *out points into the
 * text.  Returns 0 if macro_arg must do it. */
static int macro_arg_plain(const char **pp, const char *e, int vararg, Sv *out)
{
	const char *p = *pp, *end;
	if (vararg) {
		p = ws(p, e);
		const char *q = e;
		while (q > p && (q[-1] == ' ' || q[-1] == '\t')) q--;
		*pp = e;
		*out = (Sv){ p, (uint32_t)(q - p) };
		return 1;
	}
	const char *s = p;
	int depth = 0;
	for (;;) {
		if (depth == 0) {
			if (p < e && *p == ',') { end = p; break; }
			if (p < e && (*p == ' ' || *p == '\t')) {
				end = p;
				p = ws(p, e);
				if (p < e && is_op_start(p, e)) return 0;
				break;
			}
			if (p < e && is_op_start(p, e)) {
				if (*p == '=' && p + 1 < e && p[1] != '=') return 0;
				p += op_len(p, e);
				if (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\f' || *p == '\v')) return 0;
				continue;
			}
		}
		if (p >= e) { end = p; break; }
		char c = *p;
		if (c == '(') depth++;
		else if (c == ')' && depth) depth--;
		if (c == '"') return 0;
		if (c == ' ' || c == '\t') { p = ws(p, e); continue; }
		if (isidc((unsigned char)c)) { while (p < e && isidc((unsigned char)*p)) p++; continue; }
		if (c == '=' && depth == 0) return 0;
		p++;
	}
	if (depth) return 0;
	*pp = p;
	*out = (Sv){ s, (uint32_t)(end - s) };
	return 1;
}

static int macro_arg(As *a, const char **pp, const char *e, int vararg, Sv *out)
{
	const char *p = *pp;
	char *buf = (char *)rget(&a->xs, (size_t)(e - p) + 1);
	size_t n = 0;
	if (vararg) {
		p = ws(p, e);
		const char *q = e;
		while (q > p && (q[-1] == ' ' || q[-1] == '\t')) q--;
		memcpy(buf, p, (size_t)(q - p)); n = (size_t)(q - p);
		*pp = e;
		*out = (Sv){ buf, (uint32_t)n };
		return 0;
	}
	int depth = 0;
	for (;;) {
		int space = 0;
		if (depth == 0) {
			if (p < e && *p == ',') break;
			if (p < e && (*p == ' ' || *p == '\t')) { p = ws(p, e); space = 1; }
			if (p < e && is_op_start(p, e)) {
				if (*p == '=' && p + 1 < e && p[1] != '=') return err(a, "unexpected token in macro instantiation");
				int k = op_len(p, e);
				memcpy(buf + n, p, (size_t)k); n += (size_t)k; p += k;
				p = ws(p, e);
				continue;
			}
			if (space) break;
		}
		if (p >= e) break;
		char c = *p;
		if (c == '(') depth++;
		else if (c == ')' && depth) depth--;
		if (c == '"') {
			const char *s = ++p;
			while (p < e && *p != '"') { if (*p == '\\' && p + 1 < e) p++; p++; }
			memcpy(buf + n, s, (size_t)(p - s)); n += (size_t)(p - s);
			if (p < e) p++;
			continue;
		}
		if (c == ' ' || c == '\t') {
			const char *s = p; p = ws(p, e);
			memcpy(buf + n, s, (size_t)(p - s)); n += (size_t)(p - s);
			continue;
		}
		if (isidc((unsigned char)c)) {
			const char *s = p;
			while (p < e && isidc((unsigned char)*p)) p++;
			memcpy(buf + n, s, (size_t)(p - s)); n += (size_t)(p - s);
			continue;
		}
		if (c == '=' && depth == 0) return err(a, "unexpected token in macro instantiation");
		buf[n++] = c; p++;
	}
	if (depth) return err(a, "unbalanced parentheses in argument");
	a->xs.used = (size_t)((uint8_t *)buf - a->xs.base) + n;
	*pp = p;
	*out = (Sv){ buf, (uint32_t)n };
	return 0;
}

/* expandMacro: body with \param, \@, \+, \() substitutions.  The body is
 * compiled once into pieces (r is the region for them), then each
 * expansion is copies. */
static Tmpl tmpl_compile(As *a, vregion *r, Sv body, const Param *ps, uint32_t np, int at)
{
	const char *b = body.s;
	size_t n = body.n, nbs = 0;
	for (const char *q = b; (q = memchr(q, '\\', (size_t)(b + n - q))); q++) nbs++;
	Tmpl t = { rget(r, sizeof(Piece) * (nbs + 1)), 0 };
	size_t lit = 0, i = 0;
	while (i < n) {
		const char *q = memchr(b + i, '\\', n - i);
		if (!q) break;
		i = (size_t)(q - b);
		if (i + 1 == n) break;
		int32_t k;
		size_t skip;
		if (at && b[i + 1] == '@') k = -2, skip = 2;
		else if (b[i + 1] == '+') k = -3, skip = 2;
		else if (b[i + 1] == '(' && i + 2 < n && b[i + 2] == ')') k = -1, skip = 3;
		else {
			size_t pos = i + 1, j = pos;
			while (j < n && isidc((unsigned char)b[j])) j++;
			uint32_t m;
			for (m = 0; m < np; m++) if (ps[m].name.n == j - pos && !memcmp(ps[m].name.s, b + pos, j - pos)) break;
			if (m == np) { i = pos; continue; }	/* '\' and the name stay as they are */
			k = (int32_t)m, skip = j - i;
		}
		t.p[t.n++] = (Piece){ (uint32_t)lit, (uint32_t)(i - lit), k };
		i += skip;
		lit = i;
	}
	if (lit < n) t.p[t.n++] = (Piece){ (uint32_t)lit, (uint32_t)(n - lit), -1 };
	(void)a;
	return t;
}

/* .altmacro: a parameter name is replaced wherever it stands as a whole
 * identifier, with or without the backslash */
static Tmpl tmpl_compile_alt(vregion *r, Sv body, const Param *ps, uint32_t np)
{
	const char *b = body.s;
	size_t n = body.n;
	Tmpl t = { rget(r, sizeof(Piece) * (n / 2 + 2)), 0 };
	size_t lit = 0, i = 0;
	while (i < n) {
		size_t st = i;
		int bs = b[i] == '\\';
		if (bs) i++;
		if (i < n && isidc((unsigned char)b[i]) && (bs || st == 0 || !isidc((unsigned char)b[st - 1]))) {
			size_t j = i;
			while (j < n && isidc((unsigned char)b[j])) j++;
			uint32_t m;
			for (m = 0; m < np; m++) if (ps[m].name.n == j - i && !memcmp(ps[m].name.s, b + i, j - i)) break;
			if (m < np) {
				t.p[t.n++] = (Piece){ (uint32_t)lit, (uint32_t)(st - lit), (int32_t)m };
				lit = j;
			}
			i = j;
			continue;
		}
		if (!bs) i++;
	}
	if (lit < n) t.p[t.n++] = (Piece){ (uint32_t)lit, (uint32_t)(n - lit), -1 };
	return t;
}

/* an .altmacro argument: %expr is its value in decimal, <text> is text with
 * !c standing for c */
static int alt_arg(As *a, const char **pp, const char *e, Sv *out)
{
	const char *p = *pp;
	if (*p == '%') {
		p++;
		int64_t v;
		if (abs_expr(a, &p, e, &v)) return -1;
		char buf[24];
		int k = snprintf(buf, sizeof buf, "%lld", (long long)v);
		*out = svdup(a, buf, (size_t)k);
	} else {
		char *o = rget(&a->defs, (size_t)(e - p) + 1);
		size_t n = 0;
		for (p++; p < e && *p != '>'; p++) {
			if (*p == '!' && p + 1 < e) p++;
			o[n++] = *p;
		}
		if (p >= e) return err(a, "expected '>'");
		p++;
		*out = (Sv){ o, (uint32_t)n };
	}
	*pp = p;
	return 0;
}

static char *utoa(char *o, uint32_t v)
{
	char tmp[10]; int k = 0;
	do tmp[k++] = (char)('0' + v % 10), v /= 10; while (v);
	while (k) *o++ = tmp[--k];
	return o;
}

/* copy n bytes to o, which may be overwritten up to 16 bytes past n (the
 * expansion stack has room); short copies are one 16-byte load when that
 * stays in the source's page */
static inline void xcopy(char *o, const char *s, size_t n)
{
#ifdef __aarch64__
	if (n <= 16 && ((uintptr_t)s & 4095) <= 4096 - 16) { vst1q_u8((uint8_t *)o, vld1q_u8((const uint8_t *)s)); return; }
#endif
	memcpy(o, s, n);
}

static size_t expand(As *a, char *o, const char *b, Tmpl t, const Sv *args, uint32_t count)
{
	char *o0 = o;
	for (uint32_t i = 0; i < t.n; i++) {
		const Piece *p = &t.p[i];
		xcopy(o, b + p->off, p->len); o += p->len;
		if (p->k >= 0) { xcopy(o, args[p->k].s, args[p->k].n); o += args[p->k].n; }
		else if (p->k == -2) o = utoa(o, a->ninst);
		else if (p->k == -3) o = utoa(o, count);
	}
	return (size_t)(o - o0);
}

static size_t expand_bound(Sv body, const Sv *args, uint32_t np)
{
	size_t m = 0;
	for (uint32_t k = 0; k < np; k++) if (args[k].n > m) m = args[k].n;
	return body.n * (m + 12) + 16;
}

VPRIV int d_macro(As *a, const char *p, const char *e)
{
	Sv name;
	/* names from file text (not a->sb) stay valid; others are copied */
	int keep = a->fr[a->nfr - 1].kind == FR_FILE && !((const uint8_t *)p >= a->sb.base && (const uint8_t *)p < a->sb.base + a->sb.cap);
	p = ws(p, e);
	const char *s = p;
	while (p < e && isidc((unsigned char)*p)) p++;
	if (p == s) return err(a, "expected identifier in '.macro' directive");
	name = keep ? (Sv){ s, (uint32_t)(p - s) } : svdup(a, s, (size_t)(p - s));
	Param ps[64]; uint32_t np = 0;
	p = ws(p, e);
	if (p < e && *p == ',') p = ws(p + 1, e);
	while (p < e) {
		if (np >= 64) return err(a, "too many macro parameters");
		Param *q = &ps[np++];
		memset(q, 0, sizeof *q);
		s = p;
		while (p < e && isidc((unsigned char)*p)) p++;
		if (p == s) return err(a, "expected identifier in '.macro' directive");
		q->name = keep ? (Sv){ s, (uint32_t)(p - s) } : svdup(a, s, (size_t)(p - s));
		p = ws(p, e);
		if (p < e && *p == ':') {
			p = ws(p + 1, e);
			s = p;
			while (p < e && isidc((unsigned char)*p)) p++;
			Sv qq = { s, (uint32_t)(p - s) };
			if (sveq(qq, "req")) q->req = 1;
			else if (sveq(qq, "vararg")) q->vararg = 1;
			else return err(a, "invalid macro parameter qualifier");
			p = ws(p, e);
		}
		if (p < e && *p == '=') {
			p = ws(p + 1, e);
			Sv d;
			size_t mark = a->xs.used;
			if (macro_arg(a, &p, e, 0, &d)) return -1;
			q->def = svdup(a, d.s, d.n);
			a->xs.used = mark;
			p = ws(p, e);
		}
		if (p < e && *p == ',') p = ws(p + 1, e);
	}
	Sv body;
	if (collect_body(a, 1, &body)) return -1;
	Macro *m = rget(&a->defs, sizeof *m);
	m->name = name;
	m->params = rget(&a->defs, sizeof(Param) * (np ? np : 1));
	memcpy(m->params, ps, sizeof(Param) * np);
	m->np = np;
	/* file text outlives the macro (as_snippet's contract); expansions do not */
	m->body = a->fr[a->nfr - 1].kind == FR_FILE ? body : svdup(a, body.s, body.n);
	m->count = 0;
	m->t = (Tmpl){ 0, 0 };
	if (a->mcount * 2 >= a->mmask) {
		uint32_t nm = a->mmask * 2 + 1;
		Macro **t = rget(&a->defs, sizeof *t * (nm + 1));
		memset(t, 0, sizeof *t * (nm + 1));
		for (uint32_t i = 0; i <= a->mmask; i++)
			for (Macro *x = a->mtab[i], *nx; x; x = nx) { nx = x->next; uint32_t h = hsv(x->name.s, x->name.n) & nm; x->next = t[h]; t[h] = x; }
		a->mtab = t; a->mmask = nm;
	}
	uint32_t h = hsv(name.s, name.n) & a->mmask;
	for (Macro *x = a->mtab[h]; x; x = x->next)
		if (x->name.n == name.n && !memcmp(x->name.s, name.s, name.n)) return err(a, "macro '%.*s' is already defined", (int)name.n, name.s);
	m->next = a->mtab[h]; a->mtab[h] = m; a->mcount++;
	return 0;
}

VPRIV int d_purgem(As *a, const char *p, const char *e)
{
	Sv n = first_word(p, e);
	uint32_t h = hsv(n.s, n.n) & a->mmask;
	for (Macro **pm = &a->mtab[h]; *pm; pm = &(*pm)->next)
		if ((*pm)->name.n == n.n && !memcmp((*pm)->name.s, n.s, n.n)) { *pm = (*pm)->next; a->mcount--; return 0; }
	return err(a, "macro '%.*s' is not defined", (int)n.n, n.s);
}

VPRIV int invoke(As *a, Macro *m, const char *p, const char *e)
{
	size_t mark = a->xs.used;
	Sv args[64];
	uint8_t set[64] = { 0 };
	uint32_t np = m->np, pi = 0;
	p = ws(p, e);
	int hasvararg = np && m->params[np - 1].vararg;
	for (uint32_t k = 0; k < np; k++) args[k] = (Sv){ "", 0 };
	if (p < e) {
		for (;;) {
			/* keyword argument? */
			const char *q = p;
			while (q < e && isidc((unsigned char)*q)) q++;
			const char *r = ws(q, e);
			int kw = q > p && r < e && *r == '=' && !(r + 1 < e && r[1] == '=');
			uint32_t k;
			if (kw) {
				for (k = 0; k < np; k++) if (m->params[k].name.n == (uint32_t)(q - p) && !memcmp(m->params[k].name.s, p, (size_t)(q - p))) break;
				if (k == np) return err(a, "parameter named '%.*s' does not exist for macro '%.*s'", (int)(q - p), p, (int)m->name.n, m->name.s);
				p = ws(r + 1, e);
			} else {
				if (pi >= np) {
					if (!np) return err(a, "too many positional arguments");
					return err(a, "too many positional arguments");
				}
				k = pi++;
			}
			/* the arguments are used before the statement text changes */
			int va = hasvararg && k == np - 1;
			if (a->altmacro && p < e && (*p == '%' || *p == '<')) { if (alt_arg(a, &p, e, &args[k])) return -1; }
			else if (!macro_arg_plain(&p, e, va, &args[k]) && macro_arg(a, &p, e, va, &args[k])) return -1;
			set[k] = 1;
			p = ws(p, e);
			if (p >= e) break;
			if (*p == ',') { p = ws(p + 1, e); if (p >= e) { if (pi < np) set[pi] = 1, args[pi] = (Sv){ "", 0 }; break; } }
		}
	}
	for (uint32_t k = 0; k < np; k++) {
		if (set[k] && args[k].n) continue;
		if (!set[k] || !args[k].n) {
			if (m->params[k].def.n && !(set[k] && args[k].n)) args[k] = m->params[k].def;
			else if (m->params[k].req && !set[k]) return err(a, "missing value for required parameter '%.*s' in macro '%.*s'", (int)m->params[k].name.n, m->params[k].name.s, (int)m->name.n, m->name.s);
			else if (m->params[k].req && !args[k].n) return err(a, "missing value for required parameter '%.*s' in macro '%.*s'", (int)m->params[k].name.n, m->params[k].name.s, (int)m->name.n, m->name.s);
		}
	}
	Tmpl tt;
	if (a->altmacro) tt = tmpl_compile_alt(&a->defs, m->body, m->params, np);
	else {
		if (!m->t.p) m->t = tmpl_compile(a, &a->defs, m->body, m->params, np, 1);
		tt = m->t;
	}
	char *o = (char *)rget(&a->xs, expand_bound(m->body, args, np));
	size_t n = expand(a, o, m->body.s, tt, args, m->count);
	a->xs.used = (size_t)((uint8_t *)o - a->xs.base) + n;
	a->ninst++;
	m->count++;
	return push_frame(a, o, n, FR_MACRO, mark);
}

VPRIV int d_rept(As *a, const char *p, const char *e)
{
	int64_t cnt;
	if (abs_expr(a, &p, e, &cnt)) return -1;
	if (cnt < 0) return err(a, "Count is negative");
	uint32_t at = a->nfr ? a->fr[a->nfr - 1].sline : 0;	/* (the .rept line, not .endr's) */
	Sv body;
	if (collect_body(a, 0, &body)) return -1;
	size_t mark = a->xs.used;
	Tmpl t = tmpl_compile(a, &a->xs, body, 0, 0, 0);
	char *o = (char *)rget(&a->xs, expand_bound(body, 0, 0) * (size_t)(cnt ? cnt : 1));
	size_t n = 0;
	for (int64_t i = 0; i < cnt; i++) n += expand(a, o + n, body.s, t, 0, 0);
	a->xs.used = (size_t)((uint8_t *)o - a->xs.base) + n;
	if (push_frame(a, o, n, FR_LOOP, mark)) return -1;
	a->fr[a->nfr - 1].pline = at;
	return 0;
}

VPRIV int d_irp(As *a, const char *p, const char *e, int chars)
{
	p = ws(p, e);
	const char *s = p;
	while (p < e && isidc((unsigned char)*p)) p++;
	if (p == s) return err(a, "expected identifier in '.irp' directive");
	Param par; memset(&par, 0, sizeof par);
	par.name = (Sv){ s, (uint32_t)(p - s) };
	p = ws(p, e);
	if (p >= e || *p != ',') return err(a, "expected comma");
	p = ws(p + 1, e);
	size_t mark = a->xs.used;
	Sv vals[1024]; uint32_t nv = 0;
	for (;;) {
		if (nv >= 1024) return err(a, "too many .irp values");
		if (macro_arg(a, &p, e, 0, &vals[nv++])) return -1;
		p = ws(p, e);
		if (p >= e) break;
		if (*p == ',') p = ws(p + 1, e);
	}
	Sv body;
	/* the param name lives in a->sb, which collect_body overwrites */
	par.name = svdup(a, par.name.s, par.name.n);
	uint32_t at = a->nfr ? a->fr[a->nfr - 1].sline : 0;
	if (collect_body(a, 0, &body)) return -1;
	if (chars) {
		Sv v = vals[0];
		nv = 0;
		Sv cv[1024];
		for (uint32_t i = 0; i < v.n && i < 1024; i++) cv[nv++] = (Sv){ v.s + i, 1 };
		memcpy(vals, cv, sizeof(Sv) * nv);
	}
	size_t tot = 0;
	for (uint32_t i = 0; i < nv; i++) tot += expand_bound(body, &vals[i], 1);
	Tmpl t = tmpl_compile(a, &a->xs, body, &par, 1, 1);
	char *o = (char *)rget(&a->xs, tot + 1);
	size_t n = 0;
	for (uint32_t i = 0; i < nv; i++) n += expand(a, o + n, body.s, t, &vals[i], 0);
	a->xs.used = (size_t)((uint8_t *)o - a->xs.base) + n;
	if (push_frame(a, o, n, FR_LOOP, mark)) return -1;
	a->fr[a->nfr - 1].pline = at;
	return 0;
}

VPRIV int d_exitm(As *a)
{
	for (int i = a->nfr - 1; i >= 0; i--) {
		if (a->fr[i].kind == FR_MACRO) {
			a->ncs = (int)a->fr[i].ncond;
			while (a->nfr > i) pop_frame(a);
			return 0;
		}
	}
	return err(a, "unexpected '.exitm' in file, no current macro definition");
}
