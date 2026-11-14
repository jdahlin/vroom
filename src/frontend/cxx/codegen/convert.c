/* C++ code generation: temporaries, class arguments, binding values to objects, parameters and references; conversions */
#include "codegen.h"

/* T(args), T{args}: a temporary of class type, constructed (cxx_ctor_use was called for it) */
VPRIV void cxx_gen_construct_temp(cxx *c, qty t, const uint32_t *args, uint32_t nargs, uint32_t loc)
{
	cc *a = c->ast;
	cxx_entity *picked = c->ctor_picked;
	bool found = c->ctor_found, none = c->ctor_none;
	cc_id_list l;
	uint32_t tmp, node, expr, call = 0, zero = 0;

	if (!cxx_gen_on(c))
		return;
	c->enode = 0;
	t = QUNQ(t);
	if (nargs == UINT32_MAX || nargs > CXX_MAX_ARGS || !cxx_gen_class_ok2(c, t, true))
		return;
	for (uint32_t i = 0; i < nargs; i++)
		if (!args[i])
			return;
	if (nargs == 1 && cxx_gen_prv_find(c, args[0]) && QUNQ(AST_NODE_AT(a, args[0])->ty) == t) {
		c->enode = args[0];	/* T(a prvalue of T): that object */
		return;
	}
	tmp = cxx_gen_class_temp(c, t, loc);
	if (!tmp)
		return;
	c->ctor_picked = picked, c->ctor_found = found, c->ctor_none = none;
	cc_id_list_init(&l);
	if (!nargs && (!(found && picked && picked->member_of == cxx_tag_scope_get(c, t)) || picked->fn_defaulted)) {
		/* T() of a class without a user-provided default constructor: zeroes (then constructed, below, if
		   anything needs it) */
		uint32_t zargs[3], saved = c->enode;

		zargs[0] = cc_address_of_build(a, cxx_gen_var_node(c, tmp, loc), loc);
		zargs[1] = cc_node_integer_new(a, 0, cc_type_basic(TY_INT), loc);
		zargs[2] = cc_node_integer_new(a, (i128)AST_TYPE(a, t)->size, cc_type_basic(TY_ULONG), loc);
		c->enode = 0;
		cxx_gen_builtin(c, "__builtin_memset", 16, zargs, 3, loc);
		zero = c->enode;
		c->enode = saved;
		if (!zero)
			return;
		if (cxx_gen_needs_construction(c, t, 0)) {
			c->ctor_picked = picked, c->ctor_found = found, c->ctor_none = none;
			cc_id_list_append(a, &l, cc_statement_new(a, N_EXPR, loc, zero, 0, 0));
			zero = 0;
			if (!cxx_gen_construct_at(c, cxx_gen_var_node(c, tmp, loc), t, args, nargs, &l, loc)) {
				cxx_gen_fail(c);
				return;
			}
		}
	} else if (!cxx_gen_construct_at(c, cxx_gen_var_node(c, tmp, loc), t, args, nargs, &l, loc)) {
		cxx_gen_fail(c);	/* (the temporary is declared: nothing of this function is kept) */
		return;
	}
	expr = zero ? zero : cxx_gen_chain(c, &l, 0, loc);
	if (!zero && l.n == 1 && AST_NODE_AT(a, expr)->op == N_CALL && AST_NODE_AT(a, expr)->c)
		call = expr;
	node = cc_address_of_build(a, cxx_gen_var_node(c, tmp, loc), loc);
	if (expr)
		node = cc_node_new_operands(a, N_COMMA, AST_NODE_AT(a, node)->ty, loc, expr, node);
	node = cc_dereference_build(a, node, loc);
	if (call)
		cxx_gen_prv_add(c, node, expr, call, tmp);
	else
		cxx_gen_prv_add(c, node, 0, 0, tmp);
	c->enode = node;
}

/* an argument for a parameter of a class passed by address: the address of the temporary the argument is,
   or of a temporary copied from the argument (0: not built) */
