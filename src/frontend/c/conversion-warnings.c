/* vcc front end: the errors and warnings of implicit conversions, in clang's words */
#include "ccimpl.h"

static void param_note(cc *c);

/* clang's message for a value of type from that cannot become to */
VPRIV void bad_conversion(cc *c, uint32_t n, qty to)
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

/* the source type clang names: an array or function before its decay */
static qty conv_from(cc *c, uint32_t n)
{
	if (N(n)->op == N_CAST && (N(n)->aux == CK_DECAY || N(n)->aux == CK_FDECAY))
		return N(N(n)->a)->ty;
	return N(n)->ty;
}

/* "... initializing 'T' with an expression of type 'F'" and the like, per context */
static void conv_error(cc *c, uint32_t n, qty to, const char *what, const char *flag, const char *id)
{
	pp_span r = rng(c, n);
	const char *f = arr_tyname(c, conv_from(c, n)), *t = tyname(c, to);
	uint32_t loc = c->cv_what == CX_PASS || c->cv_what == CX_RETURN ? r.b : c->cv_loc;
	int bit = !strcmp(flag, "-Wint-conversion") ? 1 : !strcmp(flag, "-Wincompatible-pointer-types") ? 2 : 4;
	char msg[1024];
	qty ft = N(n)->ty;
	int op = N(n)->op;
	/* clang: "; dereference with *" when *n has the target type (a fix-it; vcc: simple operands only) */
	bool dr = bit != 4 && is_ptr(c, ft) && compatible(c, QUNQ(T(c, ft)->base), QUNQ(to));
	/* a simple or parenthesized operand takes '*', another '*(' ... ')' */
	bool wrap = dr && !(op == N_VAR || op == N_MEMBER || op == N_CALL || op == N_INDEX || (N(n)->flags & NF_PAREN) ||
			    (c->paren[n].l | c->paren[n].r));
	/* "; take the address with &" when &n has the target type (an lvalue; vcc: simple operands) */
	bool ad = !dr && bit != 4 && is_ptr(c, to) && compatible(c, QUNQ(ft), QUNQ(T(c, to)->base)) &&
		  (op == N_VAR || op == N_MEMBER || op == N_INDEX) && (N(n)->flags & NF_LVALUE) &&
		  !(N(n)->flags & NF_BITFIELD) && !(op == N_VAR && c->syms[N(n)->a].sc == SC_REGISTER);
	pp_fixit fx[2] = { { r.b, r.b, wrap ? "*(" : dr ? "*" : "&", VDA_MAYBE }, { r.e, r.e, ")", VDA_MAYBE } };
	const char *h = dr ? "; dereference with *" : ad ? "; take the address with &" : "";
	int nfx = wrap ? 2 : dr || ad;
	char fl[64];

	if (bit == 1 && dr) {	/* clang's tryToFixConversion: "; remove &" before "; dereference with *" */
		uint32_t m = n;

		while (N(m)->op == N_CAST && !c->span[m].e)
			m = N(m)->a;
		if (!is_ptr(c, to) && N(m)->op == N_ADDR && compatible(c, QUNQ(N(N(m)->a)->ty), QUNQ(to)))
			h = "; remove &", fx[0] = (pp_fixit){ rng_np(c, m).b, rng_np(c, m).b + 1, "", VDA_MAYBE }, nfx = 1;
	}
	snprintf(fl, sizeof fl, "%s [%s", h, flag);

	switch (c->cv_what) {
	case CX_INIT:
		snprintf(msg, sizeof msg, "%s initializing '%s' with an expression of type '%s'%s]", what, t, f, fl);
		break;
	case CX_PASS:
		snprintf(msg, sizeof msg, "%s passing '%s' to parameter of type '%s'%s]", what, f, t, fl);
		break;
	case CX_RETURN:
		snprintf(msg, sizeof msg, "%s returning '%s' from a function with result type '%s'%s]", what, f, t, fl);
		break;
	default:
		snprintf(msg, sizeof msg, "%s assigning to '%s' from '%s'%s]", what, t, f, fl);
	}
	static const char *const ipt[] = { "incompatible-pointer-types", NULL };

	if (c->o->conv_warn & bit) {
		/* -Wno-error=...: clang's warning (the message without its flag, which pp_warnx adds) */
		char *b = strrchr(msg, '[');

		if (pp_werror_named(c->p, flag + 2, bit == 4 ? ipt : NULL) == 1)
			goto err;	/* -Werror= again: clang's own error (no "-Werror,") */
		if (b && b > msg)
			b[-1] = 0;
		pp_diag_next_id = id;
		if (cc_warnx(c, flag + 2, true, bit == 4 ? ipt : NULL, loc, &r, 1, fx, nfx, "%s", msg) && c->cv_what == CX_PASS)
			param_note(c);
		pp_diag_next_id = NULL;
		return;
	}
err:
	pp_diag_next_id = id;
	report(c, "error", loc, &r, 1, fx, nfx, "%s", msg);
	pp_diag_next_id = NULL;
	c->nconv++;
	if (c->cv_what == CX_PASS)
		param_note(c);
}

