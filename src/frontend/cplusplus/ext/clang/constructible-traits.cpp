/*
 * constructible-traits.cpp: [ext:clang_type_traits] __is_constructible(T,
 * Args...) and __is_nothrow_constructible(T, Args...), the traits libc++'s
 * <__type_traits/is_constructible.h> and is_nothrow_constructible.h use
 * when __has_builtin says they exist.
 *
 * [meta.unary.prop] is_constructible: "the variable definition T t(declval<
 * Args>()...); is well-formed"; is_nothrow_constructible: "is_constructible
 * is true and the variable definition is known not to throw any
 * exceptions" (clang also asks T's destructor not to throw).  The
 * definition is analysed as the program would: a class by its
 * constructors' overload resolution (construct), anything else by
 * initialize_with, and what it makes ill-formed is no error but "false"
 * ([temp.deduct]/8's way: the diagnostics are taken back).  What the
 * analysis has no rule for (parenthesized aggregate initialization,
 * arrays with arguments) is unsupported, not a guess.
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* [class.dtor]: whether an object of class c can be destroyed (its
   destructor, and those of its subobjects when it is implicit, is neither
   deleted nor inaccessible) and, nothrow, whether that cannot throw
   ([except.spec]/8: an implicit destructor throws when a subobject's does).
   throws is set when a destructor's exception specification allows it. */
bool semantic_analysis::class_destructible(entity_index c, bool &throws)
{
    const entity &x = entity_table[c];
    if (x.destructor) {
        const entity &d = entity_table[x.destructor];
        if (d.deleted || !accessible(x.destructor))
            return false;
        if (!d.implicit) {
            if (!type_table[d.type.unqualified()].non_throwing)
                throws = true;
            return true;
        }
    }
    owned_array<entity_index> classes;
    subobject_classes(c, classes);
    for (entity_index k : classes)
        if (!class_destructible(k, throws))
            return false;
    return true;
}

/* [except.spec]/7: whether the default member initializer of member e can
   throw, as default_member_initializer found it when the constructor that
   uses it was defined; false when it was not analysed or has no rule */
bool semantic_analysis::default_initializer_throws(entity_index e, bool &throws)
{
    switch (entity_table[e].initializer_throws) {
    case initializer_exception::cannot_throw:
        throws = false;
        return true;
    case initializer_exception::may_throw:
        throws = true;
        return true;
    default:
        return false;
    }
}

/* [except.spec]/8-9: whether the implicit constructor f of its class
   (default, or copy: one parameter) can throw, one of its subobjects'
   constructors it calls being potentially-throwing; known is false when a
   default member initializer or an overload would have to be analysed */
bool semantic_analysis::implicit_constructor_throws(entity_index f, bool &known)
{
    if (entity_table[f].inherited_constructor) {
        /* [except.spec]/8: the inherited constructor's specification and
           the other subobjects' default initialization: not analysed */
        known = false;
        return false;
    }
    entity_index c = entity_table[f].enclosing;
    bool copy = type_table[entity_table[f].type.unqualified()].parameter_count;
    const entity x = entity_table[c];
    for (uint32_t i = 0; !copy && i < x.member_count; i++) {
        entity_index e = class_members[x.first_member + i];
        if (!entity_table[e].default_initializer)
            continue;
        bool throws = false;
        if (!default_initializer_throws(e, throws)) {
            known = false;
            return false;
        }
        if (throws)
            return true;
    }
    owned_array<entity_index> classes;
    subobject_classes(c, classes);
    for (entity_index k : classes) {
        if (!copy && !default_constructed(c, k))
            continue;
        if (subobject_constructor_throws(k, copy, known))
            return true;
        if (!known)
            return false;
    }
    return false;
}

/* ... the default constructor of class k, or its copy constructor from a
   const lvalue, can throw */
bool semantic_analysis::subobject_constructor_throws(entity_index k, bool copy,
                                                     bool &known)
{
    entity_index found = 0;
    for (entity_index e = entity_table[k].first_constructor; e;
         e = entity_table[e].next_constructor) {
        const struct type &f = type_table[entity_table[e].type.unqualified()];
        if (!copy && f.parameter_count == 0)
            found = e;
        if (copy && f.parameter_count == 1) {
            const struct type &p = type_table
                [type_table.parameters(entity_table[e].type.unqualified())[0]
                     .unqualified()];
            if (p.kind == type_kind::lvalue_reference &&
                p.operand.unqualified() == entity_table[k].type.unqualified() &&
                (!found || p.operand.cv() == cv_const))
                found = e;
        }
    }
    if (found) {
        if (entity_table[found].implicit)
            return implicit_constructor_throws(found, known);
        return !type_table[entity_table[found].type.unqualified()].non_throwing;
    }
    /* none declared: a trivial one, which copies bytes or does nothing */
    if (copy && !copies_bytes(k))
        known = false;
    return false;
}

