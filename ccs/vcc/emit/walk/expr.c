/* vcc code generator, AST walker: expressions, lvalues, conditions (walk.h) */
#include "walk.h"
#include "../../lib/vstack.h"
#include "../../lib/f128.h"

/* ---------------- types and values ---------------- */

VPRIV int wk_size(cc *c, qty t)
{
	uint64_t n = KIND(c, t) == TY_VOID ? 0 : tsize(c, t);

	return n > 8 ? 16 : (int)n;
}

VPRIV bool wk_signed(cc *c, qty t) { return !is_ptr(c, t) && is_integer(c, t) && is_signed(c, t); }

/* the width an integer operation of type t runs at */
static int opw(cc *c, qty t) { return wk_size(c, t) > 4 ? 8 : 4; }

static __attribute__((noinline, cold, noreturn)) void wk_unsup(ecx *e, uint32_t n, const char *what)
{
	cc *c = e->c;

	efatal(e, N(n)->loc, "x86-64: %s is not supported yet (PLAN-x86.md X3)", what);
}

static void wk_scalar(ecx *e, uint32_t n, qty t)
{
	cc *c = e->c;
	int k = KIND(c, t);

	if (k == TY_FLOAT16 || k == TY_FLOAT128 || ((k == TY_INT128 || k == TY_UINT128) && !LO(i128mem)()))
		wk_unsup(e, n, k == TY_FLOAT16 ? "_Float16" : k == TY_FLOAT128 ? "__float128" : "__int128");
	if (k == TY_VECTOR)
		wk_unsup(e, n, "a vector value");	/* (records are values by address) */
}

static bool wk_wide(cc *c, qty t) { return KIND(c, t) == TY_INT128 || KIND(c, t) == TY_UINT128; }

VPRIV bool wk_mem(ecx *e, qty t)
{
	cc *c = e->c;

	return is_record(c, t) || is_array(c, t) || (KIND(c, t) == TY_LDOUBLE && LO(ldmem)()) || (wk_wide(c, t) && LO(i128mem)());
}

/* a float value's register (float/double: the CPU's float registers) */
static bool wk_freg(ecx *e, qty t) { return is_flt(e->c, t) && !wk_mem(e, t); }

/* a new temporary for a value of type t */
static int wk_treg(ecx *e, qty t) { return wk_freg(e, t) ? LO(ftreg)(e) : LO(treg)(e); }

VPRIV int wk_reg(ecx *e, val *v, qty t)
{
	return LO(toreg)(e, v, wk_size(e->c, t) > 4 ? 8 : 4);
}

VPRIV void wk_free(ecx *e, const val *v)
{
	if (v->k == V_REG)
		LO(tfree)(e, v->r);
}

static val wk_vreg(int r) { return (val){ .k = V_REG, .r = (uint8_t)r }; }

/* an address that may be 0 at run time: an undefined weak symbol's */
static bool wk_weak0(cc *c, val v)
{
	const sym *y;

	if (v.k != V_ADDR || RKIND(v.ref) != RK_SYM)
		return false;
	y = &c->syms[RIDX(v.ref)];
	return (y->flags & SF_WEAK) && !(y->flags & SF_DEFINED);
}

/* v as a value of type t (truncated as an integer of t) */
static val wk_const(cc *c, i128 v, qty t) { return vconst(em_trunc_to(c, v, t)); }

/* ---------------- folding: both operands constant ---------------- */

static bool wk_fold(cc *c, int op, qty t, i128 a, i128 b, i128 *r)
{
	bool sg = wk_signed(c, t);
	int bits = wk_size(c, t) * 8;
	u128 ua = (u128)em_trunc_to(c, a, t), ub = (u128)em_trunc_to(c, b, t);

	a = em_trunc_to(c, a, t), b = em_trunc_to(c, b, t);
	switch (op) {
	case T_PLUS: *r = (i128)(ua + ub); break;
	case T_MINUS: *r = (i128)(ua - ub); break;
	case T_STAR: *r = (i128)(ua * ub); break;
	case T_AMP: *r = a & b; break;
	case T_PIPE: *r = a | b; break;
	case T_CARET: *r = a ^ b; break;
	case T_SLASH: case T_PERCENT:
		if (!b || (sg && b == -1))
			return false;	/* trap or overflow: at run time */
		*r = sg ? (op == T_SLASH ? a / b : a % b) : (i128)(op == T_SLASH ? ua / ub : ua % ub);
		break;
	case T_LESSLESS:
		if (b < 0 || b >= bits)
			return false;
		*r = (i128)(ua << (int)b);
		break;
	case T_GREATERGREATER:
		if (b < 0 || b >= bits)
			return false;
		*r = sg ? a >> (int)b : (i128)(ua >> (int)b);
		break;
	default:
		return false;
	}
	*r = em_trunc_to(c, *r, t);
	return true;
}

static bool wk_is_cmp(int op)
{
	return op == T_LESS || op == T_LESSEQ || op == T_GREATER || op == T_GREATEREQ || op == T_EQUALEQUAL || op == T_EXCLAIMEQ;
}

static bool wk_fold_cmp(int op, bool sg, i128 a, i128 b)
{
	u128 ua = (u128)a, ub = (u128)b;

	switch (op) {
	case T_LESS: return sg ? a < b : ua < ub;
	case T_LESSEQ: return sg ? a <= b : ua <= ub;
	case T_GREATER: return sg ? a > b : ua > ub;
	case T_GREATEREQ: return sg ? a >= b : ua >= ub;
	case T_EQUALEQUAL: return a == b;
	default: return a != b;
	}
}

/* ---------------- lvalues ---------------- */

