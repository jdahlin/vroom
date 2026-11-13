/* x86-64 lowering of VIR: constants, memory operands and the folds into one instruction. */

#include "lowering-internal.h"

VPRIV bool vx_imm(const vir_fn *f, uint32_t v, int64_t *value)
{
	const vir_ins *i=&f->ins[v];
	if (i->op!=VO_ICONST) return false;
	int64_t k=i->ty==VT_I32?(int64_t)(int32_t)i->b:vx_bits(i);
	if (k!=(int32_t)k) return false;
	*value=k;return true;
}

VPRIV bool vx_smallmem(const vir_fn *f,const vir_ins *i)
{
	const vir_ins *count=&f->ins[i->c];
	if(count->op!=VO_ICONST || (i->fl&VIF_VOL))return false;
	uint64_t n=(uint64_t)vx_bits(count);
	if(i->op==VO_MEMCPY)return n<=128;
	const vir_ins *byte=&f->ins[i->b];
	return n<=256 && byte->op==VO_ICONST && !(byte->b&255);
}

VPRIV bool vx_supported(const vir_fn *f, uint32_t v)
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

/* Edits are sorted by instruction. A folded index must survive the intervening
 * reloads/rematerializations; memory consumers require no intervening edit. */
VPRIV bool vx_foldedits(vx *L,uint32_t b,uint32_t pos,int keep)
{
	uint32_t lo=L->o->boff[L->f->nblk+1+b],hi=L->o->boff[L->f->nblk+2+b],end=hi;
	while(lo<hi) {uint32_t m=lo+(hi-lo)/2;if(L->o->edit[m].pos<pos)lo=m+1;else hi=m;}
	for(uint32_t k=lo;k<end && L->o->edit[k].pos==pos;k++) {
		const vir_edit *edit=&L->o->edit[k];
		if(keep==XNOREG || (edit->kind!=VE_REMAT && edit->kind!=VE_RELOAD) || vx_reg(edit->dst)==keep)return false;
	}
	return true;
}

VPRIV bool vx_memnext(vx *L,uint32_t b,uint32_t v)
{
	return v+1<L->f->blk[b].first+L->f->blk[b].n && L->uses[v]==1 && L->o->slot[v]==VIR_NOBLK &&
		L->f->ins[v+1].a==v && (L->f->ins[v+1].op==VO_LOAD || L->f->ins[v+1].op==VO_STORE) &&
		vx_foldedits(L,b,v+1,XNOREG);
}

/* Integer-address form of the same single memory consumer. */
VPRIV bool vx_castmem(vx *L,uint32_t b,uint32_t v)
{
	const vir_fn *f=L->f;
	return f->ins[v].op==VO_ADD && f->ins[v].ty==VT_I64 && L->uses[v]==1 && L->o->slot[v]==VIR_NOBLK &&
	 v+2<f->blk[b].first+f->blk[b].n && f->ins[v+1].op==VO_I2P && f->ins[v+1].a==v &&
	 vx_memnext(L,b,v+1) && vx_foldedits(L,b,v+1,XNOREG);
}

/* Exact consecutive big-endian byte assembly, with no observable access or
 * allocator edit interleaved. Never widen beyond the bytes actually read. */
VPRIV unsigned vx_readword(vx *L,uint32_t b,uint32_t v,uint32_t *end)
{
	unsigned width=vir_read_be(L->f,L->uses,v,L->f->blk[b].first+L->f->blk[b].n,end);
	if(!width)return 0;
	for(uint32_t k=v;k<*end;k++)
	 if(L->o->slot[k]!=VIR_NOBLK || !vx_foldedits(L,b,k+1,XNOREG))return 0;
	return width;
}

