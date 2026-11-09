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
				for (uint64_t k = v[3 * i]; k <= v[3 * i + 1]; k++)
					welems(e, off + k * es, et, v[3 * i + 2]);
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

		wget(e, wfp(e));
		if (wty(c, ty) == WT_MEM)
			wfold_off(e, &l);
		wexpr(e, init, true);
		wstore(e, &l, ty);
	}
}

/* initialise the frame object at off (zeroed: already zero) */
VPRIV void winit(ecx *e, uint64_t off, qty ty, uint32_t init, bool zeroed)
{
	cc *c = e->c;
	const node *x = N(init);

	if (!init)
		return;
	if (x->op == N_INIT || x->op == N_ZERO || (x->op == N_STR && is_array(c, ty))) {
		if (!zeroed) {
			wslot_addr(e, off);
			wconst(e, WT_I32, 0);
			wconst(e, WT_I64, (int64_t)tsize(c, ty));
			wi(e, "memory.fill\t0");
		}
		welems(e, off, ty, init);
		return;
	}
	{
		wlv l = { .k = WL_MEM, .off = off };

		wget(e, wfp(e));
		if (wty(c, ty) == WT_MEM)
			wfold_off(e, &l);
		wexpr(e, init, true);
		wstore(e, &l, ty);
	}
}

/* ---------------- inline asm ---------------- */

/*
 * Only the empty template (an optimization barrier, common in tests and in
 * the kernel's headers): inputs are evaluated, an input tied to an output
 * ("0", "[name]") is stored into it, other outputs keep their value.
 */
static void wasm_asm(ecx *e, const node *x)
{
	cc *c = e->c;
	const casm *a = &c->asms[x->a];
	const char *t = c->strs[a->tmpl - 1].s;

	for (; *t; t++)
		if (*t != ' ' && *t != '\t' && *t != '\n' && *t != ';')
			efatal(e, x->loc, "wasm: inline assembly is not supported (only an empty template)");
	if (a->go)
		efatal(e, x->loc, "wasm: asm goto is not supported");
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
		}
	wgscan(e, s, (int)r, i);
}

