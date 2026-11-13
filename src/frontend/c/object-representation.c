/* __builtin_bit_cast: object representations, without numeric conversions.
 * Constant reads use bounded 128-bit windows. Padding remains unknown rather
 * than becoming a fabricated zero; no buffer proportional to an object size. */
#include "cc-internal.h"

static u128 cc_bit_cast_mask(unsigned n)
{
	return n >= 128 ? ~(u128)0 : n ? ((u128)1 << n) - 1 : 0;
}

CC_PRIVATE uint32_t cc_bit_cast_build(cc *c, uint32_t loc)
{
	cc_token_expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
	qty to = cc_type_name_parse(c);
	cc_token_expect(c, LEX_TOKEN_COMMA, "','");
	uint32_t a = cc_assignment_parse(c);
	cc_token_expect_here(c, LEX_TOKEN_RIGHT_PAREN, "')'");
	qty from = AST_NODE(a)->ty;
	if (!cc_type_complete(c, to) || !cc_type_complete(c, from) || AST_TYPE_KIND(c, to) == TY_VOID || AST_TYPE_KIND(c, from) == TY_FUNC ||
	    AST_TYPE_KIND(c, to) == TY_FUNC || AST_TYPE_KIND(c, to) == TY_SVE || AST_TYPE_KIND(c, from) == TY_SVE || (AST_TYPE(c, to)->flags & TF_VLA) ||
	    (AST_TYPE(c, from)->flags & TF_VLA)) {
		cc_report_error(c, loc, "__builtin_bit_cast requires complete object types of constant size");
		return cc_node_error_new(c, loc);
	}
	if (AST_TYPE(c, from)->flags & TF_ATOMIC) {
		cc_report_error(c, loc, "__builtin_bit_cast source type must be trivially copyable");
		return cc_node_error_new(c, loc);
	}
	if (cc_type_size(c, to) != cc_type_size(c, from)) {
		cc_report_error(c, loc, "__builtin_bit_cast source and destination have different sizes (%llu and %llu bytes)",
			 (unsigned long long)cc_type_size(c, from), (unsigned long long)cc_type_size(c, to));
		return cc_node_error_new(c, loc);
	}
	return cc_cast_insert(c, a, QUNQ(to), CK_BITS);
}

/* A whole bit_cast must be eligible even if only one target member is read. */
static bool cc_bit_cast_type_is_eligible(cc *c, qty t, unsigned depth)
{
	if (depth > 128 || (QQ(t) & Q_VOLATILE))
		return false;
	int k = AST_TYPE_KIND(c, t);
	if (k == TY_PTR || k == TY_UNION || k == TY_FUNC || k == TY_SVE || k == TY_VOID)
		return false;
	if (k == TY_ARRAY || k == TY_VECTOR)
		return cc_bit_cast_type_is_eligible(c, AST_TYPE(c, t)->base, depth + 1);
	if (k == TY_STRUCT) {
		const tag *g = &c->tags[AST_TYPE(c, t)->aux];
		for (uint32_t i = 0; i < g->nfield; i++)
			if (!cc_bit_cast_type_is_eligible(c, g->f[i].ty, depth + 1))
				return false;
	}
	return true;
}

static uint32_t cc_bit_cast_index_base(cc *c, const node *index)
{
	uint32_t n = index->a;
	for (unsigned i = 0; i < 512; i++) {
		const node *x = AST_NODE(n);
		if (x->op == N_CAST && x->aux == CK_P2P) {
			qty from = AST_NODE(x->a)->ty;
			bool same = cc_type_is_pointer(c, from) && cc_type_is_pointer(c, x->ty) &&
				    cc_type_is_compatible(c, QUNQ(AST_TYPE(c, from)->base), QUNQ(AST_TYPE(c, x->ty)->base));
			bool lane = index->c && cc_type_is_vector(c, index->c) && AST_NODE(x->a)->op == N_ADDR &&
				    cc_type_is_vector(c, AST_NODE(AST_NODE(x->a)->a)->ty) &&
				    cc_type_is_compatible(c, QUNQ(AST_TYPE(c, index->c)->base), QUNQ(AST_TYPE(c, x->ty)->base));
			if (!same && !lane)
				break;
			n = x->a;
		} else if (x->op == N_CAST && (x->aux == CK_DECAY || x->aux == CK_VADDR || x->aux == CK_NOP))
			n = x->a;
		else if (x->op == N_ADDR && (cc_type_is_array(c, AST_NODE(x->a)->ty) || cc_type_is_vector(c, AST_NODE(x->a)->ty)))
			n = x->a;
		else
			break;
	}
	return n;
}

/* Resolve initializer identity without synthesizing AST nodes. Union member
 * access must refer to the active initializer, even when its bytes overlap. */
