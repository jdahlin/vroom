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
static void cxx_substatement_parse(cxx *c);

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
		/* A::B::f(...): a qualified name of functions or variables is an expression (Base::f(x); calls) */
		if (e && n == LEX_TOKEN_COLON_COLON) {
			cxx_entity *x = e;
			uint32_t i = 0;

			while (x && cxx_peek(c, i + 1)->kind == LEX_TOKEN_COLON_COLON && cxx_peek(c, i + 2)->kind == LEX_TOKEN_IDENTIFIER &&
			       !cxx_peek(c, i + 2)->kw && x->members && !x->members->dependent && !x->next) {
				x = cxx_lookup_in(c, x->members, cxx_peek(c, i + 2)->id, 0);
				i += 2;
			}
			if (x && i && cxx_peek(c, i + 1)->kind != LEX_TOKEN_COLON_COLON && cxx_peek(c, i + 1)->kind != LEX_TOKEN_LESS) {
				bool values = true;

				for (cxx_entity *y = x; y && values; y = y->next)
					values = y->kind == CXX_ENTITY_VALUE || y->kind == CXX_ENTITY_FUNCTION_TEMPLATE;
				if (values)
					return false;
			}
		}
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
			cxx_entity *var = NULL;
			qty var_type = CXX_TY_UNKNOWN;
			bool var_auto = false;

			/* the declaration, when it is a plain one outside templates: its name and type */
			if (!c->template_depth && !c->tentative && !cxx_is(c, LEX_TOKEN_SEMICOLON) &&
			    cxx_lookahead_fn(c, cxx_for_range_declaration_parse)) {
				cxx_for_range_declaration_parse(c);
				if (c->range_name > 1 && c->pos == colon) {
					var = cxx_declare(c, fs, c->range_name, CXX_ENTITY_VALUE);
					var_type = c->range_type, var_auto = c->range_auto;
				}
				c->pos = colon;
			}
			for (uint32_t k = c->pos; k < colon; k++)
				if (c->tok[k].kind == LEX_TOKEN_IDENTIFIER && !c->tok[k].kw)
					last_id = c->tok[k].id;
			if (cxx_is(c, LEX_TOKEN_SEMICOLON) == false && c->pos < colon) {
				/* (a C++20 init-statement before the declaration is not handled) */
				c->pos = colon;
			}
			if (last_id && !var)
				cxx_declare(c, fs, last_id, CXX_ENTITY_VALUE);
			for (uint32_t k = colon - 1; k > 0 && c->tok[k].kind != LEX_TOKEN_LEFT_PAREN; k--)
				if (c->tok[k].kind == LEX_TOKEN_IDENTIFIER && !c->tok[k].kw && c->tok[k - 1].kind == LEX_TOKEN_COMMA)
					cxx_declare(c, fs, c->tok[k].id, CXX_ENTITY_VALUE);	/* structured bindings */
			cxx_expect(c, LEX_TOKEN_COLON);
			cxx_initializer_parse(c);
			if (var) {
				/* auto: deduced from *begin(range), an lvalue; else as declared */
				qty elem;

				if (!var_auto)
					var->ty = var_type;
				else if (cxx_range_element_type(c, c->etype, &elem))
					var->ty = cxx_auto_deduce(c, var_type, elem, true);
			}
			cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
			cxx_statement_parse(c);
			c->scope = outer;
			return;
		}
	}
	/* code generation: { init; for (; cond; step) body } with the init's declarations in the block */
	bool gen = cxx_gen_on(c);
	cc_id_list pre;
	void *outer_block = c->gen_block;
	uint32_t floc = cxx_peek(c, 0)->loc, gcond = 0, gstep = 0, gbody;
	bool has_step = false;

	if (gen) {
		cc_id_list_init(&pre);
		c->gen_block = &pre;
	}
	if (!cxx_accept(c, LEX_TOKEN_SEMICOLON)) {
		if (cxx_declaration_statement_try(c))
			cxx_declaration_parse(c, CXX_CONTEXT_BLOCK);
		else {
			cxx_expression_parse(c);
			if (gen && cxx_gen_on(c)) {
				if (c->enode)
					cc_id_list_append(c->ast, &pre, cxx_gen_new_statement(c, N_EXPR, floc, c->enode, 0, 0));
				else
					cxx_gen_fail(c);
			}
			cxx_expect(c, LEX_TOKEN_SEMICOLON);
		}
	}
	if (gen)
		c->gen_block = outer_block;
	c->gen_notemp++;
	if (!cxx_is(c, LEX_TOKEN_SEMICOLON)) {
		cxx_expression_parse(c);
		if (gen)
			gcond = cxx_gen_condition(c, c->enode);
	}
	cxx_expect(c, LEX_TOKEN_SEMICOLON);
	if (!cxx_is(c, LEX_TOKEN_RIGHT_PAREN)) {
		cxx_expression_parse(c);
		has_step = true, gstep = c->enode;
	}
	c->gen_notemp--;
	cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
	cxx_substatement_parse(c);
	c->scope = outer;
	if (gen && cxx_gen_on(c)) {
		cc_id_list sb;

		gbody = c->snode ? c->snode : cxx_gen_new_statement(c, N_BLOCK, floc, 0, 0, 0);
		if (has_step && !gstep) {
			cxx_gen_fail(c);
			return;
		}
		cc_id_list_init(&sb);
		cc_id_list_append(c->ast, &sb, gstep);
		cc_id_list_append(c->ast, &sb, gbody);
		cc_id_list_append(c->ast, &pre, cxx_gen_new_statement(c, N_FOR, floc, 0, gcond, cc_id_list_freeze(c->ast, &sb)));
		cxx_gen_statement(c, cxx_gen_new_statement(c, N_BLOCK, floc, 0, cc_id_list_freeze(c->ast, &pre), pre.n));
	}
}

