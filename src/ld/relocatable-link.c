/* ld -r: relocatable output. */

#include "ld-internal.h"

/* ================= ld -r ================= */

/*
 * Relocatable output (vmlinux.o for modpost).  Input sections are grouped
 * by name into output sections in order of first appearance, contents are
 * copied unrelocated and every relocation is re-emitted: section-symbol
 * references move to the output section's symbol with the input section's
 * offset folded into the addend.  SHF_MERGE sections are concatenated, not
 * deduplicated, and .llvm_addrsig is dropped (its symbol indices would be
 * stale).  Byte identity with lld is not a goal here; modpost's view is.
 */

static uint32_t ld_relocatable_section_find(struct ld_context *ctx, const char *name)
{
	uint32_t h = ld_name_hash(name, strlen(name));
	uint32_t j = h & ctx->relocatable.section_map_mask;
	while (ctx->relocatable.section_map[j] != LD_NONE) {
		ld_relocatable_section *r = &ctx->relocatable.sections.v[ctx->relocatable.section_map[j]];
		if (r->hash == h && !strcmp(r->name, name))
			return ctx->relocatable.section_map[j];
		j = (j + 1) & ctx->relocatable.section_map_mask;
	}
	ld_relocatable_section r = {.name = name, .hash = h, .alignment = 1};
	VVEC_PUSH(ctx->relocatable.sections, r, ld_region_alloc);
	ctx->relocatable.section_map[j] = ctx->relocatable.sections.n - 1;
	return ctx->relocatable.sections.n - 1;
}

/* an output section of its own (groups and their members, as lld -r) */
static uint32_t ld_relocatable_section_create(struct ld_context *ctx, const char *name)
{
	ld_relocatable_section r = {.name = name, .hash = ld_name_hash(name, strlen(name)), .alignment = 1};
	VVEC_PUSH(ctx->relocatable.sections, r, ld_region_alloc);
	return ctx->relocatable.sections.n - 1;
}

static void ld_relocatable_locals_count(void *arg, size_t i)
{
	struct ld_context *ctx = arg;
	ld_object *o = ctx->inputs.objects.v[i];
	uint32_t n = 0;
	uint64_t str = 0;
	if (o->file_bytes)
		for (uint32_t s = 1; s < o->first_global_index && s < o->symbol_count; s++) {
			const elf_symbol *y = &o->symbols[s];
			if ((y->st_info & 15) == STT_SECTION)
				continue;
			n++;
			str += strlen(o->string_table + y->st_name) + 1;
		}
	o->kept_local_count = n;
	o->local_string_bytes = str;
	ctx->relocatable.local_symbol_map[i] = o->first_global_index ? ld_region_alloc(o->first_global_index * sizeof(uint32_t)) : NULL;
}

static void ld_relocatable_locals_write(void *arg, size_t i)
{
	struct ld_context *ctx = arg;
	ld_object *o = ctx->inputs.objects.v[i];
	if (!o->file_bytes)
		return;
	elf_symbol *d = (elf_symbol *)(ctx->relocatable.image + ctx->relocatable.symtab_offset) + ctx->relocatable.local_first_index[i];
	uint32_t idx = ctx->relocatable.local_first_index[i];
	uint64_t so = ctx->relocatable.local_string_offsets[i];
	uint8_t *strbuf = ctx->relocatable.image + ctx->relocatable.strtab_offset;
	for (uint32_t s = 1; s < o->first_global_index && s < o->symbol_count; s++) {
		const elf_symbol *y = &o->symbols[s];
		if ((y->st_info & 15) == STT_SECTION) {
			uint32_t sec = o->first_input_section + y->st_shndx;
			ctx->relocatable.local_symbol_map[i][s] = y->st_shndx < o->section_header_count && ctx->inputs.sections.v[sec].kind != LD_SECTION_KIND_NONE ?
				     ctx->relocatable.sections.v[ctx->inputs.sections.v[sec].output_section].section_symbol : 0;
			continue;
		}
		uint16_t shndx = y->st_shndx;
		uint64_t val = y->st_value;
		if (shndx != SHN_UNDEF && shndx < 0xff00) {
			ld_input_section *is = &ctx->inputs.sections.v[o->first_input_section + shndx];
			if (is->kind == LD_SECTION_KIND_NONE) {
				shndx = SHN_UNDEF;
				val = 0;
			} else {
				shndx = ctx->relocatable.sections.v[is->output_section].section_header_index;
				val += is->offset;
			}
		}
		ctx->relocatable.local_symbol_map[i][s] = idx++;
		ld_symbol_record_store(d++, strbuf, &so, o->string_table + y->st_name, y->st_info, y->st_other, shndx, val, y->st_size);
	}
}

