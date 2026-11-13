/* The second half of the symbol table: sizing, layout of globals and locals, and filling it in parallel. */

/* ---------------- symbol table ---------------- */

typedef struct { uint32_t g; uint32_t idx; uint64_t str; bool local; } ld_global_symbol_output;
static ld_global_symbol_output *gouts;
static uint32_t ngouts;
static uint8_t *symtab_buf, *strtab_buf;	/* in the output image (ld_symbol_table_fill) */
static uint64_t thunk_sym0, thunk_str0;		/* first thunk symbol and its string */
static uint64_t nsymout, strsize;
static uint32_t nlocal_out;

static void ld_global_symbol_record_store(elf_symbol *d, uint64_t stroff, uint32_t g, bool local)
{
	ld_global_symbol *s = &gsyms[g];
	uint8_t bind = local ? STB_LOCAL : s->bind;
	uint16_t shndx;
	uint64_t val, size = s->size;
	if (s->state == LD_SYMBOL_STATE_SCRIPT) {
		shndx = s->ev.kind == LD_VALUE_KIND_ABSOLUTE ? SHN_ABS : s->ev.kind == LD_VALUE_KIND_OUTPUT_SECTION ? ld_output_section_header_index(s->ev.idx)
										: ld_symbol_section_index_of_input_section(s->ev.idx);
		val = ld_global_symbol_address(g);
		size = 0;
	} else if (s->state == LD_SYMBOL_STATE_OBJECT && !s->demoted) {
		shndx = s->sec == LD_NONE ? SHN_ABS : ld_symbol_section_index_of_input_section(s->sec);
		val = ld_global_symbol_address(g);
	} else {
		shndx = SHN_UNDEF;
		val = 0;
		size = 0;
	}
	uint64_t so = stroff;
	ld_symbol_record_store(d, strtab_buf, &so, ld_global_name(g), (uint8_t)(bind << 4 | s->type), s->vis, shndx, val, size);
}

