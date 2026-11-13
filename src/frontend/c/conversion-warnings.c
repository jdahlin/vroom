/* vcc front end: the errors and warnings of implicit conversions, in clang's words */
#include "cc-internal.h"

static void cc_conversion_parameter_note(cc *c);

/* clang's message for a value of type from that cannot become to */
CC_PRIVATE void cc_conversion_invalid_report(cc *c, uint32_t n, ast_qualified_type to)
{
	diag_span r = cc_node_range(c, n);
	const char *f = tyname(c, AST_NODE(n)->ty), *t = tyname(c, to);

	if (AST_NODE(n)->flags & AST_NODE_FLAG_ERROR)
		return;
	if (c->cv_what == CC_CONVERSION_CONTEXT_ARGUMENT) {
		report(c, "error", r.b, &r, 1, NULL, 0, "passing '%s' to parameter of incompatible type '%s'", f, t);
		cc_conversion_parameter_note(c);
		return;
	}
	switch (c->cv_what) {
	case CC_CONVERSION_CONTEXT_INITIALIZER:
		report(c, "error", c->cv_loc, &r, 1, NULL, 0, "initializing '%s' with an expression of incompatible type '%s'", t, f);
		break;
	case CC_CONVERSION_CONTEXT_ARGUMENT:
		report(c, "error", r.b, NULL, 0, NULL, 0, "passing '%s' to parameter of incompatible type '%s'", f, t);
		break;
	case CC_CONVERSION_CONTEXT_RETURN:
		report(c, "error", r.b, &r, 1, NULL, 0, "returning '%s' from a function with incompatible result type '%s'", f, t);
		break;
	case CC_CONVERSION_CONTEXT_ASSIGNMENT:
		report(c, "error", c->cv_loc, &r, 1, NULL, 0, "assigning to '%s' from incompatible type '%s'", t, f);
		break;
	default:
		report(c, "error", r.b, &r, 1, NULL, 0, "incompatible types in conversion ('%s' to '%s')", f, t);
	}
}

/* "passing argument to parameter 'p' here" for the argument being converted */
static void cc_conversion_parameter_note(cc *c)
{
	const sym *f;
	uint32_t p;

	if (!c->cv_fn || !(f = &c->syms[c->cv_fn])->psyms)
		return;
	p = c->lists[f->psyms - 1 + c->cv_arg];
	if (c->syms[p].name)
		cc_report_note(c, c->syms[p].loc, "passing argument to parameter '%.*s' here", CC_IDENTIFIER(c->syms[p].name));
	else
		cc_report_note(c, c->syms[p].loc, "passing argument to parameter here");
}

/* the source type clang names: an array or function before its decay */
static ast_qualified_type cc_conversion_source_type(cc *c, uint32_t n)
{
	if (AST_NODE(n)->op == AST_NODE_CAST && (AST_NODE(n)->aux == AST_CAST_ARRAY_DECAY || AST_NODE(n)->aux == AST_CAST_FUNCTION_DECAY))
		return AST_NODE(AST_NODE(n)->a)->ty;
	return AST_NODE(n)->ty;
}

