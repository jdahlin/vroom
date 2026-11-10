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
typedef struct wkarg { uint8_t n, cls[2]; bool mem, x87; uint32_t align; uint64_t size; int32_t slot; } wkarg;	/* x87: returned in st(0) */
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
	uint32_t *vgen;		/* per node (a VLA size): its value is in slot voff when vgen == gen */
	int32_t *voff;
	uint32_t *zgen;		/* per sym (a VLA): its saved sp was zeroed on entry when zgen == gen */
	wkloop loop[WK_MAXLOOP];
	int nloop;
	wkcase *cases;
	uint32_t ncases;
	uint32_t retlbl;
	qty rty;
	wkarg rrec;		/* a record result: as the ABI returns it */
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
VPRIV void wk_init(ecx *e, const lval *dst, qty t, uint32_t init);
VPRIV void wk_bfinit(ecx *e, const lval *l, qty t, uint32_t init);	/* a bit-field initialized */	/* an object initialized by init (0: none) */
VPRIV int wk_size(cc *c, qty t);
VPRIV bool wk_mem(ecx *e, qty t);	/* values of t are held by address (records, arrays, x87 long double) */
VPRIV void ctor_entry(cc *c, tbuf *b, uint32_t s, bool fini);	/* arch/a64/emit/func.c (shared) */
VPRIV bool wk_signed(cc *c, qty t);
VPRIV void wk_vla_fix(ecx *e, qty t, bool pure);	/* the VLA sizes in t evaluated now, into slots */
VPRIV val wk_vla_bytes(ecx *e, qty t);		/* sizeof t (a VLA: computed) */
VPRIV void wk_vla_decl(ecx *e, uint32_t s, bool zero);	/* a VLA's storage below sp */

/* ---------------- the lowering interface (layer 2) ---------------- */

/* registers */
VPRIV int LO(treg)(ecx *e);				/* a free temporary */
VPRIV void LO(tfree)(ecx *e, int r);
VPRIV void LO(mov)(ecx *e, int rd, int rs);			/* rd = rs (64 bits) */
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
/* floating point: float and double values in the CPU's float registers; long double as
   LO(ldmem) says (true: by address, a 16-byte object, as records) */
VPRIV bool LO(ldmem)(void);
/* __int128: by address too when LO(i128mem) */
VPRIV bool LO(i128mem)(void);
VPRIV val LO(wconst)(ecx *e, i128 v);
VPRIV val LO(wbin)(ecx *e, int op, bool sgn, val a, val b);	/* b an int for << >> */
VPRIV val LO(wun)(ecx *e, int op, val a);			/* T_MINUS T_TILDE */
VPRIV val LO(wcmp)(ecx *e, int op, bool sgn, val a, val b);
VPRIV val LO(wcvt)(ecx *e, val v, qty from, qty to);	/* __int128 on either side */
VPRIV int LO(ftreg)(ecx *e);				/* a free float temporary */
VPRIV val LO(fconst)(ecx *e, uint64_t bits, int size);	/* a float (4) or double (8) constant */
VPRIV val LO(fbin)(ecx *e, int op, qty t, val a, val b);	/* + - * / */
VPRIV val LO(fneg)(ecx *e, qty t, val a);
VPRIV val LO(fcmp)(ecx *e, int op, qty t, val a, val b);	/* 0 or 1, false when unordered (but !=) */
VPRIV val LO(cvt)(ecx *e, val v, qty from, qty to);	/* a float on either side (to may be _Bool) */
/* builtins the CPU does */
VPRIV void LO(trap)(ecx *e);				/* __builtin_trap, __builtin_unreachable */
VPRIV val LO(frame)(ecx *e, bool ret);			/* the return address (ret) or the frame address */
VPRIV val LO(bitop)(ecx *e, int bi, int size, val a);	/* BI_CLZ.. BI_BSWAP64: a nonzero where the builtin requires it */
VPRIV val LO(alloca)(ecx *e, val n);			/* n bytes on the stack, 16-aligned */
VPRIV int LO(tlsaddr)(ecx *e, uint32_t ref, int model);	/* a thread-local's address: 0 local-exec, 1 initial-exec, 2 general dynamic */
VPRIV int LO(spget)(ecx *e);				/* the stack pointer in a temporary */
VPRIV void LO(spset)(ecx *e, int r);			/* the stack pointer = r (r freed) */
VPRIV val LO(fbits)(ecx *e, val v, int size);		/* a float's bits in an integer register */
/* control */
VPRIV void LO(label)(ecx *e, uint32_t l);
VPRIV void LO(jump)(ecx *e, uint32_t l);
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
VPRIV void LO(finish)(ecx *e, uint32_t s);

#endif
