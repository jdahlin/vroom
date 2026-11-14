/* C++ code generation: literals, variables and the built-in operators */
#include "codegen.h"

VPRIV void cxx_gen_integer(cxx *c, int64_t v, qty t, uint32_t loc)
{
	if (cxx_gen_on(c) && cxx_gen_type_ok(c, t))
		c->enode = cc_node_integer_new(c->ast, (i128)v, t, loc);
}

/* a floating literal (its spelling: decimal or hexadecimal, digit separators, a suffix f, l) */
VPRIV void cxx_gen_floating(cxx *c, const char *text, size_t len, qty t, uint32_t loc)
{
	char buf[128];
	size_t n = 0;
	int k = t != CXX_TY_UNKNOWN ? AST_TYPE_KIND(c->ast, t) : -1;

	if (!cxx_gen_on(c) || (k != TY_FLOAT && k != TY_DOUBLE && k != TY_LDOUBLE) || len >= sizeof buf)
		return;
	for (size_t i = 0; i < len; i++)
		if (text[i] != '\'')
			buf[n++] = text[i];
	buf[n] = 0;
	if (cc_type_is_float128(c->ast, t)) {
		/* a binary128 long double (Arm Linux): parsed to its own precision, as C does */
		size_t m = n;

		while (m && (buf[m - 1] == 'l' || buf[m - 1] == 'L'))
			m--;
		c->enode = cc_node_float128_new(c->ast, vsoftfloat_parse_with_precision(buf, m, cc_float_precision_bits(c->ast, t)), t, loc);
		return;
	}
	c->enode = cc_node_float_new(c->ast, k == TY_FLOAT ? (double)strtof(buf, NULL) : strtod(buf, NULL), t, loc);	/* (strto* stop at the suffix) */
}

/* ---- lambdas ------------------------------------------------------------------------------- */

/* a variable (a parameter, a local, one at namespace scope) by name */
VPRIV void cxx_gen_variable(cxx *c, const cxx_entity *v, uint32_t loc)
{
	uint32_t n;

	if (!cxx_gen_on(c) || !v->sym || !(cxx_gen_object_type_ok(c, v->ty) || cxx_gen_type_ok(c, v->ty)))
		return;
	if (v->has_value && v->ty != CXX_TY_UNKNOWN && (QQ(v->ty) & Q_CONST) && !c->ast->syms[v->sym].depth &&
	    !(c->ast->syms[v->sym].flags & (SF_DEFINED | SF_TENTATIVE)) &&
	    ((AST_TYPE_KIND(c->ast, v->ty) >= TY_BOOL && AST_TYPE_KIND(c->ast, v->ty) <= TY_ULLONG) || AST_TYPE_KIND(c->ast, v->ty) == TY_ENUM)) {
		/* a constant at namespace scope (constexpr, a const integer) with no object in this TU: its value */
		c->enode = cc_node_integer_new(c->ast, (i128)v->value, QUNQ(v->ty), loc);
		return;
	}
	if (c->gen_lambda && v->sym < c->gen_lambda->floor && c->ast->syms[v->sym].sc == SC_AUTO) {
		/* in a lambda's body: a variable of an enclosing function is a capture */
		int k = cxx_gen_capture(c, c->gen_lambda, v, loc);

		if (k < 0) {
			cxx_gen_fail(c);
			return;
		}
		c->enode = cc_dereference_build(c->ast, cc_node_rvalue(c->ast, cxx_gen_var_node(c, c->gen_lambda->cap[k].local, loc)), loc);
		return;
	}
	n = cc_node_new(c->ast, N_VAR, c->ast->syms[v->sym].ty, loc);
	AST_NODE_AT(c->ast, n)->a = v->sym;
	AST_NODE_AT(c->ast, n)->flags = NF_LVALUE;
	if (cxx_gen_is_reference(c, c->ast->syms[v->sym].ty))
		n = cxx_gen_referent(c, n, loc);	/* (a reference names what it refers to) */
	c->enode = n;
}

/* a static constant data member with no object here (a class template's): its initializer, parsed again
   where it is used, as a value of its type (0: not built) */
