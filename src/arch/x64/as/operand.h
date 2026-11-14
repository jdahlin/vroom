/* operand.h: the x86-64 assembler's structured operand (XOp: register, immediate, memory, or bare
 * expression) and as_x64_insn, the entry that encodes already parsed operands.
 *
 * Two producers share it: the assembler's AT&T text parser (asm.c) and the direct object emitter
 * (arch/x64/codegen/obj.c), which builds XOps itself and skips text.  as_x64_insn may modify the
 * mnemonic and the operands in place, so callers pass copies they do not reuse.  It reports errors
 * through as_error_report on the context given. */
#ifndef VCC_X64_OPERAND_H
#define VCC_X64_OPERAND_H
#include "as/as.h"
enum { XR_NONE, XR_GPR, XR_XMM, XR_ST, XR_SEG, XR_RIP, XR_CR, XR_DR, XR_ZMM, XR_MASK, XR_MM };
enum { XK_REG, XK_IMM, XK_MEM, XK_EXPR };	/* XK_EXPR: a bare expression (branch target, absolute memory) */
/* relocation modifiers: sym@MOD */
enum { XM_NONE, XM_PLT, XM_GOTPCREL, XM_TPOFF, XM_GOTTPOFF, XM_TLSGD, XM_TLSLD, XM_DTPOFF, XM_GOTOFF, XM_GOT, XM_IMGREL, XM_SECREL };	/* (the last two: COFF) */

typedef struct {
	uint8_t kind, cls, reg, size, hi8, star, needrex;	/* register: class, number, size; hi8: ah..bh */
	uint8_t mask, zero;             /* EVEX destination writemask and zeroing */
	int8_t base, index;	/* memory: -1 none; base 16: rip */
	uint8_t scale, seg, mod, addr32;	/* seg: 0 none, 0x64 fs, 0x65 gs */
	ElfVal v;			/* immediate / displacement / target */
	int hasv;
} XOp;

/* Encode already parsed operands using the assembler's instruction families.
 * The mnemonic and operands may be modified; no text parsing is performed. */
int as_x64_insn(as_context *a, char *mn, XOp *o, int nops);
/* ALU values are the x86 encoding's group digits. */
enum X64Op { X64O_ADD, X64O_OR, X64O_ADC, X64O_SBB, X64O_AND,
	X64O_SUB, X64O_XOR, X64O_CMP, X64O_MOV, X64O_LEA, X64O_MOVSX, X64O_MOVZX, X64O_TEST };
int as_x64_op(as_context *a, enum X64Op op, int size, XOp *o);
/* Branch condition 0..15 is the opcode nibble, 16 is an unconditional jump,
 * and -1 is a direct call. */
int as_x64_direct_branch(as_context *a, int condition, XOp *target);
/* Encode SETcc or CMOVcc with the condition nibble from the compiler record. */
enum X64ConditionOp { X64O_SETCC, X64O_CMOV };
int as_x64_condition(as_context *a, enum X64ConditionOp op, int condition, int size, XOp *operands);
#endif
