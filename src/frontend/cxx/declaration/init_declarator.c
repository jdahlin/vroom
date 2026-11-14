/* C++ declarations: init-declarators (a declaration's declarators and their initializers or bodies) */
#include "declaration.h"

VPRIV bool cxx_is_function_body_start(cxx *c)
{
	return cxx_is(c, LEX_TOKEN_LEFT_BRACE) || cxx_is(c, LEX_TOKEN_COLON) || cxx_is_kw(c, CXX_KW_try);
}

/* does the declarator at [start, pos) name operator= ? */
VPRIV bool cxx_declarator_is_assignment(cxx *c, uint32_t start)
{
	for (uint32_t i = start; i + 1 < c->pos; i++)
		if (c->tok[i].kw == CXX_KW_operator)
			return c->tok[i + 1].kind == LEX_TOKEN_EQUAL;
	return false;
}

/* the declarators after decl-specifiers, to the ; (or a function body) */
/*
 * auto [a, b] = e: its variable v is made (named var); each name is v's next public member (a class whose
 * members are its own and named, in order: std::pair's first and second, an aggregate's), an lvalue referring
 * to it.  Tuple-like classes (get<i>) and arrays: the names stay untyped.
 */
static void cxx_structured_bindings_declare(cxx *c, uint32_t var, uint32_t loc)
{
	uint32_t n = c->sb_n;
	cxx_entity *v = cxx_lookup_in(c, cxx_declaration_scope(c), var, 0);
	qty t = v ? v->ty : CXX_TY_UNKNOWN, ct;
	cxx_scope *cls;
	const tag *r;
	uint32_t k = 0;

	c->sb_n = 0;
	if (t == CXX_TY_UNKNOWN)
		return;
	ct = AST_TYPE_KIND(c->ast, t) == TY_PTR && AST_TYPE(c->ast, t)->ext ? AST_TYPE(c->ast, t)->base : t;
	if (!cc_type_is_record(c->ast, ct) || AST_TYPE_KIND(c->ast, ct) == TY_ARRAY || !(cls = cxx_type_class_scope(c, QUNQ(ct))))
		return;
	r = &c->ast->tags[AST_TYPE(c->ast, QUNQ(ct))->aux];
	for (uint32_t i = 0; i < r->nfield && k < n; i++) {
		cxx_entity *e;

		if (!r->f[i].name)
			continue;	/* (a base's bytes) */
		e = cxx_lookup_in(c, cxx_declaration_scope(c), c->sb_names[k], 0);
		while (e && e->kind != CXX_ENTITY_VALUE)
			e = e->next;
		if (e) {
			qty ft = r->f[i].ty;

			/* (a reference member: the name is what it refers to, as the member's own type says) */
			if (AST_TYPE_KIND(c->ast, ft) == TY_PTR && AST_TYPE(c->ast, ft)->ext)
				e->ty = AST_TYPE(c->ast, ft)->base;
			else
				e->ty = QT(QI(ft), QQ(ft) | (QQ(ct) & (Q_CONST | Q_VOLATILE)));
			cxx_gen_bind_name(c, e, v->sym, r->f[i].name, c->sb_locs[k]);
		}
		k++;
	}
	(void)loc;
}

