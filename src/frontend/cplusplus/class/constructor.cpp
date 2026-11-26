/*
 * constructor.cpp: [class.ctor] Constructors and [class.dtor] Destructors:
 * their declarations in the member-specification, the constructor that
 * initializes an object ([over.match.ctor]), and a constructor's member
 * initializations ([class.base.init]).
 *
 * [class.ctor]/1: "A constructor is introduced by a declaration whose
 * declarator is a function declarator ([dcl.fct]) of the form
 *     ptr-declarator ( parameter-declaration-clause ) noexcept-specifier_opt attribute-specifier-seq_opt
 * where the ptr-declarator consists solely of an id-expression, an optional
 * attribute-specifier-seq, and optional surrounding parentheses, and the
 * id-expression has one of the following forms: (1.1) in a
 * member-declaration that belongs to the member-specification of a class or
 * class template but is not a friend declaration, the id-expression is the
 * injected-class-name of the immediately-enclosing entity or ..."
 *
 * [class.dtor]/1: "A prospective destructor is introduced by a declaration
 * whose declarator is a function declarator of the form
 *     ptr-declarator ( parameter-declaration-clause ) noexcept-specifier_opt attribute-specifier-seq_opt
 * where the ptr-declarator consists solely of an id-expression, an optional
 * attribute-specifier-seq, and optional surrounding parentheses, and the
 * id-expression has one of the following forms: (1.1) in a
 * member-declaration that belongs to the member-specification of a class or
 * class template but is not a friend declaration, the id-expression is
 * ~class-name and the class-name is the injected-class-name of the
 * immediately-enclosing entity".
 *
 * What analysis has: user-declared constructors and a destructor of a class
 * whose non-virtual bases and members they construct and destroy
 * ([class.base.init], [class.dtor]/13), explicit ones among them (not
 * defaulted); no defaulted, deleted, constexpr or virtual ones, no default
 * arguments, no delegating constructor.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* whether d, the declarator of a member-declaration without a
   decl-specifier-seq, is class-name ( ... ) or ~class-name ( ... ) */
bool semantic_analysis::declares_special_member(entity_index c,
                                                SyntaxElement d) const
{
    /* (the declarator-id and its parameters-and-qualifiers alone: no
       attributes, trailing-return-type or other wrapper) */
    DeclaratorParts parts(d);
    if (!d.is_node() || parts.noptr != d.index() || d.size() != 2 ||
        !parts.parameters())
        return false;
    SyntaxElement head = parts.unqualified();
    if (head.is(syntax_kind::unqualified_id)) {
        if (head.size() != 2 || !head[0].is(preprocessing_op_or_punc::TILDE))
            return false;
        head = head[1];
    }
    if (!head.is_token() || tokens[head.token()].kind != token_kind::identifier)
        return false;
    return const_cast<token_stream &>(tokens).identifier(head.token()) ==
           entity_table[c].name;
}

/*
 * [temp.mem]/1: "A template can be declared within a class or class
 * template; such a template is called a member template."
 * [class.copy.ctor]/5: "A member function template is never instantiated to
 * produce such a constructor signature": a constructor template is never a
 * copy or move constructor; it is a
 * user-declared constructor ([dcl.init.aggr]/1.1, [class.default.ctor]/1:
 * no default constructor is implicitly declared).  Only a constructor
 * template of a class of namespace scope is known.  One defined in the
 * class is member_function_template's, as any other member function
 * template defined there; one only declared is not analysed beyond its
 * form, and a construction of the class, which would consider it
 * ([over.match.ctor]), is not analysed either (construct).
 */
analysis_result semantic_analysis::member_template_declaration(
    entity_index c, TemplateDeclaration n, uint8_t access)
{
    if (n.size() != 2 || !n.template_head() || n[1].is_token())
        return not_supported_because(n, "temp.mem",
                                     "a member template declaration without a "
                                     "template-head and a declaration");
    owned_array<SyntaxRef> parameters;
    if (!template_head_list(n.template_head(), parameters) ||
        parameters.empty())
        return not_supported_because(
            n, "temp.mem",
            "a member template with no analysable template-parameter-list");
    auto inner = n[1];
    /* [temp.friend]/1: a friend template */
    if (inner.is(syntax_kind::member_declaration,
                 syntax_kind::function_definition)) {
        uint32_t lead = leading_gnu_attributes(inner);
        if (lead < inner.size() &&
            inner[lead].is(syntax_kind::decl_specifier_seq))
            for (auto x : inner[lead].elements())
                if (x.is(standard_keyword::friend_))
                    return friend_function_template(c, n, inner);
    }
    /* [temp.mem]/2: a local class shall not have member templates; a nested
       class of a namespace-scope class may */
    entity_index top = entity_table[c].enclosing;
    while (top && entity_table[top].kind == entity_kind::class_type)
        top = entity_table[top].enclosing;
    /* [temp.alias]/1: a member alias template, of a class of namespace
       scope: an alias template declared in the class's scope */
    if (inner.is(syntax_kind::alias_declaration) && is_namespace(top)) {
        entity_index saved = current_scope;
        current_scope = c;
        analysis_result r = alias_template_definition(n, inner);
        current_scope = saved;
        return r;
    }
    /* [temp.class]/1: a member class template (template<...> struct X
       { ... };), a class template declared in the class's scope */
    if (inner.is(syntax_kind::member_declaration)) {
        SyntaxRef only = sole_decl_specifier(inner);
        if (only && only.is(syntax_kind::class_specifier)) {
            entity_index saved = current_scope, saved_class = defining_class;
            current_scope = c;
            defining_class = 0;
            analysis_result r = class_template_definition(n, only);
            current_scope = saved;
            defining_class = saved_class;
            return r;
        }
    }
    /* [temp.deduct.guide]: a deduction guide for a member class template */
    if (inner.is(syntax_kind::deduction_guide) && is_namespace(top)) {
        entity_index saved = current_scope;
        entity_index saved_class = defining_class;
        current_scope = c;
        defining_class = 0;
        analysis_result r =
            deduction_guide(n.template_declaration(), inner.deduction_guide());
        defining_class = saved_class;
        current_scope = saved;
        return r;
    }
    if ((!inner.is(syntax_kind::function_definition,
                   syntax_kind::member_declaration)) ||
        !is_namespace(top))
        return not_supported_because(
            n, "temp.mem",
            "a member template that is no function or member declaration, or "
            "not in a class within a namespace");
    /* [ext:gnu_attributes] before it all; [class.ctor]/1: "Each
       decl-specifier in the decl-specifier-seq of a constructor
       declaration (if any) shall be friend, inline, constexpr, or an
       explicit-specifier" (and consteval, [dcl.consteval]) */
    uint32_t at = leading_gnu_attributes(inner);
    if (inner.is(syntax_kind::function_definition) && at < inner.size() &&
        !declares_special_member(c, inner[at]) &&
        !(at + 1 < inner.size() &&
          inner[at].is(syntax_kind::decl_specifier_seq) &&
          declares_special_member(c, inner[at + 1])))
        return member_function_template(c, n, inner, access);
    /* [temp.mem]/1: a static data member variable template (defined in-class
       or declared only) */
    if (inner.is(syntax_kind::member_declaration) && at + 1 < inner.size() &&
        inner[at].is(syntax_kind::decl_specifier_seq)) {
        SyntaxElement item = inner[at + 1];
        if (item.is(syntax_kind::member_declarator_list) && item.size() == 1)
            item = item[0];
        bool initialized_var =
            item.is(syntax_kind::member_declarator) && item.size() == 2 &&
            item[1].is(syntax_kind::brace_or_equal_initializer);
        bool declared_var = false;
        if (!initialized_var && item.is_token() && item.is_identifier()) {
            for (auto sp : inner[at].elements())
                if (sp.is(standard_keyword::static_))
                    declared_var = true;
        }
        if (initialized_var || declared_var) {
            entity_index saved = current_scope;
            current_scope = c;
            analysis_result r = variable_template_definition(n, inner);
            current_scope = saved;
            return r;
        }
    }
    /* [temp.mem]/1: a member class template only declared (defined outside
       the class) */
    if (inner.is(syntax_kind::member_declaration)) {
        SyntaxRef only = sole_decl_specifier(inner);
        if (only && only.is(syntax_kind::elaborated_type_specifier)) {
            entity_index saved = current_scope;
            current_scope = c;
            analysis_result r = class_template_declaration(n, only);
            current_scope = saved;
            return r;
        }
    }
    /* [temp.mem]/1: a member function template only declared (defined
       outside the class) */
    if (inner.is(syntax_kind::member_declaration) && at + 1 < inner.size() &&
        inner[at].is(syntax_kind::decl_specifier_seq) &&
        !declares_special_member(c, inner[at + 1]))
        return member_function_template(c, n, inner, access);
    /* a constrained constructor template only declared ([temp.constr.decl]);
       a defined one is a member_function_template, whose deduction checks
       the constraints ([temp.deduct]/5) */
    if (head_constraint(n) && !inner.is(syntax_kind::function_definition))
        return not_supported_because(n, "temp.constr",
                                     "a constrained constructor or conversion "
                                     "template that is only declared");
    if (at < inner.size() && inner[at].is(syntax_kind::decl_specifier_seq)) {
        /* (the template-parameters are not in scope here: a condition is
           analysed with the template's declarator, member_function_template;
           a template only declared records no explicit-ness, as with the
           keyword) */
        specified s;
        VTRY(r, constructor_specifiers(inner[at], s, false));
        at++;
    }
    /* a constructor's or conversion function's declarator, not a destructor's
       (which [temp.mem]/3 does not allow a template of) */
    if (at >= inner.size() ||
        (!declares_special_member(c, inner[at]) &&
         !conversion_function_id_of(inner)) ||
        inner[at][0].is(syntax_kind::unqualified_id))
        return not_supported_because(
            n, "temp.mem",
            "a member template declaration with no recognised constructor or "
            "conversion declarator");
    return member_function_template(c, n, inner, access);
}

/*
 * A constructor template's decl-specifier-seq ([class.ctor]/1): constexpr,
 * consteval (as constexpr: [expr.const]/13's immediate invocations are not
 * told apart), inline, explicit and GNU attributes; any other is not
 * analysed.
 */
analysis_result semantic_analysis::constructor_specifiers(SyntaxRef seq,
                                                          specified &s,
                                                          bool conditions)
{
    for (SyntaxElement x : seq.elements()) {
        if (x.is(syntax_kind::gnu_attribute_specifier)) {
            VTRY(r, gnu_attribute_specifier(x, s));
            continue;
        }
        /* [dcl.fct.spec]/2: explicit ( constant-expression ), a
           constructor template's too ([temp.mem]/1): "explicit(true)" is
           explicit; a value-dependent condition is decided where the
           specialization is declared */
        if (x.is(syntax_kind::explicit_specifier)) {
            if (!conditions)
                continue;
            if (names_dependent_entity(x.first_token(), x.end_token())) {
                s.explicit_dependent = true;
                continue;
            }
            bool constant = false, value = false;
            invalid_specialization_called = false;
            VTRY(r, bool_constant_expression(x, x[2], constant, value));
            /* (an error in a function's instantiation is a hard error, not
               a candidate dropped: not told apart here) */
            if (!constant && invalid_specialization_called)
                return not_supported_because(x, "temp.deduct",
                                             "an explicit-specifier that calls "
                                             "an invalid specialization");
            if (!constant)
                return ill_formed(x,
                                  "the constant-expression of an "
                                  "explicit-specifier shall be a "
                                  "contextually converted constant "
                                  "expression of type bool",
                                  "dcl.fct.spec");
            s.explicit_ = value;
            continue;
        }
        if (!x.is_token())
            return not_supported_because(
                seq, "temp.mem",
                "a constructor template specifier that is no keyword");
        switch (x.keyword()) {
        case standard_keyword::constexpr_:
        case standard_keyword::consteval_:
            s.constexpr_ = true;
            continue;
        case standard_keyword::inline_:
            s.inline_ = true;
            continue;
        case standard_keyword::explicit_:
            s.explicit_ = true;
            continue;
        default:
            return not_supported_because(
                seq, "temp.mem",
                "a constructor template specifier other than constexpr, inline "
                "or explicit");
        }
    }
    return analysis_result::well_formed;
}

/*
 * A constructor template's declarator d, of definition: its type a
 * function returning void, as a constructor's (special_member_declarator).
 */
analysis_result semantic_analysis::constructor_template_declarator(
    SyntaxRef definition, SyntaxElement d, qualified_type &type,
    declarator_meaning &m)
{
    type = qualified_type::make(type_table.fundamental(fundamental_type::void_),
                                cv_none);
    scoped_value<bool> allowed(enable_if_allowed, true);
    return declarator(definition, d, type, m);
}

/*
 * Whether the initializer of a member of class c (with constructors) is
 * not a constructor call on the member but the value of an aggregate
 * ([dcl.init.aggr], [dcl.init.list]/3.4: `{}` or `{...}`), which the
 * lowering builds and stores: right only when copying the value is
 * the same as initializing the member in place, c's copy and destructor
 * trivial ([class.copy.ctor]/11, [class.dtor]/8).
 */
bool semantic_analysis::aggregate_value_stored(entity_index c,
                                               uint32_t value) const
{
    const semantic_node &n = semantics[value];
    if (n.kind != semantic_kind::initializer_list &&
        n.kind != semantic_kind::zero) {
        /* [dcl.init]/17.6.1: a prvalue of the class itself (a call's
           result) initializes the member as the object; a trivial class's
           copy of it is the same */
        if (n.category != value_category::prvalue ||
            n.type.unqualified() != entity_table[c].type.unqualified())
            return false;
    }
    return trivial_class(c);
}

analysis_result semantic_analysis::anonymous_member_initializers(
    token_index at, entity_index anonymous, uint32_t &statement)
{
    owned_array<uint32_t> statements;
    uint32_t first = entity_table[anonymous].first_member;
    uint32_t count = entity_table[anonymous].member_count;
    for (uint32_t i = 0; i < count; i++) {
        entity_index m = class_members[first + i];
        if (entity_table[m].kind != entity_kind::variable ||
            !entity_table[m].nonstatic_member)
            continue;
        const struct type record =
            type_table[entity_table[m].type.unqualified()];
        entity_index inner = 0;
        if (record.kind == type_kind::class_type &&
            entity_table[record.entity].anonymous_object == m)
            inner = record.entity;
        uint32_t made = 0;
        if (entity_table[m].default_initializer) {
            SyntaxRef init = entity_table[m].default_initializer;
            if (!entity_table[anonymous].union_)
                for (token_index t = init.first_token(); t < init.end_token();
                     t++)
                    /* [expr.prim.this]: the address of the enclosing
                       object in a constant expression is not evaluated
                       for an unnamed struct's member yet */
                    if (syntax_parser.keyword_of(tokens[t]) ==
                        standard_keyword::this_)
                        return not_supported_because(
                            current_declaration, "expr.prim.this",
                            "this in a default member initializer of a member "
                            "of an anonymous struct");
            VTRY(r, default_member_initializer(at, m, made));
        } else if (inner) {
            VTRY(r, anonymous_member_initializers(at, inner, made));
        } else if (record.kind == type_kind::class_type &&
                   entity_table[record.entity].first_constructor &&
                   !trivial_defaulted_default_constructor(record.entity) &&
                   !entity_table[anonymous].union_) {
            /* [class.base.init]/9.3: default-initialized, constructed in
               place as an ordinary member is (clang's unnamed struct) */
            expression_meaning object;
            VTRY(r, subobject(at, m, object));
            owned_array<expression_meaning> none;
            initialized_value value;
            VTRY(r2, construct(current_declaration, entity_table[m].type, none,
                               object.node, value));
            made = statement_node(semantic_kind::expression_statement, at,
                                  value.node);
        }
        if (made)
            statements.append(made);
    }
    statement = block_statement(at, statements);
    return analysis_result::well_formed;
}

/*
 * [class.base.init]/9.1: member e initialized from its default member
 * initializer ([class.mem]/10), analysed in the constructor ([class.mem]/6:
 * a complete-class context, `this` its object), and stored as by an
 * assignment to (*this).e.  A member of class type with constructors is
 * constructed in place, or, when its initializer is an aggregate's value
 * (aggregate_value_stored), stored; an array has no rules here yet.
 */
analysis_result
semantic_analysis::default_member_initializer(token_index at, entity_index e,
                                              uint32_t &statement)
{
    qualified_type type = entity_table[e].type;
    /* a copy: the initializer below can grow type_table */
    const struct type record = type_table[type.unqualified()];
    SyntaxRef init = entity_table[e].default_initializer;
    /* before the analysis, which may move the tables */
    bool constructed = record.kind == type_kind::class_type &&
                       entity_table[record.entity].first_constructor;
    expression_meaning object;
    VTRY(r, subobject(at, e, object));
    initialized_value value;
    /* [ext:clang_source_location] a builtin in it is the constructor's */
    struct bound_at {
        token_index &place;
        token_index before;
        ~bound_at()
        {
            place = before;
        }
    } bound = {source_location_at, source_location_at};
    source_location_at = at;
    entity_index outer_member = initialized_member;
    initialized_member = e;
    uint32_t locations_before = source_location_uses;
    /* [class.mem]/6: a default member initializer needed while it is
       being analysed (clang: "initializer for 'n' needed") */
    if (entity_table[e].initializer_active)
        return ill_formed(init,
                          "the default member initializer is needed "
                          "within itself",
                          "class.mem");
    entity_table[e].initializer_active = true;
    r = initializer(init, type, value);
    entity_table[e].initializer_active = false;
    initialized_member = outer_member;
    if (r == analysis_result::ill_formed)
        member_initializer_failed = true;
    if (r != analysis_result::well_formed)
        return r;
    /* [class.base.init]/11: "A temporary expression bound to a reference
       member from a default member initializer is ill-formed" */
    if ((record.kind == type_kind::lvalue_reference ||
         record.kind == type_kind::rvalue_reference) &&
        semantics[value.node].kind == semantic_kind::bind_reference &&
        semantics[value.node].category == value_category::prvalue) {
        /* [impl.clang] clang reports it where a constructor using the initializer is
           defined, and none is when the class is only declared: the
           implicit definition's is not reported here */
        if (defining_defaulted_member)
            return not_supported_because(
                init, "class.base.init",
                "a temporary bound to a reference member in a defaulted "
                "constructor that may be unused");
        return ill_formed(init,
                          "a temporary expression bound to a reference "
                          "member from a default member initializer is "
                          "ill-formed",
                          "class.base.init");
    }
    /* [ext:clang_source_location] a builtin in the initializer is the
       constructor's place: a defaulted one is defined where it is first
       used or at the end of its class, whose place is not the
       declaration's that clang answers */
    if (defining_defaulted_member && source_location_uses != locations_before)
        entity_table[e].initializer_located = true;
    /* [dcl.init]/17.6.1: a prvalue of the class that is no constructor
       call (a function call's result) initializes the member itself,
       whatever the class's constructors do, as in a mem-initializer */
    if (constructed && !aggregate_value_stored(record.entity, value.node) &&
        semantics[value.node].kind == semantic_kind::call &&
        semantics[value.node].category == value_category::prvalue &&
        semantics[value.node].type.unqualified() == type.unqualified()) {
        statement = subobject_store(object, type, at, value.node, true);
        return analysis_result::well_formed;
    }
    if (constructed && !aggregate_value_stored(record.entity, value.node) &&
        (semantics[value.node].kind != semantic_kind::construct ||
         semantics[value.node].a))
        /* [dcl.init]/17.6.1: a prvalue of the class, or no constructor
           call for the object: not analysed */
        return not_supported_because(
            init, "class.base.init",
            "a default member initializer of class type with no stored value "
            "or constructor call");
    /* [except.spec]/7: the initializer's own exceptions, which a
       constructor's exception specification when implicit is made of */
    bool throwing = false;
    size_t unsupported_before = unsupported.size();
    if (potentially_throwing(init, value.node, throwing) ==
        analysis_result::well_formed) {
        entity_table[e].initializer_throws =
            initializer_exception::cannot_throw;
        if (throwing)
            entity_table[e].initializer_throws =
                initializer_exception::may_throw;
    }
    unsupported.resize(unsupported_before);
    if (constructed && !aggregate_value_stored(record.entity, value.node)) {
        /* [class.base.init]/9.1: the member's constructor, called on the
           member itself (the object of the construct node: 0 until now,
           as for a declared variable) */
        semantics[value.node].a = object.node;
        statement =
            statement_node(semantic_kind::expression_statement, at, value.node);
        return analysis_result::well_formed;
    }
    statement = subobject_store(object, type, at, value.node);
    return analysis_result::well_formed;
}

