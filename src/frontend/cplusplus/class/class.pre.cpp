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
 * What analysis has rules for so far (doc/PLAN-cxx-sema.md S4): a class
 * or struct whose members are non-static data members of complete object
 * types, static data members, member functions (static or not, defined in
 * the class or outside it), nested classes and enumerations.  A local or
 * nested class has data members only.  Bit-fields, unions, virtual
 * functions and member functions of local and nested classes are
 * `unsupported` with their subclause.  The layout is the Itanium ABI's,
 * made at the closing } (abi/itanium/layout.cpp).
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* the subclause of a class declared in class c's scope: a nested class
   ([class.nest]) or, c in a block, a local one ([class.local]); none for
   a class of namespace scope */
const char *semantic_analysis::inner_class_rule(entity_index scope) const
{
    if (is_namespace(scope))
        return nullptr;
    if (entity_table[scope].kind == entity_kind::class_type)
        return "class.nest";
    return "class.local";
}

/* "class.local" for a class within a block scope, directly or as a class
   nested in such a class ([class.local]: its members have <local-name>
   symbols, which have no rules yet); none for a class of namespace scope or
   nested in one */
const char *semantic_analysis::local_class_rule(entity_index c) const
{
    for (entity_index s = entity_table[c].enclosing; s;
         s = entity_table[s].enclosing)
        if (!is_namespace(s) && entity_table[s].kind != entity_kind::class_type)
            return "class.local";
    return nullptr;
}

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
        !syntax_is(tree, tree.children(n)[0], syntax_kind::class_head))
        return r;
    /* a class whose definition is not analysed: its name is held in the
       scope it would be declared in, so that a later
       elaborated-type-specifier or class-name does not find (or declare)
       another class */
    entity_index held_in = current_scope;
    if (defining_class)
        held_in = defining_class;
    node_index head = tree.children(n)[0].index();
    for (uint32_t i = 1; i < tree[head].child_count; i++) {
        syntax_child c = tree.children(head)[i];
        if (!c.is_token() || tokens[c.index()].kind != token_kind::identifier)
            continue;
        uint32_t name = tokens.identifier(c.index());
        entity_index e = entity_table.find(held_in, name);
        if (!e) {
            e = entity_table.make(entity_kind::none, name, held_in);
            entity_table.bind(held_in, name, e);
        } else if (entity_table[e].kind == entity_kind::class_type) {
            entity_table[e].kind = entity_kind::none;
            entity_table[e].size = 0;
        }
        break;
    }
    return r;
}

/* whether a decl-specifier-seq has a class-specifier of an unnamed
   class */
bool semantic_analysis::declares_unnamed_class(node_index seq) const
{
    for (uint32_t i = 0; i < tree[seq].child_count; i++) {
        syntax_child c = tree.children(seq)[i];
        if (!syntax_is(tree, c, syntax_kind::class_specifier) ||
            !tree[c.index()].child_count)
            continue;
        syntax_child head = tree.children(c.index())[0];
        if (syntax_is(tree, head, syntax_kind::class_head) &&
            tree[head.index()].child_count == 1)
            return true;
    }
    return false;
}

/*
 * [class.union.anon]/1: "A union of the form union { member-specification
 * } ; is called an anonymous union; it defines an unnamed type and an
 * unnamed object of that type called an anonymous union member if it is a
 * non-static data member or an anonymous union variable otherwise.  Each
 * member-declaration in the member-specification of an anonymous union
 * shall either define a non-static data member or be a
 * static_assert-declaration. ... The names of the members of an anonymous
 * union are bound in the scope inherited from the scope of the anonymous
 * union declaration."  An unnamed struct so is clang's extension, the same
 * with the struct's layout.  The member object takes a field of c like a
 * named one; a name of a member is bound in c to the member, whose class
 * says which object it is in (entity::anonymous_object).
 */
analysis_result semantic_analysis::anonymous_union_member(entity_index c,
                                                          node_index n,
                                                          const specified &s,
                                                          uint8_t access)
{
    const struct type record = type_table[s.type.unqualified()];
    if (s.typedef_ || s.static_ || s.extern_ || s.inline_ || s.constexpr_ ||
        s.type.cv() || s.aligned || s.gnu_flags ||
        record.kind != type_kind::class_type)
        return not_supported(n, "class.union.anon");
    entity_index u = record.entity;
    const entity &x = entity_table[u];
    if (x.first_constructor || x.destructor || x.base_count ||
        !size_of_type(s.type))
        return not_supported(n, "class.union.anon");
    entity_index e = entity_table.make(entity_kind::variable, 0, c);
    entity_table[e].type = s.type;
    entity_table[e].nonstatic_member = true;
    entity_table[e].defined = true;
    entity_table[e].field = entity_table[c].member_count;
    entity_table[e].linkage_of_name = entity_table[c].linkage_of_name;
    entity_table[e].access = access;
    entity_table.add_declaration(e, n);
    class_members.append(e);
    entity_table[c].member_count++;
    entity_table[u].anonymous_object = e;
    return bind_anonymous_members(c, n, u);
}

/* the names of the members of anonymous union u, and of the anonymous
   unions among them, bound in c ([class.union.anon]/1) */
