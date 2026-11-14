/* The lowering driver (lowering-driver.h): the block loop, the allocator's edits, the edge blocks and the layout, for both targets. */
#include "lowering-driver.h"

/* callee k (a symbol's address) returns twice: setjmp headers do not universally attach returns_twice to their declaration */
VPRIV bool virbuild_call_returns_twice(codegen_context *e, const vir_insn *callee)
{
	cc *c = e->compile_context;
	uint32_t s;

	if (callee->op != VIR_OP_SYMADDR || CODEGEN_REFERENCE_KIND(callee->a) != CODEGEN_REFERENCE_SYMBOL)
		return false;
	s = codegen_symbol_canonical(c, CODEGEN_REFERENCE_INDEX(callee->a));
	return (codegen_symbol_attributes(c, s)->flags & CC_ATTRIBUTE_FLAG_TAIL_RETURNS_TWICE) ||
	       CODEGEN_IDENTIFIER_IS(c->syms[s].name, "setjmp") || CODEGEN_IDENTIFIER_IS(c->syms[s].name, "_setjmp") ||
	       CODEGEN_IDENTIFIER_IS(c->syms[s].name, "sigsetjmp") || CODEGEN_IDENTIFIER_IS(c->syms[s].name, "__sigsetjmp");
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
	vir_function *f = d->f;
	const vir_alloc_out *o = d->o;
	const vir_block *k = &f->blocks[b];
	uint32_t pos = o->block_offsets[b], ei = o->block_offsets[f->block_count + 1 + b], eend = o->block_offsets[f->block_count + 2 + b];
	uint32_t first = k->first + (t->params_in_loop ? 0 : k->parameter_count), last = k->first + k->insn_count;
	uint32_t run = 0;	/* -g: the run of f->locations of instruction i (the last that starts at or before it) */

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

			if (o->slot[v] != VIR_BLOCK_NONE && o->spill_defined[v] && r != VIR_REGISTER_NONE)
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
		if (d->row && f->location_count) {	/* (before the edits: they are this instruction's) */
			bool start = i == first;

			while (run + 1 < f->location_count && f->locations[run + 1].first <= i)
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
		if (res != VIR_REGISTER_NONE && o->slot[i] != VIR_BLOCK_NONE && o->spill_defined[i])
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
	vir_function *f = d->f;

	vir_layout_chases_compute(f, d->o, d->chase, d->path);
	vir_layout_order(f, d->lp, d->chase, d->rotate && d->optimize, d->order, d->position, d->rotation);
	if (t->order_adjust)
		t->order_adjust(d->ctx);
	if (t->body_begin)
		t->body_begin(d->ctx);
	for (uint32_t p = 0; p < f->block_count; p++) {
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
