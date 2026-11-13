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
static VVEC(uint32_t) dsyms;		/* exported globals in .dynsym order (index i + 1) */
static uint32_t dyn_symndx;		/* first symbol in .gnu.hash */
static uint32_t gh_nbuckets, gh_maskwords;
static uint32_t *dsym_hash;		/* GNU hash per dsyms entry */
static VVEC(char) dynstr;
static VVEC(uint32_t) dynstr_offs;	/* offsets of added strings (lld dedups) */
static uint32_t *dsym_name;		/* .dynstr offset per dsyms entry */
static uint32_t soname_off, *vername_off;
static uint32_t ndynent;

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
	for (uint32_t i = 0; i < dynstr_offs.n; i++)
		if (!strcmp(dynstr.v + dynstr_offs.v[i], str))
			return dynstr_offs.v[i];
	uint32_t o = dynstr.n;
	for (size_t i = 0; i <= n; i++)
		VVEC_PUSH(dynstr, str[i], ld_region_alloc);
	VVEC_PUSH(dynstr_offs, o, ld_region_alloc);
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
	dyn_live = opt.shared && ld_input_section_is_live(dynamic_isec);
	if (!dyn_live)
		return;
	uint32_t g = ld_global_name_intern("_DYNAMIC");
	ld_global_symbol *s = &gsyms[g];
	if (!ld_global_symbol_is_referenced(g)) {
		VVEC_PUSH(gorder, g, ld_region_alloc);
		ld_atomic_u64_min(&s->first, script_ord++);
	}
	s->state = LD_SYMBOL_STATE_SCRIPT;
	s->ev = (ld_expression_value){.kind = LD_VALUE_KIND_INPUT_SECTION, .idx = dynamic_isec, .align = 1};
	s->bind = STB_WEAK;
	s->type = STT_NOTYPE;
	s->vis = STV_HIDDEN;
	s->thunk = LD_NONE;
	s->ver = 1;
}

/* contents and sizes of the dynamic sections (addresses are filled in later) */
static void ld_dynamic_sections_build(void)
{
	if (!dyn_live)
		return;
	if (ndyn)
		ld_error_report("dynamic relocations in a shared object with .dynamic are not supported");
	dsyms.n = 0;
	for (uint32_t i = 0; i < gorder.n; i++) {
		uint32_t g = gorder.v[i];
		ld_global_symbol *s = &gsyms[g];
		if (!ld_global_symbol_is_in_symbol_table(g) || ld_global_symbol_is_local(g))
			continue;
		if (s->state == LD_SYMBOL_STATE_UNDEFINED && (ld_key_load(&s->first) >> 32) >= objs.n)
			continue;
		VVEC_PUSH(dsyms, g, ld_region_alloc);
	}
	/* .gnu.hash: undefined symbols first, then stable by bucket */
	uint32_t n = dsyms.n, nund = 0;
	uint32_t *tmp = ld_region_alloc((size_t)(n + 1) * sizeof(uint32_t));
	for (uint32_t i = 0; i < n; i++)
		if (gsyms[dsyms.v[i]].state == LD_SYMBOL_STATE_UNDEFINED || gsyms[dsyms.v[i]].demoted)
			tmp[nund++] = dsyms.v[i];
	uint32_t nh = n - nund;
	gh_nbuckets = nh / 4 > 1 ? nh / 4 : 1;
	uint64_t words = (uint64_t)nh * 12 / 64;
	gh_maskwords = 1;
	while (gh_maskwords <= words)	/* llvm::NextPowerOf2: strictly greater */
		gh_maskwords <<= 1;
	dsym_hash = ld_region_alloc((size_t)(n + 1) * sizeof(uint32_t));
	uint32_t w = nund;
	for (uint32_t b = 0; b < gh_nbuckets; b++)
		for (uint32_t i = 0; i < n; i++) {
			ld_global_symbol *s = &gsyms[dsyms.v[i]];
			if (s->state == LD_SYMBOL_STATE_UNDEFINED || s->demoted)
				continue;
			if (ld_symbol_name_gnu_hash(ld_global_name(dsyms.v[i])) % gh_nbuckets == b)
				tmp[w++] = dsyms.v[i];
		}
	if (n)
		memcpy(dsyms.v, tmp, n * sizeof(uint32_t));
	dyn_symndx = nund + 1;
	/* .dynstr: symbol names, then the file's name and version names */
	dynstr.n = dynstr_offs.n = 0;
	VVEC_PUSH(dynstr, 0, ld_region_alloc);
	dsym_name = ld_region_alloc((size_t)(n + 1) * sizeof(uint32_t));
	for (uint32_t i = 0; i < n; i++) {
		gsyms[dsyms.v[i]].dynidx = i + 1;
		dsym_hash[i] = ld_symbol_name_gnu_hash(ld_global_name(dsyms.v[i]));
		dsym_name[i] = ld_dynamic_string_add(ld_global_name(dsyms.v[i]));
	}
	const char *fname = opt.soname ? opt.soname : opt.out;
	vername_off = ld_region_alloc((nverdefs + 1) * sizeof(uint32_t));
	if (nverdefs) {
		soname_off = ld_dynamic_string_add(fname);
		for (uint32_t v = 0, k = 0; v < vernodes.n; v++)
			if (*vernodes.v[v].name)
				vername_off[k++] = ld_dynamic_string_add(vernodes.v[v].name);
	}
	if (opt.soname)
		soname_off = ld_dynamic_string_add(opt.soname);
	ndynent = (opt.soname ? 1 : 0) + (opt.bsymbolic ? 1 : 0) + 4 + 2 + (nverdefs ? 3 : 0) + 1;

	isecs.v[dynsym_isec].size = (uint64_t)(n + 1) * sizeof(elf_symbol);
	isecs.v[dynstr_isec].size = dynstr.n;
	isecs.v[hash_isec].size = (uint64_t)(2 + 2 * (n + 1)) * 4;
	isecs.v[gnuhash_isec].size = 16 + (uint64_t)gh_maskwords * 8 + (uint64_t)gh_nbuckets * 4 + (uint64_t)nh * 4;
	if (versym_isec != LD_NONE) {
		isecs.v[versym_isec].size = (uint64_t)(n + 1) * 2;
		isecs.v[verdef_isec].size = (uint64_t)(nverdefs + 1) * 28;
	}
	isecs.v[dynamic_isec].size = (uint64_t)ndynent * 16;
}

