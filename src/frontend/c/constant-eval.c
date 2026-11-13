/* vcc front end: constant evaluation (integer constant expressions and foldable ones, as clang) */
#include "ccimpl.h"

VPRIV i128 trunc_to(cc *c, i128 v, qty t)
{
	int k = KIND(c, t);
	unsigned bits;

	if (k == TY_BOOL)
		return v != 0;
	if (k == TY_PTR)
		return (i128)(uint64_t)v;
	bits = int_bits(c, t);
	if (bits >= 128)
		return is_signed(c, t) ? v : (i128)(u128)v;
	{
		u128 m = ((u128)1 << bits) - 1;
		u128 x = (u128)v & m;

		if (is_signed(c, t) && (x >> (bits - 1)) & 1)
			return (i128)(x | ~m);
		return (i128)x;
	}
}

static uint64_t elem_size(cc *c, qty ptr)
{
	qty b = T(c, ptr)->base;
	int k = KIND(c, b);

	if (k == TY_VOID || k == TY_FUNC)
		return 1;
	return ty_size(c, b);
}

static bool eval_addr(cc *c, uint32_t n, cval *v);

VPRIV const field *node_field(cc *c, const node *x)
{
	return &c->tags[x->b].f[x->c];
}

static bool eval_addr(cc *c, uint32_t n, cval *v)
{
	const node *x = N(n);

	memset(v, 0, sizeof(*v));
	switch (x->op) {
	case N_VAR: {
		const sym *s = &c->syms[x->a];

		if (s->kind == S_FUNC || (s->kind == S_VAR && (s->flags & SF_GLOBAL || s->sc == SC_STATIC))) {
			v->kind = CV_ADDR;
			v->sym = x->a;
			return true;
		}
		return false;
	}
	case N_STR:
		v->kind = CV_ADDR;
		v->str = x->a + 1;
		return true;
	case N_COMPLIT:
		if (c->syms[x->b].flags & SF_GLOBAL || c->syms[x->b].sc == SC_STATIC) {
			v->kind = CV_ADDR;
			v->sym = x->b;
			return true;
		}
		return false;
	case N_MEMBER: {
		const field *f = node_field(c, x);

		if (f->bitfield || !eval_addr(c, x->a, v))
			return false;
		v->i += (i128)(f->off / 8);
		v->ice = false;
		return true;
	}
	case N_INDEX: {
		cval b;

		if (!eval(c, x->a, v) || !eval(c, x->b, &b) || b.kind != CV_INT)
			return false;
		if (v->kind != CV_ADDR && v->kind != CV_INT)
			return false;
		v->i += b.i * (i128)elem_size(c, N(x->a)->ty);
		v->ice = false;
		return true;
	}
	case N_DEREF:
		if (!eval(c, x->a, v))
			return false;
		v->ice = false;
		return v->kind == CV_ADDR || v->kind == CV_INT;
	default:
		return false;
	}
}

static bool is_flt_lit(cc *c, uint32_t n)
{
	while (N(n)->op == N_CAST && N(n)->aux == CK_NOP)
		n = N(n)->a;
	return N(n)->op == N_FNUM;
}

/* binary128 folding: a value is exact at that precision (ld) or a double or an integer */
static void set_q(cval *v, u128 q)
{
	v->kind = CV_FLT;
	v->ld = true;
	v->q = q;
	v->f = vsoftfloat_to_double(q);
}
static u128 get_q(const cval *v, bool sgn)
{
	return v->kind == CV_INT ? vsoftfloat_of_integer(v->i, sgn) : v->ld ? v->q : vsoftfloat_of_double(v->f);
}
/* nonzero, as a condition */
static bool cv_nz(cc *c, const cval *v) { return v->kind == CV_BIGINT ? big_nonzero(c, v) : v->ld ? vsoftfloat_is_nonzero(v->q) : v->f != 0; }
/* an address that may be null: a weak symbol's (clang: Decl::isWeak) */
static bool cv_weak(const cc *c, const cval *v) { return v->kind == CV_ADDR && v->sym && (c->syms[v->sym].flags & SF_WEAK); }

VPRIV int clz128(u128 x, unsigned bits)
{
	int n = 0;

	for (int b = (int)bits - 1; b >= 0 && !((x >> b) & 1); b--)
		n++;
	return n;
}

/* Initializers may refer back to themselves, directly or through another const.
   Emit workers evaluate the same immutable cc concurrently: keep each recursion
   path thread-local, and distinguish symbols from different translation units. */
struct const_eval {
	struct const_eval *prev;
	uint32_t sym;
	cc *owner;
};
static _Thread_local struct const_eval *const_eval_path;

/* Aggregate copies keep a reference to the source object in the AST. Follow
   only certified constant objects, with the same cycle guard as evaluation. */
static bool constexpr_unwrap(cc *c, uint32_t n, uint32_t *out, bool *ice)
{
	const node *x = N(n);

	if (vstack_guard_low())
		return false;
	if (x->op == N_CAST && x->aux == CK_NOP)
		return constexpr_unwrap(c, x->a, out, ice);
	if (x->op == N_COMPLIT) {
		*ice = false;
		return constexpr_unwrap(c, x->a, out, ice);
	}
	if (x->op == N_VAR) {
		const sym *s = &c->syms[x->a];

		if (!(s->flags & SF_CONSTEXPR) || !s->body || (s->flags & SF_INVALID))
			return false;
		for (struct const_eval *p = const_eval_path; p; p = p->prev)
			if (p->owner == c && p->sym == x->a)
				return false;
		struct const_eval frame = { const_eval_path, x->a, c };

		const_eval_path = &frame;
		bool ok = constexpr_unwrap(c, s->body, out, ice);
		const_eval_path = frame.prev;
		return ok;
	}
	*out = n;
	return true;
}

