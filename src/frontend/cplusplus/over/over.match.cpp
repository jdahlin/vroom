/*
 * over.match.cpp: [over.match] Overload resolution, with [over.match.viable]
 * Viable functions and [over.match.best] Best viable function.
 *
 * [over.match]/2: "Overload resolution selects the function to call in seven
 * distinct contexts within the language"; /3: "First, a subset of the
 * candidate functions (those that have the proper number of arguments and
 * meet certain other conditions) is selected to form a set of viable
 * functions ([over.match.viable]).  Then the best viable function is
 * selected based on the implicit conversion sequences ([over.best.ics])
 * needed to match each argument to the corresponding parameter of each
 * viable function.  If a best viable function exists and is unique,
 * overload resolution succeeds and produces it as the result.  Otherwise
 * overload resolution fails and the invocation is ill-formed."
 *
 * The contexts analysed: a call of a named function ([over.call.func], in
 * expr.post.cpp's call) and the initialization of an object of class type
 * by constructor ([over.match.ctor], class.ctor.cpp's construct), and
 * the specializations of a name's function templates ([temp.over],
 * expr.post.cpp's template_call), which [over.match.best]/2.4 and 2.5
 * order; 2.6 and 2.7 never decide.
 *
 * A candidate whose conversion sequence analysis cannot tell (form::unknown)
 * is taken at its best, an Exact Match: when the best viable function of the
 * others is not better than that, what is chosen is not known.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

void semantic_analysis::overload_set(entity_index e,
                                     owned_array<entity_index> &out) const
{
    for (; e; e = entity_table[e].next_overload) {
        bool seen = false;
        for (entity_index other : out)
            if (other == e)
                seen = true;
        if (!seen)
            out.append(e);
    }
}

namespace
{

using sequence = semantic_analysis::conversion_sequence;

/* a candidate's state: [over.match.viable] */
enum class viability : uint8_t { not_viable, viable, unknown };

} // namespace

