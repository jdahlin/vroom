/* C++ declarations: entities, name lookup, nested names, template arguments */
#include "declaration.h"

VPRIV bool cxx_entity_is_type(const cxx_entity *e)
{
	return e && e->kind == CXX_ENTITY_TYPE;
}

VPRIV bool cxx_entity_is_template(const cxx_entity *e)
{
	return e && (e->kind == CXX_ENTITY_TEMPLATE || e->kind == CXX_ENTITY_FUNCTION_TEMPLATE ||
		     e->kind == CXX_ENTITY_VARIABLE_TEMPLATE || e->kind == CXX_ENTITY_CONCEPT);
}

/* t without the qualifiers in `drop`: an array's are its elements' ([basic.type.qualifier]: remove_cv<const char[2]>
   is char[2]) */
VPRIV qty cxx_type_drop_quals(cxx *c, qty t, unsigned drop)
{
	if (t != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, t) == TY_ARRAY) {
		const ctype *x = AST_TYPE(c->ast, t);
		qty e = cxx_type_drop_quals(c, x->base, drop);

		return e == x->base ? QT(QI(t), QQ(t) & ~drop) : QT(QI(cc_type_array(c->ast, e, x->n, x->flags & TF_INCOMPLETE)), QQ(t) & ~drop);
	}
	return QT(QI(t), QQ(t) & ~drop);
}

/* clang's type transform builtins (__remove_reference_t(T), ...): CXX_TY_UNKNOWN for those not done */
VPRIV qty cxx_type_transform(cxx *c, const char *n, size_t len, qty t)
{
	const ctype *x = AST_TYPE(c->ast, t);
	bool ref = x->kind == TY_PTR && x->ext;
	qty base = ref ? x->base : t;
#define IS(s) (len == sizeof s - 1 && !memcmp(n, s, len))

	if (IS("__remove_reference_t"))
		return base;
	if (IS("__remove_cvref"))
		return cxx_type_drop_quals(c, base, Q_CONST | Q_VOLATILE | Q_RESTRICT);
	if (IS("__remove_cv"))
		return ref ? t : cxx_type_drop_quals(c, t, Q_CONST | Q_VOLATILE | Q_RESTRICT);
	if (IS("__remove_const"))
		return ref ? t : cxx_type_drop_quals(c, t, Q_CONST);
	if (IS("__remove_volatile"))
		return ref ? t : cxx_type_drop_quals(c, t, Q_VOLATILE);
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
	if (IS("__underlying_type"))
		return x->kind == TY_ENUM ? x->base : CXX_TY_UNKNOWN;
	if (IS("__make_signed") || IS("__make_unsigned")) {
		/* the integer type of the same size and the other signedness (cv kept); an enumeration's by its size */
		bool to_unsigned = n[7] == 'u';
		int k = x->kind == TY_ENUM ? AST_TYPE_KIND(c->ast, x->base) : x->kind, r;

		if (ref || k < TY_CHAR || k > TY_UINT128)
			return CXX_TY_UNKNOWN;
		switch (k) {
		case TY_CHAR: case TY_SCHAR: case TY_UCHAR: r = to_unsigned ? TY_UCHAR : TY_SCHAR; break;
		case TY_SHORT: case TY_USHORT: r = to_unsigned ? TY_USHORT : TY_SHORT; break;
		case TY_INT: case TY_UINT: r = to_unsigned ? TY_UINT : TY_INT; break;
		case TY_LONG: case TY_ULONG: r = to_unsigned ? TY_ULONG : TY_LONG; break;
		case TY_LLONG: case TY_ULLONG: r = to_unsigned ? TY_ULLONG : TY_LLONG; break;
		default: r = to_unsigned ? TY_UINT128 : TY_INT128; break;
		}
		return QT(QI(cc_type_basic(r)), QQ(t));
	}
#undef IS
	return CXX_TY_UNKNOWN;	/* (__remove_restrict) */
}

