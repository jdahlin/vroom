/*
 * cxxparse: the C++ front end's parser (src/frontend/cplusplus,
 * doc/PLAN-cxx-parse.md) on one translation unit.
 *
 *   cxxparse [-fsyntax-only] [OPTIONS] FILE  parse; diagnostics on stderr
 *   cxxparse -dump-syntax [OPTIONS] FILE     the syntax tree, a nonterminal
 *                                            or token per line
 *   cxxparse -dump-declarations ...          one line per declaration of the
 *                                            translation unit: the node's
 *                                            kind, and the line:column of
 *                                            its first and of its last token
 *   cxxparse -dump-tokens ...                phase 7's tokens through the
 *                                            parser's token stream, as
 *                                            cxxlex -dump-tokens prints them
 *   cxxparse -expression-claims ...          one line per expression: where
 *                                            its declaration ends, its tokens,
 *                                            and the same with every operator's
 *                                            operands parenthesized as the tree
 *                                            has them (bench/cxx-parse.py has
 *                                            clang compare the two)
 *   cxxparse -declaration-claims ...         one line per declared name: the
 *                                            type its declarator gives it, in
 *                                            an encoding clang checks
 *                                            (declarator-claims.h)
 *   cxxparse -stats ...                      tokens, nodes, tentative parses,
 *                                            lookups, scaffolding nodes, the
 *                                            time from the start of phase 1
 *                                            to the end of the parse and the
 *                                            peak resident memory
 *   cxxparse -stats -tokens-only ...         the same for phases 1-7 alone:
 *                                            every token made, none parsed
 *                                            (bench/cxx-parse-speed.py takes
 *                                            the difference)
 *   cxxparse -selftest                       the parts no grammar uses yet
 *   cxxparse -batch [-header-cache] ... FILE...
 *                                            each FILE in turn, as its own
 *                                            translation unit, in one process;
 *                                            -header-cache shares the
 *                                            preprocessor's header cache
 *                                            between them (OPTIMIZATION,
 *                                            cpp/cpp.header_cache.cpp; -stats
 *                                            adds its counters at the end)
 *
 * OPTIONS are cxxlex's (src/bin/cxxlex/command-line.h).  The exit status is
 * 1 if a diagnostic was an error, 2 if the file cannot be read.
 */
#include <sys/resource.h>

#include "../cxxlex/command-line.h"
#include "port/clock.h"
#include "frontend/cplusplus/parse.h"
#include "../../frontend/cplusplus/lex/lex.simd.h"

#include "declarator-claims.h"

static bool check(bool condition, const char *what)
{
    if (!condition)
        fprintf(stderr, "cxxparse -selftest: FAILED: %s\n", what);
    return condition;
}

/*
 * declared_names and the tree's truncation have no grammar that uses them
 * before step P1: what they promise is checked here.
 */
