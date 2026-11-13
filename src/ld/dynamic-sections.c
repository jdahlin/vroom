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

static bool g_referenced(uint32_t g);
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

static uint32_t elf_hash(const char *n)
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
static uint32_t gnu_hash(const char *n)
{
	uint32_t h = 5381;
	for (; *n; n++)
		h = h * 33 + (uint8_t)*n;
	return h;
}

static uint32_t dynstr_add(const char *str)
{
	size_t n = strlen(str);
	for (uint32_t i = 0; i < dynstr_offs.n; i++)
		if (!strcmp(dynstr.v + dynstr_offs.v[i], str))
			return dynstr_offs.v[i];
	uint32_t o = dynstr.n;
	for (size_t i = 0; i <= n; i++)
		VVEC_PUSH(dynstr, str[i], alloc);
	VVEC_PUSH(dynstr_offs, o, alloc);
	return o;
}

/* VERSION: local patterns first, then each node's globals override (lld scanVersionScript) */
static void assign_versions(void)
{
	for (uint32_t i = 0; i < gorder.n; i++) {
		G *s = &gsyms[gorder.v[i]];
		s->ver = 1;
		if (!vernodes.n || s->state == GS_UNDEF)
			continue;
		const char *n = gname(gorder.v[i]);
		for (uint32_t k = 0; k < ver_locals.n; k++)
			if (glob(ver_locals.v[k], n)) {
				s->ver = 0;
				break;
			}
		uint16_t named = 2;
		for (uint32_t v = 0; v < vernodes.n; v++) {
			VerNode *vn = &vernodes.v[v];
			bool hit = false;
			for (uint32_t k = 0; k < vn->globs.n && !hit; k++)
				hit = glob(vn->globs.v[k], n);
			if (hit)
				s->ver = *vn->name ? named : 1;
			if (*vn->name)
				named++;
		}
	}
}

static bool sec_live(uint32_t isec)
{
	uint32_t o = isec == NONE ? NONE : isecs.v[isec].osec;
	return o != NONE && o != OS_DISCARD;
}

/* _DYNAMIC: lld defines it (weak, hidden) after the script's symbols */
static void define_dynamic_sym(void)
{
	dyn_live = opt.shared && sec_live(dynamic_isec);
	if (!dyn_live)
		return;
	uint32_t g = gintern("_DYNAMIC");
	G *s = &gsyms[g];
	if (!g_referenced(g)) {
		VVEC_PUSH(gorder, g, alloc);
		atomic_min64(&s->first, script_ord++);
	}
	s->state = GS_SCRIPT;
	s->ev = (EV){.kind = EV_ISEC, .idx = dynamic_isec, .align = 1};
	s->bind = STB_WEAK;
	s->type = STT_NOTYPE;
	s->vis = STV_HIDDEN;
	s->thunk = NONE;
	s->ver = 1;
}

