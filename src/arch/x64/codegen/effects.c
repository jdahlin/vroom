/* Physical-register and observable effects of x86 records. Keep this alongside
 * the printer: several records expand into multiple machine instructions.
 */
#include "x64impl.h"

#define X_BIT(r) (UINT64_C(1) << (r))
#define X_GP_ARGS (X_BIT(RDI) | X_BIT(RSI) | X_BIT(RDX) | X_BIT(RCX) | X_BIT(R8) | X_BIT(R9))
#define X_FP_ARGS (UINT64_C(0xff) << XMM0)
#define X_CALL_DEF (X_TEMPS | X_BIT(R11) | (UINT64_C(0xffff) << XMM0))
#define X_CALL_USE (X_GP_ARGS | X_FP_ARGS | X_BIT(RAX) | X_BIT(R10) | X_BIT(RSP))
#define X_ALL_REGS ((UINT64_C(1) << (XMM15 + 1)) - 1)
#define X_D RECORD_OPERAND_DEFINES_0
#define X_U RECORD_OPERAND_USES_1
#define X_V RECORD_OPERAND_USES_2
#define X_P RECORD_EFFECT_PURE
#define X_W RECORD_EFFECT_WRITES_FLAGS
#define X_R RECORD_EFFECT_READS_FLAGS
#define X_MEM (RECORD_EFFECT_LOAD | RECORD_EFFECT_STORE | RECORD_EFFECT_TRAP)
#define X_CALL_FLAGS (X_MEM | RECORD_EFFECT_CALL | X_W | RECORD_EFFECT_FLOAT | RECORD_EFFECT_BARRIER)
#define X_DESC(regs_, flags_) { .registers = (regs_), .flags = (flags_) }

