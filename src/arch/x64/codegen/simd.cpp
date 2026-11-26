/*
 * simd.cpp: GNU vector operations on 16-byte vectors as SSE2 instructions,
 * in place of the walker's lane-by-lane loop (codegen/walk/expression.cpp,
 * walk_vector_lanes_emit).  The operands are loaded into xmm temporaries from
 * the vectors' addresses, one instruction (a few for the predicates SSE2 does
 * not have) is applied, and the result is stored to its frame object.  What
 * SSE2 cannot express stays lane by lane.
 */
#include "x64-internal.h"
#include "../../../codegen/walk/proto.h"

/* how an operation is done: op on (xmm a, xmm b), the operands swapped, the
   result inverted; both signs flipped first for an unsigned order */
typedef struct x64_simd_plan {
    const char *op;
    bool swap, invert, bias;
} x64_simd_plan;

static const char *x64_simd_by_width(unsigned size, const char *b,
                                     const char *w, const char *d,
                                     const char *q)
{
    switch (size) {
    case 1:
        return b;
    case 2:
        return w;
    case 4:
        return d;
    case 8:
        return q;
    default:
        return NULL;
    }
}

static x64_simd_plan x64_simd_float_plan(int op, bool single)
{
    x64_simd_plan p = {};
    switch (op) {
    case LEX_TOKEN_PLUS:
        p.op = single ? "addps" : "addpd";
        break;
    case LEX_TOKEN_MINUS:
        p.op = single ? "subps" : "subpd";
        break;
    case LEX_TOKEN_STAR:
        p.op = single ? "mulps" : "mulpd";
        break;
    case LEX_TOKEN_SLASH:
        p.op = single ? "divps" : "divpd";
        break;
    case LEX_TOKEN_EQUAL_EQUAL:
        p.op = single ? "cmpeqps" : "cmpeqpd";
        break;
    case LEX_TOKEN_EXCLAMATION_EQUAL:
        p.op = single ? "cmpneqps" : "cmpneqpd";
        break;
    case LEX_TOKEN_LESS:
    case LEX_TOKEN_GREATER:
        p.op = single ? "cmpltps" : "cmpltpd";
        p.swap = op == LEX_TOKEN_GREATER;
        break;
    case LEX_TOKEN_LESS_EQUAL:
    case LEX_TOKEN_GREATER_EQUAL:
        p.op = single ? "cmpleps" : "cmplepd";
        p.swap = op == LEX_TOKEN_GREATER_EQUAL;
        break;
    default:
        break;
    }
    return p;
}

static x64_simd_plan x64_simd_integer_plan(int op, unsigned size,
                                           bool is_signed)
{
    x64_simd_plan p = {};
    switch (op) {
    case LEX_TOKEN_PLUS:
        p.op = x64_simd_by_width(size, "paddb", "paddw", "paddd", "paddq");
        break;
    case LEX_TOKEN_MINUS:
        p.op = x64_simd_by_width(size, "psubb", "psubw", "psubd", "psubq");
        break;
    case LEX_TOKEN_STAR:
        p.op = x64_simd_by_width(size, NULL, "pmullw", NULL, NULL);
        break;
    case LEX_TOKEN_AMPERSAND:
        p.op = "pand";
        break;
    case LEX_TOKEN_PIPE:
        p.op = "por";
        break;
    case LEX_TOKEN_CARET:
        p.op = "pxor";
        break;
    case LEX_TOKEN_EQUAL_EQUAL:
    case LEX_TOKEN_EXCLAMATION_EQUAL:
        p.op = x64_simd_by_width(size, "pcmpeqb", "pcmpeqw", "pcmpeqd", NULL);
        p.invert = op == LEX_TOKEN_EXCLAMATION_EQUAL;
        break;
    case LEX_TOKEN_LESS:
    case LEX_TOKEN_GREATER:
    case LEX_TOKEN_LESS_EQUAL:
    case LEX_TOKEN_GREATER_EQUAL:
        /* only "greater" exists: a < b is b > a, a <= b is not a > b */
        p.op = x64_simd_by_width(size, "pcmpgtb", "pcmpgtw", "pcmpgtd", NULL);
        p.swap = op == LEX_TOKEN_LESS || op == LEX_TOKEN_GREATER_EQUAL;
        p.invert = op == LEX_TOKEN_LESS_EQUAL || op == LEX_TOKEN_GREATER_EQUAL;
        p.bias = !is_signed;
        break;
    case LEX_TOKEN_LESS_LESS:
        p.op = x64_simd_by_width(size, NULL, "psllw", "pslld", "psllq");
        break;
    case LEX_TOKEN_GREATER_GREATER:
        if (is_signed)
            p.op = x64_simd_by_width(size, NULL, "psraw", "psrad", NULL);
        else
            p.op = x64_simd_by_width(size, NULL, "psrlw", "psrld", "psrlq");
        break;
    default:
        break;
    }
    return p;
}

