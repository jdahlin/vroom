/* vcc code generator for WebAssembly: calls, variadic arguments, builtins (wimpl.h) */
#include "wimpl.h"
#include "../../../lib/f128.h"

/* the direct callee of a call: its reference (0: an indirect call) */
static uint32_t wcallee(cc *c, uint32_t fn)
{
	for (;;) {
		const node *x = N(fn);

		if (x->op == N_CAST && (x->aux == CK_FDECAY || x->aux == CK_NOP)) {
			fn = x->a;
			continue;
		}
		if (x->op == N_ADDR && is_func(c, N(x->a)->ty)) {
			fn = x->a;
			continue;
		}
		if (x->op == N_VAR && c->syms[x->a].kind == S_FUNC)
			return REF(RK_SYM, canon_sym(c, x->a));
		if (x->op == N_VAR && c->syms[x->a].kind == S_BUILTIN)
			return REF(RK_NAME, (uint32_t)c->syms[x->a].val);
		return 0;
	}
}

/* an implicitly declared library function: clang's prototype for it, as wasm types */
typedef struct wlsig { uint32_t np; uint8_t p[16], r; bool va; } wlsig;

static bool wlib_type(cc *c, const char **pp, int enc, uint8_t *out)
{
	const char *s = *pp;
	int lng = 0;

	if (!enc) {
		switch (*s++) {
		case 'v': *out = WT_NONE; break;
		case 'b': case 'C': case 'h': case 'i': case 'u': *out = WT_I32; break;
		case 'w': case 'W': *out = WT_I64; break;
		case 'l': case 'L': case 'z': case 'p': case 'P': case 's': case 'S': case 'V':
			*out = (uint8_t)WPT(c);	/* long, size_t, pointers */
			break;
		case 'd': *out = WT_F64; break;
		case 'f': *out = WT_F32; break;
		default: return false;
		}
		*pp = s;
		return true;
	}
	for (;; s++) {
		if (*s == 'L')
			lng++;
		else if (*s != 'S' && *s != 'U' && *s != 'I')
			break;
	}
	switch (*s++) {
	case 'v': *out = WT_NONE; break;
	case 'b': case 'c': case 's': case 'w': case 'Z': *out = WT_I32; break;
	case 'i':
		if (lng > 2)
			return false;
		*out = lng == 2 ? WT_I64 : lng ? (uint8_t)WPT(c) : WT_I32;
		break;
	case 'f': *out = WT_F32; break;
	case 'd':
		if (lng)
			return false;
		*out = WT_F64;
		break;
	case 'W': *out = WT_I64; break;
	case 'z': case 'Y': case 'P': *out = (uint8_t)WPT(c); break;
	default: return false;
	}
	for (; *s == '*' || *s == 'C' || *s == 'D' || *s == 'R' || (*s >= '0' && *s <= '9'); s++)
		if (*s == '*')
			*out = (uint8_t)WPT(c);
	if (*s == '&')
		return false;
	*pp = s;
	return true;
}

static bool wlib(cc *c, uint32_t ref, wlsig *g)
{
	const sym *y;
	const char *p;
	int enc;
	char nm[128];

	if (RKIND(ref) != RK_SYM)
		return false;
	y = &c->syms[RIDX(ref)];
	if (!y->name || y->body || sym_attrs(c, RIDX(ref))->asmname || vintern_ents[y->name].len >= sizeof nm)
		return false;
	memcpy(nm, vintern_ents[y->name].s, vintern_ents[y->name].len);
	nm[vintern_ents[y->name].len] = 0;
	p = cc_lib_sig(nm, &enc);
	if (!p || !wlib_type(c, &p, enc, &g->r))
		return false;
	g->np = 0;
	g->va = false;
	while (*p) {
		if (*p == '.') {
			g->va = true;
			break;
		}
		if (g->np == 16 || !wlib_type(c, &p, enc, &g->p[g->np]) || g->p[g->np] == WT_NONE)
			return false;
		g->np++;
	}
	return true;
}

/* the value on the stack from wasm type a to b (an argument to a library prototype) */
static void wretype(ecx *e, int a, int b, bool sgn)
{
	if (a == b)
		return;
	if (a == WT_I32 && b == WT_I64)
		wi(e, sgn ? "i64.extend_i32_s" : "i64.extend_i32_u");
	else if (a == WT_I64 && b == WT_I32)
		wi(e, "i32.wrap_i64");
	else if (a == WT_F32 && b == WT_F64)
		wi(e, "f64.promote_f32");
	else if (a == WT_F64 && b == WT_F32)
		wi(e, "f32.demote_f64");
	else if ((a == WT_I32 || a == WT_I64) && (b == WT_F32 || b == WT_F64)) {
		char op[32];

		snprintf(op, sizeof op, "%s.convert_%s_%c", wt_name[b], wt_name[a], sgn ? 's' : 'u');
		wi(e, op);
	} else if ((a == WT_F32 || a == WT_F64) && (b == WT_I32 || b == WT_I64)) {
		char op[32];

		snprintf(op, sizeof op, "%s.trunc_sat_%s_s", wt_name[b], wt_name[a]);
		wi(e, op);
	}
}

/* a variadic argument's place in the buffer: size and alignment (clang's emitVoidPtrVAArg) */
static void wva_slot(cc *c, qty t, uint64_t *size, uint32_t *align, bool *ind)
{
	wabi a = wclassify(c, t);

	*ind = a.k == WA_IND;
	if (*ind) {
		*size = *align = WASM32(c) ? 4 : 8;
		return;
	}
	/* slots of at least 4 bytes (clang's SlotSize: a 2-byte record takes 4) */
	*size = (tsize(c, t) + 3) & ~(uint64_t)3;
	*align = talign(c, t) < 4 ? 4 : talign(c, t);
}

/* push an argument of type t passed as a (the value of node n) */
static void warg(ecx *e, uint32_t n, qty t, wabi a)
{
	cc *c = e->c;

	switch (a.k) {
	case WA_IGNORE:
		wexpr(e, n, false);
		return;
	case WA_SCALAR:
		wexpr(e, n, true);
		if (is_record(c, t)) {
			wlv l = { .k = WL_MEM };

			wload(e, &l, a.ft);
		}
		return;
	case WA_PAIR: {
		uint32_t ta = wtmp(e, WPT(c));
		qty pt = wpart_ty(c, a);
		uint64_t es = tsize(c, pt);

		wexpr(e, n, true);
		wset(e, ta);
		for (uint32_t j = 0; j < a.n; j++) {
			wlv l = { .k = WL_MEM, .off = j * es };

			if (is_bvec(c, a.ft)) l.off = j / 8, l.bf = 1, l.width = 1, l.bitoff = j % 8;
			wget(e, ta);
			wload(e, &l, pt);
			wretype(e, wty(c, pt), a.st, is_signed(c, pt));
		}
		wfree(e, ta);
		return;
	}
	default: {
		/* a copy the callee may change */
		uint64_t sl = wslot(e, tsize(c, t), talign(c, t) > 8 ? talign(c, t) : 8);

		wslot_addr(e, sl);
		wexpr(e, n, true);
		wcopy(e, tsize(c, t));
		wslot_addr(e, sl);
		return;
	}
	}
}

