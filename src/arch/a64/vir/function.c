/*
 * Allocated VIR to arm64 records (PLAN-vir.md V4, SPEC-vir.md 15.3): the
 * registers arm64 offers the allocator, the fixed operands of calls,
 * parameters and returns, then one pass over the blocks that writes the
 * emitter's records with the allocated registers, the edits and the edges'
 * moves.  finish_function then runs the record clean-ups, lays out the frame
 * and prints or encodes, as for any function.
 *
 *   integer: x0-x15 and x19-x28 (callee-saved); x16 is VR_SCR0 and the
 *            lowering's own scratch, x17 the printer's (large immediates),
 *            x18 the platform's, x29/x30 the frame
 *   float:   v0-v7 and v16-v30, caller-saved, and v8-v15, whose low 64 bits a
 *            call keeps: offered when no vector lives across a call (vl_vcross)
 *            and saved and restored here, not by the prologue; v31 is VR_SCR1
 *
 * Floating-point operations are encoded words (I_WORD), as in fp.c.  A
 * value in a spill slot or a VIR slot is addressed from FRAME, so the frame
 * layout stays finish_function's.  Edge moves of a branch that is not a `br`
 * go into a small block after the branch's own (a label, the moves, a jump).
 */

#include "lowering-internal.h"

VPRIV _Thread_local struct vlt VLT;

/* The callbacks of the shared driver (codegen/vir/lowering-driver.h) */

static void vl_target_block_begin(void *ctx, uint32_t b)
{
	vl *L = ctx;
	const vir_alloc_out *o = L->o;

	L->fcc = -1, L->fzr = -1, L->fbit = -1;
	L->cchi = 0;
	L->tlo = L->thi = 0;
	L->pval = 0, L->xval = 0, L->xc = VIR_NOBLK;
	L->eb = o->boff[L->f->nblk + 1 + b], L->eend = o->boff[L->f->nblk + 2 + b];
	L->ei = L->eb;
	L->ntr = 0;
	L->skip = 0;
}

/* the label defined by the layout loop: the prologue's nop, and what the code at it may do */
static void vl_target_block_enter(void *ctx, uint32_t b)
{
	vl *L = ctx;

	if (b == L->sp)
		put(L->e, I_NOP)->cond = NOP_PROLOG;
	L->bare = L->sp != VIR_NOBLK && !vir_dominates(L->f, L->sp, b);
	L->at = b;
	L->fallok = true;
}

static void vl_target_instruction(void *ctx, uint32_t b, uint32_t i, const uint8_t *opr, uint32_t nopr, uint8_t res)
{
	vl *L = ctx;
	ecx *e = L->e;

	(void)opr;	/* (the driver filled L->ov and L->opr) */
	L->nop = nopr;
	if (L->fcc >= 0 && L->fval != L->f->ins[i].a && !(i > L->tlo && i < L->thi))
		L->fcc = -1;
	if (!e->reach)
		deflabel_forced(e, newlabel(e));	/* (code after a trap: never reached) */
	vl_ins(L, b, i, res);
}

static void vl_target_result_spill(void *ctx, uint8_t res, uint32_t slot)
{
	vl_spill(ctx, res, slot, R_X16);
}

static void vl_target_edit(void *ctx, const vir_edit *x)
{
	vl_edit(ctx, x);
}

static void vl_target_edge_moves(void *ctx, uint32_t g)
{
	vl_emoves(ctx, g);
}

static void vl_target_label(void *ctx, uint32_t label)
{
	deflabel_forced(((vl *)ctx)->e, label);
}

/* the block's branches' edges with moves: a small block each after it (a label, the moves, a jump) */
static void vl_target_block_end(void *ctx, uint32_t b)
{
	vl *L = ctx;

	if (L->e->reach)
		vl_fail(L, VIRFAIL(lowering_block_without_terminator));
	if (L->ntr) {	/* (a copy in vl_goto reuses L->tr) */
		uint32_t n = L->ntr;
		struct { uint32_t l, e, to; } *tr = __builtin_alloca(n * sizeof *tr);

		memcpy(tr, L->tr, n * sizeof *tr);
		L->ntr = 0;
		bool fo = L->fallok;

		for (uint32_t t = 0; t < n; t++) {
			virbuild_lower_edge_block(&L->drv, tr[t].l, tr[t].e);
			L->fallok = fo && t == n - 1;
			vl_goto(L, b, vl_chase(L, tr[t].to));
		}
		L->fallok = fo;
	}
	L->ntr = 0;
}

