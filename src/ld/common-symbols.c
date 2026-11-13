/* lld's replaceCommonSymbols: a NOBITS section per common symbol. */

#include "ld-internal.h"

/* lld replaceCommonSymbols: each common symbol one NOBITS "COMMON" section
 * (after every file's sections), of the largest size and alignment among its
 * commons, in the first file with that size; placed in the order of the files
 * that have commons, each file's globals in order, at the first mention */
VPRIV void ld_common_symbols_replace(struct ld_context *ctx)
{
	bool any = false;

	for (uint32_t g = 0; g < atomic_load(&ctx->symbols.count) && !any; g++)
		any = ctx->symbols.globals[g].common && ctx->symbols.globals[g].state == LD_SYMBOL_STATE_OBJECT;
	if (!any)
		return;
	for (uint32_t g = 0; g < atomic_load(&ctx->symbols.count); g++)
		if (ctx->symbols.globals[g].common && ctx->symbols.globals[g].state == LD_SYMBOL_STATE_OBJECT)
			ctx->symbols.globals[g].size = 0, ctx->symbols.globals[g].value = 1, ctx->symbols.globals[g].object = LD_NONE;
	for (uint32_t i = 0; i < ctx->inputs.real_object_count; i++) {
		ld_object *o = ctx->inputs.objects.v[i];

		for (uint32_t k = o->file_bytes ? o->first_global_index : 0; o->file_bytes && k < o->symbol_count; k++) {
			const elf_symbol *y = &o->symbols[k];
			ld_global_symbol *s = &ctx->symbols.globals[o->global_slots[k - o->first_global_index]];

			if (y->st_shndx != SHN_COMMON || !s->common || s->state != LD_SYMBOL_STATE_OBJECT)
				continue;
			if (y->st_value > s->value)
				s->value = y->st_value;	/* the alignment, until placed */
			if (s->object == LD_NONE || y->st_size > s->size)
				s->object = i, s->symbol_index = k, s->size = y->st_size;
		}
	}
	for (uint32_t i = 0; i < ctx->inputs.real_object_count; i++) {
		ld_object *o = ctx->inputs.objects.v[i];
		bool has = false;

		for (uint32_t k = o->file_bytes ? o->first_global_index : 0; o->file_bytes && k < o->symbol_count && !has; k++)
			has = o->symbols[k].st_shndx == SHN_COMMON;
		/* every global the file names, references too, in its order */
		for (uint32_t k = o->first_global_index; has && k < o->symbol_count; k++) {
			ld_global_symbol *s = &ctx->symbols.globals[o->global_slots[k - o->first_global_index]];

			if (!s->common || s->state != LD_SYMBOL_STATE_OBJECT || s->input_section != LD_NONE)
				continue;
			uint32_t is = ld_input_section_create(ctx, "COMMON", LD_SECTION_KIND_NORMAL, SHT_NOBITS, SHF_ALLOC | SHF_WRITE,
					       s->value ? (uint32_t)s->value : 1);

			ctx->inputs.sections.v[is].size = s->size;
			ctx->inputs.sections.v[is].object = s->object;
			s->input_section = is;
			s->value = 0;
		}
	}
}
