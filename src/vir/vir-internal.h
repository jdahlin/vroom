/*
 * vir-internal.h: VIR's internal declarations, shared by the files of the unity module (opcode-table.c, vir.c,
 * print.c, parse.c, verify.c, facts.c, layout.c).  Another module includes include/vir/vir.h, not this.
 *
 * It holds the thread's context (struct vir_context: the arrays every function of the thread lives in,
 * and the constant table with its generation), and the few functions that cross a file (the
 * terminator of a block, successor lists, the constant table, unit allocation).  A shared function
 * is VPRIV, static in the unity build (`make VCC_UNITY=` compiles the files one by one and every
 * file compiles alone).  vir_context is per thread and not locked: a function and its context are
 * used by one thread.  loops.c, register-allocation.c, register-allocation-check.c and register-allocation-print.c are separate translation units
 * (own statics) and include this too.
 */
#ifndef VCC_VIR_INTERNAL_H
#define VCC_VIR_INTERNAL_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/job-regions.h"
#include "common/buffer.h"
#include "vir/vir.h"

#ifndef VPRIV
#ifdef VCC_UNITY
#define VPRIV static
#else
#define VPRIV
#endif
#endif

/* The thread's reservations (22.2), made once; the generation stamps the
 * constant table (22.3) so that nothing is cleared per function. */
struct vir_context {
	vir_insn *insns[2];	/* source and destination of a copying sweep */
	int current;
	vir_block *blocks[2];
	vir_slot *slots;
	uint32_t *extra_words, *scratch, *auxiliary;
	vir_location *locations[2];
	int block_current, location_current;
	struct vir_cent { uint32_t gen, val; } *ctab;
	uint32_t gen, ncon;	/* the current generation; constants in the table */
	bool fail;
	uint32_t cfg_mark, cfg_end;	/* extra before the predecessor lists, and after */
	uint32_t *bs[6];	/* per block scratch (dominators, canon) */
	uint32_t *sc;		/* successor lists (CSR) built by vir_cfg_build's users */
};
#define VIR_CONSTANT_TABLE_BITS 24

VPRIV uint32_t vir_block_terminator(const vir_function *f, uint32_t b);	/* index of block b's terminator (its last real instruction), or 0 */
VPRIV bool vir_successors_build(vir_function *f, uint32_t *off, uint32_t *succ);	/* successor lists; false if a target is out of range */
VPRIV void vir_constant_table_reset(vir_context *c);
VPRIV void vir_constant_table_rebuild(vir_function *f);
VPRIV uint32_t vir_constant_find(vir_context *x, const vir_function *f, uint32_t op, uint32_t ty, uint32_t a, uint32_t b, uint32_t c,
			 uint32_t *slot);
VPRIV const char *vir_unit_string_copy(vir_unit *u, const char *s, size_t n);	/* a copy owned by the unit */
VPRIV void *vir_unit_alloc(vir_unit *u, size_t n);

/* a printf into a vbuffer, and a one-line error for the verifier and the parser */
#define VIR_FORMAT_PRINTF __attribute__((format(printf, 2, 3)))

#endif
