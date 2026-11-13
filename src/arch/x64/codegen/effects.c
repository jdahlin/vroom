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
#define X_DESC(regs_, flags_) { .regs = (regs_), .fl = (flags_) }

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
	[X_SHL] = { .regs = X_D | X_U | X_V, .fl = X_P | X_R | X_W, .iuse = X_BIT(RCX) },
	[X_SHR] = { .regs = X_D | X_U | X_V, .fl = X_P | X_R | X_W, .iuse = X_BIT(RCX) },
	[X_SAR] = { .regs = X_D | X_U | X_V, .fl = X_P | X_R | X_W, .iuse = X_BIT(RCX) },
	[X_NEG] = X_DESC(X_D | X_U, X_P | X_W),
	[X_NOT] = X_DESC(X_D | X_U, X_P),
	[X_CMP] = X_DESC(X_U | X_V, X_P | X_W),
	[X_CMPI] = X_DESC(X_U, X_P | X_W),
	[X_TEST] = X_DESC(X_U, X_P | X_W),
	[X_SETCC] = X_DESC(X_D, X_P | X_R),
	[X_JMP] = { .br = RECORD_BRANCH_JUMP },
	[X_JCC] = { .br = RECORD_BRANCH_CONDITIONAL, .fl = X_R },
	[X_CALL] = { .fl = X_CALL_FLAGS, .idef = X_CALL_DEF, .iuse = X_CALL_USE },
	[X_CALLR] = { .regs = X_U, .fl = X_CALL_FLAGS, .idef = X_CALL_DEF, .iuse = X_CALL_USE },
	[X_TAIL] = { .br = RECORD_BRANCH_RETURN, .fl = RECORD_EFFECT_BARRIER, .iuse = X_GP_ARGS },
	[X_PUSH] = { .regs = X_U, .fl = RECORD_EFFECT_STORE | RECORD_EFFECT_TRAP, .idef = X_BIT(RSP), .iuse = X_BIT(RSP) },
	[X_POP] = { .regs = X_D, .fl = RECORD_EFFECT_LOAD | RECORD_EFFECT_TRAP, .idef = X_BIT(RSP), .iuse = X_BIT(RSP) },
	[X_ADJSP] = { .fl = X_P | X_W, .idef = X_BIT(RSP), .iuse = X_BIT(RSP) },
	[X_CQO] = { .fl = X_P, .idef = X_BIT(RDX), .iuse = X_BIT(RAX) },
	[X_IDIV] = { .regs = X_U, .fl = RECORD_EFFECT_TRAP | X_W, .idef = X_BIT(RAX) | X_BIT(RDX), .iuse = X_BIT(RAX) | X_BIT(RDX) },
	[X_DIV] = { .regs = X_U, .fl = RECORD_EFFECT_TRAP | X_W, .idef = X_BIT(RAX) | X_BIT(RDX), .iuse = X_BIT(RAX) | X_BIT(RDX) },
	[X_MULW] = { .regs = X_U, .fl = X_P | X_W, .idef = X_BIT(RAX) | X_BIT(RDX), .iuse = X_BIT(RAX) },
	[X_SMULW] = { .regs = X_U, .fl = X_P | X_W, .idef = X_BIT(RAX) | X_BIT(RDX), .iuse = X_BIT(RAX) },
	[X_UD2] = X_DESC(0, RECORD_EFFECT_TRAP | RECORD_EFFECT_BARRIER),
	[X_TLS] = X_DESC(X_D, RECORD_EFFECT_LOAD | RECORD_EFFECT_TRAP), /* dynamic model below */
	[X_GOT] = X_DESC(X_D, RECORD_EFFECT_LOAD | RECORD_EFFECT_TRAP),
	[X_XCHG] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U, X_MEM | RECORD_EFFECT_BARRIER),
	[X_XADD] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U, X_MEM | RECORD_EFFECT_BARRIER | X_W),
	[X_CMPXCHG] = { .regs = X_U | X_V, .fl = X_MEM | RECORD_EFFECT_BARRIER | X_W, .idef = X_BIT(RAX), .iuse = X_BIT(RAX) },
	[X_MFENCE] = X_DESC(0, RECORD_EFFECT_BARRIER),
	[X_BLENDVB] = { .fl = X_P, .idef = X_BIT(XMM0 + 1),
		.iuse = X_BIT(XMM0) | X_BIT(XMM0 + 1) | X_BIT(XMM0 + 2) },
	[X_PCMPSTR] = { .fl = X_P | X_W, .iuse = X_BIT(XMM0) | X_BIT(XMM0 + 1) },
	[X_RAW] = { .br = RECORD_BRANCH_INDIRECT, .fl = X_MEM | RECORD_EFFECT_BARRIER | X_R | X_W | RECORD_EFFECT_FLOAT, .idef = X_ALL_REGS, .iuse = X_ALL_REGS },
	[X_BARRIER] = X_DESC(0, RECORD_EFFECT_LOAD | RECORD_EFFECT_STORE | RECORD_EFFECT_BARRIER | X_R | X_W),
	[X_LEAL] = X_DESC(X_D, X_P),
	[X_JMPR] = { .regs = X_U, .br = RECORD_BRANCH_INDIRECT },
	[X_JTE] = X_DESC(0, RECORD_EFFECT_BARRIER),
	[X_NAMED] = X_DESC(0, RECORD_EFFECT_LABEL | RECORD_EFFECT_BARRIER),
	/* Undefined destination for zero input is conservatively an old value. */
	[X_BSF] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U, X_P | X_W),
	[X_BSR] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U, X_P | X_W),
	[X_BSWAP] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U, X_P),
	[X_EXT] = X_DESC(X_D | X_U, X_P),
	/* Direction flag is an input even though REP leaves arithmetic flags alone. */
	[X_REP] = { .fl = X_MEM | RECORD_EFFECT_BARRIER | X_R, .idef = X_BIT(RCX) | X_BIT(RDI), .iuse = X_BIT(RCX) | X_BIT(RDI) },
	[X_FADD] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U | X_V, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP),
	[X_FSUB] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U | X_V, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP),
	[X_FMUL] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U | X_V, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP),
	[X_FDIV] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U | X_V, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP),
	[X_FXOR] = X_DESC(X_D | X_U | X_V, X_P),
	[X_FSQRT] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP),
	[X_FCMP] = X_DESC(X_U | X_V, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP | X_W),
	[X_FSETCC] = { .regs = X_D, .fl = X_P | X_R | X_W, .idef = X_BIT(R11), .iuse = X_BIT(R11) },
	[X_CVTI2F] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP),
	[X_CVTF2I] = X_DESC(X_D | X_U, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP),
	[X_CVTF2F] = X_DESC(X_D | RECORD_OPERAND_USES_0 | X_U, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP),
	/* x87 stack and control/status state are not physical GPR/XMM bits. */
	[X_X87] = X_DESC(X_U, RECORD_EFFECT_FLOAT | RECORD_EFFECT_TRAP | RECORD_EFFECT_BARRIER),
	[X_RESUME_FRAME] = { .fl = RECORD_EFFECT_BARRIER | X_W, .idef = X_BIT(R12), .iuse = X_BIT(RBP) },
};

