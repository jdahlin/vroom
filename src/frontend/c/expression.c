/* vcc front end: expression semantic actions (operators, members) and primary and postfix parsing */
#include <ctype.h>

#include "cc-internal.h"

/* ---------------- expressions: operators ---------------- */

CC_PRIVATE bool cc_operator_is_comparison(int op)
{
	return op == LEX_TOKEN_LESS || op == LEX_TOKEN_LESS_EQUAL || op == LEX_TOKEN_GREATER || op == LEX_TOKEN_GREATER_EQUAL ||
	       op == LEX_TOKEN_EQUAL_EQUAL || op == LEX_TOKEN_EXCLAMATION_EQUAL;
}

/* clang -pedantic: arithmetic on void * (GNU: element size 1) */
static inline void cc_pointer_void_arithmetic_check(cc *c, ast_qualified_type pt, uint32_t loc, bool two)
{
	static const char *const g[] = { "pointer-arith", "gnu", "pedantic", NULL };

	if (unlikely(AST_TYPE_KIND(c, AST_TYPE(c, pt)->base) == AST_TYPE_SVE_BUILTIN)) {
		cc_report_error(c, loc, "arithmetic on a pointer to sizeless type '%s'", tyname(c, AST_TYPE(c, pt)->base));
		return;
	}
	if (unlikely(AST_TYPE_KIND(c, AST_TYPE(c, pt)->base) == AST_TYPE_VOID))
		CC_REPORT_EXTENSION(c, "gnu-pointer-arith", g, loc,
			 two ? "arithmetic on pointers to void is a GNU extension" : "arithmetic on a pointer to void is a GNU extension");
}

/* clang (GCC vector splat): the scalar s may become the element type et without truncation */
static bool cc_vector_scalar_is_ok(cc *c, uint32_t s, ast_qualified_type et)
{
	ast_qualified_type st = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE(s)->ty);
	if (AST_TYPE_KIND(c, et) == AST_TYPE_BOOL) return cc_type_is_integer(c, st);
	cc_value v;
	bool k;

	/* (the type before the integer promotion: short, char) */
	if (AST_NODE(s)->op == AST_NODE_CAST && AST_NODE(s)->aux == AST_CAST_INTEGER && !c->span[s].e && cc_type_is_integer(c, AST_NODE(AST_NODE(s)->a)->ty))
		st = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE(AST_NODE(s)->a)->ty);
	if (AST_TYPE_KIND(c, st) == AST_TYPE_ENUM)
		st = AST_TYPE(c, st)->base;
	memset(&v, 0, sizeof v);
	k = cc_constant_eval(c, s, &v) && (v.kind == CC_VALUE_INTEGER || v.kind == CC_VALUE_FLOAT);
	if (cc_type_is_integer(c, et)) {
		unsigned b = (unsigned)cc_type_size(c, et) * 8;

		if (cc_type_is_float(c, st))
			return false;
		if (k && v.kind == CC_VALUE_INTEGER) {
			if (cc_type_is_signed(c, et))
				return v.i >= -((i128)1 << (b - 1)) && v.i < ((i128)1 << (b - 1));
			return v.i >= -((i128)1 << (b - 1)) && (b >= 128 || v.i < ((i128)1 << b));	/* (clang: -18 into unsigned lanes) */
		}
		/* Signed-to-unsigned at the same width preserves every source bit.
		 * The reverse conversion can lose the unsigned scalar's top bit. */
		return cc_type_size(c, st) < cc_type_size(c, et) ||
		       (cc_type_size(c, st) == cc_type_size(c, et) && (!cc_type_is_signed(c, et) || cc_type_is_signed(c, st)));
	}
	if (k && v.kind == CC_VALUE_INTEGER)
		return cc_type_size(c, et) == 4 ? (i128)(float)v.i == v.i : (i128)(double)v.i == v.i;
	if (k && v.kind == CC_VALUE_FLOAT && !v.ld)
		return cc_type_size(c, et) != 4 || (double)(float)v.f == v.f;
	if (cc_type_is_float(c, st))
		return cc_type_size(c, st) <= cc_type_size(c, et);
	return cc_type_size(c, st) * 8 <= (cc_type_size(c, et) == 4 ? 24u : 53u);
}

/* the scalar operand s of a GNU vector operation (vt) at the operator loc: false after the error */
static bool cc_vector_scalar_check(cc *c, uint32_t s, ast_qualified_type vt, uint32_t loc)
{
	if ((AST_TYPE(c, vt)->ext && !cc_type_is_bool_vector(c, vt)) || !cc_type_is_arithmetic(c, AST_NODE(s)->ty) || cc_vector_scalar_is_ok(c, s, AST_TYPE(c, vt)->base))
		return true;
	cc_report_error(c, loc, "cannot convert between scalar type '%s' and vector type '%s' as implicit conversion would cause truncation",
		 tyname(c, AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE(s)->ty)), tyname(c, vt));
	return false;
}

/* ext_vector_type swizzles: v.x, v.xyz, v.s01, v.hi/.lo/.even/.odd (clang's OpenCL components) */
static int cc_swizzle_index(char ch, int set)
{
	static const char *const sets[2] = { "xyzw", "rgba" };
	const char *q = strchr(sets[set], ch);

	return ch && q ? (int)(q - sets[set]) : -1;
}

static void cc_swizzle_range_error(cc *c, uint32_t loc, ast_qualified_type vt)
{
	diag_span r = { loc, pp_token_end_find(c->p, c->prevloc) };	/* (loc: the '.', the name just consumed) */

	report(c, "error", loc, &r, 1, NULL, 0, "vector component access exceeds type '%s'", tyname(c, vt));
}

static uint32_t cc_vector_swizzle_build(cc *c, uint32_t n, uint32_t id, uint32_t loc)
{
	ast_qualified_type vt = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE(n)->ty), et = AST_TYPE(c, vt)->base;
	int ne = AST_TYPE(c, vt)->ext, k = 0, ix[16];
	const char *s = cc_identifier_string(id);
	int len = cc_identifier_length(id);

	if (cc_type_is_bool_vector(c, vt)) {
		cc_report_error(c, loc, "illegal vector component name '%.*s'", len, s);
		return cc_node_integer_new(c, 0, et, loc);
	}

	if (len == 2 && !memcmp(s, "hi", 2)) {
		for (int i = 0; i < (ne + 1) / 2; i++) ix[k++] = (ne + 1) / 2 + i < ne ? (ne + 1) / 2 + i : -1;
	} else if (len == 2 && !memcmp(s, "lo", 2)) {
		for (int i = 0; i < (ne + 1) / 2; i++) ix[k++] = i;
	} else if ((len == 4 && !memcmp(s, "even", 4)) || (len == 3 && !memcmp(s, "odd", 3))) {
		for (int i = len == 3; i < ne && k < 16; i += 2) ix[k++] = i;
	} else if ((s[0] == 's' || s[0] == 'S') && len > 1) {
		for (int i = 1; i < len && k < 16; i++) {
			int d = isdigit((unsigned char)s[i]) ? s[i] - '0' : isxdigit((unsigned char)s[i]) ? (s[i] | 32) - 'a' + 10 : 99;

			if (d >= ne) {
				cc_swizzle_range_error(c, loc, vt);
				return cc_node_integer_new(c, 0, et, loc);
			}
			ix[k++] = d;
		}
	} else {
		int set = cc_swizzle_index(s[0], 0) >= 0 ? 0 : 1;

		for (int i = 0; i < len && k < 16; i++) {
			int d = cc_swizzle_index(s[i], set);

			if (d < 0) {
				cc_report_error(c, loc, "illegal vector component name '%c'", s[i]);
				return cc_node_integer_new(c, 0, et, loc);
			}
			if (d >= ne) {
				cc_swizzle_range_error(c, loc, vt);
				return cc_node_integer_new(c, 0, et, loc);
			}
			ix[k++] = d;
		}
	}
	if (k == 1) {	/* one lane: v[i] (an lvalue when v is one) */
		uint32_t b;

		if (ix[0] < 0)
			return cc_node_integer_new(c, 0, et, loc);
		if (AST_NODE(n)->flags & AST_NODE_FLAG_LVALUE)
			b = cc_cast_insert(c, cc_address_of_build(c, n, loc), cc_type_pointer(c, AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(et), AST_QUALIFIED_TYPE_QUALIFIERS(AST_NODE(n)->ty))), AST_CAST_POINTER_TO_POINTER);
		else
			b = cc_cast_insert(c, cc_node_rvalue(c, n), cc_type_pointer(c, et), AST_CAST_VECTOR_ADDRESS);
		b = cc_node_new_operands(c, AST_NODE_INDEX, AST_TYPE(c, AST_NODE(b)->ty)->base, loc, cc_node_rvalue(c, b), cc_node_integer_new(c, ix[0], c->t_ptrdiff, loc));
		AST_NODE(b)->flags = AST_NODE_FLAG_LVALUE;
		AST_NODE(b)->c = vt;
		return b;
	}
	{	/* several: a shuffle into an ext vector of k lanes (an rvalue; vcc: not assignable) */
		uint64_t es = cc_type_size(c, et), p = 1;
		ctype rv;
		cc_id_list args;
		uint32_t a = cc_node_rvalue(c, n);
		ast_qualified_type rt;

		while (p < (uint64_t)k * es)
			p <<= 1;
		rv = *AST_TYPE(c, cc_type_vector(c, et, (uint32_t)p));
		rv.ext = (uint16_t)k;
		rt = AST_QUALIFIED_TYPE(cc_type_intern(c, &rv), 0);
		cc_id_list_init(&args);
		cc_id_list_append(c, &args, a);
		cc_id_list_append(c, &args, a);
		for (uint64_t i = 0; i < p / es; i++)
			cc_id_list_append(c, &args, cc_node_integer_new(c, i < (uint64_t)k ? ix[i] : -1, cc_type_basic(AST_TYPE_INT), loc));
		return cc_builtin_node_new(c, CC_BUILTIN_SHUFFLEVECTOR, rt, loc, &args);
	}
}

