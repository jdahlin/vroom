/* vcc code generator for WebAssembly: expressions, lvalues, loads and stores (wimpl.h) */
#include "wimpl.h"
#include "../../../lib/vstack.h"

/* ---------------- conversions ---------------- */

/* a C integer of type t kept in an i32: re-extend it after arithmetic (from: what the bits are now) */
static void wnarrow(ecx *e, qty from, qty to)
{
	cc *c = e->c;
	uint64_t ts = tsize(c, to), fs = tsize(c, from);
	bool tsg = is_signed(c, to), fsg = is_signed(c, from) && KIND(c, from) != TY_BOOL;

	if (ts >= 4 || KIND(c, to) == TY_BOOL)
		return;
	if (is_integer(c, from) && ((fs < ts && (!fsg || tsg)) || (fs == ts && fsg == tsg)))
		return;	/* already a value of type to */
	if (tsg) {
		wi(e, ts == 1 ? "i32.extend8_s" : "i32.extend16_s");
	} else {
		wconst(e, WT_I32, ts == 1 ? 0xff : 0xffff);
		wi(e, "i32.and");
	}
}

/* the value on the stack, of C type from, as a value of type to */
VPRIV void wconv(ecx *e, qty from, qty to)
{
	cc *c = e->c;
	int fk = wty(c, from), tk = wty(c, to);
	bool fsg = is_signed(c, from) && is_integer(c, from) && KIND(c, from) != TY_BOOL;

	if (tk == WT_NONE) {
		if (fk != WT_NONE)
			wi(e, "drop");
		return;
	}
	if (fk == WT_MEM || tk == WT_MEM) {
		if (fk == tk && tsize(c, from) == tsize(c, to) && is_flt(c, from) == is_flt(c, to))
			return;
		efatal(e, e->curloc, "wasm: conversion between %s and %s is not supported yet (PLAN-wasm.md W4)",
		       fk == WT_MEM ? "a 128-bit value" : "a scalar", tk == WT_MEM ? "a 128-bit value" : "a scalar");
	}
	if (KIND(c, to) == TY_BOOL) {
		if (KIND(c, from) == TY_BOOL)
			return;
		switch (fk) {
		case WT_I32: wconst(e, WT_I32, 0); wi(e, "i32.ne"); break;
		case WT_I64: wi(e, "i64.eqz"); wi(e, "i32.eqz"); break;
		case WT_F32: wfconst(e, WT_F32, 0); wi(e, "f32.ne"); break;
		case WT_F64: wfconst(e, WT_F64, 0); wi(e, "f64.ne"); break;
		}
		return;
	}
	if (tk == WT_F32 || tk == WT_F64) {
		if (fk == tk)
			return;
		if (fk == WT_F32 || fk == WT_F64) {
			wi(e, tk == WT_F32 ? "f32.demote_f64" : "f64.promote_f32");
			return;
		}
		{
			char op[32];

			snprintf(op, sizeof op, "%s.convert_%s_%c", wt_name[tk], wt_name[fk], fsg ? 's' : 'u');
			wi(e, op);
		}
		return;
	}
	/* to an integer or a pointer */
	if (fk == WT_F32 || fk == WT_F64) {
		char op[32];
		bool tsg = is_signed(c, to) || tsize(c, to) < 4;

		snprintf(op, sizeof op, "%s.trunc_sat_%s_%c", wt_name[tk], wt_name[fk], tsg ? 's' : 'u');
		wi(e, op);
		wnarrow(e, prim(TY_INT), to);
		return;
	}
	if (fk == WT_I32 && tk == WT_I64) {
		wi(e, fsg ? "i64.extend_i32_s" : "i64.extend_i32_u");
		return;
	}
	if (fk == WT_I64 && tk == WT_I32) {
		wi(e, "i32.wrap_i64");
		wnarrow(e, prim(TY_LONG), to);
		return;
	}
	if (tk == WT_I32)
		wnarrow(e, from, to);
}

/* ---------------- lvalues ---------------- */

/* fold l's constant offset into the address on the stack */
VPRIV void wfold_off(ecx *e, wlv *l)
{
	if (l->k == WL_MEM && l->off) {
		wconst(e, WT_I64, (int64_t)l->off);
		wi(e, "i64.add");
		l->off = 0;
	}
}

/* a local's binding, made where its declaration is (or first seen: a jump past it) */
VPRIV wbind *wlocal(ecx *e, uint32_t s)
{
	cc *c = e->c;
	wst *W = e->w;
	wbind *b = &W->sb[s];

	if (b->gen == W->gen)
		return b;
	b->gen = W->gen;
	if (wreg_ok(e, s)) {
		b->k = WB_LOC;
		b->loc = wnewloc(e, wty(c, c->syms[s].ty));
	} else {
		b->k = WB_FRAME;
		b->off = wslot(e, tsize(c, c->syms[s].ty), sym_align(c, s));
	}
	return b;
}

