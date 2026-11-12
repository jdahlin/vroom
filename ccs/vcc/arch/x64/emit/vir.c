/* Allocated VIR to x86-64 records.  Scalar/SSE2 operations and one-piece SysV calls.
 * Unusual ABI groups and unsupported vector operations fall back per function.
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
	uint32_t *label, *edgelab, ret, cmp, cmpblock, setrec;
	uint8_t *uses;
	uint32_t memv, scaledv; int membase, memindex, scaleindex; uint8_t memsh, scalesh; bool indexed; int64_t memoff;
	uint32_t bev; int bebase;
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
static uint8_t vx_arg(const vir_fn *f, uint32_t sig, uint32_t g, int *stack)
{
	uint32_t ni = 0, nf = 0;
	int off = 16;
	uint8_t r = VR_NONE;
	for (uint32_t k = 0; k <= g; k++) {
		bool fp = VG_CLASS(f->extra[sig + 3 + 2 * k], 0) != VC_INT;
		r = fp ? (nf < 8 ? VR_MAKE(1, nf++) : VR_NONE) : (ni < 6 ? (uint8_t)x64_argreg(ni++) : VR_NONE);
		if (k == g) *stack = off;
		if (r == VR_NONE) off += 8;
	}
	return r;
}
static bool vx_imm(const vir_fn *f, uint32_t v, int64_t *value)
{
	const vir_ins *i=&f->ins[v];
	if (i->op!=VO_ICONST) return false;
	int64_t k=i->ty==VT_I32?(int64_t)(int32_t)i->b:vx_bits(i);
	if (k!=(int32_t)k) return false;
	*value=k;return true;
}
static void vx_fixed(const vir_fn *f, uint32_t v, vir_fix *x, void *ctx)
{
	const vir_ins *i = &f->ins[v];
	(void)ctx;
	if (i->op == VO_PARAM) {
		int stack;
		x->res = vx_arg(f, f->sig, i->a, &stack);
	} else if (i->op == VO_MEMCPY || i->op == VO_MEMSET) {
		x->nopnd = 3;
		x->opnd[0] = RDI;
		x->opnd[1] = i->op == VO_MEMSET ? RAX : RSI;
		x->opnd[2] = RCX;
		x->clob[0] = (1u << RDI) | (1u << RCX) | (i->op == VO_MEMCPY ? 1u << RSI : 0);
	} else if (i->op == VO_CALL) {
		uint32_t n = f->extra[i->c];
		x->nopnd = n + 1;
		for (uint32_t k = 0; k < n; k++) {
			int stack;
			x->opnd[k + 1] = vx_arg(f, i->b, k, &stack);
			if (x->opnd[k + 1] == VR_NONE) x->mem |= 1u << (k + 1);
		}
		const vir_ins *callee = &f->ins[i->a];
		if (callee->op == VO_SYMADDR && !callee->b && !callee->c) x->mem |= 1;
		x->clob[0] = X_TEMPS & ~(1u << R10);
		x->clob[1] = (1u << 14) - 1;
		if (i->ty != VT_VOID) x->res = vir_rclass(i->ty) ? VR_MAKE(1, 0) : RAX;
	} else if (i->op == VO_RET && f->extra[i->c]) {
		x->nopnd = 1;
		x->opnd[0] = vir_rclass(f->ins[f->extra[i->c + 1]].ty) ? VR_MAKE(1, 0) : RAX;
	} else if (i->op == VO_SDIV || i->op == VO_UDIV || i->op == VO_SREM || i->op == VO_UREM || i->op == VO_SMULHI || i->op == VO_UMULHI) {
		x->nopnd = 2;
		x->opnd[0] = RAX;
		x->res = i->op == VO_SREM || i->op == VO_UREM || i->op == VO_SMULHI || i->op == VO_UMULHI ? RDX : RAX;
		x->clob[0] = (1u << RAX) | (1u << RDX);
	} else if (i->op == VO_SHL || i->op == VO_LSHR || i->op == VO_ASHR) {
		x->nopnd = 2;
		int64_t k;
		if (vx_imm(f,i->b,&k) && k>=0 && k<(i->ty==VT_I32?32:64)) x->mem=2;
		else x->opnd[1] = RCX;
	}
	int64_t k;
	if (!vir_rclass(i->ty) && (i->op==VO_ADD || i->op==VO_SUB || i->op==VO_PTRADD || i->op==VO_PSUB ||
	    i->op==VO_AND || i->op==VO_OR || i->op==VO_XOR || i->op==VO_MUL || i->op==VO_ICMP) && vx_imm(f,i->b,&k)) {
		x->nopnd=2;x->mem|=2;
	}
	/* Prefer the dying first input for x86's destructive operations. */
	switch(i->op) {
	case VO_SUB: case VO_PSUB: case VO_MUL: case VO_AND: case VO_OR: case VO_XOR:
	case VO_NEG: case VO_NOT: case VO_SHL: case VO_LSHR: case VO_ASHR:
	case VO_FADD: case VO_FSUB: case VO_FMUL: case VO_FDIV:
		x->fl|=VFX_TIED;break;
	default:break;
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

/* Put edge copies out of line. Branches without copies name their real block. */
static uint32_t vx_target(vx *L,uint32_t b,uint32_t k,uint32_t target)
{
	uint32_t g=L->o->eidx[L->o->soff[b]+k];
	if(g==VIR_NOBLK || !L->o->edge[g].n)return L->label[target];
	if(!L->edgelab[g])L->edgelab[g]=wk_newlabel(L->e);
	return L->edgelab[g];
}

static bool vx_supported(const vir_fn *f, uint32_t v)
{
	const vir_ins *i = &f->ins[v];
	bool vec = vir_isvec(i->ty);
	switch (i->op) {
	case VO_NOP: case VO_BPARAM: case VO_PARAM: case VO_LOAD: case VO_STORE:
	case VO_BR: case VO_BRIF: case VO_RET: case VO_TRAP: case VO_BARRIER: case VO_CALL:
	case VO_MEMCPY: case VO_MEMSET: case VO_SWITCH: case VO_ALOAD: case VO_ASTORE: case VO_AFENCE:
	case VO_PTRADD: case VO_PSUB: case VO_P2I: case VO_I2P: case VO_EXT: case VO_TRUNC:
	case VO_ICMP: case VO_FCMP: case VO_SELECT: case VO_SHL: case VO_LSHR: case VO_ASHR: case VO_BITCAST:
	case VO_S2F: case VO_F2S: case VO_VFCMP: case VO_VBITSEL: case VO_VS2F:
	case VO_FNEG: case VO_FABS: case VO_FSQRT: case VO_NEG: case VO_NOT:
		return true;
	case VO_SDIV: case VO_UDIV: case VO_SREM: case VO_UREM: case VO_SMULHI: case VO_UMULHI: return true;
	case VO_U2F: return f->ins[i->a].ty == VT_I32;
	case VO_F2U: return i->ty == VT_I32;
	case VO_VICMP: return i->ty == VT_I32X4 && (i->fl & 15) < 6;
	case VO_MUL: return !vec || i->ty == VT_I32X4;
	case VO_FEXT: case VO_FTRUNC:
		return true;
	case VO_ADD: case VO_SUB: case VO_AND: case VO_OR: case VO_XOR:
	case VO_FADD: case VO_FSUB: case VO_FMUL: case VO_FDIV:
		return true;
	case VO_VINSERT: case VO_VEXTRACT:
	case VO_VSPLAT: case VO_VSERIES: case VO_VREV: case VO_VREDUCE:
		return true;
	default: return false;
	}
}
/* Switch edges retain their allocator edits in separate stubs. Dense tables use
 * signed offsets from a RIP-relative base, so the dispatch stays position independent. */
static void vx_cmpconst(vx *L, int reg, int size, uint64_t value)
{
	int64_t k = size == 4 ? (int64_t)(int32_t)value : (int64_t)value;
	if (k == (int32_t)k)
		x64_op3(L->e, X_CMPI, size, XNOREG, reg, XNOREG)->imm = k;
	else {
		x64_movi(L->e, R11, k);
		x64_op3(L->e, X_CMP, size, XNOREG, reg, R11);
	}
}
static void vx_switch(vx *L, uint32_t block, const vir_ins *in, int selector)
{
	ecx *e = L->e;
	vir_fn *f = L->f;
	bool wide = f->ins[in->a].ty == VT_I64;
	uint32_t size = wide ? 8 : 4, stride = wide ? 5 : 3, n = f->extra[in->c];
	const uint32_t *cases = &f->extra[in->c+1];
	uint32_t *labels = malloc(((size_t)n+1)*sizeof *labels);
	if (!labels) abort();
	for (uint32_t k=0;k<=n;k++) labels[k]=vx_target(L,block,k,k?cases[stride*(k-1)+stride-1]:in->b);
	uint64_t lo=0,span=0,count=0;
	if (n) {
		lo = wide ? (uint64_t)cases[1]<<32 | cases[0] : cases[0];
		uint64_t hi = wide ? (uint64_t)cases[stride*(n-1)+3]<<32 | cases[stride*(n-1)+2] : cases[stride*(n-1)+1];
		span = hi-lo;
		for (uint32_t k=0;k<n;k++) {
			uint64_t a=wide ? (uint64_t)cases[stride*k+1]<<32 | cases[stride*k] : cases[stride*k];
			uint64_t h=wide ? (uint64_t)cases[stride*k+3]<<32 | cases[stride*k+2] : cases[stride*k+1];
			count += h-a >= 64 ? 64 : h-a+1;
		}
	}
	if (n >= 6 && span < 4096 && span+1 <= 4*count) {
		uint32_t table=wk_newlabel(e);
		x64_op3(e,X_MOV,size,R10,selector,XNOREG);
		if (lo) {
			x64_movi(e,R11,size==4?(int64_t)(int32_t)lo:(int64_t)lo);
			x64_op3(e,X_SUB,size,R10,R10,R11);
		}
		vx_cmpconst(L,R10,size,span);
		rec *r=x64_put(e,X_JCC);r->cond=CC_A;r->label=labels[0];
		x64_op3(e,X_LEAL,8,R11,XNOREG,XNOREG)->label=table;
		x64_op3(e,X_LEASCALE,8,R10,R11,R10)->sh=2;
		lval mem={.k=L_REG,.r=R10};
		x64_mem(e,X_LD,4,R10,&mem,true);
		x64_op3(e,X_ADD,8,R10,R10,R11);
		x64_op3(e,X_JMPR,8,XNOREG,R10,XNOREG);
		x64_label(e,table);
		uint32_t k=0;
		for (uint64_t offset=0;offset<=span;offset++) {
			uint64_t v=lo+offset,h,a;
			while (k<n && (h=wide?(uint64_t)cases[stride*k+3]<<32|cases[stride*k+2]:cases[stride*k+1])<v) k++;
			a=k<n?(wide?(uint64_t)cases[stride*k+1]<<32|cases[stride*k]:cases[stride*k]):0;
			r=x64_put(e,X_JTE);r->label=k<n && a<=v?labels[k+1]:labels[0];r->ref=table;
		}
	} else {
		for (uint32_t k=0;k<n;k++) {
			uint64_t a=wide?(uint64_t)cases[stride*k+1]<<32|cases[stride*k]:cases[stride*k];
			uint64_t h=wide?(uint64_t)cases[stride*k+3]<<32|cases[stride*k+2]:cases[stride*k+1];
			int reg=selector;
			if (a!=h) {
				x64_movi(e,R11,size==4?(int64_t)(int32_t)a:(int64_t)a);
				x64_op3(e,X_SUB,size,R10,selector,R11);reg=R10;
			}
			vx_cmpconst(L,reg,size,a==h?a:h-a);
			rec *r=x64_put(e,X_JCC);r->cond=a==h?CC_E:CC_BE;r->label=labels[k+1];
		}
		x64_jump(e,labels[0]);
	}
	free(labels);
}

/* Edits are sorted by instruction. A folded index must survive the intervening
 * reloads/rematerializations; memory consumers require no intervening edit. */
static bool vx_foldedits(vx *L,uint32_t b,uint32_t pos,int keep)
{
	uint32_t lo=L->o->boff[L->f->nblk+1+b],hi=L->o->boff[L->f->nblk+2+b],end=hi;
	while(lo<hi) {uint32_t m=lo+(hi-lo)/2;if(L->o->edit[m].pos<pos)lo=m+1;else hi=m;}
	for(uint32_t k=lo;k<end && L->o->edit[k].pos==pos;k++) {
		const vir_edit *edit=&L->o->edit[k];
		if(keep==XNOREG || (edit->kind!=VE_REMAT && edit->kind!=VE_RELOAD) || vx_reg(edit->dst)==keep)return false;
	}
	return true;
}
static bool vx_memnext(vx *L,uint32_t b,uint32_t v)
{
	return v+1<L->f->blk[b].first+L->f->blk[b].n && L->uses[v]==1 && L->o->slot[v]==VIR_NOBLK &&
		L->f->ins[v+1].a==v && (L->f->ins[v+1].op==VO_LOAD || L->f->ins[v+1].op==VO_STORE) &&
		vx_foldedits(L,b,v+1,XNOREG);
}

static uint8_t vx_condition(vx *L,uint32_t b,uint32_t v,uint32_t value,int reg)
{
	ecx *e=L->e;
	bool flags=L->cmp==value && L->cmpblock==b && v==value+1;
	uint8_t cc=CC_NE;
	if (flags) {
		for(uint32_t k=L->setrec+1;k<e->x64->nin;k++)
			if(x64_effects(e,&e->x64->in[k]).fl & (OD_WFLAGS|OD_BARRIER|OD_CALL)) {flags=false;break;}
	}
	if(flags) {
		cc=e->x64->in[L->setrec].cond;
		if(L->uses[value]==1 && L->o->slot[value]==VIR_NOBLK) {
			memmove(&e->x64->in[L->setrec],&e->x64->in[L->setrec+1],(e->x64->nin-L->setrec-1)*sizeof(rec));
			e->x64->nin--;
		}
	} else x64_op3(e, X_TEST, vx_size(L->f->ins[value].ty), XNOREG, reg, reg);
	return cc;
}

static void vx_ins(vx *L, uint32_t b, uint32_t v, const uint8_t *p, uint32_t n, uint8_t res)
{
	ecx *e = L->e;
	const vir_ins *i = &L->f->ins[v];
	int d = res == VR_NONE ? (vir_rclass(i->ty) ? XMM0 + 14 : R10) : vx_reg(res);
	int a = n ? vx_reg(p[0]) : XNOREG, c = n > 1 ? vx_reg(p[1]) : XNOREG;
	int sz = vx_size(i->ty);
	static const uint8_t alu[VO_N] = { [VO_ADD]=X_ADD, [VO_PTRADD]=X_ADD, [VO_SUB]=X_SUB, [VO_PSUB]=X_SUB,
		[VO_MUL]=X_IMUL, [VO_AND]=X_AND, [VO_OR]=X_OR, [VO_XOR]=X_XOR,
		[VO_FADD]=X_FADD, [VO_FSUB]=X_FSUB, [VO_FMUL]=X_FMUL, [VO_FDIV]=X_FDIV };
	if(L->bev) {
		if(v<L->bev)return;
		if(v==L->bev) {
			lval at={.k=L_REG,.r=L->bebase};x64_mem(e,X_LD,2,d,&at,false);
			x64_op3(e,X_BSWAP,2,d,d,XNOREG);L->bev=0;return;
		}
	}
	/* Two adjacent ordinary byte reads form one exact, unaligned word read.
	 * Every intermediate dies here, and no allocator edit may use or replace
	 * a skipped definition or the preserved base register. */
	if(i->op==VO_LOAD && i->ty==VT_I32 && !(i->fl&(VIF_VOL|VIF_MS|3)) && L->memv!=i->a &&
	   v+4<L->f->blk[b].first+L->f->blk[b].n) {
		const vir_ins *s=&L->f->ins[v+1],*padd=&L->f->ins[v+2],*lo=&L->f->ins[v+3],*join=&L->f->ins[v+4];
		int64_t shift,off;
		bool ok=s->op==VO_SHL && s->a==v && vx_imm(L->f,s->b,&shift) && shift==8 &&
			padd->op==VO_PTRADD && padd->a==i->a && vx_imm(L->f,padd->b,&off) && off==1 &&
			lo->op==VO_LOAD && lo->ty==VT_I32 && lo->a==v+2 && !(lo->fl&(VIF_VOL|VIF_MS|3)) &&
			join->op==VO_OR && join->ty==VT_I32 && ((join->a==v+1 && join->b==v+3)||(join->b==v+1 && join->a==v+3));
		for(uint32_t k=v;ok && k<v+4;k++)ok=L->uses[k]==1 && L->o->slot[k]==VIR_NOBLK && vx_foldedits(L,b,k+1,XNOREG);
		if(ok) {L->bev=v+4;L->bebase=a;return;}
	}
	if (i->op == VO_MUL && i->ty == VT_I32X4) {
		char text[600];
		snprintf(text, sizeof text,
			"\tmovaps %%xmm%d, %%xmm14\n\tmovaps %%xmm%d, %%xmm15\n"
			"\tpsrldq $4, %%xmm14\n\tpsrldq $4, %%xmm15\n\tpmuludq %%xmm15, %%xmm14\n\tpshufd $8, %%xmm14, %%xmm14\n"
			"\tmovaps %%xmm%d, %%xmm15\n\tmovaps %%xmm%d, %%xmm%d\n\tpmuludq %%xmm15, %%xmm%d\n"
			"\tpshufd $8, %%xmm%d, %%xmm%d\n\tpunpckldq %%xmm14, %%xmm%d\n",
			a-XMM0, c-XMM0, c-XMM0, a-XMM0, d-XMM0, d-XMM0, d-XMM0, d-XMM0, d-XMM0);
		vx_raw(L, text); return;
	}
	if ((i->op==VO_MUL || i->op==VO_SHL) && i->ty==VT_I64 && L->uses[v]==1 && L->o->slot[v]==VIR_NOBLK &&
	    v+2<L->f->blk[b].first+L->f->blk[b].n && L->f->ins[v+1].op==VO_PTRADD && L->f->ins[v+1].b==v &&
	    vx_memnext(L,b,v+1) && vx_foldedits(L,b,v+1,a)) {
		int64_t k;
		if(vx_imm(L->f,i->b,&k) && (i->op==VO_SHL ? k>=0 && k<=3 : k==1 || k==2 || k==4 || k==8)) {
			L->scaledv=v;L->scaleindex=a;
			L->scalesh=i->op==VO_SHL?k:k==8?3:k==4?2:k==2?1:0;
			return;
		}
	}
	if (i->op==VO_PTRADD && vx_memnext(L,b,v)) {
		L->memv=v;L->membase=a;L->memoff=0;
		L->indexed=!vx_imm(L->f,i->b,&L->memoff);
		L->memindex=L->scaledv==i->b?L->scaleindex:c;
		L->memsh=L->scaledv==i->b?L->scalesh:0;
		return;
	}
	if (alu[i->op]) {
		if (n>1 && p[1]==VR_NONE && !vir_rclass(i->ty)) {
			static const uint8_t immop[VO_N]={[VO_ADD]=X_ADDI,[VO_PTRADD]=X_ADDI,[VO_SUB]=X_SUBI,[VO_PSUB]=X_SUBI,
				[VO_AND]=X_ANDI,[VO_OR]=X_ORI,[VO_XOR]=X_XORI,[VO_MUL]=X_IMULI};
			int64_t k;vx_imm(L->f,i->b,&k);
			if(i->op==VO_PTRADD || i->op==VO_ADD) {
				lval at={.k=L_REG,.r=(uint8_t)a,.off=k};x64_mem(e,X_LEA,sz,d,&at,false);
			} else x64_op3(e,immop[i->op],sz,d,a,XNOREG)->imm=k;
			return;
		}
		if (vir_isvec(i->ty)) {
			const char *op = i->op == VO_ADD ? (i->ty == VT_I32X4 ? "paddd" : "paddq") :
				i->op == VO_SUB ? (i->ty == VT_I32X4 ? "psubd" : "psubq") :
				i->op == VO_AND ? "pand" : i->op == VO_OR ? "por" : i->op == VO_XOR ? "pxor" :
				i->op == VO_FADD ? (i->ty == VT_F32X4 ? "addps" : "addpd") :
				i->op == VO_FSUB ? (i->ty == VT_F32X4 ? "subps" : "subpd") :
				i->op == VO_FMUL ? (i->ty == VT_F32X4 ? "mulps" : "mulpd") :
				(i->ty == VT_F32X4 ? "divps" : "divpd");
			vx_simd(L, op, d, a, c);
		} else if (i->op==VO_ADD || i->op==VO_PTRADD) x64_op3(e,X_LEASCALE,sz,d,a,c)->sh=0;
		else x64_op3(e, alu[i->op], sz, d, a, c);
		return;
	}
	switch (i->op) {
	case VO_NOP: case VO_BPARAM: return;
	case VO_BARRIER: x64_put(e, X_BARRIER); return;
	case VO_MEMCPY: case VO_MEMSET:
		x64_put(e, X_REP)->cond = i->op == VO_MEMSET;
		return;
	case VO_CALL: {
		uint32_t ng = L->f->extra[i->c];
		int total = 0, nf = 0;
		for (uint32_t k = 0; k < ng; k++) {
			int off;
			uint8_t r = vx_arg(L->f, i->b, k, &off);
			if (r == VR_NONE) total = off - 8;
			else if (VR_CLASS(r)) nf++;
		}
		total = (total + 15) & ~15;
		if (total) x64_put(e, X_ADJSP)->imm = -total;
		for (uint32_t k = 0; k < ng; k++) {
			int off;
			if (vx_arg(L->f, i->b, k, &off) != VR_NONE) continue;
			uint32_t value = L->f->extra[i->c + 1 + k];
			uint8_t r = p[k + 1];
			if (r == VR_NONE) {
				r = vir_rclass(L->f->ins[value].ty) ? VR_MAKE(1, 14) : R10;
				if (value < L->f->nconst) vx_mat(L, r, value);
				else vx_memslot(L, true, r, L->o->slot[value]);
			}
			lval at = { .k = L_REG, .r = RSP, .off = off - 16 };
			x64_mem(e, X_ST, vx_size(L->f->ins[value].ty), vx_reg(r), &at, false);
		}
		bool indirect = p[0] != VR_NONE;
		if (indirect) x64_mov(e, R11, a); /* al is set for variadic calls */
		if (VSIG_VARIADIC(L->f->extra[i->b])) x64_movi(e, RAX, nf);
		if (indirect) x64_op3(e, X_CALLR, 8, XNOREG, R11, XNOREG);
		else {
			const vir_ins *callee = &L->f->ins[i->a];
			note_ref(e, callee->a);
			x64_put(e, X_CALL)->ref = callee->a;
		}
		if (total) x64_put(e, X_ADJSP)->imm = total;
		e->calls = true;
		return;
	}
	case VO_PARAM: {
		int stack;
		if (vx_arg(L->f, L->f->sig, i->a, &stack) == VR_NONE) {
			lval at = { .k = L_FRAME, .off = stack };
			x64_mem(e, X_LD, sz, d, &at, false);
		}
		return;
	}
	case VO_ALOAD: case VO_LOAD: case VO_STORE: {
		lval at = { .k = L_REG, .r = a };
		bool folded=L->memv==i->a;
		if(folded) {at.r=L->membase;at.off=L->memoff;L->memv=0;}
		bool load = i->op != VO_STORE;
		int ty = load ? i->ty : L->f->ins[i->b].ty;
		int width=vir_isvec(ty)?16:1<<VIF_W(i->fl);
		if(folded && L->indexed) {
			rec *r=x64_op3(e,load?X_LDX:X_STX,width,load?d:XNOREG,at.r,load?XNOREG:c);
			r->r[3]=L->memindex;r->sh=L->memsh;r->cond=!!(i->fl & VIF_MS);r->off=at.off;
		} else x64_mem(e,load?X_LD:X_ST,width,load?d:c,&at,i->fl & VIF_MS);
		return;
	}
	case VO_NEG: case VO_NOT:
		if (vir_isvec(i->ty)) {
			char text[200];
			snprintf(text, sizeof text, "\t%s %%xmm14, %%xmm14\n\t%s %%xmm%d, %%xmm14\n\tmovaps %%xmm14, %%xmm%d\n",
				i->op == VO_NEG ? "pxor" : "pcmpeqd",
				i->op == VO_NOT ? "pxor" : i->ty == VT_I32X4 ? "psubd" : "psubq", a-XMM0, d-XMM0);
			vx_raw(L, text);
		} else x64_op3(e, i->op == VO_NEG ? X_NEG : X_NOT, sz, d, a, XNOREG);
		return;
	case VO_P2I: case VO_I2P: case VO_TRUNC: case VO_BITCAST:
		/* An i32 value does not promise anything about the upper GPR bits;
		 * a later unsigned EXT is responsible for clearing them if needed. */
		if (d==a) return;
		x64_op3(e, X_MOV, sz, d, a, XNOREG); return;
	case VO_EXT: {
		if (VIF_W(i->fl) == 3) { x64_mov(e, d, a); return; }
		rec *r = x64_op3(e, X_EXT, 1 << VIF_W(i->fl), d, a, XNOREG);
		r->cond = !!(i->fl & VIF_MS); return;
	}
	case VO_SHL: case VO_LSHR: case VO_ASHR:
		int64_t count;
		if (vx_imm(L->f,i->b,&count) && count>=0 && count<(i->ty==VT_I32?32:64)) {
			x64_op3(e,i->op==VO_SHL?X_SHLI:i->op==VO_LSHR?X_SHRI:X_SARI,sz,d,a,XNOREG)->imm=count;return;
		}
		/* The result may be rcx: moving the first operand there would destroy cl.
		 * Shift in reserved r10, then publish the allocated result. */
		x64_mov(e, R10, a);
		x64_op3(e, i->op == VO_SHL ? X_SHL : i->op == VO_LSHR ? X_SHR : X_SAR, sz, R10, R10, c);
		x64_op3(e, X_MOV, sz, d, R10, XNOREG); return;
	case VO_ICMP: {
		static const uint8_t cc[] = { CC_E, CC_NE, CC_L, CC_LE, CC_G, CC_GE, CC_B, CC_BE, CC_A, CC_AE };
		if (p[1]==VR_NONE) {int64_t k;vx_imm(L->f,i->b,&k);vx_cmpconst(L,a,vx_size(L->f->ins[i->a].ty),(uint64_t)k);}
		else x64_op3(e, X_CMP, vx_size(L->f->ins[i->a].ty), XNOREG, a, c);
		L->cmp=v;L->cmpblock=b;L->setrec=e->x64->nin;
		x64_op3(e, X_SETCC, 4, d, XNOREG, XNOREG)->cond = cc[i->fl & 15]; return;
	}
	case VO_FCMP: {
		int cc = i->fl & 15;
		bool swap = cc == 2 || cc == 3;
		x64_op3(e, X_FCMP, vx_size(L->f->ins[i->a].ty), XNOREG, swap ? c : a, swap ? a : c);
		if (cc < 2) x64_op3(e, X_FSETCC, 4, d, XNOREG, XNOREG)->cond = cc;
		else x64_op3(e, X_SETCC, 4, d, XNOREG, XNOREG)->cond =
			cc == 6 ? CC_P : cc == 7 ? CC_NE : cc == 2 || cc == 4 ? CC_A : CC_AE;
		return;
	}
	case VO_SELECT: {
		if(!vir_rclass(i->ty)) {
			uint8_t cc=vx_condition(L,b,v,i->a,a);
			int no=vx_reg(p[2]);
			if(d==c){int swap=c;c=no;no=swap;cc^=1;}
			x64_op3(e,X_CMOV,sz,d,no,c)->cond=cc;return;
		}
		int tmp = vir_rclass(i->ty) ? XMM0 + 14 : R10;
		uint32_t done = wk_newlabel(e);
		x64_op3(e, X_TEST, vx_size(L->f->ins[i->a].ty), XNOREG, a, a);
		x64_op3(e, X_MOV, sz, tmp, vx_reg(p[2]), XNOREG);
		rec *r = x64_put(e, X_JCC); r->cond = CC_E; r->label = done;
		x64_op3(e, X_MOV, sz, tmp, c, XNOREG);
		x64_label(e, done);
		x64_op3(e, X_MOV, sz, d, tmp, XNOREG); return;
	}
	case VO_FEXT: case VO_FTRUNC: x64_op3(e, X_CVTF2F, sz, d, a, XNOREG); return;
	case VO_S2F: case VO_U2F: {
		int from = vx_size(L->f->ins[i->a].ty);
		if (i->op == VO_U2F) { x64_op3(e, X_MOV, 4, R10, a, XNOREG); a = R10; from = 8; }
		x64_op3(e, X_CVTI2F, sz, d, a, XNOREG)->sh = from; return;
	}
	case VO_F2S: case VO_F2U: {
		int to = i->op == VO_F2U ? 8 : sz;
		x64_op3(e, X_CVTF2I, to, d, a, XNOREG)->sh = vx_size(L->f->ins[i->a].ty);
		if (i->op == VO_F2U) x64_op3(e, X_MOV, 4, d, d, XNOREG);
		return;
	}
	case VO_FNEG: case VO_FABS: {
		bool wide = vir_velem(i->ty) == VT_F64;
		char text[240];
		if (vir_isvec(i->ty)) {
			snprintf(text, sizeof text, "\tpcmpeqd %%xmm14, %%xmm14\n\t%s $%d, %%xmm14\n\t%s %%xmm%d, %%xmm14\n\tmovaps %%xmm14, %%xmm%d\n",
				i->op == VO_FNEG ? (wide ? "psllq" : "pslld") : (wide ? "psrlq" : "psrld"),
				i->op == VO_FNEG ? (wide ? 63 : 31) : 1,
				i->op == VO_FNEG ? "pxor" : "pand", a-XMM0, d-XMM0);
			vx_raw(L, text);
		} else {
			x64_op3(e, X_MOV, sz, R10, a, XNOREG);
			x64_movi(e, R11, i->op == VO_FNEG ? (wide ? INT64_MIN : 0x80000000LL) : (wide ? INT64_MAX : 0x7fffffff));
			x64_op3(e, i->op == VO_FNEG ? X_XOR : X_AND, sz, R10, R10, R11);
			x64_op3(e, X_MOV, sz, d, R10, XNOREG);
		}
		return;
	}
	case VO_FSQRT:
		if (vir_isvec(i->ty)) {
			char text[100];
			snprintf(text, sizeof text, "\t%s %%xmm%d, %%xmm%d\n", i->ty == VT_F32X4 ? "sqrtps" : "sqrtpd", a-XMM0, d-XMM0);
			vx_raw(L, text);
		} else x64_op3(e, X_FSQRT, sz, d, a, XNOREG);
		return;
	case VO_VS2F:
		if (i->ty == VT_F32X4) {
			char text[100]; snprintf(text, sizeof text, "\tcvtdq2ps %%xmm%d, %%xmm%d\n", a-XMM0, d-XMM0); vx_raw(L, text);
		} else {
			char text[100]; snprintf(text, sizeof text, "\tpshufd $78, %%xmm%d, %%xmm14\n", a-XMM0); vx_raw(L, text);
			x64_op3(e, X_MOV, 8, R10, a, XNOREG);
			x64_op3(e, X_CVTI2F, 8, d, R10, XNOREG)->sh = 8;
			x64_op3(e, X_MOV, 8, R10, XMM0+14, XNOREG);
			x64_op3(e, X_CVTI2F, 8, XMM15, R10, XNOREG)->sh = 8;
			snprintf(text, sizeof text, "\tunpcklpd %%xmm15, %%xmm%d\n", d-XMM0); vx_raw(L, text);
		}
		return;
	case VO_VBITSEL: {
		char text[300];
		snprintf(text, sizeof text, "\tmovaps %%xmm%d, %%xmm14\n\tmovaps %%xmm%d, %%xmm15\n"
			"\tpand %%xmm%d, %%xmm14\n\tpandn %%xmm%d, %%xmm15\n\tpor %%xmm14, %%xmm15\n\tmovaps %%xmm15, %%xmm%d\n",
			a-XMM0, a-XMM0, c-XMM0, vx_reg(p[2])-XMM0, d-XMM0);
		vx_raw(L, text); return;
	}
	case VO_VFCMP: {
		int cc = i->fl & 15;
		bool swap = cc == 4 || cc == 5;
		int pred = cc == 0 ? 0 : cc == 1 || cc == 7 ? 4 : cc == 2 || cc == 4 ? 1 : cc == 6 ? 3 : 2;
		const char *op = i->ty == VT_I32X4 ? "cmpps" : "cmppd";
		char text[300];
		snprintf(text, sizeof text, "\tmovaps %%xmm%d, %%xmm14\n\t%s $%d, %%xmm%d, %%xmm14\n",
			(swap ? c : a)-XMM0, op, pred, (swap ? a : c)-XMM0);
		vx_raw(L, text);
		if (cc == 7) {
			snprintf(text, sizeof text, "\tmovaps %%xmm%d, %%xmm15\n\t%s $7, %%xmm%d, %%xmm15\n\tpand %%xmm15, %%xmm14\n", a-XMM0, op, c-XMM0);
			vx_raw(L, text);
		}
		x64_mov(e, d, XMM0+14); return;
	}
	case VO_VICMP: {
		int cc = i->fl & 15;
		bool swap = cc == 2 || cc == 5, invert = cc == 1 || cc == 3 || cc == 5;
		char text[260];
		snprintf(text, sizeof text, "\tmovaps %%xmm%d, %%xmm14\n\t%s %%xmm%d, %%xmm14\n",
			(swap ? c : a)-XMM0, cc < 2 ? "pcmpeqd" : "pcmpgtd", (swap ? a : c)-XMM0);
		vx_raw(L, text);
		if (invert) vx_raw(L, "\tpcmpeqd %xmm15, %xmm15\n\tpxor %xmm15, %xmm14\n");
		x64_mov(e, d, XMM0+14); return;
	}
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
	case VO_SMULHI: case VO_UMULHI:
		x64_op3(e,X_MOV,sz,R11,c,XNOREG);
		x64_op3(e,i->op==VO_SMULHI?X_SMULW:X_MULW,sz,XNOREG,R11,XNOREG);return;
	case VO_SDIV: case VO_UDIV: case VO_SREM: case VO_UREM: {
		bool sign = i->op == VO_SDIV || i->op == VO_SREM;
		x64_op3(e, X_MOV, sz, R11, c, XNOREG);
		if (sign) x64_op3(e, X_CQO, sz, RDX, RAX, XNOREG);
		else x64_op3(e, X_XOR, 4, RDX, RDX, RDX);
		x64_op3(e, sign ? X_IDIV : X_DIV, sz, XNOREG, R11, XNOREG);
		return;
	}
	case VO_VINSERT: {
		char text[320];
		int width = vx_size(vir_velem(i->ty));
		static const unsigned swap32[4] = {228,225,198,39};
		unsigned perm = width == 4 ? swap32[i->c] : i->c ? 78 : 228;
		/* Save the scalar first: the result may share its register. Swap the
		 * destination lane to lane zero, replace just that lane, then swap back. */
		x64_op3(e, X_MOV, width, XMM0+14, c, XNOREG);
		snprintf(text, sizeof text,
			"\tpshufd $%u, %%xmm%d, %%xmm15\n\t%s %%xmm14, %%xmm15\n\tpshufd $%u, %%xmm15, %%xmm%d\n",
			perm, a-XMM0, width == 4 ? "movss" : "movsd", perm, d-XMM0);
		vx_raw(L,text); return;
	}
	case VO_VEXTRACT: {
		char text[160];
		snprintf(text,sizeof text,"\tmovaps %%xmm%d, %%xmm14\n\tpsrldq $%d, %%xmm14\n",a-XMM0,(int)i->b * vx_size(i->ty));
		vx_raw(L,text); x64_op3(e,X_MOV,vx_size(i->ty),d,XMM0+14,XNOREG); return;
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
	case VO_AFENCE: x64_put(e,X_MFENCE); return;
	case VO_ASTORE:
		x64_op3(e,X_MOV,sz=vx_size(L->f->ins[i->b].ty),R10,c,XNOREG);
		x64_op3(e,X_XCHG,1<<VIF_W(i->fl),R10,a,XNOREG);return;
	case VO_SWITCH: vx_switch(L,b,i,a); return;
	case VO_BR: vx_edge(L, b, 0); x64_jump(e, L->label[i->a]); return;
	case VO_BRIF: {
		uint32_t yes = vx_target(L,b,0,i->b), no=vx_target(L,b,1,i->c);
		uint8_t cc=vx_condition(L,b,v,i->a,a);
		/* Let the next emitted block fall through, including the true edge.
		 * A deferred edge-copy stub is a different label and cannot fall through. */
		if(b+1<L->f->nblk && yes==L->label[b+1]) {
			uint32_t swap=yes;yes=no;no=swap;cc^=1;
		}
		rec *r = x64_put(e, X_JCC); r->cond = cc; r->label = yes;
		x64_jump(e, no); return;
	}
	case VO_RET: x64_jump(e, L->ret); return;
	case VO_TRAP: x64_put(e, X_UD2); return;
	default: abort();
	}
}

/* setjmp headers do not universally attach returns_twice to their declaration. */
static bool vx_twice(ecx *e, const vir_ins *callee)
{
	if (callee->op != VO_SYMADDR || RKIND(callee->a) != RK_SYM) return false;
	cc *c = e->c;
	uint32_t sym = canon_sym(c, RIDX(callee->a));
	return (sym_attrs(c, sym)->flags & AF_TAIL_RETURNS_TWICE) ||
		EM_IDENT_IS(c->syms[sym].name, "setjmp") || EM_IDENT_IS(c->syms[sym].name, "_setjmp") ||
		EM_IDENT_IS(c->syms[sym].name, "sigsetjmp") || EM_IDENT_IS(c->syms[sym].name, "__sigsetjmp");
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
		if (f->ins[i].op == VO_CALL && f->extra[f->ins[i].c] >= VIR_FIX_N) {
			*why = "VIR x86-64: too many call arguments";
			return false;
		} else if (f->ins[i].op == VO_CALL && vx_twice(e, &f->ins[f->ins[i].a])) {
			*why = "VIR x86-64: returns_twice";
			return false;
		} else if (!vx_supported(f, i)) {
			static _Thread_local char reason[160];
			snprintf(reason, sizeof reason, "VIR x86-64: unsupported %s.%s", vir_optab[f->ins[i].op].name,
				 vir_tyname[f->ins[i].ty]);
			*why = reason;
			return false;
		}
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
	L.edgelab=V->xedgelab=vx_grow(V->xedgelab,&V->cxedgelab,V->ao.nedge+1,sizeof(uint32_t));
	memset(L.edgelab,0,V->ao.nedge*sizeof(uint32_t));
	L.uses=V->xuses=vx_grow(V->xuses,&V->cxuses,f->nins+1,1);
	vir_uses(f,L.uses);
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
	for(uint32_t g=0;g<V->ao.nedge;g++)if(L.edgelab[g]) {
		const vir_aedge *edge=&V->ao.edge[g];
		x64_label(e,L.edgelab[g]);vx_edge(&L,edge->from,edge->k);x64_jump(e,L.label[edge->to]);
	}
	x64_label(e, L.ret);
	x64_finish(e, sym);
	e->vtry = false;
	*why = NULL; return true;
}