static void ld_relocatable_globals_write(void *arg, size_t c)
{
	struct ld_context *ctx = arg;
	uint32_t b = c * 4096, e = b + 4096 < ctx->symbols.order.n ? b + 4096 : ctx->symbols.order.n;
	uint8_t *strbuf = ctx->relocatable.image + ctx->relocatable.strtab_offset;
	for (uint32_t i = b; i < e; i++) {
		uint32_t g = ctx->symbols.order.v[i];
		ld_global_symbol *s = &ctx->symbols.globals[g];
		elf_symbol *d = (elf_symbol *)(ctx->relocatable.image + ctx->relocatable.symtab_offset) + s->output_index;
		uint64_t so = s->value;		/* string offset parked here, see below */
		uint16_t shndx = SHN_UNDEF;
		uint64_t val = 0, size = 0;
		if (s->state == LD_SYMBOL_STATE_OBJECT) {
			size = s->size;
			if (s->input_section == LD_NONE) {
				shndx = SHN_ABS;
				val = ctx->inputs.objects.v[s->object]->symbols[s->symbol_index].st_value;
			} else if (ctx->inputs.sections.v[s->input_section].kind == LD_SECTION_KIND_NONE) {
				size = 0;	/* in a dropped group member: undefined (lld) */
			} else {
				ld_input_section *is = &ctx->inputs.sections.v[s->input_section];
				shndx = ctx->relocatable.sections.v[is->output_section].section_header_index;
				val = is->offset + ctx->inputs.objects.v[s->object]->symbols[s->symbol_index].st_value;
			}
		}
		ld_symbol_record_store(d, strbuf, &so, ld_global_name(ctx, g), (uint8_t)(s->binding << 4 | s->type), s->visibility, shndx, val, size);
	}
}

static void ld_relocatable_input_section_write(void *arg, size_t j)
{
	struct ld_context *ctx = arg;
	uint32_t si = ctx->output.write_jobs.v[j];
	ld_input_section *is = &ctx->inputs.sections.v[si];
	ld_relocatable_section *r = &ctx->relocatable.sections.v[is->output_section];
	if (is->type != SHT_NOBITS && r->type != SHT_NOBITS && is->size)
		memcpy(ctx->relocatable.image + r->offset + is->offset, is->data, is->size);
	if (!is->rela_count)
		return;
	ld_object *o = ctx->inputs.objects.v[is->object];
	elf_rela *d = (elf_rela *)(ctx->relocatable.image + r->rela_offset) + is->first_dynamic_relocation;
	for (uint32_t k = 0; k < is->rela_count; k++) {
		const elf_rela *rl = &is->rela[k];
		uint32_t symi = rl->r_info >> 32, type = (uint32_t)rl->r_info;
		int64_t addend = rl->r_addend;
		uint32_t ns;
		if (symi == 0) {
			ns = 0;
		} else if (symi >= o->first_global_index) {
			ns = ctx->symbols.globals[o->global_slots[symi - o->first_global_index]].output_index;
		} else {
			const elf_symbol *y = &o->symbols[symi];
			ns = ctx->relocatable.local_symbol_map[is->object][symi];
			if ((y->st_info & 15) == STT_SECTION && y->st_shndx < o->section_header_count) {
				ld_input_section *t = &ctx->inputs.sections.v[o->first_input_section + y->st_shndx];
				if (t->kind == LD_SECTION_KIND_NONE)
					ld_error_report(ctx, "%s:(%s): relocation against discarded section %s", o->path, is->name, t->name);
				else
					addend += t->offset;
			}
		}
		d[k].r_offset = rl->r_offset + is->offset;
		d[k].r_info = (uint64_t)ns << 32 | type;
		d[k].r_addend = addend;
	}
}

