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

	if (k == TY_FLOAT16 || ((k == TY_INT128 || k == TY_UINT128) && !LO(i128mem)()))
		wk_unsup(e, n, k == TY_FLOAT16 ? "_Float16" : "__int128");
}

static bool wk_wide(cc *c, qty t) { return KIND(c, t) == TY_INT128 || KIND(c, t) == TY_UINT128; }

VPRIV bool wk_mem(ecx *e, qty t)
{
	cc *c = e->c;

	return is_record(c, t) || is_array(c, t) || is_vec(c, t) || KIND(c, t) == TY_FLOAT128 || (KIND(c, t) == TY_LDOUBLE && LO(ldmem)()) || (wk_wide(c, t) && LO(i128mem)());
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

static val wk_ptradd_vla(ecx *e, val p, val i, qty it, qty pt, bool sub);
static val wk_atomic_rmw(ecx *e, uint32_t n);
static val wk_incval(ecx *e, qty t, bool dec, val a);

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
		if (T(c, c->syms[s].ty)->flags & TF_VLA)
			W->soff[s] = wk_slot(e, 24, 8);	/* { address, sp before it, bytes } */
		else
			W->soff[s] = wk_slot(e, tsize(c, c->syms[s].ty) ? tsize(c, c->syms[s].ty) : 1, sym_align(c, s));
	}
	return W->soff[s];
}

/* n is a[K] of a local array that does not escape, an integer element: a and K */
static bool wk_ael_of(ecx *e, uint32_t n, uint32_t *s, int64_t *k)
{
	cc *c = e->c;
	const node *x = N(n);
	uint32_t a;
	const ctype *at;

	if (!is_aconst(c, n) || !is_integer(c, x->ty) || wk_wide(c, x->ty) || (QQ(x->ty) & Q_VOLATILE))
		return false;
	a = N(N(x->a)->a)->a, at = T(c, c->syms[a].ty);
	if (!is_local_var(c, a) || c->syms[a].sc == SC_STATIC || (e->E->sflags[a] & SA_AESC) ||
	    (QQ(c->syms[a].ty) & Q_VOLATILE) || sym_attrs(c, a)->cleanup || at->kind != TY_ARRAY ||
	    (at->flags & (TF_VLA | TF_INCOMPLETE)) || aconst_idx(c, n) >= at->n)
		return false;
	*s = a, *k = aconst_idx(c, n);
	return true;
}

static int wk_ael_find(ecx *e, uint32_t s, int64_t k)
{
	wkst *W = e->wk;

	for (int i = 0; i < W->nael; i++)
		if (W->ael[i].s == s && W->ael[i].idx == k && W->ael[i].gen == W->gen && W->ael[i].inst == W->inst)
			return i;
	return -1;
}

static void wk_ael_drop(ecx *e, uint32_t s, int64_t k, bool all)
{
	wkst *W = e->wk;

	for (int i = 0; i < W->nael; i++)
		if (W->ael[i].s == s && (all || W->ael[i].idx == k))
			W->ael[i] = W->ael[--W->nael], i--;
}

/* a local array's element values are unknown again (a label, its declaration) */
VPRIV void wk_ael_clear(ecx *e, uint32_t s)
{
	if (s == UINT32_MAX)
		e->wk->nael = 0;
	else
		wk_ael_drop(e, s, 0, true);
}

