/* vcc code generator for WebAssembly: statements and local initialisers (wimpl.h) */
#include "wimpl.h"

/* ---------------- local initialisers ---------------- */

static bool wzero_init(cc *c, uint32_t v)
{
	const node *x = N(v);

	if (!v || x->op == N_ZERO)
		return true;
	if (x->op == N_NUM)
		return c->ipool[x->a] == 0;
	if (x->op == N_CAST && (x->aux == CK_INT || x->aux == CK_NOP || x->aux == CK_NULL || x->aux == CK_I2P))
		return x->aux == CK_NULL || wzero_init(c, x->a);
	return false;
}

/* store the elements of init into the (zeroed) frame object at off */
static void welems(ecx *e, uint64_t off, qty ty, uint32_t init)
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

			for (uint32_t i = 0; i < x->c; i++)
				for (uint64_t k = v[3 * i]; k <= v[3 * i + 1]; k++) {
					if (is_bvec(c, ty)) {
						wlv l = { .k = WL_MEM, .bf = 1, .width = 1, .bitoff = k % 8, .off = k / 8 };
						wmem_lval(e, &l, off + k / 8);
						wslot_addr(e, off);
						wexpr(e, v[3*i+2], true);
						wstore(e, &l, et);
					} else welems(e, off + k * es, et, v[3 * i + 2]);
				}
			return;
		}
		{
			const tag *r = &c->tags[T(c, ty)->aux];

			for (uint32_t i = 0; i < x->c && i < r->nfield; i++) {
				const field *f = &r->f[i];

				if ((x->aux == 2 && i != x->a) || !v[i])
					continue;
				if (f->bitfield) {
					wlv l = { .k = WL_MEM, .bf = 1, .bitoff = (uint16_t)(f->off % 8), .width = f->width,
						  .sgn = is_signed(c, f->ty), .off = off + f->off / 8 };

					wmem_lval(e, &l, off + f->off / 8);
					if (wzero_init(c, v[i]))
						continue;
					wget(e, wfp(e));
					wexpr(e, v[i], true);
					wstore(e, &l, f->ty);
					continue;
				}
				welems(e, off + f->off / 8, f->ty, v[i]);
			}
		}
		return;
	}
	if (x->op == N_STR && is_array(c, ty)) {
		const cstr *s = &c->strs[x->a];
		uint64_t nb = (uint64_t)s->n * s->esz, cap = tsize(c, ty);

		if (nb > cap)
			nb = cap;
		wslot_addr(e, off);
		waddr_ref(e, REF(RK_STR, x->a), 0);
		wcopy(e, nb);
		return;
	}
	if (wzero_init(c, init) && !is_agg(c, ty))
		return;
	{
		wlv l = { .k = WL_MEM, .off = off };

		wmem_lval(e, &l, off);

		wget(e, wfp(e));
		if (wty(c, ty) == WT_MEM)
			wfold_off(e, &l);
		wexpr(e, init, true);
		wstore(e, &l, ty);
	}
}

/* initialise the frame object at off (zeroed: already zero) */
static void winit_impl(ecx *e, uint64_t off, qty ty, uint32_t init, bool zeroed)
{
	cc *c = e->c;
	const node *x = N(init);

	if (!init)
		return;
	if (x->op == N_INIT || x->op == N_ZERO || (x->op == N_STR && is_array(c, ty))) {
		if (!zeroed) {
			wslot_addr(e, off);
			wconst(e, WT_I32, 0);
			wpconst(e, (int64_t)tsize(c, ty));
			wmem_fence(e);
			wi(e, "memory.fill\t0");
		}
		welems(e, off, ty, init);
		return;
	}
	{
		wlv l = { .k = WL_MEM, .off = off };

		wmem_lval(e, &l, off);

		wget(e, wfp(e));
		if (wty(c, ty) == WT_MEM)
			wfold_off(e, &l);
		wexpr(e, init, true);
		wstore(e, &l, ty);
	}
}

VPRIV void winit(ecx *e, uint64_t off, qty ty, uint32_t init, bool zeroed)
{
	bool observable = (QQ(ty) & Q_VOLATILE) || (T(e->c, ty)->flags & TF_ATOMIC);

	e->w->memopaque += observable;
	winit_impl(e, off, ty, init, zeroed);
	e->w->memopaque -= observable;
}

/* ---------------- inline asm ---------------- */

/*
 * Only the empty template (an optimization barrier, common in tests and in
 * the kernel's headers): inputs are evaluated, an input tied to an output
 * ("0", "[name]") is stored into it, other outputs keep their value.
 */
static void wasm_asm(ecx *e, const node *x)
{
	wflow_reject(e);
	wmem_fence(e);
	cc *c = e->c;
	const casm *a = &c->asms[x->a];
	const char *t = c->strs[a->tmpl - 1].s;

	for (; *t; t++)
		if (*t != ' ' && *t != '\t' && *t != '\n' && *t != ';')
			efatal(e, x->loc, "wasm: inline assembly is not supported (only an empty template)");
	/* asm goto with an empty template never jumps: it falls through */
	for (uint32_t i = 0; i < a->nin; i++) {
		const asmop *in = &a->ops[a->nout + i];
		const char *cs = c->strs[in->constraint - 1].s;
		int tie = -1;

		if (*cs >= '0' && *cs <= '9') {
			tie = atoi(cs);
		} else if (*cs == '[') {
			for (uint32_t k = 0; k < a->nout; k++)
				if (a->ops[k].name && strlen(cs) == 2 + (size_t)vintern_ents[a->ops[k].name].len &&
				    !memcmp(cs + 1, vintern_ents[a->ops[k].name].s, vintern_ents[a->ops[k].name].len))
					tie = (int)k;
		}
		if (tie >= 0 && (uint32_t)tie < a->nout) {
			uint32_t on = a->ops[tie].expr;
			qty ot = N(on)->ty, it = N(in->expr)->ty;
			wlv l;

			wlval(e, on, &l, ot);
			wexpr(e, in->expr, true);
			if (wty(c, ot) != WT_MEM)
				wconv(e, is_array(c, it) ? prim(TY_ULONG) : it, ot);
			wstore(e, &l, ot);
			continue;
		}
		wexpr(e, in->expr, false);
	}
	for (uint32_t k = 0; k < a->nout; k++) {
		/* the output's address is computed (side effects); its value stays */
		wlv l;

		wlval(e, a->ops[k].expr, &l, prim(TY_VOID));
		if (l.k == WL_MEM)
			wi(e, "drop");
	}
	wmem_fence(e);
}

/* ---------------- dead code ---------------- */

/* the statements [i0, i1) of compound x: those after a jump are dropped up to a label */
static void wstmts(ecx *e, const node *x, uint32_t i0, uint32_t i1)
{
	cc *c = e->c;
	bool dead = false;

	for (uint32_t i = i0; i < i1; i++) {
		uint32_t s = c->lists[x->b + i];

		if (dead && !wlabels(c, s))
			continue;
		uint32_t from, to;
		if (i > i0 && em_block_value_reuse(e, x, i0, i, &from, &to)) {
			uint32_t oldfrom = e->reuse_from, oldto = e->reuse_to;
			e->reuse_from = from; e->reuse_to = to;
			wstmt(e, s);
			e->reuse_from = oldfrom; e->reuse_to = oldto;
		} else {
			wstmt(e, s);
		}
		dead = wterm(c, s);
	}
}

