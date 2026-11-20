/*
 * call-candidates.cpp: [over.match.call] Function call syntax, with
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
token_index semantic_analysis::callee_name(SyntaxElement c) const
{
    if (c.is_token()) {
        if (c.is_identifier())
            return c.token();
        return 0;
    }
    if (c.is(syntax_kind::qualified_id) && c.size() == 2 && c[1].is_token() &&
        c[1].is_identifier())
        return c[1].token();
    if (c.is(syntax_kind::postfix_expression) && c.size() == 3 &&
        (c[1].is(preprocessing_op_or_punc::DOT,
                 preprocessing_op_or_punc::ARROW)) &&
        c[2].is_identifier())
        return c[2].token();
    /* x.f<T> and x.template f<T> */
    if (c.is(syntax_kind::postfix_expression) && c.size() >= 3 &&
        (c[1].is(preprocessing_op_or_punc::DOT,
                 preprocessing_op_or_punc::ARROW))) {
        SyntaxElement id = c[c.size() - 1];
        if ((c.size() == 3 ||
             (c.size() == 4 && c[2].is(standard_keyword::template_))) &&
            id.is(syntax_kind::simple_template_id) && id[0].is_identifier())
            return id[0].token();
    }
    if (c.is(syntax_kind::postfix_expression) && c.size() == 3 &&
        (c[1].is(preprocessing_op_or_punc::DOT,
                 preprocessing_op_or_punc::ARROW)) &&
        c[2].operator_function_id())
        return c[2].operator_function_id().operator_keyword().token();
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
semantic_analysis::resolve_call(SyntaxRef n,
                                const owned_array<SyntaxElement> &arguments,
                                owned_array<expression_meaning> &early,
                                const owned_array<entity_index> &set,
                                uint32_t object, expression_meaning &f)
{
    /* the use of the name lookup resolved: it is claimed to find the
       function chosen (what clang's referencedDecl is), none until then */
    /* f's node, fetched at each use: analysing the arguments and
       choosing the function add nodes, which may move semantics' */
    auto callee = [&]() -> semantic_node & { return semantics[f.node]; };
    size_t use = name_uses.size();
    for (size_t i = name_uses.size(); i-- > 0 && use == name_uses.size();)
        if (name_uses[i].at == callee().at)
            use = i;
    for (entity_index e : set)
        if (use < name_uses.size() && name_uses[use].entity == e)
            name_uses[use].entity = 0;
    /* [over.ics.list]/10: "Otherwise, if the parameter type is not a class:
       if the initializer list has one element that is not itself an
       initializer list, the implicit conversion sequence is the one
       required to convert the element to the parameter type": the
       element stands for the list (the call list-initializes the
       parameter); other lists, and lists for a parameter of class,
       reference or array type, are not analysed */
    owned_array<SyntaxElement> lists;
    for (size_t i = 0; i < arguments.size(); i++) {
        if (!arguments[i].is(syntax_kind::braced_init_list))
            continue;
        owned_array<SyntaxElement> clauses;
        VTRY(r, list_clauses(arguments[i], clauses));
        if (clauses.size() != 1 || clauses[0].is(syntax_kind::braced_init_list))
            return not_supported(n, "over.ics.list");
        for (entity_index e : set) {
            const struct type &ft =
                type_table[entity_table[e].type.unqualified()];
            if (i >= ft.parameter_count)
                continue;
            qualified_type p =
                type_table.parameters(entity_table[e].type.unqualified())[i];
            type_kind k = type_table[p.unqualified()].kind;
            if (k != type_kind::pointer && k != type_kind::enumeration &&
                (k != type_kind::fundamental ||
                 fundamental_of(p) == fundamental_type::void_))
                return not_supported(n, "over.ics.list");
        }
        lists.append(clauses[0]);
    }
    for (size_t i = 0; i < early.size(); i++)
        if (!early[i].node && !early[i].overload_name &&
            !arguments[i].is(syntax_kind::braced_init_list))
            return not_supported(n, "over.over");
    for (size_t i = early.size(); i < arguments.size(); i++) {
        expression_meaning e;
        /* [over.over]/1: an overload set's name as an argument stays the
           set; best_viable_function converts it for each candidate's
           parameter (overload_set_sequence) and call_with resolves it */
        bool is_set = overload_set_operand(arguments[i], e);
        if (!is_set && !arguments[i].is(syntax_kind::braced_init_list)) {
            VTRY(r, passed_expression(arguments[i], e));
        }
        early.append(e);
    }
    size_t list = 0;
    for (size_t i = 0; i < arguments.size(); i++) {
        if (!arguments[i].is(syntax_kind::braced_init_list))
            continue;
        VTRY(r, expression(lists[list++], early[i]));
    }
    expression_meaning implied;
    if (object) {
        /* the implied object argument, the object expression the address
           is of: its category selects among ref-qualified overloads
           ([over.match.funcs]/4-5) */
        implied.type = type_table[semantics[object].type.unqualified()].operand;
        implied.category = value_category::lvalue;
        if (semantics[object].kind == semantic_kind::address)
            implied.category = semantics[semantics[object].a].category;
    }
    owned_array<overload_candidate> candidates;
    for (entity_index e : set) {
        /* [over.match.funcs]/2: "the set of candidate functions can contain
           both member and non-member functions to be resolved against the
           same argument list"; /4: "For static member functions, the
           implicit object parameter is considered to match any object"
           (object_sequence); a call without an object is made of the
           non-static ones' with no implied argument too, and the chosen one is
           refused below ([class.mfct.non-static]/3) */
        overload_candidate c;
        c.function = e;
        c.type = entity_table[e].type;
        candidates.append(c);
    }
    size_t chosen = 0;
    const expression_meaning *implied_object = nullptr;
    if (object)
        implied_object = &implied;
    VTRY(r, best_viable_function(n, candidates, implied_object, early, chosen));
    entity_index c = set[chosen];
    if (entity_table[c].nonstatic_member != (object != 0))
        /* a static member function chosen for a call with an object
           (called without it), or a non-static one without an object
           ([class.mfct.non-static]/3, ill-formed): not analysed */
        return not_supported(n, "over.match.funcs");
    /* a copy: deleted_use may define an implicit member, adding entities
       that may move entity_table's */
    const entity x = entity_table[c];
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
    VTRY_SET(r, deleted_use(callee().at, c));
    if (use < name_uses.size() && !name_uses[use].entity)
        name_uses[use].entity = c;
    /* the expression the name is, as analysed: its type and entity are
       the function's chosen */
    for (size_t i = expressions.size(); i-- > 0;) {
        if (expressions[i].meaning.node != f.node)
            continue;
        expressions[i].meaning.type = x.type;
        if (callee().kind != semantic_kind::bound_member_function)
            expressions[i].meaning.entity = c;
        break;
    }
    callee().type = x.type;
    if (callee().kind == semantic_kind::bound_member_function) {
        callee().b = c;
    } else {
        callee().a = c;
        f.entity = c;
    }
    f.type = x.type;
    return analysis_result::well_formed;
}

} // namespace cplusplus