analysis_result semantic_analysis::best_viable_function(
    node_index at, const owned_array<overload_candidate> &candidates,
    const expression_meaning *object,
    const owned_array<expression_meaning> &arguments, size_t &chosen,
    bool *none_viable)
{
    size_t n = arguments.size();
    /* each candidate's conversion sequences: the implicit object
       parameter's (any object when there is none), then the arguments' */
    size_t width = n + 1;
    owned_array<sequence> ics;
    owned_array<viability> state;
    for (const overload_candidate &c : candidates) {
        const struct type &f = type_table[c.type.unqualified()];
        const qualified_type *parameters =
            type_table.parameters(c.type.unqualified());
        size_t count = f.parameter_count;
        /* a member candidate's first argument is for its implicit object
           parameter ([over.match.oper]/3): its parameters are for the
           arguments after it */
        size_t first = 0;
        if (c.member)
            first = 1;
        size_t defaults = 0;
        if (c.function)
            defaults = entity_table[c.function].default_arguments;
        if (c.function && entity_table[c.function].unknown_defaults &&
            n < count + first)
            return not_supported(at, "dcl.fct.default");
        viability v = viability::viable;
        /* [over.match.viable]/2: "If there are m arguments in the list, all
           candidate functions having exactly m parameters are viable.  A
           candidate function having fewer than m parameters is viable only
           if it has an ellipsis in its parameter list.  ...  A candidate
           function having more than m parameters is viable only if all
           parameters following the mth have default arguments." */
        if (n < first || n - first + defaults < count ||
            (n - first > count && !f.variadic))
            v = viability::not_viable;
        sequence s;
        s.kind = sequence::form::standard;
        s.any_object = true;
        if (object && c.function && v == viability::viable)
            s = object_sequence(*object, c.function);
        ics.append(s);
        size_t base = ics.size();
        ics.extend(n);
        for (size_t i = 0; i < n; i++) {
            /* [over.match.oper]/3.4: a synthesized candidate with the order
               of its two parameters reversed takes the arguments the other
               way round; the sequences are kept by argument */
            size_t k = i;
            if (c.reversed)
                k = n - 1 - i;
            const expression_meaning &argument = arguments[k];
            sequence a;
            if (v == viability::not_viable) {
                a.kind = sequence::form::bad;
            } else if (i < first) {
                /* [over.match.funcs]/5: the implied object argument */
                a = object_sequence(argument, c.function);
            } else if (i - first < count) {
                /* [over.match.viable]/3: "for F to be a viable function,
                   there shall exist for each argument an implicit
                   conversion sequence ([over.best.ics]) that converts that
                   argument to the corresponding parameter of F." */
                a = implicit_conversion_sequence(argument,
                                                 parameters[i - first], true);
            } else {
                /* [over.ics.ellipsis]/1: "An ellipsis conversion sequence
                   occurs when an argument in a function call is matched
                   with the ellipsis parameter specification of the function
                   called" */
                a.kind = sequence::form::ellipsis;
            }
            ics[base + k] = a;
        }
        for (size_t i = 0; i <= n && v != viability::not_viable; i++) {
            const sequence &a = ics[ics.size() - width + i];
            if (a.kind == sequence::form::bad)
                v = viability::not_viable;
            else if (a.kind == sequence::form::unknown)
                v = viability::unknown;
        }
        state.append(v);
    }
    /* an unknown sequence at its best */
    sequence best_case;
    best_case.kind = sequence::form::standard;
    auto at_best = [&](size_t candidate, size_t i) -> const sequence & {
        const sequence &s = ics[candidate * width + i];
        if (s.kind == sequence::form::unknown)
            return best_case;
        return s;
    };
    /* [over.match.best]/2: "a viable function F1 is defined to be a better
       function than another viable function F2 if for all arguments i,
       ICSi(F1) is not a worse conversion sequence than ICSi(F2), and then
       (2.1) for some argument j, ICSj(F1) is a better conversion sequence
       than ICSj(F2)"; /1: "ICS1(F) is defined such that it is not a worse
       or better conversion sequence than ICS1(G) when F is a static member
       function" (compare_conversions) */
    auto better = [&](size_t f1, size_t f2) {
        bool some = false;
        for (size_t i = 0; i < width; i++) {
            int c = compare_conversions(at_best(f1, i), at_best(f2, i));
            if (c > 0)
                return false;
            if (c < 0)
                some = true;
        }
        if (some)
            return true;
        const overload_candidate &x = candidates[f1], &y = candidates[f2];
        /* (2.4) "F1 is not a function template specialization and F2 is a
           function template specialization"; (2.5) "F1 and F2 are
           function template specializations, and the function template
           for F1 is more specialized than the template for F2 according
           to the partial ordering rules described in [temp.func.order]" */
        if (!x.template_index && y.template_index)
            return true;
        if (x.template_index && y.template_index &&
            x.template_index != y.template_index) {
            /* (an order analysis cannot tell is neither: the final check
               below makes the call unsupported) */
            bool unknown = false;
            int order = function_template_order(x.template_index,
                                                y.template_index, n, unknown);
            if (!unknown && order != 0)
                return order > 0;
        }
        /* (2.8) "F2 is a rewritten candidate ([over.match.oper]) and F1 is
           not"; (2.9) "F1 and F2 are rewritten candidates, and F2 is a
           synthesized candidate with reversed order of parameters and F1
           is not" */
        const overload_candidate &a = candidates[f1], &b = candidates[f2];
        if (b.rewritten && !a.rewritten)
            return true;
        return a.rewritten && b.rewritten && b.reversed && !a.reversed;
    };
    size_t w = candidates.size();
    bool unknown = false;
    for (size_t i = 0; i < candidates.size(); i++) {
        if (state[i] == viability::unknown)
            unknown = true;
        if (state[i] != viability::viable)
            continue;
        if (w == candidates.size() || better(i, w))
            w = i;
    }
    if (w == candidates.size()) {
        if (unknown)
            return not_supported(at, "over.best.ics");
        if (none_viable) {
            *none_viable = true;
            return analysis_result::well_formed;
        }
        return ill_formed(at,
                          "overload resolution fails when there is no viable "
                          "function, and the invocation is ill-formed",
                          "over.match");
    }
    /* [over.match.best]/2: "If there is exactly one viable function that is
       a better function than all other viable functions, then it is the
       one selected by overload resolution; otherwise the call is
       ill-formed." */
    for (size_t i = 0; i < candidates.size(); i++) {
        if (i == w || state[i] != viability::viable)
            continue;
        if (!better(w, i) &&
            (candidates[w].rewritten || candidates[i].rewritten))
            /* [impl.clang] an ambiguity a reversed candidate makes is
               resolved for the non-reversed one with a warning
               (-Wambiguous-reversed-operator) */
            return not_supported(at, "over.match.oper");
        if (!better(w, i) &&
            (candidates[w].template_index || candidates[i].template_index))
            /* two specializations neither more specialized (or partial
               ordering analysis cannot tell): clang's ambiguity, not
               claimed */
            return not_supported(at, "temp.func.order");
        bool user_defined = false;
        for (size_t k = 0; k < width; k++)
            if (ics[w * width + k].kind ==
                    conversion_sequence::form::user_defined &&
                ics[i * width + k].kind ==
                    conversion_sequence::form::user_defined &&
                ics[w * width + k].function != ics[i * width + k].function)
                user_defined = true;
        if (!better(w, i) && user_defined)
            /* /3.3: two user-defined conversion sequences of different
               functions are indistinguishable; clang's ambiguity is not
               claimed */
            return not_supported(at, "over.ics.rank");
        if (!better(w, i))
            return ill_formed(at,
                              "overload resolution fails when no viable "
                              "function is better than all other viable "
                              "functions, and the invocation is ill-formed",
                              "over.match.best");
    }
    for (size_t i = 0; i < candidates.size(); i++)
        if (state[i] == viability::unknown && !better(w, i))
            return not_supported(at, "over.best.ics");
    /* [over.best.ics]/10: "If a function that uses the ambiguous conversion
       sequence is selected as the best viable function, the call will be
       ill-formed" (clang's diagnostic names the conversions: not analysed) */
    for (size_t i = 0; i < width; i++)
        if (ics[w * width + i].ambiguous)
            return not_supported(at, "over.best.ics");
    chosen = w;
    return analysis_result::well_formed;
}

} // namespace cplusplus
