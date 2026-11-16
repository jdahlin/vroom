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
 *   cxxparse -stats ...                      tokens, nodes, tentative parses,
 *                                            lookups, scaffolding nodes
 *   cxxparse -selftest                       the parts no grammar uses yet
 *
 * OPTIONS are cxxlex's (src/bin/cxxlex/command-line.h).  The exit status is
 * 1 if a diagnostic was an error, 2 if the file cannot be read.
 */
#include "../cxxlex/command-line.h"
#include "frontend/cplusplus/parse.h"

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
        declared_names names;
        const uint32_t N = 1, T = 2, x = 3, M = 4;
        ok &= check(names.lookup_unqualified(T) == name_kind::not_declared,
                    "an undeclared name");
        names.declare(T, name_kind::type_name);
        scope_index n = names.enter_namespace(N);
        ok &= check(names.lookup_unqualified(T) == name_kind::type_name,
                    "[basic.lookup.unqual]/1: the enclosing namespace");
        names.declare(T, name_kind::other);
        names.declare(x, name_kind::other);
        ok &= check(names.lookup_unqualified(T) == name_kind::other,
                    "the innermost declaration is found first");
        scope_index m = names.enter_namespace(M);
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
        ok &= check(names.enter_namespace(N) == n,
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

int main(int argc, char **argv)
{
    command_line options("cxxparse");
    bool dump_syntax = false, dump_declarations = false, dump_tokens = false,
         stats = false, phase4_only = false;
    const char *path = nullptr;

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
        else if (!strcmp(a, "-phase4-diagnostics-only"))
            phase4_only = true;
        else if (!strcmp(a, "-stats"))
            stats = true;
        else if (!strcmp(a, "-fsyntax-only"))
            ;
        else if (options.option(argc, argv, i))
            ;
        else if (a[0] != '-' || !a[1])
            path = a;
    }
    if (!path) {
        fprintf(stderr,
                "usage: cxxparse [-fsyntax-only|-dump-syntax|"
                "-dump-declarations|-dump-tokens|-stats] [clang -cc1 options] "
                "FILE\n       cxxparse -selftest\n");
        return 2;
    }
    options.apply();

    diagnostic_consumer diagnostics(options.diagnostic_settings);
    source_files files(options.behavior, diagnostics);
    diagnostics.files = &files;
    size_t path_length = strlen(path);
    files.file_options.preprocessed_input =
        (path_length > 3 && !strcmp(path + path_length - 3, ".ii")) ||
        (path_length > 2 && !strcmp(path + path_length - 2, ".i"));
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
    p.translation_unit();

    if (dump_syntax)
        tree.dump(tree.root, tokens, out.get());
    if (dump_declarations) {
        /* translation-unit: declaration-seq_opt */
        const syntax_node &unit = tree[tree.root];
        if (unit.child_count) {
            node_index seq = tree.children(tree.root)[0].index();
            for (uint32_t i = 0; i < tree[seq].child_count; i++) {
                syntax_child c = tree.children(seq)[i];
                if (c.is_token())
                    continue;
                const syntax_node &d = tree[c.index()];
                source_position first = tokens.position(d.first_token);
                vbuffer_format_append(out.get(), "%s %s:%u:%u",
                                      syntax_kind_name(d.kind), first.file_name,
                                      first.line, first.column);
                token_index last = d.first_token;
                if (d.end_token > d.first_token)
                    last = d.end_token - 1;
                source_position end = tokens.position(last);
                vbuffer_format_append(out.get(), " %s:%u:%u\n", end.file_name,
                                      end.line, end.column);
            }
        }
    }
    if (stats) {
        parser_statistics s = p.statistics();
        vbuffer_format_append(
            out.get(),
            "tokens %llu\nnodes %llu\ntentative-parses %llu\nrewinds %llu\n"
            "rewound-tokens %llu\nlookups %llu\nunparsed %llu\n"
            "erroneous %llu\n",
            (unsigned long long)s.tokens, (unsigned long long)s.nodes,
            (unsigned long long)s.tentative_parses,
            (unsigned long long)s.rewinds, (unsigned long long)s.rewound_tokens,
            (unsigned long long)s.lookups, (unsigned long long)s.unparsed,
            (unsigned long long)s.erroneous);
    }
    return finish();
}
