/* C++ templates: concepts, variable templates, constexpr calls */
#include "instantiate.h"

/*
 * Concept<args>: its constraint-expression with the arguments bound, as a constant: 1 satisfied,
 * 0 not, -1 not known (an argument or an atomic constraint vcc cannot evaluate).
 */
VPRIV int cxx_concept_value(cxx *c, cxx_entity *e, const cxx_targ *args, uint32_t n)
{
	cxx_ftemplate *f = e ? e->ftmpl : NULL;
	cxx_scope *ps, *binding;
	cxx_outer o;
	jmp_buf jb;
	volatile int v = -1;
	uint32_t at;

	cxx_targ acopy[32];

	if (!f || n > 32 || c->body_depth >= 64 || c->tentative)
		return -1;
	memcpy(acopy, args, n * sizeof *args);	/* (often c->targs, which the parse below reuses) */
	args = acopy;
	ps = f->p.params;
	for (uint32_t i = 0; i < n; i++)
		if (!args[i].kind || args[i].kind == 3 || args[i].kind == 5 || (args[i].kind == 1 && cxx_mentions_placeholder(c, args[i].t, 0)))
			return -1;
	for (uint32_t i = 0; i < f->ndone; i++)
		if (f->done[i].n == n && cxx_args_equal(f->done[i].args, args, n))
			return f->done[i].viable;
	uint32_t given = n;	/* (the cache's key: the arguments as written) */

	binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, f->p.home, NULL);
	for (uint32_t i = 0; i < ps->ntparams; i++) {
		const cxx_tparam *tp = &ps->tparams[i];

		if (tp->pack) {
			if (i + 1 != ps->ntparams || i > n)
				return -1;
			cxx_bind_pack(c, binding, tp, args + i, n - i);
			break;
		}
		if (i >= n && tp->def_b && n < 32) {
			/* a default argument (three_way_comparable<T> is <T, partial_ordering>), from the ones before it */
			memset(&acopy[n], 0, sizeof acopy[n]);
			if (!cxx_default_argument(c, tp, binding, &acopy[n]) || !acopy[n].kind || (acopy[n].kind == 1) != (tp->kind == 1))
				return -1;
			n++;
		}
		if (i >= n || (args[i].kind == 1) != (tp->kind == 1))
			return -1;
		cxx_bind(c, binding, tp, &args[i]);
	}
	if (f->ndone == f->cdone)
		f->done = realloc(f->done, (f->cdone = f->cdone ? f->cdone * 2 : 4) * sizeof *f->done);
	at = f->ndone++;
	memset(&f->done[at], 0, sizeof f->done[at]);
	f->done[at].args = malloc((given ? given : 1) * sizeof *args);
	memcpy(f->done[at].args, args, given * sizeof *args);
	f->done[at].n = given, f->done[at].viable = -1;	/* (a concept that names itself: not known) */
	cxx_outer_save(c, &o);
	c->instantiating++, c->body_depth++, c->unevaluated++;
	c->fail = &jb;
	c->pos = f->pos;
	c->scope = binding;
	c->template_depth = 0, c->in_template_args = 0, c->explicit_specialization = 0, c->class_depth = 0, c->implicit_typename = 0;
	if (!setjmp(jb)) {
		cxx_conditional_expression_parse(c);
		if (c->econst)
			v = c->evalue != 0;
	}
	c->instantiating--, c->body_depth--;
	cxx_outer_restore(c, &o);
	f->done[at].viable = (int8_t)v;
	return v;
}

/* ---- variable templates -------------------------------------------------------------------- */

VPRIV void cxx_vtemplate_special(cxx *c, cxx_ftemplate *f, cxx_scope *params, cxx_scope *home, uint32_t less_pos, uint32_t init_pos)
{
	cxx_vspec *sp = calloc(1, sizeof *sp);

	/* (an explicit specialization: no parameters) */
	sp->p.params = params ? params : cxx_scope_new(c, CXX_SCOPE_TEMPLATE, home, NULL);
	sp->p.home = home, sp->p.less_pos = less_pos, sp->init_pos = init_pos;
	f->vspecs = realloc(f->vspecs, (f->nvspecs + 1) * sizeof *f->vspecs);
	f->vspecs[f->nvspecs++] = sp;
}

/*
 * name<args> for a variable template: the initializer of the specialization the arguments select (the
 * most specialized partial or explicit one that matches, else the primary), as a constant.  false: not
 * known.
 */
