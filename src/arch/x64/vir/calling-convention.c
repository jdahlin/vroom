/* x86-64 lowering of VIR: the SysV argument registers and the fixed operands of calls, parameters, switches and block copies.
 * ms: the Microsoft convention (Windows x64; PLAN-win64.md WIN2): argument g in its position's register (rcx rdx r8 r9 or
 * xmm0-xmm3), else at 16 + 8g above the frame base (the caller's 32 bytes of shadow space below it). */

#include "lowering-internal.h"

/* Call words start with the hidden destination for a memory result. */
VPRIV uint32_t x64_vir_param_word(const vir_function *f, const vir_insn *i)
{
	if (i->a == VIR_GROUP_RESULT) return 0;
	uint32_t word = i->b + (VIR_GROUP_KIND(f->extra[f->signature + 1]) == VIR_GROUP_SRET);
	for (uint32_t k = 0; k < i->a; k++) {
		uint32_t group = f->extra[f->signature + 3 + 2*k];
		word += VIR_GROUP_KIND(group) == VIR_GROUP_MEM ? 1 : VIR_GROUP_PIECE_COUNT(group);
	}
	return word;
}

VPRIV uint8_t x64_vir_argument_register(const vir_function *f, uint32_t sig, uint32_t word, int *stack, bool ms)
{
	bool sret = VIR_GROUP_KIND(f->extra[sig + 1]) == VIR_GROUP_SRET;
	uint32_t ni = sret, nf = 0;
	int off = 16;
	if (sret && !word) return RDI;
	uint32_t g = word - sret;
	if (ms) {
		bool fp = VIR_GROUP_CLASS(f->extra[sig + 3 + 2 * g], 0) != VIR_CLASS_INT;
		*stack = 16 + 8 * (int)g;
		return g >= 4 ? VIR_REGISTER_NONE : fp ? VIR_REGISTER_MAKE(1, g) : (uint8_t)x64w_argreg((int)g);
	}
	for (uint32_t k = 0; k < VIR_SIGNATURE_GROUP_COUNT(f->extra[sig]); k++) {
		uint32_t w0 = f->extra[sig + 3 + 2*k], w1 = f->extra[sig + 4 + 2*k];
		uint32_t pieces = VIR_GROUP_PIECE_COUNT(w0), gp = 0;
		for (uint32_t j = 0; j < pieces; j++) gp += VIR_GROUP_CLASS(w0, j) == VIR_CLASS_INT;
		bool memory = VIR_GROUP_KIND(w0) == VIR_GROUP_MEM;
		uint32_t words = memory ? 1 : pieces;
		if (memory || ni + gp > 6 || nf + pieces - gp > 8) {
			int align = 1 << VIR_GROUP_ALIGN_LOG2(w1);
			off = (off + align - 1) & -align;
			if (g < words) {
				*stack = off + 8 * g;
				return VIR_REGISTER_NONE;
			}
			off += (VIR_GROUP_SIZE(w1) + 7) & ~7u;
		} else {
			for (uint32_t j = 0; j < pieces; j++) {
				uint8_t reg = VIR_GROUP_CLASS(w0, j) == VIR_CLASS_INT ? x64_argument_register(ni++) : VIR_REGISTER_MAKE(1, nf++);
				if (j == g) return reg;
			}
		}
		g -= words;
	}
	return VIR_REGISTER_NONE;
}

VPRIV uint32_t x64_vir_stack_object_size(const vir_function *f, uint32_t sig, uint32_t word)
{
	bool sret = VIR_GROUP_KIND(f->extra[sig + 1]) == VIR_GROUP_SRET;
	if (sret && !word) return 0;
	word -= sret;
	for (uint32_t k = 0; k < VIR_SIGNATURE_GROUP_COUNT(f->extra[sig]); k++) {
		uint32_t at = sig + 3 + 2*k;
		bool mem = VIR_GROUP_KIND(f->extra[at]) == VIR_GROUP_MEM;
		uint32_t n = mem ? 1 : VIR_GROUP_PIECE_COUNT(f->extra[at]);
		if (word < n) return mem ? VIR_GROUP_SIZE(f->extra[at+1]) : 0;
		word -= n;
	}
	return 0;
}

static uint8_t x64_vir_return_register(uint32_t group, uint32_t piece)
{
	bool fp = VIR_GROUP_CLASS(group, piece) != VIR_CLASS_INT;
	bool second = piece && (VIR_GROUP_CLASS(group, 0) != VIR_CLASS_INT) == fp;
	return fp ? VIR_REGISTER_MAKE(1, second) : second ? RDX : RAX;
}