VPRIV uint32_t cxx_gen_class_argument(cxx *c, uint32_t x, qty pt, uint32_t loc)
{
	cc *a = c->ast;
	qty t = QUNQ(pt), at[1];
	uint8_t lv[1];
	cxx_entity *call_picked = c->call_picked;
	uint8_t call_resolved = c->call_resolved, none_viable = c->none_viable, elval = c->elval, econst = c->econst, einvalid = c->einvalid;
	qty etype = c->etype;
	int64_t evalue = c->evalue;
	cxx_entity *picked;
	bool found, none;
	cc_id_list l;
	uint32_t tmp;

	if (!x)
		return 0;
	if (QUNQ(AST_NODE_AT(a, x)->ty) == t && cxx_gen_prv_find(c, x))
		return cc_address_of_build(a, x, loc);
	if (QUNQ(AST_NODE_AT(a, x)->ty) == t && !(AST_NODE_AT(a, x)->flags & NF_LVALUE))
		return 0;	/* (a prvalue of the class that is not a call's: not built yet) */
	{
		/* an object of a class derived from the parameter's: its base subobject, copied (priority_tag<4>{} for
		   priority_tag<1>) */
		qty xt = QUNQ(AST_NODE_AT(a, x)->ty);
		uint64_t off;

		if (xt != t && cc_type_is_record(a, xt) && AST_TYPE_KIND(a, xt) != TY_ARRAY && (AST_NODE_AT(a, x)->flags & NF_LVALUE) &&
		    cxx_class_base_offset(c, xt, t, &off, 0) && !(x = cxx_gen_base_object(c, x, t, loc)))
			return 0;
	}
	/* the parameter copy-initialized from the argument: a copy, or a converting constructor from a scalar,
	   pointer or array (not an explicit one: json from 3 or "text" for operator=(basic_json)) */
	/* (a call's result is a prvalue though its node is an object here: CompatibleType&& must not deduce T&) */
	at[0] = AST_NODE_AT(a, x)->ty, lv[0] = (AST_NODE_AT(a, x)->flags & NF_LVALUE) != 0 && !cxx_gen_prv_find(c, x);
	{
		/* nullptr (vcc's node: 0 as a void *): a std::nullptr_t prvalue (std::function cb = nullptr) */
		if (cxx_gen_is_nullptr_node(c, x)) {
			at[0] = c->nullptr_ty, lv[0] = 0;
			if (!(x = cxx_gen_nullptr_object(c, x, loc)))
				return 0;
		}
	}
	c->skip_explicit = QUNQ(at[0]) != t;
	cxx_ctor_use(c, t, at, lv, 1, false);
	c->skip_explicit = 0;
	picked = c->ctor_picked, found = c->ctor_found, none = c->ctor_none;
	c->call_picked = call_picked, c->call_resolved = call_resolved, c->none_viable = none_viable;
	c->etype = etype, c->elval = elval, c->econst = econst, c->evalue = evalue, c->einvalid = einvalid;
	tmp = cxx_gen_class_temp(c, t, loc);
	if (!tmp)
		return 0;
	c->ctor_picked = picked, c->ctor_found = found, c->ctor_none = none;
	cc_id_list_init(&l);
	if (!cxx_gen_construct_at(c, cxx_gen_var_node(c, tmp, loc), t, &x, 1, &l, loc)) {
		cxx_gen_fail(c);
		return 0;
	}
	return cxx_gen_chain(c, &l, cc_address_of_build(a, cxx_gen_var_node(c, tmp, loc), loc), loc);
}

VPRIV void cxx_gen_call(cxx *c, cxx_entity *f, uint32_t object, bool arrow, const uint32_t *args, uint32_t nargs, uint32_t loc);

/* an object of class type as a value of the scalar type `to`: its conversion function to exactly that type
   (operator bool for a condition); 0: none, or not built */
VPRIV uint32_t cxx_gen_convert_class(cxx *c, uint32_t x, qty to, uint32_t loc)
{
	cc *a = c->ast;
	cxx_scope *cls;
	cxx_entity *f;
	char nm[64] = { 0 };
	uint32_t saved = c->enode, n;

	if (!x || !cc_type_is_record(a, AST_NODE_AT(a, x)->ty) || AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) == TY_ARRAY ||
	    !(AST_NODE_AT(a, x)->flags & NF_LVALUE) || !(cls = cxx_tag_scope_get(c, QUNQ(AST_NODE_AT(a, x)->ty))))
		return 0;
	snprintf(nm, 32, "operator #%u", (unsigned)QUNQ(to));
	f = cxx_lookup_in(c, cls, vintern_cstr(nm), 0);
	if (!f || f->kind != CXX_ENTITY_VALUE || f->next || f->fn_deleted)
		return 0;
	if (f->late && !f->late_done)
		cxx_member_body_use(c, f);
	c->enode = 0;
	cxx_gen_call(c, f, x, false, NULL, 0, loc);
	n = c->enode;
	c->enode = saved;
	return n;
}

