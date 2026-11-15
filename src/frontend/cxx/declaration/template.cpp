/* C++ declarations: template parameters and template declarations */
#include "declaration.h"

VPRIV bool cxx_tokens_same(cxx *c, uint32_t a, uint32_t ae, uint32_t b, uint32_t be)
{
	if (ae - a != be - b)
		return false;
	for (uint32_t i = 0; i < ae - a; i++)
		if (c->tok[a + i].kind != c->tok[b + i].kind || c->tok[a + i].len != c->tok[b + i].len ||
		    memcmp(c->tok[a + i].s, c->tok[b + i].s, c->tok[a + i].len))
			return false;
	return true;
}

/*
 * A function template's redeclaration (its definition after a declaration): the default template arguments
 * the earlier declaration gave, which this one may not repeat (json's concat_into: enable_if_t<...> = 0 on
 * the declaration only).  An earlier one matches when it has as many parameters of the same kinds and each
 * value parameter's type is written the same.
 */
VPRIV void cxx_tparams_inherit_defaults(cxx *c, cxx_scope *ps, cxx_scope *in, uint32_t name)
{
	bool missing = false;

	if (!ps || !in || name <= 1)
		return;
	for (uint32_t i = 0; i < ps->ntparams; i++)
		missing |= !ps->tparams[i].def_b && !ps->tparams[i].pack;
	if (!missing)
		return;
	for (cxx_entity *o = cxx_lookup_local(in, name); o; o = o->next) {
		const cxx_scope *os;
		bool same = true, gives = false;

		if (o->kind != CXX_ENTITY_FUNCTION_TEMPLATE || !o->ftmpl || !(os = cxx_ftemplate_params(o->ftmpl)) || os == ps ||
		    os->ntparams != ps->ntparams)
			continue;
		for (uint32_t i = 0; i < ps->ntparams && same; i++) {
			const cxx_tparam *a = &ps->tparams[i], *b = &os->tparams[i];

			same = a->kind == b->kind && a->pack == b->pack;
			if (same && a->kind == 2)
				same = a->type_b && b->type_b &&
				       cxx_tokens_same(c, a->type_b, cxx_tparam_type_end(c, a->type_b), b->type_b, cxx_tparam_type_end(c, b->type_b));
			gives |= !a->def_b && b->def_b;
		}
		if (!same || !gives)
			continue;
		for (uint32_t i = 0; i < ps->ntparams; i++)
			if (!ps->tparams[i].def_b && os->tparams[i].def_b)
				ps->tparams[i].def_b = os->tparams[i].def_b, ps->tparams[i].def_e = os->tparams[i].def_e;
		return;
	}
}

/* ---- templates --------------------------------------------------------------------------- */

