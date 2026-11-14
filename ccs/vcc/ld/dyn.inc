/*
 * x86-64 dynamic executables (and -pie): part of ld/ld.c (#included there,
 * before arch/x64/ld/x64.inc, which calls it from its scan and relocation
 * passes).  What lld does, without byte identity as a goal:
 *
 *   - an imported symbol (GS_UNDEF with dso1, dso.inc) gets a PLT entry for
 *     calls (R_X86_64_PLT32), a .got slot with R_X86_64_GLOB_DAT for GOT
 *     references, a symbolic R_X86_64_64 for an absolute address in a
 *     writable section; an absolute or PC-relative reference from
 *     read-only code makes it local: a function's PLT entry becomes its
 *     address (canonical PLT, exported with that value), data is copied
 *     into .bss by an R_X86_64_COPY (with its aliases at the same address);
 *   - under -pie an absolute address of a local definition in a writable
 *     section becomes R_X86_64_RELATIVE (a read-only one is an error, as
 *     lld's "recompile with -fPIC");
 *   - the sections: .interp, .dynsym, .gnu.version, .gnu.version_r,
 *     .gnu.hash, .hash, .dynstr, .rela.dyn, .rela.plt (read-only, first),
 *     .plt, .dynamic (RELRO), .got.plt (lazy binding: PLT0 and the
 *     JUMP_SLOTs as lld's x86-64 target);
 *   - no SECTIONS (deflayout): lld's default layout, image base 0x200000
 *     (0 under -pie), the ELF and program headers in the first PT_LOAD,
 *     each PT_LOAD advanced to the next page at the same page offset;
 *   - the linker-defined symbols (__ehdr_start, _GLOBAL_OFFSET_TABLE_,
 *     _DYNAMIC, __init_array_start ..., _etext, _edata, __bss_start, _end)
 *     when referenced and not defined by an input.
 */


static uint32_t interp_isec = NONE, plt_isec = NONE, gotplt_isec = NONE, relaplt_isec = NONE;
static uint32_t verneed_isec = NONE, copy_isec = NONE;
static VVEC(uint32_t) plts;		/* G ids in PLT order */
static VVEC(uint32_t) xdsyms;		/* .dynsym order (index i + 1) */
static uint32_t xsymndx;		/* first hashed .dynsym index */
static uint32_t xgh_nb, xgh_mw;		/* .gnu.hash buckets, bloom words */
static VVEC(char) xstr;			/* .dynstr */
static uint32_t *xname, *xhash;		/* per xdsyms entry */
static uint16_t *xver;			/* per xdsyms entry: .gnu.version */
typedef struct { uint32_t dso; uint32_t name; uint32_t hash; uint16_t other; } NeedVer;
static VVEC(NeedVer) needvers;		/* grouped by dso, in dsos order */
static VVEC(uint32_t) needed;		/* DT_NEEDED: .dynstr offsets */
static uint32_t nverneed;		/* files with versions */
static uint32_t nxdyn;			/* .dynamic entries */
static uint8_t *xexport;		/* per G: exported definition */

static bool sec_live(uint32_t isec);
static uint8_t *isec_out(uint32_t isec);
static bool g_referenced(uint32_t g);
static uint32_t elf_hash(const char *n);
static uint32_t gnu_hash(const char *n);

static uint32_t xstr_add(const char *str)
{
	size_t n = strlen(str);

	for (uint32_t o = 1; o + n < xstr.n; ) {	/* (few strings: a linear search, lld dedups too) */
		size_t l = strlen(xstr.v + o);

		if (l == n && !memcmp(xstr.v + o, str, n))
			return o;
		o += l + 1;
	}
	uint32_t o = xstr.n;

	for (size_t i = 0; i <= n; i++)
		VVEC_PUSH(xstr, str[i], alloc);
	return o;
}

static const Sym *g_dsosym(const G *s)
{
	return &dsos.v[s->dso1 - 1]->dsym[s->dsym];
}


/* ---- the sections, created with the other synthetic ones (sized in dyn_after_scan) ---- */

