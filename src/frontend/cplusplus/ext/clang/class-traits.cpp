/*
 * class-traits.cpp: [ext:clang_type_traits] the traits of one type that ask
 * about a class's declaration ([meta.unary.prop]: is_abstract,
 * is_polymorphic, is_aggregate, is_empty, is_trivial, is_trivially_copyable,
 * is_trivially_destructible, has_virtual_destructor), from what the
 * analysis keeps of the class: its members and bases, its virtual
 * functions, its special members.  Those not decided here (is_final,
 * is_standard_layout, is_pod, has_unique_object_representations) are
 * unsupported.
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* [class.prop]/2 (an empty class): "no non-static data members other than
   bit-fields of size zero, no virtual functions, no virtual base classes,
   and no non-empty base classes" ([meta.unary.prop]: and not a union) */
bool semantic_analysis::empty_class(entity_index c) const
{
    const entity &x = entity_table[c];
    if (x.union_ || x.dynamic || x.virtual_base_count)
        return false;
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = entity_table[class_members[x.first_member + i]];
        if (m.base_subobject || m.kind != entity_kind::variable ||
            !m.nonstatic_member)
            continue;
        if (!m.bit_field || m.bit_width)
            return false;
    }
    for (uint32_t i = 0; i < x.base_count; i++)
        if (!empty_class(type_table[entity_table[class_bases[x.first_base + i]]
                                        .type.unqualified()]
                             .entity))
            return false;
    return true;
}

/* [class.copy.ctor]/14: a class whose implicit copy reads no value: no
   union, no scalar or array subobject */
bool semantic_analysis::copy_reads_nothing(entity_index c) const
{
    const entity &x = entity_table[c];
    if (x.union_ || x.dynamic || x.virtual_base_count)
        return false;
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = entity_table[class_members[x.first_member + i]];
        if (m.base_subobject || m.kind != entity_kind::variable ||
            !m.nonstatic_member)
            continue;
        const struct type &t = type_table[m.type.unqualified()];
        if (t.kind != type_kind::class_type || !copy_reads_nothing(t.entity))
            return false;
    }
    for (uint32_t i = 0; i < x.base_count; i++)
        if (!copy_reads_nothing(
                type_table[entity_table[class_bases[x.first_base + i]]
                               .type.unqualified()]
                    .entity))
            return false;
    return true;
}

/* [class.virtual]/1 (polymorphic): a class that declares or inherits a
   virtual function (a virtual base alone does not make one) */
bool semantic_analysis::polymorphic_class(entity_index c) const
{
    const entity &x = entity_table[c];
    if (const dynamic_class *d = dynamic_of(c))
        if (d->slot_count)
            return true;
    for (uint32_t i = 0; i < x.base_count; i++)
        if (polymorphic_class(
                type_table[entity_table[class_bases[x.first_base + i]]
                               .type.unqualified()]
                    .entity))
            return true;
    for (uint32_t i = 0; i < x.virtual_base_count; i++)
        if (polymorphic_class(
                type_table
                    [entity_table[class_virtual_bases[x.first_virtual_base + i]]
                         .type.unqualified()]
                        .entity))
            return true;
    return false;
}

/* whether a destructor is declared virtual in class c or in a base class
   ([class.dtor]/9: a destructor overriding a virtual one is virtual) */
bool semantic_analysis::virtual_destructor(entity_index c) const
{
    const entity &x = entity_table[c];
    if (x.destructor && entity_table[x.destructor].virtual_)
        return true;
    for (uint32_t i = 0; i < x.base_count; i++)
        if (virtual_destructor(
                type_table[entity_table[class_bases[x.first_base + i]]
                               .type.unqualified()]
                    .entity))
            return true;
    for (uint32_t i = 0; i < x.virtual_base_count; i++)
        if (virtual_destructor(
                type_table
                    [entity_table[class_virtual_bases[x.first_virtual_base + i]]
                         .type.unqualified()]
                        .entity))
            return true;
    return false;
}

/* whether class c, or a subobject's class, has what the analysis does not
   follow to its end in [class.prop]'s triviality: a deleted copy or move
   constructor or destructor (which clang counts as it does not
   the standard: "cwg1496: no"), a defaulted move */
bool semantic_analysis::triviality_unknown(entity_index c) const
{
    const entity &x = entity_table[c];
    if (x.implicit_copy_deleted || x.defaulted_move_constructor ||
        x.defaulted_move_assignment)
        return true;
    if (x.destructor && entity_table[x.destructor].deleted)
        return true;
    for (entity_index k = x.first_constructor; k;
         k = entity_table[k].next_constructor)
        if (entity_table[k].deleted && copy_or_move_constructor(c, k))
            return true;
    owned_array<entity_index> classes;
    subobject_classes(c, classes);
    for (entity_index k : classes)
        if (triviality_unknown(k))
            return true;
    return false;
}

/* whether member function f is declared = default in its class
   ([dcl.fct.def.default]/5: not user-provided on its first declaration) */
bool semantic_analysis::declared_defaulted(entity_index f) const
{
    uint32_t d = entity_table[f].first_declaration;
    if (!d)
        return false;
    auto definition = entity_table.declaration(d).node.function_definition();
    return definition && definition.function_body().defaulted();
}

/* [class.prop]/3 as clang answers __is_trivial: no user-provided
   constructor of the default or the copy and move kinds, no user-provided
   copy assignment or destructor, no virtual function, no default member
   initializer, in the class and its subobjects; a special member declared
   deleted or defaulted is trivial (trivial_special_members counts them) */
