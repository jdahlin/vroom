/* vcc code generator: expressions */
#include "../../../emit/emitimpl.h"
#include "../../../lib/vstack.h"

/* ---------------- expressions ---------------- */

static val const_global(ecx *e, uint32_t s)
{
	cc *c = e->c;
	const sym *y = &c->syms[s];
	cval cv;

	if (y->kind == S_VAR && (QQ(y->ty) & (Q_CONST | Q_VOLATILE)) == Q_CONST && is_integer(c, y->ty) && y->body &&
	    N(y->body)->op != N_INIT && cc_eval(c, y->body, &cv) && cv.kind == CV_INT)
		return vconst(em_trunc_to(c, cv.i, y->ty));
	if (y->kind == S_VAR && (QQ(y->ty) & (Q_CONST | Q_VOLATILE)) == Q_CONST && is_flt(c, y->ty) &&
	    tsize(c, y->ty) != 2 && y->body && N(y->body)->op != N_INIT && cc_eval(c, y->body, &cv) && cv.kind == CV_FLT)
		return f_const_cv(e, y->ty, &cv);
	return vnone();
}

static val gen_var(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	uint32_t s = x->a;
	const sym *y = &c->syms[s];
	lval l;
	val v;

	if (y->kind == S_FUNC) {
		uint32_t r = REF(RK_SYM, canon_sym(c, s));

		note_ref(e, r);
		if (unlikely(ref_weak(c, r))) {
			int rd = treg(e);

			addr_of_ref(e, rd, r, 0);
			return vreg(rd);
		}
		return vaddr(r, 0);
	}
	if (y->kind == S_BUILTIN)
		return vaddr(REF(RK_NAME, (uint32_t)y->val), 0);
	if (is_local_var(c, s)) {
		bind *b = binding(e, s);

		if (b->kind == B_CONST && b->inst)
			return b->v;
		if (b->kind == B_REG && b->inst) {
			int r = treg(e);

			mov(e, 1, r, b->v.r);
			return vreg(r);
		}
		if (b->kv && (e->kvalid >> (b->kv - 1) & 1))
			return vconst(e->kval[b->kv - 1]);
	} else {
		v = const_global(e, canon_sym(c, s));
		if (v.k == V_CONST)
			return v;
	}
	l = gen_lval(e, n);
	v = load(e, &l, x->ty);
	if (!(is_agg(c, x->ty) && l.k == L_REG))
		lfree(e, &l);
	return v;
}

/* result of a load through l, freeing the lvalue's base unless it became the value */
static val load_free(ecx *e, lval *l, qty t)
{
	val v = load(e, l, t);

	if (l->k == L_REG && !(v.k == V_REG && v.r == l->r))
		tfree(e, l->r);
	return v;
}

/* move v into the fixed register r (for joins) */
VPRIV void move_to(ecx *e, int r, int r2, val *v, qty t)
{
	cc *c = e->c;

	if (v->k == V_NONE)
		return;
	if (r2 >= 0 && is_wide(c, t)) {
		toreg2(e, v, is_signed(c, t));
		mov(e, 1, r, v->r);
		mov(e, 1, r2, v->r2);
		val_free(e, v);
		return;
	}
	switch (v->k) {
	case V_CONST:
		movi(e, 1, r, (int64_t)v->i);
		break;
	case V_ADDR:
		addr_of_ref(e, r, v->ref, (int64_t)v->i);
		break;
	default:
		toreg(e, v);
		mov(e, 1, r, v->r);
		val_free(e, v);
	}
}

/* -O3: c ? b : d with cheap arms as a compare and csel; false: not such a ?: */
static bool gen_csel(ecx *e, const node *x, val *out)
{
	cc *c = e->c;
	qty t = x->ty, ct = N(x->a)->ty;
	val ca, vb, vd;
	int cond, rb, rd, r;

	if (!e->E->opt3 || !x->b || (!is_integer(c, t) && !is_ptr(c, t)) || is_wide(c, t) || is_flt(c, ct) || is_wide(c, ct) ||
	    is_vec(c, ct) || is_agg(c, ct) || !cheap_arm(e, x->b) || !cheap_arm(e, x->c))
		return false;
	ca = gen(e, x->a);
	if (ca.k == V_CONST || ca.k == V_ADDR) {
		*out = gen(e, (ca.k == V_ADDR || ca.i) ? x->b : x->c);
		return true;
	}
	if (ca.k != V_REG)
		toreg(e, &ca);
	r = ca.r;
	if ((cond = last_cset(e, &ca)) >= 0) {
		e->nin--;
	} else {
		uint64_t sz = tsize(c, ct);
		int xe = last_ext(e, r);

		if (is_ptr(c, ct) || sz == 8 || (xe && (xe & 15) <= (int)sz))
			op3(e, I_SUBS, 1, ZR, r, ZR);
		else if (sz == 4)
			op3(e, I_SUBS, 0, ZR, r, ZR);
		else
			opi(e, I_ANDSI, 0, ZR, r, sz == 2 ? 0xffff : 0xff);
		cond = C_NE;
	}
	val_free(e, &ca);
	vb = gen(e, x->b);
	rb = toreg(e, &vb);
	vd = gen(e, x->c);
	rd = toreg(e, &vd);
	op3s(e, I_CSEL, X(c, t), rb, rb, rd, cond, 0);
	val_free(e, &vd);
	*out = vb;
	return true;
}

