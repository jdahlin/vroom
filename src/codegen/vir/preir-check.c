/*
 * The pre-IR's invariants, checked between passes (-fvir-passes=verify, pass-table.c).
 *
 * Only the invariants written down today (preir.h; quality-vir-build.md section 2, as
 * doc/quality/WORK-vir.md step 2b lists them), each as far as the facts the driver
 * knows to be valid allow:
 *   value 0 is none: no live instruction's operand (a list ends at 0, so it is in none);
 *   a constant has blk == VW_NOBLK and is in no list (a list member has its block's
 *   number: a parameter in the parameter list, the terminator is one);
 *   PIR_ORDER: V->order holds blocks reachable from block 0 only, each once, and each
 *   has a terminator;
 *   PIR_PREDS: a block's predecessors are distinct, in V->order, and are exactly the
 *   blocks of V->order that name it as a successor;
 *   from V->sealing on: every value has a fwd entry, forwarding ends (no cycle), and
 *   every operand of a live instruction is, after vw_find, a constant or a live value
 *   of a block in V->order; a live block parameter has its operand list (one per
 *   predecessor: npred words at in.b; the list's length is not stored, so this checks
 *   that it was made and lies inside V->x).
 * What it does not check because nothing writes it down: types, dominance of a use by
 * its definition (vir_verify checks that on VIR), that every reachable block is in
 * V->order.
 */
#define WK_PREFIX vw_
#define LO_PREFIX vw_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"
#include "pass-table.h"

typedef struct vw_check {
	vwst *V;
	uint8_t *live;		/* per value: a live instruction or parameter of a block in V->order */
	uint8_t *inorder;	/* per block: in V->order */
	char *buf;
	size_t n;
	const char *bad;
} vw_check;