/*
 * The value x for an object, parameter or result of type to: converted for a scalar, as it is for a class,
 * and for a reference the address of the lvalue it binds to (for a const T& or T&& bound to a value: of a
 * temporary holding it).  0: not built.
 */
/* nullptr (vcc's node: 0 cast to void *) */
VPRIV bool cxx_gen_is_nullptr_node(cxx *c, uint32_t x)
{
	cc *a = c->ast;
	const struct node *nx = AST_NODE_AT(a, x);

	return c->nullptr_ty && nx->op == N_CAST && AST_TYPE_KIND(a, nx->ty) == TY_PTR && AST_TYPE(a, nx->ty)->base == cc_type_basic(TY_VOID) &&
	       nx->a && AST_NODE_AT(a, nx->a)->op == N_NUM && a->ipool[AST_NODE_AT(a, nx->a)->a] == 0;
}

/* a nullptr_t object holding pointer value x (a temporary, an lvalue): *(void **)&tmp = x, tmp */
VPRIV uint32_t cxx_gen_nullptr_object(cxx *c, uint32_t x, uint32_t loc)
{
	cc *a = c->ast;
	qty vp = cc_type_pointer(a, cc_type_basic(TY_VOID));
	uint32_t tmp = cxx_gen_temporary(c, c->nullptr_ty, loc), tv, slot, asg;

	if (!tmp)
		return 0;
	tv = cc_node_new(a, N_VAR, a->syms[tmp].ty, loc);
	AST_NODE_AT(a, tv)->a = tmp, AST_NODE_AT(a, tv)->flags = NF_LVALUE;
	slot = cc_dereference_build(a, cc_cast_insert(a, cc_address_of_build(a, tv, loc), cc_type_pointer(a, vp), CK_NOP), loc);
	asg = cc_assignment_build(a, 0, slot, cc_conversion_apply(a, cc_node_rvalue(a, x), vp), loc);
	tv = cc_node_new(a, N_VAR, a->syms[tmp].ty, loc);
	AST_NODE_AT(a, tv)->a = tmp, AST_NODE_AT(a, tv)->flags = NF_LVALUE;
	x = cc_node_new_operands(a, N_COMMA, a->syms[tmp].ty, loc, asg, tv);
	AST_NODE_AT(a, x)->flags |= NF_LVALUE;
	return x;
}

