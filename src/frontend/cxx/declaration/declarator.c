/* C++ declarations: declarators, parameter clauses, function tails, type-ids */
#include "declaration.h"

/* does a ( after decl-specifiers start a nested declarator (not parameters)? */
VPRIV bool cxx_paren_is_nested_declarator(cxx *c)
{
	cxx_token *n = cxx_peek(c, 1);

	if (n->kind == LEX_TOKEN_STAR || n->kind == LEX_TOKEN_AMPERSAND || n->kind == LEX_TOKEN_AMPERSAND_AMPERSAND ||
	    n->kind == LEX_TOKEN_CARET)
		return true;
	if ((n->kind == LEX_TOKEN_IDENTIFIER && !n->kw) || n->kind == LEX_TOKEN_COLON_COLON) {
		cxx_entity *e = n->kind == LEX_TOKEN_IDENTIFIER ? cxx_lookup(c, n->id) : NULL;

		/* (X::*) / (a::b::*): a pointer to member; scan the nested name */
		{
			uint32_t k = 1;

			while (cxx_peek(c, k)->kind == LEX_TOKEN_COLON_COLON || (cxx_peek(c, k)->kind == LEX_TOKEN_IDENTIFIER &&
										 cxx_peek(c, k + 1)->kind == LEX_TOKEN_COLON_COLON))
				k++;
			if (k > 1 && cxx_peek(c, k)->kind == LEX_TOKEN_STAR)
				return true;
		}
		if (cxx_entity_is_type(cxx_entity_prefer(e, CXX_ENTITY_TYPE)) && !e->next && e->kind == CXX_ENTITY_TYPE)
			return false;	/* (T ...) parameters */
		if (cxx_peek(c, 2)->kind == LEX_TOKEN_COLON_COLON)
			return cxx_peek(c, 3)->kind == LEX_TOKEN_IDENTIFIER;
		return cxx_peek(c, 2)->kind == LEX_TOKEN_RIGHT_PAREN || cxx_peek(c, 2)->kind == LEX_TOKEN_LEFT_PAREN ||
		       cxx_peek(c, 2)->kind == LEX_TOKEN_LEFT_SQUARE;
	}
	return false;
}

/*
 * A declarator (abstract if allowed); returns the declared name's id (0: none) and sets
 * *qualified if the name had a nested-name-specifier and *is_function if the outermost
 * suffix is a parameter list.
 */
/* an integer literal's value (array bounds, bit-field widths; other constant expressions: -1, not evaluated yet) */
/*
 * The value of the constant expression here when it is one token: an integer literal, true or
 * false, or a name bound to a known integer (a value template argument, a const or constexpr
 * variable initialized with one).  -1 when it is not that (not evaluated yet).
 */
VPRIV int64_t cxx_literal_value(cxx *c)
{
	cxx_token *t = cxx_peek(c, 0);
	uint64_t v = 0;
	uint32_t i = 0;
	int next = cxx_peek(c, 1)->kind;
	bool ends = next == LEX_TOKEN_RIGHT_SQUARE || next == LEX_TOKEN_SEMICOLON || next == LEX_TOKEN_COMMA || next == LEX_TOKEN_EQUAL ||
		    next == LEX_TOKEN_LEFT_BRACE || (next == LEX_TOKEN_GREATER && cxx_ends_argument(c, 1)) || next == LEX_TOKEN_RIGHT_PAREN ||
		    next == LEX_TOKEN_RIGHT_BRACE;

	if (!ends)
		return -1;	/* an expression that starts here */
	if (t->kw == CXX_KW_true || t->kw == CXX_KW_false)
		return t->kw == CXX_KW_true;
	if (t->kind == LEX_TOKEN_IDENTIFIER && !t->kw) {
		cxx_entity *e = cxx_lookup(c, t->id);

		for (; e; e = e->next)
			if (e->kind == CXX_ENTITY_VALUE && e->has_value && e->value >= 0)
				return e->value;
		return -1;
	}
	if (t->kind != LEX_TOKEN_NUMBER)
		return -1;
	for (; i < t->len && t->s[i] >= '0' && t->s[i] <= '9'; i++)
		v = v * 10 + (uint64_t)(t->s[i] - '0');
	for (uint32_t k = i; k < t->len; k++)	/* suffixes u l only */
		if (t->s[k] != 'u' && t->s[k] != 'U' && t->s[k] != 'l' && t->s[k] != 'L')
			return -1;
	if (!i || (t->s[0] == '0' && t->len > 1 && i > 1))
		return -1;	/* (octal and hexadecimal: not here) */
	return (int64_t)v;
}


