/* The simplifier's driver and the pass that gives each access its own address. */
#define WK_PREFIX vw_
#define LO_PREFIX vw_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "simplify.h"

/*
 * base + constant as an address is the load's or store's own operand on the CPU, but the
 * lowering folds only a ptradd that its one user follows directly; numbering gives a
 * block's accesses to one field a shared ptradd, which then costs an instruction and a
 * register.  So each such access gets its own copy just before it (the shared one dies
 * when nothing else reads it).  Only offsets every CPU here encodes: -256..255, or a
 * multiple of the access size up to 4095 of them.
 */
static void vw_ownaddr(ecx *e, vwsimp *S)
{
	vwst *V = e->vw;

	for (uint32_t i = 0; i < V->norder; i++) {
		uint32_t b = V->order[i];

		for (uint32_t q = V->b[b].head, pq = 0; q; pq = q, q = V->p[q].next) {
			vir_ins in = V->p[q].in;
			uint32_t a, n, w = 1u << VIF_W(in.fl);
			int64_t k;

			if ((in.op != VO_LOAD && in.op != VO_STORE) || q >= S->n0 || vw_find(V, q) != q || vw_promdead(V, q) ||
			    (in.op == VO_LOAD && !S->cnt[q] && !(in.fl & VIF_VOL)))
				continue;
			a = vw_find(V, in.a);
			if (a >= S->n0 || V->p[a].in.op != VO_PTRADD || V->p[a].blk == VW_NOBLK ||
			    !vw_kint(V, vw_find(V, V->p[a].in.b), &k) ||
			    !((k >= -256 && k <= 255) || (k > 0 && !(k % w) && k / w <= 4095)))
				continue;
			if (a == pq && S->cnt[a] == 1)
				continue;	/* (its own already) */
			n = vw_put(e, VO_PTRADD, VT_PTR, V->p[a].in.fl, vw_find(V, V->p[a].in.a), vw_find(V, V->p[a].in.b), 0, b);
			V->p[n].loc = V->p[q].loc;
			V->p[n].next = q;
			if (pq)
				V->p[pq].next = n;
			else
				V->b[b].head = n;
			V->p[q].in.a = n;
			vw_dec(V, S, a), vw_inc(V, S, V->p[n].in.a);
		}
	}
}

