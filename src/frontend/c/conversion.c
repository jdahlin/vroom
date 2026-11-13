/* vcc front end: implicit conversions: casts, decay, promotion, rvalues and lvalue checks */
#include "cc-internal.h"

/* a floating constant to an integer: folded, as clang does (the kernel has no FP code); UINT32_MAX: not constant */
static __attribute__((noinline)) uint32_t cc_conversion_float_to_integer_fold(cc *c, uint32_t n, ast_qualified_type to)
{
	cc_value v;

	if (!cc_type_is_bitint(c, to) && cc_constant_eval(c, n, &v) && v.kind == CC_VALUE_FLOAT && v.f > -9.3e18 && v.f < 1.8e19) {
		u128 q = v.ld ? v.q : vsoftfloat_of_double(v.f);
		int bits = (int)cc_type_integer_bits(c, to);
		bool sg = cc_type_is_signed(c, to);
		i128 i = vsoftfloat_to_integer_saturating(q, 128, true);

		/* in range only: out of range is undefined, and eval saturates as clang */
		if (i == vsoftfloat_to_integer_saturating(q, bits, sg) || AST_TYPE_KIND(c, to) == AST_TYPE_BOOL)
			return cc_node_integer_new(c, cc_constant_truncate(c, i, to), to, AST_NODE(n)->loc);
	}
	return UINT32_MAX;
}

CC_PRIVATE uint32_t cc_cast_insert(cc *c, uint32_t n, ast_qualified_type to, int kind)
{
	uint32_t r;

	if (unlikely(kind == AST_CAST_FLOAT_TO_INTEGER) && (r = cc_conversion_float_to_integer_fold(c, n, to)) != UINT32_MAX)
		return r;
	r = cc_node_new(c, AST_NODE_CAST, to, AST_NODE(n)->loc);

	AST_NODE(r)->a = n;
	AST_NODE(r)->aux = (uint16_t)kind;
	r = cc_bitint_node_fold(c, r);
	if (AST_NODE(r)->op == AST_NODE_CAST && ((kind == AST_CAST_INTEGER_TO_FLOAT && cc_type_is_bitint(c, AST_NODE(n)->ty)) ||
	    (kind == AST_CAST_FLOAT_TO_INTEGER && cc_type_is_bitint(c, to)))) {
		cc_value v;
		if (cc_constant_eval(c, r, &v) && v.kind == CC_VALUE_FLOAT) {
			/* Keep the original cast for constant-expression grammar checks.
			 * The emitter uses its already rounded floating value. */
			AST_NODE(r)->c = v.ld ? cc_node_float128_new(c, v.q, to, AST_NODE(n)->loc) : cc_node_float_new(c, v.f, to, AST_NODE(n)->loc);
		} else {
			if (c->o->target == PP_LINUX_X86_64 || c->o->target == PP_LINUX_AARCH64)
				return cc_bitint_float_cast_build(c, n, to);
		}
	}
	return r;
}

CC_PRIVATE __attribute__((noinline)) uint32_t cc_conversion_decay_slow(cc *c, uint32_t n, ast_qualified_type t, int k)
{
	if (AST_TYPE(c, t)->flags & AST_TYPE_FLAG_ATOMIC) {
		if ((AST_NODE(n)->flags & AST_NODE_FLAG_LVALUE) && cc_atomic_wide_type_is_wide(c, t))
			return cc_atomic_wide_load_build(c, n);
		return cc_cast_insert(c, n, AST_QUALIFIED_TYPE_UNQUALIFIED(cc_type_unatomic(c, t)), AST_CAST_NOP);
	}
	if (k == AST_TYPE_ARRAY)
		return cc_cast_insert(c, n, cc_type_pointer(c, AST_TYPE(c, t)->base), AST_CAST_ARRAY_DECAY);
	return cc_cast_insert(c, n, cc_type_pointer(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t)), AST_CAST_FUNCTION_DECAY);
}

CC_PRIVATE const field *cc_node_bitfield(cc *c, uint32_t n)
{
	const node *x = AST_NODE(n);

	if (x->op == AST_NODE_MEMBER && (x->flags & AST_NODE_FLAG_BITFIELD))
		return cc_node_member_field(c, x);
	return NULL;
}

