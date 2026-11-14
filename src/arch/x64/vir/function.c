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

	L->map_block = b;
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
	x64_vir_context *L = ctx;
	vx_mark(L,true,9,b,i,i,res,VIR_REGISTER_NONE,VIR_BLOCK_NONE);
	x64_vir_insn_lower(L, b, i, opr, nopr, res);
	vx_mark(L,false,9,0,0,0,0,0,0);
	L->map_block = b;
	L->map_value = i;
}

static void vx_target_result_spill(void *ctx, uint8_t res, uint32_t slot)
{
	x64_vir_context *L = ctx;
	vx_mark(L,true,8,L->map_block,L->map_value,L->map_value,VIR_REGISTER_NONE,res,VIR_BLOCK_NONE);
	x64_vir_slot_access(L, false, res, slot);
	vx_probe(L,8);
	vx_mark(L,false,8,0,0,0,0,0,0);
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

static void vx_entry(void *ctx,uint32_t b,uint32_t pred,uint32_t v,
		     uint8_t reg,bool stored,const char *decision)
{
	vbuffer_format_append(ctx,"vl-entry b=%u pred=%u v=%u reg=%u stored=%u decision=%s\n",
		b,pred,v,reg,stored,decision);
}

VPRIV bool x64_vir_function_lower(codegen_context *e, uint32_t sym, const char **why)
{
	virbuild_context *V = e->vir_builder;
	vir_function *f = &V->f;
	/* Windows (PLAN-win64.md): the Microsoft convention, rsi, rdi and xmm6-xmm15 callee-saved
	   too, and always a frame pointer (print.c's Windows frame; the unwinder reads rbp) */
	bool ms = CODEGEN_IS_WIN64(e->compile_context);
	bool spframe=e->unit->options->omit_frame_pointer && !e->unit->cleanups_disabled && !ms;
	bool r10=true;
	uint64_t cs = (1u << RBX) | (1u << R12) | (1u << R13) | (1u << R14) | (1u << R15);
	if(spframe)cs|=1u<<RBP;
	if (ms) cs |= (1u << RSI) | (1u << RDI);
	x64_vir_context L = { .context = e, .function = f, .allocation = &V->allocation, .ms = ms };
	vir_target_registers regs = { .alloc = { (X64_TEMPORARIES & ~(1u << R10)) | cs, (1u << 14) - 1 },
		.callee = { cs, ms ? ((1u << 14) - 1) & ~((1u << 6) - 1) : 0 }, .fixed = x64_vir_fixed_registers_get, .context=&L };
	*why = VIRBUILD_FAILURE(x64_unsupported_target_option);
	if (e->unit->options->sprot || x64_target_has_no_simd(e)) return false;
	const attrs *attrs = codegen_symbol_attributes(e->compile_context, sym);
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
	/* -fvir-map, -fvir-probe, -fvir-map-function (doc/OPTIMIZATION.md) */
	const cc_vir_aids *aid=&e->unit->options->vir_aid;
	const vident *name=&vintern_ents[e->compile_context->syms[sym].name];
	bool selected=!aid->map_function || (strlen(aid->map_function)==name->len && !memcmp(aid->map_function,name->s,name->len));
	L.map=selected && aid->map;
	L.probe=selected?(unsigned)aid->probe:0;
	vbuffer entries={0};
	vir_alloc_options diagnostic={.entry=vx_entry,.entry_context=&entries};
	const vir_alloc_options *dopt=L.map?&diagnostic:NULL;
	bool allocated=vir_alloc(f,&V->loops,&regs,dopt,&V->allocation),scratch=false;
	if(allocated && r10)for(uint32_t k=0;k<V->allocation.move_count;k++) {
		const vir_edge_move *m=&V->allocation.move[k];
		if(m->src==VIR_REGISTER_SCRATCH0 || m->dst==VIR_REGISTER_SCRATCH0){scratch=true;break;}
	}
	if(r10 && (!allocated || scratch)) {
		/* A cycle may need the allocator's GP scratch at the same time as
		 * a slot-to-slot copy. Keep the second reserve for those functions. */
		r10=L.scratch_register10=false;regs.alloc[0]&=~(UINT64_C(1)<<R10);
		entries.n=0;
		allocated=vir_alloc(f,&V->loops,&regs,dopt,&V->allocation);
	}
	if(L.map && allocated) {
		const vir_alloc_statistics *s=&V->allocation.statistics;
		fprintf(stderr,"vl-function %.*s reloads=%u remats=%u moves=%u stores=%u swaps=%u ereloads=%u eremats=%u emoves=%u estores=%u edges=%u xchg=%u spills=%u slots=%u callee=%u linear=%u pool=%u\n",
			(int)name->len,name->s,s->reloads,s->remats,s->moves,s->stores,s->swaps,s->edge_reloads,s->edge_rematerializations,s->edge_moves,s->edge_stores,s->edges,s->exchanges,s->spills,s->slots,s->callee,s->linear,s->pool);
		if (entries.n) fwrite(entries.p,1,entries.n,stderr);
		for(uint32_t v=1;v<f->insn_count;v++)if(V->allocation.slot[v]!=VIR_BLOCK_NONE)
			fprintf(stderr,"vl-slot v=%u slot=%u sdef=%u\n",v,V->allocation.slot[v],V->allocation.spill_defined[v]);
	}
	free(entries.p);
	L.general_temporary=r10?R11:R10;
	if (!allocated) {
		*why = VIRBUILD_FAILURE(x64_allocation_failed); snprintf(V->failure_detail, sizeof V->failure_detail, "%s", V->allocation.error_message); return false;
	}
	if (e->unit->options->vir_aid.check) {
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
	e->vir_try = true; e->vir_failure_reason = NULL;
	if (_setjmp(e->bail_jump)) {
		*why = e->vir_failure_reason ? e->vir_failure_reason : VIRBUILD_FAILURE(x64_lowering_failed);
		e->vir_try = false;
		return false;
	}
	e->is_reachable = true;
	x64_begin(e, sym);
	e->x64_context->stack_pointer_frame=spframe;
	e->x64_context->interrupt = e->x64_context->local_register_count = 0;
	e->walk_state->statement_depth = e->walk_state->temporary_slot_in_use_count = e->walk_state->temporary_slot_free_count = 0;
	e->walk_state->frame_offset = 0; e->walk_state->frame_alignment = 16; e->walk_state->label_count = 0;
	e->walk_state->stack_pointer_needed = false; e->walk_state->canary = 0;
	for (uint32_t k = 0; k < f->slot_count; k++) L.slot[k] = walk_slot_temporary(e, f->slot[k].size ?: 1, f->slot[k].align);
	for (uint32_t k = 0; k < V->allocation.spill_count; k++) L.spill[k] = walk_slot_temporary(e, V->allocation.slot_sizes[k], V->allocation.slot_sizes[k]);
	for (uint32_t b = 0; b < f->block_count; b++) L.label[b] = walk_label_new(e);
	L.ret = walk_label_new(e);
	uint32_t *layout=V->lowering_layout=x64_vir_array_grow(V->lowering_layout,&V->lowering_layout_capacity,5*f->block_count,sizeof(uint32_t));
	L.order=layout;L.position=layout+f->block_count;L.rotations=layout+2*f->block_count;L.chase=layout+3*f->block_count;L.loops=&V->loops;
	L.drv=(virbuild_lowering){ .target=&vx_lowering_target, .ctx=&L, .f=f, .o=&V->allocation, .lp=&V->loops,
		.order=L.order, .position=L.position, .rotation=L.rotations, .chase=L.chase, .path=layout+4*f->block_count,
		.block_label=L.label, .optimize=!e->unit->cleanups_disabled, .rotate=e->unit->options->optimize.rotate_loops,
		.row=e->unit->options->debug?x64_dbgrow:NULL, .row_e=e };

	for (int r = 0; r < 16; r++) {
		if (V->allocation.callee[0] >> r & 1) x64_callee_save_use(e, r);
		if (V->allocation.callee[1] >> r & 1) x64_callee_save_use(e, XMM0 + r);
	}
	virbuild_lower_body(&L.drv);
	x64_label(e, L.ret);
	x64_finish(e, sym);
	e->vir_try = false;
	*why = NULL; return true;
}
