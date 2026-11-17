/*
 * cxxsema: the C++ front end's semantic analysis (src/frontend/cplusplus,
 * doc/PLAN-cxx-sema.md) on one translation unit.
 *
 *   cxxsema [-fsyntax-only] [OPTIONS] FILE  analyse; diagnostics on stderr
 *   cxxsema -dump-analysis [OPTIONS] FILE   one line per declaration of the
 *                                           translation unit: the node's
 *                                           kind, the line:column of its
 *                                           first and of its last token, and
 *                                           the result: well-formed,
 *                                           ill-formed, not-parsed, or
 *                                           unsupported=[SUBCLAUSE] (rules
 *                                           that are not written yet)
 *   cxxsema -debug-unsupported ...          one line per construct without
 *                                           rules: [SUBCLAUSE] nonterminal
 *                                           FILE:LINE:COLUMN
 *   cxxsema -stats ...                      declarations by result, types,
 *                                           entities
 *   cxxsema -selftest                       the tables no rule uses yet
 *
 * OPTIONS are cxxlex's (src/bin/cxxlex/command-line.h).  The exit status is
 * 1 if a diagnostic was an error, 2 if the file cannot be read; a
 * declaration that is unsupported is no error.
 */
#include "../cxxlex/command-line.h"
#include "frontend/cplusplus/parse.h"
#include "frontend/cplusplus/sema.h"

static bool check(bool condition, const char *what)
{
    if (!condition)
        fprintf(stderr, "cxxsema -selftest: FAILED: %s\n", what);
    return condition;
}

/* the type and entity tables have no rule that uses them before step S1:
   what they promise is checked here */
static int selftest()
{
    bool ok = true;
    {
        types t;
        type_index i = t.fundamental(fundamental_type::int_);
        type_index c = t.fundamental(fundamental_type::char_);
        ok &= check(i && c && i != c, "distinct fundamental types");
        ok &= check(t.fundamental(fundamental_type::int_) == i,
                    "a type made again is the same type");
        qualified_type plain = qualified_type::make(i, cv_none);
        qualified_type constant = qualified_type::make(i, cv_const);
        ok &= check(plain.unqualified() == constant.unqualified() &&
                        !(plain == constant) && constant.cv() == cv_const,
                    "[basic.type.qualifier]/1: cv-qualified versions are "
                    "distinct types of one record");
        type_index p = t.pointer_to(plain), pc = t.pointer_to(constant);
        ok &= check(p != pc && t.pointer_to(plain) == p,
                    "int* and const int* differ");
        ok &=
            check(t.lvalue_reference_to(plain) != t.rvalue_reference_to(plain),
                  "[dcl.ref]/1: the two kinds of reference");
        ok &= check(t.array_of(plain, 3) != t.array_of(plain, 4) &&
                        t.array_of(plain, 3) == t.array_of(plain, 3) &&
                        t.array_of_unknown_bound_of(plain) !=
                            t.array_of(plain, 0),
                    "[dcl.array]/1: arrays by bound");
        qualified_type two[] = {plain, qualified_type::make(c, cv_none)};
        type_index f = t.function(plain, two, 2, false);
        ok &= check(t.function(plain, two, 2, false) == f &&
                        t.function(plain, two, 1, false) != f &&
                        t.function(plain, two, 2, true) != f &&
                        t.function(constant, two, 2, false) != f,
                    "[dcl.fct]/1: functions by parameters and return type");
        ok &= check(t[f].parameter_count == 2 && t.parameters(f)[1] == two[1],
                    "a function type's parameters");
        ok &= check(t.of_entity(type_kind::class_type, 1) !=
                            t.of_entity(type_kind::class_type, 2) &&
                        t.of_entity(type_kind::class_type, 1) !=
                            t.of_entity(type_kind::enumeration, 1),
                    "one type per class and enumeration");
        ok &= check(t.pointer_to_member(1, plain) !=
                        t.pointer_to_member(2, plain),
                    "[dcl.mptr]/1: pointers to members by class");
        /* enough types for the table to grow several times */
        size_t before = t.size();
        qualified_type chain = plain;
        for (int k = 0; k < 1000; k++)
            chain = qualified_type::make(t.pointer_to(chain), cv_none);
        ok &= check(t.size() == before + 999, "p was the first of the chain");
        qualified_type again = plain;
        for (int k = 0; k < 1000; k++)
            again = qualified_type::make(t.pointer_to(again), cv_none);
        ok &= check(again == chain && t.size() == before + 999,
                    "types are found again after the table grew");
    }
    {
        entities e;
        entity_index n = e.make(entity_kind::namespace_entity, 7, 0);
        entity_index x = e.make(entity_kind::variable, 8, n);
        ok &= check(n && x && e[x].enclosing == n && e[x].name == 8,
                    "an entity and its enclosing entity");
        e.add_declaration(x, 11);
        e.add_declaration(n, 5);
        e.add_declaration(x, 12);
        uint32_t first = e[x].first_declaration;
        ok &= check(e.declaration(first).node == 11 &&
                        e.declaration(e.declaration(first).next).node == 12 &&
                        e[x].last_declaration == e.declaration(first).next &&
                        !e.declaration(e[x].last_declaration).next &&
                        e.declaration(e[n].first_declaration).node == 5,
                    "[basic.link]/1: an entity's declarations in order");
    }
    if (ok)
        printf("cxxsema -selftest: ok\n");
    if (ok)
        return 0;
    return 1;
}