/* whether constructor k of class c is a copy constructor X(cv X&)
   ([class.copy.ctor]/1) or a move constructor X(cv X&&) (/2), without
   default arguments */
bool semantic_analysis::copy_or_move_constructor(entity_index c,
                                                 entity_index k) const
{
    const struct type &f = type_table[entity_table[k].type.unqualified()];
    if (f.parameter_count != 1)
        return false;
    const struct type &p =
        type_table[type_table.parameters(entity_table[k].type.unqualified())[0]
                       .unqualified()];
    return (p.kind == type_kind::lvalue_reference ||
            p.kind == type_kind::rvalue_reference) &&
           p.operand.unqualified() == entity_table[c].type.unqualified();
}

/* whether class c declares a move constructor X(cv X&&)
   ([class.copy.ctor]/3, without default arguments) */
bool semantic_analysis::declares_move_constructor(entity_index c) const
{
    for (entity_index k = entity_table[c].first_constructor; k;
         k = entity_table[k].next_constructor) {
        if (!copy_or_move_constructor(c, k))
            continue;
        qualified_type p =
            type_table.parameters(entity_table[k].type.unqualified())[0];
        if (type_table[p.unqualified()].kind == type_kind::rvalue_reference)
            return true;
    }
    return false;
}

bool semantic_analysis::declares_copy_constructor(entity_index c) const
{
    for (entity_index k = entity_table[c].first_constructor; k;
         k = entity_table[k].next_constructor) {
        if (entity_table[k].implicit || !copy_or_move_constructor(c, k))
            continue;
        qualified_type p =
            type_table.parameters(entity_table[k].type.unqualified())[0];
        if (type_table[p.unqualified()].kind == type_kind::lvalue_reference)
            return true;
    }
    return false;
}

/*
 * Whether a decl-specifier-seq has only function-specifiers analysis has
 * rules for on a constructor or destructor: constexpr ([class.ctor]/1,
 * [class.dtor]/1) and, in a class (virtual_specified), virtual
 * ([class.dtor]/1, [dcl.fct.spec]/2; on a constructor it is ill-formed,
 * left to special_member_declarator).
 */
bool semantic_analysis::function_specifiers_only(SyntaxRef seq,
                                                 bool &constexpr_specified,
                                                 bool *virtual_specified,
                                                 bool *explicit_specified,
                                                 bool *inline_specified)
{
    constexpr_specified = false;
    bool virtual_seen = false, explicit_seen = false, inline_seen = false;
    if (inline_specified)
        *inline_specified = false;
    if (explicit_specified)
        *explicit_specified = false;
    if (virtual_specified)
        *virtual_specified = false;
    for (SyntaxElement c : seq.elements()) {
        /* [ext:gnu_attributes] between the specifiers (libc++'s inline
           _LIBCPP_HIDE_FROM_ABI explicit X(...)): special_member_declarator
           applies them */
        if (c.is(syntax_kind::gnu_attribute_specifier))
            continue;
        if (c.is(syntax_kind::explicit_specifier)) {
            if (!explicit_specified || explicit_seen ||
                names_dependent_entity(c.first_token(), c.end_token()))
                return false;
            bool constant = false, value = false;
            size_t unsupported_before = unsupported.size();
            analysis_result r =
                bool_constant_expression(c, c[2], constant, value);
            unsupported.resize(unsupported_before);
            if (r != analysis_result::well_formed || !constant)
                return false;
            explicit_seen = true;
            *explicit_specified = value;
            continue;
        }
        if (!c.is_token())
            return false;
        switch (c.keyword()) {
        case standard_keyword::constexpr_:
            if (constexpr_specified)
                return false;
            constexpr_specified = true;
            break;
        case standard_keyword::virtual_:
            if (!virtual_specified || virtual_seen)
                return false;
            virtual_seen = true;
            *virtual_specified = true;
            break;
        case standard_keyword::explicit_:
            /* [class.ctor]/1: "an explicit-specifier" (the keyword alone;
               explicit ( constant-expression ) is a node) */
            if (!explicit_specified || explicit_seen)
                return false;
            explicit_seen = true;
            *explicit_specified = true;
            break;
        case standard_keyword::inline_:
            /* [class.ctor]/1: inline, where the caller takes it */
            if (!inline_specified || inline_seen)
                return false;
            inline_seen = true;
            *inline_specified = true;
            break;
        default:
            return false;
        }
    }
    return constexpr_specified || virtual_seen || explicit_seen || inline_seen;
}

/*
 * A constructor or destructor declared in the member-specification of c.
 * [class.ctor]/3: "A constructor shall not be a coroutine.  A constructor
 * shall not have an explicit object parameter."; /4 "A return type shall
 * not be specified"; [class.dtor]/2: "A prospective destructor shall take
 * no arguments"; [class.dtor]/1: "Each decl-specifier of the
 * decl-specifier-seq of a prospective destructor declaration (if any) shall
 * be friend, inline, virtual, constexpr, or consteval."
 */
analysis_result semantic_analysis::special_member_declarator(
    entity_index c, SyntaxRef declaration, SyntaxElement d, uint8_t access,
    bool definition, bool constexpr_specified, bool virtual_specified,
    bool explicit_specified, bool inline_specified)
{
    qualified_type type = qualified_type::make(
        type_table.fundamental(fundamental_type::void_), cv_none);
    declarator_meaning m;
    function_cv = 0;
    in_member_declarator = true;
    function_parameters.clear();
    analysis_result r = declarator(declaration, d, type, m);
    in_member_declarator = false;
    if (r != analysis_result::well_formed)
        return r;
    /* [ext:gnu_attributes] before the declaration (libc++'s
       _LIBCPP_HIDE_FROM_ABI) */
    for (uint32_t i = 0; i < leading_gnu_attributes(declaration); i++) {
        VTRY_SET(r, gnu_attribute_specifier(declaration[i], m));
    }
    /* ... and between the function-specifiers */
    uint32_t lead = leading_gnu_attributes(declaration);
    if (declaration.size() > lead &&
        declaration[lead].is(syntax_kind::decl_specifier_seq)) {
        for (SyntaxElement x : declaration[lead].elements()) {
            if (x.is(syntax_kind::gnu_attribute_specifier)) {
                VTRY_SET(r, gnu_attribute_specifier(x, m));
            }
        }
    }
    /* [dcl.fct.default]: a member function's default arguments are a
       complete-class context ([class.mem]/6): analysed with the class
       complete (member_default_arguments) */
    owned_array<parameter_meaning> listed;
    bool has_defaults = false;
    for (const parameter_meaning &p : function_parameters) {
        listed.append(p);
        if (p.default_argument)
            has_defaults = true;
    }
    if (!m.has_id || m.qualifier || m.aligned ||
        (m.gnu_flags & (gnu_packed | gnu_used)))
        return not_supported_because(
            declaration, "class.ctor",
            "a constructor or destructor with a qualified or missing name, "
            "alignment, packed or used");
    if (function_cv)
        /* [class.ctor]/5: "A constructor shall not be declared const,
           volatile, or const volatile"; [class.dtor]/3 likewise */
        return ill_formed_at_token(m.id,
                                   "a constructor or destructor shall not be "
                                   "declared const, volatile, or const "
                                   "volatile",
                                   "class.ctor");
    const struct type &f = type_table[type.unqualified()];
    if (f.variadic)
        return not_supported_because(declaration, "dcl.fct",
                                     "a variadic constructor or destructor");
    const qualified_type *parameters =
        type_table.parameters(type.unqualified());
    special_member kind = special_member::constructor;
    if (m.destructor && constexpr_specified &&
        !tokens.files.behavior.has(language_feature::constexpr_destructors))
        return ill_formed(declaration, "a constexpr destructor requires C++20",
                          "dcl.constexpr");
    if (explicit_specified && m.destructor)
        /* [dcl.fct.spec]/3: "An explicit-specifier shall be used only in
           the declaration of a constructor or conversion function within
           its class definition" (clang's error) */
        return not_supported_because(declaration, "dcl.fct.spec",
                                     "an explicit destructor");
    if (virtual_specified && !m.destructor)
        /* [class.ctor]/1 lists no virtual: clang's "constructor cannot be
           declared 'virtual'" */
        return not_supported_because(declaration, "class.ctor",
                                     "a virtual constructor");
    if (m.destructor) {
        kind = special_member::destructor;
        if (f.parameter_count)
            return ill_formed_at_token(m.id,
                                       "a prospective destructor shall take "
                                       "no arguments",
                                       "class.dtor");
        if (entity_table[c].destructor)
            return ill_formed_at_token(m.id,
                                       "a member shall not be declared twice "
                                       "in the member-specification",
                                       "class.mem");
    } else {
        if (has_defaults && f.parameter_count > 1 &&
            listed.size() == f.parameter_count && listed[1].default_argument) {
            /* [class.copy.ctor]/1, /5: a first parameter of X, X& or
               X&& and the other parameters' default arguments: a copy or
               move constructor, or ill-formed (X), not told apart */
            qualified_type first = parameters[0];
            const struct type &r4 = type_table[first.unqualified()];
            if (r4.kind == type_kind::lvalue_reference ||
                r4.kind == type_kind::rvalue_reference)
                first = r4.operand;
            if (first.unqualified() == entity_table[c].type.unqualified())
                return not_supported_because(
                    declaration, "class.copy.ctor",
                    "a constructor whose first parameter is its own class and "
                    "the next has a default argument");
        }
        for (uint32_t i = 0; i < f.parameter_count; i++) {
            /* [class.copy.ctor]/5: "A declaration of a constructor for a
               class X is ill-formed if its first parameter is of type cv X
               and either there are no other parameters or else all other
               parameters have default arguments." */
            if (i == 0 && f.parameter_count == 1 &&
                parameters[0].unqualified() ==
                    entity_table[c].type.unqualified())
                return ill_formed_at_token(m.id,
                                           "a constructor for a class X is "
                                           "ill-formed if its only parameter "
                                           "is of type cv X",
                                           "class.copy.ctor");
        }
        for (entity_index k = entity_table[c].first_constructor; k;
             k = entity_table[k].next_constructor)
            if (entity_table[k].type == type)
                /* [class.mem]/5, [over.load] */
                return ill_formed_at_token(m.id,
                                           "a member shall not be declared "
                                           "twice in the member-specification",
                                           "class.mem");
    }
    /* [dcl.constexpr]/3: "if the function is a constructor or destructor,
       its class shall not have any virtual base classes": a constructor
       defined with a body (clang's "constexpr constructor not allowed in
       struct with virtual base class"); one only declared, defaulted
       (clang rejects it too, with another reason) or deleted ([ext.clang] clang
       accepts it) is not analysed */
    if (constexpr_specified && entity_table[c].virtual_base_count &&
        definition && !m.destructor)
        return ill_formed_at_token(m.id,
                                   "the class of a constexpr constructor "
                                   "shall not have any virtual base classes",
                                   "dcl.constexpr");
    if (constexpr_specified && entity_table[c].virtual_base_count)
        return not_supported_because(declaration, "dcl.constexpr",
                                     "a constexpr constructor or destructor of "
                                     "a class with virtual bases");
    entity_index e =
        entity_table.make(entity_kind::function, entity_table[c].name, c);
    entity &x = entity_table[e];
    x.type = type;
    x.exceptions = resolved_exceptions(m.exceptions, type, m.gnu_flags);
    x.nonstatic_member = true;
    x.special = kind;
    /* [ext:gnu_attributes] its symbol's */
    x.gnu_flags = m.gnu_flags & gnu_function_symbol_flags;
    x.abi_tag = m.abi_tag;
    x.explicit_ = explicit_specified;
    /* [class.mfct]/1: defined in its class, an inline member function;
       [dcl.constexpr]/1: "A function or static data member declared with
       the constexpr or consteval specifier is implicitly an inline
       function or variable" */
    x.inline_ = definition || constexpr_specified || inline_specified;
    x.constexpr_ = constexpr_specified;
    x.defined = definition;
    x.access = access;
    x.linkage_of_name = entity_table[c].linkage_of_name;
    entity_table.add_declaration(e, declaration);
    if (has_defaults)
        defer_default_arguments(e, declaration, listed);
    diagnose_if_declared(declaration, e);
    if (kind == special_member::destructor) {
        entity_table[c].destructor = e;
    } else if (!entity_table[c].first_constructor) {
        entity_table[c].first_constructor = e;
    } else {
        entity_index k = entity_table[c].first_constructor;
        while (entity_table[k].next_constructor)
            k = entity_table[k].next_constructor;
        entity_table[k].next_constructor = e;
    }
    last_declared = e;
    if (kind == special_member::destructor) {
        /* [class.virtual]/7: a virtual destructor, declared so or
           overriding a base's ([class.dtor]/13) */
        /* virt_override, virt_final: the destructor's virt-specifier-seq
           (member_declaration) */
        virt_pure = false;
        analysis_result r3 =
            virtual_member(c, e, declaration, m.id, virtual_specified);
        virt_override = virt_final = false;
        return r3;
    }
    return analysis_result::well_formed;
}

/*
 * A constructor or destructor whose function-body is = default or
 * = delete.  [dcl.fct.def.delete]/1: a deleted definition; /2 a use of it
 * is ill-formed (a constructor's, a delete-expression's destructor).
 * [dcl.fct.def.default]/5: "Explicitly-defaulted functions and
 * implicitly-declared functions are collectively called defaulted
 * functions, and the implementation shall provide implicit definitions
 * for them ... A function is user-provided if it is user-declared and not
 * explicitly defaulted or deleted on its first declaration."  A defaulted
 * default constructor, copy constructor X(const X&) or destructor is
 * declared as the implicit one would be (implicit_special_members: no
 * entity when it is trivial); a public one only, without constexpr.
 */
analysis_result semantic_analysis::defaulted_or_deleted_special_member(
    entity_index c, SyntaxRef n, SyntaxElement d, SyntaxElement body,
    uint8_t access, bool constexpr_specified, bool virtual_specified,
    bool explicit_specified)
{
    auto form = body.function_body();
    if (form.deleted()) {
        if (explicit_specified)
            return not_supported_because(n, "dcl.fct.def.delete",
                                         "an explicit deleted special member");
        VTRY(r,
             special_member_declarator(c, n, d, access, false,
                                       constexpr_specified, virtual_specified));
        const entity &f = entity_table[last_declared];
        /* a deleted destructor (libc++'s __nat): the class's destructor,
           deleted, which a delete-expression checks (deleted_use); an
           object of the class is never destroyed in a well-formed
           program */
        if (f.special == special_member::destructor)
            return deleted_definition(n, last_declared);
        /* An X(X&) constructor changes implicit-move fallback on returns,
           which return-statement analysis does not yet model. */
        const struct type &ft = type_table[f.type.unqualified()];
        if (ft.parameter_count) {
            const struct type &first =
                type_table[type_table.parameters(f.type.unqualified())[0]
                               .unqualified()];
            if (first.kind == type_kind::lvalue_reference &&
                first.operand.unqualified() ==
                    entity_table[c].type.unqualified() &&
                first.operand.cv() != cv_const)
                return not_supported_because(
                    n, "class.copy.elision",
                    "a deleted constructor from a non-const lvalue reference "
                    "to its class");
        }
        return deleted_definition(n, last_declared);
    }
    if (!form.defaulted())
        return not_supported_because(
            n, "dcl.fct.def.general",
            "a special member body that is neither deleted nor defaulted");
    qualified_type type = qualified_type::make(
        type_table.fundamental(fundamental_type::void_), cv_none);
    declarator_meaning m;
    function_cv = 0;
    in_member_declarator = true;
    function_parameters.clear();
    analysis_result r = declarator(n, d, type, m);
    in_member_declarator = false;
    if (r != analysis_result::well_formed)
        return r;
    if (!m.has_id || m.qualifier || function_cv || m.aligned || m.gnu_flags)
        return not_supported_because(
            n, "dcl.fct.def.default",
            "a defaulted special member with a qualified or missing name, "
            "cv-qualifier, alignment or GNU attribute");
    for (const parameter_meaning &parameter : function_parameters)
        if (parameter.default_argument)
            return not_supported_because(
                n, "dcl.fct.def.default",
                "a defaulted special member with a default argument");
    const struct type &f = type_table[type.unqualified()];
    if (f.variadic)
        return not_supported_because(n, "dcl.fct.def.default",
                                     "a variadic defaulted special member");
    entity &x = entity_table[c];
    /* [dcl.fct.def.default]/3: constexpr only if constexpr-compatible.  The
       default, copy and move constructors of a class without virtual bases
       are ([class.default.ctor]/4, [class.copy.ctor]/13 with
       [dcl.constexpr]/4 as of C++20, which no longer asks for every member
       to be initialized); the other functions are not analysed */
    if (constexpr_specified && x.virtual_base_count)
        return not_supported_because(
            n, "dcl.fct.def.default",
            "a constexpr default in a class with virtual bases");
    if (m.destructor) {
        /* [class.dtor]/4: as the implicitly-declared destructor */
        if (x.destructor || f.parameter_count || explicit_specified)
            return not_supported_because(n, "dcl.fct.def.default",
                                         "a defaulted destructor declared "
                                         "again, with parameters or explicit");
        if (!virtual_specified)
            return analysis_result::well_formed;
        /* virtual ~X() = default: the implicit destructor's definition,
           virtual ([class.virtual]/7) */
        declare_implicit(c, special_member::destructor, false);
        entity_index e = entity_table[c].destructor;
        entity_table[e].access = access;
        if (constexpr_specified)
            entity_table[e].constexpr_ = true;
        entity_table.add_declaration(e, n);
        virt_override = virt_final = virt_pure = false;
        return virtual_member(c, e, n, m.id, true);
    }
    if (!f.parameter_count) {
        if (x.defaulted_default_constructor)
            return not_supported_because(
                n, "class.mem", "a second defaulted default constructor");
        x.defaulted_default_constructor = true;
        x.explicit_default_constructor = explicit_specified;
        if (access) {
            declare_implicit(c, special_member::constructor, false);
            entity_index constructor = entity_table[c].first_constructor;
            while (entity_table[constructor].next_constructor)
                constructor = entity_table[constructor].next_constructor;
            entity_table[constructor].access = access;
        }
        return analysis_result::well_formed;
    }
    const qualified_type *parameters =
        type_table.parameters(type.unqualified());
    const struct type &first = type_table[parameters[0].unqualified()];
    if (explicit_specified)
        /* an explicit copy constructor: [class.copy.ctor]/7's implicit
           one is not */
        return not_supported_because(
            n, "dcl.fct.def.default",
            "an explicit defaulted copy or move constructor");
    if (f.parameter_count == 1 && first.kind == type_kind::lvalue_reference &&
        first.operand.unqualified() == x.type.unqualified() &&
        first.operand.cv() == cv_const) {
        /* [class.copy.ctor]/7's X(const X&), as implicitly declared */
        for (entity_index k = x.first_constructor; k;
             k = entity_table[k].next_constructor)
            if (copy_or_move_constructor(c, k))
                return not_supported_because(
                    n, "class.mem",
                    "a defaulted copy constructor in a class with an earlier "
                    "copy or move constructor");
        x.defaulted_copy_constructor = true;
        return analysis_result::well_formed;
    }
    /* [class.copy.ctor]/3: X(X&&) = default, the move constructor as
       implicitly declared; its triviality is checked with the class's
       subobjects (implicit_special_members) */
    if (f.parameter_count == 1 && first.kind == type_kind::rvalue_reference &&
        first.operand == qualified_type::make(x.type.unqualified(), cv_none)) {
        if (x.defaulted_move_constructor || declares_move_constructor(c))
            return not_supported_because(n, "class.mem",
                                         "a defaulted move constructor in a "
                                         "class that already declares one");
        x.defaulted_move_constructor = true;
        return analysis_result::well_formed;
    }
    /* X(X&) = default */
    return not_supported_because(n, "dcl.fct.def.default",
                                 "a defaulted constructor that is no default, "
                                 "const copy or move constructor");
}

