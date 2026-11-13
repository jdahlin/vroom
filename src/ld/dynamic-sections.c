/* -shared output: symbol versions, .dynsym, .dynstr, hash tables, .dynamic. */

/* ---------------- -shared: versions, dynamic sections, PHDRS ---------------- */

#define SHT_GNU_HASH 0x6ffffff6u
#define SHT_GNU_VERDEF 0x6ffffffdu
#define SHT_GNU_VERSYM 0x6fffffffu
enum {
	DT_NULL = 0, DT_HASH = 4, DT_STRTAB = 5, DT_SYMTAB = 6, DT_STRSZ = 10, DT_SYMENT = 11,
	DT_SONAME = 14, DT_FLAGS = 30, DF_SYMBOLIC = 2,
};
#define DT_GNU_HASH 0x6ffffef5u
#define DT_VERSYM 0x6ffffff0u
#define DT_VERDEF 0x6ffffffcu
#define DT_VERDEFNUM 0x6ffffffdu

static bool ld_global_symbol_is_referenced(struct ld_context *ctx, uint32_t g);

static uint32_t ld_symbol_name_elf_hash(const char *n)
{
	uint32_t h = 0;
	for (; *n; n++) {
		h = (h << 4) + (uint8_t)*n;
		uint32_t g = h & 0xf0000000u;
		if (g)
			h ^= g >> 24;
		h &= ~g;
	}
	return h;
}
static uint32_t ld_symbol_name_gnu_hash(const char *n)
{
	uint32_t h = 5381;
	for (; *n; n++)
		h = h * 33 + (uint8_t)*n;
	return h;
}

static uint32_t ld_dynamic_string_add(struct ld_context *ctx, const char *str)
{
	size_t n = strlen(str);
	for (uint32_t i = 0; i < ctx->dynamic.string_offsets.n; i++)
		if (!strcmp(ctx->dynamic.strings.v + ctx->dynamic.string_offsets.v[i], str))
			return ctx->dynamic.string_offsets.v[i];
	uint32_t o = ctx->dynamic.strings.n;
	for (size_t i = 0; i <= n; i++)
		VVEC_PUSH(ctx->dynamic.strings, str[i], ld_region_alloc);
	VVEC_PUSH(ctx->dynamic.string_offsets, o, ld_region_alloc);
	return o;
}

/* VERSION: local patterns first, then each node's globals override (lld scanVersionScript) */
static void ld_symbol_versions_assign(struct ld_context *ctx)
{
	for (uint32_t i = 0; i < ctx->symbols.order.n; i++) {
		ld_global_symbol *s = &ctx->symbols.globals[ctx->symbols.order.v[i]];
		s->version = 1;
		if (!ctx->script.version_nodes.n || s->state == LD_SYMBOL_STATE_UNDEFINED)
			continue;
		const char *n = ld_global_name(ctx, ctx->symbols.order.v[i]);
		for (uint32_t k = 0; k < ctx->script.version_locals.n; k++)
			if (ld_glob_match(ctx->script.version_locals.v[k], n)) {
				s->version = 0;
				break;
			}
		uint16_t named = 2;
		for (uint32_t v = 0; v < ctx->script.version_nodes.n; v++) {
			ld_version_node *vn = &ctx->script.version_nodes.v[v];
			bool hit = false;
			for (uint32_t k = 0; k < vn->globs.n && !hit; k++)
				hit = ld_glob_match(vn->globs.v[k], n);
			if (hit)
				s->version = *vn->name ? named : 1;
			if (*vn->name)
				named++;
		}
	}
}

static bool ld_input_section_is_live(struct ld_context *ctx, uint32_t isec)
{
	uint32_t o = isec == LD_NONE ? LD_NONE : ctx->inputs.sections.v[isec].output_section;
	return o != LD_NONE && o != LD_OUTPUT_SECTION_DISCARD;
}

