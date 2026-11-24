/*
 * implicit-assignment.cpp: [class.copy.assign] Copy/move assignment
 * operators, the implicitly-declared ones.
 *
 * [class.copy.assign]/2: "If the class definition does not explicitly
 * declare a copy assignment operator, one is declared implicitly.  If the
 * class definition declares a move constructor or move assignment operator,
 * the implicitly declared copy assignment operator is defined as deleted;
 * otherwise, it is defined as defaulted."  /3: the move assignment operator
 * is declared "if and only if (3.1) X does not have a user-declared copy
 * constructor, (3.2) X does not have a user-declared move constructor, (3.3)
 * X does not have a user-declared copy assignment operator, and (3.4) X does
 * not have a user-declared destructor".  /9: trivial when the class has no
 * virtual functions or virtual bases and each base's and member's is: a
 * trivial one copies the bytes (assigned_bitwise, no entity); the others
 * are declared here, as entities like the implicit constructors
 * (declare_implicit), and defined at the end of the class
 * (implicit_definitions) as /12 and /13 say: "The implicitly-defined
 * copy/move assignment operator for a non-union class X performs memberwise
 * copy/move assignment of its subobjects.  The direct base classes of X are
 * assigned first, in the order of their declaration in the
 * base-specifier-list, and then the immediate non-static data members of X
 * are assigned, in the order in which they were declared in the class
 * definition.  Let x be either the parameter of the function or, for the
 * move operator, an xvalue referring to the parameter.  Each subobject is
 * assigned in the manner appropriate to its type: (13.1) if the subobject is
 * of class type, as if by a call to operator= with the subobject as the
 * object expression and the corresponding subobject of x as a single
 * function argument; (13.2) if the subobject is an array, each element is
 * assigned, in the manner appropriate to the element type; (13.3) if the
 * subobject is of scalar type, the built-in assignment operator is used."
 *
 * What analysis does not declare, so that the assignment of such a class is
 * refused where it is used, as before: a union, a class with virtual bases,
 * an array of class type that is not assigned bytewise, a subobject whose
 * assignment operator is a template, protected, takes a non-const reference,
 * or is ambiguous.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* whether f is a move assignment operator X& operator=(X&&) of class c
   ([class.copy.assign]/3), without default arguments */
bool semantic_analysis::move_assignment_operator(entity_index f,
                                                 entity_index c) const
{
    const entity &x = entity_table[f];
    if (!x.nonstatic_member || x.enclosing != c || !x.operator_function ||
        x.implemented != preprocessing_op_or_punc::EQUAL)
        return false;
    const struct type &function = type_table[x.type.unqualified()];
    if (function.parameter_count != 1 || function.variadic)
        return false;
    qualified_type p = type_table.parameters(x.type.unqualified())[0];
    const struct type &r = type_table[p.unqualified()];
    if (r.kind != type_kind::rvalue_reference)
        return false;
    const struct type &t = type_table[r.operand.unqualified()];
    return t.kind == type_kind::class_type && t.entity == c;
}

/* whether member function f is deleted: by its flag, which a member
   function's body sets once analysed (a specialization's after the class
   that uses it), or by a declaration whose function-body is = delete */
bool semantic_analysis::declared_deleted(entity_index f) const
{
    if (entity_table[f].deleted)
        return true;
    for (uint32_t d = entity_table[f].first_declaration; d;
         d = entity_table.declaration(d).next) {
        auto definition =
            entity_table.declaration(d).node.function_definition();
        if (definition && definition.function_body().deleted())
            return true;
    }
    return false;
}

/*
 * [class.copy.assign]/7: whether the assignment of a subobject of class k,
 * from a const lvalue (the copy) or an xvalue (the move, /13's "x"), is
 * usable or makes the assignment operator of class c deleted: "a non-static
 * data member or direct base class that cannot be copied/moved because
 * overload resolution ([over.match]), as applied to find the corresponding
 * assignment operator, results in an ambiguity or a function that is deleted
 * or inaccessible".  Unknown where analysis does not follow overload
 * resolution.  non_throwing is cleared when the operator chosen may throw
 * ([except.spec]/8: the implicit one's specification allows what its
 * subobjects' operators do).
 */