/* the entity a name denotes in qualifier (NULL: unqualified); NULL if none */
VPRIV cxx_entity *cxx_name_lookup(cxx *c, cxx_scope *qualifier, bool dependent, uint32_t id)
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
VPRIV cxx_entity *cxx_entity_prefer(cxx_entity *e, int kind)
{
	for (cxx_entity *x = e; x; x = x->next)
		if (x->kind == kind)
			return x;
	return e;
}

VPRIV cxx_entity *cxx_entity_template(cxx_entity *e)
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
			cxx_scope *cls = NULL;

			cxx_advance(c);
			cxx_skip_balanced(c);
			if (!cxx_is(c, LEX_TOKEN_COLON_COLON)) {
				cxx_rewind(c, save, save_undo);
				break;
			}
			/* decltype(expr)::name outside templates: the class expr's type is (decltype(test<T>(0))::value) */
			if (!c->template_depth) {
				uint32_t after = c->pos, targs = c->in_template_args;

				c->pos = save + 2;
				c->in_template_args = 0;
				c->unevaluated++;
				cxx_expression_parse(c);
				c->unevaluated--;
				c->in_template_args = targs;
				if (c->pos + 1 == after && c->etype != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, c->etype))
					cls = cxx_type_class_scope(c, QUNQ(c->etype));
				c->pos = after;
			}
			cxx_advance(c);
			if (cls)
				*qualifier = cls, c->qualifier_hint = cls;
			else
				*dependent = true, *qualifier = NULL;
			continue;
		}
		break;
	}
	return last;
}

/* ---- template arguments ------------------------------------------------------------------ */

VPRIV int64_t cxx_literal_value(cxx *c);

/* does the token `ahead` end a template argument: a comma, or a > that is not the first piece of >= */
VPRIV bool cxx_ends_argument(cxx *c, uint32_t ahead)
{
	const cxx_token *t = cxx_peek(c, ahead), *n = cxx_peek(c, ahead + 1);

	if (t->kind == LEX_TOKEN_COMMA)
		return true;
	return t->kind == LEX_TOKEN_GREATER && !(n->kind == LEX_TOKEN_EQUAL && n->s == t->s + 1);
}
VPRIV void cxx_builtin_template(cxx *c, uint32_t id, const cxx_targ *a, uint32_t n);

/* one template argument; *out: what it is, as far as known (a type, an integer literal) */
VPRIV void cxx_template_argument_parse(cxx *c, cxx_targ *out)
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
			cxx_entity *inj = e ? cxx_entity_prefer(e, CXX_ENTITY_TYPE) : NULL;

			e = e ? cxx_entity_prefer(e, CXX_ENTITY_TEMPLATE) : NULL;
			/* an instance's injected class name (J inside J<long>): the instance, a type; where a template
			   template parameter takes it, cxx_instantiate turns it back into its template */
			if (e && inj && inj->kind == CXX_ENTITY_TYPE && !inj->dependent && inj->ty != CXX_TY_UNKNOWN && !c->template_depth &&
			    cc_type_is_record(c->ast, inj->ty))
				e = NULL;
			if (e && e->kind == CXX_ENTITY_TEMPLATE && !e->is_pack) {
				cxx_advance(c);
				if (e->tmpl && !e->dependent)
					out->kind = 4, out->v = (int64_t)(intptr_t)(e->bound_template ? e->bound_template : e);
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
					bool ok = true, was_invalid = c->member_missing || c->einvalid;

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
						/* an element that is ill-formed ends the substitution (enable_if_t<Ps::value>...: the
						   elements after a false one are not looked at; std::_And relies on it) */
						if (ok && (c->member_missing || c->einvalid) && !was_invalid)
							break;
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

VPRIV void cxx_class_specifier_parse(cxx *c, bool is_template);
VPRIV void cxx_enum_specifier_parse(cxx *c);
