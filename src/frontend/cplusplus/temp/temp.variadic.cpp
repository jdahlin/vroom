/*
 * temp.variadic.cpp: [temp.variadic] Variadic templates.
 *
 * [temp.variadic]/1: "A template parameter pack is a template parameter
 * that accepts zero or more template arguments."  /2: "A function
 * parameter pack is a function parameter that accepts zero or more function
 * arguments."  /5: "A pack expansion consists of a pattern and an ellipsis,
 * the instantiation of which produces zero or more instantiations of the
 * pattern in a list".
 *
 * A function template's template parameter pack (its last parameter) is
 * deduced from a call's trailing arguments (deduce_from_call) as one
 * template argument of kind pack, whose elements are pack_arguments'.  An
 * instantiation binds the pack's name to an entity of kind pack whose
 * elements are the arguments' typedef-names; the function parameter pack
 * of its declarator becomes one parameter per element (function, dcl.fct.cpp),
 * whose names the body finds as a pack of their variables.  A pattern is
 * analysed once per element with expanding set: a name that finds a pack
 * then finds its element expansion_element (declared_in); a pack named
 * outside a pattern is not known.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

token_index semantic_analysis::template_parameter_id(node_index p) const
{
    template_parameter_parts x;
    template_parameter_form(p, x);
    return x.id;
}

/* type-parameter: type-parameter-key ..._opt identifier_opt (= type-id)_opt
   or template-head type-parameter-key ..._opt identifier_opt (=
   id-expression)_opt; parameter-declaration: decl-specifier-seq,
   identifier or declarator_opt, (= initializer-clause)_opt */
bool semantic_analysis::template_parameter_form(
    node_index p, template_parameter_parts &out) const
{
    out = template_parameter_parts();
    const syntax_node &x = tree[p];
    const syntax_child *c = tree.children(p);
    if (!x.child_count)
        return false;
    uint32_t k = 1;
    out.type =
        syntax_is(tree, syntax_child::node(p), syntax_kind::type_parameter);
    if (out.type) {
        if (syntax_is(tree, c[0], syntax_kind::type_constraint)) {
            out.constraint = c[0].index();
        } else if (!c[0].is_token()) {
            if (!syntax_is(tree, c[0], syntax_kind::template_head) ||
                x.child_count < 2 || !c[1].is_token())
                return false;
            out.template_template = true;
            out.head = c[0].index();
            k = 2;
        }
    } else if (!syntax_is(tree, c[0], syntax_kind::decl_specifier_seq)) {
        return false;
    }
    if (k < x.child_count &&
        syntax_is_punctuator(tokens, c[k],
                             preprocessing_op_or_punc::ELLIPSIS)) {
        out.pack = true;
        k++;
    }
    if (k < x.child_count && c[k].is_token() &&
        tokens[c[k].index()].kind == token_kind::identifier) {
        out.named = true;
        out.id = c[k].index();
        k++;
    } else if (!out.type && !out.pack && k < x.child_count &&
               syntax_is(tree, c[k], syntax_kind::noptr_declarator) &&
               tree[c[k].index()].child_count == 2 &&
               syntax_is_punctuator(tokens, tree.children(c[k].index())[0],
                                    preprocessing_op_or_punc::ELLIPSIS) &&
               tree.children(c[k].index())[1].is_token() &&
               tokens[tree.children(c[k].index())[1].index()].kind ==
                   token_kind::identifier) {
        /* [temp.param]/17: a non-type template parameter pack, int... N
           (the parser's declarator: the ellipsis, then the name) */
        out.pack = true;
        out.named = true;
        out.id = tree.children(c[k].index())[1].index();
        k++;
    } else if (!out.type && k < x.child_count && !c[k].is_token()) {
        out.has_declarator = true;
        out.declarator = c[k];
        out.id = declarator_id(c[k]);
        out.named = out.id != 0;
        k++;
    }
    if (k + 2 == x.child_count &&
        syntax_is_punctuator(tokens, c[k], preprocessing_op_or_punc::EQUAL)) {
        out.has_default = true;
        out.default_argument = c[k + 1];
        k += 2;
    }
    return k == x.child_count;
}