/* ---------------- cleanups ---------------- */

/* call the cleanup of variable s: f(&s) */
static void wcleanup(ecx *e, uint32_t s)
{
	wflow_reject(e);
	cc *c = e->c;
	uint32_t f = canon_sym(c, sym_attrs(c, s)->cleanup), ref = REF(RK_SYM, f);
	const ctype *ft = T(c, c->syms[f].ty);
	wbind *b = wlocal(e, s);
	tbuf *tb = &e->w->b;

	wmem_escape(e, b->cellid);
	wmem_fence(e);
	if (b->k == WB_FRAME)
		wslot_addr(e, b->off);
	else if (b->k == WB_PTR)
		wget(e, b->loc);
	else
		efatal(e, c->syms[s].loc, "wasm: a cleanup variable in a register");
	claim_ref(e, ref);
	wdecl(e, ref, c->syms[f].ty, NULL);
	tb_s(tb, "\tcall\t");
	wfname(c, tb, ref);
	tb_c(tb, '\n');
	if (wclassify_ret(c, ft->base).k == WA_SCALAR)
		wi(e, "drop");
}

/* run the cleanups [from, ncl) in reverse; pop: they go out of scope */
static void wcleanups(ecx *e, uint32_t from, bool pop)
{
	wst *W = e->w;

	for (uint32_t i = W->ncl; i-- > from;)
		wcleanup(e, W->cl[i].sp);
	if (pop)
		W->ncl = from;
}

/* a branch to block index t: the cleanups of the blocks it leaves */
static void wcleanup_leave(ecx *e, int t)
{
	wst *W = e->w;
	uint32_t i;

	for (i = 0; i < W->ncl && W->cl[i].depth <= t; i++)
		;
	wcleanups(e, i, false);
}

/* ---------------- VLA scopes ---------------- */

/* the index of the compound statement's first VLA declaration, UINT32_MAX: none */
static uint32_t wvla_block(cc *c, const node *x)
{
	for (uint32_t i = 0; i < x->c; i++) {
		uint32_t s = c->lists[x->b + i];

		while (s && (N(s)->op == N_LABEL || N(s)->op == N_CASE || N(s)->op == N_DEFAULT))
			s = N(s)->c;
		if (s && N(s)->op == N_DECL) {
			const ctype *t = T(c, c->syms[N(s)->a].ty);

			if (c->syms[N(s)->a].kind == S_VAR && t->kind == TY_ARRAY && (t->flags & TF_VLA))
				return i;
		}
	}
	return UINT32_MAX;
}

/*
 * A branch to block index t: give back the stack of the VLA blocks it
 * leaves, and of the one whose dispatch loop t is when it goes back to
 * statement idx before the VLAs (goto; UINT32_MAX: break, continue).
 */
static void wvla_leave(ecx *e, int t, uint32_t idx)
{
	wst *W = e->w;

	for (uint32_t i = 0; i < W->nvb; i++)
		if (t < W->vb[i].depth || (t == W->vb[i].depth && idx <= W->vb[i].first)) {
			wget(e, W->vb[i].sp);
			wi(e, "global.set\t__stack_pointer");
			return;
		}
}

/* ---------------- goto ---------------- */

/*
 * Structured control flow has no goto.  A compound statement that holds a
 * label is split into segments at its labelled statements and emitted as a
 * dispatch loop:
 *
 *	loop                    ; goto: set st, br here
 *	  block .. block        ; one per segment
 *	    st, st = 0, br_table
 *	  end_block  segment 0
 *	  end_block  segment 1 ..
 *	end_loop
 *
 * A goto inside the compound sets its state local and branches to the loop.
 * A goto into a compound nested (statement in statement) under one being
 * emitted also sets the states of the compounds between, so each dispatch
 * picks the segment on the way.  The state is reset when read, and wasm
 * locals start at 0, so a compound entered normally runs from the top.
 */
static void wgscan(ecx *e, uint32_t n, int pd, uint32_t pidx);

static void wgadd(ecx *e, uint32_t label, uint32_t d, uint32_t i)
{
	wst *W = e->w;

	if (W->ngp >= 2 * WMAXGD)
		efatal(e, e->curloc, "wasm: too many labels");
	W->gp[W->ngp++] = (wgp){ label, d, i };
}

static uint32_t wgrec(ecx *e, uint32_t n, int pd, uint32_t pidx)
{
	cc *c = e->c;
	wst *W = e->w;
	uint32_t r;

	if (W->ngd >= WMAXGD)
		efatal(e, N(n)->loc, "wasm: too many blocks");
	r = W->ngd++;
	W->gd[r] = (wgd){ .node = n, .parent = pd, .pidx = pidx, .op = N(n)->op, .loop = -1 };
	return r;
}

/* a record r needed: so is its parent, a compound starts a segment there */
static void wgneed(ecx *e, uint32_t r)
{
	wst *W = e->w;
	int pd = W->gd[r].parent;

	if (!W->gd[r].need || pd < 0)
		return;
	W->gd[pd].need = true;
	if (W->gd[pd].op == N_BLOCK || W->gd[pd].op == N_SWITCH)
		wgadd(e, 0, (uint32_t)pd, W->gd[r].pidx);
}

/* statement s at position i of record r: its labels, then what they label */
static void wgsub(ecx *e, uint32_t s, uint32_t r, uint32_t i)
{
	cc *c = e->c;
	wst *W = e->w;

	for (; s && (N(s)->op == N_LABEL || N(s)->op == N_CASE || N(s)->op == N_DEFAULT); s = N(s)->c)
		if (N(s)->op == N_LABEL) {
			W->gd[r].need = true;
			wgadd(e, N(s)->a, r, i);
		} else if (W->gd[r].op != N_SWITCH) {
			/* a case label in a nested statement: its switch goes there as a goto */
			W->gd[r].need = true;
			wgadd(e, WG_CASE | s, r, i);
		}
	wgscan(e, s, (int)r, i);
}

