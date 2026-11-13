/* The second half of the symbol table: sizing, layout of globals and locals, and filling it in parallel. */

/* ---------------- symbol table ---------------- */

typedef struct { uint32_t g; uint32_t idx; uint64_t str; bool local; } GOut;
static GOut *gouts;
static uint32_t ngouts;
static uint8_t *symtab_buf, *strtab_buf;	/* in the output image (fill_symtab) */
static uint64_t thunk_sym0, thunk_str0;		/* first thunk symbol and its string */
static uint64_t nsymout, strsize;
static uint32_t nlocal_out;

static void put_global(Sym *d, uint64_t stroff, uint32_t g, bool local)
{
	G *s = &gsyms[g];
	uint8_t bind = local ? STB_LOCAL : s->bind;
	uint16_t shndx;
	uint64_t val, size = s->size;
	if (s->state == GS_SCRIPT) {
		shndx = s->ev.kind == EV_ABS ? SHN_ABS : s->ev.kind == EV_OSEC ? osec_shidx(s->ev.idx)
										: sym_shndx_isec(s->ev.idx);
		val = gsym_va(g);
		size = 0;
	} else if (s->state == GS_OBJ && !s->demoted) {
		shndx = s->sec == NONE ? SHN_ABS : sym_shndx_isec(s->sec);
		val = gsym_va(g);
	} else {
		shndx = SHN_UNDEF;
		val = 0;
		size = 0;
	}
	uint64_t so = stroff;
	put_sym(d, strtab_buf, &so, gname(g), (uint8_t)(bind << 4 | s->type), s->vis, shndx, val, size);
}

static void write_locals(void *arg, size_t i)
{
	(void)arg;
	Obj *o = objs.v[i];
	if (!o->buf || !o->nloc)
		return;
	Sym *d = (Sym *)symtab_buf + o->loc0;
	uint64_t so = o->str0;
	for (uint32_t k = 0; k < o->nloc; k++) {
		const Sym *y = &o->sym[o->kept[k][0]];
		uint32_t l = o->kept[k][1];
		uint16_t shndx;
		uint64_t val;
		if (y->st_shndx == SHN_ABS) {
			shndx = SHN_ABS;
			val = y->st_value;
		} else {
			uint32_t sec = o->isec0 + y->st_shndx;
			shndx = sym_shndx_isec(sec);
			val = isec_va(sec, y->st_value);
		}
		memcpy(strtab_buf + so, o->str + y->st_name, l + 1);
		d->st_name = (uint32_t)so;
		so += l + 1;
		d->st_info = y->st_info;
		d->st_other = y->st_other;
		d->st_shndx = shndx;
		d->st_value = val;
		d->st_size = y->st_size;
		d++;
	}
}

static void write_globals(void *arg, size_t c)
{
	(void)arg;
	uint32_t b = c * 4096, e = b + 4096 < ngouts ? b + 4096 : ngouts;
	for (uint32_t i = b; i < e; i++)
		put_global((Sym *)symtab_buf + gouts[i].idx, gouts[i].str, gouts[i].g, gouts[i].local);
}

static bool g_in_symtab(uint32_t g)
{
	G *s = &gsyms[g];
	/* isUsedInRegularObj: referenced by an object or the script */
	return s->state != GS_UNDEF || kget(&s->first) != UINT64_MAX;
}
static bool g_local(uint32_t g)
{
	G *s = &gsyms[g];
	if (s->ver == 0 && s->state != GS_UNDEF && !s->demoted)	/* VERSION local: */
		return true;
	return s->vis == STV_HIDDEN || s->vis == STV_INTERNAL;
}

/*
 * lld's order: file locals in file order, each file's group followed by its
 * hidden (now local) globals; then groups of hidden globals whose file had
 * no locals, in order of appearance (every script assignment has its own
 * internal file); then thunk symbols; then the remaining globals in symbol
 * table insertion order.  Strings are appended in the order symbols were
 * added: file locals, all globals, thunk symbols.
 */
