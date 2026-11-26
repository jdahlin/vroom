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
                                       bool b_vector, bool unary,
                                       unsigned width = 16)
{
    cc *c = e->compile_context;
    x64_simd_plan none = {};
    ast_qualified_type et = T(c, vt)->base;
    unsigned size = (unsigned)codegen_type_size(c, et);
    bool shift = op == LEX_TOKEN_LESS_LESS || op == LEX_TOKEN_GREATER_GREATER;

    if (x64_target_has_no_simd(e) || codegen_type_size(c, vt) != width ||
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

/* ---- builtins that are one instruction (frontend/x86-instructions.inc) */

typedef enum x64_form {
    BINARY,         /* op b, a */
    UNARY,          /* op a, a */
    BINARY_CONTROL, /* op $k, b, a */
    UNARY_CONTROL,  /* op $k, a, a */
    BINARY_XMM0,    /* op b, a with the third operand in xmm0 */
    SHIFT,          /* op count, a: the count in an xmm */
    LOAD,           /* op (p), a */
    TO_GENERAL,     /* op a, r */
    COMPARE,        /* the mask's lowest bit of cmp b, a */
    COMPARE_SWAPPED,
    PTEST /* ptest b, a, then the mnemonic's condition */
} x64_form;

typedef struct x64_instruction {
    const char *name, *signature, *mnemonic;
    x64_form form;
    const char *feature;
} x64_instruction;

static const x64_instruction x64_instructions[] = {
#define X86_INSTRUCTION(name, signature, mnemonic, form, feature)              \
    {name, signature, mnemonic, form, feature},
#include "frontend/x86-instructions.inc"
#undef X86_INSTRUCTION
};

static void x64_simd_general_text(codegen_context *e, const char *before, int r,
                                  int size, const char *after)
{
    char line[128];
    tbuf text = {};
    text.p = line;
    text.cap = sizeof line;
    vtext_string_append(&text, before);
    x64_register_name_append(&text, r, size);
    vtext_string_append(&text, after);
    vtext_append(&text, "", 1);
    x64_simd_text(e, line);
}

/* ---- builtins that are a stencil of clang's code (tools/gen-x86stencils.py) */

typedef struct x64_stencil {
    uint8_t features;  /* in x64_stencil_features_names */
    uint8_t constants; /* the constant operands, as a mask */
    uint8_t groups;
} x64_stencil;

/* a builtin's variants of one length: one code, and what differs */
typedef struct x64_stencil_group {
    uint32_t code;    /* in the code bytes: constants, then code */
    uint32_t columns; /* ... : `stored` bytes per variant */
    uint16_t shape;   /* in x64_stencil_shapes */
} x64_stencil_group;

typedef struct x64_stencil_shape {
    uint16_t fixups; /* in x64_stencil_fixups: stored, then patched */
    uint16_t keys;   /* in x64_stencil_keys */
    uint16_t length;
    uint16_t constants;
    uint16_t stored;
    uint16_t patched;
    uint16_t ranges;
} x64_stencil_shape;

typedef struct x64_stencil_key {
    uint32_t key; /* the constants, a byte each */
    uint16_t keys;
} x64_stencil_key;

/* (no pointers in these tables but the few feature lists: no relocations) */
static const char *const x64_stencil_features_names[] = {
#define X86_STENCIL_FEATURES
#include "x86-stencils.inc"
#undef X86_STENCIL_FEATURES
};

static const x64_stencil x64_stencil_kinds[] = {
#define X86_STENCIL_KINDS
#include "x86-stencils.inc"
#undef X86_STENCIL_KINDS
};

static const uint8_t x64_stencil_rows[] = {
#define X86_STENCIL_ROWS
#include "x86-stencils.inc"
#undef X86_STENCIL_ROWS
};

static const x64_stencil_shape x64_stencil_shapes[] = {
#define X86_STENCIL_SHAPES
#include "x86-stencils.inc"
#undef X86_STENCIL_SHAPES
};

static const x64_stencil_group x64_stencil_groups[] = {
#define X86_STENCIL_GROUPS
#include "x86-stencils.inc"
#undef X86_STENCIL_GROUPS
};

static const x64_stencil_key x64_stencil_keys[] = {
#define X86_STENCIL_KEYS
#include "x86-stencils.inc"
#undef X86_STENCIL_KEYS
};

static const uint32_t x64_stencil_fixups[] = {
#define X86_STENCIL_FIXUPS
#include "x86-stencils.inc"
#undef X86_STENCIL_FIXUPS
};

static const uint64_t x64_stencil_words[] = {
#define X86_STENCIL_WORDS
#include "x86-stencils.inc"
#undef X86_STENCIL_WORDS
};

static const uint8_t *x64_stencil_bytes(void)
{
    return (const uint8_t *)(const void *)x64_stencil_words;
}

/* the names, one after another */
static const char x64_stencil_names[] =
#define X86_STENCIL(name, signature) name "\0"
#include "frontend/x86-stencil-names.inc"
#undef X86_STENCIL
    ;

static const char *x64_stencil_name(unsigned row)
{
    const char *name = x64_stencil_names;

    while (row--)
        name += strlen(name) + 1;
    return name; /* (without "__builtin_") */
}

/* whether the target has the features: names joined by ',' (all of) and '|'
   (one of), with parentheses */
static bool x64_stencil_features(codegen_context *e, const char *&p,
                                 const x86_features &have)
{
    bool any = false, all = true;

    for (;;) {
        bool one = true;
        if (*p == '(') {
            p++;
            one = x64_stencil_features(e, p, have);
            if (*p == ')')
                p++;
        } else {
            char name[32];
            size_t n = strcspn(p, ",|()");
            if (n && n < sizeof name) {
                memcpy(name, p, n);
                name[n] = 0;
                int bit = x86_feature_find(name);
                one = bit >= 0 && x86_feature_has(have, (unsigned)bit);
            }
            p += n;
        }
        all = all && one;
        if (*p == ',') {
            p++;
            continue;
        }
        any = any || all;
        if (*p != '|')
            return any;
        p++;
        all = true;
    }
}

static void x64_stencil_text(codegen_context *e, const uint8_t *code,
                             unsigned length)
{
    for (unsigned i = 0; i < length; i += 16) {
        char text[16 * 4 + 16];
        int n = snprintf(text, sizeof text, "\t.byte\t");

        for (unsigned k = i; k < length && k < i + 16; k++) {
            const char *comma = ",";
            if (k == i)
                comma = "";
            n += snprintf(text + n, sizeof text - (size_t)n, "%s%u", comma,
                          code[k]);
        }
        snprintf(text + n, sizeof text - (size_t)n, "\n");
        x64_simd_text(e, text);
    }
}

/* the group of row's builtin with the variant for `key` and its place in it,
   or NULL */
static const x64_stencil_group *x64_stencil_find(unsigned row, uint32_t key,
                                                 unsigned *variant)
{
    unsigned first = 0;
    for (unsigned i = 0; i < row; i++)
        first += x64_stencil_kinds[x64_stencil_rows[i]].groups;

    unsigned count = x64_stencil_kinds[x64_stencil_rows[row]].groups;
    for (unsigned i = 0; i < count; i++) {
        const x64_stencil_group *g = &x64_stencil_groups[first + i];
        const x64_stencil_shape *shape = &x64_stencil_shapes[g->shape];
        unsigned before = 0;

        for (unsigned k = 0; k < shape->ranges; k++) {
            const x64_stencil_key *range = &x64_stencil_keys[shape->keys + k];

            if (key >= range->key && key - range->key < range->keys) {
                *variant = before + (key - range->key);
                return g;
            }
            before += range->keys;
        }
    }
    return NULL;
}

/* a variant's code: the group's, with its own bytes and constants put in;
   the constants it reads rip-relative go right before it, aligned, with a
   jump around them */
static void x64_stencil_emit(codegen_context *e, const x64_stencil_group *g,
                             unsigned variant, uint32_t key)
{
    const x64_stencil_shape *shape = &x64_stencil_shapes[g->shape];
    unsigned size = (unsigned)shape->length + shape->constants;
    uint8_t *code = (uint8_t *)malloc(size);
    if (!code)
        vout_of_memory();
    memcpy(code, x64_stencil_bytes() + g->code, size);
    const uint32_t *fixup = &x64_stencil_fixups[shape->fixups];
    const uint8_t *own =
        x64_stencil_bytes() + g->columns + (size_t)variant * shape->stored;
    for (unsigned i = 0; i < shape->stored; i++)
        code[fixup[i]] = own[i];
    for (unsigned i = shape->stored;
         i < (unsigned)shape->stored + shape->patched; i++) {
        unsigned constant = (fixup[i] >> 24) - 1;
        code[fixup[i] & 0xffffff] = (uint8_t)(key >> (8 * constant));
    }
    if (shape->constants) {
        x64_simd_text(e, "\tjmp\t99f\n\t.p2align\t6\n");
        x64_stencil_text(e, code, shape->constants);
        x64_simd_text(e, "99:\n");
    }
    x64_stencil_text(e, code + shape->constants, shape->length);
    free(code);
}

/* the operands and the result in one frame block, at multiples of 16 (the
   result first), its address in rdi, then clang's code for the builtin */
static codegen_value x64_x86_stencil(codegen_context *e, uint32_t n)
{
    cc *c = e->compile_context;
    const node *x = N(n);
    const uint32_t *arg = &c->lists[x->b];
    unsigned count = x->c - 1;
    unsigned row = (unsigned)c->ipool[N(arg[count])->a];
    const x64_stencil *s = &x64_stencil_kinds[x64_stencil_rows[row]];
    uint32_t fn = e->walk_state->inline_call
                      ? e->walk_state->inline_call->function
                      : e->walk_state->function_symbol;
    const char *features = x64_stencil_features_names[s->features];

    x64_nofp(e);
    if (!x64_stencil_features(e, features, cc_x86_features_get(c, fn)))
        codegen_fatal_report(
            e, x->loc, "'__builtin_%s' needs target feature %s",
            x64_stencil_name(row), x64_stencil_features_names[s->features]);

    uint32_t key = 0;
    unsigned constants = 0;
    uint64_t result_size = 0;
    if (KIND(c, x->ty) != AST_TYPE_VOID)
        result_size = (codegen_type_size(c, x->ty) + 15) & ~(uint64_t)15;
    uint64_t size = result_size;
    for (unsigned i = 0; i < count; i++) {
        if (!(s->constants & (1u << i))) {
            size += (codegen_type_size(c, N(arg[i])->ty) + 15) & ~(uint64_t)15;
            continue;
        }
        cc_value value;
        if (!cc_eval(c, arg[i], &value) || value.kind != CC_VALUE_INTEGER)
            codegen_fatal_report(
                e, x->loc,
                "argument to '__builtin_%s' must be a constant integer",
                x64_stencil_name(row));
        key |= (uint32_t)(value.i & 0xff) << (8 * constants++);
    }
    unsigned variant = 0;
    const x64_stencil_group *found = x64_stencil_find(row, key, &variant);
    if (!found)
        codegen_fatal_report(
            e, x->loc, "constant argument to '__builtin_%s' is out of range",
            x64_stencil_name(row));

    codegen_lvalue block = {.kind = CODEGEN_LVALUE_FRAME, .align = 16};
    if (size)
        block.offset = walk_slot_temporary(e, size, 16);
    codegen_lvalue operand = block;
    operand.offset += (int64_t)result_size;
    for (unsigned i = 0; i < count; i++) {
        if (s->constants & (1u << i))
            continue;
        walk_initializer_emit(e, &operand, N(arg[i])->ty, arg[i]);
        operand.offset += (int64_t)((codegen_type_size(c, N(arg[i])->ty) + 15) &
                                    ~(uint64_t)15);
    }

    /* the code uses the caller-saved registers as it likes, and the stack
       below rsp: as a call */
    x64_lowering_context *X = e->x64_context;
    uint32_t saved = X->temporaries_in_use & X64_TEMPORARIES,
             fsaved = X->temporaries_in_use & X64_FLOAT_TEMPORARIES;
    e->has_calls = true;
    x64_temporaries_save(e, saved, fsaved);
    int pad = 8;
    if (X->outstanding_pushes & 1)
        pad = 0;
    if (pad)
        x64_record_emit(e, X64_OP_ADJSP)->immediate = -pad;
    if (size)
        x64_record_emit_memory(e, X64_OP_LEA, 8, RDI, &block, false);
    x64_stencil_emit(e, found, variant, key);
    if (pad)
        x64_record_emit(e, X64_OP_ADJSP)->immediate = pad;
    x64_temporaries_restore(e, saved, fsaved);

    if (!result_size)
        return codegen_value_constant(0);
    return walk_lvalue_reload(e, &block, x->ty);
}

VPRIV codegen_value x64_x86_instruction(codegen_context *e, uint32_t n)
{
    cc *c = e->compile_context;
    const node *x = N(n);
    if (x->aux == CC_BUILTIN_X86_STENCIL)
        return x64_x86_stencil(e, n);
    const uint32_t *arg = &c->lists[x->b];
    unsigned count = x->c - 1;
    const x64_instruction *in =
        &x64_instructions[(unsigned)c->ipool[N(arg[count])->a]];
    uint32_t fn = e->walk_state->inline_call
                      ? e->walk_state->inline_call->function
                      : e->walk_state->function_symbol;
    int feature = x86_feature_find(in->feature);

    if (feature < 0 ||
        !x86_feature_has(cc_x86_features_get(c, fn), (unsigned)feature))
        codegen_fatal_report(e, x->loc, "'%s' needs target feature %s",
                             in->name, in->feature);
    bool control = in->form == BINARY_CONTROL || in->form == UNARY_CONTROL;
    unsigned operands = count;
    long long immediate = 0;
    if (control) {
        cc_value value;
        operands--;
        if (!cc_eval(c, arg[operands], &value) ||
            value.kind != CC_VALUE_INTEGER)
            codegen_fatal_report(e, x->loc,
                                 "argument to '%s' must be a constant integer",
                                 in->name);
        immediate = (long long)(value.i & 0xff);
    }
    /* every operand first: evaluating one may use the registers */
    codegen_value values[3];
    int r[3] = {0, 0, 0};
    for (unsigned i = 0; i < operands; i++) {
        values[i] = walk_value_emit(e, arg[i]);
        r[i] = walk_value_to_register(e, &values[i]);
    }
    int a = x64_ftreg(e), b = x64_ftreg(e);
    char text[96];
    if (in->form == LOAD) {
        snprintf(text, sizeof text, "\t%s\t(", in->mnemonic);
        char after[32];
        snprintf(after, sizeof after, "), %%xmm%d\n", a - XMM0);
        x64_simd_general_text(e, text, r[0], 8, after);
    } else {
        x64_simd_operand(e, a, r[0], true, 1);
        if (in->form == SHIFT)
            x64_record_emit_three_registers(e, X64_OP_MOV, 4, b, r[1],
                                            X64_REGISTER_NONE);
        else if (operands > 1)
            x64_simd_operand(e, b, r[1], true, 1);
    }
    switch (in->form) {
    case BINARY:
    case SHIFT:
        x64_simd_two(e, in->mnemonic, b, a);
        break;
    case UNARY:
        x64_simd_two(e, in->mnemonic, a, a);
        break;
    case BINARY_CONTROL:
        x64_simd_pick(e, in->mnemonic, (int)immediate, b, a);
        break;
    case UNARY_CONTROL:
        x64_simd_pick(e, in->mnemonic, (int)immediate, a, a);
        break;
    case BINARY_XMM0:
        x64_simd_operand(e, XMM0, r[2], true, 1);
        snprintf(text, sizeof text, "\t%s\t%%xmm0, %%xmm%d, %%xmm%d\n",
                 in->mnemonic, b - XMM0, a - XMM0);
        x64_simd_text(e, text);
        break;
    case COMPARE:
        x64_simd_two(e, in->mnemonic, b, a);
        break;
    case COMPARE_SWAPPED:
        x64_simd_two(e, in->mnemonic, a, b);
        x64_simd_two(e, "movdqa", b, a);
        break;
    case LOAD:
    case TO_GENERAL:
    case PTEST:
        break;
    }
    for (unsigned i = 0; i < operands; i++)
        x64_tfree(e, r[i]);
    codegen_value result = codegen_value_constant(0);
    if (in->signature[0] == 'i' || in->signature[0] == 'u' ||
        in->signature[0] == 'w' || in->signature[0] == 'W') {
        int size = 4;
        if (in->signature[0] == 'w' || in->signature[0] == 'W')
            size = 8;
        int d = x64_treg(e);
        if (in->form == TO_GENERAL) {
            snprintf(text, sizeof text, "\t%s\t%%xmm%d, ", in->mnemonic,
                     a - XMM0);
            x64_simd_general_text(e, text, d, size, "\n");
        } else if (in->form == PTEST) {
            x64_simd_two(e, "ptest", b, a);
            snprintf(text, sizeof text, "\t%s\t", in->mnemonic);
            x64_simd_general_text(e, text, d, 1, "\n");
            x64_simd_general_text(e, "\tandl\t$255, ", d, 4, "\n");
        } else {
            x64_simd_shift_by(e, "pslld", 31, a);
            x64_simd_shift_by(e, "psrld", 31, a);
            snprintf(text, sizeof text, "\tmovd\t%%xmm%d, ", a - XMM0);
            x64_simd_general_text(e, text, d, 4, "\n");
        }
        result = walk_value_from_register(d);
    } else {
        codegen_lvalue l = {.kind = CODEGEN_LVALUE_FRAME,
                            .align = 16,
                            .offset = walk_slot_temporary(e, 16, 16)};
        x64_store(e, a, &l, 16);
        result = walk_value_from_register(x64_laddr(e, &l));
    }
    x64_tfree(e, a);
    x64_tfree(e, b);
    return result;
}

/* ---- the same instructions as VIR operations (arch/x64/vir) */

/* the lane-wise operators' single instructions */
static const char *const x64_x86_operators[] = {
    "paddb",      "paddw",     "paddd",     "paddq",     "psubb",
    "psubw",      "psubd",     "psubq",     "pmullw",    "pand",
    "por",        "pxor",      "pcmpeqb",   "pcmpeqw",   "pcmpeqd",
    "pcmpgtb",    "pcmpgtw",   "pcmpgtd",   "addps",     "addpd",
    "subps",      "subpd",     "mulps",     "mulpd",     "divps",
    "divpd",      "cmpeqps",   "cmpeqpd",   "cmpneqps",  "cmpneqpd",
    "cmpltps",    "cmpltpd",   "cmpleps",   "cmplepd",   "psllw",
    "pslld",      "psllq",     "psraw",     "psrad",     "psrlw",
    "psrld",      "psrlq",     "punpcklbw", "punpcklwd", "punpckldq",
    "punpcklqdq", "punpckhbw", "punpckhwd", "punpckhdq", "punpckhqdq",
    "pshufd",     "shufps",    "pshuflw",   "pshufhw",   "psrldq",
    "pslldq",     "packuswb",  "packssdw",  "packsswb",  "packusdw",
    "pmaddubsw",  "pmaddwd",   "pmulhw",    "pmulhuw",   "pmulhrsw",
    "pshufb"};

VPRIV const char *x64_x86_code_mnemonic(uint32_t code)
{
    if (code & X64_X86_TABLE)
        return x64_instructions[code & 0xfff].mnemonic;
    return x64_x86_operators[code & 0xfff];
}

/* the code of mnemonic, or false */
static bool x64_x86_operator_find(const char *mnemonic, uint32_t *code)
{
    for (unsigned i = 0;
         i < sizeof x64_x86_operators / sizeof *x64_x86_operators; i++)
        if (!strcmp(mnemonic, x64_x86_operators[i])) {
            *code = i;
            return true;
        }
    return false;
}

/* every lane of a vector of type vt shifted by the constant count */
VPRIV bool x64_x86_shift_code(codegen_context *e, int op, ast_qualified_type vt,
                              int64_t count, uint32_t *code)
{
    x64_simd_plan p =
        x64_simd_plan_for(e, op, vt, true, false, false,
                          (unsigned)codegen_type_size(e->compile_context, vt));

    if (!p.op || count < 0 || count > 255 || !x64_x86_operator_find(p.op, code))
        return false;
    *code |= X64_X86_SHIFT | (uint32_t)count << 16;
    return true;
}

/* op $count of one vector */
static uint32_t x64_x86_by_count(const char *mnemonic, int count)
{
    uint32_t code = 0;

    x64_x86_operator_find(mnemonic, &code);
    return code | X64_X86_SHIFT | (uint32_t)count << 16;
}

/* the instructions of a planned shuffle */
VPRIV bool x64_x86_shuffle_code(codegen_context *e, const int *bytes,
                                x64_x86_shuffle *out)
{
    if (x64_target_has_no_simd(e))
        return false;
    x64_shuffle_plan p = x64_shuffle_plan_for(bytes);
    const char *mnemonic = NULL;
    uint32_t extra = 0;

    *out = {};
    out->first = p.first;
    out->second = -1;
    switch (p.kind) {
    case X64_SHUFFLE_UNPACK:
        mnemonic = x64_simd_by_width((unsigned)p.width, "punpcklbw",
                                     "punpcklwd", "punpckldq", "punpcklqdq");
        if (p.high)
            mnemonic =
                x64_simd_by_width((unsigned)p.width, "punpckhbw", "punpckhwd",
                                  "punpckhdq", "punpckhqdq");
        out->second = p.second;
        break;
    case X64_SHUFFLE_PAIRS:
        mnemonic = "shufps";
        out->second = p.second;
        extra = X64_X86_IMMEDIATE;
        break;
    case X64_SHUFFLE_DWORDS:
        mnemonic = "pshufd";
        extra = X64_X86_IMMEDIATE | X64_X86_UNARY;
        break;
    case X64_SHUFFLE_LOW:
        mnemonic = "pshuflw";
        extra = X64_X86_IMMEDIATE | X64_X86_UNARY;
        break;
    case X64_SHUFFLE_HIGH:
        mnemonic = "pshufhw";
        extra = X64_X86_IMMEDIATE | X64_X86_UNARY;
        break;
    case X64_SHUFFLE_ROTATE:
        mnemonic = "por";
        out->second = p.second;
        out->steps = 1;
        out->before[0][0] = x64_x86_by_count("psrldq", p.width);
        out->before[1][0] = x64_x86_by_count("pslldq", 16 - p.width);
        break;
    case X64_SHUFFLE_EVEN:
        /* as x64_simd_even_prepare, then the saturating pack */
        mnemonic = "packssdw";
        if (p.width == 1)
            mnemonic = "packuswb";
        out->second = p.second;
        out->steps = 2;
        for (int i = 0; i < 2; i++) {
            out->before[i][0] = x64_x86_by_count("pslld", 16);
            out->before[i][1] = x64_x86_by_count("psrad", 16);
            if (p.width == 1) {
                out->before[i][0] = x64_x86_by_count("psllw", 8);
                out->before[i][1] = x64_x86_by_count("psrlw", 8);
            }
        }
        break;
    case X64_SHUFFLE_NONE:
        return false;
    }
    if (!mnemonic || !x64_x86_operator_find(mnemonic, &out->code))
        return false;
    out->code |= extra;
    if (extra & X64_X86_IMMEDIATE)
        out->code |= (uint32_t)(p.control & 0xff) << 16;
    return true;
} /* the code of a binary operator on two vectors of type vt that is one
   instruction, its operands swapped when *swapped */
VPRIV bool x64_x86_operation_code(codegen_context *e, int op,
                                  ast_qualified_type vt, uint32_t *code,
                                  bool *swapped)
{
    cc *c = e->compile_context;
    x64_simd_plan p =
        x64_simd_plan_for(e, op, vt, true, true, false,
                          (unsigned)codegen_type_size(e->compile_context, vt));
    bool dwords = op == LEX_TOKEN_STAR &&
                  codegen_type_size(c, T(c, vt)->base) == 4 &&
                  !codegen_type_is_float(c, T(c, vt)->base);

    if (!p.op || p.invert || p.bias || dwords || op == LEX_TOKEN_LESS_LESS ||
        op == LEX_TOKEN_GREATER_GREATER)
        return false;
    if (!x64_x86_operator_find(p.op, code))
        return false;
    *swapped = p.swap;
    return true;
}

/* the code of a builtin of the instruction table that takes vectors (and a
   constant) to a vector; the target must have its feature */
VPRIV bool x64_x86_builtin_code(codegen_context *e, uint32_t n, uint32_t *code,
                                unsigned *operands)
{
    cc *c = e->compile_context;
    const node *x = N(n);
    if (x->aux != CC_BUILTIN_X86_INSTRUCTION)
        return false;
    const uint32_t *arg = &c->lists[x->b];
    unsigned count = x->c - 1;
    unsigned row = (unsigned)c->ipool[N(arg[count])->a];
    const x64_instruction *in = &x64_instructions[row];
    uint32_t fn = e->walk_state->inline_call
                      ? e->walk_state->inline_call->function
                      : e->walk_state->function_symbol;
    int feature = x86_feature_find(in->feature);

    if (feature < 0 ||
        !x86_feature_has(cc_x86_features_get(c, fn), (unsigned)feature))
        return false; /* (the direct emitter reports it) */
    *code = row | X64_X86_TABLE;
    *operands = count;
    switch (in->form) {
    case BINARY:
        return true;
    case UNARY:
        *code |= X64_X86_UNARY;
        return true;
    case BINARY_CONTROL:
    case UNARY_CONTROL: {
        cc_value value;
        if (!cc_eval(c, arg[count - 1], &value) ||
            value.kind != CC_VALUE_INTEGER)
            return false;
        *code |= X64_X86_IMMEDIATE | (uint32_t)(value.i & 0xff) << 16;
        if (in->form == UNARY_CONTROL)
            *code |= X64_X86_UNARY;
        *operands = count - 1;
        return true;
    }
    case BINARY_XMM0:
    case SHIFT:
    case LOAD:
    case TO_GENERAL:
    case COMPARE:
    case COMPARE_SWAPPED:
    case PTEST:
        break;
    }
    return false;
}

/* the AVX2 builtins that work on each 128-bit half alone: the instruction of
   a half, and whether the second operand is a shift count */
static const struct x64_halved_builtin {
    const char *name, *mnemonic;
    bool count;
} x64_halved_builtins[] = {
    {"ia32_psllwi256", "psllw", true},
    {"ia32_pslldi256", "pslld", true},
    {"ia32_psllqi256", "psllq", true},
    {"ia32_psrlwi256", "psrlw", true},
    {"ia32_psrldi256", "psrld", true},
    {"ia32_psrlqi256", "psrlq", true},
    {"ia32_psrawi256", "psraw", true},
    {"ia32_psradi256", "psrad", true},
    {"ia32_packsswb256", "packsswb", false},
    {"ia32_packssdw256", "packssdw", false},
    {"ia32_packuswb256", "packuswb", false},
    {"ia32_packusdw256", "packusdw", false},
    {"ia32_pmaddubsw256", "pmaddubsw", false},
    {"ia32_pmaddwd256", "pmaddwd", false},
    {"ia32_pmulhw256", "pmulhw", false},
    {"ia32_pmulhuw256", "pmulhuw", false},
    {"ia32_pmulhrsw256", "pmulhrsw", false},
    {"ia32_pshufb256", "pshufb", false},
};

/* the code of the half's instruction of such a builtin, node n; the target
   must have AVX2 */
VPRIV bool x64_x86_halved_code(codegen_context *e, uint32_t n, uint32_t *code,
                               bool *count)
{
    cc *c = e->compile_context;
    const node *x = N(n);
    if (x->aux != CC_BUILTIN_X86_STENCIL || x->c != 3)
        return false;
    unsigned row = (unsigned)c->ipool[N(c->lists[x->b + 2])->a];
    const char *name = x64_stencil_name(row);
    uint32_t fn = e->walk_state->function_symbol;
    if (e->walk_state->inline_call)
        fn = e->walk_state->inline_call->function;
    if (!x86_feature_has(cc_x86_features_get(c, fn), X86_FEATURE_AVX2))
        return false; /* (the direct emitter reports it) */
    for (const x64_halved_builtin &h : x64_halved_builtins)
        if (!strcmp(name, h.name)) {
            *count = h.count;
            return x64_x86_operator_find(h.mnemonic, code);
        }
    return false;
}
