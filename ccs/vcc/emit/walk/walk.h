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

/*
 * An argument, parameter or result as the ABI passes it (LO(classify)): n
 * eightbytes of class cls[i] (WA_INT, WA_SSE), or in memory (mem), or
 * nothing (n 0, not mem: an empty record).  Its bytes are in the frame slot
 * slot (8-byte words; a scalar extended to 64 bits).
 */
enum { WA_INT, WA_SSE };
typedef struct wkarg { uint8_t n, cls[2]; bool mem, x87, up; uint32_t align; uint64_t size; int64_t slot; } wkarg;	/* x87: returned in st(0); up: one 16-byte SSE word (x86-64 SSEUP) */
/* ---------------- walker state (one per ecx) ---------------- */

#define WK_MAXLOOP 1024
#define WK_MAXCASE (1u << 20)

typedef struct wkloop { uint32_t brk, cont; int cdepth; } wkloop;	/* cdepth: the cleanups live at its entry */
#define WK_MAXCLEAN 1024
typedef struct wkcase { i128 lo, hi; uint32_t node; } wkcase;

/* an inlined call: where its returns go */
typedef struct wkinl {
	struct wkinl *up;
	uint32_t fn, endlbl, inst;
	qty rty;
	int64_t rslot;		/* the result's slot (a value held by address: its object) */
	int cbase;		/* the cleanups live at the call */
	bool any, cst_ok;	/* a return seen; every return so far the constant cst */
	val cst;
} wkinl;

typedef struct wkst {
	int64_t foff;		/* bytes of locals below the frame base so far */
	uint32_t falign;
	uint32_t nlabel;	/* local labels handed out in this function (1..) */
	uint32_t gen;		/* bindings below are valid when their gen is this */
	uint32_t *sgen;		/* per sym */
	int64_t *soff;		/* per sym: frame offset */
	uint32_t *lgen, *ulbl;	/* per user label: local label */
	uint32_t *clbl;		/* per node (N_CASE/N_DEFAULT): local label, valid when cgen == gen */
	uint32_t *cgen;
	uint32_t *vgen;		/* per node (a VLA size): its value is in slot voff when vgen == gen */
	int64_t *voff;
	uint32_t *zgen;		/* per sym (a VLA): its saved sp was zeroed on entry when zgen == gen */
	wkloop loop[WK_MAXLOOP];
	int nloop;
	wkcase *cases;
	uint32_t ncases;
	uint32_t retlbl;
	qty rty;
	wkarg rrec;		/* a record result: as the ABI returns it */
	uint32_t fsym;
	uint32_t clean[WK_MAXCLEAN];	/* variables with __attribute__((cleanup)) in scope, innermost last */
	int nclean;
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
} wkst;