/*
 * X& operator=(const X&) = default in class X: [dcl.fct.def.default]/5, not
 * user-provided; declared as [class.copy.assign]/2's implicit one would be
 * ("X& X::operator=(const X&)", public, inline), which analysis declares
 * no entity for (assigned_bitwise looks the operator up and finds none).
 * Not analysed: another form, access other than public, a cv- or
 * ref-qualifier, default arguments, the specifiers static, virtual,
 * constexpr, friend.  Unlike the implicit one it suppresses the implicit
 * move constructor and move assignment operator ([class.copy.ctor]/8,
 * [class.copy.assign]/4), which analysis does not declare either.
 */
analysis_result semantic_analysis::defaulted_copy_assignment(entity_index c,
                                                             SyntaxRef n,
                                                             SyntaxElement d,
                                                             const specified &s,
                                                             uint8_t access)
{
    if (access || !s.has_type || s.typedef_ || s.static_ || s.extern_ ||
        s.virtual_ || s.friend_ || s.explicit_ || s.declares_class || s.aligned)
        return not_supported_because(
            n, "dcl.fct.def.default",
            "a defaulted assignment that is not public, has no type or has a "
            "specifier such as static or virtual");
    if (s.constexpr_ && entity_table[c].virtual_base_count)
        return not_supported_because(
            n, "dcl.fct.def.default",
            "a constexpr defaulted assignment in a class with virtual bases");
    qualified_type type = s.type;
    declarator_meaning m;
    function_cv = 0;
    in_member_declarator = true;
    function_parameters.clear();
    analysis_result r = declarator(n, d, type, m);
    in_member_declarator = false;
    if (r != analysis_result::well_formed)
        return r;
    if (m.has_id && m.operator_function &&
        m.implemented != preprocessing_op_or_punc::EQUAL)
        return not_supported_because(
            n, "dcl.fct.def.default",
            "a defaulted operator that is no copy or move assignment "
            "(a comparison operator, [class.compare.default])");
    if (!m.has_id || m.qualifier || function_cv || m.aligned ||
        !m.operator_function ||
        m.implemented != preprocessing_op_or_punc::EQUAL)
        return not_supported_because(
            n, "dcl.fct.def.default",
            "a defaulted function that is no operator, or is declared with "
            "a qualifier, cv-qualifier or alignment");
    for (const parameter_meaning &parameter : function_parameters)
        if (parameter.default_argument)
            return not_supported_because(
                n, "dcl.fct.def.default",
                "a defaulted assignment operator with a default argument");
    const struct type &f = type_table[type.unqualified()];
    if (f.kind != type_kind::function || f.variadic || f.parameter_count != 1)
        return not_supported_because(n, "dcl.fct.def.default",
                                     "a defaulted operator that is not a "
                                     "function of exactly one parameter");
    const entity &x = entity_table[c];
    const struct type &result = type_table[f.operand.unqualified()];
    const struct type &first =
        type_table[type_table.parameters(type.unqualified())[0].unqualified()];
    if (f.operand.cv() || result.kind != type_kind::lvalue_reference ||
        !(result.operand ==
          qualified_type::make(x.type.unqualified(), cv_none)) ||
        !((first.kind == type_kind::lvalue_reference &&
           first.operand ==
               qualified_type::make(x.type.unqualified(), cv_const)) ||
          /* [class.copy.assign]/3: X& operator=(X&&) = default */
          (first.kind == type_kind::rvalue_reference &&
           first.operand ==
               qualified_type::make(x.type.unqualified(), cv_none))))
        return not_supported_because(
            n, "dcl.fct.def.default",
            "a defaulted assignment whose parameter or return type is not the "
            "implicit declaration's");
    bool move = first.kind == type_kind::rvalue_reference;
    /* a second one, or beside a user-declared copy assignment operator */
    lookup_result found = lookup_qualified(
        c, operator_name(preprocessing_op_or_punc::EQUAL), lookup_filter::any);
    /* [class.member.lookup]: an operator= found in a base class is not a
       declaration of c; one copy and one move assignment operator may be
       defaulted ([class.copy.assign]/2, /3) */
    bool declared_here = false;
    if (found.entity && found.found_in == c) {
        for (entity_index f2 = found.entity;
             f2 && entity_table[f2].enclosing == c;
             f2 = entity_table[f2].next_overload) {
            if ((!move && copy_assignment_operator(f2, c)) ||
                (move && move_assignment_operator(f2, c)))
                declared_here = true;
        }
    }
    if (found.unknown || found.ambiguous || declared_here ||
        (move && x.defaulted_move_assignment) ||
        (!move && x.defaulted_copy_assignment))
        return not_supported_because(
            n, "class.mem",
            "a defaulted assignment beside another declaration of that "
            "assignment, or with unknown lookup");
    if (move)
        entity_table[c].defaulted_move_assignment = true;
    else
        entity_table[c].defaulted_copy_assignment = true;
    return analysis_result::well_formed;
}

/* The expressions of an expression-list, with each pack expansion
   analysed once per element ([temp.variadic]/5).  With `written`, a
   braced-init-list argument is allowed: its meaning is empty and the
   syntax of every argument is appended, for the overload resolution of
   [over.match.ctor] and [over.ics.list] to initialize the parameter from it. */
analysis_result semantic_analysis::expression_list_meanings(
    SyntaxElement list, owned_array<expression_meaning> &out,
    owned_array<SyntaxElement> *written)
{
    auto items = list.items(syntax_kind::initializer_list);
    auto end = items.end();
    for (auto i = items.begin(); i != end;) {
        SyntaxElement a = *i;
        ++i;
        if (a.is(syntax_kind::braced_init_list)) {
            if (!written ||
                (i != end && (*i).is(preprocessing_op_or_punc::ELLIPSIS)))
                return not_supported_because(
                    a, "over.match.list",
                    "a braced-init-list argument where none is taken, or "
                    "before an ellipsis");
            out.append(expression_meaning());
            written->append(a);
            continue;
        }
        if (a.is(preprocessing_op_or_punc::ELLIPSIS))
            return not_supported_because(
                list, "temp.variadic",
                "an ellipsis with no preceding argument");
        bool expands = i != end && (*i).is(preprocessing_op_or_punc::ELLIPSIS);
        if (!expands) {
            expression_meaning e;
            VTRY(r, expression(a, e));
            out.append(e);
            if (written)
                written->append(a);
            continue;
        }
        ++i;
        token_index first = first_token(a);
        token_index last = first + 1;
        if (!a.is_token())
            last = a.end_token();
        uint32_t count = 0;
        if (!expansion_count(first, last, count))
            return not_supported_because(
                list, "temp.variadic",
                "an argument pack expansion whose length is not known");
        for (uint32_t k = 0; k < count; k++) {
            expression_meaning e;
            VTRY(r, expansion_element_expression(a, k, e));
            out.append(e);
            if (written)
                written->append(a);
        }
    }
    return analysis_result::well_formed;
}

/* a trivial copy or move constructor that is no entity (a trivial
   implicitly-declared one, or X(X&&) = default) as a candidate of
   [over.match.ctor]/1: void(parameter) with no function to call */
semantic_analysis::overload_candidate
semantic_analysis::trivial_constructor_candidate(qualified_type parameter)
{
    overload_candidate one;
    one.type = qualified_type::make(
        type_table.function(
            qualified_type::make(
                type_table.fundamental(fundamental_type::void_), cv_none),
            &parameter, 1, false),
        cv_none);
    return one;
}

/*
 * Whether the deleted implicit copy constructor of class c can take part
 * in a construction from these arguments ([over.match.ctor]/1): its one
 * parameter binds a single argument of class c or derived from it
 * ([dcl.init.ref]/5); any other argument only reaches it through a
 * user-defined conversion, which it is not a candidate for
 * ([over.best.ics]/4), and the declared constructors decide.
 */
bool semantic_analysis::copy_argument_candidate(
    entity_index c, const owned_array<expression_meaning> &arguments) const
{
    if (arguments.size() != 1)
        return false;
    const struct type &t = type_table[arguments[0].type.unqualified()];
    if (t.kind == type_kind::fundamental || t.kind == type_kind::pointer ||
        t.kind == type_kind::enumeration)
        return false;
    /* anything else not analysed is taken as a candidate */
    if (t.kind != type_kind::class_type)
        return true;
    entity_index from = t.entity;
    bool ambiguous = false;
    return from == c || base_step(from, c, ambiguous);
}

/*
 * [dcl.init]/17.6.2: "Otherwise, if the initialization is
 * direct-initialization, or if it is copy-initialization where the
 * cv-unqualified version of the source type is the same class as, or a
 * derived class of, the class of the destination, constructors are
 * considered.  The applicable constructors are enumerated
 * ([over.match.ctor]), and the best one is chosen through overload
 * resolution ([over.match]).  The constructor so selected is called to
 * initialize the object, with the initializer expression or
 * expression-list as its argument(s)."
 *
 * [over.match.ctor]/1: the candidates are the class's constructors and its
 * implicitly-declared copy constructor ([class.copy.ctor]/6), which for a
 * class that declares none copies the object's bytes (copies_bytes).
 */
analysis_result semantic_analysis::construct(
    SyntaxRef at, qualified_type type,
    owned_array<expression_meaning> &arguments, uint32_t object,
    initialized_value &result, bool copy,
    const owned_array<SyntaxElement> *written_arguments)
{
    entity_index c = type_table[type.unqualified()].entity;
    /* (a copy: instantiating a constructor template can move the table) */
    const entity x = entity_table[c];
    if (x.kind != entity_kind::class_type)
        return not_supported_because(
            at, "class.pre",
            "a construction of a type that is not known to be a class");
    /* [class.abstract]/2 (Note 3): "no objects of an abstract class can be
       created except as subobjects of a class derived from it" */
    if (abstract_class(type) && !constructing_base)
        return not_supported_because(at, "class.abstract",
                                     "a construction of an abstract class");
    /* [class.copy.ctor]/10.1: which construction from arguments the
       deleted implicit copy constructor takes part in (copy elision,
       [dcl.init]/17.6.2's aggregate) is not analysed */
    bool moved = false;
    /* [class.copy.ctor]/8: an xvalue of the class goes to the implicitly
       declared move constructor, which is better than the deleted copy
       constructor ([over.match.best]) */
    if (arguments.size() == 1 &&
        arguments[0].category == value_category::xvalue &&
        declares_move_constructor(c))
        moved = true;
    if (!arguments.empty() && x.implicit_copy_deleted && !moved &&
        !x.defaulted_move_constructor &&
        copy_argument_candidate(c, arguments)) {
        if (arguments.size() == 1 &&
            arguments[0].category != value_category::prvalue &&
            arguments[0].type.unqualified() == type.unqualified())
            return ill_formed(at,
                              "the implicitly-declared copy constructor is "
                              "defined as deleted",
                              "class.copy.ctor");
        return not_supported_because(
            at, "class.copy.ctor",
            "a copy using a deleted implicit copy constructor");
    }
    if (arguments.empty() && trivial_defaulted_default_constructor(c)) {
        /* [dcl.init]/8.2: value-initialization of a class with a trivial
           defaulted default constructor zero-initializes the object */
        expression_meaning zero;
        zero.type = qualified_type::make(type.unqualified(), cv_none);
        zero.category = value_category::prvalue;
        result.node =
            expression_node(semantic_kind::zero, zero, at.first_token());
        result.evaluation = constness::constant;
        return analysis_result::well_formed;
    }
    /* [temp.mem]/1: a constructor template only declared is a candidate
       too (member_template_declaration) */
    if (x.constructor_template)
        return not_supported_because(
            at, "temp.mem",
            "a construction of a class with a constructor template that is "
            "only declared");
    /* [over.match.ctor]/1: "the candidate functions are all the
       constructors of the class of the object being initialized" */
    owned_array<overload_candidate> candidates;
    for (entity_index k = x.first_constructor; k;
         k = entity_table[k].next_constructor) {
        /* [over.match.ctor]/1: "For copy-initialization (including
           default initialization in the context of copy-initialization),
           the candidate functions are all the converting constructors
           ([class.conv.ctor]) of that class" */
        if ((copy && entity_table[k].explicit_) ||
            entity_table[k].constraint_unsatisfied)
            continue;
        overload_candidate one;
        one.function = k;
        one.type = entity_table[k].type;
        candidates.append(one);
    }
    /* [temp.mem]/1: the constructor templates' specializations */
    size_t templates = candidates.size();
    owned_array<uint32_t> first_arguments;
    owned_array<template_argument> deduced;
    if (x.constructor_templates) {
        VTRY(r, constructor_template_candidates(
                    at, c, arguments.data(),
                    static_cast<uint32_t>(arguments.size()), copy, candidates,
                    &first_arguments, &deduced));
    }
    /* the implicitly-declared copy constructor X(const X&)
       ([class.copy.ctor]/6: "If the class definition does not explicitly
       declare a copy constructor, a non-explicit one is declared
       implicitly"), trivial for the members analysis has; a class that
       declares a move constructor has it deleted, and is not told apart */
    bool implicit_copy =
        !x.defaulted_move_constructor || x.defaulted_copy_constructor;
    for (entity_index k = x.first_constructor; k && implicit_copy;
         k = entity_table[k].next_constructor)
        if (copy_or_move_constructor(c, k))
            implicit_copy = false;
    if (implicit_copy)
        candidates.append(trivial_constructor_candidate(qualified_type::make(
            type_table.lvalue_reference_to(
                qualified_type::make(type.unqualified(), cv_const)),
            cv_none)));
    /* X(X&&) = default, trivial (implicit_special_members): it copies the
       bytes */
    size_t move = candidates.size();
    if (x.defaulted_move_constructor)
        candidates.append(trivial_constructor_candidate(qualified_type::make(
            type_table.rvalue_reference_to(
                qualified_type::make(type.unqualified(), cv_none)),
            cv_none)));
    size_t best = 0;
    /* [over.best.ics]/4: "when considering the argument of a constructor
       ... that is a candidate by [over.match.copy]", copy-initialization of
       a class by a user-defined conversion, "user-defined conversion
       sequences are not considered" */
    VTRY(resolved,
         best_viable_function(at, candidates, nullptr, arguments, best, nullptr,
                              !copy, written_arguments));
    entity_index chosen = candidates[best].function;
    if (uint32_t g = candidates[best].template_index) {
        /* the chosen specialization's definition ([temp.inst]/4) */
        size_t k = best - templates;
        size_t end = deduced.size();
        if (k + 1 < first_arguments.size())
            end = first_arguments[k + 1];
        VTRY(r, instantiate(at, function_templates[g - 1].entity,
                            deduced.data() + first_arguments[k],
                            static_cast<uint32_t>(end - first_arguments[k]),
                            chosen));
    }
    if (!chosen) {
        if (!copies_bytes(c) && !(x.defaulted_move_constructor && best == move))
            /* [class.copy.ctor]/14: the implicitly-defined copy
               constructor copies each base and member by its own */
            return not_supported_because(
                at, "class.copy.ctor",
                "a copy or move with no chosen constructor of a class that "
                "does not copy its bytes");
        bool ambiguous = false;
        entity_index from = type_table[arguments[0].type.unqualified()].entity;
        if (from != c) {
            if (base_step(from, c, ambiguous)) {
                /* its const X& bound to the base class subobject */
                VTRY(r, to_base(at.first_token(), c, arguments[0]));
            } else {
                /* user-defined conversion to c (e.g. Src::operator Dst()) */
                qualified_type dst =
                    qualified_type::make(type.unqualified(), cv_none);
                VTRY(r, initialize_with(at, arguments[0], dst, false, false,
                                        result));
                return analysis_result::well_formed;
            }
        }
        result.evaluation = constness::unknown;
        result.value = 0;
        VTRY(r, operand_value(at, arguments[0]));
        result.node = arguments[0].node;
        return analysis_result::well_formed;
    }
    if (!accessible(chosen))
        return ill_formed(at,
                          "a private or protected member's name can be used "
                          "only by members and friends of the class in which "
                          "it is declared",
                          "class.access");
    /* [dcl.fct.def.delete]/2 */
    VTRY(deleted, deleted_use(at.first_token(), chosen));
    size_t n = arguments.size();
    const qualified_type *p =
        type_table.parameters(entity_table[chosen].type.unqualified());
    owned_array<uint32_t> nodes;
    for (size_t i = 0; i < n; i++) {
        /* [over.match.ctor]: each parameter copy-initialized from its
           argument ([expr.call]/7) */
        initialized_value one;
        if (written_arguments &&
            (*written_arguments)[i].is(syntax_kind::braced_init_list)) {
            /* copy-list-initialization, as a call's argument
               ([dcl.init.list]/3.10 for a reference parameter) */
            qualified_type pt = p[i];
            VTRY(r,
                 list_initialization((*written_arguments)[i], pt, false, one));
        } else {
            VTRY(r, initialize_with(at, arguments[i], p[i], false, false, one));
        }
        nodes.append(one.node);
    }
    /* [dcl.fct.default]/1: the default arguments of the parameters left */
    size_t count =
        type_table[entity_table[chosen].type.unqualified()].parameter_count;
    for (size_t i = n; i < count; i++) {
        const entity &k = entity_table[chosen];
        if (k.unknown_defaults || !k.first_default ||
            !default_argument_nodes[k.first_default + i])
            return not_supported_because(
                at, "dcl.fct.default",
                "a constructor call that needs a default argument that is not "
                "available");
        default_argument_used(default_argument_nodes[k.first_default + i]);
        nodes.append(bind_call_site(default_argument_nodes[k.first_default + i],
                                    at.first_token()));
    }
    expression_meaning made;
    made.type = qualified_type::make(type.unqualified(), cv_none);
    made.category = value_category::prvalue;
    made.evaluation = constness::not_constant;
    result.evaluation = constness::not_constant;
    /* [expr.const]/5.2: a constexpr constructor's invocation is the
       evaluator's (S6), and so is an implicit one's: it is constexpr when
       its definition would satisfy [dcl.constexpr]'s rules
       ([class.copy.ctor]/12), which is not decided here */
    if (entity_table[chosen].constexpr_ || entity_table[chosen].implicit) {
        made.evaluation = constness::unknown;
        result.evaluation = constness::unknown;
        constexpr_calls++;
    }
    result.value = 0;
    VTRY(du, diagnose_if_call(at.first_token(), chosen, nodes.data(),
                              nodes.size()));
    result.node = expression_node(semantic_kind::construct, made,
                                  at.first_token(), object, chosen);
    if (!nodes.empty()) {
        semantics.set_list(result.node, &nodes[0], nodes.size());
    }
    return analysis_result::well_formed;
}

