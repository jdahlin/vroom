/* __builtin_bit_cast: object representations, without numeric conversions.
 * Constant reads use bounded 128-bit windows. Padding remains unknown rather
 * than becoming a fabricated zero; no buffer proportional to an object size. */
#include "ccimpl.h"

static u128 bc_mask(unsigned n)
{
	return n >= 128 ? ~(u128)0 : n ? ((u128)1 << n) - 1 : 0;
}

VPRIV uint32_t bitcast_expr(cc *c, uint32_t loc)
{
	expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
	qty to = type_name(c);
	expect(c, LEX_TOKEN_COMMA, "','");
	uint32_t a = assign_expr(c);
	expect_here(c, LEX_TOKEN_RIGHT_PAREN, "')'");
	qty from = N(a)->ty;
	if (!complete(c, to) || !complete(c, from) || KIND(c, to) == TY_VOID || KIND(c, from) == TY_FUNC ||
	    KIND(c, to) == TY_FUNC || KIND(c, to) == TY_SVE || KIND(c, from) == TY_SVE || (T(c, to)->flags & TF_VLA) ||
	    (T(c, from)->flags & TF_VLA)) {
		cc_error(c, loc, "__builtin_bit_cast requires complete object types of constant size");
		return err_node(c, loc);
	}
	if (T(c, from)->flags & TF_ATOMIC) {
		cc_error(c, loc, "__builtin_bit_cast source type must be trivially copyable");
		return err_node(c, loc);
	}
	if (ty_size(c, to) != ty_size(c, from)) {
		cc_error(c, loc, "__builtin_bit_cast source and destination have different sizes (%llu and %llu bytes)",
			 (unsigned long long)ty_size(c, from), (unsigned long long)ty_size(c, to));
		return err_node(c, loc);
	}
	return cast_node(c, a, QUNQ(to), CK_BITS);
}

/* A whole bit_cast must be eligible even if only one target member is read. */
static bool bc_type(cc *c, qty t, unsigned depth)
{
	if (depth > 128 || (QQ(t) & Q_VOLATILE))
		return false;
	int k = KIND(c, t);
	if (k == TY_PTR || k == TY_UNION || k == TY_FUNC || k == TY_SVE || k == TY_VOID)
		return false;
	if (k == TY_ARRAY || k == TY_VECTOR)
		return bc_type(c, T(c, t)->base, depth + 1);
	if (k == TY_STRUCT) {
		const tag *g = &c->tags[T(c, t)->aux];
		for (uint32_t i = 0; i < g->nfield; i++)
			if (!bc_type(c, g->f[i].ty, depth + 1))
				return false;
	}
	return true;
}

static uint32_t bc_index_base(cc *c, const node *index)
{
	uint32_t n = index->a;
	for (unsigned i = 0; i < 512; i++) {
		const node *x = N(n);
		if (x->op == N_CAST && x->aux == CK_P2P) {
			qty from = N(x->a)->ty;
			bool same = is_ptr(c, from) && is_ptr(c, x->ty) &&
				    compatible(c, QUNQ(T(c, from)->base), QUNQ(T(c, x->ty)->base));
			bool lane = index->c && is_vector(c, index->c) && N(x->a)->op == N_ADDR &&
				    is_vector(c, N(N(x->a)->a)->ty) &&
				    compatible(c, QUNQ(T(c, index->c)->base), QUNQ(T(c, x->ty)->base));
			if (!same && !lane)
				break;
			n = x->a;
		} else if (x->op == N_CAST && (x->aux == CK_DECAY || x->aux == CK_VADDR || x->aux == CK_NOP))
			n = x->a;
		else if (x->op == N_ADDR && (is_array(c, N(x->a)->ty) || is_vector(c, N(x->a)->ty)))
			n = x->a;
		else
			break;
	}
	return n;
}

/* Resolve initializer identity without synthesizing AST nodes. Union member
 * access must refer to the active initializer, even when its bytes overlap. */
static bool bc_initializer(cc *c, uint32_t n, uint32_t *out, unsigned depth)
{
	if (!n) {
		*out = 0;
		return true;
	}
	if (depth > 512 || vstack_guard_low())
		return false;
	const node *x = N(n);
	if (x->op == N_VAR)
		return bc_initializer(c, c->syms[x->a].body, out, depth + 1);
	if (x->op == N_COMPLIT || (x->op == N_CAST && x->aux == CK_NOP))
		return bc_initializer(c, x->a, out, depth + 1);
	if (x->op == N_INIT || x->op == N_ZERO) {
		*out = x->op == N_ZERO ? 0 : n;
		return true;
	}
	if (x->op == N_MEMBER) {
		uint32_t init;
		if (!bc_initializer(c, x->a, &init, depth + 1))
			return false;
		if (KIND(c, N(x->a)->ty) == TY_UNION && x->c != (init ? N(init)->a : 0))
			return false;
		return bc_initializer(c, init && x->c < N(init)->c ? c->lists[N(init)->b + x->c] : 0, out, depth + 1);
	}
	if (x->op == N_INDEX) {
		uint32_t base = bc_index_base(c, x), init;
		cval ix;
		if (!is_array(c, N(base)->ty) || !eval(c, x->b, &ix) || ix.kind != CV_INT || ix.i < 0 ||
		    (u128)ix.i >= arr_len(c, N(base)->ty) || !bc_initializer(c, base, &init, depth + 1))
			return false;
		if (!init) {
			*out = 0;
			return true;
		}
		const node *a = N(init);
		if (a->aux == 1) {
			for (uint32_t i = 0; i < a->c; i++) {
				const uint32_t *entry = &c->lists[a->b + i * 3];
				if ((u128)ix.i >= entry[0] && (u128)ix.i <= entry[1])
					return bc_initializer(c, entry[2], out, depth + 1);
			}
			*out = 0;
			return true;
		}
		return bc_initializer(c, (u128)ix.i < a->c ? c->lists[a->b + (uint32_t)ix.i] : 0, out, depth + 1);
	}
	return false;
}

