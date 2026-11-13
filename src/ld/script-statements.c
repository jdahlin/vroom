/* The linker script's statements: assignments, PROVIDE, ASSERT, input and output section descriptions, SECTIONS, PHDRS, VERSION, and ld_script_parse. */

static uint32_t ld_output_section_find(const char *name)
{
	if (strcmp(name, "/DISCARD/"))
		for (uint32_t i = 0; i < ctx->script.sections.n; i++)
			if (!strcmp(ctx->script.sections.v[i].name, name))
				return i;
	ld_output_section o = {.name = name, .type = SHT_PROGBITS, .addralign = 1, .ptload = -1};
	o.discard = !strcmp(name, "/DISCARD/");
	VVEC_PUSH(ctx->script.sections, o, ld_region_alloc);
	return ctx->script.sections.n - 1;
}

/* After `name` was read: op= expr.  Returns NULL if not an assignment. */
static ld_assignment *ld_script_assignment_rest_parse(const char *name)
{
	char op[4];
	int n = ld_script_operator_peek(op);
	if (!n || !strchr(op, '=') || !strcmp(op, "==") || !strcmp(op, "<=") || !strcmp(op, ">=") || !strcmp(op, "!="))
		return NULL;
	ctx->script.cursor += n;
	ld_assignment *a = ld_region_alloc(sizeof *a);
	a->op = op[0] == '=' ? '=' : op[0];
	a->sym = strcmp(name, ".") ? ld_global_name_intern(name) : LD_NONE;
	a->e = ld_script_expression_parse();
	return a;
}

static ld_assignment *ld_script_provide_parse(bool hidden)
{
	ld_script_character_expect('(');
	char *name = ld_script_name_parse();
	ld_assignment *a;
	VVEC_PUSH(ctx->script.provides, ((ld_provide){.sym = ld_global_name_intern(name)}), ld_region_alloc);
	ctx->script.current_provide = &ctx->script.provides.v[ctx->script.provides.n - 1];
	a = ld_script_assignment_rest_parse(name);
	ctx->script.current_provide = NULL;
	if (!a) {
		ld_script_error_report("expected assignment");
		return NULL;
	}
	a->provide = true;
	a->hidden = hidden;
	ld_script_character_expect(')');
	return a;
}

static ld_assignment *ld_script_assert_parse(void)
{
	ld_assignment *a = ld_region_alloc(sizeof *a);
	a->sym = LD_NONE;
	a->op = '=';
	a->assert_ = true;
	ld_expression *e = ld_script_expression_create(LD_EXPRESSION_ASSERT);
	ld_script_character_expect('(');
	e->a = ld_script_expression_parse();
	ld_script_character_expect(',');
	e->msg = ld_script_name_parse();
	ld_script_character_expect(')');
	a->e = e;
	return a;
}

