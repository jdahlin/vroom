/* The linker script's statements: assignments, PROVIDE, ASSERT, input and output section descriptions, SECTIONS, PHDRS, VERSION, and ld_script_parse. */

#include "ld-internal.h"

VPRIV uint32_t ld_output_section_find(struct ld_context *ctx, const char *name)
{
	if (strcmp(name, "/DISCARD/"))
		for (uint32_t i = 0; i < ctx->script.sections.n; i++)
			if (!strcmp(ctx->script.sections.v[i].name, name))
				return i;
	ld_output_section o = {.name = name, .type = SHT_PROGBITS, .address_alignment = 1, .load_segment = -1};
	o.discard = !strcmp(name, "/DISCARD/");
	VVEC_PUSH(ctx->script.sections, o, ld_region_alloc);
	return ctx->script.sections.n - 1;
}

/* After `name` was read: op= expr.  Returns NULL if not an assignment. */
static ld_assignment *ld_script_assignment_rest_parse(struct ld_context *ctx, const char *name)
{
	char op[4];
	int n = ld_script_operator_peek(ctx, op);
	if (!n || !strchr(op, '=') || !strcmp(op, "==") || !strcmp(op, "<=") || !strcmp(op, ">=") || !strcmp(op, "!="))
		return NULL;
	ctx->script.cursor += n;
	ld_assignment *a = ld_region_alloc(sizeof *a);
	a->operator_character = op[0] == '=' ? '=' : op[0];
	a->symbol = strcmp(name, ".") ? ld_global_name_intern(ctx, name) : LD_NONE;
	a->expression = ld_script_expression_parse(ctx);
	return a;
}

static ld_assignment *ld_script_provide_parse(struct ld_context *ctx, bool hidden)
{
	ld_script_character_expect(ctx, '(');
	char *name = ld_script_name_parse(ctx);
	ld_assignment *a;
	VVEC_PUSH(ctx->script.provides, ((ld_provide){.symbol = ld_global_name_intern(ctx, name)}), ld_region_alloc);
	ctx->script.current_provide = &ctx->script.provides.v[ctx->script.provides.n - 1];
	a = ld_script_assignment_rest_parse(ctx, name);
	ctx->script.current_provide = NULL;
	if (!a) {
		ld_script_error_report(ctx, "expected assignment");
		return NULL;
	}
	a->provide = true;
	a->hidden = hidden;
	ld_script_character_expect(ctx, ')');
	return a;
}

static ld_assignment *ld_script_assert_parse(struct ld_context *ctx)
{
	ld_assignment *a = ld_region_alloc(sizeof *a);
	a->symbol = LD_NONE;
	a->operator_character = '=';
	a->is_assert = true;
	ld_expression *e = ld_script_expression_create(LD_EXPRESSION_ASSERT);
	ld_script_character_expect(ctx, '(');
	e->first_operand = ld_script_expression_parse(ctx);
	ld_script_character_expect(ctx, ',');
	e->message = ld_script_name_parse(ctx);
	ld_script_character_expect(ctx, ')');
	a->expression = e;
	return a;
}

