/* Symbol resolution: COMDAT groups, each object's definitions and references into the global table, duplicate checks and symbol finalisation. */

/* ---------------- symbol resolution ---------------- */

static inline uint64_t ld_symbol_key(uint32_t obj, uint32_t idx) { return (uint64_t)obj << 32 | idx; }

/* lld: the first COMDAT group of a signature (in file order) is kept; a later
 * one's members are dropped.  Their definitions still take part in resolution
 * (lld postParse); one that wins is demoted (ld_symbols_demote), and relocations
 * against it fail as lld's "refers to a symbol in a discarded section" */
static void ld_comdat_groups_deduplicate(struct ld_context *ctx)
{
	struct { const char *name; uint32_t h; } *tab = NULL;
	uint32_t mask = 0, n = 0;

	for (uint32_t i = 0; i < ctx->inputs.objects.n; i++) {
		ld_object *o = ctx->inputs.objects.v[i];

		for (uint32_t s = 0; o->buf && s < o->nsh; s++) {
			const elf_section_header *h = &o->sh[s];
			const uint32_t *w = (const uint32_t *)(o->buf + h->sh_offset);

			if (h->sh_type != SHT_GROUP)
				continue;
			/* members in a ring, for --gc-sections (lld nextInSectionGroup) */
			for (uint64_t k = 1; k < h->sh_size / 4; k++)
				ctx->inputs.sections.v[o->isec0 + w[k]].gnext = o->isec0 + w[k + 1 < h->sh_size / 4 ? k + 1 : 1] + 1;
			if (!(w[0] & 1))	/* GRP_COMDAT */
				continue;
			const char *name = o->str + o->sym[h->sh_info].st_name;
			uint32_t len = strlen(name), hh = ld_name_hash(name, len);

			if (!tab || (n + 1) * 2 > mask) {
				uint32_t m = tab ? (mask + 1) * 2 : 1024;
				__typeof__(tab) nt = calloc(m, sizeof *tab);

				for (uint32_t k = 0; tab && k <= mask; k++)
					if (tab[k].name) {
						uint32_t j = tab[k].h & (m - 1);

						while (nt[j].name)
							j = (j + 1) & (m - 1);
						nt[j] = tab[k];
					}
				free(tab);
				tab = nt;
				mask = m - 1;
			}
			uint32_t j = hh & mask;
			bool seen = false;

			for (; tab[j].name; j = (j + 1) & mask)
				if (tab[j].h == hh && !strcmp(tab[j].name, name)) {
					seen = true;
					break;
				}
			if (!seen) {
				tab[j].name = name;
				tab[j].h = hh;
				n++;
				continue;
			}
			for (uint64_t k = 1; k < h->sh_size / 4; k++) {
				ld_input_section *is = &ctx->inputs.sections.v[o->isec0 + w[k]];

				is->kind = LD_SECTION_KIND_NONE;
				is->dropped = true;
			}
		}
	}
	free(tab);
}

static void ld_object_symbols_resolve(void *arg, size_t i)
{
	struct ld_context *ctx = arg;
	ld_object *o = ctx->inputs.objects.v[i];
	if (!o->buf)
		return;
	for (uint32_t s = o->firstg; s < o->nsym; s++) {
		const elf_symbol *y = &o->sym[s];
		uint8_t bind = y->st_info >> 4;
		if (bind == STB_LOCAL) {
			ld_error_report(ctx, "%s: local symbol after sh_info", o->path);
			continue;
		}
		uint32_t g = ld_global_name_intern(ctx, o->str + y->st_name);
		o->gs[s - o->firstg] = g;
		ld_global_symbol *gs = &ctx->symbols.globals[g];
		uint64_t k = ld_symbol_key(o->idx, s);
		ld_atomic_u64_min(&gs->first, k);
		ld_atomic_u8_min(&gs->visrank, ld_visibility_rank(y->st_other));
		if (y->st_shndx == SHN_COMMON && ctx->options.relocatable)
			ld_error_report(ctx, "%s: common symbol %s not supported with -r", o->path, o->str + y->st_name);
		else if (y->st_shndx == SHN_UNDEF) {
			if (bind != STB_WEAK && !atomic_load_explicit(&gs->strongref, memory_order_relaxed))
				atomic_store_explicit(&gs->strongref, 1, memory_order_relaxed);
		} else
			/* lld Symbol::compare: a strong definition beats a common one, which beats a weak one */
			ld_atomic_u64_min(&gs->def, (uint64_t)(bind == STB_WEAK ? 2 : y->st_shndx == SHN_COMMON) << 62 | k);
	}
}

