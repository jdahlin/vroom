/* vcc code generator, x86-64: floating point (x64impl.h).  float and double
 * in SSE registers (scalar ss/sd); long double on the x87 stack, its values
 * in 16-byte frame slots (the walker holds their addresses, as for records). */
#include "x64impl.h"

VPRIV bool x64_ldmem(void) { return true; }

static bool x64_isld(cc *c, qty t) { return KIND(c, t) == TY_LDOUBLE; }
static int x64_fsz(cc *c, qty t) { return KIND(c, t) == TY_FLOAT ? 4 : 8; }
static val x64_vr(int r) { return (val){ .k = V_REG, .r = (uint8_t)r }; }

/* an x87 instruction, with its memory operand l (NULL: none) */
static void x64_x87(ecx *e, int xf, const lval *l, int sz)
{
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

/* Compiler-runtime conversion helpers take/return half bits in xmm0.
   Use the regular ABI call path so every live temporary survives. */
static val x64_half_call(ecx *e, int name, val v, qty from, qty to)
{
	wkarg a;
	lval slot = x64_slot(e, 16);

	x64_classify(e, from, &a);
	a.slot = (int32_t)slot.off;
	if (x64_isld(e->c, from)) {
		lval src = x64_at(x64_toreg(e, &v, 8));

		x64_x87ld(e, &src);
		x64_x87st(e, &slot);
	} else
		x64_store(e, v.r, &slot, wk_size(e->c, from));
	x64_tfree(e, v.r);
	return x64_call(e, FIXED(name), NULL, &a, 1, false, NULL, to);
}

VPRIV val x64_fconst(ecx *e, uint64_t bits, int size)
{
	int r = x64_ftreg(e);

	x64_movi(e, R11, (int64_t)bits);
	x64_op3(e, X_MOV, size <= 4 ? 4 : 8, r, R11, XNOREG);
	return x64_vr(r);
}

VPRIV val x64_fbin(ecx *e, int op, qty t, val a, val b)
{
	cc *c = e->c;
	int k = op == T_PLUS ? 0 : op == T_MINUS ? 1 : op == T_STAR ? 2 : 3;

	if (KIND(c, t) == TY_FLOAT16 || KIND(c, t) == TY_BFLOAT16) {
		a = x64_cvt(e, a, t, prim(TY_FLOAT));
		b = x64_cvt(e, b, t, prim(TY_FLOAT));
		return x64_cvt(e, x64_fbin(e, op, prim(TY_FLOAT), a, b), prim(TY_FLOAT), t);
	}

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

	if (x64_isld(c, t)) {
		int ra = x64_toreg(e, &a, 8), rd = x64_ldnew(e);
		lval ma = x64_at(ra), md = x64_at(rd);

		x64_x87(e, XF_LDT, &ma, 10);
		x64_x87(e, XF_CHS, NULL, 0);
		x64_x87(e, XF_STPT, &md, 10);
		x64_tfree(e, ra);
		return x64_vr(rd);
	}
	x64_movi(e, R11, (KIND(c, t) == TY_FLOAT16 || KIND(c, t) == TY_BFLOAT16) ? 0x8000 : x64_fsz(c, t) == 4 ? (int64_t)0x80000000 : INT64_MIN);
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

	if (KIND(c, t) == TY_FLOAT16 || KIND(c, t) == TY_BFLOAT16) {
		a = x64_cvt(e, a, t, prim(TY_FLOAT));
		b = x64_cvt(e, b, t, prim(TY_FLOAT));
		return x64_fcmp(e, op, prim(TY_FLOAT), a, b);
	}
	if (op == WK_FCMP_UNORDERED)
		cc = CC_P;
	else if (op == WK_FCMP_ORDERED_NE)
		cc = CC_NE;
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

	if (KIND(c, from) == TY_BFLOAT16) {
		if (KIND(c, to) == TY_BFLOAT16) return v;
		v = x64_half_call(e, FN_EXTENDBFSF2, v, from, prim(TY_FLOAT));
		return x64_cvt(e, v, prim(TY_FLOAT), to);
	}
	if (KIND(c, to) == TY_BFLOAT16) {
		if (KIND(c, from) == TY_FLOAT16)
			v = x64_cvt(e, v, from, prim(TY_FLOAT)), from = prim(TY_FLOAT);
		if (!ff)
			v = x64_cvt(e, v, from, prim(TY_DOUBLE)), from = prim(TY_DOUBLE);
		return x64_half_call(e, x64_isld(c, from) ? FN_TRUNCXFBF2 : KIND(c, from) == TY_FLOAT ? FN_TRUNCSFBF2 : FN_TRUNCDFBF2, v, from, to);
	}
	if (KIND(c, from) == TY_FLOAT16) {
		if (KIND(c, to) == TY_FLOAT16)
			return v;
		v = x64_half_call(e, FN_EXTENDHFSF2, v, from, prim(TY_FLOAT));
		return x64_cvt(e, v, prim(TY_FLOAT), to);
	}
	if (KIND(c, to) == TY_FLOAT16) {
		if (!ff)
			v = x64_cvt(e, v, from, prim(TY_DOUBLE)), from = prim(TY_DOUBLE);
		return x64_half_call(e, x64_isld(c, from) ? FN_TRUNCXFHF2 : KIND(c, from) == TY_FLOAT ? FN_TRUNCSFHF2 : FN_TRUNCDFHF2, v, from, to);
	}
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
