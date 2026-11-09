/* vcc code generator for WebAssembly: state, locals, frame, functions (wimpl.h) */
#include "wimpl.h"

/* ---------------- per-thread state (one per ecx, from a pool) ---------------- */

static pthread_mutex_t wpool_mu = PTHREAD_MUTEX_INITIALIZER;
static wst *wpool;

VPRIV wst *w_state(ecx *e)
{
	wst *W;

	if (e->w)
		return e->w;
	pthread_mutex_lock(&wpool_mu);
	W = wpool;
	if (W)
		wpool = W->next;
	pthread_mutex_unlock(&wpool_mu);
	if (!W) {
		W = calloc(1, sizeof *W);
		tb_init(&W->b, (size_t)1 << 32);
		tb_init(&W->fn, (size_t)1 << 32);
		tb_init(&W->decl, (size_t)1 << 30);
		tb_init(&W->sig, (size_t)1 << 24);
		W->lt = reserve(WMAXLOC);
		for (int i = 0; i < 5; i++)
			W->fl[i] = reserve(WMAXLOC * sizeof(uint32_t));
		W->sw = reserve(WMAXSW * sizeof(wswc));
		W->seg = reserve(WMAXSW * sizeof(uint32_t));
		W->gd = reserve(WMAXGD * sizeof(wgd));
		W->gdx = reserve(WMAXGD * sizeof(uint64_t));
		W->gp = reserve(2 * WMAXGD * sizeof(wgp));
	}
	/* per sym bindings: sized for this TU (generation-tagged: never cleared) */
	W->sb = reserve(((size_t)e->c->nsym + 64) * sizeof(wbind));
	W->gen = 0;
	W->decl.n = 0;
	W->next = NULL;
	e->w = W;
	return W;
}

VPRIV void wasm_ecx_free(ecx *e)
{
	wst *W = e->w;

	if (!W)
		return;
	munmap(W->sb, ((size_t)e->c->nsym + 64) * sizeof(wbind));
	W->sb = NULL;
	pthread_mutex_lock(&wpool_mu);
	W->next = wpool;
	wpool = W;
	pthread_mutex_unlock(&wpool_mu);
	e->w = NULL;
}


/* ---------------- the C ABI ---------------- */

/* the one element a record is passed as (clang's isSingleElementStruct), or 0 */
static qty wsingle(cc *c, qty t)
{
	for (;;) {
		const ctype *y = T(c, t);

		if (y->kind == TY_ARRAY && y->n == 1 && !(y->flags & (TF_VLA | TF_INCOMPLETE))) {
			t = y->base;
			continue;
		}
		if (y->kind != TY_STRUCT && y->kind != TY_UNION)
			return t;
		{
			const tag *r = &c->tags[y->aux];
			qty found = 0;

			if (r->flags & (RF_FLEX | RF_COMPLEX))
				return 0;
			for (uint32_t i = 0; i < r->nfield; i++) {
				const field *f = &r->f[i];

				if (f->bitfield && (!f->name || !f->width))
					continue;
				if (tsize(c, f->ty) == 0 && !f->bitfield)
					continue;
				if (found)
					return 0;
				found = f->ty;
			}
			if (!found || tsize(c, found) != y->size)
				return 0;
			t = found;
		}
	}
}

VPRIV wabi wclassify(cc *c, qty t)
{
	int k = wty(c, t);

	if (k != WT_MEM)
		return (wabi){ WA_SCALAR, (uint8_t)k, t, 0 };
	if (is_record(c, t)) {
		qty s;

		if (tsize(c, t) == 0)
			return (wabi){ WA_IGNORE, 0, 0, 0 };
		s = wsingle(c, t);
		if (s && !is_record(c, s) && !is_vec(c, s)) {
			k = wty(c, s);
			if (k != WT_MEM)
				return (wabi){ WA_SCALAR, (uint8_t)k, s, 0 };
			if (tsize(c, s) == 16 && !is_array(c, s))
				return (wabi){ WA_PAIR, WT_I64, s, 2 };
		}
		return (wabi){ WA_IND, WT_I64, t, 0 };
	}
	if (tsize(c, t) == 16 && (is_integer(c, t) || is_flt(c, t)))
		return (wabi){ WA_PAIR, WT_I64, t, 2 };
	if (is_vec(c, t)) {
		/* lane by lane (no simd128), as clang */
		qty b = T(c, t)->base;

		return (wabi){ WA_PAIR, (uint8_t)wrep(wty(c, b)), t, (uint32_t)(tsize(c, t) / tsize(c, b)) };
	}
	return (wabi){ WA_IND, WT_I64, t, 0 };
}

