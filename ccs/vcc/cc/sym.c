/* vcc front end: scopes, symbols and AST nodes */
#include "ccimpl.h"

/* ---------------- scopes and symbols ---------------- */

VPRIV uint32_t *dslot(cc *c, uint32_t **dir, uint32_t id)
{
	uint32_t **pg = &dir[id >> DIR_BITS];

	if (unlikely(!*pg)) {
		*pg = halloc(c, DIR_PAGE * sizeof(uint32_t));
		memset(*pg, 0, DIR_PAGE * sizeof(uint32_t));
	}
	return &(*pg)[id & (DIR_PAGE - 1)];
}

VPRIV void scope_open(cc *c)
{
	if (c->depth >= 4095)
		fatal(c, tok(c)->loc, "scopes nested too deeply");
	c->smark[++c->depth] = c->nsstack;
}

VPRIV void scope_close(cc *c)
{
	uint32_t m = c->smark[c->depth];

	while (c->nsstack > m) {
		uint32_t s = c->sstack[--c->nsstack];
		sym *y = &c->syms[s];

		/* (bind allocated the page) */
		(y->kind == S_TAG ? c->tdir : c->odir)[y->name >> DIR_BITS][y->name & (DIR_PAGE - 1)] = y->shadow;
	}
	c->depth--;
}

VPRIV uint32_t sym_new(cc *c, int kind, uint32_t name, qty ty, uint32_t loc)
{
	uint32_t i = c->nsym++;
	sym *s = &c->syms[i];

	memset(s, 0, sizeof(*s));
	s->kind = (uint8_t)kind;
	s->name = name;
	s->ty = ty;
	s->loc = loc;
	s->depth = c->depth;
	return i;
}

/* bind s in the current scope */
VPRIV void sym_bind(cc *c, uint32_t s)
{
	sym *y = &c->syms[s];
	uint32_t *slot;

	if (!y->name)
		return;
	slot = dslot(c, y->kind == S_TAG ? c->tdir : c->odir, y->name);
	y->shadow = *slot;
	y->depth = c->depth;
	*slot = s;
	c->sstack[c->nsstack++] = s;
}

/* in a lazy body (lazy.c): the file-scope binding visible at its definition */
static __attribute__((noinline)) uint32_t lz_visible(cc *c, uint32_t s)
{
	while (s >= c->lz_wm && c->syms[s].depth == 0)
		s = c->syms[s].shadow;
	return s;
}

VPRIV uint32_t lookup(cc *c, uint32_t id)
{
	uint32_t s = dget(c->odir, id);

	return unlikely(c->lz_wm) && s >= c->lz_wm ? lz_visible(c, s) : s;
}

VPRIV uint32_t lookup_tag(cc *c, uint32_t id)
{
	uint32_t s = dget(c->tdir, id);

	return unlikely(c->lz_wm) && s >= c->lz_wm ? lz_visible(c, s) : s;
}

/* Levenshtein distance, giving up above max */
static int edit_dist(const char *a, int na, const char *b, int nb, int max)
{
	int row[nb + 1];

	if (na - nb > max || nb - na > max)
		return max + 1;
	for (int j = 0; j <= nb; j++)
		row[j] = j;
	for (int i = 1; i <= na; i++) {
		int diag = row[0], best;

		row[0] = best = i;
		for (int j = 1; j <= nb; j++) {
			int up = row[j], v = diag + (a[i - 1] != b[j - 1]);

			if (up + 1 < v)
				v = up + 1;
			if (row[j - 1] + 1 < v)
				v = row[j - 1] + 1;
			diag = up, row[j] = v;
			if (v < best)
				best = v;
		}
		if (best > max)
			return max + 1;
	}
	return row[nb];
}

/* keywords clang offers as corrections (Sema's CTypeSpecs) where a type may stand,
   and at the start of a statement the statement keywords too */
static const uint16_t kw_types[] = {
	KW_char, KW_const, KW_double, KW_enum, KW_float, KW_int, KW_long, KW_short, KW_signed,
	KW_struct, KW_union, KW_unsigned, KW_void, KW_volatile, KW__Complex, KW_extern, KW_inline,
	KW_static, KW_typedef, KW_restrict, KW__Bool, KW_bool,
};
static const uint16_t kw_stmts[] = {
	KW_return, KW_goto, KW_else, KW_if, KW_for, KW_while, KW_do, KW_switch, KW_break, KW_continue,
	KW_case, KW_default,
};

