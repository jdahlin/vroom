/*
 * semantic_analysis.cpp: the analysis of a translation unit, declaration
 * by declaration, and the result for what has no rules yet
 * (doc/PLAN-cxx-sema.md).
 *
 * The rules themselves are in the file of their subclause, beside the
 * grammar: a function of semantic_analysis named as the nonterminal.
 * Here is only what calls them.  A nonterminal without such a function is
 * `unsupported` with the subclause that defines it; nothing is concluded
 * about its declaration, and no diagnostic is issued.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

const char *analysis_result_name(analysis_result result)
{
    switch (result) {
    case analysis_result::well_formed:
        return "well-formed";
    case analysis_result::ill_formed:
        return "ill-formed";
    case analysis_result::unsupported:
        return "unsupported";
    case analysis_result::not_parsed:
        return "not-parsed";
    }
    return "";
}

semantic_analysis::semantic_analysis(const syntax_tree &tree,
                                     token_stream &tokens, const parser &p,
                                     diagnostic_consumer &diagnostics)
    : tree(tree), tokens(tokens), p(p), diagnostics(diagnostics),
      syntax_cx{tree, tokens, p}
{
    unsupported.append(unsupported_construct());
}

analysis_result semantic_analysis::not_supported(node_index n, call_site where)
{
    return not_supported(n, syntax_kind_subclause(tree[n].kind), where);
}

analysis_result semantic_analysis::not_supported(node_index n,
                                                 const char *subclause,
                                                 call_site where)
{
    unsupported_construct u;
    u.node = n;
    u.subclause = subclause;
    u.file = where.file;
    u.line = where.line;
    u.function = where.function;
    unsupported.append(u);
    return analysis_result::unsupported;
}

analysis_result semantic_analysis::ill_formed(node_index at, const char *rule,
                                              const char *subclause)
{
    return ill_formed_at_token(tree[at].first_token, rule, subclause);
}

analysis_result semantic_analysis::ill_formed_at_token(token_index at,
                                                       const char *rule,
                                                       const char *subclause)
{
    diagnostics.issue(diagnostic_id::sema_violation, tokens.location(at))
        << rule << subclause;
    return analysis_result::ill_formed;
}

analysis_result semantic_analysis::extension_used(diagnostic_id id,
                                                  token_index at,
                                                  const char *argument)
{
    diagnostic_severity severity = diagnostics.severity_now(id);
    diagnostic_message &m = diagnostics.issue(id, tokens.location(at));
    if (argument)
        m << argument;
    if (severity == diagnostic_severity::error ||
        severity == diagnostic_severity::fatal)
        return analysis_result::ill_formed;
    return analysis_result::well_formed;
}

bool semantic_analysis::in_system_header(token_index at) const
{
    if (!diagnostics.files)
        return false;
    return diagnostics.files->in_system_header(tokens.location(at) +
                                               diagnostics.location_base);
}

bool semantic_analysis::parsed_completely(node_index n) const
{
    /* the nodes under n, without recursion: a tree is as deep as its
       source is nested */
    owned_array<node_index> work;
    work.append(n);
    while (!work.empty()) {
        node_index next = work.back();
        work.resize(work.size() - 1);
        const syntax_node &s = tree[next];
        if (s.kind == syntax_kind::erroneous || s.kind == syntax_kind::unparsed)
            return false;
        for (uint32_t i = 0; i < s.child_count; i++) {
            syntax_child c = tree.children(next)[i];
            if (!c.is_token())
                work.append(c.index());
        }
    }
    return true;
}

/* whether the node is a namespace-definition ([namespace.def]) */
static bool is_namespace_definition(const syntax_tree &tree, node_index n)
{
    syntax_kind kind = tree[n].kind;
    return kind == syntax_kind::named_namespace_definition ||
           kind == syntax_kind::unnamed_namespace_definition ||
           kind == syntax_kind::nested_namespace_definition;
}

