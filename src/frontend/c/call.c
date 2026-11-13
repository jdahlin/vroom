/* vcc front end: function calls: argument checks, overload choice at the call, rewrites of library calls */
#include "cc-internal.h"

/* clang's -Wnonnull: a null pointer constant passed for a nonnull parameter */
static void cc_call_nonnull_check(cc *c, uint32_t fs, qty ft, const uint32_t *args, uint32_t n)
{
	const sym *y = &c->syms[fs];
	uint32_t all = y->attrs ? c->attr[y->attrs].nonnull : 0;
	uint32_t call_end = c->prevloc;	/* ')': where clang points for a plain nonnull */
	const ctype *f = AST_TYPE(c, ft);

	if (!all && !y->psyms)
		return;
	for (uint32_t i = 0; i < n; i++) {
		uint32_t p = y->psyms && i < f->n ? c->lists[y->psyms - 1 + i] : 0;
		bool pn = p && (c->syms[p].flags & SF_NONNULL), idx = i < 31 && (all >> (i + 1) & 1);
		bool any = (all & 1) && i < f->n && cc_type_is_pointer(c, c->params[f->aux + i]);
		cval cv;

		if (!pn && !idx && !any)
			continue;
		if (!cc_type_is_pointer(c, AST_NODE(args[i])->ty) || !cc_constant_eval(c, args[i], &cv) || cv.kind != CV_INT || cv.i)
			continue;
		{
			diag_span r = cc_node_range(c, args[i]);

			if (cc_report_runtime_is_quiet(c))	/* (clang: DiagRuntimeBehavior, at the body's end) */
				continue;
			cc_report_runtime_defer(c, "nonnull", NULL, pn || idx ? r.b : call_end, &r, 1, NULL, 0,
				 "null passed to a callee that requires a non-null argument");
		}
	}
}