/* Some non-lvalue expressions retain bit-field width for integer
 * promotion. This is separate from cc_node_bitfield: sizeof/typeof may inspect
 * assignment, comma and prefix-increment results. Postfix results instead
 * have the ordinary declared type, as do explicit casts. */
static const field *cc_conversion_promotion_bitfield(cc *c, uint32_t n)
{
	for (;;) {
		const node *x = AST_NODE(n);
		if (x->op == AST_NODE_COMMA)
			n = x->b;
		else if (x->op == AST_NODE_ASSIGN || x->op == AST_NODE_PRE_INCREMENT || x->op == AST_NODE_PRE_DECREMENT)
			n = x->a;
		else
			return cc_node_bitfield(c, n);
	}
}

/* the type of n as an rvalue after integer promotion (bit-fields included) */
CC_PRIVATE ast_qualified_type cc_node_promoted_type(cc *c, uint32_t n)
{
	ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE(n)->ty);
	const field *f = cc_conversion_promotion_bitfield(c, n);

	if (f && cc_type_is_integer(c, t) && AST_TYPE_KIND(c, t) != AST_TYPE_BOOL && !AST_TYPE(c, t)->n) {
		if (f->width < 32)
			return cc_type_basic(AST_TYPE_INT);
		if (f->width == 32 && cc_type_rank(c, t) <= 4)
			return cc_type_is_signed(c, t) ? cc_type_basic(AST_TYPE_INT) : cc_type_basic(AST_TYPE_UNSIGNED_INT);
		return AST_QUALIFIED_TYPE(AST_TYPE(c, t)->canon, 0);
	}
	if (cc_type_is_integer(c, t))
		return cc_type_promote(c, t);
	return t;
}

CC_PRIVATE int cc_conversion_cast_kind(cc *c, ast_qualified_type from, ast_qualified_type to, uint32_t n)
{
	int fk = AST_TYPE_KIND(c, from), tk = AST_TYPE_KIND(c, to);

	if (tk == AST_TYPE_VOID)
		return AST_CAST_VOID;
	if (tk == AST_TYPE_BOOL)
		return fk == AST_TYPE_BOOL ? AST_CAST_NOP : AST_CAST_BOOL;
	if (cc_type_kind_is_integer(tk) || tk == AST_TYPE_ENUM) {
		if (cc_type_kind_is_integer(fk) || fk == AST_TYPE_ENUM)
			return cc_type_size(c, from) == cc_type_size(c, to) ? AST_CAST_NOP : AST_CAST_INTEGER;
		if (cc_type_kind_is_float(fk))
			return AST_CAST_FLOAT_TO_INTEGER;
		if (fk == AST_TYPE_POINTER)
			return AST_CAST_POINTER_TO_INTEGER;
		return -1;
	}
	if (cc_type_kind_is_float(tk)) {
		if (cc_type_kind_is_integer(fk) || fk == AST_TYPE_ENUM)
			return AST_CAST_INTEGER_TO_FLOAT;
		if (cc_type_kind_is_float(fk))
			return AST_CAST_FLOAT_TO_FLOAT;
		return -1;
	}
	if (tk == AST_TYPE_POINTER) {
		if (fk == AST_TYPE_POINTER)
			return AST_CAST_POINTER_TO_POINTER;
		if (cc_type_kind_is_integer(fk) || fk == AST_TYPE_ENUM)
			return n && cc_expression_is_null_pointer_constant(c, n) ? AST_CAST_NULL_POINTER : AST_CAST_INTEGER_TO_POINTER;
		return -1;
	}
	if ((tk == AST_TYPE_STRUCT || tk == AST_TYPE_UNION) && cc_type_is_compatible(c, cc_type_unatomic(c, from), cc_type_unatomic(c, to)))
		return AST_CAST_NOP;
	/* vectors of one size convert lane-agnostically (clang's lax integer vector conversions) */
	if (tk == AST_TYPE_VECTOR && fk == AST_TYPE_VECTOR && AST_TYPE(c, to)->size == AST_TYPE(c, from)->size) {
		if ((cc_type_is_bool_vector(c, to) || cc_type_is_bool_vector(c, from)) && !cc_type_is_compatible(c, to, from)) return -1;
		return AST_CAST_NOP;
	}
	return -1;
}