static void dyn_add_sections(void)
{
	if (!dynexe)
		return;
	if (opt.dynlinker && !opt.no_dynlinker) {
		size_t n = strlen(opt.dynlinker) + 1;
		uint8_t *d = alloc(n);

		memcpy(d, opt.dynlinker, n);
		interp_isec = new_isec(".interp", K_INTERP, SHT_PROGBITS, SHF_ALLOC, 1);
		isecs.v[interp_isec].data = d;
		isecs.v[interp_isec].size = n;
	}
	dynsym_isec = new_isec(".dynsym", K_DYNSYM, SHT_DYNSYM, SHF_ALLOC, 8);
	isecs.v[dynsym_isec].entsize = sizeof(Sym);
	versym_isec = new_isec(".gnu.version", K_VERSYM, 0x6fffffffu /* SHT_GNU_versym */, SHF_ALLOC, 2);
	isecs.v[versym_isec].entsize = 2;
	verneed_isec = new_isec(".gnu.version_r", K_VERNEED, 0x6ffffffeu, SHF_ALLOC, 4);
	gnuhash_isec = new_isec(".gnu.hash", K_GNUHASH, 0x6ffffff6u /* SHT_GNU_HASH */, SHF_ALLOC, 8);
	hash_isec = new_isec(".hash", K_HASH, SHT_HASH, SHF_ALLOC, 4);
	isecs.v[hash_isec].entsize = 4;
	dynstr_isec = new_isec(".dynstr", K_DYNSTR, SHT_STRTAB, SHF_ALLOC, 1);
	reladyn_isec = new_isec(".rela.dyn", K_RELADYN, SHT_RELA, SHF_ALLOC, 8);
	isecs.v[reladyn_isec].entsize = sizeof(Rela);
	relaplt_isec = new_isec(".rela.plt", K_RELAPLT, SHT_RELA, SHF_ALLOC, 8);
	isecs.v[relaplt_isec].entsize = sizeof(Rela);
	plt_isec = new_isec(".plt", K_PLT, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 16);
	dynamic_isec = new_isec(".dynamic", K_DYNAMIC, SHT_DYNAMIC, SHF_ALLOC | SHF_WRITE, 8);
	isecs.v[dynamic_isec].entsize = 16;
	copy_isec = new_isec(".bss", K_NORMAL, SHT_NOBITS, SHF_ALLOC | SHF_WRITE, 1);
}

/* .got.plt: dynamic executables, and _GLOBAL_OFFSET_TABLE_ in a static one */
static void dyn_add_gotplt(void)
{
	uint32_t g = x86_ld && !opt.relocatable ? gfind("_GLOBAL_OFFSET_TABLE_") : NONE;

	if (dynexe || (g != NONE && g_referenced(g) && gsyms[g].state == GS_UNDEF))
		gotplt_isec = new_isec(".got.plt", K_GOTPLT, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 8);
}

/* ---- linker-defined symbols ---- */

static const char *const reserved_names[] = {
	"__ehdr_start", "_GLOBAL_OFFSET_TABLE_", "_DYNAMIC",
	"__preinit_array_start", "__preinit_array_end", "__init_array_start", "__init_array_end",
	"__fini_array_start", "__fini_array_end", "__rela_iplt_start", "__rela_iplt_end",
	"_etext", "etext", "_edata", "edata", "__bss_start", "_end", "end",
};
#define NRESERVED (sizeof reserved_names / sizeof *reserved_names)
static uint32_t reserved_g[NRESERVED];

static void define_reserved(void)
{
	for (uint32_t i = 0; i < NRESERVED; i++) {
		reserved_g[i] = NONE;
		if (!x86_ld || opt.relocatable)
			continue;
		if (i == 0 && !deflayout)
			continue;	/* (the headers are loaded only in the default layout) */
		if (i == 2 && !dynexe)
			continue;
		uint32_t g = gfind(reserved_names[i]);

		if (g == NONE || !g_referenced(g) || gsyms[g].state != GS_UNDEF)
			continue;
		G *s = &gsyms[g];

		s->state = GS_SCRIPT;
		s->dso1 = 0;
		s->obj = NONE;
		s->ev = (EV){.kind = EV_OSEC, .idx = AETHER, .val = 0, .align = 1};	/* (not absolute; set after layout) */
		s->type = STT_NOTYPE;
		s->size = 0;
		s->bind = STB_GLOBAL;
		if (i <= 10)
			s->vis = STV_HIDDEN;
		s->thunk = NONE;
		s->ver = 1;
		reserved_g[i] = g;
	}
}

static uint32_t osec_named(const char *n)
{
	for (uint32_t i = 0; i < outsecs.n; i++)
		if (!strcmp(osecs.v[outsecs.v[i]].name, n))
			return outsecs.v[i];
	return NONE;
}

/* after the final layout: their values, relative to the first allocated section */
static void patch_reserved(void)
{
	uint32_t first = NONE;
	uint64_t etext = 0, edata = 0, end = 0, bss = 0;
	bool have_bss = false;

	for (uint32_t i = 0; i < outsecs.n; i++) {
		OSec *os = &osecs.v[outsecs.v[i]];

		if (!(os->flags & SHF_ALLOC))
			continue;
		if (first == NONE)
			first = outsecs.v[i];
		bool tbss = (os->flags & SHF_TLS) && os->type == SHT_NOBITS;

		if (os->flags & SHF_EXECINSTR)
			etext = os->va + os->size;
		if (os->type != SHT_NOBITS)
			edata = os->va + os->size;
		if (!tbss && os->va + os->size > end)
			end = os->va + os->size;
		if (os->type == SHT_NOBITS && !tbss && !have_bss && !strcmp(os->name, ".bss"))
			bss = os->va, have_bss = true;
	}
	if (first == NONE)
		return;
	uint64_t base = osecs.v[first].va;

	for (uint32_t i = 0; i < NRESERVED; i++) {
		if (reserved_g[i] == NONE)
			continue;
		uint64_t v = base;
		const char *n = reserved_names[i];

		if (i == 0)
			v = hdr_va;
		else if (i == 1)
			v = gotplt_isec != NONE && sec_live(gotplt_isec) ? isec_va(gotplt_isec, 0) :
			    got_isec != NONE && sec_live(got_isec) ? isec_va(got_isec, 0) : base;
		else if (i == 2)
			v = isec_va(dynamic_isec, 0);
		else if (i <= 8) {
			char sn[32];
			const char *u = strchr(n + 2, '_');	/* "__init_array_start": ".init_array" */
			size_t l = (size_t)(strrchr(n, '_') - (n + 2));

			(void)u;
			snprintf(sn, sizeof sn, ".%.*s", (int)l, n + 2);
			uint32_t o = osec_named(sn);

			if (o != NONE)
				v = osecs.v[o].va + (strstr(n, "_end") ? osecs.v[o].size : 0);
		} else if (i <= 10)
			v = base;	/* no .rela.iplt: empty */
		else if (i <= 12)
			v = etext;
		else if (i <= 14)
			v = edata;
		else if (i == 15)
			v = have_bss ? bss : edata;
		else
			v = end;
		gsyms[reserved_g[i]].ev = (EV){.kind = EV_OSEC, .idx = first, .val = v - base, .align = 1};
	}
}