CC_PRIVATE __attribute__((noinline)) uint32_t call(cc *c, uint32_t fn, uint32_t loc)
{
	uint32_t callee;
	cc_id_list args;
	qty ft;
	const ctype *f;
	uint32_t n, fmt;
	qty aty[64];
	uint32_t atd[64];
	bool have_args = false;
	unsigned aflags = 0;	/* the arguments' NF_* together */

	if (AST_NODE(fn)->op == N_VAR && (c->syms[AST_NODE(fn)->a].flags & SF_OVERLOAD) && c->syms[AST_NODE(fn)->a].kind == S_FUNC) {
		uint32_t s;

		cc_builtin_arguments_parse(c, &args);
		have_args = true;
		s = cc_overload_pick(c, AST_NODE(fn)->a, &args, loc);
		AST_NODE(fn)->a = s;
		AST_NODE(fn)->ty = c->syms[s].ty;
		if (unlikely(c->syms[s].flags & SF_DEPRECATED))
			cc_deprecated_use_report(c, AST_NODE(fn)->loc, c->syms[s].name, &c->attr[c->syms[s].attrs], c->syms[s].loc);
		if (!c->unevaluated)
			c->syms[s].flags |= SF_USED;
		c->syms[s].flags |= SF_REF;
	}
	if (unlikely(AST_NODE_IS_ERROR(fn))) {	/* an invalid declaration (or expression): silent */
		if (!have_args)
			cc_builtin_arguments_parse(c, &args);
		return cc_node_error_new(c, loc);
	}
	fn = cc_node_rvalue(c, fn);
	callee = fn;
	ft = AST_NODE(fn)->ty;
	if (!cc_type_is_pointer(c, ft) || !cc_type_is_function(c, AST_TYPE(c, ft)->base)) {
		diag_span r = cc_node_range(c, fn);

		report(c, "error", loc, &r, 1, NULL, 0, "called object type '%s' is not a function or function pointer", tyname(c, ft));
		if (!have_args)
			cc_builtin_arguments_parse(c, &args);
		return cc_node_error_new(c, loc);
	}
	ft = AST_TYPE(c, ft)->base;
	f = AST_TYPE(c, ft);
	if (!have_args)
		cc_builtin_arguments_parse(c, &args);
	f = AST_TYPE(c, ft);
	while (AST_NODE(callee)->op == N_CAST && !c->span[callee].e)
		callee = AST_NODE(callee)->a;
	if (!(f->flags & TF_NOPROTO)) {
		if (args.n < f->n || (args.n > f->n && !(f->flags & TF_VARIADIC))) {
			for (uint32_t i = 0; i < args.n; i++)
				if (AST_NODE_IS_ERROR(args.v[i]))	/* clang: no count check on an invalid argument list */
					return cc_node_error_new(c, loc);
			const char *v = f->flags & TF_VARIADIC ? "at least " : "";
			uint32_t fs = AST_NODE(callee)->op == N_VAR && c->syms[AST_NODE(callee)->a].kind == S_FUNC ? AST_NODE(callee)->a : 0;
			uint32_t p1 = fs && c->syms[fs].psyms && f->n == 1 ? c->lists[c->syms[fs].psyms - 1] : 0;
			diag_span cr = cc_node_range(c, callee);

			if (p1 && !c->syms[p1].name)
				p1 = 0;
			if (args.n < f->n) {
				if (p1)
					report(c, "error", c->prevloc, &cr, 1, NULL, 0, "too few arguments to function call, single argument '%.*s' was not specified",
					       CC_IDENTIFIER(c->syms[p1].name));
				else
					report(c, "error", c->prevloc, &cr, 1, NULL, 0, "too few arguments to function call, %sexpected %u, have %u",
					       v, f->n, args.n);
			} else {
				diag_span r[2] = { cr, { cc_node_range(c, args.v[f->n]).b, cc_node_range(c, args.v[args.n - 1]).e } };

				if (p1)
					report(c, "error", r[1].b, r, 2, NULL, 0, "too many arguments to function call, expected single argument '%.*s', have %u arguments",
					       CC_IDENTIFIER(c->syms[p1].name), args.n);
				else
					report(c, "error", r[1].b, r, 2, NULL, 0, "too many arguments to function call, expected %u, have %u",
					       f->n, args.n);
			}
			if (fs) {
				const sym *y = &c->syms[fs];
				diag_span pr = { y->plb, y->ple };
				size_t len;
				const char *t = y->ple > y->plb ? pp_source_text(c->p, y->plb, y->ple, &len) : NULL;

				if (t) {
					while (len && (t[len - 1] == ' ' || t[len - 1] == '\t' || t[len - 1] == '\n'))
						len--;
					pr.e = pr.b + (uint32_t)len;
				}
				report(c, "note", y->loc, &pr, t && len ? 1 : 0, NULL, 0, "'%.*s' declared here", CC_IDENTIFIER(y->name));
			}
			return cc_node_error_new(c, loc);	/* clang: the arguments are not converted */
		}
	}
	{
		uint32_t fs = AST_NODE(callee)->op == N_VAR && c->syms[AST_NODE(callee)->a].kind == S_FUNC ? AST_NODE(callee)->a : 0;

		fmt = fs && cc_format_check_attribute(c, fs) && args.n <= 64 && !c->unevaluated ? fs : 0;
	}
	for (uint32_t i = 0; i < args.n; i++) {
		uint32_t a = args.v[i];

		f = AST_TYPE(c, ft);
		if (fmt) {
			int k = AST_TYPE_KIND(c, AST_NODE(a)->ty);

			atd[i] = k == TY_ARRAY ? 0 : cc_typedef_sugar_expression(c, a);
			aty[i] = cc_node_bitfield(c, a) ? 0 : k == TY_ARRAY ? cc_type_pointer(c, AST_TYPE(c, AST_NODE(a)->ty)->base) : k == TY_FUNC ? 0 : QUNQ(AST_NODE(a)->ty);
		}
		if (!(f->flags & TF_NOPROTO) && i < f->n) {
			c->cv_fn = AST_NODE(callee)->op == N_VAR && c->syms[AST_NODE(callee)->a].kind == S_FUNC ? AST_NODE(callee)->a : 0;
			c->cv_arg = i;
			c->cv_callee = cc_node_range(c, callee).b;
			a = cc_conversion_apply_as(c, cc_conversion_decay(c, a), QUNQ(c->params[f->aux + i]), CC_CONVERSION_CONTEXT_ARGUMENT, loc);	/* (clang: the parameter type without its top-level qualifiers) */
			c->cv_fn = 0;
		} else {
			/* default argument promotions */
			a = cc_node_rvalue(c, a);
			if (!(AST_NODE(a)->flags & NF_ERR) && !cc_type_complete(c, AST_NODE(a)->ty)) {
				diag_span r = cc_node_range(c, a);

				report(c, "error", r.b, &r, 1, NULL, 0, "argument type '%s' is incomplete", tyname(c, AST_NODE(a)->ty));
				cc_report_forward_note(c, AST_NODE(a)->ty);
				a = cc_node_error_new(c, AST_NODE(a)->loc);
			}
			if (AST_TYPE_KIND(c, AST_NODE(a)->ty) == TY_FLOAT)
				a = cc_cast_insert(c, a, cc_type_basic(TY_DOUBLE), CK_F2F);
		}
		args.v[i] = a;
		aflags |= AST_NODE(a)->flags;
	}
	if (AST_NODE(callee)->op == N_VAR && c->syms[AST_NODE(callee)->a].kind == S_FUNC && !c->unevaluated)
		cc_call_nonnull_check(c, AST_NODE(callee)->a, ft, args.v, args.n);
	if (fmt)
		cc_format_check(c, fmt, args.v, aty, atd, args.n);
	if (!c->o->no_builtin && !cc_library_builtin_is_disabled(c->o, "strncmp", 7) && args.n == 3 &&
	    AST_NODE(callee)->op == N_VAR && c->syms[AST_NODE(callee)->a].kind == S_FUNC &&
	    (c->syms[AST_NODE(callee)->a].flags & SF_GLOBAL) && c->syms[AST_NODE(callee)->a].sc != SC_STATIC &&
	    !(c->syms[AST_NODE(callee)->a].attrs && c->attr[c->syms[AST_NODE(callee)->a].attrs].asmname)) {
		int nl;
		const char *nm = cc_identifier_name(c->syms[AST_NODE(callee)->a].name, &nl);
		if (nl == 7 && !memcmp(nm, "strncmp", 7) && f->n == 3 &&
		    !(f->flags & (TF_NOPROTO | TF_VARIADIC)) && cc_type_is_compatible(c, ft, cc_builtin_signature_function_type(c, "iSSz"))) {
			uint32_t folded = cc_builtin_strncmp_zero_fold(c, &args, loc);
			if (folded)
				return folded;
		}
	}
	if (!c->o->no_builtin && AST_NODE(callee)->op == N_VAR && c->syms[AST_NODE(callee)->a].kind == S_FUNC) {
		const sym *y = &c->syms[AST_NODE(callee)->a];
		if ((y->flags & SF_GLOBAL) && y->sc != SC_STATIC && !(y->attrs && c->attr[y->attrs].asmname) &&
		    !cc_library_builtin_is_disabled(c->o, cc_identifier_string(y->name), cc_identifier_length(y->name))) {
			uint32_t result = cc_builtin_fp_sign_build(c, cc_identifier_string(y->name), cc_identifier_length(y->name), ft, &args, loc);
			if (!result) result = cc_builtin_fast_math_build(c, cc_identifier_string(y->name), cc_identifier_length(y->name), ft, &args, loc);
			if (result) {
				AST_NODE(result)->flags |= aflags & NF_ERR;
				return result;
			}
		}
	}
	if (!c->o->no_builtin && args.n == 1 && AST_NODE(callee)->op == N_VAR && c->syms[AST_NODE(callee)->a].kind == S_FUNC &&
	    (c->syms[AST_NODE(callee)->a].flags & SF_GLOBAL) && c->syms[AST_NODE(callee)->a].sc != SC_STATIC &&
	    !(c->syms[AST_NODE(callee)->a].attrs && c->attr[c->syms[AST_NODE(callee)->a].attrs].asmname) &&
	    !cc_library_builtin_is_disabled(c->o, cc_identifier_string(c->syms[AST_NODE(callee)->a].name), cc_identifier_length(c->syms[AST_NODE(callee)->a].name))) {
		/* clang: abs, labs and llabs declared with their library types are builtins (inline) */
		int nl, k = AST_TYPE_KIND(c, AST_TYPE(c, ft)->base);
		const char *nm = cc_identifier_name(c->syms[AST_NODE(callee)->a].name, &nl);
		int want = nl == 3 && !memcmp(nm, "abs", 3) ? TY_INT : nl == 4 && !memcmp(nm, "labs", 4) ? TY_LONG :
			   nl == 5 && !memcmp(nm, "llabs", 5) ? TY_LLONG : -1;

		if (k == want && f->n == 1 && AST_TYPE_KIND(c, c->params[f->aux]) == want) {
			qty t = cc_type_basic(want);
			uint32_t pre, x = cc_lowering_once(c, args.v[0], &pre);
			uint32_t neg = cc_node_binary_new(c, LEX_TOKEN_MINUS, t, cc_node_integer_new(c, 0, t, loc), x, loc);
			uint32_t lt = cc_node_binary_new(c, LEX_TOKEN_LESS, cc_type_basic(TY_INT), x, cc_node_integer_new(c, 0, t, loc), loc);

			return cc_lowering_sequence(c, pre, cc_conditional_build(c, lt, neg, x, loc));
		}
		if (k == TY_DOUBLE && f->n == 1 && !(f->flags & (TF_NOPROTO | TF_VARIADIC)) &&
		    AST_TYPE_KIND(c, c->params[f->aux]) == TY_DOUBLE && c->syms[AST_NODE(callee)->a].sc != SC_STATIC) {
			uint32_t narrowed = cc_builtin_narrow_math_build(c, nm, (size_t)nl, args.v[0], loc);
			if (narrowed)
				return narrowed;
		}
		/* clang folds the libm functions on a constant (double f(double)) */
		static const struct { const char *s; double (*f)(double); } mf[] = {
			{ "sin", sin }, { "cos", cos }, { "tan", tan }, { "exp", exp }, { "log", log }, { "sqrt", sqrt },
			{ "fabs", fabs }, { "floor", floor }, { "ceil", ceil }, { "log10", log10 }, { "atan", atan },
		};
		cval cv;

		if (k == TY_DOUBLE && f->n == 1 && AST_TYPE_KIND(c, c->params[f->aux]) == TY_DOUBLE &&
		    cc_eval(c, args.v[0], &cv) && cv.kind == CV_FLT)
			for (size_t q = 0; q < sizeof(mf) / sizeof(mf[0]); q++)
				if ((size_t)nl == strlen(mf[q].s) && !memcmp(nm, mf[q].s, (size_t)nl)) {
					double r = mf[q].f(cv.f);

					if (r == r && (q != 5 || cv.f >= 0) && (q != 4 && q != 9 ? 1 : cv.f > 0))
						return cc_node_float_new(c, r, cc_type_basic(TY_DOUBLE), loc);
				}
	}
	if (AST_NODE(callee)->op == N_VAR) {
		const sym *fns = &c->syms[AST_NODE(callee)->a];
		if (fns->attrs && c->attr[fns->attrs].interrupt)
			cc_report_error(c, loc, "interrupt service routine cannot be called directly");
	}
	n = cc_node_new(c, N_CALL, QUNQ(AST_TYPE(c, ft)->base), loc);
	AST_NODE(n)->a = fn;
	AST_NODE(n)->b = cc_id_list_freeze(c, &args);
	AST_NODE(n)->c = args.n;
	AST_NODE(n)->flags |= aflags & NF_ERR;	/* clang: a call with an invalid argument is invalid */
	return n;
}