/* the statement expressions in expression n: compounds of their own (roots) */
static void wgexpr(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x;

	for (; n; n = x->a) {
		x = N(n);
		switch (x->op) {
		case N_STMTEXPR: {
			wst *W = e->w;
			int sr = W->sjr;
			uint32_t si = W->sji;

			wgscan(e, x->a, -1, 0);
			W->sjr = sr, W->sji = si;
			return;
		}
		case N_MEMBER: case N_CAST: case N_UNARY: case N_ADDR: case N_DEREF:
		case N_PREINC: case N_PREDEC: case N_POSTINC: case N_POSTDEC:
		case N_VAARG: case N_COMPLIT:
			continue;
		case N_INDEX: case N_BINARY: case N_LOGAND: case N_LOGOR: case N_ASSIGN:
		case N_COMMA: case N_CPLX:
			wgexpr(e, x->b);
			continue;
		case N_COND:
			wgexpr(e, x->b);
			wgexpr(e, x->c);
			continue;
		case N_CALL: case N_BUILTIN:
			if (x->op == N_CALL && wsjlj_kind(c, n) == 1) {
				/* a setjmp: its statement is where a longjmp resumes (wsj_begin) */
				wst *W = e->w;

				if (W->sjr < 0)
					efatal(e, x->loc, W->sjr == -2 ? "wasm: setjmp in a loop's condition is not supported"
								       : "wasm: setjmp outside a statement of a compound statement is not supported");
				if (W->nsj >= WMAXGD)
					efatal(e, x->loc, "wasm: too many setjmp calls");
				W->gd[W->sjr].need = true;
				wgadd(e, WG_SJ | W->nsj, (uint32_t)W->sjr, W->sji);
				W->sj[W->nsj++] = n;
			}
			for (uint32_t i = 0; i < x->c; i++)
				wgexpr(e, c->lists[x->b + i]);
			if (x->op == N_CALL)
				continue;
			return;
		case N_INIT:
			for (uint32_t i = 0; i < x->c; i++)
				wgexpr(e, c->lists[x->b + (x->aux == 1 ? 3 * i + 2 : i)]);
			return;
		default:
			return;
		}
	}
}

static void wgscan(ecx *e, uint32_t n, int pd, uint32_t pidx)
{
	cc *c = e->c;
	const node *x;
	uint32_t r;

	if (!n)
		return;
	x = N(n);
	e->w->sjr = pd, e->w->sji = pidx;
	switch (x->op) {
	case N_EXPR: case N_RETURN: case N_GOTO_IND:
		wgexpr(e, x->a);
		break;
	case N_DECL:
		wgexpr(e, x->b);
		break;
	case N_BLOCK:
		r = wgrec(e, n, pd, pidx);
		for (uint32_t i = 0; i < x->c; i++)
			wgsub(e, c->lists[x->b + i], r, i);
		wgneed(e, r);
		break;
	case N_IF:
		wgexpr(e, x->a);
		r = wgrec(e, n, pd, pidx);
		wgsub(e, x->b, r, 1);
		wgsub(e, x->c, r, 2);
		wgneed(e, r);
		break;
	case N_WHILE:
	case N_DO:
		e->w->sjr = -2;
		wgexpr(e, x->a);
		r = wgrec(e, n, pd, pidx);
		wgsub(e, x->b, r, 0);
		wgneed(e, r);
		break;
	case N_FOR:
		wgscan(e, x->a, -1, 0);
		e->w->sjr = -2;
		wgexpr(e, x->b);
		wgexpr(e, c->lists[x->c]);
		r = wgrec(e, n, pd, pidx);
		wgsub(e, c->lists[x->c + 1], r, 0);
		wgneed(e, r);
		break;
	case N_SWITCH: {
		/* its body's segments: the case labels and these */
		const node *b = N(x->b);

		wgexpr(e, x->a);
		if (b->op != N_BLOCK) {
			wgscan(e, x->b, -1, 0);
			break;
		}
		r = wgrec(e, n, pd, pidx);
		for (uint32_t i = 0; i < b->c; i++)
			wgsub(e, c->lists[b->b + i], r, i);
		wgneed(e, r);
		break;
	}
	case N_LABEL:
	case N_CASE:
	case N_DEFAULT:
		wgscan(e, x->c, -1, 0);
		break;
	default:
		break;
	}
}

static int wgp_cmp(const void *a, const void *b)
{
	const wgp *p = a, *q = b;

	if (p->d != q->d)
		return p->d < q->d ? -1 : 1;
	return p->i < q->i ? -1 : p->i > q->i;
}

static int wgdx_cmp(const void *a, const void *b)
{
	uint64_t p = *(const uint64_t *)a, q = *(const uint64_t *)b;

	return p < q ? -1 : p > q;
}

static int wglab_cmp(const void *a, const void *b)
{
	const wgp *p = a, *q = b;

	return p->label < q->label ? -1 : p->label > q->label;
}

VPRIV void wgoto_scan(ecx *e, uint32_t body)
{
	wst *W = e->w;
	uint32_t nl = 0, k = 0;
	wgp *lab;

	W->ngd = W->ngdx = W->ngp = 0;
	wgscan(e, body, -1, 0);
	if (!W->ngp)
		return;
	/* segment starts per compound: statement 0 and every labelled one */
	for (uint32_t r = 0; r < W->ngd; r++)
		if (W->gd[r].need && W->gd[r].op == N_BLOCK)
			wgadd(e, 0, r, 0);
	qsort(W->gp, W->ngp, sizeof(wgp), wgp_cmp);
	/* labels move behind the segment starts (gp[ngp ..]), starts are deduplicated */
	lab = W->gp + W->ngp;
	for (uint32_t j = 0; j < W->ngp; j++) {
		wgp p = W->gp[j];

		if (p.label)
			lab[nl++] = p;
	}
	for (uint32_t j = 0; j < W->ngp; j++) {
		wgp p = W->gp[j];

		if (k && W->gp[k - 1].d == p.d && W->gp[k - 1].i == p.i)
			continue;
		p.label = 0;
		W->gp[k++] = p;
	}
	memmove(W->gp + k, lab, nl * sizeof(wgp));
	W->ngp = k;
	for (uint32_t j = 0; j < k; j++) {
		wgd *d = &W->gd[W->gp[j].d];

		if (!d->nseg++)
			d->seg = j;
	}
	qsort(W->gp + k, nl, sizeof(wgp), wglab_cmp);
	for (uint32_t r = 0; r < W->ngd; r++)
		if (W->gd[r].need) {
			int op = W->gd[r].op;

			W->gd[r].st = (op == N_BLOCK ? W->gd[r].nseg > 1 : op != N_DO) ? wnewloc(e, WT_I32) : UINT32_MAX;
			W->gdx[W->ngdx++] = (uint64_t)W->gd[r].node << 32 | r;
		}
	qsort(W->gdx, W->ngdx, sizeof(uint64_t), wgdx_cmp);
	W->ngp = k;
	W->ngl = nl;
}

/* the segment of compound d that holds statement i */
static uint32_t wgseg(wst *W, const wgd *d, uint32_t i)
{
	uint32_t lo = 0, hi = d->nseg;

	while (hi - lo > 1) {
		uint32_t m = (lo + hi) / 2;

		if (W->gp[d->seg + m].i <= i)
			lo = m;
		else
			hi = m;
	}
	return lo;
}

static int wgfind(wst *W, uint32_t node)
{
	uint32_t lo = 0, hi = W->ngdx;

	while (lo < hi) {
		uint32_t m = (lo + hi) / 2, v = (uint32_t)(W->gdx[m] >> 32);

		if (v == node)
			return (int)(uint32_t)W->gdx[m];
		if (v < node)
			lo = m + 1;
		else
			hi = m;
	}
	return -1;
}

/* the state of record d that starts at its part i */
static uint32_t wgstate(wst *W, const wgd *d, uint32_t i)
{
	switch (d->op) {
	case N_BLOCK:
		return wgseg(W, d, i);
	case N_IF:
		return i;
	case N_SWITCH:
		return i + 1;
	default:
		return 1;
	}
}