static void ld_script_input_parse(ld_output_section *os, uint32_t osi, const char *filepat, bool keep)
{
	if (strcmp(filepat, "*"))
		ld_error_report("linker script: file pattern '%s' not supported", filepat);
	ld_input_section_description d = {.osec = osi, .keep = keep, .thunksec = LD_NONE};
	VVEC(ld_pattern) pats = {0};
	ld_script_character_expect('(');
	while (!ld_script_is_at_end() && !ld_script_character_peek(')')) {
		ld_pattern p = {0};
		VVEC(const char *) g = {0};
		const char *save = ctx->script.cursor;
		char *w = ld_script_pattern_name_parse();
		if ((!strcmp(w, "SORT") || !strcmp(w, "SORT_BY_NAME") || !strcmp(w, "SORT_BY_ALIGNMENT")) && ld_script_character_peek('(')) {
			p.sort = w[5] == 'B' && w[8] == 'A' ? LD_SORT_ALIGNMENT : LD_SORT_NAME;
			ld_script_character_expect('(');
			while (!ld_script_is_at_end() && !ld_script_character_peek(')'))
				VVEC_PUSH(g, ld_script_pattern_name_parse(), ld_region_alloc);
			ld_script_character_expect(')');
		} else if (!strcmp(w, "EXCLUDE_FILE")) {
			ld_error_report("linker script: EXCLUDE_FILE not supported");
			ctx->script.cursor = save;
			return;
		} else {
			VVEC_PUSH(g, w, ld_region_alloc);
			/* consecutive plain globs belong to the same pattern */
			for (;;) {
				const char *s2 = ctx->script.cursor;
				ld_script_whitespace_skip();
				if (ld_script_character_peek(')'))
					break;
				char *w2 = ld_script_pattern_name_parse();
				if (!strncmp(w2, "SORT", 4) || !strcmp(w2, "EXCLUDE_FILE")) {
					ctx->script.cursor = s2;
					break;
				}
				VVEC_PUSH(g, w2, ld_region_alloc);
			}
		}
		p.globs = g.v;
		p.nglobs = g.n;
		VVEC_PUSH(pats, p, ld_region_alloc);
	}
	ld_script_character_expect(')');
	d.pats = pats.v;
	d.npats = pats.n;
	VVEC_PUSH(ctx->script.input_descriptions, d, ld_region_alloc);
	ld_command c = {.kind = LD_COMMAND_INPUT_SECTION_DESCRIPTION, .idx = ctx->script.input_descriptions.n - 1};
	VVEC_PUSH(os->cmds, c, ld_region_alloc);
}