/* alloca(a): below __stack_pointer, given back on return */
static int walloca(ecx *e, uint32_t a)
{
	cc *c = e->c;
	uint32_t t = wtmp(e, WPT(c));

	e->w->dyn = true;
	wi(e, "global.get\t__stack_pointer");
	wexpr(e, a, true);
	wconv(e, N(a)->ty, prim(TY_ULONG));
	wpconst(e, 15);
	wpop(e, "add");
	wpconst(e, -16);
	wpop(e, "and");
	wpop(e, "sub");
	wtee(e, t);
	wi(e, "global.set\t__stack_pointer");
	wget(e, t);
	wfree(e, t);
	return WPT(c);
}

/* with -mllvm -wasm-enable-sjlj: 1 a call to setjmp (_setjmp), 2 to longjmp (_longjmp), else 0 */
VPRIV int wsjlj_kind(cc *c, uint32_t n)
{
	uint32_t ref;
	const sym *y;
	const char *s;
	uint32_t len;

	if (!c->o->wasm_sjlj || N(n)->op != N_CALL || !(ref = wcallee(c, N(n)->a)) || RKIND(ref) != RK_SYM)
		return 0;
	y = &c->syms[RIDX(ref)];
	if (!y->name || y->sc == SC_STATIC)
		return 0;
	s = vintern_ents[y->name].s;
	len = vintern_ents[y->name].len;
	if (len && *s == '_')
		s++, len--;
	return len == 6 && !memcmp(s, "setjmp", 6) ? 1 : len == 7 && !memcmp(s, "longjmp", 7) ? 2 : 0;
}

/* setjmp(env) at site k: the value a longjmp resumed with, else __wasm_setjmp and 0 */
static int wsetjmp(ecx *e, uint32_t n, bool want)
{
	cc *c = e->c;
	wst *W = e->w;
	const node *x = N(n);
	uint32_t k = 0;

	while (k < W->nsj && W->sj[k] != n)
		k++;
	if (k == W->nsj || x->c != 1)
		efatal(e, x->loc, "wasm: setjmp here is not supported");
	wget(e, W->sjl);
	wconst(e, WT_I32, k + 1);
	wi(e, "i32.eq");
	wblock(e, "if\ti32");
	wconst(e, WT_I32, 0);
	wset(e, W->sjl);
	wget(e, W->sjv);
	wboundary(e, "else");
	wexpr(e, c->lists[x->b], true);
	wconst(e, WT_I32, k + 1);
	wslot_addr(e, W->sjfid);
	wi(e, "call\t__wasm_setjmp");
	wconst(e, WT_I32, 0);
	wend(e, "end_if");
	if (!want) {
		wi(e, "drop");
		return WT_NONE;
	}
	return WT_I32;
}

/* longjmp(env, val): __wasm_longjmp throws __c_longjmp */
static int wlongjmp(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	uint32_t a1;

	if (x->c != 2)
		efatal(e, x->loc, "wasm: longjmp with %u arguments", x->c);
	a1 = c->lists[x->b + 1];
	wexpr(e, c->lists[x->b], true);
	wexpr(e, a1, true);
	if (wty(c, N(a1)->ty) != WT_I32)
		wconv(e, N(a1)->ty, prim(TY_INT));
	tb_s(&e->w->decl, WASM32(c) ? "__wasm_longjmp\t(i32, i32) -> ()\n" : "__wasm_longjmp\t(i64, i32) -> ()\n");
	wi(e, "call\t__wasm_longjmp");
	wi(e, "unreachable");
	return WT_NONE;
}

/*
 * A call to a C library function that is one wasm instruction: clang makes
 * these its intrinsics at every -O (emscripten has no math errno), unless
 * -fno-builtin.  Returns the instruction ("f64.floor"), NULL: not one.
 */
static const char *wmath_insn(cc *c, uint32_t ref, const ctype *f)
{
	static const struct { const char *name, *op[2]; uint8_t ar; } tab[] = {
		{ "sqrt", { "f64.sqrt", "f32.sqrt" }, 1 }, { "floor", { "f64.floor", "f32.floor" }, 1 },
		{ "ceil", { "f64.ceil", "f32.ceil" }, 1 }, { "trunc", { "f64.trunc", "f32.trunc" }, 1 },
		{ "fabs", { "f64.abs", "f32.abs" }, 1 }, { "rint", { "f64.nearest", "f32.nearest" }, 1 },
		{ "nearbyint", { "f64.nearest", "f32.nearest" }, 1 },
		{ "roundeven", { "f64.nearest", "f32.nearest" }, 1 },
		{ "copysign", { "f64.copysign", "f32.copysign" }, 2 },
	};
	const sym *y;
	const char *nm;
	uint32_t len;
	int fk;

	if (c->o->no_builtin || !ref || RKIND(ref) != RK_SYM)
		return NULL;
	y = &c->syms[RIDX(ref)];
	if (!y->name || y->sc == SC_STATIC || (f->flags & (TF_NOPROTO | TF_VARIADIC)))
		return NULL;
	nm = vintern_ents[y->name].s;
	len = vintern_ents[y->name].len;
	fk = KIND(c, f->base);
	if (fk != TY_DOUBLE && fk != TY_FLOAT)
		return NULL;
	for (uint32_t i = 0; i < sizeof tab / sizeof *tab; i++) {
		size_t tl = strlen(tab[i].name);

		if (len != tl + (fk == TY_FLOAT) || memcmp(nm, tab[i].name, tl) || (fk == TY_FLOAT && nm[tl] != 'f'))
			continue;
		if (f->n != tab[i].ar)
			return NULL;
		for (uint32_t j = 0; j < f->n; j++)
			if (KIND(c, c->params[f->aux + j]) != fk)
				return NULL;
		return tab[i].op[fk == TY_FLOAT];
	}
	return NULL;
}

/*
 * __builtin_constant_p, as the arm64 generator without its inliner: 1 if the
 * front end folds the operand (a pointer only as a string literal at offset
 * 0) or it reads a character of a literal; at -O1 and up also a local never
 * written after its constant initializer; else 0.
 */
