/* The lowering driver (lowering-driver.h): the block loop, the allocator's edits, the edge blocks and the layout, for both targets. */
#include "lowering-driver.h"

/* callee k (a symbol's address) returns twice: setjmp headers do not universally attach returns_twice to their declaration */
VPRIV bool virbuild_call_returns_twice(ecx *e, const vir_ins *callee)
{
	cc *c = e->c;
	uint32_t s;

	if (callee->op != VO_SYMADDR || RKIND(callee->a) != RK_SYM)
		return false;
	s = canon_sym(c, RIDX(callee->a));
	return (sym_attrs(c, s)->flags & AF_TAIL_RETURNS_TWICE) ||
	       EM_IDENT_IS(c->syms[s].name, "setjmp") || EM_IDENT_IS(c->syms[s].name, "_setjmp") ||
	       EM_IDENT_IS(c->syms[s].name, "sigsetjmp") || EM_IDENT_IS(c->syms[s].name, "__sigsetjmp");
}

/* the edits at position pos, from *ei on */
static void virbuild_edits(const virbuild_lowering *d, uint32_t *ei, uint32_t end, uint32_t pos)
{
	const vir_alloc_out *o = d->o;

	for (; *ei < end && o->edit[*ei].pos == pos; ++*ei)
		d->target->edit(d->ctx, &o->edit[*ei]);
}

VPRIV void virbuild_lower_edge_block(const virbuild_lowering *d, uint32_t label, uint32_t g)
{
	d->target->label(d->ctx, label);
	d->target->edge_moves(d->ctx, g);
}

VPRIV void virbuild_lower_block(const virbuild_lowering *d, uint32_t b)
{
	const virbuild_lowering_target *t = d->target;
	void *ctx = d->ctx;
	vir_fn *f = d->f;
	const vir_alloc_out *o = d->o;
	const vir_blk *k = &f->blk[b];
	uint32_t pos = o->boff[b], ei = o->boff[f->nblk + 1 + b], eend = o->boff[f->nblk + 2 + b];
	uint32_t first = k->first + (t->params_in_loop ? 0 : k->nparam), last = k->first + k->n;
	uint32_t run = 0;	/* -g: the run of f->loc of instruction i (the last that starts at or before it) */

	if (d->row && f->nloc) {
		uint32_t lo = 0, hi = f->nloc;

		while (hi - lo > 1) {
			uint32_t m = lo + (hi - lo) / 2;

			if (f->loc[m].first <= first)
				lo = m;
			else
				hi = m;
		}
		run = lo;
	}

	t->block_begin(ctx, b);
	if (!t->params_in_loop)
		for (uint32_t q = 0; q < k->nparam; q++) {
			uint8_t r = o->opr[pos++];
			uint32_t v = k->first + q;

			if (o->slot[v] != VIR_NOBLK && o->sdef[v] && r != VR_NONE)
				t->result_spill(ctx, r, o->slot[v]);
		}
	for (uint32_t i = first; i < last; i++) {
		vir_ins x = f->ins[i];
		vir_oit it;
		uint32_t *p, n = 0;
		uint8_t res = VR_NONE;
		bool result = x.ty != VT_VOID && !(t->skip_multi_operands && x.ty == VT_MULTI);

		if (d->skip_at && i == *d->skip_at)
			break;
		if (d->row && f->nloc) {	/* (before the edits: they are this instruction's) */
			bool start = i == first;

			while (run + 1 < f->nloc && f->loc[run + 1].first <= i)
				run++, start = true;
			if (start && f->loc[run].first <= i)
				d->row(d->row_e, f->loc[run].loc);
		}
		virbuild_edits(d, &ei, eend, i);
		vir_oit_init(&it, f, &x, false);
		if (t->skip_multi_operands) {
			while ((p = vir_oit_next(&it))) {
				if (f->ins[*p].ty == VT_MULTI)
					continue;
				if (n == d->max_operands)
					t->fail(ctx, VIRFAIL(too_many_operands));
				d->operand_value[n] = *p;
				d->operand_reg[n++] = o->opr[pos++];
			}
			if (result)
				res = o->opr[pos++];
			t->instruction(ctx, b, i, d->operand_reg, n, res);
		} else {
			while ((p = vir_oit_next(&it)))
				n++;
			if (result)
				res = o->opr[pos + n];
			t->instruction(ctx, b, i, &o->opr[pos], n, res);
			pos += n + result;
		}
		if (res != VR_NONE && o->slot[i] != VIR_NOBLK && o->sdef[i])
			t->result_spill(ctx, res, o->slot[i]);
	}
	if (t->edits_at_block_end)
		virbuild_edits(d, &ei, eend, last);
	if (t->block_end)
		t->block_end(ctx, b);
}

VPRIV void virbuild_lower_body(const virbuild_lowering *d)
{
	const virbuild_lowering_target *t = d->target;
	vir_fn *f = d->f;

	vir_layout_chases(f, d->o, d->chase, d->path);
	vir_layout_order(f, d->lp, d->chase, d->rotate && d->optimize, d->order, d->position, d->rotation);
	if (t->order_adjust)
		t->order_adjust(d->ctx);
	if (t->body_begin)
		t->body_begin(d->ctx);
	for (uint32_t p = 0; p < f->nblk; p++) {
		uint32_t b = d->order[p];

		if (b && d->chase[b] != b)
			continue;	/* (a block every branch goes past: not emitted) */
		t->label(d->ctx, d->block_label[b]);
		t->block_enter(d->ctx, b);
		virbuild_lower_block(d, b);
	}
	if (t->after_blocks)
		t->after_blocks(d->ctx);
}