/* "... initializing 'AST_TYPE' with an expression of type 'F'" and the like, per context */
static void cc_conversion_error_report(cc *c, uint32_t n, ast_qualified_type to, const char *what, const char *flag, const char *id)
{
	diag_span r = cc_node_range(c, n);
	const char *f = cc_type_array_name(c, cc_conversion_source_type(c, n)), *t = tyname(c, to);
	uint32_t loc = c->cv_what == CC_CONVERSION_CONTEXT_ARGUMENT || c->cv_what == CC_CONVERSION_CONTEXT_RETURN ? r.b : c->cv_loc;
	int bit = !strcmp(flag, "-Wint-conversion") ? 1 : !strcmp(flag, "-Wincompatible-pointer-types") ? 2 : 4;
	char msg[1024];
	ast_qualified_type ft = AST_NODE(n)->ty;
	int op = AST_NODE(n)->op;
	/* clang: "; dereference with *" when *n has the target type (a fix-it; vcc: simple operands only) */
	bool dr = bit != 4 && cc_type_is_pointer(c, ft) && cc_type_is_compatible(c, AST_QUALIFIED_TYPE_UNQUALIFIED(AST_TYPE(c, ft)->base), AST_QUALIFIED_TYPE_UNQUALIFIED(to));
	/* a simple or parenthesized operand takes '*', another '*(' ... ')' */
	bool wrap = dr && !(op == AST_NODE_VARIABLE || op == AST_NODE_MEMBER || op == AST_NODE_CALL || op == AST_NODE_INDEX || (AST_NODE(n)->flags & AST_NODE_FLAG_PARENTHESIZED) ||
			    (c->paren[n].l | c->paren[n].r));
	/* "; take the address with &" when &n has the target type (an lvalue; vcc: simple operands) */
	bool ad = !dr && bit != 4 && cc_type_is_pointer(c, to) && cc_type_is_compatible(c, AST_QUALIFIED_TYPE_UNQUALIFIED(ft), AST_QUALIFIED_TYPE_UNQUALIFIED(AST_TYPE(c, to)->base)) &&
		  (op == AST_NODE_VARIABLE || op == AST_NODE_MEMBER || op == AST_NODE_INDEX) && (AST_NODE(n)->flags & AST_NODE_FLAG_LVALUE) &&
		  !(AST_NODE(n)->flags & AST_NODE_FLAG_BITFIELD) && !(op == AST_NODE_VARIABLE && c->syms[AST_NODE(n)->a].sc == AST_STORAGE_REGISTER);
	diag_fixit fx[2] = { { r.b, r.b, wrap ? "*(" : dr ? "*" : "&", DIAG_APPLICABILITY_MAYBE }, { r.e, r.e, ")", DIAG_APPLICABILITY_MAYBE } };
	const char *h = dr ? "; dereference with *" : ad ? "; take the address with &" : "";
	int nfx = wrap ? 2 : dr || ad;
	char fl[64];

	if (bit == 1 && dr) {	/* clang's tryToFixConversion: "; remove &" before "; dereference with *" */
		uint32_t m = n;

		while (AST_NODE(m)->op == AST_NODE_CAST && !c->span[m].e)
			m = AST_NODE(m)->a;
		if (!cc_type_is_pointer(c, to) && AST_NODE(m)->op == AST_NODE_ADDRESS && cc_type_is_compatible(c, AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE(AST_NODE(m)->a)->ty), AST_QUALIFIED_TYPE_UNQUALIFIED(to)))
			h = "; remove &", fx[0] = (diag_fixit){ cc_node_range_bare(c, m).b, cc_node_range_bare(c, m).b + 1, "", DIAG_APPLICABILITY_MAYBE }, nfx = 1;
	}
	snprintf(fl, sizeof fl, "%s [%s", h, flag);

	switch (c->cv_what) {
	case CC_CONVERSION_CONTEXT_INITIALIZER:
		snprintf(msg, sizeof msg, "%s initializing '%s' with an expression of type '%s'%s]", what, t, f, fl);
		break;
	case CC_CONVERSION_CONTEXT_ARGUMENT:
		snprintf(msg, sizeof msg, "%s passing '%s' to parameter of type '%s'%s]", what, f, t, fl);
		break;
	case CC_CONVERSION_CONTEXT_RETURN:
		snprintf(msg, sizeof msg, "%s returning '%s' from a function with result type '%s'%s]", what, f, t, fl);
		break;
	default:
		snprintf(msg, sizeof msg, "%s assigning to '%s' from '%s'%s]", what, t, f, fl);
	}
	static const char *const ipt[] = { "incompatible-pointer-types", NULL };

	if (c->o->conv_warn & bit) {
		/* -Wno-error=...: clang's warning (the message without its flag, which diag_warning_report adds) */
		char *b = strrchr(msg, '[');

		if (diag_warning_error_named(c->p, flag + 2, bit == 4 ? ipt : NULL) == 1)
			goto err;	/* -Werror= again: clang's own error (no "-Werror,") */
		if (b && b > msg)
			b[-1] = 0;
		diag_next_id = id;
		if (cc_report_warning(c, flag + 2, true, bit == 4 ? ipt : NULL, loc, &r, 1, fx, nfx, "%s", msg) && c->cv_what == CC_CONVERSION_CONTEXT_ARGUMENT)
			cc_conversion_parameter_note(c);
		diag_next_id = NULL;
		return;
	}