static bool cc_bit_cast_initializer_find(cc *c, uint32_t n, uint32_t *out, unsigned depth)
{
	if (!n) {
		*out = 0;
		return true;
	}
	if (depth > 512 || vstack_guard_low())
		return false;
	const node *x = AST_NODE(n);
	if (x->op == N_VAR)
		return cc_bit_cast_initializer_find(c, c->syms[x->a].body, out, depth + 1);
	if (x->op == N_COMPLIT || (x->op == N_CAST && x->aux == CK_NOP))
		return cc_bit_cast_initializer_find(c, x->a, out, depth + 1);
	if (x->op == N_INIT || x->op == N_ZERO) {
		*out = x->op == N_ZERO ? 0 : n;
		return true;
	}
	if (x->op == N_MEMBER) {
		uint32_t init;
		if (!cc_bit_cast_initializer_find(c, x->a, &init, depth + 1))
			return false;
		if (AST_TYPE_KIND(c, AST_NODE(x->a)->ty) == TY_UNION && x->c != (init ? AST_NODE(init)->a : 0))
			return false;
		return cc_bit_cast_initializer_find(c, init && x->c < AST_NODE(init)->c ? c->lists[AST_NODE(init)->b + x->c] : 0, out, depth + 1);
	}
	if (x->op == N_INDEX) {
		uint32_t base = cc_bit_cast_index_base(c, x), init;
		cval ix;
		if (!cc_type_is_array(c, AST_NODE(base)->ty) || !cc_constant_eval(c, x->b, &ix) || ix.kind != CV_INT || ix.i < 0 ||
		    (u128)ix.i >= cc_type_array_length(c, AST_NODE(base)->ty) || !cc_bit_cast_initializer_find(c, base, &init, depth + 1))
			return false;
		if (!init) {
			*out = 0;
			return true;
		}
		const node *a = AST_NODE(init);
		if (a->aux == 1) {
			for (uint32_t i = 0; i < a->c; i++) {
				const uint32_t *entry = &c->lists[a->b + i * 3];
				if ((u128)ix.i >= entry[0] && (u128)ix.i <= entry[1])
					return cc_bit_cast_initializer_find(c, entry[2], out, depth + 1);
			}
			*out = 0;
			return true;
		}
		return cc_bit_cast_initializer_find(c, (u128)ix.i < a->c ? c->lists[a->b + (uint32_t)ix.i] : 0, out, depth + 1);
	}
	return false;
}

static bool cc_bit_cast_active_member(cc *c, const node *x)
{
	if (AST_TYPE_KIND(c, AST_NODE(x->a)->ty) != TY_UNION)
		return true;
	uint32_t init;
	return cc_bit_cast_initializer_find(c, x->a, &init, 0) && x->c == (init ? AST_NODE(init)->a : 0);
}

/* Intermediate unsigned-byte objects may carry padding through a nested
 * bit_cast. A constexpr definition still requires every destination value bit
 * initialized; merely reading one known member does not read the padding. */
static bool cc_bit_cast_grammar_check(cc *c, uint32_t n, unsigned depth, bool strict)
{
	if (!n)
		return true;
	if (depth > 512 || vstack_guard_low())
		return false;
	const node *x = AST_NODE(n);
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
		if (AST_NODE(sym->body)->op == N_STR)
			return true;
		return cc_bit_cast_grammar_check(c, sym->body, depth + 1, strict);
	}
	if (x->op == N_COMPLIT)
		return AST_NODE(x->a)->op == N_STR || cc_bit_cast_grammar_check(c, x->a, depth + 1, strict);
	if (x->op == N_INIT) {
		for (uint32_t i = 0; i < x->c; i++) {
			if (x->aux == 2 && i != x->a)
				continue;
			uint32_t child = c->lists[x->b + (x->aux == 1 ? 3 * i + 2 : i)];
			if (child && AST_NODE(child)->op == N_STR)
				continue; /* initializer storage */
			if (!cc_bit_cast_grammar_check(c, child, depth + 1, strict))
				return false;
		}
		return true;
	}
	if (x->op == N_CAST) {
		if (x->aux == CK_BITS && (!cc_bit_cast_type_is_eligible(c, x->ty, 0) || !cc_bit_cast_type_is_eligible(c, AST_NODE(x->a)->ty, 0)))
			return false;
		return cc_bit_cast_grammar_check(c, x->a, depth + 1, strict);
	}
	if (x->op == N_LOGAND || x->op == N_LOGOR) {
		cval test;
		if (!cc_bit_cast_grammar_check(c, x->a, depth + 1, strict) || !cc_constant_eval(c, x->a, &test))
			return false;
		bool yes = test.kind == CV_INT	 ? test.i != 0
			   : test.kind == CV_FLT ? test.ld ? vsoftfloat_is_nonzero(test.q) : test.f != 0
						 : false;
		return (x->op == N_LOGAND ? !yes : yes) || cc_bit_cast_grammar_check(c, x->b, depth + 1, strict);
	}
	if (x->op == N_BINARY || x->op == N_CPLX)
		return cc_bit_cast_grammar_check(c, x->a, depth + 1, strict) && cc_bit_cast_grammar_check(c, x->b, depth + 1, strict);
	if (x->op == N_COND) {
		cval test;
		if (!cc_bit_cast_grammar_check(c, x->a, depth + 1, strict) || !cc_constant_eval(c, x->a, &test))
			return false;
		bool yes = test.kind == CV_INT	 ? test.i != 0
			   : test.kind == CV_FLT ? test.ld ? vsoftfloat_is_nonzero(test.q) : test.f != 0
						 : false;
		return cc_bit_cast_grammar_check(c, yes ? (x->b ? x->b : x->a) : x->c, depth + 1, strict);
	}
	if (x->op == N_MEMBER && !cc_bit_cast_active_member(c, x))
		return false;
	if (x->op == N_UNARY || x->op == N_MEMBER)
		return cc_bit_cast_grammar_check(c, x->a, depth + 1, strict);
	if (x->op == N_INDEX)
		return cc_bit_cast_grammar_check(c, cc_bit_cast_index_base(c, x), depth + 1, strict) && cc_bit_cast_grammar_check(c, x->b, depth + 1, strict);
	if (x->op == N_BUILTIN && (x->aux == BI_CONVERTVECTOR || x->aux == BI_SHUFFLEVECTOR)) {
		for (uint32_t i = 0; i < x->c; i++)
			if (!cc_bit_cast_grammar_check(c, c->lists[x->b + i], depth + 1, strict))
				return false;
		return true;
	}
	if (cc_type_is_vector(c, x->ty) && x->op != N_ZERO)
		return false;
	return cc_constexpr_initializer_is_ok(c, n);
}

