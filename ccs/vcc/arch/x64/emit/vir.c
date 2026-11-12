/* Allocated VIR to x86-64 records.  First slice: scalar/SSE2 leaf functions.
 * Calls, unusual ABI groups and unsupported vector operations fall back per function.
 * r11/xmm15 break edge cycles; r10/xmm14 are lowering scratch, never allocated.
 * Keep vector instructions in opaque records until the record vocabulary has SIMD ops.
 */
#include "x64impl.h"
#include "../../../emit/walk/proto.h"
#include "../../../emit/vir/vw.h"

typedef struct vx {
	ecx *e;
	vir_fn *f;
	vir_alloc_out *o;
	int64_t *slot, *spill;
	uint32_t *label, ret;
} vx;

static void *vx_grow(void *p, uint32_t *cap, uint32_t n, size_t size)
{
	if (n <= *cap) return p;
	*cap = n + n / 2 + 16;
	p = realloc(p, (size_t)*cap * size);
	if (!p) abort();
	return p;
}
static int vx_reg(uint8_t r)
{
	return r == VR_SCR0 ? R11 : r == VR_SCR1 ? XMM15 : VR_CLASS(r) ? XMM0 + VR_NUM(r) : VR_NUM(r);
}
static int vx_size(uint32_t t) { return vir_isvec(t) ? 16 : t == VT_I32 || t == VT_F32 ? 4 : 8; }
static int64_t vx_bits(const vir_ins *i) { return (int64_t)((uint64_t)i->c << 32 | i->b); }

