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
 * order; arity its operand count (0 for the allocation functions and the
 * call, whose count varies), token the preprocessing_op_or_punc the
 * operator is written with (count: none, an allocation function or
 * co_await, a keyword).
 */
ITANIUM_OPERATOR(new_, "nw", 0, count)
ITANIUM_OPERATOR(new_array, "na", 0, count)
ITANIUM_OPERATOR(delete_, "dl", 0, count)
ITANIUM_OPERATOR(delete_array, "da", 0, count)
ITANIUM_OPERATOR(co_await_, "aw", 1, count)
ITANIUM_OPERATOR(unary_plus, "ps", 1, PLUS)
ITANIUM_OPERATOR(negate, "ng", 1, MINUS)
ITANIUM_OPERATOR(address_of, "ad", 1, AMPERSAND)
ITANIUM_OPERATOR(dereference, "de", 1, STAR)
ITANIUM_OPERATOR(complement, "co", 1, TILDE)
ITANIUM_OPERATOR(plus, "pl", 2, PLUS)
ITANIUM_OPERATOR(minus, "mi", 2, MINUS)
ITANIUM_OPERATOR(multiply, "ml", 2, STAR)
ITANIUM_OPERATOR(divide, "dv", 2, SLASH)
ITANIUM_OPERATOR(remainder, "rm", 2, PERCENT)
ITANIUM_OPERATOR(bitwise_and, "an", 2, AMPERSAND)
ITANIUM_OPERATOR(bitwise_or, "or", 2, BAR)
ITANIUM_OPERATOR(exclusive_or, "eo", 2, CARET)
ITANIUM_OPERATOR(assign, "aS", 2, EQUAL)
ITANIUM_OPERATOR(plus_assign, "pL", 2, PLUS_EQUAL)
ITANIUM_OPERATOR(minus_assign, "mI", 2, MINUS_EQUAL)
ITANIUM_OPERATOR(multiply_assign, "mL", 2, STAR_EQUAL)
ITANIUM_OPERATOR(divide_assign, "dV", 2, SLASH_EQUAL)
ITANIUM_OPERATOR(remainder_assign, "rM", 2, PERCENT_EQUAL)
ITANIUM_OPERATOR(and_assign, "aN", 2, AMPERSAND_EQUAL)
ITANIUM_OPERATOR(or_assign, "oR", 2, BAR_EQUAL)
ITANIUM_OPERATOR(exclusive_or_assign, "eO", 2, CARET_EQUAL)
ITANIUM_OPERATOR(left_shift, "ls", 2, LESS_LESS)
ITANIUM_OPERATOR(right_shift, "rs", 2, GREATER_GREATER)
ITANIUM_OPERATOR(left_shift_assign, "lS", 2, LESS_LESS_EQUAL)
ITANIUM_OPERATOR(right_shift_assign, "rS", 2, GREATER_GREATER_EQUAL)
ITANIUM_OPERATOR(equal, "eq", 2, EQUAL_EQUAL)
ITANIUM_OPERATOR(not_equal, "ne", 2, EXCLAMATION_EQUAL)
ITANIUM_OPERATOR(less, "lt", 2, LESS)
ITANIUM_OPERATOR(greater, "gt", 2, GREATER)
ITANIUM_OPERATOR(less_equal, "le", 2, LESS_EQUAL)
ITANIUM_OPERATOR(greater_equal, "ge", 2, GREATER_EQUAL)
ITANIUM_OPERATOR(three_way_compare, "ss", 2, SPACESHIP)
ITANIUM_OPERATOR(logical_not, "nt", 1, EXCLAMATION)
ITANIUM_OPERATOR(logical_and, "aa", 2, AMPERSAND_AMPERSAND)
ITANIUM_OPERATOR(logical_or, "oo", 2, BAR_BAR)
ITANIUM_OPERATOR(increment, "pp", 1, PLUS_PLUS)
ITANIUM_OPERATOR(decrement, "mm", 1, MINUS_MINUS)
ITANIUM_OPERATOR(comma, "cm", 2, COMMA)
ITANIUM_OPERATOR(pointer_to_member, "pm", 2, ARROW_STAR)
ITANIUM_OPERATOR(arrow, "pt", 2, ARROW)
ITANIUM_OPERATOR(call, "cl", 0, LEFT_PARENTHESIS)
ITANIUM_OPERATOR(subscript, "ix", 2, LEFT_BRACKET)
ITANIUM_OPERATOR(conditional, "qu", 3, QUESTION)
