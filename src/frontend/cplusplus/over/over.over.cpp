/*
 * over.over.cpp: [over.over] Address of an overload set.
 *
 * [over.over]/1: "A use of a function name without arguments is resolved
 * to a function, a pointer to function, or a pointer to member function for
 * a specific function that is chosen from a set of selected functions
 * determined based on the target type required in the context (if any), as
 * described below. ... The function name can be preceded by the & operator.
 * [Note 1: Any redundant set of parentheses surrounding the function name is
 * ignored ([expr.prim.paren]).]"  /2: "a non-template function with type F
 * is selected for the function type FT of the target type if F (after
 * possibly applying the function pointer conversion ([conv.fctptr])) is
 * identical to FT."  /4: "Non-member functions and static member functions
 * match targets of function pointer type or reference to function type."
 *
 * The targets analysed: an object or reference initialized, the left side
 * of an assignment, a parameter of a function called, the return value and
 * an explicit type conversion; the names: an unqualified name of an overload
 * set of non-member functions.  There are no templates.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

namespace
{

/* the operand of redundant parentheses */
syntax_child without_parentheses(const syntax_tree &tree, token_stream &tokens,
                                 syntax_child e)
{
    while (!e.is_token() &&
           tree[e.index()].kind == syntax_kind::primary_expression &&
           tree[e.index()].child_count == 3 &&
           syntax_is_punctuator(tokens, tree.children(e.index())[0],
                                preprocessing_op_or_punc::LEFT_PARENTHESIS))
        e = tree.children(e.index())[1];
    return e;
}

} // namespace

entity_index semantic_analysis::named_overload_set(syntax_child e,
                                                   token_index &at) const
{
    syntax_child name = without_parentheses(tree, tokens, e);
    if (!name.is_token() &&
        tree[name.index()].kind == syntax_kind::unary_expression &&
        tree[name.index()].child_count == 2 &&
        syntax_is_punctuator(tokens, tree.children(name.index())[0],
                             preprocessing_op_or_punc::AMPERSAND))
        name =
            without_parentheses(tree, tokens, tree.children(name.index())[1]);
    if (!name.is_token() || tokens[name.index()].kind != token_kind::identifier)
        return 0;
    at = name.index();
    lookup_result found =
        lookup_unqualified(tokens.identifier(at), lookup_filter::any);
    if (!found.entity || found.unknown || found.ambiguous)
        return 0;
    const entity &first = entity_table[found.entity];
    if (first.kind != entity_kind::function || !first.next_overload)
        return 0;
    return found.entity;
}

analysis_result semantic_analysis::target_expression(syntax_child e,
                                                     qualified_type target,
                                                     expression_meaning &m)
{
    /* FT, the function type of the target: a function, a pointer to one
       or a reference to one */
    qualified_type ft = target;
    const struct type &t = type_table[target.unqualified()];
    if (t.kind == type_kind::pointer || t.kind == type_kind::lvalue_reference ||
        t.kind == type_kind::rvalue_reference)
        ft = t.operand;
    if (type_table[ft.unqualified()].kind != type_kind::function)
        return expression(e, m);
    token_index at = 0;
    entity_index named = named_overload_set(e, at);
    if (!named)
        return expression(e, m);
    /* whether &f or f fits the target (&f initializes no reference) is
       the initialization's rules, of the function chosen */
    owned_array<entity_index> set;
    overload_set(named, set);
    entity_index chosen = 0;
    bool close = false;
    for (entity_index f : set) {
        const entity &x = entity_table[f];
        if (x.nonstatic_member)
            /* /4: a pointer to member */
            return not_supported(current_declaration, "over.over");
        if (x.type.unqualified() == ft.unqualified())
            chosen = f;
        else if (same_parameters(type_table, x.type.unqualified(),
                                 ft.unqualified()))
            /* the function pointer conversion ([conv.fctptr]) or a
               return type that differs */
            close = true;
    }
    if (!chosen && close)
        return not_supported(current_declaration, "over.over");
    if (!chosen)
        return ill_formed_at_token(at,
                                   "no function of the overload set has the "
                                   "type of the target",
                                   "over.over");
    /* the name is analysed as the function chosen (named_entity) */
    overload_target_token = at;
    overload_target_choice = chosen;
    analysis_result r = expression(e, m);
    overload_target_token = 0;
    overload_target_choice = 0;
    return r;
}

} // namespace cplusplus
