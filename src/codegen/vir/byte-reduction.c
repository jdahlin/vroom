/* Pack the complete 16-byte chunks of a short, fixed-trip unsigned byte OR.
 * The scalar loop handles the remaining bytes and carries the original seed. */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "vectorize.h"
#include "simplify.h"

VPRIV bool virbuild_byte_or_reduce(virbuild_vectorize_state *S)
{
	codegen_context *e = S->context;
	virbuild_context *V = e->vir_builder;
	uint32_t h = S->header, pre = S->predecessor;
	vir_insn entry = V->insns[V->blocks[pre].terminator].insn;
	if (entry.op != VIR_OP_BR || entry.a != h)
		return false;
	vir_insn branch = V->insns[V->blocks[h].terminator].insn;
	uint32_t cond = virbuild_value_resolve(V, branch.a);
	vir_insn cmp = V->insns[cond].insn;
	if (cmp.op != VIR_OP_ICMP)
		return false;
	uint32_t index = virbuild_value_resolve(V, cmp.a);
	int64_t limit, initial;
	if (V->insns[index].insn.op != VIR_OP_BPARAM || V->insns[index].block != h ||
	    !virbuild_value_integer_constant(V, virbuild_value_resolve(V, cmp.b), &limit))
		return false;
	/* Normalize the common i < count and i <= last forms. */
	unsigned cc = cmp.flags & 15;
	if (branch.b == S->exit)
		cc = (unsigned[]){1,0,5,4,3,2,9,8,7,6}[cc];
	if ((cc != 2 && cc != 3 && cc != 6 && cc != 7) || limit < 0 || limit > 256)
		return false;
	unsigned count = (unsigned)limit + (cc == 3 || cc == 7);
	if (count < 16 || count > 256)
		return false;

	uint32_t acc = 0;
	for (uint32_t p = V->blocks[h].first_parameter; p; p = V->insns[p].next) {
		if (V->forward[p] != p || p == index)
			continue;
		if (acc || V->insns[p].insn.type != VIR_TYPE_I32)
			return false;
		acc = p;
	}
	if (!acc)
		return false;
	uint32_t ie = V->insns[index].insn.b, ae = V->insns[acc].insn.b;
	if (!virbuild_value_integer_constant(V, virbuild_value_resolve(V, V->extra[ie + S->predecessor_index]), &initial) || initial)
		return false;
	uint32_t step = virbuild_value_resolve(V, V->extra[ie + S->latch_index]);
	uint32_t update = virbuild_value_resolve(V, V->extra[ae + S->latch_index]);
	vir_insn add = V->insns[step].insn, merge = V->insns[update].insn;
	if (add.op != VIR_OP_ADD || virbuild_value_resolve(V, add.a) != index ||
	    !virbuild_value_integer_constant(V, virbuild_value_resolve(V, add.b), &initial) || initial != 1 ||
	    merge.op != VIR_OP_OR)
		return false;
	uint32_t left = virbuild_value_resolve(V, merge.a), right = virbuild_value_resolve(V, merge.b);
	uint32_t load = left == acc ? right : left;
	if ((left != acc && right != acc) || V->insns[load].insn.op != VIR_OP_LOAD ||
	    (V->insns[load].insn.flags & (VIR_INSN_FLAG_VOL | VIR_INSN_FLAG_MS | 3)))
		return false;
	uint32_t addr = virbuild_value_resolve(V, V->insns[load].insn.a);
	vir_insn ptr = V->insns[addr].insn;
	if (ptr.op != VIR_OP_PTRADD || virbuild_value_resolve(V, ptr.b) != index)
		return false;
	uint32_t base = virbuild_value_resolve(V, ptr.a);
	if (base >= S->value_count || (V->insns[base].block != VIRBUILD_BLOCK_NONE && S->in_loop[V->insns[base].block]))
		return false;
	/* Only the header test may execute on the final (empty) iteration.
	 * All loop-body operations must belong to the matched expression. */
	for (uint32_t k = 0; k < S->loop_block_count; k++) {
		uint32_t b = S->loop_blocks[k];
		for (uint32_t q = V->blocks[b].first_insn; q; q = V->insns[q].next) {
			if (virbuild_vectorize_insn_is_skipped(V, q))
				continue;
			if (b == h ? q != cond : (q != step && q != addr && q != load && q != update))
				return false;
		}
	}
	if (V->insns[load].block == VIRBUILD_BLOCK_NONE || !S->in_loop[V->insns[load].block])
		return false;
	uint32_t packed = 0;
	for (unsigned off = 0; off + 16 <= count; off += 16) {
		uint32_t p = base;
		if (off)
			p = virbuild_block_insn_append(e, pre, VIR_OP_PTRADD, VIR_TYPE_PTR, 0, base,
						       virbuild_constant_integer(e, VIR_TYPE_I64, off), 0);
		uint32_t pair = virbuild_block_insn_append(e, pre, VIR_OP_LOAD, VIR_TYPE_I64X2, 3, p, 0, 0);
		packed = packed ? virbuild_block_insn_append(e, pre, VIR_OP_OR, VIR_TYPE_I64X2, 0, packed, pair, 0) : pair;
	}
	uint32_t lo = virbuild_block_insn_append(e, pre, VIR_OP_VEXTRACT, VIR_TYPE_I64, 0, packed, 0, 0);
	uint32_t hi = virbuild_block_insn_append(e, pre, VIR_OP_VEXTRACT, VIR_TYPE_I64, 0, packed, 1, 0);
	uint32_t bits = virbuild_block_insn_append(e, pre, VIR_OP_OR, VIR_TYPE_I64, 0, lo, hi, 0);
	for (unsigned shift = 32; shift >= 8; shift /= 2) {
		uint32_t hi = virbuild_block_insn_append(e, pre, VIR_OP_LSHR, VIR_TYPE_I64, 0, bits,
							virbuild_constant_integer(e, VIR_TYPE_I64, shift), 0);
		bits = virbuild_block_insn_append(e, pre, VIR_OP_OR, VIR_TYPE_I64, 0, bits, hi, 0);
	}
	uint32_t byte = virbuild_block_insn_append(e, pre, VIR_OP_EXT, VIR_TYPE_I32, 0, bits, 0, 0);
	uint32_t seed = V->extra[ae + S->predecessor_index];
	V->extra[ae + S->predecessor_index] = virbuild_block_insn_append(e, pre, VIR_OP_OR, VIR_TYPE_I32, 0, seed, byte, 0);
	V->extra[ie + S->predecessor_index] = virbuild_constant_integer(e, V->insns[index].insn.type, count & ~15u);
	return true;
}
