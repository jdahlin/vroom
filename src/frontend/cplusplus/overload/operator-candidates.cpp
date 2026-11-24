/*
 * operator-candidates.cpp: [over.match.oper] Operators in expressions.
 *
 * [over.match.oper]/1: "If no operand of an operator in an expression has a
 * type that is a class or an enumeration, the operator is assumed to be a
 * built-in operator and interpreted according to [expr.compound]."  /2: "If
 * either operand has a type that is a class or an enumeration, a
 * user-defined operator function might be declared that implements this
 * operator or a user-defined conversion can be necessary to convert the
 * operand to a type that is appropriate for a built-in operator.  In this
 * case, overload resolution is used to determine which operator function or
 * built-in operator is to be invoked to implement the operator."
 *
 * Built-in candidates ([over.built]) for an operand of a class with
 * conversion functions are not formed: such an operand is not supported,
 * but for ! && || when no operator function is a candidate (their
 * operands are then contextually converted to bool, not analysed here);
 * for an operand of another class none is viable; for an
 * operand of enumeration type one may be, and an operator function is
 * chosen only where it is better than every built-in candidate could be.
 * C++20's rewritten candidates (/3.4) are those of operator== and
 * operator<=>; a rewritten x @ y is (x <=> y) @ 0 of a three-way
 * comparison whose result is of arithmetic type (<compare>'s types are not
 * analysed).
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

namespace
{

bool is_comparison(preprocessing_op_or_punc op)
{
    switch (op) {
    case preprocessing_op_or_punc::EQUAL_EQUAL:
    case preprocessing_op_or_punc::EXCLAMATION_EQUAL:
    case preprocessing_op_or_punc::LESS:
    case preprocessing_op_or_punc::GREATER:
    case preprocessing_op_or_punc::LESS_EQUAL:
    case preprocessing_op_or_punc::GREATER_EQUAL:
        return true;
    default:
        return false;
    }
}

} // namespace

/* whether class c may convert to a built-in candidate's parameter type:
   [over.built] has no class-typed parameters, and a conversion function to
   a class type ([over.best.ics]/4: no second user-defined conversion)
   never makes one viable, a base class's included.  A template conversion
   function is not analysed: assumed to convert.  An explicit conversion
   function ([class.conv.fct]/2) is no implicit conversion of a built-in
   candidate's parameter ([over.best.ics]), unless explicit_counts */
bool semantic_analysis::converts_to_non_class(entity_index c,
                                              bool explicit_counts) const
{
    if (entity_table[c].kind != entity_kind::class_type)
        return true;
    /* [class.conv.fct]/1, [class.member.lookup]: the conversion functions of
       a base class are the derived class's; each base's are looked at the
       same way (a derived class's function that hides a base's of the same
       type is not told apart: assumed to convert) */
    for (uint32_t i = 0; i < entity_table[c].base_count; i++)
        if (converts_to_non_class(
                type_table
                    [entity_table[class_bases[entity_table[c].first_base + i]]
                         .type.unqualified()]
                        .entity,
                explicit_counts))
            return true;
    for (entity_index f = own_conversion_functions(c); f;
         f = entity_table[f].next_overload) {
        if (!entity_table[f].conversion_function)
            return true;
        if (entity_table[f].explicit_ && !explicit_counts)
            continue;
        qualified_type to = conversion_function_result(f).type;
        if (type_table[to.unqualified()].kind != type_kind::class_type)
            return true;
    }
    return false;
}

/*
 * [over.call.object]/2: "for each non-explicit conversion function declared
 * in T or in a base class of T ... to pointer to function, ... a surrogate
 * call function".  Those of the closure types of captureless lambdas have
 * their function call operators' parameters, and the operator's object
 * binds without the surrogate's user-defined conversion
 * ([over.ics.rank]/3.3): when every conversion function of class c and its
 * bases is a closure's whose const function call operator is one of calls
 * (the candidates of c's operator()), no surrogate is the better candidate.
 */
bool semantic_analysis::call_surrogates_lose(
    entity_index c, const owned_array<entity_index> &calls) const
{
    const entity &x = entity_table[c];
    if (x.kind != entity_kind::class_type)
        return false;
    for (uint32_t i = 0; i < x.base_count; i++)
        if (!call_surrogates_lose(
                type_table[entity_table[class_bases[x.first_base + i]]
                               .type.unqualified()]
                    .entity,
                calls))
            return false;
    for (entity_index f = own_conversion_functions(c); f;
         f = entity_table[f].next_overload) {
        if (!entity_table[f].conversion_function || !x.lambda ||
            !x.call_operator ||
            entity_table[x.call_operator].member_cv != cv_const)
            return false;
        bool visible = false;
        for (entity_index k : calls)
            if (k == x.call_operator)
                visible = true;
        if (!visible)
            return false;
    }
    return true;
}

