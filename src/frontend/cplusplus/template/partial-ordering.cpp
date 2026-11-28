/*
 * partial-ordering.cpp: [temp.func.order] Partial ordering of function
 * templates, with [temp.deduct.partial] Deducing template arguments during
 * partial ordering and [temp.class.order] Partial ordering of class template
 * specializations.
 *
 * [temp.func.order]/3: "To produce the transformed template, for each type,
 * non-type, or template template parameter (including template parameter
 * packs ([temp.variadic]) thereof) synthesize a unique type, value, or class
 * template respectively and substitute it for each occurrence of that
 * parameter in the function type of the template."  A template's own
 * template_parameter types are such unique types to any other template:
 * deduce_type deduces the other's parameters from them and matches them
 * with nothing else.  Templates with non-type parameters, and types
 * deduce_type has no rules for (a template-id, a dependent name, a pointer
 * to member), are not ordered (unknown).
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

namespace
{

bool is_reference(const types &table, qualified_type t)
{
    type_kind k = table[t.unqualified()].kind;
    return k == type_kind::lvalue_reference || k == type_kind::rvalue_reference;
}

/* [temp.deduct.partial]/5: "If P is a reference type, P is replaced by the
   type referred to"; /7: "Remove any top-level cv-qualifiers" (the cv the
   referred type had kept for /6) */
qualified_type transformed(const types &table, qualified_type t,
                           unsigned &referred_cv)
{
    if (is_reference(table, t))
        t = table[t.unqualified()].operand;
    referred_cv = t.cv();
    return qualified_type::make(t.unqualified(), cv_none);
}

/* (a more cv-qualified than b) */
bool more_cv(unsigned a, unsigned b)
{
    return (a & b) == b && a != b;
}

} // namespace

/* typename X<T>::type, or a reference to one: as a parameter's type a
   non-deduced context ([temp.deduct.type]/5.1), as an argument's a unique
   type */
bool semantic_analysis::member_of_unknown_specialization(qualified_type t) const
{
    if (is_reference(type_table, t))
        t = type_table[t.unqualified()].operand;
    const struct type &r = type_table[t.unqualified()];
    return r.kind == type_kind::dependent &&
           entity_table[r.entity].kind == entity_kind::dependent_member;
}

/* [temp.deduct.type]/5.1 and /5.2: Q::name and decltype(expression),
   dependent: nothing is deduced from them */
bool semantic_analysis::non_deduced(qualified_type t) const
{
    const struct type &r = type_table[t.unqualified()];
    if (r.kind != type_kind::dependent)
        return false;
    const entity &x = entity_table[r.entity];
    if (x.kind == entity_kind::dependent_member)
        return true;
    return x.kind == entity_kind::dependent_written &&
           SyntaxRef(node_index(static_cast<uint32_t>(x.value)))
               .is(syntax_kind::decltype_specifier);
}

