/* vcc front end: expressions: operators, members, primary and postfix */
#include "ccimpl.h"

/* ---------------- expressions: operators ---------------- */

VPRIV bool is_cmp(int op)
{
	return op == T_LESS || op == T_LESSEQ || op == T_GREATER || op == T_GREATEREQ ||
	       op == T_EQUALEQUAL || op == T_EXCLAIMEQ;
}

VPRIV uint32_t binop(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	qty lt, rt;

	if (op == T_AMPAMP || op == T_PIPEPIPE) {
		l = scalar_cond(c, decay(c, l), "logical operator");
		r = scalar_cond(c, decay(c, r), "logical operator");
		return mk2(c, op == T_AMPAMP ? N_LOGAND : N_LOGOR, prim(TY_INT), loc, l, r);
	}
	l = rvalue(c, l), r = rvalue(c, r);
	lt = N(l)->ty, rt = N(r)->ty;
	if (is_complex(c, lt) || is_complex(c, rt))
		return cplx_binop(c, op, l, r, loc);
	if (is_vector(c, lt) || is_vector(c, rt)) {
		/* lane-wise: two vectors of one size, or a vector and a scalar (each lane against it);
		 * comparisons give the signed integer vector of the same lanes (-1 / 0) */
		qty vt = QUNQ(is_vector(c, lt) ? lt : rt), et = T(c, vt)->base;
		bool both = is_vector(c, lt) && is_vector(c, rt);
		bool shift = op == T_LESSLESS || op == T_GREATERGREATER;
		qty res = vt;

		if ((both && T(c, lt)->size != T(c, rt)->size) ||
		    (!both && !is_arith(c, is_vector(c, lt) ? rt : lt)) ||
		    ((op == T_PERCENT || op == T_AMP || op == T_PIPE || op == T_CARET || shift) && !is_integer(c, et)))
			bad_operands(c, l, r, loc);
		if (both) {
			l = convert(c, l, vt);
			r = convert(c, r, both && shift ? QUNQ(rt) : vt);
		} else if (is_vector(c, lt)) {
			r = convert(c, r, et);
		} else {
			l = convert(c, l, et);
		}
		if (is_cmp(op))
			res = vec_mask_type(c, vt);
		return mk_bin(c, op, res, l, r, loc);
	}
	if (is_arith(c, lt) && is_arith(c, rt)) {
		qty ct;

		if (op == T_LESSLESS || op == T_GREATERGREATER) {
			if (!is_integer(c, lt) || !is_integer(c, rt))
				bad_operands(c, l, r, loc);
			return mk_bin(c, op, lt, l, r, loc);
		}
		if ((op == T_PERCENT || op == T_AMP || op == T_PIPE || op == T_CARET) &&
		    (!is_integer(c, lt) || !is_integer(c, rt)))
			bad_operands(c, l, r, loc);
		ct = arith_conv(c, lt, rt);
		l = convert(c, l, ct), r = convert(c, r, ct);
		return mk_bin(c, op, is_cmp(op) ? prim(TY_INT) : ct, l, r, loc);
	}
	if (op == T_PLUS && is_ptr(c, rt) && is_integer(c, lt)) {
		uint32_t t = l;

		l = r, r = t;
		lt = N(l)->ty, rt = N(r)->ty;
	}
	if ((op == T_PLUS || op == T_MINUS) && is_ptr(c, lt) && is_integer(c, rt)) {
		r = convert(c, r, prim(TY_LONG));
		return mk_bin(c, op, lt, l, r, loc);
	}
	if (op == T_MINUS && is_ptr(c, lt) && is_ptr(c, rt))
		return mk_bin(c, op, c->t_ptrdiff, l, r, loc);
	if (is_cmp(op) && (is_ptr(c, lt) || is_ptr(c, rt))) {
		if (!is_ptr(c, lt))
			l = convert(c, l, rt);
		else if (!is_ptr(c, rt))
			r = convert(c, r, lt);
		return mk_bin(c, op, prim(TY_INT), l, r, loc);
	}
	bad_operands(c, l, r, loc);
	return mk_bin(c, op, prim(TY_INT), l, r, loc);
}

