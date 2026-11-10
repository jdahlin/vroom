/* vcc code generator: integer helpers, truth tests, casts, arithmetic */
#include "../../../emit/emitimpl.h"

/* ---------------- integer helpers ---------------- */

/* extend the low `from` bytes of r to 64 bits */
VPRIV void extend(ecx *e, int rd, int rs, int from, bool sgn)
{
	switch (from) {
	case 1: opbf(e, sgn ? I_SBFM : I_UBFM, 1, rd, rs, 0, 7); break;
	case 2: opbf(e, sgn ? I_SBFM : I_UBFM, 1, rd, rs, 0, 15); break;
	case 4:
		if (sgn)
			opbf(e, I_SBFM, 1, rd, rs, 0, 31);
		else
			op3(e, I_MOV, 0, rd, rs, 0);
		break;
	default:
		mov(e, 1, rd, rs);
	}
}

VPRIV val fold_int_bin(ecx *e, int op, qty ot, qty rt, i128 l, i128 r, bool *ok)
{
	cc *c = e->c;
	bool sg = is_signed(c, ot);
	i128 v = 0;
	u128 ul = (u128)em_trunc_to(c, l, ot), ur = (u128)em_trunc_to(c, r, ot);

	if (!sg && tsize(c, ot) < 16) {
		ul = (u128)em_trunc_to(c, l, ot) & (((u128)1 << (tsize(c, ot) * 8)) - 1);
		ur = (u128)em_trunc_to(c, r, ot) & (((u128)1 << (tsize(c, ot) * 8)) - 1);
	}
	*ok = true;
	switch (op) {
	case T_PLUS: v = l + r; break;
	case T_MINUS: v = l - r; break;
	case T_STAR: v = (i128)((u128)l * (u128)r); break;
	case T_SLASH:
	case T_PERCENT:
		if (r == 0 || (sg && r == -1)) {
			*ok = false;
			return vnone();
		}
		if (sg)
			v = op == T_SLASH ? l / r : l % r;
		else
			v = (i128)(op == T_SLASH ? ul / ur : ul % ur);
		break;
	case T_LESSLESS:
		if (r < 0 || (uint64_t)r >= tsize(c, rt) * 8) {
			*ok = false;
			return vnone();
		}
		v = (i128)((u128)l << r);
		break;
	case T_GREATERGREATER:
		if (r < 0 || (uint64_t)r >= tsize(c, rt) * 8) {
			*ok = false;
			return vnone();
		}
		v = is_signed(c, rt) ? em_trunc_to(c, l, rt) >> r : (i128)(ul >> r);
		break;
	case T_AMP: v = l & r; break;
	case T_PIPE: v = l | r; break;
	case T_CARET: v = l ^ r; break;
	case T_LESS: v = sg ? em_trunc_to(c, l, ot) < em_trunc_to(c, r, ot) : ul < ur; break;
	case T_LESSEQ: v = sg ? em_trunc_to(c, l, ot) <= em_trunc_to(c, r, ot) : ul <= ur; break;
	case T_GREATER: v = sg ? em_trunc_to(c, l, ot) > em_trunc_to(c, r, ot) : ul > ur; break;
	case T_GREATEREQ: v = sg ? em_trunc_to(c, l, ot) >= em_trunc_to(c, r, ot) : ul >= ur; break;
	case T_EQUALEQUAL: v = ul == ur; break;
	case T_EXCLAIMEQ: v = ul != ur; break;
	default:
		*ok = false;
		return vnone();
	}
	return vconst(em_trunc_to(c, v, rt));
}

static int cmp_cond(int op, bool sg)
{
	switch (op) {
	case T_EQUALEQUAL: return C_EQ;
	case T_EXCLAIMEQ: return C_NE;
	case T_LESS: return sg ? C_LT : C_LO;
	case T_LESSEQ: return sg ? C_LE : C_LS;
	case T_GREATER: return sg ? C_GT : C_HI;
	default: return sg ? C_GE : C_HS;
	}
}

