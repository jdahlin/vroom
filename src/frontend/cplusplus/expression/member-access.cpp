/*
 * member-access.cpp: [expr.ref] Class member access, and the two other ways an
 * expression names a member: [expr.prim.this] and an unqualified name of
 * a non-static member in a member function ([class.mfct.non-static]/2).
 *
 *     postfix-expression:
 *         postfix-expression . template_opt id-expression
 *         postfix-expression -> template_opt id-expression
 *
 * The grammar is postfix-expression.cpp's.  An id-expression that is an
 * identifier, naming a non-static data member or member function (or an
 * overload set of them, [over.call.func]/2) of the class or of one of its bases
 * (there are no static data members yet).
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/*
 * [expr.prim.this]/1: "The keyword this names a pointer to the object for
 * which a non-static member function ([class.this]) is invoked ... The
 * expression this is a prvalue."  /2: "If a declaration declares a member
 * function or member function template of a class X, the expression this
 * is a prvalue of type "pointer to cv-qualifier-seq X" between the
 * optional cv-qualifier-seq and the end of the function-definition"; /3:
 * "It shall not appear within the declaration of a static member function
 * ... it shall not appear elsewhere."
 */
analysis_result semantic_analysis::this_pointer(token_index t,
                                                expression_meaning &m)
{
    if (current_lambda())
        return captured_this(t, m);
    return function_this(t, m);
}

/* this in the function analysed now, its own (a lambda's function call
   operator's: its closure object's address) */
analysis_result semantic_analysis::function_this(token_index t,
                                                 expression_meaning &m)
{
    entity_index e = function_state.this_entity;
    if (!e) {
        if (current_class())
            return ill_formed_at_token(t,
                                       "the expression this shall not "
                                       "appear within the declaration of a "
                                       "static member function",
                                       "expr.prim.this");
        return ill_formed_at_token(t,
                                   "the expression this shall appear only in "
                                   "a member function or a default member "
                                   "initializer",
                                   "expr.prim.this");
    }
    m = expression_meaning();
    m.category = value_category::lvalue;
    m.type = entity_table[e].type;
    m.entity = e;
    m.evaluation = constness::not_constant;
    m.node = expression_node(semantic_kind::entity, m, t, e);
    analysis_result r = operand_value(current_declaration, m);
    m.evaluation = constness::not_constant;
    return r;
}

/* [expr.unary.op]/1: "The unary * operator performs indirection: the
   expression to which it is applied shall be a pointer to an object type
   ... and the result is an lvalue referring to the object" */
analysis_result semantic_analysis::this_object(token_index t,
                                               expression_meaning &m, bool own)
{
    analysis_result r = analysis_result::well_formed;
    if (own)
        r = function_this(t, m);
    else
        VTRY_SET(r, this_pointer(t, m));
    m.type = type_table[m.type.unqualified()].operand;
    m.category = value_category::lvalue;
    m.evaluation = constness::not_constant;
    m.entity = 0;
    m.node = expression_node(semantic_kind::indirection, m, t, m.node);
    return analysis_result::well_formed;
}

/*
 * [expr.ref]/6.2 and /6.3: the member e of the object m, a glvalue (or a
 * prvalue, materialized: [basic.lval]/7) of the member's class.
 */
