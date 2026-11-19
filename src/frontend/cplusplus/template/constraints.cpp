/*
 * constraints.cpp: [temp.concept] concept definitions and [temp.constr]
 * template constraints: whether a constraint-expression is satisfied for
 * template arguments.
 *
 * [temp.constr.constr]/1: "A constraint is a sequence of logical
 * operations and operands that specifies requirements on template
 * arguments."  A conjunction (&&) and a disjunction (||) are evaluated
 * left to right, the right operand only when the left does not decide
 * ([temp.constr.op]/3, /4); any other expression is an atomic constraint
 * ([temp.constr.atomic]), its template parameters substituted: a
 * substitution that fails leaves it not satisfied, else it is a constant
 * expression of type bool.  Normal forms ([temp.constr.normal]) and the
 * ordering of constrained declarations by them ([temp.constr.order]) are
 * not formed yet.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/*
 * [temp.concept]/1, /2: template<template-parameter-list> concept C =
 * constraint-expression;.  /3: "A concept-definition shall inhabit a
 * namespace scope"; /4: "A concept shall not have associated
 * constraints" (no requires-clause, which the parser does not give).
 * Recorded as a template whose definition is the constraint-expression,
 * analysed for each concept-id's arguments (concept_id).
 */
analysis_result
semantic_analysis::concept_definition(SyntaxRef n, ConceptDefinition definition)
{
    entity_index outer = current_scope;
    owned_array<SyntaxRef> parameters;
    analysis_result r = template_head_parameters(n, parameters, true);
    if (r != analysis_result::well_formed)
        return r;
    auto parts = definition;
    if (parts.size() != 5 || !parts[1].is_token())
        return not_supported(n, "temp.concept");
    uint32_t name = tokens.identifier(parts[1].token());
    if (entity_table.find(outer, name))
        return not_supported(n, "temp.concept");
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    r = template_parameter_scope(n, parameters, outer, name, index, scope,
                                 first);
    if (r != analysis_result::well_formed)
        return r;
    function_template record = new_template(
        name, outer, n, index, first, static_cast<uint32_t>(parameters.size()));
    entity_table[record.entity].defined = true;
    if (parts[3].is_token())
        return not_supported(n, "temp.concept");
    record.definition = parts[3].index();
    record.is_concept = true;
    hold_template_name(record, name, outer);
    return analysis_result::well_formed;
}

uint32_t semantic_analysis::concept_of(entity_index e) const
{
    return template_of_form(e, &function_template::is_concept);
}

/*
 * [temp.names]/8: "A concept-id is a simple-template-id where the
 * template-name is a concept-name.  A concept-id is a prvalue of type
 * bool, and does not name a template specialization.  A concept-id
 * evaluates to true if the concept's normalized constraint-expression is
 * satisfied ([temp.constr.constr]) by the specified template arguments
 * and false otherwise."  Of dependent arguments: not analysed.
 */
analysis_result semantic_analysis::concept_id(SyntaxRef n, uint32_t ct,
                                              expression_meaning &m)
{
    owned_array<template_argument> arguments;
    analysis_result r = template_id_arguments(n, ct, arguments);
    /* [temp.constr.normal]/1.4: a concept-id's arguments are substituted
       into the normal form's atomic constraints, each only where it is
       checked; formed here as a whole, a failure is not known to be one */
    if (r == analysis_result::ill_formed && substituting)
        return not_supported(n, "temp.constr.normal");
    if (r != analysis_result::well_formed)
        return r;
    for (const template_argument &a : arguments)
        if (a.kind == template_argument::kind_t::pack ||
            depends_on_template_parameter(a.type) || a.template_entity)
            return not_supported(n, "temp.constr");
    bool satisfied = false;
    r = concept_satisfied(n, ct, arguments, satisfied);
    if (r != analysis_result::well_formed)
        return r;
    m = integer_constant(fundamental_type::bool_, satisfied, n.first_token());
    return analysis_result::well_formed;
}

