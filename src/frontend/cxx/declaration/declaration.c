/* C++ declarations: simple, member and top-level declarations, extern templates */
#include "declaration.h"

VPRIV void cxx_simple_declaration_parse(cxx *c, int context, cxx_scope *cs)
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
		/* ... whose data members are members of the enclosing class by name (s.x for struct { int x; }) */
		if (context == CXX_CONTEXT_CLASS && cs && !(flags & (DS_TYPEDEF | DS_STATIC | DS_FRIEND)) && c->defined_unnamed_class &&
		    c->ds_type != CXX_TY_UNKNOWN && !c->tentative) {
			cxx_scope *as = cxx_tag_scope_get(c, c->ds_type);

			for (uint32_t i = 0; as && as != cs && i < as->nslots; i++)
				for (cxx_entity *m = as->slots[i]; m; m = m->next)
					if (m->kind == CXX_ENTITY_VALUE && m->field_of && !cxx_lookup_local(cs, m->name)) {
						cxx_entity *ne = cxx_declare(c, cs, m->name, CXX_ENTITY_VALUE);

						if (ne)
							ne->ty = m->ty, ne->field_of = cs, ne->dependent = m->dependent;
					}
		}
		cxx_advance(c);
		return;
	}
	if (!flags && !(cxx_is(c, LEX_TOKEN_TILDE) || cxx_is(c, LEX_TOKEN_IDENTIFIER) || cxx_is(c, LEX_TOKEN_COLON_COLON) ||
			cxx_is_kw(c, CXX_KW_operator)))
		cxx_fail(c, "expected a declaration");
	cxx_init_declarators_parse(c, flags, context, cs);
}

VPRIV void cxx_member_declaration_parse1(cxx *c, cxx_scope *cs);

VPRIV void cxx_member_declaration_parse(cxx *c, cxx_scope *cs)
{
	/* (this member may be inside a class that is itself a [[no_unique_address]] member) */
	uint8_t outer_nua = c->attr_nua;
	uint32_t outer_align = c->attr_align;

	c->attr_nua = 0, c->attr_align = 0;
	cxx_member_declaration_parse1(c, cs);
	c->attr_nua = outer_nua, c->attr_align = outer_align;
}