static void vw_check_fail(vw_check *k, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void vw_check_fail(vw_check *k, const char *fmt, ...)
{
	va_list ap;

	if (k->bad)
		return;
	va_start(ap, fmt);
	vsnprintf(k->buf, k->n, fmt, ap);
	va_end(ap);
	k->bad = k->buf;
}

/* v forwarded to its end without changing fwd (vw_find compresses paths); 0 on a cycle */
static uint32_t vw_check_find(const vwst *V, uint32_t v)
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

static void vw_check_operand(vw_check *k, uint32_t q, uint32_t o)
{
	vwst *V = k->V;
	uint32_t r;

	if (o == 0 || o >= V->np) {
		vw_check_fail(k, "v%u (%s) has the operand v%u, %s", q, vir_optab[V->p[q].in.op].name, o,
			      o ? "past the last value" : "which is none");
		return;
	}
	r = vw_check_find(V, o);
	if (!r) {
		vw_check_fail(k, "v%u (%s): its operand v%u forwards in a cycle or out of range", q, vir_optab[V->p[q].in.op].name, o);
		return;
	}
	if (V->p[r].blk != VW_NOBLK && !k->live[r])
		vw_check_fail(k, "v%u (%s) reads v%u (forwarded from v%u, %s in b%u), which is not live", q,
			      vir_optab[V->p[q].in.op].name, r, o, vir_optab[V->p[r].in.op].name, V->p[r].blk);
}

/* the operands of live instruction q (VW_EACHOP's: a parameter's need the predecessors) */
static void vw_check_operands(vw_check *k, uint32_t q)
{
	vwst *V = k->V;

	if (V->p[q].in.op == VO_BPARAM && V->p[q].in.b != VW_NOBLK &&
	    (uint64_t)V->p[q].in.b + V->b[V->p[q].blk].npred > V->nx) {
		vw_check_fail(k, "parameter v%u of b%u: its operand list lies outside the extra words", q, V->p[q].blk);
		return;
	}
	VW_EACHOP(V, q, o, vw_check_operand(k, q, *o));
}

/* one block's lists: members in range, of this block, not constants, each once */
static void vw_check_lists(vw_check *k, uint32_t b, uint8_t *seen)
{
	vwst *V = k->V;
	const vwb *B = &V->b[b];
	uint32_t steps = 0;

	for (int list = 0; list < 2; list++)
		for (uint32_t q = list ? B->phead : B->head; q && !k->bad; q = V->p[q].next) {
			if (q >= V->np) {
				vw_check_fail(k, "b%u: its %s list holds v%u, past the last value", b, list ? "parameter" : "instruction", q);
				return;
			}
			if (seen[q]++ || ++steps > V->np) {
				vw_check_fail(k, "b%u: v%u is in a block list twice", b, q);
				return;
			}
			if (V->p[q].blk != b)
				vw_check_fail(k, "b%u: its %s list holds v%u (%s), which says it is in %s%u", b, list ? "parameter" : "instruction",
					      q, vir_optab[V->p[q].in.op].name, V->p[q].blk == VW_NOBLK ? "no block, a constant," : "b",
					      V->p[q].blk == VW_NOBLK ? 0 : V->p[q].blk);
			else if ((V->p[q].in.op == VO_BPARAM) != (list == 1))
				vw_check_fail(k, "b%u: v%u (%s) is in its %s list", b, q, vir_optab[V->p[q].in.op].name,
					      list ? "parameter" : "instruction");
		}
	if (B->term && !k->bad) {
		if (B->term >= V->np)
			vw_check_fail(k, "b%u: its terminator v%u is past the last value", b, B->term);
		else if (V->p[B->term].blk != b || !vir_is_term(V->p[B->term].in.op))
			vw_check_fail(k, "b%u: its terminator v%u is %s in b%u", b, B->term, vir_optab[V->p[B->term].in.op].name,
				      V->p[B->term].blk);
	}
}

static bool vw_check_succ_ok(vw_check *k, uint32_t b)
{
	vwst *V = k->V;
	uint32_t n = vw_nsucc(V, b);

	for (uint32_t j = 0; j < n; j++)
		if (*vw_succp(V, b, j) >= V->nb) {
			vw_check_fail(k, "b%u: successor %u is b%u, past the last block", b, j, *vw_succp(V, b, j));
			return false;
		}
	return true;
}

static void vw_check_order(vw_check *k)
{
	vwst *V = k->V;
	uint8_t *reach = calloc((size_t)V->nb + 1, 1);
	uint32_t *stk = malloc(((size_t)V->nb + 1) * sizeof *stk), sp = 0;

	if (!reach || !stk) {
		vw_check_fail(k, "out of memory in the check");
		goto out;
	}
	for (uint32_t i = 0; i < V->norder && !k->bad; i++) {
		uint32_t b = V->order[i];

		if (b >= V->nb)
			vw_check_fail(k, "V->order[%u] is b%u, past the last block", i, b);
		else if (k->inorder[b]++)
			vw_check_fail(k, "b%u is in V->order twice", b);
		else if (!V->b[b].term)
			vw_check_fail(k, "b%u is in V->order and has no terminator", b);
	}
	if (k->bad || !V->nb)
		goto out;
	reach[0] = 1, stk[sp++] = 0;
	while (sp && !k->bad) {
		uint32_t b = stk[--sp], n;

		if (!vw_check_succ_ok(k, b))
			break;
		n = vw_nsucc(V, b);
		for (uint32_t j = 0; j < n; j++) {
			uint32_t t = *vw_succp(V, b, j);

			if (!reach[t])
				reach[t] = 1, stk[sp++] = t;
		}
	}
	for (uint32_t i = 0; i < V->norder && !k->bad; i++)
		if (!reach[V->order[i]])
			vw_check_fail(k, "b%u is in V->order but not reachable from b0", V->order[i]);
out:
	free(reach), free(stk);
}

static void vw_check_preds(vw_check *k)
{
	vwst *V = k->V;

	for (uint32_t i = 0; i < V->norder && !k->bad; i++) {
		uint32_t b = V->order[i];
		const vwb *B = &V->b[b];

		if ((uint64_t)B->pred + B->npred > V->cpr) {
			vw_check_fail(k, "b%u: its predecessor list lies outside V->pr", b);
			return;
		}
		for (uint32_t j = 0; j < B->npred && !k->bad; j++) {
			uint32_t p = V->pr[B->pred + j], n, found = 0;

			for (uint32_t l = 0; l < j; l++)
				if (V->pr[B->pred + l] == p)
					vw_check_fail(k, "b%u: predecessor b%u is listed twice", b, p);
			if (p >= V->nb || !k->inorder[p]) {
				vw_check_fail(k, "b%u: predecessor b%u is not in V->order", b, p);
				break;
			}
			n = vw_nsucc(V, p);
			for (uint32_t s = 0; s < n; s++)
				found |= *vw_succp(V, p, s) == b;
			if (!found)
				vw_check_fail(k, "b%u: predecessor b%u does not branch to it", b, p);
		}
	}
	/* every edge from a block of V->order is in its target's list */
	for (uint32_t i = 0; i < V->norder && !k->bad; i++) {
		uint32_t b = V->order[i], n;

		if (!vw_check_succ_ok(k, b))
			return;
		n = vw_nsucc(V, b);
		for (uint32_t s = 0; s < n && !k->bad; s++) {
			uint32_t t = *vw_succp(V, b, s);
			bool found = false;

			for (uint32_t j = 0; j < V->b[t].npred; j++)
				found |= V->pr[V->b[t].pred + j] == b;
			if (!found)
				vw_check_fail(k, "b%u branches to b%u, which does not list it as a predecessor", b, t);
		}
	}
}

/* the live values of the blocks in V->order, then the operands of each */
static void vw_check_values(vw_check *k, uint32_t valid)
{
	vwst *V = k->V;

	if (V->cfwd < V->np) {
		vw_check_fail(k, "%u values but %u forwarding entries after sealing", V->np, V->cfwd);
		return;
	}
	for (uint32_t i = 0; i < V->np && !k->bad; i++)
		if (V->fwd[i] >= V->np)
			vw_check_fail(k, "v%u forwards to v%u, past the last value", i, V->fwd[i]);
	if (k->bad)
		return;
	for (uint32_t i = 0; i < V->norder; i++) {
		uint32_t b = V->order[i];

		for (uint32_t p = V->b[b].phead; p; p = V->p[p].next)
			k->live[p] = V->fwd[p] == p;
		for (uint32_t q = V->b[b].head; q; q = V->p[q].next)
			k->live[q] = vw_check_find(V, q) == q;
		if (V->b[b].term)
			k->live[V->b[b].term] = 1;
	}
	for (uint32_t i = 0; i < V->norder && !k->bad; i++) {
		uint32_t b = V->order[i];

		if (valid & PIR_PREDS)	/* (a parameter's operands are counted by the predecessors) */
			for (uint32_t p = V->b[b].phead; p && !k->bad; p = V->p[p].next)
				if (k->live[p]) {
					if (V->p[p].in.b == VW_NOBLK)
						vw_check_fail(k, "parameter v%u of b%u has no operands after sealing", p, b);
					else
						vw_check_operands(k, p);
				}
		for (uint32_t q = V->b[b].head; q && !k->bad; q = V->p[q].next)
			if (k->live[q] && !vw_promdead(V, q))
				vw_check_operands(k, q);
		if (V->b[b].term && !k->bad)
			vw_check_operands(k, V->b[b].term);
	}
}

VPRIV const char *vw_preir_check(ecx *e, uint32_t valid, char *buf, size_t n)
{
	vwst *V = e->vw;
	vw_check k = { .V = V, .buf = buf, .n = n };
	uint8_t *seen;

	if (!V->np) {
		snprintf(buf, n, "no values (value 0, none, is missing)");
		return buf;
	}
	k.live = calloc((size_t)V->np + 1, 1);
	k.inorder = calloc((size_t)V->nb + 1, 1);
	seen = calloc((size_t)V->np + 1, 1);
	if (!k.live || !k.inorder || !seen) {
		vw_check_fail(&k, "out of memory in the check");
		goto out;
	}
	for (uint32_t i = 1; i < V->np && !k.bad; i++)
		if (vir_is_const_op(V->p[i].in.op) && V->p[i].blk != VW_NOBLK)
			vw_check_fail(&k, "constant v%u (%s) says it is in b%u", i, vir_optab[V->p[i].in.op].name, V->p[i].blk);
	if (valid & PIR_ORDER) {
		vw_check_order(&k);
		for (uint32_t i = 0; i < V->norder && !k.bad; i++)
			vw_check_lists(&k, V->order[i], seen);
	} else
		for (uint32_t b = 0; b < V->nb && !k.bad; b++)
			vw_check_lists(&k, b, seen);
	if (!k.bad && (valid & PIR_ORDER) && (valid & PIR_PREDS))
		vw_check_preds(&k);
	if (!k.bad && (valid & PIR_ORDER) && V->sealing)
		vw_check_values(&k, valid);
out:
	free(k.live), free(k.inorder), free(seen);
	return k.bad;
}
