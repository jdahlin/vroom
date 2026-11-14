/* C++ expressions: binary, conditional, assignment and constant expressions */
#include "expression.h"

VPRIV int cxx_binary_precedence(cxx *c, uint32_t *ntok)
{
	switch (cxx_operator_at(c, ntok)) {
	case CXX_TOKEN_PERIOD_STAR: case CXX_TOKEN_ARROW_STAR: return 14;
	case LEX_TOKEN_STAR: case LEX_TOKEN_SLASH: case LEX_TOKEN_PERCENT: return 13;
	case LEX_TOKEN_PLUS: case LEX_TOKEN_MINUS: return 12;
	case LEX_TOKEN_LESS_LESS: return 11;
	case LEX_TOKEN_GREATER_GREATER: return c->in_template_args ? 0 : 11;
	case CXX_TOKEN_SPACESHIP: return 10;
	case LEX_TOKEN_LESS: case LEX_TOKEN_LESS_EQUAL: return 9;
	case LEX_TOKEN_GREATER: return c->in_template_args ? 0 : 9;
	case LEX_TOKEN_GREATER_EQUAL: return 9;	/* written as one token: an operator even in template arguments */
	case LEX_TOKEN_EQUAL_EQUAL: case LEX_TOKEN_EXCLAMATION_EQUAL: return 8;
	case LEX_TOKEN_AMPERSAND: return 7;
	case LEX_TOKEN_CARET: return 6;
	case LEX_TOKEN_PIPE: return 5;
	case LEX_TOKEN_AMPERSAND_AMPERSAND: return 4;
	case LEX_TOKEN_PIPE_PIPE: return 3;
	}
	return 0;
}

