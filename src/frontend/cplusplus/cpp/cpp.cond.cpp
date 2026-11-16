/*
 * cpp.cond.cpp: [cpp.cond] Conditional inclusion.
 *
 *     if-section:
 *         if-group elif-groups_opt else-group_opt endif-line
 *
 *     if-group:
 *         '# if' constant-expression new-line group_opt
 *         '# ifdef' identifier new-line group_opt
 *         '# ifndef' identifier new-line group_opt
 *
 *     elif-group:
 *         '# elif' constant-expression new-line group_opt
 *
 *     else-group:
 *         '# else' new-line group_opt
 *
 *     endif-line:
 *         '# endif' new-line
 *
 * [cpp.cond]/14: "Each directive's condition is checked in order.  If it
 * evaluates to false (zero), the group that it controls is skipped:
 * directives are processed only through the name that determines the
 * directive in order to keep track of the level of nested conditionals; the
 * rest of the directives' preprocessing tokens are ignored, as are the other
 * preprocessing tokens in the group.  Only the first group whose control
 * condition evaluates to true (nonzero) is processed; any following groups
 * are skipped and their controlling directives are processed as if they were
 * in a group that is skipped."
 *
 * Where the evaluation of the controlling expression is not specified
 * ([cpp.cond]/10's undefined behavior, which diagnostics are issued), clang's
 * choices are followed (its PPExpressions.cpp).
 */
#include <stdio.h>

#include "../cplusplus-internal.h"

