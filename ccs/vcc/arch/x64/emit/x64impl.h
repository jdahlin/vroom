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

enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15, XMM0 = 16, XMM8 = 24, XMM15 = 31,
       RIP = 32, XNOREG = 0xff };
#define X_TEMPS ((1u << RAX) | (1u << RCX) | (1u << RDX) | (1u << RSI) | (1u << RDI) | (1u << R8) | (1u << R9) | (1u << R10))
/* floating point temporaries: xmm8-xmm14 (xmm0-7 carry arguments and results, xmm15 is scratch) */
#define X_FTEMPS (((1u << XMM15) - 1) & ~((1u << XMM8) - 1))
#define IS_XMM(r) ((r) >= XMM0 && (r) <= XMM15)

enum {
	X_LABEL,	/* label */
	X_MOV,		/* r0 = r1 (sz 4 or 8) */
	X_MOVI,		/* r0 = imm */
	X_LD,		/* r0 = [r1 + off (+ ref)]: sz bytes, cond 1 sign-extended (to 64 bits) */
	X_ST,		/* [r1 + off (+ ref)] = r2: sz bytes */
	X_LEA,		/* r0 = r1 + off (+ ref) */
	/* r0 = r1 op r2 (sz 4 or 8) */
	X_ADD, X_SUB, X_AND, X_OR, X_XOR, X_IMUL, X_ADC, X_SBB,
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
	X_MULW,		/* rdx:rax = rax * r1 (unsigned, 64 x 64 -> 128) */
	X_UD2,
	X_TLS,		/* r0 = the address of ref, thread-local: cond 0 local-exec, 1 initial-exec;
			   2 general dynamic: rax = __tls_get_addr(ref's descriptor) (a call) */
	X_GOT,		/* r0 = ref's address from the GOT */
	X_XCHG,		/* r0 <-> [r1 + off] (sz; locked) */
	X_XADD,		/* lock xadd: r0 = [r1 + off], [r1 + off] += old r0 */
	X_CMPXCHG,	/* lock cmpxchg: if [r1 + off] == rax then = r2, else rax = [r1 + off] */
	X_MFENCE,
	X_BARRIER,	/* empty asm: a compiler memory/flags barrier, no machine instruction */
	X_JMPR,		/* jmp *r1 */
	X_LEAL,		/* r0 = the address of local label `label` */
	X_RAW,		/* inline asm: e->raw's text at off, ref bytes */
	X_NAMED,	/* ref's name as a label here */
	X_BSF, X_BSR,	/* r0 = index of the lowest / highest set bit of r1 */
	X_BSWAP,	/* r0 = r0 byte-reversed (sz 4, 8) */
	X_EXT,		/* r0 = r1's low sz bytes extended to 64 bits (cond 1: sign) */
	X_REP,		/* cond 0: rep movsb (rdi rsi rcx), 1: rep stosb (rdi al rcx) */
	/* SSE (X_LD/X_ST/X_MOV take xmm registers too) */
	X_FADD, X_FSUB, X_FMUL, X_FDIV,	/* r0 = r1 op r2 (sz 4 ss, 8 sd) */
	X_FXOR,		/* r0 = r1 ^ r2 (xorps) */
	X_FCMP,		/* flags: ucomis r1 ? r2 */
	X_FSETCC,	/* r0 = (cond 0: ==, 1: !=) after X_FCMP/x87 compare, NaN-aware (r11 scratch) */
	X_CVTI2F,	/* xmm r0 = gpr r1: sz float size, sh int size (4, 8) */
	X_CVTF2I,	/* gpr r0 = xmm r1 truncated: sz int size, sh float size */
	X_CVTF2F,	/* xmm r0 = xmm r1: sz the result's size */
	X_X87,		/* cond: XF_* (with a memory operand where it takes one) */
	X_RESUME_FRAME,	/* rebuild realigned R12 from restored RBP after builtin longjmp */
	X_NOPS
};