static uint64_t welem_size(ecx *e, qty ptr)
{
	cc *c = e->c;
	qty b = T(c, ptr)->base;

	if (vm_type(c, b))
		efatal(e, e->curloc, "wasm: variable length arrays are not supported yet (PLAN-wasm.md W4)");
	if (is_void(c, b) || is_func(c, b))
		return 1;	/* GNU: void * and function pointer arithmetic */
	return tsize(c, b);
}

/* push an integer of type t as an i64 (an index, an offset) */
VPRIV void wto_i64(ecx *e, qty t)
{
	cc *c = e->c;

	if (wty(c, t) == WT_I32)
		wi(e, is_signed(c, t) && KIND(c, t) != TY_BOOL ? "i64.extend_i32_s" : "i64.extend_i32_u");
	else if (wty(c, t) != WT_I64)
		efatal(e, e->curloc, "wasm: a 128-bit index is not supported yet");
}

static bool wnum(ecx *e, uint32_t n, int64_t *v)
{
	cc *c = e->c;

	while (N(n)->op == N_CAST && (N(n)->aux == CK_INT || N(n)->aux == CK_NOP) && is_integer(c, N(n)->ty) &&
	       tsize(c, N(n)->ty) >= tsize(c, N(N(n)->a)->ty) && is_integer(c, N(N(n)->a)->ty))
		n = N(n)->a;
	if (N(n)->op != N_NUM)
		return false;
	*v = (int64_t)em_trunc_to(c, c->ipool[N(n)->a], N(n)->ty);
	return true;
}

VPRIV void wlval(ecx *e, uint32_t n, wlv *l, qty t)
{
	cc *c = e->c;
	const node *x = N(n);

	memset(l, 0, sizeof *l);
	l->k = WL_MEM;
	l->vol = (QQ(x->ty) & Q_VOLATILE) != 0;
	switch (x->op) {
	case N_VAR: {
		uint32_t s = x->a;
		const sym *y = &c->syms[s];

		if (y->kind == S_VAR && is_local_var(c, s)) {
			wbind *b = wlocal(e, s);

			if (b->k == WB_LOC) {
				l->k = WL_LOC;
				l->loc = b->loc;
				return;
			}
			if (b->k == WB_PTR) {
				wget(e, b->loc);
				break;
			}
			wget(e, wfp(e));
			l->off = b->off;
			break;
		}
		if (y->kind == S_VAR && (y->flags & SF_THREAD))
			efatal(e, x->loc, "wasm: thread-local storage is not supported yet");
		if (y->kind == S_FUNC || y->kind == S_BUILTIN) {
			wexpr(e, n, true);
			break;
		}
		waddr_ref(e, REF(RK_SYM, canon_sym(c, s)), 0);
		break;
	}
	case N_DEREF:
		wexpr(e, x->a, true);
		break;
	case N_MEMBER: {
		const field *f = &c->tags[x->b].f[x->c];
		const node *a = N(x->a);

		if ((a->flags & NF_LVALUE) || a->op == N_VAR || a->op == N_DEREF || a->op == N_MEMBER || a->op == N_INDEX) {
			wlval(e, x->a, l, a->ty);
		} else {
			wexpr(e, x->a, true);	/* an rvalue record: its address */
			l->k = WL_MEM;
		}
		if (l->k != WL_MEM)
			efatal(e, x->loc, "wasm: member of a non-memory object");
		l->off += f->off / 8;
		l->vol |= (QQ(x->ty) & Q_VOLATILE) != 0;
		if (f->bitfield) {
			l->bf = 1;
			l->bitoff = (uint16_t)(f->off % 8);
			l->width = f->width;
			l->sgn = is_signed(c, f->ty);
		}
		break;
	}
	case N_INDEX: {
		uint64_t es = welem_size(e, N(x->a)->ty);
		int64_t k;

		wexpr(e, x->a, true);
		if (wnum(e, x->b, &k) && (k * (int64_t)es) >= 0) {
			l->off = (uint64_t)(k * (int64_t)es);
			break;
		}
		wexpr(e, x->b, true);
		wto_i64(e, N(x->b)->ty);
		if (es != 1) {
			wconst(e, WT_I64, (int64_t)es);
			wi(e, "i64.mul");
		}
		wi(e, "i64.add");
		break;
	}
	case N_COMPLIT: {
		uint32_t s = x->b;
		wst *W = e->w;
		wbind *b = &W->sb[s];

		if (b->gen != W->gen) {
			b->gen = W->gen;
			b->k = WB_FRAME;
			b->off = wslot(e, tsize(c, x->ty), sym_align(c, s));
		}
		winit(e, b->off, x->ty, x->a, false);
		wget(e, wfp(e));
		l->off = b->off;
		break;
	}
	case N_STR:
		waddr_ref(e, REF(RK_STR, x->a), 0);
		break;
	case N_CAST:
		if (x->aux == CK_NOP && (N(x->a)->flags & NF_LVALUE)) {
			wlval(e, x->a, l, t);
			return;
		}
		/* fall through */
	default:
		if (wty(c, x->ty) != WT_MEM)
			efatal(e, x->loc, "wasm: unsupported lvalue (node %d)", x->op);
		wexpr(e, n, true);
		break;
	}
	if (wty(c, t) == WT_MEM)
		wfold_off(e, l);
}

