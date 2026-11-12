/*
 * The opcode table: one row per opcode, read by the printer, the parser and
 * the verifier so that the three cannot disagree (SPEC-vir.md 7.3, 8.1).
 */
#include "virimpl.h"

#define R(op, nm, fm, tm, a, b, c, fl, ef) [op] = { nm, op, fm, tm, { OK_##a, OK_##b, OK_##c }, fl, ef }
#define ARITH_FL (VIF_NSW | VIF_NUW)

const vir_opinfo vir_optab[256] = {
	R(VO_NOP, "nop", FM_NONE, TM_V, NONE, NONE, NONE, 0, EF_P),
	R(VO_ICONST, "iconst", FM_CONST, TM_I | TM_PTR, NONE, IMM, IMM, 0, EF_P),
	R(VO_FCONST, "fconst", FM_CONST, TM_F, NONE, IMM, IMM, 0, EF_P),
	R(VO_SYMADDR, "symaddr", FM_CONST, TM_PTR, SYMREF, IMM, IMM, 0, EF_P),
	R(VO_SLOTADDR, "slotaddr", FM_CONST, TM_PTR, IMM, IMM, IMM, 0, EF_P),
	R(VO_LABELADDR, "labeladdr", FM_CONST, TM_PTR, BLK, NONE, NONE, 0, EF_P),
	R(VO_BPARAM, "bparam", FM_BPARAM, TM_ANY, NONE, NONE, NONE, 0, EF_P),
	R(VO_PARAM, "param", FM_PARAM, TM_I64 | TM_F | TM_PTR, IMM, IMM, NONE, 0, EF_P),
	R(VO_PROJ, "proj", FM_STD, TM_ANY, MULTI, IMM, NONE, 0, EF_P),
	R(VO_ADD, "add", FM_STD, TM_I, T, T, NONE, ARITH_FL, EF_P | EF_UF),
	R(VO_SUB, "sub", FM_STD, TM_I, T, T, NONE, ARITH_FL, EF_P | EF_UF),
	R(VO_MUL, "mul", FM_STD, TM_I, T, T, NONE, ARITH_FL, EF_P | EF_UF),
	R(VO_SDIV, "sdiv", FM_STD, TM_I, T, T, NONE, VIF_EXACT, EF_U),
	R(VO_UDIV, "udiv", FM_STD, TM_I, T, T, NONE, VIF_EXACT, EF_U),
	R(VO_SREM, "srem", FM_STD, TM_I, T, T, NONE, 0, EF_U),
	R(VO_UREM, "urem", FM_STD, TM_I, T, T, NONE, 0, EF_U),
	R(VO_AND, "and", FM_STD, TM_I, T, T, NONE, 0, EF_P),
	R(VO_OR, "or", FM_STD, TM_I, T, T, NONE, 0, EF_P),
	R(VO_XOR, "xor", FM_STD, TM_I, T, T, NONE, 0, EF_P),
	R(VO_SHL, "shl", FM_STD, TM_I, T, T, NONE, ARITH_FL, EF_P | EF_UF),
	R(VO_LSHR, "lshr", FM_STD, TM_I, T, T, NONE, VIF_EXACT, EF_P | EF_UF),
	R(VO_ASHR, "ashr", FM_STD, TM_I, T, T, NONE, VIF_EXACT, EF_P | EF_UF),
	R(VO_UMULHI, "umulhi", FM_STD, TM_I64, I64, I64, NONE, 0, EF_P),
	R(VO_SMULHI, "smulhi", FM_STD, TM_I64, I64, I64, NONE, 0, EF_P),
	R(VO_NEG, "neg", FM_STD, TM_I, T, NONE, NONE, ARITH_FL, EF_P | EF_UF),
	R(VO_NOT, "not", FM_STD, TM_I, T, NONE, NONE, 0, EF_P),
	R(VO_CLZ, "clz", FM_STD, TM_I, T, NONE, NONE, VIF_NZ, EF_P | EF_UF),
	R(VO_CTZ, "ctz", FM_STD, TM_I, T, NONE, NONE, VIF_NZ, EF_P | EF_UF),
	R(VO_POPCNT, "popcnt", FM_STD, TM_I, T, NONE, NONE, 0, EF_P),
	R(VO_BSWAP, "bswap", FM_STD, TM_I, T, NONE, NONE, 0, EF_P),
	R(VO_EXT, "ext", FM_EXT, TM_I, I, NONE, NONE, 7, EF_P),
	R(VO_TRUNC, "trunc", FM_STD, TM_I32, I64, NONE, NONE, 0, EF_P),
	R(VO_ICMP, "icmp", FM_CC, TM_I32, X, X, NONE, 15, EF_P),
	R(VO_FCMP, "fcmp", FM_CC, TM_I32, FP, FP, NONE, 15, EF_P),
	R(VO_SELECT, "select", FM_STD, TM_ANY, I, T, T, 0, EF_P),
	R(VO_FADD, "fadd", FM_STD, TM_F, T, T, NONE, 0, EF_P | EF_F),
	R(VO_FSUB, "fsub", FM_STD, TM_F, T, T, NONE, 0, EF_P | EF_F),
	R(VO_FMUL, "fmul", FM_STD, TM_F, T, T, NONE, 0, EF_P | EF_F),
	R(VO_FDIV, "fdiv", FM_STD, TM_F, T, T, NONE, 0, EF_P | EF_F),
	R(VO_FNEG, "fneg", FM_STD, TM_F, T, NONE, NONE, 0, EF_P),
	R(VO_FABS, "fabs", FM_STD, TM_F, T, NONE, NONE, 0, EF_P),
	R(VO_FSQRT, "fsqrt", FM_STD, TM_F, T, NONE, NONE, 0, EF_P | EF_F),
	R(VO_FMA, "fma", FM_STD, TM_F, T, T, T, VIF_NEGP | VIF_NEGC, EF_P | EF_F),
	R(VO_FEXT, "fext", FM_STD, TM_F64, F32, NONE, NONE, 0, EF_P | EF_F),
	R(VO_FTRUNC, "ftrunc", FM_STD, TM_F32, F64, NONE, NONE, 0, EF_P | EF_F),
	R(VO_S2F, "s2f", FM_STD, TM_F, I, NONE, NONE, 0, EF_P | EF_F),
	R(VO_U2F, "u2f", FM_STD, TM_F, I, NONE, NONE, 0, EF_P | EF_F),
	R(VO_F2S, "f2s", FM_STD, TM_I, F, NONE, NONE, 0, EF_U),
	R(VO_F2U, "f2u", FM_STD, TM_I, F, NONE, NONE, 0, EF_U),
	R(VO_BITCAST, "bitcast", FM_STD, TM_I | TM_F, BITC, NONE, NONE, 0, EF_P),
	R(VO_PTRADD, "ptradd", FM_STD, TM_PTR, PTR, I64, NONE, 0, EF_P),
	R(VO_PSUB, "psub", FM_STD, TM_I64, PTR, PTR, NONE, 0, EF_P),
	R(VO_P2I, "p2i", FM_STD, TM_I64, PTR, NONE, NONE, 0, EF_P),
	R(VO_I2P, "i2p", FM_STD, TM_PTR, I64, NONE, NONE, 0, EF_P),
	R(VO_LOAD, "load", FM_LOAD, TM_ANY, PTR, IMM, NONE, 0x1f7, EF_R | EF_U),
	R(VO_STORE, "store", FM_STORE, TM_V, PTR, ANY, IMM, 0x1f3, EF_W | EF_U),
	R(VO_MEMCPY, "memcpy", FM_MEMCPY, TM_V, PTR, PTR, CONSTV, 0xff0, EF_R | EF_W | EF_U),
	R(VO_MEMSET, "memset", FM_MEMCPY, TM_V, PTR, I32, CONSTV, 0x0f0, EF_W | EF_U),
	R(VO_ALOAD, "aload", FM_ATOM, TM_I, PTR, NONE, NONE, 0x1f3, EF_R | EF_U | EF_A),
	R(VO_ASTORE, "astore", FM_ATOM, TM_V, PTR, I, NONE, 0x1f3, EF_W | EF_U | EF_A),
	R(VO_AXCHG, "axchg", FM_ATOM, TM_I, PTR, T, NONE, 0x1f3, EF_R | EF_W | EF_U | EF_A),
	R(VO_AXADD, "axadd", FM_ATOM, TM_I, PTR, T, NONE, 0x1f3, EF_R | EF_W | EF_U | EF_A),
	R(VO_ACAS, "acas", FM_ATOM, TM_I, PTR, T, T, 0x1f3, EF_R | EF_W | EF_U | EF_A),
	R(VO_AFENCE, "afence", FM_NONE, TM_V, NONE, NONE, NONE, 0, EF_A),
	R(VO_CALL, "call", FM_CALL, TM_V | TM_ANY | TM_MULTI, PTR, SIG, EXT, 7, EF_C),
	R(VO_VA_START, "va_start", FM_STD, TM_V, PTR, NONE, NONE, 0, EF_W),
	R(VO_VA_ARG, "va_arg", FM_VAARG, TM_PTR, PTR, GRP, NONE, 0, EF_R | EF_W | EF_U),
	R(VO_TLSADDR, "tlsaddr", FM_TLS, TM_PTR, SYMREF, IMM, NONE, 0, EF_R),
	R(VO_ASM, "asm", FM_ASM, TM_V | TM_ANY | TM_MULTI, TPL, EXT, NONE, 0, EF_C),
	R(VO_BARRIER, "barrier", FM_NONE, TM_V, NONE, NONE, NONE, 0, EF_R | EF_W),
	R(VO_FRAMEADDR, "frameaddr", FM_STD, TM_PTR, IMM, NONE, NONE, 0, EF_R),
	R(VO_RETADDR, "retaddr", FM_STD, TM_PTR, IMM, NONE, NONE, 0, EF_R),
	R(VO_BR, "br", FM_BR, TM_V, BLK, NONE, EXT, 0, EF_T),
	R(VO_BRIF, "brif", FM_STD, TM_V, I, BLK, BLK, VIF_LIKELY | VIF_UNLIKELY, EF_T),
	R(VO_SWITCH, "switch", FM_SWITCH, TM_V, I, BLK, EXT, 0, EF_T),
	R(VO_IBR, "ibr", FM_IBR, TM_V, PTR, NONE, EXT, 0, EF_T),
	R(VO_ASMGOTO, "asmgoto", FM_ASMGOTO, TM_V | TM_ANY | TM_MULTI, TPL, EXT, EXT, 0, EF_T | EF_C),
	R(VO_RET, "ret", FM_RET, TM_V, NONE, NONE, EXT, 0, EF_T),
	R(VO_TRAP, "trap", FM_NONE, TM_V, NONE, NONE, NONE, 0, EF_T),
};