/* a type partial ordering compares with deduce_type's rules alone */
bool semantic_analysis::orderable(qualified_type t, bool packs,
                                  bool *pack_argument,
                                  unsigned *value_kinds) const
{
    const struct type &r = type_table[t.unqualified()];
    switch (r.kind) {
    case type_kind::dependent:
        /* [temp.deduct.type]/8's TT<T>: a class template's specialization
           in the template's own parameters, which deduce_type compares
           argument by argument against the other's (type arguments only: a
           non-type or pack argument is not compared by deduce_type here) */
        if (entity_table[r.entity].kind == entity_kind::dependent_member)
            return false;
        if (!entity_table[r.entity].instantiation)
            return false;
        for (uint32_t i = 0; i < specialization_argument_count(r.entity); i++) {
            const template_argument &x =
                specialization_template_argument(r.entity, i);
            /* a non-type argument that is a value or a non-type parameter
               (deduce_type's TT<i>), not an expression naming parameters
               (a non-deduced context, [temp.deduct.type]/5.3) */
            auto comparable = [&](const template_argument &y) {
                if (y.kind == template_argument::kind_t::type)
                    return true;
                if (y.kind != template_argument::kind_t::value)
                    return false;
                if (!y.template_entity)
                    return true;
                const entity &parameter = entity_table[y.template_entity];
                if (parameter.kind == entity_kind::dependent_value)
                    return false;
                /* (a parameter of placeholder type is ordered by that type
                   too ([temp.deduct.partial]): nothing separates two plain
                   auto ones, and the caller refuses auto against another
                   type; decltype(auto) and a constraint are not ordered) */
                bool decltype_auto = false;
                entity_index named_concept = entity_index();
                bool placeholder = contains_placeholder(
                    parameter.type, &decltype_auto, &named_concept);
                if (placeholder &&
                    (!value_kinds || decltype_auto || named_concept))
                    return false;
                if (value_kinds) {
                    if (placeholder)
                        *value_kinds |= 1;
                    else
                        *value_kinds |= 2;
                }
                return true;
            };
            if (x.kind != template_argument::kind_t::pack) {
                if (!comparable(x))
                    return false;
                continue;
            }
            /* a pack of arguments, S<I...>: each element the same, or
               the expansion of a parameter pack.  A pure pack expansion
               S<Ts...> has no fixed elements for CWG1432 to separate from
               another S<Us...>. */
            bool pure_expansion = x.count == 1 &&
                                  pack_arguments[x.value].kind ==
                                      template_argument::kind_t::pack &&
                                  pack_arguments[x.value].template_entity;
            if (pack_argument && !pure_expansion)
                *pack_argument = true;
            for (uint32_t k = 0; k < x.count; k++) {
                const template_argument &y = pack_arguments[x.value + k];
                bool expansion = y.kind == template_argument::kind_t::pack &&
                                 y.template_entity;
                if (!expansion && !comparable(y))
                    return false;
            }
        }
        return true;
    case type_kind::template_parameter:
    case type_kind::fundamental:
    case type_kind::enumeration:
    case type_kind::class_type:
        return true;
    case type_kind::pack_expansion:
        return packs && orderable(r.operand, packs, pack_argument, value_kinds);
    case type_kind::pointer_to_member:
        if (!r.entity &&
            !orderable(qualified_type::make(type_index_in(r.bound), cv_none),
                       packs, pack_argument, value_kinds))
            return false;
        return orderable(r.operand, packs, pack_argument, value_kinds);
    case type_kind::pointer:
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference:
        return orderable(r.operand, packs, pack_argument, value_kinds);
    case type_kind::array:
        return (r.bound_known || r.entity) &&
               orderable(r.operand, packs, pack_argument, value_kinds);
    case type_kind::function: {
        if (!orderable(r.operand, packs, pack_argument, value_kinds))
            return false;
        const qualified_type *list = type_table.parameters(t.unqualified());
        /* [temp.deduct.type]/10: a trailing function parameter pack is
           deduced element by element */
        for (uint32_t i = 0; i < r.parameter_count; i++)
            if (!orderable(list[i], packs || i + 1 == r.parameter_count,
                           pack_argument, value_kinds))
                return false;
        return true;
    }
    default:
        return false;
    }
}

/*
 * [temp.deduct.partial]/10: "Function template F is at least as
 * specialized as function template G if, for each pair of types used to
 * determine the ordering, the type from F is at least as specialized as
 * the type from G."  /3.1: "In the context of a function call, the types
 * used are those function parameter types for which the function call has
 * arguments": per argument, the parameter's type or the pattern of the
 * function parameter pack it falls in.  G's parameters are deduced from
 * F's transformed types (/2), together, so that one parameter of G has one
 * value.
 */