VPRIV void cxx_member_declaration_parse1(cxx *c, cxx_scope *cs)
{
	int kw;
	uint32_t member_attrs = c->layout_attributes;	/* (leading attributes count too) */

	c->decl_start = c->pos;	/* (its leading attributes too: an abi_tag) */
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
			cxx_class_skip(c, CXX_SKIP_ATTRIBUTE);	/* (aligned, alignas, ms_struct: not built yet) */
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

	c->decl_start = c->pos;	/* (its leading attributes too: an abi_tag) */
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
			const cxx_token *ls = cxx_peek(c, 1);
			uint32_t outer = c->c_linkage;

			c->c_linkage = ls->len == 3 && ls->s[1] == 'C' ? outer + 1 : 0;	/* (extern "C++" inside extern "C": C++ again) */
			cxx_advance(c);
			cxx_advance(c);
			if (cxx_accept(c, LEX_TOKEN_LEFT_BRACE)) {
				while (!cxx_is(c, LEX_TOKEN_RIGHT_BRACE) && !cxx_is(c, LEX_TOKEN_EOF))
					cxx_declaration_parse(c, context);
				cxx_expect(c, LEX_TOKEN_RIGHT_BRACE);
			} else
				cxx_declaration_parse(c, context);
			c->c_linkage = outer;
			return;
		}
		if (cxx_peek(c, 1)->kw == CXX_KW_template) {	/* extern template class X<int>; */
			cxx_advance(c);
			if (cxx_peek(c, 1)->kw != CXX_KW_class && cxx_peek(c, 1)->kw != CXX_KW_struct)
				cxx_extern_member_record(c);	/* extern template R X<args>::f(params); */
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

/*
 * extern template R X<args>::member(params); at 'template': the member of the class instance is defined
 * elsewhere (libc++'s dylib: basic_string<char>'s copy constructor), kept on the instance's scope as its
 * name and number of parameters.  Nothing is consumed.
 */
VPRIV void cxx_extern_member_record(cxx *c)
{
	uint32_t save = c->pos, save_undo = c->nundo, open = 0, depth = 0, end, np = 0, name;
	cxx_scope *q;
	bool dep;

	/* the parameter list: the first ( at depth 0 that is no attribute's */
	for (uint32_t i = c->pos + 1; i < c->ntok && c->tok[i].kind != LEX_TOKEN_SEMICOLON; i++) {
		int k = c->tok[i].kind;

		if (c->tok[i].kind == LEX_TOKEN_IDENTIFIER && c->tok[i].len == 13 && !memcmp(c->tok[i].s, "__attribute__", 13)) {
			int d = 0;

			for (i++; i < c->ntok; i++) {
				if (c->tok[i].kind == LEX_TOKEN_LEFT_PAREN)
					d++;
				else if (c->tok[i].kind == LEX_TOKEN_RIGHT_PAREN && !--d)
					break;
			}
			continue;
		}
		if (k == LEX_TOKEN_LESS)
			depth++;
		else if (k == LEX_TOKEN_GREATER && depth)
			depth--;
		else if (k == LEX_TOKEN_LEFT_PAREN && !depth) {
			open = i;
			break;
		}
	}
	if (!open || open < 3 || (c->tok[open - 2].kind != LEX_TOKEN_COLON_COLON && !(c->tok[open - 3].kind == LEX_TOKEN_COLON_COLON &&
	    c->tok[open - 2].kw == CXX_KW_operator)))
		return;
	{
		/* the member's name: an identifier (the class's own: its constructor), or operator= and the like */
		uint32_t at = c->tok[open - 2].kind == LEX_TOKEN_COLON_COLON ? open - 1 : open - 2, qend = at - 1, qb;
		int d = 0;

		if (c->tok[at].kw == CXX_KW_operator) {
			char nm[64];

			snprintf(nm, sizeof nm, "operator%.*s", (int)c->tok[at + 1].len, c->tok[at + 1].s);
			name = vintern_cstr(nm);
		} else if (c->tok[at].kind == LEX_TOKEN_IDENTIFIER)
			name = c->tok[at].id;
		else
			return;
		/* back over X<args> before the :: to its first name (and std:: before it) */
		qb = qend;
		if (qb && c->tok[qb - 1].kind == LEX_TOKEN_GREATER) {
			for (qb--; qb > save; qb--) {
				if (c->tok[qb].kind == LEX_TOKEN_GREATER)
					d++;
				else if (c->tok[qb].kind == LEX_TOKEN_LESS && !--d)
					break;
			}
		}
		if (!qb || c->tok[qb - 1].kind != LEX_TOKEN_IDENTIFIER)
			return;
		qb--;
		while (qb >= 2 && c->tok[qb - 1].kind == LEX_TOKEN_COLON_COLON && c->tok[qb - 2].kind == LEX_TOKEN_IDENTIFIER)
			qb -= 2;
		c->pos = qb;
		cxx_nested_name_parse(c, &q, &dep);
		if (!q || dep || q->kind != CXX_SCOPE_CLASS || c->pos != at || !q->owner) {
			cxx_rewind(c, save, save_undo);
			return;
		}
		if (c->tok[at].kind == LEX_TOKEN_IDENTIFIER && c->tok[at].id == q->owner->name)
			name = cxx_ctor_id();
	}
	/* the parameters: commas at depth 0 (none, or (void): 0) */
	depth = 0, end = open + 1;
	if (c->tok[open + 1].kind != LEX_TOKEN_RIGHT_PAREN && !(c->tok[open + 1].kw == CXX_KW_void && c->tok[open + 2].kind == LEX_TOKEN_RIGHT_PAREN))
		np = 1;
	for (; end < c->ntok; end++) {
		int k = c->tok[end].kind;

		if (k == LEX_TOKEN_LEFT_PAREN || k == LEX_TOKEN_LESS || k == LEX_TOKEN_LEFT_SQUARE)
			depth++;
		else if ((k == LEX_TOKEN_RIGHT_PAREN || k == LEX_TOKEN_GREATER || k == LEX_TOKEN_RIGHT_SQUARE)) {
			if (!depth)
				break;
			depth--;
		} else if (k == LEX_TOKEN_COMMA && !depth)
			np++;
	}
	{
		cxx_entity *of;
		const cxx_targ *ta;
		uint32_t nta, pk;
		cxx_scope *home;
		struct cxx_extern_member *m;

		if (!q->owner->ty || !cxx_instance_template(c, q->owner->ty, &of, &ta, &nta, &pk, &home) || nta > 4) {
			cxx_rewind(c, save, save_undo);
			return;
		}
		c->externs = realloc(c->externs, (c->nexterns + 1) * sizeof *c->externs);
		m = &c->externs[c->nexterns++];
		memset(m, 0, sizeof *m);
		m->of = of, m->nargs = nta, m->name = name, m->nparams = np;
		memcpy(m->args, ta, nta * sizeof *ta);
	}
	cxx_rewind(c, save, save_undo);
}

/* is the member function f of a class instance defined elsewhere (an extern template declaration names it, and
   it is not excluded from explicit instantiation) */
VPRIV bool cxx_member_is_extern(cxx *c, const cxx_entity *f)
{
	const cxx_scope *cls = f ? f->member_of : NULL;
	cxx_entity *of;
	const cxx_targ *ta;
	uint32_t nta, pk;
	cxx_scope *home;

	if (!c->nexterns || !cls || !cls->owner || !cls->owner->ty || f->no_explicit_inst || f->inst_ftmpl || f->ty == CXX_TY_UNKNOWN ||
	    AST_TYPE_KIND(c->ast, f->ty) != TY_FUNC || !cxx_instance_template(c, cls->owner->ty, &of, &ta, &nta, &pk, &home))
		return false;	/* (a member template's instance: an extern template of the class names none) */
	for (uint32_t i = 0; i < c->nexterns; i++) {
		const struct cxx_extern_member *m = &c->externs[i];
		bool same = m->of == of && m->name == f->name && m->nparams == AST_TYPE(c->ast, f->ty)->n && m->nargs <= nta;

		/* (basic_string<char> as the extern template wrote it: its arguments a prefix of the instance's, the
		   rest defaulted) */
		for (uint32_t k = 0; same && k < m->nargs; k++) {
			same = m->args[k].kind == ta[k].kind;
			if (same && m->args[k].kind == 1 && m->args[k].t != ta[k].t) {
				/* (one class may have two instances here: char_traits<char> as two types; their names agree) */
				char x[512], y[512];

				same = cxx_mangle_type(c, m->args[k].t, x, sizeof x) && cxx_mangle_type(c, ta[k].t, y, sizeof y) && !strcmp(x, y);
			} else if (same && m->args[k].kind != 1)
				same = m->args[k].v == ta[k].v;
		}
		if (same)
			return true;
	}
	return false;
}
