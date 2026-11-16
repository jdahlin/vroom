/* C++ code generation: literals, variables and the built-in operators */
#include "codegen.h"

VPRIV void cxx_gen_integer(cxx *c, int64_t v, ast_qualified_type t, uint32_t loc)
{
	if (cxx_gen_on(c) && cxx_gen_type_ok(c, t))
		c->enode = cc_node_integer_new(c->ast, (i128)v, t, loc);
}

/* a floating literal (its spelling: decimal or hexadecimal, digit separators, a suffix f, l) */
VPRIV void cxx_gen_floating(cxx *c, const char *text, size_t len, ast_qualified_type t, uint32_t loc)
{
	char buf[128];
	size_t n = 0;
	int k = t != CXX_TY_UNKNOWN ? AST_TYPE_KIND(c->ast, t) : -1;

	if (!cxx_gen_on(c) || (k != AST_TYPE_FLOAT && k != AST_TYPE_DOUBLE && k != AST_TYPE_LONG_DOUBLE) || len >= sizeof buf)
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
	c->enode = cc_node_float_new(c->ast, k == AST_TYPE_FLOAT ? (double)strtof(buf, NULL) : strtod(buf, NULL), t, loc);	/* (strto* stop at the suffix) */
}

/* ---- lambdas ------------------------------------------------------------------------------- */

/* a variable (a parameter, a local, one at namespace scope) by name */
VPRIV void cxx_gen_variable(cxx *c, const cxx_entity *v, uint32_t loc)
{
	uint32_t n;

	if (!cxx_gen_on(c) || !v->sym || !(cxx_gen_object_type_ok(c, v->ty) || cxx_gen_type_ok(c, v->ty)))
		return;
	if (v->has_value && v->ty != CXX_TY_UNKNOWN && (AST_QUALIFIED_TYPE_QUALIFIERS(v->ty) & AST_QUALIFIER_CONST) && !c->ast->syms[v->sym].depth &&
	    !(c->ast->syms[v->sym].flags & (AST_SYMBOL_FLAG_DEFINED | AST_SYMBOL_FLAG_TENTATIVE)) &&
	    ((AST_TYPE_KIND(c->ast, v->ty) >= AST_TYPE_BOOL && AST_TYPE_KIND(c->ast, v->ty) <= AST_TYPE_UNSIGNED_LONG_LONG) || AST_TYPE_KIND(c->ast, v->ty) == AST_TYPE_ENUM)) {
		/* a constant at namespace scope (constexpr, a const integer) with no object in this TU: its value */
		c->enode = cc_node_integer_new(c->ast, (i128)v->value, AST_QUALIFIED_TYPE_UNQUALIFIED(v->ty), loc);
		return;
	}
	if (c->gen_lambda && v->sym < c->gen_lambda->floor && c->ast->syms[v->sym].sc == AST_STORAGE_AUTO) {
		/* in a lambda's body: a variable of an enclosing function is a capture */
		int k = cxx_gen_capture(c, c->gen_lambda, v, loc);

		if (k < 0) {
			cxx_gen_fail(c);
			return;
		}
		c->enode = cc_dereference_build(c->ast, cc_node_rvalue(c->ast, cxx_gen_var_node(c, c->gen_lambda->cap[k].local, loc)), loc);
		return;
	}
	n = cc_node_new(c->ast, AST_NODE_VARIABLE, c->ast->syms[v->sym].ty, loc);
	AST_NODE_AT(c->ast, n)->a = v->sym;
	AST_NODE_AT(c->ast, n)->flags = AST_NODE_FLAG_LVALUE;
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
	ast_qualified_type etype = c->etype;
	uint8_t elval = c->elval, econst = c->econst, einvalid = c->einvalid, call_resolved = c->call_resolved, none_viable = c->none_viable;
	uint8_t eobj_arrow = c->eobj_arrow, eunsure = c->eunsure, edecl = c->edecl_name;
	int64_t evalue = c->evalue;
	cxx_entity *call_picked = c->call_picked, *ecallee = c->ecallee;
	volatile uint32_t node = 0;
	static __thread int depth;

	bool record = v->ty != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, v->ty) && AST_TYPE_KIND(c->ast, v->ty) != AST_TYPE_ARRAY;

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
				cxx_ctor_use(c, AST_QUALIFIED_TYPE_UNQUALIFIED(v->ty), c->cargs, c->cargs_lval, n, braced);
				cxx_gen_construct_temp(c, AST_QUALIFIED_TYPE_UNQUALIFIED(v->ty), an, n, loc);
				node = c->enode;
			}
		} else {
			cxx_assignment_expression_parse(c);
			if (c->enode && (cxx_is(c, LEX_TOKEN_SEMICOLON) || cxx_is(c, LEX_TOKEN_COMMA)))
				node = record ? (AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(c->ast, c->enode)->ty) == AST_QUALIFIED_TYPE_UNQUALIFIED(v->ty) ? c->enode : 0) :
					cc_conversion_apply(c->ast, cc_conversion_decay(c->ast, c->enode), AST_QUALIFIED_TYPE_UNQUALIFIED(v->ty));
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
		x = cxx_gen_convert_class(c, x, cc_type_basic(AST_TYPE_BOOL), loc);	/* !x: x's operator bool */
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
		r = cc_node_new(a, AST_NODE_UNARY, AST_NODE_AT(a, x)->ty, loc);
		AST_NODE_AT(a, r)->aux = (uint16_t)op;
		AST_NODE_AT(a, r)->a = x;
		break;
	case LEX_TOKEN_EXCLAMATION:
		x = cc_condition_scalar(a, cc_node_rvalue(a, x), "'!'");
		r = cc_node_new(a, AST_NODE_UNARY, cc_type_basic(AST_TYPE_INT), loc);
		AST_NODE_AT(a, r)->aux = LEX_TOKEN_EXCLAMATION;
		AST_NODE_AT(a, r)->a = x;
		break;
	case LEX_TOKEN_STAR:
		r = cc_dereference_build(a, x, loc);
		break;
	case LEX_TOKEN_AMPERSAND:
		if (!(AST_NODE_AT(a, x)->flags & AST_NODE_FLAG_LVALUE))
			return;
		r = cc_address_of_build(a, x, loc);
		break;
	case LEX_TOKEN_PLUS_PLUS: case LEX_TOKEN_MINUS_MINUS:
		if (!(AST_NODE_AT(a, x)->flags & AST_NODE_FLAG_LVALUE))
			return;
		if (AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) != AST_TYPE_BOOL && !cc_node_bitfield(a, x)) {
			/* C++: ++x is x, an lvalue (its address taken: --it bound to a reference, libc++'s
			   __iter_move(--__last_iter)): *(++*t, t) with t = &x, evaluated once (C's node is no lvalue) */
			uint32_t pre = 0, t = cc_lowering_once(a, cc_address_of_build(a, x, loc), &pre), inc;

			inc = cc_node_new(a, op == LEX_TOKEN_PLUS_PLUS ? AST_NODE_PRE_INCREMENT : AST_NODE_PRE_DECREMENT, AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, x)->ty), loc);
			AST_NODE_AT(a, inc)->a = cc_dereference_build(a, cc_node_rvalue(a, t), loc);
			r = cc_node_new_operands(a, AST_NODE_COMMA, AST_NODE_AT(a, t)->ty, loc, inc, cc_node_rvalue(a, t));
			r = cc_dereference_build(a, cc_lowering_sequence(a, pre, r), loc);
			break;
		}
		r = cc_node_new(a, op == LEX_TOKEN_PLUS_PLUS ? AST_NODE_PRE_INCREMENT : AST_NODE_PRE_DECREMENT, AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, x)->ty), loc);
		AST_NODE_AT(a, r)->a = x;
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

	if (!cxx_gen_on(c) || !x || !(AST_NODE_AT(a, x)->flags & AST_NODE_FLAG_LVALUE))
		return;
	r = cc_node_new(a, op == LEX_TOKEN_PLUS_PLUS ? AST_NODE_POST_INCREMENT : AST_NODE_POST_DECREMENT, AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, x)->ty), loc);
	AST_NODE_AT(a, r)->a = x;
	c->enode = r;
}

