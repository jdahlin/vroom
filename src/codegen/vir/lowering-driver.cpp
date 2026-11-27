/* The lowering driver (lowering-driver.h): the block loop, the allocator's edits, the edge blocks and the layout, for both targets. */
#include "lowering-driver.h"

/* callee k (a symbol's address) returns twice: setjmp headers do not universally attach returns_twice to their declaration */
VPRIV bool virbuild_call_returns_twice(codegen_context *e,
                                       const vir_insn *callee)
{
    cc *c = e->compile_context;
    uint32_t s;

    if (callee->op != VIR_OP_SYMADDR ||
        CODEGEN_REFERENCE_KIND(callee->a) != CODEGEN_REFERENCE_SYMBOL)
        return false;
    s = codegen_symbol_canonical(c, CODEGEN_REFERENCE_INDEX(callee->a));
    return (codegen_symbol_attributes(c, s)->flags &
            CC_ATTRIBUTE_FLAG_TAIL_RETURNS_TWICE) ||
           CODEGEN_IDENTIFIER_IS(c->syms[s].name, "setjmp") ||
           CODEGEN_IDENTIFIER_IS(c->syms[s].name, "_setjmp") ||
           CODEGEN_IDENTIFIER_IS(c->syms[s].name, "sigsetjmp") ||
           CODEGEN_IDENTIFIER_IS(c->syms[s].name, "__sigsetjmp");
}

/* the edits at position pos, from *ei on */
static void virbuild_edits(const virbuild_lowering *d, uint32_t *ei,
                           uint32_t end, uint32_t pos)
{
    const vir_alloc_out *o = d->o;

    for (; *ei < end && o->edit[*ei].pos == pos; ++*ei)
        d->target->edit(d->ctx, &o->edit[*ei]);
}

VPRIV void virbuild_lower_edge_block(const virbuild_lowering *d, uint32_t label,
                                     uint32_t g)
{
    d->target->label(d->ctx, label);
    d->target->edge_moves(d->ctx, g);
}

VPRIV void virbuild_lower_block(const virbuild_lowering *d, uint32_t b)
{
    const virbuild_lowering_target *t = d->target;
    void *ctx = d->ctx;
    vir_function *f = d->f;
    const vir_alloc_out *o = d->o;
    const vir_block *k = &f->blocks[b];
    uint32_t pos = o->block_offsets[b],
             ei = o->block_offsets[f->block_count + 1 + b],
             eend = o->block_offsets[f->block_count + 2 + b];
    uint32_t first = k->first + (t->params_in_loop ? 0 : k->parameter_count),
             last = k->first + k->insn_count;
    uint32_t run =
        0; /* -g: the run of f->locations of instruction i (the last that starts at or before it) */

    if (d->row && f->location_count) {
        uint32_t lo = 0, hi = f->location_count;

        while (hi - lo > 1) {
            uint32_t m = lo + (hi - lo) / 2;

            if (f->locations[m].first <= first)
                lo = m;
            else
                hi = m;
        }
        run = lo;
    }

    t->block_begin(ctx, b);
    if (!t->params_in_loop)
        for (uint32_t q = 0; q < k->parameter_count; q++) {
            uint8_t r = o->operand_registers[pos++];
            uint32_t v = k->first + q;

            if (o->slot[v] != VIR_BLOCK_NONE && o->spill_defined[v] &&
                r != VIR_REGISTER_NONE)
                t->result_spill(ctx, r, o->slot[v]);
        }
    for (uint32_t i = first; i < last; i++) {
        vir_insn x = f->insns[i];
        vir_operand_iterator it;
        uint32_t *p, n = 0;
        uint8_t res = VIR_REGISTER_NONE;
        bool result = x.type != VIR_TYPE_VOID && x.type != VIR_TYPE_MULTI;

        if (d->skip_at && i == *d->skip_at)
            break;
        if (d->row &&
            f->location_count) { /* (before the edits: they are this instruction's) */
            bool start = i == first;

            while (run + 1 < f->location_count &&
                   f->locations[run + 1].first <= i)
                run++, start = true;
            if (start && f->locations[run].first <= i)
                d->row(d->row_e, f->locations[run].source_location);
        }
        virbuild_edits(d, &ei, eend, i);
        vir_operand_iterator_init(&it, f, &x, false);
        if (t->skip_multi_operands) {
            while ((p = vir_operand_iterator_next(&it))) {
                if (f->insns[*p].type == VIR_TYPE_MULTI)
                    continue;
                if (n == d->max_operands)
                    t->fail(ctx, VIRBUILD_FAILURE(too_many_operands));
                d->operand_value[n] = *p;
                d->operand_reg[n++] = o->operand_registers[pos++];
            }
            if (result)
                res = o->operand_registers[pos++];
            t->instruction(ctx, b, i, d->operand_reg, n, res);
        } else {
            while ((p = vir_operand_iterator_next(&it)))
                n += f->insns[*p].type != VIR_TYPE_MULTI;
            if (result)
                res = o->operand_registers[pos + n];
            t->instruction(ctx, b, i, &o->operand_registers[pos], n, res);
            pos += n + result;
        }
        /* (an invoke ends its block: its lowering stores the result, before the jump) */
        if (res != VIR_REGISTER_NONE && o->slot[i] != VIR_BLOCK_NONE &&
            o->spill_defined[i] && x.op != VIR_OP_INVOKE)
            t->result_spill(ctx, res, o->slot[i]);
    }
    if (t->edits_at_block_end)
        virbuild_edits(d, &ei, eend, last);
    if (t->block_end)
        t->block_end(ctx, b);
}