static bool bc_active_member(cc *c, const node *x)
{
	if (KIND(c, N(x->a)->ty) != TY_UNION)
		return true;
	uint32_t init;
	return bc_initializer(c, x->a, &init, 0) && x->c == (init ? N(init)->a : 0);
}

/* Intermediate unsigned-byte objects may carry padding through a nested
 * bit_cast. A constexpr definition still requires every destination value bit
 * initialized; merely reading one known member does not read the padding. */
static bool bc_grammar(cc *c, uint32_t n, unsigned depth, bool strict)
{
	if (!n)
		return true;
	if (depth > 512 || vstack_guard_low())
		return false;
	const node *x = N(n);
	if (x->op == N_ZERO || x->op == N_NUM || x->op == N_FNUM)
		return true;
	if (x->op == N_STR)
		return false;
	if (x->op == N_VAR) {
		const sym *sym = &c->syms[x->a];
		if (!sym->body || (sym->flags & SF_INVALID))
			return false;
		if (!(sym->flags & SF_CONSTEXPR) && (strict || (QQ(sym->ty) & (Q_CONST | Q_VOLATILE)) != Q_CONST))
			return false;
		if (N(sym->body)->op == N_STR)
			return true;
		return bc_grammar(c, sym->body, depth + 1, strict);
	}
	if (x->op == N_COMPLIT)
		return N(x->a)->op == N_STR || bc_grammar(c, x->a, depth + 1, strict);
	if (x->op == N_INIT) {
		for (uint32_t i = 0; i < x->c; i++) {
			if (x->aux == 2 && i != x->a)
				continue;
			uint32_t child = c->lists[x->b + (x->aux == 1 ? 3 * i + 2 : i)];
			if (child && N(child)->op == N_STR)
				continue; /* initializer storage */
			if (!bc_grammar(c, child, depth + 1, strict))
				return false;
		}
		return true;
	}
	if (x->op == N_CAST) {
		if (x->aux == CK_BITS && (!bc_type(c, x->ty, 0) || !bc_type(c, N(x->a)->ty, 0)))
			return false;
		return bc_grammar(c, x->a, depth + 1, strict);
	}
	if (x->op == N_LOGAND || x->op == N_LOGOR) {
		cval test;
		if (!bc_grammar(c, x->a, depth + 1, strict) || !eval(c, x->a, &test))
			return false;
		bool yes = test.kind == CV_INT	 ? test.i != 0
			   : test.kind == CV_FLT ? test.ld ? vsoftfloat_is_nonzero(test.q) : test.f != 0
						 : false;
		return (x->op == N_LOGAND ? !yes : yes) || bc_grammar(c, x->b, depth + 1, strict);
	}
	if (x->op == N_BINARY || x->op == N_CPLX)
		return bc_grammar(c, x->a, depth + 1, strict) && bc_grammar(c, x->b, depth + 1, strict);
	if (x->op == N_COND) {
		cval test;
		if (!bc_grammar(c, x->a, depth + 1, strict) || !eval(c, x->a, &test))
			return false;
		bool yes = test.kind == CV_INT	 ? test.i != 0
			   : test.kind == CV_FLT ? test.ld ? vsoftfloat_is_nonzero(test.q) : test.f != 0
						 : false;
		return bc_grammar(c, yes ? (x->b ? x->b : x->a) : x->c, depth + 1, strict);
	}
	if (x->op == N_MEMBER && !bc_active_member(c, x))
		return false;
	if (x->op == N_UNARY || x->op == N_MEMBER)
		return bc_grammar(c, x->a, depth + 1, strict);
	if (x->op == N_INDEX)
		return bc_grammar(c, bc_index_base(c, x), depth + 1, strict) && bc_grammar(c, x->b, depth + 1, strict);
	if (x->op == N_BUILTIN && (x->aux == BI_CONVERTVECTOR || x->aux == BI_SHUFFLEVECTOR)) {
		for (uint32_t i = 0; i < x->c; i++)
			if (!bc_grammar(c, c->lists[x->b + i], depth + 1, strict))
				return false;
		return true;
	}
	if (is_vector(c, x->ty) && x->op != N_ZERO)
		return false;
	return constexpr_init_ok(c, n);
}

static bool bc_source_ok(cc *c, uint32_t n, unsigned depth)
{
	return bc_grammar(c, n, depth, false);
}

/* Per-read validation memo: nested casts reuse the proof for each intermediate
 * representation. It is local to this invocation, never shared by emitters. */
typedef struct {
	uint32_t *valid;
	size_t capacity, used;
} bc_state;

static bool bc_memo_has(const bc_state *state, uint32_t n)
{
	if (!state->capacity)
		return false;
	size_t slot = ((uint64_t)n * 2654435761u) & (state->capacity - 1);
	while (state->valid[slot] && state->valid[slot] != n)
		slot = (slot + 1) & (state->capacity - 1);
	return state->valid[slot] == n;
}