semantic_analysis::assignment_use
semantic_analysis::subobject_assignment(entity_index k, entity_index c,
                                        bool move, bool &non_throwing,
                                        bool &nonconst_form)
{
    if (entity_table[k].kind != entity_kind::class_type)
        return assignment_use::unknown;
    lookup_result found = lookup_qualified(
        k, operator_name(preprocessing_op_or_punc::EQUAL), lookup_filter::any);
    if (found.unknown || found.ambiguous)
        return assignment_use::unknown;
    if (!found.entity) {
        /* no operator= at all: the bytes are copied */
        if (!assigned_bitwise(k))
            return assignment_use::unknown;
        if (copy_assignment_deleted(k) || bytewise_const_member(k))
            return assignment_use::deleted;
        return assignment_use::usable;
    }
    if (entity_table[found.entity].member_templates)
        return assignment_use::unknown;
    /* the candidates for the source: a const lvalue binds to X, const X&;
       an xvalue also to X&& */
    entity_index copy = 0, moving = 0, nonconst_copy = 0;
    uint32_t copies = 0, movings = 0;
    bool defaulted_usable = false;
    for (entity_index f = found.entity; f; f = entity_table[f].next_overload) {
        const entity &x = entity_table[f];
        if (x.enclosing != k || x.kind != entity_kind::function ||
            !x.nonstatic_member || !x.operator_function ||
            x.implemented != preprocessing_op_or_punc::EQUAL)
            return assignment_use::unknown;
        const struct type &function = type_table[x.type.unqualified()];
        if (function.parameter_count != 1 || function.variadic ||
            x.default_arguments || (x.member_cv & cv_const))
            return assignment_use::unknown;
        qualified_type p = type_table.parameters(x.type.unqualified())[0];
        const struct type &r = type_table[p.unqualified()];
        const struct type &referred = type_table[r.operand.unqualified()];
        bool of_k =
            referred.kind == type_kind::class_type && referred.entity == k;
        if (r.kind == type_kind::class_type && r.entity == k) {
            /* by value: initialized from the source by a copy or move
               constructor */
            if (!copies_bytes(k))
                return assignment_use::unknown;
            copy = f;
            copies++;
        } else if (r.kind == type_kind::lvalue_reference && of_k) {
            if (!(r.operand.cv() & cv_const)) {
                /* [class.copy.assign]/7: the X& form: not viable for a
                   const source, so the class's implicit one is X&
                   operator=(X&) (nonconst_form), and an xvalue binds
                   to it no more than to a const X& (below) */
                if (r.operand.cv() & cv_volatile)
                    return assignment_use::unknown;
                nonconst_form = true;
                nonconst_copy = f;
            }
            copy = f;
            copies++;
        } else if (r.kind == type_kind::rvalue_reference && of_k) {
            moving = f;
            movings++;
        } else if (r.kind == type_kind::class_type ||
                   (r.kind == type_kind::lvalue_reference &&
                    referred.kind == type_kind::class_type)) {
            /* another class: viable through a conversion not analysed */
            return assignment_use::unknown;
        }
        /* operator=(int) and the like: no candidate for this source */
    }
    /* X& operator=(const X&) = default and X& operator=(X&&) = default
       are no entities (defaulted_copy_assignment): usable as a trivial
       one is, when the class's bytes are assigned */
    const entity &kx = entity_table[k];
    if (kx.defaulted_copy_assignment || kx.defaulted_move_assignment) {
        if (!assigned_bitwise(k) && !kx.defaulted_move_assignment)
            return assignment_use::unknown;
        if (kx.defaulted_move_assignment && kx.defaulted_copy_assignment)
            return assignment_use::unknown;
        /* a trivial defaulted one beside user-declared ones */
        bool trivially = true;
        owned_array<entity_index> classes;
        subobject_classes(k, classes);
        for (entity_index m : classes)
            if (!assigned_bitwise(m))
                trivially = false;
        if (!trivially || kx.dynamic)
            return assignment_use::unknown;
        if (bytewise_const_member(k))
            return assignment_use::deleted;
        if (kx.defaulted_copy_assignment && !copies) {
            copies = 1;
            copy = 0;
            defaulted_usable = true;
        }
        if (kx.defaulted_move_assignment && !movings) {
            movings = 1;
            moving = 0;
            defaulted_usable = true;
        }
    }
    entity_index chosen = copy;
    uint32_t count = copies;
    if (move && movings) {
        chosen = moving;
        count = movings;
    }
    if (count > 1)
        return assignment_use::unknown;
    /* an xvalue does not bind to X&: no viable function */
    if (chosen && chosen == nonconst_copy && move)
        return assignment_use::deleted;
    if (!chosen && count && defaulted_usable)
        return assignment_use::usable;
    if (!chosen)
        return assignment_use::deleted;
    const entity &f = entity_table[chosen];
    if (declared_deleted(chosen))
        return assignment_use::deleted;
    if (!type_table[f.type.unqualified()].non_throwing)
        non_throwing = false;
    if (f.access == 2 && !grants(k, c))
        return assignment_use::deleted;
    if (f.access)
        return assignment_use::unknown;
    return assignment_use::usable;
}