/* label lb's (compound, statement), NULL: not a goto target */
static const wgp *wglabel(wst *W, uint32_t lb)
{
	uint32_t lo = 0, hi = W->ngl;
	const wgp *L = W->gp + W->ngp;

	while (lo < hi) {
		uint32_t m = (lo + hi) / 2;

		if (L[m].label == lb)
			return &L[m];
		if (L[m].label < lb)
			lo = m + 1;
		else
			hi = m;
	}
	return NULL;
}

static void wgoto(ecx *e, uint32_t lb, uint32_t loc)
{
	wflow_reject(e);
	wst *W = e->w;
	const wgp *p = wglabel(W, lb);
	int d, a;
	uint32_t aidx;

	if (!p)
		efatal(e, loc, "wasm: goto to a label that is not a statement of a compound statement is not supported yet (PLAN-wasm.md W4)");
	/* the compound being emitted that the target is (nested) in */
	for (a = (int)p->d; a >= 0 && W->gd[a].loop < 0; a = W->gd[a].parent)
		;
	if (a < 0)
		efatal(e, loc, "wasm: goto into a statement expression is not supported yet (PLAN-wasm.md W4)");
	/* the states on the way down: target segment in d, the enclosing segments above */
	d = (int)p->d;
	aidx = p->i;
	if (W->gd[d].st != UINT32_MAX) {
		wconst(e, WT_I32, wgstate(W, &W->gd[d], p->i));
		wset(e, W->gd[d].st);
	}
	while (d != a) {
		const wgd *ch = &W->gd[d];

		d = ch->parent;
		aidx = ch->pidx;
		if (W->gd[d].st != UINT32_MAX) {
			wconst(e, WT_I32, wgstate(W, &W->gd[d], ch->pidx));
			wset(e, W->gd[d].st);
		}
	}
	wcleanup_leave(e, W->gd[a].loop);
	wvla_leave(e, W->gd[a].loop, aidx);
	wbr(e, "br", W->gd[a].loop, false);
}

/*
 * setjmp/longjmp through wasm exceptions, as clang -mllvm -wasm-enable-sjlj:
 * setjmp(env) is __wasm_setjmp(env, site, &id) and 0; longjmp throws
 * __c_longjmp ({env, val}), which a function with setjmp sites catches
 * around its body: __wasm_setjmp_test(env, &id) gives the site (0: another
 * invocation's, thrown on), and the body runs again entered at the site's
 * statement as by goto, where the setjmp is then val.
 */
VPRIV void wsj_begin(ecx *e)
{
	cc *c = e->c;
	wst *W = e->w;

	W->sjl = wnewloc(e, WT_I32);
	W->sjv = wnewloc(e, WT_I32);
	W->sjsp = wnewloc(e, WPT(c));
	W->sjp = wnewloc(e, WPT(c));
	W->sjfid = wslot(e, 4, 4);
	if (WASM32(c))
		tb_s(&W->decl, "__wasm_setjmp\t(i32, i32, i32) -> ()\n__wasm_setjmp_test\t(i32, i32) -> (i32)\n"
			       "__wasm_longjmp\t(i32, i32) -> ()\n");
	else
		tb_s(&W->decl, "__wasm_setjmp\t(i64, i32, i64) -> ()\n__wasm_setjmp_test\t(i64, i64) -> (i32)\n"
			       "__wasm_longjmp\t(i64, i32) -> ()\n");
	wi(e, "global.get\t__stack_pointer");
	wset(e, W->sjsp);
	W->sjloop = wblock(e, "loop", WF_LOOP);
	wblock(e, "try", WF_UNKNOWN);
	for (uint32_t k = 0; k < W->nsj; k++) {
		const wgp *p = wglabel(W, WG_SJ | k);
		int d = (int)p->d;
		uint32_t i = p->i;

		wget(e, W->sjl);
		wconst(e, WT_I32, k + 1);
		wi(e, "i32.eq");
		wblock(e, "if", WF_IF);
		/* the states on the way down from the body (its dispatch picks them up) */
		for (;;) {
			if (W->gd[d].st != UINT32_MAX) {
				wconst(e, WT_I32, wgstate(W, &W->gd[d], i));
				wset(e, W->gd[d].st);
			}
			if (W->gd[d].parent < 0)
				break;
			i = W->gd[d].pidx;
			d = W->gd[d].parent;
		}
		if (W->gd[d].node != c->syms[W->fsym].body)
			efatal(e, N(W->sj[k])->loc, "wasm: setjmp in a statement expression is not supported");
		wend(e, "end_if");
	}
}

VPRIV void wsj_end(ecx *e)
{
	wst *W = e->w;
	bool w32 = WASM32(e->c);
	int b;

	/* the exception's payload: { void *env; int val; } */
	wboundary(e, "catch\t__c_longjmp", WF_UNKNOWN);
	wset(e, W->sjp);
	wget(e, W->sjsp);
	wi(e, "global.set\t__stack_pointer");
	wget(e, W->sjp);
	wiu(e, "i32.load", w32 ? 4 : 8);
	wset(e, W->sjv);
	wget(e, W->sjp);
	wiu(e, w32 ? "i32.load" : "i64.load", 0);
	wset(e, W->sjp);
	b = wblock(e, "block", WF_BLOCK);
	wget(e, W->sjp);
	wslot_addr(e, W->sjfid);
	wi(e, "call\t__wasm_setjmp_test");
	wtee(e, W->sjl);
	wbr(e, "br_if", b, true);
	/* not a setjmp of this invocation: on to the next frame */
	wget(e, W->sjp);
	wget(e, W->sjv);
	wi(e, "call\t__wasm_longjmp");
	wflow_reject(e);
	wi(e, "unreachable");
	wend(e, "end_block");
	wbr(e, "br", W->sjloop, false);
	wend(e, "end_try");
	wend(e, "end_loop");
}

/* a loop's test, skipped once when the loop is entered by goto (st != 0) */
static void wgtest(ecx *e, uint32_t st, uint32_t cond, int out)
{
	wst *W = e->w;

	wget(e, st);
	wi(e, "i32.eqz");
	wblock(e, "if", WF_IF);
	if (cond) {
		wtruth(e, cond);
		wi(e, "i32.eqz");
		wbr(e, "br_if", out, true);
	}
	wend(e, "end_if");
	wconst(e, WT_I32, 0);
	wset(e, st);
	(void)W;
}

