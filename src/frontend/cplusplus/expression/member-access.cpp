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
    /* a generic lambda's specialization: its lambda-declarator analysed
       again, away from the member function the lambda-expression is in,
       whose this it names ([expr.prim.this]/2); in an unevaluated operand
       (decltype(f(this))) only its type matters */
    if (!e && unevaluated && specializing_lambda_this.unqualified()) {
        m = expression_meaning();
        m.category = value_category::prvalue;
        m.type = specializing_lambda_this;
        m.evaluation = constness::not_constant;
        return analysis_result::well_formed;
    }
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
    if (entity_table[e].argument_bound)
        argument_named = true;
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

/* [dcl.init]/8: whether the value-initialization of the class is its
   zero-initialization alone: no constructor to call, of it, a base class or
   a member */
bool semantic_analysis::trivial_zero_initialized(entity_index c)
{
    bool known = true;
    if (!trivial_default_constructor(c, known) || !known)
        return false;
    const entity &k = entity_table[c];
    for (uint32_t i = 0; i < k.member_count; i++) {
        const entity &m = entity_table[class_members[k.first_member + i]];
        if (m.kind != entity_kind::variable || !m.nonstatic_member)
            continue;
        const struct type &t = type_table[m.type.unqualified()];
        if (t.kind == type_kind::class_type &&
            !trivial_zero_initialized(t.entity))
            return false;
    }
    return true;
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
    if (aggregate_prvalue(m) && !at_block_scope() &&
        (entity_table[e].kind != entity_kind::variable ||
         !entity_table[e].nonstatic_member ||
         entity_table[e].enclosing != object_class ||
         (semantics[m.node].kind == semantic_kind::zero &&
          !trivial_zero_initialized(object_class))))
        /* [class.temporary]/2: a member function called on one, a static
           member, or a member of a value-initialized temporary (the evaluator
           has no rule for zero-initialization before a non-constexpr
           constructor) in a namespace-scope initializer: not analysed; a
           data member of an initializer list's temporary is analysed (the
           temporary is the initializer's own, destroyed with it) */
        return not_supported_because(
            current_declaration, "class.temporary",
            "a member of an aggregate prvalue at namespace scope");
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
           subobject (accessible() above already checked access to the member
           through object_class, including public using-declarations of private
           base members, [class.access.base]/5) */
        VTRY(r, to_base(at, x.enclosing, m, false));
    }
    if (x.kind == entity_kind::variable && x.nonstatic_member) {
        /* [expr.ref]/6.2: "If E2 is a non-static data member and the type
           of E1 is "cq1 vq1 X", and the type of E2 is "cq2 vq2 T", the
           expression designates the corresponding member subobject of the
           object designated by the first expression.  If E1 is an lvalue,
           then E1.E2 is an lvalue; otherwise E1.E2 is an xvalue. ... The
           type of E1.E2 is "cq12 vq12 T"." */
        uint32_t object = m.node;
        /* "If E2 is declared to be a mutable member, then the type of E1.E2
           is vq12 T" */
        uint32_t object_cv = m.type.cv();
        if (x.mutable_member)
            object_cv &= ~cv_const;
        uint32_t cv = object_cv | x.type.cv();
        expression_meaning result;
        result.type = qualified_type::make(x.type.unqualified(), cv);
        /* an array's cv-qualifiers are its elements' ([basic.type.qualifier]/3) */
        if (type_table[x.type.unqualified()].kind == type_kind::array)
            result.type = add_cv(x.type, object_cv);
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
        if (m.evaluation == constness::not_constant && !aggregate_prvalue(m))
            result.evaluation = constness::not_constant;
        result.node =
            expression_node(semantic_kind::member, result, at, object, e);
        /* [expr.ref]/6.2: "If E2 is a bit-field, E1.E2 is a bit-field." */
        if (x.bit_field)
            result.bit_field_width = x.bit_width;
        m = result;
        return analysis_result::well_formed;
    }
    /* [over.match.funcs]/2, /4: a set that begins with a static member
       function and has a non-static one is resolved with the object as the
       implied object argument (a static one is considered to match any
       object) */
    bool mixed_set = false;
    if (x.kind == entity_kind::function && !x.nonstatic_member &&
        at == overloaded_callee)
        for (entity_index o = x.next_overload; o;
             o = entity_table[o].next_overload)
            if (entity_table[o].nonstatic_member)
                mixed_set = true;
    if (x.kind == entity_kind::function && (x.nonstatic_member || mixed_set)) {
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
           the full-expression ([class.temporary]/4): lowering holds it in
           a temporary of the full-expression. */
        /* [class.this]/3: "A member function whose type has a
           cv-qualifier-seq cv1 can be called on an object expression of
           type cv2 T only if cv1 is the same as or more cv-qualified than
           cv2." */
        /* [over.over]/1 (named_entity); with more than one function the
           cv-qualifiers are the implicit object parameter's, of
           overload resolution ([over.match.funcs]/4) */
        if (x.next_overload && at != overloaded_callee)
            return not_supported_because(
                current_declaration, "over.over",
                "an overloaded member function named without a call");
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
        /* the class of the object expression as written, before the
           conversion to the member's class above: [class.protected]/1's
           check of the function chosen is made against it */
        semantics[result.node].c = object_class;
        m = result;
        return analysis_result::well_formed;
    }
    if ((x.kind == entity_kind::variable || x.kind == entity_kind::function ||
         x.kind == entity_kind::enumerator) &&
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
        /* [impl.clang] with -fms-compatibility, clang takes a member enumerator named
           through an object that is no constant as a constant (s->E in an
           array bound): not analysed */
        if (x.kind == entity_kind::enumerator &&
            object_evaluation != constness::constant &&
            tokens.files.behavior.microsoft_compatibility)
            return not_supported_because(current_declaration, "expr.ref",
                                         "an enumerator through a non-constant "
                                         "object in Microsoft mode");
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
    return not_supported_because(
        current_declaration, "expr.ref",
        "a member access naming a member of an unhandled kind");
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
    entity_index cls = current_class();
    if (!cls)
        cls = defining_class;
    bool of_current =
        cls && (member_of == cls || base_step(cls, member_of, ambiguous));
    expression_meaning object;
    if ((!function_state.this_entity || !of_current) && unevaluated &&
        entity_table[e].kind == entity_kind::variable &&
        entity_table[e].nonstatic_member) {
        /* [expr.prim.id]/3: "An id-expression that denotes a non-static
           data member ... can only be used: as part of a class member
           access in which the object expression refers to the member's
           class or a class derived from that class, or to form a pointer to
           member, or if that id-expression denotes a non-static data member
           and it appears in an unevaluated operand."  Any class's, S::m in
           sizeof included: the object is the member's class's, an lvalue,
           not evaluated */
        object.type = entity_table[member_of].type;
        object.category = value_category::lvalue;
        object.evaluation = constness::not_constant;
        object.node =
            expression_node(semantic_kind::indirection, object, at, 0);
        VTRY(r, member_access(at, e, object));
        m = object;
        return analysis_result::well_formed;
    }
    if (!function_state.this_entity || !of_current)
        /* in a static member function, or a member function unevaluated */
        return not_supported_because(
            current_declaration, "expr.prim.id",
            "a non-static member used without a usable this pointer");
    VTRY(r, this_object(at, object));
    VTRY_SET(r, member_access(at, e, object));
    m = object;
    return analysis_result::well_formed;
}

