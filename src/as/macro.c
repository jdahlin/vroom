/* macro.c: .macro, its arguments and templates, .rept, .irp and .exitm: expansion as text, as MC does. */
#include "asimpl.h"

/* ---------------------------------------------------------------- macros */

VPRIV as_macro *as_macro_find(as_context *a, const char *s, size_t n)
{
	uint32_t h = as_text_hash(s, n) & a->macro_mask;
	for (as_macro *m = a->macro_table[h]; m; m = m->next)
		if (m->name.n == n && !memcmp(m->name.s, s, n)) return m;
	return 0;
}

/* first word of a statement */
VPRIV as_text as_text_first_word(const char *p, const char *e)
{
	p = as_whitespace_skip(p, e);
	const char *s = p;
	while (p < e && as_char_is_identifier((unsigned char)*p)) p++;
	return (as_text){ s, (uint32_t)(p - s) };
}

static int as_directive_is_loop(as_text w)
{
	return as_text_equals(w, ".rept") || as_text_equals(w, ".irp") || as_text_equals(w, ".irpc") || as_text_equals(w, ".rep");
}

/* Collect a macro-like body from the current frame up to the matching
 * .endm (macro = 1) or .endr.  Returns the raw text. */
static int as_macro_body_collect(as_context *a, int macro, as_text *body)
{
	as_frame *f = &a->frames[a->frame_count - 1];
	const char *start = f->p;
	int depth = 0;
	while (f->p < f->e) {
		const char *raw, *s;
		size_t n = as_statement_next(a, f, &raw, &s);
		const char *t = as_whitespace_skip(s, s + n);
		if (t == s + n || *t != '.') continue;
		as_text w = as_text_first_word(t, s + n);
		if (macro) {
			if (as_text_equals(w, ".endm") || as_text_equals(w, ".endmacro")) {
				if (!depth) { *body = (as_text){ start, (uint32_t)(raw - start) }; return 0; }
				depth--;
			} else if (as_text_equals(w, ".macro")) depth++;
		} else {
			if (as_text_equals(w, ".endr")) {
				if (!depth) { *body = (as_text){ start, (uint32_t)(raw - start) }; return 0; }
				depth--;
			} else if (as_directive_is_loop(w)) depth++;
		}
	}
	return as_error_report(a, macro ? "no matching '.endmacro' in definition" : "no matching '.endr' in definition");
}

/* parseMacroArgument: one argument as MC tokenises it, written as the text
 * the expansion will insert (strings unquoted unless vararg). */
static int as_text_is_operator_start(const char *p, const char *e)
{
	char c = *p;
	switch (c) {
	case '+': case '-': case '~': case '/': case '*': case '=': case '|': case '^':
	case '&': case '!': case '<': case '>':
		return 1;
	case '.':
		return !(p + 1 < e && as_char_is_identifier((unsigned char)p[1]));
	}
	return 0;
}
static int as_operator_length(const char *p, const char *e)
{
	char c = p[0], d = p + 1 < e ? p[1] : 0;
	if ((c == '=' && d == '=') || (c == '|' && d == '|') || (c == '&' && d == '&') || (c == '!' && d == '=') ||
	    (c == '<' && (d == '=' || d == '<' || d == '>')) || (c == '>' && (d == '=' || d == '>')))
		return 2;
	return 1;
}

/* as_macro_argument_parse when the argument is a contiguous piece of the text (no
 * string, no blanks around an operator, no error): *out points into the
 * text.  Returns 0 if as_macro_argument_parse must do it. */
