/*
 * class.pre.cpp: [class.pre] Classes: the analysis of a class-specifier
 * and of an elaborated-type-specifier that names a class (the grammar is
 * dcl/dcl.spec.cpp's, elaborated_or_class_specifier).
 *
 *     class-specifier:
 *         class-head { member-specification_opt }
 *
 *     class-head:
 *         class-key attribute-specifier-seq_opt class-head-name class-virt-specifier_opt base-clause_opt
 *         class-key attribute-specifier-seq_opt base-clause_opt
 *
 * What analysis has rules for so far (doc/PLAN-cxx-sema.md S4): a named
 * class or struct of global namespace scope with no base class, whose
 * members are non-static data members of complete object types and
 * member functions (static or not, defined in the class or outside it).
 * Constructors, destructors, static data members, bit-fields, unions,
 * nested and local classes, base classes and virtual functions are
 * `unsupported` with their subclause.  The layout is the Itanium ABI's for
 * such a class ([abi:layout]): each member at the next offset aligned for
 * it, the size rounded up to the class's alignment, an empty class one
 * byte.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

namespace
{

uint64_t align_up(uint64_t n, uint64_t alignment)
{
    if (!alignment)
        return n;
    return (n + alignment - 1) / alignment * alignment;
}

} // namespace

/*
 * [class.pre]/1: "A class is a type.  Its name becomes a class-name
 * ([class.name]) within its scope."  [class.name]/2: "A class declaration
 * introduces the class name into the scope where it is declared".
 */
analysis_result semantic_analysis::class_specifier(node_index n,
                                                   type_index &type)
{
    analysis_result r = class_definition(n, type);
    if (r == analysis_result::well_formed || !tree[n].child_count ||
        !syntax_is(tree, tree.children(n)[0], syntax_kind::class_head) ||
        current_scope || function_state.active || defining_class)
        return r;
    /* a class of namespace scope whose definition is not analysed: its
       name is held, so that a later elaborated-type-specifier or
       class-name does not find (or declare) another class */
    node_index head = tree.children(n)[0].index();
    for (uint32_t i = 1; i < tree[head].child_count; i++) {
        syntax_child c = tree.children(head)[i];
        if (!c.is_token() || tokens[c.index()].kind != token_kind::identifier)
            continue;
        uint32_t name = tokens.identifier(c.index());
        entity_index e = entity_table.find(0, name);
        if (!e) {
            e = entity_table.make(entity_kind::none, name, 0);
            entity_table.bind(0, name, e);
        } else if (entity_table[e].kind == entity_kind::class_type) {
            entity_table[e].kind = entity_kind::none;
            entity_table[e].size = 0;
        }
        break;
    }
    return r;
}

