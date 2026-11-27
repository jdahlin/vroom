/* The simplifier's rewrite rules: what one instruction becomes, given its operands. */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
#include "common/literal.h"
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "simplify.h"

VPRIV void virbuild_use_count_increment(virbuild_context *V,
                                        virbuild_simplify_state *S, uint32_t v)
{
    if (v < S->constant_start && V->insns[v].block != VIRBUILD_BLOCK_NONE)
        S->use_counts[v]++;
}

VPRIV void virbuild_use_count_decrement(virbuild_context *V,
                                        virbuild_simplify_state *S, uint32_t v)
{
    if (v < S->constant_start && V->insns[v].block != VIRBUILD_BLOCK_NONE &&
        S->use_counts[v])
        S->use_counts[v]--;
}

VPRIV bool virbuild_value_integer_constant(const virbuild_context *V,
                                           uint32_t v, int64_t *k)
{
    const vir_insn *in = &V->insns[v].insn;

    if (in->op != VIR_OP_ICONST)
        return false;
    *k = in->type == VIR_TYPE_I32
             ? (int64_t)(int32_t)in->b
             : (int64_t)((uint64_t)in->b | (uint64_t)in->c << 32);
    return true;
}

/* v is 0 or 1: a comparison, those constants, or what only chooses between
 * or combines such values (depth bounds the search, round a loop too) */
VPRIV bool virbuild_value_is_boolean(const virbuild_context *V, uint32_t v,
                                     unsigned depth)
{
    uint32_t at = v;

    while (V->forward[at] != at)
        at = V->forward[at];
    const virbuild_preir_insn *x = &V->insns[at];
    const vir_insn *in = &x->insn;
    int64_t k;

    if (vir_type_is_vector(in->type))
        return false;
    if (in->op == VIR_OP_ICMP || in->op == VIR_OP_FCMP)
        return true;
    if (virbuild_value_integer_constant(V, at, &k))
        return k == 0 || k == 1;
    if (!depth--)
        return false;
    switch (in->op) {
    case VIR_OP_SELECT:
        return virbuild_value_is_boolean(V, in->b, depth) &&
               virbuild_value_is_boolean(V, in->c, depth);
    case VIR_OP_AND:
        return virbuild_value_is_boolean(V, in->a, depth) ||
               virbuild_value_is_boolean(V, in->b, depth);
    case VIR_OP_OR:
    case VIR_OP_XOR:
        return virbuild_value_is_boolean(V, in->a, depth) &&
               virbuild_value_is_boolean(V, in->b, depth);
    case VIR_OP_EXT:
    case VIR_OP_TRUNC:
        return virbuild_value_is_boolean(V, in->a, depth);
    case VIR_OP_BPARAM:
        if (in->b == VIRBUILD_BLOCK_NONE || x->block == VIRBUILD_BLOCK_NONE ||
            !V->blocks[x->block].predecessor_count)
            return false;
        for (uint32_t j = 0; j < V->blocks[x->block].predecessor_count; j++)
            if (!virbuild_value_is_boolean(V, V->extra[in->b + j], depth))
                return false;
        return true;
    default:
        return false;
    }
}

/* bytes v is zero-extended from (its type's size: nothing known) */
static uint32_t virbuild_value_zero_extended_bytes(const virbuild_context *V,
                                                   uint32_t v)
{
    const vir_insn *in = &V->insns[v].insn;
    uint32_t sz = vir_type_size(in->type);
    int64_t k;

    switch (in->op) {
    case VIR_OP_LOAD:
        if (vir_type_is_vector(in->type))
            return sz;
        return (in->flags & VIR_INSN_FLAG_MS) ? sz
               : 1u << VIR_INSN_FLAG_WIDTH_LOG2(in->flags) < sz
                   ? 1u << VIR_INSN_FLAG_WIDTH_LOG2(in->flags)
                   : sz;
    case VIR_OP_EXT:
        return (in->flags & VIR_INSN_FLAG_MS)
                   ? sz
                   : 1u << VIR_INSN_FLAG_WIDTH_LOG2(in->flags);
    case VIR_OP_ICMP:
    case VIR_OP_FCMP:
        return 1;
    case VIR_OP_SELECT:
    case VIR_OP_BPARAM:
    case VIR_OP_TRUNC:
        return virbuild_value_is_boolean(V, v, 4) ? 1 : sz;
    case VIR_OP_AND:
        if (virbuild_value_integer_constant(V, in->b, &k) && k >= 0)
            return k <= 0xff ? 1 : k <= 0xffff ? 2 : k <= 0xffffffffll ? 4 : sz;
        return sz;
    default:
        return sz;
    }
}