VPRIV uint32_t cxx_gen_static_member_value(cxx *c, const cxx_entity *v, uint32_t loc)
{
	jmp_buf jb, *outer_fail = c->fail;
	cxx_scope *scope = c->scope;
	uint32_t pos = c->pos, enode = c->enode, eobj = c->eobj_node;
	qty etype = c->etype;
	uint8_t elval = c->elval, econst = c->econst, einvalid = c->einvalid, call_resolved = c->call_resolved, none_viable = c->none_viable;
	uint8_t eobj_arrow = c->eobj_arrow, eunsure = c->eunsure, edecl = c->edecl_name;
	int64_t evalue = c->evalue;
	cxx_entity *call_picked = c->call_picked, *ecallee = c->ecallee;
	volatile uint32_t node = 0;
	static __thread int depth;

	bool record = v->ty != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, v->ty) && AST_TYPE_KIND(c->ast, v->ty) != TY_ARRAY;

	if (!cxx_gen_on(c) || !v->nsdmi_pos || !v->nsdmi_scope || v->ty == CXX_TY_UNKNOWN || (!record && !cxx_gen_type_ok(c, v->ty)) ||
	    cxx_gen_is_reference(c, v->ty) || depth > 8 || (record && !cxx_gen_class_ok2(c, v->ty, true)))
		return 0;
	depth++;
	c->pos = v->nsdmi_pos + (c->tok[v->nsdmi_pos].kind == LEX_TOKEN_EQUAL), c->scope = v->nsdmi_scope, c->fail = &jb;
	if (!setjmp(jb)) {
		if (record && (cxx_is(c, LEX_TOKEN_LEFT_PAREN) || cxx_is(c, LEX_TOKEN_LEFT_BRACE))) {
			/* T X::m(args), T X::m{args}: a temporary made the same way (a value use: its copy is the object's) */
			uint32_t an[CXX_MAX_ARGS], n;
			bool braced = cxx_is(c, LEX_TOKEN_LEFT_BRACE);

			if (braced)
				cxx_braced_arguments_parse(c);
			else
				cxx_call_arguments_parse(c);
			n = c->ncargs;
			if (n <= CXX_MAX_ARGS) {
				memcpy(an, c->carg_nodes, n * sizeof *an);
				cxx_ctor_use(c, QUNQ(v->ty), c->cargs, c->cargs_lval, n, braced);
				cxx_gen_construct_temp(c, QUNQ(v->ty), an, n, loc);
				node = c->enode;
			}
		} else {
			cxx_assignment_expression_parse(c);
			if (c->enode && (cxx_is(c, LEX_TOKEN_SEMICOLON) || cxx_is(c, LEX_TOKEN_COMMA)))
				node = record ? (QUNQ(AST_NODE_AT(c->ast, c->enode)->ty) == QUNQ(v->ty) ? c->enode : 0) :
					cc_conversion_apply(c->ast, cc_conversion_decay(c->ast, c->enode), QUNQ(v->ty));
		}
	}
	depth--;
	c->fail = outer_fail, c->pos = pos, c->scope = scope;
	c->enode = enode, c->eobj_node = eobj, c->eobj_arrow = eobj_arrow, c->etype = etype, c->elval = elval, c->econst = econst;
	c->evalue = evalue, c->einvalid = einvalid, c->eunsure = eunsure, c->call_resolved = call_resolved, c->none_viable = none_viable;
	c->call_picked = call_picked, c->ecallee = ecallee, c->edecl_name = edecl;
	return node;
}

