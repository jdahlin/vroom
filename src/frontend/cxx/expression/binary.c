/* C++ expressions: binary, conditional, assignment and constant expressions */
#include "../codegen/codegen.h"
#include "expression.h"

VPRIV int cxx_binary_precedence(cxx *c, uint32_t *ntok)
{
    switch (cxx_operator_at(c, ntok)) {
    case CXX_TOKEN_PERIOD_STAR:
    case CXX_TOKEN_ARROW_STAR:
        return 14;
    case LEX_TOKEN_STAR:
    case LEX_TOKEN_SLASH:
    case LEX_TOKEN_PERCENT:
        return 13;
    case LEX_TOKEN_PLUS:
    case LEX_TOKEN_MINUS:
        return 12;
    case LEX_TOKEN_LESS_LESS:
        return 11;
    case LEX_TOKEN_GREATER_GREATER:
        return c->in_template_args ? 0 : 11;
    case CXX_TOKEN_SPACESHIP:
        return 10;
    case LEX_TOKEN_LESS:
    case LEX_TOKEN_LESS_EQUAL:
        return 9;
    case LEX_TOKEN_GREATER:
        return c->in_template_args ? 0 : 9;
    case LEX_TOKEN_GREATER_EQUAL:
        return 9; /* written as one token: an operator even in template arguments */
    case LEX_TOKEN_EQUAL_EQUAL:
    case LEX_TOKEN_EXCLAMATION_EQUAL:
        return 8;
    case LEX_TOKEN_AMPERSAND:
        return 7;
    case LEX_TOKEN_CARET:
        return 6;
    case LEX_TOKEN_PIPE:
        return 5;
    case LEX_TOKEN_AMPERSAND_AMPERSAND:
        return 4;
    case LEX_TOKEN_PIPE_PIPE:
        return 3;
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
            ast_qualified_type l = c->etype;
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

            c->gen_notemp += op == LEX_TOKEN_AMPERSAND_AMPERSAND ||
                             op == LEX_TOKEN_PIPE_PIPE;
            uint32_t rstart = c->pos;

            c->fn_set_seen = NULL;
            cxx_binary_expression_parse(c, p + 1);
            c->gen_notemp -= op == LEX_TOKEN_AMPERSAND_AMPERSAND ||
                             op == LEX_TOKEN_PIPE_PIPE;
            rk = c->econst, rv = c->evalue;
            ast_qualified_type rt = c->etype;
            uint32_t rn = c->enode;

            c->op_lv_set = 1, c->op_lv[0] = ll, c->op_lv[1] = c->elval;
            uint8_t r_lval = c->elval;
            bool rewritten =
                false; /* a != b as !(a == b) (C++20's rewritten candidates) */

            if ((op == CXX_TOKEN_PERIOD_STAR || op == CXX_TOKEN_ARROW_STAR) &&
                cxx_memptr_of(c, rt) && l != CXX_TY_UNKNOWN &&
                (op == CXX_TOKEN_PERIOD_STAR
                     ? cc_type_is_record(c->ast, l)
                     : AST_TYPE_KIND(c->ast, l) == AST_TYPE_POINTER &&
                           !AST_TYPE(c->ast, l)->ext)) {
                /* x.*mp, p->*mp built in: a data member (an lvalue of x's, always of *p's), or a member function to call */
                const cxx_memptr *mp = cxx_memptr_of(c, rt);
                ast_qualified_type ot =
                    op == CXX_TOKEN_PERIOD_STAR ? l : AST_TYPE(c->ast, l)->base;
                uint8_t oq = AST_QUALIFIED_TYPE_QUALIFIERS(ot) &
                             (AST_QUALIFIER_CONST | AST_QUALIFIER_VOLATILE);

                c->op_lv_set = 0;
                if (mp->fn)
                    cxx_et(c, mp->mem, false);
                else
                    cxx_et(c,
                           AST_QUALIFIED_TYPE(
                               AST_QUALIFIED_TYPE_INDEX(mp->mem),
                               AST_QUALIFIED_TYPE_QUALIFIERS(mp->mem) | oq),
                           op == CXX_TOKEN_ARROW_STAR || ll);
                if (!c->tentative && cxx_gen_on(c)) {
                    c->enode = cxx_gen_memptr_apply(
                        c, ln, op == CXX_TOKEN_ARROW_STAR, rn, rt, oploc);
                    if (!c->enode)
                        cxx_gen_fail(c);
                }
                continue;
            }
            if ((op == LEX_TOKEN_EQUAL_EQUAL ||
                 op == LEX_TOKEN_EXCLAMATION_EQUAL) &&
                (cxx_memptr_of(c, l) || cxx_memptr_of(c, rt))) {
                /* pointers to members compared (with each other, or with the null one: 0, nullptr) */
                bool lm = cxx_memptr_of(c, l) != NULL;

                c->op_lv_set = 0;
                cxx_et(c, cc_type_basic(AST_TYPE_BOOL), false);
                if (!c->tentative && cxx_gen_on(c)) {
                    c->enode = cxx_gen_memptr_compare(
                        c, lm ? ln : rn, lm ? rn : ln,
                        op == LEX_TOKEN_EQUAL_EQUAL, oploc);
                    if (!c->enode)
                        cxx_gen_fail(c);
                }
                continue;
            }
            cxx_entity *r_fnset = cxx_fnset_of_expression(
                c, rstart); /* (os << endl: a set of functions) */

            c->op_fnset = r_fnset;
            bool found = !c->tentative &&
                         cxx_operator_call_type(c, opid, l, &c->etype, 1);

            c->op_fnset = NULL;
            if (r_fnset && found && c->call_resolved && c->call_picked && !rn &&
                !c->tentative) {
                /* its argument: the function of the parameter's type (a member's first, a non-member's second) */
                cxx_entity *pk = c->call_picked;
                const ctype *pft =
                    pk->kind == CXX_ENTITY_VALUE && pk->ty != CXX_TY_UNKNOWN &&
                            AST_TYPE_KIND(c->ast, pk->ty) == AST_TYPE_FUNCTION
                        ? AST_TYPE(c->ast, pk->ty)
                        : NULL;
                uint32_t pi = pk->member_of && !pk->fn_static ? 0 : 1;
                ast_qualified_type ety = c->etype;
                uint8_t elv = c->elval;

                if (pft && pi < pft->n)
                    rn = cxx_fnset_node(c, r_fnset,
                                        c->ast->params[pft->aux + pi], oploc);
                c->etype = ety, c->elval = elv;
            }

            if (!c->tentative && (!found || !c->call_resolved) &&
                op == LEX_TOKEN_EXCLAMATION_EQUAL && l != CXX_TY_UNKNOWN &&
                rt != CXX_TY_UNKNOWN &&
                (cc_type_is_record(c->ast, l) ||
                 cc_type_is_record(c->ast, rt))) {
                /* no operator!= that fits: operator== (C++20 rewrites a != b as !(a == b)) */
                ast_qualified_type ra = rt;

                c->op_lv_set = 1, c->op_lv[0] = ll;
                if (cxx_operator_call_type(c, vintern_cstr("operator=="), l,
                                           &ra, 1) &&
                    c->call_resolved)
                    rewritten = true, found = true;
            }
            /* a == b, a != b with no operator that fits: the reversed candidate b == a, !(b == a) ([over.match.oper]
               /3.4.4, C++20: 1 == j with only json's member operator==(ScalarType) const) */
            int eq_reversed = 0; /* 1: b == a, 2: !(b == a) */
            cxx_entity *eq_pick = NULL;

            if (!c->tentative && (!found || !c->call_resolved) && !rewritten &&
                c->o->standard >= 20 && c->o->standard != 98 &&
                (op == LEX_TOKEN_EQUAL_EQUAL ||
                 op == LEX_TOKEN_EXCLAMATION_EQUAL) &&
                l != CXX_TY_UNKNOWN && rt != CXX_TY_UNKNOWN &&
                cc_type_is_record(c->ast, rt) &&
                AST_TYPE_KIND(c->ast, rt) != AST_TYPE_ARRAY &&
                !(cc_type_is_record(c->ast, l) &&
                  AST_QUALIFIED_TYPE_UNQUALIFIED(l) ==
                      AST_QUALIFIED_TYPE_UNQUALIFIED(rt))) {
                ast_qualified_type la = l;
                ast_qualified_type et = c->etype;

                c->op_lv_set = 1, c->op_lv[0] = r_lval, c->op_lv[1] = ll;
                if (cxx_operator_call_type(c, vintern_cstr("operator=="), rt,
                                           &la, 1) &&
                    c->call_resolved && c->call_picked)
                    eq_reversed = op == LEX_TOKEN_EQUAL_EQUAL ? 1 : 2,
                    eq_pick = c->call_picked, found = true;
                else
                    c->etype = et;
            }
            /* no operator< (<=, >, >=) that fits, a class operand: (a <=> b) @ 0, or 0 @ (b <=> a) (C++20) */
            int spaceship = 0; /* 1: a <=> b, 2: b <=> a */
            cxx_entity *ss_pick = NULL;

            if (!c->tentative && (!found || !c->call_resolved) && !rewritten &&
                l != CXX_TY_UNKNOWN && rt != CXX_TY_UNKNOWN &&
                (op == LEX_TOKEN_LESS || op == LEX_TOKEN_GREATER ||
                 op == LEX_TOKEN_LESS_EQUAL || op == LEX_TOKEN_GREATER_EQUAL) &&
                (cc_type_is_record(c->ast, l) ||
                 cc_type_is_record(c->ast, rt))) {
                ast_qualified_type ra = rt, la = l;
                uint8_t lvl = ll, lvr = c->op_lv[1];

                c->op_lv_set = 1, c->op_lv[0] = lvl, c->op_lv[1] = lvr;
                if (cxx_operator_call_type(c, vintern_cstr("operator<=>"), l,
                                           &ra, 1) &&
                    c->call_resolved && c->call_picked)
                    spaceship = 1, ss_pick = c->call_picked, found = true;
                else {
                    c->op_lv_set = 1, c->op_lv[0] = lvr, c->op_lv[1] = lvl;
                    if (cxx_operator_call_type(c, vintern_cstr("operator<=>"),
                                               rt, &la, 1) &&
                        c->call_resolved && c->call_picked)
                        spaceship = 2, ss_pick = c->call_picked, found = true;
                }
            }
            /* a <=> b with no operator<=>(a, b): the reversed candidate, 0 <=> (b <=> a) ([over.match.oper]/3.4,
			   "text" <=> string) */
            bool ss_reversed = false;
            ast_qualified_type ss_type = CXX_TY_UNKNOWN;

            if (!c->tentative && (!found || !c->call_resolved) &&
                op == CXX_TOKEN_SPACESHIP && l != CXX_TY_UNKNOWN &&
                rt != CXX_TY_UNKNOWN &&
                (cc_type_is_record(c->ast, l) ||
                 cc_type_is_record(c->ast, rt)) &&
                !(cc_type_is_record(c->ast, l) &&
                  AST_TYPE_KIND(c->ast, l) != AST_TYPE_ARRAY &&
                  cc_type_is_record(c->ast, rt) &&
                  AST_QUALIFIED_TYPE_UNQUALIFIED(l) ==
                      AST_QUALIFIED_TYPE_UNQUALIFIED(rt))) {
                ast_qualified_type la = l;

                c->op_lv_set = 1, c->op_lv[0] = r_lval, c->op_lv[1] = ll;
                if (cxx_operator_call_type(c, vintern_cstr("operator<=>"), rt,
                                           &la, 1) &&
                    c->call_resolved && c->call_picked &&
                    c->etype != CXX_TY_UNKNOWN)
                    ss_reversed = true, ss_pick = c->call_picked,
                    ss_type = c->etype, found = true;
            }
            if ((!found || (c->none_viable && !c->call_resolved)) &&
                op != LEX_TOKEN_AMPERSAND_AMPERSAND &&
                op != LEX_TOKEN_PIPE_PIPE && op != CXX_TOKEN_SPACESHIP &&
                !c->template_depth && l != CXX_TY_UNKNOWN &&
                rt != CXX_TY_UNKNOWN) {
                /* a class operand, no operator function that fits, and no conversion of the class to anything:
				   ill-formed (std::string < int: a substitution failure in decltype(a < b)) */
                for (int k = 0; k < 2; k++) {
                    ast_qualified_type o = k ? rt : l;
                    cxx_scope *os =
                        cc_type_is_record(c->ast, o) &&
                                AST_TYPE_KIND(c->ast, o) != AST_TYPE_ARRAY
                            ? cxx_type_class_scope(
                                  c, AST_QUALIFIED_TYPE_UNQUALIFIED(o))
                            : NULL;

                    if (os && !os->has_dependent_base && !os->dependent &&
                        !cxx_class_converts_public(c, os) &&
                        !(c->nullptr_ty &&
                          AST_QUALIFIED_TYPE_UNQUALIFIED(o) == c->nullptr_ty))
                        CXX_INVALID(c);
                }
            }
            if (!c->tentative &&
                (!found || (!c->call_resolved && !c->call_picked &&
                            c->etype == CXX_TY_UNKNOWN)) &&
                !rewritten && !spaceship && !ss_reversed && !eq_reversed &&
                op != LEX_TOKEN_AMPERSAND_AMPERSAND &&
                op != LEX_TOKEN_PIPE_PIPE) {
                /* a class operand of a built-in operator: its conversion function to a scalar, then the
				   operator ([over.match.oper]/7: atomic<int> == 4) */
                ast_qualified_type cl = cxx_class_scalar_conversion(c, l),
                                   cr = cxx_class_scalar_conversion(c, rt);

                if (cl != CXX_TY_UNKNOWN) {
                    l = cl;
                    if (ln)
                        ln = cxx_gen_convert_class(c, ln, cl, oploc);
                }
                if (cr != CXX_TY_UNKNOWN) {
                    rt = cr;
                    if (rn)
                        rn = cxx_gen_convert_class(c, rn, cr, oploc);
                }
                if (cl != CXX_TY_UNKNOWN || cr != CXX_TY_UNKNOWN)
                    found = false;
            }
            if (!c->tentative && !found)
                cxx_binary_type(c, op, l, rt), built_in = true;
            c->op_lv_set = 0;
            if (rewritten) {
                cxx_entity *eq = c->call_picked;

                cxx_et(c, cc_type_basic(AST_TYPE_BOOL),
                       false); /* (first: it clears the node) */
                c->call_resolved = 1, c->call_picked = eq;
                if (eq) {
                    cxx_gen_operator_call(c, ln, &rn, 1, oploc);
                    if (c->enode)
                        cxx_gen_unary(c, LEX_TOKEN_EXCLAMATION, c->enode,
                                      oploc);
                }
            } else if (eq_reversed) {
                cxx_et(c, cc_type_basic(AST_TYPE_BOOL), false);
                c->call_resolved = 1, c->call_picked = eq_pick;
                cxx_gen_operator_call(c, rn, &ln, 1, oploc);
                if (eq_reversed == 2 && c->enode)
                    cxx_gen_unary(c, LEX_TOKEN_EXCLAMATION, c->enode, oploc);
            } else if (spaceship) {
                cxx_et(c, cc_type_basic(AST_TYPE_BOOL), false);
                c->call_resolved = 1, c->call_picked = ss_pick;
                if (spaceship == 1)
                    cxx_gen_operator_call(c, ln, &rn, 1, oploc);
                else
                    cxx_gen_operator_call(c, rn, &ln, 1, oploc);
                cxx_gen_ordering_test(
                    c, c->enode,
                    spaceship == 1               ? op
                    : op == LEX_TOKEN_LESS       ? LEX_TOKEN_GREATER
                    : op == LEX_TOKEN_GREATER    ? LEX_TOKEN_LESS
                    : op == LEX_TOKEN_LESS_EQUAL ? LEX_TOKEN_GREATER_EQUAL
                                                 : LEX_TOKEN_LESS_EQUAL,
                    oploc); /* (the type set above: setting it clears the node) */
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
                cxx_gen_binary(
                    c, op, ln, rn,
                    oploc); /* (no operator function: x && y through operator bool) */
            /* constants: both operands, or the left alone when it decides && and || */
            if (lk && rk) {
                bool uns =
                    c->etype != CXX_TY_UNKNOWN &&
                    AST_TYPE_KIND(c->ast, c->etype) >= AST_TYPE_CHAR &&
                    AST_TYPE_KIND(c->ast, c->etype) <= AST_TYPE_UINT128 &&
                    !cc_type_is_signed(c->ast, c->etype);

                /* a comparison's result is bool: unsigned when the operands' common type is (an
				   unsigned operand of at least int's size, no narrower than the other) */
                if (op == LEX_TOKEN_LESS || op == LEX_TOKEN_GREATER ||
                    op == LEX_TOKEN_LESS_EQUAL ||
                    op == LEX_TOKEN_GREATER_EQUAL) {
                    ast_qualified_type o[2] = {l, rt};

                    uns = false;
                    for (int i = 0;
                         i < 2 && l != CXX_TY_UNKNOWN && rt != CXX_TY_UNKNOWN;
                         i++) {
                        const ctype *a = AST_TYPE(c->ast, o[i]),
                                    *b = AST_TYPE(c->ast, o[!i]);

                        uns |= a->kind >= AST_TYPE_CHAR &&
                               a->kind <= AST_TYPE_UINT128 &&
                               !cc_type_is_signed(c->ast, o[i]) &&
                               a->size >= 4 && a->size >= b->size;
                    }
                }

                if (cxx_binary_value(op, lv, rv, uns, &v))
                    c->evalue = v, c->econst = 1;
            } else if (lk && ((op == LEX_TOKEN_AMPERSAND_AMPERSAND && !lv) ||
                              (op == LEX_TOKEN_PIPE_PIPE && lv)))
                c->evalue = lv != 0, c->econst = 1;
        }
    }
}

