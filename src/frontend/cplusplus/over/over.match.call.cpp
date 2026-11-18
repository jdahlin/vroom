/*
 * over.match.call.cpp: [over.match.call] Function call syntax, with
 * [over.call.func] Call to named function.
 *
 * [over.match.call]/1: "In a function call
 *     postfix-expression ( expression-list_opt )
 * if the postfix-expression names at least one function or function
 * template, overload resolution is applied as specified in
 * [over.call.func]."  [over.call.func]/1: "Of interest in [over.call.func]
 * are only those function calls in which the postfix-expression ultimately
 * contains an id-expression that denotes one or more functions.  Such a
 * postfix-expression, perhaps nested arbitrarily deep in parentheses, has
 * one of the following forms:
 *     postfix-expression . id-expression
 *     postfix-expression -> id-expression
 *     primary-expression
 * These represent two syntactic subcategories of function calls: qualified
 * function calls and unqualified function calls."
 *
 * The name in parentheses is not analysed: overload resolution applies to
 * the name directly the postfix-expression (callee_name).
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* the token of the name the postfix-expression of a call is, 0 if it is
   none of [over.call.func]/1's forms */
token_index semantic_analysis::callee_name(syntax_child c) const
{
    if (c.is_token()) {
        if (tokens[c.index()].kind == token_kind::identifier)
            return c.index();
        return 0;
    }
    const syntax_node &node = tree[c.index()];
    const syntax_child *children = tree.children(c.index());
    if (node.kind == syntax_kind::qualified_id && node.child_count == 2 &&
        children[1].is_token() &&
        tokens[children[1].index()].kind == token_kind::identifier)
        return children[1].index();
    if (node.kind == syntax_kind::postfix_expression && node.child_count == 3 &&
        (syntax_is_punctuator(tokens, children[1],
                              preprocessing_op_or_punc::DOT) ||
         syntax_is_punctuator(tokens, children[1],
                              preprocessing_op_or_punc::ARROW)) &&
        children[2].is_token() &&
        tokens[children[2].index()].kind == token_kind::identifier)
        return children[2].index();
    return 0;
}

/*
 * [over.call.func]/2: "In qualified function calls, the function is named by
 * an id-expression preceded by an -> or . operator. ... the construct A->B
 * is rewritten as (*A).B.  ... the function declarations found by that
 * lookup constitute the set of candidate functions.  The argument list is
 * the expression-list in the call augmented by the addition of the implied
 * object argument as in a qualified function call."  /3: "In unqualified
 * function calls, the function is named by a primary-expression.  The
 * function declarations found by name lookup ([basic.lookup]) constitute the
 * set of candidate functions. ... If the keyword this is in scope and refers
 * to class T, or a derived class of T, then the implied object argument is
 * (*this)."
 *
 * The callee f (an entity node, or a bound member function whose object's
 * address is object) is made the best viable function of the set; the
 * arguments are analysed into early.
 */
analysis_result
semantic_analysis::resolve_call(node_index n,
                                const owned_array<syntax_child> &arguments,
                                owned_array<expression_meaning> &early,
                                const owned_array<entity_index> &set,
                                uint32_t object, expression_meaning &f)
{
    /* the use of the name lookup resolved: it is claimed to find the
       function chosen (what clang's referencedDecl is), none until then */
    semantic_node &callee = semantics[f.node];
    size_t use = name_uses.size();
    for (size_t i = name_uses.size(); i-- > 0 && use == name_uses.size();)
        if (name_uses[i].at == callee.at)
            use = i;
    for (entity_index e : set)
        if (use < name_uses.size() && name_uses[use].entity == e)
            name_uses[use].entity = 0;
    for (size_t i = 0; i < arguments.size(); i++)
        if (syntax_is(tree, arguments[i], syntax_kind::braced_init_list))
            /* [over.ics.list]: list-initialization sequences */
            return not_supported(n, "over.ics.list");
    for (size_t i = early.size(); i < arguments.size(); i++) {
        expression_meaning e;
        analysis_result r = expression(arguments[i], e);
        if (r != analysis_result::well_formed)
            return r;
        early.append(e);
    }
    expression_meaning implied;
    if (object) {
        /* the implied object argument, the lvalue the address is of */
        implied.type = type_table[semantics[object].type.unqualified()].operand;
        implied.category = value_category::lvalue;
    }
    owned_array<overload_candidate> candidates;
    for (entity_index e : set) {
        /* [over.match.funcs]/2: "the set of candidate functions can contain
           both member and non-member functions to be resolved against the
           same argument list": a static member function among non-static
           ones is called without its object, not analysed */
        if (entity_table[e].nonstatic_member != (object != 0))
            return not_supported(n, "over.match.funcs");
        overload_candidate c;
        c.function = e;
        c.type = entity_table[e].type;
        candidates.append(c);
    }
    size_t chosen = 0;
    const expression_meaning *implied_object = nullptr;
    if (object)
        implied_object = &implied;
    analysis_result r =
        best_viable_function(n, candidates, implied_object, early, chosen);
    if (r != analysis_result::well_formed)
        return r;
    entity_index c = set[chosen];
    const entity &x = entity_table[c];
    entity_index object_class = 0;
    if (object)
        object_class = type_table[implied.type.unqualified()].entity;
    if (x.enclosing &&
        entity_table[x.enclosing].kind == entity_kind::class_type &&
        !accessible(c, object_class))
        /* [class.access]/4: "access control is applied to the function
           selected by overload resolution" */
        return ill_formed(n,
                          "a private or protected member's name can be used "
                          "only by members and friends of the class in which "
                          "it is declared",
                          "class.access");
    /* [dcl.fct.def.delete]/2: "If a function is overloaded, it is
       referenced only if the function is selected by overload
       resolution." */
    r = deleted_use(callee.at, c);
    if (r != analysis_result::well_formed)
        return r;
    if (use < name_uses.size() && !name_uses[use].entity)
        name_uses[use].entity = c;
    /* the expression the name is, as analysed: its type and entity are
       the function's chosen */
    for (size_t i = expressions.size(); i-- > 0;) {
        if (expressions[i].meaning.node != f.node)
            continue;
        expressions[i].meaning.type = x.type;
        if (callee.kind != semantic_kind::bound_member_function)
            expressions[i].meaning.entity = c;
        break;
    }
    callee.type = x.type;
    if (callee.kind == semantic_kind::bound_member_function) {
        callee.b = c;
    } else {
        callee.a = c;
        f.entity = c;
    }
    f.type = x.type;
    return analysis_result::well_formed;
}

} // namespace cplusplus
