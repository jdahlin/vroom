/* C++ code generation: new and delete expressions */
#include "codegen.h"

/*
 * new T(args), new T{args}, new T, new T[n], new (p) T(args): the storage (operator new, or the placement
 * argument p), the object constructed in it (cxx_ctor_use was called for a class with the arguments), the
 * pointer the result.  ninit: -1 no initializer, else the arguments' count.  Arrays: of scalars only.
 */
VPRIV void cxx_gen_new(cxx *c, qty t, const uint32_t *place, uint32_t nplace, uint32_t count, bool array, const uint32_t *init,
		       int ninit, uint32_t loc)
{
	cc *a = c->ast;
	qty pt;
	uint32_t mem, tmp, obj, size, last;
	cc_id_list l;

	c->enode = 0;
	if (!cxx_gen_on(c) || t == CXX_TY_UNKNOWN || AST_TYPE_KIND(a, t) == TY_VOID || AST_TYPE_KIND(a, t) == TY_FUNC ||
	    !(cxx_gen_object_type_ok(c, t) || cxx_gen_type_ok(c, t)) || cxx_gen_is_reference(c, t))
		return;
	if (array && (cc_type_is_record(a, t) || ninit > 0 || !count))
		return;	/* (an array of a class, an array from a list: not built yet) */
	pt = cc_type_pointer(a, t);
	size = cc_node_integer_new(a, (i128)AST_TYPE(a, QUNQ(t))->size, cc_type_basic(TY_ULONG), loc);
	if (nplace == 1 && place[0] && AST_TYPE_KIND(a, AST_NODE_AT(a, cc_conversion_decay(a, place[0]))->ty) == TY_PTR)
		mem = cc_conversion_decay(a, place[0]);	/* new (p) T: operator new(size_t, void *) gives p back */
	else if (nplace)
		return;	/* (nothrow, alignment, user placement forms: not built yet) */
	else if (array) {
		uint32_t n = cc_conversion_apply(a, cc_conversion_decay(a, count), cc_type_basic(TY_ULONG));

		size = cc_binary_build(a, LEX_TOKEN_STAR, n, size, loc);
		mem = cxx_gen_library_call(c, "__vcc_new_array", &size, 1, loc);
	} else
		mem = cxx_gen_library_call(c, "__builtin_operator_new", &size, 1, loc);
	if (!mem || !(tmp = cxx_gen_temporary(c, pt, loc)))
		return;
	cc_id_list_init(&l);
	cc_id_list_append(a, &l, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, cxx_gen_var_node(c, tmp, loc),
					cc_cast_insert(a, mem, pt, cc_conversion_cast_kind(a, AST_NODE_AT(a, mem)->ty, pt, mem)), loc), 0, 0));
	obj = cc_dereference_build(a, cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc)), loc);
	if (array) {
		if (ninit == 0) {	/* new T[n](): zeroed */
			uint32_t z[3] = { cc_cast_insert(a, cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc)), cc_type_pointer(a, cc_type_basic(TY_VOID)), CK_NOP),
					  cc_node_integer_new(a, 0, cc_type_basic(TY_INT), loc), size };

			if (!(last = cxx_gen_library_call(c, "__builtin_memset", z, 3, loc)))
				return;
			cc_id_list_append(a, &l, cc_statement_new(a, N_EXPR, loc, last, 0, 0));
		}
	} else if (cc_type_is_record(a, t) && AST_TYPE_KIND(a, t) != TY_ARRAY) {
		/* (no initializer: default-initialized, as T() is for a class with a constructor) */
		if (!cxx_gen_construct_at(c, obj, QUNQ(t), init, ninit < 0 ? 0 : (uint32_t)ninit, &l, loc))
			return;
	} else if (ninit == 1 || ninit == 0) {
		uint32_t v = ninit ? cxx_gen_bind(c, init[0], QUNQ(t), loc) : cc_conversion_apply(a, cc_node_integer_new(a, 0, cc_type_basic(TY_INT), loc), QUNQ(t));

		if (!v)
			return;
		if (t != QUNQ(t)) {
			qty upt = cc_type_pointer(a, QUNQ(t));

			obj = cc_dereference_build(a, cc_cast_insert(a, cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc)), upt, CK_NOP), loc);
		}
		cc_id_list_append(a, &l, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, obj, v, loc), 0, 0));
	} else if (ninit > 1)
		return;
	c->enode = cxx_gen_chain(c, &l, cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc)), loc);
}