static bool cc_bit_cast_source_is_ok(cc *c, uint32_t n, unsigned depth)
{
	return cc_bit_cast_grammar_check(c, n, depth, false);
}

/* Per-read validation memo: nested casts reuse the proof for each intermediate
 * representation. It is local to this invocation, never shared by emitters. */
typedef struct {
	uint32_t *valid;
	size_t capacity, used;
} cc_bit_cast_state;

static bool cc_bit_cast_memo_has(const cc_bit_cast_state *state, uint32_t n)
{
	if (!state->capacity)
		return false;
	size_t slot = ((uint64_t)n * 2654435761u) & (state->capacity - 1);
	while (state->valid[slot] && state->valid[slot] != n)
		slot = (slot + 1) & (state->capacity - 1);
	return state->valid[slot] == n;
}

static bool cc_bit_cast_memo_add(cc_bit_cast_state *state, uint32_t n)
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

static bool cc_bit_cast_read(cc_bit_cast_state *, cc *, uint32_t, qty, uint64_t, unsigned, u128 *, u128 *, unsigned);
static bool cc_bit_cast_check(cc_bit_cast_state *, cc *, uint32_t, qty, uint64_t, unsigned, bool);
static bool cc_bit_cast_lane_read(cc_bit_cast_state *, cc *, uint32_t, uint64_t, cval *, unsigned);

/* Put an overlapping part of a child object into the requested window. */
static bool cc_bit_cast_part_copy(cc_bit_cast_state *state, cc *c, uint32_t n, qty t, uint64_t pos, uint64_t size, uint64_t off, unsigned count,
		    u128 *v, u128 *known, unsigned depth)
{
	if (pos >= off + count || off >= pos + size)
		return true;
	uint64_t lo = pos > off ? pos : off, hi = pos + size < off + count ? pos + size : off + count;
	u128 a, b;
	if (!cc_bit_cast_read(state, c, n, t, lo - pos, (unsigned)(hi - lo), &a, &b, depth + 1))
		return false;
	unsigned shift = (unsigned)(lo - off);
	*v |= a << shift;
	*known |= b << shift;
	return true;
}