static bool bc_memo_add(bc_state *state, uint32_t n)
{
	/* Keep a free slot even after recursive validation inserts many children.
	 * A fixed cache that silently saturates restores exponential traversal. */
	if (!state->capacity || state->used >= state->capacity / 2) {
		if (state->capacity > SIZE_MAX / (2 * sizeof(*state->valid)))
			return false;
		size_t capacity = state->capacity ? state->capacity * 2 : 32;
		uint32_t *valid = calloc(capacity, sizeof(*valid));
		if (!valid)
			return false;
		for (size_t i = 0; i < state->capacity; i++) {
			uint32_t key = state->valid[i];
			if (!key)
				continue;
			size_t slot = ((uint64_t)key * 2654435761u) & (capacity - 1);
			while (valid[slot])
				slot = (slot + 1) & (capacity - 1);
			valid[slot] = key;
		}
		free(state->valid);
		state->valid = valid;
		state->capacity = capacity;
	}
	size_t slot = ((uint64_t)n * 2654435761u) & (state->capacity - 1);
	while (state->valid[slot] && state->valid[slot] != n)
		slot = (slot + 1) & (state->capacity - 1);
	if (!state->valid[slot])
		state->valid[slot] = n, state->used++;
	return true;
}

static bool bc_read(bc_state *, cc *, uint32_t, qty, uint64_t, unsigned, u128 *, u128 *, unsigned);
static bool bc_check(bc_state *, cc *, uint32_t, qty, uint64_t, unsigned, bool);
static bool bc_lane(bc_state *, cc *, uint32_t, uint64_t, cval *, unsigned);

/* Put an overlapping part of a child object into the requested window. */
static bool bc_part(bc_state *state, cc *c, uint32_t n, qty t, uint64_t pos, uint64_t size, uint64_t off, unsigned count,
		    u128 *v, u128 *known, unsigned depth)
{
	if (pos >= off + count || off >= pos + size)
		return true;
	uint64_t lo = pos > off ? pos : off, hi = pos + size < off + count ? pos + size : off + count;
	u128 a, b;
	if (!bc_read(state, c, n, t, lo - pos, (unsigned)(hi - lo), &a, &b, depth + 1))
		return false;
	unsigned shift = (unsigned)(lo - off);
	*v |= a << shift;
	*known |= b << shift;
	return true;
}

static bool bc_encode(cc *c, qty t, cval cv, u128 *v, unsigned *valid)
{
	*valid = (unsigned)(ty_size(c, t) * 8);
	if (is_integer(c, t)) {
		if (cv.kind != CV_INT)
			return false;
		*valid = int_bits(c, t);
		/* _Bool occupies a byte; representations other than 0 and 1 are invalid. */
		if (KIND(c, t) == TY_BOOL)
			*valid = 8;
		*v = (u128)cv.i & bc_mask(*valid);
		return true;
	}
	if (!is_float(c, t) || cv.kind != CV_FLT)
		return false;
	u128 q = cv.ld ? cv.q : vsoftfloat_of_double(cv.f);
	if (cv.ld && vsoftfloat_is_nan(q) && *valid <= 64) {
		*v = vsoftfloat_nan_storage(q,
				   KIND(c, t) == TY_BFLOAT16 ? 7
				   : *valid == 16	     ? 10
				   : *valid == 32	     ? 23
							     : 52,
				   KIND(c, t) == TY_BFLOAT16 ? 8
				   : *valid == 16	     ? 5
				   : *valid == 32	     ? 8
							     : 11);
		return true;
	}
	if (KIND(c, t) == TY_FLOAT16)
		*v = vsoftfloat_to_half(q);
	else if (KIND(c, t) == TY_BFLOAT16)
		*v = vsoftfloat_to_bfloat16(q);
	else if (*valid == 32) {
		float f = (float)cv.f;
		uint32_t bits;
		memcpy(&bits, &f, 4);
		*v = bits;
	} else if (*valid == 64) {
		uint64_t bits;
		memcpy(&bits, &cv.f, 8);
		*v = bits;
	} else if (CC_X86_64(c) && KIND(c, t) == TY_LDOUBLE) {
		uint8_t b[10];
		vsoftfloat_to_x87(q, b);
		*v = 0;
		for (unsigned i = 0; i < 10; i++)
			*v |= (u128)b[i] << (i * 8);
		*valid = 80;
	} else
		*v = q;
	return true;
}

static bool bc_scalar(bc_state *state, cc *c, uint32_t n, qty t, u128 *v, unsigned *valid)
{
	cval cv = {0};
	if (n && !eval(c, n, &cv) && !bc_lane(state, c, n, 0, &cv, 0))
		return false;
	if (!n)
		cv.kind = is_float(c, t) ? CV_FLT : CV_INT;
	return bc_encode(c, t, cv, v, valid);
}

