/* vcc code generator, x86-64: floating point (x64impl.h).  float and double
 * in SSE registers (scalar ss/sd); long double on the x87 stack, its values
 * in 16-byte frame slots (the walker holds their addresses, as for records). */
#include "x64impl.h"

VPRIV bool x64_ldmem(void) { return true; }

static bool x64_isld(cc *c, qty t) { return KIND(c, t) == TY_LDOUBLE; }
static bool x64_isq(cc *c, qty t) { return KIND(c, t) == TY_FLOAT128; }
static int x64_fsz(cc *c, qty t) { return KIND(c, t) == TY_FLOAT ? 4 : 8; }
static val x64_vr(int r) { return (val){ .k = V_REG, .r = (uint8_t)r }; }

/* an x87 instruction, with its memory operand l (NULL: none) */
static void x64_x87(ecx *e, int xf, const lval *l, int sz)
{
	x64_nofp(e);
	uint32_t ref = 0;
	int base = l ? x64_base(e, l, &ref) : 0;
	rec *i = x64_put(e, X_X87);

	i->cond = (uint8_t)xf;
	i->sz = (uint8_t)sz;
	if (l) {
		i->off = (int32_t)l->off;
		i->r[1] = (uint8_t)base;
		i->ref = ref;
	}
}

VPRIV void x64_x87ld(ecx *e, const lval *l) { x64_x87(e, XF_LDT, l, 10); }
VPRIV void x64_x87st(ecx *e, const lval *l) { x64_x87(e, XF_STPT, l, 10); }

static lval x64_at(int r) { return (lval){ .k = L_REG, .r = (uint8_t)r, .align = 16 }; }
static lval x64_slot(ecx *e, uint64_t n) { return (lval){ .k = L_FRAME, .off = wk_slot(e, n, n >= 16 ? 16 : 8), .align = 8 }; }

/* a new long double object: its address in a temporary */
static int x64_ldnew(ecx *e)
{
	lval l = x64_slot(e, 16);

	return x64_laddr(e, &l);
}

static void x64_jcc(ecx *e, int cc, uint32_t l)
{
	rec *i = x64_put(e, X_JCC);

	i->cond = (uint8_t)cc;
	i->label = l;
}

static val x64_qbin(ecx *e, int k, val a, val b);
static val x64_qcmp(ecx *e, int op, val a, val b);
static val x64_qcvt(ecx *e, val v, qty from, qty to);
static val x64_qzero(ecx *e);

VPRIV val x64_fconst(ecx *e, uint64_t bits, int size)
{
	x64_nofp(e);
	int r = x64_ftreg(e);

	x64_movi(e, R11, (int64_t)bits);
	x64_op3(e, X_MOV, size == 4 ? 4 : 8, r, R11, XNOREG);
	return x64_vr(r);
}

VPRIV val x64_fbin(ecx *e, int op, qty t, val a, val b)
{
	cc *c = e->c;
	int k = op == T_PLUS ? 0 : op == T_MINUS ? 1 : op == T_STAR ? 2 : 3;

	if (x64_isq(c, t))
		return x64_qbin(e, k, a, b);
	if (x64_isld(c, t)) {
		static const int xf[4] = { XF_ADD, XF_SUB, XF_MUL, XF_DIV };
		int ra = x64_toreg(e, &a, 8), rb = x64_toreg(e, &b, 8), rd = x64_ldnew(e);
		lval ma = x64_at(ra), mb = x64_at(rb), md = x64_at(rd);

		x64_x87(e, XF_LDT, &ma, 10);
		x64_x87(e, XF_LDT, &mb, 10);
		x64_x87(e, xf[k], NULL, 0);
		x64_x87(e, XF_STPT, &md, 10);
		x64_tfree(e, ra);
		x64_tfree(e, rb);
		return x64_vr(rd);
	}
	x64_op3(e, X_FADD + k, x64_fsz(c, t), a.r, a.r, b.r);
	x64_tfree(e, b.r);
	return a;
}