static int as_macro_argument_parse_plain(const char **pp, const char *e, int vararg, as_text *out)
{
	const char *p = *pp, *end;
	if (vararg) {
		p = as_whitespace_skip(p, e);
		const char *q = e;
		while (q > p && (q[-1] == ' ' || q[-1] == '\t')) q--;
		*pp = e;
		*out = (as_text){ p, (uint32_t)(q - p) };
		return 1;
	}
	const char *s = p;
	int depth = 0;
	for (;;) {
		if (depth == 0) {
			if (p < e && *p == ',') { end = p; break; }
			if (p < e && (*p == ' ' || *p == '\t')) {
				end = p;
				p = as_whitespace_skip(p, e);
				if (p < e && as_text_is_operator_start(p, e)) return 0;
				break;
			}
			if (p < e && as_text_is_operator_start(p, e)) {
				if (*p == '=' && p + 1 < e && p[1] != '=') return 0;
				p += as_operator_length(p, e);
				if (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\f' || *p == '\v')) return 0;
				continue;
			}
		}
		if (p >= e) { end = p; break; }
		char c = *p;
		if (c == '(') depth++;
		else if (c == ')' && depth) depth--;
		if (c == '"') return 0;
		if (c == ' ' || c == '\t') { p = as_whitespace_skip(p, e); continue; }
		if (as_char_is_identifier((unsigned char)c)) { while (p < e && as_char_is_identifier((unsigned char)*p)) p++; continue; }
		if (c == '=' && depth == 0) return 0;
		p++;
	}
	if (depth) return 0;
	*pp = p;
	*out = (as_text){ s, (uint32_t)(end - s) };
	return 1;
}

static int as_macro_argument_parse(as_context *a, const char **pp, const char *e, int vararg, as_text *out)
{
	const char *p = *pp;
	char *buf = (char *)as_region_alloc(&a->expansion_text, (size_t)(e - p) + 1);
	size_t n = 0;
	if (vararg) {
		p = as_whitespace_skip(p, e);
		const char *q = e;
		while (q > p && (q[-1] == ' ' || q[-1] == '\t')) q--;
		memcpy(buf, p, (size_t)(q - p)); n = (size_t)(q - p);
		*pp = e;
		*out = (as_text){ buf, (uint32_t)n };
		return 0;
	}
	int depth = 0;
	for (;;) {
		int space = 0;
		if (depth == 0) {
			if (p < e && *p == ',') break;
			if (p < e && (*p == ' ' || *p == '\t')) { p = as_whitespace_skip(p, e); space = 1; }
			if (p < e && as_text_is_operator_start(p, e)) {
				if (*p == '=' && p + 1 < e && p[1] != '=') return as_error_report(a, "unexpected token in macro instantiation");
				int k = as_operator_length(p, e);
				memcpy(buf + n, p, (size_t)k); n += (size_t)k; p += k;
				p = as_whitespace_skip(p, e);
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
			const char *s = p; p = as_whitespace_skip(p, e);
			memcpy(buf + n, s, (size_t)(p - s)); n += (size_t)(p - s);
			continue;
		}
		if (as_char_is_identifier((unsigned char)c)) {
			const char *s = p;
			while (p < e && as_char_is_identifier((unsigned char)*p)) p++;
			memcpy(buf + n, s, (size_t)(p - s)); n += (size_t)(p - s);
			continue;
		}
		if (c == '=' && depth == 0) return as_error_report(a, "unexpected token in macro instantiation");
		buf[n++] = c; p++;
	}
	if (depth) return as_error_report(a, "unbalanced parentheses in argument");
	a->expansion_text.used = (size_t)((uint8_t *)buf - a->expansion_text.base) + n;
	*pp = p;
	*out = (as_text){ buf, (uint32_t)n };
	return 0;
}

/* expandMacro: body with \param, \@, \+, \() substitutions.  The body is
 * compiled once into pieces (r is the region for them), then each
 * expansion is copies. */
static as_template as_template_compile(as_context *a, vregion *r, as_text body, const as_macro_parameter *ps, uint32_t np, int at)
{
	const char *b = body.s;
	size_t n = body.n, nbs = 0;
	for (const char *q = b; (q = memchr(q, '\\', (size_t)(b + n - q))); q++) nbs++;
	as_template t = { as_region_alloc(r, sizeof(as_template_piece) * (nbs + 1)), 0 };
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
			while (j < n && as_char_is_identifier((unsigned char)b[j])) j++;
			uint32_t m;
			for (m = 0; m < np; m++) if (ps[m].name.n == j - pos && !memcmp(ps[m].name.s, b + pos, j - pos)) break;
			if (m == np) { i = pos; continue; }	/* '\' and the name stay as they are */
			k = (int32_t)m, skip = j - i;
		}
		t.p[t.n++] = (as_template_piece){ (uint32_t)lit, (uint32_t)(i - lit), k };
		i += skip;
		lit = i;
	}
	if (lit < n) t.p[t.n++] = (as_template_piece){ (uint32_t)lit, (uint32_t)(n - lit), -1 };
	(void)a;
	return t;
}

