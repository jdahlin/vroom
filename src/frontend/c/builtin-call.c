/* vcc front end: typing of a builtin call, builtin_call's one switch by id; see builtin-table.c for where a builtin's other facts live */
#include "ccimpl.h"

VPRIV uint32_t args_list(cc *c, vec *args)
{
	uint32_t open = tok(c)->loc;

	vinit(args);
	expect(c, T_LPAREN, "'('");
	if (!is(c, T_RPAREN)) {
		do
			vpush(c, args, assign_expr(c));
		while (accept(c, T_COMMA));
	}
	c->args_rp = tok(c)->loc;
	if (unlikely(!is(c, T_RPAREN))) {
		/* clang: a call without its ')' is invalid; the last argument says so */
		if (args->n && ISERR(args->v[args->n - 1]))
			skip_close(c, T_RPAREN);
		else
			expect_match_fail(c, T_RPAREN, open);
		if (args->n)
			args->v[args->n - 1] = err_node(c, open);
		else
			vpush(c, args, err_node(c, open));
	} else {
		next(c);
	}
	return args->n;
}

/* t without qualifiers, down through array element types (clang's unqualified array type) */
static qty unqual_array(cc *c, qty t)
{
	const ctype *x;

	t = QUNQ(t);
	if (!is_array(c, t))
		return t;
	x = T(c, t);
	if (x->flags & TF_VLA)
		return t;
	{
		qty e = x->base, ue = unqual_array(c, e);
		int fl = x->flags & TF_INCOMPLETE;

		return ue == e ? t : ty_array(c, ue, fl ? 0 : arr_len(c, t), fl);
	}
}


VPRIV uint32_t mk_builtin(cc *c, int id, qty ty, uint32_t loc, const vec *args)
{
	/* A counted pointer has a runtime extent even if its allocation is
	   unknown. Read the sibling count at the query, not at declaration.
	   Restrict this lowering to a stable base so no operand effects are lost. */
	if (id == BI_DYNAMIC_OBJECT_SIZE && args->n == 2) {
		uint32_t m = args->v[0];
		cval mode;
		while (N(m)->op == N_CAST)
			m = N(m)->a;
		if (N(m)->op == N_MEMBER && is_ptr(c, N(m)->ty) &&
		    !elementwise_abs_effects(c, args->v[0], true) && eval(c, args->v[1], &mode) &&
		    mode.kind == CV_INT && mode.i >= 0 && mode.i <= 2) {
			uint32_t count = node_field(c, N(m))->counted_by;
			const tag *r = &c->tags[N(m)->b];
			for (uint32_t i = 0; count && i < r->nfield; i++) {
				if (r->f[i].name != count || !is_integer(c, r->f[i].ty))
					continue;
				uint32_t base = N(m)->a;
				uint64_t size = ty_size(c, T(c, N(m)->ty)->base);
				if (!size) break;
				uint32_t cnt = convert(c, rvalue(c, member(c, base, count, loc)), c->t_size);
				return mk_bin(c, T_STAR, c->t_size, cnt, mk_int(c, size, c->t_size, loc), loc);
			}
		}
	}
	uint32_t n = mk(c, N_BUILTIN, ty, loc);

	N(n)->aux = (uint16_t)id;
	if (c->fn && (id == BI_OBJECT_SIZE || id == BI_DYNAMIC_OBJECT_SIZE))
		c->syms[c->fn].flags |= SF_BOS_QUERY;
	N(n)->b = vfreeze(c, args);
	N(n)->c = args->n;
	return n;
}