VPRIV void cxx_init_declarators_parse(cxx *c, int flags, int context, cxx_scope *cs)
{
	cxx_entity *type_of_typedef = c->last_type;
	uint8_t typedef_dependent = c->last_type_dependent;
	qty base = c->ds_type;	/* (each declarator's parse may change c->ds_type) */
	/* auto x = init: the declarator is parsed over auto's placeholder, deduced from the initializer */
	/* (in a class: static constexpr auto value = ..., and member functions auto f(): a non-static data
	   member cannot be auto) */
	bool is_auto = c->ds_auto && base == CXX_TY_UNKNOWN && !c->template_depth && !c->tentative && !(flags & (DS_TYPEDEF | DS_FRIEND));
	int auto_quals = c->ds_quals;
	/* a data member of the class being built: a class body, not a template's or a function's */
	bool member = context == CXX_CONTEXT_CLASS && c->build && !(flags & (DS_TYPEDEF | DS_STATIC | DS_FRIEND));
	bool global = context == CXX_CONTEXT_NAMESPACE && !c->template_depth && !(flags & (DS_TYPEDEF | DS_FRIEND));
	uint32_t ds_from = c->decl_start;	/* (its decl-specifiers: an abi_tag attribute among them) */

	for (;;) {
		bool q, f;
		cxx_scope *qs;
		uint32_t name;
		uint32_t start = c->pos;

		if (context == CXX_CONTEXT_CLASS && cxx_is(c, LEX_TOKEN_COLON)) {	/* an unnamed bit-field */
			int64_t w;

			cxx_advance(c);
			cxx_constant_expression_parse(c);
			w = c->econst && c->evalue >= 0 && c->evalue <= 64 ? c->evalue : -1;
			if (member)
				cxx_class_field(c, 0, base, c->tok[start].loc, w < 0 ? -2 : (int)w);
			goto next;
		}
		c->type_in = is_auto ? QT(QI(cxx_auto_type(c)), auto_quals | ((flags & DS_CONSTEXPR) ? Q_CONST : 0)) : base;
		name = cxx_declarator_parse(c, false, &q, &f, &qs);
		if (q && context == CXX_CONTEXT_BLOCK && c->tentative)
			cxx_fail(c, "a block declares no qualified name");	/* (X<T>::f(a, b); is a call, not a declaration) */
		qty t = c->type_out;
		/* a constexpr variable is const (not a reference; an array's elements are) */
		if ((flags & DS_CONSTEXPR) && !f && t != CXX_TY_UNKNOWN && !c->tentative && AST_TYPE_KIND(c->ast, t) != TY_ARRAY &&
		    AST_TYPE_KIND(c->ast, t) != TY_FUNC && !(AST_TYPE_KIND(c->ast, t) == TY_PTR && AST_TYPE(c->ast, t)->ext))
			t = QT(QI(t), QQ(t) | Q_CONST);
		qty auto_pattern = CXX_TY_UNKNOWN;
		cxx_entity *auto_e = NULL;
		uint32_t name_loc = c->tok[start].loc;

		qty fn_ret_pattern = CXX_TY_UNKNOWN;	/* auto f(...): the declared return type, a pattern */
		bool fn_variadic = false;

		if (is_auto && t != CXX_TY_UNKNOWN && cxx_type_has_placeholder(c, t)) {
			if (!f)
				auto_pattern = t;	/* (a function's auto return type: its body tells, cxx_late_deduce) */
			else if (AST_TYPE_KIND(c->ast, t) == TY_FUNC && cxx_type_has_placeholder(c, AST_TYPE(c->ast, t)->base) &&
				 !c->decl_has_trailing)
				fn_ret_pattern = AST_TYPE(c->ast, t)->base, fn_variadic = (AST_TYPE(c->ast, t)->flags & TF_VARIADIC) != 0;
			t = CXX_TY_UNKNOWN;	/* until the initializer is seen (it may not name the variable) */
		}
		/* an object of class type defined here (not in a template): its constructor and destructor are used */
		cxx_entity *const_e = NULL;	/* a const integer whose initializer is evaluated */
		cxx_entity *fn_template = NULL;	/* the function template this declarator declares (is it a definition?) */
		cxx_entity *decl_fn = NULL;	/* the function it declares (= delete, = default follow the declarator) */
		uint32_t gen_local = 0;		/* code generation: the local variable's symbol */
		cxx_entity *unsized_e = NULL;	/* ... an array of unknown bound, made after its initializer */
		bool unsized_static = false;
		cxx_entity *gen_var = NULL;	/* ... a variable at namespace scope */
		uint32_t gen_after_declarator = c->pos;
		bool gen_declared = false, auto_gen = false;
		bool object = !f && !(flags & (DS_TYPEDEF | DS_FRIEND)) && context != CXX_CONTEXT_CLASS && !c->tentative &&
			      !c->template_depth && t != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, t);
		if (!c->tentative) {
			c->stats.declarations++;
			c->stats.declarations_typed += t != CXX_TY_UNKNOWN;
		}
		if (f && (flags & DS_VIRTUAL) && context == CXX_CONTEXT_CLASS)
			cxx_class_virtual(c);
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
			if (e)
				e->fn_explicit = (flags & DS_EXPLICIT) ? 1 : (flags & DS_EXPLICIT_MAYBE) ? 2 : 0, e->fn_noexcept = c->decl_noexcept;
			if (e && !e->abi_tag && ds_from < c->pos)
				e->abi_tag = cxx_abi_tag_scan(c, ds_from, c->pos);
			if (e && ds_from < c->pos && cxx_word_scan(c, ds_from, c->pos, "__exclude_from_explicit_instantiation__"))
				e->no_explicit_inst = 1;	/* (_LIBCPP_HIDE_FROM_ABI on a constructor: this TU's own) */
			if (e && !tmpl && dtor && !e->vslot)
				cxx_vtable_declare(c, cxx_declaration_scope(c), e, (flags & DS_VIRTUAL) != 0);	/* (virtual ~X(), or overriding a base's) */
			if (e && !tmpl)
				decl_fn = e;
			if (e && !tmpl) {
				e->ty = known ? cxx_type_function(c, cc_type_basic(TY_VOID), ps, c->ndecl_params, false) : CXX_TY_UNKNOWN;
				e->ndefaults = (uint16_t)c->decl_ndefaults, cxx_defaults_record(c, e);
				c->last_fn = e;	/* (its body: parsed when used) */
			} else if (e && !dtor && c->template_depth == 1 && c->scope == c->ftmpl_scope) {
				e->ndefaults = (uint16_t)c->decl_ndefaults;
				e->ftmpl = cxx_ftemplate_new(c, c->scope, c->scope->parent, c->ftmpl_begin, cs);
				cxx_ftemplate_requires(e->ftmpl, c->ftmpl_req);
				fn_template = e;
			}
		}
		/* what it declares (not a constructor or destructor: they have no name to look up) */
		{
		uint32_t sm_init = !f && (cxx_is(c, LEX_TOKEN_LEFT_PAREN) || cxx_is(c, LEX_TOKEN_LEFT_BRACE) || cxx_is(c, LEX_TOKEN_EQUAL)) ? c->pos : 0;

		if (f && q && qs && qs->kind == CXX_SCOPE_CLASS && c->pos && c->tok[c->pos - 1].kind == LEX_TOKEN_RIGHT_PAREN && !c->tentative) {
			/* T X::m(e): parsed as a function's declarator; when the class's m is a variable, its initializer */
			bool var = false;

			for (cxx_entity *m = cxx_lookup_local(qs, name); m; m = m->next)
				var |= m->kind == CXX_ENTITY_VALUE && !m->field_of && m->ty != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, m->ty) != TY_FUNC;
			if (var) {
				int depth = 0;

				for (uint32_t i = c->pos - 1; i > start; i--) {
					if (c->tok[i].kind == LEX_TOKEN_RIGHT_PAREN)
						depth++;
					else if (c->tok[i].kind == LEX_TOKEN_LEFT_PAREN && !--depth) {
						sm_init = i;
						break;
					}
				}
			}
		}
		if (sm_init && q && qs && qs->kind == CXX_SCOPE_CLASS && !c->tentative && !c->template_depth) {
			uint32_t keep = c->pos;

			c->pos = sm_init;
			/* a static member's definition: its initializer, on the member the class declares (and this
			   declaration's entity), for cxx_gen_static_member_value */
			for (cxx_entity *m = cxx_lookup_local(qs, name); m; m = m->next)
				if (m->kind == CXX_ENTITY_VALUE && !m->field_of && !m->nsdmi_pos && m->ty != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, m->ty) != TY_FUNC)
					m->nsdmi_pos = c->pos, m->nsdmi_scope = c->scope;
			c->pos = keep;
		}
		if (q && qs && qs->kind == CXX_SCOPE_CLASS && !f && global && !c->tentative && !(flags & DS_TYPEDEF) && !c->explicit_specialization &&
		    (cxx_is(c, LEX_TOKEN_EQUAL) || cxx_is(c, LEX_TOKEN_LEFT_BRACE) || cxx_is(c, LEX_TOKEN_LEFT_PAREN) || sm_init == 0)) {
			/* T X::m = ...;: the definition of the static data member's variable (made where the class declares it) */
			for (cxx_entity *m = cxx_lookup_local(qs, name); m; m = m->next)
				if (m->kind == CXX_ENTITY_VALUE && !m->field_of && m->sym && c->ast->syms[m->sym].kind == S_VAR &&
				    !(m->nsdmi_pos && m->nsdmi_pos < c->pos - 1 && !cxx_is(c, LEX_TOKEN_EQUAL) && !cxx_is(c, LEX_TOKEN_LEFT_BRACE) &&
				      !cxx_is(c, LEX_TOKEN_LEFT_PAREN))) {
					/* (T X::m; of a member initialized in its class: the definition has that initializer, not
					   built here: left to the uses, which take the value) */
					c->ast->syms[m->sym].sc = SC_NONE;
					gen_var = m;
					break;
				}
		}
		}
		if (name > 1 && !q && !(cs && cs->owner && cs->owner->name == name)) {
			int kind = (flags & DS_TYPEDEF) ? CXX_ENTITY_TYPE :
				   c->template_depth && c->scope->kind == CXX_SCOPE_TEMPLATE ? (f ? CXX_ENTITY_FUNCTION_TEMPLATE : CXX_ENTITY_VARIABLE_TEMPLATE)
											   : CXX_ENTITY_VALUE;
			cxx_entity *e = (flags & DS_FRIEND) ? NULL : cxx_declare(c, cxx_declaration_scope(c), name, kind);
			bool is_friend = false;

			/* a friend function declared in a class: not a member, not found by ordinary lookup; a call
			   with an argument of the class finds it (kept in the class's friends) */
			if ((flags & DS_FRIEND) && f && context == CXX_CONTEXT_CLASS && cs && !c->tentative &&
			    (kind == CXX_ENTITY_VALUE || kind == CXX_ENTITY_FUNCTION_TEMPLATE)) {
				if (!cs->friends)
					cs->friends = cxx_scope_new(c, CXX_SCOPE_BLOCK, cs, NULL);
				e = cxx_declare(c, cs->friends, name, kind);
				is_friend = e != NULL;
				/* (it is the enclosing namespace's function; defined in the class: inline) */
				if (e && kind == CXX_ENTITY_VALUE && !e->home)
					e->home = cxx_enclosing_namespace(c), e->fn_inline = 1;
			}
			/* a function template (one level of parameters): its signature, for deduction at calls */
			if (e && kind == CXX_ENTITY_FUNCTION_TEMPLATE && !e->ftmpl && c->template_depth == 1 && c->scope == c->ftmpl_scope &&
			    !c->tentative) {
				cxx_tparams_inherit_defaults(c, c->scope, cxx_declaration_scope(c), name);
				e->ftmpl = cxx_ftemplate_new(c, c->scope, c->scope->parent, c->ftmpl_begin, NULL);
				cxx_ftemplate_requires(e->ftmpl, c->ftmpl_req);
				if (is_friend)
					cxx_ftemplate_friend(e->ftmpl);
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
			if (e && kind == CXX_ENTITY_VALUE && f && fn_ret_pattern != CXX_TY_UNKNOWN && !c->tentative)
				e->has_ret_pattern = 1, e->ret_pattern = fn_ret_pattern, e->ret_variadic = fn_variadic;
			if (e && kind == CXX_ENTITY_VALUE && auto_pattern != CXX_TY_UNKNOWN)
				auto_e = e;
			/* code generation: a local variable of the function body being built */
			if (e && kind == CXX_ENTITY_VALUE && !f && context == CXX_CONTEXT_BLOCK && cxx_gen_on(c)) {
				if (auto_e && !(flags & (DS_STATIC | DS_CONSTEXPR)))
					auto_gen = true;	/* (auto: its symbol once the initializer gave its type) */
				else if (!auto_e && (flags & (DS_STATIC | DS_CONSTEXPR)) && !cxx_tokens_have_kw(c, ds_from, c->pos, CXX_KW_thread_local) && t != CXX_TY_UNKNOWN &&
					 !(AST_TYPE_KIND(c->ast, t) == TY_ARRAY && !AST_TYPE(c->ast, t)->n) &&
					 (!cc_type_is_record(c->ast, t) || !cxx_gen_needs_construction_public(c, t)))
					/* static int n = 3; constexpr char t[] = ...: in static storage, initialized by a constant (C's
					   function-scope static); one with a dynamic initializer (a guard) is not built yet */
					gen_local = e->sym = cxx_gen_local_static(c, name, t, name_loc);
				else if (!auto_e && t != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, t) == TY_ARRAY && !AST_TYPE(c->ast, t)->n &&
					 cxx_is(c, LEX_TOKEN_EQUAL) && !cxx_tokens_have_kw(c, ds_from, c->pos, CXX_KW_thread_local))
					unsized_e = e, unsized_static = (flags & (DS_STATIC | DS_CONSTEXPR)) != 0;	/* T a[] = {...}: its symbol once the initializer gave the bound */
				else if (auto_e || (flags & (DS_STATIC | DS_CONSTEXPR)))
					cxx_gen_fail(c);
				else
					gen_local = e->sym = cxx_gen_local(c, name, t, name_loc);
			}
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
			c->last_fn = e && kind == CXX_ENTITY_VALUE && f && context == CXX_CONTEXT_CLASS ? e : NULL;	/* (a friend's body too) */
			if (e && f)
				e->fn_const = c->decl_const, e->fn_noexcept = c->decl_noexcept;
			if (e && f && kind == CXX_ENTITY_VALUE)
				decl_fn = e;
			if (e && f && context == CXX_CONTEXT_CLASS && !is_friend)
				e->member_of = cxx_declaration_scope(c);
			if (e && f && context == CXX_CONTEXT_CLASS && (flags & DS_STATIC))
				e->fn_static = 1;
			if (e && f && (kind == CXX_ENTITY_VALUE || kind == CXX_ENTITY_FUNCTION_TEMPLATE) && !e->abi_tag && ds_from < c->pos)
				e->abi_tag = cxx_abi_tag_scan(c, ds_from, c->pos);	/* (mangled after its name: B <len> <tag>) */
			if (e && f && (kind == CXX_ENTITY_VALUE || kind == CXX_ENTITY_FUNCTION_TEMPLATE) && ds_from < c->pos &&
			    cxx_word_scan(c, ds_from, c->pos, "__exclude_from_explicit_instantiation__"))
				e->no_explicit_inst = 1;	/* (_LIBCPP_HIDE_FROM_ABI: always this TU's own) */
			if (e && f && kind == CXX_ENTITY_VALUE && (flags & (DS_INLINE | DS_CONSTEXPR)))
				e->fn_inline = 1;	/* (an inline or constexpr function: its definition is weak, emitted where used) */
			if (e && f && kind == CXX_ENTITY_VALUE && context == CXX_CONTEXT_CLASS && !is_friend && !(flags & DS_STATIC))
				cxx_vtable_declare(c, cxx_declaration_scope(c), e, (flags & DS_VIRTUAL) != 0);
			if (e && f && kind == CXX_ENTITY_VALUE && context != CXX_CONTEXT_CLASS && !is_friend && (flags & DS_STATIC))
				e->fn_internal = 1;
			if (e && !f && kind == CXX_ENTITY_VALUE && context == CXX_CONTEXT_CLASS && !(flags & (DS_STATIC | DS_TYPEDEF | DS_FRIEND)))
				e->field_of = cxx_declaration_scope(c), e->is_mutable = (flags & DS_MUTABLE) != 0;
			if (e && f && c->decl_ndefaults > e->ndefaults)
				e->ndefaults = (uint16_t)c->decl_ndefaults, cxx_defaults_record(c, e);	/* (redeclarations may add defaults) */
			if (e && global && kind == CXX_ENTITY_VALUE && !e->sym)
				e->sym = cxx_symbol(c, f ? S_FUNC : S_VAR, name, t, name_loc), e->c_linkage = c->c_linkage > 0,
				e->global_ns = cxx_declaration_scope(c) == c->global, e->home = cxx_declaration_scope(c);
			if (e && global && kind == CXX_ENTITY_VALUE && !f && !(flags & DS_EXTERN_SEEN))
				gen_var = e;
			if (e && global && kind == CXX_ENTITY_VALUE && e->sym && (flags & DS_STATIC) && !c->tentative)
				c->ast->syms[e->sym].sc = SC_STATIC;	/* (internal linkage) */
			if (e && !f && kind == CXX_ENTITY_VALUE && context == CXX_CONTEXT_CLASS && (flags & DS_STATIC) && !(flags & (DS_TYPEDEF | DS_FRIEND)) &&
			    !e->sym && !c->tentative && !c->template_depth && !c->explicit_specialization && t != CXX_TY_UNKNOWN) {
				/* a static data member: a variable of its own, named in its class (defined where the class's
				   definition outside it is, or inline) */
				e->sym = cxx_symbol(c, S_VAR, name, t, name_loc), e->home = cxx_declaration_scope(c);
				if (e->sym)
					c->ast->syms[e->sym].sc = SC_EXTERN;
				e->mangled_var = 1;
				cxx_gen_variable_name(c, e, false);
			} else if (e && global && kind == CXX_ENTITY_VALUE && !f && e->sym && !c->tentative && e->home == c->global && (flags & DS_INLINE) &&
				   !e->mangled_var) {
				attrs at;	/* an inline variable at the global scope: weak (its plain name) */

				e->mangled_var = 1;
				memset(&at, 0, sizeof at);
				at.weak = 1;
				cc_attribute_apply_symbol(c->ast, &c->ast->syms[e->sym], &at);
				c->ast->syms[e->sym].flags |= SF_LINKONCE;
			} else if (e && global && kind == CXX_ENTITY_VALUE && !f && e->sym && !c->tentative && e->home != c->global && !e->c_linkage &&
				   c->ast->syms[e->sym].ty == t && !e->mangled_var) {
				/* a namespace's variable: internal linkage written L (static, or const and not extern) */
				bool internal = (flags & DS_STATIC) || cxx_scope_in_unnamed_namespace(c, e->home) ||
						(t != CXX_TY_UNKNOWN && (QQ(t) & Q_CONST) && !(flags & (DS_EXTERN_SEEN | DS_INLINE)));

				e->mangled_var = 1;
				if (c->explicit_specialization) {
					e->sym = 0;	/* (a variable template's explicit specialization: its name is the template-id's, not mangled yet) */
					goto named;
				}
				if ((flags & DS_INLINE) && e->sym) {
					/* an inline variable: weak, every TU that uses it defines it */
					attrs at;

					memset(&at, 0, sizeof at);
					at.weak = 1;
					cc_attribute_apply_symbol(c->ast, &c->ast->syms[e->sym], &at);
					c->ast->syms[e->sym].flags |= SF_LINKONCE;
				}
				if (internal && e->sym)
					c->ast->syms[e->sym].sc = SC_STATIC, c->ast->syms[e->sym].flags &= ~SF_GLOBAL;
				cxx_gen_variable_name(c, e, internal);
			named:;
			}
			if (e && kind == CXX_ENTITY_VALUE && !f && ((flags & DS_CONSTEXPR) || (t != CXX_TY_UNKNOWN && (QQ(t) & Q_CONST))) &&
			    cxx_is(c, LEX_TOKEN_EQUAL) && context == CXX_CONTEXT_CLASS && (flags & DS_STATIC) && !c->tentative && !c->template_depth)
				e->nsdmi_pos = c->pos, e->nsdmi_scope = c->scope;	/* (a static constant member's initializer: cxx_gen_static_member_value) */
			if (e && kind == CXX_ENTITY_VALUE && !f && ((flags & DS_CONSTEXPR) || (t != CXX_TY_UNKNOWN && (QQ(t) & Q_CONST))) &&
			    cxx_is(c, LEX_TOKEN_EQUAL)) {
				int64_t v;

				cxx_advance(c);
				v = cxx_literal_value(c);
				c->pos--;
				if (v >= 0 && !c->tentative)
					e->value = v, e->has_value = 1;
				else if (!c->tentative && !c->template_depth && t != CXX_TY_UNKNOWN &&
					 ((AST_TYPE_KIND(c->ast, t) >= TY_BOOL && AST_TYPE_KIND(c->ast, t) <= TY_ULLONG) || AST_TYPE_KIND(c->ast, t) == TY_ENUM))
					const_e = e;	/* (its initializer: a constant expression, evaluated below; an enumeration's
							   too: constexpr memory_order memory_order_relaxed = memory_order::relaxed) */
			}
		}
		if (member && !f && name > 1 && cs && !c->tentative && !c->template_depth) {
			/* (its type, for the class's implicit copy, move and assignment) */
			cs->field_types = realloc(cs->field_types, (cs->nfield_types + 1) * sizeof *cs->field_types);
			cs->field_types[cs->nfield_types++] = t;
		}
		if (member && !f && name > 1) {
			/* a data member: its bit-field width (a constant expression) comes after the declarator */
			int w = -1;

			if (cxx_is(c, LEX_TOKEN_COLON)) {
				cxx_advance(c);
				cxx_constant_expression_parse(c);
				w = c->econst && c->evalue >= 0 && c->evalue <= 64 ? (int)c->evalue : -2;
			}
			cxx_class_field(c, name, t, name_loc, w);
			if (cxx_is(c, LEX_TOKEN_EQUAL) || cxx_is(c, LEX_TOKEN_LEFT_BRACE)) {
				cxx_class_member_traits(c, CXX_MEMBER_INITIALIZER);	/* (a default member initializer) */
				cxx_entity *fe = c->tentative ? NULL : cxx_lookup_local(cxx_declaration_scope(c), name);

				while (fe && !(fe->kind == CXX_ENTITY_VALUE && fe->field_of))
					fe = fe->next;
				if (fe) {
					/* (parsed again where a constructor applies it: cxx_gen_nsdmi) */
					fe->has_nsdmi = 1;
					if (!c->template_depth)
						fe->nsdmi_pos = c->pos, fe->nsdmi_scope = c->scope;
				}
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
		if (f && cxx_is_function_body_start(c) && !(context == CXX_CONTEXT_BLOCK) && fn_template)
			cxx_ftemplate_defined(fn_template->ftmpl);
		if (f && cxx_is_function_body_start(c) && context == CXX_CONTEXT_NAMESPACE && q && qs && qs->kind == CXX_SCOPE_CLASS &&
		    qs->owner && !qs->owner->tmpl && !qs->dependent && c->template_depth == 1 && c->scope == c->ftmpl_scope && name > 1 &&
		    !c->tentative && !c->instantiating && !c->class_depth) {
			/* template<...> R X::f(...) { }: a member template of a class (not of a class template) defined outside
			   it: its declarations in the class instantiate this body (its scope the class's) */
			struct cxx_ftemplate *df = cxx_ftemplate_new(c, c->scope, qs, c->ftmpl_begin, NULL);

			cxx_ftemplate_requires(df, c->ftmpl_req);
			cxx_ftemplate_defined(df);
			cxx_ftemplate_join_declarations(c, df, cxx_lookup_in(c, qs, name, 0), c->ndecl_params, c->decl_const);
		}
		if (f && cxx_is_function_body_start(c) && context == CXX_CONTEXT_NAMESPACE && q && qs && qs->owner && qs->owner->tmpl &&
		    c->template_depth == 2 && c->scope == c->ftmpl_scope && c->scope->parent && c->scope->parent->kind == CXX_SCOPE_TEMPLATE &&
		    !c->tentative && !c->instantiating && !c->class_depth)
			/* template<...> template<...> R X<...>::f(...) { }: a member template defined outside its class template */
			cxx_template_member_template_definition(c, qs->owner, name, c->scope->parent, c->scope, c->scope->parent->parent,
								c->ftmpl_begin, c->ndecl_params, c->decl_const);
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
				/* only X<T, U>::f with the definition's own parameters in order: X<bool, A>::f is a member of a
				   partial specialization, whose parameters are not the primary's */
				{
					bool primary = false;
					uint32_t i = start;

					while (i < c->pos && c->tok[i].kind != LEX_TOKEN_LESS)
						i++;
					if (i < c->pos) {
						uint32_t k = 0;

						primary = true;
						for (i++; i < c->pos && primary; i++) {
							if (c->tok[i].kind == LEX_TOKEN_GREATER)
								break;
							if (c->tok[i].kind == LEX_TOKEN_COMMA || c->tok[i].kind == LEX_TOKEN_ELLIPSIS)
								continue;
							primary = c->tok[i].kind == LEX_TOKEN_IDENTIFIER && k < c->scope->ntparams &&
								  c->tok[i].id == c->scope->tparams[k].name;
							k++;
						}
						primary &= k == c->scope->ntparams;
					}
					/* (a partial specialization's members: kept apart, for its instances) */
					cxx_template_member_definition(c, qs->owner, mname, c->scope, c->scope->parent, c->ftmpl_begin,
								       c->ndecl_params, c->decl_const, !primary);
				}
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
				{
					/* code generation: a function at namespace scope that is no template and no member */
					bool gen;

					/* (a member defined outside its class: the member its class declares) */
					if (context == CXX_CONTEXT_NAMESPACE && qs && qs->kind == CXX_SCOPE_CLASS && !qs->dependent && !decl_fn && name > 1 &&
					    !c->tentative && !c->template_depth && qs->owner && !qs->owner->instance)
					{
						/* (X::X and X::~X: the constructor or destructor, under its own name) */
						uint32_t mname = name;

						if (qs->owner->name == name) {
							mname = cxx_ctor_id();
							for (uint32_t k = start; k < start + 8 && k < c->pos && c->tok[k].kind != LEX_TOKEN_LEFT_PAREN; k++)
								if (c->tok[k].kind == LEX_TOKEN_TILDE)
									mname = cxx_dtor_id();
						}
						decl_fn = cxx_member_function_find(c, qs, mname, c->decl_params, c->ndecl_params, c->decl_const);
					}
					/* (an instance's member defined outside the class template: the member being instantiated) */
					if (context == CXX_CONTEXT_NAMESPACE && qs && c->ooc_target && c->ooc_target->member_of == qs && !c->tentative &&
					    !c->template_depth && (!decl_fn || decl_fn->member_of != qs))
						decl_fn = c->ooc_target;
					c->ooc_target = NULL;
					if (decl_fn && (flags & (DS_INLINE | DS_CONSTEXPR)))
						decl_fn->fn_inline = 1;	/* (inline X::f() {...} outside its class: weak, as clang's) */
					fs->fn = decl_fn;
					{
						/* R X<A...>::f(As... a) of a class pack, its parameters expanded: a, a pack of those
						   parameters (a... in the body: std::function's operator()) */
						cxx_entity *pe = NULL;

						if (context == CXX_CONTEXT_NAMESPACE && decl_fn && c->cpack_n != UINT32_MAX && c->cpack_name > 1 &&
						    c->cpack_at + c->cpack_n <= c->ndecl_params && (pe = cxx_declare(c, fs, c->cpack_name, CXX_ENTITY_VALUE))) {
							uint32_t pn = c->cpack_n;

							pe->is_pack = 1, pe->pack_index = -1, pe->ty = CXX_TY_UNKNOWN, pe->npack = pn;
							pe->pack = calloc(pn ? pn : 1, sizeof *pe->pack);
							for (uint32_t j = 0; j < pn; j++)
								pe->pack[j].kind = 1, pe->pack[j].t = c->decl_params[c->cpack_at + j].ty;
							pe->pack_syms = calloc(pn ? pn : 1, sizeof *pe->pack_syms);
							c->gen_pack_entity = pe, c->gen_pack_from = c->cpack_at, c->gen_pack_n = pn;
						}
						gen = context == CXX_CONTEXT_NAMESPACE && decl_fn && (!qs || decl_fn->member_of == qs) && !cxx_is_kw(c, CXX_KW_try) &&
						      cxx_gen_function_begin(c, decl_fn, fs, c->decl_params, c->ndecl_params);
						c->gen_pack_entity = NULL;
					}

					/* VCC_CXX_MANGLE=file: each function defined at namespace scope by its mangled name
					   ("?name": not mangled yet), for bench/cxx-mangle.py to compare with clang's */
					if (c->mangle_fd >= 0 && context == CXX_CONTEXT_NAMESPACE && decl_fn && !c->tentative && !c->template_depth &&
					    !c->instantiating && !decl_fn->c_linkage && (decl_fn->home || decl_fn->member_of)) {
						char mn[2048];
						const vident *fv = &vintern_ents[decl_fn->name];

						if (cxx_mangle_function(c, decl_fn, decl_fn->member_of ? decl_fn->member_of : decl_fn->home, decl_fn->ty,
									decl_fn->member_of && decl_fn->fn_const, 1, mn, sizeof mn))
							dprintf(c->mangle_fd, "%s\n", mn);
						else
							dprintf(c->mangle_fd, "?%.*s\n", (int)fv->len, fv->s);
					}

					/* (a member function's body is parsed later, for this function: what came after the
					   declarator, a noexcept(...) that completed a class, may have declared others since) */
					if (context == CXX_CONTEXT_CLASS && decl_fn)
						c->last_fn = decl_fn;
					cxx_function_body_parse(c);
					if (gen)
						cxx_gen_function_end(c, decl_fn, c->snode);
				}
				c->scope = outer;
			}
			return;
		}
		if (cxx_accept(c, LEX_TOKEN_EQUAL)) {
			if (f && (cxx_is_kw(c, CXX_KW_default) || cxx_is_kw(c, CXX_KW_delete) ||
				  (cxx_is(c, LEX_TOKEN_NUMBER) && cxx_peek(c, 0)->len == 1))) {
				if (decl_fn && !c->tentative)
					decl_fn->fn_deleted = cxx_is_kw(c, CXX_KW_delete), decl_fn->fn_defaulted = cxx_is_kw(c, CXX_KW_default);
				if (!c->tentative && !c->template_depth && !c->instantiating && q && qs && qs->kind == CXX_SCOPE_CLASS &&
				    cxx_is_kw(c, CXX_KW_default) && !decl_fn) {
					/* X::~X() = default; (a key function: the vtable here) */
					cxx_entity *d = cxx_lookup_local(qs, cxx_dtor_id());

					while (d && (d->kind != CXX_ENTITY_VALUE || d->member_of != qs))
						d = d->next;
					for (uint32_t i = start; d && i < c->pos; i++)
						if (c->tok[i].kind == LEX_TOKEN_TILDE) {	/* (a destructor's definition, not another member's) */
							d->fn_defaulted = 1;
							cxx_gen_defaulted_destructor(c, d);
							break;
						}
				}
				cxx_advance(c);
				if (cxx_is(c, LEX_TOKEN_LEFT_PAREN))	/* = delete("reason") */
					cxx_skip_balanced(c);
			} else if (object && !cxx_is(c, LEX_TOKEN_LEFT_BRACE)) {
				bool gobj = global && gen_var && !(flags & DS_CONSTEXPR) && cxx_gen_global_begin(c, gen_var);

				cxx_initializer_parse(c);	/* copy-initialization: a constructor from the value */
				{
					uint32_t in = c->enode;
					qty it = c->etype;
					uint8_t ilv = c->elval;

					if (it != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, it) == TY_ARRAY)	/* (an array, "text": a pointer to its first element) */
						it = cc_type_pointer(c->ast, AST_TYPE(c->ast, it)->base), ilv = 0;
					else if (it != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, it) == TY_FUNC)	/* (a function: a pointer to it) */
						it = cc_type_pointer(c->ast, it), ilv = 0;
					cxx_ctor_use(c, t, &it, &ilv, 1, false);
					if (gen_local) {	/* code generation: the constructor's call, or a copy */
						cxx_gen_construct_local(c, gen_local, t, &in, 1, name_loc);
						gen_declared = true;
					}
					if (gobj) {	/* ... of an object at namespace scope: when the program starts */
						cxx_gen_global_object(c, gen_var, t, &in, 1, name_loc);
						gen_declared = true;
					}
				}
			} else if (object) {
				/* T x = { ... }: copy-list-initialization, as T x{ ... } (its explicit constructors: not told apart) */
				uint32_t an[CXX_MAX_ARGS], n, abr[CXX_MAX_ARGS];
				bool gobj = global && gen_var && !(flags & DS_CONSTEXPR) && cxx_gen_global_begin(c, gen_var);

				cxx_braced_arguments_parse(c);
				n = c->ncargs;
				memcpy(an, c->carg_nodes, sizeof an);
				memcpy(abr, c->carg_brace, sizeof abr);
				cxx_ctor_use(c, t, c->cargs, c->cargs_lval, c->ncargs, true);
				if (gen_local) {
					c->gen_agg_brace = n <= CXX_MAX_ARGS ? abr : NULL;	/* (an aggregate's elements that are braced lists) */
					cxx_gen_construct_local(c, gen_local, t, an, n, name_loc);
					c->gen_agg_brace = NULL;
					gen_declared = true;
				}
				if (gobj) {	/* ... at namespace scope: a constructor when the program starts, or the aggregate's values */
					if (c->ctor_found && c->ctor_picked)
						cxx_gen_global_object(c, gen_var, t, an, n, name_loc);
					else
						cxx_gen_global_end(c, gen_var, cxx_gen_record_initializer(c, t, an, n <= CXX_MAX_ARGS ? n : UINT32_MAX, name_loc), true);
					gen_declared = true;
				}
			} else {
				bool braced = cxx_is(c, LEX_TOKEN_LEFT_BRACE);
				/* code generation: a variable defined at namespace scope, its initializer's node */
				/* (a constexpr one too when it is not an integer, whose uses are its value: a floating constant or an
				   array has an object) */
				bool cx_object = (flags & DS_CONSTEXPR) && t != CXX_TY_UNKNOWN && !cc_type_is_record(c->ast, t) &&
						 !cc_type_is_integer(c->ast, t) && AST_TYPE_KIND(c->ast, t) != TY_ENUM;
				bool gen_global = global && gen_var && !f && (!(flags & DS_CONSTEXPR) || cx_object) && !auto_e && cxx_gen_global_begin(c, gen_var);

				cxx_initializer_parse(c);
				if (unsized_e && cxx_gen_on(c)) {
					/* the bound: the list's elements, or the string literal's length */
					uint64_t n = braced ? (c->brace_n != UINT32_MAX ? c->brace_n : 0) :
						     c->enode && AST_TYPE_KIND(c->ast, AST_NODE_AT(c->ast, c->enode)->ty) == TY_ARRAY ?
						     AST_TYPE(c->ast, AST_NODE_AT(c->ast, c->enode)->ty)->n : 0;

					if (n) {
						t = cc_type_array(c->ast, AST_TYPE(c->ast, t)->base, n, 0);
						unsized_e->ty = t;
						gen_local = unsized_e->sym = unsized_static ? cxx_gen_local_static(c, name, t, name_loc) : cxx_gen_local(c, name, t, name_loc);
					} else
						cxx_gen_fail(c);
				}
				if (gen_global) {
					cxx_gen_global_end(c, gen_var, !braced ? c->enode : AST_TYPE_KIND(c->ast, t) == TY_ARRAY ? cxx_gen_array_initializer(c, t, name_loc) :
							   cxx_gen_record_initializer(c, t, c->brace_nodes, c->brace_n, name_loc), true);
					gen_declared = true;
				}
				if (gen_local) {
					cxx_gen_local_declare(c, gen_local, braced ? cxx_gen_array_initializer(c, t, name_loc) : c->enode, true, name_loc);
					gen_declared = true;
				}
				/* auto x = expr: its type from the initializer's */
				if (auto_e && !braced && c->etype != CXX_TY_UNKNOWN) {
					bool k = c->econst;
					int64_t v = c->evalue;
					uint32_t in = c->enode;
					qty ity = c->etype;
					uint8_t ilv = c->elval;

					auto_e->ty = cxx_auto_deduce(c, auto_pattern, c->etype, c->elval);
					if (auto_gen && cxx_gen_on(c) && auto_e->ty != CXX_TY_UNKNOWN && in) {
						/* code generation: the local, now that its type is known */
						qty at = auto_e->ty;
						uint32_t ls = auto_e->sym = cxx_gen_local(c, name, at, name_loc);

						if (ls && cc_type_is_record(c->ast, at) && AST_TYPE_KIND(c->ast, at) != TY_ARRAY) {
							cxx_ctor_use(c, at, &ity, &ilv, 1, false);
							cxx_gen_construct_local(c, ls, at, &in, 1, name_loc);
						} else if (ls)
							cxx_gen_local_declare(c, ls, in, true, name_loc);
						gen_declared = true;
					}
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
				uint32_t an[CXX_MAX_ARGS], n, abr[CXX_MAX_ARGS];
				bool gobj = global && gen_var && !(flags & DS_CONSTEXPR) && cxx_gen_global_begin(c, gen_var);

				cxx_braced_arguments_parse(c);
				n = c->ncargs;
				memcpy(an, c->carg_nodes, sizeof an);
				memcpy(abr, c->carg_brace, sizeof abr);
				cxx_ctor_use(c, t, c->cargs, c->cargs_lval, c->ncargs, true);
				if (gen_local) {
					c->gen_agg_brace = n <= CXX_MAX_ARGS ? abr : NULL;	/* (an aggregate's elements that are braced lists) */
					cxx_gen_construct_local(c, gen_local, t, an, n, name_loc);
					c->gen_agg_brace = NULL;
					gen_declared = true;
				}
				if (gobj) {
					if (c->ctor_found && c->ctor_picked)
						cxx_gen_global_object(c, gen_var, t, an, n, name_loc);
					else	/* (a list for an aggregate of scalars: its values; else not built yet) */
						cxx_gen_global_end(c, gen_var, cxx_gen_record_initializer(c, t, an, n <= CXX_MAX_ARGS ? n : UINT32_MAX, name_loc), true);
					gen_declared = true;
				}
			} else
				cxx_braced_init_list_parse(c);
		} else if (!f && cxx_is(c, LEX_TOKEN_LEFT_PAREN)) {
			if (object) {
				uint32_t an[CXX_MAX_ARGS], n;
				bool gobj = global && gen_var && !(flags & DS_CONSTEXPR) && cxx_gen_global_begin(c, gen_var);

				uint32_t pbr[CXX_MAX_ARGS];

				cxx_call_arguments_parse(c);	/* direct-initialization */
				n = c->ncargs;
				memcpy(an, c->carg_nodes, sizeof an);
				memcpy(pbr, c->carg_brace, sizeof pbr);
				cxx_ctor_use(c, t, c->cargs, c->cargs_lval, c->ncargs, false);
				if (gen_local) {
					memcpy(c->gen_call_brace, pbr, sizeof pbr);	/* (T x({...}, v): the list a temporary of its parameter's class) */
					cxx_gen_construct_local(c, gen_local, t, an, n, name_loc);
					memset(c->gen_call_brace, 0, sizeof c->gen_call_brace);
					gen_declared = true;
				}
				if (gobj) {
					cxx_gen_global_object(c, gen_var, t, an, n, name_loc);
					gen_declared = true;
				}
			} else if (gen_local && !c->tentative) {
				/* T x(e) of a scalar or a reference: as T x = e */
				cxx_call_arguments_parse(c);
				if (c->ncargs == 1 && c->carg_nodes[0]) {
					cxx_gen_local_declare(c, gen_local, c->carg_nodes[0], true, name_loc);
					gen_declared = true;
				}
			} else
				cxx_expression_list_parse(c);
		} else if (object) {
			cxx_ctor_use(c, t, NULL, NULL, 0, false);	/* default-initialization */
			if (gen_local) {
				cxx_gen_construct_local(c, gen_local, t, NULL, 0, name_loc);
				gen_declared = true;
			}
		}
	next:
		if (auto_gen && !gen_declared)
			cxx_gen_fail(c);	/* (auto with a list, or no type: not built yet) */
		/* code generation: a local with no initializer; one with a form of initializer not built yet */
		if (gen_local && !gen_declared) {
			if (c->pos == gen_after_declarator)
				cxx_gen_local_declare(c, gen_local, 0, false, c->tok[c->pos ? c->pos - 1 : 0].loc);
			else
				cxx_gen_fail(c);
		}
		/* ... a variable at namespace scope with no initializer: zero-initialized */
		if (gen_var && !gen_declared && c->pos == gen_after_declarator && !(flags & DS_CONSTEXPR) && cxx_gen_global_begin(c, gen_var))
			cxx_gen_global_end(c, gen_var, 0, false);
		if (c->sb_n && name == c->sb_var && name)
			cxx_structured_bindings_declare(c, name, name_loc);
		if (!cxx_accept(c, LEX_TOKEN_COMMA))
			break;
	}
	cxx_expect(c, LEX_TOKEN_SEMICOLON);
	(void)cs;
}
