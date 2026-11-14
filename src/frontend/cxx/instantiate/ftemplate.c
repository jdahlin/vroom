/* C++ templates: function templates, member templates defined outside */
#include "instantiate.h"

VPRIV void cxx_outer_save(cxx *c, cxx_outer *o)
{
	cxx_state_save(c, &o->s);
	o->ntargs = c->ntargs, o->ndp = c->ndecl_params, o->ndd = c->decl_ndefaults;
	o->targs = malloc((o->ntargs ? o->ntargs : 1) * sizeof *o->targs);
	memcpy(o->targs, c->targs, o->ntargs * sizeof *o->targs);
	o->dp = malloc((o->ndp ? o->ndp : 1) * sizeof *o->dp);
	memcpy(o->dp, c->decl_params, o->ndp * sizeof *o->dp);
	o->this_type = c->this_type, o->etype = c->etype, o->elval = c->elval, o->ecallee = c->ecallee, o->last_fn = c->last_fn;
	o->this_const = c->this_const, o->ret_capture = c->ret_capture, o->ret_lval = c->ret_lval, o->ret_type = c->ret_type;
	c->ret_capture = 0;
	o->unevaluated = c->unevaluated;
	o->enode = c->enode, o->snode = c->snode, o->sdone = c->sdone, o->sdepth = c->sdepth, o->gen_block = c->gen_block;
	o->gen_stack_depth = c->gen_stack_depth;
}

VPRIV void cxx_outer_restore(cxx *c, cxx_outer *o)
{
	cxx_state_restore(c, &o->s);
	c->sig_pack_types = NULL, c->sig_pack_n = UINT32_MAX;	/* (a pack signature's: consumed, or left by a parse that failed) */
	memcpy(c->targs, o->targs, o->ntargs * sizeof *o->targs), c->ntargs = o->ntargs;
	memcpy(c->decl_params, o->dp, o->ndp * sizeof *o->dp), c->ndecl_params = o->ndp, c->decl_ndefaults = o->ndd;
	free(o->targs), free(o->dp);
	c->this_type = o->this_type, c->etype = o->etype, c->elval = o->elval, c->ecallee = o->ecallee, c->last_fn = o->last_fn;
	c->this_const = o->this_const, c->ret_capture = o->ret_capture, c->ret_lval = o->ret_lval, c->ret_type = o->ret_type;
	c->unevaluated = o->unevaluated;
	cxx_gen_unwind(c, o->gen_stack_depth);	/* (a body whose parse failed: the one around it goes on) */
	c->enode = o->enode, c->snode = o->snode, c->sdone = o->sdone, c->sdepth = o->sdepth, c->gen_block = o->gen_block;
}

/* ---- function templates: deduction from a call's argument types -------------------------- */




/*
 * The entity of one instantiation of a function template (code generation: what is defined and called):
 * the template's name and scope, the arguments it was instantiated with; its type when its signature is
 * parsed with them bound.
 */
VPRIV cxx_entity *cxx_ftemplate_instance(cxx *c, cxx_partial *p, const cxx_targ *args, uint32_t nargs)
{
	cxx_entity *te = c->ftmpl_te, *inst;
	cxx_targ *copy;

	if (!te || !p->home)
		return NULL;
	inst = calloc(1, sizeof *inst);
	inst->name = te->name, inst->kind = CXX_ENTITY_VALUE, inst->ty = CXX_TY_UNKNOWN, inst->pack_index = -1;
	if (p->home->kind == CXX_SCOPE_CLASS)
		inst->member_of = p->home;
	else
		inst->home = p->home;
	inst->fn_const = te->fn_const, inst->fn_static = te->fn_static, inst->fn_inline = 1, inst->is_instance = 1;
	copy = malloc((nargs ? nargs : 1) * sizeof *copy);
	memcpy(copy, args, nargs * sizeof *copy);
	inst->targs = copy, inst->ntargs = nargs, inst->inst_pack_at = UINT32_MAX, inst->inst_params = p->params, inst->abi_tag = te->abi_tag, inst->inst_ftmpl = te->ftmpl;
	return inst;
}

VPRIV cxx_ftemplate *cxx_ftemplate_new(cxx *c, cxx_scope *params, cxx_scope *home, uint32_t pos, cxx_scope *ctor_class)
{
	cxx_ftemplate *f = calloc(1, sizeof *f);

	(void)c;
	f->p.params = params, f->p.home = home, f->pos = pos, f->sig = CXX_TY_UNKNOWN, f->ctor_class = ctor_class;
	return f;
}