VPRIV uint32_t assign(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	qty lt = QUNQ(N(l)->ty);
	uint32_t n;

	need_lvalue(c, l, loc);
	bounds_warn(c, l, false);
	r = decay(c, r);
	if (op && (is_complex(c, lt) || is_complex(c, N(r)->ty)))	/* l = l op r (l evaluated twice) */
		return assign(c, 0, l, cplx_binop(c, op, rvalue(c, l), rvalue(c, r), loc), loc);
	if (!op) {
		const field *bf = bitfield_of(c, l);

		if (bf)
			bitfield_const(c, bf, r, loc);
		r = convert_as(c, r, lt, CX_ASSIGN, loc);
		n = mk2(c, N_ASSIGN, lt, loc, l, r);
		return n;
	}
	{
		qty pt = promoted(c, l), rt;

		r = rvalue(c, r);
		rt = N(r)->ty;
		if (is_ptr(c, lt) && (op == T_PLUS || op == T_MINUS)) {
			r = convert(c, r, prim(TY_LONG));
			pt = lt;
		} else if (is_vector(c, lt)) {
			if (is_vector(c, rt) ? T(c, lt)->size != T(c, rt)->size : !is_arith(c, rt))
				cc_error(c, loc, "invalid operands to compound assignment");
			r = convert(c, r, is_vector(c, rt) ? QUNQ(lt) : T(c, lt)->base);
			pt = QUNQ(lt);
		} else if (op == T_LESSLESS || op == T_GREATERGREATER) {
			/* computation type is the promoted lhs */
		} else if (is_arith(c, pt) && is_arith(c, rt)) {
			pt = arith_conv(c, pt, rt);
			r = convert(c, r, pt);
		} else {
			cc_error(c, loc, "invalid operands to compound assignment");
		}
		n = mk2(c, N_ASSIGN, lt, loc, l, r);
		N(n)->aux = (uint16_t)op;
		N(n)->c = pt;	/* computation type */
		return n;
	}
}

/* the type of a ?: with these (decayed) arms */
VPRIV uint32_t conditional(cc *c, uint32_t cond, uint32_t a, uint32_t b, uint32_t loc)
{
	uint32_t ra, rb, n;
	qty at, bt, t;

	ra = a ? rvalue(c, a) : rvalue(c, cond);
	rb = rvalue(c, b);
	at = N(ra)->ty, bt = N(rb)->ty;
	if ((is_complex(c, at) || is_complex(c, bt)) && (is_arith(c, at) || is_complex(c, at)) &&
	    (is_arith(c, bt) || is_complex(c, bt))) {
		t = ty_complex(c, arith_conv(c, is_complex(c, at) ? cplx_elem(c, at) : at, is_complex(c, bt) ? cplx_elem(c, bt) : bt));
		ra = convert(c, ra, t), rb = convert(c, rb, t);
	} else if (is_arith(c, at) && is_arith(c, bt)) {
		t = arith_conv(c, at, bt);
		ra = convert(c, ra, t), rb = convert(c, rb, t);
	} else if (is_void(c, at) || is_void(c, bt)) {
		t = prim(TY_VOID);
	} else if (is_ptr(c, at) && is_ptr(c, bt)) {
		qty pa = T(c, at)->base, pb = T(c, bt)->base;
		int q = QQ(pa) | QQ(pb);

		if (is_npc(c, rb) && !is_npc(c, ra))
			t = at;
		else if (is_npc(c, ra) && !is_npc(c, rb))
			t = bt;
		else if (is_void(c, pa) || is_void(c, pb))
			t = ty_ptr(c, QT(TY_VOID, q));
		else if (compatible(c, pa, pb))
			t = ty_ptr(c, QT(QI(composite(c, pa, pb)), q));
		else
			t = ty_ptr(c, QT(TY_VOID, q));	/* clang: warning, void * */
		ra = convert(c, ra, t), rb = convert(c, rb, t);
	} else if (is_ptr(c, at) && is_integer(c, bt)) {
		t = at;
		rb = convert(c, rb, t);
	} else if (is_ptr(c, bt) && is_integer(c, at)) {
		t = bt;
		ra = convert(c, ra, t);
	} else if ((is_record(c, at) || is_vector(c, at)) && compatible(c, at, bt)) {
		t = QUNQ(at);
	} else {
		cc_error(c, loc, "incompatible operand types in conditional expression");
		t = at;
	}
	n = mk(c, N_COND, t, loc);
	N(n)->a = decay(c, cond);
	N(n)->b = a ? ra : 0;
	N(n)->c = rb;
	if (!a)
		N(n)->a = ra;	/* GNU ?: evaluates the condition once, as the value */
	return n;
}

/* ---------------- expressions: members ---------------- */

VPRIV int find_member(cc *c, uint32_t ti, uint32_t name, uint32_t *path, int depth)
{
	const tag *r = &c->tags[ti];

	if (depth >= FM_DEPTH)
		return -1;
	for (uint32_t i = 0; i < r->nfield; i++) {
		const field *f = &r->f[i];

		if (f->name == name) {
			path[depth] = i;
			return depth + 1;
		}
		if (!f->name && !f->bitfield && is_record(c, f->ty)) {
			int d = find_member(c, T(c, f->ty)->aux, name, path, depth + 1);

			if (d > 0) {
				path[depth] = i;
				return d;
			}
		}
	}
	return -1;
}

