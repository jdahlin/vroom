/*
 * triviality.cpp: when a special member function is trivial ([special]):
 * [class.default.ctor]/3 the default constructor, [class.copy.ctor]/11 the
 * copy and move constructors, [class.dtor]/8 the destructor, and what the
 * rest of analysis asks in terms of them.  Each definition has one function
 * here; the clang traits (ext/clang/) and the rules that need a trivial
 * member ([class.union], [class.copy.elision], lowering) call these.
 *
 * What analysis keeps: a trivial implicitly-declared constructor or
 * destructor is no entity (implicit_special_members); a non-trivial one is,
 * as a user-declared one.  So "not user-provided" is "no constructor entity
 * that is not implicit", and the recursion over "direct base classes" and
 * "non-static data members of class type (or array thereof)" is
 * subobject_classes.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* [class.default.ctor]/3, for X() = default beside other constructors: it
   is trivial when no constructor without parameters is an entity (a
   trivial one is none) */
bool semantic_analysis::trivial_defaulted_default_constructor(
    entity_index c) const
{
    const entity &x = entity_table[c];
    if (!x.defaulted_default_constructor)
        return false;
    for (entity_index k = x.first_constructor; k;
         k = entity_table[k].next_constructor)
        if (!type_table[entity_table[k].type.unqualified()].parameter_count)
            return false;
    return true;
}

/*
 * [class.default.ctor]/3: "A default constructor is trivial if it is not
 * user-provided and if:
 * - its class has no virtual functions and no virtual base classes, and
 * - no non-static data member of its class has a default member
 *   initializer, and
 * - all the direct base classes of its class have trivial default
 *   constructors, and
 * - for all the non-static data members of its class that are of class type
 *   (or array thereof), each such class has a trivial default constructor."
 * Whether class c has one: /1 declares it implicitly when "there is no
 * user-declared constructor", else X() = default declares it.  known is
 * false for a constructor template that may be the one, which analysis
 * does not follow.
 */
bool semantic_analysis::trivial_default_constructor(entity_index c,
                                                    bool &known) const
{
    const entity &x = entity_table[c];
    /* (3.1) */
    if (x.dynamic || x.virtual_base_count)
        return false;
    /* a constructor template may be the default constructor; beside
       X() = default it is not the one default-initialization selects
       ([over.match.best]/2.4: "F1 is not a function template
       specialization and F2 is") */
    if ((x.constructor_template || x.constructor_templates) &&
        !x.defaulted_default_constructor) {
        known = false;
        return false;
    }
    /* not user-provided */
    bool user_declared = false;
    for (entity_index k = x.first_constructor; k;
         k = entity_table[k].next_constructor) {
        if (entity_table[k].implicit)
            continue;
        const struct type &f = type_table[entity_table[k].type.unqualified()];
        /* [ext:clang_type_traits] X() = delete is not user-provided
           ([dcl.fct.def.default]/5): clang counts it trivial */
        if (entity_table[k].deleted && !f.parameter_count)
            continue;
        user_declared = true;
        /* a user-provided default constructor ([class.default.ctor]/1: one
           "for which each parameter that is not a function parameter pack
           has a default argument") */
        if (f.parameter_count == entity_table[k].default_arguments)
            return false;
    }
    /* /1: a user-declared constructor and no X() = default: there is no
       default constructor to be trivial */
    if (user_declared && !trivial_defaulted_default_constructor(c))
        return false;
    /* (3.2) */
    for (uint32_t i = 0; i < x.member_count; i++)
        if (entity_table[class_members[x.first_member + i]].default_initializer)
            return false;
    /* (3.3), (3.4) */
    owned_array<entity_index> classes;
    subobject_classes(c, classes);
    for (entity_index k : classes)
        if (!trivial_default_constructor(k, known))
            return false;
    return true;
}

/*
 * [class.copy.ctor]/11: "A copy/move constructor for class X is trivial if
 * it is not user-provided and if:
 * - class X has no virtual functions and no virtual base classes, and
 * - the constructor selected to copy/move each direct base class subobject
 *   is trivial, and
 * - for each non-static data member of X that is of class type (or array
 *   thereof), the constructor selected to copy/move that member is
 *   trivial".
 * The constructor selected is the subobject class's copy constructor
 * (analysis does not tell the copy from the move here).  Whether it exists
 * or is deleted is copy_constructor_deleted_by_move's.
 */
bool semantic_analysis::trivial_copy_constructor(entity_index c) const
{
    const entity &x = entity_table[c];
    /* (11.1): a virtual base makes the class dynamic too */
    if (x.dynamic)
        return false;
    /* not user-provided: an implicit one is an entity for overload
       resolution (declared beside a constructor template, /6) or because
       a subobject's is not trivial, which (11.2) and (11.3) say */
    for (entity_index k = x.first_constructor; k;
         k = entity_table[k].next_constructor)
        if (!entity_table[k].implicit && copy_or_move_constructor(c, k))
            return false;
    /* (11.2), (11.3) */
    owned_array<entity_index> classes;
    subobject_classes(c, classes);
    for (entity_index k : classes)
        if (!trivial_copy_constructor(k))
            return false;
    return true;
}

