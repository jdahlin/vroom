/* vcc code generator, AST walker: statements, switch, the function (walk.h) */
#include "walk.h"

/* ---------------- state ---------------- */

/* Array element constants are local to a straight-line region. Forget them at
   every control-flow join/loop header rather than guessing predecessor facts. */
VPRIV void wk_label(ecx *e, uint32_t label)
{
	e->kvalid = 0;
	LO(label)(e, label);
}

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
		W->soff = reserve(((size_t)c->nsym + 64) * sizeof(int64_t));
		W->lgen = reserve(((size_t)c->nlabel + 64) * sizeof(uint32_t));
		W->ulbl = reserve(((size_t)c->nlabel + 64) * sizeof(uint32_t));
		W->cgen = reserve(((size_t)c->nnode + 64) * sizeof(uint32_t));
		W->clbl = reserve(((size_t)c->nnode + 64) * sizeof(uint32_t));
		W->kgen = reserve(((size_t)c->nsym + 64) * sizeof(uint32_t));
		W->kval = reserve(((size_t)c->nsym + 64) * sizeof(val));
		W->linst = reserve(((size_t)c->nlabel + 64) * sizeof(uint32_t));
		W->cinst = reserve(((size_t)c->nnode + 64) * sizeof(uint32_t));
		W->csn = reserve(((size_t)c->nsym + 64) * sizeof(uint32_t));
		W->csg = reserve(((size_t)c->nsym + 64) * sizeof(uint32_t));
		W->lcn = reserve(((size_t)c->nlabel + 64) * sizeof(uint32_t));
		W->lcg = reserve(((size_t)c->nlabel + 64) * sizeof(uint32_t));
		W->zgen = reserve(((size_t)c->nsym + 64) * sizeof(uint32_t));
		W->vgen = reserve(((size_t)c->nnode + 64) * sizeof(uint32_t));
		W->voff = reserve(((size_t)c->nnode + 64) * sizeof(int64_t));
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
		munmap(W->soff, ((size_t)c->nsym + 64) * sizeof(int64_t));
		munmap(W->lgen, ((size_t)c->nlabel + 64) * sizeof(uint32_t));
		munmap(W->ulbl, ((size_t)c->nlabel + 64) * sizeof(uint32_t));
		munmap(W->cgen, ((size_t)c->nnode + 64) * sizeof(uint32_t));
		munmap(W->clbl, ((size_t)c->nnode + 64) * sizeof(uint32_t));
		munmap(W->kgen, ((size_t)c->nsym + 64) * sizeof(uint32_t));
		munmap(W->kval, ((size_t)c->nsym + 64) * sizeof(val));
		munmap(W->linst, ((size_t)c->nlabel + 64) * sizeof(uint32_t));
		munmap(W->cinst, ((size_t)c->nnode + 64) * sizeof(uint32_t));
		munmap(W->csn, ((size_t)c->nsym + 64) * sizeof(uint32_t));
		munmap(W->csg, ((size_t)c->nsym + 64) * sizeof(uint32_t));
		munmap(W->lcn, ((size_t)c->nlabel + 64) * sizeof(uint32_t));
		munmap(W->lcg, ((size_t)c->nlabel + 64) * sizeof(uint32_t));
		munmap(W->zgen, ((size_t)c->nsym + 64) * sizeof(uint32_t));
		munmap(W->vgen, ((size_t)c->nnode + 64) * sizeof(uint32_t));
		munmap(W->voff, ((size_t)c->nnode + 64) * sizeof(int64_t));
	}
	munmap(W->cases, WK_MAXCASE * sizeof(wkcase));
	free(W);
	e->wk = NULL;
}

/* a frame slot of size bytes: its offset from the frame base (negative) */
VPRIV int64_t wk_slot(ecx *e, uint64_t size, uint32_t align)
{
	wkst *W = e->wk;
	int64_t o;

	if (align < 1)
		align = 1;
	/* Reserve alignment rounding and the prologue's final 16-byte rounding
	   before adding, so even malformed enormous objects cannot overflow. */
	uint64_t limit = (uint64_t)INT64_MAX - align - 15;
	if ((uint64_t)W->foff > limit || size > limit - (uint64_t)W->foff)
		efatal(e, e->curloc, "x86-64: frame too large");
	o = W->foff + (int64_t)size;
	o = (o + align - 1) & ~(int64_t)(align - 1);
	W->foff = o;
	if (align > W->falign)
		W->falign = align;
	return -(int64_t)o;
}

VPRIV uint32_t wk_newlabel(ecx *e) { return ++e->wk->nlabel; }

static uint32_t wk_ulabel(ecx *e, uint32_t l)
{
	wkst *W = e->wk;

	if (W->lgen[l] != W->gen || W->linst[l] != W->inst) {
		W->lgen[l] = W->gen;
		W->linst[l] = W->inst;
		W->ulbl[l] = wk_newlabel(e);
	}
	return W->ulbl[l];
}

VPRIV uint32_t wk_userlabel(ecx *e, uint32_t l) { return wk_ulabel(e, l); }

