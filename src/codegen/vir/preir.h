/*
 * The VIR builder: the walker's lowering interface (codegen/walk/proto.h) as a
 * virtual target, virbuild_ (SPEC-vir.md section 19, PLAN-vir.md V2).  The walker
 * is compiled a second time against it (codegen/vir/walker-instance.c); what it builds is
 * a function in VIR, handed to vir_canon and from there to the allocator and
 * a real target's lowering (codegen/vir/a64.c).
 *
 * The walker's registers are names (X9): 0-191 integer, 192-254 float, 255
 * the result.  A name, and every local the walker gives us (LO(local), X3),
 * is a variable; reads and writes go through Braun et al.'s construction
 * ("Simple and Efficient Construction of SSA Form", CC 2013) on a pre-IR of
 * our own: every block is unsealed until the walk is done, a read with no
 * definition in its block makes a placeholder parameter, and at the end the
 * blocks are sealed in order and trivial parameters removed.  Then the
 * function is emitted into VIR in reverse postorder.
 *
 * A value in a name has the type of the operation that made it.  An i32
 * value stands for a register whose upper half is zero (the walker's x86
 * model: a 32-bit operation zero-extends), so widening one is a zero
 * extension; signed narrow loads give i64.  Pointers are ptr; arithmetic on
 * them is ptradd.
 *
 * Frame slots (X2): every hand-out of a frame offset makes a slot object
 * that owns the range until the next hand-out over it.  Per block a table
 * keeps the value last stored to each slot range (X4): a load from it,
 * including a call's argument words, gets that value.  Stores to a slot
 * whose address never escapes and that no load reads any more are dropped.
 *
 * What the builder does not handle makes the function fall back to the
 * direct emitter (X8): virbuild_fail, and efatal while e->vtry is set, longjmp to
 * the walker's bail; virbuild_function_try then reports false.
 */

/* The VIR builder's state (codegen/vir/): one per ecx, reused by every function. */
#ifndef VCC_CODEGEN_VIR_PREIR_H
#define VCC_CODEGEN_VIR_PREIR_H
#include "vir/vir.h"
#include "vir/register-allocation.h"

typedef struct virbuild_preir_insn {	/* a pre-IR instruction: operands are pre-IR values, extras in virbuild_context.x */
	vir_insn insn;
	uint32_t block;	/* its block; VIRBUILD_BLOCK_NONE for a constant */
	uint32_t next;	/* the next one in the block's list (0: last) */
	uint32_t location;	/* source location */
	uint32_t slot_plus_one;	/* load, store: its frame slot + 1, 0 none */
} virbuild_preir_insn;

typedef struct virbuild_preir_block {
	uint32_t first_insn, last_insn;	/* instructions, in order */
	uint32_t first_parameter, last_parameter;	/* parameters (Braun's phis) */
	uint32_t terminator;		/* terminator (0: open) */
	uint32_t predecessor_first, predecessor_count;	/* predecessors: virbuild_context.pr[pred..pred+npred), distinct */
	uint32_t final_block;		/* final block id, VIRBUILD_BLOCK_NONE unreachable */
	uint8_t falls_through, is_reachable;
	uint8_t has_definition;		/* a variable is written in it: not empty, whatever its instructions */
} virbuild_preir_block;

typedef struct virbuild_variable_entry { uint64_t key; uint32_t value, generation; } virbuild_variable_entry;	/* (block, variable) -> current definition */

typedef struct virbuild_preir_slot {
	int64_t off;		/* the walker's frame offset */
	uint64_t size;
	uint32_t align;
	uint8_t exposed;	/* its address escaped (laddr) */
	uint32_t loads;		/* loads the store table did not answer (during the walk) */
	uint8_t used;		/* a reachable load reads it (emission) */
	uint32_t final_slot;		/* VIR slot id, VIRBUILD_BLOCK_NONE none yet */
	uint8_t is_promoted;		/* promoted to variables (virbuild_slots_promote): its stores go */
	uint8_t float32_written;		/* an 8-byte slot whose last float stored was an f32 (its low 4 bytes) */
} virbuild_preir_slot;

