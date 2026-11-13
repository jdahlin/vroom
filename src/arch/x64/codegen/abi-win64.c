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

VPRIV void x64_x87st(ecx *e, const lval *l);	/* fp.c */

VPRIV int x64w_argreg(int i) { return x64w_gpr[i]; }

/* the classes: n 1 one 8-byte word in a register (cls WA_INT or WA_SSE); up: returned in xmm0
   (16 bytes), passed by reference; mem: by reference both ways; n 0 and not mem: nothing */
VPRIV void x64w_classify(ecx *e, qty t, wkarg *a, bool unnamed)
{
	cc *c = e->c;
	uint64_t size = tsize(c, t);
	int k = KIND(c, t);

	*a = (wkarg){ .size = size, .unnamed = unnamed, .vector = is_vec(c, t), .align = talign(c, t) > 8 ? talign(c, t) : 8 };
	if (is_record(c, t) || is_array(c, t)) {
		/* "Any argument that doesn't fit in 8 bytes, or is not 1, 2, 4, or 8 bytes, must be
		   passed by reference" (an empty record too) */
		if (size == 1 || size == 2 || size == 4 || size == 8)
			a->n = 1, a->cls[0] = WA_INT;
		else
			a->mem = true;
		return;
	}
	if (is_vec(c, t) && T(c, t)->n == 1 && !is_bvec(c, t) && size <= 8) {
		/* one lane: LLVM scalarizes it, so the element's register (v1hi in rax, v1sf in xmm0) */
		a->n = 1;
		a->cls[0] = is_flt(c, T(c, t)->base) ? WA_SSE : WA_INT;
		return;
	}
	if (is_vec(c, t)) {
		if (size <= 16)	/* by reference, returned in xmm0; LLVM widens a short one to 16 bytes, and
				   reads it with an aligned 16-byte load */
			a->n = 1, a->cls[0] = WA_SSE, a->up = true, a->align = 16;
		else if (size % 16 == 0 && size <= 64)
			a->mem = a->up = true;	/* (LLVM without AVX: a pointer per 16 bytes, returned in xmm0..) */
		else
			a->mem = true;
		return;
	}
	if (em_bigint(c, t)) {
		a->mem = true;
		return;
	}
	if ((k == TY_INT128 || k == TY_UINT128) && !em_bitint(c, t)) {	/* (_BitInt(65..128): by reference both ways) */
		a->n = 1, a->cls[0] = WA_SSE, a->up = true;	/* MinGW: returned in xmm0 */
		return;
	}
	if (k == TY_LDOUBLE && !WIN64(c)) {
		a->mem = a->x87 = true;	/* ms_abi elsewhere (no MinGW rule): by reference, returned in st(0) */
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

/* a vector of 32 or 64 bytes: LLVM splits it in 16-byte halves, each by reference in its own
   position, and returns it in xmm0, xmm1 (...) */
static bool x64w_split(const wkarg *a) { return a->mem && a->up; }

/* the positions an argument takes */
static int x64w_npos(const wkarg *a) { return x64w_split(a) ? (int)(a->size / 16) : 1; }

/* passed as a pointer to the caller's copy (the one object) */
static bool x64w_byref(const wkarg *a) { return (a->mem || a->up) && !x64w_split(a); }

static lval x64w_fr(int64_t off) { return (lval){ .k = L_FRAME, .off = off, .align = 8 }; }

/* the first size bytes of xmm0 stored to the frame slot at off (X_ST) or loaded from the object at
   [r] (X_LD, off the register): a short vector result, no wider than it is */
VPRIV void x64w_xmm_part(ecx *e, int op, int64_t off, int size)
{
	lval m = op == X_ST ? x64w_fr(off) : (lval){ .k = L_REG, .r = (uint8_t)off, .align = 1 };

	if (size == 8 || size == 4) {
		x64_mem(e, op, size, XMM0, &m, false);
	} else if (op == X_ST) {
		x64_op3(e, X_MOV, 4, R11, XMM0, XNOREG);
		x64_mem(e, X_ST, size, R11, &m, false);
	} else {
		x64_mem(e, X_LD, size, R11, &m, false);
		x64_op3(e, X_MOV, 4, XMM0, R11, XNOREG);
	}
}

/* the live temporaries a Microsoft callee does not preserve, pushed (rsi and rdi survive, and the
   float temporaries xmm8-xmm14 are among xmm6-xmm15) */
static uint32_t x64w_save(ecx *e)
{
	static const uint8_t volatile_temps[] = { RAX, RCX, RDX, R8, R9, R10 };

	return x64_save(e, volatile_temps, sizeof volatile_temps, -1, -1);
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
	xst *X = e->x64;
	int first = r && r->mem && !r->up && !r->x87 ? 1 : 0;	/* the hidden result pointer takes position 0 */
	int np = first;
	uint32_t saved = x64w_save(e);
	rec *i;

	for (int k = 0; k < n; k++)
		np += x64w_npos(&a[k]);
	int64_t total = x64w_outgoing(e, np);
	/* the stack words first: r11 is their scratch, and fn's register moves into it next */
	for (int k = 0, p = first; k < n; p += x64w_npos(&a[k]), k++)
		for (int j = 0; j < x64w_npos(&a[k]); j++) {
			lval s = x64w_fr(a[k].slot + 16 * j), d = { .k = L_REG, .r = RSP, .off = 8 * (p + j) };

			if (p + j < 4)
				continue;
			x64_mem(e, x64w_byref(&a[k]) || x64w_split(&a[k]) ? X_LEA : X_LD, 8, R11, &s, false);
			x64_mem(e, X_ST, 8, R11, &d, false);
		}
	if (fn)
		x64_mov(e, R11, fn->r);
	if (first) {
		lval s = x64w_fr(r->slot);

		x64_mem(e, X_LEA, 8, RCX, &s, false);
	}
	for (int k = 0, p = first; k < n && p < 4; p += x64w_npos(&a[k]), k++) {
		lval s = x64w_fr(a[k].slot);

		if (x64w_split(&a[k])) {
			for (int j = 0; j < x64w_npos(&a[k]) && p + j < 4; j++) {
				lval h = x64w_fr(a[k].slot + 16 * j);

				x64_mem(e, X_LEA, 8, x64w_gpr[p + j], &h, false);
			}
		} else if (x64w_byref(&a[k]) || (!a[k].n && !a[k].mem)) {
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
	if (r && r->x87) {
		lval d = x64w_fr(r->slot);

		x64_x87st(e, &d);
	} else if (r && r->up && !r->mem && r->size < 16) {
		x64w_xmm_part(e, X_ST, r->slot, (int)r->size);
	} else if (r && !r->mem && r->n) {
		lval d = x64w_fr(r->slot);

		x64_mem(e, X_ST, r->up ? 16 : 8, r->cls[0] == WA_SSE ? XMM0 : RAX, &d, false);
	} else if (r && x64w_split(r)) {
		x64_call_xmms(e, r);
	} else if (!r) {
		x64_call_park(e, rt);
	}
	x64_restore(e, saved);
	return x64_call_value(e, fn, rt, r);
}

/*
 * The incoming side.  Position p's word is in its register (p < 4) or at [rbp + 16 + 8p].  A
 * by-reference parameter is used in place: its slot holds the address (x64_byref, the walker).
 */
VPRIV void x64w_params(ecx *e, wkarg *a, int n, const wkarg *r, bool variadic)
{
	int first = r && r->mem && !r->up && !r->x87 ? 1 : 0;
	int64_t half[4] = { 0 };	/* a split vector's half pointer from a register, parked in a slot */
	cc *c = e->c;
	xst *X = e->x64;
	const node *body = N(c->syms[e->wk->fsym].body);

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
	for (int k = 0, p = first; k < n; p += x64w_npos(&a[k]), k++) {
		uint32_t sym = c->lists[body->a + (uint32_t)k];
		qty ty = c->syms[sym].ty;
		lval home;
		bool local = x64_local(e, sym, &home);

		if (x64w_split(&a[k])) {	/* (copied into the slot below, once the registers are stored) */
			for (int j = 0; j < x64w_npos(&a[k]) && p + j < 4; j++) {
				lval d = x64w_fr(half[p + j] = wk_slot(e, 8, 8));

				x64_mem(e, X_ST, 8, x64w_gpr[p + j], &d, false);
			}
			continue;
		}
		if (x64w_byref(&a[k])) {	/* (x64_byref: the slot holds the caller copy's address) */
			lval d = x64w_fr(a[k].slot);

			if (p < 4)
				x64_mem(e, X_ST, 8, x64w_gpr[p], &d, false);
			else
				a[k].slot = 16 + 8 * p;
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
	int np = first;

	for (int k = 0; k < n; np += x64w_npos(&a[k]), k++)
		for (int j = 0; x64w_split(&a[k]) && j < x64w_npos(&a[k]); j++) {
			lval s = x64w_fr(np + j < 4 ? half[np + j] : 16 + 8 * (np + j)), d = x64w_fr(a[k].slot + 16 * j);
			int rs = x64_treg(e), rd;

			x64_mem(e, X_LD, 8, rs, &s, false);
			rd = x64_laddr(e, &d);
			x64_copy(e, rd, rs, 16);
			x64_tfree(e, rd);
			x64_tfree(e, rs);
		}
	X->ngp = np, X->nfp = 0, X->stk = 0;
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
	lval first = x64w_fr(16 + 8 * e->x64->ngp), ap = { .k = L_REG, .r = (uint8_t)rap };
	int r = x64_laddr(e, &first);

	x64_store(e, r, &ap, 8);
	x64_tfree(e, r);
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
