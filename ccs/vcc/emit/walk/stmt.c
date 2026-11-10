/* vcc code generator, AST walker: statements, switch, the function (walk.h) */
#include "walk.h"

/* ---------------- state ---------------- */

VPRIV wkst *wk_state(ecx *e)
{
	cc *c = e->c;
	wkst *W = e->wk;

	if (!W) {
		W = calloc(1, sizeof *W);
		W->cases = reserve(WK_MAXCASE * sizeof(wkcase));
		e->wk = W;
	}
	if (!W->sgen) {
		/* sized for this TU (gen-tagged: never cleared); one TU per ecx */
		W->sgen = reserve(((size_t)c->nsym + 64) * sizeof(uint32_t));
		W->soff = reserve(((size_t)c->nsym + 64) * sizeof(int32_t));
		W->lgen = reserve(((size_t)c->nlabel + 64) * sizeof(uint32_t));
		W->ulbl = reserve(((size_t)c->nlabel + 64) * sizeof(uint32_t));
		W->cgen = reserve(((size_t)c->nnode + 64) * sizeof(uint32_t));
		W->clbl = reserve(((size_t)c->nnode + 64) * sizeof(uint32_t));
	}
	return W;
}

VPRIV void x64_ecx_free(ecx *e);

/* the walker's and the lowering's state of this ecx, given back (the TU is done) */
VPRIV void wk_ecx_free(ecx *e)
{
	cc *c = e->c;
	wkst *W = e->wk;

	x64_ecx_free(e);
	if (!W)
		return;
	if (W->sgen) {
		munmap(W->sgen, ((size_t)c->nsym + 64) * sizeof(uint32_t));
		munmap(W->soff, ((size_t)c->nsym + 64) * sizeof(int32_t));
		munmap(W->lgen, ((size_t)c->nlabel + 64) * sizeof(uint32_t));
		munmap(W->ulbl, ((size_t)c->nlabel + 64) * sizeof(uint32_t));
		munmap(W->cgen, ((size_t)c->nnode + 64) * sizeof(uint32_t));
		munmap(W->clbl, ((size_t)c->nnode + 64) * sizeof(uint32_t));
	}
	munmap(W->cases, WK_MAXCASE * sizeof(wkcase));
	free(W);
	e->wk = NULL;
}

/* a frame slot of size bytes: its offset from the frame base (negative) */
VPRIV int32_t wk_slot(ecx *e, uint64_t size, uint32_t align)
{
	wkst *W = e->wk;
	int64_t o;

	if (align < 1)
		align = 1;
	if (align > 16)
		efatal(e, e->curloc, "x86-64: alignment above 16 is not supported yet (PLAN-x86.md X3)");
	if (size > ((uint64_t)1 << 30))
		efatal(e, e->curloc, "x86-64: frame too large");
	o = (int64_t)W->foff + (int64_t)size;
	o = (o + align - 1) & ~(int64_t)(align - 1);
	W->foff = (int32_t)o;
	if (align > W->falign)
		W->falign = align;
	return -(int32_t)o;
}

VPRIV uint32_t wk_newlabel(ecx *e) { return ++e->wk->nlabel; }

static uint32_t wk_ulabel(ecx *e, uint32_t l)
{
	wkst *W = e->wk;

	if (W->lgen[l] != W->gen) {
		W->lgen[l] = W->gen;
		W->ulbl[l] = wk_newlabel(e);
	}
	return W->ulbl[l];
}

static uint32_t wk_caselabel(ecx *e, uint32_t n)
{
	wkst *W = e->wk;

	if (W->cgen[n] != W->gen) {
		W->cgen[n] = W->gen;
		W->clbl[n] = wk_newlabel(e);
	}
	return W->clbl[n];
}

/* ---------------- initializers ---------------- */

