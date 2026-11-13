/* The linker script's expression parser (precedence climbing) and its primaries. */

static uint32_t ld_output_section_find(const char *name);
static ld_expression *ld_script_expression_parse(void);

static ld_expression *ld_script_expression_create(uint8_t op)
{
	ld_expression *e = ld_region_alloc(sizeof *e);
	e->op = op;
	return e;
}


static void ld_script_symbol_reference_note(uint32_t g)
{
	if (ctx->script.current_provide)
		VVEC_PUSH(ctx->script.current_provide->refs, g, ld_region_alloc);
	else
		VVEC_PUSH(ctx->script.referenced_symbols, g, ld_region_alloc);
}

static int ld_script_operator_precedence(const char *op)
{
	char c = op[0], c1 = c ? op[1] : 0;
	if (c1 && op[2])
		return -1;
	switch (c) {
	case '*': case '/': case '%': return c1 ? -1 : 11;
	case '+': case '-': return c1 ? -1 : 10;
	case '<': case '>': return c1 == c ? 9 : !c1 || c1 == '=' ? 8 : -1;
	case '=': case '!': return c1 == '=' ? 7 : -1;
	case '&': return !c1 ? 6 : c1 == '&' ? 3 : -1;
	case '^': return c1 ? -1 : 5;
	case '|': return !c1 ? 4 : c1 == '|' ? 2 : -1;
	case '?': return c1 ? -1 : 1;
	default: return -1;
	}
}
/* peek an operator token (longest match of << >> <= >= == != && || op= and
 * the single-character operators); returns length or 0 */
static int ld_script_operator_peek(char out[4])
{
	ld_script_whitespace_skip();
	size_t left = (size_t)(ctx->script.end - ctx->script.cursor);
	if (!left)
		return 0;
	char c = ctx->script.cursor[0], c1 = left > 1 ? ctx->script.cursor[1] : 0, c2 = left > 2 ? ctx->script.cursor[2] : 0;
	int n;
	switch (c) {
	case '<': case '>': n = c1 == c ? (c2 == '=' ? 3 : 2) : c1 == '=' ? 2 : 1; break;
	case '=': n = c1 == '=' ? 2 : 1; break;
	case '!': n = c1 == '=' ? 2 : 0; break;
	case '&': case '|': n = c1 == c || c1 == '=' ? 2 : 1; break;
	case '+': case '-': case '*': case '/': case '^': n = c1 == '=' ? 2 : 1; break;
	case '%': case '?': case ':': n = 1; break;
	default: n = 0;
	}
	memcpy(out, ctx->script.cursor, (size_t)n);
	out[n] = 0;
	return n;
}

static ld_expression *ld_script_primary_expression_parse(void);
static ld_expression *ld_script_expression_climb(ld_expression *lhs, int minp)
{
	char op1[4], op2[4];
	for (;;) {
		int n = ld_script_operator_peek(op1);
		if (!n || ld_script_operator_precedence(op1) < minp || (strchr(op1, '=') && strcmp(op1, "==") && strcmp(op1, "!=") &&
		    strcmp(op1, "<=") && strcmp(op1, ">=")))
			break;
		if (!strcmp(op1, ":"))
			break;
		ctx->script.cursor += n;
		if (!strcmp(op1, "?")) {
			ld_expression *t = ld_script_expression_create(LD_EXPRESSION_TERNARY);
			t->a = lhs;
			t->b = ld_script_expression_parse();
			ld_script_character_expect(':');
			t->c = ld_script_expression_parse();
			return t;
		}
		ld_expression *rhs = ld_script_primary_expression_parse();
		for (;;) {
			int m = ld_script_operator_peek(op2);
			if (!m || ld_script_operator_precedence(op2) <= ld_script_operator_precedence(op1) || !strcmp(op2, ":"))
				break;
			if (strchr(op2, '=') && strcmp(op2, "==") && strcmp(op2, "!=") &&
			    strcmp(op2, "<=") && strcmp(op2, ">="))
				break;
			rhs = ld_script_expression_climb(rhs, ld_script_operator_precedence(op2));
		}
		ld_expression *b = ld_script_expression_create(LD_EXPRESSION_BINARY);
		strcpy(b->bop, op1);
		b->a = lhs;
		b->b = rhs;
		lhs = b;
	}
	return lhs;
}
static ld_expression *ld_script_expression_parse(void) { return ld_script_expression_climb(ld_script_primary_expression_parse(), 0); }