VPRIV bool em_is_cmp(int op)
{
	return op == T_LESS || op == T_LESSEQ || op == T_GREATER || op == T_GREATEREQ || op == T_EQUALEQUAL || op == T_EXCLAIMEQ;
}

/* compare a with b (width x); returns the condition for "op is true" */
VPRIV int emit_cmp(ecx *e, int op, int x, bool sg, val *a, val *b)
{
	int ra = toreg(e, a);

	if (e->E->opt3 && b->k == V_CONST) {
		/* the constant in the compare's width: an immediate (or lsl #12), else materialised at that width */
		int64_t v = x ? (int64_t)b->i : (int32_t)b->i;
		uint64_t m = v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;

		if (m < 4096 || (!(m & 0xfff) && m < (1u << 24))) {
			opi(e, v < 0 ? I_ADDSI : I_SUBSI, x, ZR, ra, (int64_t)m);
		} else {
			int rb = treg(e);

			movi(e, x, rb, v);
			op3(e, I_SUBS, x, ZR, ra, rb);
			tfree(e, rb);
		}
		return cmp_cond(op, sg);
	}
	if (b->k == V_CONST && b->i >= -4095 && b->i <= 4095) {
		if (b->i < 0)
			opi(e, I_ADDSI, x, ZR, ra, (int64_t)-b->i);
		else
			opi(e, I_SUBSI, x, ZR, ra, (int64_t)b->i);
	} else {
		int rb = toreg(e, b);

		op3(e, I_SUBS, x, ZR, ra, rb);
	}
	return cmp_cond(op, sg);
}

/* 128-bit compare: returns the condition */
VPRIV int emit_cmp2(ecx *e, int op, bool sg, val *a, val *b)
{
	toreg2(e, a, sg);
	toreg2(e, b, sg);
	if (op == T_EQUALEQUAL || op == T_EXCLAIMEQ) {
		op3(e, I_EOR, 1, a->r, a->r, b->r);
		op3(e, I_EOR, 1, a->r2, a->r2, b->r2);
		op3(e, I_ORR, 1, a->r, a->r, a->r2);
		op3(e, I_SUBS, 1, ZR, a->r, ZR);
		return op == T_EQUALEQUAL ? C_EQ : C_NE;
	}
	if (op == T_GREATER || op == T_LESSEQ) {
		val t = *a;

		*a = *b;
		*b = t;
		op = op == T_GREATER ? T_LESS : T_GREATEREQ;
	}
	op3(e, I_SUBS, 1, ZR, a->r, b->r);
	op3(e, I_SBCS, 1, ZR, a->r2, b->r2);
	return cmp_cond(op, sg);
}

/* ---------------- -O3: what the last record says about a register ---------------- */

/*
 * The last record runs right before whatever is emitted next, so if it
 * wrote r its result is what r holds: a load extends, a 32-bit operation
 * clears the upper half, cset gives 0 or 1.  EXT_S | n: r is the sign
 * extension of its low n bytes; EXT_Z | n: the zero extension (n 0: r is 0
 * or 1).  0: nothing known (and always below -O3).
 */
enum { EXT_Z = 0x10, EXT_S = 0x20 };

/* record i leaves r 0 or 1 (cset, x & 1) */
static bool prev_bool(const ins *i, int r)
{
	if (i->rd != r)
		return false;
	return (i->op == I_CSINC && !i->sz && i->rn == ZR && i->rm == ZR) || (i->op == I_ANDI && i->imm == 1);
}

