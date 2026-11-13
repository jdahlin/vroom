/* Validation of a relocatable ELF input (every offset and size is checked before use) and its parsing into an Obj. */

/* the bytes of a relocation's field (0: none, 4: an instruction) */
static uint32_t ld_relocation_width(uint32_t type, uint16_t machine)
{
	if (machine == 62)	/* x86-64: the fields relocation.inc writes */
		switch (type) {
		case R_X86_64_NONE: return 0;
		case R_X86_64_64: case R_X86_64_PC64: case R_X86_64_TPOFF64: case R_X86_64_DTPOFF64:
		case R_X86_64_SIZE64: return 8;
		case R_X86_64_16: case R_X86_64_PC16: return 2;
		case R_X86_64_8: case R_X86_64_PC8: return 1;
		default: return 4;
		}
	switch (type) {
	case R_AARCH64_NONE: return 0;
	case R_AARCH64_ABS64: case R_AARCH64_PREL64: return 8;
	case R_AARCH64_ABS16: case R_AARCH64_PREL16: return 2;
	default: return 4;
	}
}

/* a string table: in the file, NUL-terminated */
static bool ld_string_table_is_valid(const ld_object *o, uint32_t k)
{
	const elf_section_header *h = &o->sh[k];

	return h->sh_type == SHT_STRTAB && h->sh_size && o->buf[h->sh_offset + h->sh_size - 1] == 0;
}


/* every offset, index and size the later passes use, checked once (as lld's
 * ObjFile: the message names what is wrong; the object is then dropped) */
static bool ld_object_is_valid(ld_object *o, const elf_file_header *eh)
{
#define LD_OBJECT_INVALID(...) do { if (!ctx->inputs.quiet) ld_error_report(__VA_ARGS__); return false; } while (0)
	uint64_t n = o->size;

	if (eh->e_shoff == 0 || eh->e_shentsize != sizeof(elf_section_header) || eh->e_shoff % 8 ||
	    eh->e_shoff > n || n - eh->e_shoff < sizeof(elf_section_header))
		LD_OBJECT_INVALID("%s: invalid section header table offset", o->path);
	o->sh = (const elf_section_header *)(o->buf + eh->e_shoff);
	/* more than 0xff00 sections: the count in sh[0].sh_size, the index in sh_link */
	uint64_t nsh = eh->e_shnum ? eh->e_shnum : o->sh[0].sh_size;
	uint32_t shstrndx = eh->e_shstrndx == SHN_XINDEX ? o->sh[0].sh_link : eh->e_shstrndx;

	if (nsh == 0 || nsh > (n - eh->e_shoff) / sizeof(elf_section_header))
		LD_OBJECT_INVALID("%s: section header table goes past the end of the file", o->path);
	o->nsh = nsh;
	for (uint32_t s = 0; s < o->nsh; s++) {	/* (0 too: shstrndx and sh_link may name it) */
		const elf_section_header *h = &o->sh[s];

		if (h->sh_type != SHT_NOBITS && h->sh_type != SHT_NULL && !vchecked_range(h->sh_offset, h->sh_size, n))
			LD_OBJECT_INVALID("%s: section %u goes past the end of the file", o->path, s);
		if (h->sh_addralign & (h->sh_addralign - 1))
			LD_OBJECT_INVALID("%s: section %u: sh_addralign is not a power of 2", o->path, s);
	}
	if (shstrndx >= o->nsh || !ld_string_table_is_valid(o, shstrndx))
		LD_OBJECT_INVALID("%s: invalid section name string table", o->path);
	o->shstr = (const char *)o->buf + o->sh[shstrndx].sh_offset;
	uint64_t shstrsz = o->sh[shstrndx].sh_size;
	const elf_section_header *symtab = NULL;

	for (uint32_t s = 0; s < o->nsh; s++) {
		const elf_section_header *h = &o->sh[s];

		if (h->sh_name >= shstrsz)
			LD_OBJECT_INVALID("%s: section %u: invalid sh_name", o->path, s);
		if (h->sh_type == SHT_SYMTAB) {
			if (symtab)
				LD_OBJECT_INVALID("%s: more than one SHT_SYMTAB", o->path);
			symtab = h;
		}
	}
	uint64_t nsym = 0, strsz = 0;

	if (symtab) {
		if (symtab->sh_offset % 8 || symtab->sh_size % sizeof(elf_symbol))
			LD_OBJECT_INVALID("%s: invalid SHT_SYMTAB", o->path);
		if (symtab->sh_link >= o->nsh || !ld_string_table_is_valid(o, symtab->sh_link))
			LD_OBJECT_INVALID("%s: invalid symbol string table", o->path);
		nsym = symtab->sh_size / sizeof(elf_symbol);
		if (nsym > UINT32_MAX || symtab->sh_info > nsym || (nsym && symtab->sh_info == 0))
			LD_OBJECT_INVALID("%s: invalid sh_info in symbol table", o->path);
		strsz = o->sh[symtab->sh_link].sh_size;
		const elf_symbol *y = (const elf_symbol *)(o->buf + symtab->sh_offset);

		for (uint64_t k = 0; k < nsym; k++) {
			if (y[k].st_name >= strsz)
				LD_OBJECT_INVALID("%s: invalid symbol name offset", o->path);
			uint16_t x = y[k].st_shndx;

			if (x == SHN_XINDEX)
				LD_OBJECT_INVALID("%s: SHN_XINDEX symbols are not supported", o->path);
			if (x != SHN_UNDEF && x < 0xff00 && x >= o->nsh)
				LD_OBJECT_INVALID("%s: invalid section index: %u", o->path, x);
		}
	}
	for (uint32_t s = 0; s < o->nsh; s++) {
		const elf_section_header *h = &o->sh[s];

		if (h->sh_type == SHT_GROUP) {
			if (h->sh_offset % 4 || h->sh_size % 4 || h->sh_size < 4 || !symtab ||
			    h->sh_link >= o->nsh || &o->sh[h->sh_link] != symtab || h->sh_info >= nsym)
				LD_OBJECT_INVALID("%s: invalid SHT_GROUP section %u", o->path, s);
			const uint32_t *w = (const uint32_t *)(o->buf + h->sh_offset);

			for (uint64_t k = 1; k < h->sh_size / 4; k++)
				if (w[k] == 0 || w[k] >= o->nsh)
					LD_OBJECT_INVALID("%s: invalid section index in group section %u", o->path, s);
			continue;
		}
		if (h->sh_type != SHT_RELA)
			continue;
		if (h->sh_offset % 8 || h->sh_size % sizeof(elf_rela) || h->sh_info >= o->nsh)
			LD_OBJECT_INVALID("%s: invalid relocation section %u", o->path, s);
		const elf_section_header *t = &o->sh[h->sh_info];
		const elf_rela *r = (const elf_rela *)(o->buf + h->sh_offset);

		for (uint64_t k = 0; k < h->sh_size / sizeof(elf_rela); k++) {
			if ((r[k].r_info >> 32) >= nsym && (r[k].r_info >> 32))
				LD_OBJECT_INVALID("%s: invalid symbol index in relocation section %u", o->path, s);
			uint32_t w = ld_relocation_width((uint32_t)r[k].r_info, eh->e_machine);

			if (t->sh_type != SHT_NOBITS && (r[k].r_offset > t->sh_size || t->sh_size - r[k].r_offset < w))
				LD_OBJECT_INVALID("%s: relocation at 0x%llx is past the end of section %u", o->path,
				    (unsigned long long)r[k].r_offset, h->sh_info);
		}
	}
	return true;
#undef LD_OBJECT_INVALID
}

