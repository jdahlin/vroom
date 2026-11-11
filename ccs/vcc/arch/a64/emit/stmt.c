/* vcc code generator: local initialisers, cleanups, statements */
#include "../../../emit/emitimpl.h"

/* ---------------- local initialisers ---------------- */

static void zero_lval(ecx *e, const lval *d, uint64_t size)
{
	int64_t off;
	int b;

	if (size > 256) {
		val a;
		lval t = *d;
		int r;

		a = laddr(e, &t);
		r = toreg(e, &a);
		zero_mem(e, r, size);
		if (d->k != L_REG)
			val_free(e, &a);
		return;
	}
	b = lbase(e, d, &off, 8);
	for (uint64_t o = 0; o < size;) {
		uint64_t k = size - o >= 8 ? 8 : size - o >= 4 ? 4 : size - o >= 2 ? 2 : 1;

		mem(e, I_ST, log2sz(k), 0, ZR, b, off + (int64_t)o);
		o += k;
	}
}

static bool is_zero_init(cc *c, uint32_t v)
{
	const node *x = N(v);

	if (!v || x->op == N_ZERO)
		return true;
	if (x->op == N_NUM)
		return c->ipool[x->a] == 0;
	if (x->op == N_CAST && (x->aux == CK_INT || x->aux == CK_NOP || x->aux == CK_NULL || x->aux == CK_I2P))
		return x->aux == CK_NULL || is_zero_init(c, x->a);
	return false;
}

/* store the elements of init into the (already zeroed) object at d */
static void init_elems(ecx *e, const lval *d, qty ty, uint32_t init)
{
	cc *c = e->c;
	const node *x = N(init);

	/* Initialization precedes publication and copies the plain payload. */
	ty = (T(c, ty)->flags & TF_ATOMIC) ? QT(T(c, ty)->n, 0) : ty;

	if (!init || x->op == N_ZERO)
		return;
	if (x->op == N_INIT) {
		const uint32_t *v = &c->lists[x->b];

		if (x->aux == 1) {
			qty et = T(c, ty)->base;
			uint64_t es = tsize(c, et);

			for (uint32_t i = 0; i < x->c; i++) {
				uint32_t lo = v[3 * i], hi = v[3 * i + 1], val_ = v[3 * i + 2];

				for (uint64_t k = lo; k <= hi; k++) {
					lval el = *d;

					if (is_bvec(c, ty)) {
						val bv = gen(e, val_);
						el.off += (int64_t)(k / 8);
						el.bf = 1, el.width = 1, el.bitoff = k % 8;
						store(e, &el, et, &bv);
						val_free(e, &bv);
						continue;
					}
					el.off += (int64_t)(k * es);
					el.align = align_at((uint64_t)el.off, d->align);
					init_elems(e, &el, et, val_);
				}
			}
			return;
		}
		{
			const tag *r = &c->tags[T(c, ty)->aux];

			for (uint32_t i = 0; i < x->c && i < r->nfield; i++) {
				const field *f = &r->f[i];
				lval el = *d;

				if (x->aux == 2 && i != x->a)
					continue;
				if (!v[i])
					continue;
				el.off += (int64_t)(f->off / 8);
				el.align = align_at((uint64_t)el.off, d->align);
				if (f->bitfield) {
					val bv;

					if (is_zero_init(c, v[i]))
						continue;
					bv = gen(e, v[i]);
					el.bf = 1;
					el.bitoff = (uint16_t)(f->off % 8);
					el.width = f->width;
					el.sgn = is_signed(c, f->ty);
					store(e, &el, f->ty, &bv);
					val_free(e, &bv);
					continue;
				}
				init_elems(e, &el, f->ty, v[i]);
			}
		}
		return;
	}
	if (x->op == N_STR && is_array(c, ty)) {
		const cstr *s = &c->strs[x->a];
		uint64_t n = (uint64_t)s->n * s->esz, cap = tsize(c, ty);
		int64_t off;
		int b = lbase(e, d, &off, 8);

		if (n > cap)
			n = cap;
		for (uint64_t o = 0; o < n;) {
			uint64_t k = n - o >= 8 ? 8 : n - o >= 4 ? 4 : n - o >= 2 ? 2 : 1;
			uint64_t w = 0;

			memcpy(&w, s->s + o, k);
			if (w) {
				movi(e, 1, R_X17 - 1, (int64_t)w);
				mem(e, I_ST, log2sz(k), 0, R_X16, b, off + (int64_t)o);
			}
			o += k;
		}
		return;
	}
	if (is_zero_init(c, init) && !is_agg(c, ty))
		return;
	{
		val v = gen(e, init);
		lval dd = *d;

		store(e, &dd, ty, &v);
		val_free(e, &v);
	}
}