/* a string literal: its elements (decoded, esz bytes each, len bytes without the terminating NUL element, which
   text has after them), of element type et */
VPRIV void cxx_gen_string_kind(cxx *c, const char *text, size_t len, int esz, int kind, ast_qualified_type et, uint32_t loc)
{
	cc *a = c->ast;
	char *buf;
	uint32_t si, n;

	if (!cxx_gen_on(c))
		return;
	buf = cc_heap_allocate(a, len + (size_t)esz);
	memcpy(buf, text, len);
	memset(buf + len, 0, (size_t)esz);
	si = a->nstr++;
	a->strs[si] = (cstr){ buf, (uint32_t)(len / (size_t)esz) + 1, (uint8_t)esz, (uint8_t)kind };
	n = cc_node_new(a, AST_NODE_STRING, cc_type_array(a, cc_type_qualify(a, et, AST_QUALIFIER_CONST), (uint64_t)(len / (size_t)esz) + 1, 0), loc);
	AST_NODE_AT(a, n)->a = si;
	AST_NODE_AT(a, n)->flags = AST_NODE_FLAG_LVALUE;
	c->enode = n;
}

/* a string literal: its bytes (no escapes), NUL-terminated */
VPRIV void cxx_gen_string(cxx *c, const char *text, size_t len, uint32_t loc)
{
	cxx_gen_string_kind(c, text, len, 1, LEX_TOKEN_STRING, cc_type_basic(AST_TYPE_CHAR), loc);
}

/* static_cast<T>(x) for a class T: a temporary direct-initialized from x ([expr.static.cast]/4); a prvalue of T
   is that object already (static_cast<strong_ordering>(a <=> 0)).  (T(x) and (T)x build theirs elsewhere.) */
VPRIV void cxx_gen_static_cast_class(cxx *c, uint32_t x, ast_qualified_type to, uint32_t loc)
{
	cc *a = c->ast;

	if (!cxx_gen_on(c) || !x || to == CXX_TY_UNKNOWN || !cc_type_is_record(a, to) || AST_TYPE_KIND(a, to) == AST_TYPE_ARRAY ||
	    !cxx_gen_object_type_ok(c, AST_QUALIFIED_TYPE_UNQUALIFIED(to)))
		return;
	{
		ast_qualified_type xt = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, x)->ty), at[1];
		bool prv = cxx_gen_prv_find(c, x) || !(AST_NODE_AT(a, x)->flags & AST_NODE_FLAG_LVALUE);
		uint8_t lv[1] = { !prv };
		ast_qualified_type etype = c->etype;
		uint8_t elval = c->elval;

		if (xt == AST_QUALIFIED_TYPE_UNQUALIFIED(to) && prv) {
			c->enode = x;
			return;
		}
		at[0] = xt;
		if (AST_TYPE_KIND(a, xt) == AST_TYPE_ARRAY)
			at[0] = cc_type_pointer(a, AST_TYPE(a, xt)->base), lv[0] = 0;
		cxx_ctor_use(c, AST_QUALIFIED_TYPE_UNQUALIFIED(to), at, lv, 1, false);
		if ((c->ctor_found && c->ctor_picked) || xt == AST_QUALIFIED_TYPE_UNQUALIFIED(to)) {	/* (a copy: the implicit copy constructor too) */
			c->enode = 0;
			cxx_gen_construct_temp(c, AST_QUALIFIED_TYPE_UNQUALIFIED(to), &x, 1, loc);
		} else if (cc_type_is_record(a, xt) && AST_TYPE_KIND(a, xt) != AST_TYPE_ARRAY) {
			uint32_t v = cxx_gen_convert_class(c, x, AST_QUALIFIED_TYPE_UNQUALIFIED(to), loc);	/* (its conversion function to T) */

			c->enode = v && AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, v)->ty) == AST_QUALIFIED_TYPE_UNQUALIFIED(to) ? v : 0;
		} else
			c->enode = 0;
		c->etype = etype, c->elval = elval;
	}
}