bool semantic_analysis::clang_trivial_special_members(entity_index c)
{
    const entity &x = entity_table[c];
    for (entity_index k = x.first_constructor; k;
         k = entity_table[k].next_constructor) {
        if (entity_table[k].implicit || declared_deleted(k) ||
            declared_defaulted(k))
            continue;
        const struct type &f = type_table[entity_table[k].type.unqualified()];
        if (!f.parameter_count || copy_or_move_constructor(c, k))
            return false;
    }
    if (x.dynamic || (x.destructor && !declared_deleted(x.destructor) &&
                      !declared_defaulted(x.destructor)))
        return false;
    lookup_result found = lookup_qualified(
        c, operator_name(preprocessing_op_or_punc::EQUAL), lookup_filter::any);
    if (found.unknown || found.ambiguous)
        return false;
    for (entity_index o = found.entity; o; o = entity_table[o].next_overload)
        if (entity_table[o].kind == entity_kind::function &&
            (copy_assignment_operator(o, c) ||
             move_assignment_operator(o, c)) &&
            !declared_deleted(o) && !declared_defaulted(o))
            return false;
    for (uint32_t i = 0; i < x.member_count; i++)
        if (entity_table[class_members[x.first_member + i]].default_initializer)
            return false;
    owned_array<entity_index> classes;
    subobject_classes(c, classes);
    for (entity_index k : classes)
        if (!clang_trivial_special_members(k))
            return false;
    return true;
}

/* [class.prop]/1: whether class c is trivially copyable; known is false
   when the analysis cannot tell.  [ext:clang_type_traits] clang's answer
   (CXXRecordDecl::isTriviallyCopyable): no non-trivial copy or move
   constructor or assignment operator and a trivial destructor, where a
   deleted special member is trivial (cwg1734 is not applied: a class whose
   copy and move are all deleted is trivially copyable) */
bool semantic_analysis::class_trivially_copyable(entity_index c, bool &known)
{
    known = true;
    const entity &x = entity_table[c];
    if (x.dynamic)
        return false;
    /* a user-provided destructor (a deleted one is trivial) */
    if (x.destructor && !declared_deleted(x.destructor) &&
        !declared_defaulted(x.destructor))
        return false;
    for (entity_index k = x.first_constructor; k;
         k = entity_table[k].next_constructor)
        if (copy_or_move_constructor(c, k) && !entity_table[k].implicit &&
            !declared_deleted(k) && !declared_defaulted(k) &&
            !entity_table[k].constraint_unsatisfied)
            return false;
    lookup_result found = lookup_qualified(
        c, operator_name(preprocessing_op_or_punc::EQUAL), lookup_filter::any);
    /* [class.copy.assign]/1: "A user-declared copy assignment operator
       X::operator= is a non-static non-template member function of class
       X": a name held by member function templates only (move_iterator's
       operator=(const move_iterator<U>&)) declares none, and leaves the
       implicit ones (as trivial_assignment, operator-function.cpp) */
    owned_array<entity_index> assignments;
    if (found.unknown && found.unknown_entity &&
        member_function_template_of(found.unknown_entity) && !found.ambiguous) {
        for (const template_name_function &named : template_name_functions)
            if (named.name == found.unknown_entity)
                assignments.append(named.function);
    } else if (found.unknown || found.ambiguous) {
        known = false;
        return false;
    } else {
        for (entity_index o = found.entity; o;
             o = entity_table[o].next_overload)
            assignments.append(o);
    }
    for (entity_index o : assignments)
        if (entity_table[o].kind == entity_kind::function &&
            (copy_assignment_operator(o, c) ||
             move_assignment_operator(o, c)) &&
            !declared_deleted(o) && !declared_defaulted(o))
            return false;
    owned_array<entity_index> classes;
    subobject_classes(c, classes);
    for (entity_index k : classes) {
        if (entity_table[k].kind != entity_kind::class_type) {
            known = false;
            return false;
        }
        if (!class_trivially_copyable(k, known) || !known)
            return false;
    }
    return true;
}

/* [ext:clang_type_traits] __has_trivial_constructor and __is_trivial on a
   class with a constructor template: clang does not answer by the
   constructor [over.match.best] selects (llvm-project issue 59206); not
   followed */
static bool clang_template_default_constructor_of(const entity &x)
{
    return x.constructor_template || x.constructor_templates;
}

/* [class.derived]/2: whether class d has class b among its direct or
   indirect base classes, virtual ones included */
bool semantic_analysis::derives_from(entity_index d, entity_index b) const
{
    const entity &x = entity_table[d];
    for (uint32_t i = 0; i < x.base_count; i++) {
        entity_index k = type_table[entity_table[class_bases[x.first_base + i]]
                                        .type.unqualified()]
                             .entity;
        if (k == b || derives_from(k, b))
            return true;
    }
    for (uint32_t i = 0; i < x.virtual_base_count; i++) {
        entity_index k =
            type_table
                [entity_table[class_virtual_bases[x.first_virtual_base + i]]
                     .type.unqualified()]
                    .entity;
        if (k == b || derives_from(k, b))
            return true;
    }
    return false;
}

/* ( type-id , type-id ): __is_base_of(Base, Derived), [meta.rel] is_base_of:
   "Base is a base class of Derived, or both are the same non-union class
   type" (cv-qualification ignored); a type that is no class is none.  A
   Derived that is an incomplete class other than Base is ill-formed
   ([meta.rel]/5: "complete type"), left unsupported. */
