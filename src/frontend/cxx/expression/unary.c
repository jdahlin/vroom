/* C++ expressions: new, unary operators, casts */
#include "expression.h"

VPRIV void cxx_new_expression_parse(cxx *c)
{
	uint32_t place[4], nplace = 0, init[CXX_MAX_ARGS], loc = cxx_peek(c, 0)->loc, count = 0;
	int ninit = -1;
	bool array = false, braced = false;
	qty t = CXX_TY_UNKNOWN, at[CXX_MAX_ARGS];
	uint8_t lv[CXX_MAX_ARGS];

	cxx_accept(c, LEX_TOKEN_COLON_COLON);
	cxx_expect_kw(c, CXX_KW_new);
	if (cxx_is(c, LEX_TOKEN_LEFT_PAREN)) {
		/* placement arguments, or a parenthesized type */
		if (cxx_paren_type_id_follows(c)) {
			cxx_advance(c);
			cxx_type_id_parse(c);
			t = c->type_out;
			cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
			goto init;
		}
		cxx_call_arguments_parse(c);
		nplace = c->ncargs <= 4 ? c->ncargs : UINT32_MAX;
		if (nplace != UINT32_MAX)
			memcpy(place, c->carg_nodes, nplace * sizeof *place);
	}
	if (cxx_is(c, LEX_TOKEN_LEFT_PAREN)) {
		cxx_advance(c);
		cxx_type_id_parse(c);
		t = c->type_out;
		cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
	} else {
		cxx_new_type_id_parse(c);
		t = c->type_out, array = c->new_array, count = c->new_count;
	}
init:
	if (cxx_is(c, LEX_TOKEN_LEFT_PAREN) || cxx_is(c, LEX_TOKEN_LEFT_BRACE)) {
		braced = cxx_is(c, LEX_TOKEN_LEFT_BRACE);
		if (braced)
			cxx_braced_arguments_parse(c);
		else
			cxx_call_arguments_parse(c);
		ninit = c->ncargs <= CXX_MAX_ARGS ? (int)c->ncargs : -2;
		if (ninit >= 0) {
			memcpy(init, c->carg_nodes, (size_t)ninit * sizeof *init);
			memcpy(at, c->cargs, (size_t)ninit * sizeof *at);
			memcpy(lv, c->cargs_lval, (size_t)ninit);
		}
	}
	if (t == CXX_TY_UNKNOWN || c->tentative || ninit == -2 || nplace == UINT32_MAX) {
		cxx_et(c, t != CXX_TY_UNKNOWN && !c->tentative ? cc_type_pointer(c->ast, t) : CXX_TY_UNKNOWN, false);
		return;
	}
	if (!c->template_depth && cc_type_is_record(c->ast, t) && AST_TYPE_KIND(c->ast, t) != TY_ARRAY && !array)
		cxx_ctor_use(c, QUNQ(t), ninit > 0 ? at : NULL, ninit > 0 ? lv : NULL, ninit > 0 ? (uint32_t)ninit : 0, braced);
	cxx_et(c, cc_type_pointer(c->ast, t), false);
	if (!c->template_depth)
		cxx_gen_new(c, t, place, nplace, count, array, init, ninit, loc);
}