analysis_result semantic_analysis::class_definition(node_index n,
                                                    type_index &type)
{
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    if (!node.child_count ||
        !syntax_is(tree, children[0], syntax_kind::class_head))
        return not_supported(n, "class.pre");
    node_index head = children[0].index();
    const syntax_node &h = tree[head];
    const syntax_child *head_children = tree.children(head);
    if (!h.child_count || !head_children[0].is_token())
        return not_supported(n, "class.pre");
    /* [class.pre]/1: "A class-specifier whose class-head omits the
       class-head-name defines an unnamed class." */
    bool unnamed = h.child_count == 1;
    if (!unnamed &&
        (!head_children[1].is_token() ||
         tokens[head_children[1].index()].kind != token_kind::identifier))
        /* a class-head-name with a nested-name-specifier, attributes */
        return not_supported(n, "class.pre");
    if (h.child_count > 2)
        /* a base-clause or a class-virt-specifier */
        return not_supported(n, "class.derived");
    standard_keyword key = p.keyword_of(tokens[head_children[0].index()]);
    if (key == standard_keyword::union_)
        return not_supported(n, "class.union");
    if (key != standard_keyword::class_ && key != standard_keyword::struct_)
        return not_supported(n, "class.pre");
    if (defining_class)
        return not_supported(n, "class.nest");
    if (current_scope || function_state.active)
        return not_supported(n, "class.local");
    token_index id = head_children[0].index();
    uint32_t name = 0;
    entity_index c = 0;
    if (!unnamed) {
        id = head_children[1].index();
        name = tokens.identifier(id);
        c = entity_table.find(0, name);
    }
    if (unnamed) {
        /* [basic.link]/4: an unnamed class has no linkage until a typedef
           names it for linkage purposes (declare) */
        c = entity_table.make(entity_kind::class_type, 0, 0);
        entity_table[c].type = qualified_type::make(
            type_table.of_entity(type_kind::class_type, c), cv_none);
    } else if (c) {
        entity &prior = entity_table[c];
        if (prior.kind == entity_kind::none)
            return not_supported(n, "basic.scope.declarative");
        if (prior.kind != entity_kind::class_type)
            /* a class and a variable or function of one name
               ([basic.scope.hiding]/2) */
            return not_supported(n, "basic.scope.hiding");
        /* [basic.def.odr]/1: "No translation unit shall contain more than
           one definition of any variable, function, class type,
           enumeration type, template, default argument for a parameter
           (for a function in a given scope), or default template
           argument." */
        if (prior.defined)
            return ill_formed_at_token(id,
                                       "no translation unit shall contain "
                                       "more than one definition of any "
                                       "class type",
                                       "basic.def.odr");
    } else {
        c = entity_table.make(entity_kind::class_type, name, 0);
        entity_table[c].linkage_of_name = linkage::external;
        entity_table[c].type = qualified_type::make(
            type_table.of_entity(type_kind::class_type, c), cv_none);
        entity_table.bind(0, name, c);
    }
    entity_table.add_declaration(c, current_declaration);
    entity_table[c].defined = true;
    entity_table[c].first_member = static_cast<uint32_t>(class_members.size());
    entity_table[c].member_count = 0;
    type = entity_table[c].type.unqualified();

    /* [class.access]/3: "Members of a class defined with the keyword class
       are private by default.  Members of a class defined with the keywords
       struct or union are public by default." */
    uint8_t access = 0;
    if (key == standard_keyword::class_)
        access = 2;
    defining_class = c;
    layout_size = 0;
    layout_alignment = 1;
    analysis_result result = analysis_result::well_formed;
    owned_array<node_index> bodies;
    for (uint32_t i = 1; i < node.child_count; i++) {
        syntax_child part = children[i];
        if (!syntax_is(tree, part, syntax_kind::member_specification))
            continue;
        const syntax_node &spec = tree[part.index()];
        for (uint32_t k = 0; k < spec.child_count; k++) {
            syntax_child member = tree.children(part.index())[k];
            if (member.is_token()) {
                switch (p.keyword_of(tokens[member.index()])) {
                case standard_keyword::public_:
                    access = 0;
                    continue;
                case standard_keyword::protected_:
                    access = 1;
                    continue;
                case standard_keyword::private_:
                    access = 2;
                    continue;
                default:
                    break;
                }
                if (syntax_is_punctuator(tokens, member,
                                         preprocessing_op_or_punc::COLON))
                    continue;
                result = worse(result, not_supported(n, "class.mem"));
                continue;
            }
            result =
                worse(result, member_declaration(c, member, access, bodies));
        }
    }
    defining_class = 0;
    /* [class.mem]/7: complete at the closing } */
    entity &x = entity_table[c];
    x.alignment = layout_alignment;
    x.size = align_up(layout_size, layout_alignment);
    /* [intro.object]/8: "Unless it is a bit-field, an object with nonzero
       size shall occupy one or more bytes of storage": an empty class is
       one byte ([abi:layout]: dsize, size 1) */
    if (!x.size)
        x.size = 1;
    if (result != analysis_result::well_formed) {
        /* nothing is concluded about the class: its name finds nothing
           later declarations can use, and a type that names it (a typedef
           of it declared before) is neither complete nor incomplete */
        entity_table[c].kind = entity_kind::none;
        entity_table[c].size = 0;
        return result;
    }
    /* [class.mem]/6: "A complete-class context of a class is a function
       body ... within the member-specification of the class": the bodies
       are analysed with the class complete */
    node_index outer = current_declaration;
    for (node_index body : bodies) {
        result = worse(result, member_function_body(c, body));
        current_declaration = outer;
    }
    if (result != analysis_result::well_formed) {
        entity_table[c].kind = entity_kind::none;
        entity_table[c].size = 0;
    }
    return result;
}