analysis_result semantic_analysis::clang_base_of_trait(SyntaxRef n,
                                                       bool virtual_base,
                                                       expression_meaning &m,
                                                       bool interconvertible)
{
    qualified_type b, d;
    VTRY(r, type_id(n[2], b));
    VTRY_SET(r, type_id(n[4], d));
    if (depends_on_template_parameter(b) || depends_on_template_parameter(d))
        return not_supported_because(n, "temp.dep.constexpr",
                                     "a base-of trait of a dependent type");
    const struct type bk = type_table[b.unqualified()];
    const struct type dk = type_table[d.unqualified()];
    bool value = false;
    if (bk.kind == type_kind::class_type && dk.kind == type_kind::class_type) {
        if (entity_table[bk.entity].kind != entity_kind::class_type ||
            entity_table[dk.entity].kind != entity_kind::class_type)
            return not_supported_because(
                n, "meta.rel",
                "a base-of trait on a class type whose entity is not a class");
        if (virtual_base) {
            /* __builtin_is_virtual_base_of(B, D): B is one of D's virtual
               bases, direct or indirect ([class.mi]/4); a class is none of
               its own */
            require_complete(dk.entity);
            if (!entity_table[dk.entity].size)
                return not_supported_because(n, "meta.rel",
                                             "a virtual-base-of trait with a "
                                             "derived class that has no size");
            const entity &x = entity_table[dk.entity];
            for (uint32_t i = 0; i < x.virtual_base_count; i++)
                if (type_table[entity_table[class_virtual_bases
                                                [x.first_virtual_base + i]]
                                   .type.unqualified()]
                        .entity == bk.entity)
                    value = true;
        } else if (bk.entity == dk.entity) {
            value = !entity_table[bk.entity].union_;
        } else {
            require_complete(dk.entity);
            if (!entity_table[dk.entity].size)
                return not_supported_because(
                    n, "meta.rel",
                    "a base-of trait with a derived class that has no size");
            value = derives_from(dk.entity, bk.entity);
        }
        /* [meta.rel] is_pointer_interconvertible_base_of: "Derived is
           a standard-layout class and Base is Derived or one of its
           base classes" ([basic.compound]/4.3's chain of first
           members), what clang answers: a class with data in two of
           its bases is not standard-layout (not by this rule) */
        if (interconvertible && value) {
            bool layout = false;
            if (!class_property_trait(int(unary_trait_kind::is_standard_layout),
                                      d, layout))
                return not_supported_because(
                    n, "meta.rel",
                    "a pointer-interconvertible base trait with unknown "
                    "standard-layout property");
            value = layout;
        }
    }
    m = integer_constant(fundamental_type::bool_, value, n.first_token());
    return analysis_result::well_formed;
}