/* op x with the built-in operator: + - ~ ! * & ++ -- (prefix) */
VPRIV void cxx_gen_unary(cxx *c, int op, uint32_t x, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t r;

	if (!cxx_gen_on(c) || !x)
		return;
	if (cc_type_is_record(a, AST_NODE_AT(a, x)->ty) && op == LEX_TOKEN_EXCLAMATION) {
		x = cxx_gen_convert_class(c, x, cc_type_basic(TY_BOOL), loc);	/* !x: x's operator bool */
		if (!x)
			return;
	}
	if (cc_type_is_record(a, AST_NODE_AT(a, x)->ty) && op != LEX_TOKEN_AMPERSAND)
		return;	/* (a class operand of a built-in operator: a conversion function, not built yet) */
	switch (op) {
	case LEX_TOKEN_PLUS: case LEX_TOKEN_MINUS: case LEX_TOKEN_TILDE:
		x = cc_node_rvalue(a, x);
		if (!cc_type_is_arithmetic(a, AST_NODE_AT(a, x)->ty))
			return;
		x = cc_conversion_apply(a, x, cc_node_promoted_type(a, x));
		r = cc_node_new(a, N_UNARY, AST_NODE_AT(a, x)->ty, loc);
		AST_NODE_AT(a, r)->aux = (uint16_t)op;
		AST_NODE_AT(a, r)->a = x;
		break;
	case LEX_TOKEN_EXCLAMATION:
		x = cc_condition_scalar(a, cc_node_rvalue(a, x), "'!'");
		r = cc_node_new(a, N_UNARY, cc_type_basic(TY_INT), loc);
		AST_NODE_AT(a, r)->aux = LEX_TOKEN_EXCLAMATION;
		AST_NODE_AT(a, r)->a = x;
		break;
	case LEX_TOKEN_STAR:
		r = cc_dereference_build(a, x, loc);
		break;
	case LEX_TOKEN_AMPERSAND:
		if (!(AST_NODE_AT(a, x)->flags & NF_LVALUE))
			return;
		r = cc_address_of_build(a, x, loc);
		break;
	case LEX_TOKEN_PLUS_PLUS: case LEX_TOKEN_MINUS_MINUS:
		if (!(AST_NODE_AT(a, x)->flags & NF_LVALUE))
			return;
		r = cc_node_new(a, op == LEX_TOKEN_PLUS_PLUS ? N_PREINC : N_PREDEC, QUNQ(AST_NODE_AT(a, x)->ty), loc);
		AST_NODE_AT(a, r)->a = x;
		if (AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) != TY_BOOL)
			AST_NODE_AT(a, r)->flags |= NF_LVALUE;	/* (C++: ++x is an lvalue) */
		break;
	default:
		return;
	}
	c->enode = r;
}

/* x++ or x-- */
VPRIV void cxx_gen_postfix(cxx *c, int op, uint32_t x, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t r;

	if (!cxx_gen_on(c) || !x || !(AST_NODE_AT(a, x)->flags & NF_LVALUE))
		return;
	r = cc_node_new(a, op == LEX_TOKEN_PLUS_PLUS ? N_POSTINC : N_POSTDEC, QUNQ(AST_NODE_AT(a, x)->ty), loc);
	AST_NODE_AT(a, r)->a = x;
	c->enode = r;
}

/* a string literal: its bytes (no escapes), NUL-terminated */
VPRIV void cxx_gen_string(cxx *c, const char *text, size_t len, uint32_t loc)
{
	cc *a = c->ast;
	char *buf;
	uint32_t si, n;

	if (!cxx_gen_on(c))
		return;
	buf = cc_heap_allocate(a, len + 1);
	memcpy(buf, text, len);
	buf[len] = 0;
	si = a->nstr++;
	a->strs[si] = (cstr){ buf, (uint32_t)len + 1, 1, LEX_TOKEN_STRING };
	n = cc_node_new(a, N_STR, cc_type_array(a, QT(TY_CHAR, Q_CONST), (uint64_t)len + 1, 0), loc);
	AST_NODE_AT(a, n)->a = si;
	AST_NODE_AT(a, n)->flags = NF_LVALUE;
	c->enode = n;
}