bool semantic_analysis::at_least_as_specialized(uint32_t f, uint32_t g,
                                                size_t arguments, bool &unknown,
                                                bool f_reversed,
                                                bool g_reversed)
{
    const function_template ff = function_templates[f - 1];
    const function_template gg = function_templates[g - 1];
    qualified_type ftype = entity_table[ff.entity].type;
    qualified_type gtype = entity_table[gg.entity].type;
    const struct type fr = type_table[ftype.unqualified()];
    const struct type gr = type_table[gtype.unqualified()];
    const qualified_type *fp = type_table.parameters(ftype.unqualified());
    const qualified_type *gp = type_table.parameters(gtype.unqualified());
    /* [temp.func.order]/3 (CWG2445): a synthesized candidate of reversed
       order takes its template's two parameters in the other order */
    if ((f_reversed && (fr.parameter_count != 2 || arguments != 2)) ||
        (g_reversed && (gr.parameter_count != 2 || arguments != 2))) {
        unknown = true;
        return false;
    }
    auto fparam = [&](size_t k) {
        if (f_reversed)
            k = 1 - k;
        return fp[k];
    };
    auto gparam = [&](size_t k) {
        if (g_reversed)
            k = 1 - k;
        return gp[k];
    };
    uint32_t ffixed = fr.parameter_count, gfixed = gr.parameter_count;
    if (ffixed && type_table[fp[ffixed - 1].unqualified()].kind ==
                      type_kind::pack_expansion)
        ffixed--;
    if (gfixed && type_table[gp[gfixed - 1].unqualified()].kind ==
                      type_kind::pack_expansion)
        gfixed--;
    /* [temp.deduct.partial]/3.1: only the parameter types "for which the
       function call has arguments" are used: the parameters after them,
       which have default arguments, take no part */
    if (arguments < ffixed)
        ffixed = static_cast<uint32_t>(arguments);
    if (arguments < gfixed)
        gfixed = static_cast<uint32_t>(arguments);
    if ((ffixed == fr.parameter_count && arguments != ffixed) ||
        (gfixed == gr.parameter_count && arguments != gfixed)) {
        unknown = true;
        return false;
    }
    /* a function parameter pack before the last parameter: not analysed */
    for (uint32_t i = 0; i + 1 < ffixed; i++)
        if (type_table[fp[i].unqualified()].kind == type_kind::pack_expansion) {
            unknown = true;
            return false;
        }
    for (uint32_t i = 0; i + 1 < gfixed; i++)
        if (type_table[gp[i].unqualified()].kind == type_kind::pack_expansion) {
            unknown = true;
            return false;
        }
    /* the template parameter pack G's function parameter pack expands
       (defaulted parameters may follow it: template <class... U, int = 0>) */
    uint32_t pack = gg.parameter_count;
    for (uint32_t i = 0; i < gg.parameter_count; i++)
        if (template_parameter_is_pack(gg.first_parameter + i))
            pack = i;
    owned_array<template_argument> values;
    owned_array<bool> set;
    for (uint32_t i = 0; i < gg.parameter_count; i++) {
        values.append(template_argument());
        set.append(false);
    }
    bool deduced = true;
    bool penalty = false;
    bool skipped = false;
    for (size_t k = 0; k < arguments; k++) {
        bool apack = k >= ffixed, ppack = k >= gfixed;
        qualified_type a = fparam(k), p = gparam(k);
        if (apack)
            a = type_table[fp[ffixed].unqualified()].operand;
        if (ppack)
            p = type_table[gp[gfixed].unqualified()].operand;
        /* [temp.deduct.type]/10: "During partial ordering
           ([temp.deduct.partial]), if Ai was originally a function
           parameter pack: ... if Pi is not a function parameter pack,
           template argument deduction fails." */
        if (apack && !ppack)
            return false;
        /* [temp.deduct.type]/5.1: nothing is deduced from this pair */
        if (member_of_unknown_specialization(p)) {
            skipped = true;
            continue;
        }
        bool references =
            is_reference(type_table, a) && is_reference(type_table, p);
        bool a_lvalue =
            type_table[a.unqualified()].kind == type_kind::lvalue_reference;
        bool p_lvalue =
            type_table[p.unqualified()].kind == type_kind::lvalue_reference;
        unsigned acv = 0, pcv = 0;
        qualified_type at = transformed(type_table, a, acv);
        qualified_type pt = transformed(type_table, p, pcv);
        if (references) {
            /* [temp.deduct.partial]/9: "If, for a given type, the types
               are identical after the transformations above and both P and
               A were reference types (before being replaced with the type
               referred to above): if the type from the argument template
               was an lvalue reference and the type from the parameter
               template was not, the parameter type is not considered to be
               at least as specialized as the argument type; otherwise, if
               the type from the argument template is more cv-qualified
               than the type from the parameter template (as described
               above), the parameter type is not considered to be at least
               as specialized as the argument type."  For F's type to be
               at least as specialized as G's, G's is the argument here */
            owned_array<template_argument> v1, v2;
            owned_array<bool> s1, s2;
            for (uint32_t i = 0; i < gg.parameter_count; i++) {
                v1.append(template_argument());
                s1.append(false);
            }
            for (uint32_t i = 0; i < ff.parameter_count; i++) {
                v2.append(template_argument());
                s2.append(false);
            }
            if (deduce_type(pt, at, v1, s1, true) &&
                deduce_type(at, pt, v2, s2, true)) {
                if (p_lvalue && !a_lvalue)
                    penalty = true;
                else if (more_cv(pcv, acv))
                    penalty = true;
            }
        }
        /* [temp.deduct.type]/4: a non-deduced context deduces nothing,
           and takes no part in whether deduction succeeds */
        if (non_deduced(pt))
            continue;
        if (!ppack) {
            if (!deduce_type(pt, at, values, set, true))
                deduced = false;
            continue;
        }
        /* /8: "If P is a function parameter pack, the type A of each
           remaining parameter type of the argument template is compared
           with the type P of the declarator-id of the function parameter
           pack.  Each comparison deduces template arguments for subsequent
           positions in the template parameter packs expanded by the
           function parameter pack." */
        owned_array<template_argument> one;
        owned_array<bool> one_set;
        for (uint32_t i = 0; i < values.size(); i++) {
            one.append(values[i]);
            one_set.append(set[i] && i != pack);
        }
        if (!deduce_type(pt, at, one, one_set, true)) {
            deduced = false;
            continue;
        }
        for (uint32_t i = 0; i < values.size(); i++)
            if (i != pack && one_set[i] && !set[i]) {
                values[i] = one[i];
                set[i] = true;
            }
    }
    /* [temp.deduct.partial]/12: a parameter that only a skipped pair
       uses may be left without a value; whether one does is not looked
       into, so every type parameter needs one from the other pairs */
    for (uint32_t i = 0; skipped && deduced && i < gg.parameter_count; i++) {
        template_parameter_parts form;
        if (!set[i] &&
            (!template_parameter_form(
                 template_parameter_nodes[gg.first_parameter + i], form) ||
             form.type))
            unknown = true;
    }
    return deduced && !penalty;
}

