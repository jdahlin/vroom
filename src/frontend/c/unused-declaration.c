/* vcc front end: warnings for variables, statics and typedefs that were never used */
#include "ccimpl.h"

/* errors other than clang's DefaultError warnings (-Wint-conversion...): clang's
   hasUncompilableErrorOccurred */
static uint32_t hard_errors(cc *c)
{
	uint32_t ce = (uint32_t)c->errors, pe = (uint32_t)pp_errors(c->p), e = ce > pe ? ce : pe;

	return e > c->nconv ? e - c->nconv : 0;
}

/* -Wunused-variable: a block-scope variable never named (clang's
   ActOnPopScope, in declaration order) */
VPRIV void unused_vars(cc *c, uint32_t m)
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

	if (hard_errors(c))
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
