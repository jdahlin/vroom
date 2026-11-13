/* Script symbol assignments and the address-assignment passes over output sections. */

#include "ld-internal.h"

/* ================= script symbols and address assignment ================= */

/* op= of a symbol assignment (lld readSymbolAssignment) */
static ld_expression_value ld_assignment_operator_apply(struct ld_context *ctx, char op, ld_expression_value lhs, ld_expression_value v)
{
	uint64_t x, y;
	switch (op) {
	case '=': return v;
	case '+': return ld_expression_value_add(ctx, lhs, v);
	case '-': return ld_expression_value_subtract(ctx, lhs, v);
	}
	x = ld_expression_value_resolve(ctx, lhs);
	y = ld_expression_value_resolve(ctx, v);
	switch (op) {
	case '*': return ld_expression_value_absolute_make(x * y);
	case '/':
		if (!y)
			ld_error_report(ctx, "division by zero");
		return ld_expression_value_absolute_make(y ? x / y : 0);
	case '<': return ld_expression_value_absolute_make(x << (y % 64));
	case '>': return ld_expression_value_absolute_make(x >> (y % 64));
	case '&': return ld_expression_value_absolute_make(x & y);
	case '^': return ld_expression_value_absolute_make(x ^ y);
	case '|': return ld_expression_value_absolute_make(x | y);
	}
	return v;
}


/* assignSymbol(); returns true if the symbol's (section, value) changed */
static bool ld_script_symbol_assign(struct ld_context *ctx, ld_assignment *a, bool insec)
{
	if (a->is_assert) {
		ld_expression_eval(ctx, a->expression);
		return false;
	}
	if (a->symbol == LD_NONE) {
		ld_expression_value v = ld_assignment_operator_apply(ctx, a->operator_character, ld_dot_expression_value(ctx), ld_expression_eval(ctx, a->expression));
		uint64_t val = ld_expression_value_resolve(ctx, v);
		if (insec && val < ctx->layout.dot) {
			char *m = ld_region_alloc(160);
			snprintf(m, 160, "unable to move location counter (0x%llx) backward to 0x%llx for section '%s'",
				 (unsigned long long)ctx->layout.dot, (unsigned long long)val, ctx->script.sections.v[ctx->layout.current_output_section].name);
			VVEC_PUSH(ctx->script.address_errors, m, ld_region_alloc);
		}
		ctx->layout.dot = val;
		return false;
	}
	if (!a->defined)
		return false;
	ld_global_symbol *s = &ctx->symbols.globals[a->symbol];
	ld_expression_value v = ld_assignment_operator_apply(ctx, a->operator_character, ld_global_symbol_expression_value(ctx, a->symbol), ld_expression_eval(ctx, a->expression));
	ld_expression_value old = s->expression_value;
	if (ld_expression_value_is_absolute(v))
		s->expression_value = (ld_expression_value){.kind = LD_VALUE_KIND_ABSOLUTE, .value = ld_expression_value_resolve(ctx, v), .alignment = 1};
	else
		s->expression_value = (ld_expression_value){.kind = v.kind, .index = v.index, .value = ld_expression_value_section_offset(ctx, v), .alignment = 1};
	s->type = v.type;
	return old.kind != s->expression_value.kind || old.index != s->expression_value.index || old.value != s->expression_value.value;
}