analysis_result semantic_analysis::member_access(token_index at, entity_index e,
                                                 expression_meaning &m)
{
    /* E1.E2 names no structured binding, whatever E1 does */
    m.binding = 0;
    entity_index object_class = type_table[m.type.unqualified()].entity;
    entity_index through =
        entity_table[entity_table[e].enclosing].anonymous_object;
    if (entity_table[e].nonstatic_member && through) {
        /* [class.union.anon]/1: a member of an anonymous union, of its
           unnamed object */
        VTRY(r, member_access(at, through, m));
        object_class = type_table[m.type.unqualified()].entity;
    }
    if (aggregate_prvalue(m))
        return not_supported(current_declaration, "class.temporary");
    if (!accessible(e, object_class))
        /* [class.access]/1.1: "private; that is, its name can be used only
           by members and friends of the class in which it is declared" */
        return ill_formed_at_token(at,
                                   "a private or protected member's name can "
                                   "be used only by members and friends of "
                                   "the class in which it is declared",
                                   "class.access");
    const entity &x = entity_table[e];
    if (x.nonstatic_member && x.enclosing != object_class) {
        /* [class.derived]/1: a member of a base class, of the base class
           subobject */
        VTRY(r, to_base(at, x.enclosing, m));
    }
    if (x.kind == entity_kind::variable && x.nonstatic_member) {
        /* [expr.ref]/6.2: "If E2 is a non-static data member and the type
           of E1 is "cq1 vq1 X", and the type of E2 is "cq2 vq2 T", the
           expression designates the corresponding member subobject of the
           object designated by the first expression.  If E1 is an lvalue,
           then E1.E2 is an lvalue; otherwise E1.E2 is an xvalue. ... The
           type of E1.E2 is "cq12 vq12 T"." */
        uint32_t object = m.node;
        uint32_t cv = m.type.cv() | x.type.cv();
        expression_meaning result;
        result.type = qualified_type::make(x.type.unqualified(), cv);
        /* an array's cv-qualifiers are its elements' ([basic.type.qualifier]/3) */
        if (type_table[x.type.unqualified()].kind == type_kind::array)
            result.type = add_cv(x.type, m.type.cv());
        result.category = value_category::xvalue;
        if (m.category == value_category::lvalue)
            result.category = value_category::lvalue;
        const struct type &declared = type_table[x.type.unqualified()];
        if (declared.kind == type_kind::lvalue_reference ||
            declared.kind == type_kind::rvalue_reference) {
            /* [expr.ref]/6.1: "If E2 is declared to have type "reference
               to T", then E1.E2 is an lvalue of type T." */
            result.type = declared.operand;
            result.category = value_category::lvalue;
        }
        result.evaluation = constness::unknown;
        if (m.evaluation == constness::not_constant)
            result.evaluation = constness::not_constant;
        result.node =
            expression_node(semantic_kind::member, result, at, object, e);
        /* [expr.ref]/6.2: "If E2 is a bit-field, E1.E2 is a bit-field." */
        if (x.bit_field)
            result.bit_field_width = x.bit_width;
        m = result;
        return analysis_result::well_formed;
    }
    if (x.kind == entity_kind::function && x.nonstatic_member) {
        /* [expr.ref]/6.3.2: "Otherwise, if E1.E2 refers to a non-static
           member function and the type of E2 is "function of
           parameter-type-list cv ref-qualifier_opt returning T", then
           E1.E2 is a prvalue.  The expression designates a non-static
           member function.  The expression can be used only as the
           left-hand operand of a member function call." */
        /* [basic.lval]/7: "Whenever a prvalue appears as an operand of an
           operator that expects a glvalue for that operand, the temporary
           materialization conversion is applied to convert the expression
           to an xvalue."  One with a destructor is destroyed at the end of
           the full-expression ([class.temporary]/4), not analysed yet */
        if (m.category == value_category::prvalue &&
            entity_table[x.enclosing].destructor)
            return not_supported(current_declaration, "class.temporary");
        /* [class.this]/3: "A member function whose type has a
           cv-qualifier-seq cv1 can be called on an object expression of
           type cv2 T only if cv1 is the same as or more cv-qualified than
           cv2." */
        /* [over.over]/1 (named_entity); with more than one function the
           cv-qualifiers are the implicit object parameter's, of
           overload resolution ([over.match.funcs]/4) */
        if (x.next_overload && at != overloaded_callee)
            return not_supported(current_declaration, "over.over");
        if (!x.next_overload) {
            /* [dcl.fct.def.delete]/2 */
            VTRY(r, deleted_use(at, e));
        }
        if ((m.type.cv() & ~x.member_cv) && !x.next_overload)
            return ill_formed_at_token(at,
                                       "a member function can be called on "
                                       "an object expression only if its "
                                       "cv-qualifier-seq is the same as or "
                                       "more cv-qualified than the object's",
                                       "class.this");
        expression_meaning address;
        address.type =
            qualified_type::make(type_table.pointer_to(m.type), cv_none);
        address.category = value_category::prvalue;
        address.evaluation = constness::not_constant;
        address.node =
            expression_node(semantic_kind::address, address, at, m.node);
        expression_meaning result;
        result.type = x.type;
        result.category = value_category::prvalue;
        result.evaluation = constness::not_constant;
        result.node = expression_node(semantic_kind::bound_member_function,
                                      result, at, address.node, e);
        m = result;
        return analysis_result::well_formed;
    }
    if ((x.kind == entity_kind::variable || x.kind == entity_kind::function) &&
        !x.nonstatic_member && !x.next_overload) {
        /* [expr.ref]/6.1: "If E2 is a static data member and the type of E2
           is T, then E1.E2 is an lvalue; the expression designates the named
           member of the class."  /6.3.1: "If it refers to a static member
           function and the type of E2 is "function of parameter-type-list
           returning T", then E1.E2 is an lvalue; the expression designates
           the static member function."  /1: "The postfix expression before
           the dot or arrow is evaluated", first, as a comma's left operand */
        uint32_t object = m.node;
        constness object_evaluation = m.evaluation;
        expression_meaning result;
        VTRY(r, named_entity(at, e, result));
        if (object_evaluation != constness::constant &&
            result.evaluation != constness::not_constant)
            result.evaluation = constness::unknown;
        result.entity = 0;
        result.node = expression_node(semantic_kind::comma, result, at, object,
                                      result.node);
        m = result;
        return analysis_result::well_formed;
    }
    /* a type, an enumerator, an overloaded static member function */
    return not_supported(current_declaration, "expr.ref");
}

/*
 * [class.mfct.non-static]/2: "When an id-expression that is not part of a
 * class member access syntax and not used to form a pointer to member is
 * used in a member of class X in a context where this can be used, if name
 * lookup resolves the name in the id-expression to a non-static non-type
 * member of some class C, and if either the id-expression is potentially
 * evaluated or C is X or a base class of X, the id-expression is
 * transformed into a class member access expression using (*this) as the
 * postfix-expression to the left of the . operator."
 */
