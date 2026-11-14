/* C++ expressions: try blocks in expressions, lambdas */
#include "expression.h"

/*
 * A try block's body, at its {, where code is generated: built as a function of its own, a closure's
 * operator() that captures everything by reference and returns 0, so that it can be called through the
 * function that has the exception table (cxx_gen_try).  *closure_node: the closure object; false: not
 * built (the body is parsed all the same).
 */
VPRIV bool cxx_try_block_parse(cxx *c, uint32_t *closure_node, cxx_entity **call_out)
{
	uint32_t serial = c->lambda_serial++, lname = cxx_closure_name(c, serial), loc = cxx_peek(c, 0)->loc, bnode;
	uint32_t tag = cc_tag_new(c->ast, TY_STRUCT, lname, 0);
	cxx_entity *owner = calloc(1, sizeof *owner), *call;
	cxx_scope *outer = c->scope, *cs, *fs;
	cxx_gen_lambda l;

	*closure_node = 0, *call_out = NULL;
	owner->kind = CXX_ENTITY_TYPE, owner->name = lname, owner->pack_index = -1;
	owner->ty = QT(c->ast->tags[tag].ty, 0);
	cs = cxx_scope_new(c, CXX_SCOPE_CLASS, outer, owner);
	cs->is_lambda = 1;
	owner->members = cs;
	cxx_tag_scope_set(c, tag, cs);
	call = cxx_declare(c, cs, vintern_cstr("operator()"), CXX_ENTITY_VALUE);
	fs = cxx_scope_new(c, CXX_SCOPE_BLOCK, outer, NULL);
	memset(&l, 0, sizeof l);
	l.def = 1, l.is_try = 1;
	if (call) {
		call->ty = cxx_type_function(c, cc_type_basic(TY_INT), NULL, 0, false);
		call->fn_const = 1, call->member_of = cs;
	}
	if (!call || !cxx_gen_lambda_symbol(c, call, serial) || (l.floor = c->ast->nsym, !cxx_gen_function_begin(c, call, fs, NULL, 0))) {
		cxx_compound_statement_parse(c);
		return false;
	}
	c->gen_lambda = &l;
	c->scope = fs;
	cxx_compound_statement_parse(c);
	bnode = cxx_gen_return_zero(c, c->snode, loc);
	if (!cxx_gen_lambda_finish(c, &l, owner, &bnode, loc))
		cxx_gen_fail(c);
	cxx_gen_function_end(c, call, bnode);
	c->scope = outer;
	if (!(c->ast->syms[call->sym].flags & SF_DEFINED))
		return false;
	cxx_gen_lambda_object(c, &l, owner->ty, loc);
	*closure_node = c->enode, *call_out = call;
	c->enode = 0;
	return *closure_node != 0;
}

