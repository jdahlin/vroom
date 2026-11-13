/* Two branches to a common destination become one. */
#define WK_PREFIX virbuild_
#define LO_PREFIX virbuild_
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
static bool virbuild_condition_is_zero_test(virbuild_context *V, uint32_t c)
{
	const vir_insn *in = &V->p[c].in;
	int cc = in->flags & 15;
	int64_t k;

	if (!virbuild_value_integer_constant(V, virbuild_value_resolve(V, in->b), &k))
		return false;
	return !k ? cc == 0 || cc == 1 || cc == 2 || cc == 5 || cc == 7 || cc == 8 : k == -1 && (cc == 3 || cc == 4);
}

/* v is a condition the lowering reads from the flags: a compare, or and/or of such a
 * condition and a compare; all in block b, each read once */
static bool virbuild_condition_is_chainable(virbuild_context *V, uint32_t v, const uint32_t *cnt, uint32_t b)
{
	for (;;) {
		const virbuild_preir_insn *x = &V->p[v];
		uint32_t r;

		if (x->blk != b || cnt[v] != 1)
			return false;
		if (x->in.op == VIR_OP_ICMP)
			return !virbuild_condition_is_zero_test(V, v);
		if ((x->in.op != VIR_OP_AND && x->in.op != VIR_OP_OR) || x->in.type != VIR_TYPE_I32)
			return false;
		r = virbuild_value_resolve(V, x->in.b);
		if (V->p[r].in.op != VIR_OP_ICMP || V->p[r].blk != b || cnt[r] != 1 || virbuild_condition_is_zero_test(V, r))
			return false;
		v = virbuild_value_resolve(V, x->in.a);
	}
}