static void build_symtab(void)
{
	pfor(objs.n, count_locals, NULL);
	/* per-file hidden counts */
	uint32_t *hcount = alloc((objs.n + 1) * sizeof(uint32_t));
	uint32_t nglob = 0, nhid = 0;
	for (uint32_t i = 0; i < gorder.n; i++) {
		uint32_t g = gorder.v[i];
		if (!g_in_symtab(g))
			continue;
		nglob++;
		if (g_local(g)) {
			nhid++;
			G *s = &gsyms[g];
			if (s->state == GS_OBJ)
				hcount[s->obj]++;
		}
	}
	/* local indices */
	uint64_t idx = 1, str = 1;
	uint32_t *hnext = alloc((objs.n + 1) * sizeof(uint32_t));
	for (uint32_t i = 0; i < objs.n; i++) {
		Obj *o = objs.v[i];
		o->loc0 = idx;
		o->str0 = str;
		str += o->locstr;
		idx += o->nloc;
		hnext[i] = idx;
		if (o->nloc)
			idx += hcount[i];
	}
	/* hidden-only groups after all file groups */
	uint32_t *grp_next = alloc((objs.n + 1) * sizeof(uint32_t));
	for (uint32_t i = 0; i < objs.n; i++)
		grp_next[i] = NONE;
	gouts = alloc((size_t)nglob * sizeof(GOut));
	ngouts = 0;
	uint64_t gstr = str;
	/* first pass: place hidden symbols */
	for (uint32_t i = 0; i < gorder.n; i++) {
		uint32_t g = gorder.v[i];
		if (!g_in_symtab(g) || !g_local(g))
			continue;
		G *s = &gsyms[g];
		uint32_t at;
		if (s->state == GS_OBJ && objs.v[s->obj]->nloc) {
			at = hnext[s->obj]++;
		} else if (s->state == GS_OBJ) {
			if (grp_next[s->obj] == NONE) {
				grp_next[s->obj] = idx;
				idx += hcount[s->obj];
			}
			at = grp_next[s->obj]++;
		} else {
			at = idx++;
		}
		gouts[ngouts++] = (GOut){.g = g, .idx = at, .local = true};
	}
	/* thunk symbols */
	uint64_t thunk0 = idx;
	idx += 2 * (uint64_t)thunks.n;
	nlocal_out = idx;
	/* globals, and string offsets in insertion order */
	uint32_t h = 0;
	for (uint32_t i = 0; i < gorder.n; i++) {
		uint32_t g = gorder.v[i];
		if (!g_in_symtab(g))
			continue;
		uint64_t so = gstr;
		gstr += strlen(gname(g)) + 1;
		if (g_local(g)) {
			gouts[h++].str = so;	/* same order as the first pass */
			continue;
		}
		gouts[ngouts++] = (GOut){.g = g, .idx = idx++, .str = so};
	}
	nsymout = idx;
	uint64_t tstr = gstr;
	for (uint32_t t = 0; t < thunks.n; t++)
		tstr += strlen(thunks.v[t].name) + 1 + 3;
	strsize = tstr;
	ISec *st = &isecs.v[symtab_isec], *ss = &isecs.v[strtab_isec];
	st->size = nsymout * sizeof(Sym);
	ss->size = strsize;
	thunk_sym0 = thunk0;
	thunk_str0 = gstr;
	(void)nhid;
}

/* .symtab and .strtab are written in place in the output image */
static void fill_symtab(void)
{
	symtab_buf = out + osecs.v[isec_osec(symtab_isec)].offset + isecs.v[symtab_isec].off;
	strtab_buf = out + osecs.v[isec_osec(strtab_isec)].offset + isecs.v[strtab_isec].off;
	uint64_t so = thunk_str0;
	for (uint32_t t = 0; t < thunks.n; t++) {
		Thunk *th = &thunks.v[t];
		Sym *d = (Sym *)(void *)symtab_buf + thunk_sym0 + 2 * t;
		uint16_t shndx = sym_shndx_isec(th->tsec);
		put_sym(d, strtab_buf, &so, th->name, STB_LOCAL << 4 | STT_FUNC, 0, shndx, thunk_va(t), th->size);
		put_sym(d + 1, strtab_buf, &so, "$x", STB_LOCAL << 4 | STT_NOTYPE, 0, shndx, thunk_va(t), 0);
	}
	pfor(objs.n, write_locals, NULL);
	pfor((ngouts + 4095) / 4096, write_globals, NULL);
}