/* ---------------- loads and stores ---------------- */

static int wbf_bytes(ecx *e, const wlv *l)
{
	int nb = (l->bitoff + l->width + 7) / 8;

	if (nb > 8)
		efatal(e, e->curloc, "wasm: a bit-field over 8 bytes is not supported");
	return nb <= 1 ? 1 : nb <= 2 ? 2 : nb <= 4 ? 4 : 8;
}

static void wbf_container(ecx *e, int nb, bool store, uint64_t off)
{
	static const char *const ld[] = { [1] = "i64.load8_u", [2] = "i64.load16_u", [4] = "i64.load32_u", [8] = "i64.load" };
	static const char *const st[] = { [1] = "i64.store8", [2] = "i64.store16", [4] = "i64.store32", [8] = "i64.store" };

	wiu(e, store ? st[nb] : ld[nb], off);
}

static const char *wmemop(cc *c, qty t, bool store)
{
	uint64_t sz = tsize(c, t);
	int k = wty(c, t);
	bool sg = is_signed(c, t) && KIND(c, t) != TY_BOOL;

	if (k == WT_F32)
		return store ? "f32.store" : "f32.load";
	if (k == WT_F64)
		return store ? "f64.store" : "f64.load";
	if (k == WT_I64)
		return store ? "i64.store" : "i64.load";
	switch (sz) {
	case 1: return store ? "i32.store8" : sg ? "i32.load8_s" : "i32.load8_u";
	case 2: return store ? "i32.store16" : sg ? "i32.load16_s" : "i32.load16_u";
	default: return store ? "i32.store" : "i32.load";
	}
}

VPRIV void wload(ecx *e, wlv *l, qty t)
{
	cc *c = e->c;

	if (l->k == WL_LOC) {
		wget(e, l->loc);
		return;
	}
	if (wty(c, t) == WT_MEM) {
		wfold_off(e, l);
		return;
	}
	if (l->bf) {
		int nb = wbf_bytes(e, l);

		wbf_container(e, nb, false, l->off);
		if (l->sgn) {
			wconst(e, WT_I64, 64 - l->bitoff - l->width);
			wi(e, "i64.shl");
			wconst(e, WT_I64, 64 - l->width);
			wi(e, "i64.shr_s");
		} else {
			if (l->bitoff) {
				wconst(e, WT_I64, l->bitoff);
				wi(e, "i64.shr_u");
			}
			if (l->width < 64) {
				wconst(e, WT_I64, (int64_t)((1ull << l->width) - 1));
				wi(e, "i64.and");
			}
		}
		if (wty(c, t) == WT_I32)
			wi(e, "i32.wrap_i64");
		return;
	}
	wiu(e, wmemop(c, t, false), l->off);
}

/* stack: [address] value (WL_MEM), or value (WL_LOC); records: [dst src], dst folded */
VPRIV void wstore(ecx *e, wlv *l, qty t)
{
	cc *c = e->c;

	if (l->k == WL_LOC) {
		wset(e, l->loc);
		return;
	}
	if (wty(c, t) == WT_MEM) {
		wcopy(e, tsize(c, t));
		return;
	}
	if (l->bf) {
		int nb = wbf_bytes(e, l);
		uint64_t mask = l->width >= 64 ? ~0ull : (1ull << l->width) - 1;
		uint32_t tv = wtmp(e, wty(c, t)), ta = wtmp(e, WT_I64);

		wset(e, tv);
		wtee(e, ta);
		wget(e, ta);
		wbf_container(e, nb, false, l->off);
		wconst(e, WT_I64, (int64_t)~(mask << l->bitoff));
		wi(e, "i64.and");
		wget(e, tv);
		if (wty(c, t) == WT_I32)
			wi(e, "i64.extend_i32_u");
		wconst(e, WT_I64, (int64_t)mask);
		wi(e, "i64.and");
		if (l->bitoff) {
			wconst(e, WT_I64, l->bitoff);
			wi(e, "i64.shl");
		}
		wi(e, "i64.or");
		wbf_container(e, nb, true, l->off);
		wfree(e, tv);
		wfree(e, ta);
		return;
	}
	wiu(e, wmemop(c, t, true), l->off);
}

/* stack: [dst src] -> [] */
VPRIV void wcopy(ecx *e, uint64_t n)
{
	wconst(e, WT_I64, (int64_t)n);
	wi(e, "memory.copy\t0, 0");
}

/* the value of a bit-field assignment: v (in tv) truncated to the field */
static void wbf_trunc(ecx *e, const wlv *l, qty t)
{
	cc *c = e->c;
	int k = wty(c, t), bits = k == WT_I32 ? 32 : 64;

	if (l->width >= bits)
		return;
	if (l->sgn) {
		wconst(e, k, bits - l->width);
		wop(e, k, "shl");
		wconst(e, k, bits - l->width);
		wop(e, k, "shr_s");
	} else {
		wconst(e, k, (int64_t)((1ull << l->width) - 1));
		wop(e, k, "and");
	}
}

