/* The header size, the input-section-name cache, committing sections to output sections, and lld's orphan section ranking. */

/* ---------------- layout ---------------- */

static uint64_t ld_headers_size(void)
{
	return sizeof(elf_file_header) + (uint64_t)(have_phdrs ? sphdrs.n : phdrs.n) * sizeof(elf_program_header);
}

/* ---------------- section assignment ---------------- */

/* input section name -> its ld_input_section_description (or LD_NONE), open addressing; grown at half
   full: an -ffunction-sections link has about as many names as sections */
typedef struct { const char *name; uint32_t h; uint32_t isd; } ld_name_cache_entry;
static ld_name_cache_entry *ncache;
static uint32_t ncmask, ncount;

static void ld_name_cache_grow(void)
{
	uint32_t ncap = (ncmask + 1) * 2;
	ld_name_cache_entry *nc = ld_region_alloc(ncap * sizeof *nc);

	memset(nc, 0, ncap * sizeof *nc);
	for (uint32_t i = 0; i <= ncmask; i++)
		if (ncache[i].name) {
			uint32_t j = ncache[i].h & (ncap - 1);

			while (nc[j].name)
				j = (j + 1) & (ncap - 1);
			nc[j] = ncache[i];
		}
	ncache = nc;
	ncmask = ncap - 1;
}

static uint32_t ld_input_section_description_match(const char *name)
{
	size_t n = strlen(name);
	uint32_t h = ld_name_hash(name, n);
	uint32_t j = h & ncmask;
	while (ncache[j].name) {
		if (ncache[j].h == h && !strcmp(ncache[j].name, name))
			return ncache[j].isd;
		j = (j + 1) & ncmask;
	}
	uint32_t res = LD_NONE;
	for (uint32_t i = 0; i < topcmds.n && res == LD_NONE; i++) {
		ld_command *c = &topcmds.v[i];
		if (c->kind != LD_COMMAND_OUTPUT_SECTION)
			continue;
		ld_output_section *os = &osecs.v[c->idx];
		for (uint32_t k = 0; k < os->cmds.n && res == LD_NONE; k++) {
			ld_command *cc = &os->cmds.v[k];
			if (cc->kind != LD_COMMAND_INPUT_SECTION_DESCRIPTION)
				continue;
			ld_input_section_description *d = &isds.v[cc->idx];
			for (uint32_t p = 0; p < d->npats; p++)
				if (ld_pattern_match(&d->pats[p], name)) {
					res = cc->idx;
					break;
				}
		}
	}
	ncache[j] = (ld_name_cache_entry){.name = name, .h = h, .isd = res};
	if (++ncount * 2 > ncmask + 1)
		ld_name_cache_grow();
	return res;
}

static int ld_section_name_stable_cmp(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
	int c = strcmp(isecs.v[x].name, isecs.v[y].name);
	return c ? c : (x > y) - (x < y);
}
static int ld_section_alignment_stable_cmp(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
	uint32_t ax = isecs.v[x].align, ay = isecs.v[y].align;
	if (ax != ay)
		return ax > ay ? -1 : 1;
	return (x > y) - (x < y);
}

static VVEC(uint32_t) orphans;

static void ld_section_commit(ld_output_section *os, ld_input_section *is)
{
	if (os->type != is->type) {
		if (!os->has_input)	/* (lld's .eh_frame is PROGBITS, x86-64's input too) */
			os->type = is->type == 0x70000001 && !opt.relocatable ? SHT_PROGBITS : is->type;
		else if (os->type != SHT_NOBITS || is->type != SHT_NOBITS)
			os->type = SHT_PROGBITS;
	}
	if (!os->has_input) {
		os->has_input = true;
		os->entsize = is->entsize;
		os->flags = is->flags;
	} else {
		if (os->entsize != is->entsize)
			os->entsize = 0;
		os->flags |= is->flags;
	}
	if (os->nonalloc)
		os->flags &= ~(uint64_t)SHF_ALLOC;
	if (is->align > os->addralign)
		os->addralign = is->align;
}

