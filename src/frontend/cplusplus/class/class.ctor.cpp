/*
 * class.ctor.cpp: [class.ctor] Constructors and [class.dtor] Destructors:
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
                                                syntax_child d) const
{
    if (!syntax_is(tree, d, syntax_kind::noptr_declarator))
        return false;
    const syntax_node &node = tree[d.index()];
    const syntax_child *children = tree.children(d.index());
    if (node.child_count != 2 ||
        !syntax_is(tree, children[1], syntax_kind::parameters_and_qualifiers))
        return false;
    syntax_child head = children[0];
    if (syntax_is(tree, head, syntax_kind::unqualified_id)) {
        const syntax_child *parts = tree.children(head.index());
        if (tree[head.index()].child_count != 2 ||
            !syntax_is_punctuator(tokens, parts[0],
                                  preprocessing_op_or_punc::TILDE))
            return false;
        head = parts[1];
    }
    if (!head.is_token() || tokens[head.index()].kind != token_kind::identifier)
        return false;
    return const_cast<token_stream &>(tokens).identifier(head.index()) ==
           entity_table[c].name;
}

/*
 * [temp.mem]/1: "A template can be declared within a class or class
 * template; such a template is called a member template."  [temp.mem]/5:
 * a constructor template is never a copy or move constructor; it is a
 * user-declared constructor ([dcl.init.aggr]/1.1, [class.default.ctor]/1:
 * no default constructor is implicitly declared).  Only a constructor
 * template of a class of namespace scope is known: its declaration is not
 * analysed beyond its form, and a construction of the class, which would
 * consider it ([over.match.ctor]), is not analysed either (construct).
 * Any other member function template defined in the class is
 * member_function_template's.
 */
analysis_result semantic_analysis::member_template_declaration(entity_index c,
                                                               node_index n,
                                                               uint8_t access)
{
    const syntax_node &node = tree[n];
    if (node.child_count != 2 ||
        !syntax_is(tree, tree.children(n)[0], syntax_kind::template_head) ||
        tree.children(n)[1].is_token())
        return not_supported(n, "temp.mem");
    owned_array<node_index> parameters;
    if (!template_head_list(tree.children(n)[0].index(), parameters) ||
        parameters.empty())
        return not_supported(n, "temp.mem");
    node_index inner = tree.children(n)[1].index();
    const syntax_node &d = tree[inner];
    if ((d.kind != syntax_kind::function_definition &&
         d.kind != syntax_kind::member_declaration) ||
        !is_namespace(entity_table[c].enclosing))
        return not_supported(n, "temp.mem");
    /* [ext:gnu_attributes] before it all; [class.ctor]/1: "Each
       decl-specifier in the decl-specifier-seq of a constructor
       declaration (if any) shall be friend, inline, constexpr, or an
       explicit-specifier" (and consteval, [dcl.consteval]) */
    uint32_t at = leading_gnu_attributes(inner);
    const syntax_child *dc = tree.children(inner);
    if (d.kind == syntax_kind::function_definition && at < d.child_count &&
        !declares_special_member(c, dc[at]) &&
        !(at + 1 < d.child_count &&
          syntax_is(tree, dc[at], syntax_kind::decl_specifier_seq) &&
          declares_special_member(c, dc[at + 1])))
        return member_function_template(c, n, inner, access);
    /* a constrained constructor template ([temp.constr.decl]) */
    if (head_constraint(n).bits)
        return not_supported(n, "temp.constr");
    if (at < d.child_count &&
        syntax_is(tree, dc[at], syntax_kind::decl_specifier_seq)) {
        node_index seq = dc[at].index();
        for (uint32_t i = 0; i < tree[seq].child_count; i++) {
            syntax_child x = tree.children(seq)[i];
            if (syntax_is(tree, x, syntax_kind::gnu_attribute_specifier))
                continue;
            if (!x.is_token())
                return not_supported(n, "temp.mem");
            switch (p.keyword_of(tokens[x.index()])) {
            case standard_keyword::constexpr_:
            case standard_keyword::consteval_:
            case standard_keyword::inline_:
            case standard_keyword::explicit_:
                continue;
            default:
                return not_supported(n, "temp.mem");
            }
        }
        at++;
    }
    /* a constructor's declarator, not a destructor's (which [temp.mem]/3
       does not allow a template of) */
    if (at >= d.child_count || !declares_special_member(c, dc[at]) ||
        syntax_is(tree, tree.children(dc[at].index())[0],
                  syntax_kind::unqualified_id))
        return not_supported(n, "temp.mem");
    if (!entity_table[c].constructor_template)
        entity_table[c].constructor_template = n;
    return analysis_result::well_formed;
}

/*
 * [class.base.init]/9.1: member e initialized from its default member
 * initializer ([class.mem]/10), analysed in the constructor ([class.mem]/6:
 * a complete-class context, `this` its object), and stored as by an
 * assignment to (*this).e.  A member of class type with constructors, or
 * an array, has no rules here yet.
 */
analysis_result
semantic_analysis::default_member_initializer(token_index at, entity_index e,
                                              uint32_t &statement)
{
    qualified_type type = entity_table[e].type;
    const struct type &record = type_table[type.unqualified()];
    node_index init = entity_table[e].default_initializer;
    if (record.kind == type_kind::array ||
        record.kind == type_kind::lvalue_reference ||
        record.kind == type_kind::rvalue_reference ||
        (record.kind == type_kind::class_type &&
         entity_table[record.entity].first_constructor))
        return not_supported(init, "class.base.init");
    expression_meaning object;
    analysis_result r = subobject(at, e, object);
    if (r != analysis_result::well_formed)
        return r;
    initialized_value value;
    r = initializer(syntax_child::node(init), type, value);
    if (r != analysis_result::well_formed)
        return r;
    statement = subobject_store(object, type, at, value.node);
    return analysis_result::well_formed;
}

/*
 * [class.copy.ctor]/11: "A copy/move constructor for class X is trivial if
 * it is not user-provided and ... (11.4) for each non-static data member of
 * X that is of class type (or array thereof), the constructor selected to
 * copy/move that member is trivial"; [class.dtor]/8: "A destructor is
 * trivial if it is not user-provided and if: ... for all of the non-static
 * data members of its class that are of class type (or array thereof),
 * each such class has a trivial destructor."  A user-declared constructor
 * other than a copy constructor leaves the copy trivial.
 */