/* _DYNAMIC: lld defines it (weak, hidden) after the script's symbols */
static void ld_dynamic_symbol_define(struct ld_context *ctx)
{
	ctx->output.dynamic_live = ctx->options.shared && ld_input_section_is_live(ctx, ctx->output.dynamic_section);
	if (!ctx->output.dynamic_live)
		return;
	uint32_t g = ld_global_name_intern(ctx, "_DYNAMIC");
	ld_global_symbol *s = &ctx->symbols.globals[g];
	if (!ld_global_symbol_is_referenced(ctx, g)) {
		VVEC_PUSH(ctx->symbols.order, g, ld_region_alloc);
		ld_atomic_u64_min(&s->first_occurrence, ctx->symbols.script_order++);
	}
	s->state = LD_SYMBOL_STATE_SCRIPT;
	s->expression_value = (ld_expression_value){.kind = LD_VALUE_KIND_INPUT_SECTION, .index = ctx->output.dynamic_section, .alignment = 1};
	s->binding = STB_WEAK;
	s->type = STT_NOTYPE;
	s->visibility = STV_HIDDEN;
	s->thunk = LD_NONE;
	s->version = 1;
}

/* contents and sizes of the dynamic sections (addresses are filled in later) */
static void ld_dynamic_sections_build(struct ld_context *ctx)
{
	if (!ctx->output.dynamic_live)
		return;
	if (ctx->output.relative_count)
		ld_error_report(ctx, "dynamic relocations in a shared object with .dynamic are not supported");
	ctx->dynamic.symbols.n = 0;
	for (uint32_t i = 0; i < ctx->symbols.order.n; i++) {
		uint32_t g = ctx->symbols.order.v[i];
		ld_global_symbol *s = &ctx->symbols.globals[g];
		if (!ld_global_symbol_is_in_symbol_table(ctx, g) || ld_global_symbol_is_local(ctx, g))
			continue;
		if (s->state == LD_SYMBOL_STATE_UNDEFINED && (ld_key_load(&s->first_occurrence) >> 32) >= ctx->inputs.objects.n)
			continue;
		VVEC_PUSH(ctx->dynamic.symbols, g, ld_region_alloc);
	}
	/* .gnu.hash: undefined symbols first, then stable by bucket */
	uint32_t n = ctx->dynamic.symbols.n, nund = 0;
	uint32_t *tmp = ld_region_alloc((size_t)(n + 1) * sizeof(uint32_t));
	for (uint32_t i = 0; i < n; i++)
		if (ctx->symbols.globals[ctx->dynamic.symbols.v[i]].state == LD_SYMBOL_STATE_UNDEFINED || ctx->symbols.globals[ctx->dynamic.symbols.v[i]].demoted)
			tmp[nund++] = ctx->dynamic.symbols.v[i];
	uint32_t nh = n - nund;
	ctx->dynamic.gnu_hash_buckets = nh / 4 > 1 ? nh / 4 : 1;
	uint64_t words = (uint64_t)nh * 12 / 64;
	ctx->dynamic.gnu_hash_mask_words = 1;
	while (ctx->dynamic.gnu_hash_mask_words <= words)	/* llvm::NextPowerOf2: strictly greater */
		ctx->dynamic.gnu_hash_mask_words <<= 1;
	ctx->dynamic.symbol_hashes = ld_region_alloc((size_t)(n + 1) * sizeof(uint32_t));
	uint32_t w = nund;
	for (uint32_t b = 0; b < ctx->dynamic.gnu_hash_buckets; b++)
		for (uint32_t i = 0; i < n; i++) {
			ld_global_symbol *s = &ctx->symbols.globals[ctx->dynamic.symbols.v[i]];
			if (s->state == LD_SYMBOL_STATE_UNDEFINED || s->demoted)
				continue;
			if (ld_symbol_name_gnu_hash(ld_global_name(ctx, ctx->dynamic.symbols.v[i])) % ctx->dynamic.gnu_hash_buckets == b)
				tmp[w++] = ctx->dynamic.symbols.v[i];
		}
	if (n)
		memcpy(ctx->dynamic.symbols.v, tmp, n * sizeof(uint32_t));
	ctx->dynamic.first_hashed_symbol = nund + 1;
	/* .dynstr: symbol names, then the file's name and version names */
	ctx->dynamic.strings.n = ctx->dynamic.string_offsets.n = 0;
	VVEC_PUSH(ctx->dynamic.strings, 0, ld_region_alloc);
	ctx->dynamic.symbol_names = ld_region_alloc((size_t)(n + 1) * sizeof(uint32_t));
	for (uint32_t i = 0; i < n; i++) {
		ctx->symbols.globals[ctx->dynamic.symbols.v[i]].dynamic_index = i + 1;
		ctx->dynamic.symbol_hashes[i] = ld_symbol_name_gnu_hash(ld_global_name(ctx, ctx->dynamic.symbols.v[i]));
		ctx->dynamic.symbol_names[i] = ld_dynamic_string_add(ctx, ld_global_name(ctx, ctx->dynamic.symbols.v[i]));
	}
	const char *fname = ctx->options.soname ? ctx->options.soname : ctx->options.output_path;
	ctx->dynamic.version_name_offsets = ld_region_alloc((ctx->script.version_definition_count + 1) * sizeof(uint32_t));
	if (ctx->script.version_definition_count) {
		ctx->dynamic.soname_offset = ld_dynamic_string_add(ctx, fname);
		for (uint32_t v = 0, k = 0; v < ctx->script.version_nodes.n; v++)
			if (*ctx->script.version_nodes.v[v].name)
				ctx->dynamic.version_name_offsets[k++] = ld_dynamic_string_add(ctx, ctx->script.version_nodes.v[v].name);
	}
	if (ctx->options.soname)
		ctx->dynamic.soname_offset = ld_dynamic_string_add(ctx, ctx->options.soname);
	ctx->dynamic.entry_count = (ctx->options.soname ? 1 : 0) + (ctx->options.bsymbolic ? 1 : 0) + 4 + 2 + (ctx->script.version_definition_count ? 3 : 0) + 1;

	ctx->inputs.sections.v[ctx->output.dynsym_section].size = (uint64_t)(n + 1) * sizeof(elf_symbol);
	ctx->inputs.sections.v[ctx->output.dynstr_section].size = ctx->dynamic.strings.n;
	ctx->inputs.sections.v[ctx->output.hash_section].size = (uint64_t)(2 + 2 * (n + 1)) * 4;
	ctx->inputs.sections.v[ctx->output.gnu_hash_section].size = 16 + (uint64_t)ctx->dynamic.gnu_hash_mask_words * 8 + (uint64_t)ctx->dynamic.gnu_hash_buckets * 4 + (uint64_t)nh * 4;
	if (ctx->output.versym_section != LD_NONE) {
		ctx->inputs.sections.v[ctx->output.versym_section].size = (uint64_t)(n + 1) * 2;
		ctx->inputs.sections.v[ctx->output.verdef_section].size = (uint64_t)(ctx->script.version_definition_count + 1) * 28;
	}
	ctx->inputs.sections.v[ctx->output.dynamic_section].size = (uint64_t)ctx->dynamic.entry_count * 16;
}

