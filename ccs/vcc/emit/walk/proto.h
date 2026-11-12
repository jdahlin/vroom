/*
 * The walker's prototypes and the lowering interface (walk.h).  No include
 * guard: a second instantiation of the walker (SPEC-vir.md X1, emit/vir/)
 * includes this again with WK_PREFIX and LO_PREFIX defined, so that the
 * renamed functions are declared too.  Only prototypes and macros here.
 */
#include "prefix.h"

/* The lowering target: x64_ today.  A second instantiation of the walker
   (SPEC-vir.md X1) defines WK_PREFIX and LO_PREFIX before including the
   walker files; LO_PREFIX is then the prefix of the target's operations. */
#undef LO
#ifdef LO_PREFIX
#define LO_PASTE_(p, n) p##n
#define LO_PASTE(p, n) LO_PASTE_(p, n)	/* (the extra level expands LO_PREFIX first) */
#define LO(name) LO_PASTE(LO_PREFIX, name)
#else
#define LO(name) x64_##name
#endif

VPRIV bool wk_switch_values(ecx *e, uint32_t n);	/* (also in emitimpl.h, for emit/) */
VPRIV void wk_ecx_free(ecx *e);
VPRIV wkst *wk_state(ecx *e);
VPRIV void wk_label(ecx *e, uint32_t label);
VPRIV bool wk_isconst(ecx *e, uint32_t n);
VPRIV int wk_condfold(ecx *e, uint32_t n);
VPRIV bool wk_branch_entry(cc *c, uint32_t n);
VPRIV bool wk_const_sym(ecx *e, uint32_t s, val *v);	/* s is bound to the constant *v */
VPRIV void wk_ael_clear(ecx *e, uint32_t s);	/* UINT32_MAX: every array */
VPRIV bool wk_pk_get(ecx *e, uint32_t s, val *v);	/* a provisional constant of parameter s (wkst.pk) */
VPRIV bool wk_weak0(cc *c, val v);	/* the address of a weak undefined symbol (may be 0) */
VPRIV val wk_conv(ecx *e, val v, qty from, qty to);
VPRIV val wk_inline(ecx *e, uint32_t n, uint32_t s);	/* the call n to s, inlined (wk_can_inline) */
VPRIV bool wk_can_inline(ecx *e, uint32_t s);
VPRIV void wk_function(ecx *e, uint32_t s);	/* emit/walk/func.c: what emit/tu.c calls for x86-64 */
VPRIV int64_t wk_slot(ecx *e, uint64_t size, uint32_t align);	/* a temporary: free again after the statement */
VPRIV int64_t wk_pslot(ecx *e, uint64_t size, uint32_t align);	/* for the whole function */
VPRIV int64_t wk_pslot_kind(ecx *e, uint64_t size, uint32_t align, int kind);	/* wk_pslot, told to LO(slot) as kind (WK_SLOT_*) */
VPRIV int64_t wk_local(ecx *e, uint32_t s);	/* a local's slot, made on first use (a goto may reach a use first) */
VPRIV uint32_t wk_newlabel(ecx *e);
VPRIV val wk_gen(ecx *e, uint32_t n);
VPRIV lval wk_lval(ecx *e, uint32_t n);
VPRIV val wk_sdivpow2(ecx *e, qty t, val a, const em_sdivpow2_recipe *r);
VPRIV bool wk_udivconst_ok(ecx *e, val a, const em_udivconst_recipe *r);
VPRIV val wk_udivconst(ecx *e, qty ty, val a, const em_udivconst_recipe *r);
VPRIV int wk_reg(ecx *e, val *v, qty t);
VPRIV void wk_free(ecx *e, const val *v);
VPRIV void wk_cond(ecx *e, uint32_t n, bool jump_if, uint32_t lbl);
VPRIV void wk_stmt(ecx *e, uint32_t n);
VPRIV void wk_init(ecx *e, const lval *dst, qty t, uint32_t init);
VPRIV void wk_bfinit(ecx *e, const lval *l, qty t, uint32_t init);	/* a bit-field initialized */	/* an object initialized by init (0: none) */
VPRIV int wk_size(cc *c, qty t);
VPRIV bool wk_mem(ecx *e, qty t);	/* values of t are held by address (records, arrays, x87 long double) */
VPRIV val wk_conv(ecx *e, val v, qty from, qty to);
VPRIV void wk_store(ecx *e, const lval *l, qty t, val *v);
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
VPRIV val LO(umulhi)(ecx *e, val a, val b);	/* high64(a*b), consumes both, returns a register */
VPRIV val LO(smulhi)(ecx *e, val a, val b);	/* signed high64(a*b), same ownership as umulhi */
VPRIV bool wk_sdivconst_ok(ecx *e, val a, const em_sdivconst_recipe *r);
VPRIV val wk_sdivconst(ecx *e, qty ty, val a, const em_sdivconst_recipe *r);
VPRIV val LO(bin)(ecx *e, int op, int size, bool sgn, val a, val b);	/* T_PLUS T_MINUS T_STAR T_SLASH T_PERCENT T_AMP T_PIPE T_CARET T_LESSLESS T_GREATERGREATER */
VPRIV void LO(switch_bitset)(ecx *e, int r, int size, const em_switch_bitset_recipe *recipe,
			   uint32_t target, uint32_t dflt);
