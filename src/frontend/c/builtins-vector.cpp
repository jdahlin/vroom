/* vcc front end: shuffle, convertvector, lane builtins and elementwise abs; see builtin-table.c for where a builtin's other facts live */
#include "cc-internal.h"

/*
 * __builtin_shufflevector(a, b, i...): lanes of a then b picked by constant
 * indices (-1: undefined, here 0), a vector of as many lanes;
 * __builtin_convertvector(v, AST_TYPE): each lane converted to AST_TYPE's lane type.
 * Both lowered lane by lane in arch/<cpu>/codegen.
 */
CC_PRIVATE uint32_t cc_builtin_vector_build(cc *c, bool shuffle, uint32_t loc)
{
    cc_id_list args;
    uint32_t a, b;
    ast_qualified_type at, rt;

    cc_id_list_init(&args);
    cc_token_expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
    a = cc_node_rvalue(c, cc_assignment_parse(c));
    at = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE(a)->ty);
    cc_token_expect(c, LEX_TOKEN_COMMA, "','");
    if (!shuffle) {
        rt = cc_type_name_parse(c);
        cc_token_expect_here(c, LEX_TOKEN_RIGHT_PAREN, "')'");
        if (!cc_type_is_vector(c, at) || !cc_type_is_vector(c, rt) ||
            (AST_TYPE(c, at)->ext ?: AST_TYPE(c, at)->n) !=
                (AST_TYPE(c, rt)->ext ?: AST_TYPE(c, rt)->n)) {
            cc_report_error(c, loc,
                            "first two arguments to __builtin_convertvector "
                            "must have the same number of elements");
            return cc_node_integer_new(c, 0, cc_type_basic(AST_TYPE_INT), loc);
        }
        cc_id_list_append(c, &args, a);
        return cc_builtin_node_new(c, CC_BUILTIN_CONVERTVECTOR, rt, loc, &args);
    }
    b = cc_node_rvalue(c, cc_assignment_parse(c));
    cc_id_list_append(c, &args, a);
    cc_id_list_append(c, &args, b);
    if (!cc_type_is_vector(c, at) ||
        !cc_type_is_compatible(
            c, at, AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE(b)->ty))) {
        cc_report_error(c, loc,
                        "first two arguments to '__builtin_shufflevector' must "
                        "have the same type");
        while (cc_token_accept(c, LEX_TOKEN_COMMA))
            cc_assignment_parse(c);
        cc_token_expect_here(c, LEX_TOKEN_RIGHT_PAREN, "')'");
        return cc_node_integer_new(c, 0, cc_type_basic(AST_TYPE_INT), loc);
    }
    {
        ast_qualified_type et = AST_TYPE(c, at)->base;
        uint64_t n = AST_TYPE(c, at)->ext ?: AST_TYPE(c, at)->n, k = 0;

        while (cc_token_accept(c, LEX_TOKEN_COMMA)) {
            uint32_t ix = cc_assignment_parse(c);
            i128 v = 0;

            if (!cc_constant_integer(c, ix, &v) || v < -1 || v >= (i128)(2 * n))
                cc_report_error(c, AST_NODE(ix)->loc,
                                "index for __builtin_shufflevector must be a "
                                "constant integer in range");
            cc_id_list_append(c, &args,
                              cc_node_integer_new(c, v,
                                                  cc_type_basic(AST_TYPE_INT),
                                                  AST_NODE(ix)->loc));
            k++;
        }
        cc_token_expect_here(c, LEX_TOKEN_RIGHT_PAREN, "')'");
        if (k == 0 || (cc_type_is_bool_vector(c, at)
                           ? k > 4096
                           : ((k & (k - 1)) || k * cc_type_size(c, et) > 64))) {
            cc_report_error(c, loc,
                            "__builtin_shufflevector: the number of indices "
                            "must be a power of two");
            return cc_node_integer_new(c, 0, cc_type_basic(AST_TYPE_INT), loc);
        }
        if (cc_type_is_bool_vector(c, at)) {
            uint32_t size = 1;
            while (size < (k + 7) / 8)
                size *= 2;
            ctype t = *AST_TYPE(c, cc_type_vector(c, et, size));
            t.ext = (uint16_t)k, t.n = (uint32_t)k;
            rt = AST_QUALIFIED_TYPE(cc_type_intern(c, &t), 0);
        } else
            rt = cc_type_vector(c, et, (uint32_t)(k * cc_type_size(c, et)));
    }
    return cc_builtin_node_new(c, CC_BUILTIN_SHUFFLEVECTOR, rt, loc, &args);
}

/* SSE operations expressed in ordinary vector lanes, preserving argument
 * evaluation and the instruction's second-operand choice for NaNs and ties. */
CC_PRIVATE uint32_t cc_builtin_vector_lane(cc *c, uint32_t v,
                                           ast_qualified_type et, unsigned i,
                                           uint32_t loc)
{
    uint32_t p = cc_cast_insert(c, cc_node_rvalue(c, v), cc_type_pointer(c, et),
                                AST_CAST_VECTOR_ADDRESS);
    uint32_t n =
        cc_node_new_operands(c, AST_NODE_INDEX, et, loc, p,
                             cc_node_integer_new(c, i, c->t_ptrdiff, loc));
    AST_NODE(n)->flags = AST_NODE_FLAG_LVALUE;
    return n;
}

/* abs has no integer promotions and INT_MIN maps to itself. Do the
 * negation in the unsigned type so that the generated operation cannot
 * acquire signed-overflow semantics. */