VPRIV val x64_fneg(ecx *e, qty t, val a)
{
	cc *c = e->c;

	if (x64_isq(c, t)) {
		/* the sign bit flipped in a copy */
		int ra = x64_toreg(e, &a, 8), rd = x64_ldnew(e);

		x64_copy(e, rd, ra, 16);
		x64_tfree(e, ra);
		{
			lval hi = x64_at(rd);

			hi.off = 15;
			x64_mem(e, X_LD, 1, R11, &hi, false);
			x64_op3(e, X_XORI, 4, R11, R11, XNOREG)->imm = 0x80;
			x64_mem(e, X_ST, 1, R11, &hi, false);
		}
		return x64_vr(rd);
	}

	if (x64_isld(c, t)) {
		int ra = x64_toreg(e, &a, 8), rd = x64_ldnew(e);
		lval ma = x64_at(ra), md = x64_at(rd);

		x64_x87(e, XF_LDT, &ma, 10);
		x64_x87(e, XF_CHS, NULL, 0);
		x64_x87(e, XF_STPT, &md, 10);
		x64_tfree(e, ra);
		return x64_vr(rd);
	}
	x64_movi(e, R11, x64_fsz(c, t) == 4 ? (int64_t)0x80000000 : INT64_MIN);
	x64_op3(e, X_MOV, x64_fsz(c, t), XMM15, R11, XNOREG);
	x64_op3(e, X_FXOR, 8, a.r, a.r, XMM15);
	return a;
}

/* a op b as 0 or 1: false when unordered, but for != */
VPRIV val x64_fcmp(ecx *e, int op, qty t, val a, val b)
{
	cc *c = e->c;
	bool swap = op == T_LESS || op == T_LESSEQ;	/* a < b is b > a: "above" is false for NaN */
	int cc = op == T_LESS || op == T_GREATER ? CC_A : CC_AE;
	int rd;

	if (x64_isq(c, t))
		return x64_qcmp(e, op, a, b);
	if (x64_isld(c, t)) {
		int ra = x64_toreg(e, &a, 8), rb = x64_toreg(e, &b, 8);
		lval ma = x64_at(ra), mb = x64_at(rb);

		/* fucomip compares st0 with st1: st0 is the left of the comparison */
		x64_x87(e, XF_LDT, swap ? &ma : &mb, 10);
		x64_x87(e, XF_LDT, swap ? &mb : &ma, 10);
		x64_x87(e, XF_UCOMIP, NULL, 0);
		x64_x87(e, XF_POP, NULL, 0);
		x64_tfree(e, ra);
		x64_tfree(e, rb);
	} else {
		if (swap)
			x64_op3(e, X_FCMP, x64_fsz(c, t), XNOREG, b.r, a.r);
		else
			x64_op3(e, X_FCMP, x64_fsz(c, t), XNOREG, a.r, b.r);
		x64_tfree(e, a.r);
		x64_tfree(e, b.r);
	}
	rd = x64_treg(e);
	if (op == T_EQUALEQUAL || op == T_EXCLAIMEQ)
		x64_op3(e, X_FSETCC, 4, rd, XNOREG, XNOREG)->cond = op == T_EXCLAIMEQ;
	else
		x64_op3(e, X_SETCC, 4, rd, XNOREG, XNOREG)->cond = (uint8_t)cc;
	return x64_vr(rd);
}


/* ---------------- __float128: libgcc calls ---------------- */

/*
 * An argument or result of a soft-float call: Q a __float128 object (its
 * address in r; one xmm register), X an x87 long double object (address;
 * 16 bytes on the stack in, st(0) out), D/F a double/float in xmm r, I an
 * integer in r (64 bits), W an __int128 object (address; rdi:rsi in,
 * rax:rdx out).
 */
enum { TQ, TX, TD, TF, TI, TW };
typedef struct { int k, r; } tfarg;