int main(int argc, char **argv)
{
    command_line options("cxxsema");
    bool dump_analysis = false, debug_unsupported = false, stats = false;
    const char *path = nullptr;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-selftest"))
            return selftest();
        if (!strcmp(a, "-dump-analysis"))
            dump_analysis = true;
        else if (!strcmp(a, "-debug-unsupported"))
            debug_unsupported = true;
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
        fprintf(stderr, "usage: cxxsema [-fsyntax-only|-dump-analysis|"
                        "-debug-unsupported|-stats] [clang -cc1 options] "
                        "FILE\n       cxxsema -selftest\n");
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
        fprintf(stderr, "cxxsema: cannot read %s\n", path);
        return 2;
    }
    token_stream tokens(pp);
    syntax_tree tree;
    declared_names names;
    parser p(tokens, tree, names, diagnostics);
    p.translation_unit();
    semantic_analysis analysis(tree, tokens, diagnostics);
    analysis.translation_unit();

    owned_buffer out;
    size_t by_result[4] = {};
    for (const analysed_declaration &d : analysis.declarations) {
        by_result[static_cast<size_t>(d.result)]++;
        if (!dump_analysis)
            continue;
        const syntax_node &s = tree[d.node];
        source_position first = tokens.position(s.first_token);
        token_index last = s.first_token;
        if (s.end_token > s.first_token)
            last = s.end_token - 1;
        source_position end = tokens.position(last);
        vbuffer_format_append(out.get(), "%s %s:%u:%u %s:%u:%u %s",
                              syntax_kind_name(s.kind), first.file_name,
                              first.line, first.column, end.file_name, end.line,
                              end.column, analysis_result_name(d.result));
        if (d.result == analysis_result::unsupported)
            vbuffer_format_append(
                out.get(), "=[%s]",
                analysis.unsupported[d.first_unsupported].subclause);
        vbuffer_string_append(out.get(), "\n");
    }
    if (debug_unsupported) {
        for (size_t i = 1; i < analysis.unsupported.size(); i++) {
            const unsupported_construct &u = analysis.unsupported[i];
            source_position at = tokens.position(tree[u.node].first_token);
            vbuffer_format_append(out.get(), "[%s] %s %s:%u:%u\n", u.subclause,
                                  syntax_kind_name(tree[u.node].kind),
                                  at.file_name, at.line, at.column);
        }
    }
    if (stats) {
        vbuffer_format_append(
            out.get(),
            "declarations %zu\nwell-formed %zu\nill-formed %zu\n"
            "unsupported %zu\nnot-parsed %zu\ntypes %zu\nentities %zu\n",
            analysis.declarations.size(),
            by_result[static_cast<size_t>(analysis_result::well_formed)],
            by_result[static_cast<size_t>(analysis_result::ill_formed)],
            by_result[static_cast<size_t>(analysis_result::unsupported)],
            by_result[static_cast<size_t>(analysis_result::not_parsed)],
            analysis.type_table.size() - 1, analysis.entity_table.size() - 1);
    }
    fwrite(out.data(), 1, out.size(), stdout);
    owned_buffer messages;
    for (const diagnostic_message &m : diagnostics.messages)
        diagnostics.render(m, messages.get());
    fwrite(messages.data(), 1, messages.size(), stderr);
    if (diagnostics.error_count)
        return 1;
    return 0;
}