static uint32_t cc_builtin_elementwise_abs_lane(cc *c, uint32_t a,
                                                ast_qualified_type et,
                                                uint32_t loc)
{
    if (cc_type_is_integer(c, et)) {
        ast_qualified_type u = cc_type_unsigned(c, et);
        uint32_t neg = cc_conversion_apply(
            c,
            cc_node_binary_new(c, LEX_TOKEN_MINUS, u,
                               cc_node_integer_new(c, 0, u, loc),
                               cc_conversion_apply(c, a, u), loc),
            et);
        uint32_t cond =
            cc_node_binary_new(c, LEX_TOKEN_LESS, cc_type_basic(AST_TYPE_INT),
                               a, cc_node_integer_new(c, 0, et, loc), loc);
        return cc_conversion_apply(
            c, cc_conditional_build(c, cond, neg, a, loc), et);
    }
    if (cc_type_size(c, et) == 2) {
        /* Clearing the sign bit directly also preserves signaling NaNs;
		 * converting through float would quiet them. */
        uint32_t sym = cc_symbol_new(c, AST_SYMBOL_VARIABLE, 0, et, loc);
        c->syms[sym].sc = AST_STORAGE_AUTO;
        uint32_t tmp = cc_node_new(c, AST_NODE_VARIABLE, et, loc);
        AST_NODE(tmp)->a = sym;
        AST_NODE(tmp)->flags = AST_NODE_FLAG_LVALUE;
        uint32_t p = cc_cast_insert(
            c, cc_address_of_build(c, tmp, loc),
            cc_type_pointer(c, cc_type_basic(AST_TYPE_UNSIGNED_SHORT)),
            AST_CAST_POINTER_TO_POINTER);
        uint32_t bits =
            cc_node_new(c, AST_NODE_DEREFERENCE,
                        cc_type_basic(AST_TYPE_UNSIGNED_SHORT), loc);
        AST_NODE(bits)->a = p;
        AST_NODE(bits)->flags = AST_NODE_FLAG_LVALUE;
        uint32_t masked = cc_node_binary_new(
            c, LEX_TOKEN_AMPERSAND, cc_type_basic(AST_TYPE_UNSIGNED_SHORT),
            cc_node_rvalue(c, bits),
            cc_node_integer_new(c, 0x7fff,
                                cc_type_basic(AST_TYPE_UNSIGNED_SHORT), loc),
            loc);
        uint32_t write = cc_node_new_operands(
            c, AST_NODE_ASSIGN, cc_type_basic(AST_TYPE_UNSIGNED_SHORT), loc,
            bits, masked);
        return cc_lowering_sequence(
            c, cc_node_new_operands(c, AST_NODE_ASSIGN, et, loc, tmp, a),
            cc_lowering_sequence(c, write, cc_node_rvalue(c, tmp)));
    }
    const char *name =
        AST_TYPE_KIND(c, et) == AST_TYPE_FLOAT    ? "__builtin_fabsf"
        : AST_TYPE_KIND(c, et) == AST_TYPE_DOUBLE ? "__builtin_fabs"
                                                  : "__builtin_fabsl";
    const cc_builtin *b = cc_builtin_table_get();
    while (strcmp(b->name, name))
        b++;
    ast_qualified_type ft = cc_builtin_type(c, b);
    uint32_t sym = cc_symbol_new(c, AST_SYMBOL_BUILTIN, 0, ft, loc);
    c->syms[sym].val = b - cc_builtin_table_get();
    uint32_t fn = cc_node_new(c, AST_NODE_VARIABLE, ft, loc);
    AST_NODE(fn)->a = sym;
    cc_id_list args;
    cc_id_list_init(&args);
    cc_id_list_append(c, &args, a);
    uint32_t n = cc_node_new(c, AST_NODE_CALL, et, loc);
    AST_NODE(n)->a = cc_conversion_decay(c, fn);
    AST_NODE(n)->b = cc_id_list_freeze(c, &args);
    AST_NODE(n)->c = 1;
    return n;
}

/* A folded argument may still contain evaluated comma operands. This
 * check includes volatile/atomic reads and respects short-circuiting, while
 * leaving genuinely constant arguments free of synthetic comma expressions
 * (which would make a C23 constexpr initializer nonconstant). */
CC_PRIVATE bool cc_builtin_has_effects(cc *c, uint32_t n, bool load)
{
    if (!n)
        return false;
    const node *x = AST_NODE(n);
    if (load && (x->flags & AST_NODE_FLAG_LVALUE) &&
        ((AST_QUALIFIED_TYPE_QUALIFIERS(x->ty) & AST_QUALIFIER_VOLATILE) ||
         (AST_TYPE(c, x->ty)->flags & AST_TYPE_FLAG_ATOMIC)))
        return true;
    switch (x->op) {
    case AST_NODE_NUMBER:
    case AST_NODE_FLOAT_NUMBER:
    case AST_NODE_ZERO:
    case AST_NODE_STRING:
    case AST_NODE_VARIABLE:
    case AST_NODE_LABEL_ADDRESS:
        return false;
    case AST_NODE_ADDRESS:
    case AST_NODE_MEMBER:
        return cc_builtin_has_effects(c, x->a, false);
    case AST_NODE_CAST:
        return cc_builtin_has_effects(c, x->a,
                                      x->aux != AST_CAST_ARRAY_DECAY &&
                                          x->aux != AST_CAST_FUNCTION_DECAY);
    case AST_NODE_UNARY:
    case AST_NODE_DEREFERENCE:
        return cc_builtin_has_effects(c, x->a, true);
    case AST_NODE_INDEX:
    case AST_NODE_BINARY:
    case AST_NODE_COMMA:
    case AST_NODE_COMPLEX:
        return cc_builtin_has_effects(c, x->a, true) ||
               cc_builtin_has_effects(c, x->b, true);
    case AST_NODE_LOGICAL_AND:
    case AST_NODE_LOGICAL_OR:
    case AST_NODE_CONDITIONAL: {
        cc_value cv;
        if (cc_builtin_has_effects(c, x->a, true))
            return true;
        if (cc_eval(c, x->a, &cv)) {
            bool truth = cv.kind == CC_VALUE_INTEGER ? cv.i != 0
                         : cv.kind == CC_VALUE_FLOAT
                             ? (cv.ld ? vsoftfloat_is_nonzero(cv.q) : cv.f != 0)
                             : true;
            if (x->op == AST_NODE_CONDITIONAL)
                return cc_builtin_has_effects(c, truth ? x->b : x->c, true);
            return (x->op == AST_NODE_LOGICAL_AND ? truth : !truth) &&
                   cc_builtin_has_effects(c, x->b, true);
        }
        return cc_builtin_has_effects(c, x->b, true) ||
               cc_builtin_has_effects(c, x->c, true);
    }
    case AST_NODE_BUILTIN:
    case AST_NODE_CALL: {
        if (x->op == AST_NODE_BUILTIN && x->aux == CC_BUILTIN_CONSTANT_P)
            return false;
        cc_value value;
        if (!cc_eval(c, n, &value))
            return true;
        if (x->op == AST_NODE_CALL) {
            uint32_t fn = x->a;
            while (AST_NODE(fn)->op == AST_NODE_CAST)
                fn = AST_NODE(fn)->a;
            if (AST_NODE(fn)->op != AST_NODE_VARIABLE ||
                c->syms[AST_NODE(fn)->a].kind != AST_SYMBOL_BUILTIN)
                return true;
        }
        for (uint32_t i = 0; i < x->c; i++)
            if (cc_builtin_has_effects(c, c->lists[x->b + i], true))
                return true;
        return false;
    }
    default:
        return true;
    }
}