VPRIV void gen_init_local(ecx *e, const lval *dst, qty ty, uint32_t init)
{
	cc *c = e->c;
	const node *x = N(init);

	if (!init)
		return;
	if (T(c, ty)->flags & TF_ATOMIC) {
		zero_lval(e, dst, tsize(c, ty));
		ty = (T(c, ty)->flags & TF_ATOMIC) ? QT(T(c, ty)->n, 0) : ty;
	}
	if (x->op == N_INIT || x->op == N_ZERO || (x->op == N_STR && is_array(c, ty))) {
		zero_lval(e, dst, tsize(c, ty));
		init_elems(e, dst, ty, init);
		return;
	}
	{
		val v = gen(e, init);
		lval d = *dst;

		store(e, &d, ty, &v);
		val_free(e, &v);
	}
}

/* ---------------- cleanups ---------------- */

static void run_cleanup(ecx *e, uint32_t s)
{
	cc *c = e->c;
	uint32_t fn = sym_attrs(c, s)->cleanup;
	carg a;
	lval l;

	if (T(c, c->syms[s].ty)->flags & TF_VLA) {
		const bind *b = binding(e, s);
		int r;

		if (!e->reach || b->kind != B_INDIRECT || b->inst != e->f->inst)
			return;
		r = treg(e);
		mem(e, I_LD, 3, 0, r, FRAME, b->slot + 8);
		mov(e, 1, SP, r);
		{
			val v = vreg(r);

			val_free(e, &v);
		}
		return;
	}
	if (!fn || !e->reach)
		return;
	l = local_lval(e, s);
	a.v = laddr(e, &l);
	a.t = prim(TY_LONG);
	a.slot = 1;
	fn = canon_sym(c, fn);
	if (can_inline(e, fn)) {
		val r = inline_call(e, fn, &a, 1, 0);

		val_free(e, &r);
		return;
	}
	{
		val r = do_call(e, REF(RK_SYM, fn), NULL, &a, 1, prim(TY_VOID), false, -1);

		val_free(e, &r);
	}
}

/* run cleanups (innermost first) down to depth `to` without popping them */
VPRIV void run_cleanups(ecx *e, int to)
{
	for (int i = e->nclean; i-- > to;)
		run_cleanup(e, e->clean[i]);
}

/* ---------------- statements ---------------- */

VPRIV uint32_t user_label(ecx *e, uint32_t l)
{
	lbl *m = &e->lm[l];

	if (m->inst != e->f->inst) {
		m->inst = e->f->inst;
		m->id = newlabel(e);
	}
	return m->id;
}

VPRIV val gen_block(ecx *e, uint32_t n, bool want)
{
	cc *c = e->c;
	const node *x = N(n);
	int64_t mark = e->foff, cl_top = e->cl_top;
	int cmark = e->nclean;
	val v = vnone();
	uint32_t vstmt = 0;
	qty vt = 0;

	if (want && x->c) {
		uint32_t ls = c->lists[x->b + x->c - 1];

		while (ls && N(ls)->op == N_LABEL)
			ls = N(ls)->c;
		if (ls && N(ls)->op == N_EXPR && N(ls)->a) {
			vstmt = ls;
			vt = N(N(ls)->a)->ty;
		}
	}
	/* an aggregate value must outlive the block's slots */
	int64_t vslot = 1;

	if (vstmt && is_agg(c, vt))
		vslot = slot(e, tsize(c, vt), talign(c, vt));
	mark = e->foff;
	for (uint32_t i = 0; i < x->c; i++) {
		uint32_t s = c->lists[x->b + i];

		if (i + 1 == x->c && vstmt) {
			uint32_t t = s;

			/* labels in front of the value */
			while (N(t)->op == N_LABEL) {
				uint32_t l = user_label(e, N(t)->a);

				if (e->E->lref[N(t)->a] & LR_BACK)
					deflabel_forced(e, l);
				else
					deflabel(e, l);
				t = N(t)->c;
			}
			v = gen(e, N(vstmt)->a);
			if (vslot != 1) {
				lval d = { .k = L_FRAME, .off = vslot, .align = talign(c, vt) };
				int r;

				store(e, &d, vt, &v);
				val_free(e, &v);
				r = treg(e);
				opi(e, I_ADDI, 1, r, FRAME, vslot);
				v = vreg(r);
			}
			break;
		}
		gen_stmt(e, s);
	}
	if (e->nclean > cmark) {
		run_cleanups(e, cmark);
		e->nclean = cmark;
	}
	e->foff = mark;
	e->cl_top = cl_top;
	return v;
}

static void collect_cases(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x;

	if (!n)
		return;
	x = N(n);
	switch (x->op) {
	case N_CASE:
	case N_DEFAULT:
		e->cases[e->ncases++] = n;
		collect_cases(e, x->c);
		return;
	case N_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			collect_cases(e, c->lists[x->b + i]);
		return;
	case N_IF:
		collect_cases(e, x->b);
		collect_cases(e, x->c);
		return;
	case N_WHILE: case N_DO:
		collect_cases(e, x->b);
		return;
	case N_FOR:
		collect_cases(e, c->lists[x->c + 1]);
		return;
	case N_LABEL:
		collect_cases(e, x->c);
		return;
	default:
		return;
	}
}

