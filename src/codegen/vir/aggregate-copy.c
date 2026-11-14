/* Forward the fields of a freshly initialized record to its copy destination.
 * Keep the original writes until slot promotion proves the local is private. */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "simplify.h"
#include "lowering-driver.h"

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

/* ABI copies read whole words, including padding. When the word comes
 * from a real zero fill plus one low field, its value is already in SSA. */
static bool virbuild_record_word_forward(codegen_context *e, uint32_t q, uint32_t slot,
					int64_t size, const virbuild_record_init *init)
{
	virbuild_context *V = e->vir_builder;
	vir_insn load = V->insns[q].insn;
	vir_insn address = V->insns[load.a].insn;
	if (load.type != VIR_TYPE_I64 || VIR_INSN_FLAG_WIDTH_LOG2(load.flags) != 3 ||
	    (load.flags & VIR_INSN_FLAG_VOL) || address.op != VIR_OP_SLOTADDR ||
	    address.a != slot || address.c || (int64_t)address.b + 8 > size)
		return false;
	uint32_t field = 0, width = 0;
	for (uint32_t i = 0; i < init->count; i++) {
		vir_insn store = V->insns[init->fields[i]].insn;
		uint32_t off = V->insns[store.a].insn.b;
		uint32_t lg = VIR_INSN_FLAG_WIDTH_LOG2(store.flags);
		if (off >= address.b + 8 || off + (1u << lg) <= address.b)
			continue;
		if (off != address.b || (field && lg < width))
			return false;
		field = virbuild_value_resolve(V, store.b);
		width = lg;
	}
	if (!field) {
		uint32_t zero = virbuild_constant_integer(e, VIR_TYPE_I64, 0);
		V->forward[q] = zero;
		return true;
	}
	uint32_t type = V->insns[field].insn.type;
	if (width == 3 && type == VIR_TYPE_PTR)
		V->insns[q].insn = (vir_insn){ VIR_OP_P2I, VIR_TYPE_I64, 0, field, 0, 0 };
	else if (width == 3 && type == VIR_TYPE_I64)
		V->forward[q] = field;
	else if (width < 3 && (type == VIR_TYPE_I32 || type == VIR_TYPE_I64))
		V->insns[q].insn = (vir_insn){ VIR_OP_EXT, VIR_TYPE_I64, width, field, 0, 0 };
	else
		return false;
	return true;
}

/* A wrapper returning a callee's memory result can pass its own ABI
 * result buffer. Count uses by slot, including derived slot addresses and
 * call/edge arguments, so another reader cannot observe the removed copy. */
typedef struct { uint32_t count; bool escaped; } virbuild_copy_slot;

static virbuild_copy_slot *virbuild_slot_operand_counts(codegen_context *e)
{
	virbuild_context *V = e->vir_builder;
	virbuild_copy_slot *uses = calloc(V->slot_count, sizeof *uses);
	bool returns_twice = false;
	if (!uses)
		virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
	for (uint32_t q = 1; q < V->insn_count; q++) {
		if (V->insns[q].block == VIRBUILD_BLOCK_NONE || virbuild_value_resolve(V, q) != q ||
		    virbuild_promote_store_is_dead(V, q))
			continue;
		vir_insn *in = &V->insns[q].insn;
		if (in->op == VIR_OP_CALL && virbuild_call_returns_twice(e, &V->insns[virbuild_value_resolve(V, in->a)].insn))
			returns_twice = true;
		VIRBUILD_FOR_EACH_OPERAND(V, q, operand, {
			const vir_insn *value = &V->insns[virbuild_value_resolve(V, *operand)].insn;
			if (value->op == VIR_OP_SLOTADDR) {
				virbuild_copy_slot *slot = &uses[value->a];
				slot->count++;
				if (!(operand == &in->a && (in->op == VIR_OP_LOAD || in->op == VIR_OP_STORE ||
				      in->op == VIR_OP_MEMCPY || in->op == VIR_OP_MEMSET)) &&
				    !(operand == &in->b && in->op == VIR_OP_MEMCPY))
					slot->escaped = true;
			}
		});
	}
	/* A nonlocal exit could expose an incompletely assigned local. */
	if (returns_twice)
		for (uint32_t i = 0; i < V->slot_count; i++)
			uses[i].escaped = true;
	return uses;
}