VPRIV int wconstant_p(ecx *e, uint32_t a)
{
	cc *c = e->c;
	cval cv;
	qty at = N(a)->ty;
	uint32_t r = a;

	if (cc_eval(c, a, &cv) && (cv.kind == CV_INT || cv.kind == CV_FLT ||
				   (cv.kind == CV_ADDR && cv.str && cv.i == 0 && !cv.sym && !cv.label)))
		return 1;
	if (side_effects(c, a) || !(is_integer(c, at) || is_flt(c, at)))
		return 0;
	while (N(r)->op == N_CAST && (N(r)->aux == CK_INT || N(r)->aux == CK_NOP))
		r = N(r)->a;
	if (N(r)->op == N_INDEX && cc_eval(c, N(r)->b, &cv) && cv.kind == CV_INT) {
		uint32_t s = N(r)->a;

		while (N(s)->op == N_CAST && (N(s)->aux == CK_DECAY || N(s)->aux == CK_NOP))
			s = N(s)->a;
		if (N(s)->op == N_STR && cv.i >= 0 && (uint64_t)cv.i < c->strs[N(s)->a].n)
			return 1;
	}
	if (c->o->opt && N(r)->op == N_VAR) {
		uint32_t s = N(r)->a;
		const sym *y = &c->syms[s];

		if (is_local_var(c, s) && y->sc != SC_STATIC && !(QQ(y->ty) & Q_VOLATILE) &&
		    !(y->flags & SF_ADDR_TAKEN) && !(e->E->sflags[s] & (SA_WRITTEN | SA_CWRITE)) &&
		    y->body && cc_eval(c, y->body, &cv) && cv.kind == CV_INT)
			return 1;
	}
	return 0;
}

VPRIV int wcall(ecx *e, uint32_t n, bool want)
{
	cc *c = e->c;
	const node *x = N(n);
	qty ct = N(x->a)->ty, ft = is_ptr(c, ct) ? T(c, ct)->base : ct;
	const ctype *f = T(c, ft);
	uint32_t ref = wcallee(c, x->a);
	wabi r = wclassify_ret(c, f->base);
	bool va = (f->flags & TF_VARIADIC) != 0, noproto = (f->flags & TF_NOPROTO) != 0;
	uint64_t rsl = 0, vbuf = 0, vsize = 0;
	tbuf *sig = &e->w->sig;
	const char *sp;
	int k;
	uint32_t nfix = f->n;
	wlsig lg;
	bool lib = false;

	uint32_t def = 0;
	i128 constant;

	if (c->o->opt && !e->E->noopt && em_const_strcmp(c, n, &constant)) {
		if (want)
			wconst(e, WT_I32, (int64_t)constant);
		return want ? WT_I32 : WT_NONE;
	}

	if (ref && RKIND(ref) == RK_SYM && x->c == 1 && !(c->syms[RIDX(ref)].flags & SF_DEFINED) &&
	    c->syms[RIDX(ref)].name &&
	    vintern_ents[c->syms[RIDX(ref)].name].len == 6 && !memcmp(vintern_ents[c->syms[RIDX(ref)].name].s, "alloca", 6))
		return walloca(e, c->lists[x->b]);	/* clang: alloca is a builtin, declared or not */
	switch (wsjlj_kind(c, n)) {
	case 1:
		return wsetjmp(e, n, want);
	case 2:
		return wlongjmp(e, n);
	}
	{
		const char *mi = wmath_insn(c, ref, f);

		if (mi && x->c == f->n) {
			/* sqrt(x): f64.sqrt; the arguments converted to the parameters' type */
			for (uint32_t i = 0; i < x->c; i++) {
				uint32_t a = c->lists[x->b + i];

				wexpr(e, a, true);
				if (QUNQ(N(a)->ty) != QUNQ(f->base))
					wconv(e, N(a)->ty, f->base);
			}
			wi(e, mi);
			if (!want) {
				wi(e, "drop");
				return WT_NONE;
			}
			return wty(c, f->base);
		}
	}
	if (noproto && ref && RKIND(ref) == RK_SYM && c->syms[RIDX(ref)].body &&
	    N(c->syms[RIDX(ref)].body)->op == N_BLOCK && c->syms[RIDX(ref)].kind == S_FUNC)
		def = RIDX(ref);	/* a K&R definition here: its parameters, extra arguments dropped */
	if (!def && noproto && ref && wlib(c, ref, &lg) && (lg.va ? x->c >= lg.np : x->c == lg.np)) {
		/* clang: an implicitly declared library function has its library prototype */
		bool ok = true;

		for (uint32_t i = 0; i < x->c; i++) {
			int ak = wty(c, N(c->lists[x->b + i])->ty);

			ok &= ak != WT_MEM && ak != WT_NONE;
		}
		if (ok) {
			lib = true;
			va = lg.va;
			nfix = lg.np;
			r = (wabi){ lg.r == WT_NONE ? WA_IGNORE : WA_SCALAR, lg.r, f->base, 0 };
		}
	}

	if (r.k == WA_IND) {
		rsl = wslot(e, tsize(c, f->base), talign(c, f->base) > 8 ? talign(c, f->base) : 8);
		wslot_addr(e, rsl);
	}
	if (va) {
		/* the variadic arguments' buffer, laid out first */
		uint32_t mal = 1;

		for (uint32_t i = nfix; i < x->c; i++) {
			uint64_t sz;
			uint32_t al;
			bool ind;

			wva_slot(c, N(c->lists[x->b + i])->ty, &sz, &al, &ind);
			vsize = (vsize + al - 1) & ~(uint64_t)(al - 1);
			vsize += sz;
			if (al > mal)
				mal = al;
		}
		if (x->c > nfix)
			vbuf = wslot(e, vsize, mal > 16 ? mal : 16);
	}
	{
		uint64_t vo = 0;

		for (uint32_t i = 0; i < x->c; i++) {
			uint32_t an = c->lists[x->b + i];
			qty at = N(an)->ty;

			if (va && i >= nfix) {
				uint64_t sz;
				uint32_t al;
				bool ind;
				wlv l = { .k = WL_MEM };

				wva_slot(c, at, &sz, &al, &ind);
				vo = (vo + al - 1) & ~(uint64_t)(al - 1);
				wslot_addr(e, vbuf + vo);
				if (ind) {
					warg(e, an, at, (wabi){ WA_IND, (uint8_t)WPT(c), at, 0 });
					wiu(e, WASM32(c) ? "i32.store" : "i64.store", 0);
				} else {
					wexpr(e, an, true);
					wstore(e, &l, at);
				}
				vo += sz;
				continue;
			}
			if (def) {
				const node *bd = N(c->syms[def].body);

				if (i >= bd->aux) {
					wexpr(e, an, false);
					continue;
				}
				{
					qty pt = wpromote(c, c->syms[c->lists[bd->a + i]].ty);
					wabi pa = wclassify(c, pt), aa = wclassify(c, at);

					warg(e, an, at, aa);
					if (pa.k == WA_SCALAR && aa.k == WA_SCALAR)
						wretype(e, aa.st, pa.st, is_signed(c, at));
					else if (pa.k != aa.k || (pa.k == WA_SCALAR && pa.st != aa.st))
						efatal(e, x->loc, "wasm: argument %u does not match the K&R definition's parameter", i + 1);
				}
				continue;
			}
			warg(e, an, at, wclassify(c, at));
			if (lib)
				wretype(e, wty(c, at), lg.p[i], is_signed(c, at));
		}
	}
	if (def) {
		/* parameters the call does not pass: zero */
		const node *bd = N(c->syms[def].body);

		for (uint32_t i = x->c; i < bd->aux; i++) {
			wabi pa = wclassify(c, wpromote(c, c->syms[c->lists[bd->a + i]].ty));

			for (uint32_t j = 0; j < wparts(pa); j++) {
				if (pa.st == WT_F32 || pa.st == WT_F64)
					wfconst(e, pa.st, 0);
				else
					wconst(e, pa.st, 0);
			}
		}
	}
	if (va) {
		if (x->c > nfix)
			wslot_addr(e, vbuf);
		else
			wpconst(e, 0);
	}
	/* the signature as called */
	sig->n = 0;
	if (lib) {
		tb_c(sig, '(');
		for (uint32_t i = 0; i < lg.np; i++) {
			if (i)
				tb_s(sig, ", ");
			tb_s(sig, wt_name[lg.p[i]]);
		}
		if (lg.va) {
			if (lg.np)
				tb_s(sig, ", ");
			tb_s(sig, wt_name[WPT(c)]);
		}
		tb_s(sig, ") -> (");
		tb_s(sig, wt_name[lg.r]);
		tb_c(sig, ')');
	} else {
		wsig(e, sig, ft, noproto && !def ? n : 0, def, NULL);
	}
	tb_c(sig, 0);
	sp = sig->p;
	if (ref) {
		tbuf *b = &e->w->b;

		claim_ref(e, ref);
		wdecl(e, ref, ft, noproto || lib ? sp : NULL);
		tb_s(b, "\tcall\t");
		wfname(c, b, ref);
		tb_c(b, '\n');
	} else {
		tbuf *b = &e->w->b;

		wexpr(e, x->a, true);
		tb_s(b, "\tcall_indirect\t__indirect_function_table, ");
		tb_s(b, sp);
		tb_c(b, '\n');
	}
	if (r.k == WA_IND) {
		if (!want)
			return WT_NONE;
		wslot_addr(e, rsl);
		return WPT(c);
	}
	if (lib) {
		/* the library's result as the implicit declaration's int */
		int fk = wty(c, f->base);

		if (!want) {
			if (lg.r != WT_NONE)
				wi(e, "drop");
			return WT_NONE;
		}
		if (lg.r == WT_NONE) {
			if (fk == WT_NONE)
				return WT_NONE;
			wconst(e, fk, 0);
			return fk;
		}
		wretype(e, lg.r, fk, true);
		return fk;
	}
	if (r.k == WA_IGNORE) {
		if (want && is_record(c, f->base)) {
			wslot_addr(e, wslot(e, 1, 1));	/* an empty record: any address */
			return WPT(c);
		}
		return WT_NONE;
	}
	if (is_record(c, f->base) || is_vec(c, f->base)) {
		/* a single-element record or one-lane vector: back into memory */
		uint64_t sl = wslot(e, tsize(c, f->base), talign(c, f->base));
		uint32_t t = wtmp(e, r.st);
		wlv l = { .k = WL_MEM };

		wset(e, t);
		if (!want) {
			wfree(e, t);
			return WT_NONE;
		}
		wslot_addr(e, sl);
		wget(e, t);
		wstore(e, &l, r.ft);
		wfree(e, t);
		wslot_addr(e, sl);
		return WPT(c);
	}
	k = r.st;
	return k;
}