static val gen_cond_expr(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	qty t = x->ty;
	bool vd = is_void(c, t), wide = is_wide(c, t);
	uint32_t lelse, lend;
	int r = -1, r2 = -1;
	val v;

	if (gen_csel(e, x, &v))
		return v;
	lelse = newlabel(e), lend = newlabel(e);
	if (!x->b) {		/* a ?: c */
		v = gen(e, x->a);
		if (v.k == V_CONST || v.k == V_ADDR) {
			if (v.k == V_ADDR || v.i)
				return v;
			return gen(e, x->c);
		}
		if (wide) {
			toreg2(e, &v, is_signed(c, t));
			r = v.r, r2 = v.r2;
			op3(e, I_ORR, 1, R_X16, r, r2);
			cbz(e, true, 1, R_X16, lend);
		} else {
			r = toreg(e, &v);
			cbz(e, true, X(c, t), r, lend);
		}
		v = gen(e, x->c);
		move_to(e, r, r2, &v, t);
		deflabel(e, lend);
		return wide ? (val){ .k = V_REG2, .r = (uint8_t)r, .r2 = (uint8_t)r2 } : vreg(r);
	}
	{
		int k = 0;
		val ca = gen(e, x->a);

		if (ca.k == V_CONST || ca.k == V_ADDR)
			return gen(e, (ca.k == V_ADDR || ca.i) ? x->b : x->c);
		k = test_zero(e, &ca, N(x->a)->ty, lelse, false);
		(void)k;
	}
	if (!vd) {
		r = treg(e);
		if (wide)
			r2 = treg(e);
	}
	v = gen(e, x->b);
	if (!vd)
		move_to(e, r, r2, &v, t);
	else
		val_free(e, &v);
	jump(e, lend);
	deflabel(e, lelse);
	v = gen(e, x->c);
	if (!vd)
		move_to(e, r, r2, &v, t);
	else
		val_free(e, &v);
	deflabel(e, lend);
	if (vd)
		return vnone();
	return wide ? (val){ .k = V_REG2, .r = (uint8_t)r, .r2 = (uint8_t)r2 } : vreg(r);
}

static val gen_logical(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	bool isand = x->op == N_LOGAND;
	val a, b;
	uint32_t lshort, lend;
	int r = flaggable(e, n, true) ? gen_flags(e, n, true) : -1;

	if (r >= FL_TRUE)
		return vconst(r == FL_TRUE);
	if (r >= 0) {
		int d = treg(e);

		cset(e, d, r);
		return vreg(d);
	}
	a = gen(e, x->a);
	if (a.k == V_CONST || a.k == V_ADDR) {
		bool t = a.k == V_ADDR || a.i;

		if (isand && !t)
			return vconst(0);
		if (!isand && t)
			return vconst(1);
		b = gen(e, x->b);
		return tobool(e, b, N(x->b)->ty);
	}
	lshort = newlabel(e);
	lend = newlabel(e);
	r = treg(e);
	test_zero(e, &a, N(x->a)->ty, lshort, !isand);
	b = tobool(e, gen(e, x->b), N(x->b)->ty);
	if ((b.k == V_CONST && (isand ? !b.i : b.i)) || (b.k == V_ADDR && !isand)) {
		/* x && 0, x || 1: x's effects are done, the value is known (clang folds it,
		   and code it guards, such as calls to functions that do not exist, goes) */
		val_free(e, &b);
		deflabel(e, lshort);
		tfree(e, r);
		return vconst(isand ? 0 : 1);
	}
	move_to(e, r, -1, &b, prim(TY_INT));
	jump(e, lend);
	deflabel(e, lshort);
	movi(e, 0, r, isand ? 0 : 1);
	deflabel(e, lend);
	return vreg(r);
}

/* a lane-wise vector operation on the vectors at ra and rb into a new frame temporary; returns its address */
static val gen_vbin(ecx *e, int op, int ra, int rb, qty t, uint32_t loc)
{
	cc *c = e->c;
	uint64_t size = tsize(c, t);
	qty et = T(c, t)->base;
	unsigned es = (unsigned)tsize(c, et);
	bool q = size == 16;
	const char *mn, *arr;
	char ab[8];
	tbuf *rb_ = &e->raw;
	size_t st;
	int32_t sl;
	int rd;
	ins *i;

	if (is_flt(c, et))
		efatal(e, loc, "floating point vectors are not supported");
	if (size != 8 && size != 16)
		efatal(e, loc, "unsupported vector size");
	switch (op) {
	case T_AMP: mn = "and"; break;
	case T_PIPE: mn = "orr"; break;
	case T_CARET: mn = "eor"; break;
	case T_PLUS: mn = "add"; break;
	case T_MINUS: mn = "sub"; break;
	default: efatal(e, loc, "unsupported vector operator");
	}
	if (op == T_AMP || op == T_PIPE || op == T_CARET) {
		arr = q ? "16b" : "8b";
	} else {
		snprintf(ab, sizeof ab, "%u%c", (unsigned)(size / es), es == 1 ? 'b' : es == 2 ? 'h' : es == 4 ? 's' : 'd');
		arr = ab;
	}
	sl = slot(e, size, 16);
	rd = treg(e);
	opi(e, I_ADDI, 1, rd, FRAME, sl);
	st = rb_->n;
	tb_s(rb_, q ? "\tldr\tq16, [" : "\tldr\td16, [");
	reg(rb_, ra, 1);
	tb_s(rb_, q ? "]\n\tldr\tq17, [" : "]\n\tldr\td17, [");
	reg(rb_, rb, 1);
	tb_s(rb_, "]\n\t");
	tb_s(rb_, mn);
	tb_s(rb_, "\tv16.");
	tb_s(rb_, arr);
	tb_s(rb_, ", v16.");
	tb_s(rb_, arr);
	tb_s(rb_, ", v17.");
	tb_s(rb_, arr);
	tb_s(rb_, q ? "\n\tstr\tq16, [" : "\n\tstr\td16, [");
	reg(rb_, rd, 1);
	tb_c(rb_, ']');
	i = put(e, I_RAW);
	i->off = (int32_t)st;
	i->ref = (uint32_t)(rb_->n - st);
	return vreg(rd);
}