/* .altmacro: a parameter name is replaced wherever it stands as a whole
 * identifier, with or without the backslash */
static as_template as_template_compile_altmacro(vregion *r, as_text body, const as_macro_parameter *ps, uint32_t np)
{
	const char *b = body.s;
	size_t n = body.n;
	as_template t = { as_region_alloc(r, sizeof(as_template_piece) * (n / 2 + 2)), 0 };
	size_t lit = 0, i = 0;
	while (i < n) {
		size_t st = i;
		int bs = b[i] == '\\';
		if (bs) i++;
		if (i < n && as_char_is_identifier((unsigned char)b[i]) && (bs || st == 0 || !as_char_is_identifier((unsigned char)b[st - 1]))) {
			size_t j = i;
			while (j < n && as_char_is_identifier((unsigned char)b[j])) j++;
			uint32_t m;
			for (m = 0; m < np; m++) if (ps[m].name.n == j - i && !memcmp(ps[m].name.s, b + i, j - i)) break;
			if (m < np) {
				t.p[t.n++] = (as_template_piece){ (uint32_t)lit, (uint32_t)(st - lit), (int32_t)m };
				lit = j;
			}
			i = j;
			continue;
		}
		if (!bs) i++;
	}
	if (lit < n) t.p[t.n++] = (as_template_piece){ (uint32_t)lit, (uint32_t)(n - lit), -1 };
	return t;
}

/* an .altmacro argument: %expr is its value in decimal, <text> is text with
 * !c standing for c */
static int as_macro_argument_parse_altmacro(as_context *a, const char **pp, const char *e, as_text *out)
{
	const char *p = *pp;
	if (*p == '%') {
		p++;
		int64_t v;
		if (as_expression_absolute_parse(a, &p, e, &v)) return -1;
		char buf[24];
		int k = snprintf(buf, sizeof buf, "%lld", (long long)v);
		*out = as_text_duplicate(a, buf, (size_t)k);
	} else {
		char *o = as_region_alloc(&a->definitions, (size_t)(e - p) + 1);
		size_t n = 0;
		for (p++; p < e && *p != '>'; p++) {
			if (*p == '!' && p + 1 < e) p++;
			o[n++] = *p;
		}
		if (p >= e) return as_error_report(a, "expected '>'");
		p++;
		*out = (as_text){ o, (uint32_t)n };
	}
	*pp = p;
	return 0;
}

static char *as_unsigned_format(char *o, uint32_t v)
{
	char tmp[10]; int k = 0;
	do tmp[k++] = (char)('0' + v % 10), v /= 10; while (v);
	while (k) *o++ = tmp[--k];
	return o;
}

/* copy n bytes to o, which may be overwritten up to 16 bytes past n (the
 * expansion stack has room); short copies are one 16-byte load when that
 * stays in the source's page */
static inline void as_text_copy_overrun(char *o, const char *s, size_t n)
{
#ifdef __aarch64__
	if (n <= 16 && ((uintptr_t)s & 4095) <= 4096 - 16) { vst1q_u8((uint8_t *)o, vld1q_u8((const uint8_t *)s)); return; }
#endif
	memcpy(o, s, n);
}

static size_t as_template_expand(as_context *a, char *o, const char *b, as_template t, const as_text *args, uint32_t count)
{
	char *o0 = o;
	for (uint32_t i = 0; i < t.n; i++) {
		const as_template_piece *p = &t.p[i];
		as_text_copy_overrun(o, b + p->off, p->len); o += p->len;
		if (p->k >= 0) { as_text_copy_overrun(o, args[p->k].s, args[p->k].n); o += args[p->k].n; }
		else if (p->k == -2) o = as_unsigned_format(o, a->ninst);
		else if (p->k == -3) o = as_unsigned_format(o, count);
	}
	return (size_t)(o - o0);
}

static size_t as_template_expand_bound(as_text body, const as_text *args, uint32_t np)
{
	size_t m = 0;
	for (uint32_t k = 0; k < np; k++) if (args[k].n > m) m = args[k].n;
	return body.n * (m + 12) + 16;
}