/* lld's getSectionRank bits that order allocated sections: read-only, code,
 * then writable with RELRO (TLS first) before the rest and bss last; -1 for
 * non-allocated.  Orphans go next to the section whose rank shares the most
 * leading bits (lld getRankProximity) */
enum { LD_ORPHAN_RANK_WRITE = 1 << 14, LD_ORPHAN_RANK_EXECUTE = 1 << 12, LD_ORPHAN_RANK_READ_ONLY_DATA = 1 << 11, LD_ORPHAN_RANK_NOT_RELRO = 1 << 9, LD_ORPHAN_RANK_NOT_TLS = 1 << 8, LD_ORPHAN_RANK_BSS = 1 << 7 };
static int ld_orphan_section_rank(uint64_t flags, uint32_t type, const char *name)
{
	int r;

	if (!(flags & SHF_ALLOC))
		return -1;
	r = (flags & SHF_WRITE) ? LD_ORPHAN_RANK_WRITE : (flags & SHF_EXECINSTR) ? LD_ORPHAN_RANK_EXECUTE : LD_ORPHAN_RANK_READ_ONLY_DATA;
	bool relro = (flags & SHF_WRITE) &&
		     ((flags & SHF_TLS) || type == SHT_INIT_ARRAY || type == SHT_FINI_ARRAY || type == SHT_PREINIT_ARRAY ||
		      !strcmp(name, ".got") || !strcmp(name, ".data.rel.ro") || !strcmp(name, ".bss.rel.ro") ||
		      !strcmp(name, ".ctors") || !strcmp(name, ".dtors") || !strcmp(name, ".jcr") || !strcmp(name, ".eh_frame"));
	if (!relro)
		r |= LD_ORPHAN_RANK_NOT_RELRO;
	if (!(flags & SHF_TLS))
		r |= LD_ORPHAN_RANK_NOT_TLS;
	if (type == SHT_NOBITS)
		r |= LD_ORPHAN_RANK_BSS;
	return r;
}

/* orphans sort by rank; .eh_frame_hdr and .eh_frame are lld synthetics, after
 * the input orphans of their rank, the header first */
static uint64_t ld_orphan_section_key(const ld_input_section *is)
{
	unsigned r = (unsigned)ld_orphan_section_rank(is->flags, is->type, is->name);
	unsigned syn = !strcmp(is->name, ".eh_frame_hdr") ? 1 : !strcmp(is->name, ".eh_frame") ? 2 : 0;

	return (uint64_t)r << 2 | syn;
}

static int ld_rank_proximity(int a, int b) { return a == b ? 32 : __builtin_clz((unsigned)(a ^ b)); }

/* the rank of a script output section, from its first input section */
static int ld_output_section_rank(uint32_t oi)
{
	ld_output_section *os = &osecs.v[oi];
	for (uint32_t k = 0; k < os->cmds.n; k++)
		if (os->cmds.v[k].kind == LD_COMMAND_INPUT_SECTION_DESCRIPTION && isds.v[os->cmds.v[k].idx].secs.n) {
			ld_input_section *is = &isecs.v[isds.v[os->cmds.v[k].idx].secs.v[0]];
			return ld_orphan_section_rank(is->flags, is->type, is->name);
		}
	return -1;
}

static void ld_name_cache_init(void)
{
	if (ncache)
		return;
	uint32_t ncap = 1024;
	while (ncap < isecs.n / 4)
		ncap *= 2;
	ncache = ld_region_alloc(ncap * sizeof *ncache);
	memset(ncache, 0, ncap * sizeof *ncache);
	ncmask = ncap - 1;
	ncount = 0;
}

/* __start_NAME / __stop_NAME with NAME a C identifier: the NAME, else NULL */
static const char *ld_start_stop_section_name(const char *n)
{
	const char *s = !strncmp(n, "__start_", 8) ? n + 8 : !strncmp(n, "__stop_", 7) ? n + 7 : NULL;

	if (!s || !*s || isdigit((unsigned char)*s))
		return NULL;
	for (const char *p = s; *p; p++)
		if (!isalnum((unsigned char)*p) && *p != '_')
			return NULL;
	return s;
}
