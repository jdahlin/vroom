/* C++ templates: members of class template instances defined outside their class */
#include "instantiate.h"

VPRIV void cxx_template_member_definition(cxx *c, cxx_entity *te, uint32_t name, cxx_scope *params, cxx_scope *home, uint32_t pos,
					    uint32_t nparams, bool is_const, bool partial)
{
	cxx_template *t = cxx_template_of(te);

	t->oocs = realloc(t->oocs, (t->noocs + 1) * sizeof *t->oocs);
	t->oocs[t->noocs++] = (struct cxx_ooc){ .name = name, .pos = pos, .nparams = nparams, .params = params, .home = home,
						.is_const = is_const, .partial = partial, .pack = c->decl_pack_param < nparams };
}

/* template<...> template<...> R X<...>::f(...) { }: a member template defined outside its class template */
VPRIV void cxx_template_member_template_definition(cxx *c, cxx_entity *te, uint32_t name, cxx_scope *outer, cxx_scope *inner,
						     cxx_scope *home, uint32_t pos, uint32_t nparams, bool is_const)
{
	cxx_template *t = cxx_template_of(te);

	(void)c;
	t->oocs = realloc(t->oocs, (t->noocs + 1) * sizeof *t->oocs);
	t->oocs[t->noocs++] = (struct cxx_ooc){ .name = name, .pos = pos, .nparams = nparams, .params = outer, .home = home,
						.inner = inner, .is_const = is_const };
}

/* do the reference kinds of the parameters of the out-of-class definition at pos (& 1, && 2, none 0, as
   written) agree with e's declared parameters */
VPRIV bool cxx_ooc_refs_agree(cxx *c, uint32_t pos, const cxx_entity *e)
{
	const ctype *ft = AST_TYPE(c->ast, e->ty);
	uint32_t i = pos, depth = 0, k = 0;
	int ref = 0;

	/* to the parameter list: the ( after X<...>::name at depth 0 (operator(): its own () skipped) */
	for (; i < c->ntok && c->tok[i].kind != LEX_TOKEN_LEFT_BRACE && c->tok[i].kind != LEX_TOKEN_SEMICOLON; i++) {
		int kd = c->tok[i].kind;

		if (kd == LEX_TOKEN_LESS)
			depth++;
		else if (kd == LEX_TOKEN_GREATER && depth)
			depth--;
		else if (kd == LEX_TOKEN_LEFT_PAREN && !depth && i > pos + 1) {
			if (c->tok[i - 1].kw == CXX_KW_operator && c->tok[i + 1].kind == LEX_TOKEN_RIGHT_PAREN) {
				i++;
				continue;
			}
			if (c->tok[i - 1].kind == LEX_TOKEN_IDENTIFIER || c->tok[i - 1].kind == LEX_TOKEN_RIGHT_PAREN)
				break;
		}
	}
	if (i >= c->ntok || c->tok[i].kind != LEX_TOKEN_LEFT_PAREN)
		return false;
	depth = 0;
	for (i++; i < c->ntok; i++) {
		int kd = c->tok[i].kind;

		if (kd == LEX_TOKEN_LEFT_PAREN || kd == LEX_TOKEN_LESS || kd == LEX_TOKEN_LEFT_SQUARE)
			depth++;
		else if ((kd == LEX_TOKEN_RIGHT_PAREN || kd == LEX_TOKEN_GREATER || kd == LEX_TOKEN_RIGHT_SQUARE) && depth)
			depth--;
		else if (!depth && (kd == LEX_TOKEN_COMMA || kd == LEX_TOKEN_RIGHT_PAREN)) {
			if (k < ft->n) {
				qty pt = c->ast->params[ft->aux + k];
				int want = AST_TYPE_KIND(c->ast, pt) == TY_PTR ? AST_TYPE(c->ast, pt)->ext : 0;

				if (want != ref)
					return false;
			}
			k++, ref = 0;
			if (kd == LEX_TOKEN_RIGHT_PAREN)
				break;
		} else if (!depth && kd == LEX_TOKEN_AMPERSAND)
			ref = 1;
		else if (!depth && kd == LEX_TOKEN_AMPERSAND_AMPERSAND)
			ref = 2;
	}
	return true;
}

/*
 * A call uses e, a member function of a class template's instance declared without a body: its
 * definition outside the class (matched by name, parameter count and const), parsed as an ordinary
 * out-of-class member definition with the definition's template parameters bound to the instance's
 * arguments.
 */
