/* Program header creation (lld's createPhdrs) and finishing. */

/* ---------------- phdrs (lld createPhdrs) ---------------- */

/* lld isRelroSection */
static bool ld_output_section_is_relro(struct ld_context *ctx, uint32_t oi)
{
	const ld_output_section *os = &ctx->script.sections.v[oi];
	const char *n = os->name;

	if (!(os->flags & SHF_ALLOC) || !(os->flags & SHF_WRITE))
		return false;
	if ((os->flags & SHF_TLS) || os->type == SHT_INIT_ARRAY || os->type == SHT_FINI_ARRAY ||
	    os->type == SHT_PREINIT_ARRAY)
		return true;
	if (ctx->output.got_section != LD_NONE && ld_input_section_output_section(ctx, ctx->output.got_section) == oi)
		return true;
	if (ctx->output.dynamic_section != LD_NONE && ld_input_section_output_section(ctx, ctx->output.dynamic_section) == oi)
		return true;
	return !strcmp(n, ".data.rel.ro") || !strcmp(n, ".bss.rel.ro") || !strcmp(n, ".ctors") ||
	       !strcmp(n, ".dtors") || !strcmp(n, ".jcr") || !strcmp(n, ".eh_frame") ||
	       !strcmp(n, ".fini_array") || !strcmp(n, ".init_array") || !strcmp(n, ".preinit_array");
}

static void ld_program_headers_create(struct ld_context *ctx)
{
	ctx->layout.program_headers.n = ctx->layout.program_header_sections.n = 0;
	ld_segment_create(ctx, PT_PHDR, PF_R);
	int hdrload = ld_segment_create(ctx, PT_LOAD, PF_R);
	int load = hdrload;
	uint32_t flags = PF_R;
	/* lld relroEnd: the first section after the relro run starts a PT_LOAD */
	uint32_t relro_end = LD_NONE;

	for (uint32_t i = 0, in = 0; i < ctx->layout.output_sections.n && !ctx->options.no_relro && !ctx->options.relocatable && relro_end == LD_NONE; i++) {
		if (!ld_output_section_is_in_load_segment(&ctx->script.sections.v[ctx->layout.output_sections.v[i]]))
			continue;
		if (ld_output_section_is_relro(ctx, ctx->layout.output_sections.v[i]))
			in = 1;
		else if (in)
			relro_end = ctx->layout.output_sections.v[i];
	}
	for (uint32_t i = 0; i < ctx->layout.output_sections.n; i++) {
		uint32_t oi = ctx->layout.output_sections.v[i];
		ld_output_section *os = &ctx->script.sections.v[oi];
		os->load_segment = -1;
		if (!ld_output_section_is_in_load_segment(os))
			continue;
		uint32_t nf = ld_output_section_segment_flags(os);
		if ((flags ^ nf) || oi == relro_end)
			load = -1;
		bool last_is_hdr = load == hdrload && ctx->layout.program_header_sections.v[load].last_output_section < 0;
		if (load >= 0 && (!os->load_address_expression || last_is_hdr)) {
			ctx->layout.program_headers.v[load].p_flags |= nf;
		} else {
			load = ld_segment_create(ctx, PT_LOAD, nf);
			flags = nf;
		}
		ld_segment_section_add(ctx, load, oi);
	}
	/* lld: one PT_TLS over the SHF_TLS sections, after the PT_LOADs */
	{
		int tls = -1;
		for (uint32_t i = 0; i < ctx->layout.output_sections.n; i++) {
			uint32_t oi = ctx->layout.output_sections.v[i];
			if ((ctx->script.sections.v[oi].flags & (SHF_ALLOC | SHF_TLS)) != (SHF_ALLOC | SHF_TLS))
				continue;
			if (tls < 0)
				tls = ld_segment_create(ctx, PT_TLS, PF_R);
			ld_segment_section_add(ctx, tls, oi);
		}
	}
	/* lld: PT_DYNAMIC over .dynamic */
	if (ctx->output.dynamic_live)
		ld_segment_section_add(ctx, ld_segment_create(ctx, PT_DYNAMIC, ld_output_section_segment_flags(&ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.dynamic_section)])), ld_input_section_output_section(ctx, ctx->output.dynamic_section));
	/* lld: one PT_GNU_RELRO over the contiguous relro sections */
	if (!ctx->options.no_relro && !ctx->options.relocatable) {
		int rr = -1;
		bool in = false, ended = false;

		for (uint32_t i = 0; i < ctx->layout.output_sections.n; i++) {
			uint32_t oi = ctx->layout.output_sections.v[i];

			if (!ld_output_section_is_in_load_segment(&ctx->script.sections.v[oi]))
				continue;
			if (ld_output_section_is_relro(ctx, oi)) {
				in = true;
				if (ended)
					ld_error_report(ctx, "section: %s is not contiguous with other relro sections", ctx->script.sections.v[oi].name);
				else {
					if (rr < 0)
						rr = ld_segment_create(ctx, PT_GNU_RELRO, PF_R);
					ld_segment_section_add(ctx, rr, oi);
				}
			} else if (in) {
				in = false;
				ended = true;
			}
		}
		if (rr >= 0)
			ctx->layout.program_headers.v[rr].p_align = 1;
	}
	if (ctx->output.eh_frame_header_section != LD_NONE && ld_input_section_output_section(ctx, ctx->output.eh_frame_header_section) != LD_NONE && ld_input_section_output_section(ctx, ctx->output.eh_frame_header_section) != LD_OUTPUT_SECTION_DISCARD)
		ld_segment_section_add(ctx, ld_segment_create(ctx, PT_GNU_EH_FRAME, PF_R), ld_input_section_output_section(ctx, ctx->output.eh_frame_header_section));
	ld_segment_create(ctx, PT_GNU_STACK, PF_R | PF_W);
	int note = -1;
	for (uint32_t i = 0; i < ctx->layout.output_sections.n; i++) {
		uint32_t oi = ctx->layout.output_sections.v[i];
		ld_output_section *os = &ctx->script.sections.v[oi];
		if (os->type == SHT_NOTE && (os->flags & SHF_ALLOC)) {
			if (note < 0 || os->load_address_expression || ctx->script.sections.v[ctx->layout.program_header_sections.v[note].last_output_section].address_alignment != os->address_alignment)
				note = ld_segment_create(ctx, PT_NOTE, PF_R);
			ld_segment_section_add(ctx, note, oi);
		} else {
			note = -1;
		}
	}
}

