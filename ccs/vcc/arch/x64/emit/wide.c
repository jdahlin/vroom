/* vcc code generator, x86-64: __int128 (x64impl.h).  Its values are 16-byte
 * frame objects held by address, as x87 long double's: add/adc and
 * sub/sbb, the bitwise operations and the low product inline, division,
 * variable shifts and float conversions through libgcc (__divti3 ...). */
#include "x64impl.h"

VPRIV bool x64_i128mem(void) { return true; }

static val x64_wv(int r) { return (val){ .k = V_REG, .r = (uint8_t)r }; }
static lval x64_wat(int r, int off) { return (lval){ .k = L_REG, .r = (uint8_t)r, .off = off, .align = 8 }; }

/* a new 16-byte object: its frame slot, and its address in a temporary */
static int x64_wnew(ecx *e, int32_t *slot)
{
	lval l = { .k = L_FRAME, .off = wk_slot(e, 16, 16), .align = 16 };

	if (slot)
		*slot = (int32_t)l.off;
	return x64_laddr(e, &l);
}

/* the constant's object */
VPRIV val x64_wconst(ecx *e, i128 v)
{
	int rd = x64_wnew(e, NULL);
	lval lo = x64_wat(rd, 0), hi = x64_wat(rd, 8);

	x64_movi(e, R11, (int64_t)(uint64_t)v);
	x64_mem(e, X_ST, 8, R11, &lo, false);
	x64_movi(e, R11, (int64_t)(uint64_t)((u128)v >> 64));
	x64_mem(e, X_ST, 8, R11, &hi, false);
	return x64_wv(rd);
}

/* an operand's address (a constant gets its object) */
static int x64_waddr(ecx *e, val *v)
{
	if (v->k == V_CONST)
		*v = x64_wconst(e, v->i);
	return x64_toreg(e, v, 8);
}

/* ---------------- libgcc calls ---------------- */

/*
 * fn(args): words[k] (frame slots, 8 bytes each) into the integer argument
 * registers, or xmm0 for a float argument; the live temporaries saved
 * around the call.  The result's rax, rdx (or xmm0) stored to the frame
 * slot res.
 */
static void x64_lcall(ecx *e, int fn, const int32_t *words, int nw, int xmmarg, int32_t res, int resw, bool fres)
{
	xst *X = e->x64;
	uint32_t saved = X->live & X_TEMPS, fsaved = X->live & X_FTEMPS;
	int pad;
	rec *i;

	for (int q = 0; q < 16; q++)
		if (saved & (1u << q)) {
			x64_op3(e, X_PUSH, 8, XNOREG, q, XNOREG);
			X->pushes++;
		}
	for (int q = XMM8; q < XMM15; q++)
		if (fsaved & (1u << q)) {
			lval d = { .k = L_REG, .r = RSP };

			x64_put(e, X_ADJSP)->imm = -8;
			x64_mem(e, X_ST, 8, q, &d, false);
			X->pushes++;
		}
	pad = X->pushes & 1;
	if (pad) {
		x64_put(e, X_ADJSP)->imm = -8;
		X->pushes++;
	}
	for (int k = 0; k < nw; k++) {
		lval s = { .k = L_FRAME, .off = words[k], .align = 8 };

		x64_mem(e, X_LD, 8, k == xmmarg ? XMM0 : x64_argreg(k - (xmmarg >= 0 && k > xmmarg)), &s, false);
	}
	i = x64_put(e, X_CALL);
	i->ref = FIXED(fn);
	for (int w = 0; w < resw; w++) {
		lval d = { .k = L_FRAME, .off = res + 8 * w, .align = 8 };

		x64_mem(e, X_ST, 8, fres ? XMM0 : w ? RDX : RAX, &d, false);
	}
	if (pad) {
		x64_put(e, X_ADJSP)->imm = 8;
		X->pushes--;
	}
	for (int q = XMM15 - 1; q >= XMM8; q--)
		if (fsaved & (1u << q)) {
			lval s = { .k = L_REG, .r = RSP };

			x64_mem(e, X_LD, 8, q, &s, false);
			x64_put(e, X_ADJSP)->imm = 8;
			X->pushes--;
		}
	for (int q = 15; q >= 0; q--)
		if (saved & (1u << q)) {
			x64_op3(e, X_POP, 8, q, XNOREG, XNOREG);
			X->pushes--;
		}
}

/* the 8-byte word at [r + off] copied to a new frame slot */
static int32_t x64_wword(ecx *e, int r, int off)
{
	lval s = x64_wat(r, off), d = { .k = L_FRAME, .off = wk_slot(e, 8, 8), .align = 8 };

	x64_mem(e, X_LD, 8, R11, &s, false);
	x64_mem(e, X_ST, 8, R11, &d, false);
	return (int32_t)d.off;
}

