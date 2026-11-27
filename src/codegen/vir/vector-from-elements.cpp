/*
 * A vector read from a slot that stores of its elements have just filled (_mm_set1_epi16 and
 * the other set intrinsics: a vector initializer) is built from the values instead: vsplat when
 * its 32-bit words are one value, else vsplat and vinsert.  The element stores are then of a
 * slot nothing reads.
 */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
#include "common/literal.h"
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "simplify.h"

typedef struct virbuild_elements {
    uint32_t slot;      /* + 1; 0: none */
    uint32_t value[16]; /* at each byte where a store begins */
    uint8_t width[16];  /* ... its bytes; 0: none begins here */
    uint16_t covered;   /* a bit per byte stored */
} virbuild_elements;

/* a slot a memset has zeroed: the 8-byte half no store has touched holds 0 */
static void virbuild_elements_zero_half(codegen_context *e,
                                        virbuild_elements *E)
{
    for (uint32_t at = 0; at < 16; at += 8)
        if (!(E->covered & 0xffu << at)) {
            E->value[at] = virbuild_constant_integer(e, VIR_TYPE_I64, 0);
            E->width[at] = 8;
            E->covered = (uint16_t)(E->covered | 0xffu << at);
        }
}

/* the 32-bit word at byte `at` is made of the same values as the one at 0 */
static bool virbuild_elements_word_repeats(const virbuild_elements *E,
                                           uint32_t at)
{
    for (uint32_t i = 0; i < 4; i++)
        if (E->width[at + i] != E->width[i] ||
            (E->width[i] && E->value[at + i] != E->value[i]))
            return false;
    return true;
}

/* one value stored at every element */
static bool virbuild_elements_one_value(const virbuild_elements *E,
                                        uint32_t width)
{
    for (uint32_t i = 0; i < 16; i += width)
        if (E->width[i] != width || E->value[i] != E->value[0])
            return false;
    return true;
}

typedef struct virbuild_elements_at {
    codegen_context *context;
    uint32_t block;
    uint32_t *prev; /* new instructions go after it */
} virbuild_elements_at;

static uint32_t virbuild_elements_insn(const virbuild_elements_at *W,
                                       uint32_t op, uint32_t ty, uint32_t a,
                                       uint32_t b, uint32_t c)
{
    vir_insn in = VCC_LITERAL(vir_insn, (uint8_t)op, (uint8_t)ty, 0, a, b, c);

    return virbuild_preir_insn_insert(W->context, W->block, W->prev, in);
}

/* v as an i32 */
static uint32_t virbuild_elements_i32(const virbuild_elements_at *W, uint32_t v)
{
    if (W->context->vir_builder->insns[v].insn.type == VIR_TYPE_I32)
        return v;
    return virbuild_elements_insn(W, VIR_OP_TRUNC, VIR_TYPE_I32, v, 0, 0);
}

/* the 32-bit word at byte `at`, made of its 1-, 2- or 4-byte stores */
static uint32_t virbuild_elements_word(const virbuild_elements_at *W,
                                       const virbuild_elements *E, uint32_t at)
{
    codegen_context *e = W->context;
    uint32_t width = E->width[at];

    if (width == 4)
        return virbuild_elements_i32(W, E->value[at]);
    uint32_t word = 0;
    uint32_t mask = 0xffff;

    if (width == 1)
        mask = 0xff;
    for (uint32_t i = 0; i < 4; i += width) {
        uint32_t part = virbuild_elements_insn(
            W, VIR_OP_AND, VIR_TYPE_I32,
            virbuild_elements_i32(W, E->value[at + i]),
            virbuild_constant_integer(e, VIR_TYPE_I32, mask), 0);

        if (i)
            part = virbuild_elements_insn(
                W, VIR_OP_SHL, VIR_TYPE_I32, part,
                virbuild_constant_integer(e, VIR_TYPE_I32, 8 * i), 0);
        if (word)
            part = virbuild_elements_insn(W, VIR_OP_OR, VIR_TYPE_I32, word,
                                          part, 0);
        word = part;
    }
    return word;
}