static int selftest()
{
    bool ok = true;
    {
        /* OPTIMIZATION: lex.simd.h's searches are its scalar definitions,
           on generated text with NULs inside and 32 after */
        static const char alphabet[] = "aZ_09 \t\n\r\\?*/x$#\x80\x01";
        unsigned char text[300];
        uint32_t state = 1;
        auto random = [&state]() {
            state = state * 1103515245u + 12345u;
            return state >> 8;
        };
        bool same = true;
        for (int round = 0; round < 20000 && same; round++) {
            uint32_t n = random() % 260;
            for (uint32_t i = 0; i < n; i++)
                text[i] = alphabet[random() % (sizeof alphabet - 1)];
            memset(text + n, 0, 32);
            if (n && !(random() % 8))
                text[random() % n] = 0;
            for (int k = 0; k < 5; k++)
                for (uint32_t at = 0; at <= n; at += 1 + random() % 5)
                    same &= lex_search((lex_stop)k, text, at) ==
                            lex_search_scalar((lex_stop)k, text, at);
        }
        ok &= check(same, "lex.simd.h searches = scalar");
    }
    {
        declared_names names;
        const uint32_t N = 1, T = 2, x = 3, M = 4;
        ok &= check(names.lookup_unqualified(T) == name_kind::not_declared,
                    "an undeclared name");
        names.declare(T, name_kind::type_name);
        scope_index n = names.enter_namespace(N, false);
        ok &= check(names.lookup_unqualified(T) == name_kind::type_name,
                    "[basic.lookup.unqual]/1: the enclosing namespace");
        names.declare(T, name_kind::other);
        names.declare(x, name_kind::other);
        ok &= check(names.lookup_unqualified(T) == name_kind::other,
                    "the innermost declaration is found first");
        scope_index m = names.enter_namespace(M, false);
        names.leave();
        names.enter(scope_kind::block);
        names.declare(x, name_kind::type_name);
        ok &= check(names.lookup_unqualified(x) == name_kind::type_name,
                    "a block's declaration");
        names.leave();
        ok &= check(names.lookup_unqualified(x) == name_kind::other,
                    "leaving a block ends its declarations");
        names.leave();
        ok &= check(names.current() == declared_names::global_namespace,
                    "back in the global namespace");
        ok &= check(names.lookup_unqualified(T) == name_kind::type_name,
                    "the global declaration again");
        ok &= check(names.lookup_unqualified(x) == name_kind::not_declared,
                    "a namespace's member is not found outside it");
        scope_index found = 0;
        ok &=
            check(names.lookup_qualified(declared_names::global_namespace, N,
                                         &found) == name_kind::namespace_name &&
                      found == n,
                  "[basic.lookup.qual]/1: a namespace-name and its region");
        ok &= check(names.lookup_qualified(n, x) == name_kind::other,
                    "a qualified member");
        ok &= check(names.lookup_qualified(n, M, &found) ==
                            name_kind::namespace_name &&
                        found == m,
                    "a nested namespace");
        ok &= check(names.enter_namespace(N, false) == n,
                    "[namespace.def]/2: the namespace is extended");
        ok &= check(names.lookup_unqualified(x) == name_kind::other,
                    "the extended namespace has the first part's members");
        names.leave();
        ok &=
            check(names.lookups.size() == 11 &&
                      names.lookups[1].found == name_kind::type_name &&
                      !names.lookups[1].qualified && names.lookups[8].qualified,
                  "every lookup is recorded");
    }
    {
        syntax_tree tree;
        node_index a = tree.make(syntax_kind::unparsed, nullptr, 0, 0, 2);
        size_t size = tree.size();
        node_index b = tree.make(syntax_kind::unparsed, nullptr, 0, 2, 3);
        syntax_child both[] = {syntax_child::node(a), syntax_child::node(b),
                               syntax_child::token(3)};
        node_index seq = tree.make(syntax_kind::declaration_seq, both, 3, 0, 4);
        ok &= check(tree[seq].child_count == 3 &&
                        tree.children(seq)[1].index() == b &&
                        !tree.children(seq)[1].is_token() &&
                        tree.children(seq)[2].is_token() &&
                        tree.children(seq)[2].index() == 3,
                    "a node's children");
        ok &= check(tree.count(syntax_kind::unparsed) == 2,
                    "nodes are counted by kind");
        tree.truncate(size);
        ok &= check(tree.size() == size &&
                        tree.count(syntax_kind::unparsed) == 1 &&
                        tree.count(syntax_kind::declaration_seq) == 0,
                    "truncate takes back the nodes of a failed parse");
        node_index c = tree.make(syntax_kind::declaration_seq, both, 1, 0, 2);
        ok &= check(c == b && tree[c].first_child == 0 &&
                        tree.children(c)[0].index() == a,
                    "the tree is built on after a truncation");
    }
    if (ok)
        printf("cxxparse -selftest: ok\n");
    if (ok)
        return 0;
    return 1;
}

/* the tokens [first, end) as they are spelled, a space after each */
static void write_tokens(token_stream &tokens, token_index first,
                         token_index end, vbuffer *out)
{
    for (token_index t = first; t < end; t++) {
        tokens.spelling(t, out);
        vbuffer_append(out, " ", 1);
    }
}

/*
 * The tokens of node n, with parentheses around every expression that has
 * an operator: what the tree says the operands of each operator are.
 * [expr.prim.paren]/1: "The parenthesized expression can be used in exactly
 * the same contexts as those where E can be used, and with the same
 * meaning, except as otherwise indicated."  The exceptions are left as
 * written: the operand of decltype ([dcl.type.decltype]/1: an
 * unparenthesized id-expression or class member access), a qualified-id
 * (the operand of & in [expr.unary.op]/4 forms a pointer to member only
 * when it is not enclosed in parentheses), and a call or a type's
 * ( expression-list_opt ): (T()) before an operand is ( type-id ), a cast
 * ([dcl.ambig.res]/2), and nothing binds more tightly than they do, so
 * their own parentheses would say nothing.
 */