/* if/switch/while ( [init;] condition ) */
/*
 * An if's or a switch's condition that is a declaration (T x = e, T x{e}): parsed as a declaration like any
 * other, up to the ) that ends the condition; its value is the variable's.  Code generation declares the
 * variable in the block around the statement.  False: not a declaration.
 */
static bool cxx_condition_declaration_parse(cxx *c)
{
	uint32_t start = c->pos, close = c->pos, name = 0, at = 0;
	int d = 0;

	if (!c->cond_decl || c->tentative || c->template_depth)
		return false;
	for (; close < c->ntok; close++) {
		int k = c->tok[close].kind;

		if (k == LEX_TOKEN_LEFT_PAREN || k == LEX_TOKEN_LEFT_SQUARE || k == LEX_TOKEN_LEFT_BRACE)
			d++;
		else if (k == LEX_TOKEN_RIGHT_PAREN || k == LEX_TOKEN_RIGHT_SQUARE || k == LEX_TOKEN_RIGHT_BRACE) {
			if (!d--)
				break;
		} else if (!d && k == LEX_TOKEN_SEMICOLON)
			return false;	/* (an init-statement first) */
		else if (!d && !name && k == LEX_TOKEN_IDENTIFIER && !c->tok[close].kw &&
			 (c->tok[close + 1].kind == LEX_TOKEN_EQUAL || c->tok[close + 1].kind == LEX_TOKEN_LEFT_BRACE))
			name = c->tok[close].id, at = close;
	}
	if (!name || at == start || close >= c->ntok || c->tok[close].kind != LEX_TOKEN_RIGHT_PAREN)
		return false;
	{
		/* (what comes before the name ends a type: a name, >, *, &) */
		int pk = c->tok[at - 1].kind;

		if (pk != LEX_TOKEN_IDENTIFIER && pk != LEX_TOKEN_GREATER && pk != LEX_TOKEN_STAR && pk != LEX_TOKEN_AMPERSAND &&
		    pk != LEX_TOKEN_AMPERSAND_AMPERSAND)
			return false;
	}
	{
		jmp_buf djb, *outer_fail = c->fail;
		volatile bool parsed = false;
		cxx_entity *e;

		c->tok[close].kind = LEX_TOKEN_SEMICOLON;
		c->fail = &djb;
		if (!setjmp(djb)) {
			cxx_declaration_parse(c, CXX_CONTEXT_BLOCK);
			parsed = true;
		}
		c->fail = outer_fail;
		c->tok[close].kind = LEX_TOKEN_RIGHT_PAREN;
		if (!parsed)
			longjmp(*outer_fail, 1);
		c->pos = close;
		e = cxx_lookup(c, name);
		if (e && e->kind == CXX_ENTITY_VALUE)
			cxx_name_type(c, e);
		else
			cxx_et(c, CXX_TY_UNKNOWN, false);
	}
	return true;
}

