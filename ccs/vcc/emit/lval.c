/* vcc code generator: lvalues from expressions, typed loads and stores */
#include "emitimpl.h"

/* ---------------- lvalues from expressions ---------------- */

VPRIV uint32_t align_at(uint64_t off, uint32_t a)
{
	while (a > 1 && (off & (a - 1)))
		a >>= 1;
	return a ? a : 1;
}

VPRIV uint64_t elem_size(cc *c, qty ptr)
{
	qty b = T(c, ptr)->base;
	int k = KIND(c, b);

	if (k == TY_VOID || k == TY_FUNC)
		return 1;
	return tsize(c, b);
}

/* p + i * scale, as a value */
VPRIV val ptr_add(ecx *e, val p, val i, int64_t scale, bool sub)
{
	if (i.k == V_CONST) {
		i128 d = i.i * scale;

		if (sub)
			d = -d;
		if (p.k == V_CONST || p.k == V_ADDR) {
			p.i = p.k == V_CONST ? (i128)(uint64_t)(p.i + d) : p.i + d;
			return p;
		}
		{
			int r = toreg(e, &p);

			if (d)
				opi(e, I_ADDI, 1, r, r, (int64_t)d);
			return p;
		}
	}
	{
		int rp = toreg(e, &p), ri = toreg(e, &i);

		if (scale > 0 && !(scale & (scale - 1))) {
			op3s(e, sub ? I_SUB : I_ADD, 1, rp, rp, ri, 0, __builtin_ctzll((uint64_t)scale));
		} else {
			movi(e, 1, R_X16, scale);
			op4(e, I_MADD, 1, ri, ri, R_X16, ZR);
			op3(e, sub ? I_SUB : I_ADD, 1, rp, rp, ri);
		}
		tfree(e, ri);
		return p;
	}
}

/* p + i elements of a pointer to a VLA: the element size is computed now */
VPRIV val ptr_add_vla(ecx *e, val p, val i, qty ptr, bool sub)
{
	val b = vla_bytes(e, e->c->ty[QI(ptr)].base);
	int ri = toreg(e, &i), rb = toreg(e, &b);

	op4(e, I_MADD, 1, ri, ri, rb, ZR);
	val_free(e, &b);
	return ptr_add(e, p, i, 1, sub);
}

VPRIV bool ptr_to_vla(cc *c, qty ptr)
{
	const ctype *b = T(c, T(c, ptr)->base);

	return b->kind == TY_ARRAY && (b->flags & TF_VLA);
}

