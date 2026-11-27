/* Forward the fields of a freshly initialized record to its copy destination.
 * Keep the original writes until slot promotion proves the local is private. */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
#include <stdlib.h>
#include "common/literal.h"
#include "common/scoped-cleanup.h"
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "simplify.h"
#include "lowering-driver.h"

typedef struct {
    uint32_t zero, fields[4], count;
} virbuild_record_init;

/* Overwrite the copy at its original program point. All field values have
 * already been evaluated, including loads that might alias the destination. */
static void virbuild_record_copy_replace(codegen_context *e, uint32_t b,
                                         uint32_t q,
                                         const virbuild_record_init *init)
{
    virbuild_context *V = e->vir_builder;
    vir_insn copy = V->insns[q].insn;
    uint32_t next = V->insns[q].next, last = q;
    V->insns[q].insn = VCC_LITERAL(vir_insn, VIR_OP_MEMSET, VIR_TYPE_VOID, 0,
                                   copy.a, V->insns[init->zero].insn.b, copy.c);
    for (uint32_t j = 0; j < init->count; j++) {
        vir_insn store = V->insns[init->fields[j]].insn;
        uint32_t off = V->insns[store.a].insn.b, addr = copy.a;
        vir_insn dst = V->insns[copy.a].insn;
        if (off && dst.op == VIR_OP_SLOTADDR) {
            uint64_t base = (uint64_t)dst.c << 32 | dst.b;
            addr = virbuild_slot_address(e, dst.a, (int64_t)(base + off));
        } else if (off) {
            uint32_t displacement =
                virbuild_constant_integer(e, VIR_TYPE_I64, off);
            addr = virbuild_preir_insn_add(e, VIR_OP_PTRADD, VIR_TYPE_PTR, 0,
                                           addr, displacement, 0, b);
            V->insns[last].next = addr;
            last = addr;
        }
        /* Only the width transfers. The destination can be unaligned. */
        uint32_t field = virbuild_preir_insn_add(
            e, VIR_OP_STORE, VIR_TYPE_VOID,
            VIR_INSN_FLAG_WIDTH_LOG2(store.flags), addr, store.b, 0, b);
        V->insns[last].next = field;
        last = field;
    }
    V->insns[last].next = next;
    if (V->blocks[b].last_insn == q)
        V->blocks[b].last_insn = last;
}

/* ABI copies read whole words, including padding. When the word comes
 * from a real zero fill plus one low field, its value is already in SSA. */
static bool virbuild_record_word_forward(codegen_context *e, uint32_t q,
                                         uint32_t slot, int64_t size,
                                         const virbuild_record_init *init)
{
    virbuild_context *V = e->vir_builder;
    vir_insn load = V->insns[q].insn;
    vir_insn address = V->insns[load.a].insn;
    if (load.type != VIR_TYPE_I64 ||
        VIR_INSN_FLAG_WIDTH_LOG2(load.flags) != 3 ||
        (load.flags & VIR_INSN_FLAG_VOL) || address.op != VIR_OP_SLOTADDR ||
        address.a != slot || address.c || (int64_t)address.b + 8 > size)
        return false;
    uint32_t field = 0, width = 0;
    for (uint32_t i = 0; i < init->count; i++) {
        vir_insn store = V->insns[init->fields[i]].insn;
        uint32_t off = V->insns[store.a].insn.b;
        uint32_t lg = VIR_INSN_FLAG_WIDTH_LOG2(store.flags);
        if (off >= address.b + 8 || off + (1u << lg) <= address.b)
            continue;
        if (off != address.b || (field && lg < width))
            return false;
        field = virbuild_value_resolve(V, store.b);
        width = lg;
    }
    if (!field) {
        uint32_t zero = virbuild_constant_integer(e, VIR_TYPE_I64, 0);
        V->forward[q] = zero;
        return true;
    }
    uint32_t type = V->insns[field].insn.type;
    if (width == 3 && type == VIR_TYPE_PTR)
        V->insns[q].insn =
            VCC_LITERAL(vir_insn, VIR_OP_P2I, VIR_TYPE_I64, 0, field, 0, 0);
    else if (width == 3 && type == VIR_TYPE_I64)
        V->forward[q] = field;
    else if (width < 3 && (type == VIR_TYPE_I32 || type == VIR_TYPE_I64))
        V->insns[q].insn = VCC_LITERAL(vir_insn, VIR_OP_EXT, VIR_TYPE_I64,
                                       (uint16_t)(width), field, 0, 0);
    else
        return false;
    return true;
}

