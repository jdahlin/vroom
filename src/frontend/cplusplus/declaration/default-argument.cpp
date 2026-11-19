/*
 * default-argument.cpp: [dcl.fct.default] Default arguments.
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
 * the argument of every call that omits it (postfix-expression.cpp's call).
 * The default arguments of a function of namespace scope are analysed; a
 * member function's are a complete-class context ([class.mem]/6), analysed
 * when its class is complete (member_default_arguments); a block-scope
 * declaration's are its scope's alone (/4): not analysed.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

analysis_result semantic_analysis::default_arguments(SyntaxRef n,
                                                     entity_index f,
                                                     entity_kind kind,
                                                     bool specialization)
{
    if (kind == entity_kind::function && f && !specialization &&
        entity_table[f].enclosing != current_scope)
        /* /4: a declaration in another scope has its own set */
        entity_table[f].unknown_defaults = true;
    bool any = false;
    for (const parameter_meaning &p : function_parameters)
        if (p.default_argument)
            any = true;
    if (any && entity_table[current_scope].kind == entity_kind::class_type &&
        entity_table[current_scope].instantiation)
        /* [temp.inst]/3: a member of a class template's specialization
           has its default arguments instantiated when a call uses them */
        return not_supported(n, "temp.inst");
    if (!any)
        return analysis_result::well_formed;
    /* [temp.inst]/4: a function template specialization a default
       argument names is not "referenced in a context that requires a
       function definition to exist" until the argument is used in a
       call (clang's choice: the rule is [temp.inst]/11's "required") */
    struct unevaluated_scope {
        unsigned &depth;
        ~unevaluated_scope()
        {
            depth--;
        }
    } in_default = {++unevaluated};
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
    bool member = analysing_member_defaults &&
                  entity_table[x.enclosing].kind == entity_kind::class_type;
    /* ([temp.inst]/12: a specialization's are its template's, in the
       instantiation's scope wherever the call that names it is) */
    if (x.unknown_defaults ||
        (!specialization &&
         ((!is_namespace(x.enclosing) && !member) ||
          (at_block_scope() && !member) || function_state.active)))
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
        if (!p.default_argument)
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
        SyntaxRef clause = node_of(p.default_argument);
        token_index first = clause.first_token();
        token_index end = clause.end_token();
        if (p.default_argument.is_token()) {
            first = p.default_argument.token();
            end = first + 1;
        }
        for (token_index t = first; t < end && member; t++) {
            /* /9: "A non-static member shall not appear in a default
               argument unless it appears as the id-expression of a class
               member access expression or unless it is used to form a
               pointer to member"; /8: "The keyword this shall not be
               used in a default argument of a member function": a name
               that may be one is not analysed */
            if (tokens[t].kind == token_kind::keyword)
                if (this->p.keyword_of(tokens[t]) == standard_keyword::this_)
                    return not_supported(p.declaration, "dcl.fct.default");
            if (tokens[t].kind != token_kind::identifier)
                continue;
            lookup_result found = lookup_qualified(
                x.enclosing, tokens.identifier(t), lookup_filter::any);
            if (found.unknown || found.ambiguous ||
                (found.entity && entity_table[found.entity].nonstatic_member))
                return not_supported(p.declaration, "dcl.fct.default");
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

analysis_result semantic_analysis::member_default_arguments(entity_index c)
{
    analysis_result result = analysis_result::well_formed;
    owned_array<pending_default_arguments> kept;
    entity_index scope = current_scope;
    for (const pending_default_arguments &p : pending_defaults) {
        if (entity_table[p.function].enclosing != c) {
            kept.append(p);
            continue;
        }
        function_parameters.clear();
        for (uint32_t i = 0; i < p.count; i++)
            function_parameters.append(member_default_parameters[p.first + i]);
        current_scope = c;
        analysing_member_defaults = true;
        analysis_result r =
            default_arguments(p.declaration, p.function, entity_kind::function);
        analysing_member_defaults = false;
        current_scope = scope;
        if (r != analysis_result::well_formed)
            entity_table[p.function].unknown_defaults = true;
        result = worse(result, r);
    }
    pending_defaults.clear();
    for (const pending_default_arguments &p : kept)
        pending_defaults.append(p);
    if (pending_defaults.empty())
        member_default_parameters.clear();
    function_parameters.clear();
    return result;
}

void semantic_analysis::defer_default_arguments(
    entity_index f, SyntaxRef declaration,
    const owned_array<parameter_meaning> &list)
{
    pending_default_arguments p;
    p.function = f;
    p.declaration = declaration;
    p.first = static_cast<uint32_t>(member_default_parameters.size());
    p.count = static_cast<uint32_t>(list.size());
    for (const parameter_meaning &q : list)
        member_default_parameters.append(q);
    pending_defaults.append(p);
}

} // namespace cplusplus
