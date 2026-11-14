/* x86-64 lowering of VIR: one instruction. */

#include "lowering-internal.h"

VPRIV void x64_vir_insn_lower(x64_vir_context *L, uint32_t b, uint32_t v, const uint8_t *p, uint32_t n, uint8_t res)
{
	codegen_context *e = L->context;
	const vir_insn *i = &L->function->insns[v];
	int d = res == VIR_REGISTER_NONE ? (vir_register_class(i->type) ? XMM0 + 14 : L->general_temporary) : x64_vir_register_number(res);
	int a = n ? x64_vir_register_number(p[0]) : X64_REGISTER_NONE, c = n > 1 ? x64_vir_register_number(p[1]) : X64_REGISTER_NONE;
	int sz = x64_vir_type_size(i->type);
	static const uint8_t alu[VIR_OP_COUNT] = { [VIR_OP_ADD]=X64_OP_ADD, [VIR_OP_PTRADD]=X64_OP_ADD, [VIR_OP_SUB]=X64_OP_SUB, [VIR_OP_PSUB]=X64_OP_SUB,
		[VIR_OP_MUL]=X64_OP_IMUL, [VIR_OP_AND]=X64_OP_AND, [VIR_OP_OR]=X64_OP_OR, [VIR_OP_XOR]=X64_OP_XOR,
		[VIR_OP_FADD]=X64_OP_FADD, [VIR_OP_FSUB]=X64_OP_FSUB, [VIR_OP_FMUL]=X64_OP_FMUL, [VIR_OP_FDIV]=X64_OP_FDIV };
	if(L->test_value) {
		if(v<L->test_value)return;
		if(v==L->test_value) {
			record *r=x64_record_emit_three_registers(e,X64_OP_TESTM,L->test_size,X64_REGISTER_NONE,L->memory_base,X64_REGISTER_NONE);
			r->registers[3]=L->memory_is_indexed?L->memory_index:X64_REGISTER_NONE;r->shift=L->memory_shift;r->offset=L->memory_offset;r->reference=(uint32_t)L->test_immediate;
			L->compare_condition=v;L->compare_block=b;L->setcc_record=e->x64_context->record_count;
			x64_record_emit_three_registers(e,X64_OP_SETCC,4,d,X64_REGISTER_NONE,X64_REGISTER_NONE)->condition=(i->flags&15)?X64_CONDITION_NE:X64_CONDITION_E;
			L->test_value=0;return;
		}
	}
	if(L->memory_skip==v){L->memory_skip=0;return;}
	if(L->scaled_skip==v){L->scaled_skip=0;return;}
	if(L->big_endian_value) {
		if(v<L->big_endian_value)return;
		if(v==L->big_endian_value) {
			codegen_lvalue at={.kind=CODEGEN_LVALUE_REGISTER,.register_number=L->big_endian_base,.offset=L->big_endian_offset};
			if(L->big_endian_is_indexed) {
				record *r=x64_record_emit_three_registers(e,X64_OP_LDX,L->big_endian_width,d,at.register_number,X64_REGISTER_NONE);r->registers[3]=L->big_endian_index;r->shift=L->big_endian_shift;r->offset=at.offset;
			} else x64_record_emit_memory(e,X64_OP_LD,L->big_endian_width,d,&at,false);
			x64_record_emit_three_registers(e,X64_OP_BSWAP,L->big_endian_width,d,d,X64_REGISTER_NONE);L->big_endian_value=0;return;
		}
	}
	/* A single-use ordinary unsigned load, constant mask and zero comparison
	 * can test the exact source width in memory. Preserve the address until
	 * the comparison; neither skipped definition may spill or have edits. */
	if(i->op==VIR_OP_LOAD && (i->type==VIR_TYPE_I32 || i->type==VIR_TYPE_I64) && !(i->flags&(VIR_INSN_FLAG_VOL|VIR_INSN_FLAG_MS)) &&
	   v+2<L->function->blocks[b].first+L->function->blocks[b].insn_count && L->uses[v]==1 && L->allocation->slot[v]==VIR_BLOCK_NONE) {
		const vir_insn *mask=&L->function->insns[v+1],*cmp=&L->function->insns[v+2];int64_t k,zero;
		if(mask->op==VIR_OP_AND && mask->type==i->type && mask->a==v && x64_vir_value_immediate(L->function,mask->b,&k) &&
		   L->uses[v+1]==1 && L->allocation->slot[v+1]==VIR_BLOCK_NONE && cmp->op==VIR_OP_ICMP &&
		   (cmp->flags&15)<2 && cmp->a==v+1 && x64_vir_value_immediate(L->function,cmp->b,&zero) && !zero &&
		   x64_vir_fold_edits_check(L,b,v+1,X64_REGISTER_NONE) && x64_vir_fold_edits_check(L,b,v+2,X64_REGISTER_NONE)) {
			L->test_value=v+2;L->test_size=1<<VIR_INSN_FLAG_WIDTH_LOG2(i->flags);L->test_immediate=k;
			if(L->memory_value==i->a)L->memory_value=0;
			else {L->memory_base=a;L->memory_offset=0;L->memory_is_indexed=false;L->memory_shift=0;}
			return;
		}
	}
	/* Fold an adjacent private scalar load into its comparison. Narrow
	 * equality with a representable immediate needs no promotion bits. */
	if(i->op==VIR_OP_LOAD && (i->type==VIR_TYPE_I32 || i->type==VIR_TYPE_I64) && !(i->flags&VIR_INSN_FLAG_VOL) &&
	   L->uses[v]==1 && L->allocation->slot[v]==VIR_BLOCK_NONE && v+1<L->function->blocks[b].first+L->function->blocks[b].insn_count &&
	   x64_vir_fold_edits_check(L,b,v+1,X64_REGISTER_NONE)) {
		const vir_insn *next=&L->function->insns[v+1];unsigned width=1<<VIR_INSN_FLAG_WIDTH_LOG2(i->flags);int64_t k=0;
		bool side=next->b==v,ok=false;
		if(next->op==VIR_OP_ICMP && (next->a==v || side)) {
			uint32_t other=side?next->a:next->b;
			bool imm=x64_vir_value_immediate(L->function,other,&k);
			if(width==x64_vir_type_size(i->type))ok=!imm || (!side && (width==4 || k==(int32_t)k));
			else if(!side && imm && (next->flags&15)<2) {
				int64_t lo=(i->flags&VIR_INSN_FLAG_MS)?-(INT64_C(1)<<(width*8-1)):0;
				int64_t hi=(INT64_C(1)<<(width*8-!!(i->flags&VIR_INSN_FLAG_MS)))-1;
				ok=k>=lo && k<=hi;
			}
		}
		if(ok) {
			L->compare_memory_value=v+1;L->compare_memory_size=width;L->compare_memory_side=side;
			if(L->memory_value==i->a)L->memory_value=0;
			else {L->memory_base=a;L->memory_offset=0;L->memory_is_indexed=false;L->memory_shift=0;}
			return;
		}
	}
	if(i->op==VIR_OP_LOAD) {
		uint32_t end;unsigned width=x64_vir_word_read_match(L,b,v,&end);
		if(width) {
			L->big_endian_value=end;L->big_endian_width=width;L->big_endian_base=a;L->big_endian_offset=0;L->big_endian_is_indexed=false;
			if(L->memory_value==i->a) {
				L->big_endian_base=L->memory_base;L->big_endian_offset=L->memory_offset;L->big_endian_is_indexed=L->memory_is_indexed;
				L->big_endian_index=L->memory_index;L->big_endian_shift=L->memory_shift;L->memory_value=0;
			}
			return;
		}
	}
	/* The walker also spells pointer arithmetic as add.i64 + i2p. The
	 * cast is an identity; keep both inputs until the immediate memory use. */
	if(i->op==VIR_OP_ADD || i->op==VIR_OP_PTRADD) {
		uint32_t end;unsigned width=x64_vir_big_endian_address_match(L,b,v,&end);
		if(width) {
			L->big_endian_value=end;L->big_endian_width=width;L->big_endian_base=a;L->big_endian_offset=0;
			L->big_endian_is_indexed=!x64_vir_value_immediate(L->function,i->b,&L->big_endian_offset);
			L->big_endian_index=L->scaled_value==i->b?L->scaled_index:c;
			L->big_endian_shift=L->scaled_value==i->b?L->scaled_shift:0;return;
		}
	}
	if(x64_vir_memory_cast_fold(L,b,v)) {
		L->memory_value=v+1;L->memory_skip=v+1;L->memory_base=a;L->memory_offset=0;
		L->memory_is_indexed=!x64_vir_value_immediate(L->function,i->b,&L->memory_offset);
		L->memory_index=L->scaled_value==i->b?L->scaled_index:c;
		L->memory_shift=L->scaled_value==i->b?L->scaled_shift:0;return;
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
	   vir_sign_extend_shift_match(L->function,v,&signed_scale) && signed_scale<=3 && L->uses[v]==1 &&
	   L->allocation->slot[v]==VIR_BLOCK_NONE && v+3<L->function->blocks[b].first+L->function->blocks[b].insn_count &&
	   L->uses[v+1]==1 && L->allocation->slot[v+1]==VIR_BLOCK_NONE && L->function->insns[v+2].b==v+1 &&
	   x64_vir_address_memory_fold(L,b,v+2) && x64_vir_fold_edits_check(L,b,v+1,X64_REGISTER_NONE) && x64_vir_fold_edits_check(L,b,v+2,d)) {
		if(x64_vir_value_is_sign_extended(L,i->a))x64_mov(e,d,a);
		else x64_record_emit_three_registers(e,X64_OP_EXT,4,d,a,X64_REGISTER_NONE)->condition=1;
		L->scaled_value=v+1;L->scaled_skip=v+1;L->scaled_index=d;L->scaled_shift=signed_scale;return;
	}
	if ((i->op==VIR_OP_MUL || i->op==VIR_OP_SHL) && i->type==VIR_TYPE_I64 && L->uses[v]==1 && L->allocation->slot[v]==VIR_BLOCK_NONE &&
	    v+2<L->function->blocks[b].first+L->function->blocks[b].insn_count && L->function->insns[v+1].b==v &&
	    x64_vir_address_memory_fold(L,b,v+1) &&
	    x64_vir_fold_edits_check(L,b,v+1,a)) {
		int64_t k;
		if(x64_vir_value_immediate(L->function,i->b,&k) && (i->op==VIR_OP_SHL ? k>=0 && k<=3 : k==1 || k==2 || k==4 || k==8)) {
			L->scaled_value=v;L->scaled_index=a;
			L->scaled_shift=i->op==VIR_OP_SHL?k:k==8?3:k==4?2:k==2?1:0;
			return;
		}
	}
	if (i->op==VIR_OP_PTRADD && x64_vir_memory_next_fold(L,b,v)) {
		L->memory_value=v;L->memory_base=a;L->memory_offset=0;
		L->memory_is_indexed=!x64_vir_value_immediate(L->function,i->b,&L->memory_offset);
		L->memory_index=L->scaled_value==i->b?L->scaled_index:c;
		L->memory_shift=L->scaled_value==i->b?L->scaled_shift:0;
		return;
	}
	if (alu[i->op]) {
		if (n>1 && p[1]==VIR_REGISTER_NONE && !vir_register_class(i->type)) {
			static const uint8_t immop[VIR_OP_COUNT]={[VIR_OP_ADD]=X64_OP_ADDI,[VIR_OP_PTRADD]=X64_OP_ADDI,[VIR_OP_SUB]=X64_OP_SUBI,[VIR_OP_PSUB]=X64_OP_SUBI,
				[VIR_OP_AND]=X64_OP_ANDI,[VIR_OP_OR]=X64_OP_ORI,[VIR_OP_XOR]=X64_OP_XORI,[VIR_OP_MUL]=X64_OP_IMULI};
			int64_t k;x64_vir_value_immediate(L->function,i->b,&k);
			if(i->op==VIR_OP_PTRADD || i->op==VIR_OP_ADD) {
				codegen_lvalue at={.kind=CODEGEN_LVALUE_REGISTER,.register_number=(uint8_t)a,.offset=k};x64_record_emit_memory(e,X64_OP_LEA,sz,d,&at,false);
			} else x64_record_emit_three_registers(e,immop[i->op],sz,d,a,X64_REGISTER_NONE)->immediate=k;
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
		} else if (i->op==VIR_OP_ADD || i->op==VIR_OP_PTRADD) x64_record_emit_three_registers(e,X64_OP_LEASCALE,sz,d,a,c)->shift=0;
		else x64_record_emit_three_registers(e, alu[i->op], sz, d, a, c);
		return;
	}
	switch (i->op) {
	case VIR_OP_NOP: case VIR_OP_BPARAM: return;
	case VIR_OP_BARRIER: x64_record_emit(e, X64_OP_BARRIER); return;
	case VIR_OP_MEMCPY: case VIR_OP_MEMSET:
		if(x64_vir_memory_block_is_small(L->function,i)){x64_vir_memory_block_lower(L,i,a,c);return;}
		x64_record_emit(e, X64_OP_REP)->condition = i->op == VIR_OP_MEMSET;
		return;
	case VIR_OP_CALL: {
		uint32_t ng = L->function->extra[i->c];
		int total = 0, nf = 0;
		for (uint32_t k = 0; k < ng; k++) {
			int off;
			uint8_t r = x64_vir_argument_register(L->function, i->b, k, &off, L->ms);
			if (r == VIR_REGISTER_NONE) total = off - 8;
			else if (VIR_REGISTER_CLASS(r)) nf++;
		}
		if (L->ms && total < 32) total = 32;	/* the shadow space */
		total = (total + 15) & ~15;
		if (total) x64_record_emit(e, X64_OP_ADJSP)->immediate = -total;
		for (uint32_t k = 0; k < ng; k++) {
			int off;
			if (x64_vir_argument_register(L->function, i->b, k, &off, L->ms) != VIR_REGISTER_NONE) continue;
			uint32_t value = L->function->extra[i->c + 1 + k];
			uint8_t r = p[k + 1];
			if (r == VIR_REGISTER_NONE) {
				r = vir_register_class(L->function->insns[value].type) ? VIR_REGISTER_MAKE(1, 14) : L->general_temporary;
				if (value < L->function->constant_count) x64_vir_constant_materialize(L, r, value);
				else x64_vir_slot_access(L, true, r, L->allocation->slot[value]);
			}
			codegen_lvalue at = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = RSP, .offset = off - 16 };
			x64_record_emit_memory(e, X64_OP_ST, x64_vir_type_size(L->function->insns[value].type), x64_vir_register_number(r), &at, false);
		}
		bool indirect = p[0] != VIR_REGISTER_NONE;
		if (indirect) x64_mov(e, R11, a); /* al is set for variadic calls */
		if (L->ms && VIR_SIGNATURE_IS_VARIADIC(L->function->extra[i->b]))
			for (uint32_t k = 0; k < ng && k < 4; k++) {	/* a float also in its integer register */
				int off;
				uint8_t r = x64_vir_argument_register(L->function, i->b, k, &off, true);
				if (VIR_REGISTER_CLASS(r)) x64_record_emit_three_registers(e, X64_OP_MOV, 8, x64w_argreg((int)k), x64_vir_register_number(r), X64_REGISTER_NONE);
			}
		else if (VIR_SIGNATURE_IS_VARIADIC(L->function->extra[i->b])) x64_record_emit_move_immediate(e, RAX, nf);
		record *call;
		if (indirect) call = x64_record_emit_three_registers(e, X64_OP_CALLR, 8, X64_REGISTER_NONE, R11, X64_REGISTER_NONE);
		else {
			const vir_insn *callee = &L->function->insns[i->a];
			codegen_reference_note(e, callee->a);
			call = x64_record_emit(e, X64_OP_CALL);
			call->reference = callee->a;
		}
		call->shift = L->ms;	/* (a Microsoft callee: print.c's save set) */
		if (total) x64_record_emit(e, X64_OP_ADJSP)->immediate = total;
		e->has_calls = true;
		return;
	}
	case VIR_OP_PARAM: {
		int stack;
		if (x64_vir_argument_register(L->function, L->function->signature, i->a, &stack, L->ms) == VIR_REGISTER_NONE) {
			codegen_lvalue at = { .kind = CODEGEN_LVALUE_FRAME, .offset = stack };
			x64_record_emit_memory(e, X64_OP_LD, sz, d, &at, false);
		}
		return;
	}
	case VIR_OP_ALOAD: case VIR_OP_LOAD: case VIR_OP_STORE: {
		codegen_lvalue at = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = a };
		const vir_insn *addr = &L->function->insns[i->a];
		if (i->op == VIR_OP_STORE && addr->op == VIR_OP_SLOTADDR) {
			at.kind = CODEGEN_LVALUE_FRAME;
			at.offset = L->slot[addr->a] + x64_vir_constant_bits(addr);
		}
		bool folded=L->memory_value==i->a;
		if(folded) {at.register_number=L->memory_base;at.offset=L->memory_offset;L->memory_value=0;}
		bool load = i->op != VIR_OP_STORE;
		int ty = load ? i->type : L->function->insns[i->b].type;
		int width=vir_type_is_vector(ty)?16:1<<VIR_INSN_FLAG_WIDTH_LOG2(i->flags);
		if(folded && L->memory_is_indexed) {
			record *r=x64_record_emit_three_registers(e,load?X64_OP_LDX:X64_OP_STX,width,load?d:X64_REGISTER_NONE,at.register_number,load?X64_REGISTER_NONE:c);
			r->registers[3]=L->memory_index;r->shift=L->memory_shift;r->condition=!!(i->flags & VIR_INSN_FLAG_MS);r->offset=at.offset;
		} else x64_record_emit_memory(e,load?X64_OP_LD:X64_OP_ST,width,load?d:c,&at,i->flags & VIR_INSN_FLAG_MS);
		return;
	}
	case VIR_OP_NEG: case VIR_OP_NOT:
		if (vir_type_is_vector(i->type)) {
			char text[200];
			snprintf(text, sizeof text, "\t%s %%xmm14, %%xmm14\n\t%s %%xmm%d, %%xmm14\n\tmovaps %%xmm14, %%xmm%d\n",
				i->op == VIR_OP_NEG ? "pxor" : "pcmpeqd",
				i->op == VIR_OP_NOT ? "pxor" : i->type == VIR_TYPE_I32X4 ? "psubd" : "psubq", a-XMM0, d-XMM0);
			x64_vir_text_append(L, text);
		} else x64_record_emit_three_registers(e, i->op == VIR_OP_NEG ? X64_OP_NEG : X64_OP_NOT, sz, d, a, X64_REGISTER_NONE);
		return;
	case VIR_OP_P2I: case VIR_OP_I2P: case VIR_OP_TRUNC: case VIR_OP_BITCAST:
		/* An i32 value does not promise anything about the upper GPR bits;
		 * a later unsigned EXT is responsible for clearing them if needed. */
		if (d==a) return;
		x64_record_emit_three_registers(e, X64_OP_MOV, sz, d, a, X64_REGISTER_NONE); return;
	case VIR_OP_EXT: {
		if (VIR_INSN_FLAG_WIDTH_LOG2(i->flags) == 3) { x64_mov(e, d, a); return; }
		if(VIR_INSN_FLAG_WIDTH_LOG2(i->flags)==2 && ((i->flags&VIR_INSN_FLAG_MS)?x64_vir_value_is_sign_extended(L,i->a):x64_vir_value_upper_half_is_clear(L->function,i->a))) {
			x64_mov(e,d,a);return;
		}
		record *r = x64_record_emit_three_registers(e, X64_OP_EXT, 1 << VIR_INSN_FLAG_WIDTH_LOG2(i->flags), d, a, X64_REGISTER_NONE);
		r->condition = !!(i->flags & VIR_INSN_FLAG_MS); return;
	}
	case VIR_OP_SHL: case VIR_OP_LSHR: case VIR_OP_ASHR:
		int64_t count;
		if (x64_vir_value_immediate(L->function,i->b,&count) && count>=0 && count<(i->type==VIR_TYPE_I32?32:64)) {
			x64_record_emit_three_registers(e,i->op==VIR_OP_SHL?X64_OP_SHLI:i->op==VIR_OP_LSHR?X64_OP_SHRI:X64_OP_SARI,sz,d,a,X64_REGISTER_NONE)->immediate=count;return;
		}
		/* The result may be rcx: moving the first operand there would destroy cl.
		 * Shift in the reserved scratch, then publish the allocated result. */
		x64_mov(e, L->general_temporary, a);
		x64_record_emit_three_registers(e, i->op == VIR_OP_SHL ? X64_OP_SHL : i->op == VIR_OP_LSHR ? X64_OP_SHR : X64_OP_SAR, sz, L->general_temporary, L->general_temporary, c);
		x64_record_emit_three_registers(e, X64_OP_MOV, sz, d, L->general_temporary, X64_REGISTER_NONE); return;
	case VIR_OP_ICMP: {
		static const uint8_t cc[] = { X64_CONDITION_E, X64_CONDITION_NE, X64_CONDITION_L, X64_CONDITION_LE, X64_CONDITION_G, X64_CONDITION_GE, X64_CONDITION_B, X64_CONDITION_BE, X64_CONDITION_A, X64_CONDITION_AE };
		if(L->compare_memory_value==v) {
			int other=L->compare_memory_side?a:c;
			record *r=x64_record_emit_three_registers(e,X64_OP_CMPM,L->compare_memory_size,X64_REGISTER_NONE,L->memory_base,other);
			r->registers[3]=L->memory_is_indexed?L->memory_index:X64_REGISTER_NONE;r->shift=L->memory_shift;r->offset=L->memory_offset;r->condition=L->compare_memory_side;
			if(!L->compare_memory_side && p[1]==VIR_REGISTER_NONE) {int64_t k;x64_vir_value_immediate(L->function,i->b,&k);r->registers[2]=X64_REGISTER_NONE;r->reference=(uint32_t)k;}
			L->compare_memory_value=0;
		} else
		if (p[1]==VIR_REGISTER_NONE) {int64_t k;x64_vir_value_immediate(L->function,i->b,&k);x64_vir_constant_compare_emit(L,a,x64_vir_type_size(L->function->insns[i->a].type),(uint64_t)k);}
		else x64_record_emit_three_registers(e, X64_OP_CMP, x64_vir_type_size(L->function->insns[i->a].type), X64_REGISTER_NONE, a, c);
		L->compare_condition=v;L->compare_block=b;L->setcc_record=e->x64_context->record_count;
		x64_record_emit_three_registers(e, X64_OP_SETCC, 4, d, X64_REGISTER_NONE, X64_REGISTER_NONE)->condition = cc[i->flags & 15]; return;
	}
	case VIR_OP_FCMP: {
		int cc = i->flags & 15;
		bool swap = cc == 2 || cc == 3;
		x64_record_emit_three_registers(e, X64_OP_FCMP, x64_vir_type_size(L->function->insns[i->a].type), X64_REGISTER_NONE, swap ? c : a, swap ? a : c);
		if (cc < 2) x64_record_emit_three_registers(e, X64_OP_FSETCC, 4, d, X64_REGISTER_NONE, X64_REGISTER_NONE)->condition = cc;
		else x64_record_emit_three_registers(e, X64_OP_SETCC, 4, d, X64_REGISTER_NONE, X64_REGISTER_NONE)->condition =
			cc == 6 ? X64_CONDITION_P : cc == 7 ? X64_CONDITION_NE : cc == 2 || cc == 4 ? X64_CONDITION_A : X64_CONDITION_AE;
		return;
	}
	case VIR_OP_SELECT: {
		if(!vir_register_class(i->type)) {
			uint8_t cc=x64_vir_condition_lower(L,b,v,i->a,a);
			int no=x64_vir_register_number(p[2]);
			if(d==c){int swap=c;c=no;no=swap;cc^=1;}
			x64_record_emit_three_registers(e,X64_OP_CMOV,sz,d,no,c)->condition=cc;return;
		}
		int tmp = vir_register_class(i->type) ? XMM0 + 14 : L->general_temporary;
		uint32_t done = walk_label_new(e);
		x64_record_emit_three_registers(e, X64_OP_TEST, x64_vir_type_size(L->function->insns[i->a].type), X64_REGISTER_NONE, a, a);
		x64_record_emit_three_registers(e, X64_OP_MOV, sz, tmp, x64_vir_register_number(p[2]), X64_REGISTER_NONE);
		record *r = x64_record_emit(e, X64_OP_JCC); r->condition = X64_CONDITION_E; r->label = done;
		x64_record_emit_three_registers(e, X64_OP_MOV, sz, tmp, c, X64_REGISTER_NONE);
		x64_label(e, done);
		x64_record_emit_three_registers(e, X64_OP_MOV, sz, d, tmp, X64_REGISTER_NONE); return;
	}
	case VIR_OP_FEXT: case VIR_OP_FTRUNC: x64_record_emit_three_registers(e, X64_OP_CVTF2F, sz, d, a, X64_REGISTER_NONE); return;
	case VIR_OP_U2F:
		if (L->function->insns[i->a].type == VIR_TYPE_I64) {
			/* f64 of an unsigned 64-bit value: hi * 2^32 + lo, both halves exact, one rounding (in the add) */
			int t = L->general_temporary, x = XMM0 + 14;

			x64_record_emit_three_registers(e, X64_OP_MOV, 8, t, a, X64_REGISTER_NONE);
			x64_record_emit_three_registers(e, X64_OP_SHRI, 8, t, t, X64_REGISTER_NONE)->immediate = 32;
			x64_record_emit_three_registers(e, X64_OP_CVTI2F, 8, d, t, X64_REGISTER_NONE)->shift = 8;
			x64_record_emit_move_immediate(e, t, (int64_t)1 << 32);
			x64_record_emit_three_registers(e, X64_OP_CVTI2F, 8, x, t, X64_REGISTER_NONE)->shift = 8;
			x64_record_emit_three_registers(e, X64_OP_FMUL, 8, d, d, x);
			x64_record_emit_three_registers(e, X64_OP_MOV, 4, t, a, X64_REGISTER_NONE);
			x64_record_emit_three_registers(e, X64_OP_CVTI2F, 8, x, t, X64_REGISTER_NONE)->shift = 8;
			x64_record_emit_three_registers(e, X64_OP_FADD, 8, d, d, x);
			return;
		}
		__attribute__((fallthrough));
	case VIR_OP_S2F: {
		int from = x64_vir_type_size(L->function->insns[i->a].type);
		if (i->op == VIR_OP_U2F) { x64_record_emit_three_registers(e, X64_OP_MOV, 4, L->general_temporary, a, X64_REGISTER_NONE); a = L->general_temporary; from = 8; }
		x64_record_emit_three_registers(e, X64_OP_CVTI2F, sz, d, a, X64_REGISTER_NONE)->shift = from; return;
	}
	case VIR_OP_F2S: case VIR_OP_F2U: {
		int to = i->op == VIR_OP_F2U ? 8 : sz;
		x64_record_emit_three_registers(e, X64_OP_CVTF2I, to, d, a, X64_REGISTER_NONE)->shift = x64_vir_type_size(L->function->insns[i->a].type);
		if (i->op == VIR_OP_F2U) x64_record_emit_three_registers(e, X64_OP_MOV, 4, d, d, X64_REGISTER_NONE);
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
		} else x64_record_emit_three_registers(e, X64_OP_FSQRT, sz, d, a, X64_REGISTER_NONE);
		return;
	case VIR_OP_VS2F:
		if (i->type == VIR_TYPE_F32X4) {
			char text[100]; snprintf(text, sizeof text, "\tcvtdq2ps %%xmm%d, %%xmm%d\n", a-XMM0, d-XMM0); x64_vir_text_append(L, text);
		} else {
			char text[100]; snprintf(text, sizeof text, "\tpshufd $78, %%xmm%d, %%xmm14\n", a-XMM0); x64_vir_text_append(L, text);
			x64_record_emit_three_registers(e, X64_OP_MOV, 8, L->general_temporary, a, X64_REGISTER_NONE);
			x64_record_emit_three_registers(e, X64_OP_CVTI2F, 8, d, L->general_temporary, X64_REGISTER_NONE)->shift = 8;
			x64_record_emit_three_registers(e, X64_OP_MOV, 8, L->general_temporary, XMM0+14, X64_REGISTER_NONE);
			x64_record_emit_three_registers(e, X64_OP_CVTI2F, 8, XMM15, L->general_temporary, X64_REGISTER_NONE)->shift = 8;
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
		x64_record_emit_three_registers(e, X64_OP_MOV, lane, d, a, X64_REGISTER_NONE);
		snprintf(s, sizeof s, "\t%s\t$0, %%xmm%d, %%xmm%d\n", lane == 4 ? "pshufd" : "shufpd", d-XMM0, d-XMM0);
		x64_vir_text_append(L, s); return;
	}
	case VIR_OP_VSERIES: {
		/* Construct the lane numbers with SSE2, without a constant pool. */
		char s[240];
		x64_record_emit_move_immediate(e, L->general_temporary, i->type == VIR_TYPE_I32X4 ? 0x0000000100000000LL : 0);
		x64_record_emit_three_registers(e, X64_OP_MOV, 8, d, L->general_temporary, X64_REGISTER_NONE);
		x64_record_emit_move_immediate(e, L->general_temporary, i->type == VIR_TYPE_I32X4 ? 0x0000000300000002LL : 1);
		x64_record_emit_three_registers(e, X64_OP_MOV, 8, (XMM0 + 14), L->general_temporary, X64_REGISTER_NONE);
		snprintf(s, sizeof s, "\tpunpcklqdq\t%%xmm14, %%xmm%d\n", d-XMM0);
		x64_vir_text_append(L, s); return;
	}
	case VIR_OP_SMULHI: case VIR_OP_UMULHI:
		x64_record_emit_three_registers(e,X64_OP_MOV,sz,R11,c,X64_REGISTER_NONE);
		x64_record_emit_three_registers(e,i->op==VIR_OP_SMULHI?X64_OP_SMULW:X64_OP_MULW,sz,X64_REGISTER_NONE,R11,X64_REGISTER_NONE);return;
	case VIR_OP_SDIV: case VIR_OP_UDIV: case VIR_OP_SREM: case VIR_OP_UREM: {
		bool sign = i->op == VIR_OP_SDIV || i->op == VIR_OP_SREM;
		x64_record_emit_three_registers(e, X64_OP_MOV, sz, R11, c, X64_REGISTER_NONE);
		if (sign) x64_record_emit_three_registers(e, X64_OP_CQO, sz, RDX, RAX, X64_REGISTER_NONE);
		else x64_record_emit_three_registers(e, X64_OP_XOR, 4, RDX, RDX, RDX);
		x64_record_emit_three_registers(e, sign ? X64_OP_IDIV : X64_OP_DIV, sz, X64_REGISTER_NONE, R11, X64_REGISTER_NONE);
		return;
	}
	case VIR_OP_VINSERT: {
		char text[320];
		int width = x64_vir_type_size(vir_type_element(i->type));
		static const unsigned swap32[4] = {228,225,198,39};
		unsigned perm = width == 4 ? swap32[i->c] : i->c ? 78 : 228;
		/* Save the scalar first: the result may share its register. Swap the
		 * destination lane to lane zero, replace just that lane, then swap back. */
		x64_record_emit_three_registers(e, X64_OP_MOV, width, XMM0+14, c, X64_REGISTER_NONE);
		snprintf(text, sizeof text,
			"\tpshufd $%u, %%xmm%d, %%xmm15\n\t%s %%xmm14, %%xmm15\n\tpshufd $%u, %%xmm15, %%xmm%d\n",
			perm, a-XMM0, width == 4 ? "movss" : "movsd", perm, d-XMM0);
		x64_vir_text_append(L,text); return;
	}
	case VIR_OP_VEXTRACT: {
		char text[160];
		snprintf(text,sizeof text,"\tmovaps %%xmm%d, %%xmm14\n\tpsrldq $%d, %%xmm14\n",a-XMM0,(int)i->b * x64_vir_type_size(i->type));
		x64_vir_text_append(L,text); x64_record_emit_three_registers(e,X64_OP_MOV,x64_vir_type_size(i->type),d,XMM0+14,X64_REGISTER_NONE); return;
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
		x64_record_emit_three_registers(e, X64_OP_MOV, sz, d, (XMM0 + 14), X64_REGISTER_NONE); return;
	}
	case VIR_OP_AXADD: case VIR_OP_AXCHG: {
		int w = 1 << VIR_INSN_FLAG_WIDTH_LOG2(i->flags);

		x64_record_emit_three_registers(e, X64_OP_MOV, sz, L->general_temporary, c, X64_REGISTER_NONE);
		x64_record_emit_three_registers(e, i->op == VIR_OP_AXADD ? X64_OP_XADD : X64_OP_XCHG,
			w, L->general_temporary, a, X64_REGISTER_NONE);
		if (w < sz)	/* a narrower access: the old value extended as a load's (flag S) */
			x64_record_emit_three_registers(e, X64_OP_EXT, w, d, L->general_temporary, X64_REGISTER_NONE)->condition = (i->flags & VIR_INSN_FLAG_MS) != 0;
		else
			x64_record_emit_three_registers(e, X64_OP_MOV, sz, d, L->general_temporary, X64_REGISTER_NONE);
		return;
	}
	case VIR_OP_ACAS: {
		int w = 1 << VIR_INSN_FLAG_WIDTH_LOG2(i->flags);

		x64_record_emit_three_registers(e, X64_OP_CMPXCHG, w, X64_REGISTER_NONE, a, x64_vir_register_number(p[2]));
		if (w < sz)	/* the old value in rax, extended as a load's (flag S) */
			x64_record_emit_three_registers(e, X64_OP_EXT, w, RAX, RAX, X64_REGISTER_NONE)->condition = (i->flags & VIR_INSN_FLAG_MS) != 0;
		return;
	}
	case VIR_OP_POPCNT:
		x64_record_emit_three_registers(e, X64_OP_POPCNT, sz, d, a, X64_REGISTER_NONE);
		return;
	case VIR_OP_AFENCE: x64_record_emit(e,X64_OP_MFENCE); return;
	case VIR_OP_ASTORE:
		x64_record_emit_three_registers(e,X64_OP_MOV,sz=x64_vir_type_size(L->function->insns[i->b].type),L->general_temporary,c,X64_REGISTER_NONE);
		x64_record_emit_three_registers(e,X64_OP_XCHG,1<<VIR_INSN_FLAG_WIDTH_LOG2(i->flags),L->general_temporary,a,X64_REGISTER_NONE);return;
	case VIR_OP_CTZ: case VIR_OP_CLZ:
		x64_record_emit_three_registers(e, i->op == VIR_OP_CTZ ? X64_OP_BSF : X64_OP_BSR, sz, d, a, X64_REGISTER_NONE);
		if (i->op == VIR_OP_CLZ)
			x64_record_emit_three_registers(e, X64_OP_XORI, 4, d, d, X64_REGISTER_NONE)->immediate = sz * 8 - 1;
		return;
	case VIR_OP_IBR:
		x64_record_emit_three_registers(e, X64_OP_JMPR, 8, X64_REGISTER_NONE, a, X64_REGISTER_NONE);
		/* Static initializers and &&label name these entry points. A normal
		 * branch to the same C label has its own parallel copies. */
		for (uint32_t k = 0; k < L->function->extra[i->c]; k++) {
			uint32_t target = L->function->extra[i->c + 1 + k];
			x64_named(e, e->vir_builder->indirect_targets[k].reference);
			x64_jump(e, x64_vir_edge_target(L, b, k, target));
		}
		return;
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
		record *r = x64_record_emit(e, X64_OP_JCC); r->condition = cc; r->label = yes;
		if(no==L->label[L->chase[nt]])x64_vir_goto_emit(L,b,nt);else x64_jump(e,no);return;
	}
	case VIR_OP_RET: x64_jump(e, L->ret); return;
	case VIR_OP_TRAP: x64_record_emit(e, X64_OP_UD2); return;
	default: abort();
	}
}
