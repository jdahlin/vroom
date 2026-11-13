/* Declaring the symbols a script defines or references (lld's addScriptReferencedSymbolsToSymTable and declareSymbols). */

static void for_assigns(void (*fn)(Assign *))
{
	for (uint32_t c = 0; c < topcmds.n; c++) {
		Cmd *cm = &topcmds.v[c];
		if (cm->kind == C_ASSIGN) {
			fn(cm->as);
			continue;
		}
		OSec *os = &osecs.v[cm->idx];
		for (uint32_t k = 0; k < os->cmds.n; k++)
			if (os->cmds.v[k].kind == C_ASSIGN)
				fn(os->cmds.v[k].as);
	}
}

static bool g_referenced(uint32_t g) { return kget(&gsyms[g].first) != UINT64_MAX; }
static bool g_defined_obj(uint32_t g) { return kget(&gsyms[g].def) != UINT64_MAX; }

static VVEC(uint32_t) script_new;	/* slots first inserted by the script, in order */

static void script_insert(uint32_t g)
{
	if (!g_referenced(g))
		VVEC_PUSH(script_new, g, alloc);
	atomic_min64(&gsyms[g].first, script_ord++);
}

static void decide_define(Assign *a)
{
	if (a->sym == NONE)
		return;
	a->defined = !a->provide || (g_referenced(a->sym) && !g_defined_obj(a->sym));
}
static void declare(Assign *a)
{
	if (a->sym == NONE || !a->defined)
		return;
	script_insert(a->sym);
	G *s = &gsyms[a->sym];
	s->state = GS_SCRIPT;
	s->scriptdef = 1;
	s->ev = (EV){.kind = EV_ABS, .align = 1};
}

/* addScriptReferencedSymbolsToSymTable() + declareSymbols() */
static void script_symbols(void)
{
	script_ord = (uint64_t)(objs.n + 1) << 32;
	for (uint32_t i = 0; i < refsyms.n; i++)
		script_insert(refsyms.v[i]);
	uint8_t *added = alloc(provides.n + 1);
	VVEC(uint32_t) stack = {0};
	for (uint32_t i = 0; i < provides.n; i++) {
		uint32_t g = provides.v[i].sym;
		bool dup = false;
		for (uint32_t k = 0; k < i && !dup; k++)
			dup = provides.v[k].sym == g;
		if (!dup && g_referenced(g) && !g_defined_obj(g)) {
			added[i] = 1;
			VVEC_PUSH(stack, i, alloc);
		}
	}
	while (stack.n) {
		Provide *p = &provides.v[stack.v[--stack.n]];
		for (uint32_t k = 0; k < p->refs.n; k++) {
			uint32_t g = p->refs.v[k];
			script_insert(g);
			for (uint32_t j = 0; j < provides.n; j++) {
				if (provides.v[j].sym != g)
					continue;
				bool seen = false;
				for (uint32_t q = 0; q < provides.n && !seen; q++)
					seen = provides.v[q].sym == g && added[q];
				if (!seen && !g_defined_obj(g)) {
					added[j] = 1;
					VVEC_PUSH(stack, j, alloc);
				}
				break;
			}
		}
	}
	for_assigns(decide_define);
	for_assigns(declare);
}