/* (T)x, static_cast<T>(x), T(x) between scalar types */
VPRIV void cxx_gen_cast(cxx *c, uint32_t x, qty to, uint32_t loc)
{
	cc *a = c->ast;

	(void)loc;
	if (!cxx_gen_on(c) || !x || to == CXX_TY_UNKNOWN)
		return;
	if (AST_TYPE_KIND(a, to) == TY_VOID) {	/* (void)x */
		c->enode = cc_cast_insert(a, cc_node_rvalue(a, x), cc_type_basic(TY_VOID), CK_VOID);
		return;
	}
	if (cxx_gen_is_reference(c, to)) {
		/* static_cast<T&&>(x), static_cast<T&>(x), const_cast<T&>(x) of an lvalue of that type: the same object
		   (a class: the same record, however its type is spelled; const dropped through its address) */
		qty xt = QUNQ(AST_NODE_AT(a, x)->ty), bt = QUNQ(AST_TYPE(a, to)->base);
		bool same = xt == bt || (cc_type_is_record(a, xt) && cc_type_is_record(a, bt) && AST_TYPE_KIND(a, xt) == AST_TYPE_KIND(a, bt) &&
					 AST_TYPE(a, xt)->aux == AST_TYPE(a, bt)->aux);

		if ((AST_NODE_AT(a, x)->flags & NF_LVALUE) && same) {
			if (AST_NODE_AT(a, x)->ty != AST_TYPE(a, to)->base) {
				qty pt = cc_type_pointer(a, AST_TYPE(a, to)->base);
				uint32_t addr = cc_address_of_build(a, x, loc);

				x = cc_dereference_build(a, cc_cast_insert(a, addr, pt, CK_NOP), loc);
			}
			c->enode = x;
		}
		return;
	}
	if (!cxx_gen_type_ok(c, to) || cc_type_is_record(a, to))
		return;
	if (cc_type_is_record(a, AST_NODE_AT(a, x)->ty) && AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) != TY_ARRAY) {
		/* static_cast<E>(obj): the class's conversion function to exactly that type */
		uint32_t v = cxx_gen_convert_class(c, x, QUNQ(to), loc);

		if (v)
			c->enode = v;
		return;
	}
	x = cc_conversion_decay(a, x);
	if (!cxx_gen_type_ok(c, AST_NODE_AT(a, x)->ty) || cc_type_is_record(a, AST_NODE_AT(a, x)->ty))
		return;
	c->enode = cc_cast_insert(a, x, QUNQ(to), cc_conversion_cast_kind(a, AST_NODE_AT(a, x)->ty, QUNQ(to), x));
}

/* an object of an empty class with no node (a constexpr reference to a function object, json's to_json):
   a temporary of it does as well, nothing in it being read (0: not an empty class) */
VPRIV uint32_t cxx_gen_empty_object(cxx *c, qty t, uint32_t loc)
{
	bool pod, empty;
	uint32_t tmp;

	if (!cxx_gen_on(c) || t == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, t) || AST_TYPE_KIND(c->ast, t) == TY_ARRAY ||
	    !cxx_class_layout_info(c, QUNQ(t), &pod, &empty) || !empty || !pod)
		return 0;
	tmp = cxx_gen_temporary(c, QUNQ(t), loc);
	return tmp ? cxx_gen_var_node(c, tmp, loc) : 0;
}

/* nullptr: a null void pointer (it converts to any pointer type) */
VPRIV void cxx_gen_nullptr(cxx *c, uint32_t loc)
{
	cc *a = c->ast;
	qty vp = cc_type_pointer(a, cc_type_basic(TY_VOID));

	if (!cxx_gen_on(c))
		return;
	c->enode = cc_cast_insert(a, cc_node_integer_new(a, 0, cc_type_basic(TY_LONG), loc), vp, cc_conversion_cast_kind(a, cc_type_basic(TY_LONG), vp, 0));
}

/* reinterpret_cast<T>(x) between pointers, or a pointer and an integer of its size */
VPRIV void cxx_gen_reinterpret(cxx *c, uint32_t x, qty to, uint32_t loc)
{
	cc *a = c->ast;
	qty from;
	bool pf, pt;

	(void)loc;
	if (!cxx_gen_on(c) || !x || to == CXX_TY_UNKNOWN || cxx_gen_is_reference(c, to) || !cxx_gen_type_ok(c, to))
		return;
	x = cc_conversion_decay(a, x);
	from = AST_NODE_AT(a, x)->ty;
	pf = AST_TYPE_KIND(a, from) == TY_PTR, pt = AST_TYPE_KIND(a, to) == TY_PTR;
	if (!cxx_gen_type_ok(c, from) || !(pf || pt) || (!pf && !cc_type_is_integer(a, from)) || (!pt && !cc_type_is_integer(a, to)))
		return;
	c->enode = cc_cast_insert(a, x, QUNQ(to), cc_conversion_cast_kind(a, from, QUNQ(to), x));
}