err:
	diag_next_id = id;
	report(c, "error", loc, &r, 1, fx, nfx, "%s", msg);
	diag_next_id = NULL;
	c->nconv++;
	if (c->cv_what == CC_CONVERSION_CONTEXT_ARGUMENT)
		cc_conversion_parameter_note(c);
}

/* pointee types that clang's -Wincompatible-pointer-types accepts: compatible once
   qualifiers (at every level) and integer signedness are ignored, or one is void */
static bool cc_conversion_pointer_target_is_ok_nested(cc *c, ast_qualified_type a, ast_qualified_type b, bool top)
{
	int ka, kb;

	a = AST_QUALIFIED_TYPE_UNQUALIFIED(a), b = AST_QUALIFIED_TYPE_UNQUALIFIED(b);
	ka = AST_TYPE_KIND(c, a), kb = AST_TYPE_KIND(c, b);
	if (cc_type_is_compatible(c, a, b) || (top && (ka == AST_TYPE_VOID || kb == AST_TYPE_VOID)))
		return true;
	if ((AST_TYPE(c, a)->flags ^ AST_TYPE(c, b)->flags) & AST_TYPE_FLAG_ATOMIC)
		return false;
	if (ka == AST_TYPE_POINTER && kb == AST_TYPE_POINTER)
		return cc_conversion_pointer_target_is_ok_nested(c, AST_TYPE(c, a)->base, AST_TYPE(c, b)->base, false);
	if (top && (cc_type_is_integer(c, a) || ka == AST_TYPE_ENUM) && (cc_type_is_integer(c, b) || kb == AST_TYPE_ENUM))
		return cc_type_size(c, a) == cc_type_size(c, b);	/* -Wpointer-sign (a warning; only the pointee itself) */
	if (ka == AST_TYPE_ARRAY && kb == AST_TYPE_ARRAY)
		return cc_conversion_pointer_target_is_ok_nested(c, AST_TYPE(c, a)->base, AST_TYPE(c, b)->base, false);
	return false;
}

static bool cc_conversion_pointer_target_is_ok(cc *c, ast_qualified_type a, ast_qualified_type b) { return cc_conversion_pointer_target_is_ok_nested(c, a, b, true); }

/* -Wincompatible-pointer-types-discards-qualifiers: the pointee loses const/volatile/restrict */
/* const char ** from char **: the levels below the pointee differ in qualifiers (clang's
   IncompatibleNestedPointerQualifiers: the types otherwise the same) */
static bool cc_conversion_nested_qualifiers_differ(cc *c, ast_qualified_type a, ast_qualified_type b)
{
	bool d = false;

	while (cc_type_is_pointer(c, a) && cc_type_is_pointer(c, b)) {
		a = AST_TYPE(c, a)->base, b = AST_TYPE(c, b)->base;
		d |= (AST_QUALIFIED_TYPE_QUALIFIERS(a) & AST_QUALIFIER_MASK) != (AST_QUALIFIED_TYPE_QUALIFIERS(b) & AST_QUALIFIER_MASK);
	}
	return d && cc_type_is_compatible(c, AST_QUALIFIED_TYPE_UNQUALIFIED(a), AST_QUALIFIED_TYPE_UNQUALIFIED(b));
}