/*
 * [temp.deduct.partial]/3.2: "In the context of a call to a conversion
 * function, the return types of the conversion function templates are
 * used."  Of the templates t1 and t2 (entities), 1 when t1 is more
 * specialized (t2's return type is deduced from t1's and not the other way
 * round), -1 when t2 is, 0 when neither.  The return types are transformed
 * as call ordering's parameter types are: /5 "If P is a reference type, P
 * is replaced by the type referred to", /7 "Remove any top-level
 * cv-qualifiers", and /9 for two references identical after that.
 */
int semantic_analysis::conversion_template_order(entity_index t1,
                                                 entity_index t2)
{
    const function_template ft1 =
        function_templates[entity_table[t1].template_record - 1];
    const function_template ft2 =
        function_templates[entity_table[t2].template_record - 1];
    qualified_type o1 = type_table[entity_table[t1].type.unqualified()].operand;
    qualified_type o2 = type_table[entity_table[t2].type.unqualified()].operand;
    unsigned cv1 = 0, cv2 = 0;
    qualified_type r1 = transformed(type_table, o1, cv1);
    qualified_type r2 = transformed(type_table, o2, cv2);
    owned_array<template_argument> v1, v2;
    owned_array<bool> s1, s2;
    for (uint32_t i = 0; i < ft1.parameter_count; i++) {
        v1.append(template_argument());
        s1.append(false);
    }
    for (uint32_t i = 0; i < ft2.parameter_count; i++) {
        v2.append(template_argument());
        s2.append(false);
    }
    bool two_from_one = deduce_type(r2, r1, v2, s2, true);
    bool one_from_two = deduce_type(r1, r2, v1, s1, true);
    if (two_from_one && one_from_two && is_reference(type_table, o1) &&
        is_reference(type_table, o2)) {
        /* /9: "if the type from the argument template was an lvalue
           reference and the type from the parameter template was not, the
           parameter type is not considered to be at least as specialized as
           the argument type; otherwise, if the type from the argument
           template is more cv-qualified than the type from the parameter
           template ..., the parameter type is not considered to be at least
           as specialized as the argument type" */
        bool l1 =
            type_table[o1.unqualified()].kind == type_kind::lvalue_reference;
        bool l2 =
            type_table[o2.unqualified()].kind == type_kind::lvalue_reference;
        if (l1 != l2) {
            /* the lvalue reference is the more specialized */
            two_from_one = l1;
            one_from_two = l2;
        } else if (more_cv(cv1, cv2) || more_cv(cv2, cv1)) {
            /* the more cv-qualified is the more specialized */
            two_from_one = more_cv(cv1, cv2);
            one_from_two = more_cv(cv2, cv1);
        }
    }
    if (two_from_one && !one_from_two)
        return 1;
    if (one_from_two && !two_from_one)
        return -1;
    return 0;
}

/* whether function template t's parameter i is a template parameter, by
   value or by reference */
bool semantic_analysis::template_parameter_alone(uint32_t t, uint32_t i) const
{
    qualified_type type = entity_table[function_templates[t - 1].entity].type;
    const struct type &r = type_table[type.unqualified()];
    if (r.kind != type_kind::function || i >= r.parameter_count)
        return false;
    unsigned cv = 0;
    qualified_type p = transformed(
        type_table, type_table.parameters(type.unqualified())[i], cv);
    return type_table[p.unqualified()].kind == type_kind::template_parameter;
}

