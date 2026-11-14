/* C++ expressions: postfix expressions (calls, members, subscripts) */
#include "expression.h"

/* VCC_CXX_GEN=2: the innermost postfix expression of each function that code generation did not build */
VPRIV void cxx_postfix_expression_parse(cxx *c)
{
	static __thread int trace = -1;
	uint32_t start = c->pos;

	cxx_postfix_expression_parse1(c);
	if (trace < 0)
		trace = (getenv("VCC_CXX_GEN") && atoi(getenv("VCC_CXX_GEN")) >= 2) ? 1 : getenv("VCC_CXX_MISSING") ? 2 : 0;
	if (trace && !c->enode && cxx_gen_on(c))
		cxx_gen_note_no_node(c, start);	/* (the function's reason, unless one is known) */
	if (trace == 1 && !c->enode && cxx_gen_on(c) && !c->gen_why) {
		c->gen_why = 1;
		fprintf(stderr, "no node:");
		for (uint32_t i = start; i < c->pos && i < start + 12; i++)
			fprintf(stderr, " %.*s", (int)c->tok[i].len, c->tok[i].s);
		fprintf(stderr, " -> %s\n", c->etype == CXX_TY_UNKNOWN ? "untyped" : cc_type_is_record(c->ast, c->etype) ? "class" : "typed");
	}
}