static bool bc_read(bc_state *state, cc *c, uint32_t n, qty t, uint64_t off, unsigned count, u128 *v, u128 *known,
		    unsigned depth)
{
	*v = *known = 0;
	if (depth > 512 || vstack_guard_low() || count > 128 || off > ty_size(c, t) * 8 || count > ty_size(c, t) * 8 - off)
		return false;
	const node *x = n ? N(n) : NULL;
	if (x && x->op == N_CAST && x->aux == CK_BITS) {
		if (!bc_type(c, x->ty, 0) || !bc_type(c, N(x->a)->ty, 0) || !bc_source_ok(c, x->a, depth + 1))
			return false;
		/* A byte may carry indeterminate padding, but an invalid bool or an
		 * indeterminate non-byte scalar cannot be hidden by another cast. */
		if (!bc_memo_has(state, n)) {
			if (!bc_check(state, c, x->a, x->ty, 0, depth + 1, true) || !bc_memo_add(state, n))
				return false;
		}
		if (!bc_read(state, c, x->a, N(x->a)->ty, off, count, v, known, depth + 1))
			return false;
		u128 ignored, value_bits;
		if (!bc_read(state, c, 0, x->ty, off, count, &ignored, &value_bits, depth + 1))
			return false;
		*known &= value_bits;
		*v &= value_bits;
		return true;
	}
	if (x && x->op == N_CAST && (x->aux == CK_NOP || x->aux == CK_DECAY))
		return bc_read(state, c, x->a, N(x->a)->ty, off, count, v, known, depth + 1);
	if (x && x->op == N_VAR) {
		const sym *s = &c->syms[x->a];
		if (((s->flags & SF_CONSTEXPR) == 0 && (QQ(s->ty) & (Q_CONST | Q_VOLATILE)) != Q_CONST) || !s->body ||
		    (s->flags & SF_INVALID))
			return false;
		/* The depth bound also bounds mutually recursive invalid initializers. */
		return bc_read(state, c, s->body, s->ty, off, count, v, known, depth + 1);
	}
	if (x && x->op == N_COMPLIT)
		return bc_read(state, c, x->a, t, off, count, v, known, depth + 1);
	if (x && x->op == N_MEMBER) {
		if (!bc_active_member(c, x))
			return false;
		const field *f = node_field(c, x);
		if (!f->bitfield)
			return bc_read(state, c, x->a, N(x->a)->ty, f->off + off, count, v, known, depth + 1);
		unsigned take = off >= f->width ? 0 : count < f->width - off ? count : (unsigned)(f->width - off);
		if (take && !bc_read(state, c, x->a, N(x->a)->ty, f->off + off, take, v, known, depth + 1))
			return false;
		if (take < count) {
			u128 high = bc_mask(count) & ~bc_mask(take);
			if (!is_signed(c, f->ty))
				*known |= high;
			else {
				u128 sign, sk;
				if (!bc_read(state, c, x->a, N(x->a)->ty, f->off + f->width - 1, 1, &sign, &sk, depth + 1))
					return false;
				if (sk) {
					*known |= high;
					if (sign)
						*v |= high;
				}
			}
		}
		return true;
	}
	if (x && x->op == N_INDEX) {
		cval ix;
		uint32_t base = bc_index_base(c, x);
		qty bt = N(base)->ty;
		if ((!is_array(c, bt) && !is_vector(c, bt)) || !eval(c, x->b, &ix) || ix.kind != CV_INT || ix.i < 0)
			return false;
		uint64_t num = is_array(c, bt) ? arr_len(c, bt) : T(c, bt)->ext ? T(c, bt)->ext : T(c, bt)->n;
		if ((u128)ix.i >= num)
			return false;
		bool packed = is_vector(c, bt) && KIND(c, T(c, bt)->base) == TY_BOOL;
		if (packed) {
			if (off) {
				*known = bc_mask(count);
				return true;
			}
			if (!bc_read(state, c, base, bt, (uint64_t)ix.i, 1, v, known, depth + 1))
				return false;
			*known |= bc_mask(count) & ~(u128)1;
			return true;
		}
		return bc_read(state, c, base, bt, (uint64_t)ix.i * ty_size(c, t) * 8 + off, count, v, known, depth + 1);
	}
	if (x && x->op == N_STR) {
		const cstr *s = &c->strs[x->a];
		for (unsigned i = 0; i < count; i++) {
			uint64_t b = off + i;
			if (b / 8 < (uint64_t)s->n * s->esz)
				*v |= (u128)(((unsigned char)s->s[b / 8] >> (b % 8)) & 1) << i;
		}
		*known = bc_mask(count);
		return true;
	}
	if (x && x->op == N_ZERO)
		n = 0, x = NULL;
	int k = KIND(c, t);
	if (k == TY_ARRAY || k == TY_VECTOR) {
		qty et = T(c, t)->base;
		uint64_t es = ty_size(c, et) * 8;
		bool packed = k == TY_VECTOR && KIND(c, et) == TY_BOOL;
		if (packed)
			es = 1;
		if (!es)
			return true;
		uint64_t num = k == TY_ARRAY ? arr_len(c, t) : T(c, t)->ext ? T(c, t)->ext : T(c, t)->n;
		if (x && x->op == N_CAST && x->aux == CK_SPLAT) {
			for (uint64_t i = off / es; i < (off + count + es - 1) / es && i < num; i++)
				if (!bc_part(state, c, x->a, et, i * es, es, off, count, v, known, depth))
					return false;
			return true;
		}
		if (x && x->op != N_INIT) {
			for (uint64_t i = off / es; i < (off + count + es - 1) / es && i < num; i++) {
				cval lane;
				u128 raw;
				unsigned valid;
				if (!bc_lane(state, c, n, i, &lane, depth + 1) || !bc_encode(c, et, lane, &raw, &valid))
					return false;
				uint64_t lo = i * es > off ? i * es : off,
					 hi = (i + 1) * es < off + count ? (i + 1) * es : off + count;
				unsigned shift = (unsigned)(lo - off), skip = (unsigned)(lo - i * es),
					 take = (unsigned)(hi - lo);
				*v |= ((raw >> skip) & bc_mask(take)) << shift;
				*known |= ((bc_mask(valid) >> skip) & bc_mask(take)) << shift;
			}
			return true;
		}
		for (uint64_t i = off / es; i < (off + count + es - 1) / es && i < num; i++) {
			uint32_t child = 0;
			if (x)
				for (uint32_t j = 0; j < x->c; j++) {
					if (x->aux == 1) {
						const uint32_t *a = &c->lists[x->b + 3 * j];
						if (a[0] <= i && i <= a[1])
							child = a[2];
					} else if (i == j)
						child = c->lists[x->b + j];
				}
			if (!bc_part(state, c, child, et, i * es, es, off, count, v, known, depth))
				return false;
		}
		return true;
	}
	if (k == TY_STRUCT || k == TY_UNION) {
		if (x && x->op != N_INIT && x->op != N_CPLX)
			return false;
		const tag *g = &c->tags[T(c, t)->aux];
		for (uint32_t i = 0; i < g->nfield; i++) {
			const field *f = &g->f[i];
			if (f->bitfield && !f->name)
				continue;
			if (k == TY_UNION && i != (x ? x->a : 0))
				continue;
			uint32_t child = 0;
			if (x && x->op == N_INIT && i < x->c)
				child = c->lists[x->b + i];
			if (x && x->op == N_CPLX && i < 2)
				child = i ? x->b : x->a;
			if (!bc_part(state, c, child, f->ty, f->off, f->bitfield ? f->width : ty_size(c, f->ty) * 8, off, count,
				     v, known, depth))
				return false;
		}
		return true;
	}
	u128 bits;
	unsigned valid;
	if (!bc_scalar(state, c, n, t, &bits, &valid))
		return false;
	if (off < 128) {
		*v = (bits >> off) & bc_mask(count);
		*known = (bc_mask(valid) >> off) & bc_mask(count);
	}
	return true;
}

