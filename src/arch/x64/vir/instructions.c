/* x86-64 lowering of VIR: one instruction. */

#include "lowering-internal.h"

VPRIV void vx_ins(vx *L, uint32_t b, uint32_t v, const uint8_t *p, uint32_t n, uint8_t res)
{
	ecx *e = L->e;
	const vir_ins *i = &L->f->ins[v];
	int d = res == VR_NONE ? (vir_rclass(i->ty) ? XMM0 + 14 : L->gtmp) : vx_reg(res);
	int a = n ? vx_reg(p[0]) : XNOREG, c = n > 1 ? vx_reg(p[1]) : XNOREG;
	int sz = vx_size(i->ty);
	static const uint8_t alu[VO_N] = { [VO_ADD]=X_ADD, [VO_PTRADD]=X_ADD, [VO_SUB]=X_SUB, [VO_PSUB]=X_SUB,
		[VO_MUL]=X_IMUL, [VO_AND]=X_AND, [VO_OR]=X_OR, [VO_XOR]=X_XOR,
		[VO_FADD]=X_FADD, [VO_FSUB]=X_FSUB, [VO_FMUL]=X_FMUL, [VO_FDIV]=X_FDIV };
	if(L->testv) {
		if(v<L->testv)return;
		if(v==L->testv) {
			rec *r=x64_op3(e,X_TESTM,L->testsize,XNOREG,L->membase,XNOREG);
			r->r[3]=L->indexed?L->memindex:XNOREG;r->sh=L->memsh;r->off=L->memoff;r->ref=(uint32_t)L->testimm;
			L->cmp=v;L->cmpblock=b;L->setrec=e->x64->nin;
			x64_op3(e,X_SETCC,4,d,XNOREG,XNOREG)->cond=(i->fl&15)?CC_NE:CC_E;
			L->testv=0;return;
		}
	}
	if(L->memskip==v){L->memskip=0;return;}
	if(L->scaledskip==v){L->scaledskip=0;return;}
	if(L->bev) {
		if(v<L->bev)return;
		if(v==L->bev) {
			lval at={.k=L_REG,.r=L->bebase,.off=L->beoff};
			if(L->beindexed) {
				rec *r=x64_op3(e,X_LDX,L->bewidth,d,at.r,XNOREG);r->r[3]=L->beindex;r->sh=L->besh;r->off=at.off;
			} else x64_mem(e,X_LD,L->bewidth,d,&at,false);
			x64_op3(e,X_BSWAP,L->bewidth,d,d,XNOREG);L->bev=0;return;
		}
	}
	/* A single-use ordinary unsigned load, constant mask and zero comparison
	 * can test the exact source width in memory. Preserve the address until
	 * the comparison; neither skipped definition may spill or have edits. */
	if(i->op==VO_LOAD && (i->ty==VT_I32 || i->ty==VT_I64) && !(i->fl&(VIF_VOL|VIF_MS)) &&
	   v+2<L->f->blk[b].first+L->f->blk[b].n && L->uses[v]==1 && L->o->slot[v]==VIR_NOBLK) {
		const vir_ins *mask=&L->f->ins[v+1],*cmp=&L->f->ins[v+2];int64_t k,zero;
		if(mask->op==VO_AND && mask->ty==i->ty && mask->a==v && vx_imm(L->f,mask->b,&k) &&
		   L->uses[v+1]==1 && L->o->slot[v+1]==VIR_NOBLK && cmp->op==VO_ICMP &&
		   (cmp->fl&15)<2 && cmp->a==v+1 && vx_imm(L->f,cmp->b,&zero) && !zero &&
		   vx_foldedits(L,b,v+1,XNOREG) && vx_foldedits(L,b,v+2,XNOREG)) {
			L->testv=v+2;L->testsize=1<<VIF_W(i->fl);L->testimm=k;
			if(L->memv==i->a)L->memv=0;
			else {L->membase=a;L->memoff=0;L->indexed=false;L->memsh=0;}
			return;
		}
	}
	/* Fold an adjacent private scalar load into its comparison. Narrow
	 * equality with a representable immediate needs no promotion bits. */
	if(i->op==VO_LOAD && (i->ty==VT_I32 || i->ty==VT_I64) && !(i->fl&VIF_VOL) &&
	   L->uses[v]==1 && L->o->slot[v]==VIR_NOBLK && v+1<L->f->blk[b].first+L->f->blk[b].n &&
	   vx_foldedits(L,b,v+1,XNOREG)) {
		const vir_ins *next=&L->f->ins[v+1];unsigned width=1<<VIF_W(i->fl);int64_t k=0;
		bool side=next->b==v,ok=false;
		if(next->op==VO_ICMP && (next->a==v || side)) {
			uint32_t other=side?next->a:next->b;
			bool imm=vx_imm(L->f,other,&k);
			if(width==vx_size(i->ty))ok=!imm || (!side && (width==4 || k==(int32_t)k));
			else if(!side && imm && (next->fl&15)<2) {
				int64_t lo=(i->fl&VIF_MS)?-(INT64_C(1)<<(width*8-1)):0;
				int64_t hi=(INT64_C(1)<<(width*8-!!(i->fl&VIF_MS)))-1;
				ok=k>=lo && k<=hi;
			}
		}
		if(ok) {
			L->cmpmemv=v+1;L->cmpmemsize=width;L->cmpmemside=side;
			if(L->memv==i->a)L->memv=0;
			else {L->membase=a;L->memoff=0;L->indexed=false;L->memsh=0;}
			return;
		}
	}
	if(i->op==VO_LOAD) {
		uint32_t end;unsigned width=vx_readword(L,b,v,&end);
		if(width) {
			L->bev=end;L->bewidth=width;L->bebase=a;L->beoff=0;L->beindexed=false;
			if(L->memv==i->a) {
				L->bebase=L->membase;L->beoff=L->memoff;L->beindexed=L->indexed;
				L->beindex=L->memindex;L->besh=L->memsh;L->memv=0;
			}
			return;
		}
	}
	/* The walker also spells pointer arithmetic as add.i64 + i2p. The
	 * cast is an identity; keep both inputs until the immediate memory use. */
	if(i->op==VO_ADD || i->op==VO_PTRADD) {
		uint32_t end;unsigned width=vx_beaddr(L,b,v,&end);
		if(width) {
			L->bev=end;L->bewidth=width;L->bebase=a;L->beoff=0;
			L->beindexed=!vx_imm(L->f,i->b,&L->beoff);
			L->beindex=L->scaledv==i->b?L->scaleindex:c;
			L->besh=L->scaledv==i->b?L->scalesh:0;return;
		}
	}
	if(vx_castmem(L,b,v)) {
		L->memv=v+1;L->memskip=v+1;L->membase=a;L->memoff=0;
		L->indexed=!vx_imm(L->f,i->b,&L->memoff);
		L->memindex=L->scaledv==i->b?L->scaleindex:c;
		L->memsh=L->scaledv==i->b?L->scalesh:0;return;
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
	/* Keep the unscaled signed index in the product's allocated register.
	 * The shared proof permits extending it before the hardware scale. */
	unsigned signed_scale;
	if(i->op==VO_SHL && i->ty==VT_I32 && (i->fl&VIF_NSW) &&
	   vir_sext_shift(L->f,v,&signed_scale) && signed_scale<=3 && L->uses[v]==1 &&
	   L->o->slot[v]==VIR_NOBLK && v+3<L->f->blk[b].first+L->f->blk[b].n &&
	   L->uses[v+1]==1 && L->o->slot[v+1]==VIR_NOBLK && L->f->ins[v+2].b==v+1 &&
	   vx_addrmem(L,b,v+2) && vx_foldedits(L,b,v+1,XNOREG) && vx_foldedits(L,b,v+2,d)) {
		if(vx_sign64(L,i->a))x64_mov(e,d,a);
		else x64_op3(e,X_EXT,4,d,a,XNOREG)->cond=1;
		L->scaledv=v+1;L->scaledskip=v+1;L->scaleindex=d;L->scalesh=signed_scale;return;
	}
	if ((i->op==VO_MUL || i->op==VO_SHL) && i->ty==VT_I64 && L->uses[v]==1 && L->o->slot[v]==VIR_NOBLK &&
	    v+2<L->f->blk[b].first+L->f->blk[b].n && L->f->ins[v+1].b==v &&
	    vx_addrmem(L,b,v+1) &&
	    vx_foldedits(L,b,v+1,a)) {
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
		if(vx_smallmem(L->f,i)){vx_memblock(L,i,a,c);return;}
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
				r = vir_rclass(L->f->ins[value].ty) ? VR_MAKE(1, 14) : L->gtmp;
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
		if(VIF_W(i->fl)==2 && ((i->fl&VIF_MS)?vx_sign64(L,i->a):vx_clear32(L->f,i->a))) {
			x64_mov(e,d,a);return;
		}
		rec *r = x64_op3(e, X_EXT, 1 << VIF_W(i->fl), d, a, XNOREG);
		r->cond = !!(i->fl & VIF_MS); return;
	}
	case VO_SHL: case VO_LSHR: case VO_ASHR:
		int64_t count;
		if (vx_imm(L->f,i->b,&count) && count>=0 && count<(i->ty==VT_I32?32:64)) {
			x64_op3(e,i->op==VO_SHL?X_SHLI:i->op==VO_LSHR?X_SHRI:X_SARI,sz,d,a,XNOREG)->imm=count;return;
		}
		/* The result may be rcx: moving the first operand there would destroy cl.
		 * Shift in the reserved scratch, then publish the allocated result. */
		x64_mov(e, L->gtmp, a);
		x64_op3(e, i->op == VO_SHL ? X_SHL : i->op == VO_LSHR ? X_SHR : X_SAR, sz, L->gtmp, L->gtmp, c);
		x64_op3(e, X_MOV, sz, d, L->gtmp, XNOREG); return;
	case VO_ICMP: {
		static const uint8_t cc[] = { CC_E, CC_NE, CC_L, CC_LE, CC_G, CC_GE, CC_B, CC_BE, CC_A, CC_AE };
		if(L->cmpmemv==v) {
			int other=L->cmpmemside?a:c;
			rec *r=x64_op3(e,X_CMPM,L->cmpmemsize,XNOREG,L->membase,other);
			r->r[3]=L->indexed?L->memindex:XNOREG;r->sh=L->memsh;r->off=L->memoff;r->cond=L->cmpmemside;
			if(!L->cmpmemside && p[1]==VR_NONE) {int64_t k;vx_imm(L->f,i->b,&k);r->r[2]=XNOREG;r->ref=(uint32_t)k;}
			L->cmpmemv=0;
		} else
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
		int tmp = vir_rclass(i->ty) ? XMM0 + 14 : L->gtmp;
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
		if (i->op == VO_U2F) { x64_op3(e, X_MOV, 4, L->gtmp, a, XNOREG); a = L->gtmp; from = 8; }
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
		snprintf(text, sizeof text, "\tpcmpeqd %%xmm14, %%xmm14\n\t%s $%d, %%xmm14\n\t%s %%xmm%d, %%xmm14\n\tmovaps %%xmm14, %%xmm%d\n",
			i->op == VO_FNEG ? (wide ? "psllq" : "pslld") : (wide ? "psrlq" : "psrld"),
			i->op == VO_FNEG ? (wide ? 63 : 31) : 1,
			i->op == VO_FNEG ? "pxor" : "pand", a-XMM0, d-XMM0);
		vx_raw(L, text);
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
			x64_op3(e, X_MOV, 8, L->gtmp, a, XNOREG);
			x64_op3(e, X_CVTI2F, 8, d, L->gtmp, XNOREG)->sh = 8;
			x64_op3(e, X_MOV, 8, L->gtmp, XMM0+14, XNOREG);
			x64_op3(e, X_CVTI2F, 8, XMM15, L->gtmp, XNOREG)->sh = 8;
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
		x64_movi(e, L->gtmp, i->ty == VT_I32X4 ? 0x0000000100000000LL : 0);
		x64_op3(e, X_MOV, 8, d, L->gtmp, XNOREG);
		x64_movi(e, L->gtmp, i->ty == VT_I32X4 ? 0x0000000300000002LL : 1);
		x64_op3(e, X_MOV, 8, (XMM0 + 14), L->gtmp, XNOREG);
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
		x64_op3(e,X_MOV,sz=vx_size(L->f->ins[i->b].ty),L->gtmp,c,XNOREG);
		x64_op3(e,X_XCHG,1<<VIF_W(i->fl),L->gtmp,a,XNOREG);return;
	case VO_SWITCH: vx_switch(L,b,i,a); return;
	case VO_BR: vx_edge(L, b, 0); vx_goto(L,b,i->a); return;
	case VO_BRIF: {
		uint32_t yes = vx_target(L,b,0,i->b), no=vx_target(L,b,1,i->c), nt=i->c;
		uint32_t next=vx_next(L);
		uint8_t cc=vx_condition(L,b,v,i->a,a);
		/* Let the next placed block fall through, including the true edge.
		 * A deferred edge-copy stub is a different label and cannot fall through. */
		if(next!=VIR_NOBLK && yes==L->label[next]) {
			uint32_t swap=yes;yes=no;no=swap;cc^=1;nt=i->b;
		}
		rec *r = x64_put(e, X_JCC); r->cond = cc; r->label = yes;
		if(no==L->label[L->chase[nt]])vx_goto(L,b,nt);else x64_jump(e,no);return;
	}
	case VO_RET: x64_jump(e, L->ret); return;
	case VO_TRAP: x64_put(e, X_UD2); return;
	default: abort();
	}
}