VPRIV int last_ext(ecx *e, int r)
{
	const ins *i;

	if (!e->E->opt3 || !e->reach || !e->nin)
		return 0;
	i = &e->in[e->nin - 1];
	switch (i->op) {
	case I_LD: case I_LDLO:
		if (i->rd != r || i->sz == 3)
			return 0;
		return i->cond == 1 ? EXT_S | 1 << i->sz : i->cond == 0 ? EXT_Z | 1 << i->sz : EXT_Z | 4;
	case I_MOVI: {
		uint64_t v = i->sz ? (uint64_t)i->imm : (uint32_t)i->imm;

		if (i->rd != r)
			return 0;
		if (v <= 1)
			return EXT_Z;
		if (v <= 0xff)
			return EXT_Z | 1;
		if (v <= 0xffff)
			return EXT_Z | 2;
		if (v <= 0xffffffffu)
			return EXT_Z | 4;
		if ((int64_t)v >= INT8_MIN)
			return EXT_S | 1;
		if ((int64_t)v >= INT16_MIN)
			return EXT_S | 2;
		return (int64_t)v >= INT32_MIN ? EXT_S | 4 : 0;
	}
	case I_SBFM: case I_UBFM:
		if (i->rd != r)
			return 0;
		if (i->sz && i->imm == 0 && (i->sh == 7 || i->sh == 15 || i->sh == 31))
			return (i->op == I_SBFM ? EXT_S : EXT_Z) | (i->sh + 1) / 8;
		return i->sz ? 0 : EXT_Z | 4;
	case I_CSINC:
		if (i->rd != r)
			return 0;
		if (!i->sz && i->rn == ZR && i->rm == ZR)
			return EXT_Z;	/* cset */
		return i->sz ? 0 : EXT_Z | 4;
	case I_ANDI: case I_EORI:
		if (i->rd != r)
			return 0;
		if (i->imm == 1 && (i->op == I_ANDI || (i->rn == r && e->nin >= 2 && prev_bool(&e->in[e->nin - 2], r))))
			return EXT_Z;	/* x & 1, or !b of a 0/1 b */
		return i->sz ? 0 : EXT_Z | 4;
	default:
		if (i->op >= I_ADD && i->op <= I_REV32 && !i->sz && i->rd == r)
			return EXT_Z | 4;
		return 0;
	}
}

/* does extension state x (last_ext) make extending the low `from` bytes a no-op? */
VPRIV bool ext_ok(int x, int from, bool sgn)
{
	int n = x & 15;

	if (!x)
		return false;
	if (x & EXT_S)
		return sgn && n <= from;
	return sgn ? n < from : n <= from;
}

/* ---------------- truth tests ---------------- */

/* branch to l if (v != 0) == nz; consumes v.  Returns the constant truth or -1. */
VPRIV int test_zero(ecx *e, val *v, qty t, uint32_t l, bool nz)
{
	cc *c = e->c;
	uint64_t sz = tsize(c, t);
	int r;

	if (v->k == V_CONST || v->k == V_ADDR) {
		bool truth = v->k == V_ADDR || v->i != 0;

		if (truth == nz)
			jump(e, l);
		return truth;
	}
	if (v->k == V_NONE)
		return -1;
	if (is_ld(c, t)) {
		*v = ld_compare(e, T_EXCLAIMEQ, t, *v, vconst(0));
		t = prim(TY_INT);
		sz = 4;
	}
	if (is_flt(c, t)) {
		int d = fpk(e, t), cond = f_cmp(e, T_EXCLAIMEQ, d, v, NULL);

		bcond(e, nz ? cond : C_EQ, l);
		val_free(e, v);
		return -1;
	}
	if (v->k == V_REG2) {
		op3(e, I_ORR, 1, R_X16, v->r, v->r2);
		cbz(e, nz, 1, R_X16, l);
		val_free(e, v);
		return -1;
	}
	r = v->r;
	{
		int cond = last_cset(e, v);

		if (cond >= 0) {
			e->nin--;
			bcond_opt(e, nz ? cond : cond ^ 1, l);
			val_free(e, v);
			return -1;
		}
	}
	if (e->E->opt3 && e->reach && e->nin) {
		const ins *p = &e->in[e->nin - 1];
		int bit, x;

		if (p->op == I_ANDI && p->rd == r && p->rn == r && (bit = one_bit((uint64_t)p->imm, p->sz)) >= 0 &&
		    (sz >= 4 || bit < (int)sz * 8)) {
			/* (r & bit) != 0: test the bit */
			e->nin--;
			tbz(e, nz, r, bit, l);
			val_free(e, v);
			return -1;
		}
		if (sz < 4 && (x = last_ext(e, r)) && (x & 15) <= (int)sz) {
			/* zero-extended (or sign-extended from within) its size: the whole register */
			cbz(e, nz, 1, r, l);
			val_free(e, v);
			return -1;
		}
	}
	if (e->E->opt3 && KIND(c, t) == TY_BOOL) {
		/* a bool is 0 or 1 in its low byte (AAPCS64 too): test bit 0 */
		tbz(e, nz, r, 0, l);
		val_free(e, v);
		return -1;
	}
	if (is_ptr(c, t) || sz == 8) {
		cbz(e, nz, 1, r, l);
	} else if (sz == 4) {
		cbz(e, nz, 0, r, l);
	} else {
		opi(e, I_ANDSI, 0, ZR, r, sz == 2 ? 0xffff : 0xff);
		bcond(e, nz ? C_NE : C_EQ, l);
	}
	val_free(e, v);
	return -1;
}

