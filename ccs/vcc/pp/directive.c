/* vcc preprocessor: directives: conditionals, #pragma, #line, the dispatcher */
#include "ppimpl.h"

/* ---------------- directives ---------------- */

static bool cond_true(pp *p, const ctx *c, const pdir *d)
{
	uint32_t b = d->tok + 2;

	switch (d->kind) {
	case D_IFDEF: case D_IFNDEF: case D_ELIFDEF: case D_ELIFNDEF: {
		bool def;

		if (b >= d->end || c->vt[b].kind != T_IDENT) {
			error(p, c->loc + c->vt[d->tok].off, "macro name missing");
			return false;
		}
		def = mdefined(p, c->vt[b].id);
		return (d->kind == D_IFDEF || d->kind == D_ELIFDEF) ? def : !def;
	}
	default:
		return eval_if(p, c, b, d->end);
	}
}

/* in a false group: run on from directive k to the group that is taken */
static void skip_from(pp *p, ctx *c, uint32_t k)
{
	const pdir *dir = c->fi->dir;

	for (;;) {
		const pdir *d = &dir[k];

		if ((d->kind == D_ELIF || d->kind == D_ELIFDEF || d->kind == D_ELIFNDEF) && !cond_true(p, c, d)) {
			k = d->next;
			continue;
		}
		c->i = d->end;
		c->di = k + 1;
		return;
	}
}

/*
 * Parse "#define" tokens vt[b, e) into *m (pidx in memory from mem).
 * Returns 0, or an error message (*eoff: the token it is at).
 */
VPRIV const char *parse_define(const vtok *vt, uint32_t b, uint32_t e, pmacro *m,
				void *(*mem)(void *, size_t), void *mc, uint32_t *eoff)
{
	uint32_t params[256], np = 0, i = b + 1;
	uint8_t *pidx = NULL;
	bool va = false;

	memset(m, 0, sizeof(*m));
	m->name = vt[b].id;
	m->kind = MK_OBJ;
	*eoff = b;
	if (i < e && vt[i].kind == T_LPAREN && !(vt[i].flags & TF_SPACE)) {
		m->kind = MK_FUNC;
		i++;
		if (i < e && vt[i].kind == T_RPAREN) {
			i++;
		} else {
			for (;;) {
				if (i >= e)
					return "invalid macro parameter list";
				if (vt[i].kind == T_ELLIPSIS) {
					params[np++] = KW_VA_ARGS;
					va = true;
					i++;
				} else if (vt[i].kind == T_IDENT && np < 256) {
					params[np++] = vt[i++].id;
					if (i < e && vt[i].kind == T_ELLIPSIS) {
						va = true;	/* GNU named variadic */
						i++;
					}
				} else {
					return "invalid macro parameter list";
				}
				if (i < e && vt[i].kind == T_RPAREN && (i++, 1))
					break;
				if (va || i >= e || vt[i].kind != T_COMMA)
					return "invalid macro parameter list";
				i++;
			}
		}
	}
	m->body = vt + i;
	m->nbody = e - i;
	if (m->kind == MK_FUNC) {
		m->plist = vt + b + 1;
		m->nplist = (uint16_t)(i - b - 1 < 0xffff ? i - b - 1 : 0);
	}
	m->nparam = (uint16_t)np;
	if (va)
		m->flags |= MF_VARIADIC;
	for (uint32_t j = i; j < e; j++) {
		if (vt[j].kind == T_HASHHASH)
			m->flags |= MF_PASTE;
		if (vt[j].kind == T_IDENT && vt[j].id == KW_VA_OPT)
			m->flags |= MF_VAOPT;
		if (vt[j].kind == T_HASH)
			m->flags |= MF_HASH;
	}
	if (m->nbody && (m->body[0].kind == T_HASHHASH || m->body[m->nbody - 1].kind == T_HASHHASH))
		return "'##' cannot appear at either end of a macro expansion";
	if (np) {
		pidx = mem(mc, m->nbody + 1);
		for (uint32_t j = 0; j < m->nbody; j++) {
			pidx[j] = 0;
			if (m->body[j].kind != T_IDENT)
				continue;
			for (uint32_t k = 0; k < np; k++)
				if (params[k] == m->body[j].id) {
					pidx[j] = (uint8_t)(k + 1);
					break;
				}
		}
		m->pidx = pidx;
	}
	if (m->kind == MK_FUNC)
		for (uint32_t j = 0; j < m->nbody; j++)
			if (m->body[j].kind == T_HASH && !(j + 1 < m->nbody && pidx && pidx[j + 1]) &&
			    !(j + 1 < m->nbody && m->body[j + 1].kind == T_IDENT && m->body[j + 1].id == KW_VA_OPT)) {
				m->flags |= MF_BADHASH;
				*eoff = i + j;
				break;
			}
	return NULL;
}

