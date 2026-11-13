/* The simplifier's value numbering: constants, pure instructions and loads of the dominators. */
#define WK_PREFIX vw_
#define LO_PREFIX vw_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "simplify.h"

/* the table's entry for key x (an empty one when it has none) */
VPRIV vwhe *vw_hlook(vwsimp *S, const vwhe *x)
{
	uint64_t h = (uint64_t)x->op * 0x9e3779b97f4a7c15ull;

	h = (h ^ x->ty ^ (uint64_t)x->fl << 8) * 0xff51afd7ed558ccdull;
	h = (h ^ x->a ^ (uint64_t)x->b << 32) * 0xc4ceb9fe1a85ec53ull;
	h = (h ^ x->c ^ (uint64_t)x->gen << 32) * 0x9e3779b97f4a7c15ull;
	for (uint32_t i = (uint32_t)(h >> 40) & S->hmask;; i = (i + 1) & S->hmask) {
		vwhe *t = &S->ht[i];

		if (!t->v || (t->op == x->op && t->ty == x->ty && t->fl == x->fl && t->a == x->a &&
			      t->b == x->b && t->c == x->c && t->gen == x->gen))
			return t;
	}
}

/* one pre-IR value per constant (the walker makes one per use) */
static uint32_t vw_kcanon(vwst *V, vwsimp *S, uint32_t v)
{
	const vir_ins *in = &V->p[v].in;
	uint64_t h;

	if (V->p[v].blk != VW_NOBLK || in->op == VO_BPARAM)
		return v;
	h = ((uint64_t)in->op << 8 | in->ty) * 0x9e3779b97f4a7c15ull;
	h = (h ^ in->a ^ (uint64_t)in->b << 32) * 0xff51afd7ed558ccdull;
	h = (h ^ in->c) * 0xc4ceb9fe1a85ec53ull;
	for (uint32_t i = (uint32_t)(h >> 40) & S->kmask;; i = (i + 1) & S->kmask) {
		uint32_t k = S->kt[i];

		if (!k) {
			if (2 * ++S->nk > S->kmask)
				return v;	/* (full: v as it is) */
			S->kt[i] = v + 1;
			return v;
		}
		k--;
		if (V->p[k].in.op == in->op && V->p[k].in.ty == in->ty && V->p[k].in.a == in->a &&
		    V->p[k].in.b == in->b && V->p[k].in.c == in->c)
			return k;
	}
}