VPRIV void cxx_member_definition_use(cxx *c, cxx_entity *e)
{
	cxx_scope *cs = e->member_of;
	cxx_instance *x = cs && cs->kind == CXX_SCOPE_CLASS && cs->owner ? cs->owner->instance : NULL;
	cxx_template *t = x ? x->t : NULL;
	const struct cxx_ooc *d = NULL;
	uint32_t nparams, matches = 0;
	cxx_scope *binding;
	cxx_outer o;
	jmp_buf jb;

	if (!t || !t->noocs || e->ty == CXX_TY_UNKNOWN || AST_TYPE_KIND(c->ast, e->ty) != TY_FUNC)
		return;
	const cxx_targ *bargs = x->args;
	uint32_t nbargs = x->nargs;

	if (x->pos) {
		/* an instance of a partial specialization: the definitions of its members (template<class A> void
		   V<bool, A>::f), the partial's parameters bound to what matching it deduced (vector<bool>) */
		if (!x->pargs)
			return;
		bargs = x->pargs, nbargs = x->npargs;
	}
	nparams = AST_TYPE(c->ast, e->ty)->n;
	for (int pass = 0; pass < 2 && matches != 1; pass++) {
		/* (pass 1, several with that many parameters: each parameter's reference kind as written must agree,
		   function(const function&) against function(function&&)) */
		matches = 0, d = NULL;
		for (uint32_t i = 0; i < t->noocs; i++)
			if (t->oocs[i].name == e->name && t->oocs[i].is_const == e->fn_const && !t->oocs[i].inner &&
			    (t->oocs[i].pack ? nparams + 1 >= t->oocs[i].nparams : t->oocs[i].nparams == nparams) &&
			    (x->pos ? t->oocs[i].partial && t->oocs[i].params->ntparams == nbargs : !t->oocs[i].partial) &&
			    (!pass || cxx_ooc_refs_agree(c, t->oocs[i].pos, e)))
				d = &t->oocs[i], matches++;
		if (!matches)
			break;
	}
	if (!matches)
		return;	/* (declared only: defined in another TU, or = default) */
	e->late_done = 1;
	if (matches > 1) {
		c->stats.ooc_ambiguous++;	/* (overloads with the same parameter count: their types would tell) */
		return;
	}
	/* the definition's own template parameters are the class template's, in order */
	binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, d->home, NULL);
	for (uint32_t i = 0; i < d->params->ntparams; i++) {
		const cxx_tparam *tp = &d->params->tparams[i];

		if (tp->pack) {
			if (i + 1 != d->params->ntparams || i > nbargs)
				return;
			if (x->pos && i < nbargs && bargs[i].kind == 6)	/* (a partial's deduced pack: its elements) */
				cxx_bind_pack(c, binding, tp, bargs[i].list + bargs[i].t, (uint32_t)bargs[i].v);
			else
				cxx_bind_pack(c, binding, tp, bargs + i, nbargs - i);
			break;
		}
		if (i >= nbargs)
			return;
		cxx_bind(c, binding, tp, &bargs[i]);
	}
	cxx_outer_save(c, &o);
	c->instantiating++, c->body_depth++;
	if (c->body_depth > c->stats.body_depth_max)
		c->stats.body_depth_max = c->body_depth;
	c->unevaluated = 0;
	c->fail = &jb;
	c->pos = d->pos;
	c->scope = binding;
	c->template_depth = 0, c->in_template_args = 0, c->explicit_specialization = 0, c->class_depth = 0, c->implicit_typename = 0;
	c->this_type = cs->owner->ty ? cs->owner->ty : CXX_TY_UNKNOWN, c->this_const = e->fn_const;
	c->ooc_target = e;	/* (its code: this member's, cxx_init_declarators_parse) */
	e->fn_inline = 1;	/* (an instance's member: defined in every TU that uses it, emitted weak) */
	if (!setjmp(jb)) {
		cxx_declaration_parse(c, CXX_CONTEXT_NAMESPACE);
		c->stats.ooc_bodies++;
	} else
		c->stats.ooc_failed++;
	c->ooc_target = NULL;
	c->instantiating--, c->body_depth--;
	cxx_outer_restore(c, &o);
}

/* a call uses e: an instance's member function, its body parsed now (once) */
VPRIV void cxx_member_body_use(cxx *c, cxx_entity *e)
{
	cxx_outer o;
	jmp_buf jb;

	if (e && !e->late && !e->late_done && !c->tentative && c->body_depth < 64 && !c->unevaluated && e->member_of) {
		cxx_member_definition_use(c, e);
		return;
	}
	if (!e || !e->late || e->late_done || c->tentative || c->body_depth >= 64 || c->unevaluated)
		return;
	e->late_done = 1;
	if (vintern_ents[e->name].len == 5 && !memcmp(vintern_ents[e->name].s, " dtor", 5))
		cxx_gen_destructor_parts_use(c, e);	/* (the destructors its end calls: their bodies first) */
	if (getenv("VCC_CXX_BODIES") && e->late->scope && e->late->scope->owner)
		fprintf(stderr, "body %.*s::%.*s\n", (int)vintern_ents[e->late->scope->owner->name].len, vintern_ents[e->late->scope->owner->name].s,
			(int)vintern_ents[e->name].len, vintern_ents[e->name].s);
	cxx_outer_save(c, &o);
	c->instantiating++;
	c->body_depth++;
	if (c->body_depth > c->stats.body_depth_max)
		c->stats.body_depth_max = c->body_depth;
	c->fail = &jb;
	c->in_template_args = 0, c->explicit_specialization = 0, c->implicit_typename = 0;
	if (!setjmp(jb)) {
		cxx_late_parse_one(c, e->late);
		c->stats.member_bodies++;
	} else
		c->stats.member_bodies_failed++;
	c->body_depth--;
	c->instantiating--;
	cxx_outer_restore(c, &o);
}