bool semantic_analysis::template_head_list(
    node_index head, owned_array<node_index> &parameters) const
{
    for (uint32_t i = 0; i < tree[head].child_count; i++) {
        syntax_child c = tree.children(head)[i];
        if (c.is_token())
            continue;
        if (syntax_is(tree, c, syntax_kind::type_parameter) ||
            syntax_is(tree, c, syntax_kind::parameter_declaration)) {
            parameters.append(c.index());
            continue;
        }
        /* (its requires-clause: head_constraint) */
        if (syntax_is(tree, c, syntax_kind::requires_clause))
            continue;
        if (!syntax_is(tree, c, syntax_kind::template_parameter_list))
            return false;
        for (uint32_t k = 0; k < tree[c.index()].child_count; k++) {
            syntax_child p = tree.children(c.index())[k];
            if (p.is_token())
                continue;
            if (!syntax_is(tree, p, syntax_kind::type_parameter) &&
                !syntax_is(tree, p, syntax_kind::parameter_declaration))
                return false;
            parameters.append(p.index());
        }
    }
    return true;
}

bool semantic_analysis::template_template_parameter(entity_index e) const
{
    const entity &x = entity_table[e];
    return x.kind == entity_kind::template_entity && x.instantiation &&
           function_templates[x.instantiation - 1].is_parameter;
}

/* [temp.param]/18: "If a template-parameter is a type-parameter with an
   ellipsis prior to its optional identifier ... the template-parameter is
   a template parameter pack" */
bool semantic_analysis::template_parameter_is_pack(uint32_t i) const
{
    template_parameter_parts x;
    return template_parameter_form(template_parameter_nodes[i], x) && x.pack;
}

/* a pack's argument: its elements stored once, so that two packs of the
   same elements are the same argument (an instantiation's key) */
semantic_analysis::template_argument
semantic_analysis::make_pack(const template_argument *elements, uint32_t count)
{
    template_argument a;
    a.kind = template_argument::kind_t::pack;
    a.count = count;
    if (!count)
        return a;
    size_t size = pack_arguments.size();
    for (size_t start = 0; start + count <= size; start++) {
        bool same = true;
        for (uint32_t i = 0; i < count && same; i++)
            same = pack_arguments[start + i] == elements[i];
        if (same) {
            a.value = start;
            return a;
        }
    }
    a.value = size;
    for (uint32_t i = 0; i < count; i++)
        pack_arguments.append(elements[i]);
    return a;
}

entity_index semantic_analysis::make_bound_pack(entity_index scope,
                                                uint32_t name,
                                                const entity_index *elements,
                                                uint32_t count)
{
    entity_index e = entity_table.make(entity_kind::pack, name, scope);
    entity_table[e].value = pack_elements.size();
    entity_table[e].member_count = count;
    entity_table[e].defined = true;
    for (uint32_t i = 0; i < count; i++)
        pack_elements.append(elements[i]);
    entity_table.bind(scope, name, e);
    return e;
}

entity_index semantic_analysis::find_pack(uint32_t name) const
{
    /* (as lookup_unqualified: a member-specification's names first) */
    entity_index start = current_scope;
    if (is_namespace(start) && defining_class)
        start = defining_class;
    for (entity_index s = start;; s = entity_table[s].enclosing) {
        entity_index e = entity_table.find(s, name);
        if (e) {
            if (entity_table[e].kind == entity_kind::pack)
                return e;
            return 0;
        }
        if (!s)
            return 0;
    }
}

static bool token_is(token_stream &tokens, token_index t,
                     preprocessing_op_or_punc op)
{
    return syntax_is_punctuator(tokens, syntax_child::token(t), op);
}

/* [temp.variadic]/7: "All of the packs expanded by a pack expansion shall
   have the same number of arguments specified." */
bool semantic_analysis::expansion_count(token_index first, token_index end,
                                        uint32_t &count) const
{
    bool found = false;
    count = 0;
    for (token_index t = first; t < end; t++) {
        if (token_is(tokens, t, preprocessing_op_or_punc::ELLIPSIS))
            return false;
        if (tokens[t].kind != token_kind::identifier)
            continue;
        /* (a member's name after . -> or ::) */
        if (t > first &&
            (token_is(tokens, t - 1, preprocessing_op_or_punc::DOT) ||
             token_is(tokens, t - 1, preprocessing_op_or_punc::ARROW) ||
             token_is(tokens, t - 1, preprocessing_op_or_punc::COLON_COLON)))
            continue;
        entity_index e = find_pack(tokens.identifier(t));
        if (!e)
            continue;
        uint32_t n = entity_table[e].member_count;
        if (found && n != count)
            return false;
        count = n;
        found = true;
    }
    return found;
}