/* A wrapper returning a callee's memory result can pass its own ABI
 * result buffer. Count uses by slot, including derived slot addresses and
 * call/edge arguments, so another reader cannot observe the removed copy. */
typedef struct {
    uint32_t count;
    bool escaped;
} virbuild_copy_slot;

static virbuild_copy_slot *virbuild_slot_operand_counts(codegen_context *e)
{
    virbuild_context *V = e->vir_builder;
    virbuild_copy_slot *uses =
        (virbuild_copy_slot *)calloc(V->slot_count, sizeof *uses);
    bool returns_twice = false;
    if (!uses)
        virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
    for (uint32_t q = 1; q < V->insn_count; q++) {
        if (V->insns[q].block == VIRBUILD_BLOCK_NONE ||
            virbuild_value_resolve(V, q) != q ||
            virbuild_promote_store_is_dead(V, q))
            continue;
        vir_insn *in = &V->insns[q].insn;
        if (in->op == VIR_OP_CALL &&
            virbuild_call_returns_twice(
                e, &V->insns[virbuild_value_resolve(V, in->a)].insn))
            returns_twice = true;
        VIRBUILD_FOR_EACH_OPERAND(V, q, operand, {
            const vir_insn *value =
                &V->insns[virbuild_value_resolve(V, *operand)].insn;
            if (value->op == VIR_OP_SLOTADDR) {
                virbuild_copy_slot *slot = &uses[value->a];
                slot->count++;
                if (!(operand == &in->a &&
                      (in->op == VIR_OP_LOAD || in->op == VIR_OP_STORE ||
                       in->op == VIR_OP_MEMCPY || in->op == VIR_OP_MEMSET)) &&
                    !(operand == &in->b && in->op == VIR_OP_MEMCPY))
                    slot->escaped = true;
            }
        });
    }
    /* A nonlocal exit could expose an incompletely assigned local. */
    if (returns_twice)
        for (uint32_t i = 0; i < V->slot_count; i++)
            uses[i].escaped = true;
    return uses;
}

static bool virbuild_record_call_forward(codegen_context *e, uint32_t call_id,
                                         uint32_t copy_id,
                                         virbuild_copy_slot **slot_uses)
{
    virbuild_context *V = e->vir_builder;
    vir_insn copy = V->insns[copy_id].insn, call = V->insns[call_id].insn;
    vir_insn dst = V->insns[copy.a].insn, src = V->insns[copy.b].insn;
    bool returning = dst.op == VIR_OP_PARAM && dst.a == VIR_GROUP_RESULT;
    if ((!returning && (dst.op != VIR_OP_SLOTADDR || dst.b || dst.c)) ||
        src.op != VIR_OP_SLOTADDR || src.b || src.c ||
        VIR_GROUP_KIND(V->extra[call.b + 1]) != VIR_GROUP_SRET ||
        virbuild_value_resolve(V, V->extra[call.c + 1]) != copy.b)
        return false;
    uint32_t callee = V->extra[call.b + 2];
    int64_t bytes;
    if (!virbuild_value_integer_constant(V, copy.c, &bytes) ||
        bytes != VIR_GROUP_SIZE(callee))
        return false;
    if (returning) {
        uint32_t result = V->extra[V->function_signature + 2];
        if (VIR_GROUP_SIZE(result) != bytes ||
            VIR_GROUP_ALIGN_LOG2(result) < VIR_GROUP_ALIGN_LOG2(callee))
            return false;
    } else if (dst.a == src.a || V->slots[dst.a].size < (uint64_t)bytes ||
               V->slots[dst.a].align < (1u << VIR_GROUP_ALIGN_LOG2(callee))) {
        return false;
    }
    if (!*slot_uses)
        *slot_uses = virbuild_slot_operand_counts(e);
    if ((*slot_uses)[src.a].count != 2 ||
        (!returning && (*slot_uses)[dst.a].escaped))
        return false;
    if (!returning)
        (*slot_uses)[dst.a].escaped = true; /* now passed to a call */
    V->extra[call.c + 1] = copy.a;
    V->insns[copy_id].insn =
        VCC_LITERAL(vir_insn, VIR_OP_NOP, VIR_TYPE_VOID, 0, 0, 0, 0);
    return true;
}

/* A temporary retargeted: `memcpy D, $S` where $S is only filled (and read)
 * earlier in the same block and then copied once.  Its stores go to D, and the
 * copy goes.  D is the function's result object, which the caller guarantees
 * nothing else names (clang marks it noalias), or a local whose address goes
 * nowhere; either way D is not touched between $S's first use and the copy.
 */
