/*
 * mangling/expressions-constraint.cpp: [abi:expressions] of a requires-clause, as written (doc/PLAN-mangling.md).
 */
#include "mangling-internal.h"

namespace cplusplus
{

/*
 * [abi:expressions] of a requires-clause, as far as it is written
 * with concept-ids of the template's parameters (each an
 * <unresolved-name> with its <template-args>), !, && and ||: an
 * operator's name (nt, aa, oo), then its one or two operands.
 *
 * Parentheses are not mangled.  Anything else: not mangled yet.
 */
bool itanium_mangler::expression_of_constraint(entity_index t, SyntaxElement c)
{
    if (c.is_token()) {
        if (entity_index parameter =
                analysis.constraint_parameter(t, c.token()))
            return template_param_of_constraint(t, parameter);
        if (expr_primary_of_variable(t, c))
            return true;
        /* a decimal integer-literal of type int: Li <value> E */
        owned_buffer spelling;
        analysis.token_spelling(c.token(), spelling.get());
        if (!spelling.size() || spelling.size() > 9 ||
            (spelling.data()[0] == '0' && spelling.size() > 1))
            return false;
        for (size_t i = 0; i < spelling.size(); i++)
            if (spelling.data()[i] < '0' || spelling.data()[i] > '9')
                return false;
        uint64_t value = 0;
        for (size_t i = 0; i < spelling.size(); i++)
            value =
                value * 10 + static_cast<uint64_t>(spelling.data()[i] - '0');
        expr_primary_value(fundamental_type::int_, false, value);
        return true;
    }
    switch (c.kind()) {
    case syntax_kind::constraint_logical_or_expression:
    case syntax_kind::constraint_logical_and_expression:
    case syntax_kind::logical_or_expression:
    case syntax_kind::logical_and_expression:
        if (c.size() != 3)
            return false;
        if (c.kind() == syntax_kind::constraint_logical_or_expression ||
            c.kind() == syntax_kind::logical_or_expression)
            enc.write(itanium_operator::oo_logical_or);
        else
            enc.write(itanium_operator::aa_logical_and);
        return expression_of_constraint(t, c[0]) &&
               expression_of_constraint(t, c[2]);
    case syntax_kind::primary_expression:
        if (c.size() != 3 ||
            !punctuator(c[0], preprocessing_op_or_punc::LEFT_PARENTHESIS))
            return false;
        return expression_of_constraint(t, c[1]);
    case syntax_kind::unary_expression:
        if (c.size() == 4 && c[0].is_token() &&
            punctuator(c[1], preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
            /* sizeof ( T ), T a template parameter: st <type> */
            owned_buffer spelling;
            analysis.token_spelling(c[0].token(), spelling.get());
            SyntaxElement a = c[2];
            while (a.is_node() && a.size() == 1 &&
                   a.is(syntax_kind::type_id, syntax_kind::type_specifier_seq))
                a = a[0];
            if (spelling.size() != 6 || memcmp(spelling.data(), "sizeof", 6) ||
                !a.is_token())
                return false;
            entity_index pe = analysis.constraint_parameter(t, a.token());
            enc.write(terminal::st_sizeof_type);
            return pe && template_param_of_constraint(t, pe);
        }
        /* sizeof ... ( P ), P a template parameter pack: sZ
           <template-param> */
        if (c.size() == 5 && c[0].is(standard_keyword::sizeof_) &&
            c[1].is(preprocessing_op_or_punc::ELLIPSIS) &&
            c[3].is_identifier()) {
            entity_index pe = analysis.constraint_parameter(t, c[3].token());
            /* (an expression's <template-param>: no candidate) */
            if (!pe || !analysis.constraint_parameter_is_own(t, pe))
                return false;
            sizeof_pack(analysis.entity_table[pe].value);
            return true;
        }
        if (c.size() != 2 ||
            !punctuator(c[0], preprocessing_op_or_punc::EXCLAMATION))
            return false;
        enc.write(itanium_operator::nt_logical_not);
        return expression_of_constraint(t, c[1]);
    case syntax_kind::multiplicative_expression:
    case syntax_kind::additive_expression:
    case syntax_kind::relational_expression:
    case syntax_kind::equality_expression: {
        if (c.size() != 3)
            return false;
        itanium_operator o = operator_written(c[1], 2);
        if (o == itanium_operator::none)
            return false;
        enc.write(o);
        return expression_of_constraint(t, c[0]) &&
               expression_of_constraint(t, c[2]);
    }
    case syntax_kind::requires_expression: {
        /* [abi:expressions]: requires-expression without parameter list
           ("rq" <requirement>+ "E"), simple-requirement ("X" <expression>),
           compound-requirement ("X" <expression> ["N"]
           ["R" <type-constraint>]) */
        if (c.size() != 2)
            return false;
        SyntaxElement body = c[1];
        uint32_t first = 1;
        uint32_t count = 0;
        if (body.size() >= 2)
            count = body.size() - 2;
        if (count == 1 && body[1].is(syntax_kind::requirement_seq)) {
            body = body[1];
            first = 0;
            count = body.size();
        }
        if (!count)
            return false;
        enc.write(terminal::rq_requires_expression);
        for (uint32_t i = first; i < first + count; i++) {
            SyntaxElement req = body[i];
            if (req.is(syntax_kind::simple_requirement) && req.size() >= 1) {
                enc.write(terminal::X_requirement);
                if (!expression_of_written(req[0]))
                    return false;
            } else if (req.is(syntax_kind::compound_requirement)) {
                if (!compound_requirement(t, req))
                    return false;
            } else {
                return false;
            }
        }
        enc.write(terminal::E_end);
        return true;
    }
    case syntax_kind::simple_template_id:
        if (!c[0].is_token() || !analysis.constraint_template(t, c[0].token()))
            return false;
        if (!source_name(analysis.token_identifier(c[0].token())))
            return false;
        return template_args_of_constraint(t, c);
    case syntax_kind::qualified_id: {
        /* Q<args>::name, Q a class template of the template's scope:
           [abi:expressions]' <unresolved-name>, [impl.clang] in its
           qualifier-level form,

               sr <unresolved-qualifier-level> E <base-unresolved-name>

           the level Q's name and template-args */
        if (c.size() != 2 || !c[0].is(syntax_kind::nested_name_specifier))
            return false;
        if (c[1].is(syntax_kind::simple_template_id))
            return unresolved_name_of_constraint_member(t, c[0], c[1]);
        if (c[0].size() != 2 || !c[0][0].is(syntax_kind::simple_template_id) ||
            !c[1].is_token() || !c[1].is_identifier())
            return false;
        SyntaxElement q = c[0][0];
        if (!q[0].is_token())
            return false;
        if (!analysis.constraint_class_template(
                analysis.entity_table[t].enclosing, q[0].token()))
            return false;
        enc.write(terminal::sr_unresolved_scope);
        if (!source_name(analysis.token_identifier(q[0].token())) ||
            !template_args_of_constraint(t, q))
            return false;
        enc.write(terminal::E_end);
        return source_name(analysis.token_identifier(c[1].token()));
    }
    default:
        return false;
    }
}

/* { e } noexcept -> C<args>; in t's requires-clause, C a concept named
   unqualified */
bool itanium_mangler::compound_requirement(entity_index t, SyntaxElement req)
{
    if (req.size() < 3)
        return false;
    enc.write(terminal::X_requirement);
    if (!expression_of_written(req[1]))
        return false;
    for (uint32_t i = 3; i < req.size(); i++) {
        if (req[i].is(standard_keyword::noexcept_)) {
            enc.write(terminal::N_noexcept_requirement);
            continue;
        }
        if (!req[i].is(syntax_kind::return_type_requirement))
            continue;
        if (req[i].size() != 2)
            return false;
        SyntaxElement q = req[i][1];
        if (!q.is(syntax_kind::type_constraint) || !q.size() ||
            !q[0].is_identifier())
            return false;
        enc.write(terminal::R_return_type_requirement);
        if (!type_constraint(t, q))
            return false;
    }
    return true;
}

/* the namespace a nested-name-specifier written in t's requires-clause
   names, one or more namespaces (the first found from t's scope, as
   written: std, not its inline namespace); false for another */
bool itanium_mangler::constraint_namespace(entity_index t,
                                           SyntaxElement qualifier,
                                           entity_index &scope) const
{
    scope = 0;
    for (uint32_t i = 0; i + 1 < qualifier.size(); i += 2) {
        if (!qualifier[i].is_identifier())
            return false;
        entity_index next = 0;
        if (i == 0)
            next = analysis.constraint_name(analysis.entity_table[t].enclosing,
                                            qualifier[i].token());
        else
            next = analysis.entity_table.find(
                scope, analysis.token_identifier(qualifier[i].token()));
        if (!next ||
            analysis.entity_table[next].kind != entity_kind::namespace_entity)
            return false;
        scope = next;
    }
    return true;
}

/* ns::v<args> in t's requires-clause, v a concept or variable template of
   namespace ns (one or more levels, as written): [abi:expressions]'
   <unresolved-name>,

       sr <unresolved-qualifier-level>+ E <base-unresolved-name>

   each level the namespace's name as written (std, not its inline
   namespace) */
bool itanium_mangler::unresolved_name_of_constraint_member(
    entity_index t, SyntaxElement qualifier, SyntaxElement id)
{
    if (!id[0].is_token() || !qualifier.size() ||
        qualifier[0].is(preprocessing_op_or_punc::COLON_COLON))
        return false;
    entity_index scope = 0;
    if (!constraint_namespace(t, qualifier, scope) ||
        !analysis.constraint_namespace_template(scope, id[0].token()))
        return false;
    enc.write(terminal::sr_unresolved_scope);
    for (uint32_t i = 0; i + 1 < qualifier.size(); i += 2)
        if (!source_name(analysis.token_identifier(qualifier[i].token())))
            return false;
    enc.write(terminal::E_end);
    if (!source_name(analysis.token_identifier(id[0].token())))
        return false;
    return template_args_of_constraint(t, id);
}

/* the <template-args> of a template-id c written in t's requires-clause:
   I, each argument (one of t's parameters, a fundamental type keyword or
   a class of t's scope), E */
bool itanium_mangler::template_args_of_constraint(entity_index t,
                                                  SyntaxElement c)
{
    enc.write(terminal::I_template_args);
    /* (more than one argument: a template-argument-list) */
    owned_array<SyntaxElement> written;
    template_argument_elements(c, written);
    for (size_t wi = 0; wi < written.size(); wi++) {
        SyntaxElement a = written[wi];
        /* (a pack expansion: the type-id's '...') */
        bool expansion = a.is_node() && a.is(syntax_kind::type_id) &&
                         a.size() == 2 &&
                         a[1].is(preprocessing_op_or_punc::ELLIPSIS);
        SyntaxElement whole = a;
        if (expansion)
            a = a[0];
        /* a type-id that is one template parameter's name */
        while (a.is_node() && a.size() == 1 &&
               a.is(syntax_kind::type_id, syntax_kind::type_specifier_seq))
            a = a[0];
        /* decltype(p), p a function parameter of t: Dt <function-param>
           E; expanded, a pack's, after Dp ([abi:expressions]) */
        if (a.is_node() && a.is(syntax_kind::decltype_specifier) &&
            a.size() == 4 && a[2].is_token()) {
            if (uint32_t parameter =
                    analysis.function_parameter_position(t, a[2].token())) {
                if (expansion)
                    enc.write(terminal::Dp_pack_expansion);
                enc.write(terminal::Dt_decltype_entity);
                function_param(parameter);
                enc.write(terminal::E_end);
                continue;
            }
        }
        /* (other expansions are not mangled yet) */
        if (expansion)
            a = whole;
        if (!a.is_token()) {
            /* another type (an alias template's specialization, a
               typename, ...): analysed in the template's parameter scope */
            qualified_type argument;
            if (!analysis.constraint_type(t, written[wi], argument))
                return false;
            /* (an enclosing template's parameter there is bound to its
               argument: clang mangles the parameter) */
            bool leveled = constraint_level(t) != 0;
            for (token_index k = a.first_token(); leveled && k < a.end_token();
                 k++) {
                entity_index pe = analysis.constraint_parameter(t, k);
                if (pe && !analysis.constraint_parameter_is_own(t, pe))
                    return refuse("a type of a constraint that names an "
                                  "enclosing template's parameter");
            }
            entity_index level_of = 0;
            if (leveled)
                level_of = t;
            scoped_value<entity_index> in_template(encoding_template, t);
            scoped_value<entity_index> in_level(leveled_template, level_of);
            if (!type(argument))
                return false;
            continue;
        }
        entity_index pe = analysis.constraint_parameter(t, a.token());
        if (pe) {
            if (!template_param_of_constraint(t, pe))
                return false;
            continue;
        }
        /* a fundamental type keyword (int, long, char, ...) */
        fundamental_type ft = simple_type_keyword_alone(a.keyword());
        if (ft != fundamental_type::none) {
            if (!builtin_type(ft))
                return false;
            continue;
        }
        /* a class of the template's scope, or a typedef-name there that
           no template parameter's argument gave its type */
        entity_index k = analysis.constraint_class(
            analysis.entity_table[t].enclosing, a.token());
        if (!k)
            k = analysis.constraint_plain_typedef(t, a.token());
        if (!k || !type(analysis.entity_table[k].type))
            return false;
    }
    enc.write(terminal::E_end);
    return true;
}

/* the number of class template specializations t is a member of */
uint64_t itanium_mangler::constraint_level(entity_index t) const
{
    const entities &table = analysis.entity_table;
    uint64_t level = 0;
    for (entity_index s = table[t].enclosing; s; s = table[s].enclosing)
        if (table[s].kind == entity_kind::class_type && table[s].instantiation)
            level++;
    return level;
}

/* [impl.clang]: a type-constraint of t's: its concept as a <name>, with
   the template-args written, the concept then a substitution candidate
   ([abi:mangling-compression]'s template-prefix) */
bool itanium_mangler::type_constraint(entity_index t, SyntaxElement q)
{
    /* N::C, N a chain of namespaces: the concept's name */
    if (q.size() == 2 && q[0].is(syntax_kind::nested_name_specifier)) {
        entity_index scope = 0;
        if (!constraint_namespace(t, q[0], scope))
            return false;
        entity_index member = analysis.entity_table.find(
            scope, analysis.token_identifier(q[1].token()));
        return member && name(member);
    }
    entity_index found = analysis.constraint_name(
        analysis.entity_table[t].enclosing, q[0].token());
    if (!found)
        return false;
    if (q.size() == 1)
        return name(found);
    if (q.size() < 3 || !punctuator(q[1], preprocessing_op_or_punc::LESS))
        return false;
    /* (a substituted template-prefix of a namespace's member is still a
       nested-name's) */
    entity_index scope = analysis.entity_table[found].enclosing;
    bool wrapped = scope && !is_std(scope);
    /* [abi:mangle.name] <name> ::= <unscoped-template-name> <template-args>
       or <nested-name> ::= N <template-prefix> <template-args> E, the
       template-prefix the concept's prefix and name, or its substitution */
    if (wrapped)
        enc.write(terminal::N_nested_name);
    if (!substitution(found)) {
        if (wrapped) {
            if (!prefix(scope) || !unqualified_name(found))
                return false;
        } else if (!unscoped_name(found)) {
            return false;
        }
        add_candidate(found);
    }
    if (!template_args_of_constraint(t, q))
        return false;
    if (wrapped)
        enc.write(terminal::E_end);
    return true;
}

/*
 * One of t's template parameters in t's requires-clause.  [impl.clang]: the
 * clause is mangled unsubstituted, so a parameter keeps its level
 * ([abi:mangle.template-param]):
 *
 *     <template-param> ::= T_
 *                      ::= T <parameter-2 non-negative number> _
 *
 * and, for a parameter of an enclosing level, clang's TL forms of a later
 * revision of the ABI than docs/itanium-abi-grammar.tsv has: TL, the level
 * L-1 as a non-negative number, then __ for the first parameter or _, the
 * parameter-2 number and _.
 *
 * L the number of class templates t is a member of (0: T_); a parameter
 * of another level is a candidate of its own, not the type T_ names.
 */
bool itanium_mangler::template_param_of_constraint(entity_index t,
                                                   entity_index parameter)
{
    const entities &table = analysis.entity_table;
    uint64_t level = constraint_level(t);
    if (!level || !analysis.constraint_parameter_is_own(t, parameter)) {
        /* a non-type parameter (its type is the value's, not the
           parameter's): T_ or T <n> _ alone */
        if (record(table[parameter].type).kind !=
            type_kind::template_parameter) {
            template_param(table[parameter].value);
            return true;
        }
        return type(table[parameter].type);
    }
    /* (a type parameter is a type, a candidate; a non-type one an
       expression operand, never) */
    bool is_type =
        record(table[parameter].type).kind == type_kind::template_parameter;
    if (is_type && substitution(parameter))
        return true;
    uint64_t position = table[parameter].value;
    enc.write(terminal::TL_leveled_template_param);
    enc.number(level - 1);
    enc.write(terminal::underscore);
    if (position)
        enc.number(position - 1);
    enc.write(terminal::underscore);
    if (!is_type)
        return true;
    add_candidate(parameter);
    return true;
}

} // namespace cplusplus