/* Only one-piece scalar ABI groups reach this target. */
static uint8_t vx_param(const vir_fn *f, uint32_t g, int *stack)
{
	uint32_t ni = 0, nf = 0;
	int off = 16;
	uint8_t r = VR_NONE;
	for (uint32_t k = 0; k <= g; k++) {
		bool fp = VG_CLASS(f->extra[f->sig + 3 + 2 * k], 0) != VC_INT;
		r = fp ? (nf < 8 ? VR_MAKE(1, nf++) : VR_NONE) : (ni < 6 ? (uint8_t)x64_argreg(ni++) : VR_NONE);
		if (k == g) *stack = off;
		if (r == VR_NONE) off += 8;
	}
	return r;
}
static void vx_fixed(const vir_fn *f, uint32_t v, vir_fix *x, void *ctx)
{
	const vir_ins *i = &f->ins[v];
	(void)ctx;
	if (i->op == VO_PARAM) {
		int stack;
		x->res = vx_param(f, i->a, &stack);
	} else if (i->op == VO_RET && f->extra[i->c]) {
		x->nopnd = 1;
		x->opnd[0] = vir_rclass(f->ins[f->extra[i->c + 1]].ty) ? VR_MAKE(1, 0) : RAX;
	} else if (i->op == VO_SHL || i->op == VO_LSHR || i->op == VO_ASHR) {
		x->nopnd = 2;
		x->opnd[1] = RCX;
	}
}
static void vx_raw(vx *L, const char *s)
{
	rec *r = x64_put(L->e, X_RAW);
	r->off = L->e->raw.n;
	r->ref = strlen(s);
	tb_s(&L->e->raw, s);
}
static void vx_simd(vx *L, const char *op, int d, int a, int b)
{
	char s[160];
	/* A three-address noncommutative operation may overwrite its second source. */
	int out = d == b && d != a ? (XMM0 + 14) : d;
	if (out != a) x64_op3(L->e, X_MOV, 16, out, a, XNOREG);
	snprintf(s, sizeof s, "\t%s\t%%xmm%d, %%xmm%d\n", op, b - XMM0, out - XMM0);
	vx_raw(L, s);
	if (out != d) x64_op3(L->e, X_MOV, 16, d, out, XNOREG);
}
static void vx_mat(vx *L, uint8_t dst, uint32_t v)
{
	const vir_ins *i = &L->f->ins[v];
	int d = vx_reg(dst);
	if (i->op == VO_FCONST) {
		x64_movi(L->e, R10, vx_bits(i));
		x64_op3(L->e, X_MOV, vx_size(i->ty), d, R10, XNOREG);
	} else if (i->op == VO_ICONST) {
		x64_movi(L->e, d, vx_bits(i));
	} else if (i->op == VO_SYMADDR && sym_needs_got(L->e->c, i->a)) {
		rec *r = x64_op3(L->e, X_GOT, 8, d, XNOREG, XNOREG);
		r->ref = i->a;
		if (vx_bits(i)) x64_op3(L->e, X_ADDI, 8, d, d, XNOREG)->imm = vx_bits(i);
	} else {
		lval a = { .k = i->op == VO_SLOTADDR ? L_FRAME : L_SYM,
			.off = vx_bits(i) + (i->op == VO_SLOTADDR ? L->slot[i->a] : 0), .ref = i->a };
		x64_mem(L->e, X_LEA, 8, d, &a, false);
	}
}
static void vx_memslot(vx *L, bool load, uint8_t reg, uint32_t s)
{
	lval a = { .k = L_FRAME, .off = L->spill[s] };
	x64_mem(L->e, load ? X_LD : X_ST, L->o->ssz[s], vx_reg(reg), &a, false);
}
static void vx_edits(vx *L, uint32_t *at, uint32_t end, uint32_t pos)
{
	for (; *at < end && L->o->edit[*at].pos == pos; ++*at) {
		const vir_edit *x = &L->o->edit[*at];
		switch (x->kind) {
		case VE_REMAT: vx_mat(L, x->dst, x->v); break;
		case VE_RELOAD: vx_memslot(L, true, x->dst, L->o->slot[x->v]); break;
		case VE_STORE: vx_memslot(L, false, x->src, L->o->slot[x->v]); break;
		case VE_MOVE: x64_mov(L->e, vx_reg(x->dst), vx_reg(x->src)); break;
		default: abort(); /* no swapped/tied operands in vx_fixed */
		}
	}
}
static void vx_edge(vx *L, uint32_t b, uint32_t k)
{
	uint32_t g = L->o->eidx[L->o->soff[b] + k];
	if (g == VIR_NOBLK) return;
	const vir_aedge *ed = &L->o->edge[g];
	for (uint32_t j = 0; j < ed->n; j++) {
		const vir_emove *m = &L->o->move[ed->first + j];
		bool ds = m->dst & VL_SLOT, ss = m->src & VL_SLOT;
		uint32_t d = m->dst & ~VL_SLOT, s = m->src & ~VL_SLOT;
		uint8_t tmp = vir_rclass(L->f->ins[m->v].ty) ? VR_MAKE(1, 14) : R10;
		if (m->kind == VE_REMAT) {
			vx_mat(L, ds ? tmp : d, m->v);
			if (ds) vx_memslot(L, false, tmp, d);
		} else if (m->kind == VE_XCHG) {
			/* Exchanges are register-only; use the second reserved scratch. */
			x64_mov(L->e, vx_reg(tmp), vx_reg(d));
			x64_mov(L->e, vx_reg(d), vx_reg(s));
			x64_mov(L->e, vx_reg(s), vx_reg(tmp));
		} else if (ds && ss) {
			vx_memslot(L, true, tmp, s);
			vx_memslot(L, false, tmp, d);
		} else if (ds) vx_memslot(L, false, s, d);
		else if (ss) vx_memslot(L, true, d, s);
		else x64_mov(L->e, vx_reg(d), vx_reg(s));
	}
}

