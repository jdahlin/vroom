/* C++ expressions: id-expressions, qualified names */
#include "expression.h"

VPRIV bool cxx_lookahead_fn(cxx *c, void (*fn)(cxx *))
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

/* ---- names in expressions ---------------------------------------------------------------- */

VPRIV void cxx_template_arguments_then_call(cxx *c)
{
	cxx_template_arguments_parse(c);
	if (!cxx_is(c, LEX_TOKEN_LEFT_PAREN))
		cxx_fail(c, "not a template-id call");
}

VPRIV cxx_entity *cxx_entity_kind_find(cxx_entity *e, int kind)
{
	for (; e; e = e->next)
		if (e->kind == kind)
			return e;
	return NULL;
}

VPRIV bool cxx_entity_has_template(cxx_entity *e)
{
	for (; e; e = e->next)
		if (e->kind == CXX_ENTITY_TEMPLATE || e->kind == CXX_ENTITY_FUNCTION_TEMPLATE ||
		    e->kind == CXX_ENTITY_VARIABLE_TEMPLATE || e->kind == CXX_ENTITY_CONCEPT)
			return true;
	return false;
}

/* operator, conversion and destructor names after a nested-name-specifier */
VPRIV void cxx_unqualified_id_rest_parse(cxx *c)
{
	if (cxx_accept_kw(c, CXX_KW_operator)) {
		int k = cxx_kind(c);

		if (cxx_is_kw(c, CXX_KW_new) || cxx_is_kw(c, CXX_KW_delete)) {
			cxx_advance(c);
			if (cxx_accept(c, LEX_TOKEN_LEFT_SQUARE))
				cxx_expect(c, LEX_TOKEN_RIGHT_SQUARE);
		} else if (k == LEX_TOKEN_LEFT_PAREN) {
			cxx_advance(c);
			cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
		} else if (k == LEX_TOKEN_LEFT_SQUARE) {
			cxx_advance(c);
			cxx_expect(c, LEX_TOKEN_RIGHT_SQUARE);
		} else if (k == LEX_TOKEN_STRING) {
			cxx_advance(c);
			cxx_accept(c, LEX_TOKEN_IDENTIFIER);
		} else if ((k >= LEX_TOKEN_PERIOD && k <= LEX_TOKEN_COMMA && k != LEX_TOKEN_SEMICOLON && k != LEX_TOKEN_COLON &&
			    k != LEX_TOKEN_QUESTION && k != LEX_TOKEN_COLON_COLON && k != LEX_TOKEN_PERIOD && k != LEX_TOKEN_ELLIPSIS) ||
			   k == CXX_TOKEN_ARROW_STAR || k == CXX_TOKEN_SPACESHIP) {
			uint32_t n;

			cxx_operator_at(c, &n);	/* operator>> and friends: joined pieces */
			while (n--)
				cxx_advance(c);
		} else
			cxx_type_id_parse(c);	/* conversion function */
		if (cxx_is(c, LEX_TOKEN_LESS) && cxx_lookahead_fn(c, cxx_template_arguments_then_call))
			cxx_template_arguments_parse(c);
		return;
	}
	if (cxx_accept(c, LEX_TOKEN_TILDE)) {
		if (cxx_is_kw(c, CXX_KW_decltype)) {
			cxx_advance(c);
			cxx_skip_balanced(c);
			return;
		}
		cxx_expect(c, LEX_TOKEN_IDENTIFIER);
		if (cxx_is(c, LEX_TOKEN_LESS))
			cxx_template_arguments_parse(c);
		return;
	}
	cxx_fail(c, "expected an identifier");
}

/* is the name just read the member in x.m or p->m (x.B::m, p->template m too)?  Not a member of this then */
VPRIV bool cxx_after_member_access(cxx *c)
{
	uint32_t p = c->pos ? c->pos - 1 : 0;

	while (p >= 2 && c->tok[p - 1].kind == LEX_TOKEN_COLON_COLON && c->tok[p - 2].kind == LEX_TOKEN_IDENTIFIER)
		p -= 2;
	if (p >= 1 && c->tok[p - 1].kind == LEX_TOKEN_IDENTIFIER && c->tok[p - 1].len == 8 && !memcmp(c->tok[p - 1].s, "template", 8))
		p--;
	if (p >= 1 && c->tok[p - 1].kind == LEX_TOKEN_TILDE)
		p--;
	return p >= 1 && (c->tok[p - 1].kind == LEX_TOKEN_PERIOD || c->tok[p - 1].kind == LEX_TOKEN_ARROW);
}