/* delete p, delete[] p: the object's destructor (a class's), then operator delete (sized, as clang does) */
VPRIV void cxx_gen_delete(cxx *c, uint32_t p, bool array, uint32_t loc)
{
	cc *a = c->ast;
	qty pt, t;
	uint32_t tmp, n, args[2];
	cc_id_list l;
	cxx_entity *d;

	c->enode = 0;
	if (!cxx_gen_on(c) || !p)
		return;
	p = cc_node_rvalue(a, p);
	pt = QUNQ(AST_NODE_AT(a, p)->ty);
	if (AST_TYPE_KIND(a, pt) != TY_PTR || AST_TYPE(a, pt)->ext)
		return;
	t = AST_TYPE(a, pt)->base;
	if (AST_TYPE_KIND(a, t) == TY_VOID || (array && cc_type_is_record(a, t) && cxx_gen_needs_destruction(c, t, 0)))
		return;	/* (an array of objects with destructors: its count cookie, not built yet) */
	if (cc_type_is_record(a, t) && cxx_class_polymorphic(c, t)) {
		/* a polymorphic object: its deleting destructor (D0) through the vtable, the slot after the complete
		   one's -- tmp = p, tmp ? (*(F ***)tmp)[slot](tmp) : (void)0 */
		cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t));
		qty pf, ppf, pppf;
		uint32_t vptr, fn, call, cond;

		d = array ? NULL : cxx_gen_effective_destructor(c, t, 0);
		if (!d || !d->fn_virtual || !d->vslot || !cls || !cxx_vtable_usable(c, cls) || !cxx_gen_name(c, d) || !d->sym0)
			return;
		if (!(tmp = cxx_gen_temporary(c, pt, loc)))
			return;
		pf = cc_type_pointer(a, a->syms[d->sym0].ty), ppf = cc_type_pointer(a, pf), pppf = cc_type_pointer(a, ppf);
		vptr = cc_node_rvalue(a, cc_dereference_build(a, cc_cast_insert(a, cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc)), pppf, CK_NOP), loc));
		fn = cc_node_rvalue(a, cc_dereference_build(a, cc_binary_build(a, LEX_TOKEN_PLUS, vptr,
				cc_node_integer_new(a, d->vslot, cc_type_basic(TY_LONG), loc), loc), loc));
		call = cc_node_new(a, N_CALL, cc_type_basic(TY_VOID), loc);
		{
			cc_id_list al;

			cc_id_list_init(&al);
			cc_id_list_append(a, &al, cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc)));
			AST_NODE_AT(a, call)->a = fn;
			AST_NODE_AT(a, call)->b = cc_id_list_freeze(a, &al);
			AST_NODE_AT(a, call)->c = al.n;
		}
		cond = cc_conditional_build(a, cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc)), call,
					    cc_cast_insert(a, cc_node_integer_new(a, 0, cc_type_basic(TY_INT), loc), cc_type_basic(TY_VOID), CK_VOID), loc);
		c->enode = cc_node_new_operands(a, N_COMMA, cc_type_basic(TY_VOID), loc,
						cc_assignment_build(a, 0, cxx_gen_var_node(c, tmp, loc), p, loc), cond);
		return;
	}
	if (!(tmp = cxx_gen_temporary(c, pt, loc)))
		return;
	cc_id_list_init(&l);
	cc_id_list_append(a, &l, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, cxx_gen_var_node(c, tmp, loc), p, loc), 0, 0));
	d = cc_type_is_record(a, t) ? cxx_gen_effective_destructor(c, t, 0) : NULL;
	if (cc_type_is_record(a, t) && !d && cxx_gen_needs_destruction(c, t, 0))
		return;
	args[0] = cc_cast_insert(a, cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc)), cc_type_pointer(a, cc_type_basic(TY_VOID)), CK_NOP);
	args[1] = cc_node_integer_new(a, (i128)AST_TYPE(a, QUNQ(t))->size, cc_type_basic(TY_ULONG), loc);
	n = array ? cxx_gen_library_call(c, "__vcc_delete_array", args, 1, loc) : cxx_gen_library_call(c, "__builtin_operator_delete", args, 2, loc);
	if (!n)
		return;
	if (d) {
		/* (the destructor runs on the object; both only when p is not null) */
		uint32_t saved = c->enode;

		c->enode = 0;
		cxx_gen_call(c, d, cc_dereference_build(a, cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc)), loc), false, NULL, 0, loc);
		if (!c->enode) {
			c->enode = saved;
			return;
		}
		n = cc_node_new_operands(a, N_COMMA, AST_NODE_AT(a, n)->ty, loc, c->enode, n);
		c->enode = saved;
	}
	{
		/* p ? (dtor, delete) : (void)0 */
		uint32_t cond = cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc));
		uint32_t none = cc_cast_insert(a, cc_node_integer_new(a, 0, cc_type_basic(TY_INT), loc), cc_type_basic(TY_VOID), CK_VOID);

		n = cc_conditional_build(a, cond, cc_cast_insert(a, n, cc_type_basic(TY_VOID), CK_VOID), none, loc);
	}
	c->enode = cxx_gen_chain(c, &l, n, loc);
}