VPRIV uint32_t cxx_gen_bind(cxx *c, uint32_t x, qty to, uint32_t loc)
{
	cc *a = c->ast;

	if (!x)
		return 0;
	if (c->nullptr_ty && AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) == TY_PTR && !AST_TYPE(a, AST_NODE_AT(a, x)->ty)->ext &&
	    QUNQ(cxx_gen_is_reference(c, to) ? AST_TYPE(a, to)->base : to) == c->nullptr_ty) {
		/* nullptr (a void * here) to a std::nullptr_t parameter: an object of that type */
		x = cxx_gen_nullptr_object(c, x, loc);
		if (!x)
			return 0;
		if (!cxx_gen_is_reference(c, to))
			return cc_node_rvalue(a, x);
		return cc_cast_insert(a, cc_address_of_build(a, x, loc), QUNQ(to), CK_NOP);
	}
	if (c->nullptr_ty && QUNQ(AST_NODE_AT(a, x)->ty) == c->nullptr_ty && (AST_NODE_AT(a, x)->flags & NF_LVALUE) &&
	    AST_TYPE_KIND(a, to) == TY_PTR && !cxx_gen_is_reference(c, to)) {
		/* a std::nullptr_t object to a pointer: the void * it holds */
		qty vp = cc_type_pointer(a, cc_type_basic(TY_VOID));

		x = cc_dereference_build(a, cc_cast_insert(a, cc_address_of_build(a, x, loc), cc_type_pointer(a, vp), CK_NOP), loc);
		return cc_conversion_apply(a, cc_node_rvalue(a, x), QUNQ(to));
	}
	if (c->nullptr_ty && QUNQ(AST_NODE_AT(a, x)->ty) == c->nullptr_ty && AST_TYPE_KIND(a, to) == TY_PTR && !cxx_gen_is_reference(c, to)) {
		/* ... one that is no lvalue (std::forward<nullptr_t>(v)): evaluated, and a null pointer (its only value) */
		qty pt = QUNQ(to);
		uint32_t z = cc_cast_insert(a, cc_node_integer_new(a, 0, cc_type_basic(TY_LONG), loc), pt, CK_I2P);

		return cc_node_new_operands(a, N_COMMA, pt, loc, x, z);
	}
	if (cxx_gen_is_reference(c, to)) {
		qty rt = AST_TYPE(a, to)->base;

		if (AST_NODE_AT(a, x)->op == N_ASSIGN && !cc_type_is_record(a, rt) && AST_NODE_AT(a, x)->a &&
		    (AST_NODE_AT(a, AST_NODE_AT(a, x)->a)->flags & NF_LVALUE) && QUNQ(AST_NODE_AT(a, AST_NODE_AT(a, x)->a)->ty) == QUNQ(rt)) {
			/* l = r (or l op= r) bound to a reference: C++'s assignment is l itself, an lvalue (return l = l | r):
			   (p = &l, *p op= r, p), l evaluated once */
			const node *an = AST_NODE_AT(a, x);
			qty pt = cc_type_pointer(a, AST_NODE_AT(a, an->a)->ty);
			uint32_t p = cxx_gen_temporary(c, pt, loc), set, asg;

			if (!p)
				return 0;
			set = cc_assignment_build(a, 0, cxx_gen_var_node(c, p, loc), cc_address_of_build(a, an->a, loc), loc);
			asg = cc_assignment_build(a, an->aux, cc_dereference_build(a, cc_node_rvalue(a, cxx_gen_var_node(c, p, loc)), loc), an->b, loc);
			x = cc_node_new_operands(a, N_COMMA, pt, loc, set,
						 cc_node_new_operands(a, N_COMMA, pt, loc, asg, cc_node_rvalue(a, cxx_gen_var_node(c, p, loc))));
			return cc_cast_insert(a, x, QUNQ(to), CK_NOP);
		}

		if (cc_type_is_record(a, rt) && AST_TYPE_KIND(a, rt) != TY_ARRAY &&
		    (!cc_type_is_record(a, AST_NODE_AT(a, x)->ty) || AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) == TY_ARRAY)) {
			/* a reference to a class bound to a value of another type ("text" for a const std::string &): a
			   temporary made by the class's converting constructor */
			qty at[1] = { AST_NODE_AT(a, x)->ty };
			uint8_t lv[1] = { (AST_NODE_AT(a, x)->flags & NF_LVALUE) != 0 };
			uint32_t saved = c->enode, tmp;

			if (AST_TYPE(a, to)->ext == 1 && !(QQ(rt) & Q_CONST))
				return 0;	/* (T& does not bind a temporary) */
			if (AST_TYPE_KIND(a, at[0]) == TY_ARRAY)	/* ("text": a pointer, as for a call) */
				at[0] = cc_type_pointer(a, AST_TYPE(a, at[0])->base), lv[0] = 0;
			if (cxx_gen_is_nullptr_node(c, x)) {	/* (nullptr: a std::nullptr_t, not a void *) */
				at[0] = c->nullptr_ty, lv[0] = 0;
				if (!(x = cxx_gen_nullptr_object(c, x, loc)))
					return 0;
			}
			cxx_ctor_use(c, QUNQ(rt), at, lv, 1, false);
			if (!c->ctor_found || !c->ctor_picked || c->ctor_picked->fn_explicit)
				return 0;
			c->enode = 0;
			cxx_gen_construct_temp(c, QUNQ(rt), &x, 1, loc);
			tmp = c->enode;
			c->enode = saved;
			if (!tmp || !(AST_NODE_AT(a, tmp)->flags & NF_LVALUE))
				return 0;
			return cc_cast_insert(a, cc_address_of_build(a, tmp, loc), QUNQ(to), CK_NOP);
		}
		if ((AST_NODE_AT(a, x)->flags & NF_LVALUE) && cc_type_is_record(a, rt) && cc_type_is_record(a, AST_NODE_AT(a, x)->ty) &&
		    QUNQ(AST_NODE_AT(a, x)->ty) != QUNQ(rt)) {
			/* a reference to a base class bound to a derived object: its base subobject; to another class:
			   the object its conversion function to that class gives (operator Result()) */
			qty from = QUNQ(AST_NODE_AT(a, x)->ty);
			uint64_t off;

			if (cxx_class_base_offset(c, from, QUNQ(rt), &off, 0))
				x = cxx_gen_base_object(c, x, QUNQ(rt), loc);
			else
				x = cxx_gen_convert_class(c, x, QUNQ(rt), loc);
			if (!x)
				return 0;
		}
		if (!(AST_NODE_AT(a, x)->flags & NF_LVALUE) || (cxx_gen_type_ok(c, rt) && !cc_type_is_record(a, rt) &&
								  QUNQ(AST_NODE_AT(a, x)->ty) != QUNQ(rt))) {
			/* a value (or an lvalue of another type, converted): a temporary */
			uint32_t tmp, tv, asg;

			if (AST_TYPE(a, to)->ext == 1 && !(QQ(rt) & Q_CONST))
				return 0;	/* (T& does not bind a value) */
			if (!cxx_gen_object_type_ok(c, rt) || AST_TYPE_KIND(a, rt) == TY_ARRAY)
				return 0;
			if (cc_type_is_record(a, rt) != cc_type_is_record(a, AST_NODE_AT(a, x)->ty) ||
			    (cc_type_is_record(a, rt) && QUNQ(AST_NODE_AT(a, x)->ty) != QUNQ(rt)))
				return 0;	/* (a class from another type: a converting constructor or conversion function, not built yet) */
			tmp = cxx_gen_temporary(c, rt, loc);
			if (!tmp)
				return 0;
			tv = cc_node_new(a, N_VAR, a->syms[tmp].ty, loc);
			AST_NODE_AT(a, tv)->a = tmp;
			AST_NODE_AT(a, tv)->flags = NF_LVALUE;
			asg = cc_assignment_build(a, 0, tv, cc_type_is_record(a, rt) ? x : cc_conversion_apply(a, cc_conversion_decay(a, x), QUNQ(rt)), loc);
			tv = cc_node_new(a, N_VAR, a->syms[tmp].ty, loc);
			AST_NODE_AT(a, tv)->a = tmp;
			AST_NODE_AT(a, tv)->flags = NF_LVALUE;
			x = cc_node_new_operands(a, N_COMMA, cc_type_pointer(a, a->syms[tmp].ty), loc, asg, cc_address_of_build(a, tv, loc));
		} else
			x = cc_address_of_build(a, x, loc);
		return cc_cast_insert(a, x, QUNQ(to), CK_NOP);
	}
	if (cc_type_is_record(a, to)) {
		if (QUNQ(AST_NODE_AT(a, x)->ty) == QUNQ(to))
			return cc_node_rvalue(a, x);
		if (cc_type_is_record(a, AST_NODE_AT(a, x)->ty) && AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) != TY_ARRAY) {
			/* another class's object: its conversion function to this class (strong_ordering to partial_ordering) */
			uint32_t v = cxx_gen_convert_class(c, x, QUNQ(to), loc);

			if (v && QUNQ(AST_NODE_AT(a, v)->ty) == QUNQ(to))
				return cc_node_rvalue(a, v);
			/* (else: this class's converting constructor from it, below; __map_iterator(__tree_iterator)) */
		}
		{
			/* a value of another type: the class's converting constructor (0 for _CmpUnspecifiedParam) */
			qty at[1] = { AST_NODE_AT(a, x)->ty };
			uint8_t lv[1] = { (AST_NODE_AT(a, x)->flags & NF_LVALUE) != 0 };
			uint32_t saved = c->enode, tmp;

			if (AST_TYPE_KIND(a, at[0]) == TY_ARRAY)
				at[0] = cc_type_pointer(a, AST_TYPE(a, at[0])->base), lv[0] = 0;
			if (cxx_gen_is_nullptr_node(c, x)) {	/* (nullptr: a std::nullptr_t, not a void *) */
				at[0] = c->nullptr_ty, lv[0] = 0;
				if (!(x = cxx_gen_nullptr_object(c, x, loc)))
					return 0;
			}
			cxx_ctor_use(c, QUNQ(to), at, lv, 1, false);
			if (!c->ctor_found || !c->ctor_picked || c->ctor_picked->fn_explicit)
				return 0;
			c->enode = 0;
			cxx_gen_construct_temp(c, QUNQ(to), &x, 1, loc);
			tmp = c->enode;
			c->enode = saved;
			return tmp ? cc_node_rvalue(a, tmp) : 0;
		}
	}
	if (cc_type_is_record(a, AST_NODE_AT(a, x)->ty) && AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) != TY_ARRAY) {
		x = cxx_gen_convert_class(c, x, to, loc);	/* a class to a scalar: its conversion function to that type */
		if (!x)
			return 0;
	}
	x = cc_conversion_decay(a, x);
	if (AST_TYPE_KIND(a, to) == TY_PTR && AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) == TY_PTR) {
		/* Derived* to Base*: the pointer moved to the base subobject */
		qty tb = AST_TYPE(a, to)->base, fb = AST_TYPE(a, AST_NODE_AT(a, x)->ty)->base;

		if (cc_type_is_record(a, tb) && cc_type_is_record(a, fb) && QUNQ(tb) != QUNQ(fb)) {
			x = cxx_gen_base_pointer(c, x, tb, loc);
			return x ? cc_cast_insert(a, x, QUNQ(to), CK_NOP) : 0;
		}
	}
	return cc_conversion_apply(a, x, QUNQ(to));
}

