/* Appending the synthetic input sections (.rela.dyn, .symtab, .strtab and the like) after all files. */

static void ld_synthetic_sections_add(void)
{
	/* lld appends synthetic input sections after all files, in this order */
	ctx->output.comment_section = ld_input_section_create(".comment", LD_SECTION_KIND_MERGE_INPUT, SHT_PROGBITS, SHF_MERGE | SHF_STRINGS, 1);
	{
		ld_input_section *c = &ctx->inputs.sections.v[ctx->output.comment_section];
		size_t n = strlen(ctx->options.comment) + 1;
		uint8_t *d = ld_region_alloc(n);
		memcpy(d, ctx->options.comment, n);
		c->data = d;
		c->size = n;
		c->entsize = 1;
	}
	/* (lld always has .rela.dyn: dropped when empty, but a script that names it
	   keeps an empty RELA output section) */
	if (ctx->relocations.pic || (ctx->inputs.x86 && ctx->options.script && !ctx->options.relocatable))
	{
		ctx->output.rela_dyn_section = ld_input_section_create(".rela.dyn", LD_SECTION_KIND_DYNAMIC_RELOCATIONS, SHT_RELA, SHF_ALLOC, 8);
		ctx->inputs.sections.v[ctx->output.rela_dyn_section].entsize = sizeof(elf_rela);
	}
	/* dynamic sections only under a script (the vDSO; vmlinux discards them): a
	 * script-less -shared link keeps its old output, with no .dynamic yet */
	if (ctx->options.shared && ctx->options.script) {
		ctx->output.dynsym_section = ld_input_section_create(".dynsym", LD_SECTION_KIND_DYNAMIC_SYMBOLS, SHT_DYNSYM, SHF_ALLOC, 8);
		ctx->inputs.sections.v[ctx->output.dynsym_section].entsize = sizeof(elf_symbol);
		ctx->output.dynstr_section = ld_input_section_create(".dynstr", LD_SECTION_KIND_DYNAMIC_STRINGS, SHT_STRTAB, SHF_ALLOC, 1);
		ctx->output.dynamic_section = ld_input_section_create(".dynamic", LD_SECTION_KIND_DYNAMIC, SHT_DYNAMIC, SHF_ALLOC | SHF_WRITE, 8);
		ctx->inputs.sections.v[ctx->output.dynamic_section].entsize = 16;
		if (ctx->script.version_definition_count) {
			ctx->output.versym_section = ld_input_section_create(".gnu.version", LD_SECTION_KIND_SYMBOL_VERSIONS, SHT_GNU_VERSYM, SHF_ALLOC, 2);
			ctx->inputs.sections.v[ctx->output.versym_section].entsize = 2;
			ctx->output.verdef_section = ld_input_section_create(".gnu.version_d", LD_SECTION_KIND_VERSION_DEFINITIONS, SHT_GNU_VERDEF, SHF_ALLOC, 4);
		}
		ctx->output.gnu_hash_section = ld_input_section_create(".gnu.hash", LD_SECTION_KIND_GNU_HASH_TABLE, SHT_GNU_HASH, SHF_ALLOC, 8);
		ctx->output.hash_section = ld_input_section_create(".hash", LD_SECTION_KIND_HASH_TABLE, SHT_HASH, SHF_ALLOC, 4);
		ctx->inputs.sections.v[ctx->output.hash_section].entsize = 4;
	}
	if (ctx->options.build_id) {
		ctx->output.build_id_section = ld_input_section_create(".note.gnu.build-id", LD_SECTION_KIND_BUILD_ID, SHT_NOTE, SHF_ALLOC, 4);
		ctx->inputs.sections.v[ctx->output.build_id_section].size = 16 + (!strcmp(ctx->options.build_id, "md5") ? 16 :
						   !strcmp(ctx->options.build_id, "fast") ? 8 : 20);
	}
	/* lld: .eh_frame_hdr when there is an .eh_frame (sized by ld_eh_frame_merge) */
	for (uint32_t i = 0; ctx->options.eh_frame_hdr && !ctx->options.relocatable && ctx->output.eh_frame_header_section == LD_NONE && i < ctx->inputs.sections.n; i++)
		if (ld_eh_frame_is_input(&ctx->inputs.sections.v[i]))
			ctx->output.eh_frame_header_section = ld_input_section_create(".eh_frame_hdr", LD_SECTION_KIND_EH_FRAME_HEADER, SHT_PROGBITS, SHF_ALLOC, 4);
	/* only when some GOT relocation needs it (lld drops an empty .got); sized after the scan */
	if (ld_got_is_needed())
		ctx->output.got_section = ld_input_section_create(".got", LD_SECTION_KIND_GOT, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 8);
	ctx->output.symtab_section = ld_input_section_create(".symtab", LD_SECTION_KIND_SYMBOL_TABLE, SHT_SYMTAB, 0, 8);
	ctx->inputs.sections.v[ctx->output.symtab_section].entsize = sizeof(elf_symbol);
	ctx->output.shstrtab_section = ld_input_section_create(".shstrtab", LD_SECTION_KIND_SECTION_NAME_TABLE, SHT_STRTAB, 0, 1);
	ctx->output.strtab_section = ld_input_section_create(".strtab", LD_SECTION_KIND_STRING_TABLE, SHT_STRTAB, 0, 1);
}
