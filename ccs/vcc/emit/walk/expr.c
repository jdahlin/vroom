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

	if ((k == TY_INT128 || k == TY_UINT128) && !LO(i128mem)())
		wk_unsup(e, n, "__int128");
}

static bool wk_wide(cc *c, qty t) { return KIND(c, t) == TY_INT128 || KIND(c, t) == TY_UINT128; }

VPRIV bool wk_mem(ecx *e, qty t)
{
	cc *c = e->c;

	return em_bigint(c, t) || is_record(c, t) || is_array(c, t) || is_vec(c, t) || KIND(c, t) == TY_FLOAT128 || (KIND(c, t) == TY_LDOUBLE && LO(ldmem)()) || (wk_wide(c, t) && LO(i128mem)());
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
VPRIV bool wk_weak0(cc *c, val v)
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
VPRIV val wk_reload(ecx *e, const lval *l, qty t);
VPRIV val wk_putmem(ecx *e, lval *l, qty t, val v);
static val wk_incval(ecx *e, qty t, bool dec, val a);

static void wk_lfree(ecx *e, const lval *l)
{
	if (l->k == L_REG)
		LO(tfree)(e, l->r);
}

/* a local's frame slot, made on first use */
VPRIV int64_t wk_local(ecx *e, uint32_t s)
{
	wkst *W = e->wk;
	cc *c = e->c;

	if (W->sgen[s] != W->gen) {
		W->sgen[s] = W->gen;
		if (W->canary && (is_array(c, c->syms[s].ty) || is_agg(c, c->syms[s].ty) || e->E->lnoreg[s]))
			W->spneed = true;	/* (clang's -fstack-protector-strong, a superset) */
		if (T(c, c->syms[s].ty)->flags & TF_VLA)
			W->soff[s] = wk_pslot(e, 24, 8);	/* { address, sp before it, bytes } */
		else {
			uint64_t size = tsize(c, c->syms[s].ty) ? tsize(c, c->syms[s].ty) : 1;
			uint32_t align = sym_align(c, s) ? sym_align(c, s) : 1;
			uint32_t i = W->ntf;

			if (W->inlocal && W->nlu < WK_MAXTSLOT && size <= UINT32_MAX) {
				/* a local of an inline copy: a free slot, given back when the copy ends */
				while (i-- > 0)
					if (W->tf[i].size == size && W->tf[i].align == align)
						break;
				if (i != UINT32_MAX) {
					W->soff[s] = W->tf[i].off;
					W->tf[i] = W->tf[--W->ntf];
				} else {
					W->soff[s] = wk_pslot(e, size, align);
				}
				W->lu[W->nlu++] = (struct wklslot){ W->soff[s], (uint32_t)size, align, s };
			} else {
				W->soff[s] = wk_pslot(e, size, align);
			}
		}
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
		e->wk->nael = 0, e->wk->npk = 0;
	else
		wk_ael_drop(e, s, 0, true);
}

VPRIV bool wk_pk_get(ecx *e, uint32_t s, val *v)
{
	wkst *W = e->wk;

	for (int i = 0; i < W->npk; i++)
		if (W->pk[i].s == s && W->pk[i].gen == W->gen) {	/* (the parameters of one copy: its own syms) */
			*v = W->pk[i].v;
			return true;
		}
	return false;
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
		if (x->op == N_VAR && e->wk->npk)	/* (a provisional constant ends at a write, or its address) */
			for (int i = 0; i < e->wk->npk; i++)
				if (e->wk->pk[i].s == x->a)
					e->wk->pk[i] = e->wk->pk[--e->wk->npk], i--;
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
			if (LO(local)(e, s, &l))
				return l;
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

		if (x->aux == 1) {
			if (i.k == V_CONST) {
				l = wk_fromval(e, p, et);
				l.off += (int64_t)(i.i / 8);
				l.bf = 1, l.width = 1, l.bitoff = (uint16_t)(i.i & 7);
				return l;
			}
			int ri = wk_reg(e, &i, prim(TY_LONG)), rb = LO(treg)(e);
			LO(mov)(e, rb, ri);
			val bit = LO(bin)(e, T_AMP, 8, false, wk_vreg(rb), vconst(7));
			lval bs = { .k = L_FRAME, .off = wk_slot(e, 8, 8), .align = 8 };
			LO(store)(e, bit.r, &bs, 8);
			LO(tfree)(e, bit.r);
			i = LO(bin)(e, T_GREATERGREATER, 8, false, wk_vreg(ri), vconst(3));
			l = wk_fromval(e, LO(bin)(e, T_PLUS, 8, false, p, i), et);
			l.bf = 1, l.width = 1, l.dynbit_slot = bs.off;
			return l;
		}

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
		if (x->aux == CK_NOP) return wk_lval(e, x->a);
		if (x->aux == CK_BITS && wk_mem(e, x->ty)) return wk_fromval(e, wk_gen(e, n), x->ty);
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
	/* Packed fields can span two machine words. Access only their actual
	   bytes, avoiding an over-read beyond the containing packed object. */
	*uoff = wk_floordiv(bit, 8), *us = 0, *pos = (int)(bit - *uoff * 8);
}

/* The largest machine load/store contained in the remaining field bytes. */
static int wk_bf_piece(int pos, int width)
{
	int bytes = (pos + width + 7) / 8;

	return bytes >= 8 ? 8 : bytes >= 4 ? 4 : bytes >= 2 ? 2 : 1;
}

static val wk_bf_load_split(ecx *e, lval u, int pos, int width)
{
	val out = vconst(0);
	int done = 0;

	while (width) {
		int size = wk_bf_piece(pos, width);
		int bits = size * 8 - pos;
		val v = { .k = V_REG, .r = (uint8_t)LO(treg)(e) };

		if (bits > width) bits = width;
		LO(load)(e, v.r, &u, size, false);
		if (pos)
			v = LO(bin)(e, T_GREATERGREATER, 8, false, v, vconst(pos));
		if (bits < 64)
			v = LO(bin)(e, T_AMP, 8, false, v, vconst(((uint64_t)1 << bits) - 1));
		if (done)
			v = LO(bin)(e, T_LESSLESS, 8, false, v, vconst(done));
		out = done ? LO(bin)(e, T_PIPE, 8, false, out, v) : v;
		done += bits, width -= bits, pos = 0, u.off += size;
	}
	return out;
}

static void wk_bf_store_split(ecx *e, lval u, int pos, int width, int rv)
{
	int done = 0;

	while (width) {
		int size = wk_bf_piece(pos, width);
		int bits = size * 8 - pos;
		val v = { .k = V_REG, .r = (uint8_t)LO(treg)(e) };

		if (bits > width) bits = width;
		uint64_t mask = (bits == 64 ? UINT64_MAX : ((uint64_t)1 << bits) - 1) << pos;

		LO(mov)(e, v.r, rv);
		if (done)
			v = LO(bin)(e, T_GREATERGREATER, 8, false, v, vconst(done));
		if (pos)
			v = LO(bin)(e, T_LESSLESS, 8, false, v, vconst(pos));
		if (bits < size * 8) {
			val old = { .k = V_REG, .r = (uint8_t)LO(treg)(e) };

			LO(load)(e, old.r, &u, size, false);
			old = LO(bin)(e, T_AMP, 8, false, old, vconst((i128)(int64_t)~mask));
			v = LO(bin)(e, T_AMP, 8, false, v, vconst((i128)(int64_t)mask));
			v = LO(bin)(e, T_PIPE, 8, false, v, old);
		}
		LO(store)(e, v.r, &u, size);
		LO(tfree)(e, v.r);
		done += bits, width -= bits, pos = 0, u.off += size;
	}
}

/* Wide integer values are held in 16-byte temporaries on x86. Extract
   each word independently so no host shift or machine shift reaches 64. */
static val wk_bf_load_wide(ecx *e, const lval *l)
{
	lval src = *l;
	lval dst = { .k = L_FRAME, .off = wk_slot(e, 16, 16), .align = 16 };
	int low = l->width < 64 ? l->width : 64;
	val lo = wk_bf_load_split(e, src, l->bitoff, low), hi;
	if (l->sgn && low < 64) {
		lo = LO(bin)(e, T_LESSLESS, 8, false, lo, vconst(64 - low));
		lo = LO(bin)(e, T_GREATERGREATER, 8, true, lo, vconst(64 - low));
	}
	LO(store)(e, lo.r, &dst, 8);
	if (l->width > 64) {
		wk_free(e, &lo);
		src.off += 8;
		hi = wk_bf_load_split(e, src, l->bitoff, l->width - 64);
		if (l->sgn && l->width < 128) {
			hi = LO(bin)(e, T_LESSLESS, 8, false, hi, vconst(128 - l->width));
			hi = LO(bin)(e, T_GREATERGREATER, 8, true, hi, vconst(128 - l->width));
		}
	} else if (l->sgn) {
		hi = LO(bin)(e, T_GREATERGREATER, 8, true, lo, vconst(63));
	} else {
		wk_free(e, &lo);
		hi = vconst(0);
		LO(toreg)(e, &hi, 8);
	}
	dst.off += 8;
	LO(store)(e, hi.r, &dst, 8);
	wk_free(e, &hi);
	dst.off -= 8;
	return wk_vreg(LO(laddr)(e, &dst));
}

/* Extract only bytes belonging to the field, then extend its actual field
 * width to the declared BitInt storage. Packed neighbours remain untouched. */
static val wk_bf_load_big(ecx *e, const lval *l, qty t)
{
	lval src = *l;
	lval dst = { .k = L_FRAME, .off = wk_slot(e, tsize(e->c, t), 8), .align = 8 };
	lval out = dst;
	val v = vconst(0);
	unsigned n = (unsigned)tsize(e->c, t)/8;
	for (unsigned i = 0; i < n; i++, out.off += 8) {
		if (i*64 < l->width) {
			unsigned bits = l->width - i*64;
			if (bits > 64) bits = 64;
			wk_free(e, &v);
			v = wk_bf_load_split(e, src, l->bitoff, (int)bits);
			if (l->sgn && bits < 64) {
				v = LO(bin)(e, T_LESSLESS, 8, false, v, vconst(64-bits));
				v = LO(bin)(e, T_GREATERGREATER, 8, true, v, vconst(64-bits));
			}
			src.off += 8;
		} else if ((i-1)*64 < l->width) {
			if (l->sgn) v = LO(bin)(e, T_GREATERGREATER, 8, true, v, vconst(63));
			else { wk_free(e, &v); v = vconst(0); }
		}
		int r = wk_reg(e, &v, prim(TY_ULONG));
		LO(store)(e, r, &out, 8);
	}
	wk_free(e, &v);
	return wk_vreg(LO(laddr)(e, &dst));
}

static void wk_bf_store_wide(ecx *e, const lval *l, int address)
{
	lval src = { .k = L_REG, .r = (uint8_t)address }, dst = *l;
	int part = LO(treg)(e);
	for (unsigned done = 0; done < l->width; done += 64) {
		unsigned bits = l->width - done;
		if (bits > 64) bits = 64;
		LO(load)(e, part, &src, 8, false);
		wk_bf_store_split(e, dst, l->bitoff, (int)bits, part);
		src.off += 8, dst.off += 8;
	}
	LO(tfree)(e, part);
}

/* a bit-field's value in a new register (l stays) */
static val wk_bf_load(ecx *e, const lval *l, qty t)
{
	if (em_bigint(e->c, t)) return wk_bf_load_big(e, l, t);
	if (wk_wide(e->c, t)) return wk_bf_load_wide(e, l);
	lval u = *l;
	int us, pos, w = l->width;
	int64_t uo;
	val v;

	if (l->dynbit_slot) {
		lval bs = { .k = L_FRAME, .off = l->dynbit_slot, .align = 8 };
		val bit = wk_vreg(LO(treg)(e));
		v = wk_vreg(LO(treg)(e));
		LO(load)(e, v.r, l, 1, false);
		LO(load)(e, bit.r, &bs, 8, false);
		v = LO(bin)(e, T_GREATERGREATER, 8, false, v, bit);
		return LO(bin)(e, T_AMP, 8, false, v, vconst(1));
	}

	wk_bf_unit(e, l, t, &uo, &us, &pos);
	u.bf = 0, u.off = uo;
	if (!us) {
		v = wk_bf_load_split(e, u, pos, w);
		pos = 0;
	} else {
		v = (val){ .k = V_REG, .r = (uint8_t)LO(treg)(e) };
		LO(load)(e, v.r, &u, us, false);
	}
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
	if (wk_wide(e->c, t) || em_bigint(e->c, t)) {
		wk_bf_store_wide(e, l, rv);
		return;
	}
	lval u = *l;
	int us, pos, w = l->width;
	int64_t uo;
	uint64_t m;
	val old, nv;

	if (l->dynbit_slot) {
		lval bs = { .k = L_FRAME, .off = l->dynbit_slot, .align = 8 };
		val bit = wk_vreg(LO(treg)(e));
		LO(load)(e, bit.r, &bs, 8, false);
		val mask = LO(bin)(e, T_LESSLESS, 8, false, vconst(1), bit);
		int rm = LO(treg)(e);
		LO(mov)(e, rm, mask.r);
		old = wk_vreg(LO(treg)(e));
		LO(load)(e, old.r, l, 1, false);
		old = LO(bin)(e, T_AMP, 8, false, old, LO(bin)(e, T_CARET, 8, false, mask, vconst(-1)));
		nv = wk_vreg(LO(treg)(e));
		LO(mov)(e, nv.r, rv);
		nv = LO(bin)(e, T_MINUS, 8, false, vconst(0), nv);
		nv = LO(bin)(e, T_AMP, 8, false, nv, wk_vreg(rm));
		old = LO(bin)(e, T_PIPE, 8, false, old, nv);
		LO(store)(e, old.r, l, 1);
		LO(tfree)(e, old.r);
		return;
	}

	wk_bf_unit(e, l, t, &uo, &us, &pos);
	u.bf = 0, u.off = uo;
	if (!us) {
		wk_bf_store_split(e, u, pos, w, rv);
		return;
	}
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

/* Atomic records travel as immutable snapshots, never as their shared address. */
static int wk_atomic_record_size(ecx *e, qty t)
{
	uint64_t size = tsize(e->c, t);

	if (size != 1 && size != 2 && size != 4 && size != 8)
		efatal(e, e->curloc, "x86-64: atomic aggregate size %llu is not supported yet", (unsigned long long)size);
	return (int)size;
}

static val wk_load(ecx *e, lval l, qty t)
{
	cc *c = e->c;
	int r;

	/* GNU void lvalues evaluate their address, but have no value to load,
	   including when qualified volatile. */
	if (KIND(c, t) == TY_VOID) {
		wk_lfree(e, &l);
		return vconst(0);
	}
	if (is_record(c, t) && (T(c, t)->flags & TF_ATOMIC)) {
		int size = wk_atomic_record_size(e, t);
		lval tmp = { .k = L_FRAME, .off = wk_slot(e, size, size), .align = (uint32_t)size };
		int rp = LO(laddr)(e, &l);
		int value = LO(atomic)(e, WK_ALOAD, size, rp, -1, -1);

		LO(tfree)(e, rp);
		LO(store)(e, value, &tmp, size);
		LO(tfree)(e, value);
		return wk_vreg(LO(laddr)(e, &tmp));
	}
	if (l.bf) {
		val v = wk_bf_load(e, &l, t);

		wk_lfree(e, &l);
		return v;
	}
	if (is_func(c, t) || wk_mem(e, t)) {
		/* the object itself: its address (arrays and functions decay; records, x87 long double) */
		if (l.k == L_SYM)
			return (val){ .k = V_ADDR, .ref = l.ref, .i = l.off };
		return wk_vreg(LO(laddr)(e, &l));
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
VPRIV void wk_store(ecx *e, const lval *l, qty t, val *v)
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

	if (em_bigint(c, from) || em_bigint(c, to))
		return LO(bigcvt)(e, v, from, to);

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
	case CK_BITS: {
		lval d = { .k = L_FRAME, .off = wk_slot(e, tsize(c, x->ty), talign(c, x->ty)), .align = talign(c, x->ty) };
		if (N(x->a)->flags & NF_LVALUE) {
			lval src = wk_lval(e, x->a);
			if (src.dynbit_slot) {
				val v = wk_reload(e, &src, from);
				int r = wk_reg(e, &v, from);
				LO(store)(e, r, &d, wk_size(c, from)); LO(tfree)(e, r);
				wk_lfree(e, &src);
			} else if (src.bf) {
				/* Extract only field bytes, including packed and wide fields.
				 * An ordinary aligned container load may exceed the object. */
				for (unsigned i = 0; i < tsize(c, from); i++) {
					unsigned bit = i * 8;
					val v = vconst(0);
					lval byte = d;
					byte.off += i;
					if (bit < src.width || src.sgn) {
						lval part = src;
						unsigned start = bit < src.width ? bit : src.width - 1;
						part.off += (src.bitoff + start) / 8;
						part.bitoff = (src.bitoff + start) % 8;
						part.width = bit >= src.width ? 1 : src.width - bit < 8 ? src.width - bit : 8;
						part.sgn = src.sgn && bit + 8 >= src.width;
						part.bf = 0;
						v = wk_bf_load_split(e, part, part.bitoff, part.width);
						if (part.sgn) {
							v = LO(bin)(e, T_LESSLESS, 8, false, v, vconst(64 - part.width));
							v = LO(bin)(e, T_GREATERGREATER, 8, true, v, vconst(64 - part.width));
						}
					}
					int r = wk_reg(e, &v, prim(TY_ULONG));
					LO(store)(e, r, &byte, 1); LO(tfree)(e, r);
				}
				wk_lfree(e, &src);

			} else {
				int rs = LO(laddr)(e, &src), rd = LO(laddr)(e, &d);
				LO(copy)(e, rd, rs, tsize(c, from));
				LO(tfree)(e, rs); LO(tfree)(e, rd);
			}
		} else {
			val v = wk_gen(e, x->a);
			if (wk_mem(e, from)) { val p = wk_putmem(e, &d, from, v); wk_free(e, &p); }
			else { int r = wk_reg(e, &v, from); LO(store)(e, r, &d, wk_size(c, from)); LO(tfree)(e, r); }
		}
		if (wk_mem(e, x->ty)) return wk_vreg(LO(laddr)(e, &d));
		return wk_load(e, d, x->ty);
	}
	case CK_VBITS: {
		/* a vector's bits as a scalar of its size */
		val v = wk_gen(e, x->a);

		return wk_load(e, wk_fromval(e, v, x->ty), x->ty);
	}
	case CK_SPLAT: {
		/* the scalar into every lane of a new vector */
		qty et = T(c, x->ty)->base;
		uint64_t es = tsize(c, et), nl = T(c, x->ty)->n;
		lval d = { .k = L_FRAME, .off = wk_slot(e, tsize(c, x->ty), 16), .align = talign(c, et) };
		val v = wk_gen(e, x->a);
		int r = wk_reg(e, &v, et);

		for (uint64_t i = 0; i < nl; i++) {
			lval dl = d;
			if (is_bvec(c, x->ty)) dl.off += i / 8, dl.bf = 1, dl.width = 1, dl.bitoff = i % 8;
			else dl.off += (int64_t)(i * es);
			wk_put(e, &dl, et, r);
		}
		LO(tfree)(e, r);
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

	if (em_bigint(c, t))
		return LO(bigbin)(e, op, t, a, b);
	if (is_flt(c, t))
			return LO(fbin)(e, op, t, a, b);
	if (wk_wide(c, t))
		return LO(wbin)(e, op, wk_signed(c, t), a, b);
	if (a.k == V_CONST && b.k == V_CONST && wk_fold(c, op, t, a.i, b.i, &r))
		return vconst(r);
	/* x & 0, x * 0, 0 & x, 0 * x: 0 (x evaluated already, its effects done: IS_DAX(i) = i->i_flags & 0) */
	if ((op == T_AMP || op == T_STAR) && ((b.k == V_CONST && b.i == 0) || (a.k == V_CONST && a.i == 0))) {
		e->discarded_scalar |= a.k == V_REG || b.k == V_REG;
		wk_free(e, &a);
		wk_free(e, &b);
		return vconst(0);
	}
	/* 0 << x, 0 >> x, -1 >> x (signed): the constant, whatever x is (as arm64) */
	if ((op == T_LESSLESS || op == T_GREATERGREATER) && a.k == V_CONST && b.k != V_CONST &&
	    (em_trunc_to(c, a.i, t) == 0 || (op == T_GREATERGREATER && wk_signed(c, t) && em_trunc_to(c, a.i, t) == -1))) {
		e->discarded_scalar |= b.k == V_REG;
		wk_free(e, &b);
		return vconst(em_trunc_to(c, a.i, t));
	}
	if (b.k == V_CONST) {
		int id = em_bin_identity(c, t, op, b.i);

		if (id == EM_ID_LEFT) {
			int size = wk_size(c, t);

			if (a.k == V_CONST)
				return wk_const(c, a.i, t);
			if (size <= 4) {
				int ra = wk_reg(e, &a, t);

				/* Keep narrow results extended; a 32-bit operation clears high bits. */
				LO(ext)(e, ra, size, size < 4 && wk_signed(c, t));
			}
			return a;
		}
		if (id == EM_ID_ZERO || id == EM_ID_ONES) {
			e->discarded_scalar |= a.k == V_REG;
			wk_free(e, &a);
			return wk_const(c, id == EM_ID_ZERO ? 0 : -1, t);
		}
		{
			em_sdivpow2_recipe recipe;

			if (em_sdivpow2(c, t, op, b.i, &recipe))
				return wk_sdivpow2(e, t, a, &recipe);
		}
		if ((op == T_SLASH || op == T_PERCENT) && e->E->o->opt && !e->E->noopt) {
			em_udivconst_recipe recipe;
			em_sdivconst_recipe signed_recipe;
			/* Bound the extra live temporaries; pressure retains native divide. */
			if (em_udivconst(c, t, op, b.i, &recipe) && wk_udivconst_ok(e, a, &recipe))
				return wk_udivconst(e, t, a, &recipe);
			if (em_sdivconst(c, t, op, b.i, &signed_recipe) && wk_sdivconst_ok(e, a, &signed_recipe))
				return wk_sdivconst(e, t, a, &signed_recipe);
		}
		em_divpow2(c, t, &op, &b.i);
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

	if (em_bigint(c, it) || wk_wide(c, it)) {
		i = wk_conv(e, i, it, prim(TY_LONG));
		it = prim(TY_LONG);
	}

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

	if (W->vgen[sn] == W->gen && W->vinst[sn] == W->inst) {
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
			/* Already captured by this declaration/typedef in this instance.
			   The original store still executes on every loop iteration. */
			if (W->vgen[a->n] == W->gen && W->vinst[a->n] == W->inst) {
				t = a->base;
				continue;
			}
			val v = wk_conv(e, wk_gen(e, a->n), N(a->n)->ty, c->t_size);
			int r = wk_reg(e, &v, c->t_size);
			lval l = { .k = L_FRAME, .off = wk_pslot(e, 8, 8), .align = 8 };	/* (sizeof reads it later) */

			LO(store)(e, r, &l, 8);
			LO(tfree)(e, r);
			W->vgen[a->n] = W->gen;
			W->vinst[a->n] = W->inst;
			W->voff[a->n] = (int64_t)l.off;
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
	if (em_bigint(c, it) || wk_wide(c, it)) {
		i = wk_conv(e, i, it, prim(TY_LONG));
		it = prim(TY_LONG);
	}
	int ri = wk_reg(e, &i, it);

	if (wk_size(c, it) < 8)
		LO(ext)(e, ri, wk_size(c, it), wk_signed(c, it));
	i = wk_arith(e, T_STAR, prim(TY_ULONG), wk_vreg(ri), wk_vla_bytes(e, T(c, pt)->base));
	return wk_arith(e, sub ? T_MINUS : T_PLUS, prim(TY_ULONG), p, i);
}

VPRIV void wk_vla_decl(ecx *e, uint32_t s, bool zero)
{
	cc *c = e->c;
	int64_t off = wk_local(e, s);
	lval la = { .k = L_FRAME, .off = off, .align = 8 }, lsp = la, ln = la;
	val n = wk_vla_bytes(e, c->syms[s].ty), p;
	int r;

	uint32_t align = sym_align(c, s);
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
		wk_label(e, fresh);
	}
	{
		int t = LO(spget)(e);

		LO(store)(e, t, &lsp, 8);
		LO(tfree)(e, t);
	}
	/* Leave enough padding to round the returned pointer up independently
	   of the stack pointer; the saved stack pointer still releases it all. */
	if (align > 16) {
		n = wk_arith(e, T_PLUS, prim(TY_ULONG), wk_vreg(r), vconst(align - 16));
		r = wk_reg(e, &n, prim(TY_ULONG));
	}
	e->wk->spneed |= e->wk->canary != 0;	/* (alloca'd memory below the frame) */
	p = LO(alloca)(e, wk_vreg(r));
	if (align > 16) {
		p = wk_arith(e, T_PLUS, prim(TY_ULONG), p, vconst(align - 1));
		p = wk_arith(e, T_AMP, prim(TY_ULONG), p, vconst(-(int64_t)align));
		wk_reg(e, &p, prim(TY_ULONG));
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
		wk_label(e, top);
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
		wk_label(e, done);
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
		int64_t sl = wk_slot(e, 8, 8);
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

		if (is_bvec(c, t)) l.off = i / 8, l.bf = 1, l.width = 1, l.bitoff = i % 8;
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
	unsigned nl = T(c, vt)->n;
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

			if (is_bvec(c, rt)) {
				l.off += i / 8;
				l.bf = 1, l.width = 1, l.bitoff = i % 8;
				int rr = wk_reg(e, &r, re);
				wk_put(e, &l, re, rr);
				LO(tfree)(e, rr);
				continue;
			}
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
		if (em_bigint(c, lt))
			return LO(bigcmp)(e, op, lt, a, b);
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
	/* Wide integer values are addresses; shift helpers need a scalar count. */
	if ((op == T_LESSLESS || op == T_GREATERGREATER) && (wk_wide(c, rt) || em_bigint(c, rt)))
		b = wk_conv(e, b, rt, prim(TY_INT));
	return wk_arith(e, op, x->ty, a, b);
}

/* a value 0 or 1 from a condition */
static val wk_condval(ecx *e, uint32_t n)
{
	int64_t sl = wk_slot(e, 4, 4);
	lval l = { .k = L_FRAME, .off = sl, .align = 4 };
	uint32_t lf = wk_newlabel(e), lj = wk_newlabel(e);
	val one = vconst(1), zero = vconst(0);
	int r;

	e->wk->cjump = false;
	wk_cond(e, n, false, lf);
	if (e->wk->cjump) {
		/* false whatever it evaluated (x && 0, the operands' effects done): the constant, so an
		   inlined function returning it (dax_mapping) folds its callers' tests */
		e->wk->cjump = false;
		LO(label)(e, lf);
		return vconst(0);
	}
	wk_store(e, &l, prim(TY_INT), &one);
	LO(jump)(e, lj);
	wk_label(e, lf);
	wk_store(e, &l, prim(TY_INT), &zero);
	wk_label(e, lj);
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
	int64_t sl = vd ? 0 : wk_slot(e, 8, 8);
	lval l = { .k = L_FRAME, .off = sl, .align = 8 };
	uint32_t lf = wk_newlabel(e), lj = wk_newlabel(e);
	val v;
	int r;

	if (x->b && wk_isconst(e, x->a) &&
	    !wk_branch_entry(c, x->b) && !wk_branch_entry(c, x->c)) {
		val k = wk_gen(e, x->a);
		if (k.k == V_CONST)
			return wk_gen(e, k.i ? x->b : x->c);
		wk_free(e, &k);
	}
	if (!vd)
		wk_scalar(e, n, t);
	if (!x->b) {
		val a = wk_gen(e, x->a);
		int ra = wk_reg(e, &a, N(x->a)->ty);

		LO(store)(e, ra, &l, 8);
		a = wk_conv(e, a, N(x->a)->ty, prim(TY_BOOL));
		LO(brz)(e, a, 4, false, lf);
		LO(jump)(e, lj);
	} else {
		int k = -1;

		/* a condition known here (after inlining, or from its form): one arm only, as
		   clang -O2 (an error() call or a link_error in the other is gone) */
		if (!(e->E->nflags[x->b] & NA_LABEL) && !(e->E->nflags[x->c] & NA_LABEL)) {
			cval kc;

			k = cond_fold(e, x->a);
			if (k < 0 && cc_eval(c, x->a, &kc) && kc.kind == CV_INT)
				k = kc.i != 0;	/* (sizeof(t) == sizeof(t[1]) && ...: the kernel's _IOC_TYPECHECK) */
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
	wk_label(e, lf);
	v = wk_gen(e, x->c);
	if (vd) {
		wk_free(e, &v);
	} else {
		r = wk_reg(e, &v, t);
		LO(store)(e, r, &l, 8);
		LO(tfree)(e, r);
	}
	wk_label(e, lj);
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
	int rs = wk_reg(e, &v, prim(TY_ULONG));
	if (l->bf) {
		wk_bf_store(e, l, t, rs);
		LO(tfree)(e, rs);
		val result = wk_bf_load(e, l, t);
		wk_lfree(e, l);
		return result;
	}
	int rd = LO(laddr)(e, l);

	if (is_record(e->c, t) && (T(e->c, t)->flags & TF_ATOMIC)) {
		int size = wk_atomic_record_size(e, t);
		uint64_t payload = tsize(e->c, QT(T(e->c, t)->n, 0));
		lval tmp = { .k = L_FRAME, .off = wk_slot(e, size, size), .align = (uint32_t)size };
		int snapshot = LO(laddr)(e, &tmp);
		int value;

		/* Padding belongs to the atomic representation, not the RHS object. */
		LO(zero)(e, snapshot, size);
		LO(copy)(e, snapshot, rs, payload);
		LO(tfree)(e, rs);
		value = LO(treg)(e);
		LO(load)(e, value, &tmp, size, false);
		LO(atomic)(e, WK_ASTORE, size, rd, value, -1);
		LO(tfree)(e, value);
		LO(tfree)(e, rd);
		/* Assignment yields the stored value even if another writer changes l. */
		return wk_vreg(snapshot);
	}
	LO(copy)(e, rd, rs, tsize(e->c, t));
	LO(tfree)(e, rs);
	return wk_vreg(rd);
}

/* the type l op r is computed in, for l of type lt op= r of type rt */
static qty wk_optype(cc *c, int op, qty lt, qty rt)
{
	qty pl = wk_size(c, lt) >= 4 ? lt : prim(TY_INT);

	if (em_bigint(c, lt) || em_bigint(c, rt)) {
		if (op == T_LESSLESS || op == T_GREATERGREATER) return lt;
		unsigned lb = em_bigint(c, lt) ? T(c, lt)->n : (unsigned)tsize(c, lt)*8;
		unsigned rb = em_bigint(c, rt) ? T(c, rt)->n : (unsigned)tsize(c, rt)*8;
		return rb > lb || (rb == lb && !wk_signed(c, rt)) ? rt : lt;
	}

	if (is_flt(c, lt) || is_flt(c, rt))
		return !is_flt(c, lt) || (is_flt(c, rt) && KIND(c, rt) > KIND(c, lt)) ? rt : lt;
	if (op == T_LESSLESS || op == T_GREATERGREATER)
		return pl;
	if (wk_size(c, rt) > wk_size(c, pl) || (wk_size(c, rt) == wk_size(c, pl) && !wk_signed(c, rt)))
		return rt;
	return pl;
}

/* Compound assignments reload their lvalue directly instead of through
 * wk_gen. Normalize that snapshot before arithmetic sees ABI padding. */
static val wk_bitint_value(ecx *e, val v, qty t)
{
	cc *c = e->c;
	{
		unsigned bits = T(c, t)->n;

		if (bits > 64)
			return LO(wbitint)(e, v, bits, is_signed(c, t));
		if (v.k == V_CONST)
			return wk_const(c, v.i, t);
		if (bits < 64) {
			v = LO(bin)(e, T_LESSLESS, 8, false, v, vconst(64 - bits));
			v = LO(bin)(e, T_GREATERGREATER, 8, is_signed(c, t), v, vconst(64 - bits));
		}
	}
	return v;
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
		if ((x->aux == T_LESSLESS || x->aux == T_GREATERGREATER) && (wk_wide(c, rt) || em_bigint(c, rt))) {
			b = wk_conv(e, b, rt, prim(TY_INT));
			rt = prim(TY_INT);
		}
		if (b.k == V_REG && wk_short(e, rt)) {
			int64_t sl = wk_slot(e, 8, 8);
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
		if (em_bitint(c, lt) && !em_bigint(c, lt))
			a = wk_bitint_value(e, a, lt);
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
	if (e->E->o->opt && tsize(c, lt) <= 8) {
		int k = ael_slot(e, x->a, true);
		if (k >= 0) {
			e->kvalid &= (uint16_t)~(1u << k);
			if (v.k == V_CONST) {
				e->kval[k] = (int64_t)em_trunc_to(c, v.i, lt);
				e->kvalid |= (uint16_t)(1u << k);
			}
		}
	}
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
	wk_label(e, top);
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
		wk_label(e, done);
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
		nv = wk_incval(e, t, dec, wk_reload(e, &l, t));
	} else {
		nv = wk_reload(e, &l, t);
		if (post) {
			/* One lvalue read supplies both results. The arithmetic consumes
			 * its register, so keep the old value and increment a typed copy.
			 */
			old = nv;
			int copy = wk_treg(e, t);

			LO(mov)(e, copy, wk_reg(e, &nv, t));
			nv = wk_vreg(copy);
		}
		nv = wk_incval(e, t, dec, nv);
	}
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
	if (x->aux == T_EXCLAIM) {
		/* !call() with the call folded or inlined to a constant (!__builtin_strcmp(op, "<")):
		   the constant; else as a condition */
		uint32_t tc = x->a;

		while (N(tc)->op == N_CAST && N(tc)->a)
			tc = N(tc)->a;
		if ((N(tc)->op == N_CALL || (!side_effects(c, x->a) && wk_isconst(e, x->a)) || N(tc)->op == N_UNARY) &&
		    !is_flt(c, N(x->a)->ty) && !wk_mem(e, N(x->a)->ty)) {
			val v = wk_gen(e, x->a);

			if (v.k == V_CONST)
				return vconst(v.i == 0);
			if (v.k == V_ADDR)
				return wk_free(e, &v), vconst(0);
			v = wk_conv(e, v, N(x->a)->ty, prim(TY_BOOL));
			return wk_arith(e, T_CARET, prim(TY_INT), wk_conv(e, v, prim(TY_BOOL), prim(TY_INT)), vconst(1));
		}
		return wk_condval(e, n);
	}
	wk_scalar(e, n, x->ty);
	a = wk_gen(e, x->a);
	if (x->aux == T_PLUS)
		return a;
	if (is_flt(c, x->ty))
		return LO(fneg)(e, x->ty, a);
	if (em_bigint(c, x->ty))
		return LO(bigbin)(e, x->aux == T_MINUS ? T_MINUS : T_CARET, x->ty,
			LO(bigconst)(e, x->ty, NULL, x->aux == T_MINUS ? 0 : -1), a);
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
	cc *c = e->c;
	const node *x = N(n), *a;
	uint32_t id = c->syms[s].name;

	if (x->c != 1 || c->syms[s].sc == SC_STATIC || KIND(c, N(n)->ty) != TY_DOUBLE || !e->E->o->opt ||
	    sym_attrs(c, s)->asmname || e->E->o->no_builtin)
		return 0;	/* (an asm-renamed floor is not the library's) */
	a = N(c->lists[x->b]);
	if (a->op != N_CAST || KIND(c, a->ty) != TY_DOUBLE || KIND(c, N(a->a)->ty) != TY_FLOAT)
		return 0;
	return EM_IDENT_IS(id, "floor") ? FN_FLOORF + 1 :
	       EM_IDENT_IS(id, "ceil") ? FN_FLOORF + 2 :
	       EM_IDENT_IS(id, "trunc") ? FN_FLOORF + 3 :
	       EM_IDENT_IS(id, "round") ? FN_FLOORF + 4 :
	       EM_IDENT_IS(id, "rint") ? FN_FLOORF + 5 :
	       EM_IDENT_IS(id, "nearbyint") ? FN_FLOORF + 6 :
	       EM_IDENT_IS(id, "fabs") ? FN_FLOORF + 7 : 0;
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
	wkarg argbuf[64], *args = argbuf, r = { 0 };
	bool var = (f->flags & (TF_VARIADIC | TF_NOPROTO)) != 0, rec_ = wk_mem(e, f->base);

	/* Keep common calls on the stack; large calls scale with their arity. */
	if (x->c > sizeof argbuf / sizeof *argbuf &&
	    !(args = malloc((size_t)x->c * sizeof *args)))
		efatal(e, x->loc, "out of memory");
	if (KIND(c, f->base) != TY_VOID)
		wk_scalar(e, n, f->base);
	if (fx->op == N_CAST && fx->aux == CK_FDECAY && N(fx->a)->op == N_VAR) {
		uint32_t s = canon_sym(c, N(fx->a)->a);

		if (c->syms[s].kind == S_BUILTIN) {
			int kind = cc_builtin_libkind(c->syms[s].val);
			cval cv;

			if (x->c == 2 && kind == CC_LIB_STRCMP) {
				/* two string literals: folded (the kernel's vruntime_cmp(a, "<", b)) */
				uint32_t sa = c->lists[x->b], sb = c->lists[x->b + 1];

				while (N(sa)->op == N_CAST)
					sa = N(sa)->a;
				while (N(sb)->op == N_CAST)
					sb = N(sb)->a;
				if (N(sa)->op == N_STR && N(sb)->op == N_STR && c->strs[N(sa)->a].esz == 1 && c->strs[N(sb)->a].esz == 1) {
					int r = strcmp(c->strs[N(sa)->a].s, c->strs[N(sb)->a].s);

					return vconst(r < 0 ? -1 : r > 0);
				}
			}
			/* memcpy/memmove/memset of a constant size up to 64 bytes: inline, as clang (8 stores
			   of 8 bytes; a freestanding vDSO has no memcpy) */
			if (x->c == 3 && (kind >= CC_LIB_MEMCPY && kind <= CC_LIB_MEMSET) &&
			    cc_eval(c, c->lists[x->b + 2], &cv) && cv.kind == CV_INT && cv.i >= 0 && cv.i <= 64 &&
			    (kind != CC_LIB_MEMSET || (cc_eval(c, c->lists[x->b + 1], &cv) && cv.kind == CV_INT && cv.i == 0))) {
				uint64_t len;
				val d = wk_gen(e, c->lists[x->b]), v2;
				int rd = wk_reg(e, &d, prim(TY_ULONG));

				cc_eval(c, c->lists[x->b + 2], &cv);
				len = (uint64_t)cv.i;
				if (kind == CC_LIB_MEMSET) {
					v2 = wk_gen(e, c->lists[x->b + 1]);
					wk_free(e, &v2);
					if (len)
						LO(zero)(e, rd, len);
				} else {
					val sv = wk_gen(e, c->lists[x->b + 1]);
					int rs = wk_reg(e, &sv, prim(TY_ULONG));

					if (len && kind == CC_LIB_MEMMOVE) {	/* memmove: through a slot (the two may overlap) */
						lval t = { .k = L_FRAME, .off = wk_slot(e, len, 8), .align = 8 };
						int rt = LO(laddr)(e, &t);

						LO(copy)(e, rt, rs, len);
						LO(copy)(e, rd, rt, len);
						LO(tfree)(e, rt);
					} else if (len) {
						LO(copy)(e, rd, rs, len);
					}
					LO(tfree)(e, rs);
				}
				return wk_vreg(rd);	/* (the destination, as the call returns) */
			}
			ref = REF(RK_NAME, (uint32_t)c->syms[s].val);	/* a library function by name */
		} else if (wk_can_inline(e, s)) {
			if (args != argbuf) free(args);
			return wk_inline(e, n, s);
		} else if (unlikely(sym_attrs(c, s)->error_fn) && e->reach) {
			/* a call to an error() function folding did not remove (__compiletime_assert_N,
			   __bad_copy_to): clang -O2 removed it with value ranges vcc lacks.  The kernel's
			   own -O0 fallback drops it (arm64's call.c does the same); elsewhere an error */
			if (!e->E->o->vcc_kernel) {
				const vident *nm = &vintern_ents[c->syms[s].name];
				const attrs *at = sym_attrs(c, s);

				efatal(e, N(x->a)->loc, "call to '%.*s' declared with 'error' attribute: %s", (int)nm->len, nm->s,
				       at->errmsg ? c->strs[at->errmsg - 1].s : "");
			}
			for (uint32_t i = 0; i < x->c; i++) {	/* (the arguments' side effects) */
				val v = wk_gen(e, c->lists[x->b + i]);

				wk_free(e, &v);
			}
			return KIND(c, f->base) == TY_VOID ? vconst(0) : vconst(0);
		} else if (wk_shrink(e, n, s)) {
			int k = wk_shrink(e, n, s) - 1;
			uint32_t an = N(c->lists[x->b])->a;	/* the float */
			wkarg fa;
			val v;

			LO(classify)(e, N(an)->ty, &fa, false);
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
		LO(classify)(e, at, &args[i], (f->flags & TF_VARIADIC) && i >= f->n);
		wk_argslot(e, &args[i], at, wk_gen(e, an));
	}
	if (rec_) {
		LO(classify)(e, f->base, &r, false);
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

		if (args != argbuf)
			free(args);
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
	return v;
}

/* ---------------- builtins ---------------- */

/* n is a constant once the bound constants are known (no loads, calls or side effects) */
VPRIV bool wk_isconst(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	val k;

	if (!is_integer(c, x->ty))
		return false;

	switch (x->op) {
	case N_NUM: case N_BIGNUM:
		return true;
	case N_BUILTIN:
		return x->aux == BI_CONSTANT_P;
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
		wk_label(e, done);
		LO(tfree)(e, old);
		LO(tfree)(e, ex);
		LO(tfree)(e, r1);
		LO(tfree)(e, r2);
		r = ok.r;
		sg = false;
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

		wk_label(e, top);
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
			wk_label(e, keep);
		}
		got = LO(atomic)(e, WK_ACAS, sz, rp, nw, old);
		LO(cmpbr)(e, T_EQUALEQUAL, 8, false, wk_vreg(got), wk_vreg(old), done);
		LO(tkeep)(e, got);
		LO(tkeep)(e, old);
		LO(mov)(e, old, got);
		LO(tfree)(e, got);
		LO(tfree)(e, nw);
		LO(jump)(e, top);
		wk_label(e, done);
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
	if (sz < 8)
		LO(ext)(e, r, sz, sg);
	return wk_vreg(r);
}

/* a float value parked in the frame slot sl (16 bytes): by address, its bytes; else stored */
static val wk_park(ecx *e, val v, qty t, int64_t sl)
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

/*
 * The class of the float parked at slot sl as a 64-bit pattern without its
 * sign: the exponent's eb bits at the top, a nonzero mantissa nonzero below
 * (x87: the explicit integer bit dropped; __float128: its low word as a
 * sticky bit).
 */
static val wk_fpattern(ecx *e, qty t, int64_t sl, int *eb)
{
	cc *c = e->c;
	lval l = { .k = L_FRAME, .off = sl, .align = 16 }, h = l;
	int k = KIND(c, t), r = LO(treg)(e);
	qty u = prim(TY_ULONG);

	h.off += 8;
	if (k == TY_FLOAT16 || k == TY_BFLOAT16) {
		LO(load)(e, r, &l, 2, false);
		*eb = k == TY_BFLOAT16 ? 8 : 5;
		return wk_arith(e, T_LESSLESS, u, wk_vreg(r), vconst(49));
	}
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
static val wk_fptest(ecx *e, int bi, qty t, int64_t sl)
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
	int64_t sl = wk_slot(e, 16, 16);
	val v;

	if (!is_flt(c, t))
		wk_unsup(e, n, "a floating-point class builtin on an integer");
	if (x->aux == BI_FPCLASSIFY) {
		/* fpclassify(nan, inf, normal, subnormal, zero, x): the five values evaluated first */
		int64_t rs = wk_slot(e, 4 * 5, 4);
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
			wk_label(e, skip);
		}
		wk_label(e, done);
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

		l.off += k == TY_FLOAT16 || k == TY_BFLOAT16 ? 1 : k == TY_FLOAT ? 3 : k == TY_DOUBLE ? 7 : k == TY_LDOUBLE ? 9 : 15;
		r = LO(treg)(e);
		LO(load)(e, r, &l, 1, false);
		sg = LO(bin)(e, T_GREATERGREATER, 4, false, wk_vreg(r), vconst(7));
		sg = LO(bin)(e, T_STAR, 4, false, sg, vconst(-2));
		sg = LO(bin)(e, T_PLUS, 4, false, sg, vconst(1));	/* 1 or -1 */
		return LO(bin)(e, T_STAR, 4, false, sg, inf);
	}
	return wk_fptest(e, x->aux, t, sl);
}

static val wk_fpconst(ecx *e, qty t, double d, u128 q)
{
	cc *c = e->c;

	if (wk_mem(e, t)) {
		/* a long double constant: its bytes stored to a new object */
		uint8_t b[10];
		uint64_t lo, hi;
		lval l = { .k = L_FRAME, .off = wk_slot(e, 16, 16), .align = 16 }, h;
		val v;
		int r;

		if (KIND(c, t) == TY_FLOAT128) {
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
		float f = (float)d;
		uint64_t bits = 0;
		if (q_isnan(q)) {
			int sz = wk_size(c, t);
			return LO(fconst)(e, q_nan_storage(q, KIND(c, t) == TY_BFLOAT16 ? 7 : sz == 2 ? 10 : sz == 4 ? 23 : 52, KIND(c, t) == TY_BFLOAT16 ? 8 : sz == 2 ? 5 : sz == 4 ? 8 : 11), sz);
		}

		if (KIND(c, t) == TY_BFLOAT16)
			return LO(fconst)(e, q_to_bf(q_of_d(d)), 2);
		if (KIND(c, t) == TY_FLOAT16)
			return LO(fconst)(e, q_to_h(q_of_d(d)), 2);
		if (KIND(c, t) == TY_FLOAT)
			memcpy(&bits, &f, 4);
		else
			memcpy(&bits, &d, 8);
		return LO(fconst)(e, bits, KIND(c, t) == TY_FLOAT ? 4 : 8);
	}
}

/* Wide overflow arithmetic uses an exact 256-bit two's-complement value.
 * Base-2^32 limbs keep every multiply/add/carry inside an unsigned 64-bit
 * operation, including unsigned 128-bit multiplication and mixed signs. */
static val wk_ovload(ecx *e, int64_t slot, int limb)
{
	lval l = { .k = L_FRAME, .off = slot + limb * 4, .align = 4 };
	int r = LO(treg)(e);
	LO(load)(e, r, &l, 4, false);
	return wk_vreg(r);
}

static void wk_ovstore(ecx *e, int64_t slot, int limb, val v)
{
	lval l = { .k = L_FRAME, .off = slot + limb * 4, .align = 4 };
	int r = wk_reg(e, &v, prim(TY_ULONG));
	LO(store)(e, r, &l, 4);
	LO(tfree)(e, r);
}

static int64_t wk_ovextend(ecx *e, val v, qty t)
{
	int64_t slot = wk_slot(e, 32, 16);
	lval lo = { .k = L_FRAME, .off = slot, .align = 16 };
	val wide = wk_conv(e, v, t, prim(TY_INT128));
	wide = wk_putmem(e, &lo, prim(TY_INT128), wide);
	wk_free(e, &wide);
	for (int i = 4; i < 8; i++) {
		val sign = wk_signed(e->c, t)
			? LO(bin)(e, T_GREATERGREATER, 4, true, wk_ovload(e, slot, 3), vconst(31)) : vconst(0);
		wk_ovstore(e, slot, i, sign);
	}
	return slot;
}

static val wk_overflow_wide(ecx *e, const uint32_t *arg, int op)
{
	cc *c = e->c;
	qty ta = N(arg[0])->ty, tb = N(arg[1])->ty, tr = T(c, N(arg[2])->ty)->base;
	int64_t as = wk_ovextend(e, wk_gen(e, arg[0]), ta);
	int64_t bs = wk_ovextend(e, wk_gen(e, arg[1]), tb);
	int64_t rs = wk_slot(e, 32, 16), carry = wk_slot(e, 4, 4);
	lval dst = wk_fromval(e, wk_gen(e, arg[2]), tr);
	for (int i = 0; i < 8; i++) wk_ovstore(e, rs, i, vconst(0));
	if (op == T_STAR) {
		for (int i = 0; i < 8; i++) {
			wk_ovstore(e, carry, 0, vconst(0));
			for (int j = 0; j < 8 - i; j++) {
				val v = LO(bin)(e, T_STAR, 8, false, wk_ovload(e, as, i), wk_ovload(e, bs, j));
				v = LO(bin)(e, T_PLUS, 8, false, v, wk_ovload(e, rs, i + j));
				v = LO(bin)(e, T_PLUS, 8, false, v, wk_ovload(e, carry, 0));
				int r = wk_reg(e, &v, prim(TY_ULONG));
				wk_ovstore(e, rs, i + j, wk_vreg(wk_dup(e, r)));
				wk_ovstore(e, carry, 0, LO(bin)(e, T_GREATERGREATER, 8, false, v, vconst(32)));
			}
		}
	} else {
		wk_ovstore(e, carry, 0, vconst(op == T_MINUS));
		for (int i = 0; i < 8; i++) {
			val b = wk_ovload(e, bs, i);
			if (op == T_MINUS) b = LO(bin)(e, T_CARET, 8, false, b, vconst(0xffffffffu));
			val v = LO(bin)(e, T_PLUS, 8, false, wk_ovload(e, as, i), b);
			v = LO(bin)(e, T_PLUS, 8, false, v, wk_ovload(e, carry, 0));
			int r = wk_reg(e, &v, prim(TY_ULONG));
			wk_ovstore(e, rs, i, wk_vreg(wk_dup(e, r)));
			wk_ovstore(e, carry, 0, LO(bin)(e, T_GREATERGREATER, 8, false, v, vconst(32)));
		}
	}
	lval result = { .k = L_FRAME, .off = rs, .align = 16 };
	val v = wk_conv(e, wk_vreg(LO(laddr)(e, &result)), prim(TY_INT128), tr);
	if (em_bitint(c, tr)) {
		unsigned bits = T(c, tr)->n;
		if (bits > 64 && bits < 128) v = LO(wbitint)(e, v, bits, wk_signed(c, tr));
		else if (bits < 64) {
			v = LO(bin)(e, T_LESSLESS, 8, false, v, vconst(64 - bits));
			v = LO(bin)(e, T_GREATERGREATER, 8, wk_signed(c, tr), v, vconst(64 - bits));
		}
	}
	/* Compare the exact value with the destination conversion, sign-extended
	 * according to its own type.  Snapshot it before writing an aliased output. */
	int64_t back = wk_ovextend(e, v, tr);
	lval converted = { .k = L_FRAME, .off = back, .align = 16 };
	v = wk_conv(e, wk_vreg(LO(laddr)(e, &converted)), prim(TY_INT128), tr);
	if (wk_size(c, tr) > 8) {
		v = wk_putmem(e, &dst, tr, v);
		wk_free(e, &v);
	} else {
		int r = wk_reg(e, &v, tr);
		LO(store)(e, r, &dst, wk_size(c, tr));
		LO(tfree)(e, r);
		wk_lfree(e, &dst);
	}
	val different = vconst(0);
	for (int i = 0; i < 8; i++) {
		val d = LO(bin)(e, T_CARET, 4, false, wk_ovload(e, rs, i), wk_ovload(e, back, i));
		different = LO(bin)(e, T_PIPE, 4, false, different, d);
	}
	return LO(cmp)(e, T_EXCLAIMEQ, 4, false, different, vconst(0));
}

/* The same non-emitting constant_p policy serves values and control proofs. */
static int wk_constant_p(ecx *e, uint32_t arg)
{
	cc *c = e->c;
	cval cv;

	if (cc_eval(c, arg, &cv) && (cv.kind == CV_INT || cv.kind == CV_FLT ||
	    (cv.kind == CV_ADDR && cv.str && cv.i == 0 && !cv.sym && !cv.label)))
		return 1;
	return !side_effects(c, arg) && wk_isconst(e, arg);
}

static val wk_builtin(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	const uint32_t *arg = &c->lists[x->b];
	char b[64];

	switch (x->aux) {
	case BI_X86_PCMPSTR: case BI_X86_BLENDVB: return LO(pcmpstr)(e, n);
	case BI_FAST_SQRT:
		return LO(fsqrt)(e, x->ty, wk_gen(e, arg[0]));
	case BI_FAST_ROOT4:
		return LO(root4)(e, wk_gen(e, arg[0]));
	case BI_CONSTANT_P:
		return vconst(wk_constant_p(e, arg[0]));
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
	case BI_SETJMP: case BI_LONGJMP: {
		val p = wk_gen(e, arg[0]);
		int r = wk_reg(e, &p, prim(TY_ULONG));

		if (x->aux == BI_SETJMP) {
			return LO(setjmp)(e, r);
		}
		{
			val k = wk_gen(e, arg[1]);	/* (GCC requires 1) */

			wk_free(e, &k);
		}
		LO(longjmp)(e, r);
		return vconst(0);
	}
	case BI_RETURN_ADDRESS: case BI_FRAME_ADDRESS: {
		val v = wk_gen(e, arg[0]);

		if (v.k != V_CONST)
			wk_unsup(e, n, "a return or frame address of a level not a constant");
		return LO(frame)(e, x->aux == BI_RETURN_ADDRESS, (uint32_t)v.i);
	}
	case BI_OBJECT_SIZE: case BI_DYNAMIC_OBJECT_SIZE: {
		cval cv;
		i128 size;
		wkinl *inl = e->wk->inl;
		if (inl && inline_object_size(e, inl->fn, inl->call, arg[0], arg[1], &size))
			return vconst(size);
		if (cc_eval(c, n, &cv) && cv.kind == CV_INT)
			return vconst(cv.i);
		return vconst((i128)(uint64_t)-1);
	}
	case BI_SHUFFLEVECTOR: case BI_CONVERTVECTOR: {
		/* lane by lane through a frame object (vectors are addresses) */
		qty at = N(arg[0])->ty, ae = T(c, at)->base, re = T(c, x->ty)->base;
		int64_t na = T(c, at)->ext ?: T(c, at)->n, nr = T(c, x->ty)->ext ?: T(c, x->ty)->n;
		bool sh = x->aux == BI_SHUFFLEVECTOR;
		val a = wk_gen(e, arg[0]), bv = sh ? wk_rhs(e, &a, arg[1], prim(TY_ULONG)) : vconst(0);
		int ra = wk_reg(e, &a, prim(TY_ULONG)), rb = sh ? wk_reg(e, &bv, prim(TY_ULONG)) : -1;
		lval d = { .k = L_FRAME, .off = wk_slot(e, tsize(c, x->ty), 16), .align = talign(c, re) };

		for (int64_t i = 0; i < nr; i++) {
			lval dl = d, sl = { .k = L_REG, .align = talign(c, ae) };
			val v;
			int r;

			if (is_bvec(c, x->ty)) dl.off += i / 8, dl.bf = 1, dl.width = 1, dl.bitoff = i % 8;
			else dl.off += i * (int64_t)tsize(c, re);
			if (sh) {
				int64_t k = (int64_t)c->ipool[N(arg[2 + i])->a];

				if (k < 0)
					continue;	/* an undefined lane */
				sl.r = (uint8_t)(k < na ? ra : rb);
				sl.off = (k < na ? k : k - na) * (int64_t)tsize(c, ae);
				if (is_bvec(c, at)) { int64_t j = k < na ? k : k - na; sl.off = j / 8, sl.bf = 1, sl.width = 1, sl.bitoff = j % 8; }
				v = wk_reload(e, &sl, ae);
			} else {
				sl.r = (uint8_t)ra;
				sl.off = i * (int64_t)tsize(c, ae);
				if (is_bvec(c, at)) sl.off = i / 8, sl.bf = 1, sl.width = 1, sl.bitoff = i % 8;
				v = wk_conv(e, wk_reload(e, &sl, ae), ae, re);
			}
			r = wk_reg(e, &v, re);
			wk_put(e, &dl, re, r);
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
	case BI_ATOMIC_FETCH_NAND: case BI_ATOMIC_FETCH_MAX: case BI_ATOMIC_FETCH_MIN:
	case BI_ATOMIC_NAND_FETCH: case BI_ATOMIC_MAX_FETCH: case BI_ATOMIC_MIN_FETCH:
		return wk_atomic(e, n);
	case BI_ATOMIC_THREAD_FENCE: case BI_ATOMIC_SIGNAL_FENCE: {
		val m = wk_gen(e, arg[0]);

		wk_free(e, &m);
		if (x->aux == BI_ATOMIC_THREAD_FENCE)
			LO(atomic)(e, WK_AFENCE, 8, -1, -1, -1);
		return vconst(0);
	}
	case BI_ATOMIC_LOCK_FREE: {
		for (uint32_t i = 0; i < x->c; i++) {
			val a = wk_gen(e, arg[i]);
			wk_free(e, &a);
		}
		return vconst(1);
	}
	case BI_ALLOCA: case BI_ALLOCA_UNINIT: {
		val v = wk_gen(e, arg[0]);

		e->dyn = true;
		val p = LO(alloca)(e, wk_conv(e, v, N(arg[0])->ty, prim(TY_ULONG)));
		/* Preserve this allocation across every currently active VLA scope. */
		if (e->wk->nvla) {
			int sp = LO(spget)(e);

			for (int i = e->wk->inl ? e->wk->inl->vbase : 0; i < e->wk->nvla; ++i) {
				lval saved = { .k = L_FRAME, .off = wk_local(e, e->wk->vla[i]) + 8, .align = 8 };
				LO(store)(e, sp, &saved, 8);
			}
			LO(tfree)(e, sp);
		}
		return p;
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
	case BI_ADD_OVERFLOW: case BI_SUB_OVERFLOW: case BI_MUL_OVERFLOW: {
		if (LO(i128mem)() && (wk_size(c, N(arg[0])->ty) > 8 || wk_size(c, N(arg[1])->ty) > 8 || (em_bitint(c, T(c, N(arg[2])->ty)->base) && wk_size(c, T(c, N(arg[2])->ty)->base) > 8)))
			return wk_overflow_wide(e, arg, x->aux == BI_ADD_OVERFLOW ? T_PLUS : x->aux == BI_SUB_OVERFLOW ? T_MINUS : T_STAR);
		if (wk_size(c, N(arg[0])->ty) <= 8 && wk_size(c, N(arg[1])->ty) <= 8 && (KIND(c, T(c, N(arg[2])->ty)->base) == TY_INT128 || KIND(c, T(c, N(arg[2])->ty)->base) == TY_UINT128) &&
		    !em_bitint(c, T(c, N(arg[2])->ty)->base) && LO(i128mem)()) {
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
		if (em_bitint(c, tr) && T(c, tr)->n < 64) {
			unsigned shift = 64 - T(c, tr)->n;

			lo = LO(bin)(e, T_LESSLESS, 8, false, lo, vconst(shift));
			lo = LO(bin)(e, T_GREATERGREATER, 8, is_signed(c, tr), lo, vconst(shift));
		}
		rs = wk_reg(e, &lo, tr);
		LO(store)(e, rs, &l, wk_size(c, tr));
		wk_lfree(e, &l);
		back = wk_conv(e, lo, tr, w);
		return LO(wcmp)(e, T_EXCLAIMEQ, true, (val){ .k = V_REG, .r = (uint8_t)rr }, back);
	}
	case BI_ISINF_SIGN: case BI_FPCLASSIFY:
		return wk_fpclass(e, n);
	case BI_ISNAN: case BI_ISINF: case BI_ISFINITE: case BI_ISNORMAL: case BI_ISSIGNALING: {
		qty at = N(arg[0])->ty;
		if (KIND(c, at) == TY_FLOAT128 && x->aux != BI_ISSIGNALING)
			return wk_fpclass(e, n);
		val v = wk_gen(e, arg[0]);
		int sz = wk_size(c, at);

		if (KIND(c, at) == TY_FLOAT128) {
			/* Signaling quad NaN: all-one exponent, nonzero fraction,
			   and a clear quiet bit. Preserve the low word as sticky. */
			lval l = wk_fromval(e, v, at), h = l;
			int lo = LO(treg)(e), hi = LO(treg)(e);
			h.off += 8;
			LO(load)(e, lo, &l, 8, false);
			LO(load)(e, hi, &h, 8, false);
			wk_lfree(e, &l);
			val sticky = LO(cmp)(e, T_EXCLAIMEQ, 8, false, wk_vreg(lo), vconst(0));
			val bits = LO(bin)(e, T_AMP, 8, false, wk_vreg(hi), vconst(0x7fffffffffffffffULL));
			bits = LO(bin)(e, T_PIPE, 8, false, bits, sticky);
			bits = LO(bin)(e, T_MINUS, 8, false, bits, vconst(0x7fff000000000001ULL));
			return LO(cmp)(e, T_LESS, 8, false, bits, vconst(0x7fffffffffffULL));
		}
		if (wk_mem(e, at)) {
			/* x87 stores an explicit integer bit next to its 63 fraction bits. */
			lval l = wk_fromval(e, v, at), hi = l;
			int re = LO(treg)(e);
			val exp, mant;

			hi.off += 8;
			LO(load)(e, re, &hi, 2, false);
			exp = LO(bin)(e, T_AMP, 4, false, wk_vreg(re), vconst(0x7fff));
			re = LO(treg)(e);
			LO(load)(e, re, &l, 8, false);
			wk_lfree(e, &l);
			val sig = wk_vreg(re);
			val integer = vconst(0);
			if (x->aux == BI_ISFINITE || x->aux == BI_ISNORMAL || x->aux == BI_ISNAN)
				integer = LO(cmp)(e, T_GREATEREQ, 8, false, wk_vreg(wk_dup(e, sig.r)), vconst((i128)1 << 63));
			if (x->aux == BI_ISFINITE || x->aux == BI_ISNORMAL) {
				val valid = integer;
				if (x->aux == BI_ISFINITE) {
					val zeroexp = LO(cmp)(e, T_EQUALEQUAL, 4, false, wk_vreg(wk_dup(e, exp.r)), vconst(0));
					valid = LO(bin)(e, T_PIPE, 4, false, valid, zeroexp);
				} else
					exp = LO(bin)(e, T_MINUS, 4, false, exp, vconst(1));
				exp = LO(cmp)(e, T_LESS, 4, false, exp, vconst(x->aux == BI_ISNORMAL ? 0x7ffe : 0x7fff));
				LO(tfree)(e, sig.r);
				return LO(bin)(e, T_AMP, 4, false, exp, valid);
			}
			if (x->aux == BI_ISSIGNALING) {
				/* Unsupported encodings (integer bit clear) are quiet NaNs. */
				mant = LO(bin)(e, T_MINUS, 8, false, sig, vconst(((i128)1 << 63) + 1));
				mant = LO(cmp)(e, T_LESS, 8, false, mant, vconst(((i128)1 << 62) - 1));
				exp = LO(cmp)(e, T_EQUALEQUAL, 4, false, exp, vconst(0x7fff));
				return LO(bin)(e, T_AMP, 4, false, exp, mant);
			}
			val unsupported = vconst(0);
			if (x->aux == BI_ISNAN) {
				val nonzero = LO(cmp)(e, T_EXCLAIMEQ, 4, false, wk_vreg(wk_dup(e, exp.r)), vconst(0));
				val nointeger = LO(bin)(e, T_CARET, 4, false, integer, vconst(1));
				unsupported = LO(bin)(e, T_AMP, 4, false, nonzero, nointeger);
			}
			mant = LO(cmp)(e, x->aux == BI_ISNAN ? T_EXCLAIMEQ : T_EQUALEQUAL,
				8, false, sig, vconst((i128)1 << 63));
			exp = LO(cmp)(e, T_EQUALEQUAL, 4, false, exp, vconst(0x7fff));
			val result = LO(bin)(e, T_AMP, 4, false, exp, mant);
			return x->aux == BI_ISNAN ? LO(bin)(e, T_PIPE, 4, false, result, unsupported) : result;
		}
		v = LO(fbits)(e, v, sz);
		v = LO(bin)(e, T_AMP, 8, false, v,
			vconst(sz == 2 ? 0x7fffULL : sz == 4 ? 0x7fffffffULL : 0x7fffffffffffffffULL));
		if (x->aux == BI_ISSIGNALING) {
			v = LO(bin)(e, T_MINUS, 8, false, v, vconst(KIND(c, at) == TY_BFLOAT16 ? 0x7f81ULL : sz == 2 ? 0x7c01ULL : sz == 4 ? 0x7f800001ULL : 0x7ff0000000000001ULL));
			return LO(cmp)(e, T_LESS, 8, false, v, vconst(KIND(c, at) == TY_BFLOAT16 ? 0x3fULL : sz == 2 ? 0x1ffULL : sz == 4 ? 0x3fffffULL : 0x7ffffffffffffULL));
		}
		if (x->aux == BI_ISNORMAL) {
			v = LO(bin)(e, T_GREATERGREATER, 8, false, v, vconst(KIND(c, at) == TY_BFLOAT16 ? 7 : sz == 2 ? 10 : sz == 4 ? 23 : 52));
			v = LO(bin)(e, T_MINUS, 8, false, v, vconst(1));
			return LO(cmp)(e, T_LESS, 8, false, v, vconst(KIND(c, at) == TY_BFLOAT16 ? 0xfe : sz == 2 ? 0x1e : sz == 4 ? 0xfe : 0x7fe));
		}
		return LO(cmp)(e, x->aux == BI_ISNAN ? T_GREATER : x->aux == BI_ISINF ? T_EQUALEQUAL : T_LESS,
			8, false, v, vconst(KIND(c, at) == TY_BFLOAT16 ? 0x7f80ULL : sz == 2 ? 0x7c00ULL : sz == 4 ? 0x7f800000ULL : 0x7ff0000000000000ULL));
	}
	case BI_SIGNBIT: {
		qty at = N(arg[0])->ty;
		val v = wk_gen(e, arg[0]);

		if (wk_mem(e, at)) {
			/* x87: the sign is bit 15 of the top word */
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
	case BI_ISGREATER: case BI_ISGREATEREQUAL: case BI_ISLESS:
	case BI_ISLESSEQUAL: case BI_ISLESSGREATER: case BI_ISUNORDERED: {
		static const int ops[] = { T_GREATER, T_GREATEREQ, T_LESS, T_LESSEQ,
			WK_FCMP_ORDERED_NE, WK_FCMP_UNORDERED };
		qty at = N(arg[0])->ty, bt = N(arg[1])->ty;
		qty ct = wk_optype(c, T_PLUS, at, bt);
		val a = wk_gen(e, arg[0]), b = wk_rhs(e, &a, arg[1], at);

		a = wk_conv(e, a, at, ct);
		b = wk_conv(e, b, bt, ct);
		return LO(fcmp)(e, ops[x->aux - BI_ISGREATER], ct, a, b);
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

		if (cc_eval(c, n, &cv)) {
			if (cv.kind == CV_INT)
				return vconst(cv.i);	/* __builtin_classify_type ... */
			if (cv.kind == CV_FLT) {
				wk_scalar(e, n, x->ty);
				return wk_fpconst(e, x->ty, cv.f, cv.ld ? cv.q : q_of_d(cv.f));
			}
		}
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

static val wk_gen_inner(ecx *e, uint32_t n);

/* Integer storage and operation widths need not equal _BitInt precision.
   Normalize every produced value, including loads and call results, before
   another expression observes it. Wide values use private temporary storage. */
VPRIV val wk_gen(ecx *e, uint32_t n)
{
	cc *c = e->c;
	qty t = N(n)->ty;

	if (em_bigint(c, t))
		return LO(bigcvt)(e, wk_gen_inner(e, n), t, t);
	if (likely(!em_bitint(c, t)))
		return wk_gen_inner(e, n);
	val v = wk_gen_inner(e, n);
	return wk_bitint_value(e, v, t);
}

static val wk_gen_inner(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);

	if (unlikely(vstack_low()))
		efatal(e, x->loc, "expression nested too deeply");
	switch (x->op) {
	case N_BIGNUM:
		return LO(bigconst)(e, x->ty, &c->lists[x->a], 0);
	case N_NUM:
		if (em_bigint(c, x->ty))
			return LO(bigconst)(e, x->ty, NULL, c->ipool[x->a]);
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
		if (y->sc == SC_REGISTER && sym_attrs(c, x->a)->asmname && !is_local_var(c, x->a)) {
			/* a global register variable read (current_stack_pointer: register ... asm("rsp")) */
			const char *rn = c->strs[sym_attrs(c, x->a)->asmname - 1].s;

			if (!strcmp(rn, "rsp") || !strcmp(rn, "esp") || !strcmp(rn, "sp"))
				return wk_vreg(LO(spget)(e));
			wk_unsup(e, n, "a global register variable other than the stack pointer");
		}
		wk_scalar(e, n, x->ty);
		return wk_load(e, wk_lval(e, n), x->ty);
	}
	case N_COMPLIT: {
		lval l = wk_lval(e, n);

		return wk_load(e, l, x->ty);
	}
	case N_INDEX: {
		int k = e->E->o->opt && tsize(c, x->ty) <= 8 ? ael_slot(e, n, false) : -1;
		uint32_t as;
		int64_t ak;
		int i;

		if (k >= 0 && (e->kvalid >> k & 1))
			return wk_const(c, e->kval[k], x->ty);
		if (wk_ael_of(e, n, &as, &ak) && (i = wk_ael_find(e, as, ak)) >= 0)
			return vconst(e->wk->ael[i].v);
		if (!is_array(c, x->ty) && !is_func(c, x->ty))
			wk_scalar(e, n, x->ty);
		return wk_load(e, wk_lval(e, n), x->ty);
	}
	case N_STR: case N_MEMBER: case N_DEREF:
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
		if (x->c) return wk_gen(e, x->c);
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
	case N_FNUM: {
		u128 q = 0;

		wk_scalar(e, n, x->ty);
		if (wk_mem(e, x->ty))
			memcpy(&q, &c->fpool[x->a + 1], 16);
		return wk_fpconst(e, x->ty, c->fpool[x->a], q);
	}
	case N_SIZEOF_VLA:
		if (x->aux == 1) {
			wk_vla_fix(e, x->b, false);
			return wk_vla_bytes(e, x->b);
		}
		if (x->a && N(x->a)->op == N_VAR && is_local_var(c, N(x->a)->a) && (T(c, c->syms[N(x->a)->a].ty)->flags & TF_VLA)) {
			lval l = { .k = L_FRAME, .off = wk_local(e, N(x->a)->a) + 16, .align = 8 };
			int r = LO(treg)(e);

			LO(load)(e, r, &l, 8, false);
			return wk_vreg(r);
		}
		if (x->a) {
			val operand = wk_gen(e, x->a);

			wk_free(e, &operand);
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

/* Evaluate integer expressions after binding locals, without emitting code or
   changing the shared AST. The caller rejects side effects and volatile reads. */
static bool wk_ival(ecx *e, uint32_t n, i128 *out)
{
	cc *c = e->c;
	const node *x = N(n);
	cval cv;
	i128 a, b, v;
	val k;
	if (!is_integer(c, x->ty) || em_bigint(c, x->ty)) return false;
	if (x->op == N_BUILTIN && x->aux == BI_CONSTANT_P) {
		*out = wk_constant_p(e, c->lists[x->b]);
		return true;
	}
	if ((x->op == N_NUM || x->op == N_VAR || x->op == N_BUILTIN) &&
	    cc_eval(c, n, &cv) && cv.kind == CV_INT) {
		*out = em_trunc_to(c, cv.i, x->ty);
		return true;
	}
	switch (x->op) {
	case N_INDEX: {
		int slot = tsize(c, x->ty) <= 8 ? ael_slot(e, n, false) : -1;
		if (slot < 0 || !(e->kvalid >> slot & 1)) return false;
		*out = em_trunc_to(c, e->kval[slot], x->ty);
		return true;
	}
	case N_VAR:
		if (!wk_const_sym(e, x->a, &k)) return false;
		*out = k.i;
		return true;
	case N_CAST:
		if (x->aux == CK_F2I && N(x->a)->op == N_VAR) {
			const sym *y = &c->syms[N(x->a)->a];
			if ((QQ(y->ty) & (Q_CONST | Q_VOLATILE)) != Q_CONST || (y->flags & SF_WEAK) || !y->body ||
			    !cc_eval(c, y->body, &cv) || cv.kind != CV_FLT) return false;
			*out = em_trunc_to(c, q_to_int(cv.ld ? cv.q : q_of_d(cv.f),
				(int)(tsize(c, x->ty) * 8), wk_signed(c, x->ty)), x->ty);
			return true;
		}
		if (!is_integer(c, N(x->a)->ty) || !wk_ival(e, x->a, &a)) return false;
		*out = x->aux == CK_BOOL ? a != 0 : em_trunc_to(c, a, x->ty);
		return true;
	case N_UNARY:
		if (!wk_ival(e, x->a, &a)) return false;
		switch (x->aux) {
		case T_PLUS: v = a; break;
		case T_MINUS: v = (i128)((u128)0 - (u128)a); break;
		case T_TILDE: v = ~a; break;
		case T_EXCLAIM: v = !a; break;
		default: return false;
		}
		*out = em_trunc_to(c, v, x->ty);
		return true;
	case N_BINARY:
		if (!wk_ival(e, x->a, &a)) {
			int known = is_integer(c, N(x->a)->ty) ? cond_fold(e, n) : -1;
			if (known < 0) return false;
			*out = known;
			return true;
		}
		if ((x->aux == T_LESSLESS || x->aux == T_GREATERGREATER) &&
		    (a == 0 || (a == -1 && x->aux == T_GREATERGREATER && wk_signed(c, x->ty)))) {
			*out = a;
			return true;
		}
		if (!wk_ival(e, x->b, &b)) return false;
		if (wk_is_cmp(x->aux))
			v = wk_fold_cmp(x->aux, wk_signed(c, N(x->a)->ty), a, b);
		else if (!wk_fold(c, x->aux, x->ty, a, b, &v)) return false;
		*out = em_trunc_to(c, v, x->ty);
		return true;
	case N_COMMA:
		return wk_ival(e, x->b, out);
	case N_COND:
		if (!wk_ival(e, x->a, &a)) return false;
		if (a && !x->b) { *out = em_trunc_to(c, a, x->ty); return true; }
		return wk_ival(e, a ? x->b : x->c, out);
	case N_LOGAND: case N_LOGOR: {
		bool ka = wk_ival(e, x->a, &a), kb = wk_ival(e, x->b, &b);
		if (x->op == N_LOGAND && ((ka && !a) || (kb && !b))) *out = 0;
		else if (x->op == N_LOGOR && ((ka && a) || (kb && b))) *out = 1;
		else if (ka && kb) *out = x->op == N_LOGAND ? a && b : a || b;
		else return false;
		return true;
	}
	default: return false;
	}
}

/* The existing range/comparison analysis is machine-independent. Its floating
   facts recognize library calls by name, so do not use it when builtins were
   disabled. Side effects and volatile reads must still execute. */
VPRIV int wk_condfold(ecx *e, uint32_t n)
{
	i128 v;
	if (!e->E->o->opt) return -1;
	int known = !e->E->noopt ? em_const_truth(e, n, wk_constant_p) : -1;
	if (known >= 0) return known;
	if (side_effects(e->c, n))
		return -1;
	if (wk_ival(e, n, &v)) return v != 0;
	if (e->E->o->no_builtin) return -1;
	for (unsigned i = 0; i < CC_DISABLED_BUILTIN_WORDS; i++)
		if (e->E->o->disabled_builtins[i])
			return -1;
	return cond_fold(e, n);
}

/* to lbl if (n != 0) == jump_if */
VPRIV void wk_cond(ecx *e, uint32_t n, bool jump_if, uint32_t lbl)
{
	cc *c = e->c;
	const node *x = N(n);
	int known = wk_condfold(e, n);

	if (known >= 0) {
		if (known == (int)jump_if)
			LO(jump)(e, lbl), wk_state(e)->cjump = true;	/* (always: what follows is dead) */
		return;
	}

	switch (x->op) {
	case N_LOGAND: case N_LOGOR: {
		bool and = x->op == N_LOGAND;

		if (and != jump_if) {
			/* a && b falls through when false: both must hold (or one hold for ||) */
			wk_state(e)->cjump = false;
			wk_cond(e, x->a, jump_if, lbl);
			if (wk_state(e)->cjump)
				return;	/* (a decided it: b is dead, __builtin_constant_p(x) && ...) */
			wk_cond(e, x->b, jump_if, lbl);
		} else {
			uint32_t skip = wk_newlabel(e);

			wk_state(e)->cjump = false;
			wk_cond(e, x->a, !jump_if, skip);
			if (!wk_state(e)->cjump)	/* (else b is dead: a always goes past it) */
				wk_cond(e, x->b, jump_if, lbl);
			wk_label(e, skip);
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

		if (is_flt(c, x->ty) || wk_wide(c, x->ty) || em_bigint(c, x->ty)) {
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