static void ld_script_output_section_parse(const char *name)
{
	uint32_t osi = ld_output_section_find(name);
	uint32_t nref0 = ctx->script.referenced_symbols.n;
	ld_output_section *os;
	ld_expression *addr = NULL;
	bool info = false, noload = false;
	/* optional address and (TYPE) */
	if (!ld_script_character_peek(':')) {
		if (ld_script_character_peek('(')) {
			const char *save = ctx->script.cursor;
			ctx->script.cursor++;
			char *t = ld_script_pattern_name_parse();
			if (!strcmp(t, "INFO") || !strcmp(t, "NOLOAD") || !strcmp(t, "COPY") || !strcmp(t, "OVERLAY")) {
				ld_script_character_expect(')');
				info = strcmp(t, "NOLOAD");
				noload = !info;
			} else {
				ctx->script.cursor = save;
				addr = ld_script_expression_parse();
			}
		} else {
			addr = ld_script_expression_parse();
			if (ld_script_character_peek('(')) {
				ctx->script.cursor++;
				char *t = ld_script_pattern_name_parse();
				ld_script_character_expect(')');
				info = strcmp(t, "NOLOAD");
				noload = !info;
			}
		}
	}
	ld_script_character_expect(':');
	os = &ctx->script.sections.v[osi];
	os->addr = addr;
	os->nonalloc |= info;
	os->noload |= noload;
	for (;;) {
		if (ld_script_word_consume("AT")) { ld_expression *e = ld_script_parenthesized_expression_parse(); ctx->script.sections.v[osi].lma = e; continue; }
		if (ld_script_word_consume("ALIGN")) { ld_expression *e = ld_script_parenthesized_expression_parse(); ctx->script.sections.v[osi].alignx = e; continue; }
		if (ld_script_word_consume("SUBALIGN")) { ld_expression *e = ld_script_parenthesized_expression_parse(); ctx->script.sections.v[osi].subalign = e; continue; }
		break;
	}
	ld_script_character_expect('{');
	while (!ld_script_is_at_end() && !ld_script_character_peek('}')) {
		if (ld_script_character_consume(';'))
			continue;
		if (ld_script_word_consume("KEEP")) {
			ld_script_character_expect('(');
			char *fp = ld_script_pattern_name_parse();
			ld_script_input_parse(&ctx->script.sections.v[osi], osi, fp, true);
			ld_script_character_expect(')');
			continue;
		}
		if (ld_script_word_consume("CONSTRUCTORS"))
			continue;
		if (ld_script_word_consume("ASSERT")) {
			ld_assignment *a = ld_script_assert_parse();
			ld_command c = {.kind = LD_COMMAND_ASSIGNMENT, .as = a};
			VVEC_PUSH(ctx->script.sections.v[osi].cmds, c, ld_region_alloc);
			ld_script_character_consume(';');
			continue;
		}
		int pk = ld_script_word_consume("PROVIDE") ? 1 : ld_script_word_consume("PROVIDE_HIDDEN") ? 2 : ld_script_word_consume("HIDDEN") ? 3 : 0;
		if (pk) {
			bool hid = pk >= 2, prov = pk <= 2;
			ld_assignment *a = ld_script_provide_parse(hid);
			if (a) {
				a->provide = prov;
				ld_command c = {.kind = LD_COMMAND_ASSIGNMENT, .as = a};
				VVEC_PUSH(ctx->script.sections.v[osi].cmds, c, ld_region_alloc);
			}
			ld_script_character_expect(';');
			continue;
		}
		const char *save = ctx->script.cursor;
		char *w = ld_script_name_parse();
		ld_assignment *a = ld_script_assignment_rest_parse(w);
		if (a) {
			ld_command c = {.kind = LD_COMMAND_ASSIGNMENT, .as = a};
			VVEC_PUSH(ctx->script.sections.v[osi].cmds, c, ld_region_alloc);
			ld_script_character_expect(';');
			continue;
		}
		ctx->script.cursor = save;
		w = ld_script_pattern_name_parse();
		if (!*w) {
			ld_script_error_report("unexpected token in output section");
			ctx->script.cursor = ctx->script.end;
			break;
		}
		ld_script_input_parse(&ctx->script.sections.v[osi], osi, w, false);
	}
	ld_script_character_expect('}');
	/* trailers: >region, AT>region, :phdr, =fill */
	for (;;) {
		ld_script_whitespace_skip();
		if (ctx->script.cursor < ctx->script.end && *ctx->script.cursor == '>') {
			ld_error_report("linker script: memory regions not supported in %s", name);
			ctx->script.cursor = ctx->script.end;
			break;
		}
		if (ctx->script.cursor < ctx->script.end && *ctx->script.cursor == '=') {
			ctx->script.cursor++;
			ctx->script.sections.v[osi].fill = ld_script_primary_expression_parse();	/* lld readFill: a primary */
			continue;
		}
		if (ld_script_character_peek(':') && ctx->script.cursor + 1 < ctx->script.end && ctx->script.cursor[1] != ':') {
			ctx->script.cursor++;
			ld_output_section *o = &ctx->script.sections.v[osi];
			if (o->nphn == 4)
				ld_script_error_report("too many :phdr names");
			else
				o->phn[o->nphn++] = ld_script_name_parse();
			continue;
		}
		break;
	}
	ld_script_character_consume(',');
	if (ctx->script.referenced_symbols.n > nref0)
		ctx->script.sections.v[osi].uses_syms = true;
	ld_command c = {.kind = LD_COMMAND_OUTPUT_SECTION, .idx = osi};
	VVEC_PUSH(ctx->script.commands, c, ld_region_alloc);
}