/* (T)x, static_cast<T>(x), T(x) between scalar types */
VPRIV void cxx_gen_cast(cxx *c, uint32_t x, ast_qualified_type to, uint32_t loc)
{
	cc *a = c->ast;

	(void)loc;
	if (!cxx_gen_on(c) || !x || to == CXX_TY_UNKNOWN)
		return;
	if (AST_TYPE_KIND(a, to) == AST_TYPE_VOID) {	/* (void)x */
		c->enode = cc_cast_insert(a, cc_node_rvalue(a, x), cc_type_basic(AST_TYPE_VOID), AST_CAST_VOID);
		return;
	}
	if (cxx_gen_is_reference(c, to)) {
		/* static_cast<T&&>(x), static_cast<T&>(x), const_cast<T&>(x) of an lvalue of that type: the same object
		   (a class: the same record, however its type is spelled; const dropped through its address) */
		ast_qualified_type xt = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, x)->ty), bt = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_TYPE(a, to)->base);
		bool same = xt == bt || (cc_type_is_record(a, xt) && cc_type_is_record(a, bt) && AST_TYPE_KIND(a, xt) == AST_TYPE_KIND(a, bt) &&
					 AST_TYPE(a, xt)->aux == AST_TYPE(a, bt)->aux);
		uint64_t off;

		if ((AST_NODE_AT(a, x)->flags & AST_NODE_FLAG_LVALUE) && !same && cc_type_is_record(a, xt) && cc_type_is_record(a, bt) &&
		    AST_TYPE_KIND(a, xt) != AST_TYPE_ARRAY && AST_TYPE_KIND(a, bt) != AST_TYPE_ARRAY && cxx_class_base_offset(c, xt, bt, &off, 0)) {
			/* static_cast<B&>(d) of an object of a class derived from B: its B subobject ([expr.static.cast]/2;
			   std::get on a tuple's leaf), as the reference's cv */
			uint32_t b = cxx_gen_base_object(c, x, bt, loc);

			if (b && AST_NODE_AT(a, b)->ty != AST_TYPE(a, to)->base) {
				ast_qualified_type pt = cc_type_pointer(a, AST_TYPE(a, to)->base);

				b = cc_dereference_build(a, cc_cast_insert(a, cc_address_of_build(a, b, loc), pt, AST_CAST_NOP), loc);
			}
			c->enode = b;
			return;
		}
		if ((AST_NODE_AT(a, x)->flags & AST_NODE_FLAG_LVALUE) && !same && cc_type_is_record(a, xt) && cc_type_is_record(a, bt) &&
		    AST_TYPE_KIND(a, xt) != AST_TYPE_ARRAY && AST_TYPE_KIND(a, bt) != AST_TYPE_ARRAY && cxx_class_base_offset(c, bt, xt, &off, 0)) {
			/* static_cast<D&>(b) of a B that is a non-virtual base of D: the D it is in, off bytes before it
			   ([expr.static.cast]/2; use_facet's static_cast<const ctype<char>&>(*facet)) */
			ast_qualified_type cp = cc_type_pointer(a, cc_type_basic(AST_TYPE_CHAR));
			uint32_t p = cc_cast_insert(a, cc_address_of_build(a, x, loc), cp, AST_CAST_NOP);

			if (off)
				p = cc_binary_build(a, LEX_TOKEN_MINUS, p, cc_node_integer_new(a, (i128)off, cc_type_basic(AST_TYPE_LONG), loc), loc);
			c->enode = cc_dereference_build(a, cc_cast_insert(a, p, cc_type_pointer(a, AST_TYPE(a, to)->base), AST_CAST_NOP), loc);
			return;
		}
		if ((AST_NODE_AT(a, x)->flags & AST_NODE_FLAG_LVALUE) && same) {
			if (AST_NODE_AT(a, x)->ty != AST_TYPE(a, to)->base) {
				ast_qualified_type pt = cc_type_pointer(a, AST_TYPE(a, to)->base);
				uint32_t addr = cc_address_of_build(a, x, loc);

				x = cc_dereference_build(a, cc_cast_insert(a, addr, pt, AST_CAST_NOP), loc);
			}
			c->enode = x;
		} else if ((AST_NODE_AT(a, x)->flags & AST_NODE_FLAG_LVALUE) && !cc_node_bitfield(a, x) && AST_TYPE_KIND(a, bt) != AST_TYPE_FUNCTION) {
			/* (T&)x of an lvalue of another type: reinterpret_cast<T&>(x), *(T *)&x ([expr.cast]/4; libc++'s
			   is >> (char&)__c of an unsigned char) */
			ast_qualified_type pt = cc_type_pointer(a, AST_TYPE(a, to)->base);
			uint32_t addr = cc_conversion_decay(a, cc_address_of_build(a, x, loc));

			c->enode = cc_dereference_build(a, cc_cast_insert(a, addr, pt, AST_CAST_NOP), loc);
		}
		return;
	}
	if (!cxx_gen_type_ok(c, to) || cc_type_is_record(a, to))
		return;
	if (cc_type_is_record(a, AST_NODE_AT(a, x)->ty) && AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) != AST_TYPE_ARRAY) {
		/* static_cast<E>(obj): the class's conversion function to exactly that type */
		uint32_t v = cxx_gen_convert_class(c, x, AST_QUALIFIED_TYPE_UNQUALIFIED(to), loc);

		if (v)
			c->enode = v;
		return;
	}
	x = cc_conversion_decay(a, x);
	if (!cxx_gen_type_ok(c, AST_NODE_AT(a, x)->ty) || cc_type_is_record(a, AST_NODE_AT(a, x)->ty))
		return;
	{
		/* static_cast<D*>(b) or static_cast<B*>(d) between pointers to classes, one a base of the other: the
		   pointer moved by the base's offset, a null pointer kept null */
		ast_qualified_type xt = AST_NODE_AT(a, x)->ty;

		if (AST_TYPE_KIND(a, xt) == AST_TYPE_POINTER && !AST_TYPE(a, xt)->ext && AST_TYPE_KIND(a, to) == AST_TYPE_POINTER && !AST_TYPE(a, to)->ext) {
			ast_qualified_type fb = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_TYPE(a, xt)->base), tb = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_TYPE(a, to)->base);
			uint64_t off = 0;
			int dir = 0;

			if (fb != tb && cc_type_is_record(a, fb) && cc_type_is_record(a, tb) && AST_TYPE_KIND(a, fb) != AST_TYPE_ARRAY &&
			    AST_TYPE_KIND(a, tb) != AST_TYPE_ARRAY && AST_TYPE(a, fb)->aux != AST_TYPE(a, tb)->aux)
				dir = cxx_class_base_offset(c, tb, fb, &off, 0) ? -1 : cxx_class_base_offset(c, fb, tb, &off, 0) ? 1 : 0;
			if (dir && off) {
				ast_qualified_type cp = cc_type_pointer(a, cc_type_basic(AST_TYPE_CHAR));
				uint32_t tmp = cxx_gen_temporary(c, xt, loc), set, moved;

				if (!tmp)
					return;
				set = cc_assignment_build(a, 0, cxx_gen_var_node(c, tmp, loc), cc_node_rvalue(a, x), loc);
				moved = cc_binary_build(a, dir < 0 ? LEX_TOKEN_MINUS : LEX_TOKEN_PLUS,
							cc_cast_insert(a, cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc)), cp, AST_CAST_NOP),
							cc_node_integer_new(a, (i128)off, cc_type_basic(AST_TYPE_LONG), loc), loc);
				moved = cc_cast_insert(a, moved, AST_QUALIFIED_TYPE_UNQUALIFIED(to), AST_CAST_NOP);
				c->enode = cc_node_new_operands(a, AST_NODE_COMMA, AST_QUALIFIED_TYPE_UNQUALIFIED(to), loc, set,
								cc_conditional_build(a, cc_condition_scalar(a, cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc)), "'static_cast'"),
										     moved, cc_cast_insert(a, cc_node_integer_new(a, 0, cc_type_basic(AST_TYPE_LONG), loc),
													   AST_QUALIFIED_TYPE_UNQUALIFIED(to), AST_CAST_INTEGER_TO_POINTER), loc));
				return;
			}
		}
	}
	c->enode = cc_cast_insert(a, x, AST_QUALIFIED_TYPE_UNQUALIFIED(to), cc_conversion_cast_kind(a, AST_NODE_AT(a, x)->ty, AST_QUALIFIED_TYPE_UNQUALIFIED(to), x));
}