static __attribute__((noinline)) uint32_t member_slow(cc *c, uint32_t base, uint32_t name, uint32_t loc)
{
	qty bt = N(base)->ty;
	uint32_t path[FM_DEPTH], ti;
	int d;

	if (!is_record(c, bt)) {
		pp_span r[2] = { rng(c, base), { loc, pp_tok_end(c->p, c->prevloc) } };

		report(c, "error", loc, r, 2, NULL, 0, "member reference base type '%s' is not a structure or union", tyname(c, bt));
		return mk_int(c, 0, prim(TY_INT), loc);
	}
	ti = T(c, bt)->aux;
	if (!(c->tags[ti].flags & RF_COMPLETE)) {
		cc_error(c, loc, "incomplete definition of type");
		return mk_int(c, 0, prim(TY_INT), loc);
	}
	d = find_member(c, ti, name, path, 0);
	if (d < 0) {
		pp_span r = rng(c, base);

		report(c, "error", c->prevloc, &r, 1, NULL, 0, "no member named '%.*s' in '%s'", IDF(name), tyname(c, bt));
		return mk_int(c, 0, prim(TY_INT), loc);
	}
	for (int i = 0; i < d; i++) {
		const field *f = &c->tags[ti].f[path[i]];
		uint32_t m = mk(c, N_MEMBER, qualify(c, f->ty, QQ(bt)), loc);

		N(m)->a = base;
		N(m)->b = ti;
		N(m)->c = path[i];
		N(m)->flags = (uint8_t)((N(base)->flags & NF_LVALUE) | (f->bitfield ? NF_BITFIELD : 0));
		base = m;
		bt = N(m)->ty;
		if (is_record(c, bt))
			ti = T(c, bt)->aux;
	}
	return base;
}

/* base.name: a direct member found before any anonymous one is the common case */
VPRIV uint32_t member(cc *c, uint32_t base, uint32_t name, uint32_t loc)
{
	qty bt = N(base)->ty;
	const ctype *t = T(c, bt);

	if (likely(t->kind == TY_STRUCT || t->kind == TY_UNION) && likely(c->tags[t->aux].flags & RF_COMPLETE)) {
		const tag *r = &c->tags[t->aux];

		for (uint32_t i = 0; i < r->nfield; i++) {
			const field *f = &r->f[i];

			if (f->name == name) {
				uint32_t m = mk(c, N_MEMBER, qualify(c, f->ty, QQ(bt)), loc);

				N(m)->a = base;
				N(m)->b = t->aux;
				N(m)->c = i;
				N(m)->flags = (uint8_t)((N(base)->flags & NF_LVALUE) | (f->bitfield ? NF_BITFIELD : 0));
				return m;
			}
			if (!f->name && !f->bitfield && is_record(c, f->ty))
				break;	/* the name may be in an anonymous member: full search */
		}
	}
	return member_slow(c, base, name, loc);
}

VPRIV uint32_t deref(cc *c, uint32_t p, uint32_t loc)
{
	uint32_t n;
	qty t;

	p = rvalue(c, p);
	if (!is_ptr(c, N(p)->ty)) {
		pp_span r = rng(c, p);

		report(c, "error", loc, &r, 1, NULL, 0, "indirection requires pointer operand ('%s' invalid)", tyname(c, N(p)->ty));
		return p;
	}
	t = T(c, N(p)->ty)->base;
	n = mk(c, N_DEREF, t, loc);
	N(n)->a = p;
	N(n)->flags = KIND(c, t) == TY_FUNC ? 0 : NF_LVALUE;
	return n;
}

static void mark_addr_taken(cc *c, uint32_t n)
{
	while (N(n)->op == N_MEMBER || (N(n)->op == N_INDEX && 0))
		n = N(n)->a;
	if (N(n)->op == N_VAR)
		c->syms[N(n)->a].flags |= SF_ADDR_TAKEN;
}

VPRIV uint32_t addr_of(cc *c, uint32_t n, uint32_t loc)
{
	uint32_t r;
	qty t = N(n)->ty;

	if (bitfield_of(c, n))
		cc_error(c, loc, "address of bit-field requested");
	if (!(N(n)->flags & NF_LVALUE) && KIND(c, t) != TY_FUNC && N(n)->op != N_COMPLIT)
		cc_error(c, loc, "cannot take the address of an rvalue");
	mark_addr_taken(c, n);
	r = mk(c, N_ADDR, ty_ptr(c, t), loc);
	N(r)->a = n;
	return r;
}