#define VIRBUILD_MEMSET_LIFETIME_FLAG 0x8000u	/* a memset's flag: the start of a local's lifetime (virbuild_undecl) */
typedef struct virbuild_store_entry { uint32_t slot; int64_t offset; int size; uint32_t v; } virbuild_store_entry;	/* store table */
typedef struct virbuild_location { uint32_t symbol, instance; } virbuild_location;

typedef struct virbuild_context {
	virbuild_preir_insn *insns; uint32_t insn_count, insn_capacity;
	virbuild_preir_block *blocks; uint32_t block_count, block_capacity;
	uint32_t *extra; uint32_t extra_count, extra_capacity;		/* extra words (pre-IR values) */
	virbuild_variable_entry *variable_entries; uint32_t variable_entry_count, variable_entry_capacity, variable_generation, variable_entry_allocated;	/* an entry of another gen is empty (no clearing per function);
						 * hcap: the part in use, small again at each function */
	uint32_t *local_hash, local_hash_capacity, local_hash_allocated;		/* (symbol, inlining instance) -> local + 1 */
	uint32_t *forward; uint32_t forward_capacity;		/* removed parameters: the value that replaces them */
	uint32_t *labels; uint32_t label_capacity, label_high;		/* walker label -> block */
	virbuild_preir_slot *slots; uint32_t slot_count, slot_capacity;
	uint32_t *active_slots; uint32_t active_slot_count, active_slot_capacity;	/* slots that own their range now */
	virbuild_store_entry *stores; uint32_t store_count, store_capacity;
	virbuild_location *locations; uint32_t location_count, location_capacity;
	uint32_t promoted_count;				/* variables of the promoted fields so far (after the locals') */
	uint8_t *variable_types; uint32_t variable_type_capacity;		/* per local: its VIR type */
	uint8_t *variable_is_signed; uint32_t variable_is_signed_capacity;		/* ... and whether it is a signed integer */
	uint32_t *predecessors; uint32_t predecessor_count, predecessor_capacity;	/* predecessor lists */
	uint32_t *value_map; uint32_t value_map_capacity;		/* pre-IR value -> VIR value */
	uint32_t *block_order; uint32_t block_order_count, block_order_capacity;	/* blocks in emission order */
	uint32_t *block_map; uint32_t block_map_capacity;		/* scratch */
	uint64_t live_names[3], live_float_names;		/* names in use */
	uint32_t float_products[64];			/* per float name: the product (or its negation) the expression in hand just made, 0 none */
	uint32_t current_block;				/* the block being built */
	bool sealing;
	uint32_t result_type;				/* the result's type, VIR_TYPE_VOID none */
	uint32_t parameter_count, function_signature;			/* the function's signature (pre extra offset) */
	const char *failure_reason;
	char failure_detail[96];			/* what a failure reason leaves out (an opcode, the allocator's message): VCC_VIR_STATS */
	vir_function f;				/* the function built (valid until the thread's next vir_function_begin) */
	int64_t *lowering_slots, *lowering_spills;
	uint8_t *lowering_uses; uint32_t lowering_uses_capacity;
	uint32_t *lowering_edge_labels, lowering_edge_label_capacity;
	uint32_t *lowering_layout, lowering_layout_capacity;
	uint32_t *lowering_labels, lowering_slot_capacity, lowering_spill_capacity, lowering_label_capacity; /* x86 lowering buffers, reused per ecx */
	vir_loops loops;
	vir_alloc_out allocation;
} virbuild_context;

#define VIRBUILD_REGISTER_NONE 255
#define VIRBUILD_FLOAT_REGISTER_FIRST 192	/* float names 192..254 */
#define VIRBUILD_RESULT_VARIABLE 255	/* the result variable */
#define VIRBUILD_LOCAL_VARIABLE_FIRST 256	/* locals: VIRBUILD_LOCAL_VARIABLE_FIRST + k */
#define VIRBUILD_BLOCK_NONE 0xffffffffu

