/*
 * comma_paste.cpp: [ext.gnu] gnu_comma_paste (GCC manual 6.21, Variadic
 * Macros): in the replacement list of a variadic macro,
 *
 *     , ## __VA_ARGS__
 *
 * deletes the comma when the variable arguments are left out, and is the comma
 * and the arguments, pasted to nothing, when there are some.  [cpp.subst]'s
 * __VA_OPT__ (,) is the standard's way to say it; clang warns of this one
 * with -pedantic.
 */
#include "../../cplusplus-internal.h"

namespace cplusplus
{

/* at: the ## after a comma already substituted; true if this is the form
   (then the ## and __VA_ARGS__ are substituted) */
bool preprocessor::gnu_comma_paste(substitution &s, uint32_t at,
                                   owned_array<preprocessing_token> &out)
{
    const macro_definition &m = s.m;
    const replacement_element &paste = elements[at];
    const replacement_element &next = elements[at + 1];
    if (!m.variadic || next.parameter != m.parameter_count - 1 || out.empty() ||
        !is_operator(out.back(), preprocessing_op_or_punc::COMMA))
        return false;
    const argument &a = s.arguments[next.parameter];
    if (a.begin == a.end) {
        /* [impl.clang] only when the variable arguments are left out (f(x)
           for f(x, ...), or g() for g(...)); g(x,) keeps the comma */
        if (!s.variadic_elided)
            return false;
        preprocessing_token placemarker;
        placemarker.kind = preprocessing_token_kind::placemarker;
        placemarker.location = next.token.location;
        out.back() = placemarker;
        return true;
    }
    diagnostics.issue(diagnostic_id::ext_paste_comma, paste.token.location);
    for (uint32_t k = a.begin; k < a.end; k++)
        out.append(s.tokens[k]);
    return true;
}

} // namespace cplusplus