static void write_parenthesized(const syntax_tree &tree, token_stream &tokens,
                                node_index root, vbuffer *out)
{
    /* the nodes being written, outermost first (a chain of a thousand
       additions is a thousand nodes deep: no recursion) */
    struct level {
        node_index node;
        uint32_t next_child;
        bool parentheses;
    };
    owned_array<level> path;
    token_index split_written = UINT32_MAX;
    auto enter = [&](node_index n) {
        const syntax_node &s = tree[n];
        if (s.kind == syntax_kind::decltype_specifier ||
            s.kind == syntax_kind::unparsed ||
            s.kind == syntax_kind::erroneous) {
            write_tokens(tokens, s.first_token, s.end_token, out);
            return;
        }
        bool parentheses = syntax_kind_is_expression(s.kind) &&
                           s.kind != syntax_kind::qualified_id &&
                           s.kind != syntax_kind::primary_expression;
        if (s.kind == syntax_kind::postfix_expression && s.child_count) {
            syntax_child last = tree.children(n)[s.child_count - 1];
            if (last.is_token() &&
                tokens[last.index()].kind ==
                    token_kind::operator_or_punctuator &&
                preprocessing_op_or_punc_primary(
                    tokens[last.index()].op_or_punc) ==
                    preprocessing_op_or_punc::RIGHT_PARENTHESIS)
                parentheses = false;
        }
        if (parentheses)
            vbuffer_append(out, "( ", 2);
        path.append({n, 0, parentheses});
    };
    enter(root);
    while (!path.empty()) {
        level &top = path.back();
        const syntax_node &s = tree[top.node];
        if (top.next_child == s.child_count) {
            if (top.parentheses)
                vbuffer_append(out, ") ", 2);
            path.resize(path.size() - 1);
            continue;
        }
        syntax_child c = tree.children(top.node)[top.next_child++];
        if (c.is_token() && tree.is_split(c.index())) {
            /* a split '>>': its first '>', then the rest */
            if (split_written != c.index()) {
                vbuffer_append(out, "> ", 2);
                split_written = c.index();
            } else {
                owned_buffer spelling;
                tokens.spelling(c.index(), spelling.get());
                vbuffer_append(out, spelling.data() + 1, spelling.size() - 1);
                vbuffer_append(out, " ", 1);
                split_written = UINT32_MAX;
            }
        } else if (c.is_token())
            write_tokens(tokens, c.index(), c.index() + 1, out);
        else
            enter(c.index());
    }
}

/*
 * -expression-claims: for each expression that is not part of another (and
 * has an operator), a line
 *
 *     FILE <tab> LINE:COLUMN <tab> the expression <tab> it parenthesized
 *
 * where LINE:COLUMN is just after the declaration the expression is in (a
 * declaration of a declaration-seq), a place where a static_assert about
 * the expression can stand.  A declaration with a token a macro made is
 * left out: its tokens are not at one place in the file.  So is an
 * expression that gets no parentheses: there is nothing to check.
 */
static void write_expression_claims(const syntax_tree &tree,
                                    token_stream &tokens, node_index n,
                                    node_index declaration, bool in_sequence,
                                    vbuffer *out)
{
    const syntax_node &s = tree[n];
    if (in_sequence) {
        declaration = n;
        for (token_index t = s.first_token; t < s.end_token; t++)
            if (tokens[t].preprocessing.expansion)
                return;
    }
    if (declaration && syntax_kind_is_expression(s.kind) &&
        s.kind != syntax_kind::qualified_id) {
        const syntax_node &d = tree[declaration];
        source_position end = tokens.position(d.end_token - 1);
        uint32_t extent = tokens[d.end_token - 1].preprocessing.extent;
        owned_buffer written, parenthesized;
        write_tokens(tokens, s.first_token, s.end_token, written.get());
        write_parenthesized(tree, tokens, n, parenthesized.get());
        if (written.size() == parenthesized.size() &&
            !memcmp(written.data(), parenthesized.data(), written.size()))
            return;
        vbuffer_format_append(out, "%s\t%u:%u\t", end.file_name, end.line,
                              end.column + extent);
        vbuffer_append(out, written.data(), written.size());
        vbuffer_append(out, "\t", 1);
        vbuffer_append(out, parenthesized.data(), parenthesized.size());
        vbuffer_append(out, "\n", 1);
        return;
    }
    for (uint32_t i = 0; i < s.child_count; i++) {
        syntax_child c = tree.children(n)[i];
        if (!c.is_token())
            write_expression_claims(tree, tokens, c.index(), declaration,
                                    s.kind == syntax_kind::declaration_seq,
                                    out);
    }
}

