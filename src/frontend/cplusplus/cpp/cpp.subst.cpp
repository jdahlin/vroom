/*
 * cpp.subst.cpp: [cpp.subst] Argument substitution, and the # and ##
 * operators applied as the replacement list is read ([cpp.stringize]/2:
 * "The order of evaluation of # and ## operators is unspecified": here
 * left to right, [impl.clang] as clang).
 *
 *     va-opt-replacement:
 *         '__VA_OPT__ (' pp-tokens_opt ')'
 *
 * [cpp.subst]/1: "After the arguments for the invocation of a function-like
 * macro have been identified, argument substitution takes place.  For each
 * parameter in the replacement list that is neither preceded by a # or ##
 * preprocessing token nor followed by a ## preprocessing token, the
 * preprocessing tokens naming the parameter are replaced by a token sequence
 * determined as follows: ... the preprocessing tokens of corresponding
 * argument after all macros contained therein have been expanded."
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

namespace
{

preprocessing_token placemarker(source_location at)
{
    preprocessing_token t;
    t.kind = preprocessing_token_kind::placemarker;
    t.location = at;
    return t;
}

} // namespace

/* [cpp.subst]/1: "The argument's preprocessing tokens are completely macro
   replaced before being substituted as if they formed the rest of the
   preprocessing file with no other preprocessing tokens being available."
   Done once per argument, when a parameter first needs it. */
void preprocessor::expand_argument(argument &a,
                                   owned_array<preprocessing_token> &tokens)
{
    if (a.expanded_begin >= 0)
        return;
    size_t depth = contexts.size();
    push_tokens(tokens.data() + a.begin, a.end - a.begin, -1, true);
    bool saved_flags = pending_flags;
    pending_flags = false;
    a.expanded_begin = (int32_t)tokens.size();
    for (preprocessing_token t; read_expanded(t);)
        tokens.append(t);
    a.expanded_end = (int32_t)tokens.size();
    pending_flags = saved_flags;
    while (contexts.size() > depth)
        pop_context();
}

/* the index of the ) that closes the __VA_OPT__ ( at `open` */
uint32_t preprocessor::va_opt_end(uint32_t open, uint32_t end) const
{
    unsigned depth = 0;
    for (uint32_t i = open; i < end; i++) {
        const preprocessing_token &t = elements[i].token;
        if (is_operator(t, preprocessing_op_or_punc::LEFT_PARENTHESIS))
            depth++;
        else if (is_operator(t, preprocessing_op_or_punc::RIGHT_PARENTHESIS) &&
                 --depth == 0)
            return i;
    }
    return end;
}

bool preprocessor::is_va_opt(const macro_definition &m,
                             const replacement_element &e)
{
    return m.variadic && !m.named_variadic && is_identifier(e.token) &&
           identifier(e.token) == identifier("__VA_OPT__", 10);
}

/*
 * [cpp.subst]/4: "A va-opt-replacement is treated as if it were a
 * parameter, and the preprocessing token sequence for the corresponding
 * argument is defined as follows.  If the substitution of __VA_ARGS__ as
 * neither an operand of # nor ## consists of no preprocessing tokens, the
 * argument consists of a single placemarker preprocessing token.
 * Otherwise, the argument consists of the results of the expansion of the
 * contained pp-tokens as the replacement list of the current function-like
 * macro before removal of placemarker tokens, rescanning, and further
 * replacement."
 */
void preprocessor::va_opt(substitution &s, uint32_t at,
                          owned_array<preprocessing_token> &out)
{
    const macro_definition &m = s.m;
    argument &variable = s.arguments[m.parameter_count - 1];
    expand_argument(variable, s.tokens);
    uint32_t open = at + 1, close = va_opt_end(open, s.end);
    if (variable.expanded_begin == variable.expanded_end) {
        out.append(placemarker(elements[at].token.location));
        return;
    }
    uint32_t saved_end = s.end;
    s.end = close;
    substitute_range(s, open + 1, out);
    s.end = saved_end;
}

/*
 * The right operand of a ## at `at`: [cpp.concat]/2 "the parameter is
 * replaced by the corresponding argument's preprocessing token sequence;
 * however, if an argument consists of no preprocessing tokens, the
 * parameter is replaced by a placemarker preprocessing token instead".
 * Returns the index after the operand.
 */
/* the index after the operand of a # whose operand is at `at`: a parameter,
   or __VA_OPT__ ( ... ) */
static uint32_t after_stringize_operand(preprocessor &p,
                                        preprocessor::substitution &s,
                                        uint32_t at)
{
    if (p.is_va_opt(s.m, p.elements[at]))
        return p.va_opt_end(at + 1, s.end) + 1;
    return at + 1;
}

uint32_t preprocessor::paste_operand(substitution &s, uint32_t at,
                                     owned_array<preprocessing_token> &out)
{
    const replacement_element &e = elements[at];
    if (is_operator(e.token, preprocessing_op_or_punc::HASH) &&
        s.m.function_like && at + 1 < s.end) {
        out.append(stringize_operand(s, at + 1));
        return after_stringize_operand(*this, s, at + 1);
    }
    if (is_va_opt(s.m, e)) {
        va_opt(s, at, out);
        return va_opt_end(at + 1, s.end) + 1;
    }
    if (e.parameter >= 0) {
        const argument &a = s.arguments[e.parameter];
        if (a.begin == a.end)
            out.append(placemarker(e.token.location));
        for (uint32_t k = a.begin; k < a.end; k++)
            out.append(s.tokens[k]);
        return at + 1;
    }
    out.append(e.token);
    return at + 1;
}

