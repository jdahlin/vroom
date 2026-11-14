/* The simplifier's shared types and what its files share (rules.c, value-numbering.c, load-forwarding.c, simplify.c). */
#ifndef VCC_CODEGEN_VIR_SIMPLIFY_H
#define VCC_CODEGEN_VIR_SIMPLIFY_H
#include "preir.h"

/* ---------------- simplification ----------------
 *
 * The walker's output, instruction by instruction, carries what its direct target folds as it
 * emits: extensions of values already extended, a truncation of a widening, x * 1, a chain of
 * constant offsets, the same address computed again, the same byte loaded again.  One walk
 * over the dominator tree rewrites each instruction (virbuild_insn_simplify) and then looks it up: a pure
 * one among the block's earlier ones, a load among those of the block and of the blocks that
 * lead to it alone (a block with one predecessor continues its memory state; anything that
 * may write memory starts a new one, and a store remembers its value for a load of the same
 * address and type).  Constants are numbered first: the walker makes one per use.  What
 * nothing uses any more goes at the end. */

typedef struct virbuild_value_entry { uint32_t opcode, type, flags, a, b, c, generation, value; } virbuild_value_entry;	/* (key) -> value */

typedef struct virbuild_simplify_state {
	uint32_t *use_counts, constant_start;	/* uses per value; values from n0 on are the pass's constants */
	virbuild_value_entry *value_table; uint32_t value_table_mask;	/* pure instructions and loads of the dominators, by content */
	uint32_t *table_log, table_log_count;	/* ... the entries added, in order (undone leaving a block) */
	uint32_t *constant_table, constant_table_mask, constant_count, constant_entry_count;	/* constants: value + 1 */
	uint32_t memory_generation, block_generations, *end_generations;	/* the memory state loads are keyed by; per block at its end */
	uint32_t block;		/* the block pure instructions are keyed by: one kept live into other
				 * blocks costs a register more often than it saves an instruction */
	uint32_t *immediate_dominators, *dominator_first_child, *dominator_next_sibling;	/* the dominator tree */
	uint32_t undefined_values[VIR_TYPE_COUNT];
	/* loads across blocks (virbuild_far_load_find): positions in reverse postorder, what may write, the loops */
	uint32_t *positions;		/* per value below n0: its position (0: none) */
	uint32_t *writers, writer_count;	/* the instructions that may write memory, by position */
	uint32_t *reverse_postorder_index, *block_end_positions;	/* per block: its reverse-postorder index; the position of its end */
	uint32_t *loop_innermost;	/* per index: its innermost loop, VIRBUILD_BLOCK_NONE none */
	uint32_t *loop_start, *loop_end, *loop_parent;	/* per loop: first and last index, the loop round it */
	uint8_t *loop_is_merged;	/* ... made of retreating edges to different blocks */
} virbuild_simplify_state;

typedef struct virbuild_address { uint32_t base; bool slot; int64_t off; } virbuild_address;

#define VIRBUILD_FAR_LOAD_OFFSET_MAX	((int64_t)1 << 40)	/* offsets beyond are not compared */
#define VIRBUILD_FAR_LOAD_WRITERS_MAX	32			/* writers looked at per load */

/* rules.c */
VPRIV void virbuild_use_count_increment(virbuild_context *V, virbuild_simplify_state *S, uint32_t v);
VPRIV void virbuild_use_count_decrement(virbuild_context *V, virbuild_simplify_state *S, uint32_t v);
VPRIV bool virbuild_value_integer_constant(const virbuild_context *V, uint32_t v, int64_t *k);
VPRIV uint32_t virbuild_simplify_undefined_value(codegen_context *e, virbuild_simplify_state *S, uint32_t ty);
VPRIV bool virbuild_insn_simplify(codegen_context *e, virbuild_simplify_state *S, uint32_t q, uint32_t *r);
VPRIV bool virbuild_opcode_is_commutative(uint32_t op);

/* value-numbering.c */
VPRIV virbuild_value_entry *virbuild_value_table_lookup(virbuild_simplify_state *S, const virbuild_value_entry *x);
VPRIV void virbuild_block_values_number(codegen_context *e, virbuild_simplify_state *S_, uint32_t b);

/* load-forwarding.c */
VPRIV void virbuild_address_decompose(virbuild_context *V, uint32_t a, virbuild_address *A);
VPRIV void virbuild_far_load_init(codegen_context *e, virbuild_simplify_state *S);
VPRIV virbuild_value_entry *virbuild_far_load_find(virbuild_context *V, virbuild_simplify_state *S, uint32_t q, const virbuild_address *A, bool *ok);

/* cfg.c */
VPRIV void virbuild_block_dominators_compute(codegen_context *e, virbuild_simplify_state *S);

/* simplify.c */
VPRIV void virbuild_function_simplify(codegen_context *e);

#endif