static const record_op_descriptor x64_desc[X_NOPS] = {
	[X_CMOV] = X_DESC(X_D | X_U | X_V, X_P | X_R),
	[X_CMPM] = X_DESC(X_U | X_V | RECORD_OPERAND_USES_3, RECORD_EFFECT_LOAD | RECORD_EFFECT_TRAP | X_W),
	[X_TESTM] = X_DESC(X_U | RECORD_OPERAND_USES_3, RECORD_EFFECT_LOAD | RECORD_EFFECT_TRAP | X_W),
	[X_LDX] = X_DESC(X_D | X_U | RECORD_OPERAND_USES_3, RECORD_EFFECT_LOAD | RECORD_EFFECT_TRAP),
	[X_STX] = X_DESC(X_U | X_V | RECORD_OPERAND_USES_3, RECORD_EFFECT_STORE | RECORD_EFFECT_TRAP),
	[X_LABEL] = X_DESC(0, RECORD_EFFECT_LABEL),
	[X_MOV] = X_DESC(X_D | X_U, X_P),
	[X_MOVI] = X_DESC(X_D, X_P),
	[X_LD] = X_DESC(X_D | X_U, RECORD_EFFECT_LOAD | RECORD_EFFECT_TRAP),
	[X_ST] = X_DESC(X_U | X_V, RECORD_EFFECT_STORE | RECORD_EFFECT_TRAP),
	[X_LEA] = X_DESC(X_D | X_U, X_P),
	[X_LEASCALE] = X_DESC(X_D | X_U | X_V, X_P),
	[X_ADD] = X_DESC(X_D | X_U | X_V, X_P | X_W),
	[X_SUB] = X_DESC(X_D | X_U | X_V, X_P | X_W),
	[X_AND] = X_DESC(X_D | X_U | X_V, X_P | X_W),
	[X_OR] = X_DESC(X_D | X_U | X_V, X_P | X_W),
	[X_XOR] = X_DESC(X_D | X_U | X_V, X_P | X_W),
	[X_IMUL] = X_DESC(X_D | X_U | X_V, X_P | X_W),
	[X_ADC] = X_DESC(X_D | X_U | X_V, X_P | X_R | X_W),
	[X_SBB] = X_DESC(X_D | X_U | X_V, X_P | X_R | X_W),
	[X_ADDI] = X_DESC(X_D | X_U, X_P | X_W),
	[X_SUBI] = X_DESC(X_D | X_U, X_P | X_W),
	[X_ANDI] = X_DESC(X_D | X_U, X_P | X_W),
	[X_ORI] = X_DESC(X_D | X_U, X_P | X_W),
	[X_XORI] = X_DESC(X_D | X_U, X_P | X_W),
	[X_IMULI] = X_DESC(X_D | X_U, X_P | X_W),
	/* A zero (possibly variable) shift count preserves flags. */
	[X_SHLI] = X_DESC(X_D | X_U, X_P | X_R | X_W),
	[X_SHRI] = X_DESC(X_D | X_U, X_P | X_R | X_W),
	[X_SARI] = X_DESC(X_D | X_U, X_P | X_R | X_W),
	[X_SHL] = { .registers = X_D | X_U | X_V, .flags = X_P | X_R | X_W, .implicit_used = X_BIT(RCX) },
	[X_SHR] = { .registers = X_D | X_U | X_V, .flags = X_P | X_R | X_W, .implicit_used = X_BIT(RCX) },
	[X_SAR] = { .registers = X_D | X_U | X_V, .flags = X_P | X_R | X_W, .implicit_used = X_BIT(RCX) },
	[X_NEG] = X_DESC(X_D | X_U, X_P | X_W),
	[X_NOT] = X_DESC(X_D | X_U, X_P),
	[X_CMP] = X_DESC(X_U | X_V, X_P | X_W),
	[X_CMPI] = X_DESC(X_U, X_P | X_W),
	[X_TEST] = X_DESC(X_U, X_P | X_W),
	[X_SETCC] = X_DESC(X_D, X_P | X_R),
	[X_JMP] = { .branch = RECORD_BRANCH_JUMP },
	[X_JCC] = { .branch = RECORD_BRANCH_CONDITIONAL, .flags = X_R },
	[X_CALL] = { .flags = X_CALL_FLAGS, .implicit_defined = X_CALL_DEF, .implicit_used = X_CALL_USE },
	[X_CALLR] = { .registers = X_U, .flags = X_CALL_FLAGS, .implicit_defined = X_CALL_DEF, .implicit_used = X_CALL_USE },
	[X_TAIL] = { .branch = RECORD_BRANCH_RETURN, .flags = RECORD_EFFECT_BARRIER, .implicit_used = X_GP_ARGS },
	[X_PUSH] = { .registers = X_U, .flags = RECORD_EFFECT_STORE | RECORD_EFFECT_TRAP, .implicit_defined = X_BIT(RSP), .implicit_used = X_BIT(RSP) },
	[X_POP] = { .registers = X_D, .flags = RECORD_EFFECT_LOAD | RECORD_EFFECT_TRAP, .implicit_defined = X_BIT(RSP), .implicit_used = X_BIT(RSP) },
	[X_ADJSP] = { .flags = X_P | X_W, .implicit_defined = X_BIT(RSP), .implicit_used = X_BIT(RSP) },
	[X_CQO] = { .flags = X_P, .implicit_defined = X_BIT(RDX), .implicit_used = X_BIT(RAX) },
	[X_IDIV] = { .registers = X_U, .flags = RECORD_EFFECT_TRAP | X_W, .implicit_defined = X_BIT(RAX) | X_BIT(RDX), .implicit_used = X_BIT(RAX) | X_BIT(RDX) },
	[X_DIV] = { .registers = X_U, .flags = RECORD_EFFECT_TRAP | X_W, .implicit_defined = X_BIT(RAX) | X_BIT(RDX), .implicit_used = X_BIT(RAX) | X_BIT(RDX) },
	[X_MULW] = { .registers = X_U, .flags = X_P | X_W, .implicit_defined = X_BIT(RAX) | X_BIT(RDX), .implicit_used = X_BIT(RAX) },
	[X_SMULW] = { .registers = X_U, .flags = X_P | X_W, .implicit_defined = X_BIT(RAX) | X_BIT(RDX), .implicit_used = X_BIT(RAX) },
	[X_UD2] = X_DESC(0, RECORD_EFFECT_TRAP | RECORD_EFFECT_BARRIER),
	[X_TLS] = X_DESC(X_D, RECORD_EFFECT_LOAD | RECORD_EFFECT_TRAP), /* dynamic model below */
	[X_GOT] = X_DESC(X_D, RECORD_EFFECT_LOAD | RECORD_EFFECT_TRAP),
	[X_XCHG] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U, X_MEM | RECORD_EFFECT_BARRIER),
	[X_XADD] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U, X_MEM | RECORD_EFFECT_BARRIER | X_W),
	[X_CMPXCHG] = { .registers = X_U | X_V, .flags = X_MEM | RECORD_EFFECT_BARRIER | X_W, .implicit_defined = X_BIT(RAX), .implicit_used = X_BIT(RAX) },
	[X_MFENCE] = X_DESC(0, RECORD_EFFECT_BARRIER),
	[X_BLENDVB] = { .flags = X_P, .implicit_defined = X_BIT(XMM0 + 1),
		.implicit_used = X_BIT(XMM0) | X_BIT(XMM0 + 1) | X_BIT(XMM0 + 2) },
	[X_PCMPSTR] = { .flags = X_P | X_W, .implicit_used = X_BIT(XMM0) | X_BIT(XMM0 + 1) },
	[X_RAW] = { .branch = RECORD_BRANCH_INDIRECT, .flags = X_MEM | RECORD_EFFECT_BARRIER | X_R | X_W | RECORD_EFFECT_FLOAT, .implicit_defined = X_ALL_REGS, .implicit_used = X_ALL_REGS },
	[X_BARRIER] = X_DESC(0, RECORD_EFFECT_LOAD | RECORD_EFFECT_STORE | RECORD_EFFECT_BARRIER | X_R | X_W),
	[X_LEAL] = X_DESC(X_D, X_P),
	[X_JMPR] = { .registers = X_U, .branch = RECORD_BRANCH_INDIRECT },
	[X_JTE] = X_DESC(0, RECORD_EFFECT_BARRIER),
	[X_NAMED] = X_DESC(0, RECORD_EFFECT_LABEL | RECORD_EFFECT_BARRIER),
	/* Undefined destination for zero input is conservatively an old value. */
	[X_BSF] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U, X_P | X_W),
	[X_BSR] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U, X_P | X_W),
	[X_BSWAP] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U, X_P),
	[X_EXT] = X_DESC(X_D | X_U, X_P),
	/* Direction flag is an input even though REP leaves arithmetic flags alone. */
	[X_REP] = { .flags = X_MEM | RECORD_EFFECT_BARRIER | X_R, .implicit_defined = X_BIT(RCX) | X_BIT(RDI), .implicit_used = X_BIT(RCX) | X_BIT(RDI) },
	[X_FADD] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U | X_V, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP),
	[X_FSUB] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U | X_V, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP),
	[X_FMUL] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U | X_V, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP),
	[X_FDIV] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U | X_V, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP),
	[X_FXOR] = X_DESC(X_D | X_U | X_V, X_P),
	[X_FSQRT] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP),
	[X_FCMP] = X_DESC(X_U | X_V, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP | X_W),
	[X_FSETCC] = { .registers = X_D, .flags = X_P | X_R | X_W, .implicit_defined = X_BIT(R11), .implicit_used = X_BIT(R11) },
	[X_CVTI2F] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP),
	[X_CVTF2I] = X_DESC(X_D | X_U, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP),
	[X_CVTF2F] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP),
	/* x87 stack and control/status state are not physical GPR/XMM bits. */
	[X_X87] = X_DESC(X_U, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP | RECORD_EFFECT_BARRIER),
	[X_RESUME_FRAME] = { .flags = RECORD_EFFECT_BARRIER | X_W, .implicit_defined = X_BIT(R12), .implicit_used = X_BIT(RBP) },
	[X_PROBE] = { .flags = RECORD_EFFECT_LOAD | RECORD_EFFECT_STORE | RECORD_EFFECT_TRAP | RECORD_EFFECT_BARRIER | X_W, .implicit_used = X_BIT(RAX) | X_BIT(RSP) },
	[X_FRAMEADDR] = { .registers = X_D, .flags = X_P, .implicit_used = X_BIT(RBP) },
};