static int translate(command_line &options, const char *path, bool dump_syntax,
                     bool dump_declarations, bool dump_tokens, bool stats,
                     bool phase4_only, bool debug_fallbacks,
                     bool expression_claims, bool declaration_claims,
                     bool tokens_only);

int main(int argc, char **argv)
{
    command_line options("cxxparse");
    bool dump_syntax = false, dump_declarations = false, dump_tokens = false,
         stats = false, phase4_only = false, debug_fallbacks = false,
         expression_claims = false, declaration_claims = false,
         tokens_only = false;
    owned_array<const char *> paths;
    bool batch = false, use_cache = false;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-selftest"))
            return selftest();
        if (!strcmp(a, "-dump-syntax"))
            dump_syntax = true;
        else if (!strcmp(a, "-dump-declarations"))
            dump_declarations = true;
        else if (!strcmp(a, "-dump-tokens"))
            dump_tokens = true;
        else if (!strcmp(a, "-expression-claims"))
            expression_claims = true;
        else if (!strcmp(a, "-declaration-claims"))
            declaration_claims = true;
        else if (!strcmp(a, "-phase4-diagnostics-only"))
            phase4_only = true;
        else if (!strcmp(a, "-debug-fallbacks"))
            debug_fallbacks = true;
        else if (!strcmp(a, "-stats"))
            stats = true;
        else if (!strcmp(a, "-tokens-only"))
            tokens_only = true;
        else if (!strcmp(a, "-batch"))
            batch = true;
        else if (!strcmp(a, "-header-cache"))
            use_cache = true;
        else if (!strcmp(a, "-fsyntax-only"))
            ;
        else if (options.option(argc, argv, i))
            ;
        else if (a[0] != '-' || !a[1])
            paths.append(a);
    }
    if (paths.empty() || (paths.size() > 1 && !batch)) {
        fprintf(
            stderr,
            "usage: cxxparse [-fsyntax-only|-dump-syntax|"
            "-dump-declarations|-dump-tokens|-expression-claims|"
            "-declaration-claims|-stats [-tokens-only]] [clang -cc1 options] "
            "FILE\n       cxxparse -batch [-header-cache] ... FILE...\n"
            "       cxxparse -selftest\n");
        return 2;
    }
    options.apply();
    header_cache cache;
    if (use_cache)
        options.pp_options.cache = &cache;
    int status = 0;
    for (const char *path : paths) {
        int s = translate(options, path, dump_syntax, dump_declarations,
                          dump_tokens, stats, phase4_only, debug_fallbacks,
                          expression_claims, declaration_claims, tokens_only);
        if (s > status)
            status = s;
    }
    if (stats && use_cache) {
        owned_buffer out;
        vbuffer_format_append(
            out.get(),
            "header-cache-hits %llu\nheader-cache-misses %llu\n"
            "header-cache-recorded %llu\nheader-cache-not-kept %llu\n"
            "header-cache-tokens-replayed %llu\n",
            (unsigned long long)cache.hits, (unsigned long long)cache.misses,
            (unsigned long long)cache.recorded,
            (unsigned long long)cache.spoiled,
            (unsigned long long)cache.tokens_replayed);
        fwrite(out.data(), 1, out.size(), stdout);
    }
    return status;
}

