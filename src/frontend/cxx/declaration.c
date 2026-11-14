/*
 * declaration.c: C++ declarations (cxx-internal.h): namespaces, using,
 * templates, classes, enums, decl-specifiers and declarators.
 *
 * Member function bodies are parsed once the outermost class is complete
 * (they may name members declared after them), as clang does; default
 * member initializers and default arguments are parsed in place.
 */
#include "cxx-internal.h"

/* ---- tentative parsing ------------------------------------------------------------------- */

/* does fn parse from here?  Always rewinds: a tentative parse decides, the real one follows */
static bool cxx_lookahead(cxx *c, void (*fn)(cxx *))
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

/* ---- attributes -------------------------------------------------------------------------- */

VPRIV void cxx_attributes_skip(cxx *c)
{
	for (;;) {
		if (cxx_is(c, LEX_TOKEN_LEFT_SQUARE) && cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_SQUARE) {
			cxx_skip_balanced(c);
			continue;
		}
		if (cxx_is_kw(c, CXX_KW___attribute__) || cxx_is_kw(c, CXX_KW___attribute) || cxx_is_kw(c, CXX_KW___declspec) ||
		    cxx_is_kw(c, CXX_KW_alignas) || cxx_is_kw(c, CXX_KW__Alignas)) {
			cxx_advance(c);
			if (!cxx_is(c, LEX_TOKEN_LEFT_PAREN))
				cxx_fail(c, "expected '(' after attribute");
			cxx_skip_balanced(c);
			continue;
		}
		if (cxx_is_kw(c, CXX_KW___extension__) || cxx_is_kw(c, CXX_KW__Nullable) || cxx_is_kw(c, CXX_KW__Nonnull) ||
		    cxx_is_kw(c, CXX_KW__Null_unspecified)) {
			cxx_advance(c);
			continue;
		}
		break;
	}
}

/* ---- names ------------------------------------------------------------------------------- */

static bool cxx_entity_is_type(const cxx_entity *e)
{
	return e && e->kind == CXX_ENTITY_TYPE;
}

static bool cxx_entity_is_template(const cxx_entity *e)
{
	return e && (e->kind == CXX_ENTITY_TEMPLATE || e->kind == CXX_ENTITY_FUNCTION_TEMPLATE ||
		     e->kind == CXX_ENTITY_VARIABLE_TEMPLATE || e->kind == CXX_ENTITY_CONCEPT);
}

/* the entity a name denotes in qualifier (NULL: unqualified); NULL if none */
static cxx_entity *cxx_name_lookup(cxx *c, cxx_scope *qualifier, bool dependent, uint32_t id)
{
	if (dependent) {
		c->dependent_seen = 1;
		return cxx_entity_dependent();
	}
	if (qualifier) {
		cxx_entity *e = cxx_lookup_in(c, qualifier, id, 0);

		if (!e && qualifier->owner)	/* a class template's specializations */
			for (uint32_t i = 0; i < qualifier->owner->nspecs && !e; i++)
				e = cxx_lookup_in(c, qualifier->owner->specs[i], id, 0);
		return e;
	}
	return cxx_lookup(c, id);
}

/* an entity's kind preferring what the parser can use: a type or template over a value
   with the same name (struct stat / stat()) only when elaborated, else the first */
static cxx_entity *cxx_entity_prefer(cxx_entity *e, int kind)
{
	for (cxx_entity *x = e; x; x = x->next)
		if (x->kind == kind)
			return x;
	return e;
}

static cxx_entity *cxx_entity_template(cxx_entity *e)
{
	for (cxx_entity *x = e; x; x = x->next)
		if (cxx_entity_is_template(x))
			return x;
	return NULL;
}

/*
 * nested-name-specifier: [::] (name [<args>] | decltype(...) | template name<args>) :: ...
 * On return *qualifier is the scope named (NULL if none; the global scope for a leading ::)
 * and *dependent says a part depends on template parameters.  Returns the last entity.
 */
VPRIV cxx_entity *cxx_nested_name_parse(cxx *c, cxx_scope **qualifier, bool *dependent)
{
	cxx_entity *last = NULL;

	*qualifier = NULL;
	*dependent = false;
	c->qualifier_hint = NULL;
	if (cxx_is(c, LEX_TOKEN_COLON_COLON)) {
		cxx_advance(c);
		*qualifier = c->global;
	}
	for (;;) {
		cxx_token *t = cxx_peek(c, 0);

		if (t->kind == LEX_TOKEN_IDENTIFIER && !t->kw && cxx_peek(c, 1)->kind == LEX_TOKEN_COLON_COLON) {
			cxx_entity *e = cxx_name_lookup(c, *qualifier, *dependent, t->id);

			cxx_advance(c);
			cxx_advance(c);
			e = e ? cxx_entity_prefer(cxx_entity_prefer(e, CXX_ENTITY_NAMESPACE), CXX_ENTITY_TYPE) : NULL;
			if (!e || e->kind == CXX_ENTITY_DEPENDENT || !e->members || (e->dependent && c->template_depth))
				*dependent = true, *qualifier = NULL;
			else
				*qualifier = e->members;
			c->qualifier_hint = e && e->members ? e->members : NULL;
			last = e;
			continue;
		}
		if (t->kind == LEX_TOKEN_IDENTIFIER && (!t->kw || t->kw == CXX_KW_template) && (t->kw || cxx_peek(c, 1)->kind == LEX_TOKEN_LESS)) {
			/* name<args>:: or template name<args>:: */
			uint32_t save = c->pos, save_undo = c->nundo;
			bool kw = t->kw == CXX_KW_template;
			cxx_entity *e;

			if (kw)
				cxx_advance(c);
			t = cxx_peek(c, 0);
			if (t->kind != LEX_TOKEN_IDENTIFIER || cxx_peek(c, 1)->kind != LEX_TOKEN_LESS) {
				cxx_rewind(c, save, save_undo);
				break;
			}
			e = cxx_name_lookup(c, *qualifier, *dependent, t->id);
			if (!kw && !(e && (e->kind == CXX_ENTITY_DEPENDENT || cxx_entity_template(e))) &&
			    cxx_builtin_kind(t->id) != CXX_BUILTIN_TEMPLATE) {
				cxx_rewind(c, save, save_undo);
				break;
			}
			{
				cxx_entity *te = e ? cxx_entity_prefer(e, CXX_ENTITY_TEMPLATE) : NULL;
				uint8_t seen = c->dependent_seen;
				bool args_dependent;

				c->dependent_seen = 0;
				cxx_advance(c);
				cxx_template_arguments_parse(c);
				args_dependent = c->dependent_seen || *dependent;
				c->dependent_seen |= seen;
				if (!cxx_is(c, LEX_TOKEN_COLON_COLON)) {
					cxx_rewind(c, save, save_undo);
					break;
				}
				cxx_advance(c);
				if (!te || te->kind != CXX_ENTITY_TEMPLATE || !te->members || args_dependent || te->dependent)
					*dependent = true, *qualifier = NULL;
				else
					*qualifier = te->members;	/* X<args>:: looks in the primary and its specializations */
				c->qualifier_hint = te && te->members ? te->members : NULL;
				last = te;
			}
			continue;
		}
		if ((t->kw == CXX_KW_decltype || t->kw == CXX_KW___decltype) && cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_PAREN) {
			uint32_t save = c->pos, save_undo = c->nundo;

			cxx_advance(c);
			cxx_skip_balanced(c);
			if (!cxx_is(c, LEX_TOKEN_COLON_COLON)) {
				cxx_rewind(c, save, save_undo);
				break;
			}
			cxx_advance(c);
			*dependent = true, *qualifier = NULL;
			continue;
		}
		break;
	}
	return last;
}

/* ---- template arguments ------------------------------------------------------------------ */

static void cxx_template_argument_parse(cxx *c)
{
	/* a type-id if it parses as one and ends the argument, else an expression */
	if (cxx_try_type_id(c)) {
		cxx_type_id_parse(c);
		return;
	}
	c->in_template_args++;
	cxx_conditional_expression_parse(c);
	c->in_template_args--;
}

/* after the <: arguments, then > */
VPRIV void cxx_template_arguments_parse(cxx *c)
{
	uint32_t saved = c->in_template_args, implicit = c->implicit_typename;

	cxx_expect(c, LEX_TOKEN_LESS);
	c->in_template_args = 0;
	c->implicit_typename = 0;	/* (only the declaration's own decl-specifiers) */
	if (!cxx_accept_greater(c)) {
		for (;;) {
			cxx_template_argument_parse(c);
			cxx_accept(c, LEX_TOKEN_ELLIPSIS);
			if (!cxx_accept(c, LEX_TOKEN_COMMA))
				break;
		}
		if (!cxx_accept_greater(c))
			cxx_fail(c, "expected '>' after template arguments");
	}
	c->in_template_args = saved;
	c->implicit_typename = implicit;
}