/* an object of an empty class with no node (a constexpr reference to a function object, json's to_json):
   a temporary of it does as well, nothing in it being read (0: not an empty class) */
VPRIV uint32_t cxx_gen_empty_object(cxx *c, ast_qualified_type t, uint32_t loc)
{
	bool pod, empty;
	uint32_t tmp;

	if (!cxx_gen_on(c) || t == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, t) || AST_TYPE_KIND(c->ast, t) == AST_TYPE_ARRAY ||
	    !cxx_class_layout_info(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t), &pod, &empty) || !empty || !pod)
		return 0;
	tmp = cxx_gen_temporary(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t), loc);
	return tmp ? cxx_gen_var_node(c, tmp, loc) : 0;
}

/* nullptr: a null void pointer (it converts to any pointer type) */
VPRIV void cxx_gen_nullptr(cxx *c, uint32_t loc)
{
	cc *a = c->ast;
	ast_qualified_type vp = cc_type_pointer(a, cc_type_basic(AST_TYPE_VOID));

	if (!cxx_gen_on(c))
		return;
	c->enode = cc_cast_insert(a, cc_node_integer_new(a, 0, cc_type_basic(AST_TYPE_LONG), loc), vp, cc_conversion_cast_kind(a, cc_type_basic(AST_TYPE_LONG), vp, 0));
}

