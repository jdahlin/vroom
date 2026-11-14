/* C++ templates: partial specializations: matching, ordering, selection */
#include "instantiate.h"

/* parse p's argument pattern once, its parameters bound to placeholders */
VPRIV void cxx_partial_evaluate(cxx *c, cxx_partial *p)
{
	cxx_saved s;
	jmp_buf jb;
	cxx_scope *binding;

	if (p->evaluated)
		return;
	p->evaluated = 1;
	p->ok = 0;
	binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, p->home, NULL);
	p->ph = calloc(p->params->ntparams + 1, sizeof *p->ph);
	for (uint32_t i = 0; i < p->params->ntparams; i++) {
		const cxx_tparam *tp = &p->params->tparams[i];
		cxx_entity *e;

		if (!tp->name)
			return;
		if (tp->kind == 3) {
			/* a template template parameter: its name in the pattern is a template placeholder (kind 7) */
			if (tp->pack)
				return;
			e = cxx_declare(c, binding, tp->name, CXX_ENTITY_TEMPLATE);
			if (!e)
				return;
			e->dependent = 0, e->placeholder = (uint16_t)(i + 1);
			continue;
		}
		e = cxx_declare(c, binding, tp->name, tp->kind == 1 ? CXX_ENTITY_TYPE : CXX_ENTITY_VALUE);
		if (!e)
			return;
		e->dependent = 0;
		if (tp->pack) {	/* a pack: its expansion in the pattern takes the remaining arguments */
			e->is_pack = 1, e->pack_index = -1, e->pack_placeholder = (uint16_t)(i + 1), e->npack = 0;
			if (tp->kind != 1)
				continue;	/* (a type pack: a placeholder type too, for R(A...): cxx_match_type) */
		}
		if (tp->kind == 1) {
			uint32_t tag = cc_tag_new(c->ast, TY_STRUCT, tp->name, 0);

			if (tag >= c->ntag_placeholder) {
				uint32_t nn = tag + 1024;

				c->tag_placeholder = realloc(c->tag_placeholder, nn * sizeof *c->tag_placeholder);
				memset(c->tag_placeholder + c->ntag_placeholder, 0, (nn - c->ntag_placeholder) * sizeof *c->tag_placeholder);
				c->ntag_placeholder = nn;
			}
			c->tag_placeholder[tag].owner = p, c->tag_placeholder[tag].index = i + 1;
			p->ph[i] = e->ty = QT(c->ast->tags[tag].ty, 0);
		} else
			e->placeholder = (uint16_t)(i + 1);
	}
	cxx_state_save(c, &s);
	c->instantiating++;
	c->fail = &jb;
	c->pos = p->less_pos;
	c->scope = binding;
	c->template_depth = 0, c->in_template_args = 0, c->explicit_specialization = 0, c->class_depth = 0, c->implicit_typename = 0;
	uint8_t o_unsure = c->eunsure;

	c->eunsure = 0;
	if (!setjmp(jb)) {
		cxx_template_arguments_parse(c);
		p->npattern = c->ntargs;
		p->pattern = malloc((c->ntargs ? c->ntargs : 1) * sizeof *p->pattern);
		memcpy(p->pattern, c->targs, c->ntargs * sizeof *p->pattern);
		p->ok = 1;
		for (uint32_t i = 0; i < p->npattern; i++) {
			/* decltype((void)expr-of-T): void here, whatever expr is: not deducible, substituted when matching */
			if (c->eunsure && ((p->pattern[i].kind == 1 && !cxx_mentions_placeholder(c, p->pattern[i].t, 0)) || p->pattern[i].kind == 2))
				p->pattern[i].kind = 0;
			p->subst |= p->pattern[i].kind == 0;
		}
	}
	c->eunsure = o_unsure;
	c->instantiating--;
	cxx_state_restore(c, &s);
}