VPRIV bool cxx_vtemplate_value(cxx *c, cxx_entity *e, const cxx_targ *args, uint32_t n, int64_t *out)
{
	cxx_ftemplate *f = e ? e->ftmpl : NULL;
	cxx_scope *ps, *binding;
	cxx_vspec *pick = NULL;
	cxx_targ bind[64], tmp[64];
	cxx_outer o;
	jmp_buf jb;
	volatile bool known = false;
	volatile int64_t v = 0;
	uint32_t at, pos;

	cxx_targ acopy[32];

	if (!f || n > 32 || c->body_depth >= 64 || c->tentative)
		return false;
	memcpy(acopy, args, n * sizeof *args);	/* (often c->targs, which the parses below reuse) */
	args = acopy;
	ps = f->p.params;
	for (uint32_t i = 0; i < n; i++)
		if (!args[i].kind || args[i].kind == 3 || args[i].kind == 5 || (args[i].kind == 1 && cxx_mentions_placeholder(c, args[i].t, 0)))
			return false;
	for (uint32_t i = 0; i < f->ndone; i++)
		if (f->done[i].n == n && cxx_args_equal(f->done[i].args, args, n)) {
			*out = f->done[i].value;
			return f->done[i].viable == 1;
		}
	/* the complete argument list: default arguments for the rest (template<class T, class = void>) */
	if (n < ps->ntparams && !(ps->ntparams && ps->tparams[ps->ntparams - 1].pack)) {
		static __thread cxx_targ full[32];
		cxx_scope *b = cxx_scope_new(c, CXX_SCOPE_BLOCK, f->p.home, NULL);

		if (ps->ntparams > 32)
			return false;
		memcpy(full, args, n * sizeof *args);
		for (uint32_t i = 0; i < ps->ntparams; i++) {
			if (i >= n && (!ps->tparams[i].def_b || !cxx_default_argument(c, &ps->tparams[i], b, &full[i])))
				return false;
			cxx_bind(c, b, &ps->tparams[i], &full[i]);
		}
		memcpy(acopy, full, ps->ntparams * sizeof *full);
		n = ps->ntparams;
		for (uint32_t i = 0; i < f->ndone; i++)
			if (f->done[i].n == n && cxx_args_equal(f->done[i].args, args, n)) {
				*out = f->done[i].value;
				return f->done[i].viable == 1;
			}
	}
	/* the specialization: every pattern must be one vcc can match */
	for (uint32_t i = 0; i < f->nvspecs; i++) {
		cxx_vspec *sp = f->vspecs[i];

		cxx_partial_evaluate(c, &sp->p);
		if (!sp->p.ok || sp->p.params->ntparams > 64 || !cxx_partial_complete(c, &sp->p, ps, NULL, n))
			return false;
		{
			int m = cxx_partial_matches(c, &sp->p, args, n, tmp);

			if (m < 0)
				return false;
			if (!m)
				continue;
		}
		if (pick && !(cxx_partial_at_least_as_specialized(c, &sp->p, &pick->p) && !cxx_partial_at_least_as_specialized(c, &pick->p, &sp->p))) {
			if (!(cxx_partial_at_least_as_specialized(c, &pick->p, &sp->p) && !cxx_partial_at_least_as_specialized(c, &sp->p, &pick->p)))
				return false;	/* (neither is more specialized: not decided here) */
			continue;
		}
		pick = sp;
		memcpy(bind, tmp, sp->p.params->ntparams * sizeof *bind);
	}
	if (pick) {
		binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, pick->p.home, NULL);
		for (uint32_t i = 0; i < pick->p.params->ntparams; i++)
			if (bind[i].kind == 6)
				cxx_bind_pack(c, binding, &pick->p.params->tparams[i], bind[i].list + bind[i].t, (uint32_t)bind[i].v);
			else
				cxx_bind(c, binding, &pick->p.params->tparams[i], &bind[i]);
		pos = pick->init_pos;
	} else {
		binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, f->p.home, NULL);
		for (uint32_t i = 0; i < ps->ntparams; i++) {
			const cxx_tparam *tp = &ps->tparams[i];

			if (tp->pack) {
				if (i + 1 != ps->ntparams || i > n)
					return false;
				cxx_bind_pack(c, binding, tp, args + i, n - i);
				break;
			}
			if (i >= n || (args[i].kind == 1) != (tp->kind == 1))
				return false;	/* (default arguments: not yet) */
			cxx_bind(c, binding, tp, &args[i]);
		}
		pos = f->pos;
	}
	if (f->ndone == f->cdone)
		f->done = realloc(f->done, (f->cdone = f->cdone ? f->cdone * 2 : 4) * sizeof *f->done);
	at = f->ndone++;
	memset(&f->done[at], 0, sizeof f->done[at]);
	f->done[at].args = malloc((n ? n : 1) * sizeof *args);
	memcpy(f->done[at].args, args, n * sizeof *args);
	f->done[at].n = n, f->done[at].viable = -1;
	cxx_outer_save(c, &o);
	c->instantiating++, c->body_depth++, c->unevaluated++;
	c->fail = &jb;
	c->pos = pos;
	c->scope = binding;
	c->template_depth = 0, c->in_template_args = 0, c->explicit_specialization = 0, c->class_depth = 0, c->implicit_typename = 0;
	if (!setjmp(jb)) {
		cxx_conditional_expression_parse(c);
		if (c->econst)
			known = true, v = c->evalue;
	}
	c->instantiating--, c->body_depth--;
	cxx_outer_restore(c, &o);
	f->done[at].viable = known ? 1 : -1, f->done[at].value = v;
	*out = v;
	return known;
}

