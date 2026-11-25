/*
 * conversion-function.cpp: [class.conv.fct] Conversion functions.
 *
 *     conversion-function-id:
 *         operator conversion-type-id
 *
 *     conversion-type-id:
 *         type-specifier-seq conversion-declarator_opt
 *
 *     conversion-declarator:
 *         ptr-operator conversion-declarator_opt
 *
 * [class.conv.fct]/3: "The conversion-type-id shall not represent a
 * function type nor an array type.  The conversion-type-id in a
 * conversion-function-id is the longest sequence of tokens that could
 * possibly form a conversion-type-id."  So every ptr-operator after the
 * type-specifier-seq is the conversion-declarator's (operator int*() is
 * operator (int*) ()), and a '(' or '[' after it is not.  The
 * conversion-function-id itself is made by operator-function.cpp.
 *
 * Analysed: a conversion function declared or defined in its class (/1;
 * explicit, /2), one name for all of them told apart by type; the choice
 * of one by [over.match.conv] for an initialization of an object of
 * non-class type ([dcl.init]/17.7), a reference to one
 * ([dcl.init.ref]/5), a contextual conversion to bool and an implicit
 * conversion sequence of overload resolution ([over.ics.user]), also by
 * the conversion functions of the bases (/4: inherited, unless hidden).
 * Not analysed: a definition outside the class, a virtual one, conversion
 * function templates, [over.match.copy]'s and
 * [over.match.ref]'s candidate sets where they differ, and built-in
 * operator candidates ([over.built]) for an operand of such a class.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

node_index parser::conversion_type_id()
{
    size_t mark = begin_node();
    token_index first = position;
    decl_specifier_facts specifiers;
    specifiers.type_specifier_seq = true;
    /* CWG2413: a conversion-type-id is a type-id-only context (C++20,
       with [temp.res]/5) */
    specifiers.implicit_typename =
        tokens.files.behavior.has(language_feature::implicit_typename);
    decl_specifier_seq(specifiers, true);
    if (failed())
        return 0;
    if (!specifiers.node)
        return expected("a type-specifier-seq after operator",
                        "class.conv.fct");
    size_t declarator_mark = begin_node();
    unsigned operators = 0;
    while (at_ptr_operator()) {
        /* before C++11 (N2118) && is no ptr-operator: in
           &C::operator int && x it is the logical and */
        if (at(preprocessing_op_or_punc::AMPERSAND_AMPERSAND) &&
            !tokens.files.behavior.has(language_feature::rvalue_references))
            break;
        derivation kind = derivation::none;
        ptr_operator(&kind);
        if (failed())
            return 0;
        operators++;
    }
    /* conversion-declarator: ptr-operator conversion-declarator_opt, so
       each ptr-operator but the last has the rest as its
       conversion-declarator; one ptr-operator alone passes through */
    for (unsigned i = operators; i-- > 1;) {
        size_t from = declarator_mark + i - 1;
        finish_node(syntax_kind::conversion_declarator, from,
                    tree[pending[from].node()].first_token);
    }
    if (!operators && pending.size() == mark + 1)
        return 0; /* a type-specifier-seq alone passes through */
    return finish_node(syntax_kind::conversion_type_id, mark, first);
}

using sequence = semantic_analysis::conversion_sequence;

/* the spelling operator <conversion>, which no identifier has: the name
   of every conversion function, told apart by its type */
uint32_t semantic_analysis::conversion_function_name() const
{
    static const char rest[] = " <conversion>";
    return operator_identifier(rest, sizeof rest - 1);
}

/*
 * The type of the conversion-type-id of the conversion-function-id id: its
 * type-specifier-seq, then each ptr-operator of the conversion-declarator,
 * first to last.
 */