VPRIV void cxx_postfix_expression_parse1(cxx *c)
{
	cxx_primary_expression_parse(c);
	for (;;) {
		switch (cxx_kind(c)) {
		case LEX_TOKEN_LEFT_SQUARE:
			if (cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_SQUARE)
				return;	/* [[ attribute ]] */
			cxx_advance(c);
			{
				uint32_t targs = c->in_template_args;
				qty base = c->etype;
				uint32_t bn = c->enode, in, iloc = cxx_peek(c, 0)->loc;

				c->in_template_args = 0;
				if (cxx_is(c, LEX_TOKEN_LEFT_BRACE))
					cxx_braced_init_list_parse(c);
				else
					cxx_expression_parse(c);
				c->in_template_args = targs;
				in = c->enode;
				/* a[i]: an element of an array or pointer, or a class's operator[] */
				if (!c->tentative && cxx_operator_call_type(c, vintern_cstr("operator[]"), base, &c->etype, 1))
					cxx_gen_operator_call(c, bn, &in, 1, iloc);
				else {
					cxx_et(c, base != CXX_TY_UNKNOWN && (AST_TYPE_KIND(c->ast, base) == TY_PTR || AST_TYPE_KIND(c->ast, base) == TY_ARRAY) &&
					       !AST_TYPE(c->ast, base)->ext ? AST_TYPE(c->ast, base)->base : CXX_TY_UNKNOWN, true);
					if (c->etype != CXX_TY_UNKNOWN)
						cxx_gen_index(c, bn, in, iloc);
				}
			}
			cxx_expect(c, LEX_TOKEN_RIGHT_SQUARE);
			continue;
		case LEX_TOKEN_LEFT_PAREN:
			{
				uint32_t targs = c->in_template_args;
				cxx_entity *set = c->ecallee;
				qty callee = c->etype;
				uint8_t callee_lval = c->elval;
				uint32_t obj_node = c->eobj_node, callee_node = c->enode;
				bool obj_arrow = c->eobj_arrow;
				cxx_targ xa[8];
				uint32_t nx = c->xargs_set && c->nxargs <= 8 ? c->nxargs : UINT32_MAX;
				uint8_t call_obj = c->call_obj;
				uint32_t ename = c->ename;
				bool missing = c->ename_missing;
				uint32_t on_args = 0, on_nodes[CXX_MAX_ARGS];

				if (c->xargs_set && nx == UINT32_MAX)
					set = NULL, callee = CXX_TY_UNKNOWN;	/* (too many explicit arguments) */
				if (nx != UINT32_MAX)
					memcpy(xa, c->xargs, nx * sizeof *xa);

				/* (a virtual function named with a qualifier is called directly; a callee that is not a plain name: not known) */
				uint8_t vmode = c->pos >= 2 && c->tok[c->pos - 1].kind == LEX_TOKEN_IDENTIFIER ?
						(c->tok[c->pos - 2].kind == LEX_TOKEN_COLON_COLON ? 1 : 0) : 2;

				c->in_template_args = 0;
				cxx_call_arguments_parse(c);
				c->in_template_args = targs;
				if (c->ncargs == UINT32_MAX || c->tentative)
					cxx_et(c, CXX_TY_UNKNOWN, false);
				else if (nx != UINT32_MAX && set) {
					uint32_t anodes[CXX_MAX_ARGS], an = c->ncargs, cloc = c->tok[c->pos ? c->pos - 1 : 0].loc;

					memcpy(anodes, c->carg_nodes, sizeof anodes);
					uint32_t abrace[CXX_MAX_ARGS];

					memcpy(abrace, c->carg_brace, sizeof abrace);
					c->call_obj = call_obj, cxx_call_type_x(c, set, xa, nx, c->cargs, c->cargs_lval, c->ncargs);
					if (!c->ncargs && !c->econst && !c->template_depth && c->etype != CXX_TY_UNKNOWN &&
					    AST_TYPE_KIND(c->ast, c->etype) >= TY_BOOL && AST_TYPE_KIND(c->ast, c->etype) <= TY_ULLONG) {
						/* f<args>() of a constexpr function template: its value, a constant */
						cxx_entity *te = cxx_entity_kind_find(set, CXX_ENTITY_FUNCTION_TEMPLATE);
						qty rt = QUNQ(c->etype);
						int64_t v;

						if (te && cxx_ftemplate_const_call(c, te, xa, nx, &v)) {
							cxx_ec(c, rt, rt == cc_type_basic(TY_BOOL) ? v != 0 : v);
							continue;
						}
					}
					if (c->call_resolved && c->call_picked && c->etype != CXX_TY_UNKNOWN && (!call_obj || obj_node))
						c->gen_vmode = vmode, memcpy(c->gen_call_brace, abrace, sizeof abrace), cxx_gen_call(c, c->call_picked, obj_node, obj_arrow, anodes, an, cloc), c->gen_vmode = 0;
				}
				else if (!set && callee != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, callee) &&	/* (operator(): a member only) */
					 (on_args = c->ncargs, memcpy(on_nodes, c->carg_nodes, sizeof on_nodes),
						  c->op_lv_set = c->ncargs <= CXX_MAX_ARGS, c->op_lv[0] = callee_lval,
						  memcpy(c->op_lv + 1, c->cargs_lval, c->ncargs <= CXX_MAX_ARGS ? c->ncargs : 0),
						  cxx_operator_call_type(c, vintern_cstr("operator()"), callee, c->cargs, c->ncargs))) {
					/* an object of class type: its operator() (the arguments as parsed: typing the call may
					   instantiate its body, whose own calls reuse c->ncargs and c->carg_nodes) */
					if (!callee_node && c->gen)
						callee_node = cxx_gen_empty_object(c, callee, c->tok[c->pos ? c->pos - 1 : 0].loc);
					cxx_gen_operator_call(c, callee_node, on_nodes, on_args, c->tok[c->pos ? c->pos - 1 : 0].loc);
				}
				else {
					uint8_t was = c->einvalid;	/* (an earlier part of the expression may be ill-formed already) */
					uint32_t anodes[CXX_MAX_ARGS], an = c->ncargs, cloc = c->tok[c->pos ? c->pos - 1 : 0].loc;

					memcpy(anodes, c->carg_nodes, sizeof anodes);
					uint32_t abrace[CXX_MAX_ARGS];

					memcpy(abrace, c->carg_brace, sizeof abrace);
					c->call_obj = call_obj, cxx_call_type(c, set, callee, c->cargs, c->cargs_lval, c->ncargs);
					if (set && c->call_resolved && c->call_picked && c->etype != CXX_TY_UNKNOWN && (!call_obj || obj_node))
						c->gen_vmode = vmode, memcpy(c->gen_call_brace, abrace, sizeof abrace), cxx_gen_call(c, c->call_picked, obj_node, obj_arrow, anodes, an, cloc), c->gen_vmode = 0;
					else if (!set && callee_node && c->etype != CXX_TY_UNKNOWN && an != UINT32_MAX)
						cxx_gen_indirect_call(c, callee_node, anodes, an, cloc);	/* through a pointer to a function */
					/* f(x) with no f that fits: the functions named f in the namespaces of the
					   arguments' classes (argument-dependent lookup) */
					if (c->etype == CXX_TY_UNKNOWN && ename && (!set || c->none_viable)) {
						bool none = c->none_viable || (!set && missing), known = true;

						for (uint32_t i = 0; i < c->ncargs && i < 16; i++)
							known &= c->cargs[i] != CXX_TY_UNKNOWN;
						cxx_adl_call_type(c, ename, c->cargs, c->cargs_lval, c->ncargs);
						/* nothing by that name fits, here or in the arguments' namespaces */
						c->einvalid = was || (c->etype == CXX_TY_UNKNOWN && none && known && !c->einvalid_unsure);
						if (c->einvalid && !was)
							c->einvalid_at = c->pos;
					}
				}
			}
			continue;
		case LEX_TOKEN_PERIOD: case LEX_TOKEN_ARROW: {
			/* the object's class: its scope, for the member's lookup */
			qty obj = c->etype;
			cxx_scope *cls = NULL;
			uint32_t obj_node = c->enode, mloc = cxx_peek(c, 0)->loc;
			bool arrow = cxx_is(c, LEX_TOKEN_ARROW);

			/* x->m on an object of class type: (x.operator->())->m, again while the result is a class
			   ([over.ref]: p->m is interpreted as (p.operator->())->m) */
			for (uint32_t hop = 0; arrow && hop < 8 && !c->tentative && obj != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, obj) &&
			     AST_TYPE_KIND(c->ast, obj) != TY_ARRAY; hop++) {
				if (!cxx_operator_call_type(c, vintern_cstr("operator->"), obj, NULL, 0) || c->etype == CXX_TY_UNKNOWN) {
					obj = CXX_TY_UNKNOWN;
					break;
				}
				if (c->call_resolved && c->call_picked)
					cxx_gen_operator_call(c, obj_node, NULL, 0, mloc);
				obj = c->etype, obj_node = c->call_resolved && c->call_picked ? c->enode : 0;
			}
			if (cxx_is(c, LEX_TOKEN_ARROW))
				obj = obj != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, obj) == TY_PTR && !AST_TYPE(c->ast, obj)->ext ?
				      AST_TYPE(c->ast, obj)->base : CXX_TY_UNKNOWN;
			if (obj != CXX_TY_UNKNOWN && !c->tentative)
				cls = cxx_type_class_scope(c, QUNQ(obj));
			if (obj != CXX_TY_UNKNOWN && !c->tentative && !cc_type_is_record(c->ast, obj) && !cxx_is(c, LEX_TOKEN_TILDE))
				CXX_INVALID(c);	/* x.m on what is not a class (a requirement fails) */
			cxx_et(c, CXX_TY_UNKNOWN, false);
			cxx_advance(c);
			{
				/* a member: names are looked up in the object's class, which the parser does not know;
				   template-ids need 'template' when dependent, else lookup by name finds member templates */
				cxx_scope *q;
				bool dep, template_kw;
				uint32_t id;

				template_kw = cxx_accept_kw(c, CXX_KW_template);
				cxx_nested_name_parse(c, &q, &dep);
				template_kw |= cxx_accept_kw(c, CXX_KW_template);
				if (cxx_is(c, LEX_TOKEN_TILDE) && !q && !dep && cxx_peek(c, 1)->kind == LEX_TOKEN_IDENTIFIER &&
				    cxx_peek(c, 2)->kind == LEX_TOKEN_LEFT_PAREN && cxx_peek(c, 3)->kind == LEX_TOKEN_RIGHT_PAREN) {
					/* x.~T(), p->~T(): the destructor called (of a scalar: nothing) */
					uint32_t dloc = cxx_peek(c, 0)->loc;

					c->pos += 4;
					cxx_et(c, cc_type_basic(TY_VOID), false);
					if (!c->tentative && !c->template_depth && obj_node && obj != CXX_TY_UNKNOWN)
						cxx_gen_destroy_object(c, obj_node, arrow, dloc);
					continue;
				}
				if (cxx_is(c, LEX_TOKEN_TILDE) || cxx_is_kw(c, CXX_KW_operator)) {
					/* x.operator->(), x.operator+=(y): the member operator by its name */
					uint32_t oid = 0;

					if (cxx_is_kw(c, CXX_KW_operator) && cls && !q && !dep) {
						uint32_t save = c->pos, on;

						c->pos++;
						if (cxx_is(c, LEX_TOKEN_LEFT_PAREN) && cxx_peek(c, 1)->kind == LEX_TOKEN_RIGHT_PAREN)
							oid = vintern_cstr("operator()");
						else if (cxx_is(c, LEX_TOKEN_LEFT_SQUARE) && cxx_peek(c, 1)->kind == LEX_TOKEN_RIGHT_SQUARE)
							oid = vintern_cstr("operator[]");
						else if ((cxx_kind(c) >= LEX_TOKEN_LEFT_SQUARE && cxx_kind(c) < LEX_TOKEN_HASH) || cxx_is(c, CXX_TOKEN_SPACESHIP) ||
							 cxx_is(c, CXX_TOKEN_ARROW_STAR)) {
							cxx_operator_at(c, &on);
							oid = cxx_operator_id_at(c, on);
						}
						c->pos = save;
					}
					cxx_unqualified_id_rest_parse(c);
					cxx_et(c, CXX_TY_UNKNOWN, false);
					if (oid > 1) {
						cxx_entity *m = cxx_lookup_in(c, cls, oid, 0);

						if (m && m->kind != CXX_ENTITY_DEPENDENT) {
							cxx_name_type(c, m);
							c->call_obj = (QQ(obj) & Q_CONST) ? 2 : 1;
							c->eobj_node = obj_node, c->eobj_arrow = arrow;	/* (code generation: the call's object) */
						} else if (!m && !cls->dependent && !cls->has_dependent_base)
							CXX_INVALID(c);
					}
					continue;
				}
				if (!cxx_is(c, LEX_TOKEN_IDENTIFIER))
					cxx_fail(c, "expected a member name");
				id = cxx_peek(c, 0)->id;
				cxx_advance(c);
				/* the object's type is not known here: name<...>( is a member template call if it parses */
				if (cxx_is(c, LEX_TOKEN_LESS) && (template_kw || cxx_lookahead_fn(c, cxx_template_arguments_then_call))) {
					cxx_entity *m = cls && !q && !dep ? cxx_lookup_in(c, cls, id, 0) : NULL;

					cxx_template_arguments_parse(c);
					cxx_et(c, CXX_TY_UNKNOWN, false);
					/* x.f<args>(...): the member function templates named f, with the explicit arguments */
					if (m && m->kind != CXX_ENTITY_DEPENDENT && cxx_entity_kind_find(m, CXX_ENTITY_FUNCTION_TEMPLATE) && !c->tentative) {
						if (c->ntargs > c->cxargs)
							c->xargs = realloc(c->xargs, (c->cxargs = c->ntargs + 8) * sizeof *c->xargs);
						memcpy(c->xargs, c->targs, c->ntargs * sizeof *c->xargs);
						c->nxargs = c->ntargs, c->xargs_set = 1, c->ecallee = m;
						c->call_obj = (QQ(obj) & Q_CONST) ? 2 : 1;
						c->eobj_node = obj_node, c->eobj_arrow = arrow;	/* (code generation: the call's object) */
					} else if (cls && !q && !dep && !m && !cls->dependent && !cls->has_dependent_base && !c->tentative)
						CXX_INVALID(c);	/* no such member template */
					continue;
				}
				/* the member: a data member's type, or the member functions for the call */
				if (cls && !q && !dep) {
					cxx_entity *m = cxx_lookup_in(c, cls, id, 0);

					if (m && m->kind != CXX_ENTITY_DEPENDENT) {
						cxx_name_type(c, m);
						c->call_obj = (QQ(obj) & Q_CONST) ? 2 : 1;
						/* a data member of a const object is const (not a mutable one, not a reference):
						   x.layout.begin_ptr() on a const x calls the const overload */
						if (c->etype != CXX_TY_UNKNOWN && c->edecl_name && !c->ecallee && (QQ(obj) & Q_CONST)) {
							cxx_entity *fe = m;

							while (fe && !(fe->kind == CXX_ENTITY_VALUE && fe->field_of))
								fe = fe->next;
							if (fe && !fe->is_mutable && !(AST_TYPE_KIND(c->ast, c->etype) == TY_PTR && AST_TYPE(c->ast, c->etype)->ext))
								c->etype = QT(QI(c->etype), QQ(c->etype) | Q_CONST);
						}
						if (c->etype != CXX_TY_UNKNOWN && c->edecl_name && !c->ecallee)
							cxx_gen_member(c, obj_node, arrow, id, mloc);	/* a data member */
						else
							c->eobj_node = obj_node, c->eobj_arrow = arrow;	/* member functions: the call's object */
					} else if (!m && !cls->dependent && !cls->has_dependent_base)
						CXX_INVALID(c);	/* no such member (a requirement fails) */
				}
			}
			continue;
		}
		case LEX_TOKEN_PLUS_PLUS: case LEX_TOKEN_MINUS_MINUS: {
			int op = cxx_kind(c);
			uint32_t xn = c->enode, oploc = cxx_peek(c, 0)->loc;
			bool built_in = c->etype != CXX_TY_UNKNOWN && (cc_type_is_arithmetic(c->ast, c->etype) || AST_TYPE_KIND(c->ast, c->etype) == TY_PTR);

			cxx_advance(c);
			cxx_et(c, built_in ? QUNQ(c->etype) : CXX_TY_UNKNOWN, false);
			if (built_in)
				cxx_gen_postfix(c, op, xn, oploc);
			continue;
		}
		}
		return;
	}
}

/* ---- unary, cast, binary ----------------------------------------------------------------- */
