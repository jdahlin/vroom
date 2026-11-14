/* C++ code generation: statements: throw, locals, range for, declarations in a body */
#include "codegen.h"

/*
 * throw x: the exception object is allocated by the runtime and initialized from x, then thrown with its
 * type info and its destructor.  throw alone: the exception being handled, again.
 */
VPRIV void cxx_gen_throw(cxx *c, uint32_t x, bool has_operand, uint32_t loc)
{
	cc *a = c->ast;
	qty t, vp = cc_type_pointer(a, cc_type_basic(TY_VOID)), pt;
	uint32_t ti, tmp, alloc, target, init, args[3], dtor = 0, n;
	cxx_entity *d;

	if (!cxx_gen_on(c))
		return;
	c->enode = 0;
	if (!has_operand) {
		c->enode = cxx_gen_runtime_call(c, cxx_gen_eh_symbol(c, 2), NULL, 0, loc);
		return;
	}
	if (!x)
		return;
	if (!cc_type_is_record(a, AST_NODE_AT(a, x)->ty) || AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) == TY_ARRAY)
		x = cc_conversion_decay(a, x);	/* (an array, a function: a pointer is thrown) */
	t = QUNQ(AST_NODE_AT(a, x)->ty);
	ti = cxx_gen_typeinfo(c, t);
	if (!ti)
		return;
	tmp = cxx_gen_temporary(c, vp, loc);
	if (!tmp)
		return;
	n = cc_node_integer_new(a, (i128)AST_TYPE(a, t)->size, cc_type_basic(TY_ULONG), loc);
	alloc = cc_assignment_build(a, 0, cxx_gen_var_node(c, tmp, loc), cxx_gen_runtime_call(c, cxx_gen_eh_symbol(c, 0), &n, 1, loc), loc);
	pt = cc_type_pointer(a, t);
	n = cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc));
	target = cc_dereference_build(a, cc_cast_insert(a, n, pt, cc_conversion_cast_kind(a, vp, pt, n)), loc);
	if (cc_type_is_record(a, t)) {
		/* a class: copied or moved by its constructor (a prvalue: made there in the first place) */
		qty at[1] = { AST_NODE_AT(a, x)->ty };
		uint8_t lv[1] = { 1 };
		cc_id_list l;

		if (!(AST_NODE_AT(a, x)->flags & NF_LVALUE))
			return;
		if (!cxx_gen_prv_find(c, x))
			cxx_ctor_use(c, t, at, lv, 1, false);
		cc_id_list_init(&l);
		if (!cxx_gen_construct_at(c, target, t, &x, 1, &l, loc))
			return;
		init = cxx_gen_chain(c, &l, 0, loc);
		d = cxx_gen_effective_destructor(c, t, 0);
		if (cxx_gen_needs_destruction(c, t, 0)) {
			if (!d || d->fn_deleted || !cxx_gen_name(c, d) || !d->sym1 || (cxx_member_body_use(c, d), 0))
				return;
			dtor = cxx_gen_dtor_sym(d);
		}
	} else
		init = cc_assignment_build(a, 0, target, cc_node_rvalue(a, x), loc);
	args[0] = cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc));
	args[1] = cxx_gen_data_address(c, ti, 0);
	if (dtor)
		args[2] = cxx_gen_to_void_pointer(c, cxx_gen_function_node(c, dtor, loc));
	else
		args[2] = cxx_gen_to_void_pointer(c, cc_node_integer_new(a, 0, cc_type_basic(TY_LONG), loc));
	n = cxx_gen_runtime_call(c, cxx_gen_eh_symbol(c, 1), args, 3, loc);
	if (init)
		n = cc_node_new_operands(a, N_COMMA, cc_type_basic(TY_VOID), loc, init, n);
	c->enode = cc_node_new_operands(a, N_COMMA, cc_type_basic(TY_VOID), loc, alloc, n);
}

/*
 * A handler's start, into `list` (the handler's block): the exception is caught (__cxa_begin_catch, ended
 * by __cxa_end_catch when the block is left, any way but by an exception), and the handler's variable, of
 * the declared type decl_type (CXX_TY_UNKNOWN: none), is made: *var its symbol.  False: not built.
 */