/* ---------------- operators ---------------- */

static bool wis_cmp(int op)
{
	return op == T_EQUALEQUAL || op == T_EXCLAIMEQ || op == T_LESS || op == T_GREATER || op == T_LESSEQ ||
	       op == T_GREATEREQ;
}

/* stack: [a b] of C type ty (b of rty: a shift count) -> a op b */
static void warith(ecx *e, int op, qty ty, qty rty)
{
	cc *c = e->c;
	int k = wty(c, ty);
	bool sg = is_signed(c, ty);
	bool fl = k == WT_F32 || k == WT_F64;
	const char *o = NULL;

	if (k == WT_MEM)
		efatal(e, e->curloc, "wasm: 128-bit arithmetic is not supported yet (PLAN-wasm.md W4)");
	switch (op) {
	case T_PLUS: o = "add"; break;
	case T_MINUS: o = "sub"; break;
	case T_STAR: o = "mul"; break;
	case T_SLASH: o = fl ? "div" : sg ? "div_s" : "div_u"; break;
	case T_PERCENT: o = sg ? "rem_s" : "rem_u"; break;
	case T_AMP: o = "and"; break;
	case T_PIPE: o = "or"; break;
	case T_CARET: o = "xor"; break;
	case T_LESSLESS: o = "shl"; break;
	case T_GREATERGREATER: o = sg ? "shr_s" : "shr_u"; break;
	}
	if (!o || (fl && (op == T_PERCENT || op == T_AMP || op == T_PIPE || op == T_CARET || op == T_LESSLESS ||
			  op == T_GREATERGREATER)))
		efatal(e, e->curloc, "wasm: unsupported operator");
	if ((op == T_LESSLESS || op == T_GREATERGREATER) && rty && wrep(wty(c, rty)) != k) {
		if (wty(c, rty) == WT_MEM)
			efatal(e, e->curloc, "wasm: a 128-bit shift count is not supported yet");
		wi(e, k == WT_I64 ? "i64.extend_i32_u" : "i32.wrap_i64");
	}
	wop(e, k, o);
}

/* stack: [a b] of type t -> a op b as an i32 0/1 */
static void wcompare(ecx *e, int op, qty t)
{
	cc *c = e->c;
	int k = wty(c, t);
	bool fl = k == WT_F32 || k == WT_F64;
	bool sg = !is_ptr(c, t) && is_signed(c, t);
	char o[8];

	if (k == WT_MEM)
		efatal(e, e->curloc, "wasm: 128-bit comparisons are not supported yet (PLAN-wasm.md W4)");
	switch (op) {
	case T_EQUALEQUAL: wop(e, k, "eq"); return;
	case T_EXCLAIMEQ: wop(e, k, "ne"); return;
	case T_LESS: strcpy(o, "lt"); break;
	case T_GREATER: strcpy(o, "gt"); break;
	case T_LESSEQ: strcpy(o, "le"); break;
	default: strcpy(o, "ge"); break;
	}
	if (!fl)
		strcat(o, sg ? "_s" : "_u");
	wop(e, k, o);
}

static int wbinary(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	int op = x->aux;
	qty lt = N(x->a)->ty, rt = N(x->b)->ty, ty = x->ty;

	if (unlikely(vstack_low()))
		efatal(e, x->loc, "expression nested too deeply");
	if (is_vec(c, ty) || is_vec(c, lt))
		efatal(e, x->loc, "wasm: vector arithmetic is not supported yet (PLAN-wasm.md W4)");
	if (is_ptr(c, ty) && (op == T_PLUS || op == T_MINUS)) {
		bool lp = is_ptr(c, lt);
		uint64_t es = welem_size(e, lp ? lt : rt);

		wexpr(e, x->a, true);
		if (!lp) {
			wto_i64(e, lt);
			if (es != 1) {
				wconst(e, WT_I64, (int64_t)es);
				wi(e, "i64.mul");
			}
		}
		wexpr(e, x->b, true);
		if (lp) {
			wto_i64(e, rt);
			if (es != 1) {
				wconst(e, WT_I64, (int64_t)es);
				wi(e, "i64.mul");
			}
		}
		wi(e, op == T_PLUS ? "i64.add" : "i64.sub");
		return WT_I64;
	}
	if (op == T_MINUS && is_ptr(c, lt) && is_ptr(c, rt)) {
		uint64_t es = welem_size(e, lt);

		wexpr(e, x->a, true);
		wexpr(e, x->b, true);
		wi(e, "i64.sub");
		if (es > 1) {
			wconst(e, WT_I64, (int64_t)es);
			wi(e, "i64.div_s");
		}
		wconv(e, prim(TY_LONG), ty);
		return wty(c, ty);
	}
	if (wis_cmp(op)) {
		qty t = lt;

		if (is_ptr(c, lt) || is_ptr(c, rt))
			t = prim(TY_ULONG);
		wexpr(e, x->a, true);
		if (!is_ptr(c, lt) && is_ptr(c, rt))
			wconv(e, lt, t);
		wexpr(e, x->b, true);
		if (QUNQ(rt) != QUNQ(lt) && !(is_ptr(c, lt) && is_ptr(c, rt)))
			wconv(e, rt, is_ptr(c, lt) ? t : lt);
		wcompare(e, op, t);
		return WT_I32;
	}
	wexpr(e, x->a, true);
	if (QUNQ(lt) != QUNQ(ty) && op != T_LESSLESS && op != T_GREATERGREATER)
		wconv(e, lt, ty);
	wexpr(e, x->b, true);
	if (QUNQ(rt) != QUNQ(ty) && op != T_LESSLESS && op != T_GREATERGREATER)
		wconv(e, rt, ty);
	warith(e, op, ty, (op == T_LESSLESS || op == T_GREATERGREATER) ? rt : 0);
	return wty(c, ty);
}

