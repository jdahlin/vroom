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
	const vir_insn *in = &V->insns[c].insn;
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
		const virbuild_preir_insn *x = &V->insns[v];
		uint32_t r;

		if (x->block != b || cnt[v] != 1)
			return false;
		if (x->insn.op == VIR_OP_ICMP)
			return !virbuild_condition_is_zero_test(V, v);
		if ((x->insn.op != VIR_OP_AND && x->insn.op != VIR_OP_OR) || x->insn.type != VIR_TYPE_I32)
			return false;
		r = virbuild_value_resolve(V, x->insn.b);
		if (V->insns[r].insn.op != VIR_OP_ICMP || V->insns[r].block != b || cnt[r] != 1 || virbuild_condition_is_zero_test(V, r))
			return false;
		v = virbuild_value_resolve(V, x->insn.a);
	}
}

VPRIV bool virbuild_conditions_merge(ecx *e)
{
	virbuild_context *V = e->vw;
	uint32_t ncnt = V->insn_count + V->block_order_count + 1, *cnt = calloc(ncnt, sizeof *cnt);
	bool any = false, again = true;

	if (!cnt)
		virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
	for (uint32_t i = 0; i < V->block_order_count; i++) {
		uint32_t b = V->block_order[i];

		for (uint32_t p = V->blocks[b].first_parameter; p; p = V->insns[p].next)
			if (V->forward[p] == p)
				VIRBUILD_FOR_EACH_OPERAND(V, p, o, cnt[virbuild_value_resolve(V, *o)]++);
		for (uint32_t q = V->blocks[b].first_insn; q; q = V->insns[q].next)
			if (virbuild_value_resolve(V, q) == q && !virbuild_promote_store_is_dead(V, q))
				VIRBUILD_FOR_EACH_OPERAND(V, q, o, cnt[virbuild_value_resolve(V, *o)]++);
		if (V->blocks[b].terminator)
			VIRBUILD_FOR_EACH_OPERAND(V, V->blocks[b].terminator, o, cnt[virbuild_value_resolve(V, *o)]++);
	}
	while (again) {
		again = false;
		for (uint32_t i = 0; i < V->block_order_count; i++) {
			static const uint8_t inv[10] = { 1, 0, 5, 4, 3, 2, 9, 8, 7, 6 };
			uint32_t b = V->block_order[i], t = V->blocks[b].terminator, S, O, X, ts, c1, c2, nlive = 0, ib, is, ix, n, prev;
			bool onfalse, flip;

			if (!V->blocks[b].is_reachable || !t || V->insns[t].insn.op != VIR_OP_BRIF || V->insns[t].insn.b == V->insns[t].insn.c)
				continue;
			c1 = virbuild_value_resolve(V, V->insns[t].insn.a);
			for (int side = 0; side < 2; side++) {
				S = side ? V->insns[t].insn.c : V->insns[t].insn.b, O = side ? V->insns[t].insn.b : V->insns[t].insn.c;
				onfalse = side;
				ts = V->blocks[S].terminator;
				if (S == b || O == b || !V->blocks[S].is_reachable || V->blocks[S].predecessor_count != 1 || V->predecessors[V->blocks[S].predecessor_first] != b || !ts ||
				    V->insns[ts].insn.op != VIR_OP_BRIF || V->insns[ts].insn.b == V->insns[ts].insn.c)
					continue;
				if (V->insns[ts].insn.b == O)
					X = V->insns[ts].insn.c, flip = !onfalse;	/* (and: S goes on when c2 fails; or: O when c2 holds) */
				else if (V->insns[ts].insn.c == O)
					X = V->insns[ts].insn.b, flip = onfalse;
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
						uint32_t jx = ex ? V->insns[V->blocks[X].terminator].insn.a : VIRBUILD_BLOCK_NONE;
						const virbuild_preir_block *ko = &V->blocks[O];

						bool eo = ko->terminator && V->insns[ko->terminator].insn.op == VIR_OP_BR && ko->predecessor_count == 2;

						for (uint32_t p = ko->first_parameter; p && eo; p = V->insns[p].next)
							eo = V->forward[p] != p;
						for (uint32_t q = ko->first_insn; q && eo; q = V->insns[q].next)
							eo = virbuild_value_resolve(V, q) != q || virbuild_promote_store_is_dead(V, q) ||
							     (V->insns[q].insn.op == VIR_OP_MEMSET && (V->insns[q].insn.flags & VIRBUILD_MEMSET_LIFETIME_FLAG));
						if (!ex || (jx != O && !(eo && V->insns[ko->terminator].insn.a == jx)))
							continue;
					}
				}
				c2 = virbuild_value_resolve(V, V->insns[ts].insn.a);
				nlive = 0;
				for (uint32_t p = V->blocks[S].first_parameter; p; p = V->insns[p].next)
					nlive += V->forward[p] == p;
				for (uint32_t q = V->blocks[S].first_insn; q; q = V->insns[q].next)
					if (virbuild_value_resolve(V, q) == q && !virbuild_promote_store_is_dead(V, q) &&
					    !(V->insns[q].insn.op == VIR_OP_MEMSET && (V->insns[q].insn.flags & VIRBUILD_MEMSET_LIFETIME_FLAG)))
						nlive += q == c2 ? 0 : 1;
				if (nlive || V->insns[c2].insn.op != VIR_OP_ICMP || V->insns[c2].block != S || cnt[c2] != 1 || virbuild_condition_is_zero_test(V, c2) ||
				    !virbuild_condition_is_chainable(V, c1, cnt, b))
					continue;
				/* c1 is b's last instruction, or a compare that can move there */
				if (V->blocks[b].last_insn != c1 && V->insns[c1].insn.op != VIR_OP_ICMP)
					continue;
				ib = virbuild_block_predecessor_index(V, O, b), is = virbuild_block_predecessor_index(V, O, S), ix = virbuild_block_predecessor_index(V, X, S);
				if (ib == VIRBUILD_BLOCK_NONE || is == VIRBUILD_BLOCK_NONE || ix == VIRBUILD_BLOCK_NONE || ib == is)
					continue;
				n = 0;
				for (uint32_t p = V->blocks[O].first_parameter; p; p = V->insns[p].next)
					if (V->forward[p] == p && (V->insns[p].insn.b == VIRBUILD_BLOCK_NONE ||
							       virbuild_value_resolve(V, V->extra[V->insns[p].insn.b + ib]) != virbuild_value_resolve(V, V->extra[V->insns[p].insn.b + is])))
						n++;
				if (n)
					continue;
				/* c1 to b's end, c2 after it, then the and/or */
				if (V->blocks[b].last_insn != c1) {
					prev = 0;
					for (uint32_t q = V->blocks[b].first_insn; q != c1; q = V->insns[q].next)
						prev = q;
					if (prev)
						V->insns[prev].next = V->insns[c1].next;
					else
						V->blocks[b].first_insn = V->insns[c1].next;
					V->insns[V->blocks[b].last_insn].next = c1;
					V->insns[c1].next = 0;
					V->blocks[b].last_insn = c1;
				}
				prev = 0;
				for (uint32_t q = V->blocks[S].first_insn; q != c2; q = V->insns[q].next)
					prev = q;
				if (prev)
					V->insns[prev].next = V->insns[c2].next;
				else
					V->blocks[S].first_insn = V->insns[c2].next;
				if (V->blocks[S].last_insn == c2)
					V->blocks[S].last_insn = prev;
				V->insns[c2].block = b, V->insns[c2].next = 0;
				V->insns[V->blocks[b].last_insn].next = c2;
				V->blocks[b].last_insn = c2;
				if (flip)
					V->insns[c2].insn.flags = (uint16_t)((V->insns[c2].insn.flags & ~15u) | inv[V->insns[c2].insn.flags & 15]);
				n = virbuild_block_insn_append(e, b, onfalse ? VIR_OP_OR : VIR_OP_AND, VIR_TYPE_I32, 0, c1, c2, 0);
				if (n >= ncnt)
					virbuild_fail(e, VIRBUILD_FAILURE(joined_conditions));
				V->insns[n].location = V->insns[t].location;
				cnt[n] = 1;
				V->insns[t].insn = onfalse ? (vir_insn){ VIR_OP_BRIF, VIR_TYPE_VOID, 0, n, O, X } : (vir_insn){ VIR_OP_BRIF, VIR_TYPE_VOID, 0, n, X, O };
				/* X is entered from b now; O's entry from S goes */
				V->predecessors[V->blocks[X].predecessor_first + ix] = b;
				for (uint32_t p = V->blocks[O].first_parameter; p; p = V->insns[p].next)
					if (V->insns[p].insn.b != VIRBUILD_BLOCK_NONE)
						for (uint32_t k = is; k + 1 < V->blocks[O].predecessor_count; k++)
							V->extra[V->insns[p].insn.b + k] = V->extra[V->insns[p].insn.b + k + 1];
				for (uint32_t k = is; k + 1 < V->blocks[O].predecessor_count; k++)
					V->predecessors[V->blocks[O].predecessor_first + k] = V->predecessors[V->blocks[O].predecessor_first + k + 1];
				V->blocks[O].predecessor_count--;
				V->blocks[S].is_reachable = 0;
				any = again = true;
				break;
			}
		}
	}
	free(cnt);
	if (any) {
		uint32_t m = 0;

		for (uint32_t i = 0; i < V->block_order_count; i++)
			if (V->blocks[V->block_order[i]].is_reachable)
				V->block_order[m++] = V->block_order[i];
		V->block_order_count = m;
	}
	return any;
}