VPRIV wabi wclassify_ret(cc *c, qty t)
{
	wabi a;

	if (is_void(c, t))
		return (wabi){ WA_IGNORE, 0, 0, 0 };
	a = wclassify(c, t);
	if (a.k == WA_PAIR)
		a.k = WA_IND;	/* an i128 result: through a pointer, as LLVM demotes it */
	return a;
}

/* K&R: what a caller without a prototype passes for a parameter of type t */
VPRIV qty wpromote(cc *c, qty t)
{
	if (KIND(c, t) == TY_FLOAT)
		return prim(TY_DOUBLE);
	if (is_integer(c, t) && tsize(c, t) < 4)
		return prim(TY_INT);
	return t;
}

static void wsig_param(cc *c, tbuf *b, qty t, bool *first)
{
	wabi a = wclassify(c, t);

	if (a.k == WA_IGNORE)
		return;
	for (uint32_t i = 0; i < wparts(a); i++) {
		if (!*first)
			tb_s(b, ", ");
		*first = false;
		tb_s(b, wt_name[a.st]);
	}
}

/*
 * The signature "(params) -> (results)": of the function type ft, or with
 * call (an N_CALL node) of the call's arguments (no prototype: the default
 * promotions; variadic: the named ones and the argument buffer's address);
 * def: a K&R definition of ft (its parameters, promoted).
 */
VPRIV void wsig(ecx *e, tbuf *b, qty ft, uint32_t call, uint32_t def, bool *sret)
{
	cc *c = e->c;
	const ctype *f = T(c, ft);
	wabi r = wclassify_ret(c, f->base);
	bool first = true;

	tb_c(b, '(');
	if (r.k == WA_IND) {
		tb_s(b, "i64");
		first = false;
	}
	if (sret)
		*sret = r.k == WA_IND;
	if ((f->flags & TF_NOPROTO) && call) {
		const node *x = N(call);

		for (uint32_t i = 0; i < x->c; i++)
			wsig_param(c, b, N(c->lists[x->b + i])->ty, &first);
	} else if ((f->flags & TF_NOPROTO) && def && c->syms[def].body && N(c->syms[def].body)->op == N_BLOCK) {
		const node *bd = N(c->syms[def].body);

		for (uint32_t i = 0; i < bd->aux; i++)
			wsig_param(c, b, wpromote(c, c->syms[c->lists[bd->a + i]].ty), &first);
	} else {
		for (uint32_t i = 0; i < f->n; i++)
			wsig_param(c, b, c->params[f->aux + i], &first);
	}
	if (f->flags & TF_VARIADIC) {
		if (!first)
			tb_s(b, ", ");
		tb_s(b, "i64");
	}
	tb_s(b, ") -> (");
	if (r.k == WA_SCALAR)
		tb_s(b, wt_name[r.st]);
	tb_c(b, ')');
}

VPRIV bool wis_main(cc *c, uint32_t s)
{
	const sym *y = &c->syms[s];

	return y->name && y->depth == 0 && y->kind == S_FUNC && vintern_ents[y->name].len == 4 &&
	       !memcmp(vintern_ents[y->name].s, "main", 4);
}

/* a function's assembler name (main is renamed as clang does for emscripten) */
VPRIV void wfname(cc *c, tbuf *b, uint32_t ref)
{
	if (RKIND(ref) == RK_SYM) {
		const sym *y = &c->syms[canon_sym(c, RIDX(ref))];

		if (wis_main(c, canon_sym(c, RIDX(ref))) && !sym_attrs(c, RIDX(ref))->asmname) {
			uint32_t np = T(c, y->ty)->n;

			if (!np && y->body && N(y->body)->op == N_BLOCK)
				np = N(y->body)->aux;	/* K&R */
			tb_s(b, np ? "__main_argc_argv" : "__original_main");
			return;
		}
	}
	ref_name(c, b, ref);
}

