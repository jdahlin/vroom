/* vcc front end: expressions: operators, members, primary and postfix */
#include <ctype.h>

#include "ccimpl.h"

/* ---------------- expressions: operators ---------------- */

VPRIV bool is_cmp(int op)
{
	return op == T_LESS || op == T_LESSEQ || op == T_GREATER || op == T_GREATEREQ ||
	       op == T_EQUALEQUAL || op == T_EXCLAIMEQ;
}

/* clang -pedantic: arithmetic on void * (GNU: element size 1) */
static inline void void_ptr_arith(cc *c, qty pt, uint32_t loc, bool two)
{
	static const char *const g[] = { "pointer-arith", "gnu", "pedantic", NULL };

	if (unlikely(KIND(c, T(c, pt)->base) == TY_SVE)) {
		cc_error(c, loc, "arithmetic on a pointer to sizeless type '%s'", tyname(c, T(c, pt)->base));
		return;
	}
	if (unlikely(KIND(c, T(c, pt)->base) == TY_VOID))
		EXT_WARN(c, "gnu-pointer-arith", g, loc,
			 two ? "arithmetic on pointers to void is a GNU extension" : "arithmetic on a pointer to void is a GNU extension");
}

/* clang (GCC vector splat): the scalar s may become the element type et without truncation */
static bool vec_scalar_ok(cc *c, uint32_t s, qty et)
{
	qty st = QUNQ(N(s)->ty);
	cval v;
	bool k;

	/* (the type before the integer promotion: short, char) */
	if (N(s)->op == N_CAST && N(s)->aux == CK_INT && !c->span[s].e && is_integer(c, N(N(s)->a)->ty))
		st = QUNQ(N(N(s)->a)->ty);
	if (KIND(c, st) == TY_ENUM)
		st = T(c, st)->base;
	memset(&v, 0, sizeof v);
	k = eval(c, s, &v) && (v.kind == CV_INT || v.kind == CV_FLT);
	if (is_integer(c, et)) {
		unsigned b = (unsigned)ty_size(c, et) * 8;

		if (is_float(c, st))
			return false;
		if (k && v.kind == CV_INT) {
			if (is_signed(c, et))
				return v.i >= -((i128)1 << (b - 1)) && v.i < ((i128)1 << (b - 1));
			return v.i >= -((i128)1 << (b - 1)) && (b >= 128 || v.i < ((i128)1 << b));	/* (clang: -18 into unsigned lanes) */
		}
		return ty_size(c, st) < ty_size(c, et) || (ty_size(c, st) == ty_size(c, et) && is_signed(c, st) == is_signed(c, et));
	}
	if (k && v.kind == CV_INT)
		return ty_size(c, et) == 4 ? (i128)(float)v.i == v.i : (i128)(double)v.i == v.i;
	if (k && v.kind == CV_FLT && !v.ld)
		return ty_size(c, et) != 4 || (double)(float)v.f == v.f;
	if (is_float(c, st))
		return ty_size(c, st) <= ty_size(c, et);
	return ty_size(c, st) * 8 <= (ty_size(c, et) == 4 ? 24u : 53u);
}

/* the scalar operand s of a GNU vector operation (vt) at the operator loc: false after the error */
static bool vec_scalar_check(cc *c, uint32_t s, qty vt, uint32_t loc)
{
	if (T(c, vt)->ext || !is_arith(c, N(s)->ty) || vec_scalar_ok(c, s, T(c, vt)->base))
		return true;
	cc_error(c, loc, "cannot convert between scalar type '%s' and vector type '%s' as implicit conversion would cause truncation",
		 tyname(c, QUNQ(N(s)->ty)), tyname(c, vt));
	return false;
}

/* ext_vector_type swizzles: v.x, v.xyz, v.s01, v.hi/.lo/.even/.odd (clang's OpenCL components) */
static int swz_idx(char ch, int set)
{
	static const char *const sets[2] = { "xyzw", "rgba" };
	const char *q = strchr(sets[set], ch);

	return ch && q ? (int)(q - sets[set]) : -1;
}

static void swz_range_error(cc *c, uint32_t loc, qty vt)
{
	pp_span r = { loc, pp_tok_end(c->p, c->prevloc) };	/* (loc: the '.', the name just consumed) */

	report(c, "error", loc, &r, 1, NULL, 0, "vector component access exceeds type '%s'", tyname(c, vt));
}