/* ---------------- expressions: primary and postfix ---------------- */

static uint32_t generic_sel(cc *c, uint32_t loc)
{
	uint32_t ctl, chosen = 0, def = 0;
	qty ct;

	expect(c, T_LPAREN, "'('");
	c->unevaluated++;
	ctl = decay(c, assign_expr(c));
	c->unevaluated--;
	ct = QUNQ(N(ctl)->ty);	/* lvalue conversion: no qualifiers, no promotion */
	while (accept(c, T_COMMA)) {
		uint32_t e;

		if (iskw(c, KW_default)) {
			next(c);
			expect(c, T_COLON, "':'");
			e = assign_expr(c);
			def = e;
		} else {
			qty t = type_name(c);

			expect(c, T_COLON, "':'");
			e = assign_expr(c);
			if (!chosen && compatible(c, QUNQ(t), ct) && QQ(t) == 0)
				chosen = e;
		}
	}
	expect(c, T_RPAREN, "')'");
	if (!chosen)
		chosen = def;
	if (!chosen) {
		cc_error(c, loc, "controlling expression type not compatible with any generic association type");
		return mk_int(c, 0, prim(TY_INT), loc);
	}
	return chosen;
}

static uint32_t func_name(cc *c, uint32_t loc)
{
	const sym *f = c->fn ? &c->syms[c->fn] : NULL;
	int len = 0;
	const char *s = f ? idname(f->name, &len) : "";
	char *buf = halloc(c, (size_t)len + 1);
	uint32_t si = c->nstr++, n;

	memcpy(buf, s, (size_t)len);
	buf[len] = 0;
	c->strs[si] = (cstr){ buf, (uint32_t)len + 1, 1, T_STRING };
	n = mk(c, N_STR, ty_array(c, QT(TY_CHAR, Q_CONST), (uint64_t)len + 1, 0), loc);
	N(n)->a = si;
	N(n)->flags = NF_LVALUE;
	return n;
}

static __attribute__((noinline, cold, noreturn)) void bad_primary(cc *c, const ptok *t)
{
	fatal(c, t->loc, "expected expression");
	abort();
}

/* a name bound to s (already consumed) */
static inline __attribute__((always_inline)) uint32_t primary_sym(cc *c, uint32_t s, uint32_t id, uint32_t loc)
{
	sym *y = &c->syms[s];

	if (unlikely(y->flags & SF_DEPRECATED)) {
		deprecated_use(c, loc, id, &c->attr[y->attrs]);
		y = &c->syms[s];
	}
	if (y->kind == S_ENUMCONST)
		return mk_int(c, y->val, y->ty, loc);
	if (unlikely(y->kind == S_TYPEDEF))
		fatal(c, loc, "unexpected type name '%.*s': expected expression", IDF(id));
	{
		uint32_t n = mk(c, N_VAR, y->ty, loc);

		N(n)->a = s;
		N(n)->flags = y->kind == S_VAR ? NF_LVALUE : 0;
		if (!c->unevaluated && !((y->flags & SF_OVERLOAD) && is(c, T_LPAREN)))
			y->flags |= SF_USED;
		if (unlikely(y->flags & SF_UVPEND)) {
			if (tok(c)->kind != T_EQUAL) {
				y->flags &= ~SF_UVPEND;
				c->uv_any |= 4;	/* read (or &) before any assignment: run the analysis */
			} else if (!uv_deeper(c, s))
				c->uv_asg = s;	/* cleared after the right side */	/* (an assignment in a nested branch keeps it: -Wsometimes-uninitialized) */
		}
		y->flags |= SF_REF;
		y->nref += y->nref != 0xffff;
		if (unlikely(y->next) && y->depth)
			c->syms[y->next].flags |= SF_REF;	/* a block-scope extern: the global */
		return n;
	}
}