/* a local's declaration with its initializer, into `list` */
VPRIV void cxx_gen_declare_with(cxx *c, cc_id_list *list, uint32_t s, uint32_t init, uint32_t loc)
{
	cc *a = c->ast;

	a->syms[s].sc = SC_AUTO;
	if (init) {
		a->syms[s].body = init;
		a->syms[s].flags |= SF_DEFINED;
	}
	cc_id_list_append(a, list, cc_node_new_operands(a, N_DECL, cc_type_basic(TY_VOID), loc, s, init));
}

VPRIV uint32_t cxx_gen_catch_begin(cxx *c, uint32_t exc, qty decl_type, uint32_t *var, cc_id_list *list, uint32_t loc)
{
	cc *a = c->ast;
	qty vp = cc_type_pointer(a, cc_type_basic(TY_VOID));
	uint32_t obj = cc_symbol_new(a, S_VAR, 0, vp, loc), guard = cc_symbol_new(a, S_VAR, 0, cc_type_basic(TY_CHAR), loc), n, arg, s, init;
	attrs at;

	*var = 0;
	arg = cc_node_rvalue(a, cxx_gen_var_node(c, exc, loc));
	cxx_gen_declare_with(c, list, obj, cxx_gen_runtime_call(c, cxx_gen_eh_symbol(c, 3), &arg, 1, loc), loc);
	memset(&at, 0, sizeof at);
	at.cleanup = cxx_gen_eh_symbol(c, 4);
	cc_attribute_apply_symbol(a, &a->syms[guard], &at);
	cxx_gen_declare_with(c, list, guard, 0, loc);
	if (decl_type == CXX_TY_UNKNOWN)
		return 1;
	n = cc_node_rvalue(a, cxx_gen_var_node(c, obj, loc));
	if (cxx_gen_is_reference(c, decl_type)) {
		/* T &e: the exception object itself */
		s = cc_symbol_new(a, S_VAR, 0, decl_type, loc);
		init = cc_cast_insert(a, n, decl_type, cc_conversion_cast_kind(a, vp, decl_type, n));
	} else if (AST_TYPE_KIND(a, decl_type) == TY_PTR) {
		/* a pointer: the runtime hands the pointer's value */
		s = cc_symbol_new(a, S_VAR, 0, QUNQ(decl_type), loc);
		init = cc_cast_insert(a, n, QUNQ(decl_type), cc_conversion_cast_kind(a, vp, QUNQ(decl_type), n));
	} else if (cxx_gen_type_ok(c, QUNQ(decl_type)) || (cc_type_is_record(a, decl_type) && cxx_gen_trivial_abi(c, decl_type, 0) == 1)) {
		/* a copy of a scalar, or of a class copied as its bytes */
		qty pt = cc_type_pointer(a, QUNQ(decl_type));

		s = cc_symbol_new(a, S_VAR, 0, QUNQ(decl_type), loc);
		init = cc_node_rvalue(a, cc_dereference_build(a, cc_cast_insert(a, n, pt, cc_conversion_cast_kind(a, vp, pt, n)), loc));
	} else
		return 0;	/* (a class copied by its constructor: not built yet) */
	cxx_gen_declare_with(c, list, s, init, loc);
	*var = s;
	return 1;
}

/* ---- statements ---------------------------------------------------------------------------- */

/* this statement's node (0: an empty statement); cxx_statement_parse checks that its own case said so */
VPRIV void cxx_gen_statement(cxx *c, uint32_t node)
{
	c->snode = node;
	c->sdone = c->sdepth;
}

VPRIV uint32_t cxx_gen_new_statement(cxx *c, int op, uint32_t loc, uint32_t x, uint32_t y, uint32_t z)
{
	return cc_statement_new(c->ast, op, loc, x, y, z);
}

/*
 * for (decl : range) over an array (or a braced list, made an array first): { E *p = range; for (long i = 0;
 * i < N; i++) { decl = p[i]; body } }.  begin/end of a class: not built yet.  The parts as the parser has
 * them: elems/nelems for a braced list (else range, an lvalue array), the variable's symbol made here.
 */