/* reinterpret_cast<T>(x) between pointers, or a pointer and an integer of its size */
VPRIV void cxx_gen_reinterpret(cxx *c, uint32_t x, ast_qualified_type to, uint32_t loc)
{
	cc *a = c->ast;
	ast_qualified_type from;
	bool pf, pt;

	(void)loc;
	if (!cxx_gen_on(c) || !x || to == CXX_TY_UNKNOWN || cxx_gen_is_reference(c, to) || !cxx_gen_type_ok(c, to))
		return;
	x = cc_conversion_decay(a, x);
	from = AST_NODE_AT(a, x)->ty;
	pf = AST_TYPE_KIND(a, from) == AST_TYPE_POINTER, pt = AST_TYPE_KIND(a, to) == AST_TYPE_POINTER;
	if (!cxx_gen_type_ok(c, from) || !(pf || pt) || (!pf && !cc_type_is_integer(a, from)) || (!pt && !cc_type_is_integer(a, to)))
		return;
	c->enode = cc_cast_insert(a, x, AST_QUALIFIED_TYPE_UNQUALIFIED(to), cc_conversion_cast_kind(a, from, AST_QUALIFIED_TYPE_UNQUALIFIED(to), x));
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

	if (!cxx_gen_on(c) || !cond || !x || !y)
		return;
	if (cc_type_is_record(a, AST_NODE_AT(a, cond)->ty))
		cond = cxx_gen_convert_class(c, cond, cc_type_basic(AST_TYPE_BOOL), loc);	/* (a class: its operator bool, explicit or not) */
	if (!cond || cc_type_is_record(a, AST_NODE_AT(a, cond)->ty))
		return;
	if (cc_type_is_record(a, AST_NODE_AT(a, x)->ty) && cc_type_is_record(a, AST_NODE_AT(a, y)->ty) &&
	    AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) != AST_TYPE_ARRAY && AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, x)->ty) == AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, y)->ty) &&
	    (AST_NODE_AT(a, x)->flags & AST_NODE_FLAG_LVALUE) && (AST_NODE_AT(a, y)->flags & AST_NODE_FLAG_LVALUE) &&
	    !cxx_gen_prv_find(c, x) && !cxx_gen_prv_find(c, y)) {
		/* c ? a : b of two lvalues of one class: an lvalue of it (const if either is), *(c ? &a : &b) */
		ast_qualified_type t = AST_NODE_AT(a, x)->ty, u = AST_NODE_AT(a, y)->ty;
		ast_qualified_type rt = AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(t), AST_QUALIFIED_TYPE_QUALIFIERS(t) | AST_QUALIFIED_TYPE_QUALIFIERS(u)), pt = cc_type_pointer(a, rt);
		uint32_t px = cc_cast_insert(a, cc_address_of_build(a, x, loc), pt, AST_CAST_NOP), py = cc_cast_insert(a, cc_address_of_build(a, y, loc), pt, AST_CAST_NOP);

		cond = cc_condition_scalar(a, cc_conversion_decay(a, cond), "'?:'");
		c->enode = cc_dereference_build(a, cc_conditional_build(a, cond, px, py, loc), loc);
		return;
	}
	if (cc_type_is_record(a, AST_NODE_AT(a, x)->ty) && cc_type_is_record(a, AST_NODE_AT(a, y)->ty) &&
	    AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) != AST_TYPE_ARRAY && AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, x)->ty) == AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, y)->ty) &&
	    cxx_gen_trivial_abi(c, AST_NODE_AT(a, x)->ty, 0) != 1) {
		/* c ? a : b of one class, not both lvalues: a prvalue, a temporary each branch constructs from its
		   operand (json's res ? result : basic_json(value_t::discarded)) */
		ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, x)->ty), pt = cc_type_pointer(a, t);
		cxx_prv *px = cxx_gen_prv_find(c, x), *py = cxx_gen_prv_find(c, y);
		/* (an operand that is a temporary just built, T(7): the result, built there; the other branch
		   constructs into it, so neither is copied twice) */
		int own = py && py->tmp && !py->call && !py->flagged ? 1 : px && px->tmp && !px->call && !px->flagged ? 0 : -1;
		uint32_t tmp = own >= 0 ? (own ? py->tmp : px->tmp) : cxx_gen_class_temp(c, t, loc), side[2] = { x, y }, br[2];

		if (!tmp)
			return;
		for (int k = 0; k < 2; k++) {
			if (k == own) {
				br[k] = cc_address_of_build(a, side[k], loc);	/* (its node constructs it, then names it) */
				continue;
			}
			ast_qualified_type at[1] = { AST_NODE_AT(a, side[k])->ty };
			uint8_t lv[1] = { (AST_NODE_AT(a, side[k])->flags & AST_NODE_FLAG_LVALUE) != 0 && !cxx_gen_prv_find(c, side[k]) };
			cc_id_list l;

			cc_id_list_init(&l);
			cxx_ctor_use(c, t, at, lv, 1, false);
			if (!cxx_gen_construct_at(c, cxx_gen_var_node(c, tmp, loc), t, &side[k], 1, &l, loc))
				return;
			br[k] = cxx_gen_chain(c, &l, cc_address_of_build(a, cxx_gen_var_node(c, tmp, loc), loc), loc);
			if (!br[k] || AST_NODE_AT(a, br[k])->ty != pt)
				br[k] = cc_cast_insert(a, br[k], pt, AST_CAST_NOP);
		}
		cond = cc_condition_scalar(a, cc_conversion_decay(a, cond), "'?:'");
		c->enode = cc_dereference_build(a, cc_conditional_build(a, cond, br[0], br[1], loc), loc);
		cxx_gen_prv_add(c, c->enode, 0, 0, tmp);
		return;
	}
	if (cc_type_is_record(a, AST_NODE_AT(a, x)->ty) != cc_type_is_record(a, AST_NODE_AT(a, y)->ty) ||
	    (cc_type_is_record(a, AST_NODE_AT(a, x)->ty) && (AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, x)->ty) != AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, y)->ty) ||
							      cxx_gen_trivial_abi(c, AST_NODE_AT(a, x)->ty, 0) != 1)))
		return;	/* (a class's: both of one class that copies as bytes, as C's ?: of structs) */
	cond = cc_condition_scalar(a, cc_conversion_decay(a, cond), "'?:'");
	c->enode = cc_conditional_build(a, cond, x, y, loc);
}

/* a <=> b of scalars: a std::strong_ordering or partial_ordering object (libc++'s: one signed char,
   -1 0 1, -127 unordered) in a temporary */