static bool vx_supported(const vir_fn *f, uint32_t v)
{
	const vir_ins *i = &f->ins[v];
	bool vec = vir_isvec(i->ty);
	switch (i->op) {
	case VO_NOP: case VO_BPARAM: case VO_PARAM: case VO_LOAD: case VO_STORE:
	case VO_BR: case VO_BRIF: case VO_RET: case VO_TRAP: case VO_BARRIER:
	case VO_PTRADD: case VO_PSUB: case VO_P2I: case VO_I2P: case VO_EXT: case VO_TRUNC:
	case VO_ICMP: case VO_SHL: case VO_LSHR: case VO_ASHR: case VO_BITCAST:
		return true;
	case VO_ADD: case VO_SUB: case VO_AND: case VO_OR: case VO_XOR:
	case VO_FADD: case VO_FSUB: case VO_FMUL: case VO_FDIV:
		return true;
	case VO_MUL: case VO_NEG: case VO_NOT: case VO_FEXT: case VO_FTRUNC: case VO_FSQRT:
		return !vec;
	case VO_VSPLAT: case VO_VSERIES: case VO_VREV: case VO_VREDUCE:
		return true;
	default: return false;
	}
}
static void vx_ins(vx *L, uint32_t b, uint32_t v, const uint8_t *p, uint32_t n, uint8_t res)
{
	ecx *e = L->e;
	const vir_ins *i = &L->f->ins[v];
	int d = res == VR_NONE ? XNOREG : vx_reg(res);
	int a = n ? vx_reg(p[0]) : XNOREG, c = n > 1 ? vx_reg(p[1]) : XNOREG;
	int sz = vx_size(i->ty);
	static const uint8_t alu[VO_N] = { [VO_ADD]=X_ADD, [VO_PTRADD]=X_ADD, [VO_SUB]=X_SUB, [VO_PSUB]=X_SUB,
		[VO_MUL]=X_IMUL, [VO_AND]=X_AND, [VO_OR]=X_OR, [VO_XOR]=X_XOR,
		[VO_FADD]=X_FADD, [VO_FSUB]=X_FSUB, [VO_FMUL]=X_FMUL, [VO_FDIV]=X_FDIV };
	if (alu[i->op]) {
		if (vir_isvec(i->ty)) {
			const char *op = i->op == VO_ADD ? (i->ty == VT_I32X4 ? "paddd" : "paddq") :
				i->op == VO_SUB ? (i->ty == VT_I32X4 ? "psubd" : "psubq") :
				i->op == VO_AND ? "pand" : i->op == VO_OR ? "por" : i->op == VO_XOR ? "pxor" :
				i->op == VO_FADD ? (i->ty == VT_F32X4 ? "addps" : "addpd") :
				i->op == VO_FSUB ? (i->ty == VT_F32X4 ? "subps" : "subpd") :
				i->op == VO_FMUL ? (i->ty == VT_F32X4 ? "mulps" : "mulpd") :
				(i->ty == VT_F32X4 ? "divps" : "divpd");
			vx_simd(L, op, d, a, c);
		} else x64_op3(e, alu[i->op], sz, d, a, c);
		return;
	}
	switch (i->op) {
	case VO_NOP: case VO_BPARAM: return;
	case VO_BARRIER: x64_put(e, X_BARRIER); return;
	case VO_PARAM: {
		int stack;
		if (vx_param(L->f, i->a, &stack) == VR_NONE) {
			lval at = { .k = L_FRAME, .off = stack };
			x64_mem(e, X_LD, sz, d, &at, false);
		}
		return;
	}
	case VO_LOAD: case VO_STORE: {
		lval at = { .k = L_REG, .r = a };
		int ty = i->op == VO_LOAD ? i->ty : L->f->ins[i->b].ty;
		x64_mem(e, i->op == VO_LOAD ? X_LD : X_ST, vir_isvec(ty) ? 16 : 1 << VIF_W(i->fl),
			i->op == VO_LOAD ? d : c, &at, i->fl & VIF_MS);
		return;
	}
	case VO_NEG: case VO_NOT: x64_op3(e, i->op == VO_NEG ? X_NEG : X_NOT, sz, d, a, XNOREG); return;
	case VO_P2I: case VO_I2P: case VO_TRUNC: case VO_BITCAST:
		x64_op3(e, X_MOV, sz, d, a, XNOREG); return;
	case VO_EXT: {
		if (VIF_W(i->fl) == 3) { x64_mov(e, d, a); return; }
		rec *r = x64_op3(e, X_EXT, 1 << VIF_W(i->fl), d, a, XNOREG);
		r->cond = !!(i->fl & VIF_MS); return;
	}
	case VO_SHL: case VO_LSHR: case VO_ASHR:
		/* The result may be rcx: moving the first operand there would destroy cl.
		 * Shift in reserved r10, then publish the allocated result. */
		x64_mov(e, R10, a);
		x64_op3(e, i->op == VO_SHL ? X_SHL : i->op == VO_LSHR ? X_SHR : X_SAR, sz, R10, R10, c);
		x64_op3(e, X_MOV, sz, d, R10, XNOREG); return;
	case VO_ICMP: {
		static const uint8_t cc[] = { CC_E, CC_NE, CC_L, CC_LE, CC_G, CC_GE, CC_B, CC_BE, CC_A, CC_AE };
		x64_op3(e, X_CMP, vx_size(L->f->ins[i->a].ty), XNOREG, a, c);
		x64_op3(e, X_SETCC, 4, d, XNOREG, XNOREG)->cond = cc[i->fl & 15]; return;
	}
	case VO_FEXT: case VO_FTRUNC: x64_op3(e, X_CVTF2F, sz, d, a, XNOREG); return;
	case VO_FSQRT: x64_op3(e, X_FSQRT, sz, d, a, XNOREG); return;
	case VO_VSPLAT: {
		char s[160];
		int lane = vx_size(vir_velem(i->ty));
		x64_op3(e, X_MOV, lane, d, a, XNOREG);
		snprintf(s, sizeof s, "\t%s\t$0, %%xmm%d, %%xmm%d\n", lane == 4 ? "pshufd" : "shufpd", d-XMM0, d-XMM0);
		vx_raw(L, s); return;
	}
	case VO_VSERIES: {
		/* Construct the lane numbers with SSE2, without a constant pool. */
		char s[240];
		x64_movi(e, R10, i->ty == VT_I32X4 ? 0x0000000100000000LL : 0);
		x64_op3(e, X_MOV, 8, d, R10, XNOREG);
		x64_movi(e, R10, i->ty == VT_I32X4 ? 0x0000000300000002LL : 1);
		x64_op3(e, X_MOV, 8, (XMM0 + 14), R10, XNOREG);
		snprintf(s, sizeof s, "\tpunpcklqdq\t%%xmm14, %%xmm%d\n", d-XMM0);
		vx_raw(L, s); return;
	}
	case VO_VREV: {
		char s[160];
		snprintf(s, sizeof s, "\tpshufd\t$%d, %%xmm%d, %%xmm%d\n", vx_size(vir_velem(i->ty)) == 4 ? 27 : 78, a-XMM0, d-XMM0);
		vx_raw(L, s); return;
	}
	case VO_VREDUCE: {
		char s[320];
		bool narrow = i->ty == VT_I32;
		snprintf(s, sizeof s, "\tpshufd\t$78, %%xmm%d, %%xmm14\n\t%s\t%%xmm%d, %%xmm14\n", a-XMM0, narrow ? "paddd" : "paddq", a-XMM0);
		vx_raw(L, s);
		if (narrow) {
			vx_raw(L, "\tpshufd\t$177, %xmm14, %xmm15\n\tpaddd\t%xmm15, %xmm14\n");
		}
		x64_op3(e, X_MOV, sz, d, (XMM0 + 14), XNOREG); return;
	}
	case VO_BR: vx_edge(L, b, 0); x64_jump(e, L->label[i->a]); return;
	case VO_BRIF: {
		uint32_t yes = wk_newlabel(e);
		x64_op3(e, X_TEST, vx_size(L->f->ins[i->a].ty), XNOREG, a, a);
		rec *r = x64_put(e, X_JCC); r->cond = CC_NE; r->label = yes;
		vx_edge(L, b, 1); x64_jump(e, L->label[i->c]);
		x64_label(e, yes); vx_edge(L, b, 0); x64_jump(e, L->label[i->b]); return;
	}
	case VO_RET: x64_jump(e, L->ret); return;
	case VO_TRAP: x64_put(e, X_UD2); return;
	default: abort();
	}
}