VPRIV void cxx_conditional_expression_parse(cxx *c)
{
    cxx_binary_expression_parse(c, 1);
    if (cxx_accept(c, LEX_TOKEN_QUESTION)) {
        uint32_t targs = c->in_template_args;

        ast_qualified_type t2 = c->etype; /* (GNU ?: : the condition's) */
        bool l2 = c->elval;
        bool ck = c->econst, k2 = c->econst, k3;
        int64_t cv = c->evalue, v2 = c->evalue, v3;
        uint32_t cn = c->enode, n2 = 0, n3, qloc = c->tok[c->pos - 1].loc;

        c->in_template_args = 0;
        c->gen_notemp++; /* (an operand that may not be evaluated: no temporaries with destructors) */
        if (!cxx_is(c, LEX_TOKEN_COLON)) /* GNU ?: */
            cxx_expression_parse(c), t2 = c->etype, l2 = c->elval,
                                     k2 = c->econst, v2 = c->evalue,
                                     n2 = c->enode;
        c->in_template_args = targs;
        cxx_expect(c, LEX_TOKEN_COLON);
        cxx_assignment_expression_parse(c);
        c->gen_notemp--;
        k3 = c->econst, v3 = c->evalue;
        n3 = c->enode;
        {
            ast_qualified_type t3 = c->etype;

            /* arrays of different types (c ? "true" : "false"): their pointers, prvalues ([expr.cond]/7) */
            if (t2 != CXX_TY_UNKNOWN && t3 != CXX_TY_UNKNOWN && t2 != t3 &&
                (AST_TYPE_KIND(c->ast, t2) == AST_TYPE_ARRAY ||
                 AST_TYPE_KIND(c->ast, t3) == AST_TYPE_ARRAY)) {
                if (AST_TYPE_KIND(c->ast, t2) == AST_TYPE_ARRAY)
                    t2 = cc_type_pointer(c->ast, AST_TYPE(c->ast, t2)->base),
                    l2 = false;
                if (AST_TYPE_KIND(c->ast, t3) == AST_TYPE_ARRAY)
                    t3 = cc_type_pointer(c->ast, AST_TYPE(c->ast, t3)->base),
                    c->elval = 0;
                if (cxx_gen_on(c) && n2 && n3)
                    n2 = cc_conversion_decay(c->ast, n2),
                    n3 = cc_conversion_decay(c->ast, n3);
            }
            if (t2 != CXX_TY_UNKNOWN && t2 == t3)
                cxx_et(c, t2, l2 && c->elval);
            else if (t2 != CXX_TY_UNKNOWN && t3 != CXX_TY_UNKNOWN &&
                     cc_type_is_record(c->ast, t2) &&
                     AST_QUALIFIED_TYPE_UNQUALIFIED(t2) ==
                         AST_QUALIFIED_TYPE_UNQUALIFIED(t3) &&
                     AST_TYPE_KIND(c->ast, t2) != AST_TYPE_ARRAY)
                cxx_et(
                    c,
                    AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(t2),
                                       AST_QUALIFIED_TYPE_QUALIFIERS(t2) |
                                           AST_QUALIFIED_TYPE_QUALIFIERS(t3)),
                    l2 && c->elval); /* (one class, cv as the more qualified) */
            else if (t2 != CXX_TY_UNKNOWN && t3 != CXX_TY_UNKNOWN &&
                     (cc_type_is_arithmetic(c->ast, t2) ||
                      cxx_type_is_complex(c, t2)) &&
                     (cc_type_is_arithmetic(c->ast, t3) ||
                      cxx_type_is_complex(
                          c, t3))) /* (_Complex too: C's conversions) */
                cxx_et(c,
                       cc_type_arithmetic_conversion(
                           c->ast, AST_QUALIFIED_TYPE_UNQUALIFIED(t2),
                           AST_QUALIFIED_TYPE_UNQUALIFIED(t3)),
                       false);
            else if (t2 != CXX_TY_UNKNOWN && t3 != CXX_TY_UNKNOWN &&
                     c->nullptr_ty &&
                     ((AST_TYPE_KIND(c->ast, t2) == AST_TYPE_POINTER &&
                       !AST_TYPE(c->ast, t2)->ext &&
                       AST_QUALIFIED_TYPE_UNQUALIFIED(t3) == c->nullptr_ty) ||
                      (AST_TYPE_KIND(c->ast, t3) == AST_TYPE_POINTER &&
                       !AST_TYPE(c->ast, t3)->ext &&
                       AST_QUALIFIED_TYPE_UNQUALIFIED(t2) == c->nullptr_ty))) {
                /* a pointer and nullptr: the pointer's type ([expr.cond]/7.3) */
                ast_qualified_type pt = AST_QUALIFIED_TYPE_UNQUALIFIED(
                    AST_QUALIFIED_TYPE_UNQUALIFIED(t3) == c->nullptr_ty ? t2
                                                                        : t3);

                cxx_et(c, pt, false);
                if (cxx_gen_on(c) && n2 && n3) {
                    if (AST_QUALIFIED_TYPE_UNQUALIFIED(t2) == c->nullptr_ty)
                        n2 = cc_conversion_apply(
                            c->ast, cc_conversion_decay(c->ast, n2), pt);
                    else
                        n3 = cc_conversion_apply(
                            c->ast, cc_conversion_decay(c->ast, n3), pt);
                }
            } else if (t2 != CXX_TY_UNKNOWN && t3 != CXX_TY_UNKNOWN &&
                       AST_TYPE_KIND(c->ast, t2) == AST_TYPE_POINTER &&
                       AST_TYPE_KIND(c->ast, t3) == AST_TYPE_POINTER &&
                       !AST_TYPE(c->ast, t2)->ext &&
                       !AST_TYPE(c->ast, t3)->ext &&
                       AST_QUALIFIED_TYPE_UNQUALIFIED(
                           AST_TYPE(c->ast, t2)->base) ==
                           AST_QUALIFIED_TYPE_UNQUALIFIED(
                               AST_TYPE(c->ast, t3)->base)) {
                /* T * and const T *: the more qualified (the composite pointer type, [expr.type]/4) */
                ast_qualified_type b2 = AST_TYPE(c->ast, t2)->base,
                                   b3 = AST_TYPE(c->ast, t3)->base;

                cxx_et(c,
                       cc_type_pointer(
                           c->ast, AST_QUALIFIED_TYPE(
                                       AST_QUALIFIED_TYPE_INDEX(b2),
                                       AST_QUALIFIED_TYPE_QUALIFIERS(b2) |
                                           AST_QUALIFIED_TYPE_QUALIFIERS(b3))),
                       false);
            } else if (t2 != CXX_TY_UNKNOWN && t3 != CXX_TY_UNKNOWN &&
                       !c->tentative && !c->template_depth &&
                       (cc_type_is_record(c->ast, t2) !=
                        cc_type_is_record(c->ast, t3)) &&
                       AST_TYPE_KIND(c->ast,
                                     cc_type_is_record(c->ast, t2) ? t2 : t3) !=
                           AST_TYPE_ARRAY) {
                /* a class and another type that converts to it by a constructor (c ? std::string(...) : ""):
				   a prvalue of the class, the other operand converted in its own branch ([expr.cond]/4) */
                bool cls2 = cc_type_is_record(c->ast, t2);
                ast_qualified_type ct = AST_QUALIFIED_TYPE_UNQUALIFIED(
                                       cls2 ? t2 : t3),
                                   ot = cls2 ? t3 : t2, at[1];
                uint8_t lv[1] = {(uint8_t)(cls2 ? c->elval : l2)};
                uint32_t on = cls2 ? n3 : n2;

                at[0] =
                    AST_TYPE_KIND(c->ast, ot) == AST_TYPE_ARRAY
                        ? cc_type_pointer(c->ast, AST_TYPE(c->ast, ot)->base)
                        : ot;
                if (AST_TYPE_KIND(c->ast, ot) == AST_TYPE_ARRAY)
                    lv[0] = 0;
                c->skip_explicit = 2;
                cxx_ctor_use(c, ct, at, lv, 1, false);
                c->skip_explicit = 0;
                if (c->ctor_found && c->ctor_picked &&
                    c->ctor_picked->fn_explicit != 1) {
                    cxx_et(c, ct, false);
                    if (cxx_gen_on(c) && on) {
                        c->gen_notemp++; /* (built only when its branch runs) */
                        cxx_gen_construct_temp(c, ct, &on, 1, qloc);
                        c->gen_notemp--;
                        on = c->enode;
                        if (cls2)
                            n3 = on;
                        else
                            n2 = on;
                        cxx_et(c, ct, false);
                    }
                } else
                    cxx_et(c, CXX_TY_UNKNOWN, false);
            } else
                cxx_et(c, CXX_TY_UNKNOWN, false);
            /* (built as a value: where the result is used as an lvalue, a ? x : y = 1, the assignment's
			   builder finds no lvalue and the function gets no code) */
            if (c->etype != CXX_TY_UNKNOWN &&
                (cc_type_is_arithmetic(c->ast, c->etype) ||
                 AST_TYPE_KIND(c->ast, c->etype) == AST_TYPE_POINTER ||
                 AST_TYPE_KIND(c->ast, c->etype) ==
                     AST_TYPE_VOID || /* (c ? void(0) : f(): assert) */
                 cxx_type_is_complex(c, c->etype) ||
                 cc_type_is_record(
                     c->ast,
                     c->etype))) /* (of one class: cxx_gen_conditional checks) */
                cxx_gen_conditional(c, cn, n2, n3, qloc);
            if (ck && (cv ? k2 : k3))
                c->evalue = cv ? v2 : v3,
                c->econst = 1; /* a constant condition: the branch taken */
        }
    }
}