VPRIV void *mem_perm(void *a, size_t n) { return varena_alloc(a, n); }
VPRIV void *mem_shared(void *a, size_t n) { (void)a; return salloc(n); }

/* the macro name in ("NAME") of push_macro/pop_macro, or 0 */
static uint32_t pragma_macro_name(const char *s, size_t n)
{
	const char *e = s + n, *q;

	while (s < e && *s != '"')
		s++;
	if (s == e)
		return 0;
	s++;
	q = memchr(s, '"', (size_t)(e - s));
	if (!q || q == s)
		return 0;
	{
		char b[q - s + 17];

		memcpy(b, s, (size_t)(q - s));
		memset(b + (q - s), 0, 17);
		return vintern_copy(b, (size_t)(q - s));
	}
}

static bool word(const char **s, const char *e, const char *w)
{
	const char *q = *s;
	size_t n = strlen(w);

	while (q < e && (*q == ' ' || *q == '\t'))
		q++;
	if ((size_t)(e - q) < n || memcmp(q, w, n) || (q + n < e && idch((unsigned char)q[n])))
		return false;
	*s = q + n;
	return true;
}

/*
 * A pragma, text "pragma ..." (from #pragma or _Pragma).  The ones the
 * preprocessor implements are run and dropped, as clang -E does; the rest
 * become a P_DIRECTIVE token.  Returns 1 if *out was set.
 */
VPRIV int pragma_text(pp *p, const char *s, size_t n, uint32_t loc, ptok *out)
{
	const char *q = s + 6, *e = s + n;	/* after "pragma" */

	if (word(&q, e, "once")) {
		const ctx *fc = cur_file(p);

		if (fc)
			once_add(p, fc->f);
		return 0;
	}
	if (word(&q, e, "push_macro")) {
		uint32_t id = pragma_macro_name(q, (size_t)(e - q));

		HC_BAD(p);
		if (id && p->npushed < 4096) {
			p->pushed[p->npushed].id = id;
			p->pushed[p->npushed++].m = mread(p, id) & ~(uintptr_t)1;
		}
		return 0;
	}
	if (word(&q, e, "pop_macro")) {
		uint32_t id = pragma_macro_name(q, (size_t)(e - q));

		HC_BAD(p);
		for (uint32_t i = p->npushed; i-- > 0;)
			if (p->pushed[i].id == id) {
				mdef(p, id, (const pmacro *)p->pushed[i].m);
				memmove(&p->pushed[i], &p->pushed[i + 1], (p->npushed - i - 1) * sizeof(p->pushed[0]));
				p->npushed--;
				break;
			}
		return 0;
	}
	{
		const char *r = q;

		if (word(&r, e, "GCC") && (word(&r, e, "system_header") || word(&r, e, "poison") ||
					    word(&r, e, "dependency")))
			return 0;
	}
	memset(out, 0, sizeof(*out));
	out->kind = P_DIRECTIVE;
	out->s = s;
	out->len = (uint32_t)n;
	out->loc = loc;
	out->flags = TF_SOL;
	return 1;
}

/* #line N ["file"] (macro-expanded) or # N "file" flags (GNU line marker) */
static void line_directive(pp *p, ctx *c, uint32_t b, uint32_t e, bool expand_line, uint32_t loc)
{
	int d = p->depth;
	const vtok *last = &c->vt[e - 1];
	struct lmark *m;
	ptok n, f;
	ival v;

	push_line(p, c, b, e);
	if (expand_line) {
		p->in_dir++;
		next_exp(p, &n);
		next_exp(p, &f);
		p->in_dir--;
	} else {
		raw(p, &n);
		raw(p, &f);
	}
	if (n.kind != T_NUMBER) {
		pop_to(p, d);
		error(p, loc, "#line directive requires a positive integer argument");
		return;
	}
	v = parse_number(p, &n);
	if (p->nlm == 65536) {
		pop_to(p, d);
		return;
	}
	m = &p->lm[p->nlm++];
	m->fe = c->fe;
	m->off = last->off + last->len;
	m->pline = file_line(c->f, c->fi, m->off) + 1;
	m->line = (uint32_t)v.v;
	m->name = NULL;
	if (f.kind == T_STRING) {
		char tmp[f.len + 1], *w;
		size_t l;
		const char *s = pp_spell(&f, tmp, &l);

		w = varena_alloc(&p->perm, l + 1);
		m->name = w;
		for (size_t i = 1; i + 1 < l; i++) {
			if (s[i] == '\\' && i + 2 < l)
				i++;
			*w++ = s[i];
		}
		m->namelen = (uint32_t)(w - m->name);
	}
	pop_to(p, d);
}