analysis_result semantic_analysis::bind_anonymous_members(entity_index c,
                                                          node_index n,
                                                          entity_index u)
{
    const entity &x = entity_table[u];
    for (uint32_t i = 0; i < x.member_count; i++) {
        entity_index m = class_members[x.first_member + i];
        const entity &member = entity_table[m];
        if (!member.name) {
            entity_index inner = 0;
            const struct type t = type_table[member.type.unqualified()];
            if (!member.bit_field && t.kind == type_kind::class_type)
                inner = t.entity;
            if (inner && entity_table[inner].anonymous_object == m) {
                analysis_result r = bind_anonymous_members(c, n, inner);
                if (r != analysis_result::well_formed)
                    return r;
            }
            continue;
        }
        if (member.access)
            /* [class.union.anon]/1: "an anonymous union shall not have
               private or protected members" */
            return ill_formed(n,
                              "an anonymous union shall not have private or "
                              "protected members",
                              "class.union.anon");
        if (entity_table.find(c, member.name))
            /* [class.mem]/5: "A member shall not be declared twice in the
               member-specification" */
            return not_supported(n, "class.union.anon");
        entity_table.bind(c, member.name, m);
    }
    return analysis_result::well_formed;
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
    /* class-head-name: nested-name-specifier_opt class-name */
    token_index head_name = 0;
    node_index head_qualifier = 0;
    if (!unnamed && head_children[1].is_token() &&
        tokens[head_children[1].index()].kind == token_kind::identifier) {
        head_name = head_children[1].index();
    } else if (!unnamed && syntax_is(tree, head_children[1],
                                     syntax_kind::class_head_name)) {
        node_index hn = head_children[1].index();
        const syntax_child *name_parts = tree.children(hn);
        if (tree[hn].child_count != 2 ||
            !syntax_is(tree, name_parts[0],
                       syntax_kind::nested_name_specifier) ||
            !name_parts[1].is_token() ||
            tokens[name_parts[1].index()].kind != token_kind::identifier)
            /* a simple-template-id */
            return not_supported(n, "class.pre");
        head_qualifier = name_parts[0].index();
        head_name = name_parts[1].index();
    } else if (!unnamed) {
        /* attributes */
        return not_supported(n, "class.pre");
    }
    bool has_bases =
        h.child_count == 3 && !unnamed &&
        syntax_is(tree, head_children[2], syntax_kind::base_clause);
    if (h.child_count > 2 && !has_bases)
        /* a class-virt-specifier, a base-clause of an unnamed class */
        return not_supported(n, "class.derived");
    standard_keyword key = p.keyword_of(tokens[head_children[0].index()]);
    if (key != standard_keyword::class_ && key != standard_keyword::struct_ &&
        key != standard_keyword::union_)
        return not_supported(n, "class.pre");
    /* #pragma pack and the like change the layout: no rules yet */
    if (tokens.files.layout_pragma)
        return not_supported(n, "cpp.pragma");
    /* [class.union]/1: "A union shall not have base classes." (clang's
       error) */
    if (key == standard_keyword::union_ && has_bases)
        return not_supported(n, "class.union");
    /* [class.nest]/1: "A class can be declared within another class.  A
       class so declared is called a nested class."  [class.local]/1: "A
       class can be declared within a function definition; such a class is
       called a local class."  Their members are analysed as a class of
       namespace scope's, but member functions (which would need a
       <nested-name> or <local-name> symbol) not yet */
    entity_index outer = defining_class;
    entity_index scope = current_scope;
    if (outer)
        scope = outer;
    if (head_qualifier) {
        /* [class.pre]/3: "If a class-head-name contains a
           nested-name-specifier, the class-specifier shall refer to a
           class that was previously declared directly in the class or
           namespace to which the nested-name-specifier refers, or in an
           element of the inline namespace set of that namespace (i.e., not
           merely inherited or introduced by a using-declaration), and the
           class-specifier shall appear in a namespace enclosing the
           previous declaration."  A member of a namespace; a nested class
           defined outside its class has no rules yet */
        if (outer || at_block_scope() || function_state.active)
            return not_supported(n, "class.pre");
        nominated_scope q;
        analysis_result qr = nested_name_specifier(head_qualifier, q);
        if (qr != analysis_result::well_formed)
            return qr;
        if (!is_namespace(q.entity)) {
            /* [class.nest]/3: "If class X is defined in a namespace scope,
               a nested class Y may be declared in class X and later defined
               in the definition of class X or be later defined in a
               namespace scope enclosing the definition of class X." */
            entity_index y =
                entity_table.find(q.entity, tokens.identifier(head_name));
            if (y && entity_table[y].hidden_type)
                y = entity_table[y].hidden_type;
            bool declared = y &&
                            entity_table[y].kind == entity_kind::class_type &&
                            entity_table[y].enclosing == q.entity;
            if (!declared ||
                entity_table[q.entity].kind != entity_kind::class_type ||
                !is_namespace(entity_table[q.entity].enclosing)) {
                /* the class this would define is not known to be
                   incomplete from here on */
                if (declared) {
                    entity_table[y].kind = entity_kind::none;
                    entity_table[y].size = 0;
                }
                return not_supported(n, "class.nest");
            }
            scope = q.entity;
        } else {
            lookup_result found = lookup_in_namespace(
                q.entity, tokens.identifier(head_name), lookup_filter::types);
            if (!found.entity ||
                entity_table[found.entity].kind != entity_kind::class_type ||
                entity_table[found.entity].enclosing != found.found_in)
                return not_supported(n, "class.pre");
            bool directly = true;
            for (entity_index y = found.found_in; y != q.entity;
                 y = entity_table[y].enclosing)
                if (!y || !entity_table[y].inline_)
                    directly = false;
            if (!directly ||
                !namespace_encloses(current_scope, found.found_in) ||
                current_scope == found.found_in) {
                /* (in its own namespace: clang's extra qualification, a
                   warning): the class may be defined here all the same */
                entity_table[found.entity].kind = entity_kind::none;
                entity_table[found.entity].size = 0;
                return not_supported(n, "class.pre");
            }
            scope = found.found_in;
        }
    }
    if (has_bases && !is_namespace(scope))
        return not_supported(n, inner_class_rule(scope));
    token_index id = head_children[0].index();
    uint32_t name = 0;
    entity_index c = 0;
    if (!unnamed) {
        id = head_name;
        name = tokens.identifier(id);
        c = entity_table.find(scope, name);
        /* a name a using-declaration brought into the scope: the class
           defined would conflict with it or, as a Microsoft extension,
           define the class it names */
        if (c && entity_table.bound_by_using(scope, name)) {
            if (entity_table[c].kind == entity_kind::class_type &&
                tokens.files.behavior.microsoft_extensions) {
                entity_table[c].kind = entity_kind::none;
                entity_table[c].size = 0;
            }
            return not_supported(n, "namespace.udecl");
        }
    }
    /* [basic.scope.hiding]/2: "If a class name or enumeration name and a
       variable, data member, function, or enumerator are declared in the same
       declarative region (in any order) with the same name ..., the class or
       enumeration name is hidden wherever the variable, data member, function,
       or enumerator name is visible.": the one that hides the class defined
       here */
    entity_index hider = 0;
    if (c && entity_table[c].enclosing == scope &&
        (entity_table[c].kind == entity_kind::variable ||
         entity_table[c].kind == entity_kind::function)) {
        hider = c;
        c = entity_table[c].hidden_type;
    }
    if (unnamed) {
        /* [basic.link]/4: an unnamed class has no linkage until a typedef
           names it for linkage purposes (declare) */
        c = entity_table.make(entity_kind::class_type, 0, scope);
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
        /* [dcl.type.elab]/3: "The class-key or enum keyword present in
           the elaborated-type-specifier shall agree in kind with the
           declaration to which the name in the elaborated-type-specifier
           refers ... the union class-key shall be used to refer to a
           union" (an earlier declaration with another class-key) */
        if (prior.union_ != (key == standard_keyword::union_))
            return not_supported(n, "dcl.type.elab");
    } else {
        c = entity_table.make(entity_kind::class_type, name, scope);
        /* [basic.link]/2.3: a local class's name has no linkage; a nested
           class's is its enclosing class's */
        /* [basic.link]/4: a named class of namespace scope has the
           linkage of its namespace */
        if (is_namespace(scope) && within_unnamed_namespace(scope))
            entity_table[c].linkage_of_name = linkage::internal;
        else if (is_namespace(scope))
            entity_table[c].linkage_of_name = linkage::external;
        else if (outer)
            entity_table[c].linkage_of_name =
                entity_table[outer].linkage_of_name;
        entity_table[c].type = qualified_type::make(
            type_table.of_entity(type_kind::class_type, c), cv_none);
        if (hider)
            entity_table[hider].hidden_type = c;
        else
            entity_table.bind(scope, name, c);
    }
    if (outer)
        entity_table[c].access = declaration_access;
    entity_table.add_declaration(c, current_declaration);
    entity_table[c].defined = true;
    entity_table[c].union_ = key == standard_keyword::union_;
    entity_table[c].class_key = head_children[0].index();
    entity_table[c].first_member = static_cast<uint32_t>(class_members.size());
    entity_table[c].member_count = 0;
    type = entity_table[c].type.unqualified();

    /* [class.access]/3: "Members of a class defined with the keyword class
       are private by default.  Members of a class defined with the keywords
       struct or union are public by default." */
    uint8_t access = 0;
    if (key == standard_keyword::class_)
        access = 2;
    uint8_t outer_access = declaration_access;
    defining_class = c;
    /* the enclosing class's analysis goes on after this one: its state
       back, and its members so far again after this class's, so that they
       stay one range of class_members */
    auto resume_outer = [&]() {
        defining_class = outer;
        declaration_access = outer_access;
        if (!outer)
            return;
        entity &o = entity_table[outer];
        if (o.first_member + o.member_count == class_members.size())
            return;
        uint32_t first = static_cast<uint32_t>(class_members.size());
        for (uint32_t i = 0; i < o.member_count; i++)
            class_members.append(class_members[o.first_member + i]);
        entity_table[outer].first_member = first;
    };
    entity_table[c].first_base = static_cast<uint32_t>(class_bases.size());
    entity_table[c].base_count = 0;
    analysis_result result = analysis_result::well_formed;
    if (has_bases)
        result = base_clause(c, head_children[2].index(), key);
    if (result != analysis_result::well_formed) {
        resume_outer();
        entity_table[c].kind = entity_kind::none;
        entity_table[c].size = 0;
        return result;
    }
    owned_array<node_index> bodies;
    size_t nested_start = nested_bodies.size();
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
    /* [class.mem]/7: complete at the closing }, where its bases and
       members get their offsets and the class its size
       ([abi:class-types]) */
    const char *layout_rule = itanium_class_layout(*this, c);
    resume_outer();
    if (layout_rule && result == analysis_result::well_formed)
        result = not_supported(n, layout_rule);
    if (result == analysis_result::well_formed)
        result = implicit_special_members(c, n);
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
    node_index outer_declaration = current_declaration;
    result = worse(result, member_default_arguments(c));
    current_declaration = outer_declaration;
    if (outer && entity_table[outer].kind == entity_kind::class_type) {
        /* [class.mem]/6: a nested class's member function bodies are in
           the complete-class context of the enclosing class too: analysed
           at its closing } */
        for (node_index body : bodies)
            nested_bodies.append(deferred_body{c, body});
    } else {
        size_t nested_end = nested_bodies.size();
        for (node_index body : bodies) {
            result = worse(result, member_function_body(c, body));
            current_declaration = outer_declaration;
        }
        for (size_t i = nested_start; i < nested_end; i++) {
            result = worse(result, member_function_body(nested_bodies[i].c,
                                                        nested_bodies[i].body));
            current_declaration = outer_declaration;
        }
        nested_bodies.resize(nested_start);
    }
    if (result == analysis_result::well_formed)
        result = implicit_definitions(c, n);
    current_declaration = outer_declaration;
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
    if (!node.child_count || !children[0].is_token())
        return not_supported(n, "dcl.type.elab");
    standard_keyword key = p.keyword_of(tokens[children[0].index()]);
    if (key != standard_keyword::class_ && key != standard_keyword::struct_ &&
        key != standard_keyword::union_)
        return not_supported(n, "dcl.type.elab");
    bool is_union = key == standard_keyword::union_;
    if (node.child_count == 3 &&
        syntax_is(tree, children[1], syntax_kind::nested_name_specifier) &&
        children[2].is_token() &&
        tokens[children[2].index()].kind == token_kind::identifier) {
        /* [basic.lookup.elab]/3: "If the elaborated-type-specifier has a
           nested-name-specifier, qualified name lookup is performed, as
           described in [basic.lookup.qual], but ignoring any non-type names
           that have been declared.  If the name lookup does not find a
           previously declared type-name, the elaborated-type-specifier is
           ill-formed." */
        nominated_scope q;
        analysis_result r = nested_name_specifier(children[1].index(), q);
        if (r != analysis_result::well_formed)
            return r;
        token_index t = children[2].index();
        lookup_result found = lookup_qualified(q.entity, tokens.identifier(t),
                                               lookup_filter::types);
        if (found.ambiguous)
            return ill_formed_at_token(t,
                                       "the use of a name is ill-formed if "
                                       "it does not allow a unique "
                                       "declaration to be chosen from those "
                                       "found in the namespaces",
                                       "namespace.qual");
        entity_index c = found.entity;
        /* one not found may be declared by a declaration analysis has no
           rules for */
        if (!c)
            return not_supported(n, "basic.lookup.elab");
        if (entity_table[c].kind == entity_kind::typedef_name)
            /* [dcl.type.elab]/2: "If the identifier or simple-template-id
               resolves to a typedef-name ..., the elaborated-type-specifier
               is ill-formed." */
            return ill_formed_at_token(t,
                                       "if the identifier resolves to a "
                                       "typedef-name, the "
                                       "elaborated-type-specifier is "
                                       "ill-formed",
                                       "dcl.type.elab");
        if (entity_table[c].kind != entity_kind::class_type)
            return not_supported(n, "basic.lookup.elab");
        if (entity_table[c].union_ != is_union)
            return not_supported(n, "dcl.type.elab");
        used(t, c);
        type = entity_table[c].type.unqualified();
        return analysis_result::well_formed;
    }
    if (node.child_count != 2 || !children[1].is_token() ||
        tokens[children[1].index()].kind != token_kind::identifier)
        return not_supported(n, "dcl.type.elab");
    token_index id = children[1].index();
    uint32_t name = tokens.identifier(id);
    bool sole = sole_elaborated == n;
    /* [basic.scope.pdecl]/7.2: "if the elaborated-type-specifier is used in
       the decl-specifier-seq or parameter-declaration-clause of a function
       defined in namespace scope, the identifier is declared as a
       class-name in the namespace that contains the declaration;
       otherwise, except as a friend declaration, the identifier is
       declared in the smallest namespace or block scope that contains the
       declaration": the scope a class it declares is declared in */
    entity_index target = current_scope;
    if (is_namespace(target) && defining_class) {
        target = defining_class;
        while (!is_namespace(target) &&
               entity_table[target].kind == entity_kind::class_type)
            target = entity_table[target].enclosing;
    }
    /* a function parameter scope (a mem-initializer's expressions) is no
       block */
    if (!is_namespace(target) &&
        (entity_table[target].kind != entity_kind::block_scope ||
         entity_table[target].parameter_scope))
        return not_supported(n, "basic.scope.pdecl");
    entity_index c = 0;
    if (sole && defining_class) {
        /* [class.nest]: a nested class declared; one declared before in
           the class is named again */
        entity_index e = entity_table.find(defining_class, name);
        if (e && entity_table[e].hidden_type)
            e = entity_table[e].hidden_type;
        if (!e) {
            /* [class.nest]/3: "If class X is defined in a namespace scope,
               a nested class Y may be declared in class X and later defined
               in the definition of class X or be later defined in a
               namespace scope enclosing the definition of class X." */
            c = declare_class(defining_class, name, is_union);
            entity_table[c].access = declaration_access;
            entity_table[c].linkage_of_name =
                entity_table[defining_class].linkage_of_name;
            type = entity_table[c].type.unqualified();
            return analysis_result::well_formed;
        }
        if (entity_table[e].kind != entity_kind::class_type ||
            entity_table[e].enclosing != defining_class)
            return not_supported(n, "class.nest");
        c = e;
    } else if (sole) {
        /* [basic.scope.pdecl]/7.1: only the scope that contains the
           declaration is searched: a class of an enclosing scope is not
           the one declared */
        entity_index e = entity_table.find(target, name);
        if (e && (entity_table[e].kind == entity_kind::none ||
                  entity_table.bound_by_using(target, name)))
            return not_supported(n, "basic.lookup.elab");
        if (e && entity_table[e].kind == entity_kind::class_type &&
            entity_table[e].enclosing == target) {
            c = e;
        } else if (e && entity_table[e].hidden_type) {
            c = entity_table[e].hidden_type;
            if (entity_table[c].kind != entity_kind::class_type)
                return not_supported(n, "basic.lookup.elab");
        } else if (e && entity_table[e].kind != entity_kind::variable &&
                   entity_table[e].kind != entity_kind::function) {
            /* a typedef-name, an enumeration, a namespace of the name:
               clang's error, by more than one rule */
            return not_supported(n, "basic.lookup.elab");
        } else {
            c = declare_class(target, name, is_union);
            type = entity_table[c].type.unqualified();
            return analysis_result::well_formed;
        }
    } else {
        /* [basic.lookup.elab]/2: "If the elaborated-type-specifier has no
           nested-name-specifier, and unless the elaborated-type-specifier
           appears in a declaration with the following form:" class-key
           attribute-specifier-seq_opt identifier ; "the identifier is looked up
           according to [basic.lookup.unqual] but ignoring any non-type names
           that have been declared. ... If the elaborated-type-specifier is
           introduced by the class-key and this lookup does not find a
           previously declared type-name, or if the elaborated-type-specifier
           appears in a declaration with the form" above, "the
           elaborated-type-specifier is a declaration that introduces the
           class-name as described in [basic.scope.pdecl]." */
        lookup_result found = lookup_unqualified(name, lookup_filter::types);
        if (found.ambiguous)
            return ill_formed_at_token(id,
                                       "a name found in two different "
                                       "namespaces whose declarations do not "
                                       "declare the same entity is ill-formed",
                                       "namespace.udir");
        if (found.unknown)
            return not_supported(n, "basic.lookup.elab");
        c = found.entity;
        if (!c) {
            /* declared in target; a name bound there by a declaration
               analysis did not follow, or by a using-declaration, may be
               the class's */
            entity_index e = entity_table.find(target, name);
            if (e && (entity_table[e].kind == entity_kind::none ||
                      entity_table.bound_by_using(target, name)))
                return not_supported(n, "basic.lookup.elab");
            c = declare_class(target, name, is_union);
            type = entity_table[c].type.unqualified();
            return analysis_result::well_formed;
        }
        if (entity_table[c].kind == entity_kind::typedef_name)
            /* [dcl.type.elab]/2: "If the identifier or simple-template-id
               resolves to a typedef-name, the elaborated-type-specifier is
               ill-formed." */
            return ill_formed_at_token(id,
                                       "if the identifier resolves to a "
                                       "typedef-name, the "
                                       "elaborated-type-specifier is "
                                       "ill-formed",
                                       "dcl.type.elab");
        if (entity_table[c].kind != entity_kind::class_type)
            return not_supported(n, "basic.lookup.elab");
    }
    /* [dcl.type.elab]/3: "The class-key or enum keyword present in the
       elaborated-type-specifier shall agree in kind with the declaration to
       which the name in the elaborated-type-specifier refers ... the union
       class-key shall be used to refer to a union" */
    if (entity_table[c].union_ != is_union)
        return not_supported(n, "dcl.type.elab");
    used(id, c);
    type = entity_table[c].type.unqualified();
    return analysis_result::well_formed;
}