static void wgscan(ecx *e, uint32_t n, int pd, uint32_t pidx)
{
	cc *c = e->c;
	const node *x;
	uint32_t r;

	if (!n)
		return;
	x = N(n);
	switch (x->op) {
	case N_BLOCK:
		r = wgrec(e, n, pd, pidx);
		for (uint32_t i = 0; i < x->c; i++)
			wgsub(e, c->lists[x->b + i], r, i);
		wgneed(e, r);
		break;
	case N_IF:
		r = wgrec(e, n, pd, pidx);
		wgsub(e, x->b, r, 1);
		wgsub(e, x->c, r, 2);
		wgneed(e, r);
		break;
	case N_WHILE:
	case N_DO:
		r = wgrec(e, n, pd, pidx);
		wgsub(e, x->b, r, 0);
		wgneed(e, r);
		break;
	case N_FOR:
		wgscan(e, x->a, -1, 0);
		r = wgrec(e, n, pd, pidx);
		wgsub(e, c->lists[x->c + 1], r, 0);
		wgneed(e, r);
		break;
	case N_SWITCH: {
		/* its body's segments: the case labels and these */
		const node *b = N(x->b);

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

static void wgoto(ecx *e, uint32_t lb, uint32_t loc)
{
	wst *W = e->w;
	uint32_t lo = 0, hi = W->ngl;
	const wgp *L = W->gp + W->ngp, *p = NULL;
	int d, a;
	uint32_t aidx;

	while (lo < hi) {
		uint32_t m = (lo + hi) / 2;

		if (L[m].label == lb) {
			p = &L[m];
			break;
		}
		if (L[m].label < lb)
			lo = m + 1;
		else
			hi = m;
	}
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
	wvla_leave(e, W->gd[a].loop, aidx);
	wbr(e, "br", W->gd[a].loop);
}

/* a loop's test, skipped once when the loop is entered by goto (st != 0) */
static void wgtest(ecx *e, uint32_t st, uint32_t cond, int out)
{
	wst *W = e->w;

	wget(e, st);
	wi(e, "i32.eqz");
	wblock(e, "if");
	if (cond) {
		wtruth(e, cond);
		wi(e, "i32.eqz");
		wbr(e, "br_if", out);
	}
	wend(e, "end_if");
	wconst(e, WT_I32, 0);
	wset(e, st);
	(void)W;
}

/* a compound statement with a dispatch loop */
static void wgblock(ecx *e, const node *x, int r)
{
	cc *c = e->c;
	wst *W = e->w;
	wgd *d = &W->gd[r];
	uint32_t m = d->nseg;
	int *sb = malloc(m * sizeof(int));
	tbuf *b = &W->b;

	d->loop = wblock(e, "loop");
	if (m > 1) {
		for (uint32_t j = m; j-- > 0;)
			sb[j] = wblock(e, "block");
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
		for (uint32_t i = s0; i < s1; i++)
			wstmt(e, c->lists[x->b + i]);
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
	if (vm_type(c, y->ty))
		wvla_fix(e, y->ty, false);
	if (T(c, y->ty)->kind == TY_ARRAY && (T(c, y->ty)->flags & TF_VLA)) {
		/* below the stack pointer, as alloca; the block gives it back (wvla_block) */
		wst *W = e->w;
		uint32_t al = sym_align(c, s) > 16 ? sym_align(c, s) : 16;

		b = &W->sb[s];
		if (b->gen != W->gen) {
			b->gen = W->gen;
			b->k = WB_PTR;
			b->loc = wnewloc(e, WT_I64);
		}
		W->dyn = true;
		wi(e, "global.get\t__stack_pointer");
		wvla_bytes(e, y->ty);
		wi(e, "i64.sub");
		wconst(e, WT_I64, -(int64_t)al);
		wi(e, "i64.and");
		wtee(e, b->loc);
		wi(e, "global.set\t__stack_pointer");
		if (x->b) {
			/* = {}: zeroed */
			wget(e, b->loc);
			wconst(e, WT_I32, 0);
			wvla_bytes(e, y->ty);
			wi(e, "memory.fill\t0");
		}
		return;
	}
	if (sym_attrs(c, s)->cleanup)
		efatal(e, x->loc, "wasm: the cleanup attribute is not supported yet (PLAN-wasm.md W4)");
	b = wlocal(e, s);
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

static void wswitch(ecx *e, uint32_t n)
{
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

	if (k == WT_MEM)
		efatal(e, x->loc, "wasm: a switch on a 128-bit value is not supported yet");
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
	ncase = W->nsw - sw0;
	t = wtmp(e, k);
	if (g) {
		/* entered by goto: the controlling expression is not evaluated */
		wget(e, g->st);
		wi(e, "i32.eqz");
		wblock(e, "if");
		wexpr(e, x->a, true);
		wset(e, t);
		wend(e, "end_if");
	} else {
		wexpr(e, x->a, true);
		wset(e, t);
	}
	brk = wblock(e, "block");
	if (g)
		g->loop = wblock(e, "loop");
	sb = malloc((m + 1) * sizeof(int));
	for (uint32_t j = m; j-- > 0;)
		sb[j] = wblock(e, "block");
	if (g) {
		/* st - 1: the statement to start at */
		tbuf *b = &W->b;
		uint32_t j = 0;

		wget(e, g->st);
		wblock(e, "if");
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
	if (ncase >= 4 && !ranges && hi - lo < 4 * (i128)ncase + 8 && hi - lo < 65536) {
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
			wbr(e, "br_if", dflt);
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
			wbr(e, "br_if", sb[s->seg]);
		}
		wbr(e, "br", dseg != UINT32_MAX ? sb[dseg] : brk);
	}
	wfree(e, t);
	W->brk = brk;
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

static void wreturn(ecx *e, const node *x)
{
	cc *c = e->c;
	wst *W = e->w;

	if (x->a) {
		qty t = N(x->a)->ty;
		wabi r = wclassify_ret(c, W->rty);

		if (r.k == WA_IND) {
			wget(e, W->sret);
			wexpr(e, x->a, true);
			wcopy(e, tsize(c, W->rty));
		} else if (r.k == WA_SCALAR && is_record(c, W->rty)) {
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
	wbr(e, "br", 0);
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
		uint32_t first = wvla_block(c, x);
		bool vla = first != UINT32_MAX;

		if (vla) {
			/* the stack pointer to give back when the block is left */
			if (W->nvb >= WMAXGD)
				efatal(e, x->loc, "wasm: too many blocks");
			W->vb[W->nvb].depth = W->depth;
			W->vb[W->nvb].first = first;
			W->vb[W->nvb].sp = wnewloc(e, WT_I64);
			W->dyn = true;
			wi(e, "global.get\t__stack_pointer");
			wset(e, W->vb[W->nvb++].sp);
		}
		if (r >= 0) {
			wgblock(e, x, r);
		} else {
			for (uint32_t i = 0; i < x->c; i++)
				wstmt(e, c->lists[x->b + i]);
		}
		if (vla) {
			wget(e, W->vb[--W->nvb].sp);
			wi(e, "global.set\t__stack_pointer");
		}
		break;
	}
	case N_EXPR:
		if (x->a)
			wexpr(e, x->a, false);
		break;
	case N_DECL:
		wdecl_stmt(e, x);
		break;
	case N_IF: {
		int r = W->ngdx ? wgfind(W, n) : -1;

		if (r >= 0) {
			/* entered by goto (st: 1 then, 2 else): the condition is not evaluated */
			uint32_t st = W->gd[r].st;

			wget(e, st);
			wi(e, "i32.eqz");
			wblock(e, "if\ti32");
			wtruth(e, x->a);
			wi(e, "else");
			wget(e, st);
			wconst(e, WT_I32, 1);
			wi(e, "i32.eq");
			wconst(e, WT_I32, 0);
			wset(e, st);
			wend(e, "end_if");
		} else {
			wtruth(e, x->a);
		}
		wblock(e, "if");
		wstmt(e, x->b);
		if (x->c) {
			wi(e, "else");
			wstmt(e, x->c);
		}
		wend(e, "end_if");
		break;
	}
	case N_WHILE: {
		int ob = W->brk, oc = W->cont, b = wblock(e, "block"), l = wblock(e, "loop");
		int r = W->ngdx ? wgfind(W, n) : -1;

		if (r >= 0)
			wgtest(e, W->gd[r].st, x->a, b);
		else {
			wtruth(e, x->a);
			wi(e, "i32.eqz");
			wbr(e, "br_if", b);
		}
		W->brk = b, W->cont = l;
		wstmt(e, x->b);
		wbr(e, "br", l);
		W->brk = ob, W->cont = oc;
		wend(e, "end_loop");
		wend(e, "end_block");
		break;
	}
	case N_DO: {
		int ob = W->brk, oc = W->cont, b = wblock(e, "block"), l = wblock(e, "loop"), k = wblock(e, "block");

		W->brk = b, W->cont = k;
		wstmt(e, x->b);
		W->brk = ob, W->cont = oc;
		wend(e, "end_block");
		wtruth(e, x->a);
		wbr(e, "br_if", l);
		wend(e, "end_loop");
		wend(e, "end_block");
		break;
	}
	case N_FOR: {
		uint32_t step = c->lists[x->c], body = c->lists[x->c + 1];
		int ob = W->brk, oc = W->cont, b, l, k;
		int r = W->ngdx ? wgfind(W, n) : -1;

		if (x->a) {
			if (r >= 0) {
				/* entered by goto: no initialization */
				wget(e, W->gd[r].st);
				wi(e, "i32.eqz");
				wblock(e, "if");
				wstmt(e, x->a);
				wend(e, "end_if");
			} else {
				wstmt(e, x->a);
			}
		}
		b = wblock(e, "block");
		l = wblock(e, "loop");
		if (r >= 0)
			wgtest(e, W->gd[r].st, x->b, b);
		else if (x->b) {
			wtruth(e, x->b);
			wi(e, "i32.eqz");
			wbr(e, "br_if", b);
		}
		k = wblock(e, "block");
		W->brk = b, W->cont = k;
		wstmt(e, body);
		W->brk = ob, W->cont = oc;
		wend(e, "end_block");
		if (step)
			wexpr(e, step, false);
		wbr(e, "br", l);
		wend(e, "end_loop");
		wend(e, "end_block");
		break;
	}
	case N_SWITCH:
		wswitch(e, n);
		break;
	case N_CASE:
	case N_DEFAULT:
		efatal(e, x->loc, "wasm: a case label inside a nested statement is not supported yet (PLAN-wasm.md W4)");
	case N_LABEL:
		wstmt(e, x->c);
		break;
	case N_GOTO:
		wgoto(e, x->a, x->loc);
		break;
	case N_GOTO_IND: {
		/* compare with the number of every label whose address is taken */
		uint32_t t = wtmp(e, WT_I64);
		const wgp *L = W->gp + W->ngp;

		wexpr(e, x->a, true);
		wset(e, t);
		for (uint32_t i = 0; i < W->ngl; i++) {
			if (!c->labels[L[i].label].addr_taken || (i && L[i - 1].label == L[i].label))
				continue;
			wget(e, t);
			wconst(e, WT_I64, (int64_t)L[i].label + 1);
			wi(e, "i64.eq");
			wblock(e, "if");
			wgoto(e, L[i].label, x->loc);
			wend(e, "end_if");
		}
		wfree(e, t);
		wi(e, "unreachable");
		break;
	}
	case N_BREAK:
	case N_CONTINUE: {
		int t = x->op == N_BREAK ? W->brk : W->cont;

		if (t < 0)
			efatal(e, x->loc, "break/continue outside a loop");
		wvla_leave(e, t, UINT32_MAX);
		wbr(e, "br", t);
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