const char *const vir_tyname[VT_N] = { "void", "i32", "i64", "f32", "f64", "ptr", "multi" };
const char *const vir_icc[10] = { "eq", "ne", "slt", "sle", "sgt", "sge", "ult", "ule", "ugt", "uge" };
const char *const vir_fcc[8] = { "oeq", "une", "olt", "ole", "ogt", "oge", "uno", "one" };

const vir_opinfo *vir_opfind(const char *s, size_t n)
{
	for (int i = 0; i < 128; i++) {
		const char *nm = vir_optab[i].name;

		if (nm && strlen(nm) == n && !memcmp(nm, s, n))
			return &vir_optab[i];
	}
	return NULL;
}

bool vir_is_const_op(uint32_t op) { return op >= VO_ICONST && op <= VO_LABELADDR; }
bool vir_is_term(uint32_t op) { return op >= VO_BR && op <= VO_TRAP && vir_optab[op].name; }

uint32_t vir_tysize(uint32_t ty)
{
	return ty == VT_I32 || ty == VT_F32 ? 4 : ty == VT_I64 || ty == VT_F64 || ty == VT_PTR ? 8 : 0;
}

uint32_t vir_opnd_fixed(uint32_t op, uint32_t ty, uint32_t k)
{
	switch (vir_optab[op & 255].k[k > 2 ? 2 : k]) {
	case OK_T: return ty;
	case OK_I32: return VT_I32;
	case OK_F32: return VT_F32;
	case OK_I64: case OK_CONSTV: return VT_I64;
	case OK_F64: return VT_F64;
	case OK_PTR: return VT_PTR;
	case OK_BITC: return ty == VT_I32 ? VT_F32 : ty == VT_F32 ? VT_I32 : ty == VT_I64 ? VT_F64 : ty == VT_F64 ? VT_I64 : VT_VOID;
	default: return VT_VOID;
	}
}
