/* lld's adjustOutputSections: which output sections survive. */

/* ---------------- adjustOutputSections ---------------- */

static bool osec_discardable(OSec *os)
{
	if (os->discard)
		return true;
	if (os->uses_syms || os->used_in_expr)
		return false;
	for (uint32_t c = 0; c < os->cmds.n; c++) {
		Cmd *cm = &os->cmds.v[c];
		if (cm->kind == C_ASSIGN) {
			if (cm->as->sym != NONE && !cm->as->defined && !cm->as->assert_)
				continue;
			return false;
		}
	}
	return true;
}

static void adjust_output_sections(void)
{
	uint64_t flags = SHF_ALLOC;
	for (uint32_t c = 0; c < topcmds.n; c++) {
		if (topcmds.v[c].kind != C_OSEC)
			continue;
		OSec *os = &osecs.v[topcmds.v[c].idx];
		if (os->alignx) {
			uint64_t a = ev_value(eval(os->alignx));
			if (a > os->addralign)
				os->addralign = a;
		}
		bool empty = true;
		for (uint32_t k = 0; k < os->cmds.n && empty; k++)
			if (os->cmds.v[k].kind == C_ISD && isds.v[os->cmds.v[k].idx].secs.n)
				empty = false;
		bool discardable = empty && osec_discardable(os);
		if (os->has_input && !discardable)
			flags = os->flags;
		if (empty)
			os->flags = flags & ((os->nonalloc ? 0 : SHF_ALLOC) | SHF_WRITE);
		os->live = !discardable;
	}
}