static void ld_object_parse(void *arg, size_t i)
{
	(void)arg;
	ld_object *o = ctx->inputs.objects.v[i];
	if (!o->buf) {
		o->buf = ld_file_map(o->path, &o->size);
		o->mapped = true;
		if (!o->buf)
			return;
	}
	if (((uintptr_t)o->buf & 7) && !o->mapped) {
		/* a member of a regular archive (at 4 mod 8, after its header): the headers need 8-byte alignment */
		uint8_t *c = ld_region_alloc(o->size);

		memcpy(c, o->buf, o->size);
		o->buf = c;
	}
	const elf_file_header *eh = (const elf_file_header *)o->buf;
	if (o->size < sizeof(elf_file_header) || memcmp(eh->e_ident, "\177ELF", 4) || eh->e_ident[4] != 2 ||
	    eh->e_ident[5] != 1) {
		ld_error_report("%s: not an ELF64 little-endian file", o->path);
		o->buf = NULL;
		return;
	}
	if (eh->e_type != 1 || (eh->e_machine != 183 && eh->e_machine != 62)) {
		ld_error_report("%s: not an AArch64 or x86-64 relocatable object", o->path);
		o->buf = NULL;
		return;
	}
	uint16_t m0 = 0;

	if (!atomic_compare_exchange_strong(&ctx->inputs.machine, &m0, eh->e_machine) && m0 != eh->e_machine) {
		ld_error_report("%s is incompatible with the other inputs (%s, not %s)", o->path,
		      eh->e_machine == 62 ? "x86-64" : "AArch64", m0 == 62 ? "x86-64" : "AArch64");
		o->buf = NULL;
		return;
	}
	if (eh->e_machine == 62)
		ctx->inputs.x86 = true;	/* (one machine for all inputs: checked above) */
	if (!ld_object_is_valid(o, eh)) {
		o->buf = NULL;
		return;
	}
	for (uint32_t s = 0; s < o->nsh; s++) {
		if (o->sh[s].sh_type == SHT_SYMTAB) {
			o->sym = (const elf_symbol *)(o->buf + o->sh[s].sh_offset);
			o->nsym = o->sh[s].sh_size / sizeof(elf_symbol);
			o->firstg = o->sh[s].sh_info;
			o->str = (const char *)o->buf + o->sh[o->sh[s].sh_link].sh_offset;
		}
	}
	if (o->nsym > o->firstg)
		o->gs = ld_region_alloc((o->nsym - o->firstg) * sizeof(uint32_t));
}
