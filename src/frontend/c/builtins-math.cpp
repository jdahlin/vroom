/* vcc front end: floating-point builtin call construction. */
#include "cc-internal.h"

/* __builtin_fpclassify(nan, infinite, normal, subnormal, zero, x)'s
   tree of conditionals on its arguments v[0..6) (also the C++ front end's,
   through ast_builder_builtin_call) */
CC_PRIVATE uint32_t cc_fpclassify_build(cc *c, const uint32_t *v, uint32_t loc)
{
    uint32_t saved[6], pre[6];

    for (int i = 0; i < 6; i++) {
        uint32_t a = cc_node_rvalue(c, v[i]);
        if (i < 5)
            a = cc_conversion_apply(c, a, cc_type_basic(AST_TYPE_INT));
        saved[i] = cc_lowering_once(c, a, &pre[i]);
    }
    /* Match clang's classification tree: save the operand once, test zero,
     * then NaN and infinity, and finally distinguish normal/subnormal.
     * A class code is evaluated as its corresponding test is reached. */
    cc_id_list unary;
    cc_id_list_init(&unary);
    cc_id_list_append(c, &unary, cc_node_rvalue(c, saved[5]));
    uint32_t n = cc_conditional_build(
        c,
        cc_builtin_node_new(c, CC_BUILTIN_ISNORMAL, cc_type_basic(AST_TYPE_INT),
                            loc, &unary),
        saved[2], saved[3], loc);
    n = cc_lowering_sequence(c, pre[2], cc_lowering_sequence(c, pre[3], n));
    const int ids[] = {CC_BUILTIN_ISNAN, CC_BUILTIN_ISINF};
    for (int i = 1; i >= 0; i--)
        n = cc_lowering_sequence(
            c, pre[i],
            cc_conditional_build(
                c,
                cc_builtin_node_new(c, ids[i], cc_type_basic(AST_TYPE_INT), loc,
                                    &unary),
                saved[i], n, loc));
    n = cc_lowering_sequence(
        c, pre[4], cc_conditional_build(c, saved[5], n, saved[4], loc));
    return cc_lowering_sequence(c, pre[5], n);
}

CC_PRIVATE uint32_t cc_builtin_math_call_build(cc *c, const cc_builtin *b,
                                               uint32_t loc, bool *handled)
{
    cc_id_list args;
    *handled = false;
    switch (b->id) {
    case CC_BUILTIN_ISFPCLASS:
    case CC_BUILTIN_ISSUBNORMAL:
    case CC_BUILTIN_ISZERO: {
        *handled = true;
        i128 mask = b->id == CC_BUILTIN_ISSUBNORMAL ? 144 : 96;
        unsigned nargs = b->id == CC_BUILTIN_ISFPCLASS ? 2 : 1;
        uint32_t pre;

        cc_builtin_arguments_parse(c, &args);
        if (args.n != nargs) {
            cc_builtin_arity_check(c, loc, nargs, &args);
            return cc_node_error_new(c, loc);
        }
        if (!cc_type_is_float(c, AST_NODE(args.v[0])->ty)) {
            cc_report_error(c, AST_NODE(args.v[0])->loc,
                            "floating point classification requires argument "
                            "of floating point type (passed in '%s')",
                            tyname(c, AST_NODE(args.v[0])->ty));
            return cc_node_error_new(c, loc);
        }
        if (b->id == CC_BUILTIN_ISFPCLASS &&
            !cc_constant_integer(c, args.v[1], &mask)) {
            cc_report_error(
                c, loc, "argument to '%s' must be a constant integer", b->name);
            return cc_node_error_new(c, loc);
        }
        if (mask < 0 || mask > 1023) {
            cc_report_error(
                c, loc,
                "argument value %lld is outside the valid range [0, 1023]",
                (long long)mask);
            return cc_node_error_new(c, loc);
        }
        uint32_t a = cc_lowering_once(c, cc_node_rvalue(c, args.v[0]), &pre);
        cc_id_list unary;
        cc_id_list_init(&unary);
        cc_id_list_append(c, &unary, cc_node_rvalue(c, a));
        uint32_t sign = cc_builtin_node_new(
            c, CC_BUILTIN_SIGNBIT, cc_type_basic(AST_TYPE_INT), loc, &unary);
        /* Each pair is the negative/positive membership bit, except NaNs,
		 * whose class is determined by the quiet bit rather than their sign. */
        const int bits[5][2] = {{0, 1}, {2, 9}, {3, 8}, {4, 7}, {5, 6}};
        uint32_t member[5];
        for (int i = 0; i < 5; i++) {
            uint32_t neg =
                cc_node_integer_new(c, !!(mask & ((i128)1 << bits[i][0])),
                                    cc_type_basic(AST_TYPE_INT), loc);
            uint32_t pos =
                cc_node_integer_new(c, !!(mask & ((i128)1 << bits[i][1])),
                                    cc_type_basic(AST_TYPE_INT), loc);
            uint32_t cond = i ? sign
                              : cc_builtin_node_new(c, CC_BUILTIN_ISSIGNALING,
                                                    cc_type_basic(AST_TYPE_INT),
                                                    loc, &unary);
            member[i] = cc_conditional_build(c, cond, neg, pos, loc);
        }
        uint32_t n = cc_conditional_build(c, a, member[3], member[4], loc);
        const int ids[] = {CC_BUILTIN_ISNAN, CC_BUILTIN_ISINF,
                           CC_BUILTIN_ISNORMAL};
        for (int i = 2; i >= 0; i--)
            n = cc_conditional_build(
                c,
                cc_builtin_node_new(c, ids[i], cc_type_basic(AST_TYPE_INT), loc,
                                    &unary),
                member[i], n, loc);
        return cc_lowering_sequence(c, pre, n);
    }
    case CC_BUILTIN_FPCLASSIFY: {
        *handled = true;
        cc_builtin_arguments_parse(c, &args);
        if (args.n != 6) {
            cc_builtin_arity_check(c, loc, 6, &args);
            return cc_node_error_new(c, loc);
        }
        if (!cc_type_is_float(c, AST_NODE(args.v[5])->ty)) {
            cc_report_error(c, AST_NODE(args.v[5])->loc,
                            "floating point classification requires argument "
                            "of floating point type (passed in '%s')",
                            tyname(c, AST_NODE(args.v[5])->ty));
            return cc_node_error_new(c, loc);
        }
        return cc_fpclassify_build(c, args.v, loc);
    }
    default:
        return 0;
    }
}
