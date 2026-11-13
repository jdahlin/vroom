/* The VIR builder's state (codegen/vir/build.c): one per ecx, reused by every function. */
#ifndef VCC_EMIT_VIR_VW_H
#define VCC_EMIT_VIR_VW_H
#include "../../vir/vir.h"
#include "../../vir/alloc.h"

typedef struct vwp {	/* a pre-IR instruction: operands are pre-IR values, extras in vwst.x */
	vir_ins in;
	uint32_t blk;	/* its block; VW_NOBLK for a constant */
	uint32_t next;	/* the next one in the block's list (0: last) */
	uint32_t loc;	/* source location */
	uint32_t slot;	/* load, store: its frame slot + 1, 0 none */
} vwp;

typedef struct vwb {
	uint32_t head, tail;	/* instructions, in order */
	uint32_t phead, ptail;	/* parameters (Braun's phis) */
	uint32_t term;		/* terminator (0: open) */
	uint32_t pred, npred;	/* predecessors: vwst.pr[pred..pred+npred), distinct */
	uint32_t fin;		/* final block id, VW_NOBLK unreachable */
	uint8_t fall, reach;
	uint8_t def;		/* a variable is written in it: not empty, whatever its instructions */
} vwb;

typedef struct vwh { uint64_t key; uint32_t v, gen; } vwh;	/* (block, variable) -> current definition */

typedef struct vwslot {
	int64_t off;		/* the walker's frame offset */
	uint64_t size;
	uint32_t align;
	uint8_t exposed;	/* its address escaped (laddr) */
	uint32_t loads;		/* loads the store table did not answer (during the walk) */
	uint8_t used;		/* a reachable load reads it (emission) */
	uint32_t fin;		/* VIR slot id, VW_NOBLK none yet */
	uint8_t prom;		/* promoted to variables (vw_promote): its stores go */
	uint8_t f32w;		/* an 8-byte slot whose last float stored was an f32 (its low 4 bytes) */
} vwslot;

#define VW_LIFETIME 0x8000u	/* a memset's flag: the start of a local's lifetime (vw_undecl) */
typedef struct vwstent { uint32_t slot; int64_t off; int size; uint32_t v; } vwstent;	/* store table */
typedef struct vwloc { uint32_t sym, inst; } vwloc;

typedef struct vwst {
	vwp *p; uint32_t np, cp;
	vwb *b; uint32_t nb, cb;
	uint32_t *x; uint32_t nx, cx;		/* extra words (pre-IR values) */
	vwh *h; uint32_t hn, hcap, hgen, halloc;	/* an entry of another gen is empty (no clearing per function);
						 * hcap: the part in use, small again at each function */
	uint32_t *lh, lhcap, lhalloc;		/* (symbol, inlining instance) -> local + 1 */
	uint32_t *fwd; uint32_t cfwd;		/* removed parameters: the value that replaces them */
	uint32_t *lab; uint32_t clab, labhi;		/* walker label -> block */
	vwslot *sl; uint32_t nsl, csl;
	uint32_t *act; uint32_t nact, cact;	/* slots that own their range now */
	vwstent *st; uint32_t nst, cst;
	vwloc *loc; uint32_t nloc, cloc;
	uint32_t nprom;				/* variables of the promoted fields so far (after the locals') */
	uint8_t *vty; uint32_t cvty;		/* per local: its VIR type */
	uint8_t *vsgn; uint32_t cvsgn;		/* ... and whether it is a signed integer */
	uint32_t *pr; uint32_t npr, cpr;	/* predecessor lists */
	uint32_t *map; uint32_t cmap;		/* pre-IR value -> VIR value */
	uint32_t *order; uint32_t norder, corder;	/* blocks in emission order */
	uint32_t *bmap; uint32_t cbmap;		/* scratch */
	uint64_t live[3], flive;		/* names in use */
	uint32_t fprod[64];			/* per float name: the product (or its negation) the expression in hand just made, 0 none */
	uint32_t cur;				/* the block being built */
	bool sealing;
	uint32_t rty;				/* the result's type, VT_VOID none */
	uint32_t nparam, fsig;			/* the function's signature (pre extra offset) */
	const char *why;
	vir_fn f;				/* the function built (valid until the thread's next vir_begin) */
	int64_t *xslot, *xspill;
	uint8_t *xuses; uint32_t cxuses;
	uint32_t *xedgelab, cxedgelab;
	uint32_t *xlayout, cxlayout;
	uint32_t *xlabel, cxslot, cxspill, cxlabel; /* x86 lowering buffers, reused per ecx */
	vir_loops lp;
	vir_alloc_out ao;
} vwst;

#endif