static void vl_target_body_begin(void *ctx)
{
	vl_fcsave(ctx, false, R_X16);
}

static void vl_target_after_blocks(void *ctx)
{
	vl *L = ctx;

	for (uint32_t b = 0; b < L->f->nblk && L->sp != VIR_NOBLK; b++)
		if (VLT.lbf[b]) {	/* a return in front of the save point, for the code behind it */
			deflabel_forced(L->e, VLT.lbf[b]);
			L->bare = false;
			L->at = b;
			L->fallok = false;
			virbuild_lower_block(&L->drv, b);
		}
}

static void vl_target_fail(void *ctx, const char *why)
{
	vl_fail(ctx, why);
}

static void vl_target_order_adjust(void *ctx);

static const virbuild_lowering_target vl_lowering_target = {
	.block_begin = vl_target_block_begin,
	.block_enter = vl_target_block_enter,
	.instruction = vl_target_instruction,
	.result_spill = vl_target_result_spill,
	.edit = vl_target_edit,
	.edge_moves = vl_target_edge_moves,
	.label = vl_target_label,
	.block_end = vl_target_block_end,
	.order_adjust = vl_target_order_adjust,
	.body_begin = vl_target_body_begin,
	.after_blocks = vl_target_after_blocks,
	.fail = vl_target_fail,
	/* unresolved differences from x86-64 (Q-VIRB-02) */
	.params_in_loop = false,
	.edits_at_block_end = false,
	.skip_multi_operands = true,
};

/*
 * The layout (L->ord): VIR's block order, except that a loop whose header is a test (a
 * `brif` after a few plain instructions: a while or for loop) has the header after its
 * last block when that block ends in a `br` to it.  The latch then falls into the test and
 * the test's branch goes back to the body: one branch a round instead of two.  The edges
 * into the loop get a copy of the test (vl_goto), not a jump to it: with the jump,
 * speedtest1 ran more instructions than without the rotation (short loops).  VLT.hl[b] is
 * the loop of a header placed so, else VIR_NOBLK.  -fno-rotate-loops: VIR's order.  The driver
 * makes that layout; this puts the blocks in front of the save point first.
 */
static void vl_target_order_adjust(void *ctx)
{
	vl *L = ctx;
	const vir_fn *f = L->f;
	uint32_t n = f->nblk;

	if (L->sp != VIR_NOBLK) {
		/* the blocks in front of the save point first, then it and what it dominates:
		 * the prologue stands between them (VLT.path: scratch here) */
		uint32_t np = 0, nq = 0, *q = VLT.path;

		for (uint32_t p = 0; p < n; p++)
			if (vir_dominates(f, L->sp, L->ord[p]))
				q[nq++] = L->ord[p];
			else
				L->ord[np++] = L->ord[p];
		memcpy(&L->ord[np], q, (size_t)nq * sizeof *q);
		if (vl_skip(L, L->sp) || L->ord[np] != L->sp)
			L->sp = VIR_NOBLK;	/* (never the case in dominator order; the order stays) */
		/* from behind the save point back in front of it only to a block that is a return */
		for (uint32_t p = np; p < n && L->sp != VIR_NOBLK; p++) {
			vir_ins t = f->ins[f->blk[L->ord[p]].first + f->blk[L->ord[p]].n - 1];
			vir_oit it;
			uint32_t *x;

			vir_oit_init(&it, (vir_fn *)f, &t, true);
			while (L->sp != VIR_NOBLK && (x = vir_oit_next(&it))) {
				const vir_blk *k = &f->blk[vl_chase(L, *x)];

				if (!vir_dominates(f, L->sp, vl_chase(L, *x)) &&
				    ((k->fl & VB_ADDR) || k->n - k->nparam != 1 || f->ins[k->first + k->n - 1].op != VO_RET))
					L->sp = VIR_NOBLK;
			}
		}
	}
	for (uint32_t p = 0; p < n; p++)
		L->opos[L->ord[p]] = p;
}

