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

/* an attribute's tokens [b, e): one that changes a layout? (counted: a class with one is not laid out yet) */
static void cxx_attribute_scan(cxx *c, uint32_t b, uint32_t e)
{
	static const char *const names[] = { "no_unique_address", "__no_unique_address__", "aligned", "__aligned__", "packed",
					     "__packed__", "ms_struct", "__ms_struct__" };

	for (uint32_t i = b; i < e; i++) {
		const cxx_token *t = &c->tok[i];

		if (t->kind != LEX_TOKEN_IDENTIFIER)
			continue;
		for (size_t k = 0; k < sizeof names / sizeof *names; k++)
			if (t->len == strlen(names[k]) && !memcmp(t->s, names[k], t->len)) {
				c->layout_attributes++;
				return;
			}
	}
}

VPRIV void cxx_attributes_skip(cxx *c)
{
	for (;;) {
		if (cxx_is(c, LEX_TOKEN_LEFT_SQUARE) && cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_SQUARE) {
			uint32_t b = c->pos;

			cxx_skip_balanced(c);
			cxx_attribute_scan(c, b, c->pos);
			continue;
		}
		if (cxx_is_kw(c, CXX_KW___attribute__) || cxx_is_kw(c, CXX_KW___attribute) || cxx_is_kw(c, CXX_KW___declspec) ||
		    cxx_is_kw(c, CXX_KW_alignas) || cxx_is_kw(c, CXX_KW__Alignas)) {
			uint32_t b = c->pos;
			bool align = cxx_is_kw(c, CXX_KW_alignas) || cxx_is_kw(c, CXX_KW__Alignas);

			cxx_advance(c);
			if (!cxx_is(c, LEX_TOKEN_LEFT_PAREN))
				cxx_fail(c, "expected '(' after attribute");
			cxx_skip_balanced(c);
			if (align)
				c->layout_attributes++;
			else
				cxx_attribute_scan(c, b, c->pos);
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

/* clang's type transform builtins (__remove_reference_t(T), ...): CXX_TY_UNKNOWN for those not done */
static qty cxx_type_transform(cxx *c, const char *n, size_t len, qty t)
{
	const ctype *x = AST_TYPE(c->ast, t);
	bool ref = x->kind == TY_PTR && x->ext;
	qty base = ref ? x->base : t;
#define IS(s) (len == sizeof s - 1 && !memcmp(n, s, len))

	if (IS("__remove_reference_t"))
		return base;
	if (IS("__remove_cvref"))
		return QUNQ(base);
	if (IS("__remove_cv"))
		return ref ? t : QUNQ(t);
	if (IS("__remove_const"))
		return ref ? t : QT(QI(t), QQ(t) & ~Q_CONST);
	if (IS("__remove_volatile"))
		return ref ? t : QT(QI(t), QQ(t) & ~Q_VOLATILE);
	if (IS("__add_lvalue_reference"))
		return x->kind == TY_VOID ? t : cxx_type_reference(c, base, false);
	if (IS("__add_rvalue_reference"))
		return ref || x->kind == TY_VOID ? t : cxx_type_reference(c, t, true);
	if (IS("__add_pointer"))
		return x->kind == TY_FUNC ? cc_type_pointer(c->ast, t) : cc_type_pointer(c->ast, base);
	if (IS("__remove_pointer"))
		return x->kind == TY_PTR && !x->ext ? x->base : t;
	if (IS("__decay")) {
		const ctype *b = AST_TYPE(c->ast, base);

		if (b->kind == TY_ARRAY)
			return cc_type_pointer(c->ast, b->base);
		if (b->kind == TY_FUNC)
			return cc_type_pointer(c->ast, base);
		return QUNQ(base);
	}
	if (IS("__remove_extent"))
		return x->kind == TY_ARRAY ? x->base : t;
	if (IS("__remove_all_extents")) {
		while (AST_TYPE_KIND(c->ast, t) == TY_ARRAY)
			t = AST_TYPE(c->ast, t)->base;
		return t;
	}
#undef IS
	return CXX_TY_UNKNOWN;	/* (__make_signed, __make_unsigned, __underlying_type, __remove_restrict) */
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
			if (e && e->kind == CXX_ENTITY_TYPE && !e->dependent && e->ty != CXX_TY_UNKNOWN) {
				cxx_scope *cls = cxx_type_class_scope(c, e->ty);	/* (an instance: completed now, in a tentative parse too) */

				if (cls && !e->members)
					e->members = cls;
				/* int::x, T::x with T bound to a type that is not a class: ill-formed (a substitution fails) */
				if (!cls && !e->members && !c->template_depth && !cc_type_is_record(c->ast, e->ty))
					c->member_missing = 1;
			}
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
				cxx_entity *inst = te && te->kind == CXX_ENTITY_TEMPLATE && !args_dependent && !c->template_depth && !*dependent ?
						   cxx_instantiate(c, te, c->targs, c->ntargs, true) : NULL;

				/* an alias template's instance: the class it names (completed), its members */
				if (inst && !inst->members && inst->ty != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, inst->ty))
					inst->members = cxx_type_class_scope(c, QUNQ(inst->ty));
				if (inst && inst->members)
					*qualifier = inst->members, te = inst;	/* the instance's own members */
				else if (!te || te->kind != CXX_ENTITY_TEMPLATE || !te->members || args_dependent || te->dependent)
					*dependent = true, *qualifier = NULL;
				else if (!c->template_depth && !c->tentative && !c->explicit_specialization)
					/* known arguments, and no instance (a specialization vcc could not decide, an argument it
					   could not form): not the primary's members, whose constants are not this instance's */
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

static int64_t cxx_literal_value(cxx *c);

/* does the token `ahead` end a template argument: a comma, or a > that is not the first piece of >= */
static bool cxx_ends_argument(cxx *c, uint32_t ahead)
{
	const cxx_token *t = cxx_peek(c, ahead), *n = cxx_peek(c, ahead + 1);

	if (t->kind == LEX_TOKEN_COMMA)
		return true;
	return t->kind == LEX_TOKEN_GREATER && !(n->kind == LEX_TOKEN_EQUAL && n->s == t->s + 1);
}
static void cxx_builtin_template(cxx *c, uint32_t id, const cxx_targ *a, uint32_t n);

/* one template argument; *out: what it is, as far as known (a type, an integer literal) */
static void cxx_template_argument_parse(cxx *c, cxx_targ *out)
{
	cxx_token *t = cxx_peek(c, 0), *n = cxx_peek(c, 1);

	out->kind = 0, out->t = 0, out->v = 0;	/* (all of it hashed by the instance cache) */
	/* a template name alone (possibly qualified): a template template argument */
	if ((t->kind == LEX_TOKEN_IDENTIFIER && !t->kw) || t->kind == LEX_TOKEN_COLON_COLON) {
		uint32_t save = c->pos;
		cxx_scope *q;
		bool dep;
		cxx_token *id, *after;

		cxx_nested_name_parse(c, &q, &dep);
		id = cxx_peek(c, 0), after = cxx_peek(c, 1);
		if (id->kind == LEX_TOKEN_IDENTIFIER && !id->kw && !dep &&
		    (cxx_ends_argument(c, 1) || after->kind == LEX_TOKEN_ELLIPSIS)) {
			cxx_entity *e = q ? cxx_lookup_in(c, q, id->id, 0) : cxx_lookup(c, id->id);

			e = e ? cxx_entity_prefer(e, CXX_ENTITY_TEMPLATE) : NULL;
			if (e && e->kind == CXX_ENTITY_TEMPLATE && !e->is_pack) {
				cxx_advance(c);
				if (e->tmpl && !e->dependent)
					out->kind = 4, out->v = (int64_t)(intptr_t)e;
				else if (e->placeholder)
					out->kind = 7, out->v = e->placeholder - 1;	/* a pattern's template template parameter */
				return;
			}
		}
		c->pos = save;	/* (no tokens were edited: they are immutable) */
	}
	/* sizeof...(pack): its length */
	if (t->kw == CXX_KW_sizeof && n->kind == LEX_TOKEN_ELLIPSIS && cxx_peek(c, 2)->kind == LEX_TOKEN_LEFT_PAREN &&
	    cxx_peek(c, 3)->kind == LEX_TOKEN_IDENTIFIER && cxx_peek(c, 4)->kind == LEX_TOKEN_RIGHT_PAREN &&
	    cxx_ends_argument(c, 5)) {
		cxx_entity *e = cxx_lookup(c, cxx_peek(c, 3)->id);

		for (int k = 0; k < 5; k++)
			cxx_advance(c);
		for (; e; e = e->next)
			if (e->is_pack && !e->pack_placeholder) {
				out->kind = 2, out->v = e->npack;
				break;
			}
		c->npacks_seen = 0;	/* (sizeof... is not an expansion) */
		return;
	}
	/* a type-id if it parses as one and ends the argument, else an expression */
	uint8_t dseen = c->dependent_seen;

	if (!cxx_try_type_id(c))
		c->dependent_seen = dseen;	/* (what the failed attempt looked up does not count) */
	else {
		uint8_t saved = c->in_template_argument;

		c->in_template_argument = 1;
		cxx_type_id_parse(c);
		c->in_template_argument = saved;
		if (c->type_out != CXX_TY_UNKNOWN)
			out->kind = 1, out->t = c->type_out;
		return;
	}
	if (cxx_ends_argument(c, 1)) {
		int64_t v = cxx_literal_value(c);
		cxx_entity *e = t->kind == LEX_TOKEN_IDENTIFIER && !t->kw ? cxx_lookup(c, t->id) : NULL;

		if (e && e->kind == CXX_ENTITY_VALUE && e->placeholder)
			out->kind = 3, out->v = e->placeholder - 1;	/* a partial specialization's value parameter */
		else if (v >= 0) {
			out->kind = 2, out->v = v;
			/* an enumerator, a constant of enumeration type: X<e> is not X<its value> */
			for (cxx_entity *x = e; x; x = x->next)
				if (x->kind == CXX_ENTITY_VALUE && x->has_value && x->ty != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, x->ty) == TY_ENUM) {
					out->t = QUNQ(x->ty);
					break;
				}
		}
	}
	(void)t;
	c->in_template_args++;
	cxx_conditional_expression_parse(c);
	c->in_template_args--;
	/* a constant expression: its value */
	/* (negative: its value in the parameter's type is not known here, size_t(-1) for one) */
	if (!out->kind && c->econst && !c->tentative && !c->template_depth && c->evalue >= 0) {
		out->kind = 2, out->v = c->evalue;
		if (c->etype != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, c->etype) == TY_ENUM)
			out->t = QUNQ(c->etype);	/* (its enumeration: part of the argument) */
	}
}

/* after the <: arguments, then > */
VPRIV void cxx_template_arguments_parse(cxx *c)
{
	uint32_t saved = c->in_template_args, implicit = c->implicit_typename;

	cxx_targ args[32];
	uint32_t n = 0;

	cxx_expect(c, LEX_TOKEN_LESS);
	c->in_template_args = 0;
	c->implicit_typename = 0;	/* (only the declaration's own decl-specifiers) */
	if (!cxx_accept_greater(c)) {
		for (;;) {
			cxx_targ a;
			uint32_t start = c->pos, nseen = c->npacks_seen;
			cxx_entity *seen[8];

			uint8_t dseen = c->dependent_seen;	/* (before the pattern's first, unexpanded parse) */

			memcpy(seen, c->packs_seen, sizeof seen);
			c->npacks_seen = 0, c->packs_overflow = 0;
			cxx_template_argument_parse(c, &a);
			if (cxx_is(c, LEX_TOKEN_ELLIPSIS)) {
				/* a pack expansion: once per element (an instance's packs), a pattern's trailing
				   pack (Rest...: kind 5), or unknown */
				uint32_t end = c->pos;

				cxx_advance(c);
				if (c->npacks_seen == 1 && c->packs_seen[0]->pack_placeholder) {
					if (end == start + 1)
						a.kind = 5, a.v = c->packs_seen[0]->pack_placeholder - 1, a.t = 0;
					else
						a.kind = 0;	/* (a pattern Ts*... : not matched yet) */
				} else if (c->npacks_seen && !c->packs_overflow && c->instantiating && !c->tentative) {
					cxx_entity *packs[8];
					uint32_t np = c->npacks_seen, len = c->packs_seen[0]->npack, after = c->pos;
					bool ok = true;

					memcpy(packs, c->packs_seen, sizeof packs);
					for (uint32_t k = 0; k < np; k++)
						ok &= packs[k]->npack == len && !packs[k]->pack_placeholder;
					/* the unexpanded parse only found the packs: what it could not resolve (Ps::value with Ps
					   the whole pack) says nothing about the elements */
					c->dependent_seen = dseen;
					for (uint32_t i = 0; ok && i < len; i++) {
						cxx_targ e;

						for (uint32_t k = 0; k < np; k++)
							cxx_pack_element(c, packs[k], (int32_t)i);
						c->pos = start;
						cxx_template_argument_parse(c, &e);
						ok = c->pos == end;
						if (n < 32)
							args[n++] = e;
						else
							args[31].kind = 0;
					}
					for (uint32_t k = 0; k < np; k++)
						cxx_pack_element(c, packs[k], -1);
					c->pos = after;
					if (!ok && n)
						args[n - 1].kind = 0;
					c->npacks_seen = nseen;
					memcpy(c->packs_seen, seen, sizeof seen);
					if (!cxx_accept(c, LEX_TOKEN_COMMA))
						break;
					continue;
				} else
					a.kind = 0;	/* (outside an instance: not known) */
				c->npacks_seen = nseen;	/* (the expansion used its packs) */
				memcpy(c->packs_seen, seen, sizeof seen);
			} else {
				/* not expanded here: its packs belong to an enclosing expansion (leaf<Is, Ts>...) */
				cxx_entity *mine[8];
				uint32_t nmine = c->npacks_seen;

				memcpy(mine, c->packs_seen, sizeof mine);
				c->npacks_seen = nseen;
				memcpy(c->packs_seen, seen, sizeof seen);
				for (uint32_t k = 0; k < nmine; k++) {
					uint32_t j = 0;

					while (j < c->npacks_seen && c->packs_seen[j] != mine[k])
						j++;
					if (j == c->npacks_seen) {
						if (j < 8)
							c->packs_seen[c->npacks_seen++] = mine[k];
						else
							c->packs_overflow = 1;
					}
				}
			}
			if (n < 32)
				args[n++] = a;
			else
				args[31].kind = 0;
			if (!cxx_accept(c, LEX_TOKEN_COMMA))
				break;
		}
		if (!cxx_accept_greater(c))
			cxx_fail(c, "expected '>' after template arguments");
	}
	c->in_template_args = saved;
	c->implicit_typename = implicit;
	/* the arguments, for the caller to read at once (a nested list overwrites them) */
	if (n > c->ctargs)
		c->targs = realloc(c->targs, (c->ctargs = n + 8) * sizeof *c->targs);
	if (n)
		memcpy(c->targs, args, n * sizeof *args);
	c->ntargs = n;
}

/* ---- decl-specifiers --------------------------------------------------------------------- */

static void cxx_class_specifier_parse(cxx *c, bool is_template);
static void cxx_enum_specifier_parse(cxx *c);

enum { DS_TYPE = 1, DS_TYPEDEF = 2, DS_FRIEND = 4, DS_MODIFIER = 8, DS_CONSTEXPR = 16, DS_DEFINED_CLASS = 32, DS_STATIC = 64,
       DS_VIRTUAL = 128 };

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
			const vident *v = &vintern_ents[t->id];
			qty r;

			cxx_advance(c);
			cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
			cxx_type_id_parse(c);
			cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
			c->last_type = NULL, c->last_type_dependent = 1;
			/* the transform, when its operand is known (not in a template, not a placeholder pattern) */
			r = c->template_depth || c->tentative || c->type_out == CXX_TY_UNKNOWN || cxx_type_has_placeholder(c, c->type_out) ?
			    CXX_TY_UNKNOWN : cxx_type_transform(c, v->s, v->len, c->type_out);
			if (r != CXX_TY_UNKNOWN)
				c->last_type = cxx_type_entity(c, r), c->last_type_dependent = 0;
			return true;
		}
		if (b == CXX_BUILTIN_TEMPLATE && cxx_peek(c, 1)->kind == LEX_TOKEN_LESS) {
			uint32_t id = t->id;

			cxx_advance(c);
			cxx_template_arguments_parse(c);
			c->last_type = NULL, c->last_type_dependent = 1;
			if (!c->template_depth && !c->tentative)
				cxx_builtin_template(c, id, c->targs, c->ntargs);
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
	/* typename names a dependent type; a name that lookup resolves (an instance's) is that type */
	/* typename X::y with X a complete class that has no y: an error (a deduction's substitution failure) */
	if (typename_kw && !e && q && !dep && !c->template_depth && q->kind == CXX_SCOPE_CLASS && !q->dependent && !q->has_dependent_base)
		c->member_missing = 1;
	if (typename_kw && (dep || !e || e->kind == CXX_ENTITY_DEPENDENT || (!cxx_entity_is_type(cxx_entity_prefer(e, CXX_ENTITY_TYPE)) &&
				 cxx_entity_prefer(e, CXX_ENTITY_TEMPLATE)->kind != CXX_ENTITY_TEMPLATE))) {
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
			uint8_t seen = c->dependent_seen;

			c->dependent_seen = 0;
			cxx_advance(c);
			cxx_template_arguments_parse(c);
			c->last_type = te, c->last_type_dependent = (uint8_t)(c->template_depth > 0);
			if (!c->template_depth && !c->dependent_seen && !dep) {
				/* X<args> outside templates: an instance (instantiate.c), or unknown */
				cxx_entity *inst = cxx_instantiate(c, te, c->targs, c->ntargs, false);	/* (named: not completed) */

				if (inst)
					c->last_type = inst, c->last_type_dependent = 0;
			}
			c->dependent_seen |= seen;
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

/*
 * clang's builtin templates, as far as libc++ uses them:
 *   __make_integer_seq<Seq, T, N>     Seq<T, 0, 1, ..., N-1>
 *   __type_pack_element<I, Ts...>     the I-th of Ts
 * The result goes to c->last_type (unknown otherwise).
 */
static void cxx_builtin_template(cxx *c, uint32_t id, const cxx_targ *a, uint32_t n)
{
	const vident *v = &vintern_ents[id];

	if (v->len == 18 && !memcmp(v->s, "__make_integer_seq", 18) && n == 3 && a[0].kind == 4 && a[1].kind == 1 && a[2].kind == 2 &&
	    a[2].v >= 0 && a[2].v < 31) {
		cxx_targ args[32];
		cxx_entity *inst;

		args[0] = a[1];
		for (int64_t i = 0; i < a[2].v; i++)
			args[i + 1] = (cxx_targ){ .kind = 2, .v = i };
		inst = cxx_instantiate(c, (cxx_entity *)(intptr_t)a[0].v, args, (uint32_t)a[2].v + 1, false);
		if (inst)
			c->last_type = inst, c->last_type_dependent = 0;
	} else if (v->len == 19 && !memcmp(v->s, "__type_pack_element", 19) && n >= 2 && a[0].kind == 2 && a[0].v >= 0 &&
		   a[0].v + 1 < n && a[a[0].v + 1].kind == 1) {
		cxx_entity *r = calloc(1, sizeof *r);	/* (a type with no name: an entity of its own) */

		r->kind = CXX_ENTITY_TYPE, r->ty = a[a[0].v + 1].t, r->pack_index = -1;
		r->members = cxx_type_class_scope(c, r->ty);
		c->last_type = r, c->last_type_dependent = 0;
	}
}

/* decl-specifier-seq; returns DS_* flags (0: nothing was consumed) */
static void cxx_spec_keyword(cxx_spec *sp, int kw)
{
	switch (kw) {
	case CXX_KW_void: sp->v_void = 1; break;
	case CXX_KW_bool: case CXX_KW__Bool: sp->v_bool = 1; break;
	case CXX_KW_char: sp->v_char = 1; break;
	case CXX_KW_char8_t: sp->v_char8 = 1; break;
	case CXX_KW_char16_t: sp->v_char16 = 1; break;
	case CXX_KW_char32_t: sp->v_char32 = 1; break;
	case CXX_KW_wchar_t: sp->v_wchar = 1; break;
	case CXX_KW_int: sp->v_int = 1; break;
	case CXX_KW_float: sp->v_float = 1; break;
	case CXX_KW_double: sp->v_double = 1; break;
	case CXX_KW___int128: sp->v_int128 = 1; break;
	case CXX_KW___float128: sp->v_float128 = 1; break;
	case CXX_KW__Float16: case CXX_KW___fp16: sp->v_float16 = 1; break;
	case CXX_KW___bf16: sp->v_bf16 = 1; break;
	case CXX_KW_short: sp->v_short = 1; break;
	case CXX_KW_long: sp->v_long++; break;
	case CXX_KW_signed: case CXX_KW___signed: case CXX_KW___signed__: sp->v_signed = 1; break;
	case CXX_KW_unsigned: sp->v_unsigned = 1; break;
	case CXX_KW_const: case CXX_KW___const: case CXX_KW___const__: sp->quals |= Q_CONST; break;
	case CXX_KW_volatile: case CXX_KW___volatile: case CXX_KW___volatile__: sp->quals |= Q_VOLATILE; break;
	case CXX_KW___restrict: case CXX_KW___restrict__: sp->quals |= Q_RESTRICT; break;
	case CXX_KW_auto: case CXX_KW___auto_type: sp->v_auto = 1, sp->v_unknown = 1; break;	/* (deduced by the declaration) */
	default: sp->v_unknown = 1;	/* _Complex: not built yet */
	}
}

static int cxx_decl_specifiers_parse(cxx *c, cxx_scope *ctor_class, bool is_template)
{
	int flags = 0, any = 0;
	cxx_spec sp;

	memset(&sp, 0, sizeof sp);
	c->last_type = NULL, c->last_type_dependent = 0;
	c->defined_unnamed_class = 0;
	for (;;) {
		int kw = cxx_kw(c);

		cxx_attributes_skip(c);
		kw = cxx_kw(c);
		if (kw == CXX_KW_static) {
			flags |= DS_STATIC;
			cxx_advance(c);
		} else if (kw == CXX_KW_virtual) {
			flags |= DS_VIRTUAL;
			cxx_advance(c);
		} else if (kw == CXX_KW_typedef) {
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
			if (cxx_kw_is_cv(kw))
				cxx_spec_keyword(&sp, kw);
			cxx_advance(c);
		} else if (cxx_kw_is_type_modifier(kw)) {
			flags |= DS_MODIFIER;
			cxx_spec_keyword(&sp, kw);
			cxx_advance(c);
		} else if (!(flags & DS_TYPE) && cxx_kw_is_simple_type(kw)) {
			flags |= DS_TYPE;
			cxx_spec_keyword(&sp, kw);
			cxx_advance(c);
		} else if (!(flags & DS_TYPE) && (kw == CXX_KW_decltype || kw == CXX_KW___decltype || kw == CXX_KW___typeof__ ||
						  kw == CXX_KW___typeof || kw == CXX_KW_typeof) &&
			   !(cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_PAREN && cxx_peek(c, 2)->kind != LEX_TOKEN_RIGHT_PAREN &&
			     false)) {
			uint32_t save = c->pos, save_undo = c->nundo;
			qty known = CXX_TY_UNKNOWN;

			cxx_advance(c);
			cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
			if (cxx_is_kw(c, CXX_KW_auto) && cxx_peek(c, 1)->kind == LEX_TOKEN_RIGHT_PAREN)
				cxx_advance(c);
			else if (kw != CXX_KW_decltype && kw != CXX_KW___decltype && cxx_try_type_id(c))
				cxx_type_id_parse(c);
			else {
				uint32_t targs = c->in_template_args;

				c->in_template_args = 0;	/* (inside the parentheses a comma is the operator: decltype(a, b)) */
				c->unevaluated++;
				cxx_expression_parse(c);
				c->unevaluated--;
				c->in_template_args = targs;
				/* decltype: a name's declared type; a call's or cast's type with its reference; T& for
				   another lvalue; else the prvalue's type */
				if (!c->tentative && c->etype != CXX_TY_UNKNOWN && !c->template_depth && (kw == CXX_KW_decltype || kw == CXX_KW___decltype)) {
					bool ref = c->edecl != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, c->edecl) == TY_PTR && AST_TYPE(c->ast, c->edecl)->ext;

					if (ref && AST_TYPE(c->ast, c->edecl)->base == c->etype)
						known = c->edecl;	/* (as declared: T& or T&&) */
					else if (c->edecl_name || !c->elval)
						known = c->etype;
					else
						known = cxx_type_reference(c, c->etype, false);
				} else if (!c->tentative && c->etype != CXX_TY_UNKNOWN && !c->elval)
					known = c->etype;
			}
			cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
			if (cxx_is(c, LEX_TOKEN_COLON_COLON)) {	/* decltype(x)::type: a nested name */
				cxx_rewind(c, save, save_undo);
				known = CXX_TY_UNKNOWN;
				if (!cxx_type_name_try(c, NULL))
					cxx_fail(c, "expected a type");
			}
			flags |= DS_TYPE;
			if (known != CXX_TY_UNKNOWN)
				sp.v_named = 1, sp.named = known;
			else
				sp.v_unknown = 1;	/* decltype/typeof: not evaluated */
			c->last_type = NULL, c->last_type_dependent = known == CXX_TY_UNKNOWN;
		} else if (!(flags & DS_TYPE) && kw == CXX_KW__Atomic && cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_PAREN) {
			cxx_advance(c);
			cxx_advance(c);
			cxx_type_id_parse(c);
			cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
			flags |= DS_TYPE;
			sp.v_unknown = 1;
		} else if (kw == CXX_KW__Atomic) {
			sp.v_unknown = 1;
			cxx_advance(c);
		} else if (!(flags & DS_TYPE) && kw == CXX_KW__BitInt && cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_PAREN) {
			cxx_advance(c);
			cxx_skip_balanced(c);
			flags |= DS_TYPE;
			sp.v_unknown = 1;
		} else if (!(flags & DS_TYPE) && (kw == CXX_KW_class || kw == CXX_KW_struct || kw == CXX_KW_union)) {
			bool unnamed = cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_BRACE;

			c->anonymous_class = CXX_TY_UNKNOWN;
			cxx_class_specifier_parse(c, is_template);
			c->defined_unnamed_class = unnamed;	/* (after: the body's declarations reset it) */
			flags |= DS_TYPE;
			sp.v_named = 1;
			sp.named = unnamed ? c->anonymous_class : c->last_type && !c->last_type_dependent ? c->last_type->ty : CXX_TY_UNKNOWN;
		} else if (!(flags & DS_TYPE) && kw == CXX_KW_enum) {
			cxx_enum_specifier_parse(c);
			flags |= DS_TYPE;
			sp.v_named = 1;
			sp.named = c->last_type ? c->last_type->ty : CXX_TY_UNKNOWN;
		} else if (!(flags & (DS_TYPE | DS_MODIFIER)) &&
			   (kw == CXX_KW_typename || ((cxx_is(c, LEX_TOKEN_IDENTIFIER) && !kw) || cxx_is(c, LEX_TOKEN_COLON_COLON) ||
						      kw == CXX_KW_decltype)) &&
			   cxx_type_name_try(c, ctor_class)) {
			flags |= DS_TYPE;
			sp.v_named = 1;
			sp.named = c->last_type && !c->last_type_dependent && c->last_type->kind == CXX_ENTITY_TYPE ? c->last_type->ty
															    : CXX_TY_UNKNOWN;
			/* a constrained placeholder: Concept auto */
		} else if (!(flags & DS_TYPE) && cxx_is(c, LEX_TOKEN_IDENTIFIER) && !kw && cxx_peek(c, 1)->kind == LEX_TOKEN_LESS) {
			/* Concept<args> auto / Concept<args> decltype(auto) */
			cxx_entity *e = cxx_lookup(c, cxx_peek(c, 0)->id);
			cxx_entity *te = e ? cxx_entity_template(e) : NULL;

			if (!te || te->kind != CXX_ENTITY_CONCEPT)
				break;
			cxx_advance(c);
			cxx_template_arguments_parse(c);
			sp.v_unknown = 1;
			if (!cxx_is_kw(c, CXX_KW_auto) && !cxx_is_kw(c, CXX_KW_decltype))
				cxx_fail(c, "expected 'auto' after a type constraint");
		} else if (!(flags & DS_TYPE) && cxx_is(c, LEX_TOKEN_IDENTIFIER) && !kw &&
			   (cxx_peek(c, 1)->kw == CXX_KW_auto || cxx_peek(c, 1)->kw == CXX_KW_decltype)) {
			cxx_entity *e = cxx_lookup(c, cxx_peek(c, 0)->id);
			cxx_entity *te = e ? cxx_entity_template(e) : NULL;

			if (!te || te->kind != CXX_ENTITY_CONCEPT)
				break;
			sp.v_unknown = 1;
			cxx_advance(c);	/* Concept auto */
		} else
			break;
		any = 1;
	}
	(void)any;
	c->ds_type = flags & (DS_TYPE | DS_MODIFIER) ? cxx_spec_type(c, &sp) : CXX_TY_UNKNOWN;
	c->ds_auto = sp.v_auto, c->ds_quals = sp.quals;
	return flags;
}

/* ---- declarators ------------------------------------------------------------------------- */

static void cxx_parameter_clause_parse(cxx *c);
static void cxx_parameter_clause_body_parse(cxx *c);
static void cxx_function_tail_parse(cxx *c);
static void cxx_params_declare(cxx *c, cxx_scope *fs, const cxx_param *ps, uint32_t n);

/* ptr-operator: * cv, &, &&, X::* cv (with attributes); false if none.  t (may be NULL): the type it applies to */
static bool cxx_ptr_operator_parse(cxx *c, qty *t)
{
	if (cxx_is(c, LEX_TOKEN_STAR) || cxx_is(c, LEX_TOKEN_AMPERSAND) || cxx_is(c, LEX_TOKEN_AMPERSAND_AMPERSAND)) {
		int k = cxx_kind(c), quals = 0;

		cxx_advance(c);
		cxx_attributes_skip(c);
		while (cxx_kw_is_cv(cxx_kw(c)) || cxx_is_kw(c, CXX_KW__Nullable) || cxx_is_kw(c, CXX_KW__Nonnull) ||
		       cxx_is_kw(c, CXX_KW__Null_unspecified)) {
			int kw = cxx_kw(c);

			quals |= kw == CXX_KW_const || kw == CXX_KW___const || kw == CXX_KW___const__ ? Q_CONST :
				 kw == CXX_KW_volatile || kw == CXX_KW___volatile || kw == CXX_KW___volatile__ ? Q_VOLATILE :
				 kw == CXX_KW___restrict || kw == CXX_KW___restrict__ ? Q_RESTRICT : 0;
			cxx_advance(c);
			cxx_attributes_skip(c);
		}
		if (t)
			*t = k == LEX_TOKEN_STAR ? cxx_type_pointer(c, *t, quals) : cxx_type_reference(c, *t, k == LEX_TOKEN_AMPERSAND_AMPERSAND);
		return true;
	}
	if (cxx_is(c, LEX_TOKEN_IDENTIFIER) || cxx_is(c, LEX_TOKEN_COLON_COLON)) {
		uint32_t save = c->pos, save_undo = c->nundo;
		cxx_scope *q;
		bool dep;

		cxx_nested_name_parse(c, &q, &dep);
		if (c->pos != save && cxx_is(c, LEX_TOKEN_STAR)) {	/* pointer to member (not built yet) */
			cxx_advance(c);
			while (cxx_kw_is_cv(cxx_kw(c)))
				cxx_advance(c);
			if (t)
				*t = CXX_TY_UNKNOWN;
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
/* the interned name "operator" + the n tokens at pos ("operator+=", "operator>>"): how operator functions are declared */
VPRIV uint32_t cxx_operator_id_at(cxx *c, uint32_t n)
{
	char buf[64] = "operator";
	size_t len = 8;

	for (uint32_t i = 0; i < n; i++) {
		const cxx_token *t = cxx_peek(c, i);

		if (len + t->len >= 40)
			return 1;
		memcpy(buf + len, t->s, t->len);
		len += t->len;
	}
	return vintern_copy(buf, len);
}

/* operator-function-id or conversion-function-id, after 'operator': its name (1: not entered in a scope) */
static uint32_t cxx_operator_name_parse(cxx *c)
{
	int k = cxx_kind(c);

	if (cxx_is_kw(c, CXX_KW_new) || cxx_is_kw(c, CXX_KW_delete)) {
		cxx_advance(c);
		if (cxx_is(c, LEX_TOKEN_LEFT_SQUARE)) {
			cxx_advance(c);
			cxx_expect(c, LEX_TOKEN_RIGHT_SQUARE);
		}
		return 1;
	}
	if (cxx_is_kw(c, CXX_KW_co_await)) {
		cxx_advance(c);
		return 1;
	}
	if (k == LEX_TOKEN_LEFT_PAREN) {
		cxx_advance(c);
		cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
		return vintern_cstr("operator()");
	}
	if (k == LEX_TOKEN_LEFT_SQUARE) {
		cxx_advance(c);
		cxx_expect(c, LEX_TOKEN_RIGHT_SQUARE);
		return vintern_cstr("operator[]");
	}
	if (k == LEX_TOKEN_STRING) {	/* operator "" _suffix */
		cxx_advance(c);
		if (cxx_is(c, LEX_TOKEN_IDENTIFIER))
			cxx_advance(c);
		return 1;
	}
	if (k >= LEX_TOKEN_LEFT_SQUARE && k < LEX_TOKEN_HASH && k != LEX_TOKEN_LEFT_BRACE && k != LEX_TOKEN_RIGHT_BRACE &&
	    k != LEX_TOKEN_RIGHT_PAREN && k != LEX_TOKEN_RIGHT_SQUARE && k != LEX_TOKEN_SEMICOLON && k != LEX_TOKEN_PERIOD &&
	    k != LEX_TOKEN_ELLIPSIS && k != LEX_TOKEN_QUESTION && k != LEX_TOKEN_COLON && k != LEX_TOKEN_COLON_COLON) {
		/* operator>>, operator>=, operator>>=: joined pieces */
		uint32_t n, id;

		cxx_operator_at(c, &n);
		id = cxx_operator_id_at(c, n);
		while (n--)
			cxx_advance(c);
		return id;
	}
	if (k == CXX_TOKEN_ARROW_STAR || k == CXX_TOKEN_SPACESHIP) {
		cxx_advance(c);
		return 1;
	}
	/* conversion: operator type-specifiers ptr-operators */
	if (!cxx_decl_specifiers_parse(c, NULL, false))
		cxx_fail(c, "expected an operator or a type after 'operator'");
	while (cxx_ptr_operator_parse(c, NULL))
		;
	return 1;
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
/* an integer literal's value (array bounds, bit-field widths; other constant expressions: -1, not evaluated yet) */
/*
 * The value of the constant expression here when it is one token: an integer literal, true or
 * false, or a name bound to a known integer (a value template argument, a const or constexpr
 * variable initialized with one).  -1 when it is not that (not evaluated yet).
 */
static int64_t cxx_literal_value(cxx *c)
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

/* a suffix of a declarator: [n] or (parameters), applied innermost last */
typedef struct cxx_suffix { qty trailing; uint8_t is_const; uint8_t is_function, variadic; int64_t n; qty *params; uint32_t nparams; cxx_param *named; uint32_t nnamed, ndefaults; } cxx_suffix;

/*
 * A declarator (abstract if allowed); returns the declared name's id (0: none) and sets
 * *qualified if the name had a nested-name-specifier and *is_function if the outermost
 * suffix is a parameter list.  Its type: c->type_in is the base (the decl-specifiers'),
 * c->type_out the declared type.  C's inside-out rule: ptr-operators apply to the base,
 * then the suffixes from the last to the first, then a parenthesized declarator to that.
 */
static uint32_t cxx_declarator_parse(cxx *c, bool abstract_ok, bool *qualified, bool *is_function, cxx_scope **qscope)
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
					      cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_SQUARE || !c->in_template_argument))
		cxx_advance(c);
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
		c->type_out = CXX_TY_UNKNOWN;
		return 0;
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
			name = cxx_operator_name_parse(c);	/* (1: a conversion, new/delete: not entered in a scope) */
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
				x->nnamed = c->fn_nparams, x->ndefaults = c->fn_ndefaults;
				x->named = c->fn_nparams ? malloc(c->fn_nparams * sizeof *x->named) : NULL;
				for (uint32_t k = 0; k < c->fn_nparams; k++)
					x->named[k].name = c->fn_pnames[k], x->named[k].ty = c->fn_pdecl[k];
				nsfx++;
			} else
				too_many = true;
			cxx_function_tail_parse(c);
			if (x)
				x->trailing = c->fn_trailing, x->is_const = c->fn_const;
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
	} else if (!nested && !c->tentative)
		c->decl_ndefaults = 0, c->decl_const = 0;
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
static void cxx_parameter_clause_parse(cxx *c)
{
	uint32_t implicit = c->implicit_typename;

	c->implicit_typename = 0;
	cxx_parameter_clause_body_parse(c);
	c->implicit_typename = implicit;
}

static void cxx_parameter_clause_body_parse(cxx *c)
{
	qty *types = NULL, *decls = NULL;
	uint32_t *names = NULL;
	uint32_t n = 0, cap = 0, ndefaults = 0;
	uint8_t variadic = 0, unknown = 0;

	cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
	if (!cxx_accept(c, LEX_TOKEN_RIGHT_PAREN)) {
		for (;;) {
			bool q, f;
			qty t;

			if (cxx_accept(c, LEX_TOKEN_ELLIPSIS)) {
				variadic = 1;
				break;
			}
			cxx_attributes_skip(c);
			if (!(cxx_decl_specifiers_parse(c, NULL, false) & (DS_TYPE | DS_MODIFIER)))
				cxx_fail(c, "expected a parameter type");
			uint32_t pname;

			c->type_in = c->ds_type;
			pname = cxx_declarator_parse(c, true, &q, &f, NULL);
			t = cxx_type_parameter_adjust(c, c->type_out);
			/* in the body: the adjusted type with the declaration's own const (not part of the function's type) */
			qty dt = t == CXX_TY_UNKNOWN || c->type_out == CXX_TY_UNKNOWN || AST_TYPE_KIND(c->ast, c->type_out) == TY_ARRAY ||
				 AST_TYPE_KIND(c->ast, c->type_out) == TY_FUNC ? t : QT(QI(t), QQ(t) | (QQ(c->type_out) & (Q_CONST | Q_VOLATILE)));

			if (cxx_accept(c, LEX_TOKEN_EQUAL)) {
				uint32_t inner = ndefaults;

				cxx_initializer_parse(c);	/* (a lambda's parameters inside: not ours) */
				ndefaults = inner + 1;
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
	free(types);
	free(names);
	free(decls);
}

static void cxx_noexcept_parse(cxx *c)
{
	if (cxx_accept_kw(c, CXX_KW_noexcept)) {
		if (cxx_is(c, LEX_TOKEN_LEFT_PAREN)) {
			cxx_advance(c);
			c->unevaluated++;	/* (noexcept(noexcept(f(x))): what it calls is not used) */
			cxx_constant_expression_parse(c);
			c->unevaluated--;
			cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
		}
	} else if (cxx_accept_kw(c, CXX_KW_throw)) {
		cxx_skip_balanced(c);
	}
}

/* cv, ref-qualifier, noexcept, attributes, -> trailing return type */
static void cxx_function_tail_parse(cxx *c)
{
	bool is_const = false;

	for (;;) {
		if (cxx_kw_is_cv(cxx_kw(c)) || cxx_is(c, LEX_TOKEN_AMPERSAND) || cxx_is(c, LEX_TOKEN_AMPERSAND_AMPERSAND)) {
			is_const |= cxx_is_kw(c, CXX_KW_const);
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
	if (cxx_accept(c, LEX_TOKEN_ARROW)) {
		/* the parameters are in scope in a trailing return type: -> decltype(a < b) */
		cxx_scope *outer = c->scope;

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
		cxx_type_id_parse(c);
		c->scope = outer;
		c->fn_trailing = c->type_out;	/* auto f() -> T: the return type */
		c->fn_const = is_const;
		return;
	}
	c->fn_trailing = CXX_TY_UNKNOWN, c->fn_const = is_const;
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
	while (cxx_ptr_operator_parse(c, NULL))
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

		c->type_in = c->ds_type;
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
	c->late[c->nlate].nparams = c->ndecl_params;
	c->late[c->nlate].template_depth = c->template_depth;
	c->late[c->nlate].fn = c->last_fn, c->last_fn = NULL;
	c->late[c->nlate].params = c->ndecl_params ? malloc(c->ndecl_params * sizeof(cxx_param)) : NULL;
	if (c->ndecl_params)
		memcpy(c->late[c->nlate].params, c->decl_params, c->ndecl_params * sizeof(cxx_param));
	c->late[c->nlate++].what = (uint8_t)what;
}

static void cxx_function_body_inner_parse(cxx *c);

/* a function's parameters, declared (with their types) in its body's scope */
static void cxx_params_declare(cxx *c, cxx_scope *fs, const cxx_param *ps, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
		if (ps[i].name > 1) {
			cxx_entity *e = cxx_declare(c, fs, ps[i].name, CXX_ENTITY_VALUE);

			if (e)
				e->ty = ps[i].ty;
		}
}

/* : mem-initializers, then { body } (or a function-try-block) */
static void cxx_function_body_parse(cxx *c)
{
	uint32_t implicit = c->implicit_typename;
	uint8_t valid = c->name_path_valid;

	c->implicit_typename = 0;
	c->name_path_valid = 0;	/* local classes: not nameable outside */
	cxx_function_body_inner_parse(c);
	c->implicit_typename = implicit;
	c->name_path_valid = valid;
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
		cxx_scope_push(c, fs);
		cxx_params_declare(c, fs, l.params, l.nparams);
		free(l.params);
		cxx_function_body_parse(c);
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

static void cxx_namespace_parse(cxx *c)
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
		check = getenv("VCC_CXX_STATIC_ASSERT") != NULL;
	if (check && !c->tentative && !c->template_depth && !c->instantiating) {
		if (!known)
			cxx_fail(c, "static assertion not evaluated");
		if (!value)
			cxx_fail(c, "static assertion failed");
	}
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
		bool pack = cxx_accept(c, LEX_TOKEN_ELLIPSIS);
		uint32_t pname = 0;

		if (cxx_is(c, LEX_TOKEN_IDENTIFIER)) {
			cxx_entity *e = cxx_declare(c, c->scope, cxx_peek(c, 0)->id, CXX_ENTITY_TEMPLATE);

			pname = cxx_peek(c, 0)->id;
			if (e)
				e->dependent = 1;
			cxx_advance(c);
		}
		uint32_t db = 0, de = 0;

		if (cxx_accept(c, LEX_TOKEN_EQUAL)) {
			cxx_scope *q;
			bool dep;

			db = c->pos;	/* (its default: a template's name, looked up when an instance needs it) */
			cxx_nested_name_parse(c, &q, &dep);
			cxx_expect(c, LEX_TOKEN_IDENTIFIER);
			de = c->pos;
		}
		cxx_tparam_add(c, 3, pname, pack, db, de);
		return;
	}
	if ((cxx_is_kw(c, CXX_KW_class) || cxx_is_kw(c, CXX_KW_typename)) &&
	    (cxx_peek(c, 1)->kind == LEX_TOKEN_IDENTIFIER || cxx_peek(c, 1)->kind == LEX_TOKEN_ELLIPSIS ||
	     cxx_peek(c, 1)->kind == LEX_TOKEN_COMMA || cxx_peek(c, 1)->kind == LEX_TOKEN_GREATER ||
	     cxx_peek(c, 1)->kind == LEX_TOKEN_EQUAL || cxx_peek(c, 1)->kind == LEX_TOKEN_GREATER_GREATER) &&
	    !(cxx_peek(c, 1)->kind == LEX_TOKEN_IDENTIFIER &&
	      (cxx_peek(c, 2)->kind == LEX_TOKEN_COLON_COLON || cxx_peek(c, 2)->kind == LEX_TOKEN_LESS))) {	/* (typename X<..>::type v) */
		/* type parameter */
		bool pack;
		uint32_t pname = 0, db = 0, de = 0;

		cxx_advance(c);
		pack = cxx_accept(c, LEX_TOKEN_ELLIPSIS);
		if (cxx_is(c, LEX_TOKEN_IDENTIFIER) && !cxx_kw(c)) {
			cxx_entity *e = cxx_declare(c, c->scope, cxx_peek(c, 0)->id, CXX_ENTITY_TYPE);

			pname = cxx_peek(c, 0)->id;
			if (e)
				e->dependent = 1;
			cxx_advance(c);
		}
		if (cxx_accept(c, LEX_TOKEN_EQUAL)) {
			db = c->pos;
			cxx_type_id_parse(c);
			de = c->pos;
		}
		cxx_tparam_add(c, 1, pname, pack, db, de);
		return;
	}
	/* a constrained type parameter: Concept<args> name, Concept name */
	if (cxx_is(c, LEX_TOKEN_IDENTIFIER) || cxx_is(c, LEX_TOKEN_COLON_COLON)) {
		uint32_t save = c->pos, save_undo = c->nundo;
		cxx_scope *q;
		bool dep;
		cxx_entity *e;
		uint32_t cb = c->pos;

		cxx_nested_name_parse(c, &q, &dep);
		e = cxx_is(c, LEX_TOKEN_IDENTIFIER) ? cxx_name_lookup(c, q, dep, cxx_peek(c, 0)->id) : NULL;
		e = e ? cxx_entity_template(e) : NULL;
		if (e && e->kind == CXX_ENTITY_CONCEPT) {
			bool pack;
			uint32_t pname = 0, db = 0, de = 0;

			cxx_advance(c);
			if (cxx_is(c, LEX_TOKEN_LESS))
				cxx_template_arguments_parse(c);
			pack = cxx_accept(c, LEX_TOKEN_ELLIPSIS);
			if (cxx_is(c, LEX_TOKEN_IDENTIFIER) && !cxx_kw(c)) {
				cxx_entity *p = cxx_declare(c, c->scope, cxx_peek(c, 0)->id, CXX_ENTITY_TYPE);

				pname = cxx_peek(c, 0)->id;
				if (p)
					p->dependent = 1;
				cxx_advance(c);
			}
			if (cxx_accept(c, LEX_TOKEN_EQUAL)) {
				db = c->pos;
				cxx_type_id_parse(c);
				de = c->pos;
			}
			cxx_tparam_add(c, 1, pname, pack, db, de);
			if (c->scope->kind == CXX_SCOPE_TEMPLATE && !c->tentative && c->scope->ntparams && !pack)
				c->scope->tparams[c->scope->ntparams - 1].constraint_b = cb;	/* (checked when deducing) */
			return;
		}
		cxx_rewind(c, save, save_undo);
	}
	/* non-type parameter: a parameter declaration */
	{
		bool q, f;
		uint32_t name;

		uint32_t tb = c->pos;

		if (!(cxx_decl_specifiers_parse(c, NULL, false) & (DS_TYPE | DS_MODIFIER)))
			cxx_fail(c, "expected a template parameter");
		uint32_t db = 0, de = 0;
		bool pack = cxx_is(c, LEX_TOKEN_ELLIPSIS);

		c->type_in = c->ds_type;
		name = cxx_declarator_parse(c, true, &q, &f, NULL);
		if (name > 1) {
			cxx_entity *e = cxx_declare(c, c->scope, name, CXX_ENTITY_VALUE);

			if (e)
				e->dependent = 1;
		}
		if (cxx_accept(c, LEX_TOKEN_EQUAL)) {
			db = c->pos;
			c->in_template_args++;
			cxx_conditional_expression_parse(c);
			c->in_template_args--;
			de = c->pos;
		}
		pack |= c->pos > 0 && c->tok[c->pos - 1].kind == LEX_TOKEN_ELLIPSIS && !db;
		cxx_tparam_add(c, 2, name > 1 ? name : 0, pack, db, de);
		if (c->scope->kind == CXX_SCOPE_TEMPLATE && !c->tentative && c->scope->ntparams)
			c->scope->tparams[c->scope->ntparams - 1].type_b = tb;	/* (enable_if_t<cond, int> = 0: checked when deducing) */
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
	uint32_t freq = c->ftmpl_req;

	c->ftmpl_req = cxx_is_kw(c, CXX_KW_requires) ? c->pos : 0;
	cxx_requires_clause_parse(c);
	uint32_t fbegin = c->ftmpl_begin;
	cxx_scope *fscope = c->ftmpl_scope;

	c->ftmpl_begin = c->pos, c->ftmpl_scope = ts;	/* (a function template: its signature is re-parsed to deduce) */
	if (cxx_is_kw(c, CXX_KW_concept)) {
		uint32_t name;

		cxx_advance(c);
		name = cxx_peek(c, 0)->id;
		cxx_expect(c, LEX_TOKEN_IDENTIFIER);
		cxx_attributes_skip(c);
		cxx_expect(c, LEX_TOKEN_EQUAL);
		{
			cxx_entity *ce = cxx_declare(c, cxx_declaration_scope(c)->parent ? cxx_declaration_scope(c) : c->global, name,
						     CXX_ENTITY_CONCEPT);

			/* its constraint-expression, evaluated with arguments bound (cxx_concept_value) */
			if (ce && !ce->ftmpl && c->template_depth == 1)
				ce->ftmpl = cxx_ftemplate_new(c, ts, ts->parent, c->pos, NULL);
		}
		cxx_conditional_expression_parse(c);
		cxx_expect(c, LEX_TOKEN_SEMICOLON);
	} else
		cxx_declaration_parse(c, context);
	c->ftmpl_req = freq;
	c->ftmpl_begin = fbegin, c->ftmpl_scope = fscope;
	c->template_depth--;
	cxx_scope_pop(c);
}


/* ---- classes and enums ------------------------------------------------------------------- */

static void cxx_class_body_parse(cxx *c, cxx_scope *cs);

/* one base-specifier: its type (CXX_TY_UNKNOWN if not known), and whether it is virtual */
static qty cxx_base_specifier_parse(cxx *c, cxx_scope *cs, bool *virtual_out)
{
	cxx_scope *q;
	bool dep;
	cxx_entity *base = NULL;
	qty base_ty = CXX_TY_UNKNOWN;
	bool is_virtual = false;

	cxx_attributes_skip(c);
	while (cxx_is_kw(c, CXX_KW_virtual) || cxx_is_kw(c, CXX_KW_public) || cxx_is_kw(c, CXX_KW_protected) ||
	       cxx_is_kw(c, CXX_KW_private)) {
		is_virtual |= cxx_is_kw(c, CXX_KW_virtual);
		cxx_advance(c);
	}
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
			uint8_t seen = c->dependent_seen;

			c->dependent_seen = 0;
			cxx_template_arguments_parse(c);
			/* a template-id base: inside a template it may depend on its parameters; elsewhere an
			   instance (completed: a base must be complete), else the primary's members */
			cxx_entity *te = base ? cxx_entity_template(base) : NULL;
			cxx_entity *inst = te && !c->template_depth && !c->dependent_seen && !dep ?
					   cxx_instantiate(c, te, c->targs, c->ntargs, true) : NULL;

			c->dependent_seen |= seen;
			base_ty = inst ? inst->ty : CXX_TY_UNKNOWN;
			/* an alias template's instance: the class it names (completed), its members */
			if (inst && !inst->members && inst->ty != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, inst->ty) && !c->tentative)
				inst->members = cxx_type_class_scope(c, QUNQ(inst->ty));
			if (inst && inst->members)
				cxx_scope_use(c, cs, inst->members);
			else if (c->template_depth || !te || !te->members)
				cs->has_dependent_base = 1;
			else {
				cxx_scope_use(c, cs, te->members);
				for (uint32_t i = 0; i < te->nspecs; i++)
					cxx_scope_use(c, cs, te->specs[i]);
			}
		} else {
			base = base ? cxx_entity_prefer(base, CXX_ENTITY_TYPE) : NULL;
			if (base && base->kind == CXX_ENTITY_TYPE && !base->dependent && base->ty != CXX_TY_UNKNOWN && !c->tentative) {
				cxx_scope *bs = cxx_type_class_scope(c, base->ty);	/* (an instance's typedef: completed) */

				base_ty = base->ty;
				if (bs && !base->members)
					base->members = bs;
			}
			if (dep || !base || base->kind == CXX_ENTITY_DEPENDENT || base->dependent || !base->members)
				cs->has_dependent_base = 1;
			else
				cxx_scope_use(c, cs, base->members);
		}
	}
	*virtual_out = is_virtual;
	return base_ty;
}

static void cxx_base_clause_parse(cxx *c, cxx_scope *cs)
{
	do {
		uint32_t start = c->pos, nseen = c->npacks_seen;
		cxx_entity *seen[8];
		bool is_virtual;
		qty base_ty;

		memcpy(seen, c->packs_seen, sizeof seen);
		c->npacks_seen = 0, c->packs_overflow = 0;
		base_ty = cxx_base_specifier_parse(c, cs, &is_virtual);
		if (cxx_is(c, LEX_TOKEN_ELLIPSIS)) {
			/* Bases...: one base per element of the packs it names (in an instance), else unknown */
			uint32_t end = c->pos, after;

			cxx_advance(c);
			after = c->pos;
			if (c->npacks_seen && !c->packs_overflow && c->instantiating && !c->tentative) {
				cxx_entity *packs[8];
				uint32_t np = c->npacks_seen, len = c->packs_seen[0]->npack;
				bool ok = true;

				memcpy(packs, c->packs_seen, sizeof packs);
				for (uint32_t k = 0; k < np; k++)
					ok &= packs[k]->npack == len && !packs[k]->pack_placeholder;
				for (uint32_t i = 0; ok && i < len; i++) {
					for (uint32_t k = 0; k < np; k++)
						cxx_pack_element(c, packs[k], (int32_t)i);
					c->pos = start;
					base_ty = cxx_base_specifier_parse(c, cs, &is_virtual);
					ok = c->pos == end;
					cxx_class_base(c, ok ? base_ty : CXX_TY_UNKNOWN, is_virtual);
				}
				for (uint32_t k = 0; k < np; k++)
					cxx_pack_element(c, packs[k], -1);
				if (!ok)
					cxx_class_base(c, CXX_TY_UNKNOWN, false);
				c->pos = after;
			} else
				cxx_class_base(c, CXX_TY_UNKNOWN, is_virtual);	/* (a pack of bases outside an instance) */
		} else
			cxx_class_base(c, base_ty, is_virtual);
		c->npacks_seen = nseen;
		memcpy(c->packs_seen, seen, sizeof seen);
	} while (cxx_accept(c, LEX_TOKEN_COMMA));
}

static void cxx_class_specifier_parse(cxx *c, bool is_template)
{
	cxx_scope *q = NULL;
	bool dep = false, specialization = false;
	uint32_t name = 0;
	cxx_entity *e = NULL;
	bool is_union = cxx_is_kw(c, CXX_KW_union), is_class = cxx_is_kw(c, CXX_KW_class);
	uint32_t key_loc = cxx_peek(c, 0)->loc, key_pos = c->pos, attrs = c->layout_attributes;
	/* an instantiation re-parsing this class's tokens (instantiate.c): it defines that instance */
	cxx_entity *instance = c->instantiation_target, *instance_of = c->instantiation_template;
	cxx_targ spec_args[32];
	uint32_t nspec_args = 0, spec_less = 0;

	c->instantiation_target = c->instantiation_template = NULL;
	cxx_advance(c);	/* class / struct / union */
	cxx_attributes_skip(c);
	if (cxx_is(c, LEX_TOKEN_IDENTIFIER) || cxx_is(c, LEX_TOKEN_COLON_COLON)) {
		cxx_nested_name_parse(c, &q, &dep);
		if (!cxx_is(c, LEX_TOKEN_IDENTIFIER))
			cxx_fail(c, "expected a class name");
		name = cxx_peek(c, 0)->id;
		cxx_advance(c);
		if (cxx_is(c, LEX_TOKEN_LESS)) {	/* a specialization */
			spec_less = c->pos;
			cxx_template_arguments_parse(c);
			specialization = true;
			nspec_args = c->ntargs < 32 ? c->ntargs : 32;
			memcpy(spec_args, c->targs, nspec_args * sizeof *spec_args);
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
					/* the AST record, incomplete until the definition (which reuses it): T* needs it */
					if (e && !is_template && !c->template_depth && e->ty == CXX_TY_UNKNOWN && !c->tentative)
						e->ty = QT(c->ast->tags[cc_tag_new(c->ast, is_union ? TY_UNION : TY_STRUCT, name, key_loc)].ty, 0);
					found = e;
				}
			}
			/* template<class T = void> struct X;: defaults the definition may not repeat */
			if (is_template && found && found->kind == CXX_ENTITY_TEMPLATE && cxx_is(c, LEX_TOKEN_SEMICOLON))
				cxx_template_declared(c, found, c->scope);
			c->last_type = found, c->last_type_dependent = found ? found->dependent : 1;
		}
		return;
	}
	/* a definition */
	{
		cxx_scope *target = q ? q : cxx_declaration_scope(c), *cs;
		cxx_entity *found = name ? cxx_lookup_local(target, name) : NULL;
		cxx_scope *outer = c->scope;
		cxx_entity *explicit_instance = NULL;	/* an explicit specialization: its class */

		if (instance) {
			e = instance;
			cs = cxx_scope_new(c, CXX_SCOPE_CLASS, c->scope, e);
			e->members = cs;
			specialization = false, is_template = false;
		} else if (specialization) {
			/* a partial or explicit specialization: its own scope, recorded with the template */
			cxx_entity *te = found ? cxx_entity_template(found) : NULL;

			if (!te && name)
				te = cxx_entity_template(cxx_lookup(c, name) ? cxx_lookup(c, name) : (cxx_entity *)NULL);
			cs = cxx_scope_new(c, CXX_SCOPE_CLASS, c->scope, te);
			if (te && !c->tentative) {
				te->specs = realloc(te->specs, (te->nspecs + 1) * sizeof *te->specs);
				te->specs[te->nspecs++] = cs;
				if (c->template_depth)	/* a partial specialization: matched when te is instantiated */
					cxx_template_partial(c, te, c->scope, target, spec_less, key_pos, c->ftmpl_req);
				else {
					explicit_instance = calloc(1, sizeof *explicit_instance);
					explicit_instance->name = name, explicit_instance->kind = CXX_ENTITY_TYPE;
					explicit_instance->ty = CXX_TY_UNKNOWN, explicit_instance->members = cs;
				}
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
		/* a class template's definition: what instantiating it re-parses */
		if (is_template && !specialization && c->scope->kind == CXX_SCOPE_TEMPLATE)
			cxx_template_define(c, e, c->scope, target, key_pos, 0, false);
		/* the AST record: built and laid out when C's rules are C++'s for it (types.c) */
		cxx_class_begin(c, specialization ? explicit_instance : e, name, key_loc, is_union,
				is_template || c->template_depth ? CXX_SKIP_TEMPLATE : specialization && !explicit_instance ? CXX_SKIP_SPECIALIZATION :
				c->layout_attributes != attrs ? CXX_SKIP_ATTRIBUTE : -1, is_class);
		if (explicit_instance)
			cxx_instance_register(c, e, spec_args, nspec_args, explicit_instance);
		{
			cxx_entity *built = specialization ? explicit_instance : e;

			if (built && built->ty != CXX_TY_UNKNOWN && !c->tentative)
				cxx_tag_scope_set(c, AST_TYPE(c->ast, built->ty)->aux, cs);
		}
		if (cxx_accept(c, LEX_TOKEN_COLON))
			cxx_base_clause_parse(c, cs);
		/* the injected-class-name */
		if (name && !c->tentative) {
			cxx_entity *inj = cxx_declare(c, cs, name, is_template && !specialization ? CXX_ENTITY_TEMPLATE : CXX_ENTITY_TYPE);

			if (inj) {
				inj->members = cs;
				inj->dependent = cs->dependent;
				if (e && inj != e)
					inj->ty = specialization && explicit_instance ? explicit_instance->ty : e->ty;
				if (is_template && !specialization && e)
					inj->tmpl = e->tmpl;	/* X<U> inside X: instances of the same template */
				if (instance && instance_of) {
					/* in an instance, X<...> names the template it instantiates */
					cxx_entity *tn = cxx_declare(c, cs, name, CXX_ENTITY_TEMPLATE);

					if (tn)
						tn->members = instance_of->members, tn->specs = instance_of->specs, tn->nspecs = instance_of->nspecs,
						tn->tmpl = instance_of->tmpl;
				}
				if (is_template && !specialization) {
					cxx_entity *ty = cxx_declare(c, cs, name, CXX_ENTITY_TYPE);

					if (ty)
						ty->members = cs, ty->dependent = 1;
				}
				if (specialization && e) {
					/* in a specialization the name followed by < still names the template */
					cxx_entity *tn = cxx_declare(c, cs, name, CXX_ENTITY_TEMPLATE);

					if (tn && tn != e)
						tn->members = e->members, tn->specs = e->specs, tn->nspecs = e->nspecs, tn->tmpl = e->tmpl;
				}
			}
		}
		cs->parent = c->scope;
		c->scope = cs;
		{
			uint32_t pl;
			uint8_t pv;

			if (instance || explicit_instance) {
				/* an instance is named by its spelling (instantiate.c set it); an explicit
				   specialization by its template's name and arguments */
				pl = c->name_path_len, pv = c->name_path_valid;
				if (explicit_instance) {
					char sp[512];
					size_t sl;

					if (cxx_template_id_spell(c, e, spec_args, nspec_args, sp, sizeof sp, &sl) && sl < sizeof c->name_path) {
						memcpy(c->name_path, sp, sl);
						c->name_path_len = (uint32_t)sl;
					} else
						c->name_path_valid = 0;
				}
			} else
				cxx_name_path_push(c, q || dep ? 0 : name, &pl, &pv);	/* (X::Y {...}: not tracked) */
			cxx_class_body_parse(c, cs);
			cxx_class_end(c);
			cxx_name_path_pop(c, pl, pv);
		}
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
	if (!c->class_depth && !c->tentative && !c->instantiating)
		cxx_late_parse_all(c, late_from);	/* the outermost class is complete */
	else if (c->instantiating && !c->class_depth) {
		/* an instance: its member functions' bodies wait for a call that uses them */
		for (uint32_t i = late_from; i < c->nlate; i++)
			if (c->late[i].fn && !c->late[i].fn->late) {
				c->late[i].fn->late = malloc(sizeof(cxx_late));
				*c->late[i].fn->late = c->late[i];
				c->late[i].fn->late->template_depth = 0;
			} else
				free(c->late[i].params);
		c->nlate = late_from;
	}
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
	qty underlying = CXX_TY_UNKNOWN;
	uint32_t loc = cxx_peek(c, 0)->loc;

	if (cxx_is(c, LEX_TOKEN_COLON) && (cxx_peek(c, 1)->kind != LEX_TOKEN_COLON)) {	/* underlying type */
		cxx_advance(c);
		cxx_type_id_parse(c);
		underlying = c->type_out;
	}
	{
		cxx_entity *e = name ? cxx_declare(c, cxx_declaration_scope(c), name, CXX_ENTITY_TYPE) : NULL;

		if (!c->template_depth && (cxx_is(c, LEX_TOKEN_LEFT_BRACE) || underlying != CXX_TY_UNKNOWN || scoped))
			cxx_enum_type(c, e, name, loc, underlying);
		cxx_scope *es = NULL;

		if (e && !e->members) {
			es = cxx_scope_new(c, CXX_SCOPE_CLASS, c->scope, e);
			e->members = es;
		} else if (e)
			es = e->members;
		c->last_type = e, c->last_type_dependent = 0;
		if (!cxx_accept(c, LEX_TOKEN_LEFT_BRACE))
			return;
		/* enumerators: the enumeration's type, and their values (the previous one's + 1, or = a constant) */
		int64_t next = 0;
		bool known = !c->template_depth;
		qty et = e && !c->template_depth ? e->ty : CXX_TY_UNKNOWN;

		while (!cxx_is(c, LEX_TOKEN_RIGHT_BRACE)) {
			uint32_t en = cxx_peek(c, 0)->id;
			cxx_entity *e1, *e2;

			cxx_expect(c, LEX_TOKEN_IDENTIFIER);
			cxx_attributes_skip(c);
			e1 = es ? cxx_declare(c, es, en, CXX_ENTITY_VALUE) : NULL;
			e2 = !scoped ? cxx_declare(c, cxx_declaration_scope(c), en, CXX_ENTITY_VALUE) : NULL;
			if (cxx_accept(c, LEX_TOKEN_EQUAL)) {
				cxx_scope *outer = c->scope;

				/* (the earlier enumerators are in scope: last = first + 2, in a scoped enumeration too) */
				if (es && scoped && !c->tentative) {
					cxx_scope *in = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL);

					cxx_scope_use(c, in, es);
					cxx_scope_push(c, in);
				}
				cxx_constant_expression_parse(c);
				c->scope = outer;
				known = c->econst && !c->tentative && !c->template_depth;
				next = c->evalue;
			}
			for (int k = 0; k < 2; k++) {
				cxx_entity *x = k ? e2 : e1;

				if (!x)
					continue;
				x->ty = et;
				if (known)
					x->value = next, x->has_value = 1;
			}
			next++;
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

/* does the declarator at [start, pos) name operator= ? */
static bool cxx_declarator_is_assignment(cxx *c, uint32_t start)
{
	for (uint32_t i = start; i + 1 < c->pos; i++)
		if (c->tok[i].kw == CXX_KW_operator)
			return c->tok[i + 1].kind == LEX_TOKEN_EQUAL;
	return false;
}

/* the declarators after decl-specifiers, to the ; (or a function body) */
static void cxx_init_declarators_parse(cxx *c, int flags, int context, cxx_scope *cs)
{
	cxx_entity *type_of_typedef = c->last_type;
	uint8_t typedef_dependent = c->last_type_dependent;
	qty base = c->ds_type;	/* (each declarator's parse may change c->ds_type) */
	/* auto x = init: the declarator is parsed over auto's placeholder, deduced from the initializer */
	bool is_auto = c->ds_auto && base == CXX_TY_UNKNOWN && !c->template_depth && !c->tentative && context != CXX_CONTEXT_CLASS &&
		       !(flags & (DS_TYPEDEF | DS_FRIEND));
	int auto_quals = c->ds_quals;
	/* a data member of the class being built: a class body, not a template's or a function's */
	bool member = context == CXX_CONTEXT_CLASS && c->build && !(flags & (DS_TYPEDEF | DS_STATIC | DS_FRIEND));
	bool global = context == CXX_CONTEXT_NAMESPACE && !c->template_depth && !(flags & (DS_TYPEDEF | DS_FRIEND));

	for (;;) {
		bool q, f;
		cxx_scope *qs;
		uint32_t name;
		uint32_t start = c->pos;

		if (context == CXX_CONTEXT_CLASS && cxx_is(c, LEX_TOKEN_COLON)) {	/* an unnamed bit-field */
			int64_t w;

			cxx_advance(c);
			w = cxx_literal_value(c);
			cxx_constant_expression_parse(c);
			if (member)
				cxx_class_field(c, 0, base, c->tok[start].loc, w < 0 ? -2 : (int)w);
			goto next;
		}
		c->type_in = is_auto ? QT(QI(cxx_auto_type(c)), auto_quals) : base;
		name = cxx_declarator_parse(c, false, &q, &f, &qs);
		qty t = c->type_out;
		qty auto_pattern = CXX_TY_UNKNOWN;
		cxx_entity *auto_e = NULL;
		uint32_t name_loc = c->tok[start].loc;

		if (is_auto && t != CXX_TY_UNKNOWN && cxx_type_has_placeholder(c, t)) {
			if (!f)
				auto_pattern = t;	/* (a function's auto return type: its body would tell) */
			t = CXX_TY_UNKNOWN;	/* until the initializer is seen (it may not name the variable) */
		}
		/* an object of class type defined here (not in a template): its constructor and destructor are used */
		cxx_entity *const_e = NULL;	/* a const integer whose initializer is evaluated */
		cxx_entity *fn_template = NULL;	/* the function template this declarator declares (is it a definition?) */
		bool object = !f && !(flags & (DS_TYPEDEF | DS_FRIEND)) && context != CXX_CONTEXT_CLASS && !c->tentative &&
			      !c->template_depth && t != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, t);
		if (!c->tentative) {
			c->stats.declarations++;
			c->stats.declarations_typed += t != CXX_TY_UNKNOWN;
		}
		if (f && (flags & DS_VIRTUAL) && context == CXX_CONTEXT_CLASS)
			cxx_class_skip(c, CXX_SKIP_VIRTUAL);	/* (a vptr: C++'s layout, not built yet) */
		/* a user-provided constructor, destructor or assignment (not = default): the class is not POD */
		if (f && context == CXX_CONTEXT_CLASS && c->build && !(flags & DS_FRIEND)) {
			bool special = (cs && cs->owner && name == cs->owner->name) ||	/* constructor, destructor */
				       (c->tok[start].kind == LEX_TOKEN_TILDE) ||
				       (name == 1 && c->pos > start && cxx_declarator_is_assignment(c, start));
			bool defaulted = cxx_is(c, LEX_TOKEN_EQUAL) && cxx_peek(c, 1)->kw == CXX_KW_default;

			if (special && !defaulted)
				cxx_class_member_traits(c, CXX_MEMBER_USER_SPECIAL);
		}
		/* a variable's definition needs its type complete (an instance: its body now) */
		if (!f && !(flags & (DS_TYPEDEF | DS_FRIEND)) && context != CXX_CONTEXT_CLASS && t != CXX_TY_UNKNOWN && !c->tentative) {
			qty et = t;

			while (AST_TYPE_KIND(c->ast, et) == TY_ARRAY)
				et = AST_TYPE(c->ast, et)->base;
			cxx_type_require_complete(c, et);
		}
		/* a constructor or destructor: declared under a name no source spells (cxx_ctor_use finds it) */
		if (f && name > 1 && !q && cs && cs->owner && cs->owner->name == name && context == CXX_CONTEXT_CLASS &&
		    !(flags & DS_FRIEND) && !c->tentative) {
			bool dtor = c->tok[start].kind == LEX_TOKEN_TILDE;
			bool tmpl = c->template_depth && c->scope->kind == CXX_SCOPE_TEMPLATE;
			cxx_entity *e = cxx_declare(c, cxx_declaration_scope(c), dtor ? cxx_dtor_id() : cxx_ctor_id(),
						    tmpl ? CXX_ENTITY_FUNCTION_TEMPLATE : CXX_ENTITY_VALUE);
			qty ps[32];
			bool known = c->ndecl_params <= 32;

			for (uint32_t i = 0; known && i < c->ndecl_params; i++)
				known = (ps[i] = c->decl_params[i].ty) != CXX_TY_UNKNOWN;
			if (e)
				e->member_of = cxx_declaration_scope(c);
			if (e && !tmpl) {
				e->ty = known ? cxx_type_function(c, cc_type_basic(TY_VOID), ps, c->ndecl_params, false) : CXX_TY_UNKNOWN;
				e->ndefaults = (uint16_t)c->decl_ndefaults;
				c->last_fn = e;	/* (its body: parsed when used) */
			} else if (e && !dtor && c->template_depth == 1 && c->scope == c->ftmpl_scope) {
				e->ndefaults = (uint16_t)c->decl_ndefaults;
				e->ftmpl = cxx_ftemplate_new(c, c->scope, c->scope->parent, c->ftmpl_begin, cs);
				cxx_ftemplate_requires(e->ftmpl, c->ftmpl_req);
				fn_template = e;
			}
		}
		/* what it declares (not a constructor or destructor: they have no name to look up) */
		if (name > 1 && !q && !(cs && cs->owner && cs->owner->name == name)) {
			int kind = (flags & DS_TYPEDEF) ? CXX_ENTITY_TYPE :
				   c->template_depth && c->scope->kind == CXX_SCOPE_TEMPLATE ? (f ? CXX_ENTITY_FUNCTION_TEMPLATE : CXX_ENTITY_VARIABLE_TEMPLATE)
											   : CXX_ENTITY_VALUE;
			cxx_entity *e = (flags & DS_FRIEND) ? NULL : cxx_declare(c, cxx_declaration_scope(c), name, kind);

			if ((flags & DS_FRIEND) && f && kind == CXX_ENTITY_FUNCTION_TEMPLATE)
				e = NULL;	/* (a friend template: found by ADL only) */
			/* a function template (one level of parameters): its signature, for deduction at calls */
			if (e && kind == CXX_ENTITY_FUNCTION_TEMPLATE && !e->ftmpl && c->template_depth == 1 && c->scope == c->ftmpl_scope &&
			    !c->tentative) {
				e->ftmpl = cxx_ftemplate_new(c, c->scope, c->scope->parent, c->ftmpl_begin, NULL);
				cxx_ftemplate_requires(e->ftmpl, c->ftmpl_req);
				fn_template = e;
			}
			if (e && (flags & DS_TYPEDEF)) {
				e->dependent = typedef_dependent;
				e->ty = t;
				if (t == CXX_TY_UNKNOWN && !c->template_depth && context == CXX_CONTEXT_CLASS)
					cxx_class_unverifiable(c);
				if (type_of_typedef && type_of_typedef->members && !typedef_dependent)
					e->members = type_of_typedef->members, e->specs = type_of_typedef->specs, e->nspecs = type_of_typedef->nspecs;
			}
			if (e && kind == CXX_ENTITY_VALUE)
				e->ty = t;	/* a variable's, a data member's, a function's type */
			if (e && kind == CXX_ENTITY_VALUE && auto_pattern != CXX_TY_UNKNOWN)
				auto_e = e;
			/* a variable template: its initializer, evaluated with arguments (cxx_vtemplate_value); a partial
			   or explicit specialization joins the primary */
			if (e && !f && !c->tentative && !c->instantiating && !(flags & DS_TYPEDEF) && cxx_is(c, LEX_TOKEN_EQUAL)) {
				uint32_t less = c->decl_less;
				bool in_template = c->template_depth == 1 && c->scope == c->ftmpl_scope;

				if (kind == CXX_ENTITY_VARIABLE_TEMPLATE && !less && in_template && !e->ftmpl) {
					e->ftmpl = cxx_ftemplate_new(c, c->scope, c->scope->parent, c->pos + 1, NULL);
					e->ty = t;
				} else if (less && ((kind == CXX_ENTITY_VARIABLE_TEMPLATE && in_template) ||
						    (kind == CXX_ENTITY_VALUE && c->explicit_specialization && !c->template_depth))) {
					for (cxx_entity *x = e->next; x; x = x->next)
						if (x->kind == CXX_ENTITY_VARIABLE_TEMPLATE && x->ftmpl) {
							cxx_vtemplate_special(c, x->ftmpl, kind == CXX_ENTITY_VARIABLE_TEMPLATE ? c->scope : NULL,
									      kind == CXX_ENTITY_VARIABLE_TEMPLATE ? c->scope->parent : c->scope,
									      less, c->pos + 1);
							break;
						}
				}
			}
			c->last_fn = e && kind == CXX_ENTITY_VALUE && f && context == CXX_CONTEXT_CLASS ? e : NULL;
			if (e && f)
				e->fn_const = c->decl_const;
			if (e && f && context == CXX_CONTEXT_CLASS)
				e->member_of = cxx_declaration_scope(c);
			if (e && f && c->decl_ndefaults > e->ndefaults)
				e->ndefaults = (uint16_t)c->decl_ndefaults;	/* (redeclarations may add defaults) */
			if (e && global && kind == CXX_ENTITY_VALUE && !e->sym)
				e->sym = cxx_symbol(c, f ? S_FUNC : S_VAR, name, t, name_loc);
			if (e && kind == CXX_ENTITY_VALUE && !f && ((flags & DS_CONSTEXPR) || (t != CXX_TY_UNKNOWN && (QQ(t) & Q_CONST))) &&
			    cxx_is(c, LEX_TOKEN_EQUAL)) {
				int64_t v;

				cxx_advance(c);
				v = cxx_literal_value(c);
				c->pos--;
				if (v >= 0 && !c->tentative)
					e->value = v, e->has_value = 1;
				else if (!c->tentative && !c->template_depth && t != CXX_TY_UNKNOWN &&
					 AST_TYPE_KIND(c->ast, t) >= TY_BOOL && AST_TYPE_KIND(c->ast, t) <= TY_ULLONG)
					const_e = e;	/* (its initializer: a constant expression, evaluated below) */
			}
		}
		if (member && !f && name > 1) {
			/* a data member: its bit-field width (a literal) comes after the declarator */
			int w = -1;

			if (cxx_is(c, LEX_TOKEN_COLON)) {
				int64_t v = (cxx_advance(c), cxx_literal_value(c));

				w = v < 0 ? -2 : (int)v;
				cxx_constant_expression_parse(c);
			}
			cxx_class_field(c, name, t, name_loc, w);
			if (cxx_is(c, LEX_TOKEN_EQUAL) || cxx_is(c, LEX_TOKEN_LEFT_BRACE))
				cxx_class_member_traits(c, CXX_MEMBER_INITIALIZER);	/* (a default member initializer) */
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
		if (f && cxx_is_function_body_start(c) && !(context == CXX_CONTEXT_BLOCK) && fn_template)
			cxx_ftemplate_defined(fn_template->ftmpl);
		if (f && cxx_is_function_body_start(c) && !(context == CXX_CONTEXT_BLOCK)) {
			/* template<...> R X<...>::f(...) { }: a class template's member defined outside it, instantiated
			   for an instance when a call uses that member */
			if (q && qs && qs->owner && qs->owner->tmpl && c->template_depth == 1 && c->scope == c->ftmpl_scope && !c->tentative &&
			    !c->instantiating && !c->class_depth && context != CXX_CONTEXT_CLASS) {
				uint32_t mname = name;
				int depth = 0;

				if (name == qs->owner->name) {
					mname = cxx_ctor_id();
					for (uint32_t i = start; i < c->pos; i++) {
						int k = c->tok[i].kind;

						depth += k == LEX_TOKEN_LEFT_PAREN ? 1 : k == LEX_TOKEN_RIGHT_PAREN ? -1 : 0;
						if (k == LEX_TOKEN_TILDE && !depth)
							mname = cxx_dtor_id();
					}
				}
				cxx_template_member_definition(c, qs->owner, mname, c->scope, c->scope->parent, c->ftmpl_begin, c->ndecl_params,
							       c->decl_const);
			}
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
				cxx_params_declare(c, fs, c->decl_params, c->ndecl_params);
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
			} else if (object && !cxx_is(c, LEX_TOKEN_LEFT_BRACE)) {
				cxx_initializer_parse(c);	/* copy-initialization: a constructor from the value */
				cxx_ctor_use(c, t, &c->etype, (uint8_t[]){ c->elval }, 1, false);
			} else if (object) {
				cxx_braced_arguments_parse(c);
				cxx_ctor_use(c, t, c->cargs, c->cargs_lval, c->ncargs, true);
			} else {
				bool braced = cxx_is(c, LEX_TOKEN_LEFT_BRACE);

				cxx_initializer_parse(c);
				/* auto x = expr: its type from the initializer's */
				if (auto_e && !braced && c->etype != CXX_TY_UNKNOWN) {
					bool k = c->econst;
					int64_t v = c->evalue;

					auto_e->ty = cxx_auto_deduce(c, auto_pattern, c->etype, c->elval);
					if (auto_e->ty != CXX_TY_UNKNOWN && !(QQ(auto_e->ty) & Q_CONST))
						k = false;
					if (k && AST_TYPE_KIND(c->ast, auto_e->ty) >= TY_BOOL && AST_TYPE_KIND(c->ast, auto_e->ty) <= TY_ULLONG)
						auto_e->value = v, auto_e->has_value = 1;	/* const auto n = 4; */
				}
				if (const_e && c->econst) {
					const_e->value = c->evalue, const_e->has_value = 1;
					if (AST_TYPE_KIND(c->ast, t) == TY_BOOL)
						const_e->value = c->evalue != 0;
				}
			}
		} else if (cxx_is(c, LEX_TOKEN_LEFT_BRACE)) {
			if (object) {
				cxx_braced_arguments_parse(c);
				cxx_ctor_use(c, t, c->cargs, c->cargs_lval, c->ncargs, true);
			} else
				cxx_braced_init_list_parse(c);
		} else if (!f && cxx_is(c, LEX_TOKEN_LEFT_PAREN)) {
			if (object) {
				cxx_call_arguments_parse(c);	/* direct-initialization */
				cxx_ctor_use(c, t, c->cargs, c->cargs_lval, c->ncargs, false);
			} else
				cxx_expression_list_parse(c);
		} else if (object)
			cxx_ctor_use(c, t, NULL, NULL, 0, false);	/* default-initialization */
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

	if (cxx_is(c, LEX_TOKEN_SEMICOLON)) {	/* struct S {...}; enum E {...}; friend class X; */
		/* an anonymous struct or union member: an unnamed field (C's layout has them) */
		if (context == CXX_CONTEXT_CLASS && c->build && !(flags & (DS_TYPEDEF | DS_STATIC | DS_FRIEND)) && c->defined_unnamed_class) {
			if (c->ds_type != CXX_TY_UNKNOWN)
				cxx_class_field(c, 0, c->ds_type, cxx_peek(c, 0)->loc, -1);
			else
				cxx_class_skip(c, CXX_SKIP_UNKNOWN_MEMBER);	/* (never lay out a class without a member) */
		}
		cxx_advance(c);
		return;
	}
	if (!flags && !(cxx_is(c, LEX_TOKEN_TILDE) || cxx_is(c, LEX_TOKEN_IDENTIFIER) || cxx_is(c, LEX_TOKEN_COLON_COLON) ||
			cxx_is_kw(c, CXX_KW_operator)))
		cxx_fail(c, "expected a declaration");
	cxx_init_declarators_parse(c, flags, context, cs);
}

static void cxx_member_declaration_parse(cxx *c, cxx_scope *cs)
{
	int kw;
	uint32_t member_attrs = c->layout_attributes;	/* (leading [[no_unique_address]] counts too) */

	cxx_attributes_skip(c);
	kw = cxx_kw(c);
	if ((kw == CXX_KW_public || kw == CXX_KW_private || kw == CXX_KW_protected) && cxx_peek(c, 1)->kind == LEX_TOKEN_COLON) {
		cxx_class_access(c, kw != CXX_KW_public);
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
	{
		struct cxx_class_build *b = c->build;

		cxx_simple_declaration_parse(c, CXX_CONTEXT_CLASS, cs);
		if (c->layout_attributes != member_attrs && c->build == b)
			cxx_class_skip(c, CXX_SKIP_ATTRIBUTE);	/* (aligned, packed, no_unique_address: not built yet) */
	}
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

/* an instance's class (instantiate.c): the class-specifier at the template's class-key */
VPRIV void cxx_class_specifier_for_instance(cxx *c)
{
	cxx_class_specifier_parse(c, false);
}

VPRIV void cxx_translation_unit_parse(cxx *c)
{
	while (!cxx_is(c, LEX_TOKEN_EOF))
		cxx_declaration_parse(c, CXX_CONTEXT_NAMESPACE);
}

/* a function template's signature, at its decl-specifiers (instantiate.c binds its parameters first) */
VPRIV qty cxx_function_signature_parse(cxx *c, bool any_return, bool *body, cxx_scope *ctor_class)
{
	bool q, f;
	qty t;

	uint8_t missing = c->member_missing, invalid = c->einvalid;

	c->member_missing = 0;	/* (typename X::y with no y in the signature: the candidate is not viable) */
	c->einvalid = 0;	/* (and an expression that is ill-formed for sure: -> decltype(c.begin()) for an int) */
	c->sig_requires = 1;
	cxx_attributes_skip(c);
	if (!(cxx_decl_specifiers_parse(c, ctor_class, false) & (DS_TYPE | DS_MODIFIER)) && !ctor_class) {
		c->member_missing = missing, c->einvalid = invalid;
		return CXX_TY_UNKNOWN;
	}
	c->type_in = ctor_class ? cc_type_basic(TY_VOID) : c->ds_type;	/* (a constructor: nothing returned) */
	/* auto f(): the declarator over auto's placeholder; the body's first return gives the type */
	bool ret_auto = !ctor_class && c->ds_auto && c->ds_type == CXX_TY_UNKNOWN;
	qty ret_pattern = CXX_TY_UNKNOWN;

	if (ret_auto)
		c->type_in = QT(QI(cxx_auto_type(c)), c->ds_quals);
	if (any_return && c->type_in == CXX_TY_UNKNOWN)
		c->type_in = cc_type_basic(TY_VOID);	/* (deduction reads the parameters only) */
	c->decl_return = CXX_TY_UNKNOWN;
	cxx_declarator_parse(c, false, &q, &f, NULL);
	t = f ? c->type_out : CXX_TY_UNKNOWN;
	c->sig_is_function = f;
	c->sig_auto = 0;
	if (ret_auto && f) {
		c->sig_auto = c->decl_return != CXX_TY_UNKNOWN && cxx_type_has_placeholder(c, c->decl_return);
		/* the placeholder stays here: a pattern for the deduction below, void or unknown to callers */
		if (c->decl_return != CXX_TY_UNKNOWN && cxx_type_has_placeholder(c, c->decl_return)) {
			ret_pattern = c->decl_return;
			c->decl_return = any_return ? cc_type_basic(TY_VOID) : CXX_TY_UNKNOWN;
			if (t != CXX_TY_UNKNOWN) {
				const ctype *ft = AST_TYPE(c->ast, t);

				t = any_return ? cxx_type_function(c, cc_type_basic(TY_VOID), &c->ast->params[ft->aux], ft->n,
								   (ft->flags & TF_VARIADIC) != 0) : CXX_TY_UNKNOWN;
			}
		}
	}
	/* a parameter of unknown type: the return type alone (callers read only it then) */
	c->sig_fake = 0;
	if (f && t == CXX_TY_UNKNOWN && !any_return && c->decl_return != CXX_TY_UNKNOWN)
		t = cxx_type_function(c, c->decl_return, NULL, 0, true), c->sig_fake = 1;
	/* a trailing requires-clause: its value with the arguments bound (false: the candidate is not viable) */
	c->sig_requires = 1;
	if (f && cxx_is_kw(c, CXX_KW_requires))
		c->sig_requires = (int8_t)(any_return ? (cxx_requires_clause_parse(c), 1) : cxx_requires_clause_value(c));
	if ((c->member_missing || c->einvalid) && !any_return)
		c->sig_requires = 0;	/* a substitution failure in the return or a parameter type */
	c->member_missing = missing, c->einvalid = invalid;
	/* body: the definition's body too (an instance's), its parameters declared */
	if (body) {
		*body = false;
		cxx_attributes_skip(c);
		if (f && cxx_is_function_body_start(c)) {
			cxx_scope *fs = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL);
			cxx_scope *outer = c->scope;

			cxx_scope_push(c, fs);
			cxx_params_declare(c, fs, c->decl_params, c->ndecl_params);
			c->ret_capture = ret_pattern != CXX_TY_UNKNOWN, c->ret_type = CXX_TY_UNKNOWN, c->ret_seen = 0;
			cxx_function_body_parse(c);
			c->scope = outer;
			*body = true;
			if (c->ret_capture) {
				/* no return statement: void */
				qty r;

				if (c->ret_type == CXX_TY_UNKNOWN && !c->ret_seen)
					c->ret_type = cc_type_basic(TY_VOID);	/* no return statement */
				r = c->ret_type == CXX_TY_UNKNOWN ? CXX_TY_UNKNOWN :
				    AST_TYPE_KIND(c->ast, c->ret_type) == TY_VOID ? (ret_pattern == cxx_auto_type(c) ? c->ret_type : CXX_TY_UNKNOWN) :
				    cxx_auto_deduce(c, ret_pattern, c->ret_type, c->ret_lval);

				c->ret_capture = 0;
				if (r != CXX_TY_UNKNOWN)
					t = cxx_type_function(c, r, NULL, 0, true), c->sig_fake = 1;	/* (callers read the return type only) */
			}
		}
	}
	return t;
}

/* one deferred member function body (an instance's, on use): as cxx_late_parse_all does */
VPRIV void cxx_late_parse_one(cxx *c, const cxx_late *l)
{
	cxx_scope *fs;

	c->pos = l->begin;
	c->this_const = l->fn && l->fn->fn_const;
	c->template_depth = l->template_depth;
	c->this_type = l->scope->kind == CXX_SCOPE_CLASS && l->scope->owner && l->scope->owner->ty ? l->scope->owner->ty : CXX_TY_UNKNOWN;
	c->scope = l->scope;
	c->class_depth = 0;
	fs = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL);
	cxx_scope_push(c, fs);
	cxx_params_declare(c, fs, l->params, l->nparams);
	cxx_function_body_parse(c);
	if (c->pos != l->end)
		cxx_fail(c, "member function body ended early");
}

/* for (decl-specifiers declarator : range): the declaration (not a structured binding), up to the colon */
VPRIV void cxx_for_range_declaration_parse(cxx *c)
{
	bool q, f, is_auto;
	int quals;

	cxx_attributes_skip(c);
	if (!(cxx_decl_specifiers_parse(c, NULL, false) & (DS_TYPE | DS_MODIFIER)))
		cxx_fail(c, "expected a type");
	is_auto = c->ds_auto && c->ds_type == CXX_TY_UNKNOWN, quals = c->ds_quals;
	c->type_in = is_auto ? QT(QI(cxx_auto_type(c)), quals) : c->ds_type;
	c->range_name = cxx_declarator_parse(c, false, &q, &f, NULL);
	c->range_type = c->type_out, c->range_auto = is_auto;
	if (!cxx_is(c, LEX_TOKEN_COLON))
		cxx_fail(c, "expected ':'");
}

/* a lambda's ( parameters ): as a function's (c->fn_params, c->fn_pnames, c->fn_nparams, c->fn_unknown) */
VPRIV void cxx_lambda_parameters_parse(cxx *c)
{
	cxx_parameter_clause_parse(c);
}

/* requires constraint-expression, at 'requires': 1 true, 0 false, -1 not known */
VPRIV int cxx_requires_clause_value(cxx *c)
{
	int v;

	cxx_expect_kw(c, CXX_KW_requires);
	c->unevaluated++;
	cxx_conditional_expression_parse(c);
	c->unevaluated--;
	v = c->econst ? c->evalue != 0 : -1;
	return v;
}

/* a type-constraint's concept, at its name: [nested::]Concept [<args>] (the arguments in c->targs, c->ntargs) */
VPRIV cxx_entity *cxx_constraint_name_parse(cxx *c)
{
	cxx_scope *q;
	bool dep;
	cxx_entity *e;

	cxx_nested_name_parse(c, &q, &dep);
	e = cxx_is(c, LEX_TOKEN_IDENTIFIER) ? cxx_name_lookup(c, q, dep, cxx_peek(c, 0)->id) : NULL;
	e = e ? cxx_entity_template(e) : NULL;
	if (!e || e->kind != CXX_ENTITY_CONCEPT)
		return NULL;
	cxx_advance(c);
	c->ntargs = 0;
	if (cxx_is(c, LEX_TOKEN_LESS))
		cxx_template_arguments_parse(c);
	return e;
}

/* a template template parameter's default argument, at its name: the template (out->kind 4), else unknown */
VPRIV void cxx_template_name_default_parse(cxx *c, cxx_targ *out)
{
	cxx_scope *q;
	bool dep;
	cxx_entity *e;

	out->kind = 0;
	cxx_nested_name_parse(c, &q, &dep);
	if (!cxx_is(c, LEX_TOKEN_IDENTIFIER) || dep)
		cxx_fail(c, "expected a template name");
	e = q ? cxx_lookup_in(c, q, cxx_peek(c, 0)->id, 0) : cxx_lookup(c, cxx_peek(c, 0)->id);
	e = e ? cxx_entity_prefer(e, CXX_ENTITY_TEMPLATE) : NULL;
	cxx_advance(c);
	if (e && e->kind == CXX_ENTITY_TEMPLATE && !e->is_pack && e->tmpl && !e->dependent)
		out->kind = 4, out->v = (int64_t)(intptr_t)e;
}