/* ---- the scan: what a relocation against an imported symbol needs ---- */

/* the target of a relocation, if it is an imported symbol */
static G *dyn_imported(const Obj *o, uint32_t symi)
{
	if (!dynexe || symi < o->firstg)
		return NULL;
	G *s = &gsyms[o->gs[symi - o->firstg]];

	return s->dso1 ? s : NULL;
}

static void reloc_where(const Obj *o, const ISec *is, const Rela *rl, char *buf, size_t n)
{
	snprintf(buf, n, "%s:(%s+0x%llx)", o->path, is->name, (unsigned long long)rl->r_offset);
}

/* one relocation of the x86-64 scan; returns the dynamic relocations it needs in .rela.dyn */
static uint32_t dyn_scan_reloc(const Obj *o, const ISec *is, const Rela *rl, const Target *t)
{
	uint32_t type = (uint32_t)rl->r_info, symi = rl->r_info >> 32;
	G *s = dyn_imported(o, symi);
	bool wr = is->flags & SHF_WRITE;
	char where[512];

	if (!(is->flags & SHF_ALLOC))
		return 0;
	if (s) {
		bool func = s->type == STT_FUNC || s->type == 10 /* STT_GNU_IFUNC */;

		switch (type) {
		case R_X86_64_PLT32:
			if (func || s->type == STT_NOTYPE)
				atomic_store_explicit(&s->needplt, 1, memory_order_relaxed);
			else
				atomic_store_explicit(&s->needcopy, 1, memory_order_relaxed);
			return 0;
		case R_X86_64_64:
			if (wr)
				return 1;	/* symbolic */
			__attribute__((fallthrough));
		case R_X86_64_PC32: case R_X86_64_PC64: case R_X86_64_32: case R_X86_64_32S:
		case R_X86_64_16: case R_X86_64_PC16: case R_X86_64_8: case R_X86_64_PC8:
			if (func) {
				atomic_store_explicit(&s->needplt, 1, memory_order_relaxed);
				atomic_store_explicit(&s->canon, 1, memory_order_relaxed);
			} else
				atomic_store_explicit(&s->needcopy, 1, memory_order_relaxed);
			return 0;
		case R_X86_64_GOTTPOFF: case R_X86_64_TLSGD: case R_X86_64_TLSLD: case R_X86_64_DTPOFF32:
		case R_X86_64_DTPOFF64: case R_X86_64_TPOFF32: case R_X86_64_TPOFF64:
			reloc_where(o, is, rl, where, sizeof where);
			error("%s: TLS relocation %u against %s, defined in a shared object, is not supported",
			      where, type, gname(o->gs[symi - o->firstg]));
			return 0;
		default:
			return 0;
		}
	}
	if (!pic || t->abs || t->undef || t->discarded)
		return 0;
	if (type == R_X86_64_64) {
		if (wr)
			return 1;	/* R_X86_64_RELATIVE */
		reloc_where(o, is, rl, where, sizeof where);
		error("%s: relocation R_X86_64_64 cannot be used in a read-only section of a PIE; recompile with -fPIC", where);
	} else if (type == R_X86_64_32 || type == R_X86_64_32S) {
		reloc_where(o, is, rl, where, sizeof where);
		error("%s: relocation R_X86_64_32%s cannot be used against a local address in a PIE; recompile with -fPIC",
		      where, type == R_X86_64_32S ? "S" : "");
	}
	return 0;
}

/* in the relocation pass: R_X86_64_64's dynamic relocation (the count dyn_scan_reloc gave) */
static void dyn_reloc64(const Obj *o, const ISec *is, const Rela *rl, const Target *t, uint64_t P, Rela *dyn, uint32_t *nd)
{
	uint32_t symi = rl->r_info >> 32;
	G *s = dyn_imported(o, symi);

	if (!dyn || !(is->flags & SHF_ALLOC) || !(is->flags & SHF_WRITE))
		return;
	if (s) {
		dyn[*nd].r_offset = P;
		dyn[*nd].r_info = (uint64_t)s->dynidx << 32 | R_X86_64_64;
		dyn[*nd].r_addend = rl->r_addend;
		(*nd)++;
		return;
	}
	if (!pic || t->abs || t->undef || t->discarded)
		return;
	dyn[*nd].r_offset = P;
	dyn[*nd].r_info = R_X86_64_RELATIVE_;
	dyn[*nd].r_addend = (int64_t)t->va;
	(*nd)++;
}

