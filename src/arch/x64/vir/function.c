/* Allocated VIR to x86-64 records.  Scalar/SSE2 operations and one-piece SysV calls.
 * Unusual ABI groups and unsupported vector operations fall back per function.
 * r11/xmm15 break edge cycles; xmm14 is a second FP scratch. R10 can be
 * allocated when the remaining GP scratch suffices for this function's edges.
 * Keep vector instructions in opaque records until the record vocabulary has SIMD ops.
 */

#include "lowering-internal.h"

/* The callbacks of the shared driver (codegen/vir/lowering-driver.h) */

static void vx_target_block_begin(void *ctx, uint32_t b)
{
	x64_vir_context *L = ctx;

	(void)b;
	L->compare_condition = UINT32_MAX;
	L->memory_value = L->memory_skip = L->scaled_value = L->scaled_skip = L->big_endian_value = L->test_value = L->compare_memory_value = 0;
}

static void vx_target_block_enter(void *ctx, uint32_t b)
{
	x64_vir_context *L = ctx;

	L->block_current = b;
	L->may_fall_through = true;
}

static void vx_target_instruction(void *ctx, uint32_t b, uint32_t i, const uint8_t *opr, uint32_t nopr, uint8_t res)
{
	x64_vir_insn_lower(ctx, b, i, opr, nopr, res);
}

static void vx_target_result_spill(void *ctx, uint8_t res, uint32_t slot)
{
	x64_vir_slot_access(ctx, false, res, slot);
}

static void vx_target_edit(void *ctx, const vir_edit *x)
{
	vx_edit(ctx, x);
}

static void vx_target_edge_moves(void *ctx, uint32_t g)
{
	vx_edge_moves(ctx, g);
}

static void vx_target_label(void *ctx, uint32_t label)
{
	x64_label(((x64_vir_context *)ctx)->context, label);
}

/* the edge blocks, all after the last block: a label, the edge's moves, a jump */
static void vx_target_after_blocks(void *ctx)
{
	x64_vir_context *L = ctx;
	const vir_alloc_out *o = L->allocation;

	for (uint32_t g = 0; g < o->edge_count; g++)
		if (L->edge_labels[g]) {
			const vir_alloc_edge *edge = &o->edge[g];

			virbuild_lower_edge_block(&L->drv, L->edge_labels[g], g);
			L->may_fall_through = false;
			x64_vir_goto_emit(L, edge->from, edge->to);
		}
}

static const virbuild_lowering_target vx_lowering_target = {
	.block_begin = vx_target_block_begin,
	.block_enter = vx_target_block_enter,
	.instruction = vx_target_instruction,
	.result_spill = vx_target_result_spill,
	.edit = vx_target_edit,
	.edge_moves = vx_target_edge_moves,
	.label = vx_target_label,
	.after_blocks = vx_target_after_blocks,
	/* unresolved differences from arm64 (Q-VIRB-02) */
	.params_in_loop = true,
	.edits_at_block_end = true,
	.skip_multi_operands = false,
};