VPRIV void vl_body(vl *L)
{
	virbuild_lower_body(&L->drv);
}

/* does a vector live across a call?  (A call keeps only the low half of v8-v15: such a function
 * is not offered them.)  Liveness of the vector values alone, a bit each */
static bool vl_vcross(vir_fn *f)
{
	uint32_t nv = 0, W, *ix;
	uint64_t *in, *out, *live;
	bool calls = false, hit = false;

	for (uint32_t b = 0; b < f->nblk; b++)
		for (uint32_t i = f->blk[b].first; i < f->blk[b].first + f->blk[b].n; i++) {
			nv += vir_isvec(f->ins[i].ty);
			calls |= f->ins[i].op == VO_CALL || ((f->ins[i].op == VO_MEMCPY || f->ins[i].op == VO_MEMSET) && vl_bigmem(f, &f->ins[i]));
		}
	if (!nv || !calls)
		return false;
	W = (nv + 63) / 64;
	ix = malloc((size_t)f->nins * sizeof *ix);
	in = calloc((size_t)(2 * f->nblk + 1) * W, sizeof *in);
	if (!ix || !in) {
		free(ix), free(in);
		return true;
	}
	out = in + (size_t)f->nblk * W, live = out + (size_t)f->nblk * W;
	nv = 0;
	for (uint32_t i = 0; i < f->nins; i++)
		ix[i] = vir_isvec(f->ins[i].ty) ? nv++ : UINT32_MAX;
	for (int pass = 0, ch = 1; ch || pass < 2; pass += !ch) {	/* to a fixed point, then once more to look at the calls */
		ch = 0;
		for (uint32_t b = f->nblk; b-- > 0;) {
			const vir_blk *k = &f->blk[b];
			vir_ins t = f->ins[k->first + k->n - 1];
			vir_oit it;
			uint32_t *p;

			memset(live, 0, W * sizeof *live);
			vir_oit_init(&it, f, &t, true);
			while ((p = vir_oit_next(&it)))
				if (*p < f->nblk)
					for (uint32_t w = 0; w < W; w++)
						live[w] |= in[(size_t)*p * W + w];
			for (uint32_t w = 0; w < W; w++)
				if (out[(size_t)b * W + w] != live[w])
					out[(size_t)b * W + w] = live[w], ch = 1;
			for (uint32_t i = k->first + k->n; i-- > k->first;) {
				vir_ins x = f->ins[i];

				if (ix[i] != UINT32_MAX)
					live[ix[i] / 64] &= ~(1ull << (ix[i] & 63));
				if (pass && (x.op == VO_CALL || x.op == VO_MEMCPY || x.op == VO_MEMSET))
					for (uint32_t w = 0; w < W; w++)
						hit |= live[w] != 0;
				vir_oit_init(&it, f, &x, false);
				while ((p = vir_oit_next(&it)))
					if (*p < f->nins && ix[*p] != UINT32_MAX)
						live[ix[*p] / 64] |= 1ull << (ix[*p] & 63);
			}
			for (uint32_t w = 0; w < W; w++)
				if (in[(size_t)b * W + w] != live[w])
					in[(size_t)b * W + w] = live[w], ch = 1;
		}
		if (pass && !ch)
			break;
	}
	free(ix), free(in);
	return hit;
}

