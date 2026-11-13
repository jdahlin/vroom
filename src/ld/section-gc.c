/* --gc-sections as lld's MarkLive. */

/* --gc-sections as lld's MarkLive: roots are the entry symbol, KEEP, SHF_GNU_RETAIN,
 * non-allocated sections, init/fini/preinit arrays, notes outside groups,
 * .ctors/.dtors/.init/.fini/.jcr, and under -shared the exported definitions;
 * relocations, SHF_LINK_ORDER and section groups propagate liveness; a
 * __start_X/__stop_X reference keeps nothing (lld's default -z start-stop-gc) */
static void ld_sections_garbage_collect(const char *entry)
{
	uint8_t *live = calloc(isecs.n, 1);
	uint32_t *work = malloc(isecs.n * sizeof(uint32_t)), nw = 0;
#define LD_SECTION_GARBAGE_MARK(x) do { uint32_t m_ = (x); if (m_ < isecs.n && !live[m_] && isecs.v[m_].kind != LD_SECTION_KIND_NONE) { live[m_] = 1; work[nw++] = m_; } } while (0)

	ld_name_cache_init();
	for (uint32_t i = 0; i < isecs.n; i++) {
		ld_input_section *is = &isecs.v[i];

		if (is->kind == LD_SECTION_KIND_NONE || is->obj == LD_NONE)
			continue;
		if (ld_eh_frame_is_input(is)) {
			/* lld MarkLive::scanEhFrameSection: kept, but its relocations do not
			 * keep the functions; a CIE's (the personality) keep their targets, an
			 * FDE's only outside code and section groups (an LSDA) */
			uint32_t nrec;
			ld_eh_frame_record *rec = ld_eh_frame_records_split(i, &nrec);
			const ld_object *o = objs.v[is->obj];

			live[i] = 1;
			for (uint32_t r = 0; rec && r < nrec; r++)
				for (uint32_t k = rec[r].r0; k < rec[r].r1; k++) {
					uint32_t t = ld_relocation_symbol_input_section(o, isecs.v[i].rela[k].r_info >> 32);

					if (t == LD_NONE)
						continue;
					if (!rec[r].cie && ((isecs.v[t].flags & SHF_EXECINSTR) || isecs.v[t].gnext))
						continue;
					LD_SECTION_GARBAGE_MARK(t);
				}
			free(rec);
			continue;
		}
		const char *n = is->name;
		uint32_t d = ld_input_section_description_match(n);
		bool root = !(is->flags & SHF_ALLOC) || (is->flags & 0x200000) /* SHF_GNU_RETAIN */ ||
			    is->type == SHT_INIT_ARRAY || is->type == SHT_FINI_ARRAY || is->type == SHT_PREINIT_ARRAY ||
			    (is->type == SHT_NOTE && !is->gnext) ||
			    !strncmp(n, ".ctors", 6) || !strncmp(n, ".dtors", 6) || !strncmp(n, ".init", 5) ||
			    !strncmp(n, ".fini", 5) || !strncmp(n, ".jcr", 4) || (d != LD_NONE && isds.v[d].keep);

		if (root)
			LD_SECTION_GARBAGE_MARK(i);
	}
	uint32_t eg = ld_global_name_find(entry);

	if (eg != LD_NONE && gsyms[eg].state == LD_SYMBOL_STATE_OBJECT)
		LD_SECTION_GARBAGE_MARK(gsyms[eg].sec);
	for (uint32_t g = 0; opt.shared && g < atomic_load(&ngsyms); g++)
		if (gsyms[g].state == LD_SYMBOL_STATE_OBJECT && (gsyms[g].vis == STV_DEFAULT || gsyms[g].vis == STV_PROTECTED))
			LD_SECTION_GARBAGE_MARK(gsyms[g].sec);
	for (bool again = true; again;) {
		while (nw) {
			ld_input_section *is = &isecs.v[work[--nw]];
			ld_object *o = objs.v[is->obj];

			for (uint32_t r = 0; r < is->nrela; r++) {
				uint32_t y = is->rela[r].r_info >> 32;

				if (!y)
					continue;
				if (y >= o->firstg) {
					ld_global_symbol *gs = &gsyms[o->gs[y - o->firstg]];

					if (gs->state == LD_SYMBOL_STATE_OBJECT)
						LD_SECTION_GARBAGE_MARK(gs->sec);
				} else if (o->sym[y].st_shndx && o->sym[y].st_shndx < 0xff00)
					LD_SECTION_GARBAGE_MARK(o->isec0 + o->sym[y].st_shndx);
			}
			if (is->gnext)
				LD_SECTION_GARBAGE_MARK(is->gnext - 1);
		}
		/* SHF_LINK_ORDER: live when the section it links to is */
		again = false;
		for (uint32_t i = 0; i < isecs.n; i++) {
			ld_input_section *is = &isecs.v[i];

			if (live[i] || is->kind == LD_SECTION_KIND_NONE || is->obj == LD_NONE || !(is->flags & 0x80))
				continue;
			const ld_object *o = objs.v[is->obj];
			uint32_t l = o->sh[is->shndx].sh_link;

			if (l && l < o->nsh && live[o->isec0 + l]) {
				LD_SECTION_GARBAGE_MARK(i);
				again = true;
			}
		}
	}
	for (uint32_t i = 0; i < isecs.n; i++) {
		ld_input_section *is = &isecs.v[i];

		if (live[i] || is->kind == LD_SECTION_KIND_NONE || is->obj == LD_NONE)
			continue;
		if (opt.print_gc)
			printf("removing unused section %s:(%s)\n", objs.v[is->obj]->path, is->name);	/* lld: stdout */
		is->kind = LD_SECTION_KIND_NONE;
	}
#undef LD_SECTION_GARBAGE_MARK
	free(live);
	free(work);
}