/* operand k of a lane-wise operation: lane i of a vector at ra, or the scalar in rs (copied) */
static val vlane(ecx *e, qty t, int r, unsigned i)
{
	cc *c = e->c;

	if (is_vec(c, t)) {
		qty et = T(c, t)->base;
		lval l = { .k = L_REG, .r = (uint8_t)r, .off = (int64_t)i * (int64_t)tsize(c, et), .align = talign(c, et) };

		return load(e, &l, et);
	}
	{
		int d = treg(e);

		mov(e, 1, d, r);
		return vreg(d);
	}
}

/*
 * Lane by lane through memory, for what the NEON path (gen_vbin) does not do:
 * other sizes, *, /, %, shifts, comparisons (-1 / 0 lanes), floating-point
 * lanes, a scalar operand (each lane against it), unary - and ~ (b NULL).
 */
static val gen_vlanes(ecx *e, int op, qty at, val *a, qty bt, val *b, qty rt, bool unary)
{
	cc *c = e->c;
	qty vt = is_vec(c, at) ? at : bt, et = T(c, vt)->base, rte = T(c, rt)->base;
	unsigned n = (unsigned)(tsize(c, vt) / tsize(c, et));
	int ra = toreg(e, a), rb = unary ? -1 : toreg(e, b), rd;
	int32_t sl = slot(e, tsize(c, rt), 16);

	rd = treg(e);
	opi(e, I_ADDI, 1, rd, FRAME, sl);
	for (unsigned i = 0; i < n; i++) {
		val x = vlane(e, at, ra, i), y = unary ? vconst(0) : vlane(e, bt, rb, i), r;
		qty yt = unary ? et : is_vec(c, bt) ? T(c, bt)->base : bt;

		if (unary) {
			if (op == T_MINUS)
				r = is_flt(c, et) ? f_neg(e, x, et) : arith(e, T_MINUS, et, vconst(0), x);
			else
				r = arith(e, T_CARET, et, x, vconst(-1));
		} else if (em_is_cmp(op)) {
			if (is_flt(c, et)) {
				r = f_compare(e, op, et, x, y);
			} else {
				int cond = is_wide(c, et) ? emit_cmp2(e, op, is_signed(c, et), &x, &y)
							  : emit_cmp(e, op, X(c, et), is_signed(c, et), &x, &y);
				int t;

				val_free(e, &y);
				t = toreg(e, &x);
				if (x.k == V_REG2)
					tfree(e, x.r2);
				cset(e, t, cond);
				r = vreg(t);
			}
			if (is_wide(c, rte)) {
				/* 0 / 1 -> 0 / all ones */
				int h = treg(e);

				op3(e, I_SUB, 1, r.r, ZR, r.r);
				mov(e, 1, h, r.r);
				r.k = V_REG2;
				r.r2 = (uint8_t)h;
			} else {
				r = arith(e, T_MINUS, rte, vconst(0), r);
			}
		} else if (is_flt(c, et)) {
			r = f_arith(e, op, et, x, y);
		} else {
			(void)yt;
			r = is_wide(c, et) ? arith2(e, op, et, x, y) : arith(e, op, et, x, y);
		}
		{
			lval l = { .k = L_REG, .r = (uint8_t)rd, .off = (int64_t)i * (int64_t)tsize(c, rte), .align = talign(c, rte) };

			store(e, &l, rte, &r);
			val_free(e, &r);
		}
	}
	tfree(e, ra);
	if (rb >= 0)
		tfree(e, rb);
	return vreg(rd);
}

static bool vbin_fast(cc *c, int op, qty lt, qty rt, qty ty)
{
	uint64_t size = tsize(c, ty);

	return is_vec(c, lt) && is_vec(c, rt) && (size == 8 || size == 16) && !is_flt(c, T(c, ty)->base) &&
	       (op == T_AMP || op == T_PIPE || op == T_CARET || op == T_PLUS || op == T_MINUS);
}

