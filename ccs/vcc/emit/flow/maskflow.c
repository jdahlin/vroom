/* Monotone backward may-liveness with explicit edge/work budget. */
#include "../emitimpl.h"

static bool em_flow_live_grow(em_flow_live *live, uint32_t blocks)
{
	if (blocks <= live->capacity)
		return true;
	uint32_t capacity = live->capacity ? live->capacity : 16;
	while (capacity < blocks)
		capacity = capacity > UINT32_MAX / 2 ? blocks : capacity * 2;
	if ((uint64_t)capacity > SIZE_MAX / sizeof(uint64_t))
		return false;
	/* Keep each old pointer valid if any later allocation fails. Capacity is
	 * committed only when all buffers can accommodate the new function. */
	uint64_t *m = realloc(live->in, (size_t)capacity * sizeof(*m));
	if (!m)
		return false;
	live->in = m;
	m = realloc(live->out, (size_t)capacity * sizeof(*m));
	if (!m)
		return false;
	live->out = m;
	uint32_t *q = realloc(live->queue, (size_t)capacity * sizeof(*q));
	if (!q)
		return false;
	live->queue = q;
	uint8_t *queued = realloc(live->queued, (size_t)capacity);
	if (!queued)
		return false;
	live->queued = queued;
	live->capacity = capacity;
	return true;
}

VPRIV bool em_flow_liveness(const em_flow_graph *g, const em_flow_transfer *t,
		em_flow_live *live, uint32_t budget)
{
	live->valid = false;
	live->work = 0;
	if (!budget || !g->valid || !g->has_pred || !g->blocks || !t ||
	    !em_flow_live_grow(live, g->blocks))
		return false;
	uint32_t n = g->blocks, head = 0, tail = 0, pending = n;
	memset(live->in, 0, (size_t)n * sizeof(*live->in));
	memset(live->out, 0, (size_t)n * sizeof(*live->out));
	memset(live->queued, 1, (size_t)n);
	/* Reverse order usually propagates acyclic facts in a single visit. */
	for (uint32_t i = 0; i < n; i++)
		live->queue[i] = n - 1 - i;
	while (pending) {
		if (live->work == budget)
			return false;
		live->work++;
		uint32_t b = live->queue[head];
		head = head + 1 == n ? 0 : head + 1;
		pending--;
		live->queued[b] = 0;
		uint64_t out = t[b].exit;
		for (uint32_t i = g->succ_off[b]; i < g->succ_off[b + 1]; i++) {
			if (live->work == budget)
				return false;
			live->work++;
			out |= live->in[g->succ[i]];
		}
		uint64_t in = t[b].use | (out & ~t[b].def);
		live->out[b] = out;
		if (in == live->in[b])
			continue;
		live->in[b] = in;
		for (uint32_t i = g->pred_off[b]; i < g->pred_off[b + 1]; i++) {
			if (live->work == budget)
				return false;
			live->work++;
			uint32_t p = g->pred[i];
			if (!live->queued[p]) {
				live->queue[tail] = p;
				tail = tail + 1 == n ? 0 : tail + 1;
				pending++;
				live->queued[p] = 1;
			}
		}
	}
	live->valid = true;
	return true;
}

VPRIV void em_flow_live_free(em_flow_live *live)
{
	free(live->in);
	free(live->out);
	free(live->queue);
	free(live->queued);
	memset(live, 0, sizeof(*live));
}