static bool wk_zero_init(cc *c, uint32_t v)
{
	const node *x = N(v);

	if (!v || x->op == N_ZERO)
		return true;
	if (x->op == N_NUM)
		return c->ipool[x->a] == 0;
	if (x->op == N_CAST && (x->aux == CK_INT || x->aux == CK_NOP || x->aux == CK_NULL || x->aux == CK_I2P))
		return x->aux == CK_NULL || wk_zero_init(c, x->a);
	return false;
}

/* the address of d (a frame slot or a symbol) in a new temporary */
static int wk_addr(ecx *e, const lval *d)
{
	if (d->k == L_REG)
		efatal(e, e->curloc, "internal error: an initializer through a register");
	return LO(laddr)(e, d);
}

/* the elements of init stored into the (already zeroed) object at d */
static void wk_elems(ecx *e, const lval *d, qty ty, uint32_t init)
{
	cc *c = e->c;
	const node *x = N(init);

	if (!init || x->op == N_ZERO)
		return;
	if (x->op == N_INIT) {
		const uint32_t *v = &c->lists[x->b];

		if (x->aux == 1) {
			qty et = T(c, ty)->base;
			uint64_t es = tsize(c, et);

			for (uint32_t i = 0; i < x->c; i++) {
				uint32_t lo = v[3 * i], hi = v[3 * i + 1], vi = v[3 * i + 2];

				for (uint64_t k = lo; k <= hi; k++) {
					lval el = *d;

					el.off += (int64_t)(k * es);
					wk_elems(e, &el, et, vi);
				}
			}
			return;
		}
		{
			const tag *r = &c->tags[T(c, ty)->aux];

			for (uint32_t i = 0; i < x->c && i < r->nfield; i++) {
				const field *f = &r->f[i];
				lval el = *d;

				if ((x->aux == 2 && i != x->a) || !v[i])
					continue;
				el.off += (int64_t)(f->off / 8);
				if (f->bitfield) {
					if (wk_zero_init(c, v[i]))
						continue;
					efatal(e, N(v[i])->loc, "x86-64: a bit-field is not supported yet (PLAN-x86.md X3)");
				}
				wk_elems(e, &el, f->ty, v[i]);
			}
		}
		return;
	}
	if (x->op == N_STR && is_array(c, ty)) {
		/* the literal's bytes (up to the array's size; the rest is zero) */
		const cstr *s = &c->strs[x->a];
		uint64_t n = (uint64_t)s->n * s->esz, cap = tsize(c, ty);
		lval src = { .k = L_SYM, .ref = REF(RK_STR, x->a), .align = s->esz };
		int rd, rs;

		if (n > cap)
			n = cap;
		note_ref(e, src.ref);
		rd = wk_addr(e, d);
		rs = LO(laddr)(e, &src);
		LO(copy)(e, rd, rs, n);
		LO(tfree)(e, rs);
		LO(tfree)(e, rd);
		return;
	}
	if (wk_zero_init(c, init) && !is_agg(c, ty))
		return;
	if (is_agg(c, ty)) {
		/* another object's bytes (a struct variable, a call's result ...) */
		val v = wk_gen(e, init);
		int rs = wk_reg(e, &v, prim(TY_ULONG)), rd = wk_addr(e, d);

		LO(copy)(e, rd, rs, tsize(c, ty));
		LO(tfree)(e, rs);
		LO(tfree)(e, rd);
		return;
	}
	{
		val v = wk_gen(e, init);
		int r = wk_reg(e, &v, ty);

		LO(store)(e, r, d, wk_size(c, ty));
		LO(tfree)(e, r);
	}
}

VPRIV void wk_init(ecx *e, const lval *d, qty t, uint32_t init)
{
	cc *c = e->c;
	const node *x = N(init);

	if (!init)
		return;
	if (x->op == N_INIT || x->op == N_ZERO || (x->op == N_STR && is_array(c, t))) {
		int rd = wk_addr(e, d);

		LO(zero)(e, rd, tsize(c, t));
		LO(tfree)(e, rd);
		wk_elems(e, d, t, init);
		return;
	}
	if (is_agg(c, t)) {
		wk_elems(e, d, t, init);	/* another object's bytes */
		return;
	}
	{
		/* a scalar: stored whatever its value (the object was not zeroed) */
		val v = wk_gen(e, init);
		int r = wk_reg(e, &v, t);

		LO(store)(e, r, d, wk_size(c, t));
		LO(tfree)(e, r);
	}
}

