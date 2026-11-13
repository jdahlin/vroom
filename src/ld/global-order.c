/* The order of global symbols: first occurrence in file order, then script insertions. */

/* global order: first occurrence in file order, then script insertions */
static void build_gorder(void)
{
	gorder.n = 0;
	for (uint32_t i = 0; i < objs.n; i++) {
		Obj *o = objs.v[i];
		if (!o->buf)
			continue;
		for (uint32_t s = o->firstg; s < o->nsym; s++) {
			uint32_t g = o->gs[s - o->firstg];
			if (kget(&gsyms[g].first) == symkey(o->idx, s))
				VVEC_PUSH(gorder, g, alloc);
		}
	}
	for (uint32_t i = 0; i < script_new.n; i++)
		VVEC_PUSH(gorder, script_new.v[i], alloc);
}