/* a function this one refers to: its .functype for the header (the assembler wants it before any use) */
VPRIV void wdecl(ecx *e, uint32_t ref, qty ft, const char *sig)
{
	cc *c = e->c;
	tbuf *d = &e->w->decl;

	wfname(c, d, ref);
	tb_c(d, '\t');
	if (sig)
		tb_s(d, sig);
	else
		wsig(e, d, ft, 0, RKIND(ref) == RK_SYM ? RIDX(ref) : 0, NULL);
	tb_c(d, '\n');
}

/* the header: .globaltype, .tabletype and every referenced function's .functype, once each */
static int wdecl_cmp(const void *a, const void *b)
{
	const char *x = *(const char *const *)a, *y = *(const char *const *)b;

	while (*x == *y && *x != '\t')
		x++, y++;
	if (*x == '\t' && *y == '\t')
		return 0;
	return *x == '\t' ? -1 : *y == '\t' ? 1 : (unsigned char)*x - (unsigned char)*y;
}

VPRIV void wasm_header(cc *c, tbuf *hdr, ecx *ctx, int nthr)
{
	size_t n = 0, k = 0;
	const char **v;

	(void)c;
	tb_s(hdr, "\t.globaltype\t__stack_pointer, i64\n\t.tabletype\t__indirect_function_table, funcref\n");
	for (int t = 0; t < nthr; t++)
		if (ctx[t].w)
			for (size_t i = 0; i < ctx[t].w->decl.n; i++)
				n += ctx[t].w->decl.p[i] == '\n';
	v = malloc((n + 1) * sizeof *v);
	for (int t = 0; t < nthr; t++) {
		const tbuf *d;

		if (!ctx[t].w)
			continue;
		d = &ctx[t].w->decl;
		for (size_t i = 0; i < d->n;) {
			v[k++] = d->p + i;
			while (d->p[i] != '\n')
				i++;
			i++;
		}
	}
	qsort(v, k, sizeof *v, wdecl_cmp);
	for (size_t i = 0; i < k; i++) {
		const char *p = v[i], *q = p;

		if (i && !wdecl_cmp(&v[i - 1], &v[i]))
			continue;	/* the first: definitions and prototypes sort the same */
		while (*q != '\n')
			q++;
		tb_s(hdr, "\t.functype\t");
		while (*p != '\t')
			tb_c(hdr, *p++);
		tb_c(hdr, ' ');
		tb_put(hdr, p + 1, (size_t)(q - p - 1));
		tb_c(hdr, '\n');
	}
	free(v);
}

/* ---------------- functions ---------------- */


/* a local in a wasm local: a scalar whose address is never taken */
VPRIV bool wreg_ok(ecx *e, uint32_t s)
{
	cc *c = e->c;
	const sym *y = &c->syms[s];
	int k = wty(c, y->ty);

	return k != WT_MEM && k != WT_NONE && !e->E->lnoreg[s] && !(y->flags & SF_ADDR_TAKEN) &&
	       !(QQ(y->ty) & Q_VOLATILE) && !sym_attrs(c, s)->cleanup && !(T(c, y->ty)->flags & TF_ATOMIC);
}