static ld_expression *ld_script_parenthesized_expression_parse(void)
{
	ld_script_character_expect('(');
	ld_expression *e = ld_script_expression_parse();
	ld_script_character_expect(')');
	return e;
}
static uint32_t ld_script_parenthesized_output_section_parse(void)
{
	ld_script_character_expect('(');
	char *n = ld_script_pattern_name_parse();
	ld_script_character_expect(')');
	return ld_output_section_find(n);
}

static ld_expression *ld_script_primary_expression_parse(void)
{
	ld_script_whitespace_skip();
	if (ld_script_character_peek('('))
		return ld_script_parenthesized_expression_parse();
	if (ld_script_character_consume('~')) { ld_expression *e = ld_script_expression_create(LD_EXPRESSION_BITWISE_NOT); e->a = ld_script_primary_expression_parse(); return e; }
	if (ld_script_character_consume('!')) { ld_expression *e = ld_script_expression_create(LD_EXPRESSION_LOGICAL_NOT); e->a = ld_script_primary_expression_parse(); return e; }
	if (ld_script_character_consume('-')) { ld_expression *e = ld_script_expression_create(LD_EXPRESSION_NEGATE); e->a = ld_script_primary_expression_parse(); return e; }
	if (ld_script_character_consume('+')) return ld_script_primary_expression_parse();
	ld_script_whitespace_skip();
	const char *b = ctx->script.cursor;
	if (ctx->script.cursor < ctx->script.end && *ctx->script.cursor == '"') {
		char *n = ld_script_name_parse();
		uint32_t g = ld_global_name_intern(n);
		ld_script_symbol_reference_note(g);
		ld_expression *e = ld_script_expression_create(LD_EXPRESSION_SYMBOL);
		e->sym = g;
		return e;
	}
	while (ctx->script.cursor < ctx->script.end && (ld_script_character_classes[(unsigned char)*ctx->script.cursor] & LD_CHARACTER_IDENTIFIER))
		ctx->script.cursor++;
	if (ctx->script.cursor == b) {
		ld_script_error_report("expected expression");
		ctx->script.cursor = ctx->script.end;
		return ld_script_expression_create(LD_EXPRESSION_NUMBER);
	}
	char *tok = ld_script_text_duplicate(b, ctx->script.cursor);
	if (!strcmp(tok, ".")) return ld_script_expression_create(LD_EXPRESSION_DOT);
	if (isdigit((unsigned char)tok[0])) {
		ld_expression *e = ld_script_expression_create(LD_EXPRESSION_NUMBER);
		char *end;
		uint64_t v;
		size_t n = strlen(tok);
		if (n > 1 && (tok[n - 1] == 'h' || tok[n - 1] == 'H') && !(tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X'))) {
			tok[n - 1] = 0;
			v = strtoull(tok, &end, 16);
		} else {
			uint64_t mul = 1;
			if (tok[n - 1] == 'K' || tok[n - 1] == 'k') { mul = 1024; tok[n - 1] = 0; }
			else if (tok[n - 1] == 'M' || tok[n - 1] == 'm') { mul = 1024 * 1024; tok[n - 1] = 0; }
			v = strtoull(tok, &end, 0) * mul;
		}
		if (*end)
			ld_script_error_report("malformed number");
		e->num = v;
		return e;
	}
	ld_expression *e;
	if (!strcmp(tok, "ABSOLUTE")) { e = ld_script_expression_create(LD_EXPRESSION_ABSOLUTE); e->a = ld_script_parenthesized_expression_parse(); return e; }
	if (!strcmp(tok, "ADDR")) {
		e = ld_script_expression_create(LD_EXPRESSION_ADDRESS);
		e->sym = ld_script_parenthesized_output_section_parse();
		ctx->script.sections.v[e->sym].used_in_expr = true;
		return e;
	}
	if (!strcmp(tok, "LOADADDR")) {
		e = ld_script_expression_create(LD_EXPRESSION_LOAD_ADDRESS);
		e->sym = ld_script_parenthesized_output_section_parse();
		ctx->script.sections.v[e->sym].used_in_expr = true;
		return e;
	}
	if (!strcmp(tok, "SIZEOF")) { e = ld_script_expression_create(LD_EXPRESSION_SIZEOF); e->sym = ld_script_parenthesized_output_section_parse(); return e; }
	if (!strcmp(tok, "ALIGNOF")) { e = ld_script_expression_create(LD_EXPRESSION_ALIGNOF); e->sym = ld_script_parenthesized_output_section_parse(); return e; }
	if (!strcmp(tok, "SIZEOF_HEADERS")) return ld_script_expression_create(LD_EXPRESSION_SIZEOF_HEADERS);
	if (!strcmp(tok, "ALIGN")) {
		ld_script_character_expect('(');
		ld_expression *a = ld_script_expression_parse();
		if (ld_script_character_consume(')')) { e = ld_script_expression_create(LD_EXPRESSION_ALIGN_ONE_ARGUMENT); e->a = a; return e; }
		ld_script_character_expect(',');
		e = ld_script_expression_create(LD_EXPRESSION_ALIGN_TWO_ARGUMENTS);
		e->a = a;
		e->b = ld_script_expression_parse();
		ld_script_character_expect(')');
		return e;
	}
	if (!strcmp(tok, "ASSERT")) {
		ld_script_character_expect('(');
		e = ld_script_expression_create(LD_EXPRESSION_ASSERT);
		e->a = ld_script_expression_parse();
		ld_script_character_expect(',');
		e->msg = ld_script_name_parse();
		ld_script_character_expect(')');
		return e;
	}
	if (!strcmp(tok, "DEFINED")) {
		ld_script_character_expect('(');
		e = ld_script_expression_create(LD_EXPRESSION_DEFINED);
		e->sym = ld_global_name_intern(ld_script_pattern_name_parse());
		ld_script_character_expect(')');
		return e;
	}
	if (!strcmp(tok, "MAX") || !strcmp(tok, "MIN")) {
		e = ld_script_expression_create(tok[1] == 'A' ? LD_EXPRESSION_MAX : LD_EXPRESSION_MIN);
		ld_script_character_expect('(');
		e->a = ld_script_expression_parse();
		ld_script_character_expect(',');
		e->b = ld_script_expression_parse();
		ld_script_character_expect(')');
		return e;
	}
	if (!strcmp(tok, "LOG2CEIL")) { e = ld_script_expression_create(LD_EXPRESSION_LOG2CEIL); e->a = ld_script_parenthesized_expression_parse(); return e; }
	if (!strcmp(tok, "CONSTANT")) {
		ld_script_character_expect('(');
		char *n = ld_script_pattern_name_parse();
		ld_script_character_expect(')');
		e = ld_script_expression_create(LD_EXPRESSION_NUMBER);
		e->num = !strcmp(n, "MAXPAGESIZE") ? ctx->options.max_page : 4096;
		return e;
	}
	uint32_t g = ld_global_name_intern(tok);
	ld_script_symbol_reference_note(g);
	e = ld_script_expression_create(LD_EXPRESSION_SYMBOL);
	e->sym = g;
	return e;
}