CC_PRIVATE uint32_t cc_vector_abs(cc *c, uint32_t arg, uint32_t loc)
{
    ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE(arg)->ty),
                       et = cc_type_is_vector(c, t) ? AST_TYPE(c, t)->base : t;
    int k = AST_TYPE_KIND(c, et);
    if ((!cc_type_is_float(c, et) &&
         (!cc_type_is_integer(c, et) || !cc_type_is_signed(c, et))) ||
        k == AST_TYPE_ENUM || k == AST_TYPE_BOOL) {
        cc_report_error(c, loc,
                        "argument to '__builtin_elementwise_abs' must be a "
                        "scalar or vector of signed integer or floating-point "
                        "type");
        return cc_node_error_new(c, loc);
    }
    /* Retain constant-expression support for scalar integer/floating args. */
    cc_value cv;
    if (!cc_type_is_vector(c, t) && cc_eval(c, arg, &cv) &&
        (cv.kind == CC_VALUE_FLOAT || cv.kind == CC_VALUE_INTEGER)) {
        /* A known value need not be a pure expression: constant evaluation
		 * of a comma expression may ignore an observable left operand.
		 * Evaluate the original exactly once, then return only its folded
		 * magnitude. Reusing arg in the integer select would evaluate it twice. */
        uint32_t folded;
        if (cv.kind == CC_VALUE_FLOAT)
            folded =
                cv.ld
                    ? cc_node_float128_new(c, cv.q & ~((u128)1 << 127), et, loc)
                    : cc_node_float_new(c, __builtin_fabs(cv.f), et, loc);
        else
            folded = cc_node_integer_new(
                c,
                cc_constant_truncate(
                    c, cv.i < 0 ? (i128)(0 - (u128)cv.i) : cv.i, et),
                et, loc);
        return cc_builtin_has_effects(c, arg, true)
                   ? cc_lowering_sequence(c, arg, folded)
                   : folded;
    }
    uint32_t sym = cc_symbol_new(c, AST_SYMBOL_VARIABLE, 0, t, loc);
    c->syms[sym].sc = AST_STORAGE_AUTO;
    uint32_t saved = cc_node_new(c, AST_NODE_VARIABLE, t, loc);
    AST_NODE(saved)->a = sym;
    AST_NODE(saved)->flags = AST_NODE_FLAG_LVALUE;
    uint32_t pre = cc_node_new_operands(c, AST_NODE_ASSIGN, t, loc, saved, arg);
    if (!cc_type_is_vector(c, t))
        return cc_lowering_sequence(c, pre,
                                    cc_builtin_elementwise_abs_lane(
                                        c, cc_node_rvalue(c, saved), et, loc));
    sym = cc_symbol_new(c, AST_SYMBOL_VARIABLE, 0, t, loc);
    c->syms[sym].sc = AST_STORAGE_AUTO;
    uint32_t out = cc_node_new(c, AST_NODE_VARIABLE, t, loc);
    AST_NODE(out)->a = sym;
    AST_NODE(out)->flags = AST_NODE_FLAG_LVALUE;
    uint32_t result = cc_node_rvalue(c, out);
    for (unsigned i = (unsigned)(cc_type_size(c, t) / cc_type_size(c, et));
         i--;) {
        uint32_t lane =
            cc_node_rvalue(c, cc_builtin_vector_lane(c, saved, et, i, loc));
        uint32_t value = cc_builtin_elementwise_abs_lane(c, lane, et, loc);
        uint32_t dst = cc_builtin_vector_lane(c, out, et, i, loc);
        result = cc_lowering_sequence(
            c, cc_node_new_operands(c, AST_NODE_ASSIGN, et, loc, dst, value),
            result);
    }
    return cc_lowering_sequence(c, pre, result);
}

CC_PRIVATE uint32_t cc_builtin_elementwise_abs_build(cc *c, const cc_builtin *b,
                                                     uint32_t loc)
{
    cc_id_list args;
    cc_builtin_arguments_parse(c, &args);
    if (args.n != 1) {
        cc_builtin_arity_check(c, loc, 1, &args);
        return cc_node_error_new(c, loc);
    }
    (void)b;
    return cc_vector_abs(c, cc_conversion_decay(c, args.v[0]), loc);
}

/* __builtin_elementwise_min and _max of two vectors of one integer type:
 * each lane the smaller (larger) of the two, as the vector operations
 * b ^ ((a ^ b) & (a < b)) with the comparison's -1 / 0 lanes as the mask;
 * both operands are evaluated once (floating-point lanes, whose NaN rules
 * minnum has, and scalars are not done). */
