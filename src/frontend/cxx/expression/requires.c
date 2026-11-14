/* C++ expressions: requires-expressions */
#include "expression.h"

VPRIV void cxx_requires_expression_parse(cxx *c)
{
	cxx_scope *rs = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL);
	cxx_scope *outer = c->scope;
	uint32_t targs = c->in_template_args;

	/* its value: every requirement holds (1), one fails for sure (0), or vcc cannot tell (-1) */
	bool eval = !c->template_depth && !c->tentative;
	int value = 1;

	cxx_advance(c);	/* requires */
	c->in_template_args = 0;
	cxx_scope_push(c, rs);
	if (eval && cxx_is(c, LEX_TOKEN_LEFT_PAREN) && cxx_lookahead_fn(c, cxx_lambda_parameters_parse)) {
		/* parameters as a function's: names with their types (lvalues in the requirements) */
		cxx_lambda_parameters_parse(c);
		for (uint32_t i = 0; i < c->fn_nparams; i++)
			if (c->fn_pnames[i] > 1) {
				cxx_entity *e = cxx_declare(c, rs, c->fn_pnames[i], CXX_ENTITY_VALUE);

				if (e)
					e->ty = c->fn_pdecl[i];
			}
	} else if (cxx_is(c, LEX_TOKEN_LEFT_PAREN)) {
		/* parameters: type and name pairs */
		cxx_advance(c);
		while (!cxx_is(c, LEX_TOKEN_RIGHT_PAREN)) {
			uint32_t last_id = 0;
			int depth = 0;

			while (!(depth == 0 && (cxx_is(c, LEX_TOKEN_COMMA) || cxx_is(c, LEX_TOKEN_RIGHT_PAREN)))) {
				if (cxx_is(c, LEX_TOKEN_EOF))
					cxx_fail(c, "unterminated requires parameters");
				if (cxx_is(c, LEX_TOKEN_LEFT_PAREN) || cxx_is(c, LEX_TOKEN_LESS))
					depth++;
				else if (cxx_is(c, LEX_TOKEN_RIGHT_PAREN) || cxx_is(c, LEX_TOKEN_GREATER))
					depth--;
				else if (cxx_is(c, LEX_TOKEN_GREATER_GREATER))
					depth -= 2;
				if (depth == 0 && cxx_is(c, LEX_TOKEN_IDENTIFIER) && !cxx_kw(c))
					last_id = cxx_peek(c, 0)->id;
				cxx_advance(c);
			}
			if (last_id)
				cxx_declare(c, rs, last_id, CXX_ENTITY_VALUE);
			if (!cxx_accept(c, LEX_TOKEN_COMMA))
				break;
		}
		cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
	}
	cxx_expect(c, LEX_TOKEN_LEFT_BRACE);
	while (!cxx_accept(c, LEX_TOKEN_RIGHT_BRACE)) {
		int r = -1;	/* this requirement */

		c->einvalid = 0;
		if (cxx_is_kw(c, CXX_KW_typename)) {	/* type requirement */
			c->member_missing = 0;
			cxx_type_id_parse(c);
			r = c->member_missing ? 0 : c->type_out != CXX_TY_UNKNOWN ? 1 : -1;
		} else if (cxx_is(c, LEX_TOKEN_LEFT_BRACE)) {	/* compound requirement */
			qty et;
			bool lv;

			cxx_advance(c);
			cxx_expression_parse(c);
			et = c->etype, lv = c->elval;
			r = c->einvalid ? 0 : et != CXX_TY_UNKNOWN ? 1 : -1;
			cxx_expect(c, LEX_TOKEN_RIGHT_BRACE);
			cxx_accept_kw(c, CXX_KW_noexcept);
			if (cxx_accept(c, LEX_TOKEN_ARROW)) {
				/* type-constraint: Concept<decltype((expr)), args> */
				cxx_entity *ce = cxx_constraint_name_parse(c);

				if (!ce)
					cxx_fail(c, "expected a concept after '->'");
				if (r == 1 && eval) {
					cxx_targ a[32];

					if (c->ntargs < 31) {
						a[0] = (cxx_targ){ .kind = 1, .t = lv ? cxx_type_reference(c, et, false) : et };
						memcpy(a + 1, c->targs, c->ntargs * sizeof *a);
						r = cxx_concept_value(c, ce, a, c->ntargs + 1);
					} else
						r = -1;
				}
			}
		} else if (cxx_is_kw(c, CXX_KW_requires)) {	/* nested requirement */
			cxx_advance(c);
			cxx_conditional_expression_parse(c);
			r = c->econst ? c->evalue != 0 : -1;
		} else {
			cxx_expression_parse(c);
			r = c->einvalid ? 0 : c->etype != CXX_TY_UNKNOWN ? 1 : -1;
		}
		cxx_expect(c, LEX_TOKEN_SEMICOLON);
		if (r == 0)
			value = 0;
		else if (r < 0 && value == 1)
			value = -1;
	}
	c->req_value = (int8_t)(eval ? value : -1);
	c->scope = outer;
	c->in_template_args = targs;
}

/* ---- primary and postfix ----------------------------------------------------------------- */
