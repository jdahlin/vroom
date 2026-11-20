/*
 * semantic-tree.cpp: the semantic tree of a translation unit
 * (doc/PLAN-cxx-sema.md 3.2).
 *
 * Analysis builds it as it checks: each expression's node is made where
 * its meaning is, and each conversion analysis applies to an operand wraps
 * the operand's node in a conversion node, so the tree says every implicit
 * step the standard describes ([conv]/1: "Standard conversions are
 * implicit conversions with built-in meaning").  Nodes of a declaration
 * that turned out unsupported stay in the table, unused.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

semantic_tree::semantic_tree()
{
    /* node 0 and list index 0 are none */
    nodes.append(semantic_node());
    lists.append(0);
}

const char *semantic_kind_name(semantic_kind kind)
{
    static const char *const names[] = {
        "none",
        "constant",
        "string_literal",
        "entity",
        "conversion",
        "unary",
        "address",
        "indirection",
        "increment",
        "binary",
        "logical_and",
        "logical_or",
        "assignment",
        "conditional",
        "comma",
        "call",
        "subscript",
        "member",
        "bound_member_function",
        "member_pointer",
        "member_pointer_access",
        "zero",
        "initializer_list",
        "bind_reference",
        "construct",
        "destroy",
        "new_object",
        "allocated",
        "array_count",
        "array_size",
        "delete_object",
        "throw_exception",
        "type_identification",
        "vtable_pointer_store",
        "block",
        "expression_statement",
        "declaration_statement",
        "declarations",
        "if_statement",
        "while_statement",
        "do_statement",
        "for_statement",
        "switch_statement",
        "case_label",
        "default_label",
        "identifier_label",
        "goto_statement",
        "break_statement",
        "continue_statement",
        "return_statement",
        "null_statement",
        "condition_declaration",
        "function_body",
    };
    size_t k = static_cast<size_t>(kind);
    if (k >= sizeof names / sizeof names[0])
        return "?";
    return names[k];
}

uint32_t semantic_tree::add(const semantic_node &n)
{
    uint32_t index = static_cast<uint32_t>(nodes.size());
    nodes.append(n);
    return index;
}

uint32_t semantic_tree::list(const uint32_t *items, size_t count)
{
    uint32_t first = static_cast<uint32_t>(lists.size());
    for (size_t i = 0; i < count; i++)
        lists.append(items[i]);
    return first;
}

uint32_t semantic_analysis::expression_node(semantic_kind kind,
                                            const expression_meaning &m,
                                            token_index at, uint32_t a,
                                            uint32_t b, uint8_t op)
{
    semantic_node n;
    n.kind = kind;
    n.op = op;
    n.category = m.category;
    n.evaluation = m.evaluation;
    n.type = m.type;
    n.at = at;
    n.a = a;
    n.b = b;
    n.value = m.value;
    return semantics.add(n);
}

void semantic_analysis::converted(expression_meaning &m, conversion_kind kind)
{
    /* the value of another type is no bit-field's */
    if (kind != conversion_kind::lvalue_to_rvalue)
        m.bit_field_width = 0;
    if (!m.node)
        return;
    m.node = expression_node(semantic_kind::conversion, m, semantics[m.node].at,
                             m.node, 0, static_cast<uint8_t>(kind));
}

uint32_t semantic_analysis::statement_node(semantic_kind kind, token_index at,
                                           uint32_t a, uint32_t b, uint32_t c,
                                           uint32_t d)
{
    semantic_node n;
    n.kind = kind;
    n.at = at;
    n.a = a;
    n.b = b;
    n.c = c;
    n.d = d;
    return semantics.add(n);
}

token_index semantic_analysis::first_token(SyntaxElement c) const
{
    if (c.is_token())
        return c.token();
    return c.first_token();
}

} // namespace cplusplus
