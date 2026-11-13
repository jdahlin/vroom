/* vcc front end: constant evaluation (integer constant expressions and foldable ones, as clang) */
#include "cc-internal.h"

CC_PRIVATE i128 cc_constant_truncate(cc *c, i128 v, qty t)
{
	int k = AST_TYPE_KIND(c, t);
	unsigned bits;

	if (k == TY_BOOL)
		return v != 0;
	if (k == TY_PTR)
		return (i128)(uint64_t)v;
	bits = cc_type_integer_bits(c, t);
	if (bits >= 128)
		return cc_type_is_signed(c, t) ? v : (i128)(u128)v;
	{
		u128 m = ((u128)1 << bits) - 1;
		u128 x = (u128)v & m;

		if (cc_type_is_signed(c, t) && (x >> (bits - 1)) & 1)
			return (i128)(x | ~m);
		return (i128)x;
	}
}

static uint64_t cc_pointer_element_size(cc *c, qty ptr)
{
	qty b = AST_TYPE(c, ptr)->base;
	int k = AST_TYPE_KIND(c, b);

	if (k == TY_VOID || k == TY_FUNC)
		return 1;
	return cc_type_size(c, b);
}

static bool cc_constant_eval_address(cc *c, uint32_t n, cval *v);

CC_PRIVATE const field *cc_node_member_field(cc *c, const node *x)
{
	return &c->tags[x->b].f[x->c];
}

static bool cc_constant_eval_address(cc *c, uint32_t n, cval *v)
{
	const node *x = AST_NODE(n);

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
		const field *f = cc_node_member_field(c, x);

		if (f->bitfield || !cc_constant_eval_address(c, x->a, v))
			return false;
		v->i += (i128)(f->off / 8);
		v->ice = false;
		return true;
	}
	case N_INDEX: {
		cval b;

		if (!cc_constant_eval(c, x->a, v) || !cc_constant_eval(c, x->b, &b) || b.kind != CV_INT)
			return false;
		if (v->kind != CV_ADDR && v->kind != CV_INT)
			return false;
		v->i += b.i * (i128)cc_pointer_element_size(c, AST_NODE(x->a)->ty);
		v->ice = false;
		return true;
	}
	case N_DEREF:
		if (!cc_constant_eval(c, x->a, v))
			return false;
		v->ice = false;
		return v->kind == CV_ADDR || v->kind == CV_INT;
	default:
		return false;
	}
}

static bool cc_constant_is_float_literal(cc *c, uint32_t n)
{
	while (AST_NODE(n)->op == N_CAST && AST_NODE(n)->aux == CK_NOP)
		n = AST_NODE(n)->a;
	return AST_NODE(n)->op == N_FNUM;
}

/* binary128 folding: a value is exact at that precision (ld) or a double or an integer */
static void cc_constant_float128_set(cval *v, u128 q)
{
	v->kind = CV_FLT;
	v->ld = true;
	v->q = q;
	v->f = vsoftfloat_to_double(q);
}
static u128 cc_constant_float128_get(const cval *v, bool sgn)
{
	return v->kind == CV_INT ? vsoftfloat_of_integer(v->i, sgn) : v->ld ? v->q : vsoftfloat_of_double(v->f);
}
/* nonzero, as a condition */
static bool cc_value_is_nonzero(cc *c, const cval *v) { return v->kind == CV_BIGINT ? cc_bitint_value_is_nonzero(c, v) : v->ld ? vsoftfloat_is_nonzero(v->q) : v->f != 0; }
/* an address that may be null: a weak symbol's (clang: Decl::isWeak) */
static bool cc_value_is_weak(const cc *c, const cval *v) { return v->kind == CV_ADDR && v->sym && (c->syms[v->sym].flags & SF_WEAK); }

CC_PRIVATE int cc_count_leading_zeros_128(u128 x, unsigned bits)
{
	int n = 0;

	for (int b = (int)bits - 1; b >= 0 && !((x >> b) & 1); b--)
		n++;
	return n;
}

/* Initializers may refer back to themselves, directly or through another const.
   Emit workers evaluate the same immutable cc concurrently: keep each recursion
   path thread-local, and distinguish symbols from different translation units. */
struct cc_constant_eval_guard {
	struct cc_constant_eval_guard *prev;
	uint32_t sym;
	cc *owner;
};
static _Thread_local struct cc_constant_eval_guard *cc_constant_eval_path;

/* Aggregate copies keep a reference to the source object in the AST. Follow
   only certified constant objects, with the same cycle guard as evaluation. */
static bool cc_constexpr_unwrap(cc *c, uint32_t n, uint32_t *out, bool *ice)
{
	const node *x = AST_NODE(n);

	if (vstack_guard_low())
		return false;
	if (x->op == N_CAST && x->aux == CK_NOP)
		return cc_constexpr_unwrap(c, x->a, out, ice);
	if (x->op == N_COMPLIT) {
		*ice = false;
		return cc_constexpr_unwrap(c, x->a, out, ice);
	}
	if (x->op == N_VAR) {
		const sym *s = &c->syms[x->a];

		if (!(s->flags & SF_CONSTEXPR) || !s->body || (s->flags & SF_INVALID))
			return false;
		for (struct cc_constant_eval_guard *p = cc_constant_eval_path; p; p = p->prev)
			if (p->owner == c && p->sym == x->a)
				return false;
		struct cc_constant_eval_guard frame = { cc_constant_eval_path, x->a, c };

		cc_constant_eval_path = &frame;
		bool ok = cc_constexpr_unwrap(c, s->body, out, ice);
		cc_constant_eval_path = frame.prev;
		return ok;
	}
	*out = n;
	return true;
}

