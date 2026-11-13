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
#define X_D OD_DEF0
#define X_U OD_USE1
#define X_V OD_USE2
#define X_P OD_PURE
#define X_W OD_WFLAGS
#define X_R OD_RFLAGS
#define X_MEM (OD_LOAD | OD_STORE | OD_TRAP)
#define X_CALL_FLAGS (X_MEM | OD_CALL | X_W | OD_FP | OD_BARRIER)
#define X_DESC(regs_, flags_) { .regs = (regs_), .fl = (flags_) }

static const opdesc x64_desc[X_NOPS] = {
	[X_CMOV] = X_DESC(X_D | X_U | X_V, X_P | X_R),
	[X_CMPM] = X_DESC(X_U | X_V | OD_USE3, OD_LOAD | OD_TRAP | X_W),
	[X_TESTM] = X_DESC(X_U | OD_USE3, OD_LOAD | OD_TRAP | X_W),
	[X_LDX] = X_DESC(X_D | X_U | OD_USE3, OD_LOAD | OD_TRAP),
	[X_STX] = X_DESC(X_U | X_V | OD_USE3, OD_STORE | OD_TRAP),
	[X_LABEL] = X_DESC(0, OD_LABEL),
	[X_MOV] = X_DESC(X_D | X_U, X_P),
	[X_MOVI] = X_DESC(X_D, X_P),
	[X_LD] = X_DESC(X_D | X_U, OD_LOAD | OD_TRAP),
	[X_ST] = X_DESC(X_U | X_V, OD_STORE | OD_TRAP),
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
	[X_JMP] = { .br = RB_JUMP },
	[X_JCC] = { .br = RB_COND, .fl = X_R },
	[X_CALL] = { .fl = X_CALL_FLAGS, .idef = X_CALL_DEF, .iuse = X_CALL_USE },
	[X_CALLR] = { .regs = X_U, .fl = X_CALL_FLAGS, .idef = X_CALL_DEF, .iuse = X_CALL_USE },
	[X_TAIL] = { .br = RB_RET, .fl = OD_BARRIER, .iuse = X_GP_ARGS },
	[X_PUSH] = { .regs = X_U, .fl = OD_STORE | OD_TRAP, .idef = X_BIT(RSP), .iuse = X_BIT(RSP) },
	[X_POP] = { .regs = X_D, .fl = OD_LOAD | OD_TRAP, .idef = X_BIT(RSP), .iuse = X_BIT(RSP) },
	[X_ADJSP] = { .fl = X_P | X_W, .idef = X_BIT(RSP), .iuse = X_BIT(RSP) },
	[X_CQO] = { .fl = X_P, .idef = X_BIT(RDX), .iuse = X_BIT(RAX) },
	[X_IDIV] = { .regs = X_U, .fl = OD_TRAP | X_W, .idef = X_BIT(RAX) | X_BIT(RDX), .iuse = X_BIT(RAX) | X_BIT(RDX) },
	[X_DIV] = { .regs = X_U, .fl = OD_TRAP | X_W, .idef = X_BIT(RAX) | X_BIT(RDX), .iuse = X_BIT(RAX) | X_BIT(RDX) },
	[X_MULW] = { .regs = X_U, .fl = X_P | X_W, .idef = X_BIT(RAX) | X_BIT(RDX), .iuse = X_BIT(RAX) },
	[X_SMULW] = { .regs = X_U, .fl = X_P | X_W, .idef = X_BIT(RAX) | X_BIT(RDX), .iuse = X_BIT(RAX) },
	[X_UD2] = X_DESC(0, OD_TRAP | OD_BARRIER),
	[X_TLS] = X_DESC(X_D, OD_LOAD | OD_TRAP), /* dynamic model below */
	[X_GOT] = X_DESC(X_D, OD_LOAD | OD_TRAP),
	[X_XCHG] = X_DESC(X_D | OD_USE0 | X_U, X_MEM | OD_BARRIER),
	[X_XADD] = X_DESC(X_D | OD_USE0 | X_U, X_MEM | OD_BARRIER | X_W),
	[X_CMPXCHG] = { .regs = X_U | X_V, .fl = X_MEM | OD_BARRIER | X_W, .idef = X_BIT(RAX), .iuse = X_BIT(RAX) },
	[X_MFENCE] = X_DESC(0, OD_BARRIER),
	[X_BLENDVB] = { .fl = X_P, .idef = X_BIT(XMM0 + 1),
		.iuse = X_BIT(XMM0) | X_BIT(XMM0 + 1) | X_BIT(XMM0 + 2) },
	[X_PCMPSTR] = { .fl = X_P | X_W, .iuse = X_BIT(XMM0) | X_BIT(XMM0 + 1) },
	[X_RAW] = { .br = RB_INDIRECT, .fl = X_MEM | OD_BARRIER | X_R | X_W | OD_FP, .idef = X_ALL_REGS, .iuse = X_ALL_REGS },
	[X_BARRIER] = X_DESC(0, OD_LOAD | OD_STORE | OD_BARRIER | X_R | X_W),
	[X_LEAL] = X_DESC(X_D, X_P),
	[X_JMPR] = { .regs = X_U, .br = RB_INDIRECT },
	[X_JTE] = X_DESC(0, OD_BARRIER),
	[X_NAMED] = X_DESC(0, OD_LABEL | OD_BARRIER),
	/* Undefined destination for zero input is conservatively an old value. */
	[X_BSF] = X_DESC(X_D | OD_USE0 | X_U, X_P | X_W),
	[X_BSR] = X_DESC(X_D | OD_USE0 | X_U, X_P | X_W),
	[X_BSWAP] = X_DESC(X_D | OD_USE0 | X_U, X_P),
	[X_EXT] = X_DESC(X_D | X_U, X_P),
	/* Direction flag is an input even though REP leaves arithmetic flags alone. */
	[X_REP] = { .fl = X_MEM | OD_BARRIER | X_R, .idef = X_BIT(RCX) | X_BIT(RDI), .iuse = X_BIT(RCX) | X_BIT(RDI) },
	[X_FADD] = X_DESC(X_D | OD_USE0 | X_U | X_V, OD_FP | OD_TRAP),
	[X_FSUB] = X_DESC(X_D | OD_USE0 | X_U | X_V, OD_FP | OD_TRAP),
	[X_FMUL] = X_DESC(X_D | OD_USE0 | X_U | X_V, OD_FP | OD_TRAP),
	[X_FDIV] = X_DESC(X_D | OD_USE0 | X_U | X_V, OD_FP | OD_TRAP),
	[X_FXOR] = X_DESC(X_D | X_U | X_V, X_P),
	[X_FSQRT] = X_DESC(X_D | OD_USE0 | X_U, OD_FP | OD_TRAP),
	[X_FCMP] = X_DESC(X_U | X_V, OD_FP | OD_TRAP | X_W),
	[X_FSETCC] = { .regs = X_D, .fl = X_P | X_R | X_W, .idef = X_BIT(R11), .iuse = X_BIT(R11) },
	[X_CVTI2F] = X_DESC(X_D | OD_USE0 | X_U, OD_FP | OD_TRAP),
	[X_CVTF2I] = X_DESC(X_D | X_U, OD_FP | OD_TRAP),
	[X_CVTF2F] = X_DESC(X_D | OD_USE0 | X_U, OD_FP | OD_TRAP),
	/* x87 stack and control/status state are not physical GPR/XMM bits. */
	[X_X87] = X_DESC(X_U, OD_FP | OD_TRAP | OD_BARRIER),
	[X_RESUME_FRAME] = { .fl = OD_BARRIER | X_W, .idef = X_BIT(R12), .iuse = X_BIT(RBP) },
	[X_PROBE] = { .fl = OD_LOAD | OD_STORE | OD_TRAP | OD_BARRIER | X_W, .iuse = X_BIT(RAX) | X_BIT(RSP) },
	[X_FRAMEADDR] = { .regs = X_D, .fl = X_P, .iuse = X_BIT(RBP) },
};

