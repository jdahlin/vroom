/* vcc front end: _Complex arithmetic */
#include "cc-internal.h"

CC_PRIVATE ast_qualified_type cc_complex_element_type(cc *c, ast_qualified_type t) { return c->tags[AST_TYPE(c, t)->aux].f[0].ty; }

CC_PRIVATE ast_qualified_type cc_type_complex(cc *c, ast_qualified_type et)
{
	int k;

	et = AST_QUALIFIED_TYPE_UNQUALIFIED(canon(c, et));
	if (AST_TYPE_KIND(c, et) == AST_TYPE_ENUM)
		et = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_TYPE(c, et)->base);
	k = AST_TYPE_KIND(c, et);
	if (!c->cplx[k]) {
		uint32_t ti = cc_tag_new(c, AST_TYPE_STRUCT, 0, 0);
		tag *r = &c->tags[ti];

		r->nfield = 2;
		r->f = cc_heap_allocate(c, 2 * sizeof(field));
		memset(r->f, 0, 2 * sizeof(field));
		r->f[0].ty = r->f[1].ty = cc_type_basic(k);
		r->f[1].off = 8 * cc_type_size(c, cc_type_basic(k));
		r->flags |= AST_RECORD_FLAG_COMPLETE | AST_RECORD_FLAG_COMPLEX;
		cc_layout_record(c, r, NULL, NULL);
		c->cplx[k] = r->ty;
	}
	return AST_QUALIFIED_TYPE(c->cplx[k], 0);
}

CC_PRIVATE uint32_t cc_complex_node_new(cc *c, ast_qualified_type t, uint32_t re, uint32_t im, uint32_t loc)
{
	return cc_node_new_operands(c, AST_NODE_COMPLEX, AST_QUALIFIED_TYPE_UNQUALIFIED(t), loc, re, im);
}

/* part k (0 real, 1 imaginary) of complex n */
CC_PRIVATE uint32_t cc_complex_part(cc *c, uint32_t n, int k)
{
	ast_qualified_type t = AST_NODE(n)->ty;
	uint32_t m;

	if (AST_NODE(n)->op == AST_NODE_COMPLEX)
		return k ? AST_NODE(n)->b : AST_NODE(n)->a;
	m = cc_node_new(c, AST_NODE_MEMBER, cc_type_qualify(c, cc_complex_element_type(c, t), AST_QUALIFIED_TYPE_QUALIFIERS(t)), AST_NODE(n)->loc);
	AST_NODE(m)->a = n;
	AST_NODE(m)->b = AST_TYPE(c, t)->aux;
	AST_NODE(m)->c = (uint32_t)k;
	AST_NODE(m)->flags = AST_NODE(n)->flags & AST_NODE_FLAG_LVALUE;
	return m;
}

/* both parts of complex n, each usable more than once */
static void cc_complex_parts(cc *c, uint32_t n, uint32_t *re, uint32_t *im, uint32_t *pre)
{
	if (AST_NODE(n)->op == AST_NODE_COMPLEX && !cc_lowering_is_pure(c, n, 0)) {
		uint32_t p1, p2;

		*re = cc_lowering_once(c, AST_NODE(n)->a, &p1);
		*im = cc_lowering_once(c, AST_NODE(n)->b, &p2);
		*pre = p1 && p2 ? cc_node_new_operands(c, AST_NODE_COMMA, AST_NODE(*im)->ty, AST_NODE(n)->loc, p1, p2) : p1 ? p1 : p2;
		return;
	}
	n = cc_lowering_once(c, n, pre);
	*re = cc_complex_part(c, n, 0);
	*im = cc_complex_part(c, n, 1);
}