static record_register_mask x64_effect_reg(unsigned r)
{
	if(r==XFRAME)return X_BIT(RSP);
	return r <= XMM15 ? X_BIT(r) : 0; /* RIP is not a mutable register. */
}

VPRIV record_effects x64_effects(const codegen_context *e, const record *i)
{
	if (i->op >= X_NOPS)
		return (record_effects){ .def = X_ALL_REGS, .use = X_ALL_REGS,
			.fl = X_MEM | RECORD_EFFECT_FLOAT | RECORD_EFFECT_BARRIER | X_R | X_W, .br = RECORD_BRANCH_INDIRECT };
	const record_op_descriptor *d = &x64_desc[i->op];
	/* A newly added opcode without metadata must be opaque, never a no-op. */
	if (!d->regs && !d->br && !d->fl && !d->idef && !d->iuse)
		return (record_effects){ .def = X_ALL_REGS, .use = X_ALL_REGS,
			.fl = X_MEM | RECORD_EFFECT_FLOAT | RECORD_EFFECT_BARRIER | X_R | X_W, .br = RECORD_BRANCH_INDIRECT };
	record_effects f = { .def = d->idef, .use = d->iuse, .fl = d->fl, .br = d->br };
	if(i->op==X_BSWAP && i->sz==2)f.fl|=RECORD_EFFECT_WRITES_FLAGS; /* byte rotate */
	if (d->regs & RECORD_OPERAND_DEFINES_0) f.def |= x64_effect_reg(i->r[0]);
	if (d->regs & RECORD_OPERAND_USES_0) f.use |= x64_effect_reg(i->r[0]);
	if (d->regs & RECORD_OPERAND_USES_1) f.use |= x64_effect_reg(i->r[1]);
	if (d->regs & RECORD_OPERAND_USES_2) f.use |= x64_effect_reg(i->r[2]);
	if (d->regs & RECORD_OPERAND_USES_3) f.use |= x64_effect_reg(i->r[3]);

	/* Only MOV and narrow ALU/atomic operations can leave GPR high bits.
	 * SETCC and EXT contain a widening move; LD also always widens GPRs.
	 */
	if ((d->regs & RECORD_OPERAND_DEFINES_0) && i->sz < 4 && i->r[0] < XMM0 &&
	    i->op != X_LD && i->op != X_LDX && i->op != X_EXT && i->op != X_SETCC && i->op != X_FSETCC)
		f.use |= x64_effect_reg(i->r[0]);
	if ((i->op == X_LD || i->op == X_ST || i->op == X_LDX || i->op == X_STX || i->op == X_TESTM || i->op == X_CMPM || i->op == X_LEA ||
	     i->op == X_XCHG || i->op == X_XADD || i->op == X_CMPXCHG || i->op == X_X87) &&
	    i->r[1] == RBP && i->off < 0 && (!e || e->wk->falign > 16))
		f.use |= X_BIT(R12); /* printer's realigned local-frame base */
	if (i->op == X_RESUME_FRAME) {
		if (e && e->wk && e->wk->falign <= 16)
			f = (record_effects){ .fl = RECORD_EFFECT_BARRIER }; /* printer emits nothing */
		else if (!e || !e->wk)
			f.use |= X_BIT(R12); /* unknown alignment may preserve its old value */
	} else if (i->op == X_PCMPSTR) {
		f.def |= X_BIT((i->cond & 1) ? RCX : XMM0);
		if (!(i->cond & 2)) f.use |= X_BIT(RAX) | X_BIT(RDX);
	} else if (i->op == X_TLS) {
		if (i->cond == 2) {
			f.def = X_CALL_DEF;
			f.use = X_CALL_USE & ~X_BIT(RDI); /* descriptor overwrites RDI */
			f.fl = X_CALL_FLAGS;
		} else if (i->cond) {
			f.fl |= X_W; /* initial-exec uses addq, local-exec LEA does not */
		}
	} else if (i->op == X_REP) {
		if (i->cond) f.use |= X_BIT(RAX);
		else f.def |= X_BIT(RSI), f.use |= X_BIT(RSI);
	} else if (i->op == X_X87) {
		switch (i->cond) {
		case XF_LDT: case XF_LD: case XF_ILD: case XF_LDCW: f.fl |= RECORD_EFFECT_LOAD; break;
		case XF_STPT: case XF_STP: case XF_ISTP: case XF_STCW: f.fl |= RECORD_EFFECT_STORE; break;
		case XF_UCOMIP: f.fl |= X_W; break;
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
	};
	return flow;
}
