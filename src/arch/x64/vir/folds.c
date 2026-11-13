/* x86-64 lowering of VIR: constants, memory operands and the folds into one instruction. */

#include "lowering-internal.h"

VPRIV bool x64_vir_value_immediate(const vir_function *f, uint32_t v, int64_t *value)
{
	const vir_insn *i=&f->insns[v];
	if (i->op!=VIR_OP_ICONST) return false;
	int64_t k=i->type==VIR_TYPE_I32?(int64_t)(int32_t)i->b:x64_vir_constant_bits(i);
	if (k!=(int32_t)k) return false;
	*value=k;return true;
}

VPRIV bool x64_vir_memory_block_is_small(const vir_function *f,const vir_insn *i)
{
	const vir_insn *count=&f->insns[i->c];
	if(count->op!=VIR_OP_ICONST || (i->flags&VIR_INSN_FLAG_VOL))return false;
	uint64_t n=(uint64_t)x64_vir_constant_bits(count);
	if(i->op==VIR_OP_MEMCPY)return n<=128;
	const vir_insn *byte=&f->insns[i->b];
	return n<=256 && byte->op==VIR_OP_ICONST && !(byte->b&255);
}

VPRIV bool x64_vir_value_is_supported(const vir_function *f, uint32_t v)
{
	const vir_insn *i = &f->insns[v];
	bool vec = vir_type_is_vector(i->type);
	switch (i->op) {
	case VIR_OP_NOP: case VIR_OP_BPARAM: case VIR_OP_PARAM: case VIR_OP_LOAD: case VIR_OP_STORE:
	case VIR_OP_BR: case VIR_OP_BRIF: case VIR_OP_RET: case VIR_OP_TRAP: case VIR_OP_BARRIER: case VIR_OP_CALL:
	case VIR_OP_MEMCPY: case VIR_OP_MEMSET: case VIR_OP_SWITCH: case VIR_OP_ALOAD: case VIR_OP_ASTORE: case VIR_OP_AFENCE:
	case VIR_OP_PTRADD: case VIR_OP_PSUB: case VIR_OP_P2I: case VIR_OP_I2P: case VIR_OP_EXT: case VIR_OP_TRUNC:
	case VIR_OP_ICMP: case VIR_OP_FCMP: case VIR_OP_SELECT: case VIR_OP_SHL: case VIR_OP_LSHR: case VIR_OP_ASHR: case VIR_OP_BITCAST:
	case VIR_OP_S2F: case VIR_OP_F2S: case VIR_OP_VFCMP: case VIR_OP_VBITSEL: case VIR_OP_VS2F:
	case VIR_OP_FNEG: case VIR_OP_FABS: case VIR_OP_FSQRT: case VIR_OP_NEG: case VIR_OP_NOT:
		return true;
	case VIR_OP_SDIV: case VIR_OP_UDIV: case VIR_OP_SREM: case VIR_OP_UREM: case VIR_OP_SMULHI: case VIR_OP_UMULHI: return true;
	case VIR_OP_U2F: return f->insns[i->a].type == VIR_TYPE_I32;
	case VIR_OP_F2U: return i->type == VIR_TYPE_I32;
	case VIR_OP_VICMP: return i->type == VIR_TYPE_I32X4 && (i->flags & 15) < 6;
	case VIR_OP_MUL: return !vec || i->type == VIR_TYPE_I32X4;
	case VIR_OP_FEXT: case VIR_OP_FTRUNC:
		return true;
	case VIR_OP_ADD: case VIR_OP_SUB: case VIR_OP_AND: case VIR_OP_OR: case VIR_OP_XOR:
	case VIR_OP_FADD: case VIR_OP_FSUB: case VIR_OP_FMUL: case VIR_OP_FDIV:
		return true;
	case VIR_OP_VINSERT: case VIR_OP_VEXTRACT:
	case VIR_OP_VSPLAT: case VIR_OP_VSERIES: case VIR_OP_VREV: case VIR_OP_VREDUCE:
		return true;
	default: return false;
	}
}

/* Edits are sorted by instruction. A folded index must survive the intervening
 * reloads/rematerializations; memory consumers require no intervening edit. */