/* what a name used as a value is: a variable's type (an lvalue), or a set of functions for a call */
VPRIV void cxx_name_type(cxx *c, cxx_entity *e)
{
	cxx_entity *v = NULL;

	for (cxx_entity *x = e; x; x = x->next) {
		if (x->kind == CXX_ENTITY_FUNCTION_TEMPLATE || (x->kind == CXX_ENTITY_VALUE && x->ty != CXX_TY_UNKNOWN &&
								  AST_TYPE_KIND(c->ast, x->ty) == TY_FUNC)) {
			cxx_et(c, CXX_TY_UNKNOWN, false);
			c->ecallee = e;	/* functions: the call picks one */
			c->fn_set_seen = e;	/* (or a cast's target type: static_cast<F *>(&f)) */
			bool one = x == e && x->kind == CXX_ENTITY_VALUE;


			for (cxx_entity *y = x->next; y && one; y = y->next)	/* (redeclarations: the same function) */
				one = y->kind != CXX_ENTITY_VALUE || y->ty == CXX_TY_UNKNOWN || AST_TYPE_KIND(c->ast, y->ty) != TY_FUNC ? y->kind != CXX_ENTITY_FUNCTION_TEMPLATE && y->kind != CXX_ENTITY_VALUE : y->ty == x->ty;
			if (!cxx_is(c, LEX_TOKEN_LEFT_PAREN) && !cxx_is(c, LEX_TOKEN_LESS) && one &&
			    (!x->member_of || x->fn_static) && !c->template_depth) {
				/* one function, not called: the function, an lvalue (it decays to a pointer to it) */
				cxx_et(c, x->ty, true);
				cxx_gen_function_value(c, x, c->tok[c->pos ? c->pos - 1 : 0].loc);
			}
			return;
		}
		if (x->kind == CXX_ENTITY_VALUE && !v)
			v = x;
	}
	cxx_et(c, v ? v->ty : CXX_TY_UNKNOWN, true);
	c->edecl_name = 1;
	if (v && v->has_value && !v->placeholder)
		c->evalue = v->value, c->econst = 1;	/* a constant (a const or constexpr integer, a value parameter) */
	/* a constant static data member with its initializer in the class and no definition seen: its value (the
	   symbol may be defined nowhere) */
	bool by_value = v && v->mangled_var && v->nsdmi_pos && v->sym && v->ty != CXX_TY_UNKNOWN && (QQ(v->ty) & Q_CONST) &&
			c->ast->syms[v->sym].sc == SC_EXTERN;

	if (v && !by_value)
		cxx_gen_variable(c, v, c->tok[c->pos ? c->pos - 1 : 0].loc);
	if (v && v->has_value && !v->placeholder && !c->enode && c->gen && v->ty != CXX_TY_UNKNOWN &&
	    (AST_TYPE_KIND(c->ast, v->ty) == TY_ENUM || (AST_TYPE_KIND(c->ast, v->ty) >= TY_BOOL && AST_TYPE_KIND(c->ast, v->ty) <= TY_ULLONG))) {
		/* an enumerator (or a constant with no object, or one whose object was not made: a static constexpr
		   member): its value, of the enumeration's type (errc::x converts to error_condition by the
		   constructor template for enumerations; C's rules make it an integer where one is wanted) */
		qty vt = QUNQ(v->ty);

		cxx_gen_integer(c, v->value, vt, c->tok[c->pos ? c->pos - 1 : 0].loc);
	}
	if (v && !v->field_of && v->nsdmi_pos && !c->enode && cxx_gen_on(c) &&
	    (!v->sym || by_value || (v->ty != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, v->ty))))
		c->enode = cxx_gen_static_member_value(c, v, c->tok[c->pos ? c->pos - 1 : 0].loc);	/* (a static constant member: its initializer) */
	if (v && v->field_of && c->gen_this && !c->enode && cxx_gen_on(c) && !cxx_after_member_access(c)) {	/* a data member of this */
		uint32_t loc = c->tok[c->pos ? c->pos - 1 : 0].loc;

		cxx_gen_this(c, loc);
		cxx_gen_member(c, c->enode, true, v->name, loc);
	}
}

/*
 * id-expression (possibly qualified, a template-id), or a type followed by ( or { (a
 * functional cast).  Called at an identifier, :: or a keyword that may start one.
 */
