/*
 * The pre-IR's invariants, checked between passes (VCC_VIR_PASSES=verify, pass-table.c).
 *
 * Only the invariants written down today (preir.h; quality-vir-build.md section 2, as
 * doc/quality/WORK-vir.md step 2b lists them), each as far as the facts the driver
 * knows to be valid allow:
 *   value 0 is none: no live instruction's operand (a list ends at 0, so it is in none);
 *   a constant has blk == VIRBUILD_BLOCK_NONE and is in no list (a list member has its block's
 *   number: a parameter in the parameter list, the terminator is one);
 *   VIRBUILD_FACT_ORDER: V->order holds blocks reachable from block 0 only, each once, and each
 *   has a terminator;
 *   VIRBUILD_FACT_PREDS: a block's predecessors are distinct, in V->order, and are exactly the
 *   blocks of V->order that name it as a successor;
 *   from V->sealing on: every value has a fwd entry, forwarding ends (no cycle), and
 *   every operand of a live instruction is, after virbuild_value_resolve, a constant or a live value
 *   of a block in V->order; a live block parameter has its operand list (one per
 *   predecessor: npred words at in.b; the list's length is not stored, so this checks
 *   that it was made and lies inside V->x).
 * What it does not check because nothing writes it down: types, dominance of a use by
 * its definition (vir_verify checks that on VIR), that every reachable block is in
 * V->order.
 */
#define WK_PREFIX virbuild_
#define LO_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "pass-table.h"

typedef struct virbuild_preir_checker {
	virbuild_context *V;
	uint8_t *live;		/* per value: a live instruction or parameter of a block in V->order */
	uint8_t *inorder;	/* per block: in V->order */
	char *buf;
	size_t n;
	const char *bad;
} virbuild_preir_checker;