/* an expression whose value is already 0 or 1 */
static bool wis_bool(cc *c, uint32_t n)
{
	const node *x = N(n);

	return (x->op == N_BINARY && wis_cmp(x->aux)) || x->op == N_LOGAND || x->op == N_LOGOR ||
	       (x->op == N_UNARY && x->aux == T_EXCLAIM) || KIND(c, x->ty) == TY_BOOL;
}

/* push an i32 that is nonzero iff n is true */
VPRIV void wtruth(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	int k;

	if (x->op == N_CAST && x->aux == CK_BOOL) {
		wtruth(e, x->a);
		return;
	}
	k = wexpr(e, n, true);
	switch (k) {
	case WT_I32: break;
	case WT_I64: wi(e, "i64.eqz"); wi(e, "i32.eqz"); break;
	case WT_F32: wfconst(e, WT_F32, 0); wi(e, "f32.ne"); break;
	case WT_F64: wfconst(e, WT_F64, 0); wi(e, "f64.ne"); break;
	default: efatal(e, x->loc, "wasm: a 128-bit condition is not supported yet (PLAN-wasm.md W4)");
	}
}

/* push 0 or 1 */
static void wbool(ecx *e, uint32_t n)
{
	wtruth(e, n);
	if (!wis_bool(e->c, n)) {
		wconst(e, WT_I32, 0);
		wi(e, "i32.ne");
	}
}

static const char *wif(int k)
{
	static const char *const s[] = { "if", "if\ti32", "if\ti64", "if\tf32", "if\tf64", "if\ti64" };

	return s[k];
}

static int wcond_expr(ecx *e, uint32_t n, bool want)
{
	cc *c = e->c;
	const node *x = N(n);
	int k = want ? wrep(wty(c, x->ty)) : WT_NONE;

	if (!x->b) {
		/* GNU a ?: c */
		qty at = N(x->a)->ty;
		int ak = wty(c, at);
		uint32_t t = wtmp(e, ak);

		wexpr(e, x->a, true);
		wtee(e, t);
		switch (wrep(ak)) {
		case WT_I32: break;
		case WT_I64: wi(e, "i64.eqz"); wi(e, "i32.eqz"); break;
		case WT_F32: wfconst(e, WT_F32, 0); wi(e, "f32.ne"); break;
		case WT_F64: wfconst(e, WT_F64, 0); wi(e, "f64.ne"); break;
		}
		wblock(e, wif(k));
		if (want) {
			wget(e, t);
			wconv(e, at, x->ty);
		}
		wi(e, "else");
		wexpr(e, x->c, want);
		wend(e, "end_if");
		wfree(e, t);
		return k;
	}
	wtruth(e, x->a);
	wblock(e, wif(k));
	wexpr(e, x->b, want);
	wi(e, "else");
	wexpr(e, x->c, want);
	wend(e, "end_if");
	return k;
}

