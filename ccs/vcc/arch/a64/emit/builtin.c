/* vcc code generator: builtins */
#include "../../../emit/emitimpl.h"

/* ---------------- builtins ---------------- */

VPRIV uint32_t strip_casts(cc *c, uint32_t n)
{
	while (N(n)->op == N_CAST)
		n = N(n)->a;
	return n;
}

/* clang's Expr::HasSideEffects (with possible effects): calls, writes, volatile reads */
VPRIV bool side_effects(cc *c, uint32_t n)
{
	const node *x;

	if (!n)
		return false;
	x = N(n);
	if ((QQ(x->ty) & Q_VOLATILE) && (x->op == N_VAR || x->op == N_DEREF || x->op == N_MEMBER || x->op == N_INDEX))
		return true;
	switch (x->op) {
	case N_NUM: case N_FNUM: case N_STR: case N_VAR: case N_ZERO: case N_LABELADDR:
		return false;
	case N_CALL: case N_ASSIGN: case N_PREINC: case N_PREDEC: case N_POSTINC: case N_POSTDEC:
	case N_ASM:
		return true;
	case N_BUILTIN:
		switch (x->aux) {
		case BI_VA_START: case BI_VA_COPY: case BI_VA_END: case BI_ALLOCA: case BI_TRAP:
		case BI_UNREACHABLE:
		case BI_ADD_OVERFLOW: case BI_SUB_OVERFLOW: case BI_MUL_OVERFLOW:	/* store the result */
		case BI_ADDC: case BI_SUBC:
			return true;
		default:
			if (x->aux >= BI_ATOMIC_LOAD_N && x->aux <= BI_ATOMIC_SIGNAL_FENCE)
				return true;
			if (x->aux == BI_CONSTANT_P)
				return false;	/* its operand is not evaluated */
			if (x->aux >= BI_ARM_NOP && x->aux <= BI_ARM_TRAP)
				return true;
			for (uint32_t i = 0; i < x->c; i++)
				if (side_effects(c, c->lists[x->b + i]))
					return true;
			return false;
		}
	case N_MEMBER: case N_DEREF: case N_UNARY: case N_CAST: case N_ADDR: case N_COMPLIT:
	case N_STMTEXPR: case N_EXPR: case N_RETURN:
		return side_effects(c, x->a);
	case N_INDEX: case N_BINARY: case N_LOGAND: case N_LOGOR: case N_COMMA: case N_CPLX:
		return side_effects(c, x->a) || side_effects(c, x->b);
	case N_COND: case N_IF:
		return side_effects(c, x->a) || side_effects(c, x->b) || side_effects(c, x->c);
	case N_DECL:
		return side_effects(c, x->b);
	case N_BLOCK: case N_INIT:
		if (x->op == N_INIT && x->aux == 1) {
			for (uint32_t i = 0; i < x->c; i++)
				if (side_effects(c, c->lists[x->b + 3 * i + 2]))
					return true;
			return false;
		}
		for (uint32_t i = 0; i < x->c; i++)
			if (side_effects(c, c->lists[x->b + i]))
				return true;
		return false;
	default:
		return true;	/* loops, gotos, labels, va_arg: conservatively yes */
	}
}

/* evaluate n without emitting anything: constant or address constant? */
/*
 * __builtin_constant_p: generate the operand as if it were reachable (so
 * inlined bodies run their returns and fold), then throw the code away.
 * References made meanwhile do not count: nothing gets queued for emission.
 */
VPRIV val dry(ecx *e, uint32_t n)
{
	uint32_t nin = e->nin, live = e->live, usedcs = e->usedcs;
	size_t raw = e->raw.n;
	int64_t foff = e->foff;
	bool reach = e->reach, calls = e->calls;
	int nclean = e->nclean, nloop = e->nloop;
	uint32_t nvsz = e->nvsz;
	uint8_t nkv = e->nkv;
	uint16_t kvalid = e->kvalid;
	int64_t kval[NKV];
	val v;

	memcpy(kval, e->kval, sizeof(kval));
	e->dry++;
	v = gen(e, n);
	e->dry--;
	e->nkv = nkv, e->kvalid = kvalid;
	while (e->nael && e->ael[e->nael - 1].kv >= nkv)
		e->nael--;
	memcpy(e->kval, kval, sizeof(kval));
	e->nin = nin, e->live = live, e->usedcs = usedcs, e->foff = foff;	/* labels stay: user labels may map to them */
	while (e->ndl && e->dl[e->ndl - 1].k > e->nin)
		e->ndl--;
	e->reach = reach, e->nclean = nclean, e->nloop = nloop, e->raw.n = raw, e->calls = calls, e->nvsz = nvsz;
	if (v.k != V_CONST && v.k != V_ADDR)
		v = vnone();
	return v;
}

static val popcount(ecx *e, val v, int x)
{
	int r = toreg(e, &v), t = treg(e);
	int w = x ? 1 : 0;

	if (!x)
		op3(e, I_MOV, 0, r, r, 0);	/* zero-extend */
	/* v - ((v >> 1) & 0x55..) */
	shift_imm(e, I_LSRV, 1, t, r, 1);
	opi(e, I_ANDI, 1, t, t, 0x5555555555555555LL);
	op3(e, I_SUB, 1, r, r, t);
	/* (v & 0x33..) + ((v >> 2) & 0x33..) */
	shift_imm(e, I_LSRV, 1, t, r, 2);
	opi(e, I_ANDI, 1, t, t, 0x3333333333333333LL);
	opi(e, I_ANDI, 1, r, r, 0x3333333333333333LL);
	op3(e, I_ADD, 1, r, r, t);
	/* (v + (v >> 4)) & 0x0f.. */
	shift_imm(e, I_LSRV, 1, t, r, 4);
	op3(e, I_ADD, 1, r, r, t);
	opi(e, I_ANDI, 1, r, r, 0x0f0f0f0f0f0f0f0fLL);
	/* (v * 0x0101..) >> 56 */
	movi(e, 1, t, 0x0101010101010101LL);
	op4(e, I_MADD, 1, r, r, t, ZR);
	shift_imm(e, I_LSRV, 1, r, r, 56);
	tfree(e, t);
	(void)w;
	return v;
}

