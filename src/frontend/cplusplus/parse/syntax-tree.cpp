/*
 * syntax-tree.cpp: the syntax tree of a translation unit.
 *
 * A node is a nonterminal of the grammar and the alternative that matched:
 * its children are that alternative's nonterminals and tokens in order
 * (doc/PLAN-cxx-parse.md).  The nodes and the child lists are two arrays;
 * a node refers to its children as a range of the second, so the tree is
 * two blocks of memory with no pointers in them.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

namespace
{

struct syntax_kind_description {
    const char *nonterminal, *subclause;
};

constexpr syntax_kind_description syntax_kinds[] = {
#define SYNTAX_NODE(name, nonterminal, subclause) {nonterminal, subclause},
#include "syntax-kinds.inc"
#undef SYNTAX_NODE
};

} // namespace

const char *syntax_kind_name(syntax_kind kind)
{
    return syntax_kinds[static_cast<size_t>(kind)].nonterminal;
}

const char *syntax_kind_subclause(syntax_kind kind)
{
    return syntax_kinds[static_cast<size_t>(kind)].subclause;
}

bool syntax_kind_is_expression(syntax_kind kind)
{
    switch (kind) {
    case syntax_kind::primary_expression:
    case syntax_kind::fold_expression:
    case syntax_kind::requires_expression:
    case syntax_kind::qualified_id:
    case syntax_kind::postfix_expression:
    case syntax_kind::unary_expression:
    case syntax_kind::noexcept_expression:
    case syntax_kind::new_expression:
    case syntax_kind::delete_expression:
    case syntax_kind::cast_expression:
    case syntax_kind::pm_expression:
    case syntax_kind::multiplicative_expression:
    case syntax_kind::additive_expression:
    case syntax_kind::shift_expression:
    case syntax_kind::compare_expression:
    case syntax_kind::relational_expression:
    case syntax_kind::equality_expression:
    case syntax_kind::and_expression:
    case syntax_kind::exclusive_or_expression:
    case syntax_kind::inclusive_or_expression:
    case syntax_kind::logical_and_expression:
    case syntax_kind::logical_or_expression:
    case syntax_kind::conditional_expression:
    case syntax_kind::assignment_expression:
    case syntax_kind::expression:
    case syntax_kind::gnu_extension_expression:
    case syntax_kind::gnu_compound_literal:
    case syntax_kind::gnu_label_address:
    case syntax_kind::clang_type_trait_expression:
    case syntax_kind::gnu_offsetof_expression:
    case syntax_kind::gnu_va_arg_expression:
    case syntax_kind::clang_bit_cast_expression:
    case syntax_kind::clang_convertvector_expression:
    case syntax_kind::clang_source_location_expression:
        return true;
    default:
        return false;
    }
}

syntax_tree::syntax_tree()
{
    memset(kind_counts, 0, sizeof(kind_counts));
    /* node 0 is no node */
    nodes.append({syntax_kind::count, 0, 0, 0, 0});
}

node_index syntax_tree::make(syntax_kind kind, const syntax_child *children,
                             size_t child_count, token_index first_token,
                             token_index end_token)
{
    syntax_node n = {kind, static_cast<uint32_t>(child_lists.size()),
                     static_cast<uint32_t>(child_count), first_token,
                     end_token};
    for (size_t i = 0; i < child_count; i++)
        child_lists.append(children[i]);
    nodes.append(n);
    kind_counts[static_cast<size_t>(kind)]++;
    return static_cast<node_index>(nodes.size() - 1);
}

bool syntax_tree::is_split(token_index t) const
{
    for (token_index s : split_tokens)
        if (s == t)
            return true;
    return false;
}

void syntax_tree::truncate(size_t node_count)
{
    if (node_count >= nodes.size())
        return;
    for (size_t i = node_count; i < nodes.size(); i++)
        kind_counts[static_cast<size_t>(nodes[i].kind)]--;
    /* a node's children come before the next node's */
    child_lists.resize(nodes[node_count].first_child);
    nodes.resize(node_count);
}