CC_PRIVATE uint32_t cc_vector_minmax(cc *c, bool max, uint32_t a, uint32_t b,
                                     uint32_t loc)
{
    a = cc_node_rvalue(c, a), b = cc_node_rvalue(c, b);
    ast_qualified_type vt = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE(a)->ty);
    if (!cc_type_is_vector(c, vt) ||
        !cc_type_is_compatible(
            c, vt, AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE(b)->ty)) ||
        !cc_type_is_integer(c, AST_TYPE(c, vt)->base)) {
        cc_report_error(c, loc,
                        "arguments to '__builtin_elementwise_%s' must be "
                        "vectors of one integer type",
                        max ? "max" : "min");
        return cc_node_error_new(c, loc);
    }
    uint32_t pre[2], saved[2], values[2] = {a, b};
    for (int i = 0; i < 2; i++) {
        uint32_t sym = cc_symbol_new(c, AST_SYMBOL_VARIABLE, 0, vt, loc);
        c->syms[sym].sc = AST_STORAGE_AUTO;
        saved[i] = cc_node_new(c, AST_NODE_VARIABLE, vt, loc);
        AST_NODE(saved[i])->a = sym;
        AST_NODE(saved[i])->flags = AST_NODE_FLAG_LVALUE;
        pre[i] = cc_node_new_operands(c, AST_NODE_ASSIGN, vt, loc, saved[i],
                                      values[i]);
    }
    uint32_t x = cc_node_rvalue(c, saved[0]), y = cc_node_rvalue(c, saved[1]);
    uint32_t mask =
        cc_node_binary_new(c, max ? LEX_TOKEN_GREATER : LEX_TOKEN_LESS,
                           cc_type_vector_mask(c, vt), x, y, loc);
    mask = cc_cast_insert(c, mask, vt, AST_CAST_NOP);
    uint32_t diff = cc_node_binary_new(c, LEX_TOKEN_CARET, vt, x, y, loc);
    diff = cc_node_binary_new(c, LEX_TOKEN_AMPERSAND, vt, diff, mask, loc);
    uint32_t result = cc_node_binary_new(
        c, LEX_TOKEN_CARET, vt, cc_node_rvalue(c, saved[1]), diff, loc);
    return cc_lowering_sequence(c, pre[0],
                                cc_lowering_sequence(c, pre[1], result));
}

/* __builtin_reduce_{add,max,min,and,or,xor}(v): horizontal reduction across
 * the lanes of an integer vector, op = 0..5. */
CC_PRIVATE uint32_t cc_vector_reduce(cc *c, int op, uint32_t arg, uint32_t loc)
{
    arg = cc_node_rvalue(c, arg);
    ast_qualified_type vt = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE(arg)->ty);
    if (!cc_type_is_vector(c, vt) ||
        !cc_type_is_integer(c, AST_TYPE(c, vt)->base))
        return cc_node_error_new(c, loc);
    ast_qualified_type et = AST_TYPE(c, vt)->base;
    uint32_t n = AST_TYPE(c, vt)->n;
    uint32_t sym = cc_symbol_new(c, AST_SYMBOL_VARIABLE, 0, vt, loc);
    c->syms[sym].sc = AST_STORAGE_AUTO;
    uint32_t saved = cc_node_new(c, AST_NODE_VARIABLE, vt, loc);
    AST_NODE(saved)->a = sym;
    AST_NODE(saved)->flags = AST_NODE_FLAG_LVALUE;
    uint32_t seq =
        cc_node_new_operands(c, AST_NODE_ASSIGN, vt, loc, saved, arg);
    uint32_t acc_sym = cc_symbol_new(c, AST_SYMBOL_VARIABLE, 0, et, loc);
    c->syms[acc_sym].sc = AST_STORAGE_AUTO;
    uint32_t acc = cc_node_new(c, AST_NODE_VARIABLE, et, loc);
    AST_NODE(acc)->a = acc_sym;
    AST_NODE(acc)->flags = AST_NODE_FLAG_LVALUE;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t idx =
            cc_node_integer_new(c, i, cc_type_basic(AST_TYPE_INT), loc);
        uint32_t lane =
            cc_node_new_operands(c, AST_NODE_INDEX, et, loc, saved, idx);
        AST_NODE(lane)->flags = AST_NODE_FLAG_LVALUE;
        uint32_t val = cc_node_rvalue(c, lane);
        uint32_t next = val;
        if (i > 0) {
            uint32_t cur = cc_node_rvalue(c, acc);
            if (op == 0)
                next = cc_node_binary_new(c, LEX_TOKEN_PLUS, et, cur, val, loc);
            else if (op == 1 || op == 2) {
                int cmp_tok = op == 1 ? LEX_TOKEN_GREATER : LEX_TOKEN_LESS;
                uint32_t cond = cc_node_binary_new(
                    c, cmp_tok, cc_type_basic(AST_TYPE_INT), cur, val, loc);
                next = cc_conversion_apply(
                    c, cc_conditional_build(c, cond, cur, val, loc), et);
            } else if (op == 3)
                next = cc_node_binary_new(c, LEX_TOKEN_AMPERSAND, et, cur, val,
                                          loc);
            else if (op == 4)
                next = cc_node_binary_new(c, LEX_TOKEN_PIPE, et, cur, val, loc);
            else
                next =
                    cc_node_binary_new(c, LEX_TOKEN_CARET, et, cur, val, loc);
        }
        uint32_t assign =
            cc_node_new_operands(c, AST_NODE_ASSIGN, et, loc, acc, next);
        seq = cc_lowering_sequence(c, seq, assign);
    }
    return cc_lowering_sequence(c, seq, cc_node_rvalue(c, acc));
}

CC_PRIVATE uint32_t cc_builtin_elementwise_minmax_build(cc *c,
                                                        const cc_builtin *b,
                                                        uint32_t loc)
{
    cc_id_list args;
    cc_builtin_arguments_parse(c, &args);
    if (args.n != 2) {
        cc_builtin_arity_check(c, loc, 2, &args);
        return cc_node_error_new(c, loc);
    }
    if (b->id == CC_BUILTIN_ELEMENTWISE_ADD_SAT ||
        b->id == CC_BUILTIN_ELEMENTWISE_SUB_SAT)
        return cc_vector_saturate(c, b->id == CC_BUILTIN_ELEMENTWISE_SUB_SAT,
                                  cc_conversion_decay(c, args.v[0]),
                                  cc_conversion_decay(c, args.v[1]), loc);
    return cc_vector_minmax(c, b->id == CC_BUILTIN_ELEMENTWISE_MAX,
                            cc_conversion_decay(c, args.v[0]),
                            cc_conversion_decay(c, args.v[1]), loc);
}

/* ---- vector helpers of the builtins below, in the AST's vector operations */

