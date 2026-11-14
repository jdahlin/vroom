/* C++ code generation: lambdas: captures, closure objects, their call operators */
#include "codegen.h"

/*
 * The capture of v (NULL: of this) in the lambda being built: found, or made if the capture list allows
 * it.  Its index, -1 if it is not allowed or not built (a class copied by value that is not trivial).
 */
VPRIV int cxx_gen_capture(cxx *c, cxx_gen_lambda *l, const cxx_entity *v, uint32_t loc)
{
	cc *a = c->ast;
	bool by_ref = l->def == 1, allowed = l->def != 0;
	qty t, lt;
	uint32_t s;

	for (uint32_t k = 0; k < l->n; k++)
		if (v ? (!l->cap[k].is_this && l->cap[k].sym == v->sym) : l->cap[k].is_this)
			return (int)k;
	if (l->n == 32)
		return -1;
	if (!v) {
		if (!allowed && !l->ex_this)
			return -1;
		if (c->this_type == CXX_TY_UNKNOWN || !c->this_type)
			return -1;
		t = cc_type_pointer(a, c->this_const ? QT(QI(c->this_type), QQ(c->this_type) | Q_CONST) : c->this_type);
		lt = t;
	} else {
		for (uint32_t i = 0; i < l->nex; i++)
			if (l->ex[i].name == v->name)
				allowed = true, by_ref = l->ex[i].by_ref;
		if (!allowed)
			return -1;
		t = a->syms[v->sym].ty;
		if (cxx_gen_is_reference(c, t))
			t = AST_TYPE(a, t)->base;
		if (!by_ref && (AST_TYPE_KIND(a, t) == TY_ARRAY || !(cxx_gen_type_ok(c, QUNQ(t)) || cxx_gen_trivial_abi(c, QUNQ(t), 0) == 1)))
			return -1;	/* (a copy of an array, or of a class with a copy constructor: not built yet) */
		lt = cc_type_pointer(a, t);
		t = by_ref ? lt : QUNQ(t);
	}
	s = cc_symbol_new(a, S_VAR, 0, lt, loc);
	a->syms[s].sc = SC_AUTO;
	l->cap[l->n].sym = v ? v->sym : 0, l->cap[l->n].e = v, l->cap[l->n].by_ref = by_ref, l->cap[l->n].is_this = !v;
	l->cap[l->n].local = s, l->cap[l->n].fty = t;
	return (int)l->n++;
}

VPRIV uint32_t cxx_gen_capture_name(uint32_t k)
{
	char nm[64] = { 0 };

	snprintf(nm, 32, "__c%u", k);
	return vintern_cstr(nm);
}

/* a closure type's operator(): its symbol, a local one with a name of the TU's own */
VPRIV bool cxx_gen_lambda_symbol(cxx *c, cxx_entity *call, uint32_t serial)
{
	cc *a = c->ast;
	char nm[64];
	attrs at;
	qty st = cxx_gen_abi_type(c, call);

	if (st == CXX_TY_UNKNOWN)
		return false;
	call->sym = cc_symbol_new(a, S_FUNC, call->name, st, 0);
	a->syms[call->sym].depth = 0;
	a->syms[call->sym].sc = SC_STATIC;	/* (internal linkage) */
	snprintf(nm, sizeof nm, " __vcc_lambda_%u", serial);
	memset(&at, 0, sizeof at);
	at.asmname = cxx_gen_text(c, nm + 1) + 1;
	cc_attribute_apply_symbol(a, &a->syms[call->sym], &at);
	call->mangled = 1, call->fn_internal = 1;
	return true;
}

/*
 * The lambda's body is built (still inside its operator()): the closure type gets its members, one per
 * capture, and the body is preceded by the pointers to the captured objects, read from the closure.
 */
