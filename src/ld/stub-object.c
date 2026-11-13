/* The in-memory stub object that defines the symbols the -O0 kernel references only in dead branches. */

/* ---------------- stubs for unresolved symbols (-O0 dead branches) ---------------- */

static int ld_string_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

/*
 * The -O0 kernel references symbols that only exist in configurations
 * where -O2 would have deleted the reference.  vroom-ld retried the link
 * with an assembled stub object: each symbol becomes 64 zero bytes in
 * .data (writable so static keys work, NX so stray calls trap).  The same
 * object is synthesized here, in memory, so the result is identical to
 * that retry without running anything.
 */
static ld_object *ld_stub_object_create(const char **names, uint32_t n)
{
	size_t strsz = 1 + 3;
	for (uint32_t i = 0; i < n; i++)
		strsz += strlen(names[i]) + 1;
	size_t datasz = 64 * (size_t)n;
	uint8_t *b = ld_region_alloc(sizeof(elf_file_header) + 5 * sizeof(elf_section_header) + (n + 2) * sizeof(elf_symbol) + strsz + 64 + datasz);
	elf_file_header *eh = (elf_file_header *)b;
	elf_section_header *sh = (elf_section_header *)(b + sizeof(elf_file_header));
	elf_symbol *sy = (elf_symbol *)(sh + 5);
	char *str = (char *)(sy + n + 2);
	char *shstr = str + strsz;
	memcpy(eh->e_ident, "\177ELF\2\1\1", 7);
	eh->e_type = 1;
	eh->e_machine = ctx->inputs.x86 ? 62 : 183;
	eh->e_shoff = sizeof(elf_file_header);
	eh->e_shnum = 5;
	eh->e_shstrndx = 1;
	/* shstr: "\0.text\0.data\0.symtab\0" in a small separate buffer */
	memcpy(shstr, "\0.text\0.data\0.symtab\0.strtab\0", 30);
	sh[1] = (elf_section_header){.sh_name = 22, .sh_type = SHT_STRTAB, .sh_offset = (uint64_t)(shstr - (char *)b), .sh_size = 30, .sh_addralign = 1};
	sh[2] = (elf_section_header){.sh_name = 1, .sh_type = SHT_PROGBITS, .sh_flags = SHF_ALLOC | SHF_EXECINSTR, .sh_addralign = 4};
	sh[3] = (elf_section_header){.sh_name = 7, .sh_type = SHT_PROGBITS, .sh_flags = SHF_ALLOC | SHF_WRITE, .sh_size = datasz,
		       .sh_offset = (uint64_t)(shstr + 32 - (char *)b), .sh_addralign = 16};
	sh[4] = (elf_section_header){.sh_name = 13, .sh_type = SHT_SYMTAB, .sh_offset = (uint64_t)((uint8_t *)sy - b),
		       .sh_size = (n + 2) * sizeof(elf_symbol), .sh_link = 1, .sh_info = 2, .sh_entsize = sizeof(elf_symbol), .sh_addralign = 8};
	/* the symbol strtab shares section 1 in the real object; here it's separate memory */
	str[0] = 0;
	memcpy(str + 1, "$d", 3);
	size_t so = 4;
	sy[1] = (elf_symbol){.st_name = 1, .st_info = STB_LOCAL << 4 | STT_NOTYPE, .st_shndx = 3};
	for (uint32_t i = 0; i < n; i++) {
		size_t l = strlen(names[i]);
		memcpy(str + so, names[i], l + 1);
		sy[i + 2] = (elf_symbol){.st_name = (uint32_t)so, .st_info = STB_GLOBAL << 4 | STT_NOTYPE,
				  .st_shndx = 3, .st_value = 64 * (uint64_t)i};
		so += l + 1;
	}
	ld_object *o = ld_region_alloc(sizeof *o);
	o->path = "<vroom-stubs>";
	o->buf = b;
	o->size = (uint8_t *)shstr + 32 + datasz - b;
	o->sh = sh;
	o->nsh = 5;
	o->shstr = shstr;
	o->sym = sy;
	o->nsym = n + 2;
	o->firstg = 2;
	o->str = str;
	o->gs = ld_region_alloc(n * sizeof(uint32_t));
	return o;
}
