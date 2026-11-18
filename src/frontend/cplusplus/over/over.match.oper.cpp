/*
 * over.match.oper.cpp: [over.match.oper] Operators in expressions.
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
 * conversion functions are not formed (unsupported, but for ! && ||
 * without an operator function); for an operand of another class none is
 * viable; for an
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

analysis_result semantic_analysis::overloaded_operator(
    node_index n, token_index at, preprocessing_op_or_punc op, bool postfix,
    const owned_array<syntax_child> &syntax,
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
    /* an operand of a class with conversion functions: built-in candidates
       ([over.built]) may be viable, which are not formed; ! && || alone
       when no operator function is declared, whose operands are
       contextually converted to bool ([over.match.oper]/7, [expr.unary.op]/9,
       [expr.log.and]/1, [expr.log.or]/1) */
    bool converts = false;
    for (const expression_meaning &e : operands) {
        const struct type &t = type_table[e.type.unqualified()];
        if (t.kind == type_kind::class_type &&
            has_conversion_functions(t.entity))
            converts = true;
    }
    bool logical = op == preprocessing_op_or_punc::EXCLAMATION ||
                   op == preprocessing_op_or_punc::AMPERSAND_AMPERSAND ||
                   op == preprocessing_op_or_punc::BAR_BAR;
    if (converts && !logical)
        return not_supported(n, "over.built");
    /* [over.match.oper], Table 15: a=b, a[b] and (by [over.call.object]) a(b)
       have member candidates alone: a of class type */
    bool member_only = op == preprocessing_op_or_punc::EQUAL ||
                       op == preprocessing_op_or_punc::LEFT_BRACKET ||
                       op == preprocessing_op_or_punc::LEFT_PARENTHESIS ||
                       op == preprocessing_op_or_punc::ARROW;
    if (member_only && type_table[operands[0].type.unqualified()].kind !=
                           type_kind::class_type)
        return analysis_result::well_formed;
    if (operators_held)
        return not_supported(n, "over.match.oper");
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
            if (entity_table[c].kind != entity_kind::class_type ||
                !entity_table[c].size)
                return not_supported(n, "over.match.oper");
            lookup_result found = lookup_qualified(c, name, lookup_filter::any);
            if (found.unknown || found.ambiguous)
                return not_supported(n, "over.match.oper");
            if (found.entity) {
                if (entity_table[found.entity].kind != entity_kind::function ||
                    !entity_table[found.entity].nonstatic_member)
                    return not_supported(n, "over.match.oper");
                overload_set(found.entity, members);
            }
        }
        /* /3.2: "The set of non-member candidates is the result of the
           unqualified lookup of operator@ in the context of the expression
           according to the usual rules for name lookup in unqualified
           function calls ([basic.lookup.argdep]) except that all member
           functions are ignored."  A member found is not passed over: not
           analysed */
        if (member_only) {
            for (entity_index e : members) {
                overload_candidate c;
                c.function = e;
                c.type = entity_table[e].type;
                c.member = true;
                candidates.append(c);
            }
            return analysis_result::well_formed;
        }
        lookup_result found = lookup_unqualified(name, lookup_filter::any);
        if (found.unknown || found.ambiguous)
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
        if (!argument_dependent_lookup(name, types, set))
            return not_supported(n, "basic.lookup.argdep");
        for (entity_index e : set)
            overload_set(e, others);
        for (entity_index e : members) {
            overload_candidate c;
            c.function = e;
            c.type = entity_table[e].type;
            c.member = true;
            c.rewritten = rewritten;
            c.reversed = reversed;
            candidates.append(c);
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
        r = collect(spaceship, true, true);
    if (r != analysis_result::well_formed)
        return r;
    if (candidates.empty())
        return analysis_result::well_formed;
    if (converts)
        return not_supported(n, "over.built");
    if (op == preprocessing_op_or_punc::EQUAL) {
        /* [class.copy.assign]/2: "If the class definition does not
           explicitly declare a copy assignment operator, one is declared
           implicitly": a candidate too, which analysis does not form */
        entity_index c = type_table[operands[0].type.unqualified()].entity;
        bool copy = false;
        for (const overload_candidate &k : candidates)
            if (copy_assignment_operator(k.function, c))
                copy = true;
        if (!copy)
            return not_supported(n, "class.copy.assign");
    }
    /* [over.match.oper]/2, Table 15: a@ is "(a).operator@ (0)" or
       "operator@(a, 0)" */
    owned_array<expression_meaning> arguments;
    owned_array<syntax_child> argument_syntax;
    for (size_t i = 0; i < operands.size(); i++) {
        arguments.append(operands[i]);
        argument_syntax.append(syntax[i]);
    }
    if (postfix) {
        expression_meaning zero =
            integer_constant(fundamental_type::int_, 0, at);
        arguments.append(zero);
        argument_syntax.append(syntax_child{});
    }
    size_t chosen = 0;
    bool none_viable = false;
    r = best_viable_function(n, candidates, nullptr, arguments, chosen,
                             &none_viable);
    if (r == analysis_result::well_formed && none_viable && member_only)
        /* no built-in operator either: whether a conversion makes one
           viable is not analysed */
        return not_supported(n, "over.match.oper");
    if (r != analysis_result::well_formed || none_viable)
        /* /9: none viable, the built-in operator's meaning (whose rules
           reject an operand of class type) */
        return r;
    const overload_candidate &c = candidates[chosen];
    entity_index f = c.function;
    const entity &x = entity_table[f];
    const struct type &type = type_table[x.type.unqualified()];
    const qualified_type *parameters =
        type_table.parameters(x.type.unqualified());
    /* the arguments in the order of the function's parameters: y, x for a
       synthesized candidate (/9: "y == x") */
    owned_array<expression_meaning> ordered;
    owned_array<syntax_child> ordered_syntax;
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
    if (from_three_way &&
        (!any_class || op == preprocessing_op_or_punc::SPACESHIP))
        /* an enumeration's built-in candidates against a rewritten one;
           0 <=> (y <=> x), whose built-in <=> is not analysed */
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
    const struct type &t1 = type_table[ordered[0].type.unqualified()];
    if (c.member) {
        entity_index object_class = t1.entity;
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
    r = deleted_use(at, f);
    if (r != analysis_result::well_formed)
        return r;
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
            entity_table[t1.entity].destructor)
            /* the temporary's destruction ([class.temporary]/4) */
            return not_supported(n, "class.temporary");
        expression_meaning address;
        address.type =
            qualified_type::make(type_table.pointer_to(e.type), cv_none);
        address.category = value_category::prvalue;
        address.evaluation = constness::not_constant;
        address.node =
            expression_node(semantic_kind::address, address, at, e.node);
        object = address.node;
        member = f;
    } else {
        /* "Otherwise, if a non-member function is selected, the expression
           is interpreted as operator @ ( x , y )" */
        callee.node = expression_node(semantic_kind::entity, callee, at, f);
    }
    /* the arguments for the parameters: a member's object is not one */
    owned_array<expression_meaning> call_arguments;
    owned_array<syntax_child> call_syntax;
    for (size_t i = 0; i < ordered.size(); i++) {
        if (c.member && i == 0)
            continue;
        call_arguments.append(ordered[i]);
        call_syntax.append(ordered_syntax[i]);
    }
    r = call_with(n, callee, x.type, object, member, call_syntax,
                  call_arguments, m);
    if (r != analysis_result::well_formed)
        return r;
    if (from_three_way) {
        /* /8: "If a rewritten operator<=> candidate is selected by overload
           resolution for an operator @, x @ y is interpreted as 0 @ (y <=>
           x) if the selected candidate is a synthesized candidate with
           reversed order of parameters, or (x <=> y) @ 0 otherwise, using
           the selected rewritten operator<=> candidate." */
        if (fundamental_of(m.type) == fundamental_type::none)
            /* a comparison category type of <compare> */
            return not_supported(n, "over.match.oper");
        expression_meaning zero =
            integer_constant(fundamental_type::int_, 0, at);
        zero.zero_literal = true;
        expression_meaning three = m;
        expression_meaning *left = &three, *right = &zero;
        if (c.reversed) {
            left = &zero;
            right = &three;
        }
        r = operand_value(n, *left);
        if (r == analysis_result::well_formed)
            r = operand_value(n, *right);
        if (r == analysis_result::well_formed)
            r = comparison(n, op, *left, *right, m);
        r = binary_built(r, at, op, *left, *right, m);
        if (r != analysis_result::well_formed)
            return r;
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
    node_index n, token_index at, preprocessing_op_or_punc op, bool postfix,
    syntax_child s, expression_meaning &operand, bool &handled)
{
    owned_array<syntax_child> syntax;
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
    node_index n, token_index at, preprocessing_op_or_punc op,
    syntax_child left_syntax, const expression_meaning &left,
    syntax_child right_syntax, const expression_meaning &right, bool &handled,
    expression_meaning &m)
{
    owned_array<syntax_child> syntax;
    owned_array<expression_meaning> operands;
    syntax.append(left_syntax);
    operands.append(left);
    syntax.append(right_syntax);
    operands.append(right);
    return overloaded_operator(n, at, op, false, syntax, operands, handled, m);
}

} // namespace cplusplus
