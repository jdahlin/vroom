/* vcc code generator, arm64: registers, condition codes and the instruction records
 * the code generator builds (included by codegen/codegen-internal.h before the emitter state) */
#include "arch/a64/a64.h"

/* ---------------- instruction records ---------------- */

/* registers: 0-30 x0-x30, ZR, SP, A64_REGISTER_FRAME (pseudo base of stack slots) */
enum { R_FP = 29, R_LR = 30, ZR = 31, SP = 32, A64_REGISTER_FRAME = 33, R_X16 = 16, R_X17 = 17 };
#define A64_REGISTER_FRAME_BASE 28		/* frame base of a realigned dynamic frame (ecx.falign, finish_function) */

enum {
	A64_OP_LABEL,	/* label */
	A64_OP_RAW,		/* off: text pool offset, ref: length; cond 1: inline asm (//APP in -S), A64_RAW_HAS_REGISTER_MASKS: with its registers */
	/* rd = rn op (rm << sh); cond: shift kind (0 lsl, 1 lsr, 2 asr); A64_OP_ADD/A64_OP_SUB/A64_OP_ADDS/A64_OP_SUBS also
	 * 8 + option: rm extended (option 0-7 uxtb uxth uxtw uxtx sxtb sxth sxtw sxtx), then << sh (0-4) */
	A64_OP_ADD, A64_OP_SUB, A64_OP_ADDS, A64_OP_SUBS, A64_OP_AND, A64_OP_ORR, A64_OP_EOR, A64_OP_ANDS, A64_OP_BIC, A64_OP_ORN, A64_OP_ADC, A64_OP_SBC, A64_OP_ADCS, A64_OP_SBCS,
	/* rd = rn op imm (any value: the backend splits or materialises) */
	A64_OP_ADDI, A64_OP_SUBI, A64_OP_ADDSI, A64_OP_SUBSI, A64_OP_ANDI, A64_OP_ORRI, A64_OP_EORI, A64_OP_ANDSI,
	A64_OP_MOVI,		/* rd = imm */
	A64_OP_MOV,		/* rd = rn (either may be SP) */
	A64_OP_MADD, A64_OP_MSUB,	/* rd = ra +- rn * rm */
	A64_OP_SMADDL, A64_OP_UMADDL,	/* x: rd = ra + wn * wm, both extended (signed, unsigned) */
	A64_OP_SDIV, A64_OP_UDIV, A64_OP_SMULH, A64_OP_UMULH, A64_OP_LSLV, A64_OP_LSRV, A64_OP_ASRV,
	A64_OP_SBFM, A64_OP_UBFM, A64_OP_BFM,	/* imm: immr, sh: imms */
	A64_OP_CSEL, A64_OP_CSINC, A64_OP_CSINV, A64_OP_CSNEG,	/* cond */
	A64_OP_CLZ, A64_OP_CLS, A64_OP_RBIT, A64_OP_REV, A64_OP_REV16, A64_OP_REV32,
	/* memory: sz = log2 bytes; cond: 0 zero-extend, 1 sign-extend to x, 2 to w; [rn + imm] */
	A64_OP_LD, A64_OP_ST,
	/* [rn + (rm extended by ra, shifted left by sz when sh)]: ra 2 uxtw, 3 lsl (x), 6 sxtw; sz, cond as A64_OP_LD */
	A64_OP_LDX, A64_OP_STX,
	A64_OP_LDP, A64_OP_STP,	/* x regs rd, ra; [rn + imm]; cond: 0 offset, 1 pre-index, 2 post-index */
	/* symbol references: ref (see refs below), off: addend */
	A64_OP_ADRP,		/* rd = page(ref + off) */
	A64_OP_ADDLO,	/* rd = rn + lo12(ref + off) */
	A64_OP_LDLO, A64_OP_STLO,	/* [rn + lo12(ref + off)], sz/cond as A64_OP_LD */
	A64_OP_LDGOT,	/* rd = address of ref through the GOT (weak undefined) */
	A64_OP_ADR,		/* rd = local label */
	A64_OP_B, A64_OP_BC,	/* label; A64_OP_BC: cond */
	A64_OP_CBZ, A64_OP_CBNZ,	/* rd, label */
	A64_OP_TBZ, A64_OP_TBNZ,	/* rd, sh: the bit (sz 1 when it is 32-63), label (+-32 KB: finish_function relaxes) */
	A64_OP_BL,		/* ref */
	A64_OP_BLR, A64_OP_BR,	/* rn */
	A64_OP_RET,
	A64_OP_BRK,		/* imm */
	A64_OP_PRFM,		/* [rn] */
	A64_OP_MRS,		/* rd, ref: raw text (system register name) */
	I_MOVSP,	/* frame setup pseudo (see finish) */
	A64_OP_NOP,
	A64_OP_WORD,		/* imm: an encoded instruction (registers already in it) */
	A64_OP_TLV,		/* x0 = address of thread-local ref; cond 0: Darwin TLV descriptor call (clobbers x16, lr),
			 * A64_TLS_LOCAL_EXEC/A64_TLS_INITIAL_EXEC: ELF via tpidr_el0 (clobbers x1), A64_TLS_DESCRIPTOR: ELF TLSDESC call (x1, lr) */
	A64_OP_JT,		/* jump table dispatch: rd = index (already bounds-checked; cond 2: a w register, uxtw), label: the table; expanded by finish_function
			 * (adr x16; add x17; ldrsw x17; add x16; br x16), every target label is CODEGEN_LABEL_EXTERNAL */
	A64_OP_JTE,		/* a table word: label (off) - ref, both labels, in bytes */
	A64_OP_TAIL,		/* ref: direct sibling target; cond 0 terminal marker, 1 branch after epilogue;
			 * VIR, anywhere in the body: 2 a sibling call to ref, 3 to the address in rn (expanded by
			 * finish_function: the epilogue, then b ref or br x16); 4 a return: the epilogue and ret when
			 * it is short, else b label (the shared epilogue at the end) */
	A64_OP_LDF, A64_OP_STF,	/* FP register rd (v0-v31; sz 2: s, 3: d) and [rn + imm] (VIR's lowering) */
};

