/*
 * VIR register allocation (SPEC-vir.md 15): the target's description of its
 * registers (15.1), the allocator's output (15.2), the loop forest the
 * allocator reads (15.5), and the checker (15.11).
 *
 *   vir_loops_build(f, &lp);		// until vir_canon builds the forest (22.5)
 *   vir_alloc(f, &lp, regs, &out);	// sweeps 5 and 6; f is not changed
 *   vir_alloc_check(f, &lp, regs, &out, &msg);
 *
 * Registers are bytes: 0-63 the integer class, 64-127 the float class (bit 6
 * is the class), VIR_REGISTER_SCRATCH0/VIR_REGISTER_SCRATCH1 the scratch register of each class that X7
 * keeps out of allocation (used only by edge moves to break a cycle),
 * VIR_REGISTER_NONE no register.  A spill slot is the allocator's own (out->spill_count of
 * them, 4 or 8 bytes), not a VIR slot: it is never exposed, and lowering adds
 * them to the frame.
 *
 * Every array lives in the thread's reservations (22.2): nothing is
 * allocated per function and nothing is cleared beyond what the function
 * uses.  The output is valid until the next vir_alloc on the thread.
 */
#ifndef VCC_VIR_ALLOC_H
#define VCC_VIR_ALLOC_H

#include "vir/vir.h"

enum { VIR_REGISTER_NONE = 0xff, VIR_REGISTER_SCRATCH0 = 0xfe, VIR_REGISTER_SCRATCH1 = 0xfd };
#define VIR_REGISTER_CLASS(r)	((r) >> 6 & 1)		/* 0 integer, 1 float (not for VR_* specials) */
#define VIR_REGISTER_NUMBER(r)	((r) & 63)
#define VIR_REGISTER_MAKE(c, n)	((uint8_t)((c) << 6 | (n)))
#define VIR_REGISTER_SCRATCH(c)	((c) ? VIR_REGISTER_SCRATCH1 : VIR_REGISTER_SCRATCH0)

/* the register class of a value type: 1 for f32/f64 and vectors, 0 otherwise */
static inline uint32_t vir_register_class(uint32_t ty) { return ty == VIR_TYPE_F32 || ty == VIR_TYPE_F64 || vir_type_is_vector(ty); }

/* ---- 15.1: what the target says about one instruction ---- */
#define VIR_REGISTER_CONSTRAINT_MAX 24
enum {
	VIR_CONSTRAINT_FLAG_TIED = 1,		/* the result must be in the first operand's register (x86 two-address) */
	VIR_CONSTRAINT_FLAG_COMMUTATIVE = 2,		/* the first two operands may be swapped */
	VIR_CONSTRAINT_FLAG_REST_MEMORY = 4		/* operands past nopnd need no register (stack-passed call arguments) */
};
typedef struct vir_register_constraint {
	uint8_t operand_count;		/* operands described in opnd[]; the rest are unconstrained (or VIR_CONSTRAINT_FLAG_REST_MEMORY) */
	uint8_t result;		/* the result's register, VIR_REGISTER_NONE: any */
	uint8_t flags;		/* VFX_* */
	uint8_t pad;
	uint8_t operands[VIR_REGISTER_CONSTRAINT_MAX];	/* per value operand (vir_operand_iterator order): its register, VIR_REGISTER_NONE: any */
	uint32_t mem;		/* bit i: operand i may be memory or an immediate, so needs no register */
	uint64_t clobbered[2];	/* registers written after the operands are read (per class, bit = number) */
	uint64_t edge_clobbered[2];	/* written before: no operand without a fixed register may be in one */
} vir_register_constraint;

static inline void vir_fix_clear(vir_register_constraint *x)
{
	*x = (vir_register_constraint){ 0 };
	for (int i = 0; i < VIR_REGISTER_CONSTRAINT_MAX; i++)
		x->operands[i] = VIR_REGISTER_NONE;
	x->result = VIR_REGISTER_NONE;
}

typedef struct vir_target_registers {
	uint64_t alloc[2];	/* allocatable registers per class: 0 integer, 1 float */
	uint64_t callee[2];	/* callee-saved */
	/* fixed operands and clobbers of instruction v; *out is cleared (all VIR_REGISTER_NONE) by the caller */
	void (*fixed)(const vir_function *f, uint32_t v, vir_register_constraint *out, void *ctx);
	void *context;
	const uint8_t *operand_fixes;	/* per opcode: nonzero if fixed() can say anything about it (NULL: call it always) */
} vir_target_registers;

/* ---- 15.5: the loop forest, as ranges of the block array ---- */
typedef struct vir_loop {
	uint32_t head, end;	/* blocks [head, end] */
	uint32_t parent;	/* enclosing loop, VIR_BLOCK_NONE at the top */
	uint32_t depth;		/* 1 for an outermost loop */
} vir_loop;
typedef struct vir_loops {
	vir_loop *loops;		/* in order of head (an enclosing loop before the loops inside it) */
	uint32_t count;
	uint32_t *inner;	/* per block: its innermost loop, VIR_BLOCK_NONE outside loops */
} vir_loops;
/* every edge to the same or an earlier block makes the range target..source; partially
 * overlapping ranges are merged (irreducible regions become one loop); vir_cfg_build first */
bool vir_loops_build(vir_function *f, vir_loops *lp);