VPRIV void cxx_ftemplate_requires(cxx_ftemplate *f, uint32_t pos)
{
	if (f)
		f->req_pos = pos;
}

VPRIV void cxx_ftemplate_evaluate(cxx *c, cxx_ftemplate *f);

/* a function template's instance: the template's signature as declared, its type parameters' placeholder
   types (ph[i] for parameter i); CXX_TY_UNKNOWN: not known */
/* does the function template have packs (its parameter lists may take any number of arguments) */
VPRIV bool cxx_ftemplate_has_packs(const cxx_entity *e)
{
	const cxx_ftemplate *f = e ? e->ftmpl : NULL;

	return !f || f->packs || f->pack_tp || f->pack_param || f->variadic;
}

VPRIV qty cxx_instance_declared_sig(cxx *c, const cxx_entity *inst, const qty **ph, uint32_t *nph, uint32_t *pack_param)
{
	cxx_ftemplate *f = inst->inst_ftmpl;

	if (!f)
		return CXX_TY_UNKNOWN;
	cxx_ftemplate_evaluate(c, f);
	if (f->packs || !f->p.ph || f->sig == CXX_TY_UNKNOWN)
		return CXX_TY_UNKNOWN;
	*ph = f->p.ph, *nph = f->p.params->ntparams;
	*pack_param = f->pack_param;	/* (the function parameter pack Ts&&... ts: index + 1, 0: none) */
	return f->sig;
}

/* does the template's declaration (its return type or parameters) use decltype? (clang mangles the
   expression, DT ... E: not written by vcc yet) */
/* the template's parameters as placeholders (T_, ...), even when its signature did not parse (NULL: none) */
VPRIV const qty *cxx_instance_placeholders(cxx *c, const cxx_entity *inst, uint32_t *n)
{
	cxx_ftemplate *f = inst->inst_ftmpl;

	if (!f)
		return NULL;
	cxx_ftemplate_evaluate(c, f);
	if (f->packs || !f->p.ph)
		return NULL;
	*n = f->p.params->ntparams;
	return f->p.ph;
}

/* the template's declaration's first token (its decl-specifiers; 0: not known) */
VPRIV uint32_t cxx_instance_decl_pos(const cxx_entity *inst)
{
	return inst->inst_ftmpl ? inst->inst_ftmpl->pos : 0;
}

VPRIV bool cxx_instance_uses_decltype(cxx *c, const cxx_entity *inst)
{
	const cxx_ftemplate *f = inst->inst_ftmpl;
	int depth = 0;

	if (!f || !f->pos)
		return false;
	for (uint32_t p = f->pos; p < c->ntok && p < f->pos + 4096; p++) {
		const cxx_token *t = &c->tok[p];

		if (t->kind == LEX_TOKEN_LEFT_PAREN || t->kind == LEX_TOKEN_LEFT_SQUARE)
			depth++;
		else if (t->kind == LEX_TOKEN_RIGHT_PAREN || t->kind == LEX_TOKEN_RIGHT_SQUARE)
			depth--;
		else if (!depth && (t->kind == LEX_TOKEN_LEFT_BRACE || t->kind == LEX_TOKEN_SEMICOLON))
			return false;
		else if (t->kind == LEX_TOKEN_IDENTIFIER && t->len == 8 && !memcmp(t->s, "decltype", 8))
			return true;
	}
	return false;
}

VPRIV const cxx_scope *cxx_ftemplate_params(const cxx_ftemplate *f)
{
	return f ? f->p.params : NULL;
}