/* a compound statement with a dispatch loop */
static void wgblock(ecx *e, const node *x, int r)
{
	wst *W = e->w;
	wgd *d = &W->gd[r];
	uint32_t m = d->nseg;
	bool notop = W->notop && W->notop == d->node && x->c && !wlabels(e->c, e->c->lists[x->b]);
	int *sb = malloc(m * sizeof(int));
	tbuf *b = &W->b;

	d->loop = wblock(e, "loop", WF_LOOP);
	if (m > 1) {
		for (uint32_t j = m; j-- > 0;)
			sb[j] = wblock(e, "block", WF_BLOCK);
		wget(e, d->st);
		wconst(e, WT_I32, 0);
		wset(e, d->st);
		tb_s(b, "\tbr_table\t{");
		for (uint32_t j = 0; j < m; j++) {
			tb_u(b, (uint64_t)(W->depth - 1 - sb[j]));
			tb_s(b, ", ");
		}
		tb_u(b, (uint64_t)(W->depth - 1 - sb[0]));
		tb_s(b, "}\n");
	}
	for (uint32_t j = 0; j < m; j++) {
		uint32_t s0 = W->gp[d->seg + j].i, s1 = j + 1 < m ? W->gp[d->seg + j + 1].i : x->c;

		if (m > 1)
			wend(e, "end_block");
		if (j || !notop || m < 2)
			wstmts(e, x, s0, s1);
	}
	wend(e, "end_loop");
	W->gd[r].loop = -1;
	free(sb);
}

/* ---------------- statements ---------------- */

static void wdecl_stmt(ecx *e, const node *x)
{
	cc *c = e->c;
	uint32_t s = x->a;
	const sym *y = &c->syms[s];
	wbind *b;

	if (y->sc == SC_TYPEDEF && vm_type(c, y->ty)) {
		wvla_fix(e, y->ty, false);
		return;
	}
	if (y->kind != S_VAR || y->sc == SC_STATIC || y->sc == SC_EXTERN || y->sc == SC_TYPEDEF)
		return;
	if (!x->aux && vm_type(c, y->ty))
		wvla_fix(e, y->ty, false);
	if (T(c, y->ty)->kind == TY_ARRAY && (T(c, y->ty)->flags & TF_VLA)) {
		/* below the stack pointer, as alloca; the block gives it back (wvla_block) */
		wst *W = e->w;
		uint32_t al = sym_align(c, s) > 16 ? sym_align(c, s) : 16;

		b = &W->sb[s];
		if (b->gen != W->gen) {
			b->gen = W->gen;
			b->k = WB_PTR;
			b->loc = wnewloc(e, WPT(c));
		}
		W->dyn = true;
		wi(e, "global.get\t__stack_pointer");
		wvla_bytes(e, y->ty);
		wpop(e, "sub");
		wpconst(e, -(int64_t)al);
		wpop(e, "and");
		wtee(e, b->loc);
		wi(e, "global.set\t__stack_pointer");
		if (x->b) {
			/* = {}: zeroed */
			wget(e, b->loc);
			wconst(e, WT_I32, 0);
			wvla_bytes(e, y->ty);
			wmem_fence(e);
			wi(e, "memory.fill\t0");
		}
		return;
	}
	{
		unsigned recipe = em_unused_init(e, x, e->w->fsym, 64);

		if (recipe != EM_UNUSED_KEEP) {
			if (recipe == EM_UNUSED_EVAL) {
				wexpr(e, x->b, true);
				wi(e, "drop");
			}
			return;
		}
	}
	b = wlocal(e, s);
	if (sym_attrs(c, s)->cleanup) {
		wst *W = e->w;

		if (W->ncl >= WMAXGD)
			efatal(e, x->loc, "wasm: too many cleanups");
		W->cl[W->ncl].depth = W->bdepth;
		W->cl[W->ncl++].sp = s;
	}
	if (!x->b)
		return;
	if (b->k == WB_LOC) {
		wexpr(e, x->b, true);
		wset(e, b->loc);
		return;
	}
	winit(e, b->off, y->ty, x->b, false);
}

/* the statement under a chain of case/default/ordinary labels */
static uint32_t wunlabel(cc *c, uint32_t s)
{
	while (s && (N(s)->op == N_CASE || N(s)->op == N_DEFAULT || N(s)->op == N_LABEL)) {
		if (N(s)->op == N_LABEL && (c->labels[N(s)->a].addr_taken))
			break;
		s = N(s)->c;
	}
	return s;
}

/* the case labels of the current switch nested in statement n (not in an inner switch) */
static void wnested_cases(cc *c, uint32_t n, uint32_t **v, uint32_t *nv, uint32_t *cap)
{
	const node *x;

	for (; n; n = x->c) {
		x = N(n);
		switch (x->op) {
		case N_CASE: case N_DEFAULT:
			if (*nv == *cap) {
				*cap = *cap ? 2 * *cap : 16;
				*v = realloc(*v, *cap * sizeof **v);
			}
			(*v)[(*nv)++] = n;
			continue;
		case N_LABEL:
			continue;
		case N_BLOCK:
			for (uint32_t i = 0; i < x->c; i++)
				wnested_cases(c, c->lists[x->b + i], v, nv, cap);
			return;
		case N_IF:
			wnested_cases(c, x->b, v, nv, cap);
			wnested_cases(c, x->c, v, nv, cap);
			return;
		case N_WHILE: case N_DO:
			wnested_cases(c, x->b, v, nv, cap);
			return;
		case N_FOR:
			wnested_cases(c, c->lists[x->c + 1], v, nv, cap);
			return;
		default:
			return;
		}
	}
}

/* All targets are enclosing block indices, so wbr accounts for the extra
 * structured ifs while descending the balanced dispatch tree. */
static void wswitch_tree(ecx *e, const em_switch_case *v, uint32_t n,
			 uint32_t t, int k, bool sg, int dflt)
{
	if (!n) {
		wbr(e, "br", dflt, false);
		return;
	}
	if (n == 1) {
		wget(e, t);
		wconst(e, k, (int64_t)v->lo);
		if (v->lo == v->hi) {
			wop(e, k, "eq");
		} else {
			wop(e, k, "sub");
			wconst(e, k, (int64_t)(v->hi - v->lo));
			wop(e, k, "le_u");
		}
		wbr(e, "br_if", (int)v->target, true);
		wbr(e, "br", dflt, false);
		return;
	}
	uint32_t mid = n / 2;
	const em_switch_case *s = &v[mid];

	wget(e, t);
	wconst(e, k, (int64_t)s->lo);
	wop(e, k, sg ? "lt_s" : "lt_u");
	wblock(e, "if", WF_IF);
	wswitch_tree(e, v, mid, t, k, sg, dflt);
	wboundary(e, "else", WF_ELSE);
	wget(e, t);
	wconst(e, k, (int64_t)s->hi);
	wop(e, k, sg ? "le_s" : "le_u");
	wbr(e, "br_if", (int)s->target, true);
	wswitch_tree(e, s + 1, n - mid - 1, t, k, sg, dflt);
	wend(e, "end_if");
}