/* value held in an automatic temporary: *pre assigns it, the result reads it */
static uint32_t cc_vector_hold(cc *c, uint32_t value, ast_qualified_type t,
                               uint32_t *pre, uint32_t loc)
{
    uint32_t sym = cc_symbol_new(c, AST_SYMBOL_VARIABLE, 0, t, loc);
    c->syms[sym].sc = AST_STORAGE_AUTO;
    uint32_t var = cc_node_new(c, AST_NODE_VARIABLE, t, loc);
    AST_NODE(var)->a = sym;
    AST_NODE(var)->flags = AST_NODE_FLAG_LVALUE;
    uint32_t assign =
        cc_node_new_operands(c, AST_NODE_ASSIGN, t, loc, var, value);
    *pre = *pre ? cc_lowering_sequence(c, *pre, assign) : assign;
    return var;
}

static uint32_t cc_vector_op(cc *c, int op, ast_qualified_type t, uint32_t l,
                             uint32_t r, uint32_t loc)
{
    return cc_node_binary_new(c, op, t, l, r, loc);
}

/* a vector's lanes picked from a and b by indices (a, then b), of type rt */
static uint32_t cc_vector_pick(cc *c, ast_qualified_type rt, uint32_t a,
                               uint32_t b, const int *indices, unsigned n,
                               uint32_t loc)
{
    cc_id_list list;
    cc_id_list_init(&list);
    cc_id_list_append(c, &list, a);
    cc_id_list_append(c, &list, b);
    for (unsigned i = 0; i < n; i++)
        cc_id_list_append(c, &list,
                          cc_node_integer_new(
                              c, indices[i], cc_type_basic(AST_TYPE_INT), loc));
    return cc_builtin_node_new(c, CC_BUILTIN_SHUFFLEVECTOR, rt, loc, &list);
}

/* __builtin_elementwise_add_sat and _sub_sat of two vectors of one integer
 * type: the result clamped to the element's range, by vector operations
 * only.  Unsigned: a + b | (r < a), a - b & (a >= b); signed: the lanes
 * whose sign overflowed take INT_MAX or INT_MIN by a's sign. */
CC_PRIVATE uint32_t cc_vector_saturate(cc *c, bool sub, uint32_t a, uint32_t b,
                                       uint32_t loc)
{
    a = cc_node_rvalue(c, a), b = cc_node_rvalue(c, b);
    ast_qualified_type vt = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE(a)->ty);
    if (!cc_type_is_vector(c, vt) ||
        !cc_type_is_compatible(
            c, vt, AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE(b)->ty)) ||
        !cc_type_is_integer(c, AST_TYPE(c, vt)->base)) {
        cc_report_error(c, loc,
                        "arguments to '__builtin_elementwise_%s_sat' must be "
                        "vectors of one integer type",
                        sub ? "sub" : "add");
        return cc_node_error_new(c, loc);
    }
    ast_qualified_type et = AST_TYPE(c, vt)->base;
    int bits = (int)cc_type_size(c, et) * 8;
    uint32_t pre = 0;
    uint32_t x = cc_vector_hold(c, a, vt, &pre, loc);
    uint32_t y = cc_vector_hold(c, b, vt, &pre, loc);
    uint32_t xr = cc_node_rvalue(c, x), yr = cc_node_rvalue(c, y);
    uint32_t r = cc_vector_op(c, sub ? LEX_TOKEN_MINUS : LEX_TOKEN_PLUS, vt, xr,
                              yr, loc);
    ast_qualified_type mask_type = cc_type_vector_mask(c, vt);
    uint32_t result;
    if (!cc_type_is_signed(c, et)) {
        uint32_t held = cc_vector_hold(c, r, vt, &pre, loc);
        uint32_t m;
        if (sub)
            m = cc_vector_op(c, LEX_TOKEN_GREATER_EQUAL, mask_type,
                             cc_node_rvalue(c, x), cc_node_rvalue(c, y), loc);
        else
            m = cc_vector_op(c, LEX_TOKEN_LESS, mask_type,
                             cc_node_rvalue(c, held), cc_node_rvalue(c, x),
                             loc);
        m = cc_cast_insert(c, m, vt, AST_CAST_NOP);
        result = cc_vector_op(c, sub ? LEX_TOKEN_AMPERSAND : LEX_TOKEN_PIPE, vt,
                              cc_node_rvalue(c, held), m, loc);
    } else {
        uint32_t rr;
        uint32_t held = cc_vector_hold(c, r, vt, &pre, loc);
        rr = cc_node_rvalue(c, held);
        uint32_t ovf;
        if (sub)
            ovf = cc_vector_op(c, LEX_TOKEN_AMPERSAND, vt,
                               cc_vector_op(c, LEX_TOKEN_CARET, vt,
                                            cc_node_rvalue(c, x),
                                            cc_node_rvalue(c, y), loc),
                               cc_vector_op(c, LEX_TOKEN_CARET, vt,
                                            cc_node_rvalue(c, x), rr, loc),
                               loc);
        else
            ovf = cc_vector_op(c, LEX_TOKEN_AMPERSAND, vt,
                               cc_vector_op(c, LEX_TOKEN_CARET, vt,
                                            cc_node_rvalue(c, x), rr, loc),
                               cc_vector_op(c, LEX_TOKEN_CARET, vt,
                                            cc_node_rvalue(c, y),
                                            cc_node_rvalue(c, held), loc),
                               loc);
        ovf = cc_vector_op(c, LEX_TOKEN_GREATER_GREATER, vt, ovf,
                           cc_node_integer_new(c, bits - 1, et, loc), loc);
        i128 top = ((i128)1 << (bits - 1)) - 1;
        uint32_t sat = cc_vector_op(
            c, LEX_TOKEN_CARET, vt,
            cc_vector_op(c, LEX_TOKEN_GREATER_GREATER, vt, cc_node_rvalue(c, x),
                         cc_node_integer_new(c, bits - 1, et, loc), loc),
            cc_node_integer_new(c, top, et, loc), loc);
        uint32_t diff =
            cc_vector_op(c, LEX_TOKEN_AMPERSAND, vt,
                         cc_vector_op(c, LEX_TOKEN_CARET, vt,
                                      cc_node_rvalue(c, held), sat, loc),
                         ovf, loc);
        result = cc_vector_op(c, LEX_TOKEN_CARET, vt, cc_node_rvalue(c, held),
                              diff, loc);
    }
    return cc_lowering_sequence(c, pre, result);
}

