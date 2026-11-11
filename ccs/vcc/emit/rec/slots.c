/* Exact allocation ranges for load forwarding and dead-store elimination.
 * No instruction decoding, liveness, or target operand policy lives here.
 */
#include "../emitimpl.h"

static void *em_slot_grow(void *p, uint32_t *cap, uint32_t n, size_t size)
{
	if (n <= *cap)
		return p;
	*cap = n * 2;
	p = realloc(p, (size_t)*cap * size);
	if (!p)
		abort();
	return p;
}

static int em_slot_cmp(const void *a, const void *b)
{
	const em_slot_range *x = a, *y = b;

	if (x->id != y->id)
		return x->id < y->id ? -1 : 1;
	if (x->off != y->off)
		return x->off < y->off ? -1 : 1;
	return x->size < y->size ? -1 : x->size > y->size;
}

/* Exact cell sets are usually one or two aggregate fields. Avoid libc's
 * general sorting machinery for bounded tiny sets; retain qsort above 16. */
static void em_slot_sort(em_slot_range *v, uint32_t n)
{
	if (n > 16) {
		qsort(v, n, sizeof(*v), em_slot_cmp);
		return;
	}
	for (uint32_t i = 1; i < n; i++) {
		em_slot_range key = v[i];
		uint32_t j = i;

		while (j && em_slot_cmp(&key, &v[j - 1]) < 0) {
			v[j] = v[j - 1];
			j--;
		}
		v[j] = key;
	}
}

static bool em_slot_hits(const em_slot_cells *s, int64_t off, uint32_t size)
{
	int64_t end = off + size;
	uint32_t lo = 0, hi = s->nw;

	while (lo < hi) {
		uint32_t mid = (lo + hi) / 2;

		if (s->w[mid].off < end)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo > 0 && s->wend[lo - 1] > off;
}

VPRIV bool em_slot_begin(em_slot_cells *s, uint32_t allocations)
{
	s->na = allocations;
	s->nm = s->nw = s->count = 0;
	s->multi = false;
	s->valid = allocations > 0 && allocations <= 0x10000;
	if (!s->valid)
		return false;
	s->a = em_slot_grow(s->a, &s->capa, allocations, sizeof(*s->a));
	memset(s->a, 0, (size_t)allocations * sizeof(*s->a));
	return true;
}

VPRIV void em_slot_access(em_slot_cells *s, uint32_t id, int64_t off, uint32_t size)
{
	if (!s->valid)
		return;
	if (id >= s->na || !size || off > INT64_MAX - size) {
		s->valid = false;
		return;
	}
	if (!id) {
		s->w = em_slot_grow(s->w, &s->capw, s->nw + 1, sizeof(*s->w));
		s->w[s->nw++] = (em_slot_range){ 0, off, size, false, false };
	} else {
		em_slot_alloc *a = &s->a[id];

		if (!a->state)
			a->state = 1, a->off = off, a->size = size;
		else if (a->state == 1 && (a->off != off || a->size != size))
			a->state = 2, s->multi = true;
	}
}

VPRIV void em_slot_escape(em_slot_cells *s, uint32_t id)
{
	if (!s->valid)
		return;
	if (id >= s->na)
		s->valid = false;
	else
		s->a[id].esc = true;
}

VPRIV void em_slot_detail(em_slot_cells *s, uint32_t id, int64_t off, uint32_t size)
{
	if (!s->valid)
		return;
	if (id >= s->na || !size || off > INT64_MAX - size) {
		s->valid = false;
		return;
	}
	if (!id || s->a[id].state != 2)
		return;
	s->c = em_slot_grow(s->c, &s->capc, s->nm + 1, sizeof(*s->c));
	s->c[s->nm++] = (em_slot_range){ id, off, size, false, false };
}

VPRIV bool em_slot_finish(em_slot_cells *s)
{
	uint32_t na = s->na, nm = s->nm;
	bool any = false;

	if (!s->valid)
		return false;
	for (uint32_t id = 1; id < na && !any; id++)
		any = s->a[id].state != 0;
	if (!any)
		return false;
	if (nm) {
		uint32_t u = 0;

		em_slot_sort(s->c, nm);
		for (uint32_t k = 0; k < nm; k++)
			if (!u || em_slot_cmp(&s->c[k], &s->c[u - 1]))
				s->c[u++] = s->c[k];
		s->nm = nm = u;
		for (uint32_t k = 0, g; k < nm; k = g) {
			int64_t maxend = INT64_MIN;

			for (g = k; g < nm && s->c[g].id == s->c[k].id; g++)
				;
			s->a[s->c[k].id].mb = k, s->a[s->c[k].id].mn = g - k;
			for (uint32_t q = k; q < g; q++) {
				int64_t end = s->c[q].off + s->c[q].size;

				s->c[q].bad = s->c[q].off < maxend || (q + 1 < g && s->c[q + 1].off < end);
				if (end > maxend)
					maxend = end;
			}
		}
	}
	if (s->nw) {
		em_slot_sort(s->w, s->nw);
		s->wend = em_slot_grow(s->wend, &s->capwe, s->nw, sizeof(*s->wend));
		for (uint32_t k = 0; k < s->nw; k++) {
			int64_t end = s->w[k].off + s->w[k].size;

			s->wend[k] = k && s->wend[k - 1] > end ? s->wend[k - 1] : end;
		}
	}
	s->count = na + nm;
	s->flags = em_slot_grow(s->flags, &s->capflags, s->count, 1);
	s->flags[0] = EM_SLOT_BAD;
	for (uint32_t id = 1; id < na; id++) {
		const em_slot_alloc *a = &s->a[id];

		s->flags[id] = a->state == 1 ?
			(uint8_t)((s->nw && em_slot_hits(s, a->off, a->size)) | a->esc << 1) : EM_SLOT_BAD;
	}
	for (uint32_t q = 0; q < nm; q++)
		s->flags[na + q] = (uint8_t)((s->c[q].bad || (s->nw && em_slot_hits(s, s->c[q].off, s->c[q].size))) |
					    s->a[s->c[q].id].esc << 1);
	return true;
}

VPRIV uint32_t em_slot_find(const em_slot_cells *s, uint32_t id, int64_t off, uint32_t size)
{
	if (!id || id >= s->na)
		return 0;
	const em_slot_alloc *a = &s->a[id];
	if (a->state == 1)
		return a->off == off && a->size == size ? id : 0;
	em_slot_range key = { id, off, size, false, false };
	uint32_t lo = a->mb, hi = lo + a->mn;

	while (lo < hi) {
		uint32_t mid = (lo + hi) / 2;

		if (em_slot_cmp(&s->c[mid], &key) < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo < a->mb + a->mn && !em_slot_cmp(&s->c[lo], &key) ? s->na + lo : 0;
}
