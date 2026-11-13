/* The second half of the symbol table: sizing, layout of globals and locals, and filling it in parallel. */

#include "ld-internal.h"

/* ---------------- symbol table ---------------- */


static void ld_global_symbol_record_store(struct ld_context *ctx, elf_symbol *d, uint64_t stroff, uint32_t g, bool local)
{
	ld_global_symbol *s = &ctx->symbols.globals[g];
	uint8_t bind = local ? STB_LOCAL : s->binding;
	uint16_t shndx;
	uint64_t val, size = s->size;
	if (s->state == LD_SYMBOL_STATE_SCRIPT) {
		shndx = s->expression_value.kind == LD_VALUE_KIND_ABSOLUTE ? SHN_ABS : s->expression_value.kind == LD_VALUE_KIND_OUTPUT_SECTION ? ld_output_section_header_index(ctx, s->expression_value.index)
										: ld_symbol_section_index_of_input_section(ctx, s->expression_value.index);
		val = ld_global_symbol_address(ctx, g);
		size = 0;
	} else if (s->state == LD_SYMBOL_STATE_OBJECT && !s->demoted) {
		shndx = s->input_section == LD_NONE ? SHN_ABS : ld_symbol_section_index_of_input_section(ctx, s->input_section);
		val = ld_global_symbol_address(ctx, g);
	} else {
		shndx = SHN_UNDEF;
		val = 0;
		size = 0;
	}
	uint64_t so = stroff;
	ld_symbol_record_store(d, ctx->symbol_table.strtab, &so, ld_global_name(ctx, g), (uint8_t)(bind << 4 | s->type), s->visibility, shndx, val, size);
}