/*
 * [expr.prim.req]/6: "The substitution of template arguments into a
 * requires-expression may result in the formation of invalid types or
 * expressions in its requirements ... In such cases, the
 * requires-expression evaluates to false"; "If substitution (if any) and
 * semantic constraint checking succeed, the requires-expression evaluates
 * to true."  Of type-requirements typename nested-name-specifier
 * type-name; (/1 a prvalue of type bool), checked in lexical order.  A
 * requirement-parameter-list, a simple, compound or nested requirement:
 * not analysed.
 */
analysis_result semantic_analysis::requires_expression(RequiresExpression n,
                                                       expression_meaning &m)
{
    auto parts = n;
    if (parts.size() != 2 || !substituting)
        return not_supported(n, "expr.prim.req");
    /* { requirement-seq }: the requirements between the braces, or the
       requirement-seq's */
    auto requirements = parts[1];
    uint32_t first = 1;
    uint32_t count = requirements.size() - 2;
    if (count == 1 && requirements[1].is(syntax_kind::requirement_seq)) {
        requirements = requirements[1];
        first = 0;
        count = requirements.size();
    }
    bool satisfied = true;
    for (uint32_t i = first; i < first + count && satisfied; i++) {
        if (!requirements[i].is(syntax_kind::type_requirement))
            return not_supported(n, "expr.prim.req");
        /* [expr.prim.req.type]/1: "A type-requirement asserts the validity
           of a type" */
        substitution_mark mark = mark_substitution();
        type_index type = 0;
        uint32_t cv = 0;
        analysis_result r = typename_type(requirements[i], type, cv);
        if (r == analysis_result::ill_formed && undo_substitution(mark)) {
            satisfied = false;
            continue;
        }
        if (r != analysis_result::well_formed)
            return r;
        if (depends_on_template_parameter(qualified_type::make(type, cv_none)))
            return not_supported(n, "expr.prim.req");
    }
    m = integer_constant(fundamental_type::bool_, satisfied,
                         parts.first_token());
    return analysis_result::well_formed;
}

analysis_result semantic_analysis::concept_satisfied(
    SyntaxRef at, uint32_t ct, const owned_array<template_argument> &arguments,
    bool &satisfied)
{
    for (const satisfaction &s : satisfactions) {
        if (s.concept_template != ct || s.count != arguments.size())
            continue;
        bool same = true;
        for (uint32_t i = 0; i < s.count && same; i++)
            same = satisfaction_arguments[s.first + i] == arguments[i];
        if (same) {
            if (s.unknown)
                return not_supported(at, "temp.constr.atomic");
            satisfied = s.satisfied;
            return analysis_result::well_formed;
        }
    }
    const function_template c = function_templates[ct - 1];
    entity_index scope = entity_table.make(entity_kind::block_scope, 0,
                                           entity_table[c.entity].enclosing);
    for (uint32_t i = 0; i < c.parameter_count; i++)
        bind_template_argument(
            scope, template_parameter_entities[c.first_parameter + i],
            arguments[i]);
    analysis_result r = constraint_satisfied(
        c.definition, scope, c.declaration.first_token(), satisfied);
    if (r != analysis_result::well_formed && r != analysis_result::unsupported)
        return r;
    satisfaction s;
    s.unknown = r == analysis_result::unsupported;
    s.concept_template = ct;
    s.first = static_cast<uint32_t>(satisfaction_arguments.size());
    s.count = static_cast<uint32_t>(arguments.size());
    s.satisfied = satisfied;
    for (const template_argument &a : arguments)
        satisfaction_arguments.append(a);
    satisfactions.append(s);
    return r;
}

/*
 * [temp.constr.constr]: whether the constraint-expression e is satisfied
 * with the template parameters' names bound in scope (looked up as from
 * horizon, the declaration it belongs to).  What its analysis records
 * (expressions, name uses) is taken back: the constraint's tokens are the
 * template's, not this substitution's.
 */
