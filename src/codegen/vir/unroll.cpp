/*
 * Full unrolling of a small loop that runs a constant number of times: acc[k] over four
 * vectors, a copy of a short array.  The counter is a constant in each copy, so simplify folds
 * the addresses and promote-again keeps the elements in registers.
 *
 * Only a straight-line loop: a header that branches on counter <cc> constant, a chain of blocks
 * joined by br back to it, no calls.
 */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
#include "common/literal.h"
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "simplify.h"

#define VIRBUILD_UNROLL_TRIPS 16
/* with the vectorizer, which runs later: a longer loop of scalars is its */
#define VIRBUILD_UNROLL_SCALAR_TRIPS_VECTORIZING 8
#define VIRBUILD_UNROLL_SIZE 640 /* instructions after unrolling */
#define VIRBUILD_UNROLL_BLOCKS 8
#define VIRBUILD_UNROLL_PARAMETERS 24

typedef struct virbuild_unroll_loop {
    uint32_t header, entry, exit;
    uint32_t entry_index, latch_index;      /* in the header's predecessors */
    uint32_t block[VIRBUILD_UNROLL_BLOCKS]; /* the header, then the chain */
    uint32_t block_count;
    uint32_t parameter[VIRBUILD_UNROLL_PARAMETERS];
    uint32_t parameter_count;
    uint32_t counter; /* an index into parameter */
    int64_t first, step;
    uint32_t trips, size;
    bool vectorizing; /* -fvectorize */
    bool has_vector;  /* the loop computes with vectors already */
    uint32_t from[VIRBUILD_UNROLL_SIZE], to[VIRBUILD_UNROLL_SIZE];
    uint32_t mapped;
} virbuild_unroll_loop;

static bool virbuild_unroll_copyable(const vir_insn *in)
{
    if (vir_opcode_table[in->op].effects & (VIR_EFFECT_C | VIR_EFFECT_T))
        return false;
    switch (in->op) {
    case VIR_OP_CALL:
    case VIR_OP_ASM:
    case VIR_OP_VA_START:
    case VIR_OP_VA_ARG:
    case VIR_OP_ALOAD:
    case VIR_OP_ASTORE:
    case VIR_OP_AXCHG:
    case VIR_OP_AXADD:
    case VIR_OP_ACAS:
    case VIR_OP_AFENCE:
    case VIR_OP_BARRIER:
    case VIR_OP_LABELADDR:
        return false;
    default:
        return !(in->op == VIR_OP_LOAD || in->op == VIR_OP_STORE) ||
               !(in->flags & VIR_INSN_FLAG_VOL);
    }
}

/* the instructions a copy of L's blocks has; false when one cannot be copied */
static bool virbuild_unroll_count(virbuild_context *V, virbuild_unroll_loop *L)
{
    L->size = 0;
    L->has_vector = false;
    for (uint32_t i = 0; i < L->block_count; i++)
        for (uint32_t q = V->blocks[L->block[i]].first_insn; q;
             q = V->insns[q].next) {
            if (virbuild_value_resolve(V, q) != q ||
                virbuild_promote_store_is_dead(V, q))
                continue;
            if (!virbuild_unroll_copyable(&V->insns[q].insn))
                return false;
            L->has_vector |= vir_type_is_vector(V->insns[q].insn.type);
            L->size++;
        }
    return true;
}

/* whether the block after the header's branch starts a chain back to the header */
static bool virbuild_unroll_chain(virbuild_context *V, virbuild_unroll_loop *L,
                                  uint32_t b)
{
    L->block[0] = L->header;
    L->block_count = 1;
    while (b != L->header) {
        const virbuild_preir_block *k = &V->blocks[b];

        if (L->block_count == VIRBUILD_UNROLL_BLOCKS || b == L->exit ||
            k->predecessor_count != 1 || !k->terminator || k->is_landing_pad ||
            V->insns[k->terminator].insn.op != VIR_OP_BR)
            return false;
        for (uint32_t p = k->first_parameter; p; p = V->insns[p].next)
            if (V->forward[p] == p)
                return false;
        L->block[L->block_count++] = b;
        b = V->insns[k->terminator].insn.a;
    }
    return L->block_count > 1;
}

