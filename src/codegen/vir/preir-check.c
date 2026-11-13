/*
 * The pre-IR's invariants, checked between passes (-fvir-passes=verify, pass-table.c).
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
	virbuild_context *context;
	uint8_t *is_live;		/* per value: a live instruction or parameter of a block in V->order */
	uint8_t *in_order;	/* per block: in V->order */
	char *buf;
	size_t count;
	const char *bad;
} virbuild_preir_checker;

static void virbuild_preir_check_fail(virbuild_preir_checker *k, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void virbuild_preir_check_fail(virbuild_preir_checker *k, const char *fmt, ...)
{
	va_list ap;

	if (k->bad)
		return;
	va_start(ap, fmt);
	vsnprintf(k->buf, k->count, fmt, ap);
	va_end(ap);
	k->bad = k->buf;
}

/* v forwarded to its end without changing fwd (virbuild_value_resolve compresses paths); 0 on a cycle */
static uint32_t virbuild_preir_check_value_find(const virbuild_context *V, uint32_t v)
{
	for (uint32_t steps = 0; steps <= V->insn_count; steps++) {
		if (V->forward[v] >= V->insn_count)
			return 0;
		if (V->forward[v] == v)
			return v;
		v = V->forward[v];
	}
	return 0;
}

static void virbuild_preir_check_operand(virbuild_preir_checker *k, uint32_t q, uint32_t o)
{
	virbuild_context *V = k->context;
	uint32_t r;

	if (o == 0 || o >= V->insn_count) {
		virbuild_preir_check_fail(k, "v%u (%s) has the operand v%u, %s", q, vir_opcode_table[V->insns[q].insn.op].name, o,
			      o ? "past the last value" : "which is none");
		return;
	}
	r = virbuild_preir_check_value_find(V, o);
	if (!r) {
		virbuild_preir_check_fail(k, "v%u (%s): its operand v%u forwards in a cycle or out of range", q, vir_opcode_table[V->insns[q].insn.op].name, o);
		return;
	}
	if (V->insns[r].block != VIRBUILD_BLOCK_NONE && !k->is_live[r])
		virbuild_preir_check_fail(k, "v%u (%s) reads v%u (forwarded from v%u, %s in b%u), which is not live", q,
			      vir_opcode_table[V->insns[q].insn.op].name, r, o, vir_opcode_table[V->insns[r].insn.op].name, V->insns[r].block);
}

/* the operands of live instruction q (VIRBUILD_FOR_EACH_OPERAND's: a parameter's need the predecessors) */
static void virbuild_preir_check_operands(virbuild_preir_checker *k, uint32_t q)
{
	virbuild_context *V = k->context;

	if (V->insns[q].insn.op == VIR_OP_BPARAM && V->insns[q].insn.b != VIRBUILD_BLOCK_NONE &&
	    (uint64_t)V->insns[q].insn.b + V->blocks[V->insns[q].block].predecessor_count > V->extra_count) {
		virbuild_preir_check_fail(k, "parameter v%u of b%u: its operand list lies outside the extra words", q, V->insns[q].block);
		return;
	}
	VIRBUILD_FOR_EACH_OPERAND(V, q, o, virbuild_preir_check_operand(k, q, *o));
}

/* one block's lists: members in range, of this block, not constants, each once */
static void virbuild_preir_check_lists(virbuild_preir_checker *k, uint32_t b, uint8_t *seen)
{
	virbuild_context *V = k->context;
	const virbuild_preir_block *B = &V->blocks[b];
	uint32_t steps = 0;

	for (int list = 0; list < 2; list++)
		for (uint32_t q = list ? B->first_parameter : B->first_insn; q && !k->bad; q = V->insns[q].next) {
			if (q >= V->insn_count) {
				virbuild_preir_check_fail(k, "b%u: its %s list holds v%u, past the last value", b, list ? "parameter" : "instruction", q);
				return;
			}
			if (seen[q]++ || ++steps > V->insn_count) {
				virbuild_preir_check_fail(k, "b%u: v%u is in a block list twice", b, q);
				return;
			}
			if (V->insns[q].block != b)
				virbuild_preir_check_fail(k, "b%u: its %s list holds v%u (%s), which says it is in %s%u", b, list ? "parameter" : "instruction",
					      q, vir_opcode_table[V->insns[q].insn.op].name, V->insns[q].block == VIRBUILD_BLOCK_NONE ? "no block, a constant," : "b",
					      V->insns[q].block == VIRBUILD_BLOCK_NONE ? 0 : V->insns[q].block);
			else if ((V->insns[q].insn.op == VIR_OP_BPARAM) != (list == 1))
				virbuild_preir_check_fail(k, "b%u: v%u (%s) is in its %s list", b, q, vir_opcode_table[V->insns[q].insn.op].name,
					      list ? "parameter" : "instruction");
		}
	if (B->terminator && !k->bad) {
		if (B->terminator >= V->insn_count)
			virbuild_preir_check_fail(k, "b%u: its terminator v%u is past the last value", b, B->terminator);
		else if (V->insns[B->terminator].block != b || !vir_opcode_is_terminator(V->insns[B->terminator].insn.op))
			virbuild_preir_check_fail(k, "b%u: its terminator v%u is %s in b%u", b, B->terminator, vir_opcode_table[V->insns[B->terminator].insn.op].name,
				      V->insns[B->terminator].block);
	}
}

static bool virbuild_preir_check_successors_are_ok(virbuild_preir_checker *k, uint32_t b)
{
	virbuild_context *V = k->context;
	uint32_t n = virbuild_block_successor_count(V, b);

	for (uint32_t j = 0; j < n; j++)
		if (*virbuild_block_successor_pointer(V, b, j) >= V->block_count) {
			virbuild_preir_check_fail(k, "b%u: successor %u is b%u, past the last block", b, j, *virbuild_block_successor_pointer(V, b, j));
			return false;
		}
	return true;
}

static void virbuild_preir_check_order(virbuild_preir_checker *k)
{
	virbuild_context *V = k->context;
	uint8_t *reach = calloc((size_t)V->block_count + 1, 1);
	uint32_t *stk = malloc(((size_t)V->block_count + 1) * sizeof *stk), sp = 0;

	if (!reach || !stk) {
		virbuild_preir_check_fail(k, "out of memory in the check");
		goto out;
	}
	for (uint32_t i = 0; i < V->block_order_count && !k->bad; i++) {
		uint32_t b = V->block_order[i];

		if (b >= V->block_count)
			virbuild_preir_check_fail(k, "V->order[%u] is b%u, past the last block", i, b);
		else if (k->in_order[b]++)
			virbuild_preir_check_fail(k, "b%u is in V->order twice", b);
		else if (!V->blocks[b].terminator)
			virbuild_preir_check_fail(k, "b%u is in V->order and has no terminator", b);
	}
	if (k->bad || !V->block_count)
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
	for (uint32_t i = 0; i < V->block_order_count && !k->bad; i++)
		if (!reach[V->block_order[i]])
			virbuild_preir_check_fail(k, "b%u is in V->order but not reachable from b0", V->block_order[i]);
out:
	free(reach), free(stk);
}

static void virbuild_preir_check_predecessors(virbuild_preir_checker *k)
{
	virbuild_context *V = k->context;

	for (uint32_t i = 0; i < V->block_order_count && !k->bad; i++) {
		uint32_t b = V->block_order[i];
		const virbuild_preir_block *B = &V->blocks[b];

		if ((uint64_t)B->predecessor_first + B->predecessor_count > V->predecessor_capacity) {
			virbuild_preir_check_fail(k, "b%u: its predecessor list lies outside V->pr", b);
			return;
		}
		for (uint32_t j = 0; j < B->predecessor_count && !k->bad; j++) {
			uint32_t p = V->predecessors[B->predecessor_first + j], n, found = 0;

			for (uint32_t l = 0; l < j; l++)
				if (V->predecessors[B->predecessor_first + l] == p)
					virbuild_preir_check_fail(k, "b%u: predecessor b%u is listed twice", b, p);
			if (p >= V->block_count || !k->in_order[p]) {
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
	for (uint32_t i = 0; i < V->block_order_count && !k->bad; i++) {
		uint32_t b = V->block_order[i], n;

		if (!virbuild_preir_check_successors_are_ok(k, b))
			return;
		n = virbuild_block_successor_count(V, b);
		for (uint32_t s = 0; s < n && !k->bad; s++) {
			uint32_t t = *virbuild_block_successor_pointer(V, b, s);
			bool found = false;

			for (uint32_t j = 0; j < V->blocks[t].predecessor_count; j++)
				found |= V->predecessors[V->blocks[t].predecessor_first + j] == b;
			if (!found)
				virbuild_preir_check_fail(k, "b%u branches to b%u, which does not list it as a predecessor", b, t);
		}
	}
}

/* the live values of the blocks in V->order, then the operands of each */
static void virbuild_preir_check_values(virbuild_preir_checker *k, uint32_t valid)
{
	virbuild_context *V = k->context;

	if (V->forward_capacity < V->insn_count) {
		virbuild_preir_check_fail(k, "%u values but %u forwarding entries after sealing", V->insn_count, V->forward_capacity);
		return;
	}
	for (uint32_t i = 0; i < V->insn_count && !k->bad; i++)
		if (V->forward[i] >= V->insn_count)
			virbuild_preir_check_fail(k, "v%u forwards to v%u, past the last value", i, V->forward[i]);
	if (k->bad)
		return;
	for (uint32_t i = 0; i < V->block_order_count; i++) {
		uint32_t b = V->block_order[i];

		for (uint32_t p = V->blocks[b].first_parameter; p; p = V->insns[p].next)
			k->is_live[p] = V->forward[p] == p;
		for (uint32_t q = V->blocks[b].first_insn; q; q = V->insns[q].next)
			k->is_live[q] = virbuild_preir_check_value_find(V, q) == q;
		if (V->blocks[b].terminator)
			k->is_live[V->blocks[b].terminator] = 1;
	}
	for (uint32_t i = 0; i < V->block_order_count && !k->bad; i++) {
		uint32_t b = V->block_order[i];

		if (valid & VIRBUILD_FACT_PREDS)	/* (a parameter's operands are counted by the predecessors) */
			for (uint32_t p = V->blocks[b].first_parameter; p && !k->bad; p = V->insns[p].next)
				if (k->is_live[p]) {
					if (V->insns[p].insn.b == VIRBUILD_BLOCK_NONE)
						virbuild_preir_check_fail(k, "parameter v%u of b%u has no operands after sealing", p, b);
					else
						virbuild_preir_check_operands(k, p);
				}
		for (uint32_t q = V->blocks[b].first_insn; q && !k->bad; q = V->insns[q].next)
			if (k->is_live[q] && !virbuild_promote_store_is_dead(V, q))
				virbuild_preir_check_operands(k, q);
		if (V->blocks[b].terminator && !k->bad)
			virbuild_preir_check_operands(k, V->blocks[b].terminator);
	}
}

VPRIV const char *virbuild_preir_check(ecx *e, uint32_t valid, char *buf, size_t n)
{
	virbuild_context *V = e->vw;
	virbuild_preir_checker k = { .context = V, .buf = buf, .count = n };
	uint8_t *seen;

	if (!V->insn_count) {
		snprintf(buf, n, "no values (value 0, none, is missing)");
		return buf;
	}
	k.is_live = calloc((size_t)V->insn_count + 1, 1);
	k.in_order = calloc((size_t)V->block_count + 1, 1);
	seen = calloc((size_t)V->insn_count + 1, 1);
	if (!k.is_live || !k.in_order || !seen) {
		virbuild_preir_check_fail(&k, "out of memory in the check");
		goto out;
	}
	for (uint32_t i = 1; i < V->insn_count && !k.bad; i++)
		if (vir_opcode_is_constant(V->insns[i].insn.op) && V->insns[i].block != VIRBUILD_BLOCK_NONE)
			virbuild_preir_check_fail(&k, "constant v%u (%s) says it is in b%u", i, vir_opcode_table[V->insns[i].insn.op].name, V->insns[i].block);
	if (valid & VIRBUILD_FACT_ORDER) {
		virbuild_preir_check_order(&k);
		for (uint32_t i = 0; i < V->block_order_count && !k.bad; i++)
			virbuild_preir_check_lists(&k, V->block_order[i], seen);
	} else
		for (uint32_t b = 0; b < V->block_count && !k.bad; b++)
			virbuild_preir_check_lists(&k, b, seen);
	if (!k.bad && (valid & VIRBUILD_FACT_ORDER) && (valid & VIRBUILD_FACT_PREDS))
		virbuild_preir_check_predecessors(&k);
	if (!k.bad && (valid & VIRBUILD_FACT_ORDER) && V->sealing)
		virbuild_preir_check_values(&k, valid);
out:
	free(k.is_live), free(k.in_order), free(seen);
	return k.bad;
}