/*
 * Whether the assignment of class c is not trivial ([class.copy.assign]/9):
 * it has virtual functions, or a subobject's assignment is not.
 */
bool semantic_analysis::assignment_not_trivial(entity_index c)
{
    if (entity_table[c].dynamic)
        return true;
    owned_array<entity_index> classes;
    subobject_classes(c, classes);
    for (entity_index k : classes)
        if (entity_table[k].kind == entity_kind::class_type &&
            !assigned_bitwise(k))
            return true;
    return false;
}

/* an implicitly-declared copy assignment operator X& operator=(const X&)
   or move assignment operator X& operator=(X&&) of class c, public and
   inline, deleted or to be defined at the class's end */
void semantic_analysis::declare_implicit_assignment(entity_index c, bool move,
                                                    bool deleted,
                                                    bool non_throwing,
                                                    bool nonconst)
{
    qualified_type self =
        qualified_type::make(entity_table[c].type.unqualified(), cv_none);
    qualified_type result =
        qualified_type::make(type_table.lvalue_reference_to(self), cv_none);
    qualified_type parameter;
    uint32_t source_cv = cv_const;
    if (nonconst)
        source_cv = cv_none;
    if (move)
        parameter =
            qualified_type::make(type_table.rvalue_reference_to(self), cv_none);
    else
        parameter = qualified_type::make(
            type_table.lvalue_reference_to(qualified_type::make(
                entity_table[c].type.unqualified(), source_cv)),
            cv_none);
    qualified_type type = qualified_type::make(
        type_table.function(result, &parameter, 1, false, non_throwing),
        cv_none);
    uint32_t name = operator_name(preprocessing_op_or_punc::EQUAL);
    entity_index e = entity_table.make(entity_kind::function, name, c);
    entity &f = entity_table[e];
    f.type = type;
    f.nonstatic_member = true;
    f.operator_function = true;
    f.implemented = preprocessing_op_or_punc::EQUAL;
    f.inline_ = true;
    f.defined = true;
    f.implicit = true;
    f.deleted = deleted;
    f.linkage_of_name = entity_table[c].linkage_of_name;
    entity_index head = entity_table.find(c, name);
    if (!head) {
        entity_table.bind(c, name, e);
        return;
    }
    /* last of the class's own chain, before a base's functions */
    entity_index last = head;
    while (entity_table[last].next_overload &&
           entity_table[entity_table[last].next_overload].enclosing == c)
        last = entity_table[last].next_overload;
    entity_table[e].next_overload = entity_table[last].next_overload;
    entity_table[last].next_overload = e;
}

/*
 * [class.copy.assign]/2, /3, /7: the implicitly-declared assignment
 * operators of class c, when they are not trivial.
 */
