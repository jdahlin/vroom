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
 * is the class), VR_SCR0/VR_SCR1 the scratch register of each class that X7
 * keeps out of allocation (used only by edge moves to break a cycle),
 * VR_NONE no register.  A spill slot is the allocator's own (out->nspill of
 * them, 4 or 8 bytes), not a VIR slot: it is never exposed, and lowering adds
 * them to the frame.
 *
 * Every array lives in the thread's reservations (22.2): nothing is
 * allocated per function and nothing is cleared beyond what the function
 * uses.  The output is valid until the next vir_alloc on the thread.
 */
#ifndef VCC_VIR_ALLOC_H
#define VCC_VIR_ALLOC_H

#include "vir.h"

enum { VR_NONE = 0xff, VR_SCR0 = 0xfe, VR_SCR1 = 0xfd };
#define VR_CLASS(r)	((r) >> 6 & 1)		/* 0 integer, 1 float (not for VR_* specials) */
#define VR_NUM(r)	((r) & 63)
#define VR_MAKE(c, n)	((uint8_t)((c) << 6 | (n)))
#define VR_SCR(c)	((c) ? VR_SCR1 : VR_SCR0)

/* the register class of a value type: 1 for f32/f64, 0 otherwise */
static inline uint32_t vir_rclass(uint32_t ty) { return ty == VT_F32 || ty == VT_F64; }

/* ---- 15.1: what the target says about one instruction ---- */
#define VIR_FIX_N 24
enum {
	VFX_TIED = 1,		/* the result must be in the first operand's register (x86 two-address) */
	VFX_COMMUT = 2,		/* the first two operands may be swapped */
	VFX_RESTMEM = 4		/* operands past nopnd need no register (stack-passed call arguments) */
};
typedef struct vir_fix {
	uint8_t nopnd;		/* operands described in opnd[]; the rest are unconstrained (or VFX_RESTMEM) */
	uint8_t res;		/* the result's register, VR_NONE: any */
	uint8_t fl;		/* VFX_* */
	uint8_t pad;
	uint8_t opnd[VIR_FIX_N];	/* per value operand (vir_oit order): its register, VR_NONE: any */
	uint32_t mem;		/* bit i: operand i may be memory or an immediate, so needs no register */
	uint64_t clob[2];	/* registers written after the operands are read (per class, bit = number) */
	uint64_t eclob[2];	/* written before: no operand without a fixed register may be in one */
} vir_fix;

static inline void vir_fix_clear(vir_fix *x)
{
	*x = (vir_fix){ 0 };
	for (int i = 0; i < VIR_FIX_N; i++)
		x->opnd[i] = VR_NONE;
	x->res = VR_NONE;
}

typedef struct vir_regs {
	uint64_t alloc[2];	/* allocatable registers per class: 0 integer, 1 float */
	uint64_t callee[2];	/* callee-saved */
	/* fixed operands and clobbers of instruction v; *out is cleared (all VR_NONE) by the caller */
	void (*fixed)(const vir_fn *f, uint32_t v, vir_fix *out, void *ctx);
	void *ctx;
	const uint8_t *opfix;	/* per opcode: nonzero if fixed() can say anything about it (NULL: call it always) */
} vir_regs;

/* ---- 15.5: the loop forest, as ranges of the block array ---- */
typedef struct vir_loop {
	uint32_t head, end;	/* blocks [head, end] */
	uint32_t parent;	/* enclosing loop, VIR_NOBLK at the top */
	uint32_t depth;		/* 1 for an outermost loop */
} vir_loop;
typedef struct vir_loops {
	vir_loop *l;		/* in order of head (an enclosing loop before the loops inside it) */
	uint32_t n;
	uint32_t *inner;	/* per block: its innermost loop, VIR_NOBLK outside loops */
} vir_loops;
/* every edge to the same or an earlier block makes the range target..source; partially
 * overlapping ranges are merged (irreducible regions become one loop); vir_cfg first */
bool vir_loops_build(vir_fn *f, vir_loops *lp);