analysis_result semantic_analysis::conversion_type(SyntaxRef id,
                                                   qualified_type &type)
{
    if (id.size() != 2 || id[1].is_token())
        return not_supported(id, "class.conv.fct");
    auto t = id[1];
    SyntaxRef seq = t;
    if (t.is(syntax_kind::conversion_type_id))
        seq = t[0].index();
    specified s;
    VTRY(r, decl_specifier_seq(seq, s, false));
    if (!s.has_type || s.typedef_ || s.static_ || s.extern_ || s.inline_ ||
        s.constexpr_ || s.virtual_ || s.friend_ || s.gnu_flags || s.aligned)
        return not_supported(seq, "class.conv.fct");
    type = s.type;
    if (t == seq)
        return analysis_result::well_formed;
    /* the ptr-operators, in source order (a conversion-declarator nests
       the rest after its first) */
    owned_array<SyntaxRef> work;
    for (uint32_t i = t.size(); i-- > 1;)
        if (!t[i].is_token())
            work.append(t[i]);
    while (!work.empty()) {
        SyntaxRef k = work.back();
        work.resize(work.size() - 1);
        if (k.is(syntax_kind::conversion_declarator)) {
            for (uint32_t i = k.size(); i-- > 0;)
                if (!k[i].is_token())
                    work.append(k[i]);
            continue;
        }
        if (!k.is(syntax_kind::ptr_operator))
            return not_supported(k, "class.conv.fct");
        declarator_meaning m;
        VTRY_SET(r, pointer_operator(k, type, m));
    }
    return analysis_result::well_formed;
}

/*
 * [class.conv.fct]/1: "A member function of a class X having no parameters
 * with a name of the form
 *     conversion-function-id:
 *         operator conversion-type-id
 * specifies a conversion from X to the type specified by the
 * conversion-type-id.  Such functions are called conversion functions.  A
 * decl-specifier in the decl-specifier-seq of a conversion function (if
 * any) shall be neither a defining-type-specifier nor static.  The type of
 * the conversion function ([dcl.fct]) is "function taking no parameter
 * returning conversion-type-id"."  The declarators are the member's
 * (member_declarators), with the conversion-type-id's type as the
 * decl-specifier-seq's.
 */
analysis_result
semantic_analysis::conversion_function_member(entity_index c, SyntaxRef n,
                                              SyntaxRef id, uint8_t access,
                                              owned_array<SyntaxRef> &bodies)
{
    specified s;
    uint32_t first = 0;
    /* [ext:gnu_attributes] attributes before the declaration */
    uint32_t lead = leading_gnu_attributes(n);
    while (first < lead) {
        VTRY(r, gnu_attribute_specifier(n[first], s));
        first++;
    }
    if (first < n.size() && n[first].is(syntax_kind::decl_specifier_seq)) {
        auto seq = n[first];
        first++;
        bool outer_virtual = virtual_allowed, outer_explicit = explicit_allowed;
        virtual_allowed = explicit_allowed = true;
        declaration_access = access;
        analysis_result r = decl_specifier_seq(seq, s, false);
        virtual_allowed = outer_virtual;
        explicit_allowed = outer_explicit;
        declaration_access = 0;
        if (r != analysis_result::well_formed)
            return r;
        if (s.has_type)
            return ill_formed(seq,
                              "no return type can be specified for a "
                              "conversion function",
                              "class.conv.fct");
        if (s.static_)
            return ill_formed(n,
                              "a conversion function shall be a non-static "
                              "member function",
                              "class.conv.fct");
        if (s.typedef_ || s.extern_)
            return not_supported(n, "class.conv.fct");
        if (s.virtual_)
            /* [class.virtual]/2: an overrider is the conversion function
               to the same type, which virtual_member does not tell yet */
            return not_supported(n, "class.conv.fct");
    }
    qualified_type type;
    VTRY(r, conversion_type(id, type));
    s.type = type;
    s.has_type = true;
    return member_declarators(c, n, s, access, first, bodies);
}

/* [class.conv.fct]/1: the declarator-id is found in the declarator, not
   in the decl-specifier-seq, the parameters or the body */
