/* vcc front end: typing of a builtin call, cc_builtin_call_build's one switch by id; see builtin-table.c for where a builtin's other facts live */
#include "cc-internal.h"

CC_PRIVATE uint32_t cc_builtin_arguments_parse(cc *c, cc_id_list *args)
{
	uint32_t open = tok(c)->loc;

	cc_id_list_init(args);
	cc_token_expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
	if (!cc_token_is(c, LEX_TOKEN_RIGHT_PAREN)) {
		do
			cc_id_list_append(c, args, cc_assignment_parse(c));
		while (cc_token_accept(c, LEX_TOKEN_COMMA));
	}
	c->args_rp = tok(c)->loc;
	if (unlikely(!cc_token_is(c, LEX_TOKEN_RIGHT_PAREN))) {
		/* clang: a call without its ')' is invalid; the last argument says so */
		if (args->n && AST_NODE_IS_ERROR(args->v[args->n - 1]))
			cc_recovery_skip_close(c, LEX_TOKEN_RIGHT_PAREN);
		else
			cc_recovery_expect_match_fail(c, LEX_TOKEN_RIGHT_PAREN, open);
		if (args->n)
			args->v[args->n - 1] = cc_node_error_new(c, open);
		else
			cc_id_list_append(c, args, cc_node_error_new(c, open));
	} else {
		next(c);
	}
	return args->n;
}

/* t without qualifiers, down through array element types (clang's unqualified array type) */
static qty cc_type_unqualified_array(cc *c, qty t)
{
	const ctype *x;

	t = QUNQ(t);
	if (!cc_type_is_array(c, t))
		return t;
	x = AST_TYPE(c, t);
	if (x->flags & TF_VLA)
		return t;
	{
		qty e = x->base, ue = cc_type_unqualified_array(c, e);
		int fl = x->flags & TF_INCOMPLETE;

		return ue == e ? t : cc_type_array(c, ue, fl ? 0 : cc_type_array_length(c, t), fl);
	}
}


CC_PRIVATE uint32_t cc_builtin_node_new(cc *c, int id, qty ty, uint32_t loc, const cc_id_list *args)
{
	/* A counted pointer has a runtime extent even if its allocation is
	   unknown. Read the sibling count at the query, not at declaration.
	   Restrict this lowering to a stable base so no operand effects are lost. */
	if (id == BI_DYNAMIC_OBJECT_SIZE && args->n == 2) {
		uint32_t m = args->v[0];
		cval mode;
		while (AST_NODE(m)->op == N_CAST)
			m = AST_NODE(m)->a;
		if (AST_NODE(m)->op == N_MEMBER && cc_type_is_pointer(c, AST_NODE(m)->ty) &&
		    !cc_builtin_has_effects(c, args->v[0], true) && cc_constant_eval(c, args->v[1], &mode) &&
		    mode.kind == CV_INT && mode.i >= 0 && mode.i <= 2) {
			uint32_t count = cc_node_member_field(c, AST_NODE(m))->counted_by;
			const tag *r = &c->tags[AST_NODE(m)->b];
			for (uint32_t i = 0; count && i < r->nfield; i++) {
				if (r->f[i].name != count || !cc_type_is_integer(c, r->f[i].ty))
					continue;
				uint32_t base = AST_NODE(m)->a;
				uint64_t size = cc_type_size(c, AST_TYPE(c, AST_NODE(m)->ty)->base);
				if (!size) break;
				uint32_t cnt = cc_conversion_apply(c, cc_node_rvalue(c, member(c, base, count, loc)), c->t_size);
				return cc_node_binary_new(c, LEX_TOKEN_STAR, c->t_size, cnt, cc_node_integer_new(c, size, c->t_size, loc), loc);
			}
		}
	}
	uint32_t n = cc_node_new(c, N_BUILTIN, ty, loc);

	AST_NODE(n)->aux = (uint16_t)id;
	if (c->fn && (id == BI_OBJECT_SIZE || id == BI_DYNAMIC_OBJECT_SIZE))
		c->syms[c->fn].flags |= SF_BOS_QUERY;
	AST_NODE(n)->b = cc_id_list_freeze(c, args);
	AST_NODE(n)->c = args->n;
	return n;
}

