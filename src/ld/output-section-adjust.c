/* lld's adjustOutputSections: which output sections survive. */

/* ---------------- adjustOutputSections ---------------- */

static bool ld_output_section_is_discardable(ld_output_section *os)
{
	if (os->discard)
		return true;
	if (os->uses_syms || os->used_in_expr)
		return false;
	for (uint32_t c = 0; c < os->cmds.n; c++) {
		ld_command *cm = &os->cmds.v[c];
		if (cm->kind == LD_COMMAND_ASSIGNMENT) {
			if (cm->as->sym != LD_NONE && !cm->as->defined && !cm->as->assert_)
				continue;
			return false;
		}
	}
	return true;
}

static void ld_output_sections_adjust(void)
{
	uint64_t flags = SHF_ALLOC;
	for (uint32_t c = 0; c < topcmds.n; c++) {
		if (topcmds.v[c].kind != LD_COMMAND_OUTPUT_SECTION)
			continue;
		ld_output_section *os = &osecs.v[topcmds.v[c].idx];
		if (os->alignx) {
			uint64_t a = ld_expression_value_resolve(ld_expression_eval(os->alignx));
			if (a > os->addralign)
				os->addralign = a;
		}
		bool empty = true;
		for (uint32_t k = 0; k < os->cmds.n && empty; k++)
			if (os->cmds.v[k].kind == LD_COMMAND_INPUT_SECTION_DESCRIPTION && isds.v[os->cmds.v[k].idx].secs.n)
				empty = false;
		bool discardable = empty && ld_output_section_is_discardable(os);
		if (os->has_input && !discardable)
			flags = os->flags;
		if (empty)
			os->flags = flags & ((os->nonalloc ? 0 : SHF_ALLOC) | SHF_WRITE);
		os->live = !discardable;
	}
}