static record_register_mask x64_effect_reg(unsigned r)
{
	if(r==XFRAME)return X_BIT(RSP);
	return r <= XMM15 ? X_BIT(r) : 0; /* RIP is not a mutable register. */
}

VPRIV record_effects x64_effects(const codegen_context *e, const record *i)
{
	if (i->opcode >= X_NOPS)
		return (record_effects){ .defines = X_ALL_REGS, .used = X_ALL_REGS,
			.flags = X_MEM | RECORD_EFFECT_FLOAT | RECORD_EFFECT_BARRIER | X_R | X_W, .branch = RECORD_BRANCH_INDIRECT };
	const record_op_descriptor *d = &x64_desc[i->opcode];
	/* A newly added opcode without metadata must be opaque, never a no-op. */
	if (!d->registers && !d->branch && !d->flags && !d->implicit_defined && !d->implicit_used)
		return (record_effects){ .defines = X_ALL_REGS, .used = X_ALL_REGS,
			.flags = X_MEM | RECORD_EFFECT_FLOAT | RECORD_EFFECT_BARRIER | X_R | X_W, .branch = RECORD_BRANCH_INDIRECT };
	record_effects f = { .defines = d->implicit_defined, .used = d->implicit_used, .flags = d->flags, .branch = d->branch };
	if(i->opcode==X_BSWAP && i->size==2)f.flags|=RECORD_EFFECT_WRITES_FLAGS; /* byte rotate */
	if (d->registers & RECORD_OPERAND_DEFINES_0) f.defines |= x64_effect_reg(i->registers[0]);
	if (d->registers & RECORD_OPERAND_USES_0) f.used |= x64_effect_reg(i->registers[0]);
	if (d->registers & RECORD_OPERAND_USES_1) f.used |= x64_effect_reg(i->registers[1]);
	if (d->registers & RECORD_OPERAND_USES_2) f.used |= x64_effect_reg(i->registers[2]);
	if (d->registers & RECORD_OPERAND_USES_3) f.used |= x64_effect_reg(i->registers[3]);

	/* Only MOV and narrow ALU/atomic operations can leave GPR high bits.
	 * SETCC and EXT contain a widening move; LD also always widens GPRs.
	 */
	if ((d->registers & RECORD_OPERAND_DEFINES_0) && i->size < 4 && i->registers[0] < XMM0 &&
	    i->opcode != X_LD && i->opcode != X_LDX && i->opcode != X_EXT && i->opcode != X_SETCC && i->opcode != X_FSETCC)
		f.used |= x64_effect_reg(i->registers[0]);
	if ((i->opcode == X_LD || i->opcode == X_ST || i->opcode == X_LDX || i->opcode == X_STX || i->opcode == X_TESTM || i->opcode == X_CMPM || i->opcode == X_LEA ||
	     i->opcode == X_XCHG || i->opcode == X_XADD || i->opcode == X_CMPXCHG || i->opcode == X_X87) &&
	    i->registers[1] == RBP && i->offset < 0 && (!e || e->walk_state->frame_alignment > 16))
		f.used |= X_BIT(R12); /* printer's realigned local-frame base */
	if (i->opcode == X_RESUME_FRAME) {
		if (e && e->walk_state && e->walk_state->frame_alignment <= 16)
			f = (record_effects){ .flags = RECORD_EFFECT_BARRIER }; /* printer emits nothing */
		else if (!e || !e->walk_state)
			f.used |= X_BIT(R12); /* unknown alignment may preserve its old value */
	} else if (i->opcode == X_PCMPSTR) {
		f.defines |= X_BIT((i->condition & 1) ? RCX : XMM0);
		if (!(i->condition & 2)) f.used |= X_BIT(RAX) | X_BIT(RDX);
	} else if (i->opcode == X_TLS) {
		if (i->condition == 2) {
			f.defines = X_CALL_DEF;
			f.used = X_CALL_USE & ~X_BIT(RDI); /* descriptor overwrites RDI */
			f.flags = X_CALL_FLAGS;
		} else if (i->condition) {
			f.flags |= X_W; /* initial-exec uses addq, local-exec LEA does not */
		}
		if (e && CODEGEN_IS_WIN64(e->compile_context))
			f.defines |= X_BIT(R11); /* Windows: _tls_index through r11 */
	} else if (i->opcode == X_REP) {
		if (i->condition) f.used |= X_BIT(RAX);
		else f.defines |= X_BIT(RSI), f.used |= X_BIT(RSI);
	} else if (i->opcode == X_X87) {
		switch (i->condition) {
		case XF_LDT: case XF_LD: case XF_ILD: case XF_LDCW: f.flags |= RECORD_EFFECT_LOAD; break;
		case XF_STPT: case XF_STP: case XF_ISTP: case XF_STCW: f.flags |= RECORD_EFFECT_STORE; break;
		case XF_UCOMIP: f.flags |= X_W; break;
		default: break;
		}
	}
	return f;
}

