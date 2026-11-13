/* Two branches to a common destination become one. */
#define WK_PREFIX vw_
#define LO_PREFIX vw_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "simplify.h"

/*
 * Two branches to a common destination become one, as LLVM's SimplifyCFG
 * (FoldBranchToCommonDest): b ends in brif c1, S, O (or O, S) where S is b's alone,
 * holds one integer compare c2 and ends in brif c2 with O as one target.  Then b
 * computes c2 too and branches once on c1 and c2 (S on c1's true side) or c1 or c2
 * (false side), c2 inverted when O is S's true target; S goes.  O's parameters must
 * take the same values from b and from S.  The lowering turns the joined condition
 * into cmp + ccmp, so a compare that is a cbz or tbz on its own (against 0 or -1)
 * is left as its own branch.  Repeated: a && b && c is one chain.
 */
static bool vw_cbzlike(vwst *V, uint32_t c)
{
	const vir_ins *in = &V->p[c].in;
	int cc = in->fl & 15;
	int64_t k;

	if (!vw_kint(V, vw_find(V, in->b), &k))
		return false;
	return !k ? cc == 0 || cc == 1 || cc == 2 || cc == 5 || cc == 7 || cc == 8 : k == -1 && (cc == 3 || cc == 4);
}

/* v is a condition the lowering reads from the flags: a compare, or and/or of such a
 * condition and a compare; all in block b, each read once */
static bool vw_cchain(vwst *V, uint32_t v, const uint32_t *cnt, uint32_t b)
{
	for (;;) {
		const vwp *x = &V->p[v];
		uint32_t r;

		if (x->blk != b || cnt[v] != 1)
			return false;
		if (x->in.op == VO_ICMP)
			return !vw_cbzlike(V, v);
		if ((x->in.op != VO_AND && x->in.op != VO_OR) || x->in.ty != VT_I32)
			return false;
		r = vw_find(V, x->in.b);
		if (V->p[r].in.op != VO_ICMP || V->p[r].blk != b || cnt[r] != 1 || vw_cbzlike(V, r))
			return false;
		v = vw_find(V, x->in.a);
	}
}