static bool cc_bit_cast_encode(cc *c, qty t, cval cv, u128 *v, unsigned *valid)
{
	*valid = (unsigned)(cc_type_size(c, t) * 8);
	if (cc_type_is_integer(c, t)) {
		if (cv.kind != CV_INT)
			return false;
		*valid = cc_type_integer_bits(c, t);
		/* _Bool occupies a byte; representations other than 0 and 1 are invalid. */
		if (AST_TYPE_KIND(c, t) == TY_BOOL)
			*valid = 8;
		*v = (u128)cv.i & cc_bit_cast_mask(*valid);
		return true;
	}
	if (!cc_type_is_float(c, t) || cv.kind != CV_FLT)
		return false;
	u128 q = cv.ld ? cv.q : vsoftfloat_of_double(cv.f);
	if (cv.ld && vsoftfloat_is_nan(q) && *valid <= 64) {
		*v = vsoftfloat_nan_storage(q,
				   AST_TYPE_KIND(c, t) == TY_BFLOAT16 ? 7
				   : *valid == 16	     ? 10
				   : *valid == 32	     ? 23
							     : 52,
				   AST_TYPE_KIND(c, t) == TY_BFLOAT16 ? 8
				   : *valid == 16	     ? 5
				   : *valid == 32	     ? 8
							     : 11);
		return true;
	}
	if (AST_TYPE_KIND(c, t) == TY_FLOAT16)
		*v = vsoftfloat_to_half(q);
	else if (AST_TYPE_KIND(c, t) == TY_BFLOAT16)
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
	} else if (c->o->target == PP_LINUX_X86_64 && AST_TYPE_KIND(c, t) == TY_LDOUBLE) {
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

static bool cc_bit_cast_scalar_read(cc_bit_cast_state *state, cc *c, uint32_t n, qty t, u128 *v, unsigned *valid)
{
	cval cv = {0};
	if (n && !cc_constant_eval(c, n, &cv) && !cc_bit_cast_lane_read(state, c, n, 0, &cv, 0))
		return false;
	if (!n)
		cv.kind = cc_type_is_float(c, t) ? CV_FLT : CV_INT;
	return cc_bit_cast_encode(c, t, cv, v, valid);
}

static bool cc_bit_cast_read(cc_bit_cast_state *state, cc *c, uint32_t n, qty t, uint64_t off, unsigned count, u128 *v, u128 *known,
		    unsigned depth)
{
	*v = *known = 0;
	if (depth > 512 || vstack_guard_low() || count > 128 || off > cc_type_size(c, t) * 8 || count > cc_type_size(c, t) * 8 - off)
		return false;
	const node *x = n ? AST_NODE(n) : NULL;
	if (x && x->op == N_CAST && x->aux == CK_BITS) {
		if (!cc_bit_cast_type_is_eligible(c, x->ty, 0) || !cc_bit_cast_type_is_eligible(c, AST_NODE(x->a)->ty, 0) || !cc_bit_cast_source_is_ok(c, x->a, depth + 1))
			return false;
		/* A byte may carry indeterminate padding, but an invalid bool or an
		 * indeterminate non-byte scalar cannot be hidden by another cast. */
		if (!cc_bit_cast_memo_has(state, n)) {
			if (!cc_bit_cast_check(state, c, x->a, x->ty, 0, depth + 1, true) || !cc_bit_cast_memo_add(state, n))
				return false;
		}
		if (!cc_bit_cast_read(state, c, x->a, AST_NODE(x->a)->ty, off, count, v, known, depth + 1))
			return false;
		u128 ignored, value_bits;
		if (!cc_bit_cast_read(state, c, 0, x->ty, off, count, &ignored, &value_bits, depth + 1))
			return false;
		*known &= value_bits;
		*v &= value_bits;
		return true;
	}
	if (x && x->op == N_CAST && (x->aux == CK_NOP || x->aux == CK_DECAY))
		return cc_bit_cast_read(state, c, x->a, AST_NODE(x->a)->ty, off, count, v, known, depth + 1);
	if (x && x->op == N_VAR) {
		const sym *s = &c->syms[x->a];
		if (((s->flags & SF_CONSTEXPR) == 0 && (QQ(s->ty) & (Q_CONST | Q_VOLATILE)) != Q_CONST) || !s->body ||
		    (s->flags & SF_INVALID))
			return false;
		/* The depth bound also bounds mutually recursive invalid initializers. */
		return cc_bit_cast_read(state, c, s->body, s->ty, off, count, v, known, depth + 1);
	}
	if (x && x->op == N_COMPLIT)
		return cc_bit_cast_read(state, c, x->a, t, off, count, v, known, depth + 1);
	if (x && x->op == N_MEMBER) {
		if (!cc_bit_cast_active_member(c, x))
			return false;
		const field *f = cc_node_member_field(c, x);
		if (!f->bitfield)
			return cc_bit_cast_read(state, c, x->a, AST_NODE(x->a)->ty, f->off + off, count, v, known, depth + 1);
		unsigned take = off >= f->width ? 0 : count < f->width - off ? count : (unsigned)(f->width - off);
		if (take && !cc_bit_cast_read(state, c, x->a, AST_NODE(x->a)->ty, f->off + off, take, v, known, depth + 1))
			return false;
		if (take < count) {
			u128 high = cc_bit_cast_mask(count) & ~cc_bit_cast_mask(take);
			if (!cc_type_is_signed(c, f->ty))
				*known |= high;
			else {
				u128 sign, sk;
				if (!cc_bit_cast_read(state, c, x->a, AST_NODE(x->a)->ty, f->off + f->width - 1, 1, &sign, &sk, depth + 1))
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
		uint32_t base = cc_bit_cast_index_base(c, x);
		qty bt = AST_NODE(base)->ty;
		if ((!cc_type_is_array(c, bt) && !cc_type_is_vector(c, bt)) || !cc_constant_eval(c, x->b, &ix) || ix.kind != CV_INT || ix.i < 0)
			return false;
		uint64_t num = cc_type_is_array(c, bt) ? cc_type_array_length(c, bt) : AST_TYPE(c, bt)->ext ? AST_TYPE(c, bt)->ext : AST_TYPE(c, bt)->n;
		if ((u128)ix.i >= num)
			return false;
		bool packed = cc_type_is_vector(c, bt) && AST_TYPE_KIND(c, AST_TYPE(c, bt)->base) == TY_BOOL;
		if (packed) {
			if (off) {
				*known = cc_bit_cast_mask(count);
				return true;
			}
			if (!cc_bit_cast_read(state, c, base, bt, (uint64_t)ix.i, 1, v, known, depth + 1))
				return false;
			*known |= cc_bit_cast_mask(count) & ~(u128)1;
			return true;
		}
		return cc_bit_cast_read(state, c, base, bt, (uint64_t)ix.i * cc_type_size(c, t) * 8 + off, count, v, known, depth + 1);
	}
	if (x && x->op == N_STR) {
		const cstr *s = &c->strs[x->a];
		for (unsigned i = 0; i < count; i++) {
			uint64_t b = off + i;
			if (b / 8 < (uint64_t)s->n * s->esz)
				*v |= (u128)(((unsigned char)s->s[b / 8] >> (b % 8)) & 1) << i;
		}
		*known = cc_bit_cast_mask(count);
		return true;
	}
	if (x && x->op == N_ZERO)
		n = 0, x = NULL;
	int k = AST_TYPE_KIND(c, t);
	if (k == TY_ARRAY || k == TY_VECTOR) {
		qty et = AST_TYPE(c, t)->base;
		uint64_t es = cc_type_size(c, et) * 8;
		bool packed = k == TY_VECTOR && AST_TYPE_KIND(c, et) == TY_BOOL;
		if (packed)
			es = 1;
		if (!es)
			return true;
		uint64_t num = k == TY_ARRAY ? cc_type_array_length(c, t) : AST_TYPE(c, t)->ext ? AST_TYPE(c, t)->ext : AST_TYPE(c, t)->n;
		if (x && x->op == N_CAST && x->aux == CK_SPLAT) {
			for (uint64_t i = off / es; i < (off + count + es - 1) / es && i < num; i++)
				if (!cc_bit_cast_part_copy(state, c, x->a, et, i * es, es, off, count, v, known, depth))
					return false;
			return true;
		}
		if (x && x->op != N_INIT) {
			for (uint64_t i = off / es; i < (off + count + es - 1) / es && i < num; i++) {
				cval lane;
				u128 raw;
				unsigned valid;
				if (!cc_bit_cast_lane_read(state, c, n, i, &lane, depth + 1) || !cc_bit_cast_encode(c, et, lane, &raw, &valid))
					return false;
				uint64_t lo = i * es > off ? i * es : off,
					 hi = (i + 1) * es < off + count ? (i + 1) * es : off + count;
				unsigned shift = (unsigned)(lo - off), skip = (unsigned)(lo - i * es),
					 take = (unsigned)(hi - lo);
				*v |= ((raw >> skip) & cc_bit_cast_mask(take)) << shift;
				*known |= ((cc_bit_cast_mask(valid) >> skip) & cc_bit_cast_mask(take)) << shift;
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
			if (!cc_bit_cast_part_copy(state, c, child, et, i * es, es, off, count, v, known, depth))
				return false;
		}
		return true;
	}
	if (k == TY_STRUCT || k == TY_UNION) {
		if (x && x->op != N_INIT && x->op != N_CPLX)
			return false;
		const tag *g = &c->tags[AST_TYPE(c, t)->aux];
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
			if (!cc_bit_cast_part_copy(state, c, child, f->ty, f->off, f->bitfield ? f->width : cc_type_size(c, f->ty) * 8, off, count,
				     v, known, depth))
				return false;
		}
		return true;
	}
	u128 bits;
	unsigned valid;
	if (!cc_bit_cast_scalar_read(state, c, n, t, &bits, &valid))
		return false;
	if (off < 128) {
		*v = (bits >> off) & cc_bit_cast_mask(count);
		*known = (cc_bit_cast_mask(valid) >> off) & cc_bit_cast_mask(count);
	}
	return true;
}

static bool cc_bit_cast_bytes_read(cc_bit_cast_state *state, cc *c, uint32_t n, uint64_t off, unsigned len, unsigned char *data,
			  unsigned char *known)
{
	u128 v, k;
	if (len > 16 || !cc_bit_cast_read(state, c, n, AST_NODE(n)->ty, off * 8, len * 8, &v, &k, 0))
		return false;
	for (unsigned i = 0; i < len; i++)
		data[i] = (unsigned char)(v >> (8 * i)), known[i] = (unsigned char)(k >> (8 * i));
	return true;
}

static bool cc_bit_cast_decode(cc *c, qty t, u128 raw, cval *v)
{
	unsigned bits = (unsigned)(cc_type_size(c, t) * 8), valid = cc_type_is_integer(c, t) ? cc_type_integer_bits(c, t) : bits;
	if (c->o->target == PP_LINUX_X86_64 && AST_TYPE_KIND(c, t) == TY_LDOUBLE)
		valid = 80;
	memset(v, 0, sizeof(*v));
	v->ice = true;
	if (cc_type_is_integer(c, t)) {
		if (AST_TYPE_KIND(c, t) == TY_BOOL && raw > 1)
			return false;
		v->kind = CV_INT;
		v->i = cc_constant_truncate(c, (i128)raw, t);
		return true;
	}
	v->kind = CV_FLT;
	if (bits <= 64) {
		unsigned p = AST_TYPE_KIND(c, t) == TY_BFLOAT16 ? 7 : bits == 16 ? 10 : bits == 32 ? 23 : 52;
		unsigned eb = AST_TYPE_KIND(c, t) == TY_BFLOAT16 ? 8 : bits == 16 ? 5 : bits == 32 ? 8 : 11;
		u128 frac = raw & cc_bit_cast_mask(p);
		if (((raw >> p) & cc_bit_cast_mask(eb)) == cc_bit_cast_mask(eb) && frac) {
			v->ld = true;
			v->q = ((raw >> (p + eb)) & 1) << 127 | (u128)0x7fff << 112 | frac << (112 - p);
			v->f = vsoftfloat_to_double(v->q);
			return true;
		}
	}
	if (AST_TYPE_KIND(c, t) == TY_FLOAT16)
		v->f = h_to_d((uint16_t)raw);
	else if (AST_TYPE_KIND(c, t) == TY_BFLOAT16)
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

static bool cc_bit_cast_value_read(cc_bit_cast_state *state, cc *c, uint32_t n, cval *v)
{
	/* This fallback must not turn ordinary const aggregate reads into ICEs.
	 * Explicit bit_cast operands have their own relaxed-fold/strict grammar. */
	if (AST_NODE(n)->op == N_MEMBER || AST_NODE(n)->op == N_INDEX) {
		uint32_t root = n;
		for (unsigned depth = 0; depth < 512; depth++) {
			const node *x = AST_NODE(root);
			if (x->op == N_MEMBER)
				root = x->a;
			else if (x->op == N_INDEX)
				root = cc_bit_cast_index_base(c, x);
			else if (x->op == N_CAST && (x->aux == CK_NOP || x->aux == CK_DECAY))
				root = x->a;
			else {
				if (x->op == N_VAR && !(c->syms[x->a].flags & SF_CONSTEXPR))
					return false;
				break;
			}
		}
	}
	qty t = AST_NODE(n)->ty;
	if (!cc_type_is_integer(c, t) && !cc_type_is_float(c, t))
		return false;
	unsigned bits = (unsigned)(cc_type_size(c, t) * 8), valid = cc_type_is_integer(c, t) ? cc_type_integer_bits(c, t) : bits;
	if (bits > 128)
		return false;
	if (AST_TYPE_KIND(c, t) == TY_BOOL)
		valid = 8;
	if (c->o->target == PP_LINUX_X86_64 && AST_TYPE_KIND(c, t) == TY_LDOUBLE)
		valid = 80;
	u128 raw, known;
	if (!cc_bit_cast_read(state, c, n, t, 0, bits, &raw, &known, 0) || (known & cc_bit_cast_mask(valid)) != cc_bit_cast_mask(valid))
		return false;
	return cc_bit_cast_decode(c, t, raw, v);
}

static bool cc_bit_cast_check(cc_bit_cast_state *state, cc *c, uint32_t n, qty t, uint64_t off, unsigned depth, bool carry)
{
	if (depth > 128)
		return false;
	int k = AST_TYPE_KIND(c, t);
	if (k == TY_ARRAY || k == TY_VECTOR) {
		qty et = AST_TYPE(c, t)->base;
		uint64_t sz = cc_type_size(c, et) * 8;
		if (k == TY_VECTOR && AST_TYPE_KIND(c, et) == TY_BOOL)
			sz = 1;
		uint64_t num = k == TY_ARRAY ? cc_type_array_length(c, t) : AST_TYPE(c, t)->ext ? AST_TYPE(c, t)->ext : AST_TYPE(c, t)->n;
		for (uint64_t i = 0; i < num; i++) {
			if (k == TY_VECTOR && AST_TYPE_KIND(c, et) == TY_BOOL) {
				u128 raw, known;
				if (!cc_bit_cast_read(state, c, n, AST_NODE(n)->ty, off + i, 1, &raw, &known, depth + 1) || known != 1)
					return false;
			} else if (!cc_bit_cast_check(state, c, n, et, off + i * sz, depth + 1, carry))
				return false;
		}
		return true;
	}
	if (k == TY_STRUCT) {
		const tag *g = &c->tags[AST_TYPE(c, t)->aux];
		for (uint32_t i = 0; i < g->nfield; i++) {
			const field *f = &g->f[i];
			if (f->bitfield && !f->name)
				continue;
			u128 v, known;
			if (f->bitfield) {
				if (!cc_bit_cast_read(state, c, n, AST_NODE(n)->ty, off + f->off, f->width, &v, &known, 0) ||
				    known != cc_bit_cast_mask(f->width))
					return false;
			} else if (!cc_bit_cast_check(state, c, n, f->ty, off + f->off, depth + 1, carry))
				return false;
		}
		return true;
	}
	unsigned bits = (unsigned)cc_type_size(c, t) * 8;
	if (cc_type_is_integer(c, t) && AST_TYPE_KIND(c, t) != TY_BOOL)
		bits = cc_type_integer_bits(c, t);
	if (c->o->target == PP_LINUX_X86_64 && k == TY_LDOUBLE)
		bits = 80;
	u128 v, known;
	if (bits > 128 || !cc_bit_cast_read(state, c, n, AST_NODE(n)->ty, off, bits, &v, &known, 0))
		return false;
	if (known != cc_bit_cast_mask(bits))
		return carry && (k == TY_UCHAR || (k == TY_CHAR && !cc_type_is_signed(c, t)));
	if (c->o->target == PP_LINUX_X86_64 && k == TY_LDOUBLE) {
		unsigned exp = (unsigned)(v >> 64) & 0x7fff;
		if ((exp != 0) != (((uint64_t)v >> 63) != 0))
			return false;
	}
	return k != TY_BOOL || v <= 1;
}

static bool cc_bit_cast_constant_read(cc_bit_cast_state *state, cc *c, uint32_t n)
{
	/* Restrict this fallback to a view rooted in a bit_cast; ordinary constexpr
	 * expressions continue through their existing language-specific checks. */
	uint32_t root = n;
	for (unsigned d = 0; d < 512; d++) {
		const node *x = AST_NODE(root);
		if (x->op == N_CAST && x->aux == CK_BITS)
			return cc_bit_cast_type_is_eligible(c, x->ty, 0) && cc_bit_cast_type_is_eligible(c, AST_NODE(x->a)->ty, 0) && cc_bit_cast_grammar_check(c, x->a, 0, true) &&
			       cc_bit_cast_check(state, c, n, AST_NODE(n)->ty, 0, 0, false);
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
static u128 cc_bit_cast_float128_bits(const cval *v, bool sign)
{
	return v->kind == CV_FLT ? v->ld ? v->q : vsoftfloat_of_double(v->f) : vsoftfloat_of_integer(v->i, sign);
}

static bool cc_bit_cast_value_convert(cc *c, cval *v, qty from, qty to)
{
	if (QUNQ(from) == QUNQ(to))
		return true;
	if (cc_type_is_integer(c, to)) {
		i128 i = v->kind == CV_INT ? v->i : vsoftfloat_to_integer_saturating(cc_bit_cast_float128_bits(v, true), (int)cc_type_integer_bits(c, to), cc_type_is_signed(c, to));
		if (AST_TYPE_KIND(c, to) == TY_BOOL)
			i = v->kind == CV_INT ? v->i != 0 : vsoftfloat_is_nonzero(cc_bit_cast_float128_bits(v, true));
		memset(v, 0, sizeof(*v));
		v->kind = CV_INT;
		v->i = cc_constant_truncate(c, i, to);
		return true;
	}
	if (!cc_type_is_float(c, to))
		return false;
	u128 q = cc_bit_cast_float128_bits(v, cc_type_is_signed(c, from));
	memset(v, 0, sizeof(*v));
	v->kind = CV_FLT;
	if (AST_TYPE_KIND(c, to) == TY_FLOAT16)
		v->f = h_to_d(vsoftfloat_to_half(q));
	else if (AST_TYPE_KIND(c, to) == TY_BFLOAT16)
		v->f = bf_to_d(vsoftfloat_to_bfloat16(q));
	else if (AST_TYPE_KIND(c, to) == TY_FLOAT)
		v->f = vsoftfloat_to_float(q);
	else if (AST_TYPE_KIND(c, to) == TY_DOUBLE)
		v->f = vsoftfloat_to_double(q);
	else {
		v->ld = true;
		v->q = vsoftfloat_to_precision(q, cc_float_precision_bits(c, to));
		v->f = vsoftfloat_to_double(v->q);
	}
	return true;
}

static bool cc_bit_cast_lane_read(cc_bit_cast_state *state, cc *c, uint32_t n, uint64_t lane, cval *v, unsigned depth)
{
	if (depth > 512 || vstack_guard_low())
		return false;
	const node *x = AST_NODE(n);
	qty t = x->ty;
	bool vector = cc_type_is_vector(c, t);
	if (!vector && cc_constant_eval(c, n, v))
		return true;
	qty et = vector ? AST_TYPE(c, t)->base : t;
	if (x->op == N_VAR) {
		const sym *s = &c->syms[x->a];
		if (!s->body || (s->flags & SF_INVALID))
			return false;
		return cc_bit_cast_lane_read(state, c, s->body, lane, v, depth + 1);
	}
	if (x->op == N_CAST && x->aux == CK_BITS)
		return false; /* failed representation evaluation above */
	if (x->op == N_COMPLIT || (x->op == N_CAST && x->aux == CK_NOP))
		return cc_bit_cast_lane_read(state, c, x->a, lane, v, depth + 1);
	if (x->op == N_CAST && (x->aux == CK_SPLAT || !vector))
		return cc_bit_cast_lane_read(state, c, x->a, lane, v, depth + 1) && cc_bit_cast_value_convert(c, v, AST_NODE(x->a)->ty, et);
	if (x->op == N_BUILTIN && (x->aux == BI_CONVERTVECTOR || x->aux == BI_SHUFFLEVECTOR)) {
		const uint32_t *args = &c->lists[x->b];
		uint32_t source = args[0];
		uint64_t index = lane;
		if (x->aux == BI_SHUFFLEVECTOR) {
			cval ix;
			if (lane + 2 >= x->c || !cc_constant_eval(c, args[lane + 2], &ix) || ix.kind != CV_INT || ix.i < 0)
				return false;
			uint64_t count = AST_TYPE(c, AST_NODE(source)->ty)->ext ? AST_TYPE(c, AST_NODE(source)->ty)->ext : AST_TYPE(c, AST_NODE(source)->ty)->n;
			index = (uint64_t)ix.i;
			if (index >= count)
				source = args[1], index -= count;
		}
		if (!cc_bit_cast_lane_read(state, c, source, index, v, depth + 1))
			return false;
		return x->aux == BI_SHUFFLEVECTOR || cc_bit_cast_value_convert(c, v, AST_TYPE(c, AST_NODE(source)->ty)->base, et);
	}
	if (x->op == N_COND) {
		cval test;
		if (!cc_bit_cast_lane_read(state, c, x->a, lane, &test, depth + 1))
			return false;
		bool yes = test.kind == CV_INT	 ? test.i != 0
			   : test.kind == CV_FLT ? test.ld ? vsoftfloat_is_nonzero(test.q) : test.f != 0
						 : false;
		return cc_bit_cast_lane_read(state, c, yes ? (x->b ? x->b : x->a) : x->c, lane, v, depth + 1);
	}
	if (x->op == N_UNARY) {
		if (!cc_bit_cast_lane_read(state, c, x->a, lane, v, depth + 1))
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
				v->i = AST_TYPE_KIND(c, et) == TY_BOOL ? !v->i : ~v->i;
			else if (x->aux == LEX_TOKEN_EXCLAMATION)
				v->i = (vector ? -1 : 1) * (v->i == 0);
			else if (x->aux != LEX_TOKEN_PLUS)
				return false;
			v->i = cc_constant_truncate(c, v->i, et);
		}
		return true;
	}
	if (x->op == N_BINARY) {
		cval a, b;
		if (!cc_bit_cast_lane_read(state, c, x->a, lane, &a, depth + 1) || !cc_bit_cast_lane_read(state, c, x->b, lane, &b, depth + 1))
			return false;
		int op = x->aux;
		bool compare = op == LEX_TOKEN_LESS || op == LEX_TOKEN_LESS_EQUAL || op == LEX_TOKEN_GREATER || op == LEX_TOKEN_GREATER_EQUAL || op == LEX_TOKEN_EQUAL_EQUAL ||
			       op == LEX_TOKEN_EXCLAMATION_EQUAL;
		qty at = cc_type_is_vector(c, AST_NODE(x->a)->ty) ? AST_TYPE(c, AST_NODE(x->a)->ty)->base : AST_NODE(x->a)->ty;
		memset(v, 0, sizeof(*v));
		if (a.kind == CV_FLT || b.kind == CV_FLT) {
			u128 l = cc_bit_cast_float128_bits(&a, cc_type_is_signed(c, at)), r = cc_bit_cast_float128_bits(&b, cc_type_is_signed(c, at)), q;
			int cmp = vsoftfloat_compare(l, r), p = cc_float_precision_bits(c, at);
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
			return cc_bit_cast_value_convert(c, v, at, et);
		}
		u128 l = (u128)a.i, r = (u128)b.i, result = 0;
		bool sg = cc_type_is_signed(c, at);
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
			if (b.i < 0 || r >= cc_type_integer_bits(c, at))
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
		v->i = cc_constant_truncate(c, (i128)result, et);
		return true;
	}
	if (!vector)
		return false;
	unsigned stride = AST_TYPE_KIND(c, et) == TY_BOOL ? 1 : (unsigned)cc_type_size(c, et) * 8;
	u128 raw, known;
	if (!cc_bit_cast_read(state, c, n, t, lane * stride, stride, &raw, &known, depth + 1) || known != cc_bit_cast_mask(stride))
		return false;
	return cc_bit_cast_decode(c, et, raw, v);
}

/* Vector initializer validity and indexed constants use the same object reader
 * as bit_cast. This keeps packed boolean lanes as bits, including expressions
 * formed by splats and lane operations rather than initializer lists. */
static bool cc_bit_cast_vector_constant_read(cc_bit_cast_state *state, cc *c, uint32_t n)
{
	if (!n)
		return false;
	const node *x = AST_NODE(n);
	if (!cc_type_is_vector(c, x->ty) && !(x->op == N_INDEX && cc_type_is_vector(c, AST_NODE(cc_bit_cast_index_base(c, x))->ty)))
		return false;
	return cc_bit_cast_grammar_check(c, n, 0, true) && cc_bit_cast_check(state, c, n, x->ty, 0, 0, false);
}

CC_PRIVATE bool cc_bit_cast_value(cc *c, uint32_t n, cval *v)
{
	cc_bit_cast_state state = {0};
	bool ok = cc_bit_cast_value_read(&state, c, n, v);
	free(state.valid);
	return ok;
}
CC_PRIVATE bool cc_bit_cast_is_constant(cc *c, uint32_t n)
{
	cc_bit_cast_state state = {0};
	bool ok = cc_bit_cast_constant_read(&state, c, n);
	free(state.valid);
	return ok;
}
CC_PRIVATE bool cc_bit_cast_vector_is_constant(cc *c, uint32_t n)
{
	cc_bit_cast_state state = {0};
	bool ok = cc_bit_cast_vector_constant_read(&state, c, n);
	free(state.valid);
	return ok;
}
bool cc_bitcast_read(cc *c, uint32_t n, uint64_t off, unsigned len, unsigned char *data, unsigned char *known)
{
	cc_bit_cast_state state = {0};
	bool ok = cc_bit_cast_bytes_read(&state, c, n, off, len, data, known);
	free(state.valid);
	return ok;
}
