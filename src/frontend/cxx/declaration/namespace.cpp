/* C++ declarations: namespaces, using, static_assert */
#include "declaration.h"

VPRIV void cxx_namespace_parse(cxx *c)
{
	bool is_inline = cxx_accept_kw(c, CXX_KW_inline);
	cxx_scope *outer = c->scope;
	uint32_t path_len = c->name_path_len;
	uint8_t path_valid = c->name_path_valid;

	cxx_expect_kw(c, CXX_KW_namespace);
	cxx_attributes_skip(c);
	if (cxx_is(c, LEX_TOKEN_IDENTIFIER) && cxx_peek(c, 1)->kind == LEX_TOKEN_EQUAL) {
		/* namespace alias: namespace a = b::c; */
		uint32_t name = cxx_peek(c, 0)->id;
		cxx_scope *q;
		bool dep;
		cxx_entity *e, *target;

		cxx_advance(c);
		cxx_advance(c);
		cxx_nested_name_parse(c, &q, &dep);
		if (!cxx_is(c, LEX_TOKEN_IDENTIFIER))
			cxx_fail(c, "expected a namespace name");
		target = cxx_name_lookup(c, q, dep, cxx_peek(c, 0)->id);
		cxx_advance(c);
		cxx_expect(c, LEX_TOKEN_SEMICOLON);
		e = cxx_declare(c, c->scope, name, CXX_ENTITY_NAMESPACE);
		if (e && target)
			e->members = cxx_entity_prefer(target, CXX_ENTITY_NAMESPACE)->members;
		return;
	}
	/* namespace a::b::inline c { (C++20 nested, with inline) or anonymous */
	do {
		bool inl = cxx_accept_kw(c, CXX_KW_inline) || is_inline;
		uint32_t name = 0;
		cxx_entity *e;
		cxx_scope *s;

		if (cxx_is(c, LEX_TOKEN_IDENTIFIER)) {
			name = cxx_peek(c, 0)->id;
			cxx_advance(c);
		}
		cxx_attributes_skip(c);
		e = name ? cxx_entity_prefer(cxx_lookup_local(c->scope, name), CXX_ENTITY_NAMESPACE) : NULL;
		if (e && e->kind == CXX_ENTITY_NAMESPACE && e->members)
			s = e->members;
		else {
			if (!name) {
				e = NULL;
				s = NULL;
				for (uint32_t i = 0; i < c->scope->nused; i++)
					if (c->scope->used[i]->owner == NULL && c->scope->used[i]->kind == CXX_SCOPE_NAMESPACE &&
					    !c->scope->used[i]->inline_namespace)
						s = c->scope->used[i];	/* the anonymous namespace, reopened */
				if (!s) {
					s = cxx_scope_new(c, CXX_SCOPE_NAMESPACE, c->scope, NULL);
					cxx_scope_use(c, c->scope, s);
				}
			} else {
				e = cxx_declare(c, c->scope, name, CXX_ENTITY_NAMESPACE);
				s = cxx_scope_new(c, CXX_SCOPE_NAMESPACE, c->scope, e);
				if (e)
					e->members = s;
			}
		}
		if (inl) {
			s->inline_namespace = 1;
			cxx_scope_use(c, c->scope, s);
		}
		s->parent = c->scope;
		c->scope = s;
		{
			uint32_t pl;
			uint8_t pv;

			cxx_name_path_push(c, name, &pl, &pv);	/* (anonymous: not nameable) */
			(void)pl, (void)pv;
		}
		is_inline = false;
	} while (cxx_accept(c, LEX_TOKEN_COLON_COLON));
	cxx_expect(c, LEX_TOKEN_LEFT_BRACE);
	while (!cxx_is(c, LEX_TOKEN_RIGHT_BRACE) && !cxx_is(c, LEX_TOKEN_EOF))
		cxx_declaration_parse(c, CXX_CONTEXT_NAMESPACE);
	cxx_expect(c, LEX_TOKEN_RIGHT_BRACE);
	c->scope = outer;
	cxx_name_path_pop(c, path_len, path_valid);
}