bool semantic_analysis::trivial_class(entity_index c) const
{
    return copies_bytes(c) && !destroys(c);
}

/* whether the copy constructor of class c is the implicit one and
   trivial: no subobject's class declares one either */
/* whether constructor k of class c is a copy constructor X(cv X&)
   ([class.copy.ctor]/1, without default arguments) */
bool semantic_analysis::is_copy_constructor(entity_index c,
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
        if (!is_copy_constructor(c, k))
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
        if (entity_table[k].implicit || !is_copy_constructor(c, k))
            continue;
        qualified_type p =
            type_table.parameters(entity_table[k].type.unqualified())[0];
        if (type_table[p.unqualified()].kind == type_kind::lvalue_reference)
            return true;
    }
    return false;
}

bool semantic_analysis::copies_bytes(entity_index c) const
{
    const entity &x = entity_table[c];
    /* [class.copy.ctor]/11.1: not trivial when "class X has no virtual
       functions" fails: the copy stores the virtual table pointer of X */
    if (x.dynamic)
        return false;
    for (entity_index k = x.first_constructor; k;
         k = entity_table[k].next_constructor)
        if (is_copy_constructor(c, k))
            return false;
    owned_array<entity_index> classes;
    subobject_classes(c, classes);
    for (entity_index k : classes)
        if (!copies_bytes(k))
            return false;
    return true;
}

/*
 * Whether a decl-specifier-seq has only function-specifiers analysis has
 * rules for on a constructor or destructor: constexpr ([class.ctor]/1,
 * [class.dtor]/1) and, in a class (virtual_specified), virtual
 * ([class.dtor]/1, [dcl.fct.spec]/2; on a constructor it is ill-formed,
 * left to special_member_declarator).  A constexpr destructor is C++20's
 * and is not analysed.
 */
