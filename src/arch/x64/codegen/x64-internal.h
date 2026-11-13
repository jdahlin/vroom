/*
 * vcc code generator, x86-64 (PLAN-x86.md): the lowering of the walker's
 * operations (layer 2) to records (codegen/rec/record.h), the two-address pass
 * and the printer of clang's AT&T syntax.  System V ABI, Linux ELF.
 *
 * Registers are physical, numbered as the encoding does (rax 0 .. r15 15).
 * Temporaries come from the caller-saved rax rcx rdx rsi rdi r8 r9 r10;
 * r11 is the lowering's scratch and never handed out.  Operations that
 * need a fixed register (div: rax rdx, shifts: rcx) save what lives there
 * around themselves; a call saves the live temporaries.  Records are
 * three-address (r[0] = r[1] op r[2]) until twoaddr.c.  Frame slots are
 * rbp-relative.  Part of the codegen unity TU: every name starts with x64_
 * or X_.
 */
#ifndef VCC_X64_INTERNAL_H
#define VCC_X64_INTERNAL_H
#include "../../../codegen/walk/walk.h"

enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15, XMM0 = 16, XMM8 = 24, XMM15 = 31,
       RIP = 32, X64_REGISTER_FRAME = 33, X64_REGISTER_NONE = 0xff };
#define X64_TEMPORARIES ((1u << RAX) | (1u << RCX) | (1u << RDX) | (1u << RSI) | (1u << RDI) | (1u << R8) | (1u << R9) | (1u << R10))
/* floating point temporaries: xmm8-xmm14 (xmm0-7 carry arguments and results, xmm15 is scratch) */
#define X64_FLOAT_TEMPORARIES (((1u << XMM15) - 1) & ~((1u << XMM8) - 1))
#define X64_REGISTER_IS_XMM(r) ((r) >= XMM0 && (r) <= XMM15)