VPRIV int as_directive_macro(as_context *a, const char *p, const char *e)
{
	as_text name;
	/* names from file text (not a->sb) stay valid; others are copied */
	int keep = a->frames[a->frame_count - 1].kind == AS_FRAME_FILE && !((const uint8_t *)p >= a->sb.base && (const uint8_t *)p < a->sb.base + a->sb.cap);
	p = as_whitespace_skip(p, e);
	const char *s = p;
	while (p < e && as_char_is_identifier((unsigned char)*p)) p++;
	if (p == s) return as_error_report(a, "expected identifier in '.macro' directive");
	name = keep ? (as_text){ s, (uint32_t)(p - s) } : as_text_duplicate(a, s, (size_t)(p - s));
	as_macro_parameter ps[64]; uint32_t np = 0;
	p = as_whitespace_skip(p, e);
	if (p < e && *p == ',') p = as_whitespace_skip(p + 1, e);
	while (p < e) {
		if (np >= 64) return as_error_report(a, "too many macro parameters");
		as_macro_parameter *q = &ps[np++];
		memset(q, 0, sizeof *q);
		s = p;
		while (p < e && as_char_is_identifier((unsigned char)*p)) p++;
		if (p == s) return as_error_report(a, "expected identifier in '.macro' directive");
		q->name = keep ? (as_text){ s, (uint32_t)(p - s) } : as_text_duplicate(a, s, (size_t)(p - s));
		p = as_whitespace_skip(p, e);
		if (p < e && *p == ':') {
			p = as_whitespace_skip(p + 1, e);
			s = p;
			while (p < e && as_char_is_identifier((unsigned char)*p)) p++;
			as_text qq = { s, (uint32_t)(p - s) };
			if (as_text_equals(qq, "req")) q->is_required = 1;
			else if (as_text_equals(qq, "vararg")) q->vararg = 1;
			else return as_error_report(a, "invalid macro parameter qualifier");
			p = as_whitespace_skip(p, e);
		}
		if (p < e && *p == '=') {
			p = as_whitespace_skip(p + 1, e);
			as_text d;
			size_t mark = a->expansion_text.used;
			if (as_macro_argument_parse(a, &p, e, 0, &d)) return -1;
			q->def = as_text_duplicate(a, d.s, d.n);
			a->expansion_text.used = mark;
			p = as_whitespace_skip(p, e);
		}
		if (p < e && *p == ',') p = as_whitespace_skip(p + 1, e);
	}
	as_text body;
	if (as_macro_body_collect(a, 1, &body)) return -1;
	as_macro *m = as_region_alloc(&a->definitions, sizeof *m);
	m->name = name;
	m->params = as_region_alloc(&a->definitions, sizeof(as_macro_parameter) * (np ? np : 1));
	memcpy(m->params, ps, sizeof(as_macro_parameter) * np);
	m->np = np;
	/* file text outlives the macro (as_snippet_assemble's contract); expansions do not */
	m->body = a->frames[a->frame_count - 1].kind == AS_FRAME_FILE ? body : as_text_duplicate(a, body.s, body.n);
	m->count = 0;
	m->t = (as_template){ 0, 0 };
	if (a->macro_count * 2 >= a->macro_mask) {
		uint32_t nm = a->macro_mask * 2 + 1;
		as_macro **t = as_region_alloc(&a->definitions, sizeof *t * (nm + 1));
		memset(t, 0, sizeof *t * (nm + 1));
		for (uint32_t i = 0; i <= a->macro_mask; i++)
			for (as_macro *x = a->macro_table[i], *nx; x; x = nx) { nx = x->next; uint32_t h = as_text_hash(x->name.s, x->name.n) & nm; x->next = t[h]; t[h] = x; }
		a->macro_table = t; a->macro_mask = nm;
	}
	uint32_t h = as_text_hash(name.s, name.n) & a->macro_mask;
	for (as_macro *x = a->macro_table[h]; x; x = x->next)
		if (x->name.n == name.n && !memcmp(x->name.s, name.s, name.n)) return as_error_report(a, "macro '%.*s' is already defined", (int)name.n, name.s);
	m->next = a->macro_table[h]; a->macro_table[h] = m; a->macro_count++;
	return 0;
}

