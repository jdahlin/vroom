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

/* -Wunused-variable: a block-scope variable never named (clang's
   ActOnPopScope, in declaration order) */
static void unused_vars(cc *c, uint32_t m)
{
	static const char *const g[] = { "unused", "all", "most", NULL };

	for (uint32_t i = m; i < c->nsstack; i++) {
		sym *y = &c->syms[c->sstack[i]];
		const attrs *a = y->attrs ? &c->attr[y->attrs] : NULL;
		pp_span r;

		if (y->kind != S_VAR || !y->name || (y->flags & (SF_PARAM | SF_ATTR_USED)) ||
		    y->sc == SC_EXTERN || (a && (a->unused || a->cleanup || a->used)))
			continue;
		if (y->flags & SF_REF) {	/* -Wunused-but-set-variable: only assigned */
			if (!y->nref) {
				pp_dinfo d = { "warning", y->loc, NULL, 0, NULL, 0, idstr_(y->name), (uint32_t)idlen_(y->name) };
				static const char *const gb[] = { "unused-but-set", "unused", "all", "most", NULL };

				pp_warnx(c->p, "unused-but-set-variable", false, gb, &d, "variable '%.*s' set but not used", IDF(y->name));
			}
			continue;
		}
		r = (pp_span){ y->loc, pp_tok_end(c->p, y->loc) };
		{
			pp_dinfo d = { "warning", y->loc, &r, 1, NULL, 0, idstr_(y->name), (uint32_t)idlen_(y->name) };

			pp_warnx(c->p, "unused-variable", false, g, &d, "unused variable '%.*s'", IDF(y->name));
		}
	}
}

/* -Wunused-variable, -Wunused-const-variable: a static file-scope variable
   of the main file never named, at its definition (clang's end of TU) */
VPRIV void unused_statics(cc *c)
{
	static const char *const g[] = { "unused", "all", "most", NULL };
	static const char *const gc[] = { "unused-variable", "unused", "all", "most", NULL };

	if (c->errors || pp_errors(c->p))
		return;
	for (uint32_t i = 1; i < c->nsym; i++)	/* alias("f") uses f */
		if (c->syms[i].attrs && c->attr[c->syms[i].attrs].alias) {
			uint32_t t = cc_global_named(c, c->strs[c->attr[c->syms[i].attrs].alias - 1].s);

			if (t)
				c->syms[t].flags |= SF_USED;
		}
	for (uint32_t i = 1; i < c->nsym; i++) {
		const sym *y = &c->syms[i];
		uint32_t at;
		bool ref = false, used = false, skip = false, fn = y->kind == S_FUNC;

		if ((y->kind != S_VAR && !fn) || y->depth || y->sc != SC_STATIC || !y->name || lookup(c, y->name) != i ||
		    (y->flags & SF_OVERLOAD))
			continue;
		for (uint32_t s = i; s; s = c->syms[s].next) {
			const sym *z = &c->syms[s];
			const attrs *a = z->attrs ? &c->attr[z->attrs] : NULL;

			ref |= (z->flags & SF_REF) != 0;
			used |= (z->flags & (SF_ATTR_USED | SF_USED | SF_ADDR_TAKEN)) != 0;
			skip |= a && (a->unused || a->used || a->alias || a->constructor || a->destructor);
		}
		at = y->defloc ? y->defloc : y->loc;
		if (used || skip || !pp_in_main_file(c->p, at)	/* clang: also a non-inline header function (vcc: its references from unparsed lazy bodies are unknown) */)
			continue;
		{
			qty t = y->ty;
			bool k = false;
			pp_span r = { at, pp_tok_end(c->p, at) };
			pp_dinfo d = { "warning", at, &r, 1, NULL, 0, idstr_(y->name), (uint32_t)idlen_(y->name) };

			if (!fn) {
				while (KIND(c, t) == TY_ARRAY)
					t = QT(QI(T(c, t)->base), QQ(T(c, t)->base) | QQ(t));
				k = (QQ(t) & Q_CONST) != 0;
			}
			if (ref) {	/* named only where it is not evaluated (sizeof, typeof) */
				static const char *const gu[] = { "unused-function", "unused", "all", "most", NULL };

				pp_warnx(c->p, "unneeded-internal-declaration", false, gu, &d, "%s '%.*s' is not needed and will not be emitted",
					 fn ? "function" : "variable", IDF(y->name));
			} else if (fn)
				pp_warnx(c->p, "unused-function", false, g, &d, "unused function '%.*s'", IDF(y->name));
			else
				pp_warnx(c->p, k ? "unused-const-variable" : "unused-variable", false, k ? gc : g, &d,
					 "unused variable '%.*s'", IDF(y->name));
		}
	}
}

