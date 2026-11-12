/* vcc code generator, arm64: registers, condition codes and the instruction records
 * the code generator builds (included by emit/emitimpl.h before the emitter state) */
#include "../a64.h"

/* ---------------- instruction records ---------------- */

/* registers: 0-30 x0-x30, ZR, SP, FRAME (pseudo base of stack slots) */
enum { R_FP = 29, R_LR = 30, ZR = 31, SP = 32, FRAME = 33, R_X16 = 16, R_X17 = 17 };
#define R_BP 28		/* frame base of a realigned dynamic frame (ecx.falign, finish_function) */

enum {
	I_LABEL,	/* label */
	I_RAW,		/* off: text pool offset, ref: length; cond 1: inline asm (//APP in -S), RAW_MASKS: with its registers */
	/* rd = rn op (rm << sh); cond: shift kind (0 lsl, 1 lsr, 2 asr) */
	I_ADD, I_SUB, I_ADDS, I_SUBS, I_AND, I_ORR, I_EOR, I_ANDS, I_BIC, I_ORN, I_ADC, I_SBC, I_ADCS, I_SBCS,
	/* rd = rn op imm (any value: the backend splits or materialises) */
	I_ADDI, I_SUBI, I_ADDSI, I_SUBSI, I_ANDI, I_ORRI, I_EORI, I_ANDSI,
	I_MOVI,		/* rd = imm */
	I_MOV,		/* rd = rn (either may be SP) */
	I_MADD, I_MSUB,	/* rd = ra +- rn * rm */
	I_SDIV, I_UDIV, I_SMULH, I_UMULH, I_LSLV, I_LSRV, I_ASRV,
	I_SBFM, I_UBFM, I_BFM,	/* imm: immr, sh: imms */
	I_CSEL, I_CSINC, I_CSINV, I_CSNEG,	/* cond */
	I_CLZ, I_CLS, I_RBIT, I_REV, I_REV16, I_REV32,
	/* memory: sz = log2 bytes; cond: 0 zero-extend, 1 sign-extend to x, 2 to w; [rn + imm] */
	I_LD, I_ST,
	/* [rn + (rm extended by ra, shifted left by sz when sh)]: ra 2 uxtw, 3 lsl (x), 6 sxtw; sz, cond as I_LD */
	I_LDX, I_STX,
	I_LDP, I_STP,	/* x regs rd, ra; [rn + imm]; cond: 0 offset, 1 pre-index, 2 post-index */
	/* symbol references: ref (see refs below), off: addend */
	I_ADRP,		/* rd = page(ref + off) */
	I_ADDLO,	/* rd = rn + lo12(ref + off) */
	I_LDLO, I_STLO,	/* [rn + lo12(ref + off)], sz/cond as I_LD */
	I_LDGOT,	/* rd = address of ref through the GOT (weak undefined) */
	I_ADR,		/* rd = local label */
	I_B, I_BC,	/* label; I_BC: cond */
	I_CBZ, I_CBNZ,	/* rd, label */
	I_BL,		/* ref */
	I_BLR, I_BR,	/* rn */
	I_RET,
	I_BRK,		/* imm */
	I_PRFM,		/* [rn] */
	I_MRS,		/* rd, ref: raw text (system register name) */
	I_MOVSP,	/* frame setup pseudo (see finish) */
	I_NOP,
	I_WORD,		/* imm: an encoded instruction (registers already in it) */
	I_TLV,		/* x0 = address of thread-local ref; cond 0: Darwin TLV descriptor call (clobbers x16, lr),
			 * TLS_LE/TLS_IE: ELF via tpidr_el0 (clobbers x1), TLS_DESC: ELF TLSDESC call (x1, lr) */
	I_JT,		/* jump table dispatch: rd = index (already bounds-checked), label: the table; expanded by finish_function
			 * (adr x16; add x17; ldrsw x17; add x16; br x16), every target label is LREF_EXT */
	I_JTE,		/* a table word: label (off) - ref, both labels, in bytes */
	I_TAIL,		/* ref: direct sibling target; cond 0 terminal marker, 1 branch after epilogue;
			 * VIR, anywhere in the body: 2 a sibling call to ref, 3 to the address in rn (expanded by
			 * finish_function: the epilogue, then b ref or br x16) */
};

enum { TLS_LE = 1, TLS_IE, TLS_DESC };	/* I_TLV.cond on ELF */
enum { C_EQ, C_NE, C_HS, C_LO, C_MI, C_PL, C_VS, C_VC, C_HI, C_LS, C_GE, C_LT, C_GT, C_LE, C_AL };

/* I_RAW cond 2: inline asm whose registers follow its text: uint64_t reads, writes */
#define RAW_MASKS 2

/* lrefc: a label that inline asm (asm goto) or a label address (&&l) may reach */
#define LREF_EXT (1u << 31)

typedef struct ins {
	uint8_t op, sz;		/* sz: 1 = 64-bit (x), 0 = 32-bit (w); memory: log2 bytes */
	uint8_t rd, rn, rm, ra;
	uint8_t cond, sh;
	union {
		int64_t imm;
		struct { int32_t off; uint32_t ref; };
		uint32_t label;
	};
} ins;