VPRIV void cxx_template_parameter_parse(cxx *c)
{
	cxx_attributes_skip(c);
	if (cxx_is_kw(c, CXX_KW_template)) {	/* template<...> class/typename name = default */
		cxx_scope *ts;

		cxx_advance(c);
		ts = cxx_scope_new(c, CXX_SCOPE_TEMPLATE, c->scope, NULL);
		cxx_scope_push(c, ts);
		cxx_expect(c, LEX_TOKEN_LESS);
		if (!cxx_accept_greater(c)) {
			do
				cxx_template_parameter_parse(c);
			while (cxx_accept(c, LEX_TOKEN_COMMA));
			if (!cxx_accept_greater(c))
				cxx_fail(c, "expected '>'");
		}
		cxx_scope_pop(c);
		if (!cxx_accept_kw(c, CXX_KW_class) && !cxx_accept_kw(c, CXX_KW_typename))
			cxx_fail(c, "expected 'class' in a template template parameter");
		bool pack = cxx_accept(c, LEX_TOKEN_ELLIPSIS);
		uint32_t pname = 0;

		if (cxx_is(c, LEX_TOKEN_IDENTIFIER)) {
			cxx_entity *e = cxx_declare(c, c->scope, cxx_peek(c, 0)->id, CXX_ENTITY_TEMPLATE);

			pname = cxx_peek(c, 0)->id;
			if (e)
				e->dependent = 1;
			cxx_advance(c);
		}
		uint32_t db = 0, de = 0;

		if (cxx_accept(c, LEX_TOKEN_EQUAL)) {
			cxx_scope *q;
			bool dep;

			db = c->pos;	/* (its default: a template's name, looked up when an instance needs it) */
			cxx_nested_name_parse(c, &q, &dep);
			cxx_expect(c, LEX_TOKEN_IDENTIFIER);
			de = c->pos;
		}
		cxx_tparam_add(c, 3, pname, pack, db, de);
		return;
	}
	if ((cxx_is_kw(c, CXX_KW_class) || cxx_is_kw(c, CXX_KW_typename)) &&
	    (cxx_peek(c, 1)->kind == LEX_TOKEN_IDENTIFIER || cxx_peek(c, 1)->kind == LEX_TOKEN_ELLIPSIS ||
	     cxx_peek(c, 1)->kind == LEX_TOKEN_COMMA || cxx_peek(c, 1)->kind == LEX_TOKEN_GREATER ||
	     cxx_peek(c, 1)->kind == LEX_TOKEN_EQUAL || cxx_peek(c, 1)->kind == LEX_TOKEN_GREATER_GREATER) &&
	    !(cxx_peek(c, 1)->kind == LEX_TOKEN_IDENTIFIER &&
	      (cxx_peek(c, 2)->kind == LEX_TOKEN_COLON_COLON || cxx_peek(c, 2)->kind == LEX_TOKEN_LESS))) {	/* (typename X<..>::type v) */
		/* type parameter */
		bool pack;
		uint32_t pname = 0, db = 0, de = 0;

		cxx_advance(c);
		pack = cxx_accept(c, LEX_TOKEN_ELLIPSIS);
		if (cxx_is(c, LEX_TOKEN_IDENTIFIER) && !cxx_kw(c)) {
			cxx_entity *e = cxx_declare(c, c->scope, cxx_peek(c, 0)->id, CXX_ENTITY_TYPE);

			pname = cxx_peek(c, 0)->id;
			if (e)
				e->dependent = 1;
			cxx_advance(c);
		}
		if (cxx_accept(c, LEX_TOKEN_EQUAL)) {
			db = c->pos;
			cxx_type_id_parse(c);
			de = c->pos;
		}
		cxx_tparam_add(c, 1, pname, pack, db, de);
		return;
	}
	/* a constrained type parameter: Concept<args> name, Concept name */
	if (cxx_is(c, LEX_TOKEN_IDENTIFIER) || cxx_is(c, LEX_TOKEN_COLON_COLON)) {
		uint32_t save = c->pos, save_undo = c->nundo;
		cxx_scope *q;
		bool dep;
		cxx_entity *e;
		uint32_t cb = c->pos;

		cxx_nested_name_parse(c, &q, &dep);
		e = cxx_is(c, LEX_TOKEN_IDENTIFIER) ? cxx_name_lookup(c, q, dep, cxx_peek(c, 0)->id) : NULL;
		e = e ? cxx_entity_template(e) : NULL;
		if (e && e->kind == CXX_ENTITY_CONCEPT) {
			bool pack;
			uint32_t pname = 0, db = 0, de = 0;

			cxx_advance(c);
			if (cxx_is(c, LEX_TOKEN_LESS))
				cxx_template_arguments_parse(c);
			pack = cxx_accept(c, LEX_TOKEN_ELLIPSIS);
			if (cxx_is(c, LEX_TOKEN_IDENTIFIER) && !cxx_kw(c)) {
				cxx_entity *p = cxx_declare(c, c->scope, cxx_peek(c, 0)->id, CXX_ENTITY_TYPE);

				pname = cxx_peek(c, 0)->id;
				if (p)
					p->dependent = 1;
				cxx_advance(c);
			}
			if (cxx_accept(c, LEX_TOKEN_EQUAL)) {
				db = c->pos;
				cxx_type_id_parse(c);
				de = c->pos;
			}
			cxx_tparam_add(c, 1, pname, pack, db, de);
			if (c->scope->kind == CXX_SCOPE_TEMPLATE && !c->tentative && c->scope->ntparams && !pack)
				c->scope->tparams[c->scope->ntparams - 1].constraint_b = cb;	/* (checked when deducing) */
			return;
		}
		cxx_rewind(c, save, save_undo);
	}
	/* non-type parameter: a parameter declaration */
	{
		bool q, f;
		uint32_t name;

		uint32_t tb = c->pos;

		if (!(cxx_decl_specifiers_parse(c, NULL, false) & (DS_TYPE | DS_MODIFIER)))
			cxx_fail(c, "expected a template parameter");
		uint32_t db = 0, de = 0;
		bool pack = cxx_is(c, LEX_TOKEN_ELLIPSIS);

		c->type_in = c->ds_type;
		name = cxx_declarator_parse(c, true, &q, &f, NULL);
		if (name > 1) {
			cxx_entity *e = cxx_declare(c, c->scope, name, CXX_ENTITY_VALUE);

			if (e)
				e->dependent = 1;
		}
		if (cxx_accept(c, LEX_TOKEN_EQUAL)) {
			db = c->pos;
			c->in_template_args++;
			cxx_conditional_expression_parse(c);
			c->in_template_args--;
			de = c->pos;
		}
		pack |= c->pos > 0 && c->tok[c->pos - 1].kind == LEX_TOKEN_ELLIPSIS && !db;
		cxx_tparam_add(c, 2, name > 1 ? name : 0, pack, db, de);
		if (c->scope->kind == CXX_SCOPE_TEMPLATE && !c->tentative && c->scope->ntparams)
			c->scope->tparams[c->scope->ntparams - 1].type_b = tb;	/* (enable_if_t<cond, int> = 0: checked when deducing) */
	}
}