VPRIV bool cxx_gen_range_for_begin(cxx *c, cxx_gen_range *r, uint32_t range, const uint32_t *elems, uint32_t nelems, qty elem, uint32_t loc)
{
	cc *a = c->ast;
	qty lt = cc_type_basic(TY_LONG), et, pt;
	uint32_t arr, n;

	memset(r, 0, sizeof *r);
	if (!cxx_gen_on(c) || !c->gen_block)
		return false;
	cc_id_list_init(&r->pre);
	r->outer = c->gen_block;
	if (elems) {
		/* a braced list: a local array of its elements, converted to their type */
		if (!nelems || elem == CXX_TY_UNKNOWN || !cxx_gen_type_ok(c, elem))
			return false;
		et = QUNQ(elem);
		arr = cxx_gen_local(c, 0, cc_type_array(a, et, nelems, 0), loc);
		if (!arr)
			return false;
		c->gen_block = &r->pre;
		cxx_gen_local_declare(c, arr, 0, false, loc);
		for (uint32_t i = 0; i < nelems; i++) {
			uint32_t x = elems[i] ? cc_conversion_apply(a, cc_conversion_decay(a, cc_node_rvalue(a, elems[i])), et) : 0, slot;

			if (!x)
				goto fail;
			slot = cc_dereference_build(a, cc_binary_build(a, LEX_TOKEN_PLUS, cc_conversion_decay(a, cxx_gen_var_node(c, arr, loc)),
								   cc_node_integer_new(a, i, lt, loc), loc), loc);
			cc_id_list_append(a, &r->pre, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, slot, x, loc), 0, 0));
		}
		range = cxx_gen_var_node(c, arr, loc);
		n = nelems;
	} else {
		const ctype *x;

		if (!range || !(AST_NODE_AT(a, range)->flags & NF_LVALUE) || AST_TYPE_KIND(a, AST_NODE_AT(a, range)->ty) != TY_ARRAY)
			return false;
		x = AST_TYPE(a, AST_NODE_AT(a, range)->ty);
		if ((x->flags & TF_INCOMPLETE) || !x->n || !cxx_gen_object_type_ok(c, x->base))
			return false;
		et = x->base, n = (uint32_t)x->n;
		c->gen_block = &r->pre;
	}
	pt = cc_type_pointer(a, et);
	r->p = cxx_gen_local(c, 0, pt, loc);
	r->i = cxx_gen_local(c, 0, lt, loc);
	if (!r->p || !r->i)
		goto fail;
	cxx_gen_local_declare(c, r->p, cc_conversion_decay(a, range), true, loc);
	cxx_gen_local_declare(c, r->i, cc_node_integer_new(a, 0, lt, loc), true, loc);
	r->n = n, r->loc = loc, r->ok = true;
	cc_id_list_init(&r->body);
	c->gen_block = &r->body;
	return true;
fail:
	c->gen_block = r->outer;
	return false;
}

VPRIV uint32_t cxx_gen_var_node_public(cxx *c, uint32_t s, uint32_t loc)
{
	return cxx_gen_var_node(c, s, loc);
}

/* !x, as a node (0: not built) */
VPRIV uint32_t cc_unary_build_public(cxx *c, int op, uint32_t x, uint32_t loc)
{
	uint32_t saved = c->enode, n;

	c->enode = 0;
	cxx_gen_unary(c, op, x, loc);
	n = c->enode;
	c->enode = saved;
	return n;
}

/* the element the loop is at (an lvalue): p[i] */
VPRIV uint32_t cxx_gen_range_for_element(cxx *c, cxx_gen_range *r)
{
	cc *a = c->ast;

	if (r->cls)
		return r->elem;

	return cc_dereference_build(a, cc_binary_build(a, LEX_TOKEN_PLUS, cc_node_rvalue(a, cxx_gen_var_node(c, r->p, r->loc)),
						       cc_node_rvalue(a, cxx_gen_var_node(c, r->i, r->loc)), r->loc), r->loc);
}

