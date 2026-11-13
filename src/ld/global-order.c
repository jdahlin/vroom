/* The order of global symbols: first occurrence in file order, then script insertions. */

/* global order: first occurrence in file order, then script insertions */
static void ld_global_order_build(void)
{
	gorder.n = 0;
	for (uint32_t i = 0; i < objs.n; i++) {
		ld_object *o = objs.v[i];
		if (!o->buf)
			continue;
		for (uint32_t s = o->firstg; s < o->nsym; s++) {
			uint32_t g = o->gs[s - o->firstg];
			if (ld_key_load(&gsyms[g].first) == ld_symbol_key(o->idx, s))
				VVEC_PUSH(gorder, g, ld_region_alloc);
		}
	}
	for (uint32_t i = 0; i < script_new.n; i++)
		VVEC_PUSH(gorder, script_new.v[i], ld_region_alloc);
}