/* a warning about the implicit conversion of n to to, in clang's words for the context */
static void conv_warn(cc *c, uint32_t n, ast_qualified_type to, const char *flag, const char *const *g, const char *what)
{
	diag_span r = cc_node_range(c, n);
	const char *f = cc_type_array_name(c, cc_conversion_source_type(c, n)), *t = tyname(c, to);
	uint32_t loc = c->cv_what == CC_CONVERSION_CONTEXT_ARGUMENT || c->cv_what == CC_CONVERSION_CONTEXT_RETURN ? r.b : c->cv_loc;
	bool on;

	switch (c->cv_what) {
	case CC_CONVERSION_CONTEXT_INITIALIZER:
		on = cc_report_warning(c, flag, true, g, loc, &r, 1, NULL, 0, "initializing '%s' with an expression of type '%s' %s", t, f, what);
		break;
	case CC_CONVERSION_CONTEXT_ARGUMENT:
		on = cc_report_warning(c, flag, true, g, loc, &r, 1, NULL, 0, "passing '%s' to parameter of type '%s' %s", f, t, what);
		break;
	case CC_CONVERSION_CONTEXT_RETURN:
		on = cc_report_warning(c, flag, true, g, loc, &r, 1, NULL, 0, "returning '%s' from a function with result type '%s' %s", f, t, what);
		break;
	default:
		on = cc_report_warning(c, flag, true, g, loc, &r, 1, NULL, 0, "assigning to '%s' from '%s' %s", t, f, what);
	}
	if (on && c->cv_what == CC_CONVERSION_CONTEXT_ARGUMENT)
		cc_conversion_parameter_note(c);
}

static void cc_conversion_qualifiers_discarded_report(cc *c, uint32_t n, ast_qualified_type to, bool nested)
{
	static const char *const g[] = { "incompatible-pointer-types", NULL };

	conv_warn(c, n, to, "incompatible-pointer-types-discards-qualifiers", g,
		  nested ? "discards qualifiers in nested pointer types" : "discards qualifiers");
}

/* clang's implicit-conversion errors (int-conversion, incompatible pointer types) */
CC_PRIVATE uint32_t cc_conversion_check(cc *c, uint32_t n, ast_qualified_type to)
{
	ast_qualified_type from = AST_NODE(n)->ty;

	if (AST_NODE(n)->flags & AST_NODE_FLAG_ERROR)
		return n;
	if (cc_type_is_pointer(c, to) && (cc_type_is_integer(c, from) || AST_TYPE_KIND(c, from) == AST_TYPE_ENUM) && !cc_expression_is_null_pointer_constant(c, n) && AST_TYPE_KIND(c, from) != AST_TYPE_BOOL) {
		cc_conversion_error_report(c, n, to, "incompatible integer to pointer conversion", "-Wint-conversion", "ext_typecheck_convert_int_pointer");
	} else if ((cc_type_is_integer(c, to) || AST_TYPE_KIND(c, to) == AST_TYPE_ENUM) && AST_TYPE_KIND(c, to) != AST_TYPE_BOOL && cc_type_is_pointer(c, from)) {
		cc_conversion_error_report(c, n, to, "incompatible pointer to integer conversion", "-Wint-conversion", "ext_typecheck_convert_pointer_int");
	} else if (cc_type_is_pointer(c, to) && cc_type_is_pointer(c, from) && !(AST_TYPE(c, to)->flags & AST_TYPE_FLAG_BLOCK) && !(AST_TYPE(c, from)->flags & AST_TYPE_FLAG_BLOCK) &&
		   !cc_conversion_pointer_target_is_ok(c, AST_TYPE(c, to)->base, AST_TYPE(c, from)->base)) {
		if (cc_type_is_function(c, AST_TYPE(c, to)->base) && cc_type_is_function(c, AST_TYPE(c, from)->base))
			cc_conversion_error_report(c, n, to, "incompatible function pointer types", "-Wincompatible-function-pointer-types", "ext_typecheck_convert_incompatible_function_pointer");
		else
			cc_conversion_error_report(c, n, to, "incompatible pointer types", "-Wincompatible-pointer-types", "ext_typecheck_convert_incompatible_pointer");
	} else {
		if (cc_type_is_pointer(c, to) && cc_type_is_pointer(c, from) && !cc_type_is_function(c, AST_TYPE(c, to)->base) && !cc_type_is_function(c, AST_TYPE(c, from)->base) &&
		    (AST_QUALIFIED_TYPE_QUALIFIERS(AST_TYPE(c, from)->base) & ~AST_QUALIFIED_TYPE_QUALIFIERS(AST_TYPE(c, to)->base) & AST_QUALIFIER_MASK))
			cc_conversion_qualifiers_discarded_report(c, n, to, false);
		else if (cc_type_is_pointer(c, to) && cc_type_is_pointer(c, from) && cc_conversion_nested_qualifiers_differ(c, AST_TYPE(c, to)->base, AST_TYPE(c, from)->base))
			cc_conversion_qualifiers_discarded_report(c, n, to, true);
		else if (cc_type_is_pointer(c, to) && cc_type_is_pointer(c, from) && cc_type_is_integer(c, AST_TYPE(c, to)->base) && cc_type_is_integer(c, AST_TYPE(c, from)->base) &&
			 !cc_type_is_compatible(c, AST_QUALIFIED_TYPE_UNQUALIFIED(AST_TYPE(c, to)->base), AST_QUALIFIED_TYPE_UNQUALIFIED(AST_TYPE(c, from)->base))) {
			static const char *const g[] = { "pointer-sign", NULL };

			conv_warn(c, n, to, "pointer-sign", g, "converts between pointers to integer types with different sign");
		}
		return n;
	}
	AST_NODE(n)->flags |= AST_NODE_FLAG_ERROR;	/* no second message from convert */
	return n;
}

