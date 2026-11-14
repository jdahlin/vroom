/* vcc front end: types: interning, predicates, compatibility (C11 6.2.7) */
#include "cc-internal.h"

const cc_basic_type_layout cc_basic_types[AST_TYPE_POINTER] = {
	[AST_TYPE_VOID] = { 1, 1 }, [AST_TYPE_BOOL] = { 1, 1 },
	[AST_TYPE_CHAR] = { 1, 1 }, [AST_TYPE_SIGNED_CHAR] = { 1, 1 }, [AST_TYPE_UNSIGNED_CHAR] = { 1, 1 },
	[AST_TYPE_SHORT] = { 2, 2 }, [AST_TYPE_UNSIGNED_SHORT] = { 2, 2 },
	[AST_TYPE_INT] = { 4, 4 }, [AST_TYPE_UNSIGNED_INT] = { 4, 4 },
	[AST_TYPE_LONG] = { 8, 8 }, [AST_TYPE_UNSIGNED_LONG] = { 8, 8 },
	[AST_TYPE_LONG_LONG] = { 8, 8 }, [AST_TYPE_UNSIGNED_LONG_LONG] = { 8, 8 },
	[AST_TYPE_INT128] = { 16, 16 }, [AST_TYPE_UINT128] = { 16, 16 },
	[AST_TYPE_BFLOAT16] = { 2, 2 }, [AST_TYPE_FLOAT16] = { 2, 2 }, [AST_TYPE_FLOAT] = { 4, 4 }, [AST_TYPE_DOUBLE] = { 8, 8 },
	[AST_TYPE_LONG_DOUBLE] = { 16, 16 }, [AST_TYPE_FLOAT128] = { 16, 16 },
};
CC_PRIVATE bool cc_type_is_bitint(cc *c, ast_qualified_type t) { int k = AST_TYPE_KIND(c, t); return k == AST_TYPE_BITINT || k == AST_TYPE_UNSIGNED_BITINT; }
CC_PRIVATE bool cc_type_is_integer(cc *c, ast_qualified_type t) { int k = AST_TYPE_KIND(c, t); return cc_type_kind_is_integer(k) || k == AST_TYPE_ENUM; }
CC_PRIVATE bool cc_type_is_float(cc *c, ast_qualified_type t) { return cc_type_kind_is_float(AST_TYPE_KIND(c, t)); }
CC_PRIVATE bool cc_type_is_arithmetic(cc *c, ast_qualified_type t) { return cc_type_is_integer(c, t) || cc_type_is_float(c, t); }
CC_PRIVATE bool cc_type_is_pointer(cc *c, ast_qualified_type t) { return AST_TYPE_KIND(c, t) == AST_TYPE_POINTER; }
CC_PRIVATE bool cc_type_is_scalar(cc *c, ast_qualified_type t) { return cc_type_is_arithmetic(c, t) || cc_type_is_pointer(c, t); }
CC_PRIVATE bool cc_type_is_void(cc *c, ast_qualified_type t) { return AST_TYPE_KIND(c, t) == AST_TYPE_VOID; }
CC_PRIVATE bool cc_type_is_array(cc *c, ast_qualified_type t) { return AST_TYPE_KIND(c, t) == AST_TYPE_ARRAY; }
CC_PRIVATE bool cc_type_is_function(cc *c, ast_qualified_type t) { return AST_TYPE_KIND(c, t) == AST_TYPE_FUNCTION; }
CC_PRIVATE bool cc_type_is_record(cc *c, ast_qualified_type t) { int k = AST_TYPE_KIND(c, t); return k == AST_TYPE_STRUCT || k == AST_TYPE_UNION; }
CC_PRIVATE bool cc_type_is_vector(cc *c, ast_qualified_type t) { return AST_TYPE_KIND(c, t) == AST_TYPE_VECTOR; }
CC_PRIVATE bool cc_type_is_bool_vector(cc *c, ast_qualified_type t) { return cc_type_is_vector(c, t) && AST_TYPE_KIND(c, AST_TYPE(c, t)->base) == AST_TYPE_BOOL; }
/* initialized by a list of elements */
CC_PRIVATE bool cc_type_is_complex(cc *c, ast_qualified_type t) { return AST_TYPE_KIND(c, t) == AST_TYPE_STRUCT && (c->tags[AST_TYPE(c, t)->aux].flags & AST_RECORD_FLAG_COMPLEX); }
CC_PRIVATE bool cc_type_is_initializer_aggregate(cc *c, ast_qualified_type t) { int k = AST_TYPE_KIND(c, t); return (k == AST_TYPE_STRUCT && !cc_type_is_complex(c, t)) || k == AST_TYPE_UNION || k == AST_TYPE_ARRAY || k == AST_TYPE_VECTOR; }