analysis_result semantic_analysis::implicit_assignments(entity_index c)
{
    const entity x = entity_table[c];
    /* C++03 has no deleted functions or move assignment */
    if (x.union_ || x.kind != entity_kind::class_type ||
        !tokens.files.behavior.has(language_feature::rvalue_references) ||
        !assignment_not_trivial(c))
        return analysis_result::well_formed;
    /* a virtual base is assigned once, not by each class that has it: not
       analysed */
    if (x.virtual_base_count)
        return analysis_result::well_formed;
    uint32_t name = operator_name(preprocessing_op_or_punc::EQUAL);
    entity_index head = entity_table.find(c, name);
    /* a base's operator= brought in by a using-declaration */
    if (head && entity_table[head].enclosing != c)
        return analysis_result::well_formed;
    bool user_copy = false, user_move = false;
    for (entity_index f = head; f && entity_table[f].enclosing == c;
         f = entity_table[f].next_overload) {
        if (entity_table[f].implicit)
            continue;
        if (copy_assignment_operator(f, c))
            user_copy = true;
        else if (move_assignment_operator(f, c))
            user_move = true;
    }
    /* [class.copy.assign]/1: a user-declared copy assignment operator;
       none is implicitly declared, nor a move one (/3.3) */
    if (user_copy)
        return analysis_result::well_formed;
    bool defaulted_copy = x.defaulted_copy_assignment;
    bool defaulted_move = x.defaulted_move_assignment;
    /* [class.dtor]/4 (an implicit destructor is no user-declared one) */
    bool user_destructor = x.destructor && !entity_table[x.destructor].implicit;
    bool user_copy_constructor =
        declares_copy_constructor(c) || x.defaulted_copy_constructor;
    /* (declares_move_constructor counts the implicit one too) */
    bool user_move_constructor = x.defaulted_move_constructor;
    for (entity_index k = x.first_constructor; k;
         k = entity_table[k].next_constructor)
        if (!entity_table[k].implicit && copy_or_move_constructor(c, k) &&
            type_table[type_table
                           .parameters(entity_table[k].type.unqualified())[0]
                           .unqualified()]
                    .kind == type_kind::rvalue_reference)
            user_move_constructor = true;
    /* the subobjects' assignments */
    owned_array<entity_index> order;
    construction_order(c, order);
    bool copy_deleted = false, move_deleted = false;
    bool copy_non_throwing = true, move_non_throwing = true;
    bool nonconst_form = false;
    for (entity_index e : order) {
        qualified_type t = entity_table[e].type;
        const struct type &r = type_table[t.unqualified()];
        if (r.kind == type_kind::lvalue_reference ||
            r.kind == type_kind::rvalue_reference ||
            (cv_of(type_table, t) & cv_const)) {
            /* /7: a member of reference type or of const type */
            copy_deleted = true;
            move_deleted = true;
            continue;
        }
        entity_index k = element_class(t);
        if (!k)
            continue;
        if (r.kind == type_kind::array && !assigned_bitwise(k))
            /* element by element: not analysed */
            return analysis_result::well_formed;
        assignment_use copying =
            subobject_assignment(k, c, false, copy_non_throwing, nonconst_form);
        assignment_use moving =
            subobject_assignment(k, c, true, move_non_throwing, nonconst_form);
        if (copying == assignment_use::unknown ||
            moving == assignment_use::unknown)
            return analysis_result::well_formed;
        if (copying == assignment_use::deleted)
            copy_deleted = true;
        if (moving == assignment_use::deleted)
            move_deleted = true;
    }
    /* /2: a declared move constructor or move assignment operator makes the
       implicit copy assignment operator deleted, unless it is defaulted */
    if (!defaulted_copy &&
        (user_move_constructor || user_move || defaulted_move))
        copy_deleted = true;
    declare_implicit_assignment(c, false, copy_deleted, copy_non_throwing,
                                nonconst_form);
    /* /3: the move assignment operator; /7: "A defaulted move assignment
       operator that is defined as deleted is ignored by overload
       resolution" */
    bool declare_move = !user_move && !move_deleted;
    if (!defaulted_move && (user_copy_constructor || user_move_constructor ||
                            defaulted_copy || user_destructor))
        declare_move = false;
    if (declare_move)
        declare_implicit_assignment(c, true, false, move_non_throwing);
    return analysis_result::well_formed;
}

/*
 * The bodies of class c's implicitly-declared assignment operators
 * ([class.copy.assign]/12, /13): each base and member of *this assigned
 * from the corresponding one of the source, then return *this.
 */
analysis_result
semantic_analysis::implicit_assignment_definitions(entity_index c, SyntaxRef n)
{
    uint32_t name = operator_name(preprocessing_op_or_punc::EQUAL);
    owned_array<entity_index> operators;
    for (entity_index f = entity_table.find(c, name);
         f && entity_table[f].enclosing == c; f = entity_table[f].next_overload)
        if (entity_table[f].implicit && !entity_table[f].deleted)
            operators.append(f);
    qualified_type self =
        qualified_type::make(entity_table[c].type.unqualified(), cv_none);
    qualified_type returns =
        qualified_type::make(type_table.lvalue_reference_to(self), cv_none);
    for (entity_index f : operators) {
        size_t before = unsupported.size();
        analysis_result r = analysis_result::well_formed;
        {
            /* a body that fails is no error of the class: the operator
               may never be used ([temp.inst]/4's instantiated members
               are treated the same, instantiated_member_body) */
            diagnostics_suppressed hush(diagnostics);
            /* [class.copy.assign]/12: defined when odr-used: what its body
               needs waits for its use (as constructor.cpp's defaulted
               members) */
            scoped_value<entity_index> deferring(needs_deferred_by,
                                                 needs_deferred_by);
            if (!entity_table[f].member_definition_needed)
                needs_deferred_by = f;
            r = implicit_assignment_body(c, n, f, returns);
        }
        if (r == analysis_result::well_formed)
            continue;
        /* [class.copy.assign]/7: an assignment operator that would be
           ill-formed is defined as deleted, an error only where it is used;
           what analysis does not follow is left undefined, and a use of it
           is a refusal (lowering), never code without what the body did */
        if (r == analysis_result::ill_formed || unsupported.size() <= before)
            not_supported(n, "class.copy.assign");
        entity_table[f].defined = false;
        body_failures.append(unsupported[before]);
        entity_table[f].unsupported_body =
            static_cast<uint32_t>(body_failures.size());
        unsupported.resize(before);
    }
    return analysis_result::well_formed;
}