/* v as 0/1 (int) */
VPRIV val tobool(ecx *e, val v, qty t)
{
	cc *c = e->c;
	uint64_t sz = tsize(c, t);
	int r;

	if (is_flt(c, t))
		return f_tobool(e, v, t);
	if (v.k == V_CONST)
		return vconst(v.i != 0);
	if (v.k == V_ADDR)
		return vconst(1);
	if (v.k == V_REG2) {
		op3(e, I_ORR, 1, v.r, v.r, v.r2);
		tfree(e, v.r2);
		v = vreg(v.r);
		sz = 8;
	}
	r = toreg(e, &v);
	if (KIND(c, t) == TY_BOOL || last_cset(e, &v) >= 0 || last_ext(e, r) == EXT_Z)
		return v;
	if (is_ptr(c, t) || sz == 8)
		op3(e, I_SUBS, 1, ZR, r, ZR);
	else if (sz == 4)
		op3(e, I_SUBS, 0, ZR, r, ZR);
	else
		opi(e, I_ANDSI, 0, ZR, r, sz == 2 ? 0xffff : 0xff);
	cset(e, r, C_NE);
	return v;
}

/* ---------------- casts ---------------- */

VPRIV val cast(ecx *e, val v, qty from, qty to, int kind)
{
	cc *c = e->c;
	uint64_t fs = tsize(c, from), ts = tsize(c, to);

	if ((is_flt(c, from) || is_flt(c, to)) && kind != CK_VOID)
		return f_cast(e, v, from, to);
	switch (kind) {
	case CK_NOP:
	case CK_P2P:
		if (v.k == V_CONST && !is_ptr(c, to))
			v.i = em_trunc_to(c, v.i, to);
		return v;
	case CK_BOOL:
		return tobool(e, v, from);
	case CK_NULL:
		val_free(e, &v);
		return vconst(0);
	case CK_INT:
	case CK_P2I:
	case CK_I2P:
		if (v.k == V_CONST) {
			if (kind == CK_I2P)
				return vconst((i128)(uint64_t)em_trunc_to(c, v.i, from));
			return vconst(em_trunc_to(c, v.i, kind == CK_P2I && ts == 16 ? prim(TY_UINT128) : to));
		}
		if (v.k == V_ADDR) {
			if (ts == 8)
				return v;
			toreg(e, &v);
		}
		if (v.k == V_NONE)
			return v;
		if (ts == 16 && fs < 16) {
			bool sg = kind == CK_INT && is_signed(c, from);
			int r = toreg(e, &v);

			if (fs < 8)
				extend(e, r, r, (int)fs, sg);
			toreg2(e, &v, sg);
			return v;
		}
		if (fs == 16 && ts < 16) {
			if (v.k == V_REG2) {
				tfree(e, v.r2);
				v = vreg(v.r);
			}
			return v;
		}
		if (ts > fs && last_cset(e, &v) < 0) {	/* a cset's 0/1 is already extended */
			int r = toreg(e, &v);
			bool sg = kind != CK_P2I && is_signed(c, from);

			if (!ext_ok(last_ext(e, r), (int)fs, sg))
				extend(e, r, r, (int)fs, sg);
		}
		return v;
	case CK_VOID:
		val_free(e, &v);
		return vnone();
	default:
		return v;
	}
}

