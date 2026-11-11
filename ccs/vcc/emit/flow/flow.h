/* Target-independent block adjacency and bounded backward mask dataflow.
 * Source ranges, effects and transformations belong to adapters. No machine
 * register numbering, record layout or wasm operand-stack model is assumed.
 */
#ifndef VCC_EMIT_FLOW_H
#define VCC_EMIT_FLOW_H
#include <stdbool.h>
#include <stdint.h>

typedef struct em_flow_edge { uint32_t from, to; } em_flow_edge;

typedef struct em_flow_graph {
	uint32_t blocks, edges;
	uint32_t *succ_off, *succ, *pred_off, *pred;
	uint32_t *storage;
	uint32_t capacity;
	bool valid, has_pred;
} em_flow_graph;

typedef struct em_flow_transfer { uint64_t use, def, exit; } em_flow_transfer;

typedef struct em_flow_live {
	uint64_t *in, *out;
	uint32_t *queue;
	uint8_t *queued;
	uint32_t capacity;
	uint64_t work;
	bool valid;
} em_flow_live;

/* IDs are dense [0,blocks). Duplicates and self edges are legal. Failure
 * invalidates the graph; no previously built adjacency may then be used. */
VPRIV bool em_flow_build(em_flow_graph *g, uint32_t blocks,
		const em_flow_edge *edges, uint32_t count, bool predecessors);
/* out[b] = exit[b] | union(in[successors]); in[b] = use[b] | (out[b]&~def[b]).
 * Valid only after convergence. Work counts each block evaluation and each
 * successor/predecessor visited, including a queued predecessor. Budget zero
 * disables analysis. A failed analysis must never authorize a transformation.
 */
VPRIV bool em_flow_liveness(const em_flow_graph *g, const em_flow_transfer *t,
		em_flow_live *live, uint32_t budget);
VPRIV void em_flow_graph_free(em_flow_graph *g) __attribute__((unused));
VPRIV void em_flow_live_free(em_flow_live *live) __attribute__((unused));
#endif
