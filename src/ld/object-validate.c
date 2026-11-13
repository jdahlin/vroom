/* Validation of a relocatable ELF input (every offset and size is checked before use) and its parsing into an Obj. */

/* the bytes of a relocation's field (0: none, 4: an instruction) */
static uint32_t rel_width(uint32_t type)
{
	switch (type) {
	case R_AARCH64_NONE: return 0;
	case R_AARCH64_ABS64: case R_AARCH64_PREL64: return 8;
	case R_AARCH64_ABS16: case R_AARCH64_PREL16: return 2;
	default: return 4;
	}
}

/* a string table: in the file, NUL-terminated */
static bool strtab_ok(const Obj *o, uint32_t k)
{
	const Shdr *h = &o->sh[k];

	return h->sh_type == SHT_STRTAB && h->sh_size && o->buf[h->sh_offset + h->sh_size - 1] == 0;
}

static bool obj_quiet;	/* the sequential lazy-archive pass: no messages */

/* every offset, index and size the later passes use, checked once (as lld's
 * ObjFile: the message names what is wrong; the object is then dropped) */
static bool obj_valid(Obj *o, const Ehdr *eh)
{
#define BAD(...) do { if (!obj_quiet) error(__VA_ARGS__); return false; } while (0)
	uint64_t n = o->size;

	if (eh->e_shoff == 0 || eh->e_shentsize != sizeof(Shdr) || eh->e_shoff % 8 ||
	    eh->e_shoff > n || n - eh->e_shoff < sizeof(Shdr))
		BAD("%s: invalid section header table offset", o->path);
	o->sh = (const Shdr *)(o->buf + eh->e_shoff);
	/* more than 0xff00 sections: the count in sh[0].sh_size, the index in sh_link */
	uint64_t nsh = eh->e_shnum ? eh->e_shnum : o->sh[0].sh_size;
	uint32_t shstrndx = eh->e_shstrndx == SHN_XINDEX ? o->sh[0].sh_link : eh->e_shstrndx;

	if (nsh == 0 || nsh > (n - eh->e_shoff) / sizeof(Shdr))
		BAD("%s: section header table goes past the end of the file", o->path);
	o->nsh = nsh;
	for (uint32_t s = 0; s < o->nsh; s++) {	/* (0 too: shstrndx and sh_link may name it) */
		const Shdr *h = &o->sh[s];

		if (h->sh_type != SHT_NOBITS && h->sh_type != SHT_NULL && !vckd_range(h->sh_offset, h->sh_size, n))
			BAD("%s: section %u goes past the end of the file", o->path, s);
		if (h->sh_addralign & (h->sh_addralign - 1))
			BAD("%s: section %u: sh_addralign is not a power of 2", o->path, s);
	}
	if (shstrndx >= o->nsh || !strtab_ok(o, shstrndx))
		BAD("%s: invalid section name string table", o->path);
	o->shstr = (const char *)o->buf + o->sh[shstrndx].sh_offset;
	uint64_t shstrsz = o->sh[shstrndx].sh_size;
	const Shdr *symtab = NULL;

	for (uint32_t s = 0; s < o->nsh; s++) {
		const Shdr *h = &o->sh[s];

		if (h->sh_name >= shstrsz)
			BAD("%s: section %u: invalid sh_name", o->path, s);
		if (h->sh_type == SHT_SYMTAB) {
			if (symtab)
				BAD("%s: more than one SHT_SYMTAB", o->path);
			symtab = h;
		}
	}
	uint64_t nsym = 0, strsz = 0;

	if (symtab) {
		if (symtab->sh_offset % 8 || symtab->sh_size % sizeof(Sym))
			BAD("%s: invalid SHT_SYMTAB", o->path);
		if (symtab->sh_link >= o->nsh || !strtab_ok(o, symtab->sh_link))
			BAD("%s: invalid symbol string table", o->path);
		nsym = symtab->sh_size / sizeof(Sym);
		if (nsym > UINT32_MAX || symtab->sh_info > nsym || (nsym && symtab->sh_info == 0))
			BAD("%s: invalid sh_info in symbol table", o->path);
		strsz = o->sh[symtab->sh_link].sh_size;
		const Sym *y = (const Sym *)(o->buf + symtab->sh_offset);

		for (uint64_t k = 0; k < nsym; k++) {
			if (y[k].st_name >= strsz)
				BAD("%s: invalid symbol name offset", o->path);
			uint16_t x = y[k].st_shndx;

			if (x == SHN_XINDEX)
				BAD("%s: SHN_XINDEX symbols are not supported", o->path);
			if (x != SHN_UNDEF && x < 0xff00 && x >= o->nsh)
				BAD("%s: invalid section index: %u", o->path, x);
		}
	}
	for (uint32_t s = 0; s < o->nsh; s++) {
		const Shdr *h = &o->sh[s];

		if (h->sh_type == SHT_GROUP) {
			if (h->sh_offset % 4 || h->sh_size % 4 || h->sh_size < 4 || !symtab ||
			    h->sh_link >= o->nsh || &o->sh[h->sh_link] != symtab || h->sh_info >= nsym)
				BAD("%s: invalid SHT_GROUP section %u", o->path, s);
			const uint32_t *w = (const uint32_t *)(o->buf + h->sh_offset);

			for (uint64_t k = 1; k < h->sh_size / 4; k++)
				if (w[k] == 0 || w[k] >= o->nsh)
					BAD("%s: invalid section index in group section %u", o->path, s);
			continue;
		}
		if (h->sh_type != SHT_RELA)
			continue;
		if (h->sh_offset % 8 || h->sh_size % sizeof(Rela) || h->sh_info >= o->nsh)
			BAD("%s: invalid relocation section %u", o->path, s);
		const Shdr *t = &o->sh[h->sh_info];
		const Rela *r = (const Rela *)(o->buf + h->sh_offset);

		for (uint64_t k = 0; k < h->sh_size / sizeof(Rela); k++) {
			if ((r[k].r_info >> 32) >= nsym && (r[k].r_info >> 32))
				BAD("%s: invalid symbol index in relocation section %u", o->path, s);
			uint32_t w = rel_width((uint32_t)r[k].r_info);

			if (t->sh_type != SHT_NOBITS && (r[k].r_offset > t->sh_size || t->sh_size - r[k].r_offset < w))
				BAD("%s: relocation at 0x%llx is past the end of section %u", o->path,
				    (unsigned long long)r[k].r_offset, h->sh_info);
		}
	}
	return true;
#undef BAD
}

