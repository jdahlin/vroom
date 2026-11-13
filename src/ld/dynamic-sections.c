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

static bool ld_global_symbol_is_referenced(uint32_t g);
static uint64_t script_ord;

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

static uint32_t ld_dynamic_string_add(const char *str)
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
static void ld_symbol_versions_assign(void)
{
	for (uint32_t i = 0; i < gorder.n; i++) {
		ld_global_symbol *s = &gsyms[gorder.v[i]];
		s->ver = 1;
		if (!vernodes.n || s->state == LD_SYMBOL_STATE_UNDEFINED)
			continue;
		const char *n = ld_global_name(gorder.v[i]);
		for (uint32_t k = 0; k < ver_locals.n; k++)
			if (ld_glob_match(ver_locals.v[k], n)) {
				s->ver = 0;
				break;
			}
		uint16_t named = 2;
		for (uint32_t v = 0; v < vernodes.n; v++) {
			ld_version_node *vn = &vernodes.v[v];
			bool hit = false;
			for (uint32_t k = 0; k < vn->globs.n && !hit; k++)
				hit = ld_glob_match(vn->globs.v[k], n);
			if (hit)
				s->ver = *vn->name ? named : 1;
			if (*vn->name)
				named++;
		}
	}
}

static bool ld_input_section_is_live(uint32_t isec)
{
	uint32_t o = isec == LD_NONE ? LD_NONE : isecs.v[isec].osec;
	return o != LD_NONE && o != LD_OUTPUT_SECTION_DISCARD;
}

/* _DYNAMIC: lld defines it (weak, hidden) after the script's symbols */
static void ld_dynamic_symbol_define(void)
{
	ctx->output.dynamic_live = opt.shared && ld_input_section_is_live(ctx->output.dynamic_section);
	if (!ctx->output.dynamic_live)
		return;
	uint32_t g = ld_global_name_intern("_DYNAMIC");
	ld_global_symbol *s = &gsyms[g];
	if (!ld_global_symbol_is_referenced(g)) {
		VVEC_PUSH(gorder, g, ld_region_alloc);
		ld_atomic_u64_min(&s->first, script_ord++);
	}
	s->state = LD_SYMBOL_STATE_SCRIPT;
	s->ev = (ld_expression_value){.kind = LD_VALUE_KIND_INPUT_SECTION, .idx = ctx->output.dynamic_section, .align = 1};
	s->bind = STB_WEAK;
	s->type = STT_NOTYPE;
	s->vis = STV_HIDDEN;
	s->thunk = LD_NONE;
	s->ver = 1;
}

/* contents and sizes of the dynamic sections (addresses are filled in later) */
static void ld_dynamic_sections_build(void)
{
	if (!ctx->output.dynamic_live)
		return;
	if (ctx->output.relative_count)
		ld_error_report("dynamic relocations in a shared object with .dynamic are not supported");
	ctx->dynamic.symbols.n = 0;
	for (uint32_t i = 0; i < gorder.n; i++) {
		uint32_t g = gorder.v[i];
		ld_global_symbol *s = &gsyms[g];
		if (!ld_global_symbol_is_in_symbol_table(g) || ld_global_symbol_is_local(g))
			continue;
		if (s->state == LD_SYMBOL_STATE_UNDEFINED && (ld_key_load(&s->first) >> 32) >= objs.n)
			continue;
		VVEC_PUSH(ctx->dynamic.symbols, g, ld_region_alloc);
	}
	/* .gnu.hash: undefined symbols first, then stable by bucket */
	uint32_t n = ctx->dynamic.symbols.n, nund = 0;
	uint32_t *tmp = ld_region_alloc((size_t)(n + 1) * sizeof(uint32_t));
	for (uint32_t i = 0; i < n; i++)
		if (gsyms[ctx->dynamic.symbols.v[i]].state == LD_SYMBOL_STATE_UNDEFINED || gsyms[ctx->dynamic.symbols.v[i]].demoted)
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
			ld_global_symbol *s = &gsyms[ctx->dynamic.symbols.v[i]];
			if (s->state == LD_SYMBOL_STATE_UNDEFINED || s->demoted)
				continue;
			if (ld_symbol_name_gnu_hash(ld_global_name(ctx->dynamic.symbols.v[i])) % ctx->dynamic.gnu_hash_buckets == b)
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
		gsyms[ctx->dynamic.symbols.v[i]].dynidx = i + 1;
		ctx->dynamic.symbol_hashes[i] = ld_symbol_name_gnu_hash(ld_global_name(ctx->dynamic.symbols.v[i]));
		ctx->dynamic.symbol_names[i] = ld_dynamic_string_add(ld_global_name(ctx->dynamic.symbols.v[i]));
	}
	const char *fname = opt.soname ? opt.soname : opt.out;
	ctx->dynamic.version_name_offsets = ld_region_alloc((nverdefs + 1) * sizeof(uint32_t));
	if (nverdefs) {
		ctx->dynamic.soname_offset = ld_dynamic_string_add(fname);
		for (uint32_t v = 0, k = 0; v < vernodes.n; v++)
			if (*vernodes.v[v].name)
				ctx->dynamic.version_name_offsets[k++] = ld_dynamic_string_add(vernodes.v[v].name);
	}
	if (opt.soname)
		ctx->dynamic.soname_offset = ld_dynamic_string_add(opt.soname);
	ctx->dynamic.entry_count = (opt.soname ? 1 : 0) + (opt.bsymbolic ? 1 : 0) + 4 + 2 + (nverdefs ? 3 : 0) + 1;

	isecs.v[ctx->output.dynsym_section].size = (uint64_t)(n + 1) * sizeof(elf_symbol);
	isecs.v[ctx->output.dynstr_section].size = ctx->dynamic.strings.n;
	isecs.v[ctx->output.hash_section].size = (uint64_t)(2 + 2 * (n + 1)) * 4;
	isecs.v[ctx->output.gnu_hash_section].size = 16 + (uint64_t)ctx->dynamic.gnu_hash_mask_words * 8 + (uint64_t)ctx->dynamic.gnu_hash_buckets * 4 + (uint64_t)nh * 4;
	if (ctx->output.versym_section != LD_NONE) {
		isecs.v[ctx->output.versym_section].size = (uint64_t)(n + 1) * 2;
		isecs.v[ctx->output.verdef_section].size = (uint64_t)(nverdefs + 1) * 28;
	}
	isecs.v[ctx->output.dynamic_section].size = (uint64_t)ctx->dynamic.entry_count * 16;
}