VPRIV bool vw_condmerge(ecx *e)
{
	vwst *V = e->vw;
	uint32_t ncnt = V->np + V->norder + 1, *cnt = calloc(ncnt, sizeof *cnt);
	bool any = false, again = true;

	if (!cnt)
		vw_fail(e, VIRFAIL(out_of_memory));
	for (uint32_t i = 0; i < V->norder; i++) {
		uint32_t b = V->order[i];

		for (uint32_t p = V->b[b].phead; p; p = V->p[p].next)
			if (V->fwd[p] == p)
				VW_EACHOP(V, p, o, cnt[vw_find(V, *o)]++);
		for (uint32_t q = V->b[b].head; q; q = V->p[q].next)
			if (vw_find(V, q) == q && !vw_promdead(V, q))
				VW_EACHOP(V, q, o, cnt[vw_find(V, *o)]++);
		if (V->b[b].term)
			VW_EACHOP(V, V->b[b].term, o, cnt[vw_find(V, *o)]++);
	}
	while (again) {
		again = false;
		for (uint32_t i = 0; i < V->norder; i++) {
			static const uint8_t inv[10] = { 1, 0, 5, 4, 3, 2, 9, 8, 7, 6 };
			uint32_t b = V->order[i], t = V->b[b].term, S, O, X, ts, c1, c2, nlive = 0, ib, is, ix, n, prev;
			bool onfalse, flip;

			if (!V->b[b].reach || !t || V->p[t].in.op != VO_BRIF || V->p[t].in.b == V->p[t].in.c)
				continue;
			c1 = vw_find(V, V->p[t].in.a);
			for (int side = 0; side < 2; side++) {
				S = side ? V->p[t].in.c : V->p[t].in.b, O = side ? V->p[t].in.b : V->p[t].in.c;
				onfalse = side;
				ts = V->b[S].term;
				if (S == b || O == b || !V->b[S].reach || V->b[S].npred != 1 || V->pr[V->b[S].pred] != b || !ts ||
				    V->p[ts].in.op != VO_BRIF || V->p[ts].in.b == V->p[ts].in.c)
					continue;
				if (V->p[ts].in.b == O)
					X = V->p[ts].in.c, flip = !onfalse;	/* (and: S goes on when c2 fails; or: O when c2 holds) */
				else if (V->p[ts].in.c == O)
					X = V->p[ts].in.b, flip = onfalse;
				else
					continue;
				if (X == S || X == b)
					continue;
				{
					/* -fvir-ccmp=: `all` joins every such pair, `none` none.  The default (`select`) joins only where
					 * the if-conversion can follow (X an empty arm into O, or both into one block: a
					 * value, not control flow).  Joining a plain branch pair costs the second compare
					 * whenever the first decided alone, and pays only when the branches mispredict:
					 * measured on sqlite, all: +0.4 % instructions, cycles unchanged (PLAN-vir.md). */
					int mode = e->E->o->vir_aid.ccmp;
					uint32_t dn = 0;

					if (mode == CC_CCMP_NONE)
						continue;
					if (mode == CC_CCMP_SELECT) {
						bool ex = vw_armempty(V, X, S, 0, &dn);
						uint32_t jx = ex ? V->p[V->b[X].term].in.a : VW_NOBLK;
						const vwb *ko = &V->b[O];

						bool eo = ko->term && V->p[ko->term].in.op == VO_BR && ko->npred == 2;

						for (uint32_t p = ko->phead; p && eo; p = V->p[p].next)
							eo = V->fwd[p] != p;
						for (uint32_t q = ko->head; q && eo; q = V->p[q].next)
							eo = vw_find(V, q) != q || vw_promdead(V, q) ||
							     (V->p[q].in.op == VO_MEMSET && (V->p[q].in.fl & VW_LIFETIME));
						if (!ex || (jx != O && !(eo && V->p[ko->term].in.a == jx)))
							continue;
					}
				}
				c2 = vw_find(V, V->p[ts].in.a);
				nlive = 0;
				for (uint32_t p = V->b[S].phead; p; p = V->p[p].next)
					nlive += V->fwd[p] == p;
				for (uint32_t q = V->b[S].head; q; q = V->p[q].next)
					if (vw_find(V, q) == q && !vw_promdead(V, q) &&
					    !(V->p[q].in.op == VO_MEMSET && (V->p[q].in.fl & VW_LIFETIME)))
						nlive += q == c2 ? 0 : 1;
				if (nlive || V->p[c2].in.op != VO_ICMP || V->p[c2].blk != S || cnt[c2] != 1 || vw_cbzlike(V, c2) ||
				    !vw_cchain(V, c1, cnt, b))
					continue;
				/* c1 is b's last instruction, or a compare that can move there */
				if (V->b[b].tail != c1 && V->p[c1].in.op != VO_ICMP)
					continue;
				ib = vw_predix(V, O, b), is = vw_predix(V, O, S), ix = vw_predix(V, X, S);
				if (ib == VW_NOBLK || is == VW_NOBLK || ix == VW_NOBLK || ib == is)
					continue;
				n = 0;
				for (uint32_t p = V->b[O].phead; p; p = V->p[p].next)
					if (V->fwd[p] == p && (V->p[p].in.b == VW_NOBLK ||
							       vw_find(V, V->x[V->p[p].in.b + ib]) != vw_find(V, V->x[V->p[p].in.b + is])))
						n++;
				if (n)
					continue;
				/* c1 to b's end, c2 after it, then the and/or */
				if (V->b[b].tail != c1) {
					prev = 0;
					for (uint32_t q = V->b[b].head; q != c1; q = V->p[q].next)
						prev = q;
					if (prev)
						V->p[prev].next = V->p[c1].next;
					else
						V->b[b].head = V->p[c1].next;
					V->p[V->b[b].tail].next = c1;
					V->p[c1].next = 0;
					V->b[b].tail = c1;
				}
				prev = 0;
				for (uint32_t q = V->b[S].head; q != c2; q = V->p[q].next)
					prev = q;
				if (prev)
					V->p[prev].next = V->p[c2].next;
				else
					V->b[S].head = V->p[c2].next;
				if (V->b[S].tail == c2)
					V->b[S].tail = prev;
				V->p[c2].blk = b, V->p[c2].next = 0;
				V->p[V->b[b].tail].next = c2;
				V->b[b].tail = c2;
				if (flip)
					V->p[c2].in.fl = (uint16_t)((V->p[c2].in.fl & ~15u) | inv[V->p[c2].in.fl & 15]);
				n = vw_ins_in(e, b, onfalse ? VO_OR : VO_AND, VT_I32, 0, c1, c2, 0);
				if (n >= ncnt)
					vw_fail(e, VIRFAIL(joined_conditions));
				V->p[n].loc = V->p[t].loc;
				cnt[n] = 1;
				V->p[t].in = onfalse ? (vir_ins){ VO_BRIF, VT_VOID, 0, n, O, X } : (vir_ins){ VO_BRIF, VT_VOID, 0, n, X, O };
				/* X is entered from b now; O's entry from S goes */
				V->pr[V->b[X].pred + ix] = b;
				for (uint32_t p = V->b[O].phead; p; p = V->p[p].next)
					if (V->p[p].in.b != VW_NOBLK)
						for (uint32_t k = is; k + 1 < V->b[O].npred; k++)
							V->x[V->p[p].in.b + k] = V->x[V->p[p].in.b + k + 1];
				for (uint32_t k = is; k + 1 < V->b[O].npred; k++)
					V->pr[V->b[O].pred + k] = V->pr[V->b[O].pred + k + 1];
				V->b[O].npred--;
				V->b[S].reach = 0;
				any = again = true;
				break;
			}
		}
	}
	free(cnt);
	if (any) {
		uint32_t m = 0;

		for (uint32_t i = 0; i < V->norder; i++)
			if (V->b[V->order[i]].reach)
				V->order[m++] = V->order[i];
		V->norder = m;
	}
	return any;
}