VPRIV void cxx_lambda_parse(cxx *c)
{
	cxx_scope *ls = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL);
	cxx_scope *outer = c->scope;
	uint32_t depth = c->class_depth, targs = c->in_template_args;
	/* a closure type is built for a lambda outside templates whose parameter types are known */
	bool typed = false, generic = false;
	qty params[16], ret = CXX_TY_UNKNOWN;
	uint32_t nparams = 0;
	uint8_t o_capture = c->ret_capture, o_lval = c->ret_lval, o_seen = c->ret_seen;
	qty o_type = c->ret_type;

	/* code generation: the body is parsed for its types first, then again as the closure's operator() */
	bool want_gen = cxx_gen_on(c) && c->gen_block != NULL;
	uint8_t was_gen = c->gen;
	uint32_t cap_b = c->pos, cap_e, body_b, gen_pnames[16], lloc = cxx_peek(c, 0)->loc;

	c->lambda_type = CXX_TY_UNKNOWN;
	c->lambda_node = 0;
	cxx_skip_balanced(c);	/* [captures]: init-captures parse later with the body (names are values) */
	cap_e = c->pos;
	c->in_template_args = 0;
	cxx_scope_push(c, ls);
	cxx_attributes_skip(c);
	if (cxx_is(c, LEX_TOKEN_LESS)) {	/* []<typename T>(...) */
		generic = true;
		cxx_advance(c);
		c->template_depth++;
		while (!cxx_accept_greater(c)) {
			int kw = cxx_kw(c);

			if ((kw == CXX_KW_typename || kw == CXX_KW_class) && cxx_peek(c, 1)->kind != LEX_TOKEN_COLON_COLON) {
				cxx_advance(c);
				cxx_accept(c, LEX_TOKEN_ELLIPSIS);
				if (cxx_is(c, LEX_TOKEN_IDENTIFIER)) {
					cxx_entity *e = cxx_declare(c, ls, cxx_peek(c, 0)->id, CXX_ENTITY_TYPE);

					if (e)
						e->dependent = 1;
					cxx_advance(c);
				}
			} else
				cxx_advance(c);
			cxx_accept(c, LEX_TOKEN_COMMA);
		}
		c->template_depth--;
		cxx_requires_clause_parse(c);
	}
	if (!generic && !c->template_depth && !c->tentative &&
	    (!cxx_is(c, LEX_TOKEN_LEFT_PAREN) || cxx_lookahead_fn(c, cxx_lambda_parameters_parse))) {
		/* parameters as a function's: their names with their types */
		if (cxx_is(c, LEX_TOKEN_LEFT_PAREN)) {
			cxx_lambda_parameters_parse(c);
			typed = !c->fn_unknown && c->fn_nparams <= 16 && !c->fn_variadic;
			nparams = c->fn_nparams;
			for (uint32_t i = 0; i < nparams && i < 16; i++) {
				params[i] = c->fn_params[i];
				gen_pnames[i] = c->fn_pnames[i];
				if (c->fn_pnames[i] > 1) {
					cxx_entity *e = cxx_declare(c, ls, c->fn_pnames[i], CXX_ENTITY_VALUE);

					if (e)
						e->ty = c->fn_pdecl[i];
				}
			}
		} else
			typed = true;	/* [] { ... }: no parameters */
	} else if (cxx_is(c, LEX_TOKEN_LEFT_PAREN)) {
		/* parameters: declare their names (for the body) */
		cxx_advance(c);
		while (!cxx_is(c, LEX_TOKEN_RIGHT_PAREN)) {
			uint32_t last_id = 0;
			int depth2 = 0;

			while (!(depth2 == 0 && (cxx_is(c, LEX_TOKEN_COMMA) || cxx_is(c, LEX_TOKEN_RIGHT_PAREN)))) {
				if (cxx_is(c, LEX_TOKEN_EOF))
					cxx_fail(c, "unterminated lambda parameters");
				if (cxx_is(c, LEX_TOKEN_LEFT_PAREN) || cxx_is(c, LEX_TOKEN_LEFT_SQUARE) || cxx_is(c, LEX_TOKEN_LEFT_BRACE) ||
				    cxx_is(c, LEX_TOKEN_LESS))
					depth2++;
				else if (cxx_is(c, LEX_TOKEN_RIGHT_PAREN) || cxx_is(c, LEX_TOKEN_RIGHT_SQUARE) ||
					 cxx_is(c, LEX_TOKEN_RIGHT_BRACE) || cxx_is(c, LEX_TOKEN_GREATER))
					depth2--;
				else if (cxx_is(c, LEX_TOKEN_GREATER_GREATER))
					depth2 -= 2;
				if (depth2 == 0 && cxx_is(c, LEX_TOKEN_IDENTIFIER) && !cxx_kw(c))
					last_id = cxx_peek(c, 0)->id;
				if (cxx_is(c, LEX_TOKEN_EQUAL) && depth2 == 0) {
					/* default argument: skip to , or ) */
					while (!(depth2 == 0 && (cxx_is(c, LEX_TOKEN_COMMA) || cxx_is(c, LEX_TOKEN_RIGHT_PAREN)))) {
						if (cxx_is(c, LEX_TOKEN_LEFT_PAREN) || cxx_is(c, LEX_TOKEN_LEFT_BRACE))
							depth2++;
						else if (cxx_is(c, LEX_TOKEN_RIGHT_PAREN) || cxx_is(c, LEX_TOKEN_RIGHT_BRACE))
							depth2--;
						cxx_advance(c);
					}
					break;
				}
				cxx_advance(c);
			}
			if (last_id)
				cxx_declare(c, ls, last_id, CXX_ENTITY_VALUE);
			if (!cxx_accept(c, LEX_TOKEN_COMMA))
				break;
		}
		cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
	}
	for (;;) {
		if (cxx_is_kw(c, CXX_KW_mutable) || cxx_is_kw(c, CXX_KW_constexpr) || cxx_is_kw(c, CXX_KW_consteval) ||
		    cxx_is_kw(c, CXX_KW_static)) {
			cxx_advance(c);
			continue;
		}
		if (cxx_is_kw(c, CXX_KW_noexcept)) {
			cxx_advance(c);
			if (cxx_is(c, LEX_TOKEN_LEFT_PAREN))
				cxx_skip_balanced(c);
			continue;
		}
		if ((cxx_is(c, LEX_TOKEN_LEFT_SQUARE) && cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_SQUARE) || cxx_is_kw(c, CXX_KW___attribute__)) {
			cxx_attributes_skip(c);
			continue;
		}
		break;
	}
	if (cxx_accept(c, LEX_TOKEN_ARROW)) {
		cxx_type_id_parse(c);
		ret = c->type_out;
		typed &= ret != CXX_TY_UNKNOWN;
	}
	cxx_requires_clause_parse(c);
	c->class_depth = 0;
	/* no trailing type: the body's first return (none: void); a lambda inside has its own */
	c->ret_capture = typed && ret == CXX_TY_UNKNOWN, c->ret_type = CXX_TY_UNKNOWN, c->ret_seen = 0;
	body_b = c->pos;
	if (want_gen)
		c->gen = 0;
	cxx_compound_statement_parse(c);
	if (want_gen)
		c->gen = was_gen;
	if (typed && ret == CXX_TY_UNKNOWN) {
		if (c->ret_type == CXX_TY_UNKNOWN && !c->ret_seen)
			ret = cc_type_basic(TY_VOID);
		else if (c->ret_type != CXX_TY_UNKNOWN)
			ret = AST_TYPE_KIND(c->ast, c->ret_type) == TY_VOID ? c->ret_type :
			      cxx_auto_deduce(c, cxx_auto_type(c), c->ret_type, c->ret_lval);
	}
	c->ret_capture = o_capture, c->ret_lval = o_lval, c->ret_seen = o_seen, c->ret_type = o_type;
	c->class_depth = depth;
	c->scope = outer;
	c->in_template_args = targs;
	if (typed && ret != CXX_TY_UNKNOWN) {
		/* the closure type: a class of its own with operator()(parameters) (its captures: not laid out) */
		/* (its name: $_N and a number of the TU's own, so that templates instantiated with it in two TUs differ) */
		uint32_t serial = c->lambda_serial++, lname = cxx_closure_name(c, serial), tag;
		cxx_entity *owner = calloc(1, sizeof *owner), *call;
		cxx_scope *cs;

		tag = cc_tag_new(c->ast, TY_STRUCT, lname, 0);
		owner->kind = CXX_ENTITY_TYPE, owner->name = lname, owner->pack_index = -1;
		owner->ty = QT(c->ast->tags[tag].ty, 0);
		cs = cxx_scope_new(c, CXX_SCOPE_CLASS, outer, owner);
		cs->is_lambda = 1;
		owner->members = cs;
		cxx_tag_scope_set(c, tag, cs);
		call = cxx_declare(c, cs, vintern_cstr("operator()"), CXX_ENTITY_VALUE);
		if (call) {
			call->ty = cxx_type_function(c, ret, params, nparams, false);
			call->fn_const = 1;
			call->member_of = cs;
			c->lambda_type = owner->ty;
		}
		if (call && want_gen && cxx_gen_on(c)) {
			/* code generation: the capture list, then the body again as the closure's operator() */
			cxx_gen_lambda l;
			cxx_param ps[16];
			cxx_scope *fs = cxx_scope_new(c, CXX_SCOPE_BLOCK, outer, NULL);
			uint32_t end = c->pos, bnode;
			bool ok = true;

			memset(&l, 0, sizeof l);
			for (uint32_t i = cap_b + 1; i + 1 < cap_e && ok; i++) {
				const cxx_token *x = &c->tok[i], *y = &c->tok[i + 1];
				bool last = i + 2 >= cap_e || y->kind == LEX_TOKEN_COMMA;

				if (x->kind == LEX_TOKEN_COMMA)
					continue;
				if (x->kind == LEX_TOKEN_AMPERSAND && last)
					l.def = 1;
				else if (x->kind == LEX_TOKEN_EQUAL && last)
					l.def = 2;
				else if (x->kind == LEX_TOKEN_IDENTIFIER && x->kw == CXX_KW_this && last)
					l.ex_this = 1;
				else if (x->kind == LEX_TOKEN_AMPERSAND && y->kind == LEX_TOKEN_IDENTIFIER && !y->kw && l.nex < 16 &&
					 (i + 3 >= cap_e || c->tok[i + 2].kind == LEX_TOKEN_COMMA))
					l.ex[l.nex].name = y->id, l.ex[l.nex++].by_ref = 1, i++;
				else if (x->kind == LEX_TOKEN_IDENTIFIER && !x->kw && last && l.nex < 16)
					l.ex[l.nex].name = x->id, l.ex[l.nex++].by_ref = 0;
				else
					ok = false;	/* (an init-capture, *this, a pack: not built yet) */
			}
			for (uint32_t i = 0; i < nparams && ok; i++) {
				ps[i].name = gen_pnames[i], ps[i].ty = params[i];
				if (gen_pnames[i] > 1) {
					cxx_entity *e = cxx_declare(c, fs, gen_pnames[i], CXX_ENTITY_VALUE);

					if (e)
						e->ty = params[i];
				}
			}
			if (ok && cxx_gen_lambda_symbol(c, call, serial)) {
				l.floor = c->ast->nsym;
				if (cxx_gen_function_begin(c, call, fs, ps, nparams)) {
					uint8_t g_capture = c->ret_capture, g_seen = c->ret_seen, g_lval = c->ret_lval;
					qty g_type = c->ret_type;

					c->gen_lambda = &l;
					c->pos = body_b;
					c->scope = fs;
					c->class_depth = 0, c->in_template_args = 0;
					c->ret_capture = 0;
					cxx_compound_statement_parse(c);
					bnode = c->snode;
					if (!cxx_gen_lambda_finish(c, &l, owner, &bnode, lloc))
						cxx_gen_fail(c);
					cxx_gen_function_end(c, call, bnode);
					c->ret_capture = g_capture, c->ret_seen = g_seen, c->ret_lval = g_lval, c->ret_type = g_type;
					c->class_depth = depth, c->in_template_args = targs;
					c->scope = outer;
					c->pos = end;
					c->lambda_node = 0;
					if (c->ast->syms[call->sym].flags & SF_DEFINED) {
						cxx_gen_lambda_object(c, &l, owner->ty, lloc);
						c->lambda_node = c->enode;
					}
					c->lambda_type = owner->ty;	/* (the body's lambdas, parsed again, left theirs) */
				}
			}
		}
	}
}
