/*
 * vcc code generator: the AST walker (PLAN-x86.md, layer 1) and the
 * lowering interface it calls (layer 2).
 *
 * The walker makes the decisions at C level, once for every target that uses
 * it: what a node means, constant folding of the values it builds, frame
 * slots for locals, labels and the loop stack, the call sequence, switch
 * strategy.  What an operation becomes is the target's: every call into it
 * goes through LO(name), x64_name (arch/x64/codegen/) in the first
 * instantiation and vw_name, the VIR builder, in the second, which
 * codegen/vir/walk.c compiles with WK_PREFIX and LO_PREFIX defined.
 *
 * Values are the emitter's val (constant, address, physical register);
 * lvalues its lval (frame slot, symbol, register base).  Frame slots are
 * offsets from the frame base, negative for locals.
 *
 * Part of the codegen unity TU, next to arch/a64/codegen/: every name here
 * starts with wk_ (vw_wk_ in the VIR instantiation).  The helpers it shares
 * with the other backends (note_ref, walk_fn, out_chunk, sym_header,
 * is_local_var, ...) are in codegen/ (references.c, function-output.c, ...).
 */
#ifndef VCC_WALK_H
#define VCC_WALK_H
#include "prefix.h"	/* generated (src/tools/gen-walkprefix.py): WK_PREFIX renames the walker's functions; first, so that emitimpl.h's wk_ prototypes are renamed too */
#include "../emitimpl.h"
#include "../rec/rec.h"

/*
 * An argument, parameter or result as the ABI passes it (LO(classify)): n
 * eightbytes of class cls[i] (WA_INT, WA_SSE), or in memory (mem), or
 * nothing (n 0, not mem: an empty record).  Its bytes are in the frame slot
 * slot (8-byte words; a scalar extended to 64 bits).
 */
enum { WA_INT, WA_SSE };
typedef struct wkarg { uint8_t n, cls[2]; bool mem, x87, up, vector, wide_record, unnamed; uint32_t align, blanes; uint64_t size; int64_t slot; } wkarg;	/* x87: returned in st(0); up: one 16-byte SSE word (x86-64 SSEUP) */
/* ---------------- walker state (one per ecx) ---------------- */

#define WK_MAXLOOP 1024
#define WK_MAXCASE (1u << 20)

typedef struct wkloop { uint32_t brk, cont; int cdepth; } wkloop;	/* cdepth: the cleanups live at its entry */
#define WK_MAXCLEAN 1024
typedef struct wkcase { i128 lo, hi; uint32_t node; } wkcase;

/* an inlined call: where its returns go */
typedef struct wkael { uint32_t s, gen, inst; int64_t idx; i128 v; } wkael;	/* a[K] = constant (wkst.ael) */

typedef struct wkinl {
	struct wkinl *up;
	uint32_t fn, endlbl, inst, call;
	ast_qualified_type rty;
	int64_t rslot;		/* the result's slot (a value held by address: its object) */
	int cbase;		/* the cleanups live at the call */
	int vbase;		/* caller VLA saves must outlive callee alloca */
	bool any, cst_ok;	/* a return seen; every return so far the constant cst */
	val cst;
	wkael ael0[16];		/* the caller's known array elements at the call (restored after the copy) */
	int nael0;
} wkinl;

#define WK_MAXTSLOT 1024

typedef struct wkst {
	int64_t foff;		/* bytes of locals below the frame base so far */
	uint32_t falign;
	/* Temporary slots: one handed out inside a statement is dead when the
	   statement ends (statement expressions give their value outside the
	   inner statements), so later statements reuse it.  tu: those of the
	   statements being walked (a stack, marked per statement); tf: free. */
	struct wktslot { int64_t off; uint32_t size, align; } tu[WK_MAXTSLOT], tf[WK_MAXTSLOT];
	uint32_t ntu, ntf, sdepth;
	/* the locals of the inline copies being walked: free (and unbound) when
	   the copy ends; inlocal: inside a copy's parameters or body */
	struct wklslot { int64_t off; uint32_t size, align, sym; } lu[WK_MAXTSLOT];
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
	wkloop loop[WK_MAXLOOP];
	int nloop;
	wkcase *cases;
	uint32_t ncases;
	uint32_t retlbl;
	ast_qualified_type rty;
	wkarg rrec;		/* a record result: as the ABI returns it */
	uint32_t fsym;
	uint32_t clean[WK_MAXCLEAN];	/* variables with __attribute__((cleanup)) in scope, innermost last */
	int nclean;
	uint32_t vla[WK_MAXCLEAN];	/* active VLA restore points, lowered by explicit alloca */
	int nvla;
	/* the cleanup scopes as a tree (wk_scan): node k (1..) is a cleanup variable and the one
	   enclosing it; per sym its node, per user label the innermost node in scope there */
	struct { uint32_t sym, parent; } cn[WK_MAXCLEAN];
	uint32_t ncn;
	uint32_t *csn, *csg, *lcn, *lcg;
	/* Constants for locals or parameters never written, bound per sym when kgen == gen. */
	uint32_t *kgen;
	val *kval;
	/* inlining: the copy being generated (0: the function itself), labels per copy */
	uint32_t inst, ninst, *linst, *cinst;
	struct wkinl *inl;	/* the innermost inlined call, NULL in the function itself */
	int depth;
	bool cjump;	/* wk_cond jumped whatever the condition (AST_NODE_IF: the branch it skips is dead) */
	/* a[K] = constant for local arrays used only as a[CONST] (no SA_AESC): straight-line
	   code only (a label drops them all), as arm64's ael */
	wkael ael[16];
	int nael;
	/* a parameter of an inlined copy that is written later, bound to its constant argument
	   until its first write or a label: only an asm "i" operand reads it (rip_rel_ptr) */
	struct { uint32_t s, gen, inst; val v; } pk[16];
	int npk;
	/* -fstack-protector*: the canary's slot (made first: above every local), and whether this
	   function needs it (an array, an aggregate or an address-taken local; alloca) */
	int32_t canary;
	/* a switch on a constant: only its selected case's label is placed (swon, swsel) */
	bool swon;
	uint32_t swsel;
	bool spneed;
} wkst;

enum { WK_FCMP_UNORDERED = 0x10000, WK_FCMP_ORDERED_NE };
enum { WK_ALOAD, WK_ASTORE, WK_AXCHG, WK_AXADD, WK_ACAS, WK_AFENCE };
enum { WK_SLOT_TEMP, WK_SLOT_LOCAL, WK_SLOT_PARAM, WK_SLOT_FUNC };	/* WK_SLOT_FUNC: lives for the whole function */
#endif

/* the prototypes: again for every instantiation of the walker (proto.h has no guard) */
#include "proto.h"
