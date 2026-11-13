/*
 * VIR's passes (doc/quality/DESIGN-vir-optimizer.md, SPEC-vir.md 14): the
 * interface of a pass that works on the documented format, and the registry
 * that virtest and the compiler's pass table look passes up in.
 *
 * A pass gets a function in canonical form (SPEC-vir 2.4) that vir_verify
 * accepts, in the thread's vir_context, and returns one that is a refinement of
 * it (10.1) in canonical form, with whether it changed anything.  It
 * depends on nothing in frontend/, codegen/ or arch/.  The registry
 * (pass-list.c) has no order: the order of the -O2 pipeline is the builder's
 * table (codegen/vir/pass-table.c); virtest runs the order a test file or
 * --passes names.
 *
 * Scratch a pass may use between its calls to an analysis's getter:
 * f->scratch and vir_context.aux (a uint32_t per value), vir_context.bs[] (a uint32_t
 * per block); they are undefined when it returns.  A pass does not allocate.
 */
#ifndef VCC_VIR_PASS_H
#define VCC_VIR_PASS_H

#include "vir/vir.h"

typedef struct vir_pass_context {
	const vir_unit *unit;	/* virtest: the unit the function came from; the compiler: NULL */
	uint32_t budget;	/* SPEC-vir 10.4: rounds a fixed point may take (default 4) */
	uint64_t changes;	/* the pass adds the instructions it rewrote or removed */
	bool checked;		/* VCC_VIR_PASSES=verify or virtest: the pass may check its own invariants */
} vir_pass_context;

enum { VIR_PASS_BUDGET = 4 };

/* flags of a registry row */
enum {
	VIR_PASS_TARGET_FACTS = 1	/* reads vir_target_facts_get (there is no such table yet) */
};

typedef struct vir_pass_info {
	const char *name;	/* stable: VCC_VIR_PASSES, `; PASSES:`, virtest --passes, the statistics */
	bool (*run)(vir_function *f, vir_pass_context *ctx);	/* canonical in, canonical out; whether it changed f */
	uint8_t needs;		/* VIR_VALID_*: the analyses it asks for (it calls the getters itself) */
	uint8_t keeps;		/* VIR_VALID_*: still valid after it if they were valid before */
	uint8_t flags;		/* VIR_PASS_* */
} vir_pass_info;

const vir_pass_info *vir_pass_find(const char *name, size_t n);	/* NULL: no such pass */
const vir_pass_info *vir_pass_list(uint32_t *n);

/* Runs p on f.  Returns whether it changed f.  When ctx->checked or VCC_VIR_CHECK_ANALYSES=1, then
 * after the run every analysis in p->keeps that is still valid in f->valid is computed again and
 * compared with the one the pass left; on a mismatch *err names it (err has n bytes) and f is left as
 * the pass made it.  *err is the empty string otherwise. */
bool vir_pass_run(vir_function *f, const vir_pass_info *p, vir_pass_context *ctx, char *err, size_t n);

/* Drops the nops and the constants nothing uses from a canonical function, in place, and renumbers
 * the rest in the same order (operands, block ranges, source location runs, the constant table).
 * Blocks, edges and terminators do not change, so f->valid stays.  Returns the number of
 * instructions dropped (constants included). */
uint32_t vir_function_compact(vir_function *f);

#endif