/* an arithmetic or complex rvalue as complex type t */
CC_PRIVATE uint32_t cc_complex_convert(cc *c, uint32_t n, ast_qualified_type t)
{
	ast_qualified_type et = cc_complex_element_type(c, t);
	uint32_t loc = AST_NODE(n)->loc;

	if (cc_type_is_complex(c, AST_NODE(n)->ty)) {
		uint32_t re, im, pre = 0;

		if (AST_QUALIFIED_TYPE_INDEX(canon(c, AST_NODE(n)->ty)) == AST_QUALIFIED_TYPE_INDEX(canon(c, t)))
			return n;
		if (AST_NODE(n)->op == AST_NODE_COMPLEX) {
			re = AST_NODE(n)->a, im = AST_NODE(n)->b;
		} else {
			n = cc_lowering_once(c, n, &pre);
			re = cc_complex_part(c, n, 0), im = cc_complex_part(c, n, 1);
		}
		return cc_lowering_sequence(c, pre, cc_complex_node_new(c, t, cc_conversion_apply(c, cc_node_rvalue(c, re), et), cc_conversion_apply(c, cc_node_rvalue(c, im), et), loc));
	}
	return cc_complex_node_new(c, t, cc_conversion_apply(c, n, et), cc_lowering_zero(c, et, loc), loc);
}

/* complex n != 0 */
CC_PRIVATE uint32_t cc_complex_to_bool(cc *c, uint32_t n)
{
	uint32_t re, im, pre, loc = AST_NODE(n)->loc;
	ast_qualified_type et = cc_complex_element_type(c, AST_NODE(n)->ty);

	cc_complex_parts(c, n, &re, &im, &pre);
	re = cc_node_binary_new(c, LEX_TOKEN_EXCLAMATION_EQUAL, cc_type_basic(AST_TYPE_INT), re, cc_lowering_zero(c, et, loc), loc);
	im = cc_node_binary_new(c, LEX_TOKEN_EXCLAMATION_EQUAL, cc_type_basic(AST_TYPE_INT), im, cc_lowering_zero(c, et, loc), loc);
	return cc_lowering_sequence(c, pre, cc_node_new_operands(c, AST_NODE_LOGICAL_OR, cc_type_basic(AST_TYPE_INT), loc, re, im));
}

/* Scale without first constructing a power outside binary128's range. */
static u128 cc_complex_float128_scale(u128 q, int shift)
{
	int sign, exp;
	u128 mant;

	if (!vsoftfloat_is_nonzero(q) || vsoftfloat_is_infinity(q) || vsoftfloat_is_nan(q))
		return q;
	vsoftfloat_unpack(q, &sign, &exp, &mant);
	return vsoftfloat_round_with_precision(sign, exp + shift, mant << 3, 112);
}

static u128 cc_complex_float_round(cc *c, ast_qualified_type t, u128 q)
{
	if (cc_type_is_float128(c, t))
		return vsoftfloat_to_precision(q, cc_float_precision_bits(c, t));
	if (AST_TYPE_KIND(c, t) == AST_TYPE_FLOAT16)
		return vsoftfloat_of_double(h_to_d(vsoftfloat_to_half(q)));
	return vsoftfloat_of_double(AST_TYPE_KIND(c, t) == AST_TYPE_FLOAT ? (double)vsoftfloat_to_float(q) : vsoftfloat_to_double(q));
}

/* Clang's Annex G constant division: scale the denominator, rounding every
 * operation at the target precision, including static initializers. */