CC_PRIVATE uint32_t cc_binary_build(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	ast_qualified_type lt, rt;

	if (op == LEX_TOKEN_AMPERSAND_AMPERSAND || op == LEX_TOKEN_PIPE_PIPE) {
		l = cc_condition_scalar(c, cc_conversion_decay(c, l), "logical operator");
		r = cc_condition_scalar(c, cc_conversion_decay(c, r), "logical operator");
		return cc_node_new_operands(c, op == LEX_TOKEN_AMPERSAND_AMPERSAND ? AST_NODE_LOGICAL_AND : AST_NODE_LOGICAL_OR, cc_type_basic(AST_TYPE_INT), loc, l, r);
	}
	l = cc_node_rvalue(c, l), r = cc_node_rvalue(c, r);
	lt = AST_NODE(l)->ty, rt = AST_NODE(r)->ty;
	if (cc_type_is_complex(c, lt) || cc_type_is_complex(c, rt))
		return cc_complex_binary(c, op, l, r, loc);
	if (cc_type_is_vector(c, lt) || cc_type_is_vector(c, rt)) {
		/* lane-wise: two vectors of one size, or a vector and a scalar (each lane against it);
		 * comparisons give the signed integer vector of the same lanes (-1 / 0) */
		ast_qualified_type vt = AST_QUALIFIED_TYPE_UNQUALIFIED(cc_type_is_vector(c, lt) ? lt : rt), et = AST_TYPE(c, vt)->base;
		bool both = cc_type_is_vector(c, lt) && cc_type_is_vector(c, rt);
		bool shift = op == LEX_TOKEN_LESS_LESS || op == LEX_TOKEN_GREATER_GREATER;
		ast_qualified_type res = vt;

		if (cc_type_is_bool_vector(c, vt) && !(op == LEX_TOKEN_AMPERSAND || op == LEX_TOKEN_PIPE || op == LEX_TOKEN_CARET || cc_operator_is_comparison(op)))
			cc_operands_invalid_report(c, op, l, r, loc);

		if ((both && AST_TYPE(c, lt)->size != AST_TYPE(c, rt)->size) ||
		    (!both && !cc_type_is_arithmetic(c, cc_type_is_vector(c, lt) ? rt : lt)) ||
		    ((op == LEX_TOKEN_PERCENT || op == LEX_TOKEN_AMPERSAND || op == LEX_TOKEN_PIPE || op == LEX_TOKEN_CARET || shift) && !cc_type_is_integer(c, et)))
			cc_operands_invalid_report(c, op, l, r, loc);
		else if (!both && !shift && !cc_vector_scalar_check(c, cc_type_is_vector(c, lt) ? r : l, vt, loc))
			return cc_node_binary_new(c, op, cc_operator_is_comparison(op) ? cc_type_vector_mask(c, vt) : vt, l, l, loc);	/* (stands in; diagnosed) */
		if (both) {
			l = cc_conversion_apply(c, l, vt);
			r = cc_conversion_apply(c, r, both && shift ? AST_QUALIFIED_TYPE_UNQUALIFIED(rt) : vt);
		} else if (cc_type_is_vector(c, lt)) {
			r = cc_conversion_apply(c, r, et);
		} else {
			l = cc_conversion_apply(c, l, et);
		}
		if (cc_operator_is_comparison(op))
			res = cc_type_vector_mask(c, vt);
		return cc_node_binary_new(c, op, res, l, r, loc);
	}
	if (cc_type_is_arithmetic(c, lt) && cc_type_is_arithmetic(c, rt)) {
		ast_qualified_type ct;

		if (op == LEX_TOKEN_LESS_LESS || op == LEX_TOKEN_GREATER_GREATER) {
			if (!cc_type_is_integer(c, lt) || !cc_type_is_integer(c, rt))
				cc_operands_invalid_report(c, op, l, r, loc);
			return cc_node_binary_new(c, op, lt, l, r, loc);
		}
		if ((op == LEX_TOKEN_PERCENT || op == LEX_TOKEN_AMPERSAND || op == LEX_TOKEN_PIPE || op == LEX_TOKEN_CARET) &&
		    (!cc_type_is_integer(c, lt) || !cc_type_is_integer(c, rt)))
			cc_operands_invalid_report(c, op, l, r, loc);
		ct = cc_type_arithmetic_conversion(c, lt, rt);
		l = cc_conversion_apply(c, l, ct), r = cc_conversion_apply(c, r, ct);
		return cc_node_binary_new(c, op, cc_operator_is_comparison(op) ? cc_type_basic(AST_TYPE_INT) : ct, l, r, loc);
	}
	if (op == LEX_TOKEN_PLUS && cc_type_is_pointer(c, rt) && cc_type_is_integer(c, lt)) {
		uint32_t t = l;

		l = r, r = t;
		lt = AST_NODE(l)->ty, rt = AST_NODE(r)->ty;
	}
	if ((op == LEX_TOKEN_PLUS || op == LEX_TOKEN_MINUS) && cc_type_is_pointer(c, lt) && cc_type_is_integer(c, rt)) {
		cc_pointer_void_arithmetic_check(c, lt, loc, false);
		r = cc_conversion_apply(c, r, c->t_ptrdiff);
		return cc_node_binary_new(c, op, lt, l, r, loc);
	}
	if (op == LEX_TOKEN_MINUS && cc_type_is_pointer(c, lt) && cc_type_is_pointer(c, rt)) {
		cc_pointer_void_arithmetic_check(c, lt, loc, true);
		return cc_node_binary_new(c, op, c->t_ptrdiff, l, r, loc);
	}
	if (cc_operator_is_comparison(op) && (cc_type_is_pointer(c, lt) || cc_type_is_pointer(c, rt))) {
		ast_qualified_type ot = cc_type_is_pointer(c, lt) ? rt : lt;

		if (!cc_type_is_pointer(c, ot) && !cc_type_is_integer(c, ot) && AST_TYPE_KIND(c, ot) != AST_TYPE_ENUM) {	/* (p == 0.5) */
			cc_operands_invalid_report(c, op, l, r, loc);
			return cc_node_error_new(c, loc);
		}
		if (!cc_type_is_pointer(c, lt))
			l = cc_conversion_apply(c, l, rt);
		else if (!cc_type_is_pointer(c, rt))
			r = cc_conversion_apply(c, r, lt);
		return cc_node_binary_new(c, op, cc_type_basic(AST_TYPE_INT), l, r, loc);
	}
	cc_operands_invalid_report(c, op, l, r, loc);
	return cc_node_error_new(c, loc);
}