CC_PRIVATE ast_qualified_type canon(cc *c, ast_qualified_type t) { return AST_QUALIFIED_TYPE(AST_TYPE(c, t)->canon, AST_QUALIFIED_TYPE_QUALIFIERS(t)); }

CC_PRIVATE bool cc_type_is_signed(cc *c, ast_qualified_type t)
{
	int k = AST_TYPE_KIND(c, t);

	if (k == AST_TYPE_ENUM)
		return cc_type_is_signed(c, AST_TYPE(c, t)->base);
	if (k == AST_TYPE_CHAR)
		return !c->o->unsigned_char;
	return k == AST_TYPE_SIGNED_CHAR || k == AST_TYPE_SHORT || k == AST_TYPE_INT || k == AST_TYPE_LONG || k == AST_TYPE_LONG_LONG || k == AST_TYPE_INT128 || k == AST_TYPE_BITINT;
}

CC_PRIVATE uint32_t cc_type_hash(const ctype *t)
{
	uint64_t h = (uint64_t)t->kind * 0x9E3779B97F4A7C15ull;

	h ^= t->base + 0x632BE59BD9B4E019ull + (h << 6) + (h >> 2);
	h ^= t->n + 0x8CB92BA72F3D8DD7ull + (h << 6) + (h >> 2);
	h ^= t->aux + (uint64_t)t->flags * 31 + t->align * 131 + (uint64_t)t->ext * 7919 + (h << 6) + (h >> 2);
	return (uint32_t)(h ^ (h >> 32));
}

static bool cc_type_equal(const ctype *a, const ctype *b)
{
	return a->kind == b->kind && a->flags == b->flags && a->base == b->base && a->n == b->n &&
	       a->aux == b->aux && a->align == b->align && a->size == b->size && a->ext == b->ext;
}

/* intern a type (records and enums are created with cc_type_new) */
CC_PRIVATE uint32_t cc_type_intern(cc *c, const ctype *t)
{
	uint32_t h = cc_type_hash(t), m = c->tyhcap - 1;

	for (uint32_t i = h & m;; i = (i + 1) & m) {
		uint32_t e = c->tyhash[i];

		if (!e) {
			uint32_t idx;

			if (unlikely(c->dr_on))
				return cc_replay_type_intern(c, t);	/* (a region's own table: first use of t in it) */
			idx = c->nty++;

			c->ty[idx] = *t;
			if (!(t->flags & AST_TYPE_FLAG_ALIGNED))
				c->ty[idx].canon = idx;
			c->tyhash[i] = idx;
			if (c->nty * 2 > c->tyhcap) {	/* grow */
				uint32_t ncap = c->tyhcap * 2, *nh = cc_heap_allocate(c, ncap * sizeof(uint32_t));

				memset(nh, 0, ncap * sizeof(uint32_t));
				for (uint32_t j = 0; j < c->tyhcap; j++) {
					uint32_t x = c->tyhash[j], k;

					if (!x)
						continue;
					for (k = cc_type_hash(&c->ty[x]) & (ncap - 1); nh[k]; k = (k + 1) & (ncap - 1))
						;
					nh[k] = x;
				}
				c->tyhash = nh;
				c->tyhcap = ncap;
			}
			return idx;
		}
		if (cc_type_equal(&c->ty[e], t)) {
			return e;
		}
	}
}

CC_PRIVATE uint32_t cc_type_new(cc *c, int kind)
{
	uint32_t idx = c->nty++;

	memset(&c->ty[idx], 0, sizeof(ctype));
	c->ty[idx].kind = (uint8_t)kind;
	c->ty[idx].canon = idx;
	if (unlikely(c->dr_on))
		cc_replay_type_new_log(c, idx);	/* (frontend/c/replay.c: the type log) */
	return idx;
}

/* Bit-precise integers retain a storage kind for scalar lowering, but their
   precision makes them distinct from all standard integer types. */
