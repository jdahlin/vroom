/* x86-64 lowering of VIR: the SysV argument registers and the fixed operands of calls, parameters, switches and block copies. */

#include "lowering-internal.h"

/* Only one-piece scalar ABI groups reach this target. */
VPRIV uint8_t vx_arg(const vir_fn *f, uint32_t sig, uint32_t g, int *stack)
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

VPRIV void vx_fixed(const vir_fn *f, uint32_t v, vir_fix *x, void *ctx)
{
	const vir_ins *i = &f->ins[v];
	const vx *target=ctx;
	bool r10=target && target->r10;
	if (i->op == VO_PARAM) {
		int stack;
		x->res = vx_arg(f, f->sig, i->a, &stack);
	} else if(i->op==VO_SWITCH && r10) {
		/* A private dense selector can be produced directly in the dispatch
		 * register. Range chains and shared selectors keep their original. */
		if(target->uses[i->a]==1 && vx_dense_switch(f,i,NULL,NULL)) {
			x->nopnd=1;x->opnd[0]=R10;x->clob[0]=UINT64_C(1)<<R10;
		} else x->eclob[0]=UINT64_C(1)<<R10;
	} else if (i->op == VO_MEMCPY || i->op == VO_MEMSET) {
		x->nopnd = 3;
		if(vx_smallmem(f,i)) {x->mem=i->op==VO_MEMSET?6:4;return;}
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
		x->clob[0] = r10?X_TEMPS:X_TEMPS & ~(1u << R10);
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
	case VO_EXT: case VO_TRUNC: case VO_P2I: case VO_I2P:
	case VO_FADD: case VO_FSUB: case VO_FMUL: case VO_FDIV:
		x->fl|=VFX_TIED;break;
	default:break;
	}
}
