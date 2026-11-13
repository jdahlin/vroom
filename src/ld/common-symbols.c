/* lld's replaceCommonSymbols: a NOBITS section per common symbol. */

/* lld replaceCommonSymbols: each common symbol one NOBITS "COMMON" section
 * (after every file's sections), of the largest size and alignment among its
 * commons, in the first file with that size; placed in the order of the files
 * that have commons, each file's globals in order, at the first mention */
static void ld_common_symbols_replace(void)
{
	bool any = false;

	for (uint32_t g = 0; g < atomic_load(&ngsyms) && !any; g++)
		any = gsyms[g].common && gsyms[g].state == LD_SYMBOL_STATE_OBJECT;
	if (!any)
		return;
	for (uint32_t g = 0; g < atomic_load(&ngsyms); g++)
		if (gsyms[g].common && gsyms[g].state == LD_SYMBOL_STATE_OBJECT)
			gsyms[g].size = 0, gsyms[g].val = 1, gsyms[g].obj = LD_NONE;
	for (uint32_t i = 0; i < nobj_real; i++) {
		ld_object *o = objs.v[i];

		for (uint32_t k = o->buf ? o->firstg : 0; o->buf && k < o->nsym; k++) {
			const elf_symbol *y = &o->sym[k];
			ld_global_symbol *s = &gsyms[o->gs[k - o->firstg]];

			if (y->st_shndx != SHN_COMMON || !s->common || s->state != LD_SYMBOL_STATE_OBJECT)
				continue;
			if (y->st_value > s->val)
				s->val = y->st_value;	/* the alignment, until placed */
			if (s->obj == LD_NONE || y->st_size > s->size)
				s->obj = i, s->symidx = k, s->size = y->st_size;
		}
	}
	for (uint32_t i = 0; i < nobj_real; i++) {
		ld_object *o = objs.v[i];
		bool has = false;

		for (uint32_t k = o->buf ? o->firstg : 0; o->buf && k < o->nsym && !has; k++)
			has = o->sym[k].st_shndx == SHN_COMMON;
		/* every global the file names, references too, in its order */
		for (uint32_t k = o->firstg; has && k < o->nsym; k++) {
			ld_global_symbol *s = &gsyms[o->gs[k - o->firstg]];

			if (!s->common || s->state != LD_SYMBOL_STATE_OBJECT || s->sec != LD_NONE)
				continue;
			uint32_t is = ld_input_section_create("COMMON", LD_SECTION_KIND_NORMAL, SHT_NOBITS, SHF_ALLOC | SHF_WRITE,
					       s->val ? (uint32_t)s->val : 1);

			isecs.v[is].size = s->size;
			isecs.v[is].obj = s->obj;
			s->sec = is;
			s->val = 0;
		}
	}
}