/* lower V->f (allocated in V->ao) for function s; false: why in *why, nothing emitted */
VPRIV bool vw_a64(ecx *e, uint32_t s, const char **why)
{
	vwst *V = e->vw;
	vir_fn *f = &V->f;
	vir_regs regs;
	vl L = { 0 };
	cc *c = e->c;
	const ctype *ft = T(c, c->syms[s].ty);

	*why = NULL;
	if (e->E->o->debug) {
		*why = VIRFAIL(debug_info);
		return false;
	}
	/* (unwind tables would have to describe the d registers' slots: not written) */
	vl_regs(&regs, DARWIN(c), !e->E->o->cfi && !vl_vcross(f));
	if (!vir_loops_build(f, &V->lp)) {
		*why = VIRFAIL(loop_analysis);
		return false;
	}
	/* i2p and p2i are copies here: their users read the operand (a load's address an add,
	 * to fold), and they go unemitted */
	for (uint32_t b = 0; b < f->nblk; b++)
		for (uint32_t i = f->blk[b].first; i < f->blk[b].first + f->blk[b].n; i++) {
			vir_oit it;
			uint32_t *p;

			vir_oit_init(&it, f, &f->ins[i], false);
			while ((p = vir_oit_next(&it)))
				while (f->ins[*p].op == VO_I2P || f->ins[*p].op == VO_P2I)
					*p = f->ins[*p].a;
		}
	/* shrink-wrapping (SPEC-vir.md 15.12): the allocator is told which calls will be sibling
	 * calls, which need no frame.  Not with unwind tables (the rows would have to follow the
	 * prologue into the body).  -fno-shrink-wrap: off */
	{
		vir_aopt ao = { 0 };

		L.e = e, L.f = f;
		VLT.lo32 = vl_grow(VLT.lo32, &VLT.c32, f->nins + 1, 1);
		VLT.nf = vl_grow(VLT.nf, &VLT.cnf, f->nins + 1, 1);
		L.ret32 = KIND(c, ft->base) != TY_VOID && is_integer(c, ft->base) && tsize(c, ft->base) == 4;
		L.tail = !(ft->flags & TF_VARIADIC) && em_tail_caller_ok(e, s) && !vl_notail(&L, VLT.lo32);
		if (e->E->o->optimize.shrink_wrap && !e->E->noopt && !(e->E->o->cfi && !DARWIN(c))) {
			memset(VLT.nf, 0, f->nins);
			for (uint32_t b = 0; L.tail && b < f->nblk; b++)
				for (uint32_t i = f->blk[b].first; i < f->blk[b].first + f->blk[b].n; i++)
					if (f->ins[i].op == VO_CALL) {
						VLT.nf[i] = vl_sibcand(&L, b, i);
					}
			ao.shrink = true;
			ao.noframe = VLT.nf;
		}
		if (!vir_alloc(f, &V->lp, &regs, &ao, &V->ao)) {
			*why = VIRFAIL(allocation_failed);
			snprintf(V->detail, sizeof V->detail, "%s", V->ao.err);
			return false;
		}
		L.sp = ao.shrink ? V->ao.spoint : VIR_NOBLK;
	}
	if (e->E->o->vir_aid.check) {
		vbuffer msg = { 0 };
		uint32_t n = vir_alloc_check(f, &regs, &V->ao, &msg);

		if (n) {
			fprintf(stderr, "vir: allocation check: %u failures\n%.*s", n, (int)msg.n, msg.p ? (char *)msg.p : "");
			free(msg.p);
			*why = VIRFAIL(allocation_check_failed);
			return false;
		}
		free(msg.p);
	}
	L.e = e, L.f = f, L.o = &V->ao;
	VLT.soff = vl_grow(VLT.soff, &VLT.cs, f->nslot + 1, sizeof(int64_t));
	VLT.spoff = vl_grow(VLT.spoff, &VLT.csp, V->ao.nspill + 1, sizeof(int64_t));
	VLT.lbl = vl_grow(VLT.lbl, &VLT.cl, f->nblk + 1, sizeof(uint32_t));
	VLT.uses = vl_grow(VLT.uses, &VLT.cu, f->nins + 1, 1);
	VLT.lo32 = vl_grow(VLT.lo32, &VLT.c32, f->nins + 1, 1);
	VLT.chs = vl_grow(VLT.chs, &VLT.cch, f->nblk + 1, sizeof(uint32_t));
	VLT.path = vl_grow(VLT.path, &VLT.cpa, f->nblk + 1, sizeof(uint32_t));
	L.soff = VLT.soff, L.spoff = VLT.spoff, L.lbl = VLT.lbl, L.uses = VLT.uses, L.lo32 = VLT.lo32;
	L.chs = VLT.chs;
	VLT.ord = vl_grow(VLT.ord, &VLT.co, f->nblk + 1, sizeof(uint32_t));
	VLT.opos = vl_grow(VLT.opos, &VLT.cop, f->nblk + 1, sizeof(uint32_t));
	VLT.hl = vl_grow(VLT.hl, &VLT.chl, f->nblk + 1, sizeof(uint32_t));
	VLT.lbf = vl_grow(VLT.lbf, &VLT.clf, f->nblk + 1, sizeof(uint32_t));
	memset(VLT.lbf, 0, (size_t)(f->nblk + 1) * sizeof(uint32_t));
	L.ord = VLT.ord, L.opos = VLT.opos, L.lp = &V->lp;
	L.drv = (virbuild_lowering){
		.target = &vl_lowering_target, .ctx = &L, .f = f, .o = L.o, .lp = L.lp,
		.order = L.ord, .position = L.opos, .rotation = VLT.hl, .chase = L.chs, .path = VLT.path,
		.block_label = L.lbl, .optimize = !e->E->noopt, .rotate = e->E->o->optimize.rotate_loops,
		.operand_value = L.ov, .operand_reg = L.opr, .max_operands = VL_MAXOP, .skip_at = &L.skip,
	};
	L.ret32 = KIND(c, ft->base) != TY_VOID && is_integer(c, ft->base) && tsize(c, ft->base) == 4;
	L.tail = !(ft->flags & TF_VARIADIC) && em_tail_caller_ok(e, s) && !vl_notail(&L, VLT.lo32);	/* (lo32: scratch until it is filled) */
	vir_uses(f, L.uses);
	vir_low32(f, L.ret32, L.lo32);

	/* Keep result tables queued by the VIR walker: end_function emits them. */
	/* the emitter's per-function state, as emit_function1 sets it */
	e->nin = 0;
	e->ndl = 0;
	e->raw.n = 0;
	e->nlabel = 0;
	e->nkv = 0, e->kvalid = 0, e->nkvs = 0, e->nael = 0;
	e->foff = e->fmax = e->outmax = e->cl_top = 0;
	e->nsla = e->nsstk = 0;
	e->falign = 16, e->resv = 0, e->nvsz = 0;
	e->dyn = e->calls = false;
	e->live = 0;
	e->usedcs = 0;
	e->nloop = e->nclean = 0;
	e->ncases = 0;
	e->njt = e->njtl = 0;
	e->reach = true;
	e->top = s;
	e->asmk = 0;
	e->inline_depth = 0;
	e->vararg = (ft->flags & TF_VARIADIC) != 0;
	e->nlr = 0;
	e->f = NULL;
	e->vfail = NULL;
	e->vlow = true;
	e->vtry = true;	/* efatal, eerror: back here */
	switch (_setjmp(e->bail)) {
	case 0:
		break;
	default:
		*why = e->vfail ? e->vfail : VIRFAIL(lowering_failed);
		e->vtry = false;
		e->vlow = false;
		return false;
	}
	for (uint32_t k = 0; k < f->nslot; k++) {
		if (f->slot[k].align > 16)
			vl_fail(&L, VIRFAIL(over_aligned_slot));
		L.soff[k] = slot(e, f->slot[k].size ? f->slot[k].size : 1, f->slot[k].align);
	}
	for (uint32_t k = 0; k < V->ao.nspill; k++)
		L.spoff[k] = slot(e, V->ao.ssz[k], V->ao.ssz[k]);
	for (int r = 8; r < 16; r++)
		if (V->ao.callee[1] >> r & 1)
			L.fcs[r - 8] = slot(e, 8, 8);
	for (uint32_t b = 0; b < f->nblk; b++)
		L.lbl[b] = newlabel(e);
	L.retlbl = newlabel(e);
	vl_body(&L);
	deflabel_forced(e, L.retlbl);
	if (L.risk && e->fmax + e->outmax + 128 > 4000)
		vl_fail(&L, VIRFAIL(large_frame_scratch));
	for (int r = 19; r <= 28; r++)
		if (V->ao.callee[0] >> r & 1)
			e->usedcs |= 1u << r;
	finish_function(e, s);
	e->vtry = false;
	e->vlow = false;
	return true;
}