/* the address a call or reference to an imported symbol resolves to (its PLT entry), or 0 */
static uint64_t dyn_plt_va(const G *s)
{
	return s->needplt ? isec_va(plt_isec, 16 * ((uint64_t)s->pltidx + 1)) : 0;
}

/* ---- after the scan: PLT, copies, .dynsym, versions, sizes ---- */

static int cmp_xdsym(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
	uint32_t hx = gnu_hash(gname(x)) % xgh_nb, hy = gnu_hash(gname(y)) % xgh_nb;

	return hx != hy ? (hx < hy ? -1 : 1) : (x > y) - (x < y);
}

static void drop_isec(uint32_t si)
{
	if (si == NONE || !sec_live(si))
		return;
	ISD *d = &isds.v[isecs.v[si].isd];
	uint32_t w = 0;

	for (uint32_t k = 0; k < d->secs.n; k++)
		if (d->secs.v[k] != si)
			d->secs.v[w++] = d->secs.v[k];
	d->secs.n = w;
	isecs.v[si].osec = NONE;
}

/* the imported symbols that became local definitions (copies, canonical PLTs): before the .got is counted */
static void dyn_localize(void)
{
	if (!dynexe)
		return;
	uint32_t ng = atomic_load(&ngsyms);
	uint64_t coff = 0;
	uint32_t calign = 1;

	/* (in symbol table order: ids depend on the threads' interning order) */
	plts.n = 0;
	for (uint32_t i = 0; i < gorder.n; i++) {
		uint32_t g = gorder.v[i];
		G *s = &gsyms[g];

		if (!s->dso1 || s->state != GS_UNDEF)
			continue;
		if (s->needplt) {
			s->pltidx = plts.n;
			VVEC_PUSH(plts, g, alloc);
		}
	}
	for (uint32_t i = 0, n0 = gorder.n; i < n0; i++) {
		uint32_t g = gorder.v[i];
		G *s = &gsyms[g];

		if (!s->dso1 || s->state != GS_UNDEF)
			continue;
		const Sym *y = g_dsosym(s);

		if (s->canon) {
			s->state = GS_SCRIPT;
			s->ev = (EV){.kind = EV_ISEC, .idx = plt_isec, .val = 16 * ((uint64_t)s->pltidx + 1), .align = 1};
			s->type = STT_FUNC;
		} else if (s->needcopy) {
			if (!y->st_size)
				warn("copy relocation of %s, which has no size", gname(g));
			uint64_t v = y->st_value;
			uint32_t a = v ? 1u << __builtin_ctzll(v) : 16;

			if (a > 32)
				a = 32;
			coff = alignup(coff, a);
			if (a > calign)
				calign = a;
			/* the same object under other names (environ, __environ): one copy, all of them
			 * exported (the shared object's own references bind to the copy) */
			{
				const Dso *so = dsos.v[s->dso1 - 1];

				for (uint32_t k = 1; k < so->ndsym; k++) {
					const Sym *z = &so->dsym[k];

					if (k == s->dsym || z->st_value != v || z->st_shndx != y->st_shndx || !dso_sym_def(so, k) ||
					    (z->st_info & 15) == STT_FUNC)
						continue;
					const char *an = so->dstr + z->st_name;
					uint32_t h = gfind(an);

					if (h != NONE && (gsyms[h].state != GS_UNDEF || gsyms[h].dso1))
						continue;	/* defined here, or imported: handled below */
					if (h == NONE) {
						h = gintern(an);
						VVEC_PUSH(gorder, h, alloc);
						ng = atomic_load(&ngsyms);
					}
					G *q = &gsyms[h];

					q->dso1 = s->dso1;
					q->dsym = k;
					q->state = GS_SCRIPT;
					q->ev = (EV){.kind = EV_ISEC, .idx = copy_isec, .val = coff, .align = 1};
					q->needcopy = 2;
					q->type = z->st_info & 15;
					q->size = z->st_size;
					q->bind = z->st_info >> 4 == STB_WEAK ? STB_WEAK : STB_GLOBAL;
					q->vis = STV_DEFAULT;
					q->obj = NONE;
					q->thunk = NONE;
					q->ver = 1;
				}
			}
			for (uint32_t h = 0; h < ng; h++) {
				G *q = &gsyms[h];

				if (h == g || q->dso1 != s->dso1 || q->state != GS_UNDEF || q->canon)
					continue;
				const Sym *z = g_dsosym(q);

				if (z->st_value == v && z->st_shndx == y->st_shndx && (z->st_info & 15) != STT_FUNC) {
					q->state = GS_SCRIPT;
					q->ev = (EV){.kind = EV_ISEC, .idx = copy_isec, .val = coff, .align = 1};
					q->needcopy = 2;	/* an alias: no relocation of its own */
					q->bind = z->st_info >> 4 == STB_WEAK ? STB_WEAK : STB_GLOBAL;
					q->vis = STV_DEFAULT;
					q->obj = NONE;
				}
			}
			s->state = GS_SCRIPT;
			s->ev = (EV){.kind = EV_ISEC, .idx = copy_isec, .val = coff, .align = 1};
			s->needcopy = 1;
			coff += y->st_size;
		} else
			continue;
		s->bind = y->st_info >> 4 == STB_WEAK ? STB_WEAK : STB_GLOBAL;
		s->vis = STV_DEFAULT;
		s->obj = NONE;
	}
	isecs.v[copy_isec].size = coff;
	isecs.v[copy_isec].align = calign;
}

