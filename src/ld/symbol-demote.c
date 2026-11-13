/* Demoting symbols to local, and the __start_X/__stop_X symbols. */

static int ld_dynamic_symbol_cmp(const void *a, const void *b)
{
	uint64_t x = ld_input_section_address(*(const uint32_t *)a, 0), y = ld_input_section_address(*(const uint32_t *)b, 0);
	return (x > y) - (x < y);
}

static void ld_symbols_demote(void *arg, size_t i)
{
	(void)arg;
	ld_global_symbol *s = &gsyms[gorder.v[i]];
	if (s->state == LD_SYMBOL_STATE_OBJECT && s->sec != LD_NONE) {
		uint32_t os = ld_input_section_output_section(s->sec);
		if (isecs.v[s->sec].kind == LD_SECTION_KIND_NONE || os == LD_NONE || os == LD_OUTPUT_SECTION_DISCARD)
			s->demoted = 1;
	}
}

static void ld_global_symbol_finish_task(void *arg, size_t i) { ld_global_symbol_finish(arg, i); }

static VVEC(uint32_t) stop_syms;	/* __stop_X: the section size, after layout */

/* lld addStartStopSymbols: a referenced, undefined __start_X/__stop_X is the
 * start/end of output section X (X a C identifier), protected, global */
static void ld_start_stop_symbols_define(void)
{
	for (uint32_t i = 0; i < gorder.n; i++) {
		ld_global_symbol *s = &gsyms[gorder.v[i]];
		const char *n = ld_global_name(gorder.v[i]), *x;

		if (s->state != LD_SYMBOL_STATE_UNDEFINED || !(x = ld_start_stop_section_name(n)))
			continue;
		for (uint32_t k = 0; k < topcmds.n; k++) {
			if (topcmds.v[k].kind != LD_COMMAND_OUTPUT_SECTION || strcmp(osecs.v[topcmds.v[k].idx].name, x) ||
			    osecs.v[topcmds.v[k].idx].discard)
				continue;
			s->state = LD_SYMBOL_STATE_SCRIPT;
			s->ev = (ld_expression_value){.kind = LD_VALUE_KIND_OUTPUT_SECTION, .idx = topcmds.v[k].idx, .align = 1};
			s->vis = STV_PROTECTED;
			s->bind = STB_GLOBAL;
			s->type = STT_NOTYPE;
			if (n[2] == 's' && n[3] == 't' && n[4] == 'o')
				VVEC_PUSH(stop_syms, gorder.v[i], ld_region_alloc);
			break;
		}
	}
}
