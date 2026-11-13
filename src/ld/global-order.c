/* The order of global symbols: first occurrence in file order, then script insertions. */

/* global order: first occurrence in file order, then script insertions */
static void ld_global_order_build(void)
{
	ctx->symbols.order.n = 0;
	for (uint32_t i = 0; i < ctx->inputs.objects.n; i++) {
		ld_object *o = ctx->inputs.objects.v[i];
		if (!o->buf)
			continue;
		for (uint32_t s = o->firstg; s < o->nsym; s++) {
			uint32_t g = o->gs[s - o->firstg];
			if (ld_key_load(&ctx->symbols.globals[g].first) == ld_symbol_key(o->idx, s))
				VVEC_PUSH(ctx->symbols.order, g, ld_region_alloc);
		}
	}
	for (uint32_t i = 0; i < ctx->symbols.script_inserted.n; i++)
		VVEC_PUSH(ctx->symbols.order, ctx->symbols.script_inserted.v[i], ld_region_alloc);
}