static uint32_t cc_complex_divide_constant(cc *c, ast_qualified_type ct, ast_qualified_type et, uint32_t *args, uint32_t loc)
{
	u128 q[4], den, re, im;
	int be = 0;

	for (int i = 0; i < 4; i++) {
		cc_value v;

		if (!cc_eval(c, args[i], &v) || v.kind != CC_VALUE_FLOAT)
			return 0;
		q[i] = v.ld ? v.q : vsoftfloat_of_double(v.f);
	}
	u128 max = q[2] & ~Q_SIGN, other = q[3] & ~Q_SIGN;
	if (vsoftfloat_is_nan(max) || (!vsoftfloat_is_nan(other) && other > max))
		max = other;
	if (vsoftfloat_is_nonzero(max) && !vsoftfloat_is_nan(max) && !vsoftfloat_is_infinity(max)) {
		int sign, exp;
		u128 mant;

		vsoftfloat_unpack(max, &sign, &exp, &mant);
		vsoftfloat_normalize(&exp, &mant);
		be = exp - 16383;
	}
#define CC_COMPLEX_ROUND(x) cc_complex_float_round(c, et, (x))
#define CC_COMPLEX_MULTIPLY(a, b) CC_COMPLEX_ROUND(vsoftfloat_multiply((a), (b)))
#define CC_COMPLEX_ADD(a, b) CC_COMPLEX_ROUND(vsoftfloat_add((a), (b)))
#define CC_COMPLEX_SUBTRACT(a, b) CC_COMPLEX_ROUND(vsoftfloat_subtract((a), (b)))
#define CC_COMPLEX_DIVIDE(a, b) CC_COMPLEX_ROUND(vsoftfloat_divide((a), (b)))
	q[2] = CC_COMPLEX_ROUND(cc_complex_float128_scale(q[2], -be));
	q[3] = CC_COMPLEX_ROUND(cc_complex_float128_scale(q[3], -be));
	den = CC_COMPLEX_ADD(CC_COMPLEX_MULTIPLY(q[2], q[2]), CC_COMPLEX_MULTIPLY(q[3], q[3]));
	re = CC_COMPLEX_ROUND(cc_complex_float128_scale(CC_COMPLEX_DIVIDE(CC_COMPLEX_ADD(CC_COMPLEX_MULTIPLY(q[0], q[2]), CC_COMPLEX_MULTIPLY(q[1], q[3])), den), -be));
	im = CC_COMPLEX_ROUND(cc_complex_float128_scale(CC_COMPLEX_DIVIDE(CC_COMPLEX_SUBTRACT(CC_COMPLEX_MULTIPLY(q[1], q[2]), CC_COMPLEX_MULTIPLY(q[0], q[3])), den), -be));
	/* Annex G recovery, matching the helpers for zero and infinite operands. */
	if (vsoftfloat_is_nan(re) && vsoftfloat_is_nan(im)) {
		u128 inf = (u128)0x7fff << 112, one = (u128)16383 << 112;
		if (!vsoftfloat_is_nonzero(den) && (!vsoftfloat_is_nan(q[0]) || !vsoftfloat_is_nan(q[1]))) {
			inf |= q[2] & Q_SIGN;
			re = CC_COMPLEX_MULTIPLY(inf, q[0]);
			im = CC_COMPLEX_MULTIPLY(inf, q[1]);
		} else if ((vsoftfloat_is_infinity(q[0]) || vsoftfloat_is_infinity(q[1])) &&
		           !vsoftfloat_is_infinity(q[2]) && !vsoftfloat_is_nan(q[2]) && !vsoftfloat_is_infinity(q[3]) && !vsoftfloat_is_nan(q[3])) {
			q[0] = (q[0] & Q_SIGN) | (vsoftfloat_is_infinity(q[0]) ? one : 0);
			q[1] = (q[1] & Q_SIGN) | (vsoftfloat_is_infinity(q[1]) ? one : 0);
			re = CC_COMPLEX_MULTIPLY(inf, CC_COMPLEX_ADD(CC_COMPLEX_MULTIPLY(q[0], q[2]), CC_COMPLEX_MULTIPLY(q[1], q[3])));
			im = CC_COMPLEX_MULTIPLY(inf, CC_COMPLEX_SUBTRACT(CC_COMPLEX_MULTIPLY(q[1], q[2]), CC_COMPLEX_MULTIPLY(q[0], q[3])));
		} else if ((vsoftfloat_is_infinity(q[2]) || vsoftfloat_is_infinity(q[3])) &&
		           !vsoftfloat_is_infinity(q[0]) && !vsoftfloat_is_nan(q[0]) && !vsoftfloat_is_infinity(q[1]) && !vsoftfloat_is_nan(q[1])) {
			q[2] = (q[2] & Q_SIGN) | (vsoftfloat_is_infinity(q[2]) ? one : 0);
			q[3] = (q[3] & Q_SIGN) | (vsoftfloat_is_infinity(q[3]) ? one : 0);
			re = CC_COMPLEX_MULTIPLY(0, CC_COMPLEX_ADD(CC_COMPLEX_MULTIPLY(q[0], q[2]), CC_COMPLEX_MULTIPLY(q[1], q[3])));
			im = CC_COMPLEX_MULTIPLY(0, CC_COMPLEX_SUBTRACT(CC_COMPLEX_MULTIPLY(q[1], q[2]), CC_COMPLEX_MULTIPLY(q[0], q[3])));
		}
	}
#undef CC_COMPLEX_ROUND
#undef CC_COMPLEX_MULTIPLY
#undef CC_COMPLEX_ADD
#undef CC_COMPLEX_SUBTRACT
#undef CC_COMPLEX_DIVIDE
	if (cc_type_is_float128(c, et))
		return cc_complex_node_new(c, ct, cc_node_float128_new(c, vsoftfloat_to_precision(re, cc_float_precision_bits(c, et)), et, loc),
		               cc_node_float128_new(c, vsoftfloat_to_precision(im, cc_float_precision_bits(c, et)), et, loc), loc);
	if (AST_TYPE_KIND(c, et) == AST_TYPE_FLOAT16)
		return cc_complex_node_new(c, ct, cc_node_float_new(c, h_to_d(vsoftfloat_to_half(re)), et, loc),
		               cc_node_float_new(c, h_to_d(vsoftfloat_to_half(im)), et, loc), loc);
	return cc_complex_node_new(c, ct, cc_node_float_new(c, AST_TYPE_KIND(c, et) == AST_TYPE_FLOAT ? vsoftfloat_to_float(re) : vsoftfloat_to_double(re), et, loc),
	               cc_node_float_new(c, AST_TYPE_KIND(c, et) == AST_TYPE_FLOAT ? vsoftfloat_to_float(im) : vsoftfloat_to_double(im), et, loc), loc);
}