static void ld_local_symbols_write(void *arg, size_t i)
{
	(void)arg;
	ld_object *o = objs.v[i];
	if (!o->buf || !o->nloc)
		return;
	elf_symbol *d = (elf_symbol *)symtab_buf + o->loc0;
	uint64_t so = o->str0;
	for (uint32_t k = 0; k < o->nloc; k++) {
		const elf_symbol *y = &o->sym[o->kept[k][0]];
		uint32_t l = o->kept[k][1];
		uint16_t shndx;
		uint64_t val;
		if (y->st_shndx == SHN_ABS) {
			shndx = SHN_ABS;
			val = y->st_value;
		} else {
			uint32_t sec = o->isec0 + y->st_shndx;
			shndx = ld_symbol_section_index_of_input_section(sec);
			val = ld_input_section_address(sec, y->st_value);
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

static void ld_global_symbols_write(void *arg, size_t c)
{
	(void)arg;
	uint32_t b = c * 4096, e = b + 4096 < ngouts ? b + 4096 : ngouts;
	for (uint32_t i = b; i < e; i++)
		ld_global_symbol_record_store((elf_symbol *)symtab_buf + gouts[i].idx, gouts[i].str, gouts[i].g, gouts[i].local);
}

static bool ld_global_symbol_is_in_symbol_table(uint32_t g)
{
	ld_global_symbol *s = &gsyms[g];
	/* isUsedInRegularObj: referenced by an object or the script */
	return s->state != LD_SYMBOL_STATE_UNDEFINED || ld_key_load(&s->first) != UINT64_MAX;
}
static bool ld_global_symbol_is_local(uint32_t g)
{
	ld_global_symbol *s = &gsyms[g];
	if (s->ver == 0 && s->state != LD_SYMBOL_STATE_UNDEFINED && !s->demoted)	/* VERSION local: */
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
static void ld_symbol_table_build(void)
{
	ld_parallel_for(objs.n, ld_local_symbols_count, NULL);
	/* per-file hidden counts */
	uint32_t *hcount = ld_region_alloc((objs.n + 1) * sizeof(uint32_t));
	uint32_t nglob = 0, nhid = 0;
	for (uint32_t i = 0; i < gorder.n; i++) {
		uint32_t g = gorder.v[i];
		if (!ld_global_symbol_is_in_symbol_table(g))
			continue;
		nglob++;
		if (ld_global_symbol_is_local(g)) {
			nhid++;
			ld_global_symbol *s = &gsyms[g];
			if (s->state == LD_SYMBOL_STATE_OBJECT)
				hcount[s->obj]++;
		}
	}
	/* local indices */
	uint64_t idx = 1, str = 1;
	uint32_t *hnext = ld_region_alloc((objs.n + 1) * sizeof(uint32_t));
	for (uint32_t i = 0; i < objs.n; i++) {
		ld_object *o = objs.v[i];
		o->loc0 = idx;
		o->str0 = str;
		str += o->locstr;
		idx += o->nloc;
		hnext[i] = idx;
		if (o->nloc)
			idx += hcount[i];
	}
	/* hidden-only groups after all file groups */
	uint32_t *grp_next = ld_region_alloc((objs.n + 1) * sizeof(uint32_t));
	for (uint32_t i = 0; i < objs.n; i++)
		grp_next[i] = LD_NONE;
	gouts = ld_region_alloc((size_t)nglob * sizeof(ld_global_symbol_output));
	ngouts = 0;
	uint64_t gstr = str;
	/* first pass: place hidden symbols */
	for (uint32_t i = 0; i < gorder.n; i++) {
		uint32_t g = gorder.v[i];
		if (!ld_global_symbol_is_in_symbol_table(g) || !ld_global_symbol_is_local(g))
			continue;
		ld_global_symbol *s = &gsyms[g];
		uint32_t at;
		if (s->state == LD_SYMBOL_STATE_OBJECT && objs.v[s->obj]->nloc) {
			at = hnext[s->obj]++;
		} else if (s->state == LD_SYMBOL_STATE_OBJECT) {
			if (grp_next[s->obj] == LD_NONE) {
				grp_next[s->obj] = idx;
				idx += hcount[s->obj];
			}
			at = grp_next[s->obj]++;
		} else {
			at = idx++;
		}
		gouts[ngouts++] = (ld_global_symbol_output){.g = g, .idx = at, .local = true};
	}
	/* thunk symbols */
	uint64_t thunk0 = idx;
	idx += 2 * (uint64_t)thunks.n;
	nlocal_out = idx;
	/* globals, and string offsets in insertion order */
	uint32_t h = 0;
	for (uint32_t i = 0; i < gorder.n; i++) {
		uint32_t g = gorder.v[i];
		if (!ld_global_symbol_is_in_symbol_table(g))
			continue;
		uint64_t so = gstr;
		gstr += strlen(ld_global_name(g)) + 1;
		if (ld_global_symbol_is_local(g)) {
			gouts[h++].str = so;	/* same order as the first pass */
			continue;
		}
		gouts[ngouts++] = (ld_global_symbol_output){.g = g, .idx = idx++, .str = so};
	}
	nsymout = idx;
	uint64_t tstr = gstr;
	for (uint32_t t = 0; t < thunks.n; t++)
		tstr += strlen(thunks.v[t].name) + 1 + 3;
	strsize = tstr;
	ld_input_section *st = &isecs.v[symtab_isec], *ss = &isecs.v[strtab_isec];
	st->size = nsymout * sizeof(elf_symbol);
	ss->size = strsize;
	thunk_sym0 = thunk0;
	thunk_str0 = gstr;
	(void)nhid;
}

/* .symtab and .strtab are written in place in the output image */
static void ld_symbol_table_fill(void)
{
	symtab_buf = out + osecs.v[ld_input_section_output_section(symtab_isec)].offset + isecs.v[symtab_isec].off;
	strtab_buf = out + osecs.v[ld_input_section_output_section(strtab_isec)].offset + isecs.v[strtab_isec].off;
	uint64_t so = thunk_str0;
	for (uint32_t t = 0; t < thunks.n; t++) {
		a64_ld_thunk *th = &thunks.v[t];
		elf_symbol *d = (elf_symbol *)(void *)symtab_buf + thunk_sym0 + 2 * t;
		uint16_t shndx = ld_symbol_section_index_of_input_section(th->tsec);
		ld_symbol_record_store(d, strtab_buf, &so, th->name, STB_LOCAL << 4 | STT_FUNC, 0, shndx, a64_ld_thunk_address(t), th->size);
		ld_symbol_record_store(d + 1, strtab_buf, &so, "$x", STB_LOCAL << 4 | STT_NOTYPE, 0, shndx, a64_ld_thunk_address(t), 0);
	}
	ld_parallel_for(objs.n, ld_local_symbols_write, NULL);
	ld_parallel_for((ngouts + 4095) / 4096, ld_global_symbols_write, NULL);
}
