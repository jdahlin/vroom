/* The first half of the symbol table: which locals are kept, counting them, and writing a symbol record. */

/* ---------------- symbol table ---------------- */

static bool keep_local(const Obj *o, const Sym *y)
{
	uint8_t type = y->st_info & 15;
	if (type == STT_SECTION)
		return false;
	const char *n = o->str + y->st_name;
	if (opt.discard_all)
		return false;
	if (y->st_shndx == SHN_UNDEF)
		return false;		/* not a Defined */
	if (y->st_shndx != SHN_ABS && y->st_shndx < 0xff00) {
		uint32_t s = o->isec0 + y->st_shndx;
		ISec *is = &isecs.v[s];
		uint32_t os = isec_osec(s);
		if (is->kind == K_NONE || os == NONE || os == OS_DISCARD)
			return false;
		if (n[0] == '.' && n[1] == 'L' && (opt.discard_locals || (is->flags & SHF_MERGE)))
			return false;
	} else if (n[0] == '.' && n[1] == 'L' && opt.discard_locals) {
		return false;
	}
	return true;
}

/* which locals go to .symtab, remembered (index, name length) for write_locals */
static void count_locals(void *arg, size_t i)
{
	(void)arg;
	static __thread uint32_t (*scratch)[2];
	static __thread uint32_t scap;
	Obj *o = objs.v[i];
	uint32_t n = 0, end = o->firstg < o->nsym ? o->firstg : o->nsym;
	uint64_t str = 0;
	if (o->buf && end > 1) {
		if (scap < end) {
			free(scratch);
			scap = end < 4096 ? 4096 : end;
			scratch = malloc((size_t)scap * sizeof *scratch);
		}
		for (uint32_t s = 1; s < end; s++)
			if (keep_local(o, &o->sym[s])) {
				uint32_t l = (uint32_t)strlen(o->str + o->sym[s].st_name);
				scratch[n][0] = s;
				scratch[n][1] = l;
				n++;
				str += l + 1;
			}
		o->kept = alloc((size_t)n * sizeof *scratch + 1);
		memcpy(o->kept, scratch, (size_t)n * sizeof *scratch);
	}
	o->nloc = n;
	o->locstr = str;
}

static uint16_t sym_shndx_isec(uint32_t sec)
{
	uint32_t os = isec_osec(sec);
	if (os == NONE || os == OS_DISCARD)
		return 0;
	return osecs.v[os].shidx;
}

static void put_sym(Sym *d, uint8_t *strbuf, uint64_t *stroff, const char *name, uint8_t info,
		    uint8_t other, uint16_t shndx, uint64_t val, uint64_t size)
{
	size_t n = strlen(name);
	memcpy(strbuf + *stroff, name, n + 1);
	d->st_name = (uint32_t)*stroff;
	*stroff += n + 1;
	d->st_info = info;
	d->st_other = other;
	d->st_shndx = shndx;
	d->st_value = val;
	d->st_size = size;
}
