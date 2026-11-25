/*
 * closure-type.cpp: [expr.prim.lambda.closure] Closure types:
 * a closure type completed after its lambda's body (lambda-expression.cpp
 * makes it, with its function call operator; the captures' members are
 * lambda-capture.cpp's), and the conversion function of a lambda
 * without a lambda-capture (/7, /11).
 *
 * The closure type's layout, special member functions and their
 * definitions are a class's (abi/itanium/layout.cpp, constructor.cpp).
 * /13's deleted copy assignment operator and the default constructor a
 * closure type with a lambda-capture does not have are deleted ones (a
 * use is an error).  Not analysed: the conversion function of a variadic
 * lambda.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

analysis_result semantic_analysis::closure_class(entity_index c, SyntaxRef n,
                                                 bool has_capture)
{
    /* [expr.prim.lambda.closure]/13: "The closure type associated with a
       lambda-expression has no default constructor if the
       lambda-expression has a lambda-capture and a defaulted default
       constructor otherwise. ... It has a deleted copy assignment
       operator if the lambda-expression has a lambda-capture and
       defaulted copy and move assignment operators otherwise" (before
       C++20 every closure type has neither: a use is an error either
       way) */
    if (has_capture || !tokens.files.behavior.has(
                           language_feature::default_constructible_lambdas)) {
        entity_table[c].implicit_default_deleted = true;
        entity_table[c].closure_assignment_deleted = true;
    }
    /* [class.mem]/7: complete, its members laid out ([abi:class-types]) */
    if (const char *rule = itanium_class_layout(*this, c))
        return not_supported_because(
            n, rule, "a closure type the class layout cannot place");
    VTRY(r, implicit_special_members(c, n));
    /* the definitions are analysed as functions of their own: the
       enclosing function's state aside meanwhile (class_definition) */
    function_context enclosing_function =
        static_cast<function_context &&>(function_state);
    function_state = function_context();
    owned_array<entity_index> enclosing_block_variables =
        static_cast<owned_array<entity_index> &&>(block_variables);
    block_variables = owned_array<entity_index>();
    entity_index enclosing_scope = current_scope;
    SyntaxRef declaration = current_declaration;
    /* (a generic lambda's conversion function template is not
       declared) */
    entity_index call = entity_table[c].call_operator;
    if (!has_capture && entity_table[call].kind == entity_kind::function)
        r = closure_conversion(c, call, n);
    if (r == analysis_result::well_formed)
        r = implicit_definitions(c, n);
    function_state = static_cast<function_context &&>(enclosing_function);
    block_variables =
        static_cast<owned_array<entity_index> &&>(enclosing_block_variables);
    current_scope = enclosing_scope;
    current_declaration = declaration;
    return r;
}

/*
 * [expr.prim.lambda.closure]/7: "The closure type for a non-generic
 * lambda-expression with no lambda-capture whose constraints (if any) are
 * satisfied has a conversion function to pointer to function with C++
 * language linkage having the same parameter and return types as the
 * closure type's function call operator. ... The value returned by this
 * conversion function is the address of a function F that, when invoked,
 * has the same effect as invoking the closure type's function call
 * operator on a default-constructed instance of the closure type."  /11:
 * "The conversion function or conversion function template is public,
 * constexpr, non-virtual, non-explicit, const, and has a non-throwing
 * exception specification".  F is [impl.clang]'s static member function
 * __invoke, which calls the function call operator on an object of its
 * own (the closure has no members), its parameters forwarded.
 */
