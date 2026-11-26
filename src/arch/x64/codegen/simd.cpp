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
    uint16_t groups;
    uint32_t first; /* in x64_stencil_groups */
} x64_stencil;

/* a builtin's variants of one length: one code, and what differs */
typedef struct x64_stencil_group {
    uint32_t code;    /* in x64_stencil_bytes: constants, then code */
    uint32_t columns; /* ... : `stored` bytes per variant */
    uint32_t fixups;  /* in x64_stencil_fixups: stored, then patched */
    uint32_t keys;    /* in x64_stencil_keys */
    uint16_t length;
    uint16_t constants;
    uint16_t stored;
    uint16_t patched;
    uint16_t ranges;
} x64_stencil_group;

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

/* rows, groups, keys, fixups, bytes */
static constexpr uint32_t x64_stencil_counts[] = {
#define X86_STENCIL_LAYOUT
#include "x86-stencils.inc"
#undef X86_STENCIL_LAYOUT
};

static const char x64_stencil_packed[] =
#define X86_STENCIL_PACKED
#include "x86-stencils.inc"
#undef X86_STENCIL_PACKED
    ;

static_assert(sizeof(x64_stencil) == 8 && sizeof(x64_stencil_group) == 28 &&
                  sizeof(x64_stencil_key) == 8,
              "the layout the generator packs");

static constexpr size_t x64_stencil_size =
    x64_stencil_counts[0] * sizeof(x64_stencil) +
    x64_stencil_counts[1] * sizeof(x64_stencil_group) +
    x64_stencil_counts[2] * sizeof(x64_stencil_key) +
    x64_stencil_counts[3] * sizeof(uint32_t) + x64_stencil_counts[4];

/* the tables, unpacked at the first stencil a process emits */
typedef struct x64_stencil_tables {
    const x64_stencil *rows;
    const x64_stencil_group *groups;
    const x64_stencil_key *keys;
    const uint32_t *fixups;
    const uint8_t *bytes;
} x64_stencil_tables;

/* the next byte of the base64 text */
static uint8_t x64_stencil_byte(const char **text, uint32_t *bits, int *count)
{
    while (*count < 8) {
        char ch = *(*text)++;
        uint32_t value = 63; /* '/' */
        if (ch >= 'A' && ch <= 'Z')
            value = (uint32_t)(ch - 'A');
        else if (ch >= 'a' && ch <= 'z')
            value = (uint32_t)(ch - 'a') + 26;
        else if (ch >= '0' && ch <= '9')
            value = (uint32_t)(ch - '0') + 52;
        else if (ch == '+')
            value = 62;
        *bits = *bits << 6 | value;
        *count += 6;
    }
    *count -= 8;
    return (uint8_t)(*bits >> *count);
}

static x64_stencil_tables x64_stencil_unpacked;

static void x64_stencil_unpack(void)
{
    alignas(4) static uint8_t data[x64_stencil_size];
    const char *text = x64_stencil_packed;
    uint32_t bits = 0;
    int count = 0;

    for (size_t at = 0; at < sizeof data;) {
        unsigned token = x64_stencil_byte(&text, &bits, &count);
        if (token < 128) {
            for (unsigned i = 0; i <= token; i++)
                data[at++] = x64_stencil_byte(&text, &bits, &count);
            continue;
        }
        size_t distance = x64_stencil_byte(&text, &bits, &count);
        distance |= (size_t)x64_stencil_byte(&text, &bits, &count) << 8;
        for (unsigned i = 0; i < token - 128 + 4; i++, at++)
            data[at] = data[at - distance];
    }
    x64_stencil_tables &t = x64_stencil_unpacked;
    const uint8_t *p = data;
    t.rows = (const x64_stencil *)(const void *)p;
    p += x64_stencil_counts[0] * sizeof(x64_stencil);
    t.groups = (const x64_stencil_group *)(const void *)p;
    p += x64_stencil_counts[1] * sizeof(x64_stencil_group);
    t.keys = (const x64_stencil_key *)(const void *)p;
    p += x64_stencil_counts[2] * sizeof(x64_stencil_key);
    t.fixups = (const uint32_t *)(const void *)p;
    t.bytes = p + x64_stencil_counts[3] * sizeof(uint32_t);
}

static const x64_stencil_tables &x64_stencil_data(void)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;

    pthread_once(&once, x64_stencil_unpack);
    return x64_stencil_unpacked;
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
    return name;
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

/* the group with the variant for `key` and its place in it, or NULL */
static const x64_stencil_group *
x64_stencil_find(const x64_stencil *s, uint32_t key, unsigned *variant)
{
    for (unsigned i = 0; i < s->groups; i++) {
        const x64_stencil_group *g = &x64_stencil_data().groups[s->first + i];
        unsigned before = 0;

        for (unsigned k = 0; k < g->ranges; k++) {
            const x64_stencil_key *range =
                &x64_stencil_data().keys[g->keys + k];

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
    unsigned size = (unsigned)g->length + g->constants;
    uint8_t *code = (uint8_t *)malloc(size);
    if (!code)
        vout_of_memory();
    const uint8_t *bytes = x64_stencil_data().bytes;
    memcpy(code, bytes + g->code, size);
    const uint32_t *fixup = &x64_stencil_data().fixups[g->fixups];
    const uint8_t *own = bytes + g->columns + (size_t)variant * g->stored;
    for (unsigned i = 0; i < g->stored; i++)
        code[fixup[i]] = own[i];
    for (unsigned i = g->stored; i < (unsigned)g->stored + g->patched; i++) {
        unsigned constant = (fixup[i] >> 24) - 1;
        code[fixup[i] & 0xffffff] = (uint8_t)(key >> (8 * constant));
    }
    if (g->constants) {
        x64_simd_text(e, "\tjmp\t99f\n\t.p2align\t6\n");
        x64_stencil_text(e, code, g->constants);
        x64_simd_text(e, "99:\n");
    }
    x64_stencil_text(e, code + g->constants, g->length);
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
    const x64_stencil *s = &x64_stencil_data().rows[row];
    uint32_t fn = e->walk_state->inline_call
                      ? e->walk_state->inline_call->function
                      : e->walk_state->function_symbol;
    const char *features = x64_stencil_features_names[s->features];

    x64_nofp(e);
    if (!x64_stencil_features(e, features, cc_x86_features_get(c, fn)))
        codegen_fatal_report(e, x->loc, "'%s' needs target feature %s",
                             x64_stencil_name(row),
                             x64_stencil_features_names[s->features]);

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
            codegen_fatal_report(e, x->loc,
                                 "argument to '%s' must be a constant integer",
                                 x64_stencil_name(row));
        key |= (uint32_t)(value.i & 0xff) << (8 * constants++);
    }
    unsigned variant = 0;
    const x64_stencil_group *found = x64_stencil_find(s, key, &variant);
    if (!found)
        codegen_fatal_report(e, x->loc,
                             "constant argument to '%s' is out of range",
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