/* is union type u a valid target for n (transparent unions, GNU casts to union) */
CC_PRIVATE bool cc_conversion_is_union_target(cc *c, ast_qualified_type u, uint32_t n)
{
	const tag *r = &c->tags[AST_TYPE(c, u)->aux];
	ast_qualified_type from = AST_NODE(n)->ty;

	for (uint32_t i = 0; i < r->nfield; i++) {
		ast_qualified_type ft = r->f[i].ty;

		if (cc_type_is_compatible(c, ft, from))
			return true;
		if (cc_type_is_pointer(c, ft) && (cc_type_is_pointer(c, from) || cc_expression_is_null_pointer_constant(c, n)))
			return true;
	}
	return false;
}

/* an expression that changes something (for sizes evaluated only for their effects) */
CC_PRIVATE bool cc_expression_has_side_effects(cc *c, uint32_t n)
{
	const node *x;

	if (!n)
		return false;
	x = AST_NODE(n);
	switch (x->op) {
	case AST_NODE_CALL: case AST_NODE_ASSIGN: case AST_NODE_PRE_INCREMENT: case AST_NODE_PRE_DECREMENT: case AST_NODE_POST_INCREMENT: case AST_NODE_POST_DECREMENT: case AST_NODE_STATEMENT_EXPRESSION:
	case AST_NODE_BUILTIN: case AST_NODE_VA_ARG:
		return true;
	case AST_NODE_CAST: case AST_NODE_UNARY: case AST_NODE_ADDRESS: case AST_NODE_DEREFERENCE: case AST_NODE_MEMBER: case AST_NODE_SIZEOF_VLA:
		return cc_expression_has_side_effects(c, x->a);
	case AST_NODE_INDEX: case AST_NODE_BINARY: case AST_NODE_LOGICAL_AND: case AST_NODE_LOGICAL_OR: case AST_NODE_COMMA: case AST_NODE_COMPLEX:
		return cc_expression_has_side_effects(c, x->a) || cc_expression_has_side_effects(c, x->b);
	case AST_NODE_CONDITIONAL:
		return cc_expression_has_side_effects(c, x->a) || cc_expression_has_side_effects(c, x->b) || cc_expression_has_side_effects(c, x->c);
	default:
		return false;
	}
}

/* convert for an initialization, argument, return or assignment at loc */
CC_PRIVATE uint32_t cc_conversion_apply_as(cc *c, uint32_t n, ast_qualified_type to, int what, uint32_t loc)
{
	uint8_t sw = c->cv_what;
	uint32_t sl = c->cv_loc;

	c->cv_what = (uint8_t)what, c->cv_loc = loc;
	if (unlikely(c->constexpr_init && what == CC_CONVERSION_CONTEXT_INITIALIZER) && !AST_NODE_IS_ERROR(n))
		cc_constexpr_conversion_check(c, n, to);
	if (what) {
		n = cc_conversion_check(c, n, to);
		if (!(AST_NODE(n)->flags & AST_NODE_FLAG_ERROR))
			cc_conversion_constant_check(c, n, to);
	}
	n = cc_conversion_apply(c, n, to);
	c->cv_what = sw, c->cv_loc = sl;
	return n;
}

