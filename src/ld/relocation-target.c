/* Where a relocation points: the target symbol or section, merge-aware, before and after final addresses. */

#include "ld-internal.h"

/* ---------------- relocation targets ---------------- */


VPRIV void ld_relocation_target_find(struct ld_context *ctx, const ld_object *o, uint32_t symidx, int64_t addend, ld_relocation_target *t)
{
	t->is_undefined = t->discarded = false;
	if (symidx >= o->first_global_index) {
		uint32_t g = o->global_slots[symidx - o->first_global_index];
		ld_global_symbol *s = &ctx->symbols.globals[g];
		t->key = g;
		t->is_global = true;
		if (s->state == LD_SYMBOL_STATE_UNDEFINED || s->demoted) {
			t->is_undefined = true;
			t->is_absolute = true;
			t->symbol_address = 0;
			t->address = addend;
			return;
		}
		t->is_absolute = ld_global_symbol_is_absolute(ctx, g);
		t->symbol_address = ld_global_symbol_address(ctx, g);
		if (s->state == LD_SYMBOL_STATE_OBJECT && s->input_section != LD_NONE) {
			ld_input_section *is = &ctx->inputs.sections.v[s->input_section];
			if (is->kind == LD_SECTION_KIND_MERGE_INPUT) {
				t->address = ld_input_section_address(ctx, s->input_section, s->value + addend) ;
				/* non-section symbol: piece of value, then + addend */
				t->address = ld_input_section_address(ctx, s->input_section, s->value) + addend;
				return;
			}
		}
		t->address = t->symbol_address + addend;
		return;
	}
	const elf_symbol *y = &o->symbols[symidx];
	t->key = symidx;
	t->is_global = false;
	if (y->st_shndx == SHN_ABS) {
		t->is_absolute = true;
		t->symbol_address = y->st_value;
		t->address = t->symbol_address + addend;
		return;
	}
	if (y->st_shndx == SHN_UNDEF || y->st_shndx >= 0xff00) {
		t->is_undefined = true;
		t->is_absolute = true;
		t->symbol_address = 0;
		t->address = addend;
		return;
	}
	uint32_t sec = o->first_input_section + y->st_shndx;
	ld_input_section *is = &ctx->inputs.sections.v[sec];
	t->is_absolute = false;
	uint32_t os = ld_input_section_output_section(ctx, sec);
	if (is->kind == LD_SECTION_KIND_NONE || os == LD_OUTPUT_SECTION_DISCARD || os == LD_NONE) {
		t->discarded = true;
		t->symbol_address = 0;
		t->address = addend;
		return;
	}
	if ((y->st_info & 15) == STT_SECTION) {
		t->symbol_address = ld_input_section_address(ctx, sec, 0);
		t->address = ld_input_section_address(ctx, sec, (uint64_t)addend);
		if (is->kind != LD_SECTION_KIND_MERGE_INPUT)
			t->address = t->symbol_address + addend;
		return;
	}
	t->symbol_address = ld_input_section_address(ctx, sec, y->st_value);
	t->address = t->symbol_address + addend;
}

/*
 * Final addresses, once layout is done: ld_relocation_target_find() for the copy pass
 * from per-symbol and per-section values computed once (fin_*), falling
 * back to ld_relocation_target_find() for undefined, discarded and merge-piece targets.
 */

static void ld_input_section_final_address_set(void *arg, size_t i)
{
	struct ld_context *ctx = arg;
	ld_input_section *is = &ctx->inputs.sections.v[i];
	uint32_t os = ld_input_section_output_section(ctx, i);
	ctx->relocations.final_section_addresses[i] = is->kind == LD_SECTION_KIND_NONE || is->kind == LD_SECTION_KIND_MERGE_INPUT || os == LD_NONE || os == LD_OUTPUT_SECTION_DISCARD
			      ? UINT64_MAX : ctx->script.sections.v[os].address + is->offset;
}
static void ld_global_symbol_final_address_set(void *arg, size_t g)
{
	struct ld_context *ctx = arg;
	ld_global_symbol *s = &ctx->symbols.globals[g];
	s->is_undefined = s->state == LD_SYMBOL_STATE_UNDEFINED || s->demoted;
	if (!s->is_undefined) {
		s->is_absolute = ld_global_symbol_is_absolute(ctx, (uint32_t)g);
		s->final_address = ld_global_symbol_address(ctx, (uint32_t)g);
	}
}
VPRIV void ld_final_addresses_compute(struct ld_context *ctx)
{
	ctx->relocations.final_section_addresses = ld_region_alloc((size_t)ctx->inputs.sections.n * sizeof(uint64_t));
	ld_parallel_for(ctx->inputs.sections.n, ld_input_section_final_address_set, ctx);
	ld_parallel_for(atomic_load(&ctx->symbols.count), ld_global_symbol_final_address_set, ctx);
}