enum {
	X64_OP_LABEL,	/* label */
	X64_OP_MOV,		/* r0 = r1 (sz 4 or 8) */
	X64_OP_MOVI,		/* r0 = imm */
	X64_OP_LD,		/* r0 = [r1 + off (+ ref)]: sz bytes, cond 1 sign-extended (to 64 bits) */
	X64_OP_ST,		/* [r1 + off (+ ref)] = r2: sz bytes */
	X64_OP_LEA,		/* r0 = r1 + off (+ ref) */
	X64_OP_LEASCALE,	/* r0 = r1 + (r2 << sh), sz 4 zero-extends or sz 8 */
	/* r0 = r1 op r2 (sz 4 or 8) */
	X64_OP_ADD, X64_OP_SUB, X64_OP_AND, X64_OP_OR, X64_OP_XOR, X64_OP_IMUL, X64_OP_ADC, X64_OP_SBB,
	/* r0 = r1 op imm (32-bit signed) */
	X64_OP_ADDI, X64_OP_SUBI, X64_OP_ANDI, X64_OP_ORI, X64_OP_XORI, X64_OP_IMULI,
	X64_OP_SHLI, X64_OP_SHRI, X64_OP_SARI,	/* r0 = r1 op imm */
	X64_OP_SHL, X64_OP_SHR, X64_OP_SAR,	/* r0 = r1 op cl (r2 is rcx) */
	X64_OP_NEG, X64_OP_NOT,		/* r0 = op r1 */
	X64_OP_CMP,		/* flags: r1 ? r2 */
	X64_OP_CMPI,		/* flags: r1 ? imm */
	X64_OP_TEST,		/* flags: r1 & r1 */
	X64_OP_SETCC,	/* r0 = cond ? 1 : 0 (32 bits) */
	X64_OP_JMP,		/* label */
	X64_OP_JCC,		/* cond, label */
	X64_OP_CALL,		/* ref; cond 1: variadic (al already set) */
	X64_OP_CALLR,	/* r1 */
	X64_OP_TAIL,		/* ref: terminal sibling-call marker, emitted after the epilogue */
	X64_OP_PUSH, X64_OP_POP,	/* r1 / r0 */
	X64_OP_ADJSP,	/* rsp += imm */
	X64_OP_CQO,		/* rdx = sign of rax (sz 4: cltd, 8: cqto) */
	X64_OP_IDIV, X64_OP_DIV,	/* rdx:rax / r1: rax quotient, rdx remainder */
	X64_OP_MULW,		/* rdx:rax = rax * r1 (unsigned, 64 x 64 -> 128) */
	X64_OP_SMULW,	/* rdx:rax = rax * r1 (signed, 64 x 64 -> 128) */
	X64_OP_UD2,
	X64_OP_TLS,		/* r0 = the address of ref, thread-local: cond 0 local-exec, 1 initial-exec;
			   2 general dynamic: rax = __tls_get_addr(ref's descriptor) (a call) */
	X64_OP_GOT,		/* r0 = ref's address from the GOT */
	X64_OP_XCHG,		/* r0 <-> [r1 + off] (sz; locked) */
	X64_OP_XADD,		/* lock xadd: r0 = [r1 + off], [r1 + off] += old r0 */
	X64_OP_CMPXCHG,	/* lock cmpxchg: if [r1 + off] == rax then = r2, else rax = [r1 + off] */
	X64_OP_MFENCE,
	X64_OP_BARRIER,	/* empty asm: a compiler memory/flags barrier, no machine instruction */
	X64_OP_JMPR,		/* jmp *r1 */
	X64_OP_LEAL,		/* r0 = the address of local label `label` */
	X64_OP_BLENDVB,	/* SSE4.1: xmm1 = select(xmm1, xmm2, high bits of xmm0) */
	X64_OP_PCMPSTR,	/* SSE4.2: xmm0/xmm1, eax/edx lengths; cond selects opcode, imm control */
	X64_OP_RAW,		/* inline asm: e->raw's text at off, ref bytes */
	X64_OP_NAMED,	/* ref's name as a label here */
	X64_OP_BSF, X64_OP_BSR,	/* r0 = index of the lowest / highest set bit of r1 */
	X64_OP_BSWAP,	/* r0 = r0 byte-reversed (sz 4, 8) */
	X64_OP_EXT,		/* r0 = r1's low sz bytes extended to 64 bits (cond 1: sign) */
	X64_OP_REP,		/* cond 0: rep movsb (rdi rsi rcx), 1: rep stosb (rdi al rcx) */
	/* SSE (X64_OP_LD/X64_OP_ST/X64_OP_MOV take xmm registers too) */
	X64_OP_FADD, X64_OP_FSUB, X64_OP_FMUL, X64_OP_FDIV,	/* r0 = r1 op r2 (sz 4 ss, 8 sd) */
	X64_OP_FXOR,		/* r0 = r1 ^ r2 (xorps) */
	X64_OP_FSQRT,	/* scalar square root r0 = sqrt(r1) */
	X64_OP_FCMP,		/* flags: ucomis r1 ? r2 */
	X64_OP_FSETCC,	/* r0 = (cond 0: ==, 1: !=) after X64_OP_FCMP/x87 compare, NaN-aware (r11 scratch) */
	X64_OP_CVTI2F,	/* xmm r0 = gpr r1: sz float size, sh int size (4, 8) */
	X64_OP_CVTF2I,	/* gpr r0 = xmm r1 truncated: sz int size, sh float size */
	X64_OP_CVTF2F,	/* xmm r0 = xmm r1: sz the result's size */
	X64_OP_X87,		/* cond: XF_* (with a memory operand where it takes one) */
	X64_OP_JTE,		/* a jump table word: .long label - ref (both labels) */
	X64_OP_RESUME_FRAME,	/* rebuild realigned R12 from restored RBP after builtin longjmp */
	X64_OP_LDX, X64_OP_STX,	/* memory at r1 + (r3 << sh) + off; load r0 / store r2 */
	X64_OP_CMOV,		/* r0 = flags[cond] ? r2 : r1 */
	X64_OP_TESTM,		/* flags: [r1 + (r3 << sh) + off] & (int32_t)ref; r3 optional */
	X64_OP_CMPM,		/* compare memory r1+(r3<<sh)+off with r2 (X64_REGISTER_NONE: immediate ref); cond reverses operands */
	X64_OP_COUNT
};

enum { X64_X87_LDT, X64_X87_STPT, X64_X87_LD, X64_X87_STP, X64_X87_ILD, X64_X87_ISTP, X64_X87_ADD, X64_X87_SUB, X64_X87_MUL, X64_X87_DIV, X64_X87_CHS, X64_X87_UCOMIP, X64_X87_POP,
       X64_X87_STCW, X64_X87_LDCW, X64_X87_LDZ };

/* condition codes in encoding order */
enum { X64_CONDITION_O, X64_CONDITION_NO, X64_CONDITION_B, X64_CONDITION_AE, X64_CONDITION_E, X64_CONDITION_NE, X64_CONDITION_BE, X64_CONDITION_A, X64_CONDITION_S, X64_CONDITION_NS, X64_CONDITION_P, X64_CONDITION_NP, X64_CONDITION_L, X64_CONDITION_GE, X64_CONDITION_LE, X64_CONDITION_G };