CC_PRIVATE uint32_t cc_assignment_build(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	ast_qualified_type lt = AST_QUALIFIED_TYPE_UNQUALIFIED(cc_type_unatomic(c, AST_NODE(l)->ty));
	uint32_t n;

	if (unlikely(!cc_node_require_lvalue(c, l, loc)))
		return cc_node_error_new(c, loc);	/* clang: the assignment is invalid */
	cc_array_bounds_warn(c, l, false);
	r = cc_conversion_decay(c, r);
	if ((AST_TYPE(c, AST_NODE(l)->ty)->flags & AST_TYPE_FLAG_ATOMIC) &&
	    (cc_atomic_wide_type_is_wide(c, AST_NODE(l)->ty) || (op && cc_type_is_complex(c, lt))))
		return cc_atomic_wide_assignment_build(c, l, r, op, false, loc);
	if (op && (cc_type_is_complex(c, lt) || cc_type_is_complex(c, AST_NODE(r)->ty)))	/* l = l op r (l evaluated twice) */
		return cc_assignment_build(c, 0, l, cc_complex_binary(c, op, cc_node_rvalue(c, l), cc_node_rvalue(c, r), loc), loc);
	if (!op) {
		const field *bf = cc_node_bitfield(c, l);

		if (bf)
			cc_conversion_bitfield_constant_check(c, bf, r, loc);
		r = cc_conversion_apply_as(c, r, lt, CC_CONVERSION_CONTEXT_ASSIGNMENT, loc);
		n = cc_node_new_operands(c, AST_NODE_ASSIGN, lt, loc, l, r);
		return n;
	}
	{
		ast_qualified_type pt = cc_node_promoted_type(c, l), rt;

		r = cc_node_rvalue(c, r);
		rt = AST_NODE(r)->ty;
		if (cc_type_is_pointer(c, lt) && (op == LEX_TOKEN_PLUS || op == LEX_TOKEN_MINUS)) {
			cc_pointer_void_arithmetic_check(c, lt, loc, false);
			r = cc_conversion_apply(c, r, c->t_ptrdiff);
			pt = lt;
		} else if (cc_type_is_vector(c, lt)) {
			if (cc_type_is_bool_vector(c, lt) && op != LEX_TOKEN_AMPERSAND && op != LEX_TOKEN_PIPE && op != LEX_TOKEN_CARET)
				cc_report_error(c, loc, "invalid operands to compound assignment");
			if (cc_type_is_vector(c, rt) ? AST_TYPE(c, lt)->size != AST_TYPE(c, rt)->size : !cc_type_is_arithmetic(c, rt))
				cc_report_error(c, loc, "invalid operands to compound assignment");
			else if (!cc_type_is_vector(c, rt) && op != LEX_TOKEN_LESS_LESS && op != LEX_TOKEN_GREATER_GREATER)
				cc_vector_scalar_check(c, r, AST_QUALIFIED_TYPE_UNQUALIFIED(lt), loc);
			r = cc_conversion_apply(c, r, cc_type_is_vector(c, rt) ? AST_QUALIFIED_TYPE_UNQUALIFIED(lt) : AST_TYPE(c, lt)->base);
			pt = AST_QUALIFIED_TYPE_UNQUALIFIED(lt);
		} else if (op == LEX_TOKEN_LESS_LESS || op == LEX_TOKEN_GREATER_GREATER) {
			/* computation type is the promoted lhs */
		} else if (cc_type_is_arithmetic(c, pt) && cc_type_is_arithmetic(c, rt)) {
			pt = cc_type_arithmetic_conversion(c, pt, rt);
			r = cc_conversion_apply(c, r, pt);
		} else {
			cc_report_error(c, loc, "invalid operands to compound assignment");
			return cc_node_error_new(c, loc);
		}
		n = cc_node_new_operands(c, AST_NODE_ASSIGN, lt, loc, l, r);
		AST_NODE(n)->aux = (uint16_t)op;
		AST_NODE(n)->c = pt;	/* computation type */
		return n;
	}
}

/* the type of a ?: with these (decayed) arms */
CC_PRIVATE uint32_t cc_conditional_build(cc *c, uint32_t cond, uint32_t a, uint32_t b, uint32_t loc)
{
	uint32_t ra, rb, n;
	ast_qualified_type at, bt, t;

	ra = a ? cc_node_rvalue(c, a) : cc_node_rvalue(c, cond);
	rb = cc_node_rvalue(c, b);
	at = AST_NODE(ra)->ty, bt = AST_NODE(rb)->ty;
	if (cc_type_is_bool_vector(c, AST_NODE(cond)->ty)) {
		ast_qualified_type vt = AST_NODE(cond)->ty;
		if ((!cc_type_is_bool_vector(c, at) && !cc_type_is_bool_vector(c, bt)) ||
		    (cc_type_is_vector(c, at) && !cc_type_is_compatible(c, at, vt)) ||
		    (cc_type_is_vector(c, bt) && !cc_type_is_compatible(c, bt, vt))) {
			cc_report_error(c, loc, "incompatible vector condition and result types");
			return cc_node_error_new(c, loc);
		}
		uint32_t pre = 0;
		cond = cc_lowering_once(c, cc_node_rvalue(c, cond), &pre);
		uint32_t neg = cc_node_new(c, AST_NODE_UNARY, vt, loc);
		AST_NODE(neg)->aux = LEX_TOKEN_TILDE, AST_NODE(neg)->a = cond;
		uint32_t result = cc_binary_build(c, LEX_TOKEN_PIPE, cc_binary_build(c, LEX_TOKEN_AMPERSAND, cond, cc_conversion_apply(c, ra, vt), loc),
		                        cc_binary_build(c, LEX_TOKEN_AMPERSAND, neg, cc_conversion_apply(c, rb, vt), loc), loc);
		return cc_lowering_sequence(c, pre, result);
	}
	if ((cc_type_is_complex(c, at) || cc_type_is_complex(c, bt)) && (cc_type_is_arithmetic(c, at) || cc_type_is_complex(c, at)) &&
	    (cc_type_is_arithmetic(c, bt) || cc_type_is_complex(c, bt))) {
		t = cc_type_complex(c, cc_type_arithmetic_conversion(c, cc_type_is_complex(c, at) ? cc_complex_element_type(c, at) : at, cc_type_is_complex(c, bt) ? cc_complex_element_type(c, bt) : bt));
		ra = cc_conversion_apply(c, ra, t), rb = cc_conversion_apply(c, rb, t);
	} else if (cc_type_is_arithmetic(c, at) && cc_type_is_arithmetic(c, bt)) {
		t = cc_type_arithmetic_conversion(c, at, bt);
		ra = cc_conversion_apply(c, ra, t), rb = cc_conversion_apply(c, rb, t);
	} else if (cc_type_is_void(c, at) || cc_type_is_void(c, bt)) {
		t = cc_type_basic(AST_TYPE_VOID);
	} else if (cc_type_is_pointer(c, at) && cc_type_is_pointer(c, bt)) {
		ast_qualified_type pa = AST_TYPE(c, at)->base, pb = AST_TYPE(c, bt)->base;
		int q = AST_QUALIFIED_TYPE_QUALIFIERS(pa) | AST_QUALIFIED_TYPE_QUALIFIERS(pb);

		if (cc_expression_is_null_pointer_constant(c, rb) && !cc_expression_is_null_pointer_constant(c, ra))
			t = at;
		else if (cc_expression_is_null_pointer_constant(c, ra) && !cc_expression_is_null_pointer_constant(c, rb))
			t = bt;
		else if (cc_type_is_void(c, pa) || cc_type_is_void(c, pb))
			t = cc_type_pointer(c, AST_QUALIFIED_TYPE(AST_TYPE_VOID, q));
		else if (cc_type_is_compatible(c, pa, pb))
			t = cc_type_pointer(c, AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(cc_type_composite(c, pa, pb)), q));
		else
			t = cc_type_pointer(c, AST_QUALIFIED_TYPE(AST_TYPE_VOID, q));	/* clang: warning, void * */
		ra = cc_conversion_apply(c, ra, t), rb = cc_conversion_apply(c, rb, t);
	} else if ((cc_type_is_pointer(c, at) && cc_type_is_integer(c, bt)) || (cc_type_is_pointer(c, bt) && cc_type_is_integer(c, at))) {
		bool pa = cc_type_is_pointer(c, at);

		if (!cc_expression_is_null_pointer_constant(c, pa ? rb : ra)) {	/* (clang: not for a null pointer constant) */
			diag_span r[2] = { cc_node_range(c, ra), cc_node_range(c, rb) };
			char an[256];

			snprintf(an, sizeof an, "%s", tyname(c, at));
			cc_report_warning(c, "conditional-type-mismatch", true, NULL, loc, r, 2, NULL, 0,
				 "pointer/integer type mismatch in conditional expression ('%s' and '%s')", an, tyname(c, bt));
		}
		t = pa ? at : bt;
		if (pa)
			rb = cc_conversion_apply(c, rb, t);
		else
			ra = cc_conversion_apply(c, ra, t);
	} else if ((cc_type_is_record(c, at) || cc_type_is_vector(c, at) || AST_TYPE_KIND(c, at) == AST_TYPE_SVE_BUILTIN) && cc_type_is_compatible(c, at, bt)) {
		t = AST_QUALIFIED_TYPE_UNQUALIFIED(at);
	} else {
		cc_report_error(c, loc, "incompatible operand types in conditional expression");
		return cc_node_error_new(c, loc);
	}
	n = cc_node_new(c, AST_NODE_CONDITIONAL, t, loc);
	AST_NODE(n)->a = cc_conversion_decay(c, cond);
	AST_NODE(n)->b = a ? ra : 0;
	AST_NODE(n)->c = rb;
	if (!a)
		AST_NODE(n)->a = ra;	/* GNU ?: evaluates the condition once, as the value */
	return n;
}