/* The runtime helper owns overflow/underflow scaling and IEEE special cases.
 * It returns the real complex ABI type, rather than an ordinary two-field struct. */
static uint32_t cc_complex_divide_call(cc *c, ast_qualified_type ct, ast_qualified_type et, uint32_t *a, uint32_t loc)
{
	ast_qualified_type result_ty = ct;
	if (AST_TYPE_KIND(c, et) == AST_TYPE_FLOAT16) {
		et = cc_type_basic(AST_TYPE_FLOAT);
		ct = cc_type_complex(c, et);
	}
	const char *name = AST_TYPE_KIND(c, et) == AST_TYPE_FLOAT ? "__divsc3" : !cc_type_is_float128(c, et) ? "__divdc3" :
	                   cc_float_precision_bits(c, et) == VSOFTFLOAT_X87_80 ? "__divxc3" : "__divtc3";
	uint32_t id = vintern_cstr(name), s, fn, n;
	ast_qualified_type params[4] = { et, et, et, et };
	ast_qualified_type ft = cc_type_function(c, ct, params, 4, 0);
	cc_id_list args;

	s = cc_symbol_new(c, AST_SYMBOL_FUNCTION, id, ft, loc);
	c->syms[s].sc = AST_STORAGE_EXTERN;
	c->syms[s].flags |= AST_SYMBOL_FLAG_GLOBAL | AST_SYMBOL_FLAG_USED;
	fn = cc_node_new(c, AST_NODE_VARIABLE, ft, loc);
	AST_NODE(fn)->a = s;
	cc_id_list_init(&args);
	for (int i = 0; i < 4; i++)
		cc_id_list_append(c, &args, cc_conversion_apply(c, cc_node_rvalue(c, a[i]), et));
	n = cc_node_new(c, AST_NODE_CALL, ct, loc);
	AST_NODE(n)->a = cc_conversion_decay(c, fn);
	AST_NODE(n)->b = cc_id_list_freeze(c, &args);
	AST_NODE(n)->c = 4;
	return cc_complex_convert(c, n, result_ty);
}