static val x64_tfcall(ecx *e, int fn, const tfarg *a, int n, int rk)
{
	x64_nofp(e);
	xst *X = e->x64;
	uint32_t saved, fsaved;
	int nx = 0, ng = 0, pad, stk = 0, res;
	lval out = { 0 };
	rec *i;

	for (int k = 0; k < n; k++)
		x64_tfree(e, a[k].r);	/* (the values stay in their registers until loaded below) */
	saved = X->live & X_TEMPS, fsaved = X->live & X_FTEMPS;
	if (rk == TQ || rk == TX || rk == TW)
		out = x64_slot(e, 16);
	e->calls = true;
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
	for (int k = 0; k < n; k++)
		stk += a[k].k == TX ? 16 : 0;
	pad = X->pushes & 1 ? 8 : 0;
	if (pad + stk)
		x64_put(e, X_ADJSP)->imm = -(pad + stk);
	for (int k = 0, so = 0; k < n; k++) {
		lval m = x64_at(a[k].r);

		switch (a[k].k) {
		case TQ:
			x64_mem(e, X_LD, 16, XMM0 + nx++, &m, false);
			break;
		case TX:
			for (int w = 0; w < 2; w++) {
				lval s = m, d = { .k = L_REG, .r = RSP, .off = so + 8 * w };

				s.off = 8 * w;
				x64_mem(e, X_LD, 8, R11, &s, false);
				x64_mem(e, X_ST, 8, R11, &d, false);
			}
			so += 16;
			break;
		case TD: case TF:
			x64_op3(e, X_MOV, 8, XMM0 + nx++, a[k].r, XNOREG);
			break;
		case TI:
			x64_mov(e, x64_argreg(ng++), a[k].r);
			break;
		case TW: {
			lval hi = m;

			hi.off = 8;
			x64_mem(e, X_LD, 8, x64_argreg(ng + 1), &hi, false);
			x64_mem(e, X_LD, 8, x64_argreg(ng), &m, false);
			ng += 2;
			break;
		}
		}
	}
	i = x64_put(e, X_CALL);
	i->ref = FIXED(fn);
	switch (rk) {
	case TQ:
		x64_mem(e, X_ST, 16, XMM0, &out, false);
		break;
	case TX:
		x64_x87st(e, &out);
		break;
	case TW: {
		lval hi = out;

		hi.off += 8;
		x64_mem(e, X_ST, 8, RAX, &out, false);
		x64_mem(e, X_ST, 8, RDX, &hi, false);
		break;
	}
	case TD: case TF:
		x64_op3(e, X_MOV, 8, XMM15, XMM0, XNOREG);
		break;
	default:
		x64_mov(e, R11, RAX);
		break;
	}
	if (pad + stk)
		x64_put(e, X_ADJSP)->imm = pad + stk;
	for (int q = XMM15 - 1; q >= XMM8; q--)
		if (fsaved & (1u << q)) {
			lval s2 = { .k = L_REG, .r = RSP };

			x64_mem(e, X_LD, 8, q, &s2, false);
			x64_put(e, X_ADJSP)->imm = 8;
			X->pushes--;
		}
	for (int q = 15; q >= 0; q--)
		if (saved & (1u << q)) {
			x64_op3(e, X_POP, 8, q, XNOREG, XNOREG);
			X->pushes--;
		}
	if (rk == TQ || rk == TX || rk == TW)
		return x64_vr(x64_laddr(e, &out));
	if (rk == TD || rk == TF) {
		res = x64_ftreg(e);
		x64_op3(e, X_MOV, 8, res, XMM15, XNOREG);
		return x64_vr(res);
	}
	res = x64_treg(e);
	x64_mov(e, res, R11);
	return x64_vr(res);
}

/* a op b on __float128 objects */
static val x64_qbin(ecx *e, int k, val a, val b)
{
	static const int fn[4] = { FN_ADDTF3, FN_SUBTF3, FN_MULTF3, FN_DIVTF3 };
	tfarg ar[2] = { { TQ, x64_toreg(e, &a, 8) }, { TQ, x64_toreg(e, &b, 8) } };

	return x64_tfcall(e, fn[k], ar, 2, TQ);
}

