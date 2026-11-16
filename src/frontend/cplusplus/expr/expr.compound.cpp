/*
 * expr.compound.cpp: [expr.compound] Compound expressions: the binary
 * operators of [expr.mptr.oper] to [expr.log.or], each subclause one
 * production of the same form, and [expr.comma] and [expr.const], which are
 * too small for a file of their own.
 *
 *     pm-expression:
 *         cast-expression
 *         pm-expression .* cast-expression
 *         pm-expression ->* cast-expression
 *
 *     multiplicative-expression:
 *         pm-expression
 *         multiplicative-expression * pm-expression
 *         multiplicative-expression / pm-expression
 *         multiplicative-expression % pm-expression
 *
 *     additive-expression:
 *         multiplicative-expression
 *         additive-expression + multiplicative-expression
 *         additive-expression - multiplicative-expression
 *
 *     shift-expression:
 *         additive-expression
 *         shift-expression << additive-expression
 *         shift-expression >> additive-expression
 *
 *     compare-expression:
 *         shift-expression
 *         compare-expression <=> shift-expression
 *
 *     relational-expression:
 *         compare-expression
 *         relational-expression < compare-expression
 *         relational-expression > compare-expression
 *         relational-expression <= compare-expression
 *         relational-expression >= compare-expression
 *
 *     equality-expression:
 *         relational-expression
 *         equality-expression == relational-expression
 *         equality-expression != relational-expression
 *
 *     and-expression:
 *         equality-expression
 *         and-expression & equality-expression
 *
 *     exclusive-or-expression:
 *         and-expression
 *         exclusive-or-expression ^ and-expression
 *
 *     inclusive-or-expression:
 *         exclusive-or-expression
 *         inclusive-or-expression | exclusive-or-expression
 *
 *     logical-and-expression:
 *         inclusive-or-expression
 *         logical-and-expression && inclusive-or-expression
 *
 *     logical-or-expression:
 *         logical-and-expression
 *         logical-or-expression || logical-and-expression
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

namespace
{

/*
 * The twelve productions as a table: each nonterminal is its own operators
 * between operands of the next row's nonterminal, and the first alternative
 * passes that operand on.  A row's index is its level: logical-or-expression
 * is the lowest, pm-expression the highest, and above it is cast-expression.
 */
struct binary_production {
    syntax_kind kind;
    preprocessing_op_or_punc operators[4];
    unsigned operator_count;
};

constexpr binary_production binary_productions[] = {
    {syntax_kind::logical_or_expression,
     {preprocessing_op_or_punc::BAR_BAR},
     1},
    {syntax_kind::logical_and_expression,
     {preprocessing_op_or_punc::AMPERSAND_AMPERSAND},
     1},
    {syntax_kind::inclusive_or_expression, {preprocessing_op_or_punc::BAR}, 1},
    {syntax_kind::exclusive_or_expression,
     {preprocessing_op_or_punc::CARET},
     1},
    {syntax_kind::and_expression, {preprocessing_op_or_punc::AMPERSAND}, 1},
    {syntax_kind::equality_expression,
     {preprocessing_op_or_punc::EQUAL_EQUAL,
      preprocessing_op_or_punc::EXCLAMATION_EQUAL},
     2},
    {syntax_kind::relational_expression,
     {preprocessing_op_or_punc::LESS, preprocessing_op_or_punc::GREATER,
      preprocessing_op_or_punc::LESS_EQUAL,
      preprocessing_op_or_punc::GREATER_EQUAL},
     4},
    {syntax_kind::compare_expression, {preprocessing_op_or_punc::SPACESHIP}, 1},
    {syntax_kind::shift_expression,
     {preprocessing_op_or_punc::LESS_LESS,
      preprocessing_op_or_punc::GREATER_GREATER},
     2},
    {syntax_kind::additive_expression,
     {preprocessing_op_or_punc::PLUS, preprocessing_op_or_punc::MINUS},
     2},
    {syntax_kind::multiplicative_expression,
     {preprocessing_op_or_punc::STAR, preprocessing_op_or_punc::SLASH,
      preprocessing_op_or_punc::PERCENT},
     3},
    {syntax_kind::pm_expression,
     {preprocessing_op_or_punc::DOT_STAR, preprocessing_op_or_punc::ARROW_STAR},
     2},
};
constexpr unsigned binary_production_count =
    sizeof binary_productions / sizeof binary_productions[0];

/* the level whose operator the token is; binary_production_count if it is none */
unsigned level_of_operator(const token &t)
{
    if (t.kind != token_kind::operator_or_punctuator)
        return binary_production_count;
    preprocessing_op_or_punc op =
        preprocessing_op_or_punc_primary(t.op_or_punc);
    for (unsigned level = 0; level < binary_production_count; level++)
        for (unsigned i = 0; i < binary_productions[level].operator_count; i++)
            if (binary_productions[level].operators[i] == op)
                return level;
    return binary_production_count;
}

} // namespace

/*
 * The nonterminal of lowest_level: operands of the next level between its
 * operators, grouped as the left-recursive productions say ([expr.mul]/1:
 * "The multiplicative operators *, /, and % group left-to-right", and the
 * same sentence in each of the other subclauses).  An operand that is
 * followed by an operator of a higher level is first the left operand of
 * that one.  With no operator, the cast-expression passes through.
 *
 * A '>' is always the greater-than operator here: where it instead ends a
 * template-argument-list ([temp.names]/3) is P7's.  A fold-operator before
 * '...' ([expr.prim.fold]) is P7's too.
 */
node_index parser::binary_expression(unsigned lowest_level)
{
    size_t mark = begin_node();
    token_index first = position;
    node_index n = cast_expression();
    if (failed())
        return 0;
    for (;;) {
        unsigned level = level_of_operator(peek());
        if (level == binary_production_count || level < lowest_level)
            break;
        if (at(preprocessing_op_or_punc::ELLIPSIS, 1))
            return beyond_this_step(); /* fold-expression: P7 */
        add_token();
        binary_expression(level + 1);
        if (failed())
            return 0;
        n = finish_node(binary_productions[level].kind, mark, first);
    }
    return n;
}

/*
 * [expr.comma]:
 *
 *     expression:
 *         assignment-expression
 *         expression , assignment-expression
 *
 * [expr.comma]/1: "The comma operator groups left-to-right."
 */
node_index parser::expression()
{
    size_t mark = begin_node();
    token_index first = position;
    node_index n = assignment_expression();
    if (failed())
        return 0;
    while (at(preprocessing_op_or_punc::COMMA)) {
        if (at(preprocessing_op_or_punc::ELLIPSIS, 1))
            return beyond_this_step(); /* fold-expression: P7 */
        add_token();
        assignment_expression();
        if (failed())
            return 0;
        n = finish_node(syntax_kind::expression, mark, first);
    }
    return n;
}

/*
 * [expr.const]:
 *
 *     constant-expression:
 *         conditional-expression
 *
 * [expr.const]/1: "Certain contexts require expressions that satisfy
 * additional requirements as detailed in this subclause": the requirements
 * are semantic; the grammar only leaves out the assignment and comma
 * operators.
 */
node_index parser::constant_expression()
{
    return conditional_expression();
}

} // namespace cplusplus
