/* vcc front end: x86 SSE and pcmpstr builtins in ordinary vector lanes; see builtin-table.c for where a builtin's other facts live */
#include "cc-internal.h"

CC_PRIVATE uint32_t cc_builtin_x86_addcarry_build(cc *c, const cc_builtin *b,
                                                  const cc_id_list *args,
                                                  uint32_t loc)
{
    uint32_t saved[4], pre = 0, assign;

    if (!CC_X86_64(c)) {
        cc_report_error(c, loc, "builtin '%s' requires an x86-64 target",
                        b->name);
        return cc_node_error_new(c, loc);
    }
    if (args->n != 4) {
        cc_builtin_arity_check(c, loc, 4, args);
        return cc_node_error_new(c, loc);
    }
    for (unsigned i = 0; i < 4; ++i) {
        c->cv_arg = i;
        c->cv_callee = loc;
        uint32_t arg =
            cc_conversion_apply_as(c, cc_conversion_decay(c, args->v[i]),
                                   cc_builtin_signature_type(c, b->sig[i + 1]),
                                   CC_CONVERSION_CONTEXT_ARGUMENT, loc);
        if (AST_NODE_IS_ERROR(arg))
            return cc_node_error_new(c, loc);
        saved[i] = cc_lowering_save(c, arg, &assign);
        pre = cc_lowering_sequence(c, pre, assign);
    }

    /* Add the two words and then the normalized carry byte. Either
       addition can overflow. Compute the carry before storing the result
       through a pointer that can alias an input. */
    uint32_t carry_in =
        cc_conversion_apply(c, saved[0], cc_type_basic(AST_TYPE_BOOL));
    uint32_t first =
        cc_binary_build(c, LEX_TOKEN_PLUS, saved[1], saved[2], loc);
    first = cc_lowering_save(c, first, &assign);
    pre = cc_lowering_sequence(c, pre, assign);
    uint32_t sum = cc_binary_build(c, LEX_TOKEN_PLUS, first, carry_in, loc);
    sum = cc_lowering_save(c, sum, &assign);
    pre = cc_lowering_sequence(c, pre, assign);
    uint32_t carry_first =
        cc_binary_build(c, LEX_TOKEN_LESS, first, saved[1], loc);
    uint32_t carry_second = cc_binary_build(c, LEX_TOKEN_LESS, sum, first, loc);
    uint32_t carry =
        cc_binary_build(c, LEX_TOKEN_PIPE, carry_first, carry_second, loc);
    carry = cc_lowering_save(
        c, cc_conversion_apply(c, carry, cc_type_basic(AST_TYPE_UNSIGNED_CHAR)),
        &assign);
    pre = cc_lowering_sequence(c, pre, assign);
    uint32_t store =
        cc_assignment_build(c, 0, cc_dereference_build(c, saved[3], loc),
                            cc_node_rvalue(c, sum), loc);
    return cc_lowering_sequence(
        c, pre, cc_lowering_sequence(c, store, cc_node_rvalue(c, carry)));
}

typedef struct x86_instruction_name {
    const char *name, *signature;
} x86_instruction_name;

static const x86_instruction_name x86_instruction_names[] = {
#define X86_INSTRUCTION(name, signature, mnemonic, form, feature)              \
    {name, signature},
#include "frontend/x86-instructions.inc"
#undef X86_INSTRUCTION
};

/* a builtin of frontend/x86-instructions.inc: the node keeps its arguments and
   its row; code generation checks the feature and emits the instruction */
CC_PRIVATE uint32_t cc_builtin_x86_instruction_build(cc *c, const cc_builtin *b,
                                                     cc_id_list *args,
                                                     ast_qualified_type rt,
                                                     uint32_t loc)
{
    unsigned row = 0;
    unsigned count =
        sizeof x86_instruction_names / sizeof *x86_instruction_names;

    while (row < count && strcmp(x86_instruction_names[row].name, b->name))
        row++;
    if (!CC_X86_64(c)) {
        cc_report_error(c, loc, "builtin '%s' requires an x86-64 target",
                        b->name);
        return cc_node_error_new(c, loc);
    }
    if (row == count ||
        args->n + 1 != strlen(x86_instruction_names[row].signature))
        return cc_node_error_new(c, loc);
    cc_id_list_append(
        c, args, cc_node_integer_new(c, row, cc_type_basic(AST_TYPE_INT), loc));
    return cc_builtin_node_new(c, CC_BUILTIN_X86_INSTRUCTION, rt, loc, args);
}

/* a builtin of frontend/x86-stencil-names.inc: as above, its code the
   stencil of its row */