/* ---- 15.2: the output ---- */
enum { VE_RELOAD, VE_REMAT, VE_MOVE, VE_SWAP, VE_XCHG, VE_STORE };
typedef struct vir_edit {	/* 12 bytes */
	uint32_t pos;		/* before instruction pos (VE_SWAP: of it) */
	uint8_t kind, dst, src, pad;	/* VE_RELOAD dst <- spill slot of v; VE_REMAT dst <- constant v; VE_MOVE dst <- src;
					 * VE_STORE spill slot of v <- src (dst VR_NONE) */
	uint32_t v;		/* the value moved */
} vir_edit;

/* a location of an edge move: a register byte, or VL_SLOT | spill slot */
#define VL_SLOT	0x80000000u
typedef struct vir_emove {	/* one step of an edge's sequenced parallel move */
	uint32_t dst, src;	/* VE_MOVE dst <- src (either may be a slot); VE_REMAT dst <- constant v; VE_XCHG dst <-> src */
	uint32_t v;		/* the value (for a block argument: the argument) */
	uint8_t kind;
	uint8_t pad[3];
} vir_emove;
typedef struct vir_aedge {
	uint32_t from, to, k;	/* block from's successor number k (vir_oit order) */
	uint32_t first, n;	/* moves [first, first+n) */
} vir_aedge;

typedef struct vir_astats {
	uint32_t spills;	/* values given a spill slot */
	uint32_t reloads, remats, moves, swaps, stores;	/* edits inside blocks */
	uint32_t emoves, ereloads, eremats, estores, edges, xchg;	/* edge moves: register, reload, rematerialize, to a slot */
	uint32_t slots;		/* spill slots after sharing */
	uint32_t callee;	/* callee-saved registers used */
	uint32_t linear;	/* 1: the live-in pool overflowed (15.5 linear mode) */
	uint32_t pool;		/* words of the live-in pool used */
} vir_astats;

typedef struct vir_alloc_out {
	uint32_t *slot;		/* per value: its spill slot, VIR_NOBLK none */
	uint8_t *sdef;		/* per value with a slot: 1 stored at its definition (a bparam: at its block's
				 * top); 0 the slot is written by VE_STORE edits and edge moves on the paths
				 * that read it (a value out of its register is always in its slot) */
	uint8_t *opr;		/* per value operand, then the result, in block and operand order (bparams: at the block's top) */
	uint32_t nopr;
	uint32_t *boff;		/* per block: offset into opr (boff[0..nblk]), then into edit (boff[nblk+1+b], to [2*nblk+1]) */
	vir_edit *edit; uint32_t nedit;
	vir_aedge *edge; uint32_t nedge;
	vir_emove *move; uint32_t nmove;
	uint32_t *eidx;		/* per successor (soff[b] + k): its vir_aedge, VIR_NOBLK none */
	uint32_t *soff;		/* successor offsets per block, nblk+1 */
	uint8_t *ssz;		/* per spill slot: 4 or 8 */
	uint32_t nspill;
	uint64_t callee[2];	/* callee-saved registers used */
	uint32_t *cfirst;	/* per register byte (0-127): the first block that writes it */
	vir_astats st;
	char err[160];		/* why vir_alloc returned false */
} vir_alloc_out;

/* tuning and tests */
typedef struct vir_aopt {
	uint64_t pool;		/* live-in pool limit in words (0: the reservation) */
} vir_aopt;

bool vir_alloc(vir_fn *f, const vir_loops *lp, const vir_regs *regs, const vir_aopt *opt, vir_alloc_out *out);
/* 15.11: 0 when every operand reads its own value on every path; else the number of
 * failures, the first few described in msg */
uint32_t vir_alloc_check(vir_fn *f, const vir_regs *regs, const vir_alloc_out *out, vbuf *msg);
/* the function with the allocation as comments, and a counter line */
void vir_alloc_print(vbuf *b, const vir_unit *u, vir_fn *f, const vir_loops *lp, const vir_alloc_out *out);
void vir_reg_name(char *o, uint32_t r);	/* r3, f1, s0/s1 (scratch), - */

#endif