/* the parameters: wasm locals 0.. as the ABI passes them, bound to locals or frame slots */
static void wparams(ecx *e, uint32_t s, uint32_t *np)
{
	cc *c = e->c;
	wst *W = e->w;
	const node *body = N(c->syms[s].body);
	const ctype *ft = T(c, c->syms[s].ty);
	uint32_t idx = 0;

	if (wclassify_ret(c, ft->base).k == WA_IND) {
		W->lt[idx] = WT_I64;
		W->sret = idx++;
	}
	/* the incoming locals first (their indices come before every other local) */
	for (uint32_t i = 0; i < body->aux; i++) {
		uint32_t p = c->lists[body->a + i];
		qty pt = (ft->flags & TF_NOPROTO) ? wpromote(c, c->syms[p].ty) : c->syms[p].ty;
		wabi a = wclassify(c, pt);

		for (uint32_t j = 0; j < wparts(a); j++)
			W->lt[idx++] = a.st;
	}
	if (ft->flags & TF_VARIADIC) {
		W->lt[idx] = WT_I64;
		W->vap = idx++;
	}
	W->nloc = W->nparam = idx;
	*np = idx;
	/* then the bindings, now that new locals can be made */
	idx = W->sret != UINT32_MAX;
	for (uint32_t i = 0; i < body->aux; i++) {
		uint32_t p = c->lists[body->a + i];
		const sym *y = &c->syms[p];
		qty pt = (ft->flags & TF_NOPROTO) ? wpromote(c, y->ty) : y->ty;
		wabi a = wclassify(c, pt);
		wbind *b = &W->sb[p];

		b->gen = W->gen;
		if (a.k == WA_IGNORE) {
			b->k = WB_FRAME;
			b->off = wslot(e, 1, 1);
			continue;
		}
		if (a.k == WA_IND) {
			b->k = WB_PTR;
			b->loc = idx++;
			continue;
		}
		if (a.k == WA_SCALAR && !is_record(c, y->ty) && wreg_ok(e, p)) {
			b->k = WB_LOC;
			if (QUNQ(pt) != QUNQ(y->ty)) {
				/* K&R: received promoted, kept as declared */
				b->loc = wnewloc(e, wty(c, y->ty));
				wget(e, idx);
				wconv(e, pt, y->ty);
				wset(e, b->loc);
			} else {
				b->loc = idx;
			}
			idx++;
			continue;
		}
		b->k = WB_FRAME;
		b->off = wslot(e, tsize(c, y->ty), sym_align(c, p));
		if (a.k == WA_PAIR) {
			qty pt1 = wpart_ty(c, a);
			uint64_t es = tsize(c, pt1);

			for (uint32_t j = 0; j < a.n; j++) {
				wlv l = { .k = WL_MEM, .off = j * es };

				wslot_addr(e, b->off);
				wget(e, idx++);
				wstore(e, &l, pt1);
			}
			continue;
		}
		{
			wlv l = { .k = WL_MEM };

			wslot_addr(e, b->off);
			wget(e, idx++);
			if (QUNQ(pt) != QUNQ(y->ty) && !is_record(c, y->ty))
				wconv(e, pt, y->ty);
			wstore(e, &l, is_record(c, y->ty) ? a.ft : y->ty);
		}
	}
}

static void wsection(ecx *e, tbuf *b, uint32_t s, const char *kind)
{
	cc *c = e->c;
	const attrs *a = sym_attrs(c, s);

	tb_s(b, "\t.section\t");
	if (a->section) {
		tb_s(b, c->strs[a->section - 1].s);
	} else {
		tb_s(b, kind);
		wfname(c, b, REF(RK_SYM, s));
	}
	tb_s(b, ",\"\",@\n");
}