/* keywords that start a primary expression (the current token) */
static __attribute__((noinline)) uint32_t primary_kw(cc *c, int kw, uint32_t loc)
{
	if (kw == KW__Generic) {
		next(c);
		return generic_sel(c, loc);
	}
	if (kw == KW___builtin_va_arg) {
		static const builtin va = { "__builtin_va_arg", BI_VA_ARG, 0, "v" };

		next(c);
		return builtin_call(c, &va, loc);
	}
	if (kw == KW___builtin_offsetof) {
		next(c);
		return offsetof_expr(c, loc);
	}
	if (kw == KW___builtin_types_compatible_p) {
		static const builtin tc = { "", BI_TYPES_COMPAT, 1, "i" };

		next(c);
		return builtin_call(c, &tc, loc);
	}
	if (kw == KW_true || kw == KW_false) {	/* C23: constants of type bool */
		next(c);
		return mk_int(c, kw == KW_true, prim(TY_BOOL), loc);
	}
	if (kw == KW_nullptr) {	/* C23: a null pointer constant (nullptr_t is void * here) */
		next(c);
		return cast_node(c, mk_int(c, 0, prim(TY_INT), loc), ty_ptr(c, prim(TY_VOID)), CK_NULL);
	}
	if (kw == KW___builtin_shufflevector || kw == KW___builtin_convertvector) {
		next(c);
		return vector_builtin(c, kw == KW___builtin_shufflevector, loc);
	}
	if (kw == KW___builtin_choose_expr) {
		static const builtin ch = { "", BI_CHOOSE, 1, "i" };

		next(c);
		return builtin_call(c, &ch, loc);
	}
	bad_primary(c, tok(c));
}

/* an identifier without a binding (already consumed): builtins, __func__, errors */
static __attribute__((noinline)) uint32_t primary_unbound(cc *c, uint32_t id, uint32_t loc)
{
	{
		const builtin *b = builtin_of(id);

		if (b && is(c, T_LPAREN))
			return builtin_call(c, b, loc);
	}
	if (id == c->id_func || id == c->id_FUNCTION || id == c->id_PRETTY)
		return func_name(c, loc);
	{
		uint32_t s = is(c, T_LPAREN) ? lib_builtin(c, id, loc) : 0;

		if (s)
			return primary_sym(c, s, id, loc);
	}
	if (is(c, T_LPAREN)) {
		qty lt = implicit_lib_type(c, id);

		if (!c->o->implicit_fn && lt)
			cc_error(c, loc, "call to undeclared library function '%.*s' with type '%s'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]", IDF(id), tyname(c, lt));
		else if (!c->o->implicit_fn)
			cc_error(c, loc, "call to undeclared function '%.*s'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]", IDF(id));
		if (idlen_(id) == 6 && !memcmp(idstr_(id), "alloca", 6)) {
			/* clang: an implicitly declared library builtin is the builtin */
			uint32_t bid = vintern_find("__builtin_alloca", 16, vintern_hash("__builtin_alloca", 16));
			const builtin *b = bid ? builtin_of(bid) : NULL;

			if (b)
				return builtin_call(c, b, loc);
		}
		{
			/* clang: a library function gets its library type (asin(x) is a double) */
			uint32_t fs = lt ? implicit_lib_func(c, id, loc, lt) : implicit_func(c, id, loc);
			uint32_t n = mk(c, N_VAR, c->syms[fs].ty, loc);

			N(n)->a = fs;
			c->syms[fs].flags |= SF_USED;
			return n;
		}
	}
	{
		uint32_t s = undeclared(c, "use of undeclared identifier", id, loc, false);

		if (s)
			return primary_sym(c, s, id, loc);	/* clang recovers with the correction */
	}
	{
		uint32_t n = mk_int(c, 0, prim(TY_INT), loc);

		N(n)->flags |= NF_ERR | NF_LVALUE;
		return n;
	}
}

static __attribute__((noinline)) uint32_t stmt_expr(cc *c, uint32_t loc)
{
	qty last = prim(TY_VOID);
	uint32_t b, n;

	if (!c->fn)
		cc_error(c, loc, "statement expression not allowed at file scope");
	c->in_stmtexpr++;
	b = compound_stmt(c, &last);
	c->in_stmtexpr--;
	expect(c, T_RPAREN, "')'");
	n = mk(c, N_STMTEXPR, last, loc);
	N(n)->a = b;
	if (!c->in_stmtexpr)
		unused_result(c, n, false);	/* the statements before the value */
	return n;
}

/* ( expression ) or a GNU statement expression; '(' (at loc) is consumed */
VPRIV uint32_t paren_expr(cc *c, uint32_t loc)
{
	uint32_t n;

	if (is(c, T_LBRACE)) {
		bool mac = tok(c)->flags & PF_MACRO;

		n = stmt_expr(c, loc);
		if (mac)
			N(n)->flags |= NF_MACRO;	/* (no -Wunused-value) */
		return n;
	}
	n = expr(c);
	if (!is(c, T_RPAREN)) {
		report(c, "error", tok(c)->loc, NULL, 0, NULL, 0, "expected ')'");
		note(c, loc, "to match this '('");
		longjmp(c->bail, 1);
	}
	next(c);
	N(n)->flags |= NF_PAREN;
	return n;
}
static uint32_t primary_(cc *c);

