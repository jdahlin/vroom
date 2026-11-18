/*
 * dcl.fct.default.cpp: [dcl.fct.default] Default arguments.
 *
 * [dcl.fct.default]/1: "If an initializer-clause is specified in a
 * parameter-declaration this initializer-clause is used as a default
 * argument.  [Note 1: Default arguments will be used in calls where
 * trailing arguments are missing ([expr.call]).]"  /5: "The default
 * argument has the same semantic constraints as the initializer in a
 * declaration of a variable of the parameter type, using the
 * copy-initialization semantics ([dcl.init]).  The names in the default
 * argument are bound, and the semantic constraints are checked, at the
 * point where the default argument appears."  /9: "A default argument is
 * evaluated each time the function is called with no argument for the
 * corresponding parameter."
 *
 * So each is analysed once, where it appears, and its initialization is
 * the argument of every call that omits it (expr.post.cpp's call).  The
 * default arguments of a function of namespace scope are analysed; a
 * member function's are a complete-class context ([class.mem]/6) the
 * parser keeps as tokens, and a block-scope declaration's are its scope's
 * alone (/4): neither is analysed.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

analysis_result semantic_analysis::default_arguments(node_index n,
                                                     entity_index f,
                                                     entity_kind kind)
{
    if (kind == entity_kind::function && f &&
        entity_table[f].enclosing != current_scope)
        /* /4: a declaration in another scope has its own set */
        entity_table[f].unknown_defaults = true;
    bool any = false;
    for (const parameter_meaning &p : function_parameters)
        if (p.default_argument.index())
            any = true;
    if (!any)
        return analysis_result::well_formed;
    /* /3: "A default argument shall be specified only in the
       parameter-declaration-clause of a function declaration or
       lambda-declarator or in a template-parameter" */
    if (kind != entity_kind::function || !f)
        return ill_formed(n,
                          "a default argument shall be specified only in the "
                          "parameter-declaration-clause of a function "
                          "declaration",
                          "dcl.fct.default");
    entity &x = entity_table[f];
    if (x.unknown_defaults || !is_namespace(x.enclosing) || at_block_scope() ||
        function_state.active)
        return not_supported(n, "dcl.fct.default");
    const struct type &function = type_table[x.type.unqualified()];
    size_t count = function.parameter_count;
    if (function_parameters.size() != count)
        return not_supported(n, "dcl.fct.default");
    const qualified_type *types = type_table.parameters(x.type.unqualified());
    /* the default arguments the function has so far, and this
       declaration's */
    owned_array<uint32_t> nodes;
    nodes.resize(count);
    for (size_t i = 0; i < count; i++) {
        nodes[i] = 0;
        if (x.first_default)
            nodes[i] = default_argument_nodes[x.first_default + i];
    }
    for (size_t i = 0; i < count; i++) {
        const parameter_meaning &p = function_parameters[i];
        if (!p.default_argument.index())
            continue;
        /* /4 (Note 2): "A default argument cannot be redefined by a later
           declaration (not even to the same value)" */
        if (nodes[i])
            return ill_formed(p.declaration,
                              "a default argument cannot be redefined by a "
                              "later declaration, not even to the same value",
                              "dcl.fct.default");
        /* /9: "Parameters of a function declared before a default argument
           are in scope and can hide namespace and class member names": not
           so here, so a default argument that may name one is not
           analysed */
        node_index clause = node_of(p.default_argument);
        token_index first = tree[clause].first_token;
        token_index end = tree[clause].end_token;
        if (p.default_argument.is_token()) {
            first = p.default_argument.index();
            end = first + 1;
        }
        for (token_index t = first; t < end; t++)
            for (const parameter_meaning &q : function_parameters)
                if (q.id && tokens[t].kind == token_kind::identifier &&
                    tokens.identifier(t) == tokens.identifier(q.id))
                    return not_supported(p.declaration, "dcl.fct.default");
        qualified_type type = types[i];
        initialized_value value;
        analysis_result r = initialize_from(p.declaration, p.default_argument,
                                            type, false, false, value);
        if (r != analysis_result::well_formed)
            return r;
        nodes[i] = value.node;
    }
    /* /4: "In a given function declaration, each parameter subsequent to a
       parameter with a default argument shall have a default argument
       supplied in this or a previous declaration" */
    uint32_t trailing = 0;
    for (size_t i = 0; i < count; i++) {
        if (nodes[i]) {
            trailing++;
            continue;
        }
        if (trailing)
            return ill_formed(function_parameters[i].declaration,
                              "each parameter subsequent to a parameter with "
                              "a default argument shall have a default "
                              "argument supplied in this or a previous "
                              "declaration",
                              "dcl.fct.default");
    }
    if (default_argument_nodes.empty())
        default_argument_nodes.append(0);
    x.first_default = static_cast<uint32_t>(default_argument_nodes.size());
    for (size_t i = 0; i < count; i++)
        default_argument_nodes.append(nodes[i]);
    x.default_arguments = trailing;
    return analysis_result::well_formed;
}

} // namespace cplusplus