VPRIV void cxx_using_parse(cxx *c)
{
	cxx_expect_kw(c, CXX_KW_using);
	if (cxx_accept_kw(c, CXX_KW_namespace)) {	/* using-directive */
		cxx_scope *q;
		bool dep;
		cxx_entity *e;

		cxx_nested_name_parse(c, &q, &dep);
		if (!cxx_is(c, LEX_TOKEN_IDENTIFIER))
			cxx_fail(c, "expected a namespace name");
		e = cxx_name_lookup(c, q, dep, cxx_peek(c, 0)->id);
		cxx_advance(c);
		cxx_attributes_skip(c);
		cxx_expect(c, LEX_TOKEN_SEMICOLON);
		e = e ? cxx_entity_prefer(e, CXX_ENTITY_NAMESPACE) : NULL;
		if (e && e->members)
			cxx_scope_use(c, c->scope, e->members);
		return;
	}
	if (cxx_accept_kw(c, CXX_KW_enum)) {	/* using enum E; */
		cxx_scope *q;
		bool dep;

		cxx_nested_name_parse(c, &q, &dep);
		cxx_expect(c, LEX_TOKEN_IDENTIFIER);
		cxx_expect(c, LEX_TOKEN_SEMICOLON);
		return;
	}
	if (cxx_is(c, LEX_TOKEN_IDENTIFIER) && (cxx_peek(c, 1)->kind == LEX_TOKEN_EQUAL ||
						 (cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_SQUARE && cxx_peek(c, 2)->kind == LEX_TOKEN_LEFT_SQUARE) ||
						 cxx_peek(c, 1)->kw == CXX_KW___attribute__)) {
		/* alias-declaration: using name attrs = type-id; */
		uint32_t name = cxx_peek(c, 0)->id;
		cxx_entity *e;

		cxx_advance(c);
		cxx_attributes_skip(c);
		cxx_expect(c, LEX_TOKEN_EQUAL);
		uint32_t alias_b = c->pos;
		{
			uint32_t implicit = c->implicit_typename;

			c->implicit_typename = 1;
			cxx_type_id_parse(c);
			c->implicit_typename = implicit;
		}
		uint32_t alias_e = c->pos;
		{
			cxx_entity *target = c->last_type;
			uint8_t dep = c->last_type_dependent;

			cxx_expect(c, LEX_TOKEN_SEMICOLON);
			e = cxx_declare(c, cxx_declaration_scope(c), name, c->template_depth ? CXX_ENTITY_TEMPLATE : CXX_ENTITY_TYPE);
			if (e && c->template_depth && c->scope->kind == CXX_SCOPE_TEMPLATE) {
				e->kind = CXX_ENTITY_TEMPLATE;
				cxx_template_define(c, e, c->scope, cxx_declaration_scope(c), alias_b, alias_e, true);
			}
			else if (e) {
				e->kind = CXX_ENTITY_TYPE;
				e->dependent = dep;
				e->ty = c->type_out;
				if (c->type_out == CXX_TY_UNKNOWN && !c->template_depth && c->class_depth)
					cxx_class_unverifiable(c);
				if (target && target->members)	/* (dependent: followed outside templates, json::lexer::) */
					e->members = target->members, e->specs = target->specs, e->nspecs = target->nspecs;
			}
		}
		return;
	}
	/* using-declaration: using [typename] nested::name, ...; */
	do {
		cxx_scope *q;
		bool dep;
		cxx_entity *target;
		bool is_typename = cxx_accept_kw(c, CXX_KW_typename);

		cxx_nested_name_parse(c, &q, &dep);
		if (cxx_accept_kw(c, CXX_KW_operator)) {
			cxx_operator_name_parse(c);
		} else if (cxx_is(c, LEX_TOKEN_IDENTIFIER)) {
			uint32_t name = cxx_peek(c, 0)->id;

			target = cxx_name_lookup(c, q, dep, name);
			cxx_advance(c);
			if (target && !c->tentative) {
				/* the names it brings in: each entity, by kind */
				for (cxx_entity *t = target; t; t = t->next) {
					cxx_entity *e;

					if (t->kind == CXX_ENTITY_DEPENDENT) {
						e = cxx_declare(c, cxx_declaration_scope(c), name, is_typename ? CXX_ENTITY_TYPE : CXX_ENTITY_VALUE);
						if (e)
							e->dependent = 1;
						break;
					}
					e = cxx_declare(c, cxx_declaration_scope(c), name, t->kind);
					if (e && e != t) {
						/* the same entity under another name: all of what it is (std::uint64_t's type) */
						e->members = t->members, e->dependent = t->dependent;
						e->specs = t->specs, e->nspecs = t->nspecs;
						e->ty = t->ty, e->tmpl = t->tmpl, e->sym = t->sym, e->instance = t->instance;
						e->value = t->value, e->has_value = t->has_value;
						/* a function, a function or variable template, a concept: what calls and
						   evaluations of it need */
						e->ftmpl = t->ftmpl, e->ndefaults = t->ndefaults, e->fn_const = t->fn_const;
						e->late = t->late, e->late_done = t->late_done, e->member_of = t->member_of;
						/* (its name too: where it is declared, its linkage, as the original's) */
						e->home = t->home, e->c_linkage = t->c_linkage, e->global_ns = t->global_ns, e->mangled = t->mangled;
						e->fn_inline = t->fn_inline, e->fn_internal = t->fn_internal, e->abi_tag = t->abi_tag;
						e->no_explicit_inst = t->no_explicit_inst;
						e->fn_noexcept = t->fn_noexcept, e->fn_static = t->fn_static, e->defs = t->defs, e->def_scope = t->def_scope;
						e->field_of = t->field_of, e->sym1 = t->sym1, e->fn_defaulted = t->fn_defaulted, e->fn_deleted = t->fn_deleted;
					}
				}
			} else if (dep || is_typename) {
				cxx_entity *e = cxx_declare(c, cxx_declaration_scope(c), name, is_typename ? CXX_ENTITY_TYPE : CXX_ENTITY_VALUE);

				if (e)
					e->dependent = 1;
			}
		} else if (cxx_accept(c, LEX_TOKEN_TILDE)) {
			cxx_expect(c, LEX_TOKEN_IDENTIFIER);
		} else
			cxx_fail(c, "expected a name in a using-declaration");
		cxx_accept(c, LEX_TOKEN_ELLIPSIS);
		cxx_attributes_skip(c);
	} while (cxx_accept(c, LEX_TOKEN_COMMA));
	cxx_expect(c, LEX_TOKEN_SEMICOLON);
}

