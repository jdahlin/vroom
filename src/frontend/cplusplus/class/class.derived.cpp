/*
 * class.derived.cpp: [class.derived] Derived classes: the base-clause of a
 * class-head ([class.pre]).
 *
 *     base-clause:
 *         : base-specifier-list
 *
 *     base-specifier-list:
 *         base-specifier ..._opt
 *         base-specifier-list , base-specifier ..._opt
 *
 * [class.derived]/2: "The class-or-decltype shall denote a (possibly
 * cv-qualified) class type that is not an incompletely defined class";
 * /1: "Unless redeclared in the derived class, members of a base class are
 * also considered to be members of the derived class."  A base the parser
 * knows (a class it has read the member-specification of) is searched
 * after the derived class's own region; of another base nothing is known,
 * and a name not found in the class is unknown, as before.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

node_index parser::base_clause(scope_index derived)
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* : */
    if (!comma_list(syntax_kind::base_specifier_list,
                    [this, derived] { base_specifier(derived); }))
        return 0;
    return finish_node(syntax_kind::base_clause, mark, first);
}

/*
 *     base-specifier:
 *         attribute-specifier-seq_opt class-or-decltype
 *         attribute-specifier-seq_opt virtual access-specifier_opt class-or-decltype
 *         attribute-specifier-seq_opt access-specifier virtual_opt class-or-decltype
 *
 * A class-or-decltype alone passes through.
 */
node_index parser::base_specifier(scope_index derived)
{
    size_t mark = begin_node();
    token_index first = position;
    attribute_specifier_seq();
    if (failed())
        return 0;
    auto access = [&] {
        return accept(standard_keyword::private_) ||
               accept(standard_keyword::protected_) ||
               accept(standard_keyword::public_);
    };
    if (accept(standard_keyword::virtual_))
        access();
    else if (access())
        accept(standard_keyword::virtual_);
    /* class-or-decltype: a class template's name without a
       template-argument-list is a type-name only as the injected-class-name
       in its own scope ([temp.local]/1); a placeholder for a deduced class
       type is no base ([dcl.type.class.deduct]/2) */
    if (at_identifier() && !at(preprocessing_op_or_punc::LESS, 1) &&
        !at(preprocessing_op_or_punc::COLON_COLON, 1)) {
        scope_index found = 0;
        if (names.lookup_unqualified(tokens.identifier(position), &found) ==
                name_kind::template_name &&
            !(found && names.encloses_current(found)))
            return violates("a template-argument-list after a class "
                            "template's name in a base-specifier",
                            "class.derived", position);
    }
    scope_index base = 0;
    class_or_decltype(&base);
    if (failed())
        return 0;
    if (base)
        names.add_base(derived, base);
    if (pending.size() == mark + 1)
        return 0;
    return finish_node(syntax_kind::base_specifier, mark, first);
}

/*
 *     class-or-decltype:
 *         nested-name-specifier_opt type-name
 *         nested-name-specifier template simple-template-id
 *         decltype-specifier
 *
 * [class.derived]/2: "lookup for the
 * component name of the type-name or simple-template-id is type-only": the
 * name is a type-name whatever else it could name, so it is not looked up
 * as an expression's would be.  A type-name alone passes through.
 */
node_index parser::class_or_decltype(scope_index *region)
{
    *region = 0;
    size_t mark = begin_node();
    token_index first = position;
    if (at(standard_keyword::decltype_) &&
        !at(preprocessing_op_or_punc::COLON_COLON, group_end(1)))
        return decltype_specifier();
    scope_index scope = 0;
    bool known = false;
    node_index qualifier = nested_name_specifier(&scope, &known);
    if (failed())
        return 0;
    bool dependent_qualifier = qualifier && qualifier_dependent;
    bool template_keyword = qualifier && accept(standard_keyword::template_);
    if (!at_identifier())
        return expected("a class-name", "class.derived");
    uint32_t name = tokens.identifier(position);
    size_t id_mark = begin_node();
    token_index id_first = position;
    add_token();
    scope_index found = 0;
    name_kind kind = name_kind::not_declared;
    if (!qualifier)
        kind = names.lookup_unqualified(name, &found);
    else if (known)
        kind = names.lookup_qualified(scope, name, &found);
    if (at(preprocessing_op_or_punc::LESS) &&
        (template_keyword || is_template_kind(kind) || (qualifier && !known))) {
        if (dependent_qualifier && !template_keyword)
            return violates("template before a dependent template-name",
                            "temp.names", id_first);
        template_id_rest(id_mark, id_first, syntax_kind::simple_template_id);
        if (failed())
            return 0;
        /* a specialization's members are the primary template's, as far
           as the parser knows them */
        if (kind == name_kind::template_name && found &&
            names.kind_of(found) == scope_kind::class_)
            *region = found;
        if (!qualifier)
            return 0;
        return finish_node(syntax_kind::class_or_decltype, mark, first);
    }
    if (template_keyword)
        return expected("a simple-template-id after template", "class.derived");
    /* [class.derived]/2: a class-or-decltype names a class (a
       mem-initializer-id a class or member, [class.base.init]/2): a name
       lookup does not find may be declared where the parser kept the
       tokens, so the declaration is a later step's */
    if (kind == name_kind::not_declared && (!qualifier || known)) {
        unknown_name = true;
        return expected("a class-name", "class.derived");
    }
    if ((kind == name_kind::type_name || kind == name_kind::template_name) &&
        found && names.kind_of(found) == scope_kind::class_)
        *region = found;
    if (!qualifier)
        return 0;
    return finish_node(syntax_kind::class_or_decltype, mark, first);
}