/* the body parsed (c->snode): the loop, as the statement */
VPRIV void cxx_gen_range_for_end(cxx *c, cxx_gen_range *r)
{
	cc *a = c->ast;
	uint32_t cond, step, body;
	cc_id_list sb;

	c->gen_block = r->outer;
	if (!r->ok || !cxx_gen_on(c))
		return;
	if (c->snode)
		cc_id_list_append(a, &r->body, c->snode);
	body = cc_statement_new(a, N_BLOCK, r->loc, 0, cc_id_list_freeze(a, &r->body), r->body.n);
	if (r->cls)
		cond = r->cond, step = r->step;
	else {
		cond = cc_binary_build(a, LEX_TOKEN_LESS, cc_node_rvalue(a, cxx_gen_var_node(c, r->i, r->loc)),
				       cc_node_integer_new(a, r->n, cc_type_basic(TY_LONG), r->loc), r->loc);
		step = cc_assignment_build(a, LEX_TOKEN_PLUS, cxx_gen_var_node(c, r->i, r->loc), cc_node_integer_new(a, 1, cc_type_basic(TY_LONG), r->loc), r->loc);
	}
	cc_id_list_init(&sb);
	cc_id_list_append(a, &sb, step);
	cc_id_list_append(a, &sb, body);
	cc_id_list_append(a, &r->pre, cc_statement_new(a, N_FOR, r->loc, 0, cond, cc_id_list_freeze(a, &sb)));
	cxx_gen_statement(c, cc_statement_new(a, N_BLOCK, r->loc, 0, cc_id_list_freeze(a, &r->pre), r->pre.n));
}

/* a condition: the expression's node as a scalar truth value (0: not built) */
VPRIV uint32_t cxx_gen_condition(cxx *c, uint32_t e)
{
	if (!cxx_gen_on(c))
		return 0;
	if (e && cc_type_is_record(c->ast, AST_NODE_AT(c->ast, e)->ty))
		e = cxx_gen_convert_class(c, e, cc_type_basic(TY_BOOL), 0);	/* a class as a condition: its operator bool */
	if (!e || cc_type_is_record(c->ast, AST_NODE_AT(c->ast, e)->ty)) {
		cxx_gen_fail(c);
		return 0;
	}
	return cc_condition_scalar(c->ast, cc_conversion_decay(c->ast, e), "statement");
}

/* switch (e): the value switched on */
VPRIV uint32_t cxx_gen_switch_value(cxx *c, uint32_t e)
{
	cc *a = c->ast;

	if (!cxx_gen_on(c))
		return 0;
	if (!e) {
		cxx_gen_fail(c);
		return 0;
	}
	e = cc_node_rvalue(a, e);
	if (AST_TYPE_KIND(a, AST_NODE_AT(a, e)->ty) == TY_ENUM)
		e = cc_cast_insert(a, e, QUNQ(AST_TYPE(a, AST_NODE_AT(a, e)->ty)->base), CK_NOP);
	if (!cc_type_is_integer(a, AST_NODE_AT(a, e)->ty)) {
		cxx_gen_fail(c);
		return 0;
	}
	return cc_conversion_apply(a, e, cc_node_promoted_type(a, e));
}

/* case lo ... hi: stmt */
VPRIV uint32_t cxx_gen_case(cxx *c, int64_t lo, int64_t hi, uint32_t stmt, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t n;

	a->ipool[a->nipool] = lo;
	a->ipool[a->nipool + 1] = hi;
	n = cc_statement_new(a, N_CASE, loc, a->nipool, a->nipool + 1, stmt);
	a->nipool += 2;
	return n;
}