/*
 * A declarator (abstract if allowed); returns the declared name's id (0: none) and sets
 * *qualified if the name had a nested-name-specifier and *is_function if the outermost
 * suffix is a parameter list.  Its type: c->type_in is the base (the decl-specifiers'),
 * c->type_out the declared type.  C's inside-out rule: ptr-operators apply to the base,
 * then the suffixes from the last to the first, then a parenthesized declarator to that.
 */
VPRIV uint32_t cxx_declarator_parse(cxx *c, bool abstract_ok, bool *qualified, bool *is_function, cxx_scope **qscope)
{
	uint32_t name = 0, nested = 0, nested_end = 0;
	qty t = c->type_in;
	cxx_suffix sfx[8];
	uint32_t nsfx = 0;
	bool too_many = false;

	*qualified = false;
	*is_function = false;
	if (qscope)
		*qscope = NULL;
	while (cxx_ptr_operator_parse(c, &t))
		;
	cxx_attributes_skip(c);
	/* a pack declarator (Ts... ts, Ts&&...): not T... in a template argument list, that is an expansion */
	if (cxx_is(c, LEX_TOKEN_ELLIPSIS) && (cxx_peek(c, 1)->kind == LEX_TOKEN_IDENTIFIER || cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_PAREN ||
					      cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_SQUARE || !c->in_template_argument)) {
		cxx_advance(c);
		c->decl_pack = 1;
	}
	if (!abstract_ok && cxx_is(c, LEX_TOKEN_LEFT_SQUARE) && cxx_peek(c, 1)->kind == LEX_TOKEN_IDENTIFIER) {
		/* a structured binding: auto [a, b, ...c]: the declaration declares a variable of its own (__vcc_sb_N, the
		   type and initializer as written); the names refer to its members once it is made
		   (cxx_structured_bindings_declare) */
		bool pack = false;
		char nm[32];

		cxx_advance(c);
		c->sb_n = 0;
		do {
			pack |= cxx_accept(c, LEX_TOKEN_ELLIPSIS);
			if (!cxx_is(c, LEX_TOKEN_IDENTIFIER))
				cxx_fail(c, "expected a name in a structured binding");
			cxx_declare(c, cxx_declaration_scope(c), cxx_peek(c, 0)->id, CXX_ENTITY_VALUE);
			if (c->sb_n < 8)
				c->sb_names[c->sb_n] = cxx_peek(c, 0)->id, c->sb_locs[c->sb_n] = cxx_peek(c, 0)->loc;
			c->sb_n++;
			cxx_advance(c);
			cxx_attributes_skip(c);
		} while (cxx_accept(c, LEX_TOKEN_COMMA));
		cxx_expect(c, LEX_TOKEN_RIGHT_SQUARE);
		cxx_attributes_skip(c);
		if (pack || c->sb_n > 8 || abstract_ok) {
			c->sb_n = 0;
			c->type_out = CXX_TY_UNKNOWN;
			return 0;
		}
		snprintf(nm, sizeof nm, "__vcc_sb_%u", c->sb_serial++);
		c->sb_var = vintern_cstr(nm);
		c->type_out = t;
		return c->sb_var;
	}
	if (cxx_is(c, LEX_TOKEN_LEFT_PAREN) && cxx_paren_is_nested_declarator(c)) {
		nested = c->pos;	/* parsed after the suffixes (their type is its base) */
		cxx_skip_balanced(c);
		nested_end = c->pos;
	} else if (cxx_is(c, LEX_TOKEN_IDENTIFIER) || cxx_is(c, LEX_TOKEN_COLON_COLON) || cxx_is(c, LEX_TOKEN_TILDE)) {
		cxx_scope *q;
		bool dep;
		uint32_t save = c->pos;

		cxx_nested_name_parse(c, &q, &dep);
		if (c->pos != save)
			*qualified = true;
		if (qscope)
			*qscope = q ? q : c->qualifier_hint;
		if (cxx_accept(c, LEX_TOKEN_TILDE)) {
			if (!cxx_is(c, LEX_TOKEN_IDENTIFIER))
				cxx_fail(c, "expected a class name after '~'");
			name = cxx_peek(c, 0)->id;
			cxx_advance(c);
			if (cxx_is(c, LEX_TOKEN_LESS))
				cxx_template_arguments_parse(c);
		} else if (cxx_is_kw(c, CXX_KW_operator)) {
			cxx_advance(c);
			c->conv_last = CXX_TY_UNKNOWN;
			name = cxx_operator_name_parse(c);	/* (1: new/delete, a conversion of a template: not entered in a scope) */
			if (c->conv_last != CXX_TY_UNKNOWN)
				t = c->conv_last;	/* a conversion function returns the type it names */
			if (cxx_is(c, LEX_TOKEN_LESS) && cxx_lookahead(c, cxx_template_arguments_then_paren))
				cxx_template_arguments_parse(c);	/* operator+<char, ...>( : a specialization */
		} else if (cxx_is(c, LEX_TOKEN_IDENTIFIER) && !cxx_kw(c)) {
			name = cxx_peek(c, 0)->id;
			cxx_advance(c);
			/* an explicit specialization's or a qualified template's arguments */
			c->decl_less = 0;
			if (cxx_is(c, LEX_TOKEN_LESS)) {
				cxx_entity *e = cxx_lookup(c, name);

				if (((*qualified || c->template_depth || c->explicit_specialization) && (*qualified || (e && cxx_entity_template(e)))) ||
				    (e && cxx_entity_template(e) && cxx_lookahead(c, cxx_template_arguments_then_paren))) {
					c->decl_less = c->pos;
					cxx_template_arguments_parse(c);	/* a specialization, or an explicit instantiation's */
				}
			}
		} else if (!abstract_ok || *qualified)
			cxx_fail(c, "expected a declarator name");
	} else if (!abstract_ok)
		cxx_fail(c, "expected a declarator");
	cxx_attributes_skip(c);
	/* X::f(params): the parameters and array bounds see X's members (and a template's specializations) */
	cxx_scope *outer = c->scope;

	if (*qualified && qscope && *qscope && !c->tentative) {
		cxx_scope *ms = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL);

		cxx_scope_use(c, ms, *qscope);
		if ((*qscope)->owner)
			for (uint32_t i = 0; i < (*qscope)->owner->nspecs; i++)
				cxx_scope_use(c, ms, (*qscope)->owner->specs[i]);
		c->scope = ms;
	} else if (*qualified && qscope && *qscope) {
		/* (tentative: cxx_scope_use records nothing; a member scope pushed as is) */
		cxx_scope *ms = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL);

		ms->used = malloc(sizeof *ms->used * (1 + ((*qscope)->owner ? (*qscope)->owner->nspecs : 0)));
		ms->used[ms->nused++] = *qscope;
		if ((*qscope)->owner)
			for (uint32_t i = 0; i < (*qscope)->owner->nspecs; i++)
				ms->used[ms->nused++] = (*qscope)->owner->specs[i];
		c->scope = ms;
	}
	/* suffixes */
	for (;;) {
		cxx_suffix *x = nsfx < 8 ? &sfx[nsfx] : NULL;

		if (cxx_is(c, LEX_TOKEN_LEFT_SQUARE) && cxx_peek(c, 1)->kind != LEX_TOKEN_LEFT_SQUARE) {
			int64_t n = -1;

			cxx_advance(c);
			if (!cxx_is(c, LEX_TOKEN_RIGHT_SQUARE)) {
				cxx_entity *be = cxx_is(c, LEX_TOKEN_IDENTIFIER) && !cxx_kw(c) && cxx_peek(c, 1)->kind == LEX_TOKEN_RIGHT_SQUARE ?
						 cxx_lookup(c, cxx_peek(c, 0)->id) : NULL;

				n = cxx_literal_value(c);
				if (n < 0)
					n = -2;	/* a bound not evaluated yet: the type is unknown */
				/* T[N] in a pattern, N a value parameter's placeholder: an array whose bound is deduced */
				if (be && be->kind == CXX_ENTITY_VALUE && be->placeholder)
					n = -3 - (int64_t)(be->placeholder - 1);
				cxx_expression_parse(c);
				if (n == -2 && c->econst && !c->tentative && !c->template_depth && c->evalue > 0)
					n = c->evalue;	/* (a constant expression: int a[N * 2]) */
			}
			cxx_expect(c, LEX_TOKEN_RIGHT_SQUARE);
			cxx_attributes_skip(c);
			if (x)
				x->is_function = 0, x->n = n, nsfx++;
			else
				too_many = true;
			continue;
		}
		if (cxx_is(c, LEX_TOKEN_LEFT_PAREN)) {
			/* parameters if they parse as such; else ( starts a direct-initializer: T x(a, b) */
			if (cxx_peek(c, 1)->kind != LEX_TOKEN_RIGHT_PAREN && !cxx_lookahead(c, cxx_parameter_clause_parse))
				break;
			cxx_parameter_clause_parse(c);
			if (x) {
				x->is_function = 1, x->variadic = c->fn_variadic;
				x->nparams = c->fn_unknown ? UINT32_MAX : c->fn_nparams;
				x->params = c->fn_nparams && !c->fn_unknown ? malloc(c->fn_nparams * sizeof(qty)) : NULL;
				if (x->params)
					memcpy(x->params, c->fn_params, c->fn_nparams * sizeof(qty));
				/* the names, for the body (the parameters' own types, unknown ones too) */
				x->nnamed = c->fn_nparams, x->ndefaults = c->fn_ndefaults, x->pack = c->fn_pack;
				x->named = c->fn_nparams ? malloc(c->fn_nparams * sizeof *x->named) : NULL;
				for (uint32_t k = 0; k < c->fn_nparams; k++)
					x->named[k].name = c->fn_pnames[k], x->named[k].ty = c->fn_pdecl[k];
				nsfx++;
			} else
				too_many = true;
			cxx_function_tail_parse(c);
			if (x)
				x->trailing = c->fn_trailing, x->is_const = c->fn_const, x->is_noexcept = c->fn_noexcept;
			if (!nested)
				c->decl_has_trailing = c->fn_has_trailing;
			*is_function = true;
			continue;
		}
		break;
	}
	c->scope = outer;
	/* the body's parameters: the first suffix's, when it is a function (not a pointer to one) */
	if (nsfx && sfx[0].is_function && !nested && !c->tentative) {
		if (sfx[0].nnamed > c->cdecl_params)
			c->decl_params = realloc(c->decl_params, (c->cdecl_params = sfx[0].nnamed) * sizeof *c->decl_params);
		if (sfx[0].nnamed)
			memcpy(c->decl_params, sfx[0].named, sfx[0].nnamed * sizeof *c->decl_params);
		c->ndecl_params = sfx[0].nnamed;
		c->decl_ndefaults = sfx[0].ndefaults, c->decl_const = sfx[0].is_const, c->decl_variadic = sfx[0].variadic;
		c->decl_noexcept = sfx[0].is_noexcept;
		c->decl_pack_param = sfx[0].pack;
	} else if (!nested && !c->tentative)
		c->decl_ndefaults = 0, c->decl_const = 0, c->decl_noexcept = 0;
	for (uint32_t i = 0; i < nsfx; i++)
		if (sfx[i].is_function)
			free(sfx[i].named);
	/* the type: suffixes from the last (innermost) to the first */
	for (uint32_t i = nsfx; i-- > 0;) {
		if (too_many)
			t = CXX_TY_UNKNOWN;
		else if (sfx[i].is_function) {
			if (sfx[i].trailing != CXX_TY_UNKNOWN)
				t = sfx[i].trailing;	/* (replaces auto) */
			if (i == 0 && !nested)
				c->decl_return = t;	/* (known even when a parameter's type is not) */
			t = sfx[i].nparams == UINT32_MAX ? CXX_TY_UNKNOWN : cxx_type_function(c, t, sfx[i].params, sfx[i].nparams, sfx[i].variadic);
		}
		else
			t = sfx[i].n == -2 ? CXX_TY_UNKNOWN : cxx_type_array(c, t, sfx[i].n);
		if (sfx[i].is_function)
			free(sfx[i].params);
	}
	if (nested) {
		/* ( declarator ): its own ptr-operators and suffixes apply to this type */
		uint32_t end = c->pos;
		bool q2, f2;

		/* (*f)(int) declares a pointer, (f)(int) a function */
		cxx_token *first = &c->tok[nested + 1];
		bool pointer = first->kind == LEX_TOKEN_STAR || first->kind == LEX_TOKEN_AMPERSAND ||
			       first->kind == LEX_TOKEN_AMPERSAND_AMPERSAND || first->kind == LEX_TOKEN_CARET ||
			       (first->kind == LEX_TOKEN_IDENTIFIER && c->tok[nested + 2].kind == LEX_TOKEN_COLON_COLON);

		c->pos = nested + 1;
		c->type_in = t;
		name = cxx_declarator_parse(c, abstract_ok, &q2, &f2, qscope);
		*is_function = f2 || (*is_function && !pointer);
		t = c->type_out;
		cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
		if (c->pos != nested_end)
			cxx_fail(c, "a parenthesized declarator ended early");
		c->pos = end;
		*qualified = q2;
	}
	c->type_out = t;
	return name;
}