/* a op b on __float128 objects as 0 or 1 (false when unordered, but for !=) */
static val x64_qcmp(ecx *e, int op, val a, val b)
{
	int fn = op == T_EQUALEQUAL ? FN_EQTF2 : op == T_EXCLAIMEQ ? FN_NETF2 : op == T_LESS ? FN_LTTF2 :
		 op == T_LESSEQ ? FN_LETF2 : op == T_GREATER ? FN_GTTF2 : FN_GETF2;
	int cc = op == T_EQUALEQUAL ? CC_E : op == T_EXCLAIMEQ ? CC_NE : op == T_LESS ? CC_L :
		 op == T_LESSEQ ? CC_LE : op == T_GREATER ? CC_G : CC_GE;
	tfarg ar[2] = { { TQ, x64_toreg(e, &a, 8) }, { TQ, x64_toreg(e, &b, 8) } };
	val r = x64_tfcall(e, fn, ar, 2, TI);

	x64_op3(e, X_TEST, 4, XNOREG, r.r, XNOREG);
	x64_op3(e, X_SETCC, 4, r.r, XNOREG, XNOREG)->cond = (uint8_t)cc;
	return r;
}

/* a new __float128 zero */
static val x64_qzero(ecx *e)
{
	lval l = x64_slot(e, 16);
	int r = x64_laddr(e, &l);

	x64_zero(e, r, 16);
	return x64_vr(r);
}

/* conversions with a __float128 side (to: not _Bool) */
static val x64_qcvt(ecx *e, val v, qty from, qty to)
{
	cc *c = e->c;
	bool fq = x64_isq(c, from);
	qty o = fq ? to : from;	/* the other side */
	int os = wk_size(c, o), k = KIND(c, o);
	bool sg = wk_signed(c, o);

	if (fq && x64_isq(c, to))
		return v;
	if (is_flt(c, o)) {
		tfarg a = { k == TY_LDOUBLE ? TX : fq ? TQ : k == TY_FLOAT ? TF : TD,
			    v.k == V_REG ? v.r : x64_toreg(e, &v, 8) };

		if (fq)
			return x64_tfcall(e, k == TY_LDOUBLE ? FN_TRUNCTFXF2 : k == TY_FLOAT ? FN_TRUNCTFSF2 : FN_TRUNCTFDF2,
					  &(tfarg){ TQ, a.r }, 1, k == TY_LDOUBLE ? TX : k == TY_FLOAT ? TF : TD);
		return x64_tfcall(e, k == TY_LDOUBLE ? FN_EXTENDXFTF2 : k == TY_FLOAT ? FN_EXTENDSFTF2 : FN_EXTENDDFTF2,
				  &a, 1, TQ);
	}
	if (k == TY_INT128 || k == TY_UINT128) {
		tfarg a = { fq ? TQ : TW, x64_toreg(e, &v, 8) };

		return x64_tfcall(e, fq ? (sg ? FN_FIXTFTI : FN_FIXUNSTFTI) : (sg ? FN_FLOATTITF : FN_FLOATUNTITF), &a, 1, fq ? TW : TQ);
	}
	if (fq) {
		/* to an integer: through 32 or 64 bits, unsigned 32 by the 64-bit fix */
		tfarg a = { TQ, x64_toreg(e, &v, 8) };
		int fn = os == 8 ? (sg ? FN_FIXTFDI : FN_FIXUNSTFDI) : os == 4 && !sg ? FN_FIXTFDI : FN_FIXTFSI;
		val r = x64_tfcall(e, fn, &a, 1, TI);

		if (os < 8)
			x64_ext(e, r.r, os, sg);
		return r;
	}
	{
		int r = x64_toreg(e, &v, 8);
		tfarg a = { TI, r };

		if (os < 8)
			x64_ext(e, r, os, sg);
		return x64_tfcall(e, os < 8 ? FN_FLOATDITF : sg ? FN_FLOATDITF : FN_FLOATUNDITF, &a, 1, TQ);
	}
}

/* ---------------- conversions ---------------- */