/* pointee types that clang's -Wincompatible-pointer-types accepts: compatible once
   qualifiers (at every level) and integer signedness are ignored, or one is void */
static bool ptr_ok_(cc *c, qty a, qty b, bool top)
{
	int ka, kb;

	a = QUNQ(a), b = QUNQ(b);
	ka = KIND(c, a), kb = KIND(c, b);
	if (compatible(c, a, b) || (top && (ka == TY_VOID || kb == TY_VOID)))
		return true;
	if ((T(c, a)->flags ^ T(c, b)->flags) & TF_ATOMIC)
		return false;
	if (ka == TY_PTR && kb == TY_PTR)
		return ptr_ok_(c, T(c, a)->base, T(c, b)->base, false);
	if (top && (is_integer(c, a) || ka == TY_ENUM) && (is_integer(c, b) || kb == TY_ENUM))
		return ty_size(c, a) == ty_size(c, b);	/* -Wpointer-sign (a warning; only the pointee itself) */
	if (ka == TY_ARRAY && kb == TY_ARRAY)
		return ptr_ok_(c, T(c, a)->base, T(c, b)->base, false);
	return false;
}

static bool ptr_ok(cc *c, qty a, qty b) { return ptr_ok_(c, a, b, true); }

/* -Wincompatible-pointer-types-discards-qualifiers: the pointee loses const/volatile/restrict */
/* const char ** from char **: the levels below the pointee differ in qualifiers (clang's
   IncompatibleNestedPointerQualifiers: the types otherwise the same) */
static bool nested_quals(cc *c, qty a, qty b)
{
	bool d = false;

	while (is_ptr(c, a) && is_ptr(c, b)) {
		a = T(c, a)->base, b = T(c, b)->base;
		d |= (QQ(a) & Q_MASK) != (QQ(b) & Q_MASK);
	}
	return d && compatible(c, QUNQ(a), QUNQ(b));
}

/* a warning about the implicit conversion of n to to, in clang's words for the context */
static void conv_warn(cc *c, uint32_t n, qty to, const char *flag, const char *const *g, const char *what)
{
	pp_span r = rng(c, n);
	const char *f = arr_tyname(c, conv_from(c, n)), *t = tyname(c, to);
	uint32_t loc = c->cv_what == CX_PASS || c->cv_what == CX_RETURN ? r.b : c->cv_loc;
	bool on;

	switch (c->cv_what) {
	case CX_INIT:
		on = cc_warnx(c, flag, true, g, loc, &r, 1, NULL, 0, "initializing '%s' with an expression of type '%s' %s", t, f, what);
		break;
	case CX_PASS:
		on = cc_warnx(c, flag, true, g, loc, &r, 1, NULL, 0, "passing '%s' to parameter of type '%s' %s", f, t, what);
		break;
	case CX_RETURN:
		on = cc_warnx(c, flag, true, g, loc, &r, 1, NULL, 0, "returning '%s' from a function with result type '%s' %s", f, t, what);
		break;
	default:
		on = cc_warnx(c, flag, true, g, loc, &r, 1, NULL, 0, "assigning to '%s' from '%s' %s", t, f, what);
	}
	if (on && c->cv_what == CX_PASS)
		param_note(c);
}