VPRIV void cxx_binary_expression_parse(cxx *c, int min)
{
	cxx_cast_expression_parse(c);
	for (;;) {
		uint32_t n;
		int p = cxx_binary_precedence(c, &n);

		if (!p || p < min)
			return;
		/* a fold: e op ... (the caller, a parenthesized expression, takes it) */
		if (cxx_peek(c, n)->kind == LEX_TOKEN_ELLIPSIS)
			return;
		{
			qty l = c->etype;
			bool ll = c->elval;
			uint32_t ln = c->enode, oploc = cxx_peek(c, 0)->loc;
			bool built_in = false;
			uint32_t k;
			int op = cxx_operator_at(c, &k);
			uint32_t opid = cxx_operator_id_at(c, k);

			while (n--)
				cxx_advance(c);
			bool lk = c->econst, rk;
			int64_t lv = c->evalue, rv, v;

			c->gen_notemp += op == LEX_TOKEN_AMPERSAND_AMPERSAND || op == LEX_TOKEN_PIPE_PIPE;
			cxx_binary_expression_parse(c, p + 1);
			c->gen_notemp -= op == LEX_TOKEN_AMPERSAND_AMPERSAND || op == LEX_TOKEN_PIPE_PIPE;
			rk = c->econst, rv = c->evalue;
			qty rt = c->etype;
			uint32_t rn = c->enode;

			c->op_lv_set = 1, c->op_lv[0] = ll, c->op_lv[1] = c->elval;
			uint8_t r_lval = c->elval;
			bool rewritten = false;	/* a != b as !(a == b) (C++20's rewritten candidates) */

			bool found = !c->tentative && cxx_operator_call_type(c, opid, l, &c->etype, 1);

			if (!c->tentative && (!found || !c->call_resolved) && op == LEX_TOKEN_EXCLAMATION_EQUAL && l != CXX_TY_UNKNOWN &&
			    rt != CXX_TY_UNKNOWN && (cc_type_is_record(c->ast, l) || cc_type_is_record(c->ast, rt))) {
				/* no operator!= that fits: operator== (C++20 rewrites a != b as !(a == b)) */
				qty ra = rt;

				c->op_lv_set = 1, c->op_lv[0] = ll;
				if (cxx_operator_call_type(c, vintern_cstr("operator=="), l, &ra, 1) && c->call_resolved)
					rewritten = true, found = true;
			}
			/* no operator< (<=, >, >=) that fits, a class operand: (a <=> b) @ 0, or 0 @ (b <=> a) (C++20) */
			int spaceship = 0;	/* 1: a <=> b, 2: b <=> a */
			cxx_entity *ss_pick = NULL;

			if (!c->tentative && (!found || !c->call_resolved) && !rewritten && l != CXX_TY_UNKNOWN && rt != CXX_TY_UNKNOWN &&
			    (op == LEX_TOKEN_LESS || op == LEX_TOKEN_GREATER || op == LEX_TOKEN_LESS_EQUAL || op == LEX_TOKEN_GREATER_EQUAL) &&
			    (cc_type_is_record(c->ast, l) || cc_type_is_record(c->ast, rt))) {
				qty ra = rt, la = l;
				uint8_t lvl = ll, lvr = c->op_lv[1];

				c->op_lv_set = 1, c->op_lv[0] = lvl, c->op_lv[1] = lvr;
				if (cxx_operator_call_type(c, vintern_cstr("operator<=>"), l, &ra, 1) && c->call_resolved && c->call_picked)
					spaceship = 1, ss_pick = c->call_picked, found = true;
				else {
					c->op_lv_set = 1, c->op_lv[0] = lvr, c->op_lv[1] = lvl;
					if (cxx_operator_call_type(c, vintern_cstr("operator<=>"), rt, &la, 1) && c->call_resolved && c->call_picked)
						spaceship = 2, ss_pick = c->call_picked, found = true;
				}
			}
			/* a <=> b with no operator<=>(a, b): the reversed candidate, 0 <=> (b <=> a) ([over.match.oper]/3.4,
			   "text" <=> string) */
			bool ss_reversed = false;
			qty ss_type = CXX_TY_UNKNOWN;

			if (!c->tentative && (!found || !c->call_resolved) && op == CXX_TOKEN_SPACESHIP && l != CXX_TY_UNKNOWN && rt != CXX_TY_UNKNOWN &&
			    (cc_type_is_record(c->ast, l) || cc_type_is_record(c->ast, rt)) && !(cc_type_is_record(c->ast, l) && AST_TYPE_KIND(c->ast, l) != TY_ARRAY &&
												 cc_type_is_record(c->ast, rt) && QUNQ(l) == QUNQ(rt))) {
				qty la = l;

				c->op_lv_set = 1, c->op_lv[0] = r_lval, c->op_lv[1] = ll;
				if (cxx_operator_call_type(c, vintern_cstr("operator<=>"), rt, &la, 1) && c->call_resolved && c->call_picked &&
				    c->etype != CXX_TY_UNKNOWN)
					ss_reversed = true, ss_pick = c->call_picked, ss_type = c->etype, found = true;
			}
			if ((!found || (c->none_viable && !c->call_resolved)) && op != LEX_TOKEN_AMPERSAND_AMPERSAND && op != LEX_TOKEN_PIPE_PIPE && op != CXX_TOKEN_SPACESHIP &&
			    !c->template_depth && l != CXX_TY_UNKNOWN && rt != CXX_TY_UNKNOWN) {
				/* a class operand, no operator function that fits, and no conversion of the class to anything:
				   ill-formed (std::string < int: a substitution failure in decltype(a < b)) */
				for (int k = 0; k < 2; k++) {
					qty o = k ? rt : l;
					cxx_scope *os = cc_type_is_record(c->ast, o) && AST_TYPE_KIND(c->ast, o) != TY_ARRAY ? cxx_type_class_scope(c, QUNQ(o)) : NULL;

					if (os && !os->has_dependent_base && !os->dependent && !cxx_class_converts_public(c, os) &&
					    !(c->nullptr_ty && QUNQ(o) == c->nullptr_ty))
						CXX_INVALID(c);
				}
			}
			if (!c->tentative && !found)
				cxx_binary_type(c, op, l, rt), built_in = true;
			c->op_lv_set = 0;
			if (rewritten) {
				cxx_entity *eq = c->call_picked;

				cxx_et(c, cc_type_basic(TY_BOOL), false);	/* (first: it clears the node) */
				c->call_resolved = 1, c->call_picked = eq;
				if (eq) {
					cxx_gen_operator_call(c, ln, &rn, 1, oploc);
					if (c->enode)
						cxx_gen_unary(c, LEX_TOKEN_EXCLAMATION, c->enode, oploc);
				}
			} else if (spaceship) {
				cxx_et(c, cc_type_basic(TY_BOOL), false);
				c->call_resolved = 1, c->call_picked = ss_pick;
				if (spaceship == 1)
					cxx_gen_operator_call(c, ln, &rn, 1, oploc);
				else
					cxx_gen_operator_call(c, rn, &ln, 1, oploc);
				cxx_gen_ordering_test(c, c->enode, spaceship == 1 ? op : op == LEX_TOKEN_LESS ? LEX_TOKEN_GREATER :
						      op == LEX_TOKEN_GREATER ? LEX_TOKEN_LESS : op == LEX_TOKEN_LESS_EQUAL ? LEX_TOKEN_GREATER_EQUAL :
						      LEX_TOKEN_LESS_EQUAL, oploc);	/* (the type set above: setting it clears the node) */
			} else if (ss_reversed) {
				cxx_et(c, ss_type, false);
				c->call_resolved = 1, c->call_picked = ss_pick;
				cxx_gen_operator_call(c, rn, &ln, 1, oploc);
				cxx_gen_ordering_reverse(c, c->enode, ss_type, oploc);
			} else if (built_in && op == CXX_TOKEN_SPACESHIP)
				cxx_gen_spaceship(c, ln, rn, c->etype, oploc);
			else if (built_in)
				cxx_gen_binary(c, op, ln, rn, oploc);
			else if (!c->tentative && c->call_resolved && c->call_picked)
				cxx_gen_operator_call(c, ln, &rn, 1, oploc);
			else if (!c->tentative)
				cxx_gen_binary(c, op, ln, rn, oploc);	/* (no operator function: x && y through operator bool) */
			/* constants: both operands, or the left alone when it decides && and || */
			if (lk && rk) {
				bool uns = c->etype != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, c->etype) >= TY_CHAR &&
					   AST_TYPE_KIND(c->ast, c->etype) <= TY_UINT128 && !cc_type_is_signed(c->ast, c->etype);

				/* a comparison's result is bool: unsigned when the operands' common type is (an
				   unsigned operand of at least int's size, no narrower than the other) */
				if (op == LEX_TOKEN_LESS || op == LEX_TOKEN_GREATER || op == LEX_TOKEN_LESS_EQUAL || op == LEX_TOKEN_GREATER_EQUAL) {
					qty o[2] = { l, rt };

					uns = false;
					for (int i = 0; i < 2 && l != CXX_TY_UNKNOWN && rt != CXX_TY_UNKNOWN; i++) {
						const ctype *a = AST_TYPE(c->ast, o[i]), *b = AST_TYPE(c->ast, o[!i]);

						uns |= a->kind >= TY_CHAR && a->kind <= TY_UINT128 && !cc_type_is_signed(c->ast, o[i]) &&
						       a->size >= 4 && a->size >= b->size;
					}
				}

				if (cxx_binary_value(op, lv, rv, uns, &v))
					c->evalue = v, c->econst = 1;
			} else if (lk && ((op == LEX_TOKEN_AMPERSAND_AMPERSAND && !lv) || (op == LEX_TOKEN_PIPE_PIPE && lv)))
				c->evalue = lv != 0, c->econst = 1;
		}
	}
}