static void ld_script_input_parse(struct ld_context *ctx, ld_output_section *os, uint32_t osi, const char *filepat, bool keep)
{
	if (strcmp(filepat, "*"))
		ld_error_report(ctx, "linker script: file pattern '%s' not supported", filepat);
	ld_input_section_description d = {.output_section = osi, .keep = keep, .thunk_section = LD_NONE};
	VVEC(ld_pattern) pats = {0};
	ld_script_character_expect(ctx, '(');
	while (!ld_script_is_at_end(ctx) && !ld_script_character_peek(ctx, ')')) {
		ld_pattern p = {0};
		VVEC(const char *) g = {0};
		const char *save = ctx->script.cursor;
		char *w = ld_script_pattern_name_parse(ctx);
		if ((!strcmp(w, "SORT") || !strcmp(w, "SORT_BY_NAME") || !strcmp(w, "SORT_BY_ALIGNMENT")) && ld_script_character_peek(ctx, '(')) {
			p.sort = w[5] == 'B' && w[8] == 'A' ? LD_SORT_ALIGNMENT : LD_SORT_NAME;
			ld_script_character_expect(ctx, '(');
			while (!ld_script_is_at_end(ctx) && !ld_script_character_peek(ctx, ')'))
				VVEC_PUSH(g, ld_script_pattern_name_parse(ctx), ld_region_alloc);
			ld_script_character_expect(ctx, ')');
		} else if (!strcmp(w, "EXCLUDE_FILE")) {
			ld_error_report(ctx, "linker script: EXCLUDE_FILE not supported");
			ctx->script.cursor = save;
			return;
		} else {
			VVEC_PUSH(g, w, ld_region_alloc);
			/* consecutive plain globs belong to the same pattern */
			for (;;) {
				const char *s2 = ctx->script.cursor;
				ld_script_whitespace_skip(ctx);
				if (ld_script_character_peek(ctx, ')'))
					break;
				char *w2 = ld_script_pattern_name_parse(ctx);
				if (!strncmp(w2, "SORT", 4) || !strcmp(w2, "EXCLUDE_FILE")) {
					ctx->script.cursor = s2;
					break;
				}
				VVEC_PUSH(g, w2, ld_region_alloc);
			}
		}
		p.globs = g.v;
		p.glob_count = g.n;
		VVEC_PUSH(pats, p, ld_region_alloc);
	}
	ld_script_character_expect(ctx, ')');
	d.patterns = pats.v;
	d.pattern_count = pats.n;
	VVEC_PUSH(ctx->script.input_descriptions, d, ld_region_alloc);
	ld_command c = {.kind = LD_COMMAND_INPUT_SECTION_DESCRIPTION, .index = ctx->script.input_descriptions.n - 1};
	VVEC_PUSH(os->commands, c, ld_region_alloc);
}

