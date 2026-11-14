/*
 * statement.c: C++ statements (cxx-internal.h).
 *
 * A statement that may be a declaration is tried as one first (the
 * language's rule: what can be a declaration is one), then as an
 * expression.
 */
#include "cxx-internal.h"

static bool cxx_try(cxx *c, void (*fn)(cxx *))
{
	cxx_mark m;
	jmp_buf jb;
	volatile bool ok = false;

	cxx_mark_set(c, &m);
	c->tentative++;
	c->fail = &jb;
	if (!setjmp(jb)) {
		fn(c);
		ok = true;
	}
	c->tentative--;
	cxx_mark_rewind(c, &m);
	return ok;
}

static bool cxx_kw_is_asm_qualifier(cxx *c)
{
	int kw = cxx_kw(c);

	return kw == CXX_KW_volatile || kw == CXX_KW___volatile__ || kw == CXX_KW___volatile || kw == CXX_KW_inline || kw == CXX_KW_goto;
}

static void cxx_block_declaration(cxx *c)
{
	cxx_declaration_parse(c, CXX_CONTEXT_BLOCK);
}

/* a declaration of a block scope (tried before an expression) */
static bool cxx_declaration_statement_try(cxx *c)
{
	cxx_token *t = cxx_peek(c, 0);

	if (cxx_starts_declaration(c) && t->kw != CXX_KW_typename)
		return true;
	if (t->kind != LEX_TOKEN_IDENTIFIER && t->kind != LEX_TOKEN_COLON_COLON)
		return false;
	if (t->kw && t->kw != CXX_KW_typename && t->kw != CXX_KW_decltype)
		return false;
	/* x = 1; f(a); a.b(): not declarations, and quick to see */
	if (t->kind == LEX_TOKEN_IDENTIFIER && !t->kw) {
		int n = cxx_peek(c, 1)->kind;
		cxx_entity *e = cxx_lookup(c, t->id);

		if (n == LEX_TOKEN_PERIOD || n == LEX_TOKEN_ARROW || n == LEX_TOKEN_EQUAL || n == LEX_TOKEN_PLUS_PLUS ||
		    n == LEX_TOKEN_MINUS_MINUS || n == LEX_TOKEN_LEFT_SQUARE || n == LEX_TOKEN_PLUS_EQUAL || n == LEX_TOKEN_SEMICOLON)
			return false;
		if (e) {	/* functions, variables, function templates: an expression */
			bool values = true;

			for (cxx_entity *x = e; x && values; x = x->next)
				values = x->kind == CXX_ENTITY_VALUE || x->kind == CXX_ENTITY_FUNCTION_TEMPLATE;
			if (values)
				return false;
		}
		if (!e && n != LEX_TOKEN_COLON_COLON && n != LEX_TOKEN_LESS)
			return false;
	}
	return cxx_try(c, cxx_block_declaration);
}