VPRIV int as_directive_purgem(as_context *a, const char *p, const char *e)
{
	as_text n = as_text_first_word(p, e);
	uint32_t h = as_text_hash(n.s, n.n) & a->macro_mask;
	for (as_macro **pm = &a->macro_table[h]; *pm; pm = &(*pm)->next)
		if ((*pm)->name.n == n.n && !memcmp((*pm)->name.s, n.s, n.n)) { *pm = (*pm)->next; a->macro_count--; return 0; }
	return as_error_report(a, "macro '%.*s' is not defined", (int)n.n, n.s);
}

VPRIV int as_macro_invoke(as_context *a, as_macro *m, const char *p, const char *e)
{
	size_t mark = a->expansion_text.used;
	as_text args[64];
	uint8_t set[64] = { 0 };
	uint32_t np = m->np, pi = 0;
	p = as_whitespace_skip(p, e);
	int hasvararg = np && m->params[np - 1].vararg;
	for (uint32_t k = 0; k < np; k++) args[k] = (as_text){ "", 0 };
	if (p < e) {
		for (;;) {
			/* keyword argument? */
			const char *q = p;
			while (q < e && as_char_is_identifier((unsigned char)*q)) q++;
			const char *r = as_whitespace_skip(q, e);
			int kw = q > p && r < e && *r == '=' && !(r + 1 < e && r[1] == '=');
			uint32_t k;
			if (kw) {
				for (k = 0; k < np; k++) if (m->params[k].name.n == (uint32_t)(q - p) && !memcmp(m->params[k].name.s, p, (size_t)(q - p))) break;
				if (k == np) return as_error_report(a, "parameter named '%.*s' does not exist for macro '%.*s'", (int)(q - p), p, (int)m->name.n, m->name.s);
				p = as_whitespace_skip(r + 1, e);
			} else {
				if (pi >= np) {
					if (!np) return as_error_report(a, "too many positional arguments");
					return as_error_report(a, "too many positional arguments");
				}
				k = pi++;
			}
			/* the arguments are used before the statement text changes */
			int va = hasvararg && k == np - 1;
			if (a->altmacro_mode && p < e && (*p == '%' || *p == '<')) { if (as_macro_argument_parse_altmacro(a, &p, e, &args[k])) return -1; }
			else if (!as_macro_argument_parse_plain(&p, e, va, &args[k]) && as_macro_argument_parse(a, &p, e, va, &args[k])) return -1;
			set[k] = 1;
			p = as_whitespace_skip(p, e);
			if (p >= e) break;
			if (*p == ',') { p = as_whitespace_skip(p + 1, e); if (p >= e) { if (pi < np) set[pi] = 1, args[pi] = (as_text){ "", 0 }; break; } }
		}
	}
	for (uint32_t k = 0; k < np; k++) {
		if (set[k] && args[k].n) continue;
		if (!set[k] || !args[k].n) {
			if (m->params[k].def.n && !(set[k] && args[k].n)) args[k] = m->params[k].def;
			else if (m->params[k].is_required && !set[k]) return as_error_report(a, "missing value for required parameter '%.*s' in macro '%.*s'", (int)m->params[k].name.n, m->params[k].name.s, (int)m->name.n, m->name.s);
			else if (m->params[k].is_required && !args[k].n) return as_error_report(a, "missing value for required parameter '%.*s' in macro '%.*s'", (int)m->params[k].name.n, m->params[k].name.s, (int)m->name.n, m->name.s);
		}
	}
	as_template tt;
	if (a->altmacro_mode) tt = as_template_compile_altmacro(&a->definitions, m->body, m->params, np);
	else {
		if (!m->t.p) m->t = as_template_compile(a, &a->definitions, m->body, m->params, np, 1);
		tt = m->t;
	}
	char *o = (char *)as_region_alloc(&a->expansion_text, as_template_expand_bound(m->body, args, np));
	size_t n = as_template_expand(a, o, m->body.s, tt, args, m->count);
	a->expansion_text.used = (size_t)((uint8_t *)o - a->expansion_text.base) + n;
	a->ninst++;
	m->count++;
	return as_frame_push(a, o, n, AS_FRAME_MACRO, mark);
}

