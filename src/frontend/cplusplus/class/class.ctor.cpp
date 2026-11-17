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
 * ([class.base.init], [class.dtor]/13); no explicit, defaulted, deleted, constexpr or virtual ones, no
 * default arguments, no delegating constructor.
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
bool semantic_analysis::copies_bytes(entity_index c) const
{
    const entity &x = entity_table[c];
    for (entity_index k = x.first_constructor; k;
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
            p.operand.unqualified() == x.type.unqualified())
            return false;
    }
    owned_array<entity_index> classes;
    subobject_classes(c, classes);
    for (entity_index k : classes)
        if (!copies_bytes(k))
            return false;
    return true;
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
    bool definition)
{
    qualified_type type = qualified_type::make(
        type_table.fundamental(fundamental_type::void_), cv_none);
    declarator_meaning m;
    function_cv = 0;
    in_member_declarator = true;
    analysis_result r = declarator(declaration, d, type, m);
    in_member_declarator = false;
    if (r != analysis_result::well_formed)
        return r;
    if (!m.has_id || m.qualifier || m.aligned)
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
            const struct type &p = type_table[parameters[i].unqualified()];
            if (p.kind == type_kind::rvalue_reference)
                /* a move constructor: overload resolution with rvalues */
                return not_supported(declaration, "class.copy.ctor");
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
    entity_index e =
        entity_table.make(entity_kind::function, entity_table[c].name, c);
    entity &x = entity_table[e];
    x.type = type;
    x.nonstatic_member = true;
    x.special = kind;
    /* [class.mfct]/1: defined in its class, an inline member function */
    x.inline_ = definition;
    x.defined = definition;
    x.access = access;
    x.linkage_of_name = entity_table[c].linkage_of_name;
    entity_table.add_declaration(e, declaration);
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

namespace
{

/* [over.ics.rank] for what analysis compares: 0 an identity conversion
   (the argument's type is the parameter's, or what its reference refers
   to), 1 a standard conversion between arithmetic types or to a pointer,
   -1 none analysis knows */
int conversion_rank(const types &table, const expression_meaning &argument,
                    qualified_type parameter)
{
    const struct type *p = &table[parameter.unqualified()];
    if (p->kind == type_kind::lvalue_reference ||
        p->kind == type_kind::rvalue_reference) {
        qualified_type referred = p->operand;
        if (referred.unqualified() != argument.type.unqualified())
            return -1;
        /* [dcl.init.ref]/5: a non-const lvalue reference binds to an
           lvalue only, and only as cv-qualified as it is */
        if (!(referred.cv() & cv_const) &&
            (argument.category != value_category::lvalue ||
             (argument.type.cv() & ~referred.cv())))
            return -1;
        return 0;
    }
    if (parameter.unqualified() == argument.type.unqualified())
        return 0;
    const struct type &a = table[argument.type.unqualified()];
    bool arithmetic_p = p->kind == type_kind::fundamental &&
                        p->fundamental != fundamental_type::void_ &&
                        p->fundamental != fundamental_type::std_nullptr_t;
    bool arithmetic_a = a.kind == type_kind::fundamental &&
                        a.fundamental != fundamental_type::void_ &&
                        a.fundamental != fundamental_type::std_nullptr_t;
    if (arithmetic_p && arithmetic_a)
        return 1;
    if (p->kind == type_kind::pointer &&
        (a.kind == type_kind::array || a.kind == type_kind::pointer ||
         (a.kind == type_kind::fundamental &&
          a.fundamental == fundamental_type::std_nullptr_t)))
        return 1;
    return -1;
}

} // namespace

/* whether an argument of a derived class (or a pointer to one) converts
   to parameter, a reference to (or a pointer to) a base of it
   ([dcl.init.ref]/5, [conv.ptr]/3) */
bool semantic_analysis::derived_to_base_conversion(
    const expression_meaning &argument, qualified_type parameter) const
{
    const struct type &p = type_table[parameter.unqualified()];
    qualified_type from = argument.type;
    qualified_type to = p.operand;
    if (p.kind == type_kind::pointer) {
        if (type_table[from.unqualified()].kind != type_kind::pointer)
            return false;
        from = type_table[from.unqualified()].operand;
    } else if (p.kind == type_kind::lvalue_reference) {
        if (!(to.cv() & cv_const) &&
            argument.category != value_category::lvalue)
            return false;
    } else if (p.kind != type_kind::rvalue_reference) {
        return false;
    }
    if (from.cv() & ~to.cv())
        return false;
    const struct type &a = type_table[from.unqualified()];
    const struct type &b = type_table[to.unqualified()];
    if (a.kind != type_kind::class_type || b.kind != type_kind::class_type ||
        entity_table[a.entity].kind != entity_kind::class_type)
        return false;
    bool ambiguous = false;
    return base_step(a.entity, b.entity, ambiguous) != 0;
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
 * Overload resolution of [over.match.best] for the conversions
 * conversion_rank tells apart: a candidate is better when none of its
 * conversions is worse and one is better; no unique best one is not
 * analysed.  The implicitly-declared copy constructor ([class.copy.ctor]/6)
 * of a class that declares none copies the object's bytes (trivial_class).
 */
analysis_result
semantic_analysis::construct(node_index at, qualified_type type,
                             owned_array<expression_meaning> &arguments,
                             uint32_t object, initialized_value &result)
{
    entity_index c = type_table[type.unqualified()].entity;
    const entity &x = entity_table[c];
    if (x.kind != entity_kind::class_type)
        return not_supported(at, "class.pre");
    owned_array<entity_index> viable;
    owned_array<int> ranks;
    for (entity_index k = x.first_constructor; k;
         k = entity_table[k].next_constructor) {
        const struct type &f = type_table[entity_table[k].type.unqualified()];
        if (f.parameter_count != arguments.size())
            continue;
        const qualified_type *p =
            type_table.parameters(entity_table[k].type.unqualified());
        bool ok = true;
        size_t first = ranks.size();
        for (size_t i = 0; i < arguments.size() && ok; i++) {
            int rank = conversion_rank(type_table, arguments[i], p[i]);
            if (rank < 0 && derived_to_base_conversion(arguments[i], p[i]))
                /* [over.best.ics]/6: "the conversion of a derived class
                   to a base class ... is a derived-to-base Conversion";
                   [over.ics.rank]/3.2: of Conversion rank */
                rank = 1;
            if (rank < 0)
                ok = false;
            ranks.append(rank);
        }
        if (!ok) {
            ranks.resize(first);
            continue;
        }
        viable.append(k);
    }
    /* the implicitly-declared copy constructor X(const X&)
       ([class.copy.ctor]/6: "If the class definition does not explicitly
       declare a copy constructor, a non-explicit one is declared
       implicitly"), trivial for the members analysis has */
    bool implicit_copy = arguments.size() == 1 &&
                         arguments[0].type.unqualified() == type.unqualified();
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
        if (!viable.empty())
            return not_supported(at, "over.match.best");
        if (!copies_bytes(c))
            /* [class.copy.ctor]/14: the implicitly-defined copy
               constructor copies each base and member by its own */
            return not_supported(at, "class.copy.ctor");
        result.evaluation = constness::unknown;
        result.value = 0;
        analysis_result r = operand_value(at, arguments[0]);
        if (r != analysis_result::well_formed)
            return r;
        result.node = arguments[0].node;
        return analysis_result::well_formed;
    }
    if (viable.empty()) {
        /* no constructor whose parameters analysis can match: either none
           is viable ([over.match.viable], ill-formed) or a conversion
           analysis does not know is */
        bool arity = false;
        for (entity_index k = x.first_constructor; k;
             k = entity_table[k].next_constructor)
            if (type_table[entity_table[k].type.unqualified()]
                    .parameter_count == arguments.size())
                arity = true;
        if (!arity && x.first_constructor)
            return ill_formed(at,
                              "to be a viable function, a candidate function "
                              "shall have enough parameters to agree in "
                              "number with the arguments in the list",
                              "over.match.viable");
        return not_supported(at, "over.match");
    }
    size_t n = arguments.size();
    size_t best = 0;
    bool unique = true;
    for (size_t i = 1; i < viable.size(); i++) {
        bool better = false, worse_ = false;
        for (size_t k = 0; k < n; k++) {
            if (ranks[i * n + k] < ranks[best * n + k])
                better = true;
            if (ranks[i * n + k] > ranks[best * n + k])
                worse_ = true;
        }
        if (better && !worse_) {
            best = i;
            unique = true;
        } else if (better == worse_) {
            unique = false;
        }
    }
    if (!unique)
        return not_supported(at, "over.match.best");
    entity_index chosen = viable[best];
    if (!accessible(chosen))
        return ill_formed(at,
                          "a private or protected member's name can be used "
                          "only by members and friends of the class in which "
                          "it is declared",
                          "class.access");
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
    expression_meaning made;
    made.type = qualified_type::make(type.unqualified(), cv_none);
    made.category = value_category::prvalue;
    made.evaluation = constness::not_constant;
    result.evaluation = constness::not_constant;
    result.value = 0;
    result.node = expression_node(semantic_kind::construct, made,
                                  tree[at].first_token, object, chosen);
    if (!nodes.empty()) {
        semantics[result.node].first = semantics.list(&nodes[0], nodes.size());
        semantics[result.node].count = static_cast<uint32_t>(nodes.size());
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
    {
        const entity &x = entity_table[c];
        for (uint32_t i = 0; i < x.base_count; i++)
            subobjects.append(class_bases[x.first_base + i]);
        for (uint32_t i = 0; i < x.member_count; i++)
            if (!entity_table[class_members[x.first_member + i]].base_subobject)
                subobjects.append(class_members[x.first_member + i]);
    }
    owned_array<uint32_t> initialized;
    initialized.resize(subobjects.size());
    for (uint32_t &i : initialized)
        i = 0;
    for (syntax_child item : items) {
        node_index n = item.index();
        const syntax_node &node = tree[n];
        const syntax_child *children = tree.children(n);
        if (!node.child_count || !children[0].is_token() ||
            tokens[children[0].index()].kind != token_kind::identifier)
            return not_supported(n, "class.base.init");
        token_index id = children[0].index();
        uint32_t name = tokens.identifier(id);
        if (name == entity_table[c].name)
            /* [class.base.init]/6: a delegating constructor */
            return not_supported(n, "class.base.init");
        entity_index e = entity_table.find(c, name);
        if (!e) {
            /* [class.base.init]/3: "A mem-initializer-list can initialize
               a base class using any class-or-decltype that denotes that
               base class type" */
            entity_index base = lookup(name);
            for (uint32_t i = 0; base && i < entity_table[c].base_count; i++) {
                entity_index b = class_bases[entity_table[c].first_base + i];
                if (entity_table[b].type.unqualified() ==
                    entity_table[base].type.unqualified())
                    e = b;
            }
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
            if (!e && lookup(name))
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
        const struct type &record = type_table[type.unqualified()];
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
            r = construct(n, type, arguments, object.node, value);
            if (r != analysis_result::well_formed)
                return r;
            if (semantics[value.node].kind != semantic_kind::construct)
                /* the implicit copy constructor */
                return not_supported(n, "class.copy.ctor");
            initialized[slot] = statement_node(
                semantic_kind::expression_statement, id, value.node);
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
        expression_meaning stored = object;
        stored.type = qualified_type::make(type.unqualified(), cv_none);
        stored.evaluation = constness::not_constant;
        stored.entity = 0;
        uint32_t assigned = expression_node(
            semantic_kind::assignment, stored, id, object.node, value.node,
            static_cast<uint8_t>(preprocessing_op_or_punc::EQUAL));
        initialized[slot] =
            statement_node(semantic_kind::expression_statement, id, assigned);
    }
    /* [class.base.init]/9: "In a non-delegating constructor, if a given
       potentially constructed subobject is not designated by a
       mem-initializer-id ... (9.3) otherwise, the entity is
       default-initialized ([dcl.init])": [dcl.init]/7.1, a class's default
       constructor */
    for (size_t i = 0; i < subobjects.size(); i++) {
        if (initialized[i])
            continue;
        const struct type &record =
            type_table[entity_table[subobjects[i]].type.unqualified()];
        if (record.kind != type_kind::class_type ||
            !entity_table[record.entity].first_constructor)
            continue;
        expression_meaning object;
        analysis_result r = subobject(at, subobjects[i], object);
        if (r != analysis_result::well_formed)
            return r;
        owned_array<expression_meaning> none;
        initialized_value value;
        r = construct(current_declaration, entity_table[subobjects[i]].type,
                      none, object.node, value);
        if (r != analysis_result::well_formed)
            return r;
        initialized[i] =
            statement_node(semantic_kind::expression_statement, at, value.node);
    }
    owned_array<uint32_t> statements;
    for (uint32_t s : initialized)
        if (s)
            statements.append(s);
    built = statement_node(semantic_kind::block, at);
    if (!statements.empty()) {
        semantics[built].first =
            semantics.list(&statements[0], statements.size());
        semantics[built].count = static_cast<uint32_t>(statements.size());
    }
    return analysis_result::well_formed;
}

/* the subobject e (a base or a non-static data member) of *this, an lvalue
   ([expr.prim.this], [expr.unary.op]/1, [expr.ref]/6.2) */
analysis_result semantic_analysis::subobject(token_index at, entity_index e,
                                             expression_meaning &m)
{
    if (!entity_table[e].base_subobject)
        return implicit_member_access(at, e, m);
    analysis_result r = this_pointer(at, m);
    if (r != analysis_result::well_formed)
        return r;
    m.type = type_table[m.type.unqualified()].operand;
    m.category = value_category::lvalue;
    m.evaluation = constness::not_constant;
    m.node = expression_node(semantic_kind::indirection, m, at, m.node);
    return to_base(at, type_table[entity_table[e].type.unqualified()].entity,
                   m);
}

/* the classes of the subobjects of class c (its bases' and its members',
   arrays' elements), in the order of their construction */
void semantic_analysis::subobject_classes(entity_index c,
                                          owned_array<entity_index> &out) const
{
    const entity &x = entity_table[c];
    for (uint32_t i = 0; i < x.base_count; i++)
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
 * [class.default.ctor]/4: "an implicitly-defined default constructor
 * performs the set of initializations of the class that would be performed
 * by a user-written default constructor for that class with no
 * ctor-initializer and an empty compound-statement"; [class.dtor]/11 an
 * implicitly-defined destructor, /13 "the destructor for class X calls the
 * destructors for X's direct non-variant non-static data members, ... the
 * destructors for X's non-virtual direct base classes".  Not analysed: a
 * class without constructors whose subobjects have some, without a
 * destructor whose subobjects have one, and arrays of classes with either.
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
    for (entity_index k : classes) {
        if (entity_table[k].first_constructor && !x.first_constructor)
            return not_supported(n, "class.default.ctor");
        if (destroys(k) && !x.destructor)
            return not_supported(n, "class.dtor");
    }
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
    const entity &x = entity_table[c];
    owned_array<entity_index> order;
    for (uint32_t i = 0; i < x.base_count; i++)
        order.append(class_bases[x.first_base + i]);
    for (uint32_t i = 0; i < x.member_count; i++)
        if (!entity_table[class_members[x.first_member + i]].base_subobject)
            order.append(class_members[x.first_member + i]);
    owned_array<uint32_t> statements;
    for (size_t i = order.size(); i-- > 0;) {
        const struct type &r =
            type_table[entity_table[order[i]].type.unqualified()];
        if (r.kind != type_kind::class_type ||
            !entity_table[r.entity].destructor)
            continue;
        expression_meaning object;
        if (subobject(at, order[i], object) != analysis_result::well_formed)
            return 0;
        expression_meaning call;
        call.type = qualified_type::make(
            type_table.fundamental(fundamental_type::void_), cv_none);
        call.category = value_category::prvalue;
        call.evaluation = constness::not_constant;
        uint32_t destroyed =
            expression_node(semantic_kind::destroy, call, at, object.node,
                            entity_table[r.entity].destructor);
        statements.append(
            statement_node(semantic_kind::expression_statement, at, destroyed));
    }
    if (statements.empty())
        return 0;
    uint32_t built = statement_node(semantic_kind::block, at);
    semantics[built].first = semantics.list(&statements[0], statements.size());
    semantics[built].count = static_cast<uint32_t>(statements.size());
    return built;
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
analysis_result semantic_analysis::special_member_definition(node_index n)
{
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    if (node.child_count != 2 ||
        !syntax_is(tree, children[1], syntax_kind::function_body) ||
        current_scope || function_state.active)
        return not_supported(n, "dcl.fct.def.general");
    qualified_type type = qualified_type::make(
        type_table.fundamental(fundamental_type::void_), cv_none);
    declarator_meaning m;
    function_cv = 0;
    analysis_result r = declarator(n, children[0], type, m);
    in_member_declarator = false;
    if (r != analysis_result::well_formed)
        return r;
    entity_index c = m.qualifier;
    if (!c || !m.has_id || m.aligned)
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
    /* [basic.def.odr]/1 */
    if (entity_table[e].defined)
        return ill_formed_at_token(m.id,
                                   "no translation unit shall contain more "
                                   "than one definition of any function",
                                   "basic.def.odr");
    entity_table[e].defined = true;
    entity_table.add_declaration(e, n);
    last_declared = e;
    return special_member_body(n, children[1]);
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
        if (!entity_table[c].first_constructor)
            /* value-initialization, or the copy of an aggregate */
            return not_supported(n, "expr.type.conv");
        r = construct(n, type, arguments, 0, value);
    }
    if (r != analysis_result::well_formed)
        return r;
    m = expression_meaning();
    m.type = qualified_type::make(type.unqualified(), cv_none);
    m.category = value_category::prvalue;
    m.evaluation = constness::not_constant;
    m.node = value.node;
    return analysis_result::well_formed;
}

} // namespace cplusplus
