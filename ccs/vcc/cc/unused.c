/* vcc front end: -Wunused-variable, -Wunused-but-set-variable, -Wunused-function,
   -Wunused-const-variable, -Wunneeded-internal-declaration (clang's rules) */
#include "ccimpl.h"

static const char *const w_unused[] = { "unused", "all", "most", NULL };
static const char *const w_unused_var[] = { "unused-variable", "unused", "all", "most", NULL };
static const char *const w_unused_fn[] = { "unused-function", "unused", "all", "most", NULL };

/* the declaration is silenced by its attributes: unused, used, cleanup */
static bool attr_quiet(cc *c, const sym *y)
{
	const attrs *a = y->attrs ? &c->attr[y->attrs] : NULL;

	return a && (a->unused || a->used || a->cleanup);
}

/* clang: a discarded assignment, compound assignment, ++ or -- of a block-scope variable named
   directly is not a use of it (Sema's RefsMinusAssignments); volatile ones only for '=' */
VPRIV void discard(cc *c, uint32_t e)
{
	const node *n;
	sym *y;
	bool rmw;

	if (!e)
		return;
	n = N(e);
	if (n->op == N_ASSIGN)
		rmw = n->aux != 0;
	else if (n->op >= N_PREINC && n->op <= N_POSTDEC)
		rmw = true;
	else
		return;
	if (N(n->a)->op != N_VAR || (N(n->a)->flags & NF_PAREN))
		return;
	y = &c->syms[N(n->a)->a];
	if (y->kind != S_VAR || !y->depth || !y->nref || (rmw && (y->ty & Q_VOLATILE)))
		return;
	y->nref--;
}

/* at a scope's end: its variables never named, or only assigned (clang's ActOnPopScope, in
   declaration order; none after an error in the function, clang: in the scope) */
VPRIV void unused_locals(cc *c, uint32_t from, uint32_t to)
{
	if (c->errors != c->fnerr)
		return;
	for (uint32_t i = from; i < to; i++) {
		const sym *y = &c->syms[c->sstack[i]];
		bool set;
		vdiag *d;

		if (y->kind == S_TYPEDEF && !(y->flags & SF_REF) && y->name && !attr_quiet(c, y)) {
			if (!(c->nutd & (c->nutd + 1)))	/* (at 0, 1, 3, 7...: room for twice as many) */
				c->utd = realloc(c->utd, (c->nutd + 1) * 2 * sizeof(uint32_t));
			c->utd[c->nutd++] = c->sstack[i];
			continue;
		}
		if (y->kind != S_VAR || y->nref || !y->name || (y->flags & (SF_PARAM | SF_INVALID)) ||
		    y->sc == SC_EXTERN || attr_quiet(c, y))
			continue;
		set = y->flags & SF_REF;
		d = set ? cc_warn_begin(c, "unused-but-set-variable", false, w_unused, y->loc,
					"variable '%.*s' set but not used", IDF(y->name))
			: cc_warn_begin(c, "unused-variable", false, w_unused_var, y->loc,
					"unused variable '%.*s'", IDF(y->name));
		if (d) {
			if (!set)	/* (clang: the name's range, for a variable never named) */
				pp_diag_label(c->p, d, y->loc, pp_tok_end(c->p, y->loc), NULL);
			pp_diag_help(c->p, d, set ? "its value is never read: remove it with the assignments to it"
					       : "remove it, or mark it __attribute__((unused)) if it is needed");
			pp_diag_emit(c->p, d);
		}
	}
}

static bool const_type(cc *c, qty t)
{
	while (KIND(c, t) == TY_ARRAY)
		t = T(c, t)->base;
	return t & Q_CONST;
}

typedef struct ucand { uint32_t first, last; } ucand;

static int ucand_cmp(const void *a, const void *b)
{
	uint32_t x = ((const ucand *)a)->first, y = ((const ucand *)b)->first;

	return x < y ? -1 : x > y;
}