/* the operand of # at `at`: [cpp.stringize]/2, of the argument's tokens
   "with placemarker tokens removed" */
preprocessing_token preprocessor::stringize_operand(substitution &s,
                                                    uint32_t at)
{
    const replacement_element &e = elements[at];
    source_location where = elements[at - 1].token.location;
    if (is_va_opt(s.m, e)) {
        owned_array<preprocessing_token> tokens;
        va_opt(s, at, tokens);
        return stringize(tokens.data(), tokens.size(), where);
    }
    const argument &a = s.arguments[e.parameter];
    return stringize(s.tokens.data() + a.begin, a.end - a.begin, where);
}

/* an argument's tokens substituted: the first takes the white space before
   the parameter, and is at no line's start */
void preprocessor::emit(substitution &s, const preprocessing_token *tokens,
                        size_t count, owned_array<preprocessing_token> &out)
{
    if (!count)
        return;
    size_t first = out.size();
    for (size_t k = 0; k < count; k++)
        out.append(tokens[k]);
    out[first].after_white_space = s.next_space;
    out[first].at_start_of_line = false;
    s.next_space = false;
}

void preprocessor::substitute_range(substitution &s, uint32_t i,
                                    owned_array<preprocessing_token> &out)
{
    using op = preprocessing_op_or_punc;
    const macro_definition &m = s.m;
    while (i < s.end) {
        const replacement_element &e = elements[i];
        /* [impl.clang] white space before a token of the replacement list (not
           after a ##) goes to the next token substituted, through empty
           arguments */
        if (i > m.first_element &&
            !is_operator(elements[i - 1].token, op::HASH_HASH) &&
            e.token.after_white_space)
            s.next_space = true;
        bool paste_follows =
            i + 1 < s.end && is_operator(elements[i + 1].token, op::HASH_HASH);

        /* [cpp.concat]/3: "each instance of a ## preprocessing token in the
           replacement list (not from an argument) is deleted and the
           preceding preprocessing token is concatenated with the following
           preprocessing token" */
        if (is_operator(e.token, op::HASH_HASH) && !out.empty()) {
            /* [impl.clang] white space before the ## is the deleted ##'s */
            s.next_space = false;
            /* [ext.gnu] gnu_comma_paste */
            if (behavior.has(extension::gnu_comma_paste))
                if (uint32_t used = gnu_comma_paste(s, i, out)) {
                    i += used;
                    continue;
                }
            owned_array<preprocessing_token> right;
            uint32_t after = paste_operand(s, i + 1, right);
            preprocessing_token &left = out[out.size() - 1];
            size_t used = 1;
            if (!right.empty() &&
                !concatenate(left, right[0], e.token.location))
                used = 0;
            for (size_t k = used; k < right.size(); k++)
                out.append(right[k]);
            i = after;
            continue;
        }
        /* [cpp.stringize]/2: "If, in the replacement list, a parameter is
           immediately preceded by a # preprocessing token, both are replaced
           by a single character string literal preprocessing token" */
        if (m.function_like && is_operator(e.token, op::HASH) &&
            i + 1 < s.end) {
            preprocessing_token string = stringize_operand(s, i + 1);
            string.after_white_space = s.next_space;
            s.next_space = false;
            out.append(string);
            i = after_stringize_operand(*this, s, i + 1);
            continue;
        }
        if (is_va_opt(m, e)) {
            size_t first = out.size();
            va_opt(s, i, out);
            /* the white space before __VA_OPT__ went to its first token */
            (void)first;
            i = va_opt_end(i + 1, s.end) + 1;
            continue;
        }
        if (e.parameter >= 0) {
            argument &a = s.arguments[e.parameter];
            uint32_t begin = a.begin, end = a.end;
            if (paste_follows) {
                /* [cpp.concat]/2: the argument as it is, or a placemarker,
                   which keeps the white space for the token pasted to it */
                if (begin == end) {
                    out.append(placemarker(e.token.location));
                    out.back().after_white_space = s.next_space;
                    s.next_space = false;
                }
            } else {
                expand_argument(a, s.tokens);
                begin = (uint32_t)a.expanded_begin;
                end = (uint32_t)a.expanded_end;
            }
            emit(s, s.tokens.data() + begin, end - begin, out);
            i++;
            continue;
        }
        out.append(e.token);
        if (s.next_space)
            out.back().after_white_space = true;
        else if (i > s.m.first_element &&
                 is_operator(elements[i - 1].token, op::HASH_HASH))
            out.back().after_white_space = false;
        s.next_space = false;
        i++;
    }
}

void preprocessor::substitute(const preprocessing_token &name,
                              const macro_definition &m,
                              owned_array<argument> &arguments,
                              owned_array<preprocessing_token> &tokens,
                              owned_array<preprocessing_token> &out)
{
    (void)name;
    substitution s{m,     arguments,      tokens, m.first_element + m.length,
                   false, variadic_elided};
    substitute_range(s, m.first_element, out);
    /* [impl.clang] white space no token took goes to the token after an
       empty replacement */
    trailing_space = s.next_space;
}

} // namespace cplusplus
