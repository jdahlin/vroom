/* C++ declarations: decl-specifiers, type names, pointer operators */
#include "declaration.h"

VPRIV bool cxx_kw_is_simple_type(int kw)
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

VPRIV bool cxx_kw_is_type_modifier(int kw)
{
	switch (kw) {
	case CXX_KW_short: case CXX_KW_long: case CXX_KW_signed: case CXX_KW_unsigned: case CXX_KW___signed: case CXX_KW___signed__:
	case CXX_KW__Complex: case CXX_KW___complex__:
		return true;
	}
	return false;
}

VPRIV bool cxx_kw_is_cv(int kw)
{
	switch (kw) {
	case CXX_KW_const: case CXX_KW_volatile: case CXX_KW___const: case CXX_KW___const__: case CXX_KW___volatile:
	case CXX_KW___volatile__: case CXX_KW___restrict: case CXX_KW___restrict__: case CXX_KW___unaligned:
		return true;
	}
	return false;
}

VPRIV bool cxx_kw_is_decl_specifier(int kw)
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
VPRIV bool cxx_type_name_try(cxx *c, cxx_scope *ctor_class)
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
		typename_kw |= dep;	/* (X known: what Y is decides; X::template f<int>() names a function) */
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
VPRIV void cxx_builtin_template(cxx *c, uint32_t id, const cxx_targ *a, uint32_t n)
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
VPRIV void cxx_spec_keyword(cxx_spec *sp, int kw)
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

VPRIV int cxx_decl_specifiers_parse(cxx *c, cxx_scope *ctor_class, bool is_template)
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
		} else if (kw == CXX_KW_inline || kw == CXX_KW___inline || kw == CXX_KW___inline__) {
			flags |= DS_INLINE;
			cxx_advance(c);
		} else if (kw == CXX_KW_mutable) {
			flags |= DS_MUTABLE;
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
				flags |= !c->econst || c->template_depth ? DS_EXPLICIT_MAYBE : c->evalue ? DS_EXPLICIT : 0;
				cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
			} else
				flags |= DS_EXPLICIT;
		} else if (cxx_kw_is_decl_specifier(kw) || cxx_kw_is_cv(kw)) {
			if (kw == CXX_KW_extern)
				flags |= DS_EXTERN_SEEN;
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
			bool unnamed = cxx_class_key_unnamed(c);

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

VPRIV void cxx_parameter_clause_parse(cxx *c);
VPRIV void cxx_parameter_clause_body_parse(cxx *c);
VPRIV void cxx_function_tail_parse(cxx *c);
VPRIV void cxx_params_declare(cxx *c, cxx_scope *fs, const cxx_param *ps, uint32_t n);

/* ptr-operator: * cv, &, &&, X::* cv (with attributes); false if none.  t (may be NULL): the type it applies to */
VPRIV bool cxx_ptr_operator_parse(cxx *c, qty *t)
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
		if (c->pos != save && cxx_is(c, LEX_TOKEN_STAR)) {
			/* a pointer to member: an opaque type of its own (no such values are built yet), so that a template's
			   parameter of that form is a deduction that fails for other arguments, not one not known */
			cxx_advance(c);
			while (cxx_kw_is_cv(cxx_kw(c)))
				cxx_advance(c);
			if (t) {
				if (!c->memptr_ty)
					c->memptr_ty = QT(c->ast->tags[cc_tag_new(c->ast, TY_STRUCT, vintern_cstr("__vcc_member_pointer"), 0)].ty, 0);
				*t = c->memptr_ty;
			}
			return true;
		}
		cxx_rewind(c, save, save_undo);
	}
	return false;
}

VPRIV void cxx_template_arguments_then_paren(cxx *c)
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

/* the function just declared has default arguments: where their tokens are, for the calls that leave them out */
VPRIV void cxx_defaults_record(cxx *c, cxx_entity *e)
{
	if (c->tentative || !e->ndefaults || c->fn_ndefs != e->ndefaults || c->template_depth)
		return;
	e->defs = malloc(2 * c->fn_ndefs * sizeof *e->defs);
	memcpy(e->defs, c->fn_defs, 2 * c->fn_ndefs * sizeof *e->defs);
	e->def_scope = c->scope;
}

/* operator-function-id or conversion-function-id, after 'operator': its name (1: not entered in a scope) */
VPRIV uint32_t cxx_operator_name_parse(cxx *c)
{
	int k = cxx_kind(c);

	if (cxx_is_kw(c, CXX_KW_new) || cxx_is_kw(c, CXX_KW_delete)) {
		/* operator new, operator delete[]: named so (with the space, like no other operator's name) */
		bool is_new = cxx_is_kw(c, CXX_KW_new), arr = false;

		cxx_advance(c);
		if (cxx_is(c, LEX_TOKEN_LEFT_SQUARE)) {
			cxx_advance(c);
			cxx_expect(c, LEX_TOKEN_RIGHT_SQUARE);
			arr = true;
		}
		return vintern_cstr(is_new ? (arr ? "operator new[]" : "operator new") : (arr ? "operator delete[]" : "operator delete"));
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
		uint32_t id = cxx_operator_id_at(c, 1);	/* operator<=>, operator->*: one token, their names */

		cxx_advance(c);
		return id;
	}
	/* conversion: operator type-specifiers ptr-operators */
	cxx_scope *conv_class = NULL;
	bool ptr = false;

	for (cxx_scope *cs = c->scope; cs; cs = cs->parent)
		if (cs->kind == CXX_SCOPE_CLASS) {
			cs->has_conversion = 1;
			conv_class = cs;
			break;
		} else if (cs->kind == CXX_SCOPE_NAMESPACE)
			break;
	if (!cxx_decl_specifiers_parse(c, NULL, false))
		cxx_fail(c, "expected an operator or a type after 'operator'");
	qty conv_to = c->ds_type;

	while (cxx_ptr_operator_parse(c, conv_to != CXX_TY_UNKNOWN ? &conv_to : NULL))
		ptr = true;
	(void)ptr;
	/* the type it converts to, for is_convertible and the like (a class's, not a template's) */
	if (conv_class && !c->tentative) {
		if (conv_to == CXX_TY_UNKNOWN || c->template_depth || cxx_type_has_placeholder(c, conv_to))
			conv_class->conv_unknown = 1;
		else {
			/* (its name in the class: "operator #" and the type's number; mangle.c writes cv <type>) */
			char nm[64] = { 0 };

			conv_class->conv_types = realloc(conv_class->conv_types, (conv_class->nconv + 1) * sizeof(qty));
			conv_class->conv_types[conv_class->nconv++] = conv_to;
			snprintf(nm, 32, "operator #%u", (unsigned)QUNQ(conv_to));
			c->conv_last = conv_to;
			return vintern_cstr(nm);
		}
	}
	return 1;
}