VPRIV void x64_vir_fixed_registers_get(const vir_function *f, uint32_t v, vir_register_constraint *x, void *ctx)
{
	const vir_insn *i = &f->insns[v];
	const x64_vir_context *target=ctx;
	bool r10=target && target->scratch_register10, ms=target && target->ms;
	if (i->op == VIR_OP_PARAM) {
		int stack;
		x->result = x64_vir_argument_register(f, f->signature, x64_vir_param_word(f, i), &stack, ms);
	} else if (i->op == VIR_OP_ACAS) {
		x->operand_count = 3;
		x->operands[1] = RAX;
		x->result = RAX;
		x->clobbered[0] = 1u << RAX;
	} else if (i->op == VIR_OP_STORE && f->insns[i->a].op == VIR_OP_SLOTADDR) {
		/* The frame displacement is part of the store's addressing mode. */
		x->operand_count = 2;
		x->mem = 1;
	} else if(i->op==VIR_OP_SWITCH && r10) {
		/* A private dense selector can be produced directly in the dispatch
		 * register. Range chains and shared selectors keep their original. */
		if(target->uses[i->a]==1 && x64_vir_switch_is_dense(f,i,NULL,NULL)) {
			x->operand_count=1;x->operands[0]=R10;x->clobbered[0]=UINT64_C(1)<<R10;
		} else x->edge_clobbered[0]=UINT64_C(1)<<R10;
	} else if (i->op == VIR_OP_MEMCPY || i->op == VIR_OP_MEMSET) {
		x->operand_count = 3;
		if(x64_vir_memory_block_is_small(f,i)) {x->mem=i->op==VIR_OP_MEMSET?6:4;return;}
		x->operands[0] = RDI;
		x->operands[1] = i->op == VIR_OP_MEMSET ? RAX : RSI;
		x->operands[2] = RCX;
		x->clobbered[0] = (1u << RDI) | (1u << RCX) | (i->op == VIR_OP_MEMCPY ? 1u << RSI : 0);
	} else if (i->op == VIR_OP_CALL) {
		uint32_t n = f->extra[i->c];
		x->operand_count = n + 1;
		for (uint32_t k = 0; k < n; k++) {
			int stack;
			x->operands[k + 1] = x64_vir_argument_register(f, i->b, k, &stack, ms);
			if (x->operands[k + 1] == VIR_REGISTER_NONE && !x64_vir_stack_object_size(f, i->b, k)) x->mem |= 1u << (k + 1);
		}
		const vir_insn *callee = &f->insns[i->a];
		if (callee->op == VIR_OP_SYMADDR && CODEGEN_REFERENCE_KIND(callee->a) < CODEGEN_REFERENCE_TLS_LE && !callee->b && !callee->c) x->mem |= 1;
		x->clobbered[0] = r10?X64_TEMPORARIES:X64_TEMPORARIES & ~(1u << R10);
		x->clobbered[1] = (1u << 14) - 1;
		if (ms) {	/* a Microsoft callee keeps rsi, rdi and xmm6-xmm15 */
			x->clobbered[0] &= ~((1u << RSI) | (1u << RDI));
			x->clobbered[1] = (1u << 6) - 1;
		}
		if (i->type != VIR_TYPE_VOID && i->type != VIR_TYPE_MULTI) x->result = vir_register_class(i->type) ? VIR_REGISTER_MAKE(1, 0) : RAX;
	} else if (i->op == VIR_OP_PROJ) {
		x->result = x64_vir_return_register(f->extra[f->insns[i->a].b + 1], i->b);
	} else if (i->op == VIR_OP_RET && f->extra[i->c]) {
		x->operand_count = f->extra[i->c];
		for (uint32_t j = 0; j < x->operand_count; j++)
			x->operands[j] = x64_vir_return_register(f->extra[f->signature + 1], j);
	} else if (i->op == VIR_OP_SDIV || i->op == VIR_OP_UDIV || i->op == VIR_OP_SREM || i->op == VIR_OP_UREM || i->op == VIR_OP_SMULHI || i->op == VIR_OP_UMULHI) {
		x->operand_count = 2;
		x->operands[0] = RAX;
		x->result = i->op == VIR_OP_SREM || i->op == VIR_OP_UREM || i->op == VIR_OP_SMULHI || i->op == VIR_OP_UMULHI ? RDX : RAX;
		x->clobbered[0] = (1u << RAX) | (1u << RDX);
	} else if (i->op == VIR_OP_SHL || i->op == VIR_OP_LSHR || i->op == VIR_OP_ASHR) {
		x->operand_count = 2;
		int64_t k;
		if (x64_vir_value_immediate(f,i->b,&k) && k>=0 && k<(i->type==VIR_TYPE_I32?32:64)) x->mem=2;
		else x->operands[1] = RCX;
	}
	int64_t k;
	if (!vir_register_class(i->type) && (i->op==VIR_OP_ADD || i->op==VIR_OP_SUB || i->op==VIR_OP_PTRADD || i->op==VIR_OP_PSUB ||
	    i->op==VIR_OP_AND || i->op==VIR_OP_OR || i->op==VIR_OP_XOR || i->op==VIR_OP_MUL || i->op==VIR_OP_ICMP) && x64_vir_value_immediate(f,i->b,&k)) {
		x->operand_count=2;x->mem|=2;
	}
	/* Prefer the dying first input for x86's destructive operations. */
	switch(i->op) {
	case VIR_OP_SUB: case VIR_OP_PSUB: case VIR_OP_MUL: case VIR_OP_AND: case VIR_OP_OR: case VIR_OP_XOR:
	case VIR_OP_NEG: case VIR_OP_NOT: case VIR_OP_SHL: case VIR_OP_LSHR: case VIR_OP_ASHR:
	case VIR_OP_EXT: case VIR_OP_TRUNC: case VIR_OP_P2I: case VIR_OP_I2P:
	case VIR_OP_FADD: case VIR_OP_FSUB: case VIR_OP_FMUL: case VIR_OP_FDIV:
		x->flags|=VIR_CONSTRAINT_FLAG_TIED;break;
	default:break;
	}
}