analysis_result semantic_analysis::constraint_satisfied(SyntaxElement e,
                                                        entity_index scope,
                                                        token_index horizon,
                                                        bool &satisfied)
{
    satisfied = false;
    /* (a token is an atomic constraint: true) */
    SyntaxRef at = current_declaration;
    if (!e.is_token()) {
        auto c = e;
        at = c;
        auto node = c;
        /* [temp.constr.op]/2: "A conjunction is a constraint taking two
           operands.  To determine if a conjunction is satisfied, the
           satisfaction of the first operand is checked.  If that is not
           satisfied, the conjunction is not satisfied.  Otherwise, the
           conjunction is satisfied if and only if the second operand is
           satisfied."  /3, a disjunction, the same with "satisfied" */
        bool conjunction =
            node.is(syntax_kind::logical_and_expression) ||
            node.is(syntax_kind::constraint_logical_and_expression);
        bool disjunction =
            node.is(syntax_kind::logical_or_expression) ||
            node.is(syntax_kind::constraint_logical_or_expression);
        if ((conjunction || disjunction) && node.size() == 3) {
            analysis_result r =
                constraint_satisfied(node[0], scope, horizon, satisfied);
            if (r != analysis_result::well_formed || satisfied != conjunction)
                return r;
            return constraint_satisfied(node[2], scope, horizon, satisfied);
        }
        /* [temp.constr.normal]/1.1: "The normal form of an expression (E)
           is the normal form of E." */
        if (node.is(syntax_kind::primary_expression) && node.size() == 3 &&
            node[0].is(preprocessing_op_or_punc::LEFT_PARENTHESIS))
            return constraint_satisfied(node[1], scope, horizon, satisfied);
    }
    /* [temp.constr.atomic]/3: "To determine if an atomic constraint is
       satisfied, the parameter mapping and template arguments are first
       substituted into its expression.  If substitution results in an
       invalid type or expression, the constraint is not satisfied.
       Otherwise, the lvalue-to-rvalue conversion is performed if necessary,
       and E shall be a constant expression of type bool." */
    substitution_mark mark = mark_substitution();
    entity_index saved_scope = current_scope;
    token_index saved_horizon = lookup_horizon;
    current_scope = scope;
    lookup_horizon = horizon;
    substituting++;
    expression_meaning value;
    analysis_result r = expression(e, value);
    if (r == analysis_result::well_formed)
        r = operand_value(at, value);
    substituting--;
    current_scope = saved_scope;
    lookup_horizon = saved_horizon;
    if (r == analysis_result::ill_formed && undo_substitution(mark))
        return analysis_result::well_formed;
    expressions.resize(mark.expressions);
    name_uses.resize(mark.uses);
    if (r != analysis_result::well_formed)
        return r;
    const struct type &t = type_table[value.type.unqualified()];
    if (t.kind != type_kind::fundamental ||
        t.fundamental != fundamental_type::bool_)
        /* ill-formed ([temp.constr.atomic]/3), which clang reports at
           the constraint, not where its satisfaction is checked */
        return not_supported(at, "temp.constr.atomic");
    if (value.evaluation != constness::constant)
        return not_supported(at, "temp.constr.atomic");
    satisfied = value.value != 0;
    return analysis_result::well_formed;
}

/*
 * [temp.over.link]/7: whether two template-heads' requires-clauses are
 * equivalent; taken as the same tokens (/5's rule of equivalent
 * expressions, without renaming the template parameters)
 */
