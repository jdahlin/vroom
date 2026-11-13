/* Appending the synthetic input sections (.rela.dyn, .symtab, .strtab and the like) after all files. */

static void ld_synthetic_sections_add(void)
{
	/* lld appends synthetic input sections after all files, in this order */
	comment_isec = ld_input_section_create(".comment", LD_SECTION_KIND_MERGE_INPUT, SHT_PROGBITS, SHF_MERGE | SHF_STRINGS, 1);
	{
		ld_input_section *c = &isecs.v[comment_isec];
		size_t n = strlen(opt.comment) + 1;
		uint8_t *d = ld_region_alloc(n);
		memcpy(d, opt.comment, n);
		c->data = d;
		c->size = n;
		c->entsize = 1;
	}
	/* (lld always has .rela.dyn: dropped when empty, but a script that names it
	   keeps an empty RELA output section) */
	if (pic || (x86_ld && opt.script && !opt.relocatable))
	{
		reladyn_isec = ld_input_section_create(".rela.dyn", LD_SECTION_KIND_DYNAMIC_RELOCATIONS, SHT_RELA, SHF_ALLOC, 8);
		isecs.v[reladyn_isec].entsize = sizeof(elf_rela);
	}
	/* dynamic sections only under a script (the vDSO; vmlinux discards them): a
	 * script-less -shared link keeps its old output, with no .dynamic yet */
	if (opt.shared && opt.script) {
		dynsym_isec = ld_input_section_create(".dynsym", LD_SECTION_KIND_DYNAMIC_SYMBOLS, SHT_DYNSYM, SHF_ALLOC, 8);
		isecs.v[dynsym_isec].entsize = sizeof(elf_symbol);
		dynstr_isec = ld_input_section_create(".dynstr", LD_SECTION_KIND_DYNAMIC_STRINGS, SHT_STRTAB, SHF_ALLOC, 1);
		dynamic_isec = ld_input_section_create(".dynamic", LD_SECTION_KIND_DYNAMIC, SHT_DYNAMIC, SHF_ALLOC | SHF_WRITE, 8);
		isecs.v[dynamic_isec].entsize = 16;
		if (nverdefs) {
			versym_isec = ld_input_section_create(".gnu.version", LD_SECTION_KIND_SYMBOL_VERSIONS, SHT_GNU_VERSYM, SHF_ALLOC, 2);
			isecs.v[versym_isec].entsize = 2;
			verdef_isec = ld_input_section_create(".gnu.version_d", LD_SECTION_KIND_VERSION_DEFINITIONS, SHT_GNU_VERDEF, SHF_ALLOC, 4);
		}
		gnuhash_isec = ld_input_section_create(".gnu.hash", LD_SECTION_KIND_GNU_HASH_TABLE, SHT_GNU_HASH, SHF_ALLOC, 8);
		hash_isec = ld_input_section_create(".hash", LD_SECTION_KIND_HASH_TABLE, SHT_HASH, SHF_ALLOC, 4);
		isecs.v[hash_isec].entsize = 4;
	}
	if (opt.build_id) {
		buildid_isec = ld_input_section_create(".note.gnu.build-id", LD_SECTION_KIND_BUILD_ID, SHT_NOTE, SHF_ALLOC, 4);
		isecs.v[buildid_isec].size = 16 + (!strcmp(opt.build_id, "md5") ? 16 :
						   !strcmp(opt.build_id, "fast") ? 8 : 20);
	}
	/* lld: .eh_frame_hdr when there is an .eh_frame (sized by ld_eh_frame_merge) */
	for (uint32_t i = 0; opt.eh_frame_hdr && !opt.relocatable && ehhdr_isec == LD_NONE && i < isecs.n; i++)
		if (ld_eh_frame_is_input(&isecs.v[i]))
			ehhdr_isec = ld_input_section_create(".eh_frame_hdr", LD_SECTION_KIND_EH_FRAME_HEADER, SHT_PROGBITS, SHF_ALLOC, 4);
	/* only when some GOT relocation needs it (lld drops an empty .got); sized after the scan */
	if (ld_got_is_needed())
		got_isec = ld_input_section_create(".got", LD_SECTION_KIND_GOT, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 8);
	symtab_isec = ld_input_section_create(".symtab", LD_SECTION_KIND_SYMBOL_TABLE, SHT_SYMTAB, 0, 8);
	isecs.v[symtab_isec].entsize = sizeof(elf_symbol);
	shstrtab_isec = ld_input_section_create(".shstrtab", LD_SECTION_KIND_SECTION_NAME_TABLE, SHT_STRTAB, 0, 1);
	strtab_isec = ld_input_section_create(".strtab", LD_SECTION_KIND_STRING_TABLE, SHT_STRTAB, 0, 1);
}