analysis_result semantic_analysis::parameter_pack_expansion(
    node_index d, bool last, uint32_t &elements, token_index &ellipsis)
{
    elements = 0;
    ellipsis = 0;
    token_index first = tree[d].first_token, end = tree[d].end_token;
    /* [dcl.fct]/21: the ellipsis of a function parameter pack is the
       declarator's; one in the decl-specifier-seq is a template
       argument's (tuple<Ts...>&) */
    token_index from = first;
    if (tree[d].child_count &&
        syntax_is(tree, tree.children(d)[0], syntax_kind::decl_specifier_seq))
        from = tree[tree.children(d)[0].index()].end_token;
    token_index found = 0;
    uint32_t ellipses = 0;
    for (token_index t = from; t < end; t++)
        if (token_is(tokens, t, preprocessing_op_or_punc::ELLIPSIS)) {
            found = t;
            ellipses++;
        }
    if (ellipses != 1)
        return analysis_result::well_formed;
    uint32_t before = 0, after = 0;
    bool in_before = expansion_count(first, found, before);
    bool in_after = expansion_count(found + 1, end, after);
    if (!in_before && !in_after)
        return analysis_result::well_formed;
    if (!last || (in_before && in_after && before != after))
        return not_supported(d, "temp.variadic");
    ellipsis = found;
    elements = before;
    if (in_after)
        elements = after;
    return analysis_result::well_formed;
}

bool semantic_analysis::has_pack_expansion(syntax_child list) const
{
    if (list.is_token())
        return false;
    for (uint32_t i = 0; i < tree[list.index()].child_count; i++)
        if (syntax_is_punctuator(tokens, tree.children(list.index())[i],
                                 preprocessing_op_or_punc::ELLIPSIS))
            return true;
    return false;
}

analysis_result semantic_analysis::expansion_element_expression(
    syntax_child pattern, uint32_t i, expression_meaning &m)
{
    bool saved = expanding;
    uint32_t saved_element = expansion_element;
    expanding = true;
    expansion_element = i;
    analysis_result r = expression(pattern, m);
    expanding = saved;
    expansion_element = saved_element;
    return r;
}

/* the tokens of a syntax child */
static void child_tokens(const syntax_tree &tree, syntax_child c,
                         token_index &first, token_index &end)
{
    if (c.is_token()) {
        first = c.index();
        end = first + 1;
        return;
    }
    first = tree[c.index()].first_token;
    end = tree[c.index()].end_token;
}

/*
 * [expr.prim.fold]/1: "A fold expression performs a fold of a pack over a
 * binary operator."  [temp.variadic]/10: "The instantiation of a
 * fold-expression produces:
 *     ((E1 op E2) op ...) op EN for a unary left fold,
 *     E1 op (... op (EN-1 op EN)) for a unary right fold,
 *     (((E op E1) op E2) op ...) op EN for a binary left fold, and
 *     E1 op (... op (EN-1 op (EN op E))) for a binary right fold.
 * In each case, op is the fold-operator, N is the number of elements in
 * the pack expansion parameters, and each Ei is generated by instantiating
 * the pattern and replacing each pack expansion parameter with its ith
 * element.  For a binary fold-expression, E is generated by instantiating
 * the cast-expression that did not contain an unexpanded pack."
 * Here the built-in operators of [expr.mul] to [expr.log.or] and the comma
 * on operands of neither class nor enumeration type (whose operator
 * functions [over.match.oper] would consider).
 */
