/* --gc-sections as lld's MarkLive. */

/* --gc-sections as lld's MarkLive: roots are the entry symbol, KEEP, SHF_GNU_RETAIN,
 * non-allocated sections, init/fini/preinit arrays, notes outside groups,
 * .ctors/.dtors/.init/.fini/.jcr, and under -shared the exported definitions;
 * relocations, SHF_LINK_ORDER and section groups propagate liveness; a
 * __start_X/__stop_X reference keeps nothing (lld's default -z start-stop-gc) */
static void ld_sections_garbage_collect(struct ld_context *ctx, const char *entry)
{
	uint8_t *live = calloc(ctx->inputs.sections.n, 1);
	uint32_t *work = malloc(ctx->inputs.sections.n * sizeof(uint32_t)), nw = 0;
#define LD_SECTION_GARBAGE_MARK(x) do { uint32_t m_ = (x); if (m_ < ctx->inputs.sections.n && !live[m_] && ctx->inputs.sections.v[m_].kind != LD_SECTION_KIND_NONE) { live[m_] = 1; work[nw++] = m_; } } while (0)

	ld_name_cache_init(ctx);
	for (uint32_t i = 0; i < ctx->inputs.sections.n; i++) {
		ld_input_section *is = &ctx->inputs.sections.v[i];

		if (is->kind == LD_SECTION_KIND_NONE || is->object == LD_NONE)
			continue;
		if (ld_eh_frame_is_input(ctx, is)) {
			/* lld MarkLive::scanEhFrameSection: kept, but its relocations do not
			 * keep the functions; a CIE's (the personality) keep their targets, an
			 * FDE's only outside code and section groups (an LSDA) */
			uint32_t nrec;
			ld_eh_frame_record *rec = ld_eh_frame_records_split(ctx, i, &nrec);
			const ld_object *o = ctx->inputs.objects.v[is->object];

			live[i] = 1;
			for (uint32_t r = 0; rec && r < nrec; r++)
				for (uint32_t k = rec[r].first_relocation; k < rec[r].end_relocation; k++) {
					uint32_t t = ld_relocation_symbol_input_section(ctx, o, ctx->inputs.sections.v[i].rela[k].r_info >> 32);

					if (t == LD_NONE)
						continue;
					if (!rec[r].is_cie && ((ctx->inputs.sections.v[t].flags & SHF_EXECINSTR) || ctx->inputs.sections.v[t].next_group_member))
						continue;
					LD_SECTION_GARBAGE_MARK(t);
				}
			free(rec);
			continue;
		}
		const char *n = is->name;
		uint32_t d = ld_input_section_description_match(ctx, n);
		bool root = !(is->flags & SHF_ALLOC) || (is->flags & 0x200000) /* SHF_GNU_RETAIN */ ||
			    is->type == SHT_INIT_ARRAY || is->type == SHT_FINI_ARRAY || is->type == SHT_PREINIT_ARRAY ||
			    (is->type == SHT_NOTE && !is->next_group_member) ||
			    !strncmp(n, ".ctors", 6) || !strncmp(n, ".dtors", 6) || !strncmp(n, ".init", 5) ||
			    !strncmp(n, ".fini", 5) || !strncmp(n, ".jcr", 4) || (d != LD_NONE && ctx->script.input_descriptions.v[d].keep);

		if (root)
			LD_SECTION_GARBAGE_MARK(i);
	}
	uint32_t eg = ld_global_name_find(ctx, entry);

	if (eg != LD_NONE && ctx->symbols.globals[eg].state == LD_SYMBOL_STATE_OBJECT)
		LD_SECTION_GARBAGE_MARK(ctx->symbols.globals[eg].input_section);
	for (uint32_t g = 0; ctx->options.shared && g < atomic_load(&ctx->symbols.count); g++)
		if (ctx->symbols.globals[g].state == LD_SYMBOL_STATE_OBJECT && (ctx->symbols.globals[g].visibility == STV_DEFAULT || ctx->symbols.globals[g].visibility == STV_PROTECTED))
			LD_SECTION_GARBAGE_MARK(ctx->symbols.globals[g].input_section);
	for (bool again = true; again;) {
		while (nw) {
			ld_input_section *is = &ctx->inputs.sections.v[work[--nw]];
			ld_object *o = ctx->inputs.objects.v[is->object];

			for (uint32_t r = 0; r < is->rela_count; r++) {
				uint32_t y = is->rela[r].r_info >> 32;

				if (!y)
					continue;
				if (y >= o->first_global_index) {
					ld_global_symbol *gs = &ctx->symbols.globals[o->global_slots[y - o->first_global_index]];

					if (gs->state == LD_SYMBOL_STATE_OBJECT)
						LD_SECTION_GARBAGE_MARK(gs->input_section);
				} else if (o->symbols[y].st_shndx && o->symbols[y].st_shndx < 0xff00)
					LD_SECTION_GARBAGE_MARK(o->first_input_section + o->symbols[y].st_shndx);
			}
			if (is->next_group_member)
				LD_SECTION_GARBAGE_MARK(is->next_group_member - 1);
		}
		/* SHF_LINK_ORDER: live when the section it links to is */
		again = false;
		for (uint32_t i = 0; i < ctx->inputs.sections.n; i++) {
			ld_input_section *is = &ctx->inputs.sections.v[i];

			if (live[i] || is->kind == LD_SECTION_KIND_NONE || is->object == LD_NONE || !(is->flags & 0x80))
				continue;
			const ld_object *o = ctx->inputs.objects.v[is->object];
			uint32_t l = o->section_headers[is->section_header_index].sh_link;

			if (l && l < o->section_header_count && live[o->first_input_section + l]) {
				LD_SECTION_GARBAGE_MARK(i);
				again = true;
			}
		}
	}
	for (uint32_t i = 0; i < ctx->inputs.sections.n; i++) {
		ld_input_section *is = &ctx->inputs.sections.v[i];

		if (live[i] || is->kind == LD_SECTION_KIND_NONE || is->object == LD_NONE)
			continue;
		if (ctx->options.print_garbage_collected_sections)
			printf("removing unused section %s:(%s)\n", ctx->inputs.objects.v[is->object]->path, is->name);	/* lld: stdout */
		is->kind = LD_SECTION_KIND_NONE;
	}
#undef LD_SECTION_GARBAGE_MARK
	free(live);
	free(work);
}