/* a[i] on a pointer or an array: *(a + i) */
VPRIV void cxx_gen_index(cxx *c, uint32_t base, uint32_t index, uint32_t loc)
{
	cc *a = c->ast;

	if (!cxx_gen_on(c) || !base || !index)
		return;
	c->enode = cc_dereference_build(a, cc_binary_build(a, LEX_TOKEN_PLUS, base, index, loc), loc);
}

/* cond ? x : y of scalars */
VPRIV void cxx_gen_conditional(cxx *c, uint32_t cond, uint32_t x, uint32_t y, uint32_t loc)
{
	cc *a = c->ast;

	if (!cxx_gen_on(c) || !cond || !x || !y || cc_type_is_record(a, AST_NODE_AT(a, cond)->ty))
		return;
	if (cc_type_is_record(a, AST_NODE_AT(a, x)->ty) && cc_type_is_record(a, AST_NODE_AT(a, y)->ty) &&
	    AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) != TY_ARRAY && QUNQ(AST_NODE_AT(a, x)->ty) == QUNQ(AST_NODE_AT(a, y)->ty) &&
	    (AST_NODE_AT(a, x)->flags & NF_LVALUE) && (AST_NODE_AT(a, y)->flags & NF_LVALUE)) {
		/* c ? a : b of two lvalues of one class: an lvalue of it (const if either is), *(c ? &a : &b) */
		qty t = AST_NODE_AT(a, x)->ty, u = AST_NODE_AT(a, y)->ty;
		qty rt = QT(QI(t), QQ(t) | QQ(u)), pt = cc_type_pointer(a, rt);
		uint32_t px = cc_cast_insert(a, cc_address_of_build(a, x, loc), pt, CK_NOP), py = cc_cast_insert(a, cc_address_of_build(a, y, loc), pt, CK_NOP);

		cond = cc_condition_scalar(a, cc_conversion_decay(a, cond), "'?:'");
		c->enode = cc_dereference_build(a, cc_conditional_build(a, cond, px, py, loc), loc);
		return;
	}
	if (cc_type_is_record(a, AST_NODE_AT(a, x)->ty) != cc_type_is_record(a, AST_NODE_AT(a, y)->ty) ||
	    (cc_type_is_record(a, AST_NODE_AT(a, x)->ty) && (QUNQ(AST_NODE_AT(a, x)->ty) != QUNQ(AST_NODE_AT(a, y)->ty) ||
							      cxx_gen_trivial_abi(c, AST_NODE_AT(a, x)->ty, 0) != 1)))
		return;	/* (a class's: both of one class that copies as bytes, as C's ?: of structs) */
	cond = cc_condition_scalar(a, cc_conversion_decay(a, cond), "'?:'");
	c->enode = cc_conditional_build(a, cond, x, y, loc);
}

/* a <=> b of scalars: a std::strong_ordering or partial_ordering object (libc++'s: one signed char,
   -1 0 1, -127 unordered) in a temporary */