bool semantic_analysis::converts_to_lvalue_reference(entity_index c) const
{
    if (entity_table[c].kind != entity_kind::class_type)
        return true;
    for (uint32_t i = 0; i < entity_table[c].base_count; i++)
        if (converts_to_lvalue_reference(
                type_table
                    [entity_table[class_bases[entity_table[c].first_base + i]]
                         .type.unqualified()]
                        .entity))
            return true;
    for (entity_index f = own_conversion_functions(c); f;
         f = entity_table[f].next_overload) {
        if (!entity_table[f].conversion_function)
            return true;
        qualified_type to = conversion_function_result(f).type;
        if (type_table[to.unqualified()].kind == type_kind::lvalue_reference)
            return true;
    }
    return false;
}

analysis_result semantic_analysis::overloaded_operator(
    SyntaxRef n, token_index at, preprocessing_op_or_punc op, bool postfix,
    const owned_array<SyntaxElement> &syntax,
    owned_array<expression_meaning> &operands, bool &handled,
    expression_meaning &m)
{
    handled = false;
    bool any_class = false, any_enumeration = false;
    for (const expression_meaning &e : operands) {
        type_kind k = type_table[e.type.unqualified()].kind;
        if (k == type_kind::class_type)
            any_class = true;
        if (k == type_kind::enumeration)
            any_enumeration = true;
    }
    if (!any_class && !any_enumeration)
        return analysis_result::well_formed;
    /* [temp.inst]/2: the members the candidates come from: a specialization
       named only is defined here */
    for (const expression_meaning &e : operands)
        require_complete(e.type);
    /* an operand of a class with conversion functions: built-in candidates
       ([over.built]) may be viable, which are not formed.  ! && || go on
       here only to find whether an operator function is a candidate:
       with one, not supported (below); with none, their operands are
       contextually converted to bool ([over.match.oper]/7,
       [expr.unary.op]/9, [expr.log.and]/1, [expr.log.or]/1) by the
       built-in operator, not by this function */
    bool logical = op == preprocessing_op_or_punc::EXCLAMATION ||
                   op == preprocessing_op_or_punc::AMPERSAND_AMPERSAND ||
                   op == preprocessing_op_or_punc::BAR_BAR;
    bool converts = false;
    for (const expression_meaning &e : operands) {
        const struct type &t = type_table[e.type.unqualified()];
        if (t.kind == type_kind::class_type &&
            converts_to_non_class(t.entity, logical))
            converts = true;
    }
    /* [over.call.object]/2: a captureless lambda's surrogate call function
       ([expr.prim.lambda.closure]/7) has its const function call
       operator's parameters, and the operator's object binds without the
       surrogate's user-defined conversion ([over.ics.rank]/3.3): never the
       better candidate */
    if (converts && op == preprocessing_op_or_punc::LEFT_PARENTHESIS) {
        const struct type &t = type_table[operands[0].type.unqualified()];
        if (t.kind == type_kind::class_type && entity_table[t.entity].lambda &&
            entity_table[entity_table[t.entity].call_operator].member_cv ==
                cv_const) {
            converts = false;
        } else if (t.kind == type_kind::class_type &&
                   entity_table[t.entity].kind == entity_kind::class_type) {
            /* a class derived from closure types (the "overloaded"
               visitor): the same for each, when the class's function call
               operators include theirs */
            lookup_result found = lookup_qualified(t.entity, operator_name(op),
                                                   lookup_filter::any);
            if (found.entity && !found.ambiguous && !found.unknown) {
                owned_array<entity_index> calls;
                overload_set(found.entity, calls);
                if (call_surrogates_lose(t.entity, calls))
                    converts = false;
            }
        }
    }
    /* [over.match.oper]/3.3, [over.built]: the built-in candidates of = are
       operator=(L VQ &, R) of arithmetic or pointer L; no conversion binds
       the class left operand to a reference to one ([over.match.oper]/4,
       [over.ics.ref]), so none is viable */
    if (converts && op == preprocessing_op_or_punc::EQUAL &&
        type_table[operands[0].type.unqualified()].kind ==
            type_kind::class_type)
        converts = false;
    /* [over.built]/3-4, /18-19: ++ -- and the compound assignments'
       built-in candidates take their left operand by L VQ &: a class
       operand whose conversion functions return no lvalue reference binds
       to none, so only the right operand can make one viable */
    if (converts && !logical &&
        type_table[operands[0].type.unqualified()].kind ==
            type_kind::class_type) {
        switch (op) {
        case preprocessing_op_or_punc::PLUS_PLUS:
        case preprocessing_op_or_punc::MINUS_MINUS:
        case preprocessing_op_or_punc::PLUS_EQUAL:
        case preprocessing_op_or_punc::MINUS_EQUAL:
        case preprocessing_op_or_punc::STAR_EQUAL:
        case preprocessing_op_or_punc::SLASH_EQUAL:
        case preprocessing_op_or_punc::PERCENT_EQUAL:
        case preprocessing_op_or_punc::LESS_LESS_EQUAL:
        case preprocessing_op_or_punc::GREATER_GREATER_EQUAL:
        case preprocessing_op_or_punc::AMPERSAND_EQUAL:
        case preprocessing_op_or_punc::CARET_EQUAL:
        case preprocessing_op_or_punc::BAR_EQUAL:
            if (!converts_to_lvalue_reference(
                    type_table[operands[0].type.unqualified()].entity)) {
                converts = false;
                for (size_t i = 1; i < operands.size(); i++) {
                    const struct type &t =
                        type_table[operands[i].type.unqualified()];
                    if (t.kind == type_kind::class_type &&
                        converts_to_non_class(t.entity, false))
                        converts = true;
                }
            }
            break;
        default:
            break;
        }
    }
    /* [over.built]: a built-in binary operator (other than , and ->*) requires
       every class-type operand to convert to a non-class type; if any class
       operand cannot, no built-in candidate is viable */
    if (converts && operands.size() == 2 &&
        op != preprocessing_op_or_punc::COMMA &&
        op != preprocessing_op_or_punc::ARROW_STAR) {
        for (const expression_meaning &operand : operands) {
            const struct type &t = type_table[operand.type.unqualified()];
            if (t.kind == type_kind::class_type &&
                !converts_to_non_class(t.entity, logical)) {
                converts = false;
                break;
            }
        }
    }
    /* [over.match.oper], Table 15: a=b, a[b] and (by [over.call.object]) a(b)
       have member candidates alone: a of class type */
    bool member_only = op == preprocessing_op_or_punc::EQUAL ||
                       op == preprocessing_op_or_punc::LEFT_BRACKET ||
                       op == preprocessing_op_or_punc::LEFT_PARENTHESIS ||
                       op == preprocessing_op_or_punc::ARROW;
    /* a prefix unary operator: the built-in candidates' only argument needs a
       user-defined conversion, so a user-defined candidate whose parameter
       takes the operand by a standard conversion beats them all (checked
       below once it is chosen) */
    bool unary_prefix = operands.size() == 1 && !postfix;
    if (converts && !logical && !unary_prefix &&
        (operands.size() != 2 || member_only ||
         op == preprocessing_op_or_punc::SPACESHIP))
        return not_supported(n, "over.built");
    if (member_only && type_table[operands[0].type.unqualified()].kind !=
                           type_kind::class_type)
        return analysis_result::well_formed;
    if (operators_held) {
        /* (the detail names the first declaration not analysed that may
           declare an operator function: --why follows it) */
        char at[256] = "a declaration not analysed";
        if (operators_held_at) {
            source_position where = tokens.position(operators_held_at);
            snprintf(at, sizeof at,
                     "an operator function may be declared by the "
                     "declaration at %s:%u:%u, not analysed",
                     where.file_name, where.line, where.column);
        }
        analysis_result refused =
            not_supported_because(n, "over.match.oper", at);
        unsupported[unsupported.size() - 1].holder = operators_holder;
        return refused;
    }
    owned_array<overload_candidate> candidates;
    auto stripped = [&](qualified_type t) {
        const struct type &r = type_table[t.unqualified()];
        if (r.kind == type_kind::lvalue_reference ||
            r.kind == type_kind::rvalue_reference)
            t = r.operand;
        return t.unqualified();
    };
    /* the candidates named name for the operands, in their order or (for a
       synthesized candidate) reversed */
    auto collect = [&](uint32_t name, bool rewritten,
                       bool reversed) -> analysis_result {
        size_t count = operands.size();
        auto operand = [&](size_t i) -> const expression_meaning & {
            if (reversed)
                return operands[count - 1 - i];
            return operands[i];
        };
        /* /3.1: "If T1 is a complete class type or a class currently being
           defined, the set of member candidates is the result of the
           qualified lookup of T1::operator@ ([over.call.func]); otherwise,
           the set of member candidates is empty." */
        owned_array<entity_index> members, others;
        const struct type &t1 = type_table[operand(0).type.unqualified()];
        if (t1.kind == type_kind::class_type) {
            entity_index c = t1.entity;
            require_complete(c);
            if (entity_table[c].kind != entity_kind::class_type ||
                !entity_table[c].size)
                return not_supported(n, "over.match.oper");
            lookup_result found = lookup_qualified(c, name, lookup_filter::any);
            entity_index templates = 0;
            if (found.unknown && !found.ambiguous && !found.entity &&
                member_function_template_of(found.unknown_entity))
                templates = found.unknown_entity;
            else if (found.entity && !found.ambiguous)
                templates = member_templates_of(found.entity);
            if (templates) {
                /* [temp.mem]/1: member operator function templates, their
                   specializations deduced from the other operands
                   ([temp.over]/1) member candidates */
                owned_array<expression_meaning> rest;
                for (size_t i = 1; i < count; i++)
                    rest.append(operand(i));
                size_t first = candidates.size();
                VTRY(r, member_template_candidates(
                            n, templates, true, rest.data(),
                            static_cast<uint32_t>(rest.size()), candidates));
                for (size_t i = first; i < candidates.size(); i++) {
                    candidates[i].rewritten = rewritten;
                    candidates[i].reversed = reversed;
                }
            } else if (found.unknown || found.ambiguous) {
                return not_supported(n, "over.match.oper");
            }
            if (found.entity) {
                if (entity_table[found.entity].kind != entity_kind::function ||
                    (!entity_table[found.entity].nonstatic_member &&
                     op != preprocessing_op_or_punc::LEFT_PARENTHESIS &&
                     op != preprocessing_op_or_punc::LEFT_BRACKET))
                    return not_supported(n, "over.match.oper");
                overload_set(found.entity, members);
            }
        }
        /* /3.2: "The set of non-member candidates is the result of the
           unqualified lookup of operator@ in the context of the expression
           according to the usual rules for name lookup in unqualified
           function calls ([basic.lookup.argdep]) except that all member
           functions are ignored." */
        if (member_only) {
            function_candidates(members, true, candidates);
            return analysis_result::well_formed;
        }
        lookup_result found =
            lookup_unqualified(name, lookup_filter::non_members);
        /* [temp.over]/1: operator function templates by that name, whose
           specializations deduced from the operands are candidates too */
        owned_array<entity_index> held_names;
        if (found.unknown && !found.ambiguous &&
            function_template_of(found.unknown_entity))
            held_names.append(found.unknown_entity);
        else if (found.unknown || found.ambiguous)
            return not_supported(n, "over.match.oper");
        owned_array<entity_index> set;
        if (found.entity) {
            const entity &x = entity_table[found.entity];
            if (x.kind != entity_kind::function || !is_namespace(x.enclosing) ||
                !is_namespace(found.found_in))
                return not_supported(n, "over.match.oper");
            set.append(found.entity);
        }
        owned_array<qualified_type> types;
        for (const expression_meaning &e : operands)
            types.append(e.type);
        entity_index known = 0;
        if (!held_names.empty())
            known = held_names[0];
        if (!argument_dependent_lookup(name, types, set, known, &held_names))
            return not_supported(n, "basic.lookup.argdep");
        if (!held_names.empty()) {
            owned_array<uint32_t> group;
            owned_array<entity_index> associated_classes, associated_spaces;
            if (!associated_namespaces(types, associated_classes,
                                       associated_spaces))
                return not_supported(n, "basic.lookup.argdep");
            for (entity_index held : held_names) {
                owned_array<uint32_t> one;
                function_template_overloads(held, one);
                for (uint32_t t : one) {
                    const function_template &entry = function_templates[t - 1];
                    bool visible = !entry.hidden_friend;
                    if (entry.hidden_friend)
                        for (entity_index cls : associated_classes)
                            if (cls == entry.friend_class)
                                visible = true;
                    if (visible)
                        group.append(t);
                }
                for (const template_name_function &x : template_name_functions)
                    if (x.name == held)
                        set.append(x.function);
            }
            owned_array<expression_meaning> in_order;
            for (size_t i = 0; i < count; i++)
                in_order.append(operand(i));
            size_t first = candidates.size();
            VTRY(r,
                 template_candidates(n, group, in_order.data(),
                                     static_cast<uint32_t>(count), candidates));
            for (size_t i = first; i < candidates.size(); i++) {
                candidates[i].rewritten = rewritten;
                candidates[i].reversed = reversed;
            }
        }
        for (entity_index e : set)
            overload_set(e, others);
        size_t first_member = candidates.size();
        function_candidates(members, true, candidates);
        for (size_t i = first_member; i < candidates.size(); i++) {
            candidates[i].rewritten = rewritten;
            candidates[i].reversed = reversed;
        }
        /* "However, if no operand has a class type, only those non-member
           functions in the lookup set that have a first parameter of type T1
           or "reference to cv T1", when T1 is an enumeration type, or (if
           there is a right operand) a second parameter of type T2 or
           "reference to cv T2", when T2 is an enumeration type, are
           candidate functions." */
        for (entity_index e : others) {
            const entity &x = entity_table[e];
            if (!any_class) {
                const struct type &f = type_table[x.type.unqualified()];
                const qualified_type *parameters =
                    type_table.parameters(x.type.unqualified());
                bool takes = false;
                for (size_t i = 0; i < count && i < f.parameter_count; i++)
                    if (type_table[operand(i).type.unqualified()].kind ==
                            type_kind::enumeration &&
                        stripped(parameters[i]) ==
                            operand(i).type.unqualified())
                        takes = true;
                if (!takes)
                    continue;
            }
            overload_candidate c;
            c.function = e;
            c.type = x.type;
            c.rewritten = rewritten;
            c.reversed = reversed;
            candidates.append(c);
        }
        return analysis_result::well_formed;
    };
    analysis_result r = collect(operator_name(op), false, false);
    /* /3.4: "For the != operator ([expr.eq]), the rewritten candidates
       include all non-rewritten candidates for the expression x == y.  For
       the equality operators, the rewritten candidates also include a
       synthesized candidate, with the order of the two parameters reversed,
       for each non-rewritten candidate for the expression y == x." */
    bool rewrites = tokens.files.behavior.has(
                        language_feature::three_way_comparison_operator) &&
                    (op == preprocessing_op_or_punc::EQUAL_EQUAL ||
                     op == preprocessing_op_or_punc::EXCLAMATION_EQUAL);
    uint32_t equality = operator_name(preprocessing_op_or_punc::EQUAL_EQUAL);
    if (r == analysis_result::well_formed && rewrites &&
        op == preprocessing_op_or_punc::EXCLAMATION_EQUAL)
        r = collect(equality, true, false);
    if (r == analysis_result::well_formed && rewrites)
        r = collect(equality, true, true);
    /* "For the relational ([expr.rel]) operators, the rewritten candidates
       include all non-rewritten candidates for the expression x <=> y.  For
       the relational ([expr.rel]) and three-way comparison ([expr.spaceship])
       operators, the rewritten candidates also include a synthesized
       candidate, with the order of the two parameters reversed, for each
       non-rewritten candidate for the expression y <=> x." */
    bool three_way =
        tokens.files.behavior.has(
            language_feature::three_way_comparison_operator) &&
        (op == preprocessing_op_or_punc::SPACESHIP ||
         (is_comparison(op) && op != preprocessing_op_or_punc::EQUAL_EQUAL &&
          op != preprocessing_op_or_punc::EXCLAMATION_EQUAL));
    uint32_t spaceship = operator_name(preprocessing_op_or_punc::SPACESHIP);
    if (r == analysis_result::well_formed && three_way &&
        op != preprocessing_op_or_punc::SPACESHIP)
        r = collect(spaceship, true, false);
    if (r == analysis_result::well_formed && three_way)
        VTRY_SET(r, collect(spaceship, true, true));
    if (candidates.empty()) {
        if (converts && !logical && !member_only && operands.size() == 2) {
            /* [over.built]: no user-defined operator was found; convert class
               operands via their unique non-class conversion target and apply
               the built-in binary operator */
            auto convert_operand =
                [&](expression_meaning &op_val) -> analysis_result {
                const struct type &ot = type_table[op_val.type.unqualified()];
                if (ot.kind != type_kind::class_type)
                    return analysis_result::well_formed;
                owned_array<entity_index> fns;
                if (!visible_conversion_functions(ot.entity, fns))
                    return not_supported(n, "over.built");
                qualified_type target;
                for (entity_index f : fns) {
                    if (entity_table[f].explicit_)
                        continue;
                    qualified_type rt = conversion_function_result(f).type;
                    const struct type &rtr = type_table[rt.unqualified()];
                    if (rtr.kind == type_kind::class_type)
                        continue;
                    qualified_type unq =
                        qualified_type::make(rt.unqualified(), cv_none);
                    if (!target.unqualified())
                        target = unq;
                    else if (target != unq)
                        return not_supported(n, "over.built");
                }
                if (!target.unqualified())
                    return not_supported(n, "over.built");
                bool converted = false;
                VTRY(cr, conversion_function_call(n, op_val, target, false,
                                                  converted));
                if (!converted)
                    return not_supported(n, "over.built");
                return analysis_result::well_formed;
            };
            expression_meaning lhs = operands[0];
            expression_meaning rhs = operands[1];
            VTRY(lr, convert_operand(lhs));
            VTRY(rr, convert_operand(rhs));
            VTRY(br, builtin_binary(n, at, op, lhs, rhs, m));
            handled = true;
        }
        return analysis_result::well_formed;
    }
    if (converts && (!unary_prefix || candidates.empty()))
        return not_supported(n, "over.built");
    bool deleted_copy = false;
    if (op == preprocessing_op_or_punc::EQUAL) {
        /* [class.copy.assign]/2: "If the class definition does not
           explicitly declare a copy assignment operator, one is declared
           implicitly": a candidate too, which analysis does not form */
        entity_index c = type_table[operands[0].type.unqualified()].entity;
        bool copy = false;
        for (const overload_candidate &k : candidates)
            if (copy_assignment_operator(k.function, c))
                copy = true;
        if (!copy) {
            /* [class.copy.assign]/2: a class that declares a move assignment
               operator and no copy assignment operator has its implicit copy
               assignment operator defined as deleted; it is a candidate
               ([over.match.funcs]) taking const X&, which is settled below
               for a right operand of the class's own type */
            bool declares_move = false;
            for (const overload_candidate &k : candidates)
                if (!k.rewritten && !k.reversed &&
                    move_assignment_operator(k.function, c))
                    declares_move = true;
            /* a copy assignment operator declared defaulted has no
               candidate here, and is no implicit one */
            if (!declares_move || entity_table[c].defaulted_copy_assignment ||
                operands.size() != 2 ||
                operands[1].type.unqualified() !=
                    operands[0].type.unqualified())
                return not_supported(n, "class.copy.assign");
            deleted_copy = true;
        }
    }
    /* [over.match.oper]/2, Table 15: a@ is "(a).operator@ (0)" or
       "operator@(a, 0)" */
    owned_array<expression_meaning> arguments;
    owned_array<SyntaxElement> argument_syntax;
    for (size_t i = 0; i < operands.size(); i++) {
        arguments.append(operands[i]);
        argument_syntax.append(syntax[i]);
    }
    if (postfix) {
        expression_meaning zero =
            integer_constant(fundamental_type::int_, 0, at);
        arguments.append(zero);
        argument_syntax.append(SyntaxElement{});
    }
    size_t chosen = 0;
    bool none_viable = false;
    r = best_viable_function(n, candidates, nullptr, arguments, chosen,
                             &none_viable);
    if (r == analysis_result::well_formed && deleted_copy) {
        /* the deleted copy assignment operator is viable for the left
           operand and the class's own type on the right: with no other
           candidate viable it is selected; a viable move assignment
           operator binds a non-const rvalue better ([over.ics.rank]/3.2.3),
           any other choice is not analysed */
        bool movable = operands[1].category != value_category::lvalue &&
                       !(operands[1].type.cv() & cv_const);
        if (none_viable || (operands[0].type.cv() & cv_const))
            return ill_formed(n,
                              "the implicitly-declared copy assignment "
                              "operator is defined as deleted",
                              "class.copy.assign");
        if (!movable)
            return not_supported(n, "class.copy.assign");
    }
    if (r == analysis_result::well_formed && none_viable && converts)
        /* a built-in candidate may be viable through the conversion */
        return not_supported(n, "over.built");
    if (r == analysis_result::well_formed && none_viable && member_only &&
        has_conversion_functions(
            type_table[operands[0].type.unqualified()].entity))
        /* no built-in operator either: whether a conversion function makes
           one viable ([over.match.oper]/3.3, [over.built]) is not analysed;
           a class with none has no built-in candidate */
        return not_supported(n, "over.match.oper");
    if (r == analysis_result::well_formed && none_viable &&
        op == preprocessing_op_or_punc::EQUAL &&
        (operands[0].type.cv() & cv_const))
        /* the copy assignment operator was found above, so the class
           declares one and ([class.copy.assign]/2-3) no implicit operator=
           is a candidate besides: with a const left operand, whose
           implicit object parameter cannot bind ([over.match.viable]/4),
           none viable is the expression's error; a built-in = has no class
           left operand */
        return ill_formed(n, "no viable overloaded operator= for the operands",
                          "over.match.oper");
    if (r != analysis_result::well_formed || none_viable)
        /* /9: none viable, the built-in operator's meaning (whose rules
           reject an operand of class type) */
        return r;
    const overload_candidate &c = candidates[chosen];
    entity_index f = c.function;
    if (converts && !c.member) {
        /* [over.match.best]/2: a non-member's parameter reached by a
           user-defined conversion is no better than the built-in's */
        if (c.template_index)
            return not_supported(n, "over.built");
        conversion_sequence s = implicit_conversion_sequence(
            arguments[0],
            type_table.parameters(entity_table[f].type.unqualified())[0], true);
        if (s.kind != conversion_sequence::form::standard)
            return not_supported(n, "over.built");
    }
    if (c.template_index) {
        /* [temp.inst]/4: the specialization chosen is referenced: its
           definition is instantiated (a candidate's declaration only was,
           in an unevaluated context) */
        owned_array<template_argument> deduced;
        owned_array<expression_meaning> in_order;
        for (size_t i = 0; i < arguments.size(); i++) {
            if (c.reversed)
                in_order.append(arguments[arguments.size() - 1 - i]);
            else
                in_order.append(arguments[i]);
        }
        /* (a member's object is no argument to deduce from) */
        size_t skip = c.member;
        r = deduce_from_call(n, c.template_index, in_order.data() + skip,
                             static_cast<uint32_t>(in_order.size() - skip),
                             deduced);
        if (r == analysis_result::well_formed)
            VTRY_SET(r, instantiate(
                            n, function_templates[c.template_index - 1].entity,
                            deduced.data(),
                            static_cast<uint32_t>(deduced.size()), f));
    }
    const entity &x = entity_table[f];
    const struct type &type = type_table[x.type.unqualified()];
    const qualified_type *parameters =
        type_table.parameters(x.type.unqualified());
    /* the arguments in the order of the function's parameters: y, x for a
       synthesized candidate (/9: "y == x") */
    owned_array<expression_meaning> ordered;
    owned_array<SyntaxElement> ordered_syntax;
    for (size_t i = 0; i < arguments.size(); i++) {
        size_t k = i;
        if (c.reversed)
            k = arguments.size() - 1 - i;
        ordered.append(arguments[k]);
        ordered_syntax.append(argument_syntax[k]);
    }
    if (!any_class) {
        /* /3.3: the built-in candidates of an enumeration operand convert
           it by an integral promotion or conversion, except the
           comparisons' of the enumeration type T itself (bool
           operator<(T, T), [over.built]/16), which are left out when "the
           same parameter-type-list as any non-member candidate": chosen
           only when better than any of them */
        for (size_t i = 0; i < ordered.size(); i++) {
            conversion_sequence s =
                implicit_conversion_sequence(ordered[i], parameters[i], true);
            if (s.kind != conversion_sequence::form::standard || s.rank != 0)
                return not_supported(n, "over.built");
        }
        if (is_comparison(op) && operands.size() == 2 &&
            operands[0].type.unqualified() == operands[1].type.unqualified() &&
            (type.parameter_count != 2 ||
             stripped(parameters[0]) != operands[0].type.unqualified() ||
             stripped(parameters[1]) != operands[1].type.unqualified()))
            return not_supported(n, "over.built");
    }
    bool from_three_way =
        c.rewritten && x.implemented == preprocessing_op_or_punc::SPACESHIP;
    if (from_three_way && !any_class)
        /* an enumeration's built-in candidates against a rewritten one */
        return not_supported(n, "over.match.oper");
    if (c.rewritten && !from_three_way) {
        /* /9: "If a rewritten operator== candidate is selected by overload
           resolution for an operator @, its return type shall be cv bool" */
        qualified_type result =
            qualified_type::make(type.operand.unqualified(), cv_none);
        if (fundamental_of(result) != fundamental_type::bool_) {
            if (is_integral(result) || enumeration_of(result))
                /* [impl.clang] an integral or unscoped enumeration type is
                   accepted, with a warning */
                return not_supported(n, "over.match.oper");
            return ill_formed(n,
                              "a rewritten operator== candidate selected by "
                              "overload resolution shall have the return type "
                              "cv bool",
                              "over.match.oper");
        }
    }
    entity_index object_class =
        type_table[ordered[0].type.unqualified()].entity;
    if (c.member) {
        if (!accessible(f, object_class))
            /* [class.access]/4: "access control is applied to the function
               selected by overload resolution" */
            return ill_formed(n,
                              "a private or protected member's name can be "
                              "used only by members and friends of the class "
                              "in which it is declared",
                              "class.access");
    }
    /* [dcl.fct.def.delete]/2 */
    VTRY_SET(r, deleted_use(at, f));
    expression_meaning callee;
    callee.type = x.type;
    callee.category = value_category::lvalue;
    callee.evaluation = constness::constant;
    callee.entity = f;
    uint32_t object = 0;
    entity_index member = 0;
    if (c.member) {
        /* [over.binary]/1: "If a member function is selected, the expression
           is interpreted as x . operator @ ( y )" */
        expression_meaning &e = ordered[0];
        if (e.category == value_category::prvalue &&
            entity_table[object_class].destructor &&
            entity_table[entity_table[object_class].destructor]
                .member_body_deferred)
            /* the temporary's destructor is an instantiated member's
               definition still to be made ([temp.inst]/4), whose errors
               this expression's declaration owns: not analysed */
            return not_supported(n, "class.temporary");
        expression_meaning address;
        address.type =
            qualified_type::make(type_table.pointer_to(e.type), cv_none);
        address.category = value_category::prvalue;
        address.evaluation = constness::not_constant;
        address.node =
            expression_node(semantic_kind::address, address, at, e.node);
        if (x.nonstatic_member && x.enclosing != object_class) {
            /* [over.match.funcs]/4: the implicit object argument
               designates the selected function's base class subobject. */
            qualified_type base = qualified_type::make(
                entity_table[x.enclosing].type.unqualified(), e.type.cv());
            qualified_type pointer =
                qualified_type::make(type_table.pointer_to(base), cv_none);
            VTRY_SET(r, pointer_to_base(at, object_class, x.enclosing, pointer,
                                        address));
        }
        object = address.node;
        member = f;
        if (!x.nonstatic_member) {
            /* [over.match.funcs]/4: a static member function: the object
               expression is evaluated and discarded */
            callee.node = expression_node(semantic_kind::entity, callee, at, f);
            callee.node = expression_node(semantic_kind::comma, callee, at,
                                          e.node, callee.node);
            object = 0;
            member = 0;
        }
    } else {
        /* "Otherwise, if a non-member function is selected, the expression
           is interpreted as operator @ ( x , y )" */
        callee.node = expression_node(semantic_kind::entity, callee, at, f);
    }
    /* the arguments for the parameters: a member's object is not one */
    owned_array<expression_meaning> call_arguments;
    owned_array<SyntaxElement> call_syntax;
    for (size_t i = 0; i < ordered.size(); i++) {
        if (c.member && i == 0)
            continue;
        call_arguments.append(ordered[i]);
        call_syntax.append(ordered_syntax[i]);
    }
    VTRY_SET(r, call_with(n, callee, x.type, object, member, call_syntax,
                          call_arguments, m));
    if (from_three_way) {
        /* /8: "If a rewritten operator<=> candidate is selected by overload
           resolution for an operator @, x @ y is interpreted as 0 @ (y <=>
           x) if the selected candidate is a synthesized candidate with
           reversed order of parameters, or (x <=> y) @ 0 otherwise, using
           the selected rewritten operator<=> candidate." */
        expression_meaning zero =
            integer_constant(fundamental_type::int_, 0, at);
        zero.zero_literal = true;
        expression_meaning three = m;
        expression_meaning *left = &three, *right = &zero;
        if (c.reversed) {
            left = &zero;
            right = &three;
        }
        if (fundamental_of(three.type) == fundamental_type::none) {
            owned_array<expression_meaning> cmp_ops;
            owned_array<SyntaxElement> cmp_syn;
            cmp_ops.append(*left);
            cmp_ops.append(*right);
            cmp_syn.append(SyntaxElement{});
            cmp_syn.append(SyntaxElement{});
            bool cmp_handled = false;
            VTRY_SET(r, overloaded_operator(n, at, op, false, cmp_syn, cmp_ops,
                                            cmp_handled, m));
            if (!cmp_handled)
                return not_supported(n, "over.match.oper");
            handled = true;
            return analysis_result::well_formed;
        }
        r = operand_value(n, *left);
        if (r == analysis_result::well_formed)
            r = operand_value(n, *right);
        if (r == analysis_result::well_formed)
            r = comparison(n, op, *left, *right, m);
        VTRY_SET(r, binary_built(r, at, op, *left, *right, m));
    }
    if (c.rewritten && op == preprocessing_op_or_punc::EXCLAMATION_EQUAL) {
        /* /9: x != y is "!(x == y)", or "!(y == x)" */
        expression_meaning negated;
        negated.type = qualified_type::make(
            type_table.fundamental(fundamental_type::bool_), cv_none);
        negated.category = value_category::prvalue;
        negated.evaluation = constness::not_constant;
        negated.node = expression_node(
            semantic_kind::unary, negated, at, m.node, 0,
            static_cast<uint8_t>(preprocessing_op_or_punc::EXCLAMATION));
        m = negated;
    }
    handled = true;
    return analysis_result::well_formed;
}

analysis_result semantic_analysis::unary_operator_function(
    SyntaxRef n, token_index at, preprocessing_op_or_punc op, bool postfix,
    SyntaxElement s, expression_meaning &operand, bool &handled)
{
    owned_array<SyntaxElement> syntax;
    owned_array<expression_meaning> operands;
    syntax.append(s);
    operands.append(operand);
    expression_meaning result;
    analysis_result r = overloaded_operator(n, at, op, postfix, syntax,
                                            operands, handled, result);
    if (r == analysis_result::well_formed && handled)
        operand = result;
    return r;
}

analysis_result semantic_analysis::binary_operator_function(
    SyntaxRef n, token_index at, preprocessing_op_or_punc op,
    SyntaxElement left_syntax, const expression_meaning &left,
    SyntaxElement right_syntax, const expression_meaning &right, bool &handled,
    expression_meaning &m)
{
    owned_array<SyntaxElement> syntax;
    owned_array<expression_meaning> operands;
    syntax.append(left_syntax);
    operands.append(left);
    syntax.append(right_syntax);
    operands.append(right);
    return overloaded_operator(n, at, op, false, syntax, operands, handled, m);
}

} // namespace cplusplus