static lval wk_fromval(ecx *e, val v, qty t)
{
	cc *c = e->c;
	lval l = { 0 };

	if (v.k == V_ADDR) {
		l.k = L_SYM;
		l.ref = v.ref;
		l.off = (int64_t)v.i;
	} else {
		l.k = L_REG;
		l.r = (uint8_t)wk_reg(e, &v, prim(TY_LONG));
	}
	l.align = KIND(c, t) == TY_VOID ? 1 : talign(c, t);
	return l;
}

static void wk_lfree(ecx *e, const lval *l)
{
	if (l->k == L_REG)
		LO(tfree)(e, l->r);
}

/* a local's frame slot, made on first use */
VPRIV int32_t wk_local(ecx *e, uint32_t s)
{
	wkst *W = e->wk;
	cc *c = e->c;

	if (W->sgen[s] != W->gen) {
		W->sgen[s] = W->gen;
		W->soff[s] = wk_slot(e, tsize(c, c->syms[s].ty) ? tsize(c, c->syms[s].ty) : 1, sym_align(c, s));
	}
	return W->soff[s];
}

VPRIV lval wk_lval(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	lval l = { 0 };

	switch (x->op) {
	case N_VAR: {
		uint32_t s = x->a, cs;

		if (is_local_var(c, s)) {
			l.k = L_FRAME;
			l.off = wk_local(e, s);
			l.align = sym_align(c, s);
			return l;
		}
		cs = canon_sym(c, s);
		if ((c->syms[cs].flags | c->syms[s].flags) & SF_THREAD)
			wk_unsup(e, n, "a thread-local variable");
		note_ref(e, REF(RK_SYM, cs));
		l.k = L_SYM;
		l.ref = REF(RK_SYM, cs);
		l.align = sym_align(c, cs);
		return l;
	}
	case N_STR:
		note_ref(e, REF(RK_STR, x->a));
		l.k = L_SYM;
		l.ref = REF(RK_STR, x->a);
		l.align = c->strs[x->a].esz;
		return l;
	case N_DEREF:
		return wk_fromval(e, wk_gen(e, x->a), T(c, N(x->a)->ty)->base);
	case N_MEMBER: {
		const field *f = &c->tags[x->b].f[x->c];
		const node *b = N(x->a);

		if (b->flags & NF_LVALUE || b->op == N_VAR || b->op == N_DEREF || b->op == N_MEMBER || b->op == N_INDEX ||
		    b->op == N_STR || b->op == N_COMPLIT)
			l = wk_lval(e, x->a);
		else
			l = wk_fromval(e, wk_gen(e, x->a), b->ty);	/* a record rvalue is its address */
		l.off += (int64_t)(f->off / 8);
		if (f->bitfield) {
			l.bf = 1;
			l.bitoff = (uint16_t)(f->off % 8);
			l.width = f->width;
			l.sgn = is_signed(c, f->ty);
		}
		return l;
	}
	case N_INDEX: {
		qty pt = N(x->a)->ty, et = T(c, pt)->base;
		int64_t es = (int64_t)tsize(c, et);
		val p = wk_gen(e, x->a), i = wk_gen(e, x->b);

		if (ptr_to_vla(c, pt))
			wk_unsup(e, n, "a variable-length array");
		if (i.k == V_CONST && p.k == V_ADDR) {
			l.k = L_SYM;
			l.ref = p.ref;
			l.off = (int64_t)(p.i + i.i * es);
			l.align = talign(c, et);
			return l;
		}
		if (i.k == V_CONST && i.i * es >= INT32_MIN && i.i * es <= INT32_MAX) {
			l = wk_fromval(e, p, et);
			l.off += (int64_t)(i.i * es);
			return l;
		}
		/* p + i * es: i extended to 64 bits as its type, then scaled */
		{
			qty it = N(x->b)->ty;
			int ri = wk_reg(e, &i, it);

			if (wk_size(c, it) < 8)
				LO(ext)(e, ri, wk_size(c, it), wk_signed(c, it));
			p = LO(bin)(e, T_PLUS, 8, false, p, LO(bin)(e, T_STAR, 8, false, wk_vreg(ri), vconst(es)));
			return wk_fromval(e, p, et);
		}
	}
	case N_CAST:
		if (x->aux == CK_NOP)
			return wk_lval(e, x->a);
		break;
	case N_COMMA: {
		val v = wk_gen(e, x->a);

		wk_free(e, &v);
		return wk_lval(e, x->b);
	}
	case N_COMPLIT: {
		/* (T){ ... }: an unnamed object (b), initialized where it is reached */
		uint32_t s = x->b;

		l.k = L_FRAME;
		l.off = wk_local(e, s);
		l.align = sym_align(c, s);
		wk_init(e, &l, c->syms[s].ty, x->a);
		return l;
	}
	default:
		if (is_record(c, x->ty))
			return wk_fromval(e, wk_gen(e, n), x->ty);	/* a call, ?:, ({ }) ...: the address it gives */
		break;
	}
	wk_unsup(e, n, "this lvalue");
}

/* ---------------- bit-fields ---------------- */

