/* Segment membership helpers and the assignment of file offsets to output sections. */

/* ---------------- output sections: liveness, flags ---------------- */

/* ---------------- phdrs / file layout ---------------- */

static bool ld_output_section_is_in_load_segment(ld_output_section *os) { return (os->flags & SHF_ALLOC) && !((os->flags & SHF_TLS) && os->type == SHT_NOBITS); }
static uint32_t ld_output_section_segment_flags(ld_output_section *os)
{
	uint32_t f = PF_R;
	if (os->flags & SHF_WRITE) f |= PF_W;
	if (os->flags & SHF_EXECINSTR) f |= PF_X;
	return f;
}


static void ld_segment_section_add(struct ld_context *ctx, int ph, uint32_t oi)
{
	ld_segment_sections *p = &ctx->layout.program_header_sections.v[ph];
	if (p->first_output_section < 0)
		p->first_output_section = oi;
	p->last_output_section = oi;
	ld_output_section *os = &ctx->script.sections.v[oi];
	if (os->address_alignment > ctx->layout.program_headers.v[ph].p_align)
		ctx->layout.program_headers.v[ph].p_align = os->address_alignment;
	if (ctx->layout.program_headers.v[ph].p_type == PT_LOAD)
		os->load_segment = ph;
}
static int ld_segment_create(struct ld_context *ctx, uint32_t type, uint32_t flags)
{
	elf_program_header p = {.p_type = type, .p_flags = flags, .p_align = type == PT_LOAD ? ctx->options.max_page_size : 0};
	VVEC_PUSH(ctx->layout.program_headers, p, ld_region_alloc);
	VVEC_PUSH(ctx->layout.program_header_sections, ((ld_segment_sections){.first_output_section = -1, .last_output_section = -1}), ld_region_alloc);
	return ctx->layout.program_headers.n - 1;
}

static void ld_file_offsets_assign(struct ld_context *ctx)
{
	uint64_t off = sizeof(elf_file_header) + (uint64_t)ctx->layout.program_headers.n * sizeof(elf_program_header);
	for (uint32_t i = 0; i < ctx->layout.output_sections.n; i++) {
		ld_output_section *os = &ctx->script.sections.v[ctx->layout.output_sections.v[i]];
		if (!(os->flags & SHF_ALLOC))
			continue;
		if (os->load_segment >= 0 && ctx->layout.program_header_sections.v[os->load_segment].includes_headers && os->type != SHT_NOBITS)
			off = os->address - ctx->layout.file_header_address;	/* the ELF header is the segment's first "section" */
		else if (os->load_segment >= 0 && ctx->layout.program_header_sections.v[os->load_segment].first_output_section == (int)ctx->layout.output_sections.v[i])
			off = ld_value_align_skew(off, ctx->layout.program_headers.v[os->load_segment].p_align, os->address);
		else if (os->type == SHT_NOBITS)
			;
		else if (os->load_segment < 0)
			off = ld_value_align_up(off, os->address_alignment);
		else {
			ld_output_section *f = &ctx->script.sections.v[ctx->layout.program_header_sections.v[os->load_segment].first_output_section];
			off = f->offset + os->address - f->address;
		}
		os->offset = off;
		if (os->type != SHT_NOBITS)
			off += os->size;
	}
	for (uint32_t i = 0; i < ctx->layout.output_sections.n; i++) {
		ld_output_section *os = &ctx->script.sections.v[ctx->layout.output_sections.v[i]];
		if (os->flags & SHF_ALLOC)
			continue;
		os->offset = ld_value_align_up(off, os->address_alignment);
		off = os->offset + os->size;
	}
	ctx->layout.section_header_offset = ld_value_align_up(off, 8);
	ctx->layout.file_size = ctx->layout.section_header_offset + (uint64_t)(ctx->layout.output_sections.n + 1) * sizeof(elf_section_header);
	for (uint32_t i = 0; i < ctx->layout.program_headers.n; i++) {
		elf_program_header *p = &ctx->layout.program_headers.v[i];
		ld_segment_sections *s = &ctx->layout.program_header_sections.v[i];
		if (s->first_output_section < 0)
			continue;
		ld_output_section *f = &ctx->script.sections.v[s->first_output_section], *l = &ctx->script.sections.v[s->last_output_section];
		uint64_t foff = s->includes_headers ? 0 : f->offset, fva = s->includes_headers ? ctx->layout.file_header_address : f->address;
		p->p_filesz = l->offset - foff + (l->type != SHT_NOBITS ? l->size : 0);
		p->p_memsz = l->address + l->size - fva;
		p->p_offset = foff;
		p->p_vaddr = p->p_paddr = fva;
		if (p->p_type == PT_TLS) {
			uint64_t a = p->p_align ? p->p_align : 1;
			ctx->layout.tls_thread_pointer = p->p_vaddr - ld_value_align_up(16, a);
			if (ctx->inputs.x86)	/* variant 2: the thread pointer at the block's end */
				ctx->layout.tls_thread_pointer = p->p_vaddr + ld_value_align_up(p->p_memsz, a);
			ctx->layout.tls_start = p->p_vaddr;
			ctx->layout.has_tls = true;
		}
	}
}
