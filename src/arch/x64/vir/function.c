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

	L->map_block = b;
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
	vx *L = ctx;
	vx_mark(L,true,9,b,i,i,res,VIR_REGISTER_NONE,VIR_BLOCK_NONE);
	vx_ins(L, b, i, opr, nopr, res);
	vx_mark(L,false,9,0,0,0,0,0,0);
	L->map_block = b;
	L->map_value = i;
}

static void vx_target_result_spill(void *ctx, uint8_t res, uint32_t slot)
{
	vx *L = ctx;
	vx_mark(L,true,8,L->map_block,L->map_value,L->map_value,VIR_REGISTER_NONE,res,VIR_BLOCK_NONE);
	vx_memslot(L, false, res, slot);
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
	x64_label(((vx *)ctx)->e, label);
}

/* the edge blocks, all after the last block: a label, the edge's moves, a jump */
static void vx_target_after_blocks(void *ctx)
{
	vx *L = ctx;
	const vir_alloc_out *o = L->o;

	for (uint32_t g = 0; g < o->edge_count; g++)
		if (L->edgelab[g]) {
			const vir_alloc_edge *edge = &o->edge[g];

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

static void vx_entry(void *ctx,uint32_t b,uint32_t pred,uint32_t v,
		     uint8_t reg,bool stored,const char *decision)
{
	vbuffer_format_append(ctx,"vl-entry b=%u pred=%u v=%u reg=%u stored=%u decision=%s\n",
		b,pred,v,reg,stored,decision);
}

/* The diagnostics' environment variables, read once per process */
static struct { const char *filter, *mapping, *probe; } vx_diag_env;
static pthread_once_t vx_diag_once = PTHREAD_ONCE_INIT;
static void vx_diag_init(void)
{
	vx_diag_env.filter = getenv("VCC_VL_FILTER");
	vx_diag_env.mapping = getenv("VCC_VL_MAP");
	vx_diag_env.probe = getenv("VCC_VL_PROBE");
}

VPRIV bool vw_x64(ecx *e, uint32_t sym, const char **why)
{
	vwst *V = e->vw;
	vir_function *f = &V->f;
	bool spframe=e->E->o->omit_frame_pointer && !e->E->noopt;
	bool r10=true;
	uint64_t cs = (1u << RBX) | (1u << R12) | (1u << R13) | (1u << R14) | (1u << R15);
	if(spframe)cs|=1u<<RBP;
	vx L = { .e = e, .f = f, .o = &V->ao };
	vir_target_registers regs = { .alloc = { (X_TEMPS & ~(1u << R10)) | cs, (1u << 14) - 1 },
		.callee = { cs, 0 }, .fixed = vx_fixed, .context=&L };
	*why = VIRFAIL(x64_unsupported_target_option);
	if (e->E->o->debug || e->E->o->sprot || x64_nosimd(e)) return false;
	const attrs *attrs = sym_attrs(e->c, sym);
	if (attrs && attrs->interrupt) { *why = VIRFAIL(x64_interrupt_function); return false; }
	for (uint32_t i = 1; i < f->constant_count; i++)
		if (f->insns[i].op != VIR_OP_ICONST && f->insns[i].op != VIR_OP_FCONST &&
		    f->insns[i].op != VIR_OP_SLOTADDR && f->insns[i].op != VIR_OP_SYMADDR) { *why = VIRFAIL(x64_constant_kind); return false; }
	for (uint32_t i = 1; i < f->constant_count; i++)
		if ((f->insns[i].op == VIR_OP_SYMADDR || f->insns[i].op == VIR_OP_SLOTADDR) &&
		    (vx_bits(&f->insns[i]) < INT32_MIN / 2 || vx_bits(&f->insns[i]) > INT32_MAX / 2)) { *why = VIRFAIL(x64_constant_range); return false; }
	for (uint32_t i = f->constant_count; i < f->insn_count; i++)
		if (f->insns[i].op == VIR_OP_CALL && f->extra[f->insns[i].c] >= VIR_REGISTER_CONSTRAINT_MAX) {
			*why = VIRFAIL(x64_too_many_call_arguments);
			return false;
		} else if (f->insns[i].op == VIR_OP_CALL && virbuild_call_returns_twice(e, &f->insns[f->insns[i].a])) {
			*why = VIRFAIL(x64_returns_twice);
			return false;
		} else if (!vx_supported(f, i)) {
			snprintf(V->detail, sizeof V->detail, "%s.%s", vir_opcode_table[f->insns[i].op].name, vir_type_names[f->insns[i].type]);
			*why = VIRFAIL(x64_unsupported_instruction);
			return false;
		}
	uint64_t framesz = (uint64_t)f->insn_count * 16; /* worst-case spill slots */
	for (uint32_t i = 0; i < f->slot_count; i++) {
		if (f->slot[i].align > 16 || f->slot[i].size > INT32_MAX / 4) { *why = VIRFAIL(x64_slot_size); return false; }
		framesz += f->slot[i].size + 16;
	}
	if (framesz > INT32_MAX / 2) { *why = VIRFAIL(x64_frame_size); return false; }
	L.uses=V->xuses=vx_grow(V->xuses,&V->cxuses,f->insn_count+1,1);
	vir_uses_count(f,L.uses);L.r10=r10;
	if(r10)regs.alloc[0]|=1u<<R10;
	if(!vir_loops_build(f,&V->lp)){*why=VIRFAIL(x64_loop_analysis);return false;}
	pthread_once(&vx_diag_once,vx_diag_init);
	const vident *name=&vintern_ents[e->c->syms[sym].name];
	bool selected=!vx_diag_env.filter || (strlen(vx_diag_env.filter)==name->len && !memcmp(vx_diag_env.filter,name->s,name->len));
	L.map=selected && vx_diag_env.mapping && *vx_diag_env.mapping && *vx_diag_env.mapping!='0';
	L.probe=selected && vx_diag_env.probe?(unsigned)strtoul(vx_diag_env.probe,NULL,0):0;
	vbuffer entries={0};
	vir_alloc_options diagnostic={.entry=vx_entry,.entry_context=&entries};
	const vir_alloc_options *dopt=L.map?&diagnostic:NULL;
	bool allocated=vir_alloc(f,&V->lp,&regs,dopt,&V->ao),scratch=false;
	if(allocated && r10)for(uint32_t k=0;k<V->ao.move_count;k++) {
		const vir_edge_move *m=&V->ao.move[k];
		if(m->src==VIR_REGISTER_SCRATCH0 || m->dst==VIR_REGISTER_SCRATCH0){scratch=true;break;}
	}
	if(r10 && (!allocated || scratch)) {
		/* A cycle may need the allocator's GP scratch at the same time as
		 * a slot-to-slot copy. Keep the second reserve for those functions. */
		r10=L.r10=false;regs.alloc[0]&=~(UINT64_C(1)<<R10);
		entries.n=0;
		allocated=vir_alloc(f,&V->lp,&regs,dopt,&V->ao);
	}
	if(L.map && allocated) {
		const vir_alloc_statistics *s=&V->ao.statistics;
		fprintf(stderr,"vl-function %.*s reloads=%u remats=%u moves=%u stores=%u swaps=%u ereloads=%u eremats=%u emoves=%u estores=%u edges=%u xchg=%u spills=%u slots=%u callee=%u linear=%u pool=%u\n",
			(int)name->len,name->s,s->reloads,s->remats,s->moves,s->stores,s->swaps,s->edge_reloads,s->edge_rematerializations,s->edge_moves,s->edge_stores,s->edges,s->exchanges,s->spills,s->slots,s->callee,s->linear,s->pool);
		if (entries.n) fwrite(entries.p,1,entries.n,stderr);
		for(uint32_t v=1;v<f->insn_count;v++)if(V->ao.slot[v]!=VIR_BLOCK_NONE)
			fprintf(stderr,"vl-slot v=%u slot=%u sdef=%u\n",v,V->ao.slot[v],V->ao.spill_defined[v]);
	}
	free(entries.p);
	L.gtmp=r10?R11:R10;
	if (!allocated) {
		*why = VIRFAIL(x64_allocation_failed); snprintf(V->detail, sizeof V->detail, "%s", V->ao.error_message); return false;
	}
	if (getenv("VCC_VIR_CHECK")) {
		vbuffer msg = { 0 };
		uint32_t n = vir_alloc_check(f, &regs, &V->ao, &msg);
		if (n) fprintf(stderr, "vir x86-64: allocation check: %.*s", (int)msg.n, msg.p ? (char *)msg.p : "");
		free(msg.p);
		if (n) { *why = VIRFAIL(x64_allocation_check_failed); return false; }
	}
	/* No partial function is published before x64_finish; a failed lowering retries the walker. */
	L.edgelab=V->xedgelab=vx_grow(V->xedgelab,&V->cxedgelab,V->ao.edge_count+1,sizeof(uint32_t));
	memset(L.edgelab,0,V->ao.edge_count*sizeof(uint32_t));
	L.slot = V->xslot = vx_grow(V->xslot, &V->cxslot, f->slot_count + 1, sizeof *L.slot);
	L.spill = V->xspill = vx_grow(V->xspill, &V->cxspill, V->ao.spill_count + 1, sizeof *L.spill);
	L.label = V->xlabel = vx_grow(V->xlabel, &V->cxlabel, f->block_count + 1, sizeof *L.label);
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
	for (uint32_t k = 0; k < f->slot_count; k++) L.slot[k] = wk_slot(e, f->slot[k].size ?: 1, f->slot[k].align);
	for (uint32_t k = 0; k < V->ao.spill_count; k++) L.spill[k] = wk_slot(e, V->ao.slot_sizes[k], V->ao.slot_sizes[k]);
	for (uint32_t b = 0; b < f->block_count; b++) L.label[b] = wk_newlabel(e);
	L.ret = wk_newlabel(e);
	uint32_t *layout=V->xlayout=vx_grow(V->xlayout,&V->cxlayout,5*f->block_count,sizeof(uint32_t));
	L.order=layout;L.position=layout+f->block_count;L.rotation=layout+2*f->block_count;L.chase=layout+3*f->block_count;L.lp=&V->lp;
	L.drv=(virbuild_lowering){ .target=&vx_lowering_target, .ctx=&L, .f=f, .o=&V->ao, .lp=&V->lp,
		.order=L.order, .position=L.position, .rotation=L.rotation, .chase=L.chase, .path=layout+4*f->block_count,
		.block_label=L.label, .optimize=!e->E->noopt };

	for (int r = 0; r < 16; r++)
		if (V->ao.callee[0] >> r & 1) x64_csused(e, r);
	virbuild_lower_body(&L.drv);
	x64_label(e, L.ret);
	x64_finish(e, sym);
	e->vtry = false;
	*why = NULL; return true;
}