/* the simple-template-id of E1.f<T> and E1.template f<T>, else nothing */
SyntaxElement semantic_analysis::member_template_id(SyntaxRef n) const
{
    SyntaxElement id;
    if (n.size() == 3)
        id = n[2];
    else if (n.size() == 4 && n[2].is(standard_keyword::template_))
        id = n[3];
    if (id.is(syntax_kind::qualified_id) && id.size() >= 2)
        id = id.back();
    if (id.simple_template_id().template_name())
        return id;
    if (id.is(syntax_kind::template_id) && id.size() >= 3 &&
        id[0].operator_function_id())
        return id;
    return SyntaxElement{};
}

/*
 * [expr.ref]/1-/2: the object expression of E1.E2 and E1->E2, analysed as
 * the glvalue of the object (E1->E2 is "(*(E1)).E2").  A pseudo-destructor's
 * is of scalar type ([expr.ref]/3: scalar_allowed), another's of class type
 * (/4).
 */
analysis_result semantic_analysis::member_object(SyntaxRef n, bool arrow,
                                                 bool scalar_allowed,
                                                 expression_meaning &m)
{
    /* (the call's callee name, which analysing the object expression's
       calls changes) */
    token_index callee = overloaded_callee;
    VTRY(r, expression(n[0], m));
    /* (a call in the object expression left it unset) */
    overloaded_callee = callee;
    /* [over.ref]/1: "An expression x->m is interpreted as
       (x.operator->())->m for a class object x of type T if T::operator->()
       exists and if the operator is selected as the best match function by
       the overload resolution mechanism", again while the result is one */
    for (unsigned depth = 0; arrow; depth++) {
        if (type_table[m.type.unqualified()].kind != type_kind::class_type)
            break;
        if (depth == 16)
            /* a chain of operator-> this long, or a cycle */
            return not_supported_because(n, "over.ref",
                                         "an operator-> chain deeper than 16");
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
            (!scalar_allowed &&
             type_table[pointer.operand.unqualified()].kind !=
                 type_kind::class_type)) {
            if (fundamental_of(m.type) == fundamental_type::none &&
                pointer.kind != type_kind::pointer)
                return not_supported_because(
                    n, "expr.ref", "-> on an operand of an unhandled type");
            /* [ext:clang_ext_vector_type]: v->xy, an element access of a
               vector, is not analysed */
            if (pointer.kind == type_kind::pointer &&
                (type_table[pointer.operand.unqualified()].kind ==
                     type_kind::ext_vector ||
                 type_table[pointer.operand.unqualified()].kind ==
                     type_kind::vector))
                return not_supported_because(
                    n, "ext.clang",
                    "member access through a pointer to a vector type");
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
    token_index name = 0;
    entity_index qual_class = 0;
    bool conversion_id = false;
    qualified_type conversion_target;
    if (n[2].is_identifier()) {
        name = n[2].token();
    } else if (n[2].is(syntax_kind::qualified_id) && n[2].size() >= 2 &&
               n[2][0].is(syntax_kind::nested_name_specifier)) {
        /* [expr.ref]/4: E1.B::m or E1->B::m */
        nominated_scope qscope;
        VTRY(qr, nested_name_specifier(n[2][0], qscope, false));
        require_complete(qscope.entity);
        if (qscope.global || qscope.dependent || !qscope.entity ||
            entity_table[qscope.entity].kind != entity_kind::class_type ||
            !entity_table[qscope.entity].size)
            return not_supported_because(n, "expr.ref",
                                         "a qualified pseudo-member name whose "
                                         "scope is not a complete class");
        qual_class = qscope.entity;
        SyntaxElement rhs = n[2].back();
        if (rhs.is_identifier()) {
            name = rhs.token();
        } else if (SyntaxElement t = rhs.simple_template_id().template_name()) {
            name = t.token();
        } else if (auto function = rhs.operator_function_id()) {
            declarator_meaning function_name;
            VTRY(r, operator_function_id(function, function_name));
            name = function_name.id;
        } else {
            return not_supported_because(
                n, "expr.ref",
                "a template keyword member name of an unhandled form");
        }
    } else if (SyntaxElement id = member_template_id(n); id.is(
                   syntax_kind::simple_template_id, syntax_kind::template_id)) {
        /* [temp.names]/4: x.f<T>, x.template f<T>, x.operator()<T> */
        if (id[0].is_identifier()) {
            name = id[0].token();
        } else if (auto function = id[0].operator_function_id()) {
            declarator_meaning function_name;
            VTRY(r, operator_function_id(function, function_name));
            name = function_name.id;
        }
    } else if (auto function = n[2].operator_function_id()) {
        /* [over.oper]/1: x.operator->() names the member operator
           function: the keyword's token takes the operator's name */
        declarator_meaning function_name;
        VTRY(r, operator_function_id(function, function_name));
        name = function_name.id;
    } else if (destructor_id(n[2])) {
        /* [expr.prim.id.dtor]/2: a destructor's name can be the callee of
           a call only (a parenthesized one is not analysed) */
        return not_supported_because(
            n, "expr.prim.id.dtor",
            "a destructor name in a member access that is not called");
    } else if (n[2].is(syntax_kind::conversion_function_id)) {
        /* [class.conv.fct]/1, [temp.mem]/5: explicit call to a conversion
           function or conversion function template (a.operator T()) */
        VTRY(r, conversion_type(n[2], conversion_target));
        conversion_id = true;
    } else {
        /* template, a qualified-id, a conversion function's or a literal
           operator's */
        return not_supported_because(n, "expr.ref",
                                     "a member name of an unhandled form");
    }
    /* (the call's callee name: member_object leaves it as it was) */
    token_index callee = overloaded_callee;
    VTRY(r, member_object(n, arrow, false, m));
    const struct type object = type_table[m.type.unqualified()];
    if (object.kind != type_kind::class_type) {
        if (fundamental_of(m.type) == fundamental_type::none &&
            object.kind != type_kind::pointer &&
            object.kind != type_kind::array &&
            object.kind != type_kind::enumeration)
            return not_supported_because(
                n, "expr.ref",
                "member access on an operand of an unhandled type");
        return ill_formed(n,
                          "the object expression of a class member access "
                          "shall be of class type",
                          "expr.ref");
    }
    entity_index c = object.entity;
    /* [temp.inst]/2: a specialization named only is defined here; one
       that cannot be is no class */
    require_complete(c);
    if (entity_table[c].kind != entity_kind::class_type)
        /* a class whose definition was not analysed */
        return not_supported_because(
            n, "class.pre",
            "member access on a class type whose entity is not a class");
    /* a specialization whose definition could not be analysed is no
       incomplete class */
    if (!entity_table[c].size && entity_table[c].instantiation &&
        instantiations[entity_table[c].instantiation - 1].has_failure)
        return not_supported_because(
            n, "temp.inst",
            "member access on a class whose instantiation failed");
    if (!entity_table[c].size && entity_table[c].defined)
        /* [expr.ref]/4: in the definition of the class, a complete-class
           context ([class.mem]/6) whose layout is not known yet */
        return not_supported_because(
            n, "expr.ref",
            "a member access of the class being defined before its layout");
    if (!entity_table[c].size)
        return ill_formed(n,
                          "the class type shall be complete unless the class "
                          "member access appears in the definition of that "
                          "class",
                          "expr.ref");
    if (conversion_id) {
        bool ambiguous = false, unknown = false;
        entity_index f = conversion_function_for(m, conversion_target, true,
                                                 ambiguous, unknown);
        if (unknown || ambiguous || !f ||
            type_table[entity_table[f].type.unqualified()].operand !=
                conversion_target)
            return not_supported_because(n, "expr.ref",
                                         "a conversion function name with no "
                                         "single exactly matching function");
        VTRY_SET(r, need_specialization_definition(n, f));
        return member_access(n[2].first_token(), f, m);
    }
    entity_index lookup_class = c;
    if (qual_class) {
        if (qual_class != c) {
            VTRY(br, to_base(name, qual_class, m));
        }
        lookup_class = qual_class;
    }
    bool ambiguous = false;
    entity_index e =
        class_member_lookup(lookup_class, tokens.identifier(name), ambiguous);
    if (ambiguous)
        return not_supported_because(n, "class.member.lookup",
                                     "an ambiguous member name");
    if (e && (entity_table[e].kind == entity_kind::none ||
              entity_table[e].member_templates)) {
        if (uint32_t vt = variable_template_of(e)) {
            SyntaxElement id = member_template_id(n);
            if (id.is(syntax_kind::simple_template_id,
                      syntax_kind::template_id))
                return variable_template_id_of(id.index(), vt, m);
        }
        /* [temp.mem]/1: member function templates, called: the call
           deduces their arguments (member_template_call), with the
           functions of the name ([temp.mem]/2) */
        if (name == callee && member_templates_of(e)) {
            member_template_name = e;
            member_template_object = m;
            member_template_written.clear();
            member_template_written_at = 0;
            SyntaxElement id = member_template_id(n);
            if (id.is(syntax_kind::simple_template_id,
                      syntax_kind::template_id)) {
                owned_array<bool> expansion;
                VTRY(w, written_arguments_of(id, member_template_written,
                                             expansion));
                for (bool x : expansion)
                    if (x)
                        return not_supported_because(
                            id.index(), "temp.variadic",
                            "a pack expansion in a member template's explicit "
                            "arguments");
                member_template_written_at = id.index();
            }
            return analysis_result::well_formed;
        }
        return not_supported_because(n, "over",
                                     "a member name that lookup does not find");
    }
    if (member_template_id(n).is(syntax_kind::simple_template_id,
                                 syntax_kind::template_id))
        /* a template-id of no member function template analysis knows */
        return not_supported_because(
            n, "temp.names",
            "a template-id naming a member that is not a member template");
    if (!e)
        return ill_formed_at_token(name,
                                   "the id-expression shall name a member "
                                   "of the class",
                                   "expr.ref");
    return member_access(name, e, m);
}

} // namespace cplusplus