/*
 * [dcl.type.elab]:
 *
 *     elaborated-type-specifier:
 *         class-key attribute-specifier-seq_opt nested-name-specifier_opt identifier
 *
 * [basic.lookup.elab]/2: the identifier is looked up; a class found is the
 * type.  [dcl.type.elab]/2 (with [basic.scope.pdecl]/7): if none is
 * found, the declaration `class-key identifier ;` declares the class in
 * the scope (an incomplete class), as does any other at namespace scope.
 */
analysis_result semantic_analysis::elaborated_type_specifier(node_index n,
                                                             type_index &type)
{
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    if (node.child_count != 2 || !children[0].is_token() ||
        !children[1].is_token() ||
        tokens[children[1].index()].kind != token_kind::identifier)
        return not_supported(n, "dcl.type.elab");
    standard_keyword key = p.keyword_of(tokens[children[0].index()]);
    if (key != standard_keyword::class_ && key != standard_keyword::struct_)
        return not_supported(n, "dcl.type.elab");
    if (current_scope || function_state.active || defining_class)
        return not_supported(n, "basic.lookup.elab");
    uint32_t name = tokens.identifier(children[1].index());
    entity_index c = entity_table.find(0, name);
    if (c) {
        if (entity_table[c].kind != entity_kind::class_type)
            return not_supported(n, "basic.lookup.elab");
        type = entity_table[c].type.unqualified();
        return analysis_result::well_formed;
    }
    c = entity_table.make(entity_kind::class_type, name, 0);
    entity_table[c].linkage_of_name = linkage::external;
    entity_table[c].type = qualified_type::make(
        type_table.of_entity(type_kind::class_type, c), cv_none);
    entity_table.add_declaration(c, current_declaration);
    entity_table.bind(0, name, c);
    type = entity_table[c].type.unqualified();
    return analysis_result::well_formed;
}

/*
 * [dcl.init.aggr]/1: "An aggregate is an array or a class ([class]) with
 * (1.1) no user-declared or inherited constructors ([class.ctor]), (1.2) no
 * private or protected direct non-static data members ([class.access]),
 * (1.3) no virtual functions ([class.virtual]), and (1.4) no virtual,
 * private, or protected base classes ([class.mi])."  The classes analysis
 * has have no constructor, virtual function or base class.
 */
bool semantic_analysis::is_aggregate(entity_index c) const
{
    const entity &x = entity_table[c];
    for (uint32_t i = 0; i < x.member_count; i++)
        if (entity_table[class_members[x.first_member + i]].access)
            return false;
    return true;
}

/* the class of the member function whose body is analysed */
entity_index semantic_analysis::current_class() const
{
    if (!function_state.active || !function_state.function)
        return 0;
    entity_index c = entity_table[function_state.function].enclosing;
    if (c && entity_table[c].kind == entity_kind::class_type)
        return c;
    return 0;
}

/*
 * [class.access]/1: "A member of a class can be (1.1) private; that is,
 * its name can be used only by members and friends of the class in which
 * it is declared. (1.2) protected; that is, its name can be used only by
 * members and friends of the class in which it is declared, by classes
 * derived from that class, and by their friends (see [class.protected]).
 * (1.3) public; that is, its name can be used anywhere without access
 * restriction."  There are no friends or derived classes yet.
 */
bool semantic_analysis::accessible(entity_index member) const
{
    const entity &x = entity_table[member];
    if (!x.access)
        return true;
    return current_class() == x.enclosing;
}

/* ---- [class.mem] -------------------------------------------------------- */

/*
 * One member-declaration or function-definition of the member-specification
 * of class c, whose members declared before it have access `access` by
 * default.  The body of a member function defined here is analysed at the
 * end of the class (bodies).
 */