/* ( parameter-declaration-clause ) */
VPRIV void cxx_parameter_clause_parse(cxx *c)
{
	uint32_t implicit = c->implicit_typename;

	c->implicit_typename = 0;
	cxx_parameter_clause_body_parse(c);
	c->implicit_typename = implicit;
}

VPRIV void cxx_parameter_clause_body_parse(cxx *c)
{
	qty *types = NULL, *decls = NULL;
	uint32_t *names = NULL;
	uint32_t n = 0, cap = 0, ndefaults = 0, pack = UINT32_MAX, def_tok[16];
	uint8_t variadic = 0, unknown = 0;

	cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
	c->cpack_n = UINT32_MAX;	/* (this list's) */
	if (!cxx_accept(c, LEX_TOKEN_RIGHT_PAREN)) {
		for (;;) {
			bool q, f;
			qty t;

			if (cxx_accept(c, LEX_TOKEN_ELLIPSIS)) {
				variadic = 1;
				break;
			}
			cxx_attributes_skip(c);
			uint32_t spec_pos = c->pos;

			if (!(cxx_decl_specifiers_parse(c, NULL, false) & (DS_TYPE | DS_MODIFIER)))
				cxx_fail(c, "expected a parameter type");
			uint32_t pname, decl_pos = c->pos;

			c->type_in = c->ds_type;
			c->decl_pack = 0;
			pname = cxx_declarator_parse(c, true, &q, &f, NULL);
			if (c->decl_pack && !c->template_depth && !c->tentative && !c->sig_own_pack) {	/* (a function template instance's own pack: expanded below) */
				/* Ts... args (Ts&&..., const Ts&...) in an instance, Ts a bound type pack: a parameter per
				   element, the declarator applied to each (std::function's operator()(_ArgTypes... __arg)) */
				uint32_t k0 = spec_pos;
				cxx_entity *pe;

				while (k0 < decl_pos && (c->tok[k0].kw == CXX_KW_const || c->tok[k0].kw == CXX_KW_volatile))
					k0++;
				pe = k0 < decl_pos && c->tok[k0].kind == LEX_TOKEN_IDENTIFIER && !c->tok[k0].kw ? cxx_lookup(c, c->tok[k0].id) : NULL;
				if (pe && pe->is_pack && !pe->pack_placeholder && pe->pack && pe->pack_index < 0) {	/* (its elements types: checked below) */
					bool all = true, cq = k0 > spec_pos || (k0 + 1 < decl_pos && c->tok[k0 + 1].kw == CXX_KW_const);
					uint32_t end = c->pos;

					for (uint32_t k = 0; k < pe->npack && all; k++)
						all = pe->pack[k].kind == 1 && pe->pack[k].t != CXX_TY_UNKNOWN;
					if (all && !cxx_is(c, LEX_TOKEN_EQUAL) && pe->npack <= 32) {
						c->cpack_name = pname, c->cpack_at = n, c->cpack_n = pe->npack;
						for (uint32_t k = 0; k < pe->npack; k++) {
							bool q2, f2;
							qty et = pe->pack[k].t;

							if (cq)
								et = QT(QI(et), QQ(et) | Q_CONST);
							c->pos = decl_pos, c->type_in = et, c->decl_pack = 0;
							cxx_declarator_parse(c, true, &q2, &f2, NULL);
							if (n == cap) {
								cap = cap ? cap * 2 : 8;
								types = realloc(types, cap * sizeof *types);
								names = realloc(names, cap * sizeof *names);
								decls = realloc(decls, cap * sizeof *decls);
							}
							names[n] = 0;
							decls[n] = types[n] = cxx_type_parameter_adjust(c, c->type_out);
							c->cpack_types[k] = types[n];
							n++;
						}
						c->pos = end, c->decl_pack = 0;
						if (!cxx_accept(c, LEX_TOKEN_COMMA))
							break;
						continue;
					}
				}
			}
			if (c->decl_pack)
				pack = n;	/* a parameter pack: Ts&&... ts */
			t = cxx_type_parameter_adjust(c, c->type_out);
			/* in the body: the adjusted type with the declaration's own const (not part of the function's type) */
			qty dt = t == CXX_TY_UNKNOWN || c->type_out == CXX_TY_UNKNOWN || AST_TYPE_KIND(c->ast, c->type_out) == TY_ARRAY ||
				 AST_TYPE_KIND(c->ast, c->type_out) == TY_FUNC ? t : QT(QI(t), QQ(t) | (QQ(c->type_out) & (Q_CONST | Q_VOLATILE)));

			if (cxx_accept(c, LEX_TOKEN_EQUAL)) {
				uint32_t inner = ndefaults, db = c->pos;

				cxx_initializer_parse(c);	/* (a lambda's parameters inside: not ours) */
				ndefaults = inner + 1;
				if (inner < 8)
					def_tok[2 * inner] = db, def_tok[2 * inner + 1] = c->pos;
			}
			if (cxx_is(c, LEX_TOKEN_ELLIPSIS) && c->pos && c->tok[c->pos - 1].kind == LEX_TOKEN_IDENTIFIER && !c->tok[c->pos - 1].kw &&
			    !c->template_depth) {
				/* R(Args...) in an instance, Args a bound type pack: a parameter per element (__base<R(Args...)>) */
				cxx_entity *pe = cxx_lookup(c, c->tok[c->pos - 1].id);

				if (pe && pe->is_pack && !pe->pack_placeholder && pe->pack && pe->pack_index < 0 && pe->kind == CXX_ENTITY_TYPE) {
					bool all = true;

					for (uint32_t k = 0; k < pe->npack && all; k++)
						all = pe->pack[k].kind == 1 && pe->pack[k].t != CXX_TY_UNKNOWN;
					if (all) {
						cxx_advance(c);
						for (uint32_t k = 0; k < pe->npack; k++) {
							if (n == cap) {
								cap = cap ? cap * 2 : 8;
								types = realloc(types, cap * sizeof *types);
								names = realloc(names, cap * sizeof *names);
								decls = realloc(decls, cap * sizeof *decls);
							}
							names[n] = 0;
							decls[n] = types[n] = cxx_type_parameter_adjust(c, pe->pack[k].t);
							n++;
						}
						if (!cxx_accept(c, LEX_TOKEN_COMMA))
							break;
						continue;
					}
				}
			}
			if (cxx_is(c, LEX_TOKEN_ELLIPSIS) && c->pos && c->tok[c->pos - 1].kind == LEX_TOKEN_IDENTIFIER && !c->tok[c->pos - 1].kw &&
			    t != CXX_TY_UNKNOWN) {
				/* R(Args...) in a pattern, Args a pack's placeholder: a parameter pack (function<R(Args...)>) */
				cxx_entity *pe = cxx_lookup(c, c->tok[c->pos - 1].id);

				if (pe && pe->is_pack && pe->pack_placeholder) {
					cxx_advance(c);
					pack = n;
				}
			}
			if (cxx_accept(c, LEX_TOKEN_ELLIPSIS)) {	/* (int...): a pack or a C variadic */
				variadic = 1;
				unknown = 1;
			}
			if (t == CXX_TY_UNKNOWN)
				unknown = 1;
			/* (void): no parameters */
			if (!(n == 0 && t == cc_type_basic(TY_VOID) && cxx_is(c, LEX_TOKEN_RIGHT_PAREN))) {
				if (n == cap) {
					cap = cap ? cap * 2 : 8;
					types = realloc(types, cap * sizeof *types);
					names = realloc(names, cap * sizeof *names);
					decls = realloc(decls, cap * sizeof *decls);
				}
				names[n] = pname;
				decls[n] = dt;
				types[n++] = t;
			}
			if (variadic || !cxx_accept(c, LEX_TOKEN_COMMA))
				break;
		}
		cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
	}
	if (n > c->fn_cparams) {
		c->fn_cparams = n;
		c->fn_params = realloc(c->fn_params, n * sizeof(qty));
		c->fn_pnames = realloc(c->fn_pnames, n * sizeof(uint32_t));
		c->fn_pdecl = realloc(c->fn_pdecl, n * sizeof(qty));
	}
	if (n) {
		memcpy(c->fn_params, types, n * sizeof(qty));
		memcpy(c->fn_pnames, names, n * sizeof(uint32_t));
		memcpy(c->fn_pdecl, decls, n * sizeof(qty));
	}
	c->fn_nparams = n, c->fn_variadic = variadic, c->fn_unknown = unknown, c->fn_ndefaults = ndefaults;
	c->fn_ndefs = ndefaults <= 8 ? ndefaults : 0;
	memcpy(c->fn_defs, def_tok, 2 * c->fn_ndefs * sizeof *def_tok);
	c->fn_pack = pack;
	free(types);
	free(names);
	free(decls);
}