/* the loop's counter and trip count from the header's branch; false when not constant */
static bool virbuild_unroll_trips(virbuild_context *V, virbuild_unroll_loop *L,
                                  bool continues_when_true)
{
    const vir_insn *branch = &V->insns[V->blocks[L->header].terminator].insn;
    uint32_t test = virbuild_value_resolve(V, branch->a);
    vir_insn compare = V->insns[test].insn;

    if (compare.op != VIR_OP_ICMP || V->insns[test].block != L->header)
        return false;
    uint32_t a = virbuild_value_resolve(V, compare.a);
    uint32_t b = virbuild_value_resolve(V, compare.b);
    int64_t bound = 0;

    if (!virbuild_value_integer_constant(V, b, &bound))
        return false;
    for (uint32_t i = 0; i < L->parameter_count; i++) {
        uint32_t p = L->parameter[i];

        if (p != a)
            continue;
        const uint32_t *incoming = &V->extra[V->insns[p].insn.b];
        uint32_t first = virbuild_value_resolve(V, incoming[L->entry_index]);
        uint32_t next = virbuild_value_resolve(V, incoming[L->latch_index]);
        vir_insn add = V->insns[next].insn;

        if (add.op != VIR_OP_ADD || virbuild_value_resolve(V, add.a) != p ||
            !virbuild_value_integer_constant(
                V, virbuild_value_resolve(V, add.b), &L->step) ||
            !virbuild_value_integer_constant(V, first, &L->first))
            return false;
        uint32_t type = V->insns[p].insn.type;

        if (type != VIR_TYPE_I32 && type != VIR_TYPE_I64)
            return false;
        L->counter = i;
        int64_t v = L->first;
        for (L->trips = 0; L->trips <= VIRBUILD_UNROLL_TRIPS; L->trips++) {
            bool holds =
                virbuild_compare_constants(compare.flags & 15, type, v, bound);

            if (holds != continues_when_true)
                return true;
            v = (int64_t)((uint64_t)v + (uint64_t)L->step);
            if (type == VIR_TYPE_I32)
                v = (int32_t)v;
        }
        return false;
    }
    return false;
}

static bool virbuild_unroll_find(virbuild_context *V, virbuild_unroll_loop *L,
                                 uint32_t h)
{
    const virbuild_preir_block *k = &V->blocks[h];

    if (!k->is_reachable || k->predecessor_count != 2 || !k->terminator ||
        k->is_landing_pad || V->insns[k->terminator].insn.op != VIR_OP_BRIF)
        return false;
    L->header = h;
    L->parameter_count = 0;
    for (uint32_t p = k->first_parameter; p; p = V->insns[p].next)
        if (V->forward[p] == p) {
            if (L->parameter_count == VIRBUILD_UNROLL_PARAMETERS)
                return false;
            L->parameter[L->parameter_count++] = p;
        }
    for (int side = 0; side < 2; side++) {
        uint32_t body = *virbuild_block_successor_pointer(V, h, (uint32_t)side);

        L->exit = *virbuild_block_successor_pointer(V, h, (uint32_t)(1 - side));
        if (L->exit == h || !virbuild_unroll_chain(V, L, body))
            continue;
        uint32_t latch = L->block[L->block_count - 1];
        uint32_t first = V->predecessors[k->predecessor_first];
        uint32_t second = V->predecessors[k->predecessor_first + 1];

        if (first == latch)
            L->latch_index = 0;
        else if (second == latch)
            L->latch_index = 1;
        else
            return false;
        L->entry_index = 1 - L->latch_index;
        L->entry = V->predecessors[k->predecessor_first + L->entry_index];
        const virbuild_preir_block *entry = &V->blocks[L->entry];

        if (L->entry == latch || !entry->terminator ||
            V->insns[entry->terminator].insn.op != VIR_OP_BR)
            return false;
        return virbuild_unroll_trips(V, L, side == 0) &&
               virbuild_unroll_count(V, L) &&
               (!L->vectorizing || L->has_vector ||
                L->trips <= VIRBUILD_UNROLL_SCALAR_TRIPS_VECTORIZING) &&
               L->size * (L->trips + 1) <= VIRBUILD_UNROLL_SIZE;
    }
    return false;
}

/* v in the copy being made */
static uint32_t virbuild_unroll_value(virbuild_context *V,
                                      const virbuild_unroll_loop *L, uint32_t v)
{
    v = virbuild_value_resolve(V, v);
    for (uint32_t i = 0; i < L->mapped; i++)
        if (L->from[i] == v)
            return L->to[i];
    return v;
}

static void virbuild_unroll_map(virbuild_unroll_loop *L, uint32_t from,
                                uint32_t to)
{
    for (uint32_t i = 0; i < L->mapped; i++)
        if (L->from[i] == from) {
            L->to[i] = to;
            return;
        }
    L->from[L->mapped] = from;
    L->to[L->mapped++] = to;
}

/* one copy of block b's instructions at the end of the entry block */
static void virbuild_unroll_copy(codegen_context *e, virbuild_unroll_loop *L,
                                 uint32_t b)
{
    virbuild_context *V = e->vir_builder;

    for (uint32_t q = V->blocks[b].first_insn; q; q = V->insns[q].next) {
        if (virbuild_value_resolve(V, q) != q ||
            virbuild_promote_store_is_dead(V, q))
            continue;
        vir_insn in = V->insns[q].insn;
        const vir_opcode_info *info = &vir_opcode_table[in.op];
        uint32_t *operand[3] = {&in.a, &in.b, &in.c};

        for (int k = 0; k < 3; k++)
            if (virbuild_operand_kind_is_value(info->operand_kinds[k]))
                *operand[k] = virbuild_unroll_value(V, L, *operand[k]);
        uint32_t copy = virbuild_block_insn_append(e, L->entry, in.op, in.type,
                                                   in.flags, in.a, in.b, in.c);

        V->insns[copy].location = V->insns[q].location;
        V->insns[copy].slot_plus_one = V->insns[q].slot_plus_one;
        virbuild_unroll_map(L, q, copy);
    }
}

