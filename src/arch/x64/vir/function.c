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
	vx *L = ctx;

	(void)b;
	L->cmp = UINT32_MAX;
	L->memv = L->memskip = L->scaledv = L->scaledskip = L->bev = L->testv = L->cmpmemv = 0;
}

static void vx_target_block_enter(void *ctx, uint32_t b)
{
	vx *L = ctx;

	L->at = b;
	L->fallok = true;
}

static void vx_target_instruction(void *ctx, uint32_t b, uint32_t i, const uint8_t *opr, uint32_t nopr, uint8_t res)
{
	vx_ins(ctx, b, i, opr, nopr, res);
}

static void vx_target_result_spill(void *ctx, uint8_t res, uint32_t slot)
{
	vx_memslot(ctx, false, res, slot);
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
	x64_label(((vx *)ctx)->e, label);
}

/* the edge blocks, all after the last block: a label, the edge's moves, a jump */
static void vx_target_after_blocks(void *ctx)
{
	vx *L = ctx;
	const vir_alloc_out *o = L->o;

	for (uint32_t g = 0; g < o->nedge; g++)
		if (L->edgelab[g]) {
			const vir_aedge *edge = &o->edge[g];

			virbuild_lower_edge_block(&L->drv, L->edgelab[g], g);
			L->fallok = false;
			vx_goto(L, edge->from, edge->to);
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
	.rotation_cached = false,
};

VPRIV bool vw_x64(ecx *e, uint32_t sym, const char **why)
{
	vwst *V = e->vw;
	vir_fn *f = &V->f;
	/* Windows (PLAN-win64.md): the Microsoft convention, rsi, rdi and xmm6-xmm15 callee-saved
	   too, and always a frame pointer (print.c's Windows frame; the unwinder reads rbp) */
	bool ms = WIN64(e->c);
	bool spframe=e->E->o->omit_frame_pointer && !e->E->noopt && !ms;
	bool r10=true;
	uint64_t cs = (1u << RBX) | (1u << R12) | (1u << R13) | (1u << R14) | (1u << R15);
	if(spframe)cs|=1u<<RBP;
	if (ms) cs |= (1u << RSI) | (1u << RDI);
	vx L = { .e = e, .f = f, .o = &V->ao, .ms = ms };
	vir_regs regs = { .alloc = { (X_TEMPS & ~(1u << R10)) | cs, (1u << 14) - 1 },
		.callee = { cs, ms ? ((1u << 14) - 1) & ~((1u << 6) - 1) : 0 }, .fixed = vx_fixed, .ctx=&L };
	*why = VIRFAIL(x64_unsupported_target_option);
	if (e->E->o->debug || e->E->o->sprot || x64_nosimd(e)) return false;
	const attrs *attrs = sym_attrs(e->c, sym);
	if (attrs && attrs->interrupt) { *why = VIRFAIL(x64_interrupt_function); return false; }
	for (uint32_t i = 1; i < f->nconst; i++)
		if (f->ins[i].op != VO_ICONST && f->ins[i].op != VO_FCONST &&
		    f->ins[i].op != VO_SLOTADDR && f->ins[i].op != VO_SYMADDR) { *why = VIRFAIL(x64_constant_kind); return false; }
	for (uint32_t i = 1; i < f->nconst; i++)
		if ((f->ins[i].op == VO_SYMADDR || f->ins[i].op == VO_SLOTADDR) &&
		    (vx_bits(&f->ins[i]) < INT32_MIN / 2 || vx_bits(&f->ins[i]) > INT32_MAX / 2)) { *why = VIRFAIL(x64_constant_range); return false; }
	for (uint32_t i = f->nconst; i < f->nins; i++)
		if (f->ins[i].op == VO_CALL && f->extra[f->ins[i].c] >= VIR_FIX_N) {
			*why = VIRFAIL(x64_too_many_call_arguments);
			return false;
		} else if (f->ins[i].op == VO_CALL && virbuild_call_returns_twice(e, &f->ins[f->ins[i].a])) {
			*why = VIRFAIL(x64_returns_twice);
			return false;
		} else if (!vx_supported(f, i)) {
			snprintf(V->detail, sizeof V->detail, "%s.%s", vir_optab[f->ins[i].op].name, vir_tyname[f->ins[i].ty]);
			*why = VIRFAIL(x64_unsupported_instruction);
			return false;
		}
	uint64_t framesz = (uint64_t)f->nins * 16; /* worst-case spill slots */
	for (uint32_t i = 0; i < f->nslot; i++) {
		if (f->slot[i].align > 16 || f->slot[i].size > INT32_MAX / 4) { *why = VIRFAIL(x64_slot_size); return false; }
		framesz += f->slot[i].size + 16;
	}
	if (framesz > INT32_MAX / 2) { *why = VIRFAIL(x64_frame_size); return false; }
	L.uses=V->xuses=vx_grow(V->xuses,&V->cxuses,f->nins+1,1);
	vir_uses(f,L.uses);L.r10=r10;
	if(r10)regs.alloc[0]|=1u<<R10;
	if(!vir_loops_build(f,&V->lp)){*why=VIRFAIL(x64_loop_analysis);return false;}
	bool allocated=vir_alloc(f,&V->lp,&regs,NULL,&V->ao),scratch=false;
	if(allocated && r10)for(uint32_t k=0;k<V->ao.nmove;k++) {
		const vir_emove *m=&V->ao.move[k];
		if(m->src==VR_SCR0 || m->dst==VR_SCR0){scratch=true;break;}
	}
	if(r10 && (!allocated || scratch)) {
		/* A cycle may need the allocator's GP scratch at the same time as
		 * a slot-to-slot copy. Keep the second reserve for those functions. */
		r10=L.r10=false;regs.alloc[0]&=~(UINT64_C(1)<<R10);
		allocated=vir_alloc(f,&V->lp,&regs,NULL,&V->ao);
	}
	L.gtmp=r10?R11:R10;
	if (!allocated) {
		*why = VIRFAIL(x64_allocation_failed); snprintf(V->detail, sizeof V->detail, "%s", V->ao.err); return false;
	}
	if (getenv("VCC_VIR_CHECK")) {
		vbuffer msg = { 0 };
		uint32_t n = vir_alloc_check(f, &regs, &V->ao, &msg);
		if (n) fprintf(stderr, "vir x86-64: allocation check: %.*s", (int)msg.n, msg.p ? (char *)msg.p : "");
		free(msg.p);
		if (n) { *why = VIRFAIL(x64_allocation_check_failed); return false; }
	}
	/* No partial function is published before x64_finish; a failed lowering retries the walker. */
	L.edgelab=V->xedgelab=vx_grow(V->xedgelab,&V->cxedgelab,V->ao.nedge+1,sizeof(uint32_t));
	memset(L.edgelab,0,V->ao.nedge*sizeof(uint32_t));
	L.slot = V->xslot = vx_grow(V->xslot, &V->cxslot, f->nslot + 1, sizeof *L.slot);
	L.spill = V->xspill = vx_grow(V->xspill, &V->cxspill, V->ao.nspill + 1, sizeof *L.spill);
	L.label = V->xlabel = vx_grow(V->xlabel, &V->cxlabel, f->nblk + 1, sizeof *L.label);
	e->vtry = true; e->vfail = NULL;
	if (_setjmp(e->bail)) {
		*why = e->vfail ? e->vfail : VIRFAIL(x64_lowering_failed);
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
	for (uint32_t k = 0; k < f->nslot; k++) L.slot[k] = wk_slot(e, f->slot[k].size ?: 1, f->slot[k].align);
	for (uint32_t k = 0; k < V->ao.nspill; k++) L.spill[k] = wk_slot(e, V->ao.ssz[k], V->ao.ssz[k]);
	for (uint32_t b = 0; b < f->nblk; b++) L.label[b] = wk_newlabel(e);
	L.ret = wk_newlabel(e);
	uint32_t *layout=V->xlayout=vx_grow(V->xlayout,&V->cxlayout,5*f->nblk,sizeof(uint32_t));
	L.order=layout;L.position=layout+f->nblk;L.rotation=layout+2*f->nblk;L.chase=layout+3*f->nblk;L.lp=&V->lp;
	L.drv=(virbuild_lowering){ .target=&vx_lowering_target, .ctx=&L, .f=f, .o=&V->ao, .lp=&V->lp,
		.order=L.order, .position=L.position, .rotation=L.rotation, .chase=L.chase, .path=layout+4*f->nblk,
		.block_label=L.label, .optimize=!e->E->noopt };

	for (int r = 0; r < 16; r++) {
		if (V->ao.callee[0] >> r & 1) x64_csused(e, r);
		if (V->ao.callee[1] >> r & 1) x64_csused(e, XMM0 + r);
	}
	virbuild_lower_body(&L.drv);
	x64_label(e, L.ret);
	x64_finish(e, sym);
	e->vtry = false;
	*why = NULL; return true;
}