int semantic_analysis::function_template_order(uint32_t f, uint32_t g,
                                               size_t arguments, bool &unknown,
                                               bool f_reversed, bool g_reversed)
{
    unknown = false;
    /* a non-type pack parameter or a specialization with a pack of
       arguments took part */
    bool packs_taken = false;
    /* the kinds of non-type parameter in the types: plain auto 1, other 2 */
    unsigned value_kinds = 0;
    const uint32_t both[2] = {f, g};
    for (uint32_t x : both) {
        const function_template t = function_templates[x - 1];
        /* [temp.deduct.partial]/12: a parameter no function parameter's
           type uses takes no part; where none of the types depends on a
           parameter (a constructor of a class specialization, templated by
           its defaulted parameters alone: tuple(const T&...)), a template
           template parameter or a non-type pack is no obstacle */
        bool dependent_types = false;
        qualified_type declared = entity_table[t.entity].type;
        const struct type d = type_table[declared.unqualified()];
        const qualified_type *types_of =
            type_table.parameters(declared.unqualified());
        for (uint32_t i = 0; i < d.parameter_count; i++)
            if (depends_on_template_parameter(types_of[i]))
                dependent_types = true;
        for (uint32_t i = 0; i < t.parameter_count; i++) {
            /* (a type-parameter, or an invented one, [dcl.fct]/22; a
               non-type parameter takes no part: [temp.deduct.partial]/12,
               "a template parameter may remain without a value provided
               it is not used in the types being used for partial
               ordering", and a use in a parameter type is a type orderable
               refuses) */
            template_parameter_parts form;
            if (!template_parameter_form(
                    template_parameter_nodes[t.first_parameter + i], form) ||
                (dependent_types && form.template_template))
                unknown = true;
            else if (dependent_types && !form.type && form.pack)
                packs_taken = true;
        }
        qualified_type type = entity_table[t.entity].type;
        const struct type r = type_table[type.unqualified()];
        const qualified_type *list = type_table.parameters(type.unqualified());
        if (r.kind != type_kind::function || r.variadic)
            unknown = true;
        for (uint32_t i = 0; i < r.parameter_count && !unknown; i++)
            /* (a non-deduced parameter itself: at_least_as_specialized
               compares nothing with it as P) */
            if (!non_deduced(list[i]) &&
                !member_of_unknown_specialization(list[i]) &&
                !orderable(list[i], true, &packs_taken, &value_kinds)) {
                /* (a class template's specialization with an expression
                   among its arguments, Vec128<T, 8 / sizeof(T)>: where
                   the other's parameter is a template parameter alone, it
                   is deduced from anything, and nothing of this one is
                   deduced from that parameter's unique type) */
                uint32_t other = f;
                if (x == f)
                    other = g;
                if (f_reversed || g_reversed ||
                    !template_parameter_alone(other, i))
                    unknown = true;
            }
    }
    if (unknown || value_kinds == 3) {
        unknown = true;
        return 0;
    }
    bool fg = at_least_as_specialized(f, g, arguments, unknown, f_reversed,
                                      g_reversed);
    bool gf = at_least_as_specialized(g, f, arguments, unknown, g_reversed,
                                      f_reversed);
    /* (a tie with packs of arguments is not claimed:
       [temp.deduct.type]/9's ignored pack expansion leaves clang to order
       such templates by CWG1432, which is open) */
    if (packs_taken && fg == gf)
        unknown = true;
    if (unknown)
        return 0;
    if (fg && !gf)
        return 1;
    if (gf && !fg)
        return -1;
    if (!fg)
        return 0;
    /* [temp.deduct.partial]/11: "If, after considering the above, function
       template F is at least as specialized as function template G and
       vice-versa, and if G has a trailing function parameter pack for which
       F does not have a corresponding parameter, and if F does not have a
       trailing function parameter pack, then F is more specialized than
       G." */
    auto trailing_pack = [&](uint32_t x, uint32_t &position) {
        qualified_type type =
            entity_table[function_templates[x - 1].entity].type;
        const struct type r = type_table[type.unqualified()];
        const qualified_type *list = type_table.parameters(type.unqualified());
        position = r.parameter_count;
        if (!r.parameter_count ||
            type_table[list[r.parameter_count - 1].unqualified()].kind !=
                type_kind::pack_expansion)
            return false;
        position = r.parameter_count - 1;
        return true;
    };
    uint32_t fcount = 0, gcount = 0;
    bool fpack = trailing_pack(f, fcount), gpack = trailing_pack(g, gcount);
    if (gpack && !fpack && fcount <= gcount)
        return 1;
    if (fpack && !gpack && gcount <= fcount)
        return -1;
    /* [temp.func.order]/6.2.2: else, with equivalent template-parameter-lists
       and function parameters, "if one is more constrained than the other
       ([temp.constr.order]), the more constrained template is more
       specialized": here a constrained template against one without
       constraints ([temp.constr.order]/1.2's subsumption of two sets of
       constraints is not analysed) */
    if (fcount != gcount || fpack != gpack ||
        function_templates[f - 1].parameter_count !=
            function_templates[g - 1].parameter_count)
        return 0;
    bool fc = template_head_constrained(function_templates[f - 1].declaration);
    bool gc = template_head_constrained(function_templates[g - 1].declaration);
    if (fc && gc)
        return constraint_subsumption_order(f, g);
    return static_cast<int>(fc) - static_cast<int>(gc);
}

/*
 * [temp.constr.order]: conjunction subsumption between two function
 * templates with equivalent template-parameter-lists and parameter types.
 * Each top-level conjunction atom is either a concept-id C<T_i> (from a
 * type-constraint or a requires-clause) or an opaque atom.  Template a
 * subsumes template b when every atom of b appears in a.
 */
