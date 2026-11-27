/*
 * The operator encodings, the one table of them: the name of an operator
 * function, and the operator of an expression (the <unary operator-name>,
 * <binary operator-name> and <ternary operator-name> of expressions).
 * [abi:mangling-operator]:
 *
 *     <operator-name> ::= nw  # new
 *                     ::= na  # new[]
 *                     ::= dl  # delete
 *                     ::= da  # delete[]
 *                     ::= aw  # co_await
 *                     ::= ps  # + (unary)
 *                     ::= ng  # - (unary)
 *                     ::= ad  # & (unary)
 *                     ::= de  # * (unary)
 *                     ::= co  # ~
 *                     ::= pl  # +
 *                     ::= mi  # -
 *                     ::= ml  # *
 *                     ::= dv  # /
 *                     ::= rm  # %
 *                     ::= an  # &
 *                     ::= or  # |
 *                     ::= eo  # ^
 *                     ::= aS  # =
 *                     ::= pL  # +=
 *                     ::= mI  # -=
 *                     ::= mL  # *=
 *                     ::= dV  # /=
 *                     ::= rM  # %=
 *                     ::= aN  # &=
 *                     ::= oR  # |=
 *                     ::= eO  # ^=
 *                     ::= ls  # <<
 *                     ::= lS  # <<=
 *                     ::= eq  # ==
 *                     ::= ne  # !=
 *                     ::= lt  # <
 *                     ::= le  # <=
 *                     ::= ss  # <=>
 *                     ::= nt  # !
 *                     ::= aa  # &&
 *                     ::= oo  # ||
 *                     ::= pp  # ++ (postfix in <expression> context)
 *                     ::= mm  # -- (postfix in <expression> context)
 *                     ::= cm  # ,
 *                     ::= pm  # ->*
 *                     ::= pt  # ->
 *                     ::= cl  # ()
 *                     ::= ix  # []
 *                     ::= qu  # ?
 *
 * and rs (>>), rS (>>=), gt (>) and ge (>=), which the section's table has
 * and docs/itanium-abi-grammar.tsv's extraction of it lacks.
 *
 * ITANIUM_OPERATOR(name, code, arity, token): the section's table, in its
 * order (a name starts with its code: nt_logical_not is nt);
 * arity its operand count (0 for the allocation functions and the
 * call, whose count varies), token the preprocessing_op_or_punc the
 * operator is written with (count: none, an allocation function or
 * co_await, a keyword).
 */
ITANIUM_OPERATOR(nw_new_, "nw", 0, count)
ITANIUM_OPERATOR(na_new_array, "na", 0, count)
ITANIUM_OPERATOR(dl_delete_, "dl", 0, count)
ITANIUM_OPERATOR(da_delete_array, "da", 0, count)
ITANIUM_OPERATOR(aw_co_await_, "aw", 1, count)
ITANIUM_OPERATOR(ps_unary_plus, "ps", 1, PLUS)
ITANIUM_OPERATOR(ng_negate, "ng", 1, MINUS)
ITANIUM_OPERATOR(ad_address_of, "ad", 1, AMPERSAND)
ITANIUM_OPERATOR(de_dereference, "de", 1, STAR)
ITANIUM_OPERATOR(co_complement, "co", 1, TILDE)
ITANIUM_OPERATOR(pl_plus, "pl", 2, PLUS)
ITANIUM_OPERATOR(mi_minus, "mi", 2, MINUS)
ITANIUM_OPERATOR(ml_multiply, "ml", 2, STAR)
ITANIUM_OPERATOR(dv_divide, "dv", 2, SLASH)
ITANIUM_OPERATOR(rm_remainder, "rm", 2, PERCENT)
ITANIUM_OPERATOR(an_bitwise_and, "an", 2, AMPERSAND)
ITANIUM_OPERATOR(or_bitwise_or, "or", 2, BAR)
ITANIUM_OPERATOR(eo_exclusive_or, "eo", 2, CARET)
ITANIUM_OPERATOR(aS_assign, "aS", 2, EQUAL)
ITANIUM_OPERATOR(pL_plus_assign, "pL", 2, PLUS_EQUAL)
ITANIUM_OPERATOR(mI_minus_assign, "mI", 2, MINUS_EQUAL)
ITANIUM_OPERATOR(mL_multiply_assign, "mL", 2, STAR_EQUAL)
ITANIUM_OPERATOR(dV_divide_assign, "dV", 2, SLASH_EQUAL)
ITANIUM_OPERATOR(rM_remainder_assign, "rM", 2, PERCENT_EQUAL)
ITANIUM_OPERATOR(aN_and_assign, "aN", 2, AMPERSAND_EQUAL)
ITANIUM_OPERATOR(oR_or_assign, "oR", 2, BAR_EQUAL)
ITANIUM_OPERATOR(eO_exclusive_or_assign, "eO", 2, CARET_EQUAL)
ITANIUM_OPERATOR(ls_left_shift, "ls", 2, LESS_LESS)
ITANIUM_OPERATOR(rs_right_shift, "rs", 2, GREATER_GREATER)
ITANIUM_OPERATOR(lS_left_shift_assign, "lS", 2, LESS_LESS_EQUAL)
ITANIUM_OPERATOR(rS_right_shift_assign, "rS", 2, GREATER_GREATER_EQUAL)
ITANIUM_OPERATOR(eq_equal, "eq", 2, EQUAL_EQUAL)
ITANIUM_OPERATOR(ne_not_equal, "ne", 2, EXCLAMATION_EQUAL)
ITANIUM_OPERATOR(lt_less, "lt", 2, LESS)
ITANIUM_OPERATOR(gt_greater, "gt", 2, GREATER)
ITANIUM_OPERATOR(le_less_equal, "le", 2, LESS_EQUAL)
ITANIUM_OPERATOR(ge_greater_equal, "ge", 2, GREATER_EQUAL)
ITANIUM_OPERATOR(ss_three_way_compare, "ss", 2, SPACESHIP)
ITANIUM_OPERATOR(nt_logical_not, "nt", 1, EXCLAMATION)
ITANIUM_OPERATOR(aa_logical_and, "aa", 2, AMPERSAND_AMPERSAND)
ITANIUM_OPERATOR(oo_logical_or, "oo", 2, BAR_BAR)
ITANIUM_OPERATOR(pp_increment, "pp", 1, PLUS_PLUS)
ITANIUM_OPERATOR(mm_decrement, "mm", 1, MINUS_MINUS)
ITANIUM_OPERATOR(cm_comma, "cm", 2, COMMA)
ITANIUM_OPERATOR(pm_pointer_to_member, "pm", 2, ARROW_STAR)
ITANIUM_OPERATOR(pt_arrow, "pt", 2, ARROW)
ITANIUM_OPERATOR(cl_call, "cl", 0, LEFT_PARENTHESIS)
ITANIUM_OPERATOR(ix_subscript, "ix", 2, LEFT_BRACKET)
ITANIUM_OPERATOR(qu_conditional, "qu", 3, QUESTION)
