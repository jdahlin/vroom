/*
 * The lowering driver (doc/quality/WORK-vir.md step 2e, Q-VIRB-02): what the
 * arm64 and the x86-64 lowering of allocated VIR do the same way, written once
 * and calling a table of the target's callbacks.  The driver walks the layout
 * (VIR's block order, loops rotated, empty blocks chased), and in each block the
 * allocator's edits by position and the instructions with their operand registers
 * and result register; it defines labels and emits an edge's block (label, moves).
 * It knows no registers, no records and no walker: a target is its callbacks.
 *
 * Differences between the two targets that are kept as they are (identical output
 * is the rule) are fields of the table with today's value per target.  They are
 * unresolved, not decided: see Q-VIRB-02.
 */
#ifndef VCC_VIR_LOWERING_DRIVER_H
#define VCC_VIR_LOWERING_DRIVER_H

#include "../emitimpl.h"
#include "vir/layout.h"

typedef struct virbuild_lowering_target {
	/* the block's per-block state is reset (arm64: the flags the next instruction reads, the block's edits) */
	void (*block_begin)(void *ctx, uint32_t b);
	/* the block's label has been defined and the block is about to be lowered from the layout (not for a copy of it, vir_layout_duplicate) */
	void (*block_enter)(void *ctx, uint32_t b);
	/* instruction i of block b; opr[k] is the register of its k-th operand (nopr of them), res that of its result (VIR_REGISTER_NONE: none) */
	void (*instruction)(void *ctx, uint32_t b, uint32_t i, const uint8_t *opr, uint32_t nopr, uint8_t res);
	/* a result (or, on arm64, a block parameter) that lives in spill slot `slot` when defined: store res to it */
	void (*result_spill)(void *ctx, uint8_t res, uint32_t slot);
	/* one allocator edit; the driver walks o->edit[] by position */
	void (*edit)(void *ctx, const vir_edit *x);
	/* the parallel moves of edge g */
	void (*edge_moves)(void *ctx, uint32_t g);
	/* define a label */
	void (*label)(void *ctx, uint32_t label);
	/* the block's last instruction is lowered (arm64: the blocks of its branches' edges follow; NULL on x86-64) */
	void (*block_end)(void *ctx, uint32_t b);
	/* the layout is made (arm64: blocks in front of the save point first; NULL on x86-64) */
	void (*order_adjust)(void *ctx);
	/* the layout is final, before the first block (arm64: d8-d15 saved; NULL on x86-64) */
	void (*body_begin)(void *ctx);
	/* after the last block of the layout (arm64: the returns in front of the save point; x86-64: all edge blocks) */
	void (*after_blocks)(void *ctx);
	/* the lowering cannot go on: does not return */
	void (*fail)(void *ctx, const char *why);
	/* differences kept as they are (unresolved, Q-VIRB-02): today's value per target */
	bool params_in_loop;		/* x86-64: block parameters go through instruction(); arm64: result_spill(), here, in the order of the operand registers */
	bool edits_at_block_end;	/* x86-64: the edits at pos = first + n run after the last instruction; arm64: none */
	bool skip_multi_operands;	/* arm64: a VIR_TYPE_MULTI operand has no register and instruction() reads them from ov/opr; x86-64 rejects such functions */
} virbuild_lowering_target;

/* the driver's inputs for one function; the target fills it, the driver reads it */
typedef struct virbuild_lowering {
	const virbuild_lowering_target *target;
	void *ctx;
	vir_function *f;
	const vir_alloc_out *o;
	const vir_loops *lp;
	/* arrays of f->block_count entries the layout fills: order, position, rotation, chase; path is scratch of f->block_count, block_label is the target's */
	uint32_t *order, *position, *rotation, *chase, *path;
	const uint32_t *block_label;
	bool optimize;			/* loop rotation and entry duplication are allowed (not VCC_NOOPT) */
	bool rotate;			/* -frotate-loops: the layout puts a loop's test at its end (else VIR's block order) */
	/* skip_multi_operands: where the operands' values and registers go, and the room (arm64) */
	uint32_t *operand_value;
	uint8_t *operand_reg;
	uint32_t max_operands;
	/* an instruction at which the block's lowering stops (a sibling call left the ret: arm64), none when *skip_at is 0; NULL: never */
	const uint32_t *skip_at;
	/* -g: a line-table row at the next record, called at each block and wherever f->locations starts a run; NULL: none */
	void (*row)(ecx *e, uint32_t loc);
	ecx *row_e;
} virbuild_lowering;

/* the chases, the layout, then each block in it that is not chased away, then after_blocks */
VPRIV void virbuild_lower_body(const virbuild_lowering *d);
/* block b's code (its label defined by the caller): also for a copy of a block at a jump (vir_layout_duplicate) */
VPRIV void virbuild_lower_block(const virbuild_lowering *d, uint32_t b);
/* an edge's block: its label, then the edge's moves (the caller ends it with a jump) */
VPRIV void virbuild_lower_edge_block(const virbuild_lowering *d, uint32_t label, uint32_t g);
/* callee (a symbol's address) returns twice: setjmp and the like */
VPRIV bool virbuild_call_returns_twice(ecx *e, const vir_insn *callee);

#endif