/*
 * [class.base.init]/1: "In the definition of a constructor for a class,
 * initializers for direct and virtual base class subobjects and non-static
 * data members can be specified by a ctor-initializer"; /2: "Lookup for an
 * unqualified name in a mem-initializer-id ignores the constructor's
 * function parameter scope. ... Unless the mem-initializer-id names the
 * constructor's class, a non-static data member of the constructor's
 * class, or a direct or virtual base of that class, the mem-initializer is
 * ill-formed."  /3: "A mem-initializer-list can initialize a base class
 * using any class-or-decltype that denotes that base class type."  /7:
 * "The expression-list or braced-init-list in a mem-initializer is used to
 * initialize the designated subobject ... according to the initialization
 * rules of [dcl.init] for direct-initialization."  /13: "(13.3) Then,
 * non-static data members are initialized in the order they were declared
 * in the class definition (again regardless of the order of the
 * mem-initializers)."  /9: a member not named is default-initialized: no
 * initialization for the members analysis has.
 */
analysis_result
semantic_analysis::ctor_initializer(SyntaxElement list, uint32_t &built,
                                    const struct inherited_call *inherited)
{
    entity_index c = current_class();
    token_index at = current_declaration.first_token();
    owned_array<SyntaxElement> items;
    owned_array<bool> item_expands;
    for (uint32_t i = 0; list && i < list.size(); i++) {
        SyntaxElement item = list[i];
        if (item.is(syntax_kind::mem_initializer_list)) {
            for (SyntaxElement one : item.elements()) {
                if (one.is(syntax_kind::mem_initializer)) {
                    items.append(one);
                    item_expands.append(false);
                } else if (one.is(preprocessing_op_or_punc::ELLIPSIS) &&
                           !item_expands.empty()) {
                    item_expands[item_expands.size() - 1] = true;
                } else if (!one.is(preprocessing_op_or_punc::COMMA)) {
                    return not_supported_because(
                        list.index(), "class.base.init",
                        "a mem-initializer-list element that is no "
                        "mem-initializer, ellipsis or comma");
                }
            }
        } else if (item.is(syntax_kind::mem_initializer)) {
            items.append(item);
            item_expands.append(false);
        } else if (item.is(preprocessing_op_or_punc::ELLIPSIS) &&
                   !item_expands.empty()) {
            item_expands[item_expands.size() - 1] = true;
        }
    }
    /* [class.base.init]/6: a delegating constructor (the sole mem-initializer
       names the constructor's own class) */
    if (items.size() == 1 && !item_expands[0] && items[0].size() >= 2 &&
        items[0][0].is_identifier() &&
        tokens.identifier(items[0][0].token()) == entity_table[c].name) {
        SyntaxElement item = items[0];
        token_index id = item[0].token();
        owned_array<expression_meaning> arguments;
        owned_array<SyntaxElement> listed_arguments;
        if (item.size() == 4) {
            VTRY(r, expression_list_meanings(item[2], arguments,
                                             &listed_arguments));
        } else if (item.size() != 3) {
            return not_supported_because(
                item, "class.base.init",
                "a delegating mem-initializer of an unhandled form");
        }
        initialized_value value;
        VTRY(r, construct(item, entity_table[c].type, arguments, 0, value,
                          false, &listed_arguments));
        expression_meaning ptr;
        VTRY_SET(r, this_pointer(id, ptr));
        expression_meaning self;
        self.type = entity_table[c].type;
        self.category = value_category::lvalue;
        self.evaluation = constness::not_constant;
        self.node =
            expression_node(semantic_kind::indirection, self, id, ptr.node);
        if (semantics[value.node].kind == semantic_kind::construct &&
            !semantics[value.node].a) {
            semantics[value.node].a = self.node;
            built = statement_node(semantic_kind::expression_statement, id,
                                   value.node);
            return analysis_result::well_formed;
        }
        if (semantics[value.node].type.unqualified() ==
            entity_table[c].type.unqualified()) {
            uint32_t copy = expression_node(
                semantic_kind::assignment, self, id, self.node, value.node,
                static_cast<uint8_t>(preprocessing_op_or_punc::EQUAL));
            built =
                statement_node(semantic_kind::expression_statement, id, copy);
            return analysis_result::well_formed;
        }
        return not_supported_because(
            item, "class.base.init",
            "a delegating initialization that is not a constructor call");
    }
    /* [class.base.init]/13: "(13.2) Then, direct base classes are
       initialized in declaration order as they appear in the
       base-specifier-list ... (13.3) Then, non-static data members are
       initialized in the order they were declared": the subobjects in that
       order */
    owned_array<entity_index> subobjects;
    construction_order(c, subobjects);
    uint32_t base_count = 0;
    for (entity_index e : subobjects)
        if (entity_table[e].base_subobject)
            base_count++;
    owned_array<uint32_t> initialized;
    initialized.resize(subobjects.size());
    for (uint32_t &i : initialized)
        i = 0;
    /* [class.base.init]/2: the distinct members of an anonymous struct
       share its unnamed object's slot: their initializations run in the
       order written */
    auto set_slot = [&](size_t slot, token_index where, uint32_t statement) {
        if (!initialized[slot]) {
            initialized[slot] = statement;
            return;
        }
        owned_array<uint32_t> both;
        both.append(initialized[slot]);
        both.append(statement);
        initialized[slot] = block_statement(where, both);
    };
    for (size_t idx = 0; idx < items.size(); idx++) {
        SyntaxElement item = items[idx];
        /* mem-initializer-id ( expression-list_opt ) or braced-init-list */
        InitializerParts written = InitializerParts::after(item, 1);
        if (!item.size())
            return not_supported_because(item, "class.base.init",
                                         "an empty mem-initializer");
        uint32_t times = 1;
        if (item_expands[idx]) {
            if (!expansion_count(item.first_token(), item.end_token(), times))
                return not_supported_because(item, "temp.variadic",
                                             "a mem-initializer pack expansion "
                                             "whose length is not known");
        }
        for (uint32_t exp_k = 0; exp_k < times; exp_k++) {
            expansion_scope scope(*this, item_expands[idx], exp_k);
            token_index id = 0;
            uint32_t name = 0;
            entity_index e = 0;
            /* a class-or-decltype with a nested-name-specifier: the class it
           names ([basic.lookup.qual]) */
            entity_index qualified_class = 0;
            if (item[0].is(syntax_kind::simple_template_id,
                           syntax_kind::class_or_decltype)) {
                /* [class.base.init]/3: a base class named by its type. */
                VTRY(r, base_class_name(item[0], item, qualified_class, id));
                if (qualified_class == c)
                    return not_supported_because(
                        item, "class.base.init",
                        "a base-class mem-initializer that names the "
                        "constructor's own class");
                name = tokens.identifier(id);
            } else {
                if (!item[0].is_identifier())
                    return not_supported_because(item, "class.base.init",
                                                 "a mem-initializer-id that is "
                                                 "no identifier or class name");
                id = item[0].token();
                name = tokens.identifier(id);
                if (name == entity_table[c].name)
                    /* [class.base.init]/6: a delegating constructor */
                    return not_supported_because(
                        item, "class.base.init",
                        "a delegating constructor mem-initializer among other "
                        "forms");
                e = entity_table.find(c, name);
            }
            entity_index found_name = e;
            if (e && entity_table[e].kind == entity_kind::typedef_name)
                e = 0;
            if (!e) {
                /* [class.base.init]/3: "A mem-initializer-list can initialize
               a base class using any class-or-decltype that denotes that
               base class type" */
                entity_index base = qualified_class;
                if (!base) {
                    if (!found_name) {
                        /* [class.base.init]/2: skip the parameter scope. */
                        entity_index saved_scope = current_scope;
                        current_scope = c;
                        found_name = lookup(name);
                        current_scope = saved_scope;
                    }
                    base = found_name;
                }
                if (base &&
                    entity_table[base].kind == entity_kind::typedef_name) {
                    const struct type &alias =
                        type_table[entity_table[base].type.unqualified()];
                    base = 0;
                    if (alias.kind == type_kind::class_type)
                        base = alias.entity;
                }
                for (uint32_t i = 0; base && i < entity_table[c].base_count;
                     i++) {
                    entity_index b =
                        class_bases[entity_table[c].first_base + i];
                    if (entity_table[b].type.unqualified() ==
                        entity_table[base].type.unqualified())
                        e = b;
                }
                /* [class.base.init]/2: "a direct or virtual base" */
                if (!e && base &&
                    entity_table[base].kind == entity_kind::class_type)
                    e = virtual_base_of(c, base);
                if (!e && base &&
                    entity_table[base].kind == entity_kind::class_type) {
                    bool ambiguous = false;
                    if (base_step(c, base, ambiguous))
                        return ill_formed_at_token(
                            id,
                            "a mem-initializer-id shall name a direct or "
                            "virtual base of the constructor's class",
                            "class.base.init");
                }
            }
            if (!e || entity_table[e].kind != entity_kind::variable ||
                !entity_table[e].nonstatic_member) {
                if (e && entity_table[e].kind == entity_kind::none)
                    return not_supported_because(
                        item, "over",
                        "a mem-initializer naming a member whose declaration "
                        "was not analysed");
                if (!e && (qualified_class || found_name))
                    /* a name of an enclosing scope */
                    return not_supported_because(
                        item, "class.base.init",
                        "a mem-initializer naming something found that is no "
                        "member or base of the class");
                return ill_formed_at_token(
                    id,
                    "unless the mem-initializer-id names "
                    "the constructor's class, a "
                    "non-static data member of the "
                    "constructor's class, or a direct or "
                    "virtual base of that class, the "
                    "mem-initializer is ill-formed",
                    "class.base.init");
            }
            /* [class.base.init]/2: a member of an anonymous union
           ([class.union.anon]) is initialized in its unnamed object */
            entity_index target = e;
            while (entity_index anon =
                       entity_table[entity_table[target].enclosing]
                           .anonymous_object)
                target = anon;
            size_t slot = 0;
            while (slot < subobjects.size() && subobjects[slot] != target)
                slot++;
            if (slot == subobjects.size())
                return not_supported_because(
                    item, "class.base.init",
                    "a mem-initializer for a member that is no subobject the "
                    "constructor initializes");
            /* [class.base.init]/4: "if a mem-initializer-id designates the
           same member or base class more than once, the program is
           ill-formed" (clang's "multiple initializations"); an anonymous
           struct's distinct members share one unnamed object */
            if (initialized[slot] &&
                (target == e || entity_table[entity_table[e].enclosing].union_))
                return ill_formed_at_token(
                    id,
                    "a mem-initializer-list shall not "
                    "designate the same member more than "
                    "once",
                    "class.base.init");
            qualified_type type = entity_table[e].type;
            /* a copy: subobject and the initializer add types, which may move
           type_table's */
            const struct type record = type_table[type.unqualified()];
            /* [class.base.init]/7: an array member can be initialized
               by a braced-init-list, using the ordinary array rules. */
            /* [dcl.init]/17: or by () where value-initialization of the
               elements is zero-initialization (they are no classes) */
            if (record.kind == type_kind::array && !written.braced_list()) {
                qualified_type element = type;
                while (type_table[element.unqualified()].kind ==
                       type_kind::array)
                    element = type_table[element.unqualified()].operand;
                if (!written || written.list ||
                    type_table[element.unqualified()].kind ==
                        type_kind::class_type)
                    return not_supported_because(
                        item, "class.base.init",
                        "an array member initializer that is not a "
                        "braced-init-list or () of scalars");
            }
            expression_meaning object;
            VTRY(r, subobject(id, e, object));
            if (record.kind == type_kind::class_type &&
                entity_table[record.entity].first_constructor) {
                /* [class.base.init]/7: direct-initialization: a constructor */
                owned_array<expression_meaning> arguments;
                owned_array<SyntaxElement> listed_arguments;
                SyntaxElement braced = written.braced_list();
                if (!braced && written.list &&
                    written.list.is(syntax_kind::braced_init_list)) {
                    bool use_list = is_aggregate(record.entity);
                    if (!use_list) {
                        /* m({a, b}) where the class has no 1-parameter
                           non-copy/move constructor initializes via its
                           implicit copy/move constructor from {a, b} */
                        use_list = true;
                        for (entity_index cf =
                                 entity_table[record.entity].first_constructor;
                             cf; cf = entity_table[cf].next_overload) {
                            if (entity_table[cf].implicit)
                                continue;
                            uint32_t np =
                                type_table[entity_table[cf].type.unqualified()]
                                    .parameter_count;
                            if (np == 1) {
                                use_list = false;
                                break;
                            }
                        }
                    }
                    if (use_list)
                        braced = written.list;
                }
                if (written.list && !braced)
                    r = expression_list_meanings(written.list, arguments,
                                                 &listed_arguments);
                else if (braced) {
                    bool base = entity_table[e].base_subobject;
                    if (base && is_aggregate(record.entity))
                        return not_supported_because(
                            item, "class.base.init",
                            "a braced initializer of an aggregate base class");
                    initialized_value listed;
                    bool saved_constructing_base = constructing_base;
                    constructing_base = base;
                    r = list_initialization(braced, type, true, listed);
                    constructing_base = saved_constructing_base;
                    if (r != analysis_result::well_formed)
                        return r;
                    if (semantics[listed.node].kind ==
                            semantic_kind::construct &&
                        !semantics[listed.node].a) {
                        semantics[listed.node].a = object.node;
                        set_slot(
                            slot, id,
                            statement_node(semantic_kind::expression_statement,
                                           id, listed.node));
                        if (entity_table[e].virtual_base)
                            initialized[slot] =
                                complete_object(initialized[slot]);
                        continue;
                    }
                    /* [dcl.init.list]/3.4: store the value of an aggregate
                       member. A base's tail padding can hold derived members. */
                    if (base ||
                        !aggregate_value_stored(record.entity, listed.node))
                        return not_supported_because(
                            item, "class.base.init",
                            "a braced initializer whose value cannot be stored "
                            "in the base or member");
                    set_slot(slot, id,
                             subobject_store(object, type, id, listed.node));
                    continue;
                } else if (!written)
                    return not_supported_because(
                        item, "class.base.init",
                        "a mem-initializer with neither parentheses contents "
                        "nor braces that is not handled");
                if (r != analysis_result::well_formed)
                    return r;
                if (!entity_table[e].base_subobject && arguments.size() == 1 &&
                    !listed_arguments[0].is(syntax_kind::braced_init_list) &&
                    type_table[arguments[0].type.unqualified()].kind ==
                        type_kind::class_type &&
                    arguments[0].type.unqualified() != type.unqualified()) {
                    qualified_type unqualified =
                        qualified_type::make(type.unqualified(), cv_none);
                    bool ambiguous = false, unknown = false;
                    entity_index conv = conversion_function_for(
                        arguments[0], unqualified, true, ambiguous, unknown);
                    if (conv && entity_table[conv].explicit_ && !ambiguous &&
                        !unknown) {
                        bool converted = false;
                        VTRY(cr, conversion_function_call(item, arguments[0],
                                                          unqualified, true,
                                                          converted));
                    }
                }
                /* [dcl.init]/17.6.1: a member initialized from a prvalue of
               its class is that object, no constructor called: only one
               whose class copies its bytes is analysed (below) */
                bool elided =
                    !entity_table[e].base_subobject && arguments.size() == 1 &&
                    !listed_arguments[0].is(syntax_kind::braced_init_list) &&
                    arguments[0].category == value_category::prvalue &&
                    arguments[0].type.unqualified() == type.unqualified();
                initialized_value value;
                if (elided) {
                    /* [class.copy.elision]/1.3 ([dcl.init]/17.6.1): the
                       prvalue initializes the member itself, whatever its
                       copy and move constructors do (as a variable's
                       initializer, initialize_with), also for a trivially
                       copyable class with a user-provided constructor */
                    VTRY(rv, initialize_with(item, arguments[0], type, true,
                                             false, value));
                    set_slot(
                        slot, id,
                        subobject_store(object, type, id, value.node, true));
                    continue;
                }
                constructing_base = entity_table[e].base_subobject;
                r = construct(item, type, arguments, object.node, value, false,
                              &listed_arguments);
                constructing_base = false;
                if (r != analysis_result::well_formed)
                    return r;
                if (semantics[value.node].kind != semantic_kind::construct) {
                    /* [class.copy.ctor]/14: the implicit copy constructor of a
                   class that copies its bytes (construct gave the
                   argument's value): stored as for a member with no
                   constructors; a base class subobject's bytes are
                   not, its padding may hold a derived member (a base with
                   no tail padding, whose data size is its size, is) */
                    require_complete(record.entity);
                    if (entity_table[e].base_subobject &&
                        entity_table[record.entity].data_size !=
                            entity_table[record.entity].size)
                        return not_supported_because(
                            item, "class.copy.ctor",
                            "a copy of bytes into a base class with tail "
                            "padding");
                    set_slot(slot, id,
                             subobject_store(object, type, id, value.node));
                    continue;
                }
                if (elided)
                    /* (the copy or move constructor of the class is called:
                   [class.copy.elision]/1 calls none) */
                    return not_supported_because(
                        item, "class.copy.elision",
                        "a mem-initializer where copy elision applies");
                set_slot(slot, id,
                         statement_node(semantic_kind::expression_statement, id,
                                        value.node));
                if (entity_table[e].virtual_base)
                    initialized[slot] = complete_object(initialized[slot]);
                continue;
            }
            initialized_value value;
            if (SyntaxElement braced = written.braced_list()) {
                r = list_initialization(braced, type, true, value);
                if (r == analysis_result::well_formed &&
                    (record.kind == type_kind::lvalue_reference ||
                     record.kind == type_kind::rvalue_reference) &&
                    semantics[value.node].kind ==
                        semantic_kind::bind_reference &&
                    semantics[value.node].category == value_category::prvalue)
                    /* the reference is bound to the temporary the list
                   initialized (reference_list_initialization) */
                    return ill_formed(
                        item,
                        "a temporary expression bound to a "
                        "reference member in a mem-initializer is "
                        "ill-formed",
                        "class.base.init");
            } else if (written && !written.list) {
                /* ( ): value-initialization ([dcl.init]/11): zero for the
               members analysis has */
                expression_meaning zero;
                zero.type = qualified_type::make(type.unqualified(), cv_none);
                zero.evaluation = constness::constant;
                value.node = expression_node(semantic_kind::zero, zero,
                                             first_token(item));
            } else if (written.list) {
                SyntaxElement init = written.list;
                if (init.is(syntax_kind::initializer_list)) {
                    if (has_pack_expansion(init)) {
                        owned_array<expression_meaning> expanded;
                        VTRY(re, expression_list_meanings(init, expanded));
                        if (expanded.empty() &&
                            !entity_table[e].base_subobject) {
                            expression_meaning zero;
                            zero.type = qualified_type::make(type.unqualified(),
                                                             cv_none);
                            zero.evaluation = constness::constant;
                            value.node = expression_node(
                                semantic_kind::zero, zero, first_token(item));
                        } else if (expanded.size() == 1 &&
                                   !entity_table[e].base_subobject) {
                            r = initialize_with(item, expanded[0], type, true,
                                                false, value);
                        } else {
                            return not_supported_because(
                                item, "temp.variadic",
                                "a pack expansion inside the expression-list "
                                "of a member initializer");
                        }
                    } else if (record.kind != type_kind::class_type) {
                        return ill_formed(
                            item,
                            "if the entity being initialized does "
                            "not have class type, the "
                            "expression-list in a parenthesized "
                            "initializer shall be a single "
                            "expression",
                            "dcl.init");
                    } else {
                        return not_supported_because(
                            item, "dcl.init",
                            "a parenthesized list of several expressions for a "
                            "class member");
                    }
                } else if (entity_table[e].base_subobject &&
                           entity_table[e].field == UINT32_MAX) {
                    /* an empty base ([class.derived]/4: no field) copied
                       from an expression (json's basic_json(const
                       basic_json &other) : json_base_class_t(other)): its
                       implicit copy constructor copies no byte
                       ([class.copy.ctor]/14), the expression is evaluated
                       for its effects */
                    VTRY(rb,
                         initialize_from(item, init, type, true, false, value));
                    set_slot(slot, id,
                             statement_node(semantic_kind::expression_statement,
                                            id, value.node));
                    continue;
                } else if (entity_table[e].base_subobject) {
                    /* a base copied from an expression: [conv.ptr]/3's
                   conversion of the source */
                    return not_supported_because(
                        item, "class.base.init",
                        "a base class initialized from a single expression "
                        "that is not a constructor call");
                } else {
                    r = initialize_from(item, init, type, true, false, value);
                }
            } else {
                return not_supported_because(
                    item, "class.base.init",
                    "a mem-initializer of an unhandled form");
            }
            if (r != analysis_result::well_formed)
                return r;
            /* the member's initialization: its bytes, stored as by an
           assignment to (*this).member */
            set_slot(slot, id, subobject_store(object, type, id, value.node));
        }
    }
    /* [class.base.init]/9: "In a non-delegating constructor, if a given
       potentially constructed subobject is not designated by a
       mem-initializer-id ... (9.3) otherwise, the entity is
       default-initialized ([dcl.init])": [dcl.init]/7.1, a class's default
       constructor */
    /* (9.1) a default member initializer of a variant member is used only
       when "no other variant member of the union is designated by a
       mem-initializer-id"; (9.2) otherwise "if the entity is an anonymous
       union or a variant member, no initialization is performed" */
    bool union_member_designated = false;
    if (entity_table[c].union_)
        for (size_t i = 0; i < subobjects.size(); i++)
            if (initialized[i])
                union_member_designated = true;
    /* (9.1) the variant member with a default member initializer is the
       one initialized; (9.2) no other variant member is */
    size_t union_default = subobjects.size();
    if (entity_table[c].union_ && !union_member_designated)
        for (size_t i = subobjects.size(); i-- > 0;)
            if (entity_table[subobjects[i]].default_initializer) {
                union_default = i;
                union_member_designated = true;
            }
    /* (9.2) a variant member that is neither designated nor has a default
       member initializer: "no initialization is performed" */
    if (entity_table[c].union_)
        union_member_designated = true;
    for (size_t i = 0; i < subobjects.size(); i++) {
        if (initialized[i])
            continue;
        if (union_member_designated && i != union_default)
            continue;
        const dynamic_class *dynamic = dynamic_of(c);
        if (entity_table[subobjects[i]].virtual_base && dynamic &&
            dynamic->abstract)
            /* [class.base.init]/9 (Note): "An abstract class is never a
               most derived class, thus its constructors never initialize
               virtual base classes" */
            continue;
        if (inherited && subobjects[i] == inherited->base) {
            /* [class.inhctor.init]/1: the base the constructor was
               inherited from, initialized by it */
            VTRY(r, inherited_base_construct(at, *inherited, subobjects[i],
                                             initialized[i]));
            continue;
        }
        if (entity_table[subobjects[i]].default_initializer) {
            /* (9.1) "if the entity is a non-static data member that has a
               default member initializer ([class.mem]) and either ... the
               constructor's class is not a union ... the entity is
               initialized from its default member initializer as
               specified in [dcl.init]" */
            VTRY(r,
                 default_member_initializer(at, subobjects[i], initialized[i]));
            continue;
        }
        qualified_type declared = entity_table[subobjects[i]].type;
        const struct type &record = type_table[declared.unqualified()];
        if (record.kind == type_kind::lvalue_reference ||
            record.kind == type_kind::rvalue_reference ||
            ((declared.cv() & cv_const) &&
             (record.kind == type_kind::fundamental ||
              record.kind == type_kind::pointer ||
              record.kind == type_kind::enumeration)))
            /* [dcl.init]/12, /7: a reference default-initialized, or an
               object of const-qualified type that is no class */
            return ill_formed(current_declaration,
                              "a reference member or a const member of "
                              "non-class type is not initialized by the "
                              "constructor",
                              "class.base.init");
        /* [class.base.init]/9.2: an anonymous union's variant members a
           user-provided constructor does not name are not initialized: the
           union's deleted default constructor is not called (libc++'s
           __tree_node, union { __node_value_type __value_; }) */
        entity_index own = function_state.function;
        bool user_provided =
            own && !entity_table[own].implicit && !declared_defaulted(own);
        if (record.kind == type_kind::class_type &&
            entity_table[record.entity].kind == entity_kind::class_type &&
            !(user_provided &&
              entity_table[record.entity].anonymous_object == subobjects[i]) &&
            default_constructor_deleted(record.entity))
            /* its implicit default constructor is deleted
               ([class.default.ctor]/2) */
            return not_supported_because(
                current_declaration, "class.default.ctor",
                "an implicit initialization of a subobject whose default "
                "constructor is deleted");
        if (record.kind == type_kind::array && record.bound_known &&
            record.bound > 0) {
            qualified_type et = record.operand;
            const struct type &er = type_table[et.unqualified()];
            if (er.kind == type_kind::class_type &&
                entity_table[er.entity].first_constructor &&
                !trivial_defaulted_default_constructor(er.entity)) {
                /* (read before subobject and the pointer type add types,
                   which may move record) */
                uint64_t bound = record.bound;
                expression_meaning arr;
                VTRY(r, subobject(at, subobjects[i], arr));
                /* [class.base.init]/9: each element default-initialized,
                   first to last ([dcl.init]/7.2) */
                expression_meaning elem = array_element_at_index(
                    at, arr.node, et, value_category::lvalue);
                owned_array<expression_meaning> none;
                initialized_value value;
                VTRY(r2, construct(current_declaration, et, none, elem.node,
                                   value));
                initialized[i] = array_loop_statement(
                    at, bound, false,
                    statement_node(semantic_kind::expression_statement, at,
                                   value.node));
                continue;
            }
        }
        if (record.kind == type_kind::class_type &&
            entity_table[record.entity].anonymous_object == subobjects[i] &&
            entity_table[record.entity].first_constructor) {
            /* [class.union.anon]/1: an anonymous union's members belong to
               the class: its default member initializers run here.
               Before C++20 a constexpr constructor must initialize one
               member of each such union ([dcl.constexpr]/4): another
               anonymous union left alone is not analysed */
            /* (a constructor that is not constexpr, as libc++'s
               _LIBCPP_CONSTEXPR_SINCE_CXX26 ones before C++26, is not
               bound by it) */
            /* (a defaulted one, analysed with no function: as before) */
            bool constexpr_constructor =
                !function_state.function ||
                entity_table[function_state.function].constexpr_ ||
                (current_class() &&
                 entity_table[current_class()].defaulted_default_constructor);
            if (constexpr_constructor &&
                !tokens.files.behavior.has(
                    language_feature::three_way_comparison_operator))
                for (size_t j = 0; j < subobjects.size(); j++) {
                    const struct type &other =
                        type_table[entity_table[subobjects[j]]
                                       .type.unqualified()];
                    if (other.kind == type_kind::class_type &&
                        entity_table[other.entity].anonymous_object ==
                            subobjects[j] &&
                        !variant_initialized(subobjects[j]))
                        return not_supported_because(
                            current_declaration, "dcl.constexpr",
                            "a constexpr constructor that leaves an anonymous "
                            "union uninitialized before C++20");
                }
            VTRY(r, anonymous_member_initializers(at, record.entity,
                                                  initialized[i]));
            continue;
        }
        /* [class.default.ctor]/3: default-initialized through a trivial
           X() = default, it is left as it is */
        if (record.kind != type_kind::class_type ||
            !entity_table[record.entity].first_constructor ||
            trivial_defaulted_default_constructor(record.entity))
            continue;
        expression_meaning object;
        VTRY(r, subobject(at, subobjects[i], object));
        owned_array<expression_meaning> none;
        initialized_value value;
        constructing_base = entity_table[subobjects[i]].base_subobject;
        r = construct(current_declaration, entity_table[subobjects[i]].type,
                      none, object.node, value);
        constructing_base = false;
        if (r != analysis_result::well_formed)
            return r;
        initialized[i] =
            statement_node(semantic_kind::expression_statement, at, value.node);
        if (entity_table[subobjects[i]].virtual_base)
            initialized[i] = complete_object(initialized[i]);
    }
    /* [class.cdtor]/4: from the members' initialization on, a virtual
       call finds this class's final overriders: its virtual table pointer
       stored after the bases ([abi:vtable-ctor]) */
    owned_array<uint32_t> statements;
    for (uint32_t i = 0; i < initialized.size(); i++) {
        if (i == base_count)
            if (uint32_t store = vtable_pointer_store(c, at))
                statements.append(store);
        if (initialized[i])
            statements.append(initialized[i]);
    }
    if (initialized.size() == base_count)
        if (uint32_t store = vtable_pointer_store(c, at))
            statements.append(store);
    built = block_statement(at, statements);
    return analysis_result::well_formed;
}

