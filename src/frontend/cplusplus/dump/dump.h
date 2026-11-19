/*
 * dump.h: the C++ front end's developer outputs, which the driver writes
 * for `vcc -x c++ -fsyntax-only -fcxx-dump=WHAT[,WHAT...] FILE`
 * (lower/compile.cpp).  bench/cxx-lex.py, cxx-pp.py, cxx-parse.py and
 * cxx-sema.py compare them with what clang says about the same file.
 *
 * Each output belongs to the phase that makes it: the translation unit is
 * taken only as far as the deepest output asked for, so that the
 * diagnostics are that phase's (a lexing dump does not report a parse
 * error).  The outputs go to stdout in the order of the table below,
 * diagnostics to stderr.
 */
#ifndef VCC_CPLUSPLUS_DUMP_H
#define VCC_CPLUSPLUS_DUMP_H

#include <stdint.h>

#include "frontend/cplusplus/parse.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* X(identifier, name, phase, what it writes) */
#define CPLUSPLUS_DUMPS(X)                                                     \
    X(selftest, "selftest", none,                                              \
      "the tables no rule uses yet, and the claims' encoding (no input "       \
      "read)")                                                                 \
    X(raw_tokens, "raw-tokens", lexing,                                        \
      "phase 3: preprocessing tokens, white space and comments, as clang "     \
      "-cc1 -dump-raw-tokens")                                                 \
    X(definitions, "definitions", preprocessing,                               \
      "the macros defined at the end, as clang -E -dM")                        \
    X(preprocessed, "preprocessed", preprocessing,                             \
      "phase 4's tokens, a line where a line started (clang "                  \
      "-E -P without its spacing)")                                            \
    X(tokens, "tokens", preprocessing,                                         \
      "phase 7's tokens, as clang -cc1 -dump-tokens")                          \
    X(literal_claims, "literal-claims", preprocessing,                         \
      "each literal's type and value as static_asserts for clang")             \
    X(parser_tokens, "parser-tokens", parsing,                                 \
      "phase 7's tokens through the parser's token stream, as tokens")         \
    X(syntax, "syntax", parsing,                                               \
      "the syntax tree, a nonterminal or token per line")                      \
    X(expression_claims, "expression-claims", parsing,                         \
      "per expression: its tokens, and the same with every operator's "        \
      "operands parenthesized as the tree has them")                           \
    X(declarator_claims, "declarator-claims", parsing,                         \
      "per declared name: the type its declarator gives it")                   \
    X(declarations, "declarations", parsing,                                   \
      "per declaration: its kind, first and last token, erroneous and "        \
      "unparsed parts")                                                        \
    X(syntax_conformance, "syntax-conformance", parsing,                       \
      "per child node its productions do not allow (syntax-views.inc): "       \
      "parent kind, child kind, FILE:LINE:COL")                                \
    X(parser_stats, "parser-stats", parsing,                                   \
      "tokens, nodes, tentative parses, lookups, unparsed nodes")              \
    X(analysis, "analysis", analysis,                                          \
      "per declaration: its kind, first and last token, and well-formed, "     \
      "ill-formed, not-parsed or unsupported=[SUBCLAUSE]")                     \
    X(unsupported, "unsupported", analysis,                                    \
      "per construct without rules: [SUBCLAUSE] nonterminal FILE:LINE:COL")    \
    X(declaration_claims, "declaration-claims", analysis,                      \
      "per declared name: the type analysis gave it")                          \
    X(type_claims, "type-claims", analysis,                                    \
      "per typed expression: its type, value category and constant value")     \
    X(lookup_claims, "lookup-claims", analysis,                                \
      "per name lookup resolved: the declaration it found")                    \
    X(check_tree, "check-tree", analysis,                                      \
      "the semantic tree of each well-formed definition is complete: a "       \
      "line per problem")                                                      \
    X(tree, "tree", analysis,                                                  \
      "the semantic tree of each definition, a node per line")                 \
    X(symbols, "symbols", analysis,                                            \
      "per function and variable of a well-formed declaration: its symbol")    \
    X(layout_claims, "layout-claims", analysis,                                \
      "per complete class: its size, data size and alignment, and the "        \
      "offset of each base and non-static data member")                        \
    X(stats, "stats", analysis, "declarations by result, types, entities")

enum class dump_phase { none, lexing, preprocessing, parsing, analysis };

enum class dump_output : uint32_t {
#define X(identifier, name, phase, what) identifier,
    CPLUSPLUS_DUMPS(X)
#undef X
};

/* the outputs asked for, and the options that change them */
struct dump_request {
    uint32_t outputs = 0;
    /* -fcxx-phase4-diagnostics-only: phase 7's diagnostics are not issued
       (clang -cc1 -dump-tokens does not convert literals) */
    bool phase4_diagnostics_only = false;
    /* -fcxx-debug-fallbacks: the parser reports each declaration it keeps
       as tokens */
    bool debug_fallbacks = false;
    /* -fcxx-speculation-limit=N: the parser's tentative parses per
       declaration (0: its default), to test the limit */
    uint32_t speculation_limit = 0;

    bool has(dump_output d) const
    {
        return outputs & (1u << static_cast<uint32_t>(d));
    }
    /* the deepest phase an output asked for needs */
    dump_phase phase() const;
};

/* -fcxx-dump=LIST: adds the outputs to r; false at an unknown name (*bad
   the name) */
bool dump_request_parse(const char *list, dump_request &r, const char **bad);
/* -fcxx-dump=help: the table */
void dump_help(vbuffer *out);

/* selftest: true when every check holds; a line per failure on out
   (dump_selftest runs both) */
bool dump_selftest(vbuffer *out);
bool dump_parsing_selftest(vbuffer *out);
bool dump_analysis_selftest(vbuffer *out);

/* raw-tokens: phase 3 alone, on the file as it is, its diagnostics
   rendered on messages; false if it cannot be read */
bool dump_raw_tokens(const char *path,
                     const implementation_defined_behavior &behavior,
                     diagnostic_consumer &diagnostics, vbuffer *out,
                     vbuffer *messages);

/* definitions, preprocessed, tokens, literal-claims: through phase 7,
   tokens taken from pp */
void dump_preprocessing(const dump_request &r, source_files &files,
                        preprocessor &pp, diagnostic_consumer &diagnostics,
                        vbuffer *out);

/* parser-tokens: the token stream, before any parse */
void dump_parser_tokens(const dump_request &r, source_files &files,
                        token_stream &tokens, diagnostic_consumer &diagnostics,
                        vbuffer *out);

/* syntax, expression-claims, declarator-claims, declarations,
   parser-stats */
void dump_parsing(const dump_request &r, const syntax_tree &tree,
                  token_stream &tokens, parser &p, vbuffer *out);

/* analysis, unsupported, declaration-claims, type-claims, lookup-claims,
   check-tree, tree, symbols, layout-claims, stats */
void dump_analysis(const dump_request &r, const semantic_analysis &analysis,
                   const syntax_tree &tree, token_stream &tokens, vbuffer *out);

/* a token as clang -cc1 -dump-raw-tokens / -dump-tokens prints it */
void dump_token(vbuffer *out, const char *name, const char *spelling, size_t n,
                source_position p, const source_files *files,
                const preprocessing_token &pp, bool raw,
                const unsigned char *physical);

} // namespace cplusplus

#endif