static int wassign(ecx *e, uint32_t n, bool want)
{
	cc *c = e->c;
	const node *x = N(n);
	qty lt = x->ty;
	int k = wty(c, lt);
	wlv l;

	if (is_vec(c, lt))
		efatal(e, x->loc, "wasm: vector assignment is not supported yet (PLAN-wasm.md W4)");
	if (T(c, lt)->flags & TF_ATOMIC)
		efatal(e, x->loc, "wasm: _Atomic objects are not supported yet (PLAN-wasm.md W4)");
	if (!x->aux) {
		wlval(e, x->a, &l, lt);
		if (k == WT_MEM) {
			uint32_t ta = want ? wtmp(e, WT_I64) : UINT32_MAX;

			if (want)
				wtee(e, ta);
			wexpr(e, x->b, true);
			wcopy(e, tsize(c, lt));
			if (want) {
				wget(e, ta);
				wfree(e, ta);
			}
			return want ? WT_I64 : WT_NONE;
		}
		wexpr(e, x->b, true);
		if (!want) {
			wstore(e, &l, lt);
			return WT_NONE;
		}
		if (l.k == WL_LOC) {
			wtee(e, l.loc);
			return k;
		}
		{
			uint32_t tv = wtmp(e, k);

			wtee(e, tv);
			wstore(e, &l, lt);
			wget(e, tv);
			if (l.bf)
				wbf_trunc(e, &l, lt);
			wfree(e, tv);
		}
		return k;
	}
	/* compound: old op= b in the computation type x->c */
	{
		qty pt = x->c, bt = N(x->b)->ty;
		int op = x->aux;
		uint32_t ta = UINT32_MAX;

		wlval(e, x->a, &l, lt);
		if (l.k == WL_MEM) {
			ta = wtmp(e, WT_I64);
			wtee(e, ta);
			wget(e, ta);
		}
		wload(e, &l, lt);
		if (is_ptr(c, lt)) {
			uint64_t es = welem_size(e, lt);

			wexpr(e, x->b, true);
			wto_i64(e, bt);
			if (es != 1) {
				wconst(e, WT_I64, (int64_t)es);
				wi(e, "i64.mul");
			}
			wi(e, op == T_PLUS ? "i64.add" : "i64.sub");
		} else {
			if (!pt)
				pt = lt;
			wconv(e, lt, pt);
			wexpr(e, x->b, true);
			if (op != T_LESSLESS && op != T_GREATERGREATER && QUNQ(bt) != QUNQ(pt))
				wconv(e, bt, pt);
			warith(e, op, pt, (op == T_LESSLESS || op == T_GREATERGREATER) ? bt : 0);
			wconv(e, pt, lt);
		}
		if (!want) {
			wstore(e, &l, lt);
		} else if (l.k == WL_LOC) {
			wtee(e, l.loc);
		} else {
			uint32_t tv = wtmp(e, k);

			wtee(e, tv);
			wstore(e, &l, lt);
			wget(e, tv);
			if (l.bf)
				wbf_trunc(e, &l, lt);
			wfree(e, tv);
		}
		if (ta != UINT32_MAX)
			wfree(e, ta);
		return want ? k : WT_NONE;
	}
}

static int wincdec(ecx *e, uint32_t n, bool want)
{
	cc *c = e->c;
	const node *x = N(n);
	qty t = N(x->a)->ty;
	int k = wty(c, t);
	bool inc = x->op == N_PREINC || x->op == N_POSTINC, post = x->op == N_POSTINC || x->op == N_POSTDEC;
	uint32_t ta = UINT32_MAX, told = UINT32_MAX, tnew = UINT32_MAX;
	wlv l;

	if (k == WT_MEM || is_vec(c, t))
		efatal(e, x->loc, "wasm: ++/-- on a 128-bit value is not supported yet (PLAN-wasm.md W4)");
	if (T(c, t)->flags & TF_ATOMIC)
		efatal(e, x->loc, "wasm: _Atomic objects are not supported yet (PLAN-wasm.md W4)");
	wlval(e, x->a, &l, t);
	if (l.k == WL_MEM) {
		ta = wtmp(e, WT_I64);
		wtee(e, ta);
		wget(e, ta);
	}
	wload(e, &l, t);
	if (want && post) {
		told = wtmp(e, k);
		wtee(e, told);
	}
	if (KIND(c, t) == TY_BOOL) {
		if (inc) {
			wi(e, "drop");
			wconst(e, WT_I32, 1);
		} else {
			wconst(e, WT_I32, 1);
			wi(e, "i32.xor");
		}
	} else if (k == WT_F32 || k == WT_F64) {
		wfconst(e, k, 1.0);
		wop(e, k, inc ? "add" : "sub");
	} else {
		wconst(e, k, is_ptr(c, t) ? (int64_t)welem_size(e, t) : 1);
		wop(e, k, inc ? "add" : "sub");
		if (k == WT_I32)
			wnarrow(e, prim(TY_INT), t);
	}
	if (want && !post) {
		tnew = wtmp(e, k);
		wtee(e, tnew);
	}
	wstore(e, &l, t);
	if (want) {
		wget(e, post ? told : tnew);
		if (!post && l.bf)
			wbf_trunc(e, &l, t);
		wfree(e, post ? told : tnew);
	}
	if (ta != UINT32_MAX)
		wfree(e, ta);
	return want ? k : WT_NONE;
}

