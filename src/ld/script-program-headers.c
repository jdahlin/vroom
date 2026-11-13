/* Segments declared by the script's PHDRS command. */

/* PHDRS: segments from the script, sections assigned by their :phdr lists */
static void create_script_phdrs(void)
{
	ctx->layout.program_headers.n = ctx->layout.program_header_sections.n = 0;
	for (uint32_t i = 0; i < sphdrs.n; i++) {
		SPhdr *h = &sphdrs.v[i];
		int k = phnew(h->type, h->flags ? (uint32_t)ev_value(eval(h->flags)) : 0);
		ctx->layout.program_header_sections.v[k].hdr = (h->filehdr || h->phdrs) && h->type == PT_LOAD;
	}
	const char **cur = NULL;
	uint32_t ncur = 0;
	uint64_t min = UINT64_MAX;
	for (uint32_t i = 0; i < ctx->layout.output_sections.n; i++) {
		uint32_t oi = ctx->layout.output_sections.v[i];
		OSec *os = &osecs.v[oi];
		os->ptload = -1;
		if (!(os->flags & SHF_ALLOC))
			continue;
		if (os->va < min)
			min = os->va;
		if (os->nphn) {
			cur = os->phn;
			ncur = os->nphn;
		}
		if (!ncur)
			error("section %s is not assigned to a PHDRS segment", os->name);
		for (uint32_t k = 0; k < ncur; k++) {
			uint32_t p = 0;
			while (p < sphdrs.n && strcmp(sphdrs.v[p].name, cur[k]))
				p++;
			if (p == sphdrs.n) {
				error("section header '%s' is not listed in PHDRS", cur[k]);
				continue;
			}
			phadd(p, oi);
			if (!sphdrs.v[p].flags)
				ctx->layout.program_headers.v[p].p_flags |= phflags(os);
		}
	}
	/* allocateHeaders() with explicit headers */
	uint64_t hs = header_size();
	if (min == UINT64_MAX || min < hs)
		error("could not allocate headers");
	else
		ctx->layout.file_header_address = (min - hs) & ~(opt.max_page - 1);
}