static int64_t wk_floordiv(int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

/* the unit a bit-field is accessed in: its type's size, aligned, or wider
   for one that crosses it (packed); its offset and the field's bit in it */
static void wk_bf_unit(ecx *e, const lval *l, qty t, int64_t *uoff, int *us, int *pos)
{
	int64_t bit = l->off * 8 + l->bitoff;
	int s = wk_size(e->c, t);

	for (s = s < 1 ? 1 : s; s <= 8; s *= 2) {
		int64_t ub = wk_floordiv(bit, s * 8);
		int p = (int)(bit - ub * s * 8);

		if (p + l->width <= s * 8) {
			*uoff = ub * s, *us = s, *pos = p;
			return;
		}
	}
	efatal(e, e->curloc, "x86-64: a bit-field across 8 bytes is not supported yet (PLAN-x86.md X3)");
}

/* a bit-field's value in a new register (l stays) */
static val wk_bf_load(ecx *e, const lval *l, qty t)
{
	lval u = *l;
	int us, pos, w = l->width;
	int64_t uo;
	val v;

	wk_bf_unit(e, l, t, &uo, &us, &pos);
	u.bf = 0, u.off = uo;
	v = (val){ .k = V_REG, .r = (uint8_t)LO(treg)(e) };
	LO(load)(e, v.r, &u, us, false);
	if (l->sgn) {
		if (64 - pos - w)
			v = LO(bin)(e, T_LESSLESS, 8, false, v, vconst(64 - pos - w));
		if (64 - w)
			v = LO(bin)(e, T_GREATERGREATER, 8, true, v, vconst(64 - w));
		return v;
	}
	if (pos)
		v = LO(bin)(e, T_GREATERGREATER, 8, false, v, vconst(pos));
	if (w < 64)
		v = LO(bin)(e, T_AMP, 8, false, v, vconst((i128)(((uint64_t)1 << w) - 1)));
	return v;
}

/* the low bits of rv into the bit-field (rv stays) */
static void wk_bf_store(ecx *e, const lval *l, qty t, int rv)
{
	lval u = *l;
	int us, pos, w = l->width;
	int64_t uo;
	uint64_t m;
	val old, nv;

	wk_bf_unit(e, l, t, &uo, &us, &pos);
	u.bf = 0, u.off = uo;
	m = (w >= 64 ? ~(uint64_t)0 : (((uint64_t)1 << w) - 1)) << pos;
	if (us < 8)
		m &= ((uint64_t)1 << (us * 8)) - 1;
	old = (val){ .k = V_REG, .r = (uint8_t)LO(treg)(e) };
	LO(load)(e, old.r, &u, us, false);
	old = LO(bin)(e, T_AMP, 8, false, old, vconst((i128)(int64_t)~m));
	nv = (val){ .k = V_REG, .r = (uint8_t)LO(treg)(e) };
	LO(mov)(e, nv.r, rv);
	if (pos)
		nv = LO(bin)(e, T_LESSLESS, 8, false, nv, vconst(pos));
	nv = LO(bin)(e, T_AMP, 8, false, nv, vconst((i128)(int64_t)m));
	old = LO(bin)(e, T_PIPE, 8, false, old, nv);
	LO(store)(e, old.r, &u, us);
	LO(tfree)(e, old.r);
}

/* r stored to l as type t (a bit-field or not); r stays */
static void wk_put(ecx *e, const lval *l, qty t, int r)
{
	if (l->bf)
		wk_bf_store(e, l, t, r);
	else
		LO(store)(e, r, l, wk_size(e->c, t));
}

VPRIV void wk_bfinit(ecx *e, const lval *l, qty t, uint32_t init)
{
	val v = wk_gen(e, init);
	int r = wk_reg(e, &v, t);

	wk_bf_store(e, l, t, r);
	LO(tfree)(e, r);
}

static val wk_load(ecx *e, lval l, qty t)
{
	cc *c = e->c;
	int r;

	if (is_func(c, t) || wk_mem(e, t)) {
		/* the object itself: its address (arrays and functions decay; records, x87 long double) */
		if (l.k == L_SYM)
			return (val){ .k = V_ADDR, .ref = l.ref, .i = l.off };
		return wk_vreg(LO(laddr)(e, &l));
	}
	if (l.bf) {
		val v = wk_bf_load(e, &l, t);

		wk_lfree(e, &l);
		return v;
	}
	if (wk_freg(e, t)) {
		r = LO(ftreg)(e);
		LO(load)(e, r, &l, wk_size(c, t), false);
		wk_lfree(e, &l);
		return wk_vreg(r);
	}
	r = l.k == L_REG ? l.r : LO(treg)(e);
	LO(load)(e, r, &l, wk_size(c, t), wk_signed(c, t) || KIND(c, t) == TY_BOOL ? wk_signed(c, t) : false);
	return wk_vreg(r);
}

/* v stored to l as type t; v is consumed, l is not */
static void wk_store(ecx *e, const lval *l, qty t, val *v)
{
	int r = wk_reg(e, v, t);

	LO(store)(e, r, l, wk_size(e->c, t));
	LO(tfree)(e, r);
}

/* ---------------- conversions ---------------- */

/* v (of type from) as type to */
static val wk_conv(ecx *e, val v, qty from, qty to)
{
	cc *c = e->c;
	int fs = wk_size(c, from), ts = wk_size(c, to);

	if (KIND(c, to) == TY_VOID) {
		wk_free(e, &v);
		return vconst(0);
	}
	if (wk_wide(c, from) || wk_wide(c, to)) {
		if (wk_wide(c, from) && wk_wide(c, to))
			return v;
		return LO(wcvt)(e, v, from, to);
	}
	if (is_flt(c, from) || is_flt(c, to)) {
		if (KIND(c, from) == KIND(c, to))
			return v;
		return LO(cvt)(e, v, from, to);
	}
	if (KIND(c, to) == TY_BOOL) {
		if (v.k == V_CONST)
			return vconst(v.i != 0);
		if (v.k == V_ADDR && !wk_weak0(c, v))
			return vconst(1);
		return LO(cmp)(e, T_EXCLAIMEQ, fs > 4 ? 8 : 4, false, v, vconst(0));
	}
	if (v.k == V_CONST)
		return wk_const(c, v.i, to);
	if (v.k == V_ADDR)
		return v;
	if (ts > fs && fs < 8)
		LO(ext)(e, v.r, fs, wk_signed(c, from));
	else if (ts < fs && ts < 4)
		LO(ext)(e, v.r, ts, wk_signed(c, to));	/* narrowed: keep the register extended as its type */
	return v;
}

static val wk_cast(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	qty from = N(x->a)->ty;

	switch (x->aux) {
	case CK_DECAY: {
		lval l = wk_lval(e, x->a);

		if (l.k == L_SYM)
			return (val){ .k = V_ADDR, .ref = l.ref, .i = l.off };
		return wk_vreg(LO(laddr)(e, &l));
	}
	case CK_FDECAY:
		if (N(x->a)->op == N_VAR) {
			uint32_t s = canon_sym(c, N(x->a)->a);

			note_ref(e, REF(RK_SYM, s));
			return (val){ .k = V_ADDR, .ref = REF(RK_SYM, s) };
		}
		return wk_gen(e, x->a);
	case CK_VOID: {
		val v = wk_gen(e, x->a);

		wk_free(e, &v);
		return vconst(0);
	}
	case CK_NULL:
		return vconst(0);
	case CK_NOP: case CK_INT: case CK_BOOL: case CK_P2I: case CK_I2P: case CK_P2P: case CK_I2F: case CK_F2I: case CK_F2F:
		wk_scalar(e, n, x->ty);
		wk_scalar(e, x->a, from);
		return wk_conv(e, wk_gen(e, x->a), is_ptr(c, from) ? prim(TY_ULONG) : from, is_ptr(c, x->ty) ? prim(TY_ULONG) : x->ty);
	default:
		wk_unsup(e, n, "this conversion");
	}
}

/* ---------------- operators ---------------- */

/* a op b for integers of type t (the usual conversions are already in the tree) */
static val wk_arith(ecx *e, int op, qty t, val a, val b)
{
	cc *c = e->c;
	i128 r;

	if (is_flt(c, t))
		return LO(fbin)(e, op, t, a, b);
	if (wk_wide(c, t))
		return LO(wbin)(e, op, wk_signed(c, t), a, b);
	if (a.k == V_CONST && b.k == V_CONST && wk_fold(c, op, t, a.i, b.i, &r))
		return vconst(r);
	if (a.k == V_ADDR && b.k == V_CONST && (op == T_PLUS || op == T_MINUS))
		return (val){ .k = V_ADDR, .ref = a.ref, .i = op == T_PLUS ? a.i + b.i : a.i - b.i };
	{
		val v = LO(bin)(e, op, opw(c, t), wk_signed(c, t), a, b);

		if (wk_size(c, t) < 4)
			LO(ext)(e, v.r, wk_size(c, t), wk_signed(c, t));
		return v;
	}
}

/* p +- i for p a pointer to elements of es bytes, i of type it */
static val wk_ptradd(ecx *e, val p, val i, qty it, int64_t es, bool sub)
{
	cc *c = e->c;

	if (i.k == V_CONST)
		return wk_arith(e, sub ? T_MINUS : T_PLUS, prim(TY_ULONG), p, vconst(i.i * es));
	{
		int ri = wk_reg(e, &i, it);

		if (wk_size(c, it) < 8)
			LO(ext)(e, ri, wk_size(c, it), wk_signed(c, it));
		i = wk_vreg(ri);
		if (es != 1)
			i = LO(bin)(e, T_STAR, 8, false, i, vconst(es));
		return LO(bin)(e, sub ? T_MINUS : T_PLUS, 8, false, p, i);
	}
}

/* the right operand next: if temporaries are short, the left waits in a slot */
static val wk_rhs(ecx *e, val *a, uint32_t rn, qty at)
{
	if (a->k == V_REG && LO(nfree)(e) < 3) {
		int32_t sl = wk_slot(e, 8, 8);
		lval l = { .k = L_FRAME, .off = sl, .align = 8 };
		val b;

		LO(store)(e, a->r, &l, 8);
		LO(tfree)(e, a->r);
		b = wk_gen(e, rn);
		a->r = (uint8_t)wk_treg(e, at);
		LO(load)(e, a->r, &l, 8, false);
		return b;
	}
	return wk_gen(e, rn);
}

static val wk_binary(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	int op = x->aux;
	qty lt = N(x->a)->ty, rt = N(x->b)->ty;
	val a, b;

	if (wk_is_cmp(op)) {
		wk_scalar(e, x->a, lt);
		wk_scalar(e, x->b, rt);
		a = wk_gen(e, x->a);
		b = wk_rhs(e, &a, x->b, lt);
		if (is_flt(c, lt))
			return LO(fcmp)(e, op, lt, a, b);
		if (wk_wide(c, lt))
			return LO(wcmp)(e, op, wk_signed(c, lt), a, b);
		if (a.k == V_CONST && b.k == V_CONST)
			return vconst(wk_fold_cmp(op, wk_signed(c, lt), em_trunc_to(c, a.i, lt), em_trunc_to(c, b.i, lt)));
		return LO(cmp)(e, op, is_ptr(c, lt) ? 8 : opw(c, lt), wk_signed(c, lt), a, b);
	}
	wk_scalar(e, n, x->ty);
	a = wk_gen(e, x->a);
	b = wk_rhs(e, &a, x->b, lt);
	if (is_ptr(c, lt) && is_ptr(c, rt) && op == T_MINUS) {
		int64_t es = (int64_t)tsize(c, T(c, lt)->base);
		val d = wk_arith(e, T_MINUS, prim(TY_LONG), a, b);

		if (es <= 1)
			return d;
		return wk_arith(e, T_SLASH, prim(TY_LONG), d, vconst(es));
	}
	if (is_ptr(c, lt) && (op == T_PLUS || op == T_MINUS))
		return wk_ptradd(e, a, b, rt, (int64_t)tsize(c, T(c, lt)->base) ?: 1, op == T_MINUS);
	if (is_ptr(c, rt) && op == T_PLUS)
		return wk_ptradd(e, b, a, lt, (int64_t)tsize(c, T(c, rt)->base) ?: 1, false);
	return wk_arith(e, op, x->ty, a, b);
}

/* a value 0 or 1 from a condition */
static val wk_condval(ecx *e, uint32_t n)
{
	int32_t sl = wk_slot(e, 4, 4);
	lval l = { .k = L_FRAME, .off = sl, .align = 4 };
	uint32_t lf = wk_newlabel(e), lj = wk_newlabel(e);
	val one = vconst(1), zero = vconst(0);
	int r;

	wk_cond(e, n, false, lf);
	wk_store(e, &l, prim(TY_INT), &one);
	LO(jump)(e, lj);
	LO(label)(e, lf);
	wk_store(e, &l, prim(TY_INT), &zero);
	LO(label)(e, lj);
	r = LO(treg)(e);
	LO(load)(e, r, &l, 4, false);
	return wk_vreg(r);
}

/* a ? b : c (b == 0: a ?: c) */
static val wk_ternary(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	qty t = x->ty;
	bool vd = KIND(c, t) == TY_VOID;
	int32_t sl = vd ? 0 : wk_slot(e, 8, 8);
	lval l = { .k = L_FRAME, .off = sl, .align = 8 };
	uint32_t lf = wk_newlabel(e), lj = wk_newlabel(e);
	val v;
	int r;

	if (!vd)
		wk_scalar(e, n, t);
	if (!x->b) {
		val a = wk_gen(e, x->a);
		int ra = wk_reg(e, &a, N(x->a)->ty);

		LO(store)(e, ra, &l, 8);
		LO(brz)(e, a, opw(c, N(x->a)->ty), false, lf);
		LO(jump)(e, lj);
	} else {
		wk_cond(e, x->a, false, lf);
		v = wk_gen(e, x->b);
		if (vd) {
			wk_free(e, &v);
		} else {
			r = wk_reg(e, &v, t);
			LO(store)(e, r, &l, 8);
			LO(tfree)(e, r);
		}
		LO(jump)(e, lj);
	}
	LO(label)(e, lf);
	v = wk_gen(e, x->c);
	if (vd) {
		wk_free(e, &v);
	} else {
		r = wk_reg(e, &v, t);
		LO(store)(e, r, &l, 8);
		LO(tfree)(e, r);
	}
	LO(label)(e, lj);
	if (vd)
		return vconst(0);
	r = wk_treg(e, t);
	LO(load)(e, r, &l, 8, false);
	return wk_vreg(r);
}

/* l's value in a new register (l and its register stay) */
static val wk_reload(ecx *e, const lval *l, qty t)
{
	int r;

	if (l->bf)
		return wk_bf_load(e, l, t);
	if (wk_mem(e, t)) {
		/* the object's address in a new register (l keeps its own) */
		lval t2 = *l;

		if (t2.k == L_REG) {
			t2.r = (uint8_t)LO(treg)(e);
			LO(mov)(e, t2.r, l->r);
		}
		return wk_vreg(LO(laddr)(e, &t2));
	}
	r = wk_treg(e, t);
	LO(load)(e, r, l, wk_size(e->c, t), wk_signed(e->c, t));
	return wk_vreg(r);
}

/* the bytes of the object at v (type t, held by address) into l; l's address is the value */
static val wk_putmem(ecx *e, lval *l, qty t, val v)
{
	int rs = wk_reg(e, &v, prim(TY_ULONG)), rd = LO(laddr)(e, l);

	LO(copy)(e, rd, rs, tsize(e->c, t));
	LO(tfree)(e, rs);
	return wk_vreg(rd);
}

/* the type l op r is computed in, for l of type lt op= r of type rt */
static qty wk_optype(cc *c, int op, qty lt, qty rt)
{
	qty pl = wk_size(c, lt) >= 4 ? lt : prim(TY_INT);

	if (is_flt(c, lt) || is_flt(c, rt))
		return !is_flt(c, lt) || (is_flt(c, rt) && KIND(c, rt) > KIND(c, lt)) ? rt : lt;
	if (op == T_LESSLESS || op == T_GREATERGREATER)
		return pl;
	if (wk_size(c, rt) > wk_size(c, pl) || (wk_size(c, rt) == wk_size(c, pl) && !wk_signed(c, rt)))
		return rt;
	return pl;
}

/* x = y, x op= y: the value stored */
static val wk_assign(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	qty lt = N(x->a)->ty;
	lval l;
	val v;

	wk_scalar(e, x->a, lt);
	if (wk_mem(e, lt) && !x->aux) {
		/* the bytes of the right side's object, then the left's address as the value */
		val s = wk_gen(e, x->b);

		l = wk_lval(e, x->a);
		return wk_putmem(e, &l, lt, s);
	}
	if (!x->aux) {
		v = wk_gen(e, x->b);
		l = wk_lval(e, x->a);
	} else {
		qty rt = N(x->b)->ty;
		val a, b;

		/* as clang: the right side first, then the left's address and value */
		b = wk_gen(e, x->b);
		if (b.k == V_REG && LO(nfree)(e) < 3) {
			int32_t sl = wk_slot(e, 8, 8);
			lval t = { .k = L_FRAME, .off = sl, .align = 8 };

			LO(store)(e, b.r, &t, 8);
			LO(tfree)(e, b.r);
			l = wk_lval(e, x->a);
			a = wk_reload(e, &l, lt);
			b.r = (uint8_t)wk_treg(e, rt);
			LO(load)(e, b.r, &t, 8, false);
		} else {
			l = wk_lval(e, x->a);
			a = wk_reload(e, &l, lt);
		}
		if (is_ptr(c, lt)) {
			v = wk_ptradd(e, a, b, rt, (int64_t)tsize(c, T(c, lt)->base) ?: 1, x->aux == T_MINUS);
		} else {
			qty ct = wk_optype(c, x->aux, lt, rt);

			a = wk_conv(e, a, lt, ct);
			if (x->aux != T_LESSLESS && x->aux != T_GREATERGREATER)
				b = wk_conv(e, b, rt, ct);	/* (a shift count stays as it is) */
			v = wk_conv(e, wk_arith(e, x->aux, ct, a, b), ct, lt);
		}
	}
	if (wk_mem(e, lt))
		return wk_putmem(e, &l, lt, v);
	{
		int r = wk_reg(e, &v, lt);

		wk_put(e, &l, lt, r);
		if (l.bf) {
			/* the value is the field's (truncated to its width) */
			LO(tfree)(e, r);
			v = wk_bf_load(e, &l, lt);
			wk_lfree(e, &l);
			return v;
		}
		wk_lfree(e, &l);
		return wk_vreg(r);
	}
}

/* ++x, x++, --x, x-- */
static val wk_incdec(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	qty t = N(x->a)->ty;
	bool post = x->op == N_POSTINC || x->op == N_POSTDEC, dec = x->op == N_PREDEC || x->op == N_POSTDEC;
	int64_t d = is_ptr(c, t) ? (int64_t)tsize(c, T(c, t)->base) : 1;
	qty ot = is_ptr(c, t) ? prim(TY_ULONG) : t;
	lval l;
	val old = { 0 }, nv;
	int r;

	wk_scalar(e, x->a, t);
	if (KIND(c, t) == TY_BOOL)
		wk_unsup(e, n, "++/-- on _Bool");
	l = wk_lval(e, x->a);
	if (post && wk_mem(e, t)) {
		/* the old value of an object held by address: a copy */
		lval cp = { .k = L_FRAME, .off = wk_slot(e, tsize(c, t), talign(c, t)), .align = talign(c, t) };

		old = wk_putmem(e, &cp, t, wk_reload(e, &l, t));
	} else if (post) {
		old = wk_reload(e, &l, t);
	}
	nv = wk_arith(e, dec ? T_MINUS : T_PLUS, ot, wk_reload(e, &l, t),
		      is_flt(c, t) ? wk_conv(e, vconst(1), prim(TY_INT), t) : vconst(d ? d : 1));
	if (wk_mem(e, t)) {
		nv = wk_putmem(e, &l, t, nv);
		if (post) {
			wk_free(e, &nv);
			return old;
		}
		return nv;
	}
	r = wk_reg(e, &nv, t);
	wk_put(e, &l, t, r);
	if (l.bf && !post) {
		LO(tfree)(e, r);
		nv = wk_bf_load(e, &l, t);
		wk_lfree(e, &l);
		return nv;
	}
	wk_lfree(e, &l);
	if (post) {
		LO(tfree)(e, r);
		return old;
	}
	return wk_vreg(r);
}

static val wk_unary(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	val a;

	if (x->aux == T_EXCLAIM)
		return wk_condval(e, n);
	wk_scalar(e, n, x->ty);
	a = wk_gen(e, x->a);
	if (x->aux == T_PLUS)
		return a;
	if (is_flt(c, x->ty))
		return LO(fneg)(e, x->ty, a);
	if (wk_wide(c, x->ty))
		return LO(wun)(e, x->aux, a);
	if (a.k == V_CONST)
		return wk_const(c, x->aux == T_MINUS ? -a.i : ~a.i, x->ty);
	{
		val v = LO(un)(e, x->aux, opw(c, x->ty), a);

		if (wk_size(c, x->ty) < 4)
			LO(ext)(e, v.r, wk_size(c, x->ty), wk_signed(c, x->ty));
		return v;
	}
}

/* ---------------- calls ---------------- */

/* v (of type t) into the argument a's slot: a scalar extended, a record's bytes */
static void wk_argslot(ecx *e, wkarg *a, qty t, val v)
{
	cc *c = e->c;
	lval l;

	a->slot = wk_slot(e, a->size > 8 ? (a->size + 7) & ~(uint64_t)7 : 8, a->align);
	l = (lval){ .k = L_FRAME, .off = a->slot, .align = 8 };
	if (wk_mem(e, t)) {
		int rs = wk_reg(e, &v, prim(TY_ULONG)), rd = LO(laddr)(e, &l);

		if (a->size)
			LO(copy)(e, rd, rs, a->size);
		LO(tfree)(e, rs);
		LO(tfree)(e, rd);
		return;
	}
	{
		int r = wk_reg(e, &v, t);

		if (wk_size(c, t) < 4 && !is_flt(c, t))
			LO(ext)(e, r, wk_size(c, t), wk_signed(c, t));
		LO(store)(e, r, &l, 8);
		LO(tfree)(e, r);
	}
}

static val wk_call(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	const node *fx = N(x->a);
	qty pt = fx->ty, ft = is_ptr(c, pt) ? T(c, pt)->base : pt;
	const ctype *f = T(c, ft);
	uint32_t ref = 0;
	val fn = { 0 };
	wkarg args[64], r = { 0 };
	bool var = (f->flags & (TF_VARIADIC | TF_NOPROTO)) != 0, rec_ = wk_mem(e, f->base);

	if (x->c > 64)
		wk_unsup(e, n, "a call with more than 64 arguments");
	if (KIND(c, f->base) != TY_VOID)
		wk_scalar(e, n, f->base);
	if (fx->op == N_CAST && fx->aux == CK_FDECAY && N(fx->a)->op == N_VAR) {
		uint32_t s = canon_sym(c, N(fx->a)->a);

		if (c->syms[s].kind == S_BUILTIN) {
			ref = REF(RK_NAME, (uint32_t)c->syms[s].val);	/* a library function by name */
		} else {
			note_ref(e, REF(RK_SYM, s));
			ref = REF(RK_SYM, s);
		}
	}
	for (uint32_t i = 0; i < x->c; i++) {
		uint32_t an = c->lists[x->b + i];
		qty at = N(an)->ty;

		wk_scalar(e, an, at);
		LO(classify)(e, at, &args[i]);
		wk_argslot(e, &args[i], at, wk_gen(e, an));
	}
	if (rec_) {
		LO(classify)(e, f->base, &r);
		r.slot = wk_slot(e, (r.size + 7) & ~(uint64_t)7 ?: 8, r.align);
	}
	if (!ref) {
		fn = wk_gen(e, x->a);
		wk_reg(e, &fn, prim(TY_ULONG));
	}
	e->calls = true;
	{
		val v = LO(call)(e, ref, ref ? NULL : &fn, args, (int)x->c, var, rec_ ? &r : NULL, f->base);
		lval l = { .k = L_FRAME, .off = r.slot, .align = 8 };

		if (!rec_)
			return v;
		return wk_vreg(LO(laddr)(e, &l));	/* a record result: its object's address */
	}
}

/* ({ ...; expr; }): the statements, then the last one's value */
static val wk_stmtexpr(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *b = N(N(n)->a);

	for (uint32_t i = 0; i + 1 < b->c; i++)
		wk_stmt(e, c->lists[b->b + i]);
	if (b->c) {
		uint32_t last = c->lists[b->b + b->c - 1];

		if (N(last)->op == N_EXPR && N(last)->a)
			return wk_gen(e, N(last)->a);
		wk_stmt(e, last);
	}
	return vconst(0);
}

/* ---------------- builtins ---------------- */

/* a va_list operand's address in a temporary (x86-64's is an array: it decays) */
static int wk_valist(ecx *e, uint32_t n)
{
	cc *c = e->c;

	if (is_ptr(c, N(n)->ty)) {
		val v = wk_gen(e, n);

		return wk_reg(e, &v, prim(TY_ULONG));
	}
	{
		lval l = wk_lval(e, n);

		return LO(laddr)(e, &l);
	}
}

static val wk_builtin(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	const uint32_t *arg = &c->lists[x->b];
	char b[64];

	switch (x->aux) {
	case BI_VA_START: {
		int r = wk_valist(e, arg[0]);

		LO(va_start)(e, r);
		LO(tfree)(e, r);
		return vconst(0);
	}
	case BI_VA_END: {
		val v = wk_gen(e, arg[0]);

		wk_free(e, &v);
		return vconst(0);
	}
	case BI_VA_COPY: {
		int rd = wk_valist(e, arg[0]), rs = wk_valist(e, arg[1]);

		LO(copy)(e, rd, rs, tsize(c, T(c, c->t_valist)->kind == TY_ARRAY ? T(c, c->t_valist)->base : c->t_valist));
		LO(tfree)(e, rd);
		LO(tfree)(e, rs);
		return vconst(0);
	}
	default:
		break;
	}

	snprintf(b, sizeof b, "builtin %d", N(n)->aux);
	efatal(e, N(n)->loc, "x86-64: %s is not supported yet (PLAN-x86.md X3)", b);
}

/* ---------------- expressions ---------------- */

VPRIV val wk_gen(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);

	if (unlikely(vstack_low()))
		efatal(e, x->loc, "expression nested too deeply");
	switch (x->op) {
	case N_NUM:
		if (wk_mem(e, x->ty))
			return LO(wconst)(e, c->ipool[x->a]);	/* __int128 */
		return wk_const(c, c->ipool[x->a], x->ty);
	case N_VAR: {
		const sym *y = &c->syms[x->a];

		if (y->kind == S_ENUMCONST)
			return vconst(y->val);
		if (y->kind == S_FUNC) {
			uint32_t s = canon_sym(c, x->a);

			note_ref(e, REF(RK_SYM, s));
			return (val){ .k = V_ADDR, .ref = REF(RK_SYM, s) };
		}
		wk_scalar(e, n, x->ty);
		return wk_load(e, wk_lval(e, n), x->ty);
	}
	case N_COMPLIT: {
		lval l = wk_lval(e, n);

		return wk_load(e, l, x->ty);
	}
	case N_STR: case N_MEMBER: case N_INDEX: case N_DEREF:
		if (!is_array(c, x->ty) && !is_func(c, x->ty))
			wk_scalar(e, n, x->ty);
		return wk_load(e, wk_lval(e, n), x->ty);
	case N_ADDR: {
		lval l;

		if (N(x->a)->op == N_VAR && c->syms[N(x->a)->a].kind == S_FUNC) {
			uint32_t s = canon_sym(c, N(x->a)->a);

			note_ref(e, REF(RK_SYM, s));
			return (val){ .k = V_ADDR, .ref = REF(RK_SYM, s) };
		}
		l = wk_lval(e, x->a);
		if (l.k == L_SYM)
			return (val){ .k = V_ADDR, .ref = l.ref, .i = l.off };
		return wk_vreg(LO(laddr)(e, &l));
	}
	case N_CAST:
		return wk_cast(e, n);
	case N_UNARY:
		return wk_unary(e, n);
	case N_BINARY:
		return wk_binary(e, n);
	case N_LOGAND: case N_LOGOR:
		return wk_condval(e, n);
	case N_ASSIGN:
		return wk_assign(e, n);
	case N_PREINC: case N_PREDEC: case N_POSTINC: case N_POSTDEC:
		return wk_incdec(e, n);
	case N_COND:
		return wk_ternary(e, n);
	case N_COMMA: {
		val v = wk_gen(e, x->a);

		wk_free(e, &v);
		return wk_gen(e, x->b);
	}
	case N_CALL:
		return wk_call(e, n);
	case N_STMTEXPR:
		return wk_stmtexpr(e, n);
	case N_BUILTIN:
		return wk_builtin(e, n);
	case N_VAARG: {
		int ra = wk_valist(e, x->a), rp;
		lval l;

		wk_scalar(e, n, x->ty);
		rp = LO(va_arg)(e, ra, x->ty);
		LO(tfree)(e, ra);
		l = (lval){ .k = L_REG, .r = (uint8_t)rp, .align = 8 };
		return wk_load(e, l, x->ty);
	}
	case N_FNUM:
		wk_scalar(e, n, x->ty);
		if (wk_mem(e, x->ty)) {
			/* a long double constant: its bytes stored to a new object */
			u128 q;

			/* a 16-byte long double constant keeps its bits after its double in the pool */
			memcpy(&q, &c->fpool[x->a + 1], 16);
			uint8_t b[10];
			uint64_t lo, hi;
			lval l = { .k = L_FRAME, .off = wk_slot(e, 16, 16), .align = 16 }, h;
			val v;
			int r;

			q_to_x87(q, b);
			memcpy(&lo, b, 8);
			hi = (uint64_t)b[8] | (uint64_t)b[9] << 8;
			v = vconst((i128)(int64_t)lo);
			r = LO(toreg)(e, &v, 8);
			LO(store)(e, r, &l, 8);
			LO(tfree)(e, r);
			v = vconst((i128)hi);
			r = LO(toreg)(e, &v, 8);
			h = l;
			h.off += 8;
			LO(store)(e, r, &h, 8);
			LO(tfree)(e, r);
			return wk_vreg(LO(laddr)(e, &l));
		}
		{
			double d = c->fpool[x->a];
			float f = (float)d;
			uint64_t bits = 0;

			if (KIND(c, x->ty) == TY_FLOAT)
				memcpy(&bits, &f, 4);
			else
				memcpy(&bits, &d, 8);
			return LO(fconst)(e, bits, KIND(c, x->ty) == TY_FLOAT ? 4 : 8);
		}
	default: {
		static const char *const nm[N_NKINDS] = { [N_SIZEOF_VLA] = "sizeof of a VLA", [N_COMPLIT] = "a compound literal",
			[N_INIT] = "an initializer list", [N_LABELADDR] = "a label address", [N_VAARG] = "va_arg",
			[N_CPLX] = "a complex value", [N_MEMBER] = "a member" };

		wk_unsup(e, n, nm[x->op] ? nm[x->op] : "this expression");
	}
	}
}