VPRIV void cxx_id_expression_parse(cxx *c)
{
	cxx_scope *q;
	bool dep, template_kw;
	uint32_t save = c->pos;
	cxx_entity *e;
	cxx_token *t;

	uint32_t ename = 0;

	cxx_et(c, CXX_TY_UNKNOWN, false);
	cxx_nested_name_parse(c, &q, &dep);
	template_kw = cxx_accept_kw(c, CXX_KW_template);
	t = cxx_peek(c, 0);
	if (t->kind != LEX_TOKEN_IDENTIFIER || (t->kw && t->kw != CXX_KW_operator)) {
		if (t->kind == LEX_TOKEN_TILDE || t->kw == CXX_KW_operator) {
			cxx_unqualified_id_rest_parse(c);
			cxx_et(c, CXX_TY_UNKNOWN, false);
			return;
		}
		cxx_fail(c, "expected an expression");
	}
	if (t->kw == CXX_KW_operator && q && !dep && q->kind == CXX_SCOPE_CLASS && q->owner && q->owner->ty &&
	    cxx_peek(c, 1)->kind == LEX_TOKEN_EQUAL && cxx_peek(c, 2)->kind == LEX_TOKEN_LEFT_PAREN &&
	    !cxx_lookup_in(c, q, vintern_cstr("operator="), 0)) {
		/* B::operator=(x) of a class that declares none: its implicit assignment (json_base_class_t::operator=) */
		qty bt = q->owner->ty;
		uint32_t n;

		cxx_advance(c), cxx_advance(c);
		cxx_call_arguments_parse(c);
		n = c->ncargs == 1 ? c->carg_nodes[0] : 0;
		cxx_et(c, bt, true);
		if (!c->tentative && !c->template_depth)
			cxx_gen_base_assign(c, bt, n, t->loc);
		return;
	}
	if (t->kw == CXX_KW_operator) {
		/* operator<<(a, b), written as a call: the operator functions by that name (and, at the call,
		   those in the arguments' namespaces); no member, no built-in */
		uint32_t oid = 0;

		if (!dep && !template_kw) {
			uint32_t at = c->pos, on;

			c->pos++;
			if ((cxx_kind(c) >= LEX_TOKEN_LEFT_SQUARE && cxx_kind(c) < LEX_TOKEN_HASH && !cxx_is(c, LEX_TOKEN_LEFT_PAREN) &&
			     !cxx_is(c, LEX_TOKEN_LEFT_SQUARE)) || cxx_is(c, CXX_TOKEN_SPACESHIP) || cxx_is(c, CXX_TOKEN_ARROW_STAR)) {
				cxx_operator_at(c, &on);
				oid = cxx_operator_id_at(c, on);
			} else if (cxx_is(c, LEX_TOKEN_LEFT_PAREN) && cxx_peek(c, 1)->kind == LEX_TOKEN_RIGHT_PAREN)
				oid = vintern_cstr("operator()");	/* (in a member function: this->operator()(...)) */
			else if (cxx_is(c, LEX_TOKEN_LEFT_SQUARE) && cxx_peek(c, 1)->kind == LEX_TOKEN_RIGHT_SQUARE)
				oid = vintern_cstr("operator[]");
			else if (cxx_is_kw(c, CXX_KW_new) || cxx_is_kw(c, CXX_KW_delete)) {
				bool arr = cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_SQUARE && cxx_peek(c, 2)->kind == LEX_TOKEN_RIGHT_SQUARE;

				oid = vintern_cstr(cxx_is_kw(c, CXX_KW_new) ? (arr ? "operator new[]" : "operator new") :
						   (arr ? "operator delete[]" : "operator delete"));
			}
			c->pos = at;
		}
		cxx_unqualified_id_rest_parse(c);
		cxx_et(c, CXX_TY_UNKNOWN, false);
		if (oid > 1 && !c->tentative) {
			cxx_entity *oe = q ? cxx_lookup_in(c, q, oid, 0) : cxx_lookup(c, oid);

			if (oe && oe->kind != CXX_ENTITY_DEPENDENT)
				cxx_name_type(c, oe);
			if (!q)
				c->ename = oid, c->ename_missing = !oe;
		}
		return;
	}
	if (dep)
		e = cxx_entity_dependent();
	else if (q) {
		e = cxx_lookup_in(c, q, t->id, 0);
		if (!e && q->owner)
			for (uint32_t i = 0; i < q->owner->nspecs && !e; i++)
				e = cxx_lookup_in(c, q->owner->specs[i], t->id, 0);
	} else
		e = cxx_lookup(c, t->id);
	cxx_advance(c);
	if (cxx_is(c, LEX_TOKEN_LESS)) {
		bool is_template = template_kw || cxx_entity_has_template(e) ||
				   (!q && !dep && cxx_builtin_kind(t->id) == CXX_BUILTIN_TEMPLATE);

		/* C++20: an unqualified name lookup does not find (or finds a function) followed by <...>( */
		if (!is_template && !q && !dep && (!e || cxx_entity_kind_find(e, CXX_ENTITY_VALUE)) &&
		    cxx_lookahead_fn(c, cxx_template_arguments_then_call))
			is_template = true;
		if (is_template) {
			cxx_template_arguments_parse(c);
			cxx_et(c, CXX_TY_UNKNOWN, false);
			/* a variable template: its value for these arguments */
			if (!dep && e && !c->tentative && !c->template_depth) {
				cxx_entity *ve = NULL;
				int64_t v;

				for (cxx_entity *x = e; x && !ve; x = x->next)
					if (x->kind == CXX_ENTITY_VARIABLE_TEMPLATE && x->ftmpl)
						ve = x;
				if (ve && cxx_vtemplate_value(c, ve, c->targs, c->ntargs, &v)) {
					if (ve->ty != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, ve->ty) >= TY_BOOL && AST_TYPE_KIND(c->ast, ve->ty) <= TY_ULLONG)
						cxx_ec(c, QUNQ(ve->ty), v);
					else
						c->evalue = v, c->econst = 1;	/* (its type: the parameter's, not known here) */
					return;
				}
			}
			/* Concept<args>: a bool, its value when the arguments are known */
			if (!dep && e && cxx_entity_kind_find(e, CXX_ENTITY_CONCEPT)) {
				int v = c->template_depth || c->tentative ? -1 :
					cxx_concept_value(c, cxx_entity_kind_find(e, CXX_ENTITY_CONCEPT), c->targs, c->ntargs);

				if (v >= 0)
					cxx_ec(c, cc_type_basic(TY_BOOL), v);
				else
					cxx_et(c, cc_type_basic(TY_BOOL), false);
				return;
			}
			/* f<args>: a call takes the explicit arguments (a function template's) */
			if (!dep && e && cxx_entity_kind_find(e, CXX_ENTITY_FUNCTION_TEMPLATE) && !c->tentative) {
				if (c->ntargs > c->cxargs)
					c->xargs = realloc(c->xargs, (c->cxargs = c->ntargs + 8) * sizeof *c->xargs);
				memcpy(c->xargs, c->targs, c->ntargs * sizeof *c->xargs);
				c->nxargs = c->ntargs, c->xargs_set = 1, c->ecallee = e;
			}
			return;
		}
	}
	if (!q && !dep && !template_kw)
		ename = t->id;
	/* X::y with X a complete class that has no y: ill-formed (a substitution fails) */
	if (q && !dep && !e && q->kind == CXX_SCOPE_CLASS && !q->dependent && !q->has_dependent_base && !c->template_depth)
		CXX_INVALID(c);
	if (!dep && e)
		cxx_name_type(c, e);
	else if (!e && !q && t->len > 10 && !memcmp(t->s, "__builtin_", 10) && !c->tentative &&
		 ((t->len == 18 && !memcmp(t->s + 10, "va_start", 8)) || (t->len == 16 && !memcmp(t->s + 10, "va_end", 6)) ||
		  (t->len == 17 && !memcmp(t->s + 10, "va_copy", 7)) || (t->len == 21 && !memcmp(t->s + 10, "unreachable", 11)) ||
		  (t->len == 14 && !memcmp(t->s + 10, "trap", 4)) || (t->len == 18 && !memcmp(t->s + 10, "prefetch", 8))))
		cxx_et(c, cxx_type_function(c, cc_type_basic(TY_VOID), NULL, 0, true), false);	/* builtins that return void */
	else if (!e && !q && !c->tentative && ((t->len == 8 && !memcmp(t->s, "__func__", 8)) || (t->len == 12 && !memcmp(t->s, "__FUNCTION__", 12)) ||
					       (t->len == 19 && !memcmp(t->s, "__PRETTY_FUNCTION__", 19)))) {
		/* the function's name, a static const char[] (here its symbol's name: clang's spells the signature;
		   only messages read it) */
		const char *fn = "";
		size_t fl = 0;

		if (c->ast->fn && cxx_gen_on(c))
			fn = vintern_ents[c->ast->syms[c->ast->fn].name].s, fl = vintern_ents[c->ast->syms[c->ast->fn].name].len;
		cxx_et(c, cc_type_array(c->ast, cc_type_qualify(c->ast, cc_type_basic(TY_CHAR), Q_CONST), fl + 1, 0), true);
		cxx_gen_string(c, fn, fl, t->loc);
	} else
		cxx_et(c, CXX_TY_UNKNOWN, false);
	(void)save;
	c->ename = ename;	/* (after the cxx_et calls above, which clear it) */
	c->ename_missing = ename && !e;
}
