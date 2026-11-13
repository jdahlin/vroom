/* Internal header of the arm64 lowering of VIR (src/arch/a64/vir/): the lowering state and the
 * helpers and prototypes its files share.  One unity TU with codegen, or file by file. */
#ifndef VCC_ARCH_A64_VIR_LOWERING_INTERNAL_H
#define VCC_ARCH_A64_VIR_LOWERING_INTERNAL_H

#include "../../../codegen/emitimpl.h"
#include "../../../codegen/vir/preir.h"
#include "../../../codegen/vir/lowering-driver.h"
#include "vir/layout.h"

/* data several files read: static in the unity TU, defined once (compares.c, function.c) otherwise */
#ifdef VCC_UNITY
#define A64_VIR_DATA static
#else
#define A64_VIR_DATA extern
#endif

#define A64_VIR_OPERAND_MAX 512

typedef struct a64_vir_argument { uint8_t reg; uint8_t size; int32_t stk; } a64_vir_argument;	/* reg VIR_REGISTER_NONE: at stk */

typedef struct a64_vir_context {
	ecx *e;
	vir_function *f;
	const vir_alloc_out *o;
	int64_t *soff;		/* per VIR slot: its FRAME offset */
	int64_t *spoff;		/* per spill slot */
	uint32_t *lbl;		/* per block: its label */
	uint32_t *chs;		/* per block: where a branch to it ends up (a64_vir_block_chase) */
	uint32_t *ord, *opos;	/* the layout: the blocks in the order they are emitted, and each block's place in it (a64_vir_blocks_order) */
	const vir_loops *lp;
	uint32_t sp;		/* shrink-wrapping: the save point (vir_alloc_out.save_point), VIR_BLOCK_NONE none */
	bool bare;		/* the code being emitted runs in front of it: returns without an epilogue */
	uint8_t *uses;
	uint8_t *lo32;		/* per value: 1 if every use is a trunc or a ret32 return (only the low 32 bits are read) */
	uint32_t retlbl;
	uint32_t ov[A64_VIR_OPERAND_MAX];	/* the instruction's value operands (vir_operand_iterator order, no multi) */
	uint8_t opr[A64_VIR_OPERAND_MAX];	/* ... and their registers */
	uint32_t nop;
	int fcc;		/* a compare's condition the next instruction reads from the flags, -1 none */
	uint32_t fval;		/* ... that compare */
	int fzr, fzx;		/* ... a test of register fzr (fzx: 64-bit) against 0 instead: cbz, cbnz (-1 none) */
	int fbit;		/* ... of its bit fbit instead (fzr's register): tbz, tbnz (-1 none) */
	uint32_t tlo, thi;	/* (tlo, thi): instructions folded into a branch (a64_vir_test_branch_fold) that emit nothing */
	uint32_t cclo, cchi;	/* [cclo, cchi]: compares joined by and/or into one condition (cmp, ccmp ...); cchi 0 none */
	int ccrun;		/* ... the condition the flags hold so far */
	uint32_t pval;		/* a ptradd the next load or store addresses through (0 none) */
	int pbase;		/* ... its base register */
	int64_t poff;		/* ... and offset */
	uint32_t xval;		/* a ptradd the next load or store addresses through as [base, index] (0 none) */
	uint32_t xfirst, xc;	/* the chain a64_vir_chain_fold folds: its first instruction, its consumer (xc VIR_BLOCK_NONE none) */
	struct a64_vir_chain { uint32_t first, xv; uint8_t kind, opt, sh; int64_t k; } xx;	/* ... a64_vir_chain_info_get's answer */
	int xbase, xreg;	/* ... its base register, the index's */
	uint8_t xopt, xsh;	/* ... the index's extension (2 uxtw, 3 lsl, 6 sxtw), scaled by the access size */
	uint32_t ei, eend;	/* the block's edits: the next one, the end */
	uint32_t eb;		/* ... the first */
	bool risk;		/* x17 held data across a frame access: the frame must stay small */
	bool tail;		/* a call right before a ret may be a sibling call (a64_vir_tail_call_reason) */
	bool ret32;		/* the function returns a 32-bit integer: the upper half of x0 is unspecified */
	int64_t fcs[8];		/* the FRAME offsets where d8-d15 are kept (those in o->callee[1]) */
	uint32_t skip;		/* an instruction not to lower (the ret after a sibling call), 0 none */
	uint32_t at;		/* the block whose place in the layout the code is at (a copy of another block's: vl_dup) */
	int dup;		/* ... copies being lowered, one in another: vir_layout_duplicate */
	bool fallok;		/* the code may fall through to the block after at (no edge moves follow) */
	struct { uint32_t l, e, to; } tr[1024];	/* moves of a branch's edges, after the block */
	uint32_t ntr;
	virbuild_lowering drv;	/* the shared driver's inputs (codegen/vir/lowering-driver.h) */
} a64_vir_context;

struct a64_vir_thread_state { int64_t *soff, *spoff; uint32_t *lbl, *chs, *path, *ord, *opos, *hl, *lbf; uint8_t *nf; uint32_t co, cop, chl, cnf, clf; uint8_t *uses, *lo32; uint32_t cs, csp, cl, cch, cpa, cu, c32; };