VPRIV wkst *wk_state(ecx *e);
VPRIV void wk_label(ecx *e, uint32_t label);
VPRIV bool wk_isconst(ecx *e, uint32_t n);
VPRIV int wk_condfold(ecx *e, uint32_t n);
VPRIV bool wk_branch_entry(cc *c, uint32_t n);
VPRIV bool wk_const_sym(ecx *e, uint32_t s, val *v);	/* s is bound to the constant *v */
VPRIV val wk_inline(ecx *e, uint32_t n, uint32_t s);	/* the call n to s, inlined (wk_can_inline) */
VPRIV bool wk_can_inline(ecx *e, uint32_t s);
VPRIV void wk_function(ecx *e, uint32_t s);	/* emit/walk/func.c: what emit/tu.c calls for x86-64 */
VPRIV int64_t wk_slot(ecx *e, uint64_t size, uint32_t align);
VPRIV int64_t wk_local(ecx *e, uint32_t s);	/* a local's slot, made on first use (a goto may reach a use first) */
VPRIV uint32_t wk_newlabel(ecx *e);
VPRIV val wk_gen(ecx *e, uint32_t n);
VPRIV lval wk_lval(ecx *e, uint32_t n);
VPRIV val wk_sdivpow2(ecx *e, qty t, val a, const em_sdivpow2_recipe *r);
VPRIV int wk_reg(ecx *e, val *v, qty t);
VPRIV void wk_free(ecx *e, const val *v);
VPRIV void wk_cond(ecx *e, uint32_t n, bool jump_if, uint32_t lbl);
VPRIV void wk_stmt(ecx *e, uint32_t n);
VPRIV void wk_init(ecx *e, const lval *dst, qty t, uint32_t init);
VPRIV void wk_bfinit(ecx *e, const lval *l, qty t, uint32_t init);	/* a bit-field initialized */	/* an object initialized by init (0: none) */
VPRIV int wk_size(cc *c, qty t);
VPRIV bool wk_mem(ecx *e, qty t);	/* values of t are held by address (records, arrays, x87 long double) */
VPRIV val wk_conv(ecx *e, val v, qty from, qty to);
VPRIV val wk_reload(ecx *e, const lval *l, qty t);	/* l's value again (a new register or address) */
VPRIV val wk_putmem(ecx *e, lval *l, qty t, val v);	/* v's bytes (held by address) into l: l's address */
VPRIV void ctor_entry(cc *c, tbuf *b, uint32_t s, bool fini);	/* arch/a64/emit/func.c (shared) */
VPRIV bool wk_signed(cc *c, qty t);
VPRIV void wk_vla_fix(ecx *e, qty t, bool pure);	/* the VLA sizes in t evaluated now, into slots */
VPRIV val wk_vla_bytes(ecx *e, qty t);		/* sizeof t (a VLA: computed) */
VPRIV void wk_vla_decl(ecx *e, uint32_t s, bool zero);	/* a VLA's storage below sp */
VPRIV uint32_t wk_userlabel(ecx *e, uint32_t l);
VPRIV void wk_put(ecx *e, const lval *l, qty t, int r);
VPRIV void wk_goto(ecx *e, uint32_t label);
VPRIV void wk_cleanups(ecx *e, int to);	/* the cleanups above depth `to` run, innermost first (not popped) */

/* ---------------- the lowering interface (layer 2) ---------------- */

/* registers */
VPRIV int LO(treg)(ecx *e);				/* a free temporary */
VPRIV void LO(tfree)(ecx *e, int r);
VPRIV void LO(mov)(ecx *e, int rd, int rs);			/* rd = rs (64 bits) */
VPRIV void LO(tkeep)(ecx *e, int r);			/* r in use again (an operation consumed it, the walker still holds it) */
VPRIV int LO(nfree)(ecx *e);				/* free temporaries left */
VPRIV int LO(nffree)(ecx *e);				/* free float temporaries left */
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
/* floating point: float and double values in the CPU's float registers; long double as
   LO(ldmem) says (true: by address, a 16-byte object, as records) */