VPRIV bool x64_vir_fold_edits_check(x64_vir_context *L,uint32_t b,uint32_t pos,int keep)
{
	uint32_t lo=L->allocation->block_offsets[L->function->block_count+1+b],hi=L->allocation->block_offsets[L->function->block_count+2+b],end=hi;
	while(lo<hi) {uint32_t m=lo+(hi-lo)/2;if(L->allocation->edit[m].pos<pos)lo=m+1;else hi=m;}
	for(uint32_t k=lo;k<end && L->allocation->edit[k].pos==pos;k++) {
		const vir_edit *edit=&L->allocation->edit[k];
		if(keep==XNOREG || (edit->kind!=VIR_EDIT_REMATERIALIZE && edit->kind!=VIR_EDIT_RELOAD) || x64_vir_register_number(edit->dst)==keep)return false;
	}
	return true;
}

VPRIV bool x64_vir_memory_next_fold(x64_vir_context *L,uint32_t b,uint32_t v)
{
	return v+1<L->function->blocks[b].first+L->function->blocks[b].insn_count && L->uses[v]==1 && L->allocation->slot[v]==VIR_BLOCK_NONE &&
		L->function->insns[v+1].a==v && (L->function->insns[v+1].op==VIR_OP_LOAD || L->function->insns[v+1].op==VIR_OP_STORE) &&
		x64_vir_fold_edits_check(L,b,v+1,XNOREG);
}

/* Integer-address form of the same single memory consumer. */
VPRIV bool x64_vir_memory_cast_fold(x64_vir_context *L,uint32_t b,uint32_t v)
{
	const vir_function *f=L->function;
	return f->insns[v].op==VIR_OP_ADD && f->insns[v].type==VIR_TYPE_I64 && L->uses[v]==1 && L->allocation->slot[v]==VIR_BLOCK_NONE &&
	 v+2<f->blocks[b].first+f->blocks[b].insn_count && f->insns[v+1].op==VIR_OP_I2P && f->insns[v+1].a==v &&
	 x64_vir_memory_next_fold(L,b,v+1) && x64_vir_fold_edits_check(L,b,v+1,XNOREG);
}

/* Exact consecutive big-endian byte assembly, with no observable access or
 * allocator edit interleaved. Never widen beyond the bytes actually read. */
VPRIV unsigned x64_vir_word_read_match(x64_vir_context *L,uint32_t b,uint32_t v,uint32_t *end)
{
	unsigned width=vir_big_endian_read_match(L->function,L->uses,v,L->function->blocks[b].first+L->function->blocks[b].insn_count,end);
	if(!width)return 0;
	for(uint32_t k=v;k<*end;k++)
	 if(L->allocation->slot[k]!=VIR_BLOCK_NONE || !x64_vir_fold_edits_check(L,b,k+1,XNOREG))return 0;
	return width;
}

/* An address private to one exact byte-assembly expression. */
VPRIV unsigned x64_vir_big_endian_address_match(x64_vir_context *L,uint32_t b,uint32_t v,uint32_t *end)
{
	const vir_function *f=L->function;const vir_insn *i=&f->insns[v];
	uint32_t ptr=v,load=v+1;
	if(L->allocation->slot[v]!=VIR_BLOCK_NONE)return 0;
	if(i->op==VIR_OP_ADD && i->type==VIR_TYPE_I64) {
		if(L->uses[v]!=1 || v+2>=f->blocks[b].first+f->blocks[b].insn_count ||
		   f->insns[v+1].op!=VIR_OP_I2P || f->insns[v+1].a!=v || L->allocation->slot[v+1]!=VIR_BLOCK_NONE ||
		   !x64_vir_fold_edits_check(L,b,v+1,XNOREG))return 0;
		ptr=v+1;load=v+2;
	} else if(i->op!=VIR_OP_PTRADD)return 0;
	if(load>=f->blocks[b].first+f->blocks[b].insn_count || f->insns[load].a!=ptr || !x64_vir_fold_edits_check(L,b,load,XNOREG))return 0;
	unsigned width=x64_vir_word_read_match(L,b,load,end);
	return width && L->uses[ptr]==width?width:0;
}

VPRIV bool x64_vir_address_memory_fold(x64_vir_context *L,uint32_t b,uint32_t v)
{
	uint32_t end;
	return (L->function->insns[v].op==VIR_OP_PTRADD && x64_vir_memory_next_fold(L,b,v)) || x64_vir_memory_cast_fold(L,b,v) || x64_vir_big_endian_address_match(L,b,v,&end);
}

/* An IR i32 does not itself guarantee zero upper bits: parameters, calls,
 * signed narrow loads and identity truncations can retain arbitrary bits. */