VPRIV uint32_t offsetof_expr(cc *c, uint32_t loc)
{
	qty t;
	uint32_t res;
	i128 off = 0;
	uint32_t dyn = 0;	/* non-constant part (node), 0 if none */

	expect(c, T_LPAREN, "'('");
	t = type_name(c);
	expect(c, T_COMMA, "','");
	for (bool first = true;; first = false) {
		if (first || accept(c, T_PERIOD)) {
			uint32_t path[FM_DEPTH], ti;
			int d;

			if (!is(c, T_IDENT))
				fatal(c, tok(c)->loc, "expected identifier");
			if (!is_record(c, t)) {
				cc_error(c, tok(c)->loc, "offsetof requires struct, union, or class type, '%s' invalid", tyname(c, t));
				next(c);
				break;
			}
			ti = T(c, t)->aux;
			d = find_member(c, ti, tok(c)->id, path, 0);
			if (d < 0) {
				cc_error(c, tok(c)->loc, "no member named '%.*s' in '%s'", IDF(tok(c)->id), tyname(c, t));
				next(c);
				break;
			}
			for (int i = 0; i < d; i++) {
				const field *f = &c->tags[ti].f[path[i]];

				off += (i128)(f->off / 8);
				t = f->ty;
				if (is_record(c, t))
					ti = T(c, t)->aux;
			}
			next(c);
		} else if (accept(c, T_LSQUARE)) {
			uint32_t ix = expr(c);
			i128 k;

			expect(c, T_RSQUARE, "']'");
			if (!is_array(c, t)) {
				cc_error(c, loc, "subscripted value is not an array ('%s' invalid)", tyname(c, t));
				break;
			}
			t = T(c, t)->base;
			if (const_int(c, ix, &k)) {
				off += k * (i128)ty_size(c, t);
			} else {
				uint32_t sz = mk_int(c, (i128)ty_size(c, t), c->t_size, loc);
				uint32_t term = mk_bin(c, T_STAR, c->t_size, convert(c, rvalue(c, ix), c->t_size), sz, loc);

				dyn = dyn ? mk_bin(c, T_PLUS, c->t_size, dyn, term, loc) : term;
			}
		} else {
			break;
		}
	}
	expect_here(c, T_RPAREN, "')'");
	res = mk_int(c, off, c->t_size, loc);
	if (dyn)
		res = mk_bin(c, T_PLUS, c->t_size, res, dyn, loc);
	return res;
}

