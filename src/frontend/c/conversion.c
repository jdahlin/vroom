/* vcc front end: implicit conversions: casts, decay, promotion, rvalues and lvalue checks */
#include "ccimpl.h"

/* a floating constant to an integer: folded, as clang does (the kernel has no FP code); UINT32_MAX: not constant */
static __attribute__((noinline)) uint32_t fold_f2i(cc *c, uint32_t n, qty to)
{
	cval v;

	if (!is_bigint(c, to) && eval(c, n, &v) && v.kind == CV_FLT && v.f > -9.3e18 && v.f < 1.8e19) {
		u128 q = v.ld ? v.q : q_of_d(v.f);
		int bits = (int)int_bits(c, to);
		bool sg = is_signed(c, to);
		i128 i = q_to_int(q, 128, true);

		/* in range only: out of range is undefined, and eval saturates as clang */
		if (i == q_to_int(q, bits, sg) || KIND(c, to) == TY_BOOL)
			return mk_int(c, trunc_to(c, i, to), to, N(n)->loc);
	}
	return UINT32_MAX;
}

VPRIV uint32_t cast_node(cc *c, uint32_t n, qty to, int kind)
{
	uint32_t r;

	if (unlikely(kind == CK_F2I) && (r = fold_f2i(c, n, to)) != UINT32_MAX)
		return r;
	r = mk(c, N_CAST, to, N(n)->loc);

	N(r)->a = n;
	N(r)->aux = (uint16_t)kind;
	r = big_fold_node(c, r);
	if (N(r)->op == N_CAST && ((kind == CK_I2F && is_bigint(c, N(n)->ty)) ||
	    (kind == CK_F2I && is_bigint(c, to)))) {
		cval v;
		if (eval(c, r, &v) && v.kind == CV_FLT) {
			/* Keep the original cast for constant-expression grammar checks.
			 * The emitter uses its already rounded floating value. */
			N(r)->c = v.ld ? mk_fltq(c, v.q, to, N(n)->loc) : mk_flt(c, v.f, to, N(n)->loc);
		} else {
			if (c->o->target == PP_LINUX_X86_64 || c->o->target == PP_LINUX_AARCH64)
				return big_float_cast(c, n, to);
		}
	}
	return r;
}

VPRIV __attribute__((noinline)) uint32_t decay_slow(cc *c, uint32_t n, qty t, int k)
{
	if (T(c, t)->flags & TF_ATOMIC) {
		if ((N(n)->flags & NF_LVALUE) && atomic_wide(c, t))
			return atomic_load_expr(c, n);
		return cast_node(c, n, QUNQ(ty_unatomic(c, t)), CK_NOP);
	}
	if (k == TY_ARRAY)
		return cast_node(c, n, ty_ptr(c, T(c, t)->base), CK_DECAY);
	return cast_node(c, n, ty_ptr(c, QUNQ(t)), CK_FDECAY);
}

VPRIV const field *bitfield_of(cc *c, uint32_t n)
{
	const node *x = N(n);

	if (x->op == N_MEMBER && (x->flags & NF_BITFIELD))
		return node_field(c, x);
	return NULL;
}

/* Some non-lvalue expressions retain bit-field width for integer
 * promotion. This is separate from bitfield_of: sizeof/typeof may inspect
 * assignment, comma and prefix-increment results. Postfix results instead
 * have the ordinary declared type, as do explicit casts. */
static const field *promotion_bitfield(cc *c, uint32_t n)
{
	for (;;) {
		const node *x = N(n);
		if (x->op == N_COMMA)
			n = x->b;
		else if (x->op == N_ASSIGN || x->op == N_PREINC || x->op == N_PREDEC)
			n = x->a;
		else
			return bitfield_of(c, n);
	}
}