/* The selector is borrowed from gen_switch and stays allocated across every
 * subtree. Only constants used by one comparison own temporary registers. */
static void a64_switch_tree(ecx *e, const em_switch_case *cases, uint32_t n,
                            int r, int cx, bool sg, uint32_t dflt)
{
	if (!n) {
		jump(e, dflt);
		return;
	}
	uint32_t mid = n / 2;
	const em_switch_case *p = &cases[mid];
	uint32_t left = mid ? newlabel(e) : dflt;
	val a = vreg(r), b = vconst(p->lo);
	int cond = emit_cmp(e, T_LESS, cx, sg, &a, &b);

	val_free(e, &b);
	bcond(e, cond, left);
	if (p->lo == p->hi) {
		/* The lower-bound compare also establishes equality. */
		bcond(e, C_EQ, p->target);
	} else {
		b = vconst(p->hi);
		cond = emit_cmp(e, T_LESSEQ, cx, sg, &a, &b);
		val_free(e, &b);
		bcond(e, cond, p->target);
	}
	a64_switch_tree(e, cases + mid + 1, n - mid - 1, r, cx, sg, dflt);
	if (mid) {
		deflabel(e, left);
		a64_switch_tree(e, cases, mid, r, cx, sg, dflt);
	}
}

static bool a64_switch_dispatch(ecx *e, uint32_t base, int r, int cx,
                                bool sg, uint32_t dflt)
{
	if (!e->E->o->opt || e->E->noopt || e->ncases - base < 8)
		return false;
	cc *c = e->c;
	uint32_t count = 0;
	for (uint32_t k = base; k < e->ncases; k++)
		count += N(e->cases[k])->op == N_CASE;
	if (em_switch_strategy(count, 0, 0, false, false) != EM_SWITCH_TREE)
		return false;
	em_switch_case *cases = malloc((size_t)count * sizeof *cases);
	if (!cases)
		return false;
	uint32_t j = 0;
	for (uint32_t k = base; k < e->ncases; k++) {
		uint32_t cn = e->cases[k];
		if (N(cn)->op == N_CASE)
			cases[j++] = (em_switch_case){c->ipool[N(cn)->a],
				c->ipool[N(cn)->b], e->caselbl[cn]};
	}
	bool valid = em_switch_prepare(cases, count, cx ? 64 : 32, sg);
	if (valid)
		a64_switch_tree(e, cases, count, r, cx, sg, dflt);
	free(cases);
	return valid;
}

static void gen_switch(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	qty ct = N(x->a)->ty;
	int cx = X(c, ct);
	val v = gen(e, x->a);
	uint32_t base = e->ncases, dflt = 0, lbrk = newlabel(e);

	collect_cases(e, x->b);
	for (uint32_t k = base; k < e->ncases; k++) {
		uint32_t cn = e->cases[k];

		e->caselbl[cn] = newlabel(e);
		if (N(cn)->op == N_DEFAULT)
			dflt = e->caselbl[cn];
	}
	if (!dflt)
		dflt = lbrk;
	if (v.k == V_CONST) {
		uint32_t target = dflt;
		i128 cv = em_trunc_to(c, v.i, ct);

		for (uint32_t k = base; k < e->ncases; k++) {
			const node *cn = N(e->cases[k]);

			if (cn->op == N_CASE && c->ipool[cn->a] <= cv && cv <= c->ipool[cn->b]) {
				target = e->caselbl[e->cases[k]];
				break;
			}
		}
		jump(e, target);
	} else if (em_bigint(c, ct)) {
		int source = toreg(e, &v);
		for (uint32_t k = base; k < e->ncases; k++) {
			const node *cn = N(e->cases[k]);
			if (cn->op != N_CASE) continue;
			i128 lo = c->ipool[cn->a], hi = c->ipool[cn->b];
			uint32_t next = newlabel(e);
			int copy = treg(e);
			mov(e, 1, copy, source);
			val test = a64_bigcmp(e, lo == hi ? T_EQUALEQUAL : T_GREATEREQ,
			                     ct, vreg(copy), vconst(lo));
			cbz(e, false, 1, toreg(e, &test), next);
			val_free(e, &test);
			if (lo != hi) {
				copy = treg(e);
				mov(e, 1, copy, source);
				test = a64_bigcmp(e, T_LESSEQ, ct, vreg(copy), vconst(hi));
				cbz(e, false, 1, toreg(e, &test), next);
				val_free(e, &test);
			}
			jump(e, e->caselbl[e->cases[k]]);
			deflabel(e, next);
		}
		val_free(e, &v);
		jump(e, dflt);
	} else if (is_wide(c, ct)) {
		/* 128-bit: v - lo <= hi - lo, unsigned, in two words (a single value: hi == lo) */
		int t0 = treg(e), t1 = treg(e);

		toreg2(e, &v, is_signed(c, ct));
		for (uint32_t k = base; k < e->ncases; k++) {
			const node *cn = N(e->cases[k]);
			u128 lo, d;

			if (cn->op != N_CASE)
				continue;
			lo = (u128)c->ipool[cn->a], d = (u128)c->ipool[cn->b] - lo;
			movi(e, 1, t0, (int64_t)(uint64_t)lo);
			movi(e, 1, t1, (int64_t)(uint64_t)(lo >> 64));
			op3(e, I_SUBS, 1, R_X16, v.r, t0);
			op3(e, I_SBC, 1, R_X17, v.r2, t1);
			movi(e, 1, t0, (int64_t)(uint64_t)d);
			movi(e, 1, t1, (int64_t)(uint64_t)(d >> 64));
			op3(e, I_SUBS, 1, ZR, t0, R_X16);
			op3(e, I_SBCS, 1, ZR, t1, R_X17);
			bcond(e, C_HS, e->caselbl[e->cases[k]]);
		}
		tfree(e, t0);
		tfree(e, t1);
		val_free(e, &v);
		jump(e, dflt);
	} else {
		int r = toreg(e, &v);

		bool tree = a64_switch_dispatch(e, base, r, cx, is_signed(c, ct), dflt);
		if (!tree) {
			for (uint32_t k = base; k < e->ncases; k++) {
				const node *cn = N(e->cases[k]);
				i128 lo, hi;

				if (cn->op != N_CASE)
					continue;
				lo = c->ipool[cn->a], hi = c->ipool[cn->b];
				if (lo == hi) {
					val a = vreg(r), b = vconst(lo);

					emit_cmp(e, T_EQUALEQUAL, cx, false, &a, &b);
					val_free(e, &b);
					bcond(e, C_EQ, e->caselbl[e->cases[k]]);
				} else {
					opi(e, I_SUBI, cx, R_X16, r, (int64_t)lo);
					if (hi - lo <= 4095) {
						opi(e, I_SUBSI, cx, ZR, R_X16, (int64_t)(hi - lo));
					} else {
						movi(e, cx, R_X17, (int64_t)(hi - lo));
						op3(e, I_SUBS, cx, ZR, R_X16, R_X17);
					}
					bcond(e, C_LS, e->caselbl[e->cases[k]]);
				}
			}
		}
		val_free(e, &v);
		if (!tree)
			jump(e, dflt);
	}
	e->ncases = base + (e->ncases - base);	/* keep the labels until the body is done */
	e->loops[e->nloop++] = (loopctx){ lbrk, 0, e->nclean };
	e->reach = false;
	gen_stmt(e, x->b);
	e->nloop--;
	e->ncases = base;
	deflabel(e, lbrk);
}