/* bytes v is sign-extended from */
static uint32_t virbuild_value_sign_extended_bytes(const virbuild_context *V,
                                                   uint32_t v)
{
    const vir_insn *in = &V->insns[v].insn;
    uint32_t sz = vir_type_size(in->type), z;

    if ((in->op == VIR_OP_LOAD || in->op == VIR_OP_EXT) &&
        (in->flags & VIR_INSN_FLAG_MS))
        return 1u << VIR_INSN_FLAG_WIDTH_LOG2(in->flags) < sz
                   ? 1u << VIR_INSN_FLAG_WIDTH_LOG2(in->flags)
                   : sz;
    z = virbuild_value_zero_extended_bytes(V, v);
    return z < sz
               ? 2 * z
               : sz; /* (zero-extended from z bytes: a signed 2z-byte value) */
}

VPRIV uint32_t virbuild_simplify_undefined_value(codegen_context *e,
                                                 virbuild_simplify_state *S,
                                                 uint32_t ty)
{
    if (!S->undefined_values[ty])
        S->undefined_values[ty] = virbuild_undefined_value(e, ty);
    return S->undefined_values[ty];
}

/* q rewritten in place (true), or the value that replaces it in *r */
/* v's instruction when it is the x86 operator named, in a block; else NULL */
static const vir_insn *virbuild_x86_operator(virbuild_context *V, uint32_t v,
                                             const char *named)
{
    const virbuild_preir_insn *x = &V->insns[virbuild_value_resolve(V, v)];

    if (x->insn.op != VIR_OP_X86 || x->block == VIRBUILD_BLOCK_NONE ||
        !x64_x86_operator_is(x->insn.c, named))
        return NULL;
    return &x->insn;
}

/*
 * y ^ ((x ^ y) & (a > b)), words, {a, b} = {x, y}: the front end's elementwise max or min
 * (__builtin_elementwise_max: _mm_max_epi16), one instruction on x86-64.
 */
static bool virbuild_minmax_simplify(virbuild_context *V,
                                     virbuild_simplify_state *S, uint32_t q)
{
    vir_insn in = V->insns[q].insn;

    if (!x64_x86_operator_is(in.c, "pxor"))
        return false;
    uint32_t y = virbuild_value_resolve(V, in.a);
    const vir_insn *chosen = virbuild_x86_operator(V, in.b, "pand");

    if (!chosen)
        return false;
    const vir_insn *differ = virbuild_x86_operator(V, chosen->a, "pxor");
    const vir_insn *greater = virbuild_x86_operator(V, chosen->b, "pcmpgtw");

    if (!differ || !greater)
        return false;
    uint32_t x = virbuild_value_resolve(V, differ->a);
    uint32_t other = virbuild_value_resolve(V, differ->b);

    if (x == y)
        x = other;
    else if (other != y)
        return false;
    uint32_t a = virbuild_value_resolve(V, greater->a);
    uint32_t b = virbuild_value_resolve(V, greater->b);
    const char *mnemonic;

    if (a == x && b == y)
        mnemonic = "pmaxsw";
    else if (a == y && b == x)
        mnemonic = "pminsw";
    else
        return false;
    virbuild_use_count_decrement(V, S, in.a),
        virbuild_use_count_decrement(V, S, in.b);
    virbuild_use_count_increment(V, S, x),
        virbuild_use_count_increment(V, S, y);
    V->insns[q].insn.a = x;
    V->insns[q].insn.b = y;
    V->insns[q].insn.c = x64_x86_operator_code(mnemonic);
    return true;
}