/* a reference as an expression: the lvalue it refers to */
VPRIV uint32_t cxx_gen_referent(cxx *c, uint32_t x, uint32_t loc)
{
	cc *a = c->ast;
	qty t = AST_NODE_AT(a, x)->ty;

	x = cc_node_rvalue(a, x);
	x = cc_cast_insert(a, x, cc_type_pointer(a, AST_TYPE(a, t)->base), CK_NOP);	/* (a plain pointer to the referent) */
	return cc_dereference_build(a, x, loc);
}

/*
 * A pointer to an object of class type as a pointer to its base class subobject (to: the base class, with
 * the cv of the pointee): the pointer moved by the base's offset.  0: not a base of it.
 */
VPRIV uint32_t cxx_gen_base_pointer(cxx *c, uint32_t p, qty to, uint32_t loc)
{
	cc *a = c->ast;
	qty pt = AST_NODE_AT(a, p)->ty, from;
	uint64_t off;

	if (AST_TYPE_KIND(a, pt) != TY_PTR)
		return 0;
	from = AST_TYPE(a, pt)->base;
	if (QUNQ(from) == QUNQ(to))
		return cc_cast_insert(a, p, cc_type_pointer(a, to), CK_NOP);
	if (!cxx_class_base_offset(c, QUNQ(from), QUNQ(to), &off, 0))
		return 0;
	if (off) {
		p = cc_cast_insert(a, p, cc_type_pointer(a, cc_type_basic(TY_CHAR)), CK_NOP);
		p = cc_binary_build(a, LEX_TOKEN_PLUS, p, cc_node_integer_new(a, (i128)off, cc_type_basic(TY_LONG), loc), loc);
	}
	return cc_cast_insert(a, p, cc_type_pointer(a, to), CK_NOP);
}

