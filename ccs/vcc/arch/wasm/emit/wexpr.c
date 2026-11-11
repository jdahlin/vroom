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
		if ((is_vec(c, to) || is_vec(c, from)) && tsize(c, from) == tsize(c, to)) {
			/* the same bits */
			if (fk == WT_MEM && tk == WT_MEM)
				return;
			if (fk == WT_MEM) {
				wlv l = { .k = WL_MEM };

				wload(e, &l, to);
			} else {
				uint64_t sl = wslot(e, tsize(c, to), 16);
				uint32_t v = wtmp(e, fk);
				wlv l = { .k = WL_MEM };

				wset(e, v);
				wslot_addr(e, sl);
				wget(e, v);
				wstore(e, &l, from);
				wslot_addr(e, sl);
				wfree(e, v);
			}
			return;
		}
		if ((w16(c, from) || fk != WT_MEM) && (w16(c, to) || tk != WT_MEM)) {
			w16conv(e, from, to);
			return;
		}
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
		wnarrow(e, prim(TY_LLONG), to);
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
		wpconst(e, (int64_t)l->off);
		wpop(e, "add");
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

/* ---------------- variable length arrays ---------------- */

static bool wvla(cc *c, qty t)
{
	const ctype *a = T(c, t);

	return a->kind == TY_ARRAY && (a->flags & TF_VLA);
}

/* push the value of the VLA size expression sn (a size_t): fixed when its declaration ran */
static void wvla_len(ecx *e, uint32_t sn)
{
	cc *c = e->c;
	wst *W = e->w;

	for (uint32_t k = W->nvsz; k-- > 0;)
		if (W->vszn[k] == sn) {
			wget(e, W->vszl[k]);
			return;
		}
	wexpr(e, sn, true);
	wconv(e, N(sn)->ty, prim(TY_ULONG));
}

/* push the size of type t in bytes (a size_t) */
VPRIV void wvla_bytes(ecx *e, qty t)
{
	cc *c = e->c;
	const ctype *a = T(c, t);

	if (!wvla(c, t)) {
		wpconst(e, (int64_t)tsize(c, t));
		return;
	}
	if (!a->n)
		efatal(e, e->curloc, "wasm: a VLA of unspecified size");
	wvla_len(e, a->n);
	if (wvla(c, a->base) || tsize(c, a->base) != 1) {
		wvla_bytes(e, a->base);
		wpop(e, "mul");
	}
}

/*
 * A declaration whose type has VLAs in it (int a[n], int (*p)[n], typedef
 * int T[n]): each size is evaluated now, once, into a local, so a later
 * n = ... does not change the type (C11 6.7.6.2p5).  pure: only sizes
 * without side effects (parameters).
 */
VPRIV void wvla_fix(ecx *e, qty t, bool pure)
{
	cc *c = e->c;
	wst *W = e->w;

	for (;;) {
		const ctype *a = T(c, t);

		if (a->kind == TY_PTR) {
			t = a->base;
			continue;
		}
		if (a->kind != TY_ARRAY)
			return;
		if ((a->flags & TF_VLA) && a->n && N(a->n)->op != N_NUM && !(pure && side_effects(c, a->n))) {
			uint32_t k;

			for (k = 0; k < W->nvsz && W->vszn[k] != a->n; k++)
				;
			if (k < W->nvsz) {
				t = a->base;
				continue;
			}
			if (k == W->nvsz) {
				if (W->nvsz >= WMAXGD)
					efatal(e, e->curloc, "too many variable length array sizes in one function");
				W->vszn[k] = a->n;
				W->vszl[k] = wnewloc(e, WPT(c));
				/* Bound emission may recursively capture another VLA dimension. */
				W->nvsz++;
			}
			wexpr(e, a->n, true);
			wconv(e, N(a->n)->ty, prim(TY_ULONG));
			wset(e, W->vszl[k]);
		}
		t = a->base;
	}
}

/* the element size of pointer type ptr, 0 if it is only known at run time */
static uint64_t welem_size(ecx *e, qty ptr)
{
	cc *c = e->c;
	qty b = T(c, ptr)->base;

	if (wvla(c, b))
		return 0;
	if (is_void(c, b) || is_func(c, b))
		return 1;	/* GNU: void * and function pointer arithmetic */
	return tsize(c, b);
}

/* stack: [n, address width] -> n times the element size of ptr (es: welem_size) */
static void wscale(ecx *e, qty ptr, uint64_t es)
{
	if (es == 1)
		return;
	if (es)
		wpconst(e, (int64_t)es);
	else
		wvla_bytes(e, T(e->c, ptr)->base);
	wpop(e, "mul");
}