VPRIV lval gen_lval(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	lval l = { 0 };

	switch (x->op) {
	case N_VAR: {
		uint32_t s = x->a, cs;

		if (is_local_var(c, s))
			return local_lval(e, s);
		cs = canon_sym(c, s);
		if (c->syms[cs].sc == SC_REGISTER && c->syms[cs].depth == 0) {
			int r = regvar_reg(c, cs);

			if (r < 0)
				efatal(e, x->loc, "unsupported global register variable");
			l.k = L_GLOBREG;
			l.r = (uint8_t)r;
			return l;
		}
		if (DARWIN(c) && ((c->syms[cs].flags | c->syms[s].flags) & SF_THREAD)) {
			/* Darwin TLV: the descriptor's thunk returns the address in x0 */
			int r;

			note_ref(e, REF(RK_SYM, cs));
			e->calls = true;
			if (e->reach)
				put(e, I_TLV)->ref = REF(RK_SYM, cs);
			r = treg(e);
			mov(e, 1, r, 0);
			l.k = L_REG;
			l.r = (uint8_t)r;
			l.align = sym_align(c, cs);
			return l;
		}
		l.k = L_SYM;
		l.ref = REF(RK_SYM, cs);
		l.align = sym_align(c, cs);
		return l;
	}
	case N_STR:
		l.k = L_SYM;
		l.ref = REF(RK_STR, x->a);
		l.align = c->strs[x->a].esz;
		return l;
	case N_DEREF: {
		val p = gen(e, x->a);
		qty bt = T(c, N(x->a)->ty)->base;

		return lfromval(e, p, KIND(c, bt) == TY_VOID ? 1 : talign(c, bt));
	}
	case N_MEMBER: {
		const field *f = &c->tags[x->b].f[x->c];
		const node *b = N(x->a);

		if (b->flags & NF_LVALUE || b->op == N_VAR || b->op == N_DEREF || b->op == N_MEMBER || b->op == N_INDEX ||
		    b->op == N_COMPLIT || b->op == N_STR)
			l = gen_lval(e, x->a);
		else
			l = lfromval(e, gen(e, x->a), talign(c, b->ty));
		l.off += (int64_t)(f->off / 8);
		l.align = align_at((uint64_t)l.off, l.align ? l.align : 1);
		if (l.k != L_SYM && l.k != L_FRAME)
			l.align = align_at(f->off / 8, talign(c, f->ty) < l.align ? talign(c, f->ty) : l.align);
		if (f->bitfield) {
			l.bf = 1;
			l.bitoff = (uint16_t)(f->off % 8);
			l.width = f->width;
			l.sgn = is_signed(c, f->ty);
		}
		return l;
	}
	case N_INDEX: {
		val p = gen(e, x->a), i = gen(e, x->b);
		uint64_t es = elem_size(c, N(x->a)->ty);
		qty et = T(c, N(x->a)->ty)->base;

		if (ptr_to_vla(c, N(x->a)->ty))
			return lfromval(e, ptr_add_vla(e, p, i, N(x->a)->ty, false), talign(c, et));
		if (i.k == V_CONST && (p.k == V_ADDR)) {
			l.k = L_SYM;
			l.ref = p.ref;
			l.off = (int64_t)(p.i + i.i * (i128)es);
			l.align = talign(c, et);
			return l;
		}
		if (i.k == V_CONST && p.k == V_REG) {
			l.k = L_REG;
			l.r = p.r;
			l.off = (int64_t)(i.i * (i128)es);
			l.align = talign(c, et);
			return l;
		}
		p = ptr_add(e, p, i, (int64_t)es, false);
		return lfromval(e, p, talign(c, et));
	}
	case N_COMPLIT: {
		uint32_t s = x->b;
		const sym *y = &c->syms[s];

		if (y->sc == SC_STATIC || (y->flags & SF_GLOBAL)) {
			l.k = L_SYM;
			l.ref = REF(RK_SYM, s);
			l.align = sym_align(c, s);
			return l;
		}
		{
			int32_t sl = bind_slot(e, s);

			/* it lives until the end of the block, past the statement's temporaries */
			if (e->foff > e->cl_top)
				e->cl_top = e->foff;
			lval d = { .k = L_FRAME, .off = sl, .align = sym_align(c, s) };

			gen_init_local(e, &d, y->ty, x->a);
			return d;
		}
	}
	case N_CAST:
		if (x->aux == CK_NOP && (N(x->a)->flags & NF_LVALUE))
			return gen_lval(e, x->a);
		/* fall through */
	default: {
		val v = gen(e, n);

		if (!is_agg(c, x->ty) && !is_func(c, x->ty))
			efatal(e, x->loc, "not an lvalue (node %d)", x->op);
		return lfromval(e, v, talign(c, x->ty));
	}
	}
}
static int bf_bytes(int bits) { return bits <= 8 ? 1 : bits <= 16 ? 2 : bits <= 32 ? 4 : 8; }

