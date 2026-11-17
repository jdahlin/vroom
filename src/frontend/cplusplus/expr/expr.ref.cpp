/*
 * expr.ref.cpp: [expr.ref] Class member access, and the two other ways an
 * expression names a member: [expr.prim.this] and an unqualified name of
 * a non-static member in a member function ([class.mfct.non-static]/2).
 *
 *     postfix-expression:
 *         postfix-expression . template_opt id-expression
 *         postfix-expression -> template_opt id-expression
 *
 * The grammar is expr.post.cpp's.  An id-expression that is an identifier,
 * naming a non-static data member or member function of the class (there
 * are no base classes, static data members or overload sets yet).
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

/*
 * [expr.ref]/6.2 and /6.3: the member e of the object m, a glvalue (or a
 * prvalue, materialized: [basic.lval]/7) of the member's class.
 */
analysis_result semantic_analysis::member_access(token_index at, entity_index e,
                                                 expression_meaning &m)
{
    if (!accessible(e))
        /* [class.access]/1.1: "private; that is, its name can be used only
           by members and friends of the class in which it is declared" */
        return ill_formed_at_token(at,
                                   "a private or protected member's name can "
                                   "be used only by members and friends of "
                                   "the class in which it is declared",
                                   "class.access");
    const entity &x = entity_table[e];
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
        result.category = value_category::xvalue;
        if (m.category == value_category::lvalue)
            result.category = value_category::lvalue;
        result.evaluation = constness::unknown;
        if (m.evaluation == constness::not_constant)
            result.evaluation = constness::not_constant;
        result.node =
            expression_node(semantic_kind::member, result, at, object, e);
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
        if (m.category == value_category::prvalue)
            return not_supported(current_declaration, "class.temporary");
        /* [class.this]/3: "A member function whose type has a
           cv-qualifier-seq cv1 can be called on an object expression of
           type cv2 T only if cv1 is the same as or more cv-qualified than
           cv2." */
        if (m.type.cv() & ~x.member_cv)
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
    /* a static member, a type, an enumerator */
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
    if (!function_state.this_entity ||
        entity_table[e].enclosing != current_class())
        /* in a static member function, or unevaluated ([expr.prim.id]/2) */
        return not_supported(current_declaration, "expr.prim.id");
    expression_meaning object;
    analysis_result r = this_pointer(at, object);
    if (r != analysis_result::well_formed)
        return r;
    /* (*this): [expr.unary.op]/1 */
    object.type = type_table[object.type.unqualified()].operand;
    object.category = value_category::lvalue;
    object.evaluation = constness::not_constant;
    object.node =
        expression_node(semantic_kind::indirection, object, at, object.node);
    r = member_access(at, e, object);
    if (r != analysis_result::well_formed)
        return r;
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
analysis_result semantic_analysis::member_reference(node_index n,
                                                    expression_meaning &m)
{
    const syntax_child *children = tree.children(n);
    bool arrow = syntax_is_punctuator(tokens, children[1],
                                      preprocessing_op_or_punc::ARROW);
    if (!children[2].is_token() ||
        tokens[children[2].index()].kind != token_kind::identifier)
        /* template, a qualified-id, a destructor's name, an operator
           function's */
        return not_supported(n, "expr.ref");
    token_index name = children[2].index();
    analysis_result r = expression(children[0], m);
    if (r != analysis_result::well_formed)
        return r;
    if (arrow) {
        r = operand_value(n, m);
        if (r != analysis_result::well_formed)
            return r;
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
        m.node = expression_node(semantic_kind::indirection, m,
                                 tree[n].first_token, m.node);
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
    entity_index e = entity_table.find(c, tokens.identifier(name));
    if (e && entity_table[e].kind == entity_kind::none)
        return not_supported(n, "over");
    if (!e)
        return ill_formed_at_token(name,
                                   "the id-expression shall name a member "
                                   "of the class",
                                   "expr.ref");
    return member_access(name, e, m);
}

} // namespace cplusplus