static uint32_t swizzle(cc *c, uint32_t n, uint32_t id, uint32_t loc)
{
	qty vt = QUNQ(N(n)->ty), et = T(c, vt)->base;
	int ne = T(c, vt)->ext, k = 0, ix[16];
	const char *s = idstr_(id);
	int len = idlen_(id);

	if (len == 2 && !memcmp(s, "hi", 2)) {
		for (int i = 0; i < (ne + 1) / 2; i++) ix[k++] = (ne + 1) / 2 + i < ne ? (ne + 1) / 2 + i : -1;
	} else if (len == 2 && !memcmp(s, "lo", 2)) {
		for (int i = 0; i < (ne + 1) / 2; i++) ix[k++] = i;
	} else if ((len == 4 && !memcmp(s, "even", 4)) || (len == 3 && !memcmp(s, "odd", 3))) {
		for (int i = len == 3; i < ne && k < 16; i += 2) ix[k++] = i;
	} else if ((s[0] == 's' || s[0] == 'S') && len > 1) {
		for (int i = 1; i < len && k < 16; i++) {
			int d = isdigit((unsigned char)s[i]) ? s[i] - '0' : isxdigit((unsigned char)s[i]) ? (s[i] | 32) - 'a' + 10 : 99;

			if (d >= ne) {
				swz_range_error(c, loc, vt);
				return mk_int(c, 0, et, loc);
			}
			ix[k++] = d;
		}
	} else {
		int set = swz_idx(s[0], 0) >= 0 ? 0 : 1;

		for (int i = 0; i < len && k < 16; i++) {
			int d = swz_idx(s[i], set);

			if (d < 0) {
				cc_error(c, loc, "illegal vector component name '%c'", s[i]);
				return mk_int(c, 0, et, loc);
			}
			if (d >= ne) {
				swz_range_error(c, loc, vt);
				return mk_int(c, 0, et, loc);
			}
			ix[k++] = d;
		}
	}
	if (k == 1) {	/* one lane: v[i] (an lvalue when v is one) */
		uint32_t b;

		if (ix[0] < 0)
			return mk_int(c, 0, et, loc);
		if (N(n)->flags & NF_LVALUE)
			b = cast_node(c, addr_of(c, n, loc), ty_ptr(c, QT(QI(et), QQ(N(n)->ty))), CK_P2P);
		else
			b = cast_node(c, rvalue(c, n), ty_ptr(c, et), CK_VADDR);
		b = mk2(c, N_INDEX, T(c, N(b)->ty)->base, loc, rvalue(c, b), mk_int(c, ix[0], prim(TY_LONG), loc));
		N(b)->flags = NF_LVALUE;
		return b;
	}
	{	/* several: a shuffle into an ext vector of k lanes (an rvalue; vcc: not assignable) */
		uint64_t es = ty_size(c, et), p = 1;
		ctype rv;
		vec args;
		uint32_t a = rvalue(c, n);
		qty rt;

		while (p < (uint64_t)k * es)
			p <<= 1;
		rv = *T(c, ty_vector(c, et, (uint32_t)p));
		rv.ext = (uint16_t)k;
		rt = QT(ty_intern(c, &rv), 0);
		vinit(&args);
		vpush(c, &args, a);
		vpush(c, &args, a);
		for (uint64_t i = 0; i < p / es; i++)
			vpush(c, &args, mk_int(c, i < (uint64_t)k ? ix[i] : -1, prim(TY_INT), loc));
		return mk_builtin(c, BI_SHUFFLEVECTOR, rt, loc, &args);
	}
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
			bad_operands(c, op, l, r, loc);
		else if (!both && !shift && !vec_scalar_check(c, is_vector(c, lt) ? r : l, vt, loc))
			return mk_bin(c, op, is_cmp(op) ? vec_mask_type(c, vt) : vt, l, l, loc);	/* (stands in; diagnosed) */
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
				bad_operands(c, op, l, r, loc);
			return mk_bin(c, op, lt, l, r, loc);
		}
		if ((op == T_PERCENT || op == T_AMP || op == T_PIPE || op == T_CARET) &&
		    (!is_integer(c, lt) || !is_integer(c, rt)))
			bad_operands(c, op, l, r, loc);
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
		void_ptr_arith(c, lt, loc, false);
		r = convert(c, r, prim(TY_LONG));
		return mk_bin(c, op, lt, l, r, loc);
	}
	if (op == T_MINUS && is_ptr(c, lt) && is_ptr(c, rt)) {
		void_ptr_arith(c, lt, loc, true);
		return mk_bin(c, op, c->t_ptrdiff, l, r, loc);
	}
	if (is_cmp(op) && (is_ptr(c, lt) || is_ptr(c, rt))) {
		qty ot = is_ptr(c, lt) ? rt : lt;

		if (!is_ptr(c, ot) && !is_integer(c, ot) && KIND(c, ot) != TY_ENUM) {	/* (p == 0.5) */
			bad_operands(c, op, l, r, loc);
			return err_node(c, loc);
		}
		if (!is_ptr(c, lt))
			l = convert(c, l, rt);
		else if (!is_ptr(c, rt))
			r = convert(c, r, lt);
		return mk_bin(c, op, prim(TY_INT), l, r, loc);
	}
	bad_operands(c, op, l, r, loc);
	return err_node(c, loc);
}