/* icmp's condition cc (eq ne slt sle sgt sge ult ule ugt uge) of two constants of type ty */
VPRIV bool virbuild_compare_constants(uint32_t cc, uint32_t ty, int64_t a,
                                      int64_t b)
{
    uint64_t x = (uint64_t)a, y = (uint64_t)b;

    if (ty == VIR_TYPE_I32)
        x = (uint32_t)x, y = (uint32_t)y;
    switch (cc) {
    case 0:
        return x == y;
    case 1:
        return x != y;
    case 2:
        return a < b;
    case 3:
        return a <= b;
    case 4:
        return a > b;
    case 5:
        return a >= b;
    case 6:
        return x < y;
    case 7:
        return x <= y;
    case 8:
        return x > y;
    default:
        return x >= y;
    }
}

VPRIV bool virbuild_insn_simplify(codegen_context *e,
                                  virbuild_simplify_state *S, uint32_t q,
                                  uint32_t *r)
{
    virbuild_context *V = e->vir_builder;
    vir_insn in = V->insns[q].insn;
    int64_t k, k2;

    *r = q;
    if (virbuild_opcode_is_commutative(in.op) &&
        virbuild_value_integer_constant(V, in.a, &k) &&
        !virbuild_value_integer_constant(V, in.b, &k2)) {
        V->insns[q].insn.a = in.b,
        V->insns[q].insn.b = in.a; /* the constant second (an immediate) */
        in = V->insns[q].insn;
    }
    /* what became constant only here (an inlined argument): the walker folded the rest */
    if ((in.op == VIR_OP_ADD || in.op == VIR_OP_SUB || in.op == VIR_OP_AND ||
         in.op == VIR_OP_OR || in.op == VIR_OP_XOR || in.op == VIR_OP_SHL ||
         in.op == VIR_OP_MUL) &&
        (in.type == VIR_TYPE_I32 || in.type == VIR_TYPE_I64) &&
        virbuild_value_integer_constant(V, in.a, &k) &&
        virbuild_value_integer_constant(V, in.b, &k2)) {
        uint64_t x = (uint64_t)k, y = (uint64_t)k2, v;

        if (in.op == VIR_OP_ADD)
            v = x + y;
        else if (in.op == VIR_OP_SUB)
            v = x - y;
        else if (in.op == VIR_OP_AND)
            v = x & y;
        else if (in.op == VIR_OP_OR)
            v = x | y;
        else if (in.op == VIR_OP_MUL)
            v = x * y;
        else if (in.op == VIR_OP_SHL)
            v = x << (y & (8 * vir_type_size(in.type) - 1));
        else
            v = x ^ y;
        if (in.type == VIR_TYPE_I32)
            v = (uint32_t)v;
        *r = virbuild_constant_integer(e, in.type, (int64_t)v);
        return false;
    }
    switch (in.op) {
    case VIR_OP_I2P:
    case VIR_OP_P2I: /* a pointer and its address as an integer are the same bits */
        if (in.op == VIR_OP_I2P && V->insns[in.a].insn.type == VIR_TYPE_PTR) {
            *r = in.a;
            return false;
        }
        if (V->insns[in.a].insn.op ==
            (in.op == VIR_OP_I2P ? VIR_OP_P2I : VIR_OP_I2P))
            *r = V->insns[in.a].insn.a;
        return false;
    case VIR_OP_STORE: { /* only the low bytes go to memory: not their extension, nor a truncation to more */
        vir_insn xi = V->insns[in.b].insn;

        if (virbuild_promote_store_is_dead(V, q))
            return false; /* (it goes: its operands are not counted) */
        if (V->insns[in.b].block != VIRBUILD_BLOCK_NONE &&
            ((xi.op == VIR_OP_EXT && VIR_INSN_FLAG_WIDTH_LOG2(xi.flags) >=
                                         VIR_INSN_FLAG_WIDTH_LOG2(in.flags)) ||
             (xi.op == VIR_OP_TRUNC &&
              VIR_INSN_FLAG_WIDTH_LOG2(in.flags) <= 2))) {
            virbuild_use_count_decrement(V, S, in.b),
                virbuild_use_count_increment(V, S, xi.a);
            V->insns[q].insn.b = xi.a;
            return true;
        }
        return false;
    }
    case VIR_OP_ICMP:
        if (virbuild_value_integer_constant(V, in.a, &k) &&
            virbuild_value_integer_constant(V, in.b, &k2)) {
            *r = virbuild_constant_integer(
                e, VIR_TYPE_I32,
                virbuild_compare_constants(in.flags & 15,
                                           V->insns[in.a].insn.type, k, k2));
            return false;
        }
        /* A value that is 0 or 1 is its own comparison with zero. This also
		 * preserves unordered floating comparison results verbatim. */
        if ((in.flags & 15) == 1 /* ne */ &&
            virbuild_value_integer_constant(V, in.b, &k) && !k &&
            V->insns[in.a].insn.type == VIR_TYPE_I32 &&
            virbuild_value_is_boolean(V, in.a, 4))
            *r = in.a;
        return false;
    case VIR_OP_SELECT: {
        /* c ? 1 : 0 and c ? 0 : 1 are comparisons of c with zero (c itself
		 * when it is 0 or 1 already) */
        if (in.type != VIR_TYPE_I32 ||
            !virbuild_value_integer_constant(V, in.b, &k) ||
            !virbuild_value_integer_constant(V, in.c, &k2) || (k | k2) != 1 ||
            (k & k2) != 0 || V->insns[in.a].insn.type != VIR_TYPE_I32)
            return false;
        if (k && virbuild_value_is_boolean(V, in.a, 4)) {
            *r = in.a;
            return false;
        }
        uint32_t zero = virbuild_constant_integer(e, VIR_TYPE_I32, 0);

        virbuild_use_count_decrement(V, S, in.b),
            virbuild_use_count_decrement(V, S, in.c),
            virbuild_use_count_increment(V, S, zero);
        V->insns[q].insn = VCC_LITERAL(vir_insn, VIR_OP_ICMP, VIR_TYPE_I32,
                                       (uint16_t)(k != 0), in.a, zero, 0);
        return true;
    }
    case VIR_OP_X86: {
        if (virbuild_minmax_simplify(V, S, q))
            return true;
        /* pand with all ones: the other operand (a shift by a constant ANDs
		 * its mask of the counts in range) */
        if (x64_x86_operator_is(in.c, "pand")) {
            for (int side = 0; side < 2; side++) {
                uint32_t mask = virbuild_value_resolve(V, side ? in.a : in.b);
                uint32_t kept = virbuild_value_resolve(V, side ? in.b : in.a);
                const vir_insn *ones = &V->insns[mask].insn;

                if (ones->op == VIR_OP_VSPLAT &&
                    virbuild_value_integer_constant(
                        V, virbuild_value_resolve(V, ones->a), &k) &&
                    k == -1 && V->insns[kept].insn.type == in.type) {
                    *r = kept;
                    return false;
                }
            }
        }
        /* a shift by a constant in a vector: by an immediate */
        vir_insn count = V->insns[in.b].insn;
        uint32_t code = 0;

        if (count.op != VIR_OP_VINSERT || count.c ||
            !virbuild_value_integer_constant(
                V, virbuild_value_resolve(V, count.b), &k))
            return false;
        vir_insn rest = V->insns[virbuild_value_resolve(V, count.a)].insn;
        if (rest.op != VIR_OP_VSPLAT ||
            !virbuild_value_integer_constant(
                V, virbuild_value_resolve(V, rest.a), &k2) ||
            k2 || !x64_x86_shift_immediate_code(in.c, (uint32_t)k, &code))
            return false;
        virbuild_use_count_decrement(V, S, in.b),
            virbuild_use_count_increment(V, S, in.a);
        V->insns[q].insn.b = in.a;
        V->insns[q].insn.c = code;
        return true;
    }
    case VIR_OP_EXT: {
        uint32_t x = in.a, K = 1u << VIR_INSN_FLAG_WIDTH_LOG2(in.flags),
                 ts = vir_type_size(in.type);
        bool s = in.flags & VIR_INSN_FLAG_MS;
        vir_insn xi = V->insns[x].insn;
        uint32_t xs = vir_type_size(xi.type);

        if (virbuild_value_integer_constant(V, x, &k)) {
            uint64_t u = (uint64_t)k & (K == 8 ? ~0ull : (1ull << 8 * K) - 1);

            if (s && K < 8 && (u >> (8 * K - 1)) & 1)
                u |= ~0ull << 8 * K;
            *r = virbuild_constant_integer(e, in.type, (int64_t)u);
            return false;
        }
        if (K >= ts) { /* (the whole value: a copy or a plain widening) */
            if (xs == ts) {
                *r = x;
                return false;
            }
        }
        if (xs == ts && (s ? virbuild_value_sign_extended_bytes(V, x) <= K
                           : virbuild_value_zero_extended_bytes(V, x) <= K)) {
            *r = x;
            return false;
        }
        if (xs == ts && s && virbuild_value_zero_extended_bytes(V, x) < K) {
            *r = x;
            return false;
        }
        if (xi.op == VIR_OP_EXT &&
            vir_type_size(V->insns[xi.a].insn.type) <= ts) {
            uint32_t M = 1u << VIR_INSN_FLAG_WIDTH_LOG2(xi.flags);

            if (K <= M) { /* the low K bytes of x are y's */
                virbuild_use_count_decrement(V, S, x),
                    virbuild_use_count_increment(V, S, xi.a);
                V->insns[q].insn.a = xi.a;
                return true;
            }
            if (s == !!(xi.flags & VIR_INSN_FLAG_MS) ||
                !(xi.flags & VIR_INSN_FLAG_MS)) { /* x's extension is q's */
                virbuild_use_count_decrement(V, S, x),
                    virbuild_use_count_increment(V, S, xi.a);
                V->insns[q].insn.a = xi.a;
                V->insns[q].insn.flags = xi.flags;
                return true;
            }
        }
        /* a load only q reads: the load extends */
        if (xi.op == VIR_OP_LOAD && !(xi.flags & VIR_INSN_FLAG_VOL) &&
            S->use_counts[x] == 1 &&
            (1u << VIR_INSN_FLAG_WIDTH_LOG2(xi.flags)) == K) {
            V->insns[x].insn.type = (uint8_t)in.type;
            V->insns[x].insn.flags = (uint16_t)((xi.flags & ~VIR_INSN_FLAG_MS) |
                                                (s ? VIR_INSN_FLAG_MS : 0));
            *r = x;
            return false;
        }
        if (xi.op == VIR_OP_LOAD && !(xi.flags & VIR_INSN_FLAG_VOL) &&
            S->use_counts[x] == 1 &&
            (1u << VIR_INSN_FLAG_WIDTH_LOG2(xi.flags)) < K &&
            (s ? virbuild_value_sign_extended_bytes(V, x) <= K
               : virbuild_value_zero_extended_bytes(V, x) <= K)) {
            V->insns[x].insn.type = (uint8_t)in.type;
            *r = x;
            return false;
        }
        return false;
    }
    case VIR_OP_TRUNC: {
        uint32_t x = in.a;
        vir_insn xi = V->insns[x].insn;

        if (virbuild_value_integer_constant(V, x, &k)) {
            *r = virbuild_constant_integer(e, VIR_TYPE_I32,
                                           (int64_t)(uint32_t)k);
            return false;
        }
        if (xi.op == VIR_OP_EXT && V->insns[xi.a].insn.type == VIR_TYPE_I32 &&
            VIR_INSN_FLAG_WIDTH_LOG2(xi.flags) >= 2) {
            *r = xi.a;
            return false;
        }
        if (xi.op == VIR_OP_EXT &&
            V->insns[xi.a].insn.type ==
                VIR_TYPE_I32) { /* a narrower extension, in i32 */
            virbuild_use_count_decrement(V, S, x),
                virbuild_use_count_increment(V, S, xi.a);
            V->insns[q].insn = VCC_LITERAL(vir_insn, VIR_OP_EXT, VIR_TYPE_I32,
                                           xi.flags, xi.a, 0, 0);
            return true;
        }
        if (xi.op == VIR_OP_EXT &&
            VIR_INSN_FLAG_WIDTH_LOG2(xi.flags) >=
                2) { /* trunc of a widening of the low half */
            virbuild_use_count_decrement(V, S, x),
                virbuild_use_count_increment(V, S, xi.a);
            V->insns[q].insn.a = xi.a;
            return true;
        }
        if (xi.op == VIR_OP_LOAD && !(xi.flags & VIR_INSN_FLAG_VOL) &&
            S->use_counts[x] == 1 && VIR_INSN_FLAG_WIDTH_LOG2(xi.flags) <= 2) {
            V->insns[x].insn.type = VIR_TYPE_I32;
            if (VIR_INSN_FLAG_WIDTH_LOG2(xi.flags) == 2)
                V->insns[x].insn.flags &= (uint16_t)~VIR_INSN_FLAG_MS;
            *r = x;
            return false;
        }
        return false;
    }
    case VIR_OP_S2F:
    case VIR_OP_U2F: /* a constant converted: the constant (to nearest, as the instruction) */
        if (virbuild_value_integer_constant(V, in.a, &k)) {
            bool w = V->insns[in.a].insn.type != VIR_TYPE_I32;
            double d = in.op == VIR_OP_S2F
                           ? (w ? (double)k : (double)(int32_t)k)
                           : (w ? (double)(uint64_t)k : (double)(uint32_t)k);
            float f = in.op == VIR_OP_S2F
                          ? (w ? (float)k : (float)(int32_t)k)
                          : (w ? (float)(uint64_t)k : (float)(uint32_t)k);
            uint64_t bits;
            uint32_t fb;

            memcpy(&bits, &d, 8), memcpy(&fb, &f, 4);
            *r = in.type == VIR_TYPE_F32
                     ? virbuild_constant_make(e, VIR_OP_FCONST, VIR_TYPE_F32, 0,
                                              fb, 0)
                     : virbuild_constant_make(e, VIR_OP_FCONST, VIR_TYPE_F64, 0,
                                              (uint32_t)bits,
                                              (uint32_t)(bits >> 32));
        }
        return false;
    case VIR_OP_FEXT:
    case VIR_OP_FTRUNC:
        if (V->insns[in.a].insn.op == VIR_OP_FCONST &&
            V->insns[in.a].block == VIRBUILD_BLOCK_NONE) {
            uint64_t bits = (uint64_t)V->insns[in.a].insn.b |
                            (uint64_t)V->insns[in.a].insn.c << 32;
            uint32_t fb = (uint32_t)bits;
            double d;
            float f;

            if (in.op == VIR_OP_FEXT) {
                memcpy(&f, &fb, 4);
                d = f;
                if (d != d)
                    return false; /* (a NaN's payload: left to the instruction) */
                memcpy(&bits, &d, 8);
                *r = virbuild_constant_make(e, VIR_OP_FCONST, VIR_TYPE_F64, 0,
                                            (uint32_t)bits,
                                            (uint32_t)(bits >> 32));
            } else {
                memcpy(&d, &bits, 8);
                f = (float)d;
                if (f != f)
                    return false;
                memcpy(&fb, &f, 4);
                *r = virbuild_constant_make(e, VIR_OP_FCONST, VIR_TYPE_F32, 0,
                                            fb, 0);
            }
        }
        return false;
    case VIR_OP_ADD:
    case VIR_OP_SUB:
    case VIR_OP_OR:
    case VIR_OP_XOR:
    case VIR_OP_SHL:
    case VIR_OP_LSHR:
    case VIR_OP_ASHR:
        if (virbuild_value_integer_constant(V, in.b, &k) && !k)
            *r = in.a;
        return false;
    case VIR_OP_MUL:
        if (virbuild_value_integer_constant(V, in.b, &k) && k == 1)
            *r = in.a;
        else if (virbuild_value_integer_constant(V, in.b, &k) && !k)
            *r = in.b; /* (a 128-bit product's zero high halves) */
        return false;
    case VIR_OP_AND:
        if (virbuild_value_integer_constant(V, in.b, &k) &&
            (k == -1 ||
             (in.type == VIR_TYPE_I32 && (uint32_t)k == 0xffffffffu)))
            *r = in.a;
        else if (virbuild_value_integer_constant(V, in.b, &k) && k > 0 &&
                 !((uint64_t)k & ((uint64_t)k + 1)) &&
                 virbuild_value_zero_extended_bytes(V, in.a) < 8 &&
                 (uint64_t)k >=
                     (virbuild_value_zero_extended_bytes(V, in.a) == 8
                          ? ~0ull
                          : (1ull << 8 * virbuild_value_zero_extended_bytes(
                                             V, in.a)) -
                                1))
            *r = in.a; /* the mask keeps every bit that can be set */
        return false;
    case VIR_OP_PTRADD:
        if (!virbuild_value_integer_constant(V, in.b, &k))
            return false;
        if (!k) {
            *r = in.a;
            return false;
        }
        if (V->insns[in.a].insn.op == VIR_OP_SLOTADDR &&
            V->insns[in.a].block ==
                VIRBUILD_BLOCK_NONE) { /* $s+k2 + k: $s+(k2+k) */
            vir_insn a =
                V->insns[in.a]
                    .insn; /* (a copy: virbuild_constant_make can move V->p) */

            k2 = (int64_t)((uint64_t)a.b | (uint64_t)a.c << 32) + k;
            *r = virbuild_constant_make(e, VIR_OP_SLOTADDR, VIR_TYPE_PTR, a.a,
                                        (uint32_t)k2,
                                        (uint32_t)((uint64_t)k2 >> 32));
            return false;
        }
        if (V->insns[in.a].insn.op == VIR_OP_PTRADD &&
            V->insns[in.a].block != VIRBUILD_BLOCK_NONE &&
            virbuild_value_integer_constant(V, V->insns[in.a].insn.b, &k2)) {
            uint32_t y = V->insns[in.a].insn.a,
                     c = virbuild_constant_integer(
                         e, VIR_TYPE_I64,
                         (int64_t)((uint64_t)k + (uint64_t)k2));

            virbuild_use_count_decrement(V, S, in.a),
                virbuild_use_count_increment(V, S, y);
            V->insns[q].insn.a = y, V->insns[q].insn.b = c;
            return true;
        }
        return false;
    default:
        return false;
    }
}

VPRIV bool virbuild_opcode_is_commutative(uint32_t op)
{
    return op == VIR_OP_ADD || op == VIR_OP_MUL || op == VIR_OP_AND ||
           op == VIR_OP_OR || op == VIR_OP_XOR || op == VIR_OP_UMULHI ||
           op == VIR_OP_SMULHI;
}