CC_PRIVATE uint32_t cc_builtin_x86_stencil_build(cc *c, const cc_builtin *b,
                                                 cc_id_list *args,
                                                 ast_qualified_type rt,
                                                 uint32_t loc)
{
    if (!CC_X86_64(c)) {
        cc_report_error(c, loc, "builtin '%s' requires an x86-64 target",
                        b->name);
        return cc_node_error_new(c, loc);
    }
    /* (a wrong number of arguments: the call has reported it) */
    if (args->n != AST_TYPE(c, cc_builtin_type(c, b))->n)
        return cc_node_error_new(c, loc);
    cc_id_list_append(c, args,
                      cc_node_integer_new(c, cc_builtin_stencil_row(b),
                                          cc_type_basic(AST_TYPE_INT), loc));
    return cc_builtin_node_new(c, CC_BUILTIN_X86_STENCIL, rt, loc, args);
}

/* clang's type-generic vector builtins that are stencils of x86-64 here
   (tools/gen-x86stencils.py, GENERIC): the row is that of the first operand's
   shape; the result is of its type, a reduction's of its element's */
CC_PRIVATE uint32_t cc_x86_generic_build(cc *c, const char *name,
                                         const uint32_t *values, uint32_t n,
                                         uint32_t loc)
{
    cc_id_list args;
    cc_id_list_init(&args);
    if (!CC_X86_64(c) || !n) {
        cc_report_error(c, loc, "builtin '%s' requires an x86-64 target", name);
        return cc_node_error_new(c, loc);
    }
    /* the operands in their own types: no integer promotion */
    for (uint32_t i = 0; i < n; i++) {
        uint32_t a = cc_conversion_decay(c, values[i]);
        cc_id_list_append(
            c, &args,
            cc_conversion_apply(
                c, a, AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE(a)->ty)));
    }
    ast_qualified_type t =
        AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE(args.v[0])->ty);
    ast_qualified_type element = t;
    unsigned width = 0;
    if (cc_type_is_vector(c, t)) {
        width = (unsigned)cc_type_size(c, t);
        element = AST_TYPE(c, t)->base;
    }
    char kind = 'f';
    if (cc_type_is_integer(c, element))
        kind = 'i';
    char shaped[80];
    snprintf(shaped, sizeof shaped, "%s/%u%c%u/%u", name, width, kind,
             (unsigned)cc_type_size(c, element), n);

    const cc_builtin *found = cc_builtin_stencil_named(shaped);
    if (!found) {
        cc_report_error(c, loc, "'%s' of this operand type is not supported",
                        name);
        return cc_node_error_new(c, loc);
    }
    ast_qualified_type result = t;
    if (strstr(name, "reduce"))
        result = element;
    cc_id_list_append(c, &args,
                      cc_node_integer_new(c, cc_builtin_stencil_row(found),
                                          cc_type_basic(AST_TYPE_INT), loc));
    return cc_builtin_node_new(c, CC_BUILTIN_X86_STENCIL, result, loc, &args);
}

/* The builtin converts its constant control to char, as Clang does. The final hidden
 * argument identifies the result projection and explicit-length variant. */
CC_PRIVATE uint32_t cc_builtin_x86_pcmpstr_build(cc *c, const cc_builtin *b,
                                                 cc_id_list *args,
                                                 ast_qualified_type rt,
                                                 uint32_t loc)
{
    bool explicit_length = strstr(b->name, "pcmpestr") != NULL;
    unsigned count = explicit_length ? 5 : 3;
    i128 imm;
    if (!CC_X86_64(c)) {
        cc_report_error(c, loc, "'%s' requires an x86-64 target", b->name);
        return cc_node_error_new(c, loc);
    }
    if (args->n != count)
        return cc_node_error_new(c, loc);
    if (cc_expression_has_side_effects(c, args->v[count - 1]) ||
        !cc_constant_integer(c, args->v[count - 1], &imm)) {
        cc_report_error(c, loc, "argument to '%s' must be a constant integer",
                        b->name);
        return cc_node_error_new(c, loc);
    }
    const char *name = strstr(b->name, "str") + 3;
    unsigned kind = name[0] == 'm'   ? 1
                    : name[1] == 'a' ? 2
                    : name[1] == 'c' ? 3
                    : name[1] == 'o' ? 4
                    : name[1] == 's' ? 5
                    : name[1] == 'z' ? 6
                                     : 0;
    cc_id_list_append(c, args,
                      cc_node_integer_new(c, kind | (explicit_length ? 8 : 0),
                                          cc_type_basic(AST_TYPE_INT), loc));
    return cc_builtin_node_new(c, CC_BUILTIN_X86_PCMPSTR, rt, loc, args);
}

