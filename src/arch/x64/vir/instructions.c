/* x86-64 lowering of VIR: one instruction. */

#include "lowering-internal.h"

VPRIV void x64_vir_insn_lower(vx *L, uint32_t b, uint32_t v, const uint8_t *p, uint32_t n, uint8_t res)
{
	ecx *e = L->e;
	const vir_insn *i = &L->f->insns[v];
	int d = res == VIR_REGISTER_NONE ? (vir_register_class(i->type) ? XMM0 + 14 : L->gtmp) : x64_vir_register_number(res);
	int a = n ? x64_vir_register_number(p[0]) : XNOREG, c = n > 1 ? x64_vir_register_number(p[1]) : XNOREG;
	int sz = x64_vir_type_size(i->type);
	static const uint8_t alu[VIR_OP_COUNT] = { [VIR_OP_ADD]=X_ADD, [VIR_OP_PTRADD]=X_ADD, [VIR_OP_SUB]=X_SUB, [VIR_OP_PSUB]=X_SUB,
		[VIR_OP_MUL]=X_IMUL, [VIR_OP_AND]=X_AND, [VIR_OP_OR]=X_OR, [VIR_OP_XOR]=X_XOR,
		[VIR_OP_FADD]=X_FADD, [VIR_OP_FSUB]=X_FSUB, [VIR_OP_FMUL]=X_FMUL, [VIR_OP_FDIV]=X_FDIV };
	if(L->testv) {
		if(v<L->testv)return;
		if(v==L->testv) {
			rec *r=x64_op3(e,X_TESTM,L->testsize,XNOREG,L->membase,XNOREG);
			r->r[3]=L->indexed?L->memindex:XNOREG;r->sh=L->memsh;r->off=L->memoff;r->ref=(uint32_t)L->testimm;
			L->cmp=v;L->cmpblock=b;L->setrec=e->x64->nin;
			x64_op3(e,X_SETCC,4,d,XNOREG,XNOREG)->cond=(i->flags&15)?CC_NE:CC_E;
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
	if(i->op==VIR_OP_LOAD && (i->type==VIR_TYPE_I32 || i->type==VIR_TYPE_I64) && !(i->flags&(VIR_INSN_FLAG_VOL|VIR_INSN_FLAG_MS)) &&
	   v+2<L->f->blocks[b].first+L->f->blocks[b].insn_count && L->uses[v]==1 && L->o->slot[v]==VIR_BLOCK_NONE) {
		const vir_insn *mask=&L->f->insns[v+1],*cmp=&L->f->insns[v+2];int64_t k,zero;
		if(mask->op==VIR_OP_AND && mask->type==i->type && mask->a==v && x64_vir_value_immediate(L->f,mask->b,&k) &&
		   L->uses[v+1]==1 && L->o->slot[v+1]==VIR_BLOCK_NONE && cmp->op==VIR_OP_ICMP &&
		   (cmp->flags&15)<2 && cmp->a==v+1 && x64_vir_value_immediate(L->f,cmp->b,&zero) && !zero &&
		   x64_vir_fold_edits_check(L,b,v+1,XNOREG) && x64_vir_fold_edits_check(L,b,v+2,XNOREG)) {
			L->testv=v+2;L->testsize=1<<VIR_INSN_FLAG_WIDTH_LOG2(i->flags);L->testimm=k;
			if(L->memv==i->a)L->memv=0;
			else {L->membase=a;L->memoff=0;L->indexed=false;L->memsh=0;}
			return;
		}
	}
	/* Fold an adjacent private scalar load into its comparison. Narrow
	 * equality with a representable immediate needs no promotion bits. */
	if(i->op==VIR_OP_LOAD && (i->type==VIR_TYPE_I32 || i->type==VIR_TYPE_I64) && !(i->flags&VIR_INSN_FLAG_VOL) &&
	   L->uses[v]==1 && L->o->slot[v]==VIR_BLOCK_NONE && v+1<L->f->blocks[b].first+L->f->blocks[b].insn_count &&
	   x64_vir_fold_edits_check(L,b,v+1,XNOREG)) {
		const vir_insn *next=&L->f->insns[v+1];unsigned width=1<<VIR_INSN_FLAG_WIDTH_LOG2(i->flags);int64_t k=0;
		bool side=next->b==v,ok=false;
		if(next->op==VIR_OP_ICMP && (next->a==v || side)) {
			uint32_t other=side?next->a:next->b;
			bool imm=x64_vir_value_immediate(L->f,other,&k);
			if(width==x64_vir_type_size(i->type))ok=!imm || (!side && (width==4 || k==(int32_t)k));
			else if(!side && imm && (next->flags&15)<2) {
				int64_t lo=(i->flags&VIR_INSN_FLAG_MS)?-(INT64_C(1)<<(width*8-1)):0;
				int64_t hi=(INT64_C(1)<<(width*8-!!(i->flags&VIR_INSN_FLAG_MS)))-1;
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
	if(i->op==VIR_OP_LOAD) {
		uint32_t end;unsigned width=x64_vir_word_read_match(L,b,v,&end);
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
	if(i->op==VIR_OP_ADD || i->op==VIR_OP_PTRADD) {
		uint32_t end;unsigned width=x64_vir_big_endian_address_match(L,b,v,&end);
		if(width) {
			L->bev=end;L->bewidth=width;L->bebase=a;L->beoff=0;
			L->beindexed=!x64_vir_value_immediate(L->f,i->b,&L->beoff);
			L->beindex=L->scaledv==i->b?L->scaleindex:c;
			L->besh=L->scaledv==i->b?L->scalesh:0;return;
		}
	}
	if(x64_vir_memory_cast_fold(L,b,v)) {
		L->memv=v+1;L->memskip=v+1;L->membase=a;L->memoff=0;
		L->indexed=!x64_vir_value_immediate(L->f,i->b,&L->memoff);
		L->memindex=L->scaledv==i->b?L->scaleindex:c;
		L->memsh=L->scaledv==i->b?L->scalesh:0;return;
	}
	if (i->op == VIR_OP_MUL && i->type == VIR_TYPE_I32X4) {
		char text[600];
		snprintf(text, sizeof text,
			"\tmovaps %%xmm%d, %%xmm14\n\tmovaps %%xmm%d, %%xmm15\n"
			"\tpsrldq $4, %%xmm14\n\tpsrldq $4, %%xmm15\n\tpmuludq %%xmm15, %%xmm14\n\tpshufd $8, %%xmm14, %%xmm14\n"
			"\tmovaps %%xmm%d, %%xmm15\n\tmovaps %%xmm%d, %%xmm%d\n\tpmuludq %%xmm15, %%xmm%d\n"
			"\tpshufd $8, %%xmm%d, %%xmm%d\n\tpunpckldq %%xmm14, %%xmm%d\n",
			a-XMM0, c-XMM0, c-XMM0, a-XMM0, d-XMM0, d-XMM0, d-XMM0, d-XMM0, d-XMM0);
		x64_vir_text_append(L, text); return;
	}
	/* Keep the unscaled signed index in the product's allocated register.
	 * The shared proof permits extending it before the hardware scale. */
	unsigned signed_scale;
	if(i->op==VIR_OP_SHL && i->type==VIR_TYPE_I32 && (i->flags&VIR_INSN_FLAG_NSW) &&
	   vir_sign_extend_shift_match(L->f,v,&signed_scale) && signed_scale<=3 && L->uses[v]==1 &&
	   L->o->slot[v]==VIR_BLOCK_NONE && v+3<L->f->blocks[b].first+L->f->blocks[b].insn_count &&
	   L->uses[v+1]==1 && L->o->slot[v+1]==VIR_BLOCK_NONE && L->f->insns[v+2].b==v+1 &&
	   x64_vir_address_memory_fold(L,b,v+2) && x64_vir_fold_edits_check(L,b,v+1,XNOREG) && x64_vir_fold_edits_check(L,b,v+2,d)) {
		if(x64_vir_value_is_sign_extended(L,i->a))x64_mov(e,d,a);
		else x64_op3(e,X_EXT,4,d,a,XNOREG)->cond=1;
		L->scaledv=v+1;L->scaledskip=v+1;L->scaleindex=d;L->scalesh=signed_scale;return;
	}
	if ((i->op==VIR_OP_MUL || i->op==VIR_OP_SHL) && i->type==VIR_TYPE_I64 && L->uses[v]==1 && L->o->slot[v]==VIR_BLOCK_NONE &&
	    v+2<L->f->blocks[b].first+L->f->blocks[b].insn_count && L->f->insns[v+1].b==v &&
	    x64_vir_address_memory_fold(L,b,v+1) &&
	    x64_vir_fold_edits_check(L,b,v+1,a)) {
		int64_t k;
		if(x64_vir_value_immediate(L->f,i->b,&k) && (i->op==VIR_OP_SHL ? k>=0 && k<=3 : k==1 || k==2 || k==4 || k==8)) {
			L->scaledv=v;L->scaleindex=a;
			L->scalesh=i->op==VIR_OP_SHL?k:k==8?3:k==4?2:k==2?1:0;
			return;
		}
	}
	if (i->op==VIR_OP_PTRADD && x64_vir_memory_next_fold(L,b,v)) {
		L->memv=v;L->membase=a;L->memoff=0;
		L->indexed=!x64_vir_value_immediate(L->f,i->b,&L->memoff);
		L->memindex=L->scaledv==i->b?L->scaleindex:c;
		L->memsh=L->scaledv==i->b?L->scalesh:0;
		return;
	}
	if (alu[i->op]) {
		if (n>1 && p[1]==VIR_REGISTER_NONE && !vir_register_class(i->type)) {
			static const uint8_t immop[VIR_OP_COUNT]={[VIR_OP_ADD]=X_ADDI,[VIR_OP_PTRADD]=X_ADDI,[VIR_OP_SUB]=X_SUBI,[VIR_OP_PSUB]=X_SUBI,
				[VIR_OP_AND]=X_ANDI,[VIR_OP_OR]=X_ORI,[VIR_OP_XOR]=X_XORI,[VIR_OP_MUL]=X_IMULI};
			int64_t k;x64_vir_value_immediate(L->f,i->b,&k);
			if(i->op==VIR_OP_PTRADD || i->op==VIR_OP_ADD) {
				lval at={.k=L_REG,.r=(uint8_t)a,.off=k};x64_mem(e,X_LEA,sz,d,&at,false);
			} else x64_op3(e,immop[i->op],sz,d,a,XNOREG)->imm=k;
			return;
		}
		if (vir_type_is_vector(i->type)) {
			const char *op = i->op == VIR_OP_ADD ? (i->type == VIR_TYPE_I32X4 ? "paddd" : "paddq") :
				i->op == VIR_OP_SUB ? (i->type == VIR_TYPE_I32X4 ? "psubd" : "psubq") :
				i->op == VIR_OP_AND ? "pand" : i->op == VIR_OP_OR ? "por" : i->op == VIR_OP_XOR ? "pxor" :
				i->op == VIR_OP_FADD ? (i->type == VIR_TYPE_F32X4 ? "addps" : "addpd") :
				i->op == VIR_OP_FSUB ? (i->type == VIR_TYPE_F32X4 ? "subps" : "subpd") :
				i->op == VIR_OP_FMUL ? (i->type == VIR_TYPE_F32X4 ? "mulps" : "mulpd") :
				(i->type == VIR_TYPE_F32X4 ? "divps" : "divpd");
			x64_vir_simd_emit(L, op, d, a, c);
		} else if (i->op==VIR_OP_ADD || i->op==VIR_OP_PTRADD) x64_op3(e,X_LEASCALE,sz,d,a,c)->sh=0;
		else x64_op3(e, alu[i->op], sz, d, a, c);
		return;
	}
	switch (i->op) {
	case VIR_OP_NOP: case VIR_OP_BPARAM: return;
	case VIR_OP_BARRIER: x64_put(e, X_BARRIER); return;
	case VIR_OP_MEMCPY: case VIR_OP_MEMSET:
		if(x64_vir_memory_block_is_small(L->f,i)){x64_vir_memory_block_lower(L,i,a,c);return;}
		x64_put(e, X_REP)->cond = i->op == VIR_OP_MEMSET;
		return;
	case VIR_OP_CALL: {
		uint32_t ng = L->f->extra[i->c];
		int total = 0, nf = 0;
		for (uint32_t k = 0; k < ng; k++) {
			int off;
			uint8_t r = x64_vir_argument_register(L->f, i->b, k, &off);
			if (r == VIR_REGISTER_NONE) total = off - 8;
			else if (VIR_REGISTER_CLASS(r)) nf++;
		}
		total = (total + 15) & ~15;
		if (total) x64_put(e, X_ADJSP)->imm = -total;
		for (uint32_t k = 0; k < ng; k++) {
			int off;
			if (x64_vir_argument_register(L->f, i->b, k, &off) != VIR_REGISTER_NONE) continue;
			uint32_t value = L->f->extra[i->c + 1 + k];
			uint8_t r = p[k + 1];
			if (r == VIR_REGISTER_NONE) {
				r = vir_register_class(L->f->insns[value].type) ? VIR_REGISTER_MAKE(1, 14) : L->gtmp;
				if (value < L->f->constant_count) x64_vir_constant_materialize(L, r, value);
				else x64_vir_slot_access(L, true, r, L->o->slot[value]);
			}
			lval at = { .k = L_REG, .r = RSP, .off = off - 16 };
			x64_mem(e, X_ST, x64_vir_type_size(L->f->insns[value].type), x64_vir_register_number(r), &at, false);
		}
		bool indirect = p[0] != VIR_REGISTER_NONE;
		if (indirect) x64_mov(e, R11, a); /* al is set for variadic calls */
		if (VIR_SIGNATURE_IS_VARIADIC(L->f->extra[i->b])) x64_movi(e, RAX, nf);
		if (indirect) x64_op3(e, X_CALLR, 8, XNOREG, R11, XNOREG);
		else {
			const vir_insn *callee = &L->f->insns[i->a];
			note_ref(e, callee->a);
			x64_put(e, X_CALL)->ref = callee->a;
		}
		if (total) x64_put(e, X_ADJSP)->imm = total;
		e->calls = true;
		return;
	}
	case VIR_OP_PARAM: {
		int stack;
		if (x64_vir_argument_register(L->f, L->f->signature, i->a, &stack) == VIR_REGISTER_NONE) {
			lval at = { .k = L_FRAME, .off = stack };
			x64_mem(e, X_LD, sz, d, &at, false);
		}
		return;
	}
	case VIR_OP_ALOAD: case VIR_OP_LOAD: case VIR_OP_STORE: {
		lval at = { .k = L_REG, .r = a };
		bool folded=L->memv==i->a;
		if(folded) {at.r=L->membase;at.off=L->memoff;L->memv=0;}
		bool load = i->op != VIR_OP_STORE;
		int ty = load ? i->type : L->f->insns[i->b].type;
		int width=vir_type_is_vector(ty)?16:1<<VIR_INSN_FLAG_WIDTH_LOG2(i->flags);
		if(folded && L->indexed) {
			rec *r=x64_op3(e,load?X_LDX:X_STX,width,load?d:XNOREG,at.r,load?XNOREG:c);
			r->r[3]=L->memindex;r->sh=L->memsh;r->cond=!!(i->flags & VIR_INSN_FLAG_MS);r->off=at.off;
		} else x64_mem(e,load?X_LD:X_ST,width,load?d:c,&at,i->flags & VIR_INSN_FLAG_MS);
		return;
	}
	case VIR_OP_NEG: case VIR_OP_NOT:
		if (vir_type_is_vector(i->type)) {
			char text[200];
			snprintf(text, sizeof text, "\t%s %%xmm14, %%xmm14\n\t%s %%xmm%d, %%xmm14\n\tmovaps %%xmm14, %%xmm%d\n",
				i->op == VIR_OP_NEG ? "pxor" : "pcmpeqd",
				i->op == VIR_OP_NOT ? "pxor" : i->type == VIR_TYPE_I32X4 ? "psubd" : "psubq", a-XMM0, d-XMM0);
			x64_vir_text_append(L, text);
		} else x64_op3(e, i->op == VIR_OP_NEG ? X_NEG : X_NOT, sz, d, a, XNOREG);
		return;
	case VIR_OP_P2I: case VIR_OP_I2P: case VIR_OP_TRUNC: case VIR_OP_BITCAST:
		/* An i32 value does not promise anything about the upper GPR bits;
		 * a later unsigned EXT is responsible for clearing them if needed. */
		if (d==a) return;
		x64_op3(e, X_MOV, sz, d, a, XNOREG); return;
	case VIR_OP_EXT: {
		if (VIR_INSN_FLAG_WIDTH_LOG2(i->flags) == 3) { x64_mov(e, d, a); return; }
		if(VIR_INSN_FLAG_WIDTH_LOG2(i->flags)==2 && ((i->flags&VIR_INSN_FLAG_MS)?x64_vir_value_is_sign_extended(L,i->a):x64_vir_value_upper_half_is_clear(L->f,i->a))) {
			x64_mov(e,d,a);return;
		}
		rec *r = x64_op3(e, X_EXT, 1 << VIR_INSN_FLAG_WIDTH_LOG2(i->flags), d, a, XNOREG);
		r->cond = !!(i->flags & VIR_INSN_FLAG_MS); return;
	}
	case VIR_OP_SHL: case VIR_OP_LSHR: case VIR_OP_ASHR:
		int64_t count;
		if (x64_vir_value_immediate(L->f,i->b,&count) && count>=0 && count<(i->type==VIR_TYPE_I32?32:64)) {
			x64_op3(e,i->op==VIR_OP_SHL?X_SHLI:i->op==VIR_OP_LSHR?X_SHRI:X_SARI,sz,d,a,XNOREG)->imm=count;return;
		}
		/* The result may be rcx: moving the first operand there would destroy cl.
		 * Shift in the reserved scratch, then publish the allocated result. */
		x64_mov(e, L->gtmp, a);
		x64_op3(e, i->op == VIR_OP_SHL ? X_SHL : i->op == VIR_OP_LSHR ? X_SHR : X_SAR, sz, L->gtmp, L->gtmp, c);
		x64_op3(e, X_MOV, sz, d, L->gtmp, XNOREG); return;
	case VIR_OP_ICMP: {
		static const uint8_t cc[] = { CC_E, CC_NE, CC_L, CC_LE, CC_G, CC_GE, CC_B, CC_BE, CC_A, CC_AE };
		if(L->cmpmemv==v) {
			int other=L->cmpmemside?a:c;
			rec *r=x64_op3(e,X_CMPM,L->cmpmemsize,XNOREG,L->membase,other);
			r->r[3]=L->indexed?L->memindex:XNOREG;r->sh=L->memsh;r->off=L->memoff;r->cond=L->cmpmemside;
			if(!L->cmpmemside && p[1]==VIR_REGISTER_NONE) {int64_t k;x64_vir_value_immediate(L->f,i->b,&k);r->r[2]=XNOREG;r->ref=(uint32_t)k;}
			L->cmpmemv=0;
		} else
		if (p[1]==VIR_REGISTER_NONE) {int64_t k;x64_vir_value_immediate(L->f,i->b,&k);x64_vir_constant_compare_emit(L,a,x64_vir_type_size(L->f->insns[i->a].type),(uint64_t)k);}
		else x64_op3(e, X_CMP, x64_vir_type_size(L->f->insns[i->a].type), XNOREG, a, c);
		L->cmp=v;L->cmpblock=b;L->setrec=e->x64->nin;
		x64_op3(e, X_SETCC, 4, d, XNOREG, XNOREG)->cond = cc[i->flags & 15]; return;
	}
	case VIR_OP_FCMP: {
		int cc = i->flags & 15;
		bool swap = cc == 2 || cc == 3;
		x64_op3(e, X_FCMP, x64_vir_type_size(L->f->insns[i->a].type), XNOREG, swap ? c : a, swap ? a : c);
		if (cc < 2) x64_op3(e, X_FSETCC, 4, d, XNOREG, XNOREG)->cond = cc;
		else x64_op3(e, X_SETCC, 4, d, XNOREG, XNOREG)->cond =
			cc == 6 ? CC_P : cc == 7 ? CC_NE : cc == 2 || cc == 4 ? CC_A : CC_AE;
		return;
	}
	case VIR_OP_SELECT: {
		if(!vir_register_class(i->type)) {
			uint8_t cc=x64_vir_condition_lower(L,b,v,i->a,a);
			int no=x64_vir_register_number(p[2]);
			if(d==c){int swap=c;c=no;no=swap;cc^=1;}
			x64_op3(e,X_CMOV,sz,d,no,c)->cond=cc;return;
		}
		int tmp = vir_register_class(i->type) ? XMM0 + 14 : L->gtmp;
		uint32_t done = wk_newlabel(e);
		x64_op3(e, X_TEST, x64_vir_type_size(L->f->insns[i->a].type), XNOREG, a, a);
		x64_op3(e, X_MOV, sz, tmp, x64_vir_register_number(p[2]), XNOREG);
		rec *r = x64_put(e, X_JCC); r->cond = CC_E; r->label = done;
		x64_op3(e, X_MOV, sz, tmp, c, XNOREG);
		x64_label(e, done);
		x64_op3(e, X_MOV, sz, d, tmp, XNOREG); return;
	}
	case VIR_OP_FEXT: case VIR_OP_FTRUNC: x64_op3(e, X_CVTF2F, sz, d, a, XNOREG); return;
	case VIR_OP_S2F: case VIR_OP_U2F: {
		int from = x64_vir_type_size(L->f->insns[i->a].type);
		if (i->op == VIR_OP_U2F) { x64_op3(e, X_MOV, 4, L->gtmp, a, XNOREG); a = L->gtmp; from = 8; }
		x64_op3(e, X_CVTI2F, sz, d, a, XNOREG)->sh = from; return;
	}
	case VIR_OP_F2S: case VIR_OP_F2U: {
		int to = i->op == VIR_OP_F2U ? 8 : sz;
		x64_op3(e, X_CVTF2I, to, d, a, XNOREG)->sh = x64_vir_type_size(L->f->insns[i->a].type);
		if (i->op == VIR_OP_F2U) x64_op3(e, X_MOV, 4, d, d, XNOREG);
		return;
	}
	case VIR_OP_FNEG: case VIR_OP_FABS: {
		bool wide = vir_type_element(i->type) == VIR_TYPE_F64;
		char text[240];
		snprintf(text, sizeof text, "\tpcmpeqd %%xmm14, %%xmm14\n\t%s $%d, %%xmm14\n\t%s %%xmm%d, %%xmm14\n\tmovaps %%xmm14, %%xmm%d\n",
			i->op == VIR_OP_FNEG ? (wide ? "psllq" : "pslld") : (wide ? "psrlq" : "psrld"),
			i->op == VIR_OP_FNEG ? (wide ? 63 : 31) : 1,
			i->op == VIR_OP_FNEG ? "pxor" : "pand", a-XMM0, d-XMM0);
		x64_vir_text_append(L, text);
		return;
	}
	case VIR_OP_FSQRT:
		if (vir_type_is_vector(i->type)) {
			char text[100];
			snprintf(text, sizeof text, "\t%s %%xmm%d, %%xmm%d\n", i->type == VIR_TYPE_F32X4 ? "sqrtps" : "sqrtpd", a-XMM0, d-XMM0);
			x64_vir_text_append(L, text);
		} else x64_op3(e, X_FSQRT, sz, d, a, XNOREG);
		return;
	case VIR_OP_VS2F:
		if (i->type == VIR_TYPE_F32X4) {
			char text[100]; snprintf(text, sizeof text, "\tcvtdq2ps %%xmm%d, %%xmm%d\n", a-XMM0, d-XMM0); x64_vir_text_append(L, text);
		} else {
			char text[100]; snprintf(text, sizeof text, "\tpshufd $78, %%xmm%d, %%xmm14\n", a-XMM0); x64_vir_text_append(L, text);
			x64_op3(e, X_MOV, 8, L->gtmp, a, XNOREG);
			x64_op3(e, X_CVTI2F, 8, d, L->gtmp, XNOREG)->sh = 8;
			x64_op3(e, X_MOV, 8, L->gtmp, XMM0+14, XNOREG);
			x64_op3(e, X_CVTI2F, 8, XMM15, L->gtmp, XNOREG)->sh = 8;
			snprintf(text, sizeof text, "\tunpcklpd %%xmm15, %%xmm%d\n", d-XMM0); x64_vir_text_append(L, text);
		}
		return;
	case VIR_OP_VBITSEL: {
		char text[300];
		snprintf(text, sizeof text, "\tmovaps %%xmm%d, %%xmm14\n\tmovaps %%xmm%d, %%xmm15\n"
			"\tpand %%xmm%d, %%xmm14\n\tpandn %%xmm%d, %%xmm15\n\tpor %%xmm14, %%xmm15\n\tmovaps %%xmm15, %%xmm%d\n",
			a-XMM0, a-XMM0, c-XMM0, x64_vir_register_number(p[2])-XMM0, d-XMM0);
		x64_vir_text_append(L, text); return;
	}
	case VIR_OP_VFCMP: {
		int cc = i->flags & 15;
		bool swap = cc == 4 || cc == 5;
		int pred = cc == 0 ? 0 : cc == 1 || cc == 7 ? 4 : cc == 2 || cc == 4 ? 1 : cc == 6 ? 3 : 2;
		const char *op = i->type == VIR_TYPE_I32X4 ? "cmpps" : "cmppd";
		char text[300];
		snprintf(text, sizeof text, "\tmovaps %%xmm%d, %%xmm14\n\t%s $%d, %%xmm%d, %%xmm14\n",
			(swap ? c : a)-XMM0, op, pred, (swap ? a : c)-XMM0);
		x64_vir_text_append(L, text);
		if (cc == 7) {
			snprintf(text, sizeof text, "\tmovaps %%xmm%d, %%xmm15\n\t%s $7, %%xmm%d, %%xmm15\n\tpand %%xmm15, %%xmm14\n", a-XMM0, op, c-XMM0);
			x64_vir_text_append(L, text);
		}
		x64_mov(e, d, XMM0+14); return;
	}
	case VIR_OP_VICMP: {
		int cc = i->flags & 15;
		bool swap = cc == 2 || cc == 5, invert = cc == 1 || cc == 3 || cc == 5;
		char text[260];
		snprintf(text, sizeof text, "\tmovaps %%xmm%d, %%xmm14\n\t%s %%xmm%d, %%xmm14\n",
			(swap ? c : a)-XMM0, cc < 2 ? "pcmpeqd" : "pcmpgtd", (swap ? a : c)-XMM0);
		x64_vir_text_append(L, text);
		if (invert) x64_vir_text_append(L, "\tpcmpeqd %xmm15, %xmm15\n\tpxor %xmm15, %xmm14\n");
		x64_mov(e, d, XMM0+14); return;
	}
	case VIR_OP_VSPLAT: {
		char s[160];
		int lane = x64_vir_type_size(vir_type_element(i->type));
		x64_op3(e, X_MOV, lane, d, a, XNOREG);
		snprintf(s, sizeof s, "\t%s\t$0, %%xmm%d, %%xmm%d\n", lane == 4 ? "pshufd" : "shufpd", d-XMM0, d-XMM0);
		x64_vir_text_append(L, s); return;
	}
	case VIR_OP_VSERIES: {
		/* Construct the lane numbers with SSE2, without a constant pool. */
		char s[240];
		x64_movi(e, L->gtmp, i->type == VIR_TYPE_I32X4 ? 0x0000000100000000LL : 0);
		x64_op3(e, X_MOV, 8, d, L->gtmp, XNOREG);
		x64_movi(e, L->gtmp, i->type == VIR_TYPE_I32X4 ? 0x0000000300000002LL : 1);
		x64_op3(e, X_MOV, 8, (XMM0 + 14), L->gtmp, XNOREG);
		snprintf(s, sizeof s, "\tpunpcklqdq\t%%xmm14, %%xmm%d\n", d-XMM0);
		x64_vir_text_append(L, s); return;
	}
	case VIR_OP_SMULHI: case VIR_OP_UMULHI:
		x64_op3(e,X_MOV,sz,R11,c,XNOREG);
		x64_op3(e,i->op==VIR_OP_SMULHI?X_SMULW:X_MULW,sz,XNOREG,R11,XNOREG);return;
	case VIR_OP_SDIV: case VIR_OP_UDIV: case VIR_OP_SREM: case VIR_OP_UREM: {
		bool sign = i->op == VIR_OP_SDIV || i->op == VIR_OP_SREM;
		x64_op3(e, X_MOV, sz, R11, c, XNOREG);
		if (sign) x64_op3(e, X_CQO, sz, RDX, RAX, XNOREG);
		else x64_op3(e, X_XOR, 4, RDX, RDX, RDX);
		x64_op3(e, sign ? X_IDIV : X_DIV, sz, XNOREG, R11, XNOREG);
		return;
	}
	case VIR_OP_VINSERT: {
		char text[320];
		int width = x64_vir_type_size(vir_type_element(i->type));
		static const unsigned swap32[4] = {228,225,198,39};
		unsigned perm = width == 4 ? swap32[i->c] : i->c ? 78 : 228;
		/* Save the scalar first: the result may share its register. Swap the
		 * destination lane to lane zero, replace just that lane, then swap back. */
		x64_op3(e, X_MOV, width, XMM0+14, c, XNOREG);
		snprintf(text, sizeof text,
			"\tpshufd $%u, %%xmm%d, %%xmm15\n\t%s %%xmm14, %%xmm15\n\tpshufd $%u, %%xmm15, %%xmm%d\n",
			perm, a-XMM0, width == 4 ? "movss" : "movsd", perm, d-XMM0);
		x64_vir_text_append(L,text); return;
	}
	case VIR_OP_VEXTRACT: {
		char text[160];
		snprintf(text,sizeof text,"\tmovaps %%xmm%d, %%xmm14\n\tpsrldq $%d, %%xmm14\n",a-XMM0,(int)i->b * x64_vir_type_size(i->type));
		x64_vir_text_append(L,text); x64_op3(e,X_MOV,x64_vir_type_size(i->type),d,XMM0+14,XNOREG); return;
	}
	case VIR_OP_VREV: {
		char s[160];
		snprintf(s, sizeof s, "\tpshufd\t$%d, %%xmm%d, %%xmm%d\n", x64_vir_type_size(vir_type_element(i->type)) == 4 ? 27 : 78, a-XMM0, d-XMM0);
		x64_vir_text_append(L, s); return;
	}
	case VIR_OP_VREDUCE: {
		char s[320];
		bool narrow = i->type == VIR_TYPE_I32;
		snprintf(s, sizeof s, "\tpshufd\t$78, %%xmm%d, %%xmm14\n\t%s\t%%xmm%d, %%xmm14\n", a-XMM0, narrow ? "paddd" : "paddq", a-XMM0);
		x64_vir_text_append(L, s);
		if (narrow) {
			x64_vir_text_append(L, "\tpshufd\t$177, %xmm14, %xmm15\n\tpaddd\t%xmm15, %xmm14\n");
		}
		x64_op3(e, X_MOV, sz, d, (XMM0 + 14), XNOREG); return;
	}
	case VIR_OP_AFENCE: x64_put(e,X_MFENCE); return;
	case VIR_OP_ASTORE:
		x64_op3(e,X_MOV,sz=x64_vir_type_size(L->f->insns[i->b].type),L->gtmp,c,XNOREG);
		x64_op3(e,X_XCHG,1<<VIR_INSN_FLAG_WIDTH_LOG2(i->flags),L->gtmp,a,XNOREG);return;
	case VIR_OP_SWITCH: x64_vir_switch_lower(L,b,i,a); return;
	case VIR_OP_BR: x64_vir_edge_emit(L, b, 0); x64_vir_goto_emit(L,b,i->a); return;
	case VIR_OP_BRIF: {
		uint32_t yes = x64_vir_edge_target(L,b,0,i->b), no=x64_vir_edge_target(L,b,1,i->c), nt=i->c;
		uint32_t next=x64_vir_block_next(L);
		uint8_t cc=x64_vir_condition_lower(L,b,v,i->a,a);
		/* Let the next placed block fall through, including the true edge.
		 * A deferred edge-copy stub is a different label and cannot fall through. */
		if(next!=VIR_BLOCK_NONE && yes==L->label[next]) {
			uint32_t swap=yes;yes=no;no=swap;cc^=1;nt=i->b;
		}
		rec *r = x64_put(e, X_JCC); r->cond = cc; r->label = yes;
		if(no==L->label[L->chase[nt]])x64_vir_goto_emit(L,b,nt);else x64_jump(e,no);return;
	}
	case VIR_OP_RET: x64_jump(e, L->ret); return;
	case VIR_OP_TRAP: x64_put(e, X_UD2); return;
	default: abort();
	}
}