bool semantic_analysis::same_constraints(SyntaxRef a, SyntaxRef b)
{
    /* the type-constraints too, parameter by parameter ([temp.over.link]/6:
       equivalent template-heads) */
    owned_array<SyntaxRef> pa, pb;
    if (a.size() && b.size() && a[0].is(syntax_kind::template_head) &&
        b[0].is(syntax_kind::template_head) && template_head_list(a[0], pa) &&
        template_head_list(b[0], pb)) {
        if (pa.size() != pb.size())
            return false;
        for (size_t i = 0; i < pa.size(); i++) {
            template_parameter_parts u, v;
            if (!template_parameter_form(pa[i], u) ||
                !template_parameter_form(pb[i], v))
                continue;
            if (!u.constraint != !v.constraint ||
                (u.constraint && !same_tokens(u.constraint, v.constraint)))
                return false;
        }
    }
    SyntaxElement x = head_constraint(a), y = head_constraint(b);
    if (!x && !y)
        x = trailing_constraint(a), y = trailing_constraint(b);
    if (!x || !y)
        return !x && !y;
    if (x.is_token() || y.is_token())
        return x.is_token() && y.is_token() &&
               same_token_spelling(x.token(), y.token());
    return same_tokens(x, y);
}

/* whether template-head n has a requires-clause or a type-constraint */
bool semantic_analysis::template_head_constrained(SyntaxRef n)
{
    owned_array<SyntaxRef> parameters;
    if (head_constraint(n))
        return true;
    if (n.size() && n[0].is(syntax_kind::template_head) &&
        !template_head_list(n[0], parameters))
        return false;
    abbreviated_parameters(n, parameters);
    for (SyntaxRef p : parameters) {
        template_parameter_parts x;
        if (template_parameter_form(p, x) && x.constraint)
            return true;
    }
    return false;
}

/* whether nodes a and b are the same tokens, spelled the same */
bool semantic_analysis::same_tokens(SyntaxRef a, SyntaxRef b)
{
    auto p = a;
    auto q = b;
    if (p.end_token() - p.first_token() != q.end_token() - q.first_token())
        return false;
    for (token_index i = 0; i < p.end_token() - p.first_token(); i++)
        if (!same_token_spelling(p.first_token() + i, q.first_token() + i))
            return false;
    return true;
}

bool semantic_analysis::same_token_spelling(token_index a, token_index b)
{
    owned_buffer x, y;
    tokens.spelling(a, x.get());
    tokens.spelling(b, y.get());
    return x.size() == y.size() && !memcmp(x.data(), y.data(), x.size());
}

/* the function template whose entity (not held name) t is, plus one */
uint32_t semantic_analysis::template_record(entity_index t) const
{
    for (size_t i = 0; i < function_templates.size(); i++)
        if (function_templates[i].entity == t && t)
            return static_cast<uint32_t>(i + 1);
    return 0;
}

SyntaxElement semantic_analysis::template_constraint(entity_index t) const
{
    uint32_t ft = template_record(t);
    if (!ft)
        return SyntaxElement();
    return head_constraint(function_templates[ft - 1].declaration);
}

SyntaxElement
semantic_analysis::template_trailing_constraint(entity_index t) const
{
    uint32_t ft = template_record(t);
    if (!ft)
        return SyntaxElement();
    return trailing_constraint(function_templates[ft - 1].declaration);
}

entity_index semantic_analysis::constraint_parameter(entity_index t,
                                                     token_index i) const
{
    uint32_t ft = template_record(t);
    uint32_t name = tokens.identifier(i);
    if (!ft || !name)
        return 0;
    const struct function_template &f = function_templates[ft - 1];
    for (uint32_t k = 0; k < f.parameter_count; k++) {
        entity_index pe = template_parameter_entities[f.first_parameter + k];
        if (entity_table[pe].name == name)
            return pe;
    }
    return 0;
}

/* the function parameter of function template t's declarator whose name
   token i spells: its position plus one (0: none); the parameters of the
   first parameters-and-qualifiers outside the trailing-return-type */