/* ---------------- declarations ---------------- */

static void wk_decl(ecx *e, const node *x)
{
	cc *c = e->c;
	uint32_t s = x->a;
	const sym *y = &c->syms[s];

	if (y->kind != S_VAR || y->sc == SC_STATIC || y->sc == SC_EXTERN || y->sc == SC_TYPEDEF)
		return;
	if (vm_type(c, y->ty))
		efatal(e, x->loc, "x86-64: variable-length arrays are not supported yet (PLAN-x86.md X3)");
	if (sym_attrs(c, s)->cleanup)
		efatal(e, x->loc, "x86-64: cleanup variables are not supported yet (PLAN-x86.md X3)");
	{
		lval l = { .k = L_FRAME, .align = sym_align(c, s) };

		l.off = wk_local(e, s);
		wk_init(e, &l, y->ty, x->b);
	}
}

/* ---------------- switch ---------------- */

static void wk_cases(ecx *e, uint32_t n)
{
	cc *c = e->c;
	wkst *W = e->wk;
	const node *x;

	if (!n)
		return;
	x = N(n);
	switch (x->op) {
	case N_CASE: case N_DEFAULT:
		if (W->ncases == WK_MAXCASE)
			efatal(e, x->loc, "too many case labels");
		W->cases[W->ncases++] = (wkcase){ x->op == N_CASE ? c->ipool[x->a] : 0, x->op == N_CASE ? c->ipool[x->b] : 0, n };
		wk_cases(e, x->c);
		return;
	case N_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			wk_cases(e, c->lists[x->b + i]);
		return;
	case N_IF:
		wk_cases(e, x->b);
		wk_cases(e, x->c);
		return;
	case N_WHILE: case N_DO:
		wk_cases(e, x->b);
		return;
	case N_FOR:
		wk_cases(e, c->lists[x->c + 1]);
		return;
	case N_LABEL:
		wk_cases(e, x->c);
		return;
	default:
		return;
	}
}

/* a compare chain: the value once in a register, each case tested in turn */
static void wk_switch(ecx *e, uint32_t n)
{
	cc *c = e->c;
	wkst *W = e->wk;
	const node *x = N(n);
	qty ct = N(x->a)->ty;
	int w = wk_size(c, ct) > 4 ? 8 : 4;
	bool sg = wk_signed(c, ct);
	uint32_t c0 = W->ncases, lbrk = wk_newlabel(e), ldef = lbrk;
	val v = wk_gen(e, x->a);
	int r = wk_reg(e, &v, ct);

	wk_cases(e, x->b);
	for (uint32_t i = c0; i < W->ncases; i++) {
		wkcase *k = &W->cases[i];
		uint32_t l = wk_caselabel(e, k->node);

		if (N(k->node)->op == N_DEFAULT) {
			ldef = l;
			continue;
		}
		if (k->lo == k->hi) {
			LO(cmpbr)(e, T_EQUALEQUAL, w, sg, (val){ .k = V_REG, .r = (uint8_t)r }, vconst(k->lo), l);
			LO(tkeep)(e, r);
		} else {
			/* lo <= v <= hi */
			uint32_t skip = wk_newlabel(e);

			LO(cmpbr)(e, T_LESS, w, sg, (val){ .k = V_REG, .r = (uint8_t)r }, vconst(k->lo), skip);
			LO(tkeep)(e, r);
			LO(cmpbr)(e, T_LESSEQ, w, sg, (val){ .k = V_REG, .r = (uint8_t)r }, vconst(k->hi), l);
			LO(tkeep)(e, r);
			LO(label)(e, skip);
		}
	}
	W->ncases = c0;
	LO(tfree)(e, r);
	LO(jump)(e, ldef);
	if (W->nloop == WK_MAXLOOP)
		efatal(e, x->loc, "statements nested too deeply");
	W->loop[W->nloop++] = (wkloop){ lbrk, 0 };
	wk_stmt(e, x->b);
	W->nloop--;
	LO(label)(e, lbrk);
}