/* the subobject e (a base or a non-static data member) of *this, an lvalue
   ([expr.prim.this], [expr.unary.op]/1, [expr.ref]/6.2) */
analysis_result semantic_analysis::subobject(token_index at, entity_index e,
                                             expression_meaning &m)
{
    if (!entity_table[e].base_subobject) {
        analysis_result r = implicit_member_access(at, e, m);
        type_kind k = type_table[entity_table[e].type.unqualified()].kind;
        if (r == analysis_result::well_formed &&
            (k == type_kind::lvalue_reference ||
             k == type_kind::rvalue_reference)) {
            /* a reference member is initialized itself ([dcl.init.ref]),
               not the object it would refer to ([expr.ref]/6.1) */
            m.type = entity_table[e].type;
            semantics[m.node].type = m.type;
        }
        return r;
    }
    VTRY(r, this_object(at, m));
    return to_base(at, type_table[entity_table[e].type.unqualified()].entity,
                   m);
}

void semantic_analysis::construction_order(
    entity_index c, owned_array<entity_index> &order) const
{
    const entity &x = entity_table[c];
    /* [class.base.init]/13.1: "First, and only for the constructor of the
       most derived class, virtual base classes are initialized" */
    for (uint32_t i = 0; i < x.virtual_base_count; i++)
        order.append(class_virtual_bases[x.first_virtual_base + i]);
    for (uint32_t i = 0; i < x.base_count; i++)
        if (!entity_table[class_bases[x.first_base + i]].virtual_base)
            order.append(class_bases[x.first_base + i]);
    for (uint32_t i = 0; i < x.member_count; i++)
        if (!entity_table[class_members[x.first_member + i]].base_subobject)
            order.append(class_members[x.first_member + i]);
}

uint32_t semantic_analysis::subobject_store(const expression_meaning &object,
                                            qualified_type type, token_index at,
                                            uint32_t value, bool initializes)
{
    /* [dcl.attr.nouniqueaddr]: an empty subobject may share its address with
       another member; do not write its padding byte */
    const struct type &r = type_table[type.unqualified()];
    if (r.kind == type_kind::class_type &&
        entity_table[r.entity].member_count == 0 &&
        entity_table[r.entity].base_count == 0 &&
        !entity_table[r.entity].dynamic)
        return statement_node(semantic_kind::expression_statement, at, value);
    expression_meaning stored = object;
    stored.type = qualified_type::make(type.unqualified(), cv_none);
    stored.evaluation = constness::not_constant;
    stored.entity = 0;
    uint32_t assigned = expression_node(
        semantic_kind::assignment, stored, at, object.node, value,
        static_cast<uint8_t>(preprocessing_op_or_punc::EQUAL));
    if (initializes)
        semantics[assigned].c = 1;
    /* a member's store of its initialization (the constant evaluator's
       places under initialization) */
    semantics[assigned].d = 1;
    return statement_node(semantic_kind::expression_statement, at, assigned);
}

/* statement, run by the complete object constructor or destructor only
   ([abi:mangling-special-ctor-dtor] C1, D1): a virtual base's
   ([class.base.init]/13.1, [class.dtor]/13) */
uint32_t semantic_analysis::complete_object(uint32_t statement)
{
    semantics[statement].d = complete_object_only;
    return statement;
}

uint32_t
semantic_analysis::block_statement(token_index at,
                                   const owned_array<uint32_t> &statements)
{
    uint32_t built = statement_node(semantic_kind::block, at);
    if (!statements.empty()) {
        semantics.set_list(built, &statements[0], statements.size());
    }
    return built;
}

expression_meaning
semantic_analysis::array_element_at_index(token_index at, uint32_t array,
                                          qualified_type element,
                                          value_category category)
{
    /* [expr.sub]/2: E1[E2] is *((E1)+(E2)), E1 the array converted to a
       pointer ([conv.array]) */
    expression_meaning pointer;
    pointer.type =
        qualified_type::make(type_table.pointer_to(element), cv_none);
    pointer.category = value_category::prvalue;
    pointer.evaluation = constness::not_constant;
    pointer.node = expression_node(
        semantic_kind::conversion, pointer, at, array, 0,
        static_cast<uint8_t>(conversion_kind::array_to_pointer));
    expression_meaning index;
    index.type = qualified_type::make(
        type_table.fundamental(fundamental_type::long_int), cv_none);
    index.category = value_category::prvalue;
    index.evaluation = constness::unknown;
    index.node = expression_node(semantic_kind::array_index, index, at);
    expression_meaning result;
    result.type = element;
    result.category = category;
    result.evaluation = constness::not_constant;
    result.node = expression_node(semantic_kind::subscript, result, at,
                                  pointer.node, index.node);
    return result;
}

uint32_t semantic_analysis::array_loop_statement(token_index at, uint64_t count,
                                                 bool reverse, uint32_t body)
{
    uint32_t loop =
        statement_node(semantic_kind::array_loop, at, body, reverse, 0, 0);
    semantics[loop].value = count;
    return loop;
}

/* the classes of the subobjects of class c (its bases' and its members',
   arrays' elements), in the order of their construction */
void semantic_analysis::subobject_classes(entity_index c,
                                          owned_array<entity_index> &out) const
{
    const entity &x = entity_table[c];
    for (uint32_t i = 0; i < x.virtual_base_count; i++)
        out.append(
            type_table
                [entity_table[class_virtual_bases[x.first_virtual_base + i]]
                     .type.unqualified()]
                    .entity);
    for (uint32_t i = 0; i < x.base_count; i++)
        if (!entity_table[class_bases[x.first_base + i]].virtual_base)
            out.append(type_table[entity_table[class_bases[x.first_base + i]]
                                      .type.unqualified()]
                           .entity);
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = entity_table[class_members[x.first_member + i]];
        if (m.base_subobject)
            continue;
        qualified_type t = m.type;
        while (type_table[t.unqualified()].kind == type_kind::array)
            t = type_table[t.unqualified()].operand;
        if (type_table[t.unqualified()].kind == type_kind::class_type)
            out.append(type_table[t.unqualified()].entity);
    }
}

/* a protected destructor of a base class is accessible from class c's
   members, one of a member's class is not ([class.access.base]/1) */
bool semantic_analysis::subobject_destructor_inaccessible(entity_index c,
                                                          entity_index k) const
{
    entity_index kd = entity_table[k].destructor;
    if (!kd)
        return false;
    uint8_t access = entity_table[kd].access;
    if (access != 1) {
        if (access == 0)
            return false;
        /* [class.access.base]/1: a private member is accessible in the
           members and friends of its class ([class.friend]/2), and of a
           class nested in it ([class.access.nest]/1) */
        for (entity_index e = c; !is_namespace(e);
             e = entity_table[e].enclosing)
            if (e == k || grants(k, e))
                return false;
        return true;
    }
    const entity &x = entity_table[c];
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = entity_table[class_members[x.first_member + i]];
        if (!m.base_subobject && element_class(m.type) == k)
            return true;
    }
    return false;
}

/*
 * [class.default.ctor]/1: "If there is no user-declared constructor for
 * class X, a non-explicit constructor having no parameters is implicitly
 * declared as defaulted"; /3: "A default constructor is trivial if it is
 * not user-provided and if ... (3.3) no non-static data member of its
 * class has a default member initializer, and (3.4) all the direct base
 * classes of its class have trivial default constructors, and (3.5) for
 * all the non-static data members of its class that are of class type
 * (or array thereof), each such class has a trivial default constructor";
 * [class.copy.ctor]/6: "If the class definition does not explicitly
 * declare a copy constructor, a non-explicit one is declared implicitly";
 * /11: it is trivial when each base's and member's is; [class.dtor]/4:
 * "If a class has no user-declared prospective destructor, a prospective
 * destructor is implicitly declared as defaulted"; /8: trivial when
 * each base's and member's is.  The non-trivial ones are declared here,
 * as entities like user-declared ones (inline, [class.default.ctor]/4:
 * "implicitly defined"); implicit_definitions gives them their bodies.
 * A trivial one is no entity: a copy copies the bytes, a default
 * construction or destruction does nothing.  Arrays of classes whose
 * construction or destruction runs code are not analysed.
 */
