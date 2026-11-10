/* vcc front end: expressions: implicit conversions */
#include "ccimpl.h"

/* a floating constant to an integer: folded, as clang does (the kernel has no FP code); UINT32_MAX: not constant */
static __attribute__((noinline)) uint32_t fold_f2i(cc *c, uint32_t n, qty to)
{
	cval v;

	if (eval(c, n, &v) && v.kind == CV_FLT && v.f > -9.3e18 && v.f < 1.8e19) {
		u128 q = v.ld ? v.q : q_of_d(v.f);
		int bits = (int)ty_size(c, to) * 8;
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
	return r;
}

VPRIV __attribute__((noinline)) uint32_t decay_slow(cc *c, uint32_t n, qty t, int k)
{
	if (T(c, t)->flags & TF_ATOMIC)	/* lvalue conversion drops _Atomic: the load is atomic (emit) */
		return cast_node(c, n, QUNQ(ty_unatomic(c, t)), CK_NOP);
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

/* the type of n as an rvalue after integer promotion (bit-fields included) */
VPRIV qty promoted(cc *c, uint32_t n)
{
	qty t = QUNQ(N(n)->ty);
	const field *f = bitfield_of(c, n);

	if (f && is_integer(c, t) && KIND(c, t) != TY_BOOL) {
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
	if ((tk == TY_STRUCT || tk == TY_UNION) && compatible(c, from, to))
		return CK_NOP;
	/* vectors of one size convert lane-agnostically (clang's lax integer vector conversions) */
	if (tk == TY_VECTOR && fk == TY_VECTOR && T(c, to)->size == T(c, from)->size)
		return CK_NOP;
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

static void param_note(cc *c);

/* clang's message for a value of type from that cannot become to */
static void bad_conversion(cc *c, uint32_t n, qty to)
{
	pp_span r = rng(c, n);
	const char *f = tyname(c, N(n)->ty), *t = tyname(c, to);

	if (N(n)->flags & NF_ERR)
		return;
	if (c->cv_what == CX_PASS) {
		report(c, "error", r.b, &r, 1, NULL, 0, "passing '%s' to parameter of incompatible type '%s'", f, t);
		param_note(c);
		return;
	}
	switch (c->cv_what) {
	case CX_INIT:
		report(c, "error", c->cv_loc, &r, 1, NULL, 0, "initializing '%s' with an expression of incompatible type '%s'", t, f);
		break;
	case CX_PASS:
		report(c, "error", r.b, NULL, 0, NULL, 0, "passing '%s' to parameter of incompatible type '%s'", f, t);
		break;
	case CX_RETURN:
		report(c, "error", r.b, &r, 1, NULL, 0, "returning '%s' from a function with incompatible result type '%s'", f, t);
		break;
	case CX_ASSIGN:
		report(c, "error", c->cv_loc, &r, 1, NULL, 0, "assigning to '%s' from incompatible type '%s'", t, f);
		break;
	default:
		report(c, "error", r.b, &r, 1, NULL, 0, "incompatible types in conversion ('%s' to '%s')", f, t);
	}
}

/* "passing argument to parameter 'p' here" for the argument being converted */
static void param_note(cc *c)
{
	const sym *f;
	uint32_t p;

	if (!c->cv_fn || !(f = &c->syms[c->cv_fn])->psyms)
		return;
	p = c->lists[f->psyms - 1 + c->cv_arg];
	if (c->syms[p].name)
		note(c, c->syms[p].loc, "passing argument to parameter '%.*s' here", IDF(c->syms[p].name));
	else
		note(c, c->syms[p].loc, "passing argument to parameter here");
}

/* "... initializing 'T' with an expression of type 'F'" and the like, per context */
static void conv_error(cc *c, uint32_t n, qty to, const char *what, const char *flag)
{
	pp_span r = rng(c, n);
	const char *f = tyname(c, N(n)->ty), *t = tyname(c, to);
	uint32_t loc = c->cv_what == CX_PASS || c->cv_what == CX_RETURN ? r.b : c->cv_loc;
	int bit = !strcmp(flag, "-Wint-conversion") ? 1 : !strcmp(flag, "-Wincompatible-pointer-types") ? 2 : 4;
	char msg[1024];

	switch (c->cv_what) {
	case CX_INIT:
		snprintf(msg, sizeof msg, "%s initializing '%s' with an expression of type '%s' [%s]", what, t, f, flag);
		break;
	case CX_PASS:
		snprintf(msg, sizeof msg, "%s passing '%s' to parameter of type '%s' [%s]", what, f, t, flag);
		break;
	case CX_RETURN:
		snprintf(msg, sizeof msg, "%s returning '%s' from a function with result type '%s' [%s]", what, f, t, flag);
		break;
	default:
		snprintf(msg, sizeof msg, "%s assigning to '%s' from '%s' [%s]", what, t, f, flag);
	}
	static const char *const ipt[] = { "incompatible-pointer-types", NULL };

	if (c->o->conv_warn & bit) {
		/* -Wno-error=...: clang's warning (the message without its flag, which pp_warnx adds) */
		char *b = strrchr(msg, '[');

		if (pp_werror_named(c->p, flag + 2, bit == 4 ? ipt : NULL) == 1)
			goto err;	/* -Werror= again: clang's own error (no "-Werror,") */
		if (b && b > msg)
			b[-1] = 0;
		if (cc_warnx(c, flag + 2, true, bit == 4 ? ipt : NULL, loc, &r, 1, NULL, 0, "%s", msg) && c->cv_what == CX_PASS)
			param_note(c);
		return;
	}
err:
	report(c, "error", loc, &r, 1, NULL, 0, "%s", msg);
	if (c->cv_what == CX_PASS)
		param_note(c);
}

/* pointee types that clang's -Wincompatible-pointer-types accepts: compatible once
   qualifiers (at every level) and integer signedness are ignored, or one is void */
static bool ptr_ok(cc *c, qty a, qty b)
{
	int ka, kb;

	a = QUNQ(a), b = QUNQ(b);
	ka = KIND(c, a), kb = KIND(c, b);
	if (ka == TY_VOID || kb == TY_VOID || compatible(c, a, b))
		return true;
	if (ka == TY_PTR && kb == TY_PTR)
		return ptr_ok(c, T(c, a)->base, T(c, b)->base);
	if ((is_integer(c, a) || ka == TY_ENUM) && (is_integer(c, b) || kb == TY_ENUM))
		return ty_size(c, a) == ty_size(c, b);	/* -Wpointer-sign (a warning) */
	if (ka == TY_ARRAY && kb == TY_ARRAY)
		return ptr_ok(c, T(c, a)->base, T(c, b)->base);
	return false;
}

/* clang's implicit-conversion errors (int-conversion, incompatible pointer types) */
static uint32_t conv_check(cc *c, uint32_t n, qty to)
{
	qty from = N(n)->ty;

	if (N(n)->flags & NF_ERR)
		return n;
	if (is_ptr(c, to) && (is_integer(c, from) || KIND(c, from) == TY_ENUM) && !is_npc(c, n) && KIND(c, from) != TY_BOOL) {
		conv_error(c, n, to, "incompatible integer to pointer conversion", "-Wint-conversion");
	} else if ((is_integer(c, to) || KIND(c, to) == TY_ENUM) && KIND(c, to) != TY_BOOL && is_ptr(c, from)) {
		conv_error(c, n, to, "incompatible pointer to integer conversion", "-Wint-conversion");
	} else if (is_ptr(c, to) && is_ptr(c, from) && !(T(c, to)->flags & TF_BLOCK) && !(T(c, from)->flags & TF_BLOCK) &&
		   !ptr_ok(c, T(c, to)->base, T(c, from)->base)) {
		if (is_func(c, T(c, to)->base) && is_func(c, T(c, from)->base))
			conv_error(c, n, to, "incompatible function pointer types", "-Wincompatible-function-pointer-types");
		else
			conv_error(c, n, to, "incompatible pointer types", "-Wincompatible-pointer-types");
	} else {
		return n;
	}
	N(n)->flags |= NF_ERR;	/* no second message from convert */
	return n;
}

/* convert for an initialization, argument, return or assignment at loc */
VPRIV uint32_t convert_as(cc *c, uint32_t n, qty to, int what, uint32_t loc)
{
	uint8_t sw = c->cv_what;
	uint32_t sl = c->cv_loc;

	c->cv_what = (uint8_t)what, c->cv_loc = loc;
	if (what)
		n = conv_check(c, n, to);
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
	if (k < 0) {
		if (KIND(c, to) == TY_UNION && union_member_for(c, to, n))
			return cast_node(c, n, to, CK_UNION);
		bad_conversion(c, n, to);
		return n;
	}
	return cast_node(c, n, to, k);
}

/* clang spells the array type of this warning without the space: 'int[4]' */
static const char *arr_tyname(cc *c, qty t)
{
	const char *s = tyname(c, t), *sp = strstr(s, " [");
	char *o;

	if (!sp)
		return s;
	o = halloc(c, strlen(s) + 1);
	memcpy(o, s, (size_t)(sp - s));
	strcpy(o + (sp - s), sp + 1);
	return o;
}

/* -Warray-bounds: a[CONST] outside a's array type (addr: &a[n] may point one past the end) */
VPRIV void bounds_warn(cc *c, uint32_t n, bool addr)
{
	const node *x = N(n);
	uint32_t a, s = 0;
	const ctype *at;
	cval cv;
	i128 i;

	if (x->op != N_INDEX || (x->flags & NF_BOUNDS) || c->dead)
		return;
	N(n)->flags |= NF_BOUNDS;
	a = x->a;
	if (N(a)->op != N_CAST || N(a)->aux != CK_DECAY)
		return;
	a = N(a)->a;
	at = T(c, N(a)->ty);
	if (at->kind != TY_ARRAY || (at->flags & (TF_VLA | TF_INCOMPLETE)) || at->n < 2 ||
	    !cc_eval(c, x->b, &cv) || cv.kind != CV_INT)
		return;
	if (N(a)->op == N_VAR)
		s = N(a)->a;
	else if (N(a)->op != N_MEMBER)
		return;
	i = cv.i;
	if (i >= 0 && (i < (i128)at->n || (addr && i == (i128)at->n)))
		return;
	{
		pp_span r[2] = { rng(c, a), rng(c, x->b) };
		bool on;

		if (i < 0)
			on = cc_warnx(c, "array-bounds", true, NULL, r[0].b, &r[1], 1, NULL, 0,
				   "array index %lld is before the beginning of the array", (long long)i);
		else
			on = cc_warnx(c, "array-bounds", true, NULL, r[0].b, &r[1], 1, NULL, 0,
				   "array index %lld is past the end of the array (that has type '%s')", (long long)i,
				   arr_tyname(c, N(a)->ty));
		if (on && s) {
			int m;
			const char *nm = idname(c->syms[s].name, &m);

			note(c, c->syms[s].ple ? c->syms[s].plb : c->syms[s].loc, "array '%.*s' declared here", m, nm);
		}
	}
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
	if (t->kind >= TY_INT && t->kind <= TY_UINT128 && t->canon == QI(x->ty) && !(x->op == N_MEMBER && (x->flags & NF_BITFIELD)))
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
	pp_span r = rng(c, n);

	if (!(N(n)->flags & NF_LVALUE)) {
		report(c, "error", loc, &r, 1, NULL, 0, "expression is not assignable");
		return false;
	}
	if (QQ(N(n)->ty) & Q_CONST) {
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

VPRIV uint32_t mk_bin(cc *c, int op, qty ty, uint32_t a, uint32_t b, uint32_t loc)
{
	uint32_t r = mk2(c, N_BINARY, ty, loc, a, b);

	N(r)->aux = (uint16_t)op;
	return r;
}