/* a frame word into a new temporary */
static val x64_wload(ecx *e, const lval *l)
{
	int r = x64_treg(e);

	x64_mem(e, X_LD, 8, r, l, false);
	return x64_wv(r);
}

/* ---------------- arithmetic ---------------- */

VPRIV val x64_wbin(ecx *e, int op, bool sgn, val a, val b)
{
	int ra, rb, rd;
	int32_t slot;

	if (op == T_LESSLESS || op == T_GREATERGREATER) {
		/* inline, as clang (a freestanding vDSO has no libgcc): the words in slots, the count
		   masked to 0..127, three cases (0, below 64, 64 and over) */
		int32_t w0, w1;
		bool shl = op == T_LESSLESS;
		uint32_t lge = wk_newlabel(e), lzero = wk_newlabel(e), ldone = wk_newlabel(e);
		lval L0, L1, LC;
		val v;
		int r;

		ra = x64_waddr(e, &a);
		w0 = x64_wword(e, ra, 0);
		w1 = x64_wword(e, ra, 8);
		x64_tfree(e, ra);
		v = x64_bin(e, T_AMP, 8, false, b, vconst(127));
		r = x64_toreg(e, &v, 8);
		LC = (lval){ .k = L_FRAME, .off = wk_slot(e, 8, 8), .align = 8 };
		x64_mem(e, X_ST, 8, r, &LC, false);
		x64_tfree(e, r);
		L0 = (lval){ .k = L_FRAME, .off = w0, .align = 8 };
		L1 = (lval){ .k = L_FRAME, .off = w1, .align = 8 };
		rd = x64_wnew(e, &slot);
		x64_tfree(e, rd);
#define WLD(l) x64_wload(e, &(l))
#define WST(vv, wo_) do { val t_ = (vv); int rt_ = x64_toreg(e, &t_, 8); lval d_ = { .k = L_FRAME, .off = slot + (wo_), .align = 8 }; \
		x64_mem(e, X_ST, 8, rt_, &d_, false); x64_tfree(e, rt_); } while (0)
		x64_cmpbr(e, T_EQUALEQUAL, 8, false, WLD(LC), vconst(0), lzero);
		x64_cmpbr(e, T_GREATEREQ, 8, false, WLD(LC), vconst(64), lge);
		if (shl) {	/* lo << c, hi << c | lo >> (64 - c) */
			WST(x64_bin(e, T_LESSLESS, 8, false, WLD(L0), WLD(LC)), 0);
			WST(x64_bin(e, T_PIPE, 8, false, x64_bin(e, T_LESSLESS, 8, false, WLD(L1), WLD(LC)),
				    x64_bin(e, T_GREATERGREATER, 8, false, WLD(L0), x64_bin(e, T_MINUS, 8, false, vconst(64), WLD(LC)))), 8);
		} else {	/* lo >> c | hi << (64 - c), hi >> c */
			WST(x64_bin(e, T_PIPE, 8, false, x64_bin(e, T_GREATERGREATER, 8, false, WLD(L0), WLD(LC)),
				    x64_bin(e, T_LESSLESS, 8, false, WLD(L1), x64_bin(e, T_MINUS, 8, false, vconst(64), WLD(LC)))), 0);
			WST(x64_bin(e, T_GREATERGREATER, 8, sgn, WLD(L1), WLD(LC)), 8);
		}
		x64_jump(e, ldone);
		x64_label(e, lge);
		if (shl) {
			WST(vconst(0), 0);
			WST(x64_bin(e, T_LESSLESS, 8, false, WLD(L0), x64_bin(e, T_MINUS, 8, false, WLD(LC), vconst(64))), 8);
		} else {
			WST(x64_bin(e, T_GREATERGREATER, 8, sgn, WLD(L1), x64_bin(e, T_MINUS, 8, false, WLD(LC), vconst(64))), 0);
			WST(sgn ? x64_bin(e, T_GREATERGREATER, 8, true, WLD(L1), vconst(63)) : vconst(0), 8);
		}
		x64_jump(e, ldone);
		x64_label(e, lzero);
		WST(WLD(L0), 0);
		WST(WLD(L1), 8);
		x64_label(e, ldone);
#undef WLD
#undef WST
		{
			lval l = { .k = L_FRAME, .off = slot, .align = 16 };

			return x64_wv(x64_laddr(e, &l));
		}
	}
	if (0) {
		/* b is an int: __ashlti3, __lshrti3, __ashrti3 (a, (int)b) */
		int32_t w[3];
		int rc;

		ra = x64_waddr(e, &a);
		w[0] = x64_wword(e, ra, 0);
		w[1] = x64_wword(e, ra, 8);
		rc = x64_toreg(e, &b, 8);
		{
			lval d = { .k = L_FRAME, .off = wk_slot(e, 8, 8), .align = 8 };

			x64_mem(e, X_ST, 8, rc, &d, false);
			w[2] = (int32_t)d.off;
		}
		x64_tfree(e, ra);
		x64_tfree(e, rc);
		rd = x64_wnew(e, &slot);
		x64_lcall(e, op == T_LESSLESS ? FN_ASHLTI3 : sgn ? FN_ASHRTI3 : FN_LSHRTI3, w, 3, -1, slot, 2, false);
		return x64_wv(rd);
	}
	ra = x64_waddr(e, &a);
	rb = x64_waddr(e, &b);
	if (op == T_SLASH || op == T_PERCENT) {
		int32_t w[4] = { x64_wword(e, ra, 0), x64_wword(e, ra, 8), x64_wword(e, rb, 0), x64_wword(e, rb, 8) };

		x64_tfree(e, ra);
		x64_tfree(e, rb);
		rd = x64_wnew(e, &slot);
		x64_lcall(e, op == T_SLASH ? (sgn ? FN_DIVTI3 : FN_UDIVTI3) : (sgn ? FN_MODTI3 : FN_UMODTI3), w, 4, -1, slot, 2, false);
		return x64_wv(rd);
	}
	rd = x64_wnew(e, NULL);
	{
		lval alo = x64_wat(ra, 0), ahi = x64_wat(ra, 8), blo = x64_wat(rb, 0), bhi = x64_wat(rb, 8);
		lval dlo = x64_wat(rd, 0), dhi = x64_wat(rd, 8);
		int t = x64_treg(e);

		if (op == T_STAR) {
			/*
			 * hi = alo * bhi + ahi * blo + high(alo * blo), lo = low(alo * blo).  Everything
			 * through frame slots: mul writes rdx:rax, and any operand register may be one
			 * of them.
			 */
			static const uint8_t rr[2] = { RAX, RDX };
			int32_t w[4] = { x64_wword(e, ra, 0), x64_wword(e, ra, 8), x64_wword(e, rb, 0), x64_wword(e, rb, 8) }, ds;
			lval s0 = { .k = L_FRAME, .off = w[0] }, s1 = { .k = L_FRAME, .off = w[1] };
			lval s2 = { .k = L_FRAME, .off = w[2] }, s3 = { .k = L_FRAME, .off = w[3] };
			lval flo, fhi;
			uint32_t sv;

			x64_tfree(e, rd);
			rd = x64_wnew(e, &ds);
			flo = (lval){ .k = L_FRAME, .off = ds }, fhi = (lval){ .k = L_FRAME, .off = ds + 8 };
			x64_mem(e, X_LD, 8, t, &s0, false);
			x64_mem(e, X_LD, 8, R11, &s3, false);
			x64_op3(e, X_IMUL, 8, t, t, R11);
			x64_mem(e, X_LD, 8, R11, &s1, false);
			x64_mem(e, X_ST, 8, t, &fhi, false);
			x64_mem(e, X_LD, 8, t, &s2, false);
			x64_op3(e, X_IMUL, 8, R11, R11, t);
			x64_mem(e, X_LD, 8, t, &fhi, false);
			x64_op3(e, X_ADD, 8, t, t, R11);
			x64_mem(e, X_ST, 8, t, &fhi, false);
			sv = x64_save(e, rr, 2, -1, -1);
			x64_mem(e, X_LD, 8, RAX, &s0, false);
			x64_mem(e, X_LD, 8, R11, &s2, false);
			x64_op3(e, X_MULW, 8, XNOREG, R11, XNOREG);
			x64_mem(e, X_ST, 8, RAX, &flo, false);
			x64_mem(e, X_LD, 8, R11, &fhi, false);
			x64_op3(e, X_ADD, 8, R11, R11, RDX);
			x64_mem(e, X_ST, 8, R11, &fhi, false);
			x64_restore(e, sv);
		} else {
			static const int lo_op[] = { [T_PLUS] = X_ADD, [T_MINUS] = X_SUB, [T_AMP] = X_AND, [T_PIPE] = X_OR, [T_CARET] = X_XOR };
			int xo, xhi;

			if (op != T_PLUS && op != T_MINUS && op != T_AMP && op != T_PIPE && op != T_CARET)
				efatal(e, e->curloc, "x86-64: __int128 operator %d", op);
			xo = lo_op[op];
			xhi = op == T_PLUS ? X_ADC : op == T_MINUS ? X_SBB : xo;
			x64_mem(e, X_LD, 8, t, &alo, false);
			x64_mem(e, X_LD, 8, R11, &blo, false);
			x64_op3(e, xo, 8, t, t, R11);
			x64_mem(e, X_ST, 8, t, &dlo, false);
			x64_mem(e, X_LD, 8, t, &ahi, false);
			x64_mem(e, X_LD, 8, R11, &bhi, false);
			x64_op3(e, xhi, 8, t, t, R11);	/* (mov does not touch the carry) */
			x64_mem(e, X_ST, 8, t, &dhi, false);
		}
		x64_tfree(e, t);
	}
	x64_tfree(e, ra);
	x64_tfree(e, rb);
	return x64_wv(rd);
}