VPRIV void cxx_conditional_expression_parse(cxx *c)
{
	cxx_binary_expression_parse(c, 1);
	if (cxx_accept(c, LEX_TOKEN_QUESTION)) {
		uint32_t targs = c->in_template_args;

		qty t2 = c->etype;	/* (GNU ?: : the condition's) */
		bool l2 = c->elval;
		bool ck = c->econst, k2 = c->econst, k3;
		int64_t cv = c->evalue, v2 = c->evalue, v3;
		uint32_t cn = c->enode, n2 = 0, n3, qloc = c->tok[c->pos - 1].loc;

		c->in_template_args = 0;
		c->gen_notemp++;	/* (an operand that may not be evaluated: no temporaries with destructors) */
		if (!cxx_is(c, LEX_TOKEN_COLON))	/* GNU ?: */
			cxx_expression_parse(c), t2 = c->etype, l2 = c->elval, k2 = c->econst, v2 = c->evalue, n2 = c->enode;
		c->in_template_args = targs;
		cxx_expect(c, LEX_TOKEN_COLON);
		cxx_assignment_expression_parse(c);
		c->gen_notemp--;
		k3 = c->econst, v3 = c->evalue;
		n3 = c->enode;
		{
			qty t3 = c->etype;

			if (t2 != CXX_TY_UNKNOWN && t2 == t3)
				cxx_et(c, t2, l2 && c->elval);
			else if (t2 != CXX_TY_UNKNOWN && t3 != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, t2) && QUNQ(t2) == QUNQ(t3) &&
				 AST_TYPE_KIND(c->ast, t2) != TY_ARRAY)
				cxx_et(c, QT(QI(t2), QQ(t2) | QQ(t3)), l2 && c->elval);	/* (one class, cv as the more qualified) */
			else if (t2 != CXX_TY_UNKNOWN && t3 != CXX_TY_UNKNOWN && cc_type_is_arithmetic(c->ast, t2) && cc_type_is_arithmetic(c->ast, t3))
				cxx_et(c, cc_type_arithmetic_conversion(c->ast, QUNQ(t2), QUNQ(t3)), false);
			else if (t2 != CXX_TY_UNKNOWN && t3 != CXX_TY_UNKNOWN && c->nullptr_ty &&
				 ((AST_TYPE_KIND(c->ast, t2) == TY_PTR && !AST_TYPE(c->ast, t2)->ext && QUNQ(t3) == c->nullptr_ty) ||
				  (AST_TYPE_KIND(c->ast, t3) == TY_PTR && !AST_TYPE(c->ast, t3)->ext && QUNQ(t2) == c->nullptr_ty))) {
				/* a pointer and nullptr: the pointer's type ([expr.cond]/7.3) */
				qty pt = QUNQ(QUNQ(t3) == c->nullptr_ty ? t2 : t3);

				cxx_et(c, pt, false);
				if (cxx_gen_on(c) && n2 && n3) {
					if (QUNQ(t2) == c->nullptr_ty)
						n2 = cc_conversion_apply(c->ast, cc_conversion_decay(c->ast, n2), pt);
					else
						n3 = cc_conversion_apply(c->ast, cc_conversion_decay(c->ast, n3), pt);
				}
			} else if (t2 != CXX_TY_UNKNOWN && t3 != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, t2) == TY_PTR && AST_TYPE_KIND(c->ast, t3) == TY_PTR &&
				   !AST_TYPE(c->ast, t2)->ext && !AST_TYPE(c->ast, t3)->ext &&
				   QUNQ(AST_TYPE(c->ast, t2)->base) == QUNQ(AST_TYPE(c->ast, t3)->base)) {
				/* T * and const T *: the more qualified (the composite pointer type, [expr.type]/4) */
				qty b2 = AST_TYPE(c->ast, t2)->base, b3 = AST_TYPE(c->ast, t3)->base;

				cxx_et(c, cc_type_pointer(c->ast, QT(QI(b2), QQ(b2) | QQ(b3))), false);
			} else
				cxx_et(c, CXX_TY_UNKNOWN, false);
			/* (built as a value: where the result is used as an lvalue, a ? x : y = 1, the assignment's
			   builder finds no lvalue and the function gets no code) */
			if (c->etype != CXX_TY_UNKNOWN && (cc_type_is_arithmetic(c->ast, c->etype) || AST_TYPE_KIND(c->ast, c->etype) == TY_PTR ||
							   AST_TYPE_KIND(c->ast, c->etype) == TY_VOID ||	/* (c ? void(0) : f(): assert) */
							   cc_type_is_record(c->ast, c->etype)))	/* (of one class: cxx_gen_conditional checks) */
				cxx_gen_conditional(c, cn, n2, n3, qloc);
			if (ck && (cv ? k2 : k3))
				c->evalue = cv ? v2 : v3, c->econst = 1;	/* a constant condition: the branch taken */
		}
	}
}

