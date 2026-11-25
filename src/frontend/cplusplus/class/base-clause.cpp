/*
 * base-clause.cpp: [class.derived] Derived classes: the base-clause of a
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
            names.kind_of(found) == scope_kind::class_) {
            *region = found;
            /* [temp.expl.spec]/7: an explicit specialization has its own
               members: those of the one these arguments name */
            for (size_t i = specialization_regions.size(); i-- > 0;) {
                const specialization_region &r = specialization_regions[i];
                if (r.primary == found &&
                    same_template_id(r.first, id_first, r.end - r.first)) {
                    *region = r.region;
                    break;
                }
            }
        }
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
    /* a member of a base the parser does not know (a class template whose
       base is a built-in, a dependent base): it is in the class's scope
       for the semantic analysis, as after the nested-name-specifiers of an
       expression (nested_name_specifier) */
    bool in_class =
        qualifier && known && names.kind_of(scope) == scope_kind::class_;
    if (kind == name_kind::not_declared && (!qualifier || known) && !in_class) {
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

/*
 * [class.derived]/2: the class a base-specifier's class-or-decltype
 * denotes (base) and the token to report it at (id): a type-name, one
 * after a nested-name-specifier, or a simple-template-id naming a class
 * template's specialization ([temp.names]/1; decltype has no rules yet)
 */
analysis_result semantic_analysis::base_class_name(SyntaxElement name,
                                                   SyntaxRef n,
                                                   entity_index &base,
                                                   token_index &id)
{
    if (name.is(syntax_kind::simple_template_id)) {
        type_index t = 0;
        VTRY(r, template_type(name, 0, false, t));
        id = name.first_token();
        if (type_table[t].kind != type_kind::class_type)
            return not_supported_because(
                n, "class.derived",
                "a base simple-template-id that does not name a class type");
        base = type_table[t].entity;
        return analysis_result::well_formed;
    }
    if (name.is(syntax_kind::class_or_decltype) && name.size() == 2 &&
        name[0].is(syntax_kind::nested_name_specifier) &&
        name[1].is(syntax_kind::simple_template_id)) {
        type_index t = 0;
        uint32_t cv = 0;
        VTRY(r, qualified_type_name(name, t, cv));
        id = name[1].first_token();
        if (type_table[t].kind != type_kind::class_type)
            return not_supported_because(
                n, "class.derived",
                "a qualified base template-id that does not name a class type");
        base = type_table[t].entity;
        return analysis_result::well_formed;
    }
    if (!name.is_token() && !name.is(syntax_kind::class_or_decltype))
        return not_supported_because(
            n, "class.derived",
            "a base specified by a node that is no class-or-decltype");
    size_t before = unsupported.size();
    analysis_result named = named_class(name, n, base, id);
    if (named == analysis_result::ill_formed)
        return named;
    if (named != analysis_result::well_formed) {
        /* a class whose definition was not analysed, a name not found */
        unsupported.resize(before);
        /* a base a refused declaration held: that declaration's reason */
        if (name.is_token() &&
            tokens[name.token()].kind == token_kind::identifier) {
            lookup_result found = lookup_unqualified(
                tokens.identifier(name.token()), lookup_filter::types);
            if (found.unknown) {
                analysis_result held = not_supported(
                    n, unknown_name_subclause(found, "class.derived"));
                name_lookup_detail(name.token(), found);
                return held;
            }
        }
        return not_supported_because(
            n, "class.derived",
            "a base name for which no analysed class was found");
    }
    return analysis_result::well_formed;
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
 * Each base class subobject gets its offset with the class's layout, at
 * the closing } ([abi:class-types], abi/itanium/layout.cpp); one that is
 * not empty is also a field of the class.  [class.mi]/4: "A base class
 * specifier that contains the keyword virtual specifies a virtual base
 * class.  For each distinct base class that is specified virtual, the
 * most derived object shall contain a single base class subobject of that
 * type": a virtual base is no field, and the class's virtual bases, direct
 * and indirect, are listed with it (entity::first_virtual_base) in the
 * order of their construction ([class.base.init]/13.1), each a subobject
 * entity whose offset the layout gives (abi/itanium/layout.cpp).
 */
analysis_result semantic_analysis::base_clause(entity_index c, BaseClause n,
                                               standard_keyword key)
{
    /* [temp.spec]/6: a base-clause is checked */
    flag_set checked(explicit_declaration_names, false);
    /* [temp.variadic]/5.3: "In a base-specifier-list; the pattern is a
       base-specifier": a base-specifier followed by an ellipsis is one
       base per element of the packs it names (an instantiation's; in a
       template's own definition they are not known) */
    owned_array<SyntaxElement> listed;
    owned_array<bool> expanded;
    for (auto item : n.elements(1)) {
        if (!item.is(syntax_kind::base_specifier_list)) {
            listed.append(item);
            expanded.append(false);
            continue;
        }
        for (SyntaxElement one : item.elements()) {
            if (one.is(preprocessing_op_or_punc::COMMA))
                continue;
            if (one.is(preprocessing_op_or_punc::ELLIPSIS)) {
                if (expanded.empty())
                    return not_supported_because(
                        n, "temp.variadic",
                        "an ellipsis with no preceding base-specifier");
                expanded[expanded.size() - 1] = true;
                continue;
            }
            listed.append(one);
            expanded.append(false);
        }
    }
    owned_array<SyntaxElement> specifiers;
    /* each specifier's pack element, UINT32_MAX for none */
    owned_array<uint32_t> elements;
    for (size_t i = 0; i < listed.size(); i++) {
        if (!expanded[i]) {
            specifiers.append(listed[i]);
            elements.append(UINT32_MAX);
            continue;
        }
        token_index first = listed[i].token(), end = first + 1;
        if (!listed[i].is_token()) {
            first = listed[i].first_token();
            end = listed[i].end_token();
        }
        uint32_t count = 0;
        if (!expansion_count(first, end, count))
            return not_supported_because(
                n, "temp.variadic",
                "a base-specifier pack expansion whose length is not known");
        for (uint32_t e = 0; e < count; e++) {
            specifiers.append(listed[i]);
            elements.append(e);
        }
    }
    /* [class.derived]/2: "A class-or-decltype shall denote a (possibly
       cv-qualified) class type that is not an incompletely defined class
       ... During the lookup for a base class name, non-type names are
       ignored" (base_class_name).  All named first: a specialization
       instantiated here lists its own bases in class_bases, which must
       come before this class's range */
    owned_array<entity_index> named_bases;
    owned_array<token_index> named_ids;
    for (size_t si = 0; si < specifiers.size(); si++) {
        SyntaxElement name = specifiers[si];
        if (name.is(syntax_kind::base_specifier))
            name = name[name.size() - 1];
        token_index id = 0;
        entity_index base = 0;
        analysis_result named = analysis_result::well_formed;
        {
            expansion_scope scope(*this, elements[si] != UINT32_MAX,
                                  elements[si]);
            named = base_class_name(name, n, base, id);
        }
        if (named != analysis_result::well_formed)
            return named;
        /* [class.derived]/2: complete each base before recording this class's
           first_base, since completing a base specialization appends its own
           entries to class_bases */
        require_complete(base);
        named_bases.append(base);
        named_ids.append(id);
    }
    entity_table[c].first_base = static_cast<uint32_t>(class_bases.size());
    entity_table[c].base_count = 0;
    for (size_t si = 0; si < specifiers.size(); si++) {
        SyntaxElement specifier = specifiers[si];
        uint8_t access = 0;
        bool is_virtual = false;
        if (key == standard_keyword::class_)
            access = 2;
        SyntaxElement name = specifier;
        if (specifier.is(syntax_kind::base_specifier)) {
            auto b = specifier;
            name = b[b.size() - 1];
            for (uint32_t k = 0; k + 1 < b.size(); k++) {
                if (!b[k].is_token())
                    /* an attribute-specifier-seq */
                    return not_supported_because(
                        specifier.index(), "class.derived",
                        "a base-specifier with a part before the name that is "
                        "no keyword (such as an attribute)");
                switch (b[k].keyword()) {
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
                    is_virtual = true;
                    break;
                default:
                    return not_supported_because(
                        specifier.index(), "class.derived",
                        "a base-specifier with a keyword other than an access "
                        "specifier or virtual");
                }
            }
        }
        token_index id = named_ids[si];
        entity_index base = named_bases[si];
        const entity &bx = entity_table[base];
        /* (a class whose definition the analysis could not follow: not
           known to be incomplete) */
        if (!bx.size && bx.kind != entity_kind::class_type)
            return not_supported_because(
                specifier.index(), "class.derived",
                "a base that names no class type and has no size");
        if (!bx.size)
            return ill_formed_at_token(id,
                                       "the class-or-decltype shall denote a "
                                       "class type that is not an "
                                       "incompletely defined class",
                                       "class.derived");
        if (bx.final_)
            return ill_formed_at_token(id,
                                       "a class marked final shall not be "
                                       "specified as a direct base class",
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
        entity_table[e].dependent_base = dependent_base_specifier(c, name);
        entity_table[e].defined = true;
        entity_table[e].access = access;
        entity_table[e].virtual_base = is_virtual;
        entity_table.add_declaration(e, current_declaration);
        if (is_virtual) {
            /* [class.mi]/4: no member of the class; its offset is in the
               complete object's layout */
            entity_table[e].field = UINT32_MAX;
        } else if (empty_class(base)) {
            /* [class.derived]/4 (Note): "A base class subobject may be of
               zero size": an empty class's ([abi:definitions]; a dynamic class
               has its virtual table pointer) */
            entity_table[e].field = UINT32_MAX;
        } else {
            entity_table[e].field = entity_table[c].member_count;
            class_members.append(e);
            entity_table[c].member_count++;
        }
        class_bases.append(e);
        entity_table[c].base_count++;
    }
    /* [class.base.init]/13.1: the virtual base classes "in the order they
       appear on a depth-first left-to-right traversal of the directed
       acyclic graph of base classes": each base's own virtual bases, then
       the base if it is virtual, each class once */
    owned_array<entity_index> order;
    owned_array<entity_index> direct;
    const entity &x = entity_table[c];
    for (uint32_t i = 0; i < x.base_count; i++) {
        entity_index b = class_bases[x.first_base + i];
        entity_index of = type_table[entity_table[b].type.unqualified()].entity;
        const entity &bx = entity_table[of];
        for (uint32_t k = 0; k < bx.virtual_base_count; k++) {
            entity_index v = class_virtual_bases[bx.first_virtual_base + k];
            entity_index vc =
                type_table[entity_table[v].type.unqualified()].entity;
            bool seen = false;
            for (entity_index o : order)
                seen = seen || o == vc;
            if (!seen) {
                order.append(vc);
                direct.append(0);
            }
        }
        if (!entity_table[b].virtual_base)
            continue;
        bool seen = false;
        for (size_t k = 0; k < order.size(); k++)
            if (order[k] == of) {
                seen = true;
                direct[k] = b;
            }
        if (!seen) {
            order.append(of);
            direct.append(b);
        }
    }
    uint32_t first = static_cast<uint32_t>(class_virtual_bases.size());
    for (size_t k = 0; k < order.size(); k++) {
        entity_index v = direct[k];
        if (!v) {
            /* an indirect virtual base: a subobject entity of its own */
            v = entity_table.make(entity_kind::variable, 0, c);
            entity_table[v].type = entity_table[order[k]].type;
            entity_table[v].nonstatic_member = true;
            entity_table[v].base_subobject = true;
            entity_table[v].virtual_base = true;
            entity_table[v].defined = true;
            entity_table[v].field = UINT32_MAX;
        }
        class_virtual_bases.append(v);
    }
    entity_table[c].first_virtual_base = first;
    entity_table[c].virtual_base_count = static_cast<uint32_t>(order.size());
    return analysis_result::well_formed;
}

entity_index semantic_analysis::virtual_base_of(entity_index c,
                                                entity_index v) const
{
    const entity &x = entity_table[c];
    for (uint32_t i = 0; i < x.virtual_base_count; i++) {
        entity_index b = class_virtual_bases[x.first_virtual_base + i];
        if (type_table[entity_table[b].type.unqualified()].entity == v)
            return b;
    }
    return 0;
}

/* [abi:class-types] III "in inheritance graph order": each direct base in
   declaration order, a virtual one before its own virtual bases */
void semantic_analysis::virtual_bases_in_graph_order(
    entity_index c, owned_array<entity_index> &out) const
{
    const entity &x = entity_table[c];
    for (uint32_t i = 0; i < x.base_count; i++) {
        entity_index b = class_bases[x.first_base + i];
        entity_index of = type_table[entity_table[b].type.unqualified()].entity;
        if (entity_table[b].virtual_base) {
            bool seen = false;
            for (entity_index o : out)
                seen = seen || o == of;
            if (!seen)
                out.append(of);
        }
        if (entity_table[of].virtual_base_count)
            virtual_bases_in_graph_order(of, out);
    }
}

uint32_t semantic_analysis::nonvirtual_subobjects(entity_index c,
                                                  entity_index base) const
{
    const entity &x = entity_table[c];
    uint32_t count = 0;
    for (uint32_t i = 0; i < x.base_count; i++) {
        entity_index b = class_bases[x.first_base + i];
        if (entity_table[b].virtual_base)
            continue;
        entity_index of = type_table[entity_table[b].type.unqualified()].entity;
        if (of == base)
            count++;
        count += nonvirtual_subobjects(of, base);
    }
    return count;
}

uint32_t semantic_analysis::base_subobjects(entity_index c,
                                            entity_index base) const
{
    uint32_t count = nonvirtual_subobjects(c, base);
    const entity &x = entity_table[c];
    for (uint32_t i = 0; i < x.virtual_base_count; i++) {
        entity_index v =
            type_table
                [entity_table[class_virtual_bases[x.first_virtual_base + i]]
                     .type.unqualified()]
                    .entity;
        if (v == base)
            count++;
        count += nonvirtual_subobjects(v, base);
    }
    return count;
}

/*
 * [temp.dep]/3: "In the definition of a class or class template, the scope
 * of a dependent base class is not examined during unqualified name lookup
 * either at the point of definition of the class template or member or
 * during an instantiation of the class template or member."  Analysis
 * looks up names in a specialization's class, made from the template's
 * class-specifier; the specialization's base subobjects whose
 * base-specifier depends on a template-parameter are marked
 * (entity::dependent_base).  Class c's scope is passed over when the name
 * is found in such a base and in nothing else of c's; unknown when a
 * dependent base and c itself or another base may both have it.
 */
bool semantic_analysis::dependent_base_specifier(entity_index c,
                                                 SyntaxElement name) const
{
    if (!entity_table[c].instantiation)
        return false;
    token_index first = name.token(), end = name.token() + 1;
    if (!name.is_token()) {
        first = name.first_token();
        end = name.end_token();
    }
    for (token_index t = first; t < end; t++) {
        if (tokens[t].kind != token_kind::identifier)
            continue;
        /* (a name after :: is a member's, not the parameter's) */
        if (t > first &&
            tokens[t - 1].kind == token_kind::operator_or_punctuator &&
            preprocessing_op_or_punc_primary(tokens[t - 1].op_or_punc) ==
                preprocessing_op_or_punc::COLON_COLON)
            continue;
        /* the class's own names before its members: the
           injected-class-name and the template-parameters' */
        entity_index e = entity_table.find(c, tokens.identifier(t));
        if (e && e != c)
            return true;
    }
    return false;
}

bool semantic_analysis::in_dependent_base_only(entity_index c, uint32_t name,
                                               bool &unknown) const
{
    unknown = false;
    const entity x = entity_table[c];
    bool dependent = false;
    for (uint32_t k = 0; k < x.base_count; k++)
        if (entity_table[class_bases[x.first_base + k]].dependent_base)
            dependent = true;
    if (!dependent || entity_table.find(c, name))
        return false;
    bool in_dependent = false, in_other = false;
    for (uint32_t k = 0; k < x.base_count; k++) {
        const entity &b = entity_table[class_bases[x.first_base + k]];
        entity_index of = type_table[b.type.unqualified()].entity;
        bool ambiguous = false;
        entity_index e = class_member_lookup(of, name, ambiguous);
        if (!e && !ambiguous)
            continue;
        if (b.dependent_base)
            in_dependent = true;
        else
            in_other = true;
    }
    if (in_dependent && in_other) {
        unknown = true;
        return false;
    }
    return in_dependent;
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
    for (uint32_t i = 0; i < x.base_count && !found; i++) {
        entity_index b = class_bases[x.first_base + i];
        entity_index of = type_table[entity_table[b].type.unqualified()].entity;
        if (of == base || base_subobjects(of, base))
            found = b;
    }
    if (found && base_subobjects(c, base) > 1)
        ambiguous = true;
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
    require_complete(c);
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
 * private or protected one in the derived class's own members and its
 * friends ([class.access.base]/5.2; further derived classes are not
 * analysed).
 */
analysis_result semantic_analysis::to_base(token_index at, entity_index base,
                                           expression_meaning &m,
                                           bool check_access)
{
    for (;;) {
        entity_index c = type_table[m.type.unqualified()].entity;
        if (c == base)
            return analysis_result::well_formed;
        bool ambiguous = false;
        entity_index step = base_step(c, base, ambiguous);
        if (!step)
            return not_supported_because(
                current_declaration, "class.derived",
                "a conversion to a class for which no base step is found");
        if (ambiguous)
            return not_supported_because(
                current_declaration, "class.member.lookup",
                "a conversion to a base reached ambiguously");
        if (!nonvirtual_subobjects(c, base)) {
            /* through a virtual base: the virtual base base is, or the one
               with base among its non-virtual bases */
            entity_index v = virtual_route(c, base);
            if (!v)
                return not_supported_because(current_declaration, "class.mi",
                                             "a conversion to a base with no "
                                             "route through a virtual base");
            VTRY(r, to_virtual_base(at, c, v, m));
            continue;
        }
        step = nonvirtual_step(c, base);
        if (check_access && entity_table[step].access && current_class() != c &&
            !in_friend_of(c)) {
            if (current_class())
                return not_supported_because(current_declaration,
                                             "class.access.base",
                                             "a conversion in a member of some "
                                             "class to a non-public base");
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
        } else if (m.category == value_category::prvalue &&
                   (aggregate_prvalue(m) || entity_table[c].destructor)) {
            /* [basic.lval]/7: a prvalue's temporary ([conv.rval]), whose
               address the lowering takes as a reference binding's; one
               destroyed at the end of the full-expression
               ([class.temporary]/4) or an aggregate's temporary is not
               analysed yet (as .*'s first operand) */
            return not_supported_because(
                current_declaration, "class.temporary",
                "a conversion of such a prvalue to an empty base");
        } else {
            /* an empty base, at the object's address: *(B *)&object (an
               xvalue's object, or a prvalue's temporary materialized by
               [conv.rval]) */
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
 * `from`, to a pointer to its base `to` (to's cv-qualified type in type).
 */
analysis_result semantic_analysis::pointer_to_base(token_index at,
                                                   entity_index from,
                                                   entity_index to,
                                                   qualified_type type,
                                                   expression_meaning &m)
{
    bool ambiguous = false;
    if (!base_step(from, to, ambiguous))
        return not_supported_because(
            current_declaration, "class.derived",
            "a pointer conversion to a class for which no base step is found");
    if (ambiguous)
        return not_supported_because(
            current_declaration, "class.member.lookup",
            "a pointer conversion to a base reached ambiguously");
    if (!nonvirtual_subobjects(from, to)) {
        /* [conv.ptr]/3 through a virtual base: its offset is the object's
           ([abi:vtable-components] vbase offset), the rest of the way
           constant offsets */
        entity_index v = virtual_route(from, to);
        if (!v)
            return not_supported_because(current_declaration, "conv.ptr",
                                         "a pointer conversion to a base with "
                                         "no route through a virtual base");
        if (!virtual_base_accessible(from, v)) {
            if (current_class())
                return not_supported_because(
                    current_declaration, "class.access.base",
                    "a pointer conversion in a member of some class to an "
                    "inaccessible virtual base");
            return ill_formed_at_token(at,
                                       "if B is an inaccessible base class of "
                                       "D, a program that necessitates this "
                                       "conversion is ill-formed",
                                       "conv.ptr");
        }
        if (v == to) {
            m.type = type;
            converted(m, conversion_kind::derived_to_base);
            if (m.node)
                semantics[m.node].b = virtual_base_of(from, v);
            return analysis_result::well_formed;
        }
        /* a base inside the virtual base: to the virtual base first, then
           the non-virtual path from it */
        m.type = qualified_type::make(
            type_table.pointer_to(qualified_type::make(
                entity_table[v].type.unqualified(),
                type_table[type.unqualified()].operand.cv())),
            cv_none);
        converted(m, conversion_kind::derived_to_base);
        if (m.node)
            semantics[m.node].b = virtual_base_of(from, v);
        return pointer_to_base(at, v, to, type, m);
    }
    /* [conv.ptr]/3: "The result of the conversion is a pointer to the base
       class subobject of the derived class object.  The null pointer value
       is converted to the null pointer value of the destination type" */
    return pointer_along_path(at, from, to, type, false, true, m);
}

/* [conv.ptr]/3: "If B is an inaccessible or ambiguous base class of D, a
   program that necessitates this conversion is ill-formed": the direct
   non-virtual base step of class c */
analysis_result semantic_analysis::base_pointer_access(token_index at,
                                                       entity_index c,
                                                       entity_index step)
{
    if (!entity_table[step].access || current_class() == c || in_friend_of(c))
        return analysis_result::well_formed;
    if (current_class())
        return not_supported_because(current_declaration, "class.access.base",
                                     "a pointer conversion in a member of some "
                                     "class to a non-public direct base");
    return ill_formed_at_token(at,
                               "if B is an inaccessible base class of D, a "
                               "program that necessitates this conversion is "
                               "ill-formed",
                               "conv.ptr");
}

/*
 * [expr.static.cast]/11: "A prvalue of type "pointer to cv1 B", where B is
 * a class type, can be converted to a prvalue of type "pointer to cv2 D",
 * where D is a complete class derived from B, if cv2 is the same
 * cv-qualification as, or greater cv-qualification than, cv1.  If B is a
 * virtual base class of D or a base class of a virtual base class of D, or
 * if no valid standard conversion from "pointer to D" to "pointer to B"
 * exists, the program is ill-formed.  The null pointer value is converted
 * to the null pointer value of the destination type."  m, a pointer to
 * class from (B), to a pointer to its derived class to (D), of type type
 * (the caller checks the cv-qualifiers).
 */
analysis_result semantic_analysis::pointer_to_derived(token_index at,
                                                      entity_index from,
                                                      entity_index to,
                                                      qualified_type type,
                                                      expression_meaning &m)
{
    /* "or if no valid standard conversion from "pointer to D" to "pointer
       to B" exists" ([conv.ptr]/3: "If B is an inaccessible or ambiguous
       base class of D, a program that necessitates this conversion is
       ill-formed") */
    if (base_subobjects(to, from) != 1)
        return ill_formed_at_token(at,
                                   "if no valid standard conversion from "
                                   "\"pointer to D\" to \"pointer to B\" "
                                   "exists, the program is ill-formed",
                                   "expr.static.cast");
    if (!nonvirtual_subobjects(to, from))
        return ill_formed_at_token(at,
                                   "if B is a virtual base class of D or a "
                                   "base class of a virtual base class of D, "
                                   "the program is ill-formed",
                                   "expr.static.cast");
    return pointer_along_path(at, to, from, type, true, true, m);
}

/*
 * The pointer m between class derived and its base on their one
 * non-virtual path, to a pointer of type type: to the base
 * ([conv.ptr]/3), or to the derived class if to_derived
 * ([expr.static.cast]/11), each base on the way accessible if access
 * ([expr.cast]/4: "the conversion is valid even if the base class is
 * inaccessible").  One conversion per base at a nonzero offset, the
 * lowering's addition or subtraction of that offset after a null check;
 * the bases at offset 0 on either side of it (the object's address) are
 * in the same conversion, and with none at a nonzero offset, one
 * conversion names the first base.
 */
analysis_result semantic_analysis::pointer_along_path(
    token_index at, entity_index derived, entity_index base,
    qualified_type type, bool to_derived, bool access, expression_meaning &m)
{
    owned_array<entity_index> steps;
    VTRY(r, accessible_path(at, derived, base, access, "conv.ptr", steps));
    /* the steps at a nonzero offset in the order the pointer passes
       them */
    owned_array<size_t> shifts;
    for (size_t k = 0; k < steps.size(); k++) {
        size_t i = k;
        if (to_derived)
            i = steps.size() - 1 - k;
        if (entity_table[steps[i]].offset)
            shifts.append(i);
    }
    conversion_kind kind = conversion_kind::derived_to_base;
    if (to_derived)
        kind = conversion_kind::base_to_derived;
    uint32_t cv = type_table[type.unqualified()].operand.cv();
    if (shifts.empty()) {
        m.type = type;
        if (to_derived && m.evaluation == constness::constant)
            m.evaluation = constness::unknown;
        converted(m, kind);
        if (m.node)
            semantics[m.node].b = steps[0];
        return analysis_result::well_formed;
    }
    for (size_t k = 0; k < shifts.size(); k++) {
        m.type = type;
        if (k + 1 < shifts.size()) {
            /* the class the pointer reaches before the next such step:
               that step's derived class going up, its base going down */
            entity_index next = steps[shifts[k + 1]];
            entity_index reached = entity_table[next].enclosing;
            if (to_derived)
                reached =
                    type_table[entity_table[next].type.unqualified()].entity;
            m.type = qualified_type::make(
                type_table.pointer_to(qualified_type::make(
                    entity_table[reached].type.unqualified(), cv)),
                cv_none);
        }
        if (to_derived && m.evaluation == constness::constant)
            m.evaluation = constness::unknown;
        converted(m, kind);
        if (m.node)
            semantics[m.node].b = steps[shifts[k]];
    }
    return analysis_result::well_formed;
}

/* the non-virtual path from class derived to its base (unsupported, as
   subclause label, where there is none), each base on it accessible
   ([conv.ptr]/3, [conv.mem]/2) if access */
analysis_result semantic_analysis::accessible_path(
    token_index at, entity_index derived, entity_index base, bool access,
    const char *label, owned_array<entity_index> &steps)
{
    if (!nonvirtual_path(derived, base, steps))
        return not_supported_because(
            current_declaration, label,
            "a path to a base for which no non-virtual route is found");
    for (size_t i = 0; access && i < steps.size(); i++) {
        VTRY(r, base_pointer_access(at, entity_table[steps[i]].enclosing,
                                    steps[i]));
    }
    return analysis_result::well_formed;
}

/* the direct non-virtual bases on the way from class c to its base, each
   the base of the class before it, outermost first; false if base is not
   a non-virtual base of c */
bool semantic_analysis::nonvirtual_path(entity_index c, entity_index base,
                                        owned_array<entity_index> &steps) const
{
    while (c != base) {
        entity_index step = nonvirtual_step(c, base);
        if (!step)
            return false;
        steps.append(step);
        c = type_table[entity_table[step].type.unqualified()].entity;
    }
    return true;
}

/* [expr.cast]/4: one path from class c to its base, every base on it
   non-virtual */
bool semantic_analysis::single_nonvirtual_path(entity_index c,
                                               entity_index base) const
{
    owned_array<entity_index> steps;
    return base_subobjects(c, base) == 1 && nonvirtual_path(c, base, steps);
}

/* the direct non-virtual base of c through which a non-virtual path leads
   to base */
entity_index semantic_analysis::nonvirtual_step(entity_index c,
                                                entity_index base) const
{
    const entity &x = entity_table[c];
    for (uint32_t i = 0; i < x.base_count; i++) {
        entity_index b = class_bases[x.first_base + i];
        if (entity_table[b].virtual_base)
            continue;
        entity_index of = type_table[entity_table[b].type.unqualified()].entity;
        if (of == base || nonvirtual_subobjects(of, base))
            return b;
    }
    return 0;
}

/* the virtual base of c that is base or has base among its non-virtual
   bases (base reached through no non-virtual path), 0 if none */
entity_index semantic_analysis::virtual_route(entity_index c,
                                              entity_index base) const
{
    const entity &x = entity_table[c];
    for (uint32_t i = 0; i < x.virtual_base_count; i++) {
        entity_index v =
            type_table
                [entity_table[class_virtual_bases[x.first_virtual_base + i]]
                     .type.unqualified()]
                    .entity;
        if (v == base || nonvirtual_subobjects(v, base))
            return v;
    }
    return 0;
}

/* [class.access.base]/4 for virtual base v of c: some path to it public
   at each step (the first step any where c's members or friends name
   it) */
bool semantic_analysis::virtual_base_accessible(entity_index c,
                                                entity_index v) const
{
    bool inside = current_class() == c || in_friend_of(c);
    const entity &x = entity_table[c];
    for (uint32_t i = 0; i < x.base_count; i++) {
        entity_index b = class_bases[x.first_base + i];
        if (entity_table[b].access && !inside)
            continue;
        entity_index of = type_table[entity_table[b].type.unqualified()].entity;
        if (of == v && entity_table[b].virtual_base)
            return true;
        if (of != v && virtual_base_of(of, v) && public_path_to(of, v))
            return true;
    }
    return false;
}

/* whether a path of public bases leads from c to its virtual base v */
bool semantic_analysis::public_path_to(entity_index c, entity_index v) const
{
    const entity &x = entity_table[c];
    for (uint32_t i = 0; i < x.base_count; i++) {
        entity_index b = class_bases[x.first_base + i];
        if (entity_table[b].access)
            continue;
        entity_index of = type_table[entity_table[b].type.unqualified()].entity;
        if (of == v && entity_table[b].virtual_base)
            return true;
        if (of != v && virtual_base_of(of, v) && public_path_to(of, v))
            return true;
    }
    return false;
}

/*
 * The glvalue m of class c as its virtual base class subobject of class v
 * ([class.mi]/4): *(V *)((char *)&m + vbase offset), the offset read from
 * the object's virtual table ([abi:vtable-components]: "Virtual Base
 * (vbase) offsets are used to access the virtual bases of an object"),
 * the lowering's.
 */
analysis_result semantic_analysis::to_virtual_base(token_index at,
                                                   entity_index c,
                                                   entity_index v,
                                                   expression_meaning &m)
{
    if (m.category != value_category::lvalue)
        return not_supported_because(
            current_declaration, "class.temporary",
            "a conversion of a non-lvalue to a virtual base");
    if (!virtual_base_accessible(c, v)) {
        if (current_class())
            return not_supported_because(
                current_declaration, "class.access.base",
                "a conversion in a member of some class to an inaccessible "
                "virtual base");
        return ill_formed_at_token(at,
                                   "a base class is accessible only if an "
                                   "invented public member of it is",
                                   "class.access.base");
    }
    expression_meaning result;
    result.type =
        qualified_type::make(entity_table[v].type.unqualified(), m.type.cv());
    result.category = value_category::lvalue;
    result.evaluation = constness::not_constant;
    expression_meaning address;
    address.type = qualified_type::make(type_table.pointer_to(m.type), cv_none);
    address.category = value_category::prvalue;
    address.evaluation = constness::not_constant;
    address.node = expression_node(semantic_kind::address, address, at, m.node);
    address.type =
        qualified_type::make(type_table.pointer_to(result.type), cv_none);
    converted(address, conversion_kind::derived_to_base);
    semantics[address.node].b = virtual_base_of(c, v);
    result.node =
        expression_node(semantic_kind::indirection, result, at, address.node);
    m = result;
    return analysis_result::well_formed;
}

} // namespace cplusplus