int semantic_analysis::constraint_subsumption_order(uint32_t f,
                                                    uint32_t g) const
{
    struct atom {
        entity_index concept_entity = 0;
        uint32_t param_index = UINT32_MAX;
    };
    auto collect_atoms = [&](uint32_t tmpl, owned_array<atom> &out) {
        const function_template &ft = function_templates[tmpl - 1];
        entity_index scope = entity_table[ft.entity].enclosing;
        for (uint32_t i = 0; i < ft.parameter_count; i++) {
            template_parameter_parts parts;
            if (!template_parameter_form(
                    template_parameter_nodes[ft.first_parameter + i], parts) ||
                !parts.constraint)
                continue;
            SyntaxElement q = parts.constraint;
            if (q.size() == 1 && q[0].is_token()) {
                entity_index ce = constraint_concept(scope, q[0].token());
                out.append({ce, i});
            } else {
                out.append({0, UINT32_MAX});
            }
        }
        auto split = [&](auto &&self, SyntaxElement c) -> void {
            while (c.is(syntax_kind::primary_expression) && c.size() == 3 &&
                   c[0].is(preprocessing_op_or_punc::LEFT_PARENTHESIS))
                c = c[1];
            if (c.is(syntax_kind::constraint_logical_and_expression,
                     syntax_kind::logical_and_expression) &&
                c.size() == 3) {
                self(self, c[0]);
                self(self, c[2]);
                return;
            }
            if (c.is(syntax_kind::simple_template_id) && c.size() == 4 &&
                c[0].is_token()) {
                entity_index ce = constraint_concept(scope, c[0].token());
                SyntaxElement arg = c[2];
                while (arg.is_node() && arg.size() == 1 &&
                       arg.is(syntax_kind::type_id,
                              syntax_kind::type_specifier_seq))
                    arg = arg[0];
                if (ce && arg.is_token()) {
                    entity_index pe =
                        constraint_parameter(ft.entity, arg.token());
                    if (pe) {
                        out.append({ce, static_cast<uint32_t>(
                                            entity_table[pe].value)});
                        return;
                    }
                }
            }
            out.append({0, UINT32_MAX});
        };
        if (SyntaxElement hc = head_constraint(ft.declaration))
            split(split, hc);
        if (SyntaxElement tc = trailing_constraint(ft.declaration))
            split(split, tc);
    };
    owned_array<atom> fa, ga;
    collect_atoms(f, fa);
    collect_atoms(g, ga);
    auto subsumes = [](const owned_array<atom> &a, const owned_array<atom> &b) {
        for (const atom &x : b) {
            if (!x.concept_entity)
                return false;
            bool found = false;
            for (const atom &y : a) {
                if (y.concept_entity == x.concept_entity &&
                    y.param_index == x.param_index) {
                    found = true;
                    break;
                }
            }
            if (!found)
                return false;
        }
        return true;
    };
    bool f_sub_g = subsumes(fa, ga);
    bool g_sub_f = subsumes(ga, fa);
    if (f_sub_g && !g_sub_f)
        return 1;
    if (g_sub_f && !f_sub_g)
        return -1;
    return 0;
}

/* the scope the nested-name-specifier q nominates, from scope (its names
   looked up as members, through inline namespaces), scope if unknown */
entity_index semantic_analysis::constraint_qualifier(entity_index scope,
                                                     SyntaxElement q)
{
    entity_index from = 0;
    for (SyntaxElement x : q.elements()) {
        if (!x.is_identifier())
            continue;
        entity_index named = 0;
        if (from)
            named = lookup_qualified(from, tokens.identifier(x.token()),
                                     lookup_filter::any)
                        .entity;
        else
            named = constraint_name(scope, x.token());
        if (!named)
            return scope;
        from = named;
    }
    if (!from)
        return scope;
    return from;
}

entity_index semantic_analysis::constraint_member_concept(entity_index scope,
                                                          token_index i)
{
    entity_index e = constraint_concept(scope, i);
    if (!e)
        e = lookup_qualified(scope, tokens.identifier(i), lookup_filter::any)
                .entity;
    if (e && concept_of(e))
        return e;
    return 0;
}

/*
 * [temp.constr.normal]: the atomic constraints of the normal form of the
 * constraint-expression e, added to atoms: conjunctions split, concept-ids
 * replaced by their definitions, with env the parameter mapping (each name
 * in scope and what it stands for).  An atomic constraint is its expression
 * and its mapping ([temp.constr.atomic]/2).  A disjunction is kept as one
 * atom, which can only miss a subsumption.
 */