/* noexcept, noexcept(e), throw(...): 1 not throwing, 0 throwing, 2 not known (a condition not evaluated) */
VPRIV int cxx_noexcept_parse(cxx *c)
{
	int r = 0;

	if (cxx_accept_kw(c, CXX_KW_noexcept)) {
		r = 1;
		if (cxx_is(c, LEX_TOKEN_LEFT_PAREN)) {
			cxx_advance(c);
			c->unevaluated++;	/* (noexcept(noexcept(f(x))): what it calls is not used) */
			cxx_constant_expression_parse(c);
			c->unevaluated--;
			r = c->econst ? c->evalue != 0 : 2;
			cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
		}
	} else if (cxx_accept_kw(c, CXX_KW_throw)) {
		r = cxx_is(c, LEX_TOKEN_LEFT_PAREN) && cxx_peek(c, 1)->kind == LEX_TOKEN_RIGHT_PAREN;	/* (throw(): not throwing) */
		cxx_skip_balanced(c);
	}
	return r;
}

/* cv, ref-qualifier, noexcept, attributes, -> trailing return type */
VPRIV void cxx_function_tail_parse(cxx *c)
{
	bool is_const = false;
	uint8_t is_noexcept = 0;
	cxx_scope *outer = c->scope;

	/* The parameters are in scope in a noexcept-specifier and a trailing return type: -> decltype(a < b).
	   Declared from here, before either is parsed: what a noexcept(...) instantiates has declarators of
	   its own, which leave their parameters in c->fn_*. */
	if (!c->tentative && !c->template_depth && c->fn_nparams && !c->fn_variadic) {
		cxx_scope *ps = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL);
		uint32_t n = c->fn_nparams;
		cxx_param *save = malloc(n * sizeof *save);

		for (uint32_t i = 0; i < n; i++)
			save[i].name = c->fn_pnames[i], save[i].ty = c->fn_pdecl[i];
		cxx_scope_push(c, ps);
		cxx_params_declare(c, ps, save, n);
		free(save);
	}
	for (;;) {
		if (cxx_kw_is_cv(cxx_kw(c)) || cxx_is(c, LEX_TOKEN_AMPERSAND) || cxx_is(c, LEX_TOKEN_AMPERSAND_AMPERSAND)) {
			is_const |= cxx_is_kw(c, CXX_KW_const);
			cxx_advance(c);
			continue;
		}
		if (cxx_is_kw(c, CXX_KW_noexcept) || cxx_is_kw(c, CXX_KW_throw)) {
			is_noexcept = (uint8_t)cxx_noexcept_parse(c);
			continue;
		}
		if (cxx_is(c, LEX_TOKEN_LEFT_SQUARE) && cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_SQUARE) {
			cxx_attributes_skip(c);
			continue;
		}
		if (cxx_is_kw(c, CXX_KW___attribute__) || cxx_is_kw(c, CXX_KW___attribute)) {
			cxx_attributes_skip(c);
			continue;
		}
		break;
	}
	if (cxx_accept(c, LEX_TOKEN_ARROW)) {
		cxx_type_id_parse(c);
		c->scope = outer;
		c->fn_trailing = c->type_out;	/* auto f() -> T: the return type */
		c->fn_const = is_const, c->fn_noexcept = is_noexcept, c->fn_has_trailing = 1;
		return;
	}
	c->scope = outer;
	c->fn_trailing = CXX_TY_UNKNOWN, c->fn_const = is_const, c->fn_noexcept = is_noexcept, c->fn_has_trailing = 0;
}

