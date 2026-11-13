/* Declaring the symbols a script defines or references (lld's addScriptReferencedSymbolsToSymTable and declareSymbols). */

static void ld_script_assignments_for_each(void (*fn)(ld_assignment *))
{
	for (uint32_t c = 0; c < ctx->script.commands.n; c++) {
		ld_command *cm = &ctx->script.commands.v[c];
		if (cm->kind == LD_COMMAND_ASSIGNMENT) {
			fn(cm->as);
			continue;
		}
		ld_output_section *os = &ctx->script.sections.v[cm->idx];
		for (uint32_t k = 0; k < os->cmds.n; k++)
			if (os->cmds.v[k].kind == LD_COMMAND_ASSIGNMENT)
				fn(os->cmds.v[k].as);
	}
}

static bool ld_global_symbol_is_referenced(uint32_t g) { return ld_key_load(&ctx->symbols.globals[g].first) != UINT64_MAX; }
static bool ld_global_symbol_is_defined_by_object(uint32_t g) { return ld_key_load(&ctx->symbols.globals[g].def) != UINT64_MAX; }

static void ld_script_symbol_insert(uint32_t g)
{
	if (!ld_global_symbol_is_referenced(g))
		VVEC_PUSH(ctx->symbols.script_inserted, g, ld_region_alloc);
	ld_atomic_u64_min(&ctx->symbols.globals[g].first, ctx->symbols.script_order++);
}

static void ld_script_symbol_define_decide(ld_assignment *a)
{
	if (a->sym == LD_NONE)
		return;
	a->defined = !a->provide || (ld_global_symbol_is_referenced(a->sym) && !ld_global_symbol_is_defined_by_object(a->sym));
}
static void ld_script_symbol_declare(ld_assignment *a)
{
	if (a->sym == LD_NONE || !a->defined)
		return;
	ld_script_symbol_insert(a->sym);
	ld_global_symbol *s = &ctx->symbols.globals[a->sym];
	s->state = LD_SYMBOL_STATE_SCRIPT;
	s->scriptdef = 1;
	s->ev = (ld_expression_value){.kind = LD_VALUE_KIND_ABSOLUTE, .align = 1};
}

/* addScriptReferencedSymbolsToSymTable() + declareSymbols() */
static void ld_script_symbols_declare(void)
{
	ctx->symbols.script_order = (uint64_t)(ctx->inputs.objects.n + 1) << 32;
	for (uint32_t i = 0; i < ctx->script.referenced_symbols.n; i++)
		ld_script_symbol_insert(ctx->script.referenced_symbols.v[i]);
	uint8_t *added = ld_region_alloc(ctx->script.provides.n + 1);
	VVEC(uint32_t) stack = {0};
	for (uint32_t i = 0; i < ctx->script.provides.n; i++) {
		uint32_t g = ctx->script.provides.v[i].sym;
		bool dup = false;
		for (uint32_t k = 0; k < i && !dup; k++)
			dup = ctx->script.provides.v[k].sym == g;
		if (!dup && ld_global_symbol_is_referenced(g) && !ld_global_symbol_is_defined_by_object(g)) {
			added[i] = 1;
			VVEC_PUSH(stack, i, ld_region_alloc);
		}
	}
	while (stack.n) {
		ld_provide *p = &ctx->script.provides.v[stack.v[--stack.n]];
		for (uint32_t k = 0; k < p->refs.n; k++) {
			uint32_t g = p->refs.v[k];
			ld_script_symbol_insert(g);
			for (uint32_t j = 0; j < ctx->script.provides.n; j++) {
				if (ctx->script.provides.v[j].sym != g)
					continue;
				bool seen = false;
				for (uint32_t q = 0; q < ctx->script.provides.n && !seen; q++)
					seen = ctx->script.provides.v[q].sym == g && added[q];
				if (!seen && !ld_global_symbol_is_defined_by_object(g)) {
					added[j] = 1;
					VVEC_PUSH(stack, j, ld_region_alloc);
				}
				break;
			}
		}
	}
	ld_script_assignments_for_each(ld_script_symbol_define_decide);
	ld_script_assignments_for_each(ld_script_symbol_declare);
}