VPRIV void LO(switch_table)(ecx *e, int r, int size, const em_switch_case *cases, uint32_t n, uint32_t dflt);
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
/* Integer objects wider than 128 bits, canonical little-endian limbs. */
VPRIV val LO(bigconst)(ecx *e, qty t, const uint32_t *words, i128 small);
VPRIV val LO(bigbin)(ecx *e, int op, qty t, val a, val b);
VPRIV val LO(bigcmp)(ecx *e, int op, qty t, val a, val b);
VPRIV val LO(bigcvt)(ecx *e, val v, qty from, qty to);
VPRIV val LO(wbin)(ecx *e, int op, bool sgn, val a, val b);	/* b an int for << >> */
VPRIV val LO(wun)(ecx *e, int op, val a);			/* T_MINUS T_TILDE */
VPRIV val LO(wcmp)(ecx *e, int op, bool sgn, val a, val b);
VPRIV val LO(wcvt)(ecx *e, val v, qty from, qty to);	/* __int128 on either side */
VPRIV int LO(ftreg)(ecx *e);				/* a free float temporary */
VPRIV val LO(fconst)(ecx *e, uint64_t bits, int size);	/* a float (4) or double (8) constant */
VPRIV val LO(fbin)(ecx *e, int op, qty t, val a, val b);	/* + - * / */
VPRIV val LO(fneg)(ecx *e, qty t, val a);
VPRIV val LO(fsqrt)(ecx *e, qty t, val a);
VPRIV val LO(root4)(ecx *e, val a);
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
VPRIV void LO(classify)(ecx *e, qty t, wkarg *a, bool unnamed);
/* r: a record result (its slot receives it), NULL otherwise; retsize/retsgn: a scalar result */
VPRIV val LO(call)(ecx *e, uint32_t ref, val *fn, const wkarg *a, int n, bool variadic, const wkarg *r, qty rt);
/* Incoming arguments to their slots or selected register homes; a parameter
   left on the stack keeps the caller's location as its slot. */
VPRIV void LO(params)(ecx *e, wkarg *a, int n, const wkarg *r, bool variadic);
VPRIV bool LO(tail_return)(ecx *e, uint32_t expr);
VPRIV void LO(ret)(ecx *e, val *v, qty t, uint32_t lbl);	/* a scalar result of type t (v may be NULL), then to lbl */
VPRIV void LO(retrec)(ecx *e, int raddr, const wkarg *r, uint32_t lbl);	/* a record result from the object at raddr */
VPRIV val LO(pcmpstr)(ecx *e, uint32_t n);
VPRIV void LO(va_start)(ecx *e, int rap);			/* the va_list at rap */
VPRIV int LO(va_arg)(ecx *e, int rap, qty t);		/* the next argument's address */
/* the function: before its body, and once it is generated (frame, prologue, output) */
VPRIV void LO(begin)(ecx *e, uint32_t s);
VPRIV void LO(locals_begin)(ecx *e, uint32_t s);
VPRIV bool LO(local)(ecx *e, uint32_t s, lval *l);
#ifdef WK_PREFIX
/* VIR: a local declared without an initializer: its value is indeterminate from here (C11 6.2.4) */
VPRIV void LO(undecl)(ecx *e, const lval *l, uint64_t size);
#endif
/* The walker hands out the frame offset off (negative, from the frame base)
   for size bytes at align: fresh, or an earlier slot of the same size and
   alignment made free again.  The same offset names different objects at
   different times; this is the only place a target learns when.  Called once
   per hand-out. */
VPRIV void LO(slot)(ecx *e, int64_t off, uint64_t size, uint32_t align, int kind);
VPRIV void LO(finish)(ecx *e, uint32_t s);