enum { XF_LDT, XF_STPT, XF_LD, XF_STP, XF_ILD, XF_ISTP, XF_ADD, XF_SUB, XF_MUL, XF_DIV, XF_CHS, XF_UCOMIP, XF_POP,
       XF_STCW, XF_LDCW, XF_LDZ };

/* condition codes in encoding order */
enum { CC_O, CC_NO, CC_B, CC_AE, CC_E, CC_NE, CC_BE, CC_A, CC_S, CC_NS, CC_P, CC_NP, CC_L, CC_GE, CC_LE, CC_G };

typedef struct xst {
	rec *in;		/* the function's records */
	uint32_t nin, capin;
	rec *tw;		/* twoaddr.c's output */
	uint32_t ntw;
	uint32_t live;		/* temporaries in use (bit = register) */
	int pushes;		/* 8-byte pushes outstanding (the call alignment) */
	/* locals.c: persistent homes, disjoint from expression temporaries and R12. */
	uint32_t lsym[4];
	uint8_t lreg[4], nlreg;
	int64_t lsave[4];
	/* the function's incoming arguments (abi.c) */
	int ngp, nfp;		/* named arguments in general and vector registers */
	int32_t stk;		/* bytes of named stack arguments */
	int64_t sret;		/* frame slot of the hidden result pointer (0: none) */
	int64_t save;		/* variadic: the register save area's slot (0: none) */
	int64_t csslot[16];	/* callee-saved registers inline asm uses: the prologue saves them here */
} xst;

/* the integer argument registers, in order */
static inline int x64_argreg(int i) { static const uint8_t r[6] = { RDI, RSI, RDX, RCX, R8, R9 }; return r[i]; }
VPRIV xst *x64_state(ecx *e);
VPRIV rec *x64_op3(ecx *e, int op, int sz, int r0, int r1, int r2);
VPRIV void x64_mem(ecx *e, int op, int sz, int r, const lval *l, bool sgn);
VPRIV void x64_mov(ecx *e, int rd, int rs);
VPRIV void x64_movi(ecx *e, int r, int64_t v);
VPRIV uint32_t x64_save(ecx *e, const uint8_t *regs, int n, int k0, int k1);
VPRIV void x64_restore(ecx *e, uint32_t sv);
VPRIV int x64_ftreg(ecx *e);
VPRIV rec *x64_put(ecx *e, int op);
VPRIV void x64_twoaddr(ecx *e);
VPRIV void x64_locals_begin(ecx *e, uint32_t s);
VPRIV void x64_locals_params(ecx *e);
VPRIV void x64_locals_restore(ecx *e);
VPRIV bool x64_local(ecx *e, uint32_t s, lval *l);
/* effects.c: logical records before or after two-address expansion. */
VPRIV const uint8_t *x64_flow_table(void);
/* Prepared for shared copy/liveness passes; also exercised by contract tests. */
VPRIV __attribute__((unused)) receffects x64_effects(const ecx *e, const rec *i);
VPRIV void x64_print(ecx *e, tbuf *b, uint32_t s);
VPRIV void x64_x87(ecx *e, int xf, const lval *l, int sz);

VPRIV void x64_lbl(tbuf *b, uint32_t s, uint32_t l);
VPRIV void x64_reg(tbuf *b, int r, int sz);
VPRIV int x64_asm_namedreg(const char *name);
VPRIV int x64_tlsaddr(ecx *e, uint32_t ref, int model);
VPRIV bool sym_needs_got(cc *c, uint32_t ref);	/* arch/a64/emit/value.c (shared) */
VPRIV int x64_base(ecx *e, const lval *l, uint32_t *ref);
VPRIV void x64_nofp(ecx *e);
VPRIV void x64_x87(ecx *e, int xf, const lval *l, int sz);	/* an x87 record (fp.c) */	/* fatal under -mgeneral-regs-only / -mno-sse */

#endif