VPRIV void cxx_gen_spaceship(cxx *c, uint32_t l, uint32_t r, qty t, uint32_t loc)
{
	cc *a = c->ast;
	const tag *rec;
	uint32_t tmp, v, lt, gt, node;
	bool partial;

	c->enode = 0;
	if (!cxx_gen_on(c) || !l || !r || t == CXX_TY_UNKNOWN || !cc_type_is_record(a, t))
		return;
	rec = &a->tags[AST_TYPE(a, QUNQ(t))->aux];
	if (rec->nfield < 1 || !rec->f[0].name || AST_TYPE(a, rec->f[0].ty)->size != 1 || AST_TYPE(a, QUNQ(t))->size != 1)
		return;	/* (not libc++'s layout) */
	partial = !cc_type_is_integer(a, AST_NODE_AT(a, cc_node_rvalue(a, l))->ty) && AST_TYPE_KIND(a, AST_NODE_AT(a, cc_node_rvalue(a, l))->ty) != TY_PTR;
	if (!(tmp = cxx_gen_temporary(c, QUNQ(t), loc)))
		return;
	/* (each operand read twice: the operands are values here, as a built-in comparison's are) */
	lt = cc_binary_build(a, LEX_TOKEN_LESS, cc_node_rvalue(a, l), cc_node_rvalue(a, r), loc);
	gt = cc_binary_build(a, LEX_TOKEN_GREATER, cc_node_rvalue(a, l), cc_node_rvalue(a, r), loc);
	v = cc_node_integer_new(a, 0, cc_type_basic(TY_INT), loc);
	if (partial)
		v = cc_conditional_build(a, cc_binary_build(a, LEX_TOKEN_EQUAL_EQUAL, cc_node_rvalue(a, l), cc_node_rvalue(a, r), loc), v,
					 cc_node_integer_new(a, -127, cc_type_basic(TY_INT), loc), loc);
	v = cc_conditional_build(a, gt, cc_node_integer_new(a, 1, cc_type_basic(TY_INT), loc), v, loc);
	v = cc_conditional_build(a, lt, cc_node_integer_new(a, -1, cc_type_basic(TY_INT), loc), v, loc);
	v = cc_assignment_build(a, 0, member(a, cxx_gen_var_node(c, tmp, loc), rec->f[0].name, loc),
				cc_conversion_apply(a, v, QUNQ(rec->f[0].ty)), loc);
	(void)node;
	c->enode = cc_node_new_operands(a, N_COMMA, QUNQ(t), loc, v, cxx_gen_var_node(c, tmp, loc));	/* (the ordering object, a value) */
}

/* x, y */
VPRIV void cxx_gen_comma(cxx *c, uint32_t x, uint32_t y, uint32_t loc)
{
	cc *a = c->ast;

	if (!cxx_gen_on(c) || !x || !y)
		return;
	y = cc_conversion_decay(a, y);
	c->enode = cc_node_new_operands(a, N_COMMA, QUNQ(AST_NODE_AT(a, y)->ty), loc, x, y);
}

/* l op r with the built-in operator (the operands' nodes; 0: one was not built) */
/*
 * a @ b rewritten as (a <=> b) @ 0 (C++20): `ord` the ordering object operator<=> gave (std::strong_ordering,
 * weak_ordering, partial_ordering: libc++'s __value_ is -1, 0, 1, or -127 for unordered), tested so that an
 * unordered value is false for every @.  c->enode: the bool (0: not built).
 */
VPRIV void cxx_gen_ordering_test(cxx *c, uint32_t ord, int op, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t v, t1, t2;
	qty it = cc_type_basic(TY_INT);

	c->enode = 0;
	if (!cxx_gen_on(c) || !ord || !(v = cxx_gen_member_of(c, ord, vintern_cstr("__value_"), loc, 0)))
		return;
#define CMPV(k) cc_binary_build(a, LEX_TOKEN_EQUAL_EQUAL, cc_conversion_apply(a, cc_node_rvalue(a, v), it), cc_node_integer_new(a, (int64_t)(k), it, loc), loc)
	if (op == LEX_TOKEN_LESS)
		c->enode = CMPV(-1);
	else if (op == LEX_TOKEN_GREATER)
		c->enode = CMPV(1);
	else {
		t1 = CMPV(0);
		t2 = op == LEX_TOKEN_LESS_EQUAL ? CMPV(-1) : CMPV(1);
		c->enode = cc_binary_build(a, LEX_TOKEN_PIPE_PIPE, t1, t2, loc);
	}
#undef CMPV
}

VPRIV void cxx_gen_binary(cxx *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	if (!cxx_gen_on(c) || !l || !r)
		return;
	if (op == LEX_TOKEN_AMPERSAND_AMPERSAND || op == LEX_TOKEN_PIPE_PIPE) {
		/* a class operand of && and ||: its operator bool */
		if (cc_type_is_record(c->ast, AST_NODE_AT(c->ast, l)->ty))
			l = cxx_gen_convert_class(c, l, cc_type_basic(TY_BOOL), loc);
		if (r && cc_type_is_record(c->ast, AST_NODE_AT(c->ast, r)->ty))
			r = cxx_gen_convert_class(c, r, cc_type_basic(TY_BOOL), loc);
		if (!l || !r)
			return;
	}
	if (cc_type_is_record(c->ast, AST_NODE_AT(c->ast, l)->ty) || cc_type_is_record(c->ast, AST_NODE_AT(c->ast, r)->ty))
		return;	/* (a class operand of a built-in operator: a conversion function, not built yet) */
	c->enode = cc_binary_build(c->ast, op, l, r, loc);
}