/*
 * [basic.scope.pdecl]/7: the class an elaborated-type-specifier declares
 * in scope (a namespace or a block), incomplete until a class-specifier
 * defines it.  [basic.scope.hiding]/2: a variable or function of the name
 * declared in the scope hides it.
 */
entity_index semantic_analysis::declare_class(entity_index scope, uint32_t name,
                                              bool is_union)
{
    entity_index c = entity_table.make(entity_kind::class_type, name, scope);
    /* [basic.link]/4: a named class of namespace scope has its namespace's
       linkage; [basic.link]/8: one of a block scope none */
    if (is_namespace(scope)) {
        entity_table[c].linkage_of_name = linkage::external;
        if (within_unnamed_namespace(scope))
            entity_table[c].linkage_of_name = linkage::internal;
    }
    entity_table[c].union_ = is_union;
    entity_table[c].type = qualified_type::make(
        type_table.of_entity(type_kind::class_type, c), cv_none);
    entity_table.add_declaration(c, current_declaration);
    entity_index hider = entity_table.find(scope, name);
    if (hider)
        entity_table[hider].hidden_type = c;
    else
        entity_table.bind(scope, name, c);
    return c;
}

/*
 * [dcl.init.aggr]/1: "An aggregate is an array or a class ([class]) with
 * (1.1) no user-declared or inherited constructors ([class.ctor]), (1.2) no
 * private or protected direct non-static data members ([class.access]),
 * (1.3) no virtual functions ([class.virtual]), and (1.4) no virtual,
 * private, or protected base classes ([class.mi])."  The classes analysis
 * has have no virtual function or virtual base.  [dcl.init.aggr]/2: "The
 * elements of an aggregate are: ... for a class, the direct base classes
 * in declaration order, followed by the direct non-static data members";
 * an empty base, which is no field, is not analysed as one.
 */