VPRIV void cxx_gen_spaceship(cxx *c, uint32_t l, uint32_t r, ast_qualified_type t, uint32_t loc)
{
	cc *a = c->ast;
	const tag *rec;
	uint32_t tmp, v, lt, gt, node;
	bool partial;

	c->enode = 0;
	if (!cxx_gen_on(c) || !l || !r || t == CXX_TY_UNKNOWN || !cc_type_is_record(a, t))
		return;
	rec = &a->tags[AST_TYPE(a, AST_QUALIFIED_TYPE_UNQUALIFIED(t))->aux];
	if (rec->nfield < 1 || !rec->f[0].name || AST_TYPE(a, rec->f[0].ty)->size != 1 || AST_TYPE(a, AST_QUALIFIED_TYPE_UNQUALIFIED(t))->size != 1)
		return;	/* (not libc++'s layout) */
	partial = !cc_type_is_integer(a, AST_NODE_AT(a, cc_node_rvalue(a, l))->ty) && AST_TYPE_KIND(a, AST_NODE_AT(a, cc_node_rvalue(a, l))->ty) != AST_TYPE_POINTER;
	if (!(tmp = cxx_gen_temporary(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t), loc)))
		return;
	/* (each operand read twice: the operands are values here, as a built-in comparison's are) */
	lt = cc_binary_build(a, LEX_TOKEN_LESS, cc_node_rvalue(a, l), cc_node_rvalue(a, r), loc);
	gt = cc_binary_build(a, LEX_TOKEN_GREATER, cc_node_rvalue(a, l), cc_node_rvalue(a, r), loc);
	v = cc_node_integer_new(a, 0, cc_type_basic(AST_TYPE_INT), loc);
	if (partial)
		v = cc_conditional_build(a, cc_binary_build(a, LEX_TOKEN_EQUAL_EQUAL, cc_node_rvalue(a, l), cc_node_rvalue(a, r), loc), v,
					 cc_node_integer_new(a, -127, cc_type_basic(AST_TYPE_INT), loc), loc);
	v = cc_conditional_build(a, gt, cc_node_integer_new(a, 1, cc_type_basic(AST_TYPE_INT), loc), v, loc);
	v = cc_conditional_build(a, lt, cc_node_integer_new(a, -1, cc_type_basic(AST_TYPE_INT), loc), v, loc);
	v = cc_assignment_build(a, 0, member(a, cxx_gen_var_node(c, tmp, loc), rec->f[0].name, loc),
				cc_conversion_apply(a, v, AST_QUALIFIED_TYPE_UNQUALIFIED(rec->f[0].ty)), loc);
	(void)node;
	c->enode = cc_node_new_operands(a, AST_NODE_COMMA, AST_QUALIFIED_TYPE_UNQUALIFIED(t), loc, v, cxx_gen_var_node(c, tmp, loc));	/* (the ordering object, a value) */
}

/* x, y */
VPRIV void cxx_gen_comma(cxx *c, uint32_t x, uint32_t y, uint32_t loc)
{
	cc *a = c->ast;

	if (!cxx_gen_on(c) || !x || !y)
		return;
	y = cc_conversion_decay(a, y);
	c->enode = cc_node_new_operands(a, AST_NODE_COMMA, AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, y)->ty), loc, x, y);
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
	ast_qualified_type it = cc_type_basic(AST_TYPE_INT);

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

/* 0 <=> ord, an ordering reversed (a reversed candidate's result): its less and greater swapped, equivalent and
   unordered kept (libc++'s __value_: -1 and 1 swap) */
VPRIV void cxx_gen_ordering_reverse(cxx *c, uint32_t ord, ast_qualified_type t, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t tmp, set, v, nv, flip, asg;
	ast_qualified_type it = cc_type_basic(AST_TYPE_INT);

	c->enode = 0;
	if (!cxx_gen_on(c) || !ord || t == CXX_TY_UNKNOWN || !(tmp = cxx_gen_temporary(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t), loc)))
		return;
	set = cc_assignment_build(a, 0, cxx_gen_var_node(c, tmp, loc), cc_node_rvalue(a, ord), loc);
	if (!(v = cxx_gen_member_of(c, cxx_gen_var_node(c, tmp, loc), vintern_cstr("__value_"), loc, 0)))
		return;
	nv = cc_conversion_apply(a, cc_node_rvalue(a, v), it);
	flip = cc_binary_build(a, LEX_TOKEN_PIPE_PIPE,
			       cc_binary_build(a, LEX_TOKEN_EQUAL_EQUAL, nv, cc_node_integer_new(a, -1, it, loc), loc),
			       cc_binary_build(a, LEX_TOKEN_EQUAL_EQUAL, cc_conversion_apply(a, cc_node_rvalue(a, v), it),
					       cc_node_integer_new(a, 1, it, loc), loc), loc);
	nv = cc_node_new_operands(a, AST_NODE_CONDITIONAL, it, loc, flip,
				  cc_binary_build(a, LEX_TOKEN_MINUS, cc_node_integer_new(a, 0, it, loc), cc_conversion_apply(a, cc_node_rvalue(a, v), it), loc));
	AST_NODE_AT(a, nv)->c = cc_conversion_apply(a, cc_node_rvalue(a, v), it);
	asg = cc_assignment_build(a, 0, v, cc_conversion_apply(a, nv, AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, v)->ty)), loc);
	{
		ast_qualified_type pt = cc_type_pointer(a, AST_QUALIFIED_TYPE_UNQUALIFIED(t));
		uint32_t addr = cc_address_of_build(a, cxx_gen_var_node(c, tmp, loc), loc);

		c->enode = cc_dereference_build(a, cc_node_new_operands(a, AST_NODE_COMMA, pt, loc, set,
									cc_node_new_operands(a, AST_NODE_COMMA, pt, loc, asg, addr)), loc);
	}
}

