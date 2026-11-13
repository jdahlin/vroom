/* vcc front end: implicit conversions: casts, decay, promotion, rvalues and lvalue checks */
#include "cc-internal.h"

/* a floating constant to an integer: folded, as clang does (the kernel has no FP code); UINT32_MAX: not constant */
static __attribute__((noinline)) uint32_t cc_conversion_float_to_integer_fold(cc *c, uint32_t n, qty to)
{
	cval v;

	if (!cc_type_is_bitint(c, to) && cc_constant_eval(c, n, &v) && v.kind == CV_FLT && v.f > -9.3e18 && v.f < 1.8e19) {
		u128 q = v.ld ? v.q : vsoftfloat_of_double(v.f);
		int bits = (int)cc_type_integer_bits(c, to);
		bool sg = cc_type_is_signed(c, to);
		i128 i = vsoftfloat_to_integer_saturating(q, 128, true);

		/* in range only: out of range is undefined, and eval saturates as clang */
		if (i == vsoftfloat_to_integer_saturating(q, bits, sg) || AST_TYPE_KIND(c, to) == TY_BOOL)
			return cc_node_integer_new(c, cc_constant_truncate(c, i, to), to, AST_NODE(n)->loc);
	}
	return UINT32_MAX;
}

CC_PRIVATE uint32_t cc_cast_insert(cc *c, uint32_t n, qty to, int kind)
{
	uint32_t r;

	if (unlikely(kind == CK_F2I) && (r = cc_conversion_float_to_integer_fold(c, n, to)) != UINT32_MAX)
		return r;
	r = cc_node_new(c, N_CAST, to, AST_NODE(n)->loc);

	AST_NODE(r)->a = n;
	AST_NODE(r)->aux = (uint16_t)kind;
	r = cc_bitint_node_fold(c, r);
	if (AST_NODE(r)->op == N_CAST && ((kind == CK_I2F && cc_type_is_bitint(c, AST_NODE(n)->ty)) ||
	    (kind == CK_F2I && cc_type_is_bitint(c, to)))) {
		cval v;
		if (cc_constant_eval(c, r, &v) && v.kind == CV_FLT) {
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

CC_PRIVATE __attribute__((noinline)) uint32_t cc_conversion_decay_slow(cc *c, uint32_t n, qty t, int k)
{
	if (AST_TYPE(c, t)->flags & TF_ATOMIC) {
		if ((AST_NODE(n)->flags & NF_LVALUE) && cc_atomic_wide_type_is_wide(c, t))
			return cc_atomic_wide_load_build(c, n);
		return cc_cast_insert(c, n, QUNQ(cc_type_unatomic(c, t)), CK_NOP);
	}
	if (k == TY_ARRAY)
		return cc_cast_insert(c, n, cc_type_pointer(c, AST_TYPE(c, t)->base), CK_DECAY);
	return cc_cast_insert(c, n, cc_type_pointer(c, QUNQ(t)), CK_FDECAY);
}

CC_PRIVATE const field *cc_node_bitfield(cc *c, uint32_t n)
{
	const node *x = AST_NODE(n);

	if (x->op == N_MEMBER && (x->flags & NF_BITFIELD))
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
		if (x->op == N_COMMA)
			n = x->b;
		else if (x->op == N_ASSIGN || x->op == N_PREINC || x->op == N_PREDEC)
			n = x->a;
		else
			return cc_node_bitfield(c, n);
	}
}

/* the type of n as an rvalue after integer promotion (bit-fields included) */
CC_PRIVATE qty cc_node_promoted_type(cc *c, uint32_t n)
{
	qty t = QUNQ(AST_NODE(n)->ty);
	const field *f = cc_conversion_promotion_bitfield(c, n);

	if (f && cc_type_is_integer(c, t) && AST_TYPE_KIND(c, t) != TY_BOOL && !AST_TYPE(c, t)->n) {
		if (f->width < 32)
			return cc_type_basic(TY_INT);
		if (f->width == 32 && cc_type_rank(c, t) <= 4)
			return cc_type_is_signed(c, t) ? cc_type_basic(TY_INT) : cc_type_basic(TY_UINT);
		return QT(AST_TYPE(c, t)->canon, 0);
	}
	if (cc_type_is_integer(c, t))
		return cc_type_promote(c, t);
	return t;
}

CC_PRIVATE int cc_conversion_cast_kind(cc *c, qty from, qty to, uint32_t n)
{
	int fk = AST_TYPE_KIND(c, from), tk = AST_TYPE_KIND(c, to);

	if (tk == TY_VOID)
		return CK_VOID;
	if (tk == TY_BOOL)
		return fk == TY_BOOL ? CK_NOP : CK_BOOL;
	if (cc_type_kind_is_integer(tk) || tk == TY_ENUM) {
		if (cc_type_kind_is_integer(fk) || fk == TY_ENUM)
			return cc_type_size(c, from) == cc_type_size(c, to) ? CK_NOP : CK_INT;
		if (cc_type_kind_is_float(fk))
			return CK_F2I;
		if (fk == TY_PTR)
			return CK_P2I;
		return -1;
	}
	if (cc_type_kind_is_float(tk)) {
		if (cc_type_kind_is_integer(fk) || fk == TY_ENUM)
			return CK_I2F;
		if (cc_type_kind_is_float(fk))
			return CK_F2F;
		return -1;
	}
	if (tk == TY_PTR) {
		if (fk == TY_PTR)
			return CK_P2P;
		if (cc_type_kind_is_integer(fk) || fk == TY_ENUM)
			return n && cc_expression_is_null_pointer_constant(c, n) ? CK_NULL : CK_I2P;
		return -1;
	}
	if ((tk == TY_STRUCT || tk == TY_UNION) && cc_type_is_compatible(c, cc_type_unatomic(c, from), cc_type_unatomic(c, to)))
		return CK_NOP;
	/* vectors of one size convert lane-agnostically (clang's lax integer vector conversions) */
	if (tk == TY_VECTOR && fk == TY_VECTOR && AST_TYPE(c, to)->size == AST_TYPE(c, from)->size) {
		if ((cc_type_is_bool_vector(c, to) || cc_type_is_bool_vector(c, from)) && !cc_type_is_compatible(c, to, from)) return -1;
		return CK_NOP;
	}
	return -1;
}

/* is union type u a valid target for n (transparent unions, GNU casts to union) */
CC_PRIVATE bool cc_conversion_is_union_target(cc *c, qty u, uint32_t n)
{
	const tag *r = &c->tags[AST_TYPE(c, u)->aux];
	qty from = AST_NODE(n)->ty;

	for (uint32_t i = 0; i < r->nfield; i++) {
		qty ft = r->f[i].ty;

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
	case N_CALL: case N_ASSIGN: case N_PREINC: case N_PREDEC: case N_POSTINC: case N_POSTDEC: case N_STMTEXPR:
	case N_BUILTIN: case N_VAARG:
		return true;
	case N_CAST: case N_UNARY: case N_ADDR: case N_DEREF: case N_MEMBER: case N_SIZEOF_VLA:
		return cc_expression_has_side_effects(c, x->a);
	case N_INDEX: case N_BINARY: case N_LOGAND: case N_LOGOR: case N_COMMA: case N_CPLX:
		return cc_expression_has_side_effects(c, x->a) || cc_expression_has_side_effects(c, x->b);
	case N_COND:
		return cc_expression_has_side_effects(c, x->a) || cc_expression_has_side_effects(c, x->b) || cc_expression_has_side_effects(c, x->c);
	default:
		return false;
	}
}

/* convert for an initialization, argument, return or assignment at loc */
CC_PRIVATE uint32_t cc_conversion_apply_as(cc *c, uint32_t n, qty to, int what, uint32_t loc)
{
	uint8_t sw = c->cv_what;
	uint32_t sl = c->cv_loc;

	c->cv_what = (uint8_t)what, c->cv_loc = loc;
	if (unlikely(c->constexpr_init && what == CC_CONVERSION_CONTEXT_INITIALIZER) && !AST_NODE_IS_ERROR(n))
		cc_constexpr_conversion_check(c, n, to);
	if (what) {
		n = cc_conversion_check(c, n, to);
		if (!(AST_NODE(n)->flags & NF_ERR))
			cc_conversion_constant_check(c, n, to);
	}
	n = cc_conversion_apply(c, n, to);
	c->cv_what = sw, c->cv_loc = sl;
	return n;
}

CC_PRIVATE __attribute__((noinline)) uint32_t cc_conversion_apply_slow(cc *c, uint32_t n, qty to)
{
	qty from = AST_NODE(n)->ty;
	int k;

	if (cc_type_is_complex(c, to) && (cc_type_is_arithmetic(c, from) || cc_type_is_complex(c, from)))
		return cc_complex_convert(c, n, to);
	if (cc_type_is_complex(c, from) && AST_TYPE_KIND(c, to) == TY_BOOL)
		return cc_complex_to_bool(c, n);
	if (cc_type_is_complex(c, from) && cc_type_is_arithmetic(c, to))	/* the real part */
		return cc_conversion_apply(c, cc_node_rvalue(c, cc_complex_part(c, n, 0)), to);
	k = cc_conversion_cast_kind(c, from, to, n);
	if (k < 0 && cc_type_is_vector(c, to) && AST_TYPE(c, to)->ext && cc_type_is_arithmetic(c, from) && !cc_type_is_complex(c, from))
		/* clang: a scalar converts to an ext_vector_type by splatting */
		return cc_cast_insert(c, cc_conversion_apply(c, n, AST_TYPE(c, to)->base), QUNQ(to), CK_SPLAT);
	if (k < 0) {
		if (AST_TYPE_KIND(c, to) == TY_UNION && cc_conversion_is_union_target(c, to, n))
			return cc_cast_insert(c, n, to, CK_UNION);
		cc_conversion_invalid_report(c, n, to);
		return n;
	}
	return cc_cast_insert(c, n, to, k);
}

static __attribute__((noinline)) uint32_t cc_node_rvalue_promote(cc *c, uint32_t n)
{
	{
		qty p = cc_node_promoted_type(c, n);

		if (p != QUNQ(AST_NODE(n)->ty) || cc_node_bitfield(c, n))
			return cc_cast_insert(c, n, p, cc_type_size(c, p) == cc_type_size(c, AST_NODE(n)->ty) && !cc_node_bitfield(c, n) ? CK_NOP : CK_INT);
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
	if (t->kind >= TY_INT && t->kind <= TY_UINT128 && t->canon == QI(x->ty) && !cc_conversion_promotion_bitfield(c, n))
		return n;
	if (!cc_type_kind_is_integer(t->kind) && t->kind != TY_ENUM)
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

	if (!(AST_NODE(n)->flags & NF_LVALUE)) {
		r = cc_node_range(c, n);
		report(c, "error", loc, &r, 1, NULL, 0, "expression is not assignable");
		return false;
	}
	if (QQ(AST_NODE(n)->ty) & Q_CONST) {
		r = cc_node_range(c, n);
		if (AST_NODE(n)->op == N_VAR) {
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