/* A zero initializer or a string element need not exist as an AST node.
   Keep those values here rather than appending nodes/pool entries while emit
   workers are traversing the already parsed translation unit. */
typedef struct cc_constexpr_object {
	uint32_t init;          /* 0: implicit zero or the integer value below */
	qty ty;
	i128 integer;
} cc_constexpr_object;

static bool cc_constexpr_object_unwrap(cc *c, cc_constexpr_object *obj, bool *ice)
{
	if (!obj->init) return true;
	if (!cc_constexpr_unwrap(c, obj->init, &obj->init, ice)) return false;
	obj->ty = AST_NODE(obj->init)->ty;
	return true;
}

/* Resolve a constant aggregate subobject to the initializer which supplies it.
   Identity matters: a const object is not a C23 constexpr object. */
static bool cc_constexpr_subobject(cc *c, uint32_t n, cc_constexpr_object *out, bool *ice, bool static_fold)
{
	const node *x = AST_NODE(n);
	cc_constexpr_object obj;

	*out = (cc_constexpr_object){ .ty = x->ty };
	switch (x->op) {
	case N_VAR: {
		const sym *s = &c->syms[x->a];

		if (!s->body || (s->flags & SF_INVALID) || s->kind != S_VAR)
			return false;
		if (!(s->flags & SF_CONSTEXPR)) {
			/* clang, static data only: a read of a const, non-volatile object with an
			   initializer (clk_main[0], ca[1].index), not an ICE (int e[ci[0]] is a
			   folded VLA), not for a constexpr definition (cc_constexpr_initializer_is_ok) */
			qty t = s->ty;
			unsigned q = QQ(t);

			while (cc_type_is_array(c, t))
				t = AST_TYPE(c, t)->base, q |= QQ(t);
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
		if (static_fold && !cc_initializer_static_is_ok(c, x->ty, x->a, &bad))
			return false;
		out->init = x->a;
		*ice = false;
		return x->a != 0;
	}
	case N_CAST:
		if (x->aux != CK_NOP && x->aux != CK_DECAY)
			return false;
		return cc_constexpr_subobject(c, x->a, out, ice, static_fold);
	case N_COMMA: {
		uint32_t bad = 0;

		if (!static_fold || !cc_initializer_static_is_ok(c, AST_NODE(x->a)->ty, x->a, &bad))
			return false;
		*ice = false;
		return cc_constexpr_subobject(c, x->b, out, ice, static_fold);
	}
	case N_MEMBER: {
		if (!cc_constexpr_subobject(c, x->a, &obj, ice, static_fold) || !cc_constexpr_object_unwrap(c, &obj, ice))
			return false;
		uint32_t init = obj.init;
		if (!init || AST_NODE(init)->op == N_ZERO)
			return true;
		if (AST_NODE(init)->op == N_CPLX && x->c < 2) {
			out->init = x->c ? AST_NODE(init)->b : AST_NODE(init)->a;
			return true;
		}
		if (AST_NODE(init)->op != N_INIT || AST_NODE(init)->aux == 1 || x->c >= AST_NODE(init)->c ||
		    (AST_NODE(init)->aux == 2 && AST_NODE(init)->a != x->c))
			return false;
		out->init = c->lists[AST_NODE(init)->b + x->c];
		return true;
	}
	case N_INDEX: {
		cval v;

		if (!cc_constexpr_subobject(c, x->a, &obj, ice, static_fold) || !cc_constexpr_object_unwrap(c, &obj, ice) ||
		    !cc_constant_eval(c, x->b, &v) || v.kind != CV_INT || v.i < 0 || (u128)v.i >= (u128)cc_type_array_length(c, obj.ty))
			return false;
		uint32_t init = obj.init;
		if (!init || AST_NODE(init)->op == N_ZERO)
			return true;
		if (AST_NODE(init)->op == N_STR) {
			const cstr *str = &c->strs[AST_NODE(init)->a];
			i128 value = 0;

			if ((u128)v.i >= str->n)
				return false;
			for (unsigned k = 0; k < str->esz; k++)
				value |= (i128)(unsigned char)str->s[(size_t)v.i * str->esz + k] << (8*k);
			out->integer = cc_constant_truncate(c, value, x->ty);
			return true;
		}
		if (AST_NODE(init)->op != N_INIT || AST_NODE(init)->aux != 1)
			return false;
		const uint32_t *a = &c->lists[AST_NODE(init)->b];

		for (uint32_t i = 0; i < AST_NODE(init)->c; i++)
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
	cc_constexpr_object obj;
	bool ice = true, indexed = false;
	uint32_t root = n;

	if (vstack_guard_low() || (AST_NODE(n)->op != N_MEMBER && AST_NODE(n)->op != N_INDEX))
		return false;
	while (AST_NODE(root)->op == N_MEMBER || AST_NODE(root)->op == N_INDEX || AST_NODE(root)->op == N_CAST) {
		indexed |= AST_NODE(root)->op == N_INDEX;
		root = AST_NODE(root)->a;
	}
	/* Clang accepts compound-literal member selection here, but not an
	   indexed array compound literal. C23 constexpr arrays stay separate. */
	if (indexed && AST_NODE(root)->op == N_COMPLIT)
		return false;
	if (!cc_constexpr_subobject(c, n, &obj, &ice, true) || obj.init == n || (!obj.init && obj.integer))
		return false;
	*init = obj.init;
	return true;
}

static bool cc_constexpr_value(cc *c, uint32_t n, cval *v)
{
	cc_constexpr_object obj;
	uint32_t root = n;
	bool ice = true;

	while (AST_NODE(root)->op == N_MEMBER || AST_NODE(root)->op == N_INDEX || AST_NODE(root)->op == N_CAST)
		root = AST_NODE(root)->a;
	uint32_t sym = AST_NODE(root)->op == N_VAR ? AST_NODE(root)->a : 0;
	for (struct cc_constant_eval_guard *p = cc_constant_eval_path; sym && p; p = p->prev)
		if (p->owner == c && p->sym == sym)
			return false;
	struct cc_constant_eval_guard frame = { cc_constant_eval_path, sym, c };

	cc_constant_eval_path = &frame;
	bool ok = cc_constexpr_subobject(c, n, &obj, &ice, true) && obj.init != n;
	if (ok) {
		if (obj.init) ok = cc_constant_eval(c, obj.init, v);
		else {
			memset(v, 0, sizeof *v);
			v->kind = cc_type_is_float(c, obj.ty) ? CV_FLT : CV_INT;
			v->i = obj.integer;
			ok = cc_type_is_scalar(c, obj.ty);
		}
	}
	cc_constant_eval_path = frame.prev;
	if (ok)
		v->ice = ice && (v->kind == CV_INT || v->kind == CV_BIGINT);
	return ok;
}

/* Constant-initializer grammar. Ordinary const variables can be folded as an
   extension elsewhere, but cannot make a constexpr definition valid. */
CC_PRIVATE bool cc_constexpr_initializer_is_ok(cc *c, uint32_t n)
{
	const node *x;
	cval v;
	cc_constexpr_object obj;
	bool ice = true;

	if (!n)
		return true;
	x = AST_NODE(n);
	if (AST_NODE_IS_ERROR(n))
		return true;
	if (cc_bit_cast_vector_is_constant(c,n))
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
			if (!cc_constexpr_initializer_is_ok(c, a[x->aux == 1 ? 3*i+2 : i]))
				return false;
		}
		return true;
	}
	case N_VAR: {
		for (struct cc_constant_eval_guard *p = cc_constant_eval_path; p; p = p->prev)
			if (p->owner == c && p->sym == x->a)
				return false;
		struct cc_constant_eval_guard frame = { cc_constant_eval_path, x->a, c };

		cc_constant_eval_path = &frame;
		bool ok = cc_constexpr_subobject(c, n, &obj, &ice, false) && obj.init != n && cc_constexpr_initializer_is_ok(c, obj.init);
		cc_constant_eval_path = frame.prev;
		return ok;
	}
	case N_MEMBER: case N_INDEX:
		if (cc_bit_cast_is_constant(c, n)) return true;
		return cc_constant_eval(c, n, &v) && cc_constexpr_subobject(c, n, &obj, &ice, false) && obj.init != n && cc_constexpr_initializer_is_ok(c, obj.init);
	case N_COMPLIT:
		return cc_constexpr_initializer_is_ok(c, x->a);
	case N_CAST: case N_UNARY:
		if (x->op == N_CAST && x->aux == CK_BITS) return cc_bit_cast_is_constant(c, n);
		return cc_constexpr_initializer_is_ok(c, x->a) && cc_constant_eval(c, n, &v);
	case N_BINARY:
		return cc_constexpr_initializer_is_ok(c, x->a) && cc_constexpr_initializer_is_ok(c, x->b) && cc_constant_eval(c, n, &v);
	case N_LOGAND: case N_LOGOR:
		if (!cc_constexpr_initializer_is_ok(c, x->a) || !cc_constant_eval(c, x->a, &v))
			return false;
		if ((v.kind == CV_INT && (v.i != 0) == (x->op == N_LOGOR)) ||
		    ((v.kind == CV_FLT || v.kind == CV_BIGINT) && cc_value_is_nonzero(c, &v) == (x->op == N_LOGOR)))
			return true;
		return cc_constexpr_initializer_is_ok(c, x->b) && cc_constant_eval(c, n, &v);
	case N_COND:
		if (!cc_constexpr_initializer_is_ok(c, x->a) || !cc_constant_eval(c, x->a, &v))
			return false;
		return cc_constexpr_initializer_is_ok(c, (v.kind == CV_INT ? v.i != 0 : cc_value_is_nonzero(c, &v)) ?
					      (x->b ? x->b : x->a) : x->c) && cc_constant_eval(c, n, &v);
	case N_CPLX:
		return cc_constexpr_initializer_is_ok(c, x->a) && cc_constexpr_initializer_is_ok(c, x->b);
	case N_BUILTIN:
		return cc_constant_eval(c, n, &v) && (v.ice || x->aux == BI_INF || x->aux == BI_INFF ||
			x->aux == BI_HUGE_VAL || x->aux == BI_HUGE_VALF || x->aux == BI_NAN ||
			x->aux == BI_NANF || x->aux == BI_NANS);
	default:
		return false;
	}
}