/* the traits; known is false when the analysis cannot tell */
bool semantic_analysis::class_property_trait(int which, qualified_type t,
                                             bool &answer)
{
    bool const_qualified = (t.cv() & cv_const) != 0;
    t = qualified_type::make(t.unqualified(), cv_none);
    /* an array of T is as T for what follows ([basic.types]/9) */
    type_kind kind = type_table[t.unqualified()].kind;
    bool array = kind == type_kind::array;
    bool unbounded = array && !type_table[t.unqualified()].bound_known;
    while (kind == type_kind::array) {
        t = type_table[t.unqualified()].operand;
        if (t.cv() & cv_const)
            const_qualified = true;
        t = qualified_type::make(t.unqualified(), cv_none);
        kind = type_table[t.unqualified()].kind;
    }
    bool is_class = kind == type_kind::class_type;
    entity_index c = 0;
    if (is_class) {
        c = type_table[t.unqualified()].entity;
        require_complete(c);
        if (!entity_table[c].size)
            return false;
    }
    answer = false;
    switch (static_cast<unary_trait_kind>(which)) {
    case unary_trait_kind::is_abstract:
        answer = !array && is_class && abstract_class(t);
        return true;
    case unary_trait_kind::is_final:
        answer = !array && is_class && entity_table[c].final_;
        return true;
    case unary_trait_kind::is_standard_layout: {
        /* [class.prop]/7: a scalar type is a standard-layout type
           ([basic.types]/9) */
        if (!is_class) {
            answer = kind != type_kind::function &&
                     kind != type_kind::lvalue_reference &&
                     kind != type_kind::rvalue_reference &&
                     fundamental_of(t) != fundamental_type::void_;
            return true;
        }
        return standard_layout_class(c, answer);
    }
    /* a POD: trivial and standard-layout ([class.prop]); a scalar is */
    case unary_trait_kind::is_pod: {
        if (unbounded)
            return false;
        bool trivial = false, layout = false;
        if (!class_property_trait(int(unary_trait_kind::is_trivial), t,
                                  trivial) ||
            !class_property_trait(int(unary_trait_kind::is_standard_layout), t,
                                  layout))
            return false;
        answer = trivial && layout;
        return true;
    }
    /* [basic.types]/10 (literal_type), "unknown" is not answered */
    case unary_trait_kind::is_literal: {
        constness literal = literal_type(t);
        if (literal == constness::unknown)
            return false;
        answer = literal == constness::constant;
        return true;
    }
    /* [ext:clang_type_traits] __has_trivial_constructor, _copy, _assign:
       what clang answers for a scalar (true; an assignment of a const
       one, not) and a class (its trivial default constructor, copy
       constructor, copy assignment operator) */
    case unary_trait_kind::has_trivial_constructor:
    case unary_trait_kind::has_trivial_copy:
    case unary_trait_kind::has_trivial_assign: {
        if (unbounded || kind == type_kind::function ||
            kind == type_kind::lvalue_reference ||
            kind == type_kind::rvalue_reference ||
            fundamental_of(t) == fundamental_type::void_)
            return false;
        if (!is_class) {
            answer = true;
            if (static_cast<unary_trait_kind>(which) ==
                    unary_trait_kind::has_trivial_assign &&
                const_qualified)
                answer = false;
            return true;
        }
        if (triviality_unknown(c))
            return false;
        /* a declared move constructor or assignment deletes the implicit
           copy, which clang still answers for as it does not the
           standard: not followed */
        if (static_cast<unary_trait_kind>(which) !=
                unary_trait_kind::has_trivial_constructor &&
            (declares_move_constructor(c) || declares_move_assignment(c)))
            return false;
        switch (static_cast<unary_trait_kind>(which)) {
        case unary_trait_kind::has_trivial_constructor: {
            if (clang_template_default_constructor_of(entity_table[c]))
                return false;
            bool known = true;
            answer = trivial_default_constructor(c, known);
            if (!known)
                return false;
            break;
        }
        case unary_trait_kind::has_trivial_copy:
            answer = copies_bytes(c);
            break;
        default:
            answer = assigned_bitwise(c);
            break;
        }
        return true;
    }
    /* [ext:clang_type_traits] the deprecated __has_trivial_move_* and
       __has_nothrow_* traits.  A scalar: true (an assignment of a const
       one, not).  A class: what clang answers by its implicit special
       members; one that declares its own copy or move members, or has a
       non-trivial subobject, is not followed (clang's answer there is
       by the declared exception specifications and by overload
       resolution of the copy and move members) */
    case unary_trait_kind::has_trivial_move_constructor:
    case unary_trait_kind::has_trivial_move_assign:
    case unary_trait_kind::has_nothrow_copy:
    case unary_trait_kind::has_nothrow_assign:
    case unary_trait_kind::has_nothrow_constructor:
    case unary_trait_kind::has_nothrow_move_assign: {
        unary_trait_kind w = static_cast<unary_trait_kind>(which);
        if (unbounded || kind == type_kind::function ||
            kind == type_kind::lvalue_reference ||
            kind == type_kind::rvalue_reference ||
            fundamental_of(t) == fundamental_type::void_)
            return false;
        bool assign = w == unary_trait_kind::has_nothrow_assign;
        if (!is_class) {
            answer = !(assign && const_qualified);
            return true;
        }
        if (triviality_unknown(c))
            return false;
        const entity &x = entity_table[c];
        if (declares_move_constructor(c) || declares_move_assignment(c) ||
            declares_copy_constructor(c) || x.user_copy_assignments)
            return false;
        bool declared_destructor =
            x.destructor && !entity_table[x.destructor].implicit;
        switch (w) {
        case unary_trait_kind::has_trivial_move_constructor:
        case unary_trait_kind::has_trivial_move_assign:
        case unary_trait_kind::has_nothrow_move_assign:
            /* a user-declared destructor leaves no implicit move */
            if (declared_destructor)
                return false;
            break;
        default:
            break;
        }
        switch (w) {
        case unary_trait_kind::has_trivial_move_constructor:
        case unary_trait_kind::has_nothrow_copy:
            answer = copies_bytes(c);
            break;
        case unary_trait_kind::has_nothrow_constructor: {
            if (clang_template_default_constructor_of(x))
                return false;
            bool known = true;
            answer = trivial_default_constructor(c, known);
            if (!known)
                return false;
            break;
        }
        default:
            if (assign && const_qualified) {
                answer = false;
                return true;
            }
            answer = assigned_bitwise(c);
            break;
        }
        /* a non-trivial answer rests on exception specifications */
        return answer;
    }
    case unary_trait_kind::has_unique_object_representations:
        if (unbounded)
            return false;
        if (kind == type_kind::lvalue_reference ||
            kind == type_kind::rvalue_reference) {
            answer = false;
            return true;
        }
        return unique_representation(t, answer);
    /* [ext:clang_type_traits] __is_trivially_relocatable (clang's own
       rule) and __builtin_is_cpp_trivially_relocatable ([class.prop] of
       C++26, P2786): an array is as its element */
    case unary_trait_kind::is_trivially_relocatable:
    case unary_trait_kind::is_cpp_trivially_relocatable: {
        bool known = true;
        answer =
            relocatable_type(t,
                             static_cast<unary_trait_kind>(which) ==
                                 unary_trait_kind::is_cpp_trivially_relocatable,
                             known);
        return known;
    }
    /* clang: not an array or an enumeration; a class needs a defaulted
       operator==, which is not followed; else the type has unique object
       representations without being trivially copyable */
    case unary_trait_kind::is_trivially_equality_comparable: {
        if (array)
            return true;
        if (is_class)
            return false;
        /* an enumeration is, unless an operator== of its namespace may
           take it: not followed */
        if (kind == type_kind::enumeration) {
            entity_index space =
                entity_table[type_table[t.unqualified()].entity].enclosing;
            if (entity_table.find(
                    space,
                    operator_name(preprocessing_op_or_punc::EQUAL_EQUAL)))
                return false;
            answer = true;
            return true;
        }
        if (kind == type_kind::function ||
            kind == type_kind::lvalue_reference ||
            kind == type_kind::rvalue_reference ||
            fundamental_of(t) == fundamental_type::void_) {
            answer = false;
            return true;
        }
        return unique_representation(t, answer);
    }
    case unary_trait_kind::is_polymorphic:
        answer = !array && is_class && polymorphic_class(c);
        return true;
    case unary_trait_kind::is_aggregate:
        answer = array || (is_class && is_aggregate(c));
        return true;
    case unary_trait_kind::is_empty:
        answer = !array && is_class && empty_class(c);
        return true;
    case unary_trait_kind::has_virtual_destructor:
        answer = !array && is_class && virtual_destructor(c);
        return true;
    case unary_trait_kind::is_trivial:
        /* [class.prop]/3: trivially copyable with a trivial default
           constructor; a scalar is trivial ([basic.types]/9) */
        if (!is_class) {
            answer = kind != type_kind::function &&
                     kind != type_kind::lvalue_reference &&
                     kind != type_kind::rvalue_reference &&
                     fundamental_of(t) != fundamental_type::void_;
            return true;
        }
        {
            bool known = true;
            bool copyable = class_trivially_copyable(c, known);
            if (!known)
                return false;
            if (clang_template_default_constructor_of(entity_table[c]))
                return false;
            bool known_default = true;
            answer = copyable && clang_trivial_special_members(c) &&
                     trivial_default_constructor(c, known_default);
            if (!known_default)
                return false;
        }
        return true;
    case unary_trait_kind::is_trivially_copyable:
        if (!is_class) {
            answer = kind != type_kind::function &&
                     kind != type_kind::lvalue_reference &&
                     kind != type_kind::rvalue_reference &&
                     fundamental_of(t) != fundamental_type::void_;
            return true;
        }
        {
            bool known = true;
            answer = class_trivially_copyable(c, known);
            if (!known)
                return false;
        }
        return true;
    case unary_trait_kind::is_trivially_destructible:
    case unary_trait_kind::has_trivial_destructor:
        if (kind == type_kind::function ||
            fundamental_of(t) == fundamental_type::void_)
            return true;
        /* is_destructible: an array of unknown bound is not */
        if (unbounded && static_cast<unary_trait_kind>(which) ==
                             unary_trait_kind::is_trivially_destructible)
            return true;
        if (!is_class) {
            answer = true;
            return true;
        }
        {
            bool throws = false;
            answer = class_destructible(c, throws) && !destroys(c);
        }
        return true;
    /* [meta.unary.prop] is_destructible: "the expression declval<U&>().~U()
       is well-formed when treated as an unevaluated operand" for U the
       type without its array extents or reference ("T is a reference
       type: true; void, a function, an array of unknown bound: false");
       is_nothrow_destructible: and the destructor does not throw */
    case unary_trait_kind::is_destructible:
    case unary_trait_kind::is_nothrow_destructible:
        if (kind == type_kind::function ||
            fundamental_of(t) == fundamental_type::void_ || unbounded)
            return true;
        if (!is_class) {
            answer = true;
            return true;
        }
        {
            bool throws = false;
            answer = class_destructible(c, throws);
            if (static_cast<unary_trait_kind>(which) ==
                    unary_trait_kind::is_nothrow_destructible &&
                throws)
                answer = false;
        }
        return true;
    default:
        return false;
    }
}

