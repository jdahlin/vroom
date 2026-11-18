/*
 * temp.decls.cpp: the semantic side of [temp.pre], [temp.expl.spec],
 * [temp.explicit] and [temp.deduct.guide]: the declarations of templates
 * and of their specializations.  (The grammar is the parser's, in
 * temp.pre.cpp and temp.concept.cpp.)
 *
 * The skeleton of S6b: each declaration answers the subclause whose rules
 * it waits for, by what it declares, so that cxx-sema.py's list of what is
 * unsupported says which kind of template blocks most.  hold_names (called
 * for any declaration that is not well-formed) keeps the names they
 * declare, marked as a template's ([temp.names]).
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/*
 * [temp.pre]/1: "A template defines a family of classes, functions, or
 * variables, an alias for a family of types, or a concept."
 */
analysis_result semantic_analysis::template_declaration(node_index n)
{
    const syntax_node &node = tree[n];
    if (!node.child_count || tree.children(n)[node.child_count - 1].is_token())
        return not_supported(n, "temp.pre");
    node_index inner = tree.children(n)[node.child_count - 1].index();
    switch (tree[inner].kind) {
    /* [temp.mem]/1: "A member template can be defined within or outside
       its class definition or class template definition" (template<...>
       template<...> ...) */
    case syntax_kind::template_declaration:
        return not_supported(n, "temp.mem");
    /* [temp.concept]/1: "A concept is a template that defines constraints
       on its template arguments." */
    case syntax_kind::concept_definition:
        return not_supported(n, "temp.concept");
    /* [temp.alias]/1: "A template-declaration in which the declaration is
       an alias-declaration declares the identifier to be an alias
       template." */
    case syntax_kind::alias_declaration:
        return not_supported(n, "temp.alias");
    /* [temp.fct]/1: "A function template defines an unbounded set of
       related functions." */
    case syntax_kind::function_definition:
        return not_supported(n, "temp.fct");
    case syntax_kind::simple_declaration: {
        /* [temp.class]/1: "A class template defines the layout and
           operations for an unbounded set of related types." (a
           declaration with no declarator: a class's) */
        bool declarators = false;
        for (uint32_t i = 0; i < tree[inner].child_count; i++)
            if (syntax_is(tree, tree.children(inner)[i],
                          syntax_kind::init_declarator_list))
                declarators = true;
        if (!declarators)
            return not_supported(n, "temp.class");
        /* a function template's declaration or a variable template: which
           needs the declarator's type */
        return not_supported(n, "temp.pre");
    }
    default:
        return not_supported(n, "temp.pre");
    }
}

/* [temp.expl.spec]/1: "An explicit specialization of any of the following
   ... can be declared by a declaration introduced by template<>" */
analysis_result semantic_analysis::explicit_specialization(node_index n)
{
    return not_supported(n, "temp.expl.spec");
}

/* [temp.explicit]/1: "A class, function, variable, or member template
   specialization can be explicitly instantiated from its template." */
analysis_result semantic_analysis::explicit_instantiation(node_index n)
{
    return not_supported(n, "temp.explicit");
}

/* [temp.deduct.guide]/1: "Deduction guides are used when a template-name
   appears as a type specifier for a deduced class type" */
analysis_result semantic_analysis::deduction_guide(node_index n)
{
    return not_supported(n, "temp.deduct.guide");
}

/*
 * [temp.deduct]/1: "When a function template specialization is referenced,
 * all of the template arguments shall have values.  The values can be
 * explicitly specified or, in some cases, be deduced from the use or
 * obtained from default template-arguments."  [temp.deduct.call]: from a
 * call's arguments.  No rules yet.
 */
analysis_result
semantic_analysis::deduce_from_call(node_index at, entity_index,
                                    const expression_meaning *, uint32_t,
                                    owned_array<template_argument> &)
{
    return not_supported(at, "temp.deduct.call");
}

} // namespace cplusplus
