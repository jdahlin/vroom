/* Forward the fields of a freshly initialized record to its copy destination.
 * Keep the original writes until slot promotion proves the local is private. */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "simplify.h"

typedef struct {
	uint32_t zero, fields[4], count;
} virbuild_record_init;

/* Overwrite the copy at its original program point. All field values have
 * already been evaluated, including loads that might alias the destination. */
static void virbuild_record_copy_replace(codegen_context *e, uint32_t b, uint32_t q,
					 const virbuild_record_init *init)
{
	virbuild_context *V = e->vir_builder;
	vir_insn copy = V->insns[q].insn;
	uint32_t next = V->insns[q].next, last = q;
	V->insns[q].insn = (vir_insn){ VIR_OP_MEMSET, VIR_TYPE_VOID, 0, copy.a, V->insns[init->zero].insn.b, copy.c };
	for (uint32_t j = 0; j < init->count; j++) {
		vir_insn store = V->insns[init->fields[j]].insn;
		uint32_t off = V->insns[store.a].insn.b, addr = copy.a;
		vir_insn dst = V->insns[copy.a].insn;
		if (off && dst.op == VIR_OP_SLOTADDR) {
			uint64_t base = (uint64_t)dst.c << 32 | dst.b;
			addr = virbuild_slot_address(e, dst.a, (int64_t)(base + off));
		} else if (off) {
			uint32_t displacement = virbuild_constant_integer(e, VIR_TYPE_I64, off);
			addr = virbuild_preir_insn_add(e, VIR_OP_PTRADD, VIR_TYPE_PTR, 0, addr, displacement, 0, b);
			V->insns[last].next = addr;
			last = addr;
		}
		/* Only the width transfers. The destination can be unaligned. */
		uint32_t field = virbuild_preir_insn_add(e, VIR_OP_STORE, VIR_TYPE_VOID,
			VIR_INSN_FLAG_WIDTH_LOG2(store.flags), addr, store.b, 0, b);
		V->insns[last].next = field;
		last = field;
	}
	V->insns[last].next = next;
	if (V->blocks[b].last_insn == q)
		V->blocks[b].last_insn = last;
}

VPRIV bool virbuild_aggregate_copies_forward(codegen_context *e)
{
	virbuild_context *V = e->vir_builder;
	virbuild_record_init init = { 0 };
	uint32_t slot = 0, successor = VIRBUILD_BLOCK_NONE;
	int64_t size = 0;
	bool changed = false;
	for (uint32_t i = 0; i < V->block_order_count; i++) {
		uint32_t b = V->block_order[i];
		if (b != successor || V->blocks[b].predecessor_count != 1)
			init.zero = 0;
		for (uint32_t q = V->blocks[b].first_insn; q; q = V->insns[q].next) {
			if (virbuild_value_resolve(V, q) != q || virbuild_promote_store_is_dead(V, q))
				continue;
			vir_insn in = V->insns[q].insn;
			int64_t k;
			if (init.zero && in.op == VIR_OP_MEMCPY && !in.flags &&
			    in.b == V->insns[init.zero].insn.a &&
			    virbuild_value_integer_constant(V, in.c, &k) && k == size) {
				virbuild_record_copy_replace(e, b, q, &init);
				changed = true;
				in = V->insns[q].insn;
			}
			if (in.op == VIR_OP_MEMSET && !in.flags && V->insns[in.a].insn.op == VIR_OP_SLOTADDR &&
			    !V->insns[in.a].insn.b && !V->insns[in.a].insn.c &&
			    virbuild_value_integer_constant(V, in.b, &k) && k == 0 &&
			    virbuild_value_integer_constant(V, in.c, &k) && k > 0 && k <= 128) {
				slot = V->insns[in.a].insn.a;
				size = k;
				init.zero = q;
				init.count = 0;
			} else if (init.zero && in.op == VIR_OP_STORE && !(in.flags & VIR_INSN_FLAG_VOL) &&
				   !vir_type_is_vector(V->insns[in.b].insn.type) &&
				   V->insns[in.a].insn.op == VIR_OP_SLOTADDR && V->insns[in.a].insn.a == slot &&
				   !V->insns[in.a].insn.c && (int64_t)V->insns[in.a].insn.b +
				   (1 << VIR_INSN_FLAG_WIDTH_LOG2(in.flags)) <= size && init.count < 4) {
				init.fields[init.count++] = q;
			} else if (vir_opcode_table[in.op].effects & (VIR_EFFECT_W | VIR_EFFECT_C | VIR_EFFECT_A)) {
				init.zero = 0;
			}
		}
		const vir_insn *term = &V->insns[V->blocks[b].terminator].insn;
		successor = term->op == VIR_OP_BR ? term->a : VIRBUILD_BLOCK_NONE;
	}
	return changed;
}