CC_PRIVATE uint32_t cc_builtin_offsetof_build(cc *c, uint32_t loc)
{
	qty t;
	uint32_t res;
	i128 off = 0;
	uint32_t dyn = 0;	/* non-constant part (node), 0 if none */

	cc_token_expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
	t = cc_type_name_parse(c);
	cc_token_expect(c, LEX_TOKEN_COMMA, "','");
	for (bool first = true;; first = false) {
		if (first || cc_token_accept(c, LEX_TOKEN_PERIOD)) {
			uint32_t path[CC_MEMBER_FIND_DEPTH_MAX], ti;
			int d;

			if (!cc_token_is(c, LEX_TOKEN_IDENTIFIER))
				cc_report_fatal(c, tok(c)->loc, "expected identifier");
			if (!cc_type_is_record(c, t)) {
				cc_report_error(c, tok(c)->loc, "offsetof requires struct, union, or class type, '%s' invalid", tyname(c, t));
				next(c);
				break;
			}
			ti = AST_TYPE(c, t)->aux;
			d = cc_member_find(c, ti, tok(c)->id, path, 0);
			if (d < 0) {
				cc_report_error(c, tok(c)->loc, "no member named '%.*s' in '%s'", CC_IDENTIFIER(tok(c)->id), tyname(c, t));
				next(c);
				break;
			}
			if (unlikely(c->field_uses))
				cc_field_uses_path_record(c, FU_OFFSETOF, ti, path, d);
			for (int i = 0; i < d; i++) {
				const field *f = &c->tags[ti].f[path[i]];

				off += (i128)(f->off / 8);
				t = f->ty;
				if (cc_type_is_record(c, t))
					ti = AST_TYPE(c, t)->aux;
			}
			next(c);
		} else if (cc_token_accept(c, LEX_TOKEN_LEFT_SQUARE)) {
			uint32_t ix = expr(c);
			i128 k;

			cc_token_expect(c, LEX_TOKEN_RIGHT_SQUARE, "']'");
			if (!cc_type_is_array(c, t)) {
				cc_report_error(c, loc, "subscripted value is not an array ('%s' invalid)", tyname(c, t));
				break;
			}
			t = AST_TYPE(c, t)->base;
			if (cc_constant_integer(c, ix, &k)) {
				off += k * (i128)cc_type_size(c, t);
			} else {
				uint32_t sz = cc_node_integer_new(c, (i128)cc_type_size(c, t), c->t_size, loc);
				uint32_t term = cc_node_binary_new(c, LEX_TOKEN_STAR, c->t_size, cc_conversion_apply(c, cc_node_rvalue(c, ix), c->t_size), sz, loc);

				dyn = dyn ? cc_node_binary_new(c, LEX_TOKEN_PLUS, c->t_size, dyn, term, loc) : term;
			}
		} else {
			break;
		}
	}
	cc_token_expect_here(c, LEX_TOKEN_RIGHT_PAREN, "')'");
	res = cc_node_integer_new(c, off, c->t_size, loc);
	if (dyn)
		res = cc_node_binary_new(c, LEX_TOKEN_PLUS, c->t_size, res, dyn, loc);
	return res;
}

