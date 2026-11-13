/* Lazy archive members: which are extracted and in which order, as lld. */

#include "ld-internal.h"

/* ---- lazy archives: which members, in which order (as lld) ----
 * lld reads the inputs in order; an object's undefined (non-weak) reference to
 * a symbol a lazy member defines extracts that member at once (depth first,
 * the member is added to the files before its own symbols are read), and a
 * lazy archive's member is extracted when one of its definitions meets an
 * undefined non-weak reference already seen.  Only links with a non-whole
 * archive run this sequential pass. */

static ld_lazy_symbol *ld_lazy_symbol_find(struct ld_context *ctx, const char *name)
{
	uint32_t len = strlen(name), h = ld_name_hash(name, len);

	if (!ctx->inputs.lazy_symbols.v || (ctx->inputs.lazy_symbols.n + 1) * 2 > ctx->inputs.lazy_symbols.mask) {
		uint32_t m = ctx->inputs.lazy_symbols.v ? (ctx->inputs.lazy_symbols.mask + 1) * 2 : 4096;
		ld_lazy_symbol *nv = calloc(m, sizeof(ld_lazy_symbol));

		for (uint32_t k = 0; ctx->inputs.lazy_symbols.v && k <= ctx->inputs.lazy_symbols.mask; k++)
			if (ctx->inputs.lazy_symbols.v[k].name) {
				uint32_t j = ctx->inputs.lazy_symbols.v[k].hash & (m - 1);

				while (nv[j].name)
					j = (j + 1) & (m - 1);
				nv[j] = ctx->inputs.lazy_symbols.v[k];
			}
		free(ctx->inputs.lazy_symbols.v);
		ctx->inputs.lazy_symbols.v = nv;
		ctx->inputs.lazy_symbols.mask = m - 1;
	}
	uint32_t j = h & ctx->inputs.lazy_symbols.mask;

	for (; ctx->inputs.lazy_symbols.v[j].name; j = (j + 1) & ctx->inputs.lazy_symbols.mask)
		if (ctx->inputs.lazy_symbols.v[j].hash == h && ctx->inputs.lazy_symbols.v[j].length == len && !memcmp(ctx->inputs.lazy_symbols.v[j].name, name, len))
			return &ctx->inputs.lazy_symbols.v[j];
	ctx->inputs.lazy_symbols.n++;
	ctx->inputs.lazy_symbols.v[j] = (ld_lazy_symbol){ .name = name, .length = len, .hash = h };
	return &ctx->inputs.lazy_symbols.v[j];
}

/* the global symbols of a pending object (mapped and aligned here; NULL: not
 * a valid object, left to ld_object_parse to report) */
static const elf_symbol *ld_lazy_object_globals(struct ld_context *ctx, ld_pending_member *p, uint32_t *n, const char **str)
{
	if (!p->file_bytes) {
		p->file_bytes = ld_file_map(ctx, p->path, &p->size);
		if (!p->file_bytes)
			return NULL;
	}
	if ((uintptr_t)p->file_bytes & 7) {
		uint8_t *c = ld_region_alloc(p->size);

		memcpy(c, p->file_bytes, p->size);
		p->file_bytes = c;
	}
	const elf_file_header *eh = (const elf_file_header *)p->file_bytes;

	if (p->size < sizeof(elf_file_header) || memcmp(eh->e_ident, "\177ELF\2\1", 6))
		return NULL;
	ld_object o = { .path = p->path, .file_bytes = p->file_bytes, .size = p->size };
	ctx->inputs.quiet = true;	/* ld_object_parse reports */
	bool ok = ld_object_is_valid(ctx, &o, eh);

	ctx->inputs.quiet = false;
	if (!ok)
		return NULL;
	for (uint32_t s = 0; s < o.section_header_count; s++)
		if (o.section_headers[s].sh_type == SHT_SYMTAB) {
			const elf_symbol *y = (const elf_symbol *)(o.file_bytes + o.section_headers[s].sh_offset);

			*n = o.section_headers[s].sh_size / sizeof(elf_symbol) - o.section_headers[s].sh_info;
			*str = (const char *)o.file_bytes + o.section_headers[o.section_headers[s].sh_link].sh_offset;
			return y + o.section_headers[s].sh_info;
		}
	*n = 0;
	return NULL;
}

static void ld_lazy_member_extract(struct ld_context *ctx, uint32_t c);

VPRIV void ld_lazy_object_add(struct ld_context *ctx, ld_pending_member p)
{
	uint32_t n = 0;
	const char *str;
	VVEC_PUSH(ctx->inputs.lazy_order, p, ld_region_alloc);
	ld_pending_member *q = &ctx->inputs.lazy_order.v[ctx->inputs.lazy_order.n - 1];
	const elf_symbol *y = ld_lazy_object_globals(ctx, q, &n, &str);

	for (uint32_t k = 0; y && k < n; k++) {
		ld_lazy_symbol *l = ld_lazy_symbol_find(ctx, str + y[k].st_name);
		bool weak = y[k].st_info >> 4 == STB_WEAK;

		if (y[k].st_shndx != SHN_UNDEF)
			l->state = LD_LAZY_SYMBOL_DEFINED;
		else if (l->state == LD_LAZY_SYMBOL_LAZY) {
			if (!weak)
				ld_lazy_member_extract(ctx, l->candidate);
		} else if (l->state == LD_LAZY_SYMBOL_NONE || l->state == LD_LAZY_SYMBOL_WEAK_REFERENCE)
			l->state = weak ? (l->state == LD_LAZY_SYMBOL_NONE ? LD_LAZY_SYMBOL_WEAK_REFERENCE : l->state) : LD_LAZY_SYMBOL_UNDEFINED;
	}
}

static void ld_lazy_member_extract(struct ld_context *ctx, uint32_t c)
{
	if (ctx->inputs.lazy_taken.v[c])
		return;
	ctx->inputs.lazy_taken.v[c] = 1;
	ld_lazy_object_add(ctx, ctx->inputs.lazy_candidates.v[c]);
}

VPRIV void ld_lazy_archive_process(struct ld_context *ctx, uint32_t c0, uint32_t c1)
{
	for (uint32_t c = c0; c < c1; c++) {
		uint32_t n = 0;
		const char *str;
		const elf_symbol *y = ld_lazy_object_globals(ctx, &ctx->inputs.lazy_candidates.v[c], &n, &str);

		for (uint32_t k = 0; y && k < n && !ctx->inputs.lazy_taken.v[c]; k++) {
			if (y[k].st_shndx == SHN_UNDEF)
				continue;
			ld_lazy_symbol *l = ld_lazy_symbol_find(ctx, str + y[k].st_name);

			if (l->state == LD_LAZY_SYMBOL_UNDEFINED)
				ld_lazy_member_extract(ctx, c);
			else if (l->state == LD_LAZY_SYMBOL_NONE || l->state == LD_LAZY_SYMBOL_WEAK_REFERENCE) {
				l->state = LD_LAZY_SYMBOL_LAZY;
				l->candidate = c;
			}
		}
	}
}