static val gen_binary(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	int op = x->aux;
	qty lt = N(x->a)->ty, rt = N(x->b)->ty, ty = x->ty;
	int known = em_is_cmp(op) ? cond_fold(e, n) : -1;
	val a, b;

	if (known >= 0)
		return vconst(known);
	if (unlikely(vstack_low()))	/* nested operators recurse here: an error before the stack runs out */
		efatal(e, x->loc, "expression nested too deeply");
	a = gen(e, x->a);
	if (unlikely(e->spill) && (a.k == V_REG || a.k == V_REG2) && tidx(a.r) >= 0 &&
	    !is_vec(c, lt) && N(x->b)->op != N_NUM && N(x->b)->op != N_VAR) {
		/* out of registers before: the left value waits in the frame while the right is computed */
		int32_t sl = slot(e, 16, 8);
		bool two = a.k == V_REG2;

		mem(e, I_ST, 3, 0, a.r, FRAME, sl);
		if (two)
			mem(e, I_ST, 3, 0, a.r2, FRAME, sl + 8);
		val_free(e, &a);
		b = gen(e, x->b);
		a.r = (uint8_t)treg(e);
		mem(e, I_LD, 3, 0, a.r, FRAME, sl);
		if (two) {
			a.r2 = (uint8_t)treg(e);
			mem(e, I_LD, 3, 0, a.r2, FRAME, sl + 8);
		}
	} else {
		b = gen(e, x->b);
	}
	if (is_vec(c, ty) && !vbin_fast(c, op, lt, rt, ty))
		return gen_vlanes(e, op, lt, &a, rt, &b, ty, false);
	if (is_vec(c, ty)) {
		int ra = toreg(e, &a), rb = toreg(e, &b);
		val r = gen_vbin(e, op, ra, rb, ty, x->loc);

		val_free(e, &a);
		val_free(e, &b);
		return r;
	}

	if (is_flt(c, lt) || is_flt(c, ty))
		return em_is_cmp(op) ? f_compare(e, op, lt, a, b) : f_arith(e, op, ty, a, b);
	/* pointer arithmetic */
	if (is_ptr(c, ty) && (op == T_PLUS || op == T_MINUS) && is_ptr(c, lt))
		return ptr_to_vla(c, lt) ? ptr_add_vla(e, a, b, lt, op == T_MINUS)
					 : ptr_add(e, a, b, (int64_t)elem_size(c, lt), op == T_MINUS);
	if (op == T_MINUS && is_ptr(c, lt) && is_ptr(c, rt) && ptr_to_vla(c, lt)) {
		/* (p - q) / the element size, which is known only now */
		val es = vla_bytes(e, T(c, lt)->base);
		int ra = toreg(e, &a), rb = toreg(e, &b), rs = toreg(e, &es);

		op3(e, I_SUB, 1, ra, ra, rb);
		op3(e, I_SDIV, 1, ra, ra, rs);
		tfree(e, rb);
		val_free(e, &es);
		return a;
	}
	if (op == T_MINUS && is_ptr(c, lt) && is_ptr(c, rt)) {
		int64_t es = (int64_t)elem_size(c, lt);

		if ((a.k == V_ADDR && b.k == V_ADDR && a.ref == b.ref) || (a.k == V_CONST && b.k == V_CONST))
			return vconst((a.i - b.i) / (es ? es : 1));
		{
			int ra = toreg(e, &a), rb = toreg(e, &b);

			op3(e, I_SUB, 1, ra, ra, rb);
			tfree(e, rb);
			if (es > 1 && !(es & (es - 1))) {
				shift_imm(e, I_ASRV, 1, ra, ra, __builtin_ctzll((uint64_t)es));
			} else if (es > 1) {
				movi(e, 1, R_X16, es);
				op3(e, I_SDIV, 1, ra, ra, R_X16);
			}
			return a;
		}
	}
	if (em_is_cmp(op)) {
		bool sg = !is_ptr(c, lt) && is_signed(c, lt);
		int cond, r;

		if (a.k == V_CONST && b.k == V_CONST) {
			bool ok;
			val v = fold_int_bin(e, op, is_ptr(c, lt) ? prim(TY_ULONG) : lt, ty, a.i, b.i, &ok);

			if (ok)
				return v;
		}
		if ((a.k == V_ADDR && b.k == V_ADDR && a.ref == b.ref) ||
		    (a.k == V_ADDR && b.k == V_CONST && b.i == 0) || (b.k == V_ADDR && a.k == V_CONST && a.i == 0)) {
			if (a.k == V_ADDR && b.k == V_ADDR) {
				bool ok;

				return fold_int_bin(e, op, prim(TY_LONG), ty, a.i, b.i, &ok);
			}
			if (op == T_EQUALEQUAL)
				return vconst(0);
			if (op == T_EXCLAIMEQ)
				return vconst(1);
		}
		if ((op == T_EQUALEQUAL || op == T_EXCLAIMEQ) && b.k == V_CONST && b.i == 0 && last_cset(e, &a) >= 0) {
			if (op == T_EQUALEQUAL)	/* (cond) == 0: the inverse cset */
				e->in[e->nin - 1].cond ^= 1;
			return a;
		}
		if (is_wide(c, lt))
			cond = emit_cmp2(e, op, sg, &a, &b);
		else
			cond = emit_cmp(e, op, X(c, lt), sg, &a, &b);
		val_free(e, &b);
		r = toreg(e, &a);
		if (a.k == V_REG2)
			tfree(e, a.r2);
		cset(e, r, cond);
		return vreg(r);
	}
	if (a.k == V_CONST && b.k == V_CONST) {
		bool ok;
		val v = fold_int_bin(e, op, ty, ty, a.i, b.i, &ok);

		if (op == T_LESSLESS || op == T_GREATERGREATER)
			v = fold_int_bin(e, op, lt, ty, a.i, b.i, &ok);
		if (ok)
			return v;
	}
	/* 0 << x, 0 >> x, -1 >> x (signed): the constant, whatever x is */
	if ((op == T_LESSLESS || op == T_GREATERGREATER) && a.k == V_CONST && b.k != V_CONST &&
	    (em_trunc_to(c, a.i, ty) == 0 || (op == T_GREATERGREATER && is_signed(c, ty) && em_trunc_to(c, a.i, ty) == -1))) {
		val_free(e, &b);
		return vconst(em_trunc_to(c, a.i, ty));
	}
	if (a.k == V_ADDR && b.k == V_CONST && (op == T_PLUS || op == T_MINUS) && tsize(c, ty) == 8) {
		a.i += op == T_PLUS ? b.i : -b.i;
		return a;
	}
	if (is_wide(c, ty))
		return arith2(e, op, ty, a, b);
	return arith(e, op, ty, a, b);
}