/* the type of a functional cast T(x) / T{x}: type-specifiers only, no declarator */
VPRIV void cxx_type_specifiers_parse(cxx *c)
{
	if (!(cxx_decl_specifiers_parse(c, NULL, false) & (DS_TYPE | DS_MODIFIER)))
		cxx_fail(c, "expected a type");
}

/* new-type-id: type-specifiers, ptr-operators and [n]... (no parentheses: T(args) is the initializer) */
/* new-type-id: c->type_out the type allocated (an array's element), c->new_count its first bound's node
   (new_array: there is one), CXX_TY_UNKNOWN when not known */
VPRIV void cxx_new_type_id_parse(cxx *c)
{
	qty t;
	uint32_t dims = 0;

	cxx_type_specifiers_parse(c);
	t = c->ds_type;
	while (cxx_ptr_operator_parse(c, t == CXX_TY_UNKNOWN ? NULL : &t))
		;
	c->new_array = 0, c->new_count = 0;
	while (cxx_is(c, LEX_TOKEN_LEFT_SQUARE) && cxx_peek(c, 1)->kind != LEX_TOKEN_LEFT_SQUARE) {
		cxx_advance(c);
		if (!cxx_is(c, LEX_TOKEN_RIGHT_SQUARE)) {
			cxx_expression_parse(c);
			if (!dims)
				c->new_array = 1, c->new_count = c->enode;
			else if (c->econst && t != CXX_TY_UNKNOWN)
				t = cc_type_array(c->ast, t, (uint64_t)c->evalue, 0);	/* (new T[n][4]: of T[4]) */
			else
				t = CXX_TY_UNKNOWN;
		}
		dims++;
		cxx_expect(c, LEX_TOKEN_RIGHT_SQUARE);
	}
	c->type_out = t;
}