VPRIV val x64_wun(ecx *e, int op, val a)
{
	int ra = x64_waddr(e, &a), rd = x64_wnew(e, NULL), t = x64_treg(e);
	lval alo = x64_wat(ra, 0), ahi = x64_wat(ra, 8), dlo = x64_wat(rd, 0), dhi = x64_wat(rd, 8);

	if (op == T_TILDE) {
		x64_mem(e, X_LD, 8, t, &alo, false);
		x64_op3(e, X_NOT, 8, t, t, XNOREG);
		x64_mem(e, X_ST, 8, t, &dlo, false);
		x64_mem(e, X_LD, 8, t, &ahi, false);
		x64_op3(e, X_NOT, 8, t, t, XNOREG);
		x64_mem(e, X_ST, 8, t, &dhi, false);
	} else {
		/* 0 - a */
		x64_movi(e, t, 0);
		x64_mem(e, X_LD, 8, R11, &alo, false);
		x64_op3(e, X_SUB, 8, t, t, R11);
		x64_mem(e, X_ST, 8, t, &dlo, false);
		x64_movi(e, t, 0);
		x64_mem(e, X_LD, 8, R11, &ahi, false);
		x64_op3(e, X_SBB, 8, t, t, R11);
		x64_mem(e, X_ST, 8, t, &dhi, false);
	}
	x64_tfree(e, t);
	x64_tfree(e, ra);
	return x64_wv(rd);
}