static val gen_unary(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	qty t = N(x->a)->ty;
	val v = gen(e, x->a);

	if (is_vec(c, x->ty) && (x->aux == T_MINUS || x->aux == T_TILDE))
		return gen_vlanes(e, x->aux, t, &v, t, NULL, x->ty, true);
	switch (x->aux) {
	case T_PLUS:
		return v;
	case T_MINUS:
		if (is_flt(c, x->ty))
			return f_neg(e, v, x->ty);
		if (v.k == V_CONST)
			return vconst(em_trunc_to(c, -v.i, x->ty));
		if (is_wide(c, x->ty)) {
			toreg2(e, &v, is_signed(c, x->ty));
			op3(e, I_SUBS, 1, v.r, ZR, v.r);
			op3(e, I_SBC, 1, v.r2, ZR, v.r2);
			return v;
		}
		toreg(e, &v);
		op3(e, I_SUB, X(c, x->ty), v.r, ZR, v.r);
		return v;
	case T_TILDE:
		if (v.k == V_CONST)
			return vconst(em_trunc_to(c, ~v.i, x->ty));
		if (is_wide(c, x->ty)) {
			toreg2(e, &v, is_signed(c, x->ty));
			op3(e, I_ORN, 1, v.r, ZR, v.r);
			op3(e, I_ORN, 1, v.r2, ZR, v.r2);
			return v;
		}
		toreg(e, &v);
		op3(e, I_ORN, X(c, x->ty), v.r, ZR, v.r);
		return v;
	case T_EXCLAIM:
		v = tobool(e, v, t);
		if (v.k == V_CONST)
			return vconst(!v.i);
		if (last_cset(e, &v) >= 0)
			e->in[e->nin - 1].cond ^= 1;
		else
			opi(e, I_EORI, 0, v.r, v.r, 1);
		return v;
	}
	efatal(e, x->loc, "unsupported unary operator");
}

/* convert v from type `from` to `to` the way an assignment back to an integer lvalue does */
static val conv_back(ecx *e, val v, qty from, qty to)
{
	cc *c = e->c;

	if (KIND(c, to) == TY_BOOL)
		return tobool(e, v, from);
	if (is_ptr(c, to) || is_ptr(c, from))
		return v;
	return cast(e, v, from, to, CK_INT);
}

/* old op= v (x a compound assignment to an lvalue of type lt): the new value, of type lt */
static val assign_op(ecx *e, const node *x, qty lt, val old, val v)
{
	cc *c = e->c;
	qty pt = x->c;
	int op = x->aux;

	if (is_ptr(c, lt)) {
		old = ptr_to_vla(c, lt) ? ptr_add_vla(e, old, v, lt, op == T_MINUS)
					: ptr_add(e, old, v, (int64_t)elem_size(c, lt), op == T_MINUS);
	} else if (is_flt(c, pt) || is_flt(c, lt)) {
		old = cast(e, old, lt, pt, CK_INT);
		old = is_flt(c, pt) ? f_arith(e, op, pt, old, v) : arith(e, op, pt, old, v);
		old = cast(e, old, pt, lt, CK_INT);
	} else {
		old = conv_back(e, old, lt, pt);
		if (old.k == V_CONST && v.k == V_CONST) {
			bool ok;
			val f = fold_int_bin(e, op, (op == T_LESSLESS || op == T_GREATERGREATER) ? pt : pt, pt, old.i, v.i, &ok);

			if (ok)
				old = f;
			else
				old = is_wide(c, pt) ? arith2(e, op, pt, old, v) : arith(e, op, pt, old, v);
		} else {
			old = is_wide(c, pt) ? arith2(e, op, pt, old, v) : arith(e, op, pt, old, v);
		}
		old = conv_back(e, old, pt, lt);
	}
	return old;
}

static val incdec_op(ecx *e, qty t, bool inc, bool post, val *old);

/* an assignment operator or ++/-- whose operand is an _Atomic object */
static inline bool atomic_target(ecx *e, uint32_t a, qty t)
{
	(void)a;
	return unlikely(T(e->c, t)->flags & TF_ATOMIC);
}

/*
 * x op= v, ++x, x++ ... on an _Atomic x: one atomic read-modify-write.
 *   ldar  ob, [p]
 * top:
 *   new = (copy of ob) op (copy of v)       the operator's usual code
 *   mov   cmp, ob
 *   casal cmp, new, [p]                     cmp = what memory held
 *   cmp   cmp, ob;  mov ob, cmp;  b.ne top
 * The operator runs outside any exclusive section, so anything it does
 * (calls included) is fine; v is evaluated once, before the loop.
 */
