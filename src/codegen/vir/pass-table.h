/*
 * The VIR builder's passes as one table (pass-table.c), in the order vw_emit
 * (pipeline.c) runs them, and VCC_VIR_PASSES, the switch that times, skips
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

/* facts about the pre-IR */
enum {
	PIR_ORDER = 1,	/* V->order: the reachable blocks, each once (vw_rpo) */
	PIR_PREDS = 2,	/* V->pr, pred, npred: distinct predecessors that agree with the successors (vw_preds) */
	PIR_DOMS = 4,	/* dominators: no pass keeps them yet (vw_simplify computes and frees its own) */
};

/* flags of a row */
enum {
	PASS_REQUIRED = 1,		/* later code relies on it: VCC_VIR_PASSES cannot leave it out */
	PASS_THEN_TRIVIAL = 2,		/* when it changed something: vw_trivial to a fixed point */
	PASS_IF_PREVIOUS_CHANGED = 4,	/* runs only if the last row without this flag ran and changed something */
	PASS_VECTOR_GATE = 8,		/* under the vectorizer's gate (VCC_VIR_VEC, -mgeneral-regs-only), which
					   the pass tests itself */
	PASS_FIXED_POINT = 16,		/* run again while it changes something (vw_trivial) */
	PASS_CHANGE_UNKNOWN = 32,	/* returns nothing: its "changed" is not counted */
	PASS_ON_VIR = 64,		/* works on VIR (V->f), not the pre-IR: checked by vir_verify */
};

typedef struct vir_pass {
	const char *name;	/* stable; VCC_VIR_PASSES and the statistics use it */
	bool (*run)(ecx *e);	/* returns whether it changed anything; a failure is vw_fail */
	uint8_t needs;		/* PIR_*: facts it reads */
	uint8_t makes;		/* PIR_*: facts it computes */
	uint8_t keeps;		/* PIR_*: the facts still valid after it; the others are cleared */
	uint8_t flags;		/* PASS_* */
} vir_pass;

/* VCC_VIR_PASSES, read once per process */
typedef struct vir_pass_switch {
	uint32_t skip;		/* bit per row: left out */
	uint32_t verify;	/* bit per row: the check runs after it */
	bool time;		/* per pass: runs, changed, instructions (with VCC_VIR_STATS) */
	bool active;		/* any of the above: vw_emit takes the instrumented path */
} vir_pass_switch;

/* pass-table.c */
VPRIV const vir_pass *vir_pass_list(uint32_t *n);
VPRIV const vir_pass_switch *vir_pass_switch_get(void);
VPRIV bool vir_pass_step(ecx *e, const vir_pass *p);
VPRIV bool vir_pass_step_instrumented(ecx *e, uint32_t i, uint32_t *valid);
VPRIV void vir_pass_note(emitter *E, uint32_t row, uint64_t instructions);
VPRIV void vir_pass_stats_print(emitter *E);
/* the rows of vir_pass_note that are not passes: the whole build (walk, passes, to-vir,
   canon) and the lowering, so that a pass's share of either can be computed; the
   instructions spent timing the passes inside are taken off */
#define VIR_PASS_ROW_BUILD(n) (n)
#define VIR_PASS_ROW_LOWER(n) ((n) + 1)

/* pipeline.c */
VPRIV void vw_seal(ecx *e);
VPRIV void vw_slot_uses(ecx *e);
VPRIV bool vw_to_vir(ecx *e);

/* preir-check.c: the pre-IR's invariants (preir.h, quality-vir-build.md section 2) as far as
   the facts in `valid` allow; NULL when they hold, else the first broken one (in buf) */
VPRIV const char *vw_preir_check(ecx *e, uint32_t valid, char *buf, size_t n);

#endif