static void ld_script_output_section_parse(struct ld_context *ctx, const char *name)
{
	uint32_t osi = ld_output_section_find(ctx, name);
	uint32_t nref0 = ctx->script.referenced_symbols.n;
	ld_output_section *os;
	ld_expression *addr = NULL;
	bool info = false, noload = false;
	/* optional address and (TYPE) */
	if (!ld_script_character_peek(ctx, ':')) {
		if (ld_script_character_peek(ctx, '(')) {
			const char *save = ctx->script.cursor;
			ctx->script.cursor++;
			char *t = ld_script_pattern_name_parse(ctx);
			if (!strcmp(t, "INFO") || !strcmp(t, "NOLOAD") || !strcmp(t, "COPY") || !strcmp(t, "OVERLAY")) {
				ld_script_character_expect(ctx, ')');
				info = strcmp(t, "NOLOAD");
				noload = !info;
			} else {
				ctx->script.cursor = save;
				addr = ld_script_expression_parse(ctx);
			}
		} else {
			addr = ld_script_expression_parse(ctx);
			if (ld_script_character_peek(ctx, '(')) {
				ctx->script.cursor++;
				char *t = ld_script_pattern_name_parse(ctx);
				ld_script_character_expect(ctx, ')');
				info = strcmp(t, "NOLOAD");
				noload = !info;
			}
		}
	}
	ld_script_character_expect(ctx, ':');
	os = &ctx->script.sections.v[osi];
	os->address_expression = addr;
	os->not_allocated |= info;
	os->noload |= noload;
	for (;;) {
		if (ld_script_word_consume(ctx, "AT")) { ld_expression *e = ld_script_parenthesized_expression_parse(ctx); ctx->script.sections.v[osi].load_address_expression = e; continue; }
		if (ld_script_word_consume(ctx, "ALIGN")) { ld_expression *e = ld_script_parenthesized_expression_parse(ctx); ctx->script.sections.v[osi].align_expression = e; continue; }
		if (ld_script_word_consume(ctx, "SUBALIGN")) { ld_expression *e = ld_script_parenthesized_expression_parse(ctx); ctx->script.sections.v[osi].subalign_expression = e; continue; }
		break;
	}
	ld_script_character_expect(ctx, '{');
	while (!ld_script_is_at_end(ctx) && !ld_script_character_peek(ctx, '}')) {
		if (ld_script_character_consume(ctx, ';'))
			continue;
		if (ld_script_word_consume(ctx, "KEEP")) {
			ld_script_character_expect(ctx, '(');
			char *fp = ld_script_pattern_name_parse(ctx);
			ld_script_input_parse(ctx, &ctx->script.sections.v[osi], osi, fp, true);
			ld_script_character_expect(ctx, ')');
			continue;
		}
		if (ld_script_word_consume(ctx, "CONSTRUCTORS"))
			continue;
		if (ld_script_word_consume(ctx, "ASSERT")) {
			ld_assignment *a = ld_script_assert_parse(ctx);
			ld_command c = {.kind = LD_COMMAND_ASSIGNMENT, .assignment = a};
			VVEC_PUSH(ctx->script.sections.v[osi].commands, c, ld_region_alloc);
			ld_script_character_consume(ctx, ';');
			continue;
		}
		int pk = ld_script_word_consume(ctx, "PROVIDE") ? 1 : ld_script_word_consume(ctx, "PROVIDE_HIDDEN") ? 2 : ld_script_word_consume(ctx, "HIDDEN") ? 3 : 0;
		if (pk) {
			bool hid = pk >= 2, prov = pk <= 2;
			ld_assignment *a = ld_script_provide_parse(ctx, hid);
			if (a) {
				a->provide = prov;
				ld_command c = {.kind = LD_COMMAND_ASSIGNMENT, .assignment = a};
				VVEC_PUSH(ctx->script.sections.v[osi].commands, c, ld_region_alloc);
			}
			ld_script_character_expect(ctx, ';');
			continue;
		}
		const char *save = ctx->script.cursor;
		char *w = ld_script_name_parse(ctx);
		ld_assignment *a = ld_script_assignment_rest_parse(ctx, w);
		if (a) {
			ld_command c = {.kind = LD_COMMAND_ASSIGNMENT, .assignment = a};
			VVEC_PUSH(ctx->script.sections.v[osi].commands, c, ld_region_alloc);
			ld_script_character_expect(ctx, ';');
			continue;
		}
		ctx->script.cursor = save;
		w = ld_script_pattern_name_parse(ctx);
		if (!*w) {
			ld_script_error_report(ctx, "unexpected token in output section");
			ctx->script.cursor = ctx->script.end;
			break;
		}
		ld_script_input_parse(ctx, &ctx->script.sections.v[osi], osi, w, false);
	}
	ld_script_character_expect(ctx, '}');
	/* trailers: >region, AT>region, :phdr, =fill */
	for (;;) {
		ld_script_whitespace_skip(ctx);
		if (ctx->script.cursor < ctx->script.end && *ctx->script.cursor == '>') {
			ld_error_report(ctx, "linker script: memory regions not supported in %s", name);
			ctx->script.cursor = ctx->script.end;
			break;
		}
		if (ctx->script.cursor < ctx->script.end && *ctx->script.cursor == '=') {
			ctx->script.cursor++;
			ctx->script.sections.v[osi].fill = ld_script_primary_expression_parse(ctx);	/* lld readFill: a primary */
			continue;
		}
		if (ld_script_character_peek(ctx, ':') && ctx->script.cursor + 1 < ctx->script.end && ctx->script.cursor[1] != ':') {
			ctx->script.cursor++;
			ld_output_section *o = &ctx->script.sections.v[osi];
			if (o->program_header_name_count == 4)
				ld_script_error_report(ctx, "too many :phdr names");
			else
				o->program_header_names[o->program_header_name_count++] = ld_script_name_parse(ctx);
			continue;
		}
		break;
	}
	ld_script_character_consume(ctx, ',');
	if (ctx->script.referenced_symbols.n > nref0)
		ctx->script.sections.v[osi].uses_symbols = true;
	ld_command c = {.kind = LD_COMMAND_OUTPUT_SECTION, .index = osi};
	VVEC_PUSH(ctx->script.commands, c, ld_region_alloc);
}

/* a command that is an assignment, at the top level or in SECTIONS: ASSERT,
   PROVIDE, PROVIDE_HIDDEN or HIDDEN, or `name op expression;`.  True when it
   was one (appended to the commands); else *name is the name read ("" if
   none) and *start where it began */