/* ---- decl-specifiers --------------------------------------------------------------------- */

static void cxx_class_specifier_parse(cxx *c, bool is_template);
static void cxx_enum_specifier_parse(cxx *c);

enum { DS_TYPE = 1, DS_TYPEDEF = 2, DS_FRIEND = 4, DS_MODIFIER = 8, DS_CONSTEXPR = 16, DS_DEFINED_CLASS = 32 };

static bool cxx_kw_is_simple_type(int kw)
{
	switch (kw) {
	case CXX_KW_void: case CXX_KW_bool: case CXX_KW__Bool: case CXX_KW_char: case CXX_KW_char8_t: case CXX_KW_char16_t:
	case CXX_KW_char32_t: case CXX_KW_wchar_t: case CXX_KW_int: case CXX_KW_float: case CXX_KW_double: case CXX_KW_auto:
	case CXX_KW___int128: case CXX_KW___float128: case CXX_KW___bf16: case CXX_KW__Float16: case CXX_KW___fp16:
	case CXX_KW___auto_type:
		return true;
	}
	return false;
}

static bool cxx_kw_is_type_modifier(int kw)
{
	switch (kw) {
	case CXX_KW_short: case CXX_KW_long: case CXX_KW_signed: case CXX_KW_unsigned: case CXX_KW___signed: case CXX_KW___signed__:
	case CXX_KW__Complex: case CXX_KW___complex__:
		return true;
	}
	return false;
}

static bool cxx_kw_is_cv(int kw)
{
	switch (kw) {
	case CXX_KW_const: case CXX_KW_volatile: case CXX_KW___const: case CXX_KW___const__: case CXX_KW___volatile:
	case CXX_KW___volatile__: case CXX_KW___restrict: case CXX_KW___restrict__: case CXX_KW___unaligned:
		return true;
	}
	return false;
}

static bool cxx_kw_is_decl_specifier(int kw)
{
	switch (kw) {
	case CXX_KW_static: case CXX_KW_extern: case CXX_KW_mutable: case CXX_KW_register: case CXX_KW_thread_local:
	case CXX_KW__Thread_local: case CXX_KW___thread: case CXX_KW_inline: case CXX_KW___inline: case CXX_KW___inline__:
	case CXX_KW_virtual: case CXX_KW_explicit: case CXX_KW_friend: case CXX_KW_constexpr: case CXX_KW_consteval:
	case CXX_KW_constinit: case CXX_KW_typedef: case CXX_KW__Noreturn:
		return true;
	}
	return false;
}

/*
 * A name that is a type here: qualified, a template-id, typename ..., or a
 * builtin type transform.  Consumes it and returns true, else consumes
 * nothing.  ctor_class: in a class body, that class's name followed by ( is
 * a constructor, not a type.
 */
static bool cxx_type_name_try(cxx *c, cxx_scope *ctor_class)
{
	uint32_t save = c->pos, save_undo = c->nundo;
	cxx_scope *q;
	bool dep, typename_kw = false;
	cxx_token *t;
	cxx_entity *e;

	if (cxx_is_kw(c, CXX_KW_typename)) {
		typename_kw = true;
		cxx_advance(c);
	}
	cxx_nested_name_parse(c, &q, &dep);
	if (cxx_is_kw(c, CXX_KW_template)) {	/* typename X::template Y<...> */
		cxx_advance(c);
		typename_kw = true;
	}
	t = cxx_peek(c, 0);
	if (t->kind != LEX_TOKEN_IDENTIFIER || t->kw)
		goto no;
	if (!q && !dep) {
		int b = cxx_builtin_kind(t->id);

		if (b == CXX_BUILTIN_TYPE_TRANSFORM && cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_PAREN) {
			cxx_advance(c);
			cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
			cxx_type_id_parse(c);
			cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
			c->last_type = NULL, c->last_type_dependent = 1;
			return true;
		}
		if (b == CXX_BUILTIN_TEMPLATE && cxx_peek(c, 1)->kind == LEX_TOKEN_LESS) {
			cxx_advance(c);
			cxx_template_arguments_parse(c);
			c->last_type = NULL, c->last_type_dependent = 1;
			return true;
		}
	}
	/* X::X( names X's constructor, not a type (an out-of-class definition, an explicit instantiation) */
	if (!typename_kw && (q || c->qualifier_hint) && cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_PAREN) {
		cxx_scope *cls = q ? q : c->qualifier_hint;

		if (cls->owner && cls->owner->name == t->id)
			goto no;
	}
	e = cxx_name_lookup(c, q, dep, t->id);
	if (c->implicit_typename && dep && e && e->kind == CXX_ENTITY_DEPENDENT && cxx_peek(c, 1)->kind != LEX_TOKEN_LEFT_PAREN)
		typename_kw = true;	/* C++20: typename is implied here */
	if (typename_kw || (e && e->kind == CXX_ENTITY_DEPENDENT && typename_kw)) {
		cxx_advance(c);
		if (cxx_is(c, LEX_TOKEN_LESS))
			cxx_template_arguments_parse(c);
		c->last_type = NULL, c->last_type_dependent = 1;
		return true;
	}
	if (!e)
		goto no;
	{
		cxx_entity *te = cxx_entity_prefer(e, CXX_ENTITY_TEMPLATE), *ty = cxx_entity_prefer(e, CXX_ENTITY_TYPE);

		if (te && te->kind == CXX_ENTITY_TEMPLATE && cxx_peek(c, 1)->kind == LEX_TOKEN_LESS) {
			cxx_advance(c);
			cxx_template_arguments_parse(c);
			c->last_type = te, c->last_type_dependent = (uint8_t)(c->template_depth > 0);
			return true;
		}
		if (cxx_entity_is_type(ty)) {
			if (ctor_class && ctor_class->owner && ty->name == ctor_class->owner->name && !q &&
			    cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_PAREN)
				goto no;	/* a constructor */
			cxx_advance(c);
			c->last_type = ty, c->last_type_dependent = ty->dependent;
			return true;
		}
		/* the injected name of a class template used as a type inside it */
		if (te && te->kind == CXX_ENTITY_TEMPLATE) {
			if (ctor_class && ctor_class->owner && te->name == ctor_class->owner->name && !q &&
			    cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_PAREN)
				goto no;
			cxx_advance(c);
			c->last_type = te, c->last_type_dependent = 1;
			return true;
		}
	}
no:
	cxx_rewind(c, save, save_undo);
	return false;
}

