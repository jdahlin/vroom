/* The section name table, the ELF and section headers, synthetic contents and the build id. */

/* ---------------- output ---------------- */

static uint8_t *shstrtab_buf;

static void ld_section_name_table_build(void)
{
	/* deduplicated, in section header order (StringTableBuilder in insertion order) */
	size_t cap = 1;
	for (uint32_t i = 0; i < outsecs.n; i++)
		cap += strlen(osecs.v[outsecs.v[i]].name) + 1;
	shstrtab_buf = ld_region_alloc(cap);
	size_t n = 1;
	for (uint32_t i = 0; i < outsecs.n; i++) {
		ld_output_section *os = &osecs.v[outsecs.v[i]];
		size_t l = strlen(os->name);
		uint32_t found = LD_NONE;
		for (uint32_t k = 0; k < i; k++)
			if (!strcmp(osecs.v[outsecs.v[k]].name, os->name)) {
				found = osecs.v[outsecs.v[k]].nameoff;
				break;
			}
		if (found != LD_NONE) {
			os->nameoff = found;
			continue;
		}
		memcpy(shstrtab_buf + n, os->name, l + 1);
		os->nameoff = n;
		n += l + 1;
	}
	isecs.v[shstrtab_isec].size = n;
}

static void ld_headers_write(void)
{
	elf_file_header *eh = (elf_file_header *)out;
	memcpy(eh->e_ident, "\177ELF\2\1\1", 7);
	eh->e_type = opt.relocatable ? 1 : opt.shared ? 3 : 2;
	eh->e_machine = x86_ld ? 62 : 183;
	eh->e_version = 1;
	/* lld: -e, else ENTRY(), else _start (silently absent) */
	const char *en = opt.entry ? opt.entry : entry_name ? entry_name : "_start";
	uint32_t eg = ld_global_name_find(en);
	if (!opt.relocatable && (opt.entry || entry_name) && (eg == LD_NONE || gsyms[eg].state == LD_SYMBOL_STATE_UNDEFINED))
		ld_warning_report("cannot find entry symbol %s; not setting start address", en);
	if (!opt.relocatable)
		eh->e_entry = eg != LD_NONE && gsyms[eg].state != LD_SYMBOL_STATE_UNDEFINED ? ld_global_symbol_address(eg) : 0;
	eh->e_phoff = phdrs.n ? sizeof(elf_file_header) : 0;
	eh->e_shoff = shoff;
	eh->e_ehsize = sizeof(elf_file_header);
	eh->e_phentsize = phdrs.n ? sizeof(elf_program_header) : 0;
	eh->e_phnum = phdrs.n;
	eh->e_shentsize = sizeof(elf_section_header);
	eh->e_shnum = outsecs.n + 1;
	eh->e_shstrndx = osecs.v[ld_input_section_output_section(shstrtab_isec)].shidx;
	if (phdrs.n)
		memcpy(out + sizeof(elf_file_header), phdrs.v, phdrs.n * sizeof(elf_program_header));
	elf_section_header *sh = (elf_section_header *)(out + shoff);
	for (uint32_t i = 0; i < outsecs.n; i++) {
		ld_output_section *os = &osecs.v[outsecs.v[i]];
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

static void ld_synthetic_sections_write(void)
{
	/* .rela.dyn, .symtab and .strtab were written in place */
	ld_input_section *is = &isecs.v[shstrtab_isec];
	memcpy(out + osecs.v[ld_input_section_output_section(shstrtab_isec)].offset + is->off, shstrtab_buf, is->size);
}

/* --build-id: lld's "sha1"/"tree" ids are a BLAKE3 tree over 1 MiB chunks */
static void ld_build_id_write(void)
{
	if (buildid_isec == LD_NONE)
		return;
	ld_input_section *is = &isecs.v[buildid_isec];
	uint8_t *dst = out + osecs.v[ld_input_section_output_section(buildid_isec)].offset + is->off + 16;
	size_t hl = is->size - 16;
	size_t nchunks = (filesize + (1u << 20) - 1) >> 20;
	uint8_t *hashes = ld_region_alloc(nchunks * 20 + 32);
	if (!strcmp(opt.build_id, "sha1") || !strcmp(opt.build_id, "tree")) {
		ld_parallel_for(nchunks, ld_build_id_chunk_hash, hashes);
		vdigest_blake3(hashes, nchunks * 20, dst, hl);
	} else if (!strcmp(opt.build_id, "fast")) {
		/* xxh3 of the chunk hashes (lld: --build-id=fast is xxh3_64bits per chunk) */
		uint8_t *h8 = ld_region_alloc(nchunks * 8);
		for (size_t i = 0; i < nchunks; i++) {
			uint64_t b = (uint64_t)i << 20, n = filesize - b < (1u << 20) ? filesize - b : (1u << 20);
			uint64_t x = vdigest_xxh3(out + b, n);
			memcpy(h8 + i * 8, &x, 8);
		}
		uint64_t x = vdigest_xxh3(h8, nchunks * 8);
		memcpy(dst, &x, 8);
	} else {
		ld_error_report("--build-id=%s is not supported", opt.build_id);
	}
}