/* ---------------- conditions: branches, not values ---------------- */

/* to lbl if (n != 0) == jump_if */
VPRIV void wk_cond(ecx *e, uint32_t n, bool jump_if, uint32_t lbl)
{
	cc *c = e->c;
	const node *x = N(n);

	switch (x->op) {
	case N_LOGAND: case N_LOGOR: {
		bool and = x->op == N_LOGAND;

		if (and != jump_if) {
			/* a && b falls through when false: both must hold (or one hold for ||) */
			wk_cond(e, x->a, jump_if, lbl);
			wk_cond(e, x->b, jump_if, lbl);
		} else {
			uint32_t skip = wk_newlabel(e);

			wk_cond(e, x->a, !jump_if, skip);
			wk_cond(e, x->b, jump_if, lbl);
			LO(label)(e, skip);
		}
		return;
	}
	case N_UNARY:
		if (x->aux == T_EXCLAIM) {
			wk_cond(e, x->a, !jump_if, lbl);
			return;
		}
		break;
	case N_BINARY:
		if (wk_is_cmp(x->aux) && !is_flt(c, N(x->a)->ty) && !wk_mem(e, N(x->a)->ty)) {
			qty lt = N(x->a)->ty;
			int op = x->aux;
			val a, b;

			wk_scalar(e, x->a, lt);
			a = wk_gen(e, x->a);
			b = wk_rhs(e, &a, x->b, lt);
			if (a.k == V_CONST && b.k == V_CONST) {
				if (wk_fold_cmp(op, wk_signed(c, lt), em_trunc_to(c, a.i, lt), em_trunc_to(c, b.i, lt)) == jump_if)
					LO(jump)(e, lbl);
				return;
			}
			if (!jump_if)
				op = op == T_LESS ? T_GREATEREQ : op == T_LESSEQ ? T_GREATER : op == T_GREATER ? T_LESSEQ :
				     op == T_GREATEREQ ? T_LESS : op == T_EQUALEQUAL ? T_EXCLAIMEQ : T_EQUALEQUAL;
			LO(cmpbr)(e, op, is_ptr(c, lt) ? 8 : opw(c, lt), wk_signed(c, lt), a, b, lbl);
			return;
		}
		break;
	case N_CAST:
		if ((x->aux == CK_BOOL || x->aux == CK_NOP || x->aux == CK_INT) && !is_flt(c, N(x->a)->ty) &&
		    wk_size(c, x->ty) >= wk_size(c, N(x->a)->ty)) {
			wk_cond(e, x->a, jump_if, lbl);
			return;
		}
		break;
	case N_COMMA: {
		val v = wk_gen(e, x->a);

		wk_free(e, &v);
		wk_cond(e, x->b, jump_if, lbl);
		return;
	}
	default:
		break;
	}
	wk_scalar(e, n, x->ty);
	{
		val v = wk_gen(e, n);

		if (is_flt(c, x->ty) || wk_wide(c, x->ty)) {
			v = wk_conv(e, v, x->ty, prim(TY_BOOL));	/* != 0.0 (NaN is true) */
			LO(brz)(e, v, 4, jump_if, lbl);
			return;
		}
		if (v.k == V_CONST) {
			if ((v.i != 0) == jump_if)
				LO(jump)(e, lbl);
			return;
		}
		if (v.k == V_ADDR && !wk_weak0(c, v)) {
			if (jump_if)
				LO(jump)(e, lbl);
			return;
		}
		LO(brz)(e, v, is_ptr(c, x->ty) ? 8 : opw(c, x->ty), jump_if, lbl);
	}
}