VPRIV void cxx_gen_binary(cxx *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	if (!cxx_gen_on(c) || !l || !r)
		return;
	if (op == LEX_TOKEN_AMPERSAND_AMPERSAND || op == LEX_TOKEN_PIPE_PIPE) {
		/* a class operand of && and ||: its operator bool */
		if (cc_type_is_record(c->ast, AST_NODE_AT(c->ast, l)->ty))
			l = cxx_gen_convert_class(c, l, cc_type_basic(AST_TYPE_BOOL), loc);
		if (r && cc_type_is_record(c->ast, AST_NODE_AT(c->ast, r)->ty))
			r = cxx_gen_convert_class(c, r, cc_type_basic(AST_TYPE_BOOL), loc);
		if (!l || !r)
			return;
	}
	if (c->nullptr_ty) {
		/* p == n, n != p of a std::nullptr_t object (doctest's CHECK(p == nullptr), the value forwarded): the
		   null pointer it holds, of the other operand's pointer type (void * when both are nullptr_t) */
		bool ln = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(c->ast, l)->ty) == c->nullptr_ty;
		bool rn = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(c->ast, r)->ty) == c->nullptr_ty;
		ast_qualified_type vp = cc_type_pointer(c->ast, cc_type_basic(AST_TYPE_VOID));

		if (ln && !(l = cxx_gen_bind(c, l, !rn && AST_TYPE_KIND(c->ast, AST_NODE_AT(c->ast, r)->ty) == AST_TYPE_POINTER ? AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(c->ast, r)->ty) : vp, loc)))
			return;
		if (rn && !(r = cxx_gen_bind(c, r, AST_TYPE_KIND(c->ast, AST_NODE_AT(c->ast, l)->ty) == AST_TYPE_POINTER ? AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(c->ast, l)->ty) : vp, loc)))
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
	if (!(AST_NODE_AT(c->ast, l)->flags & AST_NODE_FLAG_LVALUE))
		return;
	if (!base && cc_type_is_record(c->ast, AST_NODE_AT(c->ast, l)->ty) && AST_TYPE_KIND(c->ast, AST_NODE_AT(c->ast, l)->ty) != AST_TYPE_ARRAY &&
	    AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(c->ast, l)->ty) == AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(c->ast, r)->ty)) {
		/* x = y of an empty class: nothing copied (its byte may be another member's, [[no_unique_address]]
		   __deleter_ at __ptr_'s offset in std::unique_ptr); y evaluated, x the result */
		bool pod, empty;

		if (cxx_class_layout_info(c, AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(c->ast, l)->ty), &pod, &empty) && empty) {
			c->enode = cc_node_new_operands(c->ast, AST_NODE_COMMA, AST_NODE_AT(c->ast, l)->ty, loc, cc_cast_insert(c->ast, cc_address_of_build(c->ast, r, loc), cc_type_basic(AST_TYPE_VOID), AST_CAST_VOID), l);
			AST_NODE_AT(c->ast, c->enode)->flags |= AST_NODE_FLAG_LVALUE;
			return;
		}
	}
	if (!cc_type_is_record(c->ast, AST_NODE_AT(c->ast, l)->ty) && cc_type_is_record(c->ast, AST_NODE_AT(c->ast, r)->ty) &&
	    AST_TYPE_KIND(c->ast, AST_NODE_AT(c->ast, r)->ty) != AST_TYPE_ARRAY) {
		/* x = v, x += v of a scalar from a class object: its conversion function to x's type (json's
		   position_t to size_t) */
		r = cxx_gen_bind(c, r, AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(c->ast, l)->ty), loc);
		if (!r)
			return;
	}
	if (!base && c->nullptr_ty && AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(c->ast, r)->ty) == c->nullptr_ty && AST_TYPE_KIND(c->ast, AST_NODE_AT(c->ast, l)->ty) == AST_TYPE_POINTER) {
		/* p = v of a std::nullptr_t object (std::__exchange(p, nullptr)): the null pointer it holds */
		r = cxx_gen_bind(c, r, AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(c->ast, l)->ty), loc);
		if (!r)
			return;
	}
	if (!base && cc_type_is_record(c->ast, AST_NODE_AT(c->ast, l)->ty) && AST_TYPE_KIND(c->ast, AST_NODE_AT(c->ast, l)->ty) != AST_TYPE_ARRAY &&
	    AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(c->ast, l)->ty) != AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(c->ast, r)->ty) && cxx_gen_trivial_abi(c, AST_NODE_AT(c->ast, l)->ty, 0) == 1) {
		/* x = v of a trivially copyable class from another type: the converting constructor's temporary,
		   copied (what the implicit operator= does) */
		r = cxx_gen_bind(c, r, AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(c->ast, l)->ty), loc);
		if (!r || AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(c->ast, r)->ty) != AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(c->ast, l)->ty))
			return;
	} else if (!base && cc_type_is_record(c->ast, AST_NODE_AT(c->ast, l)->ty) && AST_TYPE_KIND(c->ast, AST_NODE_AT(c->ast, l)->ty) != AST_TYPE_ARRAY &&
		   AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(c->ast, l)->ty) != AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(c->ast, r)->ty) &&
		   (AST_NODE_AT(c->ast, l)->flags & AST_NODE_FLAG_LVALUE)) {
		/* x = v of a class that is not trivially copyable from another type (alt_string = "text"): the
		   converting constructor's temporary, then assigned member by member as below (moved from) */
		r = cxx_gen_bind(c, r, AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(c->ast, l)->ty), loc);
		if (!r || AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(c->ast, r)->ty) != AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(c->ast, l)->ty))
			return;
		c->gen_assign_rv = 1;	/* (a temporary of its own: moved from) */
	}
	if (!base && cc_type_is_record(c->ast, AST_NODE_AT(c->ast, l)->ty) && AST_TYPE_KIND(c->ast, AST_NODE_AT(c->ast, l)->ty) != AST_TYPE_ARRAY &&
	    AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(c->ast, l)->ty) == AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(c->ast, r)->ty) &&
	    cxx_gen_trivial_abi(c, AST_NODE_AT(c->ast, l)->ty, 0) != 1 && (AST_NODE_AT(c->ast, l)->flags & AST_NODE_FLAG_LVALUE)) {
		/* x = y of a class that is not trivially copyable, with no operator= of its own: the implicit one,
		   each base and member assigned by its own operator= (moved from a prvalue: p.m_name = get<name>()) */
		cc *a = c->ast;
		ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, l)->ty);
		uint32_t src = r, chain;
		cxx_prv *p = cxx_gen_prv_find(c, r);	/* (a call's prvalue: its node may be marked an lvalue, its temporary) */
		bool prv = !(AST_NODE_AT(a, r)->flags & AST_NODE_FLAG_LVALUE) || (p && p->call);
		bool move = prv || c->gen_assign_rv;	/* (an xvalue, std::move(y): moved from where it is) */
		cc_id_list parts;

		c->gen_assign_rv = 0;
		cc_id_list_init(&parts);
		if (prv) {
			/* (a prvalue: built in a temporary first, once) */
			uint32_t tmp;

			if (!p || !p->call || !(tmp = cxx_gen_class_temp(c, t, loc)))
				return;
			cc_id_list_append(a, &parts, cc_statement_new(a, AST_NODE_EXPRESSION, loc, cxx_gen_prv_aim(c, p, cc_address_of_build(a, cxx_gen_var_node(c, tmp, loc), loc)), 0, 0));
			src = cxx_gen_var_node(c, tmp, loc);
		}
		if (!cxx_gen_defaulted_assign_parts(c, l, t, src, move, &parts, loc))
			return;
		chain = cxx_gen_chain(c, &parts, cc_address_of_build(a, l, loc), loc);
		c->enode = chain ? cc_dereference_build(a, chain, loc) : 0;
		return;
	}
	if (cc_type_is_record(c->ast, AST_NODE_AT(c->ast, l)->ty) &&
	    (base || AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(c->ast, l)->ty) != AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(c->ast, r)->ty) || cxx_gen_trivial_abi(c, AST_NODE_AT(c->ast, l)->ty, 0) != 1))
		return;	/* (a class whose assignment is not a copy of its bytes: the implicit operator=, not built yet) */
	if (cc_type_is_record(c->ast, AST_NODE_AT(c->ast, r)->ty) != cc_type_is_record(c->ast, AST_NODE_AT(c->ast, l)->ty))
		return;
	if (!base && AST_TYPE_KIND(c->ast, AST_NODE_AT(c->ast, l)->ty) == AST_TYPE_POINTER && !cxx_gen_is_reference(c, AST_NODE_AT(c->ast, l)->ty)) {
		/* a pointer: the value converted as C++ does (Derived * to Base *, moved to the base subobject) */
		r = cxx_gen_bind(c, r, AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(c->ast, l)->ty), loc);
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
	ast_qualified_type etype = c->etype;
	uint8_t elval = c->elval, econst = c->econst, einvalid = c->einvalid, call_resolved = c->call_resolved, none_viable = c->none_viable;
	uint8_t eobj_arrow = c->eobj_arrow, eunsure = c->eunsure;
	int64_t evalue = c->evalue;
	cxx_entity *call_picked = c->call_picked, *ecallee = c->ecallee;
	volatile uint32_t node = 0;

	if (!f->defs || k >= f->ndefaults || !f->def_scope)
		return 0;
	bool keep_why = !cxx_gen_why[0];	/* (= {...}: the caller builds it; what this parse notes is no cause) */

	c->pos = f->defs[2 * k], c->scope = f->def_scope, c->fail = &jb;
	if (!setjmp(jb)) {
		cxx_assignment_expression_parse(c);
		if (c->pos == f->defs[2 * k + 1])
			node = c->enode;
	}
	if (!node && keep_why && c->tok[f->defs[2 * k]].kind == LEX_TOKEN_LEFT_BRACE)
		cxx_gen_why[0] = 0;
	c->fail = outer_fail, c->pos = pos, c->scope = scope;
	c->enode = enode, c->eobj_node = eobj, c->eobj_arrow = eobj_arrow, c->etype = etype, c->elval = elval, c->econst = econst;
	c->evalue = evalue, c->einvalid = einvalid, c->eunsure = eunsure, c->call_resolved = call_resolved, c->none_viable = none_viable;
	c->call_picked = call_picked, c->ecallee = ecallee;
	return node;
}