VPRIV int ld_relocatable_link(struct ld_context *ctx)
{
	/* global order and final binding */
	ld_global_order_build(ctx);
	ld_parallel_for(ctx->symbols.order.n, ld_global_symbol_finish_task, ctx);

	/* output sections by name, first appearance */
	uint32_t cap = 1024;
	while (cap < ctx->inputs.sections.n)
		cap *= 2;
	ctx->relocatable.section_map = ld_region_alloc(cap * sizeof(uint32_t));
	memset(ctx->relocatable.section_map, 0xff, cap * sizeof(uint32_t));
	ctx->relocatable.section_map_mask = cap - 1;
	for (uint32_t i = 0; i < ctx->inputs.sections.n; i++) {
		ld_input_section *is = &ctx->inputs.sections.v[i];
		const ld_object *io = is->object != LD_NONE ? ctx->inputs.objects.v[is->object] : NULL;
		const elf_section_header *ih = io && is->section_header_index < io->section_header_count ? &io->section_headers[is->section_header_index] : NULL;

		if (ih && ih->sh_type == SHT_GROUP) {
			/* a group whose members are kept (a later COMDAT copy is dropped) */
			const uint32_t *w = (const uint32_t *)(io->file_bytes + ih->sh_offset);
			uint32_t nw = (uint32_t)(ih->sh_size / 4);

			if (nw < 2 || ctx->inputs.sections.v[io->first_input_section + w[1]].dropped)
				continue;
			uint32_t gi = ld_relocatable_section_create(ctx, ".group");
			ld_relocatable_section *g = &ctx->relocatable.sections.v[gi];

			g->type = SHT_GROUP;
			g->alignment = 4;
			g->entry_size = 4;
			g->group = i + 1;
			g->size = 4 * (uint64_t)nw;
			is->output_section = gi;
			continue;
		}
		if (is->kind != LD_SECTION_KIND_NORMAL)
			continue;
		bool member = ih && (ih->sh_flags & SHF_GROUP);
		uint32_t ri = member ? ld_relocatable_section_create(ctx, is->name) : ld_relocatable_section_find(ctx, is->name);
		ld_relocatable_section *r = &ctx->relocatable.sections.v[ri];

		if (member) {
			r->is_group_member = true;
			is->flags |= SHF_GROUP;
		}
		if (!r->sections.n) {
			r->type = is->type;
			r->flags = is->flags;
			r->entry_size = is->entry_size;
		} else {
			if (r->type != is->type && !(r->type == SHT_NOBITS && is->type == SHT_NOBITS))
				r->type = SHT_PROGBITS;
			if (r->entry_size != is->entry_size)
				r->entry_size = 0;
			r->flags |= is->flags;
		}
		if (is->alignment > r->alignment)
			r->alignment = is->alignment;
		is->output_section = ri;
		r->size = ld_value_align_up(r->size, is->alignment);
		is->offset = r->size;
		r->size += is->size;
		is->first_dynamic_relocation = r->rela_count;
		r->rela_count += is->rela_count;
		VVEC_PUSH(r->sections, i, ld_region_alloc);
	}
	ld_phase_time_report(ctx, "-r: output sections");

	/* section header indices and names */
	uint32_t sh = 1;
	uint64_t shs = 1;
	for (uint32_t i = 0; i < ctx->relocatable.sections.n; i++) {
		ld_relocatable_section *r = &ctx->relocatable.sections.v[i];
		r->section_header_index = sh++;
		r->name_offset = shs;
		shs += strlen(r->name) + 1;
		if (r->rela_count) {
			r->rela_section_header_index = sh++;
			r->rela_name_offset = shs;
			shs += strlen(r->name) + 6;
		}
	}
	if (sh > 0xff00) {
		ld_error_report(ctx, "-r: too many output sections (%u)", sh);
		return 1;
	}
	ctx->relocatable.symtab_index = sh++;
	ctx->relocatable.strtab_index = sh++;
	ctx->relocatable.shstrtab_index = sh++;
	ctx->relocatable.section_header_count = sh;
	uint64_t shs_symtab = shs; shs += 8;
	uint64_t shs_strtab = shs; shs += 8;
	uint64_t shs_shstrtab = shs; shs += 10;
	ctx->relocatable.shstrtab_size = shs;

	/* symbols: null, section symbols, locals per file, globals */
	ctx->relocatable.local_symbol_map = ld_region_alloc(ctx->inputs.objects.n * sizeof(uint32_t *));
	ld_parallel_for(ctx->inputs.objects.n, ld_relocatable_locals_count, ctx);
	ctx->relocatable.local_first_index = ld_region_alloc(ctx->inputs.objects.n * sizeof(uint64_t));
	ctx->relocatable.local_string_offsets = ld_region_alloc(ctx->inputs.objects.n * sizeof(uint64_t));
	uint64_t idx = 1 + ctx->relocatable.sections.n, str = 1;
	for (uint32_t i = 0; i < ctx->relocatable.sections.n; i++)
		ctx->relocatable.sections.v[i].section_symbol = 1 + i;
	for (uint32_t i = 0; i < ctx->inputs.objects.n; i++) {
		ctx->relocatable.local_first_index[i] = idx;
		ctx->relocatable.local_string_offsets[i] = str;
		idx += ctx->inputs.objects.v[i]->kept_local_count;
		str += ctx->inputs.objects.v[i]->local_string_bytes;
	}
	ctx->relocatable.local_count = idx;
	for (uint32_t i = 0; i < ctx->symbols.order.n; i++) {
		ld_global_symbol *s = &ctx->symbols.globals[ctx->symbols.order.v[i]];
		s->output_index = idx++;
		/* s->value is unused for -r output: park the string offset there */
		s->value = str;
		str += strlen(ld_global_name(ctx, ctx->symbols.order.v[i])) + 1;
	}
	ctx->relocatable.symbol_count = idx;
	uint64_t strsz = str;

	/* file layout */
	uint64_t off = sizeof(elf_file_header);
	for (uint32_t i = 0; i < ctx->relocatable.sections.n; i++) {
		ld_relocatable_section *r = &ctx->relocatable.sections.v[i];
		off = ld_value_align_up(off, r->alignment);
		r->offset = off;
		if (r->type != SHT_NOBITS)
			off += r->size;
	}
	off = ld_value_align_up(off, 8);
	for (uint32_t i = 0; i < ctx->relocatable.sections.n; i++) {
		ld_relocatable_section *r = &ctx->relocatable.sections.v[i];
		if (!r->rela_count)
			continue;
		r->rela_offset = off;
		off += r->rela_count * sizeof(elf_rela);
	}
	ctx->relocatable.symtab_offset = off;
	off += ctx->relocatable.symbol_count * sizeof(elf_symbol);
	ctx->relocatable.strtab_offset = off;
	off += strsz;
	ctx->relocatable.shstrtab_offset = off;
	off += ctx->relocatable.shstrtab_size;
	uint64_t shoff_r = ld_value_align_up(off, 8);
	uint64_t fsize = shoff_r + (uint64_t)ctx->relocatable.section_header_count * sizeof(elf_section_header);
	ctx->relocatable.image = ld_region_large_alloc(fsize);
	ld_phase_time_report(ctx, "-r: layout");

	/* contents and relocations */
	ctx->output.write_jobs.n = 0;
	for (uint32_t i = 0; i < ctx->relocatable.sections.n; i++)
		for (uint32_t k = 0; k < ctx->relocatable.sections.v[i].sections.n; k++)
			VVEC_PUSH(ctx->output.write_jobs, ctx->relocatable.sections.v[i].sections.v[k], ld_region_alloc);
	/* local maps must exist before relocations are rewritten */
	ld_parallel_for(ctx->inputs.objects.n, ld_relocatable_locals_write, ctx);
	ld_parallel_for(ctx->output.write_jobs.n, ld_relocatable_input_section_write, ctx);
	ld_parallel_for((ctx->symbols.order.n + 4095) / 4096, ld_relocatable_globals_write, ctx);
	/* groups: the flag word, then the members' output indices (a .rela member: its section's) */
	for (uint32_t i = 0; i < ctx->relocatable.sections.n; i++) {
		ld_relocatable_section *g = &ctx->relocatable.sections.v[i];

		if (!g->group)
			continue;
		const ld_input_section *gs = &ctx->inputs.sections.v[g->group - 1];
		const ld_object *o = ctx->inputs.objects.v[gs->object];
		const elf_section_header *h = &o->section_headers[gs->section_header_index];
		const uint32_t *w = (const uint32_t *)(o->file_bytes + h->sh_offset);
		uint32_t *d = (uint32_t *)(void *)(ctx->relocatable.image + g->offset), n = 0;

		d[n++] = w[0];
		for (uint64_t k = 1; k < h->sh_size / 4; k++) {
			const elf_section_header *m = &o->section_headers[w[k]];
			const ld_input_section *mi = &ctx->inputs.sections.v[o->first_input_section + (m->sh_type == SHT_RELA ? m->sh_info : w[k])];

			if (mi->kind == LD_SECTION_KIND_NONE || mi->output_section == LD_NONE)
				continue;
			uint32_t x = m->sh_type == SHT_RELA ? ctx->relocatable.sections.v[mi->output_section].rela_section_header_index : ctx->relocatable.sections.v[mi->output_section].section_header_index;

			if (x)
				d[n++] = x;
		}
		g->size = 4 * (uint64_t)n;
	}
	{
		elf_symbol *sy = (elf_symbol *)(ctx->relocatable.image + ctx->relocatable.symtab_offset);
		for (uint32_t i = 0; i < ctx->relocatable.sections.n; i++)
			sy[1 + i] = (elf_symbol){.st_info = STB_LOCAL << 4 | STT_SECTION, .st_shndx = ctx->relocatable.sections.v[i].section_header_index};
	}
	ld_phase_time_report(ctx, "-r: copy, relocations, symbols");

	/* headers */
	char *shstr = (char *)ctx->relocatable.image + ctx->relocatable.shstrtab_offset;
	elf_section_header *shd = (elf_section_header *)(ctx->relocatable.image + shoff_r);
	for (uint32_t i = 0; i < ctx->relocatable.sections.n; i++) {
		ld_relocatable_section *r = &ctx->relocatable.sections.v[i];
		strcpy(shstr + r->name_offset, r->name);
		shd[r->section_header_index] = (elf_section_header){.sh_name = r->name_offset, .sh_type = r->type, .sh_flags = r->flags,
				       .sh_offset = r->offset, .sh_size = r->size, .sh_addralign = r->alignment,
				       .sh_entsize = r->entry_size};
		if (r->group) {	/* sh_link the symbol table, sh_info the signature */
			const ld_input_section *gs = &ctx->inputs.sections.v[r->group - 1];
			const ld_object *o = ctx->inputs.objects.v[gs->object];
			uint32_t y = o->section_headers[gs->section_header_index].sh_info;

			shd[r->section_header_index].sh_size = r->size;
			shd[r->section_header_index].sh_link = ctx->relocatable.symtab_index;
			shd[r->section_header_index].sh_info = y >= o->first_global_index ? ctx->symbols.globals[o->global_slots[y - o->first_global_index]].output_index : ctx->relocatable.local_symbol_map[gs->object][y];
		}
		if (r->rela_count) {
			sprintf(shstr + r->rela_name_offset, ".rela%s", r->name);
			shd[r->rela_section_header_index] = (elf_section_header){.sh_name = r->rela_name_offset, .sh_type = SHT_RELA,
						 .sh_flags = SHF_INFO_LINK | (r->is_group_member ? SHF_GROUP : 0),
						 .sh_offset = r->rela_offset, .sh_size = r->rela_count * sizeof(elf_rela),
						 .sh_link = ctx->relocatable.symtab_index, .sh_info = r->section_header_index, .sh_addralign = 8,
						 .sh_entsize = sizeof(elf_rela)};
		}
	}
	strcpy(shstr + shs_symtab, ".symtab");
	strcpy(shstr + shs_strtab, ".strtab");
	strcpy(shstr + shs_shstrtab, ".shstrtab");
	shd[ctx->relocatable.symtab_index] = (elf_section_header){.sh_name = shs_symtab, .sh_type = SHT_SYMTAB, .sh_offset = ctx->relocatable.symtab_offset,
			       .sh_size = ctx->relocatable.symbol_count * sizeof(elf_symbol), .sh_link = ctx->relocatable.strtab_index, .sh_info = ctx->relocatable.local_count,
			       .sh_addralign = 8, .sh_entsize = sizeof(elf_symbol)};
	shd[ctx->relocatable.strtab_index] = (elf_section_header){.sh_name = shs_strtab, .sh_type = SHT_STRTAB, .sh_offset = ctx->relocatable.strtab_offset,
			       .sh_size = strsz, .sh_addralign = 1};
	shd[ctx->relocatable.shstrtab_index] = (elf_section_header){.sh_name = shs_shstrtab, .sh_type = SHT_STRTAB, .sh_offset = ctx->relocatable.shstrtab_offset,
				 .sh_size = ctx->relocatable.shstrtab_size, .sh_addralign = 1};
	elf_file_header *eh = (elf_file_header *)ctx->relocatable.image;
	memcpy(eh->e_ident, "\177ELF\2\1\1", 7);
	eh->e_type = 1;
	eh->e_machine = ctx->inputs.x86 ? 62 : 183;
	eh->e_version = 1;
	eh->e_shoff = shoff_r;
	eh->e_ehsize = sizeof(elf_file_header);
	eh->e_shentsize = sizeof(elf_section_header);
	eh->e_shnum = ctx->relocatable.section_header_count;
	eh->e_shstrndx = ctx->relocatable.shstrtab_index;
	if (atomic_load(&ctx->diagnostics.error_count)) {
		munmap(ctx->relocatable.image, fsize);
		return 1;
	}
	ctx->output.image = ctx->relocatable.image;
	ctx->layout.file_size = fsize;
	int rc = ld_output_file_write(ctx);
	ld_phase_time_report(ctx, "-r: write");
	ld_region_unmap(ctx->relocatable.image, fsize);
	ctx->output.image = NULL;
	for (uint32_t i = 0; i < ctx->inputs.objects.n; i++)
		if (ctx->inputs.objects.v[i]->mapped && ctx->inputs.objects.v[i]->file_bytes)
			ld_region_unmap(ctx->inputs.objects.v[i]->file_bytes, ctx->inputs.objects.v[i]->size);
	ld_global_table_free(ctx);
	memset(&ctx->relocatable.sections, 0, sizeof ctx->relocatable.sections);
	return rc || atomic_load(&ctx->diagnostics.error_count) ? 1 : 0;
}