static void wswitch(ecx *e, uint32_t n)
{
	if (wswitch_values(e,n)) return;
	wflow_reject(e);
	cc *c = e->c;
	wst *W = e->w;
	const node *x = N(n);
	qty ct = N(x->a)->ty;
	int k = wty(c, ct);
	const node *body = N(x->b);
	uint32_t nst = body->op == N_BLOCK ? body->c : 1;
	const uint32_t *st = body->op == N_BLOCK ? &c->lists[body->b] : &x->b;
	uint32_t sw0 = W->nsw, seg0 = W->nseg, m, ncase, t, dseg = UINT32_MAX;
	int brk, obrk = W->brk, *sb;
	i128 lo = 0, hi = 0;
	bool ranges = false;
	int r = W->ngdx && body->op == N_BLOCK ? wgfind(W, n) : -1;
	wgd *g = r >= 0 ? &W->gd[r] : NULL;
	uint32_t gj = 0;	/* goto: the next segment start of g */
	uint32_t *nc = NULL, nnc = 0, cnc = 0;	/* nested case labels */

	bool wide = k == WT_MEM;	/* __int128: compared by halves (tl, th) */
	uint32_t th = 0;

	/* segments: top-level statements that carry case labels */
	for (uint32_t i = 0; i < nst; i++) {
		uint32_t s = st[i];
		bool head = false;

		while (g && gj < g->nseg && W->gp[g->seg + gj].i < i)
			gj++;
		if (g && gj < g->nseg && W->gp[g->seg + gj].i == i) {
			/* a goto target */
			if (W->nseg >= WMAXSW)
				efatal(e, x->loc, "wasm: switch too large");
			W->seg[W->nseg++] = i;
			head = true;
		}
		for (; s && (N(s)->op == N_CASE || N(s)->op == N_DEFAULT || N(s)->op == N_LABEL); s = N(s)->c) {
			if (N(s)->op == N_LABEL)
				continue;
			if (!head) {
				if (W->nseg >= WMAXSW)
					efatal(e, x->loc, "wasm: switch too large");
				W->seg[W->nseg++] = i;
				head = true;
			}
			if (N(s)->op == N_DEFAULT) {
				dseg = W->nseg - 1 - seg0;
				continue;
			}
			if (W->nsw >= WMAXSW)
				efatal(e, x->loc, "wasm: switch too large");
			W->sw[W->nsw++] = (wswc){ em_trunc_to(c, c->ipool[N(s)->a], ct),
						  em_trunc_to(c, c->ipool[N(s)->b ? N(s)->b : N(s)->a], ct), W->nseg - 1 - seg0, false };
		}
	}
	m = W->nseg - seg0;
	/* case labels in nested statements: a trampoline block each (sb[m + j]), a goto */
	for (uint32_t i = 0; i < nst; i++) {
		uint32_t s = st[i];

		while (s && (N(s)->op == N_CASE || N(s)->op == N_DEFAULT || N(s)->op == N_LABEL))
			s = N(s)->c;
		wnested_cases(c, s, &nc, &nnc, &cnc);
	}
	if (nnc && !g)
		efatal(e, x->loc, "wasm: a case label in a nested statement of a switch whose body is not a compound statement");
	for (uint32_t j = 0; j < nnc; j++) {
		const node *y = N(nc[j]);

		if (y->op == N_DEFAULT) {
			dseg = m + j;
			continue;
		}
		if (W->nsw >= WMAXSW)
			efatal(e, x->loc, "wasm: switch too large");
		W->sw[W->nsw++] = (wswc){ em_trunc_to(c, c->ipool[y->a], ct),
					  em_trunc_to(c, c->ipool[y->b ? y->b : y->a], ct), m + j, false };
	}
	ncase = W->nsw - sw0;
	t = wtmp(e, wide ? WT_I64 : k);
	if (wide)
		th = wtmp(e, WT_I64);
	if (g) {
		/* entered by goto: the controlling expression is not evaluated */
		wget(e, g->st);
		wi(e, "i32.eqz");
		wblock(e, "if", WF_IF);
	}
	wexpr(e, x->a, true);
	if (wide) {
		wsplit(e);
		wset(e, th);
	}
	wset(e, t);
	if (g)
		wend(e, "end_if");
	brk = wblock(e, "block", WF_BLOCK);
	if (g)
		g->loop = wblock(e, "loop", WF_LOOP);
	sb = malloc((m + nnc + 1) * sizeof(int));
	for (uint32_t j = m; j-- > 0;)
		sb[j] = wblock(e, "block", WF_BLOCK);
	for (uint32_t j = m + nnc; j-- > m;)
		sb[j] = wblock(e, "block", WF_BLOCK);
	if (g) {
		/* st - 1: the statement to start at */
		tbuf *b = &W->b;
		uint32_t j = 0;

		wget(e, g->st);
		wblock(e, "if", WF_IF);
		wget(e, g->st);
		wconst(e, WT_I32, 1);
		wi(e, "i32.sub");
		wconst(e, WT_I32, 0);
		wset(e, g->st);
		tb_s(b, "\tbr_table\t{");
		for (uint32_t i = 0; i < nst; i++) {
			while (j < m && W->seg[seg0 + j] < i)
				j++;
			tb_u(b, (uint64_t)(W->depth - 1 - (j < m && W->seg[seg0 + j] == i ? sb[j] : brk)));
			tb_s(b, ", ");
		}
		tb_u(b, (uint64_t)(W->depth - 1 - brk));
		tb_s(b, "}\n");
		wend(e, "end_if");
	}
	for (uint32_t i = 0; i < ncase; i++) {
		const wswc *s = &W->sw[sw0 + i];

		if (!i || s->lo < lo)
			lo = s->lo;
		if (!i || s->hi > hi)
			hi = s->hi;
		ranges |= s->lo != s->hi;
	}
	if (wide) {
		for (uint32_t i = 0; i < ncase; i++) {
			const wswc *s = &W->sw[sw0 + i];

			if (s->lo == s->hi) {
				wget(e, t);
				wconst(e, WT_I64, (int64_t)(uint64_t)s->lo);
				wi(e, "i64.eq");
				wget(e, th);
				wconst(e, WT_I64, (int64_t)(uint64_t)((u128)s->lo >> 64));
				wi(e, "i64.eq");
				wi(e, "i32.and");
			} else {
				/* (t - lo) <= (hi - lo), unsigned, in 128 bits */
				u128 rg = (u128)s->hi - (u128)s->lo;
				uint32_t dl = wtmp(e, WT_I64), dh = wtmp(e, WT_I64);

				wget(e, t);
				wconst(e, WT_I64, (int64_t)(uint64_t)s->lo);
				wi(e, "i64.sub");
				wset(e, dl);
				wget(e, th);
				wconst(e, WT_I64, (int64_t)(uint64_t)((u128)s->lo >> 64));
				wi(e, "i64.sub");
				wget(e, t);
				wconst(e, WT_I64, (int64_t)(uint64_t)s->lo);
				wi(e, "i64.lt_u");
				wi(e, "i64.extend_i32_u");
				wi(e, "i64.sub");
				wset(e, dh);
				wget(e, dh);
				wconst(e, WT_I64, (int64_t)(uint64_t)(rg >> 64));
				wi(e, "i64.lt_u");
				wget(e, dh);
				wconst(e, WT_I64, (int64_t)(uint64_t)(rg >> 64));
				wi(e, "i64.eq");
				wget(e, dl);
				wconst(e, WT_I64, (int64_t)(uint64_t)rg);
				wi(e, "i64.le_u");
				wi(e, "i32.and");
				wi(e, "i32.or");
				wfree(e, dl);
				wfree(e, dh);
			}
			wbr(e, "br_if", sb[s->seg], true);
		}
		wbr(e, "br", dseg != UINT32_MAX ? sb[dseg] : brk, false);
		wfree(e, th);
	} else if (e->E->o->opt && !e->E->noopt && !em_bitint(c, ct) &&
		   wswitch_bitset(e, x->b, sw0, ncase, ncase + (dseg != UINT32_MAX), t, k,
				  is_signed(c, ct), dseg != UINT32_MAX ? sb[dseg] : brk, sb)) {
		/* The shared proof selected one destination and bounded membership. */
	} else if (em_switch_strategy(ncase, lo, hi, ranges, true) == EM_SWITCH_TABLE) {
		/* dense: a branch table on t - lo */
		uint32_t span = (uint32_t)(hi - lo) + 1;
		uint32_t *tg = malloc(span * sizeof *tg);
		int dflt = dseg != UINT32_MAX ? sb[dseg] : brk;
		tbuf *b = &W->b;

		for (uint32_t v = 0; v < span; v++)
			tg[v] = (uint32_t)dflt;
		for (uint32_t i = ncase; i-- > 0;)
			tg[(uint32_t)(W->sw[sw0 + i].lo - lo)] = (uint32_t)sb[W->sw[sw0 + i].seg];
		wget(e, t);
		if (lo) {
			if (k == WT_I32)
				wconst(e, WT_I32, (int64_t)lo);
			else
				wconst(e, WT_I64, (int64_t)lo);
			wop(e, k, "sub");
		}
		if (k == WT_I64) {
			/* out of the table's range: the default */
			uint32_t u = wtmp(e, WT_I64);

			wtee(e, u);
			wconst(e, WT_I64, span);
			wi(e, "i64.ge_u");
			wbr(e, "br_if", dflt, true);
			wget(e, u);
			wi(e, "i32.wrap_i64");
			wfree(e, u);
		}
		tb_s(b, "\tbr_table\t{");
		for (uint32_t v = 0; v < span; v++) {
			tb_u(b, (uint64_t)(W->depth - 1 - (int)tg[v]));
			tb_s(b, ", ");
		}
		tb_u(b, (uint64_t)(W->depth - 1 - dflt));
		tb_s(b, "}\n");
		free(tg);
	} else {
		bool tree = false;
		if (e->E->o->opt && !e->E->noopt &&
		    em_switch_strategy(ncase, lo, hi, ranges, false) == EM_SWITCH_TREE) {
			em_switch_case *cases = malloc(ncase * sizeof *cases);
			bool sg = is_signed(c, ct);

			for (uint32_t i = 0; cases && i < ncase; i++) {
				const wswc *s = &W->sw[sw0 + i];
				cases[i] = (em_switch_case){ s->lo, s->hi, (uint32_t)sb[s->seg] };
			}
			tree = cases && em_switch_prepare(cases, ncase, k == WT_I32 ? 32 : 64, sg);
			if (tree)
				wswitch_tree(e, cases, ncase, t, k, sg, dseg != UINT32_MAX ? sb[dseg] : brk);
			free(cases);
		}
		if (!tree) {
			for (uint32_t i = 0; i < ncase; i++) {
				const wswc *s = &W->sw[sw0 + i];

				wget(e, t);
				if (s->lo == s->hi) {
					wconst(e, k, (int64_t)s->lo);
					wop(e, k, "eq");
				} else {
					wconst(e, k, (int64_t)s->lo);
					wop(e, k, "sub");
					wconst(e, k, (int64_t)(s->hi - s->lo));
					wop(e, k, "le_u");
				}
				wbr(e, "br_if", sb[s->seg], true);
			}
			wbr(e, "br", dseg != UINT32_MAX ? sb[dseg] : brk, false);
		}
	}
	wfree(e, t);
	W->brk = brk;
	for (uint32_t j = 0; j < nnc; j++) {
		wend(e, "end_block");
		wgoto(e, WG_CASE | nc[j], N(nc[j])->loc);
	}
	free(nc);
	/* statements before the first label: never run (their declarations count) */
	for (uint32_t i = 0; i < (m ? W->seg[seg0] : nst); i++)
		wstmt(e, st[i]);
	for (uint32_t j = 0; j < m; j++) {
		uint32_t end = j + 1 < m ? W->seg[seg0 + j + 1] : nst;

		wend(e, "end_block");
		for (uint32_t i = W->seg[seg0 + j]; i < end; i++)
			wstmt(e, i == W->seg[seg0 + j] ? wunlabel(c, st[i]) : st[i]);
	}
	free(sb);
	if (g) {
		wend(e, "end_loop");
		g->loop = -1;
	}
	wend(e, "end_block");
	W->brk = obrk;
	W->nsw = sw0;
	W->nseg = seg0;
}