typedef struct {
    uint32_t slot;        /* $S */
    uint32_t to;          /* $T, or VIRBUILD_BLOCK_NONE for the result */
    uint32_t destination; /* D's value; the result object's for it */
    uint64_t base;        /* D's offset in $T or in the result */
    uint32_t align;       /* what D is known to be aligned to */
} virbuild_retarget;

/* the slot an operand of q addresses, resolved: its slotaddr, else NULL */
static const vir_insn *virbuild_retarget_address(virbuild_context *V,
                                                 uint32_t operand)
{
    const vir_insn *a = &V->insns[virbuild_value_resolve(V, operand)].insn;

    if (a->op != VIR_OP_SLOTADDR)
        return NULL;
    return a;
}

/* the straight line of blocks that ends in the one with the copy: each but the
 * first has the one before it as its only predecessor */
typedef struct {
    const uint32_t *blocks;
    uint32_t count;
} virbuild_straight_line;

/* whether value points into D's object (the result, or $T), and where */
static bool virbuild_retarget_points_in(virbuild_context *V,
                                        const virbuild_retarget *r,
                                        uint32_t value, int64_t *offset)
{
    const vir_insn *in = &V->insns[virbuild_value_resolve(V, value)].insn;

    *offset = 0;
    if (r->to != VIRBUILD_BLOCK_NONE) {
        *offset = (int64_t)((uint64_t)in->c << 32 | in->b);
        return in->op == VIR_OP_SLOTADDR && in->a == r->to;
    }
    while (in->op == VIR_OP_PTRADD) {
        int64_t k;

        if (!virbuild_value_integer_constant(V, in->b, &k))
            break;
        *offset += k;
        in = &V->insns[virbuild_value_resolve(V, in->a)].insn;
    }
    return in->op == VIR_OP_PARAM && in->a == VIR_GROUP_RESULT;
}

/* whether instruction q, with an operand pointing at offset in D's object,
 * leaves D's bytes alone: an access beside them, or the address of one */
static bool virbuild_retarget_beside(virbuild_context *V,
                                     const virbuild_retarget *r, uint32_t q,
                                     bool address, int64_t offset,
                                     int64_t bytes)
{
    const vir_insn *in = &V->insns[q].insn;
    int64_t size = 0, k;

    if (!address)
        return false;
    if (in->op == VIR_OP_PTRADD) /* (its uses are checked where they are) */
        return virbuild_value_integer_constant(V, in->b, &k);
    if (in->op == VIR_OP_LOAD || in->op == VIR_OP_STORE)
        size = virbuild_access_bytes(V, in);
    else if ((in->op != VIR_OP_MEMCPY && in->op != VIR_OP_MEMSET) ||
             !virbuild_value_integer_constant(V, in->c, &size))
        return false;
    return offset + size <= (int64_t)r->base ||
           offset >= (int64_t)r->base + bytes;
}

/* whether every use of $S is before the copy on its straight line, and such
 * that D serves as well; *references counts them */
static bool virbuild_retarget_allowed(virbuild_context *V,
                                      virbuild_straight_line line,
                                      uint32_t copy, const virbuild_retarget *r,
                                      int64_t bytes, uint32_t *references)
{
    bool started = false;

    *references = 0;
    uint32_t at_block = 0;

    for (uint32_t q = V->blocks[line.blocks[0]].first_insn; q != copy;
         q = V->insns[q].next) {
        while (!q) {
            if (++at_block == line.count)
                return false;
            q = V->blocks[line.blocks[at_block]].first_insn;
        }
        if (q == copy)
            break;
        if (virbuild_value_resolve(V, q) != q ||
            virbuild_promote_store_is_dead(V, q))
            continue;
        vir_insn *in = &V->insns[q].insn;
        bool uses = false, destination = false, allowed = true;

        VIRBUILD_FOR_EACH_OPERAND(V, q, operand, {
            const vir_insn *at = virbuild_retarget_address(V, *operand);
            int64_t offset = 0;
            bool address = operand == &in->a ||
                           (in->op == VIR_OP_MEMCPY && operand == &in->b);

            if (at && at->a == r->slot) {
                uses = true;
                (*references)++;
                if (operand != &in->a || (in->flags & VIR_INSN_FLAG_VOL) ||
                    (in->op != VIR_OP_LOAD && in->op != VIR_OP_STORE &&
                     in->op != VIR_OP_MEMCPY && in->op != VIR_OP_MEMSET))
                    allowed = false;
            } else if (virbuild_retarget_points_in(V, r, *operand, &offset) &&
                       !virbuild_retarget_beside(V, r, q, address, offset,
                                                 bytes)) {
                destination = true;
            }
        });
        if (!allowed)
            return false;
        started = started || uses;
        if (started && destination)
            return false;
    }
    return true;
}