static void gen_return(ecx *e, const node *x)
{
	cc *c = e->c;
	fnctx *f = e->f;
	qty rt = f->rty;
	val v;

	if (x->a && a64_tail_return(e, x->a))
		return;
	v = x->a ? gen(e, x->a) : vnone();

	if (!e->reach) {
		val_free(e, &v);
		return;
	}
	if (f->inl) {
		if (!is_void(c, rt) && x->a) {
			lval l = { .k = L_FRAME, .off = f->retslot, .align = talign(c, rt) };

			if (v.k == V_CONST || v.k == V_ADDR) {
				if (!f->any_ret)
					f->cst = v;
				else if (f->cst.k != v.k || f->cst.i != v.i || f->cst.ref != v.ref)
					f->cst_ok = false;
			} else {
				f->cst_ok = false;
			}
			f->any_ret = true;
			if (f->retreg) {
				/* what a store to and load from retslot would give */
				int rr = f->retreg - 1;
				uint64_t sz = tsize(c, rt);

				if (v.k == V_CONST) {
					movi(e, 1, rr, (int64_t)em_trunc_to(c, v.i, rt));
				} else {
					int r = toreg(e, &v);

					if (sz < 8)
						extend(e, rr, r, (int)sz, is_signed(c, rt));
					else
						mov(e, 1, rr, r);
				}
			} else {
				store(e, &l, rt, &v);
			}
		}
		val_free(e, &v);
		run_cleanups(e, f->cbase);
		jump(e, f->retlbl);
		return;
	}
	if (!is_void(c, rt) && x->a && v.k != V_NONE) {
		if (e->nclean > f->cbase) {
			/* park the value while the cleanups run */
			if (is_agg(c, rt) && N(x->a)->op == N_VAR && is_local_var(c, N(x->a)->a) &&
			    c->syms[N(x->a)->a].sc != SC_STATIC) {
				/* clang returns the local itself (NRVO): its cleanups' changes are seen */
				run_cleanups(e, f->cbase);
			} else if (is_agg(c, rt)) {
				int64_t sl = slot(e, tsize(c, rt), 16);
				int rs = toreg(e, &v), rd = treg(e);

				opi(e, I_ADDI, 1, rd, FRAME, sl);
				copy_mem(e, rd, rs, tsize(c, rt));
				tfree(e, rd);
				val_free(e, &v);
				run_cleanups(e, f->cbase);
				rd = treg(e);
				opi(e, I_ADDI, 1, rd, FRAME, sl);
				v = vreg(rd);
			} else {
				int64_t sl = slot(e, 16, 16);
				lval l = { .k = L_FRAME, .off = sl, .align = 16 };

				store(e, &l, rt, &v);
				val_free(e, &v);
				run_cleanups(e, f->cbase);
				v = load(e, &l, rt);
			}
		}
		int hn, hb = is_recabi(c, rt) && !is_sret(c, rt) ? hfa(c, rt, &hn) : 0;

		if (is_bvec(c, rt) && !is_sret(c, rt)) {
			bvec_ret(e, rt, toreg(e, &v));
		} else if (hb) {
			int rs = toreg(e, &v);

			for (int k = 0; k < hn; k++)
				vr_load(e, hb, k, rs, hb * k);
		} else if (vec_small(c, rt)) {
			vec_small_ret(e, rt, toreg(e, &v));
		} else if (is_recabi(c, rt)) {
			int rs = toreg(e, &v);

			if (is_sret(c, rt)) {
				int rd = treg(e);

				mem(e, I_LD, 3, 0, rd, FRAME, f->retslot);	/* the caller's buffer (x8 at entry) */
				copy_mem(e, rd, rs, tsize(c, rt));
				tfree(e, rd);
			} else {
				mem(e, I_LD, 3, 0, 0, rs, 0);
				if (tsize(c, rt) > 8)
					mem(e, I_LD, 3, 0, 1, rs, 8);
			}
		} else if (is_wide(c, rt)) {
			toreg2(e, &v, is_signed(c, rt));
			if (is_ld(c, rt)) {
				word(e, f_in(1, 0, v.r));
				word(e, q_in_hi(0, v.r2));
			} else {
				mov(e, 1, 0, v.r);
				mov(e, 1, 1, v.r2);
			}
		} else {
			move_to(e, 0, -1, &v, rt);
			if (is_flt(c, rt))
				word(e, f_in(fpk(e, rt), 0, 0));	/* fmov d0/s0, x0/w0 */
			else if (darwin_narrow(c, rt))	/* Apple: the callee extends char/short/_Bool to 32 bits */
				extend(e, 0, 0, (int)tsize(c, rt), is_signed(c, rt));
		}
		val_free(e, &v);
	} else {
		val_free(e, &v);
		run_cleanups(e, f->cbase);
	}
	jump(e, f->retlbl);
}