/* ---------------- arithmetic ---------------- */

VPRIV void shift_imm(ecx *e, int op, int x, int rd, int rn, int k)
{
	int w = x ? 64 : 32;

	k &= w - 1;
	if (op == T_LESSLESS)
		opbf(e, I_UBFM, x, rd, rn, (w - k) % w, w - 1 - k);
	else if (op == I_ASRV)
		opbf(e, I_SBFM, x, rd, rn, k, w - 1);
	else
		opbf(e, I_UBFM, x, rd, rn, k, w - 1);
}

/* -O3: ra = ra / m or ra % m for a power of two m (k as given; false: not one) */
static bool div_pow2(ecx *e, int op, int x, bool sg, int ra, int64_t k)
{
	uint64_t m = x ? (uint64_t)k : (uint32_t)k;
	int s;

	if ((sg && (x ? k <= 0 : (int32_t)k <= 0)) || !m || (m & (m - 1)))
		return false;
	s = __builtin_ctzll(m);
	if (op == T_SLASH) {
		if (!s)
			return true;
		if (!sg) {
			shift_imm(e, I_LSRV, x, ra, ra, s);
			return true;
		}
		/* round toward zero: bias negative dividends by m - 1 */
		opi(e, I_ADDI, x, R_X16, ra, (int64_t)(m - 1));
		opi(e, I_SUBSI, x, ZR, ra, 0);
		op3s(e, I_CSEL, x, R_X16, R_X16, ra, C_LT, 0);
		shift_imm(e, I_ASRV, x, ra, R_X16, s);
		return true;
	}
	if (!s) {
		movi(e, x, ra, 0);
		return true;
	}
	if (!sg) {
		opi(e, I_ANDI, x, ra, ra, (int64_t)(m - 1));
		return true;
	}
	/* the remainder takes the dividend's sign: a > 0 ? a & (m-1) : -(-a & (m-1)) */
	op3(e, I_SUBS, x, R_X16, ZR, ra);
	opi(e, I_ANDI, x, ra, ra, (int64_t)(m - 1));
	opi(e, I_ANDI, x, R_X16, R_X16, (int64_t)(m - 1));
	op3s(e, I_CSNEG, x, ra, ra, R_X16, C_MI, 0);
	return true;
}

