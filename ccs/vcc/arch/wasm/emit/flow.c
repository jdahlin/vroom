/* Wasm exact-cell DSE across structured joins. Control topology is recorded
 * by typed emitter hooks from entry; instruction text is never inspected.
 * Initial scope excludes loops, dispatchers, EH and dynamic frames. */
#include "wimpl.h"

#define WFLOW_EVENTS 8192u
#define WFLOW_WORK 1048576u

VPRIV void wflow_begin(ecx *e)
{
	wst *W = e->w;

	W->nflow = W->flowdepth = W->flowjoins = W->flowstores = 0;
	W->flowfailed = !W->memopt;
}

VPRIV void wflow_reject(ecx *e) { e->w->flowfailed = true; }

static wflow_event *wf_event(ecx *e, int kind)
{
	wst *W = e->w;

	if (W->flowfailed)
		return NULL;
	if (W->nflow == WFLOW_EVENTS) {
		W->flowfailed = true;
		return NULL;
	}
	if (W->nflow == W->capflow) {
		uint32_t cap = W->capflow ? W->capflow * 2 : 32;
		wflow_event *p = realloc(W->flow, cap * sizeof(*p));

		if (!p) {
			W->flowfailed = true;
			return NULL;
		}
		W->flow = p;
		W->capflow = cap;
	}
	wflow_event *f = &W->flow[W->nflow++];
	*f = (wflow_event){ .at = W->nmem, .kind = (uint8_t)kind,
		.token = UINT32_MAX, .end = UINT32_MAX, .alternate = UINT32_MAX };
	return f;
}

VPRIV void wflow_open(ecx *e, int kind)
{
	wst *W = e->w;
	wflow_event *f;

	if (W->flowfailed)
		return;
	if ((kind != WF_BLOCK && kind != WF_IF) || W->flowdepth == 256 ||
	    W->flowdepth != (uint32_t)W->depth) {
		wflow_reject(e);
		return;
	}
	f = wf_event(e, kind);
	if (f) {
		f->token = W->nflow - 1;
		W->flowstack[W->flowdepth++] = f->token;
		W->flowjoins += kind == WF_IF;
	}
}

VPRIV void wflow_else(ecx *e, int kind)
{
	wst *W = e->w;

	if (W->flowfailed)
		return;
	if (kind != WF_ELSE || !W->flowdepth) {
		wflow_reject(e);
		return;
	}
	uint32_t token = W->flowstack[W->flowdepth - 1];
	if (W->flow[token].kind != WF_IF || W->flow[token].alternate != UINT32_MAX) {
		wflow_reject(e);
		return;
	}
	wflow_event *f = wf_event(e, WF_ELSE);
	if (f) {
		f->token = token;
		W->flow[token].alternate = W->nflow - 1;
	}
}

VPRIV void wflow_end(ecx *e)
{
	wst *W = e->w;

	if (W->flowfailed)
		return;
	if (!W->flowdepth || W->flowdepth != (uint32_t)W->depth) {
		wflow_reject(e);
		return;
	}
	uint32_t token = W->flowstack[--W->flowdepth];
	wflow_event *f = wf_event(e, WF_END);
	if (f) {
		f->token = token;
		W->flow[token].end = W->nflow - 1;
	}
}

VPRIV void wflow_branch(ecx *e, int depth, bool conditional)
{
	wst *W = e->w;

	if (W->flowfailed)
		return;
	if (depth < 0 || (uint32_t)depth >= W->flowdepth) {
		wflow_reject(e);
		return;
	}
	wflow_event *f = wf_event(e, conditional ? WF_BR_IF : WF_BR);
	if (f)
		f->token = W->flowstack[depth];
}

static bool wf_barrier(const wmem *m)
{
	return (m->kind == WM_FENCE && !m->control) || m->kind == WM_POISON;
}