/*
 * A variable length array: the slot holds { pointer, sp before the
 * allocation, size in bytes }.  The array lives below sp like alloca's
 * memory; leaving its scope (block end, break, goto, return) restores sp
 * through the cleanup stack.
 */
/* the value of a VLA size expression: from its slot once its declaration ran (vla_fix) */
static val vla_len(ecx *e, uint32_t sn)
{
	cc *c = e->c;

	for (uint32_t k = e->nvsz; k-- > 0;)
		if (e->vsz[k].node == sn && e->vsz[k].inst == e->f->inst) {
			int r = treg(e);

			mem(e, I_LD, 3, 0, r, FRAME, e->vsz[k].slot);
			return vreg(r);
		}
	return cast(e, gen(e, sn), N(sn)->ty, c->t_size, CK_INT);
}

/*
 * A declaration whose type has VLAs in it (int a[n], int (*p)[n], typedef
 * int T[n]): each size is evaluated now, once, and kept in a slot of the
 * block, so a later n = ... does not change the type (C11 6.7.6.2p5).
 * pure: only sizes without side effects (parameters: function_def already
 * evaluated the others on entry).
 */
VPRIV void vla_fix(ecx *e, qty t, bool pure)
{
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
			bool captured = false;

			for (uint32_t k = e->nvsz; k-- > 0;)
				if (e->vsz[k].node == a->n && e->vsz[k].inst == e->f->inst) {
					captured = true;
					break;
				}
			if (captured) {
				t = a->base;
				continue;
			}
			val v = cast(e, gen(e, a->n), N(a->n)->ty, c->t_size, CK_INT);
			int r = toreg(e, &v);
			int64_t sl = slot(e, 8, 8);

			mem(e, I_ST, 3, 0, r, FRAME, sl);
			val_free(e, &v);
			if (e->nvsz >= NVSZ)
				efatal(e, 0, "too many variable length array sizes in one function");
			e->vsz[e->nvsz].node = a->n;
			e->vsz[e->nvsz].inst = e->f->inst;
			e->vsz[e->nvsz++].slot = sl;
		}
		t = a->base;
	}
}

VPRIV val vla_bytes(ecx *e, qty t)
{
	cc *c = e->c;
	const ctype *a = T(c, t);
	val n, b;
	int r;

	if (a->kind != TY_ARRAY || !(a->flags & TF_VLA))
		return vconst(tsize(c, t));
	n = vla_len(e, a->n);
	b = vla_bytes(e, a->base);
	r = toreg(e, &n);
	if (b.k == V_CONST) {
		if (b.i != 1) {
			movi(e, 1, R_X16, (int64_t)b.i);
			op4(e, I_MADD, 1, r, r, R_X16, ZR);
		}
	} else {
		int rb = toreg(e, &b);

		op4(e, I_MADD, 1, r, r, rb, ZR);
		val_free(e, &b);
	}
	return vreg(r);
}