analysis_result semantic_analysis::implicit_special_members(entity_index c,
                                                            SyntaxRef n)
{
    /* (a copy: a probe below may make entities, which moves the table) */
    const entity x = entity_table[c];
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = entity_table[class_members[x.first_member + i]];
        qualified_type t = m.type;
        const struct type &ar = type_table[t.unqualified()];
        if (ar.kind != type_kind::array)
            continue;
        bool all_dims_known = true;
        while (type_table[t.unqualified()].kind == type_kind::array) {
            const struct type &dim = type_table[t.unqualified()];
            if (!dim.bound_known || dim.bound == 0)
                all_dims_known = false;
            t = dim.operand;
        }
        if (all_dims_known)
            continue;
        const struct type &r = type_table[t.unqualified()];
        if (r.kind == type_kind::class_type &&
            (entity_table[r.entity].first_constructor || destroys(r.entity)))
            return not_supported_because(
                n, "class.ctor",
                "a multidimensional or zero or unknown bound array member of a "
                "class with constructors or a destructor");
    }
    owned_array<entity_index> classes;
    subobject_classes(c, classes);
    /* [class.copy.assign]/12 before C++11: the implicit copy assignment
       operator that calls a subobject's user-declared one (clang defines
       it, and reports what its call breaks, at the class) */
    if (!tokens.files.behavior.has(language_feature::rvalue_references))
        for (entity_index k : classes)
            if (!assigned_bitwise(k))
                return not_supported_because(
                    n, "class.copy.assign",
                    "an implicit copy assignment before C++11 that calls a "
                    "subobject's assignment operator");
    bool construction = false, destruction = false, copy = false;
    /* [class.default.ctor]/3: not trivial when "no non-static data member
       of its class has a default member initializer" fails */
    for (uint32_t i = 0; i < x.member_count; i++)
        if (entity_table[class_members[x.first_member + i]].default_initializer)
            construction = true;
    /* /3.1, [class.copy.ctor]/11.1: a class with virtual functions has
       neither a trivial default constructor nor a trivial copy one; each
       stores the virtual table pointer ([class.cdtor]) */
    if (x.dynamic) {
        construction = true;
        copy = true;
    }
    for (entity_index k : classes) {
        /* /3.4, /3.5: a subobject's trivial X() = default keeps it trivial */
        if (entity_table[k].first_constructor &&
            !trivial_defaulted_default_constructor(k))
            construction = true;
        if (destroys(k))
            destruction = true;
        if (!copies_bytes(k))
            copy = true;
    }
    /* [class.default.ctor]/2, [class.copy.ctor]/10, [class.dtor]/7: an
       implicitly-declared one that would call a subobject's constructor
       or destructor that does not exist or is not accessible is defined
       as deleted, an error only where it is used: not analysed */
    bool declares_copy = false, copy_deleted = false;
    for (entity_index q = x.first_constructor; q;
         q = entity_table[q].next_constructor)
        if (copy_or_move_constructor(c, q))
            declares_copy = true;
    /* [class.copy.ctor]/8: "If the definition of a class X does not
       explicitly declare a move constructor, a non-explicit one will be
       implicitly declared as defaulted if and only if X does not have a
       user-declared copy constructor, copy assignment operator, move
       assignment operator, or destructor": otherwise a copy copies each
       subobject by its copy constructor */
    bool move_implicitly_declared =
        !declares_copy && !x.destructor && !x.user_copy_assignments &&
        !x.defaulted_copy_assignment && !x.defaulted_move_assignment;
    /* [class.copy.ctor]/8: the implicit move constructor X(X&&), declared
       when some subobject has a move constructor (otherwise a move copies
       as the copy does); defined as deleted, and then ignored by overload
       resolution ([over.match.funcs]/8), when a subobject can be neither
       moved nor copied */
    bool move_declared = false, move_deleted = false;
    for (entity_index k : classes) {
        const entity sub = entity_table[k];
        /* X() = default beside other constructors may be no entity
           (trivial_defaulted_default_constructor) */
        bool default_ok = (!sub.first_constructor ||
                           trivial_defaulted_default_constructor(k)) &&
                          !sub.implicit_default_deleted,
             copy_ok = copies_bytes(k), copy_usable = false,
             move_usable = false, move_seen = false;
        for (entity_index q = sub.first_constructor; q;
             q = entity_table[q].next_constructor) {
            /* [class.access]/1: c a friend of k may call it, where an
               aggregate initialization of c may not (not analysed for an
               aggregate c; any other class initializes k in its own
               members' context) */
            bool is_default =
                !type_table[entity_table[q].type.unqualified()].parameter_count;
            if (entity_table[q].access && grants(k, c) && !is_default &&
                is_aggregate(c))
                return not_supported_because(
                    n, "class.friend",
                    "a friend's access to a non-public subobject constructor "
                    "in an aggregate");
            bool allowed = !entity_table[q].access || grants(k, c);
            if (is_default && entity_table[q].access == 1) {
                bool ambiguous = false;
                bool has_member = false;
                for (uint32_t i = 0; i < x.member_count; i++) {
                    const entity &member =
                        entity_table[class_members[x.first_member + i]];
                    if (member.nonstatic_member && !member.base_subobject &&
                        !member.default_initializer &&
                        element_class(member.type) == k)
                        has_member = true;
                }
                if (!has_member && base_step(c, k, ambiguous) && !ambiguous)
                    allowed = true;
            }
            bool usable = allowed && !entity_table[q].deleted;
            bool is_move = false;
            if (copy_or_move_constructor(k, q))
                is_move =
                    type_table[type_table
                                   .parameters(
                                       entity_table[q].type.unqualified())[0]
                                   .unqualified()]
                        .kind == type_kind::rvalue_reference;
            if (is_move) {
                move_seen = true;
                if (usable)
                    move_usable = true;
            }
            if (!usable)
                continue;
            if (!type_table[entity_table[q].type.unqualified()].parameter_count)
                default_ok = true;
            /* [class.copy.ctor]/10.1: a move constructor does not copy */
            if (copy_or_move_constructor(k, q) && !is_move)
                copy_usable = true;
        }
        if (copy_usable)
            copy_ok = true;
        /* [class.default.ctor]/2.1: a constructor template whose template
           arguments all have defaults and that takes no parameter (libc++'s
           unique_ptr()) is the subobject's default constructor */
        if (!default_ok && sub.constructor_templates && !sub.union_) {
            owned_array<overload_candidate> found;
            VTRY(probe, constructor_template_candidates(n, k, nullptr, 0, false,
                                                        found));
            if (!found.empty())
                default_ok = true;
        }
        if (move_seen && !copies_bytes(k))
            move_declared = true;
        /* the subobject's move, by overload resolution on an xvalue: its
           move constructor if it declares one, else its copy constructor */
        /* [over.ics.ref]/3: a const member (unique_ptr const) does not bind
           to X(X&&)'s parameter, an xvalue of const T selects the copy
           constructor */
        bool const_member = false;
        for (uint32_t j = 0; j < x.member_count; j++) {
            const entity &mj = entity_table[class_members[x.first_member + j]];
            if (mj.base_subobject || element_class(mj.type) != k)
                continue;
            qualified_type t = mj.type;
            while (type_table[t.unqualified()].kind == type_kind::array)
                t = type_table[t.unqualified()].operand;
            if (t.cv() & cv_const)
                const_member = true;
        }
        bool moves = move_seen && move_usable && !const_member;
        bool copies = copy_usable;
        if (!const_member && move_seen)
            copies = false;
        if (!copies_bytes(k) && !moves && !copies)
            move_deleted = true;
        /* [class.default.ctor]/2.2: a variant member of a union with a
           default member initializer does not delete the constructor */
        bool variant_initialized = false;
        if (x.union_)
            for (uint32_t j = 0; j < x.member_count; j++)
                if (entity_table[class_members[x.first_member + j]]
                        .default_initializer)
                    variant_initialized = true;
        if (variant_initialized && construction && !default_ok &&
            (!x.first_constructor || x.defaulted_default_constructor) &&
            default_constructed(c, k))
            /* the constructor initializes that member alone: not analysed
               for a variant member that has no default constructor */
            return not_supported_because(
                n, "class.default.ctor",
                "a union initialized through a member initializer where "
                "another member has no default constructor");
        if (construction && !default_ok &&
            ((!x.first_constructor && !x.constructor_templates &&
              !x.constructor_template) ||
             x.defaulted_default_constructor) &&
            default_constructed(c, k)) {
            /* C++03 has no deleted functions: the implicit default
               constructor's definition is ill-formed, reported at the
               class */
            if (!tokens.files.behavior.has(language_feature::rvalue_references))
                return not_supported_because(
                    n, "class.default.ctor",
                    "a deleted implicit default constructor before C++11");
            /* [class.default.ctor]/2: the implicit default constructor is
               deleted, an error only where it is used */
            entity_table[c].implicit_default_deleted = true;
        }
        /* [class.copy.ctor]/10.1: the implicit copy constructor is
           deleted when a subobject's copy constructor is deleted or not
           accessible: none is declared, and a copy of the class, which
           does not copy its bytes, is refused where it is used */
        if (!copy_ok && (!declares_copy || x.defaulted_copy_constructor)) {
            /* C++03 has no deleted functions: [impl.clang] clang reports the
               implicit copy constructor's definition at the class */
            if (!tokens.files.behavior.has(language_feature::rvalue_references))
                return not_supported_because(
                    n, "class.default.ctor",
                    "a deleted implicit copy constructor before C++11");
            copy_deleted = true;
            entity_table[c].defaulted_copy_constructor = false;
        }
        /* [class.copy.ctor]/10.2, [class.default.ctor]/2.7: an
           implicit constructor beside a subobject's inaccessible
           destructor is deleted */
        if (subobject_destructor_inaccessible(c, k) &&
            (!x.destructor || !declares_copy || !x.first_constructor)) {
            if (construction && !x.first_constructor)
                entity_table[c].implicit_default_deleted = true;
            copy_deleted = true;
            move_deleted = true;
        }
    }
    /* [temp.mem]/1: a constructor template is user-declared, and the
       implicit copy constructor is declared beside it
       ([class.copy.ctor]/6) */
    bool templates = x.constructor_template || x.constructor_templates;
    bool user_constructors = x.first_constructor != 0 || templates;
    if (templates) {
        construction = true;
        copy = true;
    }
    /* [dcl.fct.def.default]: X() = default beside other constructors: the
       default constructor, trivial or not, that a default-initialization
       chooses */
    bool has_default = false;
    for (entity_index k = x.first_constructor; k;
         k = entity_table[k].next_constructor)
        if (!type_table[entity_table[k].type.unqualified()].parameter_count)
            has_default = true;
    if (x.defaulted_default_constructor && user_constructors && construction &&
        !has_default)
        declare_implicit(c, special_member::constructor, false);
    bool user_copy = false;
    /* entity_table[c], not x: declare_implicit may have moved it */
    for (entity_index k = entity_table[c].first_constructor; k;
         k = entity_table[k].next_constructor)
        if (copy_or_move_constructor(c, k)) {
            /* X(const X&) = default beside a user-declared X(X&): the
               defaulted one is an overload candidate too
               ([over.match.ctor]), declared as the implicit one */
            const struct type &p = type_table
                [type_table.parameters(entity_table[k].type.unqualified())[0]
                     .unqualified()];
            if (entity_table[c].defaulted_copy_constructor &&
                p.kind == type_kind::lvalue_reference &&
                !(p.operand.cv() & cv_const)) {
                copy = true;
                continue;
            }
            /* [class.copy.ctor]/6, /8: a user-provided X(X&&) beside
               X(const X&) = default: the defaulted copy constructor is
               the explicit declaration of the copy one, declared as the
               implicit one is */
            if (p.kind == type_kind::rvalue_reference &&
                entity_table[c].defaulted_copy_constructor &&
                !entity_table[c].defaulted_move_constructor) {
                copy = true;
                continue;
            }
            user_copy = true;
        }
    /* [class.copy.ctor]/11: X(X&&) = default is trivial when each
       subobject's move is (the analysed case: each copies its bytes);
       /6: it makes the implicit copy constructor deleted */
    const entity &y = entity_table[c];
    if (y.defaulted_move_constructor || y.defaulted_move_assignment) {
        bool trivial_move = !y.dynamic;
        for (entity_index k : classes)
            if (!copies_bytes(k))
                trivial_move = false;
        /* [class.default.ctor]/1: "If there is no user-declared
           constructor for class X, a non-explicit constructor having no
           parameters is implicitly declared as defaulted": X(X&&) =
           default is a user-declared constructor (with no entity when
           trivial), so none is.  No default constructor is, for every use
           analysis makes of it, a deleted one ([class.default.ctor]/2: "M
           has no default constructor or ... a function that is deleted");
           only the diagnostic differs (libc++'s <variant> __dtor) */
        if (y.defaulted_move_constructor && !y.defaulted_default_constructor &&
            !user_constructors)
            entity_table[c].implicit_default_deleted = true;
        if (!user_copy && !y.defaulted_copy_constructor)
            copy_deleted = true;
        if (y.defaulted_move_constructor && !trivial_move) {
            entity_table[c].defaulted_move_constructor = false;
            /* [class.copy.ctor]/10.1: a union with a variant member that has
               a non-trivial move constructor has its defaulted move
               constructor defined as deleted */
            if (y.union_)
                move_deleted = true;
            /* [class.copy.ctor]/10: "A defaulted move constructor that
               is defined as deleted is ignored by overload resolution"
               ([over.match.funcs]/8) when a subobject can be neither
               moved nor copied: none is declared */
            if (!move_deleted)
                declare_implicit(c, special_member::constructor, false, true);
            move_declared = false;
        }
    }
    /* [class.copy.ctor]/10.1: a union with a variant member that has a
       non-trivial copy/move constructor has its implicit copy/move
       constructor defined as deleted */
    if (entity_table[c].union_) {
        for (entity_index k : classes) {
            if (!trivial_special_members(k)) {
                copy_deleted = true;
                move_deleted = true;
                break;
            }
        }
    }
    /* [class.copy.ctor]/10.4: a non-static data member of rvalue
       reference type makes the defaulted copy constructor deleted */
    for (uint32_t i = 0; i < entity_table[c].member_count; i++) {
        const entity &m =
            entity_table[class_members[entity_table[c].first_member + i]];
        if (m.kind == entity_kind::variable && m.nonstatic_member &&
            type_table[m.type.unqualified()].kind ==
                type_kind::rvalue_reference)
            copy_deleted = true;
    }
    entity_table[c].implicit_copy_deleted = copy_deleted;
    if (copy && !user_copy && !copy_deleted) {
        bool nonconst_copy = false;
        for (entity_index k : classes)
            if (!has_const_copy_constructor(k))
                nonconst_copy = true;
        /* [dcl.fct.def.default]/2: X(const X&) = default where the
           implicit one would be X(X&): ill-formed before C++20, deleted
           from C++20 (not analysed) */
        if (nonconst_copy && entity_table[c].defaulted_copy_constructor)
            return not_supported_because(
                n, "dcl.fct.def.default",
                "X(const X&) = default where a subobject's copy "
                "constructor takes a non-const reference");
        declare_implicit(c, special_member::constructor, true, false,
                         nonconst_copy);
    }
    /* C++03 has no move constructors ([class.copy.ctor]/3 is C++11's) */
    if (move_implicitly_declared && move_declared && !move_deleted &&
        tokens.files.behavior.has(language_feature::rvalue_references)) {
        /* a union's variant members are not moved by analysis */
        if (entity_table[c].union_)
            return not_supported_because(
                n, "class.copy.ctor",
                "an implicit move constructor of a union");
        declare_implicit(c, special_member::constructor, false, true);
    }
    if (!user_constructors && !entity_table[c].implicit_default_deleted &&
        (construction || entity_table[c].first_constructor)) {
        /* [class.default.ctor]/2: "A defaulted default constructor for
           class X is defined as deleted if ... any non-variant non-static
           data member with no default member initializer is of reference
           type" (or a const one without a brace-or-equal-initializer): a
           deleted function has no definition ([dcl.fct.def.delete]), and an
           aggregate with such members is initialized by a braced list,
           which calls no constructor ([dcl.init.aggr]) */
        if (default_constructor_deleted(c))
            entity_table[c].implicit_default_deleted = true;
        else
            declare_implicit(c, special_member::constructor, false);
    }
    if (entity_table[c].defaulted_default_constructor &&
        default_constructor_deleted(c)) {
        entity_table[c].implicit_default_deleted = true;
        for (entity_index k = entity_table[c].first_constructor; k;
             k = entity_table[k].next_constructor)
            if (entity_table[k].implicit &&
                !type_table[entity_table[k].type.unqualified()].parameter_count)
                entity_table[k].deleted = true;
    }
    if (destruction && !entity_table[c].destructor)
        declare_implicit(c, special_member::destructor, false);
    VTRY(assignments, implicit_assignments(c));
    return inherit_constructors(c, n);
}

/*
 * [namespace.udecl]/3: "If a using-declarator names a constructor, its
 * nested-name-specifier shall name a direct base class of the class being
 * defined", and /1 "it declares that the class inherits the set of
 * constructor declarations introduced by the using-declarator".  Kept until
 * the class is complete (inherit_constructors).  A virtual base, a base
 * that is no direct one, and a class with virtual bases have no rules.
 */
analysis_result semantic_analysis::inheriting_using(UsingDeclarator n,
                                                    token_index at,
                                                    entity_index base)
{
    entity_index c = defining_class;
    entity_index b = 0;
    for (uint32_t i = 0; i < entity_table[c].base_count; i++) {
        entity_index k = class_bases[entity_table[c].first_base + i];
        if (entity_table[k].type.unqualified() ==
            entity_table[base].type.unqualified())
            b = k;
    }
    if (!b || entity_table[b].virtual_base ||
        entity_table[c].virtual_base_count ||
        entity_table[base].virtual_base_count)
        return not_supported_because(
            n, "class.inhctor.init",
            "inherited constructors with a virtual base or a base not found "
            "among the direct bases");
    for (const inheriting_declaration &d : inheriting)
        if (d.c == c && d.base == b)
            /* [namespace.udecl]/9: again */
            return analysis_result::well_formed;
    inheriting_declaration d;
    d.c = c;
    d.base = b;
    d.at = at;
    inheriting.append(d);
    entity_table[c].inherits_constructors = true;
    return analysis_result::well_formed;
}

/*
 * [namespace.udecl]/16: "Constructors that are introduced by a
 * using-declaration are treated as though they were constructors of the
 * derived class when looking up the constructors of the derived class ...
 * or forming a set of overload candidates", /15: "If a constructor ...
 * brought from a base class into a derived class has the signature of a
 * constructor of the derived class, it is hidden by it", and
 * [over.match.funcs]/8 excludes the base's copy and move constructors from
 * the one-argument case (they are not inherited here at all), the default
 * constructor has the derived class's own.  One constructor of class c per
 * constructor of the base: of its parameters, forwarding to it
 * ([class.inhctor.init]/1); declared at the class's end, after the
 * implicit special members (which an inherited constructor does not
 * suppress, [class.default.ctor]/1).  Default arguments, ellipsis,
 * parameters of class type that does not copy its bytes and constructor
 * templates have no rules.
 */
analysis_result semantic_analysis::inherit_constructors(entity_index c,
                                                        SyntaxRef n)
{
    for (size_t d = 0; d < inheriting.size(); d++) {
        if (inheriting[d].c != c)
            continue;
        entity_index base_class =
            type_table[entity_table[inheriting[d].base].type.unqualified()]
                .entity;
        if (entity_table[base_class].constructor_template)
            return not_supported_because(n, "temp.mem",
                                         "inherited constructors from a base "
                                         "with a constructor template");
        /* [class.default.ctor]/1: c has an implicit default constructor
           unless a constructor is user-declared, which hides the base's
           X(); else the base's is c's too (clang: "a default constructor
           is inherited") */
        bool user_declared = entity_table[c].defaulted_default_constructor ||
                             entity_table[c].constructor_template ||
                             entity_table[c].constructor_templates;
        for (entity_index k = entity_table[c].first_constructor; k;
             k = entity_table[k].next_constructor)
            if (!entity_table[k].implicit)
                user_declared = true;
        if (entity_table[base_class].constructor_templates) {
            entity_index from = entity_table[base_class].constructor_templates;
            entity_index to = entity_table[c].constructor_templates;
            if (!to) {
                to = make_held_name(entity_table[c].name, c);
                entity_table[c].constructor_templates = to;
            }
            for (size_t i = 0, m = function_templates.size(); i < m; i++)
                if (function_templates[i].name == from &&
                    !function_templates[i].unusable) {
                    function_template copy = function_templates[i];
                    copy.name = to;
                    function_templates.append(copy);
                }
        }
        /* [class.default.ctor]/1: the inherited constructors are no
           user-declared ones, so c has its implicit default constructor;
           a trivial one is no entity until a constructor beside it makes
           the default-initialization ask for one */
        bool has_default = false;
        for (entity_index k = entity_table[c].first_constructor; k;
             k = entity_table[k].next_constructor)
            if (entity_table[k].implicit &&
                !type_table[entity_table[k].type.unqualified()].parameter_count)
                has_default = true;
        /* ([class.default.ctor]/2: deleted when the base has no default
           constructor, or a deleted one) */
        if (!user_declared && !has_default &&
            !entity_table[c].implicit_default_deleted &&
            default_constructor_deleted(base_class))
            entity_table[c].implicit_default_deleted = true;
        if (!user_declared && !has_default &&
            !entity_table[c].implicit_default_deleted)
            declare_implicit(c, special_member::constructor, false);
        bool base_default = false;
        for (entity_index k = entity_table[base_class].first_constructor; k;
             k = entity_table[k].next_constructor)
            if (!type_table[entity_table[k].type.unqualified()].parameter_count)
                base_default = true;
        if (user_declared && !base_default &&
            (!entity_table[base_class].first_constructor ||
             trivial_defaulted_default_constructor(base_class)) &&
            !default_constructor_deleted(base_class))
            /* the base's trivial default constructor has no entity to
               forward to */
            return not_supported_because(
                n, "class.inhctor.init",
                "inherited constructors where the base's trivial default "
                "constructor must be forwarded to");
        for (entity_index k = entity_table[base_class].first_constructor; k;
             k = entity_table[k].next_constructor) {
            const struct type base_type =
                type_table[entity_table[k].type.unqualified()];
            if (copy_or_move_constructor(base_class, k) ||
                (!base_type.parameter_count && !user_declared))
                continue;
            if (base_type.variadic || entity_table[k].first_default ||
                entity_table[k].unknown_defaults)
                return not_supported_because(
                    n, "namespace.udecl",
                    "an inherited constructor that is variadic or has default "
                    "arguments");
            owned_array<qualified_type> parameters;
            for (uint32_t i = 0; i < base_type.parameter_count; i++) {
                parameters.append(type_table.parameters(
                    entity_table[k].type.unqualified())[i]);
                const struct type &p = type_table[parameters[i].unqualified()];
                if (p.kind == type_kind::class_type &&
                    (!copies_bytes(p.entity) || destroys(p.entity)))
                    return not_supported_because(
                        n, "class.inhctor.init",
                        "an inherited constructor with a class parameter that "
                        "is not trivially copied and destroyed");
            }
            /* hidden by a constructor of c with the signature */
            bool hidden = false;
            for (entity_index q = entity_table[c].first_constructor; q;
                 q = entity_table[q].next_constructor) {
                const struct type &own =
                    type_table[entity_table[q].type.unqualified()];
                if (own.parameter_count != base_type.parameter_count)
                    continue;
                bool same = true;
                for (uint32_t i = 0; i < own.parameter_count; i++)
                    if (type_table
                            .parameters(entity_table[q].type.unqualified())[i]
                            .unqualified() != parameters[i].unqualified())
                        same = false;
                if (same) {
                    if (entity_table[q].inherited_constructor)
                        return not_supported_because(
                            n, "namespace.udecl",
                            "an inherited constructor with the signature of "
                            "one already inherited");
                    hidden = true;
                }
            }
            if (hidden)
                continue;
            qualified_type v = qualified_type::make(
                type_table.fundamental(fundamental_type::void_), cv_none);
            qualified_type type = qualified_type::make(
                type_table.function(v, parameters.data(),
                                    base_type.parameter_count, false,
                                    base_type.non_throwing),
                cv_none);
            entity_index e = entity_table.make(entity_kind::function,
                                               entity_table[c].name, c);
            entity &f = entity_table[e];
            f.type = type;
            f.nonstatic_member = true;
            f.special = special_member::constructor;
            f.inline_ = true;
            f.defined = true;
            f.implicit = true;
            f.inherited_constructor = k;
            f.linkage_of_name = entity_table[c].linkage_of_name;
            f.explicit_ = entity_table[k].explicit_;
            f.deleted = entity_table[k].deleted;
            f.access = entity_table[k].access;
            entity_index last = entity_table[c].first_constructor;
            if (!last) {
                entity_table[c].first_constructor = e;
                continue;
            }
            while (entity_table[last].next_constructor)
                last = entity_table[last].next_constructor;
            entity_table[last].next_constructor = e;
        }
    }
    return analysis_result::well_formed;
}

