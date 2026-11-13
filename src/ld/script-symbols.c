/* Declaring the symbols a script defines or references (lld's addScriptReferencedSymbolsToSymTable and declareSymbols). */

static void ld_script_assignments_for_each(struct ld_context *ctx, void (*fn)(struct ld_context *, ld_assignment *))
{
	for (uint32_t c = 0; c < ctx->script.commands.n; c++) {
		ld_command *cm = &ctx->script.commands.v[c];
		if (cm->kind == LD_COMMAND_ASSIGNMENT) {
			fn(ctx, cm->assignment);
			continue;
		}
		ld_output_section *os = &ctx->script.sections.v[cm->index];
		for (uint32_t k = 0; k < os->commands.n; k++)
			if (os->commands.v[k].kind == LD_COMMAND_ASSIGNMENT)
				fn(ctx, os->commands.v[k].assignment);
	}
}

static bool ld_global_symbol_is_referenced(struct ld_context *ctx, uint32_t g) { return ld_key_load(&ctx->symbols.globals[g].first_occurrence) != UINT64_MAX; }
static bool ld_global_symbol_is_defined_by_object(struct ld_context *ctx, uint32_t g) { return ld_key_load(&ctx->symbols.globals[g].definition) != UINT64_MAX; }

static void ld_script_symbol_insert(struct ld_context *ctx, uint32_t g)
{
	if (!ld_global_symbol_is_referenced(ctx, g))
		VVEC_PUSH(ctx->symbols.script_inserted, g, ld_region_alloc);
	ld_atomic_u64_min(&ctx->symbols.globals[g].first_occurrence, ctx->symbols.script_order++);
}

static void ld_script_symbol_define_decide(struct ld_context *ctx, ld_assignment *a)
{
	if (a->symbol == LD_NONE)
		return;
	a->defined = !a->provide || (ld_global_symbol_is_referenced(ctx, a->symbol) && !ld_global_symbol_is_defined_by_object(ctx, a->symbol));
}
static void ld_script_symbol_declare(struct ld_context *ctx, ld_assignment *a)
{
	if (a->symbol == LD_NONE || !a->defined)
		return;
	ld_script_symbol_insert(ctx, a->symbol);
	ld_global_symbol *s = &ctx->symbols.globals[a->symbol];
	s->state = LD_SYMBOL_STATE_SCRIPT;
	s->defined_by_script = 1;
	s->expression_value = (ld_expression_value){.kind = LD_VALUE_KIND_ABSOLUTE, .alignment = 1};
}

/* addScriptReferencedSymbolsToSymTable() + declareSymbols() */
static void ld_script_symbols_declare(struct ld_context *ctx)
{
	ctx->symbols.script_order = (uint64_t)(ctx->inputs.objects.n + 1) << 32;
	for (uint32_t i = 0; i < ctx->script.referenced_symbols.n; i++)
		ld_script_symbol_insert(ctx, ctx->script.referenced_symbols.v[i]);
	uint8_t *added = ld_region_alloc(ctx->script.provides.n + 1);
	VVEC(uint32_t) stack = {0};
	for (uint32_t i = 0; i < ctx->script.provides.n; i++) {
		uint32_t g = ctx->script.provides.v[i].symbol;
		bool dup = false;
		for (uint32_t k = 0; k < i && !dup; k++)
			dup = ctx->script.provides.v[k].symbol == g;
		if (!dup && ld_global_symbol_is_referenced(ctx, g) && !ld_global_symbol_is_defined_by_object(ctx, g)) {
			added[i] = 1;
			VVEC_PUSH(stack, i, ld_region_alloc);
		}
	}
	while (stack.n) {
		ld_provide *p = &ctx->script.provides.v[stack.v[--stack.n]];
		for (uint32_t k = 0; k < p->references.n; k++) {
			uint32_t g = p->references.v[k];
			ld_script_symbol_insert(ctx, g);
			for (uint32_t j = 0; j < ctx->script.provides.n; j++) {
				if (ctx->script.provides.v[j].symbol != g)
					continue;
				bool seen = false;
				for (uint32_t q = 0; q < ctx->script.provides.n && !seen; q++)
					seen = ctx->script.provides.v[q].symbol == g && added[q];
				if (!seen && !ld_global_symbol_is_defined_by_object(ctx, g)) {
					added[j] = 1;
					VVEC_PUSH(stack, j, ld_region_alloc);
				}
				break;
			}
		}
	}
	ld_script_assignments_for_each(ctx, ld_script_symbol_define_decide);
	ld_script_assignments_for_each(ctx, ld_script_symbol_declare);
}
