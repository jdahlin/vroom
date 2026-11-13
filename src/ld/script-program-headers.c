/* Segments declared by the script's PHDRS command. */

/* PHDRS: segments from the script, sections assigned by their :phdr lists */
static void ld_script_program_headers_create(void)
{
	phdrs.n = phsecs.n = 0;
	for (uint32_t i = 0; i < sphdrs.n; i++) {
		ld_script_segment *h = &sphdrs.v[i];
		int k = ld_segment_create(h->type, h->flags ? (uint32_t)ld_expression_value_resolve(ld_expression_eval(h->flags)) : 0);
		phsecs.v[k].hdr = (h->filehdr || h->phdrs) && h->type == PT_LOAD;
	}
	const char **cur = NULL;
	uint32_t ncur = 0;
	uint64_t min = UINT64_MAX;
	for (uint32_t i = 0; i < outsecs.n; i++) {
		uint32_t oi = outsecs.v[i];
		ld_output_section *os = &osecs.v[oi];
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
			ld_error_report("section %s is not assigned to a PHDRS segment", os->name);
		for (uint32_t k = 0; k < ncur; k++) {
			uint32_t p = 0;
			while (p < sphdrs.n && strcmp(sphdrs.v[p].name, cur[k]))
				p++;
			if (p == sphdrs.n) {
				ld_error_report("section header '%s' is not listed in PHDRS", cur[k]);
				continue;
			}
			ld_segment_section_add(p, oi);
			if (!sphdrs.v[p].flags)
				phdrs.v[p].p_flags |= ld_output_section_segment_flags(os);
		}
	}
	/* allocateHeaders() with explicit headers */
	uint64_t hs = ld_headers_size();
	if (min == UINT64_MAX || min < hs)
		ld_error_report("could not allocate headers");
	else
		hdr_va = (min - hs) & ~(opt.max_page - 1);
}