static bool virbuild_temporary_retarget(codegen_context *e,
                                        virbuild_straight_line line,
                                        uint32_t copy, virbuild_copy_slot *uses)
{
    virbuild_context *V = e->vir_builder;
    vir_insn in = V->insns[copy].insn;
    uint32_t to = virbuild_value_resolve(V, in.a);
    vir_insn dst = V->insns[to].insn;
    const vir_insn *source = virbuild_retarget_address(V, in.b);
    int64_t bytes;

    if (!source || source->b || source->c ||
        !virbuild_value_integer_constant(V, in.c, &bytes) || bytes <= 0 ||
        (uint64_t)bytes != V->slots[source->a].size || uses[source->a].escaped)
        return false;
    virbuild_retarget r = {source->a, VIRBUILD_BLOCK_NONE, to, 0, 1};

    if (dst.op == VIR_OP_SLOTADDR) {
        r.to = dst.a;
        r.base = (uint64_t)dst.c << 32 | dst.b;
        r.align = V->slots[r.to].align;
        if (r.to == r.slot || uses[r.to].escaped)
            return false;
    } else {
        /* the result object, or at a constant offset in it */
        int64_t offset = 0;

        if (dst.op == VIR_OP_PTRADD) {
            if (!virbuild_value_integer_constant(V, dst.b, &offset) ||
                offset < 0)
                return false;
            r.destination = virbuild_value_resolve(V, dst.a);
            dst = V->insns[r.destination].insn;
        }
        if (dst.op != VIR_OP_PARAM || dst.a != VIR_GROUP_RESULT)
            return false;
        uint32_t result = V->extra[V->function_signature + 2];

        if (VIR_GROUP_SIZE(result) < offset + bytes)
            return false;
        r.base = (uint64_t)offset;
        r.align = 1u << VIR_GROUP_ALIGN_LOG2(result);
    }
    if (r.base)
        r.align = (uint32_t)((r.base | r.align) & -(r.base | r.align));
    uint32_t references;

    if (!virbuild_retarget_allowed(V, line, copy, &r, bytes, &references) ||
        references + 1 != uses[r.slot].count)
        return false;
    uint32_t previous = 0, at_block = 0, b = line.blocks[0];

    for (uint32_t q = V->blocks[b].first_insn; q != copy;
         previous = q, q = V->insns[q].next) {
        while (!q) { /* (virbuild_retarget_allowed found the copy) */
            b = line.blocks[++at_block];
            previous = 0;
            q = V->blocks[b].first_insn;
        }
        if (q == copy)
            break;
        if (virbuild_value_resolve(V, q) != q ||
            virbuild_promote_store_is_dead(V, q))
            continue;
        uint32_t op = V->insns[q].insn.op;

        /* (only these have an address in .a) */
        if (op != VIR_OP_LOAD && op != VIR_OP_STORE && op != VIR_OP_MEMCPY &&
            op != VIR_OP_MEMSET)
            continue;
        const vir_insn *at = virbuild_retarget_address(V, V->insns[q].insn.a);

        if (!at || at->a != r.slot)
            continue;
        uint64_t offset = (uint64_t)at->c << 32 | at->b;
        uint32_t address = r.destination;
        uint64_t known = offset | r.align;

        /* the access claims no more alignment than its new address has */
        uint32_t limit = virbuild_integer_log2(known & (0 - known));
        vir_insn *access = &V->insns[q].insn;

        if (VIR_INSN_FLAG_ALIGN_LOG2(access->flags) > limit)
            access->flags =
                (uint16_t)((access->flags & ~(15u << 4)) | limit << 4);
        offset += r.base;

        if (r.to != VIRBUILD_BLOCK_NONE) {
            address = virbuild_slot_address(e, r.to, (int64_t)offset);
            uses[r.to].count++;
        } else if (offset) {
            uint32_t displacement =
                virbuild_constant_integer(e, VIR_TYPE_I64, (int64_t)offset);

            address = virbuild_preir_insn_add(e, VIR_OP_PTRADD, VIR_TYPE_PTR, 0,
                                              address, displacement, 0, b);
            V->insns[address].next = q;
            if (previous)
                V->insns[previous].next = address;
            else
                V->blocks[b].first_insn = address;
            previous = address;
        }
        V->insns[q].insn.a = address;
        V->insns[q].slot_plus_one = 0;
    }
    uses[r.slot].count = 0;
    if (r.to != VIRBUILD_BLOCK_NONE) /* (the copy's own) */
        uses[r.to].count--;
    V->insns[copy].insn =
        VCC_LITERAL(vir_insn, VIR_OP_NOP, VIR_TYPE_VOID, 0, 0, 0, 0);
    return true;
}

