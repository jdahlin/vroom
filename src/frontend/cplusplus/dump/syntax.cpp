/*
 * syntax.cpp: the developer outputs of the parser (dump.h): syntax,
 * expression-claims, declarator-claims, declarations and parser-stats,
 * which bench/cxx-parse.py checks with clang; and the parser's selftest.
 */
#include <string.h>

#include "../cplusplus-internal.h"
#include "dump.h"

namespace cplusplus
{

#include "declarator-claims.h"

static bool parsing_check(vbuffer *out, bool condition, const char *what)
{
    if (!condition)
        vbuffer_format_append(out, "selftest: FAILED: %s\n", what);
    return condition;
}

/*
 * declared_names and the tree's truncation have no grammar that uses them
 * before step P1: what they promise is checked here.
 */
bool dump_parsing_selftest(vbuffer *out)
{
    bool ok = true;
    {
        declared_names names;
        const uint32_t N = 1, T = 2, x = 3, M = 4;
        ok &= parsing_check(
            out, names.lookup_unqualified(T) == name_kind::not_declared,
            "an undeclared name");
        names.declare(T, name_kind::type_name);
        scope_index n = names.enter_namespace(N, false);
        ok &= parsing_check(out,
                            names.lookup_unqualified(T) == name_kind::type_name,
                            "[basic.lookup.unqual]/1: the enclosing namespace");
        names.declare(T, name_kind::other);
        names.declare(x, name_kind::other);
        ok &=
            parsing_check(out, names.lookup_unqualified(T) == name_kind::other,
                          "the innermost declaration is found first");
        scope_index m = names.enter_namespace(M, false);
        names.leave();
        names.enter(scope_kind::block);
        names.declare(x, name_kind::type_name);
        ok &= parsing_check(out,
                            names.lookup_unqualified(x) == name_kind::type_name,
                            "a block's declaration");
        names.leave();
        ok &=
            parsing_check(out, names.lookup_unqualified(x) == name_kind::other,
                          "leaving a block ends its declarations");
        names.leave();
        ok &= parsing_check(out,
                            names.current() == declared_names::global_namespace,
                            "back in the global namespace");
        ok &= parsing_check(out,
                            names.lookup_unqualified(T) == name_kind::type_name,
                            "the global declaration again");
        ok &= parsing_check(
            out, names.lookup_unqualified(x) == name_kind::not_declared,
            "a namespace's member is not found outside it");
        scope_index found = 0;
        ok &= parsing_check(
            out,
            names.lookup_qualified(declared_names::global_namespace, N,
                                   &found) == name_kind::namespace_name &&
                found == n,
            "[basic.lookup.qual]/1: a namespace-name and its region");
        ok &=
            parsing_check(out, names.lookup_qualified(n, x) == name_kind::other,
                          "a qualified member");
        ok &= parsing_check(out,
                            names.lookup_qualified(n, M, &found) ==
                                    name_kind::namespace_name &&
                                found == m,
                            "a nested namespace");
        ok &= parsing_check(out, names.enter_namespace(N, false) == n,
                            "[namespace.def]/2: the namespace is extended");
        ok &= parsing_check(
            out, names.lookup_unqualified(x) == name_kind::other,
            "the extended namespace has the first part's members");
        names.leave();
        ok &= parsing_check(
            out,
            names.lookups.size() == 11 &&
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
        ok &= parsing_check(out,
                            tree[seq].child_count == 3 &&
                                tree.children(seq)[1].index() == b &&
                                !tree.children(seq)[1].is_token() &&
                                tree.children(seq)[2].is_token() &&
                                tree.children(seq)[2].index() == 3,
                            "a node's children");
        ok &= parsing_check(out, tree.count(syntax_kind::unparsed) == 2,
                            "nodes are counted by kind");
        tree.truncate(size);
        ok &= parsing_check(out,
                            tree.size() == size &&
                                tree.count(syntax_kind::unparsed) == 1 &&
                                tree.count(syntax_kind::declaration_seq) == 0,
                            "truncate takes back the nodes of a failed parse");
        node_index c = tree.make(syntax_kind::declaration_seq, both, 1, 0, 2);
        ok &= parsing_check(out,
                            c == b && tree[c].first_child == 0 &&
                                tree.children(c)[0].index() == a,
                            "the tree is built on after a truncation");
    }
    return ok;
}

/* the tokens [first, end) as they are spelled, a space after each */
static void dump_spelled_tokens(token_stream &tokens, token_index first,
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
static void dump_parenthesized(const syntax_tree &tree, token_stream &tokens,
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
            dump_spelled_tokens(tokens, s.first_token, s.end_token, out);
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
            dump_spelled_tokens(tokens, c.index(), c.index() + 1, out);
        else
            enter(c.index());
    }
}

/*
 * expression-claims: for each expression that is not part of another (and
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
static void dump_expression_claims(const syntax_tree &tree,
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
        dump_spelled_tokens(tokens, s.first_token, s.end_token, written.get());
        dump_parenthesized(tree, tokens, n, parenthesized.get());
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
            dump_expression_claims(tree, tokens, c.index(), declaration,
                                   s.kind == syntax_kind::declaration_seq, out);
    }
}

void dump_parsing(const dump_request &r, const syntax_tree &tree,
                  token_stream &tokens, parser &p, vbuffer *out)
{
    if (r.has(dump_output::syntax))
        tree.dump(tree.root, tokens, out);
    if (r.has(dump_output::expression_claims))
        dump_expression_claims(tree, tokens, tree.root, 0, false, out);
    if (r.has(dump_output::declarator_claims))
        declarator_claims(tree, tokens, p, out).write(tree.root);
    if (r.has(dump_output::declarations)) {
        /* translation-unit: declaration-seq_opt; per declaration, whether
           it has an erroneous node, and whether a declaration in it was
           kept as tokens because it uses a later step's grammar (an
           unparsed node that is a whole declaration: a child of a
           declaration-seq or a linkage-specification) */
        /* a module unit's declarations: those of its global module
           fragment, its module-declaration, its purview and its private
           module fragment, in order */
        owned_array<node_index> declarations;
        tree.declarations(&declarations);
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
                        if (opaque.size())
                            vbuffer_string_append(opaque.get(), ",");
                        vbuffer_format_append(opaque.get(), "%u:%u-%u:%u",
                                              a.line, a.column, b.line,
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
                vbuffer_format_append(out, "%s %s:%u:%u",
                                      syntax_kind_name(d.kind), first.file_name,
                                      first.line, first.column);
                token_index last = d.first_token;
                if (d.end_token > d.first_token)
                    last = d.end_token - 1;
                source_position end = tokens.position(last);
                vbuffer_format_append(out, " %s:%u:%u", end.file_name, end.line,
                                      end.column);
                if (erroneous)
                    vbuffer_string_append(out, " erroneous");
                if (fallback)
                    vbuffer_string_append(out, " fallback");
                if (opaque.size()) {
                    vbuffer_string_append(out, " opaque=");
                    vbuffer_append(out, opaque.data(), opaque.size());
                }
                vbuffer_string_append(out, "\n");
            }
        }
    }
    if (r.has(dump_output::parser_stats)) {
        parser_statistics s = p.statistics();
        vbuffer_format_append(
            out,
            "tokens %llu\nnodes %llu\ntentative-parses %llu\nrewinds %llu\n"
            "rewound-tokens %llu\nlookups %llu\nunparsed %llu\n"
            "erroneous %llu\n",
            (unsigned long long)s.tokens, (unsigned long long)s.nodes,
            (unsigned long long)s.tentative_parses,
            (unsigned long long)s.rewinds, (unsigned long long)s.rewound_tokens,
            (unsigned long long)s.lookups, (unsigned long long)s.unparsed,
            (unsigned long long)s.erroneous);
    }
}

} // namespace cplusplus