namespace cplusplus
{

/* [cpp.cond]/12: a value of a controlling expression, an intmax_t or a
   uintmax_t */
struct preprocessor::value {
    uint64_t bits = 0;
    bool is_unsigned = false;
    /* the identifier the value was made of ([cpp.cond]/11: replaced by 0),
       or 0: "function-like macro %0 is not defined" when a ( follows */
    uint32_t identifier = 0;
    preprocessing_token identifier_token;
    bool negative() const
    {
        return !is_unsigned && (int64_t)bits < 0;
    }
};

/* the preprocessing tokens of the controlling expression, macros replaced as
   they are read ([cpp.cond]/10) */
struct preprocessor::expression_reader {
    preprocessor &p;
    preprocessing_token token;
    void advance()
    {
        if (!p.read_expanded(token)) {
            token = preprocessing_token{};
            token.location = p.directive_end;
        }
    }
    void advance_unexpanded()
    {
        if (!p.read_unexpanded(token)) {
            token = preprocessing_token{};
            token.location = p.directive_end;
        }
    }
};

namespace
{

/* [lex.digraph], [lex.key]/2: and, or, not, ...: an alternative token, not
   an identifier ([cpp.cond]/11 Note 2) */
bool alternative_token(const preprocessor &p, const preprocessing_token &t,
                       preprocessing_op_or_punc *op)
{
    if (!is_identifier(t) || p.behavior.microsoft_compatibility)
        return false;
    owned_buffer s;
    p.files.spelling(t, s.get());
    int k = lookup_keyword(p.behavior, s.data(), s.size());
    return k >= 0 && keyword_is_alternative_representation(k, op);
}

/* the operator a token is, its alternative tokens included */
preprocessing_op_or_punc operator_of(const preprocessor &p,
                                     const preprocessing_token &t)
{
    preprocessing_op_or_punc op;
    if (t.kind == preprocessing_token_kind::preprocessing_op_or_punc)
        return preprocessing_op_or_punc_primary(t.op_or_punc);
    if (alternative_token(p, t, &op))
        return op;
    return preprocessing_op_or_punc::count;
}

/* the precedence of a binary operator, as clang numbers them: 0 for the end
   of the expression, ~0u for a token that is no binary operator */
unsigned precedence(const preprocessor &p, const preprocessing_token &t)
{
    using op = preprocessing_op_or_punc;
    if (is_end(t))
        return 0;
    switch (operator_of(p, t)) {
    case op::PERCENT:
    case op::SLASH:
    case op::STAR:
        return 14;
    case op::PLUS:
    case op::MINUS:
        return 13;
    case op::LESS_LESS:
    case op::GREATER_GREATER:
        return 12;
    case op::LESS_EQUAL:
    case op::LESS:
    case op::GREATER_EQUAL:
    case op::GREATER:
        return 11;
    case op::EXCLAMATION_EQUAL:
    case op::EQUAL_EQUAL:
        return 10;
    case op::AMPERSAND:
        return 9;
    case op::CARET:
        return 8;
    case op::BAR:
        return 7;
    case op::AMPERSAND_AMPERSAND:
        return 6;
    case op::BAR_BAR:
        return 5;
    case op::QUESTION:
        return 4;
    case op::COMMA:
        return 3;
    case op::COLON:
        return 2;
    case op::RIGHT_PARENTHESIS:
        return 0;
    default:
        return ~0u;
    }
}

bool condition_word(const preprocessor &p, const preprocessing_token &t,
                    const char *word)
{
    if (!is_identifier(t))
        return false;
    owned_buffer s;
    p.files.spelling(t, s.get());
    return s.size() == strlen(word) && !memcmp(s.data(), word, s.size());
}

} // namespace

/*
 * [cpp.cond]:
 *
 *     defined-macro-expression:
 *         'defined' identifier
 *         'defined (' identifier ')'
 *
 * [cpp.cond]/2: "A defined-macro-expression evaluates to 1 if the
 * identifier is currently defined as a macro name ..., 0 if it is not."
 */
static bool evaluate_defined(preprocessor &p,
                             preprocessor::expression_reader &r,
                             preprocessor::value &v)
{
    preprocessing_token defined = r.token;
    r.advance_unexpanded();
    bool parenthesized =
        is_operator(r.token, preprocessing_op_or_punc::LEFT_PARENTHESIS);
    if (parenthesized)
        r.advance_unexpanded();
    if (!is_identifier(r.token)) {
        p.diagnostics.issue(is_end(r.token)
                                ? diagnostic_id::err_pp_missing_macro_name
                                : diagnostic_id::err_pp_macro_not_identifier,
                            r.token.location);
        return false;
    }
    preprocessing_op_or_punc alternative;
    if (alternative_token(p, r.token, &alternative)) {
        owned_buffer s;
        p.files.spelling(r.token, s.get());
        p.diagnostics
                .issue(diagnostic_id::err_pp_operator_used_as_macro_name,
                       r.token.location)
                .text(s.data(), s.size())
            << preprocessing_op_or_punc_spelling(alternative);
        return false;
    }
    int32_t m = p.identifiers[p.identifier(r.token)].macro;
    v.bits = m >= 0;
    v.is_unsigned = false;
    if (m >= 0)
        p.macros[m].used = true;
    if (parenthesized) {
        r.advance_unexpanded();
        if (!is_operator(r.token,
                         preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
            p.diagnostics.issue(diagnostic_id::err_pp_expected_after,
                                r.token.location)
                << "'defined'"
                << "')'";
            return false;
        }
    }
    r.advance();
    /* [cpp.cond]/10: "If the token defined is generated as a result of this
       replacement process ... the behavior is undefined": clang evaluates
       it, with a warning */
    if (defined.expansion)
        p.diagnostics.issue(
            p.expansion_is_function_like(defined)
                ? diagnostic_id::warn_defined_in_function_type_macro
                : diagnostic_id::warn_defined_in_object_type_macro,
            defined.expansion);
    return true;
}

/* one operand of the controlling expression: a literal, an identifier, a
   unary operator and its operand, or a parenthesized expression */
static bool evaluate_value(preprocessor &p, preprocessor::expression_reader &r,
                           preprocessor::value &v, bool live);
static bool evaluate_binary(preprocessor &p, preprocessor::expression_reader &r,
                            preprocessor::value &left, unsigned minimum,
                            bool live);

static bool evaluate_value(preprocessor &p, preprocessor::expression_reader &r,
                           preprocessor::value &v, bool live)
{
    using op = preprocessing_op_or_punc;
    v = preprocessor::value{};
    const preprocessing_token t = r.token;
    switch (t.kind) {
    case preprocessing_token_kind::identifier: {
        if (condition_word(p, t, "defined"))
            return evaluate_defined(p, r, v);
        if (condition_word(p, t, "__has_include") ||
            condition_word(p, t, "__has_include_next")) {
            int64_t found;
            if (!p.has_include(r, t, condition_word(p, t, "__has_include_next"),
                               &found))
                return false;
            v.bits = (uint64_t)found;
            r.advance();
            return true;
        }
        if (operator_of(p, t) != op::count)
            break;
        /* [cpp.cond]/11: "all remaining identifiers and keywords, except
           for true and false, are replaced with the pp-number 0" */
        if (condition_word(p, t, "true") || condition_word(p, t, "false")) {
            v.bits = condition_word(p, t, "true");
            r.advance();
            return true;
        }
        if (live) {
            owned_buffer s;
            p.files.spelling(t, s.get());
            p.diagnostics
                .issue(diagnostic_id::warn_pp_undef_identifier, t.location)
                .text(s.data(), s.size());
        }
        v.identifier = p.identifier(t) + 1;
        v.identifier_token = t;
        r.advance();
        return true;
    }
    case preprocessing_token_kind::pp_number: {
        token converted = p.condition_lexer.convert(t);
        if (converted.kind == token_kind::invalid)
            return false;
        if (converted.literal == literal_kind::floating_point_literal) {
            p.diagnostics.issue(diagnostic_id::err_pp_illegal_floating_literal,
                                t.location);
            return false;
        }
        if (converted.literal == literal_kind::user_defined_literal)
            p.diagnostics.issue(diagnostic_id::err_pp_invalid_udl, t.location)
                << 1;
        v.bits = converted.value;
        v.is_unsigned =
            !fundamental_type_is_signed(p.condition_behavior, converted.type);
        r.advance();
        return true;
    }
    case preprocessing_token_kind::character_literal:
    case preprocessing_token_kind::user_defined_character_literal: {
        if (t.kind == preprocessing_token_kind::user_defined_character_literal)
            p.diagnostics.issue(diagnostic_id::err_pp_invalid_udl, t.location)
                << 0;
        token converted = p.files.convert(t);
        if (converted.kind == token_kind::invalid || !converted.value_known)
            return false;
        /* [cpp.cond]/12: "This includes interpreting character-literals";
           the value of its type, as intmax_t or uintmax_t */
        unsigned width = fundamental_type_width(p.behavior, converted.type);
        bool is_signed = fundamental_type_is_signed(p.behavior, converted.type);
        uint64_t bits = converted.value;
        if (width < 64) {
            bits &= (UINT64_C(1) << width) - 1;
            if (is_signed && (bits >> (width - 1)) & 1)
                bits |= ~((UINT64_C(1) << width) - 1);
        }
        v.bits = bits;
        v.is_unsigned = !is_signed && width >= 64;
        r.advance();
        return true;
    }
    default:
        break;
    }
    if (is_end(t) || is_operator(t, op::RIGHT_PARENTHESIS)) {
        p.diagnostics.issue(diagnostic_id::err_pp_expected_value_in_expr,
                            t.location);
        return false;
    }
    switch (operator_of(p, t)) {
    case op::LEFT_PARENTHESIS: {
        r.advance();
        if (!evaluate_value(p, r, v, live))
            return false;
        if (!is_operator(r.token, op::RIGHT_PARENTHESIS)) {
            if (!evaluate_binary(p, r, v, 1, live))
                return false;
            if (!is_operator(r.token, op::RIGHT_PARENTHESIS)) {
                p.diagnostics.issue(diagnostic_id::err_pp_expected_rparen,
                                    r.token.location);
                return false;
            }
        }
        v.identifier = 0;
        r.advance();
        return true;
    }
    case op::PLUS:
        r.advance();
        if (!evaluate_value(p, r, v, live))
            return false;
        v.identifier = 0;
        return true;
    case op::MINUS: {
        r.advance();
        if (!evaluate_value(p, r, v, live))
            return false;
        v.identifier = 0;
        bool overflow = !v.is_unsigned && v.bits == UINT64_C(1) << 63;
        v.bits = 0 - v.bits;
        if (overflow && live)
            p.diagnostics.issue(diagnostic_id::warn_pp_expr_overflow,
                                t.location);
        return true;
    }
    case op::TILDE:
        r.advance();
        if (!evaluate_value(p, r, v, live))
            return false;
        v.identifier = 0;
        v.bits = ~v.bits;
        return true;
    case op::EXCLAMATION:
        r.advance();
        if (!evaluate_value(p, r, v, live))
            return false;
        v.identifier = 0;
        v.bits = v.bits == 0;
        v.is_unsigned = false;
        return true;
    default:
        p.diagnostics.issue(diagnostic_id::err_pp_expr_bad_token_start_expr,
                            t.location);
        return false;
    }
}

static void unexpected_operator(preprocessor &p,
                                const preprocessor::value &left,
                                const preprocessing_token &t)
{
    if (is_operator(t, preprocessing_op_or_punc::LEFT_PARENTHESIS) &&
        left.identifier) {
        owned_buffer s;
        p.files.spelling(left.identifier_token, s.get());
        p.diagnostics
            .issue(diagnostic_id::err_pp_expr_bad_token_lparen,
                   left.identifier_token.location)
            .text(s.data(), s.size());
    } else
        p.diagnostics.issue(diagnostic_id::err_pp_expr_bad_token_binop,
                            t.location);
}

static void converted_to_positive(preprocessor &p, int side,
                                  const preprocessor::value &v,
                                  source_location at)
{
    char text[64];
    snprintf(text, sizeof text, "%lld to %llu", (long long)v.bits,
             (unsigned long long)v.bits);
    p.diagnostics.issue(diagnostic_id::warn_pp_convert_to_positive, at)
        << side << text;
}

/* the binary operators of [expr.mul] to [expr.comma], by precedence
   climbing from minimum */
static bool evaluate_binary(preprocessor &p, preprocessor::expression_reader &r,
                            preprocessor::value &left, unsigned minimum,
                            bool live)
{
    using op = preprocessing_op_or_punc;
    unsigned peek = precedence(p, r.token);
    if (peek == ~0u) {
        unexpected_operator(p, left, r.token);
        return false;
    }
    for (;;) {
        if (peek < minimum)
            return true;
        preprocessing_op_or_punc o = operator_of(p, r.token);
        source_location at = r.token.location;
        bool right_live = live;
        if ((o == op::AMPERSAND_AMPERSAND && left.bits == 0) ||
            (o == op::BAR_BAR && left.bits != 0) ||
            (o == op::QUESTION && left.bits == 0))
            right_live = false;
        r.advance();
        preprocessor::value right;
        if (!evaluate_value(p, r, right, right_live))
            return false;
        unsigned this_precedence = peek;
        peek = precedence(p, r.token);
        if (peek == ~0u) {
            unexpected_operator(p, right, r.token);
            return false;
        }
        /* [expr.cond]: the operand after ? is an expression */
        unsigned right_minimum = o == op::QUESTION ? 3 : this_precedence + 1;
        if (peek >= right_minimum) {
            if (!evaluate_binary(p, r, right, right_minimum, right_live))
                return false;
            peek = precedence(p, r.token);
        }

        /* [expr.arith.conv]: "the usual arithmetic conversions", but not
           for the operands of ?, the shifts, the comma and the logical
           operators */
        bool is_unsigned = left.is_unsigned;
        switch (o) {
        case op::QUESTION:
        case op::LESS_LESS:
        case op::GREATER_GREATER:
        case op::COMMA:
        case op::BAR_BAR:
        case op::AMPERSAND_AMPERSAND:
            break;
        default:
            is_unsigned = left.is_unsigned || right.is_unsigned;
            if (live && is_unsigned) {
                if (left.negative())
                    converted_to_positive(p, 0, left, at);
                if (right.negative())
                    converted_to_positive(p, 1, right, at);
            }
            left.is_unsigned = right.is_unsigned = is_unsigned;
        }
        uint64_t result = 0;
        bool overflow = false;
        int64_t a = (int64_t)left.bits, b = (int64_t)right.bits;
        switch (o) {
        case op::PERCENT:
            if (right.bits == 0) {
                if (live) {
                    p.diagnostics.issue(diagnostic_id::err_pp_remainder_by_zero,
                                        at);
                    return false;
                }
            } else if (is_unsigned)
                result = left.bits % right.bits;
            else
                result = (b == -1) ? 0 : (uint64_t)(a % b);
            break;
        case op::SLASH:
            if (right.bits == 0) {
                if (live) {
                    p.diagnostics.issue(diagnostic_id::err_pp_division_by_zero,
                                        at);
                    return false;
                }
            } else if (is_unsigned)
                result = left.bits / right.bits;
            else if (a == INT64_MIN && b == -1)
                result = (uint64_t)a, overflow = true;
            else
                result = (uint64_t)(a / b);
            break;
        case op::STAR:
            if (is_unsigned)
                result = left.bits * right.bits;
            else {
                int64_t product;
                overflow = __builtin_mul_overflow(a, b, &product);
                result = (uint64_t)product;
            }
            break;
        case op::LESS_LESS: {
            uint64_t shift = right.bits;
            if (left.is_unsigned) {
                overflow =
                    shift >= 64 || (shift && (left.bits >> (64 - shift)) != 0);
                result = shift >= 64 ? 0 : left.bits << shift;
            } else {
                /* APInt::sshl_ov: the bits shifted out are not all the sign */
                if (shift >= 64) {
                    overflow = true;
                    result = 0;
                } else {
                    result = left.bits << shift;
                    overflow = shift && ((a < 0 ? ~left.bits : left.bits) >>
                                         (63 - shift)) != 0;
                }
            }
            break;
        }
        case op::GREATER_GREATER: {
            uint64_t shift = right.bits;
            if (shift >= 64) {
                overflow = true;
                shift = 63;
            }
            result =
                left.is_unsigned ? left.bits >> shift : (uint64_t)(a >> shift);
            break;
        }
        case op::PLUS:
            if (is_unsigned)
                result = left.bits + right.bits;
            else {
                int64_t sum;
                overflow = __builtin_add_overflow(a, b, &sum);
                result = (uint64_t)sum;
            }
            break;
        case op::MINUS:
            if (is_unsigned)
                result = left.bits - right.bits;
            else {
                int64_t difference;
                overflow = __builtin_sub_overflow(a, b, &difference);
                result = (uint64_t)difference;
            }
            break;
        case op::LESS_EQUAL:
            result = is_unsigned ? left.bits <= right.bits : a <= b;
            is_unsigned = false;
            break;
        case op::LESS:
            result = is_unsigned ? left.bits < right.bits : a < b;
            is_unsigned = false;
            break;
        case op::GREATER_EQUAL:
            result = is_unsigned ? left.bits >= right.bits : a >= b;
            is_unsigned = false;
            break;
        case op::GREATER:
            result = is_unsigned ? left.bits > right.bits : a > b;
            is_unsigned = false;
            break;
        case op::EXCLAMATION_EQUAL:
            result = left.bits != right.bits;
            is_unsigned = false;
            break;
        case op::EQUAL_EQUAL:
            result = left.bits == right.bits;
            is_unsigned = false;
            break;
        case op::AMPERSAND:
            result = left.bits & right.bits;
            break;
        case op::CARET:
            result = left.bits ^ right.bits;
            break;
        case op::BAR:
            result = left.bits | right.bits;
            break;
        case op::AMPERSAND_AMPERSAND:
            result = left.bits != 0 && right.bits != 0;
            is_unsigned = false;
            break;
        case op::BAR_BAR:
            result = left.bits != 0 || right.bits != 0;
            is_unsigned = false;
            break;
        case op::COMMA:
            /* [expr.const]/5.?: a comma operator is not in a constant
               expression's grammar before C++11's relaxation; clang: an
               extension in C++ */
            if (live)
                p.diagnostics.issue(diagnostic_id::ext_pp_comma_expr, at);
            result = right.bits;
            is_unsigned = right.is_unsigned;
            break;
        case op::QUESTION: {
            if (!is_operator(r.token, op::COLON)) {
                p.diagnostics.issue(diagnostic_id::err_expected,
                                    r.token.location)
                    << "':'";
                return false;
            }
            r.advance();
            bool after_colon_live = live && left.bits == 0;
            preprocessor::value otherwise;
            if (!evaluate_value(p, r, otherwise, after_colon_live))
                return false;
            if (!evaluate_binary(p, r, otherwise, this_precedence,
                                 after_colon_live))
                return false;
            result = left.bits != 0 ? right.bits : otherwise.bits;
            is_unsigned = right.is_unsigned || otherwise.is_unsigned;
            peek = precedence(p, r.token);
            break;
        }
        case op::COLON:
            p.diagnostics.issue(diagnostic_id::err_pp_colon_without_question,
                                at);
            return false;
        default:
            unexpected_operator(p, left, r.token);
            return false;
        }
        if (overflow && live)
            p.diagnostics.issue(diagnostic_id::warn_pp_expr_overflow, at);
        left.bits = result;
        left.is_unsigned = is_unsigned;
        left.identifier = 0;
    }
}

/*
 * [cpp.cond]/9: "Preprocessing directives of the forms '# if'
 * constant-expression new-line group_opt, '# elif' ... check whether the
 * controlling constant expression evaluates to nonzero."  The directive's
 * tokens are collected first, header-names formed inside each
 * has-include-expression ([lex.pptoken]/3.3), then read with macros
 * replaced; an expression that cannot be evaluated is false.
 */
bool preprocessor::evaluate_condition(source_location at)
{
    owned_array<preprocessing_token> line;
    int state = 0; /* 1: after __has_include, 2: after its ( */
    for (;;) {
        preprocessing_token t = directive_token(state == 2);
        if (is_end(t))
            break;
        line.append(t);
        if (state == 1 &&
            is_operator(t, preprocessing_op_or_punc::LEFT_PARENTHESIS))
            state = 2;
        else
            state = condition_word(*this, t, "__has_include") ||
                            condition_word(*this, t, "__has_include_next") ||
                            condition_word(*this, t, "__has_embed")
                        ? 1
                        : 0;
    }
    (void)at;
    size_t depth = contexts.size();
    push_tokens(line.data(), line.size(), -1, true);
    in_condition++;
    expression_reader r{*this, {}};
    r.advance();
    value v;
    bool ok = evaluate_value(*this, r, v, true);
    if (ok && !is_end(r.token)) {
        ok = evaluate_binary(*this, r, v, 4, true);
        if (ok && !is_end(r.token)) {
            diagnostics.issue(diagnostic_id::err_pp_expected_eol,
                              r.token.location);
            ok = false;
        }
    }
    in_condition--;
    while (contexts.size() > depth)
        pop_context();
    return ok && v.bits != 0;
}

/* [cpp.cond]/13: "Their conditions are equivalent to #if defined
   identifier and #if !defined identifier respectively." */
bool preprocessor::evaluate_defined_condition(const preprocessing_token &name,
                                              bool negated)
{
    preprocessing_token t = directive_token();
    owned_buffer directive;
    files.spelling(name, directive.get());
    if (is_end(t)) {
        diagnostics.issue(diagnostic_id::err_pp_missing_macro_name, t.location);
        return false;
    }
    if (!is_identifier(t)) {
        diagnostics.issue(diagnostic_id::err_pp_macro_not_identifier,
                          t.location);
        skip_rest_of_directive();
        return false;
    }
    preprocessing_op_or_punc alternative;
    if (alternative_token(*this, t, &alternative)) {
        owned_buffer s;
        files.spelling(t, s.get());
        diagnostics
                .issue(diagnostic_id::err_pp_operator_used_as_macro_name,
                       t.location)
                .text(s.data(), s.size())
            << preprocessing_op_or_punc_spelling(alternative);
        skip_rest_of_directive();
        return false;
    }
    int32_t m = identifiers[identifier(t)].macro;
    if (m >= 0)
        macros[m].used = true;
    char name_text[16];
    snprintf(name_text, sizeof name_text, "%.*s", (int)directive.size(),
             directive.data());
    expect_end_of_directive(name_text);
    return (m >= 0) != negated;
}

void preprocessor::directive_if(const preprocessing_token &name,
                                directive_kind kind)
{
    bool condition =
        kind == directive_kind::if_
            ? evaluate_condition(name.location)
            : evaluate_defined_condition(name, kind == directive_kind::ifndef);
    conditionals.append({name.location, condition, false});
    if (!condition)
        skip_group();
}

/* an #elif, #elifdef, #elifndef or #else met in a group that was
   processed: the rest of the if-section is skipped */
void preprocessor::directive_elif_else(const preprocessing_token &name,
                                       directive_kind kind)
{
    int which = kind == directive_kind::elifdef    ? 1
                : kind == directive_kind::elifndef ? 2
                                                   : 0;
    if (kind == directive_kind::elifdef || kind == directive_kind::elifndef)
        diagnostics.issue(behavior.has(language_feature::elifdef_directives)
                              ? diagnostic_id::warn_cxx23_compat_pp_directive
                              : diagnostic_id::ext_cxx23_pp_directive,
                          name.location)
            << which;
    if (conditionals.size() <= inclusions.back().conditional_depth) {
        diagnostics.issue(kind == directive_kind::else_
                              ? diagnostic_id::pp_err_else_without_if
                              : diagnostic_id::pp_err_elif_without_if,
                          name.location)
            << which;
        skip_rest_of_directive();
        return;
    }
    conditional &c = conditionals[conditionals.size() - 1];
    if (c.else_seen)
        diagnostics.issue(kind == directive_kind::else_
                              ? diagnostic_id::pp_err_else_after_else
                              : diagnostic_id::pp_err_elif_after_else,
                          name.location)
            << which;
    if (kind == directive_kind::else_) {
        c.else_seen = true;
        expect_end_of_directive("else");
    } else
        skip_rest_of_directive();
    c.group_taken = true;
    skip_group();
}

void preprocessor::directive_endif(const preprocessing_token &name)
{
    if (conditionals.size() <= inclusions.back().conditional_depth) {
        diagnostics.issue(diagnostic_id::err_pp_endif_without_if,
                          name.location);
        skip_rest_of_directive();
        return;
    }
    conditionals.resize(conditionals.size() - 1);
    expect_end_of_directive("endif");
}

/*
 * Skips a group ([cpp.cond]/14): its lines are read as preprocessing tokens
 * ([lex.phases]: phase 3 comes first) without diagnostics, and only the
 * names of its directives are looked at, to find the #elif, #else or #endif
 * that ends it.  [cpp.pre]/4: "When in a group that is skipped, the
 * directive syntax is relaxed to allow any sequence of preprocessing tokens
 * to occur between the directive name and the following new-line
 * character."
 */
void preprocessor::skip_group()
{
    unsigned depth = 0;
    for (;;) {
        preprocessing_token t;
        {
            diagnostics_suppressed quiet(diagnostics);
            t = directive_token_or_line();
        }
        if (t.kind == preprocessing_token_kind::end_of_file)
            return; /* leave_file reports the if-section unterminated */
        if (!at_directive(t))
            continue;
        directive_end = t.location + t.extent;
        preprocessing_token name;
        {
            diagnostics_suppressed quiet(diagnostics);
            name = directive_token();
        }
        directive_kind kind = directive_kind::unknown;
        if (is_identifier(name)) {
            owned_buffer s;
            files.spelling(name, s.get());
            kind = find_directive(s.data(), s.size());
        }
        conditional &c = conditionals[conditionals.size() - 1];
        int which = kind == directive_kind::elifdef    ? 1
                    : kind == directive_kind::elifndef ? 2
                                                       : 0;
        switch (kind) {
        case directive_kind::if_:
        case directive_kind::ifdef:
        case directive_kind::ifndef:
            depth++;
            break;
        case directive_kind::endif:
            if (depth) {
                depth--;
                break;
            }
            conditionals.resize(conditionals.size() - 1);
            expect_end_of_directive("endif");
            return;
        case directive_kind::else_:
            if (depth)
                break;
            if (c.else_seen)
                diagnostics.issue(diagnostic_id::pp_err_else_after_else,
                                  name.location);
            c.else_seen = true;
            if (!c.group_taken) {
                c.group_taken = true;
                expect_end_of_directive("else");
                return;
            }
            break;
        case directive_kind::elif:
        case directive_kind::elifdef:
        case directive_kind::elifndef:
            if (depth)
                break;
            if (kind != directive_kind::elif)
                diagnostics.issue(
                    behavior.has(language_feature::elifdef_directives)
                        ? diagnostic_id::warn_cxx23_compat_pp_directive
                        : diagnostic_id::ext_cxx23_pp_directive,
                    name.location)
                    << which;
            if (c.else_seen)
                diagnostics.issue(diagnostic_id::pp_err_elif_after_else,
                                  name.location)
                    << which;
            if (!c.group_taken) {
                bool condition =
                    kind == directive_kind::elif
                        ? evaluate_condition(name.location)
                        : evaluate_defined_condition(
                              name, kind == directive_kind::elifndef);
                if (condition) {
                    c.group_taken = true;
                    return;
                }
                continue;
            }
            break;
        default:
            break;
        }
        diagnostics_suppressed quiet(diagnostics);
        skip_rest_of_directive();
    }
}

/* a skipped line's next token: from the lookahead or the lexer, no
   directive executed, the end of the file not left */
preprocessing_token preprocessor::directive_token_or_line()
{
    inclusion &in = inclusions.back();
    if (in.has_lookahead) {
        in.has_lookahead = false;
        preprocessing_token t = in.lookahead;
        if (t.kind == preprocessing_token_kind::end_of_file)
            in.has_lookahead = true; /* read_file_token leaves the file */
        return t;
    }
    preprocessing_token t = files.next(*in.file);
    if (t.kind == preprocessing_token_kind::end_of_file)
        unread_file_token(t);
    return t;
}

/*
 * [cpp.cond]:
 *
 *     has-include-expression:
 *         '__has_include' '(' header-name ')'
 *         '__has_include' '(' header-name-tokens ')'
 *
 * [cpp.cond]/4: "The has-include-expression evaluates to 1 if the search
 * for the source file succeeds, and to 0 if the search fails."
 */
bool preprocessor::has_include(expression_reader &r,
                               const preprocessing_token &name, bool next,
                               int64_t *result)
{
    owned_buffer spelling;
    files.spelling(name, spelling.get());
    char name_text[24];
    snprintf(name_text, sizeof name_text, "%.*s", (int)spelling.size(),
             spelling.data());
    r.advance_unexpanded();
    if (!is_operator(r.token, preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
        diagnostics.issue(diagnostic_id::err_pp_expected_after,
                          r.token.location)
            << name_text << "'('";
        return false;
    }
    r.advance_unexpanded();
    owned_buffer header;
    bool angled;
    source_location at = r.token.location;
    if (r.token.kind == preprocessing_token_kind::header_name ||
        is_string_literal(r.token)) {
        files.spelling(r.token, header.get());
    } else if (is_operator(r.token, preprocessing_op_or_punc::LESS)) {
        /* [cpp.cond]/3: "The second form of has-include-expression is
           considered only if the first form does not match, in which case
           the preprocessing tokens are processed just as in normal text" */
        vbuffer_append(header.get(), "<", 1);
        for (;;) {
            r.advance();
            if (is_end(r.token)) {
                diagnostics.issue(diagnostic_id::err_pp_expected_after,
                                  r.token.location)
                    << name_text << "'>'";
                return false;
            }
            if (r.token.after_white_space && header.size() > 1)
                vbuffer_append(header.get(), " ", 1);
            files.spelling(r.token, header.get());
            if (is_operator(r.token, preprocessing_op_or_punc::GREATER))
                break;
        }
    } else {
        diagnostics.issue(diagnostic_id::err_pp_expects_filename,
                          r.token.location);
        return false;
    }
    angled = header.data()[0] == '<';
    r.advance_unexpanded();
    if (!is_operator(r.token, preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
        diagnostics.issue(diagnostic_id::err_pp_expected_after,
                          r.token.location)
            << name_text << "')'";
        return false;
    }
    if (header.size() <= 2) {
        diagnostics.issue(diagnostic_id::err_pp_empty_filename, at);
        return false;
    }
    int32_t after = -1, directory;
    if (next)
        after = include_next_start(name);
    bool system;
    owned_buffer path;
    *result = find_header(header.data() + 1, header.size() - 2, angled, after,
                          path, &directory, &system);
    return true;
}

} // namespace cplusplus

namespace cplusplus
{

/* [cpp.cond]/10's warning: whether the replacement that produced a token
   was a function-like macro's (the innermost being read) */
bool preprocessor::expansion_is_function_like(
    const preprocessing_token &t) const
{
    (void)t;
    for (size_t i = contexts.size(); i-- > 0;)
        if (contexts[i].macro >= 0)
            return macros[contexts[i].macro].function_like;
    return false;
}

/*
 * [cpp.cond]/6 Table 18: "__has_cpp_attribute values" of the attributes
 * the standard specifies; [cpp.cond]/6 "For other attributes recognized by
 * the implementation, the value is implementation-defined".
 */
int64_t cpp_attribute_value(const implementation_defined_behavior &b,
                            const char *scope, size_t scope_length,
                            const char *name, size_t length)
{
    static constexpr struct {
        const char *name;
        int64_t value;
    } table18[] = {
        {"carries_dependency", 200809}, {"deprecated", 201309},
        {"fallthrough", 201603},        {"likely", 201803},
        {"maybe_unused", 201603},       {"no_unique_address", 201803},
        {"nodiscard", 201907},          {"noreturn", 200809},
        {"unlikely", 201803},
    };
    (void)b;
    if (scope_length)
        return 0;
    for (const auto &a : table18)
        if (strlen(a.name) == length && !memcmp(a.name, name, length))
            return a.value;
    return 0;
}

bool has_feature(const implementation_defined_behavior &b, const char *name,
                 size_t length, bool extension)
{
    (void)b, (void)name, (void)length, (void)extension;
    return false;
}

int64_t has_gnu_attribute(const implementation_defined_behavior &b,
                          const char *name, size_t length)
{
    (void)b, (void)name, (void)length;
    return 0;
}

bool has_builtin(const implementation_defined_behavior &b, const char *name,
                 size_t length)
{
    (void)b, (void)name, (void)length;
    return false;
}

} // namespace cplusplus