/* the direct and indirect base classes of c (non-virtual ones: a class
   with a virtual base is no standard-layout class) */
void semantic_analysis::base_classes_of(entity_index c,
                                        owned_array<entity_index> &out) const
{
    const entity &x = entity_table[c];
    for (uint32_t i = 0; i < x.base_count; i++) {
        entity_index b = type_table[entity_table[class_bases[x.first_base + i]]
                                        .type.unqualified()]
                             .entity;
        out.append(b);
        base_classes_of(b, out);
    }
}

/* [class.prop]/3: the class that declares c's (possibly inherited)
   first non-static data member, 0 if none; false when two classes of the
   hierarchy declare some (not standard-layout) */
bool semantic_analysis::first_data_class(entity_index c,
                                         entity_index &owner) const
{
    owner = 0;
    owned_array<entity_index> all;
    all.append(c);
    base_classes_of(c, all);
    uint32_t declaring = 0;
    for (entity_index k : all) {
        const entity &x = entity_table[k];
        bool has = false;
        for (uint32_t i = 0; i < x.member_count; i++) {
            const entity &m = entity_table[class_members[x.first_member + i]];
            if (m.kind == entity_kind::variable && m.nonstatic_member &&
                !m.base_subobject)
                has = true;
        }
        if (has) {
            owner = k;
            declaring++;
        }
    }
    return declaring <= 1;
}

/* [class.prop]/3's M(X) for the standard-layout rule: the types of the
   first non-static data member of X (and of a member of zero size), and
   of those, recursively (an
   array's element type is included); a union's, of every member */
void semantic_analysis::layout_member_types(entity_index c,
                                            owned_array<type_index> &out)
{
    auto add = [&](auto &&self, qualified_type t) -> void {
        t = qualified_type::make(t.unqualified(), cv_none);
        while (type_table[t.unqualified()].kind == type_kind::array) {
            t = type_table[t.unqualified()].operand;
            t = qualified_type::make(t.unqualified(), cv_none);
        }
        out.append(t.unqualified());
        if (type_table[t.unqualified()].kind == type_kind::class_type)
            layout_member_types(type_table[t.unqualified()].entity, out);
        (void)self;
    };
    const entity &x = entity_table[c];
    if (x.union_) {
        for (uint32_t i = 0; i < x.member_count; i++) {
            const entity &m = entity_table[class_members[x.first_member + i]];
            if (m.kind == entity_kind::variable && m.nonstatic_member &&
                !m.base_subobject)
                add(add, m.type);
        }
        return;
    }
    entity_index owner = 0;
    first_data_class(c, owner);
    if (!owner)
        return;
    /* [class.bit]/2: "An unnamed bit-field is not a member": the first
       member is the first named one.  [intro.object]/8: a member of zero
       size ([[no_unique_address]] of an empty class) may be at offset zero
       wherever it is declared, and is in M(X) too (clang's IsZeroSize) */
    const entity &o = entity_table[owner];
    bool first = true;
    for (uint32_t i = 0; i < o.member_count; i++) {
        const entity &m = entity_table[class_members[o.first_member + i]];
        if (m.kind != entity_kind::variable || !m.nonstatic_member ||
            m.base_subobject || (m.bit_field && !m.name))
            continue;
        const type &mt = type_table[m.type.unqualified()];
        bool zero_size = m.no_unique_address &&
                         mt.kind == type_kind::class_type &&
                         empty_class(mt.entity);
        if (first || zero_size)
            add(add, m.type);
        first = false;
    }
}

/* [class.prop]/3: a standard-layout class has no virtual functions or
   virtual bases, no non-static data member of reference or
   non-standard-layout type, the same access control for all its
   non-static data members, no non-standard-layout base class, all its
   non-static data members (and its bases') first declared in one class,
   and no base class of a type in M(S).  false: not analysed */