/* l = r, l op= r (op: the assignment operator's token) */
VPRIV void cxx_gen_assignment(cxx *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	int base;

	if (!cxx_gen_on(c) || !l || !r)
		return;
	switch (op) {
	case LEX_TOKEN_EQUAL: base = 0; break;
	case LEX_TOKEN_PLUS_EQUAL: base = LEX_TOKEN_PLUS; break;
	case LEX_TOKEN_MINUS_EQUAL: base = LEX_TOKEN_MINUS; break;
	case LEX_TOKEN_STAR_EQUAL: base = LEX_TOKEN_STAR; break;
	case LEX_TOKEN_SLASH_EQUAL: base = LEX_TOKEN_SLASH; break;
	case LEX_TOKEN_PERCENT_EQUAL: base = LEX_TOKEN_PERCENT; break;
	case LEX_TOKEN_AMPERSAND_EQUAL: base = LEX_TOKEN_AMPERSAND; break;
	case LEX_TOKEN_PIPE_EQUAL: base = LEX_TOKEN_PIPE; break;
	case LEX_TOKEN_CARET_EQUAL: base = LEX_TOKEN_CARET; break;
	case LEX_TOKEN_LESS_LESS_EQUAL: base = LEX_TOKEN_LESS_LESS; break;
	case LEX_TOKEN_GREATER_GREATER_EQUAL: base = LEX_TOKEN_GREATER_GREATER; break;
	default: return;
	}
	if (!(AST_NODE_AT(c->ast, l)->flags & NF_LVALUE))
		return;
	if (!base && c->nullptr_ty && QUNQ(AST_NODE_AT(c->ast, r)->ty) == c->nullptr_ty && AST_TYPE_KIND(c->ast, AST_NODE_AT(c->ast, l)->ty) == TY_PTR) {
		/* p = v of a std::nullptr_t object (std::__exchange(p, nullptr)): the null pointer it holds */
		r = cxx_gen_bind(c, r, QUNQ(AST_NODE_AT(c->ast, l)->ty), loc);
		if (!r)
			return;
	}
	if (!base && cc_type_is_record(c->ast, AST_NODE_AT(c->ast, l)->ty) && AST_TYPE_KIND(c->ast, AST_NODE_AT(c->ast, l)->ty) != TY_ARRAY &&
	    QUNQ(AST_NODE_AT(c->ast, l)->ty) != QUNQ(AST_NODE_AT(c->ast, r)->ty) && cxx_gen_trivial_abi(c, AST_NODE_AT(c->ast, l)->ty, 0) == 1) {
		/* x = v of a trivially copyable class from another type: the converting constructor's temporary,
		   copied (what the implicit operator= does) */
		r = cxx_gen_bind(c, r, QUNQ(AST_NODE_AT(c->ast, l)->ty), loc);
		if (!r || QUNQ(AST_NODE_AT(c->ast, r)->ty) != QUNQ(AST_NODE_AT(c->ast, l)->ty))
			return;
	}
	if (cc_type_is_record(c->ast, AST_NODE_AT(c->ast, l)->ty) &&
	    (base || QUNQ(AST_NODE_AT(c->ast, l)->ty) != QUNQ(AST_NODE_AT(c->ast, r)->ty) || cxx_gen_trivial_abi(c, AST_NODE_AT(c->ast, l)->ty, 0) != 1))
		return;	/* (a class whose assignment is not a copy of its bytes: the implicit operator=, not built yet) */
	if (cc_type_is_record(c->ast, AST_NODE_AT(c->ast, r)->ty) != cc_type_is_record(c->ast, AST_NODE_AT(c->ast, l)->ty))
		return;
	if (!base && AST_TYPE_KIND(c->ast, AST_NODE_AT(c->ast, l)->ty) == TY_PTR && !cxx_gen_is_reference(c, AST_NODE_AT(c->ast, l)->ty)) {
		/* a pointer: the value converted as C++ does (Derived * to Base *, moved to the base subobject) */
		r = cxx_gen_bind(c, r, QUNQ(AST_NODE_AT(c->ast, l)->ty), loc);
		if (!r)
			return;
	}
	c->enode = cc_assignment_build(c->ast, base, l, r, loc);
}

