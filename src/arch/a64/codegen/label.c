/* vcc code generator: emitting records, local labels and reachability */
#include "../../../codegen/codegen-internal.h"

/* ---------------- local labels and reachability ---------------- */

/* label l is placed: what is known is what every way in agrees on */
static void kv_join(codegen_context *e, uint32_t l)
{
	const codegen_known_values *st;

	if (!e->known_value_count || !e->label_reference_counts[l])
		return;
	if (!e->label_known_values_index[l] || e->label_known_values_index[l] >= CODEGEN_KNOWN_LABEL_PLACED) {
		e->known_values_valid = 0;
		return;
	}
	st = &e->known_value_states[e->label_known_values_index[l] - 1];
	if (!e->is_reachable) {
		e->known_values_valid = st->valid;
		memcpy(e->known_values, st->values, sizeof(e->known_values));
		return;
	}
	for (uint16_t m = e->known_values_valid; m; m &= m - 1) {
		int k = __builtin_ctz(m);

		if (!(st->valid >> k & 1) || st->values[k] != e->known_values[k])
			e->known_values_valid &= (uint16_t)~(1u << k);
	}
}

VPRIV void deflabel(codegen_context *e, uint32_t l)
{
	kv_join(e, l);
	if (e->label_reference_counts[l])
		e->is_reachable = true;
	if (!e->dry_run_depth)
		e->label_known_values_index[l] = !e->is_reachable ? CODEGEN_KNOWN_LABEL_DROPPED : e->known_values_valid ? CODEGEN_KNOWN_LABEL_KNOWN : CODEGEN_KNOWN_LABEL_PLACED;
	if (!e->is_reachable)
		return;
	put(e, I_LABEL)->label = l;
}

/* a loop head: the back edges come later */
VPRIV void deflabel_loop(codegen_context *e, uint32_t l, bool forced)
{
	e->known_values_valid = 0;
	if (forced)
		deflabel_forced(e, l);
	else
		deflabel(e, l);
}
