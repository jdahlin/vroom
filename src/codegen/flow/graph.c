/* Dense-ID CSR topology, shared by native records and typed wasm events. */
#include "../emitimpl.h"

VPRIV bool em_flow_build(em_flow_graph *g, uint32_t blocks,
		const em_flow_edge *edges, uint32_t count, bool predecessors)
{
	g->valid = false;
	g->has_pred = false;
	g->blocks = g->edges = 0;
	if (!blocks || (count && !edges))
		return false;
	/* Leave room for adjacency offsets and one reusable construction cursor. */
	uint64_t words = (uint64_t)(predecessors ? 3 : 2) * ((uint64_t)blocks + 1) +
		(uint64_t)(predecessors ? 2 : 1) * count;
	if (words > UINT32_MAX || words > SIZE_MAX / sizeof(uint32_t))
		return false;
	for (uint32_t i = 0; i < count; i++)
		if (edges[i].from >= blocks || edges[i].to >= blocks)
			return false;
	if (words > g->capacity) {
		uint32_t capacity = g->capacity ? g->capacity : 64;
		while (capacity < words)
			capacity = capacity > UINT32_MAX / 2 ? (uint32_t)words : capacity * 2;
		if ((uint64_t)capacity > SIZE_MAX / sizeof(uint32_t))
			capacity = (uint32_t)words;
		uint32_t *p = realloc(g->storage, (size_t)capacity * sizeof(*p));
		if (!p)
			return false;
		g->storage = p;
		g->capacity = capacity;
	}
	g->succ_off = g->storage;
	g->succ = g->succ_off + blocks + 1;
	g->pred_off = predecessors ? g->succ + count : NULL;
	g->pred = predecessors ? g->pred_off + blocks + 1 : NULL;
	uint32_t *cursor = predecessors ? g->pred + count : g->succ + count;
	memset(g->succ_off, 0, ((size_t)blocks + 1) * sizeof(uint32_t));
	if (predecessors)
		memset(g->pred_off, 0, ((size_t)blocks + 1) * sizeof(uint32_t));
	for (uint32_t i = 0; i < count; i++) {
		g->succ_off[edges[i].from + 1]++;
		if (predecessors)
			g->pred_off[edges[i].to + 1]++;
	}
	for (uint32_t b = 1; b <= blocks; b++) {
		g->succ_off[b] += g->succ_off[b - 1];
		if (predecessors)
			g->pred_off[b] += g->pred_off[b - 1];
	}
	memcpy(cursor, g->succ_off, (size_t)blocks * sizeof(*cursor));
	for (uint32_t i = 0; i < count; i++)
		g->succ[cursor[edges[i].from]++] = edges[i].to;
	if (predecessors) {
		memcpy(cursor, g->pred_off, (size_t)blocks * sizeof(*cursor));
		for (uint32_t i = 0; i < count; i++)
			g->pred[cursor[edges[i].to]++] = edges[i].from;
	}
	g->blocks = blocks;
	g->edges = count;
	g->has_pred = predecessors;
	g->valid = true;
	return true;
}

VPRIV void em_flow_graph_free(em_flow_graph *g)
{
	free(g->storage);
	memset(g, 0, sizeof(*g));
}