VPRIV void wasm_emit_function(ecx *e, uint32_t s)
{
	cc *c = e->c;
	const sym *y = &c->syms[s];
	const ctype *ft = T(c, y->ty);
	wst *W = w_state(e);
	tbuf *f = &W->fn;
	uint32_t np;
	size_t start;
	wabi r;

	walk_fn(e, s);
	W->b.n = 0;
	W->fn.n = 0;
	W->gen++;
	W->nloc = W->nparam = 0;
	memset(W->nfl, 0, sizeof W->nfl);
	W->frame = 0;
	W->falign = 16;
	W->fp = W->sp0 = W->rv = W->vap = W->sret = UINT32_MAX;
	W->dyn = false;
	W->depth = 0;
	W->brk = W->cont = -1;
	W->rty = ft->base;
	W->fsym = s;
	W->nsw = W->nseg = 0;
	r = wclassify_ret(c, ft->base);
	W->rt = r.k == WA_SCALAR ? r.st : WT_NONE;
	e->top = s;
	e->reach = true;
	e->curloc = y->loc;
	if (_setjmp(e->bail))
		return;
	wparams(e, s, &np);
	if (W->rt != WT_NONE)
		W->rv = wnewloc(e, W->rt);
	wgoto_scan(e, y->body);
	wblock(e, "block");	/* index 0: return branches out of it */
	wstmt(e, y->body);
	if (W->rt != WT_NONE && wis_main(c, s)) {
		/* falling off the end of main returns 0 */
		wconst(e, W->rt, 0);
		wset(e, W->rv);
	}
	wend(e, "end_block");
	if (W->fp != UINT32_MAX || W->dyn)
		W->sp0 = wnewloc(e, WT_I64);

	/* the function: header, locals, prologue, body, epilogue */
	{
		bool global = y->sc != SC_STATIC && y->name;

		wsection(e, f, s, ".text.");
		if (global) {
			tb_s(f, (y->flags & SF_WEAK) ? "\t.weak\t" : "\t.globl\t");
			wfname(c, f, REF(RK_SYM, s));
			tb_c(f, '\n');
		}
		tb_s(f, "\t.type\t");
		wfname(c, f, REF(RK_SYM, s));
		tb_s(f, ",@function\n");
		wfname(c, f, REF(RK_SYM, s));
		tb_s(f, ":\n\t.functype\t");
		wfname(c, f, REF(RK_SYM, s));
		tb_c(f, ' ');
		{
			size_t sg = f->n;

			wsig(e, f, y->ty, 0, s, NULL);
			/* the header declares it too */
			wfname(c, &W->decl, REF(RK_SYM, s));
			tb_c(&W->decl, '\t');
			tb_put(&W->decl, f->p + sg, f->n - sg);
			tb_c(&W->decl, '\n');
		}
		tb_c(f, '\n');
	}
	if (W->nloc > np) {
		tb_s(f, "\t.local\t");
		for (uint32_t i = np; i < W->nloc; i++) {
			if (i > np)
				tb_s(f, ", ");
			tb_s(f, wt_name[W->lt[i]]);
		}
		tb_c(f, '\n');
	}
	if (W->sp0 != UINT32_MAX) {
		uint64_t fs = (W->frame + 15) & ~(uint64_t)15;

		tb_s(f, "\tglobal.get\t__stack_pointer\n\tlocal.tee\t");
		tb_u(f, W->sp0);
		tb_s(f, "\n");
		if (W->fp != UINT32_MAX) {
			tb_s(f, "\ti64.const\t");
			tb_u(f, fs);
			tb_s(f, "\n\ti64.sub\n");
			if (W->falign > 16) {
				tb_s(f, "\ti64.const\t");
				tb_i(f, -(int64_t)W->falign);
				tb_s(f, "\n\ti64.and\n");
			}
			tb_s(f, "\tlocal.tee\t");
			tb_u(f, W->fp);
			tb_s(f, "\n");
		}
		tb_s(f, "\tglobal.set\t__stack_pointer\n");
	}
	tb_put(f, W->b.p, W->b.n);
	if (W->sp0 != UINT32_MAX) {
		tb_s(f, "\tlocal.get\t");
		tb_u(f, W->sp0);
		tb_s(f, "\n\tglobal.set\t__stack_pointer\n");
	}
	if (W->rt != WT_NONE) {
		tb_s(f, "\tlocal.get\t");
		tb_u(f, W->rv);
		tb_c(f, '\n');
	}
	tb_s(f, "\tend_function\n");
	if (wis_main(c, s) && f->n > 0 && strstr(f->p, "__original_main:")) {
		/* main(void): what emscripten calls is main(argc, argv) (clang's wrapper) */
		tb_s(f, "\t.section\t.text.main,\"\",@\n\t.globl\tmain\n\t.type\tmain,@function\nmain:\n"
			"\t.functype\tmain (i32, i64) -> (i32)\n\tcall\t__original_main\n\tend_function\n"
			"\t.globl\t__main_void\n\t.type\t__main_void,@function\n__main_void = __original_main\n");
		tb_s(&W->decl, "main\t(i32, i64) -> (i32)\n");
	}
	e->out.n = (e->out.n + 7) & ~(size_t)7;
	start = e->out.n;
	tb_put(&e->out, f->p, f->n);
	e->E->nfn++;
	out_chunk(e, (uint64_t)s << 1, start);
}