static bool ld_script_assignment_statement_parse(struct ld_context *ctx, char **name, const char **start)
{
	ld_assignment *a;
	*name = NULL;
	*start = ctx->script.cursor;
	if (ld_script_word_consume(ctx, "ASSERT")) {
		a = ld_script_assert_parse(ctx);
		VVEC_PUSH(ctx->script.commands, ((ld_command){.kind = LD_COMMAND_ASSIGNMENT, .assignment = a}), ld_region_alloc);
		ld_script_character_consume(ctx, ';');
		return true;
	}
	if (ld_script_word_consume(ctx, "PROVIDE_HIDDEN") || ld_script_word_consume(ctx, "HIDDEN"))
		a = ld_script_provide_parse(ctx, true);
	else if (ld_script_word_consume(ctx, "PROVIDE"))
		a = ld_script_provide_parse(ctx, false);
	else {
		*start = ctx->script.cursor;
		*name = ld_script_name_parse(ctx);
		a = ld_script_assignment_rest_parse(ctx, *name);
		if (!a)
			return false;
	}
	if (a)
		VVEC_PUSH(ctx->script.commands, ((ld_command){.kind = LD_COMMAND_ASSIGNMENT, .assignment = a}), ld_region_alloc);
	ld_script_character_expect(ctx, ';');
	return true;
}

static void ld_script_sections_parse(struct ld_context *ctx)
{
	ld_script_character_expect(ctx, '{');
	while (!ld_script_is_at_end(ctx) && !ld_script_character_peek(ctx, '}')) {
		if (ld_script_character_consume(ctx, ';'))
			continue;
		const char *save;
		char *w;
		if (ld_script_assignment_statement_parse(ctx, &w, &save))
			continue;
		if (!*w) {
			ctx->script.cursor = save;
			ld_script_error_report(ctx, "unexpected token in SECTIONS");
			ctx->script.cursor = ctx->script.end;
			return;
		}
		ld_script_output_section_parse(ctx, w);
	}
	ld_script_character_expect(ctx, '}');
}

static void ld_script_program_headers_parse(struct ld_context *ctx)
{
	static const struct { const char *n; uint32_t t; } pt[] = {
		{"PT_NULL", 0}, {"PT_LOAD", 1}, {"PT_DYNAMIC", 2}, {"PT_INTERP", 3}, {"PT_NOTE", 4},
		{"PT_SHLIB", 5}, {"PT_PHDR", 6}, {"PT_TLS", 7}, {"PT_GNU_EH_FRAME", 0x6474e550},
		{"PT_GNU_STACK", 0x6474e551}, {"PT_GNU_RELRO", 0x6474e552}, {"PT_GNU_PROPERTY", 0x6474e553},
	};
	ctx->script.has_phdrs = true;
	ld_script_character_expect(ctx, '{');
	while (!ld_script_is_at_end(ctx) && !ld_script_character_peek(ctx, '}')) {
		if (ld_script_character_consume(ctx, ';'))
			continue;
		ld_script_segment h = {.name = ld_script_name_parse(ctx)};
		char *t = ld_script_name_parse(ctx);
		bool ok = false;
		for (size_t i = 0; i < sizeof pt / sizeof pt[0] && !ok; i++)
			if (!strcmp(t, pt[i].n))
				h.type = pt[i].t, ok = true;
		if (!ok && *t >= '0' && *t <= '9')
			h.type = (uint32_t)strtoul(t, NULL, 0), ok = true;
		if (!ok) {
			ld_script_error_report(ctx, "unknown program header type");
			ctx->script.cursor = ctx->script.end;
			return;
		}
		for (;;) {
			if (ld_script_word_consume(ctx, "FILEHDR")) { h.file_header = true; continue; }
			if (ld_script_word_consume(ctx, "PHDRS")) { h.program_headers = true; continue; }
			if (ld_script_word_consume(ctx, "FLAGS")) { h.flags = ld_script_parenthesized_expression_parse(ctx); continue; }
			if (ld_script_word_consume(ctx, "AT")) { ld_script_error_report(ctx, "PHDRS AT() not supported"); ctx->script.cursor = ctx->script.end; return; }
			break;
		}
		VVEC_PUSH(ctx->script.segments, h, ld_region_alloc);
		ld_script_character_expect(ctx, ';');
	}
	ld_script_character_expect(ctx, '}');
}