VPRIV bool cxx_is_assignment_operator(cxx *c, uint32_t *ntok)
{
    switch (cxx_operator_at(c, ntok)) {
    case LEX_TOKEN_EQUAL:
    case LEX_TOKEN_STAR_EQUAL:
    case LEX_TOKEN_SLASH_EQUAL:
    case LEX_TOKEN_PERCENT_EQUAL:
    case LEX_TOKEN_PLUS_EQUAL:
    case LEX_TOKEN_MINUS_EQUAL:
    case LEX_TOKEN_LESS_LESS_EQUAL:
    case LEX_TOKEN_AMPERSAND_EQUAL:
    case LEX_TOKEN_CARET_EQUAL:
    case LEX_TOKEN_PIPE_EQUAL:
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
        if (!cxx_is(c, LEX_TOKEN_SEMICOLON) &&
            !cxx_is(c, LEX_TOKEN_RIGHT_PAREN) && !cxx_is(c, LEX_TOKEN_COMMA) &&
            !cxx_is(c, LEX_TOKEN_COLON) && !cxx_is(c, LEX_TOKEN_RIGHT_BRACE)) {
            cxx_assignment_expression_parse(c);
            operand = true, x = c->enode;
        }
        cxx_et(c, cc_type_basic(AST_TYPE_VOID), false);
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
        ast_qualified_type l = c->etype;
        uint8_t l_lval = c->elval;
        bool built_in = l != CXX_TY_UNKNOWN &&
                        (cc_type_is_arithmetic(c->ast, l) ||
                         AST_TYPE_KIND(c->ast, l) == AST_TYPE_POINTER);
        uint32_t ln = c->enode, oploc = cxx_peek(c, 0)->loc, ntok = n;
        int optok = cxx_operator_at(c, &ntok);

        uint32_t opid = cxx_operator_id_at(c, n);

        if (cxx_peek(c, n)->kind == LEX_TOKEN_ELLIPSIS)
            return; /* a fold over = */
        while (n--)
            cxx_advance(c);
        /* a = {...}: the list is operator='s argument, a temporary of its parameter's class (its node is
           not the list's: the last element's) */
        uint32_t rbrace = cxx_is(c, LEX_TOKEN_LEFT_BRACE) ? c->pos : 0;

        cxx_initializer_parse(c);
        {
            uint32_t rn = rbrace && !built_in ? 0 : c->enode;

            ast_qualified_type rt = c->etype;
            uint8_t unsure = c->eunsure, invalid = c->einvalid,
                    missing = c->member_missing;

            /* a = b of one class with no operator= declared: the implicit copy or move assignment, whose
               result is a, an lvalue ([class.copy.assign]: assignable_from<T&, T> for json's iterators) */
            bool implicit =
                !built_in && optok == LEX_TOKEN_EQUAL && l != CXX_TY_UNKNOWN &&
                rt != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, l) &&
                AST_TYPE_KIND(c->ast, l) != AST_TYPE_ARRAY &&
                !(AST_QUALIFIED_TYPE_QUALIFIERS(l) & AST_QUALIFIER_CONST) &&
                AST_QUALIFIED_TYPE_UNQUALIFIED(l) ==
                    AST_QUALIFIED_TYPE_UNQUALIFIED(rt) &&
                !cxx_class_user_special(c, l, vintern_cstr("operator="));

            /* a = {...} of a class: the list a T prvalue (operator=(T) or (T&&) takes it; the list's own
               type, its last element's, is not the argument's) */
            uint32_t btmp = 0;

            if (rbrace && !built_in && !c->tentative && l != CXX_TY_UNKNOWN &&
                cc_type_is_record(c->ast, l) &&
                AST_TYPE_KIND(c->ast, l) != AST_TYPE_ARRAY &&
                optok == LEX_TOKEN_EQUAL) {
                ast_qualified_type et = c->etype;
                bool el = c->elval;

                if (cxx_gen_on(c))
                    btmp = cxx_gen_braced_temp(
                        c, AST_QUALIFIED_TYPE_UNQUALIFIED(l), rbrace, oploc);
                c->etype = et, c->elval = el;
                rt = AST_QUALIFIED_TYPE_UNQUALIFIED(l);
                c->etype = rt, c->elval = 0;
                rbrace = 0,
                rn = btmp; /* (the temporary: an lvalue node, a prvalue of T) */
            }
            /* (the operands' value categories: a = b with b an lvalue takes operator=(const T&), not (T&&)) */
            c->op_lv_set = 1, c->op_lv[0] = l_lval, c->op_lv[1] = c->elval;
            if (built_in || c->tentative ||
                !cxx_operator_call_type(c, opid, l, &c->etype, 1))
                cxx_et(c, built_in ? l : CXX_TY_UNKNOWN, built_in);
            c->op_lv_set = 0;
            if (implicit && c->etype == CXX_TY_UNKNOWN) {
                /* (what looking for a declared operator= left: no doubt about this one) */
                c->eunsure = unsure, c->einvalid = invalid,
                c->member_missing = missing;
                cxx_et(c, l, true);
            }
            if (built_in)
                cxx_gen_assignment(c, optok, ln, rn, oploc);
            else if (!c->tentative && btmp && implicit &&
                     !(c->call_resolved && c->call_picked))
                cxx_gen_assignment(c, optok, ln, btmp, oploc);
            else if (!c->tentative && c->call_resolved && c->call_picked) {
                if (rbrace) {
                    memset(c->gen_call_brace, 0, sizeof c->gen_call_brace);
                    c->gen_call_brace[0] = rbrace;
                }
                cxx_gen_operator_call(c, ln, &rn, 1, oploc);
                memset(c->gen_call_brace, 0, sizeof c->gen_call_brace);
            } else if (!c->tentative && rbrace) {
                /* a = {...} of a class with the implicit operator=: a = T{...}, a temporary of a's class */
                uint32_t tmp = cxx_gen_on(c) && l != CXX_TY_UNKNOWN
                                   ? cxx_gen_braced_temp(c, l, rbrace, oploc)
                                   : 0;

                if (tmp)
                    cxx_gen_assignment(c, optok, ln, tmp, oploc);
                else
                    cxx_gen_fail(c);
            } else if (!c->tentative)
                cxx_gen_assignment(
                    c, optok, ln, rn,
                    oploc); /* (no operator= declared: the implicit one, a copy of the bytes where that is what it does) */
        }
    }
}