/* the type of n as an rvalue after integer promotion (bit-fields included) */
VPRIV qty promoted(cc *c, uint32_t n)
{
	qty t = QUNQ(N(n)->ty);
	const field *f = promotion_bitfield(c, n);

	if (f && is_integer(c, t) && KIND(c, t) != TY_BOOL && !T(c, t)->n) {
		if (f->width < 32)
			return prim(TY_INT);
		if (f->width == 32 && rank(c, t) <= 4)
			return is_signed(c, t) ? prim(TY_INT) : prim(TY_UINT);
		return QT(T(c, t)->canon, 0);
	}
	if (is_integer(c, t))
		return promote(c, t);
	return t;
}

VPRIV int conv_kind(cc *c, qty from, qty to, uint32_t n)
{
	int fk = KIND(c, from), tk = KIND(c, to);

	if (tk == TY_VOID)
		return CK_VOID;
	if (tk == TY_BOOL)
		return fk == TY_BOOL ? CK_NOP : CK_BOOL;
	if (is_int_kind(tk) || tk == TY_ENUM) {
		if (is_int_kind(fk) || fk == TY_ENUM)
			return ty_size(c, from) == ty_size(c, to) ? CK_NOP : CK_INT;
		if (is_flt_kind(fk))
			return CK_F2I;
		if (fk == TY_PTR)
			return CK_P2I;
		return -1;
	}
	if (is_flt_kind(tk)) {
		if (is_int_kind(fk) || fk == TY_ENUM)
			return CK_I2F;
		if (is_flt_kind(fk))
			return CK_F2F;
		return -1;
	}
	if (tk == TY_PTR) {
		if (fk == TY_PTR)
			return CK_P2P;
		if (is_int_kind(fk) || fk == TY_ENUM)
			return n && is_npc(c, n) ? CK_NULL : CK_I2P;
		return -1;
	}
	if ((tk == TY_STRUCT || tk == TY_UNION) && compatible(c, ty_unatomic(c, from), ty_unatomic(c, to)))
		return CK_NOP;
	/* vectors of one size convert lane-agnostically (clang's lax integer vector conversions) */
	if (tk == TY_VECTOR && fk == TY_VECTOR && T(c, to)->size == T(c, from)->size) {
		if ((is_bool_vector(c, to) || is_bool_vector(c, from)) && !compatible(c, to, from)) return -1;
		return CK_NOP;
	}
	return -1;
}

/* is union type u a valid target for n (transparent unions, GNU casts to union) */
VPRIV bool union_member_for(cc *c, qty u, uint32_t n)
{
	const tag *r = &c->tags[T(c, u)->aux];
	qty from = N(n)->ty;

	for (uint32_t i = 0; i < r->nfield; i++) {
		qty ft = r->f[i].ty;

		if (compatible(c, ft, from))
			return true;
		if (is_ptr(c, ft) && (is_ptr(c, from) || is_npc(c, n)))
			return true;
	}
	return false;
}

/* an expression that changes something (for sizes evaluated only for their effects) */
VPRIV bool has_side_effects(cc *c, uint32_t n)
{
	const node *x;

	if (!n)
		return false;
	x = N(n);
	switch (x->op) {
	case N_CALL: case N_ASSIGN: case N_PREINC: case N_PREDEC: case N_POSTINC: case N_POSTDEC: case N_STMTEXPR:
	case N_BUILTIN: case N_VAARG:
		return true;
	case N_CAST: case N_UNARY: case N_ADDR: case N_DEREF: case N_MEMBER: case N_SIZEOF_VLA:
		return has_side_effects(c, x->a);
	case N_INDEX: case N_BINARY: case N_LOGAND: case N_LOGOR: case N_COMMA: case N_CPLX:
		return has_side_effects(c, x->a) || has_side_effects(c, x->b);
	case N_COND:
		return has_side_effects(c, x->a) || has_side_effects(c, x->b) || has_side_effects(c, x->c);
	default:
		return false;
	}
}