static void ld_script_version_parse(struct ld_context *ctx)
{
	ld_script_character_expect(ctx, '{');
	while (!ld_script_is_at_end(ctx) && !ld_script_character_peek(ctx, '}')) {
		ld_version_node v = {.name = ""};
		if (!ld_script_character_peek(ctx, '{'))
			v.name = ld_script_name_parse(ctx);
		ld_script_character_expect(ctx, '{');
		bool local = false;
		while (!ld_script_is_at_end(ctx) && !ld_script_character_peek(ctx, '}')) {
			if (ld_script_character_consume(ctx, ';'))
				continue;
			if (ld_script_word_consume(ctx, "global")) { ld_script_character_expect(ctx, ':'); local = false; continue; }
			if (ld_script_word_consume(ctx, "local")) { ld_script_character_expect(ctx, ':'); local = true; continue; }
			if (ld_script_word_consume(ctx, "extern")) { ld_script_error_report(ctx, "VERSION extern not supported"); ctx->script.cursor = ctx->script.end; return; }
			char *n = ld_script_name_parse(ctx);
			if (!*n) {
				ld_script_error_report(ctx, "unexpected token in VERSION");
				ctx->script.cursor = ctx->script.end;
				return;
			}
			if (local)
				VVEC_PUSH(ctx->script.version_locals, n, ld_region_alloc);
			else
				VVEC_PUSH(v.globs, n, ld_region_alloc);
			ld_script_character_expect(ctx, ';');
		}
		ld_script_character_expect(ctx, '}');
		while (!ld_script_is_at_end(ctx) && !ld_script_character_peek(ctx, ';'))	/* dependencies */
			if (!*ld_script_name_parse(ctx)) {
				ld_script_error_report(ctx, "unexpected token in VERSION");
				ctx->script.cursor = ctx->script.end;
				return;
			}
		ld_script_character_expect(ctx, ';');
		if (*v.name)
			ctx->script.version_definition_count++;
		VVEC_PUSH(ctx->script.version_nodes, v, ld_region_alloc);
	}
	ld_script_character_expect(ctx, '}');
}

VPRIV void ld_script_parse(struct ld_context *ctx, const char *text, size_t len)
{
	ctx->script.cursor = ctx->script.file = text;
	ctx->script.end = text + len;
	while (!ld_script_is_at_end(ctx)) {
		if (ld_script_character_consume(ctx, ';'))
			continue;
		if (ld_script_word_consume(ctx, "SECTIONS")) { ld_script_sections_parse(ctx); continue; }
		if (ld_script_word_consume(ctx, "PHDRS")) { ld_script_program_headers_parse(ctx); continue; }
		if (ld_script_word_consume(ctx, "VERSION")) { ld_script_version_parse(ctx); continue; }
		if (ld_script_word_consume(ctx, "OUTPUT_ARCH") || ld_script_word_consume(ctx, "OUTPUT_FORMAT") || ld_script_word_consume(ctx, "TARGET") || ld_script_word_consume(ctx, "SEARCH_DIR")) {
			ld_script_character_expect(ctx, '(');
			while (!ld_script_is_at_end(ctx) && !ld_script_character_peek(ctx, ')'))
				ctx->script.cursor++;
			ld_script_character_expect(ctx, ')');
			continue;
		}
		if (ld_script_word_consume(ctx, "ENTRY")) {
			ld_script_character_expect(ctx, '(');
			ctx->script.entry = ld_script_name_parse(ctx);
			ld_script_character_expect(ctx, ')');
			continue;
		}
		const char *save;
		char *w;
		if (ld_script_assignment_statement_parse(ctx, &w, &save))
			continue;
		ctx->script.cursor = save;
		ld_script_error_report(ctx, "unsupported linker script command");
		return;
	}
}
