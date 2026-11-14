/* Exact allocation ranges for load forwarding and dead-store elimination.
 * No instruction decoding, liveness, or target operand policy lives here.
 */
#include "../codegen-internal.h"

static void *record_slot_grow(void *p, uint32_t *cap, uint32_t n, size_t size)
{
	if (n <= *cap)
		return p;
	*cap = n * 2;
	p = realloc(p, (size_t)*cap * size);
	if (!p)
		abort();
	return p;
}

static int record_slot_compare(const void *a, const void *b)
{
	const record_slot_range *x = a, *y = b;

	if (x->id != y->id)
		return x->id < y->id ? -1 : 1;
	if (x->offset != y->offset)
		return x->offset < y->offset ? -1 : 1;
	return x->size < y->size ? -1 : x->size > y->size;
}

/* Exact cell sets are usually one or two aggregate fields. Avoid libc's
 * general sorting machinery for bounded tiny sets; retain qsort above 16. */
static void record_slot_sort(record_slot_range *v, uint32_t n)
{
	if (n > 16) {
		qsort(v, n, sizeof(*v), record_slot_compare);
		return;
	}
	for (uint32_t i = 1; i < n; i++) {
		record_slot_range key = v[i];
		uint32_t j = i;

		while (j && record_slot_compare(&key, &v[j - 1]) < 0) {
			v[j] = v[j - 1];
			j--;
		}
		v[j] = key;
	}
}

