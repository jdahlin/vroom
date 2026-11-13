/* Script symbol assignments and the address-assignment passes over output sections. */

/* ================= script symbols and address assignment ================= */

/* op= of a symbol assignment (lld readSymbolAssignment) */
static ld_expression_value ld_assignment_operator_apply(char op, ld_expression_value lhs, ld_expression_value v)
{
	uint64_t x, y;
	switch (op) {
	case '=': return v;
	case '+': return ld_expression_value_add(lhs, v);
	case '-': return ld_expression_value_subtract(lhs, v);
	}
	x = ld_expression_value_resolve(lhs);
	y = ld_expression_value_resolve(v);
	switch (op) {
	case '*': return ld_expression_value_absolute_make(x * y);
	case '/':
		if (!y)
			ld_error_report("division by zero");
		return ld_expression_value_absolute_make(y ? x / y : 0);
	case '<': return ld_expression_value_absolute_make(x << (y % 64));
	case '>': return ld_expression_value_absolute_make(x >> (y % 64));
	case '&': return ld_expression_value_absolute_make(x & y);
	case '^': return ld_expression_value_absolute_make(x ^ y);
	case '|': return ld_expression_value_absolute_make(x | y);
	}
	return v;
}


/* assignSymbol(); returns true if the symbol's (section, value) changed */
static bool ld_script_symbol_assign(ld_assignment *a, bool insec)
{
	if (a->assert_) {
		ld_expression_eval(a->e);
		return false;
	}
	if (a->sym == LD_NONE) {
		ld_expression_value v = ld_assignment_operator_apply(a->op, ld_dot_expression_value(), ld_expression_eval(a->e));
		uint64_t val = ld_expression_value_resolve(v);
		if (insec && val < ctx->layout.dot) {
			char *m = ld_region_alloc(160);
			snprintf(m, 160, "unable to move location counter (0x%llx) backward to 0x%llx for section '%s'",
				 (unsigned long long)ctx->layout.dot, (unsigned long long)val, ctx->script.sections.v[ctx->layout.current_output_section].name);
			VVEC_PUSH(ctx->script.address_errors, m, ld_region_alloc);
		}
		ctx->layout.dot = val;
		return false;
	}
	if (!a->defined)
		return false;
	ld_global_symbol *s = &ctx->symbols.globals[a->sym];
	ld_expression_value v = ld_assignment_operator_apply(a->op, ld_global_symbol_expression_value(a->sym), ld_expression_eval(a->e));
	ld_expression_value old = s->ev;
	if (ld_expression_value_is_absolute(v))
		s->ev = (ld_expression_value){.kind = LD_VALUE_KIND_ABSOLUTE, .val = ld_expression_value_resolve(v), .align = 1};
	else
		s->ev = (ld_expression_value){.kind = v.kind, .idx = v.idx, .val = ld_expression_value_section_offset(v), .align = 1};
	s->type = v.type;
	return old.kind != s->ev.kind || old.idx != s->ev.idx || old.val != s->ev.val;
}

/* assignOffsets(); returns true if the section address changed */
static bool ld_output_section_offsets_assign(uint32_t oi, bool *symchg)
{
	ld_output_section *os = &ctx->script.sections.v[oi];
	uint64_t saved = ctx->layout.dot;
	bool tbss = (os->flags & (SHF_ALLOC | SHF_TLS)) == (SHF_ALLOC | SHF_TLS) && os->type == SHT_NOBITS;
	if (tbss)
		ctx->layout.dot += ctx->layout.tbss_offset;
	if (!(os->flags & SHF_ALLOC))
		ctx->layout.dot = 0;
	else if (os->addr)
		ctx->layout.dot = ld_expression_value_resolve(ld_expression_eval(os->addr));
	ctx->layout.current_output_section = oi;
	if (!os->addr)
		ctx->layout.dot = ld_value_align_up(ctx->layout.dot, os->addralign);
	bool changed = os->va != ctx->layout.dot;
	os->va = ctx->layout.dot;
	for (uint32_t c = 0; c < os->cmds.n; c++) {
		ld_command *cm = &os->cmds.v[c];
		if (cm->kind == LD_COMMAND_ASSIGNMENT) {
			*symchg |= ld_script_symbol_assign(cm->as, true);
			continue;
		}
		ld_input_section_description *d = &ctx->script.input_descriptions.v[cm->idx];
		for (uint32_t k = 0; k < d->secs.n; k++) {
			ld_input_section *is = &ctx->inputs.sections.v[d->secs.v[k]];
			ctx->layout.dot = ld_value_align_up(ctx->layout.dot, is->align);
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

static bool ld_addresses_assign(void)
{
	bool changed = false, symchg = false;
	ctx->layout.dot = 0;
	ctx->layout.tbss_offset = 0;
	ctx->layout.current_output_section = LD_OUTPUT_SECTION_AETHER;
	ctx->script.address_errors.n = 0;
	ctx->script.assert_failures.n = 0;
	for (uint32_t i = 0; i < ctx->script.commands.n; i++) {
		ld_command *c = &ctx->script.commands.v[i];
		if (c->kind == LD_COMMAND_ASSIGNMENT) {
			symchg |= ld_script_symbol_assign(c->as, false);
			continue;
		}
		if (!ctx->script.sections.v[c->idx].live)
			continue;
		changed |= ld_output_section_offsets_assign(c->idx, &symchg);
	}
	return changed || symchg;
}

/* processSymbolAssignments(): first definition of script symbols, dot = 0
   in the aether.  A section-relative value starts at the section's start. */
static void ld_script_symbol_add(ld_assignment *a)
{
	if (a->sym == LD_NONE || !a->defined)
		return;
	ld_global_symbol *s = &ctx->symbols.globals[a->sym];
	ld_expression_value v = ld_assignment_operator_apply(a->op, ld_global_symbol_expression_value(a->sym), ld_expression_eval(a->e));
	s->state = LD_SYMBOL_STATE_SCRIPT;
	s->obj = LD_NONE;
	s->bind = STB_GLOBAL;
	s->size = 0;
	s->type = v.type;
	if (a->hidden && ld_visibility_rank(s->vis) > ld_visibility_rank(STV_HIDDEN))
		s->vis = STV_HIDDEN;
	if (ld_expression_value_is_absolute(v))
		s->ev = (ld_expression_value){.kind = LD_VALUE_KIND_ABSOLUTE, .val = v.kind != LD_VALUE_KIND_ABSOLUTE ? 0 : ld_expression_value_resolve(v), .align = 1};
	else
		s->ev = (ld_expression_value){.kind = v.kind, .idx = v.idx, .val = 0, .align = 1};
}

static void ld_symbol_assignments_process(void)
{
	ctx->layout.dot = 0;
	ctx->layout.current_output_section = LD_OUTPUT_SECTION_AETHER;
	for (uint32_t i = 0; i < ctx->script.commands.n; i++) {
		ld_command *c = &ctx->script.commands.v[i];
		if (c->kind == LD_COMMAND_ASSIGNMENT) {
			ld_script_symbol_add(c->as);
			continue;
		}
		ld_output_section *os = &ctx->script.sections.v[c->idx];
		for (uint32_t k = 0; k < os->cmds.n; k++)
			if (os->cmds.v[k].kind == LD_COMMAND_ASSIGNMENT)
				ld_script_symbol_add(os->cmds.v[k].as);
	}
}
