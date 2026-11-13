/* The second half of the symbol table: sizing, layout of globals and locals, and filling it in parallel. */

/* ---------------- symbol table ---------------- */


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
	ld_symbol_record_store(d, ctx->symbol_table.strtab, &so, ld_global_name(g), (uint8_t)(bind << 4 | s->type), s->vis, shndx, val, size);
}

static void ld_local_symbols_write(void *arg, size_t i)
{
	(void)arg;
	ld_object *o = objs.v[i];
	if (!o->buf || !o->nloc)
		return;
	elf_symbol *d = (elf_symbol *)ctx->symbol_table.symtab + o->loc0;
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
		memcpy(ctx->symbol_table.strtab + so, o->str + y->st_name, l + 1);
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
	uint32_t b = c * 4096, e = b + 4096 < ctx->symbol_table.global_count ? b + 4096 : ctx->symbol_table.global_count;
	for (uint32_t i = b; i < e; i++)
		ld_global_symbol_record_store((elf_symbol *)ctx->symbol_table.symtab + ctx->symbol_table.globals[i].idx, ctx->symbol_table.globals[i].str, ctx->symbol_table.globals[i].g, ctx->symbol_table.globals[i].local);
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
	ctx->symbol_table.globals = ld_region_alloc((size_t)nglob * sizeof(ld_global_symbol_output));
	ctx->symbol_table.global_count = 0;
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
		ctx->symbol_table.globals[ctx->symbol_table.global_count++] = (ld_global_symbol_output){.g = g, .idx = at, .local = true};
	}
	/* thunk symbols */
	uint64_t thunk0 = idx;
	idx += 2 * (uint64_t)ctx->relocations.thunks.n;
	ctx->symbol_table.local_count = idx;
	/* globals, and string offsets in insertion order */
	uint32_t h = 0;
	for (uint32_t i = 0; i < gorder.n; i++) {
		uint32_t g = gorder.v[i];
		if (!ld_global_symbol_is_in_symbol_table(g))
			continue;
		uint64_t so = gstr;
		gstr += strlen(ld_global_name(g)) + 1;
		if (ld_global_symbol_is_local(g)) {
			ctx->symbol_table.globals[h++].str = so;	/* same order as the first pass */
			continue;
		}
		ctx->symbol_table.globals[ctx->symbol_table.global_count++] = (ld_global_symbol_output){.g = g, .idx = idx++, .str = so};
	}
	ctx->symbol_table.symbol_count = idx;
	uint64_t tstr = gstr;
	for (uint32_t t = 0; t < ctx->relocations.thunks.n; t++)
		tstr += strlen(ctx->relocations.thunks.v[t].name) + 1 + 3;
	ctx->symbol_table.string_size = tstr;
	ld_input_section *st = &isecs.v[ctx->output.symtab_section], *ss = &isecs.v[ctx->output.strtab_section];
	st->size = ctx->symbol_table.symbol_count * sizeof(elf_symbol);
	ss->size = ctx->symbol_table.string_size;
	ctx->symbol_table.first_thunk_symbol = thunk0;
	ctx->symbol_table.first_thunk_string = gstr;
	(void)nhid;
}

/* .symtab and .strtab are written in place in the output image */
static void ld_symbol_table_fill(void)
{
	ctx->symbol_table.symtab = ctx->output.image + osecs.v[ld_input_section_output_section(ctx->output.symtab_section)].offset + isecs.v[ctx->output.symtab_section].off;
	ctx->symbol_table.strtab = ctx->output.image + osecs.v[ld_input_section_output_section(ctx->output.strtab_section)].offset + isecs.v[ctx->output.strtab_section].off;
	uint64_t so = ctx->symbol_table.first_thunk_string;
	for (uint32_t t = 0; t < ctx->relocations.thunks.n; t++) {
		a64_ld_thunk *th = &ctx->relocations.thunks.v[t];
		elf_symbol *d = (elf_symbol *)(void *)ctx->symbol_table.symtab + ctx->symbol_table.first_thunk_symbol + 2 * t;
		uint16_t shndx = ld_symbol_section_index_of_input_section(th->tsec);
		ld_symbol_record_store(d, ctx->symbol_table.strtab, &so, th->name, STB_LOCAL << 4 | STT_FUNC, 0, shndx, a64_ld_thunk_address(t), th->size);
		ld_symbol_record_store(d + 1, ctx->symbol_table.strtab, &so, "$x", STB_LOCAL << 4 | STT_NOTYPE, 0, shndx, a64_ld_thunk_address(t), 0);
	}
	ld_parallel_for(objs.n, ld_local_symbols_write, NULL);
	ld_parallel_for((ctx->symbol_table.global_count + 4095) / 4096, ld_global_symbols_write, NULL);
}
