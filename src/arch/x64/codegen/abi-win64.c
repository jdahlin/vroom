/* vcc code generator, x86-64: the Microsoft x64 calling convention (PLAN-win64.md WIN2), for
 * Windows x64 and for ms_abi functions on every x86-64 OS.  abi.c's entry points (the walker's
 * LO(classify), LO(call), ...) come here when e->msabi says the call, function or va_list is
 * Microsoft's; abi.c has System V's.
 *
 * Every argument is one 8-byte position: rcx, rdx, r8, r9 (xmm0-xmm3 for float and double) by
 * position, the rest on the stack above 32 bytes of shadow space the caller always reserves.
 * Records of 1, 2, 4 or 8 bytes travel as an integer; everything else (other records, empty ones,
 * long double, __int128, __float128, vectors) by reference to a copy the caller makes.  Results in
 * rax or xmm0; __int128 and vectors in xmm0; the rest through a hidden pointer (the first argument,
 * returned in rax).  Varargs: a floating-point argument goes in both its xmm register and the
 * integer one; the callee homes rcx..r9 to the shadow space, so va_list is a char * walking 8-byte
 * slots, reading a pointer for a by-reference type.  As clang (WinX86_64ABIInfo).
 *
 * The callee-saved set (rbx rbp rdi rsi r12-r15 xmm6-xmm15) is print.c's: its prologue saves those
 * the body writes.
 */
#include "x64impl.h"

static const uint8_t x64w_gpr[4] = { RCX, RDX, R8, R9 };

VPRIV int x64w_argreg(int i) { return x64w_gpr[i]; }

/* the classes: n 1 one 8-byte word in a register (cls WA_INT or WA_SSE); up: returned in xmm0
   (16 bytes), passed by reference; mem: by reference both ways; n 0 and not mem: nothing */
VPRIV void x64w_classify(ecx *e, qty t, wkarg *a, bool unnamed)
{
	cc *c = e->c;
	uint64_t size = tsize(c, t);
	int k = KIND(c, t);

	memset(a, 0, sizeof *a);
	a->size = size;
	a->unnamed = unnamed;
	a->align = talign(c, t) > 8 ? talign(c, t) : 8;
	a->vector = is_vec(c, t);
	if (is_record(c, t) || is_array(c, t)) {
		/* "Any argument that doesn't fit in 8 bytes, or is not 1, 2, 4, or 8 bytes, must be
		   passed by reference" (an empty record too) */
		if (size == 1 || size == 2 || size == 4 || size == 8)
			a->n = 1, a->cls[0] = WA_INT;
		else
			a->mem = true;
		return;
	}
	if (is_vec(c, t)) {
		if (size <= 16)
			a->n = 1, a->cls[0] = WA_SSE, a->up = true;	/* by reference, returned in xmm0 */
		else
			a->mem = true;
		return;
	}
	if (em_bigint(c, t)) {
		a->mem = true;
		return;
	}
	if (k == TY_INT128 || k == TY_UINT128) {
		a->n = 1, a->cls[0] = WA_SSE, a->up = true;	/* MinGW: returned in xmm0 */
		return;
	}
	if (k == TY_LDOUBLE || k == TY_FLOAT128 || size > 8) {
		a->mem = true;
		return;
	}
	a->n = 1;
	a->cls[0] = is_flt(c, t) ? WA_SSE : WA_INT;
	if (x64_nosimd(e) && a->cls[0] == WA_SSE)
		x64_nofp(e);
}

/* passed as a pointer to the caller's copy */
static bool x64w_byref(const wkarg *a) { return a->mem || a->up; }

static lval x64w_fr(int64_t off) { return (lval){ .k = L_FRAME, .off = off, .align = 8 }; }

/* the live temporaries a Microsoft callee does not preserve, pushed (rsi and rdi survive, and the
   float temporaries xmm8-xmm14 are among xmm6-xmm15) */
static uint32_t x64w_save(ecx *e)
{
	xst *X = e->x64;
	uint32_t saved = X->live & X_TEMPS & ~((1u << RSI) | (1u << RDI));

	for (int q = 0; q < 16; q++)
		if (saved & (1u << q)) {
			x64_op3(e, X_PUSH, 8, XNOREG, q, XNOREG);
			X->pushes++;
		}
	return saved;
}

static void x64w_restore(ecx *e, uint32_t saved)
{
	xst *X = e->x64;

	for (int q = 15; q >= 0; q--)
		if (saved & (1u << q)) {
			x64_op3(e, X_POP, 8, q, XNOREG, XNOREG);
			X->pushes--;
		}
}