static inline bool a64_vir_value_is_constant(const vir_function *f, uint32_t v) { return v && v < f->constant_count; }

static inline int64_t a64_vir_constant_bits(const vir_insn *k) { return (int64_t)((uint64_t)k->c << 32 | k->b); }

/* an integer constant's value as the operation's width sees it */
static inline int64_t a64_vir_constant_integer(const vir_insn *k) { return k->type == VIR_TYPE_I32 ? (int64_t)(int32_t)k->b : a64_vir_constant_bits(k); }

/* ... as the bits of a logical immediate (i32: the low 32) */
static inline uint64_t a64_vir_constant_logical_immediate(const vir_insn *k) { return k->type == VIR_TYPE_I32 ? (uint64_t)k->b : (uint64_t)a64_vir_constant_bits(k); }

static inline int a64_vir_constant_log2(const vir_insn *k)	/* log2 of a positive power of two, else -1 */
{
	uint64_t v = k->type == VIR_TYPE_I32 ? (uint64_t)k->b : (uint64_t)a64_vir_constant_bits(k);

	return v && !(v & (v - 1)) ? __builtin_ctzll(v) : -1;
}

/* a multiplier opt.c's fmulk turns into neg, add or sub with a shift (then a shift): -1,
 * (2^n + 1) << s, 2^n - 1; the constant is materialized next to the madd, where it sees it */
static inline bool a64_vir_constant_is_multiplier(const vir_insn *k)
{
	int w = k->type == VIR_TYPE_I32 ? 32 : 64;
	uint64_t m = w == 64 ? ~0ull : 0xffffffffull, c = (k->type == VIR_TYPE_I32 ? (uint64_t)k->b : (uint64_t)a64_vir_constant_bits(k)) & m;
	int sh;

	if (!c || c == m)
		return c != 0;
	sh = __builtin_ctzll(c);
	c >>= sh;
	return (c > 1 && !((c - 1) & (c - 2)) && sh < w) || (!sh && c > 3 && !(c & (c + 1)) && 64 - __builtin_clzll(c) < w);
}

static inline bool a64_vir_value_is_integer_constant(const vir_function *f, uint32_t v) { return a64_vir_value_is_constant(f, v) && f->insns[v].op == VIR_OP_ICONST; }

static inline bool a64_vir_value_is_zero(const vir_function *f, uint32_t v)
{
	return a64_vir_value_is_constant(f, v) && (f->insns[v].op == VIR_OP_ICONST || f->insns[v].op == VIR_OP_FCONST) && !f->insns[v].b && !f->insns[v].c;
}

static inline bool a64_vir_value_is_memory_address(const vir_function *f, uint32_t v)	/* an address a load or store uses directly */
{
	return a64_vir_value_is_constant(f, v) && (f->insns[v].op == VIR_OP_SLOTADDR || f->insns[v].op == VIR_OP_SYMADDR);
}

static inline int a64_vir_value_is_64_bit(const vir_function *f, uint32_t v) { return f->insns[v].type != VIR_TYPE_I32; }	/* 64-bit */

/* a load or store of a vector: 16 bytes through a base register, no folded address */
static inline bool a64_vir_access_is_vector(const vir_function *f, const vir_insn *m)
{
	return vir_type_is_vector(m->op == VIR_OP_LOAD ? m->type : m->op == VIR_OP_STORE ? f->insns[m->b].type : VIR_TYPE_VOID);
}

static inline int a64_vir_general_register(uint8_t r) { return r == VIR_REGISTER_SCRATCH0 ? R_X16 : (int)VIR_REGISTER_NUMBER(r); }

static inline int a64_vir_vector_register(uint8_t r) { return r == VIR_REGISTER_SCRATCH1 ? 31 : (int)VIR_REGISTER_NUMBER(r); }

static inline bool a64_vir_register_is_float(uint8_t r) { return r == VIR_REGISTER_SCRATCH1 || (r != VIR_REGISTER_SCRATCH0 && r < 128 && VIR_REGISTER_CLASS(r)); }

static inline int a64_vir_slot_size_log2(const a64_vir_context *L, uint32_t s) { return L->o->slot_sizes[s] == 4 ? 2 : 3; }

/* a branch's condition: flags (r < 0) or register r against zero (cc EQ or NE) */
typedef struct a64_vir_condition { int cc, r, x, bit; } a64_vir_condition;	/* bit >= 0: of r (tbz, tbnz) */

/* a folded value: used once, by the next instruction, never in a slot */
static inline bool a64_vir_value_is_folded(const a64_vir_context *L, uint32_t v) { return L->uses[v] == 1 && L->o->slot[v] == VIR_BLOCK_NONE; }

typedef struct a64_vir_chain a64_vir_chain;

A64_VIR_DATA _Thread_local struct a64_vir_thread_state a64_vir_thread_state_instance;
A64_VIR_DATA const uint8_t a64_vir_integer_condition_codes[10];
A64_VIR_DATA const int8_t a64_vir_float_condition_codes[8];
A64_VIR_DATA const uint8_t a64_vir_condition_swapped[10];

