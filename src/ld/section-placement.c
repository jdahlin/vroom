/* The header size, the input-section-name cache, committing sections to output sections, and lld's orphan section ranking. */

/* ---------------- layout ---------------- */

static uint64_t ld_headers_size(struct ld_context *ctx)
{
	return sizeof(elf_file_header) + (uint64_t)(ctx->script.has_phdrs ? ctx->script.segments.n : ctx->layout.program_headers.n) * sizeof(elf_program_header);
}

/* ---------------- section assignment ---------------- */

/* the input-section-name cache (ld_layout.name_cache): grown at half full, an
   -ffunction-sections link has about as many names as sections */
static void ld_name_cache_grow(struct ld_context *ctx)
{
	uint32_t ncap = (ctx->layout.name_cache_mask + 1) * 2;
	ld_name_cache_entry *nc = ld_region_alloc(ncap * sizeof *nc);

	memset(nc, 0, ncap * sizeof *nc);
	for (uint32_t i = 0; i <= ctx->layout.name_cache_mask; i++)
		if (ctx->layout.name_cache[i].name) {
			uint32_t j = ctx->layout.name_cache[i].hash & (ncap - 1);

			while (nc[j].name)
				j = (j + 1) & (ncap - 1);
			nc[j] = ctx->layout.name_cache[i];
		}
	ctx->layout.name_cache = nc;
	ctx->layout.name_cache_mask = ncap - 1;
}

static uint32_t ld_input_section_description_match(struct ld_context *ctx, const char *name)
{
	size_t n = strlen(name);
	uint32_t h = ld_name_hash(name, n);
	uint32_t j = h & ctx->layout.name_cache_mask;
	while (ctx->layout.name_cache[j].name) {
		if (ctx->layout.name_cache[j].hash == h && !strcmp(ctx->layout.name_cache[j].name, name))
			return ctx->layout.name_cache[j].input_description;
		j = (j + 1) & ctx->layout.name_cache_mask;
	}
	uint32_t res = LD_NONE;
	for (uint32_t i = 0; i < ctx->script.commands.n && res == LD_NONE; i++) {
		ld_command *c = &ctx->script.commands.v[i];
		if (c->kind != LD_COMMAND_OUTPUT_SECTION)
			continue;
		ld_output_section *os = &ctx->script.sections.v[c->index];
		for (uint32_t k = 0; k < os->commands.n && res == LD_NONE; k++) {
			ld_command *cc = &os->commands.v[k];
			if (cc->kind != LD_COMMAND_INPUT_SECTION_DESCRIPTION)
				continue;
			ld_input_section_description *d = &ctx->script.input_descriptions.v[cc->index];
			for (uint32_t p = 0; p < d->pattern_count; p++)
				if (ld_pattern_match(&d->patterns[p], name)) {
					res = cc->index;
					break;
				}
		}
	}
	ctx->layout.name_cache[j] = (ld_name_cache_entry){.name = name, .hash = h, .input_description = res};
	if (++ctx->layout.name_cache_count * 2 > ctx->layout.name_cache_mask + 1)
		ld_name_cache_grow(ctx);
	return res;
}

/* input section ids sorted by a key taken from the context first, so that the
   comparator needs no context; ties by id, so the order is total (stable) */
typedef struct { uint64_t key; const char *name; uint32_t section; } ld_section_sort_entry;
enum { LD_SECTION_ORDER_NAME, LD_SECTION_ORDER_ALIGNMENT_DESCENDING, LD_SECTION_ORDER_ADDRESS };

static int ld_section_sort_entry_cmp(const void *a, const void *b)
{
	const ld_section_sort_entry *x = a, *y = b;
	if (x->name || y->name) {
		int c = strcmp(x->name, y->name);
		if (c)
			return c;
	}
	if (x->key != y->key)
		return x->key < y->key ? -1 : 1;
	return (x->section > y->section) - (x->section < y->section);
}

static void ld_section_ids_sort(struct ld_context *ctx, uint32_t *ids, uint32_t n, int order)
{
	if (n < 2)
		return;
	ld_section_sort_entry *t = ld_region_alloc((uint64_t)n * sizeof *t);
	for (uint32_t i = 0; i < n; i++) {
		const ld_input_section *is = &ctx->inputs.sections.v[ids[i]];
		t[i] = (ld_section_sort_entry){.section = ids[i]};
		if (order == LD_SECTION_ORDER_NAME)
			t[i].name = is->name;
		else if (order == LD_SECTION_ORDER_ALIGNMENT_DESCENDING)
			t[i].key = UINT32_MAX - is->alignment;
		else
			t[i].key = ld_input_section_address(ctx, ids[i], 0);
	}
	qsort(t, n, sizeof *t, ld_section_sort_entry_cmp);
	for (uint32_t i = 0; i < n; i++)
		ids[i] = t[i].section;
}

static void ld_section_commit(struct ld_context *ctx, ld_output_section *os, ld_input_section *is)
{
	if (os->type != is->type) {
		if (!os->has_input)	/* (lld's .eh_frame is PROGBITS, x86-64's input too) */
			os->type = is->type == 0x70000001 && !ctx->options.relocatable ? SHT_PROGBITS : is->type;
		else if (os->type != SHT_NOBITS || is->type != SHT_NOBITS)
			os->type = SHT_PROGBITS;
	}
	if (!os->has_input) {
		os->has_input = true;
		os->entry_size = is->entry_size;
		os->flags = is->flags;
	} else {
		if (os->entry_size != is->entry_size)
			os->entry_size = 0;
		os->flags |= is->flags;
	}
	if (os->not_allocated)
		os->flags &= ~(uint64_t)SHF_ALLOC;
	if (is->alignment > os->address_alignment)
		os->address_alignment = is->alignment;
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
static int ld_output_section_rank(struct ld_context *ctx, uint32_t oi)
{
	ld_output_section *os = &ctx->script.sections.v[oi];
	for (uint32_t k = 0; k < os->commands.n; k++)
		if (os->commands.v[k].kind == LD_COMMAND_INPUT_SECTION_DESCRIPTION && ctx->script.input_descriptions.v[os->commands.v[k].index].sections.n) {
			ld_input_section *is = &ctx->inputs.sections.v[ctx->script.input_descriptions.v[os->commands.v[k].index].sections.v[0]];
			return ld_orphan_section_rank(is->flags, is->type, is->name);
		}
	return -1;
}

static void ld_name_cache_init(struct ld_context *ctx)
{
	if (ctx->layout.name_cache)
		return;
	uint32_t ncap = 1024;
	while (ncap < ctx->inputs.sections.n / 4)
		ncap *= 2;
	ctx->layout.name_cache = ld_region_alloc(ncap * sizeof *ctx->layout.name_cache);
	memset(ctx->layout.name_cache, 0, ncap * sizeof *ctx->layout.name_cache);
	ctx->layout.name_cache_mask = ncap - 1;
	ctx->layout.name_cache_count = 0;
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