/*
 *     translation-unit 1:1
 *       declaration-seq 1:1
 *         (unparsed) 1:1 'int' 'x' ';'
 *
 * A nonterminal and the line and column of its first token; a token child
 * as its spelling in quotes on a line of its own; the tokens of a
 * scaffolding node, which has no children, after it on its line.
 */
void syntax_tree::dump(node_index n, token_stream &tokens, vbuffer *out) const
{
    struct level {
        node_index node;
        uint32_t next_child;
    };
    owned_array<level> path;
    path.append({n, 0});
    auto write_spelling = [&](token_index t) {
        vbuffer_append(out, "'", 1);
        tokens.spelling(t, out);
        vbuffer_append(out, "'", 1);
    };
    /* a split token ([temp.names]/3) is written as what each child is: the
       first '>', then the rest */
    token_index last_split = 0;
    bool in_split = false;
    auto write_child_token = [&](token_index t) {
        if (!is_split(t)) {
            write_spelling(t);
            return;
        }
        if (!in_split || last_split != t) {
            vbuffer_string_append(out, "'>'");
            in_split = true;
            last_split = t;
            return;
        }
        owned_buffer spelling;
        tokens.spelling(t, spelling.get());
        vbuffer_append(out, "'", 1);
        vbuffer_append(out, spelling.data() + 1, spelling.size() - 1);
        vbuffer_append(out, "'", 1);
        in_split = false;
    };
    auto write_line = [&](node_index node, size_t depth) {
        const syntax_node &s = nodes[node.index()];
        for (size_t i = 0; i < depth; i++)
            vbuffer_append(out, "  ", 2);
        vbuffer_string_append(out, syntax_kind_name(s.kind));
        if (s.first_token < s.end_token) {
            source_position p = tokens.position(s.first_token);
            vbuffer_format_append(out, " %u:%u", p.line, p.column);
        }
        if (s.kind == syntax_kind::unparsed || s.kind == syntax_kind::erroneous)
            for (token_index t = s.first_token; t < s.end_token; t++) {
                vbuffer_append(out, " ", 1);
                write_spelling(t);
            }
        vbuffer_append(out, "\n", 1);
    };
    if (!n)
        return;
    write_line(n, 0);
    while (!path.empty()) {
        level &top = path.back();
        const syntax_node &s = nodes[top.node.index()];
        if (top.next_child == s.child_count) {
            path.resize(path.size() - 1);
            continue;
        }
        syntax_child c = child_lists[s.first_child + top.next_child++];
        if (c.is_token()) {
            for (size_t i = 0; i < path.size(); i++)
                vbuffer_append(out, "  ", 2);
            write_child_token(c.index());
            vbuffer_append(out, "\n", 1);
        } else {
            write_line(c.node(), path.size());
            path.append({c.node(), 0});
        }
    }
}

void syntax_tree::declarations(owned_array<node_index> *out) const
{
    auto add_seq = [&](node_index seq) {
        for (uint32_t i = 0; i < nodes[seq.index()].child_count; i++) {
            syntax_child c = children(seq)[i];
            if (!c.is_token())
                out->append(c.node());
        }
    };
    const syntax_node &unit = nodes[root.index()];
    for (uint32_t u = 0; u < unit.child_count; u++) {
        syntax_child part = children(root)[u];
        if (part.is_token())
            continue;
        const syntax_node &x = nodes[part.index()];
        if (x.kind == syntax_kind::declaration_seq) {
            add_seq(part.node());
        } else if (x.kind == syntax_kind::global_module_fragment ||
                   x.kind == syntax_kind::private_module_fragment) {
            for (uint32_t k = 0; k < x.child_count; k++) {
                syntax_child y = children(part.node())[k];
                if (y.is_token())
                    continue;
                if (nodes[y.index()].kind == syntax_kind::declaration_seq)
                    add_seq(y.node());
                else
                    out->append(y.node());
            }
        } else {
            out->append(part.node());
        }
    }
}

} // namespace cplusplus