VPRIV bool virbuild_conditions_merge(ecx *e)
{
	virbuild_context *V = e->vw;
	uint32_t ncnt = V->np + V->norder + 1, *cnt = calloc(ncnt, sizeof *cnt);
	bool any = false, again = true;

	if (!cnt)
		virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
	for (uint32_t i = 0; i < V->norder; i++) {
		uint32_t b = V->order[i];

		for (uint32_t p = V->b[b].phead; p; p = V->p[p].next)
			if (V->fwd[p] == p)
				VIRBUILD_FOR_EACH_OPERAND(V, p, o, cnt[virbuild_value_resolve(V, *o)]++);
		for (uint32_t q = V->b[b].head; q; q = V->p[q].next)
			if (virbuild_value_resolve(V, q) == q && !virbuild_promote_store_is_dead(V, q))
				VIRBUILD_FOR_EACH_OPERAND(V, q, o, cnt[virbuild_value_resolve(V, *o)]++);
		if (V->b[b].term)
			VIRBUILD_FOR_EACH_OPERAND(V, V->b[b].term, o, cnt[virbuild_value_resolve(V, *o)]++);
	}
	while (again) {
		again = false;
		for (uint32_t i = 0; i < V->norder; i++) {
			static const uint8_t inv[10] = { 1, 0, 5, 4, 3, 2, 9, 8, 7, 6 };
			uint32_t b = V->order[i], t = V->b[b].term, S, O, X, ts, c1, c2, nlive = 0, ib, is, ix, n, prev;
			bool onfalse, flip;

			if (!V->b[b].reach || !t || V->p[t].in.op != VIR_OP_BRIF || V->p[t].in.b == V->p[t].in.c)
				continue;
			c1 = virbuild_value_resolve(V, V->p[t].in.a);
			for (int side = 0; side < 2; side++) {
				S = side ? V->p[t].in.c : V->p[t].in.b, O = side ? V->p[t].in.b : V->p[t].in.c;
				onfalse = side;
				ts = V->b[S].term;
				if (S == b || O == b || !V->b[S].reach || V->b[S].npred != 1 || V->pr[V->b[S].pred] != b || !ts ||
				    V->p[ts].in.op != VIR_OP_BRIF || V->p[ts].in.b == V->p[ts].in.c)
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
					/* VCC_CCMP: `all` joins every such pair, `off` none.  The default joins only where
					 * the if-conversion can follow (X an empty arm into O, or both into one block: a
					 * value, not control flow).  Joining a plain branch pair costs the second compare
					 * whenever the first decided alone, and pays only when the branches mispredict:
					 * measured on sqlite, all: +0.4 % instructions, cycles unchanged (PLAN-vir.md). */
					static _Thread_local int mode = -1;
					uint32_t dn = 0;

					if (mode < 0) {
						const char *m = getenv("VCC_CCMP");

						mode = !m ? 1 : !strcmp(m, "off") ? 0 : !strcmp(m, "all") ? 2 : 1;
					}
					if (!mode)
						continue;
					if (mode == 1) {
						bool ex = virbuild_arm_is_hoistable(V, X, S, 0, &dn);
						uint32_t jx = ex ? V->p[V->b[X].term].in.a : VIRBUILD_BLOCK_NONE;
						const virbuild_preir_block *ko = &V->b[O];

						bool eo = ko->term && V->p[ko->term].in.op == VIR_OP_BR && ko->npred == 2;

						for (uint32_t p = ko->phead; p && eo; p = V->p[p].next)
							eo = V->fwd[p] != p;
						for (uint32_t q = ko->head; q && eo; q = V->p[q].next)
							eo = virbuild_value_resolve(V, q) != q || virbuild_promote_store_is_dead(V, q) ||
							     (V->p[q].in.op == VIR_OP_MEMSET && (V->p[q].in.flags & VIRBUILD_MEMSET_LIFETIME_FLAG));
						if (!ex || (jx != O && !(eo && V->p[ko->term].in.a == jx)))
							continue;
					}
				}
				c2 = virbuild_value_resolve(V, V->p[ts].in.a);
				nlive = 0;
				for (uint32_t p = V->b[S].phead; p; p = V->p[p].next)
					nlive += V->fwd[p] == p;
				for (uint32_t q = V->b[S].head; q; q = V->p[q].next)
					if (virbuild_value_resolve(V, q) == q && !virbuild_promote_store_is_dead(V, q) &&
					    !(V->p[q].in.op == VIR_OP_MEMSET && (V->p[q].in.flags & VIRBUILD_MEMSET_LIFETIME_FLAG)))
						nlive += q == c2 ? 0 : 1;
				if (nlive || V->p[c2].in.op != VIR_OP_ICMP || V->p[c2].blk != S || cnt[c2] != 1 || virbuild_condition_is_zero_test(V, c2) ||
				    !virbuild_condition_is_chainable(V, c1, cnt, b))
					continue;
				/* c1 is b's last instruction, or a compare that can move there */
				if (V->b[b].tail != c1 && V->p[c1].in.op != VIR_OP_ICMP)
					continue;
				ib = virbuild_block_predecessor_index(V, O, b), is = virbuild_block_predecessor_index(V, O, S), ix = virbuild_block_predecessor_index(V, X, S);
				if (ib == VIRBUILD_BLOCK_NONE || is == VIRBUILD_BLOCK_NONE || ix == VIRBUILD_BLOCK_NONE || ib == is)
					continue;
				n = 0;
				for (uint32_t p = V->b[O].phead; p; p = V->p[p].next)
					if (V->fwd[p] == p && (V->p[p].in.b == VIRBUILD_BLOCK_NONE ||
							       virbuild_value_resolve(V, V->x[V->p[p].in.b + ib]) != virbuild_value_resolve(V, V->x[V->p[p].in.b + is])))
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
					V->p[c2].in.flags = (uint16_t)((V->p[c2].in.flags & ~15u) | inv[V->p[c2].in.flags & 15]);
				n = virbuild_block_insn_append(e, b, onfalse ? VIR_OP_OR : VIR_OP_AND, VIR_TYPE_I32, 0, c1, c2, 0);
				if (n >= ncnt)
					virbuild_fail(e, VIRBUILD_FAILURE(joined_conditions));
				V->p[n].loc = V->p[t].loc;
				cnt[n] = 1;
				V->p[t].in = onfalse ? (vir_insn){ VIR_OP_BRIF, VIR_TYPE_VOID, 0, n, O, X } : (vir_insn){ VIR_OP_BRIF, VIR_TYPE_VOID, 0, n, X, O };
				/* X is entered from b now; O's entry from S goes */
				V->pr[V->b[X].pred + ix] = b;
				for (uint32_t p = V->b[O].phead; p; p = V->p[p].next)
					if (V->p[p].in.b != VIRBUILD_BLOCK_NONE)
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