/* the bits of v as clang's IntRange of a constant: width, non-negative */
static int cc_conversion_integer_range(i128 v, bool *nn)
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
static void cc_conversion_float_format(char *o, double v, int prec)
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
static void cc_conversion_literal_check(cc *c, uint32_t n, ast_qualified_type to)
{
	uint32_t e = n, lit;
	double v, t;
	char vs[64], ts[48];
	const char *what = NULL;

	while (AST_NODE(e)->op == AST_NODE_CAST && !c->span[e].e && AST_NODE(e)->a)
		e = AST_NODE(e)->a;
	lit = AST_NODE(e)->op == AST_NODE_UNARY && AST_NODE(e)->aux == LEX_TOKEN_MINUS ? AST_NODE(e)->a : e;
	if (AST_NODE(lit)->op != AST_NODE_FLOAT_NUMBER || cc_type_is_float128(c, AST_NODE(lit)->ty) || cc_type_size(c, AST_NODE(lit)->ty) > 8)
		return;
	v = c->fpool[AST_NODE(lit)->a];
	if (lit != e)
		v = -v;
	t = trunc(v);
	if (AST_TYPE_KIND(c, to) == AST_TYPE_BOOL) {
		if (v == 0 || v == 1)
			return;
		snprintf(ts, sizeof ts, "%s", v != 0 ? "true" : "false");
	} else {
		int w = (int)cc_type_integer_bits(c, to);
		bool sg = cc_type_is_signed(c, to);
		double lo = sg ? -ldexp(1, w - 1) : 0, hi = sg ? ldexp(1, w - 1) : ldexp(1, w);

		if (!(t >= lo && t < hi))
			what = "out of range";
		else if (t == v)
			return;
		else
			snprintf(ts, sizeof ts, "%.0f", t == 0 ? 0.0 : t);
	}
	{
		diag_span r[2] = { { c->cv_what == CC_CONVERSION_CONTEXT_ARGUMENT ? c->cv_callee : c->cv_loc, 0 },
				 { AST_NODE(e)->loc, pp_token_end_find(c->p, AST_NODE(lit)->loc) } };

		r[0].e = pp_token_end_find(c->p, r[0].b);
		if (what) {
			cc_report_warning(c, "literal-conversion", true, cc_warning_group_conversion, r[1].b, r, 2, NULL, 0,
				 "implicit conversion of out of range value from '%s' to '%s' is undefined",
				 tyname(c, AST_NODE(lit)->ty), tyname(c, to));
			return;
		}
		cc_conversion_float_format(vs, v, cc_type_size(c, AST_NODE(lit)->ty) == 4 ? 8 : 16);
		cc_report_warning(c, "literal-conversion", true, cc_warning_group_conversion, r[1].b, r, 2, NULL, 0,
			 "implicit conversion from '%s' to '%s' changes value from %s to %s", tyname(c, AST_NODE(lit)->ty),
			 tyname(c, to), vs, ts);
	}
}

