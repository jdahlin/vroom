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
                                     token_stream &tokens,
                                     diagnostic_consumer &diagnostics)
    : tree(tree), tokens(tokens), diagnostics(diagnostics)
{
    unsupported.append(unsupported_construct());
}

analysis_result semantic_analysis::not_supported(node_index n)
{
    return not_supported(n, syntax_kind_subclause(tree[n].kind));
}

analysis_result semantic_analysis::not_supported(node_index n,
                                                 const char *subclause)
{
    unsupported_construct u;
    u.node = n;
    u.subclause = subclause;
    unsupported.append(u);
    return analysis_result::unsupported;
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

analysis_result semantic_analysis::declaration(node_index n)
{
    analysed_declaration d;
    d.node = n;
    size_t unsupported_before = unsupported.size();
    if (!parsed_completely(n)) {
        d.result = analysis_result::not_parsed;
    } else {
        switch (tree[n].kind) {
        case syntax_kind::empty_declaration:
            d.result = empty_declaration(n);
            break;
        default:
            d.result = not_supported(n);
            break;
        }
    }
    if (d.result == analysis_result::unsupported)
        d.first_unsupported = static_cast<uint32_t>(unsupported_before);
    declarations.append(d);
    return d.result;
}

void semantic_analysis::translation_unit()
{
    owned_array<node_index> all;
    tree.declarations(&all);
    for (node_index n : all)
        declaration(n);
}

} // namespace cplusplus
