/* Lazy archive members: which are extracted and in which order, as lld. */

/* ---- lazy archives: which members, in which order (as lld) ----
 * lld reads the inputs in order; an object's undefined (non-weak) reference to
 * a symbol a lazy member defines extracts that member at once (depth first,
 * the member is added to the files before its own symbols are read), and a
 * lazy archive's member is extracted when one of its definitions meets an
 * undefined non-weak reference already seen.  Only links with a non-whole
 * archive run this sequential pass. */
enum { LS_NONE, LS_UNDEF, LS_WEAKREF, LS_DEF, LS_LAZY };
typedef struct { const char *name; uint32_t len, h; uint8_t st; uint32_t cand; } LSym;
static struct { LSym *v; uint32_t mask, n; } lsyms;
static PendVec lcand, lout;
static VVEC(uint8_t) ltaken;

static LSym *lsym(const char *name)
{
	uint32_t len = strlen(name), h = strhash(name, len);

	if (!lsyms.v || (lsyms.n + 1) * 2 > lsyms.mask) {
		uint32_t m = lsyms.v ? (lsyms.mask + 1) * 2 : 4096;
		LSym *nv = calloc(m, sizeof(LSym));

		for (uint32_t k = 0; lsyms.v && k <= lsyms.mask; k++)
			if (lsyms.v[k].name) {
				uint32_t j = lsyms.v[k].h & (m - 1);

				while (nv[j].name)
					j = (j + 1) & (m - 1);
				nv[j] = lsyms.v[k];
			}
		free(lsyms.v);
		lsyms.v = nv;
		lsyms.mask = m - 1;
	}
	uint32_t j = h & lsyms.mask;

	for (; lsyms.v[j].name; j = (j + 1) & lsyms.mask)
		if (lsyms.v[j].h == h && lsyms.v[j].len == len && !memcmp(lsyms.v[j].name, name, len))
			return &lsyms.v[j];
	lsyms.n++;
	lsyms.v[j] = (LSym){ .name = name, .len = len, .h = h };
	return &lsyms.v[j];
}

/* the global symbols of a pending object (mapped and aligned here; NULL: not
 * a valid object, left to parse_obj to report) */
static const Sym *lglobals(Pending *p, uint32_t *n, const char **str)
{
	if (!p->buf) {
		p->buf = map_file(p->path, &p->size);
		if (!p->buf)
			return NULL;
	}
	if ((uintptr_t)p->buf & 7) {
		uint8_t *c = alloc(p->size);

		memcpy(c, p->buf, p->size);
		p->buf = c;
	}
	const Ehdr *eh = (const Ehdr *)p->buf;

	if (p->size < sizeof(Ehdr) || memcmp(eh->e_ident, "\177ELF\2\1", 6))
		return NULL;
	Obj o = { .path = p->path, .buf = p->buf, .size = p->size };
	obj_quiet = true;	/* parse_obj reports */
	bool ok = obj_valid(&o, eh);

	obj_quiet = false;
	if (!ok)
		return NULL;
	for (uint32_t s = 0; s < o.nsh; s++)
		if (o.sh[s].sh_type == SHT_SYMTAB) {
			const Sym *y = (const Sym *)(o.buf + o.sh[s].sh_offset);

			*n = o.sh[s].sh_size / sizeof(Sym) - o.sh[s].sh_info;
			*str = (const char *)o.buf + o.sh[o.sh[s].sh_link].sh_offset;
			return y + o.sh[s].sh_info;
		}
	*n = 0;
	return NULL;
}

static void lextract(uint32_t c);

static void lobject(Pending p)
{
	uint32_t n = 0;
	const char *str;
	VVEC_PUSH(lout, p, alloc);
	Pending *q = &lout.v[lout.n - 1];
	const Sym *y = lglobals(q, &n, &str);

	for (uint32_t k = 0; y && k < n; k++) {
		LSym *l = lsym(str + y[k].st_name);
		bool weak = y[k].st_info >> 4 == STB_WEAK;

		if (y[k].st_shndx != SHN_UNDEF)
			l->st = LS_DEF;
		else if (l->st == LS_LAZY) {
			if (!weak)
				lextract(l->cand);
		} else if (l->st == LS_NONE || l->st == LS_WEAKREF)
			l->st = weak ? (l->st == LS_NONE ? LS_WEAKREF : l->st) : LS_UNDEF;
	}
}

static void lextract(uint32_t c)
{
	if (ltaken.v[c])
		return;
	ltaken.v[c] = 1;
	lobject(lcand.v[c]);
}

static void larchive(uint32_t c0, uint32_t c1)
{
	for (uint32_t c = c0; c < c1; c++) {
		uint32_t n = 0;
		const char *str;
		const Sym *y = lglobals(&lcand.v[c], &n, &str);

		for (uint32_t k = 0; y && k < n && !ltaken.v[c]; k++) {
			if (y[k].st_shndx == SHN_UNDEF)
				continue;
			LSym *l = lsym(str + y[k].st_name);

			if (l->st == LS_UNDEF)
				lextract(c);
			else if (l->st == LS_NONE || l->st == LS_WEAKREF) {
				l->st = LS_LAZY;
				l->cand = c;
			}
		}
	}
}