VPRIV int as_directive_rept(as_context *a, const char *p, const char *e)
{
	int64_t cnt;
	if (as_expression_absolute_parse(a, &p, e, &cnt)) return -1;
	if (cnt < 0) return as_error_report(a, "Count is negative");
	uint32_t at = a->frame_count ? a->frames[a->frame_count - 1].statement_line : 0;	/* (the .rept line, not .endr's) */
	as_text body;
	if (as_macro_body_collect(a, 0, &body)) return -1;
	size_t mark = a->expansion_text.used;
	as_template t = as_template_compile(a, &a->expansion_text, body, 0, 0, 0);
	char *o = (char *)as_region_alloc(&a->expansion_text, as_template_expand_bound(body, 0, 0) * (size_t)(cnt ? cnt : 1));
	size_t n = 0;
	for (int64_t i = 0; i < cnt; i++) n += as_template_expand(a, o + n, body.s, t, 0, 0);
	a->expansion_text.used = (size_t)((uint8_t *)o - a->expansion_text.base) + n;
	if (as_frame_push(a, o, n, AS_FRAME_LOOP, mark)) return -1;
	a->frames[a->frame_count - 1].caller_line = at;
	return 0;
}

VPRIV int as_directive_irp(as_context *a, const char *p, const char *e, int chars)
{
	p = as_whitespace_skip(p, e);
	const char *s = p;
	while (p < e && as_char_is_identifier((unsigned char)*p)) p++;
	if (p == s) return as_error_report(a, "expected identifier in '.irp' directive");
	as_macro_parameter par; memset(&par, 0, sizeof par);
	par.name = (as_text){ s, (uint32_t)(p - s) };
	p = as_whitespace_skip(p, e);
	if (p >= e || *p != ',') return as_error_report(a, "expected comma");
	p = as_whitespace_skip(p + 1, e);
	size_t mark = a->expansion_text.used;
	as_text vals[1024]; uint32_t nv = 0;
	for (;;) {
		if (nv >= 1024) return as_error_report(a, "too many .irp values");
		if (as_macro_argument_parse(a, &p, e, 0, &vals[nv++])) return -1;
		p = as_whitespace_skip(p, e);
		if (p >= e) break;
		if (*p == ',') p = as_whitespace_skip(p + 1, e);
	}
	as_text body;
	/* the param name lives in a->sb, which as_macro_body_collect overwrites */
	par.name = as_text_duplicate(a, par.name.s, par.name.n);
	uint32_t at = a->frame_count ? a->frames[a->frame_count - 1].statement_line : 0;
	if (as_macro_body_collect(a, 0, &body)) return -1;
	if (chars) {
		as_text v = vals[0];
		nv = 0;
		as_text cv[1024];
		for (uint32_t i = 0; i < v.n && i < 1024; i++) cv[nv++] = (as_text){ v.s + i, 1 };
		memcpy(vals, cv, sizeof(as_text) * nv);
	}
	size_t tot = 0;
	for (uint32_t i = 0; i < nv; i++) tot += as_template_expand_bound(body, &vals[i], 1);
	as_template t = as_template_compile(a, &a->expansion_text, body, &par, 1, 1);
	char *o = (char *)as_region_alloc(&a->expansion_text, tot + 1);
	size_t n = 0;
	for (uint32_t i = 0; i < nv; i++) n += as_template_expand(a, o + n, body.s, t, &vals[i], 0);
	a->expansion_text.used = (size_t)((uint8_t *)o - a->expansion_text.base) + n;
	if (as_frame_push(a, o, n, AS_FRAME_LOOP, mark)) return -1;
	a->frames[a->frame_count - 1].caller_line = at;
	return 0;
}

VPRIV int as_directive_exitm(as_context *a)
{
	for (int i = a->frame_count - 1; i >= 0; i--) {
		if (a->frames[i].kind == AS_FRAME_MACRO) {
			a->condition_count = (int)a->frames[i].condition_count;
			while (a->frame_count > i) as_frame_pop(a);
			return 0;
		}
	}
	return as_error_report(a, "unexpected '.exitm' in file, no current macro definition");
}