VPRIV void cxx_unary_expression_parse(cxx *c)
{
	switch (cxx_kind(c)) {
	case LEX_TOKEN_PLUS_PLUS: case LEX_TOKEN_MINUS_MINUS: case LEX_TOKEN_STAR: case LEX_TOKEN_AMPERSAND: case LEX_TOKEN_PLUS:
	case LEX_TOKEN_MINUS: case LEX_TOKEN_EXCLAMATION: case LEX_TOKEN_TILDE:
		if (cxx_kind(c) == LEX_TOKEN_TILDE && cxx_peek(c, 1)->kind == LEX_TOKEN_IDENTIFIER && cxx_peek(c, 2)->kind == LEX_TOKEN_LEFT_PAREN &&
		    cxx_peek(c, 3)->kind == LEX_TOKEN_RIGHT_PAREN) {
			cxx_entity *e = cxx_lookup(c, cxx_peek(c, 1)->id);

			if (e && (e->kind == CXX_ENTITY_TYPE || e->kind == CXX_ENTITY_TEMPLATE)) {	/* ~T() pseudo-destructor */
				cxx_postfix_expression_parse(c);
				return;
			}
		}
		{
			int op = cxx_kind(c);

			uint32_t opid = cxx_operator_id_at(c, 1);

			uint32_t oploc = cxx_peek(c, 0)->loc;

			cxx_advance(c);
			cxx_cast_expression_parse(c);
			{
				bool k = c->econst;
				int64_t v = c->evalue;
				uint32_t xn = c->enode;

				if (!c->tentative && !cxx_operator_call_type(c, opid, c->etype, NULL, 0)) {
					cxx_unary_type(c, op, c->etype, c->elval);
					cxx_gen_unary(c, op, xn, oploc);
				} else if (!c->tentative && c->call_resolved && c->call_picked)
					cxx_gen_operator_call(c, xn, NULL, 0, oploc);
				else if (!c->tentative)
					cxx_gen_unary(c, op, xn, oploc);	/* (no operator function: a conversion of the operand, !x) */
				if (k && (op == LEX_TOKEN_EXCLAMATION || op == LEX_TOKEN_MINUS || op == LEX_TOKEN_PLUS || op == LEX_TOKEN_TILDE)) {
					c->evalue = op == LEX_TOKEN_EXCLAMATION ? !v : op == LEX_TOKEN_MINUS ? (int64_t)(0 - (uint64_t)v) :
						    op == LEX_TOKEN_TILDE ? ~v : v;
					c->econst = 1;
				}
			}
		}
		return;
	case LEX_TOKEN_AMPERSAND_AMPERSAND:	/* GNU &&label */
		cxx_advance(c);
		cxx_expect(c, LEX_TOKEN_IDENTIFIER);
		cxx_et(c, CXX_TY_UNKNOWN, false);
		return;
	case LEX_TOKEN_COLON_COLON:
		if (cxx_peek(c, 1)->kw == CXX_KW_new) {
			cxx_new_expression_parse(c);
			return;
		}
		if (cxx_peek(c, 1)->kw == CXX_KW_delete) {
			cxx_advance(c);
			break;
		}
		break;
	}
	switch (cxx_kw(c)) {
	case CXX_KW_sizeof:
		cxx_advance(c);
		if (cxx_accept(c, LEX_TOKEN_ELLIPSIS)) {	/* sizeof...(pack) */
			cxx_entity *pk;

			cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
			pk = cxx_is(c, LEX_TOKEN_IDENTIFIER) ? cxx_lookup(c, cxx_peek(c, 0)->id) : NULL;
			cxx_expect(c, LEX_TOKEN_IDENTIFIER);
			cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
			if (pk && pk->is_pack && !pk->pack_placeholder && !c->template_depth && !c->tentative) {
				cxx_ec(c, cc_type_basic(TY_ULONG), (int64_t)pk->npack);	/* (a bound pack: its length) */
				return;
			}
		} else if (cxx_paren_type_id_follows(c)) {
			uint64_t size;
			uint32_t align;

			cxx_advance(c);
			cxx_type_id_parse(c);
			cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
			if (!c->template_depth && !c->tentative && cxx_type_size_known(c, c->type_out, &size, &align)) {
				cxx_ec(c, cc_type_basic(TY_ULONG), (int64_t)size);
				return;
			}
			if (!c->template_depth && !c->tentative && cxx_type_definitely_incomplete(c, c->type_out)) {
				/* sizeof of a type that stays incomplete: ill-formed, a substitution failure where one counts */
				cxx_et(c, cc_type_basic(TY_ULONG), false);
				CXX_INVALID(c);
				return;
			}
		} else {
			uint64_t size;
			uint32_t align;

			c->unevaluated++;
			cxx_unary_expression_parse(c);
			c->unevaluated--;
			/* sizeof expr: its type's size */
			if (!c->template_depth && !c->tentative && cxx_type_size_known(c, c->etype, &size, &align)) {
				cxx_ec(c, cc_type_basic(TY_ULONG), (int64_t)size);
				return;
			}
		}
		cxx_et(c, cc_type_basic(TY_ULONG), false);	/* size_t */
		return;
	case CXX_KW_alignof: case CXX_KW__Alignof: case CXX_KW___alignof__: case CXX_KW___alignof:
		cxx_advance(c);
		if (cxx_paren_type_id_follows(c)) {
			uint64_t size;
			uint32_t align;

			cxx_advance(c);
			cxx_type_id_parse(c);
			cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
			if (!c->template_depth && !c->tentative && cxx_type_size_known(c, c->type_out, &size, &align)) {
				cxx_ec(c, cc_type_basic(TY_ULONG), (int64_t)align);
				return;
			}
		} else
			cxx_unary_expression_parse(c);
		cxx_et(c, cc_type_basic(TY_ULONG), false);
		return;
	case CXX_KW_noexcept:
		cxx_advance(c);
		c->unevaluated++;
		cxx_paren_expression_parse(c);
		c->unevaluated--;
		cxx_et(c, cc_type_basic(TY_BOOL), false);
		return;
	case CXX_KW_new:
		cxx_new_expression_parse(c);
		return;
	case CXX_KW_delete: {
		bool array = false;
		uint32_t loc = cxx_peek(c, 0)->loc, x;

		cxx_advance(c);
		if (cxx_accept(c, LEX_TOKEN_LEFT_SQUARE))
			cxx_expect(c, LEX_TOKEN_RIGHT_SQUARE), array = true;
		cxx_cast_expression_parse(c);
		x = c->enode;
		cxx_et(c, cc_type_basic(TY_VOID), false);
		if (!c->tentative && !c->template_depth)
			cxx_gen_delete(c, x, array, loc);
		return;
	}
	case CXX_KW_co_await:
		cxx_advance(c);
		cxx_cast_expression_parse(c);
		cxx_et(c, CXX_TY_UNKNOWN, false);
		return;
	}
	cxx_postfix_expression_parse(c);
}