#define VIRBUILD_ARRAY_GROW(p, n, cap, k) do { \
	if ((uint64_t)(n) + (k) > (cap)) { \
		size_t nc_ = (cap) ? (size_t)(cap) * 2 : 256; \
		while (nc_ < (uint64_t)(n) + (k)) nc_ *= 2; \
		void *q_ = realloc((p), nc_ * sizeof *(p)); \
		if (!q_) virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory)); \
		(p) = q_; (cap) = (uint32_t)nc_; \
	} } while (0)

#define VIRBUILD_VARIABLE_TABLE_INITIAL 1024

/* the value operands of x (a parameter's: one per predecessor), as words that can be rewritten */
#define VIRBUILD_FOR_EACH_OPERAND(V_, X_, O_, BODY_) do { \
	virbuild_preir_insn *x_ = &(V_)->insns[X_]; \
	const vir_opcode_info *oi_ = &vir_opcode_table[x_->insn.op]; \
	if (x_->insn.op == VIR_OP_BPARAM) { \
		if (x_->insn.b != VIRBUILD_BLOCK_NONE) \
			for (uint32_t j_ = 0; j_ < (V_)->blocks[x_->block].predecessor_count; j_++) { \
				uint32_t *O_ = &(V_)->extra[x_->insn.b + j_]; BODY_; \
			} \
		break; \
	} \
	for (int k_ = 0; k_ < 3; k_++) \
		if (virbuild_operand_kind_is_value(oi_->operand_kinds[k_]) && !(x_->insn.op == VIR_OP_BRIF && k_ > 0)) { \
			uint32_t *O_ = &(&x_->insn.a)[k_]; BODY_; \
		} \
	if (x_->insn.op == VIR_OP_CALL || x_->insn.op == VIR_OP_RET) \
		for (uint32_t k_ = 0; k_ < (V_)->extra[x_->insn.c]; k_++) { \
			uint32_t *O_ = &(V_)->extra[x_->insn.c + 1 + k_]; BODY_; \
		} \
} while (0)

static inline bool virbuild_operand_kind_is_value(uint8_t k)
{
	return k >= VIR_OPERAND_KIND_T && k <= VIR_OPERAND_KIND_BITC ? true : k == VIR_OPERAND_KIND_CONSTV;
}

static inline uint32_t virbuild_value_type(ecx *e, uint32_t v) { return e->vw->insns[v].insn.type; }

static inline uint32_t virbuild_value_resolve(virbuild_context *V, uint32_t v)
{
	uint32_t r = v;

	while (V->forward[r] != r)
		r = V->forward[r];
	while (V->forward[v] != r) {
		uint32_t n = V->forward[v];

		V->forward[v] = r;
		v = n;
	}
	return r;
}

/* what the files share (each defined in the file named) */
/* preir.c */
VPRIV virbuild_context *virbuild_context_get(ecx *e);
VPRIV __attribute__((noreturn, cold)) void virbuild_fail(ecx *e, const char *why);
VPRIV uint32_t virbuild_preir_insn_add(ecx *e, uint32_t op, uint32_t ty, uint32_t fl, uint32_t a, uint32_t b, uint32_t c, uint32_t blk);
VPRIV uint32_t virbuild_block_insn_append(ecx *e, uint32_t b, uint32_t op, uint32_t ty, uint32_t fl, uint32_t a, uint32_t bb, uint32_t c);
VPRIV uint32_t virbuild_insn_append(ecx *e, uint32_t op, uint32_t ty, uint32_t fl, uint32_t a, uint32_t b, uint32_t c);
VPRIV uint32_t virbuild_constant_make(ecx *e, uint32_t op, uint32_t ty, uint32_t a, uint32_t b, uint32_t c);
VPRIV uint32_t virbuild_constant_integer(ecx *e, uint32_t ty, int64_t v);
VPRIV uint32_t virbuild_extra_append(ecx *e, const uint32_t *w, uint32_t n);
VPRIV uint32_t virbuild_block_new(ecx *e);
VPRIV uint32_t virbuild_label_block(ecx *e, uint32_t l);
VPRIV void virbuild_block_terminate(ecx *e, uint32_t op, uint32_t a, uint32_t b, uint32_t c, uint32_t fl);
VPRIV void virbuild_branch_emit(ecx *e, uint32_t blk);