VPRIV lval wk_lval(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	lval l = { 0 };
	{
		uint32_t as;
		int64_t ak;

		if (wk_ael_of(e, n, &as, &ak))
			wk_ael_drop(e, as, ak, false);	/* (written, or its address used) */
	}

	switch (x->op) {
	case N_VAR: {
		uint32_t s = x->a, cs;

		if (is_local_var(c, s) && (T(c, c->syms[s].ty)->flags & TF_VLA)) {
			lval a = { .k = L_FRAME, .off = wk_local(e, s), .align = 8 };
			int r = LO(treg)(e);

			LO(load)(e, r, &a, 8, false);
			return wk_fromval(e, wk_vreg(r), c->syms[s].ty);
		}
		if (is_local_var(c, s)) {
			l.k = L_FRAME;
			l.off = wk_local(e, s);
			l.align = sym_align(c, s);
			return l;
		}
		cs = canon_sym(c, s);
		note_ref(e, REF(RK_SYM, cs));
		if ((c->syms[cs].flags | c->syms[s].flags) & SF_THREAD) {
			/* as clang: tls_model(), else -ftls-model=, if stronger than the default
			   (local-exec for a symbol defined here, else initial-exec) */
			const attrs *ta = sym_attrs(c, cs);
			int want = ta && ta->tlsmodel ? ta->tlsmodel : c->o->tlsmodel;
			int def = (c->syms[cs].flags & (SF_DEFINED | SF_TENTATIVE)) ? VCC_TLS_LE : VCC_TLS_IE;

			if (c->o->pic == 2)
				def = VCC_TLS_GD;
			if (want > def)
				def = want;
			if (def < VCC_TLS_IE)
				e->calls = true;	/* (local-dynamic as general dynamic) */
			return wk_fromval(e, wk_vreg(LO(tlsaddr)(e, REF(RK_SYM, cs), def == VCC_TLS_LE ? 0 : def == VCC_TLS_IE ? 1 : 2)),
					  c->syms[s].ty);
		}
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
			return wk_fromval(e, wk_ptradd_vla(e, p, i, N(x->b)->ty, pt, false), et);
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
   for one that crosses it (packed); its offset and the field's bit in it.
   false: no aligned unit of up to 8 bytes holds it (it crosses an 8-byte boundary) */
static bool wk_bf_unit(ecx *e, int64_t bit, int width, qty t, int64_t *uoff, int *us, int *pos)
{
	int s = wk_size(e->c, t);

	for (s = s < 1 ? 1 : s > 8 ? 8 : s; s <= 8; s *= 2) {
		int64_t ub = wk_floordiv(bit, s * 8);
		int p = (int)(bit - ub * s * 8);

		if (p + width <= s * 8) {
			*uoff = ub * s, *us = s, *pos = p;
			return true;
		}
	}
	return false;
}

/* the smallest aligned unit holding bits [bit, bit + width) (they do not cross 8 bytes) */
static void wk_bf_unit1(ecx *e, int64_t bit, int width, int64_t *uoff, int *us, int *pos)
{
	if (!wk_bf_unit(e, bit, width, prim(TY_CHAR), uoff, us, pos))
		efatal(e, e->curloc, "internal error: a bit-field part across 8 bytes");
}

/* width bits at bit pos of the unit (us bytes at uo from l's base), zero-extended, in a new register */
static val wk_bfu_load(ecx *e, const lval *l, int64_t uo, int us, int pos, int w)
{
	lval u = *l;
	val v;

	u.bf = 0, u.off = uo;
	v = (val){ .k = V_REG, .r = (uint8_t)LO(treg)(e) };
	LO(load)(e, v.r, &u, us, false);
	if (pos)
		v = LO(bin)(e, T_GREATERGREATER, 8, false, v, vconst(pos));
	if (w < 64)
		v = LO(bin)(e, T_AMP, 8, false, v, vconst((i128)(((uint64_t)1 << w) - 1)));
	return v;
}

/* the low w bits of rv into the unit (rv stays) */
static void wk_bfu_store(ecx *e, const lval *l, int64_t uo, int us, int pos, int w, int rv)
{
	lval u = *l;
	uint64_t m = (w >= 64 ? ~(uint64_t)0 : (((uint64_t)1 << w) - 1)) << pos;
	val old, nv;

	u.bf = 0, u.off = uo;
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

/* a field crossing an 8-byte boundary: split there, the low part ending at it (wa bits) */
static int64_t wk_bf_split(ecx *e, const lval *l, int *wa)
{
	int64_t bit = l->off * 8 + l->bitoff, b = (wk_floordiv(bit, 64) + 1) * 64;

	if (l->width > 64)
		efatal(e, e->curloc, "x86-64: a bit-field wider than 64 bits is not supported yet (PLAN-x86.md X3)");
	*wa = (int)(b - bit);
	return b;
}

/* a bit-field's value in a new register (l stays) */
static val wk_bf_load(ecx *e, const lval *l, qty t)
{
	int us, pos, w = l->width;
	int64_t uo, bit = l->off * 8 + l->bitoff;
	val v;

	if (wk_bf_unit(e, bit, w, t, &uo, &us, &pos)) {
		v = wk_bfu_load(e, l, uo, us, pos, w);
	} else {
		int wa, ub, pb;
		int64_t b = wk_bf_split(e, l, &wa), ob;
		val hi;

		wk_bf_unit1(e, bit, wa, &uo, &us, &pos);
		v = wk_bfu_load(e, l, uo, us, pos, wa);
		wk_bf_unit1(e, b, w - wa, &ob, &ub, &pb);
		hi = wk_bfu_load(e, l, ob, ub, pb, w - wa);
		hi = LO(bin)(e, T_LESSLESS, 8, false, hi, vconst(wa));
		v = LO(bin)(e, T_PIPE, 8, false, v, hi);
	}
	if (l->sgn && w < 64) {
		v = LO(bin)(e, T_LESSLESS, 8, false, v, vconst(64 - w));
		v = LO(bin)(e, T_GREATERGREATER, 8, true, v, vconst(64 - w));
	}
	return v;
}

/* the low bits of rv into the bit-field (rv stays) */
static void wk_bf_store(ecx *e, const lval *l, qty t, int rv)
{
	int us, pos, w = l->width;
	int64_t uo, bit = l->off * 8 + l->bitoff;

	if (wk_bf_unit(e, bit, w, t, &uo, &us, &pos)) {
		wk_bfu_store(e, l, uo, us, pos, w, rv);
	} else {
		int wa;
		int64_t b = wk_bf_split(e, l, &wa);
		val hi = (val){ .k = V_REG, .r = (uint8_t)LO(treg)(e) };

		wk_bf_unit1(e, bit, wa, &uo, &us, &pos);
		wk_bfu_store(e, l, uo, us, pos, wa, rv);
		LO(mov)(e, hi.r, rv);
		hi = LO(bin)(e, T_GREATERGREATER, 8, false, hi, vconst(wa));
		wk_bf_unit1(e, b, w - wa, &uo, &us, &pos);
		wk_bfu_store(e, l, uo, us, pos, w - wa, hi.r);
		LO(tfree)(e, hi.r);
	}
}

/* r stored to l as type t (a bit-field or not); r stays */
VPRIV void wk_put(ecx *e, const lval *l, qty t, int r)
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
VPRIV val wk_conv(ecx *e, val v, qty from, qty to)
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
	case CK_VADDR:
		return wk_gen(e, x->a);	/* a vector value is its address */
	case CK_VBITS: {
		/* a vector's bits as a scalar of its size */
		val v = wk_gen(e, x->a);

		return wk_load(e, wk_fromval(e, v, x->ty), x->ty);
	}
	case CK_SPLAT: {
		/* the scalar into every lane of a new vector */
		qty et = T(c, x->ty)->base;
		uint64_t es = tsize(c, et), nl = tsize(c, x->ty) / es;
		lval d = { .k = L_FRAME, .off = wk_slot(e, tsize(c, x->ty), 16), .align = talign(c, et) };
		val v = wk_gen(e, x->a);
		int r = wk_reg(e, &v, et);

		for (uint64_t i = 0; i < nl; i++, d.off += (int64_t)es)
			LO(store)(e, r, &d, (int)es);
		LO(tfree)(e, r);
		d.off -= (int64_t)(nl * es);
		return wk_vreg(LO(laddr)(e, &d));
	}
	case CK_UNION: {
		/* the operand's bytes at the start of a new object (zeroed when larger) */
		lval d = { .k = L_FRAME, .off = wk_slot(e, tsize(c, x->ty), talign(c, x->ty) > 16 ? 16 : talign(c, x->ty)), .align = 8 };
		val v;

		if (tsize(c, x->ty) > tsize(c, from)) {
			int rz = LO(laddr)(e, &d);

			LO(zero)(e, rz, tsize(c, x->ty));
			LO(tfree)(e, rz);
		}
		v = wk_gen(e, x->a);
		if (wk_mem(e, from)) {
			v = wk_putmem(e, &d, from, v);
			wk_free(e, &v);
		} else {
			int r = wk_reg(e, &v, from);

			LO(store)(e, r, &d, wk_size(c, from));
			LO(tfree)(e, r);
		}
		return wk_vreg(LO(laddr)(e, &d));
	}
	case CK_NOP: case CK_INT: case CK_BOOL: case CK_P2I: case CK_I2P: case CK_P2P: case CK_I2F: case CK_F2I: case CK_F2F:
		if (x->aux == CK_NOP && wk_mem(e, x->ty) && wk_mem(e, from))
			return wk_gen(e, x->a);	/* one representation (vectors of one size, records) */
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
	/* 0 << x, 0 >> x, -1 >> x (signed): the constant, whatever x is (as arm64) */
	if ((op == T_LESSLESS || op == T_GREATERGREATER) && a.k == V_CONST && b.k != V_CONST &&
	    (em_trunc_to(c, a.i, t) == 0 || (op == T_GREATERGREATER && wk_signed(c, t) && em_trunc_to(c, a.i, t) == -1))) {
		wk_free(e, &b);
		return vconst(em_trunc_to(c, a.i, t));
	}
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

/* ---------------- variable length arrays ----------------
 * A VLA's slot holds { address, sp before the allocation, bytes }; the
 * array is below sp as alloca's memory, and its block's end restores sp.
 * Each size expression is evaluated once, at its declaration, into a slot
 * (C11 6.7.6.2p5).
 */
static val wk_vla_len(ecx *e, uint32_t sn)
{
	wkst *W = e->wk;
	cc *c = e->c;

	if (W->vgen[sn] == W->gen) {
		lval l = { .k = L_FRAME, .off = W->voff[sn], .align = 8 };
		int r = LO(treg)(e);

		LO(load)(e, r, &l, 8, false);
		return wk_vreg(r);
	}
	return wk_conv(e, wk_gen(e, sn), N(sn)->ty, c->t_size);
}

VPRIV void wk_vla_fix(ecx *e, qty t, bool pure)
{
	wkst *W = e->wk;
	cc *c = e->c;

	for (;;) {
		const ctype *a = T(c, t);

		if (a->kind == TY_PTR) {
			t = a->base;
			continue;
		}
		if (a->kind != TY_ARRAY)
			return;
		if ((a->flags & TF_VLA) && a->n && N(a->n)->op != N_NUM && !(pure && side_effects(c, a->n))) {
			val v = wk_conv(e, wk_gen(e, a->n), N(a->n)->ty, c->t_size);
			int r = wk_reg(e, &v, c->t_size);
			lval l = { .k = L_FRAME, .off = wk_slot(e, 8, 8), .align = 8 };

			LO(store)(e, r, &l, 8);
			LO(tfree)(e, r);
			W->vgen[a->n] = W->gen;
			W->voff[a->n] = (int32_t)l.off;
		}
		t = a->base;
	}
}

VPRIV val wk_vla_bytes(ecx *e, qty t)
{
	cc *c = e->c;
	const ctype *a = T(c, t);
	val n, b;

	if (a->kind != TY_ARRAY || !(a->flags & TF_VLA))
		return vconst(tsize(c, t));
	n = wk_vla_len(e, a->n);
	b = wk_vla_bytes(e, a->base);
	return wk_arith(e, T_STAR, prim(TY_ULONG), n, b);
}

/* p +- i elements of a pointer to a VLA */
static val wk_ptradd_vla(ecx *e, val p, val i, qty it, qty pt, bool sub)
{
	cc *c = e->c;
	int ri = wk_reg(e, &i, it);

	if (wk_size(c, it) < 8)
		LO(ext)(e, ri, wk_size(c, it), wk_signed(c, it));
	i = wk_arith(e, T_STAR, prim(TY_ULONG), wk_vreg(ri), wk_vla_bytes(e, T(c, pt)->base));
	return wk_arith(e, sub ? T_MINUS : T_PLUS, prim(TY_ULONG), p, i);
}

VPRIV void wk_vla_decl(ecx *e, uint32_t s, bool zero)
{
	cc *c = e->c;
	int32_t off = wk_local(e, s);
	lval la = { .k = L_FRAME, .off = off, .align = 8 }, lsp = la, ln = la;
	val n = wk_vla_bytes(e, c->syms[s].ty), p;
	int r;
	uint32_t al = sym_align(c, s) > talign(c, c->syms[s].ty) ? sym_align(c, s) : talign(c, c->syms[s].ty);

	lsp.off += 8, ln.off += 16;
	r = wk_reg(e, &n, prim(TY_ULONG));
	LO(store)(e, r, &ln, 8);
	if (e->wk->zgen[s] == e->wk->gen) {
		/* reached again (a goto back): the previous instance freed first */
		uint32_t fresh = wk_newlabel(e);
		int t = LO(treg)(e);

		LO(load)(e, t, &lsp, 8, false);
		LO(brz)(e, wk_vreg(t), 8, false, fresh);
		t = LO(treg)(e);
		LO(load)(e, t, &lsp, 8, false);
		LO(spset)(e, t);
		LO(label)(e, fresh);
	}
	{
		int t = LO(spget)(e);

		LO(store)(e, t, &lsp, 8);
		LO(tfree)(e, t);
	}
	if (al > 16)	/* over-allocated, the address rounded up */
		r = LO(bin)(e, T_PLUS, 8, false, wk_vreg(r), vconst((i128)(al - 16))).r;
	p = LO(alloca)(e, wk_vreg(r));
	if (al > 16) {
		p = LO(bin)(e, T_PLUS, 8, false, p, vconst((i128)(al - 1)));
		p = LO(bin)(e, T_AMP, 8, false, p, vconst(-(i128)al));
	}
	LO(store)(e, p.r, &la, 8);
	if (zero) {
		/* = {}: the bytes rounded up to 8 (still inside the 16-aligned allocation) */
		uint32_t top = wk_newlabel(e), done = wk_newlabel(e);
		int k = LO(treg)(e), z;
		val zv = vconst(0);

		LO(load)(e, k, &ln, 8, false);
		k = LO(bin)(e, T_PLUS, 8, false, wk_vreg(k), vconst(7)).r;
		k = LO(bin)(e, T_AMP, 8, false, wk_vreg(k), vconst(-8)).r;
		LO(label)(e, top);
		LO(brz)(e, wk_vreg(k), 8, false, done);
		LO(tkeep)(e, k);
		z = LO(toreg)(e, &zv, 8);
		{
			lval d = { .k = L_REG, .r = (uint8_t)p.r, .align = 8 };

			LO(store)(e, z, &d, 8);
		}
		LO(tfree)(e, z);
		p = LO(bin)(e, T_PLUS, 8, false, p, vconst(8));
		k = LO(bin)(e, T_MINUS, 8, false, wk_vreg(k), vconst(8)).r;
		LO(jump)(e, top);
		LO(label)(e, done);
		LO(tfree)(e, k);
	}
	LO(tfree)(e, p.r);
}

/* few temporaries of the kind a value of type t is held in are left: it waits in a slot */
static bool wk_short(ecx *e, qty t) { return wk_freg(e, t) ? LO(nffree)(e) < 3 : LO(nfree)(e) < 3; }

/* the right operand next: if temporaries are short, the left waits in a slot */
static val wk_rhs(ecx *e, val *a, uint32_t rn, qty at)
{
	if (a->k == V_REG && wk_short(e, at)) {
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

/* a comparison of scalars of type t: 0 or 1 */
static val wk_cmpv(ecx *e, int op, qty t, val a, val b)
{
	cc *c = e->c;

	if (is_flt(c, t))
		return LO(fcmp)(e, op, t, a, b);
	if (wk_wide(c, t))
		return LO(wcmp)(e, op, wk_signed(c, t), a, b);
	if (a.k == V_CONST && b.k == V_CONST)
		return vconst(wk_fold_cmp(op, wk_signed(c, t), em_trunc_to(c, a.i, t), em_trunc_to(c, b.i, t)));
	return LO(cmp)(e, op, is_ptr(c, t) ? 8 : opw(c, t), wk_signed(c, t), a, b);
}

/* lane i of the vector at r (type t), or (t a scalar) a copy of the scalar in r */
static val wk_lane(ecx *e, qty t, int r, unsigned i)
{
	cc *c = e->c;

	if (is_vec(c, t)) {
		qty et = T(c, t)->base;
		lval l = { .k = L_REG, .r = (uint8_t)r, .off = (int64_t)i * (int64_t)tsize(c, et), .align = talign(c, et) };

		return wk_reload(e, &l, et);
	}
	{
		int d = wk_treg(e, t);

		LO(mov)(e, d, r);
		return wk_vreg(d);
	}
}

/*
 * A GNU vector operation, lane by lane through memory (vectors are values by
 * address): a op b, one side may be a scalar; unary (b unused) - ~ !;
 * comparisons give -1 / 0 lanes of rt's element type.  The result is a new
 * frame object of type rt.
 */
static val wk_vlanes(ecx *e, int op, qty at, val a, qty bt, val b, qty rt, bool unary)
{
	cc *c = e->c;
	qty vt = is_vec(c, at) ? at : bt, et = T(c, vt)->base, re = T(c, rt)->base;
	uint64_t es = tsize(c, re);
	unsigned nl = (unsigned)(tsize(c, vt) / tsize(c, et));
	int ra, rb = -1;
	lval d = { .k = L_FRAME, .off = wk_slot(e, tsize(c, rt), 16), .align = talign(c, re) };
	/* a constant of a lane type (__int128 and long double lanes are objects, by address) */
#define WK_LK(t, k) (wk_wide(c, t) ? LO(wconst)(e, (k)) : is_flt(c, t) ? wk_conv(e, vconst(k), prim(TY_INT), t) : vconst(k))

	ra = wk_reg(e, &a, is_vec(c, at) || wk_mem(e, at) ? prim(TY_ULONG) : at);
	if (!unary)
		rb = wk_reg(e, &b, is_vec(c, bt) || wk_mem(e, bt) ? prim(TY_ULONG) : bt);
	for (unsigned i = 0; i < nl; i++) {
		val x = wk_lane(e, at, ra, i), r;

		if (unary) {
			if (op == T_EXCLAIM)
				r = wk_arith(e, T_MINUS, re, WK_LK(re, 0), wk_conv(e, wk_cmpv(e, T_EQUALEQUAL, et, x, WK_LK(et, 0)), prim(TY_INT), re));
			else if (op == T_MINUS)
				r = is_flt(c, et) ? LO(fneg)(e, et, x) : wk_arith(e, T_MINUS, et, WK_LK(et, 0), x);
			else
				r = wk_arith(e, T_CARET, et, x, WK_LK(et, -1));
		} else {
			val y = wk_lane(e, bt, rb, i);

			if (wk_is_cmp(op))
				r = wk_arith(e, T_MINUS, re, WK_LK(re, 0), wk_conv(e, wk_cmpv(e, op, et, x, y), prim(TY_INT), re));
			else if ((op == T_LESSLESS || op == T_GREATERGREATER) && wk_wide(c, et))	/* (a 128-bit shift counts in an int) */
				r = wk_arith(e, op, et, x, wk_conv(e, y, is_vec(c, bt) ? T(c, bt)->base : bt, prim(TY_INT)));
			else
				r = wk_arith(e, op, et, x, y);
		}
		{
			lval l = d;

			l.off += (int64_t)(i * es);
			if (wk_mem(e, re)) {
				r = wk_putmem(e, &l, re, r);
				wk_free(e, &r);
			} else {
				int rr = wk_reg(e, &r, re);

				LO(store)(e, rr, &l, (int)es);
				LO(tfree)(e, rr);
			}
		}
	}
#undef WK_LK
	LO(tfree)(e, ra);
	if (rb >= 0)
		LO(tfree)(e, rb);
	return wk_vreg(LO(laddr)(e, &d));
}

static val wk_binary(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	int op = x->aux;
	qty lt = N(x->a)->ty, rt = N(x->b)->ty;
	val a, b;

	if (unlikely(is_vec(c, x->ty) || is_vec(c, lt) || is_vec(c, rt))) {
		a = wk_gen(e, x->a);
		b = wk_rhs(e, &a, x->b, is_vec(c, lt) ? prim(TY_ULONG) : lt);
		return wk_vlanes(e, op, lt, a, rt, b, x->ty, false);
	}
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
	if (is_ptr(c, lt) && is_ptr(c, rt) && op == T_MINUS && ptr_to_vla(c, lt)) {
		val d = wk_arith(e, T_MINUS, prim(TY_LONG), a, b);

		return wk_arith(e, T_SLASH, prim(TY_LONG), d, wk_vla_bytes(e, T(c, lt)->base));
	}
	if (is_ptr(c, lt) && ptr_to_vla(c, lt) && (op == T_PLUS || op == T_MINUS))
		return wk_ptradd_vla(e, a, b, rt, lt, op == T_MINUS);
	if (is_ptr(c, rt) && ptr_to_vla(c, rt) && op == T_PLUS)
		return wk_ptradd_vla(e, b, a, lt, rt, false);
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
		int k = -1;

		/* a condition known here (after inlining, or from its form): one arm only, as
		   clang -O2 (an error() call or a link_error in the other is gone) */
		if (!(e->E->nflags[x->b] & NA_LABEL) && !(e->E->nflags[x->c] & NA_LABEL)) {
			k = cond_fold(e, x->a);
			if (k < 0 && !side_effects(c, x->a) && wk_isconst(e, x->a)) {
				val kv = wk_conv(e, wk_gen(e, x->a), N(x->a)->ty, prim(TY_BOOL));

				k = kv.k == V_CONST ? kv.i != 0 : (wk_free(e, &kv), -1);
			}
		}
		if (k >= 0) {
			v = wk_gen(e, k ? x->b : x->c);
			if (vd)
				return wk_free(e, &v), vconst(0);
			if (v.k == V_CONST && is_integer(c, t))	/* (stays a constant: an asm "i" operand) */
				return vconst(em_trunc_to(c, v.i, t));
			r = wk_reg(e, &v, t);
			LO(store)(e, r, &l, 8);
			LO(tfree)(e, r);
			r = wk_treg(e, t);
			LO(load)(e, r, &l, 8, false);
			return wk_vreg(r);
		}
		wk_state(e)->cjump = false;
		wk_cond(e, x->a, false, lf);
		if (wk_state(e)->cjump) {	/* (false whatever its operands: b is dead) */
			wk_state(e)->cjump = false;
		} else {
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
VPRIV val wk_reload(ecx *e, const lval *l, qty t)
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
VPRIV val wk_putmem(ecx *e, lval *l, qty t, val v)
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
static val wk_assign_(ecx *e, uint32_t n);
static val wk_assign(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	uint32_t as;
	int64_t ak;
	bool ael = !x->aux && wk_ael_of(e, x->a, &as, &ak);
	val v = wk_assign_(e, n);
	wkst *W = e->wk;

	cval cv;

	if (ael && W->nael < 16 && (v.k == V_CONST || (cc_eval(c, x->b, &cv) && cv.kind == CV_INT))) {
		int i = W->nael++;

		W->ael[i].s = as, W->ael[i].gen = W->gen, W->ael[i].inst = W->inst;
		W->ael[i].idx = ak, W->ael[i].v = em_trunc_to(c, v.k == V_CONST ? v.i : cv.i, N(x->a)->ty);
	}
	return v;
}

static val wk_assign_(ecx *e, uint32_t n)
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
	if (x->aux && unlikely(T(c, lt)->flags & TF_ATOMIC))
		return wk_atomic_rmw(e, n);
	if (x->aux && unlikely(is_vec(c, lt))) {
		/* v op= w: the right side first, the lanes into a temporary, its bytes into v */
		qty rt = N(x->b)->ty;
		val b = wk_gen(e, x->b), a;

		l = wk_lval(e, x->a);
		a = wk_reload(e, &l, lt);
		v = wk_vlanes(e, x->aux, lt, a, rt, b, lt, false);
		return wk_putmem(e, &l, lt, v);
	}
	if (!x->aux) {
		v = wk_gen(e, x->b);
		l = wk_lval(e, x->a);
	} else {
		qty rt = N(x->b)->ty;
		val a, b;

		/* as clang: the right side first, then the left's address and value */
		b = wk_gen(e, x->b);
		if (b.k == V_REG && wk_short(e, rt)) {
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
		if (is_ptr(c, lt) && ptr_to_vla(c, lt)) {
			v = wk_ptradd_vla(e, a, b, rt, lt, x->aux == T_MINUS);
		} else if (is_ptr(c, lt)) {
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
		if (unlikely(T(c, lt)->flags & TF_ATOMIC))
			LO(atomic)(e, WK_AFENCE, 8, -1, -1, -1);	/* a sequentially consistent store */
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

/* a ++ or -- of the value a of type t: the new value */
static val wk_incval(ecx *e, qty t, bool dec, val a)
{
	cc *c = e->c;
	int64_t d = is_ptr(c, t) ? (int64_t)tsize(c, T(c, t)->base) : 1;

	if (KIND(c, t) == TY_BOOL) {
		/* ++ gives 1; -- gives a - 1 != 0, which is !a */
		if (!dec) {
			wk_free(e, &a);
			return vconst(1);
		}
		return LO(cmp)(e, T_EQUALEQUAL, 4, false, a, vconst(0));
	}
	if (is_ptr(c, t) && ptr_to_vla(c, t))
		return wk_ptradd_vla(e, a, vconst(1), prim(TY_LONG), t, dec);
	return wk_arith(e, dec ? T_MINUS : T_PLUS, is_ptr(c, t) ? prim(TY_ULONG) : t, a,
			is_flt(c, t) ? wk_conv(e, vconst(1), prim(TY_INT), t) : vconst(d ? d : 1));
}

/*
 * x op= y, ++x, x++ ... on an _Atomic x: one compare-and-swap loop.  The
 * operator's usual code runs on a copy in a frame slot (so it is the same
 * for every type), y evaluated once before the loop:
 *   old = load [p]
 * top: slot = old;  new = slot op y;  slot = new
 *   got = CAS([p], old, slot's bits);  if got != old: old = got, to top
 */
static val wk_atomic_rmw(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	bool asg = x->op == N_ASSIGN, post = x->op == N_POSTINC || x->op == N_POSTDEC;
	bool dec = x->op == N_PREDEC || x->op == N_POSTDEC;
	qty lt = N(x->a)->ty, rt = asg ? N(x->b)->ty : lt;
	int sz = wk_size(c, lt), ra, o, res = -1, oldr = -1;
	lval l, bs = { .k = L_FRAME, .align = 8 }, s = { .k = L_FRAME, .align = 8 };
	uint32_t top = wk_newlabel(e), done = wk_newlabel(e);

	if (wk_mem(e, lt) || (asg && wk_mem(e, rt)) || (sz != 1 && sz != 2 && sz != 4 && sz != 8))
		wk_unsup(e, n, "this operation on an _Atomic object");
	if (asg) {
		val b = wk_gen(e, x->b);
		int rb = wk_reg(e, &b, rt);

		bs.off = wk_slot(e, 8, 8);
		LO(store)(e, rb, &bs, 8);
		LO(tfree)(e, rb);
	}
	l = wk_lval(e, x->a);
	if (l.bf)
		wk_unsup(e, n, "an _Atomic bit-field");
	ra = LO(laddr)(e, &l);
	s.off = wk_slot(e, 8, 8);
	o = LO(atomic)(e, WK_ALOAD, sz, ra, -1, -1);
	LO(label)(e, top);
	LO(store)(e, o, &s, sz);
	{
		val a = wk_reload(e, &s, lt), v;
		int r, nw, got;

		if (post) {
			val ov = wk_reload(e, &s, lt);

			oldr = wk_reg(e, &ov, lt);
		}
		if (asg) {
			val b = wk_vreg(wk_treg(e, rt));

			LO(load)(e, b.r, &bs, 8, false);
			if (is_ptr(c, lt) && ptr_to_vla(c, lt)) {
				v = wk_ptradd_vla(e, a, b, rt, lt, x->aux == T_MINUS);
			} else if (is_ptr(c, lt)) {
				v = wk_ptradd(e, a, b, rt, (int64_t)tsize(c, T(c, lt)->base) ?: 1, x->aux == T_MINUS);
			} else {
				qty ct = wk_optype(c, x->aux, lt, rt);

				a = wk_conv(e, a, lt, ct);
				if (x->aux != T_LESSLESS && x->aux != T_GREATERGREATER)
					b = wk_conv(e, b, rt, ct);
				v = wk_conv(e, wk_arith(e, x->aux, ct, a, b), ct, lt);
			}
		} else {
			v = wk_incval(e, lt, dec, a);
		}
		r = wk_reg(e, &v, lt);
		LO(store)(e, r, &s, sz);
		nw = LO(treg)(e);
		LO(load)(e, nw, &s, sz, false);
		got = LO(atomic)(e, WK_ACAS, sz, ra, nw, o);
		LO(tfree)(e, nw);
		LO(cmpbr)(e, T_EQUALEQUAL, 8, false, wk_vreg(got), wk_vreg(o), done);
		LO(tkeep)(e, got);
		LO(tkeep)(e, o);
		LO(mov)(e, o, got);
		LO(tfree)(e, got);
		LO(tfree)(e, r);
		if (oldr >= 0)
			LO(tfree)(e, oldr);
		LO(jump)(e, top);
		LO(label)(e, done);
		res = post ? oldr : r;
		LO(tkeep)(e, res);
	}
	LO(tfree)(e, o);
	LO(tfree)(e, ra);
	wk_lfree(e, &l);
	return wk_vreg(res);
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

	(void)d, (void)ot;
	wk_scalar(e, x->a, t);
	if (unlikely(T(c, t)->flags & TF_ATOMIC))
		return wk_atomic_rmw(e, n);
	l = wk_lval(e, x->a);
	if (post && wk_mem(e, t)) {
		/* the old value of an object held by address: a copy */
		lval cp = { .k = L_FRAME, .off = wk_slot(e, tsize(c, t), talign(c, t)), .align = talign(c, t) };

		old = wk_putmem(e, &cp, t, wk_reload(e, &l, t));
	} else if (post) {
		old = wk_reload(e, &l, t);
	}
	nv = wk_incval(e, t, dec, wk_reload(e, &l, t));
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

	if (unlikely(is_vec(c, x->ty))) {
		a = wk_gen(e, x->a);
		if (x->aux == T_PLUS)
			return a;
		return wk_vlanes(e, x->aux, N(x->a)->ty, a, N(x->a)->ty, vconst(0), x->ty, true);
	}
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

/* f((double)x) for x a float and f floor, ceil, ... (exact in float): fN in float
   (clang -O2's shrinking of library calls): FN_* + 1, 0 when not */
static int wk_shrink(ecx *e, uint32_t n, uint32_t s)
{
	static const char *const nm[] = { "floor", "ceil", "trunc", "round", "rint", "nearbyint", "fabs" };
	cc *c = e->c;
	const node *x = N(n), *a;
	const vident *id = &vintern_ents[c->syms[s].name];

	if (x->c != 1 || c->syms[s].sc == SC_STATIC || KIND(c, N(n)->ty) != TY_DOUBLE || !e->E->o->opt)
		return 0;
	a = N(c->lists[x->b]);
	if (a->op != N_CAST || KIND(c, a->ty) != TY_DOUBLE || KIND(c, N(a->a)->ty) != TY_FLOAT)
		return 0;
	for (int k = 0; k < 7; k++)
		if (id->len == strlen(nm[k]) && !memcmp(id->s, nm[k], id->len))
			return FN_FLOORF + k + 1;
	return 0;
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
	wkarg *args = alloca((x->c ? x->c : 1) * sizeof(wkarg)), r = { 0 };
	bool var = (f->flags & (TF_VARIADIC | TF_NOPROTO)) != 0, rec_ = wk_mem(e, f->base);

	if (x->c > 4096)
		wk_unsup(e, n, "a call with more than 4096 arguments");
	if (KIND(c, f->base) != TY_VOID)
		wk_scalar(e, n, f->base);
	if (fx->op == N_CAST && fx->aux == CK_FDECAY && N(fx->a)->op == N_VAR) {
		uint32_t s = canon_sym(c, N(fx->a)->a);

		if (c->syms[s].kind == S_BUILTIN) {
			ref = REF(RK_NAME, (uint32_t)c->syms[s].val);	/* a library function by name */
		} else if (wk_can_inline(e, s)) {
			return wk_inline(e, n, s);
		} else if (wk_shrink(e, n, s)) {
			int k = wk_shrink(e, n, s) - 1;
			uint32_t an = N(c->lists[x->b])->a;	/* the float */
			wkarg fa;
			val v;

			LO(classify)(e, N(an)->ty, &fa);
			wk_argslot(e, &fa, N(an)->ty, wk_gen(e, an));
			e->calls = true;
			v = LO(call)(e, FIXED(k), NULL, &fa, 1, false, NULL, prim(TY_FLOAT));
			return wk_conv(e, v, prim(TY_FLOAT), prim(TY_DOUBLE));
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

	int cmark = e->wk->nclean;
	bool r0 = e->reach;	/* (a jump inside does not make what follows the expression dead) */
	val v = vconst(0);

	for (uint32_t i = 0; i + 1 < b->c; i++)
		wk_stmt(e, c->lists[b->b + i]);
	if (b->c) {
		uint32_t last = c->lists[b->b + b->c - 1];

		if (N(last)->op == N_EXPR && N(last)->a)
			v = wk_gen(e, N(last)->a);
		else
			wk_stmt(e, last);
	}
	if (e->wk->nclean > cmark) {
		/* (a register value survives the calls: they save the live temporaries) */
		if (v.k == V_REG || v.k == V_CONST)
			wk_cleanups(e, cmark);
		else
			wk_unsup(e, n, "a statement expression's value with cleanups");
		e->wk->nclean = cmark;
	}
	e->reach = r0;
	return v;
}

/* ---------------- builtins ---------------- */

/* n is a constant once the bound constants are known (no loads, calls or side effects) */
VPRIV bool wk_isconst(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	val k;

	switch (x->op) {
	case N_NUM: case N_FNUM:
		return true;
	case N_VAR:
		return c->syms[x->a].kind == S_ENUMCONST || wk_const_sym(e, x->a, &k);
	case N_CAST:
		return x->aux != CK_DECAY && x->aux != CK_FDECAY && x->aux != CK_VADDR && wk_isconst(e, x->a);
	case N_UNARY:
		return wk_isconst(e, x->a);
	case N_BINARY:
		return wk_isconst(e, x->a) && wk_isconst(e, x->b);
	case N_COND:
		return wk_isconst(e, x->a) && (!x->b || wk_isconst(e, x->b)) && wk_isconst(e, x->c);
	default:
		return false;
	}
}

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

/* a new temporary holding r's value */
static int wk_dup(ecx *e, int r)
{
	int k = LO(treg)(e);

	LO(mov)(e, k, r);
	return k;
}

/*
 * __atomic_* and __sync_* (cc/builtin.c normalized them: the memory orders
 * are the last arguments, evaluated and then ignored, every access is
 * sequentially consistent).  Values travel as integers of the object's size
 * (the generic forms through their pointers); and/or/xor and the op_fetch
 * forms other than add/sub are a compare-and-swap loop.
 */
static val wk_atomic(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	const uint32_t *arg = &c->lists[x->b];
	int id = x->aux, nmo = id == BI_ATOMIC_CMPXCHG_N || id == BI_ATOMIC_CMPXCHG ? 2 : 1, nargs = (int)x->c;
	qty t = QUNQ(T(c, N(arg[0])->ty)->base);
	int sz = (int)tsize(c, t), rp, r1 = -1, r2 = -1, r = -1;
	bool sg = is_integer(c, t) && !is_ptr(c, t) && is_signed(c, t), byptr = id >= BI_ATOMIC_LOAD && id <= BI_ATOMIC_CMPXCHG;
	val p, v;

	if (id == BI_ATOMIC_TEST_AND_SET || id == BI_ATOMIC_CLEAR)
		sz = 1;	/* (a byte, whatever the pointer's type) */
	if (sz != 1 && sz != 2 && sz != 4 && sz != 8)
		wk_unsup(e, n, "a 16-byte atomic");
	p = wk_gen(e, arg[0]);
	rp = wk_reg(e, &p, N(arg[0])->ty);
	if (nargs - nmo > 1) {
		v = wk_gen(e, arg[1]);
		r1 = wk_reg(e, &v, N(arg[1])->ty);
	}
	if (nargs - nmo > 2) {
		v = wk_gen(e, arg[2]);
		r2 = wk_reg(e, &v, N(arg[2])->ty);
	}
	for (int k = nmo + 3 < nargs ? 3 : nargs - nmo; k < nargs; k++) {	/* weak, the orders */
		v = wk_gen(e, arg[k]);
		wk_free(e, &v);
	}
	if (byptr) {
		/* the generic forms: the value operands' bytes */
		lval l = { .k = L_REG, .align = 1 };
		int k;

		if (id == BI_ATOMIC_STORE || id == BI_ATOMIC_EXCHANGE) {
			l.r = (uint8_t)r1;
			k = LO(treg)(e);
			LO(load)(e, k, &l, sz, false);
			LO(tfree)(e, r1);
			r1 = k;
		}
		if (id == BI_ATOMIC_CMPXCHG) {
			l.r = (uint8_t)r2;
			k = LO(treg)(e);
			LO(load)(e, k, &l, sz, false);
			LO(tfree)(e, r2);
			r2 = k;
		}
	}
	switch (id) {
	case BI_ATOMIC_LOAD_N: case BI_ATOMIC_LOAD:
		r = LO(atomic)(e, WK_ALOAD, sz, rp, -1, -1);
		if (id == BI_ATOMIC_LOAD) {
			lval d = { .k = L_REG, .r = (uint8_t)r1, .align = 1 };

			LO(store)(e, r, &d, sz);
			LO(tfree)(e, r);
			LO(tfree)(e, r1);
			r = -1;
		}
		break;
	case BI_ATOMIC_STORE_N: case BI_ATOMIC_STORE:
		LO(atomic)(e, WK_ASTORE, sz, rp, r1, -1);
		LO(tfree)(e, r1);
		break;
	case BI_ATOMIC_EXCHANGE_N: case BI_ATOMIC_EXCHANGE:
		r = LO(atomic)(e, WK_AXCHG, sz, rp, r1, -1);
		LO(tfree)(e, r1);
		if (id == BI_ATOMIC_EXCHANGE) {
			lval d = { .k = L_REG, .r = (uint8_t)r2, .align = 1 };

			LO(store)(e, r, &d, sz);
			LO(tfree)(e, r);
			LO(tfree)(e, r2);
			r = -1;
		}
		break;
	case BI_ATOMIC_CMPXCHG_N: case BI_ATOMIC_CMPXCHG: {
		/* ok = CAS(p, *expected, desired) == *expected; if !ok, *expected = what p held */
		lval le = { .k = L_REG, .r = (uint8_t)r1, .align = 1 };
		int ex = LO(treg)(e), old;
		uint32_t done = wk_newlabel(e);
		val ok;

		LO(load)(e, ex, &le, sz, false);
		old = LO(atomic)(e, WK_ACAS, sz, rp, r2, ex);
		ok = LO(cmp)(e, T_EQUALEQUAL, 8, false, wk_vreg(wk_dup(e, old)), wk_vreg(wk_dup(e, ex)));
		LO(brz)(e, ok, 4, true, done);
		LO(tkeep)(e, ok.r);
		LO(store)(e, old, &le, sz);
		LO(label)(e, done);
		LO(tfree)(e, old);
		LO(tfree)(e, ex);
		LO(tfree)(e, r1);
		LO(tfree)(e, r2);
		r = ok.r;
		sg = false;
		break;
	}
	case BI_ATOMIC_TEST_AND_SET: {
		val one = vconst(1);
		int k = LO(toreg)(e, &one, 8);

		r = LO(atomic)(e, WK_AXCHG, 1, rp, k, -1);
		LO(tfree)(e, k);
		r = LO(cmp)(e, T_EXCLAIMEQ, 4, false, wk_vreg(r), vconst(0)).r;
		sg = false;
		break;
	}
	case BI_ATOMIC_CLEAR: {
		val z = vconst(0);
		int k = LO(toreg)(e, &z, 8);

		LO(atomic)(e, WK_ASTORE, 1, rp, k, -1);
		LO(tfree)(e, k);
		break;
	}
	case BI_SYNC_VAL_CAS: case BI_SYNC_BOOL_CAS: {
		int ex = wk_dup(e, r1);

		LO(ext)(e, ex, sz, false);
		r = LO(atomic)(e, WK_ACAS, sz, rp, r2, ex);
		LO(tfree)(e, r1);
		LO(tfree)(e, r2);
		if (id == BI_SYNC_BOOL_CAS) {
			r = LO(cmp)(e, T_EQUALEQUAL, 8, false, wk_vreg(r), wk_vreg(ex)).r;
			sg = false;
		} else {
			LO(tfree)(e, ex);
		}
		break;
	}
	case BI_ATOMIC_FETCH_ADD: case BI_ATOMIC_FETCH_SUB: case BI_ATOMIC_ADD_FETCH: case BI_ATOMIC_SUB_FETCH: {
		bool sub = id == BI_ATOMIC_FETCH_SUB || id == BI_ATOMIC_SUB_FETCH;
		int a = sub ? LO(un)(e, T_MINUS, 8, wk_vreg(wk_dup(e, r1))).r : r1;

		r = LO(atomic)(e, WK_AXADD, sz, rp, a, -1);
		if (sub)
			LO(tfree)(e, a);
		if (id == BI_ATOMIC_ADD_FETCH || id == BI_ATOMIC_SUB_FETCH)
			r = LO(bin)(e, sub ? T_MINUS : T_PLUS, 8, false, wk_vreg(r), wk_vreg(r1)).r;
		else
			LO(tfree)(e, r1);
		break;
	}
	default: {
		/* and, or, xor, nand, max, min: old = *p; do new = old op v while CAS(p, old, new) != old
		   (old = what it held) */
		int base = id >= BI_ATOMIC_ADD_FETCH ? id - BI_ATOMIC_ADD_FETCH : id - BI_ATOMIC_FETCH_ADD;
		static const int bop[] = { T_PLUS, T_MINUS, T_AMP, T_PIPE, T_CARET };
		uint32_t top = wk_newlabel(e), done = wk_newlabel(e);
		int old = LO(atomic)(e, WK_ALOAD, sz, rp, -1, -1), nw, got;

		LO(label)(e, top);
		if (base < 5) {
			nw = LO(bin)(e, bop[base], 8, false, wk_vreg(wk_dup(e, old)), wk_vreg(wk_dup(e, r1))).r;
		} else if (base == 5) {	/* nand: ~(old & v) */
			nw = LO(bin)(e, T_AMP, 8, false, wk_vreg(wk_dup(e, old)), wk_vreg(wk_dup(e, r1))).r;
			nw = LO(un)(e, T_TILDE, 8, wk_vreg(nw)).r;
		} else {	/* max, min as the object's type compares */
			uint32_t keep = wk_newlabel(e);
			int a = wk_dup(e, old), b = wk_dup(e, r1);

			if (sz < 4) {
				LO(ext)(e, a, sz, sg);
				LO(ext)(e, b, sz, sg);
			}
			nw = wk_dup(e, old);
			LO(cmpbr)(e, base == 6 ? T_GREATEREQ : T_LESSEQ, sz > 4 ? 8 : 4, sg, wk_vreg(a), wk_vreg(b), keep);
			LO(mov)(e, nw, r1);
			LO(label)(e, keep);
		}
		got = LO(atomic)(e, WK_ACAS, sz, rp, nw, old);
		LO(cmpbr)(e, T_EQUALEQUAL, 8, false, wk_vreg(got), wk_vreg(old), done);
		LO(tkeep)(e, got);
		LO(tkeep)(e, old);
		LO(mov)(e, old, got);
		LO(tfree)(e, got);
		LO(tfree)(e, nw);
		LO(jump)(e, top);
		LO(label)(e, done);
		LO(tfree)(e, r1);
		if (id >= BI_ATOMIC_ADD_FETCH) {
			LO(tfree)(e, old);
			r = nw;
			LO(tkeep)(e, nw);
		} else {
			LO(tfree)(e, nw);
			r = old;
		}
		break;
	}
	}
	LO(tfree)(e, rp);
	if (r < 0)
		return vconst(0);
	if (sg && sz < 8)
		LO(ext)(e, r, sz, true);
	return wk_vreg(r);
}

/* a float value parked in the frame slot sl (16 bytes): by address, its bytes; else stored */
static val wk_park(ecx *e, val v, qty t, int32_t sl)
{
	lval l = { .k = L_FRAME, .off = sl, .align = 16 };

	if (wk_mem(e, t))
		return wk_putmem(e, &l, t, v);
	{
		int r = wk_reg(e, &v, t);

		LO(store)(e, r, &l, wk_size(e->c, t));
		LO(tfree)(e, r);
		return vconst(0);
	}
}

/* the parked value again (a new register or address) */
static val wk_unpark(ecx *e, qty t, int32_t sl)
{
	lval l = { .k = L_FRAME, .off = sl, .align = 16 };

	return wk_reload(e, &l, t);
}

/*
 * The class of the float parked at slot sl as a 64-bit pattern without its
 * sign: the exponent's eb bits at the top, a nonzero mantissa nonzero below
 * (x87: the explicit integer bit dropped; __float128: its low word as a
 * sticky bit).
 */
static val wk_fpattern(ecx *e, qty t, int32_t sl, int *eb)
{
	cc *c = e->c;
	lval l = { .k = L_FRAME, .off = sl, .align = 16 }, h = l;
	int k = KIND(c, t), r = LO(treg)(e);
	qty u = prim(TY_ULONG);

	h.off += 8;
	if (k == TY_FLOAT || k == TY_DOUBLE) {
		LO(load)(e, r, &l, k == TY_FLOAT ? 4 : 8, false);
		*eb = k == TY_FLOAT ? 8 : 11;
		return wk_arith(e, T_LESSLESS, u, wk_vreg(r), vconst(k == TY_FLOAT ? 33 : 1));
	}
	{
		int m = LO(treg)(e);
		val s;

		*eb = 15;
		LO(load)(e, m, &l, 8, false);
		if (k == TY_LDOUBLE) {
			LO(load)(e, r, &h, 2, false);
			s = wk_arith(e, T_LESSLESS, u, wk_vreg(m), vconst(1));
			s = LO(cmp)(e, T_EXCLAIMEQ, 8, false, s, vconst(0));
			return wk_arith(e, T_PIPE, u, wk_arith(e, T_LESSLESS, u, wk_vreg(r), vconst(49)), s);
		}
		LO(load)(e, r, &h, 8, false);
		s = LO(cmp)(e, T_EXCLAIMEQ, 8, false, wk_vreg(m), vconst(0));
		return wk_arith(e, T_PIPE, u, wk_arith(e, T_LESSLESS, u, wk_vreg(r), vconst(1)), s);
	}
}

/* one class test of the parked float: 0 or 1 */
static val wk_fptest(ecx *e, int bi, qty t, int32_t sl)
{
	int eb;
	val p = wk_fpattern(e, t, sl, &eb);
	uint64_t top = ~0ull << (64 - eb);	/* the exponent all ones, a zero mantissa */

	if (bi == BI_ISNORMAL) {
		/* exponent - 1 below all ones - 1: neither zero/subnormal nor inf/NaN */
		p = wk_arith(e, T_GREATERGREATER, prim(TY_ULONG), p, vconst(64 - eb));
		p = wk_arith(e, T_MINUS, prim(TY_ULONG), p, vconst(1));
		return LO(cmp)(e, T_LESS, 8, false, p, vconst((i128)((1ull << eb) - 2)));
	}
	if (bi == 0)	/* zero */
		return LO(cmp)(e, T_EQUALEQUAL, 8, false, p, vconst(0));
	return LO(cmp)(e, bi == BI_ISNAN ? T_GREATER : bi == BI_ISINF ? T_EQUALEQUAL : T_LESS, 8, false, p, vconst((i128)top));
}

/* isnan, isinf, isfinite, isnormal, isinf_sign, fpclassify on a value */
static val wk_fpclass(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	const uint32_t *arg = &c->lists[x->b];
	uint32_t vn = x->aux == BI_FPCLASSIFY ? arg[5] : arg[0];
	qty t = N(vn)->ty;
	int32_t sl = wk_slot(e, 16, 16);
	val v;

	if (!is_flt(c, t))
		wk_unsup(e, n, "a floating-point class builtin on an integer");
	if (x->aux == BI_FPCLASSIFY) {
		/* fpclassify(nan, inf, normal, subnormal, zero, x): the five values evaluated first */
		int32_t rs = wk_slot(e, 4 * 5, 4);
		static const int test[4] = { BI_ISNAN, BI_ISINF, BI_ISNORMAL, 0 };
		static const int which[4] = { 0, 1, 2, 4 };
		uint32_t done = wk_newlabel(e);
		lval out = { .k = L_FRAME, .off = rs, .align = 4 };

		for (int k = 0; k < 5; k++) {
			lval d = { .k = L_FRAME, .off = rs + 4 * k, .align = 4 };
			val a = wk_conv(e, wk_gen(e, arg[k]), N(arg[k])->ty, prim(TY_INT));
			int r = wk_reg(e, &a, prim(TY_INT));

			LO(store)(e, r, &d, 4);
			LO(tfree)(e, r);
		}
		v = wk_park(e, wk_gen(e, vn), t, sl);
		wk_free(e, &v);
		/* the result in the subnormal entry (3) unless a test picks another */
		for (int k = 0; k < 4; k++) {
			uint32_t skip = wk_newlabel(e);
			lval s = { .k = L_FRAME, .off = rs + 4 * which[k], .align = 4 }, d = out;
			int r;

			d.off = rs + 12;
			LO(brz)(e, wk_fptest(e, test[k], t, sl), 4, false, skip);
			r = LO(treg)(e);
			LO(load)(e, r, &s, 4, false);
			LO(store)(e, r, &d, 4);
			LO(tfree)(e, r);
			LO(jump)(e, done);
			LO(label)(e, skip);
		}
		LO(label)(e, done);
		out.off = rs + 12;
		{
			int r = LO(treg)(e);

			LO(load)(e, r, &out, 4, true);
			return wk_vreg(r);
		}
	}
	v = wk_park(e, wk_gen(e, vn), t, sl);
	wk_free(e, &v);
	if (x->aux == BI_ISINF_SIGN) {
		/* isinf(x) ? (signbit(x) ? -1 : 1) : 0 */
		lval l = { .k = L_FRAME, .off = sl, .align = 16 };
		int k = KIND(c, t), r;
		val inf = wk_fptest(e, BI_ISINF, t, sl), sg;

		l.off += k == TY_FLOAT ? 3 : k == TY_DOUBLE ? 7 : k == TY_LDOUBLE ? 9 : 15;
		r = LO(treg)(e);
		LO(load)(e, r, &l, 1, false);
		sg = LO(bin)(e, T_GREATERGREATER, 4, false, wk_vreg(r), vconst(7));
		sg = LO(bin)(e, T_STAR, 4, false, sg, vconst(-2));
		sg = LO(bin)(e, T_PLUS, 4, false, sg, vconst(1));	/* 1 or -1 */
		return LO(bin)(e, T_STAR, 4, false, sg, inf);
	}
	return wk_fptest(e, x->aux, t, sl);
}

static val wk_builtin(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	const uint32_t *arg = &c->lists[x->b];
	char b[64];

	switch (x->aux) {
	case BI_CONSTANT_P: {
		/* clang's rule without the optimizer: 1 if the front end folds the operand (a pointer
		   only as a string literal at offset 0), else 0 (the operand is not evaluated) */
		cval cv;

		if (cc_eval(c, arg[0], &cv) && (cv.kind == CV_INT || cv.kind == CV_FLT ||
						(cv.kind == CV_ADDR && cv.str && cv.i == 0 && !cv.sym && !cv.label)))
			return vconst(1);
		/* after inlining and constant locals: built from constants only, without side effects */
		return vconst(!side_effects(c, arg[0]) && wk_isconst(e, arg[0]));
	}
	case BI_EXPECT: case BI_EXPECT_PROB: case BI_ASSUME_ALIGNED: case BI_EXTRACT_RETURN_ADDR: {
		/* the value of the first argument; the others are evaluated first */
		val v;

		for (uint32_t i = 1; i < x->c; i++) {
			v = wk_gen(e, arg[i]);
			wk_free(e, &v);
		}
		return wk_gen(e, arg[0]);
	}
	case BI_ASSUME: case BI_IS_CONSTANT_EVALUATED:
		return vconst(0);
	case BI_PREFETCH: {
		val v = wk_gen(e, arg[0]);

		wk_free(e, &v);
		return vconst(0);
	}
	case BI_UNREACHABLE: case BI_TRAP:
		LO(trap)(e);
		return vconst(0);
	case BI_RETURN_ADDRESS: case BI_FRAME_ADDRESS: {
		val v = wk_gen(e, arg[0]);

		if (v.k != V_CONST || v.i < 0 || v.i > 4096)
			wk_unsup(e, n, "a return or frame address of a level not a constant");
		return LO(frame)(e, x->aux == BI_RETURN_ADDRESS, (int)v.i);
	}
	case BI_OBJECT_SIZE: case BI_DYNAMIC_OBJECT_SIZE: {
		val t = wk_gen(e, arg[1]);

		return vconst(t.k == V_CONST && (t.i & 2) ? 0 : (i128)(uint64_t)-1);
	}
	case BI_SHUFFLEVECTOR: case BI_CONVERTVECTOR: {
		/* lane by lane through a frame object (vectors are addresses) */
		qty at = N(arg[0])->ty, ae = T(c, at)->base, re = T(c, x->ty)->base;
		int64_t na = (int64_t)(tsize(c, at) / tsize(c, ae)), nr = (int64_t)(tsize(c, x->ty) / tsize(c, re));
		bool sh = x->aux == BI_SHUFFLEVECTOR;
		val a = wk_gen(e, arg[0]), bv = sh ? wk_rhs(e, &a, arg[1], prim(TY_ULONG)) : vconst(0);
		int ra = wk_reg(e, &a, prim(TY_ULONG)), rb = sh ? wk_reg(e, &bv, prim(TY_ULONG)) : -1;
		lval d = { .k = L_FRAME, .off = wk_slot(e, tsize(c, x->ty), 16), .align = talign(c, re) };

		for (int64_t i = 0; i < nr; i++) {
			lval dl = d, sl = { .k = L_REG, .align = talign(c, ae) };
			val v;
			int r;

			dl.off += i * (int64_t)tsize(c, re);
			if (sh) {
				int64_t k = (int64_t)c->ipool[N(arg[2 + i])->a];

				if (k < 0)
					continue;	/* an undefined lane */
				sl.r = (uint8_t)(k < na ? ra : rb);
				sl.off = (k < na ? k : k - na) * (int64_t)tsize(c, ae);
				v = wk_reload(e, &sl, ae);
			} else {
				sl.r = (uint8_t)ra;
				sl.off = i * (int64_t)tsize(c, ae);
				v = wk_conv(e, wk_reload(e, &sl, ae), ae, re);
			}
			r = wk_reg(e, &v, re);
			LO(store)(e, r, &dl, (int)tsize(c, re));
			LO(tfree)(e, r);
		}
		LO(tfree)(e, ra);
		if (rb >= 0)
			LO(tfree)(e, rb);
		return wk_vreg(LO(laddr)(e, &d));
	}
	case BI_ATOMIC_LOAD_N: case BI_ATOMIC_STORE_N: case BI_ATOMIC_EXCHANGE_N: case BI_ATOMIC_CMPXCHG_N:
	case BI_ATOMIC_LOAD: case BI_ATOMIC_STORE: case BI_ATOMIC_EXCHANGE: case BI_ATOMIC_CMPXCHG:
	case BI_ATOMIC_FETCH_ADD: case BI_ATOMIC_FETCH_SUB: case BI_ATOMIC_FETCH_AND: case BI_ATOMIC_FETCH_OR:
	case BI_ATOMIC_FETCH_XOR: case BI_ATOMIC_ADD_FETCH: case BI_ATOMIC_SUB_FETCH: case BI_ATOMIC_AND_FETCH:
	case BI_ATOMIC_OR_FETCH: case BI_ATOMIC_XOR_FETCH: case BI_SYNC_VAL_CAS: case BI_SYNC_BOOL_CAS:
	case BI_ATOMIC_FETCH_NAND: case BI_ATOMIC_FETCH_MAX: case BI_ATOMIC_FETCH_MIN: case BI_ATOMIC_NAND_FETCH:
	case BI_ATOMIC_MAX_FETCH: case BI_ATOMIC_MIN_FETCH: case BI_ATOMIC_TEST_AND_SET: case BI_ATOMIC_CLEAR:
		return wk_atomic(e, n);
	case BI_ATOMIC_THREAD_FENCE: case BI_ATOMIC_SIGNAL_FENCE: {
		val m = wk_gen(e, arg[0]);

		wk_free(e, &m);
		if (x->aux == BI_ATOMIC_THREAD_FENCE)
			LO(atomic)(e, WK_AFENCE, 8, -1, -1, -1);
		return vconst(0);
	}
	case BI_ATOMIC_LOCK_FREE:
		for (uint32_t i = 0; i < x->c; i++) {	/* (__c11_atomic_is_lock_free has one argument) */
			val a = wk_gen(e, arg[i]);

			wk_free(e, &a);
		}
		return vconst(1);
	case BI_SETJMP: case BI_LONGJMP: {
		val p = wk_gen(e, arg[0]);
		int r = wk_reg(e, &p, prim(TY_ULONG));

		if (x->aux == BI_SETJMP) {
			if (e->wk->falign > 16)
				wk_unsup(e, n, "__builtin_setjmp in a function with locals aligned above 16");
			return LO(setjmp)(e, r);
		}
		{
			val k = wk_gen(e, arg[1]);	/* (GCC requires 1) */

			wk_free(e, &k);
		}
		LO(longjmp)(e, r);
		return vconst(0);
	}
	case BI_ADD_OVERFLOW: case BI_SUB_OVERFLOW: case BI_MUL_OVERFLOW:
		if (wk_size(c, N(arg[0])->ty) <= 8 && wk_size(c, N(arg[1])->ty) <= 8 && wk_size(c, T(c, N(arg[2])->ty)->base) == 16 &&
		    LO(i128mem)()) {
			/*
			 * Operands of up to 64 bits, a 128-bit result: the exact value fits 128 bits
			 * (two unsigned 64-bit factors as unsigned 128, the rest as signed 128), so
			 * the overflow is its sign against the result's signedness.
			 */
			qty ta = N(arg[0])->ty, tb = N(arg[1])->ty, tr = T(c, N(arg[2])->ty)->base, w = prim(TY_INT128);
			int op = x->aux == BI_ADD_OVERFLOW ? T_PLUS : x->aux == BI_SUB_OVERFLOW ? T_MINUS : T_STAR;
			bool uu = op == T_STAR && !wk_signed(c, ta) && !wk_signed(c, tb);
			val a = wk_conv(e, wk_gen(e, arg[0]), ta, w), b = wk_conv(e, wk_gen(e, arg[1]), tb, w), r, d;
			lval hi;
			int rr, s;

			r = LO(wbin)(e, op, true, a, b);
			d = wk_gen(e, arg[2]);
			{
				lval l = wk_fromval(e, d, tr);

				r = wk_putmem(e, &l, tr, r);	/* (the stored object's address) */
			}
			rr = wk_reg(e, &r, prim(TY_ULONG));
			if (uu == wk_signed(c, tr)) {
				/* unsigned x unsigned into signed, or anything else into unsigned: the sign bit */
				hi = (lval){ .k = L_REG, .r = (uint8_t)rr, .off = 8, .align = 8 };
				s = LO(treg)(e);
				LO(load)(e, s, &hi, 8, false);
				LO(tfree)(e, rr);
				return LO(bin)(e, T_GREATERGREATER, 8, false, wk_vreg(s), vconst(63));
			}
			LO(tfree)(e, rr);
			return vconst(0);
		}
		goto ovf64;
	case BI_ALLOCA: case BI_ALLOCA_UNINIT: {
		val v = wk_gen(e, arg[0]);

		e->dyn = true;
		return LO(alloca)(e, wk_conv(e, v, N(arg[0])->ty, prim(TY_ULONG)));
	}
	case BI_CLZ: case BI_CLZL: case BI_CLZLL: case BI_CTZ: case BI_CTZL: case BI_CTZLL:
	case BI_FFS: case BI_FFSL: case BI_FFSLL: case BI_POPCOUNT: case BI_POPCOUNTL: case BI_POPCOUNTLL:
	case BI_PARITY: case BI_PARITYL: case BI_PARITYLL: case BI_BSWAP16: case BI_BSWAP32: case BI_BSWAP64: {
		qty at = N(arg[0])->ty;
		val v = wk_gen(e, arg[0]);
		int sz = wk_size(c, at) > 4 ? 8 : 4;

		if (wk_size(c, at) < 4)
			v = wk_conv(e, v, at, prim(TY_UINT));
		return LO(bitop)(e, x->aux, sz, v);
	}
	case BI_CLRSB: case BI_CLRSBL: case BI_CLRSBLL: {
		/* clz(((x ^ (x >> (bits - 1))) << 1) | 1): the redundant sign bits, never clz of 0 */
		qty at = N(arg[0])->ty;
		int sz = wk_size(c, at) > 4 ? 8 : 4;
		val v = wk_conv(e, wk_gen(e, arg[0]), at, sz == 8 ? prim(TY_LONG) : prim(TY_INT)), t;
		int r = wk_reg(e, &v, prim(TY_LONG));

		t = LO(bin)(e, T_GREATERGREATER, sz, true, wk_vreg(wk_dup(e, r)), vconst(sz * 8 - 1));
		v = LO(bin)(e, T_CARET, sz, false, wk_vreg(r), t);
		v = LO(bin)(e, T_LESSLESS, sz, false, v, vconst(1));
		v = LO(bin)(e, T_PIPE, sz, false, v, vconst(1));
		return LO(bitop)(e, sz == 8 ? BI_CLZLL : BI_CLZ, sz, v);
	}
	case BI_BITREVERSE8: case BI_BITREVERSE16: case BI_BITREVERSE32: case BI_BITREVERSE64: {
		/* bits swapped in pairs, pairs in nibbles, nibbles in bytes; then the bytes; then shifted down */
		static const uint64_t m[3] = { 0x5555555555555555ull, 0x3333333333333333ull, 0x0f0f0f0f0f0f0f0full };
		qty at = N(arg[0])->ty;
		int bits = 8 << (x->aux - BI_BITREVERSE8);
		val v = wk_gen(e, arg[0]);
		int r;

		if (v.k == V_CONST) {
			uint64_t u = (uint64_t)v.i & (bits == 64 ? ~0ull : (1ull << bits) - 1), o = 0;

			for (int i = 0; i < bits; i++)
				o |= ((u >> i) & 1) << (bits - 1 - i);
			return vconst((i128)o);
		}
		r = wk_reg(e, &v, at);
		if (bits < 64)
			LO(ext)(e, r, bits / 8, false);
		for (int k = 0; k < 3; k++) {
			val lo = LO(bin)(e, T_AMP, 8, false, wk_vreg(wk_dup(e, r)), vconst((i128)m[k]));
			val hi = LO(bin)(e, T_GREATERGREATER, 8, false, wk_vreg(r), vconst(1 << k));

			hi = LO(bin)(e, T_AMP, 8, false, hi, vconst((i128)m[k]));
			lo = LO(bin)(e, T_LESSLESS, 8, false, lo, vconst(1 << k));
			r = LO(bin)(e, T_PIPE, 8, false, lo, hi).r;
		}
		v = LO(bitop)(e, BI_BSWAP64, 8, wk_vreg(r));
		if (bits < 64)
			v = LO(bin)(e, T_GREATERGREATER, 8, false, v, vconst(64 - bits));
		return v;
	}
	case BI_HUGE_VAL: case BI_HUGE_VALF: case BI_INF: case BI_INFF: case BI_NAN: case BI_NANF: {
		/* the constant the front end folds (nan's payload from its string) */
		cval cv;
		uint64_t bits = 0;

		if (!cc_eval(c, n, &cv) || cv.kind != CV_FLT)
			wk_unsup(e, n, "this __builtin_nan argument");
		if (KIND(c, x->ty) == TY_FLOAT) {
			float f = (float)cv.f;

			memcpy(&bits, &f, 4);
		} else {
			memcpy(&bits, &cv.f, 8);
		}
		return LO(fconst)(e, bits, KIND(c, x->ty) == TY_FLOAT ? 4 : 8);
	}
	case BI_ISNAN: case BI_ISINF: case BI_ISFINITE: case BI_ISNORMAL: case BI_ISINF_SIGN: case BI_FPCLASSIFY: {
		cval cv;

		if (cc_eval(c, n, &cv) && cv.kind == CV_INT)
			return vconst(cv.i);
		return wk_fpclass(e, n);
	}
	case BI_ISGREATER: case BI_ISGREATEREQUAL: case BI_ISLESS: case BI_ISLESSEQUAL: case BI_ISLESSGREATER: case BI_ISUNORDERED: {
		/* the ordered comparison (false when unordered); unordered: a != a || b != b */
		qty ta = N(arg[0])->ty, tb = N(arg[1])->ty, t = KIND(c, tb) > KIND(c, ta) ? tb : ta;
		int32_t sa = wk_slot(e, 16, 16), sb = wk_slot(e, 16, 16);
		val a = wk_conv(e, wk_gen(e, arg[0]), ta, t), b;
		static const int op[] = { T_GREATER, T_GREATEREQ, T_LESS, T_LESSEQ };

		a = wk_park(e, a, t, sa);
		b = wk_park(e, wk_conv(e, wk_gen(e, arg[1]), tb, t), t, sb);
		if (x->aux <= BI_ISLESSEQUAL) {
			wk_free(e, &b);
			wk_free(e, &a);
			return LO(fcmp)(e, op[x->aux - BI_ISGREATER], t, wk_unpark(e, t, sa), wk_unpark(e, t, sb));
		}
		wk_free(e, &a);
		wk_free(e, &b);
		if (x->aux == BI_ISLESSGREATER) {
			val l = LO(fcmp)(e, T_LESS, t, wk_unpark(e, t, sa), wk_unpark(e, t, sb));
			val g = LO(fcmp)(e, T_GREATER, t, wk_unpark(e, t, sa), wk_unpark(e, t, sb));

			return LO(bin)(e, T_PIPE, 4, false, l, g);
		}
		{
			val u = LO(fcmp)(e, T_EXCLAIMEQ, t, wk_unpark(e, t, sa), wk_unpark(e, t, sa));
			val w = LO(fcmp)(e, T_EXCLAIMEQ, t, wk_unpark(e, t, sb), wk_unpark(e, t, sb));

			return LO(bin)(e, T_PIPE, 4, false, u, w);
		}
	}
	ovf64: {
		/* the exact result in 128 bits; overflow when it differs from the stored one extended back */
		qty ta = N(arg[0])->ty, tb = N(arg[1])->ty, tr = T(c, N(arg[2])->ty)->base, w = prim(TY_INT128);
		int op = x->aux == BI_ADD_OVERFLOW ? T_PLUS : x->aux == BI_SUB_OVERFLOW ? T_MINUS : T_STAR;
		val a, b, r, lo, back;
		lval l;
		int rr, cp, rs;

		if (wk_size(c, ta) > 8 || wk_size(c, tb) > 8 || wk_size(c, tr) > 8 || !LO(i128mem)())
			wk_unsup(e, n, "a 128-bit overflow builtin");
		a = wk_conv(e, wk_gen(e, arg[0]), ta, w);
		b = wk_conv(e, wk_gen(e, arg[1]), tb, w);
		r = LO(wbin)(e, op, true, a, b);
		l = wk_fromval(e, wk_gen(e, arg[2]), tr);
		rr = wk_reg(e, &r, prim(TY_ULONG));
		cp = LO(treg)(e);
		LO(mov)(e, cp, rr);
		lo = wk_conv(e, (val){ .k = V_REG, .r = (uint8_t)cp }, w, tr);
		rs = wk_reg(e, &lo, tr);
		LO(store)(e, rs, &l, wk_size(c, tr));
		wk_lfree(e, &l);
		back = wk_conv(e, lo, tr, w);
		return LO(wcmp)(e, T_EXCLAIMEQ, true, (val){ .k = V_REG, .r = (uint8_t)rr }, back);
	}
	case BI_SIGNBIT: {
		qty at = N(arg[0])->ty;
		val v = wk_gen(e, arg[0]);

		if (wk_mem(e, at)) {
			/* x87: the sign is bit 15 of the top word (__float128: of its last two bytes) */
			lval l = wk_fromval(e, v, at);
			int r = LO(treg)(e);

			l.off += KIND(c, at) == TY_FLOAT128 ? 14 : 8;
			LO(load)(e, r, &l, 2, false);
			wk_lfree(e, &l);
			return LO(bin)(e, T_GREATERGREATER, 4, false, (val){ .k = V_REG, .r = (uint8_t)r }, vconst(15));
		}
		v = LO(fbits)(e, v, wk_size(c, at));
		return LO(bin)(e, T_GREATERGREATER, 8, false, v, vconst(wk_size(c, at) * 8 - 1));
	}
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
	default: {
		cval cv;

		if (cc_eval(c, n, &cv) && cv.kind == CV_INT)
			return vconst(cv.i);	/* folded by the front end (__builtin_classify_type ...) */
		break;
	}
	}

	snprintf(b, sizeof b, "builtin %d", N(n)->aux);
	efatal(e, N(n)->loc, "x86-64: %s is not supported yet (PLAN-x86.md X3)", b);
}

/* ---------------- expressions ---------------- */

/* a const (not volatile) global with a constant initializer: its value (arm64's const_global) */
static bool wk_global_cv(ecx *e, uint32_t s, cval *cv)
{
	cc *c = e->c;
	const sym *y;

	s = canon_sym(c, s);
	y = &c->syms[s];
	return y->kind == S_VAR && !is_local_var(c, s) && (QQ(y->ty) & (Q_CONST | Q_VOLATILE)) == Q_CONST &&
	       (is_integer(c, y->ty) || (is_flt(c, y->ty) && tsize(c, y->ty) != 2)) && !wk_wide(c, y->ty) &&
	       y->body && N(y->body)->op != N_INIT && cc_eval(c, y->body, cv) &&
	       cv->kind == (is_integer(c, y->ty) ? CV_INT : CV_FLT);
}

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
		{
			val k;

			if (wk_const_sym(e, x->a, &k))
				return k;
		}
		{
			cval cv;

			if (is_integer(c, x->ty) && wk_global_cv(e, x->a, &cv))
				return vconst(em_trunc_to(c, cv.i, x->ty));
		}
		wk_scalar(e, n, x->ty);
		return wk_load(e, wk_lval(e, n), x->ty);
	}
	case N_COMPLIT: {
		lval l = wk_lval(e, n);

		return wk_load(e, l, x->ty);
	}
	case N_STR: case N_MEMBER: case N_INDEX: case N_DEREF:
		if (x->op == N_INDEX) {
			uint32_t as;
			int64_t ak;
			int i;

			if (wk_ael_of(e, n, &as, &ak) && (i = wk_ael_find(e, as, ak)) >= 0)
				return vconst(e->wk->ael[i].v);
		}
		if (!is_array(c, x->ty) && !is_func(c, x->ty))
			wk_scalar(e, n, x->ty);
		return wk_load(e, wk_lval(e, n), x->ty);
	case N_ADDR: {
		lval l;

		if (N(x->a)->op == N_LABELADDR)
			return wk_gen(e, x->a);
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
	case N_CAST: {
		cval cv;

		/* (int)k for a const double k = 1.0: folded, as clang -O2 */
		if (is_integer(c, x->ty) && !wk_wide(c, x->ty) && KIND(c, x->ty) != TY_BOOL && N(x->a)->op == N_VAR &&
		    is_flt(c, N(x->a)->ty) && !is_ld(c, N(x->a)->ty) && wk_global_cv(e, N(x->a)->a, &cv) &&
		    cv.f > -9.2e18 && cv.f < 9.2e18) {
			i128 v = (i128)(int64_t)cv.f;

			if (em_trunc_to(c, v, x->ty) == v)	/* (in range: else UB, left to the code) */
				return vconst(v);
		}
		return wk_cast(e, n);
	}
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
	case N_LABELADDR:
		note_ref(e, REF(RK_LBL, x->a));
		return (val){ .k = V_ADDR, .ref = REF(RK_LBL, x->a) };
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

			if (KIND(c, x->ty) == TY_FLOAT128) {	/* binary128 as it is */
				lo = (uint64_t)q, hi = (uint64_t)(q >> 64);
			} else {
				q_to_x87(q, b);
				memcpy(&lo, b, 8);
				hi = (uint64_t)b[8] | (uint64_t)b[9] << 8;
			}
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
	case N_SIZEOF_VLA:
		if (x->a && N(x->a)->op == N_VAR && is_local_var(c, N(x->a)->a) && (T(c, c->syms[N(x->a)->a].ty)->flags & TF_VLA)) {
			lval l = { .k = L_FRAME, .off = wk_local(e, N(x->a)->a) + 16, .align = 8 };
			int r = LO(treg)(e);

			LO(load)(e, r, &l, 8, false);
			return wk_vreg(r);
		}
		return wk_vla_bytes(e, x->b);
	case N_CPLX: {
		/* a _Complex value from its parts: both into a temporary object, its address */
		qty et = c->tags[T(c, x->ty)->aux].f[0].ty;
		int64_t es = (int64_t)tsize(c, et);
		lval d = { .k = L_FRAME, .off = wk_slot(e, 2 * (uint64_t)es, talign(c, x->ty)), .align = talign(c, et) };

		for (int k = 0; k < 2; k++, d.off += es) {
			val v = wk_gen(e, k ? x->b : x->a);

			if (wk_mem(e, et)) {
				v = wk_putmem(e, &d, et, v);
				wk_free(e, &v);
			} else {
				int r = wk_reg(e, &v, et);

				LO(store)(e, r, &d, (int)es);
				LO(tfree)(e, r);
			}
		}
		d.off -= 2 * es;
		return wk_vreg(LO(laddr)(e, &d));
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
	/* known from its form (x >> 0 != x, x < y && x > y, ...): arm64's folds (cond.c) */
	int k = cond_fold(e, n);

	if (k >= 0) {
		if (k == (int)jump_if)
			LO(jump)(e, lbl), wk_state(e)->cjump = true;	/* (always: what follows is dead) */
		return;
	}

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
			wk_state(e)->cjump = false;	/* (skip falls through) */
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
					LO(jump)(e, lbl), wk_state(e)->cjump = true;	/* (always: what follows is dead) */
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
				LO(jump)(e, lbl), wk_state(e)->cjump = true;	/* (always: what follows is dead) */
			return;
		}
		if (v.k == V_ADDR && !wk_weak0(c, v)) {
			if (jump_if)
				LO(jump)(e, lbl), wk_state(e)->cjump = true;	/* (always: what follows is dead) */
			return;
		}
		LO(brz)(e, v, is_ptr(c, x->ty) ? 8 : opw(c, x->ty), jump_if, lbl);
	}
}