/* contents and sizes of the dynamic sections (addresses are filled in later) */
static void build_dynamic(void)
{
	if (!dyn_live)
		return;
	if (ndyn)
		error("dynamic relocations in a shared object with .dynamic are not supported");
	dsyms.n = 0;
	for (uint32_t i = 0; i < gorder.n; i++) {
		uint32_t g = gorder.v[i];
		G *s = &gsyms[g];
		if (!g_in_symtab(g) || g_local(g))
			continue;
		if (s->state == GS_UNDEF && (kget(&s->first) >> 32) >= objs.n)
			continue;
		VVEC_PUSH(dsyms, g, alloc);
	}
	/* .gnu.hash: undefined symbols first, then stable by bucket */
	uint32_t n = dsyms.n, nund = 0;
	uint32_t *tmp = alloc((size_t)(n + 1) * sizeof(uint32_t));
	for (uint32_t i = 0; i < n; i++)
		if (gsyms[dsyms.v[i]].state == GS_UNDEF || gsyms[dsyms.v[i]].demoted)
			tmp[nund++] = dsyms.v[i];
	uint32_t nh = n - nund;
	gh_nbuckets = nh / 4 > 1 ? nh / 4 : 1;
	uint64_t words = (uint64_t)nh * 12 / 64;
	gh_maskwords = 1;
	while (gh_maskwords <= words)	/* llvm::NextPowerOf2: strictly greater */
		gh_maskwords <<= 1;
	dsym_hash = alloc((size_t)(n + 1) * sizeof(uint32_t));
	uint32_t w = nund;
	for (uint32_t b = 0; b < gh_nbuckets; b++)
		for (uint32_t i = 0; i < n; i++) {
			G *s = &gsyms[dsyms.v[i]];
			if (s->state == GS_UNDEF || s->demoted)
				continue;
			if (gnu_hash(gname(dsyms.v[i])) % gh_nbuckets == b)
				tmp[w++] = dsyms.v[i];
		}
	if (n)
		memcpy(dsyms.v, tmp, n * sizeof(uint32_t));
	dyn_symndx = nund + 1;
	/* .dynstr: symbol names, then the file's name and version names */
	dynstr.n = dynstr_offs.n = 0;
	VVEC_PUSH(dynstr, 0, alloc);
	dsym_name = alloc((size_t)(n + 1) * sizeof(uint32_t));
	for (uint32_t i = 0; i < n; i++) {
		gsyms[dsyms.v[i]].dynidx = i + 1;
		dsym_hash[i] = gnu_hash(gname(dsyms.v[i]));
		dsym_name[i] = dynstr_add(gname(dsyms.v[i]));
	}
	const char *fname = opt.soname ? opt.soname : opt.out;
	vername_off = alloc((nverdefs + 1) * sizeof(uint32_t));
	if (nverdefs) {
		soname_off = dynstr_add(fname);
		for (uint32_t v = 0, k = 0; v < vernodes.n; v++)
			if (*vernodes.v[v].name)
				vername_off[k++] = dynstr_add(vernodes.v[v].name);
	}
	if (opt.soname)
		soname_off = dynstr_add(opt.soname);
	ndynent = (opt.soname ? 1 : 0) + (opt.bsymbolic ? 1 : 0) + 4 + 2 + (nverdefs ? 3 : 0) + 1;

	isecs.v[dynsym_isec].size = (uint64_t)(n + 1) * sizeof(Sym);
	isecs.v[dynstr_isec].size = dynstr.n;
	isecs.v[hash_isec].size = (uint64_t)(2 + 2 * (n + 1)) * 4;
	isecs.v[gnuhash_isec].size = 16 + (uint64_t)gh_maskwords * 8 + (uint64_t)gh_nbuckets * 4 + (uint64_t)nh * 4;
	if (versym_isec != NONE) {
		isecs.v[versym_isec].size = (uint64_t)(n + 1) * 2;
		isecs.v[verdef_isec].size = (uint64_t)(nverdefs + 1) * 28;
	}
	isecs.v[dynamic_isec].size = (uint64_t)ndynent * 16;
}

/* sh_link / sh_info of the dynamic sections */
static void dynamic_shdrs(void)
{
	if (!dyn_live)
		return;
	uint32_t ds = osecs.v[isec_osec(dynsym_isec)].shidx, dstr = osecs.v[isec_osec(dynstr_isec)].shidx;
	osecs.v[isec_osec(dynsym_isec)].link = dstr;
	osecs.v[isec_osec(dynsym_isec)].info = 1;	/* no local dynamic symbols */
	osecs.v[isec_osec(hash_isec)].link = ds;
	osecs.v[isec_osec(gnuhash_isec)].link = ds;
	osecs.v[isec_osec(dynamic_isec)].link = dstr;
	if (versym_isec != NONE) {
		osecs.v[isec_osec(versym_isec)].link = ds;
		osecs.v[isec_osec(verdef_isec)].link = dstr;
		osecs.v[isec_osec(verdef_isec)].info = nverdefs + 1;
	}
}

static uint8_t *isec_out(uint32_t isec)
{
	return out + osecs.v[isec_osec(isec)].offset + isecs.v[isec].off;
}