/*
 * f<args>() of a constexpr function template, every template argument given and no function parameter:
 * its value, when its body is declarations of aliases (using X = ...; typedef ...; static_assert) and
 * then return expr; with expr a constant.  The template's own tokens are parsed again with the arguments
 * bound, as a variable template's initializer is.  False: not known.
 */
VPRIV bool cxx_ftemplate_const_call(cxx *c, cxx_entity *e, const cxx_targ *args, uint32_t n, int64_t *out)
{
	cxx_ftemplate *f = e ? e->ftmpl : NULL;
	cxx_scope *ps, *binding;
	cxx_targ acopy[32];
	cxx_outer o;
	jmp_buf jb;
	volatile bool known = false;
	volatile int64_t v = 0;
	uint32_t at, pos, depth = 0;
	bool is_constexpr = false;

	if (!f || !f->defined || n > 32 || c->body_depth >= 64 || c->tentative || f->ctor_class)
		return false;
	ps = f->p.params;
	if (n != ps->ntparams)
		return false;	/* (deduced or defaulted arguments: not yet) */
	memcpy(acopy, args, n * sizeof *args);
	args = acopy;
	for (uint32_t i = 0; i < n; i++)
		if (!args[i].kind || args[i].kind == 3 || args[i].kind == 5 || ps->tparams[i].pack ||
		    (args[i].kind == 1) != (ps->tparams[i].kind == 1) || (args[i].kind == 1 && cxx_mentions_placeholder(c, args[i].t, 0)))
			return false;
	for (uint32_t i = 0; i < f->nconsts; i++)
		if (f->consts[i].n == n && cxx_args_equal(f->consts[i].args, args, n)) {
			*out = f->consts[i].value;
			return f->consts[i].viable == 1;
		}
	/* its body: after the declarator's (), the first { outside parentheses */
	for (pos = f->pos; pos < c->ntok; pos++) {
		int k = c->tok[pos].kind;

		if (c->tok[pos].kind == LEX_TOKEN_IDENTIFIER && c->tok[pos].len == 9 && !memcmp(c->tok[pos].s, "constexpr", 9))
			is_constexpr = true;
		if (k == LEX_TOKEN_LEFT_PAREN || k == LEX_TOKEN_LEFT_SQUARE)
			depth++;
		else if ((k == LEX_TOKEN_RIGHT_PAREN || k == LEX_TOKEN_RIGHT_SQUARE) && depth)
			depth--;
		else if (!depth && (k == LEX_TOKEN_LEFT_BRACE || k == LEX_TOKEN_SEMICOLON || k == LEX_TOKEN_EQUAL))
			break;
	}
	if (!is_constexpr || pos >= c->ntok || c->tok[pos].kind != LEX_TOKEN_LEFT_BRACE)
		return false;
	binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, f->p.home, NULL);
	for (uint32_t i = 0; i < n; i++)
		cxx_bind(c, binding, &ps->tparams[i], &args[i]);
	cxx_outer_save(c, &o);
	c->instantiating++, c->body_depth++, c->unevaluated++;
	c->fail = &jb;
	c->pos = pos + 1;
	c->scope = cxx_scope_new(c, CXX_SCOPE_BLOCK, binding, NULL);
	c->template_depth = 0, c->in_template_args = 0, c->explicit_specialization = 0, c->class_depth = 0, c->implicit_typename = 0;
	if (!setjmp(jb)) {
		for (int guard = 0; guard < 64; guard++) {
			if (cxx_is_kw(c, CXX_KW_using) || cxx_is_kw(c, CXX_KW_typedef) || cxx_is_kw(c, CXX_KW_static_assert)) {
				cxx_declaration_parse(c, CXX_CONTEXT_BLOCK);
				continue;
			}
			if (cxx_is_kw(c, CXX_KW_return)) {
				cxx_advance(c);
				cxx_expression_parse(c);
				if (c->econst && cxx_is(c, LEX_TOKEN_SEMICOLON) && cxx_peek(c, 1)->kind == LEX_TOKEN_RIGHT_BRACE)
					known = true, v = c->evalue;
			}
			break;
		}
	}
	c->instantiating--, c->body_depth--;
	cxx_outer_restore(c, &o);
	if (f->nconsts == f->cconsts)
		f->consts = realloc(f->consts, (f->cconsts = f->cconsts ? f->cconsts * 2 : 4) * sizeof *f->consts);
	at = f->nconsts++;
	memset(&f->consts[at], 0, sizeof f->consts[at]);
	f->consts[at].args = malloc((n ? n : 1) * sizeof *args);
	memcpy(f->consts[at].args, args, n * sizeof *args);
	f->consts[at].n = n, f->consts[at].viable = known ? 1 : -1, f->consts[at].value = v;
	*out = v;
	return known;
}