CC_PRIVATE uint32_t cc_builtin_x86_simd_build(cc *c, const cc_builtin *b,
                                              cc_id_list *args,
                                              ast_qualified_type rt,
                                              uint32_t loc)
{
    const char *name = b->name + sizeof("__builtin_ia32_") - 1;
    {
        uint32_t done;
        if (cc_builtin_x86_sse2_build(c, name, args, rt, loc, &done))
            return done;
    }
    if (!strcmp(name, "shufps")) {
        i128 imm;
        if (args->n != 3 || !cc_constant_integer(c, args->v[2], &imm) ||
            imm < 0 || imm > 255) {
            cc_report_error(
                c, loc,
                "argument to '%s' must be a constant integer in range [0, 255]",
                b->name);
            return cc_node_error_new(c, loc);
        }
        cc_id_list lanes;
        cc_id_list_init(&lanes);
        cc_id_list_append(c, &lanes, args->v[0]);
        cc_id_list_append(c, &lanes, args->v[1]);
        for (unsigned i = 0; i < 4; i++)
            cc_id_list_append(
                c, &lanes,
                cc_node_integer_new(
                    c, ((unsigned)imm >> (2 * i) & 3) + (i >= 2 ? 4 : 0),
                    cc_type_basic(AST_TYPE_INT), loc));
        return cc_builtin_node_new(c, CC_BUILTIN_SHUFFLEVECTOR, rt, loc,
                                   &lanes);
    }
    if (!strcmp(name, "movmskps")) {
        if (args->n != 1)
            return cc_node_error_new(c, loc);
        ast_qualified_type vt = AST_NODE(args->v[0])->ty;
        uint32_t sym = cc_symbol_new(c, AST_SYMBOL_VARIABLE, 0, vt, loc);
        c->syms[sym].sc = AST_STORAGE_AUTO;
        uint32_t saved = cc_node_new(c, AST_NODE_VARIABLE, vt, loc);
        AST_NODE(saved)->a = sym;
        AST_NODE(saved)->flags = AST_NODE_FLAG_LVALUE;
        uint32_t result =
            cc_node_integer_new(c, 0, cc_type_basic(AST_TYPE_INT), loc);
        for (unsigned i = 0; i < 4; i++) {
            cc_id_list one;
            cc_id_list_init(&one);
            cc_id_list_append(
                c, &one,
                cc_node_rvalue(
                    c, cc_builtin_vector_lane(
                           c, saved, cc_type_basic(AST_TYPE_FLOAT), i, loc)));
            uint32_t sign = cc_builtin_node_new(
                c, CC_BUILTIN_SIGNBIT, cc_type_basic(AST_TYPE_INT), loc, &one);
            sign = cc_conversion_apply(
                c, cc_conversion_apply(c, sign, cc_type_basic(AST_TYPE_BOOL)),
                cc_type_basic(AST_TYPE_INT));
            sign = cc_node_binary_new(
                c, LEX_TOKEN_LESS_LESS, cc_type_basic(AST_TYPE_INT), sign,
                cc_node_integer_new(c, i, cc_type_basic(AST_TYPE_INT), loc),
                loc);
            result = cc_node_binary_new(c, LEX_TOKEN_PIPE,
                                        cc_type_basic(AST_TYPE_INT), result,
                                        sign, loc);
        }
        return cc_lowering_sequence(c,
                                    cc_node_new_operands(c, AST_NODE_ASSIGN, vt,
                                                         loc, saved,
                                                         args->v[0]),
                                    result);
    }
    if (!strcmp(name, "pmovmskb128")) {
        /* the sign bit of each byte, lane i in bit i: the bytes as
           unsigned, ((lane >> 7) & 1) << i, ored */
        if (args->n != 1)
            return cc_node_error_new(c, loc);
        ast_qualified_type vt = AST_QUALIFIED_TYPE_UNQUALIFIED(
                               AST_NODE(args->v[0])->ty),
                           byte = cc_type_basic(AST_TYPE_UNSIGNED_CHAR);
        uint32_t sym = cc_symbol_new(c, AST_SYMBOL_VARIABLE, 0, vt, loc);
        c->syms[sym].sc = AST_STORAGE_AUTO;
        uint32_t saved = cc_node_new(c, AST_NODE_VARIABLE, vt, loc);
        AST_NODE(saved)->a = sym;
        AST_NODE(saved)->flags = AST_NODE_FLAG_LVALUE;
        ast_qualified_type it = cc_type_basic(AST_TYPE_INT);
        uint32_t result = cc_node_integer_new(c, 0, it, loc);
        for (unsigned i = 0; i < 16; i++) {
            uint32_t lane = cc_conversion_apply(
                c,
                cc_node_rvalue(c,
                               cc_builtin_vector_lane(c, saved, byte, i, loc)),
                it);
            lane = cc_node_binary_new(c, LEX_TOKEN_GREATER_GREATER, it, lane,
                                      cc_node_integer_new(c, 7, it, loc), loc);
            lane = cc_node_binary_new(c, LEX_TOKEN_LESS_LESS, it, lane,
                                      cc_node_integer_new(c, i, it, loc), loc);
            result =
                cc_node_binary_new(c, LEX_TOKEN_PIPE, it, result, lane, loc);
        }
        return cc_lowering_sequence(c,
                                    cc_node_new_operands(c, AST_NODE_ASSIGN, vt,
                                                         loc, saved,
                                                         args->v[0]),
                                    result);
    }
    if (args->n != 2)
        return cc_node_error_new(c, loc);
    bool shift = name[0] == 'p', cmp = name[0] == 'c';
    ast_qualified_type et = shift ? cc_type_basic(AST_TYPE_UNSIGNED_SHORT)
                            : cmp ? cc_type_basic(AST_TYPE_INT)
                                  : cc_type_basic(AST_TYPE_FLOAT);
    ast_qualified_type vt = cc_type_vector(c, et, 16);
    uint32_t saved[2], pre[2];
    for (unsigned i = 0; i < 2; i++) {
        ast_qualified_type t = AST_NODE(args->v[i])->ty;
        uint32_t sym = cc_symbol_new(c, AST_SYMBOL_VARIABLE, 0, t, loc);
        c->syms[sym].sc = AST_STORAGE_AUTO;
        saved[i] = cc_node_new(c, AST_NODE_VARIABLE, t, loc);
        AST_NODE(saved[i])->a = sym;
        AST_NODE(saved[i])->flags = AST_NODE_FLAG_LVALUE;
        pre[i] = cc_node_new_operands(c, AST_NODE_ASSIGN, t, loc, saved[i],
                                      args->v[i]);
    }
    uint32_t sym = cc_symbol_new(c, AST_SYMBOL_VARIABLE, 0, vt, loc);
    c->syms[sym].sc = AST_STORAGE_AUTO;
    uint32_t out = cc_node_new(c, AST_NODE_VARIABLE, vt, loc);
    AST_NODE(out)->a = sym;
    AST_NODE(out)->flags = AST_NODE_FLAG_LVALUE;
    uint32_t result =
        cc_cast_insert(c, cc_node_rvalue(c, out), rt, AST_CAST_NOP);
    for (unsigned i = 16 / cc_type_size(c, et); i--;) {
        uint32_t a = cc_node_rvalue(
            c, cc_builtin_vector_lane(
                   c, saved[0], shift ? et : cc_type_basic(AST_TYPE_FLOAT), i,
                   loc));
        uint32_t value;
        if (shift) {
            uint32_t count =
                cc_conversion_apply(c, cc_node_rvalue(c, saved[1]),
                                    cc_type_basic(AST_TYPE_UNSIGNED_INT));
            uint32_t valid = cc_node_binary_new(
                c, LEX_TOKEN_LESS, cc_type_basic(AST_TYPE_INT), count,
                cc_node_integer_new(c, 16, cc_type_basic(AST_TYPE_UNSIGNED_INT),
                                    loc),
                loc);
            value = cc_node_binary_new(
                c,
                name[2] == 'l' ? LEX_TOKEN_LESS_LESS
                               : LEX_TOKEN_GREATER_GREATER,
                cc_type_basic(AST_TYPE_UNSIGNED_INT),
                cc_conversion_apply(c, a, cc_type_basic(AST_TYPE_UNSIGNED_INT)),
                count, loc);
            value = cc_conditional_build(
                c, valid, value,
                cc_node_integer_new(c, 0, cc_type_basic(AST_TYPE_UNSIGNED_INT),
                                    loc),
                loc);
            value = cc_conversion_apply(c, value, et);
        } else {
            uint32_t rhs = cc_node_rvalue(
                c, cc_builtin_vector_lane(
                       c, saved[1], cc_type_basic(AST_TYPE_FLOAT), i, loc));
            uint32_t cond =
                cc_node_binary_new(c,
                                   cmp ? LEX_TOKEN_LESS_EQUAL
                                   : !strcmp(name, "maxps") ? LEX_TOKEN_GREATER
                                                            : LEX_TOKEN_LESS,
                                   cc_type_basic(AST_TYPE_INT), a, rhs, loc);
            value = cmp ? cc_conditional_build(
                              c, cond, cc_node_integer_new(c, 0, et, loc),
                              cc_node_integer_new(c, -1, et, loc), loc)
                        : cc_conditional_build(c, cond, a, rhs, loc);
        }
        uint32_t dst = cc_builtin_vector_lane(c, out, et, i, loc);
        result = cc_lowering_sequence(
            c, cc_node_new_operands(c, AST_NODE_ASSIGN, et, loc, dst, value),
            result);
    }
    return cc_lowering_sequence(c, pre[0],
                                cc_lowering_sequence(c, pre[1], result));
}