CC_PRIVATE ast_qualified_type cc_type_bitint(cc *c, unsigned bits, bool unsign)
{
	if (bits > 128) {
		ctype t = { .kind = unsign ? AST_TYPE_UNSIGNED_BITINT : AST_TYPE_BITINT, .size = 8 * (((uint64_t)bits + 63) / 64), .align = 8, .n = bits };
		return AST_QUALIFIED_TYPE(cc_type_intern(c, &t), 0);
	}
	unsigned size = bits <= 8 ? 1 : bits <= 16 ? 2 : bits <= 32 ? 4 : bits <= 64 ? 8 : 16;
	int kind = size == 1 ? AST_TYPE_SIGNED_CHAR : size == 2 ? AST_TYPE_SHORT : size == 4 ? AST_TYPE_INT : size == 8 ? AST_TYPE_LONG_LONG : AST_TYPE_INT128;
	ctype t = { .kind = (uint8_t)(kind + unsign), .size = size, .align = size > 8 && (CC_X86_64(c) || PP_IS_WASM(c->o->target)) ? 8 : size, .n = bits };

	return AST_QUALIFIED_TYPE(cc_type_intern(c, &t), 0);
}

CC_PRIVATE unsigned cc_type_integer_bits(cc *c, ast_qualified_type t)
{
	if (AST_TYPE_KIND(c, t) == AST_TYPE_ENUM)
		return cc_type_integer_bits(c, AST_TYPE(c, t)->base);
	if (cc_type_kind_is_integer(AST_TYPE_KIND(c, t)) && !(AST_TYPE(c, t)->flags & AST_TYPE_FLAG_ATOMIC) && AST_TYPE(c, t)->n)
		return AST_TYPE(c, t)->n;
	return (unsigned)cc_type_size(c, t) * 8;
}

CC_PRIVATE ast_qualified_type cc_type_pointer(cc *c, ast_qualified_type base)
{
	uint32_t i = c->ptrc[base];

	/* the cache may be stale (a restored snapshot): the entry must still be the pointer */
	if (likely(i >= c->ptr_lo && i < c->nty)) {	/* (ptr_lo: 1, or a recorded region's first type) */
		const ctype *p = &c->ty[i];

		if (p->kind == AST_TYPE_POINTER && !p->flags && p->base == base && p->align == CC_PTRSIZE(c))
			return AST_QUALIFIED_TYPE(i, 0);
	}
	{
		ctype t = { .kind = AST_TYPE_POINTER, .size = CC_PTRSIZE(c), .align = CC_PTRSIZE(c), .base = base };

		i = cc_type_intern(c, &t);
		c->ptrc[base] = i;
		return AST_QUALIFIED_TYPE(i, 0);
	}
}

CC_PRIVATE ast_qualified_type cc_type_array(cc *c, ast_qualified_type elem, uint64_t n, int flags)
{
	const ctype *e = AST_TYPE(c, elem);
	ctype t = { .kind = AST_TYPE_ARRAY, .flags = (uint8_t)flags, .base = elem, .align = e->align };

	if (!(flags & (AST_TYPE_FLAG_INCOMPLETE | AST_TYPE_FLAG_VLA))) {
		t.n = (uint32_t)n;
		t.aux = (uint32_t)(n >> 32);
		t.size = e->size * n;
	}
	return AST_QUALIFIED_TYPE(cc_type_intern(c, &t), 0);
}

CC_PRIVATE uint64_t cc_type_array_length(cc *c, ast_qualified_type t)
{
	const ctype *a = AST_TYPE(c, t);

	return a->n | (uint64_t)a->aux << 32;
}

CC_PRIVATE ast_qualified_type cc_type_vla(cc *c, ast_qualified_type elem, uint32_t sizenode)
{
	ctype t = { .kind = AST_TYPE_ARRAY, .flags = AST_TYPE_FLAG_VLA, .base = elem, .align = AST_TYPE(c, elem)->align, .n = sizenode };

	return AST_QUALIFIED_TYPE(cc_type_intern(c, &t), 0);
}

CC_PRIVATE uint32_t cc_type_function_hash(ast_qualified_type ret, const ast_qualified_type *par, uint32_t n, int flags)
{
	uint32_t h = (ret * 0x9E3779B1u) ^ (n * 0x85EBCA77u) ^ (uint32_t)flags;

	for (uint32_t i = 0; i < n; i++)
		h = (h ^ par[i]) * 0x01000193u;
	return h ^ (h >> 15);
}