/* ---- 15.2: the output ---- */
enum { VIR_EDIT_RELOAD, VIR_EDIT_REMATERIALIZE, VIR_EDIT_MOVE, VIR_EDIT_SWAP, VIR_EDIT_EXCHANGE, VIR_EDIT_STORE };
typedef struct vir_edit {	/* 12 bytes */
	uint32_t pos;		/* before instruction pos (VIR_EDIT_SWAP: of it) */
	uint8_t kind, dst, src, pad;	/* VIR_EDIT_RELOAD dst <- spill slot of v; VIR_EDIT_REMATERIALIZE dst <- constant v; VIR_EDIT_MOVE dst <- src;
					 * VIR_EDIT_STORE spill slot of v <- src (dst VIR_REGISTER_NONE) */
	uint32_t value;		/* the value moved */
} vir_edit;

/* a location of an edge move: a register byte, or VIR_LOCATION_SLOT | spill slot */
#define VIR_LOCATION_SLOT	0x80000000u
typedef struct vir_edge_move {	/* one step of an edge's sequenced parallel move */
	uint32_t dst, src;	/* VIR_EDIT_MOVE dst <- src (either may be a slot); VIR_EDIT_REMATERIALIZE dst <- constant v; VIR_EDIT_EXCHANGE dst <-> src */
	uint32_t value;		/* the value (for a block argument: the argument) */
	uint8_t kind;
	uint8_t pad[3];
} vir_edge_move;
typedef struct vir_alloc_edge {
	uint32_t from, to, successor_index;	/* block from's successor number k (vir_operand_iterator order) */
	uint32_t first, count;	/* moves [first, first+n) */
} vir_alloc_edge;

typedef struct vir_alloc_statistics {
	uint32_t spills;	/* values given a spill slot */
	uint32_t reloads, remats, moves, swaps, stores;	/* edits inside blocks */
	uint32_t edge_moves, edge_reloads, edge_rematerializations, edge_stores, edges, exchanges;	/* edge moves: register, reload, rematerialize, to a slot */
	uint32_t slots;		/* spill slots after sharing */
	uint32_t callee;	/* callee-saved registers used */
	uint32_t linear;	/* 1: the live-in pool overflowed (15.5 linear mode) */
	uint32_t pool;		/* words of the live-in pool used */
} vir_alloc_statistics;

typedef struct vir_alloc_out {
	uint32_t *slot;		/* per value: its spill slot, VIR_BLOCK_NONE none */
	uint8_t *spill_defined;		/* per value with a slot: 1 stored at its definition (a bparam: at its block's
				 * top); 0 the slot is written by VIR_EDIT_STORE edits and edge moves on the paths
				 * that read it (a value out of its register is always in its slot) */
	uint8_t *operand_registers;		/* per value operand, then the result, in block and operand order (bparams: at the block's top) */
	uint32_t operand_register_count;
	uint32_t *block_offsets;		/* per block: offset into opr (boff[0..nblk]), then into edit (boff[nblk+1+b], to [2*nblk+1]) */
	vir_edit *edit; uint32_t edit_count;
	vir_alloc_edge *edge; uint32_t edge_count;
	vir_edge_move *move; uint32_t move_count;
	uint32_t *edge_indices;		/* per successor (soff[b] + k): its vir_alloc_edge, VIR_BLOCK_NONE none */
	uint32_t *slot_offsets;		/* successor offsets per block, nblk+1 */
	uint8_t *slot_sizes;		/* per spill slot: 4, 8 or 16 (a vector) */
	uint32_t spill_count;
	uint64_t callee[2];	/* callee-saved registers used */
	uint32_t *callee_first;	/* per register byte (0-127): the first block that writes it */
	uint32_t save_point;	/* shrink-wrapping (15.12): the block in front of which the frame is set up; the
				 * blocks it does not dominate were given no callee-saved register when one
				 * could be avoided.  VIR_BLOCK_NONE: none (the function's entry) */
	vir_alloc_statistics statistics;
	char error_message[160];		/* why vir_alloc returned false */
} vir_alloc_out;

/* tuning and tests */
typedef struct vir_alloc_options {
	uint64_t pool;		/* live-in pool limit in words (0: the reservation) */
	bool private_spills;	/* a resumed call can observe a spill after its ordinary live range */
	bool shrink;		/* look for a save point (15.12) */
	const uint8_t *no_frame;	/* with shrink, per instruction: a call that needs no frame (a sibling call); or NULL */
	/* Optional diagnostic observer, never consulted by allocation policy. v=0 is
	 * the block's selected predecessor; other records describe entry values.
	 * Slots are reported by the final output, after slot sharing. */
	void (*entry)(void *context, uint32_t block, uint32_t predecessor, uint32_t v,
		      uint8_t reg, bool stored, const char *decision);
	void *entry_context;
} vir_alloc_options;

bool vir_alloc(vir_function *f, const vir_loops *lp, const vir_target_registers *regs, const vir_alloc_options *opt, vir_alloc_out *out);
/* 15.11: 0 when every operand reads its own value on every path; else the number of
 * failures, the first few described in msg */
uint32_t vir_alloc_check(vir_function *f, const vir_target_registers *regs, const vir_alloc_out *out, vbuffer *msg);
/* the function with the allocation as comments, and a counter line */
void vir_alloc_print(vbuffer *b, const vir_unit *u, vir_function *f, const vir_loops *lp, const vir_alloc_out *out);
void vir_alloc_register_name(char *o, uint32_t r);	/* r3, f1, s0/s1 (scratch), - */

#endif