/* the outgoing area: max(4, n) words, rsp 16-aligned at the call; its size */
static int64_t x64w_outgoing(ecx *e, int n)
{
	xst *X = e->x64;
	int64_t total = 8 * (int64_t)(n > 4 ? n : 4);

	if ((X->pushes * 8 + total) % 16)
		total += 8;
	x64_put(e, X_ADJSP)->imm = -total;
	X->pushes += (int)(total / 8);
	return total;
}

VPRIV val x64w_call(ecx *e, uint32_t ref, val *fn, const wkarg *a, int n, bool variadic, const wkarg *r, qty rt)
{
	cc *c = e->c;
	xst *X = e->x64;
	int retsize = KIND(c, rt) == TY_VOID ? 0 : wk_size(c, rt);
	bool retsgn = wk_signed(c, rt), retf = is_flt(c, rt) && !r;
	int first = r && r->mem ? 1 : 0;	/* the hidden result pointer takes position 0 */
	uint32_t saved = x64w_save(e);
	int64_t total = x64w_outgoing(e, first + n);
	rec *i;

	/* the stack words first: r11 is their scratch, and fn's register moves into it next */
	for (int k = 0; k < n; k++) {
		int p = first + k;
		lval s = x64w_fr(a[k].slot), d = { .k = L_REG, .r = RSP, .off = 8 * p };

		if (p < 4)
			continue;
		x64_mem(e, x64w_byref(&a[k]) ? X_LEA : X_LD, 8, R11, &s, false);
		x64_mem(e, X_ST, 8, R11, &d, false);
	}
	if (fn)
		x64_mov(e, R11, fn->r);
	if (first) {
		lval s = x64w_fr(r->slot);

		x64_mem(e, X_LEA, 8, RCX, &s, false);
	}
	for (int k = 0; k < n && first + k < 4; k++) {
		int p = first + k;
		lval s = x64w_fr(a[k].slot);

		if (x64w_byref(&a[k]) || (!a[k].n && !a[k].mem)) {
			x64_mem(e, X_LEA, 8, x64w_gpr[p], &s, false);
		} else if (a[k].cls[0] == WA_SSE) {
			x64_mem(e, X_LD, 8, XMM0 + p, &s, false);
			if (variadic)	/* a variadic callee reads it from the integer register */
				x64_mem(e, X_LD, 8, x64w_gpr[p], &s, false);
		} else {
			x64_mem(e, X_LD, 8, x64w_gpr[p], &s, false);
		}
	}
	if (fn) {
		i = x64_op3(e, X_CALLR, 8, XNOREG, R11, XNOREG);
	} else {
		i = x64_put(e, X_CALL);
		i->ref = ref;
	}
	i->sh = 1;	/* a Microsoft callee: rsi, rdi and xmm6-xmm15 survive it (print.c) */
	x64_put(e, X_ADJSP)->imm = total;
	X->pushes -= (int)(total / 8);
	if (r && !r->mem && r->n) {
		lval d = x64w_fr(r->slot);

		x64_mem(e, X_ST, r->up ? 16 : 8, r->cls[0] == WA_SSE ? XMM0 : RAX, &d, false);
	} else if (retsize && !retf && !r) {
		x64_mov(e, R11, RAX);
		x64_ext(e, R11, retsize, retsgn);
	}
	x64w_restore(e, saved);
	if (fn && fn->k == V_REG)
		x64_tfree(e, fn->r);
	if (!retsize || r)
		return vconst(0);
	if (retf) {
		int q = x64_ftreg(e);

		x64_mov(e, q, XMM0);
		return (val){ .k = V_REG, .r = (uint8_t)q };
	}
	{
		int q = x64_treg(e);

		x64_mov(e, q, R11);
		return (val){ .k = V_REG, .r = (uint8_t)q };
	}
}

/*
 * The incoming side.  Position p's word is in its register (p < 4) or at [rbp + 16 + 8p]; the
 * registers are stored first (a copy of a by-reference argument may need rcx, rsi, rdi), then the
 * by-reference objects copied into the parameters' slots.
 */
