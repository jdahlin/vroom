/*
 * ambiguity-resolution.cpp: [stmt.ambig] Ambiguity resolution: what a statement
 * can be by its first tokens.  How the two readings are then tried is
 * ambiguity-resolution.h's.
 */
#include "ambiguity-resolution.h"

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
            if (call_of_undeclared_name(ahead))
                return reading::expression;
            return reading::unknown;
        case name_class::constructor:
            return reading::unknown;
        }
    }
    return reading::expression;
}

/*
 * [basic.lookup.argdep]: a call of a hidden friend's name, which only
 * argument-dependent lookup finds; in a template, of a name declared
 * nowhere ([temp.res]: found at the instantiation by the dependent
 * arguments).  As a declaration, T(x); would need T declared and would
 * declare x anew, hiding the object x: not read so.
 */
bool parser::call_of_undeclared_name(unsigned ahead)
{
    if (!at_identifier(ahead) ||
        !at(preprocessing_op_or_punc::LEFT_PARENTHESIS, ahead + 1))
        return false;
    uint32_t name = tokens.identifier(position + ahead);
    scope_index found = 0;
    bool dependent = false;
    if (names.lookup_unqualified(name, &found, &dependent) !=
        name_kind::not_declared)
        return false;
    if (names.within_template()) {
        /* (a dependent base class may declare it).  Only where the
           function's trailing return type already names the call,
           decltype(name(...)) (fmt's flockfile): a template with another
           is not parsed, as analysis would not diagnose all of what
           [temp.res] rejects in it */
        bool named = false;
        for (unsigned back = 3; back < 12 && back <= (position + ahead).index();
             back++) {
            token_index t{(position + ahead).index() - back};
            named =
                named || (tokens.identifier(t + 2) == name &&
                          keyword_of(tokens[t]) == standard_keyword::decltype_);
        }
        if (dependent || !named)
            return false;
    } else {
        /* (not in a template: a call that depends on nothing is bound at
           its definition, which analysis does not check) */
        bool befriended = false;
        for (uint32_t declared : friend_names)
            befriended = befriended || declared == name;
        if (!befriended)
            return false;
    }
    for (unsigned i = ahead + 2;; i += 2) {
        unsigned length = 0;
        if (!at_identifier(i) ||
            classify_name(i, &length, false) != name_class::not_a_type ||
            length != 1)
            return false;
        if (at(preprocessing_op_or_punc::RIGHT_PARENTHESIS, i + 1))
            return at(preprocessing_op_or_punc::SEMICOLON, i + 2);
        if (!at(preprocessing_op_or_punc::COMMA, i + 1))
            return false;
    }
}

} // namespace cplusplus