/* ---- semantic analysis ---------------------------------------------- */

namespace
{

uint64_t align_to(uint64_t n, uint64_t alignment)
{
    return (n + alignment - 1) / alignment * alignment;
}

} // namespace

/*
 * [abi:pod]: "POD for the purpose of layout": a class that is a POD in
 * C++03's sense, whose tail padding a derived class does not reuse.
 * C++03's POD class is an aggregate (no user-declared constructors, no
 * private or protected non-static data members, no base classes, no
 * virtual functions) without a user-declared destructor whose non-static
 * data members are PODs.
 */
static bool pod_for_layout(const semantic_analysis &a, const types &table,
                           entity_index c)
{
    const entity &x = a.entity_table[c];
    if (x.first_constructor || x.destructor || x.base_count)
        return false;
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = a.entity_table[a.class_members[x.first_member + i]];
        if (m.access)
            return false;
        qualified_type t = m.type;
        while (table[t.unqualified()].kind == type_kind::array)
            t = table[t.unqualified()].operand;
        if (table[t.unqualified()].kind == type_kind::class_type &&
            !pod_for_layout(a, table, table[t.unqualified()].entity))
            return false;
    }
    return true;
}

/* whether class c has a subobject of class type `of` at offset 0: itself,
   or one of its own at offset 0 ([abi:layout]'s empty base conflicts) */
static bool at_offset_zero(const semantic_analysis &a, const types &table,
                           entity_index c, entity_index of)
{
    if (c == of)
        return true;
    const entity &x = a.entity_table[c];
    for (uint32_t i = 0; i < x.base_count; i++) {
        const entity &b = a.entity_table[a.class_bases[x.first_base + i]];
        if (!b.offset &&
            at_offset_zero(a, table, table[b.type.unqualified()].entity, of))
            return true;
    }
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = a.entity_table[a.class_members[x.first_member + i]];
        qualified_type t = m.type;
        while (table[t.unqualified()].kind == type_kind::array)
            t = table[t.unqualified()].operand;
        if (!m.offset && table[t.unqualified()].kind == type_kind::class_type &&
            at_offset_zero(a, table, table[t.unqualified()].entity, of))
            return true;
    }
    return false;
}

/* whether a subobject at offset 0 of class c2 (c2 itself, or a base or
   member it has there) has the type of one at offset 0 of class c1: the
   two cannot both be placed at one address ([abi:layout]) */
static bool share_offset_zero(const semantic_analysis &a, const types &table,
                              entity_index c1, entity_index c2)
{
    if (at_offset_zero(a, table, c1, c2))
        return true;
    const entity &x = a.entity_table[c2];
    for (uint32_t i = 0; i < x.base_count; i++) {
        const entity &b = a.entity_table[a.class_bases[x.first_base + i]];
        if (!b.offset &&
            share_offset_zero(a, table, c1, table[b.type.unqualified()].entity))
            return true;
    }
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = a.entity_table[a.class_members[x.first_member + i]];
        qualified_type t = m.type;
        while (table[t.unqualified()].kind == type_kind::array)
            t = table[t.unqualified()].operand;
        if (!m.offset && table[t.unqualified()].kind == type_kind::class_type &&
            share_offset_zero(a, table, c1, table[t.unqualified()].entity))
            return true;
    }
    return false;
}