static bool kw_offered(cc *c, uint16_t k)
{
	if (!kwmap[k])	/* bool before C23 */
		return false;
	if (k == KW_break)
		return c->in_loop || c->in_switch;
	if (k == KW_continue)
		return c->in_loop;
	if (k == KW_case || k == KW_default)
		return c->in_switch;
	return true;
}

/* clang's typo correction: the visible symbol closest to id (edit distance at most
   (len + 2) / 3), of a type name (types) or of anything else, or with kws (KWS_*)
   a keyword closer than any symbol (*kw); rejected, as clang does, when the name is
   under three times as long as the distance.  Returns the symbol, 0 if none */
static uint32_t typo_sym(cc *c, uint32_t id, bool types, int kws, uint32_t *kw)
{
	int n, max, best;
	const char *s = idname(id, &n);
	uint32_t pick = 0, kpick = 0;
	int lim = pp_spell_limit(c->p);

	if (kw)
		*kw = 0;
	/* clang tries 50 corrections per TU (failed ones count): each scans every symbol */
	if (lim == -2 || (lim != -1 && c->ntypo++ >= (uint32_t)(lim ? lim : 50)))
		return 0;
	max = best = (n + 2) / 3;
	for (uint32_t i = 1; i < c->nsym; i++) {
		const sym *y = &c->syms[i];
		int m, d;
		const char *t;

		if (!y->name || y->name == id || (y->kind == S_TYPEDEF) != types || y->kind == S_BUILTIN)
			continue;
		t = idname(y->name, &m);
		if (m > 1 && t[0] == '_' && (t[1] == '_' || (t[1] >= 'A' && t[1] <= 'Z')))
			continue;
		if (lookup(c, y->name) != i)
			continue;
		d = edit_dist(s, n, t, m, max);
		if (d <= max && (!pick || d < best))
			pick = i, best = d;
	}
	for (int l = 0; kws && l < 2; l++) {
		const uint16_t *v = l ? kw_stmts : kw_types;
		size_t nv = l ? sizeof(kw_stmts) / sizeof(kw_stmts[0]) : sizeof(kw_types) / sizeof(kw_types[0]);

		if (l && !(kws & KWS_STMT))
			break;
		for (size_t i = 0; i < nv; i++) {
			int m, d;
			const char *t;

			if (!kw_offered(c, v[i]))
				continue;
			t = idname(v[i], &m);
			d = edit_dist(s, n, t, m, max);
			if (d <= max && (!(pick || kpick) || d < best))
				kpick = v[i], pick = 0, best = d;
		}
	}
	if ((pick || kpick) && best > 0 && n / best < 3)
		pick = kpick = 0;
	if (kw)
		*kw = kpick;
	return pick;
}

/* the keyword id is a typo of at the start of a statement (KWS_STMT) or where a type
   may stand (KWS_TYPE), if a keyword is its best correction; 0 if not */
VPRIV uint32_t typo_keyword(cc *c, uint32_t id, int kws)
{
	uint32_t kw;

	if (tok(c)->flags & PF_MACRO)
		return 0;
	typo_sym(c, id, kws == KWS_TYPE, kws, &kw);
	return kw;
}

/* "<msg> 'id'; did you mean 'x'?" with the fix-it and the "declared here" note,
   or just "<msg> 'id'"; returns the correction, a symbol or with kws (KWS_*) and kw
   a keyword in *kw */
VPRIV uint32_t undeclared(cc *c, const char *msg, uint32_t id, uint32_t loc, bool types, int kws, uint32_t *kw)
{
	/* clang does not correct a name from a macro expansion (and says where it was spelled) */
	bool mac = (tok(c)->loc == loc ? tok(c)->flags : c->prevflags) & PF_MACRO;
	uint32_t k = 0, s = mac ? 0 : typo_sym(c, id, types, kw ? kws : 0, &k);
	pp_span r = { loc, pp_tok_end(c->p, loc) };
	bool tn = msg[1] == 'n';	/* "unknown type name", else "use of undeclared identifier" */

	if (kw)
		*kw = k;
	if (mac) {
		int m;
		const char *t = idname(id, &m);
		pp_dinfo d = { "error", loc, &r, 1, NULL, 0, t, (uint32_t)m };

		error_limit(c), c->errors++, c->ndiag++;
		pp_diagx(c->p, &d, tn ? "unknown type name '%.*s'" : "use of undeclared identifier '%.*s'", IDF(id));
		return 0;
	}

	if (s || k) {
		int m;
		const char *t = idname(s ? c->syms[s].name : k, &m);
		char *fs = halloc(c, (size_t)m + 1);
		pp_fixit fx;

		memcpy(fs, t, m), fs[m] = 0;
		fx = (pp_fixit){ r.b, r.e, fs };
		report(c, "error", loc, &r, 1, &fx, 1, tn ? "unknown type name '%.*s'; did you mean '%s'?"
		       : "use of undeclared identifier '%.*s'; did you mean '%s'?", IDF(id), fs);
		if (s)
			note(c, c->syms[s].loc, "'%s' declared here", fs);
	} else {
		/* clang: a caret alone under an unknown type name, a range under an undeclared identifier */
		report(c, "error", loc, &r, types ? 0 : 1, NULL, 0, tn ? "unknown type name '%.*s'"
		       : "use of undeclared identifier '%.*s'", IDF(id));
	}
	return s;
}

