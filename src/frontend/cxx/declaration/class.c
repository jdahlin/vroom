/* C++ declarations: classes, bases, enumerations */
#include "declaration.h"

/* one base-specifier: its type (CXX_TY_UNKNOWN if not known), and whether it is virtual */
VPRIV qty cxx_base_specifier_parse(cxx *c, cxx_scope *cs, bool *virtual_out)
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

VPRIV void cxx_base_clause_parse(cxx *c, cxx_scope *cs)
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

VPRIV void cxx_class_specifier_parse(cxx *c, bool is_template)
{
	cxx_scope *q = NULL;
	bool dep = false, specialization = false;
	uint32_t name = 0;
	cxx_entity *e = NULL;
	bool is_union = cxx_is_kw(c, CXX_KW_union), is_class = cxx_is_kw(c, CXX_KW_class);
	uint32_t key_loc = cxx_peek(c, 0)->loc, key_pos = c->pos, attrs = c->layout_attributes;

	c->attr_align = 0;	/* (the class head's alignas: this class's alignment, cxx_class_begin) */
	/* an instantiation re-parsing this class's tokens (instantiate.c): it defines that instance */
	cxx_entity *instance = c->instantiation_target, *instance_of = c->instantiation_template;
	cxx_targ spec_args[32];
	uint32_t nspec_args = 0, spec_less = 0;
	bool packed = false, is_final = false;

	c->instantiation_target = c->instantiation_template = NULL;
	cxx_advance(c);	/* class / struct / union */
	c->attr_packed = 0;
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
	packed = c->attr_packed, c->attr_packed = 0;
	if (cxx_is(c, LEX_TOKEN_IDENTIFIER) && cxx_peek(c, 0)->len == 5 && !memcmp(cxx_peek(c, 0)->s, "final", 5)) {
		is_final = true;
		cxx_advance(c);
	}
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
					if (e && !is_template && !c->template_depth && e->ty == CXX_TY_UNKNOWN && !c->tentative) {
						uint32_t tg = cc_tag_new(c->ast, is_union ? TY_UNION : TY_STRUCT, name, key_loc);

						e->ty = QT(c->ast->tags[tg].ty, 0);
						if (e->members && !cxx_tag_scope_get(c, e->ty))
							cxx_tag_scope_set(c, tg, e->members);	/* (its name and where it is: a mangled name may need them first) */
					}
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
					explicit_instance = cxx_instance_named(c, te, spec_args, nspec_args);
					if (!explicit_instance) {
						explicit_instance = calloc(1, sizeof *explicit_instance);
						explicit_instance->name = name, explicit_instance->kind = CXX_ENTITY_TYPE;
						explicit_instance->ty = CXX_TY_UNKNOWN;
					}
					explicit_instance->members = cs;
					/* (its scope is this class's, not the template's: a class derived from it finds
					   the base's type there) */
					cs->owner = explicit_instance;
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
		if (packed)
			cxx_class_packed(c);
		if (is_final && !c->tentative)
			cs->is_final = 1;
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
			{
				/* the member function bodies of the outermost class are parsed once it is complete:
				   after its layout too (sizeof the class, members of this by value) */
				uint32_t late_from = cxx_class_body_parse(c, cs);

				cxx_class_end(c);
				/* (an unnamed class has no entity: its scope by its type, for the members of an
				   anonymous struct or union) */
				if (!name && !c->tentative && c->anonymous_class != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, c->anonymous_class))
					cxx_tag_scope_set(c, AST_TYPE(c->ast, c->anonymous_class)->aux, cs);
				if (late_from != UINT32_MAX)
					cxx_late_parse_all(c, late_from);
			}
			cxx_name_path_pop(c, pl, pv);
		}
		c->scope = outer;
		c->last_type = e, c->last_type_dependent = cs->dependent;
	}
}

VPRIV void cxx_member_declaration_parse(cxx *c, cxx_scope *cs);

/* (returns where the class's deferred bodies start, for the caller to parse after the class is laid out;
   UINT32_MAX: not now -- a nested class, a tentative parse, an instance) */
VPRIV uint32_t cxx_class_body_parse(cxx *c, cxx_scope *cs)
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
		return late_from;	/* the outermost class is complete */
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
	return UINT32_MAX;
}

VPRIV void cxx_enum_specifier_parse(cxx *c)
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
		if (e && e->ty != CXX_TY_UNKNOWN && !c->tentative)
			cxx_tag_home_set(c, e->ty, cxx_declaration_scope(c));
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

		if (!e && !c->template_depth && !c->tentative) {
			/* an unnamed enumeration: its enumerators have its type all the same */
			cxx_entity anon;

			memset(&anon, 0, sizeof anon);
			anon.ty = CXX_TY_UNKNOWN;
			cxx_enum_type(c, &anon, 0, loc, underlying);
			et = anon.ty;
		}

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