/* a statement expression's value: the last statement's, if it is an expression */
static int wstmtexpr(ecx *e, uint32_t n, bool want)
{
	cc *c = e->c;
	const node *x = N(n);
	uint32_t last = x->c ? c->lists[x->b + x->c - 1] : 0, vs = 0;

	while (last && N(last)->op == N_LABEL)
		last = N(last)->c;
	if (want && last && N(last)->op == N_EXPR && N(last)->a && !is_void(c, N(N(last)->a)->ty))
		vs = last;
	if (!vs) {
		wstmt(e, n);
		if (want && !is_void(c, N(n)->ty) && wty(c, N(n)->ty) != WT_NONE)
			efatal(e, x->loc, "wasm: statement expression without a value");
		return WT_NONE;
	}
	{
		qty vt = N(N(vs)->a)->ty;
		int k = wty(c, vt);
		uint32_t t = wtmp(e, k);

		for (uint32_t i = 0; i + 1 < x->c; i++)
			wstmt(e, c->lists[x->b + i]);
		/* labels in front of the value (a goto target: W4) */
		for (uint32_t s = c->lists[x->b + x->c - 1]; N(s)->op == N_LABEL; s = N(s)->c)
			;
		wexpr(e, N(vs)->a, true);
		if (k == WT_MEM) {
			/* a record value outlives the block's objects: copy it */
			uint64_t sl = wslot(e, tsize(c, vt), talign(c, vt));

			wset(e, t);
			wslot_addr(e, sl);
			wget(e, t);
			wcopy(e, tsize(c, vt));
			wslot_addr(e, sl);
		}
		wfree(e, t);
		return k;
	}
}

static int wexpr1(ecx *e, uint32_t n, bool want);

/* push the value of n (want), or only do what it does; returns the pushed type */
VPRIV int wexpr(ecx *e, uint32_t n, bool want)
{
	int k;

	if (!want && !side_effects(e->c, n))
		return WT_NONE;
	k = wexpr1(e, n, want);
	if (!want && k != WT_NONE) {
		wi(e, "drop");
		return WT_NONE;
	}
	return k;
}

