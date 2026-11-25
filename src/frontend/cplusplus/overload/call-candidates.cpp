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

/*
 * [over.match.funcs]/2: the functions as candidates of one overload
 * resolution; member: each takes the implied object argument first;
 * first_arguments (parallel to candidates, as template_candidates fills it):
 * 0, a function has no template arguments.
 */
void semantic_analysis::function_candidates(
    const owned_array<entity_index> &functions, bool member,
    owned_array<overload_candidate> &candidates,
    owned_array<uint32_t> *first_arguments) const
{
    for (entity_index f : functions) {
        overload_candidate c;
        c.function = f;
        c.type = entity_table[f].type;
        c.member = member;
        candidates.append(c);
        if (first_arguments)
            first_arguments->append(0);
    }
}

/* the token of the name the postfix-expression of a call is, 0 if it is
   none of [over.call.func]/1's forms */
token_index semantic_analysis::callee_name(SyntaxElement c) const
{
    if (c.is_token()) {
        if (c.is_identifier())
            return c.token();
        return 0;
    }
    if (c.is(syntax_kind::operator_function_id))
        return c.first_token();
    if (c.is(syntax_kind::qualified_id) && c.size() >= 2) {
        SyntaxElement rhs = c.back();
        if (rhs.is_token() && rhs.is_identifier())
            return rhs.token();
        if (SyntaxElement name = rhs.simple_template_id().template_name())
            return name.token();
        if (rhs.is(syntax_kind::operator_function_id))
            return rhs.first_token();
    }
    if (c.is(syntax_kind::postfix_expression) && c.size() == 3 &&
        (c[1].is(preprocessing_op_or_punc::DOT,
                 preprocessing_op_or_punc::ARROW))) {
        if (c[2].is_identifier())
            return c[2].token();
        if (c[2].is(syntax_kind::qualified_id))
            return callee_name(c[2]);
    }
    /* x.f<T> and x.template f<T> */
    if (c.is(syntax_kind::postfix_expression) && c.size() >= 3 &&
        (c[1].is(preprocessing_op_or_punc::DOT,
                 preprocessing_op_or_punc::ARROW))) {
        SyntaxElement id = c[c.size() - 1];
        if ((c.size() == 3 ||
             (c.size() == 4 && c[2].is(standard_keyword::template_))) &&
            id.simple_template_id().template_name())
            return id.simple_template_id().template_name().token();
        if ((c.size() == 3 ||
             (c.size() == 4 && c[2].is(standard_keyword::template_))) &&
            id.is(syntax_kind::template_id) && id[0].operator_function_id())
            return id[0].operator_function_id().operator_keyword().token();
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
    token_index callee_at = callee().at;
    size_t use_count = name_uses.size();
    size_t use = use_count;
    const name_use *uses = name_uses.data();
    for (size_t i = use_count; i-- > 0;)
        if (uses[i].at == callee_at) {
            use = i;
            break;
        }
    if (use < use_count)
        for (entity_index e : set)
            if (uses[use].entity == e) {
                name_uses[use].entity = 0;
                break;
            }
    /* [over.ics.list]/10: scalar parameters convert through the sole list
       element; class parameters retain their braced initializer. */
    for (size_t i = 0; i < early.size(); i++)
        if (!early[i].node && !early[i].overload_name &&
            !arguments[i].is(syntax_kind::braced_init_list))
            return not_supported_because(
                n, "over.over",
                "a call argument with no value that is not a braced-init-list");
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
    for (size_t i = 0; i < arguments.size(); i++) {
        if (!arguments[i].is(syntax_kind::braced_init_list))
            continue;
        bool has_scalar_parameter = false;
        for (entity_index e : set) {
            const struct type &ft =
                type_table[entity_table[e].type.unqualified()];
            if (i >= ft.parameter_count)
                continue;
            qualified_type parameter =
                type_table.parameters(entity_table[e].type.unqualified())[i];
            type_kind k = type_table[parameter.unqualified()].kind;
            has_scalar_parameter |=
                k == type_kind::pointer || k == type_kind::enumeration ||
                (k == type_kind::fundamental &&
                 fundamental_of(parameter) != fundamental_type::void_);
        }
        if (!has_scalar_parameter)
            continue;
        owned_array<SyntaxElement> clauses;
        VTRY(listed, list_clauses(arguments[i], clauses));
        if (clauses.size() != 1 || clauses[0].is(syntax_kind::braced_init_list))
            continue;
        expression_meaning element;
        VTRY(r, expression(clauses[0], element));
        early[i] = element;
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
    /* [over.match.funcs]/2: "the set of candidate functions can contain
       both member and non-member functions to be resolved against the
       same argument list"; /4: "For static member functions, the
       implicit object parameter is considered to match any object"
       (object_sequence); a call without an object is made of the
       non-static ones' with no implied argument too, and the chosen one is
       refused below ([class.mfct.non-static]/3) */
    function_candidates(set, false, candidates);
    size_t chosen = 0;
    const expression_meaning *implied_object = nullptr;
    if (object)
        implied_object = &implied;
    VTRY(r, best_viable_function(n, candidates, implied_object, early, chosen,
                                 nullptr, true, &arguments));
    entity_index c = set[chosen];
    if (entity_table[c].nonstatic_member != (object != 0))
        /* a static member function chosen for a call with an object
           (called without it), or a non-static one without an object
           ([class.mfct.non-static]/3, ill-formed): not analysed */
        return not_supported_because(n, "over.match.funcs",
                                     "a call whose chosen member's staticness "
                                     "does not match the object argument");
    /* a copy: deleted_use may define an implicit member, adding entities
       that may move entity_table's */
    const entity x = entity_table[c];
    entity_index object_class = 0;
    if (object)
        object_class = type_table[implied.type.unqualified()].entity;
    /* [class.protected]/1: the object expression's class, as written (the
       implied object argument may be its base subobject) */
    if (object &&
        semantics[f.node].kind == semantic_kind::bound_member_function &&
        semantics[f.node].c)
        object_class = semantics[f.node].c;
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
    VTRY_SET(r, deleted_use(callee_at, c));
    if (use < name_uses.size() && !name_uses[use].entity)
        name_uses[use].entity = c;
    /* the expression the name is, as analysed: its type and entity are
       the function's chosen */
    semantic_node &selected = callee();
    bool bound = selected.kind == semantic_kind::bound_member_function;
    analysed_expression *claims = expressions.data();
    for (size_t i = expressions.size(); i-- > 0;) {
        expression_meaning &meaning = claims[i].meaning;
        if (meaning.node != f.node)
            continue;
        meaning.type = x.type;
        if (!bound)
            meaning.entity = c;
        break;
    }
    selected.type = x.type;
    if (bound) {
        selected.b = c;
    } else {
        selected.a = c;
        f.entity = c;
    }
    f.type = x.type;
    return analysis_result::well_formed;
}

} // namespace cplusplus