static val gen_overflow(ecx *e, const node *x, const uint32_t *arg)
{
	cc *c = e->c;
	qty ta = N(arg[0])->ty, tb = N(arg[1])->ty, tr = T(c, N(arg[2])->ty)->base;
	val a = gen(e, arg[0]), b = gen(e, arg[1]), p = gen(e, arg[2]);
	int bits = em_bitint(c, tr) ? (int)T(c, tr)->n : (int)tsize(c, tr) * 8;
	bool sr = is_signed(c, tr);
	int lo, hi, t, ok;
	lval l;

	if (tsize(c, ta) > 8 || tsize(c, tb) > 8 || (em_bitint(c, tr) && bits > 64))
		efatal(e, x->loc, "128-bit overflow builtins are not supported");
	/* exact result in 128 bits */
	{
		int ra = toreg(e, &a), rb = toreg(e, &b);

		extend(e, ra, ra, (int)tsize(c, ta), is_signed(c, ta));
		extend(e, rb, rb, (int)tsize(c, tb), is_signed(c, tb));
		toreg2(e, &a, is_signed(c, ta));
		toreg2(e, &b, is_signed(c, tb));
	}
	lo = a.r, hi = a.r2;
	switch (x->aux) {
	case BI_ADD_OVERFLOW:
		op3(e, I_ADDS, 1, lo, lo, b.r);
		op3(e, I_ADC, 1, hi, hi, b.r2);
		break;
	case BI_SUB_OVERFLOW:
		op3(e, I_SUBS, 1, lo, lo, b.r);
		op3(e, I_SBC, 1, hi, hi, b.r2);
		break;
	default:
		op3(e, I_UMULH, 1, R_X16, lo, b.r);
		op4(e, I_MADD, 1, R_X16, lo, b.r2, R_X16);
		op4(e, I_MADD, 1, hi, hi, b.r, R_X16);
		op4(e, I_MADD, 1, lo, lo, b.r, ZR);
		break;
	}
	val_free(e, &b);
	if (bits == 128) {
		/*
		 * Only two unsigned factors can produce a positive result above
		 * signed 128-bit range.  Addition and subtraction fit signed 128,
		 * even for unsigned operands (subtraction can be negative).
		 */
		bool uu = x->aux == BI_MUL_OVERFLOW && !is_signed(c, ta) && !is_signed(c, tb);
		val r = { .k = V_REG2, .r = (uint8_t)lo, .r2 = (uint8_t)hi };

		ok = treg(e);
		if (uu == sr)
			shift_imm(e, I_LSRV, 1, ok, hi, 63);
		else
			movi(e, 0, ok, 0);
		l = lfromval(e, p, talign(c, tr));
		store(e, &l, tr, &r);
		lfree(e, &l);
		tfree(e, lo);
		tfree(e, hi);
		return vreg(ok);
	}
	/* fits iff ext(lo) == lo && hi == (signed ? lo >> 63 : 0) */
	t = treg(e);
	ok = treg(e);
	if (bits < 64) {
		opbf(e, sr ? I_SBFM : I_UBFM, 1, t, lo, 0, bits - 1);
		op3(e, I_SUBS, 1, ZR, t, lo);
		cset(e, ok, C_NE);
	} else {
		movi(e, 0, ok, 0);
	}
	if (sr || bits < 64)
		shift_imm(e, sr ? I_ASRV : I_LSRV, 1, t, lo, 63);
	if (sr)
		op3(e, I_SUBS, 1, ZR, hi, t);
	else
		op3(e, I_SUBS, 1, ZR, hi, ZR);
	/* ok |= (hi != expected) */
	cset(e, t, C_NE);
	op3(e, I_ORR, 0, ok, ok, t);
	tfree(e, t);
	l = lfromval(e, p, talign(c, tr));
	{
		val r = vreg(lo);

		store(e, &l, tr, &r);
	}
	lfree(e, &l);
	tfree(e, lo);
	tfree(e, hi);
	return vreg(ok);
}

/* __builtin_addc* / __builtin_subc*: x +- y +- carry_in, *carry_out = the carry (borrow) of either step */
static val gen_carry(ecx *e, const node *x, const uint32_t *arg)
{
	cc *c = e->c;
	qty ty = x->ty, tc = T(c, N(arg[3])->ty)->base;
	int sz = (int)tsize(c, ty), bits = sz * 8;
	bool add = x->aux == BI_ADDC;
	val a = gen(e, arg[0]), b = gen(e, arg[1]), ci = gen(e, arg[2]), p = gen(e, arg[3]);
	int ra = toreg(e, &a), rb = toreg(e, &b), rc = toreg(e, &ci), co = treg(e);
	lval l;

	extend(e, ra, ra, sz, false);
	extend(e, rb, rb, sz, false);
	extend(e, rc, rc, sz, false);
	if (bits == 64) {
		int t = treg(e);

		op3(e, add ? I_ADDS : I_SUBS, 1, ra, ra, rb);
		cset(e, co, add ? C_HS : C_LO);
		op3(e, add ? I_ADDS : I_SUBS, 1, ra, ra, rc);
		cset(e, t, add ? C_HS : C_LO);
		op3(e, I_ORR, 0, co, co, t);
		tfree(e, t);
	} else {
		op3(e, add ? I_ADD : I_SUB, 1, ra, ra, rb);
		op3(e, add ? I_ADD : I_SUB, 1, ra, ra, rc);
		/* add: anything at or above bit 'bits' is the carry; sub: a negative result is the borrow */
		shift_imm(e, I_LSRV, 1, co, ra, add ? bits : 63);
		op3(e, I_SUBS, 1, ZR, co, ZR);
		cset(e, co, C_NE);
		opbf(e, I_UBFM, 1, ra, ra, 0, bits - 1);
	}
	val_free(e, &b);
	val_free(e, &ci);
	l = lfromval(e, p, talign(c, tc));
	{
		val r = vreg(co);

		store(e, &l, tc, &r);
	}
	lfree(e, &l);
	tfree(e, co);
	return vreg(ra);
}

static void va_start_(ecx *e, uint32_t apn)
{
	lval l = gen_lval(e, apn);
	val a = laddr(e, &l);
	int ra = toreg(e, &a), t = treg(e);

	if (DARWIN(e->c)) {
		/* char *: the first anonymous argument, after the named stack ones */
		opi(e, I_ADDI, 1, t, R_FP, 16 + ((e->nstk_named + 7) & ~7));
		mem(e, I_ST, 3, 0, t, ra, 0);
		tfree(e, t);
		val_free(e, &a);
		return;
	}

	/* __stack */
	opi(e, I_ADDI, 1, t, R_FP, 16 + e->nstk_named);
	mem(e, I_ST, 3, 0, t, ra, 0);
	/* __gr_top: end of the register save area */
	opi(e, I_ADDI, 1, t, FRAME, e->vaslot + 64);
	mem(e, I_ST, 3, 0, t, ra, 8);
	/* __vr_top, __gr_offs, __vr_offs */
	opi(e, I_ADDI, 1, t, FRAME, e->vrslot + 128);
	mem(e, I_ST, 3, 0, t, ra, 16);
	movi(e, 0, t, -(8 - e->ngr_named) * 8);
	mem(e, I_ST, 2, 0, t, ra, 24);
	movi(e, 0, t, -(8 - e->nvr_named) * 16);
	mem(e, I_ST, 2, 0, t, ra, 28);
	tfree(e, t);
	val_free(e, &a);
}