static bool cc_constexpr_type_is_variably_modified(cc *c, qty t)
{
	while (cc_type_is_array(c, t) || cc_type_is_pointer(c, t)) {
		if (AST_TYPE(c, t)->flags & TF_VLA)
			return true;
		t = AST_TYPE(c, t)->base;
	}
	return false;
}

/* Constraints apply recursively to array elements and aggregate members, but
   not through pointers: pointer-to-volatile is a valid constexpr null pointer. */
CC_PRIVATE bool cc_constexpr_type_is_valid(cc *c, qty t)
{
	if ((QQ(t) & (Q_VOLATILE | Q_RESTRICT)) || (AST_TYPE(c, t)->flags & TF_ATOMIC) || cc_constexpr_type_is_variably_modified(c, t))
		return false;
	if (cc_type_is_array(c, t))
		return cc_constexpr_type_is_valid(c, AST_TYPE(c, t)->base);
	if (cc_type_is_record(c, t)) {
		const tag *r = &c->tags[AST_TYPE(c, t)->aux];

		for (uint32_t i = 0; i < r->nfield; i++)
			if (!cc_constexpr_type_is_valid(c, r->f[i].ty))
				return false;
	}
	return true;
}

/* Called before the implicit initialization conversion, so an explicit cast
   remains a deliberate rounding/truncation in the source expression. */