/*
 * The base-clause of the class c being defined, defined with class-key
 * key.  [class.derived]/2: "A class-or-decltype shall denote a (possibly
 * cv-qualified) class type that is not an incompletely defined class
 * ([class.mem]); any cv-qualifiers are ignored.  The class denoted by the
 * class-or-decltype of a base-specifier is called a direct base class for
 * the class being defined."  [class.mi]/3: "A class shall not be specified
 * as a direct base class of a derived class more than once."
 * [class.access.base]/2: "In the absence of an access-specifier for a base
 * class, public is assumed when the derived class is defined with the
 * class-key struct and private is assumed when the class is defined with
 * the class-key class."
 *
 * [abi:layout] places each non-virtual base in declaration order: an
 * empty one at offset 0 (if no other subobject of its type is there), any
 * other at the data size so far rounded up to its alignment, taking its
 * data size (dsize) unless it is POD for the purpose of layout
 * ([abi:pod]), whose size it takes.  Virtual bases ([class.mi]/4) are not
 * analysed.
 */
analysis_result semantic_analysis::base_clause(entity_index c, node_index n,
                                               standard_keyword key)
{
    owned_array<syntax_child> specifiers;
    for (uint32_t i = 1; i < tree[n].child_count; i++) {
        syntax_child item = tree.children(n)[i];
        if (syntax_is(tree, item, syntax_kind::base_specifier_list)) {
            for (uint32_t k = 0; k < tree[item.index()].child_count; k++) {
                syntax_child one = tree.children(item.index())[k];
                if (!syntax_is_punctuator(tokens, one,
                                          preprocessing_op_or_punc::COMMA))
                    specifiers.append(one);
            }
        } else {
            specifiers.append(item);
        }
    }
    entity_table[c].first_base = static_cast<uint32_t>(class_bases.size());
    entity_table[c].base_count = 0;
    for (syntax_child specifier : specifiers) {
        uint8_t access = 0;
        if (key == standard_keyword::class_)
            access = 2;
        syntax_child name = specifier;
        if (syntax_is(tree, specifier, syntax_kind::base_specifier)) {
            const syntax_node &b = tree[specifier.index()];
            const syntax_child *parts = tree.children(specifier.index());
            name = parts[b.child_count - 1];
            for (uint32_t k = 0; k + 1 < b.child_count; k++) {
                if (!parts[k].is_token())
                    /* an attribute-specifier-seq */
                    return not_supported(specifier.index(), "class.derived");
                switch (p.keyword_of(tokens[parts[k].index()])) {
                case standard_keyword::public_:
                    access = 0;
                    break;
                case standard_keyword::protected_:
                    access = 1;
                    break;
                case standard_keyword::private_:
                    access = 2;
                    break;
                case standard_keyword::virtual_:
                    return not_supported(specifier.index(), "class.mi");
                default:
                    return not_supported(specifier.index(), "class.derived");
                }
            }
        }
        /* [class.derived]/2: "A class-or-decltype shall denote a (possibly
           cv-qualified) class type that is not an incompletely defined class
           ... During the lookup for a base class name, non-type names are
           ignored": a type-name, or one after a nested-name-specifier (a
           simple-template-id or decltype has no rules yet) */
        token_index id = 0;
        entity_index base = 0;
        if (!name.is_token() &&
            !syntax_is(tree, name, syntax_kind::class_or_decltype))
            return not_supported(n, "class.derived");
        {
            size_t before = unsupported.size();
            analysis_result named = named_class(name, n, base, id);
            if (named == analysis_result::ill_formed)
                return named;
            if (named != analysis_result::well_formed) {
                /* a class whose definition was not analysed, a name not
                   found */
                unsupported.resize(before);
                return not_supported(n, "class.derived");
            }
        }
        const entity &bx = entity_table[base];
        if (!bx.size)
            return ill_formed_at_token(id,
                                       "the class-or-decltype shall denote a "
                                       "class type that is not an "
                                       "incompletely defined class",
                                       "class.derived");
        for (uint32_t k = 0; k < entity_table[c].base_count; k++)
            if (entity_table[class_bases[entity_table[c].first_base + k]]
                    .type.unqualified() == bx.type.unqualified())
                return ill_formed_at_token(id,
                                           "a class shall not be specified as "
                                           "a direct base class of a derived "
                                           "class more than once",
                                           "class.mi");
        entity_index e = entity_table.make(entity_kind::variable, 0, c);
        entity_table[e].type = entity_table[base].type;
        entity_table[e].nonstatic_member = true;
        entity_table[e].base_subobject = true;
        entity_table[e].defined = true;
        entity_table[e].access = access;
        entity_table.add_declaration(e, current_declaration);
        uint32_t alignment = entity_table[base].alignment;
        if (alignment > layout_alignment)
            layout_alignment = alignment;
        if (!entity_table[base].member_count) {
            /* [class.derived]/4 (Note): "A base class subobject may be of
               zero size"; [abi:layout]: an empty base at offset 0, unless
               a subobject of its type is already there */
            for (uint32_t k = 0; k < entity_table[c].base_count; k++)
                if (share_offset_zero(
                        *this, type_table,
                        type_table
                            [entity_table
                                 [class_bases[entity_table[c].first_base + k]]
                                     .type.unqualified()]
                                .entity,
                        base))
                    return not_supported(n, "class.derived");
            entity_table[e].field = UINT32_MAX;
            entity_table[e].offset = 0;
        } else {
            entity_table[e].field = entity_table[c].member_count;
            entity_table[e].offset = align_to(layout_size, alignment);
            /* at offset 0 with an empty base placed there before */
            for (uint32_t k = 0;
                 !entity_table[e].offset && k < entity_table[c].base_count; k++)
                if (share_offset_zero(
                        *this, type_table,
                        type_table
                            [entity_table
                                 [class_bases[entity_table[c].first_base + k]]
                                     .type.unqualified()]
                                .entity,
                        base))
                    return not_supported(n, "class.derived");
            uint64_t taken = entity_table[base].data_size;
            if (pod_for_layout(*this, type_table, base))
                taken = entity_table[base].size;
            layout_size = entity_table[e].offset + taken;
            class_members.append(e);
            entity_table[c].member_count++;
        }
        class_bases.append(e);
        entity_table[c].base_count++;
    }
    return analysis_result::well_formed;
}