/* -Wconstant-conversion: integer constant n converted to the narrower integer to */

/* n's value depends on a __builtin_(dynamic_)object_size whose object vcc does not
   know (folded to (size_t)-1 or 0, which clang leaves to code generation) */
static bool cc_conversion_object_size_is_unknown(cc *c, uint32_t n, int depth)
{
	const node *x = AST_NODE(n);
	i128 sz;
	cc_value mode;

	if (!n || depth > 32)
		return false;
	switch (x->op) {
	case AST_NODE_BUILTIN:
		if (x->aux != CC_BUILTIN_OBJECT_SIZE && x->aux != CC_BUILTIN_DYNAMIC_OBJECT_SIZE)
			return false;
		return x->c != 2 || !cc_eval(c, c->lists[x->b + 1], &mode) || mode.kind != CC_VALUE_INTEGER ||
		       !cc_builtin_object_size(c, c->lists[x->b], (unsigned)mode.i, &sz);
	case AST_NODE_CAST: case AST_NODE_UNARY:
		return cc_conversion_object_size_is_unknown(c, x->a, depth + 1);
	case AST_NODE_BINARY:
		return cc_conversion_object_size_is_unknown(c, x->a, depth + 1) || cc_conversion_object_size_is_unknown(c, x->b, depth + 1);
	case AST_NODE_CONDITIONAL:
		return cc_conversion_object_size_is_unknown(c, x->a, depth + 1) || cc_conversion_object_size_is_unknown(c, x->b, depth + 1) ||
		       cc_conversion_object_size_is_unknown(c, x->c, depth + 1);
	default:
		return false;
	}
}