/* the body of implicit assignment operator f of class c: each subobject
   assigned, then return *this */
analysis_result semantic_analysis::implicit_assignment_body(
    entity_index c, SyntaxRef n, entity_index f, qualified_type returns)
{
    token_index at = n.first_token();
    qualified_type self =
        qualified_type::make(entity_table[c].type.unqualified(), cv_none);
    synthesized_function sf;
    begin_synthesized(
        sf, f, returns,
        qualified_type::make(type_table.pointer_to(self), cv_none));
    qualified_type parameter =
        type_table.parameters(entity_table[f].type.unqualified())[0];
    entity_index source = synthesized_variable(sf, parameter, true);
    bool move =
        type_table[parameter.unqualified()].kind == type_kind::rvalue_reference;
    owned_array<uint32_t> statements;
    analysis_result r = implicit_assignment(c, n, source, move, statements);
    if (r == analysis_result::well_formed) {
        /* return *this; */
        expression_meaning object;
        r = this_object(at, object);
        initialized_value value;
        if (r == analysis_result::well_formed)
            r = initialize_with(n, object, returns, false, false, value);
        if (r == analysis_result::well_formed)
            statements.append(statement_node(semantic_kind::return_statement,
                                             at, value.node));
    }
    finish_synthesized(sf, n, at, block_statement(at, statements));
    return r;
}

analysis_result
semantic_analysis::implicit_assignment(entity_index c, SyntaxRef n,
                                       entity_index source, bool move,
                                       owned_array<uint32_t> &statements)
{
    token_index at = n.first_token();
    owned_array<entity_index> order;
    construction_order(c, order);
    for (entity_index e : order) {
        expression_meaning from;
        from.category = value_category::lvalue;
        from.type = type_table[entity_table[source].type.unqualified()].operand;
        from.evaluation = constness::not_constant;
        from.node = expression_node(semantic_kind::entity, from, at, source);
        analysis_result r = analysis_result::well_formed;
        if (entity_table[e].base_subobject) {
            VTRY_SET(
                r,
                to_base(at,
                        type_table[entity_table[e].type.unqualified()].entity,
                        from));
        } else {
            VTRY_SET(r, member_access(at, e, from));
        }
        /* /13: "an xvalue referring to the parameter" for the move */
        if (move)
            from.category = value_category::xvalue;
        expression_meaning object;
        VTRY_SET(r, subobject(at, e, object));
        qualified_type type = entity_table[e].type;
        const struct type &record = type_table[type.unqualified()];
        entity_index k = element_class(type);
        if (record.kind == type_kind::class_type) {
            /* /13.1: as if by a call to operator= */
            if (assigned_bitwise(k) && empty_class(k))
                /* no bytes */
                continue;
            expression_meaning result;
            VTRY_SET(r, assignment_of(n, at, preprocessing_op_or_punc::EQUAL,
                                      SyntaxElement(), SyntaxElement(), object,
                                      from, result));
            statements.append(statement_node(
                semantic_kind::expression_statement, at, result.node));
            continue;
        }
        if (k && !assigned_bitwise(k))
            return not_supported(n, "class.copy.assign");
        /* /13.2, /13.3: an array of bytewise-assigned elements, or the
           scalar, as by the built-in assignment */
        VTRY_SET(r, operand_value(n, from));
        statements.append(subobject_store(object, type, at, from.node));
    }
    return analysis_result::well_formed;
}

} // namespace cplusplus