bool semantic_analysis::standard_layout_class(entity_index c, bool &answer)
{
    const entity &x = entity_table[c];
    answer = false;
    if (x.dynamic || x.virtual_base_count)
        return true;
    uint8_t first_access = 0xff;
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = entity_table[class_members[x.first_member + i]];
        if (m.kind != entity_kind::variable || !m.nonstatic_member ||
            m.base_subobject)
            continue;
        type_kind mk = type_table[m.type.unqualified()].kind;
        if (mk == type_kind::lvalue_reference ||
            mk == type_kind::rvalue_reference)
            return true;
        if (first_access == 0xff)
            first_access = m.access;
        else if (m.access != first_access)
            return true;
        bool sub = false;
        if (!class_property_trait(int(unary_trait_kind::is_standard_layout),
                                  m.type, sub))
            return false;
        if (!sub)
            return true;
    }
    for (uint32_t i = 0; i < x.base_count; i++) {
        const entity &b = entity_table[class_bases[x.first_base + i]];
        bool sub = false;
        if (!class_property_trait(int(unary_trait_kind::is_standard_layout),
                                  b.type, sub))
            return false;
        if (!sub)
            return true;
    }
    if (x.base_count) {
        entity_index owner = 0;
        if (!first_data_class(c, owner))
            return true;
        /* two base class subobjects of one type cannot have different
           addresses */
        owned_array<entity_index> every;
        base_classes_of(c, every);
        for (size_t i = 0; i < every.size(); i++)
            for (size_t j = i + 1; j < every.size(); j++)
                if (every[i] == every[j])
                    return true;
        /* a base's access also counts: all data members of the hierarchy
           have one access control ([class.prop]/3.2) */
        owned_array<type_index> forbidden;
        layout_member_types(c, forbidden);
        owned_array<entity_index> classes;
        base_classes_of(c, classes);
        for (entity_index k : classes)
            for (type_index f : forbidden)
                if (entity_table[k].type.unqualified() == f)
                    return true;
        for (entity_index k : classes) {
            const entity &kx = entity_table[k];
            for (uint32_t i = 0; i < kx.member_count; i++) {
                const entity &m =
                    entity_table[class_members[kx.first_member + i]];
                if (m.kind != entity_kind::variable || !m.nonstatic_member ||
                    m.base_subobject)
                    continue;
                if (first_access == 0xff)
                    first_access = m.access;
                else if (m.access != first_access)
                    return true;
            }
        }
    }
    answer = true;
    return true;
}

/*
 * [meta.unary.prop] has_unique_object_representations: "T is trivially
 * copyable, and any two objects of type T with the same value have the same
 * object representation" ([basic.types]/9 for the value): an integer,
 * pointer or enumeration type has; a floating-point one has not; an array of
 * such; a class, when it is trivially copyable, is no dynamic class, and its
 * bases and members all have, with no padding between or after them (their
 * bytes cover the class's size); a union, when each member has and fills
 * it; a bit-field counts its width.  false: not analysed (an array of
 * unknown bound).
 */
bool semantic_analysis::unique_representation(qualified_type t, bool &answer)
{
    t = qualified_type::make(t.unqualified(), cv_none);
    while (type_table[t.unqualified()].kind == type_kind::array) {
        if (!type_table[t.unqualified()].bound_known)
            return false;
        t = type_table[t.unqualified()].operand;
        t = qualified_type::make(t.unqualified(), cv_none);
    }
    const struct type &record = type_table[t.unqualified()];
    answer = false;
    switch (record.kind) {
    case type_kind::enumeration:
    case type_kind::pointer:
    case type_kind::pointer_to_member:
        answer = true;
        return true;
    /* [ext:clang_type_traits] an atomic, complex or vector type has not
       (clang++) */
    case type_kind::atomic:
    case type_kind::complex:
    case type_kind::vector:
    case type_kind::ext_vector:
        return true;
    case type_kind::fundamental:
        if (is_integral(t)) {
            answer = true;
            return true;
        }
        /* a floating-point type has two representations of a zero */
        if (is_floating(t))
            return true;
        /* void and std::nullptr_t: no object representation of a value
           ([ext:clang_type_traits]) */
        return fundamental_of(t) == fundamental_type::void_ ||
               fundamental_of(t) == fundamental_type::std_nullptr_t;
    case type_kind::bit_int:
        /* [ext:clang_type_traits] _BitInt(N): unique when its bits fill
           its bytes, as clang's isUniqueObjRep: N == 8 * the size */
        if (!record.bound)
            return false;
        answer = record.bound == 8 * bit_int_layout(record.bound, false);
        return true;
    case type_kind::function:
        /* no object of the type */
        return true;
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference:
        /* a reference member is as a pointer ("References are still
           unique", clang's test); the trait of a reference type is false,
           where the trait's case answers it */
        answer = true;
        return true;
    case type_kind::class_type:
        break;
    default:
        return false;
    }
    bool unique = false;
    uint64_t extent = 0;
    if (!unique_class_extent(record.entity, unique, extent))
        return false;
    answer = unique && extent == 8 * entity_table[record.entity].size;
    return true;
}

/*
 * clang's structHasUniqueObjectRepresentations: whether the class's bases
 * and members all have unique representations and cover its bytes from
 * the first with no gap; `extent` is the bits they cover (a base's or a
 * [[no_unique_address]] member's tail padding is left to what follows it,
 * the caller compares the whole).  false: not analysed.
 */
