/* Physical-register and observable effects of x86 records. Keep this alongside
 * the printer: several records expand into multiple machine instructions.
 */
#include "x64-internal.h"

#define X64_REGISTER_BIT(r) (UINT64_C(1) << (r))
#define X64_GENERAL_ARGUMENT_REGISTERS (X64_REGISTER_BIT(RDI) | X64_REGISTER_BIT(RSI) | X64_REGISTER_BIT(RDX) | X64_REGISTER_BIT(RCX) | X64_REGISTER_BIT(R8) | X64_REGISTER_BIT(R9))
#define X64_FLOAT_ARGUMENT_REGISTERS (UINT64_C(0xff) << XMM0)
#define X64_CALL_DEFINED_REGISTERS (X64_TEMPORARIES | X64_REGISTER_BIT(R11) | (UINT64_C(0xffff) << XMM0))
#define X64_CALL_USED_REGISTERS (X64_GENERAL_ARGUMENT_REGISTERS | X64_FLOAT_ARGUMENT_REGISTERS | X64_REGISTER_BIT(RAX) | X64_REGISTER_BIT(R10) | X64_REGISTER_BIT(RSP))
#define X64_ALL_REGISTERS ((UINT64_C(1) << (XMM15 + 1)) - 1)
#define X_D RECORD_OPERAND_DEFINES_0
#define X_U RECORD_OPERAND_USES_1
#define X_V RECORD_OPERAND_USES_2
#define X_P RECORD_EFFECT_PURE
#define X_W RECORD_EFFECT_WRITES_FLAGS
#define X_R RECORD_EFFECT_READS_FLAGS
#define X_MEM (RECORD_EFFECT_LOAD | RECORD_EFFECT_STORE | RECORD_EFFECT_TRAP)
#define X_CALL_FLAGS (X_MEM | RECORD_EFFECT_CALL | X_W | RECORD_EFFECT_FLOAT | RECORD_EFFECT_BARRIER)
#define X_DESC(regs_, flags_) { .registers = (regs_), .flags = (flags_) }