analysis_result
semantic_analysis::member_declaration(entity_index c, syntax_child member,
                                      uint8_t access,
                                      owned_array<node_index> &bodies)
{
    node_index n = member.index();
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    if (node.kind == syntax_kind::empty_declaration)
        return analysis_result::well_formed;
    if (node.kind != syntax_kind::member_declaration &&
        node.kind != syntax_kind::function_definition)
        return not_supported(n);
    if (!node.child_count ||
        !syntax_is(tree, children[0], syntax_kind::decl_specifier_seq))
        /* [class.mem]/12: "The decl-specifier-seq may be omitted in
           constructor, destructor, and conversion function declarations
           only" */
        return not_supported(n, "class.ctor");
    specified s;
    analysis_result r = decl_specifier_seq(children[0].index(), s, false);
    if (r != analysis_result::well_formed)
        return r;
    if (node.kind == syntax_kind::function_definition) {
        if (node.child_count != 3 ||
            !syntax_is(tree, children[2], syntax_kind::function_body))
            return not_supported(n, "dcl.fct.def.general");
        const syntax_node &b = tree[children[2].index()];
        if (b.child_count != 1 ||
            !syntax_is(tree, tree.children(children[2].index())[0],
                       syntax_kind::compound_statement))
            /* a ctor-initializer, a function-try-block, = default,
               = delete */
            return not_supported(n, "dcl.fct.def.general");
        r = member_declarator(c, n, children[1], s, access, true);
        if (r == analysis_result::well_formed)
            bodies.append(n);
        return r;
    }
    owned_array<syntax_child> declarators;
    for (uint32_t i = 1; i < node.child_count; i++) {
        syntax_child d = children[i];
        if (syntax_is_punctuator(tokens, d,
                                 preprocessing_op_or_punc::SEMICOLON))
            continue;
        if (syntax_is(tree, d, syntax_kind::member_declarator_list)) {
            const syntax_node &list = tree[d.index()];
            for (uint32_t k = 0; k < list.child_count; k++) {
                syntax_child item = tree.children(d.index())[k];
                if (!syntax_is_punctuator(tokens, item,
                                          preprocessing_op_or_punc::COMMA))
                    declarators.append(item);
            }
            continue;
        }
        declarators.append(d);
    }
    if (declarators.empty())
        /* a nested class or enumeration, or an anonymous union */
        return not_supported(n, "class.mem");
    analysis_result result = analysis_result::well_formed;
    for (syntax_child d : declarators)
        result = worse(result, member_declarator(c, n, d, s, access, false));
    return result;
}

/*
 *     member-declarator:
 *         declarator virt-specifier-seq_opt pure-specifier_opt
 *         declarator requires-clause
 *         declarator brace-or-equal-initializer_opt
 *         identifier_opt attribute-specifier-seq_opt : constant-expression brace-or-equal-initializer_opt
 *
 * Only the first alternative without its options: a member-declarator
 * node is one with a bit-field, an initializer or a specifier.
 */
