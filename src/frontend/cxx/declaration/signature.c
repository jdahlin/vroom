/* C++ declarations: translation unit, function template signatures, late bodies */
#include "declaration.h"

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
	/* (an instance with a parameter pack: its elements' types; taken here, the body's own parses reuse the fields) */
	qty *pack_types = c->sig_pack_types;
	uint32_t pack_n = c->sig_pack_n;

	c->sig_pack_n = UINT32_MAX;
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
	c->cpack_n = UINT32_MAX;
	uint8_t own_pack = c->sig_own_pack;

	c->sig_own_pack = pack_n != UINT32_MAX;
	cxx_declarator_parse(c, false, &q, &f, NULL);
	c->sig_own_pack = own_pack;
	uint32_t cpack_name = c->cpack_name, cpack_at = c->cpack_at, cpack_n = c->cpack_n;
	qty cpack_types[32];

	if (cpack_n != UINT32_MAX)
		memcpy(cpack_types, c->cpack_types, cpack_n * sizeof *cpack_types);
	t = f ? c->type_out : CXX_TY_UNKNOWN;
	c->sig_is_function = f;
	c->sig_auto = 0;
	if (ret_auto && f) {
		/* (auto f() -> T with a T vcc could not form is not a deduced return type) */
		c->sig_auto = c->decl_return != CXX_TY_UNKNOWN && cxx_type_has_placeholder(c, c->decl_return) && !c->decl_has_trailing;
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
	/* an instance with its pack bound: f(int, Ts&&... ts) is f(int, A&, B&&): the fixed parameters, then one
	   per element (so that it is ranked like any other candidate) */
	if (f && !any_return && pack_n != UINT32_MAX && c->decl_pack_param != UINT32_MAX && c->decl_pack_param + 1 == c->ndecl_params &&
	    c->decl_return != CXX_TY_UNKNOWN && c->ndecl_params - 1 + pack_n <= 32) {
		qty ps[32];
		uint32_t np = 0;
		bool known = true;

		for (uint32_t i = 0; i + 1 < c->ndecl_params; i++)
			known &= (ps[np++] = QUNQ(c->decl_params[i].ty)) != CXX_TY_UNKNOWN && c->decl_params[i].ty != CXX_TY_UNKNOWN;
		for (uint32_t i = 0; i < pack_n; i++)
			ps[np++] = pack_types[i];
		if (known)
			t = cxx_type_function(c, c->decl_return, ps, np, false);
	}
	if (f && t == CXX_TY_UNKNOWN && !any_return && c->decl_return != CXX_TY_UNKNOWN)
		t = cxx_type_function(c, c->decl_return, NULL, 0, true), c->sig_fake = 1;
	/* a trailing requires-clause: its value with the arguments bound (false: the candidate is not viable) */
	c->sig_requires = 1;
	if (f && cxx_is_kw(c, CXX_KW_requires) && any_return)
		cxx_requires_clause_parse(c);
	else if (f && cxx_is_kw(c, CXX_KW_requires)) {
		/* (the clause may name the parameters: requires requires { x.base() != y.base(); }) */
		cxx_scope *ps = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL), *saved = c->scope;

		cxx_params_declare(c, ps, c->decl_params, c->ndecl_params);
		c->scope = ps;
		c->sig_requires = (int8_t)cxx_requires_clause_value(c);
		c->scope = saved;
	}
	if ((c->member_missing || c->einvalid) && !any_return && getenv("VCC_CXX_SIG")) {
		fprintf(stderr, "signature fails: missing %u invalid %u (%u):", c->member_missing, c->einvalid, c->einvalid_at);
		for (uint32_t i = c->einvalid_at > 12 ? c->einvalid_at - 12 : 0; i < c->einvalid_at + 3 && i < c->ntok; i++)
			fprintf(stderr, " %.*s", (int)c->tok[i].len, c->tok[i].s);
		fputc('\n', stderr);
	}
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
			cxx_entity *gen_auto = NULL;	/* an instance with a deduced return type: its body parsed again for code */
			uint32_t body_pos = c->pos, saved_nparams = c->ndecl_params <= 32 ? c->ndecl_params : 0;
			cxx_param saved_params[32];
			bool saved_variadic = c->decl_variadic;

			memcpy(saved_params, c->decl_params, saved_nparams * sizeof *saved_params);
			fs->fn = c->gen_instance;
			cxx_scope_push(c, fs);
			cxx_params_declare(c, fs, c->decl_params, c->ndecl_params);
			/* the parameter pack: a pack of values, one type per element (args... expands with Args...) */
			if (pack_n != UINT32_MAX && c->decl_pack_param < c->ndecl_params && c->decl_params[c->decl_pack_param].name > 1) {
				cxx_entity *pe = cxx_lookup_in(c, fs, c->decl_params[c->decl_pack_param].name, 0);

				if (pe && pe->kind == CXX_ENTITY_VALUE) {
					pe->is_pack = 1, pe->pack_index = -1, pe->ty = CXX_TY_UNKNOWN, pe->npack = pack_n;
					pe->pack = calloc(pack_n ? pack_n : 1, sizeof *pe->pack);
					for (uint32_t i = 0; i < pack_n; i++)
						pe->pack[i].kind = 1, pe->pack[i].t = pack_types[i];
				}
			}
			c->ret_capture = ret_pattern != CXX_TY_UNKNOWN, c->ret_type = CXX_TY_UNKNOWN, c->ret_seen = 0;
			{
				/* code generation: this instantiation of the function template (its signature as parsed
				   with the arguments bound; not one with a deduced return type or a pack, yet) */
				cxx_entity *gi = c->gen_instance;
				bool gen = false;

				c->gen_instance = NULL;	/* (this body's: not a body instantiated inside it) */
				if (gi && ret_pattern != CXX_TY_UNKNOWN && pack_n == UINT32_MAX && cpack_n == UINT32_MAX && !c->tentative &&
				    c->ndecl_params <= 32 && !cxx_is_kw(c, CXX_KW_try) && getenv("VCC_CXX_NOAUTO2") == NULL)
					gen_auto = gi;
				if (gi && c->decl_ndefaults && !gi->defs && c->fn_ndefs == c->decl_ndefaults && !c->tentative) {
					/* its default arguments, for the calls that leave them out: parsed again in this scope
					   (the template's parameters are bound in it) */
					gi->ndefaults = (uint16_t)c->decl_ndefaults;
					gi->defs = malloc(2 * c->fn_ndefs * sizeof *gi->defs);
					memcpy(gi->defs, c->fn_defs, 2 * c->fn_ndefs * sizeof *gi->defs);
					gi->def_scope = fs;
				}
				if (gi && t != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, t) == TY_FUNC && !c->sig_fake && ret_pattern == CXX_TY_UNKNOWN &&
				    pack_n == UINT32_MAX && !cxx_is_kw(c, CXX_KW_try)) {
					cxx_entity *pe = NULL;

					if (cpack_n != UINT32_MAX && cpack_name > 1 && cpack_at + cpack_n <= c->ndecl_params &&
					    (pe = cxx_declare(c, fs, cpack_name, CXX_ENTITY_VALUE))) {
						/* As... a of a class's pack, expanded: a, a pack of the parameters (a... in the body) */
						pe->is_pack = 1, pe->pack_index = -1, pe->ty = CXX_TY_UNKNOWN, pe->npack = cpack_n;
						pe->pack = calloc(cpack_n ? cpack_n : 1, sizeof *pe->pack);
						for (uint32_t i = 0; i < cpack_n; i++)
							pe->pack[i].kind = 1, pe->pack[i].t = cpack_types[i];
						pe->pack_syms = calloc(cpack_n ? cpack_n : 1, sizeof *pe->pack_syms);
						c->gen_pack_entity = pe, c->gen_pack_from = cpack_at, c->gen_pack_n = cpack_n;
					}
					gi->ty = t;
					gen = cxx_gen_function_begin(c, gi, fs, c->decl_params, c->ndecl_params);
					c->gen_pack_entity = NULL;
				} else if (gi && t != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, t) == TY_FUNC && !c->sig_fake && ret_pattern == CXX_TY_UNKNOWN &&
					   pack_n != UINT32_MAX && c->decl_pack_param < c->ndecl_params && !cxx_is_kw(c, CXX_KW_try) &&
					   AST_TYPE(c->ast, t)->n == c->ndecl_params - 1 + pack_n && AST_TYPE(c->ast, t)->n <= 32) {
					/* a function parameter pack: one parameter per element (T... v with v... expanded in the body) */
					cxx_param xp[32];
					uint32_t k = 0, at = c->decl_pack_param;
					cxx_entity *pe = c->decl_params[at].name > 1 ? cxx_lookup_in(c, fs, c->decl_params[at].name, 0) : NULL;

					for (uint32_t i = 0; i < c->ndecl_params; i++)
						if (i == at)
							for (uint32_t j = 0; j < pack_n; j++)
								xp[k].name = 0, xp[k].ty = c->ast->params[AST_TYPE(c->ast, t)->aux + k], k++;
						else
							xp[k++] = c->decl_params[i];
					if (!pe || (pe->is_pack && pe->npack == pack_n)) {	/* (an unnamed pack: its elements unused) */
						if (pe) {
							pe->pack_syms = calloc(pack_n ? pack_n : 1, sizeof *pe->pack_syms);
							c->gen_pack_entity = pe, c->gen_pack_from = at, c->gen_pack_n = pack_n;
						}
						gi->ty = t;
						gen = cxx_gen_function_begin(c, gi, fs, xp, k);
						c->gen_pack_entity = NULL;
					}
				}
				cxx_function_body_parse(c);
				if (gen)
					cxx_gen_function_end(c, gi, c->snode);
			}
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
				bool known = gen_auto && gen_auto->ty == CXX_TY_UNKNOWN && !saved_variadic;

				for (uint32_t i = 0; known && i < saved_nparams; i++)
					known = saved_params[i].ty != CXX_TY_UNKNOWN;
				if (r != CXX_TY_UNKNOWN && known) {
					/* a deduced return type (auto f(T)): the function's type with it, and the body parsed a
					   second time, now with its code (the first only deduced the type) */
					qty ps[32];
					uint32_t np = saved_nparams, end = c->pos;
					cxx_scope *fs2 = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL);
					bool gen2;

					for (uint32_t i = 0; i < np; i++)
						ps[i] = saved_params[i].ty;
					t = cxx_type_function(c, r, ps, np, false);
					gen_auto->ty = t;
					fs2->fn = gen_auto;
					c->pos = body_pos;
					cxx_scope_push(c, fs2);
					cxx_params_declare(c, fs2, saved_params, saved_nparams);
					gen2 = cxx_gen_function_begin(c, gen_auto, fs2, saved_params, saved_nparams);
					cxx_function_body_parse(c);
					if (gen2)
						cxx_gen_function_end(c, gen_auto, c->snode);
					c->scope = outer;
					if (c->pos != end)
						c->pos = end;	/* (the same tokens: it ends where the first parse did) */
				} else if (r != CXX_TY_UNKNOWN)
					t = cxx_type_function(c, r, NULL, 0, true), c->sig_fake = 1;	/* (callers read the return type only) */
			}
		} else if (c->gen_instance && t != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, t) == TY_FUNC && !c->sig_fake &&
			   c->gen_instance->ty == CXX_TY_UNKNOWN && !c->tentative) {
			/* declared only here (a member template defined outside its class): its type all the same, so that
			   calls are built; its body: another declaration's, or another TU's */
			c->gen_instance->ty = t;
			c->gen_instance = NULL;
		}
	}
	return t;
}