bool semantic_analysis::is_aggregate(entity_index c) const
{
    const entity &x = entity_table[c];
    if (x.first_constructor)
        return false;
    for (uint32_t i = 0; i < x.base_count; i++)
        if (entity_table[class_bases[x.first_base + i]].field == UINT32_MAX)
            return false;
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
 * restriction."  [class.protected]/1: a protected non-static member named
 * in a member of a derived class D is accessed "through a pointer to,
 * reference to, or object of the class D itself (or any class derived
 * from D)": object_class, the class of the object expression (0: *this).
 * There are no friends yet.
 */
bool semantic_analysis::accessible(entity_index member,
                                   entity_index object_class) const
{
    const entity &x = entity_table[member];
    if (!x.access)
        return true;
    /* the classes the name is used in a member of: the class of the
       function analysed, the class whose member-specification is
       analysed, the class of a static data member's definition
       ([class.static.data]/2: its initializer "is in the scope of its
       class"), and the classes around each of them: [class.access.nest]/1
       "A nested class is a member and as such has the same access rights
       as any other member."; [class.local]/1 a local class "has the same
       access to names outside the function as does the enclosing
       function" */
    entity_index starts[3] = {0, defining_class, 0};
    if (function_state.active)
        starts[0] = function_state.function;
    if (!is_namespace(current_scope))
        starts[2] = current_scope;
    for (entity_index start : starts) {
        for (entity_index here = start; !is_namespace(here);
             here = entity_table[here].enclosing) {
            if (entity_table[here].kind != entity_kind::class_type)
                continue;
            if (here == x.enclosing)
                return true;
            if (x.access != 1)
                continue;
            bool ambiguous = false;
            if (!base_step(here, x.enclosing, ambiguous))
                continue;
            if (!object_class || !x.nonstatic_member || object_class == here)
                return true;
            if (base_step(object_class, here, ambiguous))
                return true;
        }
    }
    return false;
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
    if (node.kind == syntax_kind::using_declaration) {
        /* [namespace.udecl]/2: a using-declaration is a
           member-declaration */
        declaration_access = access;
        analysis_result r = using_declaration(n);
        declaration_access = 0;
        return r;
    }
    if (node.kind != syntax_kind::member_declaration &&
        node.kind != syntax_kind::function_definition)
        return not_supported(n);
    const char *nested_rule = local_class_rule(c);
    if (nested_rule && node.kind == syntax_kind::function_definition)
        return not_supported(n, nested_rule);
    /* a constructor or destructor: no decl-specifier-seq, or one of
       function-specifiers alone ([class.ctor]/1: "Each decl-specifier in
       the decl-specifier-seq of a constructor declaration (if any) shall
       be friend, inline, constexpr, or an explicit-specifier") */
    uint32_t at = 0;
    bool constexpr_specified = false;
    if (node.child_count > 1 &&
        syntax_is(tree, children[0], syntax_kind::decl_specifier_seq) &&
        function_specifiers_only(children[0].index(), constexpr_specified) &&
        declares_special_member(c, children[1]))
        at = 1;
    if (!node.child_count || at ||
        !syntax_is(tree, children[0], syntax_kind::decl_specifier_seq)) {
        /* [class.mem]/12: "The decl-specifier-seq may be omitted in
           constructor, destructor, and conversion function declarations
           only" */
        if (!node.child_count || !declares_special_member(c, children[at]))
            return not_supported(n, "class.ctor");
        if (nested_rule)
            return not_supported(n, nested_rule);
        if (node.kind == syntax_kind::member_declaration) {
            if (node.child_count != at + 2 ||
                !syntax_is_punctuator(tokens, children[at + 1],
                                      preprocessing_op_or_punc::SEMICOLON))
                /* = default, = delete, a pure-specifier */
                return not_supported(n, "dcl.fct.def");
            return special_member_declarator(c, n, children[at], access, false,
                                             constexpr_specified);
        }
        if (node.child_count != at + 2 ||
            !syntax_is(tree, children[at + 1], syntax_kind::function_body))
            return not_supported(n, "dcl.fct.def.general");
        const syntax_node &b = tree[children[at + 1].index()];
        if (b.child_count &&
            syntax_is_punctuator(tokens,
                                 tree.children(children[at + 1].index())[0],
                                 preprocessing_op_or_punc::EQUAL))
            /* = default, = delete: such a constructor is not user-provided
               ([dcl.fct.def.default]/5), which the layout ([abi:pod]) and
               triviality need to know before the class is complete */
            return not_supported(n, "dcl.fct.def.general");
        analysis_result r = special_member_declarator(
            c, n, children[at], access, true, constexpr_specified);
        if (r == analysis_result::well_formed)
            bodies.append(n);
        return r;
    }
    specified s;
    declaration_access = access;
    /* [basic.scope.pdecl]/7.1: class-key identifier ; declares a nested
       class */
    sole_elaborated = 0;
    if (node.kind == syntax_kind::member_declaration && node.child_count == 2 &&
        syntax_is_punctuator(tokens, children[1],
                             preprocessing_op_or_punc::SEMICOLON) &&
        tree[children[0].index()].child_count == 1 &&
        syntax_is(tree, tree.children(children[0].index())[0],
                  syntax_kind::elaborated_type_specifier))
        sole_elaborated = tree.children(children[0].index())[0].index();
    analysis_result r = decl_specifier_seq(children[0].index(), s, false);
    sole_elaborated = 0;
    declaration_access = 0;
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
    if (declarators.empty()) {
        /* [class.mem]/3: a member enumeration ([dcl.pre]/5) */
        bool plain = !s.typedef_ && !s.static_ && !s.extern_ && !s.inline_ &&
                     !s.constexpr_ && !s.type.cv();
        if (s.declares_enumeration && plain)
            return analysis_result::well_formed;
        if (declares_unnamed_class(children[0].index()))
            return anonymous_union_member(c, n, s, access);
        /* [class.nest]/1: a nested class's definition, or its declaration
           ([class.nest]/3) */
        const syntax_node &seq = tree[children[0].index()];
        if (plain && seq.child_count == 1 &&
            (syntax_is(tree, tree.children(children[0].index())[0],
                       syntax_kind::class_specifier) ||
             syntax_is(tree, tree.children(children[0].index())[0],
                       syntax_kind::elaborated_type_specifier)))
            return analysis_result::well_formed;
        /* a nested class, an anonymous union */
        return not_supported(n, "class.mem");
    }
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
    if (syntax_is(tree, d, syntax_kind::member_declarator)) {
        /* identifier_opt attribute-specifier-seq_opt : constant-expression */
        const syntax_node &node = tree[d.index()];
        for (uint32_t i = 0; i < node.child_count; i++)
            if (syntax_is_punctuator(tokens, tree.children(d.index())[i],
                                     preprocessing_op_or_punc::COLON))
                return member_bit_field(c, declaration, d, s, access);
        /* declarator brace-or-equal-initializer: [class.mem]/10 "A
           brace-or-equal-initializer shall appear only in the declaration
           of a data member"; for a non-static one it is a default member
           initializer, analysed where a constructor uses it
           ([class.base.init]/9), a complete-class context ([class.mem]/6) */
        const syntax_child *parts = tree.children(d.index());
        if (node.child_count != 2 ||
            (!syntax_is(tree, parts[1],
                        syntax_kind::brace_or_equal_initializer) &&
             !syntax_is(tree, parts[1], syntax_kind::braced_init_list)))
            return not_supported(d.index(), "class.mem");
        last_declared = 0;
        analysis_result r =
            member_declarator(c, declaration, parts[0], s, access, definition);
        if (r != analysis_result::well_formed)
            return r;
        entity_index e = last_declared;
        if (!e || entity_table[e].kind != entity_kind::variable ||
            !entity_table[e].nonstatic_member)
            /* a static data member's initializer, a pure-specifier */
            return not_supported(d.index(), "class.static.data");
        if (entity_table[c].union_ || !entity_table[c].name)
            /* [class.union.anon]: at most one variant member's; an unnamed
               class's members are initialized by no constructor of its own */
            return not_supported(d.index(), "class.union");
        entity_table[e].default_initializer = parts[1].index();
        return analysis_result::well_formed;
    }
    if (!s.has_type && declares_special_member(c, d))
        /* a cv-qualifier or another specifier with a constructor */
        return not_supported(declaration, "class.ctor");
    qualified_type type = s.type;
    declarator_meaning m;
    function_cv = 0;
    in_member_declarator = true;
    function_parameters.clear();
    analysis_result r = declarator(declaration, d, type, m);
    in_member_declarator = false;
    if (r != analysis_result::well_formed)
        return r;
    /* [dcl.fct.default]: a member function's default arguments are a
       complete-class context ([class.mem]/6): analysed with the class
       complete (member_default_arguments) */
    bool has_defaults = false;
    for (const parameter_meaning &p : function_parameters)
        if (p.default_argument.index())
            has_defaults = true;
    owned_array<parameter_meaning> parameters;
    for (const parameter_meaning &p : function_parameters)
        parameters.append(p);
    if (!m.has_id || m.qualifier)
        return not_supported(declaration, "class.mem");
    /* [ext:gnu_attributes] the declaration's and the declarator's */
    if (s.aligned > m.aligned)
        m.aligned = s.aligned;
    m.gnu_flags |= s.gnu_flags;
    if (!m.section)
        m.section = s.section;
    if (!s.has_type)
        return not_supported(declaration, "dcl.type");
    if (s.typedef_)
        return not_supported(declaration, "dcl.typedef");
    /* [dcl.constexpr]/1: "The constexpr specifier shall be applied only to
       the definition of a variable or variable template or the declaration
       of a function or function template": a static data member's is not
       analysed yet */
    if (s.constexpr_ &&
        type_table[type.unqualified()].kind != type_kind::function)
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
    if ((is_function && (m.aligned || (m.gnu_flags & gnu_packed))) ||
        (m.gnu_flags & gnu_used))
        return not_supported(declaration, "ext.gnu");
    /* a member's symbol attributes (a member function's, a static data
       member's) are not analysed yet; on a non-static data member they
       are GCC's error */
    if (m.gnu_flags & (gnu_noinline | gnu_always_inline | gnu_noreturn |
                       gnu_weak | gnu_used | gnu_section))
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
    /* [basic.scope.hiding]/2: "If a class name or enumeration name and a
       variable, data member, function, or enumerator are declared in the same
       declarative region (in any order) with the same name ..., the class or
       enumeration name is hidden wherever the variable, data member, function,
       or enumerator name is visible." */
    entity_index hides = 0;
    if (prior && entity_table[prior].enclosing == c &&
        (entity_table[prior].kind == entity_kind::class_type ||
         entity_table[prior].kind == entity_kind::enumeration)) {
        hides = prior;
        prior = 0;
    }
    if (prior && entity_table[prior].enclosing != c)
        /* a name a using-declaration or a member enumeration's enumerator
           brought into the class */
        return not_supported(declaration, "class.mem");
    entity_index overloads = 0;
    if (prior && is_function &&
        entity_table[prior].kind == entity_kind::function &&
        entity_table[prior].special == special_member::none) {
        /* [over.load]/2: "Certain function declarations cannot be
           overloaded: Function declarations that differ only in the return
           type ... Member function declarations with the same name, the
           same parameter-type-list, and the same trailing
           requires-clause cannot be overloaded if any of them is a static
           member function declaration."  The cv-qualifier-seq of a member
           function is part of its type ([dcl.fct]/1), so one that differs
           in it is another function */
        for (entity_index o = prior; o; o = entity_table[o].next_overload) {
            const entity &x = entity_table[o];
            if (!same_parameters(type_table, x.type.unqualified(),
                                 type.unqualified()))
                continue;
            if (x.member_cv == function_cv &&
                x.nonstatic_member == !s.static_ && x.type == type)
                /* [class.mem]/5 */
                return ill_formed_at_token(m.id,
                                           "a member shall not be declared "
                                           "twice in the member-specification",
                                           "class.mem");
            if (x.member_cv == function_cv && x.nonstatic_member == !s.static_)
                return ill_formed_at_token(m.id,
                                           "function declarations that differ "
                                           "only in the return type cannot be "
                                           "overloaded",
                                           "over.load");
            if (x.nonstatic_member != !s.static_)
                return ill_formed_at_token(m.id,
                                           "member function declarations "
                                           "with the same name and the same "
                                           "parameter-type-list cannot be "
                                           "overloaded if any of them is a "
                                           "static member function "
                                           "declaration",
                                           "over.load");
        }
        overloads = prior;
        prior = 0;
    }
    if (prior) {
        if (entity_table[prior].kind == entity_kind::none)
            return not_supported(declaration, "over");
        owned_buffer spelling;
        tokens.spelling(m.id, spelling.get());
        if (spelling.size() == 1 && spelling.data()[0] == '_')
            /* C++26's name-independent declarations (P2169): _ may be
               declared again */
            return not_supported(declaration, "basic.scope.scope");
        /* [class.mem]/5: "A member shall not be declared twice in the
           member-specification" */
        return ill_formed_at_token(m.id,
                                   "a member shall not be declared twice in "
                                   "the member-specification",
                                   "class.mem");
    }
    entity_index e = 0;
    if (is_function && local_class_rule(c))
        return not_supported(declaration, local_class_rule(c));
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
        /* C++11's [dcl.constexpr]/8 made a constexpr non-static member
           function const, which C++14 dropped: not analysed */
        if (s.constexpr_ && !s.static_ &&
            tokens.files.behavior.edition < standard_edition::cplusplus_14)
            return not_supported(declaration, "dcl.constexpr");
        entity_table[e].inline_ = definition || s.inline_ || s.constexpr_;
        /* [dcl.constexpr]/1: constexpr makes it inline; its body's rules
           and its invocations in constant expressions are the evaluator's
           (S6) */
        entity_table[e].constexpr_ = s.constexpr_;
        entity_table[e].defined = definition;
        if (m.operator_function) {
            entity_table[e].operator_function = true;
            entity_table[e].implemented = m.implemented;
            entity_table[e].allocation = m.allocation;
            analysis_result checked =
                operator_function_declaration(declaration, e);
            if (checked != analysis_result::well_formed)
                return checked;
            if (copy_assignment_operator(e, entity_table[e].enclosing))
                entity_table[entity_table[e].enclosing].user_copy_assignment =
                    true;
        }
        if (m.literal_operator) {
            analysis_result checked =
                literal_operator_declaration(declaration, e);
            if (checked != analysis_result::well_formed)
                return checked;
        }
        if (has_defaults) {
            if (entity_table[e].allocation != allocation_function::none)
                /* [basic.stc.dynamic.allocation]/1: "The first parameter
                   shall not have an associated default argument"; which
                   parameter has one is known once the defaults are
                   analysed, after the class */
                return not_supported(declaration,
                                     "basic.stc.dynamic.allocation");
            if (entity_table[e].operator_function &&
                entity_table[e].allocation == allocation_function::none &&
                entity_table[e].implemented !=
                    preprocessing_op_or_punc::LEFT_PARENTHESIS)
                /* [over.oper]/10 */
                return ill_formed(declaration,
                                  "an operator function cannot have default "
                                  "arguments",
                                  "over.oper");
            defer_default_arguments(e, declaration, parameters);
        }
    } else if (has_defaults) {
        /* /3: a default argument of a member that is not a function */
        return not_supported(declaration, "dcl.fct.default");
    } else if (s.static_) {
        /* [class.static.data]/3: "The declaration of a non-inline static
           data member in its class definition is not a definition and may
           be of an incomplete type other than cv void." */
        if (s.inline_)
            return not_supported(declaration, "class.static.data");
        if (!is_namespace(entity_table[c].enclosing))
            /* [class.local]/4: "A local class cannot have static data
               members"; a nested class's needs a <nested-name> of two
               classes */
            return not_supported(declaration, "class.static.data");
        const struct type &record = type_table[type.unqualified()];
        if (record.kind == type_kind::lvalue_reference ||
            record.kind == type_kind::rvalue_reference)
            return not_supported(declaration, "class.static.data");
        e = entity_table.make(entity_kind::variable, name, c);
        entity_table[e].type = type;
        /* [class.static.data]/1: "A static data member is not part of the
           subobjects of a class." */
        entity_table[e].nonstatic_member = false;
        entity_table[e].defined = false;
        entity_table[e].alignment = static_cast<uint32_t>(m.aligned);
    } else {
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
        if (entity_table[c].union_ && record.kind == type_kind::class_type)
            /* a member whose special member functions may make the
               union's deleted ([class.union]/2 Note) */
            return not_supported(declaration, "class.union");
        /* its offset is the layout's, at the closing }
           ([abi:class-types]), with [ext:gnu_attributes]' aligned and
           packed */
        e = entity_table.make(entity_kind::variable, name, c);
        entity_table[e].type = type;
        entity_table[e].nonstatic_member = true;
        entity_table[e].defined = true;
        entity_table[e].field = entity_table[c].member_count;
        entity_table[e].alignment = static_cast<uint32_t>(m.aligned);
        entity_table[e].gnu_flags = m.gnu_flags & gnu_packed;
        class_members.append(e);
        entity_table[c].member_count++;
    }
    entity_table[e].linkage_of_name = entity_table[c].linkage_of_name;
    entity_table[e].access = access;
    entity_table.add_declaration(e, declaration);
    entity_table[e].hidden_type = hides;
    if (overloads) {
        /* another member function of the set, last in its chain */
        entity_index last = overloads;
        while (entity_table[last].next_overload)
            last = entity_table[last].next_overload;
        entity_table[last].next_overload = e;
        entity_table[e].hidden_type = entity_table[overloads].hidden_type;
    } else {
        entity_table.bind(c, name, e);
    }
    last_declared = e;
    return analysis_result::well_formed;
}