/* an lvalue of class type as its base class subobject (0: not a base of it) */
VPRIV uint32_t cxx_gen_base_object(cxx *c, uint32_t x, qty to, uint32_t loc)
{
	cc *a = c->ast;
	qty xt = AST_NODE_AT(a, x)->ty;
	uint32_t p;

	if (!(AST_NODE_AT(a, x)->flags & NF_LVALUE))
		return 0;
	p = cxx_gen_base_pointer(c, cc_address_of_build(a, x, loc), QT(QI(to), QQ(to) | QQ(xt)), loc);
	return p ? cc_dereference_build(a, p, loc) : 0;
}

/* the member `name` of the object x: a field of its class, or of a base class (the subobject first) */
VPRIV uint32_t cxx_gen_member_of(cxx *c, uint32_t x, uint32_t name, uint32_t loc, int depth)
{
	cc *a = c->ast;
	qty bt = AST_NODE_AT(a, x)->ty, bases[16];
	uint64_t bits;
	uint32_t nb;

	if (!cc_type_is_record(a, bt) || !(a->tags[AST_TYPE(a, bt)->aux].flags & RF_COMPLETE) || depth > 8)
		return 0;
	if (cxx_class_field_offset(c, bt, name, &bits, 0))
		return member(a, x, name, loc);
	nb = cxx_class_bases(c, bt, bases, 16);
	for (uint32_t i = 0; i < nb; i++) {
		uint32_t sub = cxx_gen_base_object(c, x, bases[i], loc), m;

		if (sub && (m = cxx_gen_member_of(c, sub, name, loc, depth + 1)))
			return m;
	}
	return 0;
}
