/* The first half of the symbol table: which locals are kept, counting them, and writing a symbol record. */

/* ---------------- symbol table ---------------- */

static bool ld_local_symbol_is_kept(struct ld_context *ctx, const ld_object *o, const elf_symbol *y)
{
	uint8_t type = y->st_info & 15;
	if (type == STT_SECTION)
		return false;
	const char *n = o->str + y->st_name;
	if (ctx->options.discard_all)
		return false;
	if (y->st_shndx == SHN_UNDEF)
		return false;		/* not a Defined */
	if (y->st_shndx != SHN_ABS && y->st_shndx < 0xff00) {
		uint32_t s = o->isec0 + y->st_shndx;
		ld_input_section *is = &ctx->inputs.sections.v[s];
		uint32_t os = ld_input_section_output_section(ctx, s);
		if (is->kind == LD_SECTION_KIND_NONE || os == LD_NONE || os == LD_OUTPUT_SECTION_DISCARD)
			return false;
		if (n[0] == '.' && n[1] == 'L' && (ctx->options.discard_locals || (is->flags & SHF_MERGE)))
			return false;
	} else if (n[0] == '.' && n[1] == 'L' && ctx->options.discard_locals) {
		return false;
	}
	return true;
}

/* which locals go to .symtab, remembered (index, name length) for ld_local_symbols_write */
static void ld_local_symbols_count(void *arg, size_t i)
{
	struct ld_context *ctx = arg;
	static __thread uint32_t (*ld_local_symbols_scratch)[2];
	static __thread uint32_t ld_local_symbols_scratch_capacity;
	ld_object *o = ctx->inputs.objects.v[i];
	uint32_t n = 0, end = o->firstg < o->nsym ? o->firstg : o->nsym;
	uint64_t str = 0;
	if (o->buf && end > 1) {
		if (ld_local_symbols_scratch_capacity < end) {
			free(ld_local_symbols_scratch);
			ld_local_symbols_scratch_capacity = end < 4096 ? 4096 : end;
			ld_local_symbols_scratch = malloc((size_t)ld_local_symbols_scratch_capacity * sizeof *ld_local_symbols_scratch);
		}
		for (uint32_t s = 1; s < end; s++)
			if (ld_local_symbol_is_kept(ctx, o, &o->sym[s])) {
				uint32_t l = (uint32_t)strlen(o->str + o->sym[s].st_name);
				ld_local_symbols_scratch[n][0] = s;
				ld_local_symbols_scratch[n][1] = l;
				n++;
				str += l + 1;
			}
		o->kept = ld_region_alloc((size_t)n * sizeof *ld_local_symbols_scratch + 1);
		memcpy(o->kept, ld_local_symbols_scratch, (size_t)n * sizeof *ld_local_symbols_scratch);
	}
	o->nloc = n;
	o->locstr = str;
}

static uint16_t ld_symbol_section_index_of_input_section(struct ld_context *ctx, uint32_t sec)
{
	uint32_t os = ld_input_section_output_section(ctx, sec);
	if (os == LD_NONE || os == LD_OUTPUT_SECTION_DISCARD)
		return 0;
	return ctx->script.sections.v[os].shidx;
}

static void ld_symbol_record_store(elf_symbol *d, uint8_t *strbuf, uint64_t *stroff, const char *name, uint8_t info,
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