static uint32_t wk_caselabel(ecx *e, uint32_t n)
{
	wkst *W = e->wk;

	if (W->cgen[n] != W->gen || W->cinst[n] != W->inst) {
		W->cgen[n] = W->gen;
		W->cinst[n] = W->inst;
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
					el.bf = 1;
					el.bitoff = (uint16_t)(f->off % 8);
					el.width = f->width;
					el.sgn = is_signed(c, f->ty);
					wk_bfinit(e, &el, f->ty, v[i]);
					continue;
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
	if (wk_zero_init(c, init) && !wk_mem(e, ty))
		return;
	if (wk_mem(e, ty)) {
		/* another object's bytes (a struct variable, a call's result ...) */
		val v = wk_gen(e, init);
		int rs = wk_reg(e, &v, prim(TY_ULONG)), rd = wk_addr(e, d);

		uint64_t bytes = tsize(c, ty);
		if (is_record(c, ty) && (T(c, ty)->flags & TF_ATOMIC)) {
			/* Initialization is not a concurrent store; copy only the source
			   payload and initialize any padding added by _Atomic. */
			LO(zero)(e, rd, bytes);
			bytes = tsize(c, QT(T(c, ty)->n, 0));
		}
		LO(copy)(e, rd, rs, bytes);
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
	if (wk_mem(e, t)) {
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

/* cleanup(f) variables: f(&var), innermost first, down to depth `to` (the stack stays) */
VPRIV void wk_cleanups(ecx *e, int to)
{
	cc *c = e->c;
	wkst *W = e->wk;

	for (int i = W->nclean; i-- > to;) {
		uint32_t s = W->clean[i], fn = canon_sym(c, sym_attrs(c, s)->cleanup);
		lval l = { .k = L_FRAME, .off = wk_local(e, s), .align = 8 };
		wkarg a;
		val r;

		LO(classify)(e, prim(TY_ULONG), &a);
		a.slot = wk_slot(e, 8, 8);
		{
			int ra = LO(laddr)(e, &l);
			lval d = { .k = L_FRAME, .off = a.slot, .align = 8 };

			LO(store)(e, ra, &d, 8);
			LO(tfree)(e, ra);
		}
		note_ref(e, REF(RK_SYM, fn));
		e->calls = true;
		r = LO(call)(e, REF(RK_SYM, fn), NULL, &a, 1, false, NULL, prim(TY_VOID));
		wk_free(e, &r);
	}
}

static void wk_decl(ecx *e, const node *x)
{
	cc *c = e->c;
	uint32_t s = x->a;
	const sym *y = &c->syms[s];

	if (unlikely(vm_type(c, y->ty)))
		wk_vla_fix(e, y->ty, false);
	if (y->kind != S_VAR || y->sc == SC_STATIC || y->sc == SC_EXTERN || y->sc == SC_TYPEDEF)
		return;
	if (T(c, y->ty)->flags & TF_VLA) {
		wk_vla_decl(e, s, x->b != 0);
		return;
	}
	e->wk->kgen[s] = 0;
	if (e->E->o->opt && x->b && !side_effects(c, x->b) && !sym_attrs(c, s)->cleanup) {
		/* A side-effect-free constant initializer for a local never written. */
		cval cv;
		uint8_t f = e->E->sflags[s];

		if (!(f & (SA_WRITTEN | SA_CWRITE)) && !(QQ(y->ty) & Q_VOLATILE) && y->sc != SC_REGISTER &&
		    (is_integer(c, y->ty) || is_ptr(c, y->ty)) && !wk_mem(e, y->ty) && cc_eval(c, x->b, &cv) && cv.kind == CV_INT) {
			e->wk->kgen[s] = e->wk->gen;
			e->wk->kval[s] = vconst(em_trunc_to(c, cv.i, y->ty));
			return;
		}
	}
	{
		lval l = { .k = L_FRAME, .align = sym_align(c, s) };

		if (!LO(local)(e, s, &l))
			l.off = wk_local(e, s);
		wk_init(e, &l, y->ty, x->b);
	}
	if (sym_attrs(c, s)->cleanup) {
		if (e->wk->nclean == WK_MAXCLEAN)
			efatal(e, x->loc, "too many cleanup variables in scope");
		e->wk->clean[e->wk->nclean++] = s;
	}
}

/*
 * The VLAs declared in the statements under n: their saved sp zeroed on
 * entry, so a declaration reached again (a goto back) frees the previous
 * instance first.  (One inside a statement expression is not seen: it is
 * not freed by a goto back.)
 */
static void wk_vla_scan(ecx *e, uint32_t n, uint32_t *top)
{
	cc *c = e->c;
	wkst *W = e->wk;
	const node *x;

	if (!n)
		return;
	x = N(n);
	switch (x->op) {
	case N_DECL: {
		const sym *y = &c->syms[x->a];

		if (y->kind == S_VAR && y->sc != SC_TYPEDEF && y->sc != SC_STATIC && y->sc != SC_EXTERN &&
		    (T(c, y->ty)->flags & TF_VLA)) {
			lval l = { .k = L_FRAME, .off = wk_local(e, x->a) + 8, .align = 8 };
			val z = vconst(0);
			int r = LO(toreg)(e, &z, 8);

			LO(store)(e, r, &l, 8);
			LO(tfree)(e, r);
			W->zgen[x->a] = W->gen;
		}
		if (y->kind == S_VAR && sym_attrs(c, x->a)->cleanup && W->ncn + 1 < WK_MAXCLEAN) {
			W->ncn++;
			W->cn[W->ncn].sym = x->a;
			W->cn[W->ncn].parent = *top;
			W->csn[x->a] = W->ncn;
			W->csg[x->a] = W->gen;
			*top = W->ncn;	/* in scope for the rest of the block */
		}
		return;
	}
	case N_BLOCK: {
		uint32_t t = *top;

		for (uint32_t i = 0; i < x->c; i++)
			wk_vla_scan(e, c->lists[x->b + i], &t);
		return;
	}
	case N_IF: {
		uint32_t t = *top, u = *top;

		wk_vla_scan(e, x->b, &t);
		wk_vla_scan(e, x->c, &u);
		return;
	}
	case N_WHILE: case N_DO: case N_SWITCH: {
		uint32_t t = *top;

		wk_vla_scan(e, x->b, &t);
		return;
	}
	case N_FOR: {
		uint32_t t = *top;

		wk_vla_scan(e, c->lists[x->c + 1], &t);
		return;
	}
	case N_LABEL:
		W->lcn[x->a] = *top;
		W->lcg[x->a] = W->gen;
		wk_vla_scan(e, x->c, top);
		return;
	case N_CASE: case N_DEFAULT:
		wk_vla_scan(e, x->c, top);
		return;
	default:
		return;
	}
}

/* A discarded branch can still be entered through a C label, a switch case,
   or a label inside a GNU statement expression. Unknown forms conservatively
   keep the branch; declarations/VLA bounds need their own scope traversal. */
VPRIV bool wk_branch_entry(cc *c, uint32_t n)
{
	if (!n) return false;
	const node *x = N(n);
	switch (x->op) {
	case N_NONE: case N_NUM: case N_FNUM: case N_STR: case N_VAR:
	case N_LABELADDR: case N_ZERO: case N_GOTO: case N_BREAK: case N_CONTINUE:
		return false;
	case N_LABEL: case N_CASE: case N_DEFAULT:
		return true;
	case N_BLOCK: case N_BUILTIN:
		for (uint32_t i = 0; i < x->c; i++)
			if (wk_branch_entry(c, c->lists[x->b + i])) return true;
		return false;
	case N_CALL:
		if (wk_branch_entry(c, x->a)) return true;
		for (uint32_t i = 0; i < x->c; i++)
			if (wk_branch_entry(c, c->lists[x->b + i])) return true;
		return false;
	case N_MEMBER: case N_UNARY: case N_ADDR: case N_DEREF:
	case N_PREINC: case N_PREDEC: case N_POSTINC: case N_POSTDEC:
	case N_STMTEXPR: case N_VAARG: case N_EXPR: case N_RETURN: case N_GOTO_IND:
		return wk_branch_entry(c, x->a);
	case N_CAST:
		return (T(c, x->ty)->flags & TF_VLA) || wk_branch_entry(c, x->a);
	case N_INDEX: case N_BINARY: case N_LOGAND: case N_LOGOR:
	case N_ASSIGN: case N_COMMA: case N_CPLX:
	case N_WHILE: case N_DO: case N_SWITCH:
		return wk_branch_entry(c, x->a) || wk_branch_entry(c, x->b);
	case N_COND: case N_IF:
		return wk_branch_entry(c, x->a) || wk_branch_entry(c, x->b) || wk_branch_entry(c, x->c);
	case N_FOR:
		return wk_branch_entry(c, x->a) || wk_branch_entry(c, x->b) ||
		       wk_branch_entry(c, c->lists[x->c]) || wk_branch_entry(c, c->lists[x->c + 1]);
	case N_ASM: {
		const casm *a = &c->asms[x->a];
		for (uint32_t i = 0; i < a->nout + a->nin; i++)
			if (wk_branch_entry(c, a->ops[i].expr)) return true;
		return false;
	}
	default:
		return true;
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
		if (wk_mem(e, ct)) {
			/* __int128: each test a 0/1 value (the value is an object, r its address) */
			val t = LO(wcmp)(e, k->lo == k->hi ? T_EQUALEQUAL : T_GREATEREQ, sg, (val){ .k = V_REG, .r = (uint8_t)r }, vconst(k->lo));
			uint32_t skip = wk_newlabel(e);

			LO(tkeep)(e, r);
			if (k->lo == k->hi) {
				LO(brz)(e, t, 4, true, l);
				continue;
			}
			LO(brz)(e, t, 4, false, skip);
			t = LO(wcmp)(e, T_LESSEQ, sg, (val){ .k = V_REG, .r = (uint8_t)r }, vconst(k->hi));
			LO(tkeep)(e, r);
			LO(brz)(e, t, 4, true, l);
			wk_label(e, skip);
		} else if (k->lo == k->hi) {
			LO(cmpbr)(e, T_EQUALEQUAL, w, sg, (val){ .k = V_REG, .r = (uint8_t)r }, vconst(k->lo), l);
			LO(tkeep)(e, r);
		} else {
			/* lo <= v <= hi */
			uint32_t skip = wk_newlabel(e);

			LO(cmpbr)(e, T_LESS, w, sg, (val){ .k = V_REG, .r = (uint8_t)r }, vconst(k->lo), skip);
			LO(tkeep)(e, r);
			LO(cmpbr)(e, T_LESSEQ, w, sg, (val){ .k = V_REG, .r = (uint8_t)r }, vconst(k->hi), l);
			LO(tkeep)(e, r);
			wk_label(e, skip);
		}
	}
	W->ncases = c0;
	LO(tfree)(e, r);
	LO(jump)(e, ldef);
	if (W->nloop == WK_MAXLOOP)
		efatal(e, x->loc, "statements nested too deeply");
	W->loop[W->nloop++] = (wkloop){ lbrk, 0, W->nclean };
	wk_stmt(e, x->b);
	W->nloop--;
	wk_label(e, lbrk);
}

/* ---------------- statements ---------------- */

static void wk_loop_push(ecx *e, uint32_t brk, uint32_t cont)
{
	wkst *W = e->wk;

	if (W->nloop == WK_MAXLOOP)
		efatal(e, e->curloc, "statements nested too deeply");
	W->loop[W->nloop++] = (wkloop){ brk, cont, W->nclean };
}

VPRIV void wk_goto(ecx *e, uint32_t label)
{
	wkst *W = e->wk;

	if (W->nclean) {
		/* the cleanups of the scopes left: those not around the target label */
		uint32_t t = W->lcg[label] == W->gen ? W->lcn[label] : 0;
		int k = W->nclean;

		for (; k > 0; k--) {
			uint32_t s2 = W->clean[k - 1], id = W->csg[s2] == W->gen ? W->csn[s2] : 0, a = t;

			while (a && a != id)
				a = W->cn[a].parent;
			if (id && a == id)
				break;
		}
		if (W->inl && k < W->inl->cbase)
			k = W->inl->cbase;
		wk_cleanups(e, k);
	}
	LO(jump)(e, wk_ulabel(e, label));
}

/* Entering a label in a discarded branch bypasses every preceding statement.
   Keep that suffix, including its normal fallthrough, without retaining dead
   calls before the first label. Unknown containers remain conservative. */
static bool wk_stmt_entries(ecx *e, uint32_t n)
{
	cc *c = e->c;
	if (!wk_branch_entry(c, n)) return false;
	const node *x = N(n);
	if (x->op == N_BLOCK) {
		/* Declarations carry cleanup/VLA scope bookkeeping even when their
		   initialization is bypassed; retain the normal block traversal. */
		for (uint32_t i = 0; i < x->c; i++)
			if (N(c->lists[x->b + i])->op == N_DECL) {
				wk_stmt(e, n);
				return true;
			}
		bool live = false;
		for (uint32_t i = 0; i < x->c; i++) {
			uint32_t child = c->lists[x->b + i];
			if (live) wk_stmt(e, child);
			else live = wk_stmt_entries(e, child);
		}
		return live;
	}
	if (x->op == N_IF && !wk_branch_entry(c, x->a)) {
		uint32_t end = wk_newlabel(e);
		bool left = wk_stmt_entries(e, x->b);
		if (left) LO(jump)(e, end);
		bool right = wk_stmt_entries(e, x->c);
		if (left || right) wk_label(e, end);
		return left || right;
	}
	wk_stmt(e, n);
	return true;
}

static void wk_const_if(ecx *e, const node *x, bool truth)
{
	cc *c = e->c;
	uint32_t live = truth ? x->b : x->c, dead = truth ? x->c : x->b;
	if (!wk_branch_entry(c, dead)) {
		wk_stmt(e, live);
		return;
	}
	uint32_t join = wk_newlabel(e), other = wk_newlabel(e);
	if (truth) {
		wk_stmt(e, live);
		LO(jump)(e, join);
		wk_stmt_entries(e, dead);
	} else {
		LO(jump)(e, other);
		wk_stmt_entries(e, dead);
		LO(jump)(e, join);
		wk_label(e, other);
		wk_stmt(e, live);
	}
	wk_label(e, join);
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
	if (unlikely(e->E->o->debug) && x->op != N_BLOCK)
		LO(dbgrow)(e, x->loc);
	switch (x->op) {
	case N_BLOCK: {
		uint32_t vs = 0;	/* the block's first VLA: its saved sp is restored at the end */
		int cmark = W->nclean;

		for (uint32_t i = 0; i < x->c; i++) {
			const node *d = N(c->lists[x->b + i]);

			if (!vs && d->op == N_DECL && c->syms[d->a].kind == S_VAR && c->syms[d->a].sc != SC_TYPEDEF &&
			    c->syms[d->a].sc != SC_STATIC && c->syms[d->a].sc != SC_EXTERN && (T(c, c->syms[d->a].ty)->flags & TF_VLA))
				vs = d->a;
			wk_stmt(e, c->lists[x->b + i]);
		}
		if (W->nclean > cmark) {
			wk_cleanups(e, cmark);
			W->nclean = cmark;
		}
		if (vs) {
			lval l = { .k = L_FRAME, .off = wk_local(e, vs) + 8, .align = 8 };
			int r = LO(treg)(e);

			LO(load)(e, r, &l, 8, false);
			LO(spset)(e, r);
		}
		return;
	}
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
		cval cv;
		int known = wk_condfold(e, x->a);
		if (known >= 0) {
			wk_const_if(e, x, known);
			return;
		}
		val test = vnone();
		if ((cc_eval(c, x->a, &cv) && cv.kind == CV_INT) || wk_isconst(e, x->a)) {
			/* Front-end constant_p can change after binding inline arguments.
			   Trust the generated value and retain condition effects exactly once. */
			test = wk_gen(e, x->a);
			if (test.k == V_CONST) {
				wk_const_if(e, x, test.i != 0);
				return;
			}
		}
		uint32_t lelse = wk_newlabel(e), lend = wk_newlabel(e);

		if (test.k == V_NONE)
			wk_cond(e, x->a, false, lelse);
		else
			LO(brz)(e, wk_conv(e, test, N(x->a)->ty, prim(TY_BOOL)), 4, false, lelse);
		wk_stmt(e, x->b);
		if (x->c) {
			LO(jump)(e, lend);
			wk_label(e, lelse);
			wk_stmt(e, x->c);
			wk_label(e, lend);
		} else {
			wk_label(e, lelse);
		}
		return;
	}
	case N_WHILE: {
		uint32_t lhead = wk_newlabel(e), lbrk = wk_newlabel(e);

		wk_label(e, lhead);
		wk_cond(e, x->a, false, lbrk);
		wk_loop_push(e, lbrk, lhead);
		wk_stmt(e, x->b);
		W->nloop--;
		LO(jump)(e, lhead);
		wk_label(e, lbrk);
		return;
	}
	case N_DO: {
		uint32_t lhead = wk_newlabel(e), lcont = wk_newlabel(e), lbrk = wk_newlabel(e);

		wk_label(e, lhead);
		wk_loop_push(e, lbrk, lcont);
		wk_stmt(e, x->b);
		W->nloop--;
		wk_label(e, lcont);
		wk_cond(e, x->a, true, lhead);
		wk_label(e, lbrk);
		return;
	}
	case N_FOR: {
		uint32_t lhead = wk_newlabel(e), lcont = wk_newlabel(e), lbrk = wk_newlabel(e);
		uint32_t step = c->lists[x->c], body = c->lists[x->c + 1];
		int cmark = W->nclean;	/* a cleanup variable declared in the first clause: the loop's scope */

		if (x->a && N(x->a)->op == N_BLOCK)	/* declarations: in scope until the loop ends */
			for (uint32_t i = 0; i < N(x->a)->c; i++)
				wk_stmt(e, c->lists[N(x->a)->b + i]);
		else
			wk_stmt(e, x->a);
		wk_label(e, lhead);
		if (x->b)
			wk_cond(e, x->b, false, lbrk);
		wk_loop_push(e, lbrk, lcont);
		wk_stmt(e, body);
		W->nloop--;
		wk_label(e, lcont);
		if (step) {
			val v = wk_gen(e, step);

			wk_free(e, &v);
		}
		LO(jump)(e, lhead);
		wk_label(e, lbrk);
		if (W->nclean > cmark) {
			wk_cleanups(e, cmark);
			W->nclean = cmark;
		}
		return;
	}
	case N_SWITCH:
		wk_switch(e, n);
		return;
	case N_CASE: case N_DEFAULT:
		wk_label(e, wk_caselabel(e, n));
		wk_stmt(e, x->c);
		return;
	case N_LABEL:
		wk_label(e, wk_ulabel(e, x->a));
		if (c->labels[x->a].addr_taken)
			LO(named)(e, REF(RK_LBL, x->a));	/* &&label: .Ltmp.lbl.N, as data initializers name it */
		wk_stmt(e, x->c);
		return;
	case N_GOTO:
		wk_goto(e, x->a);
		return;
	case N_BREAK: case N_CONTINUE: {
		int i = W->nloop - 1;

		while (i >= 0 && x->op == N_CONTINUE && !W->loop[i].cont)
			i--;
		if (i < 0)
			efatal(e, x->loc, "break/continue outside a loop");
		wk_cleanups(e, W->loop[i].cdepth);
		LO(jump)(e, x->op == N_BREAK ? W->loop[i].brk : W->loop[i].cont);
		return;
	}
	case N_RETURN:
		if (W->inl) {
			/* an inlined call's return: the value to its slot, then past the body */
			wkinl *I = W->inl;

			if (x->a && KIND(c, I->rty) != TY_VOID) {
				val v = wk_gen(e, x->a);
				lval t = { .k = L_FRAME, .off = I->rslot, .align = 16 };

				if (v.k == V_CONST && (!I->any || (I->cst_ok && I->cst.i == v.i)) && !wk_mem(e, I->rty) && !is_flt(c, I->rty)) {
					I->cst = v;
				} else {
					I->cst_ok = false;
				}
				I->any = true;
				if (wk_mem(e, I->rty)) {
					v = wk_putmem(e, &t, I->rty, v);
					wk_free(e, &v);
				} else {
					int r = wk_reg(e, &v, I->rty);

					LO(store)(e, r, &t, wk_size(c, I->rty));
					LO(tfree)(e, r);
				}
			} else if (x->a) {
				val v = wk_gen(e, x->a);

				wk_free(e, &v);
			}
			wk_cleanups(e, I->cbase);
			LO(jump)(e, I->endlbl);
			return;
		}
		if (x->a && wk_mem(e, W->rty)) {
			/* the object's address; a size not a multiple of 8 through a slot of whole words */
			val v = wk_gen(e, x->a);
			int r = wk_reg(e, &v, prim(TY_ULONG));

			if (W->nclean) {
				/* the cleanups run after the value is taken: a copy, except of a local
				   itself (clang returns it in place: the cleanups' changes are seen) */
				if (!(N(x->a)->op == N_VAR && is_local_var(c, N(x->a)->a))) {
					lval t = { .k = L_FRAME, .off = wk_slot(e, tsize(c, W->rty), 16), .align = 16 };
					int rt = LO(laddr)(e, &t);

					LO(copy)(e, rt, r, tsize(c, W->rty));
					LO(tfree)(e, r);
					r = rt;
				}
				wk_cleanups(e, 0);
			}

			if (!W->rrec.mem && W->rrec.size % 8) {
				lval t = { .k = L_FRAME, .off = wk_slot(e, (W->rrec.size + 7) & ~(uint64_t)7, 8), .align = 8 };
				int rt = LO(laddr)(e, &t);

				LO(copy)(e, rt, r, W->rrec.size);
				LO(tfree)(e, r);
				r = rt;
			}
			LO(retrec)(e, r, &W->rrec, W->retlbl);
		} else if (x->a && KIND(c, W->rty) != TY_VOID) {
			val v = wk_gen(e, x->a);

			if (W->nclean) {
				/* the value parked while the cleanups run */
				lval t = { .k = L_FRAME, .off = wk_slot(e, 8, 8), .align = 8 };
				int r = wk_reg(e, &v, W->rty);

				LO(store)(e, r, &t, 8);
				LO(tfree)(e, r);
				wk_cleanups(e, 0);
				v = (val){ .k = V_REG, .r = (uint8_t)(is_flt(c, W->rty) ? LO(ftreg)(e) : LO(treg)(e)) };
				LO(load)(e, v.r, &t, 8, false);
			}
			LO(ret)(e, &v, W->rty, W->retlbl);
		} else {
			if (x->a) {
				val v = wk_gen(e, x->a);

				wk_free(e, &v);
			}
			wk_cleanups(e, 0);
			LO(ret)(e, NULL, prim(TY_VOID), W->retlbl);
		}
		return;
	case N_ASM: {
		e->kvalid = 0;
		const casm *a = &c->asms[x->a];
		bool empty = a->tmpl && !*c->strs[a->tmpl - 1].s && !a->nout && !a->nin && !a->go;

		for (uint32_t i = 0; empty && i < a->nclob; i++) {
			const char *s = c->strs[a->clob[i] - 1].s;

			empty = !strcmp(s, "memory") || !strcmp(s, "cc");
		}
		if (empty) {
			/* Locals live in memory; the record preserves the sequencing point
			   for passes that will cache loads or schedule instructions. */
			LO(barrier)(e);
			return;
		}
		LO(asm)(e, n);
		return;
	}
	case N_GOTO_IND: {
		val v = wk_gen(e, x->a);

		LO(jumpr)(e, wk_reg(e, &v, prim(TY_ULONG)));
		return;
	}
	default:
		efatal(e, x->loc, "x86-64: unsupported statement (node %d)", x->op);
	}
}

/* ---------------- inlining ---------------- */

#define WK_INLINE_NODES 40	/* AST nodes: a few statements (as arm64) */

VPRIV bool wk_const_sym(ecx *e, uint32_t s, val *v)
{
	wkst *W = e->wk;

	if (W->kgen[s] != W->gen)
		return false;
	*v = W->kval[s];
	return true;
}

/*
 * As clang -O2 sees it: always_inline functions, and small inline ones (the
 * kernel and the gcc tests lean on __builtin_constant_p and folding after
 * inlining).  Not: variadic or unprototyped, recursive, a label address
 * taken (one copy only), deeper than 16.
 */
VPRIV bool wk_can_inline(ecx *e, uint32_t s)
{
	cc *c = e->c;
	const sym *y = &c->syms[s];
	const ctype *ft = T(c, y->ty);

	if (!(y->flags & SF_DEFINED) || !y->body || (ft->flags & (TF_VARIADIC | TF_NOPROTO)) || N(y->body)->aux > 64)
		return false;
	for (uint32_t i = 0; i < N(y->body)->aux; i++)
		if (vm_type(c, c->syms[c->lists[N(y->body)->a + i]].ty))
			return false;	/* (a parameter's VLA sizes) */
	if (!(y->flags & SF_ALWAYS_INLINE)) {
		if (!e->E->o->opt)
			return false;
		if ((y->flags & SF_NOINLINE) || !(y->flags & SF_INLINE))
			return false;
		walk_fn(e, s);
		if (e->E->fnsize[s] > WK_INLINE_NODES)
			return false;
	}
	ensure_walked(e->E, c, s);
	if (e->E->sflags[s] & SA_NOINL)
		return false;
	if (s == e->wk->fsym || e->wk->depth >= 16)
		return false;
	for (wkinl *I = e->wk->inl; I; I = I->up)
		if (I->fn == s)
			return false;
	return true;
}

VPRIV val wk_inline(ecx *e, uint32_t n, uint32_t s)
{
	cc *c = e->c;
	wkst *W = e->wk;
	const node *x = N(n), *body = N(c->syms[s].body);
	wkinl I = { .up = W->inl, .fn = s, .endlbl = wk_newlabel(e), .rty = T(c, c->syms[s].ty)->base, .cbase = W->nclean,
		    .cst_ok = true };
	uint32_t inst0 = W->inst;
	val res;

	walk_fn(e, s);
	if (KIND(c, I.rty) != TY_VOID)
		I.rslot = wk_slot(e, tsize(c, I.rty) ? tsize(c, I.rty) : 8, talign(c, I.rty) < 8 ? 8 : talign(c, I.rty));
	/* the arguments in order, each parked (a constant kept), then bound to the parameters: an
	   argument that inlines the same function again cannot overwrite them */
	{
		val av[64];
		int64_t as[64];

		for (uint32_t i = 0; i < body->aux; i++) {
			uint32_t p = c->lists[body->a + i];
			qty pt = c->syms[p].ty;
			val v = i < x->c ? wk_gen(e, c->lists[x->b + i]) : vconst(0);

			as[i] = 0;
			if (v.k != V_CONST || wk_mem(e, pt) || is_flt(c, pt)) {
				lval l = { .k = L_FRAME, .off = wk_slot(e, tsize(c, pt) ? tsize(c, pt) : 8, talign(c, pt) < 8 ? 8 : talign(c, pt)), .align = 8 };

				as[i] = (int64_t)l.off;
				if (wk_mem(e, pt)) {
					v = wk_putmem(e, &l, pt, v);
					wk_free(e, &v);
				} else {
					int r = wk_reg(e, &v, pt);

					LO(store)(e, r, &l, wk_size(c, pt));
					LO(tfree)(e, r);
				}
			}
			av[i] = v;
		}
		for (uint32_t i = 0; i < body->aux; i++) {
			uint32_t p = c->lists[body->a + i];
			qty pt = c->syms[p].ty;
			lval l = { .k = L_FRAME, .off = wk_local(e, p), .align = sym_align(c, p) }, t = { .k = L_FRAME, .off = as[i], .align = 8 };
			val v;

			if (!as[i] && !(e->E->sflags[p] & (SA_WRITTEN | SA_CWRITE)) && !(QQ(pt) & Q_VOLATILE) &&
			    !sym_attrs(c, p)->cleanup) {
				W->kgen[p] = W->gen;
				W->kval[p] = vconst(em_trunc_to(c, av[i].i, pt));
				continue;
			}
			W->kgen[p] = 0;
			v = as[i] ? wk_reload(e, &t, pt) : av[i];
			if (wk_mem(e, pt)) {
				v = wk_putmem(e, &l, pt, v);
				wk_free(e, &v);
			} else {
				int r = wk_reg(e, &v, pt);

				LO(store)(e, r, &l, wk_size(c, pt));
				LO(tfree)(e, r);
			}
		}
	}
	{
		/* the body's cleanup scopes and VLAs, under the caller's innermost cleanup */
		uint32_t top = 0;

		if (W->nclean) {
			uint32_t cs = W->clean[W->nclean - 1];

			top = W->csg[cs] == W->gen ? W->csn[cs] : 0;
		}
		wk_vla_scan(e, c->syms[s].body, &top);
	}
	W->inst = ++W->ninst;
	W->inl = &I;
	W->depth++;
	wk_stmt(e, c->syms[s].body);
	W->depth--;
	W->inl = I.up;
	W->inst = inst0;
	wk_label(e, I.endlbl);
	if (KIND(c, I.rty) == TY_VOID)
		return vconst(0);
	if (I.any && I.cst_ok)
		return I.cst;
	{
		lval t = { .k = L_FRAME, .off = I.rslot, .align = 16 };

		if (wk_mem(e, I.rty))
			return (val){ .k = V_REG, .r = (uint8_t)LO(laddr)(e, &t) };
		res = (val){ .k = V_REG, .r = (uint8_t)(is_flt(c, I.rty) ? LO(ftreg)(e) : LO(treg)(e)) };
		LO(load)(e, res.r, &t, wk_size(c, I.rty), wk_signed(c, I.rty));
		return res;
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
	wkarg pabuf[256];
	wkarg *pa = body->aux > sizeof pabuf / sizeof *pabuf ?
		malloc((size_t)body->aux * sizeof *pa) : pabuf;

	walk_fn(e, s);
	W->gen++;
	W->foff = 0;
	W->falign = 16;
	W->nlabel = 0;
	W->nloop = 0;
	W->ncases = 0;
	W->rty = ft->base;
	W->fsym = s;
	e->nkv = e->nael = e->kvalid = 0;
	W->rrec = (wkarg){ 0 };
	W->nclean = 0;
	W->inst = 0;
	W->inl = NULL;
	W->depth = 0;
	e->top = s;
	e->reach = true;
	e->calls = false;
	e->curloc = y->loc;
	LO(begin)(e, s);
	if (_setjmp(e->bail)) {
		if (pa != pabuf) free(pa);
		return;	/* efatal: the function is not emitted, the error counted */
	}
	if (!pa)
		efatal(e, y->loc, "out of memory");
	if (KIND(c, W->rty) != TY_VOID) {
		int k = KIND(c, W->rty);

		if (((k == TY_INT128 || k == TY_UINT128) && !LO(i128mem)()))
			efatal(e, y->loc, "x86-64: this return type is not supported yet (PLAN-x86.md X3)");
	}
	W->retlbl = wk_newlabel(e);
	LO(locals_begin)(e, s);
	if (wk_mem(e, W->rty)) {
		LO(classify)(e, W->rty, &W->rrec);
		W->rrec.slot = wk_slot(e, 8, 8);	/* (memory: the hidden pointer's slot) */
	}
	/* Parameters: slots unless the lowering selected a persistent register home. */
	for (uint32_t i = 0; i < body->aux; i++) {
		uint32_t p = c->lists[body->a + i];
		qty pt = c->syms[p].ty;
		int k = KIND(c, pt);

		if (((k == TY_INT128 || k == TY_UINT128) && !LO(i128mem)()))
			efatal(e, c->syms[p].loc, "x86-64: this parameter type is not supported yet (PLAN-x86.md X3)");
		if ((ft->flags & TF_NOPROTO) && k == TY_FLOAT)
			pt = prim(TY_DOUBLE);	/* K&R: received as the caller promoted it */
		LO(classify)(e, pt, &pa[i]);
		lval home;
		pa[i].slot = LO(local)(e, p, &home) ? 0 :
			wk_slot(e, pa[i].size > 8 ? (pa[i].size + 7) & ~(uint64_t)7 : 8, pa[i].align);
	}
	LO(params)(e, pa, (int)body->aux, wk_mem(e, W->rty) ? &W->rrec : NULL, (ft->flags & TF_VARIADIC) != 0);
	for (uint32_t i = 0; i < body->aux; i++) {
		uint32_t p = c->lists[body->a + i];

		W->sgen[p] = W->gen;
		W->soff[p] = pa[i].slot;
		if ((ft->flags & TF_NOPROTO) && KIND(c, c->syms[p].ty) == TY_FLOAT) {
			/* the double narrowed to the float the parameter is, in place */
			lval l = { .k = L_FRAME, .off = pa[i].slot, .align = 8 };
			val v = { .k = V_REG, .r = (uint8_t)LO(ftreg)(e) };
			int r;

			LO(load)(e, v.r, &l, 8, false);
			v = LO(cvt)(e, v, prim(TY_DOUBLE), prim(TY_FLOAT));
			r = v.r;
			LO(store)(e, r, &l, 4);
			LO(tfree)(e, r);
		}
	}
	for (uint32_t i = 0; i < body->aux; i++) {	/* int (*a)[n]: n as on entry */
		qty pt = c->syms[c->lists[body->a + i]].ty;

		if (unlikely(vm_type(c, pt)))
			wk_vla_fix(e, pt, true);
	}
	W->ncn = 0;
	{
		uint32_t top = 0;

		wk_vla_scan(e, y->body, &top);
	}
	wk_stmt(e, y->body);
	if (wk_is_main(c, s) && KIND(c, W->rty) == TY_INT) {
		val z = vconst(0);

		LO(ret)(e, &z, prim(TY_INT), W->retlbl);	/* falling off the end of main returns 0 */
	}
	wk_label(e, W->retlbl);
	LO(finish)(e, s);
	if (pa != pabuf) free(pa);
}