/* A zero initializer or a string element need not exist as an AST node.
   Keep those values here rather than appending nodes/pool entries while emit
   workers are traversing the already parsed translation unit. */
typedef struct const_object {
	uint32_t init;          /* 0: implicit zero or the integer value below */
	qty ty;
	i128 integer;
} const_object;

static bool constexpr_object_unwrap(cc *c, const_object *obj, bool *ice)
{
	if (!obj->init) return true;
	if (!constexpr_unwrap(c, obj->init, &obj->init, ice)) return false;
	obj->ty = N(obj->init)->ty;
	return true;
}

/* Resolve a constant aggregate subobject to the initializer which supplies it.
   Identity matters: a const object is not a C23 constexpr object. */
static bool constexpr_subobject(cc *c, uint32_t n, const_object *out, bool *ice, bool static_fold)
{
	const node *x = N(n);
	const_object obj;

	*out = (const_object){ .ty = x->ty };
	switch (x->op) {
	case N_VAR: {
		const sym *s = &c->syms[x->a];

		if (!s->body || (s->flags & SF_INVALID) || s->kind != S_VAR)
			return false;
		if (!(s->flags & SF_CONSTEXPR)) {
			/* clang, static data only: a read of a const, non-volatile object with an
			   initializer (clk_main[0], ca[1].index), not an ICE (int e[ci[0]] is a
			   folded VLA), not for a constexpr definition (constexpr_init_ok) */
			qty t = s->ty;
			unsigned q = QQ(t);

			while (is_array(c, t))
				t = T(c, t)->base, q |= QQ(t);
			if (!static_fold || (q & (Q_CONST | Q_VOLATILE)) != Q_CONST)
				return false;
			*ice = false;
		}
		out->init = s->body;
		return true;
	}
	case N_CPLX:
		out->init = n;
		*ice = false;
		return true;
	case N_COMPLIT: {
		uint32_t bad = 0;

		/* Static folding must validate the complete literal, including
		   unselected members and literals reached through comma operands. */
		if (static_fold && !static_init_ok(c, x->ty, x->a, &bad))
			return false;
		out->init = x->a;
		*ice = false;
		return x->a != 0;
	}
	case N_CAST:
		if (x->aux != CK_NOP && x->aux != CK_DECAY)
			return false;
		return constexpr_subobject(c, x->a, out, ice, static_fold);
	case N_COMMA: {
		uint32_t bad = 0;

		if (!static_fold || !static_init_ok(c, N(x->a)->ty, x->a, &bad))
			return false;
		*ice = false;
		return constexpr_subobject(c, x->b, out, ice, static_fold);
	}
	case N_MEMBER: {
		if (!constexpr_subobject(c, x->a, &obj, ice, static_fold) || !constexpr_object_unwrap(c, &obj, ice))
			return false;
		uint32_t init = obj.init;
		if (!init || N(init)->op == N_ZERO)
			return true;
		if (N(init)->op == N_CPLX && x->c < 2) {
			out->init = x->c ? N(init)->b : N(init)->a;
			return true;
		}
		if (N(init)->op != N_INIT || N(init)->aux == 1 || x->c >= N(init)->c ||
		    (N(init)->aux == 2 && N(init)->a != x->c))
			return false;
		out->init = c->lists[N(init)->b + x->c];
		return true;
	}
	case N_INDEX: {
		cval v;

		if (!constexpr_subobject(c, x->a, &obj, ice, static_fold) || !constexpr_object_unwrap(c, &obj, ice) ||
		    !eval(c, x->b, &v) || v.kind != CV_INT || v.i < 0 || (u128)v.i >= (u128)arr_len(c, obj.ty))
			return false;
		uint32_t init = obj.init;
		if (!init || N(init)->op == N_ZERO)
			return true;
		if (N(init)->op == N_STR) {
			const cstr *str = &c->strs[N(init)->a];
			i128 value = 0;

			if ((u128)v.i >= str->n)
				return false;
			for (unsigned k = 0; k < str->esz; k++)
				value |= (i128)(unsigned char)str->s[(size_t)v.i * str->esz + k] << (8*k);
			out->integer = trunc_to(c, value, x->ty);
			return true;
		}
		if (N(init)->op != N_INIT || N(init)->aux != 1)
			return false;
		const uint32_t *a = &c->lists[N(init)->b];

		for (uint32_t i = 0; i < N(init)->c; i++)
			if ((u128)v.i >= a[3*i] && (u128)v.i <= a[3*i+1]) {
				out->init = a[3*i+2];
				return true;
			}
		return true;
	}
	default:
		return false;
	}
}

/* Read-only aggregate initializer selection for static data. This does not
   certify an ordinary const variable as constexpr or permit inactive unions. */
bool cc_init_subobject(cc *c, uint32_t n, uint32_t *init)
{
	const_object obj;
	bool ice = true, indexed = false;
	uint32_t root = n;

	if (vstack_guard_low() || (N(n)->op != N_MEMBER && N(n)->op != N_INDEX))
		return false;
	while (N(root)->op == N_MEMBER || N(root)->op == N_INDEX || N(root)->op == N_CAST) {
		indexed |= N(root)->op == N_INDEX;
		root = N(root)->a;
	}
	/* Clang accepts compound-literal member selection here, but not an
	   indexed array compound literal. C23 constexpr arrays stay separate. */
	if (indexed && N(root)->op == N_COMPLIT)
		return false;
	if (!constexpr_subobject(c, n, &obj, &ice, true) || obj.init == n || (!obj.init && obj.integer))
		return false;
	*init = obj.init;
	return true;
}