/* ---------------- expressions: members ---------------- */

CC_PRIVATE int cc_member_find(cc *c, uint32_t ti, uint32_t name, uint32_t *path, int depth)
{
	const tag *r = &c->tags[ti];

	if (depth >= CC_MEMBER_FIND_DEPTH_MAX)
		return -1;
	for (uint32_t i = 0; i < r->nfield; i++) {
		const field *f = &r->f[i];

		if (f->name == name) {
			path[depth] = i;
			return depth + 1;
		}
		if (!f->name && !f->bitfield && cc_type_is_record(c, f->ty)) {
			int d = cc_member_find(c, AST_TYPE(c, f->ty)->aux, name, path, depth + 1);

			if (d > 0) {
				path[depth] = i;
				return d;
			}
		}
	}
	return -1;
}

static __attribute__((noinline)) uint32_t cc_member_build_slow(cc *c, uint32_t base, uint32_t name, uint32_t loc)
{
	ast_qualified_type bt = AST_NODE(base)->ty;
	uint32_t path[CC_MEMBER_FIND_DEPTH_MAX], ti;
	int d;

	if (!cc_type_is_record(c, bt)) {
		diag_span r[2] = { cc_node_range(c, base), { loc, pp_token_end_find(c->p, c->prevloc) } };

		report(c, "error", loc, r, 2, NULL, 0, "member reference base type '%s' is not a structure or union", tyname(c, bt));
		return cc_node_error_new(c, loc);
	}
	ti = AST_TYPE(c, bt)->aux;
	if (!(c->tags[ti].flags & AST_RECORD_FLAG_COMPLETE)) {
		if (AST_NODE(base)->op == AST_NODE_VARIABLE && (c->syms[AST_NODE(base)->a].flags & AST_SYMBOL_FLAG_INVALID))
			return cc_node_error_new(c, loc);	/* (already reported) */
		{
			diag_span r = cc_node_range(c, base);

			report(c, "error", loc, &r, 1, NULL, 0, "incomplete definition of type '%s'", tyname(c, bt));
		}
		cc_report_forward_note(c, bt);
		return cc_node_error_new(c, loc);
	}
	d = cc_member_find(c, ti, name, path, 0);
	if (d < 0) {
		diag_span r = cc_node_range(c, base);

		report(c, "error", c->prevloc, &r, 1, NULL, 0, "no member named '%.*s' in '%s'", CC_IDENTIFIER(name), tyname(c, bt));
		return cc_node_error_new(c, loc);
	}
	for (int i = 0; i < d; i++) {
		const field *f = &c->tags[ti].f[path[i]];
		uint32_t m = cc_node_new(c, AST_NODE_MEMBER, cc_type_qualify(c, f->ty, AST_QUALIFIED_TYPE_QUALIFIERS(bt)), loc);

		if (unlikely(f->invalid))
			return cc_node_error_new(c, loc);

		AST_NODE(m)->a = base;
		AST_NODE(m)->b = ti;
		AST_NODE(m)->c = path[i];
		AST_NODE(m)->flags = (uint8_t)((AST_NODE(base)->flags & AST_NODE_FLAG_LVALUE) | (f->bitfield ? AST_NODE_FLAG_BITFIELD : 0));
		base = m;
		bt = AST_NODE(m)->ty;
		if (unlikely(f->depr) && i == d - 1)
			cc_deprecated_use_report(c, c->prevloc, f->name, &c->attr[f->depr], f->loc);
		if (cc_type_is_record(c, bt))
			ti = AST_TYPE(c, bt)->aux;
	}
	return base;
}

/* base.name: a direct member found before any anonymous one is the common case */
CC_PRIVATE uint32_t member(cc *c, uint32_t base, uint32_t name, uint32_t loc)
{
	ast_qualified_type bt = AST_NODE(base)->ty;
	const ctype *t = AST_TYPE(c, bt);

	if (likely(t->kind == AST_TYPE_STRUCT || t->kind == AST_TYPE_UNION) && likely(c->tags[t->aux].flags & AST_RECORD_FLAG_COMPLETE)) {
		const tag *r = &c->tags[t->aux];

		for (uint32_t i = 0; i < r->nfield; i++) {
			const field *f = &r->f[i];

			if (f->name == name) {
				uint32_t m;

				if (unlikely(f->invalid))
					return cc_node_error_new(c, loc);
				if (unlikely(f->depr))
					cc_deprecated_use_report(c, c->prevloc, f->name, &c->attr[f->depr], f->loc);
				m = cc_node_new(c, AST_NODE_MEMBER, cc_type_qualify(c, f->ty, AST_QUALIFIED_TYPE_QUALIFIERS(bt)), loc);

				AST_NODE(m)->a = base;
				AST_NODE(m)->b = t->aux;
				AST_NODE(m)->c = i;
				AST_NODE(m)->flags = (uint8_t)((AST_NODE(base)->flags & AST_NODE_FLAG_LVALUE) | (f->bitfield ? AST_NODE_FLAG_BITFIELD : 0));
				return m;
			}
			if (!f->name && !f->bitfield && cc_type_is_record(c, f->ty))
				break;	/* the name may be in an anonymous member: full search */
		}
	}
	return cc_member_build_slow(c, base, name, loc);
}

CC_PRIVATE uint32_t cc_dereference_build(cc *c, uint32_t p, uint32_t loc)
{
	uint32_t n;
	ast_qualified_type t;

	p = cc_node_rvalue(c, p);
	if (!cc_type_is_pointer(c, AST_NODE(p)->ty)) {
		diag_span r = cc_node_range(c, p);

		report(c, "error", loc, &r, 1, NULL, 0, "indirection requires pointer operand ('%s' invalid)", tyname(c, AST_NODE(p)->ty));
		return cc_node_error_new(c, loc);
	}
	t = AST_TYPE(c, AST_NODE(p)->ty)->base;
	n = cc_node_new(c, AST_NODE_DEREFERENCE, t, loc);
	AST_NODE(n)->a = p;
	AST_NODE(n)->flags = AST_TYPE_KIND(c, t) == AST_TYPE_FUNCTION ? 0 : AST_NODE_FLAG_LVALUE;
	return n;
}

static void cc_symbol_mark_address_taken(cc *c, uint32_t n)
{
	while (AST_NODE(n)->op == AST_NODE_MEMBER || (AST_NODE(n)->op == AST_NODE_INDEX && 0))
		n = AST_NODE(n)->a;
	if (AST_NODE(n)->op == AST_NODE_VARIABLE)
		c->syms[AST_NODE(n)->a].flags |= AST_SYMBOL_FLAG_ADDRESS_TAKEN;
}

