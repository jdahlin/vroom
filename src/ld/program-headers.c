/* Program header creation (lld's createPhdrs) and finishing. */

/* ---------------- phdrs (lld createPhdrs) ---------------- */


/* lld isRelroSection */
static bool is_relro(uint32_t oi)
{
	const OSec *os = &osecs.v[oi];
	const char *n = os->name;

	if (!(os->flags & SHF_ALLOC) || !(os->flags & SHF_WRITE))
		return false;
	if ((os->flags & SHF_TLS) || os->type == SHT_INIT_ARRAY || os->type == SHT_FINI_ARRAY ||
	    os->type == SHT_PREINIT_ARRAY)
		return true;
	if (got_isec != NONE && isec_osec(got_isec) == oi)
		return true;
	if (dynamic_isec != NONE && isec_osec(dynamic_isec) == oi)
		return true;
	return !strcmp(n, ".data.rel.ro") || !strcmp(n, ".bss.rel.ro") || !strcmp(n, ".ctors") ||
	       !strcmp(n, ".dtors") || !strcmp(n, ".jcr") || !strcmp(n, ".eh_frame") ||
	       !strcmp(n, ".fini_array") || !strcmp(n, ".init_array") || !strcmp(n, ".preinit_array");
}

static void create_phdrs(void)
{
	ctx->layout.program_headers.n = ctx->layout.program_header_sections.n = 0;
	phnew(PT_PHDR, PF_R);
	int hdrload = phnew(PT_LOAD, PF_R);
	int load = hdrload;
	uint32_t flags = PF_R;
	/* lld relroEnd: the first section after the relro run starts a PT_LOAD */
	uint32_t relro_end = NONE;

	for (uint32_t i = 0, in = 0; i < ctx->layout.output_sections.n && !opt.norelro && !opt.relocatable && relro_end == NONE; i++) {
		if (!sec_in_load(&osecs.v[ctx->layout.output_sections.v[i]]))
			continue;
		if (is_relro(ctx->layout.output_sections.v[i]))
			in = 1;
		else if (in)
			relro_end = ctx->layout.output_sections.v[i];
	}
	for (uint32_t i = 0; i < ctx->layout.output_sections.n; i++) {
		uint32_t oi = ctx->layout.output_sections.v[i];
		OSec *os = &osecs.v[oi];
		os->ptload = -1;
		if (!sec_in_load(os))
			continue;
		uint32_t nf = phflags(os);
		if ((flags ^ nf) || oi == relro_end)
			load = -1;
		bool last_is_hdr = load == hdrload && ctx->layout.program_header_sections.v[load].last < 0;
		if (load >= 0 && (!os->lma || last_is_hdr)) {
			ctx->layout.program_headers.v[load].p_flags |= nf;
		} else {
			load = phnew(PT_LOAD, nf);
			flags = nf;
		}
		phadd(load, oi);
	}
	/* lld: one PT_TLS over the SHF_TLS sections, after the PT_LOADs */
	{
		int tls = -1;
		for (uint32_t i = 0; i < ctx->layout.output_sections.n; i++) {
			uint32_t oi = ctx->layout.output_sections.v[i];
			if ((osecs.v[oi].flags & (SHF_ALLOC | SHF_TLS)) != (SHF_ALLOC | SHF_TLS))
				continue;
			if (tls < 0)
				tls = phnew(PT_TLS, PF_R);
			phadd(tls, oi);
		}
	}
	/* lld: PT_DYNAMIC over .dynamic */
	if (dyn_live)
		phadd(phnew(PT_DYNAMIC, phflags(&osecs.v[isec_osec(dynamic_isec)])), isec_osec(dynamic_isec));
	/* lld: one PT_GNU_RELRO over the contiguous relro sections */
	if (!opt.norelro && !opt.relocatable) {
		int rr = -1;
		bool in = false, ended = false;

		for (uint32_t i = 0; i < ctx->layout.output_sections.n; i++) {
			uint32_t oi = ctx->layout.output_sections.v[i];

			if (!sec_in_load(&osecs.v[oi]))
				continue;
			if (is_relro(oi)) {
				in = true;
				if (ended)
					error("section: %s is not contiguous with other relro sections", osecs.v[oi].name);
				else {
					if (rr < 0)
						rr = phnew(PT_GNU_RELRO, PF_R);
					phadd(rr, oi);
				}
			} else if (in) {
				in = false;
				ended = true;
			}
		}
		if (rr >= 0)
			ctx->layout.program_headers.v[rr].p_align = 1;
	}
	if (ehhdr_isec != NONE && isec_osec(ehhdr_isec) != NONE && isec_osec(ehhdr_isec) != OS_DISCARD)
		phadd(phnew(PT_GNU_EH_FRAME, PF_R), isec_osec(ehhdr_isec));
	phnew(PT_GNU_STACK, PF_R | PF_W);
	int note = -1;
	for (uint32_t i = 0; i < ctx->layout.output_sections.n; i++) {
		uint32_t oi = ctx->layout.output_sections.v[i];
		OSec *os = &osecs.v[oi];
		if (os->type == SHT_NOTE && (os->flags & SHF_ALLOC)) {
			if (note < 0 || os->lma || osecs.v[ctx->layout.program_header_sections.v[note].last].addralign != os->addralign)
				note = phnew(PT_NOTE, PF_R);
			phadd(note, oi);
		} else {
			note = -1;
		}
	}
}

/* allocateHeaders() + removeEmptyPTLoad() */
static void finish_phdrs(void)
{
	uint64_t min = UINT64_MAX;
	for (uint32_t i = 0; i < ctx->layout.output_sections.n; i++) {
		OSec *os = &osecs.v[ctx->layout.output_sections.v[i]];
		if ((os->flags & SHF_ALLOC) && os->va < min)
			min = os->va;
	}
	uint64_t hsize = sizeof(Ehdr) + (uint64_t)ctx->layout.program_headers.n * sizeof(Phdr);
	uint64_t base = min & ~(opt.max_page - 1);
	ctx->layout.headers_loaded = !opt.nmagic && hsize <= min - base;
	if (ctx->layout.headers_loaded)
		error("ELF headers in the first PT_LOAD are not supported (no SECTIONS gap below the first section)");
	int *map = alloc(ctx->layout.program_headers.n * sizeof(int));
	uint32_t w = 0;
	for (uint32_t i = 0; i < ctx->layout.program_headers.n; i++) {
		Phdr *p = &ctx->layout.program_headers.v[i];
		PhdrSecs *s = &ctx->layout.program_header_sections.v[i];
		bool keep = true;
		if (p->p_type == PT_PHDR)
			keep = ctx->layout.headers_loaded;
		else if (p->p_type == PT_LOAD) {
			if (s->first < 0)
				keep = false;
			else {
				OSec *f = &osecs.v[s->first], *l = &osecs.v[s->last];
				keep = l->va + l->size - f->va != 0;
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
		OSec *os = &osecs.v[ctx->layout.output_sections.v[k]];
		if (os->ptload >= 0)
			os->ptload = map[os->ptload];
	}
}