analysis_result
semantic_analysis::closure_conversion(entity_index c, entity_index call,
                                      SyntaxRef n, entity_index *out_conversion)
{
    qualified_type function_type = entity_table[call].type;
    const struct type f = type_table[function_type.unqualified()];
    if (f.variadic || contains_placeholder(f.operand))
        return not_supported_because(n, "expr.prim.lambda.closure",
                                     "a conversion to function pointer of a "
                                     "variadic or placeholder lambda");
    token_index at = n.first_token();
    static const char invoke_spelling[] = "__invoke";
    uint32_t invoke_name =
        tokens.pp.identifier(invoke_spelling, sizeof invoke_spelling - 1) + 1;
    entity_index invoke =
        entity_table.make(entity_kind::function, invoke_name, c);
    entity_table[invoke].type = function_type;
    entity_table[invoke].inline_ = true;
    entity_table[invoke].defined = true;
    entity_table.add_declaration(invoke, n);
    if (!entity_table.find(c, invoke_name))
        entity_table.bind(c, invoke_name, invoke);
    qualified_type pointer =
        qualified_type::make(type_table.pointer_to(function_type), cv_none);
    bool non_throwing = tokens.files.behavior.has(
        language_feature::exception_specification_in_type);
    uint32_t conversion_name = conversion_function_name();
    entity_index conversion =
        entity_table.make(entity_kind::function, conversion_name, c);
    entity_table[conversion].type = qualified_type::make(
        type_table.function(pointer, nullptr, 0, false, non_throwing), cv_none);
    entity_table[conversion].nonstatic_member = true;
    entity_table[conversion].member_cv = cv_const;
    entity_table[conversion].conversion_function = true;
    entity_table[conversion].inline_ = true;
    entity_table[conversion].defined = true;
    entity_table.add_declaration(conversion, n);
    /* [expr.prim.lambda.closure]/8: for a generic lambda's conversion
       function template specialization, give __invoke and the conversion
       operator their template specialization records so Itanium mangling
       matches clang's __invoke<T...>(T...) and operator auto(*)(T...)<T...>() */
    if (uint32_t call_inst = entity_table[call].instantiation) {
        const instantiation_record orig = instantiations[call_inst - 1];
        entity_index call_tmpl = orig.template_entity;
        qualified_type pat_type = entity_table[call_tmpl].type;

        entity_index inv_tmpl =
            entity_table.make(entity_kind::template_entity, invoke_name, c);
        entity_table[inv_tmpl].type = pat_type;
        entity_table[inv_tmpl].instantiation =
            entity_table[call_tmpl].instantiation;
        instantiation_record inv_rec = orig;
        inv_rec.template_entity = inv_tmpl;
        inv_rec.specialization = invoke;
        instantiations.append(inv_rec);
        entity_table[invoke].instantiation =
            static_cast<uint32_t>(instantiations.size());

        qualified_type pat_ptr =
            qualified_type::make(type_table.pointer_to(pat_type), cv_none);
        entity_index conv_tmpl =
            entity_table.make(entity_kind::template_entity, conversion_name, c);
        entity_table[conv_tmpl].type = qualified_type::make(
            type_table.function(pat_ptr, nullptr, 0, false, non_throwing),
            cv_none);
        entity_table[conv_tmpl].nonstatic_member = true;
        entity_table[conv_tmpl].member_cv = cv_const;
        entity_table[conv_tmpl].conversion_function = true;
        entity_table[conv_tmpl].instantiation =
            entity_table[call_tmpl].instantiation;
        instantiation_record conv_rec = orig;
        conv_rec.template_entity = conv_tmpl;
        conv_rec.specialization = conversion;
        instantiations.append(conv_rec);
        entity_table[conversion].instantiation =
            static_cast<uint32_t>(instantiations.size());
    }
    if (entity_index existing = entity_table.find(c, conversion_name)) {
        entity_table[conversion].next_overload =
            entity_table[existing].next_overload;
        entity_table[existing].next_overload = conversion;
    } else {
        entity_table.bind(c, conversion_name, conversion);
    }
    if (out_conversion)
        *out_conversion = conversion;

    /* __invoke: its parameters, an object of the closure type, and the
       call operator's call on it, returned */
    synthesized_function sf;
    begin_synthesized(sf, invoke, f.operand, qualified_type());
    owned_array<uint32_t> arguments;
    for (uint32_t i = 0; i < f.parameter_count; i++) {
        qualified_type t =
            type_table.parameters(function_type.unqualified())[i];
        entity_index p = synthesized_variable(sf, t, true);
        /* forwarded: a reference parameter as what it is bound to, an
           rvalue reference's and a class's as an xvalue */
        const struct type record = type_table[t.unqualified()];
        expression_meaning a;
        a.type = t;
        a.category = value_category::lvalue;
        a.entity = p;
        if (record.kind == type_kind::lvalue_reference ||
            record.kind == type_kind::rvalue_reference) {
            a.type = record.operand;
            a.entity = 0;
        }
        if ((record.kind == type_kind::rvalue_reference &&
             type_table[record.operand.unqualified()].kind !=
                 type_kind::function) ||
            record.kind == type_kind::class_type)
            a.category = value_category::xvalue;
        a.evaluation = constness::not_constant;
        a.node = expression_node(semantic_kind::entity, a, at, p);
        initialized_value value;
        VTRY(r, initialize_with(n, a, t, false, false, value));
        arguments.append(value.node);
    }
    entity_index object = synthesized_variable(sf, entity_table[c].type, false);
    uint32_t declared =
        statement_node(semantic_kind::declaration_statement, at, object);
    expression_meaning o;
    o.type = entity_table[c].type;
    o.category = value_category::lvalue;
    o.entity = object;
    o.evaluation = constness::not_constant;
    o.node = expression_node(semantic_kind::entity, o, at, object);
    o.type = qualified_type::make(
        type_table.pointer_to(qualified_type::make(
            entity_table[c].type.unqualified(), entity_table[call].member_cv)),
        cv_none);
    o.category = value_category::prvalue;
    o.entity = 0;
    o.node = expression_node(semantic_kind::address, o, at, o.node);
    expression_meaning callee;
    callee.type = function_type;
    callee.category = value_category::lvalue;
    callee.evaluation = constness::constant;
    callee.entity = call;
    uint32_t callee_node =
        expression_node(semantic_kind::entity, callee, at, call);
    expression_meaning result;
    call_result(f.operand, result);
    result.evaluation = constness::not_constant;
    result.node =
        expression_node(semantic_kind::call, result, at, callee_node, o.node);
    semantics.set_list(result.node, arguments.data(), arguments.size());
    uint32_t statement = 0;
    if (fundamental_of(f.operand) == fundamental_type::void_) {
        statement = statement_node(semantic_kind::expression_statement, at,
                                   result.node);
    } else {
        initialized_value value;
        VTRY(r, initialize_with(n, result, f.operand, false, false, value));
        statement =
            statement_node(semantic_kind::return_statement, at, value.node);
    }
    uint32_t items[2] = {declared, statement};
    uint32_t block = statement_node(semantic_kind::block, at);
    semantics.set_list(block, items, 2);
    finish_synthesized(sf, current_declaration, at, block);

    /* the conversion function: __invoke's address */
    begin_synthesized(
        sf, conversion, pointer,
        qualified_type::make(type_table.pointer_to(qualified_type::make(
                                 entity_table[c].type.unqualified(), cv_const)),
                             cv_none));
    expression_meaning named;
    named.type = function_type;
    named.category = value_category::lvalue;
    named.evaluation = constness::constant;
    named.entity = invoke;
    named.node = expression_node(semantic_kind::entity, named, at, invoke);
    initialized_value value;
    VTRY(r, initialize_with(n, named, pointer, false, false, value));
    statement = statement_node(semantic_kind::return_statement, at, value.node);
    block = statement_node(semantic_kind::block, at);
    semantics.set_list(block, &statement, 1);
    finish_synthesized(sf, current_declaration, at, block);
    return analysis_result::well_formed;
}