/* ---------------- va_arg ---------------- */

static int wvaarg(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	qty t = x->ty, vt = N(x->a)->ty;
	uint64_t sz;
	uint32_t al;
	bool ind;
	uint32_t tp = wtmp(e, WPT(c)), ta = UINT32_MAX;
	wlv l;
	int k = wty(c, t);

	wva_slot(c, t, &sz, &al, &ind);
	wlval(e, x->a, &l, vt);
	if (l.k == WL_MEM) {
		ta = wtmp(e, WPT(c));
		wtee(e, ta);
		wget(e, ta);
	}
	wload(e, &l, vt);
	if (al > 4) {
		wpconst(e, al - 1);
		wpop(e, "add");
		wpconst(e, -(int64_t)al);
		wpop(e, "and");
	}
	wtee(e, tp);
	wpconst(e, (int64_t)((sz + 3) & ~(uint64_t)3));
	wpop(e, "add");
	wstore(e, &l, vt);
	if (ta != UINT32_MAX)
		wfree(e, ta);
	wget(e, tp);
	wfree(e, tp);
	if (ind) {
		wiu(e, WASM32(c) ? "i32.load" : "i64.load", 0);
		return WPT(c);
	}
	if (k == WT_MEM)
		return WPT(c);
	{
		wlv m = { .k = WL_MEM };

		wload(e, &m, t);
	}
	return k;
}

/* ---------------- builtins ---------------- */

static uint32_t warg_n(cc *c, uint32_t n, uint32_t i) { return c->lists[N(n)->b + i]; }

/* bits of a 16/32/64-bit value reversed bytewise, the value on the stack as k (i32/i64) */
static void wbswap(ecx *e, int k, int bytes)
{
	uint32_t t = wtmp(e, k), r = wtmp(e, k);

	wset(e, t);
	wconst(e, k, 0);
	wset(e, r);
	for (int i = 0; i < bytes; i++) {
		/* r |= ((t >> 8i) & 0xff) << 8(bytes-1-i) */
		wget(e, r);
		wget(e, t);
		if (i) {
			wconst(e, k, 8 * i);
			wop(e, k, "shr_u");
		}
		wconst(e, k, 0xff);
		wop(e, k, "and");
		if (bytes - 1 - i) {
			wconst(e, k, 8 * (bytes - 1 - i));
			wop(e, k, "shl");
		}
		wop(e, k, "or");
		wset(e, r);
	}
	wget(e, r);
	wfree(e, t);
	wfree(e, r);
}

/* ---------------- builtins: overflow, atomics, vectors ---------------- */

/* __builtin_{add,sub,mul}_overflow(a, b, &r): exact in 128 bits, then does r hold it */
static int wbi_overflow(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	uint32_t a = warg_n(c, n, 0), b = warg_n(c, n, 1), p = warg_n(c, n, 2);
	qty ta = N(a)->ty, tb = N(b)->ty, rt = T(c, N(p)->ty)->base;
	int op = x->aux == BI_ADD_OVERFLOW ? T_PLUS : x->aux == BI_SUB_OVERFLOW ? T_MINUS : T_STAR;
	/* u64 * u64 needs all 128 bits unsigned; every other case fits signed */
	qty v = op == T_STAR && !is_signed(c, ta) && !is_signed(c, tb) ? prim(TY_UINT128) : prim(TY_INT128);
	uint32_t tr = wtmp(e, WPT(c)), tp = wtmp(e, WPT(c));
	wlv l = { .k = WL_MEM };

	if (w16(c, ta) || w16(c, tb))
		efatal(e, x->loc, "wasm: overflow builtins with __int128 operands are not supported yet");
	wexpr(e, a, true);
	w16conv(e, ta, v);
	wexpr(e, b, true);
	w16conv(e, tb, v);
	wexpr(e, p, true);
	wset(e, tp);
	w16arith(e, op, v, 0);
	wset(e, tr);
	wget(e, tp);
	wget(e, tr);
	w16conv(e, v, rt);
	wstore(e, &l, rt);
	wget(e, tr);
	w16conv(e, v, rt);
	w16conv(e, rt, v);
	wget(e, tr);
	w16cmp(e, T_EXCLAIMEQ, v);
	if (w16(c, rt) && is_signed(c, rt) != is_signed(c, v)) {
		/* the same 128 bits, the other signedness: the top bit set is out of range */
		wget(e, tr);
		wiu(e, "i64.load", 8);
		wconst(e, WT_I64, 0);
		wi(e, "i64.lt_s");
		wi(e, "i32.or");
	}
	wfree(e, tr);
	wfree(e, tp);
	return WT_I32;
}