static bool bc_bytes_read(bc_state *state, cc *c, uint32_t n, uint64_t off, unsigned len, unsigned char *data,
			  unsigned char *known)
{
	u128 v, k;
	if (len > 16 || !bc_read(state, c, n, N(n)->ty, off * 8, len * 8, &v, &k, 0))
		return false;
	for (unsigned i = 0; i < len; i++)
		data[i] = (unsigned char)(v >> (8 * i)), known[i] = (unsigned char)(k >> (8 * i));
	return true;
}

static bool bc_decode(cc *c, qty t, u128 raw, cval *v)
{
	unsigned bits = (unsigned)(ty_size(c, t) * 8), valid = is_integer(c, t) ? int_bits(c, t) : bits;
	if (CC_X86_64(c) && KIND(c, t) == TY_LDOUBLE)
		valid = 80;
	memset(v, 0, sizeof(*v));
	v->ice = true;
	if (is_integer(c, t)) {
		if (KIND(c, t) == TY_BOOL && raw > 1)
			return false;
		v->kind = CV_INT;
		v->i = trunc_to(c, (i128)raw, t);
		return true;
	}
	v->kind = CV_FLT;
	if (bits <= 64) {
		unsigned p = KIND(c, t) == TY_BFLOAT16 ? 7 : bits == 16 ? 10 : bits == 32 ? 23 : 52;
		unsigned eb = KIND(c, t) == TY_BFLOAT16 ? 8 : bits == 16 ? 5 : bits == 32 ? 8 : 11;
		u128 frac = raw & bc_mask(p);
		if (((raw >> p) & bc_mask(eb)) == bc_mask(eb) && frac) {
			v->ld = true;
			v->q = ((raw >> (p + eb)) & 1) << 127 | (u128)0x7fff << 112 | frac << (112 - p);
			v->f = vsoftfloat_to_double(v->q);
			return true;
		}
	}
	if (KIND(c, t) == TY_FLOAT16)
		v->f = h_to_d((uint16_t)raw);
	else if (KIND(c, t) == TY_BFLOAT16)
		v->f = bf_to_d((uint16_t)raw);
	else if (bits == 32) {
		uint32_t b = (uint32_t)raw;
		float f;
		memcpy(&f, &b, 4);
		v->f = f;
	} else if (bits == 64) {
		uint64_t b = (uint64_t)raw;
		memcpy(&v->f, &b, 8);
	} else {
		v->ld = true;
		if (valid == 80) {
			unsigned exp = (unsigned)(raw >> 64) & 0x7fff;
			uint64_t sig = (uint64_t)raw;
			if ((exp != 0) != ((sig >> 63) != 0))
				return false; /* unsupported x87 encoding */
			v->q = ((raw >> 79) & 1) << 127 | (u128)exp << 112 | ((u128)(sig & 0x7fffffffffffffffULL) << 49);
		} else
			v->q = raw;
		v->f = vsoftfloat_to_double(v->q);
	}
	return true;
}