/* the deducible part: p's pattern against the arguments; bind: its parameters, all deduced */
VPRIV bool cxx_partial_deduces(cxx *c, cxx_partial *p, const cxx_targ *args, uint32_t n, cxx_targ *bind)
{
	memset(bind, 0, p->params->ntparams * sizeof *bind);
	if (!cxx_match_list(c, p, p->pattern, p->npattern, args, n, bind, 0))
		return false;
	for (uint32_t i = 0; i < p->params->ntparams; i++)
		if (!bind[i].kind)
			return false;	/* (a parameter not deduced) */
	return true;
}

/*
 * p's pattern with its deduced parameters substituted, against the arguments: 1 the same, 0 not (or
 * the substitution is ill-formed: X<T, decltype((void)T().f())> for a T with no f), -1 not known.
 */
VPRIV int cxx_partial_substitute(cxx *c, cxx_partial *p, const cxx_targ *bind, const cxx_targ *args, uint32_t n)
{
	cxx_scope *binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, p->home, NULL);
	cxx_saved s;
	jmp_buf jb;
	cxx_targ got[32];
	volatile uint32_t ngot = UINT32_MAX;
	uint8_t o_invalid = c->einvalid, o_missing = c->member_missing, o_unsure = c->eunsure;
	bool invalid, unsure;

	if (n > 32 || c->instantiating > 48)
		return -1;
	for (uint32_t i = 0; i < p->params->ntparams; i++)
		if (bind[i].kind == 6)
			cxx_bind_pack(c, binding, &p->params->tparams[i], bind[i].list + bind[i].t, (uint32_t)bind[i].v);
		else
			cxx_bind(c, binding, &p->params->tparams[i], &bind[i]);
	cxx_state_save(c, &s);
	c->instantiating++, c->unevaluated++;
	c->fail = &jb;
	c->pos = p->less_pos;
	c->scope = binding;
	c->template_depth = 0, c->in_template_args = 0, c->explicit_specialization = 0, c->class_depth = 0, c->implicit_typename = 0;
	c->einvalid = 0, c->member_missing = 0, c->eunsure = 0;
	if (!setjmp(jb)) {
		cxx_template_arguments_parse(c);
		if (c->ntargs <= 32) {
			memcpy(got, c->targs, c->ntargs * sizeof *got);
			ngot = c->ntargs;
		}
	}
	c->instantiating--, c->unevaluated--;
	cxx_state_restore(c, &s);
	invalid = c->einvalid || c->member_missing, unsure = c->eunsure;
	if (getenv("VCC_CXX_SUBST")) {
		char sp[256] = "?";
		size_t sl = 0;

		if (n && args[0].kind == 1)
			cxx_type_spell(c, args[0].t, sp, sizeof sp, &sl);
		fprintf(stderr, "substitute: invalid %u missing %u unsure %u parsed %d args %u first kind %u last kind %u; first argument %s\n",
			c->einvalid, c->member_missing, c->eunsure, ngot != UINT32_MAX, (unsigned)ngot, ngot && ngot != UINT32_MAX ? got[0].kind : 9,
			ngot && ngot != UINT32_MAX ? got[ngot - 1].kind : 9, sp);
	}
	c->einvalid = o_invalid, c->member_missing = o_missing, c->eunsure = o_unsure;
	if (invalid)
		return 0;
	if (ngot == UINT32_MAX || unsure)
		return -1;
	if (ngot > n)
		return 0;
	for (uint32_t i = 0; i < ngot; i++)
		if (!got[i].kind)
			return -1;
	if (!cxx_args_equal(got, args, ngot))
		return 0;
	/* the arguments the pattern does not write are the primary's defaults: known ones are in p->pattern;
	   one that uses the earlier parameters (class = enable_if_t<cond<T>>) is computed from the arguments */
	if (ngot < n && p->npattern != n)
		return 0;
	for (uint32_t i = ngot; i < n; i++) {
		cxx_targ d;

		if (p->pattern[i].kind) {
			if (!cxx_args_equal(&p->pattern[i], &args[i], 1))
				return 0;
			continue;
		}
		if (!p->primary || i >= p->primary->ntparams)
			return -1;
		{
			const cxx_tparam *tp = &p->primary->tparams[i];
			cxx_scope *b = cxx_scope_new(c, CXX_SCOPE_BLOCK, p->home, NULL);

			if (!tp->def_b && p->primary_d && p->primary_d != p->primary && i < p->primary_d->ntparams)
				tp = &p->primary_d->tparams[i];
			for (uint32_t j = 0; j < i; j++)
				if (!p->primary->tparams[j].pack)
					cxx_bind(c, b, &p->primary->tparams[j], &args[j]);
			memset(&d, 0, sizeof d);
			o_missing = c->member_missing, c->member_missing = 0;
			if (!tp->def_b || !cxx_default_argument(c, tp, b, &d)) {
				bool missing = c->member_missing;

				c->member_missing = o_missing;
				return missing ? 0 : -1;
			}
			c->member_missing = o_missing;
			if (!cxx_args_equal(&d, &args[i], 1))
				return 0;
		}
	}
	return 1;
}