static void gen_vla_decl(ecx *e, const node *x, uint32_t s)
{
	cc *c = e->c;
	bind *b = binding(e, s);
	val n = vla_bytes(e, c->syms[s].ty);
	int r = toreg(e, &n), t = treg(e);
	int64_t sl = slot(e, 24, 8);
	uint32_t al = talign(c, c->syms[s].ty);
	ins *i;

	if (sym_align(c, s) > al)
		al = sym_align(c, s);
	b->inst = e->f->inst;
	b->kind = B_INDIRECT;
	b->kv = 0;
	b->reg = 0xff;
	b->slot = sl;
	e->dyn = true;
	mem(e, I_ST, 3, 0, r, FRAME, sl + 16);
	mov(e, 1, t, SP);
	mem(e, I_ST, 3, 0, t, FRAME, sl + 8);
	/* sp is 16-aligned: a larger alignment allocates align - 16 more and rounds the address up */
	opi(e, I_ADDI, 1, r, r, 15 + (al > 16 ? al - 16 : 0));
	opi(e, I_ANDI, 1, r, r, -16);
	op3(e, I_SUB, 1, SP, SP, r);
	if (e->reach) {
		i = put(e, I_ADDI);
		i->sz = 1, i->rd = (uint8_t)t, i->rn = SP, i->imm = 0, i->cond = 0xee;	/* + outgoing area */
	}
	if (al > 16) {
		opi(e, I_ADDI, 1, t, t, al - 1);
		opi(e, I_ANDI, 1, t, t, -(int64_t)al);
	}
	mem(e, I_ST, 3, 0, t, FRAME, sl);
	if (x->b) {
		/* = {}: zero the array's bytes, rounded up to 8 (still inside the allocation) */
		int k = treg(e);
		uint32_t lt = newlabel(e), ld = newlabel(e);

		mem(e, I_LD, 3, 0, k, FRAME, sl + 16);
		opi(e, I_ADDI, 1, k, k, 7);
		opi(e, I_ANDI, 1, k, k, -8);
		cbz(e, false, 1, k, ld);
		deflabel_loop(e, lt, true);
		mem(e, I_ST, 3, 0, ZR, t, 0);
		opi(e, I_ADDI, 1, t, t, 8);
		opi(e, I_SUBI, 1, k, k, 8);
		cbz(e, true, 1, k, lt);
		deflabel(e, ld);
		tfree(e, k);
	}
	val_free(e, &n);
	{
		val tv = vreg(t);

		val_free(e, &tv);
	}
	e->clean[e->nclean++] = s;
}

/*
 * A declaration control never reaches (switch (k) { T *p; case 1: ... }):
 * no code, but its slot is taken here, in its block's part of the frame.
 * Bound lazily at its first use instead, it would land in that statement's
 * scratch area, which later temporaries (call arguments) reuse.
 */
static void dead_decl(ecx *e, const node *x)
{
	cc *c = e->c;
	uint32_t s = x->a;
	const sym *y = &c->syms[s];
	int rr;

	if (y->kind != S_VAR || y->sc == SC_STATIC || y->sc == SC_EXTERN || y->sc == SC_TYPEDEF ||
	    (T(c, y->ty)->flags & TF_VLA))
		return;
	bind_slot(e, s);
	if ((rr = regvar_reg(c, s)) >= 0)
		binding(e, s)->reg = (uint8_t)rr;
}

static void gen_decl(ecx *e, const node *x)
{
	cc *c = e->c;
	uint32_t s = x->a;
	const sym *y = &c->syms[s];
	bind *b;

	if (!x->aux && unlikely(vm_type(c, y->ty)))
		vla_fix(e, y->ty, false);
	if (y->kind != S_VAR || y->sc == SC_STATIC || y->sc == SC_EXTERN || y->sc == SC_TYPEDEF)
		return;
	if (T(c, y->ty)->flags & TF_VLA) {
		gen_vla_decl(e, x, s);
		return;
	}
	if (x->b && !(e->E->sflags[s] & SA_WRITTEN) && !is_agg(c, y->ty) && !(QQ(y->ty) & Q_VOLATILE) &&
	    !is_wide(c, y->ty) && y->sc != SC_REGISTER && !sym_attrs(c, s)->cleanup) {
		val v = gen(e, x->b);

		if (const_var(e, s, &v)) {
			b = binding(e, s);
			b->inst = e->f->inst;
			b->kind = B_CONST;
			b->kv = 0;
			b->v = v;
			return;
		}
		{
			lval l = { .k = L_FRAME, .align = sym_align(c, s) };

			if (lreg_of(e, s) >= 0)
				l = bind_lreg(e, s);
			else
				l.off = bind_slot(e, s);
			kv_track(e, s, &v);
			store(e, &l, y->ty, &v);
			val_free(e, &v);
		}
	} else {
		lval l = { .k = L_FRAME, .align = sym_align(c, s) };
		int rr = regvar_reg(c, s);

		if (lreg_of(e, s) >= 0)
			l = bind_lreg(e, s);
		else
			l.off = bind_slot(e, s);
		if (rr >= 0)
			binding(e, s)->reg = (uint8_t)rr;
		if (x->b)
			gen_init_local(e, &l, y->ty, x->b);
		else if (rr < 0 && y->sc != SC_REGISTER)
			kv_track(e, s, NULL);
	}
	if (sym_attrs(c, s)->cleanup) {
		e->clean[e->nclean++] = s;
	}
}