CC_PRIVATE void cc_conversion_constant_check(cc *c, uint32_t n, ast_qualified_type to)
{
	ast_qualified_type from = AST_NODE(n)->ty;
	cc_value cv;
	int tw, sw, lw;
	bool tnn, lnn, deferred;
	i128 v, t;

	if (AST_NODE(n)->op == AST_NODE_CONDITIONAL && AST_NODE(n)->b && AST_NODE(n)->c) {	/* clang: each arm (the taken one), in its own type */
		cc_value cc0;
		int k0 = 0, k1 = 2;

		if (cc_eval(c, AST_NODE(n)->a, &cc0) && cc0.kind == CC_VALUE_INTEGER)
			k0 = cc0.i == 0, k1 = k0 + 1;
		for (int k = k0; k < k1; k++) {
			uint32_t e = k ? AST_NODE(n)->c : AST_NODE(n)->b;

			while (AST_NODE(e)->op == AST_NODE_CAST && !c->span[e].e && AST_NODE(e)->a)
				e = AST_NODE(e)->a;
			cc_conversion_constant_check(c, e, to);
		}
		return;
	}
	if (AST_NODE(n)->op == AST_NODE_COMMA) {	/* clang: the right operand, at the comma */
		uint32_t sl = c->cv_loc;
		uint8_t sw = c->cv_what;

		c->cv_loc = AST_NODE(n)->loc, c->cv_what = CC_CONVERSION_CONTEXT_INITIALIZER;
		cc_conversion_constant_check(c, AST_NODE(n)->b, to);
		c->cv_loc = sl, c->cv_what = sw;
		return;
	}
	if (cc_type_is_integer(c, to) && cc_type_is_float(c, from) && !c->unevaluated) {	/* (immediate: also in dead code) */
		cc_conversion_literal_check(c, n, to);
		return;
	}
	if (!cc_type_is_integer(c, to) || !cc_type_is_integer(c, from) || AST_TYPE_KIND(c, to) == AST_TYPE_BOOL || AST_TYPE_KIND(c, from) == AST_TYPE_BOOL ||
	    c->unevaluated || !cc_eval(c, n, &cv) || cv.kind != CC_VALUE_INTEGER)
		return;
	tw = (int)cc_type_integer_bits(c, to), sw = (int)cc_type_integer_bits(c, from);
	if (tw >= sw || tw > 64)
		return;
	/* clang: __builtin_object_size of an unknown object is no constant here (its
	   evaluator leaves it to code generation): no -Wconstant-conversion for the
	   (size_t)-1 or what arithmetic makes of it, nouveau's __member_size(p) /
	   sizeof(*p); a known size warns as any constant */
	if (cc_conversion_object_size_is_unknown(c, n, 0))
		return;
	tnn = !cc_type_is_signed(c, to);
	v = cv.i;
	if (!cc_type_is_signed(c, from) && sw <= 64)
		v = (i128)((uint64_t)v & (sw == 64 ? ~0ull : (1ull << sw) - 1));
	lw = cc_conversion_integer_range(v, &lnn);
	if (lw > tw && c->dead)	/* clang's DiagRuntimeBehavior: not in unreachable code */
		return;
	if (lw > tw)	/* (crange counts a negative range's sign bit) */
		deferred = true;
	else if (lw == tw && !tnn && lnn && cc_type_is_signed(c, from)) {
		/* clang's isSameWidthConstantConversion: not a hex/octal literal, not a char in braces */
		uint32_t e = n;

		while (AST_NODE(e)->op == AST_NODE_CAST && !c->span[e].e && AST_NODE(e)->a)
			e = AST_NODE(e)->a;
		uint32_t b = cc_node_range(c, e).b;
		int ch;

		while ((ch = pp_location_char(c->p, b)) == '(' || ch == ' ' || ch == '\t')
			b++;
		if ((AST_NODE(e)->op == AST_NODE_NUMBER ? (AST_NODE(e)->aux & AST_NUMBER_RADIX) : ch == '0') ||
		    ((AST_TYPE_KIND(c, to) == AST_TYPE_CHAR || AST_TYPE_KIND(c, to) == AST_TYPE_SIGNED_CHAR) && pp_location_char(c->p, c->cv_loc) == '{'))
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
		uint32_t cl = c->cv_what == CC_CONVERSION_CONTEXT_ARGUMENT ? c->cv_callee : c->cv_loc;
		diag_span r[2] = { { cl, pp_token_end_find(c->p, cl) }, cc_node_range_bare(c, n) };
		uint32_t at = r[1].b;
		char msg[112];

		while (pp_location_char(c->p, at) == '(' || pp_location_char(c->p, at) == ' ')	/* clang: (v) at v */
			at++;

		snprintf(msg, sizeof msg, "implicit conversion from '%s' to '%s' changes value from %lld to %lld",
			 tyname(c, from), tyname(c, to), (long long)v, (long long)t);
		char vs[48];

		snprintf(vs, sizeof vs, "%lld", (long long)v);	/* (clang: "expanded from macro" at the constant) */
		if (deferred && c->fn && !c->defer_rt) {
			diag_next_id = "warn_impcast_integer_precision_constant";
			cc_report_runtime_defer(c, "constant-conversion", cc_warning_group_conversion, at, r, 2, vs, (uint32_t)strlen(vs), "%s", msg);
			diag_next_id = NULL;
			return;
		}
		{
			diag_record *d;

			diag_next_id = "warn_impcast_integer_precision_constant";
			d = cc_report_warning_begin(c, "constant-conversion", true, cc_warning_group_conversion, at, "%s", msg);
			diag_next_id = NULL;

			if (d) {
				diag_record_label_add(c->p, d, r[0].b, r[0].e, NULL);
				diag_record_label_add(c->p, d, r[1].b, r[1].e, NULL);
				diag_record_macro_set(d, vs, (uint32_t)strlen(vs));
				diag_record_emit(c->p, d);
			}
		}
	}
}