static void parse_obj(void *arg, size_t i)
{
	(void)arg;
	Obj *o = objs.v[i];
	if (!o->buf) {
		o->buf = map_file(o->path, &o->size);
		o->mapped = true;
		if (!o->buf)
			return;
	}
	if (((uintptr_t)o->buf & 7) && !o->mapped) {
		/* a member of a regular archive (at 4 mod 8, after its header): the headers need 8-byte alignment */
		uint8_t *c = alloc(o->size);

		memcpy(c, o->buf, o->size);
		o->buf = c;
	}
	const Ehdr *eh = (const Ehdr *)o->buf;
	if (o->size < sizeof(Ehdr) || memcmp(eh->e_ident, "\177ELF", 4) || eh->e_ident[4] != 2 ||
	    eh->e_ident[5] != 1) {
		error("%s: not an ELF64 little-endian file", o->path);
		o->buf = NULL;
		return;
	}
	if (eh->e_type != 1 || (eh->e_machine != 183 && eh->e_machine != 62)) {
		error("%s: not an AArch64 or x86-64 relocatable object", o->path);
		o->buf = NULL;
		return;
	}
	if (eh->e_machine == 62)
		x86_ld = true;	/* (one machine for all inputs: checked once loaded) */
	if (!obj_valid(o, eh)) {
		o->buf = NULL;
		return;
	}
	for (uint32_t s = 0; s < o->nsh; s++) {
		if (o->sh[s].sh_type == SHT_SYMTAB) {
			o->sym = (const Sym *)(o->buf + o->sh[s].sh_offset);
			o->nsym = o->sh[s].sh_size / sizeof(Sym);
			o->firstg = o->sh[s].sh_info;
			o->str = (const char *)o->buf + o->sh[o->sh[s].sh_link].sh_offset;
		}
	}
	if (o->nsym > o->firstg)
		o->gs = alloc((o->nsym - o->firstg) * sizeof(uint32_t));
}