CC_PRIVATE uint32_t cc_builtin_call_build(cc *c, const cc_builtin *b, uint32_t loc)
{
	cc_id_list args;
	qty rt = cc_builtin_signature_type(c, b->sig[0]);

	switch (b->id) {
	case BI_OS_LOG_FORMAT: case BI_OS_LOG_SIZE:
		return cc_builtin_os_log_build(c, b->id, loc);
	case BI_ATOMIC_LOCK_FREE:
		return cc_builtin_atomic_lock_free_build(c, b, loc);
	case BI_STDC_ROTL: case BI_STDC_ROTR:
		return cc_builtin_rotate_build(c, b, loc);
	case BI_ELEMENTWISE_ABS:
		return cc_builtin_elementwise_abs_build(c, b, loc);
	case BI_ISFPCLASS: case BI_ISSUBNORMAL: case BI_ISZERO: {
		i128 mask = b->id == BI_ISSUBNORMAL ? 144 : 96;
		unsigned nargs = b->id == BI_ISFPCLASS ? 2 : 1;
		uint32_t pre;

		cc_builtin_arguments_parse(c, &args);
		if (args.n != nargs) {
			cc_builtin_arity_check(c, loc, nargs, &args);
			return cc_node_error_new(c, loc);
		}
		if (!cc_type_is_float(c, AST_NODE(args.v[0])->ty)) {
			cc_report_error(c, AST_NODE(args.v[0])->loc, "floating point classification requires argument of floating point type (passed in '%s')",
			         tyname(c, AST_NODE(args.v[0])->ty));
			return cc_node_error_new(c, loc);
		}
		if (b->id == BI_ISFPCLASS && !cc_constant_integer(c, args.v[1], &mask)) {
			cc_report_error(c, loc, "argument to '%s' must be a constant integer", b->name);
			return cc_node_error_new(c, loc);
		}
		if (mask < 0 || mask > 1023) {
			cc_report_error(c, loc, "argument value %lld is outside the valid range [0, 1023]", (long long)mask);
			return cc_node_error_new(c, loc);
		}
		uint32_t a = cc_lowering_once(c, cc_node_rvalue(c, args.v[0]), &pre);
		cc_id_list unary;
		cc_id_list_init(&unary);
		cc_id_list_append(c, &unary, cc_node_rvalue(c, a));
		uint32_t sign = cc_builtin_node_new(c, BI_SIGNBIT, cc_type_basic(TY_INT), loc, &unary);
		/* Each pair is the negative/positive membership bit, except NaNs,
		 * whose class is determined by the quiet bit rather than their sign. */
		const int bits[5][2] = {{0,1}, {2,9}, {3,8}, {4,7}, {5,6}};
		uint32_t member[5];
		for (int i = 0; i < 5; i++) {
			uint32_t neg = cc_node_integer_new(c, !!(mask & ((i128)1 << bits[i][0])), cc_type_basic(TY_INT), loc);
			uint32_t pos = cc_node_integer_new(c, !!(mask & ((i128)1 << bits[i][1])), cc_type_basic(TY_INT), loc);
			uint32_t cond = i ? sign : cc_builtin_node_new(c, BI_ISSIGNALING, cc_type_basic(TY_INT), loc, &unary);
			member[i] = cc_conditional_build(c, cond, neg, pos, loc);
		}
		uint32_t n = cc_conditional_build(c, a, member[3], member[4], loc);
		const int ids[] = {BI_ISNAN, BI_ISINF, BI_ISNORMAL};
		for (int i = 2; i >= 0; i--)
			n = cc_conditional_build(c, cc_builtin_node_new(c, ids[i], cc_type_basic(TY_INT), loc, &unary), member[i], n, loc);
		return cc_lowering_sequence(c, pre, n);
	}
	case BI_FPCLASSIFY: {
		uint32_t saved[6], pre[6];

		cc_builtin_arguments_parse(c, &args);
		if (args.n != 6) {
			cc_builtin_arity_check(c, loc, 6, &args);
			return cc_node_error_new(c, loc);
		}
		if (!cc_type_is_float(c, AST_NODE(args.v[5])->ty)) {
			cc_report_error(c, AST_NODE(args.v[5])->loc, "floating point classification requires argument of floating point type (passed in '%s')",
			         tyname(c, AST_NODE(args.v[5])->ty));
			return cc_node_error_new(c, loc);
		}
		for (int i = 0; i < 6; i++) {
			uint32_t a = cc_node_rvalue(c, args.v[i]);
			if (i < 5)
				a = cc_conversion_apply(c, a, cc_type_basic(TY_INT));
			saved[i] = cc_lowering_once(c, a, &pre[i]);
		}
		/* Match clang's classification tree: save the operand once, test zero,
		 * then NaN and infinity, and finally distinguish normal/subnormal.
		 * A class code is evaluated as its corresponding test is reached. */
		cc_id_list unary;
		cc_id_list_init(&unary);
		cc_id_list_append(c, &unary, cc_node_rvalue(c, saved[5]));
		uint32_t n = cc_conditional_build(c, cc_builtin_node_new(c, BI_ISNORMAL, cc_type_basic(TY_INT), loc, &unary), saved[2], saved[3], loc);
		n = cc_lowering_sequence(c, pre[2], cc_lowering_sequence(c, pre[3], n));
		const int ids[] = {BI_ISNAN, BI_ISINF};
		for (int i = 1; i >= 0; i--)
			n = cc_lowering_sequence(c, pre[i], cc_conditional_build(c, cc_builtin_node_new(c, ids[i], cc_type_basic(TY_INT), loc, &unary), saved[i], n, loc));
		n = cc_lowering_sequence(c, pre[4], cc_conditional_build(c, saved[5], n, saved[4], loc));
		return cc_lowering_sequence(c, pre[5], n);
	}
	case BI_CHOOSE: {
		uint32_t cond, x, y;
		i128 v = 0;

		cc_token_expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
		cond = cc_assignment_parse(c);
		cc_token_expect(c, LEX_TOKEN_COMMA, "','");
		x = cc_assignment_parse(c);
		cc_token_expect(c, LEX_TOKEN_COMMA, "','");
		y = cc_assignment_parse(c);
		cc_token_expect_here(c, LEX_TOKEN_RIGHT_PAREN, "')'");
		if (!cc_constant_integer(c, cc_conversion_decay(c, cond), &v))
			cc_report_error(c, AST_NODE(cond)->loc, "'__builtin_choose_expr' requires a constant expression");
		return v ? x : y;
	}
	case BI_TYPES_COMPAT: {
		qty a, t;

		cc_token_expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
		a = cc_type_name_parse(c);
		cc_token_expect(c, LEX_TOKEN_COMMA, "','");
		t = cc_type_name_parse(c);
		cc_token_expect_here(c, LEX_TOKEN_RIGHT_PAREN, "')'");
		return cc_node_integer_new(c, cc_type_is_compatible(c, cc_type_unqualified_array(c, a), cc_type_unqualified_array(c, t)), cc_type_basic(TY_INT), loc);
	}
	case BI_OFFSETOF:
		return cc_builtin_offsetof_build(c, loc);
	case BI_VA_ARG: {
		uint32_t ap, n, first_bound;
		qty t;

		cc_token_expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
		ap = cc_assignment_parse(c);
		cc_token_expect(c, LEX_TOKEN_COMMA, "','");
		first_bound = c->nnode;
		t = cc_type_name_parse(c);
		cc_token_expect_here(c, LEX_TOKEN_RIGHT_PAREN, "')'");
		n = cc_node_new(c, N_VAARG, t, loc);
		AST_NODE(n)->a = ap;
		return cc_vla_bounds_capture(c, t, first_bound, n, loc);
	}
	case BI_CONSTANT_P: {
		uint32_t n;

		c->unevaluated++, c->bcp++;	/* (clang: what it names is used) */
		cc_builtin_arguments_parse(c, &args);
		c->unevaluated--, c->bcp--;
		if (args.n != 1)
			cc_builtin_arity_check(c, loc, 1, &args);
		for (uint32_t i = 0; i < args.n; i++)
			args.v[i] = cc_conversion_decay(c, args.v[i]);
		n = cc_builtin_node_new(c, b->id, cc_type_basic(TY_INT), loc, &args);
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
		return cc_builtin_atomic_build(c, b, loc);
	case BI_COMPLEX: {
		cc_builtin_arguments_parse(c, &args);
		if (args.n != 2) {
			cc_builtin_arity_check(c, loc, 2, &args);
			return cc_node_error_new(c, loc);
		}
		for (int i = 0; i < 2; i++) {
			args.v[i] = cc_node_rvalue(c, args.v[i]);
			if (!cc_type_is_float(c, AST_NODE(args.v[i])->ty)) {
				cc_report_error(c, AST_NODE(args.v[i])->loc, "argument type '%s' is not a real floating point type", tyname(c, AST_NODE(args.v[i])->ty));
				return cc_node_error_new(c, loc);
			}
		}
		qty t = AST_NODE(args.v[0])->ty;
		if (QI(canon(c, t)) != QI(canon(c, AST_NODE(args.v[1])->ty))) {
			cc_report_error(c, loc, "arguments are of different types ('%s' vs '%s')", tyname(c, t), tyname(c, AST_NODE(args.v[1])->ty));
			return cc_node_error_new(c, loc);
		}
		/* Both arguments are evaluated even if a surrounding __real__ or
		   __imag__ expression only extracts one component. */
		uint32_t p0, p1;
		uint32_t re = cc_lowering_once(c, args.v[0], &p0), im = cc_lowering_once(c, args.v[1], &p1);
		return cc_lowering_sequence(c, p0, cc_lowering_sequence(c, p1, cc_complex_node_new(c, cc_type_complex(c, t), re, im, loc)));
	}
	case BI_CONJ: case BI_CREAL: case BI_CIMAG: {
		qty ct = cc_type_complex(c, cc_builtin_signature_type(c, b->sig[0]));
		uint32_t a;

		cc_builtin_arguments_parse(c, &args);
		if (args.n != 1) {
			cc_builtin_arity_check(c, loc, 1, &args);
			return cc_node_integer_new(c, 0, cc_type_basic(TY_INT), loc);
		}
		a = cc_conversion_apply(c, cc_node_rvalue(c, args.v[0]), ct);
		if (b->id == BI_CONJ)
			return cc_complex_unary(c, LEX_TOKEN_TILDE, a, loc);
		return cc_node_rvalue(c, cc_complex_part(c, a, b->id == BI_CIMAG));
	}
	case BI_CLZG: case BI_CTZG: case BI_POPCOUNTG:
		return cc_builtin_bit_count_generic(c, b->id, loc);
	case BI_COUNTED_BY_REF: {
		uint32_t m;

		cc_builtin_arguments_parse(c, &args);
		m = args.n ? args.v[0] : 0;
		if (m && AST_NODE(m)->op == N_MEMBER && cc_node_member_field(c, AST_NODE(m))->counted_by) {
			uint32_t cnt = member(c, AST_NODE(m)->a, cc_node_member_field(c, AST_NODE(m))->counted_by, loc);

			return cc_address_of_build(c, cnt, loc);
		}
		return cc_cast_insert(c, cc_node_integer_new(c, 0, cc_type_basic(TY_INT), loc), cc_type_pointer(c, cc_type_basic(TY_VOID)), CK_NULL);
	}
	default:
		break;
	}
	cc_builtin_arguments_parse(c, &args);
	if (b->id == BI_VA_START && !strcmp(b->name, "__builtin_c23_va_start") && args.n > 2) {
		cc_builtin_arity_check(c, loc, 2, &args);
		return cc_node_error_new(c, loc);
	}
	{
		qty ft = cc_builtin_type(c, b);
		const ctype *f = AST_TYPE(c, ft);
		uint32_t np = f->n;

		for (uint32_t i = 0; i < args.n; i++) {
			uint32_t a = cc_conversion_decay(c, args.v[i]);

			if (i < np)
				a = cc_conversion_apply(c, a, c->params[f->aux + i]);
			else if (b->id == BI_LIB) {
				/* the variable part of __builtin_printf and the _chk
				   forms: default argument promotions, as a call */
				a = cc_node_rvalue(c, a);
				if (AST_TYPE_KIND(c, AST_NODE(a)->ty) == TY_FLOAT)
					a = cc_cast_insert(c, a, cc_type_basic(TY_DOUBLE), CK_F2F);
			}
			args.v[i] = a;
		}
		if (args.n < np || (args.n > np && !(f->flags & TF_VARIADIC)))
			cc_builtin_arity_check(c, loc, np, &args);
		if (b->id == BI_X86_PCMPSTR)
			return cc_builtin_x86_pcmpstr_build(c, b, &args, rt, loc);
		if (b->id == BI_X86_SIMD)
			return cc_builtin_x86_simd_build(c, b, &args, rt, loc);
		switch (b->id) {
		case BI_ADD_OVERFLOW: case BI_SUB_OVERFLOW: case BI_MUL_OVERFLOW:
			if (args.n != 3 || !cc_type_is_pointer(c, AST_NODE(args.v[2])->ty))
				cc_report_error(c, args.n == 3 ? AST_NODE(args.v[2])->loc : loc, "result argument to overflow builtin must be a pointer to a non-const integer type ('%s' invalid)",
					 args.n == 3 ? tyname(c, AST_NODE(args.v[2])->ty) : "void");
			break;
		case BI_LONGJMP: {
			i128 value;
			if (args.n == 2 && (!cc_constant_integer(c, args.v[1], &value) || value != 1))
				cc_report_error(c, AST_NODE(args.v[1])->loc, "second argument to '__builtin_longjmp' must be the constant integer 1");
			break;
		}
		case BI_ADDC: case BI_SUBC:
			if (args.n != 4 || !cc_type_is_pointer(c, AST_NODE(args.v[3])->ty))
				cc_report_error(c, loc, "carry-out argument to builtin must be a pointer");
			break;
		case BI_ISNAN: case BI_ISINF: case BI_ISFINITE: case BI_SIGNBIT: case BI_ISNORMAL:
		case BI_ISINF_SIGN: case BI_FPCLASSIFY:
			break;
		case BI_ISSIGNALING:
			if (args.n != 1) {
				cc_builtin_arity_check(c, loc, 1, &args);
				return cc_node_error_new(c, loc);
			}
			if (!cc_type_is_float(c, AST_NODE(args.v[0])->ty)) {
				cc_report_error(c, c->prevloc, "floating point classification requires argument of floating point type (passed in '%s')",
				         tyname(c, AST_NODE(args.v[0])->ty));
				return cc_node_error_new(c, loc);
			}
			break;
		}
		if (b->id == BI_STRLEN || b->id == BI_NAN || b->id == BI_NANF || b->id == BI_NANS) {
			/* Fold known strings/payloads; otherwise emit the library call. */
			if (args.n != 1)
				return cc_node_error_new(c, loc);
			uint32_t n = cc_builtin_node_new(c, b->id, rt, loc, &args);
			cval cv;

			if (cc_eval(c, n, &cv) && (cv.kind == CV_INT || cv.kind == CV_FLT))
				return n;
		}
		if (b->id == BI_LIB && !c->o->no_builtin && !cc_library_builtin_is_disabled(c->o, "strncmp", 7) &&
		    !strcmp(b->name, "__builtin_strncmp")) {
			uint32_t folded = cc_builtin_strncmp_zero_fold(c, &args, loc);
			if (folded)
				return folded;
		}
		if (b->id == BI_LIB) {
			uint32_t result = cc_builtin_fp_sign_build(c, b->name + 10, strlen(b->name + 10), ft, &args, loc);
			if (!result) result = cc_builtin_fast_math_build(c, b->name + 10, strlen(b->name + 10), ft, &args, loc);
			if (result) return result;
		}
		if (b->id == BI_LIB || b->id == BI_STRLEN || b->id == BI_NAN || b->id == BI_NANF || b->id == BI_NANS) {
			/* a call to the library function the builtin names */
			uint32_t s = cc_symbol_new(c, S_BUILTIN, 0, ft, loc), fn, n;

			c->syms[s].val = b - cc_builtin_table_get();
			fn = cc_node_new(c, N_VAR, ft, loc);
			AST_NODE(fn)->a = s;
			n = cc_node_new(c, N_CALL, rt, loc);
			AST_NODE(n)->a = cc_conversion_decay(c, fn);
			AST_NODE(n)->b = cc_id_list_freeze(c, &args);
			AST_NODE(n)->c = args.n;
			return n;
		}
		return cc_builtin_node_new(c, b->id, rt, loc, &args);
	}
}