static rmask x64_effect_reg(unsigned r)
{
	if(r==XFRAME)return X_BIT(RSP);
	return r <= XMM15 ? X_BIT(r) : 0; /* RIP is not a mutable register. */
}

VPRIV receffects x64_effects(const ecx *e, const rec *i)
{
	if (i->op >= X_NOPS)
		return (receffects){ .def = X_ALL_REGS, .use = X_ALL_REGS,
			.fl = X_MEM | OD_FP | OD_BARRIER | X_R | X_W, .br = RB_INDIRECT };
	const opdesc *d = &x64_desc[i->op];
	/* A newly added opcode without metadata must be opaque, never a no-op. */
	if (!d->regs && !d->br && !d->fl && !d->idef && !d->iuse)
		return (receffects){ .def = X_ALL_REGS, .use = X_ALL_REGS,
			.fl = X_MEM | OD_FP | OD_BARRIER | X_R | X_W, .br = RB_INDIRECT };
	receffects f = { .def = d->idef, .use = d->iuse, .fl = d->fl, .br = d->br };
	if(i->op==X_BSWAP && i->sz==2)f.fl|=OD_WFLAGS; /* byte rotate */
	if (d->regs & OD_DEF0) f.def |= x64_effect_reg(i->r[0]);
	if (d->regs & OD_USE0) f.use |= x64_effect_reg(i->r[0]);
	if (d->regs & OD_USE1) f.use |= x64_effect_reg(i->r[1]);
	if (d->regs & OD_USE2) f.use |= x64_effect_reg(i->r[2]);
	if (d->regs & OD_USE3) f.use |= x64_effect_reg(i->r[3]);

	/* Only MOV and narrow ALU/atomic operations can leave GPR high bits.
	 * SETCC and EXT contain a widening move; LD also always widens GPRs.
	 */
	if ((d->regs & OD_DEF0) && i->sz < 4 && i->r[0] < XMM0 &&
	    i->op != X_LD && i->op != X_LDX && i->op != X_EXT && i->op != X_SETCC && i->op != X_FSETCC)
		f.use |= x64_effect_reg(i->r[0]);
	if ((i->op == X_LD || i->op == X_ST || i->op == X_LDX || i->op == X_STX || i->op == X_TESTM || i->op == X_CMPM || i->op == X_LEA ||
	     i->op == X_XCHG || i->op == X_XADD || i->op == X_CMPXCHG || i->op == X_X87) &&
	    i->r[1] == RBP && i->off < 0 && (!e || e->wk->falign > 16))
		f.use |= X_BIT(R12); /* printer's realigned local-frame base */
	if (i->op == X_RESUME_FRAME) {
		if (e && e->wk && e->wk->falign <= 16)
			f = (receffects){ .fl = OD_BARRIER }; /* printer emits nothing */
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
		if (e && WIN64(e->c))
			f.def |= X_BIT(R11); /* Windows: _tls_index through r11 */
	} else if (i->op == X_REP) {
		if (i->cond) f.use |= X_BIT(RAX);
		else f.def |= X_BIT(RSI), f.use |= X_BIT(RSI);
	} else if (i->op == X_X87) {
		switch (i->cond) {
		case XF_LDT: case XF_LD: case XF_ILD: case XF_LDCW: f.fl |= OD_LOAD; break;
		case XF_STPT: case XF_STP: case XF_ISTP: case XF_STCW: f.fl |= OD_STORE; break;
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
		[X_LABEL] = EM_FLOW_ENTRY,
		[X_MOV] = EM_FLOW_NEXT,
		[X_CMOV] = EM_FLOW_NEXT,
		[X_TESTM] = EM_FLOW_NEXT,
		[X_MOVI] = EM_FLOW_NEXT,
		[X_LD] = EM_FLOW_NEXT,
		[X_LDX] = EM_FLOW_NEXT,
		[X_STX] = EM_FLOW_NEXT,
		[X_ST] = EM_FLOW_NEXT,
		[X_LEA] = EM_FLOW_NEXT,
		[X_LEASCALE] = EM_FLOW_NEXT,
		[X_ADD] = EM_FLOW_NEXT,
		[X_SUB] = EM_FLOW_NEXT,
		[X_AND] = EM_FLOW_NEXT,
		[X_OR] = EM_FLOW_NEXT,
		[X_XOR] = EM_FLOW_NEXT,
		[X_IMUL] = EM_FLOW_NEXT,
		[X_ADC] = EM_FLOW_NEXT,
		[X_SBB] = EM_FLOW_NEXT,
		[X_ADDI] = EM_FLOW_NEXT,
		[X_SUBI] = EM_FLOW_NEXT,
		[X_ANDI] = EM_FLOW_NEXT,
		[X_ORI] = EM_FLOW_NEXT,
		[X_XORI] = EM_FLOW_NEXT,
		[X_IMULI] = EM_FLOW_NEXT,
		[X_SHLI] = EM_FLOW_NEXT,
		[X_SHRI] = EM_FLOW_NEXT,
		[X_SARI] = EM_FLOW_NEXT,
		[X_SHL] = EM_FLOW_NEXT,
		[X_SHR] = EM_FLOW_NEXT,
		[X_SAR] = EM_FLOW_NEXT,
		[X_NEG] = EM_FLOW_NEXT,
		[X_NOT] = EM_FLOW_NEXT,
		[X_CMP] = EM_FLOW_NEXT,
		[X_CMPM] = EM_FLOW_NEXT,
		[X_CMPI] = EM_FLOW_NEXT,
		[X_TEST] = EM_FLOW_NEXT,
		[X_SETCC] = EM_FLOW_NEXT,
		[X_JMP] = EM_FLOW_STOP,
		[X_JCC] = EM_FLOW_NEXT,
		[X_CALL] = EM_FLOW_NEXT,
		[X_CALLR] = EM_FLOW_NEXT,
		[X_TAIL] = EM_FLOW_STOP,
		[X_PUSH] = EM_FLOW_NEXT,
		[X_POP] = EM_FLOW_NEXT,
		[X_ADJSP] = EM_FLOW_NEXT,
		[X_CQO] = EM_FLOW_NEXT,
		[X_IDIV] = EM_FLOW_NEXT,
		[X_DIV] = EM_FLOW_NEXT,
		[X_MULW] = EM_FLOW_NEXT,
		[X_SMULW] = EM_FLOW_NEXT,
		[X_UD2] = EM_FLOW_NEXT,
		[X_TLS] = EM_FLOW_NEXT,
		[X_GOT] = EM_FLOW_NEXT,
		[X_XCHG] = EM_FLOW_NEXT,
		[X_XADD] = EM_FLOW_NEXT,
		[X_CMPXCHG] = EM_FLOW_NEXT,
		[X_MFENCE] = EM_FLOW_NEXT,
		[X_BLENDVB] = EM_FLOW_NEXT,
		[X_PCMPSTR] = EM_FLOW_NEXT,
		[X_RAW] = EM_FLOW_ENTRY,
		[X_BARRIER] = EM_FLOW_ENTRY,
		[X_LEAL] = EM_FLOW_NEXT,
		[X_JMPR] = EM_FLOW_ENTRY,
		[X_JTE] = EM_FLOW_ENTRY,
		[X_NAMED] = EM_FLOW_ENTRY,
		[X_BSF] = EM_FLOW_NEXT,
		[X_BSR] = EM_FLOW_NEXT,
		[X_BSWAP] = EM_FLOW_NEXT,
		[X_EXT] = EM_FLOW_NEXT,
		[X_REP] = EM_FLOW_NEXT,
		[X_FADD] = EM_FLOW_NEXT,
		[X_FSUB] = EM_FLOW_NEXT,
		[X_FMUL] = EM_FLOW_NEXT,
		[X_FDIV] = EM_FLOW_NEXT,
		[X_FXOR] = EM_FLOW_NEXT,
		[X_FSQRT] = EM_FLOW_NEXT,
		[X_FCMP] = EM_FLOW_NEXT,
		[X_FSETCC] = EM_FLOW_NEXT,
		[X_CVTI2F] = EM_FLOW_NEXT,
		[X_CVTF2I] = EM_FLOW_NEXT,
		[X_CVTF2F] = EM_FLOW_NEXT,
		[X_X87] = EM_FLOW_NEXT,
		[X_RESUME_FRAME] = EM_FLOW_NEXT,
		[X_PROBE] = EM_FLOW_NEXT,
		[X_FRAMEADDR] = EM_FLOW_NEXT,
	};
	return flow;
}
