/*
 * stmt.ambig.cpp: [stmt.ambig] Ambiguity resolution: what a statement can
 * be by its first tokens.  How the two readings are then tried is
 * stmt.ambig.h's.
 */
#include "stmt.ambig.h"

namespace cplusplus
{

/*
 * A declaration-statement is a block-declaration ([stmt.dcl]); every
 * alternative but simple-declaration starts with its own keyword, and a
 * simple-declaration with a decl-specifier-seq.  A decl-specifier that no
 * expression starts with makes the statement a declaration; a type-name or
 * a fundamental type's keyword before ( or { is what [stmt.ambig]/1 is
 * about (an expression's functional cast, [expr.type.conv]); a name that
 * is declared and not a type begins an expression.
 */
parser::reading parser::reading_at(unsigned ahead)
{
    /* [ext.gnu] __extension__ before either: what follows it decides */
    if (at(extension_keyword::extension, ahead))
        return reading_at(ahead + 1);
    standard_keyword k = keyword_of(peek(ahead));
    switch (k) {
    case standard_keyword::using_:
    case standard_keyword::static_assert_:
    case standard_keyword::asm_:
    case standard_keyword::typedef_:
    case standard_keyword::static_:
    case standard_keyword::extern_:
    case standard_keyword::thread_local_:
    case standard_keyword::register_:
    case standard_keyword::mutable_:
    case standard_keyword::inline_:
    case standard_keyword::constexpr_:
    case standard_keyword::consteval_:
    case standard_keyword::constinit_:
    case standard_keyword::const_:
    case standard_keyword::volatile_:
    case standard_keyword::class_:
    case standard_keyword::struct_:
    case standard_keyword::union_:
    case standard_keyword::enum_:
    case standard_keyword::namespace_:
    case standard_keyword::friend_:
    case standard_keyword::virtual_:
    case standard_keyword::explicit_:
    case standard_keyword::typename_:
    case standard_keyword::template_:
    case standard_keyword::concept_:
        return reading::declaration;
    case standard_keyword::auto_:
        /* placeholder-type-specifier; auto(x) and auto{x} are P0849R8's
           functional casts (C++23), which the expression reading leaves to
           a later step */
        if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS, ahead + 1) ||
            at(preprocessing_op_or_punc::LEFT_BRACE, ahead + 1))
            return reading::either;
        return reading::declaration;
    case standard_keyword::decltype_:
    /* an extension's keyword: either reading is P9's */
    case standard_keyword::extension:
        return reading::either;
    default:
        break;
    }
    if (is_simple_type_keyword(k)) {
        if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS, ahead + 1) ||
            at(preprocessing_op_or_punc::LEFT_BRACE, ahead + 1))
            return reading::either;
        return reading::declaration;
    }
    /* ::operator new(n): a declaration's declarator-id in a block is not
       qualified ([dcl.meaning]/1), so this is an expression */
    if (at(preprocessing_op_or_punc::COLON_COLON, ahead) &&
        at(standard_keyword::operator_, ahead + 1))
        return reading::expression;
    if (at_identifier(ahead) ||
        at(preprocessing_op_or_punc::COLON_COLON, ahead)) {
        /* a '<' after a name is read by what the name is (id_expression,
           decl_specifier_seq) */
        unsigned length = 0;
        switch (classify_name(ahead, &length, false)) {
        case name_class::type:
            return reading::either;
        /* a concept-name: C<T> is an expression, C auto x a declaration */
        case name_class::concept_:
            return reading::either;
        /* [temp.res]/2: a dependent name without typename is assumed not
           to name a type */
        case name_class::dependent:
        case name_class::not_a_type:
        case name_class::none:
            return reading::expression;
        case name_class::unknown:
        case name_class::constructor:
            return reading::unknown;
        }
    }
    return reading::expression;
}

} // namespace cplusplus