/* run the directive at c->i; 1 if it produced a token in *out */
VPRIV int directive(pp *p, ctx *c, ptok *out)
{
	const pdir *d = &c->fi->dir[c->di];
	uint32_t b = d->tok + 1, e = d->end, loc = c->loc + c->vt[d->tok].off;

	c->i = e;
	c->di++;
	switch (d->kind) {
	case D_IF: case D_IFDEF: case D_IFNDEF:
		if (!cond_true(p, c, d))
			skip_from(p, c, d->next);
		return 0;
	case D_ELIF: case D_ELIFDEF: case D_ELIFNDEF: case D_ELSE: {
		/* the end of a group that was taken: on to #endif */
		uint32_t k = d->next;

		while (c->fi->dir[k].kind != D_ENDIF)
			k = c->fi->dir[k].next;
		c->i = c->fi->dir[k].end;
		c->di = k + 1;
		return 0;
	}
	case D_ENDIF: case D_NULL:
		return 0;
	case D_DEFINE:
		define(p, c, b + 1, e);
		return 0;
	case D_UNDEF:
		if (b + 1 < e && c->vt[b + 1].kind == T_IDENT)
			mdef(p, c->vt[b + 1].id, NULL);
		else
			error(p, loc, "macro name must be an identifier");
		return 0;
	case D_INCLUDE: case D_INCLUDE_NEXT: case D_IMPORT:
		include(p, c, b + 1, e, (int)d->kind, loc);
		return 0;
	case D_PRAGMA: {
		const vtok *f = &c->vt[b], *l = &c->vt[e - 1];

		return pragma_text(p, c->src + f->off, l->off + l->len - f->off, loc, out);
	}
	case D_IDENT: {
		const vtok *f = &c->vt[b], *l = &c->vt[e - 1];

		memset(out, 0, sizeof(*out));
		out->kind = P_DIRECTIVE;
		out->s = c->src + f->off;
		out->len = l->off + l->len - f->off;
		out->loc = loc;
		out->flags = TF_SOL;
		return 1;
	}
	case D_ERROR: case D_WARNING: {
		const vtok *f = &c->vt[b], *l = &c->vt[e - 1];

		/* clang: the text after the directive name, at the name */
		uint32_t nloc = loc + (f->off - c->vt[b - 1].off);

		if (e - b > 1)
			f = &c->vt[b + 1];
		else
			f = l + 1;
		{
			int lv = d->kind == D_ERROR ? VDL_ERROR : pp_diag_wlevel(p, "#warnings", true, NULL, UINT32_MAX);
			vdiag *r;

			if (lv < 0)
				return 0;
			r = pp_diag_begin(p, lv, nloc, "%.*s", e - b > 1 ? (int)(l->off + l->len - f->off) : 0,
					  e - b > 1 ? c->src + f->off : "");
			r->id = d->kind == D_ERROR ? "err_pp_hash_error" : "pp_hash_warning";
			if (d->kind == D_WARNING)
				pp_diag_flag(r, "#warnings"), r->werror = lv == VDL_ERROR;
			pp_diag_emit(p, r);
		}
		return 0;
	}
	case D_LINEMARK:
		if (p->o->asm_mode)
			goto pass;
		/* fallthrough */
	case D_LINE:
		line_directive(p, c, d->kind == D_LINE ? b + 1 : b, e, d->kind == D_LINE, loc);
		return 0;
	default:
		if (p->o->asm_mode)
			goto pass;
		error(p, loc, "invalid preprocessing directive");
		return 0;
	}
pass: {
	/* assembler: the line as written ("# comment") */
	const vtok *f = &c->vt[b], *l = &c->vt[e - 1];

	memset(out, 0, sizeof(*out));
	out->kind = P_DIRECTIVE;
	out->s = c->src + f->off;
	out->len = l->off + l->len - f->off;
	out->loc = loc;
	out->flags = TF_SOL;
	return 1;
	}
}