/* l op r with a complex operand: + - * / == != on the parts */
CC_PRIVATE uint32_t cc_complex_binary(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	ast_qualified_type lt = AST_NODE(l)->ty, rt = AST_NODE(r)->ty;
	bool lc = cc_type_is_complex(c, lt), rc = cc_type_is_complex(c, rt);
	ast_qualified_type le = lc ? cc_complex_element_type(c, lt) : lt, re_ = rc ? cc_complex_element_type(c, rt) : rt, et, ct;
	uint32_t a, b = 0, cr, d = 0, p1 = 0, p2 = 0, x, y;

	if (!cc_type_is_arithmetic(c, le) || !cc_type_is_arithmetic(c, re_) ||
	    !(op == LEX_TOKEN_PLUS || op == LEX_TOKEN_MINUS || op == LEX_TOKEN_STAR || op == LEX_TOKEN_SLASH || op == LEX_TOKEN_EQUAL_EQUAL || op == LEX_TOKEN_EXCLAMATION_EQUAL)) {
		cc_operands_invalid_report(c, op, l, r, loc);
		return cc_node_integer_new(c, 0, cc_type_basic(AST_TYPE_INT), loc);
	}
	et = cc_type_arithmetic_conversion(c, cc_type_promote(c, AST_QUALIFIED_TYPE_UNQUALIFIED(le)), cc_type_promote(c, AST_QUALIFIED_TYPE_UNQUALIFIED(re_)));
	ct = cc_type_complex(c, et);
	if (lc)
		cc_complex_parts(c, cc_complex_convert(c, l, ct), &a, &b, &p1);
	else
		a = cc_lowering_once(c, cc_conversion_apply(c, l, et), &p1);
	if (rc)
		cc_complex_parts(c, cc_complex_convert(c, r, ct), &cr, &d, &p2);
	else
		cr = cc_lowering_once(c, cc_conversion_apply(c, r, et), &p2);
#define CC_COMPLEX_BINARY(o, u, v) cc_node_binary_new(c, o, et, u, v, loc)
	switch (op) {
	case LEX_TOKEN_PLUS: case LEX_TOKEN_MINUS:
		x = CC_COMPLEX_BINARY(op, a, cr);
		y = !lc ? (op == LEX_TOKEN_MINUS ? neg(c, d) : d) : !rc ? b : CC_COMPLEX_BINARY(op, b, d);
		break;
	case LEX_TOKEN_STAR:
		if (!lc)
			x = CC_COMPLEX_BINARY(LEX_TOKEN_STAR, a, cr), y = CC_COMPLEX_BINARY(LEX_TOKEN_STAR, a, d);
		else if (!rc)
			x = CC_COMPLEX_BINARY(LEX_TOKEN_STAR, a, cr), y = CC_COMPLEX_BINARY(LEX_TOKEN_STAR, b, cr);
		else
			x = CC_COMPLEX_BINARY(LEX_TOKEN_MINUS, CC_COMPLEX_BINARY(LEX_TOKEN_STAR, a, cr), CC_COMPLEX_BINARY(LEX_TOKEN_STAR, b, d)), y = CC_COMPLEX_BINARY(LEX_TOKEN_PLUS, CC_COMPLEX_BINARY(LEX_TOKEN_STAR, a, d), CC_COMPLEX_BINARY(LEX_TOKEN_STAR, b, cr));
		break;
	case LEX_TOKEN_SLASH:
		if (!rc) {
			x = CC_COMPLEX_BINARY(LEX_TOKEN_SLASH, a, cr), y = CC_COMPLEX_BINARY(LEX_TOKEN_SLASH, b, cr);
		} else {
			if (cc_type_is_float(c, et)) {
				uint32_t args[4] = { a, lc ? b : cc_lowering_zero(c, et, loc), cr, d };
				uint32_t result = cc_complex_divide_constant(c, ct, et, args, loc);

				if (!result)
					result = cc_complex_divide_call(c, ct, et, args, loc);
				return cc_lowering_sequence(c, p1, cc_lowering_sequence(c, p2, result));
			}
			uint32_t den = CC_COMPLEX_BINARY(LEX_TOKEN_PLUS, CC_COMPLEX_BINARY(LEX_TOKEN_STAR, cr, cr), CC_COMPLEX_BINARY(LEX_TOKEN_STAR, d, d));

			if (!lc) {
				x = CC_COMPLEX_BINARY(LEX_TOKEN_SLASH, CC_COMPLEX_BINARY(LEX_TOKEN_STAR, a, cr), den);
				y = CC_COMPLEX_BINARY(LEX_TOKEN_SLASH, neg(c, CC_COMPLEX_BINARY(LEX_TOKEN_STAR, a, d)), den);
			} else {
				x = CC_COMPLEX_BINARY(LEX_TOKEN_SLASH, CC_COMPLEX_BINARY(LEX_TOKEN_PLUS, CC_COMPLEX_BINARY(LEX_TOKEN_STAR, a, cr), CC_COMPLEX_BINARY(LEX_TOKEN_STAR, b, d)), den);
				y = CC_COMPLEX_BINARY(LEX_TOKEN_SLASH, CC_COMPLEX_BINARY(LEX_TOKEN_MINUS, CC_COMPLEX_BINARY(LEX_TOKEN_STAR, b, cr), CC_COMPLEX_BINARY(LEX_TOKEN_STAR, a, d)), den);
			}
		}
		break;
	default: {	/* == != */
		int lop = op == LEX_TOKEN_EQUAL_EQUAL ? AST_NODE_LOGICAL_AND : AST_NODE_LOGICAL_OR;
		uint32_t z = cc_lowering_zero(c, et, loc);

		x = cc_node_binary_new(c, op, cc_type_basic(AST_TYPE_INT), a, cr, loc);
		y = cc_node_binary_new(c, op, cc_type_basic(AST_TYPE_INT), lc ? b : z, rc ? d : z, loc);
		return cc_lowering_sequence(c, p1, cc_lowering_sequence(c, p2, cc_node_new_operands(c, lop, cc_type_basic(AST_TYPE_INT), loc, x, y)));
	}
	}
#undef CC_COMPLEX_BINARY
	return cc_lowering_sequence(c, p1, cc_lowering_sequence(c, p2, cc_complex_node_new(c, ct, x, y, loc)));
}