static const record_op_descriptor x64_op_descriptors[X64_OP_COUNT] = {
	[X64_OP_CMOV] = X_DESC(X_D | X_U | X_V, X_P | X_R),
	[X64_OP_CMPM] = X_DESC(X_U | X_V | RECORD_OPERAND_USES_3, RECORD_EFFECT_LOAD | RECORD_EFFECT_TRAP | X_W),
	[X64_OP_TESTM] = X_DESC(X_U | RECORD_OPERAND_USES_3, RECORD_EFFECT_LOAD | RECORD_EFFECT_TRAP | X_W),
	[X64_OP_LDX] = X_DESC(X_D | X_U | RECORD_OPERAND_USES_3, RECORD_EFFECT_LOAD | RECORD_EFFECT_TRAP),
	[X64_OP_STX] = X_DESC(X_U | X_V | RECORD_OPERAND_USES_3, RECORD_EFFECT_STORE | RECORD_EFFECT_TRAP),
	[X64_OP_LABEL] = X_DESC(0, RECORD_EFFECT_LABEL),
	[X64_OP_MOV] = X_DESC(X_D | X_U, X_P),
	[X64_OP_MOVI] = X_DESC(X_D, X_P),
	[X64_OP_LD] = X_DESC(X_D | X_U, RECORD_EFFECT_LOAD | RECORD_EFFECT_TRAP),
	[X64_OP_ST] = X_DESC(X_U | X_V, RECORD_EFFECT_STORE | RECORD_EFFECT_TRAP),
	[X64_OP_LEA] = X_DESC(X_D | X_U, X_P),
	[X64_OP_LEASCALE] = X_DESC(X_D | X_U | X_V, X_P),
	[X64_OP_ADD] = X_DESC(X_D | X_U | X_V, X_P | X_W),
	[X64_OP_SUB] = X_DESC(X_D | X_U | X_V, X_P | X_W),
	[X64_OP_AND] = X_DESC(X_D | X_U | X_V, X_P | X_W),
	[X64_OP_OR] = X_DESC(X_D | X_U | X_V, X_P | X_W),
	[X64_OP_XOR] = X_DESC(X_D | X_U | X_V, X_P | X_W),
	[X64_OP_IMUL] = X_DESC(X_D | X_U | X_V, X_P | X_W),
	[X64_OP_ADC] = X_DESC(X_D | X_U | X_V, X_P | X_R | X_W),
	[X64_OP_SBB] = X_DESC(X_D | X_U | X_V, X_P | X_R | X_W),
	[X64_OP_ADDI] = X_DESC(X_D | X_U, X_P | X_W),
	[X64_OP_SUBI] = X_DESC(X_D | X_U, X_P | X_W),
	[X64_OP_ANDI] = X_DESC(X_D | X_U, X_P | X_W),
	[X64_OP_ORI] = X_DESC(X_D | X_U, X_P | X_W),
	[X64_OP_XORI] = X_DESC(X_D | X_U, X_P | X_W),
	[X64_OP_IMULI] = X_DESC(X_D | X_U, X_P | X_W),
	/* A zero (possibly variable) shift count preserves flags. */
	[X64_OP_SHLI] = X_DESC(X_D | X_U, X_P | X_R | X_W),
	[X64_OP_SHRI] = X_DESC(X_D | X_U, X_P | X_R | X_W),
	[X64_OP_SARI] = X_DESC(X_D | X_U, X_P | X_R | X_W),
	[X64_OP_SHL] = { .registers = X_D | X_U | X_V, .flags = X_P | X_R | X_W, .implicit_used = X64_REGISTER_BIT(RCX) },
	[X64_OP_SHR] = { .registers = X_D | X_U | X_V, .flags = X_P | X_R | X_W, .implicit_used = X64_REGISTER_BIT(RCX) },
	[X64_OP_SAR] = { .registers = X_D | X_U | X_V, .flags = X_P | X_R | X_W, .implicit_used = X64_REGISTER_BIT(RCX) },
	[X64_OP_NEG] = X_DESC(X_D | X_U, X_P | X_W),
	[X64_OP_NOT] = X_DESC(X_D | X_U, X_P),
	[X64_OP_CMP] = X_DESC(X_U | X_V, X_P | X_W),
	[X64_OP_CMPI] = X_DESC(X_U, X_P | X_W),
	[X64_OP_TEST] = X_DESC(X_U, X_P | X_W),
	[X64_OP_SETCC] = X_DESC(X_D, X_P | X_R),
	[X64_OP_JMP] = { .branch = RECORD_BRANCH_JUMP },
	[X64_OP_JCC] = { .branch = RECORD_BRANCH_CONDITIONAL, .flags = X_R },
	[X64_OP_CALL] = { .flags = X_CALL_FLAGS, .implicit_defined = X64_CALL_DEFINED_REGISTERS, .implicit_used = X64_CALL_USED_REGISTERS },
	[X64_OP_CALLR] = { .registers = X_U, .flags = X_CALL_FLAGS, .implicit_defined = X64_CALL_DEFINED_REGISTERS, .implicit_used = X64_CALL_USED_REGISTERS },
	[X64_OP_TAIL] = { .branch = RECORD_BRANCH_RETURN, .flags = RECORD_EFFECT_BARRIER, .implicit_used = X64_GENERAL_ARGUMENT_REGISTERS },
	[X64_OP_PUSH] = { .registers = X_U, .flags = RECORD_EFFECT_STORE | RECORD_EFFECT_TRAP, .implicit_defined = X64_REGISTER_BIT(RSP), .implicit_used = X64_REGISTER_BIT(RSP) },
	[X64_OP_POP] = { .registers = X_D, .flags = RECORD_EFFECT_LOAD | RECORD_EFFECT_TRAP, .implicit_defined = X64_REGISTER_BIT(RSP), .implicit_used = X64_REGISTER_BIT(RSP) },
	[X64_OP_ADJSP] = { .flags = X_P | X_W, .implicit_defined = X64_REGISTER_BIT(RSP), .implicit_used = X64_REGISTER_BIT(RSP) },
	[X64_OP_CQO] = { .flags = X_P, .implicit_defined = X64_REGISTER_BIT(RDX), .implicit_used = X64_REGISTER_BIT(RAX) },
	[X64_OP_IDIV] = { .registers = X_U, .flags = RECORD_EFFECT_TRAP | X_W, .implicit_defined = X64_REGISTER_BIT(RAX) | X64_REGISTER_BIT(RDX), .implicit_used = X64_REGISTER_BIT(RAX) | X64_REGISTER_BIT(RDX) },
	[X64_OP_DIV] = { .registers = X_U, .flags = RECORD_EFFECT_TRAP | X_W, .implicit_defined = X64_REGISTER_BIT(RAX) | X64_REGISTER_BIT(RDX), .implicit_used = X64_REGISTER_BIT(RAX) | X64_REGISTER_BIT(RDX) },
	[X64_OP_MULW] = { .registers = X_U, .flags = X_P | X_W, .implicit_defined = X64_REGISTER_BIT(RAX) | X64_REGISTER_BIT(RDX), .implicit_used = X64_REGISTER_BIT(RAX) },
	[X64_OP_SMULW] = { .registers = X_U, .flags = X_P | X_W, .implicit_defined = X64_REGISTER_BIT(RAX) | X64_REGISTER_BIT(RDX), .implicit_used = X64_REGISTER_BIT(RAX) },
	[X64_OP_UD2] = X_DESC(0, RECORD_EFFECT_TRAP | RECORD_EFFECT_BARRIER),
	[X64_OP_TLS] = X_DESC(X_D, RECORD_EFFECT_LOAD | RECORD_EFFECT_TRAP), /* dynamic model below */
	[X64_OP_GOT] = X_DESC(X_D, RECORD_EFFECT_LOAD | RECORD_EFFECT_TRAP),
	[X64_OP_XCHG] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U, X_MEM | RECORD_EFFECT_BARRIER),
	[X64_OP_XADD] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U, X_MEM | RECORD_EFFECT_BARRIER | X_W),
	[X64_OP_CMPXCHG] = { .registers = X_U | X_V, .flags = X_MEM | RECORD_EFFECT_BARRIER | X_W, .implicit_defined = X64_REGISTER_BIT(RAX), .implicit_used = X64_REGISTER_BIT(RAX) },
	[X64_OP_MFENCE] = X_DESC(0, RECORD_EFFECT_BARRIER),
	[X64_OP_BLENDVB] = { .flags = X_P, .implicit_defined = X64_REGISTER_BIT(XMM0 + 1),
		.implicit_used = X64_REGISTER_BIT(XMM0) | X64_REGISTER_BIT(XMM0 + 1) | X64_REGISTER_BIT(XMM0 + 2) },
	[X64_OP_PCMPSTR] = { .flags = X_P | X_W, .implicit_used = X64_REGISTER_BIT(XMM0) | X64_REGISTER_BIT(XMM0 + 1) },
	[X64_OP_RAW] = { .branch = RECORD_BRANCH_INDIRECT, .flags = X_MEM | RECORD_EFFECT_BARRIER | X_R | X_W | RECORD_EFFECT_FLOAT, .implicit_defined = X64_ALL_REGISTERS, .implicit_used = X64_ALL_REGISTERS },
	[X64_OP_BARRIER] = X_DESC(0, RECORD_EFFECT_LOAD | RECORD_EFFECT_STORE | RECORD_EFFECT_BARRIER | X_R | X_W),
	[X64_OP_LEAL] = X_DESC(X_D, X_P),
	[X64_OP_JMPR] = { .registers = X_U, .branch = RECORD_BRANCH_INDIRECT },
	[X64_OP_JTE] = X_DESC(0, RECORD_EFFECT_BARRIER),
	[X64_OP_NAMED] = X_DESC(0, RECORD_EFFECT_LABEL | RECORD_EFFECT_BARRIER),
	/* Undefined destination for zero input is conservatively an old value. */
	[X64_OP_BSF] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U, X_P | X_W),
	[X64_OP_POPCNT] = X_DESC(X_D | X_U, X_P | X_W),
	[X64_OP_BSR] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U, X_P | X_W),
	[X64_OP_BSWAP] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U, X_P),
	[X64_OP_EXT] = X_DESC(X_D | X_U, X_P),
	/* Direction flag is an input even though REP leaves arithmetic flags alone. */
	[X64_OP_REP] = { .flags = X_MEM | RECORD_EFFECT_BARRIER | X_R, .implicit_defined = X64_REGISTER_BIT(RCX) | X64_REGISTER_BIT(RDI), .implicit_used = X64_REGISTER_BIT(RCX) | X64_REGISTER_BIT(RDI) },
	[X64_OP_FADD] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U | X_V, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP),
	[X64_OP_FSUB] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U | X_V, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP),
	[X64_OP_FMUL] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U | X_V, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP),
	[X64_OP_FDIV] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U | X_V, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP),
	[X64_OP_FXOR] = X_DESC(X_D | X_U | X_V, X_P),
	[X64_OP_FSQRT] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP),
	[X64_OP_FCMP] = X_DESC(X_U | X_V, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP | X_W),
	[X64_OP_FSETCC] = { .registers = X_D, .flags = X_P | X_R | X_W, .implicit_defined = X64_REGISTER_BIT(R11), .implicit_used = X64_REGISTER_BIT(R11) },
	[X64_OP_CVTI2F] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP),
	[X64_OP_CVTF2I] = X_DESC(X_D | X_U, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP),
	[X64_OP_CVTF2F] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP),
	/* x87 stack and control/status state are not physical GPR/XMM bits. */
	[X64_OP_X87] = X_DESC(X_U, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP | RECORD_EFFECT_BARRIER),
	[X64_OP_RESUME_FRAME] = { .flags = RECORD_EFFECT_BARRIER | X_W, .implicit_defined = X64_REGISTER_BIT(R12), .implicit_used = X64_REGISTER_BIT(RBP) },
	[X64_OP_PROBE] = { .flags = RECORD_EFFECT_LOAD | RECORD_EFFECT_STORE | RECORD_EFFECT_TRAP | RECORD_EFFECT_BARRIER | X_W, .implicit_used = X64_REGISTER_BIT(RAX) | X64_REGISTER_BIT(RSP) },
	[X64_OP_FRAMEADDR] = { .registers = X_D, .flags = X_P, .implicit_used = X64_REGISTER_BIT(RBP) },
};