VPRIV bool vw_x64(ecx *e, uint32_t sym, const char **why)
{
	vwst *V = e->vw;
	vir_fn *f = &V->f;
	uint64_t cs = (1u << RBX) | (1u << R12) | (1u << R13) | (1u << R14) | (1u << R15);
	vir_regs regs = { .alloc = { (X_TEMPS & ~(1u << R10)) | cs, (1u << 14) - 1 },
		.callee = { cs, 0 }, .fixed = vx_fixed };
	vx L = { .e = e, .f = f, .o = &V->ao };
	*why = "VIR x86-64: unsupported operation or ABI";
	if (e->E->o->debug || e->E->o->sprot || x64_nosimd(e)) return false;
	const attrs *attrs = sym_attrs(e->c, sym);
	if (attrs && attrs->interrupt) return false;
	for (uint32_t i = 1; i < f->nconst; i++)
		if (f->ins[i].op != VO_ICONST && f->ins[i].op != VO_FCONST &&
		    f->ins[i].op != VO_SLOTADDR && f->ins[i].op != VO_SYMADDR) return false;
	for (uint32_t i = 1; i < f->nconst; i++)
		if ((f->ins[i].op == VO_SYMADDR || f->ins[i].op == VO_SLOTADDR) &&
		    (vx_bits(&f->ins[i]) < INT32_MIN / 2 || vx_bits(&f->ins[i]) > INT32_MAX / 2)) return false;
	for (uint32_t i = f->nconst; i < f->nins; i++)
		if (!vx_supported(f, i)) return false;
	uint64_t framesz = (uint64_t)f->nins * 16; /* worst-case spill slots */
	for (uint32_t i = 0; i < f->nslot; i++) {
		if (f->slot[i].align > 16 || f->slot[i].size > INT32_MAX / 4) return false;
		framesz += f->slot[i].size + 16;
	}
	if (framesz > INT32_MAX / 2) return false;
	if (!vir_loops_build(f, &V->lp) || !vir_alloc(f, &V->lp, &regs, NULL, &V->ao)) {
		*why = "VIR x86-64: allocation failed"; return false;
	}
	if (getenv("VCC_VIR_CHECK")) {
		vbuf msg = { 0 };
		uint32_t n = vir_alloc_check(f, &regs, &V->ao, &msg);
		if (n) fprintf(stderr, "vir x86-64: allocation check: %.*s", (int)msg.n, msg.p ? (char *)msg.p : "");
		free(msg.p);
		if (n) { *why = "VIR x86-64: allocation check failed"; return false; }
	}
	/* No partial function is published before x64_finish; a failed lowering retries the walker. */
	L.slot = V->xslot = vx_grow(V->xslot, &V->cxslot, f->nslot + 1, sizeof *L.slot);
	L.spill = V->xspill = vx_grow(V->xspill, &V->cxspill, V->ao.nspill + 1, sizeof *L.spill);
	L.label = V->xlabel = vx_grow(V->xlabel, &V->cxlabel, f->nblk + 1, sizeof *L.label);
	e->vtry = true; e->vfail = NULL;
	if (_setjmp(e->bail)) {
		*why = e->vfail ? e->vfail : "VIR x86-64: lowering failed";
		e->vtry = false;
		return false;
	}
	e->reach = true;
	x64_begin(e, sym);
	e->x64->interrupt = e->x64->nlreg = 0;
	e->wk->sdepth = e->wk->ntu = e->wk->ntf = 0;
	e->wk->foff = 0; e->wk->falign = 16; e->wk->nlabel = 0;
	e->wk->spneed = false; e->wk->canary = 0;
	for (uint32_t k = 0; k < f->nslot; k++) L.slot[k] = wk_slot(e, f->slot[k].size ?: 1, f->slot[k].align);
	for (uint32_t k = 0; k < V->ao.nspill; k++) L.spill[k] = wk_slot(e, V->ao.ssz[k], V->ao.ssz[k]);
	for (uint32_t b = 0; b < f->nblk; b++) L.label[b] = wk_newlabel(e);
	L.ret = wk_newlabel(e);
	for (int r = 0; r < 16; r++)
		if (V->ao.callee[0] >> r & 1) x64_csused(e, r);
	for (uint32_t b = 0; b < f->nblk; b++) {
		x64_label(e, L.label[b]);
		uint32_t at = V->ao.boff[b], ei = V->ao.boff[f->nblk + 1 + b], end = V->ao.boff[f->nblk + 2 + b];
		for (uint32_t i = f->blk[b].first; i < f->blk[b].first + f->blk[b].n; i++) {
			uint32_t n = 0;
			vir_oit it; uint32_t *p;
			vir_oit_init(&it, f, &f->ins[i], false);
			while ((p = vir_oit_next(&it))) { (void)p; n++; }
			vx_edits(&L, &ei, end, i);
			uint8_t res = f->ins[i].ty != VT_VOID ? V->ao.opr[at + n] : VR_NONE;
			vx_ins(&L, b, i, &V->ao.opr[at], n, res);
			at += n + (f->ins[i].ty != VT_VOID);
			if (res != VR_NONE && V->ao.slot[i] != VIR_NOBLK && V->ao.sdef[i]) vx_memslot(&L, false, res, V->ao.slot[i]);
		}
		vx_edits(&L, &ei, end, f->blk[b].first + f->blk[b].n);
	}
	x64_label(e, L.ret);
	x64_finish(e, sym);
	e->vtry = false;
	*why = NULL; return true;
}