/*
 * [class.inhctor.init]/1: the base subobject `base` of a constructor
 * inherited from it, "initialized by the invocation of the inherited
 * constructor" with the inheriting constructor's parameters: an xvalue
 * for one of class type or of rvalue reference type, the parameter
 * itself otherwise (the arguments are forwarded as std::forward would).
 */
analysis_result semantic_analysis::inherited_base_construct(
    token_index at, const struct inherited_call &call, entity_index base,
    uint32_t &statement)
{
    expression_meaning object;
    VTRY(r, subobject(at, base, object));
    qualified_type base_type = entity_table[base].type;
    entity_index chosen = call.constructor;
    qualified_type function = entity_table[chosen].type;
    owned_array<uint32_t> nodes;
    for (uint32_t i = 0; i < call.parameters->size(); i++) {
        entity_index p = (*call.parameters)[i];
        qualified_type declared = entity_table[p].type;
        const struct type kind = type_table[declared.unqualified()];
        expression_meaning from;
        from.type = declared;
        from.category = value_category::lvalue;
        if (kind.kind == type_kind::lvalue_reference) {
            from.type = kind.operand;
        } else if (kind.kind == type_kind::rvalue_reference) {
            from.type = kind.operand;
            from.category = value_category::xvalue;
        } else if (kind.kind == type_kind::class_type) {
            from.category = value_category::xvalue;
        }
        from.evaluation = constness::not_constant;
        from.node = expression_node(semantic_kind::entity, from, at, p);
        initialized_value one;
        VTRY_SET(
            r, initialize_with(current_declaration, from,
                               type_table.parameters(function.unqualified())[i],
                               false, false, one));
        nodes.append(one.node);
    }
    expression_meaning made;
    made.type = qualified_type::make(base_type.unqualified(), cv_none);
    made.category = value_category::prvalue;
    made.evaluation = constness::unknown;
    uint32_t built = expression_node(semantic_kind::construct, made, at,
                                     object.node, chosen);
    if (!nodes.empty())
        semantics.set_list(built, &nodes[0], nodes.size());
    statement = statement_node(semantic_kind::expression_statement, at, built);
    return analysis_result::well_formed;
}

/* an implicitly-declared constructor (the copy constructor X(const X&)
   when copy) or destructor of class c, public and inline */
/*
 * [class.copy.ctor]/7: whether class c has a copy constructor whose first
 * parameter is "of type const M& or const volatile M&": one declared with
 * such a parameter, X(const X&) = default, or none declared (the implicit
 * one then has the form X(const X&) or, when a subobject has none, X(X&),
 * which is then c's own, declared with its parameter).
 */
bool semantic_analysis::has_const_copy_constructor(entity_index c) const
{
    const entity &x = entity_table[c];
    if (x.defaulted_copy_constructor)
        return true;
    bool any = false;
    for (entity_index k = x.first_constructor; k;
         k = entity_table[k].next_constructor) {
        if (!copy_or_move_constructor(c, k))
            continue;
        const struct type &p =
            type_table[type_table
                           .parameters(entity_table[k].type.unqualified())[0]
                           .unqualified()];
        if (p.kind != type_kind::lvalue_reference)
            continue;
        any = true;
        if (p.operand.cv() & cv_const)
            return true;
    }
    return !any;
}

void semantic_analysis::declare_implicit(entity_index c, special_member kind,
                                         bool copy, bool move,
                                         bool nonconst_copy)
{
    qualified_type v = qualified_type::make(
        type_table.fundamental(fundamental_type::void_), cv_none);
    qualified_type parameter;
    size_t count = 0;
    if (copy) {
        /* [class.copy.ctor]/7: "The implicitly-declared copy constructor
           for a class X will have the form X::X(const X&) if each potentially
           constructed subobject of a class type M (or array thereof) has a
           copy constructor whose first parameter is of type const M& or
           const volatile M&"; otherwise X::X(X&) */
        uint32_t parameter_cv = cv_const;
        if (nonconst_copy)
            parameter_cv = cv_none;
        parameter = qualified_type::make(
            type_table.lvalue_reference_to(qualified_type::make(
                entity_table[c].type.unqualified(), parameter_cv)),
            cv_none);
        count = 1;
    }
    if (move) {
        /* [class.copy.ctor]/3: "If the definition of a class X does not
           explicitly declare a move constructor, a non-explicit one will
           be implicitly declared as defaulted" with the form X::X(X&&) */
        parameter = qualified_type::make(
            type_table.rvalue_reference_to(qualified_type::make(
                entity_table[c].type.unqualified(), cv_none)),
            cv_none);
        count = 1;
    }
    /* [except.spec]/8: an implicitly-declared destructor is non-throwing
       when its subobjects' are (all of the classes analysed); part of the
       type since C++17 */
    bool non_throwing = kind == special_member::destructor &&
                        tokens.files.behavior.has(
                            language_feature::exception_specification_in_type);
    qualified_type type = qualified_type::make(
        type_table.function(v, &parameter, count, false, non_throwing),
        cv_none);
    entity_index e =
        entity_table.make(entity_kind::function, entity_table[c].name, c);
    entity &f = entity_table[e];
    f.type = type;
    f.nonstatic_member = true;
    f.special = kind;
    /* [except.spec]/8: an implicitly declared destructor's is non-throwing
       (its subobjects' are, of the classes analysed); /7's of the other
       special members is left unrecorded */
    if (kind == special_member::destructor)
        f.exceptions.allows = exception_allowance::none;
    f.inline_ = true;
    f.defined = true;
    f.implicit = true;
    f.linkage_of_name = entity_table[c].linkage_of_name;
    /* explicit X() = default */
    f.explicit_ = kind == special_member::constructor && !copy &&
                  entity_table[c].explicit_default_constructor;
    if (kind == special_member::destructor) {
        entity_table[c].destructor = e;
        return;
    }
    if (!entity_table[c].first_constructor) {
        entity_table[c].first_constructor = e;
        return;
    }
    entity_index k = entity_table[c].first_constructor;
    while (entity_table[k].next_constructor)
        k = entity_table[k].next_constructor;
    entity_table[k].next_constructor = e;
}

/*
 * [class.default.ctor]/4: "an implicitly-defined default constructor
 * performs the set of initializations of the class that would be performed
 * by a user-written default constructor for that class with no
 * ctor-initializer and an empty compound-statement"; [class.copy.ctor]/14:
 * "The implicitly-defined copy/move constructor for a non-union class X
 * performs a memberwise copy/move of its bases and members ... in the
 * order of their initialization", each "direct-initialized with the
 * corresponding base or member of x"; [class.dtor]/12: the implicitly
 * defined destructor is a user-written one "with an empty
 * function-body".  The bodies of class c's implicitly-declared special
 * members, at the end of its definition n.
 */
analysis_result semantic_analysis::define_defaulted_special_member(
    entity_index c, entity_index f, SyntaxRef n, bool after_declaration)
{
    current_declaration = n;
    /* [class.base.init]/13: an abstract class's constructor does not
       construct its virtual bases: not analysed */
    if (after_declaration && entity_table[c].virtual_base_count &&
        abstract_class(entity_table[c].type))
        after_declaration = false;
    if (after_declaration &&
        entity_table[f].special == special_member::constructor &&
        !entity_table[c].union_) {
        /* [dcl.fct.def.default]/3: defaulted after its first declaration, a
           function that would be implicitly deleted is ill-formed; a
           subobject's deleted or inaccessible destructor deletes a
           constructor ([class.default.ctor]/2, [class.copy.ctor]/10) */
        owned_array<entity_index> subobjects;
        subobject_classes(c, subobjects);
        for (entity_index sub : subobjects)
            if (entity_table[sub].kind == entity_kind::class_type &&
                (destructor_deleted(sub) ||
                 subobject_destructor_inaccessible(c, sub)))
                return ill_formed(n,
                                  "defaulting this function would delete it "
                                  "after its first declaration",
                                  "dcl.fct.def.default");
    }
    token_index at = n.first_token();
    struct defining_scope {
        bool &flag;
        bool before;
        ~defining_scope()
        {
            flag = before;
        }
    } defining = {defining_defaulted_member, defining_defaulted_member};
    defining_defaulted_member = true;
    /* defaulted on its first declaration: defined only when odr-used */
    struct deferring_scope {
        entity_index &by;
        entity_index before;
        ~deferring_scope()
        {
            by = before;
        }
    } deferring = {needs_deferred_by, needs_deferred_by};
    if (!after_declaration)
        needs_deferred_by = f;
    synthesized_function sf;
    begin_synthesized(
        sf, f,
        qualified_type::make(type_table.fundamental(fundamental_type::void_),
                             cv_none),
        qualified_type::make(type_table.pointer_to(qualified_type::make(
                                 entity_table[c].type.unqualified(), cv_none)),
                             cv_none));
    const struct type &ft = type_table[entity_table[f].type.unqualified()];
    entity_index source = 0;
    owned_array<entity_index> forwarded;
    if (entity_table[f].inherited_constructor) {
        for (uint32_t i = 0; i < ft.parameter_count; i++)
            forwarded.append(synthesized_variable(
                sf,
                type_table.parameters(entity_table[f].type.unqualified())[i],
                true));
    } else if (ft.parameter_count)
        source = synthesized_variable(
            sf, type_table.parameters(entity_table[f].type.unqualified())[0],
            true);
    analysis_result r = analysis_result::well_formed;
    member_initializer_failed = false;
    uint32_t members = 0, destructions = 0;
    if (entity_table[f].special == special_member::destructor) {
        /* [class.cdtor]/4: the destructor's virtual table pointer */
        members = vtable_pointer_store(c, at);
        destructions = subobject_destructions(c, at);
    } else if (entity_table[f].inherited_constructor) {
        inherited_call call;
        call.constructor = entity_table[f].inherited_constructor;
        call.parameters = &forwarded;
        for (const inheriting_declaration &d : inheriting)
            if (d.c == c &&
                type_table[entity_table[d.base].type.unqualified()].entity ==
                    entity_table[entity_table[f].inherited_constructor]
                        .enclosing)
                call.base = d.base;
        r = ctor_initializer(SyntaxElement(), members, &call);
    } else if (source) {
        bool move =
            type_table[type_table
                           .parameters(entity_table[f].type.unqualified())[0]
                           .unqualified()]
                .kind == type_kind::rvalue_reference;
        r = implicit_copy(c, n, source, members, move);
    } else
        r = ctor_initializer(SyntaxElement(), members);
    finish_synthesized(sf, n, at, statement_node(semantic_kind::block, at),
                       members, destructions);
    if (r == analysis_result::ill_formed && after_declaration)
        return ill_formed(n,
                          "defaulting this function would delete it after "
                          "its first declaration",
                          "dcl.fct.def.default");
    if (r == analysis_result::ill_formed) {
        /* [class.default.ctor]/2, [class.copy.ctor]/10: a defaulted
           special member that would be ill-formed is defined as
           deleted (an error only where it is used): not analysed */
        if (entity_table[f].special == special_member::destructor)
            return not_supported_because(n, "class.default.ctor",
                                         "a destructor that would be deleted");
        if (entity_table[f].inherited_constructor)
            return not_supported_because(
                n, "class.default.ctor",
                "an inheriting constructor that would be deleted");
        if (source)
            return not_supported_because(
                n, "class.default.ctor",
                "a copy or move that would be deleted");
        /* [class.mem]/6: an ill-formed default member initializer is
           an error of the class (reported there), whether the constructor
           is deleted or not */
        if (member_initializer_failed)
            return r;
        return not_supported_because(n, "class.default.ctor",
                                     "a default constructor that would be "
                                     "deleted");
    }
    return r;
}

analysis_result semantic_analysis::implicit_definitions(entity_index c,
                                                        SyntaxRef n)
{
    owned_array<entity_index> implicit;
    for (entity_index k = entity_table[c].first_constructor; k;
         k = entity_table[k].next_constructor)
        if (entity_table[k].implicit && !entity_table[k].deleted)
            implicit.append(k);
    if (entity_table[c].destructor &&
        entity_table[entity_table[c].destructor].implicit)
        implicit.append(entity_table[c].destructor);
    for (entity_index f : implicit) {
        entity_index saved_deferred = needs_deferred_by;
        if (!entity_table[f].member_definition_needed)
            needs_deferred_by = f;
        analysis_result r = define_defaulted_special_member(c, f, n);
        needs_deferred_by = saved_deferred;
        if (r != analysis_result::well_formed)
            return r;
    }
    return implicit_assignment_definitions(c, n);
}

/* [class.copy.ctor]/14: each base and member of *this direct-initialized
   from the corresponding one of the source (the parameter, a const X&) */
analysis_result semantic_analysis::implicit_copy(entity_index c, SyntaxRef n,
                                                 entity_index source,
                                                 uint32_t &built, bool move)
{
    token_index at = n.first_token();
    owned_array<entity_index> order;
    construction_order(c, order);
    owned_array<uint32_t> statements;
    for (entity_index e : order) {
        expression_meaning from;
        from.category = value_category::lvalue;
        from.type = type_table[entity_table[source].type.unqualified()].operand;
        from.evaluation = constness::not_constant;
        from.node = expression_node(semantic_kind::entity, from, at, source);
        analysis_result r = analysis_result::well_formed;
        if (entity_table[e].base_subobject)
            r = to_base(at,
                        type_table[entity_table[e].type.unqualified()].entity,
                        from);
        else
            VTRY_SET(r, member_access(at, e, from));
        /* [class.copy.ctor]/14: the move constructor initializes each
           subobject from "static_cast<T&&>(x_i)", an xvalue */
        if (move)
            from.category = value_category::xvalue;
        expression_meaning object;
        VTRY_SET(r, subobject(at, e, object));
        qualified_type type = entity_table[e].type;
        const struct type &record = type_table[type.unqualified()];
        if (record.kind == type_kind::array && record.bound_known &&
            record.bound > 0 &&
            type_table[record.operand.unqualified()].kind ==
                type_kind::class_type &&
            !copies_bytes(type_table[record.operand.unqualified()].entity)) {
            /* (read before the pointer types add types, which may move
               record) */
            uint64_t bound = record.bound;
            qualified_type et = record.operand;
            qualified_type from_et = qualified_type::make(
                et.unqualified(), et.cv() | from.type.cv());
            /* [class.copy.ctor]/14: "if the member is an array, each
               element is direct-initialized with the corresponding
               subobject of x" */
            value_category source_category = value_category::lvalue;
            if (move)
                source_category = value_category::xvalue;
            expression_meaning dst_elem = array_element_at_index(
                at, object.node, et, value_category::lvalue);
            owned_array<expression_meaning> arguments;
            arguments.append(array_element_at_index(at, from.node, from_et,
                                                    source_category));
            initialized_value value;
            r = construct(n, et, arguments, dst_elem.node, value);
            if (r == analysis_result::ill_formed)
                return not_supported_because(
                    n, "class.copy.ctor",
                    "an array element's copy is ill-formed");
            if (r != analysis_result::well_formed)
                return r;
            statements.append(array_loop_statement(
                at, bound, false,
                statement_node(semantic_kind::expression_statement, at,
                               value.node)));
            continue;
        }
        if (record.kind == type_kind::class_type &&
            !copies_bytes(record.entity)) {
            owned_array<expression_meaning> arguments;
            arguments.append(from);
            initialized_value value;
            constructing_base = entity_table[e].base_subobject;
            r = construct(n, type, arguments, object.node, value);
            constructing_base = false;
            /* [class.copy.ctor]/10: "A defaulted copy/move constructor for
               a class X is defined as deleted if X has ... a potentially
               constructed subobject type M (or array thereof) that cannot
               be copied/moved because overload resolution
               ([over.match]), as applied to find M's corresponding
               constructor, results in an ambiguity or a function that is
               deleted or inaccessible"; /7 gives X(X&) when a subobject's
               copy constructor takes a non-const reference: neither is
               analysed */
            if (r == analysis_result::ill_formed)
                return not_supported_because(
                    n, "class.copy.ctor",
                    "a subobject's copy is deleted, ambiguous or "
                    "inaccessible (defined as deleted)");
            if (r != analysis_result::well_formed)
                return r;
            statements.append(statement_node(
                semantic_kind::expression_statement, at, value.node));
            if (entity_table[e].virtual_base)
                statements.back() = complete_object(statements.back());
            continue;
        }
        if (record.kind == type_kind::lvalue_reference ||
            record.kind == type_kind::rvalue_reference) {
            /* [class.copy.ctor]/10.4: a copy constructor is deleted for a
               member of rvalue reference type; (/14) a reference member is
               direct-initialized from the source's, binding the same
               object ([dcl.init.ref]); for a move, "static_cast<T&&>(x_i)"
               with T an lvalue reference stays an lvalue ([dcl.ref]/6) */
            if (record.kind == type_kind::rvalue_reference && !move)
                return not_supported_because(
                    n, "class.copy.ctor",
                    "a member of rvalue reference type (defined as deleted)");
            if (record.kind == type_kind::lvalue_reference)
                from.category = value_category::lvalue;
            initialized_value bound;
            VTRY_SET(r, initialize_with(n, from, type, true, false, bound));
            statements.append(subobject_store(object, type, at, bound.node));
            continue;
        }
        if (record.kind == type_kind::class_type &&
            entity_table[e].field == UINT32_MAX &&
            (!entity_table[e].virtual_base || empty_class(record.entity)))
            /* an empty base: no bytes */
            continue;
        VTRY_SET(r, operand_value(n, from));
        statements.append(subobject_store(object, type, at, from.node));
        if (entity_table[e].virtual_base)
            statements.back() = complete_object(statements.back());
    }
    /* [class.cdtor]/4: the copy's virtual table pointer is its class's,
       not the source's */
    if (uint32_t store = vtable_pointer_store(c, at))
        statements.append(store);
    built = block_statement(at, statements);
    return analysis_result::well_formed;
}