/*
 * A pattern shorter than the primary's parameter list takes the primary's default arguments for the
 * rest (ps: the primary's parameters, d: an earlier declaration's, which may hold the defaults).  Only
 * defaults that do not use the earlier parameters (class = void) are known here; false otherwise.
 */
VPRIV bool cxx_partial_complete(cxx *c, cxx_partial *p, const cxx_scope *ps, const cxx_scope *d, uint32_t n)
{
	cxx_scope *empty;

	if (p->completed || p->npattern >= n || !ps || (p->npattern && p->pattern[p->npattern - 1].kind == 5))
		return true;
	if (ps->ntparams && ps->tparams[ps->ntparams - 1].pack && p->npattern + 1 >= ps->ntparams)
		return true;	/* (the primary ends in a pack: the pattern's arguments are all of them) */
	if (n > ps->ntparams)
		return false;
	empty = cxx_scope_new(c, CXX_SCOPE_BLOCK, p->home, NULL);
	p->pattern = realloc(p->pattern, n * sizeof *p->pattern);
	p->primary = ps, p->primary_d = d;
	for (uint32_t i = p->npattern; i < n; i++) {
		const cxx_tparam *tp = &ps->tparams[i];

		if (!tp->def_b && d && d != ps && i < d->ntparams)
			tp = &d->tparams[i];
		memset(&p->pattern[i], 0, sizeof p->pattern[i]);
		if (!tp->def_b)
			return false;
		/* a default that uses the earlier parameters: computed from the arguments when matching */
		if (!cxx_default_argument(c, tp, empty, &p->pattern[i])) {
			memset(&p->pattern[i], 0, sizeof p->pattern[i]);
			p->subst = 1;
		}
	}
	p->npattern = n, p->completed = 1;
	return true;
}

/* does p match the arguments?  1 yes (bind: its parameters), 0 no, -1 not known */
VPRIV int cxx_partial_matches(cxx *c, cxx_partial *p, const cxx_targ *args, uint32_t n, cxx_targ *bind)
{
	if (!cxx_partial_deduces(c, p, args, n, bind))
		return 0;
	return p->subst ? cxx_partial_substitute(c, p, bind, args, n) : 1;
}

/* a is at least as specialized as b: b's pattern matches a's (a's placeholders as types) */
VPRIV bool cxx_partial_at_least_as_specialized(cxx *c, cxx_partial *a, cxx_partial *b)
{
	cxx_targ bind[64];

	if (b->params->ntparams > 64)
		return false;
	return cxx_partial_deduces(c, b, a->pattern, a->npattern, bind);
}

VPRIV int cxx_constraint_parse(cxx *c, cxx_partial *p, const cxx_targ *bind, uint32_t pos, const cxx_targ *first);