/* whether a block of b's loop that the layout puts at or after b jumps back to it: the top
 * of a loop as laid out (after rotation, not the loop's header) */
static bool virbuild_block_tops_loop(const virbuild_lowering *d, uint32_t b)
{
    const vir_function *f = d->f;
    const vir_block *k = &f->blocks[b];

    if (d->lp->inner[b] == VIR_BLOCK_NONE)
        return false;
    for (uint32_t i = 0; i < k->predecessor_count; i++)
        if (d->position[f->extra[k->predecessor_first + i]] >= d->position[b])
            return true;
    return false;
}

VPRIV void virbuild_lower_body(const virbuild_lowering *d)
{
    const virbuild_lowering_target *t = d->target;
    vir_function *f = d->f;

    vir_layout_chases_compute(f, d->o, d->chase, d->path);
    vir_layout_order(f, d->lp, d->chase, d->rotate && d->optimize, d->order,
                     d->position, d->rotation);
    if (t->order_adjust)
        t->order_adjust(d->ctx);
    if (t->body_begin)
        t->body_begin(d->ctx);
    for (uint32_t p = 0; p < f->block_count; p++) {
        uint32_t b = d->order[p];

        if (b && d->chase[b] != b)
            continue; /* (a block every branch goes past: not emitted) */
        if (t->loop_align && d->optimize && virbuild_block_tops_loop(d, b))
            t->loop_align(d->ctx);
        t->label(d->ctx, d->block_label[b]);
        t->block_enter(d->ctx, b);
        virbuild_lower_block(d, b);
    }
    if (t->after_blocks)
        t->after_blocks(d->ctx);
}

/*
 * Why the function's calls cannot be sibling calls (NULL: those right before a ret can):
 * the callee must not see the frame, so no slot's address may leave the loads, stores
 * and copies that use it (as LLVM's TailCallElim: no escaping alloca).  d (one byte per instruction) may
 * arrive with the instructions already marked that are addresses in the frame.
 */
VPRIV const char *virbuild_tail_call_reason(codegen_context *e,
                                            const vir_function *f, uint8_t *d)
{
    bool ch = true;

    while (ch) {
        ch = false;
        for (uint32_t b = 0; b < f->block_count; b++)
            for (uint32_t i = f->blocks[b].first;
                 i < f->blocks[b].first + f->blocks[b].insn_count; i++) {
                vir_insn x = f->insns[i];
                vir_operand_iterator it;
                uint32_t *p;

                if (x.op == VIR_OP_ASM || x.op == VIR_OP_ASMGOTO ||
                    x.op == VIR_OP_FRAMEADDR || x.op == VIR_OP_RETADDR ||
                    x.op == VIR_OP_VA_START)
                    return "asm or the frame's address";
                if (vir_op_is_call(x.op) &&
                    virbuild_call_returns_twice(e, &f->insns[x.a]))
                    return "a call that returns twice";
                vir_operand_iterator_init(&it, VUNCONST(vir_function *, f), &x,
                                          false);
                while ((p = vir_operand_iterator_next(&it))) {
                    if (f->insns[*p].op != VIR_OP_SLOTADDR && !d[*p])
                        continue;
                    if (x.op == VIR_OP_PTRADD && it.pos == 0) {
                        ch |= !d[i];
                        d[i] = 1;
                    } else if (!(((x.op == VIR_OP_LOAD ||
                                   x.op == VIR_OP_STORE ||
                                   x.op == VIR_OP_MEMSET) &&
                                  it.pos == 0) ||
                                 (x.op == VIR_OP_MEMCPY && it.pos <= 1))) {
                        return "a slot's address escapes";
                    }
                }
            }
    }
    return NULL;
}

