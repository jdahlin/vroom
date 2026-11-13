/* vcc front end: a function definition, from its declarator to the checks of its body */
#include "ccimpl.h"

VPRIV void function_def(cc *c, uint32_t s, const declr *d)
{
	bool clean;
	uint32_t body, first_label = c->nlabel;
	uint32_t object_sym0 = c->nsym, object_node0 = c->nnode;
	uint32_t saved_fn = c->fn;
	int saved_fnerr = c->fnerr;
	uint32_t saved_fnconv = c->fnconv;

	if (c->syms[s].flags & SF_DEFINED && !(c->syms[s].flags & SF_GNU_INLINE)) {
		cc_error(c, d->loc, "redefinition of '%.*s'", IDF(d->name));
		note(c, c->syms[s].loc, "previous definition is here");
	}
	c->syms[s].flags |= SF_DEFINED;
	c->syms[s].defloc = d->loc;
	c->fn = s;
	c->fnerr = c->errors, c->fnconv = c->nconv;
	{
		qty rt = T(c, c->syms[s].ty)->base;

		if (!is_void(c, rt) && !complete(c, rt)) {
			cc_error(c, d->loc, "incomplete result type '%s' in function definition", tyname(c, rt));
			fwd_note(c, rt);
		}
	}
	scope_open(c);
	for (uint32_t i = 0; i < d->npsyms; i++) {
		uint32_t p = c->lists[d->psyms + i];
		sym *y = &c->syms[p];

		if (!is_void(c, y->ty) && !complete(c, y->ty) && !(y->flags & SF_INVALID)) {
			cc_error(c, y->loc, "variable has incomplete type '%s'", tyname(c, y->ty));
			fwd_note(c, y->ty);
			y->flags |= SF_INVALID;
		}
		if (!y->name) {	/* C23: unnamed parameters in definitions */
			if (kwmap != kwmap_c23 && KIND(c, y->ty) != TY_VOID)
				cc_warnx(c, "c23-extensions", true, NULL, y->loc, NULL, 0, NULL, 0,
					 "omitting the parameter name in a function definition is a C23 extension");
			continue;
		}
		sym_bind(c, p);
	}
	if (!d->kr)	/* tags from the parameter list (clang: visible in the body; not K&R's) */
		for (uint32_t i = 0; i < c->nptag; i++)
			sym_bind(c, c->ptag[i]);
	c->in_loop = c->in_switch = 0;
	c->in_proto = 0, c->uv_any = 0, c->nuv = 0, c->cnd = 0, c->uv_asg = 0;
	{
		int errs = c->errors;

		body = compound_stmt(c, NULL);
		clean = c->errors == errs;	/* after an error the body is incomplete: no flow warnings */
	}
	scope_close(c);
	if (d->kr && d->npsyms)	/* (clang: at the end of the body) */
		cc_warnx(c, "deprecated-non-prototype", true, NULL, d->loc, NULL, 0, NULL, 0,
			 "a function definition without a prototype is deprecated in all versions of C and is not supported in C23");
	rt_flush(c);	/* (clang: only if reachable; vcc: not under a constant-false condition) */
	{
		uint32_t rb = c->prevloc;	/* the closing brace */
		const sym *y = &c->syms[s];
		bool nr = y->flags & SF_NORETURN;
		qty rt = T(c, y->ty)->base;
		bool main_ = idlen_(y->name) == 4 && !memcmp(idstr_(y->name), "main", 4);

		if (clean && (nr || (!is_void(c, rt) && !main_))) {
			int k = body_falls_off(c, body, first_label);

			if (k && nr)
				cc_warnx(c, "invalid-noreturn", true, NULL, rb, NULL, 0, NULL, 0, "function declared 'noreturn' should not return");
			else if (k)
				return_type_warn(c, s, body, rb, first_label, k);
		}
	}
	if ((c->uv_any & 14) && clean)
		uninit_check(c, body, first_label);
	{
		/* side effects of decayed VLA parameter sizes run first, in order */
		vec items;

		vinit(&items);
		for (uint32_t i = 0; i < d->npsyms; i++) {
			uint32_t p = c->lists[d->psyms + i];

			if (c->syms[p].body)
				vpush(c, &items, mkstmt(c, N_EXPR, c->syms[p].loc, c->syms[p].body, 0, 0));
		}
		if (items.n) {
			for (uint32_t i = 0; i < N(body)->c; i++)
				vpush(c, &items, c->lists[N(body)->b + i]);
			body = mkstmt(c, N_BLOCK, N(body)->loc, 0, vfreeze(c, &items), items.n);
		}
	}
	for (uint32_t l = first_label; l < c->nlabel; l++) {
		label *lb = &c->labels[l];

		if (!lb->defined && !lb->local)
			cc_error(c, lb->loc, "use of undeclared label '%.*s'", IDF(lb->name));
		if (!lb->local && dget(c->ldir, lb->name) == l)
			*dslot(c, c->ldir, lb->name) = 0;
	}
	if (c->o->instrument && !(c->syms[s].flags & (SF_NOINSTR | SF_ALWAYS_INLINE)))
		body = instrument(c, s, body);
	c->syms[s].body = body;
	if (clean) fold_local_builtins(c, object_sym0, object_node0);
	if (clean && (c->syms[s].flags & SF_BOS_QUERY))
		object_size_locals(c, object_sym0, object_node0);
	/* for the emitter: the parameter syms, on the body block */
	N(body)->a = d->psyms;
	N(body)->aux = (uint16_t)d->npsyms;
	c->fn = saved_fn;
	c->fnerr = saved_fnerr, c->fnconv = saved_fnconv;
}