VPRIV bool virbuild_aggregate_copies_forward(codegen_context *e)
{
    virbuild_context *V = e->vir_builder;
    virbuild_record_init init = {0};
    uint32_t call = 0;
    virbuild_copy_slot *slot_uses = NULL;
    uint32_t slot = 0, successor = VIRBUILD_BLOCK_NONE;
    int64_t size = 0;
    bool changed = false;
    VAUTOFREE uint32_t *chain =
        (uint32_t *)malloc(V->block_order_count * sizeof *chain + 1);
    uint32_t chained = 0;

    if (!chain)
        virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
    for (uint32_t i = 0; i < V->block_order_count; i++) {
        uint32_t b = V->block_order[i];
        if (b != successor || V->blocks[b].predecessor_count != 1) {
            init.zero = 0;
            call = 0;
            chained = 0;
        }
        chain[chained++] = b;
        for (uint32_t q = V->blocks[b].first_insn; q; q = V->insns[q].next) {
            if (virbuild_value_resolve(V, q) != q ||
                virbuild_promote_store_is_dead(V, q))
                continue;
            vir_insn in = V->insns[q].insn;
            int64_t k;
            if (in.op == VIR_OP_MEMCPY && !in.flags &&
                virbuild_value_resolve(V, in.a) ==
                    virbuild_value_resolve(V, in.b)) { /* onto itself */
                V->insns[q].insn = VCC_LITERAL(vir_insn, VIR_OP_NOP,
                                               VIR_TYPE_VOID, 0, 0, 0, 0);
                changed = true;
                continue;
            }
            if (in.op == VIR_OP_MEMCPY && !in.flags &&
                V->insns[virbuild_value_resolve(V, in.b)].insn.op ==
                    VIR_OP_SLOTADDR) {
                if (!slot_uses)
                    slot_uses = virbuild_slot_operand_counts(e);
                if (virbuild_temporary_retarget(e, {chain, chained}, q,
                                                slot_uses)) {
                    changed = true;
                    init.zero = 0;
                    call = 0;
                    continue;
                }
            }
            if (call && in.op == VIR_OP_MEMCPY && !in.flags &&
                virbuild_record_call_forward(e, call, q, &slot_uses)) {
                changed = true;
                call = 0;
                continue;
            }
            call = in.op == VIR_OP_CALL ? q : 0;
            if (init.zero && in.op == VIR_OP_LOAD &&
                virbuild_record_word_forward(e, q, slot, size, &init)) {
                changed = true;
                continue;
            }
            if (init.zero && in.op == VIR_OP_MEMCPY && !in.flags &&
                in.b == V->insns[init.zero].insn.a &&
                virbuild_value_integer_constant(V, in.c, &k) && k == size) {
                virbuild_record_copy_replace(e, b, q, &init);
                changed = true;
                in = V->insns[q].insn;
            }
            if (in.op == VIR_OP_MEMSET && !in.flags &&
                V->insns[in.a].insn.op == VIR_OP_SLOTADDR &&
                !V->insns[in.a].insn.b && !V->insns[in.a].insn.c &&
                virbuild_value_integer_constant(V, in.b, &k) && k == 0 &&
                virbuild_value_integer_constant(V, in.c, &k) && k > 0 &&
                k <= 128) {
                slot = V->insns[in.a].insn.a;
                size = k;
                init.zero = q;
                init.count = 0;
            } else if (init.zero && in.op == VIR_OP_STORE &&
                       !(in.flags & VIR_INSN_FLAG_VOL) &&
                       !vir_type_is_vector(V->insns[in.b].insn.type) &&
                       V->insns[in.a].insn.op == VIR_OP_SLOTADDR &&
                       V->insns[in.a].insn.a == slot &&
                       !V->insns[in.a].insn.c &&
                       (int64_t)V->insns[in.a].insn.b +
                               (1 << VIR_INSN_FLAG_WIDTH_LOG2(in.flags)) <=
                           size &&
                       init.count < 4) {
                init.fields[init.count++] = q;
            } else if (vir_opcode_table[in.op].effects &
                       (VIR_EFFECT_W | VIR_EFFECT_C | VIR_EFFECT_A)) {
                init.zero = 0;
            }
        }
        const vir_insn *term = &V->insns[V->blocks[b].terminator].insn;
        successor = term->op == VIR_OP_BR ? term->a : VIRBUILD_BLOCK_NONE;
    }
    free(slot_uses);
    return changed;
}