/* function types have their own table: the key includes the parameter list */
/* add qualifiers; on an array type they belong to the element type (C11 6.7.3p9) */
CC_PRIVATE ast_qualified_type cc_type_qualify(cc *c, ast_qualified_type t, int q)
{
	const ctype *x = AST_TYPE(c, t);

	if (!q)
		return t;
	if (x->kind == AST_TYPE_ARRAY) {
		ast_qualified_type e = cc_type_qualify(c, x->base, q);

		if (x->flags & AST_TYPE_FLAG_VLA)
			return AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(cc_type_vla(c, e, x->n)), AST_QUALIFIED_TYPE_QUALIFIERS(t));
		return AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(cc_type_array(c, e, x->flags & AST_TYPE_FLAG_INCOMPLETE ? 0 : cc_type_array_length(c, t), x->flags & AST_TYPE_FLAG_INCOMPLETE)), AST_QUALIFIED_TYPE_QUALIFIERS(t));
	}
	return AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(t), AST_QUALIFIED_TYPE_QUALIFIERS(t) | q);
}

CC_PRIVATE ast_qualified_type cc_type_function(cc *c, ast_qualified_type ret, const ast_qualified_type *par, uint32_t n, int flags)
{
	uint32_t m = c->fnhcap - 1;

	for (uint32_t i = cc_type_function_hash(ret, par, n, flags) & m;; i = (i + 1) & m) {
		uint32_t e = c->fnhash[i];

		if (!e) {
			uint32_t idx;

			if (unlikely(c->dr_on))
				return cc_replay_type_function(c, ret, par, n, flags);	/* (a region's own table: first use) */
			idx = cc_type_new(c, AST_TYPE_FUNCTION);
			ctype *t = &c->ty[idx];

			t->flags = (uint8_t)flags, t->ext = (uint16_t)(flags >> 8), t->base = ret, t->n = n, t->size = 1, t->align = 4;
			t->aux = c->nparams;
			if (n)	/* par is NULL for an implicit or unprototyped function: memcpy(_, NULL, 0) is UB */
				memcpy(&c->params[c->nparams], par, n * sizeof(ast_qualified_type));
			c->nparams += n;
			c->fnhash[i] = idx;
			if (++c->nfn * 2 > c->fnhcap) {
				uint32_t ncap = c->fnhcap * 2, *nh = cc_heap_allocate(c, ncap * sizeof(uint32_t));

				memset(nh, 0, ncap * sizeof(uint32_t));
				for (uint32_t j = 0; j < c->fnhcap; j++) {
					uint32_t x = c->fnhash[j], k;
					const ctype *f = &c->ty[x];

					if (!x)
						continue;
					for (k = cc_type_function_hash(f->base, &c->params[f->aux], f->n, f->flags | f->ext << 8) & (ncap - 1); nh[k];
					     k = (k + 1) & (ncap - 1))
						;
					nh[k] = x;
				}
				c->fnhash = nh;
				c->fnhcap = ncap;
			}
			return AST_QUALIFIED_TYPE(idx, 0);
		}
		{
			const ctype *x = &c->ty[e];

			if (x->base == ret && x->n == n && (x->flags | x->ext << 8) == flags) {
				const ast_qualified_type *xp = &c->params[x->aux];	/* (short lists: no memcmp call) */
				uint32_t k = 0;

				while (k < n && xp[k] == par[k])
					k++;
				if (k == n)
					return AST_QUALIFIED_TYPE(e, 0);
			}
		}
	}
}

/*
 * t with the function type in it (t itself, or what a pointer points to, through pointers) given
 * x86-64 calling convention conv (CC_FNABI_*, 0: the target's own); 0 if t has no function type
 */
CC_PRIVATE ast_qualified_type ty_fnabi(cc *c, ast_qualified_type t, int conv)
{
	const ctype *x = AST_TYPE(c, t);

	if (x->kind == AST_TYPE_FUNCTION)
		return AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(cc_type_function(c, x->base, &c->params[x->aux], x->n, (x->flags & 0xff) | conv << 8)), AST_QUALIFIED_TYPE_QUALIFIERS(t));
	if (x->kind == AST_TYPE_POINTER && !(x->flags & AST_TYPE_FLAG_BLOCK)) {
		ast_qualified_type b = ty_fnabi(c, x->base, conv);

		return b ? AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(cc_type_pointer(c, b)), AST_QUALIFIED_TYPE_QUALIFIERS(t)) : 0;
	}
	return 0;
}