static int find_loop(ecx *e, bool cont)
{
	for (int i = e->nloop; i-- > e->f->nloop;)
		if (!cont || e->loops[i].cont)
			return i;
	return -1;
}

static void gen_stmt1(ecx *e, uint32_t n);
static _Thread_local int leakchk = -1;	/* VCC_LEAKCHK: first leak per thread */

/* every statement leaves the temporaries it found; VCC_LEAKCHK reports the first that does not */
/* the .file number of a presumed file name (1-based; under dmu, shared by the threads) */
static uint32_t dbg_file(ecx *e, const char *name, uint32_t len)
{
	emitter *E = e->E;
	uint32_t f = 0;

	const char *mf = E->o->dbg_name;

	if (e->dlname && e->dlnlen == len && !memcmp(e->dlname, name, len))
		return e->dlfile;
	if (mf && strlen(mf) == len && !memcmp(mf, name, len))
		return 0;	/* the main file is file 0 (as clang; MC maps a .file N of it to 0) */
	pthread_mutex_lock(&E->dmu);
	for (uint32_t i = 0; i < E->ndfile && !f; i++)
		if (strlen(E->dfile[i]) == len && !memcmp(E->dfile[i], name, len))
			f = i + 1;
	if (!f) {
		if (E->ndfile == E->dfcap) {
			E->dfcap = E->dfcap ? 2 * E->dfcap : 16;
			E->dfile = realloc(E->dfile, E->dfcap * sizeof *E->dfile);
		}
		E->dfile[E->ndfile] = strndup(name, len);
		f = ++E->ndfile;
	}
	pthread_mutex_unlock(&E->dmu);
	e->dlname = name, e->dlnlen = len, e->dlfile = f;
	return f;
}

VPRIV bool dbg_where(ecx *e, uint32_t loc, uint32_t *file, uint32_t *line, uint32_t *col)
{
	const char *name;
	uint32_t len;

	if (!pp_where(e->c->p, loc, &name, &len, line, col) || !*line)
		return false;
	*file = dbg_file(e, name, len);
	return true;
}

VPRIV void dbg_row(ecx *e, uint32_t loc)
{
	uint32_t file, line, col;

	if (!e->reach || !dbg_where(e, loc, &file, &line, &col))
		return;
	if (e->ndl && e->dl[e->ndl - 1].k >= e->nin)
		e->ndl--;	/* nothing emitted for the previous one */
	if (e->ndl == e->dlcap) {
		e->dlcap = e->dlcap ? 2 * e->dlcap : 256;
		e->dl = realloc(e->dl, e->dlcap * sizeof *e->dl);
	}
	e->dl[e->ndl++] = (struct dbgrow){ e->nin, file, line, col };
}

VPRIV void dbg_remap(ecx *e, uint32_t *j, uint32_t k, uint32_t n)
{
	/* compaction: record k (kept or dropped) is followed by new index n; rows at it move there */
	while (*j < e->ndl && e->dl[*j].k <= k)
		e->dl[(*j)++].k = n;
}

VPRIV void gen_stmt(ecx *e, uint32_t n)
{
	cc *c = e->c;
	uint32_t live = e->live, loc = e->curloc;

	if (n)
		e->curloc = N(n)->loc;
	if (unlikely(e->E->o->debug) && n && N(n)->op != N_BLOCK && N(n)->op != N_CASE &&
	    N(n)->op != N_DEFAULT && N(n)->op != N_LABEL)
		dbg_row(e, N(n)->loc);
	gen_stmt1(e, n);
	e->curloc = loc;
	if (unlikely(leakchk < 0))
		leakchk = getenv("VCC_LEAKCHK") != NULL;
	if (unlikely(leakchk) && n && e->live != live) {
		ediag(e, N(n)->loc, "temporaries leaked: %#x -> %#x (op %d)", live, e->live, N(n)->op);
		leakchk = 0;
	}
}