VPRIV void vw_simplify(ecx *e)
{
	vwst *V = e->vw;
	vwsimp S = { 0 };
	uint32_t *stk, ns = 0;

	S.n0 = V->np;
	S.cnt = calloc(2 * ((size_t)S.n0 + 1), sizeof *S.cnt);
	stk = malloc(((size_t)S.n0 + 1) * sizeof *stk);
	{
		uint32_t kc = 16;

		for (uint32_t i = 0; i < V->np; i++)
			S.ncst += V->p[i].blk == VW_NOBLK;
		while (kc < 2 * (V->np + 64))	/* (and the constants the pass makes) */
			kc *= 2;
		S.kt = calloc(kc, sizeof *S.kt);
		S.kmask = kc - 1;
	}
	if (!S.cnt || !stk || !S.kt)
		vw_fail(e, VIRFAIL(out_of_memory));
	uint32_t *pcopy = S.cnt + S.n0 + 1;
	/* uses */
	for (uint32_t i = 0; i < V->norder; i++) {
		uint32_t b = V->order[i];

		for (uint32_t p = V->b[b].phead; p; p = V->p[p].next)
			if (V->fwd[p] == p)
				VW_EACHOP(V, p, o, vw_inc(V, &S, vw_find(V, *o)));
		for (uint32_t q = V->b[b].head;; q = V->p[q].next) {
			uint32_t x = q ? q : V->b[b].term;

			if (!x)
				break;
			if (vw_find(V, x) == x && !vw_promdead(V, x)) {
				VW_EACHOP(V, x, o, vw_inc(V, &S, vw_find(V, *o)));
				if (V->p[x].in.op == VO_I2P) pcopy[vw_find(V, V->p[x].in.a)]++;
			}
			if (!q)
				break;
		}
	}
	/* Pointer-only views need no separately allocated integer carrier. */
	for (uint32_t q = 0; q < S.n0; q++) {
		vir_ins *in = &V->p[q].in;
		if (in->op == VO_LOAD && in->ty == VT_I64 && !(in->fl & (VIF_VOL | VIF_MS)) &&
		    VIF_W(in->fl) == 3 && pcopy[q] && pcopy[q] == S.cnt[q]) in->ty = VT_PTR;
	}
	/* rewriting and numbering over the dominator tree */
	vw_doms(e, &S);
	vw_far_init(e, &S);
	{
		uint32_t cap = 16, top = 0, *bst = malloc((size_t)(V->nb + 1) * 2 * sizeof *bst);

		while (cap < 2 * (V->np - S.ncst) + 2)
			cap *= 2;
		S.ht = calloc(cap, sizeof *S.ht);
		S.hmask = cap - 1;
		S.log = malloc((2 * (size_t)V->np + 2) * sizeof *S.log);	/* (a store or a load adds two) */
		S.egen = malloc(((size_t)V->nb + 1) * sizeof *S.egen);
		if (!S.ht || !S.log || !bst || !S.egen)
			vw_fail(e, VIRFAIL(out_of_memory));
		bst[top++] = V->order[0], bst[top++] = 0;
		while (top) {
			uint32_t ph = bst[--top], b = bst[--top];

			if (ph) {	/* leaving b: its entries go */
				while (S.nlog > ph - 1) {
					S.nlog--;
					S.ht[S.log[S.nlog]].v = 0;
				}
				continue;
			}
			bst[top++] = b, bst[top++] = S.nlog + 1;
			S.memgen = V->b[b].npred == 1 ? S.egen[V->pr[V->b[b].pred]] : ++S.gens;
			S.blk = ++S.gens;
			vw_cse_blk(e, &S, b);
			S.egen[b] = S.memgen;
			for (uint32_t c = S.dkid[b]; c != VW_NOBLK; c = S.dnext[c])
				bst[top++] = c, bst[top++] = 0;
		}
		free(bst);
	}
	vw_ownaddr(e, &S);
	/* what nothing reads */
	for (uint32_t i = 0; i < V->norder; i++)
		for (uint32_t q = V->b[V->order[i]].head; q; q = V->p[q].next)
			if (q < S.n0 && vw_find(V, q) == q && !S.cnt[q])
				stk[ns++] = q;
	while (ns) {
		uint32_t q = stk[--ns], op = V->p[q].in.op, eff = vir_optab[op].eff;

		if (vw_find(V, q) != q || S.cnt[q] || V->p[q].blk == VW_NOBLK)
			continue;
		if (op == VO_LOAD ? (V->p[q].in.fl & VIF_VOL) != 0 :
		    ((eff & ~(EF_UF | EF_F)) != EF_P || op == VO_BPARAM || op == VO_PARAM))	/* (FP status flags are not kept: no FENV_ACCESS) */
			continue;
		VW_EACHOP(V, q, o, {
			uint32_t x = vw_find(V, *o);

			vw_dec(V, &S, x);
			if (x < S.n0 && V->p[x].blk != VW_NOBLK && !S.cnt[x] && V->p[x].in.op != VO_BPARAM)
				stk[ns++] = x;
		});
		V->fwd[q] = vw_undefk(e, &S, V->p[q].in.ty);
	}
	free(S.ht), free(S.log), free(S.egen), free(S.kt);
	free(S.idom), free(S.dkid), free(S.dnext);
	free(S.seq), free(S.wr), free(S.ri), free(S.bend), free(S.linner), free(S.lstart), free(S.lend), free(S.lpar), free(S.lmerged);
	free(S.cnt);
	free(stk);
}
