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

namespace cplusplus
{

node_index parser::base_clause(scope_index derived)
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* : */
    size_t list_mark = begin_node();
    token_index list_first = position;
    unsigned count = 0;
    bool ellipsis = false;
    for (;;) {
        base_specifier(derived);
        if (failed())
            return 0;
        count++;
        if (accept(preprocessing_op_or_punc::ELLIPSIS))
            ellipsis = true;
        if (!accept(preprocessing_op_or_punc::COMMA))
            break;
    }
    if (count > 1 || ellipsis)
        finish_node(syntax_kind::base_specifier_list, list_mark, list_first);
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

} // namespace cplusplus
