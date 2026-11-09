/* vcc code generator for WebAssembly: calls, variadic arguments, builtins (wimpl.h) */
#include "wimpl.h"

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

static bool wlib_type(const char **pp, int enc, uint8_t *out)
{
	const char *s = *pp;
	int lng = 0;

	if (!enc) {
		switch (*s++) {
		case 'v': *out = WT_NONE; break;
		case 'b': case 'C': case 'h': case 'i': case 'u': *out = WT_I32; break;
		case 'l': case 'L': case 'w': case 'W': case 'z': case 'p': case 'P': case 's': case 'S': case 'V':
			*out = WT_I64;
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
		*out = lng ? WT_I64 : WT_I32;
		break;
	case 'f': *out = WT_F32; break;
	case 'd':
		if (lng)
			return false;
		*out = WT_F64;
		break;
	case 'z': case 'Y': case 'W': case 'P': *out = WT_I64; break;
	default: return false;
	}
	for (; *s == '*' || *s == 'C' || *s == 'D' || *s == 'R' || (*s >= '0' && *s <= '9'); s++)
		if (*s == '*')
			*out = WT_I64;
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
	if (!p || !wlib_type(&p, enc, &g->r))
		return false;
	g->np = 0;
	g->va = false;
	while (*p) {
		if (*p == '.') {
			g->va = true;
			break;
		}
		if (g->np == 16 || !wlib_type(&p, enc, &g->p[g->np]) || g->p[g->np] == WT_NONE)
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
		*size = 8;
		*align = 8;
		return;
	}
	*size = tsize(c, t);
	*align = talign(c, t);
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
		uint32_t ta = wtmp(e, WT_I64);

		wexpr(e, n, true);
		wtee(e, ta);
		wiu(e, "i64.load", 0);
		wget(e, ta);
		wiu(e, "i64.load", 8);
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
			r = (wabi){ lg.r == WT_NONE ? WA_IGNORE : WA_SCALAR, lg.r, f->base };
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
					warg(e, an, at, (wabi){ WA_IND, WT_I64, at });
					wiu(e, "i64.store", 0);
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

			for (int j = 0; j < (pa.k == WA_PAIR ? 2 : pa.k == WA_IGNORE ? 0 : 1); j++) {
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
			wconst(e, WT_I64, 0);
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
		if (lg.va)
			tb_s(sig, lg.np ? ", i64" : "i64");
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
		return WT_I64;
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
			return WT_I64;
		}
		return WT_NONE;
	}
	if (is_record(c, f->base)) {
		/* a single-element record: back into memory */
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
		return WT_I64;
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
	uint32_t tp = wtmp(e, WT_I64), ta = UINT32_MAX;
	wlv l;
	int k = wty(c, t);

	wva_slot(c, t, &sz, &al, &ind);
	wlval(e, x->a, &l, vt);
	if (l.k == WL_MEM) {
		ta = wtmp(e, WT_I64);
		wtee(e, ta);
		wget(e, ta);
	}
	wload(e, &l, vt);
	if (al > 4) {
		wconst(e, WT_I64, al - 1);
		wi(e, "i64.add");
		wconst(e, WT_I64, -(int64_t)al);
		wi(e, "i64.and");
	}
	wtee(e, tp);
	wconst(e, WT_I64, (int64_t)((sz + 3) & ~(uint64_t)3));
	wi(e, "i64.add");
	wstore(e, &l, vt);
	if (ta != UINT32_MAX)
		wfree(e, ta);
	wget(e, tp);
	wfree(e, tp);
	if (ind) {
		wiu(e, "i64.load", 0);
		return WT_I64;
	}
	if (k == WT_MEM)
		return WT_I64;
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
	case BI_FRAME_ADDRESS:
		wexpr(e, warg_n(c, n, 0), false);
		wconst(e, WT_I64, 0);
		return WT_I64;
	case BI_ALLOCA:
	case BI_ALLOCA_UNINIT: {
		uint32_t t = wtmp(e, WT_I64);
		uint32_t a = warg_n(c, n, 0);

		e->w->dyn = true;
		wi(e, "global.get\t__stack_pointer");
		wexpr(e, a, true);
		wconv(e, N(a)->ty, prim(TY_ULONG));
		wconst(e, WT_I64, 15);
		wi(e, "i64.add");
		wconst(e, WT_I64, -16);
		wi(e, "i64.and");
		wi(e, "i64.sub");
		wtee(e, t);
		wi(e, "global.set\t__stack_pointer");
		wget(e, t);
		wfree(e, t);
		return WT_I64;
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
	case BI_ROTL32: case BI_ROTR32: case BI_ROTL64: case BI_ROTR64: {
		int ak = x->aux == BI_ROTL32 || x->aux == BI_ROTR32 ? WT_I32 : WT_I64;
		uint32_t b = warg_n(c, n, 1);

		wexpr(e, warg_n(c, n, 0), true);
		wexpr(e, b, true);
		if (wrep(wty(c, N(b)->ty)) != ak)
			wi(e, ak == WT_I64 ? "i64.extend_i32_u" : "i32.wrap_i64");
		wop(e, ak, x->aux == BI_ROTL32 || x->aux == BI_ROTL64 ? "rotl" : "rotr");
		return k;
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
	case BI_ATOMIC_THREAD_FENCE:
	case BI_ATOMIC_SIGNAL_FENCE:
	case BI_SYNC_SYNCHRONIZE:
		for (uint32_t i = 0; i < x->c; i++)
			wexpr(e, warg_n(c, n, i), false);
		return WT_NONE;
	}
	if (cc_eval(c, n, &cv) && (cv.kind == CV_INT || (cv.kind == CV_FLT && !cv.ld))) {
		if (k == WT_F32 || k == WT_F64)
			wfconst(e, k, cv.kind == CV_FLT ? cv.f : (double)cv.i);
		else if (k == WT_I32 || k == WT_I64)
			wconst(e, k, (int64_t)em_trunc_to(c, cv.i, x->ty));
		else if (k == WT_NONE)
			return WT_NONE;
		else
			efatal(e, x->loc, "wasm: 128-bit builtin constant");
		return k;
	}
	if (x->aux == BI_CONSTANT_P) {
		wconst(e, WT_I32, 0);
		return WT_I32;
	}
	if (x->aux == BI_OBJECT_SIZE || x->aux == BI_DYNAMIC_OBJECT_SIZE) {
		int64_t ty = 0;

		if (x->c > 1 && cc_eval(c, warg_n(c, n, 1), &cv) && cv.kind == CV_INT)
			ty = (int64_t)cv.i;
		wexpr(e, warg_n(c, n, 0), false);
		wconst(e, WT_I64, ty & 2 ? 0 : -1);
		return WT_I64;
	}
	efatal(e, x->loc, "wasm: builtin %d is not supported yet (PLAN-wasm.md W4)", x->aux);
}
