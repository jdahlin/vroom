/* Exact frame-cell optimization over typed emission events. Instruction text
 * remains opaque: only operations recorded at typed load/store sites change.
 * The serializer preserves every address and RHS evaluation and consumes the
 * same typed operand-stack inputs as the original operation.
 */
#include "wimpl.h"

#define WMAXMEM 65536u

static void *wm_grow(void *p, uint32_t *cap, uint32_t n, size_t size)
{
	if (n <= *cap)
		return p;
	*cap = n * 2;
	p = realloc(p, (size_t)*cap * size);
	if (!p)
		abort();
	return p;
}

VPRIV void wmem_begin(ecx *e)
{
	wst *W = e->w;

	W->memopt = e->c->o->opt && !e->E->noopt;
	W->memfailed = W->memchanged = false;
	W->memopaque = W->nmem = 0;
	W->nalloc = 1;
}

VPRIV void wmem_alloc(ecx *e, uint64_t off, uint64_t size)
{
	wst *W = e->w;

	if (!W->memopt || W->memfailed)
		return;
	if (W->nalloc == WMAXMEM || off > INT64_MAX || size > UINT32_MAX || size > INT64_MAX - off) {
		W->memfailed = true;
		return;
	}
	W->alloc = wm_grow(W->alloc, &W->capalloc, W->nalloc + 1, sizeof(*W->alloc));
	W->alloc[W->nalloc++] = (walloc){ off, size, false };
}