static bool constexpr_value(cc *c, uint32_t n, cval *v)
{
	const_object obj;
	uint32_t root = n;
	bool ice = true;

	while (N(root)->op == N_MEMBER || N(root)->op == N_INDEX || N(root)->op == N_CAST)
		root = N(root)->a;
	uint32_t sym = N(root)->op == N_VAR ? N(root)->a : 0;
	for (struct const_eval *p = const_eval_path; sym && p; p = p->prev)
		if (p->owner == c && p->sym == sym)
			return false;
	struct const_eval frame = { const_eval_path, sym, c };

	const_eval_path = &frame;
	bool ok = constexpr_subobject(c, n, &obj, &ice, true) && obj.init != n;
	if (ok) {
		if (obj.init) ok = eval(c, obj.init, v);
		else {
			memset(v, 0, sizeof *v);
			v->kind = is_float(c, obj.ty) ? CV_FLT : CV_INT;
			v->i = obj.integer;
			ok = is_scalar(c, obj.ty);
		}
	}
	const_eval_path = frame.prev;
	if (ok)
		v->ice = ice && (v->kind == CV_INT || v->kind == CV_BIGINT);
	return ok;
}

/* Constant-initializer grammar. Ordinary const variables can be folded as an
   extension elsewhere, but cannot make a constexpr definition valid. */
VPRIV bool constexpr_init_ok(cc *c, uint32_t n)
{
	const node *x;
	cval v;
	const_object obj;
	bool ice = true;

	if (!n)
		return true;
	x = N(n);
	if (ISERR(n))
		return true;
	if (vector_constant(c,n))
		return true;
	switch (x->op) {
	case N_NUM: case N_BIGNUM:
		return !(x->aux & NUM_NO_CONSTEXPR);
	case N_FNUM: case N_ZERO: case N_STR:
		return true;
	case N_INIT: {
		const uint32_t *a = &c->lists[x->b];

		for (uint32_t i = 0; i < x->c; i++) {
			if (x->aux == 2 && i != x->a)
				continue;
			if (!constexpr_init_ok(c, a[x->aux == 1 ? 3*i+2 : i]))
				return false;
		}
		return true;
	}
	case N_VAR: {
		for (struct const_eval *p = const_eval_path; p; p = p->prev)
			if (p->owner == c && p->sym == x->a)
				return false;
		struct const_eval frame = { const_eval_path, x->a, c };

		const_eval_path = &frame;
		bool ok = constexpr_subobject(c, n, &obj, &ice, false) && obj.init != n && constexpr_init_ok(c, obj.init);
		const_eval_path = frame.prev;
		return ok;
	}
	case N_MEMBER: case N_INDEX:
		if (bitcast_constant(c, n)) return true;
		return eval(c, n, &v) && constexpr_subobject(c, n, &obj, &ice, false) && obj.init != n && constexpr_init_ok(c, obj.init);
	case N_COMPLIT:
		return constexpr_init_ok(c, x->a);
	case N_CAST: case N_UNARY:
		if (x->op == N_CAST && x->aux == CK_BITS) return bitcast_constant(c, n);
		return constexpr_init_ok(c, x->a) && eval(c, n, &v);
	case N_BINARY:
		return constexpr_init_ok(c, x->a) && constexpr_init_ok(c, x->b) && eval(c, n, &v);
	case N_LOGAND: case N_LOGOR:
		if (!constexpr_init_ok(c, x->a) || !eval(c, x->a, &v))
			return false;
		if ((v.kind == CV_INT && (v.i != 0) == (x->op == N_LOGOR)) ||
		    ((v.kind == CV_FLT || v.kind == CV_BIGINT) && cv_nz(c, &v) == (x->op == N_LOGOR)))
			return true;
		return constexpr_init_ok(c, x->b) && eval(c, n, &v);
	case N_COND:
		if (!constexpr_init_ok(c, x->a) || !eval(c, x->a, &v))
			return false;
		return constexpr_init_ok(c, (v.kind == CV_INT ? v.i != 0 : cv_nz(c, &v)) ?
					      (x->b ? x->b : x->a) : x->c) && eval(c, n, &v);
	case N_CPLX:
		return constexpr_init_ok(c, x->a) && constexpr_init_ok(c, x->b);
	case N_BUILTIN:
		return eval(c, n, &v) && (v.ice || x->aux == BI_INF || x->aux == BI_INFF ||
			x->aux == BI_HUGE_VAL || x->aux == BI_HUGE_VALF || x->aux == BI_NAN ||
			x->aux == BI_NANF || x->aux == BI_NANS);
	default:
		return false;
	}
}

static bool constexpr_variably_modified(cc *c, qty t)
{
	while (is_array(c, t) || is_ptr(c, t)) {
		if (T(c, t)->flags & TF_VLA)
			return true;
		t = T(c, t)->base;
	}
	return false;
}

/* Constraints apply recursively to array elements and aggregate members, but
   not through pointers: pointer-to-volatile is a valid constexpr null pointer. */
VPRIV bool constexpr_type_ok(cc *c, qty t)
{
	if ((QQ(t) & (Q_VOLATILE | Q_RESTRICT)) || (T(c, t)->flags & TF_ATOMIC) || constexpr_variably_modified(c, t))
		return false;
	if (is_array(c, t))
		return constexpr_type_ok(c, T(c, t)->base);
	if (is_record(c, t)) {
		const tag *r = &c->tags[T(c, t)->aux];

		for (uint32_t i = 0; i < r->nfield; i++)
			if (!constexpr_type_ok(c, r->f[i].ty))
				return false;
	}
	return true;
}

/* Called before the implicit initialization conversion, so an explicit cast
   remains a deliberate rounding/truncation in the source expression. */
