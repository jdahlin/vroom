/* C++ declarations: deduction guides and class template argument deduction */
#include "declaration.h"

/* a guide's type parameter named by token i: its index in the template scope around (-1: not one) */
VPRIV int cxx_guide_tparam(cxx *c, uint32_t i)
{
	cxx_scope *ts = c->scope;

	if (!ts || ts->kind != CXX_SCOPE_TEMPLATE || c->tok[i].kind != LEX_TOKEN_IDENTIFIER || c->tok[i].kw)
		return -1;
	for (uint32_t k = 0; k < ts->ntparams && k < 8; k++)
		if (ts->tparams[k].kind == 1 && !ts->tparams[k].pack && ts->tparams[k].name == c->tok[i].id)
			return (int)k;
	return -1;
}

/* the guide at token i (its template's name) if it has the simple form cxx_guide describes: kept on its template */
VPRIV void cxx_guide_record(cxx *c, cxx_entity *te, uint32_t i)
{
	cxx_guide g = { 0 };
	uint32_t p = i + 2;	/* (past the name and its '(') */

	if (c->tok[p].kind != LEX_TOKEN_RIGHT_PAREN)
		for (;;) {
			int k, mode = 0;

			if (g.np == 8)
				return;
			if (c->tok[p].kw == CXX_KW_const)
				p++, mode = 1;
			if ((k = cxx_guide_tparam(c, p)) < 0)
				return;
			p++;
			if (c->tok[p].kind == LEX_TOKEN_AMPERSAND && mode == 1)
				p++;
			else if (c->tok[p].kind == LEX_TOKEN_AMPERSAND_AMPERSAND && mode == 0)
				p++, mode = 2;
			else if (mode == 1)
				return;	/* (const T by value: decays as T does) */
			if (c->tok[p].kind == LEX_TOKEN_IDENTIFIER && !c->tok[p].kw)
				p++;	/* (a parameter name) */
			g.pidx[g.np] = (uint8_t)k, g.mode[g.np++] = (uint8_t)mode;
			if (c->tok[p].kind == LEX_TOKEN_COMMA) {
				p++;
				continue;
			}
			if (c->tok[p].kind != LEX_TOKEN_RIGHT_PAREN)
				return;
			break;
		}
	p++;
	if (c->tok[p].kind != LEX_TOKEN_ARROW || c->tok[p + 1].kind != LEX_TOKEN_IDENTIFIER || c->tok[p + 1].id != c->tok[i].id ||
	    c->tok[p + 2].kind != LEX_TOKEN_LESS)
		return;
	for (p += 3;; p++) {
		int k = cxx_guide_tparam(c, p);

		if (k < 0 || g.nres == 8)
			return;
		g.res[g.nres++] = (uint8_t)k;
		p++;
		if (c->tok[p].kind == LEX_TOKEN_GREATER)
			break;
		if (c->tok[p].kind != LEX_TOKEN_COMMA)
			return;
	}
	if (c->tok[p + 1].kind != LEX_TOKEN_SEMICOLON)
		return;
	for (uint32_t r = 0; r < g.nres; r++) {	/* (each result argument deduced from a parameter) */
		bool from = false;

		for (uint32_t q = 0; q < g.np; q++)
			from |= g.pidx[q] == g.res[r];
		if (!from)
			return;
	}
	te->guides = realloc(te->guides, (te->nguides + 1) * sizeof *te->guides);
	te->guides[te->nguides++] = g;
}

/*
 * Name{args} or Name(args) of a class template without its arguments (C++17's class template argument
 * deduction): the class its deduction guides of the simple form give (pair{a, b}: pair<A, B>); CXX_TY_UNKNOWN
 * if none applies (the implicit guides from its constructors: not built).
 */
VPRIV qty cxx_ctad(cxx *c, cxx_entity *te, const qty *args, const uint8_t *lval, uint32_t nargs)
{
	for (uint32_t gi = 0; te && gi < te->nguides; gi++) {
		const cxx_guide *g = &te->guides[gi];
		qty dt[8];
		cxx_targ ta[8];
		bool ok = g->np == nargs;
		cxx_entity *inst;

		for (int k = 0; k < 8; k++)
			dt[k] = CXX_TY_UNKNOWN;
		for (uint32_t j = 0; ok && j < nargs; j++) {
			qty at = args[j], d;

			if (at == CXX_TY_UNKNOWN) {
				ok = false;
				break;
			}
			if (g->mode[j] == 2 && lval && lval[j])
				d = cxx_type_reference(c, at, false);	/* (T&& from an lvalue: T is a reference) */
			else if (g->mode[j] == 0 && AST_TYPE_KIND(c->ast, at) == TY_ARRAY)
				d = cc_type_pointer(c->ast, AST_TYPE(c->ast, at)->base);
			else if (g->mode[j] == 0 && AST_TYPE_KIND(c->ast, at) == TY_FUNC)
				d = cc_type_pointer(c->ast, at);
			else
				d = QUNQ(at);
			if (dt[g->pidx[j]] != CXX_TY_UNKNOWN && dt[g->pidx[j]] != d)
				ok = false;
			dt[g->pidx[j]] = d;
		}
		if (!ok)
			continue;
		memset(ta, 0, sizeof ta);
		for (uint32_t r = 0; r < g->nres; r++)
			ta[r].kind = 1, ta[r].t = dt[g->res[r]];
		inst = cxx_instantiate(c, te, ta, g->nres, true);
		if (inst && inst->ty != CXX_TY_UNKNOWN)
			return inst->ty;
	}
	return CXX_TY_UNKNOWN;
}

/* a deduction guide: [explicit] template-name ( parameters ) -> template-id ; */
VPRIV bool cxx_deduction_guide_try(cxx *c)
{
	uint32_t i = c->pos, depth = 0;
	cxx_entity *e;

	if (c->tok[i].kw == CXX_KW_explicit) {
		i++;
		if (c->tok[i].kind == LEX_TOKEN_LEFT_PAREN)
			return false;	/* (explicit(bool) guides: not in json's headers) */
	}
	if (c->tok[i].kind != LEX_TOKEN_IDENTIFIER || c->tok[i].kw || c->tok[i + 1].kind != LEX_TOKEN_LEFT_PAREN)
		return false;
	e = cxx_lookup(c, c->tok[i].id);
	e = e ? cxx_entity_prefer(e, CXX_ENTITY_TEMPLATE) : NULL;
	if (!e || e->kind != CXX_ENTITY_TEMPLATE)
		return false;
	for (uint32_t k = i + 1; k < c->ntok; k++) {
		int kind = c->tok[k].kind;

		if (kind == LEX_TOKEN_LEFT_PAREN)
			depth++;
		else if (kind == LEX_TOKEN_RIGHT_PAREN && !--depth) {
			if (c->tok[k + 1].kind != LEX_TOKEN_ARROW)
				return false;
			break;
		}
	}
	cxx_guide_record(c, e, i);
	cxx_accept_kw(c, CXX_KW_explicit);
	cxx_advance(c);
	{
		bool q, f;

		/* the parameters (a declarator with only a parameter suffix), -> and the type */
		cxx_parameter_clause_parse(c);
		(void)q, (void)f;
	}
	cxx_expect(c, LEX_TOKEN_ARROW);
	cxx_type_id_parse(c);
	cxx_expect(c, LEX_TOKEN_SEMICOLON);
	return true;
}