CC_PRIVATE uint32_t cc_address_of_build(cc *c, uint32_t n, uint32_t loc)
{
	uint32_t r;
	ast_qualified_type t = AST_NODE(n)->ty;

	if (AST_NODE(n)->op == AST_NODE_INDEX && AST_NODE(n)->aux == 1)
		cc_report_error(c, loc, "address of vector element requested");
	if (cc_node_bitfield(c, n))
		cc_report_error(c, loc, "address of bit-field requested");
	if (!(AST_NODE(n)->flags & AST_NODE_FLAG_LVALUE) && AST_TYPE_KIND(c, t) != AST_TYPE_FUNCTION && AST_NODE(n)->op != AST_NODE_COMPOUND_LITERAL) {
		diag_span sp = cc_node_range(c, n);

		report(c, "error", loc, &sp, 1, NULL, 0, "cannot take the address of an rvalue of type '%s'", tyname(c, t));
	}
	{
		uint32_t b = n;

		while (AST_NODE(b)->op == AST_NODE_MEMBER)
			b = AST_NODE(b)->a;
		if (AST_NODE(b)->op == AST_NODE_VARIABLE && c->syms[AST_NODE(b)->a].sc == AST_STORAGE_REGISTER && c->syms[AST_NODE(b)->a].kind == AST_SYMBOL_VARIABLE) {
			diag_span sp = cc_node_range(c, n);

			report(c, "error", loc, &sp, 1, NULL, 0, "address of register variable requested");
		}
	}
	cc_symbol_mark_address_taken(c, n);
	r = cc_node_new(c, AST_NODE_ADDRESS, cc_type_pointer(c, t), loc);
	AST_NODE(r)->a = n;
	return r;
}

/* ---------------- expressions: primary and postfix ---------------- */

static uint32_t cc_generic_selection_parse(cc *c, uint32_t loc)
{
	uint32_t ctl, chosen = 0, def = 0;
	ast_qualified_type ct;
	uint32_t open = cc_token_expect_left_paren(c);

	c->unevaluated++;
	ctl = cc_conversion_decay(c, cc_assignment_parse(c));
	c->unevaluated--;
	ct = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE(ctl)->ty);	/* lvalue conversion: no qualifiers, no promotion */
	while (cc_token_accept(c, LEX_TOKEN_COMMA)) {
		uint32_t e;

		if (cc_token_is_keyword(c, KW_default)) {
			next(c);
			cc_token_expect(c, LEX_TOKEN_COLON, "':'");
			e = cc_assignment_parse(c);
			def = e;
		} else {
			ast_qualified_type t = cc_type_name_parse(c);

			cc_token_expect(c, LEX_TOKEN_COLON, "':'");
			e = cc_assignment_parse(c);
			if (!chosen && cc_type_is_compatible(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t), ct) && AST_QUALIFIED_TYPE_QUALIFIERS(t) == 0)
				chosen = e;
		}
	}
	cc_token_expect_match(c, LEX_TOKEN_RIGHT_PAREN, open);
	if (!chosen)
		chosen = def;
	if (!chosen) {
		diag_span r = cc_node_range(c, ctl);

		if (!AST_NODE_IS_ERROR(ctl))	/* (clang: nothing more for an invalid operand; the result is invalid) */
			report(c, "error", AST_NODE(ctl)->loc, &r, 1, NULL, 0,
			       "controlling expression type '%s' not compatible with any generic association type", tyname(c, ct));
		return cc_node_error_new(c, loc);
	}
	return chosen;
}

static uint32_t cc_function_name_build(cc *c, uint32_t loc)
{
	const sym *f = c->fn ? &c->syms[c->fn] : NULL;
	int len = 0;
	const char *s = f ? cc_identifier_name(f->name, &len) : "";
	char *buf = cc_heap_allocate(c, (size_t)len + 1);
	uint32_t si = c->nstr++, n;

	memcpy(buf, s, (size_t)len);
	buf[len] = 0;
	c->strs[si] = (cstr){ buf, (uint32_t)len + 1, 1, LEX_TOKEN_STRING };
	n = cc_node_new(c, AST_NODE_STRING, cc_type_array(c, AST_QUALIFIED_TYPE(AST_TYPE_CHAR, AST_QUALIFIER_CONST), (uint64_t)len + 1, 0), loc);
	AST_NODE(n)->a = si;
	AST_NODE(n)->flags = AST_NODE_FLAG_LVALUE;
	return n;
}

static __attribute__((noinline, cold, noreturn)) void cc_primary_invalid_report(cc *c, const ptok *t)
{
	cc_report_fatal(c, t->loc, "expected expression");
	abort();
}

/* a name bound to s (already consumed) */
static inline __attribute__((always_inline)) uint32_t cc_primary_symbol_build(cc *c, uint32_t s, uint32_t id, uint32_t loc)
{
	sym *y = &c->syms[s];

	if (unlikely(y->flags & AST_SYMBOL_FLAG_DEPRECATED) && !(y->flags & AST_SYMBOL_FLAG_OVERLOAD)) {	/* an overload set: the member a call picks */
		cc_deprecated_use_report(c, loc, id, &c->attr[y->attrs], y->loc);
		y = &c->syms[s];
	}
	if (y->kind == AST_SYMBOL_ENUMERATOR) {
		uint32_t n = cc_node_integer_new(c, cc_constant_truncate(c, y->val, y->ty), y->ty, loc);

		if (AST_NODE(n)->op == AST_NODE_NUMBER)
			AST_NODE(n)->aux |= AST_NUMBER_ENUMERATOR;
		return n;
	}
	if (unlikely(y->kind == AST_SYMBOL_TYPEDEF))
		cc_report_fatal(c, loc, "unexpected type name '%.*s': expected expression", CC_IDENTIFIER(id));
	{
		uint32_t n = cc_node_new(c, AST_NODE_VARIABLE, y->ty, loc);

		AST_NODE(n)->a = s;
		AST_NODE(n)->flags = y->kind == AST_SYMBOL_VARIABLE ? AST_NODE_FLAG_LVALUE : 0;
		if (unlikely(y->flags & AST_SYMBOL_FLAG_INVALID))
			AST_NODE(n)->flags |= AST_NODE_FLAG_ERROR;
		/* (clang: a function's references from its own body do not use it) */
		if ((!c->unevaluated || c->bcp) && !((y->flags & AST_SYMBOL_FLAG_OVERLOAD) && cc_token_is(c, LEX_TOKEN_LEFT_PAREN)) && s != c->fn)
			y->flags |= AST_SYMBOL_FLAG_USED;
		if (unlikely(y->flags & AST_SYMBOL_FLAG_UNINITIALIZED_PENDING)) {
			if (tok(c)->kind == LEX_TOKEN_PERIOD)
				y->flags &= ~AST_SYMBOL_FLAG_UNINITIALIZED_PENDING;	/* s.m: initializes it for the analysis */
			else if (tok(c)->kind != LEX_TOKEN_EQUAL) {
				y->flags &= ~AST_SYMBOL_FLAG_UNINITIALIZED_PENDING;
				c->uv_any |= 4;	/* read (or &) before any assignment: run the analysis */
			} else if (!cc_uninitialized_is_assigned_deeper(c, s))
				c->uv_asg = s;	/* cleared after the right side */	/* (an assignment in a nested branch keeps it: -Wsometimes-uninitialized) */
		}
		y->flags |= AST_SYMBOL_FLAG_REFERENCED;
		y->nref += y->nref != 0xffff;
		if (unlikely(y->next) && y->depth)
			c->syms[y->next].flags |= AST_SYMBOL_FLAG_REFERENCED;	/* a block-scope extern: the global */
		return n;
	}
}