/* -x, ~x (conjugate), +x of a complex x */
CC_PRIVATE uint32_t cc_complex_unary(cc *c, int op, uint32_t n, uint32_t loc)
{
	uint32_t re, im, pre;
	ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE(n)->ty);

	if (op == LEX_TOKEN_PLUS)
		return n;
	cc_complex_parts(c, n, &re, &im, &pre);
	return cc_lowering_sequence(c, pre, cc_complex_node_new(c, t, op == LEX_TOKEN_MINUS ? neg(c, re) : re, neg(c, im), loc));
}

/* Atomically update both components with a compare-exchange retry loop.
 * The expected value is refreshed by each failed CAS, including the imaginary
 * payload; only the successful old/new snapshot becomes the expression value. */
static uint32_t cc_complex_atomic_increment(cc *c, uint32_t n, int op, uint32_t loc)
{
	ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(cc_type_unatomic(c, AST_NODE(n)->ty)), et = cc_complex_element_type(c, t);
	bool inc = op == AST_NODE_PRE_INCREMENT || op == AST_NODE_POST_INCREMENT, post = op == AST_NODE_POST_INCREMENT || op == AST_NODE_POST_DECREMENT;
	uint32_t vars[2], pointers[2], pre;
	uint32_t address = cc_node_new(c, AST_NODE_ADDRESS, cc_type_pointer(c, AST_NODE(n)->ty), loc);
	AST_NODE(address)->a = n;
	address = cc_lowering_once(c, address, &pre);
	for (int i = 0; i < 2; i++) {
		uint32_t sym = cc_symbol_new(c, AST_SYMBOL_VARIABLE, 0, t, loc);
		c->syms[sym].sc = AST_STORAGE_AUTO;
		vars[i] = cc_node_new(c, AST_NODE_VARIABLE, t, loc);
		AST_NODE(vars[i])->a = sym;
		AST_NODE(vars[i])->flags = AST_NODE_FLAG_LVALUE;
		pointers[i] = cc_node_new(c, AST_NODE_ADDRESS, cc_type_pointer(c, t), loc);
		AST_NODE(pointers[i])->a = vars[i];
	}
	cc_id_list args, body;
	cc_id_list_init(&args);
	cc_id_list_init(&body);
	if (pre) {
		cc_id_list_append(c, &body, cc_node_new_operands(c, AST_NODE_DECLARATION, cc_type_basic(AST_TYPE_VOID), loc, AST_NODE(AST_NODE(pre)->a)->a, 0));
		cc_id_list_append(c, &body, cc_statement_new(c, AST_NODE_EXPRESSION, loc, pre, 0, 0));
	}
	for (int i = 0; i < 2; i++)
		cc_id_list_append(c, &body, cc_node_new_operands(c, AST_NODE_DECLARATION, cc_type_basic(AST_TYPE_VOID), loc, AST_NODE(vars[i])->a, 0));
	cc_id_list_append(c, &args, address);
	cc_id_list_append(c, &args, pointers[0]);
	cc_id_list_append(c, &args, cc_node_integer_new(c, 5, cc_type_basic(AST_TYPE_INT), loc));
	uint32_t load = cc_builtin_node_new(c, CC_BUILTIN_ATOMIC_LOAD, cc_type_basic(AST_TYPE_VOID), loc, &args);
	cc_id_list_append(c, &body, cc_statement_new(c, AST_NODE_EXPRESSION, loc, load, 0, 0));
	uint32_t re = cc_binary_build(c, inc ? LEX_TOKEN_PLUS : LEX_TOKEN_MINUS, cc_node_rvalue(c, cc_complex_part(c, vars[0], 0)), cc_node_integer_new(c, 1, cc_type_basic(AST_TYPE_INT), loc), loc);
	uint32_t desired = cc_complex_node_new(c, t, cc_conversion_apply(c, re, et), cc_node_rvalue(c, cc_complex_part(c, vars[0], 1)), loc);
	uint32_t update = cc_assignment_build(c, 0, vars[1], desired, loc);
	args.n = 0;
	cc_id_list_append(c, &args, address);
	cc_id_list_append(c, &args, pointers[0]);
	cc_id_list_append(c, &args, pointers[1]);
	cc_id_list_append(c, &args, cc_node_integer_new(c, 0, cc_type_basic(AST_TYPE_INT), loc));
	cc_id_list_append(c, &args, cc_node_integer_new(c, 5, cc_type_basic(AST_TYPE_INT), loc));
	cc_id_list_append(c, &args, cc_node_integer_new(c, 5, cc_type_basic(AST_TYPE_INT), loc));
	uint32_t cas = cc_builtin_node_new(c, CC_BUILTIN_ATOMIC_CMPXCHG, cc_type_basic(AST_TYPE_BOOL), loc, &args);
	uint32_t retry = cc_node_binary_new(c, LEX_TOKEN_EQUAL_EQUAL, cc_type_basic(AST_TYPE_INT), cas, cc_node_integer_new(c, 0, cc_type_basic(AST_TYPE_INT), loc), loc);
	cc_id_list_append(c, &body, cc_statement_new(c, AST_NODE_DO, loc, retry, cc_statement_new(c, AST_NODE_EXPRESSION, loc, update, 0, 0), 0));
	cc_id_list_append(c, &body, cc_statement_new(c, AST_NODE_EXPRESSION, loc, cc_node_rvalue(c, vars[post ? 0 : 1]), 0, 0));
	uint32_t block = cc_statement_new(c, AST_NODE_BLOCK, loc, 0, cc_id_list_freeze(c, &body), body.n);
	uint32_t out = cc_node_new(c, AST_NODE_STATEMENT_EXPRESSION, t, loc);
	AST_NODE(out)->a = block;
	AST_NODE(out)->aux = (uint16_t)op;
	return out;
}

