/*
 * expr.prim.cpp: [expr.prim] Primary expressions, with [expr.prim.literal],
 * [expr.prim.this] and [expr.prim.paren]; the names are expr.prim.id.cpp.
 *
 *     primary-expression:
 *         literal
 *         this
 *         ( expression )
 *         id-expression
 *         lambda-expression
 *         fold-expression
 *         requires-expression
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

#include <string.h>

namespace cplusplus
{

/*
 * A literal, this and an identifier are their token; a qualified-id is
 * its node; the third alternative is the primary-expression node.
 * [expr.prim.paren]/1: "A parenthesized expression (E) is a primary
 * expression whose type, value, and value category are identical to those
 * of E."
 *
 * lambda-expression is expr.prim.lambda.cpp's, fold-expression and
 * requires-expression expr.prim.fold.cpp's.
 */
node_index parser::primary_expression()
{
    /* a literal ([lex.literal.kinds]; true, false and nullptr are keywords
       that are literals) */
    if (peek().literal != literal_kind::none || at(standard_keyword::this_)) {
        add_token();
        return 0;
    }
    if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
        if (at(preprocessing_op_or_punc::LEFT_BRACE, 1))
            /* [ext.gnu] a statement expression, ({ ... }): P9 */
            return beyond_this_step();
        if (fold_expression_follows())
            return fold_expression();
        size_t mark = begin_node();
        token_index first = position;
        flag_set greater(greater_is_operator, true);
        add_token();
        expression();
        if (failed())
            return 0;
        if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
            return expected("')' to close the parenthesized expression",
                            "expr.prim.paren");
        return finish_node(syntax_kind::primary_expression, mark, first);
    }
    if (at(preprocessing_op_or_punc::LEFT_BRACKET)) {
        /* [dcl.attr.grammar]/7: [[ only for an attribute-specifier, and no
           expression begins with one */
        if (at(preprocessing_op_or_punc::LEFT_BRACKET, 1))
            return misplaced_attribute_brackets();
        return lambda_expression();
    }
    if (at_identifier() || at(preprocessing_op_or_punc::COLON_COLON) ||
        at(standard_keyword::decltype_) || at(standard_keyword::operator_))
        return id_expression(false);
    if (at(standard_keyword::requires_))
        return requires_expression();
    if (at(extension_keyword::type_trait)) /* [ext.clang] */
        return clang_type_trait_expression();
    if (at(extension_keyword::offsetof_)) /* [ext.gnu] */
        return gnu_offsetof_expression();
    if (at(extension_keyword::bit_cast)) /* [ext.clang] */
        return clang_bit_cast_expression();
    if (at(extension_keyword::convertvector)) /* [ext.clang] */
        return clang_convertvector_expression();
    if (at(extension_keyword::source_location)) /* [ext.clang] */
        return clang_source_location_expression();
    switch (keyword_of(peek())) {
    /* an extension's keyword (__builtin_va_arg, __is_same, ...): P9 */
    case standard_keyword::template_:
    case standard_keyword::extension:
        return beyond_this_step();
    default:
        break;
    }
    return expected("an expression", "expr.prim");
}

/* ---- semantic analysis ---------------------------------------------- */

/*
 * [expr.prim.literal]/1: "A literal is a primary expression.  The type of
 * a literal is determined based on its form as specified in [lex.literal].
 * A string-literal is an lvalue, a user-defined-literal has the same value
 * category as the corresponding operator call expression described in
 * [lex.ext], and any other literal is a prvalue."  The lexer found each
 * literal's type ([lex.icon] Table 8, [lex.ccon], [lex.fcon]) and value.
 */
/*
 * [ext.gnu] GCC's imaginary constants (1.0i, 2j): a literal with an i or j
 * suffix has a _Complex type, which the lexer does not record (it types
 * the literal as without the suffix).  Analysis has no complex types.
 */
bool semantic_analysis::imaginary_literal(token_index t) const
{
    owned_buffer spelling;
    tokens.files.spelling(tokens[t].preprocessing, spelling.get());
    for (size_t i = spelling.size(); i-- > 0;) {
        char c = spelling.data()[i];
        if (c == 'i' || c == 'I' || c == 'j' || c == 'J')
            return true;
        /* a digit ends the suffix; f, l, u and friends are part of it */
        if (c >= '0' && c <= '9')
            return false;
    }
    return false;
}