/* keywords that start a primary expression (the current token) */
static __attribute__((noinline)) uint32_t cc_primary_keyword_parse(cc *c, int kw, uint32_t loc)
{
	if (kw == KW___builtin_bit_cast) {
		next(c);
		return cc_bit_cast_build(c, loc);
	}
	if (kw == KW__Generic) {
		next(c);
		return cc_generic_selection_parse(c, loc);
	}
	if (kw == KW___builtin_va_arg) {
		static const cc_builtin va = { "__builtin_va_arg", CC_BUILTIN_VA_ARG, 0, "v" };

		next(c);
		return cc_builtin_call_build(c, &va, loc);
	}
	if (kw == KW___builtin_offsetof) {
		next(c);
		return cc_builtin_offsetof_build(c, loc);
	}
	if (kw == KW___builtin_types_compatible_p) {
		static const cc_builtin tc = { "", CC_BUILTIN_TYPES_COMPATIBLE, 1, "i" };

		next(c);
		return cc_builtin_call_build(c, &tc, loc);
	}
	if (kw == KW_true || kw == KW_false) {	/* C23: constants of type bool */
		next(c);
		return cc_node_integer_new(c, kw == KW_true, cc_type_basic(AST_TYPE_BOOL), loc);
	}
	if (kw == KW_nullptr) {	/* C23: a null pointer constant (nullptr_t is void * here) */
		next(c);
		return cc_cast_insert(c, cc_node_integer_new(c, 0, cc_type_basic(AST_TYPE_INT), loc), cc_type_pointer(c, cc_type_basic(AST_TYPE_VOID)), AST_CAST_NULL_POINTER);
	}
	if (kw == KW___builtin_shufflevector || kw == KW___builtin_convertvector) {
		next(c);
		return cc_builtin_vector_build(c, kw == KW___builtin_shufflevector, loc);
	}
	if (kw == KW___builtin_choose_expr) {
		static const cc_builtin ch = { "", CC_BUILTIN_CHOOSE_EXPR, 1, "i" };

		next(c);
		return cc_builtin_call_build(c, &ch, loc);
	}
	cc_primary_invalid_report(c, tok(c));
}

/* C99: a call to an undeclared function; for one a standard header declares (POSIX's write,
   open, ...) vcc adds where it is, as help with the #include fix-it */
static __attribute__((cold, noinline)) void cc_primary_implicit_call_report(cc *c, uint32_t id, uint32_t loc)
{
	const char *h = cc_library_header(c, id);
	diag_record *d;

	diag_report_site_pc = __builtin_return_address(0);
	cc_report_error_limit(c), c->errors++, c->ndiag++;
	d = diag_record_begin(c->p, DIAG_LEVEL_ERROR, loc, "call to undeclared function '%.*s'; ISO C99 and later do not support "
			  "implicit function declarations", CC_IDENTIFIER(id));
	diag_record_flag_set(d, "implicit-function-declaration");
	cc_typo_c23_word_help(c, d, id);
	if (h) {
		diag_record_help_set(c->p, d, "'%.*s' is declared in <%s>", CC_IDENTIFIER(id), h);
		cc_library_include_fixit(c, d, loc, h);
	}
	diag_record_emit(c->p, d);
}

/* an identifier without a binding (already consumed): builtins, __func__, errors */
static __attribute__((noinline)) uint32_t cc_primary_unbound_parse(cc *c, uint32_t id, uint32_t loc)
{
	{
		const cc_builtin *b = cc_builtin_lookup(id);

		if (b && cc_token_is(c, LEX_TOKEN_LEFT_PAREN))
			return cc_builtin_call_build(c, b, loc);
	}
	if (id == c->id_func || id == c->id_FUNCTION || id == c->id_PRETTY)
		return cc_function_name_build(c, loc);
	{
		uint32_t s = cc_token_is(c, LEX_TOKEN_LEFT_PAREN) ? cc_library_builtin_declare(c, id, loc) : 0;

		if (s)
			return cc_primary_symbol_build(c, s, id, loc);
	}
	if (cc_token_is(c, LEX_TOKEN_LEFT_PAREN)) {
		uint32_t ls = cc_library_function_declare(c, id, loc);
		ast_qualified_type lt;

		if (ls) {
			uint32_t n = cc_node_new(c, AST_NODE_VARIABLE, c->syms[ls].ty, loc);

			AST_NODE(n)->a = ls;
			c->syms[ls].flags |= AST_SYMBOL_FLAG_USED;
			return n;
		}
		lt = cc_library_implicit_type(c, id);
		if (!c->o->implicit_fn && lt)
			cc_report_error(c, loc, "call to undeclared library function '%.*s' with type '%s'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]", CC_IDENTIFIER(id), tyname(c, lt));
		else if (!c->o->implicit_fn)
			cc_primary_implicit_call_report(c, id, loc);
		if (cc_identifier_length(id) == 6 && !memcmp(cc_identifier_string(id), "alloca", 6)) {
			/* clang: an implicitly declared library builtin is the builtin */
			uint32_t bid = vintern_find("__builtin_alloca", 16, vintern_hash("__builtin_alloca", 16));
			const cc_builtin *b = bid ? cc_builtin_lookup(bid) : NULL;

			if (b)
				return cc_builtin_call_build(c, b, loc);
		}
		{
			/* clang: a library function gets its library type (asin(x) is a double) */
			uint32_t fs = lt ? cc_library_implicit_declare(c, id, loc, lt) : cc_implicit_function_declare(c, id, loc);
			uint32_t n = cc_node_new(c, AST_NODE_VARIABLE, c->syms[fs].ty, loc);

			AST_NODE(n)->a = fs;
			c->syms[fs].flags |= AST_SYMBOL_FLAG_USED;
			return n;
		}
	}
	{
		uint32_t s = cc_undeclared_report(c, "use of undeclared identifier", id, loc, false, 0, NULL);

		if (s)
			return cc_primary_symbol_build(c, s, id, loc);	/* clang recovers with the correction */
	}
	{
		uint32_t n = cc_node_integer_new(c, 0, cc_type_basic(AST_TYPE_INT), loc);

		AST_NODE(n)->flags |= AST_NODE_FLAG_ERROR | AST_NODE_FLAG_LVALUE;
		return n;
	}
}

static __attribute__((noinline)) uint32_t cc_statement_expression_parse(cc *c, uint32_t loc)
{
	ast_qualified_type last = cc_type_basic(AST_TYPE_VOID);
	uint32_t b, n;

	if (!c->fn)
		cc_report_error(c, loc, "statement expression not allowed at file scope");
	c->in_stmtexpr++;
	bool fh = c->for_head;

	c->for_head = false;
	b = cc_compound_statement_parse(c, &last);
	c->for_head = fh;
	c->in_stmtexpr--;
	cc_token_expect_match(c, LEX_TOKEN_RIGHT_PAREN, loc);
	n = cc_node_new(c, AST_NODE_STATEMENT_EXPRESSION, last, loc);
	AST_NODE(n)->a = b;
	if (!c->in_stmtexpr)
		cc_unused_result_check(c, n, false);	/* the statements before the value */
	return n;
}

/* ( expression ) or a GNU statement expression; '(' (at loc) is consumed */
CC_PRIVATE uint32_t cc_parenthesized_parse(cc *c, uint32_t loc)
{
	uint32_t n;

	if (cc_token_is(c, LEX_TOKEN_LEFT_BRACE)) {
		static const char *const g[] = { "gnu-statement-expression", "gnu", "pedantic", NULL };
		bool mac = tok(c)->flags & PP_TOKEN_FLAG_FROM_MACRO;

		if (!(c->prevflags & PP_TOKEN_FLAG_FROM_MACRO))
			CC_REPORT_EXTENSION(c, "gnu-statement-expression", cc_warning_group_gnu, tok(c)->loc, "use of GNU statement expression extension");
		else if (unlikely(cc_extension_is_enabled(c, "gnu-statement-expression-from-macro-expansion", g))) {
			c->dms = "{";	/* (clang: the '(' from a macro; the caret at the invocation) */
			cc_report_extension(c, "gnu-statement-expression-from-macro-expansion", g, tok(c)->loc,
				 "use of GNU statement expression extension from macro expansion");
		}
		n = cc_statement_expression_parse(c, loc);
		if (mac)
			AST_NODE(n)->flags |= AST_NODE_FLAG_FROM_MACRO;	/* (no -Wunused-value) */
		return n;
	}
	n = expr(c);
	if (likely(cc_token_is(c, LEX_TOKEN_RIGHT_PAREN)) && !AST_NODE_IS_ERROR(n)) {
		/* the parentheses around it, as distances (no location fix-up in the caches) */
		uint32_t b = c->span[n].e ? c->span[n].b : AST_NODE(n)->loc, e = c->span[n].e ? c->span[n].e : AST_NODE(n)->loc, rp = tok(c)->loc;

		if (b >= loc && b - loc < 65536 && rp >= e && rp - e < 65536)
			c->paren[n] = (struct cc_node_parentheses){ (uint16_t)(b - loc), (uint16_t)(rp - e) };
	}
	cc_token_expect_close(c, LEX_TOKEN_RIGHT_PAREN, loc, n);
	AST_NODE(n)->flags |= AST_NODE_FLAG_PARENTHESIZED;
	return n;
}
static uint32_t cc_primary_parse(cc *c)
{
	ptok *t = tok(c);
	uint32_t loc = t->loc;

	switch (t->kind) {
	case LEX_TOKEN_NUMBER: {
		uint32_t n = cc_literal_number_parse(c, t);

		next(c);
		return n;
	}
	case LEX_TOKEN_CHARACTER: case LEX_TOKEN_WIDE_CHARACTER: case LEX_TOKEN_UTF8_CHARACTER: case LEX_TOKEN_UTF16_CHARACTER: case LEX_TOKEN_UTF32_CHARACTER:
		return cc_literal_character_build(c);
	case LEX_TOKEN_STRING: case LEX_TOKEN_WIDE_STRING: case LEX_TOKEN_UTF8_STRING: case LEX_TOKEN_UTF16_STRING: case LEX_TOKEN_UTF32_STRING:
		return cc_literal_string_parse(c);
	case LEX_TOKEN_LEFT_PAREN:
		next(c);
		return cc_parenthesized_parse(c, loc);
	case LEX_TOKEN_IDENTIFIER: {
		uint32_t id = t->id, s;
		int kw = cc_token_keyword(t);

		if (unlikely(kw))
			return cc_primary_keyword_parse(c, kw, loc);
		if (unlikely(c->o->std == 23 && cc_identifier_length(id) == 2 && !memcmp(cc_identifier_string(id), "u8", 2))) {
			/* C23 u8'a': the lexer gives u8 and 'a' (C17 tokens) */
			ptok *q = cc_token_peek(c, 1);

			if (q->kind == LEX_TOKEN_CHARACTER && q->loc == loc + 2) {
				next(c);
				tok(c)->kind = LEX_TOKEN_UTF8_CHARACTER, tok(c)->loc = loc;
				return cc_literal_character_build(c);
			}
		}
		s = cc_symbol_lookup(c, id);
		next(c);
		if (likely(s))
			return cc_primary_symbol_build(c, s, id, loc);
		return cc_primary_unbound_parse(c, id, loc);
	}
	default:
		cc_primary_invalid_report(c, t);
	}
}

