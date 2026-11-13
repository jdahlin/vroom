/* Segments declared by the script's PHDRS command. */

/* PHDRS: segments from the script, sections assigned by their :phdr lists */
static void ld_script_program_headers_create(struct ld_context *ctx)
{
	ctx->layout.program_headers.n = ctx->layout.program_header_sections.n = 0;
	for (uint32_t i = 0; i < ctx->script.segments.n; i++) {
		ld_script_segment *h = &ctx->script.segments.v[i];
		int k = ld_segment_create(ctx, h->type, h->flags ? (uint32_t)ld_expression_value_resolve(ctx, ld_expression_eval(ctx, h->flags)) : 0);
		ctx->layout.program_header_sections.v[k].includes_headers = (h->file_header || h->program_headers) && h->type == PT_LOAD;
	}
	const char **cur = NULL;
	uint32_t ncur = 0;
	uint64_t min = UINT64_MAX;
	for (uint32_t i = 0; i < ctx->layout.output_sections.n; i++) {
		uint32_t oi = ctx->layout.output_sections.v[i];
		ld_output_section *os = &ctx->script.sections.v[oi];
		os->load_segment = -1;
		if (!(os->flags & SHF_ALLOC))
			continue;
		if (os->address < min)
			min = os->address;
		if (os->program_header_name_count) {
			cur = os->program_header_names;
			ncur = os->program_header_name_count;
		}
		if (!ncur)
			ld_error_report(ctx, "section %s is not assigned to a PHDRS segment", os->name);
		for (uint32_t k = 0; k < ncur; k++) {
			uint32_t p = 0;
			while (p < ctx->script.segments.n && strcmp(ctx->script.segments.v[p].name, cur[k]))
				p++;
			if (p == ctx->script.segments.n) {
				ld_error_report(ctx, "section header '%s' is not listed in PHDRS", cur[k]);
				continue;
			}
			ld_segment_section_add(ctx, p, oi);
			if (!ctx->script.segments.v[p].flags)
				ctx->layout.program_headers.v[p].p_flags |= ld_output_section_segment_flags(os);
		}
	}
	/* allocateHeaders() with explicit headers */
	uint64_t hs = ld_headers_size(ctx);
	if (min == UINT64_MAX || min < hs)
		ld_error_report(ctx, "could not allocate headers");
	else
		ctx->layout.file_header_address = (min - hs) & ~(ctx->options.max_page_size - 1);
}