VPRIV val va_arg_(ecx *e, const node *x)
{
	cc *c = e->c;
	qty t = x->ty;
	uint64_t size = tsize(c, t);
	bool big = is_recabi(c, t) && size > 16;
	int nreg = big ? 1 : (int)((size + 7) / 8);
	/* Empty aggregates consume neither registers nor stack alignment. */
	bool al16 = size != 0 && !big && abi_align(c, t) == 16;
	lval l = gen_lval(e, x->a);
	val a = laddr(e, &l);
	int ra = toreg(e, &a), off = treg(e), addr = treg(e);
	uint32_t lstack = newlabel(e), ldone = newlabel(e);

	if (DARWIN(c)) {
		mem(e, I_LD, 3, 0, addr, ra, 0);
		if (al16) {
			opi(e, I_ADDI, 1, addr, addr, 15);
			opi(e, I_ANDI, 1, addr, addr, -16);
		}
		opi(e, I_ADDI, 1, off, addr, big ? 8 : ((int64_t)size + 7) & ~7);
		mem(e, I_ST, 3, 0, off, ra, 0);
	} else {
	if (is_flt(c, t)) {
		/* __vr_offs / __vr_top, 16 bytes a register */
		mem(e, I_LD, 2, 1, off, ra, 28);
		opi(e, I_SUBSI, 1, ZR, off, 0);
		bcond(e, C_GE, lstack);
		opi(e, I_ADDI, 1, addr, off, 16);
		mem(e, I_ST, 2, 0, addr, ra, 28);
		opi(e, I_SUBSI, 1, ZR, addr, 0);
		bcond(e, C_GT, lstack);
		mem(e, I_LD, 3, 0, addr, ra, 16);
		op3(e, I_ADD, 1, addr, addr, off);
		jump(e, ldone);
		nreg = size > 8 ? 2 : 1;
		al16 = size > 8;
		goto stack;
	}
	int hn, hb = hfa(c, t, &hn);

	if (hb) {
		/* an HFA: hn v registers, copied together into a temporary */
		int64_t sl = slot(e, 64, 16);

		mem(e, I_LD, 2, 1, off, ra, 28);
		opi(e, I_SUBSI, 1, ZR, off, 0);
		bcond(e, C_GE, lstack);
		opi(e, I_ADDI, 1, addr, off, 16 * hn);
		mem(e, I_ST, 2, 0, addr, ra, 28);
		opi(e, I_SUBSI, 1, ZR, addr, 0);
		bcond(e, C_GT, lstack);
		mem(e, I_LD, 3, 0, addr, ra, 16);
		op3(e, I_ADD, 1, addr, addr, off);
		for (int k = 0; k < hn; k++)
			for (int w = 0; w < hb; w += 8) {
				int lg = hb >= 8 ? 3 : 2;

				mem(e, I_LD, lg, 0, R_X16, addr, 16 * k + w);
				mem(e, I_ST, lg, 0, R_X16, FRAME, sl + hb * k + w);
			}
		opi(e, I_ADDI, 1, addr, FRAME, sl);
		jump(e, ldone);
		nreg = (int)((size + 7) / 8);
		big = false;
		al16 = abi_align(c, t) >= 16;
		goto stack;
	}
	mem(e, I_LD, 2, 1, off, ra, 24);
	opi(e, I_SUBSI, 1, ZR, off, 0);
	bcond(e, C_GE, lstack);
	if (al16) {
		opi(e, I_ADDI, 1, off, off, 15);
		opi(e, I_ANDI, 1, off, off, -16);
	}
	opi(e, I_ADDI, 1, addr, off, nreg * 8);
	mem(e, I_ST, 2, 0, addr, ra, 24);
	opi(e, I_SUBSI, 1, ZR, addr, 0);
	bcond(e, C_GT, lstack);
	mem(e, I_LD, 3, 0, addr, ra, 8);
	op3(e, I_ADD, 1, addr, addr, off);
	jump(e, ldone);
stack:
	deflabel(e, lstack);
	mem(e, I_LD, 3, 0, addr, ra, 0);
	if (al16) {
		opi(e, I_ADDI, 1, addr, addr, 15);
		opi(e, I_ANDI, 1, addr, addr, -16);
	}
	opi(e, I_ADDI, 1, off, addr, nreg * 8);
	mem(e, I_ST, 3, 0, off, ra, 0);
	deflabel(e, ldone);
	}
	tfree(e, off);
	val_free(e, &a);
	if (big)
		mem(e, I_LD, 3, 0, addr, addr, 0);
	{
		lval m = { .k = L_REG, .r = (uint8_t)addr, .align = 8 };
		val v = load(e, &m, t);

		if (!(v.k == V_REG && v.r == addr))
			tfree(e, addr);
		return v;
	}
}

/*
 * Atomic read-modify-write instructions: the LSE one when the target has
 * LSE (-march=armv8.1-a and later, +lse, most -mcpu, Darwin), else the same
 * effect as an ldaxr/stlxr loop (what clang emits with -mno-outline-atomics).
 * Registers as in the LSE forms; sz is log2 of the size.
 */
static void ex_ld(ecx *e, unsigned sz, int rt, int rn)
{
	word(e, sz << 30 | 0x085ffc00u | (unsigned)rn << 5 | (unsigned)rt);	/* ldaxr */
}

static void ex_st(ecx *e, unsigned sz, int ws, int rt, int rn)
{
	word(e, sz << 30 | 0x0800fc00u | (unsigned)ws << 16 | (unsigned)rn << 5 | (unsigned)rt);	/* stlxr */
}

/* casal rs, rt, [rn]: rs = old (zero-extended); *rn = rt if old == rs */
VPRIV void at_cas(ecx *e, unsigned sz, int rs, int rt, int rn)
{
	int t, ws, cmpv;
	uint32_t top, fail;

	if (e->c->o->lse) {
		word(e, sz << 30 | 0x08e0fc00u | (unsigned)rs << 16 | (unsigned)rn << 5 | (unsigned)rt);
		return;
	}
	t = treg(e), ws = treg(e), cmpv = treg(e);
	if (sz < 2)
		extend(e, cmpv, rs, 1 << sz, false);
	else
		mov(e, 1, cmpv, rs);
	top = newlabel(e), fail = newlabel(e);
	deflabel_loop(e, top, true);
	ex_ld(e, sz, t, rn);
	op3(e, I_SUBS, sz == 3, ZR, t, cmpv);
	bcond(e, C_NE, fail);
	ex_st(e, sz, ws, rt, rn);
	cbz(e, true, 0, ws, top);
	deflabel_forced(e, fail);
	word(e, 0xd503305fu);	/* clrex (harmless after a store) */
	mov(e, 1, rs, t);
	tfree(e, cmpv), tfree(e, ws), tfree(e, t);
}

/* swpal rs, rt, [rn]: rt = old, *rn = rs */
VPRIV void at_swp(ecx *e, unsigned sz, int rs, int rt, int rn)
{
	int t, ws;
	uint32_t top;

	if (e->c->o->lse) {
		word(e, sz << 30 | 0x38e08000u | (unsigned)rs << 16 | (unsigned)rn << 5 | (unsigned)rt);
		return;
	}
	t = treg(e), ws = treg(e);
	top = newlabel(e);
	deflabel_loop(e, top, true);
	ex_ld(e, sz, t, rn);
	ex_st(e, sz, ws, rs, rn);
	cbz(e, true, 0, ws, top);
	mov(e, 1, rt, t);
	tfree(e, ws), tfree(e, t);
}

/* u = t op rs for the opc of at_ldop (min/max on the low 1 << sz bytes, signed or not) */
static void at_opval(ecx *e, unsigned sz, unsigned opc, int u, int t, int rs)
{
	static const int op[] = { I_ADD, I_BIC, I_EOR, I_ORR };
	static const int cond[] = { C_GT, C_LT, C_HI, C_LO };	/* smax smin umax umin: keep t */

	if (opc < 4) {
		op3(e, op[opc], sz == 3, u, t, rs);
	} else if (opc == 8) {	/* nand */
		op3(e, I_AND, 1, u, t, rs);
		op3(e, I_ORN, 1, u, ZR, u);
	} else {
		bool sg = opc < 6;
		int a = t, b = rs, w = -1;

		if (sz < 2) {
			w = treg(e);
			extend(e, u, t, 1 << sz, sg), extend(e, w, rs, 1 << sz, sg);
			a = u, b = w;
		}
		op3(e, I_SUBS, sz == 3, ZR, a, b);
		op3s(e, I_CSEL, 1, u, t, rs, cond[opc - 4], 0);
		if (w >= 0)
			tfree(e, w);
	}
}

