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


static void ld_segment_section_add(int ph, uint32_t oi)
{
	ld_segment_sections *p = &phsecs.v[ph];
	if (p->first < 0)
		p->first = oi;
	p->last = oi;
	ld_output_section *os = &osecs.v[oi];
	if (os->addralign > phdrs.v[ph].p_align)
		phdrs.v[ph].p_align = os->addralign;
	if (phdrs.v[ph].p_type == PT_LOAD)
		os->ptload = ph;
}
static int ld_segment_create(uint32_t type, uint32_t flags)
{
	elf_program_header p = {.p_type = type, .p_flags = flags, .p_align = type == PT_LOAD ? opt.max_page : 0};
	VVEC_PUSH(phdrs, p, ld_region_alloc);
	VVEC_PUSH(phsecs, ((ld_segment_sections){.first = -1, .last = -1}), ld_region_alloc);
	return phdrs.n - 1;
}

/* arm64 TLS variant 1: tp = the PT_TLS start - ld_value_align_up(16, p_align); tprel = va - tls_tp */
static uint64_t tls_tp;
static bool has_tls;
static uint64_t hdr_va;		/* PHDRS FILEHDR: address of the ELF header */

static void ld_file_offsets_assign(void)
{
	uint64_t off = sizeof(elf_file_header) + (uint64_t)phdrs.n * sizeof(elf_program_header);
	for (uint32_t i = 0; i < outsecs.n; i++) {
		ld_output_section *os = &osecs.v[outsecs.v[i]];
		if (!(os->flags & SHF_ALLOC))
			continue;
		if (os->ptload >= 0 && phsecs.v[os->ptload].hdr && os->type != SHT_NOBITS)
			off = os->va - hdr_va;	/* the ELF header is the segment's first "section" */
		else if (os->ptload >= 0 && phsecs.v[os->ptload].first == (int)outsecs.v[i])
			off = ld_value_align_skew(off, phdrs.v[os->ptload].p_align, os->va);
		else if (os->type == SHT_NOBITS)
			;
		else if (os->ptload < 0)
			off = ld_value_align_up(off, os->addralign);
		else {
			ld_output_section *f = &osecs.v[phsecs.v[os->ptload].first];
			off = f->offset + os->va - f->va;
		}
		os->offset = off;
		if (os->type != SHT_NOBITS)
			off += os->size;
	}
	for (uint32_t i = 0; i < outsecs.n; i++) {
		ld_output_section *os = &osecs.v[outsecs.v[i]];
		if (os->flags & SHF_ALLOC)
			continue;
		os->offset = ld_value_align_up(off, os->addralign);
		off = os->offset + os->size;
	}
	shoff = ld_value_align_up(off, 8);
	filesize = shoff + (uint64_t)(outsecs.n + 1) * sizeof(elf_section_header);
	for (uint32_t i = 0; i < phdrs.n; i++) {
		elf_program_header *p = &phdrs.v[i];
		ld_segment_sections *s = &phsecs.v[i];
		if (s->first < 0)
			continue;
		ld_output_section *f = &osecs.v[s->first], *l = &osecs.v[s->last];
		uint64_t foff = s->hdr ? 0 : f->offset, fva = s->hdr ? hdr_va : f->va;
		p->p_filesz = l->offset - foff + (l->type != SHT_NOBITS ? l->size : 0);
		p->p_memsz = l->va + l->size - fva;
		p->p_offset = foff;
		p->p_vaddr = p->p_paddr = fva;
		if (p->p_type == PT_TLS) {
			uint64_t a = p->p_align ? p->p_align : 1;
			tls_tp = p->p_vaddr - ld_value_align_up(16, a);
			if (x86_ld)	/* variant 2: the thread pointer at the block's end */
				tls_tp = p->p_vaddr + ld_value_align_up(p->p_memsz, a);
			tls_start = p->p_vaddr;
			has_tls = true;
		}
	}
}
