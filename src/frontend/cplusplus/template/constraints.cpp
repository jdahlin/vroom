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
#include "common/hash.h"
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
    VTRY(r, template_head_parameters(n, parameters, true));
    if (definition.size() != 5 || !definition[1].is_token())
        return not_supported(n, "temp.concept");
    uint32_t name = tokens.identifier(definition[1].token());
    if (entity_table.find(outer, name))
        return not_supported(n, "temp.concept");
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    VTRY_SET(r, template_parameter_scope(n, parameters, outer, name, index,
                                         scope, first));
    function_template record = new_template(
        name, outer, n, index, first, static_cast<uint32_t>(parameters.size()));
    entity_table[record.entity].defined = true;
    if (definition[3].is_token())
        return not_supported(n, "temp.concept");
    record.definition = definition[3].index();
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
    VTRY_SET(r, concept_satisfied(n, ct, arguments, satisfied));
    m = integer_constant(fundamental_type::bool_, satisfied, n.first_token());
    return analysis_result::well_formed;
}

/*
 * [expr.prim.req.compound]/1.3 and [temp.param]/4: whether the
 * immediately-declared constraint of the type-constraint q, C<U, A...>,
 * is satisfied for the type u (the current scope has the template
 * arguments bound).  A substitution failure in A... is "not satisfied".
 */
analysis_result semantic_analysis::type_constraint_satisfied(SyntaxElement q,
                                                             qualified_type u,
                                                             bool &satisfied)
{
    satisfied = false;
    uint32_t parts = q.size();
    if (q.is_token() || (parts != 1 && parts < 3) || !q[0].is_token())
        return not_supported(q, "temp.param");
    entity_index named = constraint_concept(current_scope, q[0].token());
    uint32_t ct = 0;
    if (named)
        ct = concept_of(named);
    if (!ct ||
        (parts == 1 && function_templates[ct - 1].parameter_count != 1) ||
        template_parameter_is_pack(function_templates[ct - 1].first_parameter))
        return not_supported(q, "temp.param");
    owned_array<template_argument> arguments;
    template_argument first;
    first.kind = template_argument::kind_t::type;
    first.type = u;
    arguments.append(first);
    if (parts != 1) {
        substitution_mark mark = mark_substitution();
        analysis_result r = template_id_arguments(q, ct, arguments);
        if (r == analysis_result::ill_formed && undo_substitution(mark))
            return analysis_result::well_formed;
        if (r != analysis_result::well_formed)
            return r;
    }
    for (const template_argument &a : arguments)
        if (a.kind == template_argument::kind_t::pack ||
            depends_on_template_parameter(a.type) || a.template_entity)
            return not_supported(q, "temp.constr");
    return concept_satisfied(q, ct, arguments, satisfied);
}

/*
 * [expr.prim.req]/5-6: whether the requirement r of a requires-expression
 * holds, the local parameters and the template arguments bound in the
 * current scope, in an unevaluated operand.  What does not substitute
 * validly is not satisfied; what is valid but not understood is refused.
 */
analysis_result semantic_analysis::requirement_satisfied(SyntaxElement r,
                                                         bool &satisfied)
{
    satisfied = true;
    substitution_mark mark = mark_substitution();
    analysis_result result = analysis_result::well_formed;
    if (r.is(syntax_kind::type_requirement)) {
        /* [expr.prim.req.type]/1: "A type-requirement asserts the validity
           of a type" */
        type_index type = 0;
        uint32_t cv = 0;
        result = typename_type(r, type, cv);
        if (result == analysis_result::well_formed &&
            depends_on_template_parameter(qualified_type::make(type, cv_none)))
            return not_supported(r, "expr.prim.req");
    } else if (r.is(syntax_kind::simple_requirement)) {
        /* [expr.prim.req.simple]/1: "A simple-requirement asserts the
           validity of an expression", an unevaluated operand */
        expression_meaning v;
        result = expression(r[0], v);
        if (result == analysis_result::well_formed &&
            depends_on_template_parameter(v.type))
            return not_supported(r, "expr.prim.req");
    } else if (r.is(syntax_kind::nested_requirement)) {
        /* [expr.prim.req.nested]/1: "A nested-requirement can be used to
           specify additional constraints in terms of local parameters" */
        return constraint_satisfied(r[1], current_scope, lookup_horizon,
                                    satisfied);
    } else if (r.is(syntax_kind::compound_requirement)) {
        /* [expr.prim.req.compound]/1: the expression's validity, then
           (1.2) noexcept, (1.3) the return-type-requirement's
           type-constraint for decltype((E)) */
        expression_meaning v;
        result = expression(r[1], v);
        if (result == analysis_result::well_formed) {
            if (depends_on_template_parameter(v.type))
                return not_supported(r, "expr.prim.req.compound");
            for (uint32_t i = 3; i < r.size(); i++) {
                if (r[i].is(standard_keyword::noexcept_)) {
                    bool throwing = false;
                    VTRY(t, potentially_throwing(r, v.node, throwing));
                    if (throwing)
                        satisfied = false;
                } else if (r[i].is(syntax_kind::return_type_requirement) &&
                           satisfied) {
                    qualified_type u;
                    if (!decltype_of(v, false, u))
                        return not_supported(r, "expr.prim.req.compound");
                    VTRY(t, type_constraint_satisfied(r[i][1], u, satisfied));
                }
            }
        }
    } else {
        return not_supported(r, "expr.prim.req");
    }
    if (result == analysis_result::ill_formed && undo_substitution(mark)) {
        satisfied = false;
        return analysis_result::well_formed;
    }
    return result;
}