/* parse the signature once, the parameters bound to placeholders */
VPRIV void cxx_ftemplate_evaluate(cxx *c, cxx_ftemplate *f)
{
	cxx_partial *p = &f->p;
	cxx_saved s;
	jmp_buf jb;
	cxx_scope *binding;

	if (f->evaluated)
		return;
	f->evaluated = 1;
	if (p->params->ntparams > 64)
		return;
	binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, p->home, NULL);
	p->ph = calloc(p->params->ntparams + 1, sizeof *p->ph);
	for (uint32_t i = 0; i < p->params->ntparams; i++) {
		const cxx_tparam *tp = &p->params->tparams[i];
		cxx_entity *e;

		if (tp->pack && !tp->name) {
			f->packs = 1;	/* (unnamed packs: not deduced yet) */
			return;
		}
		if (tp->kind == 3) {
			/* a template template parameter: not deduced; its name in a parameter's type is a template
			   placeholder, so that the type has a shape (X<T, Layout>&) */
			f->packs = 1;
			if (tp->name && !tp->pack) {
				e = cxx_declare(c, binding, tp->name, CXX_ENTITY_TEMPLATE);
				if (e)
					e->dependent = 0, e->placeholder = (uint16_t)(i + 1);
			}
			continue;
		}
		bool rest_defaulted = true;	/* (a pack followed only by parameters with defaults: enable_if_t<...> = 0) */

		for (uint32_t j = i + 1; j < p->params->ntparams; j++)
			rest_defaulted &= !p->params->tparams[j].pack && p->params->tparams[j].def_b && p->params->tparams[j].kind != 3;
		if (tp->pack && (tp->kind != 1 || (i + 1 != p->params->ntparams && !rest_defaulted)))
			f->packs = 1;	/* (value packs, packs not last: not deduced; their placeholders still give the parameters'
					   shapes, which rule the candidate out for arguments of another shape) */
		if (tp->pack && !f->packs)
			f->pack_tp = i + 1;	/* Ts...: a placeholder as a type's; the parameter Ts&&... ts gives its pattern */
		if (!tp->name)
			continue;	/* (unnamed: a default argument, a constraint in its type) */
		e = cxx_declare(c, binding, tp->name, tp->kind == 1 ? CXX_ENTITY_TYPE : CXX_ENTITY_VALUE);
		if (!e)
			return;
		e->dependent = 0;
		if (tp->pack)	/* tuple<Ts...> in a parameter's type: a pattern whose arguments end in the pack */
			e->is_pack = 1, e->pack_index = -1, e->pack_placeholder = (uint16_t)(i + 1), e->npack = 0;
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
	c->pos = f->pos;
	c->scope = binding;
	c->template_depth = 0, c->in_template_args = 0, c->explicit_specialization = 0, c->class_depth = 0, c->implicit_typename = 0;
	if (!setjmp(jb)) {
		f->sig = cxx_function_signature_parse(c, true, NULL, f->ctor_class);
		if (c->sig_is_function) {
			/* each parameter's type on its own: one that vcc cannot form does not stop the others */
			f->np = c->ndecl_params, f->variadic = c->decl_variadic, f->has_pt = 1;
			f->pt = malloc((f->np ? f->np : 1) * sizeof *f->pt);
			for (uint32_t i = 0; i < f->np; i++)
				f->pt[i] = c->decl_params[i].ty;
			/* the pack's parameter: the last one, its type a pattern over the pack's placeholder */
			if (f->pack_tp && c->decl_pack_param != UINT32_MAX && c->decl_pack_param + 1 == f->np &&
			    f->pt[f->np - 1] != CXX_TY_UNKNOWN)
				f->pack_param = f->np;
		}
	}
	if (f->pack_tp && !f->pack_param)
		f->packs = 1;	/* (the pack is not expanded by a plain last parameter: tuple<Ts...>, only explicit) */
	c->instantiating--;
	cxx_state_restore(c, &s);
	if (f->sig != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, f->sig) != TY_FUNC)
		f->sig = CXX_TY_UNKNOWN;
}

/*
 * The return type with the deduced arguments: the signature re-parsed with them bound (member typedefs,
 * partial specializations: as for a real instance).  want_body: the function is the one called, in
 * evaluated code: its body is instantiated too (once).  An auto return type needs the body anyway.
 */
/*
 * A member template's instance whose class declares it without a body: its definition outside the class
 * template (template<class T> template<class... A> R X<T>::f(...) {...}), parsed with the class instance's
 * arguments for the outer parameters and bind for its own (the last from pack_from a pack; ptypes its
 * elements' parameter types), its code generated for inst.  False: no such definition.
 */