static void gen_stmt1(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x;
	int64_t mark;

	if (!n)
		return;
	if (!e->reach && !(e->E->nflags[n] & NA_LABEL)) {
		if (N(n)->op == N_DECL)
			dead_decl(e, N(n));
		return;
	}
	x = N(n);
	mark = e->foff;
	switch (x->op) {
	case N_BLOCK:
		gen_block(e, n, false);
		return;
	case N_EXPR:
		/* a value thrown away: (void)(RCU) loads nothing (and refers to nothing) */
		if (x->a && side_effects(c, x->a)) {
			val v = gen(e, x->a);

			val_free(e, &v);
		}
		break;
	case N_DECL:
		gen_decl(e, x);
		return;
	case N_IF: {
		uint32_t lelse = newlabel(e), lend = newlabel(e);

		gen_cond(e, x->a, false, lelse);
		e->foff = keep_cl(e, mark);
		gen_stmt(e, x->b);
		if (x->c) {
			jump(e, lend);
			deflabel(e, lelse);
			gen_stmt(e, x->c);
			deflabel(e, lend);
		} else {
			deflabel(e, lelse);
		}
		return;
	}
	case N_WHILE: {
		uint32_t lhead = newlabel(e), lbrk = newlabel(e);

		deflabel_loop(e, lhead, e->E->nflags[n] & NA_LABEL);
		gen_cond(e, x->a, false, lbrk);
		e->foff = keep_cl(e, mark);
		e->loops[e->nloop++] = (loopctx){ lbrk, lhead, e->nclean };
		gen_stmt(e, x->b);
		e->nloop--;
		jump(e, lhead);
		deflabel(e, lbrk);
		return;
	}
	case N_DO: {
		uint32_t lhead = newlabel(e), lcont = newlabel(e), lbrk = newlabel(e);

		deflabel_loop(e, lhead, e->E->nflags[n] & NA_LABEL);
		e->loops[e->nloop++] = (loopctx){ lbrk, lcont, e->nclean };
		gen_stmt(e, x->b);
		e->nloop--;
		deflabel(e, lcont);
		gen_cond(e, x->a, true, lhead);
		e->foff = keep_cl(e, mark);
		deflabel(e, lbrk);
		return;
	}
	case N_FOR: {
		uint32_t lhead = newlabel(e), lcont = newlabel(e), lbrk = newlabel(e);
		uint32_t step = c->lists[x->c], body = c->lists[x->c + 1];
		int cmark = e->nclean;

		if (x->a) {
			const node *in = N(x->a);

			if (in->op == N_BLOCK) {
				for (uint32_t i = 0; i < in->c; i++)
					gen_stmt(e, c->lists[in->b + i]);
			} else {
				gen_stmt(e, x->a);
			}
		}
		deflabel_loop(e, lhead, e->E->nflags[n] & NA_LABEL);
		if (x->b) {
			int64_t m2 = e->foff;

			gen_cond(e, x->b, false, lbrk);
			e->foff = keep_cl(e, m2);
		}
		e->loops[e->nloop++] = (loopctx){ lbrk, lcont, e->nclean };
		gen_stmt(e, body);
		e->nloop--;
		deflabel(e, lcont);
		if (step) {
			int64_t m2 = e->foff;
			val v = gen(e, step);

			val_free(e, &v);
			e->foff = keep_cl(e, m2);
		}
		jump(e, lhead);
		deflabel(e, lbrk);
		if (e->nclean > cmark) {
			run_cleanups(e, cmark);
			e->nclean = cmark;
		}
		break;
	}
	case N_SWITCH:
		gen_switch(e, n);
		break;
	case N_CASE:
	case N_DEFAULT:
		deflabel(e, e->caselbl[n]);
		gen_stmt(e, x->c);
		return;
	case N_LABEL: {
		uint32_t l = user_label(e, x->a);

		if (e->E->lref[x->a] & LR_BACK)
			deflabel_forced(e, l);
		else
			deflabel(e, l);
		if (c->labels[x->a].addr_taken && !e->f->inl) {
			ins *i = put(e, I_RAW);
			char b[48];
			int k = snprintf(b, sizeof(b), ".Ltmp.lbl.%u:", x->a);

			i->off = (int32_t)e->raw.n;
			i->ref = (uint32_t)k;
			tb_put(&e->raw, b, (size_t)k);
		}
		gen_stmt(e, x->c);
		return;
	}
	case N_GOTO:
		if (e->reach) {
			run_cleanups(e, e->f->cbase + e->E->lcdepth[x->a] < e->nclean ? e->f->cbase + e->E->lcdepth[x->a] : e->nclean);
			jump(e, user_label(e, x->a));
		}
		return;
	case N_GOTO_IND: {
		val v = gen(e, x->a);
		int r = toreg(e, &v);

		if (e->reach)
			put(e, I_BR)->rn = (uint8_t)r;
		val_free(e, &v);
		e->reach = false;
		break;
	}
	case N_BREAK:
	case N_CONTINUE: {
		int i = find_loop(e, x->op == N_CONTINUE);

		if (i < 0)
			efatal(e, x->loc, "break/continue outside a loop");
		run_cleanups(e, e->loops[i].cdepth);
		jump(e, x->op == N_BREAK ? e->loops[i].brk : e->loops[i].cont);
		return;
	}
	case N_RETURN:
		gen_return(e, x);
		break;
	case N_ASM:
		gen_asm(e, n);
		break;
	default:
		efatal(e, x->loc, "unsupported statement (node %d)", x->op);
	}
	e->foff = keep_cl(e, mark);
}