typedef struct x64_lowering_context {
	record *in;		/* the function's records */
	uint32_t nin, capin;
	record *tw;		/* twoaddr.c's output */
	uint32_t ntw;
	uint32_t live;		/* temporaries in use (bit = register) */
	bool asmcs;		/* inside an asm: out of temporaries, take a callee-saved register (saved by the prologue) */
	int pushes;		/* 8-byte pushes outstanding (the call alignment) */
	bool framefree;		/* frame.c: no frame/stack dependency in the final body */
	bool spframe;		/* allocated VIR: virtual frame base, leaving rbp allocatable */
	int64_t framesz, spdelta; /* printer's final frame and temporary outgoing arguments */
	/* locals.c: persistent homes, disjoint from expression temporaries and R12. */
	uint32_t lsym[4];
	uint8_t lreg[4], nlreg;
	/* the function's incoming arguments (abi.c) */
	int interrupt;		/* interrupt handler parameter count, zero for normal ABI */
	int ngp, nfp;		/* named arguments in general and vector registers */
	int32_t stk;		/* bytes of named stack arguments */
	int64_t sret;		/* frame slot of the hidden result pointer (0: none) */
	int64_t save;		/* variadic: the register save area's slot (0: none) */
	int64_t csslot[16];	/* callee saves for asm and register locals: prologue/epilogue + CFI */
} x64_lowering_context;

/* the integer argument registers, in order */
static inline int x64_argument_register(int i) { static const uint8_t r[6] = { RDI, RSI, RDX, RCX, R8, R9 }; return r[i]; }
VPRIV x64_lowering_context *x64_context_get(codegen_context *e);
VPRIV record *x64_record_emit_three_registers(codegen_context *e, int op, int sz, int r0, int r1, int r2);
VPRIV void x64_record_emit_memory(codegen_context *e, int op, int sz, int r, const codegen_lvalue *l, bool sgn);
VPRIV void x64_mov(codegen_context *e, int rd, int rs);
VPRIV void x64_record_emit_move_immediate(codegen_context *e, int r, int64_t v);
VPRIV uint32_t x64_save(codegen_context *e, const uint8_t *regs, int n, int k0, int k1);
VPRIV void x64_restore(codegen_context *e, uint32_t sv);
VPRIV int x64_ftreg(codegen_context *e);
VPRIV void x64_callee_save_use(codegen_context *e, int r);	/* a callee-saved register used: the prologue saves it */
VPRIV record *x64_record_emit(codegen_context *e, int op);
VPRIV void x64_two_address_convert(codegen_context *e);
VPRIV bool x64_mulconst(codegen_context *e, int size, int r, i128 rhs);

VPRIV bool x64_tail_return(codegen_context *e, uint32_t expr);
VPRIV uint32_t x64_tail_ref(const codegen_context *e);
VPRIV bool x64_framefree_candidate(codegen_context *e);
VPRIV bool x64_framefree_record(codegen_context *e, const record *i);
VPRIV bool x64_copy_forward_at(codegen_context *e, uint32_t k);
VPRIV bool x64_last_u32(const codegen_context *e, int r);
VPRIV void x64_dead(codegen_context *e);
VPRIV void x64_dead_budget(codegen_context *e, uint32_t budget);	/* internal bounded-fallback contracts */
VPRIV void x64_locals_begin(codegen_context *e, uint32_t s);
VPRIV bool x64_local(codegen_context *e, uint32_t s, codegen_lvalue *l);
/* effects.c: logical records before or after two-address expansion. */
VPRIV const uint8_t *x64_flow_table(void);
/* Prepared for shared copy/liveness passes; also exercised by contract tests. */
VPRIV __attribute__((unused)) record_effects x64_record_effects(const codegen_context *e, const record *i);
VPRIV void x64_function_print(codegen_context *e, tbuf *b, uint32_t s);
VPRIV void x64_x87(codegen_context *e, int xf, const codegen_lvalue *l, int sz);

VPRIV void x64_label_name_append(tbuf *b, uint32_t s, uint32_t l);
VPRIV void x64_register_name_append(tbuf *b, int r, int sz);
VPRIV int x64_tlsaddr(codegen_context *e, uint32_t ref, int model);
VPRIV bool codegen_symbol_needs_got(cc *c, uint32_t ref);	/* arch/a64/codegen/value.c (shared) */
VPRIV int x64_memory_base_register(codegen_context *e, const codegen_lvalue *l, uint32_t *ref);
VPRIV bool x64_target_has_no_simd(codegen_context *e);
VPRIV void x64_nofp(codegen_context *e);
VPRIV void x64_x87(codegen_context *e, int xf, const codegen_lvalue *l, int sz);	/* an x87 record (fp.c) */	/* fatal under -mgeneral-regs-only / -mno-sse */

#endif