analysis_result semantic_analysis::declaration(node_index n)
{
    analysed_declaration d;
    d.node = n;
    current_declaration = n;
    size_t unsupported_before = unsupported.size();
    if (is_namespace_definition(tree, n)) {
        /* the declarations of its body are analysed as declarations of
           their own: one of them not parsed does not make the namespace
           so, and the definition has no record unless it is not
           well-formed itself */
        d.result = namespace_definition(n);
        if (d.result == analysis_result::well_formed)
            return d.result;
    } else if (!parsed_completely(n))
        d.result = analysis_result::not_parsed;
    else
        d.result = declaration_by_kind(n);
    if (d.result == analysis_result::unsupported)
        d.first_unsupported = static_cast<uint32_t>(unsupported_before);
    d.has_unsupported = unsupported.size() > unsupported_before;
    if (d.result != analysis_result::well_formed)
        hold_names(n);
    declarations.append(d);
    return d.result;
}

analysis_result semantic_analysis::declaration_by_kind(node_index n)
{
    return syntax(n).declaration().visit(Overloaded{
        [&](FunctionDefinition f) {
            /* [dcl.fct]/18: an abbreviated function template */
            if (abbreviated_function(f) && !out_of_class_member(f))
                return function_template_definition(f, f);
            return function_definition(f);
        },
        [&](TemplateDeclaration t) { return template_declaration(t); },
        [&](Declaration d) { return other_declaration(d); }});
}

/* a declaration of an alternative without a view of its own */
analysis_result semantic_analysis::other_declaration(Declaration d)
{
    node_index n = d;
    switch (d.kind()) {
    case syntax_kind::gnu_extension_declaration:
        /* [ext:gnu_extension_keyword]: __extension__ changes no meaning */
        return declaration_by_kind(tree.children(n)[1].index());
    case syntax_kind::empty_declaration:
        return empty_declaration(n);
    case syntax_kind::static_assert_declaration:
        return static_assert_declaration(n);
    case syntax_kind::alias_declaration:
        return alias_declaration(n);
    case syntax_kind::explicit_specialization:
        return explicit_specialization(n);
    case syntax_kind::explicit_instantiation:
        return explicit_instantiation(n);
    case syntax_kind::deduction_guide:
        return deduction_guide(n);
    case syntax_kind::simple_declaration:
        return simple_declaration(n);
    case syntax_kind::linkage_specification:
        return linkage_specification(n);
    case syntax_kind::named_namespace_definition:
    case syntax_kind::unnamed_namespace_definition:
    case syntax_kind::nested_namespace_definition:
        /* in a linkage-specification */
        return namespace_definition(n);
    case syntax_kind::using_directive:
        return using_directive(n);
    case syntax_kind::namespace_alias_definition:
        return namespace_alias_definition(n);
    case syntax_kind::using_declaration:
        return using_declaration(n);
    case syntax_kind::opaque_enum_declaration: {
        type_index type = 0;
        return enum_specifier(n, type);
    }
    default:
        return not_supported(n);
    }
}

void semantic_analysis::translation_unit()
{
    /* [impl.clang] a typedef-name every translation unit starts with
       (the parser's names know it too) */
    {
        static const char name[] = "__builtin_va_list";
        uint32_t id = tokens.pp.identifier(name, sizeof name - 1) + 1;
        entity_index e = entity_table.make(entity_kind::typedef_name, id, 0);
        entity_table[e].type = qualified_type::make(
            type_table.fundamental(fundamental_type::builtin_va_list), cv_none);
        entity_table[e].defined = true;
        entity_table.bind(0, id, e);
    }
    /* [impl.clang] __int128_t and __uint128_t, as the parser's names */
    static const struct {
        const char *name;
        fundamental_type type;
    } int128_names[] = {{"__int128_t", fundamental_type::int128},
                        {"__uint128_t", fundamental_type::unsigned_int128}};
    for (const auto &t : int128_names) {
        uint32_t id = tokens.pp.identifier(t.name, strlen(t.name)) + 1;
        entity_index e = entity_table.make(entity_kind::typedef_name, id, 0);
        entity_table[e].type =
            qualified_type::make(type_table.fundamental(t.type), cv_none);
        entity_table[e].defined = true;
        entity_table.bind(0, id, e);
    }
    owned_array<node_index> all;
    tree.declarations(&all);
    for (node_index n : all)
        declaration(n);
    instantiate_pending();
}

} // namespace cplusplus