enum { A64_TLS_LOCAL_EXEC = 1, A64_TLS_INITIAL_EXEC, A64_TLS_DESCRIPTOR };	/* A64_OP_TLV.cond on ELF */
/* A64_OP_BL/A64_OP_BLR.cond: sh is the mask of the x0-x7 the call's arguments are in (VIR's lowering;
 * otherwise a call reads x0-x8) */
#define A64_CALL_ARGUMENT_REGISTERS 0xa5
/* A64_OP_LD.ra of a volatile access not at A64_REGISTER_FRAME (VIR's lowering) */
#define A64_LOAD_VOLATILE 1
enum { A64_CONDITION_EQ, A64_CONDITION_NE, A64_CONDITION_HS, A64_CONDITION_LO, A64_CONDITION_MI, A64_CONDITION_PL, A64_CONDITION_VS, A64_CONDITION_VC, A64_CONDITION_HI, A64_CONDITION_LS, A64_CONDITION_GE, A64_CONDITION_LT, A64_CONDITION_GT, A64_CONDITION_LE, A64_CONDITION_AL };

/* A64_OP_RAW cond 2: inline asm whose registers follow its text: uint64_t reads, writes */
#define A64_RAW_HAS_REGISTER_MASKS 2

/* lrefc: a label that inline asm (asm goto) or a label address (&&l) may reach */
#define CODEGEN_LABEL_EXTERNAL (1u << 31)

/* A64_OP_NOP.cond: where VIR's lowering wants the prologue (shrink-wrapping: the records in front
 * of it run without the frame; finish_function checks that they can, else it goes to the top) */
enum { A64_NOP_PROLOGUE = 0xc0 };
/* A64_OP_TAIL.sh: 1 on a return or sibling call (cond 2-4) in front of A64_NOP_PROLOGUE: no epilogue */

typedef struct a64_record {
	uint8_t opcode, size;		/* sz: 1 = 64-bit (x), 0 = 32-bit (w); memory: log2 bytes */
	uint8_t rd, rn, rm, ra;
	uint8_t condition, shift;
	union {
		int64_t immediate;
		struct { int32_t offset; uint32_t reference; };
		uint32_t label;
	};
} a64_record;