VPRIV void cxx_static_assert_parse(cxx *c)
{
	static int check = -1;
	bool known, value;
	uint32_t start = c->pos;

	cxx_advance(c);
	cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
	cxx_assignment_expression_parse(c);
	known = c->econst && !c->tentative && !c->template_depth, value = c->evalue != 0;
	if (!c->tentative && !c->template_depth && !c->unevaluated) {
		if (!known) {
			c->stats.asserts_unknown++;
			if (getenv("VCC_CXX_WHY")) {
				fprintf(stderr, "static_assert unknown:");
				for (uint32_t i = start + 2; i < c->pos && i < start + 16; i++)
					fprintf(stderr, " %.*s", (int)c->tok[i].len, c->tok[i].s);
				fputc('\n', stderr);
			}
		}
		else if (value)
			c->stats.asserts_true++;
		else {
			c->stats.asserts_false++;
			if (getenv("VCC_CXX_WHY")) {
				fprintf(stderr, "static_assert false:");
				for (uint32_t i = start; i < c->pos && i < start + 40; i++)
					fprintf(stderr, " %.*s", (int)c->tok[i].len, c->tok[i].s);
				fputc('\n', stderr);
			}
		}
	}
	if (cxx_accept(c, LEX_TOKEN_COMMA))
		cxx_assignment_expression_parse(c);	/* a string literal (or, in C++26, an expression) */
	cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
	/* evaluated; a false one is an error only on request for now (VCC_CXX_STATIC_ASSERT=1, the tests): a
	   type or constant vcc gets wrong must not reject valid code while its semantics are incomplete */
	if (check < 0)
		check = getenv("VCC_CXX_STATIC_ASSERT") ? atoi(getenv("VCC_CXX_STATIC_ASSERT")) : 0;
	if (check == 2 && !c->tentative && !c->template_depth && !c->instantiating && (!known || !value)) {
		/* =2: every one of them reported, by its message, and the parse goes on (probing a library) */
		fprintf(stderr, "static_assert %s:", known ? "FALSE" : "UNKNOWN");
		for (uint32_t i = start; i < c->pos && i < start + 60; i++)
			fprintf(stderr, " %.*s", (int)c->tok[i].len, c->tok[i].s);
		fputc('\n', stderr);
	} else if (check && !c->tentative && !c->template_depth && !c->instantiating) {
		if (!known)
			cxx_fail(c, "static assertion not evaluated");
		if (!value)
			cxx_fail(c, "static assertion failed");
	}
	cxx_expect(c, LEX_TOKEN_SEMICOLON);
}

/* the end of a template parameter's type, from p: the ',' '>' or '=' after it at depth 0 */
VPRIV uint32_t cxx_tparam_type_end(cxx *c, uint32_t p)
{
	int depth = 0;

	for (; p < c->ntok; p++) {
		int k = c->tok[p].kind;

		if (k == LEX_TOKEN_LEFT_PAREN || k == LEX_TOKEN_LEFT_SQUARE || k == LEX_TOKEN_LEFT_BRACE || k == LEX_TOKEN_LESS)
			depth++;
		else if (k == LEX_TOKEN_RIGHT_PAREN || k == LEX_TOKEN_RIGHT_SQUARE || k == LEX_TOKEN_RIGHT_BRACE)
			depth--;
		else if (k == LEX_TOKEN_GREATER || k == LEX_TOKEN_GREATER_GREATER) {
			if (depth <= 0)
				return p;
			depth -= k == LEX_TOKEN_GREATER ? 1 : 2;
			if (depth < 0)
				return p;	/* (>> closing this list too) */
		} else if (!depth && (k == LEX_TOKEN_COMMA || k == LEX_TOKEN_EQUAL || k == LEX_TOKEN_SEMICOLON))
			return p;
	}
	return p;
}