static void ld_local_symbols_write(void *arg, size_t i)
{
	struct ld_context *ctx = arg;
	ld_object *o = ctx->inputs.objects.v[i];
	if (!o->file_bytes || !o->kept_local_count)
		return;
	elf_symbol *d = (elf_symbol *)ctx->symbol_table.symtab + o->first_local_output_index;
	uint64_t so = o->string_offset;
	for (uint32_t k = 0; k < o->kept_local_count; k++) {
		const elf_symbol *y = &o->symbols[o->kept_locals[k][0]];
		uint32_t l = o->kept_locals[k][1];
		uint16_t shndx;
		uint64_t val;
		if (y->st_shndx == SHN_ABS) {
			shndx = SHN_ABS;
			val = y->st_value;
		} else {
			uint32_t sec = o->first_input_section + y->st_shndx;
			shndx = ld_symbol_section_index_of_input_section(ctx, sec);
			val = ld_input_section_address(ctx, sec, y->st_value);
		}
		memcpy(ctx->symbol_table.strtab + so, o->string_table + y->st_name, l + 1);
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
	struct ld_context *ctx = arg;
	uint32_t b = c * 4096, e = b + 4096 < ctx->symbol_table.global_count ? b + 4096 : ctx->symbol_table.global_count;
	for (uint32_t i = b; i < e; i++)
		ld_global_symbol_record_store(ctx, (elf_symbol *)ctx->symbol_table.symtab + ctx->symbol_table.globals[i].index, ctx->symbol_table.globals[i].string_offset, ctx->symbol_table.globals[i].global_symbol, ctx->symbol_table.globals[i].is_local);
}

VPRIV bool ld_global_symbol_is_in_symbol_table(struct ld_context *ctx, uint32_t g)
{
	ld_global_symbol *s = &ctx->symbols.globals[g];
	/* isUsedInRegularObj: referenced by an object or the script */
	return s->state != LD_SYMBOL_STATE_UNDEFINED || ld_key_load(&s->first_occurrence) != UINT64_MAX;
}
VPRIV bool ld_global_symbol_is_local(struct ld_context *ctx, uint32_t g)
{
	ld_global_symbol *s = &ctx->symbols.globals[g];
	if (s->version == 0 && s->state != LD_SYMBOL_STATE_UNDEFINED && !s->demoted)	/* VERSION local: */
		return true;
	return s->visibility == STV_HIDDEN || s->visibility == STV_INTERNAL;
}

/*
 * lld's order: file locals in file order, each file's group followed by its
 * hidden (now local) globals; then groups of hidden globals whose file had
 * no locals, in order of appearance (every script assignment has its own
 * internal file); then thunk symbols; then the remaining globals in symbol
 * table insertion order.  Strings are appended in the order symbols were
 * added: file locals, all globals, thunk symbols.
 */
VPRIV void ld_symbol_table_build(struct ld_context *ctx)
{
	ld_parallel_for(ctx->inputs.objects.n, ld_local_symbols_count, ctx);
	/* per-file hidden counts */
	uint32_t *hcount = ld_region_alloc((ctx->inputs.objects.n + 1) * sizeof(uint32_t));
	uint32_t nglob = 0, nhid = 0;
	for (uint32_t i = 0; i < ctx->symbols.order.n; i++) {
		uint32_t g = ctx->symbols.order.v[i];
		if (!ld_global_symbol_is_in_symbol_table(ctx, g))
			continue;
		nglob++;
		if (ld_global_symbol_is_local(ctx, g)) {
			nhid++;
			ld_global_symbol *s = &ctx->symbols.globals[g];
			if (s->state == LD_SYMBOL_STATE_OBJECT)
				hcount[s->object]++;
		}
	}
	/* local indices */
	uint64_t idx = 1, str = 1;
	uint32_t *hnext = ld_region_alloc((ctx->inputs.objects.n + 1) * sizeof(uint32_t));
	for (uint32_t i = 0; i < ctx->inputs.objects.n; i++) {
		ld_object *o = ctx->inputs.objects.v[i];
		o->first_local_output_index = idx;
		o->string_offset = str;
		str += o->local_string_bytes;
		idx += o->kept_local_count;
		hnext[i] = idx;
		if (o->kept_local_count)
			idx += hcount[i];
	}
	/* hidden-only groups after all file groups */
	uint32_t *grp_next = ld_region_alloc((ctx->inputs.objects.n + 1) * sizeof(uint32_t));
	for (uint32_t i = 0; i < ctx->inputs.objects.n; i++)
		grp_next[i] = LD_NONE;
	ctx->symbol_table.globals = ld_region_alloc((size_t)nglob * sizeof(ld_global_symbol_output));
	ctx->symbol_table.global_count = 0;
	uint64_t gstr = str;
	/* first pass: place hidden symbols */
	for (uint32_t i = 0; i < ctx->symbols.order.n; i++) {
		uint32_t g = ctx->symbols.order.v[i];
		if (!ld_global_symbol_is_in_symbol_table(ctx, g) || !ld_global_symbol_is_local(ctx, g))
			continue;
		ld_global_symbol *s = &ctx->symbols.globals[g];
		uint32_t at;
		if (s->state == LD_SYMBOL_STATE_OBJECT && ctx->inputs.objects.v[s->object]->kept_local_count) {
			at = hnext[s->object]++;
		} else if (s->state == LD_SYMBOL_STATE_OBJECT) {
			if (grp_next[s->object] == LD_NONE) {
				grp_next[s->object] = idx;
				idx += hcount[s->object];
			}
			at = grp_next[s->object]++;
		} else {
			at = idx++;
		}
		ctx->symbol_table.globals[ctx->symbol_table.global_count++] = (ld_global_symbol_output){.global_symbol = g, .index = at, .is_local = true};
	}
	/* thunk symbols */
	uint64_t thunk0 = idx;
	idx += 2 * (uint64_t)ctx->relocations.thunks.n;
	ctx->symbol_table.local_count = idx;
	/* globals, and string offsets in insertion order */
	uint32_t h = 0;
	for (uint32_t i = 0; i < ctx->symbols.order.n; i++) {
		uint32_t g = ctx->symbols.order.v[i];
		if (!ld_global_symbol_is_in_symbol_table(ctx, g))
			continue;
		uint64_t so = gstr;
		gstr += strlen(ld_global_name(ctx, g)) + 1;
		if (ld_global_symbol_is_local(ctx, g)) {
			ctx->symbol_table.globals[h++].string_offset = so;	/* same order as the first pass */
			continue;
		}
		ctx->symbol_table.globals[ctx->symbol_table.global_count++] = (ld_global_symbol_output){.global_symbol = g, .index = idx++, .string_offset = so};
	}
	ctx->symbol_table.symbol_count = idx;
	uint64_t tstr = gstr;
	for (uint32_t t = 0; t < ctx->relocations.thunks.n; t++)
		tstr += strlen(ctx->relocations.thunks.v[t].name) + 1 + 3;
	ctx->symbol_table.string_size = tstr;
	ld_input_section *st = &ctx->inputs.sections.v[ctx->output.symtab_section], *ss = &ctx->inputs.sections.v[ctx->output.strtab_section];
	st->size = ctx->symbol_table.symbol_count * sizeof(elf_symbol);
	ss->size = ctx->symbol_table.string_size;
	ctx->symbol_table.first_thunk_symbol = thunk0;
	ctx->symbol_table.first_thunk_string = gstr;
	(void)nhid;
}

/* .symtab and .strtab are written in place in the output image */
VPRIV void ld_symbol_table_fill(struct ld_context *ctx)
{
	ctx->symbol_table.symtab = ctx->output.image + ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.symtab_section)].offset + ctx->inputs.sections.v[ctx->output.symtab_section].offset;
	ctx->symbol_table.strtab = ctx->output.image + ctx->script.sections.v[ld_input_section_output_section(ctx, ctx->output.strtab_section)].offset + ctx->inputs.sections.v[ctx->output.strtab_section].offset;
	uint64_t so = ctx->symbol_table.first_thunk_string;
	for (uint32_t t = 0; t < ctx->relocations.thunks.n; t++) {
		a64_ld_thunk *th = &ctx->relocations.thunks.v[t];
		elf_symbol *d = (elf_symbol *)(void *)ctx->symbol_table.symtab + ctx->symbol_table.first_thunk_symbol + 2 * t;
		uint16_t shndx = ld_symbol_section_index_of_input_section(ctx, th->thunk_section);
		ld_symbol_record_store(d, ctx->symbol_table.strtab, &so, th->name, STB_LOCAL << 4 | STT_FUNC, 0, shndx, a64_ld_thunk_address(ctx, t), th->size);
		ld_symbol_record_store(d + 1, ctx->symbol_table.strtab, &so, "$x", STB_LOCAL << 4 | STT_NOTYPE, 0, shndx, a64_ld_thunk_address(ctx, t), 0);
	}
	ld_parallel_for(ctx->inputs.objects.n, ld_local_symbols_write, ctx);
	ld_parallel_for((ctx->symbol_table.global_count + 4095) / 4096, ld_global_symbols_write, ctx);
}
