/*
 * expr.const.evaluate.cpp: [expr.const] the evaluation of a core constant
 * expression over the semantic tree.
 *
 * [expr.const]/5: "An expression E is a core constant expression unless
 * the evaluation of E, following the rules of the abstract machine
 * ([intro.execution]), would evaluate one of the following".  Analysis
 * folds the operators of the fundamental types as it builds the tree
 * (expression_meaning::evaluation); this is the evaluator for what it does
 * not fold: invocations of constexpr functions, their statements and
 * objects.
 *
 * The skeleton: every kind of node has its case, and the ones whose rules
 * are not written answer unsupported ([expr.const]).  A node analysis
 * folded already gives its value; nothing else does yet.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

analysis_result semantic_analysis::evaluate_constant(node_index at,
                                                     uint32_t node,
                                                     constant_value &v,
                                                     bool &constant)
{
    constant_evaluation state;
    analysis_result r = evaluate_node(state, node, v, constant);
    if (r == analysis_result::unsupported)
        return not_supported(at, "expr.const");
    return r;
}

analysis_result semantic_analysis::evaluate_node(constant_evaluation &state,
                                                 uint32_t node,
                                                 constant_value &v,
                                                 bool &constant)
{
    constant = false;
    const semantic_node &x = semantics[node];
    if (x.evaluation == constness::not_constant)
        return analysis_result::well_formed;
    if (x.evaluation == constness::constant) {
        /* [basic.fundamental]: an integer or a double analysis folded */
        fundamental_type f = fundamental_of(x.type);
        if (fundamental_type_is_integral(f)) {
            v.kind = constant_value_kind::integer;
            v.integer = static_cast<unsigned __int128>(
                integer_value(tokens.files.behavior, f, x.value));
            constant = true;
            return analysis_result::well_formed;
        }
        if (f == fundamental_type::float_ || f == fundamental_type::double_) {
            v.kind = constant_value_kind::floating;
            v.floating = floating_value(x.value);
            constant = true;
            return analysis_result::well_formed;
        }
        return analysis_result::unsupported;
    }
    switch (x.kind) {
    case semantic_kind::call:
        return evaluate_call(state, node, v, constant);
    /* [expr.const]/5.8: "an lvalue-to-rvalue conversion unless it is
       applied to ... a non-volatile glvalue that refers to an object that
       is usable in constant expressions, or ... whose lifetime began
       within the evaluation of E" */
    case semantic_kind::entity:
    case semantic_kind::conversion:
    /* the operators of the fundamental types on values the evaluator
       computed ([expr.arith.conv], [expr.add], ...) */
    case semantic_kind::unary:
    case semantic_kind::binary:
    case semantic_kind::logical_and:
    case semantic_kind::logical_or:
    case semantic_kind::conditional:
    case semantic_kind::comma:
    /* [expr.const]/5.16: "a modification of an object unless it is
       applied to a non-volatile lvalue of literal type that refers to a
       non-volatile object whose lifetime began within the evaluation of
       E" */
    case semantic_kind::assignment:
    case semantic_kind::increment:
    /* pointers and objects: [expr.const]/11's permitted results */
    case semantic_kind::address:
    case semantic_kind::indirection:
    case semantic_kind::subscript:
    case semantic_kind::member:
    case semantic_kind::string_literal:
    case semantic_kind::bound_member_function:
    /* initialization: [dcl.init], [class.ctor], [class.dtor] */
    case semantic_kind::zero:
    case semantic_kind::initializer_list:
    case semantic_kind::bind_reference:
    case semantic_kind::construct:
    case semantic_kind::destroy:
    default:
        return analysis_result::unsupported;
    }
}

/*
 * [expr.call]/7 with [dcl.constexpr]: the invocation of a constexpr
 * function.  [expr.const]/5.2: "an invocation of a non-constexpr function"
 * is not a core constant expression; /5.3 "an invocation of an undefined
 * constexpr function" neither.  The evaluation binds the parameters to the
 * arguments' values in a frame of state.objects and runs the body's
 * statements; [implimits]/2 bounds the depth.
 */
analysis_result semantic_analysis::evaluate_call(constant_evaluation &state,
                                                 uint32_t node,
                                                 constant_value &v,
                                                 bool &constant)
{
    constant = false;
    if (state.depth >= constexpr_invocation_limit)
        return analysis_result::unsupported;
    return analysis_result::unsupported;
}

/*
 * [stmt.stmt] as the evaluation runs a constexpr function's body:
 * returned is set by a return statement ([stmt.return]/2), with its value
 * in result.  [dcl.constexpr]/3.4 lists what the body may not contain; the
 * rest runs as the abstract machine does.
 */
analysis_result
semantic_analysis::evaluate_statement(constant_evaluation &state, uint32_t node,
                                      bool &constant, bool &returned,
                                      constant_value &result)
{
    constant = false;
    returned = false;
    if (++state.full_expressions > constant_full_expression_limit)
        return analysis_result::unsupported;
    switch (semantics[node].kind) {
    case semantic_kind::block:
    case semantic_kind::expression_statement:
    case semantic_kind::declaration_statement:
    case semantic_kind::declarations:
    case semantic_kind::null_statement:
    case semantic_kind::if_statement:
    case semantic_kind::while_statement:
    case semantic_kind::do_statement:
    case semantic_kind::for_statement:
    case semantic_kind::switch_statement:
    case semantic_kind::break_statement:
    case semantic_kind::continue_statement:
    case semantic_kind::return_statement:
    /* [dcl.constexpr]/3.4: "a goto statement" may not be in the body */
    case semantic_kind::goto_statement:
    default:
        return analysis_result::unsupported;
    }
}

} // namespace cplusplus