static bool cc_x86_constant(cc *c, uint32_t n, const char *name, uint32_t loc,
                            i128 *v)
{
    if (cc_expression_has_side_effects(c, n) || !cc_constant_integer(c, n, v)) {
        cc_report_error(c, loc, "argument to '%s' must be a constant integer",
                        name);
        return false;
    }
    return true;
}

/* pmulhw, pmaddwd, pmaddubsw: each pair of lanes seen as one lane of twice
 * the width, whose two halves shifts take apart */
static uint32_t cc_x86_multiply_build(cc *c, const char *name, uint32_t a,
                                      uint32_t b, ast_qualified_type rt,
                                      uint32_t loc)
{
    bool bytes = !strcmp(name, "pmaddubsw128");
    bool high = !strcmp(name, "pmulhw128");
    ast_qualified_type et = cc_type_basic(AST_TYPE_INT);
    ast_qualified_type ut = cc_type_basic(AST_TYPE_UNSIGNED_INT);
    int half = 16;
    if (bytes) {
        et = cc_type_basic(AST_TYPE_SHORT);
        ut = cc_type_basic(AST_TYPE_UNSIGNED_SHORT);
        half = 8;
    }
    ast_qualified_type vt = cc_type_vector(c, et, 16);
    ast_qualified_type uvt = cc_type_vector(c, ut, 16);
    uint32_t shift = cc_node_integer_new(c, half, et, loc);
    uint32_t pre = 0;
    uint32_t x = cc_vector_hold(
        c, cc_cast_insert(c, cc_node_rvalue(c, a), vt, AST_CAST_NOP), vt, &pre,
        loc);
    uint32_t y = cc_vector_hold(
        c, cc_cast_insert(c, cc_node_rvalue(c, b), vt, AST_CAST_NOP), vt, &pre,
        loc);
    /* the low and high halves, sign-extended; pmaddubsw's first operand is
       unsigned */
    uint32_t parts[2][2];
    uint32_t operands[2] = {x, y};
    for (unsigned i = 0; i < 2; i++) {
        if (bytes && i == 0) {
            parts[0][0] =
                cc_vector_op(c, LEX_TOKEN_AMPERSAND, vt, cc_node_rvalue(c, x),
                             cc_node_integer_new(c, 0xff, et, loc), loc);
            uint32_t u =
                cc_cast_insert(c, cc_node_rvalue(c, x), uvt, AST_CAST_NOP);
            u = cc_vector_op(c, LEX_TOKEN_GREATER_GREATER, uvt, u,
                             cc_node_integer_new(c, half, ut, loc), loc);
            parts[0][1] = cc_cast_insert(c, u, vt, AST_CAST_NOP);
            continue;
        }
        uint32_t up = cc_vector_op(c, LEX_TOKEN_LESS_LESS, vt,
                                   cc_node_rvalue(c, operands[i]), shift, loc);
        parts[i][0] =
            cc_vector_op(c, LEX_TOKEN_GREATER_GREATER, vt, up, shift, loc);
        parts[i][1] = cc_vector_op(c, LEX_TOKEN_GREATER_GREATER, vt,
                                   cc_node_rvalue(c, operands[i]), shift, loc);
    }
    uint32_t low =
        cc_vector_op(c, LEX_TOKEN_STAR, vt, parts[0][0], parts[1][0], loc);
    uint32_t top =
        cc_vector_op(c, LEX_TOKEN_STAR, vt, parts[0][1], parts[1][1], loc);
    uint32_t result;
    if (bytes) {
        result = cc_vector_saturate(c, false, low, top, loc);
    } else if (high) {
        uint32_t down = cc_cast_insert(c, low, uvt, AST_CAST_NOP);
        down = cc_vector_op(c, LEX_TOKEN_GREATER_GREATER, uvt, down,
                            cc_node_integer_new(c, half, ut, loc), loc);
        uint32_t keep =
            cc_vector_op(c, LEX_TOKEN_AMPERSAND, uvt,
                         cc_cast_insert(c, top, uvt, AST_CAST_NOP),
                         cc_node_integer_new(c, 0xffff0000u, ut, loc), loc);
        result = cc_vector_op(c, LEX_TOKEN_PIPE, uvt, down, keep, loc);
    } else {
        /* wraps when all four factors are -32768 */
        result = cc_vector_op(c, LEX_TOKEN_PLUS, uvt,
                              cc_cast_insert(c, low, uvt, AST_CAST_NOP),
                              cc_cast_insert(c, top, uvt, AST_CAST_NOP), loc);
    }
    return cc_lowering_sequence(c, pre,
                                cc_cast_insert(c, result, rt, AST_CAST_NOP));
}

/* packsswb, packssdw, packuswb, packusdw: both operands' signed lanes clamped
 * to the narrow type's range, then the low half of each (little-endian) */