/* a for statement's declarations: their cleanups run when the loop ends */
static void wfor_init(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);

	if (x->op == N_BLOCK && wvla_block(c, x) == UINT32_MAX)
		wstmts(e, x, 0, x->c);
	else
		wstmt(e, n);
}

static void wreturn(ecx *e, const node *x)
{
	cc *c = e->c;
	wst *W = e->w;

	if (x->a) {
		qty t = N(x->a)->ty;
		wabi r = wclassify_ret(c, W->rty);

		if (r.k == WA_IND) {
			const node *v = N(x->a);

			/* clang builds a returned local in the sret slot (NRVO): its
			   cleanup sees, and changes, what the caller gets */
			if (v->op == N_VAR && W->ncl && sym_attrs(c, v->a)->cleanup &&
			    QUNQ(t) == QUNQ(W->rty)) {
				wcleanups(e, 0, false);
				wget(e, W->sret);
				wexpr(e, x->a, true);
				wcopy(e, tsize(c, W->rty));
				wbr(e, "br", 0, false);
				return;
			}
			wget(e, W->sret);
			wexpr(e, x->a, true);
			wcopy(e, tsize(c, W->rty));
		} else if (r.k == WA_SCALAR && (is_record(c, W->rty) || is_vec(c, W->rty))) {
			wlv l = { .k = WL_MEM };

			wexpr(e, x->a, true);
			wload(e, &l, r.ft);
			wset(e, W->rv);
		} else if (W->rt != WT_NONE) {
			wexpr(e, x->a, true);
			if (QUNQ(t) != QUNQ(W->rty))
				wconv(e, t, W->rty);
			wset(e, W->rv);
		} else {
			wexpr(e, x->a, false);
		}
	}
	wcleanups(e, 0, false);
	wbr(e, "br", 0, false);
}