static void write_dynamic(void)
{
	if (!dyn_live)
		return;
	uint32_t n = dsyms.n;
	Sym *ds = (Sym *)(void *)isec_out(dynsym_isec);
	for (uint32_t i = 0; i < n; i++) {
		uint32_t g = dsyms.v[i];
		G *s = &gsyms[g];
		Sym *d = &ds[i + 1];
		d->st_name = dsym_name[i];
		d->st_info = (uint8_t)(s->bind << 4 | s->type);
		d->st_other = s->vis;
		if (s->state == GS_SCRIPT) {
			d->st_shndx = s->ev.kind == EV_ABS ? SHN_ABS : s->ev.kind == EV_OSEC ? osec_shidx(s->ev.idx)
										: sym_shndx_isec(s->ev.idx);
			d->st_value = gsym_va(g);
		} else if (s->state == GS_OBJ && !s->demoted) {
			d->st_shndx = s->sec == NONE ? SHN_ABS : sym_shndx_isec(s->sec);
			d->st_value = gsym_va(g);
			d->st_size = s->size;
		}
	}
	memcpy(isec_out(dynstr_isec), dynstr.v, dynstr.n);
	/* .hash: nbucket = nchain = symbol count */
	uint8_t *h = isec_out(hash_isec);
	uint32_t nb = n + 1, *bk = (uint32_t *)(void *)(h + 8), *ch = bk + nb;
	wr32(h, nb);
	wr32(h + 4, nb);
	for (uint32_t i = 0; i < n; i++) {
		uint32_t b = elf_hash(gname(dsyms.v[i])) % nb;
		ch[i + 1] = bk[b];
		bk[b] = i + 1;
	}
	/* .gnu.hash */
	uint8_t *gh = isec_out(gnuhash_isec);
	wr32(gh, gh_nbuckets);
	wr32(gh + 4, dyn_symndx);
	wr32(gh + 8, gh_maskwords);
	wr32(gh + 12, 26);
	uint64_t *bloom = (uint64_t *)(void *)(gh + 16);
	uint32_t *gb = (uint32_t *)(void *)(gh + 16 + gh_maskwords * 8), *gv = gb + gh_nbuckets;
	uint32_t old = NONE;
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
	if (versym_isec != NONE) {
		uint8_t *vs = isec_out(versym_isec);
		for (uint32_t i = 0; i < n; i++) {
			uint16_t v = gsyms[dsyms.v[i]].ver;
			memcpy(vs + 2 * (i + 1), &v, 2);
		}
		uint8_t *vd = isec_out(verdef_isec);
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
			wr32(e + 8, elf_hash(nm));
			wr32(e + 12, 20);
			wr32(e + 16, k == nverdefs ? 0 : 28);
			wr32(e + 20, k ? vername_off[k - 1] : soname_off);
			wr32(e + 24, 0);
		}
	}
	/* .dynamic (lld DynamicSection::computeContents order) */
	uint8_t *dy = isec_out(dynamic_isec);
	uint32_t at = 0;
#define DYN(tag, v) do { wr64(dy + 16 * at, (uint64_t)(tag)); wr64(dy + 16 * at + 8, (uint64_t)(v)); at++; } while (0)
	if (opt.soname)
		DYN(DT_SONAME, soname_off);
	if (opt.bsymbolic)
		DYN(DT_FLAGS, DF_SYMBOLIC);
	DYN(DT_SYMTAB, isec_va(dynsym_isec, 0));
	DYN(DT_SYMENT, sizeof(Sym));
	DYN(DT_STRTAB, isec_va(dynstr_isec, 0));
	DYN(DT_STRSZ, dynstr.n);
	DYN(DT_GNU_HASH, isec_va(gnuhash_isec, 0));
	DYN(DT_HASH, isec_va(hash_isec, 0));
	if (versym_isec != NONE) {
		DYN(DT_VERSYM, isec_va(versym_isec, 0));
		DYN(DT_VERDEF, isec_va(verdef_isec, 0));
		DYN(DT_VERDEFNUM, nverdefs + 1);
	}
	DYN(DT_NULL, 0);
#undef DYN
}