/* an integer in a register (size bytes, sgn) to the float of fs bytes */
static val x64_i2f(ecx *e, int r, int size, bool sgn, int fs)
{
	int x = x64_ftreg(e);

	if (sgn || size < 8) {
		if (!sgn && size == 4)
			x64_ext(e, r, 4, false);	/* (zero-extended: converted as 64 bits) */
		x64_op3(e, X_CVTI2F, fs, x, r, XNOREG)->sh = (uint8_t)(sgn && size <= 4 ? 4 : 8);
	} else {
		/* unsigned 64 bits: halved (keeping the low bit) and doubled when the top bit is set */
		uint32_t lbig = wk_newlabel(e), ldone = wk_newlabel(e);

		x64_op3(e, X_TEST, 8, XNOREG, r, XNOREG);
		x64_jcc(e, CC_S, lbig);
		x64_op3(e, X_CVTI2F, fs, x, r, XNOREG)->sh = 8;
		x64_jump(e, ldone);
		x64_label(e, lbig);
		x64_mov(e, R11, r);
		x64_op3(e, X_SHRI, 8, R11, R11, XNOREG)->imm = 1;
		x64_op3(e, X_ANDI, 8, r, r, XNOREG)->imm = 1;
		x64_op3(e, X_OR, 8, R11, R11, r);
		x64_op3(e, X_CVTI2F, fs, x, R11, XNOREG)->sh = 8;
		x64_op3(e, X_FADD, fs, x, x, x);
		x64_label(e, ldone);
	}
	x64_tfree(e, r);
	return x64_vr(x);
}

/* a float of fs bytes in x to an integer of ts bytes */
static val x64_f2i(ecx *e, int x, int fs, int ts, bool sgn)
{
	int r = x64_treg(e);

	if (sgn || ts < 8) {
		x64_op3(e, X_CVTF2I, ts > 4 || !sgn ? 8 : 4, r, x, XNOREG)->sh = (uint8_t)fs;
	} else {
		/* unsigned 64 bits: values from 2^63 up are converted less 2^63, the top bit set after */
		uint32_t lbig = wk_newlabel(e), ldone = wk_newlabel(e);

		x64_movi(e, R11, fs == 4 ? 0x5f000000 : 0x43e0000000000000);
		x64_op3(e, X_MOV, fs, XMM15, R11, XNOREG);
		x64_op3(e, X_FCMP, fs, XNOREG, x, XMM15);
		x64_jcc(e, CC_AE, lbig);
		x64_op3(e, X_CVTF2I, 8, r, x, XNOREG)->sh = (uint8_t)fs;
		x64_jump(e, ldone);
		x64_label(e, lbig);
		x64_op3(e, X_FSUB, fs, x, x, XMM15);
		x64_op3(e, X_CVTF2I, 8, r, x, XNOREG)->sh = (uint8_t)fs;
		x64_movi(e, R11, INT64_MIN);
		x64_op3(e, X_XOR, 8, r, r, R11);
		x64_label(e, ldone);
	}
	x64_tfree(e, x);
	if (ts < 4)
		x64_ext(e, r, ts, sgn);
	return x64_vr(r);
}