VPRIV val arith(ecx *e, int op, qty ty, val a, val b)
{
	cc *c = e->c;
	int x = X(c, ty), ra, rb;
	bool sg = is_signed(c, ty);

	/* commutative: constant on the right */
	if (a.k == V_CONST && b.k != V_CONST && (op == T_PLUS || op == T_STAR || op == T_AMP || op == T_PIPE || op == T_CARET)) {
		val t = a;

		a = b;
		b = t;
	}
	ra = toreg(e, &a);
	if (b.k == V_CONST) {
		int64_t k = (int64_t)b.i;
		uint64_t m = x ? (uint64_t)k : (uint32_t)k;

		switch (op) {
		case T_PLUS:
			opi(e, I_ADDI, x, ra, ra, x ? k : (int32_t)k);
			return a;
		case T_MINUS:
			opi(e, I_SUBI, x, ra, ra, x ? k : (int32_t)k);
			return a;
		case T_STAR:
			if (m && !(m & (m - 1))) {
				shift_imm(e, T_LESSLESS, x, ra, ra, __builtin_ctzll(m));
				return a;
			}
			if (e->E->opt3 && m > 2 && !((m - 1) & (m - 2))) {
				/* 2^s + 1 */
				op3s(e, I_ADD, x, ra, ra, ra, 0, __builtin_ctzll(m - 1));
				return a;
			}
			break;
		case T_SLASH: case T_PERCENT:
			if (e->E->opt3 && div_pow2(e, op, x, sg, ra, k))
				return a;
			break;
		case T_AMP: case T_PIPE: case T_CARET:
			if (logimm_ok(m, x)) {
				opi(e, op == T_AMP ? I_ANDI : op == T_PIPE ? I_ORRI : I_EORI, x, ra, ra, (int64_t)m);
				return a;
			}
			if (op == T_AMP && m == 0) {
				movi(e, x, ra, 0);
				return a;
			}
			if ((op == T_PIPE || op == T_CARET) && m == 0)
				return a;
			break;
		case T_LESSLESS:
			shift_imm(e, T_LESSLESS, x, ra, ra, (int)k);
			return a;
		case T_GREATERGREATER:
			shift_imm(e, sg ? I_ASRV : I_LSRV, x, ra, ra, (int)k);
			return a;
		}
	}
	rb = toreg(e, &b);
	if (e->E->opt3 && (op == T_PLUS || op == T_MINUS) && e->reach && e->nin) {
		/* ra +- rb where rb = rn * rm was just computed: madd/msub */
		ins *p = &e->in[e->nin - 1];

		if (p->op == I_MADD && p->ra == ZR && p->rd == rb && rb != ra && p->sz == x && p->rn != ra && p->rm != ra) {
			p->op = op == T_PLUS ? I_MADD : I_MSUB;
			p->rd = (uint8_t)ra;
			p->ra = (uint8_t)ra;
			tfree(e, rb);
			return a;
		}
	}
	switch (op) {
	case T_PLUS: op3(e, I_ADD, x, ra, ra, rb); break;
	case T_MINUS: op3(e, I_SUB, x, ra, ra, rb); break;
	case T_STAR: op4(e, I_MADD, x, ra, ra, rb, ZR); break;
	case T_SLASH: op3(e, sg ? I_SDIV : I_UDIV, x, ra, ra, rb); break;
	case T_PERCENT:
		op3(e, sg ? I_SDIV : I_UDIV, x, R_X16, ra, rb);
		op4(e, I_MSUB, x, ra, R_X16, rb, ra);
		break;
	case T_AMP: op3(e, I_AND, x, ra, ra, rb); break;
	case T_PIPE: op3(e, I_ORR, x, ra, ra, rb); break;
	case T_CARET: op3(e, I_EOR, x, ra, ra, rb); break;
	case T_LESSLESS: op3(e, I_LSLV, x, ra, ra, rb); break;
	case T_GREATERGREATER: op3(e, sg ? I_ASRV : I_LSRV, x, ra, ra, rb); break;
	default:
		efatal(e, 0, "unsupported operator %d", op);
	}
	tfree(e, rb);
	return a;
}