/* convert for an initialization, argument, return or assignment at loc */
VPRIV uint32_t convert_as(cc *c, uint32_t n, qty to, int what, uint32_t loc)
{
	uint8_t sw = c->cv_what;
	uint32_t sl = c->cv_loc;

	c->cv_what = (uint8_t)what, c->cv_loc = loc;
	if (unlikely(c->constexpr_init && what == CX_INIT) && !ISERR(n))
		constexpr_conversion(c, n, to);
	if (what) {
		n = conv_check(c, n, to);
		if (!(N(n)->flags & NF_ERR))
			const_conv(c, n, to);
	}
	n = convert(c, n, to);
	c->cv_what = sw, c->cv_loc = sl;
	return n;
}

VPRIV __attribute__((noinline)) uint32_t convert_slow(cc *c, uint32_t n, qty to)
{
	qty from = N(n)->ty;
	int k;

	if (is_complex(c, to) && (is_arith(c, from) || is_complex(c, from)))
		return to_complex(c, n, to);
	if (is_complex(c, from) && KIND(c, to) == TY_BOOL)
		return cplx_bool(c, n);
	if (is_complex(c, from) && is_arith(c, to))	/* the real part */
		return convert(c, rvalue(c, cplx_part(c, n, 0)), to);
	k = conv_kind(c, from, to, n);
	if (k < 0 && is_vector(c, to) && T(c, to)->ext && is_arith(c, from) && !is_complex(c, from))
		/* clang: a scalar converts to an ext_vector_type by splatting */
		return cast_node(c, convert(c, n, T(c, to)->base), QUNQ(to), CK_SPLAT);
	if (k < 0) {
		if (KIND(c, to) == TY_UNION && union_member_for(c, to, n))
			return cast_node(c, n, to, CK_UNION);
		bad_conversion(c, n, to);
		return n;
	}
	return cast_node(c, n, to, k);
}

static __attribute__((noinline)) uint32_t rvalue_promote(cc *c, uint32_t n)
{
	{
		qty p = promoted(c, n);

		if (p != QUNQ(N(n)->ty) || bitfield_of(c, n))
			return cast_node(c, n, p, ty_size(c, p) == ty_size(c, N(n)->ty) && !bitfield_of(c, n) ? CK_NOP : CK_INT);
	}
	return n;
}

/* value of n as an rvalue of its promoted type */
VPRIV uint32_t rvalue(cc *c, uint32_t n)
{
	const node *x;
	const ctype *t;

	n = decay(c, n);
	x = N(n), t = T(c, x->ty);
	/* int and wider, not a bit-field: already promoted */
	if (t->kind >= TY_INT && t->kind <= TY_UINT128 && t->canon == QI(x->ty) && !promotion_bitfield(c, n))
		return n;
	if (!is_int_kind(t->kind) && t->kind != TY_ENUM)
		return n;
	return rvalue_promote(c, n);
}

VPRIV void need_scalar(cc *c, uint32_t n, const char *what)
{
	if (!is_scalar(c, N(n)->ty))
		cc_error(c, N(n)->loc, "%s requires a scalar operand", what);
}

/* n is assigned (or incremented) by the operator at loc */
VPRIV bool need_lvalue(cc *c, uint32_t n, uint32_t loc)
{
	pp_span r;	/* the operand's range, for the diagnostics only (pp_tok_end is not free) */

	if (!(N(n)->flags & NF_LVALUE)) {
		r = rng(c, n);
		report(c, "error", loc, &r, 1, NULL, 0, "expression is not assignable");
		return false;
	}
	if (QQ(N(n)->ty) & Q_CONST) {
		r = rng(c, n);
		if (N(n)->op == N_VAR) {
			const sym *v = &c->syms[N(n)->a];
			pp_span vr = { v->plb, pp_tok_end(c->p, v->ple) };

			if (!v->ple)
				vr = (pp_span){ v->loc, v->loc };
			report(c, "error", loc, &r, 1, NULL, 0, "cannot assign to variable '%.*s' with const-qualified type '%s'",
			       IDF(v->name), tyname(c, N(n)->ty));
			report(c, "note", v->loc, &vr, 1, NULL, 0, "variable '%.*s' declared const here", IDF(v->name));
		} else {
			report(c, "error", loc, &r, 1, NULL, 0, "read-only variable is not assignable");
		}
		return false;
	}
	return true;
}