/* return e; (e: 0 for return;) converted to the function's return type */
VPRIV uint32_t cxx_gen_return(cxx *c, uint32_t e, bool has_value, uint32_t loc)
{
	cc *a = c->ast;

	if (!cxx_gen_on(c))
		return 0;
	if (c->gen_lambda && c->gen_lambda->is_try) {
		cxx_gen_fail(c);	/* (a return out of a try block: not built yet) */
		return 0;
	}
	if (has_value && e && AST_TYPE_KIND(a, c->gen_ret) == TY_VOID && AST_TYPE_KIND(a, AST_NODE_AT(a, e)->ty) == TY_VOID) {
		/* return f(); in a void function (C++ allows it, C does not): f(), then return */
		cc_id_list l;

		cc_id_list_init(&l);
		cc_id_list_append(a, &l, cc_statement_new(a, N_EXPR, loc, e, 0, 0));
		cc_id_list_append(a, &l, cc_statement_new(a, N_RETURN, loc, 0, 0, 0));
		return cc_statement_new(a, N_BLOCK, loc, 0, cc_id_list_freeze(a, &l), l.n);
	}
	if (has_value && (!e || AST_TYPE_KIND(a, c->gen_ret) == TY_VOID)) {
		cxx_gen_fail(c);
		return 0;
	}
	if (has_value && c->gen_sret) {
		/* a class returned by address: constructed in the object the caller gave, whose address is returned */
		qty t = QUNQ(c->gen_ret), at[1];
		uint8_t lv[1];
		const node *en = AST_NODE_AT(a, e);
		cc_id_list l;
		uint32_t target = cc_dereference_build(a, cc_node_rvalue(a, cxx_gen_var_node(c, c->gen_sret, loc)), loc);

		if (QUNQ(en->ty) != t) {
			/* return "text" (or 0, nullptr, an object of another class: pair<U, V> for pair<T, W>) for a
			   class: copy-initialized in the caller's object by a converting constructor (not an explicit
			   one) [stmt.return]/2, [dcl.init.general]/16.6.2 */
			qty ct[1] = { en->ty };
			uint8_t cl[1] = { (en->flags & NF_LVALUE) != 0 && !cxx_gen_prv_find(c, e) };

			if (AST_TYPE_KIND(a, ct[0]) == TY_ARRAY)
				ct[0] = cc_type_pointer(a, AST_TYPE(a, ct[0])->base), cl[0] = 0;
			if (cxx_gen_is_nullptr_node(c, e)) {
				ct[0] = c->nullptr_ty, cl[0] = 0;
				if (!(e = cxx_gen_nullptr_object(c, e, loc))) {
					cxx_gen_fail(c);
					return 0;
				}
			}
			c->skip_explicit = 2;
			cxx_ctor_use(c, t, ct, cl, 1, false);
			c->skip_explicit = 0;
			cc_id_list_init(&l);
			if (!c->ctor_found || !c->ctor_picked || c->ctor_picked->fn_explicit == 1 || !cxx_gen_construct_at(c, target, t, &e, 1, &l, loc)) {
				cxx_gen_fail(c);
				return 0;
			}
			cc_id_list_append(a, &l, cc_statement_new(a, N_RETURN, loc, cc_node_rvalue(a, cxx_gen_var_node(c, c->gen_sret, loc)), 0, 0));
			return cc_statement_new(a, N_BLOCK, loc, 0, cc_id_list_freeze(a, &l), l.n);
		}
		if (QUNQ(en->ty) != t || !(en->flags & NF_LVALUE)) {
			cxx_gen_fail(c);	/* (a conversion to the class: not built yet) */
			return 0;
		}
		/* a local variable named alone is moved from; any other object is copied */
		at[0] = en->ty;
		lv[0] = !(en->op == N_VAR && a->syms[en->a].sc == SC_AUTO && !cxx_gen_is_reference(c, a->syms[en->a].ty));
		if (!cxx_gen_prv_find(c, e))
			cxx_ctor_use(c, t, at, lv, 1, false);
		cc_id_list_init(&l);
		if (!cxx_gen_construct_at(c, target, t, &e, 1, &l, loc)) {
			cxx_gen_fail(c);
			return 0;
		}
		cc_id_list_append(a, &l, cc_statement_new(a, N_RETURN, loc, cc_node_rvalue(a, cxx_gen_var_node(c, c->gen_sret, loc)), 0, 0));
		return cc_statement_new(a, N_BLOCK, loc, 0, cc_id_list_freeze(a, &l), l.n);
	}
	if (has_value) {
		e = cxx_gen_bind(c, e, c->gen_ret, loc);
		if (!e) {
			cxx_gen_fail(c);
			return 0;
		}
	}
	return cc_statement_new(a, N_RETURN, loc, e, 0, 0);
}