/* B::operator=(x) in a member function of a class derived from B, B's assignment the implicit one of a
   trivially copyable class: its bytes from x (B or derived from it) into this's B subobject, an lvalue of B */
VPRIV void cxx_gen_base_assign(cxx *c, ast_qualified_type base, uint32_t x, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t self, tb, src;

	c->enode = 0;
	if (!cxx_gen_on(c) || !x || !c->gen_this || !cxx_gen_defaulted_copy_trivial(c, AST_QUALIFIED_TYPE_UNQUALIFIED(base)))
		return;
	cxx_gen_this(c, loc);
	self = c->enode ? cc_dereference_build(a, cc_node_rvalue(a, c->enode), loc) : 0;
	c->enode = 0;
	if (!self || !(tb = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, self)->ty) == AST_QUALIFIED_TYPE_UNQUALIFIED(base) ? self : cxx_gen_base_object(c, self, AST_QUALIFIED_TYPE_UNQUALIFIED(base), loc)))
		return;
	if (!(AST_NODE_AT(a, x)->flags & AST_NODE_FLAG_LVALUE) || !cc_type_is_record(a, AST_NODE_AT(a, x)->ty))
		return;
	src = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, x)->ty) == AST_QUALIFIED_TYPE_UNQUALIFIED(base) ? x : cxx_gen_base_object(c, x, AST_QUALIFIED_TYPE_UNQUALIFIED(base), loc);
	if (!src)
		return;
	if (AST_NODE_AT(a, src)->ty != AST_NODE_AT(a, tb)->ty) {
		ast_qualified_type pt = cc_type_pointer(a, AST_NODE_AT(a, tb)->ty);
		uint32_t addr = cc_address_of_build(a, src, loc);

		src = cc_dereference_build(a, cc_cast_insert(a, addr, pt, AST_CAST_NOP), loc);
	}
	{
		bool pod, empty;
		/* (an empty class: no bytes; its one byte may be the derived class's first member, json's m_data) */
		uint32_t asg = cxx_class_layout_info(c, AST_QUALIFIED_TYPE_UNQUALIFIED(base), &pod, &empty) && empty ?
			       cc_address_of_build(a, src, loc) : cc_assignment_build(a, 0, tb, cc_node_rvalue(a, src), loc);

		c->enode = cc_node_new_operands(a, AST_NODE_COMMA, AST_NODE_AT(a, tb)->ty, loc, asg, tb);
		AST_NODE_AT(a, c->enode)->flags |= AST_NODE_FLAG_LVALUE;
	}
}