static val atomic_rmw(ecx *e, uint32_t n, bool inc, bool post)
{
	cc *c = e->c;
	const node *x = N(n);
	bool asg = x->op == N_ASSIGN;
	qty t = x->ty;
	val v = vnone(), old, nv;
	lval l;
	int sz, ra, ob, cmp, nb;
	bool own;
	uint32_t top;

	if (asg) {
		v = gen(e, x->b);
		if (v.k == V_REG2)
			efatal(e, x->loc, "compound assignment to an _Atomic object with a 128-bit operand is not supported");
		if (v.k != V_CONST)
			toreg(e, &v);
	}
	l = gen_lval(e, x->a);
	if (!is_atomic_lv(e, &l, t))
		efatal(e, x->loc, "_Atomic register variables are not supported");
	sz = atomic_sz(e, t);
	if (is_flt(c, t))
		fpk(e, t);
	ra = atomic_addr(e, &l, &own);
	ob = treg(e);
	word(e, (unsigned)sz << 30 | 0x08dffc00u | (unsigned)ra << 5 | (unsigned)ob);	/* ldar */
	top = newlabel(e);
	deflabel_loop(e, top, true);
	{
		int r = treg(e);

		if (sz < 3 && !is_flt(c, t) && is_signed(c, t))
			extend(e, r, ob, 1 << sz, true);
		else
			mov(e, 1, r, ob);
		old = vreg(r);
	}
	if (asg) {
		val vc = v;

		if (v.k == V_REG) {
			int r = treg(e);

			mov(e, 1, r, v.r);
			vc = vreg(r);
		}
		nv = assign_op(e, x, t, old, vc);
	} else {
		nv = incdec_op(e, t, inc, post, &old);
	}
	nb = nv.k == V_CONST && nv.i == 0 ? ZR : toreg(e, &nv);
	cmp = treg(e);
	mov(e, 1, cmp, ob);
	at_cas(e, (unsigned)sz, cmp, nb, ra);	/* casal */
	op3(e, I_SUBS, sz == 3, ZR, cmp, ob);	/* both zero-extended */
	mov(e, 1, ob, cmp);
	bcond(e, C_NE, top);
	tfree(e, cmp);
	tfree(e, ob);
	if (own)
		tfree(e, ra);
	lfree(e, &l);
	val_free(e, &v);
	if (!asg && post) {
		val_free(e, &nv);
		return old;
	}
	if (!asg && KIND(c, t) == TY_BOOL)
		val_free(e, &old);
	return nv;
}

static val gen_assign(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	qty lt = x->ty;
	lval l;
	val v, old;

	if (x->aux && is_vec(c, lt)) {
		/* v op= w */
		val w = gen(e, x->b), av, r;
		int ra, rw;

		l = gen_lval(e, x->a);
		{
			lval l2 = l;

			av = laddr(e, &l2);
		}
		if (!vbin_fast(c, x->aux, lt, N(x->b)->ty, lt)) {
			qty bt = N(x->b)->ty;

			r = gen_vlanes(e, x->aux, lt, &av, bt, &w, lt, false);
			store(e, &l, lt, &r);
			return r;
		}
		ra = toreg(e, &av), rw = toreg(e, &w);
		r = gen_vbin(e, x->aux, ra, rw, lt, x->loc);
		val_free(e, &w);
		val_free(e, &av);
		store(e, &l, lt, &r);
		return r;
	}
	if (!x->aux) {
		int ak;

		v = gen(e, x->b);
		if ((ak = ael_slot(e, x->a, true)) >= 0) {
			val cv = v;

			l = gen_lval(e, x->a);
			store(e, &l, lt, &v);
			lfree(e, &l);
			e->kvalid &= (uint16_t)~(1u << ak);
			if (cv.k == V_CONST) {
				e->kval[ak] = (int64_t)em_trunc_to(c, cv.i, lt);
				e->kvalid |= (uint16_t)(1u << ak);
			}
			return v;
		}
		if (N(x->a)->op == N_VAR && (e->E->sflags[N(x->a)->a] & SA_CWRITE)) {
			bind *b = binding(e, N(x->a)->a);

			if (b->kind == B_CONST && b->inst) {
				/* the same constant again (note_assign) */
				val_free(e, &v);
				return b->v;
			}
			if (b->kv) {
				int k = b->kv - 1;
				val cv = v;	/* store() may move v into a register */

				l = gen_lval(e, x->a);
				store(e, &l, lt, &v);
				lfree(e, &l);
				e->kvalid &= (uint16_t)~(1u << k);
				if (cv.k == V_CONST) {
					e->kval[k] = (int64_t)em_trunc_to(c, cv.i, lt);
					e->kvalid |= (uint16_t)(1u << k);
				}
				return v;
			}
		}
		l = gen_lval(e, x->a);
		l.once = e->E->opt3;
		store(e, &l, lt, &v);
		lfree(e, &l);
		if (l.bf && v.k == V_REG) {
			/* the value of a bit-field assignment is the truncated value */
			opbf(e, l.sgn ? I_SBFM : I_UBFM, 1, v.r, v.r, 0, l.width - 1);
		}
		return v;
	}
	if (atomic_target(e, x->a, lt))
		return atomic_rmw(e, n, false, false);
	v = gen(e, x->b);
	l = gen_lval(e, x->a);
	old = load(e, &l, lt);
	old = assign_op(e, x, lt, old, v);
	store(e, &l, lt, &old);
	lfree(e, &l);
	return old;
}

