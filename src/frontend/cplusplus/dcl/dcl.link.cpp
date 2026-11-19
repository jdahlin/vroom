/*
 * dcl.link.cpp: [dcl.link] Linkage specifications.
 *
 *     linkage-specification:
 *         extern string-literal { declaration-seq_opt }
 *         extern string-literal declaration
 *
 * The grammar is namespace.def.cpp's.  [dcl.link]/2: "The string-literal
 * indicates the required language linkage. ... Every implementation shall
 * provide for linkage to functions written in the C programming language,
 * "C", and linkage to C++ functions, "C++"."  /5: "Linkage specifications
 * nest.  When linkage specifications nest, the innermost one determines
 * the language linkage."  /1: "the names of functions with external linkage
 * and variable names with external linkage" are the ones that have one; a
 * function or variable of C language linkage is named in the object file
 * by its identifier ([abi:mangling]: "entities with C linkage ... are not
 * mangled").
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

analysis_result semantic_analysis::linkage_specification(LinkageSpecification n)
{
    auto node = n;
    if (node.size() < 3 || !node[1].is_token())
        return not_supported(n, "dcl.link");
    /* [dcl.link]/4: "A linkage-specification shall inhabit a namespace
       scope." */
    owned_buffer spelling;
    tokens.spelling(node[1].token(), spelling.get());
    bool c = false;
    if (spelling.size() == 3 && !memcmp(spelling.data(), "\"C\"", 3))
        c = true;
    else if (!(spelling.size() == 5 && !memcmp(spelling.data(), "\"C++\"", 5)))
        /* [dcl.link]/2: "Use of a string-literal other than "C" or "C++" is
           conditionally-supported, with implementation-defined
           semantics." */
        return not_supported(n, "dcl.link");
    owned_array<node_index> inner;
    bool braces = false;
    for (auto x : node.elements(2)) {
        if (x.is_token()) {
            braces = true;
            continue;
        }
        if (x.is(syntax_kind::declaration_seq)) {
            for (SyntaxElement y : x.elements()) {
                if (!y.is_token())
                    inner.append(y.index());
            }
            continue;
        }
        inner.append(x.index());
    }
    bool saved = c_language;
    bool saved_braces = linkage_without_braces;
    c_language = c;
    linkage_without_braces = !braces;
    analysis_result result = analysis_result::well_formed;
    for (node_index d : inner) {
        current_declaration = d;
        if (!parsed_completely(syntax(d))) {
            result = worse(result, analysis_result::not_parsed);
            continue;
        }
        result = worse(result, declaration_by_kind(syntax(d)));
    }
    c_language = saved;
    linkage_without_braces = saved_braces;
    current_declaration = n;
    return result;
}

} // namespace cplusplus