/* -Wunused-local-typedef: a block-scope typedef never named (clang: at the end of the TU) */
VPRIV void unused_typedefs(cc *c)
{
	static const char *const g[] = { "unused-local-typedefs", "unused", "all", "most", NULL };

	if (c->errors || pp_errors(c->p))
		return;
	for (uint32_t i = 1; i < c->nsym; i++) {
		const sym *y = &c->syms[i];
		const attrs *a = y->attrs ? &c->attr[y->attrs] : NULL;

		if (y->kind != S_TYPEDEF || !y->depth || !y->name || (y->flags & SF_REF) || (a && (a->unused || a->used)))
			continue;
		{
			pp_dinfo d = { "warning", y->loc, NULL, 0, NULL, 0, idstr_(y->name), (uint32_t)idlen_(y->name) };

			pp_warnx(c->p, "unused-local-typedef", false, g, &d, "unused typedef '%.*s'", IDF(y->name));
		}
	}
}

VPRIV void scope_close(cc *c)
{
	uint32_t m = c->smark[c->depth];

	if (c->depth > 0 && c->nsstack > m && !c->errors && !pp_errors(c->p))	/* (clang: none after an error) */
		unused_vars(c, m);

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

/* clang's typo correction: the visible symbol closest to id (edit distance at most
   (len + 2) / 3), of a type name (types) or of anything else; 0 if none */
static uint32_t typo_sym(cc *c, uint32_t id, bool types)
{
	int n, max, best;
	const char *s = idname(id, &n);
	uint32_t pick = 0;

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
	return pick;
}

/* "<msg> 'id'; did you mean 'x'?" with the fix-it and the "declared here" note,
   or just "<msg> 'id'"; returns the correction */
VPRIV uint32_t undeclared(cc *c, const char *msg, uint32_t id, uint32_t loc, bool types)
{
	/* clang does not correct a name from a macro expansion (and says where it was spelled) */
	bool mac = (tok(c)->loc == loc ? tok(c)->flags : c->prevflags) & PF_MACRO;
	uint32_t s = mac ? 0 : typo_sym(c, id, types);
	pp_span r = { loc, pp_tok_end(c->p, loc) };

	if (mac) {
		int m;
		const char *t = idname(id, &m);
		pp_dinfo d = { "error", loc, &r, 1, NULL, 0, t, (uint32_t)m };

		error_limit(c), c->errors++, c->ndiag++;
		pp_diagx(c->p, &d, "%s '%.*s'", msg, IDF(id));
		return 0;
	}

	if (s) {
		int m;
		const char *t = idname(c->syms[s].name, &m);
		char *fs = halloc(c, (size_t)m + 1);
		pp_fixit fx;

		memcpy(fs, t, m), fs[m] = 0;
		fx = (pp_fixit){ r.b, r.e, fs };
		report(c, "error", loc, &r, 1, &fx, 1, "%s '%.*s'; did you mean '%s'?", msg, IDF(id), fs);
		note(c, c->syms[s].loc, "'%s' declared here", fs);
	} else {
		report(c, "error", loc, &r, 1, NULL, 0, "%s '%.*s'", msg, IDF(id));
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