static void dyn_after_scan(void)
{
	if (!dynexe)
		return;
	uint32_t ng = atomic_load(&ngsyms);

	/* exported definitions: -E, or named by a shared object's undefined symbol */
	xexport = alloc(ng + 1);
	memset(xexport, 0, ng + 1);
	for (uint32_t d = 0; d < dsos.n; d++) {
		const Dso *so = dsos.v[d];

		for (uint32_t k = 1; k < so->ndsym; k++)
			if (so->dsym[k].st_shndx == SHN_UNDEF) {
				uint32_t g = gfind(so->dstr + so->dsym[k].st_name);

				if (g != NONE && gsyms[g].state == GS_OBJ && !gsyms[g].demoted)
					xexport[g] = 1;
			}
	}
	/* .dynsym: imported (undefined), then the hashed definitions by .gnu.hash bucket */
	xdsyms.n = 0;
	uint32_t nund = 0;

	for (uint32_t i = 0; i < gorder.n; i++) {
		uint32_t g = gorder.v[i];
		G *s = &gsyms[g];

		if (s->dso1 && s->state == GS_UNDEF && g_referenced(g))
			VVEC_PUSH(xdsyms, g, alloc);
	}
	nund = xdsyms.n;
	for (uint32_t i = 0; i < gorder.n; i++) {
		uint32_t g = gorder.v[i];
		G *s = &gsyms[g];
		bool def = (s->dso1 && s->state == GS_SCRIPT) ||
			   (s->state == GS_OBJ && !s->demoted && (xexport[g] || opt.export_dynamic) &&
			    (s->vis == STV_DEFAULT || s->vis == STV_PROTECTED) && s->bind != STB_LOCAL && s->ver != 0);

		if (def)
			VVEC_PUSH(xdsyms, g, alloc);
	}
	uint32_t n = xdsyms.n, nh = n - nund;

	xgh_nb = nh / 4 > 1 ? nh / 4 : 1;
	xgh_mw = 1;
	while (xgh_mw <= (uint64_t)nh * 12 / 64)
		xgh_mw <<= 1;
	if (nh)
		qsort(xdsyms.v + nund, nh, sizeof(uint32_t), cmp_xdsym);
	xsymndx = nund + 1;
	xstr.n = 0;
	VVEC_PUSH(xstr, 0, alloc);
	xname = alloc((n + 1) * sizeof(uint32_t));
	xhash = alloc((n + 1) * sizeof(uint32_t));
	xver = alloc((n + 1) * sizeof(uint16_t));
	/* DT_NEEDED: every shared object, an --as-needed one only when used */
	needed.n = 0;
	for (uint32_t d = 0; d < dsos.n; d++)
		if (!dsos.v[d]->as_needed || atomic_load(&dsos.v[d]->used))
			VVEC_PUSH(needed, xstr_add(dsos.v[d]->soname), alloc);
	needvers.n = 0;
	nverneed = 0;
	uint16_t nextver = 2;

	for (uint32_t i = 0; i < n; i++) {
		uint32_t g = xdsyms.v[i];
		G *s = &gsyms[g];

		s->dynidx = i + 1;
		xname[i] = xstr_add(gname(g));
		xhash[i] = gnu_hash(gname(g));
		xver[i] = 1;
		if (!s->dso1)
			continue;
		const Dso *so = dsos.v[s->dso1 - 1];
		const char *vn = dso_vername(so, s->dsym);

		if (!vn)
			continue;
		/* one Vernaux per (file, version) */
		uint32_t k;

		for (k = 0; k < needvers.n; k++)
			if (needvers.v[k].dso == s->dso1 - 1 && !strcmp(xstr.v + needvers.v[k].name, vn))
				break;
		if (k == needvers.n) {
			NeedVer nv = { .dso = s->dso1 - 1, .name = xstr_add(vn), .hash = elf_hash(vn), .other = nextver++ };
			uint32_t at = needvers.n;

			VVEC_PUSH(needvers, nv, alloc);
			/* keep the entries of one file together (sorted insertion by dso) */
			while (at > 0 && needvers.v[at - 1].dso > nv.dso) {
				needvers.v[at] = needvers.v[at - 1];
				at--;
			}
			needvers.v[at] = nv;
			k = at;
		}
		xver[i] = needvers.v[k].other;
	}
	for (uint32_t k = 0; k < needvers.n; k++)
		if (!k || needvers.v[k].dso != needvers.v[k - 1].dso) {
			nverneed++;
			(void)xstr_add(dsos.v[needvers.v[k].dso]->soname);
		}
	/* .dynamic */
	bool init = gfind("_init") != NONE && gsyms[gfind("_init")].state == GS_OBJ;
	bool fini = gfind("_fini") != NONE && gsyms[gfind("_fini")].state == GS_OBJ;

	nxdyn = needed.n + 1 /* DEBUG */ + 3 /* RELA */ + 4 /* PLT */ + 4 /* SYMTAB.. */ + 2 /* hashes */ + 3 /* versions */ +
		(init ? 1 : 0) + (fini ? 1 : 0) + 6 /* (pre)init/fini arrays */ + 2 /* FLAGS, FLAGS_1 */ + 1 /* NULL */;

	isecs.v[dynsym_isec].size = (uint64_t)(n + 1) * sizeof(Sym);
	isecs.v[versym_isec].size = (uint64_t)(n + 1) * 2;
	isecs.v[verneed_isec].size = (uint64_t)nverneed * 16 + (uint64_t)needvers.n * 16;
	isecs.v[hash_isec].size = (uint64_t)(2 + 2 * (n + 1)) * 4;
	isecs.v[gnuhash_isec].size = 16 + (uint64_t)xgh_mw * 8 + (uint64_t)xgh_nb * 4 + (uint64_t)nh * 4;
	isecs.v[dynstr_isec].size = xstr.n;
	isecs.v[relaplt_isec].size = (uint64_t)plts.n * sizeof(Rela);
	isecs.v[plt_isec].size = plts.n ? 16 * ((uint64_t)plts.n + 1) : 0;
	isecs.v[gotplt_isec].size = 8 * (3 + (uint64_t)plts.n);
	isecs.v[dynamic_isec].size = (uint64_t)nxdyn * 16;
	/* copy relocations go in .rela.dyn with the copy section's address */
	uint32_t nc = 0;

	for (uint32_t i = 0; i < n; i++)
		if (gsyms[xdsyms.v[i]].needcopy == 1 && gsyms[xdsyms.v[i]].state == GS_SCRIPT)
			nc++;
	isecs.v[copy_isec].ndyn = nc;
	if (!plts.n) {
		drop_isec(plt_isec);
		drop_isec(relaplt_isec);
	}
	if (!needvers.n)
		drop_isec(verneed_isec);
	if (!isecs.v[copy_isec].size)
		drop_isec(copy_isec);
}

