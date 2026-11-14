/* vcc code generator for WebAssembly: state, locals, frame, functions (wimpl.h) */
#include <stdarg.h>
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
		vtext_init(&W->b, (size_t)1 << 32);
		vtext_init(&W->fn, (size_t)1 << 32);
		vtext_init(&W->decl, (size_t)1 << 30);
		vtext_init(&W->sig, (size_t)1 << 24);
		W->lt = reserve(WMAXLOC);
		for (int i = 0; i < 5; i++)
			W->fl[i] = reserve(WMAXLOC * sizeof(uint32_t));
		W->sw = reserve(WMAXSW * sizeof(wswc));
		W->seg = reserve(WMAXSW * sizeof(uint32_t));
		W->gd = reserve(WMAXGD * sizeof(wgd));
		W->gdx = reserve(WMAXGD * sizeof(uint64_t));
		W->gp = reserve(2 * WMAXGD * sizeof(wgp));
		W->vszn = reserve(WMAXGD * sizeof(uint32_t));
		W->vszl = reserve(WMAXGD * sizeof(uint32_t));
		W->vb = reserve(WMAXGD * sizeof(wvb));
		W->cl = reserve(WMAXGD * sizeof(wvb));
		W->sj = reserve(WMAXGD * sizeof(uint32_t));
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
static ast_qualified_type wsingle(cc *c, ast_qualified_type t)
{
	for (;;) {
		const ctype *y = T(c, t);

		if (y->kind == AST_TYPE_ARRAY && y->n == 1 && !(y->flags & (AST_TYPE_FLAG_VLA | AST_TYPE_FLAG_INCOMPLETE))) {
			t = y->base;
			continue;
		}
		if (y->kind != AST_TYPE_STRUCT && y->kind != AST_TYPE_UNION)
			return t;
		{
			const tag *r = &c->tags[y->aux];
			ast_qualified_type found = 0;

			if (r->flags & (AST_RECORD_FLAG_FLEXIBLE | AST_RECORD_FLAG_COMPLEX))
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

VPRIV wabi wclassify(cc *c, ast_qualified_type t)
{
	int k = wty(c, t);

	if (k != WT_MEM)
		return (wabi){ WA_SCALAR, (uint8_t)k, t, 0 };
	if (is_record(c, t)) {
		ast_qualified_type s;

		if (tsize(c, t) == 0)
			return (wabi){ WA_IGNORE, 0, 0, 0 };
		s = wsingle(c, t);
		if (s && is_bvec(c, s))
			return wclassify(c, s);
		if (s && !is_record(c, s) && !is_vec(c, s)) {
			k = wty(c, s);
			if (k != WT_MEM)
				return (wabi){ WA_SCALAR, (uint8_t)k, s, 0 };
			if (tsize(c, s) == 16 && !is_array(c, s))
				return (wabi){ WA_PAIR, WT_I64, s, 2 };
		}
		return (wabi){ WA_IND, (uint8_t)WPT(c), t, 0 };
	}
	if (tsize(c, t) == 16 && (is_integer(c, t) || is_flt(c, t)))
		return (wabi){ WA_PAIR, WT_I64, t, 2 };
	if (is_vec(c, t)) {
		/* lane by lane (no simd128), as clang */
		ast_qualified_type b = T(c, t)->base;

		return (wabi){ WA_PAIR, (uint8_t)wrep(c, wty(c, b)), t, T(c, t)->ext ?: T(c, t)->n };
	}
	return (wabi){ WA_IND, (uint8_t)WPT(c), t, 0 };
}

VPRIV wabi wclassify_ret(cc *c, ast_qualified_type t)
{
	wabi a;

	if (is_void(c, t))
		return (wabi){ WA_IGNORE, 0, 0, 0 };
	a = wclassify(c, t);
	if (a.k == WA_PAIR && a.n == 1 && is_vec(c, t))
		return (wabi){ WA_SCALAR, a.st, T(c, t)->base, 0 };	/* one lane: as it is, as LLVM */
	if (a.k == WA_PAIR)
		a.k = WA_IND;	/* an i128 or vector result: through a pointer, as LLVM demotes it */
	return a;
}

/* K&R: what a caller without a prototype passes for a parameter of type t */
VPRIV ast_qualified_type wpromote(cc *c, ast_qualified_type t)
{
	if (KIND(c, t) == AST_TYPE_FLOAT)
		return prim(AST_TYPE_DOUBLE);
	if (is_integer(c, t) && tsize(c, t) < 4)
		return prim(AST_TYPE_INT);
	return t;
}

static void wsig_param(cc *c, tbuf *b, ast_qualified_type t, bool *first)
{
	wabi a = wclassify(c, t);

	if (a.k == WA_IGNORE)
		return;
	for (uint32_t i = 0; i < wparts(a); i++) {
		if (!*first)
			vtext_string_append(b, ", ");
		*first = false;
		vtext_string_append(b, wt_name[a.st]);
	}
}

/*
 * The signature "(params) -> (results)": of the function type ft, or with
 * call (an AST_NODE_CALL node) of the call's arguments (no prototype: the default
 * promotions; variadic: the named ones and the argument buffer's address);
 * def: a K&R definition of ft (its parameters, promoted).
 */
VPRIV void wsig(ecx *e, tbuf *b, ast_qualified_type ft, uint32_t call, uint32_t def, bool *sret)
{
	cc *c = e->c;
	const ctype *f = T(c, ft);
	wabi r = wclassify_ret(c, f->base);
	bool first = true;

	vtext_char_append(b, '(');
	if (r.k == WA_IND) {
		vtext_string_append(b, wt_name[WPT(c)]);
		first = false;
	}
	if (sret)
		*sret = r.k == WA_IND;
	if ((f->flags & AST_TYPE_FLAG_NO_PROTOTYPE) && call) {
		const node *x = N(call);

		for (uint32_t i = 0; i < x->c; i++)
			wsig_param(c, b, N(c->lists[x->b + i])->ty, &first);
	} else if ((f->flags & AST_TYPE_FLAG_NO_PROTOTYPE) && def && c->syms[def].body && N(c->syms[def].body)->op == AST_NODE_BLOCK) {
		const node *bd = N(c->syms[def].body);

		for (uint32_t i = 0; i < bd->aux; i++)
			wsig_param(c, b, wpromote(c, c->syms[c->lists[bd->a + i]].ty), &first);
	} else {
		for (uint32_t i = 0; i < f->n; i++)
			wsig_param(c, b, c->params[f->aux + i], &first);
	}
	if (f->flags & AST_TYPE_FLAG_VARIADIC) {
		if (!first)
			vtext_string_append(b, ", ");
		vtext_string_append(b, wt_name[WPT(c)]);
	}
	vtext_string_append(b, ") -> (");
	if (r.k == WA_SCALAR)
		vtext_string_append(b, wt_name[r.st]);
	vtext_char_append(b, ')');
}

VPRIV bool wis_main(cc *c, uint32_t s)
{
	const sym *y = &c->syms[s];

	return y->depth == 0 && y->kind == AST_SYMBOL_FUNCTION && EM_IDENT_IS(y->name, "main");
}

/* a function's assembler name (main is renamed as clang does for emscripten) */
VPRIV void wfname(cc *c, tbuf *b, uint32_t ref)
{
	if (RKIND(ref) == RK_SYM) {
		const sym *y = &c->syms[canon_sym(c, RIDX(ref))];

		if (wis_main(c, canon_sym(c, RIDX(ref))) && !sym_attrs(c, RIDX(ref))->asmname) {
			uint32_t np = T(c, y->ty)->n;

			if (!np && y->body && N(y->body)->op == AST_NODE_BLOCK)
				np = N(y->body)->aux;	/* K&R */
			vtext_string_append(b, np ? "__main_argc_argv" : "__original_main");
			return;
		}
	}
	ref_name(c, b, ref);
}

/* a function this one refers to: its .functype for the header (the assembler wants it before any use) */
/* static data refers to ref: a function needs its .functype (its table index is the address) */
VPRIV void wasm_data_ref(ecx *e, uint32_t ref)
{
	cc *c = e->c;

	if (RKIND(ref) != RK_SYM || c->syms[RIDX(ref)].kind != AST_SYMBOL_FUNCTION)
		return;
	if (!e->w)
		w_state(e);
	wdecl(e, ref, c->syms[RIDX(ref)].ty, NULL);
}

VPRIV void wdecl(ecx *e, uint32_t ref, ast_qualified_type ft, const char *sig)
{
	cc *c = e->c;
	tbuf *d = &e->w->decl;

	wfname(c, d, ref);
	vtext_char_append(d, '\t');
	if (sig)
		vtext_string_append(d, sig);
	else
		wsig(e, d, ft, 0, RKIND(ref) == RK_SYM ? RIDX(ref) : 0, NULL);
	vtext_char_append(d, '\n');
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

static void wfmt(tbuf *t, const char *fmt, ...)
{
	char b[1024];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(b, sizeof b, fmt, ap);
	va_end(ap);
	vtext_string_append(t, b);
}

static int wprio_cmp(const void *a, const void *b)
{
	uint64_t p = *(const uint64_t *)a, q = *(const uint64_t *)b;

	return p < q ? -1 : p > q;
}

/* destructors, as clang: per priority, .Lcall_dtors[.N](void *) calls them in
   reverse order and .Lregister_call_dtors[.N], last in .init_array[.N] (after
   the constructors of that priority), registers it with __cxa_atexit */
VPRIV void wasm_tail(cc *c, tbuf *t, _Atomic uint8_t *claimed)
{
	uint64_t *d = NULL;
	uint32_t n = 0, cap = 0;

	for (uint32_t ti = 0; ti < c->ntoplevel; ti++) {
		uint32_t s = c->toplevel[ti];
		const attrs *a = sym_attrs(c, s);

		if (c->syms[s].kind != AST_SYMBOL_FUNCTION || !a || !a->destructor || !c->syms[s].body || !claimed[s])
			continue;
		if (n == cap) {
			cap = cap ? 2 * cap : 16;
			d = realloc(d, cap * sizeof *d);
		}
		/* priority, then position: a stable order */
		d[n] = (uint64_t)(a->prio ? a->prio : 65535) << 40 | (uint64_t)n << 32 | s;
		n++;
	}
	if (!n)
		return;
	const char *P = wt_name[WPT(c)];

	qsort(d, n, sizeof *d, wprio_cmp);
	wfmt(t, "\t.functype\t__cxa_atexit (%s, %s, %s) -> (i32)\n", P, P, P);
	for (uint32_t i = 0; i < n;) {
		uint32_t p = (uint32_t)(d[i] >> 40), j = i;
		char sfx[16] = "";

		while (j < n && (uint32_t)(d[j] >> 40) == p)
			j++;
		if (p != 65535)
			snprintf(sfx, sizeof sfx, ".%u", p);
		wfmt(t, "\t.section\t.text..Lcall_dtors%s,\"\",@\n\t.type\t.Lcall_dtors%s,@function\n"
			  ".Lcall_dtors%s:\n\t.functype\t.Lcall_dtors%s (%s) -> ()\n", sfx, sfx, sfx, sfx, P);
		for (uint32_t k = j; k-- > i;) {
			uint32_t s = (uint32_t)d[k];

			vtext_string_append(t, "\tcall\t");
			wfname(c, t, REF(RK_SYM, s));
			vtext_char_append(t, '\n');
			if (!is_void(c, T(c, c->syms[s].ty)->base))
				vtext_string_append(t, "\tdrop\n");
		}
		wfmt(t, "\tend_function\n\t.section\t.text..Lregister_call_dtors%s,\"\",@\n"
			  "\t.type\t.Lregister_call_dtors%s,@function\n.Lregister_call_dtors%s:\n"
			  "\t.functype\t.Lregister_call_dtors%s () -> ()\n\tblock\n\t%s.const\t.Lcall_dtors%s\n"
			  "\t%s.const\t0\n\t%s.const\t__dso_handle\n\tcall\t__cxa_atexit\n\ti32.eqz\n"
			  "\tbr_if\t0\n\tunreachable\n\tend_block\n\tend_function\n"
			  "\t.section\t.init_array%s,\"\",@\n\t.p2align\t%d, 0x0\n\t.int%d\t.Lregister_call_dtors%s\n",
			  sfx, sfx, sfx, sfx, P, sfx, P, P, sfx, WASM32(c) ? 2 : 3, WASM32(c) ? 32 : 64, sfx);
		i = j;
	}
	vtext_string_append(t, "\t.hidden\t__dso_handle\n\t.weak\t__dso_handle\n");
	free(d);
}

VPRIV void wasm_header(cc *c, tbuf *hdr, ecx *ctx, int nthr)
{
	size_t n = 0, k = 0;
	bool tag = false;
	const char **v;

	vtext_string_append(hdr, WASM32(c) ? "\t.globaltype\t__stack_pointer, i32\n" : "\t.globaltype\t__stack_pointer, i64\n");
	vtext_string_append(hdr, "\t.tabletype\t__indirect_function_table, funcref\n");
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
		if (!strncmp(p, "__wasm_setjmp_test\t", 19))
			tag = true;
		vtext_string_append(hdr, "\t.functype\t");
		while (*p != '\t')
			vtext_char_append(hdr, *p++);
		vtext_char_append(hdr, ' ');
		vtext_append(hdr, p + 1, (size_t)(q - p - 1));
		vtext_char_append(hdr, '\n');
	}
	/* a function catches longjmp's exception (wsj_end) */
	if (tag)
		vtext_string_append(hdr, WASM32(c) ? "\t.tagtype\t__c_longjmp i32\n" : "\t.tagtype\t__c_longjmp i64\n");
	free(v);
}

/* ---------------- functions ---------------- */


/* a local in a wasm local: a scalar whose address is never taken */
VPRIV bool wreg_ok(ecx *e, uint32_t s)
{
	cc *c = e->c;
	const sym *y = &c->syms[s];
	int k = wty(c, y->ty);

	return k != WT_MEM && k != WT_NONE && em_local_eligible(e, s);
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
		W->lt[idx] = (uint8_t)WPT(c);
		W->sret = idx++;
	}
	/* the incoming locals first (their indices come before every other local) */
	for (uint32_t i = 0; i < body->aux; i++) {
		uint32_t p = c->lists[body->a + i];
		ast_qualified_type pt = (ft->flags & AST_TYPE_FLAG_NO_PROTOTYPE) ? wpromote(c, c->syms[p].ty) : c->syms[p].ty;
		wabi a = wclassify(c, pt);

		for (uint32_t j = 0; j < wparts(a); j++)
			W->lt[idx++] = a.st;
	}
	if (ft->flags & AST_TYPE_FLAG_VARIADIC) {
		W->lt[idx] = (uint8_t)WPT(c);
		W->vap = idx++;
	}
	W->nloc = W->nparam = idx;
	*np = idx;
	/* then the bindings, now that new locals can be made */
	idx = W->sret != UINT32_MAX;
	for (uint32_t i = 0; i < body->aux; i++) {
		uint32_t p = c->lists[body->a + i];
		const sym *y = &c->syms[p];
		ast_qualified_type pt = (ft->flags & AST_TYPE_FLAG_NO_PROTOTYPE) ? wpromote(c, y->ty) : y->ty;
		wabi a = wclassify(c, pt);
		wbind *b = &W->sb[p];

		b->gen = W->gen;
		b->cellid = 0;
		if (a.k == WA_IGNORE) {
			b->k = WB_FRAME;
			b->off = wslot(e, 1, 1);
			b->cellid = wmem_id(e, b->off);
			continue;
		}
		if (a.k == WA_IND) {
			b->k = WB_PTR;
			b->loc = idx++;
			continue;
		}
		if (a.k == WA_SCALAR && !is_record(c, y->ty) && wreg_ok(e, p)) {
			b->k = WB_LOC;
			if (AST_QUALIFIED_TYPE_UNQUALIFIED(pt) != AST_QUALIFIED_TYPE_UNQUALIFIED(y->ty)) {
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
		b->cellid = wmem_id(e, b->off);
		if (a.k == WA_PAIR) {
			ast_qualified_type pt1 = wpart_ty(c, a);
			uint64_t es = tsize(c, pt1);

			for (uint32_t j = 0; j < a.n; j++) {
				wlv l = { .k = WL_MEM, .off = j * es };

				wmem_lval(e, &l, b->off + j * es);

				if (is_bvec(c, a.ft)) l.off = j / 8, l.bf = 1, l.width = 1, l.bitoff = j % 8;
				wslot_addr(e, b->off);
				wget(e, idx++);
				wstore(e, &l, pt1);
			}
			continue;
		}
		{
			wlv l = { .k = WL_MEM };

			wmem_lval(e, &l, b->off);
			wslot_addr(e, b->off);
			wget(e, idx++);
			if (AST_QUALIFIED_TYPE_UNQUALIFIED(pt) != AST_QUALIFIED_TYPE_UNQUALIFIED(y->ty) && !is_record(c, y->ty))
				wconv(e, pt, y->ty);
			wstore(e, &l, is_record(c, y->ty) ? a.ft : y->ty);
		}
	}
	for (uint32_t i = 0; i < body->aux; i++) {	/* int (*a)[n]: n as on entry */
		uint32_t p = c->lists[body->a + i];

		if (vm_type(c, c->syms[p].ty))
			wvla_fix(e, c->syms[p].ty, true);
	}
}

static void wsection(ecx *e, tbuf *b, uint32_t s, const char *kind)
{
	cc *c = e->c;
	const attrs *a = sym_attrs(c, s);

	vtext_string_append(b, "\t.section\t");
	if (a->section) {
		vtext_string_append(b, c->strs[a->section - 1].s);
	} else {
		bool q = sym_quoted(c, s);
		size_t at;

		if (q)
			vtext_char_append(b, '"');	/* the whole section name quoted, as clang */
		vtext_string_append(b, kind);
		at = b->n;
		wfname(c, b, REF(RK_SYM, s));
		if (q) {
			memmove(b->p + at, b->p + at + 1, b->n - at - 1);
			b->n--;
		}
	}
	vtext_string_append(b, ",\"\",@\n");
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

	em_switch_values_reset(e);
	walk_fn(e, s);
	W->b.n = 0;
	W->fn.n = 0;
	W->gen++;
	W->nloc = W->nparam = 0;
	memset(W->nfl, 0, sizeof W->nfl);
	W->frame = 0;
	wmem_begin(e);
	W->falign = 16;
	W->fp = W->sp0 = W->rv = W->vap = W->sret = UINT32_MAX;
	W->dyn = false;
	W->depth = 0;
	W->brk = W->cont = -1;
	W->rty = ft->base;
	W->fsym = s;
	W->nsw = W->nseg = 0;
	W->nvsz = W->nvb = W->ncl = 0;
	W->bdepth = 0;
	W->sxv = W->notop = 0;
	W->nsj = 0;
	W->sjr = -1;
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
	wblock(e, "block", WF_BLOCK);	/* index 0: return branches out of it */
	if (W->nsj)
		wsj_begin(e);
	wstmt(e, y->body);
	if (W->nsj)
		wsj_end(e);
	if (W->rt != WT_NONE && wis_main(c, s)) {
		/* falling off the end of main returns 0 */
		wconst(e, W->rt, 0);
		wset(e, W->rv);
	}
	wend(e, "end_block");
	if (W->fp != UINT32_MAX || W->dyn)
		W->sp0 = wnewloc(e, WPT(c));

	wmem_optimize(e);

	/* the function: header, locals, prologue, body, epilogue */
	{
		bool global = y->sc != AST_STORAGE_STATIC && y->name && !inline_only(y);

		wsection(e, f, s, ".text.");
		if (global) {
			vtext_string_append(f, (y->flags & AST_SYMBOL_FLAG_WEAK) ? "\t.weak\t" : "\t.globl\t");
			wfname(c, f, REF(RK_SYM, s));
			vtext_char_append(f, '\n');
		}
		vtext_string_append(f, "\t.type\t");
		wfname(c, f, REF(RK_SYM, s));
		vtext_string_append(f, ",@function\n");
		wfname(c, f, REF(RK_SYM, s));
		vtext_string_append(f, ":\n\t.functype\t");
		wfname(c, f, REF(RK_SYM, s));
		vtext_char_append(f, ' ');
		{
			size_t sg = f->n;

			wsig(e, f, y->ty, 0, s, NULL);
			/* the header declares it too */
			wfname(c, &W->decl, REF(RK_SYM, s));
			vtext_char_append(&W->decl, '\t');
			vtext_append(&W->decl, f->p + sg, f->n - sg);
			vtext_char_append(&W->decl, '\n');
		}
		vtext_char_append(f, '\n');
	}
	if (W->nloc > np) {
		vtext_string_append(f, "\t.local\t");
		for (uint32_t i = np; i < W->nloc; i++) {
			if (i > np)
				vtext_string_append(f, ", ");
			vtext_string_append(f, wt_name[W->lt[i]]);
		}
		vtext_char_append(f, '\n');
	}
	if (W->sp0 != UINT32_MAX) {
		uint64_t fs = (W->frame + 15) & ~(uint64_t)15;

		vtext_string_append(f, "\tglobal.get\t__stack_pointer\n\tlocal.tee\t");
		vtext_unsigned_append(f, W->sp0);
		vtext_string_append(f, "\n");
		if (W->fp != UINT32_MAX) {
			const char *P = wt_name[WPT(c)];

			wfmt(f, "\t%s.const\t%llu\n\t%s.sub\n", P, (unsigned long long)fs, P);
			if (W->falign > 16)
				wfmt(f, "\t%s.const\t%lld\n\t%s.and\n", P, -(long long)W->falign, P);
			vtext_string_append(f, "\tlocal.tee\t");
			vtext_unsigned_append(f, W->fp);
			vtext_string_append(f, "\n");
		}
		vtext_string_append(f, "\tglobal.set\t__stack_pointer\n");
	}
	wmem_render(e, f);
	if (W->sp0 != UINT32_MAX) {
		vtext_string_append(f, "\tlocal.get\t");
		vtext_unsigned_append(f, W->sp0);
		vtext_string_append(f, "\n\tglobal.set\t__stack_pointer\n");
	}
	if (W->rt != WT_NONE) {
		vtext_string_append(f, "\tlocal.get\t");
		vtext_unsigned_append(f, W->rv);
		vtext_char_append(f, '\n');
	}
	vtext_string_append(f, "\tend_function\n");
	{
		/* a constructor: a pointer in .init_array[.prio], which the assembler turns
		   into the object's init functions */
		const attrs *a = sym_attrs(c, s);

		if (a && a->constructor) {
			vtext_string_append(f, "\t.section\t.init_array");
			if (a->prio) {
				vtext_char_append(f, '.');
				vtext_unsigned_append(f, a->prio);
			}
			vtext_string_append(f, WASM32(c) ? ",\"\",@\n\t.p2align\t2, 0x0\n\t.int32\t" : ",\"\",@\n\t.p2align\t3, 0x0\n\t.int64\t");
			wfname(c, f, REF(RK_SYM, s));
			vtext_char_append(f, '\n');
		}
		/* a destructor: registered at the end of the TU (wasm_tail) */
	}
	if (wis_main(c, s) && f->n > 0 && strstr(f->p, "__original_main:")) {
		/* main(void): what emscripten calls is main(argc, argv) (clang's wrapper) */
		const char *P = wt_name[WPT(c)];

		wfmt(f, "\t.section\t.text.main,\"\",@\n\t.globl\tmain\n\t.type\tmain,@function\nmain:\n"
			"\t.functype\tmain (i32, %s) -> (i32)\n\tcall\t__original_main\n\tend_function\n"
			"\t.globl\t__main_void\n\t.type\t__main_void,@function\n__main_void = __original_main\n", P);
		wfmt(&W->decl, "main\t(i32, %s) -> (i32)\n", P);
	}
	e->out.n = (e->out.n + 7) & ~(size_t)7;
	start = e->out.n;
	vtext_append(&e->out, f->p, f->n);
	e->E->nfn++;
	em_switch_values_emit(e);
	out_chunk(e, (uint64_t)s << 1, start);
}
