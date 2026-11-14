/* C++ declarations: function bodies, deferred member bodies */
#include "declaration.h"

VPRIV void cxx_late_add(cxx *c, uint32_t begin, uint32_t end, int what)
{
	if (c->tentative)
		return;
	if (c->nlate == c->clate)
		c->late = realloc(c->late, (c->clate = c->clate ? c->clate * 2 : 64) * sizeof *c->late);
	c->late[c->nlate].begin = begin, c->late[c->nlate].end = end, c->late[c->nlate].scope = c->scope;
	c->late[c->nlate].nparams = c->ndecl_params;
	c->late[c->nlate].template_depth = c->template_depth;
	c->late[c->nlate].fn = c->last_fn, c->last_fn = NULL;
	c->late[c->nlate].params = c->ndecl_params ? malloc(c->ndecl_params * sizeof(cxx_param)) : NULL;
	if (c->ndecl_params)
		memcpy(c->late[c->nlate].params, c->decl_params, c->ndecl_params * sizeof(cxx_param));
	c->late[c->nlate].pack_n = UINT32_MAX, c->late[c->nlate].pack_types = NULL;
	if (c->cpack_n != UINT32_MAX && c->cpack_name > 1 && c->cpack_at + c->cpack_n <= c->ndecl_params) {
		c->late[c->nlate].pack_name = c->cpack_name, c->late[c->nlate].pack_at = c->cpack_at, c->late[c->nlate].pack_n = c->cpack_n;
		c->late[c->nlate].pack_types = malloc((c->cpack_n ? c->cpack_n : 1) * sizeof(qty));
		memcpy(c->late[c->nlate].pack_types, c->cpack_types, c->cpack_n * sizeof(qty));
	}
	c->late[c->nlate++].what = (uint8_t)what;
}

/* a late body's parameter pack of a class's type pack (As... a): a, a pack of those parameters; code generation
   fills its symbols (cxx_gen_function_begin) */
VPRIV void cxx_late_pack_declare(cxx *c, const cxx_late *l, cxx_scope *fs)
{
	cxx_entity *pe;

	if (l->pack_n == UINT32_MAX || !l->pack_types || !(pe = cxx_declare(c, fs, l->pack_name, CXX_ENTITY_VALUE)))
		return;
	pe->is_pack = 1, pe->pack_index = -1, pe->ty = CXX_TY_UNKNOWN, pe->npack = l->pack_n;
	pe->pack = calloc(l->pack_n ? l->pack_n : 1, sizeof *pe->pack);
	for (uint32_t i = 0; i < l->pack_n; i++)
		pe->pack[i].kind = 1, pe->pack[i].t = l->pack_types[i];
	pe->pack_syms = calloc(l->pack_n ? l->pack_n : 1, sizeof *pe->pack_syms);
	c->gen_pack_entity = pe, c->gen_pack_from = l->pack_at, c->gen_pack_n = l->pack_n;
}

VPRIV void cxx_function_body_inner_parse(cxx *c);

/* a function's parameters, declared (with their types) in its body's scope */
VPRIV void cxx_params_declare(cxx *c, cxx_scope *fs, const cxx_param *ps, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
		if (ps[i].name > 1) {
			cxx_entity *e = cxx_declare(c, fs, ps[i].name, CXX_ENTITY_VALUE);

			if (e)
				e->ty = ps[i].ty;
		}
}

/* : mem-initializers, then { body } (or a function-try-block) */
VPRIV void cxx_function_body_parse(cxx *c)
{
	uint32_t implicit = c->implicit_typename;
	uint8_t valid = c->name_path_valid;

	c->implicit_typename = 0;
	c->name_path_valid = 0;	/* local classes: not nameable outside */
	cxx_function_body_inner_parse(c);
	c->implicit_typename = implicit;
	c->name_path_valid = valid;
}