/* the unsigned integer type of n bytes */
static qty wuint_n(ecx *e, uint64_t n)
{
	switch (n) {
	case 1: return prim(TY_UCHAR);
	case 2: return prim(TY_USHORT);
	case 4: return prim(TY_UINT);
	case 8: return prim(TY_ULLONG);
	default: efatal(e, e->curloc, "wasm: %llu-byte atomics are not supported", (unsigned long long)n);
	}
}

/*
 * __atomic_* and __sync_*: plain loads and stores, as clang does for wasm
 * without the atomics feature (one thread).
 */
static int wbi_atomic(ecx *e, uint32_t n, bool want)
{
	cc *c = e->c;
	const node *x = N(n);
	int id = x->aux;
	qty t = QUNQ(T(c, N(warg_n(c, n, 0))->ty)->base);
	uint64_t sz = tsize(c, t);
	bool gen = id == BI_ATOMIC_LOAD || id == BI_ATOMIC_STORE || id == BI_ATOMIC_EXCHANGE || id == BI_ATOMIC_CMPXCHG;
	qty vt = gen || !(is_integer(c, t) || is_ptr(c, t)) ? wuint_n(e, sz) : t;	/* the type moved */
	int nmo = id == BI_ATOMIC_CMPXCHG_N || id == BI_ATOMIC_CMPXCHG ? 2 : id == BI_SYNC_VAL_CAS || id == BI_SYNC_BOOL_CAS || id == BI_SYNC_LOCK_RELEASE ? 0 : 1;
	int nargs = (int)x->c, k = wty(c, vt);
	uint32_t tp = wtmp(e, WPT(c)), v1 = UINT32_MAX, v2 = UINT32_MAX, cur;
	wlv l = { .k = WL_MEM };

	wexpr(e, warg_n(c, n, 0), true);
	wset(e, tp);
	/* the operands (a pointer for the generic forms), then the orders and weak */
	for (int i = 1; i < nargs; i++) {
		uint32_t a = warg_n(c, n, (uint32_t)i);
		bool opnd = i < nargs - nmo && i <= 2;

		if (id == BI_ATOMIC_CMPXCHG_N || id == BI_ATOMIC_CMPXCHG)
			opnd = i <= 2;
		if (!opnd) {
			wexpr(e, a, false);
			continue;
		}
		{
			/* the generic forms and cmpxchg's expected: pointers */
			bool ptr = gen || ((id == BI_ATOMIC_CMPXCHG_N) && i == 1);
			uint32_t tv = wtmp(e, ptr ? WPT(c) : k);

			wexpr(e, a, true);
			if (!ptr && QUNQ(N(a)->ty) != QUNQ(vt))
				wconv(e, N(a)->ty, vt);
			wset(e, tv);
			if (i == 1)
				v1 = tv;
			else
				v2 = tv;
		}
	}
	cur = wtmp(e, k);
	switch (id) {
	case BI_ATOMIC_LOAD_N:
		wget(e, tp);
		wload(e, &l, vt);
		break;
	case BI_ATOMIC_LOAD:
		wget(e, v1);
		wget(e, tp);
		wcopy(e, sz);
		break;
	case BI_ATOMIC_STORE_N:
	case BI_SYNC_LOCK_RELEASE:
		wget(e, tp);
		if (id == BI_SYNC_LOCK_RELEASE)
			wconst(e, k, 0);
		else
			wget(e, v1);
		wstore(e, &l, vt);
		break;
	case BI_ATOMIC_STORE:
		wget(e, tp);
		wget(e, v1);
		wcopy(e, sz);
		break;
	case BI_ATOMIC_EXCHANGE_N:
	case BI_ATOMIC_EXCHANGE:
		wget(e, tp);
		wload(e, &l, vt);
		wset(e, cur);
		wget(e, tp);
		wget(e, v1);
		if (gen)
			wload(e, &l, vt);
		wstore(e, &l, vt);
		if (gen) {
			wget(e, v2);
			wget(e, cur);
			wstore(e, &l, vt);
		} else {
			wget(e, cur);
		}
		break;
	case BI_ATOMIC_CMPXCHG_N:
	case BI_ATOMIC_CMPXCHG:
	case BI_SYNC_VAL_CAS:
	case BI_SYNC_BOOL_CAS: {
		bool sync = id == BI_SYNC_VAL_CAS || id == BI_SYNC_BOOL_CAS;

		wget(e, tp);
		wload(e, &l, vt);
		wtee(e, cur);
		wget(e, v1);
		if (!sync)
			wload(e, &l, vt);
		wop(e, k, "eq");
		wblock(e, "if\ti32");
		wget(e, tp);
		wget(e, v2);
		if (gen)
			wload(e, &l, vt);
		wstore(e, &l, vt);
		wconst(e, WT_I32, 1);
		wboundary(e, "else");
		if (!sync) {
			wget(e, v1);
			wget(e, cur);
			wstore(e, &l, vt);
		}
		wconst(e, WT_I32, 0);
		wend(e, "end_if");
		if (id == BI_SYNC_VAL_CAS) {
			wi(e, "drop");
			wget(e, cur);
		}
		break;
	}
	default: {
		/* fetch_op / op_fetch: add sub and or xor nand max min */
		int base = id >= BI_ATOMIC_ADD_FETCH ? id - BI_ATOMIC_ADD_FETCH : id - BI_ATOMIC_FETCH_ADD;
		static const int bop[] = { T_PLUS, T_MINUS, T_AMP, T_PIPE, T_CARET };
		qty at = is_ptr(c, vt) ? prim(TY_ULONG) : k == WT_I32 ? prim(TY_INT) : vt;
		uint32_t nv = wtmp(e, k);

		wget(e, tp);
		wload(e, &l, vt);
		wtee(e, cur);
		wget(e, v1);
		if (base == 5) {	/* nand: ~(cur & v) */
			warith(e, T_AMP, at, 0);
			wconst(e, wty(c, at), -1);
			warith(e, T_CARET, at, 0);
		} else if (base >= 6) {	/* max/min: select(cur, v, cur > v) */
			bool sg = is_signed(c, vt);

			wget(e, cur);
			wget(e, v1);
			wop(e, k, base == 6 ? (sg ? "gt_s" : "gt_u") : (sg ? "lt_s" : "lt_u"));
			wop(e, k, "select");
		} else {
			warith(e, bop[base], at, 0);
		}
		if (base < 6)
			wconv(e, at, vt);
		wset(e, nv);
		wget(e, tp);
		wget(e, nv);
		wstore(e, &l, vt);
		wget(e, id >= BI_ATOMIC_ADD_FETCH ? nv : cur);
		wfree(e, nv);
		break;
	}
	}
	wfree(e, cur);
	wfree(e, tp);
	if (v1 != UINT32_MAX)
		wfree(e, v1);
	if (v2 != UINT32_MAX)
		wfree(e, v2);
	{
		int rk = wty(c, x->ty);

		if (rk == WT_NONE)
			return WT_NONE;
		if (id == BI_ATOMIC_CMPXCHG_N || id == BI_ATOMIC_CMPXCHG || id == BI_SYNC_BOOL_CAS)
			return WT_I32;
		if (QUNQ(vt) != QUNQ(x->ty))
			wconv(e, vt, x->ty);
		(void)want;
		return rk;
	}
}