/* a op b as 0 or 1 */
VPRIV val x64_wcmp(ecx *e, int op, bool sgn, val a, val b)
{
	int ra = x64_waddr(e, &a), rb = x64_waddr(e, &b), t = x64_treg(e);
	bool swap = op == T_GREATER || op == T_LESSEQ;
	int pa = swap ? rb : ra, pb = swap ? ra : rb, cc;
	lval alo = x64_wat(pa, 0), ahi = x64_wat(pa, 8), blo = x64_wat(pb, 0), bhi = x64_wat(pb, 8);

	if (op == T_EQUALEQUAL || op == T_EXCLAIMEQ) {
		int u = x64_treg(e);

		x64_mem(e, X_LD, 8, t, &alo, false);
		x64_mem(e, X_LD, 8, R11, &blo, false);
		x64_op3(e, X_XOR, 8, t, t, R11);
		x64_mem(e, X_LD, 8, u, &ahi, false);
		x64_mem(e, X_LD, 8, R11, &bhi, false);
		x64_op3(e, X_XOR, 8, u, u, R11);
		x64_op3(e, X_OR, 8, t, t, u);
		x64_tfree(e, u);
		cc = op == T_EQUALEQUAL ? CC_E : CC_NE;
	} else {
		/* x < y as the flags of the 128-bit x - y: cmp the low words, sbb the high ones */
		x64_mem(e, X_LD, 8, t, &alo, false);
		x64_mem(e, X_LD, 8, R11, &blo, false);
		x64_op3(e, X_CMP, 8, XNOREG, t, R11);
		x64_mem(e, X_LD, 8, t, &ahi, false);
		x64_mem(e, X_LD, 8, R11, &bhi, false);
		x64_op3(e, X_SBB, 8, t, t, R11);
		/* <: x=a, y=b; >: x=b, y=a; >=: !(a < b); <=: !(b < a) */
		cc = op == T_LESS || op == T_GREATER ? (sgn ? CC_L : CC_B) : (sgn ? CC_GE : CC_AE);
	}
	x64_tfree(e, ra);
	x64_tfree(e, rb);
	x64_op3(e, X_SETCC, 4, t, XNOREG, XNOREG)->cond = (uint8_t)cc;
	return x64_wv(t);
}

