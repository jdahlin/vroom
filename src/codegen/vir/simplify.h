/* The simplifier's shared types and what its files share (rules.c, value-numbering.c, load-forwarding.c, simplify.c). */
#ifndef VCC_EMIT_VIR_SIMPLIFY_H
#define VCC_EMIT_VIR_SIMPLIFY_H
#include "preir.h"

/* ---------------- simplification ----------------
 *
 * The walker's output, instruction by instruction, carries what its direct target folds as it
 * emits: extensions of values already extended, a truncation of a widening, x * 1, a chain of
 * constant offsets, the same address computed again, the same byte loaded again.  One walk
 * over the dominator tree rewrites each instruction (vw_simp1) and then looks it up: a pure
 * one among the block's earlier ones, a load among those of the block and of the blocks that
 * lead to it alone (a block with one predecessor continues its memory state; anything that
 * may write memory starts a new one, and a store remembers its value for a load of the same
 * address and type).  Constants are numbered first: the walker makes one per use.  What
 * nothing uses any more goes at the end. */

typedef struct vwhe { uint32_t op, ty, fl, a, b, c, gen, v; } vwhe;	/* (key) -> value */

typedef struct vwsimp {
	uint32_t *cnt, n0;	/* uses per value; values from n0 on are the pass's constants */
	vwhe *ht; uint32_t hmask;	/* pure instructions and loads of the dominators, by content */
	uint32_t *log, nlog;	/* ... the entries added, in order (undone leaving a block) */
	uint32_t *kt, kmask, ncst, nk;	/* constants: value + 1 */
	uint32_t memgen, gens, *egen;	/* the memory state loads are keyed by; per block at its end */
	uint32_t blk;		/* the block pure instructions are keyed by: one kept live into other
				 * blocks costs a register more often than it saves an instruction */
	uint32_t *idom, *dkid, *dnext;	/* the dominator tree */
	uint32_t undef[VT_N];
	/* loads across blocks (vw_far): positions in reverse postorder, what may write, the loops */
	uint32_t *seq;		/* per value below n0: its position (0: none) */
	uint32_t *wr, nwr;	/* the instructions that may write memory, by position */
	uint32_t *ri, *bend;	/* per block: its reverse-postorder index; the position of its end */
	uint32_t *linner;	/* per index: its innermost loop, VW_NOBLK none */
	uint32_t *lstart, *lend, *lpar;	/* per loop: first and last index, the loop round it */
	uint8_t *lmerged;	/* ... made of retreating edges to different blocks */
} vwsimp;

typedef struct vwaddr { uint32_t base; bool slot; int64_t off; } vwaddr;

#define VW_FAROFF	((int64_t)1 << 40)	/* offsets beyond are not compared */
#define VW_FARMAX	32			/* writers looked at per load */

/* rules.c */
VPRIV void vw_inc(vwst *V, vwsimp *S, uint32_t v);
VPRIV void vw_dec(vwst *V, vwsimp *S, uint32_t v);
VPRIV bool vw_kint(const vwst *V, uint32_t v, int64_t *k);
VPRIV uint32_t vw_undefk(ecx *e, vwsimp *S, uint32_t ty);
VPRIV bool vw_simp1(ecx *e, vwsimp *S, uint32_t q, uint32_t *r);
VPRIV bool vw_commutes(uint32_t op);

/* value-numbering.c */
VPRIV vwhe *vw_hlook(vwsimp *S, const vwhe *x);
VPRIV void vw_cse_blk(ecx *e, vwsimp *S_, uint32_t b);

/* load-forwarding.c */
VPRIV void vw_baseoff(vwst *V, uint32_t a, vwaddr *A);
VPRIV void vw_far_init(ecx *e, vwsimp *S);
VPRIV vwhe *vw_far(vwst *V, vwsimp *S, uint32_t q, const vwaddr *A, bool *ok);

/* cfg.c */
VPRIV void vw_doms(ecx *e, vwsimp *S);

/* simplify.c */
VPRIV void vw_simplify(ecx *e);

#endif
