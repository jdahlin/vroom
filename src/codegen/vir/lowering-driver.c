/* The lowering driver (lowering-driver.h): the block loop, the allocator's edits, the edge blocks and the layout, for both targets. */
#include "lowering-driver.h"

/* callee k (a symbol's address) returns twice: setjmp headers do not universally attach returns_twice to their declaration */
VPRIV bool virbuild_call_returns_twice(ecx *e, const vir_insn *callee)
{
	cc *c = e->c;
	uint32_t s;

	if (callee->op != VIR_OP_SYMADDR || RKIND(callee->a) != RK_SYM)
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
	vir_function *f = d->f;
	const vir_alloc_out *o = d->o;
	const vir_block *k = &f->blocks[b];
	uint32_t pos = o->block_offsets[b], ei = o->block_offsets[f->block_count + 1 + b], eend = o->block_offsets[f->block_count + 2 + b];
	uint32_t first = k->first + (t->params_in_loop ? 0 : k->parameter_count), last = k->first + k->insn_count;

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
		bool result = x.type != VIR_TYPE_VOID && !(t->skip_multi_operands && x.type == VIR_TYPE_MULTI);

		if (d->skip_at && i == *d->skip_at)
			break;
		virbuild_edits(d, &ei, eend, i);
		vir_operand_iterator_init(&it, f, &x, false);
		if (t->skip_multi_operands) {
			while ((p = vir_operand_iterator_next(&it))) {
				if (f->insns[*p].type == VIR_TYPE_MULTI)
					continue;
				if (n == d->max_operands)
					t->fail(ctx, VIRFAIL(too_many_operands));
				d->operand_value[n] = *p;
				d->operand_reg[n++] = o->operand_registers[pos++];
			}
			if (result)
				res = o->operand_registers[pos++];
			t->instruction(ctx, b, i, d->operand_reg, n, res);
		} else {
			while ((p = vir_operand_iterator_next(&it)))
				n++;
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

/* VCC_VIR_ROT=0 keeps VIR's block order */
static bool virbuild_rotation_wanted(bool cached)
{
	static int once = -1;

	if (!cached || once < 0) {
		const char *s = getenv("VCC_VIR_ROT");
		int v = !(s && *s == '0');

		if (!cached)
			return v;
		once = v;
	}
	return once;
}

VPRIV void virbuild_lower_body(const virbuild_lowering *d)
{
	const virbuild_lowering_target *t = d->target;
	vir_function *f = d->f;

	vir_layout_chases_compute(f, d->o, d->chase, d->path);
	vir_layout_order(f, d->lp, d->chase, virbuild_rotation_wanted(t->rotation_cached) && d->optimize, d->order, d->position, d->rotation);
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