/* ++/-- on *old (of type t): returns the new value; *old becomes what a postfix operator yields */
static val incdec_op(ecx *e, qty t, bool inc, bool post, val *old)
{
	cc *c = e->c;
	val nv;
	int64_t d = is_ptr(c, t) ? (int64_t)elem_size(c, t) : 1;

	if (KIND(c, t) == TY_BOOL) {
		nv = vconst(1);
		if (!inc) {
			/* --b: b = b - 1 != 0, i.e. !b */
			int r = treg(e), ro = toreg(e, old);

			opi(e, I_EORI, 0, r, ro, 1);
			nv = vreg(r);
		}
	} else if (is_ld(c, t)) {
		val one = vconst(ld_of_d(1.0)), cp = *old;

		toreg2(e, &cp, false);
		if (post) {
			nv.k = V_REG2;
			nv.r = (uint8_t)treg(e);
			nv.r2 = (uint8_t)treg(e);
			mov(e, 1, nv.r, cp.r);
			mov(e, 1, nv.r2, cp.r2);
			nv = f_arith(e, inc ? T_PLUS : T_MINUS, t, nv, one);
			*old = cp;
		} else {
			nv = *old = f_arith(e, inc ? T_PLUS : T_MINUS, t, cp, one);
		}
	} else if (is_flt(c, t)) {
		int d = fpk(e, t);

		toreg(e, old);
		if (post) {
			int r = treg(e);

			mov(e, 1, r, old->r);
			nv = f_arith(e, inc ? T_PLUS : T_MINUS, t, vreg(r), f_const(d, 1.0));
		} else {
			nv = *old = f_arith(e, inc ? T_PLUS : T_MINUS, t, *old, f_const(d, 1.0));
		}
	} else if (is_wide(c, t)) {
		val one = vconst(1), cp = *old;

		toreg2(e, &cp, is_signed(c, t));
		if (post) {
			nv.k = V_REG2;
			nv.r = (uint8_t)treg(e);
			nv.r2 = (uint8_t)treg(e);
			mov(e, 1, nv.r, cp.r);
			mov(e, 1, nv.r2, cp.r2);
			nv = arith2(e, inc ? T_PLUS : T_MINUS, t, nv, one);
			*old = cp;
		} else {
			nv = arith2(e, inc ? T_PLUS : T_MINUS, t, cp, one);
			*old = nv;
		}
	} else if (is_ptr(c, t) && ptr_to_vla(c, t)) {
		/* a pointer to a VLA steps by the size computed now */
		toreg(e, old);
		if (post) {
			int r = treg(e);

			mov(e, 1, r, old->r);
			nv = ptr_add_vla(e, vreg(r), vconst(1), t, !inc);
		} else {
			nv = *old = ptr_add_vla(e, *old, vconst(1), t, !inc);
		}
	} else {
		int ro = toreg(e, old);

		if (post) {
			int r = treg(e);

			opi(e, inc ? I_ADDI : I_SUBI, X(c, t), r, ro, d);
			nv = vreg(r);
		} else {
			opi(e, inc ? I_ADDI : I_SUBI, X(c, t), ro, ro, d);
			nv = *old;
		}
	}
	return nv;
}

static val gen_incdec(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	qty t = x->ty;
	bool inc = x->op == N_PREINC || x->op == N_POSTINC, post = x->op == N_POSTINC || x->op == N_POSTDEC;
	lval l;
	val old, nv;

	if (atomic_target(e, x->a, t))
		return atomic_rmw(e, n, inc, post);
	l = gen_lval(e, x->a);
	old = load(e, &l, t);
	nv = incdec_op(e, t, inc, post, &old);
	store(e, &l, t, &nv);
	lfree(e, &l);
	if (post) {
		val_free(e, &nv);
		return old;
	}
	if (KIND(c, t) == TY_BOOL)
		val_free(e, &old);
	return nv;
}

/* -O3: an int expression whose value is 0 or 1: a comparison, !, && or || */
static bool bool_valued(cc *c, uint32_t n)
{
	const node *x = N(n);

	if (!is_integer(c, x->ty) || is_wide(c, x->ty))
		return false;
	switch (x->op) {
	case N_LOGAND: case N_LOGOR: return true;
	case N_UNARY: return x->aux == T_EXCLAIM;
	case N_BINARY: return em_is_cmp(x->aux) && !is_vec(c, x->ty) && !is_vec(c, N(x->a)->ty);
	default: return false;
	}
}