/* the block's instructions, rewritten and numbered (S->ht holds what its dominators computed) */
VPRIV void vw_cse_blk(ecx *e, vwsimp *S_, uint32_t b)
{
	vwst *V = e->vw;
	vwsimp *S = S_;

	for (uint32_t q = V->b[b].head; q; q = V->p[q].next) {
		uint32_t r, op, eff;
		vwhe key, *t;

		if (vw_find(V, q) != q)
			continue;
	again:
		VW_EACHOP(V, q, o, *o = vw_kcanon(V, S, vw_find(V, *o)));
		for (int it = 0; vw_simp1(e, S, q, &r) && it < 8; it++)
			;	/* (rewritten in place: again) */
		if (r != q) {
			r = vw_kcanon(V, S, vw_find(V, r));
			if (r < S->n0 && V->p[r].blk != VW_NOBLK)
				S->cnt[r] += S->cnt[q];
			VW_EACHOP(V, q, o, vw_dec(V, S, *o));
			S->cnt[q] = 0;
			V->fwd[q] = r;
			continue;
		}
		op = V->p[q].in.op;
		eff = vir_optab[op].eff;
		if (vw_promdead(V, q))
			continue;
		if (eff & (EF_W | EF_C | EF_A) || op == VO_BARRIER || op == VO_VA_ARG || op == VO_VA_START)
			S->memgen = ++S->gens;
		if (op == VO_STORE) {
			vir_ins in = V->p[q].in;
			uint32_t vt = V->p[in.b].in.ty;

			if (!(in.fl & VIF_VOL) && vir_tysize(vt) && 1u << VIF_W(in.fl) == vir_tysize(vt)) {
				key = (vwhe){ VO_LOAD, vt, VIF_W(in.fl), in.a, 0, 0, S->memgen, 0 };
				t = vw_hlook(S, &key);
				if (!t->v)
					S->log[S->nlog++] = (uint32_t)(t - S->ht);
				*t = key;
				t->v = in.b;
			}
			if (!(in.fl & VIF_VOL) && (vt == VT_I32 || vt == VT_I64)) {	/* (any load of its width) */
				key = (vwhe){ VO_STORE, 0, VIF_W(in.fl), in.a, 0, 0, S->memgen, 0 };
				t = vw_hlook(S, &key);
				if (!t->v)
					S->log[S->nlog++] = (uint32_t)(t - S->ht);
				*t = key;
				t->v = in.b;
			}
			continue;
		}
		if (op == VO_LOAD) {
			vir_ins in = V->p[q].in;

			if (in.fl & VIF_VOL)
				continue;
			key = (vwhe){ VO_LOAD, in.ty, in.fl & (3 | VIF_MS), in.a, 0, 0, S->memgen, 0 };
			if ((in.ty == VT_I32 || in.ty == VT_I64) && !vw_hlook(S, &key)->v) {
				/* the value a store of this width left: truncated or extended to the load's */
				vwhe sk = { VO_STORE, 0, VIF_W(in.fl), in.a, 0, 0, S->memgen, 0 };
				uint32_t v = vw_hlook(S, &sk)->v, w = VIF_W(in.fl), vt;

				if (v) {
					v = vw_find(V, v);
					vt = V->p[v].in.ty;
					if (in.ty == VT_I32 && vt == VT_I64 && w == 2)
						V->p[q].in = (vir_ins){ VO_TRUNC, VT_I32, 0, v, 0, 0 };
					else if (w < 3 && vir_tysize(vt) <= vir_tysize(in.ty))
						V->p[q].in = (vir_ins){ VO_EXT, (uint8_t)in.ty, (uint16_t)(w | (in.fl & VIF_MS)), v, 0, 0 };
					else
						v = 0;
					if (v) {
						vw_dec(V, S, in.a), vw_inc(V, S, v);
						goto again;
					}
				}
			}
		} else if ((eff & ~EF_UF) == EF_P && op != VO_BPARAM && op != VO_PARAM && op != VO_PROJ) {
			vir_ins in = V->p[q].in;

			if (vw_commutes(op) && in.a > in.b)
				key = (vwhe){ op, in.ty, in.fl, in.b, in.a, in.c, S->blk, 0 };
			else
				key = (vwhe){ op, in.ty, in.fl, in.a, in.b, in.c, S->blk, 0 };
		} else
			continue;
		t = vw_hlook(S, &key);
		if (op == VO_LOAD && !t->v && q < S->n0 && S->seq[q]) {
			vwaddr A;
			bool ok;

			vw_baseoff(V, V->p[q].in.a, &A);
			if (A.off > -VW_FAROFF && A.off < VW_FAROFF) {
				vwhe *g;

				/* this memory state's entry first: the two lookups may meet in one empty slot */
				S->log[S->nlog++] = (uint32_t)(t - S->ht);
				*t = key;
				t->v = q;
				g = vw_far(V, S, q, &A, &ok);
				if (ok)
					t = g;	/* (q's own entry stays: it is forwarded to the same value) */
				else if (!g->v) {
					S->log[S->nlog++] = (uint32_t)(g - S->ht);
					g->v = q;
					continue;
				} else
					continue;
			}
		}
		if (t->v && op == VO_LOAD && V->p[vw_find(V, t->v)].in.ty != V->p[q].in.ty) {
			/* the same bytes, loaded as the other integer type (a truncation retyped it) */
			vir_ins in = V->p[q].in;
			uint32_t v = vw_find(V, t->v), vt = V->p[v].in.ty;

			if (in.ty == VT_I32 && vt == VT_I64)
				V->p[q].in = (vir_ins){ VO_TRUNC, VT_I32, 0, v, 0, 0 };
			else if (in.ty == VT_I64 && vt == VT_I32 && VIF_W(in.fl) < 3)
				V->p[q].in = (vir_ins){ VO_EXT, VT_I64, (uint16_t)(in.fl & (3 | VIF_MS)), v, 0, 0 };
			else
				v = 0;
			if (v) {
				vw_dec(V, S, in.a), vw_inc(V, S, v);
				goto again;
			}
		}
		if (t->v && V->p[vw_find(V, t->v)].in.ty == V->p[q].in.ty) {
			r = vw_find(V, t->v);
			if (r < S->n0 && V->p[r].blk != VW_NOBLK)
				S->cnt[r] += S->cnt[q];
			VW_EACHOP(V, q, o, vw_dec(V, S, *o));
			S->cnt[q] = 0;
			V->fwd[q] = r;
			continue;
		}
		if (!t->v) {
			S->log[S->nlog++] = (uint32_t)(t - S->ht);
			*t = key;
			t->v = q;
		}
	}
}