VPRIV __attribute__((noreturn, cold)) void a64_vir_fail(a64_vir_context *L, const char *why);
VPRIV void *a64_vir_array_grow(void *p, uint32_t *cap, uint32_t n, size_t sz);
VPRIV uint32_t a64_vir_arguments_assign(bool darwin, const uint32_t *w, a64_vir_argument *out, uint32_t max, int32_t *stk);
VPRIV bool a64_vir_insn_is_big_memory(const vir_function *f, const vir_insn *in);
VPRIV void a64_vir_registers_describe(vir_target_registers *r, bool darwin, bool fcs);
VPRIV int a64_vir_operand_general_register(a64_vir_context *L, uint32_t k, int t);
VPRIV int a64_vir_operand_float_register(a64_vir_context *L, uint32_t k);
VPRIV void a64_vir_register_spill(a64_vir_context *L, uint8_t src, uint32_t s, int t);
VPRIV void vl_edit(a64_vir_context *L, const vir_edit *x);
VPRIV void a64_vir_edge_moves_emit(a64_vir_context *L, uint32_t g);
VPRIV uint32_t a64_vir_block_label(a64_vir_context *L, uint32_t t);
VPRIV bool a64_vir_block_is_skipped(a64_vir_context *L, uint32_t b);
VPRIV uint32_t a64_vir_block_fallthrough(a64_vir_context *L);
VPRIV uint32_t a64_vir_successor_label(a64_vir_context *L, uint32_t b, uint32_t k, uint32_t to);
VPRIV bool a64_vir_edge_is_plain(a64_vir_context *L, uint32_t b, uint32_t k);
VPRIV int a64_vir_integer_compare_lower(a64_vir_context *L, const vir_insn *in);
VPRIV void a64_vir_float_compare_lower(a64_vir_context *L, const vir_insn *in);
VPRIV bool a64_vir_compare_chain_lower(a64_vir_context *L, uint32_t b, uint32_t i);
VPRIV void a64_vir_compare_chained_lower(a64_vir_context *L, const vir_insn *in, bool isor);
VPRIV int a64_vir_condition_flags_get(a64_vir_context *L, const vir_insn *in);
VPRIV a64_vir_condition a64_vir_branch_condition_get(a64_vir_context *L, const vir_insn *in);
VPRIV void a64_vir_branch_conditional_emit(ecx *e, a64_vir_condition c, bool inv, uint32_t l);
VPRIV bool a64_vir_register_is_kept(const a64_vir_context *L, uint8_t r, uint32_t upto);
VPRIV bool a64_vir_test_branch_fold(a64_vir_context *L, uint32_t b, uint32_t i);
VPRIV lval a64_vir_address_get(a64_vir_context *L, uint32_t w, int t);
VPRIV void a64_vir_load_lower(a64_vir_context *L, uint32_t i, const vir_insn *in, uint8_t res);
VPRIV void a64_vir_store_lower(a64_vir_context *L, const vir_insn *in);
VPRIV void a64_vir_memory_block_lower(a64_vir_context *L, const vir_insn *in);
VPRIV const char *a64_vir_tail_call_reason(a64_vir_context *L, uint8_t *d);
VPRIV bool a64_vir_call_is_sibling(a64_vir_context *L, uint32_t b, uint32_t i);
VPRIV void a64_vir_call_lower(a64_vir_context *L, uint32_t b, uint32_t i, const vir_insn *in);
VPRIV void a64_vir_parameter_lower(a64_vir_context *L, const vir_insn *in, uint8_t res);
VPRIV void a64_vir_switch_lower(a64_vir_context *L, uint32_t b, const vir_insn *in);
VPRIV bool a64_vir_pointer_fold(a64_vir_context *L, uint32_t b, uint32_t i);
VPRIV bool a64_vir_chain_info_get(const a64_vir_context *L, uint32_t b, uint32_t c, a64_vir_chain *x);
VPRIV bool a64_vir_chain_fold(a64_vir_context *L, uint32_t b, uint32_t i, uint8_t res);
VPRIV uint32_t a64_vir_block_chase(a64_vir_context *L, uint32_t t);
VPRIV void a64_vir_callee_saved_float_save(a64_vir_context *L, bool restore, int t);
VPRIV void a64_vir_return_lower(a64_vir_context *L);
VPRIV void a64_vir_goto_emit(a64_vir_context *L, uint32_t b, uint32_t t);
VPRIV bool a64_vir_call_is_sibling_candidate(a64_vir_context *L, uint32_t b, uint32_t i);
VPRIV bool a64_vir_vector_lower(a64_vir_context *L, const vir_insn *in, uint8_t res);
VPRIV void a64_vir_insn_lower(a64_vir_context *L, uint32_t b, uint32_t i, uint8_t res);
VPRIV void a64_vir_blocks_order(a64_vir_context *L);
VPRIV void a64_vir_function_body_lower(a64_vir_context *L);

#endif