/* sh_link / sh_info of the dynamic sections */
static void dyn_shdrs(void)
{
	if (!dynexe)
		return;
	uint32_t ds = osecs.v[isec_osec(dynsym_isec)].shidx, dstr = osecs.v[isec_osec(dynstr_isec)].shidx;

	osecs.v[isec_osec(dynsym_isec)].link = dstr;
	osecs.v[isec_osec(dynsym_isec)].info = 1;
	osecs.v[isec_osec(hash_isec)].link = ds;
	osecs.v[isec_osec(gnuhash_isec)].link = ds;
	osecs.v[isec_osec(versym_isec)].link = ds;
	if (sec_live(verneed_isec)) {
		osecs.v[isec_osec(verneed_isec)].link = dstr;
		osecs.v[isec_osec(verneed_isec)].info = nverneed;
	}
	osecs.v[isec_osec(dynamic_isec)].link = dstr;
	if (sec_live(reladyn_isec))
		osecs.v[isec_osec(reladyn_isec)].link = ds;
	if (sec_live(relaplt_isec)) {
		osecs.v[isec_osec(relaplt_isec)].link = ds;
		osecs.v[isec_osec(relaplt_isec)].info = osecs.v[isec_osec(gotplt_isec)].shidx;
		osecs.v[isec_osec(relaplt_isec)].flags |= 0x40;	/* SHF_INFO_LINK */
	}
}

/* ---- writing ---- */

static void dyn_write_gotplt(void)
{
	if (gotplt_isec == NONE || !sec_live(gotplt_isec))
		return;
	uint8_t *gp = isec_out(gotplt_isec);

	if (dynexe)
		wr64(gp, isec_va(dynamic_isec, 0));
	if (!dynexe || !plts.n)
		return;
	uint64_t gva = isec_va(gotplt_isec, 0), pva = isec_va(plt_isec, 0);
	uint8_t *p = isec_out(plt_isec);
	Rela *jr = (Rela *)(void *)isec_out(relaplt_isec);

	/* PLT0: pushq GOTPLT+8(%rip); jmp *GOTPLT+16(%rip); nopl 0(%rax) */
	p[0] = 0xff, p[1] = 0x35;
	wr32(p + 2, (uint32_t)(gva + 8 - (pva + 6)));
	p[6] = 0xff, p[7] = 0x25;
	wr32(p + 8, (uint32_t)(gva + 16 - (pva + 12)));
	memcpy(p + 12, "\x0f\x1f\x40\x00", 4);
	for (uint32_t i = 0; i < plts.n; i++) {
		uint8_t *e = p + 16 * (i + 1);
		uint64_t eva = pva + 16 * ((uint64_t)i + 1), slot = gva + 8 * (3 + (uint64_t)i);

		e[0] = 0xff, e[1] = 0x25;	/* jmp *slot(%rip) */
		wr32(e + 2, (uint32_t)(slot - (eva + 6)));
		e[6] = 0x68;			/* pushq $i */
		wr32(e + 7, i);
		e[11] = 0xe9;			/* jmp PLT0 */
		wr32(e + 12, (uint32_t)(pva - (eva + 16)));
		wr64(gp + 8 * (3 + i), eva + 6);	/* lazy: back to the push (ld.so adds the load bias) */
		jr[i].r_offset = slot;
		jr[i].r_info = (uint64_t)gsyms[plts.v[i]].dynidx << 32 | R_X86_64_JUMP_SLOT_;
		jr[i].r_addend = 0;
	}
}

