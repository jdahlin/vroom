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

/* ---------------- statements ---------------- */

static void wdecl_stmt(ecx *e, const node *x)
{
	cc *c = e->c;
	uint32_t s = x->a;
	const sym *y = &c->syms[s];
	wbind *b;

	if (y->kind != S_VAR || y->sc == SC_STATIC || y->sc == SC_EXTERN || y->sc == SC_TYPEDEF)
		return;
	if (vm_type(c, y->ty))
		efatal(e, x->loc, "wasm: variable length arrays are not supported yet (PLAN-wasm.md W4)");
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

	if (k == WT_MEM)
		efatal(e, x->loc, "wasm: a switch on a 128-bit value is not supported yet");
	/* segments: top-level statements that carry case labels */
	for (uint32_t i = 0; i < nst; i++) {
		uint32_t s = st[i];
		bool head = false;

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
	wexpr(e, x->a, true);
	wset(e, t);
	brk = wblock(e, "block");
	sb = malloc((m + 1) * sizeof(int));
	for (uint32_t j = m; j-- > 0;)
		sb[j] = wblock(e, "block");
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
	case N_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			wstmt(e, c->lists[x->b + i]);
		break;
	case N_EXPR:
		if (x->a)
			wexpr(e, x->a, false);
		break;
	case N_DECL:
		wdecl_stmt(e, x);
		break;
	case N_IF:
		wtruth(e, x->a);
		wblock(e, "if");
		wstmt(e, x->b);
		if (x->c) {
			wi(e, "else");
			wstmt(e, x->c);
		}
		wend(e, "end_if");
		break;
	case N_WHILE: {
		int ob = W->brk, oc = W->cont, b = wblock(e, "block"), l = wblock(e, "loop");

		wtruth(e, x->a);
		wi(e, "i32.eqz");
		wbr(e, "br_if", b);
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

		if (x->a)
			wstmt(e, x->a);
		b = wblock(e, "block");
		l = wblock(e, "loop");
		if (x->b) {
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
		if (c->labels[x->a].addr_taken)
			efatal(e, x->loc, "wasm: label addresses are not supported (PLAN-wasm.md W4)");
		wstmt(e, x->c);
		break;
	case N_GOTO:
	case N_GOTO_IND:
		efatal(e, x->loc, "wasm: goto is not supported yet (PLAN-wasm.md W4)");
	case N_BREAK:
	case N_CONTINUE: {
		int t = x->op == N_BREAK ? W->brk : W->cont;

		if (t < 0)
			efatal(e, x->loc, "break/continue outside a loop");
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