/*
 * ld<op>al rs, rt, [rn]: rt = old, *rn = old op rs; opc: 0 add, 1 clr, 2 eor,
 * 3 set, 4 smax, 5 smin, 6 umax, 7 umin; 8 nand (no LSE form: always a loop)
 */
VPRIV void at_ldop(ecx *e, unsigned sz, unsigned opc, int rs, int rt, int rn)
{
	int t, u, ws;
	uint32_t top;

	if (e->c->o->lse && opc < 8) {
		word(e, sz << 30 | 0x38e00000u | (unsigned)rs << 16 | opc << 12 | (unsigned)rn << 5 | (unsigned)rt);
		return;
	}
	t = treg(e), u = treg(e), ws = treg(e);
	top = newlabel(e);
	deflabel_loop(e, top, true);
	ex_ld(e, sz, t, rn);
	at_opval(e, sz, opc, u, t, rs);
	ex_st(e, sz, ws, u, rn);
	cbz(e, true, 0, ws, top);
	mov(e, 1, rt, t);
	tfree(e, ws), tfree(e, u), tfree(e, t);
}

/*
 * __atomic_*: LSE instructions, always acquire+release (ldar/stlr, swpal,
 * casal, ld<op>al), which is at least what any memory order asks for.
 */
static val gen_atomic(ecx *e, const node *x, const uint32_t *arg)
{
	cc *c = e->c;
	int id = x->aux;
	qty t = QUNQ(T(c, N(arg[0])->ty)->base);
	uint64_t n = tsize(c, t);
	unsigned sz = (unsigned)log2sz(n), xs = n == 8;
	bool sg = is_integer(c, t) && is_signed(c, t);
	val p = gen(e, arg[0]), v1 = vnone(), v2 = vnone(), res = vnone();
	int rp, nmo = id == BI_ATOMIC_CMPXCHG_N || id == BI_ATOMIC_CMPXCHG ? 2 : 1, nargs = (int)x->c;

	if (n == 16)
		efatal(e, x->loc, "16-byte atomics are not supported");
	rp = toreg(e, &p);
	/* operands, then the memory orders (evaluated for their side effects) */
	if (nargs - nmo > 1)
		v1 = gen(e, arg[1]);
	if (nargs - nmo > 2)
		v2 = gen(e, arg[2]);
	for (int k = nargs - nmo; k < nargs; k++) {
		val m = gen(e, arg[k]);

		val_free(e, &m);
	}
	if (nargs - nmo > 3) {	/* weak: ignored */
		val w = gen(e, arg[3]);

		val_free(e, &w);
	}
	switch (id) {
	case BI_ATOMIC_LOAD_N: case BI_ATOMIC_LOAD: {
		int r = treg(e);

		word(e, sz << 30 | 0x08dffc00u | (unsigned)rp << 5 | (unsigned)r);	/* ldar */
		if (id == BI_ATOMIC_LOAD) {
			int rr = toreg(e, &v1);

			mem(e, I_ST, (int)sz, 0, r, rr, 0);
			tfree(e, r);
			break;
		}
		if (sg && n < 8)
			extend(e, r, r, (int)n, true);
		res = vreg(r);
		break;
	}
	case BI_ATOMIC_STORE_N: case BI_ATOMIC_STORE: {
		int r;

		if (id == BI_ATOMIC_STORE) {
			int rv = toreg(e, &v1);

			r = treg(e);
			mem(e, I_LD, (int)sz, 0, r, rv, 0);
			tfree(e, r);
		}
		r = id == BI_ATOMIC_STORE ? r : toreg(e, &v1);
		word(e, sz << 30 | 0x089ffc00u | (unsigned)rp << 5 | (unsigned)r);	/* stlr */
		break;
	}
	case BI_ATOMIC_EXCHANGE_N: case BI_ATOMIC_EXCHANGE: {
		int rs, r = treg(e);

		if (id == BI_ATOMIC_EXCHANGE) {
			int rv = toreg(e, &v1);

			rs = treg(e);
			mem(e, I_LD, (int)sz, 0, rs, rv, 0);
		} else {
			rs = toreg(e, &v1);
		}
		at_swp(e, sz, rs, r, rp);	/* swpal */
		if (id == BI_ATOMIC_EXCHANGE) {
			int rr = toreg(e, &v2);

			mem(e, I_ST, (int)sz, 0, r, rr, 0);
			tfree(e, rs);
			tfree(e, r);
			break;
		}
		if (sg && n < 8)
			extend(e, r, r, (int)n, true);
		res = vreg(r);
		break;
	}
	case BI_ATOMIC_CMPXCHG_N: case BI_ATOMIC_CMPXCHG: {
		/* old = *expected; casal old, desired, [p]; ok = old == *expected; if !ok *expected = old */
		int re = toreg(e, &v1), rd, old = treg(e), cmpv = treg(e), ok;
		uint32_t l = newlabel(e);

		if (id == BI_ATOMIC_CMPXCHG) {
			int rv = toreg(e, &v2);

			rd = treg(e);
			mem(e, I_LD, (int)sz, 0, rd, rv, 0);
		} else {
			rd = toreg(e, &v2);
		}
		mem(e, I_LD, (int)sz, 0, old, re, 0);
		mov(e, 1, cmpv, old);
		at_cas(e, sz, old, rd, rp);	/* casal */
		if (n < 4)
			extend(e, cmpv, cmpv, (int)n, false), extend(e, old, old, (int)n, false);
		op3(e, I_SUBS, xs, ZR, old, cmpv);
		ok = treg(e);
		cset(e, ok, C_EQ);
		cbz(e, true, 0, ok, l);
		mem(e, I_ST, (int)sz, 0, old, re, 0);
		deflabel(e, l);
		tfree(e, old);
		tfree(e, cmpv);
		if (id == BI_ATOMIC_CMPXCHG)
			tfree(e, rd);
		res = vreg(ok);
		break;
	}
	case BI_SYNC_VAL_CAS: case BI_SYNC_BOOL_CAS: {
		/* old = expected; casal old, desired, [p]; val: old; bool: old == expected (low n bytes) */
		int re = toreg(e, &v1), rd = toreg(e, &v2), old = treg(e);

		mov(e, 1, old, re);
		at_cas(e, sz, old, rd, rp);	/* casal */
		if (id == BI_SYNC_VAL_CAS) {
			if (sg && n < 8)
				extend(e, old, old, (int)n, true);
			res = vreg(old);
			break;
		}
		if (n < 4) {
			int cmpv = treg(e);

			mov(e, 1, cmpv, re);
			extend(e, cmpv, cmpv, (int)n, false);
			op3(e, I_SUBS, 0, ZR, old, cmpv);	/* casal b/h zero-extends old */
			tfree(e, cmpv);
		} else {
			op3(e, I_SUBS, xs, ZR, old, re);
		}
		cset(e, old, C_EQ);
		res = vreg(old);
		break;
	}
	default: {
		/* ld<op>al: add 000, clr 001, eor 010, set 011, smax 100, smin 101, umax 110, umin 111 */
		int base = id >= BI_ATOMIC_ADD_FETCH ? id - BI_ATOMIC_ADD_FETCH : id - BI_ATOMIC_FETCH_ADD;	/* add sub and or xor nand max min */
		static const unsigned opc[] = { 0, 0, 1, 3, 2, 8, 4, 5 };
		unsigned o = opc[base] + (base >= 6 && !sg ? 2 : 0);
		int rv = toreg(e, &v1), rs = treg(e), r = treg(e);

		if (base == 1)
			op3(e, I_SUB, 1, rs, ZR, rv);		/* neg */
		else if (base == 2)
			op3(e, I_ORN, 1, rs, ZR, rv);		/* mvn: clear the bits not in v */
		else
			mov(e, 1, rs, rv);
		at_ldop(e, sz, o, rs, r, rp);
		tfree(e, rs);
		if (id >= BI_ATOMIC_ADD_FETCH) {
			static const int bop[] = { I_ADD, I_SUB, I_AND, I_ORR, I_EOR };

			if (base < 5) {
				op3(e, bop[base], 1, r, r, rv);
			} else {
				int u = treg(e);

				at_opval(e, sz, o, u, r, rv);
				mov(e, 1, r, u);
				tfree(e, u);
			}
		}
		if (n < 8)
			extend(e, r, r, (int)n, sg);
		res = vreg(r);
		break;
	}
	}
	val_free(e, &v1);
	val_free(e, &v2);
	val_free(e, &p);
	return res;
}

