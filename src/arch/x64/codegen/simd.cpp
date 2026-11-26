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
        /* SSE2 has no pmulld: x64_simd_multiply_dwords */
        p.op = x64_simd_by_width(size, NULL, "pmullw", "pmuludq", NULL);
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

/* an instruction with a control byte: pshufd, shufps ... */
static void x64_simd_pick(codegen_context *e, const char *op, int control,
                          int source, int d)
{
    char text[64];
    snprintf(text, sizeof text, "\t%s\t$%d, %%xmm%d, %%xmm%d\n", op, control,
             source - XMM0, d - XMM0);
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

/* x *= y on four dwords: pmuludq multiplies lanes 0 and 2 into qwords, a
   second one the lanes shifted down, and the low halves are interleaved; y and
   t are scratch afterwards */
static void x64_simd_multiply_dwords(codegen_context *e, int x, int y, int t)
{
    x64_simd_two(e, "movdqa", x, t);
    x64_simd_two(e, "pmuludq", y, t);
    x64_simd_shift_by(e, "psrlq", 32, x);
    x64_simd_shift_by(e, "psrlq", 32, y);
    x64_simd_two(e, "pmuludq", y, x);
    x64_simd_pick(e, "pshufd", 8, t, t);
    x64_simd_pick(e, "pshufd", 8, x, x);
    x64_simd_two(e, "punpckldq", x, t);
    x64_simd_two(e, "movdqa", t, x);
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
        if (op == LEX_TOKEN_STAR && size == 4 &&
            !codegen_type_is_float(c, T(c, vt)->base))
            x64_simd_multiply_dwords(e, x, y, t);
        else
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

/* ---- shuffles: a __builtin_shufflevector of two 16-byte vectors, as the 16
 * source bytes it takes (0-15 the first operand, 16-31 the second, -1 any) */

typedef enum x64_shuffle_kind {
    X64_SHUFFLE_NONE,
    X64_SHUFFLE_UNPACK, /* punpckl/h of lanes `width` wide */
    X64_SHUFFLE_DWORDS, /* pshufd */
    X64_SHUFFLE_PAIRS,  /* shufps: two dwords of each source */
    X64_SHUFFLE_LOW,    /* pshuflw */
    X64_SHUFFLE_HIGH,   /* pshufhw */
    X64_SHUFFLE_ROTATE, /* 16 consecutive bytes of the two */
    X64_SHUFFLE_EVEN    /* the even lanes `width` wide of both */
} x64_shuffle_kind;

typedef struct x64_shuffle_plan {
    x64_shuffle_kind kind;
    int first, second; /* the operands used: 0 or 1 */
    int width;         /* unpack and even: lane bytes; rotate: the start */
    int control;
    bool high;
} x64_shuffle_plan;

static bool x64_shuffle_byte_is(int have, int want)
{
    return have < 0 || have == want;
}

static bool x64_shuffle_unpack_matches(const int *bytes,
                                       const x64_shuffle_plan *p)
{
    int base = 0;
    if (p->high)
        base = 8;
    for (int i = 0; i < 16; i++) {
        int lane = i / p->width;
        int operand = p->first;
        if (lane & 1)
            operand = p->second;
        int want = operand * 16 + base + (lane / 2) * p->width + i % p->width;
        if (!x64_shuffle_byte_is(bytes[i], want))
            return false;
    }
    return true;
}

/* the source of the `width`-byte lane at i, as operand * lanes + lane;
   -1 any, -2 not a whole aligned lane */
static int x64_shuffle_lane(const int *bytes, int i, int width)
{
    int lane = -1;
    for (int k = 0; k < width; k++) {
        int b = bytes[i * width + k];
        if (b < 0)
            continue;
        if (b % width != k || (lane >= 0 && lane != b / width))
            return -2;
        lane = b / width;
    }
    return lane;
}

/* four lanes at `from` taken from the four at `from` of one operand: their
   control byte */
static bool x64_shuffle_control(const int *bytes, int width, int from,
                                int *operand, int *control)
{
    int lanes = 16 / width;
    *operand = -1;
    *control = 0;
    for (int i = 0; i < 4; i++) {
        int s = x64_shuffle_lane(bytes, from + i, width);
        int pick = i;
        if (s == -2)
            return false;
        if (s >= 0) {
            pick = s % lanes - from;
            if (pick < 0 || pick > 3 ||
                (*operand >= 0 && *operand != s / lanes))
                return false;
            *operand = s / lanes;
        }
        *control |= pick << (2 * i);
    }
    if (*operand < 0)
        *operand = 0;
    return true;
}

static bool x64_shuffle_words_keep(const int *bytes, int from, int operand)
{
    for (int i = from * 2; i < from * 2 + 8; i++)
        if (!x64_shuffle_byte_is(bytes[i], operand * 16 + i))
            return false;
    return true;
}

static x64_shuffle_plan x64_shuffle_plan_for(const int *bytes)
{
    x64_shuffle_plan p = {};

    for (p.width = 1; p.width <= 8; p.width *= 2)
        for (int pair = 0; pair < 8; pair++) {
            p.first = pair & 1;
            p.second = (pair >> 1) & 1;
            p.high = pair >> 2;
            if (x64_shuffle_unpack_matches(bytes, &p)) {
                p.kind = X64_SHUFFLE_UNPACK;
                return p;
            }
        }
    p = {};
    if (x64_shuffle_control(bytes, 4, 0, &p.first, &p.control)) {
        p.kind = X64_SHUFFLE_DWORDS;
        return p;
    }
    {
        int low = 0, high = 0;
        bool pairs = true;
        p.first = p.second = -1;
        for (int i = 0; i < 4; i++) {
            int s = x64_shuffle_lane(bytes, i, 4);
            int *operand = &p.first;
            if (i >= 2)
                operand = &p.second;
            if (s == -1)
                continue;
            if (s < 0 || (*operand >= 0 && *operand != s / 4)) {
                pairs = false;
                break;
            }
            *operand = s / 4;
            if (i < 2)
                low |= (s % 4) << (2 * i);
            else
                high |= (s % 4) << (2 * (i - 2));
        }
        if (pairs) {
            if (p.first < 0)
                p.first = 0;
            if (p.second < 0)
                p.second = 0;
            p.control = low | (high << 4);
            p.kind = X64_SHUFFLE_PAIRS;
            return p;
        }
    }
    if (x64_shuffle_control(bytes, 2, 0, &p.first, &p.control) &&
        x64_shuffle_words_keep(bytes, 4, p.first)) {
        p.kind = X64_SHUFFLE_LOW;
        return p;
    }
    if (x64_shuffle_control(bytes, 2, 4, &p.first, &p.control) &&
        x64_shuffle_words_keep(bytes, 0, p.first)) {
        p.kind = X64_SHUFFLE_HIGH;
        return p;
    }
    for (int pair = 0; pair < 4; pair++) {
        p.first = pair & 1;
        p.second = pair >> 1;
        for (int start = 1; start < 16; start++) {
            bool match = true;
            for (int i = 0; i < 16; i++) {
                int at = start + i;
                int want = p.first * 16 + at;
                if (at >= 16)
                    want = p.second * 16 + at - 16;
                match = match && x64_shuffle_byte_is(bytes[i], want);
            }
            if (match) {
                p.width = start;
                p.kind = X64_SHUFFLE_ROTATE;
                return p;
            }
        }
        for (int width = 1; width <= 2; width++) {
            bool match = true;
            for (int i = 0; i < 16; i++) {
                int half = p.first;
                if (i >= 8)
                    half = p.second;
                int want =
                    half * 16 + 2 * width * ((i % 8) / width) + i % width;
                match = match && x64_shuffle_byte_is(bytes[i], want);
            }
            if (match) {
                p.width = width;
                p.kind = X64_SHUFFLE_EVEN;
                return p;
            }
        }
    }
    p.kind = X64_SHUFFLE_NONE;
    return p;
}

VPRIV bool x64_vector_shuffle_exists(codegen_context *e, const int *bytes)
{
    return !x64_target_has_no_simd(e) &&
           x64_shuffle_plan_for(bytes).kind != X64_SHUFFLE_NONE;
}

/* the low `width` bytes of each pair of x, sign or zero bits above, ready for
   the saturating pack to keep them as they are */
static void x64_simd_even_prepare(codegen_context *e, int x, int width)
{
    if (width == 1) {
        x64_simd_shift_by(e, "psllw", 8, x);
        x64_simd_shift_by(e, "psrlw", 8, x);
    } else {
        x64_simd_shift_by(e, "pslld", 16, x);
        x64_simd_shift_by(e, "psrad", 16, x);
    }
}

/* ra, rb: the operands' addresses */
VPRIV bool x64_vector_shuffle(codegen_context *e, const int *bytes, int ra,
                              int rb, const codegen_lvalue *d)
{
    if (x64_target_has_no_simd(e))
        return false;
    x64_shuffle_plan p = x64_shuffle_plan_for(bytes);
    if (p.kind == X64_SHUFFLE_NONE)
        return false;
    int address[2] = {ra, rb};
    int x = x64_ftreg(e), y = x64_ftreg(e);

    x64_simd_operand(e, x, address[p.first], true, 1);
    if (p.kind == X64_SHUFFLE_UNPACK || p.kind == X64_SHUFFLE_PAIRS ||
        p.kind == X64_SHUFFLE_ROTATE || p.kind == X64_SHUFFLE_EVEN)
        x64_simd_operand(e, y, address[p.second], true, 1);
    switch (p.kind) {
    case X64_SHUFFLE_UNPACK: {
        const char *op =
            x64_simd_by_width((unsigned)p.width, "punpcklbw", "punpcklwd",
                              "punpckldq", "punpcklqdq");
        if (p.high)
            op = x64_simd_by_width((unsigned)p.width, "punpckhbw", "punpckhwd",
                                   "punpckhdq", "punpckhqdq");
        x64_simd_two(e, op, y, x);
        break;
    }
    case X64_SHUFFLE_DWORDS:
        x64_simd_pick(e, "pshufd", p.control, x, x);
        break;
    case X64_SHUFFLE_PAIRS:
        x64_simd_pick(e, "shufps", p.control, y, x);
        break;
    case X64_SHUFFLE_LOW:
        x64_simd_pick(e, "pshuflw", p.control, x, x);
        break;
    case X64_SHUFFLE_HIGH:
        x64_simd_pick(e, "pshufhw", p.control, x, x);
        break;
    case X64_SHUFFLE_ROTATE:
        x64_simd_shift_by(e, "psrldq", p.width, x);
        x64_simd_shift_by(e, "pslldq", 16 - p.width, y);
        x64_simd_two(e, "por", y, x);
        break;
    case X64_SHUFFLE_EVEN:
        x64_simd_even_prepare(e, x, p.width);
        x64_simd_even_prepare(e, y, p.width);
        if (p.width == 1)
            x64_simd_two(e, "packuswb", y, x);
        else
            x64_simd_two(e, "packssdw", y, x);
        break;
    case X64_SHUFFLE_NONE:
        break;
    }
    x64_store(e, x, d, 16);
    x64_tfree(e, x);
    x64_tfree(e, y);
    return true;
}