VPRIV bool x64_vir_value_upper_half_is_clear(const vir_function *f,uint32_t v)
{
	const vir_insn *i=&f->insns[v];
	if(i->type!=VIR_TYPE_I32)return false;
	switch(i->op) {
	case VIR_OP_ICONST:return !(i->b>>31);
	case VIR_OP_LOAD: case VIR_OP_EXT:return !(i->flags&VIR_INSN_FLAG_MS);
	case VIR_OP_ADD: case VIR_OP_SUB: case VIR_OP_MUL: case VIR_OP_AND: case VIR_OP_OR: case VIR_OP_XOR:
	case VIR_OP_NEG: case VIR_OP_NOT: case VIR_OP_ICMP: case VIR_OP_FCMP: case VIR_OP_F2S: case VIR_OP_F2U:
	case VIR_OP_SDIV: case VIR_OP_UDIV: case VIR_OP_SREM: case VIR_OP_UREM: case VIR_OP_SMULHI: case VIR_OP_UMULHI:return true;
	case VIR_OP_BITCAST:return vir_register_class(f->insns[i->a].type);
	default:return false;
	}
}

/* Whether an i32 holder already has the physical sign extension required
 * by a signed index. A 4-byte spill reload destroys a negative sign extension. */
VPRIV bool x64_vir_value_is_sign_extended(x64_vir_context *L,uint32_t v)
{
	const vir_insn *source=&L->function->insns[v];
	if(source->type!=VIR_TYPE_I32)return false;
	if((source->op==VIR_OP_LOAD || source->op==VIR_OP_EXT) && (source->flags&VIR_INSN_FLAG_MS) &&
				VIR_INSN_FLAG_WIDTH_LOG2(source->flags)<=2 && L->allocation->slot[v]==VIR_BLOCK_NONE)return true;
	return x64_vir_value_upper_half_is_clear(L->function,v) && !(vir_value_possible_bits(L->function,v,8)>>31);
}

/* Small constant blocks do not need REP's fixed registers or their clobbers.
 * SSE2 handles complete 16-byte pieces; scalar tails never overread. */
VPRIV void x64_vir_memory_block_lower(x64_vir_context *L,const vir_insn *i,int dst,int src)
{
	codegen_context *e=L->context;
	uint64_t n=(uint64_t)x64_vir_constant_bits(&L->function->insns[i->c]);
	bool copy=i->op==VIR_OP_MEMCPY;
	if(!copy) {
		if(n>=16)x64_op3(e,X_FXOR,16,XMM0+14,XMM0+14,XMM0+14);
		if(n&15)x64_movi(e,L->general_temporary,0);
	}
	for(uint64_t off=0;off<n;) {
		unsigned width=n-off>=16?16:n-off>=8?8:n-off>=4?4:n-off>=2?2:1;
		int scratch=width==16?XMM0+14:L->general_temporary;
		codegen_lvalue from={.k=CODEGEN_LVALUE_REGISTER,.r=src,.off=(int64_t)off},to={.k=CODEGEN_LVALUE_REGISTER,.r=dst,.off=(int64_t)off};
		if(copy)x64_mem(e,X_LD,width,scratch,&from,false);
		x64_mem(e,X_ST,width,scratch,&to,false);off+=width;
	}
}

VPRIV uint8_t x64_vir_condition_lower(x64_vir_context *L,uint32_t b,uint32_t v,uint32_t value,int reg)
{
	codegen_context *e=L->context;
	bool flags=L->compare_condition==value && L->compare_block==b && v==value+1;
	uint8_t cc=CC_NE;
	if (flags) {
		for(uint32_t k=L->setcc_record+1;k<e->x64->nin;k++)
			if(x64_effects(e,&e->x64->in[k]).fl & (RECORD_EFFECT_WRITES_FLAGS|RECORD_EFFECT_BARRIER|RECORD_EFFECT_CALL)) {flags=false;break;}
	}
	if(flags) {
		cc=e->x64->in[L->setcc_record].cond;
		if(L->uses[value]==1 && L->allocation->slot[value]==VIR_BLOCK_NONE) {
			memmove(&e->x64->in[L->setcc_record],&e->x64->in[L->setcc_record+1],(e->x64->nin-L->setcc_record-1)*sizeof(record));
			e->x64->nin--;
		}
	} else x64_op3(e, X_TEST, x64_vir_type_size(L->function->insns[value].type), XNOREG, reg, reg);
	return cc;
}