/* [abi:layout]: a data member of class type placed at offset 0 where an
   empty base of its type (or one it has at offset 0) is: not analysed */
analysis_result semantic_analysis::empty_base_conflict(entity_index c,
                                                       node_index n)
{
    const entity &x = entity_table[c];
    if (!x.member_count)
        return analysis_result::well_formed;
    const entity &first = entity_table[class_members[x.first_member]];
    if (first.base_subobject || first.offset)
        return analysis_result::well_formed;
    qualified_type t = first.type;
    while (type_table[t.unqualified()].kind == type_kind::array)
        t = type_table[t.unqualified()].operand;
    if (type_table[t.unqualified()].kind != type_kind::class_type)
        return analysis_result::well_formed;
    for (uint32_t i = 0; i < x.base_count; i++) {
        const entity &b = entity_table[class_bases[x.first_base + i]];
        if (b.field == UINT32_MAX &&
            share_offset_zero(*this, type_table,
                              type_table[b.type.unqualified()].entity,
                              type_table[t.unqualified()].entity))
            return not_supported(n, "class.derived");
    }
    return analysis_result::well_formed;
}

/*
 * [class.derived]/2: "A class B is a base class of a class D if it is a
 * direct base class of D or a direct base class of one of D's base
 * classes."  The direct base subobject of c through which base is
 * reached; ambiguous ([class.member.lookup]/6, [conv.ptr]/3) when two of
 * them lead to it.
 */
entity_index semantic_analysis::base_step(entity_index c, entity_index base,
                                          bool &ambiguous) const
{
    const entity &x = entity_table[c];
    entity_index found = 0;
    for (uint32_t i = 0; i < x.base_count; i++) {
        entity_index b = class_bases[x.first_base + i];
        entity_index of = type_table[entity_table[b].type.unqualified()].entity;
        bool inner = false;
        if (of == base || base_step(of, base, inner)) {
            if (found || inner)
                ambiguous = true;
            found = b;
        }
    }
    return found;
}

/*
 * [class.member.lookup]/1-/6: "The lookup set for N in C ... If C contains
 * a declaration of the name N, ... the result is S(N,C).  Otherwise ...
 * S(N,C) is calculated as follows: ... merge each such lookup set S(N,Bi)
 * in turn into S(N,C)"; /6: "if the declaration sets differ, the merge is
 * ambiguous".  A name declared in two bases is ambiguous here even when
 * it is the same static member ([class.member.lookup]/6.2 allows that).
 */
entity_index semantic_analysis::class_member_lookup(entity_index c,
                                                    uint32_t name,
                                                    bool &ambiguous) const
{
    entity_index e = entity_table.find(c, name);
    if (e)
        return e;
    const entity &x = entity_table[c];
    entity_index found = 0;
    for (uint32_t i = 0; i < x.base_count; i++) {
        entity_index of = type_table[entity_table[class_bases[x.first_base + i]]
                                         .type.unqualified()]
                              .entity;
        entity_index one = class_member_lookup(of, name, ambiguous);
        if (one && found && one != found)
            ambiguous = true;
        if (one)
            found = one;
    }
    return found;
}

