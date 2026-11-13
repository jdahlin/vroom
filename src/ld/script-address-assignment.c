/* Script symbol assignments and the address-assignment passes over output sections. */

/* ================= script symbols and address assignment ================= */

/* op= of a symbol assignment (lld readSymbolAssignment) */
static EV apply_op(char op, EV lhs, EV v)
{
	uint64_t x, y;
	switch (op) {
	case '=': return v;
	case '+': return ev_add(lhs, v);
	case '-': return ev_sub(lhs, v);
	}
	x = ev_value(lhs);
	y = ev_value(v);
	switch (op) {
	case '*': return evabs(x * y);
	case '/':
		if (!y)
			error("division by zero");
		return evabs(y ? x / y : 0);
	case '<': return evabs(x << (y % 64));
	case '>': return evabs(x >> (y % 64));
	case '&': return evabs(x & y);
	case '^': return evabs(x ^ y);
	case '|': return evabs(x | y);
	}
	return v;
}

static VVEC(const char *) recorded;	/* errors of the current address pass */

/* assignSymbol(); returns true if the symbol's (section, value) changed */
static bool assign_symbol(Assign *a, bool insec)
{
	if (a->assert_) {
		eval(a->e);
		return false;
	}
	if (a->sym == NONE) {
		EV v = apply_op(a->op, dot_ev(), eval(a->e));
		uint64_t val = ev_value(v);
		if (insec && val < ctx->layout.dot) {
			char *m = alloc(160);
			snprintf(m, 160, "unable to move location counter (0x%llx) backward to 0x%llx for section '%s'",
				 (unsigned long long)ctx->layout.dot, (unsigned long long)val, osecs.v[ctx->layout.current_output_section].name);
			VVEC_PUSH(recorded, m, alloc);
		}
		ctx->layout.dot = val;
		return false;
	}
	if (!a->defined)
		return false;
	G *s = &gsyms[a->sym];
	EV v = apply_op(a->op, gsym_ev(a->sym), eval(a->e));
	EV old = s->ev;
	if (ev_isabs(v))
		s->ev = (EV){.kind = EV_ABS, .val = ev_value(v), .align = 1};
	else
		s->ev = (EV){.kind = v.kind, .idx = v.idx, .val = ev_secoff(v), .align = 1};
	s->type = v.type;
	return old.kind != s->ev.kind || old.idx != s->ev.idx || old.val != s->ev.val;
}

/* assignOffsets(); returns true if the section address changed */
static bool assign_offsets(uint32_t oi, bool *symchg)
{
	OSec *os = &osecs.v[oi];
	uint64_t saved = ctx->layout.dot;
	bool tbss = (os->flags & (SHF_ALLOC | SHF_TLS)) == (SHF_ALLOC | SHF_TLS) && os->type == SHT_NOBITS;
	if (tbss)
		ctx->layout.dot += ctx->layout.tbss_offset;
	if (!(os->flags & SHF_ALLOC))
		ctx->layout.dot = 0;
	else if (os->addr)
		ctx->layout.dot = ev_value(eval(os->addr));
	ctx->layout.current_output_section = oi;
	if (!os->addr)
		ctx->layout.dot = alignup(ctx->layout.dot, os->addralign);
	bool changed = os->va != ctx->layout.dot;
	os->va = ctx->layout.dot;
	for (uint32_t c = 0; c < os->cmds.n; c++) {
		Cmd *cm = &os->cmds.v[c];
		if (cm->kind == C_ASSIGN) {
			*symchg |= assign_symbol(cm->as, true);
			continue;
		}
		ISD *d = &isds.v[cm->idx];
		for (uint32_t k = 0; k < d->secs.n; k++) {
			ISec *is = &isecs.v[d->secs.v[k]];
			ctx->layout.dot = alignup(ctx->layout.dot, is->align);
			is->off = ctx->layout.dot - os->va;
			ctx->layout.dot += is->size;
		}
	}
	os->size = ctx->layout.dot - os->va;
	if (!(os->flags & SHF_ALLOC))
		ctx->layout.dot = saved;
	else if (tbss) {
		ctx->layout.tbss_offset = ctx->layout.dot - saved;
		ctx->layout.dot = saved;
	}
	return changed;
}

static bool assign_addresses(void)
{
	bool changed = false, symchg = false;
	ctx->layout.dot = 0;
	ctx->layout.tbss_offset = 0;
	ctx->layout.current_output_section = AETHER;
	recorded.n = 0;
	assert_fail.n = 0;
	for (uint32_t i = 0; i < topcmds.n; i++) {
		Cmd *c = &topcmds.v[i];
		if (c->kind == C_ASSIGN) {
			symchg |= assign_symbol(c->as, false);
			continue;
		}
		if (!osecs.v[c->idx].live)
			continue;
		changed |= assign_offsets(c->idx, &symchg);
	}
	return changed || symchg;
}

/* processSymbolAssignments(): first definition of script symbols, dot = 0
   in the aether.  A section-relative value starts at the section's start. */
static void add_script_symbol(Assign *a)
{
	if (a->sym == NONE || !a->defined)
		return;
	G *s = &gsyms[a->sym];
	EV v = apply_op(a->op, gsym_ev(a->sym), eval(a->e));
	s->state = GS_SCRIPT;
	s->obj = NONE;
	s->bind = STB_GLOBAL;
	s->size = 0;
	s->type = v.type;
	if (a->hidden && visrank(s->vis) > visrank(STV_HIDDEN))
		s->vis = STV_HIDDEN;
	if (ev_isabs(v))
		s->ev = (EV){.kind = EV_ABS, .val = v.kind != EV_ABS ? 0 : ev_value(v), .align = 1};
	else
		s->ev = (EV){.kind = v.kind, .idx = v.idx, .val = 0, .align = 1};
}

static void process_symbol_assignments(void)
{
	ctx->layout.dot = 0;
	ctx->layout.current_output_section = AETHER;
	for (uint32_t i = 0; i < topcmds.n; i++) {
		Cmd *c = &topcmds.v[i];
		if (c->kind == C_ASSIGN) {
			add_script_symbol(c->as);
			continue;
		}
		OSec *os = &osecs.v[c->idx];
		for (uint32_t k = 0; k < os->cmds.n; k++)
			if (os->cmds.v[k].kind == C_ASSIGN)
				add_script_symbol(os->cmds.v[k].as);
	}
}