static bool record_slot_hits(const record_slot_cells *s, int64_t off, uint32_t size)
{
	int64_t end = off + size;
	uint32_t lo = 0, hi = s->write_count;

	while (lo < hi) {
		uint32_t mid = (lo + hi) / 2;

		if (s->writes[mid].offset < end)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo > 0 && s->write_ends[lo - 1] > off;
}

VPRIV bool record_slot_begin(record_slot_cells *s, uint32_t allocations)
{
	s->allocation_count = allocations;
	s->cell_count = s->write_count = s->count = 0;
	s->has_multi_range = false;
	s->valid = allocations > 0 && allocations <= 0x10000;
	if (!s->valid)
		return false;
	s->allocations = record_slot_grow(s->allocations, &s->allocation_capacity, allocations, sizeof(*s->allocations));
	memset(s->allocations, 0, (size_t)allocations * sizeof(*s->allocations));
	return true;
}

VPRIV void record_slot_access(record_slot_cells *s, uint32_t id, int64_t off, uint32_t size)
{
	if (!s->valid)
		return;
	if (id >= s->allocation_count || !size || off > INT64_MAX - size) {
		s->valid = false;
		return;
	}
	if (!id) {
		/* Poison classification needs the range union, not multiplicity. */
		if (s->write_count && s->writes[s->write_count - 1].offset == off && s->writes[s->write_count - 1].size == size)
			return;
		s->writes = record_slot_grow(s->writes, &s->write_capacity, s->write_count + 1, sizeof(*s->writes));
		s->writes[s->write_count++] = (record_slot_range){ 0, off, size, false, false };
	} else {
		record_slot_allocation *a = &s->allocations[id];

		if (!a->state)
			a->state = 1, a->offset = off, a->size = size;
		else if (a->state == 1 && (a->offset != off || a->size != size))
			a->state = 2, s->has_multi_range = true;
	}
}

VPRIV void record_slot_escape(record_slot_cells *s, uint32_t id)
{
	if (!s->valid)
		return;
	if (id >= s->allocation_count)
		s->valid = false;
	else
		s->allocations[id].escaped = true;
}

VPRIV void record_slot_detail(record_slot_cells *s, uint32_t id, int64_t off, uint32_t size)
{
	if (!s->valid)
		return;
	if (id >= s->allocation_count || !size || off > INT64_MAX - size) {
		s->valid = false;
		return;
	}
	if (!id || s->allocations[id].state != 2)
		return;
	s->cells = record_slot_grow(s->cells, &s->cell_capacity, s->cell_count + 1, sizeof(*s->cells));
	s->cells[s->cell_count++] = (record_slot_range){ id, off, size, false, false };
}

/* Retain the first exact range only when a distinct second range appears.
 * Repeated first-range accesses need no duplicate details: finish deduplicates
 * them anyway. Unknown physical ranges still use the ordinary poison path. */
VPRIV void record_slot_collect(record_slot_cells *s, uint32_t id, int64_t off, uint32_t size)
{
	if (!id) {
		record_slot_access(s, id, off, size);
		return;
	}
	if (!s->valid)
		return;
	if (id >= s->allocation_count || !size || off > INT64_MAX - size) {
		s->valid = false;
		return;
	}
	record_slot_allocation *a = &s->allocations[id];
	uint8_t before = a->state;

	if (!before) {
		a->state = 1, a->offset = off, a->size = size;
		return;
	}
	if (before == 1 && a->offset == off && a->size == size)
		return;
	a->state = 2, s->has_multi_range = true;
	if (before == 1)
		record_slot_detail(s, id, a->offset, a->size);
	record_slot_detail(s, id, off, size);
}

VPRIV bool record_slot_finish(record_slot_cells *s)
{
	uint32_t na = s->allocation_count, nm = s->cell_count;
	bool any = false;

	if (!s->valid)
		return false;
	for (uint32_t id = 1; id < na && !any; id++)
		any = s->allocations[id].state != 0;
	if (!any)
		return false;
	if (nm) {
		uint32_t u = 0;

		record_slot_sort(s->cells, nm);
		for (uint32_t k = 0; k < nm; k++)
			if (!u || record_slot_compare(&s->cells[k], &s->cells[u - 1]))
				s->cells[u++] = s->cells[k];
		s->cell_count = nm = u;
		for (uint32_t k = 0, g; k < nm; k = g) {
			int64_t maxend = INT64_MIN;

			for (g = k; g < nm && s->cells[g].id == s->cells[k].id; g++)
				;
			s->allocations[s->cells[k].id].multi_begin = k, s->allocations[s->cells[k].id].multi_count = g - k;
			for (uint32_t q = k; q < g; q++) {
				int64_t end = s->cells[q].offset + s->cells[q].size;

				s->cells[q].bad = s->cells[q].offset < maxend || (q + 1 < g && s->cells[q + 1].offset < end);
				if (end > maxend)
					maxend = end;
			}
		}
	}
	if (s->write_count) {
		record_slot_sort(s->writes, s->write_count);
		s->write_ends = record_slot_grow(s->write_ends, &s->write_end_capacity, s->write_count, sizeof(*s->write_ends));
		for (uint32_t k = 0; k < s->write_count; k++) {
			int64_t end = s->writes[k].offset + s->writes[k].size;

			s->write_ends[k] = k && s->write_ends[k - 1] > end ? s->write_ends[k - 1] : end;
		}
	}
	s->count = na + nm;
	s->flags = record_slot_grow(s->flags, &s->flag_capacity, s->count, 1);
	s->flags[0] = RECORD_SLOT_BAD;
	for (uint32_t id = 1; id < na; id++) {
		const record_slot_allocation *a = &s->allocations[id];

		s->flags[id] = a->state == 1 ?
			(uint8_t)((s->write_count && record_slot_hits(s, a->offset, a->size)) | a->escaped << 1) : RECORD_SLOT_BAD;
	}
	for (uint32_t q = 0; q < nm; q++)
		s->flags[na + q] = (uint8_t)((s->cells[q].bad || (s->write_count && record_slot_hits(s, s->cells[q].offset, s->cells[q].size))) |
					    s->allocations[s->cells[q].id].escaped << 1);
	return true;
}

VPRIV uint32_t record_slot_find(const record_slot_cells *s, uint32_t id, int64_t off, uint32_t size)
{
	if (!id || id >= s->allocation_count)
		return 0;
	const record_slot_allocation *a = &s->allocations[id];
	if (a->state == 1)
		return a->offset == off && a->size == size ? id : 0;
	record_slot_range key = { id, off, size, false, false };
	uint32_t lo = a->multi_begin, hi = lo + a->multi_count;

	while (lo < hi) {
		uint32_t mid = (lo + hi) / 2;

		if (record_slot_compare(&s->cells[mid], &key) < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo < a->multi_begin + a->multi_count && !record_slot_compare(&s->cells[lo], &key) ? s->allocation_count + lo : 0;
}