VPRIV bool cxx_gen_lambda_finish(cxx *c, cxx_gen_lambda *l, cxx_entity *owner, uint32_t *body, uint32_t loc)
{
	cc *a = c->ast;
	uint8_t path_valid = c->name_path_valid;
	cc_id_list list;

	if (l->bad || !*body || !c->gen_this)
		return false;
	c->name_path_valid = 0;
	cxx_class_begin(c, owner, owner->name, loc, false, -1, false);
	for (uint32_t k = 0; k < l->n; k++)
		cxx_class_field(c, cxx_gen_capture_name(k), l->cap[k].fty, loc, -1);
	cxx_class_end(c);
	c->name_path_valid = path_valid;
	if (!(a->tags[AST_TYPE(a, owner->ty)->aux].flags & RF_COMPLETE))
		return false;
	if (!l->n)
		return true;
	cc_id_list_init(&list);
	for (uint32_t k = 0; k < l->n; k++) {
		uint32_t self = cc_dereference_build(a, cc_node_rvalue(a, cxx_gen_var_node(c, c->gen_this, loc)), loc);
		uint32_t fld = member(a, self, cxx_gen_capture_name(k), loc), v;
		qty lt = a->syms[l->cap[k].local].ty;

		v = l->cap[k].is_this || l->cap[k].by_ref ? cc_node_rvalue(a, fld) : cc_address_of_build(a, fld, loc);
		v = cc_cast_insert(a, v, lt, cc_conversion_cast_kind(a, AST_NODE_AT(a, v)->ty, lt, v));
		cc_id_list_append(a, &list, cc_node_new_operands(a, N_DECL, cc_type_basic(TY_VOID), loc, l->cap[k].local, 0));
		cc_id_list_append(a, &list, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, cxx_gen_var_node(c, l->cap[k].local, loc), v, loc), 0, 0));
	}
	cc_id_list_append(a, &list, *body);
	*body = cc_statement_new(a, N_BLOCK, loc, 0, cc_id_list_freeze(a, &list), list.n);
	return true;
}

/* the lambda expression's value, where it stands: a temporary of the closure type holding its captures */
VPRIV void cxx_gen_lambda_object(cxx *c, cxx_gen_lambda *l, qty t, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t tmp, expr = 0, node;

	c->enode = 0;
	if (!cxx_gen_on(c))
		return;
	tmp = cxx_gen_temporary(c, t, loc);
	if (!tmp)
		return;
	for (uint32_t k = 0; k < l->n; k++) {
		uint32_t fld, v, asg;
		qty ft = l->cap[k].fty;

		c->enode = 0;
		if (l->cap[k].is_this)
			cxx_gen_this(c, loc);
		else if (l->cap[k].sym && !c->gen_lambda) {
			/* the variable captured, by its symbol (a pack's element: its entity names another by now) */
			uint32_t n = cxx_gen_var_node(c, l->cap[k].sym, loc);

			c->enode = cxx_gen_is_reference(c, c->ast->syms[l->cap[k].sym].ty) ? cxx_gen_referent(c, n, loc) : n;
		} else
			cxx_gen_variable(c, l->cap[k].e, loc);
		v = c->enode;
		c->enode = 0;
		if (!v) {
			cxx_gen_fail(c);
			return;
		}
		if (l->cap[k].by_ref && !l->cap[k].is_this) {
			if (!(AST_NODE_AT(a, v)->flags & NF_LVALUE)) {
				cxx_gen_fail(c);
				return;
			}
			v = cc_address_of_build(a, v, loc);
		}
		if (l->cap[k].by_ref || l->cap[k].is_this)
			v = cc_cast_insert(a, v, ft, cc_conversion_cast_kind(a, AST_NODE_AT(a, v)->ty, ft, v));
		else if (cc_type_is_record(a, ft))
			v = cc_node_rvalue(a, v);
		else
			v = cc_conversion_apply(a, cc_conversion_decay(a, v), ft);
		fld = member(a, cxx_gen_var_node(c, tmp, loc), cxx_gen_capture_name(k), loc);
		asg = cc_assignment_build(a, 0, fld, v, loc);
		expr = expr ? cc_node_new_operands(a, N_COMMA, AST_NODE_AT(a, asg)->ty, loc, expr, asg) : asg;
	}
	if (expr) {
		node = cc_address_of_build(a, cxx_gen_var_node(c, tmp, loc), loc);
		node = cc_dereference_build(a, cc_node_new_operands(a, N_COMMA, AST_NODE_AT(a, node)->ty, loc, expr, node), loc);
	} else
		node = cxx_gen_var_node(c, tmp, loc);
	c->enode = node;
}