/* from j in block k: past the instructions that only pass *res on (i2p, p2i; under ret32
 * trunc to i32 and ext.4: the low half is the result, the upper half unspecified), *res
 * following them; the first other instruction */
static uint32_t virbuild_pass_through_skip(const vir_function *f, bool ret32,
                                           const vir_block *k, uint32_t j,
                                           uint32_t *res)
{

    for (; j < k->first + k->insn_count && f->insns[j].a == *res; j++) {
        const vir_insn *x = &f->insns[j];

        if (!(x->op == VIR_OP_I2P || x->op == VIR_OP_P2I ||
              (ret32 && ((x->op == VIR_OP_TRUNC && x->type == VIR_TYPE_I32) ||
                         (x->op == VIR_OP_EXT &&
                          VIR_INSN_FLAG_WIDTH_LOG2(x->flags) == 2)))))
            break;
        *res = j;
    }
    return j;
}

/* v without the instructions that only pass a value on (virbuild_pass_through_skip's): a reader may name the
 * value under them, since the lowering reads through i2p and p2i */
static uint32_t virbuild_pass_through_value(const vir_function *f, bool ret32,
                                            uint32_t v)
{
    for (;;) {
        const vir_insn *x = &f->insns[v];

        if (!(x->op == VIR_OP_I2P || x->op == VIR_OP_P2I ||
              (ret32 && ((x->op == VIR_OP_TRUNC && x->type == VIR_TYPE_I32) ||
                         (x->op == VIR_OP_EXT &&
                          VIR_INSN_FLAG_WIDTH_LOG2(x->flags) == 2)))))
            return v;
        v = x->a;
    }
}

/* the call at i, in b, is a sibling call: what the ret after it returns is its result (or
 * nothing), also through brs to blocks of only what passes the result on */
VPRIV bool virbuild_call_is_sibling(codegen_context *e, const vir_function *f,
                                    bool ret32, bool tail, uint32_t b,
                                    uint32_t i)
{
    const vir_block *k = &f->blocks[b];
    const vir_insn *r, *t = &f->insns[f->insns[i].a];
    uint32_t res = i, j;

    if (f->insns[i].op ==
        VIR_OP_INVOKE) /* (its frame must stay for the landing pad) */
        return false;
    if (!tail || f->insns[i].type == VIR_TYPE_MULTI ||
        (t->op == VIR_OP_SYMADDR &&
         CODEGEN_REFERENCE_KIND(t->a) == CODEGEN_REFERENCE_SYMBOL &&
         !em_tail_target_ok(
             e, codegen_symbol_canonical(e->compile_context,
                                         CODEGEN_REFERENCE_INDEX(t->a)))))
        return false;
    j = virbuild_pass_through_skip(f, ret32, k, i + 1, &res);
    for (int hop = 0;; hop++) {
        uint32_t q;

        if (j >= k->first + k->insn_count)
            return false;
        r = &f->insns[j];
        if (r->op != VIR_OP_BR || hop == 4)
            break;
        k = &f->blocks[r->a];
        for (q = 0; q < k->parameter_count &&
                    virbuild_pass_through_value(f, ret32, f->extra[r->c + q]) !=
                        virbuild_pass_through_value(f, ret32, res);
             q++)
            ;
        if (q < k->parameter_count)
            res = k->first + q; /* (else the ret may name the value itself) */
        j = virbuild_pass_through_skip(f, ret32, k,
                                       k->first + k->parameter_count, &res);
    }
    if (r->op != VIR_OP_RET)
        return false;
    return !f->extra[r->c] ||
           (f->extra[r->c] == 1 &&
            virbuild_pass_through_value(f, ret32, f->extra[r->c + 1]) ==
                virbuild_pass_through_value(f, ret32, res));
}