VPRIV bool cxx_partial_constrained(const cxx_partial *p)
{
	if (p->req_pos)
		return true;
	for (uint32_t i = 0; i < p->params->ntparams; i++)
		if (p->params->tparams[i].constraint_b)
			return true;
	return false;
}

/* p's constraints with its parameters bound: 1 satisfied (or none), 0 not, -1 not known */
VPRIV int cxx_partial_constraints(cxx *c, cxx_partial *p, const cxx_targ *bind)
{
	for (uint32_t i = 0; i < p->params->ntparams; i++) {
		const cxx_tparam *tp = &p->params->tparams[i];

		if (tp->constraint_b && bind[i].kind == 1) {
			int v = cxx_constraint_parse(c, p, bind, tp->constraint_b, &bind[i]);

			if (v <= 0)
				return v;
		}
	}
	return p->req_pos ? cxx_constraint_parse(c, p, bind, p->req_pos, NULL) : 1;
}

/*
 * The partial specialization the arguments select: 0 none (the primary template), 1 one (*pick,
 * its parameters in bind), -1 not known (a pattern that cannot be evaluated, an ambiguity).
 */
VPRIV int cxx_partial_select(cxx *c, cxx_template *t, const cxx_targ *args, uint32_t n, cxx_partial **pick, cxx_targ *bind)
{
	cxx_partial *cand[16];
	uint32_t ncand = 0;
	cxx_targ tmp[64];

	for (uint32_t i = 0; i < t->npartials; i++) {
		cxx_partial *p = t->partials[i];

		cxx_partial_evaluate(c, p);
		if (!p->ok || p->params->ntparams > 64 || !cxx_partial_complete(c, p, t->params ? t->params : t->declared, t->declared, n)) {
			if (getenv("VCC_CXX_WHY")) fprintf(stderr, "partial unknown (%s) %s\n", !p->ok ? "pattern" : "defaults", t->path ? t->path : "?");
			return -1;	/* a pattern vcc cannot evaluate might match: do not guess */
		}
		int m = cxx_partial_matches(c, p, args, n, tmp);


		if (m < 0) {
			if (getenv("VCC_CXX_WHY")) fprintf(stderr, "partial unknown (substitution) %s\n", t->path ? t->path : "?");
			return -1;	/* (a pattern vcc cannot decide might match) */
		}
		if (m) {
			/* its constraints with the deduced parameters: template<C T> struct X<T>, requires ... */
			int ok = cxx_partial_constraints(c, p, tmp);

			if (ok < 0) {
				if (getenv("VCC_CXX_WHY")) fprintf(stderr, "partial unknown (constraint) %s\n", t->path ? t->path : "?");
				return -1;
			}
			if (!ok)
				continue;
			if (ncand == 16)
				return -1;
			cand[ncand++] = p;
		}
	}
	if (!ncand)
		return 0;
	/* the most specialized: at least as specialized as every other, and not the reverse; of two with the
	   same pattern the constrained one (two constrained ones: their constraints' subsumption, not done) */
	for (uint32_t i = 0; i < ncand; i++) {
		bool best = true;

		for (uint32_t k = 0; k < ncand && best; k++) {
			bool ik, ki;

			if (k == i)
				continue;
			ik = cxx_partial_at_least_as_specialized(c, cand[i], cand[k]);
			ki = cxx_partial_at_least_as_specialized(c, cand[k], cand[i]);
			if (ik && ki)
				best = cxx_partial_constrained(cand[i]) && !cxx_partial_constrained(cand[k]);
			else
				best = ik && !ki;
		}
		if (best) {
			*pick = cand[i];
			cxx_partial_matches(c, cand[i], args, n, bind);
			return 1;
		}
	}
	return -1;	/* ambiguous */
}

VPRIV bool cxx_instance_parse(cxx *c, cxx_instance *x);

VPRIV cxx_entity *cxx_instantiate_now(cxx *c, cxx_entity *te, const cxx_targ *args, uint32_t nargs, bool complete);