static void virbuild_preir_check_fail(virbuild_preir_checker *k, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void virbuild_preir_check_fail(virbuild_preir_checker *k, const char *fmt, ...)
{
	va_list ap;

	if (k->bad)
		return;
	va_start(ap, fmt);
	vsnprintf(k->buf, k->n, fmt, ap);
	va_end(ap);
	k->bad = k->buf;
}

/* v forwarded to its end without changing fwd (virbuild_value_resolve compresses paths); 0 on a cycle */
static uint32_t virbuild_preir_check_value_find(const virbuild_context *V, uint32_t v)
{
	for (uint32_t steps = 0; steps <= V->np; steps++) {
		if (V->fwd[v] >= V->np)
			return 0;
		if (V->fwd[v] == v)
			return v;
		v = V->fwd[v];
	}
	return 0;
}

static void virbuild_preir_check_operand(virbuild_preir_checker *k, uint32_t q, uint32_t o)
{
	virbuild_context *V = k->V;
	uint32_t r;

	if (o == 0 || o >= V->np) {
		virbuild_preir_check_fail(k, "v%u (%s) has the operand v%u, %s", q, vir_opcode_table[V->p[q].in.op].name, o,
			      o ? "past the last value" : "which is none");
		return;
	}
	r = virbuild_preir_check_value_find(V, o);
	if (!r) {
		virbuild_preir_check_fail(k, "v%u (%s): its operand v%u forwards in a cycle or out of range", q, vir_opcode_table[V->p[q].in.op].name, o);
		return;
	}
	if (V->p[r].blk != VIRBUILD_BLOCK_NONE && !k->live[r])
		virbuild_preir_check_fail(k, "v%u (%s) reads v%u (forwarded from v%u, %s in b%u), which is not live", q,
			      vir_opcode_table[V->p[q].in.op].name, r, o, vir_opcode_table[V->p[r].in.op].name, V->p[r].blk);
}

/* the operands of live instruction q (VIRBUILD_FOR_EACH_OPERAND's: a parameter's need the predecessors) */
static void virbuild_preir_check_operands(virbuild_preir_checker *k, uint32_t q)
{
	virbuild_context *V = k->V;

	if (V->p[q].in.op == VIR_OP_BPARAM && V->p[q].in.b != VIRBUILD_BLOCK_NONE &&
	    (uint64_t)V->p[q].in.b + V->b[V->p[q].blk].npred > V->nx) {
		virbuild_preir_check_fail(k, "parameter v%u of b%u: its operand list lies outside the extra words", q, V->p[q].blk);
		return;
	}
	VIRBUILD_FOR_EACH_OPERAND(V, q, o, virbuild_preir_check_operand(k, q, *o));
}

/* one block's lists: members in range, of this block, not constants, each once */
static void virbuild_preir_check_lists(virbuild_preir_checker *k, uint32_t b, uint8_t *seen)
{
	virbuild_context *V = k->V;
	const virbuild_preir_block *B = &V->b[b];
	uint32_t steps = 0;

	for (int list = 0; list < 2; list++)
		for (uint32_t q = list ? B->phead : B->head; q && !k->bad; q = V->p[q].next) {
			if (q >= V->np) {
				virbuild_preir_check_fail(k, "b%u: its %s list holds v%u, past the last value", b, list ? "parameter" : "instruction", q);
				return;
			}
			if (seen[q]++ || ++steps > V->np) {
				virbuild_preir_check_fail(k, "b%u: v%u is in a block list twice", b, q);
				return;
			}
			if (V->p[q].blk != b)
				virbuild_preir_check_fail(k, "b%u: its %s list holds v%u (%s), which says it is in %s%u", b, list ? "parameter" : "instruction",
					      q, vir_opcode_table[V->p[q].in.op].name, V->p[q].blk == VIRBUILD_BLOCK_NONE ? "no block, a constant," : "b",
					      V->p[q].blk == VIRBUILD_BLOCK_NONE ? 0 : V->p[q].blk);
			else if ((V->p[q].in.op == VIR_OP_BPARAM) != (list == 1))
				virbuild_preir_check_fail(k, "b%u: v%u (%s) is in its %s list", b, q, vir_opcode_table[V->p[q].in.op].name,
					      list ? "parameter" : "instruction");
		}
	if (B->term && !k->bad) {
		if (B->term >= V->np)
			virbuild_preir_check_fail(k, "b%u: its terminator v%u is past the last value", b, B->term);
		else if (V->p[B->term].blk != b || !vir_opcode_is_terminator(V->p[B->term].in.op))
			virbuild_preir_check_fail(k, "b%u: its terminator v%u is %s in b%u", b, B->term, vir_opcode_table[V->p[B->term].in.op].name,
				      V->p[B->term].blk);
	}
}

static bool virbuild_preir_check_successors_are_ok(virbuild_preir_checker *k, uint32_t b)
{
	virbuild_context *V = k->V;
	uint32_t n = virbuild_block_successor_count(V, b);

	for (uint32_t j = 0; j < n; j++)
		if (*virbuild_block_successor_pointer(V, b, j) >= V->nb) {
			virbuild_preir_check_fail(k, "b%u: successor %u is b%u, past the last block", b, j, *virbuild_block_successor_pointer(V, b, j));
			return false;
		}
	return true;
}

static void virbuild_preir_check_order(virbuild_preir_checker *k)
{
	virbuild_context *V = k->V;
	uint8_t *reach = calloc((size_t)V->nb + 1, 1);
	uint32_t *stk = malloc(((size_t)V->nb + 1) * sizeof *stk), sp = 0;

	if (!reach || !stk) {
		virbuild_preir_check_fail(k, "out of memory in the check");
		goto out;
	}
	for (uint32_t i = 0; i < V->norder && !k->bad; i++) {
		uint32_t b = V->order[i];

		if (b >= V->nb)
			virbuild_preir_check_fail(k, "V->order[%u] is b%u, past the last block", i, b);
		else if (k->inorder[b]++)
			virbuild_preir_check_fail(k, "b%u is in V->order twice", b);
		else if (!V->b[b].term)
			virbuild_preir_check_fail(k, "b%u is in V->order and has no terminator", b);
	}
	if (k->bad || !V->nb)
		goto out;
	reach[0] = 1, stk[sp++] = 0;
	while (sp && !k->bad) {
		uint32_t b = stk[--sp], n;

		if (!virbuild_preir_check_successors_are_ok(k, b))
			break;
		n = virbuild_block_successor_count(V, b);
		for (uint32_t j = 0; j < n; j++) {
			uint32_t t = *virbuild_block_successor_pointer(V, b, j);

			if (!reach[t])
				reach[t] = 1, stk[sp++] = t;
		}
	}
	for (uint32_t i = 0; i < V->norder && !k->bad; i++)
		if (!reach[V->order[i]])
			virbuild_preir_check_fail(k, "b%u is in V->order but not reachable from b0", V->order[i]);
out:
	free(reach), free(stk);
}

static void virbuild_preir_check_predecessors(virbuild_preir_checker *k)
{
	virbuild_context *V = k->V;

	for (uint32_t i = 0; i < V->norder && !k->bad; i++) {
		uint32_t b = V->order[i];
		const virbuild_preir_block *B = &V->b[b];

		if ((uint64_t)B->pred + B->npred > V->cpr) {
			virbuild_preir_check_fail(k, "b%u: its predecessor list lies outside V->pr", b);
			return;
		}
		for (uint32_t j = 0; j < B->npred && !k->bad; j++) {
			uint32_t p = V->pr[B->pred + j], n, found = 0;

			for (uint32_t l = 0; l < j; l++)
				if (V->pr[B->pred + l] == p)
					virbuild_preir_check_fail(k, "b%u: predecessor b%u is listed twice", b, p);
			if (p >= V->nb || !k->inorder[p]) {
				virbuild_preir_check_fail(k, "b%u: predecessor b%u is not in V->order", b, p);
				break;
			}
			n = virbuild_block_successor_count(V, p);
			for (uint32_t s = 0; s < n; s++)
				found |= *virbuild_block_successor_pointer(V, p, s) == b;
			if (!found)
				virbuild_preir_check_fail(k, "b%u: predecessor b%u does not branch to it", b, p);
		}
	}
	/* every edge from a block of V->order is in its target's list */
	for (uint32_t i = 0; i < V->norder && !k->bad; i++) {
		uint32_t b = V->order[i], n;

		if (!virbuild_preir_check_successors_are_ok(k, b))
			return;
		n = virbuild_block_successor_count(V, b);
		for (uint32_t s = 0; s < n && !k->bad; s++) {
			uint32_t t = *virbuild_block_successor_pointer(V, b, s);
			bool found = false;

			for (uint32_t j = 0; j < V->b[t].npred; j++)
				found |= V->pr[V->b[t].pred + j] == b;
			if (!found)
				virbuild_preir_check_fail(k, "b%u branches to b%u, which does not list it as a predecessor", b, t);
		}
	}
}

/* the live values of the blocks in V->order, then the operands of each */
static void virbuild_preir_check_values(virbuild_preir_checker *k, uint32_t valid)
{
	virbuild_context *V = k->V;

	if (V->cfwd < V->np) {
		virbuild_preir_check_fail(k, "%u values but %u forwarding entries after sealing", V->np, V->cfwd);
		return;
	}
	for (uint32_t i = 0; i < V->np && !k->bad; i++)
		if (V->fwd[i] >= V->np)
			virbuild_preir_check_fail(k, "v%u forwards to v%u, past the last value", i, V->fwd[i]);
	if (k->bad)
		return;
	for (uint32_t i = 0; i < V->norder; i++) {
		uint32_t b = V->order[i];

		for (uint32_t p = V->b[b].phead; p; p = V->p[p].next)
			k->live[p] = V->fwd[p] == p;
		for (uint32_t q = V->b[b].head; q; q = V->p[q].next)
			k->live[q] = virbuild_preir_check_value_find(V, q) == q;
		if (V->b[b].term)
			k->live[V->b[b].term] = 1;
	}
	for (uint32_t i = 0; i < V->norder && !k->bad; i++) {
		uint32_t b = V->order[i];

		if (valid & VIRBUILD_FACT_PREDS)	/* (a parameter's operands are counted by the predecessors) */
			for (uint32_t p = V->b[b].phead; p && !k->bad; p = V->p[p].next)
				if (k->live[p]) {
					if (V->p[p].in.b == VIRBUILD_BLOCK_NONE)
						virbuild_preir_check_fail(k, "parameter v%u of b%u has no operands after sealing", p, b);
					else
						virbuild_preir_check_operands(k, p);
				}
		for (uint32_t q = V->b[b].head; q && !k->bad; q = V->p[q].next)
			if (k->live[q] && !virbuild_promote_store_is_dead(V, q))
				virbuild_preir_check_operands(k, q);
		if (V->b[b].term && !k->bad)
			virbuild_preir_check_operands(k, V->b[b].term);
	}
}

VPRIV const char *virbuild_preir_check(ecx *e, uint32_t valid, char *buf, size_t n)
{
	virbuild_context *V = e->vw;
	virbuild_preir_checker k = { .V = V, .buf = buf, .n = n };
	uint8_t *seen;

	if (!V->np) {
		snprintf(buf, n, "no values (value 0, none, is missing)");
		return buf;
	}
	k.live = calloc((size_t)V->np + 1, 1);
	k.inorder = calloc((size_t)V->nb + 1, 1);
	seen = calloc((size_t)V->np + 1, 1);
	if (!k.live || !k.inorder || !seen) {
		virbuild_preir_check_fail(&k, "out of memory in the check");
		goto out;
	}
	for (uint32_t i = 1; i < V->np && !k.bad; i++)
		if (vir_opcode_is_constant(V->p[i].in.op) && V->p[i].blk != VIRBUILD_BLOCK_NONE)
			virbuild_preir_check_fail(&k, "constant v%u (%s) says it is in b%u", i, vir_opcode_table[V->p[i].in.op].name, V->p[i].blk);
	if (valid & VIRBUILD_FACT_ORDER) {
		virbuild_preir_check_order(&k);
		for (uint32_t i = 0; i < V->norder && !k.bad; i++)
			virbuild_preir_check_lists(&k, V->order[i], seen);
	} else
		for (uint32_t b = 0; b < V->nb && !k.bad; b++)
			virbuild_preir_check_lists(&k, b, seen);
	if (!k.bad && (valid & VIRBUILD_FACT_ORDER) && (valid & VIRBUILD_FACT_PREDS))
		virbuild_preir_check_predecessors(&k);
	if (!k.bad && (valid & VIRBUILD_FACT_ORDER) && V->sealing)
		virbuild_preir_check_values(&k, valid);
out:
	free(k.live), free(k.inorder), free(seen);
	return k.bad;
}