/* Separate one-byte control-flow view: unreachable cleanup need not decode
 * registers or inspect floating/implicit effects. Zero means opaque entry.
 */
VPRIV const uint8_t *x64_flow_table(void)
{
	static const uint8_t flow[X_NOPS] = {
		[X_LABEL] = RECORD_FLOW_ENTRY,
		[X_MOV] = RECORD_FLOW_NEXT,
		[X_CMOV] = RECORD_FLOW_NEXT,
		[X_TESTM] = RECORD_FLOW_NEXT,
		[X_MOVI] = RECORD_FLOW_NEXT,
		[X_LD] = RECORD_FLOW_NEXT,
		[X_LDX] = RECORD_FLOW_NEXT,
		[X_STX] = RECORD_FLOW_NEXT,
		[X_ST] = RECORD_FLOW_NEXT,
		[X_LEA] = RECORD_FLOW_NEXT,
		[X_LEASCALE] = RECORD_FLOW_NEXT,
		[X_ADD] = RECORD_FLOW_NEXT,
		[X_SUB] = RECORD_FLOW_NEXT,
		[X_AND] = RECORD_FLOW_NEXT,
		[X_OR] = RECORD_FLOW_NEXT,
		[X_XOR] = RECORD_FLOW_NEXT,
		[X_IMUL] = RECORD_FLOW_NEXT,
		[X_ADC] = RECORD_FLOW_NEXT,
		[X_SBB] = RECORD_FLOW_NEXT,
		[X_ADDI] = RECORD_FLOW_NEXT,
		[X_SUBI] = RECORD_FLOW_NEXT,
		[X_ANDI] = RECORD_FLOW_NEXT,
		[X_ORI] = RECORD_FLOW_NEXT,
		[X_XORI] = RECORD_FLOW_NEXT,
		[X_IMULI] = RECORD_FLOW_NEXT,
		[X_SHLI] = RECORD_FLOW_NEXT,
		[X_SHRI] = RECORD_FLOW_NEXT,
		[X_SARI] = RECORD_FLOW_NEXT,
		[X_SHL] = RECORD_FLOW_NEXT,
		[X_SHR] = RECORD_FLOW_NEXT,
		[X_SAR] = RECORD_FLOW_NEXT,
		[X_NEG] = RECORD_FLOW_NEXT,
		[X_NOT] = RECORD_FLOW_NEXT,
		[X_CMP] = RECORD_FLOW_NEXT,
		[X_CMPM] = RECORD_FLOW_NEXT,
		[X_CMPI] = RECORD_FLOW_NEXT,
		[X_TEST] = RECORD_FLOW_NEXT,
		[X_SETCC] = RECORD_FLOW_NEXT,
		[X_JMP] = RECORD_FLOW_STOP,
		[X_JCC] = RECORD_FLOW_NEXT,
		[X_CALL] = RECORD_FLOW_NEXT,
		[X_CALLR] = RECORD_FLOW_NEXT,
		[X_TAIL] = RECORD_FLOW_STOP,
		[X_PUSH] = RECORD_FLOW_NEXT,
		[X_POP] = RECORD_FLOW_NEXT,
		[X_ADJSP] = RECORD_FLOW_NEXT,
		[X_CQO] = RECORD_FLOW_NEXT,
		[X_IDIV] = RECORD_FLOW_NEXT,
		[X_DIV] = RECORD_FLOW_NEXT,
		[X_MULW] = RECORD_FLOW_NEXT,
		[X_SMULW] = RECORD_FLOW_NEXT,
		[X_UD2] = RECORD_FLOW_NEXT,
		[X_TLS] = RECORD_FLOW_NEXT,
		[X_GOT] = RECORD_FLOW_NEXT,
		[X_XCHG] = RECORD_FLOW_NEXT,
		[X_XADD] = RECORD_FLOW_NEXT,
		[X_CMPXCHG] = RECORD_FLOW_NEXT,
		[X_MFENCE] = RECORD_FLOW_NEXT,
		[X_BLENDVB] = RECORD_FLOW_NEXT,
		[X_PCMPSTR] = RECORD_FLOW_NEXT,
		[X_RAW] = RECORD_FLOW_ENTRY,
		[X_BARRIER] = RECORD_FLOW_ENTRY,
		[X_LEAL] = RECORD_FLOW_NEXT,
		[X_JMPR] = RECORD_FLOW_ENTRY,
		[X_JTE] = RECORD_FLOW_ENTRY,
		[X_NAMED] = RECORD_FLOW_ENTRY,
		[X_BSF] = RECORD_FLOW_NEXT,
		[X_BSR] = RECORD_FLOW_NEXT,
		[X_BSWAP] = RECORD_FLOW_NEXT,
		[X_EXT] = RECORD_FLOW_NEXT,
		[X_REP] = RECORD_FLOW_NEXT,
		[X_FADD] = RECORD_FLOW_NEXT,
		[X_FSUB] = RECORD_FLOW_NEXT,
		[X_FMUL] = RECORD_FLOW_NEXT,
		[X_FDIV] = RECORD_FLOW_NEXT,
		[X_FXOR] = RECORD_FLOW_NEXT,
		[X_FSQRT] = RECORD_FLOW_NEXT,
		[X_FCMP] = RECORD_FLOW_NEXT,
		[X_FSETCC] = RECORD_FLOW_NEXT,
		[X_CVTI2F] = RECORD_FLOW_NEXT,
		[X_CVTF2I] = RECORD_FLOW_NEXT,
		[X_CVTF2F] = RECORD_FLOW_NEXT,
		[X_X87] = RECORD_FLOW_NEXT,
		[X_RESUME_FRAME] = RECORD_FLOW_NEXT,
		[X_PROBE] = RECORD_FLOW_NEXT,
		[X_FRAMEADDR] = RECORD_FLOW_NEXT,
	};
	return flow;
}