/*
 * [expr.prim.req]/6: "The substitution of template arguments into a
 * requires-expression may result in the formation of invalid types or
 * expressions in its requirements or the violation of the semantic
 * constraints of those requirements.  In such cases, the requires-expression
 * evaluates to false"; "If substitution (if any) and semantic constraint
 * checking succeed, the requires-expression evaluates to true."  The
 * requirement-parameter-list's parameters are local to the requirement-body
 * (/3, /5), the requirements checked in lexical order in unevaluated
 * operands.
 */
analysis_result semantic_analysis::requires_expression(RequiresExpression n,
                                                       expression_meaning &m)
{
    if (!substituting || n.size() < 2 || n.size() > 3)
        return not_supported(n, "expr.prim.req");
    SyntaxElement list;
    if (n.size() == 3) {
        list = n[1];
        if (!list.is(syntax_kind::requirement_parameter_list))
            return not_supported(n, "expr.prim.req");
    }
    /* { requirement-seq }: the requirements between the braces, or the
       requirement-seq's */
    auto requirements = n[n.size() - 1];
    uint32_t first = 1;
    uint32_t count = requirements.size() - 2;
    if (count == 1 && requirements[1].is(syntax_kind::requirement_seq)) {
        requirements = requirements[1];
        first = 0;
        count = requirements.size();
    }
    entity_index outer = current_scope;
    bool satisfied = true;
    analysis_result result = analysis_result::well_formed;
    if (list) {
        /* [expr.prim.req]/5: the local parameters, their types
           substituted first */
        owned_array<parameter_meaning> saved_parameters =
            static_cast<owned_array<parameter_meaning> &&>(function_parameters);
        uint32_t saved_cv = function_cv;
        uint32_t saved_pack = function_empty_pack;
        bool saved_specified = function_exceptions_specified;
        bool saved_member = in_member_declarator;
        function_parameters.clear();
        substitution_mark mark = mark_substitution();
        qualified_type type = qualified_type::make(
            type_table.fundamental(fundamental_type::void_), cv_none);
        result = function(list, type);
        if (result == analysis_result::well_formed)
            open_parameter_scope();
        else if (result == analysis_result::ill_formed &&
                 undo_substitution(mark)) {
            satisfied = false;
            result = analysis_result::well_formed;
        }
        function_cv = saved_cv;
        function_empty_pack = saved_pack;
        function_exceptions_specified = saved_specified;
        in_member_declarator = saved_member;
        function_parameters =
            static_cast<owned_array<parameter_meaning> &&>(saved_parameters);
    }
    unevaluated++;
    for (uint32_t i = first; result == analysis_result::well_formed &&
                             satisfied && i < first + count;
         i++)
        result = requirement_satisfied(requirements[i], satisfied);
    unevaluated--;
    current_scope = outer;
    if (result != analysis_result::well_formed)
        return result;
    m = integer_constant(fundamental_type::bool_, satisfied, n.first_token());
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
    /* (what is not decided is not kept: asked again it names its reason) */
    if (r != analysis_result::well_formed)
        return r;
    satisfaction s;
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
        at = e;
        /* [temp.constr.op]/2: "A conjunction is a constraint taking two
           operands.  To determine if a conjunction is satisfied, the
           satisfaction of the first operand is checked.  If that is not
           satisfied, the conjunction is not satisfied.  Otherwise, the
           conjunction is satisfied if and only if the second operand is
           satisfied."  /3, a disjunction, the same with "satisfied" */
        bool conjunction = e.is(syntax_kind::logical_and_expression,
                                syntax_kind::constraint_logical_and_expression);
        bool disjunction = e.is(syntax_kind::logical_or_expression,
                                syntax_kind::constraint_logical_or_expression);
        if ((conjunction || disjunction) && e.size() == 3) {
            analysis_result r =
                constraint_satisfied(e[0], scope, horizon, satisfied);
            if (r != analysis_result::well_formed || satisfied != conjunction)
                return r;
            return constraint_satisfied(e[2], scope, horizon, satisfied);
        }
        /* [temp.constr.normal]/1.1: "The normal form of an expression (E)
           is the normal form of E." */
        if (e.is(syntax_kind::primary_expression) && e.size() == 3 &&
            e[0].is(preprocessing_op_or_punc::LEFT_PARENTHESIS))
            return constraint_satisfied(e[1], scope, horizon, satisfied);
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
    if (a.end_token() - a.first_token() != b.end_token() - b.first_token())
        return false;
    for (token_index i = 0; i < a.end_token() - a.first_token(); i++)
        if (!same_token_spelling(a.first_token() + i, b.first_token() + i))
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

/* what token i names, looked up from scope outward, the global namespace
   (0) last: the entity of the nearest scope that declares the name */
entity_index semantic_analysis::constraint_name(entity_index scope,
                                                token_index i) const
{
    uint32_t name = tokens.identifier(i);
    if (!name)
        return 0;
    for (entity_index s = scope;; s = entity_table[s].enclosing) {
        entity_index e = entity_table.find(s, name);
        if (e || !s)
            return e;
    }
}

entity_index semantic_analysis::constraint_concept(entity_index scope,
                                                   token_index i) const
{
    entity_index e = constraint_name(scope, i);
    if (e && concept_of(e))
        return e;
    return 0;
}

entity_index semantic_analysis::constraint_template(entity_index scope,
                                                    token_index i) const
{
    entity_index e = constraint_name(scope, i);
    if (e && (concept_of(e) || variable_template_of(e)))
        return e;
    return 0;
}

entity_index semantic_analysis::constraint_class(entity_index scope,
                                                 token_index i) const
{
    entity_index e = constraint_name(scope, i);
    if (e && entity_table[e].kind == entity_kind::class_type)
        return e;
    return 0;
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
       template-argument-list, C<T, A...>, with A... analysed with the
       template's arguments bound */
    entity_index scope = entity_table.make(entity_kind::block_scope, 0,
                                           entity_table[ft.entity].enclosing);
    for (uint32_t i = 0; i < ft.parameter_count; i++)
        bind_template_argument(
            scope, template_parameter_entities[ft.first_parameter + i],
            values[i]);
    for (uint32_t i = 0; i < ft.parameter_count && satisfied; i++) {
        SyntaxRef p = template_parameter_nodes[ft.first_parameter + i];
        template_parameter_parts x;
        if (!template_parameter_form(p, x) || !x.constraint)
            continue;
        auto q = x.constraint;
        uint32_t parts = q.size();
        if ((parts != 1 && parts < 3) || !q[0].is_token())
            return not_supported(x.constraint, "temp.param");
        entity_index named =
            constraint_concept(entity_table[ft.entity].enclosing, q[0].token());
        uint32_t ct = 0;
        if (named)
            ct = concept_of(named);
        if (!ct ||
            (parts == 1 && function_templates[ct - 1].parameter_count != 1) ||
            template_parameter_is_pack(
                function_templates[ct - 1].first_parameter))
            return not_supported(x.constraint, "temp.param");
        owned_array<template_argument> arguments;
        arguments.append(values[i]);
        analysis_result r = analysis_result::well_formed;
        if (parts != 1)
            r = template_id_arguments_in(x.constraint, ct, scope, arguments);
        if (r == analysis_result::well_formed)
            VTRY_SET(r,
                     concept_satisfied(x.constraint, ct, arguments, satisfied));
    }
    if (!constraint || !satisfied)
        return analysis_result::well_formed;
    /* [temp.constr.constr]: a constraint whose satisfaction asks for its own
       (a constructor template constrained by __is_constructible of its
       class) */
    owned_array<uint64_t> words;
    words.append(function_template);
    for (const template_argument &a : values) {
        words.append(static_cast<uint64_t>(a.kind) |
                     static_cast<uint64_t>(a.type.cv()) << 8);
        words.append(a.type.unqualified());
        words.append(a.value);
        words.append(a.template_entity);
        words.append(a.count);
    }
    uint32_t key = vhash_bytes(reinterpret_cast<const char *>(words.data()),
                               words.size() * sizeof(uint64_t));
    for (uint32_t k : constraints_in_progress)
        if (k == key)
            return not_supported(constraint, "temp.constr");
    constraints_in_progress.append(key);
    analysis_result r = constraint_satisfied(
        constraint, scope, ft.declaration.first_token(), satisfied);
    constraints_in_progress.resize(constraints_in_progress.size() - 1);
    return r;
}

} // namespace cplusplus
