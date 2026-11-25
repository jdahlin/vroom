/*
 * if-section.cpp: [cpp.cond] Conditional inclusion.
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
 * ([cpp.cond]/10's undefined behavior, which diagnostics are issued), [impl.clang] clang's
 * choices are followed (its PPExpressions.cpp).
 */
#include <stdio.h>
#include <strings.h>
#include <sys/stat.h>

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
            token = preprocessing_token();
            token.location = p.directive_end;
        }
    }
    void advance_unexpanded()
    {
        if (!p.read_unexpanded(token)) {
            token = preprocessing_token();
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

/*
 * [expr]: the binary operators' precedence is the grammar's: each of these
 * nonterminals has the next one as its operands, from the loosest
 *
 *     expression:                 expression ',' assignment-expression  [expr.comma]
 *     conditional-expression:     logical-or-expression '?' expression ':' ...  [expr.cond]
 *     logical-or-expression:      ... '||' logical-and-expression       [expr.log.or]
 *     logical-and-expression:     ... '&&' inclusive-or-expression      [expr.log.and]
 *     inclusive-or-expression:    ... '|' exclusive-or-expression       [expr.or]
 *     exclusive-or-expression:    ... '^' and-expression                [expr.xor]
 *     and-expression:             ... '&' equality-expression           [expr.bit.and]
 *     equality-expression:        ... '==' relational-expression ...    [expr.eq]
 *     relational-expression:      ... '<' compare-expression ...        [expr.rel]
 *     shift-expression:           ... '<<' additive-expression ...      [expr.shift]
 *     additive-expression:        ... '+' multiplicative-expression ... [expr.add]
 *     multiplicative-expression:  ... '*' pm-expression ...             [expr.mul]
 *
 * [expr.spaceship]'s compare-expression (<=>) lies between relational and
 * shift, but its result is no integer ([cpp.cond]/1 wants an "integral
 * constant expression"): as in clang, <=> is no operator of #if.  The
 * levels are numbered so that a higher one binds tighter; 0 ends the
 * expression (its end, or a ) closing it), and colon is the ':' of ?:,
 * read only by the conditional-expression.
 */
enum binary_level : unsigned {
    ends_expression = 0,
    colon = 2,
    comma_expression,
    conditional_expression,
    logical_or_expression,
    logical_and_expression,
    inclusive_or_expression,
    exclusive_or_expression,
    and_expression,
    equality_expression,
    relational_expression,
    shift_expression,
    additive_expression,
    multiplicative_expression,
    no_binary_operator = ~0u,
};

struct binary_operator {
    preprocessing_op_or_punc op;
    binary_level level;
};

constexpr binary_operator binary_operators[] = {
    {preprocessing_op_or_punc::STAR, multiplicative_expression},
    {preprocessing_op_or_punc::SLASH, multiplicative_expression},
    {preprocessing_op_or_punc::PERCENT, multiplicative_expression},
    {preprocessing_op_or_punc::PLUS, additive_expression},
    {preprocessing_op_or_punc::MINUS, additive_expression},
    {preprocessing_op_or_punc::LESS_LESS, shift_expression},
    {preprocessing_op_or_punc::GREATER_GREATER, shift_expression},
    {preprocessing_op_or_punc::LESS, relational_expression},
    {preprocessing_op_or_punc::GREATER, relational_expression},
    {preprocessing_op_or_punc::LESS_EQUAL, relational_expression},
    {preprocessing_op_or_punc::GREATER_EQUAL, relational_expression},
    {preprocessing_op_or_punc::EQUAL_EQUAL, equality_expression},
    {preprocessing_op_or_punc::EXCLAMATION_EQUAL, equality_expression},
    {preprocessing_op_or_punc::AMPERSAND, and_expression},
    {preprocessing_op_or_punc::CARET, exclusive_or_expression},
    {preprocessing_op_or_punc::BAR, inclusive_or_expression},
    {preprocessing_op_or_punc::AMPERSAND_AMPERSAND, logical_and_expression},
    {preprocessing_op_or_punc::BAR_BAR, logical_or_expression},
    {preprocessing_op_or_punc::QUESTION, conditional_expression},
    {preprocessing_op_or_punc::COMMA, comma_expression},
    {preprocessing_op_or_punc::COLON, colon},
    {preprocessing_op_or_punc::RIGHT_PARENTHESIS, ends_expression},
};

/* the level of the binary operator a token is (alternative tokens too) */
unsigned precedence(const preprocessor &p, const preprocessing_token &t)
{
    if (is_end(t))
        return ends_expression;
    preprocessing_op_or_punc o = operator_of(p, t);
    for (const binary_operator &b : binary_operators)
        if (b.op == o)
            return b.level;
    return no_binary_operator;
}

/* the low `width` bits of a value as an intmax_t or uintmax_t: zero-extended,
   or sign-extended when the type is signed (with m the sign bit,
   (x ^ m) - m copies it into every bit above) */
uint64_t value_of_width(uint64_t bits, unsigned width, bool is_signed)
{
    if (width >= 64)
        return bits;
    uint64_t mask = (UINT64_C(1) << width) - 1;
    bits &= mask;
    if (!is_signed)
        return bits;
    uint64_t sign = UINT64_C(1) << (width - 1);
    return (bits ^ sign) - sign;
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
        diagnostic_id id = diagnostic_id::err_pp_macro_not_identifier;
        if (is_end(r.token))
            id = diagnostic_id::err_pp_missing_macro_name;
        p.diagnostics.issue(id, r.token.location);
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
        p.macros[static_cast<size_t>(m)].used = true;
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
       replacement process ... the behavior is undefined": [impl.clang] clang evaluates
       it, with a warning */
    if (defined.expansion) {
        diagnostic_id id = diagnostic_id::warn_defined_in_object_type_macro;
        if (p.expansion_is_function_like(defined))
            id = diagnostic_id::warn_defined_in_function_type_macro;
        p.diagnostics.issue(id, defined.expansion);
    }
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
    v = preprocessor::value();
    const preprocessing_token t = r.token;
    switch (t.kind) {
    case preprocessing_token_kind::identifier: {
        if (condition_word(p, t, "defined"))
            return evaluate_defined(p, r, v);
        /* [ext:clang_has_include_next], clang_embed (__has_embed) */
        if (condition_word(p, t, "__has_include") ||
            (condition_word(p, t, "__has_include_next") &&
             p.behavior.has(extension::clang_has_include_next)) ||
            (condition_word(p, t, "__has_embed") &&
             p.behavior.has(extension::clang_embed))) {
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
            p.quoted(p.diagnostics.issue(
                         diagnostic_id::warn_pp_undef_identifier, t.location),
                     t);
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
        v.bits = p.condition_lexer.value_of(converted).value;
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
        v.bits =
            value_of_width(p.files.value_of(converted).value, width, is_signed);
        /* [cpp.cond]/12: "whether a single-character character-literal may
           have a negative value is implementation-defined": [impl.clang]
           the literal is unsigned when its type is, and stays so */
        v.is_unsigned = !is_signed;
        /* clang: an ordinary literal, a multicharacter one too, is as
           signed as plain char */
        if (t.prefix == encoding_prefix::none)
            v.is_unsigned = !p.behavior.plain_char_is_signed;
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
            if (!evaluate_binary(p, r, v, colon, live))
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
        p.quoted(
            p.diagnostics.issue(diagnostic_id::err_pp_expr_bad_token_lparen,
                                left.identifier_token.location),
            left.identifier_token);
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
    if (peek == no_binary_operator) {
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
        if (peek == no_binary_operator) {
            unexpected_operator(p, right, r.token);
            return false;
        }
        /* [expr.cond]: the operand after ? is an expression */
        unsigned right_minimum = this_precedence + 1;
        if (o == op::QUESTION)
            right_minimum = comma_expression;
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
            else if (b == -1)
                result = 0; /* INT64_MIN % -1 overflows in C++ */
            else
                result = (uint64_t)(a % b);
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
                result = 0;
                if (shift < 64)
                    result = left.bits << shift;
            } else {
                /* APInt::sshl_ov: the bits shifted out are not all the sign */
                if (shift >= 64) {
                    overflow = true;
                    result = 0;
                } else {
                    result = left.bits << shift;
                    uint64_t magnitude = left.bits;
                    if (a < 0)
                        magnitude = ~left.bits;
                    overflow = shift && (magnitude >> (63 - shift)) != 0;
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
            if (left.is_unsigned)
                result = left.bits >> shift;
            else
                result = (uint64_t)(a >> shift);
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
            if (is_unsigned)
                result = left.bits <= right.bits;
            else
                result = a <= b;
            is_unsigned = false;
            break;
        case op::LESS:
            if (is_unsigned)
                result = left.bits < right.bits;
            else
                result = a < b;
            is_unsigned = false;
            break;
        case op::GREATER_EQUAL:
            if (is_unsigned)
                result = left.bits >= right.bits;
            else
                result = a >= b;
            is_unsigned = false;
            break;
        case op::GREATER:
            if (is_unsigned)
                result = left.bits > right.bits;
            else
                result = a > b;
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
               expression's grammar before C++11's relaxation; [impl.clang] an
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
            if (left.bits != 0)
                result = right.bits;
            else
                result = otherwise.bits;
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
        else if (condition_word(*this, t, "__has_include") ||
                 condition_word(*this, t, "__has_include_next") ||
                 condition_word(*this, t, "__has_embed"))
            state = 1;
        else
            state = 0;
    }
    (void)at;
    uint64_t v;
    return evaluate_expression(line.data(), line.size(), &v) && v != 0;
}

/* [cpp.cond]/12: the value of a constant expression of preprocessing
   tokens, macros replaced; false if it cannot be evaluated (diagnosed) */
bool preprocessor::evaluate_expression(const preprocessing_token *tokens,
                                       size_t count, uint64_t *result,
                                       bool *negative)
{
    size_t depth = contexts.size();
    push_tokens(tokens, count, -1, true);
    in_condition++;
    expression_reader r{*this, preprocessing_token()};
    r.advance();
    value v;
    bool ok = evaluate_value(*this, r, v, true);
    if (ok && !is_end(r.token)) {
        ok = evaluate_binary(*this, r, v, conditional_expression, true);
        if (ok && !is_end(r.token)) {
            diagnostics.issue(diagnostic_id::err_pp_expected_eol,
                              r.token.location);
            ok = false;
        }
    }
    in_condition--;
    while (contexts.size() > depth)
        pop_context();
    *result = v.bits;
    if (negative)
        *negative = v.negative();
    return ok;
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
    uint32_t operand = identifier(t);
    int32_t m = identifiers[operand].macro;
    if (m >= 0)
        macros[static_cast<size_t>(m)].used = true;
    defined_condition_operand = operand; /* OPTIMIZATION: the guard */
    char name_text[16];
    snprintf(name_text, sizeof name_text, "%.*s", (int)directive.size(),
             directive.data());
    expect_end_of_directive(name_text);
    return (m >= 0) != negated;
}

/* the %select index of #elif (0), #elifdef (1) and #elifndef (2) in
   clang's messages */
static int elif_select(directive_kind kind)
{
    if (kind == directive_kind::elifdef)
        return 1;
    if (kind == directive_kind::elifndef)
        return 2;
    return 0;
}

/* #elifdef and #elifndef: C++23's; [ext:clang_later_edition_features]
   before */
static void diagnose_elifdef_edition(preprocessor &p,
                                     const preprocessing_token &name,
                                     directive_kind kind)
{
    diagnostic_id id = diagnostic_id::ext_cxx23_pp_directive;
    if (p.behavior.has(language_feature::elifdef_directives))
        id = diagnostic_id::warn_cxx23_compat_pp_directive;
    p.diagnostics.issue(id, name.location) << elif_select(kind);
}

/* the condition of #if, #ifdef, #ifndef, #elif, #elifdef or #elifndef */
static bool condition_holds(preprocessor &p, const preprocessing_token &name,
                            directive_kind kind)
{
    if (kind == directive_kind::if_ || kind == directive_kind::elif)
        return p.evaluate_condition(name.location);
    bool negated =
        kind == directive_kind::ifndef || kind == directive_kind::elifndef;
    return p.evaluate_defined_condition(name, negated);
}

void preprocessor::directive_if(const preprocessing_token &name,
                                directive_kind kind)
{
    defined_condition_operand = -1;
    bool condition = condition_holds(*this, name, kind);
    /* OPTIMIZATION: the multiple-include guard's #ifndef, its group
       entered (one skipped here would end at an #else or #endif that
       skip_group reads, not directive_endif; clang's PR6282) */
    inclusion &in = inclusions.back();
    if (in.guard_state == 1 && kind == directive_kind::ifndef &&
        defined_condition_operand >= 0 && condition) {
        in.guard_state = 2;
        in.guard_name = (uint32_t)defined_condition_operand;
        in.guard_conditional = (uint32_t)conditionals.size();
    } else if (in.guard_state == 1)
        in.guard_state = 4;
    conditionals.append({name.location, condition, false});
    if (!condition)
        skip_group();
}

/* an #elif, #elifdef, #elifndef or #else met in a group that was
   processed: the rest of the if-section is skipped */
void preprocessor::directive_elif_else(const preprocessing_token &name,
                                       directive_kind kind)
{
    int which = elif_select(kind);
    bool is_else = kind == directive_kind::else_;
    if (kind == directive_kind::elifdef || kind == directive_kind::elifndef)
        diagnose_elifdef_edition(*this, name, kind);
    if (conditionals.size() <= inclusions.back().conditional_depth) {
        diagnostic_id id = diagnostic_id::pp_err_elif_without_if;
        if (is_else)
            id = diagnostic_id::pp_err_else_without_if;
        diagnostics.issue(id, name.location) << which;
        skip_rest_of_directive();
        return;
    }
    conditional &c = conditionals[conditionals.size() - 1];
    /* OPTIMIZATION: an #else or #elif of the guard: not a guard */
    inclusion &in = inclusions.back();
    if (in.guard_state == 2 && conditionals.size() - 1 == in.guard_conditional)
        in.guard_state = 4;
    if (c.else_seen) {
        diagnostic_id id = diagnostic_id::pp_err_elif_after_else;
        if (is_else)
            id = diagnostic_id::pp_err_else_after_else;
        diagnostics.issue(id, name.location) << which;
    }
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
    /* OPTIMIZATION: the multiple-include guard's #endif */
    inclusion &in = inclusions.back();
    if (in.guard_state == 2 && conditionals.size() == in.guard_conditional)
        in.guard_state = 3;
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
            kind = find_directive(behavior, s.data(), s.size());
        }
        conditional &c = conditionals[conditionals.size() - 1];
        int which = elif_select(kind);
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
                diagnose_elifdef_edition(*this, name, kind);
            if (c.else_seen)
                diagnostics.issue(diagnostic_id::pp_err_elif_after_else,
                                  name.location)
                    << which;
            if (!c.group_taken) {
                if (condition_holds(*this, name, kind)) {
                    c.group_taken = true;
                    return;
                }
                continue;
            }
            break;
        case directive_kind::unknown:
            if (is_identifier(name))
                suggest_conditional_directive(name);
            break;
        default:
            break;
        }
        diagnostics_suppressed quiet(diagnostics);
        skip_rest_of_directive();
    }
}

namespace
{

/* Levenshtein's distance, replacements allowed */
size_t edit_distance(const char *a, size_t n, const char *b, size_t m)
{
    size_t row[32];
    if (m >= 32)
        return SIZE_MAX;
    for (size_t j = 0; j <= m; j++)
        row[j] = j;
    for (size_t i = 1; i <= n; i++) {
        size_t previous = row[0];
        row[0] = i;
        for (size_t j = 1; j <= m; j++) {
            size_t above = row[j];
            size_t best = previous + (a[i - 1] != b[j - 1]);
            if (above + 1 < best)
                best = above + 1;
            if (row[j - 1] + 1 < best)
                best = row[j - 1] + 1;
            row[j] = best;
            previous = above;
        }
    }
    return row[m];
}

} // namespace

/* [impl.clang] in a skipped group, a name that is no directive but is close
   to a conditional one gets a warning with the suggestion (clang's
   findSimilarStr: the same ignoring case, or within a third of its
   length) */
void preprocessor::suggest_conditional_directive(
    const preprocessing_token &name)
{
    owned_buffer s;
    files.spelling(name, s.get());
    char first = s.data()[0];
    if (first >= 'a' && first <= 'z' && first != 'i' && first != 'e')
        return;
    if (diagnostics.severity_now(diagnostic_id::warn_pp_invalid_directive) ==
        diagnostic_severity::ignored)
        return;
    static constexpr const char *candidates[] = {"if",      "ifdef",   "ifndef",
                                                 "elif",    "else",    "endif",
                                                 "elifdef", "elifndef"};
    /* #elifdef and #elifndef only where they are directives */
    size_t count = 6;
    if (behavior.has(language_feature::elifdef_directives))
        count = 8;
    const char *best = nullptr;
    size_t n = s.size(), best_distance = SIZE_MAX;
    for (size_t i = 0; i < count; i++)
        if (strlen(candidates[i]) == n &&
            !strncasecmp(candidates[i], s.data(), n)) {
            best = candidates[i];
            best_distance = 0;
            break;
        }
    if (!best) {
        size_t limit = n / 3;
        if (n < 3)
            limit = n - 1;
        for (size_t i = 0; i < count; i++) {
            size_t d = edit_distance(s.data(), n, candidates[i],
                                     strlen(candidates[i]));
            if (d <= limit && d < best_distance)
                best = candidates[i], best_distance = d;
        }
    }
    if (best)
        (diagnostics.issue(diagnostic_id::warn_pp_invalid_directive,
                           name.location)
         << 1)
            << best;
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
    /* [cpp.cond]/3: the second form is "processed just as in normal text" */
    r.advance();
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
    bool embed = condition_word(*this, name, "__has_embed");
    owned_array<preprocessing_token> parameter_tokens;
    if (embed) {
        /* [ext:clang_embed]: the embed-parameters up to the ) */
        unsigned depth = 0;
        while (!is_end(r.token) &&
               (depth ||
                !is_operator(r.token,
                             preprocessing_op_or_punc::RIGHT_PARENTHESIS))) {
            if (is_operator(r.token,
                            preprocessing_op_or_punc::LEFT_PARENTHESIS))
                depth++;
            else if (is_operator(r.token,
                                 preprocessing_op_or_punc::RIGHT_PARENTHESIS))
                depth--;
            parameter_tokens.append(r.token);
            r.advance();
        }
    }
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
    if (embed)
        *result =
            find_embed(header.data() + 1, header.size() - 2, angled, path);
    else
        *result = find_header(header.data() + 1, header.size() - 2, angled,
                              after, path, &directory, &system);
    if (embed && *result) {
        /* C23 6.10.1: __STDC_EMBED_FOUND__ 1, __STDC_EMBED_EMPTY__ 2; an
           unknown parameter is not found */
        embed_parameters parameters2;
        int parsed =
            parse_embed_parameters(parameter_tokens.data(),
                                   parameter_tokens.size(), parameters2, true);
        if (parsed < 0)
            return false;
        uint64_t size;
        if (parsed == 0)
            *result = 0;
        else if (!embedded_size(path.data(), parameters2, &size))
            *result = 0;
        else if (size == 0)
            *result = 2;
    }
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
            return macros[static_cast<size_t>(contexts[i].macro)].function_like;
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
    /* [impl.clang] __name__ is name, in an attribute-token and in its
       attribute-namespace (__gnu__::__const__ is gnu::const; clang's
       normalization of attribute names), except in std's */
    auto strip = [](const char *&s, size_t &n) {
        if (n > 4 && s[0] == '_' && s[1] == '_' && s[n - 1] == '_' &&
            s[n - 2] == '_')
            s += 2, n -= 4;
    };
    bool std_scope = scope_length == 3 && !memcmp(scope, "std", 3);
    strip(scope, scope_length);
    if (!std_scope)
        strip(name, length);
    if (!scope_length)
        for (const auto &a : table18)
            if (strlen(a.name) == length && !memcmp(a.name, name, length))
                return a.value;
    /* [ext.clang] the attributes clang recognizes */
    if (!b.has(extension::clang_feature_checks))
        return 0;
    owned_buffer full;
    if (scope_length)
        vbuffer_format_append(full.get(), "%.*s::", (int)scope_length, scope);
    vbuffer_append(full.get(), name, length);
    return clang_cpp_attribute_value(b, full.data(), full.size(), false);
}

} // namespace cplusplus