/* one translation unit */
static int translate(command_line &options, const char *path, bool dump_syntax,
                     bool dump_declarations, bool dump_tokens, bool stats,
                     bool phase4_only, bool debug_fallbacks,
                     bool expression_claims, bool declaration_claims,
                     bool tokens_only)
{

    diagnostic_consumer diagnostics(options.diagnostic_settings);
    source_files files(options.behavior, diagnostics);
    diagnostics.files = &files;
    size_t path_length = strlen(path);
    files.file_options.preprocessed_input =
        (path_length > 3 && !strcmp(path + path_length - 3, ".ii")) ||
        (path_length > 2 && !strcmp(path + path_length - 2, ".i"));
    double start = vclock_seconds();
    preprocessor pp(files, options.pp_options);
    if (!pp.start(path)) {
        fprintf(stderr, "cxxparse: cannot read %s\n", path);
        return 2;
    }
    token_stream tokens(pp);
    owned_buffer out, spelling;
    auto finish = [&]() {
        fwrite(out.data(), 1, out.size(), stdout);
        owned_buffer messages;
        for (const diagnostic_message &m : diagnostics.messages)
            diagnostics.render(m, messages.get());
        fwrite(messages.data(), 1, messages.size(), stderr);
        if (diagnostics.error_count)
            return 1;
        return 0;
    };

    /* the peak resident memory, in bytes (ru_maxrss is bytes on Darwin,
       KiB on Linux) */
    auto peak_resident = []() {
        struct rusage usage;
        getrusage(RUSAGE_SELF, &usage);
#ifdef __APPLE__
        return (unsigned long long)usage.ru_maxrss;
#else
        return (unsigned long long)usage.ru_maxrss * 1024;
#endif
    };
    if (tokens_only) {
        token_index i = 0;
        while (!tokens.at_end(i))
            i++;
        double seconds = vclock_seconds() - start;
        if (stats)
            vbuffer_format_append(out.get(),
                                  "tokens %llu\nnanoseconds %llu\n"
                                  "peak-resident-bytes %llu\n",
                                  (unsigned long long)tokens.made(),
                                  (unsigned long long)(seconds * 1e9),
                                  peak_resident());
        return finish();
    }

    if (dump_tokens) {
        /* as cxxlex -dump-tokens: the same lines from the same tokens,
           which is how bench/cxx-parse.py --tokens knows the token stream
           adds and loses nothing */
        const lexer &l = files.phase7();
        if (phase4_only)
            diagnostics.suppressed++;
        auto embedded = [&](const preprocessing_token &t) {
            const source_files::entry *e = files.find(t.location);
            return e && e->scratch && !strcmp(e->name.c_str(), "<embed>");
        };
        for (token_index i = 0;; i++) {
            const token t = tokens[i];
            size_t count;
            const preprocessing_token *piece = tokens.pieces(i, &count);
            /* #embed's bytes ([ext.clang]): one line, as clang's annot_embed */
            if (embedded(piece[0])) {
                while (embedded(tokens[i + 1].preprocessing))
                    i++;
                vbuffer_string_append(out.get(),
                                      "annot_embed ''\tLoc=<embed>\n");
                continue;
            }
            for (size_t k = 0; k < count; k++) {
                /* pieces() is the stream's own storage, which a later token
                   may move: a copy */
                const preprocessing_token p = tokens.pieces(i, &count)[k];
                const char *name = clang_token_name(l, t);
                if (count > 1)
                    name = clang_token_name(l, p);
                spelling.clear();
                if (p.kind != preprocessing_token_kind::end_of_file)
                    files.spelling(p, spelling.get());
                const source_files::entry *e = files.find(p.location);
                source_location place = p.location;
                const unsigned char *physical = nullptr;
                if (p.expansion)
                    place = p.expansion;
                else if (e)
                    physical =
                        e->file.physical_characters + p.location - e->base;
                write_token(out.get(), name, spelling.data(), spelling.size(),
                            files.position(place), &files, p, false, physical);
            }
            if (tokens.at_end(i))
                break;
        }
        return finish();
    }

    syntax_tree tree;
    declared_names names;
    parser p(tokens, tree, names, diagnostics);
    p.debug_fallbacks = debug_fallbacks;
    p.translation_unit();
    double seconds = vclock_seconds() - start;

    if (dump_syntax)
        tree.dump(tree.root, tokens, out.get());
    if (expression_claims)
        write_expression_claims(tree, tokens, tree.root, 0, false, out.get());
    if (declaration_claims)
        declarator_claims(tree, tokens, p, out.get()).write(tree.root);
    if (dump_declarations) {
        /* translation-unit: declaration-seq_opt; per declaration, whether
           it has an erroneous node, and whether a declaration in it was
           kept as tokens because it uses a later step's grammar (an
           unparsed node that is a whole declaration: a child of a
           declaration-seq or a linkage-specification) */
        /* a module unit's declarations: those of its global module
           fragment, its module-declaration, its purview and its private
           module fragment, in order */
        owned_array<node_index> declarations;
        auto add_seq = [&](node_index seq) {
            for (uint32_t i = 0; i < tree[seq].child_count; i++) {
                syntax_child c = tree.children(seq)[i];
                if (!c.is_token())
                    declarations.append(c.index());
            }
        };
        const syntax_node &unit = tree[tree.root];
        for (uint32_t u = 0; u < unit.child_count; u++) {
            syntax_child part = tree.children(tree.root)[u];
            if (part.is_token())
                continue;
            const syntax_node &x = tree[part.index()];
            if (x.kind == syntax_kind::declaration_seq) {
                add_seq(part.index());
            } else if (x.kind == syntax_kind::global_module_fragment ||
                       x.kind == syntax_kind::private_module_fragment) {
                for (uint32_t k = 0; k < x.child_count; k++) {
                    syntax_child y = tree.children(part.index())[k];
                    if (y.is_token())
                        continue;
                    if (tree[y.index()].kind == syntax_kind::declaration_seq)
                        add_seq(y.index());
                    else
                        declarations.append(y.index());
                }
            } else {
                declarations.append(part.index());
            }
        }
        {
            for (node_index declaration : declarations) {
                syntax_child c = syntax_child::node(declaration);
                const syntax_node &d = tree[c.index()];
                bool erroneous = false, fallback = false;
                owned_buffer opaque;
                if (d.kind == syntax_kind::unparsed)
                    fallback = true;
                owned_array<node_index> work;
                work.append(c.index());
                while (!work.empty()) {
                    node_index n = work.back();
                    work.resize(work.size() - 1);
                    const syntax_node &x = tree[n];
                    if (x.kind == syntax_kind::erroneous)
                        erroneous = true;
                    if (x.kind == syntax_kind::unparsed &&
                        x.end_token > x.first_token) {
                        /* a part kept as tokens: where it starts, and where
                           its last token ends */
                        source_position a = tokens.position(x.first_token);
                        source_position b = tokens.position(x.end_token - 1);
                        uint32_t extent =
                            tokens[x.end_token - 1].preprocessing.extent;
                        vbuffer_format_append(opaque.get(), "%s%u:%u-%u:%u",
                                              opaque.size() ? "," : "", a.line,
                                              a.column, b.line,
                                              b.column + extent);
                    }
                    for (uint32_t k = 0; k < x.child_count; k++) {
                        syntax_child y = tree.children(n)[k];
                        if (y.is_token())
                            continue;
                        if (tree[y.index()].kind == syntax_kind::unparsed &&
                            (x.kind == syntax_kind::declaration_seq ||
                             x.kind == syntax_kind::linkage_specification))
                            fallback = true;
                        work.append(y.index());
                    }
                }
                source_position first = tokens.position(d.first_token);
                vbuffer_format_append(out.get(), "%s %s:%u:%u",
                                      syntax_kind_name(d.kind), first.file_name,
                                      first.line, first.column);
                token_index last = d.first_token;
                if (d.end_token > d.first_token)
                    last = d.end_token - 1;
                source_position end = tokens.position(last);
                vbuffer_format_append(out.get(), " %s:%u:%u", end.file_name,
                                      end.line, end.column);
                if (erroneous)
                    vbuffer_string_append(out.get(), " erroneous");
                if (fallback)
                    vbuffer_string_append(out.get(), " fallback");
                if (opaque.size()) {
                    vbuffer_string_append(out.get(), " opaque=");
                    vbuffer_append(out.get(), opaque.data(), opaque.size());
                }
                vbuffer_string_append(out.get(), "\n");
            }
        }
    }
    if (stats) {
        parser_statistics s = p.statistics();
        vbuffer_format_append(
            out.get(),
            "tokens %llu\nnodes %llu\ntentative-parses %llu\nrewinds %llu\n"
            "rewound-tokens %llu\nlookups %llu\nunparsed %llu\n"
            "erroneous %llu\nbeyond-step %llu\nnanoseconds %llu\n"
            "peak-resident-bytes %llu\n",
            (unsigned long long)s.tokens, (unsigned long long)s.nodes,
            (unsigned long long)s.tentative_parses,
            (unsigned long long)s.rewinds, (unsigned long long)s.rewound_tokens,
            (unsigned long long)s.lookups, (unsigned long long)s.unparsed,
            (unsigned long long)s.erroneous, (unsigned long long)s.beyond_step,
            (unsigned long long)(seconds * 1e9), peak_resident());
    }
    return finish();
}