/* GNU vector of total size bytes with elements elem */
CC_PRIVATE ast_qualified_type cc_type_vector(cc *c, ast_qualified_type elem, uint32_t size)
{
	const ctype *e = AST_TYPE(c, elem);
	/* ARM64 caps vector alignment at 16; x86 and wasm use its full size. */
	ctype t = { .kind = AST_TYPE_VECTOR, .base = AST_QUALIFIED_TYPE_UNQUALIFIED(elem), .size = size,
		    .align = size > 16 && !PP_IS_WASM(c->o->target) && !CC_X86_64(c) ? 16 : size };

	t.n = e->size ? (uint32_t)(size / e->size) : 0;
	return AST_QUALIFIED_TYPE(cc_type_intern(c, &t), 0);
}

/* aligned typedef variant: same type, other alignment */
CC_PRIVATE ast_qualified_type cc_type_aligned(cc *c, ast_qualified_type t, uint32_t align)
{
	ctype v = *AST_TYPE(c, t);

	if (v.align == align && !(v.flags & AST_TYPE_FLAG_ALIGNED))
		return t;
	v.align = align;
	v.flags |= AST_TYPE_FLAG_ALIGNED;
	{
		uint32_t i = cc_type_intern(c, &v);

		c->ty[i].canon = AST_TYPE(c, t)->canon;
		return AST_QUALIFIED_TYPE(i, AST_QUALIFIED_TYPE_QUALIFIERS(t));
	}
}

/*
 * _Atomic(AST_TYPE): a type of its own (not compatible with AST_TYPE), laid out as clang
 * does: a size up to 16 bytes is rounded up to a power of 2, which is also
 * the alignment (_Atomic struct { char a[3]; } is 4/4).  n keeps AST_TYPE.
 */
CC_PRIVATE ast_qualified_type cc_type_atomic(cc *c, ast_qualified_type t, uint32_t loc)
{
	ctype v = *AST_TYPE(c, t);

	if (v.flags & AST_TYPE_FLAG_ATOMIC)
		return t;
	if (v.kind == AST_TYPE_SVE_BUILTIN) {
		cc_report_error(c, loc, "_Atomic cannot be applied to sizeless type '%s'", tyname(c, t));
		return t;
	}
	if (v.kind == AST_TYPE_ARRAY || v.kind == AST_TYPE_FUNCTION) {
		cc_report_error(c, loc, "_Atomic cannot be applied to %s type '%s' ", v.kind == AST_TYPE_ARRAY ? "array" : "function", tyname(c, t));
		return t;
	}
	if (!cc_type_complete(c, t)) {
		cc_report_error(c, loc, "_Atomic cannot be applied to incomplete type '%s' ", tyname(c, t));
		cc_report_forward_note(c, t);
		return t;
	}
	if (cc_type_kind_is_integer(v.kind) && v.n) {
		cc_report_error(c, loc, "_Atomic cannot be applied to integer type '%s' ", tyname(c, t));
		return t;
	}
	if (v.kind == AST_TYPE_VECTOR) {
		cc_report_error(c, loc, "_Atomic vector types are not supported by vcc");
		return t;
	}
	v.flags = (uint8_t)((v.flags & ~AST_TYPE_FLAG_ALIGNED) | AST_TYPE_FLAG_ATOMIC);
	v.n = AST_QUALIFIED_TYPE_INDEX(t);
	if (v.size && v.size <= 16) {
		uint64_t s = 1;

		while (s < v.size)
			s <<= 1;
		v.size = s;
		v.align = (uint32_t)s;
	}
	return AST_QUALIFIED_TYPE(cc_type_intern(c, &v), AST_QUALIFIED_TYPE_QUALIFIERS(t));
}

CC_PRIVATE uint64_t cc_type_size(cc *c, ast_qualified_type t) { return AST_TYPE(c, t)->size; }
CC_PRIVATE uint32_t cc_type_alignment(cc *c, ast_qualified_type t) { return AST_TYPE(c, t)->align; }

