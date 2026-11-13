/* The order of global symbols: first occurrence in file order, then script insertions. */

#include "ld-internal.h"

/* global order: first occurrence in file order, then script insertions */
VPRIV void ld_global_order_build(struct ld_context *ctx)
{
	ctx->symbols.order.n = 0;
	for (uint32_t i = 0; i < ctx->inputs.objects.n; i++) {
		ld_object *o = ctx->inputs.objects.v[i];
		if (!o->file_bytes)
			continue;
		for (uint32_t s = o->first_global_index; s < o->symbol_count; s++) {
			uint32_t g = o->global_slots[s - o->first_global_index];
			if (ld_key_load(&ctx->symbols.globals[g].first_occurrence) == ld_symbol_key(o->index, s))
				VVEC_PUSH(ctx->symbols.order, g, ld_region_alloc);
		}
	}
	for (uint32_t i = 0; i < ctx->symbols.script_inserted.n; i++)
		VVEC_PUSH(ctx->symbols.order, ctx->symbols.script_inserted.v[i], ld_region_alloc);
}
