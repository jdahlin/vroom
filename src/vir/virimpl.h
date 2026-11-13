/*
 * VIR: internal declarations shared by the files of the module (table.c,
 * vir.c, print.c, parse.c, verify.c).  A shared function is VPRIV, static in
 * the unity build (`make VCC_UNITY=` compiles the files one by one and every
 * file compiles alone).
 */
#ifndef VCC_VIRIMPL_H
#define VCC_VIRIMPL_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/arena.h"
#include "../common/buf.h"
#include "vir.h"

#ifndef VPRIV
#ifdef VCC_UNITY
#define VPRIV static
#else
#define VPRIV
#endif
#endif

/* The thread's reservations (22.2), made once; the generation stamps the
 * constant table (22.3) so that nothing is cleared per function. */
struct vir_ctx {
	vir_ins *ins[2];	/* source and destination of a copying sweep */
	int cur;
	vir_blk *blk[2];
	vir_slot *slot;
	uint32_t *extra, *tmp, *aux;
	vir_loc *loc[2];
	int blkcur, loccur;
	struct vir_cent { uint32_t gen, val; } *ctab;
	uint32_t gen, ncon;	/* the current generation; constants in the table */
	bool fail;
	uint32_t cfg_mark, cfg_end;	/* extra before the predecessor lists, and after */
	uint32_t *bs[6];	/* per block scratch (dominators, canon) */
	uint32_t *sc;		/* successor lists (CSR) built by vir_cfg's users */
};
#define VIR_CTAB_BITS 24

VPRIV uint32_t vir_term_of(const vir_fn *f, uint32_t b);	/* index of block b's terminator (its last real instruction), or 0 */
VPRIV bool vir_succ_csr(vir_fn *f, uint32_t *off, uint32_t *succ);	/* successor lists; false if a target is out of range */
VPRIV void vir_ctab_reset(vir_ctx *c);
VPRIV void vir_ctab_rebuild(vir_fn *f);
VPRIV uint32_t vir_cfind(vir_ctx *x, const vir_fn *f, uint32_t op, uint32_t ty, uint32_t a, uint32_t b, uint32_t c,
			 uint32_t *slot);
VPRIV const char *vir_unit_str(vir_unit *u, const char *s, size_t n);	/* a copy owned by the unit */
VPRIV void *vir_unit_alloc(vir_unit *u, size_t n);

/* a printf into a vbuf, and a one-line error for the verifier and the parser */
#define VIR_FMT __attribute__((format(printf, 2, 3)))

#endif