/* __builtin_shufflevector / __builtin_convertvector: lane by lane */
static int wbi_vector(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	uint32_t a0 = warg_n(c, n, 0);
	qty at = N(a0)->ty, ae = T(c, at)->base, re = T(c, x->ty)->base;
	uint32_t na = T(c, at)->ext ?: T(c, at)->n, nr = T(c, x->ty)->ext ?: T(c, x->ty)->n;
	bool sh = x->aux == BI_SHUFFLEVECTOR;
	uint32_t ta = wtmp(e, WPT(c)), tb = wtmp(e, WPT(c));
	uint64_t sl = wslot(e, tsize(c, x->ty), 16);

	wexpr(e, a0, true);
	wset(e, ta);
	if (sh) {
		wexpr(e, warg_n(c, n, 1), true);
		wset(e, tb);
	}
	for (uint32_t i = 0; i < nr; i++) {
		wlv d = { .k = WL_MEM, .off = i * tsize(c, re) };

		if (is_bvec(c, x->ty)) d.off = i / 8, d.bf = 1, d.width = 1, d.bitoff = i % 8;
		wslot_addr(e, sl);
		if (sh) {
			int64_t kx = (int64_t)c->ipool[N(warg_n(c, n, 2 + i))->a];

			if (kx < 0) {
				int rk = wty(c, re);

				if (rk == WT_F32 || rk == WT_F64)
					wfconst(e, rk, 0);
				else if (rk == WT_MEM) {
					wget(e, ta);
				} else
					wconst(e, rk, 0);
			} else {
				wlv s = { .k = WL_MEM, .off = (uint64_t)(kx < (int64_t)na ? kx : kx - (int64_t)na) * tsize(c, ae) };

				if (is_bvec(c, at)) { unsigned j = kx < (int64_t)na ? kx : kx - na; s.off = j / 8, s.bf = 1, s.width = 1, s.bitoff = j % 8; }
				wget(e, kx < (int64_t)na ? ta : tb);
				wload(e, &s, ae);
			}
		} else {
			wlv s = { .k = WL_MEM, .off = i * tsize(c, ae) };

			if (is_bvec(c, at)) s.off = i / 8, s.bf = 1, s.width = 1, s.bitoff = i % 8;
			wget(e, ta);
			wload(e, &s, ae);
			wconv(e, ae, re);
		}
		wstore(e, &d, re);
	}
	wfree(e, ta);
	wfree(e, tb);
	wslot_addr(e, sl);
	return WPT(c);
}