CC_PRIVATE __attribute__((noinline)) uint32_t cc_conversion_apply_slow(cc *c, uint32_t n, ast_qualified_type to)
{
	ast_qualified_type from = AST_NODE(n)->ty;
	int k;

	if (cc_type_is_complex(c, to) && (cc_type_is_arithmetic(c, from) || cc_type_is_complex(c, from)))
		return cc_complex_convert(c, n, to);
	if (cc_type_is_complex(c, from) && AST_TYPE_KIND(c, to) == AST_TYPE_BOOL)
		return cc_complex_to_bool(c, n);
	if (cc_type_is_complex(c, from) && cc_type_is_arithmetic(c, to))	/* the real part */
		return cc_conversion_apply(c, cc_node_rvalue(c, cc_complex_part(c, n, 0)), to);
	k = cc_conversion_cast_kind(c, from, to, n);
	if (k < 0 && cc_type_is_vector(c, to) && AST_TYPE(c, to)->ext && cc_type_is_arithmetic(c, from) && !cc_type_is_complex(c, from))
		/* clang: a scalar converts to an ext_vector_type by splatting */
		return cc_cast_insert(c, cc_conversion_apply(c, n, AST_TYPE(c, to)->base), AST_QUALIFIED_TYPE_UNQUALIFIED(to), AST_CAST_SPLAT);
	if (k < 0) {
		if (AST_TYPE_KIND(c, to) == AST_TYPE_UNION && cc_conversion_is_union_target(c, to, n))
			return cc_cast_insert(c, n, to, AST_CAST_UNION);
		cc_conversion_invalid_report(c, n, to);
		return n;
	}
	return cc_cast_insert(c, n, to, k);
}

static __attribute__((noinline)) uint32_t cc_node_rvalue_promote(cc *c, uint32_t n)
{
	{
		ast_qualified_type p = cc_node_promoted_type(c, n);

		if (p != AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE(n)->ty) || cc_node_bitfield(c, n))
			return cc_cast_insert(c, n, p, cc_type_size(c, p) == cc_type_size(c, AST_NODE(n)->ty) && !cc_node_bitfield(c, n) ? AST_CAST_NOP : AST_CAST_INTEGER);
	}
	return n;
}

/* value of n as an rvalue of its promoted type */
CC_PRIVATE uint32_t cc_node_rvalue(cc *c, uint32_t n)
{
	const node *x;
	const ctype *t;

	n = cc_conversion_decay(c, n);
	x = AST_NODE(n), t = AST_TYPE(c, x->ty);
	/* int and wider, not a bit-field: already promoted */
	if (t->kind >= AST_TYPE_INT && t->kind <= AST_TYPE_UINT128 && t->canon == AST_QUALIFIED_TYPE_INDEX(x->ty) && !cc_conversion_promotion_bitfield(c, n))
		return n;
	if (!cc_type_kind_is_integer(t->kind) && t->kind != AST_TYPE_ENUM)
		return n;
	return cc_node_rvalue_promote(c, n);
}

CC_PRIVATE void cc_node_require_scalar(cc *c, uint32_t n, const char *what)
{
	if (!cc_type_is_scalar(c, AST_NODE(n)->ty))
		cc_report_error(c, AST_NODE(n)->loc, "%s requires a scalar operand", what);
}

/* n is assigned (or incremented) by the operator at loc */
CC_PRIVATE bool cc_node_require_lvalue(cc *c, uint32_t n, uint32_t loc)
{
	diag_span r;	/* the operand's range, for the diagnostics only (pp_token_end_find is not free) */

	if (!(AST_NODE(n)->flags & AST_NODE_FLAG_LVALUE)) {
		r = cc_node_range(c, n);
		report(c, "error", loc, &r, 1, NULL, 0, "expression is not assignable");
		return false;
	}
	if (AST_QUALIFIED_TYPE_QUALIFIERS(AST_NODE(n)->ty) & AST_QUALIFIER_CONST) {
		r = cc_node_range(c, n);
		if (AST_NODE(n)->op == AST_NODE_VARIABLE) {
			const sym *v = &c->syms[AST_NODE(n)->a];
			diag_span vr = { v->plb, pp_token_end_find(c->p, v->ple) };

			if (!v->ple)
				vr = (diag_span){ v->loc, v->loc };
			report(c, "error", loc, &r, 1, NULL, 0, "cannot assign to variable '%.*s' with const-qualified type '%s'",
			       CC_IDENTIFIER(v->name), tyname(c, AST_NODE(n)->ty));
			report(c, "note", v->loc, &vr, 1, NULL, 0, "variable '%.*s' declared const here", CC_IDENTIFIER(v->name));
		} else {
			report(c, "error", loc, &r, 1, NULL, 0, "read-only variable is not assignable");
		}
		return false;
	}
	return true;
}