CC_PRIVATE void cc_constexpr_conversion_check(cc *c, uint32_t n, qty to)
{
	qty from = AST_NODE(n)->ty;
	cval a, b;
	bool exact = true;

	if (cc_type_is_pointer(c, to)) {
		if (cc_constant_eval(c, n, &a) && (a.kind == CV_ADDR || (a.kind == CV_INT && a.i != 0)))
			cc_report_error(c, cc_expression_location(c, n), "constexpr pointer initializer is not null");
		return;
	}
	if (cc_type_is_complex(c, from) && !cc_type_is_complex(c, to)) {
		cc_report_error(c, cc_expression_location(c, n), "constexpr initializer for type '%s' is of type '%s'", tyname(c, to), tyname(c, from));
		return;
	}
	if (!cc_type_is_arithmetic(c, to) || !cc_constant_eval(c, n, &a))
		return;
	if (cc_type_is_integer(c, to) && a.kind == CV_FLT) {
		cc_report_error(c, cc_expression_location(c, n), "constexpr initializer for type '%s' is of type '%s'", tyname(c, to), tyname(c, from));
		return;
	}
	if (a.kind == CV_BIGINT)
		exact = cc_bitint_conversion_is_exact(c, &a, from, to);
	else if (cc_type_is_integer(c, to) && a.kind == CV_INT)
		exact = cc_type_is_bitint(c, to) ? (cc_type_is_signed(c, to) || !cc_type_is_signed(c, from) || a.i >= 0) : cc_constant_truncate(c, a.i, to) == a.i;
	else if (cc_type_is_float(c, to) && (a.kind == CV_INT || a.kind == CV_FLT)) {
		uint32_t converted = cc_conversion_apply(c, n, to);

		if (!cc_constant_eval(c, converted, &b) || b.kind != CV_FLT)
			return;
		u128 aq = cc_constant_float128_get(&a, cc_type_is_signed(c, from)), bq = cc_constant_float128_get(&b, true);

		exact = (vsoftfloat_is_nan(aq) && vsoftfloat_is_nan(bq)) || vsoftfloat_compare(aq, bq) == 0;
		if (vsoftfloat_is_nan(aq) && !(aq & ((u128)1 << 111)) && AST_TYPE_KIND(c, from) != AST_TYPE_KIND(c, to))
			exact = false;
		if (a.kind == CV_INT) {
			u128 magnitude = cc_type_is_signed(c, from) && a.i < 0 ? (u128)0 - (u128)a.i : (u128)a.i;
			unsigned bits = 128u - (unsigned)cc_count_leading_zeros_128(magnitude, 128);
			unsigned precision = AST_TYPE_KIND(c, to) == TY_FLOAT16 ? 11 : AST_TYPE_KIND(c, to) == TY_BFLOAT16 ? 8 :
				AST_TYPE_KIND(c, to) == TY_FLOAT ? 24 : AST_TYPE_KIND(c, to) == TY_DOUBLE ? 53 : (unsigned)cc_float_precision_bits(c, to) + 1;

			/* Converting back can saturate at an integer endpoint and conceal rounding. */
			if (bits > precision && (magnitude & (((u128)1 << (bits - precision)) - 1)))
				exact = false;
		}
	}
	if (!exact) {
		char value[96];
		char *wide = a.kind == CV_BIGINT ? cc_bitint_value_format(c, &a, from) : NULL;

		if (a.kind == CV_INT)
			cc_format_i128(value, sizeof value, a.i);
		else
			snprintf(value, sizeof value, "%.9g", a.f);
		cc_report_error(c, cc_expression_location(c, n), "constexpr initializer evaluates to %s which is not exactly representable in type '%s'", wide ? wide : value, tyname(c, to));
		free(wide);
	}
}