VPRIV bool cxx_is_assignment_operator(cxx *c, uint32_t *ntok)
{
	switch (cxx_operator_at(c, ntok)) {
	case LEX_TOKEN_EQUAL: case LEX_TOKEN_STAR_EQUAL: case LEX_TOKEN_SLASH_EQUAL: case LEX_TOKEN_PERCENT_EQUAL:
	case LEX_TOKEN_PLUS_EQUAL: case LEX_TOKEN_MINUS_EQUAL: case LEX_TOKEN_LESS_LESS_EQUAL: case LEX_TOKEN_AMPERSAND_EQUAL:
	case LEX_TOKEN_CARET_EQUAL: case LEX_TOKEN_PIPE_EQUAL:
		return true;
	case LEX_TOKEN_GREATER_GREATER_EQUAL:
		return !c->in_template_args;
	}
	return false;
}

VPRIV void cxx_assignment_expression_parse(cxx *c)
{
	if (cxx_is_kw(c, CXX_KW_throw)) {
		uint32_t tloc = cxx_peek(c, 0)->loc, x = 0;
		bool operand = false;

		cxx_advance(c);
		if (!cxx_is(c, LEX_TOKEN_SEMICOLON) && !cxx_is(c, LEX_TOKEN_RIGHT_PAREN) && !cxx_is(c, LEX_TOKEN_COMMA) &&
		    !cxx_is(c, LEX_TOKEN_COLON) && !cxx_is(c, LEX_TOKEN_RIGHT_BRACE)) {
			cxx_assignment_expression_parse(c);
			operand = true, x = c->enode;
		}
		cxx_et(c, cc_type_basic(TY_VOID), false);
		cxx_gen_throw(c, x, operand, tloc);
		return;
	}
	if (cxx_is_kw(c, CXX_KW_co_yield)) {
		cxx_advance(c);
		cxx_initializer_parse(c);
		return;
	}
	cxx_conditional_expression_parse(c);
	uint32_t n;

	if (cxx_is_assignment_operator(c, &n)) {
		qty l = c->etype;
		bool built_in = l != CXX_TY_UNKNOWN && (cc_type_is_arithmetic(c->ast, l) || AST_TYPE_KIND(c->ast, l) == TY_PTR);
		uint32_t ln = c->enode, oploc = cxx_peek(c, 0)->loc, ntok = n;
		int optok = cxx_operator_at(c, &ntok);

		uint32_t opid = cxx_operator_id_at(c, n);

		if (cxx_peek(c, n)->kind == LEX_TOKEN_ELLIPSIS)
			return;	/* a fold over = */
		while (n--)
			cxx_advance(c);
		cxx_initializer_parse(c);
		{
			uint32_t rn = c->enode;

			if (built_in || c->tentative || !cxx_operator_call_type(c, opid, l, &c->etype, 1))
				cxx_et(c, built_in ? l : CXX_TY_UNKNOWN, built_in);
			if (built_in)
				cxx_gen_assignment(c, optok, ln, rn, oploc);
			else if (!c->tentative && c->call_resolved && c->call_picked)
				cxx_gen_operator_call(c, ln, &rn, 1, oploc);
			else if (!c->tentative)
				cxx_gen_assignment(c, optok, ln, rn, oploc);	/* (no operator= declared: the implicit one, a copy of the bytes where that is what it does) */
		}
	}
}