/* sh_link / sh_info of the dynamic sections */
static void ld_dynamic_section_links_set(struct ld_context *ctx)
{
	if (!ctx->output.dynamic_live)
		return;
	uint32_t ds = ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.dynsym_section)].section_header_index, dstr = ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.dynstr_section)].section_header_index;
	ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.dynsym_section)].link = dstr;
	ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.dynsym_section)].info = 1;	/* no local dynamic symbols */
	ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.hash_section)].link = ds;
	ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.gnu_hash_section)].link = ds;
	ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.dynamic_section)].link = dstr;
	if (ctx->output.versym_section != LD_NONE) {
		ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.versym_section)].link = ds;
		ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.verdef_section)].link = dstr;
		ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.verdef_section)].info = ctx->script.version_definition_count + 1;
	}
}

static uint8_t *ld_input_section_image_pointer(struct ld_context *ctx, uint32_t isec)
{
	return ctx->output.image + ctx->script.sections.v[ld_input_section_output_section(ctx, isec)].offset + ctx->inputs.sections.v[isec].offset;
}

static void ld_dynamic_sections_write(struct ld_context *ctx)
{
	if (!ctx->output.dynamic_live)
		return;
	uint32_t n = ctx->dynamic.symbols.n;
	elf_symbol *ds = (elf_symbol *)(void *)ld_input_section_image_pointer(ctx, ctx->output.dynsym_section);
	for (uint32_t i = 0; i < n; i++) {
		uint32_t g = ctx->dynamic.symbols.v[i];
		ld_global_symbol *s = &ctx->symbols.globals[g];
		elf_symbol *d = &ds[i + 1];
		d->st_name = ctx->dynamic.symbol_names[i];
		d->st_info = (uint8_t)(s->binding << 4 | s->type);
		d->st_other = s->visibility;
		if (s->state == LD_SYMBOL_STATE_SCRIPT) {
			d->st_shndx = s->expression_value.kind == LD_VALUE_KIND_ABSOLUTE ? SHN_ABS : s->expression_value.kind == LD_VALUE_KIND_OUTPUT_SECTION ? ld_output_section_header_index(ctx, s->expression_value.index)
										: ld_symbol_section_index_of_input_section(ctx, s->expression_value.index);
			d->st_value = ld_global_symbol_address(ctx, g);
		} else if (s->state == LD_SYMBOL_STATE_OBJECT && !s->demoted) {
			d->st_shndx = s->input_section == LD_NONE ? SHN_ABS : ld_symbol_section_index_of_input_section(ctx, s->input_section);
			d->st_value = ld_global_symbol_address(ctx, g);
			d->st_size = s->size;
		}
	}
	memcpy(ld_input_section_image_pointer(ctx, ctx->output.dynstr_section), ctx->dynamic.strings.v, ctx->dynamic.strings.n);
	/* .hash: nbucket = nchain = symbol count */
	uint8_t *h = ld_input_section_image_pointer(ctx, ctx->output.hash_section);
	uint32_t nb = n + 1, *bk = (uint32_t *)(void *)(h + 8), *ch = bk + nb;
	ld_u32_store(h, nb);
	ld_u32_store(h + 4, nb);
	for (uint32_t i = 0; i < n; i++) {
		uint32_t b = ld_symbol_name_elf_hash(ld_global_name(ctx, ctx->dynamic.symbols.v[i])) % nb;
		ch[i + 1] = bk[b];
		bk[b] = i + 1;
	}
	/* .gnu.hash */
	uint8_t *gh = ld_input_section_image_pointer(ctx, ctx->output.gnu_hash_section);
	ld_u32_store(gh, ctx->dynamic.gnu_hash_buckets);
	ld_u32_store(gh + 4, ctx->dynamic.first_hashed_symbol);
	ld_u32_store(gh + 8, ctx->dynamic.gnu_hash_mask_words);
	ld_u32_store(gh + 12, 26);
	uint64_t *bloom = (uint64_t *)(void *)(gh + 16);
	uint32_t *gb = (uint32_t *)(void *)(gh + 16 + ctx->dynamic.gnu_hash_mask_words * 8), *gv = gb + ctx->dynamic.gnu_hash_buckets;
	uint32_t old = LD_NONE;
	for (uint32_t i = ctx->dynamic.first_hashed_symbol - 1; i < n; i++) {
		uint32_t hv = ctx->dynamic.symbol_hashes[i], b = hv % ctx->dynamic.gnu_hash_buckets;
		bloom[(hv / 64) & (ctx->dynamic.gnu_hash_mask_words - 1)] |= (uint64_t)1 << (hv % 64) | (uint64_t)1 << ((hv >> 26) % 64);
		bool last = i + 1 == n || ctx->dynamic.symbol_hashes[i + 1] % ctx->dynamic.gnu_hash_buckets != b;
		*gv++ = last ? hv | 1 : hv & ~1u;
		if (b != old) {
			gb[b] = i + 1;
			old = b;
		}
	}
	/* .gnu.version, .gnu.version_d */
	if (ctx->output.versym_section != LD_NONE) {
		uint8_t *vs = ld_input_section_image_pointer(ctx, ctx->output.versym_section);
		for (uint32_t i = 0; i < n; i++) {
			uint16_t v = ctx->symbols.globals[ctx->dynamic.symbols.v[i]].version;
			memcpy(vs + 2 * (i + 1), &v, 2);
		}
		uint8_t *vd = ld_input_section_image_pointer(ctx, ctx->output.verdef_section);
		const char *fname = ctx->options.soname ? ctx->options.soname : ctx->options.output_path;
		for (uint32_t k = 0; k <= ctx->script.version_definition_count; k++) {
			uint8_t *e = vd + 28 * k;
			const char *nm = fname;
			if (k)
				for (uint32_t v = 0, c = 0; v < ctx->script.version_nodes.n; v++)
					if (*ctx->script.version_nodes.v[v].name && ++c == k)
						nm = ctx->script.version_nodes.v[v].name;
			uint16_t ver = 1, flags = k ? 0 : 1, ndx = (uint16_t)(k + 1), cnt = 1;
			memcpy(e, &ver, 2);
			memcpy(e + 2, &flags, 2);
			memcpy(e + 4, &ndx, 2);
			memcpy(e + 6, &cnt, 2);
			ld_u32_store(e + 8, ld_symbol_name_elf_hash(nm));
			ld_u32_store(e + 12, 20);
			ld_u32_store(e + 16, k == ctx->script.version_definition_count ? 0 : 28);
			ld_u32_store(e + 20, k ? ctx->dynamic.version_name_offsets[k - 1] : ctx->dynamic.soname_offset);
			ld_u32_store(e + 24, 0);
		}
	}
	/* .dynamic (lld DynamicSection::computeContents order) */
	uint8_t *dy = ld_input_section_image_pointer(ctx, ctx->output.dynamic_section);
	uint32_t at = 0;
#define LD_DYNAMIC_ENTRY_APPEND(tag, v) do { ld_u64_store(dy + 16 * at, (uint64_t)(tag)); ld_u64_store(dy + 16 * at + 8, (uint64_t)(v)); at++; } while (0)
	if (ctx->options.soname)
		LD_DYNAMIC_ENTRY_APPEND(DT_SONAME, ctx->dynamic.soname_offset);
	if (ctx->options.bsymbolic)
		LD_DYNAMIC_ENTRY_APPEND(DT_FLAGS, DF_SYMBOLIC);
	LD_DYNAMIC_ENTRY_APPEND(DT_SYMTAB, ld_input_section_address(ctx, ctx->output.dynsym_section, 0));
	LD_DYNAMIC_ENTRY_APPEND(DT_SYMENT, sizeof(elf_symbol));
	LD_DYNAMIC_ENTRY_APPEND(DT_STRTAB, ld_input_section_address(ctx, ctx->output.dynstr_section, 0));
	LD_DYNAMIC_ENTRY_APPEND(DT_STRSZ, ctx->dynamic.strings.n);
	LD_DYNAMIC_ENTRY_APPEND(DT_GNU_HASH, ld_input_section_address(ctx, ctx->output.gnu_hash_section, 0));
	LD_DYNAMIC_ENTRY_APPEND(DT_HASH, ld_input_section_address(ctx, ctx->output.hash_section, 0));
	if (ctx->output.versym_section != LD_NONE) {
		LD_DYNAMIC_ENTRY_APPEND(DT_VERSYM, ld_input_section_address(ctx, ctx->output.versym_section, 0));
		LD_DYNAMIC_ENTRY_APPEND(DT_VERDEF, ld_input_section_address(ctx, ctx->output.verdef_section, 0));
		LD_DYNAMIC_ENTRY_APPEND(DT_VERDEFNUM, ctx->script.version_definition_count + 1);
	}
	LD_DYNAMIC_ENTRY_APPEND(DT_NULL, 0);
#undef LD_DYNAMIC_ENTRY_APPEND
}
