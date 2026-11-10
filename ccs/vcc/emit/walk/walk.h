/*
 * vcc code generator: the AST walker (PLAN-x86.md, layer 1) and the
 * lowering interface it calls (layer 2).
 *
 * The walker makes the decisions at C level, once for every CPU that uses
 * it: what a node means, constant folding of the values it builds, frame
 * slots for locals, labels and the loop stack, the call sequence, switch
 * strategy.  What an operation becomes is the CPU's: every call into it
 * goes through LO(name), today x64_name (arch/x64/emit/); a second CPU
 * instantiates the walker with its own prefix.
 *
 * Values are the emitter's val (constant, address, physical register);
 * lvalues its lval (frame slot, symbol, register base).  Frame slots are
 * offsets from the frame base, negative for locals.
 *
 * Part of the emit unity TU, next to arch/a64/emit/: every name here
 * starts with wk_.  The reference helpers (note_ref, walk_fn, out_chunk,
 * sym_header, is_local_var, ...) are arm64's files' for now; they are
 * target-independent and move to emit/ with the arm64 migration.
 */
#ifndef VCC_WALK_H
#define VCC_WALK_H
#include "../emitimpl.h"
#include "../rec/rec.h"

#define LO(name) x64_##name

/* ---------------- walker state (one per ecx) ---------------- */

#define WK_MAXLOOP 1024
#define WK_MAXCASE (1u << 16)

typedef struct wkloop { uint32_t brk, cont; } wkloop;
typedef struct wkcase { i128 lo, hi; uint32_t node; } wkcase;

typedef struct wkst {
	int32_t foff;		/* bytes of locals below the frame base so far */
	uint32_t falign;
	uint32_t nlabel;	/* local labels handed out in this function (1..) */
	uint32_t gen;		/* bindings below are valid when their gen is this */
	uint32_t *sgen;		/* per sym */
	int32_t *soff;		/* per sym: frame offset */
	uint32_t *lgen, *ulbl;	/* per user label: local label */
	uint32_t *clbl;		/* per node (N_CASE/N_DEFAULT): local label, valid when cgen == gen */
	uint32_t *cgen;
	wkloop loop[WK_MAXLOOP];
	int nloop;
	wkcase *cases;
	uint32_t ncases;
	uint32_t retlbl;
	qty rty;
	uint32_t fsym;
} wkst;

VPRIV wkst *wk_state(ecx *e);
VPRIV void wk_function(ecx *e, uint32_t s);	/* emit/walk/func.c: what emit/tu.c calls for x86-64 */
VPRIV int32_t wk_slot(ecx *e, uint64_t size, uint32_t align);
VPRIV int32_t wk_local(ecx *e, uint32_t s);	/* a local's slot, made on first use (a goto may reach a use first) */
VPRIV uint32_t wk_newlabel(ecx *e);
VPRIV val wk_gen(ecx *e, uint32_t n);
VPRIV lval wk_lval(ecx *e, uint32_t n);
VPRIV int wk_reg(ecx *e, val *v, qty t);
VPRIV void wk_free(ecx *e, const val *v);
VPRIV void wk_cond(ecx *e, uint32_t n, bool jump_if, uint32_t lbl);
VPRIV void wk_stmt(ecx *e, uint32_t n);
VPRIV void wk_init(ecx *e, const lval *dst, qty t, uint32_t init);	/* an object initialized by init (0: none) */
VPRIV int wk_size(cc *c, qty t);
VPRIV void ctor_entry(cc *c, tbuf *b, uint32_t s, bool fini);	/* arch/a64/emit/func.c (shared) */
VPRIV bool wk_signed(cc *c, qty t);

/* ---------------- the lowering interface (layer 2) ---------------- */

/* registers */
VPRIV int LO(treg)(ecx *e);				/* a free temporary */
VPRIV void LO(tfree)(ecx *e, int r);
VPRIV void LO(tkeep)(ecx *e, int r);			/* r in use again (an operation consumed it, the walker still holds it) */
VPRIV int LO(nfree)(ecx *e);				/* free temporaries left */
VPRIV int LO(toreg)(ecx *e, val *v, int size);	/* a constant or address into a temporary: v becomes V_REG */
/* memory: size 1, 2, 4, 8; loads extend to 64 bits by sgn */
VPRIV void LO(load)(ecx *e, int rd, const lval *l, int size, bool sgn);
VPRIV void LO(store)(ecx *e, int rs, const lval *l, int size);
VPRIV int LO(laddr)(ecx *e, const lval *l);		/* the address in a temporary (l's register is reused) */
VPRIV void LO(copy)(ecx *e, int rdst, int rsrc, uint64_t n);	/* n bytes; the address registers stay live */
VPRIV void LO(zero)(ecx *e, int rdst, uint64_t n);
/* integers: size 4 or 8 (the operation's width); results in a's register */
VPRIV val LO(bin)(ecx *e, int op, int size, bool sgn, val a, val b);	/* T_PLUS T_MINUS T_STAR T_SLASH T_PERCENT T_AMP T_PIPE T_CARET T_LESSLESS T_GREATERGREATER */
VPRIV val LO(un)(ecx *e, int op, int size, val a);			/* T_MINUS T_TILDE */
VPRIV void LO(ext)(ecx *e, int r, int from, bool sgn);		/* low `from` bytes of r extended to 64 bits */
VPRIV val LO(cmp)(ecx *e, int op, int size, bool sgn, val a, val b);	/* 0 or 1 */
VPRIV void LO(cmpbr)(ecx *e, int op, int size, bool sgn, val a, val b, uint32_t lbl);	/* to lbl if a op b */
VPRIV void LO(brz)(ecx *e, val a, int size, bool nz, uint32_t lbl);	/* to lbl if (a != 0) == nz */
/* control */
VPRIV void LO(label)(ecx *e, uint32_t l);
VPRIV void LO(jump)(ecx *e, uint32_t l);
/* calls: the arguments are in frame slots (8 bytes each, extended); ref: a direct callee, else fn */
VPRIV val LO(call)(ecx *e, uint32_t ref, val *fn, const int32_t *argslot, int nargs, bool variadic, int retsize, bool retsgn);
VPRIV int LO(nregargs)(void);				/* integer arguments passed in registers */
VPRIV int32_t LO(stackarg)(int i);			/* frame offset of incoming stack argument i (i >= nregargs) */
VPRIV void LO(param)(ecx *e, int i, int32_t slot, int size);	/* incoming register argument i to a slot */
VPRIV void LO(ret)(ecx *e, val *v, int size, bool sgn, uint32_t lbl);	/* the result (v may be NULL), then to lbl */
/* the function: before its body, and once it is generated (frame, prologue, output) */
VPRIV void LO(begin)(ecx *e, uint32_t s);
VPRIV void LO(finish)(ecx *e, uint32_t s);

#endif