/* type-id: type-specifiers and an abstract declarator */
/* a handler's exception-declaration: a type and perhaps a name (the result; c->type_out the type) */
VPRIV uint32_t cxx_handler_declaration_parse(cxx *c)
{
	bool q, f;
	uint32_t name;

	if (!(cxx_decl_specifiers_parse(c, NULL, false) & (DS_TYPE | DS_MODIFIER)))
		cxx_fail(c, "expected a type");
	c->type_in = c->ds_type;
	name = cxx_declarator_parse(c, true, &q, &f, NULL);
	if (!cxx_is(c, LEX_TOKEN_RIGHT_PAREN))
		cxx_fail(c, "expected ')'");
	return name;
}

VPRIV void cxx_type_id_parse(cxx *c)
{
	bool q, f;
	cxx_entity *lt = c->last_type;
	uint8_t ld = c->last_type_dependent;

	if (!(cxx_decl_specifiers_parse(c, NULL, false) & (DS_TYPE | DS_MODIFIER)))
		cxx_fail(c, "expected a type");
	{
		cxx_entity *nlt = c->last_type;
		uint8_t nld = c->last_type_dependent;

		c->type_in = c->ds_type;
		if (cxx_declarator_parse(c, true, &q, &f, NULL))
			cxx_fail(c, "a type-id declares no name");	/* (T(f)) is an expression, not a cast */
		c->last_type = nlt, c->last_type_dependent = nld;
	}
	(void)lt, (void)ld;
}