static bool bitcast_value_read(bc_state *state, cc *c, uint32_t n, cval *v)
{
	/* This fallback must not turn ordinary const aggregate reads into ICEs.
	 * Explicit bit_cast operands have their own relaxed-fold/strict grammar. */
	if (N(n)->op == N_MEMBER || N(n)->op == N_INDEX) {
		uint32_t root = n;
		for (unsigned depth = 0; depth < 512; depth++) {
			const node *x = N(root);
			if (x->op == N_MEMBER)
				root = x->a;
			else if (x->op == N_INDEX)
				root = bc_index_base(c, x);
			else if (x->op == N_CAST && (x->aux == CK_NOP || x->aux == CK_DECAY))
				root = x->a;
			else {
				if (x->op == N_VAR && !(c->syms[x->a].flags & SF_CONSTEXPR))
					return false;
				break;
			}
		}
	}
	qty t = N(n)->ty;
	if (!is_integer(c, t) && !is_float(c, t))
		return false;
	unsigned bits = (unsigned)(ty_size(c, t) * 8), valid = is_integer(c, t) ? int_bits(c, t) : bits;
	if (bits > 128)
		return false;
	if (KIND(c, t) == TY_BOOL)
		valid = 8;
	if (CC_X86_64(c) && KIND(c, t) == TY_LDOUBLE)
		valid = 80;
	u128 raw, known;
	if (!bc_read(state, c, n, t, 0, bits, &raw, &known, 0) || (known & bc_mask(valid)) != bc_mask(valid))
		return false;
	return bc_decode(c, t, raw, v);
}

static bool bc_check(bc_state *state, cc *c, uint32_t n, qty t, uint64_t off, unsigned depth, bool carry)
{
	if (depth > 128)
		return false;
	int k = KIND(c, t);
	if (k == TY_ARRAY || k == TY_VECTOR) {
		qty et = T(c, t)->base;
		uint64_t sz = ty_size(c, et) * 8;
		if (k == TY_VECTOR && KIND(c, et) == TY_BOOL)
			sz = 1;
		uint64_t num = k == TY_ARRAY ? arr_len(c, t) : T(c, t)->ext ? T(c, t)->ext : T(c, t)->n;
		for (uint64_t i = 0; i < num; i++) {
			if (k == TY_VECTOR && KIND(c, et) == TY_BOOL) {
				u128 raw, known;
				if (!bc_read(state, c, n, N(n)->ty, off + i, 1, &raw, &known, depth + 1) || known != 1)
					return false;
			} else if (!bc_check(state, c, n, et, off + i * sz, depth + 1, carry))
				return false;
		}
		return true;
	}
	if (k == TY_STRUCT) {
		const tag *g = &c->tags[T(c, t)->aux];
		for (uint32_t i = 0; i < g->nfield; i++) {
			const field *f = &g->f[i];
			if (f->bitfield && !f->name)
				continue;
			u128 v, known;
			if (f->bitfield) {
				if (!bc_read(state, c, n, N(n)->ty, off + f->off, f->width, &v, &known, 0) ||
				    known != bc_mask(f->width))
					return false;
			} else if (!bc_check(state, c, n, f->ty, off + f->off, depth + 1, carry))
				return false;
		}
		return true;
	}
	unsigned bits = (unsigned)ty_size(c, t) * 8;
	if (is_integer(c, t) && KIND(c, t) != TY_BOOL)
		bits = int_bits(c, t);
	if (CC_X86_64(c) && k == TY_LDOUBLE)
		bits = 80;
	u128 v, known;
	if (bits > 128 || !bc_read(state, c, n, N(n)->ty, off, bits, &v, &known, 0))
		return false;
	if (known != bc_mask(bits))
		return carry && (k == TY_UCHAR || (k == TY_CHAR && !is_signed(c, t)));
	if (CC_X86_64(c) && k == TY_LDOUBLE) {
		unsigned exp = (unsigned)(v >> 64) & 0x7fff;
		if ((exp != 0) != (((uint64_t)v >> 63) != 0))
			return false;
	}
	return k != TY_BOOL || v <= 1;
}

static bool bitcast_constant_read(bc_state *state, cc *c, uint32_t n)
{
	/* Restrict this fallback to a view rooted in a bit_cast; ordinary constexpr
	 * expressions continue through their existing language-specific checks. */
	uint32_t root = n;
	for (unsigned d = 0; d < 512; d++) {
		const node *x = N(root);
		if (x->op == N_CAST && x->aux == CK_BITS)
			return bc_type(c, x->ty, 0) && bc_type(c, N(x->a)->ty, 0) && bc_grammar(c, x->a, 0, true) &&
			       bc_check(state, c, n, N(n)->ty, 0, 0, false);
		if (x->op == N_MEMBER || x->op == N_INDEX || (x->op == N_CAST && (x->aux == CK_NOP || x->aux == CK_DECAY)))
			root = x->a;
		else if (x->op == N_VAR && (c->syms[x->a].flags & SF_CONSTEXPR) && c->syms[x->a].body)
			root = c->syms[x->a].body;
		else
			break;
	}
	return false;
}

/* Constant vector expressions are evaluated lane by lane without allocating
 * AST nodes: this reader is also used by parallel static-data emitters. */
static u128 bc_q(const cval *v, bool sign)
{
	return v->kind == CV_FLT ? v->ld ? v->q : vsoftfloat_of_double(v->f) : vsoftfloat_of_integer(v->i, sign);
}