static void discards(cc *c, uint32_t n, qty to, bool nested)
{
	static const char *const g[] = { "incompatible-pointer-types", NULL };

	conv_warn(c, n, to, "incompatible-pointer-types-discards-qualifiers", g,
		  nested ? "discards qualifiers in nested pointer types" : "discards qualifiers");
}

/* clang's implicit-conversion errors (int-conversion, incompatible pointer types) */
VPRIV uint32_t conv_check(cc *c, uint32_t n, qty to)
{
	qty from = N(n)->ty;

	if (N(n)->flags & NF_ERR)
		return n;
	if (is_ptr(c, to) && (is_integer(c, from) || KIND(c, from) == TY_ENUM) && !is_npc(c, n) && KIND(c, from) != TY_BOOL) {
		conv_error(c, n, to, "incompatible integer to pointer conversion", "-Wint-conversion", "ext_typecheck_convert_int_pointer");
	} else if ((is_integer(c, to) || KIND(c, to) == TY_ENUM) && KIND(c, to) != TY_BOOL && is_ptr(c, from)) {
		conv_error(c, n, to, "incompatible pointer to integer conversion", "-Wint-conversion", "ext_typecheck_convert_pointer_int");
	} else if (is_ptr(c, to) && is_ptr(c, from) && !(T(c, to)->flags & TF_BLOCK) && !(T(c, from)->flags & TF_BLOCK) &&
		   !ptr_ok(c, T(c, to)->base, T(c, from)->base)) {
		if (is_func(c, T(c, to)->base) && is_func(c, T(c, from)->base))
			conv_error(c, n, to, "incompatible function pointer types", "-Wincompatible-function-pointer-types", "ext_typecheck_convert_incompatible_function_pointer");
		else
			conv_error(c, n, to, "incompatible pointer types", "-Wincompatible-pointer-types", "ext_typecheck_convert_incompatible_pointer");
	} else {
		if (is_ptr(c, to) && is_ptr(c, from) && !is_func(c, T(c, to)->base) && !is_func(c, T(c, from)->base) &&
		    (QQ(T(c, from)->base) & ~QQ(T(c, to)->base) & Q_MASK))
			discards(c, n, to, false);
		else if (is_ptr(c, to) && is_ptr(c, from) && nested_quals(c, T(c, to)->base, T(c, from)->base))
			discards(c, n, to, true);
		else if (is_ptr(c, to) && is_ptr(c, from) && is_integer(c, T(c, to)->base) && is_integer(c, T(c, from)->base) &&
			 !compatible(c, QUNQ(T(c, to)->base), QUNQ(T(c, from)->base))) {
			static const char *const g[] = { "pointer-sign", NULL };

			conv_warn(c, n, to, "pointer-sign", g, "converts between pointers to integer types with different sign");
		}
		return n;
	}
	N(n)->flags |= NF_ERR;	/* no second message from convert */
	return n;
}

/* the bits of v as clang's IntRange of a constant: width, non-negative */
static int crange(i128 v, bool *nn)
{
	int w = 0;

	*nn = v >= 0;
	if (v < 0)
		v = ~v, w = 1;
	while (v)
		w++, v >>= 1;
	return w;
}

/* APFloat::toString(prec, padding 3, truncate zeros) of v */
static void apf_str(char *o, double v, int prec)
{
	char b[64], dg[32];
	int nd = 0, e10, k = 0, exp;

	if (v < 0)
		o[k++] = '-', v = -v;
	snprintf(b, sizeof b, "%.*e", prec - 1, v);
	for (const char *q = b; *q && *q != 'e'; q++)
		if (*q >= '0' && *q <= '9')
			dg[nd++] = *q;
	e10 = atoi(strchr(b, 'e') + 1);
	while (nd > 1 && dg[nd - 1] == '0')
		nd--;
	exp = e10 - (nd - 1);	/* value = digits * 10^exp */
	{
		bool sci;
		int msd = exp + nd - 1;

		if (exp >= 0)
			sci = exp > 3 || nd + exp > prec;
		else
			sci = msd < 0 && -msd > 3;
		if (sci) {
			o[k++] = dg[0], o[k++] = '.';
			if (nd == 1)
				o[k++] = '0';
			for (int i = 1; i < nd; i++)
				o[k++] = dg[i];
			k += sprintf(o + k, "E%c%d", msd >= 0 ? '+' : '-', msd < 0 ? -msd : msd);
		} else if (exp >= 0) {
			for (int i = 0; i < nd; i++)
				o[k++] = dg[i];
			for (int i = 0; i < exp; i++)
				o[k++] = '0';
		} else if (nd + exp > 0) {
			for (int i = 0; i < nd; i++) {
				if (i == nd + exp)
					o[k++] = '.';
				o[k++] = dg[i];
			}
		} else {
			o[k++] = '0', o[k++] = '.';
			for (int i = 0; i < -(nd + exp); i++)
				o[k++] = '0';
			for (int i = 0; i < nd; i++)
				o[k++] = dg[i];
		}
	}
	o[k] = 0;
}