CC_PRIVATE bool cc_type_complete(cc *c, ast_qualified_type t)
{
	const ctype *x = AST_TYPE(c, t);

	switch (x->kind) {
	case AST_TYPE_VOID:
		return false;
	case AST_TYPE_ARRAY:
		return !(x->flags & AST_TYPE_FLAG_INCOMPLETE);
	case AST_TYPE_STRUCT:
	case AST_TYPE_UNION:
	case AST_TYPE_ENUM:
		return c->tags[x->aux].flags & AST_RECORD_FLAG_COMPLETE;
	default:
		return true;
	}
}

/* integer conversion rank */
CC_PRIVATE int cc_type_rank(cc *c, ast_qualified_type t)
{
	switch (AST_TYPE_KIND(c, t)) {
	case AST_TYPE_BOOL: return 1;
	case AST_TYPE_CHAR: case AST_TYPE_SIGNED_CHAR: case AST_TYPE_UNSIGNED_CHAR: return 2;
	case AST_TYPE_SHORT: case AST_TYPE_UNSIGNED_SHORT: return 3;
	case AST_TYPE_INT: case AST_TYPE_UNSIGNED_INT: return 4;
	case AST_TYPE_LONG: case AST_TYPE_UNSIGNED_LONG: return 5;
	case AST_TYPE_LONG_LONG: case AST_TYPE_UNSIGNED_LONG_LONG: return 6;
	case AST_TYPE_INT128: case AST_TYPE_UINT128: return 7;
	case AST_TYPE_BITINT: case AST_TYPE_UNSIGNED_BITINT: return 8;
	case AST_TYPE_ENUM: return cc_type_rank(c, AST_TYPE(c, t)->base);
	default: return 0;
	}
}

CC_PRIVATE ast_qualified_type cc_type_basic(int k) { return AST_QUALIFIED_TYPE(k, 0); }

CC_PRIVATE ast_qualified_type cc_type_unsigned(cc *c, ast_qualified_type t)
{
	if (cc_type_kind_is_integer(AST_TYPE_KIND(c, t)) && AST_TYPE(c, t)->n && !(AST_TYPE(c, t)->flags & AST_TYPE_FLAG_ATOMIC))
		return cc_type_bitint(c, AST_TYPE(c, t)->n, true);
	switch (AST_TYPE_KIND(c, t)) {
	case AST_TYPE_CHAR: case AST_TYPE_SIGNED_CHAR: return cc_type_basic(AST_TYPE_UNSIGNED_CHAR);
	case AST_TYPE_SHORT: return cc_type_basic(AST_TYPE_UNSIGNED_SHORT);
	case AST_TYPE_INT: return cc_type_basic(AST_TYPE_UNSIGNED_INT);
	case AST_TYPE_LONG: return cc_type_basic(AST_TYPE_UNSIGNED_LONG);
	case AST_TYPE_LONG_LONG: return cc_type_basic(AST_TYPE_UNSIGNED_LONG_LONG);
	case AST_TYPE_INT128: return cc_type_basic(AST_TYPE_UINT128);
	case AST_TYPE_ENUM: return cc_type_unsigned(c, AST_TYPE(c, t)->base);
	default: return AST_QUALIFIED_TYPE_UNQUALIFIED(t);
	}
}

/* the result of comparing vectors of type v: signed integers of the lane size */
CC_PRIVATE ast_qualified_type cc_type_vector_mask(cc *c, ast_qualified_type v)
{
	if (cc_type_is_bool_vector(c, v)) return AST_QUALIFIED_TYPE_UNQUALIFIED(v);
	uint32_t es = (uint32_t)AST_TYPE(c, AST_TYPE(c, v)->base)->size;
	int k = es == 1 ? AST_TYPE_SIGNED_CHAR : es == 2 ? AST_TYPE_SHORT : es == 4 ? AST_TYPE_INT : es == 8 ? CC_I64(c, false) : AST_TYPE_INT128;

	return cc_type_vector(c, cc_type_basic(k), (uint32_t)AST_TYPE(c, v)->size);
}

/* integer promotion (bit-fields are handled by the caller) */
CC_PRIVATE ast_qualified_type cc_type_promote(cc *c, ast_qualified_type t)
{
	int k = AST_TYPE_KIND(c, t);

	if (k == AST_TYPE_ENUM)
		return cc_type_promote(c, AST_TYPE(c, t)->base);
	if (cc_type_kind_is_integer(k) && !AST_TYPE(c, t)->n && cc_type_rank(c, t) < 4)
		return cc_type_basic(AST_TYPE_INT);
	return AST_QUALIFIED_TYPE(AST_TYPE(c, t)->canon, 0);
}