static bool virbuild_record_call_forward(codegen_context *e, uint32_t call_id, uint32_t copy_id,
					virbuild_copy_slot **slot_uses)
{
	virbuild_context *V = e->vir_builder;
	vir_insn copy = V->insns[copy_id].insn, call = V->insns[call_id].insn;
	vir_insn dst = V->insns[copy.a].insn, src = V->insns[copy.b].insn;
	bool returning = dst.op == VIR_OP_PARAM && dst.a == VIR_GROUP_RESULT;
	if ((!returning && (dst.op != VIR_OP_SLOTADDR || dst.b || dst.c)) || src.op != VIR_OP_SLOTADDR ||
	    src.b || src.c || VIR_GROUP_KIND(V->extra[call.b + 1]) != VIR_GROUP_SRET ||
	    virbuild_value_resolve(V, V->extra[call.c + 1]) != copy.b)
		return false;
	uint32_t callee = V->extra[call.b + 2];
	int64_t bytes;
	if (!virbuild_value_integer_constant(V, copy.c, &bytes) || bytes != VIR_GROUP_SIZE(callee))
		return false;
	if (returning) {
		uint32_t result = V->extra[V->function_signature + 2];
		if (VIR_GROUP_SIZE(result) != bytes || VIR_GROUP_ALIGN_LOG2(result) < VIR_GROUP_ALIGN_LOG2(callee))
			return false;
	} else if (dst.a == src.a || V->slots[dst.a].size < (uint64_t)bytes ||
		   V->slots[dst.a].align < (1u << VIR_GROUP_ALIGN_LOG2(callee))) {
		return false;
	}
	if (!*slot_uses)
		*slot_uses = virbuild_slot_operand_counts(e);
	if ((*slot_uses)[src.a].count != 2 || (!returning && (*slot_uses)[dst.a].escaped))
		return false;
	if (!returning)
		(*slot_uses)[dst.a].escaped = true; /* now passed to a call */
	V->extra[call.c + 1] = copy.a;
	V->insns[copy_id].insn = (vir_insn){ VIR_OP_NOP, VIR_TYPE_VOID, 0, 0, 0, 0 };
	return true;
}

VPRIV bool virbuild_aggregate_copies_forward(codegen_context *e)
{
	virbuild_context *V = e->vir_builder;
	virbuild_record_init init = { 0 };
	uint32_t call = 0;
	virbuild_copy_slot *slot_uses = NULL;
	uint32_t slot = 0, successor = VIRBUILD_BLOCK_NONE;
	int64_t size = 0;
	bool changed = false;
	for (uint32_t i = 0; i < V->block_order_count; i++) {
		uint32_t b = V->block_order[i];
		if (b != successor || V->blocks[b].predecessor_count != 1) {
			init.zero = 0;
			call = 0;
		}
		for (uint32_t q = V->blocks[b].first_insn; q; q = V->insns[q].next) {
			if (virbuild_value_resolve(V, q) != q || virbuild_promote_store_is_dead(V, q))
				continue;
			vir_insn in = V->insns[q].insn;
			int64_t k;
			if (call && in.op == VIR_OP_MEMCPY && !in.flags &&
			    virbuild_record_call_forward(e, call, q, &slot_uses)) {
				changed = true;
				call = 0;
				continue;
			}
			call = in.op == VIR_OP_CALL ? q : 0;
			if (init.zero && in.op == VIR_OP_LOAD && virbuild_record_word_forward(e, q, slot, size, &init)) {
				changed = true;
				continue;
			}
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
	free(slot_uses);
	return changed;
}