VPRIV void constexpr_conversion(cc *c, uint32_t n, qty to)
{
	qty from = N(n)->ty;
	cval a, b;
	bool exact = true;

	if (is_ptr(c, to)) {
		if (eval(c, n, &a) && (a.kind == CV_ADDR || (a.kind == CV_INT && a.i != 0)))
			cc_error(c, expr_loc(c, n), "constexpr pointer initializer is not null");
		return;
	}
	if (is_complex(c, from) && !is_complex(c, to)) {
		cc_error(c, expr_loc(c, n), "constexpr initializer for type '%s' is of type '%s'", tyname(c, to), tyname(c, from));
		return;
	}
	if (!is_arith(c, to) || !eval(c, n, &a))
		return;
	if (is_integer(c, to) && a.kind == CV_FLT) {
		cc_error(c, expr_loc(c, n), "constexpr initializer for type '%s' is of type '%s'", tyname(c, to), tyname(c, from));
		return;
	}
	if (a.kind == CV_BIGINT)
		exact = big_conversion_exact(c, &a, from, to);
	else if (is_integer(c, to) && a.kind == CV_INT)
		exact = is_bigint(c, to) ? (is_signed(c, to) || !is_signed(c, from) || a.i >= 0) : trunc_to(c, a.i, to) == a.i;
	else if (is_float(c, to) && (a.kind == CV_INT || a.kind == CV_FLT)) {
		uint32_t converted = convert(c, n, to);

		if (!eval(c, converted, &b) || b.kind != CV_FLT)
			return;
		u128 aq = get_q(&a, is_signed(c, from)), bq = get_q(&b, true);

		exact = (vsoftfloat_is_nan(aq) && vsoftfloat_is_nan(bq)) || vsoftfloat_compare(aq, bq) == 0;
		if (vsoftfloat_is_nan(aq) && !(aq & ((u128)1 << 111)) && KIND(c, from) != KIND(c, to))
			exact = false;
		if (a.kind == CV_INT) {
			u128 magnitude = is_signed(c, from) && a.i < 0 ? (u128)0 - (u128)a.i : (u128)a.i;
			unsigned bits = 128u - (unsigned)clz128(magnitude, 128);
			unsigned precision = KIND(c, to) == TY_FLOAT16 ? 11 : KIND(c, to) == TY_BFLOAT16 ? 8 :
				KIND(c, to) == TY_FLOAT ? 24 : KIND(c, to) == TY_DOUBLE ? 53 : (unsigned)q_prec(c, to) + 1;

			/* Converting back can saturate at an integer endpoint and conceal rounding. */
			if (bits > precision && (magnitude & (((u128)1 << (bits - precision)) - 1)))
				exact = false;
		}
	}
	if (!exact) {
		char value[96];
		char *wide = a.kind == CV_BIGINT ? big_value_string(c, &a, from) : NULL;

		if (a.kind == CV_INT)
			i128s_(value, sizeof value, a.i);
		else
			snprintf(value, sizeof value, "%.9g", a.f);
		cc_error(c, expr_loc(c, n), "constexpr initializer evaluates to %s which is not exactly representable in type '%s'", wide ? wide : value, tyname(c, to));
		free(wide);
	}
}

