/*
 * The VIR builder's passes as one table (pass-table.c), in the order virbuild_function_emit
 * (pipeline.c) runs them, and -fvir-passes, the switch that times, skips
 * and checks them by name (doc/quality/WORK-vir.md step 2b, SPEC-vir.md 16).
 *
 * A row names facts about the pre-IR, not arrays, so that a row can later run
 * on another form (WORK-vir.md, "Owner's decision"): the facts it reads
 * (needs), the ones it computes (makes) and the ones still valid after it
 * (keeps; the others are cleared).  Nothing recomputes a fact on demand yet:
 * the table is checked once, when the switch is read, so that no pass is
 * left without a fact it needs whichever passes the switch leaves out.
 */
#ifndef VCC_CODEGEN_VIR_PASS_TABLE_H
#define VCC_CODEGEN_VIR_PASS_TABLE_H

#include "vir/pass.h"

/* facts about the pre-IR */
enum {
	VIRBUILD_FACT_ORDER = 1,	/* V->order: the reachable blocks, each once (virbuild_block_order_compute) */
	VIRBUILD_FACT_PREDS = 2,	/* V->pr, pred, npred: distinct predecessors that agree with the successors (virbuild_block_predecessors_compute) */
	VIRBUILD_FACT_DOMS = 4,	/* dominators: no pass keeps them yet (virbuild_function_simplify computes and frees its own) */
};

/* flags of a row */
enum {
	VIRBUILD_PASS_FLAG_REQUIRED = 1,		/* later code relies on it: -fvir-passes cannot leave it out */
	VIRBUILD_PASS_FLAG_THEN_TRIVIAL = 2,		/* when it changed something: virbuild_parameters_trivial_remove to a fixed point */
	VIRBUILD_PASS_FLAG_IF_PREVIOUS_CHANGED = 4,	/* runs only if the last row without this flag ran and changed something */
	VIRBUILD_PASS_FLAG_VECTOR_GATE = 8,		/* under the vectorizer's gate (-fvectorize, -fhoist-loop-constants, -mgeneral-regs-only), which
					   the pass tests itself */
	VIRBUILD_PASS_FLAG_FIXED_POINT = 16,		/* run again while it changes something (virbuild_parameters_trivial_remove) */
	VIRBUILD_PASS_FLAG_CHANGE_UNKNOWN = 32,	/* returns nothing: its "changed" is not counted */
	VIRBUILD_PASS_FLAG_ON_VIR = 64,		/* works on VIR (V->f), not the pre-IR: checked by vir_verify */
	PASS_OFF_BY_DEFAULT = 128,	/* skipped unless -fvir-passes says +NAME or only=...NAME */
};

typedef struct virbuild_pass {
	const char *name;	/* stable; -fvir-passes and the statistics use it */
	bool (*run)(ecx *e);	/* returns whether it changed anything; a failure is virbuild_fail */
	uint8_t needs;		/* PIR_*: facts it reads */
	uint8_t makes;		/* PIR_*: facts it computes */
	uint8_t keeps;		/* PIR_*: the facts still valid after it; the others are cleared */
	uint8_t flags;		/* PASS_* */
	const vir_pass_info *vir;	/* a VIR pass's row (needs VIRBUILD_PASS_FLAG_ON_VIR): its registry entry (vir/pass.h), run by
				   virbuild_pass_vir_run; run is NULL then, and the names agree */
} virbuild_pass;

/* (the switch -fvir-passes fills, virbuild_pass_switch, is the emitter's: emitimpl.h) */

/* pass-table.c */
VPRIV const virbuild_pass *virbuild_pass_list(uint32_t *n);
VPRIV bool virbuild_pass_step(ecx *e, const virbuild_pass *p);
VPRIV bool virbuild_pass_step_instrumented(ecx *e, uint32_t i, uint32_t *valid);
VPRIV void virbuild_pass_note(emitter *E, uint32_t row, uint64_t instructions);
VPRIV void virbuild_pass_stats_print(emitter *E);
/* the rows of virbuild_pass_note that are not passes: the whole build (walk, passes, to-vir,
   canon) and the lowering, so that a pass's share of either can be computed; the
   instructions spent timing the passes inside are taken off */
#define VIRBUILD_PASS_ROW_BUILD(n) (n)
#define VIRBUILD_PASS_ROW_LOWER(n) ((n) + 1)

/* pipeline.c */
VPRIV void virbuild_blocks_seal(ecx *e);
VPRIV void virbuild_slot_uses_compute(ecx *e);
VPRIV bool virbuild_function_to_vir(ecx *e);

/* preir-check.c: the pre-IR's invariants (preir.h, quality-vir-build.md section 2) as far as
   the facts in `valid` allow; NULL when they hold, else the first broken one (in buf) */
VPRIV const char *virbuild_preir_check(ecx *e, uint32_t valid, char *buf, size_t n);

#endif
