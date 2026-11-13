/* vcc front end: function calls: argument checks, overload choice at the call, rewrites of library calls */
#include "ccimpl.h"

/* clang's -Wnonnull: a null pointer constant passed for a nonnull parameter */
static void nonnull_args(cc *c, uint32_t fs, qty ft, const uint32_t *args, uint32_t n)
{
	const sym *y = &c->syms[fs];
	uint32_t all = y->attrs ? c->attr[y->attrs].nonnull : 0;
	uint32_t call_end = c->prevloc;	/* ')': where clang points for a plain nonnull */
	const ctype *f = T(c, ft);

	if (!all && !y->psyms)
		return;
	for (uint32_t i = 0; i < n; i++) {
		uint32_t p = y->psyms && i < f->n ? c->lists[y->psyms - 1 + i] : 0;
		bool pn = p && (c->syms[p].flags & SF_NONNULL), idx = i < 31 && (all >> (i + 1) & 1);
		bool any = (all & 1) && i < f->n && is_ptr(c, c->params[f->aux + i]);
		cval cv;

		if (!pn && !idx && !any)
			continue;
		if (!is_ptr(c, N(args[i])->ty) || !eval(c, args[i], &cv) || cv.kind != CV_INT || cv.i)
			continue;
		{
			pp_span r = rng(c, args[i]);

			if (quiet_rt(c))	/* (clang: DiagRuntimeBehavior, at the body's end) */
				continue;
			rt_defer(c, "nonnull", NULL, pn || idx ? r.b : call_end, &r, 1, NULL, 0,
				 "null passed to a callee that requires a non-null argument");
		}
	}
}