/* for (init; cond; inc) or for (decl : range); after the ( */
static void cxx_for_parse(cxx *c)
{
	cxx_scope *fs = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL);
	cxx_scope *outer = c->scope;

	cxx_scope_push(c, fs);
	cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
	/* range-for: a declaration followed by : (find it at depth 0 before the first ;) */
	{
		int depth = 0;
		uint32_t i = c->pos;
		bool range = false;

		for (; i < c->ntok; i++) {
			int k = c->tok[i].kind;

			if (k == LEX_TOKEN_LEFT_PAREN || k == LEX_TOKEN_LEFT_SQUARE || k == LEX_TOKEN_LEFT_BRACE)
				depth++;
			else if (k == LEX_TOKEN_RIGHT_PAREN || k == LEX_TOKEN_RIGHT_SQUARE || k == LEX_TOKEN_RIGHT_BRACE) {
				if (!depth--)
					break;
			} else if (depth == 0 && k == LEX_TOKEN_SEMICOLON)
				break;
			else if (depth == 0 && k == LEX_TOKEN_COLON && c->tok[i + 1].kind != LEX_TOKEN_COLON) {
				range = true;
				break;
			}
		}
		if (range) {
			/* decl-specifiers declarator (or a structured binding [a, b]) : range */
			uint32_t colon = i, last_id = 0;

			for (uint32_t k = c->pos; k < colon; k++)
				if (c->tok[k].kind == LEX_TOKEN_IDENTIFIER && !c->tok[k].kw)
					last_id = c->tok[k].id;
			if (cxx_is(c, LEX_TOKEN_SEMICOLON) == false && c->pos < colon) {
				/* (a C++20 init-statement before the declaration is not handled) */
				c->pos = colon;
			}
			if (last_id)
				cxx_declare(c, fs, last_id, CXX_ENTITY_VALUE);
			for (uint32_t k = colon - 1; k > 0 && c->tok[k].kind != LEX_TOKEN_LEFT_PAREN; k--)
				if (c->tok[k].kind == LEX_TOKEN_IDENTIFIER && !c->tok[k].kw && c->tok[k - 1].kind == LEX_TOKEN_COMMA)
					cxx_declare(c, fs, c->tok[k].id, CXX_ENTITY_VALUE);	/* structured bindings */
			cxx_expect(c, LEX_TOKEN_COLON);
			cxx_initializer_parse(c);
			cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
			cxx_statement_parse(c);
			c->scope = outer;
			return;
		}
	}
	if (!cxx_accept(c, LEX_TOKEN_SEMICOLON)) {
		if (cxx_declaration_statement_try(c))
			cxx_declaration_parse(c, CXX_CONTEXT_BLOCK);
		else {
			cxx_expression_parse(c);
			cxx_expect(c, LEX_TOKEN_SEMICOLON);
		}
	}
	if (!cxx_is(c, LEX_TOKEN_SEMICOLON))
		cxx_expression_parse(c);
	cxx_expect(c, LEX_TOKEN_SEMICOLON);
	if (!cxx_is(c, LEX_TOKEN_RIGHT_PAREN))
		cxx_expression_parse(c);
	cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
	cxx_statement_parse(c);
	c->scope = outer;
}

/* if/switch/while ( [init;] condition ) */
static void cxx_condition_parse(cxx *c)
{
	cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
	for (;;) {
		if (cxx_declaration_statement_try(c)) {
			/* a declaration: init-statement ending in ; or a condition declaration */
			uint32_t save = c->pos;
			cxx_mark m;
			jmp_buf jb;
			volatile bool ok = false;

			cxx_mark_set(c, &m);
			c->tentative++;
			c->fail = &jb;
			if (!setjmp(jb)) {
				cxx_declaration_parse(c, CXX_CONTEXT_BLOCK);
				ok = true;
			}
			c->tentative--;
			cxx_mark_rewind(c, &m);
			(void)save;
			if (ok) {
				cxx_declaration_parse(c, CXX_CONTEXT_BLOCK);	/* (ends with ;) */
				continue;
			}
			/* a condition declaration: T x = e  or  T x{e} */
			{
				int depth = 0;

				while (!(depth == 0 && cxx_is(c, LEX_TOKEN_RIGHT_PAREN))) {
					if (cxx_is(c, LEX_TOKEN_EOF))
						cxx_fail(c, "unterminated condition");
					if (cxx_is(c, LEX_TOKEN_LEFT_PAREN))
						depth++;
					else if (cxx_is(c, LEX_TOKEN_RIGHT_PAREN))
						depth--;
					if (depth == 0 && cxx_is(c, LEX_TOKEN_IDENTIFIER) && !cxx_kw(c) &&
					    (cxx_peek(c, 1)->kind == LEX_TOKEN_EQUAL || cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_BRACE))
						cxx_declare(c, c->scope, cxx_peek(c, 0)->id, CXX_ENTITY_VALUE);
					if (depth == 0 && cxx_is(c, LEX_TOKEN_EQUAL)) {
						cxx_advance(c);
						cxx_initializer_parse(c);
						continue;
					}
					if (depth == 0 && cxx_is(c, LEX_TOKEN_LEFT_BRACE)) {
						cxx_braced_init_list_parse(c);
						continue;
					}
					cxx_advance(c);
				}
				break;
			}
		}
		cxx_expression_parse(c);
		if (cxx_accept(c, LEX_TOKEN_SEMICOLON))
			continue;	/* an expression init-statement */
		break;
	}
	cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
}

