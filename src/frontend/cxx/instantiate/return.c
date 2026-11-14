/* C++ templates: function template signatures and bodies instantiated */
#include "instantiate.h"

VPRIV qty cxx_ftemplate_return(cxx *c, cxx_ftemplate *f, const cxx_targ *bind, bool want_body)
{
	cxx_partial *p = &f->p;
	uint32_t np = p->params->ntparams;
	cxx_saved s;
	jmp_buf jb;
	cxx_scope *binding;
	volatile qty r = CXX_TY_UNKNOWN, sig = CXX_TY_UNKNOWN;
	volatile bool body = false;
	volatile int8_t viable = 1;
	cxx_targ *targs;
	uint32_t ntargs = c->ntargs, ndp = c->ndecl_params, ndd = c->decl_ndefaults;
	cxx_param *dp;
	qty this_type = c->this_type, etype = c->etype;
	uint8_t this_const = c->this_const, ret_capture = c->ret_capture, ret_lval = c->ret_lval;
	qty ret_type = c->ret_type;
	bool elval = c->elval;
	cxx_entity *ecallee = c->ecallee;
	uint32_t unevaluated = c->unevaluated, at = UINT32_MAX;

	for (uint32_t i = 0; i < f->ndone; i++)
		if (cxx_args_equal(f->done[i].args, bind, np)) {
			if (!want_body || f->done[i].body) {
				c->stats.bodies_cached++;
				c->bound_sig = f->done[i].sig;
				c->sig_requires = f->done[i].viable;
				if (want_body)
					c->call_instance = f->done[i].inst;
				return f->done[i].ret;
			}
			at = i;	/* its signature was parsed; now its body */
			break;
		}
	c->bound_sig = CXX_TY_UNKNOWN;
	if (c->body_depth >= 64)
		return at != UINT32_MAX ? f->done[at].ret : CXX_TY_UNKNOWN;
	if (at == UINT32_MAX) {
		if (f->ndone == f->cdone)
			f->done = realloc(f->done, (f->cdone = f->cdone ? f->cdone * 2 : 4) * sizeof *f->done);
		f->done[f->ndone].args = malloc((np ? np : 1) * sizeof *bind);
		memcpy(f->done[f->ndone].args, bind, np * sizeof *bind);
		f->done[f->ndone].ret = CXX_TY_UNKNOWN, f->done[f->ndone].sig = CXX_TY_UNKNOWN, f->done[f->ndone].body = 0;
		f->done[f->ndone].viable = 1, f->done[f->ndone].n = np, f->done[f->ndone].inst = NULL, f->done[f->ndone].value = 0;
		at = f->ndone++;
	}
	if (want_body)
		f->done[at].body = 1;	/* (before the body: a recursive call finds it) */
	/* code generation: this instantiation's function */
	cxx_entity *inst = NULL;
	uint32_t g_depth = c->gen_stack_depth, g_enode = c->enode, g_snode = c->snode, g_sdone = c->sdone, g_sdepth = c->sdepth;
	void *g_block = c->gen_block;

	if (want_body) {
		if (!f->done[at].inst)
			f->done[at].inst = cxx_ftemplate_instance(c, p, bind, np);
		inst = f->done[at].inst;
	}
	/* what the enclosing parse still uses */
	targs = malloc((ntargs ? ntargs : 1) * sizeof *targs);
	memcpy(targs, c->targs, ntargs * sizeof *targs);
	dp = malloc((ndp ? ndp : 1) * sizeof *dp);
	memcpy(dp, c->decl_params, ndp * sizeof *dp);
	binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, p->home, NULL);
	for (uint32_t i = 0; i < p->params->ntparams; i++)
		cxx_bind(c, binding, &p->params->tparams[i], &bind[i]);
	cxx_state_save(c, &s);
	c->instantiating++;
	c->fail = &jb;
	c->pos = f->pos;
	c->scope = binding;
	c->template_depth = 0, c->in_template_args = 0, c->explicit_specialization = 0, c->class_depth = 0, c->implicit_typename = 0;
	/* a member template: this is its class */
	c->this_type = p->home->kind == CXX_SCOPE_CLASS && p->home->owner && p->home->owner->ty ? p->home->owner->ty : CXX_TY_UNKNOWN, c->this_const = inst ? inst->fn_const : c->this_const;	/* (a const member template: this is const) */
	c->body_depth++;
	if (c->body_depth > c->stats.body_depth_max)
		c->stats.body_depth_max = c->body_depth;
	c->sig_auto = 0;
	if (want_body)
		c->unevaluated = 0;	/* (the body is evaluated code, whatever the call was in) */
	c->gen_instance = inst;
	if (!setjmp(jb)) {
		bool b = false;
		qty t = cxx_function_signature_parse(c, false, want_body ? &b : NULL, f->ctor_class);

		body = b;
		viable = c->sig_requires;
		if (t != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, t) == TY_FUNC)
			r = AST_TYPE(c->ast, t)->base, sig = c->sig_fake ? CXX_TY_UNKNOWN : t;	/* (its parameters: only if they are the real ones) */
		c->stats.bodies += body;
		if (want_body && !b && inst && viable == 1)
			body = cxx_member_template_ooc_body(c, f, inst, bind, np, UINT32_MAX, NULL, 0);	/* (defined outside its class) */
	} else
		c->stats.bodies_failed++;
	c->gen_instance = NULL;
	c->body_depth--;
	c->instantiating--;
	cxx_state_restore(c, &s);
	cxx_gen_unwind(c, g_depth);
	c->enode = g_enode, c->snode = g_snode, c->sdone = g_sdone, c->sdepth = g_sdepth, c->gen_block = g_block;
	if (want_body)
		c->call_instance = inst;
	memcpy(c->targs, targs, ntargs * sizeof *targs), c->ntargs = ntargs;
	memcpy(c->decl_params, dp, ndp * sizeof *dp), c->ndecl_params = ndp, c->decl_ndefaults = ndd;
	free(targs), free(dp);
	c->this_type = this_type, c->etype = etype, c->elval = elval, c->ecallee = ecallee;
	c->this_const = this_const, c->ret_capture = ret_capture, c->ret_lval = ret_lval, c->ret_type = ret_type;
	c->unevaluated = unevaluated;
	/* auto with no trailing type: only the body tells (an unevaluated call instantiates it too) */
	if (!want_body && c->sig_auto && r == CXX_TY_UNKNOWN && viable == 1)
		return cxx_ftemplate_return(c, f, bind, true);
	for (uint32_t i = 0; i < f->ndone; i++)
		if (cxx_args_equal(f->done[i].args, bind, np)) {
			if (r != CXX_TY_UNKNOWN || f->done[i].ret == CXX_TY_UNKNOWN)
				f->done[i].ret = r, f->done[i].sig = sig;
			if (!want_body || viable != 1)
				f->done[i].viable = viable;	/* (not viable, however it was found out) */
			r = f->done[i].ret, sig = f->done[i].sig, viable = f->done[i].viable;
			break;
		}
	c->bound_sig = sig;
	c->sig_requires = viable;
	return r;
}