/* [basic.lookup.unqual]/8: the names of the conversion-type-id of a
   qualified declaration (X<T>::operator valarray<X::result>) are looked up
   in the scope of X as well, where X is the injected-class-name.  The scope
   here is the template-parameters' of the member (a conversion function
   template's own T is found first, [temp.local]/7), so the class's scope is
   tried only when the names do not resolve without it */
analysis_result semantic_analysis::qualified_conversion_type(
    SyntaxRef declaration, SyntaxRef conversion, qualified_type &type)
{
    size_t unsupported_before = unsupported.size();
    analysis_result r = conversion_type(conversion, type);
    SyntaxElement q = DeclaratorParts::of_declaration(declaration).qualifier();
    if (r == analysis_result::well_formed || !q)
        return r;
    nominated_scope ns;
    VTRY(nr, nested_name_specifier(q.index(), ns));
    if (!is_class_entity(ns.entity))
        return r;
    unsupported.resize(unsupported_before);
    entity_index saved = current_scope;
    current_scope = ns.entity;
    r = conversion_type(conversion, type);
    current_scope = saved;
    return r;
}

SyntaxRef semantic_analysis::conversion_function_id_of(SyntaxRef n) const
{
    SyntaxElement id = DeclaratorParts::of_declaration(n).unqualified();
    if (!id.is(syntax_kind::conversion_function_id))
        return 0;
    return id.index();
}

entity_index semantic_analysis::own_conversion_functions(entity_index c) const
{
    return entity_table.find(c, conversion_function_name());
}

entity_index
semantic_analysis::conversion_function_declared_by(entity_index c,
                                                   SyntaxRef definition)
{
    for (entity_index f = own_conversion_functions(c); f;
         f = entity_table[f].next_overload)
        if (declared_by(f, definition))
            return f;
    return 0;
}

bool semantic_analysis::base_has_conversion_functions(entity_index c) const
{
    const entity &x = entity_table[c];
    for (uint32_t i = 0; i < x.base_count; i++)
        if (has_conversion_functions(
                type_table[entity_table[class_bases[x.first_base + i]]
                               .type.unqualified()]
                    .entity))
            return true;
    return false;
}

bool semantic_analysis::has_conversion_functions(entity_index c) const
{
    return entity_table[c].kind != entity_kind::class_type ||
           own_conversion_functions(c) || base_has_conversion_functions(c);
}

/*
 * [over.match.conv]/1: "A call to a conversion function returning
 * "reference to X" is a glvalue of type X"; [expr.call]/14: "A function
 * call is an lvalue if the result type is an lvalue reference type or an
 * rvalue reference to function type, an xvalue if the result type is an
 * rvalue reference to object type, and a prvalue otherwise."
 */
expression_meaning
semantic_analysis::conversion_function_result(entity_index f) const
{
    qualified_type result =
        type_table[entity_table[f].type.unqualified()].operand;
    expression_meaning value;
    value.type = result;
    value.category = value_category::prvalue;
    const struct type &r = type_table[result.unqualified()];
    if (r.kind == type_kind::lvalue_reference) {
        value.type = r.operand;
        value.category = value_category::lvalue;
    } else if (r.kind == type_kind::rvalue_reference) {
        value.type = r.operand;
        value.category = value_category::xvalue;
        if (type_table[r.operand.unqualified()].kind == type_kind::function)
            value.category = value_category::lvalue;
    }
    return value;
}

/*
 * [class.conv.fct]/4: "Conversion functions are inherited."  [class.member.lookup]:
 * a conversion function of a class hides those of its bases that convert to
 * the same type, which are no candidates of [over.match.conv]/1 ("not
 * hidden within S").  A function reached by two paths (a virtual base's) is
 * one candidate; a non-virtual base twice makes its derived-to-base
 * conversion ambiguous, which the conversion refuses.
 */