/* whether class c has a default member initializer and is a member of a
   class whose definition is not complete */
bool semantic_analysis::default_initializer_pending(entity_index c) const
{
    const entity &x = entity_table[c];
    bool has = false;
    for (uint32_t i = 0; i < x.member_count && !has; i++) {
        const entity &m = entity_table[class_members[x.first_member + i]];
        if (m.default_initializer)
            has = true;
    }
    if (!has)
        return false;
    for (entity_index e = x.enclosing;
         e && entity_table[e].kind == entity_kind::class_type;
         e = entity_table[e].enclosing)
        if (!entity_table[e].size)
            return true;
    return false;
}

/* whether the argument's class is class c or derives from it */
bool semantic_analysis::same_or_derived_class(const expression_meaning &a,
                                              entity_index c)
{
    const struct type &k = type_table[a.type.unqualified()];
    if (k.kind != type_kind::class_type)
        return false;
    bool ambiguous = false;
    return k.entity == c || base_step(k.entity, c, ambiguous);
}

/* [temp.deduct]/8's way: the initialization of type from a, whose failure
   is no error (ok false, what it issued taken back) */
analysis_result
semantic_analysis::try_initialization(SyntaxRef n, expression_meaning &a,
                                      qualified_type type, bool direct,
                                      initialized_value &made, bool &ok)
{
    ok = false;
    substitution_mark mark = mark_substitution();
    substituting++;
    unevaluated++;
    analysis_result r = initialize_with(n, a, type, direct, false, made);
    unevaluated--;
    substituting--;
    if (r == analysis_result::ill_formed && undo_substitution(mark))
        return analysis_result::well_formed;
    ok = r == analysis_result::well_formed;
    return r;
}

/* [dcl.init]/17.6.2.2 (C++20): an aggregate initialized by
   ( expression-list ): its elements, in order, copy-initialized from the
   arguments, without narrowing checks, the rest from a default member
   initializer or value-initialized; too many arguments is ill-formed.  Elements the
   analysis has no rule for (bases, bit-fields, arrays, class types left
   value-initialized) are unsupported. */
analysis_result semantic_analysis::aggregate_paren_initialization(
    SyntaxRef n, entity_index c, owned_array<expression_meaning> &arguments,
    bool nothrow, bool &value)
{
    value = false;
    const entity x = entity_table[c];
    if (x.base_count || x.virtual_base_count)
        return not_supported(n, "dcl.init.aggr");
    owned_array<entity_index> elements;
    for (uint32_t i = 0; i < x.member_count; i++) {
        entity_index e = class_members[x.first_member + i];
        const entity &m = entity_table[e];
        if (m.kind != entity_kind::variable || !m.nonstatic_member)
            continue;
        if (m.bit_field || !m.name)
            return not_supported(n, "dcl.init.aggr");
        elements.append(e);
        /* a union initializes its first member */
        if (x.union_)
            break;
    }
    if (arguments.size() > elements.size())
        return analysis_result::well_formed;
    owned_array<uint32_t> nodes;
    for (size_t i = 0; i < elements.size(); i++) {
        const entity &m = entity_table[elements[i]];
        if (i >= arguments.size()) {
            if (x.union_)
                break;
            type_kind kind = type_table[m.type.unqualified()].kind;
            if (kind == type_kind::lvalue_reference ||
                kind == type_kind::rvalue_reference)
                return analysis_result::well_formed;
            if (kind == type_kind::class_type || kind == type_kind::array ||
                (m.default_initializer && nothrow))
                return not_supported(n, "dcl.init.aggr");
            continue;
        }
        if (type_table[m.type.unqualified()].kind == type_kind::array)
            return not_supported(n, "dcl.init.aggr");
        initialized_value made;
        bool ok = false;
        VTRY(r, try_initialization(n, arguments[i], m.type, false, made, ok));
        if (!ok)
            return analysis_result::well_formed;
        nodes.append(made.node);
    }
    value = true;
    if (!nothrow)
        return analysis_result::well_formed;
    for (uint32_t node : nodes) {
        bool throwing = false;
        VTRY(r, potentially_throwing(n, node, throwing));
        if (throwing)
            value = false;
    }
    return analysis_result::well_formed;
}