/*
 * A template whose last parameter is a pack, every argument given explicitly: its return type, the
 * signature parsed with the parameters bound (not cached, no body).  c->sig_requires: 0 when the
 * substitution fails.
 */
VPRIV qty cxx_ftemplate_return_pack(cxx *c, cxx_ftemplate *f, const cxx_targ *xargs, uint32_t nx, bool want_body, qty *ptypes,
				     uint32_t pn)
{
	cxx_partial *p = &f->p;
	uint32_t np = p->params->ntparams, at = UINT32_MAX;
	cxx_scope *binding;
	cxx_targ acopy[32];
	cxx_outer o;
	jmp_buf jb;
	volatile qty r = CXX_TY_UNKNOWN;
	volatile int8_t viable = -1;
	volatile bool body = false;

	if (c->body_depth >= 64 || nx > 32) {
		c->sig_requires = -1;
		return CXX_TY_UNKNOWN;
	}
	memcpy(acopy, xargs, nx * sizeof *xargs);
	/* done before with these arguments (the pack's elements are part of the key) */
	for (uint32_t i = 0; i < f->ndone; i++)
		if (f->done[i].n == nx + 1000 && cxx_args_equal(f->done[i].args, acopy, nx)) {
			if (!want_body || f->done[i].body) {
				c->bound_sig = f->done[i].sig, c->sig_requires = f->done[i].viable;
				if (want_body)
					c->call_instance = f->done[i].inst;	/* (the call's function: this one's, not one an inner call left) */
				return f->done[i].ret;
			}
			at = i;
			break;
		}
	if (at == UINT32_MAX) {
		if (f->ndone == f->cdone)
			f->done = realloc(f->done, (f->cdone = f->cdone ? f->cdone * 2 : 4) * sizeof *f->done);
		at = f->ndone++;
		memset(&f->done[at], 0, sizeof f->done[at]);
		f->done[at].args = malloc((nx ? nx : 1) * sizeof *acopy);
		memcpy(f->done[at].args, acopy, nx * sizeof *acopy);
		f->done[at].n = nx + 1000;	/* (apart from the fixed-length entries) */
		f->done[at].ret = CXX_TY_UNKNOWN, f->done[at].sig = CXX_TY_UNKNOWN, f->done[at].viable = 1;
	}
	if (want_body)
		f->done[at].body = 1;
	/* code generation: this instantiation's function (its pack's arguments last, from np - 1) */
	cxx_entity *inst = NULL;
	uint32_t g_depth = c->gen_stack_depth, g_enode = c->enode, g_snode = c->snode, g_sdone = c->sdone, g_sdepth = c->sdepth;
	void *g_block = c->gen_block;

	if (want_body) {
		if (!f->done[at].inst && (f->done[at].inst = cxx_ftemplate_instance(c, p, acopy, nx)))
			f->done[at].inst->inst_pack_at = np - 1;
		inst = f->done[at].inst;
	}
	binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, p->home, NULL);
	{
		/* (the pack's parameter: pack_tp's, the others in order in acopy; its elements after them) */
		uint32_t pk = f->pack_tp ? f->pack_tp - 1 : np - 1, k = 0;

		for (uint32_t i = 0; i < np; i++)
			if (i != pk)
				cxx_bind(c, binding, &p->params->tparams[i], &acopy[k++]);
		cxx_bind_pack(c, binding, &p->params->tparams[pk], acopy + (np - 1), nx - (np - 1));
	}
	cxx_outer_save(c, &o);
	c->instantiating++, c->body_depth++;
	c->fail = &jb;
	c->pos = f->pos;
	c->scope = binding;
	c->template_depth = 0, c->in_template_args = 0, c->explicit_specialization = 0, c->class_depth = 0, c->implicit_typename = 0;
	c->this_type = p->home->kind == CXX_SCOPE_CLASS && p->home->owner && p->home->owner->ty ? p->home->owner->ty : CXX_TY_UNKNOWN, c->this_const = inst ? inst->fn_const : c->this_const;	/* (a const member template: this is const) */
	if (want_body) {
		c->unevaluated = 0;
		if (c->body_depth > c->stats.body_depth_max)
			c->stats.body_depth_max = c->body_depth;
	}
	c->sig_pack_types = ptypes, c->sig_pack_n = pn;	/* (the signature with the pack expanded; in a body, the pack of values) */
	c->gen_instance = inst;
	if (!setjmp(jb)) {
		bool b = false;
		qty t = cxx_function_signature_parse(c, false, want_body ? &b : NULL, f->ctor_class);

		body = b;
		viable = c->sig_requires;
		if (t != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, t) == TY_FUNC)
			r = AST_TYPE(c->ast, t)->base;
		c->bound_sig = c->sig_fake ? CXX_TY_UNKNOWN : t;
		c->stats.bodies += body;
		if (want_body && !b && inst && viable == 1) {
			qty bs = c->bound_sig;

			body = cxx_member_template_ooc_body(c, f, inst, acopy, nx, np - 1, ptypes, pn);	/* (defined outside its class) */
			c->bound_sig = bs;
		}
	} else {
		c->bound_sig = CXX_TY_UNKNOWN;
		if (want_body)
			c->stats.bodies_failed++;
	}
	c->gen_instance = NULL;
	c->instantiating--, c->body_depth--;
	{
		qty sig = c->bound_sig;

		cxx_outer_restore(c, &o);
		c->bound_sig = sig;
	}
	cxx_gen_unwind(c, g_depth);
	c->enode = g_enode, c->snode = g_snode, c->sdone = g_sdone, c->sdepth = g_sdepth, c->gen_block = g_block;
	if (want_body)
		c->call_instance = inst;
	/* (found again: the body may have added entries) */
	for (uint32_t i = 0; i < f->ndone; i++)
		if (f->done[i].n == nx + 1000 && cxx_args_equal(f->done[i].args, acopy, nx)) {
			if (r != CXX_TY_UNKNOWN || f->done[i].ret == CXX_TY_UNKNOWN)
				f->done[i].ret = r, f->done[i].sig = c->bound_sig;
			if (!want_body || viable != 1)
				f->done[i].viable = viable;
			r = f->done[i].ret, viable = f->done[i].viable, c->bound_sig = f->done[i].sig;
			break;
		}
	c->sig_requires = viable;
	return r;
}