analysis_result semantic_analysis::fold(node_index n, expression_meaning &m)
{
    const syntax_node &node = tree[n];
    const syntax_child *c = tree.children(n);
    syntax_child pattern = {}, init = {};
    bool right = false, has_init = false;
    token_index at = 0;
    auto ellipsis = [&](uint32_t i) {
        return syntax_is_punctuator(tokens, c[i],
                                    preprocessing_op_or_punc::ELLIPSIS);
    };
    uint32_t count = 0;
    if (node.child_count == 5 && ellipsis(3)) {
        pattern = c[1];
        at = c[2].index();
        right = true;
    } else if (node.child_count == 5 && ellipsis(1)) {
        pattern = c[3];
        at = c[2].index();
    } else if (node.child_count == 7 && ellipsis(3)) {
        token_index first = 0, end = 0;
        uint32_t left_count = 0, right_count = 0;
        child_tokens(tree, c[1], first, end);
        bool in_left = expansion_count(first, end, left_count);
        child_tokens(tree, c[5], first, end);
        bool in_right = expansion_count(first, end, right_count);
        if (in_left == in_right)
            return not_supported(n, "expr.prim.fold");
        has_init = true;
        at = c[2].index();
        if (in_left) {
            pattern = c[1];
            init = c[5];
            right = true;
        } else {
            pattern = c[5];
            init = c[1];
        }
    } else {
        return not_supported(n, "expr.prim.fold");
    }
    token_index first = 0, end = 0;
    child_tokens(tree, pattern, first, end);
    if (!c[2].is_token() || !expansion_count(first, end, count))
        return not_supported(n, "expr.prim.fold");
    preprocessing_op_or_punc op =
        preprocessing_op_or_punc_primary(tokens[at].op_or_punc);
    bool comma = op == preprocessing_op_or_punc::COMMA;
    switch (op) {
    case preprocessing_op_or_punc::PLUS:
    case preprocessing_op_or_punc::MINUS:
    case preprocessing_op_or_punc::STAR:
    case preprocessing_op_or_punc::SLASH:
    case preprocessing_op_or_punc::PERCENT:
    case preprocessing_op_or_punc::CARET:
    case preprocessing_op_or_punc::AMPERSAND:
    case preprocessing_op_or_punc::BAR:
    case preprocessing_op_or_punc::LESS_LESS:
    case preprocessing_op_or_punc::GREATER_GREATER:
    case preprocessing_op_or_punc::EQUAL_EQUAL:
    case preprocessing_op_or_punc::EXCLAMATION_EQUAL:
    case preprocessing_op_or_punc::LESS:
    case preprocessing_op_or_punc::GREATER:
    case preprocessing_op_or_punc::LESS_EQUAL:
    case preprocessing_op_or_punc::GREATER_EQUAL:
    case preprocessing_op_or_punc::AMPERSAND_AMPERSAND:
    case preprocessing_op_or_punc::BAR_BAR:
    case preprocessing_op_or_punc::COMMA:
        break;
    default:
        return not_supported(n, "expr.prim.fold");
    }
    /* the operands: the pattern's instantiations, then E */
    owned_array<expression_meaning> operands;
    for (uint32_t i = 0; i < count; i++) {
        expression_meaning e;
        analysis_result r = expansion_element_expression(pattern, i, e);
        if (r != analysis_result::well_formed)
            return r;
        operands.append(e);
    }
    expression_meaning e;
    if (has_init) {
        analysis_result r = expression(init, e);
        if (r != analysis_result::well_formed)
            return r;
    }
    for (const expression_meaning &x : operands) {
        type_kind k = type_table[x.type.unqualified()].kind;
        if (k == type_kind::class_type || k == type_kind::enumeration ||
            k == type_kind::none)
            return not_supported(n, "expr.prim.fold");
    }
    if (has_init) {
        type_kind k = type_table[e.type.unqualified()].kind;
        if (k == type_kind::class_type || k == type_kind::enumeration ||
            k == type_kind::none)
            return not_supported(n, "expr.prim.fold");
    }
    if (!count && has_init) {
        m = e;
        return analysis_result::well_formed;
    }
    if (!count) {
        /* /10: "If N is zero for a unary fold-expression, the value of
           the expression is shown in Table 15; if the operator is not
           listed in Table 15, the instantiation is ill-formed." */
        if (op != preprocessing_op_or_punc::AMPERSAND_AMPERSAND &&
            op != preprocessing_op_or_punc::BAR_BAR)
            return not_supported(n, "temp.variadic");
        m = integer_constant(fundamental_type::bool_,
                             op ==
                                 preprocessing_op_or_punc::AMPERSAND_AMPERSAND,
                             node.first_token);
        return analysis_result::well_formed;
    }
    auto combine = [&](expression_meaning left, expression_meaning right,
                       expression_meaning &out) {
        if (!comma)
            return builtin_binary(n, at, op, left, right, out);
        /* [expr.comma]/1: "the left expression is a discarded-value
           expression"; the result is the right operand's */
        out = right;
        if (left.evaluation != constness::constant &&
            (left.evaluation == constness::not_constant ||
             right.evaluation != constness::not_constant)) {
            out.evaluation = left.evaluation;
            out.entity = 0;
        }
        if (left.evaluation != constness::constant)
            out.entity = 0;
        out.beyond_integral_constant = true;
        out.zero_literal = false;
        out.string_literal = 0;
        out.node = expression_node(semantic_kind::comma, out, at, left.node,
                                   right.node);
        return analysis_result::well_formed;
    };
    expression_meaning acc;
    if (right) {
        uint32_t i = count;
        if (has_init) {
            acc = e;
        } else {
            i--;
            acc = operands[i];
        }
        while (i-- > 0) {
            expression_meaning next;
            analysis_result r = combine(operands[i], acc, next);
            if (r != analysis_result::well_formed)
                return r;
            acc = next;
        }
    } else {
        uint32_t i = 0;
        if (has_init) {
            acc = e;
        } else {
            acc = operands[0];
            i = 1;
        }
        for (; i < count; i++) {
            expression_meaning next;
            analysis_result r = combine(acc, operands[i], next);
            if (r != analysis_result::well_formed)
                return r;
            acc = next;
        }
    }
    m = acc;
    return analysis_result::well_formed;
}

} // namespace cplusplus