semantic_analysis::copy_selection
semantic_analysis::select_copy_constructor(entity_index c, bool rvalue,
                                           bool source_const) const
{
    copy_selection selected;
    const entity &x = entity_table[c];
    if (x.constructor_template || x.constructor_templates) {
        selected.known = false;
        return selected;
    }
    for (entity_index k = x.first_constructor; k;
         k = entity_table[k].next_constructor) {
        if (!copy_or_move_constructor(c, k))
            continue;
        const struct type &p =
            type_table[type_table
                           .parameters(entity_table[k].type.unqualified())[0]
                           .unqualified()];
        bool is_rvalue = p.kind == type_kind::rvalue_reference;
        uint8_t cv = p.operand.cv() & (cv_const | cv_volatile);
        /* [dcl.init.ref]/5: the reference is at least as cv-qualified */
        if (source_const && !(cv & cv_const))
            continue;
        /* [over.ics.rank]/3.2.3: an rvalue binds to an rvalue reference
           in preference; an lvalue does not bind to one */
        if (is_rvalue && !rvalue)
            continue;
        bool better = !selected.constructor;
        if (selected.constructor) {
            if (is_rvalue != selected.rvalue_reference)
                better = rvalue && is_rvalue;
            else if (cv != selected.cv)
                better = (cv & selected.cv) == cv;
            else {
                selected.known = false;
                return selected;
            }
        }
        if (better) {
            selected.constructor = k;
            selected.rvalue_reference = is_rvalue;
            selected.cv = cv;
        }
    }
    return selected;
}

/*
 * Whether the constructor that [over.match.ctor] selects to construct a
 * class c object from one object of its own class is trivial
 * ([class.copy.ctor]/11), for a source of the given value category and
 * constness.  A user-declared copy or move constructor is user-provided;
 * the implicit one (and X(const X&) = default, an entity beside another
 * copy constructor) is trivial when the constructor each subobject's copy
 * selects is: from a const source for X(const X&), from a non-const one
 * for X(X&) ([class.copy.ctor]/7).  known is false when analysis cannot
 * tell (a constructor template, a mutable subobject, an ambiguity).
 */
bool semantic_analysis::trivial_copy_selected(entity_index c, bool rvalue,
                                              bool source_const,
                                              bool &known) const
{
    const entity &x = entity_table[c];
    if (x.dynamic)
        return false;
    copy_selection selected = select_copy_constructor(c, rvalue, source_const);
    if (!selected.known) {
        known = false;
        return false;
    }
    entity_index best = selected.constructor;
    bool have_best = best != 0;
    bool best_rvalue = selected.rvalue_reference;
    uint8_t best_cv = selected.cv;
    /* no user-declared copy or move constructor selected: X(const X&),
       defaulted or implicit ([class.copy.ctor]/6, /7) */
    if (have_best && !entity_table[best].implicit)
        return false;
    /* implicit: X(const X&), whose subobjects are copied from const
       objects; X(X&&) from rvalues */
    bool sub_const = true;
    bool sub_rvalue = false;
    if (have_best && best_rvalue) {
        sub_const = false;
        sub_rvalue = true;
    }
    /* [class.copy.ctor]/7: X(X&) copies each subobject from a non-const one */
    if (have_best && !best_rvalue && !(best_cv & cv_const))
        sub_const = false;
    owned_array<entity_index> classes;
    subobject_classes(c, classes);
    for (uint32_t i = 0; i < x.member_count; i++)
        if (entity_table[class_members[x.first_member + i]].mutable_member) {
            known = false;
            return false;
        }
    for (entity_index k : classes)
        if (!trivial_copy_selected(k, sub_rvalue, sub_const, known))
            return false;
    return true;
}

/* whether a copy of a class c object copies its bytes: its copy constructor
   is not deleted ([class.copy.ctor]/6, /10) and is trivial (/11) */
bool semantic_analysis::copies_bytes(entity_index c) const
{
    return trivial_copy_constructor(c) && !copy_constructor_deleted_by_move(c);
}

/*
 * [class.dtor]/8: "A destructor is trivial if it is not user-provided and
 * if: the destructor is not virtual, all of the direct base classes of its
 * class have trivial destructors, and for all of the non-static data
 * members of its class that are of class type (or array thereof), each
 * such class has a trivial destructor."  Whether class c's is not: it has
 * a destructor entity (user-declared, or implicit and non-trivial), or a
 * subobject's class has.
 */
bool semantic_analysis::destroys(entity_index c) const
{
    if (entity_table[c].destructor)
        return true;
    owned_array<entity_index> classes;
    subobject_classes(c, classes);
    for (entity_index k : classes)
        if (destroys(k))
            return true;
    return false;
}

/* a class whose copy constructor (/11) and destructor ([class.dtor]/8) are
   trivial: an object of it is copied as bytes and destroyed by nothing
   (lowering passes it as C's struct) */
bool semantic_analysis::trivial_class(entity_index c) const
{
    return copies_bytes(c) && !destroys(c);
}

/*
 * [class.union]/4 Note 3: "Absent default member initializers, if any
 * non-static data member of a union has a non-trivial default
 * constructor, copy constructor, move constructor, copy assignment
 * operator, move assignment operator, or destructor, the corresponding
 * member function of the union must be user-provided or it will be
 * implicitly deleted for the union."  Whether class c has none of them
 * non-trivial: a non-trivial constructor is an entity
 * (implicit_special_members), and so is a destructor.
 */
bool semantic_analysis::trivial_special_members(entity_index c) const
{
    const entity &x = entity_table[c];
    for (entity_index k = x.first_constructor; k;
         k = entity_table[k].next_constructor)
        if (!entity_table[k].implicit &&
            (type_table[entity_table[k].type.unqualified()].parameter_count ==
                 0 ||
             copy_or_move_constructor(c, k)))
            return false;
    if (x.dynamic || x.destructor || x.user_copy_assignments)
        return false;
    for (uint32_t i = 0; i < x.member_count; i++)
        if (entity_table[class_members[x.first_member + i]].default_initializer)
            return false;
    owned_array<entity_index> classes;
    subobject_classes(c, classes);
    for (entity_index k : classes)
        if (!trivial_special_members(k))
            return false;
    return true;
}

} // namespace cplusplus