VPRIV void wstmt(ecx *e, uint32_t n)
{
	cc *c = e->c;
	wst *W = e->w;
	const node *x;
	uint32_t loc = e->curloc;

	if (!n)
		return;
	x = N(n);
	e->curloc = x->loc;
	switch (x->op) {
	case N_BLOCK: {
		int r = W->ngdx ? wgfind(W, n) : -1;
		uint32_t first = wvla_block(c, x), cl0 = W->ncl;
		bool vla = first != UINT32_MAX;
		int obd = W->bdepth;

		if (vla) {
			/* the stack pointer to give back when the block is left */
			if (W->nvb >= WMAXGD)
				efatal(e, x->loc, "wasm: too many blocks");
			W->vb[W->nvb].depth = W->depth;
			W->vb[W->nvb].first = first;
			W->vb[W->nvb].sp = wnewloc(e, WPT(c));
			W->dyn = true;
			wi(e, "global.get\t__stack_pointer");
			wset(e, W->vb[W->nvb++].sp);
		}
		W->bdepth = W->depth;
		if (r >= 0) {
			wgblock(e, x, r);
		} else {
			wstmts(e, x, 0, x->c);
		}
		W->bdepth = obd;
		if (W->ncl > cl0)
			wcleanups(e, cl0, true);
		if (vla) {
			wget(e, W->vb[--W->nvb].sp);
			wi(e, "global.set\t__stack_pointer");
		}
		break;
	}
	case N_EXPR:
		if (n == W->sxv && n) {
			/* the value of the statement expression being emitted */
			wst_sxval(e, x->a);
			break;
		}
		if (x->a)
			wexpr(e, x->a, false);
		break;
	case N_DECL:
		wdecl_stmt(e, x);
		break;
	case N_IF: {
		int r = W->ngdx ? wgfind(W, n) : -1, kn = wknown(e, x->a);

		if (r < 0 && kn >= 0 && !wlabels(c, kn ? x->c : x->b)) {
			/* if (0), if (1): only the branch taken */
			wstmt(e, kn ? x->b : x->c);
			break;
		}

		if (r >= 0) {
			/* entered by goto (st: 1 then, 2 else): the condition is not evaluated */
			uint32_t st = W->gd[r].st;

			wget(e, st);
			wi(e, "i32.eqz");
			wblock(e, "if\ti32", WF_IF);
			wtruth(e, x->a);
			wboundary(e, "else", WF_ELSE);
			wget(e, st);
			wconst(e, WT_I32, 1);
			wi(e, "i32.eq");
			wconst(e, WT_I32, 0);
			wset(e, st);
			wend(e, "end_if");
		} else {
			wtruth(e, x->a);
		}
		wblock(e, "if", WF_IF);
		/* if (0) { a; L: b; }: the branch not taken is only entered by goto,
		   what comes before its first label is dead */
		W->notop = kn == 0 ? x->b : 0;
		wstmt(e, x->b);
		if (x->c) {
			wboundary(e, "else", WF_ELSE);
			W->notop = kn == 1 ? x->c : 0;
			wstmt(e, x->c);
		}
		W->notop = 0;
		wend(e, "end_if");
		break;
	}
	case N_WHILE: {
		int ob = W->brk, oc = W->cont, b = wblock(e, "block", WF_BLOCK), l = wblock(e, "loop", WF_LOOP);
		int r = W->ngdx ? wgfind(W, n) : -1;

		if (r >= 0)
			wgtest(e, W->gd[r].st, x->a, b);
		else {
			wtruth(e, x->a);
			wi(e, "i32.eqz");
			wbr(e, "br_if", b, true);
		}
		W->brk = b, W->cont = l;
		wstmt(e, x->b);
		/* continue already branches to l; other terminal paths leave the loop. */
		if (!wterm(c, x->b))
			wbr(e, "br", l, false);
		W->brk = ob, W->cont = oc;
		wend(e, "end_loop");
		wend(e, "end_block");
		break;
	}
	case N_DO: {
		int ob = W->brk, oc = W->cont, b = wblock(e, "block", WF_BLOCK), l = wblock(e, "loop", WF_LOOP), k = wblock(e, "block", WF_BLOCK);

		W->brk = b, W->cont = k;
		wstmt(e, x->b);
		W->brk = ob, W->cont = oc;
		wend(e, "end_block");
		wtruth(e, x->a);
		wbr(e, "br_if", l, true);
		wend(e, "end_loop");
		wend(e, "end_block");
		break;
	}
	case N_FOR: {
		uint32_t step = c->lists[x->c], body = c->lists[x->c + 1];
		int ob = W->brk, oc = W->cont, b, l, k;
		int r = W->ngdx ? wgfind(W, n) : -1;
		uint32_t cl0 = W->ncl;

		if (x->a) {
			if (r >= 0) {
				/* entered by goto: no initialization */
				wget(e, W->gd[r].st);
				wi(e, "i32.eqz");
				wblock(e, "if", WF_IF);
				wfor_init(e, x->a);
				wend(e, "end_if");
			} else {
				wfor_init(e, x->a);
			}
		}
		b = wblock(e, "block", WF_BLOCK);
		l = wblock(e, "loop", WF_LOOP);
		if (r >= 0)
			wgtest(e, W->gd[r].st, x->b, b);
		else if (x->b) {
			wtruth(e, x->b);
			wi(e, "i32.eqz");
			wbr(e, "br_if", b, true);
		}
		k = wblock(e, "block", WF_BLOCK);
		W->brk = b, W->cont = k;
		wstmt(e, body);
		W->brk = ob, W->cont = oc;
		wend(e, "end_block");
		if (step)
			wexpr(e, step, false);
		wbr(e, "br", l, false);
		wend(e, "end_loop");
		wend(e, "end_block");
		if (W->ncl > cl0)
			wcleanups(e, cl0, true);
		break;
	}
	case N_SWITCH:
		wswitch(e, n);
		break;
	case N_CASE:
	case N_DEFAULT:
		/* nested: its switch reaches it as a goto target (wswitch) */
		if (!W->ngl || !wglabel(W, WG_CASE | n))
			efatal(e, x->loc, "wasm: a case label in a nested statement of a switch whose body is not a compound statement");
		__attribute__((fallthrough));
	case N_LABEL:
		wmem_fence(e);
		wstmt(e, x->c);
		break;
	case N_GOTO:
		wgoto(e, x->a, x->loc);
		break;
	case N_GOTO_IND: {
		/* compare with the number of every label whose address is taken */
		uint32_t t = wtmp(e, WPT(c));
		const wgp *L = W->gp + W->ngp;

		wexpr(e, x->a, true);
		wset(e, t);
		for (uint32_t i = 0; i < W->ngl; i++) {
			if ((L[i].label & (WG_CASE | WG_SJ)) || !c->labels[L[i].label].addr_taken ||
			    (i && L[i - 1].label == L[i].label))
				continue;
			wget(e, t);
			wpconst(e, (int64_t)L[i].label + 1);
			wpop(e, "eq");
			wblock(e, "if", WF_IF);
			wgoto(e, L[i].label, x->loc);
			wend(e, "end_if");
		}
		wfree(e, t);
		wflow_reject(e);
		wi(e, "unreachable");
		break;
	}
	case N_BREAK:
	case N_CONTINUE: {
		int t = x->op == N_BREAK ? W->brk : W->cont;

		if (t < 0)
			efatal(e, x->loc, "break/continue outside a loop");
		wcleanup_leave(e, t);
		wvla_leave(e, t, UINT32_MAX);
		wbr(e, "br", t, false);
		break;
	}
	case N_RETURN:
		wreturn(e, x);
		break;
	case N_ASM:
		wasm_asm(e, x);
		break;
	default:
		efatal(e, x->loc, "wasm: unsupported statement (node %d)", x->op);
	}
	e->curloc = loc;
}