/* sh_link / sh_info of the dynamic sections */
static void ld_dynamic_section_links_set(void)
{
	if (!ctx->output.dynamic_live)
		return;
	uint32_t ds = osecs.v[ld_input_section_output_section(ctx->output.dynsym_section)].shidx, dstr = osecs.v[ld_input_section_output_section(ctx->output.dynstr_section)].shidx;
	osecs.v[ld_input_section_output_section(ctx->output.dynsym_section)].link = dstr;
	osecs.v[ld_input_section_output_section(ctx->output.dynsym_section)].info = 1;	/* no local dynamic symbols */
	osecs.v[ld_input_section_output_section(ctx->output.hash_section)].link = ds;
	osecs.v[ld_input_section_output_section(ctx->output.gnu_hash_section)].link = ds;
	osecs.v[ld_input_section_output_section(ctx->output.dynamic_section)].link = dstr;
	if (ctx->output.versym_section != LD_NONE) {
		osecs.v[ld_input_section_output_section(ctx->output.versym_section)].link = ds;
		osecs.v[ld_input_section_output_section(ctx->output.verdef_section)].link = dstr;
		osecs.v[ld_input_section_output_section(ctx->output.verdef_section)].info = nverdefs + 1;
	}
}

static uint8_t *ld_input_section_image_pointer(uint32_t isec)
{
	return ctx->output.image + osecs.v[ld_input_section_output_section(isec)].offset + isecs.v[isec].off;
}