VPRIV bool is_typedef_name(cc *c, const ptok *t)
{
	if (t->kind != T_IDENT || kwof(t))
		return false;
	return typedef_id(c, t->id);
}

/* ---------------- nodes ---------------- */

VPRIV uint32_t mk(cc *c, int op, qty ty, uint32_t loc)
{
	uint32_t i = c->nnode++;
	node *n = &c->nodes[i];

	memset(n, 0, sizeof(*n));
	n->op = (uint8_t)op;
	n->ty = ty;
	n->loc = loc;
	return i;
}

/* the character range of n for a diagnostic (implicit conversions: their operand's) */
VPRIV pp_span rng(cc *c, uint32_t n)
{
	while (n && !c->span[n].e && N(n)->op == N_CAST && N(n)->a)
		n = N(n)->a;
	if (!n)
		return (pp_span){ 0, 0 };
	if (!c->span[n].e)
		return (pp_span){ N(n)->loc, pp_tok_end(c->p, N(n)->loc) };
	return (pp_span){ c->span[n].b, pp_tok_end(c->p, c->span[n].e) };
}

VPRIV uint32_t mk2(cc *c, int op, qty ty, uint32_t loc, uint32_t a, uint32_t b)
{
	uint32_t i = mk(c, op, ty, loc);

	c->nodes[i].a = a;
	c->nodes[i].b = b;
	return i;
}

VPRIV void vinit(vec *v) { v->v = v->inl, v->n = 0, v->cap = 16; }

VPRIV void vpush(cc *c, vec *v, uint32_t x)
{
	if (unlikely(v->n == v->cap)) {
		uint32_t *nv = halloc(c, v->cap * 2 * sizeof(uint32_t));

		memcpy(nv, v->v, v->n * sizeof(uint32_t));
		v->v = nv;
		v->cap *= 2;
	}
	v->v[v->n++] = x;
}

VPRIV uint32_t vfreeze(cc *c, const vec *v)
{
	uint32_t at = c->nlist, *d = &c->lists[at];

	/* 16-byte blocks, inline: v->v has room for n rounded up to 16 (inl[16], doubling),
	   and c->lists past nlist is free (reserved) */
	for (uint32_t i = 0; i < v->n; i += 4)
		memcpy(d + i, v->v + i, 16);
	c->nlist += v->n;
	return at;
}

VPRIV uint32_t mk_int(cc *c, i128 v, qty ty, uint32_t loc)
{
	uint32_t n = mk(c, N_NUM, ty, loc);

	c->ipool[c->nipool] = v;
	c->nodes[n].a = c->nipool++;
	return n;
}

/* stands in for an expression already diagnosed: operators on it say nothing more
   (clang's invalid expressions); an int 0 lvalue, so a constant and assignable */
VPRIV uint32_t err_node(cc *c, uint32_t loc)
{
	uint32_t n = mk_int(c, 0, prim(TY_INT), loc);

	c->nodes[n].flags |= NF_ERR | NF_LVALUE;
	return n;
}

VPRIV uint32_t mk_flt(cc *c, double v, qty ty, uint32_t loc)
{
	uint32_t n = mk(c, N_FNUM, ty, loc);

	c->fpool[c->nfpool] = v;
	c->nodes[n].a = c->nfpool++;
	if (is_q(c, ty))
		fpool_q(c, q_of_d(v));
	return n;
}

/* a 16-byte long double constant from its bits */
VPRIV uint32_t mk_fltq(cc *c, u128 q, qty ty, uint32_t loc)
{
	uint32_t n = mk(c, N_FNUM, ty, loc);

	c->fpool[c->nfpool] = q_to_d(q);
	c->nodes[n].a = c->nfpool++;
	fpool_q(c, q);
	return n;
}