static void virbuild_unroll_apply(codegen_context *e, virbuild_unroll_loop *L)
{
    virbuild_context *V = e->vir_builder;
    uint32_t value[VIRBUILD_UNROLL_PARAMETERS];
    uint32_t type = V->insns[L->parameter[L->counter]].insn.type;
    int64_t counter = L->first;

    L->mapped = 0;
    for (uint32_t i = 0; i < L->parameter_count; i++)
        value[i] = virbuild_value_resolve(
            V, V->extra[V->insns[L->parameter[i]].insn.b + L->entry_index]);
    /* the trips, then the header once more for the test that leaves */
    for (uint32_t trip = 0; trip <= L->trips; trip++) {
        value[L->counter] = virbuild_constant_integer(e, type, counter);
        for (uint32_t i = 0; i < L->parameter_count; i++)
            virbuild_unroll_map(L, L->parameter[i], value[i]);
        uint32_t blocks = L->block_count;

        if (trip == L->trips)
            blocks = 1;
        for (uint32_t i = 0; i < blocks; i++)
            virbuild_unroll_copy(e, L, L->block[i]);
        if (trip == L->trips)
            break;
        uint32_t next[VIRBUILD_UNROLL_PARAMETERS];

        for (uint32_t i = 0; i < L->parameter_count; i++)
            next[i] = virbuild_unroll_value(
                V, L,
                V->extra[V->insns[L->parameter[i]].insn.b + L->latch_index]);
        for (uint32_t i = 0; i < L->parameter_count; i++)
            value[i] = next[i];
        counter = (int64_t)((uint64_t)counter + (uint64_t)L->step);
    }
    /* the exit now follows the entry block */
    virbuild_preir_block *exit = &V->blocks[L->exit];

    for (uint32_t j = 0; j < exit->predecessor_count; j++)
        if (V->predecessors[exit->predecessor_first + j] == L->header) {
            V->predecessors[exit->predecessor_first + j] = L->entry;
            for (uint32_t p = exit->first_parameter; p; p = V->insns[p].next)
                if (V->forward[p] == p) {
                    uint32_t at = V->insns[p].insn.b + j;

                    V->extra[at] = virbuild_unroll_value(V, L, V->extra[at]);
                }
        }
    V->insns[V->blocks[L->entry].terminator].insn.a = L->exit;
    /* what the loop defined is what its last copy did, for those after it */
    for (uint32_t i = 0; i < L->block_count; i++) {
        virbuild_preir_block *k = &V->blocks[L->block[i]];

        for (uint32_t p = k->first_parameter; p; p = V->insns[p].next)
            if (V->forward[p] == p)
                V->forward[p] = virbuild_unroll_value(V, L, p);
        for (uint32_t q = k->first_insn; q; q = V->insns[q].next) {
            if (V->forward[q] != q)
                continue;
            uint32_t last = virbuild_unroll_value(V, L, q);

            V->insns[q].insn =
                VCC_LITERAL(vir_insn, VIR_OP_NOP, VIR_TYPE_VOID, 0, 0, 0, 0);
            V->insns[q].slot_plus_one = 0;
            if (last != q)
                V->forward[q] = last;
        }
        k->first_insn = k->last_insn = k->terminator = 0;
        k->first_parameter = k->last_parameter = 0;
        k->predecessor_count = 0;
        k->is_reachable = 0;
    }
}

VPRIV bool virbuild_loops_unroll(codegen_context *e)
{
    virbuild_context *V = e->vir_builder;
    virbuild_unroll_loop L;
    bool changed = false;

    if (V->indirect_dispatch && V->indirect_dispatch != VIRBUILD_BLOCK_NONE)
        return false;
    L.vectorizing = e->unit->options->optimize.vectorize;
    /* inner loops come later in the order: a loop whose inner one is gone may then qualify */
    for (uint32_t i = V->block_order_count; i-- > 0;)
        if (virbuild_unroll_find(V, &L, V->block_order[i])) {
            virbuild_unroll_apply(e, &L);
            changed = true;
        }
    if (!changed)
        return false;
    uint32_t n = 0;

    for (uint32_t i = 0; i < V->block_order_count; i++)
        if (V->blocks[V->block_order[i]].is_reachable)
            V->block_order[n++] = V->block_order[i];
    V->block_order_count = n;
    return true;
}