/* an integer of type t on the stack to the address width (an index, an offset) */
VPRIV void wto_ptr(ecx *e, qty t)
{
	cc *c = e->c;
	int k = wty(c, t), p = WPT(c);

	if (k == p)
		return;
	if (k == WT_I32 && p == WT_I64)
		wi(e, is_signed(c, t) && KIND(c, t) != TY_BOOL ? "i64.extend_i32_s" : "i64.extend_i32_u");
	else if (k == WT_I64 && p == WT_I32)
		wi(e, "i32.wrap_i64");
	else
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
		if (x->aux == 1) {
			l->bf = 1, l->width = 1;
			if (wnum(e, x->b, &k) && k >= 0) {
				l->off = (uint64_t)k / 8, l->bitoff = k % 8;
				break;
			}
			wexpr(e, x->b, true);
			wconv(e, N(x->b)->ty, prim(TY_UINT));
			uint32_t ix = wtmp(e, WT_I32);
			wset(e, ix);
			l->bf = 2, l->dynbit_slot = wslot(e, 4, 4);
			wslot_addr(e, l->dynbit_slot);
			wget(e, ix);
			wconst(e, WT_I32, 7);
			wi(e, "i32.and");
			wiu(e, "i32.store", 0);
			wget(e, ix);
			wconst(e, WT_I32, 3);
			wi(e, "i32.shr_u");
			wto_ptr(e, prim(TY_UINT));
			wpop(e, "add");
			wfree(e, ix);
			break;
		}
		if (es && wnum(e, x->b, &k) && (k * (int64_t)es) >= 0) {
			l->off = (uint64_t)(k * (int64_t)es);
			break;
		}
		wexpr(e, x->b, true);
		wto_ptr(e, N(x->b)->ty);
		wscale(e, N(x->a)->ty, es);
		wpop(e, "add");
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

/* the container's bytes; over 8 (a packed field across 9 to 16 bytes): 8 plus
   the high part's container, 1, 2, 4 or 8 */
static int wbf_bytes(ecx *e, const wlv *l)
{
	int nb = (l->bitoff + l->width + 7) / 8;

	if (nb > 16)
		efatal(e, e->curloc, "wasm: a bit-field over 16 bytes");
	if (nb > 8)
		return 8 + wbf_bytes(e, &(wlv){ .bitoff = 0, .width = l->bitoff + l->width - 64 });
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
	if (l->bf == 2) {
		wiu(e, "i32.load8_u", l->off);
		wslot_addr(e, l->dynbit_slot);
		wiu(e, "i32.load", 0);
		wi(e, "i32.shr_u");
		wconst(e, WT_I32, 1);
		wi(e, "i32.and");
		return;
	}
	if (l->bf) {
		int nb = wbf_bytes(e, l);

		if (nb > 8) {
			/* low 8 bytes >> bitoff | the high part << (64 - bitoff) */
			uint32_t ta = wtmp(e, WPT(c));

			wtee(e, ta);
			wiu(e, "i64.load", l->off);
			wconst(e, WT_I64, l->bitoff);
			wi(e, "i64.shr_u");
			wget(e, ta);
			wbf_container(e, nb - 8, false, l->off + 8);
			wconst(e, WT_I64, 64 - l->bitoff);
			wi(e, "i64.shl");
			wi(e, "i64.or");
			wfree(e, ta);
			if (l->sgn && l->width < 64) {
				wconst(e, WT_I64, 64 - l->width);
				wi(e, "i64.shl");
				wconst(e, WT_I64, 64 - l->width);
				wi(e, "i64.shr_s");
			} else if (l->width < 64) {
				wconst(e, WT_I64, (int64_t)((1ull << l->width) - 1));
				wi(e, "i64.and");
			}
			if (wty(c, t) == WT_I32)
				wi(e, "i32.wrap_i64");
			return;
		}
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
	if (l->bf == 2) {
		uint32_t tv = wtmp(e, WT_I32), ta = wtmp(e, WPT(c)), tm = wtmp(e, WT_I32);
		wset(e, tv);
		wset(e, ta);
		wconst(e, WT_I32, 1);
		wslot_addr(e, l->dynbit_slot);
		wiu(e, "i32.load", 0);
		wi(e, "i32.shl");
		wset(e, tm);
		wget(e, ta);
		wget(e, ta);
		wiu(e, "i32.load8_u", l->off);
		wget(e, tm);
		wconst(e, WT_I32, -1);
		wi(e, "i32.xor");
		wi(e, "i32.and");
		wconst(e, WT_I32, 0);
		wget(e, tv);
		wi(e, "i32.sub");
		wget(e, tm);
		wi(e, "i32.and");
		wi(e, "i32.or");
		wiu(e, "i32.store8", l->off);
		wfree(e, tv), wfree(e, ta), wfree(e, tm);
		return;
	}
	if (l->bf) {
		int nb = wbf_bytes(e, l);
		uint64_t mask = l->width >= 64 ? ~0ull : (1ull << l->width) - 1;
		uint32_t tv = wtmp(e, wty(c, t)), ta = wtmp(e, WPT(c));

		wset(e, tv);
		if (nb > 8) {
			/* the low 8 bytes, then the high part */
			int hb = (int)(l->bitoff + l->width - 64);
			uint64_t hm = (1ull << hb) - 1;

			wset(e, ta);
			if (wty(c, t) == WT_I32) {
				wget(e, tv);
				wi(e, "i64.extend_i32_u");
				wfree(e, tv);
				tv = wtmp(e, WT_I64);
				wset(e, tv);
			}
			wget(e, ta);
			wget(e, ta);
			wiu(e, "i64.load", l->off);
			wconst(e, WT_I64, (int64_t)~(mask << l->bitoff));
			wi(e, "i64.and");
			wget(e, tv);
			wconst(e, WT_I64, (int64_t)mask);
			wi(e, "i64.and");
			wconst(e, WT_I64, l->bitoff);
			wi(e, "i64.shl");
			wi(e, "i64.or");
			wiu(e, "i64.store", l->off);
			wget(e, ta);
			wget(e, ta);
			wbf_container(e, nb - 8, false, l->off + 8);
			wconst(e, WT_I64, (int64_t)~hm);
			wi(e, "i64.and");
			wget(e, tv);
			wconst(e, WT_I64, (int64_t)mask);
			wi(e, "i64.and");
			wconst(e, WT_I64, 64 - l->bitoff);
			wi(e, "i64.shr_u");
			wi(e, "i64.or");
			wbf_container(e, nb - 8, true, l->off + 8);
			wfree(e, tv);
			wfree(e, ta);
			return;
		}
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
	wpconst(e, (int64_t)n);
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

/* ---------------- 16-byte scalars: __int128 and long double ---------------- */

/*
 * A 16-byte value is the address of 16 bytes (a frame slot for a result),
 * as clang's wasm ABI passes it as two i64.  __int128 adds, subtracts, does
 * bitwise operations and compares inline; it multiplies, divides and
 * shifts in compiler-rt (__multi3 ..), as long double (binary128) does all
 * its arithmetic, comparisons and conversions (__addtf3, __lttf2 ..).
 */

/* 1: __int128, 2: long double, 0: neither */
VPRIV int w16(cc *c, qty t)
{
	switch (KIND(c, t)) {
	case TY_INT128:
	case TY_UINT128:
		return 1;
	case TY_LDOUBLE:
		return 2;
	default:
		return 0;
	}
}

/* call a compiler-rt routine; P in sig: an address (i32 or i64) */
static void wrt(ecx *e, const char *name, const char *sig)
{
	wst *W = e->w;

	tb_s(&W->decl, name);
	tb_c(&W->decl, '\t');
	for (; *sig; sig++)
		if (*sig == 'P')
			tb_s(&W->decl, wt_name[WPT(e->c)]);
		else
			tb_c(&W->decl, *sig);
	tb_c(&W->decl, '\n');
	tb_s(&W->b, "\tcall\t");
	tb_s(&W->b, name);
	tb_c(&W->b, '\n');
}

/* push half h (0 low, 1 high) of the 16 bytes at local a */
static void whalf(ecx *e, uint32_t a, int h)
{
	wget(e, a);
	wiu(e, "i64.load", h ? 8 : 0);
}

/* stack: [address] -> [lo hi] */
VPRIV void wsplit(ecx *e)
{
	uint32_t a = wtmp(e, WPT(e->c));

	wset(e, a);
	whalf(e, a, 0);
	whalf(e, a, 1);
	wfree(e, a);
}

/* stack: [lo hi] -> [address of a new slot holding them] */
static void wpair(ecx *e)
{
	uint64_t sl = wslot(e, 16, 16);
	uint32_t h = wtmp(e, WT_I64), l = wtmp(e, WT_I64);

	wset(e, h);
	wset(e, l);
	wslot_addr(e, sl);
	wget(e, l);
	wiu(e, "i64.store", 0);
	wslot_addr(e, sl);
	wget(e, h);
	wiu(e, "i64.store", 8);
	wslot_addr(e, sl);
	wfree(e, l);
	wfree(e, h);
}

/* push a 16-byte constant */
static void w16const(ecx *e, uint64_t lo, uint64_t hi)
{
	wconst(e, WT_I64, (int64_t)lo);
	wconst(e, WT_I64, (int64_t)hi);
	wpair(e);
}

/* stack: [a b] (a 16-byte, b 16-byte or a shift count of type rty) -> a op b */
VPRIV void w16arith(ecx *e, int op, qty ty, qty rty)
{
	cc *c = e->c;
	bool sg = is_signed(c, ty), fl = w16(c, ty) == 2;
	uint32_t ta, tb;
	const char *f = NULL;

	if (op == T_LESSLESS || op == T_GREATERGREATER) {
		uint32_t n = wtmp(e, WT_I32);
		int rk = wty(c, rty);

		if (rk == WT_MEM)
			wiu(e, "i64.load", 0);
		if (rk != WT_I32)
			wi(e, "i32.wrap_i64");
		wset(e, n);
		ta = wtmp(e, WPT(c));
		wset(e, ta);
		{
			uint64_t sl = wslot(e, 16, 16);

			wslot_addr(e, sl);
			whalf(e, ta, 0);
			whalf(e, ta, 1);
			wget(e, n);
			wrt(e, op == T_LESSLESS ? "__ashlti3" : sg ? "__ashrti3" : "__lshrti3", "(P, i64, i64, i32) -> ()");
			wslot_addr(e, sl);
		}
		wfree(e, ta);
		wfree(e, n);
		return;
	}
	tb = wtmp(e, WPT(c));
	wset(e, tb);
	ta = wtmp(e, WPT(c));
	wset(e, ta);
	switch (op) {
	case T_PLUS: f = fl ? "__addtf3" : NULL; break;
	case T_MINUS: f = fl ? "__subtf3" : NULL; break;
	case T_STAR: f = fl ? "__multf3" : "__multi3"; break;
	case T_SLASH: f = fl ? "__divtf3" : sg ? "__divti3" : "__udivti3"; break;
	case T_PERCENT: f = fl ? NULL : sg ? "__modti3" : "__umodti3"; break;
	case T_AMP: case T_PIPE: case T_CARET: break;
	default: efatal(e, e->curloc, "wasm: unsupported operator on a 16-byte value");
	}
	if (f) {
		uint64_t sl = wslot(e, 16, 16);

		wslot_addr(e, sl);
		whalf(e, ta, 0);
		whalf(e, ta, 1);
		whalf(e, tb, 0);
		whalf(e, tb, 1);
		wrt(e, f, "(P, i64, i64, i64, i64) -> ()");
		wslot_addr(e, sl);
	} else if (fl) {
		efatal(e, e->curloc, "wasm: unsupported operator on long double");
	} else if (op == T_PLUS || op == T_MINUS) {
		uint32_t lo = wtmp(e, WT_I64);
		const char *o = op == T_PLUS ? "i64.add" : "i64.sub";

		whalf(e, ta, 0);
		whalf(e, tb, 0);
		wi(e, o);
		wtee(e, lo);
		whalf(e, ta, 1);
		whalf(e, tb, 1);
		wi(e, o);
		/* the carry (lo < a.lo) or the borrow (a.lo < b.lo) */
		if (op == T_PLUS) {
			wget(e, lo);
			whalf(e, ta, 0);
		} else {
			whalf(e, ta, 0);
			whalf(e, tb, 0);
		}
		wi(e, "i64.lt_u");
		wi(e, "i64.extend_i32_u");
		wi(e, o);
		wpair(e);
		wfree(e, lo);
	} else {
		const char *o = op == T_AMP ? "i64.and" : op == T_PIPE ? "i64.or" : "i64.xor";

		whalf(e, ta, 0);
		whalf(e, tb, 0);
		wi(e, o);
		whalf(e, ta, 1);
		whalf(e, tb, 1);
		wi(e, o);
		wpair(e);
	}
	wfree(e, ta);
	wfree(e, tb);
}

/* stack: [a b] of 16-byte type t -> a op b as an i32 0/1 */
VPRIV void w16cmp(ecx *e, int op, qty t)
{
	cc *c = e->c;
	uint32_t ta, tb;

	tb = wtmp(e, WPT(c));
	wset(e, tb);
	ta = wtmp(e, WPT(c));
	wset(e, ta);
	if (w16(c, t) == 2) {
		static const char *const fn[] = { "__eqtf2", "__netf2", "__lttf2", "__gttf2", "__letf2", "__getf2" };
		static const char *const rel[] = { "i32.eq", "i32.ne", "i32.lt_s", "i32.gt_s", "i32.le_s", "i32.ge_s" };
		int i = op == T_EQUALEQUAL ? 0 : op == T_EXCLAIMEQ ? 1 : op == T_LESS ? 2 : op == T_GREATER ? 3 : op == T_LESSEQ ? 4 : 5;

		whalf(e, ta, 0);
		whalf(e, ta, 1);
		whalf(e, tb, 0);
		whalf(e, tb, 1);
		wrt(e, fn[i], "(i64, i64, i64, i64) -> (i32)");
		wconst(e, WT_I32, 0);
		wi(e, rel[i]);
	} else if (op == T_EQUALEQUAL || op == T_EXCLAIMEQ) {
		whalf(e, ta, 0);
		whalf(e, tb, 0);
		wi(e, "i64.xor");
		whalf(e, ta, 1);
		whalf(e, tb, 1);
		wi(e, "i64.xor");
		wi(e, "i64.or");
		wi(e, "i64.eqz");
		if (op == T_EXCLAIMEQ)
			wi(e, "i32.eqz");
	} else {
		bool sg = is_signed(c, t);
		const char *lo, *hi;

		switch (op) {
		case T_LESS: lo = "i64.lt_u"; hi = sg ? "i64.lt_s" : "i64.lt_u"; break;
		case T_GREATER: lo = "i64.gt_u"; hi = sg ? "i64.gt_s" : "i64.gt_u"; break;
		case T_LESSEQ: lo = "i64.le_u"; hi = sg ? "i64.lt_s" : "i64.lt_u"; break;
		default: lo = "i64.ge_u"; hi = sg ? "i64.gt_s" : "i64.gt_u"; break;
		}
		/* the high halves decide unless equal */
		whalf(e, ta, 0);
		whalf(e, tb, 0);
		wi(e, lo);
		whalf(e, ta, 1);
		whalf(e, tb, 1);
		wi(e, hi);
		whalf(e, ta, 1);
		whalf(e, tb, 1);
		wi(e, "i64.eq");
		wi(e, "i32.select");
	}
	wfree(e, ta);
	wfree(e, tb);
}

/* the value on the stack, of type from, as type to: one of them 16 bytes */
VPRIV void w16conv(ecx *e, qty from, qty to)
{
	cc *c = e->c;
	int f = w16(c, from), t = w16(c, to), fk = wty(c, from), tk = wty(c, to);
	bool fsg = is_signed(c, from) && is_integer(c, from) && KIND(c, from) != TY_BOOL;
	bool tsg = is_signed(c, to) && KIND(c, to) != TY_BOOL;

	if (f && t) {
		uint64_t sl;

		if (f == t)
			return;
		sl = wslot(e, 16, 16);
		wsplit(e);
		{
			uint32_t h = wtmp(e, WT_I64), l = wtmp(e, WT_I64);

			wset(e, h);
			wset(e, l);
			wslot_addr(e, sl);
			wget(e, l);
			wget(e, h);
			wfree(e, l);
			wfree(e, h);
		}
		if (t == 2)
			wrt(e, fsg ? "__floattitf" : "__floatuntitf", "(P, i64, i64) -> ()");
		else
			wrt(e, tsg ? "__fixtfti" : "__fixunstfti", "(P, i64, i64) -> ()");
		wslot_addr(e, sl);
		return;
	}
	if (f) {
		/* from a 16-byte value */
		if (KIND(c, to) == TY_BOOL) {
			if (f == 1) {
				wsplit(e);
				wi(e, "i64.or");
				wi(e, "i64.eqz");
				wi(e, "i32.eqz");
			} else {
				wsplit(e);
				wconst(e, WT_I64, 0);
				wconst(e, WT_I64, 0);
				wrt(e, "__netf2", "(i64, i64, i64, i64) -> (i32)");
				wconst(e, WT_I32, 0);
				wi(e, "i32.ne");
			}
			return;
		}
		if (f == 1) {
			if (tk == WT_F32 || tk == WT_F64) {
				wsplit(e);
				wrt(e, tk == WT_F32 ? (fsg ? "__floattisf" : "__floatuntisf") : (fsg ? "__floattidf" : "__floatuntidf"),
				    tk == WT_F32 ? "(i64, i64) -> (f32)" : "(i64, i64) -> (f64)");
				return;
			}
			wiu(e, "i64.load", 0);
			wconv(e, prim(TY_ULLONG), to);
			return;
		}
		wsplit(e);
		if (tk == WT_F32 || tk == WT_F64) {
			wrt(e, tk == WT_F32 ? "__trunctfsf2" : "__trunctfdf2", tk == WT_F32 ? "(i64, i64) -> (f32)" : "(i64, i64) -> (f64)");
			return;
		}
		if (tk == WT_I64) {
			wrt(e, tsg ? "__fixtfdi" : "__fixunstfdi", "(i64, i64) -> (i64)");
			return;
		}
		wrt(e, tsg || tsize(c, to) < 4 ? "__fixtfsi" : "__fixunstfsi", "(i64, i64) -> (i32)");
		wnarrow(e, prim(TY_INT), to);
		return;
	}
	/* to a 16-byte value */
	if (t == 1 && fk != WT_F32 && fk != WT_F64) {
		uint32_t l = wtmp(e, WT_I64);

		wconv(e, from, fsg ? prim(TY_LLONG) : prim(TY_ULLONG));
		wtee(e, l);
		if (fsg) {
			wget(e, l);
			wconst(e, WT_I64, 63);
			wi(e, "i64.shr_s");
		} else {
			wconst(e, WT_I64, 0);
		}
		wpair(e);
		wfree(e, l);
		return;
	}
	{
		uint64_t sl = wslot(e, 16, 16);
		uint32_t v = wtmp(e, fk);
		const char *fn, *sig;

		wset(e, v);
		wslot_addr(e, sl);
		wget(e, v);
		wfree(e, v);
		if (t == 1) {
			fn = fk == WT_F32 ? (tsg ? "__fixsfti" : "__fixunssfti") : (tsg ? "__fixdfti" : "__fixunsdfti");
			sig = fk == WT_F32 ? "(P, f32) -> ()" : "(P, f64) -> ()";
		} else if (fk == WT_F32 || fk == WT_F64) {
			fn = fk == WT_F32 ? "__extendsftf2" : "__extenddftf2";
			sig = fk == WT_F32 ? "(P, f32) -> ()" : "(P, f64) -> ()";
		} else if (fk == WT_I64) {
			fn = fsg ? "__floatditf" : "__floatunditf";
			sig = "(P, i64) -> ()";
		} else {
			fn = fsg || tsize(c, from) < 4 ? "__floatsitf" : "__floatunsitf";
			sig = "(P, i32) -> ()";
		}
		wrt(e, fn, sig);
		wslot_addr(e, sl);
	}
}

/* stack: [a] -> -a or ~a */
static void w16unary(ecx *e, int op, qty t)
{
	cc *c = e->c;
	uint32_t a = wtmp(e, WPT(c));

	wset(e, a);
	if (op == T_TILDE) {
		whalf(e, a, 0);
		wconst(e, WT_I64, -1);
		wi(e, "i64.xor");
		whalf(e, a, 1);
		wconst(e, WT_I64, -1);
		wi(e, "i64.xor");
	} else if (w16(c, t) == 2) {
		whalf(e, a, 0);
		whalf(e, a, 1);
		wconst(e, WT_I64, INT64_MIN);
		wi(e, "i64.xor");
	} else {
		wconst(e, WT_I64, 0);
		whalf(e, a, 0);
		wi(e, "i64.sub");
		wconst(e, WT_I64, 0);
		whalf(e, a, 1);
		wi(e, "i64.sub");
		whalf(e, a, 0);
		wi(e, "i64.eqz");
		wi(e, "i32.eqz");
		wi(e, "i64.extend_i32_u");
		wi(e, "i64.sub");
	}
	wpair(e);
	wfree(e, a);
}

/* ++/-- on a 16-byte lvalue */
static int w16incdec(ecx *e, uint32_t n, bool want)
{
	cc *c = e->c;
	const node *x = N(n);
	qty t = N(x->a)->ty;
	bool inc = x->op == N_PREINC || x->op == N_POSTINC, post = x->op == N_POSTINC || x->op == N_POSTDEC;
	uint32_t ta = wtmp(e, WPT(c));
	uint64_t old = 0;
	wlv l;

	wlval(e, x->a, &l, t);
	wset(e, ta);
	if (want && post) {
		old = wslot(e, 16, 16);
		wslot_addr(e, old);
		wget(e, ta);
		wcopy(e, 16);
	}
	wget(e, ta);
	wget(e, ta);
	if (w16(c, t) == 2)
		w16const(e, 0, 0x3fff000000000000ull);
	else
		w16const(e, 1, 0);
	w16arith(e, inc ? T_PLUS : T_MINUS, t, 0);
	wcopy(e, 16);
	if (want) {
		if (post)
			wslot_addr(e, old);
		else
			wget(e, ta);
	}
	wfree(e, ta);
	return want ? WPT(c) : WT_NONE;
}

/* ---------------- GNU vectors ---------------- */

static bool wis_cmp(int op);

/*
 * Lane by lane (no simd128, as clang without it): stack [a b] (b a vector,
 * a scalar against every lane, or nothing: unary - and ~) -> the address of
 * a new vector of type rt.  Comparisons give -1 / 0 lanes.
 */
static void wvlanes(ecx *e, int op, qty at, qty bt, qty rt, bool unary)
{
	cc *c = e->c;
	bool av = is_vec(c, at), bv = !unary && is_vec(c, bt);
	qty vt = av ? at : bt, et = T(c, vt)->base, rte = T(c, rt)->base;
	qty yt = unary ? et : bv ? T(c, bt)->base : bt, xt = av ? et : at;
	uint32_t n = T(c, vt)->n;
	uint32_t ta, tb = UINT32_MAX;
	uint64_t sl = wslot(e, tsize(c, rt), 16);
	int k = wty(c, et);
	bool shift = op == T_LESSLESS || op == T_GREATERGREATER;

	if (!unary) {
		tb = wtmp(e, wty(c, bt));
		wset(e, tb);
	}
	ta = wtmp(e, av ? WPT(c) : wty(c, at));
	wset(e, ta);
	for (uint32_t i = 0; i < n; i++) {
		wlv la = { .k = WL_MEM, .off = i * tsize(c, et) }, lr = { .k = WL_MEM, .off = i * tsize(c, rte) };

		if (is_bvec(c, at)) la.off = i / 8, la.bf = 1, la.width = 1, la.bitoff = i % 8;
		if (is_bvec(c, rt)) lr.off = i / 8, lr.bf = 1, lr.width = 1, lr.bitoff = i % 8;
		wslot_addr(e, sl);
		if (unary && op == T_MINUS && k != WT_F32 && k != WT_F64)
			wconst(e, k, 0);
		wget(e, ta);
		if (av)
			wload(e, &la, et);
		else if (QUNQ(xt) != QUNQ(et))
			wconv(e, xt, et);
		if (unary) {
			if (op == T_MINUS)
				wop(e, k, k == WT_F32 || k == WT_F64 ? "neg" : "sub");
			else {
				wconst(e, k, -1);
				wop(e, k, "xor");
			}
		} else {
			if (bv) {
				wlv lb = { .k = WL_MEM, .off = i * tsize(c, yt) };

				if (is_bvec(c, bt)) lb.off = i / 8, lb.bf = 1, lb.width = 1, lb.bitoff = i % 8;
				wget(e, tb);
				wload(e, &lb, yt);
			} else {
				wget(e, tb);
				if (!shift && QUNQ(yt) != QUNQ(et))
					wconv(e, yt, et);
			}
			if (wis_cmp(op)) {
				wcompare(e, op, et);
				/* 0 / 1 -> 0 / all ones */
				wconst(e, WT_I32, -1);
				wi(e, "i32.mul");
				wconv(e, prim(TY_INT), rte);
			} else {
				warith(e, op, et, shift ? yt : 0);
			}
		}
		wstore(e, &lr, rte);
	}
	wfree(e, ta);
	if (tb != UINT32_MAX)
		wfree(e, tb);
	wslot_addr(e, sl);
}

/* ---------------- operators ---------------- */

static bool wis_cmp(int op)
{
	return op == T_EQUALEQUAL || op == T_EXCLAIMEQ || op == T_LESS || op == T_GREATER || op == T_LESSEQ ||
	       op == T_GREATEREQ;
}

/* stack: [a b] of C type ty (b of rty: a shift count) -> a op b */
VPRIV void warith(ecx *e, int op, qty ty, qty rty)
{
	cc *c = e->c;
	int k = wty(c, ty);
	bool sg = is_signed(c, ty);
	bool fl = k == WT_F32 || k == WT_F64;
	const char *o = NULL;

	if (k == WT_MEM && w16(c, ty)) {
		w16arith(e, op, ty, rty);
		return;
	}
	if (k == WT_MEM)
		efatal(e, e->curloc, "wasm: arithmetic on this type is not supported yet (PLAN-wasm.md W4)");
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
	if ((op == T_LESSLESS || op == T_GREATERGREATER) && rty && (wty(c, rty) == WT_MEM || wty(c, rty) != k)) {
		if (wty(c, rty) == WT_MEM) {
			wiu(e, "i64.load", 0);
			if (k == WT_I32)
				wi(e, "i32.wrap_i64");
		} else {
			wi(e, k == WT_I64 ? "i64.extend_i32_u" : "i32.wrap_i64");
		}
	}
	wop(e, k, o);
}

/* stack: [a b] of type t -> a op b as an i32 0/1 */
VPRIV void wcompare(ecx *e, int op, qty t)
{
	cc *c = e->c;
	int k = wty(c, t);
	bool fl = k == WT_F32 || k == WT_F64;
	bool sg = !is_ptr(c, t) && is_signed(c, t);
	char o[8];

	if (k == WT_MEM && w16(c, t)) {
		w16cmp(e, op, t);
		return;
	}
	if (k == WT_MEM)
		efatal(e, e->curloc, "wasm: comparisons of this type are not supported yet (PLAN-wasm.md W4)");
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

/* The shared rules consume a known RHS; wasm owns its effects and stack. */
static bool wbinrule(cc *c, uint32_t n, qty ty, int *op, i128 *rhs, int *identity, bool pure)
{
	cval cv;

	switch (*op) {
	case T_PLUS: case T_MINUS: case T_STAR: case T_SLASH: case T_PERCENT:
	case T_AMP: case T_PIPE: case T_CARET: case T_LESSLESS: case T_GREATERGREATER:
		break;
	default:
		return false;
	}
	if (!cc_eval(c, n, &cv) || cv.kind != CV_INT)
		return false;
	*rhs = cv.i;
	*identity = em_bin_identity(c, ty, *op, *rhs);
	if (*identity == EM_ID_NONE && !em_divpow2(c, ty, op, rhs)) {
		em_sdivpow2_recipe recipe;

		if (!em_sdivpow2(c, ty, *op, *rhs, &recipe) || !wsdivpow2_ok(&recipe))
			return false;
	}
	return pure || !side_effects(c, n);
}

/* stack: [lhs] converted to ty; omit a pure RHS and apply its shared rule. */
static void wbinreduced(ecx *e, qty ty, int op, i128 rhs, int identity)
{
	if (identity == EM_ID_LEFT)
		return;
	if (identity == EM_ID_ZERO || identity == EM_ID_ONES) {
		wi(e, "drop");
		wconst(e, wty(e->c, ty), identity == EM_ID_ZERO ? 0 : (int64_t)em_trunc_to(e->c, -1, ty));
		return;
	}
	{
		em_sdivpow2_recipe recipe;

		if (em_sdivpow2(e->c, ty, op, rhs, &recipe) && wsdivpow2_ok(&recipe)) {
			wsdivpow2(e, ty, &recipe);
			return;
		}
	}
	/* The replacement RHS already has the operation's wasm stack type. */
	wconst(e, wty(e->c, ty), (int64_t)rhs);
	warith(e, op, ty, 0);
}

static int wbinary(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	int op = x->aux;
	qty lt = N(x->a)->ty, rt = N(x->b)->ty, ty = x->ty;

	if (unlikely(vstack_low()))
		efatal(e, x->loc, "expression nested too deeply");
	if (is_vec(c, ty) || is_vec(c, lt)) {
		wexpr(e, x->a, true);
		wexpr(e, x->b, true);
		wvlanes(e, op, lt, rt, ty, false);
		return WPT(c);
	}
	if (is_ptr(c, ty) && (op == T_PLUS || op == T_MINUS)) {
		bool lp = is_ptr(c, lt);
		uint64_t es = welem_size(e, lp ? lt : rt);

		wexpr(e, x->a, true);
		if (!lp) {
			wto_ptr(e, lt);
			wscale(e, rt, es);
		}
		wexpr(e, x->b, true);
		if (lp) {
			wto_ptr(e, rt);
			wscale(e, lt, es);
		}
		wpop(e, op == T_PLUS ? "add" : "sub");
		return WPT(c);
	}
	if (op == T_MINUS && is_ptr(c, lt) && is_ptr(c, rt)) {
		uint64_t es = welem_size(e, lt);

		wexpr(e, x->a, true);
		wexpr(e, x->b, true);
		wpop(e, "sub");
		if (es != 1) {
			if (es)
				wpconst(e, (int64_t)es);
			else
				wvla_bytes(e, T(c, lt)->base);
			wpop(e, "div_s");
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
	int reduced_op = op;
	i128 rhs = 0;
	int identity = EM_ID_NONE;
	bool reduced = wbinrule(c, x->b, ty, &reduced_op, &rhs, &identity, false);
	wexpr(e, x->a, true);
	if (QUNQ(lt) != QUNQ(ty) && op != T_LESSLESS && op != T_GREATERGREATER)
		wconv(e, lt, ty);
	if (reduced) {
		wbinreduced(e, ty, reduced_op, rhs, identity);
	} else {
		wexpr(e, x->b, true);
		if (QUNQ(rt) != QUNQ(ty) && op != T_LESSLESS && op != T_GREATERGREATER)
			wconv(e, rt, ty);
		warith(e, op, ty, (op == T_LESSLESS || op == T_GREATERGREATER) ? rt : 0);
	}
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
	if (wty(c, x->ty) == WT_MEM)
		k = WT_MEM;	/* the address of a 16-byte value */
	switch (k) {
	case WT_I32: break;
	case WT_I64: wi(e, "i64.eqz"); wi(e, "i32.eqz"); break;
	case WT_F32: wfconst(e, WT_F32, 0); wi(e, "f32.ne"); break;
	case WT_F64: wfconst(e, WT_F64, 0); wi(e, "f64.ne"); break;
	default:
		if (!w16(c, x->ty))
			efatal(e, x->loc, "wasm: a condition of this type is not supported yet (PLAN-wasm.md W4)");
		w16conv(e, x->ty, prim(TY_BOOL));
		break;
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

/* a condition known when compiling: 1 true, 0 false, -1 not known (clang folds these away) */
/* does expression n contain __builtin_constant_p (the front end folds it to 0) */
static bool wbcp_in(cc *c, uint32_t n)
{
	const node *x;

	if (!n)
		return false;
	x = N(n);
	switch (x->op) {
	case N_BUILTIN:
		if (x->aux == BI_CONSTANT_P)
			return true;
		/* fall through */
	case N_CALL:
		for (uint32_t i = 0; i < x->c; i++)
			if (wbcp_in(c, c->lists[x->b + i]))
				return true;
		return x->op == N_CALL && wbcp_in(c, x->a);
	case N_CAST: case N_UNARY:
		return wbcp_in(c, x->a);
	case N_BINARY: case N_LOGAND: case N_LOGOR: case N_COMMA:
		return wbcp_in(c, x->a) || wbcp_in(c, x->b);
	case N_COND:
		return wbcp_in(c, x->a) || wbcp_in(c, x->b) || wbcp_in(c, x->c);
	default:
		return false;
	}
}

VPRIV int wknown(ecx *e, uint32_t n)
{
	cc *c = e->c;
	cval cv;
	uint32_t r = n;

	while (N(r)->op == N_CAST && (N(r)->aux == CK_INT || N(r)->aux == CK_NOP || N(r)->aux == CK_BOOL))
		r = N(r)->a;
	if (N(r)->op == N_BUILTIN && N(r)->aux == BI_CONSTANT_P)
		return wconstant_p(e, c->lists[N(r)->b]);
	if (wbcp_in(c, n))
		return -1;
	if (side_effects(c, n) || !cc_eval(c, n, &cv))
		return -1;
	if (cv.kind == CV_INT)
		return cv.i != 0;
	if (cv.kind == CV_FLT)
		return cv.ld ? cv.q << 1 != 0 : cv.f != 0;
	return -1;
}

static int wcond_expr(ecx *e, uint32_t n, bool want)
{
	cc *c = e->c;
	const node *x = N(n);
	int k = want ? wrep(c, wty(c, x->ty)) : WT_NONE, kn;

	if (x->b && (kn = wknown(e, x->a)) >= 0) {
		wexpr(e, kn ? x->b : x->c, want);
		return k;
	}

	if (!x->b) {
		/* GNU a ?: c */
		qty at = N(x->a)->ty;
		int ak = wty(c, at);
		uint32_t t = wtmp(e, ak);

		wexpr(e, x->a, true);
		wtee(e, t);
		switch (wrep(c, ak)) {
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

	if (is_vec(c, lt) && x->aux) {
		/* v op= w */
		uint32_t ta = wtmp(e, WPT(c));

		wlval(e, x->a, &l, lt);
		wtee(e, ta);
		wget(e, ta);
		wexpr(e, x->b, true);
		wvlanes(e, x->aux, lt, N(x->b)->ty, lt, false);
		wcopy(e, tsize(c, lt));
		if (want)
			wget(e, ta);
		wfree(e, ta);
		return want ? WPT(c) : WT_NONE;
	}
	/* _Atomic: plain loads and stores, one thread (clang without the atomics feature) */
	if (!x->aux) {
		wlval(e, x->a, &l, lt);
		if (k == WT_MEM) {
			uint32_t ta = want ? wtmp(e, WPT(c)) : UINT32_MAX;

			if (want)
				wtee(e, ta);
			wexpr(e, x->b, true);
			wcopy(e, tsize(c, lt));
			if (want) {
				wget(e, ta);
				wfree(e, ta);
			}
			return want ? WPT(c) : WT_NONE;
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
		uint32_t ta = UINT32_MAX, tb = UINT32_MAX;

		if (side_effects(c, x->b)) {
			/* clang: the right operand first, then the left is read */
			tb = wtmp(e, wty(c, bt));
			wexpr(e, x->b, true);
			wset(e, tb);
		}
		wlval(e, x->a, &l, lt);
		if (l.k == WL_MEM) {
			ta = wtmp(e, WPT(c));
			wtee(e, ta);
			wget(e, ta);
		}
		wload(e, &l, lt);
		if (is_ptr(c, lt)) {
			uint64_t es = welem_size(e, lt);

			if (tb != UINT32_MAX)
				wget(e, tb);
			else
				wexpr(e, x->b, true);
			wto_ptr(e, bt);
			wscale(e, lt, es);
			wpop(e, op == T_PLUS ? "add" : "sub");
		} else {
			int reduced_op = op;
			i128 rhs = 0;
			int identity = EM_ID_NONE;
			bool reduced = false;

			if (!pt)
				pt = lt;
			/* No RHS effects: otherwise it was evaluated into tb above. */
			if (tb == UINT32_MAX)
				reduced = wbinrule(c, x->b, pt, &reduced_op, &rhs, &identity, true);
			wconv(e, lt, pt);
			if (reduced) {
				wbinreduced(e, pt, reduced_op, rhs, identity);
			} else {
				if (tb != UINT32_MAX)
					wget(e, tb);
				else
					wexpr(e, x->b, true);
				if (op != T_LESSLESS && op != T_GREATERGREATER && QUNQ(bt) != QUNQ(pt))
					wconv(e, bt, pt);
				warith(e, op, pt, (op == T_LESSLESS || op == T_GREATERGREATER) ? bt : 0);
			}
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
		if (tb != UINT32_MAX)
			wfree(e, tb);
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

	if (k == WT_MEM && w16(c, t))
		return w16incdec(e, n, want);
	if (k == WT_MEM || is_vec(c, t))
		efatal(e, x->loc, "wasm: ++/-- on this type is not supported yet (PLAN-wasm.md W4)");
	wlval(e, x->a, &l, t);
	if (l.k == WL_MEM) {
		ta = wtmp(e, WPT(c));
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
		if (is_ptr(c, t) && !welem_size(e, t))
			wvla_bytes(e, T(c, t)->base);
		else
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

/* the value statement of a statement expression: into W->sxt (a record is
   copied to a slot of its own, it outlives the block's objects) */
VPRIV void wst_sxval(ecx *e, uint32_t v)
{
	cc *c = e->c;
	wst *W = e->w;
	qty vt = N(v)->ty;

	wexpr(e, v, true);
	if (wty(c, vt) == WT_MEM) {
		uint64_t sl = wslot(e, tsize(c, vt), talign(c, vt));

		wset(e, W->sxt);
		wslot_addr(e, sl);
		wget(e, W->sxt);
		wcopy(e, tsize(c, vt));
		wslot_addr(e, sl);
	}
	wset(e, W->sxt);
}

/* a statement expression's value: the last statement's, if it is an expression */
static int wstmtexpr(ecx *e, uint32_t n, bool want)
{
	cc *c = e->c;
	wst *W = e->w;
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
		int k = wty(c, N(N(vs)->a)->ty);
		uint32_t osv = W->sxv, ost = W->sxt, t = wtmp(e, k);

		/* the whole compound (goto dispatch, VLAs, cleanups); the value into t */
		W->sxv = vs, W->sxt = t;
		wstmt(e, n);
		W->sxv = osv, W->sxt = ost;
		wget(e, t);
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

	if (unlikely(em_bitint(c, x->ty)))
		efatal(e, x->loc, "_BitInt runtime code generation is not supported yet");
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
			return WPT(c);
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
			return WPT(c);
		}
		if (k != WT_F32 && k != WT_F64)
			efatal(e, x->loc, "wasm: _Float16 is not supported");
		wfconst(e, k, c->fpool[x->a]);
		return k;
	case N_STR:
		waddr_ref(e, REF(RK_STR, x->a), 0);
		return WPT(c);
	case N_VAR: {
		const sym *y = &c->syms[x->a];

		if (y->kind == S_FUNC || y->kind == S_BUILTIN) {
			uint32_t r = y->kind == S_FUNC ? REF(RK_SYM, canon_sym(c, x->a)) : REF(RK_NAME, (uint32_t)y->val);
			tbuf *b = &e->w->b;

			claim_ref(e, r);
			wdecl(e, r, y->ty, NULL);
			tb_s(b, WASM32(c) ? "\ti32.const\t" : "\ti64.const\t");
			wfname(c, b, r);
			tb_c(b, '\n');
			return WPT(c);
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
		return WPT(c);
	}
	case N_CAST:
		switch (x->aux) {
		case CK_DECAY: {
			wlv l;

			wlval(e, x->a, &l, prim(TY_VOID));
			if (l.k != WL_MEM)
				efatal(e, x->loc, "wasm: array in a register");
			wfold_off(e, &l);
			return WPT(c);
		}
		case CK_FDECAY:
			return wexpr(e, x->a, want);
		case CK_VOID:
			wexpr(e, x->a, false);
			return WT_NONE;
		case CK_BITS: {
			qty from = N(x->a)->ty;
			uint64_t sl = wslot(e, tsize(c, x->ty), talign(c, x->ty));
			wlv d = { .k = WL_MEM };
			wslot_addr(e, sl);
			if (N(x->a)->flags & NF_LVALUE) {
				wlv src;
				wlval(e, x->a, &src, prim(TY_VOID));
				if (src.bf) {
					uint32_t addr = wtmp(e, WPT(c));
					wset(e, addr); wi(e, "drop"); /* retain the source, replace dst per byte */
					for (unsigned i = 0; i < tsize(c, from); i++) {
						unsigned bit = i * 8;
						wlv byte = { .k = WL_MEM, .off = i };
						wslot_addr(e, sl);
						if (bit < src.width || src.sgn) {
							wlv part = src;
							unsigned start = bit < src.width ? bit : src.width - 1;
							part.off += (src.bitoff + start) / 8;
							part.bitoff = (src.bitoff + start) % 8;
							part.width = bit >= src.width ? 1 : src.width - bit < 8 ? src.width - bit : 8;
							part.sgn = src.sgn && bit + 8 >= src.width;
							wget(e, addr); wload(e, &part, prim(TY_UCHAR));
						} else wconst(e, WT_I32, 0);
						wstore(e, &byte, prim(TY_UCHAR));
					}
					wfree(e, addr);

				} else {
					wfold_off(e, &src);
					wpconst(e, (int64_t)tsize(c, from)); wi(e, "memory.copy\t0, 0");
				}
			} else { wexpr(e, x->a, true); wstore(e, &d, from); }
			wslot_addr(e, sl);
			if (k != WT_MEM) wload(e, &d, x->ty);
			return k;
		}
		case CK_UNION: {
			qty at = N(x->a)->ty;
			uint64_t sl = wslot(e, tsize(c, x->ty), talign(c, x->ty));
			wlv l = { .k = WL_MEM };

			wslot_addr(e, sl);
			wconst(e, WT_I32, 0);
			wpconst(e, (int64_t)tsize(c, x->ty));
			wi(e, "memory.fill\t0");
			wslot_addr(e, sl);
			wexpr(e, x->a, true);
			wstore(e, &l, at);
			wslot_addr(e, sl);
			return WPT(c);
		}
		case CK_SPLAT: {	/* the scalar into every lane of a new vector */
			qty et = T(c, x->ty)->base;
			uint64_t es = tsize(c, et), nl = T(c, x->ty)->n, sl = wslot(e, tsize(c, x->ty), 16);
			uint32_t t;

			wexpr(e, x->a, true);
			t = wtmp(e, wty(c, et));
			wset(e, t);
			for (uint64_t i = 0; i < nl; i++) {
				wlv l = { .k = WL_MEM, .off = i * es };

				if (is_bvec(c, x->ty)) l.off = i / 8, l.bf = 1, l.width = 1, l.bitoff = i % 8;
				wslot_addr(e, sl);
				wget(e, t);
				wstore(e, &l, et);
			}
			wfree(e, t);
			wslot_addr(e, sl);
			return WPT(c);
		}
		case CK_VADDR:
			/* a vector value is already the address of its first lane */
			wexpr(e, x->a, true);
			return WPT(c);
		case CK_VBITS:
			wexpr(e, x->a, true);
			if (k != WT_MEM) {
				wlv l = { .k = WL_MEM };

				wload(e, &l, x->ty);
			}
			return k;
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
			if (k == WT_MEM) {
				if (!is_vec(c, x->ty) && !w16(c, x->ty))
					break;
				wexpr(e, x->a, true);
				if (is_vec(c, x->ty))
					wvlanes(e, T_MINUS, x->ty, 0, x->ty, true);
				else
					w16unary(e, T_MINUS, x->ty);
				return WPT(c);
			}
			wconst(e, k, 0);
			wexpr(e, x->a, true);
			wop(e, k, "sub");
			if (k == WT_I32)
				wnarrow(e, prim(TY_INT), x->ty);
			return k;
		case T_TILDE:
			if (k == WT_MEM) {
				if (!is_vec(c, x->ty) && !w16(c, x->ty))
					break;
				wexpr(e, x->a, true);
				if (is_vec(c, x->ty))
					wvlanes(e, T_TILDE, x->ty, 0, x->ty, true);
				else
					w16unary(e, T_TILDE, x->ty);
				return WPT(c);
			}
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
	case N_LOGOR: {
		int kn = wknown(e, x->a);

		if (kn >= 0) {
			/* 0 && b, 1 || b: b is not evaluated; 1 && b, 0 || b: b decides */
			if (kn == (x->op == N_LOGOR)) {
				if (!want)
					return WT_NONE;
				wconst(e, WT_I32, kn);
				return WT_I32;
			}
			if (!want) {
				wexpr(e, x->b, false);
				return WT_NONE;
			}
			wbool(e, x->b);
			return WT_I32;
		}
	}
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
			wpconst(e, (int64_t)tsize(c, x->ty));
			wi(e, "memory.fill\t0");
			wslot_addr(e, sl);
			return WPT(c);
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
		/* the imaginary part's address (a record copy, long double, takes no offset) */
		wslot_addr(e, sl + es);
		wexpr(e, x->b, true);
		wstore(e, &l, et);
		wslot_addr(e, sl);
		return WPT(c);
	}
	case N_VAARG:
		return wbuiltin(e, n, want);
	case N_LABELADDR:
		/* its number (1-based), as in data: goto * compares against these */
		wpconst(e, (int64_t)x->a + 1);
		return WPT(c);
	case N_SIZEOF_VLA:
		if (x->aux == 1) {
			wvla_fix(e, x->b, false);
			wvla_bytes(e, x->b);
			wconv(e, prim(TY_ULONG), x->ty);
			return k;
		}
		if (x->a) wexpr(e, x->a, false);
		wvla_bytes(e, x->b);
		wconv(e, prim(TY_ULONG), x->ty);
		return k;
	default:
		break;
	}
	switch (x->op) {
	case N_VAR: case N_MEMBER: case N_INDEX: case N_DEREF: case N_COMPLIT: {
		wlv l;

		if (is_func(c, x->ty)) {
			/* *fp: the function's address (its table index) */
			wlval(e, n, &l, prim(TY_VOID));
			wfold_off(e, &l);
			return WPT(c);
		}
		wlval(e, n, &l, x->ty);
		wload(e, &l, x->ty);
		return wrep(c, k);
	}
	}
	efatal(e, x->loc, "wasm: unsupported expression (node %d)", x->op);
}