/* sh_link / sh_info of the dynamic sections */
static void ld_dynamic_section_links_set(void)
{
	if (!dyn_live)
		return;
	uint32_t ds = osecs.v[ld_input_section_output_section(dynsym_isec)].shidx, dstr = osecs.v[ld_input_section_output_section(dynstr_isec)].shidx;
	osecs.v[ld_input_section_output_section(dynsym_isec)].link = dstr;
	osecs.v[ld_input_section_output_section(dynsym_isec)].info = 1;	/* no local dynamic symbols */
	osecs.v[ld_input_section_output_section(hash_isec)].link = ds;
	osecs.v[ld_input_section_output_section(gnuhash_isec)].link = ds;
	osecs.v[ld_input_section_output_section(dynamic_isec)].link = dstr;
	if (versym_isec != LD_NONE) {
		osecs.v[ld_input_section_output_section(versym_isec)].link = ds;
		osecs.v[ld_input_section_output_section(verdef_isec)].link = dstr;
		osecs.v[ld_input_section_output_section(verdef_isec)].info = nverdefs + 1;
	}
}

static uint8_t *ld_input_section_image_pointer(uint32_t isec)
{
	return out + osecs.v[ld_input_section_output_section(isec)].offset + isecs.v[isec].off;
}

static void ld_dynamic_sections_write(void)
{
	if (!dyn_live)
		return;
	uint32_t n = dsyms.n;
	elf_symbol *ds = (elf_symbol *)(void *)ld_input_section_image_pointer(dynsym_isec);
	for (uint32_t i = 0; i < n; i++) {
		uint32_t g = dsyms.v[i];
		ld_global_symbol *s = &gsyms[g];
		elf_symbol *d = &ds[i + 1];
		d->st_name = dsym_name[i];
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
	memcpy(ld_input_section_image_pointer(dynstr_isec), dynstr.v, dynstr.n);
	/* .hash: nbucket = nchain = symbol count */
	uint8_t *h = ld_input_section_image_pointer(hash_isec);
	uint32_t nb = n + 1, *bk = (uint32_t *)(void *)(h + 8), *ch = bk + nb;
	ld_u32_store(h, nb);
	ld_u32_store(h + 4, nb);
	for (uint32_t i = 0; i < n; i++) {
		uint32_t b = ld_symbol_name_elf_hash(ld_global_name(dsyms.v[i])) % nb;
		ch[i + 1] = bk[b];
		bk[b] = i + 1;
	}
	/* .gnu.hash */
	uint8_t *gh = ld_input_section_image_pointer(gnuhash_isec);
	ld_u32_store(gh, gh_nbuckets);
	ld_u32_store(gh + 4, dyn_symndx);
	ld_u32_store(gh + 8, gh_maskwords);
	ld_u32_store(gh + 12, 26);
	uint64_t *bloom = (uint64_t *)(void *)(gh + 16);
	uint32_t *gb = (uint32_t *)(void *)(gh + 16 + gh_maskwords * 8), *gv = gb + gh_nbuckets;
	uint32_t old = LD_NONE;
	for (uint32_t i = dyn_symndx - 1; i < n; i++) {
		uint32_t hv = dsym_hash[i], b = hv % gh_nbuckets;
		bloom[(hv / 64) & (gh_maskwords - 1)] |= (uint64_t)1 << (hv % 64) | (uint64_t)1 << ((hv >> 26) % 64);
		bool last = i + 1 == n || dsym_hash[i + 1] % gh_nbuckets != b;
		*gv++ = last ? hv | 1 : hv & ~1u;
		if (b != old) {
			gb[b] = i + 1;
			old = b;
		}
	}
	/* .gnu.version, .gnu.version_d */
	if (versym_isec != LD_NONE) {
		uint8_t *vs = ld_input_section_image_pointer(versym_isec);
		for (uint32_t i = 0; i < n; i++) {
			uint16_t v = gsyms[dsyms.v[i]].ver;
			memcpy(vs + 2 * (i + 1), &v, 2);
		}
		uint8_t *vd = ld_input_section_image_pointer(verdef_isec);
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
			ld_u32_store(e + 20, k ? vername_off[k - 1] : soname_off);
			ld_u32_store(e + 24, 0);
		}
	}
	/* .dynamic (lld DynamicSection::computeContents order) */
	uint8_t *dy = ld_input_section_image_pointer(dynamic_isec);
	uint32_t at = 0;
#define LD_DYNAMIC_ENTRY_APPEND(tag, v) do { ld_u64_store(dy + 16 * at, (uint64_t)(tag)); ld_u64_store(dy + 16 * at + 8, (uint64_t)(v)); at++; } while (0)
	if (opt.soname)
		LD_DYNAMIC_ENTRY_APPEND(DT_SONAME, soname_off);
	if (opt.bsymbolic)
		LD_DYNAMIC_ENTRY_APPEND(DT_FLAGS, DF_SYMBOLIC);
	LD_DYNAMIC_ENTRY_APPEND(DT_SYMTAB, ld_input_section_address(dynsym_isec, 0));
	LD_DYNAMIC_ENTRY_APPEND(DT_SYMENT, sizeof(elf_symbol));
	LD_DYNAMIC_ENTRY_APPEND(DT_STRTAB, ld_input_section_address(dynstr_isec, 0));
	LD_DYNAMIC_ENTRY_APPEND(DT_STRSZ, dynstr.n);
	LD_DYNAMIC_ENTRY_APPEND(DT_GNU_HASH, ld_input_section_address(gnuhash_isec, 0));
	LD_DYNAMIC_ENTRY_APPEND(DT_HASH, ld_input_section_address(hash_isec, 0));
	if (versym_isec != LD_NONE) {
		LD_DYNAMIC_ENTRY_APPEND(DT_VERSYM, ld_input_section_address(versym_isec, 0));
		LD_DYNAMIC_ENTRY_APPEND(DT_VERDEF, ld_input_section_address(verdef_isec, 0));
		LD_DYNAMIC_ENTRY_APPEND(DT_VERDEFNUM, nverdefs + 1);
	}
	LD_DYNAMIC_ENTRY_APPEND(DT_NULL, 0);
#undef LD_DYNAMIC_ENTRY_APPEND
}
