/* Bounded physical-register liveness before two-address lowering. Topology and
 * convergence are shared; x86 retains effects, flags and epilogue contracts.
 */
#include "x64impl.h"

#define X_DEAD_BLOCKS 256
#define X_DEAD_EDGES 1024
#define X_DEAD_RECORDS 65536
#define X_DEAD_FLAGS (UINT64_C(1) << 63)
#define X_DEAD_BIT(r) (UINT64_C(1) << (r))
#define X_DEAD_DELETED UINT8_C(0xff)

static _Thread_local struct {
	em_flow_graph graph;
	em_flow_live live;
	em_flow_edge edges[X_DEAD_EDGES];
	em_flow_transfer transfer[X_DEAD_BLOCKS];
	uint32_t start[X_DEAD_BLOCKS + 1];
	struct { uint32_t label, block; } labels[X_DEAD_BLOCKS];
} x64_dead_work;

/* Keep this in sync with the implicit printer epilogue: integer/aggregate and
 * SSE result registers, frame/canary bases and stack restoration. Callee-save
 * registers are conservatively live too, even when the epilogue reloads them.
 * x87 results have no mask bit, but every x87 record is non-removable.
 */
static uint64_t x64_dead_exit(void)
{
	return X_DEAD_BIT(RAX) | X_DEAD_BIT(RDX) | X_DEAD_BIT(XMM0) | X_DEAD_BIT(XMM0 + 1) |
	       X_DEAD_BIT(RSP) | X_DEAD_BIT(RBP) | X_DEAD_BIT(R12) | X_DEAD_BIT(RBX) |
	       X_DEAD_BIT(R13) | X_DEAD_BIT(R14) | X_DEAD_BIT(R15);
}

static uint64_t x64_dead_use(receffects f)
{
	return f.use | ((f.fl & OD_RFLAGS) ? X_DEAD_FLAGS : 0);
}

static uint64_t x64_dead_def(receffects f)
{
	return f.def | ((f.fl & OD_WFLAGS) ? X_DEAD_FLAGS : 0);
}

static bool x64_dead_removable(receffects f)
{
	return (f.fl & OD_PURE) && !(f.fl & (OD_LOAD | OD_STORE | OD_TRAP | OD_FP | OD_CALL | OD_BARRIER | OD_LABEL)) &&
	       f.br == RB_NONE;
}

static bool x64_dead_edge(uint32_t from, uint32_t to, uint32_t *count)
{
	if (*count == X_DEAD_EDGES)
		return false;
	x64_dead_work.edges[(*count)++] = (em_flow_edge){ from, to };
	return true;
}

static bool x64_dead_target(uint32_t label, uint32_t labels, uint32_t *target)
{
	/* At most 256 labels. No per-function allocation or label-ID-sized array. */
	for (uint32_t k = 0; k < labels; k++)
		if (x64_dead_work.labels[k].label == label) {
			*target = x64_dead_work.labels[k].block;
			return true;
		}
	return false;
}

VPRIV void x64_dead_budget(ecx *e, uint32_t budget)
{
	xst *x = e->x64;
	uint32_t n = x->nin, blocks = 1, labels = 0, edges = 0;
	uint32_t *start = x64_dead_work.start;
	em_flow_transfer *transfer = x64_dead_work.transfer;

	/* The normal epilogue contract below does not cover interrupt returns.
	 * Opaque asm (including valid naked bodies) is rejected by the scan.
	 */
	if (x->interrupt || (e->c && e->wk->fsym && (e->c->syms[e->wk->fsym].flags & SF_NORETURN)) ||
	    n < 2 || n > X_DEAD_RECORDS)
		return;
	start[0] = 0;
	for (uint32_t k = 0; k < n; k++) {
		const rec *i = &x->in[k];

		/* An opaque record or escaped label could add unrepresented entries
		 * or edges. Decline the whole function without modifying any record.
		 */
		if (i->op >= X_NOPS || i->op == X_RAW || i->op == X_NAMED || i->op == X_JMPR || i->op == X_LEAL)
			return;
		if (k && (i->op == X_LABEL || x->in[k - 1].op == X_JMP || x->in[k - 1].op == X_JCC)) {
			if (blocks == X_DEAD_BLOCKS)
				return;
			start[blocks++] = k;
		}
		if (i->op == X_LABEL) {
			uint32_t previous;

			if (labels == X_DEAD_BLOCKS || x64_dead_target(i->label, labels, &previous))
				return;
			x64_dead_work.labels[labels].label = i->label;
			x64_dead_work.labels[labels++].block = blocks - 1;
		}
	}
	start[blocks] = n;
	for (uint32_t b = 0; b < blocks; b++) {
		em_flow_transfer t = { 0 };

		for (uint32_t k = start[b]; k < start[b + 1]; k++) {
			receffects f = x64_effects(e, &x->in[k]);
			uint64_t use = x64_dead_use(f);

			if (f.br != RB_NONE && x->in[k].op != X_JMP && x->in[k].op != X_JCC)
				return;
			t.use |= use & ~t.def;
			/* OD_WFLAGS describes an output, not necessarily a complete
			 * replacement of every condition flag. Keep the aggregate flag
			 * token live across writers until a finer target proof exists.
			 */
			t.def |= f.def;
		}
		const rec *last = &x->in[start[b + 1] - 1];
		if (last->op == X_JMP || last->op == X_JCC) {
			uint32_t target;

			if (!x64_dead_target(last->label, labels, &target) || !x64_dead_edge(b, target, &edges))
				return;
		}
		if (last->op != X_JMP) {
			if (b + 1 < blocks) {
				if (!x64_dead_edge(b, b + 1, &edges))
					return;
			} else {
				t.exit = x64_dead_exit();
			}
		}
		transfer[b] = t;
	}
	if (budget == UINT32_MAX)
		budget = n + 32 * (blocks + edges) + 64;
	if (!em_flow_build(&x64_dead_work.graph, blocks, x64_dead_work.edges, edges, true) ||
	    !x64_dead_work.graph.valid ||
	    !em_flow_liveness(&x64_dead_work.graph, transfer, &x64_dead_work.live, budget) || !x64_dead_work.live.valid)
		return;
	/* Only a converged analysis authorizes mutation. Removing records during
	 * this backward sweep is conservative across blocks: successor facts were
	 * computed with all original uses, so no second fixed point is needed.
	 */
	bool changed = false;
	for (uint32_t b = 0; b < blocks; b++) {
		uint64_t live = x64_dead_work.live.out[b];

		for (uint32_t k = start[b + 1]; k-- > start[b];) {
			receffects f = x64_effects(e, &x->in[k]);
			uint64_t def = x64_dead_def(f);

			if (def && !(def & live) && x64_dead_removable(f)) {
				x->in[k].op = X_DEAD_DELETED;
				changed = true;
			} else {
				live = x64_dead_use(f) | (live & ~f.def);
			}
		}
	}
	if (!changed)
		return;
	uint32_t out = 0, cursor = 0;
	for (uint32_t k = 0; k < n; k++) {
		if (e->ndl)
			dbg_remap(e, &cursor, k, out);
		if (x->in[k].op != X_DEAD_DELETED)
			x->in[out++] = x->in[k];
	}
	if (e->ndl)
		dbg_remap(e, &cursor, UINT32_MAX, out);
	x->nin = out;
}

VPRIV void x64_dead(ecx *e)
{
	if (e->discarded_scalar)
		x64_dead_budget(e, UINT32_MAX);
}