VPRIV void cxx_requires_clause_parse(cxx *c)
{
	if (!cxx_accept_kw(c, CXX_KW_requires))
		return;
	cxx_conditional_expression_parse(c);	/* (a constraint-logical-or-expression; the parser stops at ; { , ) */
}

VPRIV void cxx_template_declaration_parse(cxx *c, int context)
{
	cxx_scope *ts;

	cxx_expect_kw(c, CXX_KW_template);
	if (!cxx_is(c, LEX_TOKEN_LESS)) {	/* explicit instantiation: template class X<int>; */
		cxx_declaration_parse(c, context);
		return;
	}
	if (cxx_peek(c, 1)->kind == LEX_TOKEN_GREATER) {
		/* template<>: an explicit specialization is not a template (nothing in it is dependent) */
		uint32_t depth = c->template_depth;

		cxx_advance(c);
		cxx_advance(c);
		c->template_depth = 0;
		c->explicit_specialization++;
		cxx_declaration_parse(c, context);
		c->explicit_specialization--;
		c->template_depth = depth;
		return;
	}
	ts = cxx_scope_new(c, CXX_SCOPE_TEMPLATE, c->scope, NULL);
	cxx_scope_push(c, ts);
	c->template_depth++;
	cxx_advance(c);
	uint32_t implicit = c->implicit_typename;

	c->implicit_typename = 0;
	if (!cxx_accept_greater(c)) {
		do
			cxx_template_parameter_parse(c);
		while (cxx_accept(c, LEX_TOKEN_COMMA));
		if (!cxx_accept_greater(c))
			cxx_fail(c, "expected '>' after template parameters");
	}
	c->implicit_typename = implicit;
	uint32_t freq = c->ftmpl_req;

	c->ftmpl_req = cxx_is_kw(c, CXX_KW_requires) ? c->pos : 0;
	cxx_requires_clause_parse(c);
	uint32_t fbegin = c->ftmpl_begin;
	cxx_scope *fscope = c->ftmpl_scope;

	c->ftmpl_begin = c->pos, c->ftmpl_scope = ts;	/* (a function template: its signature is re-parsed to deduce) */
	if (cxx_is_kw(c, CXX_KW_concept)) {
		uint32_t name;

		cxx_advance(c);
		name = cxx_peek(c, 0)->id;
		cxx_expect(c, LEX_TOKEN_IDENTIFIER);
		cxx_attributes_skip(c);
		cxx_expect(c, LEX_TOKEN_EQUAL);
		{
			cxx_entity *ce = cxx_declare(c, cxx_declaration_scope(c)->parent ? cxx_declaration_scope(c) : c->global, name,
						     CXX_ENTITY_CONCEPT);

			/* its constraint-expression, evaluated with arguments bound (cxx_concept_value) */
			if (ce && !ce->ftmpl && c->template_depth == 1)
				ce->ftmpl = cxx_ftemplate_new(c, ts, ts->parent, c->pos, NULL);
		}
		cxx_conditional_expression_parse(c);
		cxx_expect(c, LEX_TOKEN_SEMICOLON);
	} else
		cxx_declaration_parse(c, context);
	c->ftmpl_req = freq;
	c->ftmpl_begin = fbegin, c->ftmpl_scope = fscope;
	c->template_depth--;
	cxx_scope_pop(c);
}


/* ---- classes and enums ------------------------------------------------------------------- */

VPRIV uint32_t cxx_class_body_parse(cxx *c, cxx_scope *cs);