/* [dcl.init]/8 of an aggregate: value-initialization, which calls
   the implicit default constructor when it is not trivial */
analysis_result
semantic_analysis::aggregate_value_initialization(SyntaxRef n, entity_index c,
                                                  bool nothrow, bool &value)
{
    value = !default_constructor_deleted(c);
    if (!value || !nothrow)
        return analysis_result::well_formed;
    for (entity_index k = entity_table[c].first_constructor; k;
         k = entity_table[k].next_constructor) {
        if (!entity_table[k].implicit ||
            type_table[entity_table[k].type.unqualified()].parameter_count)
            continue;
        bool known = true;
        if (implicit_constructor_throws(k, known))
            value = false;
        if (!known)
            return not_supported(n, "except.spec");
    }
    return analysis_result::well_formed;
}

/* the declaration T t(a...) and whether it can throw (nothrow), as the
   traits ask; value is set when the analysis has an answer */
analysis_result semantic_analysis::constructible_types(
    SyntaxRef n, qualified_type t, const qualified_type *types, uint32_t count,
    bool nothrow, bool &value)
{
    value = false;
    const struct type k = type_table[t.unqualified()];
    /* [dcl.init]: no object of these types is made */
    if (k.kind == type_kind::function ||
        fundamental_of(t) == fundamental_type::void_)
        return analysis_result::well_formed;
    if (k.kind == type_kind::array) {
        /* an array of unknown bound has no object to make */
        if (!k.bound_known)
            return analysis_result::well_formed;
        if (count)
            return not_supported(n, "dcl.init.aggr");
        return constructible_types(n, k.operand, types, 0, nothrow, value);
    }
    bool reference = k.kind == type_kind::lvalue_reference ||
                     k.kind == type_kind::rvalue_reference;
    if (reference && count != 1)
        return analysis_result::well_formed;
    if (abstract_class(t))
        return analysis_result::well_formed;
    owned_array<expression_meaning> arguments;
    for (uint32_t i = 0; i < count; i++)
        arguments.append(declval_meaning(types[i]));
    bool is_class = k.kind == type_kind::class_type;
    entity_index c = 0;
    if (is_class) {
        c = k.entity;
        require_complete(c);
        if (!entity_table[c].size)
            return not_supported(n, "dcl.init");
        bool throws = false;
        if (!class_destructible(c, throws))
            return analysis_result::well_formed;
        if (throws && nothrow)
            return analysis_result::well_formed;
        /* [class.mem]/6: a default member initializer is a complete-class
           context of the outermost enclosing class: while that class is
           being defined, the nested class cannot be default-constructed
           (clang: it is not constructible yet) */
        if (!count && default_initializer_pending(c))
            return analysis_result::well_formed;
        /* [dcl.init]/8.2: a trivial default constructor does nothing */
        if (!count && trivial_defaulted_default_constructor(c)) {
            value = !default_constructor_deleted(c);
            return analysis_result::well_formed;
        }
        /* a volatile object is not copied by an implicit constructor */
        if (count == 1 && (arguments[0].type.cv() & cv_volatile))
            return not_supported(n, "dcl.init");
        bool one_of_its_own =
            count == 1 && same_or_derived_class(arguments[0], c);
        if (is_aggregate(c) && !one_of_its_own) {
            if (!count)
                return aggregate_value_initialization(n, c, nothrow, value);
            return aggregate_paren_initialization(n, c, arguments, nothrow,
                                                  value);
        }
        /* no constructor takes these: no object is made, unless a
           conversion function of the argument's class gives one */
        if (!entity_table[c].first_constructor && count && !one_of_its_own) {
            if (count == 1 &&
                type_table[arguments[0].type.unqualified()].kind ==
                    type_kind::class_type &&
                has_conversion_functions(
                    type_table[arguments[0].type.unqualified()].entity))
                return not_supported(n, "over.match.copy");
            return analysis_result::well_formed;
        }
    } else if (!count && !reference) {
        /* a scalar: value-initialized */
        value = true;
        return analysis_result::well_formed;
    } else if (count > 1) {
        return analysis_result::well_formed;
    }
    expression_meaning m;
    initialized_value made;
    bool ok = false;
    analysis_result r = analysis_result::well_formed;
    if (is_class) {
        substitution_mark mark = mark_substitution();
        substituting++;
        unevaluated++;
        r = class_direct_initialization(n, c, arguments, m);
        unevaluated--;
        substituting--;
        if (r == analysis_result::ill_formed && undo_substitution(mark))
            return analysis_result::well_formed;
        ok = r == analysis_result::well_formed;
    } else {
        r = try_initialization(n, arguments[0], t, true, made, ok);
    }
    if (!ok)
        return r;
    value = true;
    if (!nothrow)
        return analysis_result::well_formed;
    uint32_t node = made.node;
    if (is_class)
        node = m.node;
    bool throwing = false;
    VTRY_SET(r, potentially_throwing(n, node, throwing));
    value = !throwing;
    return analysis_result::well_formed;
}