bool semantic_analysis::visible_conversion_functions(
    entity_index c, owned_array<entity_index> &out,
    owned_array<entity_index> *templates) const
{
    size_t first = out.size();
    for (entity_index f = own_conversion_functions(c); f;
         f = entity_table[f].next_overload) {
        /* [temp.mem]/1: a conversion function template, held under the
           name, needs [temp.deduct.conv] */
        if (!entity_table[f].conversion_function) {
            if (!templates || !member_function_template_of(f))
                return false;
            templates->append(f);
            continue;
        }
        if (templates && entity_table[f].member_templates)
            templates->append(entity_table[f].member_templates);
        out.append(f);
    }
    size_t own_end = out.size();
    for (uint32_t i = 0; i < entity_table[c].base_count; i++) {
        entity_index b =
            type_table[entity_table[class_bases[entity_table[c].first_base + i]]
                           .type.unqualified()]
                .entity;
        if (entity_table[b].kind != entity_kind::class_type)
            return false;
        owned_array<entity_index> inherited;
        if (!visible_conversion_functions(b, inherited, templates))
            return false;
        for (entity_index f : inherited) {
            /* the declared type: operator int and operator int& convert to
               different types, though both give an int */
            type_index to = type_table[entity_table[f].type.unqualified()]
                                .operand.unqualified();
            bool hidden = false;
            for (size_t k = first; k < own_end && !hidden; k++)
                hidden = type_table[entity_table[out[k]].type.unqualified()]
                             .operand.unqualified() == to;
            for (size_t k = own_end; k < out.size() && !hidden; k++)
                hidden = out[k] == f;
            if (!hidden)
                out.append(f);
        }
    }
    return true;
}

/*
 * [over.match.conv]/1: "The conversion functions of S and its base classes
 * are considered.  Those non-explicit conversion functions that are not
 * hidden within S and yield type T or a type that can be converted to type
 * T via a standard conversion sequence ([over.ics.scs]) are candidate
 * functions.  For direct-initialization, those explicit conversion
 * functions that are not hidden within S and yield type T or a type that
 * can be converted to type T with a qualification conversion ([conv.qual])
 * are also candidate functions."  /2: "The argument list has one argument,
 * which is the initializer expression."  [over.match.best]/2.2: then "the
 * context is an initialization by user-defined conversion ... and the
 * standard conversion sequence from the return type of F1 to the
 * destination type ... is a better conversion sequence than the standard
 * conversion sequence from the return type of F2".  A base's function takes
 * the object by a derived-to-base conversion (object_sequence, [over.ics.rank]/4.4).
 */
bool semantic_analysis::deduce_conversion_templates(
    const owned_array<entity_index> &conv_templates, qualified_type to,
    owned_array<entity_index> &candidates)
{
    /* [temp.deduct.conv]/1: "Template argument deduction is done by
       comparing the return type of the conversion function template (call
       it P) with the type that is required as the result of the conversion
       (call it A)"; /4: "If A is a cv-qualified type, the top-level
       cv-qualifiers of A's type are ignored for type deduction.  If A is a
       reference type, the type referred to by A is used for type
       deduction." */
    bool a_reference =
        type_table[to.unqualified()].kind == type_kind::lvalue_reference ||
        type_table[to.unqualified()].kind == type_kind::rvalue_reference;
    qualified_type dest = qualified_type::make(to.unqualified(), cv_none);
    if (a_reference)
        dest = type_table[to.unqualified()].operand;
    for (entity_index held : conv_templates) {
        owned_array<uint32_t> group;
        function_template_overloads(held, group);
        for (uint32_t g : group) {
            const function_template ft = function_templates[g - 1];
            qualified_type ret =
                type_table[entity_table[ft.entity].type.unqualified()].operand;
            /* /2: "If P is a reference type, the type referred to by P is
               used in place of P"; /3: "If A is not a reference type: ...
               If P is a cv-qualified type, the top-level cv-qualifiers of
               P's type are ignored for type deduction" (an array or a
               function, its other bullets, is no return type) */
            const struct type &rk = type_table[ret.unqualified()];
            if (rk.kind == type_kind::lvalue_reference ||
                rk.kind == type_kind::rvalue_reference)
                ret = rk.operand;
            if (!a_reference)
                ret = qualified_type::make(ret.unqualified(), cv_none);
            owned_array<template_argument> values;
            owned_array<bool> set;
            for (uint32_t i = 0; i < ft.parameter_count; i++) {
                values.append(template_argument());
                set.append(false);
            }
            if (!deduce_type(ret, dest, values, set, true))
                continue;
            entity_index spec = 0;
            substitution_mark mark = mark_substitution();
            substituting++;
            unevaluated++;
            analysis_result r =
                default_template_arguments(current_declaration, g, values, set);
            bool all_set = r == analysis_result::well_formed;
            for (bool s : set)
                if (!s)
                    all_set = false;
            if (all_set)
                r = instantiate(current_declaration, ft.entity, values.data(),
                                ft.parameter_count, spec);
            unevaluated--;
            substituting--;
            if (r == analysis_result::ill_formed && undo_substitution(mark))
                continue;
            if (r == analysis_result::well_formed && !all_set) {
                undo_substitution(mark);
                continue;
            }
            if (r != analysis_result::well_formed || !spec)
                return false;
            candidates.append(spec);
        }
    }
    return true;
}