/* the k-th default argument of f, parsed where the call is (in the scope of f's declaration): its node (0: not built) */
VPRIV uint32_t cxx_gen_default_argument(cxx *c, const cxx_entity *f, uint32_t k)
{
	jmp_buf jb, *outer_fail = c->fail;
	cxx_scope *scope = c->scope;
	uint32_t pos = c->pos, enode = c->enode, eobj = c->eobj_node;
	qty etype = c->etype;
	uint8_t elval = c->elval, econst = c->econst, einvalid = c->einvalid, call_resolved = c->call_resolved, none_viable = c->none_viable;
	uint8_t eobj_arrow = c->eobj_arrow, eunsure = c->eunsure;
	int64_t evalue = c->evalue;
	cxx_entity *call_picked = c->call_picked, *ecallee = c->ecallee;
	volatile uint32_t node = 0;

	if (!f->defs || k >= f->ndefaults || !f->def_scope)
		return 0;
	c->pos = f->defs[2 * k], c->scope = f->def_scope, c->fail = &jb;
	if (!setjmp(jb)) {
		cxx_assignment_expression_parse(c);
		if (c->pos == f->defs[2 * k + 1])
			node = c->enode;
	}
	c->fail = outer_fail, c->pos = pos, c->scope = scope;
	c->enode = enode, c->eobj_node = eobj, c->eobj_arrow = eobj_arrow, c->etype = etype, c->elval = elval, c->econst = econst;
	c->evalue = evalue, c->einvalid = einvalid, c->eunsure = eunsure, c->call_resolved = call_resolved, c->none_viable = none_viable;
	c->call_picked = call_picked, c->ecallee = ecallee;
	return node;
}

/* B::operator=(x) in a member function of a class derived from B, B's assignment the implicit one of a
   trivially copyable class: its bytes from x (B or derived from it) into this's B subobject, an lvalue of B */
VPRIV void cxx_gen_base_assign(cxx *c, qty base, uint32_t x, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t self, tb, src;

	c->enode = 0;
	if (!cxx_gen_on(c) || !x || !c->gen_this || !cxx_gen_defaulted_copy_trivial(c, QUNQ(base)))
		return;
	cxx_gen_this(c, loc);
	self = c->enode ? cc_dereference_build(a, cc_node_rvalue(a, c->enode), loc) : 0;
	c->enode = 0;
	if (!self || !(tb = QUNQ(AST_NODE_AT(a, self)->ty) == QUNQ(base) ? self : cxx_gen_base_object(c, self, QUNQ(base), loc)))
		return;
	if (!(AST_NODE_AT(a, x)->flags & NF_LVALUE) || !cc_type_is_record(a, AST_NODE_AT(a, x)->ty))
		return;
	src = QUNQ(AST_NODE_AT(a, x)->ty) == QUNQ(base) ? x : cxx_gen_base_object(c, x, QUNQ(base), loc);
	if (!src)
		return;
	if (AST_NODE_AT(a, src)->ty != AST_NODE_AT(a, tb)->ty) {
		qty pt = cc_type_pointer(a, AST_NODE_AT(a, tb)->ty);
		uint32_t addr = cc_address_of_build(a, src, loc);

		src = cc_dereference_build(a, cc_cast_insert(a, addr, pt, CK_NOP), loc);
	}
	{
		uint32_t asg = cc_assignment_build(a, 0, tb, cc_node_rvalue(a, src), loc);

		c->enode = cc_node_new_operands(a, N_COMMA, AST_NODE_AT(a, tb)->ty, loc, asg, tb);
		AST_NODE_AT(a, c->enode)->flags |= NF_LVALUE;
	}
}