static bool bc_convert_value(cc *c, cval *v, qty from, qty to)
{
	if (QUNQ(from) == QUNQ(to))
		return true;
	if (is_integer(c, to)) {
		i128 i = v->kind == CV_INT ? v->i : vsoftfloat_to_integer_saturating(bc_q(v, true), (int)int_bits(c, to), is_signed(c, to));
		if (KIND(c, to) == TY_BOOL)
			i = v->kind == CV_INT ? v->i != 0 : vsoftfloat_is_nonzero(bc_q(v, true));
		memset(v, 0, sizeof(*v));
		v->kind = CV_INT;
		v->i = trunc_to(c, i, to);
		return true;
	}
	if (!is_float(c, to))
		return false;
	u128 q = bc_q(v, is_signed(c, from));
	memset(v, 0, sizeof(*v));
	v->kind = CV_FLT;
	if (KIND(c, to) == TY_FLOAT16)
		v->f = h_to_d(vsoftfloat_to_half(q));
	else if (KIND(c, to) == TY_BFLOAT16)
		v->f = bf_to_d(vsoftfloat_to_bfloat16(q));
	else if (KIND(c, to) == TY_FLOAT)
		v->f = vsoftfloat_to_float(q);
	else if (KIND(c, to) == TY_DOUBLE)
		v->f = vsoftfloat_to_double(q);
	else {
		v->ld = true;
		v->q = vsoftfloat_to_precision(q, q_prec(c, to));
		v->f = vsoftfloat_to_double(v->q);
	}
	return true;
}

static bool bc_lane(bc_state *state, cc *c, uint32_t n, uint64_t lane, cval *v, unsigned depth)
{
	if (depth > 512 || vstack_guard_low())
		return false;
	const node *x = N(n);
	qty t = x->ty;
	bool vector = is_vector(c, t);
	if (!vector && eval(c, n, v))
		return true;
	qty et = vector ? T(c, t)->base : t;
	if (x->op == N_VAR) {
		const sym *s = &c->syms[x->a];
		if (!s->body || (s->flags & SF_INVALID))
			return false;
		return bc_lane(state, c, s->body, lane, v, depth + 1);
	}
	if (x->op == N_CAST && x->aux == CK_BITS)
		return false; /* failed representation evaluation above */
	if (x->op == N_COMPLIT || (x->op == N_CAST && x->aux == CK_NOP))
		return bc_lane(state, c, x->a, lane, v, depth + 1);
	if (x->op == N_CAST && (x->aux == CK_SPLAT || !vector))
		return bc_lane(state, c, x->a, lane, v, depth + 1) && bc_convert_value(c, v, N(x->a)->ty, et);
	if (x->op == N_BUILTIN && (x->aux == BI_CONVERTVECTOR || x->aux == BI_SHUFFLEVECTOR)) {
		const uint32_t *args = &c->lists[x->b];
		uint32_t source = args[0];
		uint64_t index = lane;
		if (x->aux == BI_SHUFFLEVECTOR) {
			cval ix;
			if (lane + 2 >= x->c || !eval(c, args[lane + 2], &ix) || ix.kind != CV_INT || ix.i < 0)
				return false;
			uint64_t count = T(c, N(source)->ty)->ext ? T(c, N(source)->ty)->ext : T(c, N(source)->ty)->n;
			index = (uint64_t)ix.i;
			if (index >= count)
				source = args[1], index -= count;
		}
		if (!bc_lane(state, c, source, index, v, depth + 1))
			return false;
		return x->aux == BI_SHUFFLEVECTOR || bc_convert_value(c, v, T(c, N(source)->ty)->base, et);
	}
	if (x->op == N_COND) {
		cval test;
		if (!bc_lane(state, c, x->a, lane, &test, depth + 1))
			return false;
		bool yes = test.kind == CV_INT	 ? test.i != 0
			   : test.kind == CV_FLT ? test.ld ? vsoftfloat_is_nonzero(test.q) : test.f != 0
						 : false;
		return bc_lane(state, c, yes ? (x->b ? x->b : x->a) : x->c, lane, v, depth + 1);
	}
	if (x->op == N_UNARY) {
		if (!bc_lane(state, c, x->a, lane, v, depth + 1))
			return false;
		if (v->kind == CV_FLT) {
			if (x->aux == LEX_TOKEN_MINUS)
				v->f = -v->f, v->q ^= Q_SIGN;
			else if (x->aux != LEX_TOKEN_PLUS)
				return false;
		} else {
			if (x->aux == LEX_TOKEN_MINUS)
				v->i = (i128)(-(u128)v->i);
			else if (x->aux == LEX_TOKEN_TILDE)
				v->i = KIND(c, et) == TY_BOOL ? !v->i : ~v->i;
			else if (x->aux == LEX_TOKEN_EXCLAMATION)
				v->i = (vector ? -1 : 1) * (v->i == 0);
			else if (x->aux != LEX_TOKEN_PLUS)
				return false;
			v->i = trunc_to(c, v->i, et);
		}
		return true;
	}
	if (x->op == N_BINARY) {
		cval a, b;
		if (!bc_lane(state, c, x->a, lane, &a, depth + 1) || !bc_lane(state, c, x->b, lane, &b, depth + 1))
			return false;
		int op = x->aux;
		bool compare = op == LEX_TOKEN_LESS || op == LEX_TOKEN_LESS_EQUAL || op == LEX_TOKEN_GREATER || op == LEX_TOKEN_GREATER_EQUAL || op == LEX_TOKEN_EQUAL_EQUAL ||
			       op == LEX_TOKEN_EXCLAMATION_EQUAL;
		qty at = is_vector(c, N(x->a)->ty) ? T(c, N(x->a)->ty)->base : N(x->a)->ty;
		memset(v, 0, sizeof(*v));
		if (a.kind == CV_FLT || b.kind == CV_FLT) {
			u128 l = bc_q(&a, is_signed(c, at)), r = bc_q(&b, is_signed(c, at)), q;
			int cmp = vsoftfloat_compare(l, r), p = q_prec(c, at);
			if (compare) {
				bool yes = op == LEX_TOKEN_LESS		? cmp == -1
					   : op == LEX_TOKEN_LESS_EQUAL	? cmp == -1 || cmp == 0
					   : op == LEX_TOKEN_GREATER	? cmp == 1
					   : op == LEX_TOKEN_GREATER_EQUAL	? cmp == 1 || cmp == 0
					   : op == LEX_TOKEN_EQUAL_EQUAL ? cmp == 0
								: cmp != 0;
				v->kind = CV_INT;
				v->i = yes ? (vector ? -1 : 1) : 0;
				return true;
			}
			if (op == LEX_TOKEN_PLUS)
				q = vsoftfloat_add_with_precision(l, r, p);
			else if (op == LEX_TOKEN_MINUS)
				q = vsoftfloat_subtract_with_precision(l, r, p);
			else if (op == LEX_TOKEN_STAR)
				q = vsoftfloat_multiply_with_precision(l, r, p);
			else if (op == LEX_TOKEN_SLASH)
				q = vsoftfloat_divide_with_precision(l, r, p);
			else
				return false;
			v->kind = CV_FLT;
			v->ld = true;
			v->q = q;
			v->f = vsoftfloat_to_double(q);
			return bc_convert_value(c, v, at, et);
		}
		u128 l = (u128)a.i, r = (u128)b.i, result = 0;
		bool sg = is_signed(c, at);
		v->kind = CV_INT;
		switch (op) {
		case LEX_TOKEN_PLUS:
			result = l + r;
			break;
		case LEX_TOKEN_MINUS:
			result = l - r;
			break;
		case LEX_TOKEN_STAR:
			result = l * r;
			break;
		case LEX_TOKEN_AMPERSAND:
			result = l & r;
			break;
		case LEX_TOKEN_PIPE:
			result = l | r;
			break;
		case LEX_TOKEN_CARET:
			result = l ^ r;
			break;
		case LEX_TOKEN_LESS_LESS:
		case LEX_TOKEN_GREATER_GREATER:
			if (b.i < 0 || r >= int_bits(c, at))
				return false;
			result = op == LEX_TOKEN_LESS_LESS ? l << (unsigned)r : sg ? (u128)(a.i >> (unsigned)r) : l >> (unsigned)r;
			break;
		case LEX_TOKEN_SLASH:
		case LEX_TOKEN_PERCENT:
			if (!r || (sg && a.i == ((i128)((u128)1 << 127)) && b.i == -1))
				return false;
			result = sg ? (u128)(op == LEX_TOKEN_SLASH ? a.i / b.i : a.i % b.i) : op == LEX_TOKEN_SLASH ? l / r : l % r;
			break;
		case LEX_TOKEN_EQUAL_EQUAL:
			result = -(l == r);
			break;
		case LEX_TOKEN_EXCLAMATION_EQUAL:
			result = -(l != r);
			break;
		case LEX_TOKEN_LESS:
			result = -(sg ? a.i < b.i : l < r);
			break;
		case LEX_TOKEN_LESS_EQUAL:
			result = -(sg ? a.i <= b.i : l <= r);
			break;
		case LEX_TOKEN_GREATER:
			result = -(sg ? a.i > b.i : l > r);
			break;
		case LEX_TOKEN_GREATER_EQUAL:
			result = -(sg ? a.i >= b.i : l >= r);
			break;
		default:
			return false;
		}
		if (compare && !vector)
			result = (u128)(result != 0);
		v->i = trunc_to(c, (i128)result, et);
		return true;
	}
	if (!vector)
		return false;
	unsigned stride = KIND(c, et) == TY_BOOL ? 1 : (unsigned)ty_size(c, et) * 8;
	u128 raw, known;
	if (!bc_read(state, c, n, t, lane * stride, stride, &raw, &known, depth + 1) || known != bc_mask(stride))
		return false;
	return bc_decode(c, et, raw, v);
}

