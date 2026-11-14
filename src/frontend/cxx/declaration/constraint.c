/* C++ declarations: requires-clauses, constraints, range-for and lambda parameters */
#include "declaration.h"

/* for (decl-specifiers declarator : range): the declaration (not a structured binding), up to the colon */
VPRIV void cxx_for_range_declaration_parse(cxx *c)
{
	bool q, f, is_auto;
	int quals;

	cxx_attributes_skip(c);
	if (!(cxx_decl_specifiers_parse(c, NULL, false) & (DS_TYPE | DS_MODIFIER)))
		cxx_fail(c, "expected a type");
	is_auto = c->ds_auto && c->ds_type == CXX_TY_UNKNOWN, quals = c->ds_quals;
	c->type_in = is_auto ? QT(QI(cxx_auto_type(c)), quals) : c->ds_type;
	c->range_name = cxx_declarator_parse(c, false, &q, &f, NULL);
	c->range_type = c->type_out, c->range_auto = is_auto;
	if (!cxx_is(c, LEX_TOKEN_COLON))
		cxx_fail(c, "expected ':'");
}

/* a lambda's ( parameters ): as a function's (c->fn_params, c->fn_pnames, c->fn_nparams, c->fn_unknown) */
VPRIV void cxx_lambda_parameters_parse(cxx *c)
{
	cxx_parameter_clause_parse(c);
}

/* requires constraint-expression, at 'requires': 1 true, 0 false, -1 not known */
VPRIV int cxx_requires_clause_value(cxx *c)
{
	int v;
	uint8_t invalid = c->einvalid, missing = c->member_missing;
	uint32_t b;

	cxx_expect_kw(c, CXX_KW_requires);
	b = c->pos;
	c->einvalid = 0, c->member_missing = 0;
	c->unevaluated++;
	cxx_conditional_expression_parse(c);
	c->unevaluated--;
	v = c->econst ? c->evalue != 0 : -1;
	if (v < 0 && (c->einvalid || c->member_missing)) {
		/* a substitution failure in an atomic constraint: not satisfied ([temp.constr.atomic]/3); in a
		   conjunction the whole is then false (one with || at its top: not decided here) */
		bool disj = false;
		int depth = 0;

		for (uint32_t i = b; i < c->pos && !disj; i++) {
			int k = c->tok[i].kind;

			if (k == LEX_TOKEN_LEFT_PAREN && i == b)
				continue;	/* (requires (A && B): its own parentheses) */
			if (k == LEX_TOKEN_LEFT_PAREN || k == LEX_TOKEN_LEFT_SQUARE || k == LEX_TOKEN_LESS)
				depth++;
			else if ((k == LEX_TOKEN_RIGHT_PAREN || k == LEX_TOKEN_RIGHT_SQUARE || k == LEX_TOKEN_GREATER) && depth)
				depth--;
			else if (!depth && k == LEX_TOKEN_PIPE_PIPE)
				disj = true;
		}
		if (!disj)
			v = 0;
	}
	c->einvalid = invalid, c->member_missing = missing;
	return v;
}

/* a type-constraint's concept, at its name: [nested::]Concept [<args>] (the arguments in c->targs, c->ntargs) */
VPRIV cxx_entity *cxx_constraint_name_parse(cxx *c)
{
	cxx_scope *q;
	bool dep;
	cxx_entity *e;

	cxx_nested_name_parse(c, &q, &dep);
	e = cxx_is(c, LEX_TOKEN_IDENTIFIER) ? cxx_name_lookup(c, q, dep, cxx_peek(c, 0)->id) : NULL;
	e = e ? cxx_entity_template(e) : NULL;
	if (!e || e->kind != CXX_ENTITY_CONCEPT)
		return NULL;
	cxx_advance(c);
	c->ntargs = 0;
	if (cxx_is(c, LEX_TOKEN_LESS))
		cxx_template_arguments_parse(c);
	return e;
}

/* a template template parameter's default argument, at its name: the template (out->kind 4), else unknown */
VPRIV void cxx_template_name_default_parse(cxx *c, cxx_targ *out)
{
	cxx_scope *q;
	bool dep;
	cxx_entity *e;

	out->kind = 0;
	cxx_nested_name_parse(c, &q, &dep);
	if (!cxx_is(c, LEX_TOKEN_IDENTIFIER) || dep)
		cxx_fail(c, "expected a template name");
	e = q ? cxx_lookup_in(c, q, cxx_peek(c, 0)->id, 0) : cxx_lookup(c, cxx_peek(c, 0)->id);
	e = e ? cxx_entity_prefer(e, CXX_ENTITY_TEMPLATE) : NULL;
	cxx_advance(c);
	if (e && e->kind == CXX_ENTITY_TEMPLATE && !e->is_pack && e->tmpl && !e->dependent)
		out->kind = 4, out->v = (int64_t)(intptr_t)e;
}