CC_PRIVATE ast_qualified_type cc_type_arithmetic_conversion(cc *c, ast_qualified_type a, ast_qualified_type b)
{
	int ka, kb;

	if (cc_type_is_float(c, a) || cc_type_is_float(c, b)) {
		ka = cc_type_is_float(c, a) ? AST_TYPE_KIND(c, a) : 0;
		kb = cc_type_is_float(c, b) ? AST_TYPE_KIND(c, b) : 0;
		return cc_type_basic(ka > kb ? ka : kb);
	}
	a = cc_type_promote(c, a);
	b = cc_type_promote(c, b);
	if (a == b)
		return a;
	{
		bool sa = cc_type_is_signed(c, a), sb = cc_type_is_signed(c, b);
		int ra = cc_type_rank(c, a), rb = cc_type_rank(c, b);

		if (AST_TYPE(c, a)->n || AST_TYPE(c, b)->n) {
			ra = (int)cc_type_integer_bits(c, a) * 16 + (AST_TYPE(c, a)->n ? 0 : ra);
			rb = (int)cc_type_integer_bits(c, b) * 16 + (AST_TYPE(c, b)->n ? 0 : rb);
		}

		if (sa == sb)
			return ra >= rb ? a : b;
		if (!sa && ra >= rb)
			return a;
		if (!sb && rb >= ra)
			return b;
		/* signed operand has greater rank */
		if (sa && cc_type_integer_bits(c, a) > cc_type_integer_bits(c, b))
			return a;
		if (sb && cc_type_integer_bits(c, b) > cc_type_integer_bits(c, a))
			return b;
		return cc_type_unsigned(c, sa ? a : b);
	}
}

static bool cc_type_is_compatible_qualified(cc *c, ast_qualified_type a, ast_qualified_type b)
{
	return AST_QUALIFIED_TYPE_QUALIFIERS(a) == AST_QUALIFIED_TYPE_QUALIFIERS(b) && cc_type_is_compatible(c, a, b);
}

/* GNU transparent unions accept a member type in a function prototype.
   This does not make the union and that member compatible as object types. */
static bool cc_type_is_compatible_parameter(cc *c, ast_qualified_type a, ast_qualified_type b)
{
	if (cc_type_is_compatible(c, a, b)) return true;
	const ctype *x = AST_TYPE(c, canon(c, a)), *y = AST_TYPE(c, canon(c, b));
	if (x->kind == AST_TYPE_UNION && y->kind == AST_TYPE_UNION) return false;
	if (y->kind == AST_TYPE_UNION) {
		x = y;
		b = a;
	}
	if (x->kind != AST_TYPE_UNION || !(x->flags & AST_TYPE_FLAG_TRANSPARENT)) return false;
	const tag *r = &c->tags[x->aux];
	for (uint32_t i = 0; i < r->nfield; i++)
		if (cc_type_is_compatible(c, r->f[i].ty, b)) return true;
	return false;
}