VPRIV void x64w_params(ecx *e, wkarg *a, int n, const wkarg *r, bool variadic)
{
	xst *X = e->x64;
	cc *c = e->c;
	const node *body = N(c->syms[e->wk->fsym].body);
	int first = r && r->mem ? 1 : 0;
	int64_t ptr[4] = { 0 };	/* a by-reference parameter's pointer, parked in a slot */

	X->interrupt = 0;
	X->sret = 0;
	X->save = 0;
	if (variadic)	/* the register words to the shadow space: va_arg reads them all from there
			   (a float's too: a variadic call passes it in both registers) */
		for (int p = 0; p < 4; p++) {
			lval d = x64w_fr(16 + 8 * p);

			x64_mem(e, X_ST, 8, x64w_gpr[p], &d, false);
		}
	if (first) {
		lval d = x64w_fr(r->slot);

		x64_mem(e, X_ST, 8, RCX, &d, false);
		X->sret = r->slot;
	}
	for (int k = 0; k < n; k++) {
		int p = first + k;
		uint32_t sym = c->lists[body->a + (uint32_t)k];
		qty ty = c->syms[sym].ty;
		lval home;
		bool local = x64_local(e, sym, &home);

		if (x64w_byref(&a[k])) {
			if (p < 4) {
				lval d = x64w_fr(ptr[p] = wk_slot(e, 8, 8));

				x64_mem(e, X_ST, 8, x64w_gpr[p], &d, false);
			}
			continue;
		}
		if (p < 4) {
			int from = a[k].cls[0] == WA_SSE ? XMM0 + p : x64w_gpr[p];

			if (local && from < XMM0) {
				lval v = { .k = L_VREG, .r = (uint8_t)from };

				x64_load(e, home.r, &v, wk_size(c, ty), wk_signed(c, ty));
			} else {
				lval d = x64w_fr(a[k].slot);

				x64_mem(e, X_ST, 8, from, &d, false);
			}
			continue;
		}
		/* on the stack: the parameter is where the caller put it */
		a[k].slot = 16 + 8 * p;
		if (local) {
			lval from = x64w_fr(a[k].slot);

			x64_load(e, home.r, &from, wk_size(c, ty), wk_signed(c, ty));
		}
	}
	for (int k = 0; k < n; k++) {
		int p = first + k;
		lval s, d = x64w_fr(a[k].slot);
		int rs, rd;

		if (!x64w_byref(&a[k]))
			continue;
		s = x64w_fr(p < 4 ? ptr[p] : 16 + 8 * p);
		rs = x64_treg(e);
		x64_mem(e, X_LD, 8, rs, &s, false);
		rd = x64_laddr(e, &d);
		x64_copy(e, rd, rs, a[k].size);
		x64_tfree(e, rd);
		x64_tfree(e, rs);
	}
	X->ngp = first + n, X->nfp = 0, X->stk = 0;
	if (variadic)
		X->save = 16;	/* (va_start: the named words are below the first unnamed one) */
	if (WIN64(c)) {
		/* MinGW's main calls __main first (the constructors), as clang */
		const sym *y = &c->syms[e->wk->fsym];

		if (y->sc != SC_STATIC && y->name && EM_IDENT_IS(y->name, "main")) {
			e->calls = true;
			x64w_call(e, FIXED(FN___MAIN), NULL, NULL, 0, false, NULL, prim(TY_VOID));
		}
	}
}

/* va_list (a char * variable) at rap: the first unnamed argument's word */
VPRIV void x64w_va_start(ecx *e, int rap)
{
	xst *X = e->x64;
	lval ap = { .k = L_REG, .r = (uint8_t)rap }, oa = x64w_fr(16 + 8 * X->ngp);

	if (!X->save)
		efatal(e, e->curloc, "va_start in a function that is not variadic");
	x64_mem(e, X_LEA, 8, R11, &oa, false);
	x64_mem(e, X_ST, 8, R11, &ap, false);
}

/* the address of the next argument of type t: its 8-byte word, or the object it points to */
VPRIV int x64w_va_arg(ecx *e, int rap, qty t)
{
	wkarg a;
	lval ap = { .k = L_REG, .r = (uint8_t)rap };
	int ra = x64_treg(e);
	lval nx = { .k = L_REG, .r = (uint8_t)ra, .off = 8 };

	x64w_classify(e, t, &a, true);
	x64_mem(e, X_LD, 8, ra, &ap, false);
	x64_mem(e, X_LEA, 8, R11, &nx, false);
	x64_mem(e, X_ST, 8, R11, &ap, false);
	if (x64w_byref(&a)) {
		lval p = { .k = L_REG, .r = (uint8_t)ra };

		x64_mem(e, X_LD, 8, ra, &p, false);
	}
	return ra;
}

/*
 * A runtime helper (__divti3, __addtf3, __fixdfti, ...) as compiler-rt is built for the target:
 * the Microsoft convention.  in[k] is the frame slot of argument k (8-byte words; a by-reference
 * type's object), types[k] its type; the result (type rt) is stored to the frame slot res (at
 * least 8 bytes; 16 for __int128, __float128, long double).
 */
VPRIV void x64w_helper(ecx *e, int fn, const int64_t *in, const qty *types, int n, int64_t res, qty rt)
{
	wkarg a[4], r;

	for (int k = 0; k < n; k++) {
		x64w_classify(e, types[k], &a[k], false);
		a[k].slot = in[k];
	}
	x64w_classify(e, rt, &r, false);
	r.slot = res;
	e->calls = true;
	x64w_call(e, FIXED(fn), NULL, a, n, false, &r, rt);
}
