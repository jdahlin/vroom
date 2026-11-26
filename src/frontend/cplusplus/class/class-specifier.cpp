/*
 * class-specifier.cpp: [class.pre] Classes: the analysis of a class-specifier
 * and of an elaborated-type-specifier that names a class (the grammar is
 * declaration/decl-specifier.cpp's, elaborated_or_class_specifier).
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
    class_kept_ill_formed = false;
    analysis_result r = class_definition(n, type);
    bool kept = class_kept_ill_formed;
    class_kept_ill_formed = false;
    auto head = n.class_specifier().class_head();
    if (r == analysis_result::well_formed || !head || kept)
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
            note_held(e);
        } else if (entity_table[e].kind == entity_kind::class_type) {
            entity_table[e].kind = entity_kind::none;
            entity_table[e].size = 0;
            note_held(e);
        }
        break;
    }
    return r;
}

/* whether a decl-specifier-seq has a class-specifier of an unnamed
   class */
bool semantic_analysis::declares_unnamed_class(SyntaxRef seq) const
{
    for (auto c : seq.elements()) {
        auto head = c.class_specifier().class_head();
        if (head && head.size() == 1)
            return true;
        /* (GNU attributes after the class-key: struct __attribute__((packed))
           { ... };) */
        if (head && head.size() == 2 &&
            head[1].is(syntax_kind::gnu_attribute_specifier))
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
                                                          SyntaxRef n,
                                                          const specified &s,
                                                          uint8_t access)
{
    const struct type record = type_table[s.type.unqualified()];
    /* [class.union.anon]/3: an anonymous union declared in a namespace
       shall be declared static (or be in an unnamed namespace, not
       analysed): a variable of internal linkage, its members names of the
       namespace */
    bool in_namespace = is_namespace(c);
    if (in_namespace && !s.static_)
        return not_supported_because(
            n, "class.union.anon",
            "a namespace-scope anonymous union that is not static");
    if (s.typedef_ || (s.static_ && !in_namespace) || s.extern_ || s.mutable_ ||
        s.inline_ || s.constexpr_ || s.type.cv() || s.aligned || s.gnu_flags ||
        record.kind != type_kind::class_type)
        return not_supported_because(
            n, "class.union.anon",
            "an anonymous union with a storage class, specifier, cv-qualifier "
            "or attribute, or not of class type");
    entity_index u = record.entity;
    const entity &x = entity_table[u];
    if (x.base_count || !size_of_type(s.type))
        return not_supported_because(
            n, "class.union.anon",
            "an anonymous union with a base class or no size");
    if (in_namespace && !x.union_)
        return not_supported_because(n, "class.union.anon",
                                     "a namespace-scope anonymous struct");
    for (entity_index f = u + 1; f < entity_table.size(); f++) {
        const entity &member = entity_table[f];
        if (member.enclosing == u && member.kind == entity_kind::variable &&
            !member.nonstatic_member)
            /* [class.union.anon]/1: "Each member-declaration in the
               member-specification of an anonymous union shall either
               define a non-static data member or be a
               static_assert-declaration" */
            return ill_formed(n,
                              "static data member not allowed in anonymous "
                              "union",
                              "class.union.anon");
    }
    entity_index e = entity_table.make(entity_kind::variable, 0, c);
    entity_table[e].type = s.type;
    if (in_namespace) {
        entity_table[e].defined = true;
        entity_table[e].linkage_of_name = linkage::internal;
        entity_table.add_declaration(e, n);
        entity_table[u].anonymous_object = e;
        return bind_anonymous_members(c, n, u);
    }
    entity_table[e].nonstatic_member = true;
    entity_table[e].defined = true;
    entity_table[e].field = entity_table[c].member_count;
    entity_table[e].linkage_of_name = entity_table[c].linkage_of_name;
    entity_table[e].access = access;
    entity_table.add_declaration(e, n);
    class_members.append(e);
    entity_table[c].member_count++;
    entity_table[u].anonymous_object = e;
    if (entity_table[c].union_ && variant_initialized(e))
        /* [class.union]/2: a variant member of an anonymous union within
           the union counts among its variant members */
        for (uint32_t i = 0; i + 1 < entity_table[c].member_count; i++)
            if (variant_initialized(
                    class_members[entity_table[c].first_member + i]))
                return ill_formed(n, "initializing multiple members of union",
                                  "class.union");
    return bind_anonymous_members(c, n, u);
}

/* whether member m has a default member initializer, or is an anonymous
   union's (or struct's) object holding a member that has one */
bool semantic_analysis::variant_initialized(entity_index m) const
{
    if (entity_table[m].default_initializer)
        return true;
    const struct type &t = type_table[entity_table[m].type.unqualified()];
    if (t.kind != type_kind::class_type ||
        entity_table[t.entity].anonymous_object != m)
        return false;
    const entity &x = entity_table[t.entity];
    for (uint32_t i = 0; i < x.member_count; i++)
        if (variant_initialized(class_members[x.first_member + i]))
            return true;
    return false;
}

/* the names of the members of anonymous union u, and of the anonymous
   unions among them, bound in c ([class.union.anon]/1) */
analysis_result semantic_analysis::bind_anonymous_members(entity_index c,
                                                          SyntaxRef n,
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
                VTRY(r, bind_anonymous_members(c, n, inner));
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
            return not_supported_because(
                n, "class.union.anon",
                "an anonymous union member whose name is already declared");
        entity_table.bind(c, member.name, m);
    }
    return analysis_result::well_formed;
}

