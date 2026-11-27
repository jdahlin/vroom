/*
 * mangling/expressions-structure.cpp: [abi:expressions] of a value-dependent expression of a template's own
 * declaration, from its structure (expression_structure) (doc/PLAN-mangling.md).
 */
#include "mangling-internal.h"

namespace cplusplus
{

/*
 * [abi:expressions] of a value-dependent expression in a template's own
 * declaration, from its structure (expression_structure: an array bound
 * N + 1), as clang mangles it: parentheses dropped,

 *     <expression> ::= <unary operator-name> <expression>
 *                  ::= <binary operator-name> <expression> <expression>
 *                  ::= <ternary operator-name> <expression> <expression> <expression>
 *                  ::= <template-param>
 *                  ::= <expr-primary>
 *     <expr-primary> ::= L <type> <value number> E
 *
 * a non-type template parameter its <template-param>, an integer,
 * character or boolean literal its type and value.  Anything else: not
 * mangled yet.
 */
bool itanium_mangler::expression_of_structure(uint32_t s)
{
    using sa = semantic_analysis;
    uint32_t tag = 0, a = 0, b = 0;
    uint64_t word = 0;
    analysis.expression_structure_parts(s, tag, word, a, b);
    if (tag == sa::tag_parameter) {
        template_param(word);
        return true;
    }
    if (tag == sa::tag_literal + (uint32_t)literal_kind::integer_literal ||
        tag == sa::tag_literal + (uint32_t)literal_kind::character_literal ||
        tag == sa::tag_literal + (uint32_t)literal_kind::boolean_literal) {
        return expr_primary_value(static_cast<fundamental_type>(a), false,
                                  word);
    }
    if (tag == sa::tag_literal + (uint32_t)literal_kind::pointer_literal) {
        /* [abi:mangling.literal]: nullptr is its type alone */
        expr_primary_nullptr();
        return true;
    }
    if (tag != sa::tag_node)
        return false;
    /* the children, and each one that is a token its op_or_punc */
    uint32_t children[5] = {};
    preprocessing_op_or_punc ops[5] = {};
    uint32_t count = 0;
    for (uint32_t c = a; c; count++) {
        uint32_t ct = 0, ca = 0, cb = 0;
        uint64_t cw = 0;
        analysis.expression_structure_parts(c, ct, cw, ca, cb);
        if (count == 5)
            return false;
        children[count] = ca;
        ops[count] = preprocessing_op_or_punc::count;
        uint32_t t = 0, ta = 0, tb = 0;
        uint64_t tw = 0;
        analysis.expression_structure_parts(ca, t, tw, ta, tb);
        if (t == sa::tag_token && static_cast<token_kind>(tw >> 48) ==
                                      token_kind::operator_or_punctuator)
            ops[count] = preprocessing_op_or_punc_primary(
                static_cast<preprocessing_op_or_punc>(tw & 0xffffff));
        c = cb;
    }
    syntax_kind kind = static_cast<syntax_kind>(word);
    if (kind == syntax_kind::primary_expression && count == 3 &&
        ops[0] == preprocessing_op_or_punc::LEFT_PARENTHESIS &&
        ops[2] == preprocessing_op_or_punc::RIGHT_PARENTHESIS)
        return expression_of_structure(children[1]);
    if (kind == syntax_kind::conditional_expression && count == 5 &&
        ops[1] == preprocessing_op_or_punc::QUESTION &&
        ops[3] == preprocessing_op_or_punc::COLON) {
        enc.write(itanium_operator::qu_conditional);
        return expression_of_structure(children[0]) &&
               expression_of_structure(children[2]) &&
               expression_of_structure(children[4]);
    }
    if (kind == syntax_kind::unary_expression && count == 2) {
        itanium_operator o = prefix_operator(operator_written(ops[0], 1));
        if (o != itanium_operator::none) {
            enc.write(o);
            return expression_of_structure(children[1]);
        }
    }
    bool binary_kind = kind == syntax_kind::additive_expression ||
                       kind == syntax_kind::multiplicative_expression ||
                       kind == syntax_kind::shift_expression ||
                       kind == syntax_kind::relational_expression ||
                       kind == syntax_kind::equality_expression ||
                       kind == syntax_kind::and_expression ||
                       kind == syntax_kind::exclusive_or_expression ||
                       kind == syntax_kind::inclusive_or_expression ||
                       kind == syntax_kind::logical_and_expression ||
                       kind == syntax_kind::logical_or_expression;
    if (binary_kind && count == 3) {
        itanium_operator o = operator_written(ops[1], 2);
        if (o != itanium_operator::none) {
            enc.write(o);
            return expression_of_structure(children[0]) &&
                   expression_of_structure(children[2]);
        }
    }
    return false;
}

} // namespace cplusplus