/* a local variable of the body: its symbol (0: a type not generated yet) */
VPRIV uint32_t cxx_gen_local(cxx *c, uint32_t name, qty t, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t s;

	if (!cxx_gen_on(c))
		return 0;
	if (!cxx_gen_object_type_ok(c, t) && !(cxx_gen_is_reference(c, t) && cxx_gen_type_ok(c, t))) {
		cxx_gen_fail(c);
		return 0;
	}
	s = cc_symbol_new(a, S_VAR, name, t, loc);
	a->syms[s].sc = SC_AUTO;
	return s;
}

/* a local variable in static storage (static or constexpr): C's function-scope static, initialized by a
   constant (cxx_gen_local_declare checks it) */
VPRIV uint32_t cxx_gen_local_static(cxx *c, uint32_t name, qty t, uint32_t loc)
{
	uint32_t s = cxx_gen_local(c, name, t, loc);

	if (s)
		c->ast->syms[s].sc = SC_STATIC;
	return s;
}

VPRIV bool cxx_gen_needs_construction_public(cxx *c, qty t)
{
	return cxx_gen_needs_construction(c, t, 0) || cxx_gen_needs_destruction(c, t, 0);
}

/* its declaration statement, with the initializer's node (0: none), appended to the block being built */
VPRIV void cxx_gen_local_declare(cxx *c, uint32_t s, uint32_t init, bool has_init, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t n;

	if (!cxx_gen_on(c) || !s)
		return;
	if (has_init && !init) {
		cxx_gen_fail(c);
		return;
	}
	if (!c->gen_block) {
		cxx_gen_fail(c);	/* (a declaration that is not a block's statement: a condition's) */
		return;
	}
	if (has_init && AST_NODE_AT(a, init)->op != N_INIT && !cxx_gen_type_ok(c, a->syms[s].ty) &&
	    !(AST_NODE_AT(a, init)->op == N_STR && AST_TYPE_KIND(a, a->syms[s].ty) == TY_ARRAY)) {	/* (char s[] = "text": C's) */
		cxx_gen_fail(c);	/* (an array from something that is not a list) */
		return;
	}
	if (!has_init && cxx_gen_is_reference(c, a->syms[s].ty)) {
		cxx_gen_fail(c);
		return;
	}
	if (has_init) {
		if (AST_NODE_AT(a, init)->op != N_INIT && !(AST_NODE_AT(a, init)->op == N_STR && AST_TYPE_KIND(a, a->syms[s].ty) == TY_ARRAY))
			init = cxx_gen_bind(c, init, a->syms[s].ty, loc);	/* (char s[] = "text": the literal, as C's initializer takes it) */
		if (!init || (a->syms[s].sc == SC_STATIC && !cxx_gen_static_ok(a, a->syms[s].ty, init))) {
			cxx_gen_fail(c);	/* (a static local's dynamic initializer: its guard, not built yet) */
			return;
		}
		a->syms[s].body = init;
		a->syms[s].flags |= SF_DEFINED;
	}
	n = cc_node_new_operands(a, N_DECL, cc_type_basic(TY_VOID), loc, s, has_init ? init : 0);
	cc_id_list_append(a, (cc_id_list *)c->gen_block, n);
}

/*
 * T a[N] = { x, y, ... } with scalar elements: the initializer list's node from the last braced list's
 * elements (0: not built: nested lists, an array of unknown bound, too many elements).
 */
