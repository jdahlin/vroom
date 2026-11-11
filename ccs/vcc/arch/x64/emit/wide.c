/* vcc code generator, x86-64: __int128 (x64impl.h).  Its values are 16-byte
 * frame objects held by address, as x87 long double's: add/adc and
 * sub/sbb, the bitwise operations and the low product inline, division,
 * variable shifts and float conversions through libgcc (__divti3 ...). */
#include "x64impl.h"

VPRIV bool x64_i128mem(void) { return true; }

static val x64_wv(int r) { return (val){ .k = V_REG, .r = (uint8_t)r }; }
static lval x64_wat(int r, int off) { return (lval){ .k = L_REG, .r = (uint8_t)r, .off = off, .align = 8 }; }

/* a new 16-byte object: its frame slot, and its address in a temporary */
static int x64_wnew(ecx *e, int64_t *slot)
{
	lval l = { .k = L_FRAME, .off = wk_slot(e, 16, 16), .align = 16 };

	if (slot)
		*slot = (int64_t)l.off;
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

/* Preserve the source object (which may be a const global or an lvalue):
   a bit-precise value's high word is extended in a fresh 16-byte temporary. */
VPRIV val x64_wbitint(ecx *e, val v, unsigned bits, bool sgn)
{
	if (bits == 128)
		return v;
	int rs = x64_waddr(e, &v), rd = x64_wnew(e, NULL);
	lval slo = x64_wat(rs, 0), shi = x64_wat(rs, 8);
	lval dlo = x64_wat(rd, 0), dhi = x64_wat(rd, 8);

	x64_mem(e, X_LD, 8, R11, &slo, false);
	x64_mem(e, X_ST, 8, R11, &dlo, false);
	x64_mem(e, X_LD, 8, R11, &shi, false);
	x64_op3(e, X_SHLI, 8, R11, R11, XNOREG)->imm = 128 - bits;
	x64_op3(e, sgn ? X_SARI : X_SHRI, 8, R11, R11, XNOREG)->imm = 128 - bits;
	x64_mem(e, X_ST, 8, R11, &dhi, false);
	x64_tfree(e, rs);
	return x64_wv(rd);
}

/* ---------------- libgcc calls ---------------- */

/*
 * fn(args): words[k] (frame slots, 8 bytes each) into the integer argument
 * registers, or xmm0 for a float argument; the live temporaries saved
 * around the call.  The result's rax, rdx (or xmm0) stored to the frame
 * slot res.
 */
static void x64_lcall(ecx *e, int fn, const int64_t *words, int nw, int xmmarg, int64_t res, int resw, bool fres)
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
static int64_t x64_wword(ecx *e, int r, int off)
{
	lval s = x64_wat(r, off), d = { .k = L_FRAME, .off = wk_slot(e, 8, 8), .align = 8 };

	x64_mem(e, X_LD, 8, R11, &s, false);
	x64_mem(e, X_ST, 8, R11, &d, false);
	return (int64_t)d.off;
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
	int64_t slot;

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
		int64_t w[3];
		int rc;

		ra = x64_waddr(e, &a);
		w[0] = x64_wword(e, ra, 0);
		w[1] = x64_wword(e, ra, 8);
		rc = x64_toreg(e, &b, 8);
		{
			lval d = { .k = L_FRAME, .off = wk_slot(e, 8, 8), .align = 8 };

			x64_mem(e, X_ST, 8, rc, &d, false);
			w[2] = (int64_t)d.off;
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
		int64_t w[4] = { x64_wword(e, ra, 0), x64_wword(e, ra, 8), x64_wword(e, rb, 0), x64_wword(e, rb, 8) };

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
			int64_t w[4] = { x64_wword(e, ra, 0), x64_wword(e, ra, 8), x64_wword(e, rb, 0), x64_wword(e, rb, 8) }, ds;
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
	int64_t slot;

	if (fw && tw)
		return v;
	if (KIND(c, from) == TY_FLOAT128 || KIND(c, to) == TY_FLOAT128)
		return x64_cvt(e, v, from, to);	/* (fp.c: libgcc's __fixtfti, __floattitf ...) */
	if (KIND(c, from) == TY_LDOUBLE || KIND(c, to) == TY_LDOUBLE) {
		/* x86 long double uses the x87 helpers, not binary128's TF ABI.
		 * Both source representations occupy a 16-byte object; let the
		 * normal call classifier place it on the stack or in integer regs. */
		wkarg arg, result;
		int ra = x64_waddr(e, &v);
		int fn = tw ? (wk_signed(c, to) ? FN_FIXXFTI : FN_FIXUNSXFTI) :
			(wk_signed(c, from) ? FN_FLOATTIXF : FN_FLOATUNTIXF);

		x64_classify(e, from, &arg, false);
		arg.slot = wk_slot(e, 16, 16);
		for (int w = 0; w < 2; w++) {
			lval src = x64_wat(ra, 8 * w);
			lval dst = { .k = L_FRAME, .off = arg.slot + 8 * w, .align = 8 };

			x64_mem(e, X_LD, 8, R11, &src, false);
			x64_mem(e, X_ST, 8, R11, &dst, false);
		}
		x64_tfree(e, ra);
		x64_classify(e, to, &result, false);
		result.slot = wk_slot(e, 16, 16);
		x64_call(e, FIXED(fn), NULL, &arg, 1, false, &result, to);
		lval dst = { .k = L_FRAME, .off = result.slot, .align = 16 };
		return x64_wv(x64_laddr(e, &dst));
	}
	if (tw && v.k == V_CONST)
		return x64_wconst(e, wk_signed(c, from) ? v.i : (i128)(u128)(uint64_t)v.i);
	if (tw && is_flt(c, from)) {
		int fs = wk_size(c, from), rd;
		int64_t w[1];
		lval d = { .k = L_FRAME, .off = wk_slot(e, 8, 8), .align = 8 };

		x64_mem(e, X_ST, 8, v.r, &d, false);
		x64_tfree(e, v.r);
		w[0] = (int64_t)d.off;
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
		int64_t w[2] = { x64_wword(e, ra, 0), x64_wword(e, ra, 8) };
		lval d = { .k = L_FRAME, .off = wk_slot(e, 8, 8), .align = 8 };

		x64_tfree(e, ra);
		x64_lcall(e, KIND(c, from) == TY_INT128 ? (ts == 4 ? FN_FLOATTISF : FN_FLOATTIDF) : (ts == 4 ? FN_FLOATUNTISF : FN_FLOATUNTIDF),
			  w, 2, -1, (int64_t)d.off, 1, true);
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

/* ---------------- arbitrary-width integer objects ---------------- */

static int x64_bignew(ecx *e, qty t)
{
	lval l = { .k = L_FRAME, .off = wk_slot(e, tsize(e->c, t), 8), .align = 8 };
	return x64_laddr(e, &l);
}

static void x64_bignorm(ecx *e, int rd, qty t)
{
	cc *c = e->c;
	unsigned bits = T(c, t)->n, rem = bits & 63;
	if (rem) {
		lval top = x64_wat(rd, (int)tsize(c, t) - 8);
		x64_mem(e, X_LD, 8, R11, &top, false);
		x64_op3(e, X_SHLI, 8, R11, R11, XNOREG)->imm = 64 - rem;
		x64_op3(e, wk_signed(c, t) ? X_SARI : X_SHRI, 8, R11, R11, XNOREG)->imm = 64 - rem;
		x64_mem(e, X_ST, 8, R11, &top, false);
	}
}

VPRIV val x64_bigconst(ecx *e, qty t, const uint32_t *words, i128 small)
{
	int rd = x64_bignew(e, t);
	unsigned n = (unsigned)tsize(e->c, t) / 8;
	for (unsigned i = 0; i < n; i++) {
		lval l = x64_wat(rd, (int)i * 8);
		uint64_t w = words ? (uint64_t)words[2*i] | (uint64_t)words[2*i+1] << 32 :
			i < 2 ? (uint64_t)((u128)small >> (i * 64)) : small < 0 ? UINT64_MAX : 0;
		x64_movi(e, R11, w);
		x64_mem(e, X_ST, 8, R11, &l, false);
	}
	x64_bignorm(e, rd, t);
	return x64_wv(rd);
}

static val x64_bigslot(ecx *e, int64_t slot)
{
	lval l = { .k = L_FRAME, .off = slot, .align = 8 };
	int r = x64_treg(e);
	x64_mem(e, X_LD, 8, r, &l, false);
	return x64_wv(r);
}

/* Return a limb at i +/- whole, extending beyond either end without
 * forming an out-of-bounds load.  A negative index fails the unsigned bound. */
static val x64_bigword(ecx *e, int ra, unsigned n, int i, bool right, bool sgn, int64_t whole)
{
	val index = x64_bin(e, right ? T_PLUS : T_MINUS, 8, false, vconst(i), x64_bigslot(e, whole));
	int ri = x64_toreg(e, &index, 8), r = x64_treg(e), cmp = x64_treg(e);
	uint32_t done = wk_newlabel(e);
	if (right && sgn) {
		lval top = x64_wat(ra, (int)(n-1)*8);
		x64_mem(e, X_LD, 8, r, &top, false);
		x64_op3(e, X_SARI, 8, r, r, XNOREG)->imm = 63;
	} else x64_movi(e, r, 0);
	x64_mov(e, cmp, ri);
	x64_cmpbr(e, T_GREATEREQ, 8, false, x64_wv(cmp), vconst(n), done);
	x64_op3(e, X_SHLI, 8, ri, ri, XNOREG)->imm = 3;
	x64_op3(e, X_ADD, 8, ri, ri, ra);
	lval src = x64_wat(ri, 0);
	x64_mem(e, X_LD, 8, r, &src, false);
	x64_label(e, done);
	x64_tfree(e, ri);
	return x64_wv(r);
}

static val x64_bigshift(ecx *e, int op, qty t, val a, val b)
{
	bool right = op == T_GREATERGREATER;
	unsigned n = (unsigned)tsize(e->c, t)/8;
	int ra = x64_toreg(e, &a, 8), rd = x64_bignew(e, t);
	int64_t whole = wk_slot(e, 8, 8), bits = wk_slot(e, 8, 8);
	lval wl = { .k = L_FRAME, .off = whole, .align = 8 }, bl = wl;
	bl.off = bits;
	int rb = x64_toreg(e, &b, 8), r = x64_treg(e);
	x64_op3(e, X_SHRI, 8, r, rb, XNOREG)->imm = 6;
	x64_mem(e, X_ST, 8, r, &wl, false);
	x64_tfree(e, r);
	b = x64_bin(e, T_AMP, 8, false, b, vconst(63));
	x64_mem(e, X_ST, 8, b.r, &bl, false);
	x64_tfree(e, b.r);
	for (unsigned i = 0; i < n; i++) {
		lval out = x64_wat(rd, (int)i*8);
		uint32_t done = wk_newlabel(e);
		val v = x64_bigword(e, ra, n, (int)i, right, right && wk_signed(e->c, t), whole);
		v = x64_bin(e, op, 8, false, v, x64_bigslot(e, bits));
		x64_mem(e, X_ST, 8, v.r, &out, false);
		x64_tfree(e, v.r);
		x64_brz(e, x64_bigslot(e, bits), 8, false, done);
		v = x64_bigword(e, ra, n, (int)i + (right ? 1 : -1), right, right && wk_signed(e->c, t), whole);
		val count = x64_bin(e, T_MINUS, 8, false, vconst(64), x64_bigslot(e, bits));
		v = x64_bin(e, right ? T_LESSLESS : T_GREATERGREATER, 8, false, v, count);
		x64_mem(e, X_LD, 8, R11, &out, false);
		x64_op3(e, X_OR, 8, v.r, v.r, R11);
		x64_mem(e, X_ST, 8, v.r, &out, false);
		x64_tfree(e, v.r);
		x64_label(e, done);
	}
	x64_tfree(e, ra);
	x64_bignorm(e, rd, t);
	return x64_wv(rd);
}

/* Conditional two's-complement negation of a private limb object. */
static void x64_bigneg_if(ecx *e, int ptr, unsigned n, val sign)
{
	uint32_t done = wk_newlabel(e);
	x64_brz(e, sign, 8, false, done);
	int r = x64_treg(e);
	for (unsigned i = 0; i < n; i++) {
		lval l = x64_wat(ptr, (int)i*8);
		x64_mem(e, X_LD, 8, R11, &l, false);
		x64_movi(e, r, 0);
		x64_op3(e, i ? X_SBB : X_SUB, 8, r, r, R11);
		x64_mem(e, X_ST, 8, r, &l, false);
	}
	x64_tfree(e, r);
	x64_label(e, done);
}

/* Restoring division: Q initially holds the dividend. Each iteration moves
 * its high bit into the remainder and appends one quotient bit. The extra
 * remainder limb retains carry even for a divisor with its top bit set. */
static val x64_bigdiv(ecx *e, int op, qty t, val a, val b)
{
	unsigned n = (unsigned)tsize(e->c, t)/8;
	int qa = x64_toreg(e, &a, 8), db = x64_toreg(e, &b, 8);
	int q = x64_bignew(e, t), d = x64_bignew(e, t);
	x64_copy(e, q, qa, 8*n);
	x64_copy(e, d, db, 8*n);
	x64_tfree(e, qa);
	x64_tfree(e, db);
	lval rl = { .k = L_FRAME, .off = wk_slot(e, 8*(n+1), 8), .align = 8 };
	int rem = x64_laddr(e, &rl);
	x64_zero(e, rem, 8*(n+1));
	int64_t sa = wk_slot(e, 8, 8), sb = wk_slot(e, 8, 8);
	if (wk_signed(e->c, t)) {
		lval la = { .k = L_FRAME, .off = sa, .align = 8 }, lb = la;
		lb.off = sb;
		lval at = x64_wat(q, (int)(n-1)*8), bt = x64_wat(d, (int)(n-1)*8);
		x64_mem(e, X_LD, 8, R11, &at, false);
		x64_op3(e, X_SARI, 8, R11, R11, XNOREG)->imm = 63;
		x64_mem(e, X_ST, 8, R11, &la, false);
		x64_mem(e, X_LD, 8, R11, &bt, false);
		x64_op3(e, X_SARI, 8, R11, R11, XNOREG)->imm = 63;
		x64_mem(e, X_ST, 8, R11, &lb, false);
		x64_bigneg_if(e, q, n, x64_bigslot(e, sa));
		x64_bigneg_if(e, d, n, x64_bigslot(e, sb));
	}
	int r = x64_treg(e), count = x64_treg(e);
	uint32_t loop = wk_newlabel(e), less = wk_newlabel(e);
	x64_movi(e, count, n*64);
	x64_label(e, loop);
	for (unsigned i = 0; i < n; i++) {
		lval l = x64_wat(q, (int)i*8);
		x64_mem(e, X_LD, 8, r, &l, false);
		x64_op3(e, i ? X_ADC : X_ADD, 8, r, r, r);
		x64_mem(e, X_ST, 8, r, &l, false);
	}
	for (unsigned i = 0; i <= n; i++) {
		lval l = x64_wat(rem, (int)i*8);
		x64_mem(e, X_LD, 8, r, &l, false);
		x64_op3(e, X_ADC, 8, r, r, r);
		x64_mem(e, X_ST, 8, r, &l, false);
	}
	for (unsigned i = 0; i <= n; i++) {
		lval al = x64_wat(rem, (int)i*8), bl = x64_wat(d, (int)i*8);
		x64_mem(e, X_LD, 8, r, &al, false);
		if (i < n) x64_mem(e, X_LD, 8, R11, &bl, false);
		else x64_movi(e, R11, 0);
		x64_op3(e, i ? X_SBB : X_SUB, 8, r, r, R11);
	}
	rec *branch = x64_put(e, X_JCC);
	branch->cond = CC_B;
	branch->label = less;
	for (unsigned i = 0; i <= n; i++) {
		lval al = x64_wat(rem, (int)i*8), bl = x64_wat(d, (int)i*8);
		x64_mem(e, X_LD, 8, r, &al, false);
		if (i < n) x64_mem(e, X_LD, 8, R11, &bl, false);
		else x64_movi(e, R11, 0);
		x64_op3(e, i ? X_SBB : X_SUB, 8, r, r, R11);
		x64_mem(e, X_ST, 8, r, &al, false);
	}
	lval qlo = x64_wat(q, 0);
	x64_mem(e, X_LD, 8, r, &qlo, false);
	x64_op3(e, X_ORI, 8, r, r, XNOREG)->imm = 1;
	x64_mem(e, X_ST, 8, r, &qlo, false);
	x64_label(e, less);
	x64_op3(e, X_SUBI, 8, count, count, XNOREG)->imm = 1;
	branch = x64_put(e, X_JCC);
	branch->cond = CC_NE;
	branch->label = loop;
	x64_tfree(e, count);
	x64_tfree(e, r);
	x64_tfree(e, d);
	int result = op == T_SLASH ? q : rem;
	x64_tfree(e, op == T_SLASH ? rem : q);
	if (wk_signed(e->c, t)) {
		val sign = x64_bigslot(e, sa);
		if (op == T_SLASH) sign = x64_bin(e, T_CARET, 8, false, sign, x64_bigslot(e, sb));
		x64_bigneg_if(e, result, n, sign);
	}
	x64_bignorm(e, result, t);
	return x64_wv(result);
}

VPRIV val x64_bigbin(ecx *e, int op, qty t, val a, val b)
{
	if (a.k == V_CONST) a = x64_bigconst(e, t, NULL, a.i);
	if (b.k == V_CONST && op != T_LESSLESS && op != T_GREATERGREATER)
		b = x64_bigconst(e, t, NULL, b.i);
	if (op == T_SLASH || op == T_PERCENT)
		return x64_bigdiv(e, op, t, a, b);
	if (op == T_LESSLESS || op == T_GREATERGREATER)
		return x64_bigshift(e, op, t, a, b);
	cc *c = e->c;
	unsigned n = (unsigned)tsize(c, t) / 8;
	int ra = x64_toreg(e, &a, 8), rb = x64_toreg(e, &b, 8);
	int rd = x64_bignew(e, t), r = x64_treg(e);
	if (op == T_STAR) {
		int carry = x64_treg(e);
		x64_zero(e, rd, tsize(c, t));
		for (unsigned i = 0; i < 2*n; i++) {
			x64_movi(e, carry, 0);
			for (unsigned j = 0; j < 2*n-i; j++) {
				lval al = x64_wat(ra, (int)i * 4), bl = x64_wat(rb, (int)j * 4);
				lval dl = x64_wat(rd, (int)(i+j) * 4);
				x64_mem(e, X_LD, 4, r, &al, false);
				x64_mem(e, X_LD, 4, R11, &bl, false);
				x64_op3(e, X_IMUL, 8, r, r, R11);
				x64_mem(e, X_LD, 4, R11, &dl, false);
				x64_op3(e, X_ADD, 8, r, r, R11);
				x64_op3(e, X_ADD, 8, r, r, carry);
				x64_mem(e, X_ST, 4, r, &dl, false);
				x64_op3(e, X_SHRI, 8, carry, r, XNOREG)->imm = 32;
			}
		}
		x64_tfree(e, carry);
	} else {
		int first = op == T_PLUS ? X_ADD : op == T_MINUS ? X_SUB :
			op == T_AMP ? X_AND : op == T_PIPE ? X_OR : op == T_CARET ? X_XOR : 0;
		int next = op == T_PLUS ? X_ADC : op == T_MINUS ? X_SBB : first;
		if (!first) efatal(e, 0, "wide _BitInt operation not implemented");
		for (unsigned i = 0; i < n; i++) {
			lval al = x64_wat(ra, (int)i * 8), bl = x64_wat(rb, (int)i * 8), dl = x64_wat(rd, (int)i * 8);
			x64_mem(e, X_LD, 8, r, &al, false);
			x64_mem(e, X_LD, 8, R11, &bl, false);
			x64_op3(e, i ? next : first, 8, r, r, R11);
			x64_mem(e, X_ST, 8, r, &dl, false);
		}
	}
	x64_tfree(e, r);
	x64_tfree(e, ra);
	x64_tfree(e, rb);
	x64_bignorm(e, rd, t);
	return x64_wv(rd);
}

VPRIV val x64_bigcmp(ecx *e, int op, qty t, val a, val b)
{
	if (a.k == V_CONST) a = x64_bigconst(e, t, NULL, a.i);
	if (b.k == V_CONST) b = x64_bigconst(e, t, NULL, b.i);
	unsigned n = (unsigned)tsize(e->c, t) / 8;
	int ra = x64_toreg(e, &a, 8), rb = x64_toreg(e, &b, 8), r = x64_treg(e), cc;
	bool eq = op == T_EQUALEQUAL || op == T_EXCLAIMEQ;
	bool swap = op == T_GREATER || op == T_LESSEQ;
	int u = eq ? x64_treg(e) : -1;
	if (eq) x64_movi(e, r, 0);
	for (unsigned i = 0; i < n; i++) {
		lval al = x64_wat(swap ? rb : ra, (int)i * 8), bl = x64_wat(swap ? ra : rb, (int)i * 8);
		x64_mem(e, X_LD, 8, eq ? u : r, &al, false);
		x64_mem(e, X_LD, 8, R11, &bl, false);
		x64_op3(e, eq ? X_XOR : i ? X_SBB : X_SUB, 8, eq ? u : r, eq ? u : r, R11);
		if (eq) x64_op3(e, X_OR, 8, r, r, u);
	}
	if (eq) {
		x64_tfree(e, u);
		cc = op == T_EQUALEQUAL ? CC_E : CC_NE;
	} else {
		bool s = wk_signed(e->c, t);
		cc = op == T_LESS || op == T_GREATER ? (s ? CC_L : CC_B) : (s ? CC_GE : CC_AE);
	}
	x64_tfree(e, ra);
	x64_tfree(e, rb);
	x64_op3(e, X_SETCC, 4, r, XNOREG, XNOREG)->cond = (uint8_t)cc;
	return x64_wv(r);
}

VPRIV val x64_bigcvt(ecx *e, val v, qty from, qty to)
{
	cc *c = e->c;
	bool fb = em_bigint(c, from), tb = em_bigint(c, to);
	if (KIND(c, to) == TY_VOID) {
		wk_free(e, &v);
		return vconst(0);
	}
	if (is_flt(c, from) || is_flt(c, to))
		efatal(e, 0, "wide _BitInt floating conversion not implemented");
	if (tb) {
		if (!fb && !((KIND(c, from) == TY_INT128) || (KIND(c, from) == TY_UINT128)))
			v = x64_wcvt(e, v, from, prim(TY_INT128));
		unsigned src = fb ? (unsigned)tsize(c, from) / 8 : 2, dst = (unsigned)tsize(c, to) / 8;
		int rs = x64_waddr(e, &v), rd = x64_bignew(e, to);
		for (unsigned i = 0; i < dst; i++) {
			lval d = x64_wat(rd, (int)i * 8);
			if (i < src) {
				lval s = x64_wat(rs, (int)i * 8);
				x64_mem(e, X_LD, 8, R11, &s, false);
			} else if (i == src) {
				if (wk_signed(c, from)) x64_op3(e, X_SARI, 8, R11, R11, XNOREG)->imm = 63;
				else x64_movi(e, R11, 0);
			}
			x64_mem(e, X_ST, 8, R11, &d, false);
		}
		x64_tfree(e, rs);
		x64_bignorm(e, rd, to);
		return x64_wv(rd);
	}
	int rs = x64_toreg(e, &v, 8), rd;
	if (KIND(c, to) == TY_BOOL) {
		rd = x64_treg(e);
		x64_movi(e, rd, 0);
		for (unsigned i = 0; i < tsize(c, from)/8; i++) {
			lval l = x64_wat(rs, (int)i*8);
			x64_mem(e, X_LD, 8, R11, &l, false);
			x64_op3(e, X_OR, 8, rd, rd, R11);
		}
		x64_op3(e, X_SETCC, 4, rd, XNOREG, XNOREG)->cond = CC_NE;
	} else if (tsize(c, to) > 8) {
		rd = x64_wnew(e, NULL);
		x64_copy(e, rd, rs, 16);
	} else {
		lval l = x64_wat(rs, 0);
		rd = x64_treg(e);
		x64_mem(e, X_LD, (int)tsize(c, to), rd, &l, wk_signed(c, to));
	}
	x64_tfree(e, rs);
	return x64_wv(rd);
}