/* allocateHeaders() + removeEmptyPTLoad() */
static void ld_program_headers_finish(struct ld_context *ctx)
{
	uint64_t min = UINT64_MAX;
	for (uint32_t i = 0; i < ctx->layout.output_sections.n; i++) {
		ld_output_section *os = &ctx->script.sections.v[ctx->layout.output_sections.v[i]];
		if ((os->flags & SHF_ALLOC) && os->address < min)
			min = os->address;
	}
	uint64_t hsize = sizeof(elf_file_header) + (uint64_t)ctx->layout.program_headers.n * sizeof(elf_program_header);
	uint64_t base = min & ~(ctx->options.max_page_size - 1);
	ctx->layout.headers_loaded = !ctx->options.nmagic && hsize <= min - base;
	if (ctx->layout.headers_loaded)
		ld_error_report(ctx, "ELF headers in the first PT_LOAD are not supported (no SECTIONS gap below the first section)");
	int *map = ld_region_alloc(ctx->layout.program_headers.n * sizeof(int));
	uint32_t w = 0;
	for (uint32_t i = 0; i < ctx->layout.program_headers.n; i++) {
		elf_program_header *p = &ctx->layout.program_headers.v[i];
		ld_segment_sections *s = &ctx->layout.program_header_sections.v[i];
		bool keep = true;
		if (p->p_type == PT_PHDR)
			keep = ctx->layout.headers_loaded;
		else if (p->p_type == PT_LOAD) {
			if (s->first_output_section < 0)
				keep = false;
			else {
				ld_output_section *f = &ctx->script.sections.v[s->first_output_section], *l = &ctx->script.sections.v[s->last_output_section];
				keep = l->address + l->size - f->address != 0;
			}
		}
		map[i] = keep ? (int)w : -1;
		if (keep) {
			ctx->layout.program_headers.v[w] = *p;
			ctx->layout.program_header_sections.v[w] = *s;
			w++;
		}
	}
	ctx->layout.program_headers.n = ctx->layout.program_header_sections.n = w;
	for (uint32_t k = 0; k < ctx->layout.output_sections.n; k++) {
		ld_output_section *os = &ctx->script.sections.v[ctx->layout.output_sections.v[k]];
		if (os->load_segment >= 0)
			os->load_segment = map[os->load_segment];
	}
}