static record_register_mask x64_effect_register_mask(unsigned r)
{
	if(r==X64_REGISTER_FRAME)return X64_REGISTER_BIT(RSP);
	return r <= XMM15 ? X64_REGISTER_BIT(r) : 0; /* RIP is not a mutable register. */
}

VPRIV record_effects x64_record_effects(const codegen_context *e, const record *i)
{
	if (i->opcode >= X64_OP_COUNT)
		return (record_effects){ .defines = X64_ALL_REGISTERS, .used = X64_ALL_REGISTERS,
			.flags = X_MEM | RECORD_EFFECT_FLOAT | RECORD_EFFECT_BARRIER | X_R | X_W, .branch = RECORD_BRANCH_INDIRECT };
	const record_op_descriptor *d = &x64_op_descriptors[i->opcode];
	/* A newly added opcode without metadata must be opaque, never a no-op. */
	if (!d->registers && !d->branch && !d->flags && !d->implicit_defined && !d->implicit_used)
		return (record_effects){ .defines = X64_ALL_REGISTERS, .used = X64_ALL_REGISTERS,
			.flags = X_MEM | RECORD_EFFECT_FLOAT | RECORD_EFFECT_BARRIER | X_R | X_W, .branch = RECORD_BRANCH_INDIRECT };
	record_effects f = { .defines = d->implicit_defined, .used = d->implicit_used, .flags = d->flags, .branch = d->branch };
	if(i->opcode==X64_OP_BSWAP && i->size==2)f.flags|=RECORD_EFFECT_WRITES_FLAGS; /* byte rotate */
	if (d->registers & RECORD_OPERAND_DEFINES_0) f.defines |= x64_effect_register_mask(i->registers[0]);
	if (d->registers & RECORD_OPERAND_USES_0) f.used |= x64_effect_register_mask(i->registers[0]);
	if (d->registers & RECORD_OPERAND_USES_1) f.used |= x64_effect_register_mask(i->registers[1]);
	if (d->registers & RECORD_OPERAND_USES_2) f.used |= x64_effect_register_mask(i->registers[2]);
	if (d->registers & RECORD_OPERAND_USES_3) f.used |= x64_effect_register_mask(i->registers[3]);

	/* Only MOV and narrow ALU/atomic operations can leave GPR high bits.
	 * SETCC and EXT contain a widening move; LD also always widens GPRs.
	 */
	if ((d->registers & RECORD_OPERAND_DEFINES_0) && i->size < 4 && i->registers[0] < XMM0 &&
	    i->opcode != X64_OP_LD && i->opcode != X64_OP_LDX && i->opcode != X64_OP_EXT && i->opcode != X64_OP_SETCC && i->opcode != X64_OP_FSETCC)
		f.used |= x64_effect_register_mask(i->registers[0]);
	if ((i->opcode == X64_OP_LD || i->opcode == X64_OP_ST || i->opcode == X64_OP_LDX || i->opcode == X64_OP_STX || i->opcode == X64_OP_TESTM || i->opcode == X64_OP_CMPM || i->opcode == X64_OP_LEA ||
	     i->opcode == X64_OP_XCHG || i->opcode == X64_OP_XADD || i->opcode == X64_OP_CMPXCHG || i->opcode == X64_OP_X87) &&
	    i->registers[1] == RBP && i->offset < 0 && (!e || e->walk_state->frame_alignment > 16))
		f.used |= X64_REGISTER_BIT(R12); /* printer's realigned local-frame base */
	if (i->opcode == X64_OP_RESUME_FRAME) {
		if (e && e->walk_state && e->walk_state->frame_alignment <= 16)
			f = (record_effects){ .flags = RECORD_EFFECT_BARRIER }; /* printer emits nothing */
		else if (!e || !e->walk_state)
			f.used |= X64_REGISTER_BIT(R12); /* unknown alignment may preserve its old value */
	} else if (i->opcode == X64_OP_PCMPSTR) {
		f.defines |= X64_REGISTER_BIT((i->condition & 1) ? RCX : XMM0);
		if (!(i->condition & 2)) f.used |= X64_REGISTER_BIT(RAX) | X64_REGISTER_BIT(RDX);
	} else if (i->opcode == X64_OP_TLS) {
		if (i->condition == 2) {
			f.defines = X64_CALL_DEFINED_REGISTERS;
			f.used = X64_CALL_USED_REGISTERS & ~X64_REGISTER_BIT(RDI); /* descriptor overwrites RDI */
			f.flags = X_CALL_FLAGS;
		} else if (i->condition == 1) {
			f.flags |= X_W; /* initial-exec uses addq, local-exec LEA does not */
		}
		if (e && CODEGEN_IS_WIN64(e->compile_context))
			f.defines |= X64_REGISTER_BIT(R11); /* Windows: _tls_index through r11 */
	} else if (i->opcode == X64_OP_REP) {
		if (i->condition) f.used |= X64_REGISTER_BIT(RAX);
		else f.defines |= X64_REGISTER_BIT(RSI), f.used |= X64_REGISTER_BIT(RSI);
	} else if (i->opcode == X64_OP_X87) {
		switch (i->condition) {
		case X64_X87_LDT: case X64_X87_LD: case X64_X87_ILD: case X64_X87_LDCW: f.flags |= RECORD_EFFECT_LOAD; break;
		case X64_X87_STPT: case X64_X87_STP: case X64_X87_ISTP: case X64_X87_STCW: f.flags |= RECORD_EFFECT_STORE; break;
		case X64_X87_UCOMIP: f.flags |= X_W; break;
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
	static const uint8_t flow[X64_OP_COUNT] = {
		[X64_OP_LABEL] = RECORD_FLOW_ENTRY,
		[X64_OP_MOV] = RECORD_FLOW_NEXT,
		[X64_OP_CMOV] = RECORD_FLOW_NEXT,
		[X64_OP_TESTM] = RECORD_FLOW_NEXT,
		[X64_OP_MOVI] = RECORD_FLOW_NEXT,
		[X64_OP_LD] = RECORD_FLOW_NEXT,
		[X64_OP_LDX] = RECORD_FLOW_NEXT,
		[X64_OP_STX] = RECORD_FLOW_NEXT,
		[X64_OP_ST] = RECORD_FLOW_NEXT,
		[X64_OP_LEA] = RECORD_FLOW_NEXT,
		[X64_OP_LEASCALE] = RECORD_FLOW_NEXT,
		[X64_OP_ADD] = RECORD_FLOW_NEXT,
		[X64_OP_SUB] = RECORD_FLOW_NEXT,
		[X64_OP_AND] = RECORD_FLOW_NEXT,
		[X64_OP_OR] = RECORD_FLOW_NEXT,
		[X64_OP_XOR] = RECORD_FLOW_NEXT,
		[X64_OP_IMUL] = RECORD_FLOW_NEXT,
		[X64_OP_ADC] = RECORD_FLOW_NEXT,
		[X64_OP_SBB] = RECORD_FLOW_NEXT,
		[X64_OP_ADDI] = RECORD_FLOW_NEXT,
		[X64_OP_SUBI] = RECORD_FLOW_NEXT,
		[X64_OP_ANDI] = RECORD_FLOW_NEXT,
		[X64_OP_ORI] = RECORD_FLOW_NEXT,
		[X64_OP_XORI] = RECORD_FLOW_NEXT,
		[X64_OP_IMULI] = RECORD_FLOW_NEXT,
		[X64_OP_SHLI] = RECORD_FLOW_NEXT,
		[X64_OP_SHRI] = RECORD_FLOW_NEXT,
		[X64_OP_SARI] = RECORD_FLOW_NEXT,
		[X64_OP_SHL] = RECORD_FLOW_NEXT,
		[X64_OP_SHR] = RECORD_FLOW_NEXT,
		[X64_OP_SAR] = RECORD_FLOW_NEXT,
		[X64_OP_NEG] = RECORD_FLOW_NEXT,
		[X64_OP_NOT] = RECORD_FLOW_NEXT,
		[X64_OP_CMP] = RECORD_FLOW_NEXT,
		[X64_OP_CMPM] = RECORD_FLOW_NEXT,
		[X64_OP_CMPI] = RECORD_FLOW_NEXT,
		[X64_OP_TEST] = RECORD_FLOW_NEXT,
		[X64_OP_SETCC] = RECORD_FLOW_NEXT,
		[X64_OP_JMP] = RECORD_FLOW_STOP,
		[X64_OP_JCC] = RECORD_FLOW_NEXT,
		[X64_OP_CALL] = RECORD_FLOW_NEXT,
		[X64_OP_CALLR] = RECORD_FLOW_NEXT,
		[X64_OP_TAIL] = RECORD_FLOW_STOP,
		[X64_OP_PUSH] = RECORD_FLOW_NEXT,
		[X64_OP_POP] = RECORD_FLOW_NEXT,
		[X64_OP_ADJSP] = RECORD_FLOW_NEXT,
		[X64_OP_CQO] = RECORD_FLOW_NEXT,
		[X64_OP_IDIV] = RECORD_FLOW_NEXT,
		[X64_OP_DIV] = RECORD_FLOW_NEXT,
		[X64_OP_MULW] = RECORD_FLOW_NEXT,
		[X64_OP_SMULW] = RECORD_FLOW_NEXT,
		[X64_OP_UD2] = RECORD_FLOW_NEXT,
		[X64_OP_TLS] = RECORD_FLOW_NEXT,
		[X64_OP_GOT] = RECORD_FLOW_NEXT,
		[X64_OP_XCHG] = RECORD_FLOW_NEXT,
		[X64_OP_XADD] = RECORD_FLOW_NEXT,
		[X64_OP_CMPXCHG] = RECORD_FLOW_NEXT,
		[X64_OP_MFENCE] = RECORD_FLOW_NEXT,
		[X64_OP_BLENDVB] = RECORD_FLOW_NEXT,
		[X64_OP_PCMPSTR] = RECORD_FLOW_NEXT,
		[X64_OP_RAW] = RECORD_FLOW_ENTRY,
		[X64_OP_BARRIER] = RECORD_FLOW_ENTRY,
		[X64_OP_LEAL] = RECORD_FLOW_NEXT,
		[X64_OP_JMPR] = RECORD_FLOW_ENTRY,
		[X64_OP_JTE] = RECORD_FLOW_ENTRY,
		[X64_OP_NAMED] = RECORD_FLOW_ENTRY,
		[X64_OP_BSF] = RECORD_FLOW_NEXT,
		[X64_OP_POPCNT] = RECORD_FLOW_NEXT,
		[X64_OP_BSR] = RECORD_FLOW_NEXT,
		[X64_OP_BSWAP] = RECORD_FLOW_NEXT,
		[X64_OP_EXT] = RECORD_FLOW_NEXT,
		[X64_OP_REP] = RECORD_FLOW_NEXT,
		[X64_OP_FADD] = RECORD_FLOW_NEXT,
		[X64_OP_FSUB] = RECORD_FLOW_NEXT,
		[X64_OP_FMUL] = RECORD_FLOW_NEXT,
		[X64_OP_FDIV] = RECORD_FLOW_NEXT,
		[X64_OP_FXOR] = RECORD_FLOW_NEXT,
		[X64_OP_FSQRT] = RECORD_FLOW_NEXT,
		[X64_OP_FCMP] = RECORD_FLOW_NEXT,
		[X64_OP_FSETCC] = RECORD_FLOW_NEXT,
		[X64_OP_CVTI2F] = RECORD_FLOW_NEXT,
		[X64_OP_CVTF2I] = RECORD_FLOW_NEXT,
		[X64_OP_CVTF2F] = RECORD_FLOW_NEXT,
		[X64_OP_X87] = RECORD_FLOW_NEXT,
		[X64_OP_RESUME_FRAME] = RECORD_FLOW_NEXT,
		[X64_OP_PROBE] = RECORD_FLOW_NEXT,
		[X64_OP_FRAMEADDR] = RECORD_FLOW_NEXT,
	};
	return flow;
}
