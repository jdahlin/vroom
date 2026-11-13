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
		if (insec && val < dot) {
			char *m = alloc(160);
			snprintf(m, 160, "unable to move location counter (0x%llx) backward to 0x%llx for section '%s'",
				 (unsigned long long)dot, (unsigned long long)val, osecs.v[cur_osec].name);
			VVEC_PUSH(recorded, m, alloc);
		}
		dot = val;
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

/* lld: .tbss takes no address space; consecutive ones stack from here */
static uint64_t tbss_off;

/* assignOffsets(); returns true if the section address changed */
static bool assign_offsets(uint32_t oi, bool *symchg)
{
	OSec *os = &osecs.v[oi];
	uint64_t saved = dot;
	bool tbss = (os->flags & (SHF_ALLOC | SHF_TLS)) == (SHF_ALLOC | SHF_TLS) && os->type == SHT_NOBITS;
	if (tbss)
		dot += tbss_off;
	if (!(os->flags & SHF_ALLOC))
		dot = 0;
	else if (os->addr)
		dot = ev_value(eval(os->addr));
	cur_osec = oi;
	if (!os->addr)
		dot = alignup(dot, os->addralign);
	bool changed = os->va != dot;
	os->va = dot;
	for (uint32_t c = 0; c < os->cmds.n; c++) {
		Cmd *cm = &os->cmds.v[c];
		if (cm->kind == C_ASSIGN) {
			*symchg |= assign_symbol(cm->as, true);
			continue;
		}
		ISD *d = &isds.v[cm->idx];
		for (uint32_t k = 0; k < d->secs.n; k++) {
			ISec *is = &isecs.v[d->secs.v[k]];
			dot = alignup(dot, is->align);
			is->off = dot - os->va;
			dot += is->size;
		}
	}
	os->size = dot - os->va;
	if (!(os->flags & SHF_ALLOC))
		dot = saved;
	else if (tbss) {
		tbss_off = dot - saved;
		dot = saved;
	}
	return changed;
}

static bool assign_addresses(void)
{
	bool changed = false, symchg = false;
	dot = 0;
	tbss_off = 0;
	cur_osec = AETHER;
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
	dot = 0;
	cur_osec = AETHER;
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