/* one deferred member function body (an instance's, on use): as cxx_late_parse_all does */
VPRIV void cxx_late_parse_one(cxx *c, const cxx_late *l)
{
	cxx_scope *fs;

	cxx_late_deduce(c, l);

	c->pos = l->begin;
	c->this_const = l->fn && l->fn->fn_const;
	c->template_depth = l->template_depth;
	c->this_type = l->scope->kind == CXX_SCOPE_CLASS && l->scope->owner && l->scope->owner->ty ? l->scope->owner->ty : CXX_TY_UNKNOWN;
	c->scope = l->scope;
	c->class_depth = 0;
	fs = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL);
	fs->fn = l->fn;
	cxx_scope_push(c, fs);
	cxx_params_declare(c, fs, l->params, l->nparams);
	{
		/* code generation: a member function defined in its class (a class template instance's too) */
		bool gen = l->what == 0 && l->fn && !l->template_depth && l->scope->kind == CXX_SCOPE_CLASS && !l->scope->dependent &&
			   l->scope->owner && !cxx_is_kw(c, CXX_KW_try);

		if (gen) {
			l->fn->fn_inline = 1;
			cxx_late_pack_declare(c, l, fs);
			gen = cxx_gen_function_begin(c, l->fn, fs, l->params, l->nparams);
			c->gen_pack_entity = NULL;
		}
		cxx_function_body_parse(c);
		if (gen)
			cxx_gen_function_end(c, l->fn, c->snode);
	}
	if (c->pos != l->end)
		cxx_fail(c, "member function body ended early");
}
