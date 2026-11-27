/*
 * mangling/expressions-dependent-value.cpp: [abi:expressions] of a value-dependent template argument the analysis
 * kept by its form (entity_kind::dependent_value, dependent_value_form) (doc/PLAN-mangling.md).
 */
#include "mangling-internal.h"

namespace cplusplus
{

/* argument i of c's (its element - 1 if element) */
/*
 * [abi:mangle.template-arg] a value-dependent argument:
 *
 *     <template-arg> ::= X <expression> E
 *
 * [impl.clang]: the expression is "nt" before a name written with a
 * nested-name-specifier, then "sr", the qualifier levels, "E" and the
 * base name ([abi:expressions]' unresolved-name, as clang spells it).
 *
 * Q<args>::name as written, Q unqualified; clang makes the qualifier
 * level no candidate, its template-args' types are as anywhere.
 */
bool itanium_mangler::expression_of_dependent_value(entity_index d)
{
    const entity &x = analysis.entity_table[d];
    if (!x.value)
        return false;
    using form = dependent_value_form;
    auto is = [&](dependent_value_form f) {
        return x.value == dependent_value_code(f);
    };
    for (const auto &b : dependent_value_operators)
        if (is(b.form)) {
            enc.write(operator_written(b.op, 2));
            return expression_of_dependent_value(x.enclosing) &&
                   expression_of_dependent_value(x.name);
        }
    if (is(form::sizeof_type)) {
        enc.write(terminal::sizeof_type);
        return type(x.type);
    }
    if (is(form::integer_literal)) {
        enc.write(terminal::expr_primary);
        builtin_type(fundamental_type::int_);
        enc.number(x.name);
        enc.write(terminal::end);
        return true;
    }
    if (is(form::member_call)) {
        enc.write(itanium_operator::call);
        enc.write(terminal::unresolved_scope);
        if (!type(x.type) || !source_name(x.name))
            return false;
        enc.write(terminal::end);
        return true;
    }
    if (is(form::static_member)) {
        /* a static data member: an external name, L_Z <encoding> E */
        enc.write(terminal::external_name);
        if (!encoding(x.name))
            return false;
        enc.write(terminal::end);
        return true;
    }
    if (is(form::logical_not)) {
        enc.write(itanium_operator::logical_not);
        return expression_of_dependent_value(x.enclosing);
    }
    auto written_arguments = [&]() {
        enc.write(terminal::template_args);
        for (uint32_t i = 0; i < x.member_count; i++)
            if (!type(analysis.dependent_value_arguments[x.first_member + i]))
                return false;
        enc.write(terminal::end);
        return true;
    };
    if (is(form::member_template_call)) {
        /* [abi:expressions] Q::template f<args>(), Q a class template's
           specialization:

               <expression> ::= cl <expression>+ E
               <unresolved-name> ::= [gs] sr <unresolved-qualifier-level>+ E <base-unresolved-name>
         */
        entity_index c = record(x.type).entity;
        enc.write(itanium_operator::call);
        enc.write(terminal::unresolved_scope);
        if (!source_name(analysis.entity_table[c].name) ||
            !template_args(c, 0, false))
            return false;
        enc.write(terminal::end);
        if (!source_name(x.name) || !written_arguments())
            return false;
        enc.write(terminal::end);
        return true;
    }
    bool negated = x.value & dependent_value_negated;
    /* (the low half but the namespaces' number) */
    uint64_t low = x.value & 0xffff00ffu;
    if (low == (dependent_value_alias | dependent_value_code(form::member)) ||
        low == (dependent_value_alias |
                dependent_value_code(form::negated_member))) {
        /* [!] [ns::] A<args>::name, A an alias template: its name and
           arguments as written (clang) */
        if (negated)
            enc.write(itanium_operator::logical_not);
        enc.write(terminal::unresolved_scope);
        if (x.enclosing) {
            uint32_t depth = dependent_value_namespaces(x.value);
            if (!depth)
                depth = 1;
            owned_array<entity_index> chain;
            entity_index s = x.enclosing;
            for (uint32_t i = 0; i < depth && s; i++) {
                chain.append(s);
                s = analysis.entity_table[s].enclosing;
            }
            for (size_t i = chain.size(); i-- > 0;)
                if (!source_name(analysis.entity_table[chain[i]].name))
                    return false;
        }
        if (!source_name(dependent_value_identifier(x.value) - 1) ||
            !written_arguments())
            return false;
        enc.write(terminal::end);
        return source_name(x.name);
    }
    entity_index q = record(x.type).entity;
    const entity &spec = analysis.entity_table[q];
    if (is(form::variable_specialization) ||
        is(form::negated_variable_specialization)) {
        /* a variable template's specialization, [!] [ns::] v<args>:
           [abi:expressions]' unresolved-name */
        if (negated)
            enc.write(itanium_operator::logical_not);
        if (x.enclosing) {
            enc.write(terminal::unresolved_scope);
            if (!source_name(analysis.entity_table[x.enclosing].name))
                return false;
            enc.write(terminal::end);
        }
        return source_name(spec.name) && template_args(q, 0, false);
    }
    if (!x.enclosing && !dependent_value_identifier(x.value) &&
        spec.instantiation &&
        analysis.template_template_parameter(
            analysis.specialized_template(q))) {
        /* D<args>::name with D a template template-parameter: clang
           spells the qualifier as an <unresolved-type>, sr T_ I <args> E
           name, with no E closing a level */
        if (negated)
            enc.write(itanium_operator::logical_not);
        enc.write(terminal::unresolved_scope);
        return class_enum_type(x.type) && source_name(x.name);
    }
    if (negated)
        enc.write(itanium_operator::logical_not);
    enc.write(terminal::unresolved_scope);
    if (x.enclosing) {
        uint32_t depth = dependent_value_namespaces(x.value);
        if (!depth)
            depth = 1;
        owned_array<entity_index> chain;
        entity_index s = x.enclosing;
        for (uint32_t i = 0; i < depth && s; i++) {
            chain.append(s);
            s = analysis.entity_table[s].enclosing;
        }
        for (size_t i = chain.size(); i-- > 0;)
            if (!source_name(analysis.entity_table[chain[i]].name))
                return false;
    }
    /* (clang flattens a pack expansion written among the arguments: the
       substituted elements, no J ... E) */
    uint32_t written = UINT32_MAX;
    if (x.member_count)
        written = x.member_count;
    if (!source_name(spec.name) || !template_args(q, 0, false, written))
        return false;
    /* a member class between the specialization and the name */
    if (uint32_t member_class = dependent_value_identifier(x.value))
        if (!source_name(member_class - 1))
            return false;
    enc.write(terminal::end);
    return source_name(x.name);
}

} // namespace cplusplus
