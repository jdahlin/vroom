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
analysis_result semantic_analysis::class_specifier(ClassSpecifier n,
                                                   type_index &type)
{
    analysis_result r = class_definition(n, type);
    auto head = syntax(n).class_specifier().class_head();
    if (r == analysis_result::well_formed || !head)
        return r;
    /* a class whose definition is not analysed: its name is held in the
       scope it would be declared in, so that a later
       elaborated-type-specifier or class-name does not find (or declare)
       another class */
    entity_index held_in = current_scope;
    if (defining_class)
        held_in = defining_class;
    for (auto c : head.elements(1)) {
        if (!c.is_identifier())
            continue;
        uint32_t name = tokens.identifier(c.token());
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
    for (auto c : syntax(seq).elements()) {
        auto head = c.class_specifier().class_head();
        if (head && head.size() == 1)
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

/* (class_definition) */
analysis_result semantic_analysis::class_definition(node_index n,
                                                    type_index &type)
{
    /* a class template's specialization defined while another class is
       (postpone_member_bodies, define_class_instantiation) */
    bool postpone = postpone_member_bodies;
    postpone_member_bodies = false;
    entity_index postponed_scope = current_scope;
    node_index postponed_declaration = current_declaration;
    auto node = syntax(n);
    auto head = node.class_specifier().class_head();
    if (!head)
        return not_supported(n, "class.pre");
    /* class-key attribute-specifier-seq_opt class-head-name ...: the
       alignment-specifiers left out of head_children, kept in alignas */
    owned_array<SyntaxElement> head_list;
    owned_array<node_index> alignas_list;
    /* [ext:gnu_attributes] a class's visibility("hidden") (its members'
       symbols', c_lowering::gnu_attributes), abi_tag (its name's,
       [abi:mangle.abi-tag]) and exclude_from_explicit_instantiation */
    gnu_attributes gnu;
    /* the class-key first, then what follows it */
    head_list.append(head[0]);
    for (auto c : head.elements(1)) {
        bool only_alignment = c.is(syntax_kind::attribute_specifier_seq);
        for (auto k : c.elements())
            only_alignment =
                only_alignment && k.is(syntax_kind::alignment_specifier);
        if (only_alignment) {
            for (auto k : c.elements())
                alignas_list.append(k.index());
        } else if (c.is(syntax_kind::alignment_specifier)) {
            alignas_list.append(c.index());
        } else if (class_head_attributes_without_effect(c)) {
            /* [dcl.attr.deprecated] and the like: nothing generated */
        } else if (c.is_node() && gnu_or_without_effect(c.index())) {
            /* one attribute-specifier, or a seq of them */
            if (c.is(syntax_kind::attribute_specifier_seq)) {
                for (auto each : c.elements()) {
                    analysis_result r =
                        gnu_attribute_specifier(each.index(), gnu);
                    if (r != analysis_result::well_formed)
                        return r;
                }
            } else {
                analysis_result r = gnu_attribute_specifier(c.index(), gnu);
                if (r != analysis_result::well_formed)
                    return r;
            }
        } else {
            head_list.append(c);
        }
    }
    if (gnu.aligned || gnu.section ||
        (gnu.flags & ~(gnu_hidden_visibility | gnu_abi_tag |
                       gnu_exclude_from_explicit_instantiation)))
        return not_supported(n, "ext.gnu");
    const SyntaxElement *head_children = head_list.data();
    uint32_t head_count = static_cast<uint32_t>(head_list.size());
    if (!head_count || !head_children[0].is_token())
        return not_supported(n, "class.pre");
    /* [class.pre]/1: "A class-specifier whose class-head omits the
       class-head-name defines an unnamed class." */
    bool unnamed = head_count == 1;
    /* class-head-name: nested-name-specifier_opt class-name */
    token_index head_name = 0;
    node_index head_qualifier = 0;
    SyntaxElement name_syntax;
    if (!unnamed)
        name_syntax = syntax(head_children[1]);
    if (name_syntax.is_identifier()) {
        head_name = name_syntax.token();
    } else if (class_instantiating && name_syntax.simple_template_id()) {
        /* a partial or explicit specialization's definition, instantiated
           (instantiate_class): the template's name */
        head_name = name_syntax[0].token();
    } else if (auto qualified = name_syntax.qualified_class_head_name()) {
        if (!qualified.nested_name_specifier() || !qualified.identifier())
            /* a simple-template-id: an explicit or partial
               specialization's class-head ([temp.spec]/4: "A
               specialization is a class, variable, function, or class
               member that is either instantiated ... from a templated entity
               or is an explicit specialization") */
            return not_supported(n, "temp.spec");
        head_qualifier = qualified.nested_name_specifier().index();
        head_name = qualified.identifier().token();
    } else if (!unnamed) {
        /* attributes */
        return not_supported(n, "class.pre");
    }
    bool has_bases =
        head_count == 3 && !unnamed && syntax(head_children[2]).base_clause();
    if (head_count > 2 && !has_bases)
        /* a class-virt-specifier, a base-clause of an unnamed class */
        return not_supported(n, "class.derived");
    standard_keyword key = p.keyword_of(tokens[head_children[0].token()]);
    if (!is_class_key(key))
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
    /* a class template's specialization (instantiate_class): a class of
       the template's scope that no name binds there */
    uint32_t specialization = class_instantiating;
    class_instantiating = 0;
    if (has_bases && !is_namespace(scope))
        return not_supported(n, inner_class_rule(scope));
    token_index id = head_children[0].token();
    uint32_t name = 0;
    entity_index c = 0;
    if (specialization) {
        if (unnamed || head_qualifier || outer)
            return not_supported(n, "temp.class");
        id = head_name;
        name = tokens.identifier(id);
        /* (one named before its template was defined is the incomplete
           class made then) */
        c = instantiations[specialization - 1].specialization;
        if (!c)
            c = entity_table.make(entity_kind::class_type, name, scope);
        entity_table[c].linkage_of_name = linkage::external;
        if (within_unnamed_namespace(scope))
            entity_table[c].linkage_of_name = linkage::internal;
        entity_table[c].type = qualified_type::make(
            type_table.of_entity(type_kind::class_type, c), cv_none);
        entity_table[c].instantiation = specialization;
        instantiation_record &record = instantiations[specialization - 1];
        record.specialization = c;
        /* [class.pre]/2: "The class-name is also bound in the scope of the
           class (template) itself; this is known as the
           injected-class-name."  The template-parameters' names with it,
           each its argument ([temp.arg.type]) */
        entity_table.bind(c, name, c);
        const function_template &ft = function_templates
            [entity_table[record.template_entity].instantiation - 1];
        if (record.pattern) {
            /* a partial specialization's parameters, each the argument
               matching deduced */
            const function_template &ps =
                function_templates[record.pattern - 1];
            for (uint32_t i = 0; i < ps.parameter_count; i++)
                bind_template_argument(
                    c, template_parameter_entities[ps.first_parameter + i],
                    template_arguments[record.first_deduced + i]);
        } else {
            for (uint32_t i = 0; i < ft.parameter_count; i++)
                bind_template_argument(
                    c, template_parameter_entities[ft.first_parameter + i],
                    template_arguments[record.first_argument + i]);
        }
    } else if (!unnamed) {
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
    bool redeclared = false;
    if (specialization) {
    } else if (unnamed) {
        /* [basic.link]/4: an unnamed class has no linkage until a typedef
           names it for linkage purposes (declare) */
        c = entity_table.make(entity_kind::class_type, 0, scope);
        entity_table[c].type = qualified_type::make(
            type_table.of_entity(type_kind::class_type, c), cv_none);
    } else if (c) {
        redeclared = true;
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
    if (outer) {
        /* [class.access.spec]/4: "When a member is redeclared within its
           class definition, the access specified at its redeclaration
           shall be the same as at its initial declaration." */
        if (redeclared && entity_table[c].enclosing == outer &&
            entity_table[c].access != declaration_access)
            return ill_formed_at_token(id,
                                       "the access of a member's "
                                       "redeclaration within its class shall "
                                       "be the same as at its initial "
                                       "declaration",
                                       "class.access.spec");
        entity_table[c].access = declaration_access;
    }
    entity_table.add_declaration(c, current_declaration);
    entity_table[c].gnu_flags |= gnu.flags;
    if (gnu.abi_tag)
        entity_table[c].abi_tag = gnu.abi_tag;
    entity_table[c].defined = true;
    entity_table[c].union_ = key == standard_keyword::union_;
    entity_table[c].class_key = head_children[0].token();
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
        if (outer)
            make_members_contiguous(outer);
    };
    entity_table[c].first_base = static_cast<uint32_t>(class_bases.size());
    entity_table[c].base_count = 0;
    analysis_result result = analysis_result::well_formed;
    if (has_bases)
        result = base_clause(c, head_children[2].base_clause(), key);
    entity_table[c].requested_alignment = trailing_class_alignment;
    trailing_class_alignment = 0;
    for (node_index a : alignas_list) {
        uint64_t alignment = 0;
        if (result == analysis_result::well_formed)
            result =
                alignment_specifier(syntax(a).alignment_specifier(), alignment);
        if (alignment > entity_table[c].requested_alignment)
            entity_table[c].requested_alignment =
                static_cast<uint32_t>(alignment);
    }
    if (result != analysis_result::well_formed) {
        resume_outer();
        entity_table[c].kind = entity_kind::none;
        entity_table[c].size = 0;
        return result;
    }
    owned_array<node_index> bodies;
    size_t nested_start = nested_bodies.size();
    auto spec = node.class_specifier().member_specification();
    for (auto member : spec.elements()) {
        if (member.is_token()) {
            switch (p.keyword_of(tokens[member.token()])) {
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
            if (member.is(preprocessing_op_or_punc::COLON))
                continue;
            result = worse(result, not_supported(n, "class.mem"));
            continue;
        }
        auto declaration = member.member_declaration();
        if (!declaration) {
            /* a node the grammar does not place here (unparsed, an
               explicit instantiation kept as an error) */
            result = worse(result, not_supported(member.index()));
            continue;
        }
        result =
            worse(result, member_declaration(c, declaration, access, bodies));
    }

    /* [class.mem]/7: complete at the closing }, where its bases and
       members get their offsets and the class its size
       ([abi:class-types]) */
    /* [class.virtual]: its virtual table, which the layout's
       initialization needs ([abi:class-types] I) */
    if (result == analysis_result::well_formed)
        result = dynamic_class_record(c, n);
    const char *layout_rule = itanium_class_layout(*this, c);
    if (!layout_rule && result == analysis_result::well_formed)
        dynamic_class_offsets(c);
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
    /* [class.local]: a local class's member functions are analysed inside
       the enclosing function's body, whose state is kept aside meanwhile */
    function_context enclosing_function =
        static_cast<function_context &&>(function_state);
    function_state = function_context();
    owned_array<entity_index> enclosing_block_variables =
        static_cast<owned_array<entity_index> &&>(block_variables);
    block_variables = owned_array<entity_index>();
    entity_index enclosing_scope = current_scope;
    result = worse(result, member_default_arguments(c));
    current_declaration = outer_declaration;
    if (outer && entity_table[outer].kind == entity_kind::class_type) {
        /* [class.mem]/6: a nested class's member function bodies are in
           the complete-class context of the enclosing class too: analysed
           at its closing } */
        for (node_index body : bodies)
            nested_bodies.append(deferred_body{c, body});
    } else if (postpone) {
        /* [temp.inst]/4: a member function's definition is instantiated
           where it is used; the class that named this specialization
           (template<class D> struct base { ... static_cast<D *>(this) ...
           }; struct d : base<d>) is complete only after it: the bodies
           wait for the end of the translation unit (instantiate_pending) */
        for (node_index body : bodies)
            postponed_bodies.append(postponed_body{c, body, postponed_scope,
                                                   postponed_declaration});
        for (size_t i = nested_start; i < nested_bodies.size(); i++)
            postponed_bodies.append(
                postponed_body{nested_bodies[i].c, nested_bodies[i].body,
                               postponed_scope, postponed_declaration});
        nested_bodies.resize(nested_start);
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
    function_state = static_cast<function_context &&>(enclosing_function);
    block_variables =
        static_cast<owned_array<entity_index> &&>(enclosing_block_variables);
    current_scope = enclosing_scope;
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
analysis_result
semantic_analysis::elaborated_type_specifier(ElaboratedTypeSpecifier n,
                                             type_index &type)
{
    /* the attribute-specifier-seq (or GNU attributes, libc++'s
       _LIBCPP_EXPORTED_FROM_ABI) after the class-key: left out when none
       changes what vcc generates (class_head_attributes_without_effect) */
    auto e = syntax(n);
    uint32_t at = 1;
    if (e.size() > 2 && class_head_attributes_without_effect(e[1]))
        at = 2;
    size_t count = e.size() - (at - 1);
    standard_keyword key = leading_keyword(n);
    if (!is_class_key(key))
        return not_supported(n, "dcl.type.elab");
    bool is_union = key == standard_keyword::union_;
    if (count == 3 && e[at].nested_name_specifier() &&
        e[at + 1].is_identifier()) {
        /* [basic.lookup.elab]/3: "If the elaborated-type-specifier has a
           nested-name-specifier, qualified name lookup is performed, as
           described in [basic.lookup.qual], but ignoring any non-type names
           that have been declared.  If the name lookup does not find a
           previously declared type-name, the elaborated-type-specifier is
           ill-formed." */
        nominated_scope q;
        analysis_result r = nested_name_specifier(e[at].index(), q);
        if (r != analysis_result::well_formed)
            return r;
        token_index t = e[at + 1].token();
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
    if (count != 2 || !e[at].is_identifier())
        return not_supported(n, "dcl.type.elab");
    token_index id = e[at].token();
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
    const implementation_defined_behavior &b = tokens.files.behavior;
    /* /1.3: "no virtual functions" */
    if (x.dynamic)
        return false;
    /* [dcl.init.aggr]/1.1: "no user-declared or inherited constructors"
       (C++20); "no user-provided, explicit, or inherited constructors"
       before, where X() = default and = delete are not user-provided
       ([dcl.fct.def.default]/5) */
    if (b.has(
            language_feature::aggregates_without_user_declared_constructors) &&
        x.defaulted_default_constructor)
        return false;
    /* [dcl.init.aggr]/1.1 before C++20: "no ... explicit ...
       constructors" */
    if (x.explicit_default_constructor)
        return false;
    if (x.constructor_template || x.constructor_templates)
        return false;
    for (entity_index k = x.first_constructor; k;
         k = entity_table[k].next_constructor)
        if (!entity_table[k].implicit &&
            (!entity_table[k].deleted ||
             b.has(language_feature::
                       aggregates_without_user_declared_constructors)))
            return false;
    /* [dcl.init.aggr]/1.4: "no virtual, private, or protected base
       classes" (C++17); "no base classes" before */
    if (x.base_count && !b.has(language_feature::aggregates_with_bases))
        return false;
    for (uint32_t i = 0; i < x.base_count; i++)
        if (entity_table[class_bases[x.first_base + i]].field == UINT32_MAX)
            return false;
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = entity_table[class_members[x.first_member + i]];
        if (m.access)
            return false;
        /* C++11's [dcl.init.aggr]/1: "no brace-or-equal-initializers for
           non-static data members" */
        if (m.default_initializer &&
            !b.has(
                language_feature::aggregates_with_default_member_initializers))
            return false;
    }
    return true;
}

/* the class of the member function whose body is analysed */
entity_index semantic_analysis::current_class() const
{
    if (!function_state.active || !function_state.function)
        return 0;
    /* [expr.prim.lambda.closure]/12: "The lambda-expression's
       compound-statement yields the function-body of the function call
       operator, but for purposes of name lookup, determining the type and
       value of this and transforming id-expressions referring to
       non-static class members into class member access expressions using
       (*this), the compound-statement is considered in the context of the
       lambda-expression." */
    entity_index f = function_state.function;
    for (size_t i = lambdas.size(); i-- > 0;)
        if (lambdas[i].call == f)
            f = lambdas[i].enclosing_function;
    if (!f)
        return 0;
    entity_index c = entity_table[f].enclosing;
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
 * [class.friend]/2: "Declaring a class to be a friend implies that private
 * and protected members of the class granting friendship can be named in the
 * base-specifiers and member declarations of the befriended class."
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
    /* the use is in a member of class k */
    auto as_member_of = [&](entity_index k) -> bool {
        if (k == x.enclosing)
            return true;
        if (x.access != 1)
            return false;
        bool ambiguous = false;
        if (!base_step(k, x.enclosing, ambiguous))
            return false;
        if (!object_class || !x.nonstatic_member || object_class == k)
            return true;
        return base_step(object_class, k, ambiguous) != 0;
    };
    entity_index starts[3] = {0, defining_class, 0};
    if (function_state.active)
        starts[0] = function_state.function;
    if (!is_namespace(current_scope))
        starts[2] = current_scope;
    for (entity_index start : starts) {
        for (entity_index here = start; !is_namespace(here);
             here = entity_table[here].enclosing) {
            /* [class.access]/1: a friend of the class (or, for a
               protected member, of a class derived from it) */
            for (const friend_record &f : friends)
                if (friend_names(f, here) && as_member_of(f.granting))
                    return true;
            if (entity_table[here].kind != entity_kind::class_type)
                continue;
            if (as_member_of(here))
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
semantic_analysis::member_declaration(entity_index c, MemberDeclaration m,
                                      uint8_t access,
                                      owned_array<node_index> &bodies)
{
    return m.visit(Overloaded{
        [&](GnuExtensionDeclaration x) {
            /* [ext:gnu_extension_keyword]: __extension__ changes no
               meaning */
            return member_declaration(c, x[1].member_declaration(), access,
                                      bodies);
        },
        [&](EmptyDeclaration) { return analysis_result::well_formed; },
        [&](UsingDeclaration u) {
            /* [namespace.udecl]/2: a using-declaration is a
               member-declaration */
            declaration_access = access;
            analysis_result r = using_declaration(u);
            declaration_access = 0;
            return r;
        },
        [&](AliasDeclaration a) {
            /* [dcl.typedef]/2: "A typedef-name can also be introduced by
               an alias-declaration", a member-declaration ([class.mem]/1) */
            qualified_type type;
            analysis_result r = alias_type(a, type);
            if (r != analysis_result::well_formed)
                return r;
            specified s;
            s.typedef_ = true;
            s.type = type;
            s.has_type = true;
            return member_typedef(c, a, a.first_token() + 1, type, s,
                                  declarator_meaning(), access);
        },
        [&](TemplateDeclaration t) {
            return member_template_declaration(c, t, access);
        },
        [&](StaticAssertDeclaration s) {
            /* [class.mem]/1: a member-declaration */
            return static_assert_declaration(s);
        },
        [&](FunctionDefinition f) {
            return member_declaration_with_specifiers(c, f, access, bodies);
        },
        [&](MemberSimpleDeclaration d) {
            return member_declaration_with_specifiers(c, d, access, bodies);
        },
        [&](MemberDeclaration other) { return not_supported(other); }});
}

/*
 * A member-declaration that is a simple declaration or a
 * function-definition, with the decl-specifier-seq and the declarators it
 * has: a constructor, a destructor, a conversion function, a friend, or a
 * member of any other kind.
 */
analysis_result semantic_analysis::member_declaration_with_specifiers(
    entity_index c, SyntaxRef node, uint8_t access,
    owned_array<node_index> &bodies)
{
    node_index n = node.index();
    /* [ext:gnu_attributes] before it all (libc++'s _LIBCPP_HIDE_FROM_ABI
       friend ...) */
    uint32_t leading = leading_gnu_attributes(n);
    if (leading < node.size() &&
        node[leading].is(syntax_kind::decl_specifier_seq)) {
        for (auto x : node[leading].elements())
            if (x.is(standard_keyword::friend_))
                return friend_declaration(c, n, bodies);
    }

    /* [dcl.fct]/18: an abbreviated member function template */
    if (node.is(syntax_kind::function_definition) &&
        is_namespace(entity_table[c].enclosing) && abbreviated_function(n))
        return member_function_template(c, n, n, access);
    if (node_index id = conversion_function_id_of(n))
        return conversion_function_member(c, n, id, access, bodies);

    /* a constructor or destructor: no decl-specifier-seq, or one of
       function-specifiers alone ([class.ctor]/1: "Each decl-specifier in
       the decl-specifier-seq of a constructor declaration (if any) shall
       be friend, inline, constexpr, or an explicit-specifier") */
    /* [ext:gnu_attributes] before it all (special_member_declarator takes
       them) */
    uint32_t lead = leading_gnu_attributes(n);
    uint32_t at = lead;
    bool constexpr_specified = false, virtual_specified = false,
         explicit_specified = false;
    /* [class.mem]/1: member-declarator: declarator virt-specifier-seq_opt;
       a destructor's ([class.virtual]/4, /5): the declarator, its
       virt-specifiers in override_ and final_ */
    bool override_ = false, final_ = false;
    auto special = [&](SyntaxElement d) {
        if (!d.is(syntax_kind::member_declarator) || d.size() < 2)
            return d;
        if (!d[0].is(syntax_kind::noptr_declarator) ||
            !d[0][0].is(syntax_kind::unqualified_id))
            return d;
        bool o = false, f = false;
        for (auto child : d.elements(1))
            if (!virt_specifier(child, o, f))
                return d;
        override_ = o;
        final_ = f;
        return d[0];
    };
    if (node.size() > lead + 1 &&
        node[lead].is(syntax_kind::decl_specifier_seq) &&
        function_specifiers_only(node[lead].index(), constexpr_specified,
                                 &virtual_specified, &explicit_specified) &&
        declares_special_member(c, special(node[lead + 1])))
        at = lead + 1;
    if (lead == node.size() || at > lead ||
        !node[lead].is(syntax_kind::decl_specifier_seq)) {
        /* [class.mem]/12: "The decl-specifier-seq may be omitted in
           constructor, destructor, and conversion function declarations
           only" */
        if (!node.size() || !declares_special_member(c, special(node[at]))) {
            /* a conversion function ([class.conv.fct]) has no rules yet:
               reported at its conversion-function-id */
            owned_array<node_index> work;
            for (auto x : node.elements())
                if (x.is_node() && !x.is(syntax_kind::function_body))
                    work.append(x.index());
            while (!work.empty()) {
                auto k = syntax(work.back());
                work.resize(work.size() - 1);
                if (k.is(syntax_kind::conversion_function_id))
                    return not_supported(k, "class.conv.fct");
                for (auto x : k.elements())
                    if (x.is_node())
                        work.append(x.index());
            }
            return not_supported(n, "class.ctor");
        }
        if (node.is(syntax_kind::member_declaration)) {
            if (node.size() != at + 2 ||
                !node[at + 1].is(preprocessing_op_or_punc::SEMICOLON))
                /* = default, = delete, a pure-specifier */
                return not_supported(n, "dcl.fct.def");
            virt_override = override_;
            virt_final = final_;
            analysis_result r = special_member_declarator(
                c, n, special(node[at]), access, false, constexpr_specified,
                virtual_specified, explicit_specified);
            virt_override = virt_final = false;
            return r;
        }
        auto body = node.function_definition().function_body();
        if (node.size() != at + 2 || !body || body.function_try_block())
            return not_supported(n, "dcl.fct.def.general");
        if (body.defaulted() || body.deleted()) {
            /* = default, = delete: no user-provided function
               ([dcl.fct.def.default]/5), which the layout ([abi:pod]) and
               triviality need to know before the class is complete */
            return defaulted_or_deleted_special_member(
                c, n, node[at], node[at + 1], access, constexpr_specified,
                virtual_specified, explicit_specified);
        }
        analysis_result r = special_member_declarator(
            c, n, node[at], access, true, constexpr_specified,
            virtual_specified, explicit_specified);
        if (r == analysis_result::well_formed)
            bodies.append(n);
        return r;
    }
    /* [ext:gnu_attributes] before the decl-specifier-seq: what follows
       them is the declaration */
    auto first = node[lead];
    uint32_t count = node.size() - lead;
    specified s;
    declaration_access = access;
    /* [basic.scope.pdecl]/7.1: class-key identifier ; declares a nested
       class */
    sole_elaborated = 0;
    if (node.is(syntax_kind::member_declaration) && count == 2 &&
        node[lead + 1].is(preprocessing_op_or_punc::SEMICOLON) &&
        first.size() == 1 &&
        first[0].is(syntax_kind::elaborated_type_specifier))
        sole_elaborated = first[0].index();
    bool outer_virtual_allowed = virtual_allowed;
    virtual_allowed = true;
    analysis_result r = decl_specifier_seq(first.index(), s, false);
    virtual_allowed = outer_virtual_allowed;
    sole_elaborated = 0;
    declaration_access = 0;
    for (uint32_t i = 0; i < lead && r == analysis_result::well_formed; i++)
        r = gnu_attribute_specifier(node[i].index(), s);
    if (r != analysis_result::well_formed)
        return r;
    return member_declarators(c, n, s, access, lead + 1, bodies);
}

/*
 * The declarators of the member-declaration (or function-definition) n of
 * class c from node[first] on, after its decl-specifier-seq s.
 */
analysis_result semantic_analysis::member_declarators(
    entity_index c, node_index n, const specified &s, uint8_t access,
    uint32_t first, owned_array<node_index> &bodies)
{
    auto node = syntax(n);
    analysis_result r = analysis_result::well_formed;
    if (node.is(syntax_kind::function_definition)) {
        /* [dcl.fct.def.general]/1: declarator virt-specifier-seq_opt
           function-body */
        uint32_t body_at = first + 1;
        bool override_ = false, final_ = false;
        while (body_at < node.size() &&
               virt_specifier(node[body_at], override_, final_))
            body_at++;
        auto body = node.function_definition().function_body();
        if (node.size() != body_at + 1 || !body || body.function_try_block())
            return not_supported(n, "dcl.fct.def.general");
        if (body_at == first + 1 && body.defaulted())
            return defaulted_copy_assignment(c, n, node[first], s, access);
        /* = delete: member_function_definition's deleted_definition */
        if (!body.deleted() && !body.plain())
            /* a ctor-initializer, a function-try-block, = default */
            return not_supported(n, "dcl.fct.def.general");
        virt_override = override_;
        virt_final = final_;
        virt_pure = false;
        r = member_declarator(c, n, node[first], s, access, true);
        virt_override = virt_final = false;
        if (r == analysis_result::well_formed)
            bodies.append(n);
        return r;
    }
    owned_array<SyntaxElement> declarators;
    for (auto d : node.elements(first)) {
        if (d.is(preprocessing_op_or_punc::SEMICOLON))
            continue;
        if (d.is(syntax_kind::member_declarator_list)) {
            for (auto item : d.elements())
                if (!item.is(preprocessing_op_or_punc::COMMA))
                    declarators.append(item);
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
        auto seq = node[first - 1];
        if (declares_unnamed_class(seq.index()))
            return anonymous_union_member(c, n, s, access);
        /* [class.nest]/1: a nested class's definition, or its declaration
           ([class.nest]/3) */
        if (plain && seq.size() == 1 &&
            seq[0].is(syntax_kind::class_specifier,
                      syntax_kind::elaborated_type_specifier))
            return analysis_result::well_formed;
        /* a nested class, an anonymous union */
        return not_supported(n, "class.mem");
    }
    analysis_result result = analysis_result::well_formed;
    for (SyntaxElement d : declarators)
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
                                     SyntaxElement d, const specified &s,
                                     uint8_t access, bool definition)
{
    auto node = syntax(d);
    if (node.is(syntax_kind::member_declarator)) {
        /* identifier_opt attribute-specifier-seq_opt : constant-expression */
        for (auto x : node.elements())
            if (x.is(preprocessing_op_or_punc::COLON))
                return member_bit_field(c, declaration, d, s, access);
        /* declarator virt-specifier-seq_opt pure-specifier_opt */
        {
            bool override_ = false, final_ = false, pure = false;
            uint32_t i = 1;
            while (i < node.size() &&
                   virt_specifier(node[i], override_, final_))
                i++;
            if (i < node.size() && node[i].is(syntax_kind::pure_specifier)) {
                pure = true;
                i++;
            }
            if (i > 1 && i == node.size()) {
                virt_override = override_;
                virt_final = final_;
                virt_pure = pure;
                analysis_result r = member_declarator(c, declaration, node[0],
                                                      s, access, definition);
                virt_override = virt_final = virt_pure = false;
                return r;
            }
            if (i > 1)
                return not_supported(d.index(), "class.mem");
        }
        /* declarator brace-or-equal-initializer: [class.mem]/10 "A
           brace-or-equal-initializer shall appear only in the declaration
           of a data member"; for a non-static one it is a default member
           initializer, analysed where a constructor uses it
           ([class.base.init]/9), a complete-class context ([class.mem]/6) */
        if (node.size() != 2 ||
            !node[1].is(syntax_kind::brace_or_equal_initializer,
                        syntax_kind::braced_init_list))
            return not_supported(d.index(), "class.mem");
        last_declared = 0;
        analysis_result r =
            member_declarator(c, declaration, node[0], s, access, definition);
        if (r != analysis_result::well_formed)
            return r;
        entity_index e = last_declared;
        if (e && entity_table[e].kind == entity_kind::variable &&
            !entity_table[e].nonstatic_member)
            return static_member_initializer(d.index(), e, node[1], s);
        if (!e || entity_table[e].kind != entity_kind::variable)
            /* a pure-specifier ([class.abstract]) */
            return not_supported(d.index(), "class.abstract");
        if (entity_table[c].union_ || !entity_table[c].name)
            /* [class.union.anon]: at most one variant member's; an unnamed
               class's members are initialized by no constructor of its own */
            return not_supported(d.index(), "class.union");
        entity_table[e].default_initializer = node[1].index();
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
        if (p.default_argument)
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
    /* [ext:gnu_asm_labels] a member's: not yet */
    if (m.asm_label)
        return not_supported(declaration, "ext.gnu");
    if (!m.abi_tag)
        m.abi_tag = s.abi_tag;
    if (!s.has_type)
        return not_supported(declaration, "dcl.type");
    if (s.typedef_)
        return member_typedef(c, declaration, m.id, type, s, m, access);
    /* [dcl.constexpr]/1: "The constexpr specifier shall be applied only to
       the definition of a variable or variable template or the declaration
       of a function or function template": a static data member's is not
       analysed yet */
    if (s.constexpr_ && !s.static_ &&
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
    /* [dcl.fct.spec]/2: "The virtual specifier shall be used only in the
       initial declaration of a non-static class member function" */
    if (s.virtual_ && !is_function)
        return ill_formed_at_token(m.id,
                                   "the virtual specifier shall be used only "
                                   "in the declaration of a non-static "
                                   "member function",
                                   "dcl.fct.spec");
    if ((is_function && (m.aligned || (m.gnu_flags & gnu_packed))) ||
        (m.gnu_flags & gnu_used))
        return not_supported(declaration, "ext.gnu");
    /* a member's symbol attributes (a member function's weak and section,
       a static data member's, a data member's function attributes) are not
       analysed yet; on a non-static data member they are GCC's error */
    if ((m.gnu_flags & (gnu_weak | gnu_used | gnu_section)) ||
        (!is_function &&
         (m.gnu_flags & (gnu_inline_flags | gnu_function_symbol_flags))))
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
            /* conversion functions to different types have different
               names ([class.conv.fct]/1), here one */
            if (m.conversion_function && x.type != type)
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
        /* [dcl.constexpr]/3 (and clang before C++20 for any non-static
           member function): not in a class with virtual bases */
        if (s.constexpr_ && !s.static_ && entity_table[c].virtual_base_count)
            return not_supported(declaration, "dcl.constexpr");
        entity_table[e].inline_ = definition || s.inline_ || s.constexpr_;
        /* [dcl.constexpr]/1: constexpr makes it inline; its body's rules
           and its invocations in constant expressions are the evaluator's
           (S6) */
        entity_table[e].constexpr_ = s.constexpr_;
        entity_table[e].defined = definition;
        /* [ext:gnu_attributes] its symbol's */
        entity_table[e].gnu_flags |=
            m.gnu_flags & (gnu_function_symbol_flags | gnu_inline_flags);
        entity_table[e].abi_tag = m.abi_tag;
        if (m.operator_function) {
            entity_table[e].operator_function = true;
            entity_table[e].implemented = m.implemented;
            entity_table[e].allocation = m.allocation;
            analysis_result checked =
                operator_function_declaration(declaration, e);
            if (checked != analysis_result::well_formed)
                return checked;
            /* (one deleted on this, its first, declaration: not counted,
               layout's pod_for_layout reads the count at the class's end,
               before the bodies) */
            bool deleted_here = definition && syntax(declaration)
                                                  .function_definition()
                                                  .function_body()
                                                  .deleted();
            if (copy_assignment_operator(e, entity_table[e].enclosing) &&
                !deleted_here)
                entity_table[entity_table[e].enclosing].user_copy_assignments++;
        }
        if (m.conversion_function) {
            /* [class.conv.fct]/1: "A conversion function shall have no
               non-object parameters" */
            const struct type &f = type_table[type.unqualified()];
            if (f.parameter_count || f.variadic)
                return ill_formed_at_token(m.id,
                                           "a conversion function shall have "
                                           "no non-object parameters",
                                           "class.conv.fct");
            entity_table[e].conversion_function = true;
            entity_table[e].explicit_ = s.explicit_;
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
        analysis_result v = virtual_member(c, e, declaration, m.id, s.virtual_);
        if (v != analysis_result::well_formed)
            return v;
    } else if (has_defaults) {
        /* /3: a default argument of a member that is not a function */
        return not_supported(declaration, "dcl.fct.default");
    } else if (s.static_) {
        /* [class.static.data]/3: "The declaration of a non-inline static
           data member in its class definition is not a definition and may
           be of an incomplete type other than cv void." */
        /* an inline static data member ([dcl.inline]/6: defined in the
           class) has rules when it is const too: the constexpr path */
        if (s.inline_ && !s.constexpr_ && !(type.cv() & cv_const))
            return not_supported(declaration, "class.static.data");
        /* [dcl.constexpr]/10: "A constexpr specifier used in an object
           declaration declares the object as const"; [dcl.constexpr]/1 a
           constexpr static data member is an inline variable, defined by
           its declaration in the class (its initializer:
           static_member_initializer) */
        if (s.constexpr_)
            type =
                qualified_type::make(type.unqualified(), type.cv() | cv_const);
        if (local_class_rule(c))
            /* [class.local]/4: "A local class cannot have static data
               members" (a class nested in one neither) */
            return not_supported(declaration, "class.static.data");
        const struct type &record = type_table[type.unqualified()];
        if (record.kind == type_kind::lvalue_reference ||
            record.kind == type_kind::rvalue_reference || abstract_class(type))
            return not_supported(declaration, "class.static.data");
        e = entity_table.make(entity_kind::variable, name, c);
        entity_table[e].type = type;
        /* [class.static.data]/1: "A static data member is not part of the
           subobjects of a class." */
        entity_table[e].nonstatic_member = false;
        entity_table[e].defined = false;
        entity_table[e].alignment = static_cast<uint32_t>(m.aligned);
        /* its first declaration, which a name found refers to (the
           definition outside the class is a later one) */
        declared_name first;
        first.declaration = declaration;
        first.outermost = current_declaration;
        first.entity = e;
        first.id = m.id;
        first.kind = entity_kind::variable;
        first.type = type;
        first.claimable = declarations_claimable;
        declared.append(first);
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
        if (entity_table[c].union_ &&
            (record.kind == type_kind::lvalue_reference ||
             record.kind == type_kind::rvalue_reference) &&
            !tokens.files.behavior.microsoft_extensions)
            /* [class.union]/4: "If a union contains a non-static data
               member of reference type the program is ill-formed." (a
               Microsoft extension allows it) */
            return ill_formed_at_token(m.id,
                                       "a union shall not have a non-static "
                                       "data member of reference type",
                                       "class.union");
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
        /* [basic.def]/5 (a member's declaration defines it,
           [basic.def]/2) */
        if (abstract_class(type))
            return ill_formed_at_token(m.id,
                                       "the type of a non-static data member "
                                       "shall not be an abstract class type",
                                       "basic.def");
        if (is_void || (record.kind == type_kind::array && !record.bound_known))
            /* incomplete too; an array of unknown bound last is clang's
               flexible array member extension */
            return not_supported(declaration, "class.mem");
        if (!size)
            return not_supported(declaration, "class.mem");
        uint32_t alignment = static_cast<uint32_t>(alignment_of_type(type));
        if (!alignment)
            return not_supported(declaration, "class.mem");
        if (entity_table[c].union_ && element_class(type) &&
            !trivial_special_members(element_class(type)))
            /* a member whose special member functions make the union's
               deleted ([class.union]/4 Note 3) */
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
 * [dcl.typedef]/1: a typedef-name declared in a member-specification (or
 * by an alias-declaration there, [dcl.typedef]/2) is a member of the class
 * ([class.mem]/3: "Members of a class are data members, member functions
 * ..., nested types, enumerators, and member templates"), with the access
 * of its declaration.  [class.mem]/20: "every member of class T that is
 * itself a type" shall have a name different from T.  A redeclaration in
 * the class ([dcl.typedef]/3 allows one at namespace scope; [class.mem]/5
 * not in a class) and attributes on it have no rules yet.
 */
analysis_result semantic_analysis::member_typedef(
    entity_index c, node_index declaration, token_index id, qualified_type type,
    const specified &s, const declarator_meaning &m, uint8_t access)
{
    if (s.static_ || s.inline_ || s.constexpr_ || s.virtual_ || s.gnu_flags ||
        s.aligned || m.aligned || m.gnu_flags || m.section || m.asm_label)
        return not_supported(declaration, "dcl.typedef");
    uint32_t name = tokens.identifier(id);
    if (name == entity_table[c].name)
        return ill_formed_at_token(id,
                                   "every member of a class that is itself "
                                   "a type shall have a name different from "
                                   "the class",
                                   "class.mem");
    if (entity_table.find(c, name))
        return not_supported(declaration, "dcl.typedef");
    entity_index e = entity_table.make(entity_kind::typedef_name, name, c);
    entity_table[e].type = type;
    entity_table[e].defined = true;
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
                                                        node_index n)
{
    auto definition = syntax(n).function_definition();
    if (entity_index f = friend_defined_by(definition, c))
        return friend_function_body(c, definition, f);
    current_declaration = definition;
    /* [basic.scope.class]/1: the declaration is in the class's scope, its
       names (a member type as the return type) found there */
    entity_index scope = current_scope;
    current_scope = c;
    analysis_result r = member_function_definition(c, definition);
    current_scope = scope;
    return r;
}

analysis_result
semantic_analysis::member_function_definition(entity_index c,
                                              FunctionDefinition definition)
{
    /* [ext:gnu_attributes] before it all: member_declaration took them */
    uint32_t lead = leading_gnu_attributes(definition);
    /* a conversion function ([class.conv.fct]): no parameters for the
       declarator to bind, its body the function-body */
    if (entity_index f = conversion_function_declared_by(c, definition)) {
        last_declared = f;
        auto body = definition.function_body();
        if (body.deleted())
            return deleted_definition(definition, f);
        if (!body.plain())
            return not_supported(definition, "dcl.fct.def.general");
        entity_index saved_class = defining_class;
        defining_class = c;
        analysis_result r =
            analyse_function_body(definition, body.compound_statement());
        defining_class = saved_class;
        return r;
    }
    bool constexpr_specified = false, body_virtual = false,
         body_explicit = false;
    uint32_t at = 0;
    auto seq = definition.decl_specifier_seq();
    if (seq && definition.size() > leading_gnu_attributes(definition) + 2 &&
        function_specifiers_only(seq.index(), constexpr_specified,
                                 &body_virtual, &body_explicit) &&
        declares_special_member(c, definition.declarator()))
        at = 1;
    if (at || !seq) {
        /* a constructor or destructor: the one this definition declared */
        qualified_type type = qualified_type::make(
            type_table.fundamental(fundamental_type::void_), cv_none);
        declarator_meaning m;
        function_cv = 0;
        in_member_declarator = true;
        entity_index saved_class = defining_class;
        defining_class = c;
        analysis_result r =
            declarator(definition, definition[lead + at], type, m);
        defining_class = saved_class;
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
        return special_member_body(definition,
                                   definition[lead + at + 1].function_body());
    }
    /* [basic.scope.class]: the declarator's names are looked up in the
       class, as when its declaration was analysed (a class template's
       parameters are bound there) */
    entity_index saved_class = defining_class;
    defining_class = c;
    specified s;
    bool outer_virtual_allowed = virtual_allowed;
    virtual_allowed = true;
    analysis_result r = decl_specifier_seq(seq.index(), s, false);
    virtual_allowed = outer_virtual_allowed;
    qualified_type type = s.type;
    declarator_meaning m;
    if (r == analysis_result::well_formed) {
        function_cv = 0;
        in_member_declarator = true;
        r = declarator(definition, definition.declarator(), type, m);
        in_member_declarator = false;
    }
    defining_class = saved_class;
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
    /* the function-body after any virt-specifier-seq */
    auto body = definition.function_body();
    if (body.deleted())
        return deleted_definition(definition, last_declared);
    if (!body.plain())
        return not_supported(definition, "dcl.fct.def.general");
    return analyse_function_body(definition, body.compound_statement());
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
 * [class.static.data]/4: "If a non-volatile non-inline const static data
 * member is of integral or enumeration type, its declaration in the class
 * definition can specify a brace-or-equal-initializer in which every
 * initializer-clause that is an assignment-expression is a constant
 * expression ([expr.const]).  The member shall still be defined in a
 * namespace scope if it is odr-used in the program and the namespace scope
 * definition shall not contain an initializer."  [expr.const]/3: such a
 * member is usable in constant expressions.  An inline or constexpr one
 * (an inline variable, R9) is not analysed.
 */
analysis_result
semantic_analysis::static_member_initializer(node_index declarator,
                                             entity_index e, SyntaxElement init,
                                             const specified &s)
{
    qualified_type type = entity_table[e].type;
    uint32_t cv = cv_of(type_table, type);
    const struct type record = type_table[type.unqualified()];
    if (!(cv & cv_const) || (cv & cv_volatile) ||
        record.kind != type_kind::fundamental || !is_integral(type))
        /* an enumeration's too, which analysis does not fold yet */
        return not_supported(declarator, "class.static.data");
    initialized_value value;
    value.evaluation = constness::unknown;
    value.value = 0;
    analysis_result r = initializer(init, type, value);
    if (r != analysis_result::well_formed)
        return r;
    if (value.evaluation == constness::not_constant)
        return ill_formed(declarator,
                          "every initializer-clause of a static data "
                          "member's in-class initializer shall be a "
                          "constant expression",
                          "class.static.data");
    if (value.evaluation != constness::constant)
        return not_supported(declarator, "expr.const");
    entity &x = entity_table[e];
    x.usable = constness::constant;
    x.value = value.value;
    x.definition = value.node;
    if (s.constexpr_ || s.inline_) {
        /* [dcl.inline]/1 (C++17): "A function or static data member
           declared with the constexpr specifier is implicitly an inline
           function or variable"; this declaration (of it or of an inline
           one) is its definition ([basic.def]/2.3), and an odr-use emits it
           (COMDAT, as clang) */
        if (tokens.files.behavior.edition < standard_edition::cplusplus_17)
            return not_supported(declarator, "class.static.data");
        x.inline_ = true;
        x.constexpr_ = s.constexpr_;
        x.defined = true;
    }
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
    if (s.typedef_)
        return not_supported(declaration, "class.static.data");
    /* [dcl.inline]/6 and [class.static.data]/3: an inline or constexpr
       definition makes it an inline variable (C++17) */
    if ((s.inline_ || s.constexpr_) &&
        tokens.files.behavior.edition < standard_edition::cplusplus_17)
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
    if (s.inline_ || s.constexpr_) {
        entity_table[e].inline_ = true;
        entity_table[e].constexpr_ |= s.constexpr_;
    }
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

/*
 *     virt-specifier-seq:
 *         virt-specifier
 *         virt-specifier-seq virt-specifier
 *     virt-specifier:
 *         override
 *         final
 *
 * One virt-specifier is its token; more are a virt-specifier-seq node.
 */
bool semantic_analysis::virt_specifier(SyntaxElement d, bool &override_,
                                       bool &final_) const
{
    auto one = [&](SyntaxElement t) {
        if (!t.is_identifier())
            return false;
        owned_buffer spelled;
        tokens.spelling(t.token(), spelled.get());
        if (spelled.size() == 8 && !memcmp(spelled.data(), "override", 8)) {
            override_ = true;
            return true;
        }
        if (spelled.size() == 5 && !memcmp(spelled.data(), "final", 5)) {
            final_ = true;
            return true;
        }
        return false;
    };
    if (d.is_token())
        return one(syntax(d));
    if (!syntax(d).is(syntax_kind::virt_specifier_seq))
        return false;
    for (auto t : syntax(d).elements())
        if (!one(t))
            return false;
    return true;
}

/* a class-head's class-head-name (or what is in its place), after the
   class-key and alignment-specifiers; none in an unnamed class's */
SyntaxElement semantic_analysis::class_head_name(node_index head) const
{
    auto h = syntax(head);
    for (auto c : h.elements(1)) {
        if (c.is(syntax_kind::alignment_specifier) ||
            (c.is_node() && gnu_or_without_effect(c.index())))
            continue;
        bool only_alignment = c.is(syntax_kind::attribute_specifier_seq);
        for (auto k : c.elements())
            only_alignment =
                only_alignment && k.is(syntax_kind::alignment_specifier);
        if (!only_alignment)
            return c;
    }
    return SyntaxElement();
}

/*
 * [dcl.align]/2: "When the alignment-specifier is of the form alignas(
 * constant-expression ): the constant-expression shall be an integral
 * constant expression; if the constant expression does not evaluate to an
 * alignment value, or evaluates to an extended alignment and the
 * implementation does not support that alignment in the context of the
 * declaration, the program is ill-formed."  /3: "An alignment-specifier of
 * the form alignas( type-id ) has the same effect as alignas(alignof(
 * type-id ))."  /4: zero has no effect.  A pack expansion: no rules yet.
 */
analysis_result semantic_analysis::alignment_specifier(AlignmentSpecifier n,
                                                       uint64_t &alignment)
{
    alignment = 0;
    auto parts = syntax(n);
    if (parts.size() != 4)
        return not_supported(n, "dcl.align");
    if (parts[2].is(syntax_kind::type_id)) {
        qualified_type t;
        analysis_result r = type_id(parts[2].index(), t);
        if (r != analysis_result::well_formed)
            return r;
        const struct type &record = type_table[t.unqualified()];
        if (record.kind == type_kind::function ||
            (record.kind == type_kind::array && !record.bound_known))
            return not_supported(n, "dcl.align");
        alignment = alignment_of_type(t);
        /* an incomplete type, void: not known */
        if (!alignment)
            return not_supported(n, "dcl.align");
        return analysis_result::well_formed;
    }
    expression_meaning e;
    analysis_result r = expression(parts[2], e);
    if (r != analysis_result::well_formed)
        return r;
    if (!is_integral(e.type) || e.category != value_category::prvalue)
        return not_supported(n, "dcl.align");
    bool constant = false;
    r = constant_expression(n, e, constant);
    if (r != analysis_result::well_formed)
        return r;
    if (!constant)
        return not_supported(n, "dcl.align");
    alignment = e.value;
    /* [basic.align]/4: "Every alignment value shall be a non-negative
       integral power of two"; clang's limit 2^29 */
    if (alignment && ((alignment & (alignment - 1)) || alignment > (1u << 28)))
        return ill_formed(n, "an alignment shall be a power of two",
                          "dcl.align");
    return analysis_result::well_formed;
}

/* class c's members one range of class_members again, at its end, after
   a class whose analysis began within c's (a nested or local class, a
   specialization instantiated) added its own after c's so far */
void semantic_analysis::make_members_contiguous(entity_index c)
{
    const entity &x = entity_table[c];
    if (x.first_member + x.member_count == class_members.size())
        return;
    uint32_t first = static_cast<uint32_t>(class_members.size());
    uint32_t old_first = x.first_member, count = x.member_count;
    for (uint32_t i = 0; i < count; i++)
        class_members.append(class_members[old_first + i]);
    entity_table[c].first_member = first;
}

} // namespace cplusplus