VPRIV void cxx_expression_parse(cxx *c)
{
    cxx_assignment_expression_parse(c);
    while (cxx_is(c, LEX_TOKEN_COMMA) && !c->in_template_args) {
        if (cxx_peek(c, 1)->kind == LEX_TOKEN_ELLIPSIS)
            return; /* fold over , */
        /* a, b: b's type whatever a is; whether a is valid is then not known (decltype(f(x), void())) */
        if (c->etype == CXX_TY_UNKNOWN && !c->tentative && !c->template_depth)
            c->eunsure = 1;
        uint32_t ln = c->enode, cloc = cxx_peek(c, 0)->loc;
        ast_qualified_type lt = c->etype;
        uint8_t ll = c->elval;

        cxx_advance(c);
        cxx_assignment_expression_parse(c);
        if (!c->tentative && lt != CXX_TY_UNKNOWN &&
            c->etype != CXX_TY_UNKNOWN &&
            AST_TYPE_KIND(c->ast, c->etype) != AST_TYPE_VOID &&
            AST_TYPE_KIND(c->ast, lt) != AST_TYPE_VOID &&
            (cc_type_is_record(c->ast, lt) ||
             cc_type_is_record(c->ast, c->etype) ||
             AST_TYPE_KIND(c->ast, lt) == AST_TYPE_ENUM ||
             AST_TYPE_KIND(c->ast, c->etype) == AST_TYPE_ENUM)) {
            /* a, b with a class (or enumeration) operand: an operator, that fits is called (doctest's
			   MessageBuilder) -- else the built-in comma */
            ast_qualified_type rt = c->etype, ra = rt;
            uint8_t rl = c->elval;
            uint32_t rn = c->enode;
            bool found;

            c->op_lv_set = 1, c->op_lv[0] = ll,
            c->op_lv[1] =
                rl; /* (both operands': not what an earlier operator left in [1]) */
            found = cxx_operator_call_type(c, vintern_cstr("operator,"), lt,
                                           &ra, 1);
            c->op_lv_set = 0;
            if (found && !c->call_resolved && c->etype == CXX_TY_UNKNOWN)
                found =
                    false; /* (no operator, that vcc can pick: the built-in comma, b's type) */
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
        if (c->etype != CXX_TY_UNKNOWN &&
            !cc_type_is_record(c->ast, c->etype)) {
            uint32_t rn = c->enode;

            c->enode = 0;
            cxx_gen_comma(c, ln, rn, cloc);
        } else if (c->etype != CXX_TY_UNKNOWN && c->elval && c->enode && ln &&
                   cxx_gen_on(c) &&
                   (AST_NODE_AT(c->ast, c->enode)->flags &
                    AST_NODE_FLAG_LVALUE)) {
            /* a, b with b an object of class type: b, an lvalue (++n, ++it in a for's step), *(a, &b) */
            uint32_t rn = c->enode, p = cc_address_of_build(c->ast, rn, cloc);

            c->enode = cc_dereference_build(
                c->ast,
                cc_node_new_operands(c->ast, AST_NODE_COMMA,
                                     AST_NODE_AT(c->ast, p)->ty, cloc, ln, p),
                cloc);
        } else
            c->enode = 0;
    }
}

VPRIV void cxx_constant_expression_parse(cxx *c)
{
    c->want_constant++; /* (a constexpr function's call is folded here: case char_traits<char>::eof():) */
    cxx_conditional_expression_parse(c);
    c->want_constant--;
}
