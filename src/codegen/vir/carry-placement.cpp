/*
 * Carry placement: the compare that asks whether an unsigned addition wrapped, or whether an
 * unsigned subtraction borrowed, moves to right behind that addition or subtraction.
 *
 *     s = add x, y                 s = add x, y
 *     t = add s, c                 k = icmp ult s, x     ; the carry of s
 *     k = icmp ult s, x       =>   t = add s, c
 *     l = icmp ult t, s            l = icmp ult t, s     ; the carry of t
 *
 * The lowerings read such a compare from the flags of the instruction in front of it (adds and
 * cset on arm64, add and setb on x86-64: arch/<cpu>/vir), which takes the two to be neighbours.
 * A multi-word addition written in C, and the C bodies of __builtin_addc and __builtin_subc
 * (frontend/c/builtin-body.cpp), compute the sums first and the carries after.
 *
 * The carry of s = add x, y is `s <u x` or `s <u y` (and the same turned around or negated);
 * the borrow of d = sub x, y is `x <u y`.  Moving the compare up is always possible: it reads
 * the addition and one of its operands, or the subtraction's two operands, and has no effect.
 * It stays where it is when the instruction behind it already reads it from the flags (a branch,
 * a select, the and/or of a chain of compares): moving it would trade one fusion for another.
 *
 * -fno-carry-flags leaves the compares where they are, and the lowerings then compare.
 */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
#include "common/literal.h"
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "simplify.h"

enum {
    CARRY_WINDOW = 8
}; /* instructions behind an addition searched for its carry */

/* q, a live icmp, is the carry of the addition p or the borrow of the subtraction p
   (vir_carry_compare_kind says which shapes; the lowerings use the same test) */
static bool virbuild_compare_is_carry_of(virbuild_context *V, uint32_t q,
                                         uint32_t p)
{
    const vir_insn *k = &V->insns[q].insn, *m = &V->insns[p].insn;
    uint32_t a = virbuild_value_resolve(V, k->a),
             b = virbuild_value_resolve(V, k->b),
             x = virbuild_value_resolve(V, m->a),
             y = virbuild_value_resolve(V, m->b);

    return vir_carry_compare_kind(m->op, p, x, y, k->flags & 15, a, b) !=
           VIR_CARRY_NONE;
}

/* the instruction behind q reads q as its condition: the lowering fuses the two already */
static bool virbuild_compare_feeds_next(virbuild_context *V, uint32_t b,
                                        uint32_t q)
{
    uint32_t n = V->insns[q].next;
    const vir_insn *in;

    if (!n)
        n = V->blocks[b].terminator;
    if (!n)
        return false;
    in = &V->insns[n].insn;
    switch (in->op) {
    case VIR_OP_BRIF:
    case VIR_OP_SELECT:
        return virbuild_value_resolve(V, in->a) == q;
    case VIR_OP_AND:
    case VIR_OP_OR:
        /* (condition-merge's chains: icmp, icmp, and/or ...) */
        return in->type == VIR_TYPE_I32 &&
               virbuild_value_resolve(V, in->b) == q &&
               V->insns[virbuild_value_resolve(V, in->a)].insn.op ==
                   VIR_OP_ICMP;
    default:
        return false;
    }
}

VPRIV bool virbuild_carries_place(codegen_context *e)
{
    virbuild_context *V = e->vir_builder;
    bool any = false;

    if (!e->unit->options->optimize.carry_flags)
        return false;
    for (uint32_t i = 0; i < V->block_order_count; i++) {
        uint32_t b = V->block_order[i];

        for (uint32_t p = V->blocks[b].first_insn; p; p = V->insns[p].next) {
            const vir_insn *m = &V->insns[p].insn;
            uint32_t prev = p, q;

            if ((m->op != VIR_OP_ADD && m->op != VIR_OP_SUB) ||
                (m->type != VIR_TYPE_I32 && m->type != VIR_TYPE_I64) ||
                virbuild_value_resolve(V, p) != p)
                continue;
            q = V->insns[p].next;
            for (int n = 0; q && n < CARRY_WINDOW;
                 n++, prev = q, q = V->insns[q].next) {
                if (V->insns[q].insn.op != VIR_OP_ICMP ||
                    virbuild_value_resolve(V, q) != q ||
                    !virbuild_compare_is_carry_of(V, q, p))
                    continue;
                if (prev != p && !virbuild_compare_feeds_next(V, b, q)) {
                    /* out of its place, in behind p */
                    V->insns[prev].next = V->insns[q].next;
                    if (V->blocks[b].last_insn == q)
                        V->blocks[b].last_insn = prev;
                    V->insns[q].next = V->insns[p].next;
                    V->insns[p].next = q;
                    any = true;
                }
                break;
            }
        }
    }
    return any;
}