/* v of type from as type to, a float on at least one side (to may be _Bool) */
VPRIV val x64_cvt(ecx *e, val v, qty from, qty to)
{
	cc *c = e->c;
	bool ff = is_flt(c, from), tf = is_flt(c, to), fl = x64_isld(c, from), tl = x64_isld(c, to);
	int fs = wk_size(c, from), ts = wk_size(c, to);

	if (x64_isq(c, from) && KIND(c, to) == TY_BOOL)
		return x64_qcmp(e, T_EXCLAIMEQ, v, x64_qzero(e));
	if (x64_isq(c, from) || x64_isq(c, to))
		return x64_qcvt(e, v, from, to);
	if (KIND(c, to) == TY_BOOL) {
		int rd;

		if (fl) {
			int ra = x64_toreg(e, &v, 8);
			lval m = x64_at(ra);

			x64_x87(e, XF_LDZ, NULL, 0);
			x64_x87(e, XF_LDT, &m, 10);
			x64_x87(e, XF_UCOMIP, NULL, 0);
			x64_x87(e, XF_POP, NULL, 0);
			x64_tfree(e, ra);
		} else {
			x64_op3(e, X_FXOR, 8, XMM15, XMM15, XMM15);
			x64_op3(e, X_FCMP, x64_fsz(c, from), XNOREG, v.r, XMM15);
			x64_tfree(e, v.r);
		}
		rd = x64_treg(e);
		x64_op3(e, X_FSETCC, 4, rd, XNOREG, XNOREG)->cond = 1;
		return x64_vr(rd);
	}
	if (ff && tf) {
		if (fl == tl) {
			if (!fl && fs != ts)
				x64_op3(e, X_CVTF2F, ts, v.r, v.r, XNOREG);
			return v;
		}
		if (tl) {
			/* float, double -> long double: through memory */
			lval t = x64_slot(e, 8);
			int rd = x64_ldnew(e);
			lval md = x64_at(rd);

			x64_mem(e, X_ST, fs, v.r, &t, false);
			x64_tfree(e, v.r);
			x64_x87(e, XF_LD, &t, fs);
			x64_x87(e, XF_STPT, &md, 10);
			return x64_vr(rd);
		}
		{
			int ra = x64_toreg(e, &v, 8), x;
			lval m = x64_at(ra), t = x64_slot(e, 8);

			x64_x87(e, XF_LDT, &m, 10);
			x64_x87(e, XF_STP, &t, ts);
			x64_tfree(e, ra);
			x = x64_ftreg(e);
			x64_mem(e, X_LD, ts, x, &t, false);
			return x64_vr(x);
		}
	}
	if (tf) {
		bool sg = wk_signed(c, from);

		if (v.k == V_CONST && !tl) {
			/* folded as C converts (round to nearest) */
			double d = sg ? (double)v.i : (double)(u128)(uint64_t)v.i;
			float f = sg ? (float)v.i : (float)(uint64_t)v.i;
			uint64_t bits = 0;

			if (ts == 4)
				memcpy(&bits, &f, 4);
			else
				memcpy(&bits, &d, 8);
			return x64_fconst(e, bits, ts);
		}
		if (tl) {
			int r = x64_toreg(e, &v, 8), rd;
			lval t = x64_slot(e, 8), md;
			uint32_t ldone = wk_newlabel(e);

			if (fs < 8)
				x64_ext(e, r, fs, sg);
			x64_mem(e, X_ST, 8, r, &t, false);
			x64_x87(e, XF_ILD, &t, 8);
			if (!sg && fs == 8) {
				/* fild took the top bit as a sign: add 2^64 back */
				lval k = x64_slot(e, 8);

				x64_op3(e, X_TEST, 8, XNOREG, r, XNOREG);
				x64_jcc(e, CC_NS, ldone);
				x64_movi(e, R11, 0x5f800000);
				x64_mem(e, X_ST, 4, R11, &k, false);
				x64_x87(e, XF_LD, &k, 4);
				x64_x87(e, XF_ADD, NULL, 0);
				x64_label(e, ldone);
			}
			x64_tfree(e, r);
			rd = x64_ldnew(e);
			md = x64_at(rd);
			x64_x87(e, XF_STPT, &md, 10);
			return x64_vr(rd);
		}
		{
			int r = x64_toreg(e, &v, 8);

			if (fs < 4)
				x64_ext(e, r, fs, sg);
			return x64_i2f(e, r, fs < 4 ? 4 : fs, sg, ts);
		}
	}
	/* float -> integer */
	if (fl) {
		/* truncation: the control word's rounding set to zero around fistp */
		int ra = x64_toreg(e, &v, 8), r;
		lval m = x64_at(ra), cw = x64_slot(e, 8), cw2 = x64_slot(e, 8), t = x64_slot(e, 8);

		x64_x87(e, XF_STCW, &cw, 2);
		x64_mem(e, X_LD, 2, R11, &cw, false);
		x64_op3(e, X_ORI, 4, R11, R11, XNOREG)->imm = 0xc00;
		x64_mem(e, X_ST, 2, R11, &cw2, false);
		x64_x87(e, XF_LDCW, &cw2, 2);
		x64_x87(e, XF_LDT, &m, 10);
		x64_x87(e, XF_ISTP, &t, 8);
		x64_x87(e, XF_LDCW, &cw, 2);
		x64_tfree(e, ra);
		r = x64_treg(e);
		x64_mem(e, X_LD, 8, r, &t, false);
		if (ts < 8)
			x64_ext(e, r, ts, wk_signed(c, to));
		return x64_vr(r);
	}
	return x64_f2i(e, v.r, fs, ts, wk_signed(c, to));
}
