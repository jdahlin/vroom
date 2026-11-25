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
    if (!any)
        return analysis_result::well_formed;
    /* [temp.spec]/6: a default argument is checked */
    flag_set checked(explicit_declaration_names, false);
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
    /* (a copy of the entity: analysing an argument, a lambda-expression's
       for one, makes entities, which moves the table) */
    const entity x = entity_table[f];
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
        /* /8: "The keyword this shall not be used in a default argument of
           a member function" */
        for (token_index t = first; t < end && member; t++)
            if (tokens[t].kind == token_kind::keyword &&
                this->p.keyword_of(tokens[t]) == standard_keyword::this_)
                return ill_formed(p.declaration,
                                  "the keyword this shall not be used in a "
                                  "default argument of a member function",
                                  "dcl.fct.default");
        /* /9: "A non-static member shall not appear in a default argument
           unless it appears as the id-expression of a class member access
           expression or unless it is used to form a pointer to member":
           a name that may be one is not analysed, except in an unevaluated
           operand ([expr.prim.id]/3: sizeof(m) is no use of the member) */
        auto unevaluated_operand = [&](token_index k) {
            if (tokens[k].kind != token_kind::keyword)
                return false;
            standard_keyword kw = this->p.keyword_of(tokens[k]);
            return kw == standard_keyword::sizeof_ ||
                   kw == standard_keyword::alignof_ ||
                   kw == standard_keyword::decltype_ ||
                   kw == standard_keyword::noexcept_;
        };
        int depth = 0;
        int unevaluated_depth = -1;
        for (token_index t = first; t < end && member; t++) {
            if (is_operator(tokens[t],
                            preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
                if (unevaluated_depth < 0 && t > first &&
                    unevaluated_operand(t - 1))
                    unevaluated_depth = depth;
                depth++;
                continue;
            }
            if (is_operator(tokens[t],
                            preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
                depth--;
                if (depth == unevaluated_depth)
                    unevaluated_depth = -1;
                continue;
            }
            if (tokens[t].kind != token_kind::identifier ||
                unevaluated_depth >= 0)
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
        qualified_type type =
            type_table.parameters(entity_table[f].type.unqualified())[i];
        initialized_value value;
        /* [ext:clang_source_location] the call's place is the value of a
           builtin that names it */
        uint32_t site_nodes = call_site_nodes;
        struct call_site_scope {
            unsigned &depth;
            ~call_site_scope()
            {
                depth--;
            }
        } at_call = {++call_site_depth};
        entity_index outer_function = default_argument_function;
        default_argument_function = f;
        scoped_value<unsigned> recording(default_argument_unevaluated,
                                         unevaluated);
        size_t needs_before = default_argument_needed.size();
        analysis_result r = initialize_from(p.declaration, p.default_argument,
                                            type, false, false, value);
        default_argument_function = outer_function;
        if (r != analysis_result::well_formed)
            return r;
        /* an argument that made no node (__builtin_source_location(), whose
           object is not made yet) is not a default argument a call may
           use: unknown, not absent ([over.match.viable]/2) */
        if (!value.node) {
            entity_table[f].unknown_defaults = true;
            return analysis_result::well_formed;
        }
        nodes[i] = wrap_call_site(value.node, first, site_nodes);
        if (default_argument_needed.size() != needs_before) {
            default_argument_need_range needs;
            needs.node = nodes[i];
            needs.first = static_cast<uint32_t>(needs_before);
            needs.count = static_cast<uint32_t>(default_argument_needed.size() -
                                                needs_before);
            default_argument_need_ranges.append(needs);
        }
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
    entity_table[f].first_default =
        static_cast<uint32_t>(default_argument_nodes.size());
    for (size_t i = 0; i < count; i++)
        default_argument_nodes.append(nodes[i]);
    entity_table[f].default_arguments = trailing;
    return analysis_result::well_formed;
}

analysis_result semantic_analysis::member_default_arguments(entity_index c)
{
    /* [temp.spec]/6: a default argument is checked */
    flag_set checked(explicit_declaration_names, false);
    analysis_result result = analysis_result::well_formed;
    owned_array<pending_default_arguments> kept;
    entity_index scope = current_scope;
    /* [class.mem]/6: "A complete-class context of a nested class is also a
       complete-class context of any enclosing class": the default
       arguments of a class nested in a class being defined wait for the
       enclosing class's closing } */
    if (defining_class && defining_class != c &&
        entity_table[defining_class].kind == entity_kind::class_type &&
        entity_table[c].enclosing == defining_class)
        return analysis_result::well_formed;
    for (const pending_default_arguments &p : pending_defaults) {
        entity_index owner = entity_table[p.function].enclosing;
        while (owner != c && owner &&
               entity_table[owner].kind == entity_kind::class_type &&
               entity_table[owner].enclosing)
            owner = entity_table[owner].enclosing;
        if (owner != c) {
            kept.append(p);
            continue;
        }
        owner = entity_table[p.function].enclosing;
        function_parameters.clear();
        for (uint32_t i = 0; i < p.count; i++)
            function_parameters.append(member_default_parameters[p.first + i]);
        current_scope = owner;
        analysing_member_defaults = true;
        analysis_result r;
        if (entity_table[owner].instantiation) {
            /* [temp.inst]/3: a default argument of a member of an implicit
               instantiation is instantiated only when a call uses it, and
               one that is ill-formed for these arguments is no error of the
               class: analysed quietly, and one that fails is left unknown
               (a call that needs it is not supported) */
            size_t unsupported_before = unsupported.size();
            {
                diagnostics_suppressed hush(diagnostics);
                r = default_arguments(p.declaration, p.function,
                                      entity_kind::function);
            }
            if (r != analysis_result::well_formed) {
                unsupported.resize(unsupported_before);
                r = analysis_result::well_formed;
                entity_table[p.function].unknown_defaults = true;
            }
        } else {
            r = default_arguments(p.declaration, p.function,
                                  entity_kind::function);
            if (r != analysis_result::well_formed)
                entity_table[p.function].unknown_defaults = true;
        }
        analysing_member_defaults = false;
        current_scope = scope;
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