VPRIV uint32_t primary(cc *c)
{
	uint32_t b = tok(c)->loc, n = primary_(c);

	setspan(c, n, b);
	return n;
}

static uint32_t primary_(cc *c)
{
	ptok *t = tok(c);
	uint32_t loc = t->loc;

	switch (t->kind) {
	case T_NUMBER: {
		uint32_t n = number(c, t);

		next(c);
		return n;
	}
	case T_CHAR: case T_WCHAR: case T_U8CHAR: case T_U16CHAR: case T_U32CHAR:
		return char_lit(c);
	case T_STRING: case T_WSTRING: case T_U8STRING: case T_U16STRING: case T_U32STRING:
		return string_lit(c);
	case T_LPAREN:
		next(c);
		return paren_expr(c, loc);
	case T_IDENT: {
		uint32_t id = t->id, s;
		int kw = kwof(t);

		if (unlikely(kw))
			return primary_kw(c, kw, loc);
		if (unlikely(c->o->std == 23 && idlen_(id) == 2 && !memcmp(idstr_(id), "u8", 2))) {
			/* C23 u8'a': the lexer gives u8 and 'a' (C17 tokens) */
			ptok *q = peek(c, 1);

			if (q->kind == T_CHAR && q->loc == loc + 2) {
				next(c);
				tok(c)->kind = T_U8CHAR, tok(c)->loc = loc;
				return char_lit(c);
			}
		}
		s = lookup(c, id);
		next(c);
		if (likely(s))
			return primary_sym(c, s, id, loc);
		return primary_unbound(c, id, loc);
	}
	default:
		bad_primary(c, t);
	}
}

/* overloadable: how well argument n converts to parameter type p (clang's C ranks:
   0 exact, 1 qualification, 2 promotion, 3 conversion, 4 incompatible pointer, 5 ellipsis;
   -1 not viable) */
static int ovl_rank(cc *c, uint32_t n, qty p)
{
	qty a = QUNQ(canon(c, N(n)->ty));
	int ak, pk;

	p = QUNQ(canon(c, p));
	if (is_array(c, a))
		a = ty_ptr(c, T(c, a)->base);
	else if (is_func(c, a))
		a = ty_ptr(c, a);
	if (compatible(c, a, p))
		return 0;
	ak = KIND(c, a), pk = KIND(c, p);
	if (ak == TY_PTR && pk == TY_PTR) {
		qty ea = T(c, a)->base, ep = T(c, p)->base;

		if (compatible(c, QUNQ(ea), QUNQ(ep)))
			return (QQ(ep) & QQ(ea)) == QQ(ea) ? 1 : 4;
		if ((is_void(c, ea) || is_void(c, ep)) && (QQ(ep) & QQ(ea)) == QQ(ea))
			return 3;
		return 4;
	}
	if (is_arith(c, a) && is_arith(c, p)) {
		if (QUNQ(promote(c, a)) == p || (ak == TY_FLOAT && pk == TY_DOUBLE))
			return 2;
		return 3;
	}
	if (pk == TY_PTR && is_npc(c, n))
		return 3;
	if (pk == TY_BOOL && ak == TY_PTR)
		return 3;
	return -1;
}

/* the member of fn's overload set that a call with args selects */
static __attribute__((noinline)) uint32_t ovl_pick(cc *c, uint32_t head, const vec *args, uint32_t loc)
{
	uint32_t best = 0, nbest = 0;
	int br[64];

	for (uint32_t s = head; s; s = c->syms[s].ovl) {
		const ctype *f = T(c, c->syms[s].ty);
		int r[64];
		bool ok = args->n <= 64 && (args->n == f->n || (args->n > f->n && (f->flags & TF_VARIADIC)));
		bool better = false, worse = false;

		for (uint32_t i = 0; ok && i < args->n; i++) {
			r[i] = i < f->n ? ovl_rank(c, args->v[i], c->params[f->aux + i]) : 5;
			ok = r[i] >= 0;
		}
		if (!ok)
			continue;
		if (!best) {
			best = s, nbest = 1;
			memcpy(br, r, args->n * sizeof(int));
			continue;
		}
		for (uint32_t i = 0; i < args->n; i++)
			better |= r[i] < br[i], worse |= r[i] > br[i];
		if (better && !worse) {
			best = s, nbest = 1;
			memcpy(br, r, args->n * sizeof(int));
		} else if (better == worse) {
			nbest++;
		}
	}
	if (!best) {
		cc_error(c, loc, "no matching function for call to '%.*s'", IDF(c->syms[head].name));
		return head;
	}
	if (nbest > 1)
		cc_error(c, loc, "call to '%.*s' is ambiguous", IDF(c->syms[head].name));
	return best;
}

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

			cc_warnx(c, "nonnull", true, NULL, pn || idx ? r.b : call_end, &r, 1, NULL, 0,
				 "null passed to a callee that requires a non-null argument");
		}
	}
}