/* -Wliteral-conversion: a floating literal (or its negation) converted to an integer type */
static void literal_conv(cc *c, uint32_t n, qty to)
{
	uint32_t e = n, lit;
	double v, t;
	char vs[64], ts[48];
	const char *what = NULL;

	while (N(e)->op == N_CAST && !c->span[e].e && N(e)->a)
		e = N(e)->a;
	lit = N(e)->op == N_UNARY && N(e)->aux == LEX_TOKEN_MINUS ? N(e)->a : e;
	if (N(lit)->op != N_FNUM || is_q(c, N(lit)->ty) || ty_size(c, N(lit)->ty) > 8)
		return;
	v = c->fpool[N(lit)->a];
	if (lit != e)
		v = -v;
	t = trunc(v);
	if (KIND(c, to) == TY_BOOL) {
		if (v == 0 || v == 1)
			return;
		snprintf(ts, sizeof ts, "%s", v != 0 ? "true" : "false");
	} else {
		int w = (int)int_bits(c, to);
		bool sg = is_signed(c, to);
		double lo = sg ? -ldexp(1, w - 1) : 0, hi = sg ? ldexp(1, w - 1) : ldexp(1, w);

		if (!(t >= lo && t < hi))
			what = "out of range";
		else if (t == v)
			return;
		else
			snprintf(ts, sizeof ts, "%.0f", t == 0 ? 0.0 : t);
	}
	{
		pp_span r[2] = { { c->cv_what == CX_PASS ? c->cv_callee : c->cv_loc, 0 },
				 { N(e)->loc, pp_tok_end(c->p, N(lit)->loc) } };

		r[0].e = pp_tok_end(c->p, r[0].b);
		if (what) {
			cc_warnx(c, "literal-conversion", true, w_conv, r[1].b, r, 2, NULL, 0,
				 "implicit conversion of out of range value from '%s' to '%s' is undefined",
				 tyname(c, N(lit)->ty), tyname(c, to));
			return;
		}
		apf_str(vs, v, ty_size(c, N(lit)->ty) == 4 ? 8 : 16);
		cc_warnx(c, "literal-conversion", true, w_conv, r[1].b, r, 2, NULL, 0,
			 "implicit conversion from '%s' to '%s' changes value from %s to %s", tyname(c, N(lit)->ty),
			 tyname(c, to), vs, ts);
	}
}

/* -Wconstant-conversion: integer constant n converted to the narrower integer to */

/* n's value depends on a __builtin_(dynamic_)object_size whose object vcc does not
   know (folded to (size_t)-1 or 0, which clang leaves to code generation) */
static bool objsize_unknown(cc *c, uint32_t n, int depth)
{
	const node *x = N(n);
	i128 sz;
	cval mode;

	if (!n || depth > 32)
		return false;
	switch (x->op) {
	case N_BUILTIN:
		if (x->aux != BI_OBJECT_SIZE && x->aux != BI_DYNAMIC_OBJECT_SIZE)
			return false;
		return x->c != 2 || !cc_eval(c, c->lists[x->b + 1], &mode) || mode.kind != CV_INT ||
		       !cc_object_size(c, c->lists[x->b], (unsigned)mode.i, &sz);
	case N_CAST: case N_UNARY:
		return objsize_unknown(c, x->a, depth + 1);
	case N_BINARY:
		return objsize_unknown(c, x->a, depth + 1) || objsize_unknown(c, x->b, depth + 1);
	case N_COND:
		return objsize_unknown(c, x->a, depth + 1) || objsize_unknown(c, x->b, depth + 1) ||
		       objsize_unknown(c, x->c, depth + 1);
	default:
		return false;
	}
}