/* ssa-construction.c */
VPRIV void virbuild_variable_table_clear(virbuild_context *V);
VPRIV uint32_t *virbuild_variable_slot(ecx *e, uint32_t b, uint32_t var, bool add);
VPRIV void virbuild_variable_write(ecx *e, uint32_t var, uint32_t b, uint32_t v);
VPRIV uint32_t virbuild_parameter_new(ecx *e, uint32_t b, uint32_t var, uint32_t ty);
VPRIV void virbuild_parameter_operands_fill(ecx *e, uint32_t p, uint32_t b);
VPRIV uint32_t virbuild_undefined_value(ecx *e, uint32_t ty);
VPRIV uint32_t virbuild_variable_read(ecx *e, uint32_t var, uint32_t b, uint32_t ty);

/* lower-value.c */
VPRIV uint32_t virbuild_value_convert_in_block(ecx *e, uint32_t b, uint32_t v, uint32_t to);
VPRIV uint32_t virbuild_value_convert(ecx *e, uint32_t v, uint32_t to);
VPRIV uint32_t virbuild_register_read(ecx *e, int r, uint32_t ty);
VPRIV uint32_t virbuild_register_read_raw(ecx *e, int r);
VPRIV void virbuild_register_write(ecx *e, int r, uint32_t v);
VPRIV uint32_t virbuild_integer_type(int size);
VPRIV uint32_t virbuild_walker_value_get(ecx *e, const val *v, uint32_t ty);
VPRIV int virbuild_result_register_pick(ecx *e, const val *a, const val *b);
VPRIV uint32_t virbuild_float_type(ecx *e, qty t);

/* lower-memory.c */
VPRIV uint32_t virbuild_slot_find(ecx *e, int64_t off, uint64_t size, int64_t *rel);
VPRIV uint32_t virbuild_slot_address(ecx *e, uint32_t s, int64_t rel);
VPRIV void virbuild_store_table_clear(ecx *e, bool exposed_only);
VPRIV uint32_t virbuild_integer_log2(uint64_t x);
VPRIV uint32_t virbuild_memory_flags_make(int size, uint32_t align, bool sgn);

/* cfg.c */
VPRIV uint32_t virbuild_block_successor_count(virbuild_context *V, uint32_t b);
VPRIV uint32_t *virbuild_block_successor_pointer(virbuild_context *V, uint32_t b, uint32_t k);
VPRIV void virbuild_empty_blocks_thread(ecx *e);
VPRIV void virbuild_block_order_compute(ecx *e);
VPRIV void virbuild_block_predecessors_compute(ecx *e);
VPRIV uint32_t virbuild_block_predecessor_index(const virbuild_context *V, uint32_t j, uint32_t b);
VPRIV void virbuild_critical_edges_split(ecx *e);

/* block-parameters.c */
VPRIV bool virbuild_parameters_trivial_remove(ecx *e);
VPRIV void virbuild_parameters_dead_remove(ecx *e);

/* slot-promotion.c */
VPRIV bool virbuild_promote_store_is_dead(const virbuild_context *V, uint32_t q);
VPRIV bool virbuild_slots_promote(ecx *e);

/* if-conversion.c */
VPRIV bool virbuild_arm_is_hoistable(virbuild_context *V, uint32_t a, uint32_t b, uint32_t cond, uint32_t *n);
VPRIV bool virbuild_if_convert(ecx *e);

/* condition-merge.c */
VPRIV bool virbuild_conditions_merge(ecx *e);

/* to-vir.c */
VPRIV bool virbuild_to_vir_insn_emit(ecx *e, uint32_t i, uint32_t b);

/* pipeline.c */
VPRIV bool virbuild_function_emit(ecx *e);

#endif
