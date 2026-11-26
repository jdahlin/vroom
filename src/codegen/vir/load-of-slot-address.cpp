/*
 * A load through a pointer that is one of several frame slots' addresses (a select or a block
 * parameter of them: an inlined std::min, max or clamp returning a reference to an argument)
 * loads the values instead and chooses between them.  The addresses then go no further, so
 * promote-again turns the slots into SSA values.  Loading a slot that is not chosen is safe:
 * a frame slot can always be read.
 */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
#include "common/literal.h"
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "simplify.h"

#define VIRBUILD_SLOT_LOAD_DEPTH 6
#define VIRBUILD_SLOT_LOAD_LEAVES 8 /* loads one rewritten load may become */

/* v without the pointer/integer round trip of an inlined call's pointer argument */
static uint32_t virbuild_slot_load_pointer(virbuild_context *V, uint32_t v)
{
    v = virbuild_value_resolve(V, v);
    while (V->insns[v].insn.op == VIR_OP_I2P ||
           V->insns[v].insn.op == VIR_OP_P2I)
        v = virbuild_value_resolve(V, V->insns[v].insn.a);
    return v;
}

/* whether no instruction from *from to end (0: the block's end) writes memory */
static bool virbuild_slot_load_unwritten(virbuild_context *V, uint32_t from,
                                         uint32_t end)
{
    for (uint32_t q = from; q != end && q; q = V->insns[q].next) {
        const virbuild_preir_insn *x = &V->insns[q];

        if (virbuild_value_resolve(V, q) != q ||
            virbuild_promote_store_is_dead(V, q))
            continue;
        if (vir_opcode_table[x->insn.op].effects &
            (VIR_EFFECT_W | VIR_EFFECT_C | VIR_EFFECT_A | VIR_EFFECT_F))
            return false;
        if (x->insn.op == VIR_OP_LOAD && (x->insn.flags & VIR_INSN_FLAG_VOL))
            return false;
    }
    return true;
}

/*
 * Whether v, read at the point before instruction `at` of block b (0: b's end), is a tree of
 * slot addresses whose slots hold there what they held where the tree merges.
 */
static bool virbuild_slot_load_possible(virbuild_context *V, uint32_t v,
                                        uint32_t b, uint32_t at, uint32_t size,
                                        int depth, uint32_t *leaves)
{
    v = virbuild_slot_load_pointer(V, v);
    const virbuild_preir_insn *x = &V->insns[v];

    if (x->insn.op == VIR_OP_SLOTADDR) {
        uint64_t off = x->insn.b | (uint64_t)x->insn.c << 32;

        (*leaves)++;
        return *leaves <= VIRBUILD_SLOT_LOAD_LEAVES &&
               off + size <= V->slots[x->insn.a].size;
    }
    if (depth >= VIRBUILD_SLOT_LOAD_DEPTH)
        return false;
    if (x->insn.op == VIR_OP_SELECT)
        return virbuild_slot_load_possible(V, x->insn.b, b, at, size, depth + 1,
                                           leaves) &&
               virbuild_slot_load_possible(V, x->insn.c, b, at, size, depth + 1,
                                           leaves);
    if (x->insn.op != VIR_OP_BPARAM || x->insn.b == VIRBUILD_BLOCK_NONE)
        return false;
    /* back from the load to the parameter's block, nothing writing */
    uint32_t merge = x->block;
    for (int steps = 0;; steps++) {
        if (!virbuild_slot_load_unwritten(V, V->blocks[b].first_insn, at))
            return false;
        if (b == merge)
            break;
        if (V->blocks[b].predecessor_count != 1 || steps >= 8)
            return false;
        b = V->predecessors[V->blocks[b].predecessor_first];
        at = 0;
    }
    for (uint32_t k = 0; k < V->blocks[merge].predecessor_count; k++)
        if (!virbuild_slot_load_possible(
                V, V->extra[x->insn.b + k],
                V->predecessors[V->blocks[merge].predecessor_first + k], 0,
                size, depth + 1, leaves))
            return false;
    return true;
}

/* a new instruction of block b after *prev (0: first), or at b's end (prev NULL) */
static uint32_t virbuild_slot_load_insert(codegen_context *e, uint32_t b,
                                          uint32_t *prev, vir_insn in)
{
    virbuild_context *V = e->vir_builder;

    if (!prev)
        return virbuild_block_insn_append(e, b, in.op, in.type, in.flags, in.a,
                                          in.b, in.c);
    uint32_t i = virbuild_preir_insn_add(e, in.op, in.type, in.flags, in.a,
                                         in.b, in.c, b);

    if (*prev) {
        V->insns[i].next = V->insns[*prev].next;
        V->insns[*prev].next = i;
    } else {
        V->insns[i].next = V->blocks[b].first_insn;
        V->blocks[b].first_insn = i;
    }
    if (V->blocks[b].last_insn == *prev)
        V->blocks[b].last_insn = i;
    *prev = i;
    return i;
}