VPRIV int wbuiltin(ecx *e, uint32_t n, bool want)
{
	cc *c = e->c;
	const node *x = N(n);
	int k = wty(c, x->ty);
	cval cv;

	if (x->op == N_VAARG)
		return wvaarg(e, n);
	switch (x->aux) {
	case BI_VA_START: {
		wlv l;

		if (e->w->vap == UINT32_MAX)
			efatal(e, x->loc, "va_start in a function without variable arguments");
		wlval(e, warg_n(c, n, 0), &l, c->t_valist);
		wget(e, e->w->vap);
		wstore(e, &l, c->t_valist);
		return WT_NONE;
	}
	case BI_VA_END:
		wexpr(e, warg_n(c, n, 0), false);
		return WT_NONE;
	case BI_VA_COPY: {
		wlv l;

		wlval(e, warg_n(c, n, 0), &l, c->t_valist);
		wexpr(e, warg_n(c, n, 1), true);
		wstore(e, &l, c->t_valist);
		return WT_NONE;
	}
	case BI_EXPECT:
	case BI_EXPECT_PROB:
		for (uint32_t i = 1; i < x->c; i++)
			wexpr(e, warg_n(c, n, i), false);
		return wexpr(e, warg_n(c, n, 0), want);
	case BI_ASSUME_ALIGNED:
	case BI_EXTRACT_RETURN_ADDR:
		for (uint32_t i = 1; i < x->c; i++)
			wexpr(e, warg_n(c, n, i), false);
		return wexpr(e, warg_n(c, n, 0), want);
	case BI_ASSUME:
		return WT_NONE;
	case BI_PREFETCH:
		for (uint32_t i = 0; i < x->c; i++)
			wexpr(e, warg_n(c, n, i), false);
		return WT_NONE;
	case BI_UNREACHABLE:
	case BI_TRAP:
	case BI_DEBUGTRAP:
		wi(e, "unreachable");
		return WT_NONE;
	case BI_RETURN_ADDRESS:
	case BI_FRAME_ADDRESS: {
		/* level 0: the frame (on the shadow stack, below the caller's); others: 0 */
		cval lv;

		if (cc_eval(c, warg_n(c, n, 0), &lv) && lv.kind == CV_INT && lv.i == 0) {
			wget(e, wfp(e));
			return WPT(c);
		}
		wexpr(e, warg_n(c, n, 0), false);
		wpconst(e, 0);
		return WPT(c);
	}
	case BI_ALLOCA:
	case BI_ALLOCA_UNINIT: {
		int kind = walloca(e, warg_n(c, n, 0));

		for (uint32_t i = 0; i < e->w->nvb; ++i) {
			wi(e, "global.get\t__stack_pointer");
			wset(e, e->w->vb[i].sp);
		}
		return kind;
	}
	case BI_CLZ: case BI_CLZL: case BI_CLZLL:
	case BI_CTZ: case BI_CTZL: case BI_CTZLL:
	case BI_POPCOUNT: case BI_POPCOUNTL: case BI_POPCOUNTLL:
	case BI_PARITY: case BI_PARITYL: case BI_PARITYLL: {
		uint32_t a = warg_n(c, n, 0);
		qty at = N(a)->ty;
		int ak = wty(c, at);
		int b = x->aux;
		const char *op = (b == BI_CLZ || b == BI_CLZL || b == BI_CLZLL) ? "clz" :
				 (b == BI_CTZ || b == BI_CTZL || b == BI_CTZLL) ? "ctz" : "popcnt";

		wexpr(e, a, true);
		if (ak == WT_MEM)
			efatal(e, x->loc, "wasm: 128-bit bit counting is not supported yet");
		wop(e, ak, op);
		if (b == BI_PARITY || b == BI_PARITYL || b == BI_PARITYLL) {
			wconst(e, ak, 1);
			wop(e, ak, "and");
		}
		if (ak == WT_I64 && k == WT_I32)
			wi(e, "i32.wrap_i64");
		return k;
	}
	case BI_FFS: case BI_FFSL: case BI_FFSLL: {
		uint32_t a = warg_n(c, n, 0);
		int ak = wty(c, N(a)->ty);
		uint32_t t = wtmp(e, ak);

		/* x ? ctz(x) + 1 : 0 */
		wexpr(e, a, true);
		wtee(e, t);
		wop(e, ak, "ctz");
		wconst(e, ak, 1);
		wop(e, ak, "add");
		wconst(e, ak, 0);
		wget(e, t);
		wop(e, ak, "eqz");
		wi(e, "i32.eqz");
		wop(e, ak, "select");
		if (ak == WT_I64)
			wi(e, "i32.wrap_i64");
		wfree(e, t);
		return WT_I32;
	}
	case BI_CLRSB: case BI_CLRSBL: case BI_CLRSBLL: {
		uint32_t a = warg_n(c, n, 0);
		int ak = wty(c, N(a)->ty);
		uint32_t t = wtmp(e, ak);

		/* clz(x ^ (x >> (bits-1))) - 1 */
		wexpr(e, a, true);
		wtee(e, t);
		wget(e, t);
		wconst(e, ak, ak == WT_I64 ? 63 : 31);
		wop(e, ak, "shr_s");
		wop(e, ak, "xor");
		wop(e, ak, "clz");
		wconst(e, ak, 1);
		wop(e, ak, "sub");
		if (ak == WT_I64)
			wi(e, "i32.wrap_i64");
		wfree(e, t);
		return WT_I32;
	}
	case BI_BSWAP16: case BI_BSWAP32: case BI_BSWAP64: {
		int bytes = x->aux == BI_BSWAP16 ? 2 : x->aux == BI_BSWAP32 ? 4 : 8;

		wexpr(e, warg_n(c, n, 0), true);
		wbswap(e, bytes == 8 ? WT_I64 : WT_I32, bytes);
		return k;
	}
	case BI_BITREVERSE8: case BI_BITREVERSE16: case BI_BITREVERSE32: case BI_BITREVERSE64: {
		/* the bytes reversed, then the nibbles, bit pairs and bits in each byte */
		int bytes = x->aux == BI_BITREVERSE8 ? 1 : x->aux == BI_BITREVERSE16 ? 2 : x->aux == BI_BITREVERSE32 ? 4 : 8;
		int ak = bytes == 8 ? WT_I64 : WT_I32;
		static const uint64_t mask[3] = { 0x0f0f0f0f0f0f0f0full, 0x3333333333333333ull, 0x5555555555555555ull };
		uint32_t t = wtmp(e, ak);

		wexpr(e, warg_n(c, n, 0), true);
		if (bytes > 1) {
			wbswap(e, ak, bytes);
		} else {
			wconst(e, ak, 0xff);
			wop(e, ak, "and");
		}
		for (int i = 0, sh = 4; i < 3; i++, sh >>= 1) {
			int64_t m = (int64_t)(ak == WT_I64 ? mask[i] : (uint32_t)mask[i]);

			if (ak == WT_I32)
				m = (int32_t)m;
			wtee(e, t);
			wconst(e, ak, sh);
			wop(e, ak, "shr_u");
			wconst(e, ak, m);
			wop(e, ak, "and");
			wget(e, t);
			wconst(e, ak, m);
			wop(e, ak, "and");
			wconst(e, ak, sh);
			wop(e, ak, "shl");
			wop(e, ak, "or");
		}
		wfree(e, t);
		return k;
	}
	case BI_ROTL32: case BI_ROTR32: case BI_ROTL64: case BI_ROTR64: {
		int ak = x->aux == BI_ROTL32 || x->aux == BI_ROTR32 ? WT_I32 : WT_I64;
		uint32_t b = warg_n(c, n, 1);

		wexpr(e, warg_n(c, n, 0), true);
		wexpr(e, b, true);
		if (wrep(c, wty(c, N(b)->ty)) != ak)
			wi(e, ak == WT_I64 ? "i64.extend_i32_u" : "i32.wrap_i64");
		wop(e, ak, x->aux == BI_ROTL32 || x->aux == BI_ROTL64 ? "rotl" : "rotr");
		return k;
	}
	case BI_ISGREATER: case BI_ISGREATEREQUAL: case BI_ISLESS: case BI_ISLESSEQUAL:
	case BI_ISLESSGREATER: case BI_ISUNORDERED: {
		uint32_t a = warg_n(c, n, 0), b = warg_n(c, n, 1);
		qty at = N(a)->ty, bt = N(b)->ty;
		int ak = wty(c, at), bk = wty(c, bt);
		if (ak == WT_MEM || bk == WT_MEM) break;
		qty ty = prim(ak == WT_F64 || bk == WT_F64 ? TY_DOUBLE : TY_FLOAT);
		int k = wty(c, ty);
		uint32_t left = wtmp(e, k), right = wtmp(e, k);
		wexpr(e, a, true); wconv(e, at, ty); wset(e, left);
		wexpr(e, b, true); wconv(e, bt, ty); wset(e, right);
		if (x->aux == BI_ISUNORDERED) {
			wget(e, left); wget(e, left); wop(e, k, "ne");
			wget(e, right); wget(e, right); wop(e, k, "ne");
			wi(e, "i32.or");
		} else {
			wget(e, left); wget(e, right);
			const char *op = x->aux == BI_ISGREATER ? "gt" : x->aux == BI_ISGREATEREQUAL ? "ge" :
			                 x->aux == BI_ISLESSEQUAL ? "le" : "lt";
			wop(e, k, op);
			if (x->aux == BI_ISLESSGREATER) {
				wget(e, left); wget(e, right); wop(e, k, "gt");
				wi(e, "i32.or");
			}
		}
		wfree(e, left); wfree(e, right);
		return WT_I32;
	}
	case BI_ISNAN: {
		uint32_t a = warg_n(c, n, 0);
		int ak = wty(c, N(a)->ty);
		uint32_t t = wtmp(e, ak);

		if (ak != WT_F32 && ak != WT_F64)
			break;
		wexpr(e, a, true);
		wtee(e, t);
		wget(e, t);
		wop(e, ak, "ne");
		wfree(e, t);
		return WT_I32;
	}
	case BI_ISINF: case BI_ISINF_SIGN: case BI_ISFINITE: {
		/* |x| == inf, |x| < inf (false for a NaN) */
		uint32_t a = warg_n(c, n, 0);
		int ak = wty(c, N(a)->ty);

		if (ak != WT_F32 && ak != WT_F64)
			break;
		uint32_t t = x->aux == BI_ISINF_SIGN ? wtmp(e, ak) : 0;
		wexpr(e, a, true);
		if (x->aux == BI_ISINF_SIGN) wtee(e, t);
		wop(e, ak, "abs");
		wfconst(e, ak, __builtin_inf());
		wop(e, ak, x->aux == BI_ISFINITE ? "lt" : "eq");
		if (x->aux == BI_ISINF_SIGN) {
			/* isinf(x) * (1 - 2 * (x < 0)), with one argument evaluation. */
			wconst(e, WT_I32, 1);
			wget(e, t);
			wfconst(e, ak, 0);
			wop(e, ak, "lt");
			wconst(e, WT_I32, 1);
			wi(e, "i32.shl");
			wi(e, "i32.sub");
			wi(e, "i32.mul");
			wfree(e, t);
		}
		return WT_I32;
	}
	case BI_ADD_OVERFLOW: case BI_SUB_OVERFLOW: case BI_MUL_OVERFLOW:
		return wbi_overflow(e, n);
	case BI_ATOMIC_LOAD_N: case BI_ATOMIC_STORE_N: case BI_ATOMIC_EXCHANGE_N: case BI_ATOMIC_CMPXCHG_N:
	case BI_ATOMIC_LOAD: case BI_ATOMIC_STORE: case BI_ATOMIC_EXCHANGE: case BI_ATOMIC_CMPXCHG:
	case BI_ATOMIC_FETCH_ADD: case BI_ATOMIC_FETCH_SUB: case BI_ATOMIC_FETCH_AND: case BI_ATOMIC_FETCH_OR:
	case BI_ATOMIC_FETCH_XOR: case BI_ATOMIC_ADD_FETCH: case BI_ATOMIC_SUB_FETCH: case BI_ATOMIC_AND_FETCH:
	case BI_ATOMIC_OR_FETCH: case BI_ATOMIC_XOR_FETCH: case BI_ATOMIC_FETCH_NAND: case BI_ATOMIC_FETCH_MAX:
	case BI_ATOMIC_FETCH_MIN: case BI_ATOMIC_NAND_FETCH: case BI_ATOMIC_MAX_FETCH: case BI_ATOMIC_MIN_FETCH: case BI_SYNC_VAL_CAS: case BI_SYNC_BOOL_CAS:
	case BI_SYNC_LOCK_RELEASE:
		return wbi_atomic(e, n, want);
	case BI_ATOMIC_LOCK_FREE:
		for (uint32_t i = 0; i < x->c; i++)
			wexpr(e, warg_n(c, n, i), false);
		wconst(e, WT_I32, 1);
		return WT_I32;
	case BI_SHUFFLEVECTOR: case BI_CONVERTVECTOR:
		return wbi_vector(e, n);
	case BI_SIGNBIT: {
		uint32_t a = warg_n(c, n, 0);
		int ak = wty(c, N(a)->ty);

		wexpr(e, a, true);
		if (ak == WT_F32) {
			wi(e, "i32.reinterpret_f32");
			wconst(e, WT_I32, 31);
			wi(e, "i32.shr_u");
		} else {
			if (ak == WT_F64)
				wi(e, "i64.reinterpret_f64");
			else
				wiu(e, "i64.load", 8);
			wconst(e, WT_I64, 63);
			wi(e, "i64.shr_u");
			wi(e, "i32.wrap_i64");
		}
		if (k == WT_I64)
			wi(e, "i64.extend_i32_u");
		return k;
	}
	case BI_ATOMIC_THREAD_FENCE:
	case BI_ATOMIC_SIGNAL_FENCE:
	case BI_SYNC_SYNCHRONIZE:
		for (uint32_t i = 0; i < x->c; i++)
			wexpr(e, warg_n(c, n, i), false);
		return WT_NONE;
	case BI_CONSTANT_P:
		/* before the front end's fold, which says 0 for what the optimizer folds */
		if (want)
			wconst(e, WT_I32, wconstant_p(e, warg_n(c, n, 0)));
		return want ? WT_I32 : WT_NONE;
	}
	if (cc_eval(c, n, &cv) && (cv.kind == CV_INT || cv.kind == CV_FLT)) {
		if (cv.kind == CV_FLT && k == WT_MEM) {
			/* Binary128 builtins use the same address-valued representation
			   as N_FNUM, including infinities and exact NaN payloads. */
			u128 q = cv.ld ? cv.q : q_of_d(cv.f);
			uint64_t sl = wslot(e, 16, 16);
			wslot_addr(e, sl);
			wconst(e, WT_I64, (int64_t)(uint64_t)q);
			wiu(e, "i64.store", 0);
			wslot_addr(e, sl);
			wconst(e, WT_I64, (int64_t)(uint64_t)(q >> 64));
			wiu(e, "i64.store", 8);
			wslot_addr(e, sl);
			return WPT(c);
		}
		if (cv.kind == CV_FLT && cv.ld && q_isnan(cv.q) && (k == WT_F32 || k == WT_F64)) {
			/* Host floating conversions can quiet a signaling NaN. */
			wconst(e, k == WT_F32 ? WT_I32 : WT_I64,
			       (int64_t)q_nan_storage(cv.q, k == WT_F32 ? 23 : 52, k == WT_F32 ? 8 : 11));
			wi(e, k == WT_F32 ? "f32.reinterpret_i32" : "f64.reinterpret_i64");
		} else if (k == WT_F32 || k == WT_F64)
			wfconst(e, k, cv.kind == CV_FLT ? cv.ld ? q_to_d(cv.q) : cv.f : (double)cv.i);
		else if (k == WT_I32 || k == WT_I64)
			wconst(e, k, (int64_t)em_trunc_to(c, cv.i, x->ty));
		else if (k == WT_NONE)
			return WT_NONE;
		else
			efatal(e, x->loc, "wasm: 128-bit builtin constant");
		return k;
	}
	if (x->aux == BI_OBJECT_SIZE || x->aux == BI_DYNAMIC_OBJECT_SIZE) {
		int64_t ty = 0;

		if (x->c > 1 && cc_eval(c, warg_n(c, n, 1), &cv) && cv.kind == CV_INT)
			ty = (int64_t)cv.i;
		wexpr(e, warg_n(c, n, 0), false);
		wpconst(e, ty & 2 ? 0 : -1);
		return WPT(c);
	}
	efatal(e, x->loc, "wasm: builtin %d is not supported yet (PLAN-wasm.md W4)", x->aux);
}