VPRIV val gen_builtin(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	const uint32_t *arg = &c->lists[x->b];
	val v;

	switch (x->aux) {
	case BI_SHUFFLEVECTOR: case BI_CONVERTVECTOR: {
		/* lane by lane through a frame slot (vectors are addresses here) */
		qty at = N(arg[0])->ty, ae = T(c, at)->base, re = T(c, x->ty)->base;
		unsigned na = (unsigned)(tsize(c, at) / tsize(c, ae)), nr = (unsigned)(tsize(c, x->ty) / tsize(c, re));
		val a = gen(e, arg[0]), b = x->aux == BI_SHUFFLEVECTOR ? gen(e, arg[1]) : vconst(0);
		int ra = toreg(e, &a), rb = x->aux == BI_SHUFFLEVECTOR ? toreg(e, &b) : -1;
		int64_t sl = slot(e, tsize(c, x->ty), 16);
		int rd = treg(e);

		opi(e, I_ADDI, 1, rd, FRAME, sl);
		for (unsigned i = 0; i < nr; i++) {
			lval d = { .k = L_REG, .r = (uint8_t)rd, .off = (int64_t)i * (int64_t)tsize(c, re), .align = talign(c, re) };
			val v;

			if (x->aux == BI_SHUFFLEVECTOR) {
				int64_t k = (int64_t)c->ipool[N(arg[2 + i])->a];
				lval s = { .k = L_REG, .r = (uint8_t)(k < (int64_t)na ? ra : rb),
					   .off = (k < (int64_t)na ? k : k - (int64_t)na) * (int64_t)tsize(c, ae), .align = talign(c, ae) };

				v = k < 0 ? vconst(0) : load(e, &s, ae);
			} else {
				lval s = { .k = L_REG, .r = (uint8_t)ra, .off = (int64_t)i * (int64_t)tsize(c, ae), .align = talign(c, ae) };
				int ck = is_flt(c, ae) ? (is_flt(c, re) ? CK_F2F : CK_F2I) : is_flt(c, re) ? CK_I2F : CK_INT;

				v = cast(e, load(e, &s, ae), ae, re, ck);
			}
			store(e, &d, re, &v);
			val_free(e, &v);
		}
		tfree(e, ra);
		if (rb >= 0)
			tfree(e, rb);
		return vreg(rd);
	}
	case BI_CONSTANT_P: {
		/*
		 * clang: 1 if the front end folds the operand (a pointer only as a
		 * string literal at offset 0); else 0 if it has side effects or is
		 * not arithmetic; else whatever the optimizer folds, which here is
		 * the operand after inlining (dry).
		 */
		cval cv;
		qty at = N(arg[0])->ty;

		if (cc_eval(c, arg[0], &cv) && (cv.kind == CV_INT || cv.kind == CV_FLT ||
						 (cv.kind == CV_ADDR && cv.str && cv.i == 0 && !cv.sym && !cv.label)))
			return vconst(1);
		if (side_effects(c, arg[0]) || !(is_integer(c, at) || is_flt(c, at)))
			return vconst(0);
		{
			/* "hi"[0]: the optimizer reads a character of a literal */
			uint32_t r = arg[0];
			cval ix;

			while (N(r)->op == N_CAST && (N(r)->aux == CK_INT || N(r)->aux == CK_NOP))
				r = N(r)->a;
			if (N(r)->op == N_INDEX && cc_eval(c, N(r)->b, &ix) && ix.kind == CV_INT) {
				uint32_t s = N(r)->a;

				while (N(s)->op == N_CAST && (N(s)->aux == CK_DECAY || N(s)->aux == CK_NOP))
					s = N(s)->a;
				if (N(s)->op == N_STR && ix.i >= 0 && (uint64_t)ix.i < c->strs[N(s)->a].n)
					return vconst(1);
			}
		}
		v = dry(e, arg[0]);
		return vconst(v.k == V_CONST);
	}
	case BI_EXPECT:
	case BI_EXPECT_PROB:
		/* the hint is still evaluated (argument order is unspecified: first, so that
		 * nothing is live across it) */
		for (uint32_t i = 1; i < x->c; i++)
			if (side_effects(c, arg[i])) {
				v = gen(e, arg[i]);
				val_free(e, &v);
			}
		v = gen(e, arg[0]);
		return v;
	case BI_ASSUME_ALIGNED:
	case BI_EXTRACT_RETURN_ADDR:
		return gen(e, arg[0]);
	case BI_ASSUME:
		return vnone();
	case BI_VA_END: {	/* nothing to do, but the argument's side effects happen */
		lval l = gen_lval(e, arg[0]);
		val a = laddr(e, &l);

		val_free(e, &a);
		return vnone();
	}
	case BI_VA_START:
		va_start_(e, arg[0]);
		return vnone();
	case BI_VA_COPY: {
		lval d = gen_lval(e, arg[0]), s = gen_lval(e, arg[1]);
		val dv = laddr(e, &d), sv = laddr(e, &s);
		int rd = toreg(e, &dv), rs = toreg(e, &sv);

		copy_mem(e, rd, rs, tsize(c, c->t_valist));
		val_free(e, &dv);
		val_free(e, &sv);
		return vnone();
	}
	case BI_UNREACHABLE:
		e->reach = false;
		return vnone();
	case BI_TRAP:
		if (e->reach)
			put(e, I_BRK)->imm = 0x3e8;
		e->reach = false;
		return vnone();
	case BI_ARM_NOP: case BI_ARM_YIELD: case BI_ARM_WFE: case BI_ARM_WFI: case BI_ARM_SEV: case BI_ARM_SEVL:
	case BI_ARM_CLREX: case BI_ARM_ISB: case BI_ARM_DMB: case BI_ARM_DSB: case BI_ARM_TRAP: {
		/* the instruction word: hints (op2 in bits 5-7), barriers (CRm, the operand, in 8-11) */
		static const uint32_t w[] = {
			[BI_ARM_NOP - BI_ARM_NOP] = 0xd503201f, [BI_ARM_YIELD - BI_ARM_NOP] = 0xd503203f,
			[BI_ARM_WFE - BI_ARM_NOP] = 0xd503205f, [BI_ARM_WFI - BI_ARM_NOP] = 0xd503207f,
			[BI_ARM_SEV - BI_ARM_NOP] = 0xd503209f, [BI_ARM_SEVL - BI_ARM_NOP] = 0xd50320bf,
			[BI_ARM_CLREX - BI_ARM_NOP] = 0xd5033f5f, [BI_ARM_ISB - BI_ARM_NOP] = 0xd50330df,
			[BI_ARM_DMB - BI_ARM_NOP] = 0xd50330bf, [BI_ARM_DSB - BI_ARM_NOP] = 0xd503309f,
		};
		cval cv = { 0 };

		if (x->aux >= BI_ARM_ISB && (!cc_eval(c, arg[0], &cv) || cv.kind != CV_INT))
			efatal(e, x->loc, "argument to '%s' must be a constant integer",
			       x->aux == BI_ARM_TRAP ? "__builtin_arm_trap" : "__builtin_arm_dmb");
		if (!e->reach)
			return vnone();
		if (x->aux == BI_ARM_TRAP)
			put(e, I_BRK)->imm = (int64_t)(cv.i & 0xffff);
		else
			put(e, I_WORD)->imm = w[x->aux - BI_ARM_NOP] | (x->aux >= BI_ARM_ISB ? (uint32_t)(cv.i & 15) << 8 : 0);
		return vnone();
	}
	case BI_ROTL8: case BI_ROTL16: case BI_ROTL32: case BI_ROTL64:
	case BI_ROTR8: case BI_ROTR16: case BI_ROTR32: case BI_ROTR64: {
		/* LSLV/LSRV take the count mod the register width: 32/64 bits as
		   (x << n) | (x >> -n); 8/16 bits with n & (w-1), w - n and a mask */
		int k = (x->aux - BI_ROTL8) & 3, w = 8 << k, ax = w == 64, left = x->aux < BI_ROTR8;
		val a = gen(e, arg[0]), b = gen(e, arg[1]);
		int ra = toreg(e, &a), rb = toreg(e, &b), t = treg(e);

		if (w < 32) {
			opi(e, I_ANDI, 0, ra, ra, (1 << w) - 1);
			opi(e, I_ANDI, 0, rb, rb, w - 1);
		}
		op3(e, I_SUB, ax, t, ZR, rb);		/* -n */
		if (w < 32)
			opi(e, I_ADDI, 0, t, t, w);	/* w - n */
		op3(e, left ? I_LSRV : I_LSLV, ax, t, ra, t);
		op3(e, left ? I_LSLV : I_LSRV, ax, ra, ra, rb);
		op3(e, I_ORR, ax, ra, ra, t);
		if (w < 32)
			opi(e, I_ANDI, 0, ra, ra, (1 << w) - 1);
		tfree(e, t);
		val_free(e, &b);
		return a;
	}
	case BI_DEBUGTRAP:
		if (e->reach)
			put(e, I_BRK)->imm = 0xf000;
		return vnone();
	case BI_RETURN_ADDRESS: case BI_FRAME_ADDRESS: {
		val depth = gen(e, arg[0]);
		int r = treg(e);
		uint32_t level;

		if (depth.k != V_CONST)
			efatal(e, x->loc, "a return or frame address requires a constant level");
		level = (uint32_t)depth.i;
		mov(e, 1, r, R_FP);
		if (level <= 4) {
			for (uint32_t k = 0; k < level; k++)
				mem(e, I_LD, 3, 0, r, r, 0);
		} else {
			int count = treg(e);
			uint32_t loop = newlabel(e);

			movi(e, 0, count, level);
			deflabel_loop(e, loop, true);
			mem(e, I_LD, 3, 0, r, r, 0);
			opi(e, I_SUBI, 0, count, count, 1);
			cbz(e, true, 0, count, loop);
			tfree(e, count);
		}
		if (x->aux == BI_RETURN_ADDRESS)
			mem(e, I_LD, 3, 0, r, r, 8);
		return vreg(r);
	}
	case BI_OBJECT_SIZE:
	case BI_DYNAMIC_OBJECT_SIZE: {
		cval cv;
		if (cc_eval(c, n, &cv) && cv.kind == CV_INT)
			return vconst(cv.i);
		return vconst((i128)(uint64_t)-1);
	}
	case BI_IS_CONSTANT_EVALUATED:
		return vconst(0);
	case BI_ADD_OVERFLOW: case BI_SUB_OVERFLOW: case BI_MUL_OVERFLOW:
		return gen_overflow(e, x, arg);
	case BI_ADDC: case BI_SUBC:
		return gen_carry(e, x, arg);
	case BI_PREFETCH: {
		val p = gen(e, arg[0]);
		int r = toreg(e, &p);

		if (e->reach)
			put(e, I_PRFM)->rn = (uint8_t)r;
		val_free(e, &p);
		return vnone();
	}
	case BI_ALLOCA:
	case BI_ALLOCA_UNINIT: {
		val s = gen(e, arg[0]);
		int r = toreg(e, &s);
		ins *i;

		e->dyn = true;
		opi(e, I_ADDI, 1, r, r, 15);
		opi(e, I_ANDI, 1, r, r, -16);
		op3(e, I_SUB, 1, SP, SP, r);
		if (e->reach) {
			i = put(e, I_ADDI);
			i->sz = 1, i->rd = (uint8_t)r, i->rn = SP, i->imm = 0, i->cond = 0xee;	/* + outgoing area */
		}
		return s;
	}
	case BI_ATOMIC_LOAD_N: case BI_ATOMIC_STORE_N: case BI_ATOMIC_EXCHANGE_N: case BI_ATOMIC_CMPXCHG_N:
	case BI_ATOMIC_LOAD: case BI_ATOMIC_STORE: case BI_ATOMIC_EXCHANGE: case BI_ATOMIC_CMPXCHG:
	case BI_ATOMIC_FETCH_ADD: case BI_ATOMIC_FETCH_SUB: case BI_ATOMIC_FETCH_AND: case BI_ATOMIC_FETCH_OR:
	case BI_ATOMIC_FETCH_XOR: case BI_ATOMIC_ADD_FETCH: case BI_ATOMIC_SUB_FETCH: case BI_ATOMIC_AND_FETCH:
	case BI_ATOMIC_OR_FETCH: case BI_ATOMIC_XOR_FETCH: case BI_ATOMIC_FETCH_NAND: case BI_ATOMIC_FETCH_MAX:
	case BI_ATOMIC_FETCH_MIN: case BI_ATOMIC_NAND_FETCH: case BI_ATOMIC_MAX_FETCH: case BI_ATOMIC_MIN_FETCH: case BI_SYNC_VAL_CAS: case BI_SYNC_BOOL_CAS:
		return gen_atomic(e, x, arg);
	case BI_ATOMIC_THREAD_FENCE: case BI_ATOMIC_SIGNAL_FENCE: {
		val m = gen(e, arg[0]);

		val_free(e, &m);
		if (e->reach && x->aux == BI_ATOMIC_THREAD_FENCE)
			put(e, I_WORD)->imm = 0xd5033bbf;	/* dmb ish */
		return vnone();
	}
	case BI_ATOMIC_LOCK_FREE: {
		for (uint32_t i = 0; i < x->c; i++) {	/* (size, p); __c11_atomic_is_lock_free: (size) */
			val a = gen(e, arg[i]);

			val_free(e, &a);
		}
		return vconst(1);
	}
	case BI_ARM_RSR: {
		uint32_t sn = strip_casts(c, arg[0]);
		int r = treg(e);

		if (N(sn)->op != N_STR)
			efatal(e, x->loc, "__builtin_arm_rsr64 needs a string literal");
		if (e->reach) {
			ins *i = put(e, I_MRS);

			i->rd = (uint8_t)r;
			i->ref = N(sn)->a;
		}
		return vreg(r);
	}
	case BI_CLZ: case BI_CLZL: case BI_CLZLL: case BI_CTZ: case BI_CTZL: case BI_CTZLL:
	case BI_FFS: case BI_FFSL: case BI_FFSLL: case BI_POPCOUNT: case BI_POPCOUNTL: case BI_POPCOUNTLL:
	case BI_PARITY: case BI_PARITYL: case BI_PARITYLL: case BI_BSWAP16: case BI_BSWAP32: case BI_BSWAP64:
	case BI_CLRSB: case BI_CLRSBL: case BI_CLRSBLL: case BI_BITREVERSE8: case BI_BITREVERSE16:
	case BI_BITREVERSE32: case BI_BITREVERSE64: {
		qty at = N(arg[0])->ty;
		int ax = tsize(c, at) == 8, r, t;

		v = gen(e, arg[0]);
		if (v.k == V_CONST) {
			u128 u = (u128)em_trunc_to(c, v.i, at) & (ax ? ~(u128)0 >> 64 : 0xffffffffu);
			unsigned bits = ax ? 64 : 32;
			i128 res = 0;

			if (tsize(c, at) == 2)
				u &= 0xffff;
			switch (x->aux) {
			case BI_CLZ: case BI_CLZL: case BI_CLZLL:
				if (!u)
					goto runtime;
				res = __builtin_clzll((uint64_t)u) - (64 - bits);
				break;
			case BI_CTZ: case BI_CTZL: case BI_CTZLL:
				if (!u)
					goto runtime;
				res = __builtin_ctzll((uint64_t)u);
				break;
			case BI_FFS: case BI_FFSL: case BI_FFSLL:
				res = u ? __builtin_ctzll((uint64_t)u) + 1 : 0;
				break;
			case BI_POPCOUNT: case BI_POPCOUNTL: case BI_POPCOUNTLL:
				res = __builtin_popcountll((uint64_t)u);
				break;
			case BI_PARITY: case BI_PARITYL: case BI_PARITYLL:
				res = __builtin_popcountll((uint64_t)u) & 1;
				break;
			case BI_BSWAP16: res = __builtin_bswap16((uint16_t)u); break;
			case BI_BSWAP32: res = __builtin_bswap32((uint32_t)u); break;
			case BI_BSWAP64: res = (i128)__builtin_bswap64((uint64_t)u); break;
			default:
				goto runtime;
			}
			return vconst(em_trunc_to(c, res, x->ty));
		}
runtime:
		r = toreg(e, &v);
		switch (x->aux) {
		case BI_CLZ: case BI_CLZL: case BI_CLZLL:
			op3(e, I_CLZ, ax, r, r, 0);
			break;
		case BI_CTZ: case BI_CTZL: case BI_CTZLL:
			op3(e, I_RBIT, ax, r, r, 0);
			op3(e, I_CLZ, ax, r, r, 0);
			break;
		case BI_FFS: case BI_FFSL: case BI_FFSLL:
			t = treg(e);
			op3(e, I_RBIT, ax, t, r, 0);
			op3(e, I_CLZ, ax, t, t, 0);
			op3(e, I_SUBS, ax, ZR, r, ZR);
			op3s(e, I_CSINC, 0, r, ZR, t, C_EQ, 0);
			tfree(e, t);
			break;
		case BI_POPCOUNT: case BI_POPCOUNTL: case BI_POPCOUNTLL:
			return popcount(e, v, ax);
		case BI_PARITY: case BI_PARITYL: case BI_PARITYLL:
			v = popcount(e, v, ax);
			opi(e, I_ANDI, 0, v.r, v.r, 1);
			return v;
		case BI_BSWAP16:
			op3(e, I_REV16, 0, r, r, 0);
			break;
		case BI_BSWAP32:
			op3(e, I_REV, 0, r, r, 0);
			break;
		case BI_BSWAP64:
			op3(e, I_REV, 1, r, r, 0);
			break;
		case BI_CLRSB: case BI_CLRSBL: case BI_CLRSBLL:
			op3(e, I_CLS, ax, r, r, 0);
			break;
		case BI_BITREVERSE8:
			op3(e, I_RBIT, 0, r, r, 0);
			shift_imm(e, I_LSRV, 0, r, r, 24);
			break;
		case BI_BITREVERSE16:
			op3(e, I_RBIT, 0, r, r, 0);
			shift_imm(e, I_LSRV, 0, r, r, 16);
			break;
		case BI_BITREVERSE32:
			op3(e, I_RBIT, 0, r, r, 0);
			break;
		case BI_BITREVERSE64:
			op3(e, I_RBIT, 1, r, r, 0);
			break;
		}
		return v;
	}
	case BI_ISGREATER: case BI_ISGREATEREQUAL: case BI_ISLESS: case BI_ISLESSEQUAL:
	case BI_ISLESSGREATER: case BI_ISUNORDERED: {
		qty at = N(arg[0])->ty, bt = N(arg[1])->ty;
		qty ct = is_ld(c, at) ? at : is_ld(c, bt) ? bt :
		         (is_flt(c, at) && tsize(c, at) == 8) ||
		         (is_flt(c, bt) && tsize(c, bt) == 8) ? prim(TY_DOUBLE) : prim(TY_FLOAT);
		val a = cast(e, gen(e, arg[0]), at, ct, CK_INT);
		val b = cast(e, gen(e, arg[1]), bt, ct, CK_INT);
		int op = x->aux == BI_ISGREATER ? T_GREATER :
		         x->aux == BI_ISGREATEREQUAL ? T_GREATEREQ :
		         x->aux == BI_ISLESS ? T_LESS :
		         x->aux == BI_ISLESSEQUAL ? T_LESSEQ : T_EXCLAIMEQ;

		if (!is_ld(c, ct)) {
			int cond = f_cmp(e, op, fpk(e, ct), &a, &b);
			val_free(e, &b);
			if (x->aux == BI_ISUNORDERED)
				cond = C_VS;
			cset(e, a.r, cond);
			if (x->aux == BI_ISLESSGREATER) {
				int ordered = treg(e);
				cset(e, ordered, C_VC);
				op3(e, I_AND, 0, a.r, a.r, ordered);
				tfree(e, ordered);
			}
			return vreg(a.r);
		}
		if (x->aux != BI_ISUNORDERED && x->aux != BI_ISLESSGREATER)
			return f_compare(e, op, ct, a, b);

		/* Binary128 NaNs: exponent all ones and a nonzero fraction.
		 * Inspect bits so quiet NaNs do not raise FE_INVALID. Keep the
		 * original operands for the ordered inequality when needed. */
		lval saved[2];
		val operands[2] = { a, b };
		int nanregs[2];
		for (int i = 0; i < 2; i++) {
			if (x->aux == BI_ISLESSGREATER) {
				saved[i] = (lval){ .k = L_FRAME, .off = slot(e, 16, 16), .align = 16 };
				store(e, &saved[i], ct, &operands[i]);
			}
			toreg2(e, &operands[i], false);
			int lo = operands[i].r, hi = operands[i].r2;
			opi(e, I_SUBSI, 1, ZR, lo, 0);
			cset(e, lo, C_NE);
			shift_imm(e, T_LESSLESS, 1, hi, hi, 1);
			op3(e, I_ORR, 1, hi, hi, lo);
			tfree(e, lo);
			opi(e, I_SUBSI, 1, ZR, hi, (int64_t)0xfffe000000000000ULL);
			cset(e, hi, C_HI);
			nanregs[i] = hi;
		}
		op3(e, I_ORR, 0, nanregs[0], nanregs[0], nanregs[1]);
		tfree(e, nanregs[1]);
		if (x->aux == BI_ISUNORDERED)
			return vreg(nanregs[0]);
		opi(e, I_EORI, 0, nanregs[0], nanregs[0], 1);
		a = load(e, &saved[0], ct);
		b = load(e, &saved[1], ct);
		val ne = f_compare(e, T_EXCLAIMEQ, ct, a, b);
		toreg(e, &ne);
		op3(e, I_AND, 0, ne.r, ne.r, nanregs[0]);
		tfree(e, nanregs[0]);
		return ne;
	}
	case BI_ISSIGNALING: {
		qty at = N(arg[0])->ty;
		val a = gen(e, arg[0]);
		int r, d;
		uint64_t inf, quiet;
		if (is_ld(c, at)) {
			/* Fold low payload bits into a sticky bit after discarding sign. */
			toreg2(e, &a, false);
			r = a.r2, d = 1;
			opi(e, I_SUBSI, 1, ZR, a.r, 0);
			cset(e, a.r, C_NE);
			shift_imm(e, T_LESSLESS, 1, r, r, 1);
			op3(e, I_ORR, 1, r, r, a.r);
			tfree(e, a.r);
			inf = UINT64_C(0xfffe000000000000), quiet = UINT64_C(1) << 48;
		} else {
			bool half = KIND(c, at) == TY_FLOAT16 || KIND(c, at) == TY_BFLOAT16;
			bool bf = KIND(c, at) == TY_BFLOAT16;
			d = fpk(e, at);
			r = toreg(e, &a);
			opi(e, I_ANDI, d, r, r, half ? 0x7fff : d ? INT64_MAX : 0x7fffffff);
			inf = bf ? UINT64_C(0x7f80) : half ? UINT64_C(0x7c00) : d ? UINT64_C(0x7ff0000000000000) : UINT64_C(0x7f800000);
			quiet = UINT64_C(1) << (bf ? 6 : half ? 9 : d ? 51 : 22);
		}
		opi(e, I_SUBI, d, r, r, (int64_t)(inf + 1));
		opi(e, I_SUBSI, d, ZR, r, (int64_t)(quiet - 1));
		cset(e, r, C_LO);
		return vreg(r);
	}
	case BI_ISNAN: case BI_ISINF: case BI_ISFINITE: case BI_SIGNBIT: case BI_ISNORMAL: {
		cval cv;

		if (cc_eval(c, n, &cv) && cv.kind == CV_INT)
			return vconst(cv.i);
		/* on the bit pattern: sign out (<< 1), then the exponent against all ones */
		qty at = N(arg[0])->ty;
		bool ld = is_ld(c, at);
		int d = ld || fpk(e, at), bits = d ? 64 : 32, mant = ld ? 48 : d ? 52 : 23, r;

		v = gen(e, arg[0]);
		if (ld) {
			/* the high word, with a nonzero low word as a sticky bit 0 (below the shift) */
			toreg2(e, &v, false);
			r = v.r2;
			if (x->aux != BI_SIGNBIT) {
				opi(e, I_SUBSI, 1, ZR, v.r, 0);
				cset(e, v.r, C_NE);
				shift_imm(e, T_LESSLESS, 1, r, r, 1);
				op3(e, I_ORR, 1, r, r, v.r);
				tfree(e, v.r);
				if (x->aux == BI_ISNORMAL) {
					shift_imm(e, I_LSRV, 1, r, r, mant + 1);
					opi(e, I_SUBI, 1, r, r, 1);
					opi(e, I_SUBSI, 1, ZR, r, 0x7fff - 1);
					cset(e, r, C_LO);
					return vreg(r);
				}
				opi(e, I_SUBSI, 1, ZR, r, (int64_t)0xfffe000000000000ULL);
				cset(e, r, x->aux == BI_ISNAN ? C_HI : x->aux == BI_ISINF ? C_EQ : C_LO);
				return vreg(r);
			}
			tfree(e, v.r);
		} else {
			r = toreg(e, &v);
		}
		if (KIND(c, at) == TY_FLOAT16 || KIND(c, at) == TY_BFLOAT16) {
			bool bf = KIND(c, at) == TY_BFLOAT16;
			if (x->aux == BI_SIGNBIT) {
				shift_imm(e, I_LSRV, 0, r, r, 15);
				opi(e, I_ANDI, 0, r, r, 1);
				return vreg(r);
			}
			opi(e, I_ANDI, 0, r, r, 0x7fff);
			if (x->aux == BI_ISNORMAL) {
				shift_imm(e, I_LSRV, 0, r, r, bf ? 7 : 10);
				opi(e, I_SUBI, 0, r, r, 1);
				opi(e, I_SUBSI, 0, ZR, r, bf ? 254 : 30);
				cset(e, r, C_LO);
			} else {
				opi(e, I_SUBSI, 0, ZR, r, bf ? 0x7f80 : 0x7c00);
				cset(e, r, x->aux == BI_ISNAN ? C_HI : x->aux == BI_ISINF ? C_EQ : C_LO);
			}
			return vreg(r);
		}
		if (x->aux == BI_SIGNBIT) {
			shift_imm(e, I_LSRV, d, r, r, bits - 1);
			return vreg(r);
		}
		shift_imm(e, T_LESSLESS, d, r, r, 1);
		if (x->aux == BI_ISNORMAL) {
			shift_imm(e, I_LSRV, d, r, r, mant + 1);
			opi(e, I_SUBI, d, r, r, 1);
			opi(e, I_SUBSI, d, ZR, r, (bits == 64 ? 0x7ff : 0xff) - 1);
			cset(e, r, C_LO);
			return vreg(r);
		}
		opi(e, I_SUBSI, d, ZR, r, d ? (int64_t)0xffe0000000000000ULL : 0xff000000LL);
		cset(e, r, x->aux == BI_ISNAN ? C_HI : x->aux == BI_ISINF ? C_EQ : C_LO);
		return vreg(r);
	}
	default: {
		cval cv;

		if (cc_eval(c, n, &cv) && cv.kind == CV_INT)
			return vconst(cv.i);
		if (cc_eval(c, n, &cv) && cv.kind == CV_FLT && is_flt(c, x->ty))
			return f_const_cv(e, x->ty, &cv);
		efatal(e, x->loc, "unsupported builtin (%d)", x->aux);
	}
	}
}