CC_PRIVATE bool cc_type_is_compatible(cc *c, ast_qualified_type a, ast_qualified_type b)
{
	const ctype *x, *y;

	a = canon(c, a), b = canon(c, b);
	if (AST_QUALIFIED_TYPE_INDEX(a) == AST_QUALIFIED_TYPE_INDEX(b))
		return true;
	x = AST_TYPE(c, a), y = AST_TYPE(c, b);
	if ((x->flags ^ y->flags) & AST_TYPE_FLAG_ATOMIC)
		return false;
	if (x->flags & AST_TYPE_FLAG_ATOMIC)
		return cc_type_is_compatible(c, cc_type_unatomic(c, a), cc_type_unatomic(c, b));
	/* an enum is compatible with its underlying type */
	if (x->kind == AST_TYPE_ENUM && y->kind != AST_TYPE_ENUM)
		return cc_type_is_compatible(c, x->base, b);
	if (y->kind == AST_TYPE_ENUM && x->kind != AST_TYPE_ENUM)
		return cc_type_is_compatible(c, a, y->base);
	if (x->kind != y->kind)
		return false;
	if (cc_type_kind_is_integer(x->kind) && x->n != y->n)
		return false;
	switch (x->kind) {
	case AST_TYPE_POINTER:
		return (x->flags & AST_TYPE_FLAG_BLOCK) == (y->flags & AST_TYPE_FLAG_BLOCK) && cc_type_is_compatible_qualified(c, x->base, y->base);
	case AST_TYPE_ARRAY:
		if (!cc_type_is_compatible_qualified(c, x->base, y->base))
			return false;
		if ((x->flags | y->flags) & AST_TYPE_FLAG_INCOMPLETE)
			return true;
		if ((x->flags | y->flags) & AST_TYPE_FLAG_VLA) {
			/* A constant outer bound of an array of VLAs still participates
			 * in compatibility. Only genuinely variable bounds are ignored. */
			cc_value xv, yv;
			uint64_t xn = cc_type_array_length(c, a), yn = cc_type_array_length(c, b);

			if (x->flags & AST_TYPE_FLAG_VLA) {
				if (!x->n || !cc_constant_eval(c, x->n, &xv) || xv.kind != CC_VALUE_INTEGER || !xv.ice)
					return true;
				xn = (uint64_t)xv.i;
			}
			if (y->flags & AST_TYPE_FLAG_VLA) {
				if (!y->n || !cc_constant_eval(c, y->n, &yv) || yv.kind != CC_VALUE_INTEGER || !yv.ice)
					return true;
				yn = (uint64_t)yv.i;
			}
			return xn == yn;
		}
		return x->n == y->n && x->aux == y->aux;
	case AST_TYPE_FUNCTION:
		if (!cc_type_is_compatible(c, x->base, y->base) || AST_QUALIFIED_TYPE_QUALIFIERS(x->base) != AST_QUALIFIED_TYPE_QUALIFIERS(y->base) || x->ext != y->ext)
			return false;	/* (ext: the x86-64 calling convention, ms_abi/sysv_abi) */
		if ((x->flags | y->flags) & AST_TYPE_FLAG_NO_PROTOTYPE)
			return true;
		if (x->n != y->n || (x->flags & AST_TYPE_FLAG_VARIADIC) != (y->flags & AST_TYPE_FLAG_VARIADIC))
			return false;
		for (uint32_t i = 0; i < x->n; i++)
			if (!cc_type_is_compatible_parameter(c, c->params[x->aux + i], c->params[y->aux + i]))
				return false;
		return true;
	default:
		return false;
	}
}

/* composite type of two compatible types (redeclarations, ?: on pointers) */
CC_PRIVATE ast_qualified_type cc_type_composite(cc *c, ast_qualified_type a, ast_qualified_type b)
{
	const ctype *x = AST_TYPE(c, a), *y = AST_TYPE(c, b);

	if ((x->flags & y->flags) & AST_TYPE_FLAG_ATOMIC)
		return cc_type_qualify(c, cc_type_atomic(c, cc_type_composite(c, cc_type_unatomic(c, a), cc_type_unatomic(c, b)), 0), AST_QUALIFIED_TYPE_QUALIFIERS(a));

	if (x->kind == AST_TYPE_ARRAY && y->kind == AST_TYPE_ARRAY) {
		ast_qualified_type e = cc_type_composite(c, x->base, y->base);

		if (!(x->flags & (AST_TYPE_FLAG_INCOMPLETE | AST_TYPE_FLAG_VLA)))
			return AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(cc_type_array(c, e, cc_type_array_length(c, a), 0)), AST_QUALIFIED_TYPE_QUALIFIERS(a));
		if (!(y->flags & (AST_TYPE_FLAG_INCOMPLETE | AST_TYPE_FLAG_VLA)))
			return AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(cc_type_array(c, e, cc_type_array_length(c, b), 0)), AST_QUALIFIED_TYPE_QUALIFIERS(a));
		return a;
	}
	if (x->kind == AST_TYPE_FUNCTION && y->kind == AST_TYPE_FUNCTION) {
		if (x->flags & AST_TYPE_FLAG_NO_PROTOTYPE)
			return b;
		return a;
	}
	if (x->kind == AST_TYPE_POINTER && y->kind == AST_TYPE_POINTER)
		return AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(cc_type_pointer(c, cc_type_composite(c, x->base, y->base))), AST_QUALIFIED_TYPE_QUALIFIERS(a));
	return a;
}