bool semantic_analysis::function_specifiers_only(node_index seq,
                                                 bool &constexpr_specified,
                                                 bool *virtual_specified,
                                                 bool *explicit_specified) const
{
    constexpr_specified = false;
    bool virtual_seen = false, explicit_seen = false;
    if (explicit_specified)
        *explicit_specified = false;
    if (virtual_specified)
        *virtual_specified = false;
    for (uint32_t i = 0; i < tree[seq].child_count; i++) {
        syntax_child c = tree.children(seq)[i];
        if (!c.is_token())
            return false;
        switch (p.keyword_of(tokens[c.index()])) {
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
        default:
            return false;
        }
    }
    return constexpr_specified || virtual_seen || explicit_seen;
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
    entity_index c, node_index declaration, syntax_child d, uint8_t access,
    bool definition, bool constexpr_specified, bool virtual_specified,
    bool explicit_specified)
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
        r = gnu_attribute_specifier(tree.children(declaration)[i].index(), m);
        if (r != analysis_result::well_formed)
            return r;
    }
    /* [dcl.fct.default]: a member function's default arguments are a
       complete-class context ([class.mem]/6): analysed with the class
       complete (member_default_arguments) */
    owned_array<parameter_meaning> listed;
    bool has_defaults = false;
    for (const parameter_meaning &p : function_parameters) {
        listed.append(p);
        if (p.default_argument.index())
            has_defaults = true;
    }
    if (!m.has_id || m.qualifier || m.aligned ||
        (m.gnu_flags & (gnu_packed | gnu_used)))
        return not_supported(declaration, "class.ctor");
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
        return not_supported(declaration, "dcl.fct");
    const qualified_type *parameters =
        type_table.parameters(type.unqualified());
    special_member kind = special_member::constructor;
    if (m.destructor && constexpr_specified)
        /* C++20's constexpr destructors ([dcl.constexpr]/3) */
        return not_supported(declaration, "dcl.constexpr");
    if (explicit_specified && m.destructor)
        /* [dcl.fct.spec]/3: "An explicit-specifier shall be used only in
           the declaration of a constructor or conversion function within
           its class definition" (clang's error) */
        return not_supported(declaration, "dcl.fct.spec");
    if (virtual_specified && !m.destructor)
        /* [class.ctor]/1 lists no virtual: clang's "constructor cannot be
           declared 'virtual'" */
        return not_supported(declaration, "class.ctor");
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
            listed.size() == f.parameter_count &&
            listed[1].default_argument.index()) {
            /* [class.copy.ctor]/1, /5: a first parameter of X, X& or
               X&& and the other parameters' default arguments: a copy or
               move constructor, or ill-formed (X), not told apart */
            qualified_type first = parameters[0];
            const struct type &r = type_table[first.unqualified()];
            if (r.kind == type_kind::lvalue_reference ||
                r.kind == type_kind::rvalue_reference)
                first = r.operand;
            if (first.unqualified() == entity_table[c].type.unqualified())
                return not_supported(declaration, "class.copy.ctor");
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
       (clang rejects it too, with another reason) or deleted (clang
       accepts it) is not analysed */
    if (constexpr_specified && entity_table[c].virtual_base_count &&
        definition && !m.destructor)
        return ill_formed_at_token(m.id,
                                   "the class of a constexpr constructor "
                                   "shall not have any virtual base classes",
                                   "dcl.constexpr");
    if (constexpr_specified && entity_table[c].virtual_base_count)
        return not_supported(declaration, "dcl.constexpr");
    entity_index e =
        entity_table.make(entity_kind::function, entity_table[c].name, c);
    entity &x = entity_table[e];
    x.type = type;
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
    x.inline_ = definition || constexpr_specified;
    x.constexpr_ = constexpr_specified;
    x.defined = definition;
    x.access = access;
    x.linkage_of_name = entity_table[c].linkage_of_name;
    entity_table.add_declaration(e, declaration);
    if (has_defaults)
        defer_default_arguments(e, declaration, listed);
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
        analysis_result r =
            virtual_member(c, e, declaration, m.id, virtual_specified);
        virt_override = virt_final = false;
        return r;
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
    entity_index c, node_index n, syntax_child d, syntax_child body,
    uint8_t access, bool constexpr_specified, bool virtual_specified,
    bool explicit_specified)
{
    if (deleted_body(body)) {
        if (explicit_specified)
            return not_supported(n, "dcl.fct.def.delete");
        analysis_result r = special_member_declarator(
            c, n, d, access, false, constexpr_specified, virtual_specified);
        if (r != analysis_result::well_formed)
            return r;
        const entity &f = entity_table[last_declared];
        /* a deleted destructor (libc++'s __nat): the class's destructor,
           deleted, which a delete-expression checks (deleted_use); an
           object of the class is never destroyed in a well-formed
           program */
        if (f.special == special_member::destructor)
            return deleted_definition(n, last_declared);
        /* X(X&) = delete or X(X&&) = delete: which one a return of a local
           chooses depends on [class.copy.elision]/3's implicit move, not
           analysed */
        const struct type &ft = type_table[f.type.unqualified()];
        if (ft.parameter_count) {
            const struct type &first =
                type_table[type_table.parameters(f.type.unqualified())[0]
                               .unqualified()];
            if ((first.kind == type_kind::lvalue_reference ||
                 first.kind == type_kind::rvalue_reference) &&
                first.operand.unqualified() ==
                    entity_table[c].type.unqualified() &&
                !(first.kind == type_kind::lvalue_reference &&
                  first.operand.cv() == cv_const))
                return not_supported(n, "class.copy.elision");
        }
        return deleted_definition(n, last_declared);
    }
    const syntax_node &b = tree[body.index()];
    if (b.child_count != 3 || !tree.children(body.index())[1].is_token() ||
        p.keyword_of(tokens[tree.children(body.index())[1].index()]) !=
            standard_keyword::default_)
        return not_supported(n, "dcl.fct.def.general");
    if (access || constexpr_specified)
        return not_supported(n, "dcl.fct.def.default");
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
        return not_supported(n, "dcl.fct.def.default");
    for (const parameter_meaning &parameter : function_parameters)
        if (parameter.default_argument.index())
            return not_supported(n, "dcl.fct.def.default");
    const struct type &f = type_table[type.unqualified()];
    if (f.variadic)
        return not_supported(n, "dcl.fct.def.default");
    entity &x = entity_table[c];
    if (m.destructor) {
        /* [class.dtor]/4: as the implicitly-declared destructor */
        if (x.destructor || f.parameter_count || explicit_specified)
            return not_supported(n, "dcl.fct.def.default");
        if (!virtual_specified)
            return analysis_result::well_formed;
        /* virtual ~X() = default: the implicit destructor's definition,
           virtual ([class.virtual]/7) */
        declare_implicit(c, special_member::destructor, false);
        entity_index e = entity_table[c].destructor;
        entity_table.add_declaration(e, n);
        virt_override = virt_final = virt_pure = false;
        return virtual_member(c, e, n, m.id, true);
    }
    if (!f.parameter_count) {
        if (x.defaulted_default_constructor)
            return not_supported(n, "class.mem");
        x.defaulted_default_constructor = true;
        x.explicit_default_constructor = explicit_specified;
        return analysis_result::well_formed;
    }
    const qualified_type *parameters =
        type_table.parameters(type.unqualified());
    const struct type &first = type_table[parameters[0].unqualified()];
    if (explicit_specified)
        /* an explicit copy constructor: [class.copy.ctor]/7's implicit
           one is not */
        return not_supported(n, "dcl.fct.def.default");
    if (f.parameter_count == 1 && first.kind == type_kind::lvalue_reference &&
        first.operand.unqualified() == x.type.unqualified() &&
        first.operand.cv() == cv_const) {
        /* [class.copy.ctor]/7's X(const X&), as implicitly declared */
        for (entity_index k = x.first_constructor; k;
             k = entity_table[k].next_constructor)
            if (is_copy_constructor(c, k))
                return not_supported(n, "class.mem");
        return analysis_result::well_formed;
    }
    /* X(X&) = default, a move constructor */
    return not_supported(n, "dcl.fct.def.default");
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
                                                             node_index n,
                                                             syntax_child d,
                                                             const specified &s,
                                                             uint8_t access)
{
    if (access || !s.has_type || s.typedef_ || s.static_ || s.extern_ ||
        s.constexpr_ || s.virtual_ || s.friend_ || s.explicit_ ||
        s.declares_class || s.aligned)
        return not_supported(n, "dcl.fct.def.default");
    qualified_type type = s.type;
    declarator_meaning m;
    function_cv = 0;
    in_member_declarator = true;
    function_parameters.clear();
    analysis_result r = declarator(n, d, type, m);
    in_member_declarator = false;
    if (r != analysis_result::well_formed)
        return r;
    if (!m.has_id || m.qualifier || function_cv || m.aligned ||
        !m.operator_function ||
        m.implemented != preprocessing_op_or_punc::EQUAL)
        return not_supported(n, "dcl.fct.def.default");
    for (const parameter_meaning &parameter : function_parameters)
        if (parameter.default_argument.index())
            return not_supported(n, "dcl.fct.def.default");
    const struct type &f = type_table[type.unqualified()];
    if (f.kind != type_kind::function || f.variadic || f.parameter_count != 1)
        return not_supported(n, "dcl.fct.def.default");
    const entity &x = entity_table[c];
    const struct type &result = type_table[f.operand.unqualified()];
    const struct type &first =
        type_table[type_table.parameters(type.unqualified())[0].unqualified()];
    if (f.operand.cv() || result.kind != type_kind::lvalue_reference ||
        !(result.operand ==
          qualified_type::make(x.type.unqualified(), cv_none)) ||
        first.kind != type_kind::lvalue_reference ||
        !(first.operand ==
          qualified_type::make(x.type.unqualified(), cv_const)))
        return not_supported(n, "dcl.fct.def.default");
    /* a second one, or beside a user-declared copy assignment operator */
    lookup_result found = lookup_qualified(
        c, operator_name(preprocessing_op_or_punc::EQUAL), lookup_filter::any);
    if (found.entity || found.unknown || found.ambiguous ||
        x.defaulted_copy_assignment)
        return not_supported(n, "class.mem");
    entity_table[c].defaulted_copy_assignment = true;
    return analysis_result::well_formed;
}

/* the expressions of an expression-list (an initializer_list node, or one
   expression), analysed; a braced-init-list or a pack expansion among them
   is not analysed yet */
