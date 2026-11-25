/*
 * ambiguity-resolution.cpp: [dcl.ambig.res] Ambiguity resolution: a type-id
 * against an expression.  ([dcl.ambig.res]/1 and /3, a declaration against
 * a function-style cast, are with the declarators they concern:
 * declarator.cpp, parameter-declaration-clause.cpp.)
 *
 * [dcl.ambig.res]/2: "An ambiguity can arise from the similarity between a
 * function-style cast and a type-id. The resolution is that any construct
 * that could possibly be a type-id in its syntactic context shall be
 * considered a type-id."
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/*
 * Whether a cast-expression can start at the current token: what makes
 * ( type-id ) before it the cast notation of [expr.cast].  The tokens that
 * begin a primary-expression, a postfix-expression or a unary-expression.
 */
bool parser::cast_expression_follows()
{
    const token &t = peek();
    if (t.kind == token_kind::identifier || t.literal != literal_kind::none)
        return true;
    if (t.kind == token_kind::keyword) {
        switch (keyword_of(t)) {
        case standard_keyword::this_:
        case standard_keyword::sizeof_:
        case standard_keyword::alignof_:
        case standard_keyword::noexcept_:
        case standard_keyword::new_:
        case standard_keyword::delete_:
        case standard_keyword::typeid_:
        case standard_keyword::dynamic_cast_:
        case standard_keyword::static_cast_:
        case standard_keyword::reinterpret_cast_:
        case standard_keyword::const_cast_:
        case standard_keyword::operator_:
        case standard_keyword::decltype_:
        case standard_keyword::typename_:
        case standard_keyword::requires_:
        case standard_keyword::co_await_:
        case standard_keyword::auto_:
        case standard_keyword::char_:
        case standard_keyword::char8_t_:
        case standard_keyword::char16_t_:
        case standard_keyword::char32_t_:
        case standard_keyword::wchar_t_:
        case standard_keyword::bool_:
        case standard_keyword::short_:
        case standard_keyword::int_:
        case standard_keyword::long_:
        case standard_keyword::signed_:
        case standard_keyword::unsigned_:
        case standard_keyword::float_:
        case standard_keyword::double_:
        case standard_keyword::void_:
        /* an extension's keyword may be an operand (__func__, a builtin) */
        case standard_keyword::extension:
            return true;
        default:
            return false;
        }
    }
    return at(preprocessing_op_or_punc::LEFT_PARENTHESIS) ||
           at(preprocessing_op_or_punc::LEFT_BRACKET) ||
           at(preprocessing_op_or_punc::COLON_COLON) ||
           at(preprocessing_op_or_punc::TILDE) ||
           at(preprocessing_op_or_punc::EXCLAMATION) ||
           at(preprocessing_op_or_punc::PLUS) ||
           at(preprocessing_op_or_punc::MINUS) ||
           at(preprocessing_op_or_punc::STAR) ||
           at(preprocessing_op_or_punc::AMPERSAND) ||
           at(preprocessing_op_or_punc::PLUS_PLUS) ||
           at(preprocessing_op_or_punc::MINUS_MINUS);
}

/*
 * ( type-id ) at the current '(', where the grammar has it beside a
 * parenthesized expression: the cast notation ([expr.cast]), and the
 * operands of sizeof ([expr.sizeof]), typeid ([expr.typeid]) and new
 * ([expr.new]).
 *
 * [dcl.ambig.res]/2's "could possibly be a type-id in its syntactic
 * context" is a tentative parse: the tokens are a type-id when they parse
 * as one up to the ')', and, for a cast (its syntactic context is
 * ( type-id ) cast-expression), when a cast-expression can start after it.
 * Then (int())+1 is a cast and (int()) alone an expression.
 *
 * When the type-id does not parse because of a name the parser cannot
 * classify (declared in a class or a template: P5, P7), the tokens are
 * read once more with every such name taken for a type-name.  If they are
 * no type-id even then, they are an expression whatever the names are
 * (sizeof(f(x, 0)) with x a variable: no parameter-declaration starts with
 * x).  Otherwise what they are depends on the names, and the parse fails
 * as a later step's.
 */
bool parser::parenthesized_type_id(bool cast)
{
    /* [ext:gnu_extension_keyword] __extension__ begins a unary-expression,
       never a type-id: ( __extension__ ... ) is a parenthesized expression
       (glibc's CPU_ISSET_S) */
    if (at(extension_keyword::extension, 1))
        return false;
    /* [temp.names]/3: a > in brackets nested in a template-argument-list
       is an operator */
    flag_set greater(greater_is_operator, true);
    failure_state failed_with;
    bool compound_literal = false;
    bool as_types = unknown_names_are_types;
    {
        tentative_parse attempt(*this);
        unknown_names_are_types = false;
        add_token();
        type_id();
        if (!failed() && at(preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
            add_token();
            if (!cast || cast_expression_follows()) {
                attempt.commit();
                return true;
            }
            compound_literal = at(preprocessing_op_or_punc::LEFT_BRACE);
            /* [ext:gnu_compound_literals] (ext/gnu/compound-literal.cpp) */
            if (compound_literal &&
                tokens.files.behavior.has(extension::gnu_compound_literals)) {
                attempt.commit();
                return true;
            }
        }
        failed_with = failure;
    }
    unknown_names_are_types = as_types;
    if (compound_literal) {
        /* ( type-id ) braced-init-list without the extension */
        beyond_this_step();
        return false;
    }
    if (failed_with.failed && failed_with.beyond_step) {
        bool possible = false;
        {
            tentative_parse attempt(*this);
            unknown_names_are_types = true;
            add_token();
            type_id();
            if (failed()) {
                /* not for a name: a later step's grammar in the type-id */
                possible = failure.beyond_step;
            } else if (at(preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
                position++;
                possible = !cast || cast_expression_follows();
            }
        }
        unknown_names_are_types = as_types;
        if (possible)
            beyond_this_step();
    }
    return false;
}

} // namespace cplusplus