/* assignOffsets(); returns true if the section address changed */
VPRIV bool ld_output_section_offsets_assign(struct ld_context *ctx, uint32_t oi, bool *symchg)
{
	ld_output_section *os = &ctx->script.sections.v[oi];
	uint64_t saved = ctx->layout.dot;
	bool tbss = (os->flags & (SHF_ALLOC | SHF_TLS)) == (SHF_ALLOC | SHF_TLS) && os->type == SHT_NOBITS;
	if (tbss)
		ctx->layout.dot += ctx->layout.tbss_offset;
	if (!(os->flags & SHF_ALLOC))
		ctx->layout.dot = 0;
	else if (os->address_expression)
		ctx->layout.dot = ld_expression_value_resolve(ctx, ld_expression_eval(ctx, os->address_expression));
	ctx->layout.current_output_section = oi;
	if (!os->address_expression)
		ctx->layout.dot = ld_value_align_up(ctx->layout.dot, os->address_alignment);
	bool changed = os->address != ctx->layout.dot;
	os->address = ctx->layout.dot;
	for (uint32_t c = 0; c < os->commands.n; c++) {
		ld_command *cm = &os->commands.v[c];
		if (cm->kind == LD_COMMAND_ASSIGNMENT) {
			*symchg |= ld_script_symbol_assign(ctx, cm->assignment, true);
			continue;
		}
		ld_input_section_description *d = &ctx->script.input_descriptions.v[cm->index];
		for (uint32_t k = 0; k < d->sections.n; k++) {
			ld_input_section *is = &ctx->inputs.sections.v[d->sections.v[k]];
			ctx->layout.dot = ld_value_align_up(ctx->layout.dot, is->alignment);
			is->offset = ctx->layout.dot - os->address;
			ctx->layout.dot += is->size;
		}
	}
	os->size = ctx->layout.dot - os->address;
	if (!(os->flags & SHF_ALLOC))
		ctx->layout.dot = saved;
	else if (tbss) {
		ctx->layout.tbss_offset = ctx->layout.dot - saved;
		ctx->layout.dot = saved;
	}
	return changed;
}

VPRIV bool ld_addresses_assign(struct ld_context *ctx)
{
	bool changed = false, symchg = false;
	ctx->layout.dot = 0;
	ctx->layout.tbss_offset = 0;
	ctx->layout.current_output_section = LD_OUTPUT_SECTION_AETHER;
	ctx->script.address_errors.n = 0;
	ctx->script.assert_failures.n = 0;
	for (uint32_t i = 0; i < ctx->script.commands.n; i++) {
		ld_command *c = &ctx->script.commands.v[i];
		if (c->kind == LD_COMMAND_ASSIGNMENT) {
			symchg |= ld_script_symbol_assign(ctx, c->assignment, false);
			continue;
		}
		if (!ctx->script.sections.v[c->index].live)
			continue;
		changed |= ld_output_section_offsets_assign(ctx, c->index, &symchg);
	}
	return changed || symchg;
}

/* processSymbolAssignments(): first definition of script symbols, dot = 0
   in the aether.  A section-relative value starts at the section's start. */
static void ld_script_symbol_add(struct ld_context *ctx, ld_assignment *a)
{
	if (a->symbol == LD_NONE || !a->defined)
		return;
	ld_global_symbol *s = &ctx->symbols.globals[a->symbol];
	ld_expression_value v = ld_assignment_operator_apply(ctx, a->operator_character, ld_global_symbol_expression_value(ctx, a->symbol), ld_expression_eval(ctx, a->expression));
	s->state = LD_SYMBOL_STATE_SCRIPT;
	s->object = LD_NONE;
	s->binding = STB_GLOBAL;
	s->size = 0;
	s->type = v.type;
	if (a->hidden && ld_visibility_rank(s->visibility) > ld_visibility_rank(STV_HIDDEN))
		s->visibility = STV_HIDDEN;
	if (ld_expression_value_is_absolute(v))
		s->expression_value = (ld_expression_value){.kind = LD_VALUE_KIND_ABSOLUTE, .value = v.kind != LD_VALUE_KIND_ABSOLUTE ? 0 : ld_expression_value_resolve(ctx, v), .alignment = 1};
	else
		s->expression_value = (ld_expression_value){.kind = v.kind, .index = v.index, .value = 0, .alignment = 1};
}

VPRIV void ld_symbol_assignments_process(struct ld_context *ctx)
{
	ctx->layout.dot = 0;
	ctx->layout.current_output_section = LD_OUTPUT_SECTION_AETHER;
	for (uint32_t i = 0; i < ctx->script.commands.n; i++) {
		ld_command *c = &ctx->script.commands.v[i];
		if (c->kind == LD_COMMAND_ASSIGNMENT) {
			ld_script_symbol_add(ctx, c->assignment);
			continue;
		}
		ld_output_section *os = &ctx->script.sections.v[c->index];
		for (uint32_t k = 0; k < os->commands.n; k++)
			if (os->commands.v[k].kind == LD_COMMAND_ASSIGNMENT)
				ld_script_symbol_add(ctx, os->commands.v[k].assignment);
	}
}