/* decl-specifier-seq; returns DS_* flags (0: nothing was consumed) */
static int cxx_decl_specifiers_parse(cxx *c, cxx_scope *ctor_class, bool is_template)
{
	int flags = 0, any = 0;

	c->last_type = NULL, c->last_type_dependent = 0;
	for (;;) {
		int kw = cxx_kw(c);

		cxx_attributes_skip(c);
		kw = cxx_kw(c);
		if (kw == CXX_KW_typedef) {
			flags |= DS_TYPEDEF;
			cxx_advance(c);
		} else if (kw == CXX_KW_friend) {
			flags |= DS_FRIEND;
			cxx_advance(c);
		} else if (kw == CXX_KW_constexpr || kw == CXX_KW_consteval || kw == CXX_KW_constinit) {
			flags |= DS_CONSTEXPR;
			cxx_advance(c);
		} else if (kw == CXX_KW_explicit) {
			cxx_advance(c);
			if (cxx_is(c, LEX_TOKEN_LEFT_PAREN)) {
				cxx_advance(c);
				cxx_constant_expression_parse(c);
				cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
			}
		} else if (cxx_kw_is_decl_specifier(kw) || cxx_kw_is_cv(kw)) {
			cxx_advance(c);
		} else if (cxx_kw_is_type_modifier(kw)) {
			flags |= DS_MODIFIER;
			cxx_advance(c);
		} else if (!(flags & DS_TYPE) && cxx_kw_is_simple_type(kw)) {
			flags |= DS_TYPE;
			cxx_advance(c);
		} else if (!(flags & DS_TYPE) && (kw == CXX_KW_decltype || kw == CXX_KW___decltype || kw == CXX_KW___typeof__ ||
						  kw == CXX_KW___typeof || kw == CXX_KW_typeof) &&
			   !(cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_PAREN && cxx_peek(c, 2)->kind != LEX_TOKEN_RIGHT_PAREN &&
			     false)) {
			uint32_t save = c->pos, save_undo = c->nundo;

			cxx_advance(c);
			cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
			if (cxx_is_kw(c, CXX_KW_auto) && cxx_peek(c, 1)->kind == LEX_TOKEN_RIGHT_PAREN)
				cxx_advance(c);
			else if (kw != CXX_KW_decltype && kw != CXX_KW___decltype && cxx_try_type_id(c))
				cxx_type_id_parse(c);
			else
				cxx_expression_parse(c);
			cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
			if (cxx_is(c, LEX_TOKEN_COLON_COLON)) {	/* decltype(x)::type: a nested name */
				cxx_rewind(c, save, save_undo);
				if (!cxx_type_name_try(c, NULL))
					cxx_fail(c, "expected a type");
			}
			flags |= DS_TYPE;
			c->last_type = NULL, c->last_type_dependent = 1;
		} else if (!(flags & DS_TYPE) && kw == CXX_KW__Atomic && cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_PAREN) {
			cxx_advance(c);
			cxx_advance(c);
			cxx_type_id_parse(c);
			cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
			flags |= DS_TYPE;
		} else if (kw == CXX_KW__Atomic) {
			cxx_advance(c);
		} else if (!(flags & DS_TYPE) && kw == CXX_KW__BitInt && cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_PAREN) {
			cxx_advance(c);
			cxx_skip_balanced(c);
			flags |= DS_TYPE;
		} else if (!(flags & DS_TYPE) && (kw == CXX_KW_class || kw == CXX_KW_struct || kw == CXX_KW_union)) {
			cxx_class_specifier_parse(c, is_template);
			flags |= DS_TYPE;
		} else if (!(flags & DS_TYPE) && kw == CXX_KW_enum) {
			cxx_enum_specifier_parse(c);
			flags |= DS_TYPE;
		} else if (!(flags & (DS_TYPE | DS_MODIFIER)) &&
			   (kw == CXX_KW_typename || ((cxx_is(c, LEX_TOKEN_IDENTIFIER) && !kw) || cxx_is(c, LEX_TOKEN_COLON_COLON) ||
						      kw == CXX_KW_decltype)) &&
			   cxx_type_name_try(c, ctor_class)) {
			flags |= DS_TYPE;
			/* a constrained placeholder: Concept auto */
		} else if (!(flags & DS_TYPE) && cxx_is(c, LEX_TOKEN_IDENTIFIER) && !kw && cxx_peek(c, 1)->kind == LEX_TOKEN_LESS) {
			/* Concept<args> auto / Concept<args> decltype(auto) */
			cxx_entity *e = cxx_lookup(c, cxx_peek(c, 0)->id);
			cxx_entity *te = e ? cxx_entity_template(e) : NULL;

			if (!te || te->kind != CXX_ENTITY_CONCEPT)
				break;
			cxx_advance(c);
			cxx_template_arguments_parse(c);
			if (!cxx_is_kw(c, CXX_KW_auto) && !cxx_is_kw(c, CXX_KW_decltype))
				cxx_fail(c, "expected 'auto' after a type constraint");
		} else if (!(flags & DS_TYPE) && cxx_is(c, LEX_TOKEN_IDENTIFIER) && !kw &&
			   (cxx_peek(c, 1)->kw == CXX_KW_auto || cxx_peek(c, 1)->kw == CXX_KW_decltype)) {
			cxx_entity *e = cxx_lookup(c, cxx_peek(c, 0)->id);
			cxx_entity *te = e ? cxx_entity_template(e) : NULL;

			if (!te || te->kind != CXX_ENTITY_CONCEPT)
				break;
			cxx_advance(c);	/* Concept auto */
		} else
			break;
		any = 1;
	}
	return flags | (any ? 0 : 0);
}

/* ---- declarators ------------------------------------------------------------------------- */

static void cxx_parameter_clause_parse(cxx *c);
static void cxx_parameter_clause_body_parse(cxx *c);
static void cxx_function_tail_parse(cxx *c);

/* ptr-operator: * cv, &, &&, X::* cv (with attributes); false if none */
static bool cxx_ptr_operator_parse(cxx *c)
{
	if (cxx_is(c, LEX_TOKEN_STAR) || cxx_is(c, LEX_TOKEN_AMPERSAND) || cxx_is(c, LEX_TOKEN_AMPERSAND_AMPERSAND)) {
		cxx_advance(c);
		cxx_attributes_skip(c);
		while (cxx_kw_is_cv(cxx_kw(c)) || cxx_is_kw(c, CXX_KW__Nullable) || cxx_is_kw(c, CXX_KW__Nonnull) ||
		       cxx_is_kw(c, CXX_KW__Null_unspecified)) {
			cxx_advance(c);
			cxx_attributes_skip(c);
		}
		return true;
	}
	if (cxx_is(c, LEX_TOKEN_IDENTIFIER) || cxx_is(c, LEX_TOKEN_COLON_COLON)) {
		uint32_t save = c->pos, save_undo = c->nundo;
		cxx_scope *q;
		bool dep;

		cxx_nested_name_parse(c, &q, &dep);
		if (c->pos != save && cxx_is(c, LEX_TOKEN_STAR)) {	/* pointer to member */
			cxx_advance(c);
			while (cxx_kw_is_cv(cxx_kw(c)))
				cxx_advance(c);
			return true;
		}
		cxx_rewind(c, save, save_undo);
	}
	return false;
}

static void cxx_template_arguments_then_paren(cxx *c)
{
	cxx_template_arguments_parse(c);
	if (!cxx_is(c, LEX_TOKEN_LEFT_PAREN))
		cxx_fail(c, "not template arguments");
}

/* operator-function-id or conversion-function-id, after 'operator' */
static void cxx_operator_name_parse(cxx *c)
{
	int k = cxx_kind(c);

	if (cxx_is_kw(c, CXX_KW_new) || cxx_is_kw(c, CXX_KW_delete)) {
		cxx_advance(c);
		if (cxx_is(c, LEX_TOKEN_LEFT_SQUARE)) {
			cxx_advance(c);
			cxx_expect(c, LEX_TOKEN_RIGHT_SQUARE);
		}
		return;
	}
	if (cxx_is_kw(c, CXX_KW_co_await)) {
		cxx_advance(c);
		return;
	}
	if (k == LEX_TOKEN_LEFT_PAREN) {
		cxx_advance(c);
		cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
		return;
	}
	if (k == LEX_TOKEN_LEFT_SQUARE) {
		cxx_advance(c);
		cxx_expect(c, LEX_TOKEN_RIGHT_SQUARE);
		return;
	}
	if (k == LEX_TOKEN_STRING) {	/* operator "" _suffix */
		cxx_advance(c);
		if (cxx_is(c, LEX_TOKEN_IDENTIFIER))
			cxx_advance(c);
		return;
	}
	if (k >= LEX_TOKEN_LEFT_SQUARE && k < LEX_TOKEN_HASH && k != LEX_TOKEN_LEFT_BRACE && k != LEX_TOKEN_RIGHT_BRACE &&
	    k != LEX_TOKEN_RIGHT_PAREN && k != LEX_TOKEN_RIGHT_SQUARE && k != LEX_TOKEN_SEMICOLON && k != LEX_TOKEN_PERIOD &&
	    k != LEX_TOKEN_ELLIPSIS && k != LEX_TOKEN_QUESTION && k != LEX_TOKEN_COLON && k != LEX_TOKEN_COLON_COLON) {
		/* (operator> and >>: as written; a template's closing > never follows 'operator' here) */
		cxx_advance(c);
		return;
	}
	if (k == CXX_TOKEN_ARROW_STAR || k == CXX_TOKEN_SPACESHIP) {
		cxx_advance(c);
		return;
	}
	/* conversion: operator type-specifiers ptr-operators */
	if (!cxx_decl_specifiers_parse(c, NULL, false))
		cxx_fail(c, "expected an operator or a type after 'operator'");
	while (cxx_ptr_operator_parse(c))
		;
}