entity_index
semantic_analysis::conversion_function_for(const expression_meaning &m,
                                           qualified_type to, bool direct,
                                           bool &ambiguous, bool &unknown)
{
    ambiguous = unknown = false;
    entity_index c = type_table[m.type.unqualified()].entity;
    if (entity_table[c].kind != entity_kind::class_type) {
        unknown = true;
        return 0;
    }
    owned_array<entity_index> candidates;
    owned_array<entity_index> conv_templates;
    if (!visible_conversion_functions(c, candidates, &conv_templates) ||
        !deduce_conversion_templates(conv_templates, to, candidates)) {
        unknown = true;
        return 0;
    }
    entity_index best = 0;
    sequence best_object, best_second;
    for (entity_index f : candidates) {
        const entity &x = entity_table[f];
        expression_meaning value = conversion_function_result(f);
        /* explicit: the type itself or a qualification conversion to it */
        if (x.explicit_ &&
            (!direct ||
             (value.type.unqualified() != to.unqualified() &&
              !qualification_convertible(
                  qualified_type::make(value.type.unqualified(), cv_none),
                  qualified_type::make(to.unqualified(), cv_none)))))
            continue;
        sequence object = object_sequence(m, f);
        if (object.kind != sequence::form::standard)
            continue;
        sequence second = implicit_conversion_sequence(value, to, false);
        if (second.kind == sequence::form::unknown) {
            unknown = true;
            return 0;
        }
        if (second.kind != sequence::form::standard)
            continue;
        if (!best) {
            best = f;
            best_object = object;
            best_second = second;
            ambiguous = false;
            continue;
        }
        int o = compare_conversions(object, best_object);
        int s = compare_conversions(second, best_second);
        int cmp = 0;
        if (o != 0)
            cmp = o;
        else if (s != 0)
            cmp = s;
        else if (!x.instantiation && entity_table[best].instantiation)
            /* [over.match.best]/2.4: non-template beats template */
            cmp = -1;
        else if (x.instantiation && !entity_table[best].instantiation)
            cmp = 1;
        else if (x.instantiation && entity_table[best].instantiation) {
            /* [over.match.best]/2.5: the more specialized template */
            entity_index t1 =
                instantiations[x.instantiation - 1].template_entity;
            entity_index t2 =
                instantiations[entity_table[best].instantiation - 1]
                    .template_entity;
            if (t1 != t2)
                cmp = -conversion_template_order(t1, t2);
        }
        if (cmp < 0) {
            best = f;
            best_object = object;
            best_second = second;
            ambiguous = false;
        } else if (cmp == 0) {
            ambiguous = true;
        }
    }
    if (ambiguous)
        return 0;
    return best;
}