VPRIV uint32_t assign(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	qty lt = QUNQ(ty_unatomic(c, N(l)->ty));
	uint32_t n;

	if (unlikely(!need_lvalue(c, l, loc)))
		return err_node(c, loc);	/* clang: the assignment is invalid */
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
			void_ptr_arith(c, lt, loc, false);
			r = convert(c, r, prim(TY_LONG));
			pt = lt;
		} else if (is_vector(c, lt)) {
			if (is_vector(c, rt) ? T(c, lt)->size != T(c, rt)->size : !is_arith(c, rt))
				cc_error(c, loc, "invalid operands to compound assignment");
			else if (!is_vector(c, rt) && op != T_LESSLESS && op != T_GREATERGREATER)
				vec_scalar_check(c, r, QUNQ(lt), loc);
			r = convert(c, r, is_vector(c, rt) ? QUNQ(lt) : T(c, lt)->base);
			pt = QUNQ(lt);
		} else if (op == T_LESSLESS || op == T_GREATERGREATER) {
			/* computation type is the promoted lhs */
		} else if (is_arith(c, pt) && is_arith(c, rt)) {
			pt = arith_conv(c, pt, rt);
			r = convert(c, r, pt);
		} else {
			cc_error(c, loc, "invalid operands to compound assignment");
			return err_node(c, loc);
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
	} else if ((is_ptr(c, at) && is_integer(c, bt)) || (is_ptr(c, bt) && is_integer(c, at))) {
		bool pa = is_ptr(c, at);

		if (!is_npc(c, pa ? rb : ra)) {	/* (clang: not for a null pointer constant) */
			pp_span r[2] = { rng(c, ra), rng(c, rb) };
			char an[256];

			snprintf(an, sizeof an, "%s", tyname(c, at));
			cc_warnx(c, "conditional-type-mismatch", true, NULL, loc, r, 2, NULL, 0,
				 "pointer/integer type mismatch in conditional expression ('%s' and '%s')", an, tyname(c, bt));
		}
		t = pa ? at : bt;
		if (pa)
			rb = convert(c, rb, t);
		else
			ra = convert(c, ra, t);
	} else if ((is_record(c, at) || is_vector(c, at)) && compatible(c, at, bt)) {
		t = QUNQ(at);
	} else {
		cc_error(c, loc, "incompatible operand types in conditional expression");
		return err_node(c, loc);
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
		return err_node(c, loc);
	}
	ti = T(c, bt)->aux;
	if (!(c->tags[ti].flags & RF_COMPLETE)) {
		if (N(base)->op == N_VAR && (c->syms[N(base)->a].flags & SF_INVALID))
			return err_node(c, loc);	/* (already reported) */
		{
			pp_span r = rng(c, base);

			report(c, "error", loc, &r, 1, NULL, 0, "incomplete definition of type '%s'", tyname(c, bt));
		}
		fwd_note(c, bt);
		return err_node(c, loc);
	}
	d = find_member(c, ti, name, path, 0);
	if (d < 0) {
		pp_span r = rng(c, base);

		report(c, "error", c->prevloc, &r, 1, NULL, 0, "no member named '%.*s' in '%s'", IDF(name), tyname(c, bt));
		return err_node(c, loc);
	}
	for (int i = 0; i < d; i++) {
		const field *f = &c->tags[ti].f[path[i]];
		uint32_t m = mk(c, N_MEMBER, qualify(c, f->ty, QQ(bt)), loc);

		if (unlikely(f->invalid))
			return err_node(c, loc);

		N(m)->a = base;
		N(m)->b = ti;
		N(m)->c = path[i];
		N(m)->flags = (uint8_t)((N(base)->flags & NF_LVALUE) | (f->bitfield ? NF_BITFIELD : 0));
		base = m;
		bt = N(m)->ty;
		if (unlikely(f->depr) && i == d - 1)
			deprecated_use(c, c->prevloc, f->name, &c->attr[f->depr], f->loc);
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
				uint32_t m;

				if (unlikely(f->invalid))
					return err_node(c, loc);
				if (unlikely(f->depr))
					deprecated_use(c, c->prevloc, f->name, &c->attr[f->depr], f->loc);
				m = mk(c, N_MEMBER, qualify(c, f->ty, QQ(bt)), loc);

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
		return err_node(c, loc);
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
	if (!(N(n)->flags & NF_LVALUE) && KIND(c, t) != TY_FUNC && N(n)->op != N_COMPLIT) {
		pp_span sp = rng(c, n);

		report(c, "error", loc, &sp, 1, NULL, 0, "cannot take the address of an rvalue of type '%s'", tyname(c, t));
	}
	{
		uint32_t b = n;

		while (N(b)->op == N_MEMBER)
			b = N(b)->a;
		if (N(b)->op == N_VAR && c->syms[N(b)->a].sc == SC_REGISTER && c->syms[N(b)->a].kind == S_VAR) {
			pp_span sp = rng(c, n);

			report(c, "error", loc, &sp, 1, NULL, 0, "address of register variable requested");
		}
	}
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
	uint32_t open = expect_lparen(c);

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
	expect_match(c, T_RPAREN, open);
	if (!chosen)
		chosen = def;
	if (!chosen) {
		pp_span r = rng(c, ctl);

		if (!ISERR(ctl))	/* (clang: nothing more for an invalid operand; the result is invalid) */
			report(c, "error", N(ctl)->loc, &r, 1, NULL, 0,
			       "controlling expression type '%s' not compatible with any generic association type", tyname(c, ct));
		return err_node(c, loc);
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

	if (unlikely(y->flags & SF_DEPRECATED) && !(y->flags & SF_OVERLOAD)) {	/* an overload set: the member a call picks */
		deprecated_use(c, loc, id, &c->attr[y->attrs], y->loc);
		y = &c->syms[s];
	}
	if (y->kind == S_ENUMCONST) {
		uint32_t n = mk_int(c, trunc_to(c, y->val, y->ty), y->ty, loc);

		if (N(n)->op == N_NUM)
			N(n)->aux |= NUM_ENUM;
		return n;
	}
	if (unlikely(y->kind == S_TYPEDEF))
		fatal(c, loc, "unexpected type name '%.*s': expected expression", IDF(id));
	{
		uint32_t n = mk(c, N_VAR, y->ty, loc);

		N(n)->a = s;
		N(n)->flags = y->kind == S_VAR ? NF_LVALUE : 0;
		if (unlikely(y->flags & SF_INVALID))
			N(n)->flags |= NF_ERR;
		/* (clang: a function's references from its own body do not use it) */
		if ((!c->unevaluated || c->bcp) && !((y->flags & SF_OVERLOAD) && is(c, T_LPAREN)) && s != c->fn)
			y->flags |= SF_USED;
		if (unlikely(y->flags & SF_UVPEND)) {
			if (tok(c)->kind == T_PERIOD)
				y->flags &= ~SF_UVPEND;	/* s.m: initializes it for the analysis */
			else if (tok(c)->kind != T_EQUAL) {
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

/* C99: a call to an undeclared function; for one a standard header declares (POSIX's write,
   open, ...) vcc adds where it is, as help with the #include fix-it */
static __attribute__((cold, noinline)) void implicit_call_error(cc *c, uint32_t id, uint32_t loc)
{
	const char *h = lib_header(c, id);
	vdiag *d;

	pp_diag_pc = __builtin_return_address(0);
	error_limit(c), c->errors++, c->ndiag++;
	d = pp_diag_begin(c->p, VDL_ERROR, loc, "call to undeclared function '%.*s'; ISO C99 and later do not support "
			  "implicit function declarations", IDF(id));
	pp_diag_flag(d, "implicit-function-declaration");
	c23_word_help(c, d, id);
	if (h) {
		pp_diag_help(c->p, d, "'%.*s' is declared in <%s>", IDF(id), h);
		include_fix(c, d, loc, h);
	}
	pp_diag_emit(c->p, d);
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
		uint32_t ls = lib_func(c, id, loc);
		qty lt;

		if (ls) {
			uint32_t n = mk(c, N_VAR, c->syms[ls].ty, loc);

			N(n)->a = ls;
			c->syms[ls].flags |= SF_USED;
			return n;
		}
		lt = implicit_lib_type(c, id);
		if (!c->o->implicit_fn && lt)
			cc_error(c, loc, "call to undeclared library function '%.*s' with type '%s'; ISO C99 and later do not support implicit function declarations [-Wimplicit-function-declaration]", IDF(id), tyname(c, lt));
		else if (!c->o->implicit_fn)
			implicit_call_error(c, id, loc);
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
		uint32_t s = undeclared(c, "use of undeclared identifier", id, loc, false, 0, NULL);

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
	bool fh = c->for_head;

	c->for_head = false;
	b = compound_stmt(c, &last);
	c->for_head = fh;
	c->in_stmtexpr--;
	expect_match(c, T_RPAREN, loc);
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
		static const char *const g[] = { "gnu-statement-expression", "gnu", "pedantic", NULL };
		bool mac = tok(c)->flags & PF_MACRO;

		if (!(c->prevflags & PF_MACRO))
			EXT_WARN(c, "gnu-statement-expression", w_gnu, tok(c)->loc, "use of GNU statement expression extension");
		else if (unlikely(ext_on(c, "gnu-statement-expression-from-macro-expansion", g))) {
			c->dms = "{";	/* (clang: the '(' from a macro; the caret at the invocation) */
			ext_warn(c, "gnu-statement-expression-from-macro-expansion", g, tok(c)->loc,
				 "use of GNU statement expression extension from macro expansion");
		}
		n = stmt_expr(c, loc);
		if (mac)
			N(n)->flags |= NF_MACRO;	/* (no -Wunused-value) */
		return n;
	}
	n = expr(c);
	if (likely(is(c, T_RPAREN)) && !ISERR(n)) {
		/* the parentheses around it, as distances (no location fix-up in the caches) */
		uint32_t b = c->span[n].e ? c->span[n].b : N(n)->loc, e = c->span[n].e ? c->span[n].e : N(n)->loc, rp = tok(c)->loc;

		if (b >= loc && b - loc < 65536 && rp >= e && rp - e < 65536)
			c->paren[n] = (struct cparen){ (uint16_t)(b - loc), (uint16_t)(rp - e) };
	}
	expect_close(c, T_RPAREN, loc, n);
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

			if (quiet_rt(c))	/* (clang: DiagRuntimeBehavior, at the body's end) */
				continue;
			rt_defer(c, "nonnull", NULL, pn || idx ? r.b : call_end, &r, 1, NULL, 0,
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
	    (c->syms[N(callee)->a].flags & SF_GLOBAL) && c->syms[N(callee)->a].sc != SC_STATIC) {
		int nl;
		const char *nm = idname(c->syms[N(callee)->a].name, &nl);
		if (nl == 7 && !memcmp(nm, "strncmp", 7) && f->n == 3 &&
		    !(f->flags & (TF_NOPROTO | TF_VARIADIC)) && compatible(c, ft, sig_func_type(c, "iSSz"))) {
			uint32_t folded = fold_strncmp_zero(c, &args, loc);
			if (folded)
				return folded;
		}
	}
	if (!c->o->no_builtin && args.n == 1 && N(callee)->op == N_VAR && c->syms[N(callee)->a].kind == S_FUNC &&
	    (c->syms[N(callee)->a].flags & SF_GLOBAL) &&
	    !lib_builtin_disabled(c->o, idstr_(c->syms[N(callee)->a].name), idlen_(c->syms[N(callee)->a].name))) {
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
	n = mk(c, N_CALL, QUNQ(T(c, ft)->base), loc);
	N(n)->a = fn;
	N(n)->b = vfreeze(c, &args);
	N(n)->c = args.n;
	N(n)->flags |= aflags & NF_ERR;	/* clang: a call with an invalid argument is invalid */
	return n;
}

/* the postfix operators after an error node: parsed, nothing more said */
static __attribute__((noinline, cold)) uint32_t postfix_err(cc *c, uint32_t n)
{
	for (;;) {
		switch (tok(c)->kind) {
		case T_LSQUARE:
			next(c);
			expr(c);
			expect(c, T_RSQUARE, "']'");
			break;
		case T_LPAREN: {
			vec args;

			args_list(c, &args);
			break;
		}
		case T_PERIOD: case T_ARROW:
			next(c);
			if (!is(c, T_IDENT))
				fatal(c, tok(c)->loc, "expected identifier");
			next(c);
			break;
		case T_PLUSPLUS: case T_MINUSMINUS:
			next(c);
			break;
		default:	/* an error node of no type a conversion would look at (decay drops the flag) */
			return err_node(c, N(n)->loc);
		}
	}
}

VPRIV __attribute__((noinline)) uint32_t postfix_ops(cc *c, uint32_t n)
{
	uint32_t b0 = c->span[n].e ? c->span[n].b : N(n)->loc;

	for (;; setspan(c, n, b0)) {
		uint32_t loc = tok(c)->loc;

		if (unlikely(ISERR(n)))
			return postfix_err(c, n);
		switch (tok(c)->kind) {
		case T_LSQUARE: {
			uint32_t ix, r;
			qty at, it;

			next(c);
			ix = expr(c);
			expect(c, T_RSQUARE, "']'");
			if (unlikely(ISERR(ix))) {
				n = err_node(c, loc);
				continue;
			}
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

				if (!is_ptr(c, at))
					report(c, "error", loc, r, 2, NULL, 0, "subscripted value is not an array, pointer, or vector");
				else
					report(c, "error", loc, r, 2, NULL, 0, "array subscript is not an integer");
				n = err_node(c, loc);
				continue;
			}
			if (is_func(c, T(c, at)->base)) {
				pp_span r = rng(c, n);

				report(c, "error", r.b, &r, 1, NULL, 0, "subscript of pointer to function type '%s'", tyname(c, T(c, at)->base));
				n = err_node(c, loc);
				continue;
			}
			if (!is_void(c, T(c, at)->base) && !complete(c, T(c, at)->base)) {
				pp_span r = rng(c, n);

				report(c, "error", loc, &r, 1, NULL, 0, "subscript of pointer to incomplete type '%s'", tyname(c, T(c, at)->base));
				fwd_note(c, T(c, at)->base);
				n = err_node(c, loc);
				continue;
			}
			if (KIND(c, T(c, at)->base) == TY_SVE)
				void_ptr_arith(c, at, loc, false);
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
				uint32_t id = tok(c)->id, nloc = tok(c)->loc;

				next(c);
				if (is_vector(c, N(n)->ty) && T(c, N(n)->ty)->ext) {
					uint32_t b = c->span[n].e ? c->span[n].b : N(n)->loc;

					n = swizzle(c, n, id, loc);
					(void)nloc;
					setspan(c, n, b);
					continue;
				}
				if (is_ptr(c, N(n)->ty) && is_record(c, T(c, N(n)->ty)->base)) {
					/* clang: fix-it '->', then go on as if it were there */
					pp_span r = rng(c, n);
					pp_fixit fx = { loc, loc + 1, "->", VDA_MACHINE };
					pp_span b = c->span[n];

					c->dms = ".";
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
					pp_fixit fx = { loc, loc + 2, ".", VDA_MACHINE };

					c->dms = "->";
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
			if (is_ptr(c, N(n)->ty) && KIND(c, T(c, N(n)->ty)->base) == TY_SVE)
				void_ptr_arith(c, N(n)->ty, loc, false);
			r = mk(c, op, QUNQ(ty_unatomic(c, N(n)->ty)), loc);
			N(r)->a = n;
			n = r;
			continue;
		}
		default:
			return n;
		}
	}
}
