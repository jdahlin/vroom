/* vcc arm64 code generator: labels and branches, with the known-value edges they carry */
#include "../../../codegen/codegen-internal.h"

VPRIV uint32_t a64_label_new(codegen_context *e)
{
	if (e->label_count == e->label_capacity) {
		codegen_fatal_report(e, 0, "function too large (more than %u labels)", e->label_capacity);
	}
	e->label_reference_counts[e->label_count] = 0;
	e->label_known_values_index[e->label_count] = 0;
	return e->label_count++;
}

static void a64_label_back_edge_fail(codegen_context *e, uint32_t l)
{
	codegen_fatal_report(e, e->current_location, "internal: jump back to label %u, %s", l,
	       e->label_known_values_index[l] == CODEGEN_KNOWN_LABEL_DROPPED ? "dropped as unreachable" : "placed with known values");
}

/* a reachable edge to l (call before counting it in lrefc) */
VPRIV void a64_label_edge_add(codegen_context *e, uint32_t l)
{
	codegen_known_values *st;

	if (e->label_known_values_index[l] >= CODEGEN_KNOWN_LABEL_PLACED) {
		if (e->label_known_values_index[l] != CODEGEN_KNOWN_LABEL_PLACED && !e->dry_run_depth)
			a64_label_back_edge_fail(e, l);
		return;
	}
	if (!e->known_value_count || !e->is_reachable)
		return;
	if (!e->label_known_values_index[l]) {
		if (e->known_value_state_count == CODEGEN_KNOWN_VALUES_CAPACITY) {
			e->known_value_count = 0, e->known_values_valid = 0, e->array_element_count = 0;	/* out of room: stop tracking */
			return;
		}
		st = &e->known_value_states[e->known_value_state_count++];
		e->label_known_values_index[l] = e->known_value_state_count;
		st->valid = e->label_reference_counts[l] ? 0 : e->known_values_valid;	/* earlier edges were untracked */
		memcpy(st->values, e->known_values, sizeof(st->values));
		return;
	}
	st = &e->known_value_states[e->label_known_values_index[l] - 1];
	for (uint16_t m = st->valid; m; m &= m - 1) {
		int k = __builtin_ctz(m);

		if (!(e->known_values_valid >> k & 1) || st->values[k] != e->known_values[k])
			st->valid &= (uint16_t)~(1u << k);
	}
}

/* a label that is reachable from elsewhere whatever the counts say */
VPRIV void a64_label_define_forced(codegen_context *e, uint32_t l)
{
	e->known_values_valid = 0;
	e->is_reachable = true;
	if (!e->dry_run_depth)
		e->label_known_values_index[l] = CODEGEN_KNOWN_LABEL_PLACED;
	a64_record_emit(e, A64_OP_LABEL)->label = l;
}

VPRIV void a64_jump_emit(codegen_context *e, uint32_t l)
{
	if (!e->is_reachable)
		return;
	a64_label_edge_add(e, l);
	a64_record_emit(e, A64_OP_B)->label = l;
	e->label_reference_counts[l]++;
	e->is_reachable = false;
}

VPRIV void a64_branch_condition_emit(codegen_context *e, int cond, uint32_t l)
{
	a64_record *i;

	if (!e->is_reachable)
		return;
	a64_label_edge_add(e, l);
	i = a64_record_emit(e, A64_OP_BC);
	i->condition = (uint8_t)cond;
	i->label = l;
	e->label_reference_counts[l]++;
}

VPRIV void a64_branch_compare_zero_emit(codegen_context *e, bool nz, int x, int r, uint32_t l)
{
	a64_record *i;

	if (!e->is_reachable)
		return;
	a64_label_edge_add(e, l);
	i = a64_record_emit(e, nz ? A64_OP_CBNZ : A64_OP_CBZ);
	i->size = (uint8_t)x, i->rd = (uint8_t)r;
	i->label = l;
	e->label_reference_counts[l]++;
}

/* tbz, tbnz: bit of register r (finish_function relaxes one whose label may be beyond 32 KB) */
VPRIV void a64_branch_test_bit_emit(codegen_context *e, bool nz, int r, int bit, uint32_t l)
{
	a64_record *i;

	if (!e->is_reachable)
		return;
	a64_label_edge_add(e, l);
	i = a64_record_emit(e, nz ? A64_OP_TBNZ : A64_OP_TBZ);
	i->size = bit >= 32, i->rd = (uint8_t)r, i->shift = (uint8_t)bit;
	i->label = l;
	e->label_reference_counts[l]++;
}