uint32_t semantic_analysis::function_parameter_position(entity_index t,
                                                        token_index i) const
{
    uint32_t ft = template_record(t);
    uint32_t name = tokens.identifier(i);
    if (!ft || !name)
        return 0;
    SyntaxRef d = function_templates[ft - 1].declarator;
    owned_array<SyntaxElement> work;
    work.append(d[leading_gnu_attributes(d) + 1]);
    uint32_t position = 0;
    bool inside = false;
    /* (depth first, children in order: the parameters in theirs) */
    while (work.size()) {
        SyntaxElement c = work[work.size() - 1];
        work.resize(work.size() - 1);
        if (c.is_token() || c.is(syntax_kind::trailing_return_type))
            continue;
        if (c.is(syntax_kind::parameter_declaration)) {
            position++;
            for (auto child : c.elements(1)) {
                token_index id = declarator_id(child);
                if (id && tokens.identifier(id) == name)
                    return position;
            }
            continue;
        }
        bool clause = c.is(syntax_kind::parameters_and_qualifiers);
        if (clause && inside)
            continue;
        inside |= clause;
        for (uint32_t j = c.size(); j > 0; j--)
            work.append(c[j - 1]);
    }
    return 0;
}

entity_index semantic_analysis::constraint_concept(entity_index scope,
                                                   token_index i) const
{
    uint32_t name = tokens.identifier(i);
    if (!name)
        return 0;
    /* scope's scopes outward, the global namespace (0) last */
    for (entity_index s = scope;; s = entity_table[s].enclosing) {
        entity_index e = entity_table.find(s, name);
        if (e && concept_of(e))
            return e;
        if (e)
            return 0;
        if (!s)
            return 0;
    }
}

analysis_result semantic_analysis::template_constraints_satisfied(
    uint32_t function_template, const owned_array<template_argument> &values,
    bool &satisfied)
{
    satisfied = true;
    const struct function_template ft =
        function_templates[function_template - 1];
    SyntaxElement constraint = head_constraint(ft.declaration);
    /* (a trailing requires-clause only where the head has none: the
       definitions refuse both) */
    if (!constraint)
        constraint = trailing_constraint(ft.declaration);
    /* [temp.constr.decl]/3: the associated constraints are the
       conjunction of the type-constraints' immediately-declared
       constraints, in order, then the requires-clause's
       constraint-expression.  [temp.param]/4: a type-constraint Q
       designating a concept C, of a type-parameter T: C<T>; Q's own
       template-argument-list, C<T, A...>, is not analysed */
    for (uint32_t i = 0; i < ft.parameter_count && satisfied; i++) {
        SyntaxRef p = template_parameter_nodes[ft.first_parameter + i];
        template_parameter_parts x;
        if (!template_parameter_form(p, x) || !x.constraint)
            continue;
        auto q = x.constraint;
        if (q.size() != 1 || !q[0].is_token())
            return not_supported(x.constraint, "temp.param");
        entity_index named =
            constraint_concept(entity_table[ft.entity].enclosing, q[0].token());
        uint32_t ct = 0;
        if (named)
            ct = concept_of(named);
        if (!ct || function_templates[ct - 1].parameter_count != 1 ||
            template_parameter_is_pack(
                function_templates[ct - 1].first_parameter))
            return not_supported(x.constraint, "temp.param");
        owned_array<template_argument> arguments;
        arguments.append(values[i]);
        analysis_result r =
            concept_satisfied(x.constraint, ct, arguments, satisfied);
        if (r != analysis_result::well_formed)
            return r;
    }
    if (!constraint || !satisfied)
        return analysis_result::well_formed;
    entity_index scope = entity_table.make(entity_kind::block_scope, 0,
                                           entity_table[ft.entity].enclosing);
    for (uint32_t i = 0; i < ft.parameter_count; i++)
        bind_template_argument(
            scope, template_parameter_entities[ft.first_parameter + i],
            values[i]);
    return constraint_satisfied(constraint, scope, ft.declaration.first_token(),
                                satisfied);
}

} // namespace cplusplus
