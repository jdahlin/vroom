/* lld's adjustOutputSections: which output sections survive. */

#include "ld-internal.h"

/* ---------------- adjustOutputSections ---------------- */

static bool ld_output_section_is_discardable(ld_output_section *os)
{
	if (os->discard)
		return true;
	if (os->uses_symbols || os->used_in_expression)
		return false;
	for (uint32_t c = 0; c < os->commands.n; c++) {
		ld_command *cm = &os->commands.v[c];
		if (cm->kind == LD_COMMAND_ASSIGNMENT) {
			if (cm->assignment->symbol != LD_NONE && !cm->assignment->defined && !cm->assignment->is_assert)
				continue;
			return false;
		}
	}
	return true;
}

VPRIV void ld_output_sections_adjust(struct ld_context *ctx)
{
	uint64_t flags = SHF_ALLOC;
	for (uint32_t c = 0; c < ctx->script.commands.n; c++) {
		if (ctx->script.commands.v[c].kind != LD_COMMAND_OUTPUT_SECTION)
			continue;
		ld_output_section *os = &ctx->script.sections.v[ctx->script.commands.v[c].index];
		if (os->align_expression) {
			uint64_t a = ld_expression_value_resolve(ctx, ld_expression_eval(ctx, os->align_expression));
			if (a > os->address_alignment)
				os->address_alignment = a;
		}
		bool empty = true;
		for (uint32_t k = 0; k < os->commands.n && empty; k++)
			if (os->commands.v[k].kind == LD_COMMAND_INPUT_SECTION_DESCRIPTION && ctx->script.input_descriptions.v[os->commands.v[k].index].sections.n)
				empty = false;
		bool discardable = empty && ld_output_section_is_discardable(os);
		if (os->has_input && !discardable)
			flags = os->flags;
		if (empty)
			os->flags = flags & ((os->not_allocated ? 0 : SHF_ALLOC) | SHF_WRITE);
		os->live = !discardable;
	}
}