/*
 * [expr.prim.lambda.closure]/8: a generic lambda without a lambda-capture
 * has a conversion function template to pointer to function: deduce the
 * call operator template's arguments from the target function pointer's
 * parameters, instantiate the call operator, synthesize __invoke and the
 * conversion function, and convert m through it.
 */
analysis_result semantic_analysis::generic_closure_conversion(
    entity_index c, qualified_type to, SyntaxRef at, expression_meaning &m)
{
    if (entity_table[c].member_count != 0)
        return not_supported_because(
            at, "expr.prim.lambda.closure",
            "a function pointer conversion of a generic lambda with captures");
    const struct type to_ptr = type_table[to.unqualified()];
    if (to_ptr.kind != type_kind::pointer)
        return not_supported_because(
            at, "expr.prim.lambda.closure",
            "a generic lambda converted to a type that is not a pointer");
    const struct type to_fn = type_table[to_ptr.operand.unqualified()];
    if (to_fn.kind != type_kind::function || to_fn.variadic)
        return not_supported_because(
            at, "expr.prim.lambda.closure",
            "a generic lambda converted to no non-variadic function pointer");
    entity_index call_tpl = entity_table[c].call_operator;
    uint32_t ft = entity_table[call_tpl].instantiation;
    if (!ft)
        return not_supported_because(
            at, "expr.prim.lambda.closure",
            "a generic lambda whose call operator is not a template");
    const function_template f = function_templates[ft - 1];
    const struct type pat_fn =
        type_table[entity_table[f.entity].type.unqualified()];
    if (pat_fn.kind != type_kind::function || pat_fn.variadic ||
        pat_fn.parameter_count != to_fn.parameter_count)
        return not_supported_because(at, "expr.prim.lambda.closure",
                                     "a generic lambda's call operator not "
                                     "matching the pointer's parameters");
    const qualified_type *pat_params =
        type_table.parameters(entity_table[f.entity].type.unqualified());
    const qualified_type *to_params =
        type_table.parameters(to_ptr.operand.unqualified());
    owned_array<template_argument> values;
    owned_array<bool> set;
    for (uint32_t i = 0; i < f.parameter_count; i++) {
        values.append(template_argument());
        set.append(false);
    }
    for (uint32_t i = 0; i < pat_fn.parameter_count; i++)
        if (!deduce_type(pat_params[i], to_params[i], values, set, false))
            return not_supported_because(at, "expr.prim.lambda.closure",
                                         "a generic lambda conversion whose "
                                         "template arguments are not deduced");
    VTRY(dr, default_template_arguments(at, ft, values, set));
    entity_index call = 0;
    VTRY(ir, instantiate(at, call_tpl, values.data(),
                         static_cast<uint32_t>(values.size()), call));
    if (!call)
        return not_supported_because(
            at, "expr.prim.lambda.closure",
            "a generic lambda conversion that instantiated no call operator");
    const struct type inst_fn =
        type_table[entity_table[call].type.unqualified()];
    if (inst_fn.kind != type_kind::function ||
        !(inst_fn.operand == to_fn.operand) ||
        inst_fn.parameter_count != to_fn.parameter_count)
        return not_supported_because(at, "expr.prim.lambda.closure",
                                     "an instantiated lambda call operator "
                                     "with another type than the target");
    const qualified_type *inst_params =
        type_table.parameters(entity_table[call].type.unqualified());
    for (uint32_t i = 0; i < inst_fn.parameter_count; i++)
        if (!(inst_params[i] == to_params[i]))
            return not_supported_because(at, "expr.prim.lambda.closure",
                                         "an instantiated lambda call operator "
                                         "with other parameter types");
    function_context enclosing_function =
        static_cast<function_context &&>(function_state);
    function_state = function_context();
    owned_array<entity_index> enclosing_block_variables =
        static_cast<owned_array<entity_index> &&>(block_variables);
    block_variables = owned_array<entity_index>();
    entity_index enclosing_scope = current_scope;
    SyntaxRef declaration = current_declaration;
    entity_index conversion = 0;
    analysis_result r = closure_conversion(c, call, at, &conversion);
    function_state = static_cast<function_context &&>(enclosing_function);
    block_variables =
        static_cast<owned_array<entity_index> &&>(enclosing_block_variables);
    current_scope = enclosing_scope;
    current_declaration = declaration;
    if (r != analysis_result::well_formed || !conversion)
        return not_supported_because(
            at, "expr.prim.lambda.closure",
            "a generic lambda whose conversion function could not be made");
    token_index tok = at.first_token();
    expression_meaning obj = m;
    qualified_type this_ptr =
        qualified_type::make(type_table.pointer_to(qualified_type::make(
                                 entity_table[c].type.unqualified(), cv_const)),
                             cv_none);
    uint32_t this_node =
        expression_node(semantic_kind::address,
                        expression_meaning{this_ptr, value_category::prvalue,
                                           constness::not_constant},
                        tok, obj.node);
    expression_meaning callee;
    callee.type = entity_table[conversion].type;
    callee.category = value_category::lvalue;
    callee.evaluation = constness::constant;
    callee.entity = conversion;
    uint32_t callee_node =
        expression_node(semantic_kind::entity, callee, tok, conversion);
    m = expression_meaning();
    m.type = qualified_type::make(to.unqualified(), cv_none);
    m.category = value_category::prvalue;
    m.evaluation = constness::not_constant;
    m.node =
        expression_node(semantic_kind::call, m, tok, callee_node, this_node);
    return analysis_result::well_formed;
}

} // namespace cplusplus