/* the vector of type ty that E's stores make; 0 when they do not make one */
static uint32_t virbuild_elements_vector(const virbuild_elements_at *W,
                                         const virbuild_elements *E,
                                         uint32_t ty)
{
    codegen_context *e = W->context;
    virbuild_context *V = e->vir_builder;
    uint32_t built = 0;
    uint32_t made = 0;

    if (E->covered != 0xffff)
        return 0;
    if (E->width[0] == 8) {
        if (E->width[8] != 8 ||
            V->insns[E->value[0]].insn.type != VIR_TYPE_I64 ||
            V->insns[E->value[8]].insn.type != VIR_TYPE_I64)
            return 0;
        made = VIR_TYPE_I64X2;
        built =
            virbuild_elements_insn(W, VIR_OP_VSPLAT, made, E->value[0], 0, 0);
        if (E->value[8] != E->value[0])
            built = virbuild_elements_insn(W, VIR_OP_VINSERT, made, built,
                                           E->value[8], 1);
    } else if (virbuild_elements_one_value(E, 2)) {
        made = VIR_TYPE_I16X8;
        built =
            virbuild_elements_insn(W, VIR_OP_VSPLAT, made,
                                   virbuild_elements_i32(W, E->value[0]), 0, 0);
    } else {
        /* four words, each wholly of stores of one width */
        for (uint32_t at = 0; at < 16; at += 4) {
            uint32_t width = E->width[at];

            if (width != 1 && width != 2 && width != 4)
                return 0;
            for (uint32_t i = 0; i < 4; i += width) {
                if (E->width[at + i] != width)
                    return 0;
                uint32_t vt = V->insns[E->value[at + i]].insn.type;
                if (vt != VIR_TYPE_I32 && vt != VIR_TYPE_I64)
                    return 0;
            }
        }
        made = VIR_TYPE_I32X4;
        built = virbuild_elements_insn(W, VIR_OP_VSPLAT, made,
                                       virbuild_elements_word(W, E, 0), 0, 0);
        bool one = virbuild_elements_word_repeats(E, 4) &&
                   virbuild_elements_word_repeats(E, 8) &&
                   virbuild_elements_word_repeats(E, 12);
        for (uint32_t lane = 1; lane < 4 && !one; lane++)
            built = virbuild_elements_insn(
                W, VIR_OP_VINSERT, made, built,
                virbuild_elements_word(W, E, 4 * lane), lane);
    }
    if (made != ty)
        built = virbuild_elements_insn(W, VIR_OP_BITCAST, ty, built, 0, 0);
    return built;
}

/* the slot q's address operand is in, + 1, and its offset */
static uint32_t virbuild_elements_slot(virbuild_context *V, uint32_t address,
                                       uint64_t *offset)
{
    const vir_insn *a = &V->insns[virbuild_value_resolve(V, address)].insn;

    if (a->op != VIR_OP_SLOTADDR)
        return 0;
    *offset = a->b | (uint64_t)a->c << 32;
    return a->a + 1;
}

VPRIV bool virbuild_vectors_from_elements(codegen_context *e)
{
    virbuild_context *V = e->vir_builder;
    bool changed = false;

    if (!V->slot_count)
        return false;
    for (uint32_t oi = 0; oi < V->block_order_count; oi++) {
        uint32_t b = V->block_order[oi];
        virbuild_elements E = {};
        uint32_t prev = 0;
        uint32_t zeroed = 0; /* slot + 1 of the last 16-byte memset of 0 */

        for (uint32_t q = V->blocks[b].first_insn; q;
             prev = q, q = V->insns[q].next) {
            vir_insn in = V->insns[q].insn;
            uint64_t offset = 0;

            if (virbuild_value_resolve(V, q) != q ||
                virbuild_promote_store_is_dead(V, q))
                continue;
            if (in.op == VIR_OP_STORE && !(in.flags & VIR_INSN_FLAG_VOL)) {
                uint32_t slot = virbuild_elements_slot(V, in.a, &offset);
                uint32_t size = virbuild_access_bytes(V, &in);

                if (!slot || size > 8 || offset + size > 16) {
                    /* a vector store, or one that may write any slot */
                    E.slot = 0;
                    zeroed = 0;
                    continue;
                }
                uint32_t bytes = (1u << size) - 1;
                if (zeroed && zeroed != slot)
                    zeroed = 0;
                /* a byte stored twice: what the slot holds is no longer tracked,
                   its zeros neither */
                if (E.slot == slot && (E.covered & bytes << offset))
                    zeroed = 0;
                if (E.slot != slot || (E.covered & bytes << offset)) {
                    E = {};
                    E.slot = slot;
                }
                E.covered = (uint16_t)(E.covered | bytes << offset);
                E.value[offset] = virbuild_value_resolve(V, in.b);
                E.width[offset] = (uint8_t)size;
            } else if (in.op == VIR_OP_LOAD && vir_type_is_vector(in.type)) {
                if (!E.slot ||
                    virbuild_elements_slot(V, in.a, &offset) != E.slot ||
                    offset || (in.flags & VIR_INSN_FLAG_VOL))
                    continue;
                virbuild_elements_at W = {e, b, &prev};
                if (zeroed == E.slot)
                    virbuild_elements_zero_half(e, &E);
                uint32_t value = virbuild_elements_vector(&W, &E, in.type);

                if (value) {
                    V->forward[q] = value;
                    changed = true;
                }
            } else if (vir_opcode_table[in.op].effects &
                       (VIR_EFFECT_W | VIR_EFFECT_C | VIR_EFFECT_A |
                        VIR_EFFECT_F)) {
                /* a memset before the stores begins again; anything else may write */
                int64_t value = 0, bytes = 0;

                E.slot = 0;
                zeroed = 0;
                if (in.op == VIR_OP_MEMSET &&
                    virbuild_value_integer_constant(
                        V, virbuild_value_resolve(V, in.b), &value) &&
                    !value &&
                    virbuild_value_integer_constant(
                        V, virbuild_value_resolve(V, in.c), &bytes) &&
                    bytes >= 16) {
                    uint32_t slot = virbuild_elements_slot(V, in.a, &offset);

                    if (!offset)
                        zeroed = slot;
                }
            }
        }
    }
    return changed;
}