/* 128-bit arithmetic on register pairs */
VPRIV val arith2(ecx *e, int op, qty ty, val a, val b)
{
	cc *c = e->c;
	bool sg = is_signed(c, ty);

	if (op == T_LESSLESS || op == T_GREATERGREATER) {
		if (b.k == V_CONST) {
			int k = (int)(b.i & 127);

			toreg2(e, &a, sg);
			if (k == 0)
				return a;
			if (op == T_LESSLESS) {
				if (k >= 64) {
					shift_imm(e, T_LESSLESS, 1, a.r2, a.r, k - 64);
					movi(e, 1, a.r, 0);
				} else {
					shift_imm(e, T_LESSLESS, 1, a.r2, a.r2, k);
					shift_imm(e, I_LSRV, 1, R_X16, a.r, 64 - k);
					op3(e, I_ORR, 1, a.r2, a.r2, R_X16);
					shift_imm(e, T_LESSLESS, 1, a.r, a.r, k);
				}
			} else {
				if (k >= 64) {
					shift_imm(e, sg ? I_ASRV : I_LSRV, 1, a.r, a.r2, k - 64);
					if (sg)
						shift_imm(e, I_ASRV, 1, a.r2, a.r2, 63);
					else
						movi(e, 1, a.r2, 0);
				} else {
					shift_imm(e, I_LSRV, 1, a.r, a.r, k);
					shift_imm(e, T_LESSLESS, 1, R_X16, a.r2, 64 - k);
					op3(e, I_ORR, 1, a.r, a.r, R_X16);
					shift_imm(e, sg ? I_ASRV : I_LSRV, 1, a.r2, a.r2, k);
				}
			}
			return a;
		}
		{
			/* variable shift: both halves, then select on bit 6 of the count */
			int n, t1, t2, t3;

			toreg2(e, &a, sg);
			n = toreg(e, &b);
			t1 = treg(e), t2 = treg(e), t3 = treg(e);
			if (op == T_LESSLESS) {
				shift_imm(e, I_LSRV, 1, t1, a.r, 1);
				op3(e, I_ORN, 0, t2, ZR, n);
				op3(e, I_LSLV, 1, t3, a.r2, n);
				op3(e, I_LSRV, 1, t1, t1, t2);
				op3(e, I_ORR, 1, t1, t3, t1);		/* high, n < 64 */
				op3(e, I_LSLV, 1, t2, a.r, n);		/* low << n */
				opi(e, I_ANDSI, 1, ZR, n, 64);
				op3s(e, I_CSEL, 1, a.r2, t2, t1, C_NE, 0);
				op3s(e, I_CSEL, 1, a.r, ZR, t2, C_NE, 0);
			} else {
				shift_imm(e, T_LESSLESS, 1, t1, a.r2, 1);
				op3(e, I_ORN, 0, t2, ZR, n);
				op3(e, I_LSRV, 1, t3, a.r, n);
				op3(e, I_LSLV, 1, t1, t1, t2);
				op3(e, I_ORR, 1, t1, t1, t3);		/* low, n < 64 */
				op3(e, sg ? I_ASRV : I_LSRV, 1, t2, a.r2, n);	/* high >> n */
				if (sg)
					shift_imm(e, I_ASRV, 1, t3, a.r2, 63);
				opi(e, I_ANDSI, 1, ZR, n, 64);
				op3s(e, I_CSEL, 1, a.r, t2, t1, C_NE, 0);
				op3s(e, I_CSEL, 1, a.r2, sg ? t3 : ZR, t2, C_NE, 0);
			}
			tfree(e, t1), tfree(e, t2), tfree(e, t3);
			val_free(e, &b);
			return a;
		}
	}
	if (op == T_SLASH || op == T_PERCENT) {
		val args[2] = { a, b };
		qty tys[2] = { ty, ty };
		int fn = op == T_SLASH ? (sg ? FN_DIVTI3 : FN_UDIVTI3) : (sg ? FN_MODTI3 : FN_UMODTI3);

		return call_named(e, FIXED(fn), args, tys, 2, ty, false);
	}
	toreg2(e, &a, sg);
	toreg2(e, &b, sg);
	switch (op) {
	case T_PLUS:
		op3(e, I_ADDS, 1, a.r, a.r, b.r);
		op3(e, I_ADC, 1, a.r2, a.r2, b.r2);
		break;
	case T_MINUS:
		op3(e, I_SUBS, 1, a.r, a.r, b.r);
		op3(e, I_SBC, 1, a.r2, a.r2, b.r2);
		break;
	case T_AMP: op3(e, I_AND, 1, a.r, a.r, b.r); op3(e, I_AND, 1, a.r2, a.r2, b.r2); break;
	case T_PIPE: op3(e, I_ORR, 1, a.r, a.r, b.r); op3(e, I_ORR, 1, a.r2, a.r2, b.r2); break;
	case T_CARET: op3(e, I_EOR, 1, a.r, a.r, b.r); op3(e, I_EOR, 1, a.r2, a.r2, b.r2); break;
	case T_STAR:
		/* hi = umulh(alo, blo) + alo * bhi + ahi * blo; lo = alo * blo */
		op3(e, I_UMULH, 1, R_X16, a.r, b.r);
		op4(e, I_MADD, 1, R_X16, a.r, b.r2, R_X16);
		op4(e, I_MADD, 1, a.r2, a.r2, b.r, R_X16);
		op4(e, I_MADD, 1, a.r, a.r, b.r, ZR);
		break;
	default:
		efatal(e, 0, "unsupported 128-bit operator %d", op);
	}
	val_free(e, &b);
	return a;
}