VPRIV void const_conv(cc *c, uint32_t n, qty to)
{
	qty from = N(n)->ty;
	cval cv;
	int tw, sw, lw;
	bool tnn, lnn, deferred;
	i128 v, t;

	if (N(n)->op == N_COND && N(n)->b && N(n)->c) {	/* clang: each arm (the taken one), in its own type */
		cval cc0;
		int k0 = 0, k1 = 2;

		if (cc_eval(c, N(n)->a, &cc0) && cc0.kind == CV_INT)
			k0 = cc0.i == 0, k1 = k0 + 1;
		for (int k = k0; k < k1; k++) {
			uint32_t e = k ? N(n)->c : N(n)->b;

			while (N(e)->op == N_CAST && !c->span[e].e && N(e)->a)
				e = N(e)->a;
			const_conv(c, e, to);
		}
		return;
	}
	if (N(n)->op == N_COMMA) {	/* clang: the right operand, at the comma */
		uint32_t sl = c->cv_loc;
		uint8_t sw = c->cv_what;

		c->cv_loc = N(n)->loc, c->cv_what = CX_INIT;
		const_conv(c, N(n)->b, to);
		c->cv_loc = sl, c->cv_what = sw;
		return;
	}
	if (is_integer(c, to) && is_float(c, from) && !c->unevaluated) {	/* (immediate: also in dead code) */
		literal_conv(c, n, to);
		return;
	}
	if (!is_integer(c, to) || !is_integer(c, from) || KIND(c, to) == TY_BOOL || KIND(c, from) == TY_BOOL ||
	    c->unevaluated || !cc_eval(c, n, &cv) || cv.kind != CV_INT)
		return;
	tw = (int)int_bits(c, to), sw = (int)int_bits(c, from);
	if (tw >= sw || tw > 64)
		return;
	/* clang: __builtin_object_size of an unknown object is no constant here (its
	   evaluator leaves it to code generation): no -Wconstant-conversion for the
	   (size_t)-1 or what arithmetic makes of it, nouveau's __member_size(p) /
	   sizeof(*p); a known size warns as any constant */
	if (objsize_unknown(c, n, 0))
		return;
	tnn = !is_signed(c, to);
	v = cv.i;
	if (!is_signed(c, from) && sw <= 64)
		v = (i128)((uint64_t)v & (sw == 64 ? ~0ull : (1ull << sw) - 1));
	lw = crange(v, &lnn);
	if (lw > tw && c->dead)	/* clang's DiagRuntimeBehavior: not in unreachable code */
		return;
	if (lw > tw)	/* (crange counts a negative range's sign bit) */
		deferred = true;
	else if (lw == tw && !tnn && lnn && is_signed(c, from)) {
		/* clang's isSameWidthConstantConversion: not a hex/octal literal, not a char in braces */
		uint32_t e = n;

		while (N(e)->op == N_CAST && !c->span[e].e && N(e)->a)
			e = N(e)->a;
		uint32_t b = rng(c, e).b;
		int ch;

		while ((ch = pp_loc_char(c->p, b)) == '(' || ch == ' ' || ch == '\t')
			b++;
		if ((N(e)->op == N_NUM ? (N(e)->aux & NUM_RADIX) : ch == '0') ||
		    ((KIND(c, to) == TY_CHAR || KIND(c, to) == TY_SCHAR) && pp_loc_char(c->p, c->cv_loc) == '{'))
			return;
		deferred = false;
	}
	else
		return;
	t = (i128)((uint64_t)v & (tw == 64 ? ~0ull : (1ull << tw) - 1));
	if (!tnn && (t >> (tw - 1)) & 1)
		t -= (i128)1 << tw;
	if (t == v)
		return;
	{
		uint32_t cl = c->cv_what == CX_PASS ? c->cv_callee : c->cv_loc;
		pp_span r[2] = { { cl, pp_tok_end(c->p, cl) }, rng_np(c, n) };
		uint32_t at = r[1].b;
		char msg[112];

		while (pp_loc_char(c->p, at) == '(' || pp_loc_char(c->p, at) == ' ')	/* clang: (v) at v */
			at++;

		snprintf(msg, sizeof msg, "implicit conversion from '%s' to '%s' changes value from %lld to %lld",
			 tyname(c, from), tyname(c, to), (long long)v, (long long)t);
		char vs[48];

		snprintf(vs, sizeof vs, "%lld", (long long)v);	/* (clang: "expanded from macro" at the constant) */
		if (deferred && c->fn && !c->defer_rt) {
			pp_diag_next_id = "warn_impcast_integer_precision_constant";
			rt_defer(c, "constant-conversion", w_conv, at, r, 2, vs, (uint32_t)strlen(vs), "%s", msg);
			pp_diag_next_id = NULL;
			return;
		}
		{
			vdiag *d;

			pp_diag_next_id = "warn_impcast_integer_precision_constant";
			d = cc_warn_begin(c, "constant-conversion", true, w_conv, at, "%s", msg);
			pp_diag_next_id = NULL;

			if (d) {
				pp_diag_label(c->p, d, r[0].b, r[0].e, NULL);
				pp_diag_label(c->p, d, r[1].b, r[1].e, NULL);
				pp_diag_macro(d, vs, (uint32_t)strlen(vs));
				pp_diag_emit(c->p, d);
			}
		}
	}
}

