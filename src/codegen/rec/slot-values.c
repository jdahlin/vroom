/* Straight-line exact-cell value availability and store-observation facts.
 * Instruction mutation, register versions and control-flow discovery belong
 * to the target. This kernel allocates only reusable per-cell state.
 */
#include "../codegen-internal.h"

VPRIV bool record_slot_values_begin(record_slot_values *s, uint32_t cells)
{
	if (!cells || cells > UINT32_MAX / 2)
		return false;
	if (cells > s->capacity) {
		s->capacity = cells * 2;
		s->values = realloc(s->values, (size_t)s->capacity * sizeof(*s->values));
		if (!s->values)
			abort();
	}
	s->count = cells;
	memset(s->values, 0, (size_t)cells * sizeof(*s->values));
	return true;
}

VPRIV record_slot_value *record_slot_value_get(record_slot_values *s, uint32_t cell, uint32_t gen, bool load)
{
	record_slot_value *a = &s->values[cell];

	if (a->generation != gen)
		a->generation = gen, a->value = RECORD_SLOT_NO_VALUE, a->last = RECORD_SLOT_NO_STORE;
	if (load)
		a->last = RECORD_SLOT_NO_STORE;
	return a;
}

VPRIV bool record_slot_value_is_available(const record_slot_value *a, uint32_t version, uint32_t memory, bool escaped)
{
	return a->value != RECORD_SLOT_NO_VALUE && a->version == version && (!escaped || a->memory == memory);
}

VPRIV uint32_t record_slot_value_note(record_slot_value *a, bool load, bool escaped,
		uint32_t value, uint32_t version, uint32_t memory, uint32_t pos)
{
	uint32_t overwritten = RECORD_SLOT_NO_STORE;

	if (load) {
		a->loads++;
	} else {
		if (!escaped)
			overwritten = a->last;
		a->last = escaped ? RECORD_SLOT_NO_STORE : pos;
	}
	a->value = value;
	a->version = version;
	a->memory = memory;
	a->position = pos;
	return overwritten;
}

VPRIV bool record_slot_value_is_dead(const record_slot_values *s, uint32_t cell, bool escaped)
{
	return !escaped && !s->values[cell].loads;
}