VPRIV uint32_t cxx_gen_array_initializer(cxx *c, qty t, uint32_t loc)
{
	cc *a = c->ast;
	const ctype *x;
	cc_id_list v;
	uint32_t n;

	if (!cxx_gen_on(c) || t == CXX_TY_UNKNOWN || AST_TYPE_KIND(a, t) != TY_ARRAY || c->brace_n == UINT32_MAX)
		return 0;
	x = AST_TYPE(a, t);
	if (!x->n || c->brace_n > x->n || !cxx_gen_type_ok(c, x->base))
		return 0;
	cc_id_list_init(&v);
	for (uint32_t i = 0; i < c->brace_n; i++) {
		cc_id_list_append(a, &v, i);
		cc_id_list_append(a, &v, i);
		cc_id_list_append(a, &v, cc_conversion_apply(a, cc_conversion_decay(a, c->brace_nodes[i]), QUNQ(x->base)));
	}
	n = cc_node_new(a, N_INIT, t, loc);
	AST_NODE_AT(a, n)->aux = 1;
	AST_NODE_AT(a, n)->c = v.n / 3;
	AST_NODE_AT(a, n)->b = cc_id_list_freeze(a, &v);
	return n;
}

/*
 * T x = { a, b } (or T x{a, b}) for an aggregate class whose members are scalars (no base, no union, no
 * bit-field, no constructor to run): its initializer list's node, a value per member, the ones not given
 * zero ([dcl.init.aggr]/5: their default member initializer: none here) (0: not built)
 */
VPRIV uint32_t cxx_gen_record_initializer(cxx *c, qty t, const uint32_t *nodes, uint32_t n, uint32_t loc)
{
	cc *a = c->ast;
	const tag *r;
	cxx_scope *cls;
	cc_id_list v;
	uint32_t init;

	if (!cxx_gen_on(c) || t == CXX_TY_UNKNOWN || AST_TYPE_KIND(a, t) != TY_STRUCT || n == UINT32_MAX || !cxx_gen_type_ok(c, QUNQ(t)) ||
	    cxx_gen_needs_construction(c, QUNQ(t), 0) || !(cls = cxx_tag_scope_get(c, QUNQ(t))) || cls->nused)
		return 0;
	r = &a->tags[AST_TYPE(a, t)->aux];
	if (n > r->nfield)
		return 0;
	cc_id_list_init(&v);
	for (uint32_t i = 0; i < r->nfield; i++) {
		qty ft = r->f[i].ty;
		cxx_entity *fe;

		if (r->f[i].bitfield || !r->f[i].name || ft == CXX_TY_UNKNOWN || cc_type_is_record(a, ft) || AST_TYPE_KIND(a, ft) == TY_ARRAY)
			return 0;
		fe = cxx_lookup_local(cls, r->f[i].name);
		if (fe && fe->nsdmi_pos && i >= n)
			return 0;	/* (a default member initializer: not read here) */
		if (i < n && !nodes[i])
			return 0;
		cc_id_list_append(a, &v, i < n ? cc_conversion_apply(a, cc_conversion_decay(a, nodes[i]), QUNQ(ft)) : 0);
	}
	init = cc_node_new(a, N_INIT, QUNQ(t), loc);
	AST_NODE_AT(a, init)->aux = 0;
	AST_NODE_AT(a, init)->c = v.n;
	AST_NODE_AT(a, init)->b = cc_id_list_freeze(a, &v);
	return init;
}

/* ---- variables at namespace scope ------------------------------------------------------------ */

/* a structured binding's name e: a local reference to member field of the variable object (its symbol),
   initialized with that member's address */
VPRIV void cxx_gen_bind_name(cxx *c, cxx_entity *e, uint32_t object, uint32_t field, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t x, m, s;
	qty rt;

	if (!cxx_gen_on(c) || !object || e->ty == CXX_TY_UNKNOWN)
		return;
	x = cxx_gen_var_node(c, object, loc);
	if (cxx_gen_is_reference(c, a->syms[object].ty))
		x = cc_dereference_build(a, cc_node_rvalue(a, x), loc);
	if (!(m = member(a, x, field, loc)))
		return;
	if (cxx_gen_is_reference(c, AST_NODE_AT(a, m)->ty))
		m = cc_dereference_build(a, cc_node_rvalue(a, m), loc);	/* (a reference member: its referent) */
	rt = cxx_type_reference(c, e->ty, false);
	if (!(s = cxx_gen_local(c, e->name, rt, loc)))
		return;
	e->sym = s;
	cxx_gen_local_declare(c, s, m, true, loc);	/* (bound as a reference is: the member lvalue) */
}