/*
 * [class.bit]/1: "A member-declarator of the form
 *     identifier_opt attribute-specifier-seq_opt : constant-expression brace-or-equal-initializer_opt
 * specifies a bit-field.  The optional attribute-specifier-seq appertains
 * to the entity being declared.  A bit-field shall not be a static member.
 * A bit-field shall have integral or enumeration type ...  The
 * constant-expression shall be an integral constant expression with a
 * value greater than or equal to zero and is called the width of the
 * bit-field.  If the width of a bit-field is larger than the width of the
 * bit-field's type (or, in case of an enumeration type, of its underlying
 * type), the extra bits are padding bits.  Allocation of bit-fields within
 * a class object is implementation-defined."  /2: "A declaration for a
 * bit-field that omits the identifier declares an unnamed bit-field.
 * Unnamed bit-fields are not members and cannot be initialized. ... As a
 * special case, an unnamed bit-field with a width of zero specifies
 * alignment of the next bit-field at an allocation unit boundary.  Only
 * when declaring an unnamed bit-field may the width be zero."
 *
 * The allocation is the layout's, at the closing } (abi/itanium/layout.cpp:
 * clang's for aarch64 Linux).  Bit-fields with
 * padding bits, with an initializer or attributes are not
 * analysed yet.
 */
analysis_result semantic_analysis::member_bit_field(entity_index c,
                                                    node_index declaration,
                                                    syntax_child d,
                                                    const specified &s,
                                                    uint8_t access)
{
    const syntax_node &node = tree[d.index()];
    const syntax_child *children = tree.children(d.index());
    token_index id = 0;
    uint32_t k = 0;
    if (k < node.child_count && children[k].is_token() &&
        tokens[children[k].index()].kind == token_kind::identifier)
        id = children[k++].index();
    if (k + 2 != node.child_count ||
        !syntax_is_punctuator(tokens, children[k],
                              preprocessing_op_or_punc::COLON))
        /* attributes, a brace-or-equal-initializer */
        return not_supported(d.index(), "class.bit");
    syntax_child width_expression = children[k + 1];
    if (!s.has_type || s.typedef_ || s.static_ || s.extern_ || s.inline_ ||
        s.constexpr_ || s.aligned || s.gnu_flags)
        return not_supported(declaration, "class.bit");
    qualified_type type = s.type;
    const struct type record = type_table[type.unqualified()];
    /* [class.bit]/1: "A bit-field shall have integral or enumeration
       type"; an enumeration whose underlying type is not fixed yet (an
       incomplete one) has no size */
    bool enumeration = record.kind == type_kind::enumeration;
    if (enumeration && !size_of_type(type))
        return not_supported(declaration, "class.bit");
    if (!enumeration && (record.kind != type_kind::fundamental ||
                         !fundamental_type_is_integral(record.fundamental)))
        return ill_formed(declaration,
                          "a bit-field shall have integral or enumeration "
                          "type",
                          "class.bit");
    expression_meaning w;
    analysis_result r = expression(width_expression, w);
    if (r != analysis_result::well_formed)
        return r;
    r = operand_value(declaration, w);
    if (r != analysis_result::well_formed)
        return r;
    promote_enumeration(w);
    if (!is_integral(w.type) || w.evaluation != constness::constant)
        return not_supported(declaration, "class.bit");
    __int128 width_value =
        integer_value(tokens.files.behavior, fundamental_of(w.type), w.value);
    if (width_value < 0)
        return ill_formed(declaration,
                          "the width of a bit-field shall be greater than or "
                          "equal to zero",
                          "class.bit");
    if (width_value == 0 && id)
        return ill_formed(declaration,
                          "only when declaring an unnamed bit-field may the "
                          "width be zero",
                          "class.bit");
    uint64_t unit = size_of_type(type) * 8;
    uint64_t alignment = alignment_of_type(type);
    if (static_cast<unsigned __int128>(width_value) > unit || !alignment)
        /* padding bits (clang's warning) */
        return not_supported(declaration, "class.bit");
    uint64_t width = static_cast<uint64_t>(width_value);
    uint32_t name = 0;
    if (id) {
        name = tokens.identifier(id);
        if (entity_table.find(c, name))
            return not_supported(declaration, "class.mem");
    }
    entity_index e = entity_table.make(entity_kind::variable, name, c);
    entity_table[e].type = type;
    entity_table[e].nonstatic_member = true;
    entity_table[e].defined = true;
    entity_table[e].field = entity_table[c].member_count;
    entity_table[e].bit_field = true;
    entity_table[e].bit_width = static_cast<uint16_t>(width);
    entity_table[e].linkage_of_name = entity_table[c].linkage_of_name;
    entity_table[e].access = access;
    class_members.append(e);
    entity_table[c].member_count++;
    entity_table.add_declaration(e, declaration);
    if (name)
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
    /* [basic.scope.class]/1: the declaration is in the class's scope, its
       names (a member type as the return type) found there */
    entity_index scope = current_scope;
    current_scope = c;
    analysis_result r = member_function_definition(c, definition, children);
    current_scope = scope;
    return r;
}