/* does a ( after decl-specifiers start a nested declarator (not parameters)? */
static bool cxx_paren_is_nested_declarator(cxx *c)
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
static uint32_t cxx_declarator_parse(cxx *c, bool abstract_ok, bool *qualified, bool *is_function, cxx_scope **qscope)
{
	uint32_t name = 0;

	*qualified = false;
	*is_function = false;
	if (qscope)
		*qscope = NULL;
	while (cxx_ptr_operator_parse(c))
		;
	cxx_attributes_skip(c);
	cxx_accept(c, LEX_TOKEN_ELLIPSIS);	/* a pack */
	if (!abstract_ok && cxx_is(c, LEX_TOKEN_LEFT_SQUARE) && cxx_peek(c, 1)->kind == LEX_TOKEN_IDENTIFIER) {
		/* a structured binding: auto [a, b, ...c] */
		cxx_advance(c);
		do {
			cxx_accept(c, LEX_TOKEN_ELLIPSIS);
			if (!cxx_is(c, LEX_TOKEN_IDENTIFIER))
				cxx_fail(c, "expected a name in a structured binding");
			cxx_declare(c, cxx_declaration_scope(c), cxx_peek(c, 0)->id, CXX_ENTITY_VALUE);
			cxx_advance(c);
			cxx_attributes_skip(c);
		} while (cxx_accept(c, LEX_TOKEN_COMMA));
		cxx_expect(c, LEX_TOKEN_RIGHT_SQUARE);
		cxx_attributes_skip(c);
		return 0;
	}
	if (cxx_is(c, LEX_TOKEN_LEFT_PAREN) && cxx_paren_is_nested_declarator(c)) {
		bool q2, f2;

		cxx_advance(c);
		name = cxx_declarator_parse(c, abstract_ok, &q2, &f2, qscope);
		cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
		*qualified = q2;
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
			cxx_operator_name_parse(c);
			name = 1;	/* (an operator: not entered in a scope) */
			if (cxx_is(c, LEX_TOKEN_LESS) && cxx_lookahead(c, cxx_template_arguments_then_paren))
				cxx_template_arguments_parse(c);	/* operator+<char, ...>( : a specialization */
		} else if (cxx_is(c, LEX_TOKEN_IDENTIFIER) && !cxx_kw(c)) {
			name = cxx_peek(c, 0)->id;
			cxx_advance(c);
			/* an explicit specialization's or a qualified template's arguments */
			if (cxx_is(c, LEX_TOKEN_LESS)) {
				cxx_entity *e = cxx_lookup(c, name);

				if (((*qualified || c->template_depth || c->explicit_specialization) && (*qualified || (e && cxx_entity_template(e)))) ||
				    (e && cxx_entity_template(e) && cxx_lookahead(c, cxx_template_arguments_then_paren)))
					cxx_template_arguments_parse(c);	/* a specialization, or an explicit instantiation's */
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
		if (cxx_is(c, LEX_TOKEN_LEFT_SQUARE) && cxx_peek(c, 1)->kind != LEX_TOKEN_LEFT_SQUARE) {
			cxx_advance(c);
			if (!cxx_is(c, LEX_TOKEN_RIGHT_SQUARE))
				cxx_expression_parse(c);
			cxx_expect(c, LEX_TOKEN_RIGHT_SQUARE);
			cxx_attributes_skip(c);
			continue;
		}
		if (cxx_is(c, LEX_TOKEN_LEFT_PAREN)) {
			/* parameters if they parse as such; else ( starts a direct-initializer: T x(a, b) */
			if (cxx_peek(c, 1)->kind != LEX_TOKEN_RIGHT_PAREN && !cxx_lookahead(c, cxx_parameter_clause_parse))
				break;
			cxx_parameter_clause_parse(c);
			cxx_function_tail_parse(c);
			*is_function = true;
			continue;
		}
		break;
	}
	c->scope = outer;
	return name;
}

/* ( parameter-declaration-clause ) */
static void cxx_parameter_clause_parse(cxx *c)
{
	uint32_t implicit = c->implicit_typename;

	c->implicit_typename = 0;
	cxx_parameter_clause_body_parse(c);
	c->implicit_typename = implicit;
}

static void cxx_parameter_clause_body_parse(cxx *c)
{
	cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
	if (cxx_accept(c, LEX_TOKEN_RIGHT_PAREN))
		return;
	for (;;) {
		bool q, f;

		if (cxx_accept(c, LEX_TOKEN_ELLIPSIS))
			break;
		cxx_attributes_skip(c);
		if (!(cxx_decl_specifiers_parse(c, NULL, false) & (DS_TYPE | DS_MODIFIER)))
			cxx_fail(c, "expected a parameter type");
		cxx_declarator_parse(c, true, &q, &f, NULL);
		if (cxx_accept(c, LEX_TOKEN_EQUAL))
			cxx_initializer_parse(c);
		if (cxx_accept(c, LEX_TOKEN_ELLIPSIS))	/* (int...) */
			break;
		if (!cxx_accept(c, LEX_TOKEN_COMMA))
			break;
	}
	cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
}

static void cxx_noexcept_parse(cxx *c)
{
	if (cxx_accept_kw(c, CXX_KW_noexcept)) {
		if (cxx_is(c, LEX_TOKEN_LEFT_PAREN)) {
			cxx_advance(c);
			cxx_constant_expression_parse(c);
			cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
		}
	} else if (cxx_accept_kw(c, CXX_KW_throw)) {
		cxx_skip_balanced(c);
	}
}

/* cv, ref-qualifier, noexcept, attributes, -> trailing return type */
static void cxx_function_tail_parse(cxx *c)
{
	for (;;) {
		if (cxx_kw_is_cv(cxx_kw(c)) || cxx_is(c, LEX_TOKEN_AMPERSAND) || cxx_is(c, LEX_TOKEN_AMPERSAND_AMPERSAND)) {
			cxx_advance(c);
			continue;
		}
		if (cxx_is_kw(c, CXX_KW_noexcept) || cxx_is_kw(c, CXX_KW_throw)) {
			cxx_noexcept_parse(c);
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
	if (cxx_accept(c, LEX_TOKEN_ARROW))
		cxx_type_id_parse(c);
}

/* the type of a functional cast T(x) / T{x}: type-specifiers only, no declarator */
VPRIV void cxx_type_specifiers_parse(cxx *c)
{
	if (!(cxx_decl_specifiers_parse(c, NULL, false) & (DS_TYPE | DS_MODIFIER)))
		cxx_fail(c, "expected a type");
}

/* new-type-id: type-specifiers, ptr-operators and [n]... (no parentheses: T(args) is the initializer) */
VPRIV void cxx_new_type_id_parse(cxx *c)
{
	cxx_type_specifiers_parse(c);
	while (cxx_ptr_operator_parse(c))
		;
	while (cxx_is(c, LEX_TOKEN_LEFT_SQUARE) && cxx_peek(c, 1)->kind != LEX_TOKEN_LEFT_SQUARE) {
		cxx_advance(c);
		if (!cxx_is(c, LEX_TOKEN_RIGHT_SQUARE))
			cxx_expression_parse(c);
		cxx_expect(c, LEX_TOKEN_RIGHT_SQUARE);
	}
}

/* type-id: type-specifiers and an abstract declarator */
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

		if (cxx_declarator_parse(c, true, &q, &f, NULL))
			cxx_fail(c, "a type-id declares no name");	/* (T(f)) is an expression, not a cast */
		c->last_type = nlt, c->last_type_dependent = nld;
	}
	(void)lt, (void)ld;
}

static void cxx_type_id_then_end(cxx *c)
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

VPRIV void cxx_braced_init_list_parse(cxx *c)
{
	cxx_expect(c, LEX_TOKEN_LEFT_BRACE);
	while (!cxx_is(c, LEX_TOKEN_RIGHT_BRACE)) {
		if (cxx_is(c, LEX_TOKEN_PERIOD) && cxx_peek(c, 1)->kind == LEX_TOKEN_IDENTIFIER) {	/* .designator = */
			cxx_advance(c);
			cxx_advance(c);
			if (!cxx_accept(c, LEX_TOKEN_EQUAL) && !cxx_is(c, LEX_TOKEN_LEFT_BRACE))
				cxx_fail(c, "expected '=' after a designator");
		}
		cxx_initializer_parse(c);
		cxx_accept(c, LEX_TOKEN_ELLIPSIS);
		if (!cxx_accept(c, LEX_TOKEN_COMMA))
			break;
	}
	cxx_expect(c, LEX_TOKEN_RIGHT_BRACE);
}

/* initializer-clause: assignment-expression or braced-init-list */
VPRIV void cxx_initializer_parse(cxx *c)
{
	if (cxx_is(c, LEX_TOKEN_LEFT_BRACE))
		cxx_braced_init_list_parse(c);
	else
		cxx_assignment_expression_parse(c);
}

static void cxx_expression_list_parse(cxx *c)
{
	cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
	while (!cxx_is(c, LEX_TOKEN_RIGHT_PAREN)) {
		cxx_initializer_parse(c);
		cxx_accept(c, LEX_TOKEN_ELLIPSIS);
		if (!cxx_accept(c, LEX_TOKEN_COMMA))
			break;
	}
	cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
}

/* ---- functions --------------------------------------------------------------------------- */

static void cxx_late_add(cxx *c, uint32_t begin, uint32_t end, int what)
{
	if (c->tentative)
		return;
	if (c->nlate == c->clate)
		c->late = realloc(c->late, (c->clate = c->clate ? c->clate * 2 : 64) * sizeof *c->late);
	c->late[c->nlate].begin = begin, c->late[c->nlate].end = end, c->late[c->nlate].scope = c->scope;
	c->late[c->nlate++].what = (uint8_t)what;
}

static void cxx_function_body_inner_parse(cxx *c);

/* : mem-initializers, then { body } (or a function-try-block) */
static void cxx_function_body_parse(cxx *c)
{
	uint32_t implicit = c->implicit_typename;

	c->implicit_typename = 0;
	cxx_function_body_inner_parse(c);
	c->implicit_typename = implicit;
}

static void cxx_function_body_inner_parse(cxx *c)
{
	bool is_try = cxx_accept_kw(c, CXX_KW_try);

	if (cxx_accept(c, LEX_TOKEN_COLON)) {
		do {
			cxx_scope *q;
			bool dep;

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
			cxx_accept(c, LEX_TOKEN_ELLIPSIS);
		} while (cxx_accept(c, LEX_TOKEN_COMMA));
	}
	cxx_compound_statement_parse(c);
	if (is_try)
		while (cxx_is_kw(c, CXX_KW_catch))
			cxx_statement_parse(c);
}

/* in a class: skip the body (with mem-initializers) and parse it when the class is complete */
static void cxx_function_body_defer(cxx *c)
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
		uint32_t save = c->pos, depth = c->class_depth;
		cxx_scope *ss = c->scope;
		cxx_scope *fs;

		c->pos = l.begin;
		c->scope = l.scope;
		c->class_depth = 0;
		fs = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL);
		cxx_scope_push(c, fs);
		cxx_function_body_parse(c);
		if (c->pos != l.end)
			cxx_fail(c, "member function body ended early");
		c->scope = ss;
		c->pos = save;
		c->class_depth = depth;
	}
	c->nlate = from;
}