static void ld_script_sections_parse(void)
{
	ld_script_character_expect('{');
	while (!ld_script_is_at_end() && !ld_script_character_peek('}')) {
		if (ld_script_character_consume(';'))
			continue;
		if (ld_script_word_consume("ASSERT")) {
			ld_assignment *a = ld_script_assert_parse();
			VVEC_PUSH(ctx->script.commands, ((ld_command){.kind = LD_COMMAND_ASSIGNMENT, .as = a}), ld_region_alloc);
			ld_script_character_consume(';');
			continue;
		}
		if (ld_script_word_consume("PROVIDE_HIDDEN") || ld_script_word_consume("HIDDEN")) {
			ld_assignment *a = ld_script_provide_parse(true);
			if (a) VVEC_PUSH(ctx->script.commands, ((ld_command){.kind = LD_COMMAND_ASSIGNMENT, .as = a}), ld_region_alloc);
			ld_script_character_expect(';');
			continue;
		}
		if (ld_script_word_consume("PROVIDE")) {
			ld_assignment *a = ld_script_provide_parse(false);
			if (a) VVEC_PUSH(ctx->script.commands, ((ld_command){.kind = LD_COMMAND_ASSIGNMENT, .as = a}), ld_region_alloc);
			ld_script_character_expect(';');
			continue;
		}
		const char *save = ctx->script.cursor;
		char *w = ld_script_name_parse();
		ld_assignment *a = ld_script_assignment_rest_parse(w);
		if (a) {
			VVEC_PUSH(ctx->script.commands, ((ld_command){.kind = LD_COMMAND_ASSIGNMENT, .as = a}), ld_region_alloc);
			ld_script_character_expect(';');
			continue;
		}
		if (!*w) {
			ctx->script.cursor = save;
			ld_script_error_report("unexpected token in SECTIONS");
			ctx->script.cursor = ctx->script.end;
			return;
		}
		ld_script_output_section_parse(w);
	}
	ld_script_character_expect('}');
}

static void ld_script_program_headers_parse(void)
{
	static const struct { const char *n; uint32_t t; } pt[] = {
		{"PT_NULL", 0}, {"PT_LOAD", 1}, {"PT_DYNAMIC", 2}, {"PT_INTERP", 3}, {"PT_NOTE", 4},
		{"PT_SHLIB", 5}, {"PT_PHDR", 6}, {"PT_TLS", 7}, {"PT_GNU_EH_FRAME", 0x6474e550},
		{"PT_GNU_STACK", 0x6474e551}, {"PT_GNU_RELRO", 0x6474e552}, {"PT_GNU_PROPERTY", 0x6474e553},
	};
	ctx->script.has_phdrs = true;
	ld_script_character_expect('{');
	while (!ld_script_is_at_end() && !ld_script_character_peek('}')) {
		if (ld_script_character_consume(';'))
			continue;
		ld_script_segment h = {.name = ld_script_name_parse()};
		char *t = ld_script_name_parse();
		bool ok = false;
		for (size_t i = 0; i < sizeof pt / sizeof pt[0] && !ok; i++)
			if (!strcmp(t, pt[i].n))
				h.type = pt[i].t, ok = true;
		if (!ok && *t >= '0' && *t <= '9')
			h.type = (uint32_t)strtoul(t, NULL, 0), ok = true;
		if (!ok) {
			ld_script_error_report("unknown program header type");
			ctx->script.cursor = ctx->script.end;
			return;
		}
		for (;;) {
			if (ld_script_word_consume("FILEHDR")) { h.filehdr = true; continue; }
			if (ld_script_word_consume("PHDRS")) { h.phdrs = true; continue; }
			if (ld_script_word_consume("FLAGS")) { h.flags = ld_script_parenthesized_expression_parse(); continue; }
			if (ld_script_word_consume("AT")) { ld_script_error_report("PHDRS AT() not supported"); ctx->script.cursor = ctx->script.end; return; }
			break;
		}
		VVEC_PUSH(ctx->script.segments, h, ld_region_alloc);
		ld_script_character_expect(';');
	}
	ld_script_character_expect('}');
}