analysis_result semantic_analysis::implicit_member_access(token_index at,
                                                          entity_index e,
                                                          expression_meaning &m)
{
    bool ambiguous = false;
    /* [class.union.anon]/1: a member of an anonymous union is one of the
       class whose member its unnamed object is */
    entity_index member_of = entity_table[e].enclosing;
    while (entity_table[member_of].anonymous_object)
        member_of =
            entity_table[entity_table[member_of].anonymous_object].enclosing;
    if (!function_state.this_entity ||
        (member_of != current_class() &&
         !base_step(current_class(), member_of, ambiguous)))
        /* in a static member function, or unevaluated ([expr.prim.id]/2) */
        return not_supported(current_declaration, "expr.prim.id");
    expression_meaning object;
    VTRY(r, this_object(at, object));
    VTRY_SET(r, member_access(at, e, object));
    m = object;
    return analysis_result::well_formed;
}

/*
 * [expr.ref]/1-/4: E1.E2 and E1->E2.  /2: "For the first option (dot) the
 * first expression shall be a glvalue.  For the second option (arrow) the
 * first expression shall be a prvalue having pointer type.  The expression
 * E1->E2 is converted to the equivalent form (*(E1)).E2"; /4: "the object
 * expression shall be of class type.  The class type shall be complete
 * unless the class member access appears in the definition of that class.
 * ... The id-expression shall name a member of the class or of one of its
 * base classes."
 */
analysis_result semantic_analysis::member_reference(SyntaxRef n,
                                                    expression_meaning &m)
{
    bool arrow = n[1].is(preprocessing_op_or_punc::ARROW);
    if (!n[2].is_identifier())
        /* template, a qualified-id, a destructor's name, an operator
           function's */
        return not_supported(n, "expr.ref");
    token_index name = n[2].token();
    /* (the call's callee name, which analysing the object expression's
       calls changes) */
    token_index callee = overloaded_callee;
    VTRY(r, expression(n[0], m));
    /* [over.ref]/1: "An expression x->m is interpreted as
       (x.operator->())->m for a class object x of type T if T::operator->()
       exists and if the operator is selected as the best match function by
       the overload resolution mechanism", again while the result is one */
    for (unsigned depth = 0; arrow; depth++) {
        if (type_table[m.type.unqualified()].kind != type_kind::class_type)
            break;
        if (depth == 16)
            /* a chain of operator-> this long, or a cycle */
            return not_supported(n, "over.ref");
        bool handled = false;
        VTRY_SET(r, unary_operator_function(n, n[1].token(),
                                            preprocessing_op_or_punc::ARROW,
                                            false, n[0], m, handled));
        if (!handled)
            break;
    }
    if (arrow) {
        VTRY_SET(r, operand_value(n, m));
        const struct type pointer = type_table[m.type.unqualified()];
        if (pointer.kind != type_kind::pointer ||
            type_table[pointer.operand.unqualified()].kind !=
                type_kind::class_type) {
            if (fundamental_of(m.type) == fundamental_type::none &&
                pointer.kind != type_kind::pointer)
                return not_supported(n, "expr.ref");
            return ill_formed(n,
                              "the first expression of -> shall be a "
                              "pointer to a class",
                              "expr.ref");
        }
        m.type = pointer.operand;
        m.category = value_category::lvalue;
        if (m.evaluation != constness::not_constant)
            m.evaluation = constness::unknown;
        m.node = expression_node(semantic_kind::indirection, m, n.first_token(),
                                 m.node);
    }
    const struct type object = type_table[m.type.unqualified()];
    if (object.kind != type_kind::class_type) {
        if (fundamental_of(m.type) == fundamental_type::none &&
            object.kind != type_kind::pointer)
            return not_supported(n, "expr.ref");
        return ill_formed(n,
                          "the object expression of a class member access "
                          "shall be of class type",
                          "expr.ref");
    }
    entity_index c = object.entity;
    if (entity_table[c].kind != entity_kind::class_type)
        /* a class whose definition was not analysed */
        return not_supported(n, "class.pre");
    if (!entity_table[c].size)
        return ill_formed(n,
                          "the class type shall be complete unless the class "
                          "member access appears in the definition of that "
                          "class",
                          "expr.ref");
    bool ambiguous = false;
    entity_index e = class_member_lookup(c, tokens.identifier(name), ambiguous);
    if (ambiguous)
        return not_supported(n, "class.member.lookup");
    if (e && entity_table[e].kind == entity_kind::none) {
        /* [temp.mem]/1: member function templates, called: the call
           deduces their arguments (member_template_call) */
        if (name == callee && member_function_template_of(e)) {
            member_template_name = e;
            member_template_object = m;
            return analysis_result::well_formed;
        }
        return not_supported(n, "over");
    }
    if (!e)
        return ill_formed_at_token(name,
                                   "the id-expression shall name a member "
                                   "of the class",
                                   "expr.ref");
    return member_access(name, e, m);
}

} // namespace cplusplus