VPRIV uint32_t builtin_call(cc *c, const builtin *b, uint32_t loc)
{
	vec args;
	qty rt = sigtype(c, b->sig[0]);

	switch (b->id) {
	case BI_OS_LOG_FORMAT: case BI_OS_LOG_SIZE:
		return os_log_call(c, b->id, loc);
	case BI_ATOMIC_LOCK_FREE:
		return atomic_lock_free_call(c, b, loc);
	case BI_STDC_ROTL: case BI_STDC_ROTR:
		return stdc_rotate(c, b, loc);
	case BI_ELEMENTWISE_ABS:
		return elementwise_abs_call(c, b, loc);
	case BI_ISFPCLASS: case BI_ISSUBNORMAL: case BI_ISZERO: {
		i128 mask = b->id == BI_ISSUBNORMAL ? 144 : 96;
		unsigned nargs = b->id == BI_ISFPCLASS ? 2 : 1;
		uint32_t pre;

		args_list(c, &args);
		if (args.n != nargs) {
			arity(c, loc, nargs, &args);
			return err_node(c, loc);
		}
		if (!is_float(c, N(args.v[0])->ty)) {
			cc_error(c, N(args.v[0])->loc, "floating point classification requires argument of floating point type (passed in '%s')",
			         tyname(c, N(args.v[0])->ty));
			return err_node(c, loc);
		}
		if (b->id == BI_ISFPCLASS && !const_int(c, args.v[1], &mask)) {
			cc_error(c, loc, "argument to '%s' must be a constant integer", b->name);
			return err_node(c, loc);
		}
		if (mask < 0 || mask > 1023) {
			cc_error(c, loc, "argument value %lld is outside the valid range [0, 1023]", (long long)mask);
			return err_node(c, loc);
		}
		uint32_t a = cplx_reuse(c, rvalue(c, args.v[0]), &pre);
		vec unary;
		vinit(&unary);
		vpush(c, &unary, rvalue(c, a));
		uint32_t sign = mk_builtin(c, BI_SIGNBIT, prim(TY_INT), loc, &unary);
		/* Each pair is the negative/positive membership bit, except NaNs,
		 * whose class is determined by the quiet bit rather than their sign. */
		const int bits[5][2] = {{0,1}, {2,9}, {3,8}, {4,7}, {5,6}};
		uint32_t member[5];
		for (int i = 0; i < 5; i++) {
			uint32_t neg = mk_int(c, !!(mask & ((i128)1 << bits[i][0])), prim(TY_INT), loc);
			uint32_t pos = mk_int(c, !!(mask & ((i128)1 << bits[i][1])), prim(TY_INT), loc);
			uint32_t cond = i ? sign : mk_builtin(c, BI_ISSIGNALING, prim(TY_INT), loc, &unary);
			member[i] = conditional(c, cond, neg, pos, loc);
		}
		uint32_t n = conditional(c, a, member[3], member[4], loc);
		const int ids[] = {BI_ISNAN, BI_ISINF, BI_ISNORMAL};
		for (int i = 2; i >= 0; i--)
			n = conditional(c, mk_builtin(c, ids[i], prim(TY_INT), loc, &unary), member[i], n, loc);
		return with_pre(c, pre, n);
	}
	case BI_FPCLASSIFY: {
		uint32_t saved[6], pre[6];

		args_list(c, &args);
		if (args.n != 6) {
			arity(c, loc, 6, &args);
			return err_node(c, loc);
		}
		if (!is_float(c, N(args.v[5])->ty)) {
			cc_error(c, N(args.v[5])->loc, "floating point classification requires argument of floating point type (passed in '%s')",
			         tyname(c, N(args.v[5])->ty));
			return err_node(c, loc);
		}
		for (int i = 0; i < 6; i++) {
			uint32_t a = rvalue(c, args.v[i]);
			if (i < 5)
				a = convert(c, a, prim(TY_INT));
			saved[i] = cplx_reuse(c, a, &pre[i]);
		}
		/* Match clang's classification tree: save the operand once, test zero,
		 * then NaN and infinity, and finally distinguish normal/subnormal.
		 * A class code is evaluated as its corresponding test is reached. */
		vec unary;
		vinit(&unary);
		vpush(c, &unary, rvalue(c, saved[5]));
		uint32_t n = conditional(c, mk_builtin(c, BI_ISNORMAL, prim(TY_INT), loc, &unary), saved[2], saved[3], loc);
		n = with_pre(c, pre[2], with_pre(c, pre[3], n));
		const int ids[] = {BI_ISNAN, BI_ISINF};
		for (int i = 1; i >= 0; i--)
			n = with_pre(c, pre[i], conditional(c, mk_builtin(c, ids[i], prim(TY_INT), loc, &unary), saved[i], n, loc));
		n = with_pre(c, pre[4], conditional(c, saved[5], n, saved[4], loc));
		return with_pre(c, pre[5], n);
	}
	case BI_CHOOSE: {
		uint32_t cond, x, y;
		i128 v = 0;

		expect(c, T_LPAREN, "'('");
		cond = assign_expr(c);
		expect(c, T_COMMA, "','");
		x = assign_expr(c);
		expect(c, T_COMMA, "','");
		y = assign_expr(c);
		expect_here(c, T_RPAREN, "')'");
		if (!const_int(c, decay(c, cond), &v))
			cc_error(c, N(cond)->loc, "'__builtin_choose_expr' requires a constant expression");
		return v ? x : y;
	}
	case BI_TYPES_COMPAT: {
		qty a, t;

		expect(c, T_LPAREN, "'('");
		a = type_name(c);
		expect(c, T_COMMA, "','");
		t = type_name(c);
		expect_here(c, T_RPAREN, "')'");
		return mk_int(c, compatible(c, unqual_array(c, a), unqual_array(c, t)), prim(TY_INT), loc);
	}
	case BI_OFFSETOF:
		return offsetof_expr(c, loc);
	case BI_VA_ARG: {
		uint32_t ap, n, first_bound;
		qty t;

		expect(c, T_LPAREN, "'('");
		ap = assign_expr(c);
		expect(c, T_COMMA, "','");
		first_bound = c->nnode;
		t = type_name(c);
		expect_here(c, T_RPAREN, "')'");
		n = mk(c, N_VAARG, t, loc);
		N(n)->a = ap;
		return capture_vla_bounds(c, t, first_bound, n, loc);
	}
	case BI_CONSTANT_P: {
		uint32_t n;

		c->unevaluated++, c->bcp++;	/* (clang: what it names is used) */
		args_list(c, &args);
		c->unevaluated--, c->bcp--;
		if (args.n != 1)
			arity(c, loc, 1, &args);
		for (uint32_t i = 0; i < args.n; i++)
			args.v[i] = decay(c, args.v[i]);
		n = mk_builtin(c, b->id, prim(TY_INT), loc, &args);
		return n;
	}
	case BI_ATOMIC_LOAD_N: case BI_ATOMIC_STORE_N: case BI_ATOMIC_EXCHANGE_N: case BI_ATOMIC_CMPXCHG_N:
	case BI_ATOMIC_LOAD: case BI_ATOMIC_STORE: case BI_ATOMIC_EXCHANGE: case BI_ATOMIC_CMPXCHG:
	case BI_ATOMIC_FETCH_ADD: case BI_ATOMIC_FETCH_SUB: case BI_ATOMIC_FETCH_AND: case BI_ATOMIC_FETCH_OR:
	case BI_ATOMIC_FETCH_XOR: case BI_ATOMIC_ADD_FETCH: case BI_ATOMIC_SUB_FETCH: case BI_ATOMIC_AND_FETCH:
	case BI_ATOMIC_OR_FETCH: case BI_ATOMIC_XOR_FETCH: case BI_ATOMIC_FETCH_NAND: case BI_ATOMIC_FETCH_MAX:
	case BI_ATOMIC_FETCH_MIN: case BI_ATOMIC_NAND_FETCH: case BI_ATOMIC_MAX_FETCH: case BI_ATOMIC_MIN_FETCH:
	case BI_ATOMIC_TEST_AND_SET: case BI_ATOMIC_CLEAR:
	case BI_SYNC_VAL_CAS: case BI_SYNC_BOOL_CAS: case BI_SYNC_LOCK_RELEASE: case BI_SYNC_SYNCHRONIZE:
		return atomic_call(c, b, loc);
	case BI_COMPLEX: {
		args_list(c, &args);
		if (args.n != 2) {
			arity(c, loc, 2, &args);
			return err_node(c, loc);
		}
		for (int i = 0; i < 2; i++) {
			args.v[i] = rvalue(c, args.v[i]);
			if (!is_float(c, N(args.v[i])->ty)) {
				cc_error(c, N(args.v[i])->loc, "argument type '%s' is not a real floating point type", tyname(c, N(args.v[i])->ty));
				return err_node(c, loc);
			}
		}
		qty t = N(args.v[0])->ty;
		if (QI(canon(c, t)) != QI(canon(c, N(args.v[1])->ty))) {
			cc_error(c, loc, "arguments are of different types ('%s' vs '%s')", tyname(c, t), tyname(c, N(args.v[1])->ty));
			return err_node(c, loc);
		}
		/* Both arguments are evaluated even if a surrounding __real__ or
		   __imag__ expression only extracts one component. */
		uint32_t p0, p1;
		uint32_t re = cplx_reuse(c, args.v[0], &p0), im = cplx_reuse(c, args.v[1], &p1);
		return with_pre(c, p0, with_pre(c, p1, mk_cplx(c, ty_complex(c, t), re, im, loc)));
	}
	case BI_CONJ: case BI_CREAL: case BI_CIMAG: {
		qty ct = ty_complex(c, sigtype(c, b->sig[0]));
		uint32_t a;

		args_list(c, &args);
		if (args.n != 1) {
			arity(c, loc, 1, &args);
			return mk_int(c, 0, prim(TY_INT), loc);
		}
		a = convert(c, rvalue(c, args.v[0]), ct);
		if (b->id == BI_CONJ)
			return cplx_unary(c, T_TILDE, a, loc);
		return rvalue(c, cplx_part(c, a, b->id == BI_CIMAG));
	}
	case BI_CLZG: case BI_CTZG: case BI_POPCOUNTG:
		return generic_bitcount(c, b->id, loc);
	case BI_COUNTED_BY_REF: {
		uint32_t m;

		args_list(c, &args);
		m = args.n ? args.v[0] : 0;
		if (m && N(m)->op == N_MEMBER && node_field(c, N(m))->counted_by) {
			uint32_t cnt = member(c, N(m)->a, node_field(c, N(m))->counted_by, loc);

			return addr_of(c, cnt, loc);
		}
		return cast_node(c, mk_int(c, 0, prim(TY_INT), loc), ty_ptr(c, prim(TY_VOID)), CK_NULL);
	}
	default:
		break;
	}
	args_list(c, &args);
	if (b->id == BI_VA_START && !strcmp(b->name, "__builtin_c23_va_start") && args.n > 2) {
		arity(c, loc, 2, &args);
		return err_node(c, loc);
	}
	{
		qty ft = builtin_type(c, b);
		const ctype *f = T(c, ft);
		uint32_t np = f->n;

		for (uint32_t i = 0; i < args.n; i++) {
			uint32_t a = decay(c, args.v[i]);

			if (i < np)
				a = convert(c, a, c->params[f->aux + i]);
			else if (b->id == BI_LIB) {
				/* the variable part of __builtin_printf and the _chk
				   forms: default argument promotions, as a call */
				a = rvalue(c, a);
				if (KIND(c, N(a)->ty) == TY_FLOAT)
					a = cast_node(c, a, prim(TY_DOUBLE), CK_F2F);
			}
			args.v[i] = a;
		}
		if (args.n < np || (args.n > np && !(f->flags & TF_VARIADIC)))
			arity(c, loc, np, &args);
		if (b->id == BI_X86_PCMPSTR)
			return x86_pcmpstr_call(c, b, &args, rt, loc);
		if (b->id == BI_X86_SIMD)
			return x86_simd_call(c, b, &args, rt, loc);
		switch (b->id) {
		case BI_ADD_OVERFLOW: case BI_SUB_OVERFLOW: case BI_MUL_OVERFLOW:
			if (args.n != 3 || !is_ptr(c, N(args.v[2])->ty))
				cc_error(c, args.n == 3 ? N(args.v[2])->loc : loc, "result argument to overflow builtin must be a pointer to a non-const integer type ('%s' invalid)",
					 args.n == 3 ? tyname(c, N(args.v[2])->ty) : "void");
			break;
		case BI_LONGJMP: {
			i128 value;
			if (args.n == 2 && (!const_int(c, args.v[1], &value) || value != 1))
				cc_error(c, N(args.v[1])->loc, "second argument to '__builtin_longjmp' must be the constant integer 1");
			break;
		}
		case BI_ADDC: case BI_SUBC:
			if (args.n != 4 || !is_ptr(c, N(args.v[3])->ty))
				cc_error(c, loc, "carry-out argument to builtin must be a pointer");
			break;
		case BI_ISNAN: case BI_ISINF: case BI_ISFINITE: case BI_SIGNBIT: case BI_ISNORMAL:
		case BI_ISINF_SIGN: case BI_FPCLASSIFY:
			break;
		case BI_ISSIGNALING:
			if (args.n != 1) {
				arity(c, loc, 1, &args);
				return err_node(c, loc);
			}
			if (!is_float(c, N(args.v[0])->ty)) {
				cc_error(c, c->prevloc, "floating point classification requires argument of floating point type (passed in '%s')",
				         tyname(c, N(args.v[0])->ty));
				return err_node(c, loc);
			}
			break;
		}
		if (b->id == BI_STRLEN || b->id == BI_NAN || b->id == BI_NANF || b->id == BI_NANS) {
			/* Fold known strings/payloads; otherwise emit the library call. */
			if (args.n != 1)
				return err_node(c, loc);
			uint32_t n = mk_builtin(c, b->id, rt, loc, &args);
			cval cv;

			if (cc_eval(c, n, &cv) && (cv.kind == CV_INT || cv.kind == CV_FLT))
				return n;
		}
		if (b->id == BI_LIB && !c->o->no_builtin && !lib_builtin_disabled(c->o, "strncmp", 7) &&
		    !strcmp(b->name, "__builtin_strncmp")) {
			uint32_t folded = fold_strncmp_zero(c, &args, loc);
			if (folded)
				return folded;
		}
		if (b->id == BI_LIB) {
			uint32_t result = fp_sign_call(c, b->name + 10, strlen(b->name + 10), ft, &args, loc);
			if (!result) result = fast_math_call(c, b->name + 10, strlen(b->name + 10), ft, &args, loc);
			if (result) return result;
		}
		if (b->id == BI_LIB || b->id == BI_STRLEN || b->id == BI_NAN || b->id == BI_NANF || b->id == BI_NANS) {
			/* a call to the library function the builtin names */
			uint32_t s = sym_new(c, S_BUILTIN, 0, ft, loc), fn, n;

			c->syms[s].val = b - builtin_all();
			fn = mk(c, N_VAR, ft, loc);
			N(fn)->a = s;
			n = mk(c, N_CALL, rt, loc);
			N(n)->a = decay(c, fn);
			N(n)->b = vfreeze(c, &args);
			N(n)->c = args.n;
			return n;
		}
		return mk_builtin(c, b->id, rt, loc, &args);
	}
}