/* (class_definition) */
analysis_result semantic_analysis::class_definition(SyntaxRef n,
                                                    type_index &type)
{
    /* a class template's specialization defined while another class is
       (postpone_member_bodies, define_class_instantiation) */
    bool postpone = postpone_member_bodies;
    postpone_member_bodies = false;
    entity_index postponed_scope = current_scope;
    SyntaxRef postponed_declaration = current_declaration;
    auto head = n.class_specifier().class_head();
    if (!head)
        return not_supported_because(n, "class.pre",
                                     "a class-specifier without a class-head");
    /* class-key attribute-specifier-seq_opt class-head-name ...: the
       alignment-specifiers left out of head_children, kept in alignas */
    owned_array<SyntaxElement> head_list;
    owned_array<SyntaxRef> alignas_list;
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
                alignas_list.append(k);
        } else if (c.is(syntax_kind::alignment_specifier)) {
            alignas_list.append(c);
        } else if (class_head_attributes_without_effect(c)) {
            /* [dcl.attr.deprecated] and the like: nothing generated */
        } else if (c.is_node() && gnu_or_without_effect(c)) {
            /* one attribute-specifier, or a seq of them */
            if (c.is(syntax_kind::attribute_specifier_seq)) {
                for (auto each : c.elements()) {
                    VTRY(r, gnu_attribute_specifier(each, gnu));
                }
            } else {
                VTRY(r, gnu_attribute_specifier(c, gnu));
            }
        } else {
            /* [dcl.attr.fallthrough]/1, [dcl.attr.likelihood]/1: an
               attribute after the class-key appertains to the class; these
               "may be applied to" a statement only (clang: "cannot be
               applied to a declaration") */
            if (c.is_node() && (c.is(syntax_kind::attribute_specifier_seq) ||
                                c.is(syntax_kind::attribute_specifier)))
                if (token_index t = statement_only_attribute(c.first_token(),
                                                             c.end_token()))
                    return ill_formed_at_token(
                        t, "this attribute cannot be applied to a declaration",
                        "dcl.attr");
            head_list.append(c);
        }
    }
    /* (packed packs each member: the class's own, before the name or after
       the closing brace; aligned in the class-head is not analysed) */
    if (gnu.aligned || gnu.section || gnu.priority || gnu.cleanup ||
        (gnu.flags & ~(gnu_hidden_visibility | gnu_abi_tag |
                       gnu_exclude_from_explicit_instantiation | gnu_packed |
                       gnu_dllimport)))
        return not_supported_because(
            n, "ext.gnu",
            "a class-head attribute other than the handled ones, such as "
            "aligned or section");
    const SyntaxElement *head_children = head_list.data();
    uint32_t head_count = static_cast<uint32_t>(head_list.size());
    if (!head_count || !head_children[0].is_token())
        return not_supported_because(
            n, "class.pre",
            "a class-head that does not start with a class-key");
    /* [class.pre]/1: "A class-specifier whose class-head omits the
       class-head-name defines an unnamed class." */
    bool unnamed = head_count == 1;
    /* class-head-name: nested-name-specifier_opt class-name */
    token_index head_name = 0;
    SyntaxRef head_qualifier = 0;
    SyntaxElement name_syntax;
    if (!unnamed)
        name_syntax = head_children[1];
    if (name_syntax.is_identifier()) {
        head_name = name_syntax.token();
    } else if (class_instantiating && name_syntax.simple_template_id()) {
        /* a partial or explicit specialization's definition, instantiated
           (instantiate_class): the template's name */
        head_name = name_syntax[0].token();
    } else if (auto qualified = name_syntax.qualified_class_head_name()) {
        if (!qualified.nested_name_specifier())
            return not_supported_because(
                n, "temp.spec",
                "a qualified class-head-name without a nested-name-specifier");
        if (qualified.identifier()) {
            head_qualifier = qualified.nested_name_specifier().index();
            head_name = qualified.identifier().token();
        } else if (class_instantiating && name_syntax.size() == 2 &&
                   name_syntax[1].is(syntax_kind::simple_template_id)) {
            head_qualifier = qualified.nested_name_specifier().index();
            head_name = name_syntax[1][0].token();
        } else {
            return not_supported_because(
                n, "temp.spec",
                "a qualified class-head-name that ends in neither an "
                "identifier nor an instantiated template-id");
        }
    } else if (!unnamed) {
        return not_supported_because(
            n, "class.pre",
            "the class-head-name is not an identifier or a "
            "qualified name (an attribute that is not one "
            "without effect, or another form)");
    }
    bool is_final = false;
    uint32_t next = 2;
    if (!unnamed && head_count > next && head_children[next].is_identifier()) {
        owned_buffer spelling;
        tokens.spelling(head_children[next].token(), spelling.get());
        if (spelling.size() == 5 && !memcmp(spelling.data(), "final", 5)) {
            is_final = true;
            next++;
        }
    }
    bool has_bases =
        head_count == next + 1 && !unnamed && head_children[next].base_clause();
    if (head_count > next && !has_bases)
        /* a class-virt-specifier, a base-clause of an unnamed class */
        return not_supported_because(
            n, "class.derived",
            "extra class-head parts, such as bases on an unnamed class or an "
            "unhandled virt-specifier");
    standard_keyword key = head_children[0].keyword();
    if (!is_class_key(key))
        return not_supported_because(
            n, "class.pre", "a class-head whose first keyword is no class-key");
    /* #pragma pack and the like change the layout: no rules yet, for the
       classes in a region of one */
    if (tokens.layout_pragma_at(n.first_token()))
        return not_supported_because(n, "cpp.pragma",
                                     "a class in a region of a layout pragma");
    /* [class.union]/1: "A union shall not have base classes." (clang's
       error) */
    if (key == standard_keyword::union_ && has_bases)
        return not_supported_because(n, "class.union",
                                     "a union with a base-clause");
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
    if (head_qualifier && !class_instantiating) {
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
            return not_supported_because(
                n, "class.pre",
                "a qualified class definition inside a class or function");
        nominated_scope q;
        VTRY(qr, nested_name_specifier(head_qualifier, q));
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
                    note_held(y);
                    entity_table[y].size = 0;
                }
                return not_supported_because(
                    n, "class.nest",
                    "a nested class definition qualified by a class that "
                    "cannot take it");
            }
            scope = q.entity;
        } else {
            lookup_result found = lookup_in_namespace(
                q.entity, tokens.identifier(head_name), lookup_filter::types);
            if (!found.entity ||
                entity_table[found.entity].kind != entity_kind::class_type ||
                entity_table[found.entity].enclosing != found.found_in)
                return not_supported_because(n, "class.pre",
                                             "a qualified class definition "
                                             "with no matching class found");
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
                note_held(found.entity);
                entity_table[found.entity].size = 0;
                return not_supported_because(
                    n, "class.pre",
                    "a qualified class definition in a namespace that may not "
                    "define it");
            }
            scope = found.found_in;
        }
    }
    /* a class template's specialization (instantiate_class): a class of
       the template's scope that no name binds there */
    uint32_t specialization = class_instantiating;
    class_instantiating = 0;
    token_index id = head_children[0].token();
    uint32_t name = 0;
    entity_index c = 0;
    if (specialization) {
        if (unnamed || outer)
            return not_supported_because(
                n, "temp.class",
                "an unnamed or nested class as a template specialization");
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
                note_held(c);
                entity_table[c].size = 0;
            }
            return not_supported_because(
                n, "namespace.udecl",
                "a class definition of a name bound by a using-declaration");
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
        /* [impl.clang] one that is a member of a class has the class's
           linkage all the same: its Ut _ name is the same in every
           translation unit, and a specialization with it as an argument
           is the template's (func<foo::Ut_> linkonce_odr) */
        if (scope && entity_table[scope].kind == entity_kind::class_type)
            entity_table[c].linkage_of_name =
                entity_table[scope].linkage_of_name;
    } else if (c) {
        redeclared = true;
        entity &prior = entity_table[c];
        if (prior.kind == entity_kind::none)
            return not_supported_because(n, "basic.scope.declarative",
                                         "a class definition of a name whose "
                                         "declaration was not analysed");
        if (prior.kind != entity_kind::class_type)
            /* a class and a variable or function of one name
               ([basic.scope.hiding]/2) */
            return not_supported_because(
                n, "basic.scope.hiding",
                "a class with the name of an entity that is no class");
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
            return not_supported_because(
                n, "dcl.type.elab",
                "a class definition whose class-key disagrees about union with "
                "its declaration");
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
    /* [temp.friend]/4: member classes of a specialization befriended */
    member_friends_of(c, false);
    entity_table[c].final_ = is_final;
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
    /* [temp.spec]/6: a member-specification is checked */
    flag_set checked(explicit_declaration_names, false);
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
    if (has_bases) {
        require_complete_class++;
        result = base_clause(c, head_children[next], key);
        require_complete_class--;
    }
    entity_table[c].requested_alignment = trailing_class_alignment;
    entity_table[c].gnu_flags |= trailing_class_flags;
    trailing_class_alignment = 0;
    trailing_class_flags = 0;
    for (SyntaxRef a : alignas_list) {
        uint64_t alignment = 0;
        if (result == analysis_result::well_formed)
            result = alignment_specifier(a, alignment);
        if (alignment > entity_table[c].requested_alignment)
            entity_table[c].requested_alignment =
                static_cast<uint32_t>(alignment);
    }
    if (result != analysis_result::well_formed) {
        resume_outer();
        entity_table[c].kind = entity_kind::none;
        note_held(c);
        entity_table[c].size = 0;
        return result;
    }
    owned_array<SyntaxRef> bodies;
    entity_index first_member_entity =
        static_cast<entity_index>(entity_table.size());
    size_t nested_start = nested_bodies.size();
    auto spec = n.class_specifier().member_specification();
    token_index outer_members_first = defining_members_first;
    token_index outer_members_end = defining_members_end;
    owned_array<SyntaxRef> *outer_bodies = defining_bodies;
    entity_index outer_bodies_class = defining_bodies_class;
    defining_bodies = &bodies;
    defining_bodies_class = c;
    defining_members_first = spec.first_token();
    defining_members_end = spec.end_token();
    for (auto member : spec.elements()) {
        if (member.is_token()) {
            switch (member.keyword()) {
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
            result = worse(result, not_supported_because(
                                       n, "class.mem",
                                       "a token in a member-specification that "
                                       "is no access specifier or colon"));
            continue;
        }
        auto declaration = member.member_declaration();
        if (!declaration) {
            /* a node the grammar does not place here (unparsed, an
               explicit instantiation kept as an error) */
            result =
                worse(result, not_supported_because(
                                  member, syntax_kind_subclause(member.kind()),
                                  "an element of a member-specification that "
                                  "is no member-declaration"));
            continue;
        }
        entity_index before_member =
            static_cast<entity_index>(entity_table.size());
        analysis_result declared =
            member_declaration(c, declaration, access, bodies);
        /* [temp.inst]/3: a specialization's member class is declared, not
           defined, until its definition is needed: a nested class whose
           definition is ill-formed does not make the specialization so */
        if (declared == analysis_result::ill_formed &&
            entity_table[c].instantiation)
            for (entity_index e = before_member; e < entity_table.size(); e++)
                if (entity_table[e].enclosing == c &&
                    entity_table[e].kind == entity_kind::none)
                    declared =
                        not_supported_because(declaration, "temp.inst",
                                              "an invalid nested class of a "
                                              "class template specialization");
        result = worse(result, declared);
    }
    defining_members_first = outer_members_first;
    defining_members_end = outer_members_end;
    defining_bodies = outer_bodies;
    defining_bodies_class = outer_bodies_class;

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
        result = not_supported_because(n, layout_rule,
                                       "a class whose layout is not computed");
    if (result == analysis_result::well_formed)
        result = implicit_special_members(c, n);
    if (result != analysis_result::well_formed) {
        /* nothing is concluded about the class: its name finds nothing
           later declarations can use, and a type that names it (a typedef
           of it declared before) is neither complete nor incomplete */
        entity_table[c].kind = entity_kind::none;
        note_held(c);
        entity_table[c].size = 0;
        return result;
    }
    /* [temp.friend]/4: the members of a specialization that friend
       declarations of a dependent type's member name */
    member_friends_of(c, true);
    /* [temp.inst]/2: complete class template specializations that named c
       while c was being defined, now that c's layout is complete */
    {
        owned_array<deferred_class_instantiation> remaining;
        owned_array<deferred_class_instantiation> ready;
        for (const deferred_class_instantiation &d :
             deferred_class_instantiations) {
            if (d.owner == c)
                ready.append(d);
            else
                remaining.append(d);
        }
        deferred_class_instantiations =
            static_cast<owned_array<deferred_class_instantiation> &&>(
                remaining);
        for (const deferred_class_instantiation &d : ready) {
            if (!entity_table[d.specialization].defined &&
                !instantiations[d.index].defining) {
                entity_index done = d.specialization;
                size_t mark = unsupported.size();
                analysis_result defined =
                    define_class_outside(d.at, d.index, done);
                /* [temp.inst]/2: named where no complete type was needed
                   (a pointer to it): what the analysis cannot define yet
                   is no refusal of this class; the use that needs the
                   specialization complete defines it again */
                if (defined == analysis_result::unsupported) {
                    unsupported.resize(mark);
                    instantiations[d.index].lazy = true;
                    instantiations[d.index].has_failure = false;
                    continue;
                }
                result = worse(result, defined);
            }
        }
    }
    /* [class.mem]/6: "A complete-class context of a class is a function
       body ... within the member-specification of the class": the bodies
       are analysed with the class complete */
    SyntaxRef outer_declaration = current_declaration;
    bool bodies_only_ill_formed = false;
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
    result = worse(result, member_noexcept_specifiers(c));
    current_declaration = outer_declaration;
    bool incomplete_argument = false;
    if (specialization) {
        const auto &instantiation = instantiations[specialization - 1];
        for (uint32_t i = 0; i < instantiation.argument_count; i++) {
            const auto &arg =
                template_arguments[instantiation.first_argument + i];
            if (arg.kind != template_argument::kind_t::type)
                continue;
            const auto type2 = type_table[arg.type.unqualified()];
            /* no require_complete: an argument named only stays so
               ([temp.inst]/2) */
            if (type2.kind == type_kind::class_type &&
                !entity_table[type2.entity].size) {
                incomplete_argument = true;
                break;
            }
        }
    }
    /* [temp.inst]/4: a member function of a class template's specialization
       is instantiated when it is used, not with the class (every one of
       libc++'s valarray expression templates, otherwise) */
    auto queue_body_until_used = [&](SyntaxRef body) {
        if (!(incomplete_argument || specialization) ||
            body.function_definition().function_body().deleted())
            return false;
        for (entity_index f = first_member_entity; f < entity_table.size();
             f++) {
            entity &member = entity_table[f];
            if (member.kind != entity_kind::function || member.enclosing != c ||
                member.virtual_ ||
                contains_placeholder(
                    type_table[member.type.unqualified()].operand) ||
                !declared_by(f, body))
                continue;
            /* [temp.inst]/3, /11: an unused member definition need
               not make a forward-declared template argument complete. */
            member.member_body_deferred = true;
            postponed_bodies.append(postponed_body{c, body, postponed_scope,
                                                   postponed_declaration, f});
            return true;
        }
        return false;
    };
    if (outer && entity_table[outer].kind == entity_kind::class_type) {
        /* [class.mem]/6: a nested class's member function bodies are in
           the complete-class context of the enclosing class too: analysed
           at its closing } */
        for (SyntaxRef body : bodies)
            if (body)
                nested_bodies.append(deferred_body{c, body});
    } else if (postpone) {
        /* [temp.inst]/4: a member function's definition is instantiated
           where it is used; the class that named this specialization
           (template<class D> struct base { ... static_cast<D *>(this) ...
           }; struct d : base<d>) is complete only after it: the bodies
           wait for the end of the translation unit (instantiate_pending) */
        for (SyntaxRef body : bodies) {
            if (!body || queue_body_until_used(body))
                continue;
            bool placeholder = false;
            entity_index body_fn = 0;
            for (entity_index f = first_member_entity; f < entity_table.size();
                 f++) {
                const entity &member = entity_table[f];
                if (member.kind == entity_kind::function &&
                    declared_by(f, body)) {
                    body_fn = f;
                    if (member.enclosing == c && member.placeholder_return)
                        placeholder = true;
                    break;
                }
            }
            if (placeholder && !incomplete_argument) {
                instantiated_member_body(c, body);
                current_declaration = outer_declaration;
                continue;
            }
            postponed_bodies.append(postponed_body{
                c, body, postponed_scope, postponed_declaration, body_fn});
        }
        for (size_t i = nested_start; i < nested_bodies.size(); i++)
            postponed_bodies.append(
                postponed_body{nested_bodies[i].c, nested_bodies[i].body,
                               postponed_scope, postponed_declaration});
        nested_bodies.resize(nested_start);
    } else {
        size_t nested_end = nested_bodies.size();
        bool quiet = entity_table[c].instantiation != 0;
        bodies_only_ill_formed = result == analysis_result::well_formed;
        for (SyntaxRef body : bodies) {
            if (!body || queue_body_until_used(body))
                continue;
            if (quiet)
                instantiated_member_body(c, body);
            else
                result = worse(result, member_function_body(c, body));
            current_declaration = outer_declaration;
        }
        for (size_t i = nested_start; i < nested_end; i++) {
            deferred_body nested = nested_bodies[i];
            if (quiet)
                instantiated_member_body(nested.c, nested.body);
            else
                result =
                    worse(result, member_function_body(nested.c, nested.body));
            current_declaration = outer_declaration;
        }
        nested_bodies.resize(nested_start);
    }
    /* [dcl.meaning]: a class whose members are all declared and only
       whose member function bodies are ill-formed is a complete class
       the declarations after it use ([impl.clang] clang recovers) */
    bodies_only_ill_formed =
        bodies_only_ill_formed && result == analysis_result::ill_formed;
    if (result == analysis_result::well_formed || bodies_only_ill_formed) {
        analysis_result implicit = implicit_definitions(c, n);
        if (result == analysis_result::well_formed ||
            implicit != analysis_result::well_formed)
            result = worse(result, implicit);
    }
    current_declaration = outer_declaration;
    function_state = static_cast<function_context &&>(enclosing_function);
    block_variables =
        static_cast<owned_array<entity_index> &&>(enclosing_block_variables);
    current_scope = enclosing_scope;
    class_kept_ill_formed =
        bodies_only_ill_formed && result == analysis_result::ill_formed;
    if (result != analysis_result::well_formed && !class_kept_ill_formed) {
        entity_table[c].kind = entity_kind::none;
        note_held(c);
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
    auto e = n;
    uint32_t at = 1;
    bool hidden = false;
    if (e.size() > 2 && class_head_attributes_without_effect(e[1])) {
        at = 2;
    } else if (e.size() > 2 && e[1].is(syntax_kind::gnu_attribute_specifier)) {
        /* [ext:gnu_attributes] visibility("hidden") (libc++'s
           _LIBCPP_HIDDEN) in a class's forward or friend declaration: the
           class's, which its definition keeps */
        gnu_attributes gnu;
        VTRY(g, gnu_attribute_specifier(e[1], gnu));
        if (!gnu.aligned && !gnu.section && !gnu.priority && !gnu.cleanup &&
            gnu.flags == gnu_hidden_visibility) {
            at = 2;
            hidden = true;
        }
    }
    auto declared = [&](entity_index k) {
        if (hidden)
            entity_table[k].gnu_flags |= gnu_hidden_visibility;
    };
    size_t count = e.size() - (at - 1);
    standard_keyword key = leading_keyword(n);
    if (!is_class_key(key))
        return not_supported_because(
            n, "dcl.type.elab",
            "an elaborated-type-specifier without a class-key");
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
        VTRY(r, nested_name_specifier(e[at], q));
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
            return not_supported_because(
                n, "basic.lookup.elab",
                "a qualified elaborated class name that was not found");
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
            return not_supported_because(
                n, "basic.lookup.elab",
                "a qualified elaborated class name that is not a class");
        if (entity_table[c].union_ != is_union)
            return not_supported_because(n, "dcl.type.elab",
                                         "a qualified elaborated type whose "
                                         "class-key disagrees about union");
        used(t, c);
        type = entity_table[c].type.unqualified();
        return analysis_result::well_formed;
    }
    if (count == 2 && e[at].is(syntax_kind::simple_template_id)) {
        /* [dcl.type.elab]/1: class-key simple-template-id */
        type_index t = 0;
        VTRY(r, template_type(e[at], 0, false, t));
        if (type_table[t].kind != type_kind::class_type)
            return ill_formed(n,
                              "an elaborated-type-specifier with a "
                              "simple-template-id shall name a class",
                              "dcl.type.elab");
        entity_index c = type_table[t].entity;
        if (entity_table[c].union_ != is_union)
            return not_supported_because(n, "dcl.type.elab",
                                         "an elaborated type whose "
                                         "class-key disagrees about union");
        type = t;
        return analysis_result::well_formed;
    }
    if (count != 2 || !e[at].is_identifier())
        return not_supported_because(
            n, "dcl.type.elab",
            "an elaborated-type-specifier of an unhandled form");
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
       block; a class's scope declares no class of an elaborated-type-specifier
       ([basic.scope.pdecl]/7.2) but the lookup of [basic.lookup.elab]/2
       may find one declared before: a class is named then, not declared */
    bool declarable = is_namespace(target) ||
                      (entity_table[target].kind == entity_kind::block_scope &&
                       !entity_table[target].parameter_scope);
    if (!declarable &&
        (sole || entity_table[target].kind != entity_kind::class_type))
        return not_supported_because(n, "basic.scope.pdecl",
                                     "an elaborated class declaration in a "
                                     "scope that cannot declare it");
    entity_index c = 0;
    if (sole && defining_class) {
        /* [class.nest]: a nested class declared; one declared before in
           the class is named again */
        entity_index e4 = entity_table.find(defining_class, name);
        if (e4 && entity_table[e4].hidden_type)
            e4 = entity_table[e4].hidden_type;
        if (!e4) {
            /* [class.nest]/3: "If class X is defined in a namespace scope,
               a nested class Y may be declared in class X and later defined
               in the definition of class X or be later defined in a
               namespace scope enclosing the definition of class X." */
            c = declare_class(defining_class, name, is_union);
            entity_table[c].access = declaration_access;
            entity_table[c].linkage_of_name =
                entity_table[defining_class].linkage_of_name;
            declared(c);
            type = entity_table[c].type.unqualified();
            return analysis_result::well_formed;
        }
        if (entity_table[e4].kind != entity_kind::class_type ||
            entity_table[e4].enclosing != defining_class)
            return not_supported_because(
                n, "class.nest",
                "a nested class declaration of a name bound to something else");
        c = e4;
    } else if (sole) {
        /* [basic.scope.pdecl]/7.1: only the scope that contains the
           declaration is searched: a class of an enclosing scope is not
           the one declared */
        entity_index e3 = entity_table.find(target, name);
        if (e3 && (entity_table[e3].kind == entity_kind::none ||
                   entity_table.bound_by_using(target, name)))
            return not_supported_because(
                n, "basic.lookup.elab",
                "a class declaration of a name bound by using or by a "
                "declaration that was not analysed");
        if (e3 && entity_table[e3].kind == entity_kind::class_type &&
            entity_table[e3].enclosing == target) {
            c = e3;
        } else if (e3 && entity_table[e3].hidden_type) {
            c = entity_table[e3].hidden_type;
            if (entity_table[c].kind != entity_kind::class_type)
                return not_supported_because(
                    n, "basic.lookup.elab",
                    "a hidden class name that does not refer to a class");
        } else if (e3 && entity_table[e3].kind != entity_kind::variable &&
                   entity_table[e3].kind != entity_kind::function) {
            /* a typedef-name, an enumeration, a namespace of the name:
               clang's error, by more than one rule */
            return not_supported_because(
                n, "basic.lookup.elab",
                "a class declaration of a name bound to a non-class, "
                "non-variable, non-function entity");
        } else {
            c = declare_class(target, name, is_union);
            declared(c);
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
            return not_supported_because(
                n, "basic.lookup.elab",
                "an elaborated class name whose lookup is unknown");
        c = found.entity;
        if (!c) {
            if (!declarable)
                return not_supported_because(
                    n, "basic.scope.pdecl",
                    "an undeclared elaborated class name in a scope that "
                    "cannot declare it");
            /* declared in target; a name bound there by a declaration
               analysis did not follow, or by a using-declaration, may be
               the class's */
            entity_index e2 = entity_table.find(target, name);
            if (e2 && (entity_table[e2].kind == entity_kind::none ||
                       entity_table.bound_by_using(target, name)))
                return not_supported_because(
                    n, "basic.lookup.elab",
                    "an undeclared elaborated class name bound by using or by "
                    "a declaration that was not analysed");
            c = declare_class(target, name, is_union);
            declared(c);
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
            return not_supported_because(
                n, "basic.lookup.elab",
                "an elaborated class name that refers to a non-class");
    }
    /* [dcl.type.elab]/3: "The class-key or enum keyword present in the
       elaborated-type-specifier shall agree in kind with the declaration to
       which the name in the elaborated-type-specifier refers ... the union
       class-key shall be used to refer to a union" */
    if (entity_table[c].union_ != is_union)
        return not_supported_because(
            n, "dcl.type.elab",
            "an elaborated type whose class-key disagrees about union");
    used(id, c);
    declared(c);
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
 * an empty base, which is no field, is an element all the same
 * (aggregate_elements).
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
    if (x.constructor_template || x.constructor_templates ||
        x.inherits_constructors)
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
    for (uint32_t i = 0; i < x.base_count; i++) {
        const entity &b2 = entity_table[class_bases[x.first_base + i]];
        if (b2.virtual_base || b2.access)
            return false;
    }
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = entity_table[class_members[x.first_member + i]];
        if (m.base_subobject)
            continue; /* its access is the base-specifier's, checked above */
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
        return function_state.this_class;
    /* [expr.prim.lambda.closure]/12: "The lambda-expression's
       compound-statement yields the function-body of the function call
       operator, but for purposes of name lookup, determining the type and
       value of this and transforming id-expressions referring to
       non-static class members into class member access expressions using
       (*this), the compound-statement is considered in the context of the
       lambda-expression." */
    entity_index f = function_state.function, in_class = 0;
    for (size_t i = lambdas.size(); i-- > 0;)
        if (lambdas[i].call == f) {
            f = lambdas[i].enclosing_function;
            in_class = lambdas[i].enclosing_class;
        }
    if (!f)
        return in_class;
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
/* [temp.res]/6, [temp.dep]/3: in a member of a class with a dependent
   base, or of a member of a class template's specialization (which may be
   the current instantiation, A<int> in A<T>), what may be accessed
   depends on the arguments, so clang checks it at the instantiation: an
   access error there is the instantiation's (argument_named) */
bool semantic_analysis::accessible(entity_index member,
                                   entity_index object_class) const
{
    if (accessible_here(member, object_class))
        return true;
    if (!instantiation_depth)
        return false;
    entity_index naming = entity_table[member].enclosing;
    if ((entity_table[naming].kind == entity_kind::class_type &&
         entity_table[naming].instantiation) ||
        (object_class && entity_table[object_class].instantiation))
        argument_named = true;
    for (entity_index k = current_class(); k; k = entity_table[k].enclosing) {
        const entity &c = entity_table[k];
        if (c.kind != entity_kind::class_type)
            break;
        for (uint32_t i = 0; i < c.base_count; i++)
            if (entity_table[class_bases[c.first_base + i]].dependent_base)
                argument_named = true;
    }
    return false;
}

bool semantic_analysis::accessible_here(entity_index member,
                                        entity_index object_class) const
{
    const entity &x = entity_table[member];
    if (!x.access)
        return true;
    /* [impl.clang] a class whose access clang does not check (access_unchecked) */
    const entity &k = entity_table[x.enclosing];
    if (k.kind == entity_kind::class_type &&
        (k.access_unchecked ||
         (k.instantiation &&
          entity_table[specialized_template(x.enclosing)].access_unchecked)))
        return true;
    /* [temp.spec]/6: "The usual access checking rules do not apply
       to names in a declaration of an explicit instantiation or explicit
       specialization, with the exception of names appearing in a function
       body, default argument, base-clause, member-specification,
       enumerator-list, or static data member or variable template
       initializer." */
    if (explicit_declaration_names && !function_state.active)
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
    auto as_member_of = [&](entity_index member_class) -> bool {
        if (member_class == x.enclosing)
            return true;
        if (x.access != 1)
            return false;
        bool ambiguous = false;
        if (!base_step(member_class, x.enclosing, ambiguous))
            return false;
        if (!object_class || !x.nonstatic_member ||
            object_class == member_class)
            return true;
        return base_step(object_class, member_class, ambiguous) != 0;
    };
    /* [class.friend]/2, [class.access]/6: the template-head of a friend
       class template belongs to the befriended entity */
    if (friend_head.name)
        for (const friend_record &f : friends)
            if (friend_names_class(f, friend_head.scope, friend_head.name) &&
                as_member_of(f.granting))
                return true;
    /* (a default argument is checked in its function's declaration:
       [class.access]/8, a friend function's has the friend's access) */
    entity_index starts[5] = {0, defining_class, 0, access_class,
                              default_argument_function};
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
    /* [class.friend]/6: "A function can be defined in a friend declaration
       of a class if ... Such a function is implicitly an inline function
       and is in the (lexical) scope of the class": the body of an
       instantiated friend function template has the access of the class
       that declares it [impl.clang] */
    if (function_state.active) {
        uint32_t i = entity_table[function_state.function].instantiation;
        if (i) {
            entity_index t = instantiations[i - 1].template_entity;
            for (const function_template &f : function_templates)
                if (f.entity == t && f.friend_class) {
                    for (entity_index here = f.friend_class;
                         !is_namespace(here);
                         here = entity_table[here].enclosing) {
                        for (const friend_record &g : friends)
                            if (friend_names(g, here) &&
                                as_member_of(g.granting))
                                return true;
                        if (entity_table[here].kind ==
                                entity_kind::class_type &&
                            as_member_of(here))
                            return true;
                    }
                    break;
                }
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
                                      owned_array<SyntaxRef> &bodies)
{
    return m.visit(Overloaded{
        [&](GnuExtensionDeclaration x) {
            /* [ext:gnu_extension_keyword]: __extension__ changes no
               meaning */
            return member_declaration(c, x[1], access, bodies);
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
            VTRY(r, alias_type(a, type));
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
        [&](ExplicitSpecialization s) {
            /* [temp.expl.spec]/2 (CWG727): an explicit specialization of a
               member template in the class's own scope */
            return explicit_specialization(s);
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
        [&](MemberDeclaration other) {
            analysis_result refused = not_supported(other);
            last_unsupported_detail(syntax_kind_name(other.kind()),
                                    strlen(syntax_kind_name(other.kind())));
            return refused;
        }});
}

/*
 * A member-declaration that is a simple declaration or a
 * function-definition, with the decl-specifier-seq and the declarators it
 * has: a constructor, a destructor, a conversion function, a friend, or a
 * member of any other kind.
 */
analysis_result semantic_analysis::member_declaration_with_specifiers(
    entity_index c, SyntaxRef node, uint8_t access,
    owned_array<SyntaxRef> &bodies)
{
    /* [ext:gnu_attributes] before it all (libc++'s _LIBCPP_HIDE_FROM_ABI
       friend ...) */
    uint32_t leading = leading_gnu_attributes(node);
    SyntaxTail tail(node, leading);
    if (leading < node.size() && tail[0].is(syntax_kind::decl_specifier_seq)) {
        for (auto x : tail[0].elements())
            if (x.is(standard_keyword::friend_))
                return friend_declaration(c, node, bodies);
    }

    /* [dcl.fct]/18: an abbreviated member function template */
    if (node.is(syntax_kind::function_definition) &&
        is_namespace(entity_table[c].enclosing) && abbreviated_function(node))
        return member_function_template(c, node, node, access);
    if (SyntaxRef id = conversion_function_id_of(node))
        return conversion_function_member(c, node, id, access, bodies);

    /* a constructor or destructor: no decl-specifier-seq, or one of
       function-specifiers alone ([class.ctor]/1: "Each decl-specifier in
       the decl-specifier-seq of a constructor declaration (if any) shall
       be friend, inline, constexpr, or an explicit-specifier") */
    /* [ext:gnu_attributes] before it all (special_member_declarator takes
       them) */
    uint32_t lead = leading_gnu_attributes(node);
    /* [dcl.align]/1, [dcl.attr.grammar]/4: alignas before the
       decl-specifier-seq appertains to the member declared; the largest
       raises its alignment, as GCC's aligned does ([dcl.align]/5: never
       lowers it).  Not on a constructor or destructor: no rule yet. */
    uint64_t member_alignment = 0;
    uint32_t alignas_count = 0;
    owned_array<SyntaxRef> alignments;
    for (uint32_t i = lead; i < node.size(); i++) {
        SyntaxElement a = node[i];
        if (a.is(syntax_kind::alignment_specifier)) {
            alignments.append(a);
        } else if (a.is(syntax_kind::attribute_specifier_seq)) {
            bool only = true;
            for (auto k : a.elements())
                only = only && k.is(syntax_kind::alignment_specifier);
            if (!only)
                break;
            for (auto k : a.elements())
                alignments.append(k);
        } else {
            break;
        }
        alignas_count++;
    }
    for (SyntaxRef each : alignments) {
        uint64_t alignment = 0;
        VTRY(r, alignment_specifier(each, alignment));
        if (alignment > member_alignment)
            member_alignment = alignment;
    }
    lead += alignas_count;
    uint32_t at = lead;
    bool constexpr_specified = false, virtual_specified = false,
         explicit_specified = false, inline_specified = false;
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
    if (!alignas_count && node.size() > lead + 1 &&
        node[lead].is(syntax_kind::decl_specifier_seq) &&
        function_specifiers_only(node[lead], constexpr_specified,
                                 &virtual_specified, &explicit_specified,
                                 &inline_specified) &&
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
            if (SyntaxRef id = conversion_function_id_of(node))
                return not_supported_because(id, "class.conv.fct",
                                             "a conversion function without a "
                                             "decl-specifier-seq in this form");
            return not_supported_because(
                node, "class.ctor",
                "a declaration without decl-specifiers that is no recognised "
                "special member");
        }
        if (node.is(syntax_kind::member_declaration)) {
            if (node.size() != at + 2 ||
                !node[at + 1].is(preprocessing_op_or_punc::SEMICOLON))
                /* = default, = delete, a pure-specifier */
                return not_supported_because(
                    node, "dcl.fct.def",
                    "a special member declaration with more than a semicolon "
                    "after the declarator");
            virt_override = override_;
            virt_final = final_;
            analysis_result r = special_member_declarator(
                c, node, special(node[at]), access, false, constexpr_specified,
                virtual_specified, explicit_specified, inline_specified);
            virt_override = virt_final = false;
            return r;
        }
        uint32_t body_at = at + 1;
        while (body_at < node.size() &&
               virt_specifier(node[body_at], override_, final_))
            body_at++;
        auto body = node.function_definition().function_body();
        bool constrained = node.size() == body_at + 2 &&
                           node[body_at].is(syntax_kind::requires_clause) &&
                           node[body_at].size() == 2;
        if ((!constrained && node.size() != body_at + 1) || !body ||
            body.function_try_block())
            return not_supported_because(
                node, "dcl.fct.def.general",
                "a special member definition with a function-try-block or "
                "unhandled parts");
        bool satisfied = true;
        if (constrained) {
            if (!entity_table[c].instantiation || body.defaulted() ||
                body.deleted())
                return not_supported_because(
                    node, "temp.constr",
                    "a requires-clause on a defaulted or deleted special "
                    "member or outside a template specialization");
            VTRY(r, constraint_satisfied(node[body_at][1], current_scope,
                                         lookup_horizon, satisfied));
        }
        if (body.defaulted() || body.deleted()) {
            /* = default, = delete: no user-provided function
               ([dcl.fct.def.default]/5), which the layout ([abi:pod]) and
               triviality need to know before the class is complete */
            return defaulted_or_deleted_special_member(
                c, node, special(node[at]), node[body_at], access,
                constexpr_specified, virtual_specified || override_ || final_,
                explicit_specified);
        }
        analysis_result r = special_member_declarator(
            c, node, special(node[at]), access, true, constexpr_specified,
            virtual_specified || override_ || final_, explicit_specified,
            inline_specified);
        if (r == analysis_result::well_formed) {
            entity_table[last_declared].constraint_unsatisfied = !satisfied;
            if (satisfied)
                bodies.append(node);
        }
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
    bool outer_mutable_allowed = mutable_allowed;
    mutable_allowed = true;
    placeholder_allowed = true;
    /* [temp.inst]/2: a member function declaration can name an
       incomplete specialization as its return type. */
    bool saved_member_return = member_return_type;
    member_return_type = false;
    if (node.is(syntax_kind::member_declaration,
                syntax_kind::function_definition) &&
        count == 3) {
        SyntaxElement d = node[lead + 1];
        if (d.is(syntax_kind::member_declarator_list) && d.size() == 1)
            d = d[0];
        if (DeclaratorParts(d).parameters())
            member_return_type = true;
    }
    analysis_result r = decl_specifier_seq(first, s, false);
    member_return_type = saved_member_return;
    virtual_allowed = outer_virtual_allowed;
    mutable_allowed = outer_mutable_allowed;
    sole_elaborated = 0;
    declaration_access = 0;
    for (uint32_t i = 0;
         i < lead - alignas_count && r == analysis_result::well_formed; i++)
        VTRY_SET(r, gnu_attribute_specifier(node[i], s));
    if (member_alignment > s.aligned)
        s.aligned = member_alignment;
    return member_declarators(c, node, s, access, lead + 1, bodies);
}

/*
 * The declarators of the member-declaration (or function-definition) n of
 * class c from node[first] on, after its decl-specifier-seq s.
 */
analysis_result semantic_analysis::member_declarators(
    entity_index c, SyntaxRef n, const specified &s, uint8_t access,
    uint32_t first, owned_array<SyntaxRef> &bodies)
{
    analysis_result r = analysis_result::well_formed;
    if (n.is(syntax_kind::function_definition)) {
        /* [dcl.fct.def.general]/1: declarator virt-specifier-seq_opt
           function-body */
        uint32_t body_at = first + 1;
        bool override_ = false, final_ = false;
        while (body_at < n.size() &&
               virt_specifier(n[body_at], override_, final_))
            body_at++;
        auto body = n.function_definition().function_body();
        bool constrained = n.size() == body_at + 2 &&
                           n[body_at].is(syntax_kind::requires_clause) &&
                           n[body_at].size() == 2;
        if ((!constrained && n.size() != body_at + 1) || !body ||
            body.function_try_block())
            return not_supported_because(
                n, "dcl.fct.def.general",
                "a member function definition with a function-try-block or "
                "unhandled parts");
        bool satisfied = true;
        if (constrained) {
            if (!entity_table[c].instantiation || body.defaulted() ||
                body.deleted())
                return not_supported_because(
                    n, "temp.constr",
                    "a requires-clause on a defaulted or deleted member "
                    "function or outside a template specialization");
            VTRY(cr, constraint_satisfied(n[body_at][1], current_scope,
                                          lookup_horizon, satisfied));
        }
        bool compared = false;
        if (body_at == first + 1 && body.defaulted() && !constrained) {
            scoped_value<SyntaxRef> member_declaration(current_declaration, n);
            compared = defaulted_member_equality(c, n[first], s, access);
        }
        if (body_at == first + 1 && body.defaulted() && !compared)
            return defaulted_copy_assignment(c, n, n[first], s, access);
        /* = delete: member_function_definition's deleted_definition */
        if (!body.deleted() && !body.plain() && !compared)
            /* a ctor-initializer, a function-try-block, = default */
            return not_supported_because(
                n, "dcl.fct.def.general",
                "a member function body that is neither plain, deleted nor an "
                "accepted default");
        virt_override = override_;
        virt_final = final_;
        virt_pure = false;
        r = member_declarator(c, n, n[first], s, access, true);
        virt_override = virt_final = false;
        if (r == analysis_result::well_formed) {
            entity_table[last_declared].constraint_unsatisfied = !satisfied;
            if (satisfied)
                bodies.append(n);
        }
        return r;
    }
    owned_array<SyntaxElement> declarators;
    for (auto d : n.elements(first)) {
        if (d.is(preprocessing_op_or_punc::SEMICOLON))
            continue;
        for (auto item : d.items(syntax_kind::member_declarator_list))
            declarators.append(item);
    }
    if (declarators.empty()) {
        /* [class.mem]/3: a member enumeration ([dcl.pre]/5) */
        bool plain = !s.typedef_ && !s.static_ && !s.extern_ && !s.mutable_ &&
                     !s.inline_ && !s.constexpr_ && !s.type.cv();
        if (s.declares_enumeration && plain)
            return analysis_result::well_formed;
        auto seq = n[first - 1];
        if (declares_unnamed_class(seq))
            return anonymous_union_member(c, n, s, access);
        /* [class.nest]/1: a nested class's definition, or its declaration
           ([class.nest]/3) */
        if (plain && seq.size() == 1 &&
            seq[0].is(syntax_kind::class_specifier,
                      syntax_kind::elaborated_type_specifier))
            return analysis_result::well_formed;
        /* a nested class, an anonymous union */
        return not_supported_because(
            n, "class.mem",
            "a member declaration without declarators that is no nested class "
            "or anonymous union");
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
analysis_result semantic_analysis::member_declarator(
    entity_index c, SyntaxRef declaration, SyntaxElement d, const specified &s,
    uint8_t access, bool definition, bool initialized)
{
    if (d.is(syntax_kind::member_declarator)) {
        /* identifier_opt attribute-specifier-seq_opt : constant-expression */
        for (auto x : d.elements())
            if (x.is(preprocessing_op_or_punc::COLON))
                return member_bit_field(c, declaration, d, s, access);
        /* declarator virt-specifier-seq_opt pure-specifier_opt */
        {
            bool override_ = false, final_ = false, pure = false;
            uint32_t i = 1;
            while (i < d.size() && virt_specifier(d[i], override_, final_))
                i++;
            if (i < d.size() && d[i].is(syntax_kind::pure_specifier)) {
                pure = true;
                i++;
            }
            if (i > 1 && i == d.size()) {
                virt_override = override_;
                virt_final = final_;
                virt_pure = pure;
                analysis_result r = member_declarator(c, declaration, d[0], s,
                                                      access, definition);
                virt_override = virt_final = virt_pure = false;
                return r;
            }
            if (i > 1)
                return not_supported_because(
                    d.index(), "class.mem",
                    "a member-declarator with too many parts");
        }
        /* declarator brace-or-equal-initializer: [class.mem]/10 "A
           brace-or-equal-initializer shall appear only in the declaration
           of a data member"; for a non-static one it is a default member
           initializer, analysed where a constructor uses it
           ([class.base.init]/9), a complete-class context ([class.mem]/6) */
        if (d.size() != 2 || !d[1].is(syntax_kind::brace_or_equal_initializer,
                                      syntax_kind::braced_init_list))
            return not_supported_because(
                d.index(), "class.mem",
                "a member-declarator with an unhandled initializer or suffix");
        last_declared = 0;
        VTRY(r, member_declarator(c, declaration, d[0], s, access, definition,
                                  true));
        entity_index e = last_declared;
        if (e && entity_table[e].kind == entity_kind::variable &&
            !entity_table[e].nonstatic_member)
            return static_member_initializer(d, e, d[1], s);
        if (!e || entity_table[e].kind != entity_kind::variable)
            /* a pure-specifier ([class.abstract]) */
            return not_supported_because(
                d.index(), "class.abstract",
                "an initializer on a member declaration that declares no "
                "variable");
        if (entity_table[c].union_) {
            /* [class.union]/2: "At most one variant member of a union may
               have a default member initializer" */
            const entity &x = entity_table[c];
            for (uint32_t i = 0; i < x.member_count; i++) {
                entity_index o = class_members[x.first_member + i];
                if (o != e && variant_initialized(o))
                    return ill_formed(declaration,
                                      "initializing multiple members of "
                                      "union",
                                      "class.union");
            }
        }
        entity_table[e].default_initializer = d[1].index();
        return analysis_result::well_formed;
    }
    if (!s.has_type && declares_special_member(c, d))
        /* a cv-qualifier or another specifier with a constructor */
        return not_supported_because(declaration, "class.ctor",
                                     "a constructor declared with a specifier "
                                     "or cv-qualifier but no type");
    if (s.cxx_thread_local && !s.static_)
        /* [dcl.stc]/3: "It shall be applied only to the declaration of a
           variable of namespace or block scope, to a structured binding
           declaration, or to the declaration of a static data member." */
        return ill_formed(declaration,
                          "thread_local is only allowed on variable "
                          "declarations and static data members",
                          "dcl.stc");
    if (s.cxx_thread_local)
        /* a static data member of thread storage duration: its definition
           outside the class is not analysed */
        return not_supported_because(declaration, "dcl.stc",
                                     "a thread_local static data member");
    if (s.constinit_)
        /* [dcl.constinit]/1: on the initializing declaration of a static
           data member, an inline one in the class */
        return not_supported_because(declaration, "dcl.constinit",
                                     "a constinit static data member");
    qualified_type type = s.type;
    declarator_meaning m;
    m.alignas_allowed = true;
    function_cv = 0;
    in_member_declarator = true;
    function_parameters.clear();
    analysis_result r = declarator(declaration, d, type, m);
    in_member_declarator = false;
    if (r == analysis_result::well_formed)
        r = misplaced_function_qualifiers(declaration, type, true);
    if (r != analysis_result::well_formed)
        return r;
    /* C++11 [dcl.constexpr]/8: "A constexpr specifier for a non-static
       member function that is not a constructor declares that member
       function to be const" (C++14 dropped it) */
    if (s.constexpr_ && !s.static_ &&
        tokens.files.behavior.edition < standard_edition::cplusplus_14 &&
        type_table[type.unqualified()].kind == type_kind::function)
        function_cv |= cv_const;
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
        return not_supported_because(
            declaration, "class.mem",
            "a member declarator with no name or a qualified name");
    /* [ext:gnu_attributes] the declaration's and the declarator's */
    if (s.aligned > m.aligned)
        m.aligned = s.aligned;
    /* clang's layout of a data member keeps alignments up to 2^28 only */
    if (m.aligned > (uint64_t(1) << 28))
        return not_supported_because(declaration, "dcl.align",
                                     "a member alignment above 2^28");
    m.gnu_flags |= s.gnu_flags;
    if (!m.section)
        m.section = s.section;
    /* [ext:gnu_asm_labels] a member's: not yet */
    if (m.asm_label)
        return not_supported_because(declaration, "ext.gnu",
                                     "an asm label on a member");
    if (!m.abi_tag)
        m.abi_tag = s.abi_tag;
    if (!s.has_type)
        return not_supported_because(
            declaration, "dcl.type",
            "a member declaration without a type specifier");
    if (s.typedef_)
        return member_typedef(c, declaration, m.id, type, s, m, access);
    /* [dcl.stc]/8: "The mutable specifier shall appear only in the
       declaration of a non-static data member whose type is neither
       const-qualified nor a reference type." */
    if (s.mutable_ &&
        (type_table[type.unqualified()].kind == type_kind::function ||
         (type.cv() & cv_const) ||
         type_table[type.unqualified()].kind == type_kind::lvalue_reference ||
         type_table[type.unqualified()].kind == type_kind::rvalue_reference))
        return ill_formed_at_token(m.id,
                                   "the mutable specifier shall appear only "
                                   "in the declaration of a non-static data "
                                   "member whose type is neither "
                                   "const-qualified nor a reference type",
                                   "dcl.stc");
    /* [dcl.constexpr]/1: "The constexpr specifier shall be applied only to
       the definition of a variable or variable template or the declaration
       of a function or function template": a static data member's is not
       analysed yet */
    if (s.constexpr_ && !s.static_ &&
        type_table[type.unqualified()].kind != type_kind::function)
        return not_supported_because(declaration, "dcl.constexpr",
                                     "a constexpr non-static data member");
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
    if (is_function && !s.typedef_ &&
        type_table[s.type.unqualified()].kind == type_kind::function &&
        type == s.type && entity_table[c].instantiation)
        /* [temp.spec]/8: "If a function declaration acquired its function type
           through a dependent type without using the syntactic form of a
           function declarator, the program is ill-formed" */
        return ill_formed_at_token(m.id,
                                   "a function declaration shall not acquire "
                                   "its function type through a dependent type "
                                   "without a function declarator",
                                   "temp.spec");
    bool placeholder_return = is_function && contains_placeholder(type);
    if (placeholder_return) {
        /* [dcl.spec.auto]/10: "A virtual function shall not have a
           declared return type that uses a placeholder type." */
        if (s.virtual_ || virt_override || virt_final || virt_pure)
            return ill_formed_at_token(m.id,
                                       "a virtual function shall not have a "
                                       "declared return type that uses a "
                                       "placeholder type",
                                       "dcl.spec.auto");
        if (!definition ||
            !tokens.files.behavior.has(
                language_feature::return_type_deduction) ||
            m.conversion_function || m.destructor || m.literal_operator ||
            (m.operator_function && m.allocation != allocation_function::none))
            return not_supported_because(
                declaration, "dcl.spec.auto",
                "a placeholder return type of a member declared without a "
                "definition, or of a kind not handled");
    }
    if (!is_function && contains_placeholder(type)) {
        /* [dcl.spec.auto]/6: the subclause lists where a placeholder type may
           appear: a non-static data member is none of them
           ("'auto' not allowed in non-static struct member") */
        if (!s.static_)
            return ill_formed_at_token(m.id,
                                       "a placeholder type is not allowed in "
                                       "a non-static data member",
                                       "dcl.spec.auto");
        if (!initialized)
            return not_supported_because(declaration, "dcl.spec.auto",
                                         "in a static data member");
    }
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
        return not_supported_because(
            declaration, "ext.gnu",
            "a member function with aligned or packed, or a member with used");
    /* a member's symbol attributes (a member function's weak and section,
       a static data member's, a data member's function attributes) are not
       analysed yet; on a non-static data member they are GCC's error */
    if ((m.gnu_flags & (gnu_weak | gnu_used | gnu_section | gnu_mode |
                        gnu_constructor | gnu_destructor | gnu_cleanup |
                        gnu_require_constant_initialization)) ||
        (!is_function &&
         (m.gnu_flags & (gnu_inline_flags | gnu_function_symbol_flags))))
        return not_supported_because(declaration, "ext.gnu",
                                     "a member with a GNU symbol or function "
                                     "attribute that is not handled");
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
    /* [namespace.udecl]/15 (C++20 /14): the functions of a base class a
       using-declaration brought into the class, which a member function of
       the name declared here joins as an overload, or hides one of them
       (overload_set) */
    entity_index introduced = 0;
    if (prior && entity_table[prior].enclosing != c) {
        if (is_function && entity_table[prior].kind == entity_kind::function &&
            entity_table[prior].special == special_member::none &&
            entity_table.bound_by_using(c, name) && !m.conversion_function &&
            !m.destructor) {
            introduced = prior;
            prior = 0;
        } else {
            /* a name a using-declaration or a member enumeration's
               enumerator brought into the class */
            return not_supported_because(
                declaration, "class.mem",
                "a member with the name of one introduced by using or an "
                "enumerator");
        }
    }
    entity_index overloads = 0;
    /* [temp.mem]/2: member function templates of the name declared before
       are overloaded with this function: it heads the name's chain, and
       keeps their held name */
    entity_index templates = 0;
    if (prior && is_function && entity_table[prior].kind == entity_kind::none &&
        entity_table[prior].enclosing == c &&
        member_function_template_of(prior) && !m.conversion_function &&
        !m.destructor) {
        templates = prior;
        prior = 0;
    }
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
            /* the base's functions after the class's own are no conflict */
            if (x.enclosing != c)
                break;
            if (!same_parameters(type_table, x.type.unqualified(),
                                 type.unqualified()))
                continue;
            /* conversion functions to different types have different
               names ([class.conv.fct]/1), here one */
            if (m.conversion_function && x.type != type)
                continue;
            /* [basic.scope.declarative]/4, [over.load]/2: member functions
               with trailing requires-clauses can overload on constraints */
            if (x.constraint_unsatisfied || x.trailing_requires ||
                (declaration.is(syntax_kind::function_definition) &&
                 declaration.size() >= 3 &&
                 declaration[declaration.size() - 2].is(
                     syntax_kind::requires_clause)))
                continue;
            if ((placeholder_return || x.placeholder_return) &&
                x.member_cv == function_cv && x.nonstatic_member == !s.static_)
                return not_supported_because(
                    declaration, "dcl.spec.auto",
                    "a redeclaration beside a member function with a "
                    "placeholder return type");
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
            return not_supported_because(declaration, "over",
                                         "a member overloading a name whose "
                                         "declaration was not analysed");
        owned_buffer spelling;
        tokens.spelling(m.id, spelling.get());
        if (spelling.size() == 1 && spelling.data()[0] == '_')
            /* C++26's name-independent declarations (P2169): _ may be
               declared again */
            return not_supported_because(declaration, "basic.scope.scope",
                                         "a repeated member named _");
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
        entity_table[e].exceptions =
            resolved_exceptions(m.exceptions, type, m.gnu_flags);
        if (deferred_noexcept) {
            pending_noexcept p;
            p.function = e;
            p.specifier = deferred_noexcept;
            pending_noexcepts.append(p);
            deferred_noexcept = 0;
        }
        entity_table[e].placeholder_return = placeholder_return;
        entity_table[e].trailing_requires =
            declaration.is(syntax_kind::function_definition) &&
            declaration.size() >= 3 &&
            declaration[declaration.size() - 2].is(
                syntax_kind::requires_clause);
        entity_table[e].nonstatic_member = !s.static_;
        entity_table[e].member_cv = function_cv;
        /* [class.mfct]/1: "A member function may be defined in its class
           definition, in which case it is an inline member function
           ([dcl.inline])" */
        /* [dcl.constexpr]/3 (and clang before C++20 for any non-static
           member function): not in a class with virtual bases */
        if (s.constexpr_ && !s.static_ && entity_table[c].virtual_base_count)
            return not_supported_because(
                declaration, "dcl.constexpr",
                "a constexpr member function of a class with virtual bases");
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
        diagnose_if_declared(declaration, e);
        if (m.operator_function) {
            entity_table[e].operator_function = true;
            entity_table[e].implemented = m.implemented;
            entity_table[e].allocation = m.allocation;
            VTRY(checked, operator_function_declaration(declaration, e));
            /* (one deleted on this, its first, declaration: not counted,
               layout's pod_for_layout reads the count at the class's end,
               before the bodies) */
            bool deleted_here =
                definition &&
                declaration.function_definition().function_body().deleted();
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
            VTRY(checked, literal_operator_declaration(declaration, e));
        }
        if (has_defaults) {
            if (entity_table[e].allocation != allocation_function::none)
                /* [basic.stc.dynamic.allocation]/1: "The first parameter
                   shall not have an associated default argument"; which
                   parameter has one is known once the defaults are
                   analysed, after the class */
                return not_supported_because(
                    declaration, "basic.stc.dynamic.allocation",
                    "a default argument on an allocation function");
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
        VTRY(v, virtual_member(c, e, declaration, m.id, s.virtual_));
    } else if (has_defaults) {
        /* /3: a default argument of a member that is not a function */
        return not_supported_because(
            declaration, "dcl.fct.default",
            "a default argument on a member that is not a function");
    } else if (s.static_) {
        /* [class.static.data]/3: "The declaration of a non-inline static
           data member in its class definition is not a definition and may
           be of an incomplete type other than cv void." */
        /* [dcl.constexpr]/10: "A constexpr specifier used in an object
           declaration declares the object as const"; [dcl.constexpr]/1 a
           constexpr static data member is an inline variable, defined by
           its declaration in the class (its initializer:
           static_member_initializer) */
        if (s.constexpr_)
            type = add_cv(type, cv_const);
        if (local_class_rule(c))
            /* [class.local]/4: "A local class cannot have static data
               members" (a class nested in one neither) */
            return not_supported_because(
                declaration, "class.static.data",
                "a static data member of a local class");
        const struct type &record = type_table[type.unqualified()];
        if (record.kind == type_kind::lvalue_reference ||
            record.kind == type_kind::rvalue_reference || abstract_class(type))
            return not_supported_because(
                declaration, "class.static.data",
                "a static data member of reference or abstract class type");
        e = entity_table.make(entity_kind::variable, name, c);
        entity_table[e].type = type;
        /* [class.static.data]/1: "A static data member is not part of the
           subobjects of a class." */
        entity_table[e].nonstatic_member = false;
        entity_table[e].inline_ = s.inline_;
        entity_table[e].defined = s.inline_;
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
        declared_names.append(first);
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
        /* a copy: size_of_type and define_class_instantiation can grow type_table */
        const struct type record = type_table[type.unqualified()];
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
        if (record.kind == type_kind::class_type && !size &&
            entity_table[record.entity].kind == entity_kind::class_type &&
            entity_table[record.entity].instantiation &&
            !entity_table[record.entity].defined &&
            !instantiations[entity_table[record.entity].instantiation - 1]
                 .defining) {
            /* [temp.inst]/2: a member of class template specialization type
               needs a completely-defined type, so the specialization is
               defined here, though its argument is the class being defined
               (an incomplete argument is fine: D<E> in E, with D's member
               function bodies instantiated only when used, /4) */
            entity_index done = 0;
            analysis_result r2 = define_class_outside(
                SyntaxRef(), entity_table[record.entity].instantiation - 1,
                done);
            if (r2 != analysis_result::well_formed)
                return r2;
            size = size_of_type(type);
        }
        bool is_reference = record.kind == type_kind::lvalue_reference ||
                            record.kind == type_kind::rvalue_reference;
        if (is_reference)
            /* [expr.sizeof]/2 gives sizeof(T&) as sizeof(T), but the member
               is laid out as a pointer ([abi:class-types]); the referenced
               type may be incomplete ([dcl.ref]/1: "a reference to
               incomplete type") */
            size = pointer_size();
        bool is_void = record.kind == type_kind::fundamental &&
                       record.fundamental == fundamental_type::void_;
        if (record.kind == type_kind::class_type &&
            entity_table[record.entity].kind != entity_kind::class_type) {
            uint32_t inst = entity_table[record.entity].instantiation;
            if (inst && instantiations[inst - 1].has_failure) {
                unsupported.append(instantiations[inst - 1].failure);
                return analysis_result::unsupported;
            }
            return not_supported_because(
                declaration, "class.mem",
                "a data member whose class type is not known as a class");
        }
        if (record.kind == type_kind::class_type && !size) {
            /* [class.mem]/15: "The type of a non-static data member shall
               not be an incomplete type"; clang drops the member and
               goes on with the class complete */
            return recovered_error_at(m.id,
                                      "the type of a non-static data member "
                                      "shall not be an incomplete type",
                                      "class.mem");
        }
        /* [basic.def]/5 (a member's declaration defines it,
           [basic.def]/2) */
        if (abstract_class(type))
            return ill_formed_at_token(m.id,
                                       "the type of a non-static data member "
                                       "shall not be an abstract class type",
                                       "basic.def");
        if (is_void)
            /* [class.mem]/15: "The type of a non-static data member shall
               not be an incomplete type" */
            return ill_formed_at_token(m.id,
                                       "the type of a non-static data member "
                                       "shall not be an incomplete type",
                                       "class.mem");
        /* (an array of unknown bound, clang's and GCC's flexible array
           member, and an array of zero elements, their extension
           (declarator_meaning warns of it), take no bytes: glibc's struct
           file_handle) */
        if (!size && record.kind != type_kind::array)
            return not_supported_because(
                declaration, "class.mem",
                "a data member of a type with no size");
        uint32_t alignment = static_cast<uint32_t>(alignment_of_type(type));
        if (is_reference)
            alignment = static_cast<uint32_t>(pointer_size());
        if (!alignment)
            return not_supported_because(
                declaration, "class.mem",
                "a data member of a type with unknown alignment");
        /* its offset is the layout's, at the closing }
           ([abi:class-types]), with [ext:gnu_attributes]' aligned and
           packed */
        e = entity_table.make(entity_kind::variable, name, c);
        entity_table[e].type = type;
        entity_table[e].nonstatic_member = true;
        entity_table[e].mutable_member = s.mutable_;
        entity_table[e].defined = true;
        entity_table[e].field = entity_table[c].member_count;
        entity_table[e].alignment = static_cast<uint32_t>(m.aligned);
        /* [ext:gnu_attributes] a member of a typedef-name's type declared
           with aligned */
        uint32_t named =
            typedef_alignment_of(s.type.unqualified(), type, s.typedef_aligned);
        VTRY(checked, array_element_alignment(declaration, type, named));
        if (named > entity_table[e].alignment)
            entity_table[e].alignment = named;
        entity_table[e].gnu_flags =
            (m.gnu_flags | entity_table[c].gnu_flags) & gnu_packed;
        /* [dcl.attr.nouniqueaddr]/2: the declaration's or the
           declarator's */
        entity_table[e].no_unique_address =
            ((s.gnu_flags | m.gnu_flags) & gnu_no_unique_address) != 0;
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
        while (entity_table[last].next_overload &&
               entity_table[entity_table[last].next_overload].enclosing == c)
            last = entity_table[last].next_overload;
        /* before the base's functions, if any */
        entity_table[e].next_overload = entity_table[last].next_overload;
        entity_table[last].next_overload = e;
        entity_table[e].hidden_type = entity_table[overloads].hidden_type;
    } else {
        entity_table[e].next_overload = introduced;
        entity_table[e].member_templates = templates;
        entity_table.bind(c, name, e);
        /* the member function templates of the base's name */
        if (introduced && entity_table[introduced].member_templates)
            introduce_member_templates(
                c, e, entity_table[introduced].member_templates);
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
    entity_index c, SyntaxRef declaration, token_index id, qualified_type type,
    const specified &s, const declarator_meaning &m, uint8_t access)
{
    if (s.static_ || s.inline_ || s.constexpr_ || s.virtual_ || s.gnu_flags ||
        s.aligned || s.typedef_aligned || m.aligned || m.gnu_flags ||
        m.section || m.asm_label)
        return not_supported_because(declaration, "dcl.typedef",
                                     "a member typedef with a specifier, "
                                     "attribute, alignment or asm label");
    uint32_t name = tokens.identifier(id);
    if (name == entity_table[c].name)
        return ill_formed_at_token(id,
                                   "every member of a class that is itself "
                                   "a type shall have a name different from "
                                   "the class",
                                   "class.mem");
    if (entity_table.find(c, name))
        return not_supported_because(
            declaration, "dcl.typedef",
            "a member typedef of an already declared name");
    entity_index e = entity_table.make(entity_kind::typedef_name, name, c);
    entity_table[e].type = type;
    entity_table[e].defined = true;
    entity_table[e].access = access;
    entity_table.add_declaration(e, declaration);
    entity_table.bind(c, name, e);
    last_declared = e;
    /* [dcl.typedef]/9: the first typedef-name of an unnamed class (or
       enumeration) the declaration defines is its name for linkage
       purposes, as at namespace scope (declare) */
    const struct type &record = type_table[type.unqualified()];
    if (!type.cv() && (record.kind == type_kind::class_type ||
                       record.kind == type_kind::enumeration)) {
        entity &named = entity_table[record.entity];
        if (!named.name && !named.linkage_name && named.enclosing == c &&
            !named.lambda) {
            named.linkage_name = name;
            named.linkage_of_name = entity_table[c].linkage_of_name;
        }
    }
    return analysis_result::well_formed;
}

/*
 * [class.mfct]/1: the body of a member function defined in its class,
 * analysed as the definition's own: its declarator again, for the
 * parameters (function_body reads them), then the body.
 */
analysis_result semantic_analysis::member_function_body(entity_index c,
                                                        SyntaxRef n)
{
    auto definition = n.function_definition();
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

/*
 * [temp.inst]/4: a member function of an implicit instantiation is
 * instantiated only if used, and its being ill-formed for these arguments
 * (r %= x with a double r) is no error of the class's instantiation.  Uses
 * are not tracked yet: the body is analysed quietly and one that fails is
 * left undefined, so that no code is made for it; one that is unsupported
 * also makes a use of the function a refusal.
 */
analysis_result semantic_analysis::instantiated_member_body(entity_index c,
                                                            SyntaxRef n,
                                                            bool used,
                                                            uint32_t *failure)
{
    /* [temp.inst]/1, /4: a member function used is instantiated, and an
       ill-formed instantiation makes the program ill-formed: its errors
       are reported (in the instantiation's context, as clang's "in
       instantiation of member function ... requested here") [impl.clang] */
    diagnostics_suppressed_if hush(diagnostics, !used);
    size_t unsupported_before = unsupported.size();
    /* [temp.inst]/4: a member not used is not instantiated, so what its
       body would need (a deleter's call operator, whose own body waits
       for the type to be complete) is needed only once the member is:
       kept in deferred_needs until then, as a defaulted special member's */
    scoped_value<entity_index> deferring(needs_deferred_by, needs_deferred_by);
    defer_next_body_needs = !used;
    quiet_member_bodies++;
    analysis_result r = member_function_body(c, n);
    quiet_member_bodies--;
    defer_next_body_needs = false;
    /* ill-formed for these arguments (its diagnostics hushed, or a
       definition it wanted failed quietly): a use of the member is
       refused, never a call of a function without code */
    bool failed = used && r == analysis_result::ill_formed;
    if (r == analysis_result::ill_formed && last_declared && !used)
        r = not_supported_because(
            n, "temp.inst",
            "a member body that is ill-formed for these template arguments");
    if (r == analysis_result::ill_formed)
        entity_table[last_declared].defined = false;
    if (r == analysis_result::unsupported && last_declared) {
        /* what the body needs is not analysed: the function is not made,
           and a use of it is a refusal (lowering), never code without
           what the body did */
        entity_table[last_declared].defined = false;
        if (unsupported.size() > unsupported_before) {
            body_failures.append(unsupported[unsupported_before]);
            entity_table[last_declared].unsupported_body =
                static_cast<uint32_t>(body_failures.size());
            if (failure)
                *failure = static_cast<uint32_t>(body_failures.size());
        }
    }
    if (failed)
        return analysis_result::ill_formed;
    if (used && r == analysis_result::unsupported)
        return r;
    return analysis_result::well_formed;
}

analysis_result
semantic_analysis::member_function_definition(entity_index c,
                                              FunctionDefinition definition)
{
    /* [ext:gnu_attributes] before it all: member_declaration took them */
    uint32_t lead = leading_gnu_attributes(definition);
    SyntaxTail tail(definition, lead);
    /* a conversion function ([class.conv.fct]): no parameters for the
       declarator to bind, its body the function-body */
    if (entity_index f = conversion_function_declared_by(c, definition)) {
        last_declared = f;
        auto body = definition.function_body();
        if (body.deleted())
            return deleted_definition(definition, f);
        if (!body.plain())
            return not_supported_because(
                definition, "dcl.fct.def.general",
                "a conversion function body that is neither plain nor deleted");
        function_parameters.clear();
        function_empty_pack = 0;
        entity_index saved_class = defining_class;
        defining_class = c;
        analysis_result r =
            analyse_function_body(definition, body.compound_statement());
        defining_class = saved_class;
        return r;
    }
    bool constexpr_specified = false, body_virtual = false,
         body_explicit = false, body_inline = false;
    uint32_t at = 0;
    auto seq = definition.decl_specifier_seq();
    if (seq && definition.size() > leading_gnu_attributes(definition) + 2 &&
        function_specifiers_only(seq, constexpr_specified, &body_virtual,
                                 &body_explicit, &body_inline) &&
        declares_special_member(c, definition.declarator()))
        at = 1;
    if (at || !seq) {
        /* a constructor or destructor: the one this definition declared */
        qualified_type type = qualified_type::make(
            type_table.fundamental(fundamental_type::void_), cv_none);
        declarator_meaning m;
        function_cv = 0;
        in_member_declarator = true;
        reanalysing_member_declarator = true;
        entity_index saved_class = defining_class;
        defining_class = c;
        analysis_result r = declarator(definition, tail[at], type, m);
        defining_class = saved_class;
        reanalysing_member_declarator = false;
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
            return not_supported_because(
                definition, "class.ctor",
                "a special member definition that matches no declared "
                "constructor or destructor");
        return special_member_body(definition, definition.function_body());
    }
    /* [basic.scope.class]: the declarator's names are looked up in the
       class, as when its declaration was analysed (a class template's
       parameters are bound there) */
    entity_index saved_class = defining_class;
    defining_class = c;
    specified s;
    bool outer_virtual_allowed = virtual_allowed;
    virtual_allowed = true;
    placeholder_allowed = true;
    reanalysing_member_declarator = true;
    analysis_result r = decl_specifier_seq(seq, s, false);
    virtual_allowed = outer_virtual_allowed;
    qualified_type type = s.type;
    declarator_meaning m;
    if (r == analysis_result::well_formed) {
        function_cv = 0;
        in_member_declarator = true;
        r = declarator(definition, definition.declarator(), type, m);
        in_member_declarator = false;
        if (r == analysis_result::well_formed)
            r = misplaced_function_qualifiers(definition, type, true);
    }
    reanalysing_member_declarator = false;
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
        return not_supported_because(
            definition, "class.mfct",
            "a member function definition that matches no declaration");
    /* the function-body after any virt-specifier-seq */
    auto body = definition.function_body();
    if (body.deleted())
        return deleted_definition(definition, last_declared);
    if (body.defaulted()) {
        /* [class.compare.default]: the member operator== member_declarators
           declared */
        scoped_value<entity_index> compared(defaulted_comparison, c);
        return analyse_function_body(definition, body);
    }
    if (!body.plain())
        return not_supported_because(definition, "dcl.fct.def.general",
                                     "a member function body that is neither "
                                     "plain, deleted nor defaulted");
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
    SyntaxRef declaration, token_index id, entity_index qualifier,
    qualified_type type, const specified &s, exception_specification exceptions,
    bool definition)
{
    last_declared = 0;
    if (at_block_scope() || function_state.active)
        return not_supported_because(
            declaration, "class.mfct",
            "an out-of-class member definition at block scope");
    if (type_table[type.unqualified()].kind != type_kind::function)
        /* (init_declarator analyses a static data member's definition) */
        return not_supported_because(
            declaration, "class.static.data",
            "an out-of-class redeclaration that is not of function type");
    if (!definition || s.static_ || s.typedef_ || s.extern_)
        return not_supported_because(
            declaration, "class.mfct",
            "an out-of-class member function that is no definition or is "
            "static, typedef or extern");
    entity_index e = entity_table.find(qualifier, tokens.identifier(id));
    if (e && entity_table[e].kind == entity_kind::none)
        return not_supported_because(declaration, "over",
                                     "an out-of-class definition of a name "
                                     "whose declaration was not analysed");
    /* [dcl.meaning]/1: the member function of the set it declares again */
    entity_index o = 0;
    if (s.constexpr_ &&
        tokens.files.behavior.edition < standard_edition::cplusplus_14) {
        /* C++11 [dcl.constexpr]/8: the in-class declaration's constexpr
           made it const; the definition's own constexpr does as well (a
           static member function is not) */
        o = overload_of_type(e, type, function_cv | cv_const);
        if (o && entity_table[o].nonstatic_member)
            function_cv |= cv_const;
        else
            o = 0;
    }
    if (!o)
        o = overload_of_type(e, type, function_cv);
    if (o)
        e = o;
    if (!e || entity_table[e].kind != entity_kind::function ||
        !(entity_table[e].type == type) ||
        entity_table[e].member_cv != function_cv)
        return ill_formed_at_token(id,
                                   "when the declarator-id is qualified, the "
                                   "declaration shall refer to a previously "
                                   "declared member of the class",
                                   "dcl.meaning");
    VTRY(r, constexpr_redeclaration(id, e, s.constexpr_));
    VTRY(compatible,
         compatible_exceptions(
             id, e, resolved_exceptions(exceptions, type, s.gnu_flags)));
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
semantic_analysis::static_member_initializer(SyntaxRef declarator,
                                             entity_index e, SyntaxElement init,
                                             const specified &s)
{
    qualified_type type = entity_table[e].type;
    initialized_value value;
    value.evaluation = constness::unknown;
    value.value = 0;
    bool deduced = false;
    if (contains_placeholder(type)) {
        qualified_type saved = declaration_deduced;
        declaration_deduced = qualified_type();
        analysis_result r = deduced_initializer(init, e, type, value);
        declaration_deduced = saved;
        if (r != analysis_result::well_formed)
            return r;
        if (!declared_names.empty() && declared_names.back().entity == e)
            declared_names.back().type = type;
        deduced = true;
    }
    uint32_t cv = cv_of(type_table, type);
    const struct type record = type_table[type.unqualified()];
    /* [class.static.data]/4: integral or enumeration type;
       [dcl.constexpr]/1: a static constexpr data member of floating-point
       type is implicitly inline and may have an in-class initializer */
    const bool integral_or_enumeration =
        record.kind == type_kind::enumeration ||
        (record.kind == type_kind::fundamental && is_integral(type));
    const bool constexpr_floating = s.constexpr_ &&
                                    record.kind == type_kind::fundamental &&
                                    is_floating(type);
    /* (and of a literal class, array or pointer type: [dcl.constexpr]/10,
       the value the evaluator's as a constexpr variable's) */
    const bool constexpr_class =
        s.constexpr_ &&
        (record.kind == type_kind::class_type ||
         record.kind == type_kind::array || record.kind == type_kind::pointer);
    /* [expr.const]/3: until the initializer is analysed (and if analysis
       has no rules for it) whether a member that may be usable in constant
       expressions is one, and its value, are unknown: a later member's
       initializer that names it is not thereby not a constant expression */
    if (s.constexpr_ ||
        ((cv & cv_const) && !(cv & cv_volatile) && integral_or_enumeration))
        entity_table[e].usable = constness::unknown;
    const bool dynamic_inline =
        s.inline_ && !s.constexpr_ &&
        !((cv & cv_const) && !(cv & cv_volatile) && integral_or_enumeration);
    if (dynamic_inline) {
        if (!deduced) {
            VTRY(r, initializer(init, type, value));
        }
        entity &x = entity_table[e];
        x.definition = value.node;
        x.inline_ = true;
        x.defined = true;
        return analysis_result::well_formed;
    }
    if (!(cv & cv_const) || (cv & cv_volatile) ||
        (!integral_or_enumeration && !constexpr_floating && !constexpr_class))
        return not_supported_because(
            declarator, "class.static.data",
            "an in-class initializer of a static data member of a non-const, "
            "volatile or unhandled type");
    analysis_result r = analysis_result::well_formed;
    bool wide = false;
    if (!deduced)
        VTRY_SET(r, initializer(init, type, value));
    /* [expr.const]/5: an initializer that calls a function (a constexpr
       function, or a builtin clang evaluates) is the evaluator's */
    if (constexpr_class && value.node) {
        uint32_t data = 0;
        constness c =
            evaluate_variable(type, value.node, semantics[value.node].at, data);
        if (c == constness::unknown)
            return not_supported_because(
                declarator, "expr.const",
                "a static data member initializer that could not be evaluated");
        value.evaluation = c;
        if (data)
            value.node = data;
    } else if (value.evaluation == constness::unknown && value.node &&
               keep_wide_value(e, value.node)) {
        /* (a 128-bit value: the evaluator's, the initializer as written) */
        wide = true;
    } else if (value.evaluation == constness::unknown && value.node) {
        constant_value v;
        bool constant = false;
        VTRY_SET(r, evaluate_constant(declarator, value.node, v, constant));
        if (constant) {
            expression_meaning folded;
            folded.type = semantics[value.node].type;
            folded.category = value_category::prvalue;
            evaluated_meaning(folded, v);
            value.node = expression_node(semantic_kind::constant, folded,
                                         semantics[value.node].at);
            value.evaluation = constness::constant;
            value.value = folded.value;
        }
    }
    if (value.evaluation == constness::not_constant)
        return ill_formed(declarator,
                          "every initializer-clause of a static data "
                          "member's in-class initializer shall be a "
                          "constant expression",
                          "class.static.data");
    if (value.evaluation != constness::constant && !wide)
        return not_supported_because(
            declarator, "expr.const",
            "a static data member initializer not established to be constant");
    entity &x = entity_table[e];
    if (!wide) {
        x.usable = constness::constant;
        x.value = value.value;
    }
    x.definition = value.node;
    if (s.constexpr_ || s.inline_) {
        /* [dcl.inline]/1 (C++17): "A function or static data member
           declared with the constexpr specifier is implicitly an inline
           function or variable"; this declaration (of it or of an inline
           one) is its definition ([basic.def]/2.3), and an odr-use emits it
           (COMDAT, [impl.clang] as clang) */
        x.constexpr_ = s.constexpr_;
        /* before C++17 it is not inline: the declaration in the class is not
           a definition, as for a const integral member ([class.static.data]/3,
           C++14) */
        if (s.inline_ ||
            tokens.files.behavior.edition >= standard_edition::cplusplus_17) {
            x.inline_ = true;
            x.defined = true;
        }
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
    SyntaxRef declaration, token_index id, entity_index qualifier,
    qualified_type type, const specified &s, bool definition,
    entity_index &declared_entity)
{
    declared_entity = 0;
    if (at_block_scope() || function_state.active)
        return not_supported_because(
            declaration, "class.static.data",
            "a static data member definition at block scope");
    if (s.typedef_)
        return not_supported_because(
            declaration, "class.static.data",
            "a typedef definition of a static data member");
    /* [dcl.inline]/6 and [class.static.data]/3: an inline or constexpr
       definition makes it an inline variable (C++17) */
    if ((s.inline_ || s.constexpr_) &&
        tokens.files.behavior.edition < standard_edition::cplusplus_17)
        return not_supported_because(
            declaration, "class.static.data",
            "an inline static data member definition before C++17");
    /* static or extern on the definition: clang's error */
    if (s.static_ || s.extern_)
        return not_supported_because(
            declaration, "class.static.data",
            "a static data member definition declared static or extern");
    entity_index e = entity_table.find(qualifier, tokens.identifier(id));
    if (e && entity_table[e].kind == entity_kind::none)
        return not_supported_because(declaration, "class.static.data",
                                     "a static data member definition of a "
                                     "name whose declaration was not analysed");
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
    /* [temp.expl.spec]/14: not a definition */
    if (specialization_declares_member) {
        if (type_table[type.unqualified()].kind == type_kind::array &&
            type_table[type.unqualified()].bound_known)
            entity_table[e].type = type;
        entity_table.add_declaration(e, declaration);
        /* the specialization is a declaration of its own, not a
           redeclaration of the template's member: its chain starts here */
        entity_table[e].first_declaration = entity_table[e].last_declaration;
        declared_entity = e;
        declared_name d;
        d.declaration = declaration;
        d.outermost = current_declaration;
        d.entity = e;
        d.id = id;
        d.kind = entity_kind::variable;
        d.type = type;
        d.claimable = declarations_claimable;
        declared_names.append(d);
        return analysis_result::well_formed;
    }
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
    declared_names.append(d);
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
        return one(d);
    if (!d.is(syntax_kind::virt_specifier_seq))
        return false;
    for (auto t : d.elements())
        if (!one(t))
            return false;
    return true;
}

/* [dcl.attr.grammar]/3 and GCC's aligned: [[gnu::aligned(N)]] alone, the
   standard syntax of what __attribute__((aligned(N))) says.  A class
   template's head names it; an instantiation asks for the layout
   (class_definition) and is not analysed yet. */
static bool only_gnu_aligned(token_stream &tokens, SyntaxElement c)
{
    if (!c.is(syntax_kind::attribute_specifier))
        return false;
    bool seen = false;
    for (auto k : c.elements()) {
        if (k.is_token())
            continue;
        if (!k.is(syntax_kind::attribute) || !k.size() ||
            !k[0].is(syntax_kind::attribute_scoped_token) || k[0].size() != 3)
            return false;
        owned_buffer ns, name;
        tokens.spelling(k[0][0].token(), ns.get());
        tokens.spelling(k[0][2].token(), name.get());
        bool gnu = (ns.size() == 3 && !memcmp(ns.data(), "gnu", 3)) ||
                   (ns.size() == 7 && !memcmp(ns.data(), "__gnu__", 7));
        bool aligned =
            (name.size() == 7 && !memcmp(name.data(), "aligned", 7)) ||
            (name.size() == 11 && !memcmp(name.data(), "__aligned__", 11));
        if (!gnu || !aligned)
            return false;
        seen = true;
    }
    return seen;
}

/* a class-head's class-head-name (or what is in its place), after the
   class-key and alignment-specifiers; none in an unnamed class's */
SyntaxElement semantic_analysis::class_head_name(SyntaxRef head) const
{
    for (auto c : head.elements(1)) {
        if (c.is(syntax_kind::alignment_specifier) ||
            class_head_attributes_without_effect(c) ||
            (c.is_node() && gnu_or_without_effect(c)) ||
            only_gnu_aligned(tokens, c))
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
    if (n.size() != 4)
        return not_supported_because(n, "dcl.align",
                                     "an alignas specifier of an unhandled "
                                     "form, such as a pack expansion");
    if (n[2].is(syntax_kind::type_id)) {
        qualified_type t;
        VTRY(r, type_id(n[2], t));
        const struct type &record = type_table[t.unqualified()];
        if (record.kind == type_kind::function ||
            (record.kind == type_kind::array && !record.bound_known))
            return not_supported_because(
                n, "dcl.align",
                "alignas of a function type or an array of unknown bound");
        alignment = alignment_of_type(t);
        /* an incomplete type, void: not known */
        if (!alignment)
            return not_supported_because(
                n, "dcl.align", "alignas of a type with unknown alignment");
        return analysis_result::well_formed;
    }
    expression_meaning e;
    VTRY(r, expression(n[2], e));
    /* "an integral constant expression" is converted to a prvalue
       ([expr.const]/8): a constexpr variable's value */
    VTRY_SET(r, operand_value(n, e));
    if (!is_integral(e.type) || e.category != value_category::prvalue)
        return not_supported_because(
            n, "dcl.align",
            "an alignas operand that is not an integral prvalue");
    bool constant = false;
    VTRY_SET(r, constant_expression(n, e, constant));
    if (!constant)
        return not_supported_because(
            n, "dcl.align",
            "an alignas operand not established to be constant");
    alignment = e.value;
    /* [basic.align]/4: "Every alignment value shall be a non-negative
       integral power of two"; clang's limit 2^29 */
    if (alignment & (alignment - 1))
        return ill_formed(n, "an alignment shall be a power of two",
                          "dcl.align");
    /* clang: "requested alignment must be 4294967296 bytes or smaller"
       (Linux); the entities keep 32 bits of alignment */
    if (alignment > (uint64_t(1) << 32))
        return ill_formed(n, "the requested alignment is too large",
                          "dcl.align");
    if (alignment > (uint64_t(1) << 31))
        return not_supported_because(n, "dcl.align", "an alignment above 2^31");
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