/*
 * The glvalue m, of a class derived from base, as its base class
 * subobject: [expr.ref]/6.2 for each base on the way (a base subobject is
 * a member subobject, [intro.object]/2), the value category kept.
 * [class.access.base]/4: the base is accessible where an invented public
 * member of it would be: through a public base anywhere, through a
 * private or protected one in the derived class's own members (friends
 * and further derived classes are not analysed).
 */
analysis_result semantic_analysis::to_base(token_index at, entity_index base,
                                           expression_meaning &m)
{
    if (aggregate_prvalue(m) && type_table[m.type.unqualified()].entity != base)
        return not_supported(current_declaration, "class.temporary");
    for (;;) {
        entity_index c = type_table[m.type.unqualified()].entity;
        if (c == base)
            return analysis_result::well_formed;
        bool ambiguous = false;
        entity_index step = base_step(c, base, ambiguous);
        if (!step)
            return not_supported(current_declaration, "class.derived");
        if (ambiguous)
            return not_supported(current_declaration, "class.member.lookup");
        if (entity_table[step].access && current_class() != c) {
            if (current_class())
                return not_supported(current_declaration, "class.access.base");
            return ill_formed_at_token(at,
                                       "a base class is accessible only if "
                                       "an invented public member of it is",
                                       "class.access.base");
        }
        expression_meaning result;
        result.type = qualified_type::make(
            entity_table[step].type.unqualified(), m.type.cv());
        /* [expr.ref]/6.2: "If E1 is an lvalue, then E1.E2 is an lvalue;
           otherwise E1.E2 is an xvalue" */
        result.category = value_category::xvalue;
        if (m.category == value_category::lvalue)
            result.category = value_category::lvalue;
        result.evaluation = m.evaluation;
        if (result.evaluation == constness::constant)
            result.evaluation = constness::unknown;
        if (entity_table[step].field != UINT32_MAX) {
            result.node = expression_node(semantic_kind::member, result, at,
                                          m.node, step);
        } else if (m.category != value_category::lvalue) {
            return not_supported(current_declaration, "class.temporary");
        } else {
            /* an empty base, at the object's address: *(B *)&object */
            expression_meaning address;
            address.type =
                qualified_type::make(type_table.pointer_to(m.type), cv_none);
            address.category = value_category::prvalue;
            address.evaluation = constness::not_constant;
            address.node =
                expression_node(semantic_kind::address, address, at, m.node);
            address.type = qualified_type::make(
                type_table.pointer_to(result.type), cv_none);
            converted(address, conversion_kind::derived_to_base);
            semantics[address.node].b = step;
            result.node = expression_node(semantic_kind::indirection, result,
                                          at, address.node);
        }
        m = result;
    }
}

/*
 * [conv.ptr]/3: "A prvalue of type "pointer to cv D", where D is a complete
 * class type, can be converted to a prvalue of type "pointer to cv B",
 * where B is a base class of D.  If B is an inaccessible or ambiguous base
 * class of D, a program that necessitates this conversion is ill-formed.
 * The result of the conversion is a pointer to the base class subobject
 * of the derived class object.  The null pointer value is converted to the
 * null pointer value of the destination type."  m, a pointer to class
 * `from`, to a pointer to its base `to` (to's cv-qualified type in type):
 * a base at offset 0 only, whose address is the object's.
 */
analysis_result semantic_analysis::pointer_to_base(token_index at,
                                                   entity_index from,
                                                   entity_index to,
                                                   qualified_type type,
                                                   expression_meaning &m)
{
    uint64_t offset = 0;
    entity_index first = 0;
    for (entity_index c = from; c != to;) {
        bool ambiguous = false;
        entity_index step = base_step(c, to, ambiguous);
        if (ambiguous)
            return not_supported(current_declaration, "class.member.lookup");
        if (entity_table[step].access && current_class() != c) {
            if (current_class())
                return not_supported(current_declaration, "class.access.base");
            return ill_formed_at_token(at,
                                       "if B is an inaccessible base class of "
                                       "D, a program that necessitates this "
                                       "conversion is ill-formed",
                                       "conv.ptr");
        }
        if (!first)
            first = step;
        offset += entity_table[step].offset;
        c = type_table[entity_table[step].type.unqualified()].entity;
    }
    if (offset)
        /* the null pointer check before the offset added */
        return not_supported(current_declaration, "conv.ptr");
    m.type = type;
    converted(m, conversion_kind::derived_to_base);
    if (m.node)
        semantics[m.node].b = first;
    return analysis_result::well_formed;
}

} // namespace cplusplus