static void ld_script_version_parse(void)
{
	ld_script_character_expect('{');
	while (!ld_script_is_at_end() && !ld_script_character_peek('}')) {
		ld_version_node v = {.name = ""};
		if (!ld_script_character_peek('{'))
			v.name = ld_script_name_parse();
		ld_script_character_expect('{');
		bool local = false;
		while (!ld_script_is_at_end() && !ld_script_character_peek('}')) {
			if (ld_script_character_consume(';'))
				continue;
			if (ld_script_word_consume("global")) { ld_script_character_expect(':'); local = false; continue; }
			if (ld_script_word_consume("local")) { ld_script_character_expect(':'); local = true; continue; }
			if (ld_script_word_consume("extern")) { ld_script_error_report("VERSION extern not supported"); ctx->script.cursor = ctx->script.end; return; }
			char *n = ld_script_name_parse();
			if (!*n) {
				ld_script_error_report("unexpected token in VERSION");
				ctx->script.cursor = ctx->script.end;
				return;
			}
			if (local)
				VVEC_PUSH(ctx->script.version_locals, n, ld_region_alloc);
			else
				VVEC_PUSH(v.globs, n, ld_region_alloc);
			ld_script_character_expect(';');
		}
		ld_script_character_expect('}');
		while (!ld_script_is_at_end() && !ld_script_character_peek(';'))	/* dependencies */
			if (!*ld_script_name_parse()) {
				ld_script_error_report("unexpected token in VERSION");
				ctx->script.cursor = ctx->script.end;
				return;
			}
		ld_script_character_expect(';');
		if (*v.name)
			ctx->script.version_definition_count++;
		VVEC_PUSH(ctx->script.version_nodes, v, ld_region_alloc);
	}
	ld_script_character_expect('}');
}

static void ld_script_parse(const char *text, size_t len)
{
	ctx->script.cursor = ctx->script.file = text;
	ctx->script.end = text + len;
	while (!ld_script_is_at_end()) {
		if (ld_script_character_consume(';'))
			continue;
		if (ld_script_word_consume("SECTIONS")) { ld_script_sections_parse(); continue; }
		if (ld_script_word_consume("PHDRS")) { ld_script_program_headers_parse(); continue; }
		if (ld_script_word_consume("VERSION")) { ld_script_version_parse(); continue; }
		if (ld_script_word_consume("OUTPUT_ARCH") || ld_script_word_consume("OUTPUT_FORMAT") || ld_script_word_consume("TARGET") || ld_script_word_consume("SEARCH_DIR")) {
			ld_script_character_expect('(');
			while (!ld_script_is_at_end() && !ld_script_character_peek(')'))
				ctx->script.cursor++;
			ld_script_character_expect(')');
			continue;
		}
		if (ld_script_word_consume("ENTRY")) {
			ld_script_character_expect('(');
			ctx->script.entry = ld_script_name_parse();
			ld_script_character_expect(')');
			continue;
		}
		if (ld_script_word_consume("ASSERT")) {
			ld_assignment *a = ld_script_assert_parse();
			VVEC_PUSH(ctx->script.commands, ((ld_command){.kind = LD_COMMAND_ASSIGNMENT, .as = a}), ld_region_alloc);
			ld_script_character_consume(';');
			continue;
		}
		if (ld_script_word_consume("PROVIDE_HIDDEN") || ld_script_word_consume("HIDDEN")) {
			ld_assignment *a = ld_script_provide_parse(true);
			if (a) VVEC_PUSH(ctx->script.commands, ((ld_command){.kind = LD_COMMAND_ASSIGNMENT, .as = a}), ld_region_alloc);
			ld_script_character_expect(';');
			continue;
		}
		if (ld_script_word_consume("PROVIDE")) {
			ld_assignment *a = ld_script_provide_parse(false);
			if (a) VVEC_PUSH(ctx->script.commands, ((ld_command){.kind = LD_COMMAND_ASSIGNMENT, .as = a}), ld_region_alloc);
			ld_script_character_expect(';');
			continue;
		}
		const char *save = ctx->script.cursor;
		char *w = ld_script_name_parse();
		ld_assignment *a = ld_script_assignment_rest_parse(w);
		if (a) {
			VVEC_PUSH(ctx->script.commands, ((ld_command){.kind = LD_COMMAND_ASSIGNMENT, .as = a}), ld_region_alloc);
			ld_script_character_expect(';');
			continue;
		}
		ctx->script.cursor = save;
		ld_script_error_report("unsupported linker script command");
		return;
	}
}