VPRIV void cxx_function_body_inner_parse(cxx *c)
{
	bool is_try = cxx_accept_kw(c, CXX_KW_try);

	if (cxx_accept(c, LEX_TOKEN_COLON)) {
		do {
			cxx_scope *q;
			bool dep;

			if (cxx_gen_on(c)) {
				/* code generation: a member of this class by name, else a base class (or the class itself:
				   a delegating constructor) by its type; then the arguments, and the constructor they pick */
				qty self_ty = c->gen_this ? AST_TYPE(c->ast, c->ast->syms[c->gen_this].ty)->base : CXX_TY_UNKNOWN;
				cxx_scope *self = self_ty != CXX_TY_UNKNOWN ? cxx_tag_scope_get(c, QUNQ(self_ty)) : NULL;
				cxx_entity *fe = cxx_is(c, LEX_TOKEN_IDENTIFIER) && !cxx_kw(c) && self &&
						 (cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_PAREN || cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_BRACE) ?
						 cxx_lookup_local(self, cxx_peek(c, 0)->id) : NULL;
				uint32_t mloc = cxx_peek(c, 0)->loc, mname = 0, an[CXX_MAX_ARGS], n;
				qty target = CXX_TY_UNKNOWN;
				bool braced;

				if (fe && fe->kind == CXX_ENTITY_VALUE && fe->field_of) {
					mname = fe->name;
					if (fe->ty != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, fe->ty) && AST_TYPE_KIND(c->ast, fe->ty) != TY_ARRAY)
						target = fe->ty;
					cxx_advance(c);
				} else {
					cxx_decl_specifiers_parse(c, NULL, false);
					target = c->ds_type;
					if (target == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, target))
						cxx_gen_fail(c);
				}
				braced = cxx_is(c, LEX_TOKEN_LEFT_BRACE);
				if (braced)
					cxx_braced_arguments_parse(c);
				else
					cxx_call_arguments_parse(c);
				n = c->ncargs;
				memcpy(an, c->carg_nodes, sizeof an);
				if (target != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, target))
					cxx_ctor_use(c, target, c->cargs, c->cargs_lval, c->ncargs, braced);
				cxx_gen_member_initializer(c, mname, target, an, n, mloc);
			} else {
			cxx_nested_name_parse(c, &q, &dep);
			if (!cxx_is(c, LEX_TOKEN_IDENTIFIER) && !cxx_is_kw(c, CXX_KW_decltype))
				cxx_fail(c, "expected a member or base name");
			if (cxx_is_kw(c, CXX_KW_decltype)) {
				cxx_advance(c);
				cxx_skip_balanced(c);
			} else {
				cxx_advance(c);
				if (cxx_is(c, LEX_TOKEN_LESS))
					cxx_template_arguments_parse(c);
			}
			if (cxx_is(c, LEX_TOKEN_LEFT_BRACE))
				cxx_braced_init_list_parse(c);
			else
				cxx_expression_list_parse(c);
			}
			if (cxx_accept(c, LEX_TOKEN_ELLIPSIS))
				cxx_gen_fail(c);
		} while (cxx_accept(c, LEX_TOKEN_COMMA));
	}
	cxx_compound_statement_parse(c);
	if (is_try)
		while (cxx_is_kw(c, CXX_KW_catch))
			cxx_statement_parse(c);
}

/* in a class: skip the body (with mem-initializers) and parse it when the class is complete */
VPRIV void cxx_function_body_defer(cxx *c)
{
	uint32_t begin = c->pos;

	cxx_accept_kw(c, CXX_KW_try);
	if (cxx_accept(c, LEX_TOKEN_COLON)) {
		/* mem-initializers: up to the { that starts the body (balanced ( and { in between) */
		while (!cxx_is(c, LEX_TOKEN_EOF)) {
			if (cxx_is(c, LEX_TOKEN_LEFT_PAREN) || cxx_is(c, LEX_TOKEN_LEFT_SQUARE)) {
				cxx_skip_balanced(c);
				continue;
			}
			if (cxx_is(c, LEX_TOKEN_LEFT_BRACE)) {
				/* a braced mem-initializer follows a name; the body follows ) } or ... */
				cxx_token *pv = &c->tok[c->pos - 1];

				if (pv->kind == LEX_TOKEN_IDENTIFIER || pv->kind == LEX_TOKEN_GREATER) {
					cxx_skip_balanced(c);
					continue;
				}
				break;
			}
			cxx_advance(c);
		}
	}
	if (!cxx_is(c, LEX_TOKEN_LEFT_BRACE))
		cxx_fail(c, "expected a function body");
	cxx_skip_balanced(c);
	while (cxx_is_kw(c, CXX_KW_catch)) {
		cxx_advance(c);
		cxx_skip_balanced(c);
		cxx_skip_balanced(c);
	}
	cxx_late_add(c, begin, c->pos, 0);
}

VPRIV void cxx_late_parse_all(cxx *c, uint32_t from)
{
	for (uint32_t i = from; i < c->nlate; i++) {
		cxx_late l = c->late[i];
		uint32_t save = c->pos, depth = c->class_depth, tdepth = c->template_depth;
		cxx_scope *ss = c->scope;
		cxx_scope *fs;

		c->pos = l.begin;
		c->this_const = l.fn && l.fn->fn_const;
		c->template_depth = l.template_depth;	/* a member template's body: names may be dependent */
		qty this_save = c->this_type;

		/* this: a pointer to the class (its type once the class is complete) */
		c->this_type = l.scope->kind == CXX_SCOPE_CLASS && l.scope->owner && l.scope->owner->ty ? l.scope->owner->ty : CXX_TY_UNKNOWN;
		c->scope = l.scope;
		c->class_depth = 0;
		fs = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL);
		fs->fn = l.fn;
		cxx_scope_push(c, fs);
		cxx_params_declare(c, fs, l.params, l.nparams);
		{
			/* code generation: a member function defined in its class (not a template's) */
			bool gen = l.what == 0 && l.fn && !l.template_depth && l.scope->kind == CXX_SCOPE_CLASS && !l.scope->dependent &&
				   l.scope->owner && !l.scope->owner->instance && !cxx_is_kw(c, CXX_KW_try);

			if (gen) {
				l.fn->fn_inline = 1;
				cxx_late_pack_declare(c, &l, fs);
				gen = cxx_gen_function_begin(c, l.fn, fs, l.params, l.nparams);
				c->gen_pack_entity = NULL;
			}
			free(l.params);
			cxx_function_body_parse(c);
			if (gen)
				cxx_gen_function_end(c, l.fn, c->snode);
		}
		if (c->pos != l.end)
			cxx_fail(c, "member function body ended early");
		c->scope = ss;
		c->pos = save;
		c->class_depth = depth;
		c->template_depth = tdepth;
		c->this_type = this_save;
	}
	c->nlate = from;
}

/* ---- namespaces, using, static_assert ---------------------------------------------------- */
