/* The address-assignment state and the functions that turn a section or symbol into an address (lld's ExprValue). */

/* ---------------- address-assignment state ---------------- */

static uint64_t ld_output_section_address(struct ld_context *ctx, uint32_t o) { return o == LD_OUTPUT_SECTION_AETHER ? 0 : ctx->script.sections.v[o].address; }
static uint32_t ld_output_section_header_index(struct ld_context *ctx, uint32_t o) { return o == LD_OUTPUT_SECTION_AETHER ? 1 : ctx->script.sections.v[o].section_header_index; }

/* ---------------- merge pieces ---------------- */

static uint32_t ld_merge_piece_find(const ld_merge_input *m, uint64_t off)
{
	uint32_t lo = 0, hi = m->piece_count;
	while (hi - lo > 1) {
		uint32_t mid = (lo + hi) / 2;
		if (m->input_offsets[mid] <= off)
			lo = mid;
		else
			hi = mid;
	}
	return lo;
}

/* offset of input offset `off` of section s within its output section */
static uint64_t ld_input_section_output_offset(struct ld_context *ctx, uint32_t s, uint64_t off)
{
	ld_input_section *is = &ctx->inputs.sections.v[s];
	if (is->kind == LD_SECTION_KIND_MERGE_INPUT) {
		ld_merge_input *m = &ctx->inputs.merge_inputs.v[is->auxiliary_index];
		uint32_t p = ld_merge_piece_find(m, off);
		return ctx->inputs.sections.v[m->synthetic_section].offset + m->output_offsets[p] + (off - m->input_offsets[p]);
	}
	return is->offset + off;
}
static uint32_t ld_input_section_output_section(struct ld_context *ctx, uint32_t s)
{
	ld_input_section *is = &ctx->inputs.sections.v[s];
	if (is->kind == LD_SECTION_KIND_MERGE_INPUT && is->output_section != LD_OUTPUT_SECTION_DISCARD)	/* a discarded one has no MergeIn */
		return ctx->inputs.sections.v[ctx->inputs.merge_inputs.v[is->auxiliary_index].synthetic_section].output_section;
	return is->output_section;
}
static uint64_t ld_input_section_address(struct ld_context *ctx, uint32_t s, uint64_t off)
{
	uint32_t o = ld_input_section_output_section(ctx, s);
	if (o == LD_NONE || o == LD_OUTPUT_SECTION_DISCARD)
		return 0;
	return ctx->script.sections.v[o].address + ld_input_section_output_offset(ctx, s, off);
}

static uint64_t ld_expression_value_section_address(struct ld_context *ctx, ld_expression_value v)
{
	switch (v.kind) {
	case LD_VALUE_KIND_OUTPUT_SECTION: return ld_output_section_address(ctx, v.index);
	case LD_VALUE_KIND_INPUT_SECTION: return ld_input_section_address(ctx, v.index, 0);
	default: return 0;
	}
}
static uint64_t ld_expression_value_resolve(struct ld_context *ctx, ld_expression_value v)
{
	uint64_t x;
	switch (v.kind) {
	case LD_VALUE_KIND_OUTPUT_SECTION: x = ld_output_section_address(ctx, v.index) + v.value; break;
	case LD_VALUE_KIND_INPUT_SECTION: x = ld_input_section_address(ctx, v.index, v.value); break;
	default: x = v.value;
	}
	return ld_value_align_up(x, v.alignment);
}
static uint64_t ld_expression_value_section_offset(struct ld_context *ctx, ld_expression_value v) { return ld_expression_value_resolve(ctx, v) - ld_expression_value_section_address(ctx, v); }
static bool ld_expression_value_is_absolute(ld_expression_value v) { return v.force_absolute || v.kind == LD_VALUE_KIND_ABSOLUTE; }
static ld_expression_value ld_expression_value_absolute_make(uint64_t x) { return (ld_expression_value){.kind = LD_VALUE_KIND_ABSOLUTE, .value = x, .alignment = 1}; }
static ld_expression_value ld_expression_value_in_section_make(ld_expression_value a, bool fabs, uint64_t off)
{
	return (ld_expression_value){.kind = a.kind, .index = a.index, .force_absolute = fabs, .value = off, .alignment = 1};
}

/* value of a global symbol as an expression value (getSymbolValue) */
static ld_expression_value ld_global_symbol_expression_value(struct ld_context *ctx, uint32_t g)
{
	ld_global_symbol *s = &ctx->symbols.globals[g];
	ld_expression_value v = {.alignment = 1, .type = s->type};
	if (s->state == LD_SYMBOL_STATE_SCRIPT) {
		v.kind = s->expression_value.kind;
		v.index = s->expression_value.index;
		v.value = s->expression_value.value;
		return v;
	}
	if (s->state == LD_SYMBOL_STATE_OBJECT && !s->demoted) {
		if (s->input_section == LD_NONE) {
			v.kind = LD_VALUE_KIND_ABSOLUTE;
			v.value = s->value;
		} else {
			v.kind = LD_VALUE_KIND_INPUT_SECTION;
			v.index = s->input_section;
			v.value = s->value;
		}
		return v;
	}
	if (ctx->layout.in_layout)
		ld_error_report(ctx, "symbol not found: %s", ld_global_name(ctx, g));
	return ld_expression_value_absolute_make(0);
}

static uint64_t ld_global_symbol_address(struct ld_context *ctx, uint32_t g)
{
	ld_global_symbol *s = &ctx->symbols.globals[g];
	if (s->thunk != LD_NONE && 0)
		return 0;
	if (s->state == LD_SYMBOL_STATE_SCRIPT)
		return s->expression_value.kind == LD_VALUE_KIND_ABSOLUTE ? s->expression_value.value :
		       s->expression_value.kind == LD_VALUE_KIND_OUTPUT_SECTION ? ld_output_section_address(ctx, s->expression_value.index) + s->expression_value.value :
		       ld_input_section_address(ctx, s->expression_value.index, s->expression_value.value);
	if (s->state == LD_SYMBOL_STATE_OBJECT && !s->demoted)
		return s->input_section == LD_NONE ? s->value : ld_input_section_address(ctx, s->input_section, s->value);
	return 0;
}
static bool ld_global_symbol_is_absolute(struct ld_context *ctx, uint32_t g)
{
	ld_global_symbol *s = &ctx->symbols.globals[g];
	if (s->state == LD_SYMBOL_STATE_SCRIPT)
		return s->expression_value.kind == LD_VALUE_KIND_ABSOLUTE;
	if (s->state == LD_SYMBOL_STATE_OBJECT && !s->demoted)
		return s->input_section == LD_NONE;
	return true;
}