CC_PRIVATE uint32_t cc_primary_postfix_parse(cc *c, uint32_t begin)
{
	uint32_t n = cc_primary_parse(c);

	cc_node_span_set(c, n, begin);
	if (cc_token_starts_postfix(tok(c)->kind)) {
		n = cc_postfix_operators_parse(c, n);
		cc_node_span_set(c, n, begin);
	}
	return n;
}

/* overloadable: how well argument n converts to parameter type p (clang's C ranks:
   0 exact, 1 qualification, 2 promotion, 3 conversion, 4 incompatible pointer, 5 ellipsis;
   -1 not viable) */
static int cc_overload_rank(cc *c, uint32_t n, ast_qualified_type p)
{
	ast_qualified_type a = AST_QUALIFIED_TYPE_UNQUALIFIED(canon(c, AST_NODE(n)->ty));
	int ak, pk;

	p = AST_QUALIFIED_TYPE_UNQUALIFIED(canon(c, p));
	if (cc_type_is_array(c, a))
		a = cc_type_pointer(c, AST_TYPE(c, a)->base);
	else if (cc_type_is_function(c, a))
		a = cc_type_pointer(c, a);
	if (cc_type_is_compatible(c, a, p))
		return 0;
	ak = AST_TYPE_KIND(c, a), pk = AST_TYPE_KIND(c, p);
	if (ak == AST_TYPE_POINTER && pk == AST_TYPE_POINTER) {
		ast_qualified_type ea = AST_TYPE(c, a)->base, ep = AST_TYPE(c, p)->base;

		if (cc_type_is_compatible(c, AST_QUALIFIED_TYPE_UNQUALIFIED(ea), AST_QUALIFIED_TYPE_UNQUALIFIED(ep)))
			return (AST_QUALIFIED_TYPE_QUALIFIERS(ep) & AST_QUALIFIED_TYPE_QUALIFIERS(ea)) == AST_QUALIFIED_TYPE_QUALIFIERS(ea) ? 1 : 4;
		if ((cc_type_is_void(c, ea) || cc_type_is_void(c, ep)) && (AST_QUALIFIED_TYPE_QUALIFIERS(ep) & AST_QUALIFIED_TYPE_QUALIFIERS(ea)) == AST_QUALIFIED_TYPE_QUALIFIERS(ea))
			return 3;
		return 4;
	}
	if (cc_type_is_arithmetic(c, a) && cc_type_is_arithmetic(c, p)) {
		if (AST_QUALIFIED_TYPE_UNQUALIFIED(cc_type_promote(c, a)) == p || (ak == AST_TYPE_FLOAT && pk == AST_TYPE_DOUBLE))
			return 2;
		return 3;
	}
	if (pk == AST_TYPE_POINTER && cc_expression_is_null_pointer_constant(c, n))
		return 3;
	if (pk == AST_TYPE_BOOL && ak == AST_TYPE_POINTER)
		return 3;
	return -1;
}

/* the member of fn's overload set that a call with args selects */
CC_PRIVATE __attribute__((noinline)) uint32_t cc_overload_pick(cc *c, uint32_t head, const cc_id_list *args, uint32_t loc)
{
	uint32_t best = 0, nbest = 0;
	int br[64];

	for (uint32_t s = head; s; s = c->syms[s].ovl) {
		const ctype *f = AST_TYPE(c, c->syms[s].ty);
		int r[64];
		bool ok = args->n <= 64 && (args->n == f->n || (args->n > f->n && (f->flags & AST_TYPE_FLAG_VARIADIC)));
		bool better = false, worse = false;

		for (uint32_t i = 0; ok && i < args->n; i++) {
			r[i] = i < f->n ? cc_overload_rank(c, args->v[i], c->params[f->aux + i]) : 5;
			ok = r[i] >= 0;
		}
		if (!ok)
			continue;
		if (!best) {
			best = s, nbest = 1;
			memcpy(br, r, args->n * sizeof(int));
			continue;
		}
		for (uint32_t i = 0; i < args->n; i++)
			better |= r[i] < br[i], worse |= r[i] > br[i];
		if (better && !worse) {
			best = s, nbest = 1;
			memcpy(br, r, args->n * sizeof(int));
		} else if (better == worse) {
			nbest++;
		}
	}
	if (!best) {
		cc_report_error(c, loc, "no matching function for call to '%.*s'", CC_IDENTIFIER(c->syms[head].name));
		return head;
	}
	if (nbest > 1)
		cc_report_error(c, loc, "call to '%.*s' is ambiguous", CC_IDENTIFIER(c->syms[head].name));
	return best;
}

/* the postfix operators after an error node: parsed, nothing more said */
static __attribute__((noinline, cold)) uint32_t cc_postfix_error_parse(cc *c, uint32_t n)
{
	for (;;) {
		switch (tok(c)->kind) {
		case LEX_TOKEN_LEFT_SQUARE:
			next(c);
			expr(c);
			cc_token_expect(c, LEX_TOKEN_RIGHT_SQUARE, "']'");
			break;
		case LEX_TOKEN_LEFT_PAREN: {
			cc_id_list args;

			cc_builtin_arguments_parse(c, &args);
			break;
		}
		case LEX_TOKEN_PERIOD: case LEX_TOKEN_ARROW:
			next(c);
			if (!cc_token_is(c, LEX_TOKEN_IDENTIFIER))
				cc_report_fatal(c, tok(c)->loc, "expected identifier");
			next(c);
			break;
		case LEX_TOKEN_PLUS_PLUS: case LEX_TOKEN_MINUS_MINUS:
			next(c);
			break;
		default:	/* an error node of no type a conversion would look at (decay drops the flag) */
			return cc_node_error_new(c, AST_NODE(n)->loc);
		}
	}
}