/* ---- namespaces, using, static_assert ---------------------------------------------------- */

static void cxx_namespace_parse(cxx *c)
{
	bool is_inline = cxx_accept_kw(c, CXX_KW_inline);
	cxx_scope *outer = c->scope;

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
		is_inline = false;
	} while (cxx_accept(c, LEX_TOKEN_COLON_COLON));
	cxx_expect(c, LEX_TOKEN_LEFT_BRACE);
	while (!cxx_is(c, LEX_TOKEN_RIGHT_BRACE) && !cxx_is(c, LEX_TOKEN_EOF))
		cxx_declaration_parse(c, CXX_CONTEXT_NAMESPACE);
	cxx_expect(c, LEX_TOKEN_RIGHT_BRACE);
	c->scope = outer;
}

static void cxx_using_parse(cxx *c)
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
		{
			uint32_t implicit = c->implicit_typename;

			c->implicit_typename = 1;
			cxx_type_id_parse(c);
			c->implicit_typename = implicit;
		}
		{
			cxx_entity *target = c->last_type;
			uint8_t dep = c->last_type_dependent;

			cxx_expect(c, LEX_TOKEN_SEMICOLON);
			e = cxx_declare(c, cxx_declaration_scope(c), name, c->template_depth ? CXX_ENTITY_TEMPLATE : CXX_ENTITY_TYPE);
			if (e && c->template_depth && c->scope->kind == CXX_SCOPE_TEMPLATE)
				e->kind = CXX_ENTITY_TEMPLATE;
			else if (e) {
				e->kind = CXX_ENTITY_TYPE;
				e->dependent = dep;
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
						e->members = t->members, e->dependent = t->dependent;
						e->specs = t->specs, e->nspecs = t->nspecs;
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

static void cxx_static_assert_parse(cxx *c)
{
	cxx_advance(c);
	cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
	cxx_assignment_expression_parse(c);
	if (cxx_accept(c, LEX_TOKEN_COMMA))
		cxx_assignment_expression_parse(c);	/* a string literal (or, in C++26, an expression) */
	cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
	cxx_expect(c, LEX_TOKEN_SEMICOLON);
}

/* ---- templates --------------------------------------------------------------------------- */

static void cxx_template_parameter_parse(cxx *c)
{
	cxx_attributes_skip(c);
	if (cxx_is_kw(c, CXX_KW_template)) {	/* template<...> class/typename name = default */
		cxx_scope *ts;

		cxx_advance(c);
		ts = cxx_scope_new(c, CXX_SCOPE_TEMPLATE, c->scope, NULL);
		cxx_scope_push(c, ts);
		cxx_expect(c, LEX_TOKEN_LESS);
		if (!cxx_accept_greater(c)) {
			do
				cxx_template_parameter_parse(c);
			while (cxx_accept(c, LEX_TOKEN_COMMA));
			if (!cxx_accept_greater(c))
				cxx_fail(c, "expected '>'");
		}
		cxx_scope_pop(c);
		if (!cxx_accept_kw(c, CXX_KW_class) && !cxx_accept_kw(c, CXX_KW_typename))
			cxx_fail(c, "expected 'class' in a template template parameter");
		cxx_accept(c, LEX_TOKEN_ELLIPSIS);
		if (cxx_is(c, LEX_TOKEN_IDENTIFIER)) {
			cxx_entity *e = cxx_declare(c, c->scope, cxx_peek(c, 0)->id, CXX_ENTITY_TEMPLATE);

			if (e)
				e->dependent = 1;
			cxx_advance(c);
		}
		if (cxx_accept(c, LEX_TOKEN_EQUAL)) {
			cxx_scope *q;
			bool dep;

			cxx_nested_name_parse(c, &q, &dep);
			cxx_expect(c, LEX_TOKEN_IDENTIFIER);
		}
		return;
	}
	if ((cxx_is_kw(c, CXX_KW_class) || cxx_is_kw(c, CXX_KW_typename)) &&
	    (cxx_peek(c, 1)->kind == LEX_TOKEN_IDENTIFIER || cxx_peek(c, 1)->kind == LEX_TOKEN_ELLIPSIS ||
	     cxx_peek(c, 1)->kind == LEX_TOKEN_COMMA || cxx_peek(c, 1)->kind == LEX_TOKEN_GREATER ||
	     cxx_peek(c, 1)->kind == LEX_TOKEN_EQUAL || cxx_peek(c, 1)->kind == LEX_TOKEN_GREATER_GREATER) &&
	    !(cxx_peek(c, 1)->kind == LEX_TOKEN_IDENTIFIER && cxx_peek(c, 2)->kind == LEX_TOKEN_COLON_COLON)) {
		/* type parameter */
		cxx_advance(c);
		cxx_accept(c, LEX_TOKEN_ELLIPSIS);
		if (cxx_is(c, LEX_TOKEN_IDENTIFIER) && !cxx_kw(c)) {
			cxx_entity *e = cxx_declare(c, c->scope, cxx_peek(c, 0)->id, CXX_ENTITY_TYPE);

			if (e)
				e->dependent = 1;
			cxx_advance(c);
		}
		if (cxx_accept(c, LEX_TOKEN_EQUAL))
			cxx_type_id_parse(c);
		return;
	}
	/* a constrained type parameter: Concept<args> name, Concept name */
	if (cxx_is(c, LEX_TOKEN_IDENTIFIER) || cxx_is(c, LEX_TOKEN_COLON_COLON)) {
		uint32_t save = c->pos, save_undo = c->nundo;
		cxx_scope *q;
		bool dep;
		cxx_entity *e;

		cxx_nested_name_parse(c, &q, &dep);
		e = cxx_is(c, LEX_TOKEN_IDENTIFIER) ? cxx_name_lookup(c, q, dep, cxx_peek(c, 0)->id) : NULL;
		e = e ? cxx_entity_template(e) : NULL;
		if (e && e->kind == CXX_ENTITY_CONCEPT) {
			cxx_advance(c);
			if (cxx_is(c, LEX_TOKEN_LESS))
				cxx_template_arguments_parse(c);
			cxx_accept(c, LEX_TOKEN_ELLIPSIS);
			if (cxx_is(c, LEX_TOKEN_IDENTIFIER) && !cxx_kw(c)) {
				cxx_entity *p = cxx_declare(c, c->scope, cxx_peek(c, 0)->id, CXX_ENTITY_TYPE);

				if (p)
					p->dependent = 1;
				cxx_advance(c);
			}
			if (cxx_accept(c, LEX_TOKEN_EQUAL))
				cxx_type_id_parse(c);
			return;
		}
		cxx_rewind(c, save, save_undo);
	}
	/* non-type parameter: a parameter declaration */
	{
		bool q, f;
		uint32_t name;

		if (!(cxx_decl_specifiers_parse(c, NULL, false) & (DS_TYPE | DS_MODIFIER)))
			cxx_fail(c, "expected a template parameter");
		name = cxx_declarator_parse(c, true, &q, &f, NULL);
		if (name > 1) {
			cxx_entity *e = cxx_declare(c, c->scope, name, CXX_ENTITY_VALUE);

			if (e)
				e->dependent = 1;
		}
		if (cxx_accept(c, LEX_TOKEN_EQUAL)) {
			c->in_template_args++;
			cxx_conditional_expression_parse(c);
			c->in_template_args--;
		}
	}
}

VPRIV void cxx_requires_clause_parse(cxx *c)
{
	if (!cxx_accept_kw(c, CXX_KW_requires))
		return;
	cxx_conditional_expression_parse(c);	/* (a constraint-logical-or-expression; the parser stops at ; { , ) */
}

static void cxx_template_declaration_parse(cxx *c, int context)
{
	cxx_scope *ts;

	cxx_expect_kw(c, CXX_KW_template);
	if (!cxx_is(c, LEX_TOKEN_LESS)) {	/* explicit instantiation: template class X<int>; */
		cxx_declaration_parse(c, context);
		return;
	}
	if (cxx_peek(c, 1)->kind == LEX_TOKEN_GREATER) {
		/* template<>: an explicit specialization is not a template (nothing in it is dependent) */
		uint32_t depth = c->template_depth;

		cxx_advance(c);
		cxx_advance(c);
		c->template_depth = 0;
		c->explicit_specialization++;
		cxx_declaration_parse(c, context);
		c->explicit_specialization--;
		c->template_depth = depth;
		return;
	}
	ts = cxx_scope_new(c, CXX_SCOPE_TEMPLATE, c->scope, NULL);
	cxx_scope_push(c, ts);
	c->template_depth++;
	cxx_advance(c);
	uint32_t implicit = c->implicit_typename;

	c->implicit_typename = 0;
	if (!cxx_accept_greater(c)) {
		do
			cxx_template_parameter_parse(c);
		while (cxx_accept(c, LEX_TOKEN_COMMA));
		if (!cxx_accept_greater(c))
			cxx_fail(c, "expected '>' after template parameters");
	}
	c->implicit_typename = implicit;
	cxx_requires_clause_parse(c);
	if (cxx_is_kw(c, CXX_KW_concept)) {
		uint32_t name;

		cxx_advance(c);
		name = cxx_peek(c, 0)->id;
		cxx_expect(c, LEX_TOKEN_IDENTIFIER);
		cxx_attributes_skip(c);
		cxx_expect(c, LEX_TOKEN_EQUAL);
		cxx_declare(c, cxx_declaration_scope(c)->parent ? cxx_declaration_scope(c) : c->global, name, CXX_ENTITY_CONCEPT);
		cxx_conditional_expression_parse(c);
		cxx_expect(c, LEX_TOKEN_SEMICOLON);
	} else
		cxx_declaration_parse(c, context);
	c->template_depth--;
	cxx_scope_pop(c);
}

/* ---- classes and enums ------------------------------------------------------------------- */

static void cxx_class_body_parse(cxx *c, cxx_scope *cs);

static void cxx_base_clause_parse(cxx *c, cxx_scope *cs)
{
	do {
		cxx_scope *q;
		bool dep;
		cxx_entity *base = NULL;

		cxx_attributes_skip(c);
		while (cxx_is_kw(c, CXX_KW_virtual) || cxx_is_kw(c, CXX_KW_public) || cxx_is_kw(c, CXX_KW_protected) ||
		       cxx_is_kw(c, CXX_KW_private))
			cxx_advance(c);
		if (cxx_is_kw(c, CXX_KW_decltype)) {
			cxx_advance(c);
			cxx_skip_balanced(c);
			cs->has_dependent_base = 1;
		} else {
			cxx_accept_kw(c, CXX_KW_typename);
			cxx_nested_name_parse(c, &q, &dep);
			cxx_accept_kw(c, CXX_KW_template);
			if (!cxx_is(c, LEX_TOKEN_IDENTIFIER))
				cxx_fail(c, "expected a base class name");
			base = cxx_name_lookup(c, q, dep, cxx_peek(c, 0)->id);
			cxx_advance(c);
			if (cxx_is(c, LEX_TOKEN_LESS)) {
				cxx_template_arguments_parse(c);
				/* a template-id base: inside a template it may depend on its parameters; elsewhere its
				   members are those of the primary template (and its specializations) */
				cxx_entity *te = base ? cxx_entity_template(base) : NULL;

				if (c->template_depth || !te || !te->members)
					cs->has_dependent_base = 1;
				else {
					cxx_scope_use(c, cs, te->members);
					for (uint32_t i = 0; i < te->nspecs; i++)
						cxx_scope_use(c, cs, te->specs[i]);
				}
			} else {
				base = base ? cxx_entity_prefer(base, CXX_ENTITY_TYPE) : NULL;
				if (dep || !base || base->kind == CXX_ENTITY_DEPENDENT || base->dependent || !base->members)
					cs->has_dependent_base = 1;
				else
					cxx_scope_use(c, cs, base->members);
			}
		}
		cxx_accept(c, LEX_TOKEN_ELLIPSIS);
	} while (cxx_accept(c, LEX_TOKEN_COMMA));
}

static void cxx_class_specifier_parse(cxx *c, bool is_template)
{
	cxx_scope *q = NULL;
	bool dep = false, specialization = false;
	uint32_t name = 0;
	cxx_entity *e = NULL;

	cxx_advance(c);	/* class / struct / union */
	cxx_attributes_skip(c);
	if (cxx_is(c, LEX_TOKEN_IDENTIFIER) || cxx_is(c, LEX_TOKEN_COLON_COLON)) {
		cxx_nested_name_parse(c, &q, &dep);
		if (!cxx_is(c, LEX_TOKEN_IDENTIFIER))
			cxx_fail(c, "expected a class name");
		name = cxx_peek(c, 0)->id;
		cxx_advance(c);
		if (cxx_is(c, LEX_TOKEN_LESS)) {	/* a specialization */
			cxx_template_arguments_parse(c);
			specialization = true;
		}
	}
	cxx_attributes_skip(c);
	if (cxx_is(c, LEX_TOKEN_IDENTIFIER) && cxx_peek(c, 0)->len == 5 && !memcmp(cxx_peek(c, 0)->s, "final", 5))
		cxx_advance(c);
	if (!cxx_is(c, LEX_TOKEN_LEFT_BRACE) && !cxx_is(c, LEX_TOKEN_COLON)) {
		/* an elaborated type specifier: refers to (or declares) the class */
		if (name) {
			cxx_entity *found = cxx_name_lookup(c, q, dep, name);

			found = found ? cxx_entity_prefer(found, CXX_ENTITY_TYPE) : NULL;
			if (!found || (found->kind != CXX_ENTITY_TYPE && !cxx_entity_template(found))) {
				if (!q && !dep && (cxx_is(c, LEX_TOKEN_SEMICOLON) || !found)) {
					e = cxx_declare(c, cxx_declaration_scope(c)->kind == CXX_SCOPE_FUNCTION_PARAMETERS ? cxx_enclosing_namespace(c) : cxx_declaration_scope(c),
							name, is_template ? CXX_ENTITY_TEMPLATE : CXX_ENTITY_TYPE);
					/* a member scope from the first declaration: X<args>:: reaches the specializations
					   even if the primary template is never defined (libc++'s char_traits) */
					if (e && !e->members)
						e->members = cxx_scope_new(c, CXX_SCOPE_CLASS, c->scope, e);
					found = e;
				}
			}
			c->last_type = found, c->last_type_dependent = found ? found->dependent : 1;
		}
		return;
	}
	/* a definition */
	{
		cxx_scope *target = q ? q : cxx_declaration_scope(c), *cs;
		cxx_entity *found = name ? cxx_lookup_local(target, name) : NULL;
		cxx_scope *outer = c->scope;

		if (specialization) {
			/* a partial or explicit specialization: its own scope, recorded with the template */
			cxx_entity *te = found ? cxx_entity_template(found) : NULL;

			if (!te && name)
				te = cxx_entity_template(cxx_lookup(c, name) ? cxx_lookup(c, name) : (cxx_entity *)NULL);
			cs = cxx_scope_new(c, CXX_SCOPE_CLASS, c->scope, te);
			if (te && !c->tentative) {
				te->specs = realloc(te->specs, (te->nspecs + 1) * sizeof *te->specs);
				te->specs[te->nspecs++] = cs;
			}
			e = te;
		} else {
			found = found ? cxx_entity_prefer(found, is_template ? CXX_ENTITY_TEMPLATE : CXX_ENTITY_TYPE) : NULL;
			if (found && (found->kind == CXX_ENTITY_TYPE || found->kind == CXX_ENTITY_TEMPLATE))
				e = found;
			else if (name)
				e = cxx_declare(c, target, name, is_template ? CXX_ENTITY_TEMPLATE : CXX_ENTITY_TYPE);
			/* the scope made at a forward declaration: aliases and typedefs taken since point to it */
			if (e && e->members && e->members->kind == CXX_SCOPE_CLASS && !e->members->nnames && !c->tentative)
				cs = e->members;
			else {
				cs = cxx_scope_new(c, CXX_SCOPE_CLASS, c->scope, e);
				if (e && !c->tentative)
					e->members = cs;
			}
		}
		if (is_template || c->template_depth)
			cs->dependent = 1;
		if (cxx_accept(c, LEX_TOKEN_COLON))
			cxx_base_clause_parse(c, cs);
		/* the injected-class-name */
		if (name && !c->tentative) {
			cxx_entity *inj = cxx_declare(c, cs, name, is_template && !specialization ? CXX_ENTITY_TEMPLATE : CXX_ENTITY_TYPE);

			if (inj) {
				inj->members = cs;
				inj->dependent = cs->dependent;
				if (is_template && !specialization) {
					cxx_entity *ty = cxx_declare(c, cs, name, CXX_ENTITY_TYPE);

					if (ty)
						ty->members = cs, ty->dependent = 1;
				}
				if (specialization && e) {
					/* in a specialization the name followed by < still names the template */
					cxx_entity *tn = cxx_declare(c, cs, name, CXX_ENTITY_TEMPLATE);

					if (tn && tn != e)
						tn->members = e->members, tn->specs = e->specs, tn->nspecs = e->nspecs;
				}
			}
		}
		cs->parent = c->scope;
		c->scope = cs;
		cxx_class_body_parse(c, cs);
		c->scope = outer;
		c->last_type = e, c->last_type_dependent = cs->dependent;
	}
}

static void cxx_member_declaration_parse(cxx *c, cxx_scope *cs);

static void cxx_class_body_parse(cxx *c, cxx_scope *cs)
{
	uint32_t late_from = c->nlate, implicit = c->implicit_typename;

	cxx_expect(c, LEX_TOKEN_LEFT_BRACE);
	c->class_depth++;
	c->implicit_typename = 0;
	while (!cxx_is(c, LEX_TOKEN_RIGHT_BRACE) && !cxx_is(c, LEX_TOKEN_EOF))
		cxx_member_declaration_parse(c, cs);
	cxx_expect(c, LEX_TOKEN_RIGHT_BRACE);
	c->class_depth--;
	c->implicit_typename = implicit;
	if (!c->class_depth && !c->tentative)
		cxx_late_parse_all(c, late_from);	/* the outermost class is complete */
}

static void cxx_enum_specifier_parse(cxx *c)
{
	uint32_t name = 0;
	cxx_scope *q;
	bool dep;
	bool scoped = false;

	cxx_advance(c);	/* enum */
	if (cxx_accept_kw(c, CXX_KW_class) || cxx_accept_kw(c, CXX_KW_struct))
		scoped = true;
	cxx_attributes_skip(c);
	if (cxx_is(c, LEX_TOKEN_IDENTIFIER) || cxx_is(c, LEX_TOKEN_COLON_COLON)) {
		cxx_nested_name_parse(c, &q, &dep);
		name = cxx_peek(c, 0)->id;
		cxx_expect(c, LEX_TOKEN_IDENTIFIER);
	}
	cxx_attributes_skip(c);
	if (cxx_is(c, LEX_TOKEN_COLON) && (cxx_peek(c, 1)->kind != LEX_TOKEN_COLON)) {	/* underlying type */
		cxx_advance(c);
		cxx_type_id_parse(c);
	}
	{
		cxx_entity *e = name ? cxx_declare(c, cxx_declaration_scope(c), name, CXX_ENTITY_TYPE) : NULL;
		cxx_scope *es = NULL;

		if (e && !e->members) {
			es = cxx_scope_new(c, CXX_SCOPE_CLASS, c->scope, e);
			e->members = es;
		} else if (e)
			es = e->members;
		c->last_type = e, c->last_type_dependent = 0;
		if (!cxx_accept(c, LEX_TOKEN_LEFT_BRACE))
			return;
		while (!cxx_is(c, LEX_TOKEN_RIGHT_BRACE)) {
			uint32_t en = cxx_peek(c, 0)->id;

			cxx_expect(c, LEX_TOKEN_IDENTIFIER);
			cxx_attributes_skip(c);
			if (es)
				cxx_declare(c, es, en, CXX_ENTITY_VALUE);
			if (!scoped)
				cxx_declare(c, cxx_declaration_scope(c), en, CXX_ENTITY_VALUE);
			if (cxx_accept(c, LEX_TOKEN_EQUAL))
				cxx_constant_expression_parse(c);
			if (!cxx_accept(c, LEX_TOKEN_COMMA))
				break;
		}
		cxx_expect(c, LEX_TOKEN_RIGHT_BRACE);
		c->last_type = e;
	}
}

/* ---- simple declarations ----------------------------------------------------------------- */

static bool cxx_is_function_body_start(cxx *c)
{
	return cxx_is(c, LEX_TOKEN_LEFT_BRACE) || cxx_is(c, LEX_TOKEN_COLON) || cxx_is_kw(c, CXX_KW_try);
}

/* the declarators after decl-specifiers, to the ; (or a function body) */
static void cxx_init_declarators_parse(cxx *c, int flags, int context, cxx_scope *cs)
{
	cxx_entity *type_of_typedef = c->last_type;
	uint8_t typedef_dependent = c->last_type_dependent;

	for (;;) {
		bool q, f;
		cxx_scope *qs;
		uint32_t name;
		uint32_t start = c->pos;

		if (context == CXX_CONTEXT_CLASS && cxx_is(c, LEX_TOKEN_COLON)) {	/* an unnamed bit-field */
			cxx_advance(c);
			cxx_constant_expression_parse(c);
			goto next;
		}
		name = cxx_declarator_parse(c, false, &q, &f, &qs);
		(void)start;
		/* what it declares (not a constructor or destructor: they have no name to look up) */
		if (name > 1 && !q && !(cs && cs->owner && cs->owner->name == name)) {
			int kind = (flags & DS_TYPEDEF) ? CXX_ENTITY_TYPE :
				   c->template_depth && c->scope->kind == CXX_SCOPE_TEMPLATE ? (f ? CXX_ENTITY_FUNCTION_TEMPLATE : CXX_ENTITY_VARIABLE_TEMPLATE)
											   : CXX_ENTITY_VALUE;
			cxx_entity *e = (flags & DS_FRIEND) ? NULL : cxx_declare(c, cxx_declaration_scope(c), name, kind);

			if ((flags & DS_FRIEND) && f && kind == CXX_ENTITY_FUNCTION_TEMPLATE)
				e = NULL;	/* (a friend template: found by ADL only) */
			if (e && (flags & DS_TYPEDEF)) {
				e->dependent = typedef_dependent;
				if (type_of_typedef && type_of_typedef->members && !typedef_dependent)
					e->members = type_of_typedef->members, e->specs = type_of_typedef->specs, e->nspecs = type_of_typedef->nspecs;
			}
		}
		if (context == CXX_CONTEXT_CLASS) {
			/* pure, = default, = delete, bit-field width, override/final */
			while (cxx_is(c, LEX_TOKEN_IDENTIFIER) && !cxx_kw(c) &&
			       ((cxx_peek(c, 0)->len == 8 && !memcmp(cxx_peek(c, 0)->s, "override", 8)) ||
				(cxx_peek(c, 0)->len == 5 && !memcmp(cxx_peek(c, 0)->s, "final", 5))))
				cxx_advance(c);
			if (f)
				cxx_requires_clause_parse(c);
			else if (cxx_accept(c, LEX_TOKEN_COLON))	/* a bit-field (after a function: mem-initializers) */
				cxx_constant_expression_parse(c);
		} else if (f)
			cxx_requires_clause_parse(c);
		cxx_attributes_skip(c);
		if (cxx_is_kw(c, CXX_KW_asm) || cxx_is_kw(c, CXX_KW___asm__) || cxx_is_kw(c, CXX_KW___asm)) {	/* asm label */
			cxx_advance(c);
			cxx_skip_balanced(c);
			cxx_attributes_skip(c);
		}
		if (f && cxx_is_function_body_start(c) && !(context == CXX_CONTEXT_BLOCK)) {
			if (context == CXX_CONTEXT_CLASS || c->class_depth)
				cxx_function_body_defer(c);
			else {
				cxx_scope *fs = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL);
				cxx_scope *outer = c->scope;

				/* an out-of-class member definition: its class's members are in scope (for X<args>::f the
				   parser does not know which specialization: all of them) */
				if (qs) {
					cxx_scope_use(c, fs, qs);
					if (qs->owner)
						for (uint32_t i = 0; i < qs->owner->nspecs; i++)
							cxx_scope_use(c, fs, qs->owner->specs[i]);
				}
				cxx_scope_push(c, fs);
				cxx_function_body_parse(c);
				c->scope = outer;
			}
			return;
		}
		if (cxx_accept(c, LEX_TOKEN_EQUAL)) {
			if (f && (cxx_is_kw(c, CXX_KW_default) || cxx_is_kw(c, CXX_KW_delete) ||
				  (cxx_is(c, LEX_TOKEN_NUMBER) && cxx_peek(c, 0)->len == 1))) {
				cxx_advance(c);
				if (cxx_is(c, LEX_TOKEN_LEFT_PAREN))	/* = delete("reason") */
					cxx_skip_balanced(c);
			} else
				cxx_initializer_parse(c);
		} else if (cxx_is(c, LEX_TOKEN_LEFT_BRACE)) {
			cxx_braced_init_list_parse(c);
		} else if (!f && cxx_is(c, LEX_TOKEN_LEFT_PAREN)) {
			cxx_expression_list_parse(c);	/* direct-initialization */
		}
	next:
		if (!cxx_accept(c, LEX_TOKEN_COMMA))
			break;
	}
	cxx_expect(c, LEX_TOKEN_SEMICOLON);
	(void)cs;
}

/* a deduction guide: [explicit] template-name ( parameters ) -> template-id ; */
static bool cxx_deduction_guide_try(cxx *c)
{
	uint32_t i = c->pos, depth = 0;
	cxx_entity *e;

	if (c->tok[i].kw == CXX_KW_explicit) {
		i++;
		if (c->tok[i].kind == LEX_TOKEN_LEFT_PAREN)
			return false;	/* (explicit(bool) guides: not in json's headers) */
	}
	if (c->tok[i].kind != LEX_TOKEN_IDENTIFIER || c->tok[i].kw || c->tok[i + 1].kind != LEX_TOKEN_LEFT_PAREN)
		return false;
	e = cxx_lookup(c, c->tok[i].id);
	e = e ? cxx_entity_prefer(e, CXX_ENTITY_TEMPLATE) : NULL;
	if (!e || e->kind != CXX_ENTITY_TEMPLATE)
		return false;
	for (uint32_t k = i + 1; k < c->ntok; k++) {
		int kind = c->tok[k].kind;

		if (kind == LEX_TOKEN_LEFT_PAREN)
			depth++;
		else if (kind == LEX_TOKEN_RIGHT_PAREN && !--depth) {
			if (c->tok[k + 1].kind != LEX_TOKEN_ARROW)
				return false;
			break;
		}
	}
	cxx_accept_kw(c, CXX_KW_explicit);
	cxx_advance(c);
	{
		bool q, f;

		/* the parameters (a declarator with only a parameter suffix), -> and the type */
		cxx_parameter_clause_parse(c);
		(void)q, (void)f;
	}
	cxx_expect(c, LEX_TOKEN_ARROW);
	cxx_type_id_parse(c);
	cxx_expect(c, LEX_TOKEN_SEMICOLON);
	return true;
}

static void cxx_simple_declaration_parse(cxx *c, int context, cxx_scope *cs)
{
	bool is_template = c->template_depth && c->scope->kind == CXX_SCOPE_TEMPLATE;
	int flags;

	if (c->template_depth && cxx_deduction_guide_try(c))
		return;
	if (context == CXX_CONTEXT_CLASS && !cs) {	/* a member template: the class around its template scope */
		cs = cxx_declaration_scope(c);
		cs = cs && cs->kind == CXX_SCOPE_CLASS ? cs : NULL;
	}
	{
		uint32_t implicit = c->implicit_typename;

		if (context != CXX_CONTEXT_BLOCK)
			c->implicit_typename = 1;	/* member and namespace-scope declarations (P0634) */
		flags = cxx_decl_specifiers_parse(c, context == CXX_CONTEXT_CLASS ? cs : NULL, is_template);
		c->implicit_typename = implicit;
	}

	if (cxx_accept(c, LEX_TOKEN_SEMICOLON))	/* struct S {...}; enum E {...}; friend class X; */
		return;
	if (!flags && !(cxx_is(c, LEX_TOKEN_TILDE) || cxx_is(c, LEX_TOKEN_IDENTIFIER) || cxx_is(c, LEX_TOKEN_COLON_COLON) ||
			cxx_is_kw(c, CXX_KW_operator)))
		cxx_fail(c, "expected a declaration");
	cxx_init_declarators_parse(c, flags, context, cs);
}

static void cxx_member_declaration_parse(cxx *c, cxx_scope *cs)
{
	int kw;

	cxx_attributes_skip(c);
	kw = cxx_kw(c);
	if ((kw == CXX_KW_public || kw == CXX_KW_private || kw == CXX_KW_protected) && cxx_peek(c, 1)->kind == LEX_TOKEN_COLON) {
		cxx_advance(c);
		cxx_advance(c);
		return;
	}
	if (cxx_accept(c, LEX_TOKEN_SEMICOLON))
		return;
	if (kw == CXX_KW_using) {
		cxx_using_parse(c);
		return;
	}
	if (kw == CXX_KW_static_assert || kw == CXX_KW__Static_assert) {
		cxx_static_assert_parse(c);
		return;
	}
	if (kw == CXX_KW_template) {
		cxx_template_declaration_parse(c, CXX_CONTEXT_CLASS);
		return;
	}
	if (kw == CXX_KW_friend && cxx_peek(c, 1)->kw == CXX_KW_template) {	/* friend template<...> ... (rare) */
		cxx_advance(c);
		cxx_template_declaration_parse(c, CXX_CONTEXT_CLASS);
		return;
	}
	cxx_simple_declaration_parse(c, CXX_CONTEXT_CLASS, cs);
}

VPRIV bool cxx_starts_declaration(cxx *c)
{
	int kw = cxx_kw(c);

	if (kw)
		return cxx_kw_is_decl_specifier(kw) || cxx_kw_is_cv(kw) || cxx_kw_is_simple_type(kw) || cxx_kw_is_type_modifier(kw) ||
		       kw == CXX_KW_class || kw == CXX_KW_struct || kw == CXX_KW_union || kw == CXX_KW_enum ||
		       kw == CXX_KW_typename || kw == CXX_KW_using || kw == CXX_KW_static_assert || kw == CXX_KW_namespace ||
		       kw == CXX_KW_template || kw == CXX_KW___attribute__ || kw == CXX_KW__Atomic || kw == CXX_KW___extension__ ||
		       kw == CXX_KW__BitInt;
	return false;
}

VPRIV void cxx_declaration_parse(cxx *c, int context)
{
	int kw;

	cxx_attributes_skip(c);
	kw = cxx_kw(c);
	switch (kw) {
	case CXX_KW_namespace:
		cxx_namespace_parse(c);
		return;
	case CXX_KW_inline:
		if (cxx_peek(c, 1)->kw == CXX_KW_namespace) {
			cxx_namespace_parse(c);
			return;
		}
		break;
	case CXX_KW_using:
		cxx_using_parse(c);
		return;
	case CXX_KW_template:
		cxx_template_declaration_parse(c, context);
		return;
	case CXX_KW_static_assert: case CXX_KW__Static_assert:
		cxx_static_assert_parse(c);
		return;
	case CXX_KW_extern:
		if (cxx_peek(c, 1)->kind == LEX_TOKEN_STRING) {	/* extern "C" */
			cxx_advance(c);
			cxx_advance(c);
			if (cxx_accept(c, LEX_TOKEN_LEFT_BRACE)) {
				while (!cxx_is(c, LEX_TOKEN_RIGHT_BRACE) && !cxx_is(c, LEX_TOKEN_EOF))
					cxx_declaration_parse(c, context);
				cxx_expect(c, LEX_TOKEN_RIGHT_BRACE);
			} else
				cxx_declaration_parse(c, context);
			return;
		}
		if (cxx_peek(c, 1)->kw == CXX_KW_template) {	/* extern template class X<int>; */
			cxx_advance(c);
			cxx_template_declaration_parse(c, context);
			return;
		}
		break;
	case CXX_KW_asm: case CXX_KW___asm__: case CXX_KW___asm:
		cxx_advance(c);
		cxx_skip_balanced(c);
		cxx_expect(c, LEX_TOKEN_SEMICOLON);
		return;
	case CXX_KW_export:
		cxx_advance(c);
		cxx_declaration_parse(c, context);
		return;
	}
	if (cxx_accept(c, LEX_TOKEN_SEMICOLON))
		return;
	cxx_simple_declaration_parse(c, context, NULL);
}

VPRIV void cxx_translation_unit_parse(cxx *c)
{
	while (!cxx_is(c, LEX_TOKEN_EOF))
		cxx_declaration_parse(c, CXX_CONTEXT_NAMESPACE);
}