analysis_result semantic_analysis::literal(token_index t, expression_meaning &m)
{
    const implementation_defined_behavior &b = tokens.files.behavior;
    const token &k = tokens[t];
    const literal_value &v = tokens.files.value_of(k);
    m.category = value_category::prvalue;
    switch (k.literal) {
    case literal_kind::integer_literal:
    case literal_kind::character_literal:
        if (!fundamental_type_is_integral(k.type))
            return not_supported(current_declaration, "lex.icon");
        if (k.literal == literal_kind::integer_literal && imaginary_literal(t))
            return not_supported(current_declaration, "ext.gnu");
        m.type = qualified_type::make(type_table.fundamental(k.type), cv_none);
        if (!k.value_known)
            return not_supported(current_declaration, "lex.icon");
        m.evaluation = constness::constant;
        m.value = integer_wrap(b, k.type, v.value);
        m.zero_literal =
            k.literal == literal_kind::integer_literal && v.value == 0;
        return analysis_result::well_formed;
    case literal_kind::floating_point_literal:
        if (!fundamental_type_is_floating(k.type))
            return not_supported(current_declaration, "lex.fcon");
        if (imaginary_literal(t))
            return not_supported(current_declaration, "ext.gnu");
        m.type = qualified_type::make(type_table.fundamental(k.type), cv_none);
        m.beyond_integral_constant = true;
        if (!k.value_known || k.type == fundamental_type::long_double)
            return analysis_result::well_formed;
        m.evaluation = constness::constant;
        if (k.type == fundamental_type::float_) {
            uint32_t bits = static_cast<uint32_t>(v.floating_bits_low);
            float f;
            memcpy(&f, &bits, sizeof f);
            m.value = floating_kept(f);
        } else {
            m.value = v.floating_bits_low;
        }
        return analysis_result::well_formed;
    case literal_kind::boolean_literal:
        /* [lex.bool]/1: "The Boolean literals are the keywords false and
           true.  Such literals are prvalues and have type bool." */
        m.type = qualified_type::make(
            type_table.fundamental(fundamental_type::bool_), cv_none);
        m.evaluation = constness::constant;
        m.value = integer_wrap(b, fundamental_type::bool_, v.value);
        return analysis_result::well_formed;
    case literal_kind::pointer_literal:
        /* [lex.nullptr]/1: "The pointer-literal is the keyword nullptr.
           It is a prvalue of type std::nullptr_t." */
        m.type = qualified_type::make(
            type_table.fundamental(fundamental_type::std_nullptr_t), cv_none);
        m.evaluation = constness::constant;
        m.value = 0;
        return analysis_result::well_formed;
    case literal_kind::string_literal: {
        /* [lex.string]/5: "An ordinary string literal has type "array of n
           const char" where n is the size of the string", and so on for
           u8, u, U and L ([lex.string]/6-10); /12: the size counts the
           terminating '\0' */
        if (!string_literal_meaning(t, m))
            return not_supported(current_declaration, "lex.string");
        return analysis_result::well_formed;
    }
    default:
        return not_supported(current_declaration, "lex.ext");
    }
}

/*
 * [expr.prim.paren]/1: "A parenthesized expression (E) is a primary
 * expression whose type, value, and value category are identical to those
 * of E."
 */
analysis_result semantic_analysis::parenthesized(node_index n,
                                                 expression_meaning &m)
{
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    if (node.child_count != 3 ||
        !syntax(children[0]).is(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return not_supported(n, "expr.prim");
    return expression(children[1], m);
}

bool semantic_analysis::string_literal_meaning(token_index t,
                                               expression_meaning &m)
{
    const token &k = tokens[t];
    fundamental_type element = k.type;
    if (element != fundamental_type::char_ &&
        element != fundamental_type::char8_t_ &&
        element != fundamental_type::char16_t_ &&
        element != fundamental_type::char32_t_ &&
        element != fundamental_type::wchar_t_)
        return false;
    qualified_type e =
        qualified_type::make(type_table.fundamental(element), cv_const);
    m.type = qualified_type::make(
        type_table.array_of(
            e, uint64_t(tokens.files.value_of(k).string_length) + 1),
        cv_none);
    m.category = value_category::lvalue;
    m.evaluation = constness::constant;
    m.string_literal = t;
    return true;
}

expression_meaning semantic_analysis::integer_constant(fundamental_type type,
                                                       uint64_t value,
                                                       token_index at)
{
    expression_meaning c;
    c.type = qualified_type::make(type_table.fundamental(type), cv_none);
    c.category = value_category::prvalue;
    c.evaluation = constness::constant;
    c.value = value;
    c.node = expression_node(semantic_kind::constant, c, at);
    return c;
}

} // namespace cplusplus