/* the value load reads through v (virbuild_slot_load_possible), built at *prev of block b */
static uint32_t virbuild_slot_load_build(codegen_context *e, uint32_t v,
                                         vir_insn load, uint32_t b,
                                         uint32_t *prev)
{
    virbuild_context *V = e->vir_builder;

    v = virbuild_slot_load_pointer(V, v);
    vir_insn in = V->insns[v].insn;

    if (in.op == VIR_OP_SLOTADDR) {
        load.a = v;
        return virbuild_slot_load_insert(e, b, prev, load);
    }
    if (in.op == VIR_OP_SELECT) {
        uint32_t yes = virbuild_slot_load_build(e, in.b, load, b, prev);
        uint32_t no = virbuild_slot_load_build(e, in.c, load, b, prev);
        vir_insn select = in;

        select.type = load.type;
        select.b = yes;
        select.c = no;
        return virbuild_slot_load_insert(e, b, prev, select);
    }
    uint32_t merge = V->insns[v].block;
    uint32_t n = V->blocks[merge].predecessor_count;
    uint32_t p = virbuild_parameter_new(e, merge, in.a, load.type);
    uint32_t o = virbuild_extra_append(e, NULL, 0);

    VIRBUILD_ARRAY_GROW(V->extra, V->extra_count, V->extra_capacity, n);
    V->extra_count += n;
    V->insns[p].insn.b = o;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t value = virbuild_slot_load_build(
            e, V->extra[in.b + k], load,
            V->predecessors[V->blocks[merge].predecessor_first + k], NULL);

        V->extra[o + k] = value;
    }
    return p;
}

/* a value that only passes a pointer on */
static bool virbuild_slot_load_passes_on(uint32_t op)
{
    return op == VIR_OP_SELECT || op == VIR_OP_I2P || op == VIR_OP_P2I ||
           op == VIR_OP_BPARAM;
}

static void virbuild_slot_load_mark(virbuild_context *V, uint32_t *live,
                                    uint32_t *stack, uint32_t *n, uint32_t v)
{
    v = virbuild_value_resolve(V, v);
    if (!live[v]) {
        live[v] = 1;
        stack[(*n)++] = v;
    }
}

/*
 * The pointers the rewritten loads no longer read: promotion counts an address in a dead select
 * or block parameter as escaped, and nothing removes those before it runs.
 */
static void virbuild_slot_load_sweep(codegen_context *e)
{
    virbuild_context *V = e->vir_builder;
    uint32_t count = V->insn_count, n = 0;
    uint32_t *live = (uint32_t *)(virbuild_pass_scratch_get(
        e, (uint64_t)count * 2 * sizeof *live));
    uint32_t *stack = live + count;

    memset(live, 0, (size_t)count * sizeof *live);
    for (uint32_t i = 0; i < V->block_order_count; i++) {
        uint32_t b = V->block_order[i];

        for (uint32_t q = V->blocks[b].first_insn;; q = V->insns[q].next) {
            uint32_t x = q;

            if (!q)
                x = V->blocks[b].terminator;
            if (!x)
                break;
            if (virbuild_value_resolve(V, x) == x &&
                !virbuild_promote_store_is_dead(V, x) &&
                !virbuild_slot_load_passes_on(V->insns[x].insn.op))
                VIRBUILD_FOR_EACH_OPERAND(
                    V, x, o, virbuild_slot_load_mark(V, live, stack, &n, *o));
            if (!q)
                break;
        }
    }
    while (n) { /* what a live pointer is made of is live */
        uint32_t x = stack[--n];

        if (virbuild_slot_load_passes_on(V->insns[x].insn.op))
            VIRBUILD_FOR_EACH_OPERAND(
                V, x, o, virbuild_slot_load_mark(V, live, stack, &n, *o));
    }
    for (uint32_t i = 0; i < V->block_order_count; i++) {
        virbuild_preir_block *k = &V->blocks[V->block_order[i]];

        for (uint32_t p = k->first_parameter; p; p = V->insns[p].next)
            if (V->forward[p] == p && !live[p])
                V->forward[p] =
                    virbuild_undefined_value(e, V->insns[p].insn.type);
        for (uint32_t q = k->first_insn; q; q = V->insns[q].next)
            if (V->forward[q] == q && !live[q] &&
                virbuild_slot_load_passes_on(V->insns[q].insn.op))
                V->insns[q].insn = VCC_LITERAL(vir_insn, VIR_OP_NOP,
                                               VIR_TYPE_VOID, 0, 0, 0, 0);
    }
}

VPRIV bool virbuild_slot_address_loads_split(codegen_context *e)
{
    virbuild_context *V = e->vir_builder;
    bool changed = false;

    if (!V->slot_count)
        return false;
    for (uint32_t oi = 0; oi < V->block_order_count; oi++) {
        uint32_t b = V->block_order[oi];
        uint32_t prev = 0;

        for (uint32_t q = V->blocks[b].first_insn; q;
             prev = q, q = V->insns[q].next) {
            vir_insn load = V->insns[q].insn;

            if (load.op != VIR_OP_LOAD || (load.flags & VIR_INSN_FLAG_VOL) ||
                virbuild_value_resolve(V, q) != q)
                continue;
            uint32_t pointer = virbuild_slot_load_pointer(V, load.a);
            uint32_t op = V->insns[pointer].insn.op;
            uint32_t leaves = 0;
            uint32_t size = virbuild_access_bytes(V, &load);

            if ((op != VIR_OP_SELECT && op != VIR_OP_BPARAM) || size > 8 ||
                !virbuild_slot_load_possible(V, pointer, b, q, size, 0,
                                             &leaves))
                continue;
            uint32_t value =
                virbuild_slot_load_build(e, pointer, load, b, &prev);

            /* (the new instructions are before q: it leaves the list's values) */
            V->forward[q] = value;
            changed = true;
        }
    }
    if (changed)
        virbuild_slot_load_sweep(e);
    return changed;
}