/*
 * How generic a function template is for a call of nargs arguments: the parameters that are a bare
 * template parameter (T, const T&, T&&), which match anything.  Between two templates that fit equally,
 * the one with fewer is the more specialized (f(T*) over f(T&&)): a stand-in for partial ordering.
 * -1: not known.
 */
VPRIV int cxx_ftemplate_bare_params(cxx *c, cxx_entity *e, uint32_t nargs)
{
	cxx_ftemplate *f = e->ftmpl;
	int n = 0;

	if (!f || !f->has_pt)
		return -1;
	for (uint32_t i = 0; i < nargs && i < f->np; i++) {
		qty P = f->pt[i];
		void *owner;

		if (P == CXX_TY_UNKNOWN)
			return -1;
		if (AST_TYPE_KIND(c->ast, P) == TY_PTR && AST_TYPE(c->ast, P)->ext)
			P = AST_TYPE(c->ast, P)->base;
		/* (a bare T counts 2, const T 1: f(const T&) is more specialized than f(T&); so for T* and
		   const T*, counted apart from bare T's: f(T) is less specialized than f(T*)) */
		if (cxx_placeholder_of(c, P, &owner) >= 0)
			n += (QQ(P) & Q_CONST) ? 100 : 200;
		else if (AST_TYPE_KIND(c->ast, P) == TY_PTR && !AST_TYPE(c->ast, P)->ext &&
			 cxx_placeholder_of(c, AST_TYPE(c->ast, P)->base, &owner) >= 0)
			n += (QQ(AST_TYPE(c->ast, P)->base) & Q_CONST) ? 1 : 2;
	}
	return n;
}

VPRIV void cxx_ftemplate_defined(cxx_ftemplate *f)
{
	if (f)
		f->defined = 1;
}

/*
 * template<class K> R X::f(const K& v) { ... } for a member template a class X (not a class template)
 * declared: the same template ([temp.over.link]/6).  The declaration takes the definition's record, so
 * that the calls deduced through it instantiate this body.  The same function: the name, the number of
 * template and function parameters, const, the shape of the parameters; vcc does not compare them token
 * for token, so only when one declaration is like that (two such overloads told apart by enable_if: neither).
 * A call before the definition has made its instance from the declaration already: not redone.
 */
VPRIV void cxx_ftemplate_join_declarations(cxx *c, cxx_ftemplate *f, cxx_entity *set, uint32_t nparams, bool is_const)
{
	cxx_entity def = { .kind = CXX_ENTITY_FUNCTION_TEMPLATE, .ftmpl = f }, *one = NULL;
	int bare;
	uint32_t n = 0;

	if (!f || !f->p.params)
		return;
	bare = cxx_ftemplate_bare_params(c, &def, nparams);
	for (cxx_entity *x = set; x; x = x->next) {
		cxx_ftemplate *g = x->ftmpl;

		if (x->kind != CXX_ENTITY_FUNCTION_TEMPLATE || !g || g == f || x->fn_const != is_const || !g->p.params ||
		    g->p.params->ntparams != f->p.params->ntparams || cxx_ftemplate_np(c, g) != nparams || (bare >= 0 && cxx_ftemplate_bare_params(c, x, nparams) != bare))
			continue;
		one = x, n++;
	}
	if (n == 1 && !one->ftmpl->defined)
		one->ftmpl = f;
}

/* has the function template a requires-clause (in its template head) */
VPRIV bool cxx_ftemplate_constrained(const cxx_entity *e)
{
	return e->ftmpl && e->ftmpl->req_pos;
}

VPRIV bool cxx_ftemplate_has_body(const cxx_entity *e)
{
	return e->ftmpl && e->ftmpl->defined;
}

VPRIV uint32_t cxx_ftemplate_pos(const cxx_entity *e)
{
	return e->ftmpl ? e->ftmpl->pos : 0;
}
