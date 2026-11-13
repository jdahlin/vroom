/* Declaring the symbols a script defines or references (lld's addScriptReferencedSymbolsToSymTable and declareSymbols). */

static void ld_script_assignments_for_each(void (*fn)(ld_assignment *))
{
	for (uint32_t c = 0; c < topcmds.n; c++) {
		ld_command *cm = &topcmds.v[c];
		if (cm->kind == LD_COMMAND_ASSIGNMENT) {
			fn(cm->as);
			continue;
		}
		ld_output_section *os = &osecs.v[cm->idx];
		for (uint32_t k = 0; k < os->cmds.n; k++)
			if (os->cmds.v[k].kind == LD_COMMAND_ASSIGNMENT)
				fn(os->cmds.v[k].as);
	}
}

static bool ld_global_symbol_is_referenced(uint32_t g) { return ld_key_load(&gsyms[g].first) != UINT64_MAX; }
static bool ld_global_symbol_is_defined_by_object(uint32_t g) { return ld_key_load(&gsyms[g].def) != UINT64_MAX; }

static VVEC(uint32_t) script_new;	/* slots first inserted by the script, in order */

static void ld_script_symbol_insert(uint32_t g)
{
	if (!ld_global_symbol_is_referenced(g))
		VVEC_PUSH(script_new, g, ld_region_alloc);
	ld_atomic_u64_min(&gsyms[g].first, script_ord++);
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
	ld_global_symbol *s = &gsyms[a->sym];
	s->state = LD_SYMBOL_STATE_SCRIPT;
	s->scriptdef = 1;
	s->ev = (ld_expression_value){.kind = LD_VALUE_KIND_ABSOLUTE, .align = 1};
}

/* addScriptReferencedSymbolsToSymTable() + declareSymbols() */
static void ld_script_symbols_declare(void)
{
	script_ord = (uint64_t)(objs.n + 1) << 32;
	for (uint32_t i = 0; i < refsyms.n; i++)
		ld_script_symbol_insert(refsyms.v[i]);
	uint8_t *added = ld_region_alloc(provides.n + 1);
	VVEC(uint32_t) stack = {0};
	for (uint32_t i = 0; i < provides.n; i++) {
		uint32_t g = provides.v[i].sym;
		bool dup = false;
		for (uint32_t k = 0; k < i && !dup; k++)
			dup = provides.v[k].sym == g;
		if (!dup && ld_global_symbol_is_referenced(g) && !ld_global_symbol_is_defined_by_object(g)) {
			added[i] = 1;
			VVEC_PUSH(stack, i, ld_region_alloc);
		}
	}
	while (stack.n) {
		ld_provide *p = &provides.v[stack.v[--stack.n]];
		for (uint32_t k = 0; k < p->refs.n; k++) {
			uint32_t g = p->refs.v[k];
			ld_script_symbol_insert(g);
			for (uint32_t j = 0; j < provides.n; j++) {
				if (provides.v[j].sym != g)
					continue;
				bool seen = false;
				for (uint32_t q = 0; q < provides.n && !seen; q++)
					seen = provides.v[q].sym == g && added[q];
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
