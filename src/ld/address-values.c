/* The address-assignment state and the functions that turn a section or symbol into an address (lld's ExprValue). */

/* ---------------- address-assignment state ---------------- */

static uint64_t dot;
static uint32_t cur_osec;		/* LD_OUTPUT_SECTION_AETHER or an osec */
static bool in_layout;

static uint64_t ld_output_section_address(uint32_t o) { return o == LD_OUTPUT_SECTION_AETHER ? 0 : osecs.v[o].va; }
static uint32_t ld_output_section_header_index(uint32_t o) { return o == LD_OUTPUT_SECTION_AETHER ? 1 : osecs.v[o].shidx; }

/* ---------------- merge pieces ---------------- */

static uint32_t ld_merge_piece_find(const ld_merge_input *m, uint64_t off)
{
	uint32_t lo = 0, hi = m->npieces;
	while (hi - lo > 1) {
		uint32_t mid = (lo + hi) / 2;
		if (m->inoff[mid] <= off)
			lo = mid;
		else
			hi = mid;
	}
	return lo;
}

/* offset of input offset `off` of section s within its output section */
static uint64_t ld_input_section_output_offset(uint32_t s, uint64_t off)
{
	ld_input_section *is = &isecs.v[s];
	if (is->kind == LD_SECTION_KIND_MERGE_INPUT) {
		ld_merge_input *m = &mergeins.v[is->aux];
		uint32_t p = ld_merge_piece_find(m, off);
		return isecs.v[m->syn].off + m->outoff[p] + (off - m->inoff[p]);
	}
	return is->off + off;
}
static uint32_t ld_input_section_output_section(uint32_t s)
{
	ld_input_section *is = &isecs.v[s];
	if (is->kind == LD_SECTION_KIND_MERGE_INPUT && is->osec != LD_OUTPUT_SECTION_DISCARD)	/* a discarded one has no MergeIn */
		return isecs.v[mergeins.v[is->aux].syn].osec;
	return is->osec;
}
static uint64_t ld_input_section_address(uint32_t s, uint64_t off)
{
	uint32_t o = ld_input_section_output_section(s);
	if (o == LD_NONE || o == LD_OUTPUT_SECTION_DISCARD)
		return 0;
	return osecs.v[o].va + ld_input_section_output_offset(s, off);
}

static uint64_t ld_expression_value_section_address(ld_expression_value v)
{
	switch (v.kind) {
	case LD_VALUE_KIND_OUTPUT_SECTION: return ld_output_section_address(v.idx);
	case LD_VALUE_KIND_INPUT_SECTION: return ld_input_section_address(v.idx, 0);
	default: return 0;
	}
}
static uint64_t ld_expression_value_resolve(ld_expression_value v)
{
	uint64_t x;
	switch (v.kind) {
	case LD_VALUE_KIND_OUTPUT_SECTION: x = ld_output_section_address(v.idx) + v.val; break;
	case LD_VALUE_KIND_INPUT_SECTION: x = ld_input_section_address(v.idx, v.val); break;
	default: x = v.val;
	}
	return ld_value_align_up(x, v.align);
}
static uint64_t ld_expression_value_section_offset(ld_expression_value v) { return ld_expression_value_resolve(v) - ld_expression_value_section_address(v); }
static bool ld_expression_value_is_absolute(ld_expression_value v) { return v.fabs || v.kind == LD_VALUE_KIND_ABSOLUTE; }
static ld_expression_value ld_expression_value_absolute_make(uint64_t x) { return (ld_expression_value){.kind = LD_VALUE_KIND_ABSOLUTE, .val = x, .align = 1}; }
static ld_expression_value ld_expression_value_in_section_make(ld_expression_value a, bool fabs, uint64_t off)
{
	return (ld_expression_value){.kind = a.kind, .idx = a.idx, .fabs = fabs, .val = off, .align = 1};
}

/* value of a global symbol as an expression value (getSymbolValue) */
static ld_expression_value ld_global_symbol_expression_value(uint32_t g)
{
	ld_global_symbol *s = &gsyms[g];
	ld_expression_value v = {.align = 1, .type = s->type};
	if (s->state == LD_SYMBOL_STATE_SCRIPT) {
		v.kind = s->ev.kind;
		v.idx = s->ev.idx;
		v.val = s->ev.val;
		return v;
	}
	if (s->state == LD_SYMBOL_STATE_OBJECT && !s->demoted) {
		if (s->sec == LD_NONE) {
			v.kind = LD_VALUE_KIND_ABSOLUTE;
			v.val = s->val;
		} else {
			v.kind = LD_VALUE_KIND_INPUT_SECTION;
			v.idx = s->sec;
			v.val = s->val;
		}
		return v;
	}
	if (in_layout)
		ld_error_report("symbol not found: %s", ld_global_name(g));
	return ld_expression_value_absolute_make(0);
}

static uint64_t ld_global_symbol_address(uint32_t g)
{
	ld_global_symbol *s = &gsyms[g];
	if (s->thunk != LD_NONE && 0)
		return 0;
	if (s->state == LD_SYMBOL_STATE_SCRIPT)
		return s->ev.kind == LD_VALUE_KIND_ABSOLUTE ? s->ev.val :
		       s->ev.kind == LD_VALUE_KIND_OUTPUT_SECTION ? ld_output_section_address(s->ev.idx) + s->ev.val :
		       ld_input_section_address(s->ev.idx, s->ev.val);
	if (s->state == LD_SYMBOL_STATE_OBJECT && !s->demoted)
		return s->sec == LD_NONE ? s->val : ld_input_section_address(s->sec, s->val);
	return 0;
}
static bool ld_global_symbol_is_absolute(uint32_t g)
{
	ld_global_symbol *s = &gsyms[g];
	if (s->state == LD_SYMBOL_STATE_SCRIPT)
		return s->ev.kind == LD_VALUE_KIND_ABSOLUTE;
	if (s->state == LD_SYMBOL_STATE_OBJECT && !s->demoted)
		return s->sec == LD_NONE;
	return true;
}