VPRIV val load(ecx *e, lval *l, qty t)
{
	cc *c = e->c;
	uint64_t size = tsize(c, t);
	int r;

	if (is_agg(c, t) || is_func(c, t))
		return laddr(e, l);
	if (is_flt(c, t) && !is_ld(c, t))
		fpk(e, t);	/* bits, zero-extended (below: not signed) */
	if (l->k == L_GLOBREG) {
		r = treg(e);
		mov(e, 1, r, l->r);
		return vreg(r);
	}
	if (l->bf) {
		int bits = l->bitoff + l->width, nb = bf_bytes(bits);

		if (bits > 64) {
			/* straddles 8 bytes (packed): the low 64 - bitoff bits, then the rest from byte 8 on */
			lval l2 = *l;
			int h = treg(e);

			if (bits - 64 > 64)
				efatal(e, 0, "unsupported bit-field layout");
			r = treg(e);
			load_n(e, r, l, 8, 0);
			l2.off += 8;
			load_n(e, h, &l2, bf_bytes(bits - 64), 0);
			opbf(e, I_UBFM, 1, r, r, l->bitoff, 63);
			opbf(e, I_BFM, 1, r, h, l->bitoff, bits - 64 - 1);	/* bfi r, h, #(64 - bitoff), #(bits - 64) */
			tfree(e, h);
			opbf(e, l->sgn ? I_SBFM : I_UBFM, 1, r, r, 0, l->width - 1);
			return vreg(r);
		}
		r = treg(e);
		load_n(e, r, l, nb, 0);
		opbf(e, l->sgn ? I_SBFM : I_UBFM, 1, r, r, l->bitoff, l->bitoff + l->width - 1);
		return vreg(r);
	}
	if (size == 16) {
		val v = { .k = V_REG2 };
		lval h = *l;

		v.r = (uint8_t)treg(e);
		v.r2 = (uint8_t)treg(e);
		load_n(e, v.r, l, 8, 0);
		h.off += 8;
		load_n(e, v.r2, &h, 8, 0);
		return v;
	}
	r = treg(e);
	if (size == 0)
		movi(e, 1, r, 0);
	else
		load_n(e, r, l, (int)size, size < 8 && !is_flt(c, t) && is_signed(c, t) ? 1 : 0);
	return vreg(r);
}

/* store v (of type t) to l; v is consumed unless keep; returns v (in a register if kept) */
VPRIV void store(ecx *e, lval *l, qty t, val *v)
{
	cc *c = e->c;
	uint64_t size = tsize(c, t);

	if (is_agg(c, t)) {
		val d = laddr(e, l);
		int rs = toreg(e, v), rd = toreg(e, &d);

		copy_mem(e, rd, rs, size);
		val_free(e, &d);
		return;
	}
	if (l->k == L_GLOBREG) {
		int r = toreg(e, v);

		mov(e, 1, l->r, r);
		return;
	}
	if (l->bf && l->bitoff + l->width > 64) {
		/* straddles 8 bytes: see load() */
		int bits = l->bitoff + l->width, nb = bf_bytes(bits - 64);
		int r = toreg(e, v), w = treg(e), lo = 64 - l->bitoff;
		lval l2 = *l;

		load_n(e, w, l, 8, 0);
		opbf(e, I_BFM, 1, w, r, (64 - l->bitoff) & 63, lo - 1);
		store_n(e, w, l, 0, 8);
		l2.off += 8;
		load_n(e, w, &l2, nb, 0);
		shift_imm(e, I_LSRV, 1, R_X16, r, lo);
		opbf(e, I_BFM, 1, w, R_X16, 0, bits - 64 - 1);
		store_n(e, w, l, 8, nb);
		tfree(e, w);
		return;
	}
	if (l->bf) {
		int bits = l->bitoff + l->width, nb = bf_bytes(bits);
		int r = toreg(e, v), w = treg(e);
		lval l2 = *l;

		load_n(e, w, &l2, nb, 0);
		/* bfi w, r, #bitoff, #width = bfm w, r, #(-bitoff mod 64), #(width-1) */
		opbf(e, I_BFM, 1, w, r, (64 - l->bitoff) & 63, l->width - 1);
		store_n(e, w, l, 0, nb);
		tfree(e, w);
		return;
	}
	if (size == 16) {
		toreg2(e, v, true);
		store_n(e, v->r, l, 0, 8);
		store_n(e, v->r2, l, 8, 8);
		return;
	}
	if (size == 0)
		return;
	if (v->k == V_CONST && v->i == 0) {
		store_n(e, ZR, l, 0, (int)size);
		return;
	}
	{
		int r = toreg(e, v);

		store_n(e, r, l, 0, (int)size);
	}
}