/* after the TU: static functions and variables nothing uses (clang's UnusedFileScopedDecls) */
VPRIV void unused_globals(cc *c)
{
	uint32_t maxwm = 0, nc = 0;
	ucand *cand;

	if (c->errors || pp_errors(c->p))
		return;
	if (pp_diag_wlevel(c->p, "unused-function", false, w_unused_fn, UINT32_MAX) < 0 &&
	    pp_diag_wlevel(c->p, "unneeded-internal-declaration", false, w_unused_fn, UINT32_MAX) < 0 &&
	    pp_diag_wlevel(c->p, "unused-variable", false, w_unused_var, UINT32_MAX) < 0)
		goto typedefs;	/* (-Wunused-const-variable is in -Wunused-variable) */
	/* an unparsed lazy body may name what was declared before it: say nothing of those */
	for (uint32_t i = 0; i < c->nlzf; i++)
		if (!c->lzf[i].done && c->lzf[i].wm > maxwm)
			maxwm = c->lzf[i].wm;
	cand = malloc((c->ntoplevel + 1) * sizeof(ucand));
	/* (toplevel is in sym order: from the first at maxwm) */
	uint32_t lo = 0, hi = c->ntoplevel;

	while (lo < hi) {
		uint32_t mid = lo + (hi - lo) / 2;

		if (c->toplevel[mid] < maxwm)
			lo = mid + 1;
		else
			hi = mid;
	}
	for (uint32_t i = lo; i < c->ntoplevel; i++) {
		uint32_t s = c->toplevel[i], f = s, fl = 0;
		const sym *y = &c->syms[s];

		if (s < maxwm)	/* (its first declaration is earlier still) */
			continue;
		if ((y->kind != S_FUNC && y->kind != S_VAR) || !y->name || lookup(c, y->name) != s)
			continue;	/* (the latest declaration speaks for the entity) */
		for (uint32_t k = s; k; k = c->syms[k].next) {
			fl |= c->syms[k].flags;
			f = k;
			if (attr_quiet(c, &c->syms[k]))
				fl |= SF_ATTR_USED;
		}
		if (c->syms[f].sc != SC_STATIC || (fl & (SF_USED | SF_ATTR_USED | SF_INVALID | SF_OVERLOAD)) || f < maxwm)
			continue;
		if (y->attrs) {
			const attrs *a = &c->attr[y->attrs];

			if (a->constructor || a->destructor || a->alias)
				continue;
		}
		if (y->kind == S_VAR ? !pp_in_main_file(c->p, y->loc) : (fl & SF_INLINE) && !pp_in_main_file(c->p, y->loc))
			continue;
		cand[nc++] = (ucand){ f, s };
	}
	if (nc) {
		/* named in an alias attribute: the target is used */
		for (uint32_t i = 0; i < c->ntoplevel; i++) {
			const sym *y = &c->syms[c->toplevel[i]];

			if (y->attrs && c->attr[y->attrs].alias) {
				uint32_t t = cc_global_named(c, c->strs[c->attr[y->attrs].alias - 1].s);

				for (; t; t = c->syms[t].next)
					c->syms[t].flags |= SF_ATTR_USED;
			}
		}
		qsort(cand, nc, sizeof(*cand), ucand_cmp);
	}
	for (uint32_t i = 0; i < nc; i++) {
		uint32_t at = cand[i].first, fl = 0;
		const sym *y = &c->syms[cand[i].last];
		bool ref, fn = y->kind == S_FUNC;
		vdiag *d;

		for (uint32_t k = cand[i].last; k; k = c->syms[k].next) {
			fl |= c->syms[k].flags;
			if (fn ? (c->syms[k].flags & SF_DEFINED) || at == cand[i].first : (c->syms[k].flags & SF_DEFINED))
				at = k;
		}
		if (fl & SF_ATTR_USED)
			continue;
		if (fn && !(fl & SF_DEFINED))
			at = cand[i].last;	/* clang: a function never defined, at its latest declaration */
		ref = fl & SF_REF;
		if (ref)
			d = cc_warn_begin(c, "unneeded-internal-declaration", false, w_unused_fn, c->syms[at].loc,
					  "%s '%.*s' is not needed and will not be emitted", fn ? "function" : "variable",
					  IDF(y->name));
		else if (fn)
			d = cc_warn_begin(c, "unused-function", false, w_unused_fn, c->syms[at].loc,
					  "unused function '%.*s'", IDF(y->name));
		else if (const_type(c, y->ty)) {
			static const char *const g[] = { "unused-const-variable", "unused-variable", "unused", "all", "most", NULL };

			d = cc_warn_begin(c, "unused-const-variable", false, g, c->syms[at].loc,
					  "unused variable '%.*s'", IDF(y->name));
		} else
			d = cc_warn_begin(c, "unused-variable", false, w_unused_var, c->syms[at].loc,
					  "unused variable '%.*s'", IDF(y->name));
		if (!d)
			continue;
		pp_diag_label(c->p, d, c->syms[at].loc, pp_tok_end(c->p, c->syms[at].loc), NULL);
		if (ref)
			pp_diag_help(c->p, d, "it is only named where nothing is evaluated (sizeof, typeof) or from its own body");
		else
			pp_diag_help(c->p, d, "nothing in this file uses it: remove it, or mark it __attribute__((unused)) if it is needed");
		pp_diag_emit(c->p, d);
	}
	free(cand);
typedefs:
	for (uint32_t i = 0; i < c->nutd; i++) {
		const sym *y = &c->syms[c->utd[i]];
		static const char *const g[] = { "unused-local-typedef", "unused-local-typedefs", "unused", "all", "most", NULL };
		vdiag *d = cc_warn_begin(c, "unused-local-typedef", false, g, y->loc, "unused typedef '%.*s'", IDF(y->name));

		if (d) {
			pp_diag_help(c->p, d, "nothing in its scope names it: remove it");
			pp_diag_emit(c->p, d);
		}
	}
}