static void dyn_write(void)
{
	dyn_write_gotplt();
	if (!dynexe)
		return;
	uint32_t n = xdsyms.n;
	Sym *ds = (Sym *)(void *)isec_out(dynsym_isec);

	if (interp_isec != NONE && sec_live(interp_isec))
		memcpy(isec_out(interp_isec), isecs.v[interp_isec].data, isecs.v[interp_isec].size);
	for (uint32_t i = 0; i < n; i++) {
		uint32_t g = xdsyms.v[i];
		G *s = &gsyms[g];
		Sym *d = &ds[i + 1];

		d->st_name = xname[i];
		d->st_other = s->state == GS_UNDEF ? STV_DEFAULT : s->vis;
		if (s->state == GS_UNDEF) {
			d->st_info = (uint8_t)(s->bind << 4 | s->type);
			continue;
		}
		d->st_info = (uint8_t)(s->bind << 4 | (s->type == 10 /* STT_GNU_IFUNC */ ? STT_FUNC : s->type));
		d->st_value = gsym_va(g);
		if (s->dso1 && s->canon)
			d->st_shndx = SHN_UNDEF;	/* canonical PLT: undefined, with the entry's address */
		else if (s->state == GS_SCRIPT)
			d->st_shndx = sym_shndx_isec(s->ev.idx);
		else
			d->st_shndx = s->sec == NONE ? SHN_ABS : sym_shndx_isec(s->sec);
		d->st_size = s->dso1 ? g_dsosym(s)->st_size : s->size;
	}
	memcpy(isec_out(dynstr_isec), xstr.v, xstr.n);
	uint8_t *vs = isec_out(versym_isec);

	for (uint32_t i = 0; i < n; i++)
		memcpy(vs + 2 * (i + 1), &xver[i], 2);
	if (needvers.n) {
		uint8_t *vn = isec_out(verneed_isec);
		uint32_t at = 0;

		for (uint32_t k = 0; k < needvers.n; ) {
			uint32_t d = needvers.v[k].dso, c = 0;

			while (k + c < needvers.n && needvers.v[k + c].dso == d)
				c++;
			uint8_t *e = vn + at;
			uint16_t ver = 1, cnt = (uint16_t)c;
			bool last = k + c == needvers.n;

			memcpy(e, &ver, 2);
			memcpy(e + 2, &cnt, 2);
			wr32(e + 4, xstr_add(dsos.v[d]->soname));
			wr32(e + 8, 16);
			wr32(e + 12, last ? 0 : 16 + 16 * c);
			for (uint32_t j = 0; j < c; j++) {
				uint8_t *a = e + 16 + 16 * j;
				uint16_t fl = 0, oth = needvers.v[k + j].other;

				wr32(a, needvers.v[k + j].hash);
				memcpy(a + 4, &fl, 2);
				memcpy(a + 6, &oth, 2);
				wr32(a + 8, needvers.v[k + j].name);
				wr32(a + 12, j + 1 == c ? 0 : 16);
			}
			at += 16 + 16 * c;
			k += c;
		}
	}
	/* .hash */
	{
		uint8_t *h = isec_out(hash_isec);
		uint32_t nb = n + 1, *bk = (uint32_t *)(void *)(h + 8), *ch = bk + nb;

		wr32(h, nb);
		wr32(h + 4, nb);
		for (uint32_t i = 0; i < n; i++) {
			uint32_t b = elf_hash(gname(xdsyms.v[i])) % nb;

			ch[i + 1] = bk[b];
			bk[b] = i + 1;
		}
	}
	/* .gnu.hash */
	{
		uint8_t *gh = isec_out(gnuhash_isec);
		uint64_t *bloom = (uint64_t *)(void *)(gh + 16);
		uint32_t *gb = (uint32_t *)(void *)(gh + 16 + xgh_mw * 8), *gv = gb + xgh_nb, old = NONE;

		wr32(gh, xgh_nb);
		wr32(gh + 4, xsymndx);
		wr32(gh + 8, xgh_mw);
		wr32(gh + 12, 26);
		for (uint32_t i = xsymndx - 1; i < n; i++) {
			uint32_t hv = xhash[i], b = hv % xgh_nb;
			bool last = i + 1 == n || xhash[i + 1] % xgh_nb != b;

			bloom[(hv / 64) & (xgh_mw - 1)] |= (uint64_t)1 << (hv % 64) | (uint64_t)1 << ((hv >> 26) % 64);
			*gv++ = last ? hv | 1 : hv & ~1u;
			if (b != old) {
				gb[b] = i + 1;
				old = b;
			}
		}
	}
	/* copy relocations */
	if (copy_isec != NONE && isecs.v[copy_isec].ndyn && reladyn_buf) {
		Rela *r = reladyn_buf + isecs.v[copy_isec].dyn0;
		uint32_t k = 0;

		for (uint32_t i = 0; i < n; i++) {
			G *s = &gsyms[xdsyms.v[i]];

			if (s->needcopy != 1 || s->state != GS_SCRIPT)
				continue;
			r[k].r_offset = gsym_va(xdsyms.v[i]);
			r[k].r_info = (uint64_t)s->dynidx << 32 | R_X86_64_COPY_;
			r[k].r_addend = 0;
			k++;
		}
	}
	/* .dynamic */
	uint8_t *dy = isec_out(dynamic_isec);
	uint32_t at = 0;
#define XDYN(tag, v) do { wr64(dy + 16 * at, (uint64_t)(tag)); wr64(dy + 16 * at + 8, (uint64_t)(v)); at++; } while (0)
	for (uint32_t k = 0; k < needed.n; k++)
		XDYN(1, needed.v[k]);				/* DT_NEEDED */
	XDYN(21, 0);						/* DT_DEBUG */
	if (sec_live(reladyn_isec) && isecs.v[reladyn_isec].size) {
		XDYN(7, isec_va(reladyn_isec, 0));		/* DT_RELA */
		XDYN(8, isecs.v[reladyn_isec].size);		/* DT_RELASZ */
		XDYN(9, sizeof(Rela));				/* DT_RELAENT */
	}
	if (plts.n) {
		XDYN(23, isec_va(relaplt_isec, 0));		/* DT_JMPREL */
		XDYN(2, isecs.v[relaplt_isec].size);		/* DT_PLTRELSZ */
		XDYN(3, isec_va(gotplt_isec, 0));		/* DT_PLTGOT */
		XDYN(20, 7);					/* DT_PLTREL: DT_RELA */
	}
	XDYN(6, isec_va(dynsym_isec, 0));			/* DT_SYMTAB */
	XDYN(11, sizeof(Sym));					/* DT_SYMENT */
	XDYN(5, isec_va(dynstr_isec, 0));			/* DT_STRTAB */
	XDYN(10, xstr.n);					/* DT_STRSZ */
	XDYN(0x6ffffef5, /* DT_GNU_HASH */ isec_va(gnuhash_isec, 0));
	XDYN(4, isec_va(hash_isec, 0));				/* DT_HASH */
	XDYN(0x6ffffff0, /* DT_VERSYM */ isec_va(versym_isec, 0));
	if (needvers.n) {
		XDYN(0x6ffffffe, isec_va(verneed_isec, 0));	/* DT_VERNEED */
		XDYN(0x6fffffff, nverneed);			/* DT_VERNEEDNUM */
	}
	uint32_t g;

	if ((g = gfind("_init")) != NONE && gsyms[g].state == GS_OBJ)
		XDYN(12, gsym_va(g));				/* DT_INIT */
	if ((g = gfind("_fini")) != NONE && gsyms[g].state == GS_OBJ)
		XDYN(13, gsym_va(g));				/* DT_FINI */
	static const struct { const char *name; uint32_t tag, sz; } arr[] = {
		{".preinit_array", 32, 33}, {".init_array", 25, 27}, {".fini_array", 26, 28},
	};
	for (uint32_t k = 0; k < 3; k++) {
		uint32_t o = osec_named(arr[k].name);

		if (o != NONE && osecs.v[o].size) {
			XDYN(arr[k].tag, osecs.v[o].va);
			XDYN(arr[k].sz, osecs.v[o].size);
		}
	}
	if (opt.znow)
		XDYN(30, 8);					/* DT_FLAGS: DF_BIND_NOW */
	if (opt.pie || opt.znow)
		XDYN(0x6ffffffb, (opt.znow ? 1u : 0) | (opt.pie ? 0x08000000u : 0));	/* DT_FLAGS_1: NOW, PIE */
	XDYN(0, 0);
#undef XDYN
}

/* ---- the default layout ---- */

static uint32_t dl_prev;		/* the previous allocated section of this address pass */

static bool is_relro(uint32_t oi);

/* lld fixSectionAlignments: a section that starts a PT_LOAD is advanced to the
 * next page at the same page offset (and PT_TLS's start to its alignment) */
static uint64_t deflayout_dot(uint32_t oi, uint64_t d)
{
	OSec *os = &osecs.v[oi];
	uint64_t pg = opt.max_page;

	if (dl_prev != NONE) {
		OSec *pv = &osecs.v[dl_prev];
		bool split = phflags(pv) != phflags(os) || (!opt.norelro && is_relro(dl_prev) && !is_relro(oi));

		if (split) {
			if (os->flags & SHF_TLS)
				d = alignup(d, pg) + alignup(d % pg, os->addralign);
			else
				d = alignup(d, pg) + d % pg;
		}
	}
	dl_prev = oi;
	return d;
}