VPRIV __attribute__((noinline)) uint32_t call(cc *c, uint32_t fn, uint32_t loc)
{
	uint32_t callee;
	vec args;
	qty ft;
	const ctype *f;
	uint32_t n, fmt;
	qty aty[64];
	uint32_t atd[64];
	bool have_args = false;
	unsigned aflags = 0;	/* the arguments' NF_* together */

	if (N(fn)->op == N_VAR && (c->syms[N(fn)->a].flags & SF_OVERLOAD) && c->syms[N(fn)->a].kind == S_FUNC) {
		uint32_t s;

		args_list(c, &args);
		have_args = true;
		s = ovl_pick(c, N(fn)->a, &args, loc);
		N(fn)->a = s;
		N(fn)->ty = c->syms[s].ty;
		if (unlikely(c->syms[s].flags & SF_DEPRECATED))
			deprecated_use(c, N(fn)->loc, c->syms[s].name, &c->attr[c->syms[s].attrs], c->syms[s].loc);
		if (!c->unevaluated)
			c->syms[s].flags |= SF_USED;
		c->syms[s].flags |= SF_REF;
	}
	if (unlikely(ISERR(fn))) {	/* an invalid declaration (or expression): silent */
		if (!have_args)
			args_list(c, &args);
		return err_node(c, loc);
	}
	fn = rvalue(c, fn);
	callee = fn;
	ft = N(fn)->ty;
	if (!is_ptr(c, ft) || !is_func(c, T(c, ft)->base)) {
		pp_span r = rng(c, fn);

		report(c, "error", loc, &r, 1, NULL, 0, "called object type '%s' is not a function or function pointer", tyname(c, ft));
		if (!have_args)
			args_list(c, &args);
		return err_node(c, loc);
	}
	ft = T(c, ft)->base;
	f = T(c, ft);
	if (!have_args)
		args_list(c, &args);
	f = T(c, ft);
	while (N(callee)->op == N_CAST && !c->span[callee].e)
		callee = N(callee)->a;
	if (!(f->flags & TF_NOPROTO)) {
		if (args.n < f->n || (args.n > f->n && !(f->flags & TF_VARIADIC))) {
			for (uint32_t i = 0; i < args.n; i++)
				if (ISERR(args.v[i]))	/* clang: no count check on an invalid argument list */
					return err_node(c, loc);
			const char *v = f->flags & TF_VARIADIC ? "at least " : "";
			uint32_t fs = N(callee)->op == N_VAR && c->syms[N(callee)->a].kind == S_FUNC ? N(callee)->a : 0;
			uint32_t p1 = fs && c->syms[fs].psyms && f->n == 1 ? c->lists[c->syms[fs].psyms - 1] : 0;
			pp_span cr = rng(c, callee);

			if (p1 && !c->syms[p1].name)
				p1 = 0;
			if (args.n < f->n) {
				if (p1)
					report(c, "error", c->prevloc, &cr, 1, NULL, 0, "too few arguments to function call, single argument '%.*s' was not specified",
					       IDF(c->syms[p1].name));
				else
					report(c, "error", c->prevloc, &cr, 1, NULL, 0, "too few arguments to function call, %sexpected %u, have %u",
					       v, f->n, args.n);
			} else {
				pp_span r[2] = { cr, { rng(c, args.v[f->n]).b, rng(c, args.v[args.n - 1]).e } };

				if (p1)
					report(c, "error", r[1].b, r, 2, NULL, 0, "too many arguments to function call, expected single argument '%.*s', have %u arguments",
					       IDF(c->syms[p1].name), args.n);
				else
					report(c, "error", r[1].b, r, 2, NULL, 0, "too many arguments to function call, expected %u, have %u",
					       f->n, args.n);
			}
			if (fs) {
				const sym *y = &c->syms[fs];
				pp_span pr = { y->plb, y->ple };
				size_t len;
				const char *t = y->ple > y->plb ? pp_src(c->p, y->plb, y->ple, &len) : NULL;

				if (t) {
					while (len && (t[len - 1] == ' ' || t[len - 1] == '\t' || t[len - 1] == '\n'))
						len--;
					pr.e = pr.b + (uint32_t)len;
				}
				report(c, "note", y->loc, &pr, t && len ? 1 : 0, NULL, 0, "'%.*s' declared here", IDF(y->name));
			}
			return err_node(c, loc);	/* clang: the arguments are not converted */
		}
	}
	{
		uint32_t fs = N(callee)->op == N_VAR && c->syms[N(callee)->a].kind == S_FUNC ? N(callee)->a : 0;

		fmt = fs && fmt_attr(c, fs) && args.n <= 64 && !c->unevaluated ? fs : 0;
	}
	for (uint32_t i = 0; i < args.n; i++) {
		uint32_t a = args.v[i];

		f = T(c, ft);
		if (fmt) {
			int k = KIND(c, N(a)->ty);

			atd[i] = k == TY_ARRAY ? 0 : expr_tdef(c, a);
			aty[i] = bitfield_of(c, a) ? 0 : k == TY_ARRAY ? ty_ptr(c, T(c, N(a)->ty)->base) : k == TY_FUNC ? 0 : QUNQ(N(a)->ty);
		}
		if (!(f->flags & TF_NOPROTO) && i < f->n) {
			c->cv_fn = N(callee)->op == N_VAR && c->syms[N(callee)->a].kind == S_FUNC ? N(callee)->a : 0;
			c->cv_arg = i;
			c->cv_callee = rng(c, callee).b;
			a = convert_as(c, decay(c, a), QUNQ(c->params[f->aux + i]), CX_PASS, loc);	/* (clang: the parameter type without its top-level qualifiers) */
			c->cv_fn = 0;
		} else {
			/* default argument promotions */
			a = rvalue(c, a);
			if (!(N(a)->flags & NF_ERR) && !complete(c, N(a)->ty)) {
				pp_span r = rng(c, a);

				report(c, "error", r.b, &r, 1, NULL, 0, "argument type '%s' is incomplete", tyname(c, N(a)->ty));
				fwd_note(c, N(a)->ty);
				a = err_node(c, N(a)->loc);
			}
			if (KIND(c, N(a)->ty) == TY_FLOAT)
				a = cast_node(c, a, prim(TY_DOUBLE), CK_F2F);
		}
		args.v[i] = a;
		aflags |= N(a)->flags;
	}
	if (N(callee)->op == N_VAR && c->syms[N(callee)->a].kind == S_FUNC && !c->unevaluated)
		nonnull_args(c, N(callee)->a, ft, args.v, args.n);
	if (fmt)
		format_check(c, fmt, args.v, aty, atd, args.n);
	if (!c->o->no_builtin && !lib_builtin_disabled(c->o, "strncmp", 7) && args.n == 3 &&
	    N(callee)->op == N_VAR && c->syms[N(callee)->a].kind == S_FUNC &&
	    (c->syms[N(callee)->a].flags & SF_GLOBAL) && c->syms[N(callee)->a].sc != SC_STATIC &&
	    !(c->syms[N(callee)->a].attrs && c->attr[c->syms[N(callee)->a].attrs].asmname)) {
		int nl;
		const char *nm = idname(c->syms[N(callee)->a].name, &nl);
		if (nl == 7 && !memcmp(nm, "strncmp", 7) && f->n == 3 &&
		    !(f->flags & (TF_NOPROTO | TF_VARIADIC)) && compatible(c, ft, sig_func_type(c, "iSSz"))) {
			uint32_t folded = fold_strncmp_zero(c, &args, loc);
			if (folded)
				return folded;
		}
	}
	if (!c->o->no_builtin && N(callee)->op == N_VAR && c->syms[N(callee)->a].kind == S_FUNC) {
		const sym *y = &c->syms[N(callee)->a];
		if ((y->flags & SF_GLOBAL) && y->sc != SC_STATIC && !(y->attrs && c->attr[y->attrs].asmname) &&
		    !lib_builtin_disabled(c->o, idstr_(y->name), idlen_(y->name))) {
			uint32_t result = fp_sign_call(c, idstr_(y->name), idlen_(y->name), ft, &args, loc);
			if (!result) result = fast_math_call(c, idstr_(y->name), idlen_(y->name), ft, &args, loc);
			if (result) {
				N(result)->flags |= aflags & NF_ERR;
				return result;
			}
		}
	}
	if (!c->o->no_builtin && args.n == 1 && N(callee)->op == N_VAR && c->syms[N(callee)->a].kind == S_FUNC &&
	    (c->syms[N(callee)->a].flags & SF_GLOBAL) && c->syms[N(callee)->a].sc != SC_STATIC &&
	    !(c->syms[N(callee)->a].attrs && c->attr[c->syms[N(callee)->a].attrs].asmname) &&
	    !lib_builtin_disabled(c->o, idstr_(c->syms[N(callee)->a].name), idlen_(c->syms[N(callee)->a].name))) {
		/* clang: abs, labs and llabs declared with their library types are builtins (inline) */
		int nl, k = KIND(c, T(c, ft)->base);
		const char *nm = idname(c->syms[N(callee)->a].name, &nl);
		int want = nl == 3 && !memcmp(nm, "abs", 3) ? TY_INT : nl == 4 && !memcmp(nm, "labs", 4) ? TY_LONG :
			   nl == 5 && !memcmp(nm, "llabs", 5) ? TY_LLONG : -1;

		if (k == want && f->n == 1 && KIND(c, c->params[f->aux]) == want) {
			qty t = prim(want);
			uint32_t pre, x = cplx_reuse(c, args.v[0], &pre);
			uint32_t neg = mk_bin(c, LEX_TOKEN_MINUS, t, mk_int(c, 0, t, loc), x, loc);
			uint32_t lt = mk_bin(c, LEX_TOKEN_LESS, prim(TY_INT), x, mk_int(c, 0, t, loc), loc);

			return with_pre(c, pre, conditional(c, lt, neg, x, loc));
		}
		if (k == TY_DOUBLE && f->n == 1 && !(f->flags & (TF_NOPROTO | TF_VARIADIC)) &&
		    KIND(c, c->params[f->aux]) == TY_DOUBLE && c->syms[N(callee)->a].sc != SC_STATIC) {
			uint32_t narrowed = narrow_math_call(c, nm, (size_t)nl, args.v[0], loc);
			if (narrowed)
				return narrowed;
		}
		/* clang folds the libm functions on a constant (double f(double)) */
		static const struct { const char *s; double (*f)(double); } mf[] = {
			{ "sin", sin }, { "cos", cos }, { "tan", tan }, { "exp", exp }, { "log", log }, { "sqrt", sqrt },
			{ "fabs", fabs }, { "floor", floor }, { "ceil", ceil }, { "log10", log10 }, { "atan", atan },
		};
		cval cv;

		if (k == TY_DOUBLE && f->n == 1 && KIND(c, c->params[f->aux]) == TY_DOUBLE &&
		    cc_eval(c, args.v[0], &cv) && cv.kind == CV_FLT)
			for (size_t q = 0; q < sizeof(mf) / sizeof(mf[0]); q++)
				if ((size_t)nl == strlen(mf[q].s) && !memcmp(nm, mf[q].s, (size_t)nl)) {
					double r = mf[q].f(cv.f);

					if (r == r && (q != 5 || cv.f >= 0) && (q != 4 && q != 9 ? 1 : cv.f > 0))
						return mk_flt(c, r, prim(TY_DOUBLE), loc);
				}
	}
	if (N(callee)->op == N_VAR) {
		const sym *fns = &c->syms[N(callee)->a];
		if (fns->attrs && c->attr[fns->attrs].interrupt)
			cc_error(c, loc, "interrupt service routine cannot be called directly");
	}
	n = mk(c, N_CALL, QUNQ(T(c, ft)->base), loc);
	N(n)->a = fn;
	N(n)->b = vfreeze(c, &args);
	N(n)->c = args.n;
	N(n)->flags |= aflags & NF_ERR;	/* clang: a call with an invalid argument is invalid */
	return n;
}