VPRIV void cxx_expression_parse(cxx *c)
{
	cxx_assignment_expression_parse(c);
	while (cxx_is(c, LEX_TOKEN_COMMA) && !c->in_template_args) {
		if (cxx_peek(c, 1)->kind == LEX_TOKEN_ELLIPSIS)
			return;	/* fold over , */
		/* a, b: b's type whatever a is; whether a is valid is then not known (decltype(f(x), void())) */
		if (c->etype == CXX_TY_UNKNOWN && !c->tentative && !c->template_depth)
			c->eunsure = 1;
		uint32_t ln = c->enode, cloc = cxx_peek(c, 0)->loc;
		qty lt = c->etype;
		uint8_t ll = c->elval;

		cxx_advance(c);
		cxx_assignment_expression_parse(c);
		if (!c->tentative && lt != CXX_TY_UNKNOWN && c->etype != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, c->etype) != TY_VOID &&
		    AST_TYPE_KIND(c->ast, lt) != TY_VOID && (cc_type_is_record(c->ast, lt) || cc_type_is_record(c->ast, c->etype) || AST_TYPE_KIND(c->ast, lt) == TY_ENUM ||
		     AST_TYPE_KIND(c->ast, c->etype) == TY_ENUM)) {
			/* a, b with a class (or enumeration) operand: an operator, that fits is called (doctest's
			   MessageBuilder) -- else the built-in comma */
			qty rt = c->etype, ra = rt;
			uint8_t rl = c->elval;
			uint32_t rn = c->enode;
			bool found;

			c->op_lv_set = 1, c->op_lv[0] = ll;
			found = cxx_operator_call_type(c, vintern_cstr("operator,"), lt, &ra, 1);
			c->op_lv_set = 0;
			if (found && !c->call_resolved && c->etype == CXX_TY_UNKNOWN)
				found = false;	/* (no operator, that vcc can pick: the built-in comma, b's type) */
			if (found && !(c->none_viable && !c->call_resolved)) {
				if (c->call_resolved && c->call_picked)
					cxx_gen_operator_call(c, ln, &rn, 1, cloc);
				else
					c->enode = 0;
				continue;
			}
			cxx_et(c, rt, rl);
			c->enode = rn;
		}
		if (c->etype != CXX_TY_UNKNOWN && !cc_type_is_record(c->ast, c->etype)) {
			uint32_t rn = c->enode;

			c->enode = 0;
			cxx_gen_comma(c, ln, rn, cloc);
		} else
			c->enode = 0;
	}
}

VPRIV void cxx_constant_expression_parse(cxx *c)
{
	cxx_conditional_expression_parse(c);
}