VPRIV bool LO(ldmem)(void);
/* __int128: by address too when LO(i128mem) */
VPRIV bool LO(i128mem)(void);
VPRIV val LO(wconst)(ecx *e, i128 v);
VPRIV val LO(wbitint)(ecx *e, val v, unsigned bits, bool sgn);
VPRIV val LO(wbin)(ecx *e, int op, bool sgn, val a, val b);	/* b an int for << >> */
VPRIV val LO(wun)(ecx *e, int op, val a);			/* T_MINUS T_TILDE */
VPRIV val LO(wcmp)(ecx *e, int op, bool sgn, val a, val b);
VPRIV val LO(wcvt)(ecx *e, val v, qty from, qty to);	/* __int128 on either side */
VPRIV int LO(ftreg)(ecx *e);				/* a free float temporary */
VPRIV val LO(fconst)(ecx *e, uint64_t bits, int size);	/* a float (4) or double (8) constant */
VPRIV val LO(fbin)(ecx *e, int op, qty t, val a, val b);	/* + - * / */
VPRIV val LO(fneg)(ecx *e, qty t, val a);
enum { WK_FCMP_UNORDERED = 0x10000, WK_FCMP_ORDERED_NE };
VPRIV val LO(fcmp)(ecx *e, int op, qty t, val a, val b);	/* 0 or 1, false when unordered (but !=) */
VPRIV val LO(cvt)(ecx *e, val v, qty from, qty to);	/* a float on either side (to may be _Bool) */
/* builtins the CPU does */
VPRIV void LO(trap)(ecx *e);				/* __builtin_trap, __builtin_unreachable */
VPRIV void LO(asm)(ecx *e, uint32_t n);
VPRIV void LO(barrier)(ecx *e);			/* compiler barrier: no machine instruction */
VPRIV val LO(setjmp)(ecx *e, int rbuf);
VPRIV void LO(longjmp)(ecx *e, int rbuf);
VPRIV val LO(frame)(ecx *e, bool ret, uint32_t level);			/* the return address (ret) or the frame address */
VPRIV val LO(bitop)(ecx *e, int bi, int size, val a);	/* BI_CLZ.. BI_BSWAP64: a nonzero where the builtin requires it */
VPRIV val LO(alloca)(ecx *e, val n);			/* n bytes on the stack, 16-aligned */
VPRIV int LO(tlsaddr)(ecx *e, uint32_t ref, int model);	/* a thread-local's address: 0 local-exec, 1 initial-exec, 2 general dynamic */
/* atomics at [rp], size 1, 2, 4, 8, sequentially consistent; the operand registers stay
   the caller's; a result is a new temporary, zero-extended:
   LOAD -> value; STORE rv; XCHG / XADD rv -> the old value; CAS: [rp] = rv if it holds
   rexp -> the old value; FENCE */
enum { WK_ALOAD, WK_ASTORE, WK_AXCHG, WK_AXADD, WK_ACAS, WK_AFENCE };
VPRIV int LO(atomic)(ecx *e, int op, int size, int rp, int rv, int rexp);
VPRIV int LO(spget)(ecx *e);				/* the stack pointer in a temporary */
VPRIV void LO(spset)(ecx *e, int r);			/* the stack pointer = r (r freed) */
VPRIV val LO(fbits)(ecx *e, val v, int size);		/* a float's bits in an integer register */
/* control */
VPRIV void LO(dbgrow)(ecx *e, uint32_t loc);	/* -g: line row at the next instruction */
VPRIV void LO(label)(ecx *e, uint32_t l);
VPRIV void LO(jump)(ecx *e, uint32_t l);
VPRIV void LO(jumpr)(ecx *e, int r);			/* to the address in r (computed goto) */
VPRIV void LO(named)(ecx *e, uint32_t ref);		/* a label with a symbol name here (ref: RK_LBL, an address taken) */
/* calls: the arguments are in frame slots (8 bytes each, extended); ref: a direct callee, else fn */
VPRIV void LO(classify)(ecx *e, qty t, wkarg *a);
/* r: a record result (its slot receives it), NULL otherwise; retsize/retsgn: a scalar result */
VPRIV val LO(call)(ecx *e, uint32_t ref, val *fn, const wkarg *a, int n, bool variadic, const wkarg *r, qty rt);
/* the incoming arguments to their slots; a parameter the caller left on the stack gets that place as its slot */
VPRIV void LO(params)(ecx *e, wkarg *a, int n, const wkarg *r, bool variadic);
VPRIV void LO(ret)(ecx *e, val *v, qty t, uint32_t lbl);	/* a scalar result of type t (v may be NULL), then to lbl */
VPRIV void LO(retrec)(ecx *e, int raddr, const wkarg *r, uint32_t lbl);	/* a record result from the object at raddr */
VPRIV void LO(va_start)(ecx *e, int rap);			/* the va_list at rap */
VPRIV int LO(va_arg)(ecx *e, int rap, qty t);		/* the next argument's address */
/* the function: before its body, and once it is generated (frame, prologue, output) */
VPRIV void LO(begin)(ecx *e, uint32_t s);
VPRIV void LO(locals_begin)(ecx *e, uint32_t s);
VPRIV void LO(locals_params)(ecx *e);
VPRIV void LO(locals_restore)(ecx *e);
VPRIV bool LO(local)(ecx *e, uint32_t s, lval *l);
VPRIV void LO(finish)(ecx *e, uint32_t s);

#endif