VPRIV val gen(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);

	switch (x->op) {
	case N_NUM:
		return vconst(c->ipool[x->a]);
	case N_FNUM:
		if (is_ld(c, x->ty)) {
			u128 q;

			memcpy(&q, &c->fpool[x->a + 1], 16);	/* its binary128 bits, after its double */
			return vconst((i128)q);
		}
		return f_const_t(e, x->ty, c->fpool[x->a]);
	case N_STR:
		note_ref(e, REF(RK_STR, x->a));
		return vaddr(REF(RK_STR, x->a), 0);
	case N_VAR:
		return gen_var(e, n);
	case N_MEMBER:
	case N_INDEX:
	case N_DEREF: {
		int k = x->op == N_INDEX ? ael_slot(e, n, false) : -1;

		if (k >= 0 && (e->kvalid >> k & 1))
			return vconst(e->kval[k]);
		lval l = gen_lval(e, n);

		l.once = e->E->opt3;
		return load_free(e, &l, x->ty);
	}
	case N_COMPLIT: {
		lval l = gen_lval(e, n);

		return load_free(e, &l, x->ty);
	}
	case N_ADDR: {
		const node *a = N(x->a);
		lval l;

		if (a->op == N_LABELADDR || is_func(c, a->ty))
			return gen(e, x->a);
		l = gen_lval(e, x->a);
		if (l.k == L_SYM)
			note_ref(e, l.ref);
		return laddr(e, &l);
	}
	case N_LABELADDR: {
		uint32_t l = x->a;
		lbl *m = &e->lm[l];
		int r = treg(e);

		if (m->inst != e->f->inst) {
			m->inst = e->f->inst;
			m->id = newlabel(e);
		}
		if (e->reach) {
			ins *i = put(e, I_ADR);

			i->rd = (uint8_t)r;
			i->label = m->id;
		}
		e->lrefc[m->id]++;
		e->lrefc[m->id] |= LREF_EXT;
		return vreg(r);
	}
	case N_CAST:
		switch (x->aux) {
		case CK_DECAY: {
			lval l = gen_lval(e, x->a);

			if (l.k == L_SYM)
				note_ref(e, l.ref);
			return laddr(e, &l);
		}
		case CK_FDECAY:
			return gen(e, x->a);
		case CK_VADDR:
			return gen(e, x->a);	/* a vector value is its address */
		case CK_VBITS: {
			/* the vector's bits as a scalar: load from its address */
			val v = gen(e, x->a);
			lval l = { .k = L_REG, .align = talign(c, N(x->a)->ty) };

			l.r = (uint8_t)toreg(e, &v);
			v = load(e, &l, x->ty);
			lfree(e, &l);
			return v;
		}
		case CK_SPLAT: {	/* the scalar into every lane of a new vector */
			qty et = T(c, x->ty)->base;
			uint64_t es = tsize(c, et), nl = tsize(c, x->ty) / es;
			int32_t sl = slot(e, tsize(c, x->ty), talign(c, x->ty));
			val v = gen(e, x->a);
			int r;

			for (uint64_t i = 0; i < nl; i++) {
				lval l = { .k = L_FRAME, .off = sl + (int64_t)(i * es), .align = talign(c, et) };

				store(e, &l, et, &v);
			}
			val_free(e, &v);
			r = treg(e);
			opi(e, I_ADDI, 1, r, FRAME, sl);
			return vreg(r);
		}
		case CK_UNION: {
			int32_t sl = slot(e, tsize(c, x->ty), talign(c, x->ty));
			lval l = { .k = L_FRAME, .off = sl, .align = talign(c, x->ty) };
			val v = gen(e, x->a);
			int r;

			if (tsize(c, x->ty) > tsize(c, N(x->a)->ty))
				for (uint64_t o = 0; o < tsize(c, x->ty); o += 8)
					mem(e, I_ST, tsize(c, x->ty) - o >= 8 ? 3 : 0, 0, ZR, FRAME, sl + (int64_t)o);
			store(e, &l, N(x->a)->ty, &v);
			val_free(e, &v);
			r = treg(e);
			opi(e, I_ADDI, 1, r, FRAME, sl);
			return vreg(r);
		}
		default: {
			val v;

			if (e->E->opt3 && x->aux == CK_BOOL && bool_valued(c, x->a))
				return gen(e, x->a);	/* already 0 or 1 */
			v = gen(e, x->a);
			return cast(e, v, N(x->a)->ty, x->ty, x->aux);
		}
		}
	case N_UNARY:
		return gen_unary(e, n);
	case N_PREINC: case N_PREDEC: case N_POSTINC: case N_POSTDEC:
		return gen_incdec(e, n);
	case N_BINARY:
		return gen_binary(e, n);
	case N_LOGAND:
	case N_LOGOR:
		return gen_logical(e, n);
	case N_ASSIGN:
		return gen_assign(e, n);
	case N_COND:
		return gen_cond_expr(e, n);
	case N_COMMA: {
		if (side_effects(c, x->a)) {
			val v = gen(e, x->a);

			val_free(e, &v);
		}
		return gen(e, x->b);
	}
	case N_CALL:
		return gen_call(e, n);
	case N_BUILTIN:
		return gen_builtin(e, n);
	case N_STMTEXPR:
		return gen_block(e, x->a, true);
	case N_VAARG:
		return va_arg_(e, x);
	case N_ZERO:
		return vconst(0);
	case N_CPLX: {	/* a _Complex value: both parts into a temporary, its address */
		qty et = c->tags[T(c, x->ty)->aux].f[0].ty;
		uint64_t es = tsize(c, et);
		int32_t sl = slot(e, 2 * es, talign(c, x->ty));
		lval d = { .k = L_FRAME, .off = sl, .align = talign(c, et) };
		val v = gen(e, x->a);
		int r;

		store(e, &d, et, &v);
		val_free(e, &v);
		v = gen(e, x->b);
		d.off += (int32_t)es;
		store(e, &d, et, &v);
		val_free(e, &v);
		r = treg(e);
		opi(e, I_ADDI, 1, r, FRAME, sl);
		return vreg(r);
	}
	case N_SIZEOF_VLA: {
		const sym *y = x->a && N(x->a)->op == N_VAR ? &c->syms[N(x->a)->a] : NULL;

		if (y && y->kind == S_VAR && binding(e, N(x->a)->a)->kind == B_INDIRECT &&
		    binding(e, N(x->a)->a)->inst == e->f->inst && (T(c, y->ty)->flags & TF_VLA)) {
			int r = treg(e);

			mem(e, I_LD, 3, 0, r, FRAME, binding(e, N(x->a)->a)->slot + 16);
			return vreg(r);
		}
		return vla_bytes(e, x->b);
	}
	default:
		efatal(e, x->loc, "unsupported expression (node %d)", x->op);
	}
}
