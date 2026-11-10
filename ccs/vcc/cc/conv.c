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

/* the converted value's role, as a label on its span (Elm: "the 2nd argument to f"); the clang
   style prints no labels */
static void role_label(cc *c, vdiag *d, pp_span r)
{
	char b[160];

	if (c->cv_what == CX_PASS && c->cv_fn && c->syms[c->cv_fn].name)
		snprintf(b, sizeof b, "argument %u of '%.*s'", c->cv_arg + 1, IDF(c->syms[c->cv_fn].name));
	else if (c->cv_what == CX_PASS)
		snprintf(b, sizeof b, "argument %u", c->cv_arg + 1);
	else if (c->cv_what == CX_RETURN)
		snprintf(b, sizeof b, "the returned value");
	else {
		pp_diag_label(c->p, d, r.b, r.e, NULL);
		return;
	}
	pp_diag_label(c->p, d, r.b, r.e, b);
}

/* clang's message for a value of type from that cannot become to */
static void bad_conversion(cc *c, uint32_t n, qty to)
{
	pp_span r = rng(c, n);
	const char *f = tyname(c, N(n)->ty), *t = tyname(c, to);

	if (N(n)->flags & NF_ERR)
		return;
	if (c->cv_what == CX_PASS || c->cv_what == CX_RETURN) {
		vdiag *d;

		error_limit(c), c->errors++, c->ndiag++;
		if (c->cv_what == CX_PASS)
			d = pp_diag_begin(c->p, VDL_ERROR, r.b, "passing '%s' to parameter of incompatible type '%s'", f, t);
		else
			d = pp_diag_begin(c->p, VDL_ERROR, r.b, "returning '%s' from a function with incompatible result type '%s'", f, t);
		role_label(c, d, r);
		pp_diag_emit(c->p, d);
		if (c->cv_what == CX_PASS)
			param_note(c);
		return;
	}
	switch (c->cv_what) {
	case CX_INIT:
		report(c, "error", c->cv_loc, &r, 1, NULL, 0, "initializing '%s' with an expression of incompatible type '%s'", t, f);
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

/* "... initializing 'T' with an expression of type 'F'" and the like, per context: clang's
   warnings that are errors by default (id: clang's diagnostic, flag: its -W flag) */
/* the type clang names for n: an array's or function's before its decay */
static qty shown_ty(cc *c, uint32_t n)
{
	if (N(n)->op == N_CAST && (N(n)->aux == CK_DECAY || N(n)->aux == CK_FDECAY) && !c->span[n].e)
		return N(N(n)->a)->ty;
	return N(n)->ty;
}

/* a conversion diagnostic in clang's four forms (initializing, passing, returning,
   assigning), "<pre> initializing ... <post>"; warn: a warning of flag (on by default, groups),
   else an error.  false: not reported (the warning is off) */
static bool conv_diag_fx(cc *c, uint32_t n, qty to, const char *pre, const char *post, const char *id, const char *flag,
			 const char *const *groups, bool warn, const pp_fixit *fx);

static bool conv_diag(cc *c, uint32_t n, qty to, const char *pre, const char *post, const char *id, const char *flag,
		      const char *const *groups, bool warn)
{
	return conv_diag_fx(c, n, to, pre, post, id, flag, groups, warn, NULL);
}

static bool conv_diag_fx(cc *c, uint32_t n, qty to, const char *pre, const char *post, const char *id, const char *flag,
			 const char *const *groups, bool warn, const pp_fixit *fx)
{
	pp_span r = rng(c, n);
	const char *f = tyname(c, shown_ty(c, n)), *t = tyname(c, to);
	uint32_t loc = c->cv_what == CX_PASS || c->cv_what == CX_RETURN ? r.b : c->cv_loc;
	const char *sp = *pre ? " " : "";
	int lv = warn ? pp_diag_wlevel(c->p, flag, true, groups, loc) : VDL_ERROR;
	vdiag *d;

	if (lv < 0)
		return false;
	if (lv == VDL_ERROR)
		error_limit(c), c->errors++;
	c->ndiag++;
	switch (c->cv_what) {
	case CX_INIT:
		d = pp_diag_begin(c->p, lv, loc, "%s%sinitializing '%s' with an expression of type '%s'%s", pre, sp, t, f, post);
		break;
	case CX_PASS:
		d = pp_diag_begin(c->p, lv, loc, "%s%spassing '%s' to parameter of type '%s'%s", pre, sp, f, t, post);
		break;
	case CX_RETURN:
		d = pp_diag_begin(c->p, lv, loc, "%s%sreturning '%s' from a function with result type '%s'%s", pre, sp, f, t, post);
		break;
	default:
		d = pp_diag_begin(c->p, lv, loc, "%s%sassigning to '%s' from '%s'%s", pre, sp, t, f, post);
	}
	d->id = id;
	pp_diag_flag(d, flag);
	d->werror = warn && lv == VDL_ERROR;
	role_label(c, d, r);
	if (fx)
		pp_diag_fix(c->p, d, fx->b, fx->e, fx->s, VDA_MAYBE);
	pp_diag_emit(c->p, d);
	if (c->cv_what == CX_PASS)
		param_note(c);
	return true;
}

static void conv_error(cc *c, uint32_t n, qty to, const char *what, const char *id, const char *flag)
{
	conv_diag(c, n, to, what, "", id, flag, NULL, false);
}

/* -Wint-conversion with clang's tryToFixConversion: "; remove &", "; dereference with *" or
   "; take the address with &" when that gives exactly the target type, with the fix-it */
static void int_conv_error(cc *c, uint32_t n, qty to, bool to_ptr)
{
	uint32_t m = n;
	pp_fixit fx = { 0, 0, NULL };
	const char *h = "";

	while (N(m)->op == N_CAST && !c->span[m].e)
		m = N(m)->a;
	if (!to_ptr && N(m)->op == N_ADDR && compatible(c, QUNQ(N(N(m)->a)->ty), QUNQ(to)))
		h = "; remove &", fx = (pp_fixit){ rng(c, m).b, rng(c, m).b + 1, "" };
	else if (!to_ptr && is_ptr(c, N(m)->ty) && compatible(c, QUNQ(T(c, N(m)->ty)->base), QUNQ(to)))
		h = "; dereference with *", fx = (pp_fixit){ rng(c, m).b, rng(c, m).b, "*" };
	else if (to_ptr && (N(m)->flags & NF_LVALUE) && compatible(c, QUNQ(T(c, to)->base), QUNQ(N(m)->ty)))
		h = "; take the address with &", fx = (pp_fixit){ rng(c, m).b, rng(c, m).b, "&" };
	conv_diag_fx(c, n, to, to_ptr ? "incompatible integer to pointer conversion" : "incompatible pointer to integer conversion",
		     h, to_ptr ? "ext_typecheck_convert_int_pointer" : "ext_typecheck_convert_pointer_int", "int-conversion", NULL,
		     false, fx.s ? &fx : NULL);
}

/* clang's checkPointerTypesForAssignment on pointees that ptr_ok accepted: qualifiers the
   source's pointee has and the target's lacks (-Wincompatible-pointer-types-discards-
   qualifiers), deeper levels that differ at all, or integers of another sign (-Wpointer-sign) */
static void ptr_quals(cc *c, uint32_t n, qty to)
{
	static const char *const g[] = { "incompatible-pointer-types", "pedantic", NULL };	/* (ExtWarns: -pedantic-errors) */
	qty a = T(c, to)->base, b = T(c, N(n)->ty)->base;
	unsigned qa = QQ(a) & (Q_CONST | Q_VOLATILE | Q_RESTRICT), qb = QQ(b) & (Q_CONST | Q_VOLATILE | Q_RESTRICT);

	if (KIND(c, a) == TY_FUNC || KIND(c, b) == TY_FUNC)
		return;	/* (void * <-> function pointer: clang says nothing here, -Wpedantic does) */
	if (qb & ~qa) {
		conv_diag(c, n, to, "", " discards qualifiers", "ext_typecheck_convert_discards_qualifiers",
			  "incompatible-pointer-types-discards-qualifiers", g, true);
		return;
	}
	if (KIND(c, a) == TY_VOID || KIND(c, b) == TY_VOID)
		return;
	for (qty x = a, y = b; KIND(c, x) == TY_PTR && KIND(c, y) == TY_PTR;) {
		x = T(c, x)->base, y = T(c, y)->base;
		if ((QQ(x) ^ QQ(y)) & (Q_CONST | Q_VOLATILE | Q_RESTRICT)) {
			conv_diag(c, n, to, "", " discards qualifiers in nested pointer types",
				  "ext_nested_pointer_qualifier_mismatch", "incompatible-pointer-types-discards-qualifiers", g, true);
			return;
		}
		if (KIND(c, x) == TY_VOID || KIND(c, y) == TY_VOID)
			return;
	}
	a = QUNQ(a), b = QUNQ(b);
	if (QI(canon(c, a)) != QI(canon(c, b)) && is_integer(c, a) && is_integer(c, b) && KIND(c, a) != TY_BOOL &&
	    KIND(c, b) != TY_BOOL && !compatible(c, a, b)) {
		static const char *const gs[] = { "pedantic", NULL };
		bool ch = (KIND(c, a) == TY_CHAR) != (KIND(c, b) == TY_CHAR);

		conv_diag(c, n, to, "", ch ? " converts between pointers to integer types where one is of the unique plain 'char' type "
				  "and the other is not" : " converts between pointers to integer types with different sign",
			  "ext_typecheck_convert_incompatible_pointer_sign", "pointer-sign", gs, true);
	}
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

/* clang's DiagRuntimeBehavior: nothing in unevaluated operands and constant expressions, and in a
   function nothing in code its CFG finds unreachable (at file scope a dead ?: branch still warns) */
VPRIV bool quiet_rt(const cc *c)
{
	return c->unevaluated || c->cexpr || (c->dead && c->fn);
}

VPRIV void i128s_(char *b, size_t n, i128 v)
{
	char t[48];
	int k = 0;
	u128 u = v < 0 ? (u128)0 - (u128)v : (u128)v;

	do
		t[k++] = (char)('0' + (int)(u % 10)), u /= 10;
	while (u);
	if (v < 0)
		t[k++] = '-';
	for (int i = 0; i < k && (size_t)i + 1 < n; i++)
		b[i] = t[k - 1 - i];
	b[(size_t)k < n ? (size_t)k : n - 1] = 0;
}

/* clang's -Wconstant-conversion: an integer constant the narrower target type cannot
   hold.  clang's ranges: unsigned w bits holds -2^(w-1) .. 2^w-1 (the bits fit); signed
   its signed range, or up to 2^w-1 from an unsigned source; plain char -128 .. 255 */
static bool hex_literal(cc *c, uint32_t n);

static __attribute__((cold, noinline)) void const_conv(cc *c, uint32_t n, qty to)
{
	const node *x = N(n);
	qty from;
	cval v;
	uint64_t w = ty_size(c, to) * 8;
	i128 lo, hi, nv;
	char a[48], b[48], los[48], his[48];
	pp_span r;
	vdiag *d;

	if (quiet_rt(c))	/* clang: DiagRuntimeBehavior */
		return;
	while (x->op == N_CAST && !c->span[n].e)	/* implicit conversions */
		n = x->a, x = N(n);
	if (x->op == N_COND && x->b) {	/* clang: each branch, the taken one of a constant condition */
		cval k;
		bool known = c->fn && cc_eval(c, x->a, &k) && k.kind == CV_INT;

		if (!known || k.i)
			const_conv(c, x->b, to);
		if (!known || !k.i)
			const_conv(c, x->c, to);
		return;
	}
	from = x->ty;
	if (!is_integer(c, from) || KIND(c, from) == TY_BOOL || ty_size(c, to) >= ty_size(c, from) || w > 64 ||
	    !cc_eval(c, n, &v) || v.kind != CV_INT)
		return;
	if (w == 8 && c->ibr)	/* clang: byte tables in braces (plain char: its target's sign) */
		lo = -128, hi = 255;
	else if (!is_signed(c, to))
		lo = -((i128)1 << (w - 1)), hi = ((i128)1 << w) - 1;
	else
		lo = -((i128)1 << (w - 1)), hi = is_signed(c, from) ? ((i128)1 << (w - 1)) - 1 : ((i128)1 << w) - 1;
	if (v.i >= lo && v.i <= hi)
		return;
	if (v.i >= 0 && v.i <= ((i128)1 << w) - 1 && hex_literal(c, n))	/* clang: 0xfe into a char */
		return;
	nv = trunc_to(c, v.i, to);
	r = rng(c, n);
	i128s_(a, sizeof a, v.i), i128s_(b, sizeof b, nv);
	i128s_(los, sizeof los, is_signed(c, to) ? -((i128)1 << (w - 1)) : 0);
	i128s_(his, sizeof his, is_signed(c, to) ? ((i128)1 << (w - 1)) - 1 : ((i128)1 << w) - 1);
	d = cc_warn_begin(c, "constant-conversion", true, NULL, x->loc != UINT32_MAX ? x->loc : r.b, "implicit conversion from '%s' to '%s' changes value from %s to %s",
			  tyname(c, from), tyname(c, QUNQ(to)), a, b);
	if (!d)
		return;
	if (c->cv_what && !c->ibr && c->cv_loc)	/* clang: the context's token too (name, '=', callee, 'return') */
		pp_diag_label(c->p, d, c->cv_loc, pp_tok_end(c->p, c->cv_loc), NULL);
	pp_diag_label(c->p, d, r.b, r.e, NULL);
	pp_diag_macro(d, a, (uint32_t)strlen(a));	/* clang: "expanded from macro" at the constant */
	pp_diag_help(c->p, d, "'%s' holds %s to %s%s: cast the value if the wrap to %s is intended", tyname(c, QUNQ(to)), los, his,
		     char_sign(c, to), b);
	rt_emit(c, d);	/* (clang: DiagRuntimeBehavior) */
}

/* -Wconstant-conversion for a conversion of n to the integer type to (gated: narrowing only) */
VPRIV void const_conv_check(cc *c, uint32_t n, qty to)
{
	qty from = N(n)->ty;

	if (unlikely(is_integer(c, to) && KIND(c, to) != TY_BOOL && is_integer(c, from) && ty_size(c, to) < ty_size(c, from)))
		const_conv(c, n, to);
}

/* clang's -Wbitfield-constant-conversion: an integer constant stored into bit-field f that
   does not survive the truncation.  Unsigned w bits hold -2^(w-1) .. 2^w-1, signed ones
   their signed range; explicit casts count too (the truncation itself is implicit).  at:
   where clang reports it (an assignment's '='), else UINT32_MAX for the constant */
VPRIV __attribute__((noinline)) void bf_const_conv(cc *c, uint32_t n, const field *f, uint32_t at)
{
	const node *x = N(n);
	uint64_t w = f->width;
	qty from;
	cval v;
	i128 lo, hi, nv;
	bool sg;
	char a[48], b[48], los[48], his[48];
	pp_span r;
	vdiag *d;

	if (quiet_rt(c))
		return;
	while (x->op == N_CAST && !c->span[n].e)
		n = x->a, x = N(n);
	if (x->op == N_COND && x->b) {	/* clang: each branch, the taken one of a constant condition */
		cval k;
		bool known = c->fn && cc_eval(c, x->a, &k) && k.kind == CV_INT;

		if (!known || k.i)
			bf_const_conv(c, x->b, f, at);
		if (!known || !k.i)
			bf_const_conv(c, x->c, f, at);
		return;
	}
	from = x->ty;
	if (!w || w > 64 || !is_integer(c, from) || KIND(c, from) == TY_BOOL || KIND(c, f->ty) == TY_BOOL ||
	    w >= ty_size(c, from) * 8 || !cc_eval(c, n, &v) || v.kind != CV_INT)
		return;
	sg = is_signed(c, f->ty);
	lo = -((i128)1 << (w - 1));
	hi = sg ? ((i128)1 << (w - 1)) - 1 : ((i128)1 << w) - 1;
	if (v.i >= lo && v.i <= hi)
		return;
	nv = v.i & (((i128)1 << w) - 1);
	if (sg && nv > ((i128)1 << (w - 1)) - 1)
		nv -= (i128)1 << w;
	r = rng(c, n);
	i128s_(a, sizeof a, v.i), i128s_(b, sizeof b, nv);
	if (at == UINT32_MAX)
		at = x->loc != UINT32_MAX ? x->loc : r.b;
	if (sg && w == 1 && v.i == 1)
		d = cc_warn_begin(c, "single-bit-bitfield-constant-conversion", true, NULL, at,
				  "implicit truncation from '%s' to a one-bit wide bit-field changes value from %s to %s", tyname(c, from), a, b);
	else
		d = cc_warn_begin(c, "bitfield-constant-conversion", true, NULL, at,
				  "implicit truncation from '%s' to bit-field changes value from %s to %s", tyname(c, from), a, b);
	if (!d)
		return;
	pp_diag_label(c->p, d, r.b, r.e, NULL);
	pp_diag_macro(d, a, (uint32_t)strlen(a));
	i128s_(los, sizeof los, sg ? lo : 0), i128s_(his, sizeof his, hi);
	if (sg && w == 1)
		pp_diag_help(c->p, d, "a signed 1-bit bit-field holds only 0 and -1: declare it 'unsigned'");
	else
	{
		int nl = 0;
		const char *nm = f->name ? idname(f->name, &nl) : "";

		pp_diag_help(c->p, d, "the %s %u-bit bit-field '%.*s' holds %s to %s", sg ? "signed" : "unsigned", (unsigned)w, nl, nm,
			     los, his);
	}
	pp_diag_emit(c->p, d);
}

/* clang's implicit-conversion errors (int-conversion, incompatible pointer types) */
static uint32_t conv_check(cc *c, uint32_t n, qty to)
{
	qty from = N(n)->ty;

	if (N(n)->flags & NF_ERR)
		return n;
	if (is_ptr(c, to) && (is_integer(c, from) || KIND(c, from) == TY_ENUM) && !is_npc(c, n) && KIND(c, from) != TY_BOOL) {
		int_conv_error(c, n, to, true);
	} else if ((is_integer(c, to) || KIND(c, to) == TY_ENUM) && KIND(c, to) != TY_BOOL && is_ptr(c, from)) {
		int_conv_error(c, n, to, false);
	} else if (is_ptr(c, to) && is_ptr(c, from) && !(T(c, to)->flags & TF_BLOCK) && !(T(c, from)->flags & TF_BLOCK) &&
		   !ptr_ok(c, T(c, to)->base, T(c, from)->base)) {
		if (is_func(c, T(c, to)->base) && is_func(c, T(c, from)->base))
			conv_error(c, n, to, "incompatible function pointer types", "ext_typecheck_convert_incompatible_function_pointer",
				   "incompatible-function-pointer-types");
		else	/* (clang 23: an error by default) */
			conv_error(c, n, to, "incompatible pointer types", "ext_typecheck_convert_incompatible_pointer",
				   "incompatible-pointer-types");
	} else if (is_ptr(c, to) && is_ptr(c, from) && QI(canon(c, to)) != QI(canon(c, from)) &&
		   !(T(c, to)->flags & TF_BLOCK) && !(T(c, from)->flags & TF_BLOCK)) {
		ptr_quals(c, n, to);
		return n;
	} else {
		const_conv_check(c, n, to);
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
/* n is an integer literal written in hex, octal or binary (parentheses are no nodes) */
static bool hex_literal(cc *c, uint32_t n)
{
	return N(n)->op == N_NUM && N(n)->aux == NUM_RADIX;
}

/* plain 'char' in a range help: its sign is the target's, so the warning may be one target's only */
VPRIV const char *char_sign(cc *c, qty t)
{
	if (KIND(c, t) != TY_CHAR)
		return "";
	return c->o->target == PP_DARWIN ? " (plain 'char' is signed on Apple and x86, unsigned on Arm and RISC-V Linux)"
					 : " (plain 'char' is unsigned on Arm and RISC-V Linux, signed on Apple and x86)";
}

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
		vdiag *d;

		if (i < 0)
			d = cc_warn_begin(c, "array-bounds", true, NULL, r[0].b,
					  "array index %lld is before the beginning of the array", (long long)i);
		else
			d = cc_warn_begin(c, "array-bounds", true, NULL, r[0].b,
					  "array index %lld is past the end of the array (that has type '%s')", (long long)i,
					  arr_tyname(c, N(a)->ty));
		if (!d)
			return;
		pp_diag_label(c->p, d, r[1].b, r[1].e, NULL);
		if (s) {
			int m;
			const char *nm = idname(c->syms[s].name, &m);

			pp_diag_note(c->p, d, c->syms[s].ple ? c->syms[s].plb : c->syms[s].loc, "array '%.*s' declared here", m, nm);
		}
		rt_emit(c, d);	/* (clang: DiagRuntimeBehavior) */
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
	pp_span r;

	if (likely((N(n)->flags & NF_LVALUE) && !(QQ(N(n)->ty) & Q_CONST)))
		return true;	/* the common case, before the range (pp_tok_end) */
	r = rng(c, n);
	if (!(N(n)->flags & NF_LVALUE)) {
		if (KIND(c, N(n)->ty) == TY_FUNC)
			report(c, "error", loc, &r, 1, NULL, 0, "non-object type '%s' is not assignable", tyname(c, N(n)->ty));
		else
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