/*
 * [class.dtor]/13: "After executing the body of the destructor and
 * destroying any objects with automatic storage duration allocated within
 * the body, a destructor for class X calls the destructors for X's direct
 * non-variant non-static data members, the destructors for X's
 * non-virtual direct base classes ... Bases and members are destroyed in
 * the reverse order of the completion of their constructor": a block of
 * the calls, 0 if none.
 */
uint32_t semantic_analysis::subobject_destructions(entity_index c,
                                                   token_index at)
{
    owned_array<entity_index> order;
    construction_order(c, order);
    owned_array<uint32_t> statements;
    for (size_t i = order.size(); i-- > 0;) {
        const struct type r =
            type_table[entity_table[order[i]].type.unqualified()];
        if (r.kind == type_kind::array && r.bound_known && r.bound > 0 &&
            type_table[r.operand.unqualified()].kind == type_kind::class_type &&
            entity_table[type_table[r.operand.unqualified()].entity]
                .destructor) {
            qualified_type et = r.operand;
            entity_index destructor =
                entity_table[type_table[et.unqualified()].entity].destructor;
            expression_meaning arr;
            if (subobject(at, order[i], arr) != analysis_result::well_formed)
                return 0;
            /* [class.dtor]/13: "the elements of an array are destroyed in
               reverse order of their construction" */
            expression_meaning elem = array_element_at_index(
                at, arr.node, et, value_category::lvalue);
            expression_meaning call;
            call.type = qualified_type::make(
                type_table.fundamental(fundamental_type::void_), cv_none);
            call.category = value_category::prvalue;
            call.evaluation = constness::not_constant;
            uint32_t destroyed = expression_node(semantic_kind::destroy, call,
                                                 at, elem.node, destructor);
            statements.append(array_loop_statement(
                at, r.bound, true,
                statement_node(semantic_kind::expression_statement, at,
                               destroyed)));
            continue;
        }
        if (r.kind != type_kind::class_type ||
            !entity_table[r.entity].destructor)
            continue;
        /* (an anonymous union's members are variant members of c, which
           are not destroyed: no destructor of the unnamed union is
           called) */
        if (entity_table[r.entity].anonymous_object == order[i] &&
            entity_table[r.entity].union_)
            continue;
        /* read before subobject adds types, which may move r */
        entity_index destructor = entity_table[r.entity].destructor;
        expression_meaning object;
        if (subobject(at, order[i], object) != analysis_result::well_formed)
            return 0;
        expression_meaning call;
        call.type = qualified_type::make(
            type_table.fundamental(fundamental_type::void_), cv_none);
        call.category = value_category::prvalue;
        call.evaluation = constness::not_constant;
        uint32_t destroyed = expression_node(semantic_kind::destroy, call, at,
                                             object.node, destructor);
        statements.append(
            statement_node(semantic_kind::expression_statement, at, destroyed));
        if (entity_table[order[i]].virtual_base)
            statements.back() = complete_object(statements.back());
    }
    if (statements.empty())
        return 0;
    return block_statement(at, statements);
}

/*
 * [dcl.fct.def.general]/1:
 *     function-body:
 *         ctor-initializer_opt compound-statement
 * A function-try-block, = default and = delete are not analysed yet.
 * [class.base.init]/1: "the ctor-initializer ... shall appear only in the
 * definition of a constructor" (else ill-formed: clang's "only
 * constructors take base initializers").
 */
analysis_result
semantic_analysis::special_member_body(FunctionDefinition definition,
                                       FunctionBody body)
{
    SyntaxElement init = body.ctor_initializer();
    scoped_value<entity_index> deferring(needs_deferred_by, needs_deferred_by);
    defer_needs_of_instantiated_body(last_declared);
    if (body.defaulted()) {
        /* [dcl.fct.def.default]/1: out-of-class = default for a constructor
           or destructor (e.g. X::X() = default; X::~X() = default;) */
        entity_index f = last_declared;
        entity_index c = entity_table[f].enclosing;
        if (entity_table[f].special == special_member::constructor ||
            entity_table[f].special == special_member::destructor)
            return define_defaulted_special_member(c, f, definition, true);
    }
    if (!body.compound_statement())
        return not_supported_because(definition, "dcl.fct.def.general",
                                     "a constructor or destructor definition "
                                     "without a compound-statement body");
    if (init &&
        entity_table[last_declared].special != special_member::constructor)
        return ill_formed(init.index(),
                          "a ctor-initializer shall appear only in the "
                          "definition of a constructor",
                          "class.base.init");
    const entity &f = entity_table[last_declared];
    if (f.constexpr_ && f.special == special_member::destructor &&
        !tokens.files.behavior.has(
            language_feature::constexpr_without_constant_result)) {
        /* [dcl.constexpr]/5: a constexpr destructor requires constexpr
           destructors for its class subobjects. A trivial destructor
           has no entity. */
        owned_array<entity_index> subobjects;
        subobject_classes(f.enclosing, subobjects);
        for (entity_index sub : subobjects) {
            entity_index dtor = entity_table[sub].destructor;
            if (!dtor || entity_table[dtor].constexpr_)
                continue;
            if (entity_table[dtor].implicit)
                return not_supported_because(
                    definition, "dcl.constexpr",
                    "a constexpr destructor of a class with a subobject whose "
                    "implicit destructor is not constexpr");
            return ill_formed(definition,
                              "a constexpr destructor requires constexpr "
                              "subobject destructors",
                              "dcl.constexpr");
        }
    }
    if (f.special == special_member::constructor &&
        !entity_table[f.enclosing].union_) {
        /* [class.base.init]/12: "In a non-delegating constructor, the
           destructor for each potentially constructed subobject of class
           type is potentially invoked ([class.dtor])": one that is deleted
           makes the definition ill-formed ([dcl.fct.def.delete]/2) */
        owned_array<entity_index> subobjects;
        subobject_classes(f.enclosing, subobjects);
        for (entity_index sub : subobjects)
            if (entity_table[sub].kind == entity_kind::class_type &&
                (destructor_deleted(sub) ||
                 subobject_destructor_inaccessible(f.enclosing, sub)))
                return ill_formed(definition,
                                  "the destructor of a potentially "
                                  "constructed subobject is potentially "
                                  "invoked by the constructor and it is "
                                  "deleted",
                                  "class.base.init");
        /* the destructors it potentially invokes are used by this
           definition ([temp.inst]/4: an argument-dependent error in an
           instantiated one makes it ill-formed), those of an unnamed
           union's or struct's members too */
        owned_array<entity_index> pending;
        for (entity_index sub : subobjects)
            pending.append(sub);
        for (size_t i = 0; i < pending.size(); i++) {
            entity_index sub = pending[i];
            if (entity_table[sub].kind != entity_kind::class_type)
                continue;
            if (entity_table[sub].anonymous_object)
                subobject_classes(sub, pending);
            entity_index dtor = entity_table[sub].destructor;
            if (!dtor || instantiation_depth)
                continue;
            entity_table[dtor].member_needed_by = definition;
            mark_needed(dtor);
        }
    }
    return analyse_function_body(definition, body.compound_statement(), init);
}

/*
 * [class.mfct]/2: "A member function definition that appears outside of
 * the class definition shall appear in a namespace scope enclosing the
 * class definition."  [dcl.meaning]/1: the qualified declarator-id refers
 * to a constructor or the destructor declared in the class.
 */
analysis_result semantic_analysis::special_member_definition(SyntaxRef n,
                                                             uint32_t at)
{
    /* a decl-specifier-seq before the declarator holds constexpr and inline
       alone (function_specifiers_only) */
    bool constexpr_specified = false, inline_specified = false;
    if (at > 0 && n[at - 1].is(syntax_kind::decl_specifier_seq))
        function_specifiers_only(n[at - 1], constexpr_specified, nullptr,
                                 nullptr, &inline_specified);
    auto body = n.function_definition().function_body();
    if (n.size() != at + 2 || !body || body.function_try_block() ||
        at_block_scope() || function_state.active)
        return not_supported_because(
            n, "dcl.fct.def.general",
            "an out-of-class special member definition of an unhandled form, "
            "such as at block scope");
    qualified_type type = qualified_type::make(
        type_table.fundamental(fundamental_type::void_), cv_none);
    SyntaxRef conversion = conversion_function_id_of(n);
    if (conversion) {
        VTRY(cr, qualified_conversion_type(n, conversion, type));
    }
    declarator_meaning m;
    function_cv = 0;
    function_parameters.clear();
    analysis_result r = declarator(n, n[at], type, m);
    in_member_declarator = false;
    if (r != analysis_result::well_formed)
        return r;
    /* [dcl.fct.default]/6: default arguments added to the class's
       declaration's, not analysed */
    for (const parameter_meaning &p : function_parameters)
        if (p.default_argument)
            return not_supported_because(
                n, "dcl.fct.default",
                "a default argument added by an out-of-class definition");
    entity_index c = m.qualifier;
    if (!c || !m.has_id || m.aligned || (m.gnu_flags & (gnu_packed | gnu_used)))
        return not_supported_because(
            n, "dcl.fct.def.general",
            "an out-of-class special member with no class qualifier or name, "
            "or with alignment, packed or used");
    if (conversion && instantiating) {
        specified s;
        s.constexpr_ = constexpr_specified;
        s.inline_ = inline_specified;
        entity_index e = 0;
        VTRY_SET(r, declare_specialization(n, m.id, type, s, m.exceptions, e));
        if (!body.plain())
            return not_supported_because(
                n, "dcl.fct.def.general",
                "a conversion function specialization whose body is not plain");
        last_declared = e;
        analysis_result br =
            analyse_function_body(n, body.compound_statement());
        last_declared = e;
        return br;
    }
    entity_index e = 0;
    if (conversion) {
        /* [class.conv.fct]: the conversion function of the declared type
           and cv-qualifiers (its cv-qualifier-seq is const here, unlike a
           constructor's or destructor's) */
        e = overload_of_type(own_conversion_functions(c), type, function_cv);
    } else if (tokens.identifier(m.id) != entity_table[c].name) {
        /* ~ with another name */
        return not_supported_because(
            n, "class.dtor",
            "a destructor name that differs from its class name");
    } else if (m.destructor) {
        e = entity_table[c].destructor;
    } else {
        for (entity_index k = entity_table[c].first_constructor; k;
             k = entity_table[k].next_constructor)
            if (entity_table[k].type == type)
                e = k;
    }
    if (!e || (function_cv && !conversion))
        return ill_formed_at_token(m.id,
                                   "when the declarator-id is qualified, the "
                                   "declaration shall refer to a previously "
                                   "declared member of the class",
                                   "dcl.meaning");
    VTRY_SET(r, constexpr_redeclaration(m.id, e, constexpr_specified));
    VTRY_SET(r, compatible_exceptions(
                    m.id, e, resolved_exceptions(m.exceptions, type, 0)));
    /* [basic.def.odr]/1 */
    if (entity_table[e].defined)
        return ill_formed_at_token(m.id,
                                   "no translation unit shall contain more "
                                   "than one definition of any function",
                                   "basic.def.odr");
    entity_table[e].defined = true;
    if (inline_specified)
        entity_table[e].inline_ = true;
    entity_table.add_declaration(e, n);
    last_declared = e;
    return special_member_body(n, n[at + 1].function_body());
}

/* the prvalue of class type type whose result object the node
   initializes */
static void class_prvalue(qualified_type type, constness evaluation,
                          uint32_t node, expression_meaning &m)
{
    m = expression_meaning();
    m.type = qualified_type::make(type.unqualified(), cv_none);
    m.category = value_category::prvalue;
    m.evaluation = evaluation;
    if (m.evaluation == constness::constant)
        m.evaluation = constness::not_constant;
    m.node = node;
}

/*
 * [expr.type.conv]/2: "Otherwise, the expression is a prvalue of the
 * specified type whose result object is direct-initialized with the
 * initializer."  For a class with constructors, ( expression-list_opt )
 * and {}; a class without them only copied from its own type.
 */
analysis_result semantic_analysis::class_conversion(SyntaxRef n, entity_index c,
                                                    expression_meaning &m)
{
    qualified_type type = entity_table[c].type;
    require_complete(c);
    if (!entity_table[c].size)
        return not_supported_because(
            n, "expr.type.conv", "a type conversion to a class with no size");
    owned_array<expression_meaning> arguments;
    initialized_value value;
    analysis_result r = analysis_result::well_formed;
    InitializerParts written = InitializerParts::after(n, 1);
    if (SyntaxElement braced = written.braced_list()) {
        r = list_initialization(braced, type, true, value);
    } else {
        owned_array<SyntaxElement> as_written;
        if (written.list) {
            owned_array<SyntaxElement> *keep = nullptr;
            require_complete(c);
            if (entity_table[c].first_constructor)
                keep = &as_written;
            VTRY_SET(r,
                     expression_list_meanings(written.list, arguments, keep));
        } else if (!written) {
            return not_supported_because(
                n, "expr.type.conv",
                "a type conversion with neither an expression-list nor braces");
        }
        const owned_array<SyntaxElement> *lists = nullptr;
        if (!as_written.empty())
            lists = &as_written;
        return class_direct_initialization(n, c, arguments, m, lists);
    }
    if (r == analysis_result::well_formed)
        class_prvalue(type, value.evaluation, value.node, m);
    return r;
}

/*
 * [expr.type.conv]/2: "the expression is a prvalue of the specified type
 * whose result object is direct-initialized with the initializer", the
 * initializer ( expression-list_opt ); also [expr.static.cast]/4's "T t(E);"
 * for a class T.
 */
analysis_result semantic_analysis::class_direct_initialization(
    SyntaxRef n, entity_index c, owned_array<expression_meaning> &arguments,
    expression_meaning &m, const owned_array<SyntaxElement> *written_arguments)
{
    qualified_type type = entity_table[c].type;
    require_complete(c);
    if (!entity_table[c].size)
        return not_supported_because(
            n, "expr.type.conv",
            "a direct initialization of a class with no size");
    if (arguments.size() == 1 && arguments[0].node &&
        tokens.files.behavior.edition >= standard_edition::cplusplus_17 &&
        type_table[arguments[0].type.unqualified()].kind ==
            type_kind::class_type &&
        type_table[arguments[0].type.unqualified()].entity != c) {
        conversion_sequence found;
        bool cannot_tell = false;
        if (conversion_function_to_class(arguments[0], type, found,
                                         cannot_tell)) {
            conversion_sequence s = class_sequence(arguments[0], type, true);
            if (s.kind == conversion_sequence::form::user_defined &&
                !s.ambiguous && s.function == found.function) {
                expression_meaning conv = arguments[0];
                bool converted = false;
                VTRY(cr,
                     conversion_function_call(n, conv, type, true, converted));
                if (converted &&
                    conv.type.unqualified() == type.unqualified() &&
                    conv.category == value_category::prvalue) {
                    m = conv;
                    return analysis_result::well_formed;
                }
            }
        } else {
            bool ambiguous = false, unknown = false;
            entity_index fn = conversion_function_for(arguments[0], type, true,
                                                      ambiguous, unknown);
            if (fn && entity_table[fn].explicit_ && !ambiguous && !unknown) {
                expression_meaning conv = arguments[0];
                bool converted = false;
                VTRY(cr,
                     conversion_function_call(n, conv, type, true, converted));
                if (converted &&
                    conv.type.unqualified() == type.unqualified() &&
                    conv.category == value_category::prvalue) {
                    m = conv;
                    return analysis_result::well_formed;
                }
            }
        }
    }
    initialized_value value;
    analysis_result r = analysis_result::well_formed;
    if (!entity_table[c].first_constructor && arguments.empty()) {
        /* [dcl.init]/8.2: value-initialization of a class without a
           user-provided default constructor zero-initializes it (the
           implicit one is trivial: no constructor entity); the empty
           list's elements are the same zeros ([dcl.init.aggr]/5.2),
           and the same errors (a reference member, a deleted default
           constructor) */
        if (default_constructor_deleted(c)) {
            /* (8.2): "if T has a non-trivial default constructor [or
               one defined as deleted], the object is
               default-initialized": the deleted function called.
               C++03 reports it at the class */
            if (!tokens.files.behavior.has(language_feature::rvalue_references))
                return not_supported_because(
                    n, "dcl.init",
                    "a value-initialization with a deleted default constructor "
                    "before C++11");
            return ill_formed(n,
                              "value-initialization calls the default "
                              "constructor, which is defined as deleted",
                              "dcl.init");
        }
        owned_array<SyntaxElement> none;
        r = list_initialization_of(n, none, type, true, value);
    } else if (!entity_table[c].first_constructor) {
        /* T(x): "the type conversion expression is equivalent to the
           corresponding cast expression" ([expr.type.conv]/2): of the
           class's own type or of a class derived from it, a copy (of the
           base class subobject, as the declaration T t(x) is); C++20's
           parenthesized aggregate initialization is not analysed */
        bool ambiguous = false;
        if (arguments.size() != 1 ||
            (arguments[0].type.unqualified() != type.unqualified() &&
             (type_table[arguments[0].type.unqualified()].kind !=
                  type_kind::class_type ||
              !base_step(type_table[arguments[0].type.unqualified()].entity, c,
                         ambiguous))))
            return not_supported_because(
                n, "expr.type.conv",
                "a parenthesized initialization of a class without "
                "constructors from other than its own or a derived type");
        r = initialize_with(n, arguments[0], type, true, false, value);
    } else if (is_aggregate(c) && arguments.empty()) {
        /* T() of an aggregate with default member initializers:
           [dcl.init]/9 value-initialization zero-initializes the object
           and then default-initializes it with the implicit, non-trivial
           default constructor.  When every member has an initializer and
           no base class holds data the zeros are overwritten, so the
           constructor alone gives the value; otherwise the construct node
           is marked (op 1) to zero the object first */
        const entity &x = entity_table[c];
        bool overwritten = !x.base_count;
        for (uint32_t i = 0; i < x.member_count; i++)
            if (!entity_table[class_members[x.first_member + i]]
                     .default_initializer)
                overwritten = false;
        r = construct(n, type, arguments, 0, value, false, written_arguments);
        if (r == analysis_result::well_formed && !overwritten) {
            if (semantics[value.node].kind != semantic_kind::construct)
                return not_supported_because(
                    n, "dcl.init",
                    "a value-initialization that is not a constructor call");
            semantics[value.node].op = 1;
        }
    } else if (is_aggregate(c) &&
               (arguments.size() != 1 ||
                arguments[0].type.unqualified() != type.unqualified())) {
        /* an aggregate's implicit constructors (a default member
           initializer makes its default constructor non-trivial) do
           not take these arguments: C++20's parenthesized aggregate
           initialization ([dcl.init]/17) is not analysed */
        return not_supported_because(
            n, "expr.type.conv", "a parenthesized aggregate initialization");
    } else {
        r = construct(n, type, arguments, 0, value, false, written_arguments);
    }
    if (r == analysis_result::well_formed)
        class_prvalue(type, value.evaluation, value.node, m);
    return r;
}

} // namespace cplusplus