CC_PRIVATE bool cc_constant_eval(cc *c, uint32_t n, cval *v)
{
	const node *x = AST_NODE(n);
	cval a, b;

	memset(v, 0, sizeof(*v));
	if (x->op == N_BIGNUM || ((x->op == N_CAST || x->op == N_UNARY) && (cc_type_is_bitint(c, x->ty) || cc_type_is_bitint(c, AST_NODE(x->a)->ty))) ||
	    (x->op == N_BINARY && (cc_type_is_bitint(c, AST_NODE(x->a)->ty) || cc_type_is_bitint(c, AST_NODE(x->b)->ty))))
		return cc_bitint_read_eval(c, n, v);
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
	/* the parser's stack check bounds the nesting (cc_cast_expression_parse); ASan's frames make
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
		if (cc_type_is_float128(c, x->ty))
			v->q = cc_float_pool_float128(c, x->a), v->ld = true;
		v->ice = true;	/* only as the operand of a cast; arithmetic clears it */
		return true;
	case N_ZERO:
		v->kind = cc_type_is_float(c, x->ty) ? CV_FLT : CV_INT;
		v->ice = true;
		return cc_type_is_scalar(c, x->ty);
	case N_MEMBER:
		return cc_constexpr_value(c, n, v) || cc_bit_cast_value(c, n, v);
	case N_VAR: {
		/* a const integer variable with a constant initializer folds (not an ICE) */
		const sym *s = &c->syms[x->a];

		if (s->kind == S_VAR && (QQ(s->ty) & (Q_CONST | Q_VOLATILE)) == Q_CONST && (cc_type_is_integer(c, s->ty) || (s->flags & SF_CONSTEXPR)) && s->body && !(s->flags & SF_INVALID)) {
			for (struct cc_constant_eval_guard *p = cc_constant_eval_path; p; p = p->prev)
				if (p->owner == c && p->sym == x->a)
					return false;
			struct cc_constant_eval_guard frame = { cc_constant_eval_path, x->a, c };

			cc_constant_eval_path = &frame;
			bool ok = cc_constant_eval(c, s->body, v) && (v->kind == CV_INT || v->kind == CV_BIGINT || (s->flags & SF_CONSTEXPR));
			cc_constant_eval_path = frame.prev;
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
			return cc_constant_eval_address(c, x->a, v);
		case CK_VOID:
			return false;
		case CK_NULL:
			v->kind = CV_INT;
			v->ice = cc_constant_eval(c, x->a, &a) && a.ice;
			return true;
		case CK_BITS:
			return cc_bit_cast_value(c, n, v);
		case CK_UNION: case CK_SPLAT:
			return false;
		}
		if (!cc_constant_eval(c, x->a, &a))
			return false;
		*v = a;
		switch (x->aux) {
		case CK_NOP:
			if (a.kind == CV_INT && !cc_type_is_pointer(c, to))
				v->i = cc_constant_truncate(c, a.i, to);
			return true;
		case CK_INT:
			if (a.kind == CV_ADDR && a.sublabel && cc_type_size(c, to) >= 4)
				return true;	/* a label difference, as a 4- or 8-byte word */
			if (a.kind != CV_INT)
				return false;
			v->i = cc_constant_truncate(c, a.i, to);
			return true;
		case CK_BOOL:
			v->kind = CV_INT;
			if (a.kind == CV_INT)
				v->i = a.i != 0;
			else if (a.kind == CV_FLT)
				v->i = cc_value_is_nonzero(c, &a), v->ice = cc_constant_is_float_literal(c, x->a);
			else if (cc_value_is_weak(c, &a))
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
			if (cc_type_is_float128(c, to)) {
				cc_constant_float128_set(v, vsoftfloat_of_integer_with_precision(a.i, cc_type_is_signed(c, AST_NODE(x->a)->ty), cc_float_precision_bits(c, to)));
				return true;
			}
			v->f = cc_type_is_signed(c, AST_NODE(x->a)->ty) ? (double)a.i : (double)(u128)a.i;
			if (AST_TYPE_KIND(c, to) == TY_BFLOAT16)
				v->f = bf_to_d(vsoftfloat_to_bfloat16(vsoftfloat_of_integer(a.i, cc_type_is_signed(c, AST_NODE(x->a)->ty))));
			else if (AST_TYPE_KIND(c, to) == TY_FLOAT16)
				v->f = h_to_d(vsoftfloat_to_half(vsoftfloat_of_integer(a.i, cc_type_is_signed(c, AST_NODE(x->a)->ty))));
			else if (AST_TYPE_KIND(c, to) == TY_FLOAT)
				v->f = cc_type_is_signed(c, AST_NODE(x->a)->ty) ? (double)(float)a.i : (double)(float)(u128)a.i;	/* one rounding */
			return true;
		case CK_F2I:
			if (a.kind != CV_FLT)
				return false;
			v->kind = CV_INT;
			v->i = cc_constant_truncate(c, vsoftfloat_to_integer_saturating(cc_constant_float128_get(&a, true), (int)cc_type_integer_bits(c, to), cc_type_is_signed(c, to)), to);
			v->ld = false;
			v->ice = cc_constant_is_float_literal(c, x->a);
			return true;
		case CK_F2F:
			if (a.kind != CV_FLT)
				return false;
			if (a.ld && vsoftfloat_is_nan(a.q)) a.q |= (u128)1 << 111;
			v->ice = false;
			if (cc_type_is_float128(c, to))
				cc_constant_float128_set(v, vsoftfloat_to_precision(cc_constant_float128_get(&a, true), cc_float_precision_bits(c, to)));
			else if (AST_TYPE_KIND(c, to) == TY_BFLOAT16)
				v->f = bf_to_d(vsoftfloat_to_bfloat16(cc_constant_float128_get(&a, true))), v->ld = false;
			else if (AST_TYPE_KIND(c, to) == TY_FLOAT16)
				v->f = h_to_d(vsoftfloat_to_half(cc_constant_float128_get(&a, true))), v->ld = false;
			else if (a.ld && AST_TYPE_KIND(c, to) == TY_FLOAT)
				v->f = vsoftfloat_to_float(a.q), v->ld = false;	/* one rounding */
			else if (AST_TYPE_KIND(c, to) == TY_FLOAT)
				v->f = (float)a.f;
			else
				v->ld = false;	/* f already holds the nearest double */
			return true;
		case CK_P2I:
			v->ice = false;
			if (a.kind == CV_INT)
				v->i = cc_constant_truncate(c, a.i, to);
			else if (cc_type_size(c, to) < 8)
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
		if (!cc_constant_eval(c, x->a, &a))
			return false;
		*v = a;
		if (a.kind == CV_FLT) {
			v->ice = false;
			if (x->aux == LEX_TOKEN_MINUS)
				v->f = -a.f, v->q = a.q ^ Q_SIGN;
			else if (x->aux == LEX_TOKEN_EXCLAMATION)
				v->kind = CV_INT, v->i = !cc_value_is_nonzero(c, &a), v->ld = false;
			return true;
		}
		if (a.kind == CV_ADDR) {
			if (x->aux == LEX_TOKEN_EXCLAMATION && !cc_value_is_weak(c, &a)) {
				v->kind = CV_INT, v->i = 0, v->ice = false, v->sym = v->str = v->label = 0;
				return true;
			}
			return false;
		}
		switch (x->aux) {
		case LEX_TOKEN_MINUS: v->i = cc_constant_truncate(c, (i128)((u128)0 - (u128)a.i), x->ty); break;
		case LEX_TOKEN_TILDE: v->i = cc_constant_truncate(c, ~a.i, x->ty); break;
		case LEX_TOKEN_EXCLAMATION: v->i = a.i == 0; break;
		}
		return true;
	case N_COMPLIT:	/* (AST_TYPE){ v } of scalar type: its value */
		if (!cc_type_is_scalar(c, x->ty) || !x->a)
			return false;
		if (AST_NODE(x->a)->op == N_INIT)
			return false;
		if (!cc_constant_eval(c, x->a, v))
			return false;
		v->ice = false;
		return true;
	case N_CALL: {	/* strlen("literal"): clang folds the library builtin */
		uint32_t f = x->a, s;
		int l;
		const char *nm;

		while (AST_NODE(f)->op == N_CAST || AST_NODE(f)->op == N_ADDR)
			f = AST_NODE(f)->a;
		if (AST_NODE(f)->op != N_VAR || x->c != 1 || c->syms[AST_NODE(f)->a].kind != S_FUNC || c->syms[AST_NODE(f)->a].sc == SC_STATIC)
			return false;
		nm = cc_identifier_name(c->syms[AST_NODE(f)->a].name, &l);
		if (l != 6 || memcmp(nm, "strlen", 6))
			return false;
		s = c->lists[x->b];
		while (AST_NODE(s)->op == N_CAST)
			s = AST_NODE(s)->a;
		if (AST_NODE(s)->op != N_STR || c->strs[AST_NODE(s)->a].esz != 1)
			return false;
		v->kind = CV_INT;
		v->ice = false;
		v->i = (i128)strlen(c->strs[AST_NODE(s)->a].s);
		return true;
	}
	case N_BINARY: {
		int op = x->aux;
		qty lt = AST_NODE(x->a)->ty, rt;
		bool ok;

		if (!cc_constant_eval(c, x->a, &a) || !cc_constant_eval(c, x->b, &b))
			return false;
		rt = AST_NODE(x->b)->ty;
		v->kind = CV_INT;
		v->ice = a.ice && b.ice;
		/* pointer arithmetic and comparisons */
		if (cc_type_is_pointer(c, lt) || cc_type_is_pointer(c, rt) || a.kind == CV_ADDR || b.kind == CV_ADDR) {
			v->ice = false;
			if (cc_type_is_pointer(c, x->ty)) {	/* ptr +- int */
				const cval *pv = cc_type_is_pointer(c, lt) ? &a : &b, *iv = cc_type_is_pointer(c, lt) ? &b : &a;
				uint64_t es = cc_pointer_element_size(c, cc_type_is_pointer(c, lt) ? lt : rt);

				if (iv->kind != CV_INT)
					return false;
				*v = *pv;
				v->ice = false;
				v->i += (op == LEX_TOKEN_MINUS ? -iv->i : iv->i) * (i128)es;
				return true;
			}
			if (op == LEX_TOKEN_MINUS && cc_type_is_pointer(c, lt) && cc_type_is_pointer(c, rt) && a.kind == CV_ADDR && b.kind == CV_ADDR &&
			    a.label && b.label && a.label != b.label && !a.sublabel && !b.sublabel) {
				/* &&l1 - &&l0 */
				*v = a;
				v->ice = false;
				v->sublabel = b.label;
				v->i = a.i - b.i;
				return true;
			}
			if (op == LEX_TOKEN_MINUS && cc_type_is_pointer(c, lt) && cc_type_is_pointer(c, rt)) {
				if (a.kind != b.kind || a.sym != b.sym || a.str != b.str || a.label != b.label)
					return false;
				v->i = (a.i - b.i) / (i128)cc_pointer_element_size(c, lt);
				return true;
			}
			/* an address cast to a 64-bit integer, plus or minus an integer: a relocation */
			if ((op == LEX_TOKEN_PLUS || op == LEX_TOKEN_MINUS) && cc_type_size(c, x->ty) == 8 &&
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
					if (((a.kind == CV_INT && a.i == 0) || (b.kind == CV_INT && b.i == 0)) && !cc_value_is_weak(c, &a) && !cc_value_is_weak(c, &b)) {
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
		    (a.ld || b.ld || cc_type_is_float128(c, x->ty) || cc_type_is_float128(c, AST_NODE(x->a)->ty) || cc_type_is_float128(c, AST_NODE(x->b)->ty))) {
			int p = cc_float_precision_bits(c, AST_NODE(x->a)->ty) < cc_float_precision_bits(c, AST_NODE(x->b)->ty) ? cc_float_precision_bits(c, AST_NODE(x->a)->ty) : cc_float_precision_bits(c, AST_NODE(x->b)->ty);
			u128 l = vsoftfloat_to_precision(cc_constant_float128_get(&a, cc_type_is_signed(c, AST_NODE(x->a)->ty)), p), r = vsoftfloat_to_precision(cc_constant_float128_get(&b, cc_type_is_signed(c, AST_NODE(x->b)->ty)), p);
			int k = vsoftfloat_compare(l, r);

			v->ice = false;
			v->ld = false;
			switch (op) {
			case LEX_TOKEN_PLUS: cc_constant_float128_set(v, vsoftfloat_add_with_precision(l, r, p)); break;
			case LEX_TOKEN_MINUS: cc_constant_float128_set(v, vsoftfloat_subtract_with_precision(l, r, p)); break;
			case LEX_TOKEN_STAR: cc_constant_float128_set(v, vsoftfloat_multiply_with_precision(l, r, p)); break;
			case LEX_TOKEN_SLASH: cc_constant_float128_set(v, vsoftfloat_divide_with_precision(l, r, p)); break;
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
			if (v->kind == CV_FLT && AST_TYPE_KIND(c, x->ty) == TY_BFLOAT16)
				v->f = bf_to_d(vsoftfloat_to_bfloat16(vsoftfloat_of_double(v->f)));
			if (v->kind == CV_FLT && AST_TYPE_KIND(c, x->ty) == TY_FLOAT16)
				v->f = h_to_d(vsoftfloat_to_half(vsoftfloat_of_double(v->f)));
			if (v->kind == CV_FLT && AST_TYPE_KIND(c, x->ty) == TY_FLOAT)
				v->f = (float)v->f;
			return true;
		}
		{
			qty ot = AST_NODE(x->a)->ty;	/* operands are converted to the common type */
			bool sg = cc_type_is_signed(c, ot);
			i128 l = a.i, r = b.i;
			uint64_t bits = cc_type_integer_bits(c, ot);

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
					v->i = (i128)(op == LEX_TOKEN_SLASH ? (u128)cc_constant_truncate(c, l, cc_type_unsigned(c, ot)) / (u128)cc_constant_truncate(c, r, cc_type_unsigned(c, ot))
								    : (u128)cc_constant_truncate(c, l, cc_type_unsigned(c, ot)) % (u128)cc_constant_truncate(c, r, cc_type_unsigned(c, ot)));
				break;
			case LEX_TOKEN_LESS_LESS:
			case LEX_TOKEN_GREATER_GREATER: {
				/* as clang's C folding (shift-count-overflow warns): a count over the
				   width is width - 1, a negative count shifts the other way */
				uint64_t w = cc_type_integer_bits(c, x->ty);
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
					v->i = cc_type_is_signed(c, x->ty) ? l >> r : (i128)((u128)cc_constant_truncate(c, l, cc_type_unsigned(c, x->ty)) >> r);
				break;
			}
			case LEX_TOKEN_AMPERSAND: v->i = l & r; break;
			case LEX_TOKEN_PIPE: v->i = l | r; break;
			case LEX_TOKEN_CARET: v->i = l ^ r; break;
			case LEX_TOKEN_LESS: v->i = sg ? l < r : (u128)cc_constant_truncate(c, l, cc_type_unsigned(c, ot)) < (u128)cc_constant_truncate(c, r, cc_type_unsigned(c, ot)); break;
			case LEX_TOKEN_LESS_EQUAL: v->i = sg ? l <= r : (u128)cc_constant_truncate(c, l, cc_type_unsigned(c, ot)) <= (u128)cc_constant_truncate(c, r, cc_type_unsigned(c, ot)); break;
			case LEX_TOKEN_GREATER: v->i = sg ? l > r : (u128)cc_constant_truncate(c, l, cc_type_unsigned(c, ot)) > (u128)cc_constant_truncate(c, r, cc_type_unsigned(c, ot)); break;
			case LEX_TOKEN_GREATER_EQUAL: v->i = sg ? l >= r : (u128)cc_constant_truncate(c, l, cc_type_unsigned(c, ot)) >= (u128)cc_constant_truncate(c, r, cc_type_unsigned(c, ot)); break;
			case LEX_TOKEN_EQUAL_EQUAL: v->i = cc_constant_truncate(c, l, ot) == cc_constant_truncate(c, r, ot); break;
			case LEX_TOKEN_EXCLAMATION_EQUAL: v->i = cc_constant_truncate(c, l, ot) != cc_constant_truncate(c, r, ot); break;
			default: ok = false;
			}
			(void)bits;
			if (!ok)
				return false;
			if (!cc_type_is_pointer(c, x->ty))
				v->i = cc_constant_truncate(c, v->i, x->ty);
			return true;
		}
	}
	case N_LOGAND:
	case N_LOGOR: {
		bool t;

		if (!cc_constant_eval(c, x->a, &a))
			return false;
		if (cc_value_is_weak(c, &a))
			return false;
		t = a.kind == CV_INT ? a.i != 0 : a.kind == CV_FLT || a.kind == CV_BIGINT ? cc_value_is_nonzero(c, &a) : true;
		v->kind = CV_INT;
		if (a.kind == CV_ADDR)
			a.ice = false;
		if ((x->op == N_LOGAND && !t) || (x->op == N_LOGOR && t)) {
			v->i = x->op == N_LOGOR;
			v->ice = a.ice;
			return true;
		}
		if (!cc_constant_eval(c, x->b, &b) || cc_value_is_weak(c, &b))
			return false;
		v->i = b.kind == CV_INT ? b.i != 0 : b.kind == CV_FLT || b.kind == CV_BIGINT ? cc_value_is_nonzero(c, &b) : 1;
		v->ice = a.ice && b.ice && b.kind != CV_ADDR;
		return true;
	}
	case N_COND: {
		bool t, ice;

		if (!cc_constant_eval(c, x->a, &a))
			return false;
		if (cc_value_is_weak(c, &a))
			return false;
		t = a.kind == CV_INT ? a.i != 0 : a.kind == CV_FLT || a.kind == CV_BIGINT ? cc_value_is_nonzero(c, &a) : true;
		ice = a.ice && a.kind != CV_ADDR;
		if (!x->b) {	/* a ?: c */
			if (t) {
				*v = a;
				return true;
			}
			if (!cc_constant_eval(c, x->c, v))
				return false;
			v->ice = v->ice && ice;
			return true;
		}
		if (!cc_constant_eval(c, t ? x->b : x->c, v))
			return false;
		v->ice = v->ice && ice;
		return true;
	}
	case N_COMMA:
		if (!cc_constant_eval(c, x->b, v))
			return false;
		v->ice = false;
		return true;
	case N_ADDR:
		if (AST_NODE(x->a)->op == N_LABELADDR)
			return cc_constant_eval(c, x->a, v);
		return cc_constant_eval_address(c, x->a, v);
	case N_LABELADDR:
		v->kind = CV_ADDR;
		v->label = x->a + 1;
		return true;
	case N_INDEX:
	case N_DEREF: {
		if (x->op == N_INDEX && (cc_constexpr_value(c, n, v) || cc_bit_cast_value(c, n, v)))
			return true;
		/* "abc"[1], *"abc": an element of a string literal folds (clang) */
		const cstr *st;
		size_t sz = cc_type_is_integer(c, x->ty) ? cc_type_size(c, x->ty) : 0;
		int64_t e;

		if (!sz || !cc_constant_eval_address(c, n, &a) || a.kind != CV_ADDR || !a.str || a.sym || a.label || a.sublabel)
			return false;
		st = &c->strs[a.str - 1];
		if (sz != st->esz || a.i < 0 || a.i % (i128)sz || a.i / (i128)sz >= st->n)
			return false;
		e = 0;
		for (size_t k = 0; k < sz; k++)	/* little endian */
			e |= (int64_t)(uint8_t)st->s[(size_t)a.i + k] << (8 * k);
		if (cc_type_is_signed(c, x->ty) && sz < 8)
			e = (int64_t)((uint64_t)e << (64 - 8 * sz)) >> (64 - 8 * sz);
		v->kind = CV_INT;
		v->i = e;
		v->ice = false;
		return true;
	}
	case N_BUILTIN:
		return cc_builtin_fold(c, x, v);
	default:
		return false;
	}
}

/* is n a null pointer constant (C11 6.3.2.3p3, clang: ICE or (void *)ICE) */
CC_PRIVATE bool cc_expression_is_null_pointer_constant(cc *c, uint32_t n)
{
	const node *x = AST_NODE(n);
	cval v;

	if (cc_type_is_integer(c, x->ty))
		return cc_constant_eval(c, n, &v) && v.kind == CV_INT && v.ice && v.i == 0;
	if (cc_type_is_pointer(c, x->ty) && x->op == N_CAST && (x->aux == CK_I2P || x->aux == CK_NULL || x->aux == CK_P2P)) {
		qty pt = AST_TYPE(c, x->ty)->base;

		if (AST_TYPE_KIND(c, pt) != TY_VOID || QQ(pt))
			return false;
		if (x->aux == CK_P2P)
			return cc_expression_is_null_pointer_constant(c, x->a);
		return cc_constant_eval(c, n, &v) && v.kind == CV_INT && v.ice && v.i == 0;
	}
	return false;
}

/* int value of a constant expression (array sizes, widths, cases, enums) */
CC_PRIVATE bool cc_constant_integer(cc *c, uint32_t n, i128 *out)
{
	cval v;

	if (!cc_constant_eval(c, n, &v) || v.kind != CV_INT)
		return false;
	*out = v.i;
	return true;
}