static void ld_dynamic_sections_write(void)
{
	if (!ctx->output.dynamic_live)
		return;
	uint32_t n = ctx->dynamic.symbols.n;
	elf_symbol *ds = (elf_symbol *)(void *)ld_input_section_image_pointer(ctx->output.dynsym_section);
	for (uint32_t i = 0; i < n; i++) {
		uint32_t g = ctx->dynamic.symbols.v[i];
		ld_global_symbol *s = &gsyms[g];
		elf_symbol *d = &ds[i + 1];
		d->st_name = ctx->dynamic.symbol_names[i];
		d->st_info = (uint8_t)(s->bind << 4 | s->type);
		d->st_other = s->vis;
		if (s->state == LD_SYMBOL_STATE_SCRIPT) {
			d->st_shndx = s->ev.kind == LD_VALUE_KIND_ABSOLUTE ? SHN_ABS : s->ev.kind == LD_VALUE_KIND_OUTPUT_SECTION ? ld_output_section_header_index(s->ev.idx)
										: ld_symbol_section_index_of_input_section(s->ev.idx);
			d->st_value = ld_global_symbol_address(g);
		} else if (s->state == LD_SYMBOL_STATE_OBJECT && !s->demoted) {
			d->st_shndx = s->sec == LD_NONE ? SHN_ABS : ld_symbol_section_index_of_input_section(s->sec);
			d->st_value = ld_global_symbol_address(g);
			d->st_size = s->size;
		}
	}
	memcpy(ld_input_section_image_pointer(ctx->output.dynstr_section), ctx->dynamic.strings.v, ctx->dynamic.strings.n);
	/* .hash: nbucket = nchain = symbol count */
	uint8_t *h = ld_input_section_image_pointer(ctx->output.hash_section);
	uint32_t nb = n + 1, *bk = (uint32_t *)(void *)(h + 8), *ch = bk + nb;
	ld_u32_store(h, nb);
	ld_u32_store(h + 4, nb);
	for (uint32_t i = 0; i < n; i++) {
		uint32_t b = ld_symbol_name_elf_hash(ld_global_name(ctx->dynamic.symbols.v[i])) % nb;
		ch[i + 1] = bk[b];
		bk[b] = i + 1;
	}
	/* .gnu.hash */
	uint8_t *gh = ld_input_section_image_pointer(ctx->output.gnu_hash_section);
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
		uint8_t *vs = ld_input_section_image_pointer(ctx->output.versym_section);
		for (uint32_t i = 0; i < n; i++) {
			uint16_t v = gsyms[ctx->dynamic.symbols.v[i]].ver;
			memcpy(vs + 2 * (i + 1), &v, 2);
		}
		uint8_t *vd = ld_input_section_image_pointer(ctx->output.verdef_section);
		const char *fname = opt.soname ? opt.soname : opt.out;
		for (uint32_t k = 0; k <= nverdefs; k++) {
			uint8_t *e = vd + 28 * k;
			const char *nm = fname;
			if (k)
				for (uint32_t v = 0, c = 0; v < vernodes.n; v++)
					if (*vernodes.v[v].name && ++c == k)
						nm = vernodes.v[v].name;
			uint16_t ver = 1, flags = k ? 0 : 1, ndx = (uint16_t)(k + 1), cnt = 1;
			memcpy(e, &ver, 2);
			memcpy(e + 2, &flags, 2);
			memcpy(e + 4, &ndx, 2);
			memcpy(e + 6, &cnt, 2);
			ld_u32_store(e + 8, ld_symbol_name_elf_hash(nm));
			ld_u32_store(e + 12, 20);
			ld_u32_store(e + 16, k == nverdefs ? 0 : 28);
			ld_u32_store(e + 20, k ? ctx->dynamic.version_name_offsets[k - 1] : ctx->dynamic.soname_offset);
			ld_u32_store(e + 24, 0);
		}
	}
	/* .dynamic (lld DynamicSection::computeContents order) */
	uint8_t *dy = ld_input_section_image_pointer(ctx->output.dynamic_section);
	uint32_t at = 0;
#define LD_DYNAMIC_ENTRY_APPEND(tag, v) do { ld_u64_store(dy + 16 * at, (uint64_t)(tag)); ld_u64_store(dy + 16 * at + 8, (uint64_t)(v)); at++; } while (0)
	if (opt.soname)
		LD_DYNAMIC_ENTRY_APPEND(DT_SONAME, ctx->dynamic.soname_offset);
	if (opt.bsymbolic)
		LD_DYNAMIC_ENTRY_APPEND(DT_FLAGS, DF_SYMBOLIC);
	LD_DYNAMIC_ENTRY_APPEND(DT_SYMTAB, ld_input_section_address(ctx->output.dynsym_section, 0));
	LD_DYNAMIC_ENTRY_APPEND(DT_SYMENT, sizeof(elf_symbol));
	LD_DYNAMIC_ENTRY_APPEND(DT_STRTAB, ld_input_section_address(ctx->output.dynstr_section, 0));
	LD_DYNAMIC_ENTRY_APPEND(DT_STRSZ, ctx->dynamic.strings.n);
	LD_DYNAMIC_ENTRY_APPEND(DT_GNU_HASH, ld_input_section_address(ctx->output.gnu_hash_section, 0));
	LD_DYNAMIC_ENTRY_APPEND(DT_HASH, ld_input_section_address(ctx->output.hash_section, 0));
	if (ctx->output.versym_section != LD_NONE) {
		LD_DYNAMIC_ENTRY_APPEND(DT_VERSYM, ld_input_section_address(ctx->output.versym_section, 0));
		LD_DYNAMIC_ENTRY_APPEND(DT_VERDEF, ld_input_section_address(ctx->output.verdef_section, 0));
		LD_DYNAMIC_ENTRY_APPEND(DT_VERDEFNUM, nverdefs + 1);
	}
	LD_DYNAMIC_ENTRY_APPEND(DT_NULL, 0);
#undef LD_DYNAMIC_ENTRY_APPEND
}