VPRIV uint32_t wmem_id(ecx *e, uint64_t off)
{
	wst *W = e->w;
	uint32_t lo = 1, hi = W->nalloc;

	if (!W->memopt || W->memfailed)
		return 0;
	while (lo < hi) {
		uint32_t mid = lo + (hi - lo) / 2;
		if (W->alloc[mid].off <= off)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo > 1 && off - W->alloc[lo - 1].off < W->alloc[lo - 1].size ? lo - 1 : 0;
}

/* Only for compiler-known direct frame addresses, never pointer provenance. */
VPRIV void wmem_lval(ecx *e, wlv *l, uint64_t off)
{
	l->cellid = wmem_id(e, off);
	l->celloff = off;
}

VPRIV void wmem_escape(ecx *e, uint32_t id)
{
	wst *W = e->w;

	if (id && id < W->nalloc && W->memopt && !W->memfailed)
		W->alloc[id].escaped = true;
	wmem_fence(e);
}

static wmem *wm_event(ecx *e, uint8_t kind)
{
	wst *W = e->w;

	if (!W->memopt || W->memfailed)
		return NULL;
	if (W->nmem == WMAXMEM) {
		W->memfailed = true;
		return NULL;
	}
	W->mem = wm_grow(W->mem, &W->capmem, W->nmem + 1, sizeof(*W->mem));
	wmem *m = &W->mem[W->nmem++];
	*m = (wmem){ .kind = kind, .source = UINT32_MAX, .cache = UINT32_MAX };
	return m;
}

VPRIV void wmem_fence(ecx *e)
{
	wst *W = e->w;

	if (!W->nmem || W->mem[W->nmem - 1].kind == WM_FENCE)
		return;
	(void)wm_event(e, WM_FENCE);
}

VPRIV void wmem_poison(ecx *e, const wlv *l, uint64_t size)
{
	wmem *m;

	wmem_fence(e);
	if (!l->cellid || !size || size > UINT32_MAX)
		return;
	m = wm_event(e, WM_POISON);
	if (m) {
		m->off = l->celloff;
		m->size = (uint32_t)size;
	}
}

VPRIV void wmem_access(ecx *e, const wlv *l, qty t, bool store, size_t begin)
{
	wst *W = e->w;
	uint64_t sz;
	int type;
	uint32_t id = l->cellid;
	wmem *m;

	if (!W->memopt || W->memfailed || W->nalloc == 1)
		return;
	sz = tsize(e->c, t);
	type = wty(e->c, t);
	if (l->vol || (QQ(t) & Q_VOLATILE) || (T(e->c, t)->flags & TF_ATOMIC)) {
		wmem_poison(e, l, sz);
		return;
	}
	if (!id || W->memopaque || (sz != 4 && sz != 8) ||
	    (type != WT_I32 && type != WT_I64 && type != WT_F32 && type != WT_F64)) {
		if (id)
			wmem_poison(e, l, sz);
		else
			wmem_fence(e);
		return;
	}
	/* A member access outside its allocation must not acquire a neighboring ID. */
	const walloc *a = &W->alloc[id];
	if (l->celloff < a->off || l->celloff - a->off > a->size || sz > a->size - (l->celloff - a->off)) {
		W->memfailed = true;
		return;
	}
	m = wm_event(e, store ? WM_STORE : WM_LOAD);
	if (m) {
		m->begin = begin;
		m->end = W->b.n;
		m->id = id;
		m->off = l->celloff;
		m->size = (uint32_t)sz;
		m->type = (uint8_t)type;
	}
}

VPRIV void wmem_optimize(ecx *e)
{
	wst *W = e->w;
	em_slot_cells *s = &W->cells;
	uint32_t gen = 1, caches = 0;

	if (!W->memopt || W->memfailed || W->dyn || W->nsj || !W->nmem || !em_slot_begin(s, W->nalloc))
		return;
	for (uint32_t id = 1; id < W->nalloc; id++)
		if (W->alloc[id].escaped)
			em_slot_escape(s, id);
	for (uint32_t i = 0; i < W->nmem; i++) {
		wmem *m = &W->mem[i];
		if (m->kind != WM_FENCE)
			em_slot_collect(s, m->id, (int64_t)m->off, m->size);
	}
	if (!em_slot_finish(s) || !em_slot_values_begin(&W->values, s->count))
		return;
	for (uint32_t i = 0; i < W->nmem; i++) {
		wmem *m = &W->mem[i];
		if (m->kind == WM_FENCE || m->kind == WM_POISON) {
			gen++;
			continue;
		}
		uint32_t cell = em_slot_find(s, m->id, (int64_t)m->off, m->size);
		if (!cell || s->flags[cell]) {
			gen++;
			continue;
		}
		bool load = m->kind == WM_LOAD;
		em_slot_value *a = em_slot_value_get(&W->values, cell, gen, load);
		if (load && em_slot_value_available(a, 0, 0, false) && W->mem[a->value].type == m->type) {
			m->source = a->value;
			if (!W->mem[a->value].capture)
				caches++;
			W->mem[a->value].capture = true;
			W->memchanged = true;
			continue;
		}
		uint32_t prev = em_slot_value_record(a, load, false, i, 0, 0, i);
		if (prev != EM_SLOT_NO_STORE) {
			W->mem[prev].dead = true;
			W->memchanged = true;
		}
	}
	if (caches > WMAXLOC - W->nloc) {
		W->memchanged = false;
		return;
	}
	for (uint32_t i = 0; i < W->nmem; i++)
		if (W->mem[i].capture)
			W->mem[i].cache = wnewloc(e, W->mem[i].type);
}

VPRIV void wmem_render(ecx *e, tbuf *out)
{
	wst *W = e->w;
	size_t pos = 0;

	if (!W->memchanged) {
		tb_put(out, W->b.p, W->b.n);
		return;
	}
	for (uint32_t i = 0; i < W->nmem; i++) {
		const wmem *m = &W->mem[i];
		if (!m->dead && !m->capture && m->source == UINT32_MAX)
			continue;
		tb_put(out, W->b.p + pos, m->begin - pos);
		if (m->source != UINT32_MAX) {
			tb_s(out, "\tdrop\n\tlocal.get\t");
			tb_u(out, W->mem[m->source].cache);
			tb_c(out, '\n');
		} else if (m->dead) {
			if (m->capture) {
				tb_s(out, "\tlocal.set\t");
				tb_u(out, m->cache);
				tb_s(out, "\n\tdrop\n");
			} else tb_s(out, "\tdrop\n\tdrop\n");
		} else {
			if (m->capture && m->kind == WM_STORE) {
				tb_s(out, "\tlocal.tee\t");
				tb_u(out, m->cache);
				tb_c(out, '\n');
			}
			tb_put(out, W->b.p + m->begin, m->end - m->begin);
			if (m->capture && m->kind == WM_LOAD) {
				tb_s(out, "\tlocal.tee\t");
				tb_u(out, m->cache);
				tb_c(out, '\n');
			}
		}
		pos = m->end;
	}
	tb_put(out, W->b.p + pos, W->b.n - pos);
}
