/*
 * Address cloning (x86-64): a pointer plus a constant that several loads and stores use
 * becomes one pointer addition in front of each of them.
 *
 *     p = ptradd b, 776            p1 = ptradd b, 776;  load p1
 *     load p; ...; store p, x  =>  ...;  p2 = ptradd b, 776;  store p2, x
 *
 * A pointer addition with a single use, the next instruction's address, is its displacement
 * (the lowering folds it, arch/x64/vir/folds.cpp); with two or more uses it is a register of
 * its own and an lea.  The clones are single-use, so each folds, and `p` lives no longer: the
 * base does instead, which is live anyway where it is used again, and several offsets of one
 * base share it.  What keeps `p` (a call argument, a stored pointer) is left as it was.
 *
 * Only for x86-64 (displacements reach +-2 GiB; arm64's scaled and unscaled immediates are
 * a few KiB and what the lowering folds there is decided by its own range), and not for the
 * addresses of frame slots, which the slot passes handle.  -fno-address-clone leaves it out.
 */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
#include "common/literal.h"
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "simplify.h"

VPRIV bool virbuild_addresses_clone(codegen_context *e)
{
    virbuild_context *V = e->vir_builder;
    uint64_t ncnt = (uint64_t)V->insn_count + 1;
    uint32_t *cnt, *left;
    bool any = false;

    if (!CODEGEN_IS_X64(e->compile_context) ||
        !e->unit->options->optimize.address_clone)
        return false;
    cnt = (uint32_t *)virbuild_pass_scratch_get(e, 2 * ncnt * sizeof *cnt);
    left = cnt + ncnt;
    memset(cnt, 0, (size_t)(2 * ncnt) * sizeof *cnt);
    for (uint32_t i = 0; i < V->block_order_count; i++) {
        uint32_t b = V->block_order[i];

        for (uint32_t p = V->blocks[b].first_parameter; p; p = V->insns[p].next)
            if (V->forward[p] == p)
                VIRBUILD_FOR_EACH_OPERAND(V, p, o,
                                          cnt[virbuild_value_resolve(V, *o)]++);
        for (uint32_t q = V->blocks[b].first_insn; q; q = V->insns[q].next)
            if (virbuild_value_resolve(V, q) == q &&
                !virbuild_promote_store_is_dead(V, q))
                VIRBUILD_FOR_EACH_OPERAND(V, q, o,
                                          cnt[virbuild_value_resolve(V, *o)]++);
        if (V->blocks[b].terminator)
            VIRBUILD_FOR_EACH_OPERAND(V, V->blocks[b].terminator, o,
                                      cnt[virbuild_value_resolve(V, *o)]++);
    }
    memcpy(left, cnt,
           (size_t)ncnt *
               sizeof *cnt); /* the uses a pointer addition has left */
    for (uint32_t i = 0; i < V->block_order_count; i++) {
        uint32_t b = V->block_order[i], prev = 0, q, next;

        for (q = V->blocks[b].first_insn; q; prev = q, q = next) {
            uint32_t op = V->insns[q].insn.op, p, base, off, clone;
            int64_t k;

            next = V->insns[q].next;
            if ((op != VIR_OP_LOAD && op != VIR_OP_STORE) ||
                virbuild_value_resolve(V, q) != q ||
                virbuild_promote_store_is_dead(V, q))
                continue;
            p = virbuild_value_resolve(V, V->insns[q].insn.a);
            if (p >= ncnt - 1 || cnt[p] < 2 ||
                V->insns[p].insn.op != VIR_OP_PTRADD ||
                V->insns[p].block == VIRBUILD_BLOCK_NONE)
                continue;
            base = virbuild_value_resolve(V, V->insns[p].insn.a);
            off = virbuild_value_resolve(V, V->insns[p].insn.b);
            if (V->insns[base].insn.op == VIR_OP_SLOTADDR ||
                !virbuild_value_integer_constant(V, off, &k) || k != (int32_t)k)
                continue;
            clone = virbuild_preir_insn_add(e, VIR_OP_PTRADD, VIR_TYPE_PTR,
                                            V->insns[p].insn.flags, base, off,
                                            0, b);
            V->insns[clone].next = q;
            if (prev)
                V->insns[prev].next = clone;
            else
                V->blocks[b].first_insn = clone;
            V->insns[q].insn.a = clone;
            left[p]--;
            prev = clone;
            any = true;
        }
    }
    /* a pointer addition all of whose uses were cloned has none: out of its block */
    for (uint32_t i = 0; any && i < V->block_order_count; i++) {
        uint32_t b = V->block_order[i], prev = 0, q, next;

        for (q = V->blocks[b].first_insn; q; q = next) {
            next = V->insns[q].next;
            if (q < ncnt - 1 && cnt[q] >= 2 && left[q] == 0 &&
                V->insns[q].insn.op == VIR_OP_PTRADD) {
                if (prev)
                    V->insns[prev].next = next;
                else
                    V->blocks[b].first_insn = next;
                if (V->blocks[b].last_insn == q)
                    V->blocks[b].last_insn = prev;
            } else {
                prev = q;
            }
        }
    }
    return any;
}