/* An address private to one exact byte-assembly expression. */
VPRIV unsigned vx_beaddr(vx *L,uint32_t b,uint32_t v,uint32_t *end)
{
	const vir_fn *f=L->f;const vir_ins *i=&f->ins[v];
	uint32_t ptr=v,load=v+1;
	if(L->o->slot[v]!=VIR_NOBLK)return 0;
	if(i->op==VO_ADD && i->ty==VT_I64) {
		if(L->uses[v]!=1 || v+2>=f->blk[b].first+f->blk[b].n ||
		   f->ins[v+1].op!=VO_I2P || f->ins[v+1].a!=v || L->o->slot[v+1]!=VIR_NOBLK ||
		   !vx_foldedits(L,b,v+1,XNOREG))return 0;
		ptr=v+1;load=v+2;
	} else if(i->op!=VO_PTRADD)return 0;
	if(load>=f->blk[b].first+f->blk[b].n || f->ins[load].a!=ptr || !vx_foldedits(L,b,load,XNOREG))return 0;
	unsigned width=vx_readword(L,b,load,end);
	return width && L->uses[ptr]==width?width:0;
}

VPRIV bool vx_addrmem(vx *L,uint32_t b,uint32_t v)
{
	uint32_t end;
	return (L->f->ins[v].op==VO_PTRADD && vx_memnext(L,b,v)) || vx_castmem(L,b,v) || vx_beaddr(L,b,v,&end);
}

/* An IR i32 does not itself guarantee zero upper bits: parameters, calls,
 * signed narrow loads and identity truncations can retain arbitrary bits. */
VPRIV bool vx_clear32(const vir_fn *f,uint32_t v)
{
	const vir_ins *i=&f->ins[v];
	if(i->ty!=VT_I32)return false;
	switch(i->op) {
	case VO_ICONST:return !(i->b>>31);
	case VO_LOAD: case VO_EXT:return !(i->fl&VIF_MS);
	case VO_ADD: case VO_SUB: case VO_MUL: case VO_AND: case VO_OR: case VO_XOR:
	case VO_NEG: case VO_NOT: case VO_ICMP: case VO_FCMP: case VO_F2S: case VO_F2U:
	case VO_SDIV: case VO_UDIV: case VO_SREM: case VO_UREM: case VO_SMULHI: case VO_UMULHI:return true;
	case VO_BITCAST:return vir_rclass(f->ins[i->a].ty);
	default:return false;
	}
}

/* Whether an i32 holder already has the physical sign extension required
 * by a signed index. A 4-byte spill reload destroys a negative sign extension. */
VPRIV bool vx_sign64(vx *L,uint32_t v)
{
	const vir_ins *source=&L->f->ins[v];
	if(source->ty!=VT_I32)return false;
	if((source->op==VO_LOAD || source->op==VO_EXT) && (source->fl&VIF_MS) &&
				VIF_W(source->fl)<=2 && L->o->slot[v]==VIR_NOBLK)return true;
	return vx_clear32(L->f,v) && !(vir_mask32(L->f,v,8)>>31);
}

/* Small constant blocks do not need REP's fixed registers or their clobbers.
 * SSE2 handles complete 16-byte pieces; scalar tails never overread. */
VPRIV void vx_memblock(vx *L,const vir_ins *i,int dst,int src)
{
	ecx *e=L->e;
	uint64_t n=(uint64_t)vx_bits(&L->f->ins[i->c]);
	bool copy=i->op==VO_MEMCPY;
	if(!copy) {
		if(n>=16)x64_op3(e,X_FXOR,16,XMM0+14,XMM0+14,XMM0+14);
		if(n&15)x64_movi(e,L->gtmp,0);
	}
	for(uint64_t off=0;off<n;) {
		unsigned width=n-off>=16?16:n-off>=8?8:n-off>=4?4:n-off>=2?2:1;
		int scratch=width==16?XMM0+14:L->gtmp;
		lval from={.k=L_REG,.r=src,.off=(int64_t)off},to={.k=L_REG,.r=dst,.off=(int64_t)off};
		if(copy)x64_mem(e,X_LD,width,scratch,&from,false);
		x64_mem(e,X_ST,width,scratch,&to,false);off+=width;
	}
}

VPRIV uint8_t vx_condition(vx *L,uint32_t b,uint32_t v,uint32_t value,int reg)
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