static int wexpr1(ecx *e, uint32_t n, bool want)
{
	cc *c = e->c;
	const node *x = N(n);
	int k = wty(c, x->ty);

	switch (x->op) {
	case N_NUM: {
		i128 v = c->ipool[x->a];

		if (k == WT_MEM) {
			uint64_t sl = wslot(e, 16, 16);

			wslot_addr(e, sl);
			wconst(e, WT_I64, (int64_t)(uint64_t)v);
			wiu(e, "i64.store", 0);
			wslot_addr(e, sl);
			wconst(e, WT_I64, (int64_t)(uint64_t)((u128)v >> 64));
			wiu(e, "i64.store", 8);
			wslot_addr(e, sl);
			return WT_I64;
		}
		if (k == WT_F32 || k == WT_F64) {
			wfconst(e, k, (double)v);
			return k;
		}
		wconst(e, k, (int64_t)em_trunc_to(c, v, x->ty));
		return k;
	}
	case N_FNUM:
		if (k == WT_MEM) {
			uint64_t sl = wslot(e, 16, 16), q[2];

			memcpy(q, &c->fpool[x->a + 1], 16);
			wslot_addr(e, sl);
			wconst(e, WT_I64, (int64_t)q[0]);
			wiu(e, "i64.store", 0);
			wslot_addr(e, sl);
			wconst(e, WT_I64, (int64_t)q[1]);
			wiu(e, "i64.store", 8);
			wslot_addr(e, sl);
			return WT_I64;
		}
		if (k != WT_F32 && k != WT_F64)
			efatal(e, x->loc, "wasm: _Float16 is not supported");
		wfconst(e, k, c->fpool[x->a]);
		return k;
	case N_STR:
		waddr_ref(e, REF(RK_STR, x->a), 0);
		return WT_I64;
	case N_VAR: {
		const sym *y = &c->syms[x->a];

		if (y->kind == S_FUNC || y->kind == S_BUILTIN) {
			uint32_t r = y->kind == S_FUNC ? REF(RK_SYM, canon_sym(c, x->a)) : REF(RK_NAME, (uint32_t)y->val);
			tbuf *b = &e->w->b;

			claim_ref(e, r);
			wdecl(e, r, y->ty, NULL);
			tb_s(b, "\ti64.const\t");
			wfname(c, b, r);
			tb_c(b, '\n');
			return WT_I64;
		}
		if (y->kind == S_ENUMCONST) {
			wconst(e, k, y->val);
			return k;
		}
		break;
	}
	case N_ADDR: {
		const node *a = N(x->a);
		wlv l;

		if (a->op == N_LABELADDR || is_func(c, a->ty))
			return wexpr(e, x->a, want);
		wlval(e, x->a, &l, prim(TY_VOID));
		if (l.k != WL_MEM)
			efatal(e, x->loc, "wasm: address of a register local");
		wfold_off(e, &l);
		return WT_I64;
	}
	case N_CAST:
		switch (x->aux) {
		case CK_DECAY: {
			wlv l;

			wlval(e, x->a, &l, prim(TY_VOID));
			if (l.k != WL_MEM)
				efatal(e, x->loc, "wasm: array in a register");
			wfold_off(e, &l);
			return WT_I64;
		}
		case CK_FDECAY:
			return wexpr(e, x->a, want);
		case CK_VOID:
			wexpr(e, x->a, false);
			return WT_NONE;
		case CK_UNION: {
			qty at = N(x->a)->ty;
			uint64_t sl = wslot(e, tsize(c, x->ty), talign(c, x->ty));
			wlv l = { .k = WL_MEM };

			wslot_addr(e, sl);
			wconst(e, WT_I32, 0);
			wconst(e, WT_I64, (int64_t)tsize(c, x->ty));
			wi(e, "memory.fill\t0");
			wslot_addr(e, sl);
			wexpr(e, x->a, true);
			wstore(e, &l, at);
			wslot_addr(e, sl);
			return WT_I64;
		}
		case CK_VBITS:
		case CK_VADDR:
			efatal(e, x->loc, "wasm: vectors are not supported yet (PLAN-wasm.md W4)");
		default: {
			int64_t v;

			if (k != WT_MEM && is_integer(c, x->ty) && wnum(e, x->a, &v)) {
				wconst(e, k, (int64_t)em_trunc_to(c, v, x->ty));
				return k;
			}
			wexpr(e, x->a, true);
			wconv(e, N(x->a)->ty, x->ty);
			return k;
		}
		}
	case N_UNARY:
		switch (x->aux) {
		case T_PLUS:
			return wexpr(e, x->a, want);
		case T_MINUS:
			if (k == WT_F32 || k == WT_F64) {
				wexpr(e, x->a, true);
				wop(e, k, "neg");
				return k;
			}
			if (k == WT_MEM)
				break;
			wconst(e, k, 0);
			wexpr(e, x->a, true);
			wop(e, k, "sub");
			if (k == WT_I32)
				wnarrow(e, prim(TY_INT), x->ty);
			return k;
		case T_TILDE:
			if (k == WT_MEM)
				break;
			wexpr(e, x->a, true);
			wconst(e, k, -1);
			wop(e, k, "xor");
			return k;
		case T_EXCLAIM:
			wtruth(e, x->a);
			wi(e, "i32.eqz");
			return WT_I32;
		}
		efatal(e, x->loc, "wasm: unsupported unary operator");
	case N_PREINC: case N_PREDEC: case N_POSTINC: case N_POSTDEC:
		return wincdec(e, n, want);
	case N_BINARY:
		return wbinary(e, n);
	case N_LOGAND:
	case N_LOGOR:
		wtruth(e, x->a);
		if (!want) {
			if (x->op == N_LOGOR)
				wi(e, "i32.eqz");
			wblock(e, "if");
			wexpr(e, x->b, false);
			wend(e, "end_if");
			return WT_NONE;
		}
		wblock(e, "if\ti32");
		if (x->op == N_LOGAND)
			wbool(e, x->b);
		else
			wconst(e, WT_I32, 1);
		wi(e, "else");
		if (x->op == N_LOGAND)
			wconst(e, WT_I32, 0);
		else
			wbool(e, x->b);
		wend(e, "end_if");
		return WT_I32;
	case N_ASSIGN:
		return wassign(e, n, want);
	case N_COND:
		return wcond_expr(e, n, want);
	case N_COMMA:
		wexpr(e, x->a, false);
		return wexpr(e, x->b, want);
	case N_CALL:
		return wcall(e, n, want);
	case N_BUILTIN:
		return wbuiltin(e, n, want);
	case N_STMTEXPR:
		return wstmtexpr(e, x->a, want);
	case N_ZERO:
		if (k == WT_MEM) {
			uint64_t sl = wslot(e, tsize(c, x->ty), talign(c, x->ty));

			wslot_addr(e, sl);
			wconst(e, WT_I32, 0);
			wconst(e, WT_I64, (int64_t)tsize(c, x->ty));
			wi(e, "memory.fill\t0");
			wslot_addr(e, sl);
			return WT_I64;
		}
		if (k == WT_F32 || k == WT_F64)
			wfconst(e, k, 0);
		else if (k != WT_NONE)
			wconst(e, k, 0);
		return k;
	case N_CPLX: {
		qty et = c->tags[T(c, x->ty)->aux].f[0].ty;
		uint64_t es = tsize(c, et), sl = wslot(e, 2 * es, talign(c, x->ty));
		wlv l = { .k = WL_MEM };

		wslot_addr(e, sl);
		wexpr(e, x->a, true);
		wstore(e, &l, et);
		l.off = es;
		wslot_addr(e, sl);
		wexpr(e, x->b, true);
		wstore(e, &l, et);
		wslot_addr(e, sl);
		return WT_I64;
	}
	case N_VAARG:
		return wbuiltin(e, n, want);
	case N_LABELADDR:
		efatal(e, x->loc, "wasm: label addresses are not supported (PLAN-wasm.md W4)");
	case N_SIZEOF_VLA:
		efatal(e, x->loc, "wasm: variable length arrays are not supported yet (PLAN-wasm.md W4)");
	default:
		break;
	}
	switch (x->op) {
	case N_VAR: case N_MEMBER: case N_INDEX: case N_DEREF: case N_COMPLIT: {
		wlv l;

		wlval(e, n, &l, x->ty);
		wload(e, &l, x->ty);
		return wrep(k);
	}
	}
	efatal(e, x->loc, "wasm: unsupported expression (node %d)", x->op);
}
