/* Structured operands shared by the assembler and direct object emitter. */
#ifndef VCC_X64_OPERAND_H
#define VCC_X64_OPERAND_H
#include "../../../as/as.h"
enum { XR_NONE, XR_GPR, XR_XMM, XR_ST, XR_SEG, XR_RIP, XR_CR, XR_DR };
enum { XK_REG, XK_IMM, XK_MEM, XK_EXPR };	/* XK_EXPR: a bare expression (branch target, absolute memory) */
/* relocation modifiers: sym@MOD */
enum { XM_NONE, XM_PLT, XM_GOTPCREL, XM_TPOFF, XM_GOTTPOFF, XM_TLSGD, XM_TLSLD, XM_DTPOFF, XM_GOTOFF, XM_GOT };

typedef struct {
	uint8_t kind, cls, reg, size, hi8, star, needrex;	/* register: class, number, size; hi8: ah..bh */
	int8_t base, index;	/* memory: -1 none; base 16: rip */
	uint8_t scale, seg, mod;	/* seg: 0 none, 0x64 fs, 0x65 gs */
	ElfVal v;			/* immediate / displacement / target */
	int hasv;
} XOp;

/* Encode already parsed operands using the assembler's instruction families.
 * The mnemonic and operands may be modified; no text parsing is performed. */
int as_x64_insn(As *a, char *mn, XOp *o, int nops);
#endif