analysis_result
semantic_analysis::member_declarator(entity_index c, node_index declaration,
                                     syntax_child d, const specified &s,
                                     uint8_t access, bool definition)
{
    if (syntax_is(tree, d, syntax_kind::member_declarator))
        return not_supported(d.index(), "class.mem");
    qualified_type type = s.type;
    declarator_meaning m;
    function_cv = 0;
    in_member_declarator = true;
    analysis_result r = declarator(declaration, d, type, m);
    in_member_declarator = false;
    if (r != analysis_result::well_formed)
        return r;
    if (!m.has_id || m.qualifier)
        return not_supported(declaration, "class.mem");
    if (!s.has_type)
        return not_supported(declaration, "dcl.type");
    if (s.typedef_)
        return not_supported(declaration, "dcl.typedef");
    if (s.constexpr_)
        return not_supported(declaration, "dcl.constexpr");
    /* [class.mem]/11: "A member shall not be declared with the extern
       storage-class-specifier." */
    if (s.extern_)
        return ill_formed_at_token(m.id,
                                   "a member shall not be declared with the "
                                   "extern storage-class-specifier",
                                   "class.mem");
    uint32_t name = tokens.identifier(m.id);
    bool is_function =
        type_table[type.unqualified()].kind == type_kind::function;
    if (is_function && m.aligned)
        return not_supported(declaration, "ext.gnu");
    /* [class.mem]/20: "If T is the name of a class, then each of the
       following shall have a name different from T: every static data
       member of class T; every member function of class T ...; every
       member of class T that is itself a type ..." (and [class.mem]/21 a
       non-static data member, when the class has a constructor) */
    if (name == entity_table[c].name && (is_function || s.static_))
        return ill_formed_at_token(m.id,
                                   "a member function or static data member "
                                   "of a class shall have a name different "
                                   "from the class",
                                   "class.mem");
    entity_index prior = entity_table.find(c, name);
    if (prior) {
        if (is_function && entity_table[prior].kind == entity_kind::function) {
            /* an overload set of member functions */
            entity_table[prior].kind = entity_kind::none;
            return not_supported(declaration, "over");
        }
        if (entity_table[prior].kind == entity_kind::none)
            return not_supported(declaration, "over");
        /* [class.mem]/5: "A member shall not be declared twice in the
           member-specification" */
        return ill_formed_at_token(m.id,
                                   "a member shall not be declared twice in "
                                   "the member-specification",
                                   "class.mem");
    }
    entity_index e = 0;
    if (is_function) {
        /* [class.static.mfct]/2: "A static member function shall not be
           virtual ... A static member function shall not be declared
           const, volatile, or const volatile." */
        if (s.static_ && function_cv)
            return ill_formed_at_token(m.id,
                                       "a static member function shall not "
                                       "be declared const, volatile, or "
                                       "const volatile",
                                       "class.static.mfct");
        e = entity_table.make(entity_kind::function, name, c);
        entity_table[e].type = type;
        entity_table[e].nonstatic_member = !s.static_;
        entity_table[e].member_cv = function_cv;
        /* [class.mfct]/1: "A member function may be defined in its class
           definition, in which case it is an inline member function
           ([dcl.inline])" */
        entity_table[e].inline_ = definition || s.inline_;
        entity_table[e].defined = definition;
    } else {
        if (s.static_)
            return not_supported(declaration, "class.static.data");
        if (s.inline_)
            /* [dcl.inline]/5: "The inline specifier shall not appear on a
               block scope declaration or on the declaration of a function
               parameter."  On a non-static data member it is clang's
               error, from [dcl.inline]/1's "applied only to the
               declaration of a variable or function" */
            return ill_formed_at_token(m.id,
                                       "the inline specifier can be applied "
                                       "only to the declaration or definition "
                                       "of a variable or function",
                                       "dcl.inline");
        const struct type &record = type_table[type.unqualified()];
        if (record.kind == type_kind::lvalue_reference ||
            record.kind == type_kind::rvalue_reference)
            /* a reference member makes the default constructor deleted
               ([class.default.ctor]/2): with constructors */
            return not_supported(declaration, "class.ctor");
        uint64_t size = size_of_type(type);
        bool is_void = record.kind == type_kind::fundamental &&
                       record.fundamental == fundamental_type::void_;
        if (record.kind == type_kind::class_type && !size)
            /* [class.mem]/15: "The type of a non-static data member shall
               not be an incomplete type" */
            return ill_formed_at_token(m.id,
                                       "the type of a non-static data member "
                                       "shall not be an incomplete type",
                                       "class.mem");
        if (is_void || (record.kind == type_kind::array && !record.bound_known))
            /* incomplete too; an array of unknown bound last is clang's
               flexible array member extension */
            return not_supported(declaration, "class.mem");
        if (!size)
            return not_supported(declaration, "class.mem");
        uint32_t alignment = static_cast<uint32_t>(alignment_of_type(type));
        if (!alignment)
            return not_supported(declaration, "class.mem");
        /* [ext:gnu_attributes] aligned raises a member's alignment (only
           packed lowers it) */
        if (m.aligned > alignment)
            alignment = static_cast<uint32_t>(m.aligned);
        e = entity_table.make(entity_kind::variable, name, c);
        entity_table[e].type = type;
        entity_table[e].nonstatic_member = true;
        entity_table[e].defined = true;
        entity_table[e].field = entity_table[c].member_count;
        entity_table[e].alignment = static_cast<uint32_t>(m.aligned);
        /* [class.mem]/19 (Note): "Non-static data members of a (non-union)
           class with the same access control and non-zero size are
           allocated so that later members have higher addresses within a
           class object."  [abi:layout]: at the next offset aligned for the
           member's type */
        entity_table[e].offset = align_up(layout_size, alignment);
        layout_size = entity_table[e].offset + size;
        if (alignment > layout_alignment)
            layout_alignment = alignment;
        class_members.append(e);
        entity_table[c].member_count++;
    }
    entity_table[e].linkage_of_name = entity_table[c].linkage_of_name;
    entity_table[e].access = access;
    entity_table.add_declaration(e, declaration);
    entity_table.bind(c, name, e);
    last_declared = e;
    return analysis_result::well_formed;
}

