/*
 * vcc code generator: the AST walker (PLAN-x86.md, layer 1) and the
 * lowering interface it calls (layer 2).
 *
 * The walker makes the decisions at C level, once for every target that uses
 * it: what a node means, constant folding of the values it builds, frame
 * slots for locals, labels and the loop stack, the call sequence, switch
 * strategy.  What an operation becomes is the target's: every call into it
 * goes through WALK_LOWER(name), x64_name (arch/x64/codegen/) in the first
 * instantiation and virbuild_function_name_get, the VIR builder, in the second, which
 * codegen/vir/walker-instance.c compiles with WALK_PREFIX and WALK_LOWER_PREFIX defined.
 *
 * Values are the emitter's val (constant, address, physical register);
 * lvalues its lval (frame slot, symbol, register base).  Frame slots are
 * offsets from the frame base, negative for locals.
 *
 * Part of the codegen unity TU, next to arch/a64/codegen/: every name here
 * starts with wk_ (virbuild_walk_ in the VIR instantiation).  The helpers it shares
 * with the other backends (note_ref, codegen_function_prepass_request, codegen_chunk_add, codegen_symbol_header_append,
 * codegen_symbol_is_local_variable, ...) are in codegen/ (references.c, function-output.c, ...).
 */
#ifndef VCC_CODEGEN_WALK_H
#define VCC_CODEGEN_WALK_H
#include "prefix.h"	/* generated (src/tools/gen-walkprefix.py): WALK_PREFIX renames the walker's functions; first, so that codegen-internal.h's wk_ prototypes are renamed too */
#include "../codegen-internal.h"
#include "../rec/record.h"

/*
 * An argument, parameter or result as the ABI passes it (WALK_LOWER(classify)): n
 * eightbytes of class cls[i] (WALK_ARGUMENT_INTEGER, WALK_ARGUMENT_SSE), or in memory (mem), or
 * nothing (n 0, not mem: an empty record).  Its bytes are in the frame slot
 * slot (8-byte words; a scalar extended to 64 bits).
 */
enum { WALK_ARGUMENT_INTEGER, WALK_ARGUMENT_SSE };
typedef struct walk_argument { uint8_t n, cls[2]; bool mem, x87, up, vector, wide_record, unnamed; uint32_t align, blanes; uint64_t size; int64_t slot; } walk_argument;	/* x87: returned in st(0); up: one 16-byte SSE word (x86-64 SSEUP) */
/* ---------------- walker state (one per ecx) ---------------- */

#define WALK_LOOP_MAX 1024
#define WALK_CASE_MAX (1u << 20)

typedef struct walk_loop { uint32_t brk, cont; int cdepth; } walk_loop;	/* cdepth: the cleanups live at its entry */
#define WALK_CLEANUP_MAX 1024
typedef struct walk_case { i128 lo, hi; uint32_t node; } walk_case;

/* an inlined call: where its returns go */
typedef struct walk_array_element { uint32_t s, gen, inst; int64_t idx; i128 v; } walk_array_element;	/* a[K] = constant (wkst.ael) */

typedef struct walk_inline_call {
	struct walk_inline_call *up;
	uint32_t fn, endlbl, inst, call;
	ast_qualified_type rty;
	int64_t rslot;		/* the result's slot (a value held by address: its object) */
	int cbase;		/* the cleanups live at the call */
	int vbase;		/* caller VLA saves must outlive callee alloca */
	bool any, cst_ok;	/* a return seen; every return so far the constant cst */
	codegen_value cst;
	walk_array_element ael0[16];		/* the caller's known array elements at the call (restored after the copy) */
	int nael0;
} walk_inline_call;

#define WALK_TEMPORARY_SLOT_MAX 1024