VPRIV void wflow_optimize(ecx *e)
{
	wst *W = e->w;
	em_slot_cells *s = &W->cells;
	uint8_t *map = NULL, *bit = NULL, *dead = NULL;
	em_flow_transfer *t = NULL;
	em_flow_edge *edges = NULL;
	uint32_t stores[64] = { 0 }, bits = 0, ne = 0, blocks = W->nflow + 1;
	bool candidate = false;

	if (W->flowfailed || W->flowdepth || !W->flowjoins || W->flowstores < 2 ||
	    W->dyn || W->nsj || W->ngdx || W->memfailed || !s->valid)
		return;
	map = calloc(s->count, 1);
	bit = malloc(W->nmem);
	if (!map || !bit)
		goto done;
	for (uint32_t i = 1; i < s->count; i++) {
		if (s->flags[i])
			continue;
		if (bits == 64)
			goto done;
		map[i] = (uint8_t)++bits;
	}
	if (!bits)
		goto done;
	uint64_t all = bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
	for (uint32_t i = 0; i < W->nmem; i++) {
		wmem *m = &W->mem[i];
		bit[i] = UINT8_MAX;
		if (m->kind != WM_LOAD && m->kind != WM_STORE)
			continue;
		uint32_t cell = em_slot_find(s, m->id, (int64_t)m->off, m->size);
		if (!cell || !map[cell])
			continue;
		bit[i] = map[cell] - 1;
		if (m->kind == WM_STORE && ++stores[bit[i]] >= 2)
			candidate = true;
	}
	if (!candidate)
		goto done;
	/* Adjacency and solver storage are allocated only for an actual private
	 * repeated-store candidate after complete function/cell collection. */
	t = calloc(blocks, sizeof(*t));
	edges = malloc((size_t)blocks * 2 * sizeof(*edges));
	dead = calloc(W->nmem, 1);
	if (!t || !edges || !dead)
		goto done;
	for (uint32_t b = 0; b < blocks; b++) {
		uint32_t begin = b ? W->flow[b - 1].at : 0;
		uint32_t end = b < W->nflow ? W->flow[b].at : W->nmem;
		if (begin > end || end > W->nmem)
			goto done;
		for (uint32_t i = begin; i < end; i++) {
			const wmem *m = &W->mem[i];
			uint64_t v = bit[i] != UINT8_MAX ? UINT64_C(1) << bit[i] : 0;
			if (wf_barrier(m) || ((m->kind == WM_LOAD || m->kind == WM_STORE) && !v))
				t[b].use |= all & ~t[b].def;
			else if (m->kind == WM_LOAD)
				t[b].use |= v & ~t[b].def;
			else if (m->kind == WM_STORE)
				t[b].def |= v;
		}
		if (b == W->nflow) {
			/* This consumer only proves overwrites, never lifetime-end DSE. */
			t[b].exit = all;
			continue;
		}
		const wflow_event *f = &W->flow[b];
		uint32_t target = b + 1;
		bool fall = true;
		switch (f->kind) {
		case WF_BLOCK: case WF_END:
			break;
		case WF_IF:
			if (f->end == UINT32_MAX)
				goto done;
			target = (f->alternate == UINT32_MAX ? f->end : f->alternate) + 1;
			break;
		case WF_ELSE: case WF_BR: case WF_BR_IF:
			if (f->token >= W->nflow || W->flow[f->token].end == UINT32_MAX)
				goto done;
			target = W->flow[f->token].end + 1;
			fall = f->kind == WF_BR_IF;
			break;
		default:
			goto done;
		}
		if (target >= blocks)
			goto done;
		edges[ne++] = (em_flow_edge){ b, target };
		if (fall && target != b + 1)
			edges[ne++] = (em_flow_edge){ b, b + 1 };
	}
	uint32_t budget = 64 + W->nmem + 32 * (blocks + ne);
	if (budget > WFLOW_WORK)
		budget = WFLOW_WORK;
	if (!em_flow_build(&W->flowgraph, blocks, edges, ne, true) || !W->flowgraph.valid ||
	    !em_flow_liveness(&W->flowgraph, t, &W->flowlive, budget) || !W->flowlive.valid)
		goto done;
	for (uint32_t b = 0; b < blocks; b++) {
		uint32_t begin = b ? W->flow[b - 1].at : 0;
		uint32_t end = b < W->nflow ? W->flow[b].at : W->nmem;
		uint64_t live = W->flowlive.out[b];
		for (uint32_t i = end; i-- > begin;) {
			const wmem *m = &W->mem[i];
			uint64_t v = bit[i] != UINT8_MAX ? UINT64_C(1) << bit[i] : 0;
			if (wf_barrier(m) || ((m->kind == WM_LOAD || m->kind == WM_STORE) && !v))
				live |= all;
			else if (m->kind == WM_LOAD)
				live |= v;
			else if (m->kind == WM_STORE) {
				dead[i] = !(live & v);
				live &= ~v;
			}
		}
	}
	/* Publish only after convergence and complete staging. The renderer keeps
	 * operand evaluations and any local capture needed by forwarding. */
	for (uint32_t i = 0; i < W->nmem; i++)
		if (dead[i]) {
			W->mem[i].dead = true;
			W->memchanged = true;
		}
done:
	free(map);
	free(bit);
	free(dead);
	free(t);
	free(edges);
}