VPRIV bool x64_vir_function_lower(ecx *e, uint32_t sym, const char **why)
{
	virbuild_context *V = e->vw;
	vir_function *f = &V->f;
	bool spframe=e->E->o->omit_frame_pointer && !e->E->noopt;
	bool r10=true;
	uint64_t cs = (1u << RBX) | (1u << R12) | (1u << R13) | (1u << R14) | (1u << R15);
	if(spframe)cs|=1u<<RBP;
	x64_vir_context L = { .context = e, .function = f, .allocation = &V->allocation };
	vir_target_registers regs = { .alloc = { (X_TEMPS & ~(1u << R10)) | cs, (1u << 14) - 1 },
		.callee = { cs, 0 }, .fixed = x64_vir_fixed_registers_get, .context=&L };
	*why = VIRBUILD_FAILURE(x64_unsupported_target_option);
	if (e->E->o->sprot || x64_nosimd(e)) return false;
	const attrs *attrs = sym_attrs(e->c, sym);
	if (attrs && attrs->interrupt) { *why = VIRBUILD_FAILURE(x64_interrupt_function); return false; }
	for (uint32_t i = 1; i < f->constant_count; i++)
		if (f->insns[i].op != VIR_OP_ICONST && f->insns[i].op != VIR_OP_FCONST &&
		    f->insns[i].op != VIR_OP_SLOTADDR && f->insns[i].op != VIR_OP_SYMADDR) { *why = VIRBUILD_FAILURE(x64_constant_kind); return false; }
	for (uint32_t i = 1; i < f->constant_count; i++)
		if ((f->insns[i].op == VIR_OP_SYMADDR || f->insns[i].op == VIR_OP_SLOTADDR) &&
		    (x64_vir_constant_bits(&f->insns[i]) < INT32_MIN / 2 || x64_vir_constant_bits(&f->insns[i]) > INT32_MAX / 2)) { *why = VIRBUILD_FAILURE(x64_constant_range); return false; }
	for (uint32_t i = f->constant_count; i < f->insn_count; i++)
		if (f->insns[i].op == VIR_OP_CALL && f->extra[f->insns[i].c] >= VIR_REGISTER_CONSTRAINT_MAX) {
			*why = VIRBUILD_FAILURE(x64_too_many_call_arguments);
			return false;
		} else if (f->insns[i].op == VIR_OP_CALL && virbuild_call_returns_twice(e, &f->insns[f->insns[i].a])) {
			*why = VIRBUILD_FAILURE(x64_returns_twice);
			return false;
		} else if (!x64_vir_value_is_supported(f, i)) {
			snprintf(V->failure_detail, sizeof V->failure_detail, "%s.%s", vir_opcode_table[f->insns[i].op].name, vir_type_names[f->insns[i].type]);
			*why = VIRBUILD_FAILURE(x64_unsupported_instruction);
			return false;
		}
	uint64_t framesz = (uint64_t)f->insn_count * 16; /* worst-case spill slots */
	for (uint32_t i = 0; i < f->slot_count; i++) {
		if (f->slot[i].align > 16 || f->slot[i].size > INT32_MAX / 4) { *why = VIRBUILD_FAILURE(x64_slot_size); return false; }
		framesz += f->slot[i].size + 16;
	}
	if (framesz > INT32_MAX / 2) { *why = VIRBUILD_FAILURE(x64_frame_size); return false; }
	L.uses=V->lowering_uses=x64_vir_array_grow(V->lowering_uses,&V->lowering_uses_capacity,f->insn_count+1,1);
	vir_uses_count(f,L.uses);L.scratch_register10=r10;
	if(r10)regs.alloc[0]|=1u<<R10;
	if(!vir_loops_build(f,&V->loops)){*why=VIRBUILD_FAILURE(x64_loop_analysis);return false;}
	bool allocated=vir_alloc(f,&V->loops,&regs,NULL,&V->allocation),scratch=false;
	if(allocated && r10)for(uint32_t k=0;k<V->allocation.move_count;k++) {
		const vir_edge_move *m=&V->allocation.move[k];
		if(m->src==VIR_REGISTER_SCRATCH0 || m->dst==VIR_REGISTER_SCRATCH0){scratch=true;break;}
	}
	if(r10 && (!allocated || scratch)) {
		/* A cycle may need the allocator's GP scratch at the same time as
		 * a slot-to-slot copy. Keep the second reserve for those functions. */
		r10=L.scratch_register10=false;regs.alloc[0]&=~(UINT64_C(1)<<R10);
		allocated=vir_alloc(f,&V->loops,&regs,NULL,&V->allocation);
	}
	L.general_temporary=r10?R11:R10;
	if (!allocated) {
		*why = VIRBUILD_FAILURE(x64_allocation_failed); snprintf(V->failure_detail, sizeof V->failure_detail, "%s", V->allocation.error_message); return false;
	}
	if (e->E->o->vir_aid.check) {
		vbuffer msg = { 0 };
		uint32_t n = vir_alloc_check(f, &regs, &V->allocation, &msg);
		if (n) fprintf(stderr, "vir x86-64: allocation check: %.*s", (int)msg.n, msg.p ? (char *)msg.p : "");
		free(msg.p);
		if (n) { *why = VIRBUILD_FAILURE(x64_allocation_check_failed); return false; }
	}
	/* No partial function is published before x64_finish; a failed lowering retries the walker. */
	L.edge_labels=V->lowering_edge_labels=x64_vir_array_grow(V->lowering_edge_labels,&V->lowering_edge_label_capacity,V->allocation.edge_count+1,sizeof(uint32_t));
	memset(L.edge_labels,0,V->allocation.edge_count*sizeof(uint32_t));
	L.slot = V->lowering_slots = x64_vir_array_grow(V->lowering_slots, &V->lowering_slot_capacity, f->slot_count + 1, sizeof *L.slot);
	L.spill = V->lowering_spills = x64_vir_array_grow(V->lowering_spills, &V->lowering_spill_capacity, V->allocation.spill_count + 1, sizeof *L.spill);
	L.label = V->lowering_labels = x64_vir_array_grow(V->lowering_labels, &V->lowering_label_capacity, f->block_count + 1, sizeof *L.label);
	e->vtry = true; e->vfail = NULL;
	if (_setjmp(e->bail)) {
		*why = e->vfail ? e->vfail : VIRBUILD_FAILURE(x64_lowering_failed);
		e->vtry = false;
		return false;
	}
	e->reach = true;
	x64_begin(e, sym);
	e->x64->spframe=spframe;
	e->x64->interrupt = e->x64->nlreg = 0;
	e->wk->sdepth = e->wk->ntu = e->wk->ntf = 0;
	e->wk->foff = 0; e->wk->falign = 16; e->wk->nlabel = 0;
	e->wk->spneed = false; e->wk->canary = 0;
	for (uint32_t k = 0; k < f->slot_count; k++) L.slot[k] = wk_slot(e, f->slot[k].size ?: 1, f->slot[k].align);
	for (uint32_t k = 0; k < V->allocation.spill_count; k++) L.spill[k] = wk_slot(e, V->allocation.slot_sizes[k], V->allocation.slot_sizes[k]);
	for (uint32_t b = 0; b < f->block_count; b++) L.label[b] = wk_newlabel(e);
	L.ret = wk_newlabel(e);
	uint32_t *layout=V->lowering_layout=x64_vir_array_grow(V->lowering_layout,&V->lowering_layout_capacity,5*f->block_count,sizeof(uint32_t));
	L.order=layout;L.position=layout+f->block_count;L.rotations=layout+2*f->block_count;L.chase=layout+3*f->block_count;L.loops=&V->loops;
	L.drv=(virbuild_lowering){ .target=&vx_lowering_target, .ctx=&L, .f=f, .o=&V->allocation, .lp=&V->loops,
		.order=L.order, .position=L.position, .rotation=L.rotations, .chase=L.chase, .path=layout+4*f->block_count,
		.block_label=L.label, .optimize=!e->E->noopt, .rotate=e->E->o->optimize.rotate_loops,
		.row=e->E->o->debug?x64_dbgrow:NULL, .row_e=e };

	for (int r = 0; r < 16; r++)
		if (V->allocation.callee[0] >> r & 1) x64_csused(e, r);
	virbuild_lower_body(&L.drv);
	x64_label(e, L.ret);
	x64_finish(e, sym);
	e->vtry = false;
	*why = NULL; return true;
}