typedef struct walk_state {
	int64_t foff;		/* bytes of locals below the frame base so far */
	uint32_t falign;
	/* Temporary slots: one handed out inside a statement is dead when the
	   statement ends (statement expressions give their value outside the
	   inner statements), so later statements reuse it.  tu: those of the
	   statements being walked (a stack, marked per statement); tf: free. */
	struct walk_temporary_slot { int64_t off; uint32_t size, align; } tu[WALK_TEMPORARY_SLOT_MAX], tf[WALK_TEMPORARY_SLOT_MAX];
	uint32_t ntu, ntf, sdepth;
	/* the locals of the inline copies being walked: free (and unbound) when
	   the copy ends; inlocal: inside a copy's parameters or body */
	struct walk_local_slot_entry { int64_t off; uint32_t size, align, sym; } lu[WALK_TEMPORARY_SLOT_MAX];
	uint32_t nlu, inlocal;
	uint32_t nlabel;	/* local labels handed out in this function (1..) */
	uint32_t gen;		/* bindings below are valid when their gen is this */
	uint32_t *sgen;		/* per sym */
	int64_t *soff;		/* per sym: frame offset */
	uint32_t *lgen, *ulbl;	/* per user label: local label */
	uint32_t *clbl;		/* per node (AST_NODE_CASE/AST_NODE_DEFAULT): local label, valid when cgen == gen */
	uint32_t *cgen;
	uint32_t *vgen, *vinst;	/* per-node VLA size slot, keyed by function and inline instance */
	int64_t *voff;
	uint32_t *zgen;		/* per sym (a VLA): its saved sp was zeroed on entry when zgen == gen */
	walk_loop loop[WALK_LOOP_MAX];
	int nloop;
	walk_case *cases;
	uint32_t ncases;
	uint32_t retlbl;
	ast_qualified_type rty;
	walk_argument rrec;		/* a record result: as the ABI returns it */
	uint32_t fsym;
	uint32_t clean[WALK_CLEANUP_MAX];	/* variables with __attribute__((cleanup)) in scope, innermost last */
	int nclean;
	uint32_t vla[WALK_CLEANUP_MAX];	/* active VLA restore points, lowered by explicit alloca */
	int nvla;
	/* the cleanup scopes as a tree (wk_scan): node k (1..) is a cleanup variable and the one
	   enclosing it; per sym its node, per user label the innermost node in scope there */
	struct { uint32_t sym, parent; } cn[WALK_CLEANUP_MAX];
	uint32_t ncn;
	uint32_t *csn, *csg, *lcn, *lcg;
	/* Constants for locals or parameters never written, bound per sym when kgen == gen. */
	uint32_t *kgen;
	codegen_value *kval;
	/* inlining: the copy being generated (0: the function itself), labels per copy */
	uint32_t inst, ninst, *linst, *cinst;
	struct walk_inline_call *inl;	/* the innermost inlined call, NULL in the function itself */
	int depth;
	bool cjump;	/* walk_condition_emit jumped whatever the condition (AST_NODE_IF: the branch it skips is dead) */
	/* a[K] = constant for local arrays used only as a[CONST] (no CODEGEN_SYMBOL_ARRAY_ESCAPES): straight-line
	   code only (a label drops them all), as arm64's ael */
	walk_array_element ael[16];
	int nael;
	/* a parameter of an inlined copy that is written later, bound to its constant argument
	   until its first write or a label: only an asm "i" operand reads it (rip_rel_ptr) */
	struct { uint32_t s, gen, inst; codegen_value v; } pk[16];
	int npk;
	/* -fstack-protector*: the canary's slot (made first: above every local), and whether this
	   function needs it (an array, an aggregate or an address-taken local; alloca) */
	int32_t canary;
	/* a switch on a constant: only its selected case's label is placed (swon, swsel) */
	bool swon;
	uint32_t swsel;
	bool spneed;
} walk_state;

enum { WALK_FLOAT_COMPARE_UNORDERED = 0x10000, WALK_FLOAT_COMPARE_ORDERED_NOT_EQUAL };
enum { WALK_ATOMIC_LOAD, WALK_ATOMIC_STORE, WALK_ATOMIC_EXCHANGE, WALK_ATOMIC_ADD, WALK_ATOMIC_COMPARE_EXCHANGE, WALK_ATOMIC_FENCE };
enum { WALK_SLOT_TEMPORARY, WALK_SLOT_LOCAL, WALK_SLOT_PARAMETER, WALK_SLOT_FUNCTION };	/* WALK_SLOT_FUNCTION: lives for the whole function */
#endif

/* the prototypes: again for every instantiation of the walker (proto.h has no guard) */
#include "proto.h"