VPRIV void cxx_type_id_then_end(cxx *c)
{
	cxx_type_id_parse(c);
	cxx_accept(c, LEX_TOKEN_ELLIPSIS);
	/* a type-id is only a template argument or a parenthesized type if it ends there */
	if (!(cxx_is(c, LEX_TOKEN_COMMA) || cxx_is(c, LEX_TOKEN_RIGHT_PAREN) || cxx_is(c, LEX_TOKEN_GREATER) ||
	      cxx_is(c, LEX_TOKEN_GREATER_GREATER) || cxx_is(c, LEX_TOKEN_GREATER_EQUAL) || cxx_is(c, LEX_TOKEN_GREATER_GREATER_EQUAL) ||
	      cxx_is(c, LEX_TOKEN_RIGHT_SQUARE) || cxx_is(c, LEX_TOKEN_SEMICOLON) || cxx_is(c, LEX_TOKEN_RIGHT_BRACE)))
		cxx_fail(c, "not a type-id");
}

/* does a type-id start here (and end before , ) > ] ;)? */
VPRIV bool cxx_try_type_id(cxx *c)
{
	cxx_token *t = cxx_peek(c, 0);

	/* quick: what cannot start a type */
	if (t->kind != LEX_TOKEN_IDENTIFIER && t->kind != LEX_TOKEN_COLON_COLON)
		return false;
	return cxx_lookahead(c, cxx_type_id_then_end);
}

/* ---- initializers ------------------------------------------------------------------------ */