/* -Wbitfield-constant-conversion: constant v stored in bit-field f; at the
   operator op (an assignment), or at v (0: an initializer) */
VPRIV void bitfield_const(cc *c, const field *f, uint32_t v, uint32_t op)
{
	static const char *const g[] = { "bitfield-constant-conversion", "constant-conversion", "conversion", NULL };
	cval cv;
	int w = f->width;
	bool sg = is_signed(c, f->ty);
	i128 x, t;

	if (KIND(c, f->ty) == TY_BOOL || !is_integer(c, N(v)->ty) || KIND(c, N(v)->ty) == TY_BOOL || w <= 0 || w > 64 ||
	    c->unevaluated || !cc_eval(c, v, &cv) || cv.kind != CV_INT)
		return;
	x = cv.i;
	if (!is_signed(c, N(v)->ty) && ty_size(c, N(v)->ty) <= 8)
		x = (i128)((uint64_t)x & (ty_size(c, N(v)->ty) == 8 ? ~0ull : (1ull << ty_size(c, N(v)->ty) * 8) - 1));
	t = (i128)((uint64_t)x & (w == 64 ? ~0ull : (1ull << w) - 1));
	if (sg && (t >> (w - 1)) & 1)
		t -= (i128)1 << w;
	if (t == x || (!sg && x < 0 && x >= -((i128)1 << (w - 1))))
		return;
	{
		pp_span r[1] = { rng(c, v) };
		uint32_t at = op ? op : r[0].b;
		bool one = sg && w == 1 && x == 1;

		pp_dinfo d = { "warning", at, op ? r : NULL, op ? 1 : 0, NULL, 0, op ? "=" : NULL, op ? 1 : 0 };

		pp_warnx(c->p, one ? "single-bit-bitfield-constant-conversion" : "bitfield-constant-conversion", true,
			 one ? g : g + 1, &d, "implicit truncation from '%s' to %s changes value from %lld to %lld",
			 tyname(c, N(v)->ty), one ? "a one-bit wide bit-field" : "bit-field", (long long)x, (long long)t);
	}
}


/* clang spells the array type of this warning without the space: 'int[4]' */
VPRIV const char *arr_tyname(cc *c, qty t)
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

/* plain 'char' in a range help: its sign is the target's, so the warning may be one target's only */
VPRIV const char *char_sign(cc *c, qty t)
{
	if (KIND(c, t) != TY_CHAR)
		return "";
	return c->o->target == PP_DARWIN ? " (plain 'char' is signed on Apple and x86, unsigned on Arm and RISC-V Linux)"
					 : " (plain 'char' is unsigned on Arm and RISC-V Linux, signed on Apple and x86)";
}