/* Vector initializer validity and indexed constants use the same object reader
 * as bit_cast. This keeps packed boolean lanes as bits, including expressions
 * formed by splats and lane operations rather than initializer lists. */
static bool vector_constant_read(bc_state *state, cc *c, uint32_t n)
{
	if (!n)
		return false;
	const node *x = N(n);
	if (!is_vector(c, x->ty) && !(x->op == N_INDEX && is_vector(c, N(bc_index_base(c, x))->ty)))
		return false;
	return bc_grammar(c, n, 0, true) && bc_check(state, c, n, x->ty, 0, 0, false);
}

VPRIV bool bitcast_value(cc *c, uint32_t n, cval *v)
{
	bc_state state = {0};
	bool ok = bitcast_value_read(&state, c, n, v);
	free(state.valid);
	return ok;
}
VPRIV bool bitcast_constant(cc *c, uint32_t n)
{
	bc_state state = {0};
	bool ok = bitcast_constant_read(&state, c, n);
	free(state.valid);
	return ok;
}
VPRIV bool vector_constant(cc *c, uint32_t n)
{
	bc_state state = {0};
	bool ok = vector_constant_read(&state, c, n);
	free(state.valid);
	return ok;
}
bool cc_bitcast_read(cc *c, uint32_t n, uint64_t off, unsigned len, unsigned char *data, unsigned char *known)
{
	bc_state state = {0};
	bool ok = bc_bytes_read(&state, c, n, off, len, data, known);
	free(state.valid);
	return ok;
}