/* after ( type-id ): can a cast's operand start here? (else the parentheses held a value: (T())) */
VPRIV bool cxx_cast_operand_follows(cxx *c)
{
	cxx_mark m;
	bool ok;
	int k;

	cxx_mark_set(c, &m);
	cxx_advance(c);
	c->tentative++;
	{
		jmp_buf jb;

		c->fail = &jb;
		if (setjmp(jb)) {
			c->tentative--;
			cxx_mark_rewind(c, &m);
			return false;
		}
		cxx_type_id_parse(c);
		cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
	}
	c->tentative--;
	k = cxx_kind(c);
	ok = !(k == LEX_TOKEN_RIGHT_PAREN || k == LEX_TOKEN_COMMA || k == LEX_TOKEN_SEMICOLON || k == LEX_TOKEN_RIGHT_SQUARE ||
	       k == LEX_TOKEN_RIGHT_BRACE || k == LEX_TOKEN_EOF || k == LEX_TOKEN_QUESTION || k == LEX_TOKEN_COLON ||
	       k == LEX_TOKEN_EQUAL || k == LEX_TOKEN_PERIOD || k == LEX_TOKEN_ARROW || k == LEX_TOKEN_GREATER ||
	       k == LEX_TOKEN_EQUAL_EQUAL || k == LEX_TOKEN_EXCLAMATION_EQUAL || k == LEX_TOKEN_PIPE_PIPE ||
	       k == LEX_TOKEN_AMPERSAND_AMPERSAND || k == LEX_TOKEN_SLASH || k == LEX_TOKEN_PERCENT || k == LEX_TOKEN_LESS_EQUAL ||
	       k == LEX_TOKEN_GREATER_EQUAL || k == LEX_TOKEN_PIPE || k == LEX_TOKEN_CARET);
	cxx_mark_rewind(c, &m);
	return ok;
}

VPRIV void cxx_cast_expression_parse(cxx *c)
{
	if (cxx_is(c, LEX_TOKEN_LEFT_PAREN) && cxx_paren_type_id_follows(c) && cxx_cast_operand_follows(c)) {
		/* (T)x, (T){...}; but (T)(...) is also a call of a parenthesized value: the type wins */
		qty cast_to;

		cxx_advance(c);
		cxx_type_id_parse(c);
		cast_to = c->type_out;
		cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
		if (cxx_is(c, LEX_TOKEN_LEFT_BRACE)) {	/* compound literal */
			cxx_braced_init_list_parse(c);
			cxx_et(c, cast_to, false);
			return;
		}
		cxx_cast_expression_parse(c);
		{
			bool k = c->econst && cxx_const_widens(c, c->etype, cast_to, c->evalue);
			int64_t v = c->evalue;

			uint32_t xn = c->enode, cloc = c->tok[c->pos ? c->pos - 1 : 0].loc;

			if (c->etype == CXX_TY_UNKNOWN && !c->tentative)
				c->eunsure = 1;	/* ((void)x: a type whatever x is; a tentative parse types nothing) */
			cxx_et(c, cast_to, false);
			if (k && cast_to != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, cast_to) >= TY_BOOL && AST_TYPE_KIND(c->ast, cast_to) <= TY_UINT128)
				cxx_ec(c, cast_to, v);
			else
				cxx_gen_cast(c, xn, cast_to, cloc);
		}
		return;
	}
	cxx_unary_expression_parse(c);
}