VPRIV bool cxx_member_template_ooc_body(cxx *c, cxx_ftemplate *f, cxx_entity *inst, const cxx_targ *bind, uint32_t nbind,
					 uint32_t pack_from, qty *ptypes, uint32_t pn)
{
	cxx_scope *cs = f->p.home;
	cxx_instance *x = cs && cs->kind == CXX_SCOPE_CLASS && cs->owner ? cs->owner->instance : NULL;
	cxx_template *t = x ? x->t : NULL;
	const struct cxx_ooc *d = NULL;
	cxx_scope *binding;
	cxx_outer o;
	jmp_buf jb;
	uint32_t matches = 0;

	if (!t || !inst || (x->pos && (!x->pargs || (t->params && x->npargs == t->params->ntparams))))
		return false;	/* (a partial specialization's instance: its definitions told apart by their parameter count) */
	const cxx_targ *bargs = x->pos ? x->pargs : x->args;
	uint32_t nbargs = x->pos ? x->npargs : x->nargs;

	for (uint32_t i = 0; i < t->noocs; i++)
		if (t->oocs[i].inner && (t->oocs[i].name == inst->name ||	/* (a constructor template: X<...>::X, by the class's name) */
					 (inst->name == cxx_ctor_id() && cs->owner && t->oocs[i].name == cs->owner->name)) &&
		    t->oocs[i].inner->ntparams == f->p.params->ntparams &&
		    (x->pos ? t->oocs[i].params->ntparams == nbargs : !t->params || t->oocs[i].params->ntparams == t->params->ntparams))
			d = &t->oocs[i], matches++;	/* (the primary's: a partial specialization's definitions, vector<bool, A>'s, have its parameters) */
	if (matches != 1 || d->params->ntparams > nbargs + 1)
		return false;
	binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, d->home, NULL);
	for (uint32_t i = 0; i < d->params->ntparams; i++) {
		const cxx_tparam *tp = &d->params->tparams[i];

		if (tp->pack) {
			if (i + 1 != d->params->ntparams)
				return false;
			if (x->pos && i < nbargs && bargs[i].kind == 6)	/* (a partial's deduced pack: its elements) */
				cxx_bind_pack(c, binding, tp, bargs[i].list + bargs[i].t, (uint32_t)bargs[i].v);
			else
				cxx_bind_pack(c, binding, tp, bargs + i, nbargs - i);
			break;
		}
		if (i >= nbargs)
			return false;
		cxx_bind(c, binding, tp, &bargs[i]);
	}
	for (uint32_t i = 0; i < d->inner->ntparams; i++) {
		if (pack_from != UINT32_MAX && i == pack_from) {
			cxx_bind_pack(c, binding, &d->inner->tparams[i], bind + i, nbind - i);
			break;
		}
		if (i >= nbind)
			return false;
		cxx_bind(c, binding, &d->inner->tparams[i], &bind[i]);
	}
	cxx_scope_use(c, binding, cs);	/* (the class's members, by name in the body, as X<T>:: makes them) */
	cxx_outer_save(c, &o);
	c->instantiating++, c->body_depth++;
	c->unevaluated = 0;
	c->fail = &jb;
	c->pos = d->pos;
	c->scope = binding;
	c->template_depth = 0, c->in_template_args = 0, c->explicit_specialization = 0, c->class_depth = 0, c->implicit_typename = 0;
	c->this_type = cs->owner->ty ? cs->owner->ty : CXX_TY_UNKNOWN, c->this_const = inst->fn_const;
	c->sig_pack_types = ptypes, c->sig_pack_n = pack_from != UINT32_MAX ? pn : UINT32_MAX;
	c->gen_instance = inst;
	qty was = inst->ty;

	if (!setjmp(jb)) {
		bool b = false;

		inst->ty = CXX_TY_UNKNOWN;	/* (its type again, from the definition: the body's code takes it) */
		/* (a constructor template: no return type to parse, its class says what it constructs) */
		(void)cxx_function_signature_parse(c, false, &b, f->ctor_class ? f->ctor_class : inst->name == cxx_ctor_id() ? cs : NULL);
		c->stats.ooc_bodies++;
	} else
		c->stats.ooc_failed++;
	if (inst->ty == CXX_TY_UNKNOWN)
		inst->ty = was;	/* (the definition's parse failed: the declaration's type, for its calls) */
	c->gen_instance = NULL;
	c->instantiating--, c->body_depth--;
	cxx_outer_restore(c, &o);
	return true;
}

/* the template's parameter count and parameter i's type as its signature has them (placeholders for its own
   parameters; 0: not known) */
VPRIV uint32_t cxx_ftemplate_np(cxx *c, cxx_ftemplate *f)
{
	cxx_ftemplate_evaluate(c, f);
	return f->has_pt ? f->np : 0;
}

VPRIV qty cxx_ftemplate_pt(cxx_ftemplate *f, uint32_t i)
{
	return f->has_pt && i < f->np ? f->pt[i] : CXX_TY_UNKNOWN;
}