static void ld_duplicate_symbols_check(void *arg, size_t i)
{
	struct ld_context *ctx = arg;
	ld_object *o = ctx->inputs.objects.v[i];
	if (!o->buf)
		return;
	for (uint32_t s = o->firstg; s < o->nsym; s++) {
		const elf_symbol *y = &o->sym[s];
		if (y->st_shndx == SHN_UNDEF || y->st_shndx == SHN_COMMON || (y->st_info >> 4) == STB_WEAK)
			continue;
		ld_global_symbol *g = &ctx->symbols.globals[o->gs[s - o->firstg]];
		uint64_t d = ld_key_load(&g->def);
		if ((d >> 62) == 0 && d != ld_symbol_key(o->idx, s)) {
			ld_object *first = ctx->inputs.objects.v[(d >> 32) & 0x3fffffff];
			ld_error_report(ctx, "duplicate symbol: %s\n>>> defined in %s\n>>> defined in %s",
			      o->str + y->st_name, first->path, o->path);
		}
	}
}

/* fill state/value of every global from its winning definition */
static void ld_global_symbol_finish(void *arg, size_t i)
{
	struct ld_context *ctx = arg;
	uint32_t g = ctx->symbols.order.v[i];
	ld_global_symbol *s = &ctx->symbols.globals[g];
	s->vis = ld_visibility_from_rank(ld_visibility_load(&s->visrank));
	s->thunk = LD_NONE;
	uint64_t d = ld_key_load(&s->def);
	if (s->state == LD_SYMBOL_STATE_SCRIPT)
		return;
	if (d == UINT64_MAX) {
		s->state = LD_SYMBOL_STATE_UNDEFINED;
		s->obj = LD_NONE;
		/* weak only if every reference is weak; type from the first one */
		s->bind = s->strongref ? STB_GLOBAL : STB_WEAK;
		uint64_t f = ld_key_load(&s->first);
		if ((f >> 32) < ctx->inputs.objects.n) {
			ld_object *o = ctx->inputs.objects.v[f >> 32];
			const elf_symbol *y = &o->sym[(uint32_t)f];
			s->type = y->st_info & 15;
		} else {
			s->bind = STB_GLOBAL;
		}
		return;
	}
	uint32_t oi = (d >> 32) & 0x3fffffff, si = (uint32_t)d;
	ld_object *o = ctx->inputs.objects.v[oi];
	const elf_symbol *y = &o->sym[si];
	s->state = LD_SYMBOL_STATE_OBJECT;
	s->obj = oi;
	s->symidx = si;
	s->bind = y->st_info >> 4;
	s->type = y->st_info & 15;
	s->size = y->st_size;
	s->val = y->st_value;
	s->sec = y->st_shndx == SHN_ABS || y->st_shndx == SHN_COMMON ? LD_NONE : o->isec0 + y->st_shndx;
	s->common = y->st_shndx == SHN_COMMON;
	if (s->common)
		s->type = STT_OBJECT;
	else if (y->st_shndx >= SHN_ABS && y->st_shndx != SHN_ABS)
		ld_error_report(ctx, "%s: unsupported section index for %s", o->path, o->str + y->st_name);
}