/*
 * [class.mfct]/1: the body of a member function defined in its class,
 * analysed as the definition's own: its declarator again, for the
 * parameters (function_body reads them), then the body.
 */
analysis_result semantic_analysis::member_function_body(entity_index c,
                                                        node_index definition)
{
    const syntax_child *children = tree.children(definition);
    current_declaration = definition;
    specified s;
    analysis_result r = decl_specifier_seq(children[0].index(), s, false);
    if (r != analysis_result::well_formed)
        return r;
    qualified_type type = s.type;
    declarator_meaning m;
    function_cv = 0;
    in_member_declarator = true;
    r = declarator(definition, children[1], type, m);
    in_member_declarator = false;
    if (r != analysis_result::well_formed)
        return r;
    last_declared = entity_table.find(c, tokens.identifier(m.id));
    if (!last_declared)
        return not_supported(definition, "class.mfct");
    return function_body(definition, tree.children(children[2].index())[0]);
}

/*
 * [dcl.meaning]/1: "When the declarator-id is qualified, the declaration
 * shall refer to a previously declared member of the class or namespace to
 * which the qualifier refers ... and the member shall not merely have been
 * introduced by a using-declaration in the scope of the class or namespace
 * nominated by the nested-name-specifier of the declarator-id."  A member
 * function of class `qualifier` defined outside its class ([class.mfct]/2:
 * "A member function definition that appears outside of the class
 * definition shall appear in a namespace scope enclosing the class
 * definition").
 */
analysis_result semantic_analysis::member_redeclaration(
    node_index declaration, token_index id, entity_index qualifier,
    qualified_type type, const specified &s, bool definition)
{
    last_declared = 0;
    if (current_scope || function_state.active)
        return not_supported(declaration, "class.mfct");
    if (type_table[type.unqualified()].kind != type_kind::function)
        return not_supported(declaration, "class.static.data");
    if (!definition || s.static_ || s.typedef_ || s.extern_)
        return not_supported(declaration, "class.mfct");
    entity_index e = entity_table.find(qualifier, tokens.identifier(id));
    if (e && entity_table[e].kind == entity_kind::none)
        return not_supported(declaration, "over");
    if (!e || entity_table[e].kind != entity_kind::function ||
        !(entity_table[e].type == type) ||
        entity_table[e].member_cv != function_cv)
        return ill_formed_at_token(id,
                                   "when the declarator-id is qualified, the "
                                   "declaration shall refer to a previously "
                                   "declared member of the class",
                                   "dcl.meaning");
    /* [basic.def.odr]/1 */
    if (entity_table[e].defined)
        return ill_formed_at_token(id,
                                   "no translation unit shall contain more "
                                   "than one definition of any function",
                                   "basic.def.odr");
    entity_table[e].defined = true;
    if (s.inline_)
        entity_table[e].inline_ = true;
    entity_table.add_declaration(e, declaration);
    last_declared = e;
    return analysis_result::well_formed;
}

} // namespace cplusplus