static __attribute__((noinline)) uint32_t call(cc *c, uint32_t fn, uint32_t loc)
{
	uint32_t callee;
	vec args;
	qty ft;
	const ctype *f;
	uint32_t n, fmt;
	qty aty[64];
	uint32_t atd[64];
	bool have_args = false;

	if (N(fn)->op == N_VAR && (c->syms[N(fn)->a].flags & SF_OVERLOAD) && c->syms[N(fn)->a].kind == S_FUNC) {
		uint32_t s;

		args_list(c, &args);
		have_args = true;
		s = ovl_pick(c, N(fn)->a, &args, loc);
		N(fn)->a = s;
		N(fn)->ty = c->syms[s].ty;
		if (!c->unevaluated)
			c->syms[s].flags |= SF_USED;
		c->syms[s].flags |= SF_REF;
	}
	fn = rvalue(c, fn);
	callee = fn;
	ft = N(fn)->ty;
	if (!is_ptr(c, ft) || !is_func(c, T(c, ft)->base)) {
		pp_span r = rng(c, fn);

		report(c, "error", loc, &r, 1, NULL, 0, "called object type '%s' is not a function or function pointer", tyname(c, ft));
		if (!have_args)
			args_list(c, &args);
		return mk_int(c, 0, prim(TY_INT), loc);
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
		}
	}
	{
		uint32_t fs = N(callee)->op == N_VAR && c->syms[N(callee)->a].kind == S_FUNC ? N(callee)->a : 0;

		fmt = fs && c->syms[fs].attrs && c->attr[c->syms[fs].attrs].fmt && args.n <= 64 && !c->unevaluated ? fs : 0;
	}
	for (uint32_t i = 0; i < args.n; i++) {
		uint32_t a = args.v[i];

		f = T(c, ft);
		if (fmt) {
			int k = KIND(c, N(a)->ty);

			atd[i] = k == TY_ARRAY ? 0 : N(a)->op == N_VAR ? c->syms[N(a)->a].tdef :
				 N(a)->op == N_MEMBER && !(N(a)->flags & NF_BITFIELD) ? node_field(c, N(a))->tdef : 0;
			aty[i] = bitfield_of(c, a) ? 0 : k == TY_ARRAY ? ty_ptr(c, T(c, N(a)->ty)->base) : k == TY_FUNC ? 0 : QUNQ(N(a)->ty);
		}
		if (!(f->flags & TF_NOPROTO) && i < f->n) {
			c->cv_fn = N(callee)->op == N_VAR && c->syms[N(callee)->a].kind == S_FUNC ? N(callee)->a : 0;
			c->cv_arg = i;
			c->cv_callee = rng(c, callee).b;
			a = convert_as(c, decay(c, a), c->params[f->aux + i], CX_PASS, loc);
			c->cv_fn = 0;
		} else {
			/* default argument promotions */
			a = rvalue(c, a);
			if (KIND(c, N(a)->ty) == TY_FLOAT)
				a = cast_node(c, a, prim(TY_DOUBLE), CK_F2F);
		}
		args.v[i] = a;
	}
	if (N(callee)->op == N_VAR && c->syms[N(callee)->a].kind == S_FUNC && !c->unevaluated)
		nonnull_args(c, N(callee)->a, ft, args.v, args.n);
	if (fmt)
		format_check(c, fmt, args.v, aty, atd, args.n);
	if (!c->o->no_builtin && args.n == 1 && N(callee)->op == N_VAR && c->syms[N(callee)->a].kind == S_FUNC &&
	    (c->syms[N(callee)->a].flags & SF_GLOBAL)) {
		/* clang: abs, labs and llabs declared with their library types are builtins (inline) */
		int nl, k = KIND(c, T(c, ft)->base);
		const char *nm = idname(c->syms[N(callee)->a].name, &nl);
		int want = nl == 3 && !memcmp(nm, "abs", 3) ? TY_INT : nl == 4 && !memcmp(nm, "labs", 4) ? TY_LONG :
			   nl == 5 && !memcmp(nm, "llabs", 5) ? TY_LLONG : -1;

		if (k == want && f->n == 1 && KIND(c, c->params[f->aux]) == want) {
			qty t = prim(want);
			uint32_t pre, x = cplx_reuse(c, args.v[0], &pre);
			uint32_t neg = mk_bin(c, T_MINUS, t, mk_int(c, 0, t, loc), x, loc);
			uint32_t lt = mk_bin(c, T_LESS, prim(TY_INT), x, mk_int(c, 0, t, loc), loc);

			return with_pre(c, pre, conditional(c, lt, neg, x, loc));
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
	n = mk(c, N_CALL, QUNQ(T(c, ft)->base), loc);
	N(n)->a = fn;
	N(n)->b = vfreeze(c, &args);
	N(n)->c = args.n;
	return n;
}

VPRIV __attribute__((noinline)) uint32_t postfix_ops(cc *c, uint32_t n)
{
	uint32_t b0 = c->span[n].e ? c->span[n].b : N(n)->loc;

	for (;; setspan(c, n, b0)) {
		uint32_t loc = tok(c)->loc;

		switch (tok(c)->kind) {
		case T_LSQUARE: {
			uint32_t ix, r;
			qty at, it;

			next(c);
			ix = expr(c);
			expect(c, T_RSQUARE, "']'");
			if (is_vector(c, N(n)->ty) && (N(n)->flags & NF_LVALUE)) {
				/* v[i]: lane i of the vector object */
				qty et = QT(QI(T(c, N(n)->ty)->base), QQ(N(n)->ty));

				n = cast_node(c, addr_of(c, n, loc), ty_ptr(c, et), CK_P2P);
			} else if (is_vector(c, N(n)->ty)) {
				n = cast_node(c, rvalue(c, n), ty_ptr(c, T(c, N(n)->ty)->base), CK_VADDR);
			}
			n = rvalue(c, n), ix = rvalue(c, ix);
			at = N(n)->ty, it = N(ix)->ty;
			if (!is_ptr(c, at) && is_ptr(c, it)) {
				uint32_t t = n;

				n = ix, ix = t;
				at = N(n)->ty, it = N(ix)->ty;
			}
			if (!is_ptr(c, at) || !is_integer(c, it)) {
				pp_span r[2] = { rng(c, n), rng(c, ix) };

				report(c, "error", loc, r, 2, NULL, 0, "subscripted value is not an array, pointer, or vector");
				break;
			}
			r = mk2(c, N_INDEX, T(c, at)->base, loc, n, convert(c, ix, prim(TY_LONG)));
			N(r)->flags = NF_LVALUE;
			n = r;
			continue;
		}
		case T_LPAREN:
			n = call(c, n, loc);
			continue;
		case T_PERIOD:
			next(c);
			if (!is(c, T_IDENT))
				fatal(c, tok(c)->loc, "expected identifier");
			{
				uint32_t id = tok(c)->id;

				next(c);
				if (is_ptr(c, N(n)->ty) && is_record(c, T(c, N(n)->ty)->base)) {
					/* clang: fix-it '->', then go on as if it were there */
					pp_span r = rng(c, n);
					pp_fixit fx = { loc, loc + 1, "->" };
					pp_span b = c->span[n];

					report(c, "error", loc, &r, 1, &fx, 1, "member reference type '%s' is a pointer; did you mean to use '->'?",
					       tyname(c, N(n)->ty));
					n = deref(c, n, loc);
					if (b.e && !c->span[n].e)
						c->span[n] = b;
				}
				n = member(c, n, id, loc);
			}
			continue;
		case T_ARROW:
			next(c);
			if (!is(c, T_IDENT))
				fatal(c, tok(c)->loc, "expected identifier");
			{
				uint32_t id = tok(c)->id;

				next(c);
				if (is_record(c, N(n)->ty)) {
					/* clang: fix-it '.', then go on as if it were there */
					pp_span r[2] = { rng(c, n), { loc, loc + 2 } };
					pp_fixit fx = { loc, loc + 2, "." };

					report(c, "error", loc, r, 2, &fx, 1, "member reference type '%s' is not a pointer; did you mean to use '.'?",
					       tyname(c, N(n)->ty));
					n = member(c, n, id, loc);
				} else {
					pp_span b = c->span[n];

					n = deref(c, n, loc);
					if (b.e && !c->span[n].e)
						c->span[n] = b;
					n = member(c, n, id, loc);
				}
			}
			continue;
		case T_PLUSPLUS:
		case T_MINUSMINUS: {
			int op = tok(c)->kind == T_PLUSPLUS ? N_POSTINC : N_POSTDEC;
			uint32_t r;

			next(c);
			need_lvalue(c, n, loc);
			need_scalar(c, n, "increment");
			r = mk(c, op, QUNQ(N(n)->ty), loc);
			N(r)->a = n;
			n = r;
			continue;
		}
		default:
			return n;
		}
	}
}