CC_PRIVATE __attribute__((noinline)) uint32_t cc_postfix_operators_parse(cc *c, uint32_t n)
{
	uint32_t b0 = c->span[n].e ? c->span[n].b : AST_NODE(n)->loc;

	for (;; cc_node_span_set(c, n, b0)) {
		uint32_t loc = tok(c)->loc;

		if (unlikely(AST_NODE_IS_ERROR(n)))
			return cc_postfix_error_parse(c, n);
		switch (tok(c)->kind) {
		case LEX_TOKEN_LEFT_SQUARE: {
			uint32_t ix, r;
			ast_qualified_type at, it;
			ast_qualified_type vector_type = cc_type_is_vector(c, AST_NODE(n)->ty) ? AST_NODE(n)->ty : 0;

			next(c);
			ix = expr(c);
			cc_token_expect(c, LEX_TOKEN_RIGHT_SQUARE, "']'");
			if (unlikely(AST_NODE_IS_ERROR(ix))) {
				n = cc_node_error_new(c, loc);
				continue;
			}
			if (cc_type_is_vector(c, AST_NODE(n)->ty) && (AST_NODE(n)->flags & AST_NODE_FLAG_LVALUE)) {
				/* v[i]: lane i of the vector object */
				ast_qualified_type et = AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(AST_TYPE(c, AST_NODE(n)->ty)->base), AST_QUALIFIED_TYPE_QUALIFIERS(AST_NODE(n)->ty));

				n = cc_cast_insert(c, cc_address_of_build(c, n, loc), cc_type_pointer(c, et), AST_CAST_POINTER_TO_POINTER);
			} else if (cc_type_is_vector(c, AST_NODE(n)->ty)) {
				n = cc_cast_insert(c, cc_node_rvalue(c, n), cc_type_pointer(c, AST_TYPE(c, AST_NODE(n)->ty)->base), AST_CAST_VECTOR_ADDRESS);
			}
			n = cc_node_rvalue(c, n), ix = cc_node_rvalue(c, ix);
			at = AST_NODE(n)->ty, it = AST_NODE(ix)->ty;
			if (!cc_type_is_pointer(c, at) && cc_type_is_pointer(c, it)) {
				uint32_t t = n;

				n = ix, ix = t;
				at = AST_NODE(n)->ty, it = AST_NODE(ix)->ty;
			}
			if (!cc_type_is_pointer(c, at) || !cc_type_is_integer(c, it)) {
				diag_span r[2] = { cc_node_range(c, n), cc_node_range(c, ix) };

				if (!cc_type_is_pointer(c, at))
					report(c, "error", loc, r, 2, NULL, 0, "subscripted value is not an array, pointer, or vector");
				else
					report(c, "error", loc, r, 2, NULL, 0, "array subscript is not an integer");
				n = cc_node_error_new(c, loc);
				continue;
			}
			if (cc_type_is_function(c, AST_TYPE(c, at)->base)) {
				diag_span r = cc_node_range(c, n);

				report(c, "error", r.b, &r, 1, NULL, 0, "subscript of pointer to function type '%s'", tyname(c, AST_TYPE(c, at)->base));
				n = cc_node_error_new(c, loc);
				continue;
			}
			if (!cc_type_is_void(c, AST_TYPE(c, at)->base) && !cc_type_complete(c, AST_TYPE(c, at)->base)) {
				diag_span r = cc_node_range(c, n);

				report(c, "error", loc, &r, 1, NULL, 0, "subscript of pointer to incomplete type '%s'", tyname(c, AST_TYPE(c, at)->base));
				cc_report_forward_note(c, AST_TYPE(c, at)->base);
				n = cc_node_error_new(c, loc);
				continue;
			}
			if (AST_TYPE_KIND(c, AST_TYPE(c, at)->base) == AST_TYPE_SVE_BUILTIN)
				cc_pointer_void_arithmetic_check(c, at, loc, false);
			r = cc_node_new_operands(c, AST_NODE_INDEX, AST_TYPE(c, at)->base, loc, n, cc_conversion_apply(c, ix, c->t_ptrdiff));
			AST_NODE(r)->flags = AST_NODE_FLAG_LVALUE;
			AST_NODE(r)->c = vector_type;
			if (vector_type && cc_type_is_bool_vector(c, vector_type)) AST_NODE(r)->aux = 1;
			n = r;
			continue;
		}
		case LEX_TOKEN_LEFT_PAREN:
			n = call(c, n, loc);
			continue;
		case LEX_TOKEN_PERIOD:
			next(c);
			if (!cc_token_is(c, LEX_TOKEN_IDENTIFIER))
				cc_report_fatal(c, tok(c)->loc, "expected identifier");
			{
				uint32_t id = tok(c)->id, nloc = tok(c)->loc;

				if (unlikely(c->field_uses))
					cc_field_uses_name_keep(c);
				next(c);
				if (cc_type_is_vector(c, AST_NODE(n)->ty) && AST_TYPE(c, AST_NODE(n)->ty)->ext) {
					uint32_t b = c->span[n].e ? c->span[n].b : AST_NODE(n)->loc;

					n = cc_vector_swizzle_build(c, n, id, loc);
					(void)nloc;
					cc_node_span_set(c, n, b);
					continue;
				}
				if (cc_type_is_pointer(c, AST_NODE(n)->ty) && cc_type_is_record(c, AST_TYPE(c, AST_NODE(n)->ty)->base)) {
					/* clang: fix-it '->', then go on as if it were there */
					diag_span r = cc_node_range(c, n);
					diag_fixit fx = { loc, loc + 1, "->", DIAG_APPLICABILITY_MACHINE };
					diag_span b = c->span[n];

					c->dms = ".";
					report(c, "error", loc, &r, 1, &fx, 1, "member reference type '%s' is a pointer; did you mean to use '->'?",
					       tyname(c, AST_NODE(n)->ty));
					n = cc_dereference_build(c, n, loc);
					if (b.e && !c->span[n].e)
						c->span[n] = b;
				}
				n = member(c, n, id, loc);
				if (unlikely(c->field_uses))
					cc_field_uses_member(c, n, id);
			}
			continue;
		case LEX_TOKEN_ARROW:
			next(c);
			if (!cc_token_is(c, LEX_TOKEN_IDENTIFIER))
				cc_report_fatal(c, tok(c)->loc, "expected identifier");
			{
				uint32_t id = tok(c)->id;

				if (unlikely(c->field_uses))
					cc_field_uses_name_keep(c);
				next(c);
				if (cc_type_is_record(c, AST_NODE(n)->ty)) {
					/* clang: fix-it '.', then go on as if it were there */
					diag_span r[2] = { cc_node_range(c, n), { loc, loc + 2 } };
					diag_fixit fx = { loc, loc + 2, ".", DIAG_APPLICABILITY_MACHINE };

					c->dms = "->";
					report(c, "error", loc, r, 2, &fx, 1, "member reference type '%s' is not a pointer; did you mean to use '.'?",
					       tyname(c, AST_NODE(n)->ty));
					n = member(c, n, id, loc);
				} else {
					diag_span b = c->span[n];

					n = cc_dereference_build(c, n, loc);
					if (b.e && !c->span[n].e)
						c->span[n] = b;
					n = member(c, n, id, loc);
				}
				if (unlikely(c->field_uses))
					cc_field_uses_member(c, n, id);
			}
			continue;
		case LEX_TOKEN_PLUS_PLUS:
		case LEX_TOKEN_MINUS_MINUS: {
			int op = tok(c)->kind == LEX_TOKEN_PLUS_PLUS ? AST_NODE_POST_INCREMENT : AST_NODE_POST_DECREMENT;
			uint32_t r;

			next(c);
			cc_node_require_lvalue(c, n, loc);
			if (cc_type_is_complex(c, AST_NODE(n)->ty)) {
				n = cc_complex_increment(c, n, op, loc);
				continue;
			}
			if ((AST_TYPE(c, AST_NODE(n)->ty)->flags & AST_TYPE_FLAG_ATOMIC) && cc_atomic_wide_type_is_wide(c, AST_NODE(n)->ty)) {
				n = cc_atomic_wide_assignment_build(c, n, 0, op == AST_NODE_POST_INCREMENT ? LEX_TOKEN_PLUS : LEX_TOKEN_MINUS, true, loc);
				continue;
			}
			cc_node_require_scalar(c, n, "increment");
			if (cc_type_is_pointer(c, AST_NODE(n)->ty) && AST_TYPE_KIND(c, AST_TYPE(c, AST_NODE(n)->ty)->base) == AST_TYPE_SVE_BUILTIN)
				cc_pointer_void_arithmetic_check(c, AST_NODE(n)->ty, loc, false);
			r = cc_node_new(c, op, AST_QUALIFIED_TYPE_UNQUALIFIED(cc_type_unatomic(c, AST_NODE(n)->ty)), loc);
			AST_NODE(r)->a = n;
			n = r;
			continue;
		}
		default:
			return n;
		}
	}
}