VPRIV bool eval(cc *c, uint32_t n, cval *v)
{
	const node *x = N(n);
	cval a, b;

	memset(v, 0, sizeof(*v));
	if (x->op == N_BIGNUM || ((x->op == N_CAST || x->op == N_UNARY) && (is_bigint(c, x->ty) || is_bigint(c, N(x->a)->ty))) ||
	    (x->op == N_BINARY && (is_bigint(c, N(x->a)->ty) || is_bigint(c, N(x->b)->ty))))
		return big_read_eval(c, n, v);
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
	/* the parser's stack check bounds the nesting (cast_expr); ASan's frames make
	   this recursion's larger than the parser's (vcc-stress ternary-100k) */
	if (vstack_guard_low())
		return false;
#endif
#endif
	switch (x->op) {
	case N_NUM:
		v->kind = CV_INT;
		v->i = c->ipool[x->a];
		v->ice = !(x->aux & NUM_NO_ICE);
		return true;
	case N_FNUM:
		v->kind = CV_FLT;
		v->f = c->fpool[x->a];
		if (is_q(c, x->ty))
			v->q = fnum_q(c, x->a), v->ld = true;
		v->ice = true;	/* only as the operand of a cast; arithmetic clears it */
		return true;
	case N_ZERO:
		v->kind = is_float(c, x->ty) ? CV_FLT : CV_INT;
		v->ice = true;
		return is_scalar(c, x->ty);
	case N_MEMBER:
		return constexpr_value(c, n, v) || bitcast_value(c, n, v);
	case N_VAR: {
		/* a const integer variable with a constant initializer folds (not an ICE) */
		const sym *s = &c->syms[x->a];

		if (s->kind == S_VAR && (QQ(s->ty) & (Q_CONST | Q_VOLATILE)) == Q_CONST && (is_integer(c, s->ty) || (s->flags & SF_CONSTEXPR)) && s->body && !(s->flags & SF_INVALID)) {
			for (struct const_eval *p = const_eval_path; p; p = p->prev)
				if (p->owner == c && p->sym == x->a)
					return false;
			struct const_eval frame = { const_eval_path, x->a, c };

			const_eval_path = &frame;
			bool ok = eval(c, s->body, v) && (v->kind == CV_INT || v->kind == CV_BIGINT || (s->flags & SF_CONSTEXPR));
			const_eval_path = frame.prev;
			if (ok) {
				v->ice = (s->flags & SF_CONSTEXPR) && (v->kind == CV_INT || v->kind == CV_BIGINT);
				return true;
			}
		}
		return false;
	}
	case N_CAST: {
		qty to = x->ty;

		switch (x->aux) {
		case CK_DECAY:
		case CK_FDECAY:
			return eval_addr(c, x->a, v);
		case CK_VOID:
			return false;
		case CK_NULL:
			v->kind = CV_INT;
			v->ice = eval(c, x->a, &a) && a.ice;
			return true;
		case CK_BITS:
			return bitcast_value(c, n, v);
		case CK_UNION: case CK_SPLAT:
			return false;
		}
		if (!eval(c, x->a, &a))
			return false;
		*v = a;
		switch (x->aux) {
		case CK_NOP:
			if (a.kind == CV_INT && !is_ptr(c, to))
				v->i = trunc_to(c, a.i, to);
			return true;
		case CK_INT:
			if (a.kind == CV_ADDR && a.sublabel && ty_size(c, to) >= 4)
				return true;	/* a label difference, as a 4- or 8-byte word */
			if (a.kind != CV_INT)
				return false;
			v->i = trunc_to(c, a.i, to);
			return true;
		case CK_BOOL:
			v->kind = CV_INT;
			if (a.kind == CV_INT)
				v->i = a.i != 0;
			else if (a.kind == CV_FLT)
				v->i = cv_nz(c, &a), v->ice = is_flt_lit(c, x->a);
			else if (cv_weak(c, &a))
				return false;
			else
				v->i = 1, v->ice = false;	/* address of an object is never null */
			v->sym = v->str = v->label = 0;
			return true;
		case CK_I2F:
			if (a.kind != CV_INT)
				return false;
			v->kind = CV_FLT;
			v->ice = false;
			if (is_q(c, to)) {
				set_q(v, vsoftfloat_of_integer_with_precision(a.i, is_signed(c, N(x->a)->ty), q_prec(c, to)));
				return true;
			}
			v->f = is_signed(c, N(x->a)->ty) ? (double)a.i : (double)(u128)a.i;
			if (KIND(c, to) == TY_BFLOAT16)
				v->f = bf_to_d(vsoftfloat_to_bfloat16(vsoftfloat_of_integer(a.i, is_signed(c, N(x->a)->ty))));
			else if (KIND(c, to) == TY_FLOAT16)
				v->f = h_to_d(vsoftfloat_to_half(vsoftfloat_of_integer(a.i, is_signed(c, N(x->a)->ty))));
			else if (KIND(c, to) == TY_FLOAT)
				v->f = is_signed(c, N(x->a)->ty) ? (double)(float)a.i : (double)(float)(u128)a.i;	/* one rounding */
			return true;
		case CK_F2I:
			if (a.kind != CV_FLT)
				return false;
			v->kind = CV_INT;
			v->i = trunc_to(c, vsoftfloat_to_integer_saturating(get_q(&a, true), (int)int_bits(c, to), is_signed(c, to)), to);
			v->ld = false;
			v->ice = is_flt_lit(c, x->a);
			return true;
		case CK_F2F:
			if (a.kind != CV_FLT)
				return false;
			if (a.ld && vsoftfloat_is_nan(a.q)) a.q |= (u128)1 << 111;
			v->ice = false;
			if (is_q(c, to))
				set_q(v, vsoftfloat_to_precision(get_q(&a, true), q_prec(c, to)));
			else if (KIND(c, to) == TY_BFLOAT16)
				v->f = bf_to_d(vsoftfloat_to_bfloat16(get_q(&a, true))), v->ld = false;
			else if (KIND(c, to) == TY_FLOAT16)
				v->f = h_to_d(vsoftfloat_to_half(get_q(&a, true))), v->ld = false;
			else if (a.ld && KIND(c, to) == TY_FLOAT)
				v->f = vsoftfloat_to_float(a.q), v->ld = false;	/* one rounding */
			else if (KIND(c, to) == TY_FLOAT)
				v->f = (float)a.f;
			else
				v->ld = false;	/* f already holds the nearest double */
			return true;
		case CK_P2I:
			v->ice = false;
			if (a.kind == CV_INT)
				v->i = trunc_to(c, a.i, to);
			else if (ty_size(c, to) < 8)
				return false;
			return true;
		case CK_I2P:
			if (a.kind == CV_INT)
				v->i = (i128)(uint64_t)a.i;
			/* (void *)ICE stays an ICE for null pointer constant purposes */
			return true;
		case CK_P2P:
			return true;
		}
		return false;
	}
	case N_UNARY:
		if (!eval(c, x->a, &a))
			return false;
		*v = a;
		if (a.kind == CV_FLT) {
			v->ice = false;
			if (x->aux == LEX_TOKEN_MINUS)
				v->f = -a.f, v->q = a.q ^ Q_SIGN;
			else if (x->aux == LEX_TOKEN_EXCLAMATION)
				v->kind = CV_INT, v->i = !cv_nz(c, &a), v->ld = false;
			return true;
		}
		if (a.kind == CV_ADDR) {
			if (x->aux == LEX_TOKEN_EXCLAMATION && !cv_weak(c, &a)) {
				v->kind = CV_INT, v->i = 0, v->ice = false, v->sym = v->str = v->label = 0;
				return true;
			}
			return false;
		}
		switch (x->aux) {
		case LEX_TOKEN_MINUS: v->i = trunc_to(c, (i128)((u128)0 - (u128)a.i), x->ty); break;
		case LEX_TOKEN_TILDE: v->i = trunc_to(c, ~a.i, x->ty); break;
		case LEX_TOKEN_EXCLAMATION: v->i = a.i == 0; break;
		}
		return true;
	case N_COMPLIT:	/* (T){ v } of scalar type: its value */
		if (!is_scalar(c, x->ty) || !x->a)
			return false;
		if (N(x->a)->op == N_INIT)
			return false;
		if (!eval(c, x->a, v))
			return false;
		v->ice = false;
		return true;
	case N_CALL: {	/* strlen("literal"): clang folds the library builtin */
		uint32_t f = x->a, s;
		int l;
		const char *nm;

		while (N(f)->op == N_CAST || N(f)->op == N_ADDR)
			f = N(f)->a;
		if (N(f)->op != N_VAR || x->c != 1 || c->syms[N(f)->a].kind != S_FUNC || c->syms[N(f)->a].sc == SC_STATIC)
			return false;
		nm = idname(c->syms[N(f)->a].name, &l);
		if (l != 6 || memcmp(nm, "strlen", 6))
			return false;
		s = c->lists[x->b];
		while (N(s)->op == N_CAST)
			s = N(s)->a;
		if (N(s)->op != N_STR || c->strs[N(s)->a].esz != 1)
			return false;
		v->kind = CV_INT;
		v->ice = false;
		v->i = (i128)strlen(c->strs[N(s)->a].s);
		return true;
	}
	case N_BINARY: {
		int op = x->aux;
		qty lt = N(x->a)->ty, rt;
		bool ok;

		if (!eval(c, x->a, &a) || !eval(c, x->b, &b))
			return false;
		rt = N(x->b)->ty;
		v->kind = CV_INT;
		v->ice = a.ice && b.ice;
		/* pointer arithmetic and comparisons */
		if (is_ptr(c, lt) || is_ptr(c, rt) || a.kind == CV_ADDR || b.kind == CV_ADDR) {
			v->ice = false;
			if (is_ptr(c, x->ty)) {	/* ptr +- int */
				const cval *pv = is_ptr(c, lt) ? &a : &b, *iv = is_ptr(c, lt) ? &b : &a;
				uint64_t es = elem_size(c, is_ptr(c, lt) ? lt : rt);

				if (iv->kind != CV_INT)
					return false;
				*v = *pv;
				v->ice = false;
				v->i += (op == LEX_TOKEN_MINUS ? -iv->i : iv->i) * (i128)es;
				return true;
			}
			if (op == LEX_TOKEN_MINUS && is_ptr(c, lt) && is_ptr(c, rt) && a.kind == CV_ADDR && b.kind == CV_ADDR &&
			    a.label && b.label && a.label != b.label && !a.sublabel && !b.sublabel) {
				/* &&l1 - &&l0 */
				*v = a;
				v->ice = false;
				v->sublabel = b.label;
				v->i = a.i - b.i;
				return true;
			}
			if (op == LEX_TOKEN_MINUS && is_ptr(c, lt) && is_ptr(c, rt)) {
				if (a.kind != b.kind || a.sym != b.sym || a.str != b.str || a.label != b.label)
					return false;
				v->i = (a.i - b.i) / (i128)elem_size(c, lt);
				return true;
			}
			/* an address cast to a 64-bit integer, plus or minus an integer: a relocation */
			if ((op == LEX_TOKEN_PLUS || op == LEX_TOKEN_MINUS) && ty_size(c, x->ty) == 8 &&
			    ((a.kind == CV_ADDR && b.kind == CV_INT) || (op == LEX_TOKEN_PLUS && a.kind == CV_INT && b.kind == CV_ADDR))) {
				const cval *pv = a.kind == CV_ADDR ? &a : &b, *iv = a.kind == CV_ADDR ? &b : &a;
				i128 d = op == LEX_TOKEN_MINUS ? -iv->i : iv->i;

				*v = *pv;
				v->ice = false;
				v->i += d;
				return true;
			}
			/* comparison */
			if (a.kind == CV_ADDR || b.kind == CV_ADDR) {
				bool same = a.kind == b.kind && a.sym == b.sym && a.str == b.str && a.label == b.label;

				if (!same) {
					/* &obj vs null */
					if (((a.kind == CV_INT && a.i == 0) || (b.kind == CV_INT && b.i == 0)) && !cv_weak(c, &a) && !cv_weak(c, &b)) {
						if (op == LEX_TOKEN_EQUAL_EQUAL) { v->i = 0; return true; }
						if (op == LEX_TOKEN_EXCLAMATION_EQUAL) { v->i = 1; return true; }
					}
					return false;
				}
			}
			switch (op) {
			case LEX_TOKEN_EQUAL_EQUAL: v->i = a.i == b.i; return true;
			case LEX_TOKEN_EXCLAMATION_EQUAL: v->i = a.i != b.i; return true;
			case LEX_TOKEN_LESS: v->i = (u128)a.i < (u128)b.i; return true;
			case LEX_TOKEN_LESS_EQUAL: v->i = (u128)a.i <= (u128)b.i; return true;
			case LEX_TOKEN_GREATER: v->i = (u128)a.i > (u128)b.i; return true;
			case LEX_TOKEN_GREATER_EQUAL: v->i = (u128)a.i >= (u128)b.i; return true;
			case LEX_TOKEN_AMPERSAND: case LEX_TOKEN_PIPE: case LEX_TOKEN_CARET: case LEX_TOKEN_PLUS: case LEX_TOKEN_MINUS:
				if (a.kind == CV_INT && b.kind == CV_INT)
					break;	/* integers cast from pointers */
				return false;
			default:
				return false;
			}
		}
		if ((a.kind == CV_FLT || b.kind == CV_FLT) &&
		    (a.ld || b.ld || is_q(c, x->ty) || is_q(c, N(x->a)->ty) || is_q(c, N(x->b)->ty))) {
			int p = q_prec(c, N(x->a)->ty) < q_prec(c, N(x->b)->ty) ? q_prec(c, N(x->a)->ty) : q_prec(c, N(x->b)->ty);
			u128 l = vsoftfloat_to_precision(get_q(&a, is_signed(c, N(x->a)->ty)), p), r = vsoftfloat_to_precision(get_q(&b, is_signed(c, N(x->b)->ty)), p);
			int k = vsoftfloat_compare(l, r);

			v->ice = false;
			v->ld = false;
			switch (op) {
			case LEX_TOKEN_PLUS: set_q(v, vsoftfloat_add_with_precision(l, r, p)); break;
			case LEX_TOKEN_MINUS: set_q(v, vsoftfloat_subtract_with_precision(l, r, p)); break;
			case LEX_TOKEN_STAR: set_q(v, vsoftfloat_multiply_with_precision(l, r, p)); break;
			case LEX_TOKEN_SLASH: set_q(v, vsoftfloat_divide_with_precision(l, r, p)); break;
			case LEX_TOKEN_LESS: v->i = k == -1; break;
			case LEX_TOKEN_LESS_EQUAL: v->i = k == -1 || k == 0; break;
			case LEX_TOKEN_GREATER: v->i = k == 1; break;
			case LEX_TOKEN_GREATER_EQUAL: v->i = k == 1 || k == 0; break;
			case LEX_TOKEN_EQUAL_EQUAL: v->i = k == 0; break;
			case LEX_TOKEN_EXCLAMATION_EQUAL: v->i = k != 0; break;
			default: return false;
			}
			if (v->kind != CV_FLT)
				v->kind = CV_INT;
			return true;
		}
		if (a.kind == CV_FLT || b.kind == CV_FLT) {
			double l = a.kind == CV_FLT ? a.f : (double)a.i, r = b.kind == CV_FLT ? b.f : (double)b.i;

			v->ice = false;
			switch (op) {
			case LEX_TOKEN_PLUS: v->kind = CV_FLT, v->f = l + r; break;
			case LEX_TOKEN_MINUS: v->kind = CV_FLT, v->f = l - r; break;
			case LEX_TOKEN_STAR: v->kind = CV_FLT, v->f = l * r; break;
			case LEX_TOKEN_SLASH: v->kind = CV_FLT, v->f = l / r; break;
			case LEX_TOKEN_LESS: v->i = l < r; break;
			case LEX_TOKEN_LESS_EQUAL: v->i = l <= r; break;
			case LEX_TOKEN_GREATER: v->i = l > r; break;
			case LEX_TOKEN_GREATER_EQUAL: v->i = l >= r; break;
			case LEX_TOKEN_EQUAL_EQUAL: v->i = l == r; break;
			case LEX_TOKEN_EXCLAMATION_EQUAL: v->i = l != r; break;
			default: return false;
			}
			if (v->kind == CV_FLT && KIND(c, x->ty) == TY_BFLOAT16)
				v->f = bf_to_d(vsoftfloat_to_bfloat16(vsoftfloat_of_double(v->f)));
			if (v->kind == CV_FLT && KIND(c, x->ty) == TY_FLOAT16)
				v->f = h_to_d(vsoftfloat_to_half(vsoftfloat_of_double(v->f)));
			if (v->kind == CV_FLT && KIND(c, x->ty) == TY_FLOAT)
				v->f = (float)v->f;
			return true;
		}
		{
			qty ot = N(x->a)->ty;	/* operands are converted to the common type */
			bool sg = is_signed(c, ot);
			i128 l = a.i, r = b.i;
			uint64_t bits = int_bits(c, ot);

			ok = true;
			switch (op) {
			case LEX_TOKEN_PLUS: v->i = (i128)((u128)l + (u128)r); break;
			case LEX_TOKEN_MINUS: v->i = (i128)((u128)l - (u128)r); break;
			case LEX_TOKEN_STAR: v->i = sg ? l * r : (i128)((u128)l * (u128)r); break;
			case LEX_TOKEN_SLASH:
			case LEX_TOKEN_PERCENT:
				if (r == 0)
					return false;
				if (sg)
					v->i = op == LEX_TOKEN_SLASH ? l / r : l % r;
				else
					v->i = (i128)(op == LEX_TOKEN_SLASH ? (u128)trunc_to(c, l, unsigned_of(c, ot)) / (u128)trunc_to(c, r, unsigned_of(c, ot))
								    : (u128)trunc_to(c, l, unsigned_of(c, ot)) % (u128)trunc_to(c, r, unsigned_of(c, ot)));
				break;
			case LEX_TOKEN_LESS_LESS:
			case LEX_TOKEN_GREATER_GREATER: {
				/* as clang's C folding (shift-count-overflow warns): a count over the
				   width is width - 1, a negative count shifts the other way */
				uint64_t w = int_bits(c, x->ty);
				bool left = op == LEX_TOKEN_LESS_LESS;

				if (r < 0) {
					left = !left;
					r = -r;
				}
				if ((u128)r >= w)
					r = (i128)(w - 1);
				if (left)
					v->i = (i128)((u128)l << r);
				else
					v->i = is_signed(c, x->ty) ? l >> r : (i128)((u128)trunc_to(c, l, unsigned_of(c, x->ty)) >> r);
				break;
			}
			case LEX_TOKEN_AMPERSAND: v->i = l & r; break;
			case LEX_TOKEN_PIPE: v->i = l | r; break;
			case LEX_TOKEN_CARET: v->i = l ^ r; break;
			case LEX_TOKEN_LESS: v->i = sg ? l < r : (u128)trunc_to(c, l, unsigned_of(c, ot)) < (u128)trunc_to(c, r, unsigned_of(c, ot)); break;
			case LEX_TOKEN_LESS_EQUAL: v->i = sg ? l <= r : (u128)trunc_to(c, l, unsigned_of(c, ot)) <= (u128)trunc_to(c, r, unsigned_of(c, ot)); break;
			case LEX_TOKEN_GREATER: v->i = sg ? l > r : (u128)trunc_to(c, l, unsigned_of(c, ot)) > (u128)trunc_to(c, r, unsigned_of(c, ot)); break;
			case LEX_TOKEN_GREATER_EQUAL: v->i = sg ? l >= r : (u128)trunc_to(c, l, unsigned_of(c, ot)) >= (u128)trunc_to(c, r, unsigned_of(c, ot)); break;
			case LEX_TOKEN_EQUAL_EQUAL: v->i = trunc_to(c, l, ot) == trunc_to(c, r, ot); break;
			case LEX_TOKEN_EXCLAMATION_EQUAL: v->i = trunc_to(c, l, ot) != trunc_to(c, r, ot); break;
			default: ok = false;
			}
			(void)bits;
			if (!ok)
				return false;
			if (!is_ptr(c, x->ty))
				v->i = trunc_to(c, v->i, x->ty);
			return true;
		}
	}
	case N_LOGAND:
	case N_LOGOR: {
		bool t;

		if (!eval(c, x->a, &a))
			return false;
		if (cv_weak(c, &a))
			return false;
		t = a.kind == CV_INT ? a.i != 0 : a.kind == CV_FLT || a.kind == CV_BIGINT ? cv_nz(c, &a) : true;
		v->kind = CV_INT;
		if (a.kind == CV_ADDR)
			a.ice = false;
		if ((x->op == N_LOGAND && !t) || (x->op == N_LOGOR && t)) {
			v->i = x->op == N_LOGOR;
			v->ice = a.ice;
			return true;
		}
		if (!eval(c, x->b, &b) || cv_weak(c, &b))
			return false;
		v->i = b.kind == CV_INT ? b.i != 0 : b.kind == CV_FLT || b.kind == CV_BIGINT ? cv_nz(c, &b) : 1;
		v->ice = a.ice && b.ice && b.kind != CV_ADDR;
		return true;
	}
	case N_COND: {
		bool t, ice;

		if (!eval(c, x->a, &a))
			return false;
		if (cv_weak(c, &a))
			return false;
		t = a.kind == CV_INT ? a.i != 0 : a.kind == CV_FLT || a.kind == CV_BIGINT ? cv_nz(c, &a) : true;
		ice = a.ice && a.kind != CV_ADDR;
		if (!x->b) {	/* a ?: c */
			if (t) {
				*v = a;
				return true;
			}
			if (!eval(c, x->c, v))
				return false;
			v->ice = v->ice && ice;
			return true;
		}
		if (!eval(c, t ? x->b : x->c, v))
			return false;
		v->ice = v->ice && ice;
		return true;
	}
	case N_COMMA:
		if (!eval(c, x->b, v))
			return false;
		v->ice = false;
		return true;
	case N_ADDR:
		if (N(x->a)->op == N_LABELADDR)
			return eval(c, x->a, v);
		return eval_addr(c, x->a, v);
	case N_LABELADDR:
		v->kind = CV_ADDR;
		v->label = x->a + 1;
		return true;
	case N_INDEX:
	case N_DEREF: {
		if (x->op == N_INDEX && (constexpr_value(c, n, v) || bitcast_value(c, n, v)))
			return true;
		/* "abc"[1], *"abc": an element of a string literal folds (clang) */
		const cstr *st;
		size_t sz = is_integer(c, x->ty) ? ty_size(c, x->ty) : 0;
		int64_t e;

		if (!sz || !eval_addr(c, n, &a) || a.kind != CV_ADDR || !a.str || a.sym || a.label || a.sublabel)
			return false;
		st = &c->strs[a.str - 1];
		if (sz != st->esz || a.i < 0 || a.i % (i128)sz || a.i / (i128)sz >= st->n)
			return false;
		e = 0;
		for (size_t k = 0; k < sz; k++)	/* little endian */
			e |= (int64_t)(uint8_t)st->s[(size_t)a.i + k] << (8 * k);
		if (is_signed(c, x->ty) && sz < 8)
			e = (int64_t)((uint64_t)e << (64 - 8 * sz)) >> (64 - 8 * sz);
		v->kind = CV_INT;
		v->i = e;
		v->ice = false;
		return true;
	}
	case N_BUILTIN:
		return eval_builtin(c, x, v);
	default:
		return false;
	}
}

/* is n a null pointer constant (C11 6.3.2.3p3, clang: ICE or (void *)ICE) */
VPRIV bool is_npc(cc *c, uint32_t n)
{
	const node *x = N(n);
	cval v;

	if (is_integer(c, x->ty))
		return eval(c, n, &v) && v.kind == CV_INT && v.ice && v.i == 0;
	if (is_ptr(c, x->ty) && x->op == N_CAST && (x->aux == CK_I2P || x->aux == CK_NULL || x->aux == CK_P2P)) {
		qty pt = T(c, x->ty)->base;

		if (KIND(c, pt) != TY_VOID || QQ(pt))
			return false;
		if (x->aux == CK_P2P)
			return is_npc(c, x->a);
		return eval(c, n, &v) && v.kind == CV_INT && v.ice && v.i == 0;
	}
	return false;
}

/* int value of a constant expression (array sizes, widths, cases, enums) */
VPRIV bool const_int(cc *c, uint32_t n, i128 *out)
{
	cval v;

	if (!eval(c, n, &v) || v.kind != CV_INT)
		return false;
	*out = v.i;
	return true;
}