/* ---------------- statements ---------------- */

static void wk_loop_push(ecx *e, uint32_t brk, uint32_t cont)
{
	wkst *W = e->wk;

	if (W->nloop == WK_MAXLOOP)
		efatal(e, e->curloc, "statements nested too deeply");
	W->loop[W->nloop++] = (wkloop){ brk, cont };
}

VPRIV void wk_stmt(ecx *e, uint32_t n)
{
	cc *c = e->c;
	wkst *W = e->wk;
	const node *x;

	if (!n)
		return;
	x = N(n);
	e->curloc = x->loc;
	switch (x->op) {
	case N_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			wk_stmt(e, c->lists[x->b + i]);
		return;
	case N_EXPR:
		if (x->a) {
			val v = wk_gen(e, x->a);

			wk_free(e, &v);
		}
		return;
	case N_DECL:
		wk_decl(e, x);
		return;
	case N_IF: {
		uint32_t lelse = wk_newlabel(e), lend = wk_newlabel(e);

		wk_cond(e, x->a, false, lelse);
		wk_stmt(e, x->b);
		if (x->c) {
			LO(jump)(e, lend);
			LO(label)(e, lelse);
			wk_stmt(e, x->c);
			LO(label)(e, lend);
		} else {
			LO(label)(e, lelse);
		}
		return;
	}
	case N_WHILE: {
		uint32_t lhead = wk_newlabel(e), lbrk = wk_newlabel(e);

		LO(label)(e, lhead);
		wk_cond(e, x->a, false, lbrk);
		wk_loop_push(e, lbrk, lhead);
		wk_stmt(e, x->b);
		W->nloop--;
		LO(jump)(e, lhead);
		LO(label)(e, lbrk);
		return;
	}
	case N_DO: {
		uint32_t lhead = wk_newlabel(e), lcont = wk_newlabel(e), lbrk = wk_newlabel(e);

		LO(label)(e, lhead);
		wk_loop_push(e, lbrk, lcont);
		wk_stmt(e, x->b);
		W->nloop--;
		LO(label)(e, lcont);
		wk_cond(e, x->a, true, lhead);
		LO(label)(e, lbrk);
		return;
	}
	case N_FOR: {
		uint32_t lhead = wk_newlabel(e), lcont = wk_newlabel(e), lbrk = wk_newlabel(e);
		uint32_t step = c->lists[x->c], body = c->lists[x->c + 1];

		wk_stmt(e, x->a);
		LO(label)(e, lhead);
		if (x->b)
			wk_cond(e, x->b, false, lbrk);
		wk_loop_push(e, lbrk, lcont);
		wk_stmt(e, body);
		W->nloop--;
		LO(label)(e, lcont);
		if (step) {
			val v = wk_gen(e, step);

			wk_free(e, &v);
		}
		LO(jump)(e, lhead);
		LO(label)(e, lbrk);
		return;
	}
	case N_SWITCH:
		wk_switch(e, n);
		return;
	case N_CASE: case N_DEFAULT:
		LO(label)(e, wk_caselabel(e, n));
		wk_stmt(e, x->c);
		return;
	case N_LABEL:
		if (c->labels[x->a].addr_taken)
			efatal(e, x->loc, "x86-64: label addresses are not supported yet (PLAN-x86.md X3)");
		LO(label)(e, wk_ulabel(e, x->a));
		wk_stmt(e, x->c);
		return;
	case N_GOTO:
		LO(jump)(e, wk_ulabel(e, x->a));
		return;
	case N_BREAK: case N_CONTINUE: {
		int i = W->nloop - 1;

		while (i >= 0 && x->op == N_CONTINUE && !W->loop[i].cont)
			i--;
		if (i < 0)
			efatal(e, x->loc, "break/continue outside a loop");
		LO(jump)(e, x->op == N_BREAK ? W->loop[i].brk : W->loop[i].cont);
		return;
	}
	case N_RETURN:
		if (x->a && KIND(c, W->rty) != TY_VOID) {
			val v = wk_gen(e, x->a);

			LO(ret)(e, &v, wk_size(c, W->rty), wk_signed(c, W->rty), W->retlbl);
		} else {
			if (x->a) {
				val v = wk_gen(e, x->a);

				wk_free(e, &v);
			}
			LO(ret)(e, NULL, 0, false, W->retlbl);
		}
		return;
	case N_ASM:
		efatal(e, x->loc, "x86-64: inline asm is not supported yet (PLAN-x86.md X6)");
	case N_GOTO_IND:
		efatal(e, x->loc, "x86-64: computed goto is not supported yet (PLAN-x86.md X3)");
	default:
		efatal(e, x->loc, "x86-64: unsupported statement (node %d)", x->op);
	}
}

