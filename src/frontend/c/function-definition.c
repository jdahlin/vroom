/* vcc front end: a function definition, from its declarator to the checks of its body */
#include "cc-internal.h"

CC_PRIVATE void cc_function_definition_parse(cc *c, uint32_t s, const cc_declarator *d)
{
	bool clean;
	uint32_t body, first_label = c->nlabel;
	uint32_t object_sym0 = c->nsym, object_node0 = c->nnode;
	uint32_t saved_fn = c->fn;
	int saved_fnerr = c->fnerr;
	uint32_t saved_fnconv = c->fnconv;

	if (c->syms[s].flags & AST_SYMBOL_FLAG_DEFINED && !(c->syms[s].flags & AST_SYMBOL_FLAG_GNU_INLINE)) {
		cc_report_error(c, d->loc, "redefinition of '%.*s'", CC_IDENTIFIER(d->name));
		cc_report_note(c, c->syms[s].loc, "previous definition is here");
	}
	c->syms[s].flags |= AST_SYMBOL_FLAG_DEFINED;
	c->syms[s].defloc = d->loc;
	c->fn = s;
	c->fnerr = c->errors, c->fnconv = c->nconv;
	{
		ast_qualified_type rt = AST_TYPE(c, c->syms[s].ty)->base;

		if (!cc_type_is_void(c, rt) && !cc_type_complete(c, rt)) {
			cc_report_error(c, d->loc, "incomplete result type '%s' in function definition", tyname(c, rt));
			cc_report_forward_note(c, rt);
		}
	}
	cc_scope_open(c);
	for (uint32_t i = 0; i < d->npsyms; i++) {
		uint32_t p = c->lists[d->psyms + i];
		sym *y = &c->syms[p];

		if (!cc_type_is_void(c, y->ty) && !cc_type_complete(c, y->ty) && !(y->flags & AST_SYMBOL_FLAG_INVALID)) {
			cc_report_error(c, y->loc, "variable has incomplete type '%s'", tyname(c, y->ty));
			cc_report_forward_note(c, y->ty);
			y->flags |= AST_SYMBOL_FLAG_INVALID;
		}
		if (!y->name) {	/* C23: unnamed parameters in definitions */
			if (cc_keyword_map != cc_keyword_map_c23 && AST_TYPE_KIND(c, y->ty) != AST_TYPE_VOID)
				cc_report_warning(c, "c23-extensions", true, NULL, y->loc, NULL, 0, NULL, 0,
					 "omitting the parameter name in a function definition is a C23 extension");
			continue;
		}
		cc_symbol_bind(c, p);
	}
	if (!d->kr)	/* tags from the parameter list (clang: visible in the body; not K&R's) */
		for (uint32_t i = 0; i < c->nptag; i++)
			cc_symbol_bind(c, c->ptag[i]);
	c->in_loop = c->in_switch = 0;
	c->in_proto = 0, c->uv_any = 0, c->nuv = 0, c->cnd = 0, c->uv_asg = 0;
	{
		int errs = c->errors;

		body = cc_compound_statement_parse(c, NULL);
		clean = c->errors == errs;	/* after an error the body is incomplete: no flow warnings */
	}
	cc_scope_close(c);
	if (d->kr && d->npsyms)	/* (clang: at the end of the body) */
		cc_report_warning(c, "deprecated-non-prototype", true, NULL, d->loc, NULL, 0, NULL, 0,
			 "a function definition without a prototype is deprecated in all versions of C and is not supported in C23");
	cc_report_runtime_flush(c);	/* (clang: only if reachable; vcc: not under a constant-false condition) */
	{
		uint32_t rb = c->prevloc;	/* the closing brace */
		const sym *y = &c->syms[s];
		bool nr = y->flags & AST_SYMBOL_FLAG_NORETURN;
		ast_qualified_type rt = AST_TYPE(c, y->ty)->base;
		bool main_ = cc_identifier_length(y->name) == 4 && !memcmp(cc_identifier_string(y->name), "main", 4);

		if (clean && (nr || (!cc_type_is_void(c, rt) && !main_))) {
			int k = cc_reach_body_falls_off(c, body, first_label);

			if (k && nr)
				cc_report_warning(c, "invalid-noreturn", true, NULL, rb, NULL, 0, NULL, 0, "function declared 'noreturn' should not return");
			else if (k)
				cc_return_type_check(c, s, body, rb, first_label, k);
		}
	}
	if ((c->uv_any & 14) && clean)
		cc_uninitialized_check(c, body, first_label);
	{
		/* side effects of decayed VLA parameter sizes run first, in order */
		cc_id_list items;

		cc_id_list_init(&items);
		for (uint32_t i = 0; i < d->npsyms; i++) {
			uint32_t p = c->lists[d->psyms + i];

			if (c->syms[p].body)
				cc_id_list_append(c, &items, cc_statement_new(c, AST_NODE_EXPRESSION, c->syms[p].loc, c->syms[p].body, 0, 0));
		}
		if (items.n) {
			for (uint32_t i = 0; i < AST_NODE(body)->c; i++)
				cc_id_list_append(c, &items, c->lists[AST_NODE(body)->b + i]);
			body = cc_statement_new(c, AST_NODE_BLOCK, AST_NODE(body)->loc, 0, cc_id_list_freeze(c, &items), items.n);
		}
	}
	for (uint32_t l = first_label; l < c->nlabel; l++) {
		label *lb = &c->labels[l];

		if (!lb->defined && !lb->local)
			cc_report_error(c, lb->loc, "use of undeclared label '%.*s'", CC_IDENTIFIER(lb->name));
		if (!lb->local && cc_scope_get(c->ldir, lb->name) == l)
			*cc_scope_slot(c, c->ldir, lb->name) = 0;
	}
	if (c->o->instrument && !(c->syms[s].flags & (AST_SYMBOL_FLAG_NO_INSTRUMENT | AST_SYMBOL_FLAG_ALWAYS_INLINE)))
		body = cc_instrument_functions(c, s, body);
	c->syms[s].body = body;
	if (clean) cc_function_body_fold(c, object_sym0, object_node0);
	if (clean && (c->syms[s].flags & AST_SYMBOL_FLAG_OBJECT_SIZE_QUERY))
		cc_function_object_size_locals(c, object_sym0, object_node0);
	/* for the emitter: the parameter syms, on the body block */
	AST_NODE(body)->a = d->psyms;
	AST_NODE(body)->aux = (uint16_t)d->npsyms;
	c->fn = saved_fn;
	c->fnerr = saved_fnerr, c->fnconv = saved_fnconv;
}