bool semantic_analysis::unique_class_extent(entity_index c, bool &unique,
                                            uint64_t &extent)
{
    unique = false;
    extent = 0;
    require_complete(c);
    const entity &x = entity_table[c];
    if (!x.size)
        return false;
    bool known = true;
    bool copyable = class_trivially_copyable(c, known);
    if (!known)
        return false;
    if (!copyable || x.dynamic || x.virtual_base_count)
        return true;
    struct piece {
        uint64_t offset, size;
    };
    owned_array<piece> pieces;
    bool pieces_in_union = false;
    for (uint32_t i = 0; i < x.base_count; i++) {
        const entity &b = entity_table[class_bases[x.first_base + i]];
        entity_index bc = type_table[b.type.unqualified()].entity;
        /* an empty base has no bytes of its own */
        if (empty_class(bc))
            continue;
        bool sub = false;
        uint64_t covered = 0;
        if (!unique_class_extent(bc, sub, covered))
            return false;
        /* clang: a base without unique representations has the class
           without */
        if (!sub)
            return true;
        pieces.append(piece{8 * b.offset, covered});
    }
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = entity_table[class_members[x.first_member + i]];
        if (m.kind != entity_kind::variable || !m.nonstatic_member ||
            m.base_subobject)
            continue;
        /* an unnamed bit-field is padding */
        if (m.bit_field && !m.name)
            continue;
        /* an empty [[no_unique_address]] member has no bytes */
        if (m.no_unique_address &&
            type_table[m.type.unqualified()].kind == type_kind::class_type &&
            empty_class(type_table[m.type.unqualified()].entity))
            continue;
        bool sub = false;
        uint64_t covered = 0;
        bool overlapping =
            m.no_unique_address &&
            type_table[m.type.unqualified()].kind == type_kind::class_type;
        if (overlapping) {
            if (!unique_class_extent(type_table[m.type.unqualified()].entity,
                                     sub, covered))
                return false;
        } else if (!unique_representation(m.type, sub))
            return false;
        if (!sub)
            return true;
        uint64_t size = 8 * size_of_type(m.type);
        /* a [[no_unique_address]] class leaves its tail padding to the
           next member */
        if (overlapping)
            size = covered;
        /* [ext:clang_type_traits] a bit-field's bits are its width, a
           union's the whole class (as clang++ answers) */
        if (m.bit_field)
            size = m.bit_width;
        /* a reference member's bytes are a pointer's */
        if (type_table[m.type.unqualified()].kind ==
                type_kind::lvalue_reference ||
            type_table[m.type.unqualified()].kind ==
                type_kind::rvalue_reference)
            size = 8 * size_of_type(qualified_type::make(
                           type_table.pointer_to(m.type), cv_none));
        if (!size)
            continue;
        if (x.union_) {
            if (!m.bit_field && size != 8 * x.size)
                return true;
            pieces_in_union = true;
            continue;
        }
        if (m.bit_field)
            pieces.append(piece{m.bit_offset, size});
        else
            pieces.append(piece{8 * m.offset, size});
    }
    if (x.union_) {
        unique = pieces_in_union;
        extent = 8 * x.size;
        return true;
    }
    uint64_t end = 0;
    uint32_t taken = 0;
    for (;;) {
        /* the piece that starts at the end of those taken */
        bool found = false;
        for (const piece &p : pieces)
            if (p.offset == end && p.size) {
                end += p.size;
                taken++;
                found = true;
                break;
            }
        if (!found)
            break;
    }
    /* a piece left over starts after a gap */
    unique = taken == pieces.size();
    extent = end;
    return true;
}

/*
 * [basic.types]/11 and [class.mem]/26: whether a and b are
 * layout-compatible types, cv-qualification ignored: the same type; two
 * enumerations with the same underlying type; two arrays of
 * layout-compatible elements and the same bound; two standard-layout
 * non-union classes, without base classes, whose members are in order
 * layout-compatible, with the same bit-field widths, offsets and no
 * [[no_unique_address]] (clang, CWG 2759).  known is false for what is not
 * followed: an
 * incomplete class, a union, a class with bases.
 */
bool semantic_analysis::layout_compatible(qualified_type a, qualified_type b,
                                          bool &known)
{
    a = qualified_type::make(a.unqualified(), cv_none);
    b = qualified_type::make(b.unqualified(), cv_none);
    if (a == b) {
        const struct type &k = type_table[a.unqualified()];
        if (k.kind == type_kind::class_type) {
            require_complete(k.entity);
            if (!entity_table[k.entity].size)
                known = false;
        }
        return true;
    }
    const struct type &x = type_table[a.unqualified()];
    const struct type &y = type_table[b.unqualified()];
    if (x.kind == type_kind::array && y.kind == type_kind::array) {
        if (x.bound_known != y.bound_known)
            return false;
        if (x.bound_known && x.bound != y.bound)
            return false;
        return layout_compatible(x.operand, y.operand, known);
    }
    if (x.kind == type_kind::enumeration && y.kind == type_kind::enumeration) {
        require_complete(x.entity);
        require_complete(y.entity);
        return entity_table[x.entity].underlying ==
               entity_table[y.entity].underlying;
    }
    if (x.kind != type_kind::class_type || y.kind != type_kind::class_type)
        return false;
    entity_index c = x.entity, d = y.entity;
    require_complete(c);
    require_complete(d);
    const entity &p = entity_table[c];
    const entity &q = entity_table[d];
    if (!p.size || !q.size) {
        known = false;
        return false;
    }
    if (p.union_ != q.union_)
        return false;
    bool layout_p = false, layout_q = false;
    if (!standard_layout_class(c, layout_p) ||
        !standard_layout_class(d, layout_q)) {
        known = false;
        return false;
    }
    if (!layout_p || !layout_q)
        return false;
    if (p.union_ || p.base_count || q.base_count || p.virtual_base_count ||
        q.virtual_base_count) {
        known = false;
        return false;
    }
    owned_array<entity_index> first, second;
    for (uint32_t i = 0; i < p.member_count; i++) {
        const entity &m = entity_table[class_members[p.first_member + i]];
        if (m.kind == entity_kind::variable && m.nonstatic_member)
            first.append(class_members[p.first_member + i]);
    }
    for (uint32_t i = 0; i < q.member_count; i++) {
        const entity &m = entity_table[class_members[q.first_member + i]];
        if (m.kind == entity_kind::variable && m.nonstatic_member)
            second.append(class_members[q.first_member + i]);
    }
    if (first.size() != second.size())
        return false;
    for (uint32_t i = 0; i < first.size(); i++) {
        const entity &m = entity_table[first[i]];
        const entity &n = entity_table[second[i]];
        /* clang: a [[no_unique_address]] member makes two distinct
           classes not layout-compatible (CWG 2759); a member with another
           alignment or offset is not the same layout */
        if (m.bit_field != n.bit_field || m.bit_width != n.bit_width ||
            m.no_unique_address || n.no_unique_address || m.offset != n.offset)
            return false;
        if (!layout_compatible(m.type, n.type, known))
            return false;
    }
    return true;
}