/*
 * __is_trivially_constructible(T, Args...) ([meta.unary.prop]
 * is_trivially_constructible: "is_constructible_v<T, Args...> is true and
 * the variable definition for is_constructible, as defined below, is known
 * to call no operation that is not trivial ([basic.types],
 * [special])"), given that the definition is well-formed: a class is
 * trivially constructed by a trivial default constructor from nothing, by a
 * trivial copy or move constructor from one object of its own class, and
 * from nothing else; a scalar or a reference from arguments that are no
 * class objects (a conversion function is a call).  known is false when
 * analysis cannot tell.
 */
bool semantic_analysis::trivially_constructed(qualified_type t,
                                              const qualified_type *types,
                                              uint32_t count, bool &known) const
{
    const struct type k = type_table[t.unqualified()];
    if (k.kind == type_kind::array)
        return trivially_constructed(k.operand, types, count, known);
    if (k.kind == type_kind::class_type) {
        if (count == 0)
            return trivial_default_constructor(k.entity, known);
        /* (the argument's type: a reference to the object) */
        qualified_type object = types[0];
        type_kind object_kind = type_table[object.unqualified()].kind;
        if (object_kind == type_kind::lvalue_reference ||
            object_kind == type_kind::rvalue_reference)
            object = type_table[object.unqualified()].operand;
        const struct type a = type_table[object.unqualified()];
        if (count == 1 && a.kind == type_kind::class_type &&
            a.entity == k.entity)
            return trivial_copy_constructor(k.entity);
        return false;
    }
    for (uint32_t i = 0; i < count; i++) {
        qualified_type argument = types[i];
        type_kind argument_kind = type_table[argument.unqualified()].kind;
        if (argument_kind == type_kind::lvalue_reference ||
            argument_kind == type_kind::rvalue_reference)
            argument = type_table[argument.unqualified()].operand;
        if (type_table[argument.unqualified()].kind == type_kind::class_type)
            return false;
    }
    return true;
}

/* ( type-id ..._opt , ... ): __is_constructible(T, Args...) and
   __is_nothrow_constructible(T, Args...) */
analysis_result semantic_analysis::clang_constructible_trait(
    SyntaxRef n, bool nothrow, bool trivial, expression_meaning &m)
{
    owned_array<SyntaxElement> written;
    owned_array<bool> expansion;
    for (uint32_t i = 2; i + 1 < n.size(); i++) {
        if (n[i].is(preprocessing_op_or_punc::COMMA))
            continue;
        if (n[i].is(preprocessing_op_or_punc::ELLIPSIS)) {
            if (written.empty() || expansion.back())
                return not_supported(n, "temp.variadic");
            expansion.back() = true;
            continue;
        }
        if (!n[i].is(syntax_kind::type_id))
            return not_supported(n, "ext.clang");
        written.append(n[i]);
        expansion.append(false);
    }
    if (written.empty() || expansion[0])
        return not_supported(n, "ext.clang");
    owned_array<qualified_type> types;
    bool dependent = false;
    VTRY(r, builtin_template_types(n, written, expansion, 0, types, dependent));
    if (dependent)
        return not_supported(n, "temp.dep.constexpr");
    bool value = false;
    VTRY_SET(r, constructible_types(n, types[0], types.data() + 1,
                                    static_cast<uint32_t>(types.size() - 1),
                                    nothrow, value));
    if (value && trivial) {
        bool known = true;
        value = trivially_constructed(types[0], types.data() + 1,
                                      static_cast<uint32_t>(types.size() - 1),
                                      known);
        if (!known)
            return not_supported(n, "class.default.ctor");
    }
    m = integer_constant(fundamental_type::bool_, value, n.first_token());
    return analysis_result::well_formed;
}

} // namespace cplusplus