static uint32_t cc_x86_pack_build(cc *c, const char *name, uint32_t a,
                                  uint32_t b, ast_qualified_type rt,
                                  uint32_t loc)
{
    bool dwords = strstr(name, "dw") != NULL;
    bool to_unsigned = name[4] == 'u';
    ast_qualified_type et = cc_type_basic(AST_TYPE_SHORT);
    ast_qualified_type narrow = cc_type_basic(AST_TYPE_CHAR);
    int bits = 8;
    if (dwords) {
        et = cc_type_basic(AST_TYPE_INT);
        narrow = cc_type_basic(AST_TYPE_SHORT);
        bits = 16;
    }
    int most = (1 << (bits - 1)) - 1;
    int least = -most - 1;
    if (to_unsigned) {
        most = (1 << bits) - 1;
        least = 0;
    }
    ast_qualified_type vt = cc_type_vector(c, et, 16);
    ast_qualified_type nt = cc_type_vector(c, narrow, 16);
    uint32_t pre = 0;
    uint32_t packed[2];
    uint32_t operands[2] = {a, b};
    for (unsigned i = 0; i < 2; i++) {
        uint32_t v = cc_vector_hold(
            c,
            cc_cast_insert(c, cc_node_rvalue(c, operands[i]), vt, AST_CAST_NOP),
            vt, &pre, loc);
        uint32_t zero =
            cc_vector_op(c, LEX_TOKEN_CARET, vt, cc_node_rvalue(c, v),
                         cc_node_rvalue(c, v), loc);
        uint32_t lo = cc_vector_hold(
            c,
            cc_vector_op(c, LEX_TOKEN_PLUS, vt, zero,
                         cc_node_integer_new(c, least, et, loc), loc),
            vt, &pre, loc);
        uint32_t hi = cc_vector_hold(
            c,
            cc_vector_op(c, LEX_TOKEN_PLUS, vt, cc_node_rvalue(c, lo),
                         cc_node_integer_new(c, most - least, et, loc), loc),
            vt, &pre, loc);
        uint32_t clamped = cc_vector_hold(
            c, cc_vector_minmax(c, true, v, lo, loc), vt, &pre, loc);
        clamped = cc_vector_hold(
            c, cc_vector_minmax(c, false, clamped, hi, loc), vt, &pre, loc);
        packed[i] =
            cc_cast_insert(c, cc_node_rvalue(c, clamped), nt, AST_CAST_NOP);
    }
    int idx[16];
    unsigned n = 16;
    if (dwords)
        n = 8;
    for (unsigned i = 0; i < n; i++)
        idx[i] = (int)(2 * i);
    uint32_t picked = cc_vector_pick(c, nt, packed[0], packed[1], idx, n, loc);
    return cc_lowering_sequence(c, pre,
                                cc_cast_insert(c, picked, rt, AST_CAST_NOP));
}

/* the SSE2 builtins that are shuffles, shifts and conversions: ordinary
 * vector operations (a shuffle, a shift by a splat, a converted vector), so
 * that code generation picks the instructions; true when name is one */