bool semantic_analysis::result_destructible(qualified_type t) const
{
    const struct type &x = type_table[t.unqualified()];
    if (x.kind != type_kind::class_type)
        return true;
    entity_index d = entity_table[x.entity].destructor;
    return !d || (accessible(d, x.entity) && !entity_table[d].deleted);
}

/*
 * [dcl.init]/17.7: "Otherwise, if the source type is a (possibly
 * cv-qualified) class type, conversion functions are considered.  The
 * applicable conversion functions are enumerated ([over.match.conv]), and
 * the best one is chosen through overload resolution.  The user-defined
 * conversion so selected is called to convert the initializer expression
 * into the object being initialized."  converted is false when the class
 * has none that does; m is then unchanged.
 */
analysis_result
semantic_analysis::conversion_function_call(SyntaxRef at, expression_meaning &m,
                                            qualified_type to, bool direct,
                                            bool &converted)
{
    converted = false;
    bool ambiguous = false, unknown = false;
    entity_index f = conversion_function_for(m, to, direct, ambiguous, unknown);
    if (unknown)
        return not_supported(at, "over.match.conv");
    if (ambiguous)
        /* two conversion functions whose second standard conversion
           sequences [over.ics.rank]/3.2.1 may tell apart (a proper
           subsequence to another type), which compare_conversions does
           not: clang's ambiguity is not claimed */
        return not_supported(at, "over.ics.rank");
    if (!f)
        return analysis_result::well_formed;
    entity_index c = type_table[m.type.unqualified()].entity;
    if (!accessible(f, c))
        /* [class.access]/4 */
        return ill_formed(at,
                          "a private or protected member's name can be used "
                          "only by members and friends of the class in which "
                          "it is declared",
                          "class.access");
    token_index t = at.first_token();
    VTRY(r, deleted_use(t, f));
    VTRY_SET(r, need_specialization_definition(at, f));
    /* (a captureless lambda's closure object has no members: its
       temporary is the call's object argument, as for its call operator) */
    if (aggregate_prvalue(m) && !entity_table[c].lambda) {
        const char *why = nullptr;
        if (!at_block_scope() && !constant_only)
            why = "an aggregate prvalue at namespace scope";
        else if (entity_table[c].destructor)
            why = "an aggregate prvalue with a destructor";
        else if (entity_table[f].instantiation)
            why = "an aggregate prvalue, a conversion function template";
        else if (!result_destructible(to))
            why = "an aggregate prvalue, a result whose destructor is "
                  "inaccessible or deleted";
        if (why)
            return not_supported_because(at, "class.temporary", why);
    }
    if (m.category == value_category::prvalue && entity_table[c].destructor)
        return not_supported_because(
            at, "class.temporary",
            "a prvalue with a destructor (the temporary's destruction)");
    const entity &x = entity_table[f];
    expression_meaning address;
    address.type = qualified_type::make(type_table.pointer_to(m.type), cv_none);
    address.category = value_category::prvalue;
    address.evaluation = constness::not_constant;
    address.node = expression_node(semantic_kind::address, address, t, m.node);
    /* [over.match.funcs]/4-5, [class.conv.fct]/4: a function of a base class
       takes the object's base class subobject (the implied object argument's
       derived-to-base conversion) */
    entity_index owner = x.enclosing;
    if (owner != c) {
        qualified_type base_type = qualified_type::make(
            entity_table[owner].type.unqualified(), m.type.cv());
        qualified_type to_pointer =
            qualified_type::make(type_table.pointer_to(base_type), cv_none);
        VTRY_SET(r, pointer_to_base(t, c, owner, to_pointer, address));
    }
    expression_meaning callee;
    callee.type = x.type;
    callee.category = value_category::lvalue;
    callee.evaluation = constness::constant;
    callee.entity = f;
    owned_array<SyntaxElement> syntax;
    owned_array<expression_meaning> arguments;
    expression_meaning result;
    VTRY_SET(r, call_with(at, callee, x.type, address.node, f, syntax,
                          arguments, result));
    m = result;
    converted = true;
    return analysis_result::well_formed;
}

} // namespace cplusplus