/* ---------------- the function ---------------- */

static bool wk_is_main(cc *c, uint32_t s)
{
	const sym *y = &c->syms[s];

	return y->name && y->depth == 0 && y->sc != SC_STATIC && vintern_ents[y->name].len == 4 &&
	       !memcmp(vintern_ents[y->name].s, "main", 4);	/* (spellings are not NUL-terminated) */
}

VPRIV void wk_function(ecx *e, uint32_t s)
{
	cc *c = e->c;
	const sym *y = &c->syms[s];
	const ctype *ft = T(c, y->ty);
	const node *body = N(y->body);
	wkst *W = wk_state(e);
	int nreg = LO(nregargs)(), ir = 0, is = 0;

	walk_fn(e, s);
	W->gen++;
	W->foff = 0;
	W->falign = 16;
	W->nlabel = 0;
	W->nloop = 0;
	W->ncases = 0;
	W->rty = ft->base;
	W->fsym = s;
	e->top = s;
	e->reach = true;
	e->calls = false;
	e->curloc = y->loc;
	LO(begin)(e, s);
	if (_setjmp(e->bail))
		return;	/* efatal: the function is not emitted, the error counted */
	if (ft->flags & TF_VARIADIC)
		efatal(e, y->loc, "x86-64: variadic functions are not supported yet (PLAN-x86.md X3)");
	if (KIND(c, W->rty) != TY_VOID) {
		int k = KIND(c, W->rty);

		if (is_flt(c, W->rty) || is_record(c, W->rty) || k == TY_INT128 || k == TY_UINT128 || k == TY_VECTOR)
			efatal(e, y->loc, "x86-64: this return type is not supported yet (PLAN-x86.md X3)");
	}
	W->retlbl = wk_newlabel(e);
	/* parameters: in registers to frame slots, on the stack where they are */
	for (uint32_t i = 0; i < body->aux; i++) {
		uint32_t p = c->lists[body->a + i];
		qty pt = c->syms[p].ty;
		int k = KIND(c, pt);

		if (is_flt(c, pt) || is_record(c, pt) || k == TY_INT128 || k == TY_UINT128 || k == TY_VECTOR)
			efatal(e, c->syms[p].loc, "x86-64: this parameter type is not supported yet (PLAN-x86.md X3)");
		W->sgen[p] = W->gen;
		if (ir < nreg) {
			W->soff[p] = wk_slot(e, 8, 8);
			LO(param)(e, ir++, W->soff[p], wk_size(c, pt));
		} else {
			W->soff[p] = LO(stackarg)(nreg + is++);
		}
	}
	wk_stmt(e, y->body);
	if (wk_is_main(c, s) && KIND(c, W->rty) == TY_INT) {
		val z = vconst(0);

		LO(ret)(e, &z, 4, true, W->retlbl);	/* falling off the end of main returns 0 */
	}
	LO(label)(e, W->retlbl);
	LO(finish)(e, s);
}