void semantic_analysis::constraint_atoms(
    SyntaxElement e, entity_index scope,
    const owned_array<constraint_binding> &env, owned_array<uint64_t> &atoms,
    unsigned depth)
{
    while (e.is(syntax_kind::primary_expression) && e.size() == 3 &&
           e[0].is(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        e = e[1];
    if (e.is(syntax_kind::logical_and_expression,
             syntax_kind::constraint_logical_and_expression) &&
        e.size() == 3) {
        constraint_atoms(e[0], scope, env, atoms, depth);
        constraint_atoms(e[2], scope, env, atoms, depth);
        return;
    }
    SyntaxElement id = e;
    entity_index from = scope;
    while (id.is_node() && !id.is(syntax_kind::simple_template_id) &&
           id.size()) {
        SyntaxElement last = id[id.size() - 1];
        if (id.size() == 2 && id[0].is(syntax_kind::nested_name_specifier)) {
            from = constraint_qualifier(from, id[0]);
        } else if (id.size() != 1) {
            break;
        }
        id = last;
    }
    if (id.is(syntax_kind::simple_template_id) && id.size() >= 4 &&
        id[0].is_token() && depth < 32) {
        entity_index named = constraint_member_concept(from, id[0].token());
        uint32_t ct = 0;
        if (named)
            ct = concept_of(named);
        if (ct) {
            owned_array<SyntaxElement> written;
            owned_array<bool> expansion;
            for (SyntaxElement x : id.elements())
                if (x.is(syntax_kind::template_argument_list))
                    expansion_list(x, written, expansion);
            if (written.empty())
                for (uint32_t i = 2; i + 1 < id.size(); i++)
                    written.append(id[i]);
            owned_array<uint64_t> arguments;
            for (const SyntaxElement &a : written)
                arguments.append(constraint_mapping(a, env));
            concept_atoms(ct, arguments, atoms, depth + 1);
            return;
        }
    }
    uint64_t key = 0;
    if (e.is_token())
        key = constraint_mapping(e, env);
    else
        key = (uint64_t(e.first_token().index()) * 0x9e3779b97f4a7c15ull) ^
              constraint_mapping(e, env);
    atoms.append(key);
}

/* the atoms of concept ct's definition with its parameters mapped to
   arguments ([temp.constr.normal]/1.4) */
void semantic_analysis::concept_atoms(uint32_t ct,
                                      const owned_array<uint64_t> &arguments,
                                      owned_array<uint64_t> &atoms,
                                      unsigned depth)
{
    const function_template c = function_templates[ct - 1];
    owned_array<constraint_binding> env;
    for (uint32_t i = 0; i < c.parameter_count && i < arguments.size(); i++) {
        entity_index p = template_parameter_entities[c.first_parameter + i];
        env.append(constraint_binding{entity_table[p].name, arguments[i]});
    }
    constraint_atoms(c.definition[3], entity_table[c.entity].enclosing, env,
                     atoms, depth);
}

/* the tokens of a, each name env binds replaced by what it stands for */
uint64_t semantic_analysis::constraint_mapping(
    SyntaxElement a, const owned_array<constraint_binding> &env) const
{
    uint64_t h = 0xcbf29ce484222325ull;
    token_index first = 0, end = 0;
    if (a.is_token()) {
        first = a.token();
        end = first + 1;
    } else {
        first = a.first_token();
        end = tree[a.index()].end_token;
    }
    bool alone = end == first + 1;
    for (token_index t = first; t < end; t++) {
        uint64_t x = static_cast<uint64_t>(tokens[t].kind);
        if (tokens[t].kind == token_kind::identifier) {
            uint32_t name = tokens.identifier(t);
            x = name;
            for (const constraint_binding &b : env)
                if (b.name == name) {
                    if (alone)
                        return b.value;
                    x = b.value;
                }
        } else if (tokens[t].kind == token_kind::operator_or_punctuator) {
            x = static_cast<uint64_t>(tokens[t].op_or_punc) << 8;
        }
        h = (h ^ x) * 0x100000001b3ull;
    }
    return h;
}

/* the atoms of partial specialization s's associated constraints
   ([temp.constr.decl]/3), its parameters mapped by position */
void semantic_analysis::specialization_atoms(uint32_t s,
                                             owned_array<uint64_t> &atoms)
{
    const function_template ft = function_templates[s - 1];
    entity_index scope = entity_table[ft.entity].enclosing;
    owned_array<constraint_binding> env;
    for (uint32_t i = 0; i < ft.parameter_count; i++) {
        entity_index p = template_parameter_entities[ft.first_parameter + i];
        env.append(constraint_binding{entity_table[p].name, i + 1});
    }
    for (uint32_t i = 0; i < ft.parameter_count; i++) {
        template_parameter_parts parts;
        if (!template_parameter_form(
                template_parameter_nodes[ft.first_parameter + i], parts) ||
            !parts.constraint)
            continue;
        SyntaxElement q = parts.constraint;
        uint32_t at = 0;
        entity_index from = scope;
        if (!q.is_token() && q.size() >= 2 &&
            q[0].is(syntax_kind::nested_name_specifier)) {
            at = 1;
            from = constraint_qualifier(from, q[0]);
        }
        SyntaxElement name = q;
        if (!q.is_token())
            name = q[at];
        uint32_t ct = 0;
        if (name.is_token()) {
            entity_index named = constraint_member_concept(from, name.token());
            if (named)
                ct = concept_of(named);
        }
        if (!ct) {
            atoms.append(constraint_mapping(q, env) ^ (uint64_t(i + 1) << 32));
            continue;
        }
        owned_array<uint64_t> arguments;
        arguments.append(i + 1);
        if (!q.is_token())
            for (uint32_t k = at + 2; k + 1 < q.size(); k++)
                arguments.append(constraint_mapping(q[k], env));
        concept_atoms(ct, arguments, atoms, 0);
    }
    if (SyntaxElement head = head_constraint(ft.declaration))
        constraint_atoms(head, scope, env, atoms, 0);
}

/* [temp.constr.order]: whether specialization s's constraints subsume t's
   and are not subsumed by them */
bool semantic_analysis::more_constrained_specialization(uint32_t s, uint32_t t)
{
    owned_array<uint64_t> a, b;
    specialization_atoms(s, a);
    specialization_atoms(t, b);
    auto subsumes = [](const owned_array<uint64_t> &p,
                       const owned_array<uint64_t> &q) {
        for (uint64_t x : q) {
            bool found = false;
            for (uint64_t y : p)
                found |= x == y;
            if (!found)
                return false;
        }
        return true;
    };
    return subsumes(a, b) && !subsumes(b, a);
}

/*
 * [temp.class.order]/1: "For two class template partial specializations,
 * the first is more specialized than the second if, given the following
 * rewrite to two function templates, the first function template is more
 * specialized than the second according to the ordering rules for function
 * templates": each a function of one parameter, the class template's
 * specialization for the partial specialization's arguments.  Deducing
 * one such parameter from the other ([temp.deduct.type]/9) is deducing
 * t's arguments from s's.
 */
bool semantic_analysis::class_at_least_as_specialized(uint32_t s, uint32_t t,
                                                      bool &unknown)
{
    const function_template ss = function_templates[s - 1];
    const function_template tt = function_templates[t - 1];
    if (ss.pattern_count != tt.pattern_count) {
        unknown = true;
        return false;
    }
    const uint32_t firsts[2] = {ss.first_pattern, tt.first_pattern};
    unsigned expansions[2] = {0, 0};
    /* the place of each side's trailing expansion: the argument and the
       number of elements before it */
    uint64_t places[2] = {0, 0};
    for (uint32_t i = 0; i < ss.pattern_count; i++)
        for (uint32_t side = 0; side < 2; side++) {
            const template_argument a = template_arguments[firsts[side] + i];
            if (a.kind == template_argument::kind_t::pack) {
                if (a.count == UINT32_MAX) {
                    unknown = true;
                    continue;
                }
                for (uint32_t j = 0; j < a.count; j++) {
                    const template_argument e = pack_arguments[a.value + j];
                    if (e.kind == template_argument::kind_t::pack &&
                        e.count == UINT32_MAX && j + 1 == a.count &&
                        e.value != UINT32_MAX) {
                        expansions[side]++;
                        places[side] = (uint64_t(i) << 32) | j;
                    } else if (e.kind != template_argument::kind_t::type ||
                               !orderable(e.type, false)) {
                        unknown = true;
                    }
                }
            } else if (a.kind == template_argument::kind_t::template_name ||
                       (a.kind == template_argument::kind_t::type &&
                        !orderable(a.type, false))) {
                unknown = true;
            }
        }
    /* A single trailing expansion can be deduced against a fixed pack; two
       bare ones after as many elements each deduce one another
       ([temp.deduct.type]/9).  Other shapes are not ordered here. */
    if (expansions[0] > 1 || expansions[1] > 1 ||
        (expansions[0] && expansions[1] && places[0] != places[1]))
        unknown = true;
    if (unknown)
        return false;
    owned_array<template_argument> d;
    owned_array<bool> set;
    for (uint32_t i = 0; i < tt.parameter_count; i++) {
        d.append(template_argument());
        set.append(false);
    }
    for (uint32_t i = 0; i < ss.pattern_count; i++) {
        const template_argument x = template_arguments[tt.first_pattern + i];
        const template_argument y = template_arguments[ss.first_pattern + i];
        if (x.kind != y.kind)
            return false;
        if (x.kind == template_argument::kind_t::pack) {
            if (!deduce_pack_argument(x, y, d, set))
                return false;
            continue;
        }
        if (x.kind == template_argument::kind_t::type) {
            if (!deduce_type(x.type, y.type, d, set, true))
                return false;
            continue;
        }
        if (!x.template_entity) {
            if (!(x == y))
                return false;
            continue;
        }
        if (x.value >= d.size()) {
            unknown = true;
            return false;
        }
        if (set[x.value]) {
            if (!(d[x.value] == y))
                return false;
            continue;
        }
        d[x.value] = y;
        set[x.value] = true;
    }
    return true;
}

} // namespace cplusplus
