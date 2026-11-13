/* The section name table, the ELF and section headers, synthetic contents and the build id. */

/* ---------------- output ---------------- */

static uint8_t *shstrtab_buf;

static void build_shstrtab(void)
{
	/* deduplicated, in section header order (StringTableBuilder in insertion order) */
	size_t cap = 1;
	for (uint32_t i = 0; i < outsecs.n; i++)
		cap += strlen(osecs.v[outsecs.v[i]].name) + 1;
	shstrtab_buf = alloc(cap);
	size_t n = 1;
	for (uint32_t i = 0; i < outsecs.n; i++) {
		OSec *os = &osecs.v[outsecs.v[i]];
		size_t l = strlen(os->name);
		uint32_t found = NONE;
		for (uint32_t k = 0; k < i; k++)
			if (!strcmp(osecs.v[outsecs.v[k]].name, os->name)) {
				found = osecs.v[outsecs.v[k]].nameoff;
				break;
			}
		if (found != NONE) {
			os->nameoff = found;
			continue;
		}
		memcpy(shstrtab_buf + n, os->name, l + 1);
		os->nameoff = n;
		n += l + 1;
	}
	isecs.v[shstrtab_isec].size = n;
}

static void write_headers(void)
{
	Ehdr *eh = (Ehdr *)out;
	memcpy(eh->e_ident, "\177ELF\2\1\1", 7);
	eh->e_type = opt.relocatable ? 1 : opt.shared ? 3 : 2;
	eh->e_machine = x86_ld ? 62 : 183;
	eh->e_version = 1;
	/* lld: -e, else ENTRY(), else _start (silently absent) */
	const char *en = opt.entry ? opt.entry : entry_name ? entry_name : "_start";
	uint32_t eg = gfind(en);
	if (!opt.relocatable && (opt.entry || entry_name) && (eg == NONE || gsyms[eg].state == GS_UNDEF))
		warn("cannot find entry symbol %s; not setting start address", en);
	if (!opt.relocatable)
		eh->e_entry = eg != NONE && gsyms[eg].state != GS_UNDEF ? gsym_va(eg) : 0;
	eh->e_phoff = phdrs.n ? sizeof(Ehdr) : 0;
	eh->e_shoff = shoff;
	eh->e_ehsize = sizeof(Ehdr);
	eh->e_phentsize = phdrs.n ? sizeof(Phdr) : 0;
	eh->e_phnum = phdrs.n;
	eh->e_shentsize = sizeof(Shdr);
	eh->e_shnum = outsecs.n + 1;
	eh->e_shstrndx = osecs.v[isec_osec(shstrtab_isec)].shidx;
	if (phdrs.n)
		memcpy(out + sizeof(Ehdr), phdrs.v, phdrs.n * sizeof(Phdr));
	Shdr *sh = (Shdr *)(out + shoff);
	for (uint32_t i = 0; i < outsecs.n; i++) {
		OSec *os = &osecs.v[outsecs.v[i]];
		Shdr *d = &sh[i + 1];
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

static void write_synthetic(void)
{
	/* .rela.dyn, .symtab and .strtab were written in place */
	ISec *is = &isecs.v[shstrtab_isec];
	memcpy(out + osecs.v[isec_osec(shstrtab_isec)].offset + is->off, shstrtab_buf, is->size);
}

/* --build-id: lld's "sha1"/"tree" ids are a BLAKE3 tree over 1 MiB chunks */
static void write_build_id(void)
{
	if (buildid_isec == NONE)
		return;
	ISec *is = &isecs.v[buildid_isec];
	uint8_t *dst = out + osecs.v[isec_osec(buildid_isec)].offset + is->off + 16;
	size_t hl = is->size - 16;
	size_t nchunks = (filesize + (1u << 20) - 1) >> 20;
	uint8_t *hashes = alloc(nchunks * 20 + 32);
	if (!strcmp(opt.build_id, "sha1") || !strcmp(opt.build_id, "tree")) {
		pfor(nchunks, hash_chunk, hashes);
		vdigest_blake3(hashes, nchunks * 20, dst, hl);
	} else if (!strcmp(opt.build_id, "fast")) {
		/* xxh3 of the chunk hashes (lld: --build-id=fast is xxh3_64bits per chunk) */
		uint8_t *h8 = alloc(nchunks * 8);
		for (size_t i = 0; i < nchunks; i++) {
			uint64_t b = (uint64_t)i << 20, n = filesize - b < (1u << 20) ? filesize - b : (1u << 20);
			uint64_t x = vdigest_xxh3(out + b, n);
			memcpy(h8 + i * 8, &x, 8);
		}
		uint64_t x = vdigest_xxh3(h8, nchunks * 8);
		memcpy(dst, &x, 8);
	} else {
		error("--build-id=%s is not supported", opt.build_id);
	}
}