static void cxx_condition_parse(cxx *c)
{
	cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
	for (;;) {
		if (cxx_condition_declaration_parse(c))
			break;
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
				cxx_et(c, CXX_TY_UNKNOWN, false);	/* (a condition declaration: no node yet) */
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
	/* code generation: the block's statements, in a list of its own */
	cc_id_list list;
	void *outer_block = c->gen_block;
	bool gen = cxx_gen_on(c);
	uint32_t loc = cxx_peek(c, 0)->loc, outer_temps = c->gen_block_temps;

	if (gen) {
		cc_id_list_init(&list);
		c->gen_block = &list;
		c->gen_block_temps = 0;
	}
	cxx_expect(c, LEX_TOKEN_LEFT_BRACE);
	cxx_scope_push(c, bs);
	while (!cxx_is(c, LEX_TOKEN_RIGHT_BRACE)) {
		if (cxx_is(c, LEX_TOKEN_EOF))
			cxx_fail(c, "expected '}'");
		cxx_statement_parse(c);
		if (gen && c->snode && cxx_gen_on(c))
			cc_id_list_append(c->ast, &list, c->snode);
	}
	cxx_advance(c);
	c->scope = outer;
	if (gen) {
		c->gen_block = outer_block;
		c->gen_block_temps = outer_temps;
		cxx_gen_statement(c, cxx_gen_on(c) ? cxx_gen_new_statement(c, N_BLOCK, loc, 0, cc_id_list_freeze(c->ast, &list), list.n) : 0);
	}
}

static void cxx_statement_parse1(cxx *c);

/* (code generation: a statement whose own case did not give its node is one not built yet) */
VPRIV void cxx_statement_parse(cxx *c)
{
	uint32_t depth = ++c->sdepth;

	c->snode = 0;
	c->sdone = 0;	/* (the statement before this one, done at the same depth, does not vouch for this one) */
	cxx_statement_parse1(c);
	if (c->gen && c->sdone != depth) {
		cxx_gen_fail(c);
		c->snode = 0;
	}
	c->sdepth = depth - 1;
}

/*
 * A statement that is a part of another (an if's branches, a loop's body, a label's statement).  Code
 * generation: the temporaries it declares are its own, in a block around it (it may run many times or not
 * at all for one entry of the enclosing block).
 */
static void cxx_substatement_parse(cxx *c)
{
	cc_id_list list;
	void *outer_block = c->gen_block;
	uint32_t outer_temps = c->gen_block_temps, loc = cxx_peek(c, 0)->loc;
	bool gen = cxx_gen_on(c) && c->gen_block && !cxx_is(c, LEX_TOKEN_LEFT_BRACE);

	if (gen) {
		cc_id_list_init(&list);
		c->gen_block = &list;
		c->gen_block_temps = 0;
	}
	cxx_statement_parse(c);
	if (gen) {
		c->gen_block = outer_block;
		c->gen_block_temps = outer_temps;
		if (list.n && c->snode && cxx_gen_on(c)) {
			cc_id_list_append(c->ast, &list, c->snode);
			c->snode = cxx_gen_new_statement(c, N_BLOCK, loc, 0, cc_id_list_freeze(c->ast, &list), list.n);
		} else if (list.n)
			cxx_gen_fail(c);	/* (declarations with no statement to go with) */
	}
}

/* a nested statement's node, or an empty block for an empty statement */
static uint32_t cxx_gen_nested(cxx *c, uint32_t loc)
{
	if (!cxx_gen_on(c))
		return 0;
	return c->snode ? c->snode : cxx_gen_new_statement(c, N_BLOCK, loc, 0, 0, 0);
}

static void cxx_statement_parse1(cxx *c)
{
	uint32_t sloc = cxx_peek(c, 0)->loc;

	cxx_attributes_skip(c);
	switch (cxx_kind(c)) {
	case LEX_TOKEN_LEFT_BRACE:
		cxx_compound_statement_parse(c);
		return;
	case LEX_TOKEN_SEMICOLON:
		cxx_advance(c);
		cxx_gen_statement(c, 0);
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
		bool is_constexpr = cxx_accept_kw(c, CXX_KW_constexpr);
		int taken = -1;	/* if constexpr with a known condition: 1 the first statement, 0 the else */
		uint32_t gen_cond = 0, gen_then = 0, gen_else = 0;
		/* code generation: what the condition declares (a variable, temporaries) lives in a block around the if */
		cc_id_list cpre;
		void *cond_outer = c->gen_block;
		uint32_t cond_temps = c->gen_block_temps;
		bool cond_gen = cxx_gen_on(c) && c->gen_block;

		cpre.n = 0;

		if (cxx_is(c, LEX_TOKEN_EXCLAMATION) || cxx_is_kw(c, CXX_KW_consteval)) {	/* if !consteval / if consteval */
			cxx_accept(c, LEX_TOKEN_EXCLAMATION);
			cxx_expect_kw(c, CXX_KW_consteval);
			cxx_compound_statement_parse(c);
		} else {
			cxx_scope *s = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL), *outer = c->scope;

			cxx_scope_push(c, s);
			if (cond_gen) {
				cc_id_list_init(&cpre);
				c->gen_block = &cpre;
				c->gen_block_temps = 0;
			}
			c->cond_decl = 1;
			cxx_condition_parse(c);
			c->cond_decl = 0;
			gen_cond = is_constexpr ? 0 : cxx_gen_condition(c, c->enode);
			if (cond_gen) {
				c->gen_block = cond_outer;
				c->gen_block_temps = cond_temps;
			}
			if (is_constexpr && c->econst && !c->tentative && !c->template_depth)
				taken = c->evalue != 0;
			/* the discarded statement is parsed, not instantiated: what it calls is not used */
			c->unevaluated += taken == 0;
			cxx_substatement_parse(c);
			c->unevaluated -= taken == 0;
			gen_then = cxx_gen_nested(c, sloc);
			c->scope = outer;
		}
		if (cxx_accept_kw(c, CXX_KW_else)) {
			c->unevaluated += taken == 1;
			cxx_substatement_parse(c);
			c->unevaluated -= taken == 1;
			gen_else = cxx_gen_nested(c, sloc);
		}
		if (gen_cond && gen_then && cxx_gen_on(c)) {
			uint32_t st = cxx_gen_new_statement(c, N_IF, sloc, gen_cond, gen_then, gen_else);

			if (cond_gen && cpre.n) {
				cc_id_list_append(c->ast, &cpre, st);
				st = cxx_gen_new_statement(c, N_BLOCK, sloc, 0, cc_id_list_freeze(c->ast, &cpre), cpre.n);
			}
			cxx_gen_statement(c, st);
		} else if (cond_gen && cpre.n)
			cxx_gen_fail(c);
		return;
	case CXX_KW_switch: case CXX_KW_while: {
		cxx_scope *s = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL), *outer = c->scope;
		bool is_while = cxx_is_kw(c, CXX_KW_while);
		uint32_t gen_cond, gen_body;

		cxx_advance(c);
		cxx_scope_push(c, s);
		c->gen_notemp += is_while;	/* (evaluated once per iteration: no temporaries with destructors) */
		cxx_condition_parse(c);
		c->gen_notemp -= is_while;
		gen_cond = is_while ? cxx_gen_condition(c, c->enode) : cxx_gen_switch_value(c, c->enode);
		cxx_substatement_parse(c);
		gen_body = cxx_gen_nested(c, sloc);
		c->scope = outer;
		if (gen_cond && gen_body && cxx_gen_on(c))
			cxx_gen_statement(c, cxx_gen_new_statement(c, is_while ? N_WHILE : N_SWITCH, sloc, gen_cond, gen_body, 0));
		return;
	}
	case CXX_KW_do: {
		uint32_t gen_body, gen_cond;

		cxx_advance(c);
		cxx_substatement_parse(c);
		gen_body = cxx_gen_nested(c, sloc);
		cxx_expect_kw(c, CXX_KW_while);
		cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
		c->gen_notemp++;
		cxx_expression_parse(c);
		c->gen_notemp--;
		gen_cond = cxx_gen_condition(c, c->enode);
		cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
		cxx_expect(c, LEX_TOKEN_SEMICOLON);
		if (gen_body && gen_cond && cxx_gen_on(c))
			cxx_gen_statement(c, cxx_gen_new_statement(c, N_DO, sloc, gen_cond, gen_body, 0));
		return;
	}
	case CXX_KW_for:
		cxx_advance(c);
		cxx_for_parse(c);
		return;
	case CXX_KW_return: case CXX_KW_co_return: {
		bool is_return = cxx_is_kw(c, CXX_KW_return), has_value = false;
		uint32_t value = 0;

		cxx_advance(c);
		if (cxx_is(c, LEX_TOKEN_LEFT_BRACE))
			cxx_braced_init_list_parse(c), has_value = true;
		else if (!cxx_is(c, LEX_TOKEN_SEMICOLON)) {
			cxx_expression_parse(c);
			has_value = true, value = c->enode;
			if (c->ret_capture)
				c->ret_seen = 1;
			/* an auto return type: the first return's (the others must agree) */
			if (c->ret_capture && c->ret_type == CXX_TY_UNKNOWN && !c->tentative)
				c->ret_type = c->etype, c->ret_lval = c->elval;
		} else if (c->ret_capture && c->ret_type == CXX_TY_UNKNOWN && !c->tentative)
			c->ret_type = cc_type_basic(TY_VOID), c->ret_lval = 0;
		cxx_expect(c, LEX_TOKEN_SEMICOLON);
		if (is_return && cxx_gen_on(c)) {
			uint32_t r = cxx_gen_return(c, value, has_value, sloc);

			c->gen_returns = 1;
			if (r)
				cxx_gen_statement(c, r);
		}
		return;
	}
	case CXX_KW_break: case CXX_KW_continue: {
		int op = cxx_is_kw(c, CXX_KW_break) ? N_BREAK : N_CONTINUE;

		cxx_advance(c);
		cxx_expect(c, LEX_TOKEN_SEMICOLON);
		if (cxx_gen_on(c))
			cxx_gen_statement(c, cxx_gen_new_statement(c, op, sloc, 0, 0, 0));
		return;
	}
	case CXX_KW_goto:
		cxx_advance(c);
		if (cxx_accept(c, LEX_TOKEN_STAR))
			cxx_expression_parse(c);
		else
			cxx_expect(c, LEX_TOKEN_IDENTIFIER);
		cxx_expect(c, LEX_TOKEN_SEMICOLON);
		return;
	case CXX_KW_case: {
		bool known;
		int64_t lo, hi;

		cxx_advance(c);
		cxx_constant_expression_parse(c);
		known = c->econst, lo = hi = c->evalue;
		if (cxx_accept(c, LEX_TOKEN_ELLIPSIS)) {	/* GNU case ranges */
			cxx_constant_expression_parse(c);
			known &= c->econst, hi = c->evalue;
		}
		cxx_expect(c, LEX_TOKEN_COLON);
		if (c->gen_block_temps)
			cxx_gen_fail(c);	/* (a jump past a temporary that is destroyed at the block's end) */
		c->snode = 0;
		if (!cxx_is(c, LEX_TOKEN_RIGHT_BRACE))
			cxx_substatement_parse(c);
		if (known && cxx_gen_on(c))
			cxx_gen_statement(c, cxx_gen_case(c, lo, hi, cxx_gen_nested(c, sloc), sloc));
		return;
	}
	case CXX_KW_default:
		cxx_advance(c);
		cxx_expect(c, LEX_TOKEN_COLON);
		if (c->gen_block_temps)
			cxx_gen_fail(c);	/* (a jump past a temporary that is destroyed at the block's end) */
		c->snode = 0;
		if (!cxx_is(c, LEX_TOKEN_RIGHT_BRACE))
			cxx_substatement_parse(c);
		if (cxx_gen_on(c))
			cxx_gen_statement(c, cxx_gen_new_statement(c, N_DEFAULT, sloc, 0, 0, cxx_gen_nested(c, sloc)));
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
		cxx_gen_statement(c, 0);	/* (its variables' declarations were appended to the block) */
		return;
	}
	uint32_t start = c->pos;

	cxx_expression_parse(c);
	if (cxx_gen_on(c) && c->enode)
		cxx_gen_statement(c, cxx_gen_new_statement(c, N_EXPR, sloc, c->enode, 0, 0));
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
