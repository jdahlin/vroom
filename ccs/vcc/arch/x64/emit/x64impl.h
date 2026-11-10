/*
 * vcc code generator, x86-64 (PLAN-x86.md): the lowering of the walker's
 * operations (layer 2) to records (emit/rec/rec.h), the two-address pass
 * and the printer of clang's AT&T syntax.  System V ABI, Linux ELF.
 *
 * Registers are physical, numbered as the encoding does (rax 0 .. r15 15).
 * Temporaries come from the caller-saved rax rcx rdx rsi rdi r8 r9 r10;
 * r11 is the lowering's scratch and never handed out.  Operations that
 * need a fixed register (div: rax rdx, shifts: rcx) save what lives there
 * around themselves; a call saves the live temporaries.  Records are
 * three-address (r[0] = r[1] op r[2]) until twoaddr.c.  Frame slots are
 * rbp-relative.  Part of the emit unity TU: every name starts with x64_
 * or X_.
 */
#ifndef VCC_X64IMPL_H
#define VCC_X64IMPL_H
#include "../../../emit/walk/walk.h"

enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15, RIP, XNOREG = 0xff };
#define X_TEMPS ((1u << RAX) | (1u << RCX) | (1u << RDX) | (1u << RSI) | (1u << RDI) | (1u << R8) | (1u << R9) | (1u << R10))

enum {
	X_LABEL,	/* label */
	X_MOV,		/* r0 = r1 (sz 4 or 8) */
	X_MOVI,		/* r0 = imm */
	X_LD,		/* r0 = [r1 + off (+ ref)]: sz bytes, cond 1 sign-extended (to 64 bits) */
	X_ST,		/* [r1 + off (+ ref)] = r2: sz bytes */
	X_LEA,		/* r0 = r1 + off (+ ref) */
	/* r0 = r1 op r2 (sz 4 or 8) */
	X_ADD, X_SUB, X_AND, X_OR, X_XOR, X_IMUL,
	/* r0 = r1 op imm (32-bit signed) */
	X_ADDI, X_SUBI, X_ANDI, X_ORI, X_XORI, X_IMULI,
	X_SHLI, X_SHRI, X_SARI,	/* r0 = r1 op imm */
	X_SHL, X_SHR, X_SAR,	/* r0 = r1 op cl (r2 is rcx) */
	X_NEG, X_NOT,		/* r0 = op r1 */
	X_CMP,		/* flags: r1 ? r2 */
	X_CMPI,		/* flags: r1 ? imm */
	X_TEST,		/* flags: r1 & r1 */
	X_SETCC,	/* r0 = cond ? 1 : 0 (32 bits) */
	X_JMP,		/* label */
	X_JCC,		/* cond, label */
	X_CALL,		/* ref; cond 1: variadic (al already set) */
	X_CALLR,	/* r1 */
	X_PUSH, X_POP,	/* r1 / r0 */
	X_ADJSP,	/* rsp += imm */
	X_CQO,		/* rdx = sign of rax (sz 4: cltd, 8: cqto) */
	X_IDIV, X_DIV,	/* rdx:rax / r1: rax quotient, rdx remainder */
	X_EXT,		/* r0 = r1's low sz bytes extended to 64 bits (cond 1: sign) */
	X_NOPS
};

/* condition codes in encoding order */
enum { CC_O, CC_NO, CC_B, CC_AE, CC_E, CC_NE, CC_BE, CC_A, CC_S, CC_NS, CC_P, CC_NP, CC_L, CC_GE, CC_LE, CC_G };

typedef struct xst {
	rec *in;		/* the function's records */
	uint32_t nin, capin;
	rec *tw;		/* twoaddr.c's output */
	uint32_t ntw;
	uint32_t live;		/* temporaries in use (bit = register) */
	int pushes;		/* 8-byte pushes outstanding (the call alignment) */
} xst;

VPRIV xst *x64_state(ecx *e);
VPRIV rec *x64_put(ecx *e, int op);
VPRIV void x64_twoaddr(ecx *e);
VPRIV void x64_print(ecx *e, tbuf *b, uint32_t s);

#endif