/* Increment/decrement only the real component, retaining the original
 * imaginary representation and evaluating the operand's location once. */
CC_PRIVATE uint32_t cc_complex_increment(cc *c, uint32_t n, int op, uint32_t loc)
{
	bool inc = op == AST_NODE_PRE_INCREMENT || op == AST_NODE_POST_INCREMENT;
	bool post = op == AST_NODE_POST_INCREMENT || op == AST_NODE_POST_DECREMENT;
	ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(cc_type_unatomic(c, AST_NODE(n)->ty)), et = cc_complex_element_type(c, t);
	uint32_t pre = 0, save, tmp, value, put;
	if (!c->o->c2y) {
		static const char *const g[] = { "pedantic", NULL };
		CC_REPORT_EXTENSION_MICROSOFT(c, inc ? "++" : "--", "c2y-extensions", g, loc,
		            "'%s' on an object of complex type is a C2y extension", inc ? "++" : "--");
	}
	if (AST_TYPE(c, AST_NODE(n)->ty)->flags & AST_TYPE_FLAG_ATOMIC) {
		if (cc_atomic_wide_type_is_wide(c, AST_NODE(n)->ty))
			return cc_atomic_wide_assignment_build(c, n, 0, inc ? LEX_TOKEN_PLUS : LEX_TOKEN_MINUS, post, loc);
		if (cc_type_size(c, t) <= 8)
			return cc_complex_atomic_increment(c, n, op, loc);
		cc_report_error(c, loc, "atomic complex increment and decrement are not supported yet");
		return cc_node_error_new(c, loc);
	}
	if (AST_NODE(n)->op != AST_NODE_VARIABLE) {
		/* This is an internal address, also valid for a register aggregate. */
		uint32_t address = cc_node_new(c, AST_NODE_ADDRESS, cc_type_pointer(c, AST_NODE(n)->ty), loc);
		AST_NODE(address)->a = n;
		address = cc_lowering_once(c, address, &pre);
		n = cc_dereference_build(c, address, loc);
	}
	uint32_t sym = cc_symbol_new(c, AST_SYMBOL_VARIABLE, 0, t, loc);
	c->syms[sym].sc = AST_STORAGE_AUTO;
	tmp = cc_node_new(c, AST_NODE_VARIABLE, t, loc);
	AST_NODE(tmp)->a = sym;
	AST_NODE(tmp)->flags = AST_NODE_FLAG_LVALUE;
	save = cc_node_new_operands(c, AST_NODE_ASSIGN, t, loc, tmp, cc_node_rvalue(c, n));
	value = cc_binary_build(c, inc ? LEX_TOKEN_PLUS : LEX_TOKEN_MINUS, cc_node_rvalue(c, cc_complex_part(c, tmp, 0)), cc_node_integer_new(c, 1, cc_type_basic(AST_TYPE_INT), loc), loc);
	value = cc_conversion_apply(c, value, et);
	/* Materialize the arithmetic once: storing the updated real component
	 * and producing a prefix result must not repeat a trapping FP operation. */
	uint32_t desired_sym = cc_symbol_new(c, AST_SYMBOL_VARIABLE, 0, et, loc);
	c->syms[desired_sym].sc = AST_STORAGE_AUTO;
	uint32_t desired = cc_node_new(c, AST_NODE_VARIABLE, et, loc);
	AST_NODE(desired)->a = desired_sym;
	AST_NODE(desired)->flags = AST_NODE_FLAG_LVALUE;
	uint32_t compute = cc_node_new_operands(c, AST_NODE_ASSIGN, et, loc, desired, value);
	value = cc_node_rvalue(c, desired);
	put = cc_assignment_build(c, 0, cc_complex_part(c, n, 0), value, loc);
	uint32_t result = post ? cc_node_rvalue(c, tmp) : cc_complex_node_new(c, t, value, cc_node_rvalue(c, cc_complex_part(c, tmp, 1)), loc);
	uint32_t out = cc_lowering_sequence(c, pre, cc_lowering_sequence(c, save, cc_lowering_sequence(c, compute, cc_lowering_sequence(c, put, result))));
	AST_NODE(out)->aux = (uint16_t)op; /* retain the source operation for unused-value diagnostics */
	return out;
}