/* ---------------- conversions ---------------- */

/* v of type from as type to, __int128 on at least one side */
VPRIV val x64_wcvt(ecx *e, val v, qty from, qty to)
{
	cc *c = e->c;
	bool fw = KIND(c, from) == TY_INT128 || KIND(c, from) == TY_UINT128;
	bool tw = KIND(c, to) == TY_INT128 || KIND(c, to) == TY_UINT128;
	int32_t slot;

	if (fw && tw)
		return v;
	if (KIND(c, from) == TY_FLOAT128 || KIND(c, to) == TY_FLOAT128)
		return x64_cvt(e, v, from, to);	/* (fp.c: libgcc's __fixtfti, __floattitf ...) */
	if (tw && v.k == V_CONST)
		return x64_wconst(e, wk_signed(c, from) ? v.i : (i128)(u128)(uint64_t)v.i);
	if (tw && is_flt(c, from)) {
		int fs = wk_size(c, from), rd;
		int32_t w[1];
		lval d = { .k = L_FRAME, .off = wk_slot(e, 8, 8), .align = 8 };

		if (KIND(c, from) == TY_LDOUBLE)
			efatal(e, e->curloc, "x86-64: long double to __int128 is not supported yet (PLAN-x86.md X3)");
		x64_mem(e, X_ST, 8, v.r, &d, false);
		x64_tfree(e, v.r);
		w[0] = (int32_t)d.off;
		rd = x64_wnew(e, &slot);
		x64_lcall(e, KIND(c, to) == TY_INT128 ? (fs == 4 ? FN_FIXSFTI : FN_FIXDFTI) : (fs == 4 ? FN_FIXUNSSFTI : FN_FIXUNSDFTI),
			  w, 1, 0, slot, 2, false);
		return x64_wv(rd);
	}
	if (tw) {
		/* an integer: its value extended to 64 bits, the high word its sign or 0 */
		int r = x64_toreg(e, &v, 8), rd = x64_wnew(e, NULL);
		lval lo = x64_wat(rd, 0), hi = x64_wat(rd, 8);

		if (wk_size(c, from) < 8)
			x64_ext(e, r, wk_size(c, from), wk_signed(c, from));
		x64_mem(e, X_ST, 8, r, &lo, false);
		if (wk_signed(c, from)) {
			x64_op3(e, X_SARI, 8, r, r, XNOREG)->imm = 63;
		} else {
			x64_movi(e, r, 0);
		}
		x64_mem(e, X_ST, 8, r, &hi, false);
		x64_tfree(e, r);
		return x64_wv(rd);
	}
	if (KIND(c, to) == TY_BOOL) {
		int ra = x64_waddr(e, &v), t = x64_treg(e);
		lval lo = x64_wat(ra, 0), hi = x64_wat(ra, 8);

		x64_mem(e, X_LD, 8, t, &lo, false);
		x64_mem(e, X_LD, 8, R11, &hi, false);
		x64_op3(e, X_OR, 8, t, t, R11);
		x64_tfree(e, ra);
		x64_op3(e, X_SETCC, 4, t, XNOREG, XNOREG)->cond = CC_NE;
		return x64_wv(t);
	}
	if (is_flt(c, to)) {
		int ra = x64_waddr(e, &v), ts = wk_size(c, to), x;
		int32_t w[2] = { x64_wword(e, ra, 0), x64_wword(e, ra, 8) };
		lval d = { .k = L_FRAME, .off = wk_slot(e, 8, 8), .align = 8 };

		if (KIND(c, to) == TY_LDOUBLE)
			efatal(e, e->curloc, "x86-64: __int128 to long double is not supported yet (PLAN-x86.md X3)");
		x64_tfree(e, ra);
		x64_lcall(e, KIND(c, from) == TY_INT128 ? (ts == 4 ? FN_FLOATTISF : FN_FLOATTIDF) : (ts == 4 ? FN_FLOATUNTISF : FN_FLOATUNTIDF),
			  w, 2, -1, (int32_t)d.off, 1, true);
		x = x64_ftreg(e);
		x64_mem(e, X_LD, ts, x, &d, false);
		return x64_wv(x);
	}
	{
		/* to a narrower integer: the low word */
		int ra = x64_waddr(e, &v), r = x64_treg(e);
		lval lo = x64_wat(ra, 0);
		int ts = wk_size(c, to);

		x64_mem(e, X_LD, 8, r, &lo, false);
		x64_tfree(e, ra);
		if (ts < 4)
			x64_ext(e, r, ts, wk_signed(c, to));
		return x64_wv(r);
	}
}