/* ( type-id , type-id ): __is_layout_compatible(T, U), [meta.rel] */
analysis_result
semantic_analysis::clang_layout_compatible_trait(SyntaxRef n,
                                                 expression_meaning &m)
{
    qualified_type t, u;
    VTRY(r, type_id(n[2], t));
    VTRY_SET(r, type_id(n[4], u));
    if (depends_on_template_parameter(t) || depends_on_template_parameter(u))
        return not_supported_because(
            n, "temp.dep.constexpr",
            "__is_layout_compatible of a dependent type");
    bool known = true;
    bool answer = layout_compatible(t, u, known);
    if (!known)
        return not_supported_because(n, "meta.rel",
                                     "__is_layout_compatible of types whose "
                                     "layout compatibility is unknown");
    m = integer_constant(fundamental_type::bool_, answer, n.first_token());
    return analysis_result::well_formed;
}

/*
 * Whether t is trivially relocatable.  cpp26 is clang's
 * __builtin_is_cpp_trivially_relocatable (P2786: a const field, a
 * defaulted move, a union with a member that is not are not; a polymorphic
 * class may be), else __is_trivially_relocatable (clang's own: the
 * classic rule).  A scalar is; a reference, void and a function are not; an
 * array is as its element; a class is not with a virtual base, a
 * user-provided destructor, copy or move constructor, or a base or member
 * that is not.  known is false for what clang answers by rules this does
 * not follow (a defaulted or deleted special member beside others, a
 * union, a user-provided copy assignment).
 */
/* whether the head of class c's definition has [[clang::trivial_abi]]
   (which makes it trivially relocatable for clang's old rule): not kept by
   the analysis, which found it without effect, so read from the tokens up
   to the first { */
bool semantic_analysis::class_head_names_trivial_abi(entity_index c)
{
    uint32_t d = entity_table[c].first_declaration;
    if (!d)
        return false;
    SyntaxRef node = entity_table.declaration(d).node;
    if (!node)
        return false;
    for (token_index t = node.first_token(); t < node.end_token(); t++) {
        owned_buffer text;
        tokens.spelling(t, text.get());
        if (text.size() == 1 && text.data()[0] == '{')
            break;
        if (text.size() == 11 && !memcmp(text.data(), "trivial_abi", 11))
            return true;
    }
    return false;
}

bool semantic_analysis::relocatable_type(qualified_type t, bool cpp26,
                                         bool &known)
{
    t = qualified_type::make(t.unqualified(), cv_none);
    while (type_table[t.unqualified()].kind == type_kind::array) {
        t = type_table[t.unqualified()].operand;
        t = qualified_type::make(t.unqualified(), cv_none);
    }
    const struct type &record = type_table[t.unqualified()];
    switch (record.kind) {
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference:
    case type_kind::function:
        return false;
    case type_kind::class_type:
        break;
    case type_kind::fundamental:
        return fundamental_of(t) != fundamental_type::void_;
    case type_kind::pointer:
    case type_kind::enumeration:
    case type_kind::pointer_to_member:
        return true;
    default:
        known = false;
        return false;
    }
    entity_index c = record.entity;
    require_complete(c);
    const entity &x = entity_table[c];
    if (!x.size) {
        known = false;
        return false;
    }
    if (x.virtual_base_count)
        return false;
    if (class_head_names_trivial_abi(c)) {
        known = false;
        return false;
    }
    if (x.dynamic && !cpp26)
        return false;
    /* a user-declared move assignment operator: user-provided or deleted */
    if (cpp26 && declares_move_assignment(c))
        return false;
    if (x.union_) {
        /* a union of members that copy as bytes */
        bool copyable_known = true;
        bool copyable = class_trivially_copyable(c, copyable_known);
        if (!copyable_known || !copyable) {
            known = false;
            return false;
        }
        return true;
    }
    if (x.destructor && !entity_table[x.destructor].implicit)
        return false;
    for (entity_index k = x.first_constructor; k;
         k = entity_table[k].next_constructor)
        if (!entity_table[k].implicit && copy_or_move_constructor(c, k))
            return false;
    if (x.user_copy_assignments || x.implicit_copy_deleted ||
        x.constructor_template || x.constructor_templates) {
        known = false;
        return false;
    }
    if (cpp26 &&
        (x.defaulted_move_constructor || x.defaulted_move_assignment ||
         x.defaulted_copy_constructor || x.defaulted_copy_assignment)) {
        known = false;
        return false;
    }
    for (uint32_t i = 0; i < x.base_count; i++) {
        const entity &b = entity_table[class_bases[x.first_base + i]];
        if (!relocatable_type(b.type, cpp26, known) || !known)
            return false;
    }
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = entity_table[class_members[x.first_member + i]];
        if (m.kind != entity_kind::variable || !m.nonstatic_member ||
            m.base_subobject)
            continue;
        qualified_type f = m.type;
        bool constant = (f.cv() & cv_const) != 0;
        while (type_table[f.unqualified()].kind == type_kind::array) {
            f = type_table[f.unqualified()].operand;
            constant = constant || (f.cv() & cv_const);
        }
        type_kind fk = type_table[f.unqualified()].kind;
        /* a reference member: P2786's rule excludes it, clang's own does
           not */
        if (fk == type_kind::lvalue_reference ||
            fk == type_kind::rvalue_reference) {
            if (cpp26)
                return false;
            continue;
        }
        if (cpp26 && constant)
            return false;
        if (!relocatable_type(f, cpp26, known) || !known)
            return false;
    }
    return true;
}

} // namespace cplusplus