CC_PRIVATE bool cc_builtin_x86_sse2_build(cc *c, const char *name,
                                          cc_id_list *args,
                                          ast_qualified_type rt, uint32_t loc,
                                          uint32_t *out)
{
    ast_qualified_type bytes =
        cc_type_vector(c, cc_type_basic(AST_TYPE_CHAR), 16);
    ast_qualified_type it = cc_type_basic(AST_TYPE_INT);
    i128 imm = 0;
    int idx[16];
    if (!strcmp(name, "pmulhw128") || !strcmp(name, "pmaddwd128") ||
        !strcmp(name, "pmaddubsw128") || !strncmp(name, "pack", 4)) {
        if (args->n != 2) {
            *out = cc_node_error_new(c, loc);
            return true;
        }
        if (name[1] == 'a' && name[2] == 'c')
            *out = cc_x86_pack_build(c, name, args->v[0], args->v[1], rt, loc);
        else
            *out =
                cc_x86_multiply_build(c, name, args->v[0], args->v[1], rt, loc);
        return true;
    }
    if (!strcmp(name, "emms")) {
        *out = cc_cast_insert(c, cc_node_integer_new(c, 0, it, loc),
                              cc_type_basic(AST_TYPE_VOID), AST_CAST_VOID);
        return true;
    }
    if (!strcmp(name, "undef128")) {
        /* any value: a fresh automatic variable's */
        uint32_t sym = cc_symbol_new(c, AST_SYMBOL_VARIABLE, 0, rt, loc);
        c->syms[sym].sc = AST_STORAGE_AUTO;
        uint32_t var = cc_node_new(c, AST_NODE_VARIABLE, rt, loc);
        AST_NODE(var)->a = sym;
        AST_NODE(var)->flags = AST_NODE_FLAG_LVALUE;
        *out = cc_node_rvalue(c, var);
        return true;
    }
    if (!strcmp(name, "pshufd") || !strcmp(name, "pshuflw") ||
        !strcmp(name, "pshufhw")) {
        if (args->n != 2 || !cc_x86_constant(c, args->v[1], name, loc, &imm)) {
            *out = cc_node_error_new(c, loc);
            return true;
        }
        unsigned n = name[5] == 'd' ? 4 : 8, lo = name[5] == 'l',
                 hi = name[5] == 'h';
        for (unsigned i = 0; i < n; i++) {
            unsigned pick = (unsigned)(imm >> (2 * (i & 3))) & 3;
            idx[i] = (int)i;
            if (n == 4 || (lo && i < 4))
                idx[i] = (int)pick;
            else if (hi && i >= 4)
                idx[i] = (int)(4 + pick);
        }
        uint32_t pre = 0;
        uint32_t v = cc_vector_hold(c, args->v[0], rt, &pre, loc);
        *out = cc_lowering_sequence(c, pre,
                                    cc_vector_pick(c, rt, cc_node_rvalue(c, v),
                                                   cc_node_rvalue(c, v), idx, n,
                                                   loc));
        return true;
    }
    if (!strcmp(name, "pslldqi128_byteshift") ||
        !strcmp(name, "psrldqi128_byteshift")) {
        if (args->n != 2 || !cc_x86_constant(c, args->v[1], name, loc, &imm)) {
            *out = cc_node_error_new(c, loc);
            return true;
        }
        bool left = name[2] == 'l';
        long n = imm < 0 || imm > 16 ? 16 : (long)imm;
        for (int i = 0; i < 16; i++) {
            long from = left ? i - n : i + n;
            /* index 0..15: the zero vector (first operand), 16..31: the data */
            idx[i] = from >= 0 && from < 16 ? 16 + (int)from : 0;
        }
        uint32_t pre = 0;
        uint32_t v =
            cc_vector_hold(c,
                           cc_cast_insert(c, cc_node_rvalue(c, args->v[0]),
                                          bytes, AST_CAST_NOP),
                           bytes, &pre, loc);
        uint32_t zero =
            cc_vector_op(c, LEX_TOKEN_CARET, bytes, cc_node_rvalue(c, v),
                         cc_node_rvalue(c, v), loc);
        uint32_t shifted =
            cc_vector_pick(c, bytes, zero, cc_node_rvalue(c, v), idx, 16, loc);
        *out = cc_lowering_sequence(
            c, pre, cc_cast_insert(c, shifted, rt, AST_CAST_NOP));
        return true;
    }
    if (!strcmp(name, "cvttps2dq")) {
        cc_id_list one;
        cc_id_list_init(&one);
        cc_id_list_append(c, &one, args->v[0]);
        *out = cc_builtin_node_new(c, CC_BUILTIN_CONVERTVECTOR, rt, loc, &one);
        return true;
    }
    if (!strcmp(name, "cvttpd2dq")) {
        ast_qualified_type pair = cc_type_vector(c, it, 8);
        cc_id_list one;
        cc_id_list_init(&one);
        cc_id_list_append(c, &one, args->v[0]);
        uint32_t pre = 0;
        uint32_t narrowed = cc_vector_hold(
            c,
            cc_builtin_node_new(c, CC_BUILTIN_CONVERTVECTOR, pair, loc, &one),
            pair, &pre, loc);
        for (int i = 0; i < 4; i++)
            idx[i] = i;
        uint32_t zero =
            cc_vector_op(c, LEX_TOKEN_CARET, pair, cc_node_rvalue(c, narrowed),
                         cc_node_rvalue(c, narrowed), loc);
        *out = cc_lowering_sequence(c, pre,
                                    cc_vector_pick(c, rt,
                                                   cc_node_rvalue(c, narrowed),
                                                   zero, idx, 4, loc));
        return true;
    }
    if (!strcmp(name, "vec_ext_v8hi") || !strcmp(name, "vec_set_v8hi")) {
        bool set = name[4] == 's';
        uint32_t index = args->v[set ? 2 : 1];
        if (args->n != (set ? 3u : 2u) ||
            !cc_x86_constant(c, index, name, loc, &imm) || imm < 0 || imm > 7) {
            *out = cc_node_error_new(c, loc);
            return true;
        }
        ast_qualified_type vt =
            AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE(args->v[0])->ty);
        ast_qualified_type et = cc_type_basic(AST_TYPE_SHORT);
        uint32_t pre = 0;
        uint32_t v = cc_vector_hold(c, args->v[0], vt, &pre, loc);
        if (!set) {
            *out = cc_lowering_sequence(
                c, pre,
                cc_node_rvalue(
                    c, cc_builtin_vector_lane(c, v, et, (unsigned)imm, loc)));
            return true;
        }
        uint32_t store = cc_node_new_operands(
            c, AST_NODE_ASSIGN, et, loc,
            cc_builtin_vector_lane(c, v, et, (unsigned)imm, loc), args->v[1]);
        *out = cc_lowering_sequence(
            c, pre, cc_lowering_sequence(c, store, cc_node_rvalue(c, v)));
        return true;
    }
    /* shifts of every lane by one count: pslliw.. as left shifts, psrli.. as
       logical (unsigned lanes) and psrai.. as arithmetic right shifts; a
       count at or past the lane's width gives 0 (sign bits for psra) */
    if (name[0] == 'p' && name[1] == 's' && strlen(name) == 9 &&
        !strcmp(name + 5, "i128") && (name[2] == 'l' || name[2] == 'r') &&
        (name[3] == 'l' || name[3] == 'a')) {
        char width = name[4];
        if (args->n != 2 || (width != 'w' && width != 'd' && width != 'q')) {
            *out = cc_node_error_new(c, loc);
            return true;
        }
        bool left = name[2] == 'l', arithmetic = name[3] == 'a';
        ast_qualified_type vt =
            AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE(args->v[0])->ty);
        ast_qualified_type et = AST_TYPE(c, vt)->base;
        ast_qualified_type work = vt;
        if (!arithmetic)
            work = cc_type_vector(c, cc_type_unsigned(c, et), 16);
        ast_qualified_type wet = AST_TYPE(c, work)->base;
        int bits = (int)cc_type_size(c, et) * 8;
        int op = left ? LEX_TOKEN_LESS_LESS : LEX_TOKEN_GREATER_GREATER;
        uint32_t pre = 0;
        uint32_t v =
            cc_vector_hold(c,
                           cc_cast_insert(c, cc_node_rvalue(c, args->v[0]),
                                          work, AST_CAST_NOP),
                           work, &pre, loc);
        uint32_t count =
            cc_conversion_apply(c, cc_node_rvalue(c, args->v[1]),
                                cc_type_basic(AST_TYPE_UNSIGNED_INT));
        uint32_t held = cc_vector_hold(
            c, count, cc_type_basic(AST_TYPE_UNSIGNED_INT), &pre, loc);
        uint32_t cnt = cc_node_rvalue(c, held);
        ast_qualified_type ut = cc_type_basic(AST_TYPE_UNSIGNED_INT);
        uint32_t valid =
            cc_node_binary_new(c, LEX_TOKEN_LESS, it, cnt,
                               cc_node_integer_new(c, bits, ut, loc), loc);
        uint32_t result;
        if (arithmetic) {
            /* the count clamped to bits - 1 */
            uint32_t clamped = cc_conditional_build(
                c, valid, cc_node_rvalue(c, held),
                cc_node_integer_new(c, bits - 1, ut, loc), loc);
            result = cc_vector_op(c, op, work, cc_node_rvalue(c, v),
                                  cc_conversion_apply(c, clamped, wet), loc);
        } else {
            uint32_t low = cc_node_binary_new(
                c, LEX_TOKEN_AMPERSAND, ut, cc_node_rvalue(c, held),
                cc_node_integer_new(c, bits - 1, ut, loc), loc);
            uint32_t moved =
                cc_vector_op(c, op, work, cc_node_rvalue(c, v),
                             cc_conversion_apply(c, low, wet), loc);
            /* all ones where the count is below the width, else 0 */
            uint32_t keep = cc_conditional_build(
                c, valid,
                cc_node_integer_new(c, cc_constant_truncate(c, -1, wet), wet,
                                    loc),
                cc_node_integer_new(c, 0, wet, loc), loc);
            result =
                cc_vector_op(c, LEX_TOKEN_AMPERSAND, work, moved, keep, loc);
        }
        *out = cc_lowering_sequence(
            c, pre, cc_cast_insert(c, result, rt, AST_CAST_NOP));
        return true;
    }
    return false;
}
