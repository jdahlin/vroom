/*
 * expr.prim.lambda.closure.cpp: [expr.prim.lambda.closure] Closure types:
 * a closure type completed after its lambda's body (expr.prim.lambda.cpp
 * makes it, with its function call operator; the captures' members are
 * expr.prim.lambda.capture.cpp's), and the conversion function of a lambda
 * without a lambda-capture (/7, /11).
 *
 * The closure type's layout, special member functions and their
 * definitions are a class's (abi/itanium/layout.cpp, class.ctor.cpp).
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
        return not_supported(n, rule);
    analysis_result r = implicit_special_members(c, n);
    if (r != analysis_result::well_formed)
        return r;
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
analysis_result semantic_analysis::closure_conversion(entity_index c,
                                                      entity_index call,
                                                      SyntaxRef n)
{
    qualified_type function_type = entity_table[call].type;
    const struct type f = type_table[function_type.unqualified()];
    if (f.variadic || contains_placeholder(f.operand))
        return not_supported(n, "expr.prim.lambda.closure");
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
    entity_table.bind(c, conversion_name, conversion);

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
        analysis_result r = initialize_with(n, a, t, false, false, value);
        if (r != analysis_result::well_formed)
            return r;
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
        analysis_result r =
            initialize_with(n, result, f.operand, false, false, value);
        if (r != analysis_result::well_formed)
            return r;
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
    analysis_result r = initialize_with(n, named, pointer, false, false, value);
    if (r != analysis_result::well_formed)
        return r;
    statement = statement_node(semantic_kind::return_statement, at, value.node);
    block = statement_node(semantic_kind::block, at);
    semantics.set_list(block, &statement, 1);
    finish_synthesized(sf, current_declaration, at, block);
    return analysis_result::well_formed;
}

} // namespace cplusplus
