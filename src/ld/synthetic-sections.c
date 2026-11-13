/* Appending the synthetic input sections (.rela.dyn, .symtab, .strtab and the like) after all files. */

static void add_synthetic_sections(void)
{
	/* lld appends synthetic input sections after all files, in this order */
	comment_isec = new_isec(".comment", K_MERGE_IN, SHT_PROGBITS, SHF_MERGE | SHF_STRINGS, 1);
	{
		ISec *c = &isecs.v[comment_isec];
		size_t n = strlen(opt.comment) + 1;
		uint8_t *d = alloc(n);
		memcpy(d, opt.comment, n);
		c->data = d;
		c->size = n;
		c->entsize = 1;
	}
	/* (lld always has .rela.dyn: dropped when empty, but a script that names it
	   keeps an empty RELA output section) */
	if (pic || (x86_ld && opt.script && !opt.relocatable))
	{
		reladyn_isec = new_isec(".rela.dyn", K_RELADYN, SHT_RELA, SHF_ALLOC, 8);
		isecs.v[reladyn_isec].entsize = sizeof(Rela);
	}
	/* dynamic sections only under a script (the vDSO; vmlinux discards them): a
	 * script-less -shared link keeps its old output, with no .dynamic yet */
	if (opt.shared && opt.script) {
		dynsym_isec = new_isec(".dynsym", K_DYNSYM, SHT_DYNSYM, SHF_ALLOC, 8);
		isecs.v[dynsym_isec].entsize = sizeof(Sym);
		dynstr_isec = new_isec(".dynstr", K_DYNSTR, SHT_STRTAB, SHF_ALLOC, 1);
		dynamic_isec = new_isec(".dynamic", K_DYNAMIC, SHT_DYNAMIC, SHF_ALLOC | SHF_WRITE, 8);
		isecs.v[dynamic_isec].entsize = 16;
		if (nverdefs) {
			versym_isec = new_isec(".gnu.version", K_VERSYM, SHT_GNU_VERSYM, SHF_ALLOC, 2);
			isecs.v[versym_isec].entsize = 2;
			verdef_isec = new_isec(".gnu.version_d", K_VERDEF, SHT_GNU_VERDEF, SHF_ALLOC, 4);
		}
		gnuhash_isec = new_isec(".gnu.hash", K_GNUHASH, SHT_GNU_HASH, SHF_ALLOC, 8);
		hash_isec = new_isec(".hash", K_HASH, SHT_HASH, SHF_ALLOC, 4);
		isecs.v[hash_isec].entsize = 4;
	}
	if (opt.build_id) {
		buildid_isec = new_isec(".note.gnu.build-id", K_BUILDID, SHT_NOTE, SHF_ALLOC, 4);
		isecs.v[buildid_isec].size = 16 + (!strcmp(opt.build_id, "md5") ? 16 :
						   !strcmp(opt.build_id, "fast") ? 8 : 20);
	}
	/* lld: .eh_frame_hdr when there is an .eh_frame (sized by eh_frames) */
	for (uint32_t i = 0; opt.eh_frame_hdr && !opt.relocatable && ehhdr_isec == NONE && i < isecs.n; i++)
		if (eh_input(&isecs.v[i]))
			ehhdr_isec = new_isec(".eh_frame_hdr", K_EHHDR, SHT_PROGBITS, SHF_ALLOC, 4);
	/* only when some GOT relocation needs it (lld drops an empty .got); sized after the scan */
	if (any_got())
		got_isec = new_isec(".got", K_GOT, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 8);
	symtab_isec = new_isec(".symtab", K_SYMTAB, SHT_SYMTAB, 0, 8);
	isecs.v[symtab_isec].entsize = sizeof(Sym);
	shstrtab_isec = new_isec(".shstrtab", K_SHSTRTAB, SHT_STRTAB, 0, 1);
	strtab_isec = new_isec(".strtab", K_STRTAB, SHT_STRTAB, 0, 1);
}