static x64_simd_plan x64_simd_plan_for(codegen_context *e, int op,
                                       ast_qualified_type vt, bool a_vector,
                                       bool b_vector, bool unary)
{
    cc *c = e->compile_context;
    x64_simd_plan none = {};
    ast_qualified_type et = T(c, vt)->base;
    unsigned size = (unsigned)codegen_type_size(c, et);
    bool shift = op == LEX_TOKEN_LESS_LESS || op == LEX_TOKEN_GREATER_GREATER;

    if (x64_target_has_no_simd(e) || codegen_type_size(c, vt) != 16 ||
        codegen_type_is_bool_vector(c, vt) || walk_type_is_wide(c, et))
        return none;
    if (codegen_type_is_float(c, et)) {
        /* (float and double only; a scalar operand stays lane by lane) */
        if (unary || !a_vector || !b_vector || (size != 4 && size != 8) ||
            walk_type_is_in_memory(e, et))
            return none;
        return x64_simd_float_plan(op, size == 4);
    }
    if (unary) {
        x64_simd_plan p = {};
        if (op == LEX_TOKEN_TILDE)
            p = x64_simd_integer_plan(LEX_TOKEN_CARET, size, true);
        else if (op == LEX_TOKEN_MINUS)
            p = x64_simd_integer_plan(LEX_TOKEN_MINUS, size, true);
        return p;
    }
    /* a shift takes one count for every lane */
    if (shift && (!a_vector || b_vector))
        return none;
    return x64_simd_integer_plan(op, size, walk_type_is_signed(c, et));
}

VPRIV bool x64_vector_operation_exists(codegen_context *e, int op,
                                       ast_qualified_type vt, bool a_vector,
                                       bool b_vector, bool unary)
{
    return x64_simd_plan_for(e, op, vt, a_vector, b_vector, unary).op != NULL;
}

static void x64_simd_text(codegen_context *e, const char *text)
{
    record *r = x64_record_emit(e, X64_OP_RAW);
    r->offset = (int32_t)e->raw.n;
    r->reference = (uint32_t)strlen(text);
    vtext_string_append(&e->raw, text);
}

static void x64_simd_two(codegen_context *e, const char *op, int source, int d)
{
    char text[64];
    snprintf(text, sizeof text, "\t%s\t%%xmm%d, %%xmm%d\n", op, source - XMM0,
             d - XMM0);
    x64_simd_text(e, text);
}

static void x64_simd_shift_by(codegen_context *e, const char *op, int count,
                              int d)
{
    char text[64];
    snprintf(text, sizeof text, "\t%s\t$%d, %%xmm%d\n", op, count, d - XMM0);
    x64_simd_text(e, text);
}

/* the scalar in the general register r in every lane of x */
static void x64_simd_splat(codegen_context *e, int x, int r, unsigned size)
{
    char text[128];
    int n = x - XMM0;

    x64_record_emit_three_registers(e, X64_OP_MOV, size == 8 ? 8 : 4, x, r,
                                    X64_REGISTER_NONE);
    if (size == 8) {
        x64_simd_two(e, "punpcklqdq", x, x);
        return;
    }
    if (size == 1)
        x64_simd_two(e, "punpcklbw", x, x);
    if (size <= 2)
        x64_simd_two(e, "punpcklwd", x, x);
    snprintf(text, sizeof text, "\tpshufd\t$0, %%xmm%d, %%xmm%d\n", n, n);
    x64_simd_text(e, text);
}

static void x64_simd_operand(codegen_context *e, int x, int r, bool vector,
                             unsigned size)
{
    if (!vector) {
        x64_simd_splat(e, x, r, size);
        return;
    }
    codegen_lvalue l = {.kind = CODEGEN_LVALUE_REGISTER,
                        .register_number = (uint8_t)r,
                        .align = 1};
    x64_load(e, x, &l, 16, false);
}

/* ra, rb: a vector operand's address, or the scalar operand's value */
VPRIV bool x64_vector_operation(codegen_context *e, int op,
                                ast_qualified_type vt, bool a_vector,
                                bool b_vector, int ra, int rb,
                                const codegen_lvalue *d, bool unary)
{
    cc *c = e->compile_context;
    x64_simd_plan p = x64_simd_plan_for(e, op, vt, a_vector, b_vector, unary);

    if (!p.op)
        return false;
    unsigned size = (unsigned)codegen_type_size(c, T(c, vt)->base);
    int x = x64_ftreg(e), y = x64_ftreg(e), t = x64_ftreg(e);
    bool shift = op == LEX_TOKEN_LESS_LESS || op == LEX_TOKEN_GREATER_GREATER;

    if (unary) {
        /* -a is 0 - a, ~a is a ^ -1 */
        x64_simd_operand(e, y, ra, true, size);
        x64_simd_two(e, op == LEX_TOKEN_TILDE ? "pcmpeqd" : "pxor", x, x);
        x64_simd_two(e, p.op, y, x);
    } else {
        x64_simd_operand(e, x, ra, a_vector, size);
        if (shift)
            x64_record_emit_three_registers(e, X64_OP_MOV, 4, y, rb,
                                            X64_REGISTER_NONE);
        else
            x64_simd_operand(e, y, rb, b_vector, size);
        if (p.bias) {
            /* flip both sign bits: all ones, shifted to the sign alone */
            x64_simd_two(e, "pcmpeqd", t, t);
            if (size == 4) {
                x64_simd_shift_by(e, "pslld", 31, t);
            } else {
                x64_simd_shift_by(e, "psllw", 15, t);
                /* 0x8000 words saturate to 0x80 bytes */
                if (size == 1)
                    x64_simd_two(e, "packsswb", t, t);
            }
            x64_simd_two(e, "pxor", t, x);
            x64_simd_two(e, "pxor", t, y);
        }
        if (p.swap) {
            int s = x;
            x = y;
            y = s;
        }
        x64_simd_two(e, p.op, y, x);
        if (p.invert) {
            x64_simd_two(e, "pcmpeqd", t, t);
            x64_simd_two(e, "pxor", t, x);
        }
    }
    x64_store(e, x, d, 16);
    x64_tfree(e, x);
    x64_tfree(e, y);
    x64_tfree(e, t);
    return true;
}