analysis_result semantic_analysis::member_function_definition(
    entity_index c, node_index definition, const syntax_child *children)
{
    bool constexpr_specified = false;
    uint32_t at = 0;
    if (tree[definition].child_count > 2 &&
        syntax_is(tree, children[0], syntax_kind::decl_specifier_seq) &&
        function_specifiers_only(children[0].index(), constexpr_specified) &&
        declares_special_member(c, children[1]))
        at = 1;
    if (at || !syntax_is(tree, children[0], syntax_kind::decl_specifier_seq)) {
        /* a constructor or destructor: the one this definition declared */
        qualified_type type = qualified_type::make(
            type_table.fundamental(fundamental_type::void_), cv_none);
        declarator_meaning m;
        function_cv = 0;
        in_member_declarator = true;
        analysis_result r = declarator(definition, children[at], type, m);
        in_member_declarator = false;
        if (r != analysis_result::well_formed)
            return r;
        last_declared = 0;
        if (m.destructor) {
            last_declared = entity_table[c].destructor;
        } else {
            for (entity_index k = entity_table[c].first_constructor; k;
                 k = entity_table[k].next_constructor)
                if (declared_by(k, definition))
                    last_declared = k;
        }
        if (!last_declared)
            return not_supported(definition, "class.ctor");
        return special_member_body(definition, children[at + 1]);
    }
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
    /* the function of the name this definition declared (one of an
       overload set) */
    last_declared = 0;
    for (entity_index o = entity_table.find(c, tokens.identifier(m.id)); o;
         o = entity_table[o].next_overload)
        if (declared_by(o, definition))
            last_declared = o;
    if (!last_declared)
        return not_supported(definition, "class.mfct");
    if (deleted_body(children[2]))
        return deleted_definition(definition, last_declared);
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
    if (at_block_scope() || function_state.active)
        return not_supported(declaration, "class.mfct");
    if (type_table[type.unqualified()].kind != type_kind::function)
        /* (init_declarator analyses a static data member's definition) */
        return not_supported(declaration, "class.static.data");
    if (!definition || s.static_ || s.typedef_ || s.extern_)
        return not_supported(declaration, "class.mfct");
    entity_index e = entity_table.find(qualifier, tokens.identifier(id));
    if (e && entity_table[e].kind == entity_kind::none)
        return not_supported(declaration, "over");
    /* [dcl.meaning]/1: the member function of the set it declares again */
    for (entity_index o = e; o && entity_table[o].kind == entity_kind::function;
         o = entity_table[o].next_overload)
        if (entity_table[o].type == type &&
            entity_table[o].member_cv == function_cv) {
            e = o;
            break;
        }
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

/*
 * [class.static.data]/3: "The definition for a static data
 * member that is not defined inline in the class definition shall appear
 * in a namespace scope enclosing the member's class definition.  In the
 * definition at namespace scope, the name of the static data member shall
 * be qualified by its class name using the :: operator."  The declaration
 * refers to the member declared in the class ([dcl.meaning]/1), with its
 * type (an array's bound may be given here, [dcl.array]/8).
 */
analysis_result semantic_analysis::static_member_definition(
    node_index declaration, token_index id, entity_index qualifier,
    qualified_type type, const specified &s, bool definition,
    entity_index &declared_entity)
{
    declared_entity = 0;
    if (at_block_scope() || function_state.active)
        return not_supported(declaration, "class.static.data");
    if (s.typedef_ || s.inline_ || s.constexpr_)
        return not_supported(declaration, "class.static.data");
    /* static or extern on the definition: clang's error */
    if (s.static_ || s.extern_)
        return not_supported(declaration, "class.static.data");
    entity_index e = entity_table.find(qualifier, tokens.identifier(id));
    if (e && entity_table[e].kind == entity_kind::none)
        return not_supported(declaration, "class.static.data");
    /* [basic.link]/11: "declarations for an array object can specify
       array types that differ by the presence or absence of a major array
       bound" */
    bool same_type = false;
    if (e) {
        const struct type &x = type_table[entity_table[e].type.unqualified()];
        const struct type &y = type_table[type.unqualified()];
        same_type = entity_table[e].type == type ||
                    (x.kind == type_kind::array && y.kind == type_kind::array &&
                     x.operand == y.operand && x.bound_known != y.bound_known &&
                     entity_table[e].type.cv() == type.cv());
    }
    if (!e || entity_table[e].kind != entity_kind::variable ||
        entity_table[e].nonstatic_member || !same_type)
        return ill_formed_at_token(id,
                                   "when the declarator-id is qualified, the "
                                   "declaration shall refer to a previously "
                                   "declared member of the class",
                                   "dcl.meaning");
    /* [basic.def.odr]/1 */
    if (entity_table[e].defined)
        return ill_formed_at_token(id,
                                   "no translation unit shall contain more "
                                   "than one definition of any variable",
                                   "basic.def.odr");
    /* [basic.def]/2: a declaration of it outside the class is a
       definition (it has no extern) */
    (void)definition;
    entity_table[e].defined = true;
    if (type_table[type.unqualified()].kind == type_kind::array &&
        type_table[type.unqualified()].bound_known)
        entity_table[e].type = type;
    entity_table.add_declaration(e, declaration);
    declared_entity = e;
    declared_name d;
    d.declaration = declaration;
    d.outermost = current_declaration;
    d.entity = e;
    d.id = id;
    d.kind = entity_kind::variable;
    d.type = type;
    d.claimable = declarations_claimable;
    declared.append(d);
    return analysis_result::well_formed;
}

} // namespace cplusplus