/* -Wbitfield-constant-conversion: constant v stored in bit-field f; at the
   operator op (an assignment), or at v (0: an initializer) */
CC_PRIVATE void cc_conversion_bitfield_constant_check(cc *c, const field *f, uint32_t v, uint32_t op)
{
	static const char *const g[] = { "bitfield-constant-conversion", "constant-conversion", "conversion", NULL };
	cc_value cv;
	int w = f->width;
	bool sg = cc_type_is_signed(c, f->ty);
	i128 x, t;

	if (AST_TYPE_KIND(c, f->ty) == AST_TYPE_BOOL || !cc_type_is_integer(c, AST_NODE(v)->ty) || AST_TYPE_KIND(c, AST_NODE(v)->ty) == AST_TYPE_BOOL || w <= 0 || w > 64 ||
	    c->unevaluated || !cc_eval(c, v, &cv) || cv.kind != CC_VALUE_INTEGER)
		return;
	x = cv.i;
	if (!cc_type_is_signed(c, AST_NODE(v)->ty) && cc_type_size(c, AST_NODE(v)->ty) <= 8)
		x = (i128)((uint64_t)x & (cc_type_size(c, AST_NODE(v)->ty) == 8 ? ~0ull : (1ull << cc_type_size(c, AST_NODE(v)->ty) * 8) - 1));
	t = (i128)((uint64_t)x & (w == 64 ? ~0ull : (1ull << w) - 1));
	if (sg && (t >> (w - 1)) & 1)
		t -= (i128)1 << w;
	if (t == x || (!sg && x < 0 && x >= -((i128)1 << (w - 1))))
		return;
	{
		diag_span r[1] = { cc_node_range(c, v) };
		uint32_t at = op ? op : r[0].b;
		bool one = sg && w == 1 && x == 1;

		diag_info d = { "warning", at, op ? r : NULL, op ? 1 : 0, NULL, 0, op ? "=" : NULL, op ? 1 : 0 };

		diag_warning_report(c->p, one ? "single-bit-bitfield-constant-conversion" : "bitfield-constant-conversion", true,
			 one ? g : g + 1, &d, "implicit truncation from '%s' to %s changes value from %lld to %lld",
			 tyname(c, AST_NODE(v)->ty), one ? "a one-bit wide bit-field" : "bit-field", (long long)x, (long long)t);
	}
}


/* clang spells the array type of this warning without the space: 'int[4]' */
CC_PRIVATE const char *cc_type_array_name(cc *c, ast_qualified_type t)
{
	const char *s = tyname(c, t), *sp = strstr(s, " [");
	char *o;

	if (!sp)
		return s;
	o = cc_heap_allocate(c, strlen(s) + 1);
	memcpy(o, s, (size_t)(sp - s));
	strcpy(o + (sp - s), sp + 1);
	return o;
}

/* clang's DiagRuntimeBehavior: nothing in unevaluated operands and constant expressions, and in a
   function nothing in code its CFG finds unreachable (at file scope a dead ?: branch still warns) */
CC_PRIVATE bool cc_report_runtime_is_quiet(const cc *c)
{
	return c->unevaluated || c->cexpr || (c->dead && c->fn);
}

CC_PRIVATE void cc_format_i128(char *b, size_t n, i128 v)
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
CC_PRIVATE const char *cc_conversion_char_signedness(cc *c, ast_qualified_type t)
{
	if (AST_TYPE_KIND(c, t) != AST_TYPE_CHAR)
		return "";
	return c->o->target == PP_DARWIN ? " (plain 'char' is signed on Apple and x86, unsigned on Arm and RISC-V Linux)"
					 : " (plain 'char' is unsigned on Arm and RISC-V Linux, signed on Apple and x86)";
}