VPRIV void cxx_compound_statement_parse(cxx *c)
{
	cxx_scope *bs = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL);
	cxx_scope *outer = c->scope;

	cxx_expect(c, LEX_TOKEN_LEFT_BRACE);
	cxx_scope_push(c, bs);
	while (!cxx_is(c, LEX_TOKEN_RIGHT_BRACE)) {
		if (cxx_is(c, LEX_TOKEN_EOF))
			cxx_fail(c, "expected '}'");
		cxx_statement_parse(c);
	}
	cxx_advance(c);
	c->scope = outer;
}

VPRIV void cxx_statement_parse(cxx *c)
{
	cxx_attributes_skip(c);
	switch (cxx_kind(c)) {
	case LEX_TOKEN_LEFT_BRACE:
		cxx_compound_statement_parse(c);
		return;
	case LEX_TOKEN_SEMICOLON:
		cxx_advance(c);
		return;
	case LEX_TOKEN_IDENTIFIER:
		if (!cxx_kw(c) && cxx_peek(c, 1)->kind == LEX_TOKEN_COLON) {	/* label */
			cxx_advance(c);
			cxx_advance(c);
			if (!cxx_is(c, LEX_TOKEN_RIGHT_BRACE))
				cxx_statement_parse(c);
			return;
		}
		break;
	}
	switch (cxx_kw(c)) {
	case CXX_KW_if:
		cxx_advance(c);
		cxx_accept_kw(c, CXX_KW_constexpr);
		if (cxx_is(c, LEX_TOKEN_EXCLAMATION) || cxx_is_kw(c, CXX_KW_consteval)) {	/* if !consteval / if consteval */
			cxx_accept(c, LEX_TOKEN_EXCLAMATION);
			cxx_expect_kw(c, CXX_KW_consteval);
			cxx_compound_statement_parse(c);
		} else {
			cxx_scope *s = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL), *outer = c->scope;

			cxx_scope_push(c, s);
			cxx_condition_parse(c);
			cxx_statement_parse(c);
			c->scope = outer;
		}
		if (cxx_accept_kw(c, CXX_KW_else))
			cxx_statement_parse(c);
		return;
	case CXX_KW_switch: case CXX_KW_while: {
		cxx_scope *s = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL), *outer = c->scope;

		cxx_advance(c);
		cxx_scope_push(c, s);
		cxx_condition_parse(c);
		cxx_statement_parse(c);
		c->scope = outer;
		return;
	}
	case CXX_KW_do:
		cxx_advance(c);
		cxx_statement_parse(c);
		cxx_expect_kw(c, CXX_KW_while);
		cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
		cxx_expression_parse(c);
		cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
		cxx_expect(c, LEX_TOKEN_SEMICOLON);
		return;
	case CXX_KW_for:
		cxx_advance(c);
		cxx_for_parse(c);
		return;
	case CXX_KW_return: case CXX_KW_co_return:
		cxx_advance(c);
		if (cxx_is(c, LEX_TOKEN_LEFT_BRACE))
			cxx_braced_init_list_parse(c);
		else if (!cxx_is(c, LEX_TOKEN_SEMICOLON))
			cxx_expression_parse(c);
		cxx_expect(c, LEX_TOKEN_SEMICOLON);
		return;
	case CXX_KW_break: case CXX_KW_continue:
		cxx_advance(c);
		cxx_expect(c, LEX_TOKEN_SEMICOLON);
		return;
	case CXX_KW_goto:
		cxx_advance(c);
		if (cxx_accept(c, LEX_TOKEN_STAR))
			cxx_expression_parse(c);
		else
			cxx_expect(c, LEX_TOKEN_IDENTIFIER);
		cxx_expect(c, LEX_TOKEN_SEMICOLON);
		return;
	case CXX_KW_case:
		cxx_advance(c);
		cxx_constant_expression_parse(c);
		if (cxx_accept(c, LEX_TOKEN_ELLIPSIS))	/* GNU case ranges */
			cxx_constant_expression_parse(c);
		cxx_expect(c, LEX_TOKEN_COLON);
		if (!cxx_is(c, LEX_TOKEN_RIGHT_BRACE))
			cxx_statement_parse(c);
		return;
	case CXX_KW_default:
		cxx_advance(c);
		cxx_expect(c, LEX_TOKEN_COLON);
		if (!cxx_is(c, LEX_TOKEN_RIGHT_BRACE))
			cxx_statement_parse(c);
		return;
	case CXX_KW_try:
		cxx_advance(c);
		cxx_compound_statement_parse(c);
		while (cxx_is_kw(c, CXX_KW_catch))
			cxx_statement_parse(c);
		return;
	case CXX_KW_catch: {
		cxx_scope *s = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL), *outer = c->scope;
		uint32_t last_id = 0;
		int depth = 0;

		cxx_advance(c);
		cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
		while (!(depth == 0 && cxx_is(c, LEX_TOKEN_RIGHT_PAREN))) {
			if (cxx_is(c, LEX_TOKEN_EOF))
				cxx_fail(c, "unterminated catch");
			if (cxx_is(c, LEX_TOKEN_LEFT_PAREN))
				depth++;
			else if (cxx_is(c, LEX_TOKEN_RIGHT_PAREN))
				depth--;
			if (cxx_is(c, LEX_TOKEN_IDENTIFIER) && !cxx_kw(c))
				last_id = cxx_peek(c, 0)->id;
			cxx_advance(c);
		}
		cxx_advance(c);
		cxx_scope_push(c, s);
		if (last_id)
			cxx_declare(c, s, last_id, CXX_ENTITY_VALUE);
		cxx_compound_statement_parse(c);
		c->scope = outer;
		return;
	}
	case CXX_KW_asm: case CXX_KW___asm__: case CXX_KW___asm:
		cxx_advance(c);
		while (cxx_kw_is_asm_qualifier(c))
			cxx_advance(c);
		cxx_skip_balanced(c);
		cxx_expect(c, LEX_TOKEN_SEMICOLON);
		return;
	case CXX_KW___label__:
		while (!cxx_accept(c, LEX_TOKEN_SEMICOLON))
			cxx_advance(c);
		return;
	}
	if (cxx_declaration_statement_try(c)) {
		cxx_declaration_parse(c, CXX_CONTEXT_BLOCK);
		return;
	}
	uint32_t start = c->pos;

	cxx_expression_parse(c);
	if (!c->tentative && !c->template_depth && !c->instantiating) {
		static int dump = -1;

		c->stats.expressions++;	/* (an expression statement outside templates: its type known?) */
		c->stats.expressions_typed += c->etype != CXX_TY_UNKNOWN;
		if (dump < 0)
			dump = getenv("VCC_CXX_UNTYPED") != NULL;
		if (dump && c->etype == CXX_TY_UNKNOWN) {	/* the untyped statements, for the next step */
			for (uint32_t i = start; i < c->pos && i < start + 12; i++)
				fprintf(stderr, "%.*s ", (int)c->tok[i].len, c->tok[i].s);
			fputc('\n', stderr);
		}
	}
	cxx_expect(c, LEX_TOKEN_SEMICOLON);
}
