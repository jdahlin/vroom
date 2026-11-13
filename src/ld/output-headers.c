/* The section name table, the ELF and section headers, synthetic contents and the build id. */

/* ---------------- output ---------------- */


static void ld_section_name_table_build(struct ld_context *ctx)
{
	/* deduplicated, in section header order (StringTableBuilder in insertion order) */
	size_t cap = 1;
	for (uint32_t i = 0; i < ctx->layout.output_sections.n; i++)
		cap += strlen(ctx->script.sections.v[ctx->layout.output_sections.v[i]].name) + 1;
	ctx->output.shstrtab = ld_region_alloc(cap);
	size_t n = 1;
	for (uint32_t i = 0; i < ctx->layout.output_sections.n; i++) {
		ld_output_section *os = &ctx->script.sections.v[ctx->layout.output_sections.v[i]];
		size_t l = strlen(os->name);
		uint32_t found = LD_NONE;
		for (uint32_t k = 0; k < i; k++)
			if (!strcmp(ctx->script.sections.v[ctx->layout.output_sections.v[k]].name, os->name)) {
				found = ctx->script.sections.v[ctx->layout.output_sections.v[k]].nameoff;
				break;
			}
		if (found != LD_NONE) {
			os->nameoff = found;
			continue;
		}
		memcpy(ctx->output.shstrtab + n, os->name, l + 1);
		os->nameoff = n;
		n += l + 1;
	}
	ctx->inputs.sections.v[ctx->output.shstrtab_section].size = n;
}

static void ld_headers_write(struct ld_context *ctx)
{
	elf_file_header *eh = (elf_file_header *)ctx->output.image;
	memcpy(eh->e_ident, "\177ELF\2\1\1", 7);
	eh->e_type = ctx->options.relocatable ? 1 : ctx->options.shared ? 3 : 2;
	eh->e_machine = ctx->inputs.x86 ? 62 : 183;
	eh->e_version = 1;
	/* lld: -e, else ENTRY(), else _start (silently absent) */
	const char *en = ctx->options.entry ? ctx->options.entry : ctx->script.entry ? ctx->script.entry : "_start";
	uint32_t eg = ld_global_name_find(ctx, en);
	if (!ctx->options.relocatable && (ctx->options.entry || ctx->script.entry) && (eg == LD_NONE || ctx->symbols.globals[eg].state == LD_SYMBOL_STATE_UNDEFINED))
		ld_warning_report(ctx, "cannot find entry symbol %s; not setting start address", en);
	if (!ctx->options.relocatable)
		eh->e_entry = eg != LD_NONE && ctx->symbols.globals[eg].state != LD_SYMBOL_STATE_UNDEFINED ? ld_global_symbol_address(ctx, eg) : 0;
	eh->e_phoff = ctx->layout.program_headers.n ? sizeof(elf_file_header) : 0;
	eh->e_shoff = ctx->layout.section_header_offset;
	eh->e_ehsize = sizeof(elf_file_header);
	eh->e_phentsize = ctx->layout.program_headers.n ? sizeof(elf_program_header) : 0;
	eh->e_phnum = ctx->layout.program_headers.n;
	eh->e_shentsize = sizeof(elf_section_header);
	eh->e_shnum = ctx->layout.output_sections.n + 1;
	eh->e_shstrndx = ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.shstrtab_section)].shidx;
	if (ctx->layout.program_headers.n)
		memcpy(ctx->output.image + sizeof(elf_file_header), ctx->layout.program_headers.v, ctx->layout.program_headers.n * sizeof(elf_program_header));
	elf_section_header *sh = (elf_section_header *)(ctx->output.image + ctx->layout.section_header_offset);
	for (uint32_t i = 0; i < ctx->layout.output_sections.n; i++) {
		ld_output_section *os = &ctx->script.sections.v[ctx->layout.output_sections.v[i]];
		elf_section_header *d = &sh[i + 1];
		d->sh_name = os->nameoff;
		d->sh_type = os->type;
		d->sh_flags = os->flags;
		d->sh_addr = os->va;
		d->sh_offset = os->offset;
		d->sh_size = os->size;
		d->sh_link = os->link;
		d->sh_info = os->info;
		d->sh_addralign = os->addralign;
		d->sh_entsize = os->entsize;
	}
}

static void ld_synthetic_sections_write(struct ld_context *ctx)
{
	/* .rela.dyn, .symtab and .strtab were written in place */
	ld_input_section *is = &ctx->inputs.sections.v[ctx->output.shstrtab_section];
	memcpy(ctx->output.image + ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.shstrtab_section)].offset + is->off, ctx->output.shstrtab, is->size);
}

/* --build-id: lld's "sha1"/"tree" ids are a BLAKE3 tree over 1 MiB chunks */
static void ld_build_id_write(struct ld_context *ctx)
{
	if (ctx->output.build_id_section == LD_NONE)
		return;
	ld_input_section *is = &ctx->inputs.sections.v[ctx->output.build_id_section];
	uint8_t *dst = ctx->output.image + ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.build_id_section)].offset + is->off + 16;
	size_t hl = is->size - 16;
	size_t nchunks = (ctx->layout.file_size + (1u << 20) - 1) >> 20;
	uint8_t *hashes = ld_region_alloc(nchunks * 20 + 32);
	if (!strcmp(ctx->options.build_id, "sha1") || !strcmp(ctx->options.build_id, "tree")) {
		ld_build_id_job job = {ctx->output.image, ctx->layout.file_size, hashes};
		ld_parallel_for(nchunks, ld_build_id_chunk_hash, &job);
		vdigest_blake3(hashes, nchunks * 20, dst, hl);
	} else if (!strcmp(ctx->options.build_id, "fast")) {
		/* xxh3 of the chunk hashes (lld: --build-id=fast is xxh3_64bits per chunk) */
		uint8_t *h8 = ld_region_alloc(nchunks * 8);
		for (size_t i = 0; i < nchunks; i++) {
			uint64_t b = (uint64_t)i << 20, n = ctx->layout.file_size - b < (1u << 20) ? ctx->layout.file_size - b : (1u << 20);
			uint64_t x = vdigest_xxh3(ctx->output.image + b, n);
			memcpy(h8 + i * 8, &x, 8);
		}
		uint64_t x = vdigest_xxh3(h8, nchunks * 8);
		memcpy(dst, &x, 8);
	} else {
		ld_error_report(ctx, "--build-id=%s is not supported", ctx->options.build_id);
	}
}