analysis_result semantic_analysis::expression_list_meanings(
    syntax_child list, owned_array<expression_meaning> &out)
{
    owned_array<syntax_child> items;
    if (syntax_is(tree, list, syntax_kind::initializer_list)) {
        for (uint32_t i = 0; i < tree[list.index()].child_count; i++) {
            syntax_child a = tree.children(list.index())[i];
            if (syntax_is_punctuator(tokens, a,
                                     preprocessing_op_or_punc::COMMA))
                continue;
            items.append(a);
        }
    } else {
        items.append(list);
    }
    for (syntax_child a : items) {
        if (syntax_is(tree, a, syntax_kind::braced_init_list) ||
            syntax_is_punctuator(tokens, a, preprocessing_op_or_punc::ELLIPSIS))
            return not_supported(node_of(a), "over.match.list");
        expression_meaning e;
        analysis_result r = expression(a, e);
        if (r != analysis_result::well_formed)
            return r;
        out.append(e);
    }
    return analysis_result::well_formed;
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
analysis_result
semantic_analysis::construct(node_index at, qualified_type type,
                             owned_array<expression_meaning> &arguments,
                             uint32_t object, initialized_value &result,
                             bool copy)
{
    entity_index c = type_table[type.unqualified()].entity;
    const entity &x = entity_table[c];
    if (x.kind != entity_kind::class_type)
        return not_supported(at, "class.pre");
    /* [class.abstract]/2 (Note 3): "no objects of an abstract class can be
       created except as subobjects of a class derived from it" */
    if (abstract_class(type) && !constructing_base)
        return not_supported(at, "class.abstract");
    if (arguments.empty() && trivial_defaulted_default_constructor(c))
        /* value-initialization through a trivial X() = default
           ([dcl.init]/8.2: zero-initialized): not analysed */
        return not_supported(at, "dcl.init");
    /* [temp.mem]/1: a constructor template is a candidate too
       (member_template_declaration) */
    if (x.constructor_template)
        return not_supported(at, "temp.mem");
    /* [over.match.ctor]/1: "the candidate functions are all the
       constructors of the class of the object being initialized" */
    owned_array<overload_candidate> candidates;
    for (entity_index k = x.first_constructor; k;
         k = entity_table[k].next_constructor) {
        /* [over.match.ctor]/1: "For copy-initialization (including
           default initialization in the context of copy-initialization),
           the candidate functions are all the converting constructors
           ([class.conv.ctor]) of that class" */
        if (copy && entity_table[k].explicit_)
            continue;
        overload_candidate one;
        one.function = k;
        one.type = entity_table[k].type;
        candidates.append(one);
    }
    /* the implicitly-declared copy constructor X(const X&)
       ([class.copy.ctor]/6: "If the class definition does not explicitly
       declare a copy constructor, a non-explicit one is declared
       implicitly"), trivial for the members analysis has; a class that
       declares a move constructor has it deleted, and is not told apart */
    bool implicit_copy = true;
    for (entity_index k = x.first_constructor; k && implicit_copy;
         k = entity_table[k].next_constructor) {
        const struct type &f = type_table[entity_table[k].type.unqualified()];
        if (f.parameter_count != 1)
            continue;
        const struct type &p =
            type_table[type_table
                           .parameters(entity_table[k].type.unqualified())[0]
                           .unqualified()];
        if ((p.kind == type_kind::lvalue_reference ||
             p.kind == type_kind::rvalue_reference) &&
            p.operand.unqualified() == type.unqualified())
            implicit_copy = false;
    }
    if (implicit_copy) {
        qualified_type parameter = qualified_type::make(
            type_table.lvalue_reference_to(
                qualified_type::make(type.unqualified(), cv_const)),
            cv_none);
        overload_candidate copy;
        copy.type = qualified_type::make(
            type_table.function(
                qualified_type::make(
                    type_table.fundamental(fundamental_type::void_), cv_none),
                &parameter, 1, false),
            cv_none);
        candidates.append(copy);
    }
    size_t best = 0;
    analysis_result resolved =
        best_viable_function(at, candidates, nullptr, arguments, best);
    if (resolved != analysis_result::well_formed)
        return resolved;
    entity_index chosen = candidates[best].function;
    if (!chosen) {
        if (!copies_bytes(c))
            /* [class.copy.ctor]/14: the implicitly-defined copy
               constructor copies each base and member by its own */
            return not_supported(at, "class.copy.ctor");
        bool ambiguous = false;
        entity_index from = type_table[arguments[0].type.unqualified()].entity;
        if (from != c && base_step(from, c, ambiguous)) {
            /* its const X& bound to the base class subobject */
            analysis_result r = to_base(tree[at].first_token, c, arguments[0]);
            if (r != analysis_result::well_formed)
                return r;
        }
        result.evaluation = constness::unknown;
        result.value = 0;
        analysis_result r = operand_value(at, arguments[0]);
        if (r != analysis_result::well_formed)
            return r;
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
    analysis_result deleted = deleted_use(tree[at].first_token, chosen);
    if (deleted != analysis_result::well_formed)
        return deleted;
    size_t n = arguments.size();
    const qualified_type *p =
        type_table.parameters(entity_table[chosen].type.unqualified());
    owned_array<uint32_t> nodes;
    for (size_t i = 0; i < n; i++) {
        /* [over.match.ctor]: each parameter copy-initialized from its
           argument ([expr.call]/7) */
        initialized_value one;
        analysis_result r =
            initialize_with(at, arguments[i], p[i], false, false, one);
        if (r != analysis_result::well_formed)
            return r;
        nodes.append(one.node);
    }
    /* [dcl.fct.default]/1: the default arguments of the parameters left */
    size_t count =
        type_table[entity_table[chosen].type.unqualified()].parameter_count;
    for (size_t i = n; i < count; i++) {
        const entity &k = entity_table[chosen];
        if (k.unknown_defaults || !k.first_default ||
            !default_argument_nodes[k.first_default + i])
            return not_supported(at, "dcl.fct.default");
        nodes.append(default_argument_nodes[k.first_default + i]);
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
    result.node = expression_node(semantic_kind::construct, made,
                                  tree[at].first_token, object, chosen);
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
analysis_result semantic_analysis::ctor_initializer(syntax_child list,
                                                    uint32_t &built)
{
    entity_index c = current_class();
    token_index at = tree[current_declaration].first_token;
    owned_array<syntax_child> items;
    for (uint32_t i = 0; list.index() && i < tree[list.index()].child_count;
         i++) {
        syntax_child item = tree.children(list.index())[i];
        if (syntax_is(tree, item, syntax_kind::mem_initializer_list)) {
            for (uint32_t k = 0; k < tree[item.index()].child_count; k++) {
                syntax_child one = tree.children(item.index())[k];
                if (syntax_is(tree, one, syntax_kind::mem_initializer))
                    items.append(one);
                else if (!syntax_is_punctuator(tokens, one,
                                               preprocessing_op_or_punc::COMMA))
                    return not_supported(list.index(), "class.base.init");
            }
        } else if (syntax_is(tree, item, syntax_kind::mem_initializer)) {
            items.append(item);
        }
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
    for (syntax_child item : items) {
        node_index n = item.index();
        const syntax_node &node = tree[n];
        const syntax_child *children = tree.children(n);
        if (!node.child_count)
            return not_supported(n, "class.base.init");
        token_index id = 0;
        uint32_t name = 0;
        entity_index e = 0;
        /* a class-or-decltype with a nested-name-specifier: the class it
           names ([basic.lookup.qual]) */
        entity_index qualified_class = 0;
        if (syntax_is(tree, children[0], syntax_kind::class_or_decltype)) {
            analysis_result r =
                named_class(children[0], n, qualified_class, id);
            if (r != analysis_result::well_formed)
                return r;
            if (qualified_class == c)
                return not_supported(n, "class.base.init");
            name = tokens.identifier(id);
        } else {
            if (!children[0].is_token() ||
                tokens[children[0].index()].kind != token_kind::identifier)
                return not_supported(n, "class.base.init");
            id = children[0].index();
            name = tokens.identifier(id);
            if (name == entity_table[c].name)
                /* [class.base.init]/6: a delegating constructor */
                return not_supported(n, "class.base.init");
            e = entity_table.find(c, name);
        }
        if (!e) {
            /* [class.base.init]/3: "A mem-initializer-list can initialize
               a base class using any class-or-decltype that denotes that
               base class type" */
            entity_index base = qualified_class;
            if (!base)
                base = lookup(name);
            for (uint32_t i = 0; base && i < entity_table[c].base_count; i++) {
                entity_index b = class_bases[entity_table[c].first_base + i];
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
                return not_supported(n, "over");
            if (!e && (qualified_class || lookup(name)))
                /* a name of an enclosing scope */
                return not_supported(n, "class.base.init");
            return ill_formed_at_token(id,
                                       "unless the mem-initializer-id names "
                                       "the constructor's class, a "
                                       "non-static data member of the "
                                       "constructor's class, or a direct or "
                                       "virtual base of that class, the "
                                       "mem-initializer is ill-formed",
                                       "class.base.init");
        }
        if (entity_table[entity_table[e].enclosing].anonymous_object)
            /* [class.base.init]/2: a member of an anonymous union
               ([class.union.anon]), initialized in its unnamed object */
            return not_supported(n, "class.base.init");
        size_t slot = 0;
        while (subobjects[slot] != e)
            slot++;
        /* [class.base.init]/4: "if a mem-initializer-id designates the
           same member or base class more than once, the program is
           ill-formed" (clang's "multiple initializations") */
        if (initialized[slot])
            return ill_formed_at_token(id,
                                       "a mem-initializer-list shall not "
                                       "designate the same member more than "
                                       "once",
                                       "class.base.init");
        qualified_type type = entity_table[e].type;
        /* a copy: subobject and the initializer add types, which may move
           type_table's */
        const struct type record = type_table[type.unqualified()];
        if (record.kind == type_kind::array)
            return not_supported(n, "class.base.init");
        expression_meaning object;
        analysis_result r = subobject(id, e, object);
        if (r != analysis_result::well_formed)
            return r;
        if (record.kind == type_kind::class_type &&
            entity_table[record.entity].first_constructor) {
            /* [class.base.init]/7: direct-initialization: a constructor */
            owned_array<expression_meaning> arguments;
            if (node.child_count == 4)
                r = expression_list_meanings(children[2], arguments);
            else if (node.child_count != 3)
                /* a braced-init-list */
                return not_supported(n, "class.base.init");
            if (r != analysis_result::well_formed)
                return r;
            initialized_value value;
            constructing_base = entity_table[e].base_subobject;
            r = construct(n, type, arguments, object.node, value);
            constructing_base = false;
            if (r != analysis_result::well_formed)
                return r;
            if (semantics[value.node].kind != semantic_kind::construct)
                /* the implicit copy constructor */
                return not_supported(n, "class.copy.ctor");
            initialized[slot] = statement_node(
                semantic_kind::expression_statement, id, value.node);
            if (entity_table[e].virtual_base)
                initialized[slot] = complete_object(initialized[slot]);
            continue;
        }
        initialized_value value;
        if (node.child_count == 2 &&
            syntax_is(tree, children[1], syntax_kind::braced_init_list)) {
            r = list_initialization(children[1].index(), type, true, value);
        } else if (node.child_count == 3) {
            /* ( ): value-initialization ([dcl.init]/11): zero for the
               members analysis has */
            expression_meaning zero;
            zero.type = qualified_type::make(type.unqualified(), cv_none);
            zero.evaluation = constness::constant;
            value.node =
                expression_node(semantic_kind::zero, zero, first_token(item));
        } else if (node.child_count == 4) {
            syntax_child init = children[2];
            if (syntax_is(tree, init, syntax_kind::initializer_list)) {
                if (has_pack_expansion(init))
                    return not_supported(n, "temp.variadic");
                if (record.kind != type_kind::class_type)
                    return ill_formed(n,
                                      "if the entity being initialized does "
                                      "not have class type, the "
                                      "expression-list in a parenthesized "
                                      "initializer shall be a single "
                                      "expression",
                                      "dcl.init");
                return not_supported(n, "dcl.init");
            }
            if (entity_table[e].base_subobject)
                /* a base copied from an expression: [conv.ptr]/3's
                   conversion of the source */
                return not_supported(n, "class.base.init");
            r = initialize_from(n, init, type, true, false, value);
        } else {
            return not_supported(n, "class.base.init");
        }
        if (r != analysis_result::well_formed)
            return r;
        /* the member's initialization: its bytes, stored as by an
           assignment to (*this).member */
        initialized[slot] = subobject_store(object, type, id, value.node);
    }
    /* [class.base.init]/9: "In a non-delegating constructor, if a given
       potentially constructed subobject is not designated by a
       mem-initializer-id ... (9.3) otherwise, the entity is
       default-initialized ([dcl.init])": [dcl.init]/7.1, a class's default
       constructor */
    for (size_t i = 0; i < subobjects.size(); i++) {
        if (initialized[i])
            continue;
        const dynamic_class *dynamic = dynamic_of(c);
        if (entity_table[subobjects[i]].virtual_base && dynamic &&
            dynamic->abstract)
            /* [class.base.init]/9 (Note): "An abstract class is never a
               most derived class, thus its constructors never initialize
               virtual base classes" */
            continue;
        if (entity_table[subobjects[i]].default_initializer) {
            /* (9.1) "if the entity is a non-static data member that has a
               default member initializer ([class.mem]) and either ... the
               constructor's class is not a union ... the entity is
               initialized from its default member initializer as
               specified in [dcl.init]" */
            analysis_result r =
                default_member_initializer(at, subobjects[i], initialized[i]);
            if (r != analysis_result::well_formed)
                return r;
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
        if (record.kind == type_kind::class_type &&
            entity_table[record.entity].kind == entity_kind::class_type &&
            default_constructor_deleted(record.entity))
            /* its implicit default constructor is deleted
               ([class.default.ctor]/2) */
            return not_supported(current_declaration, "class.default.ctor");
        if (record.kind != type_kind::class_type ||
            !entity_table[record.entity].first_constructor)
            continue;
        expression_meaning object;
        analysis_result r = subobject(at, subobjects[i], object);
        if (r != analysis_result::well_formed)
            return r;
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
    analysis_result r = this_object(at, m);
    if (r != analysis_result::well_formed)
        return r;
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
                                            uint32_t value)
{
    expression_meaning stored = object;
    stored.type = qualified_type::make(type.unqualified(), cv_none);
    stored.evaluation = constness::not_constant;
    stored.entity = 0;
    uint32_t assigned = expression_node(
        semantic_kind::assignment, stored, at, object.node, value,
        static_cast<uint8_t>(preprocessing_op_or_punc::EQUAL));
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

/* whether the implicit default constructor of class c default-constructs a
   subobject of class k: a base, or a member (or array of them) without a
   default member initializer ([class.base.init]/9.1) */
bool semantic_analysis::default_constructed(entity_index c,
                                            entity_index k) const
{
    const entity &x = entity_table[c];
    for (uint32_t i = 0; i < x.base_count; i++)
        if (type_table[entity_table[class_bases[x.first_base + i]]
                           .type.unqualified()]
                .entity == k)
            return true;
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = entity_table[class_members[x.first_member + i]];
        if (m.base_subobject || m.default_initializer)
            continue;
        if (element_class(m.type) == k)
            return true;
    }
    return false;
}

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
                                                            node_index n)
{
    const entity &x = entity_table[c];
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = entity_table[class_members[x.first_member + i]];
        qualified_type t = m.type;
        if (type_table[t.unqualified()].kind != type_kind::array)
            continue;
        while (type_table[t.unqualified()].kind == type_kind::array)
            t = type_table[t.unqualified()].operand;
        const struct type &r = type_table[t.unqualified()];
        if (r.kind == type_kind::class_type &&
            (entity_table[r.entity].first_constructor || destroys(r.entity)))
            return not_supported(n, "class.ctor");
    }
    owned_array<entity_index> classes;
    subobject_classes(c, classes);
    /* [class.copy.assign]/12 before C++11: the implicit copy assignment
       operator that calls a subobject's user-declared one (clang defines
       it, and reports what its call breaks, at the class) */
    if (!tokens.files.behavior.has(language_feature::rvalue_references))
        for (entity_index k : classes)
            if (!assigned_bitwise(k))
                return not_supported(n, "class.copy.assign");
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
        if (entity_table[k].first_constructor)
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
    bool declares_copy = false;
    for (entity_index q = x.first_constructor; q;
         q = entity_table[q].next_constructor)
        if (is_copy_constructor(c, q))
            declares_copy = true;
    for (entity_index k : classes) {
        const entity &sub = entity_table[k];
        if (declares_move_constructor(k))
            /* [class.copy.ctor]/8: the implicit move constructor, which
               moves the subobject, is not declared yet */
            return not_supported(n, "class.copy.ctor");
        bool default_ok =
                 !sub.first_constructor && !sub.implicit_default_deleted,
             copy_ok = copies_bytes(k);
        for (entity_index q = sub.first_constructor; q;
             q = entity_table[q].next_constructor) {
            /* [class.access]/1: c a friend of k may call it, where an
               aggregate initialization may not (not analysed) */
            if (entity_table[q].access && grants(k, c))
                return not_supported(n, "class.friend");
            if (entity_table[q].access || entity_table[q].deleted)
                continue;
            if (!type_table[entity_table[q].type.unqualified()].parameter_count)
                default_ok = true;
            if (is_copy_constructor(k, q))
                copy_ok = true;
        }
        if (construction && !default_ok && !x.first_constructor &&
            default_constructed(c, k)) {
            /* C++03 has no deleted functions: the implicit default
               constructor's definition is ill-formed, reported at the
               class */
            if (!tokens.files.behavior.has(language_feature::rvalue_references))
                return not_supported(n, "class.default.ctor");
            /* [class.default.ctor]/2: the implicit default constructor is
               deleted, an error only where it is used */
            entity_table[c].implicit_default_deleted = true;
        }
        if ((!copy_ok && !declares_copy) ||
            /* [class.copy.ctor]/10.2, [class.default.ctor]/2.7: an
               implicit constructor beside a subobject's inaccessible
               destructor is deleted */
            (sub.destructor && entity_table[sub.destructor].access &&
             (!x.destructor || !declares_copy || !x.first_constructor)))
            return not_supported(n, "class.default.ctor");
    }
    /* [temp.mem]/1: a constructor template is user-declared; the copy
       constructor declared beside it makes every construction of the
       class one construct refuses */
    bool user_constructors =
        x.first_constructor != 0 || x.constructor_template != 0;
    if (x.constructor_template) {
        construction = true;
        copy = true;
    }
    /* [dcl.fct.def.default]: X() = default beside other constructors: the
       default constructor, trivial or not, that a default-initialization
       chooses */
    if (x.defaulted_default_constructor && user_constructors && construction)
        declare_implicit(c, special_member::constructor, false);
    bool user_copy = false;
    /* entity_table[c], not x: declare_implicit may have moved it */
    for (entity_index k = entity_table[c].first_constructor; k;
         k = entity_table[k].next_constructor)
        if (is_copy_constructor(c, k))
            user_copy = true;
    if (copy && !user_copy)
        declare_implicit(c, special_member::constructor, true);
    if (!user_constructors && !entity_table[c].implicit_default_deleted &&
        (construction || entity_table[c].first_constructor))
        declare_implicit(c, special_member::constructor, false);
    if (destruction && !entity_table[c].destructor)
        declare_implicit(c, special_member::destructor, false);
    return analysis_result::well_formed;
}

/*
 * Whether the default constructor of class c is implicitly declared or
 * defaulted ([dcl.fct.def.default]) and defined as deleted:
 * [class.default.ctor]/2: "A defaulted default constructor for class X is
 * defined as deleted if: ... any non-variant non-static data member with no
 * default member initializer ([class.mem]) is of reference type, any
 * non-variant non-static data member of const-qualified type (or array
 * thereof) with no brace-or-equal-initializer is not const-default-
 * constructible ([dcl.init]), ... any potentially constructed subobject ...
 * has class type M (or array thereof) and ... M has no default constructor
 * or overload resolution ... results in ... a function that is deleted".
 * A const member of class type and unions are not analysed (false).
 */
bool semantic_analysis::default_constructor_deleted(entity_index c) const
{
    const entity &x = entity_table[c];
    if (x.implicit_default_deleted)
        return true;
    if (x.union_)
        return false;
    /* a user-declared constructor and no X() = default: no implicit
       default constructor ([class.default.ctor]/1) */
    if (!x.defaulted_default_constructor)
        for (entity_index k = x.first_constructor; k;
             k = entity_table[k].next_constructor)
            if (!entity_table[k].implicit)
                return false;
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = entity_table[class_members[x.first_member + i]];
        if (m.kind != entity_kind::variable || !m.nonstatic_member ||
            m.default_initializer || (m.bit_field && !m.name))
            continue;
        qualified_type t = m.type;
        type_kind k = type_table[t.unqualified()].kind;
        if (k == type_kind::lvalue_reference ||
            k == type_kind::rvalue_reference)
            return true;
        while (type_table[t.unqualified()].kind == type_kind::array)
            t = type_table[t.unqualified()].operand;
        if ((cv_of(type_table, m.type) & cv_const) &&
            type_table[t.unqualified()].kind != type_kind::class_type)
            return true;
    }
    owned_array<entity_index> classes;
    subobject_classes(c, classes);
    for (entity_index k : classes)
        if (entity_table[k].kind == entity_kind::class_type &&
            default_constructor_deleted(k))
            return true;
    return false;
}

/* [class.default.ctor]/3: X() = default beside other constructors, trivial
   (implicit_special_members declares no entity for it): default-initialization
   calls nothing */
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

/* an implicitly-declared constructor (the copy constructor X(const X&)
   when copy) or destructor of class c, public and inline */
void semantic_analysis::declare_implicit(entity_index c, special_member kind,
                                         bool copy)
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
           const volatile M&" (the other form is not analysed) */
        parameter = qualified_type::make(
            type_table.lvalue_reference_to(qualified_type::make(
                entity_table[c].type.unqualified(), cv_const)),
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
analysis_result semantic_analysis::implicit_definitions(entity_index c,
                                                        node_index n)
{
    owned_array<entity_index> implicit;
    for (entity_index k = entity_table[c].first_constructor; k;
         k = entity_table[k].next_constructor)
        if (entity_table[k].implicit)
            implicit.append(k);
    if (entity_table[c].destructor &&
        entity_table[entity_table[c].destructor].implicit)
        implicit.append(entity_table[c].destructor);
    current_declaration = n;
    token_index at = tree[n].first_token;
    for (entity_index f : implicit) {
        synthesized_function sf;
        begin_synthesized(
            sf, f,
            qualified_type::make(
                type_table.fundamental(fundamental_type::void_), cv_none),
            qualified_type::make(
                type_table.pointer_to(qualified_type::make(
                    entity_table[c].type.unqualified(), cv_none)),
                cv_none));
        const struct type &ft = type_table[entity_table[f].type.unqualified()];
        entity_index source = 0;
        if (ft.parameter_count)
            source = synthesized_variable(
                sf,
                type_table.parameters(entity_table[f].type.unqualified())[0],
                true);
        analysis_result r = analysis_result::well_formed;
        uint32_t members = 0, destructions = 0;
        if (entity_table[f].special == special_member::destructor) {
            /* [class.cdtor]/4: the destructor's virtual table pointer */
            members = vtable_pointer_store(c, at);
            destructions = subobject_destructions(c, at);
        } else if (source)
            r = implicit_copy(c, n, source, members);
        else
            r = ctor_initializer(syntax_child(), members);
        finish_synthesized(sf, n, at, statement_node(semantic_kind::block, at),
                           members, destructions);
        if (r == analysis_result::ill_formed)
            /* [class.default.ctor]/2, [class.copy.ctor]/10: a defaulted
               special member that would be ill-formed is defined as
               deleted (an error only where it is used): not analysed */
            return not_supported(n, "class.default.ctor");
        if (r != analysis_result::well_formed)
            return r;
    }
    return analysis_result::well_formed;
}

/* [class.copy.ctor]/14: each base and member of *this direct-initialized
   from the corresponding one of the source (the parameter, a const X&) */
analysis_result semantic_analysis::implicit_copy(entity_index c, node_index n,
                                                 entity_index source,
                                                 uint32_t &built)
{
    token_index at = tree[n].first_token;
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
            r = member_access(at, e, from);
        if (r != analysis_result::well_formed)
            return r;
        expression_meaning object;
        r = subobject(at, e, object);
        if (r != analysis_result::well_formed)
            return r;
        qualified_type type = entity_table[e].type;
        const struct type &record = type_table[type.unqualified()];
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
                return not_supported(n, "class.copy.ctor");
            if (r != analysis_result::well_formed)
                return r;
            statements.append(statement_node(
                semantic_kind::expression_statement, at, value.node));
            if (entity_table[e].virtual_base)
                statements.back() = complete_object(statements.back());
            continue;
        }
        if (record.kind == type_kind::lvalue_reference ||
            record.kind == type_kind::rvalue_reference)
            return not_supported(n, "class.copy.ctor");
        if (record.kind == type_kind::class_type &&
            entity_table[e].field == UINT32_MAX &&
            (!entity_table[e].virtual_base ||
             !entity_table[record.entity].member_count))
            /* an empty base: no bytes */
            continue;
        r = operand_value(n, from);
        if (r != analysis_result::well_formed)
            return r;
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
        const struct type &r =
            type_table[entity_table[order[i]].type.unqualified()];
        if (r.kind != type_kind::class_type ||
            !entity_table[r.entity].destructor)
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
analysis_result semantic_analysis::special_member_body(node_index definition,
                                                       syntax_child body)
{
    const syntax_node &b = tree[body.index()];
    const syntax_child *parts = tree.children(body.index());
    syntax_child init;
    syntax_child compound;
    if (b.child_count == 1 &&
        syntax_is(tree, parts[0], syntax_kind::compound_statement)) {
        compound = parts[0];
    } else if (b.child_count == 2 &&
               syntax_is(tree, parts[0], syntax_kind::ctor_initializer) &&
               syntax_is(tree, parts[1], syntax_kind::compound_statement)) {
        init = parts[0];
        compound = parts[1];
        if (entity_table[last_declared].special != special_member::constructor)
            return ill_formed(init.index(),
                              "a ctor-initializer shall appear only in the "
                              "definition of a constructor",
                              "class.base.init");
    } else {
        return not_supported(definition, "dcl.fct.def.general");
    }
    return function_body(definition, compound, init);
}

/*
 * [class.mfct]/2: "A member function definition that appears outside of
 * the class definition shall appear in a namespace scope enclosing the
 * class definition."  [dcl.meaning]/1: the qualified declarator-id refers
 * to a constructor or the destructor declared in the class.
 */
analysis_result semantic_analysis::special_member_definition(node_index n,
                                                             uint32_t at)
{
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    /* a decl-specifier-seq before the declarator holds constexpr alone
       (function_specifiers_only) */
    bool constexpr_specified = at == 1;
    if (node.child_count != at + 2 ||
        !syntax_is(tree, children[at + 1], syntax_kind::function_body) ||
        at_block_scope() || function_state.active)
        return not_supported(n, "dcl.fct.def.general");
    qualified_type type = qualified_type::make(
        type_table.fundamental(fundamental_type::void_), cv_none);
    declarator_meaning m;
    function_cv = 0;
    function_parameters.clear();
    analysis_result r = declarator(n, children[at], type, m);
    in_member_declarator = false;
    if (r != analysis_result::well_formed)
        return r;
    /* [dcl.fct.default]/6: default arguments added to the class's
       declaration's, not analysed */
    for (const parameter_meaning &p : function_parameters)
        if (p.default_argument.index())
            return not_supported(n, "dcl.fct.default");
    entity_index c = m.qualifier;
    if (!c || !m.has_id || m.aligned || (m.gnu_flags & (gnu_packed | gnu_used)))
        return not_supported(n, "dcl.fct.def.general");
    if (tokens.identifier(m.id) != entity_table[c].name)
        /* a conversion function, or ~ with another name */
        return not_supported(n, "class.dtor");
    entity_index e = 0;
    if (m.destructor) {
        e = entity_table[c].destructor;
    } else {
        for (entity_index k = entity_table[c].first_constructor; k;
             k = entity_table[k].next_constructor)
            if (entity_table[k].type == type)
                e = k;
    }
    if (!e || function_cv)
        return ill_formed_at_token(m.id,
                                   "when the declarator-id is qualified, the "
                                   "declaration shall refer to a previously "
                                   "declared member of the class",
                                   "dcl.meaning");
    /* [dcl.constexpr]/1: "If any declaration of a function or function
       template has a constexpr specifier, then all its declarations shall
       contain the constexpr specifier." */
    if (entity_table[e].constexpr_ != constexpr_specified)
        return not_supported(n, "dcl.constexpr");
    /* [basic.def.odr]/1 */
    if (entity_table[e].defined)
        return ill_formed_at_token(m.id,
                                   "no translation unit shall contain more "
                                   "than one definition of any function",
                                   "basic.def.odr");
    entity_table[e].defined = true;
    entity_table.add_declaration(e, n);
    last_declared = e;
    return special_member_body(n, children[at + 1]);
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
analysis_result semantic_analysis::class_conversion(node_index n,
                                                    entity_index c,
                                                    expression_meaning &m)
{
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    qualified_type type = entity_table[c].type;
    if (!entity_table[c].size)
        return not_supported(n, "expr.type.conv");
    owned_array<expression_meaning> arguments;
    initialized_value value;
    analysis_result r = analysis_result::well_formed;
    if (syntax_is(tree, children[1], syntax_kind::braced_init_list)) {
        if (node.child_count != 2)
            return not_supported(n, "expr.type.conv");
        r = list_initialization(children[1].index(), type, true, value);
    } else {
        if (node.child_count == 4) {
            r = expression_list_meanings(children[2], arguments);
            if (r != analysis_result::well_formed)
                return r;
        } else if (node.child_count != 3) {
            return not_supported(n, "expr.type.conv");
        }
        return class_direct_initialization(n, c, arguments, m);
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
    node_index n, entity_index c, owned_array<expression_meaning> &arguments,
    expression_meaning &m)
{
    qualified_type type = entity_table[c].type;
    if (!entity_table[c].size)
        return not_supported(n, "expr.type.conv");
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
                return not_supported(n, "dcl.init");
            return ill_formed(n,
                              "value-initialization calls the default "
                              "constructor, which is defined as deleted",
                              "dcl.init");
        }
        owned_array<syntax_child> none;
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
            return not_supported(n, "expr.type.conv");
        r = initialize_with(n, arguments[0], type, true, false, value);
    } else if (is_aggregate(c) &&
               (arguments.size() != 1 ||
                arguments[0].type.unqualified() != type.unqualified())) {
        /* an aggregate's implicit constructors (a default member
           initializer makes its default constructor non-trivial) do
           not take these arguments: C++20's parenthesized aggregate
           initialization ([dcl.init]/17) is not analysed */
        return not_supported(n, "expr.type.conv");
    } else {
        r = construct(n, type, arguments, 0, value);
    }
    if (r == analysis_result::well_formed)
        class_prvalue(type, value.evaluation, value.node, m);
    return r;
}

} // namespace cplusplus
