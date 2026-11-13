/* vcc front end: types: interning, predicates, compatibility (C11 6.2.7) */
#include "cc-internal.h"

const cc_basic_type_layout cc_basic_types[TY_PTR] = {
	[TY_VOID] = { 1, 1 }, [TY_BOOL] = { 1, 1 },
	[TY_CHAR] = { 1, 1 }, [TY_SCHAR] = { 1, 1 }, [TY_UCHAR] = { 1, 1 },
	[TY_SHORT] = { 2, 2 }, [TY_USHORT] = { 2, 2 },
	[TY_INT] = { 4, 4 }, [TY_UINT] = { 4, 4 },
	[TY_LONG] = { 8, 8 }, [TY_ULONG] = { 8, 8 },
	[TY_LLONG] = { 8, 8 }, [TY_ULLONG] = { 8, 8 },
	[TY_INT128] = { 16, 16 }, [TY_UINT128] = { 16, 16 },
	[TY_BFLOAT16] = { 2, 2 }, [TY_FLOAT16] = { 2, 2 }, [TY_FLOAT] = { 4, 4 }, [TY_DOUBLE] = { 8, 8 },
	[TY_LDOUBLE] = { 16, 16 }, [TY_FLOAT128] = { 16, 16 },
};
CC_PRIVATE bool cc_type_is_bitint(cc *c, qty t) { int k = AST_TYPE_KIND(c, t); return k == TY_BITINT || k == TY_UBITINT; }
CC_PRIVATE bool cc_type_is_integer(cc *c, qty t) { int k = AST_TYPE_KIND(c, t); return cc_type_kind_is_integer(k) || k == TY_ENUM; }
CC_PRIVATE bool cc_type_is_float(cc *c, qty t) { return cc_type_kind_is_float(AST_TYPE_KIND(c, t)); }
CC_PRIVATE bool cc_type_is_arithmetic(cc *c, qty t) { return cc_type_is_integer(c, t) || cc_type_is_float(c, t); }
CC_PRIVATE bool cc_type_is_pointer(cc *c, qty t) { return AST_TYPE_KIND(c, t) == TY_PTR; }
CC_PRIVATE bool cc_type_is_scalar(cc *c, qty t) { return cc_type_is_arithmetic(c, t) || cc_type_is_pointer(c, t); }
CC_PRIVATE bool cc_type_is_void(cc *c, qty t) { return AST_TYPE_KIND(c, t) == TY_VOID; }
CC_PRIVATE bool cc_type_is_array(cc *c, qty t) { return AST_TYPE_KIND(c, t) == TY_ARRAY; }
CC_PRIVATE bool cc_type_is_function(cc *c, qty t) { return AST_TYPE_KIND(c, t) == TY_FUNC; }
CC_PRIVATE bool cc_type_is_record(cc *c, qty t) { int k = AST_TYPE_KIND(c, t); return k == TY_STRUCT || k == TY_UNION; }
CC_PRIVATE bool cc_type_is_vector(cc *c, qty t) { return AST_TYPE_KIND(c, t) == TY_VECTOR; }
CC_PRIVATE bool cc_type_is_bool_vector(cc *c, qty t) { return cc_type_is_vector(c, t) && AST_TYPE_KIND(c, AST_TYPE(c, t)->base) == TY_BOOL; }
/* initialized by a list of elements */
CC_PRIVATE bool cc_type_is_complex(cc *c, qty t) { return AST_TYPE_KIND(c, t) == TY_STRUCT && (c->tags[AST_TYPE(c, t)->aux].flags & RF_COMPLEX); }
CC_PRIVATE bool cc_type_is_initializer_aggregate(cc *c, qty t) { int k = AST_TYPE_KIND(c, t); return (k == TY_STRUCT && !cc_type_is_complex(c, t)) || k == TY_UNION || k == TY_ARRAY || k == TY_VECTOR; }

CC_PRIVATE qty canon(cc *c, qty t) { return QT(AST_TYPE(c, t)->canon, QQ(t)); }

CC_PRIVATE bool cc_type_is_signed(cc *c, qty t)
{
	int k = AST_TYPE_KIND(c, t);

	if (k == TY_ENUM)
		return cc_type_is_signed(c, AST_TYPE(c, t)->base);
	if (k == TY_CHAR)
		return !c->o->unsigned_char;
	return k == TY_SCHAR || k == TY_SHORT || k == TY_INT || k == TY_LONG || k == TY_LLONG || k == TY_INT128 || k == TY_BITINT;
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
			if (!(t->flags & TF_ALIGNED))
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
CC_PRIVATE qty cc_type_bitint(cc *c, unsigned bits, bool unsign)
{
	if (bits > 128) {
		ctype t = { .kind = unsign ? TY_UBITINT : TY_BITINT, .size = 8 * (((uint64_t)bits + 63) / 64), .align = 8, .n = bits };
		return QT(cc_type_intern(c, &t), 0);
	}
	unsigned size = bits <= 8 ? 1 : bits <= 16 ? 2 : bits <= 32 ? 4 : bits <= 64 ? 8 : 16;
	int kind = size == 1 ? TY_SCHAR : size == 2 ? TY_SHORT : size == 4 ? TY_INT : size == 8 ? TY_LLONG : TY_INT128;
	ctype t = { .kind = (uint8_t)(kind + unsign), .size = size, .align = size > 8 && (c->o->target == PP_LINUX_X86_64 || PP_IS_WASM(c->o->target)) ? 8 : size, .n = bits };

	return QT(cc_type_intern(c, &t), 0);
}

CC_PRIVATE unsigned cc_type_integer_bits(cc *c, qty t)
{
	if (AST_TYPE_KIND(c, t) == TY_ENUM)
		return cc_type_integer_bits(c, AST_TYPE(c, t)->base);
	if (cc_type_kind_is_integer(AST_TYPE_KIND(c, t)) && !(AST_TYPE(c, t)->flags & TF_ATOMIC) && AST_TYPE(c, t)->n)
		return AST_TYPE(c, t)->n;
	return (unsigned)cc_type_size(c, t) * 8;
}

CC_PRIVATE qty cc_type_pointer(cc *c, qty base)
{
	uint32_t i = c->ptrc[base];

	/* the cache may be stale (a restored snapshot): the entry must still be the pointer */
	if (likely(i >= c->ptr_lo && i < c->nty)) {	/* (ptr_lo: 1, or a recorded region's first type) */
		const ctype *p = &c->ty[i];

		if (p->kind == TY_PTR && !p->flags && !p->ext && p->base == base && p->align == CC_PTRSIZE(c))	/* (ext: a C++ reference) */
			return QT(i, 0);
	}
	{
		ctype t = { .kind = TY_PTR, .size = CC_PTRSIZE(c), .align = CC_PTRSIZE(c), .base = base };

		i = cc_type_intern(c, &t);
		c->ptrc[base] = i;
		return QT(i, 0);
	}
}

CC_PRIVATE qty cc_type_array(cc *c, qty elem, uint64_t n, int flags)
{
	const ctype *e = AST_TYPE(c, elem);
	ctype t = { .kind = TY_ARRAY, .flags = (uint8_t)flags, .base = elem, .align = e->align };

	if (!(flags & (TF_INCOMPLETE | TF_VLA))) {
		t.n = (uint32_t)n;
		t.aux = (uint32_t)(n >> 32);
		t.size = e->size * n;
	}
	return QT(cc_type_intern(c, &t), 0);
}

CC_PRIVATE uint64_t cc_type_array_length(cc *c, qty t)
{
	const ctype *a = AST_TYPE(c, t);

	return a->n | (uint64_t)a->aux << 32;
}

CC_PRIVATE qty cc_type_vla(cc *c, qty elem, uint32_t sizenode)
{
	ctype t = { .kind = TY_ARRAY, .flags = TF_VLA, .base = elem, .align = AST_TYPE(c, elem)->align, .n = sizenode };

	return QT(cc_type_intern(c, &t), 0);
}

CC_PRIVATE uint32_t cc_type_function_hash(qty ret, const qty *par, uint32_t n, int flags)
{
	uint32_t h = (ret * 0x9E3779B1u) ^ (n * 0x85EBCA77u) ^ (uint32_t)flags;

	for (uint32_t i = 0; i < n; i++)
		h = (h ^ par[i]) * 0x01000193u;
	return h ^ (h >> 15);
}

/* function types have their own table: the key includes the parameter list */
/* add qualifiers; on an array type they belong to the element type (C11 6.7.3p9) */
CC_PRIVATE qty cc_type_qualify(cc *c, qty t, int q)
{
	const ctype *x = AST_TYPE(c, t);

	if (!q)
		return t;
	if (x->kind == TY_ARRAY) {
		qty e = cc_type_qualify(c, x->base, q);

		if (x->flags & TF_VLA)
			return QT(QI(cc_type_vla(c, e, x->n)), QQ(t));
		return QT(QI(cc_type_array(c, e, x->flags & TF_INCOMPLETE ? 0 : cc_type_array_length(c, t), x->flags & TF_INCOMPLETE)), QQ(t));
	}
	return QT(QI(t), QQ(t) | q);
}

CC_PRIVATE qty cc_type_function(cc *c, qty ret, const qty *par, uint32_t n, int flags)
{
	uint32_t m = c->fnhcap - 1;

	for (uint32_t i = cc_type_function_hash(ret, par, n, flags) & m;; i = (i + 1) & m) {
		uint32_t e = c->fnhash[i];

		if (!e) {
			uint32_t idx;

			if (unlikely(c->dr_on))
				return cc_replay_type_function(c, ret, par, n, flags);	/* (a region's own table: first use) */
			idx = cc_type_new(c, TY_FUNC);
			ctype *t = &c->ty[idx];

			t->flags = (uint8_t)flags, t->base = ret, t->n = n, t->size = 1, t->align = 4;
			t->aux = c->nparams;
			if (n)	/* par is NULL for an implicit or unprototyped function: memcpy(_, NULL, 0) is UB */
				memcpy(&c->params[c->nparams], par, n * sizeof(qty));
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
					for (k = cc_type_function_hash(f->base, &c->params[f->aux], f->n, f->flags) & (ncap - 1); nh[k];
					     k = (k + 1) & (ncap - 1))
						;
					nh[k] = x;
				}
				c->fnhash = nh;
				c->fnhcap = ncap;
			}
			return QT(idx, 0);
		}
		{
			const ctype *x = &c->ty[e];

			if (x->base == ret && x->n == n && x->flags == flags) {
				const qty *xp = &c->params[x->aux];	/* (short lists: no memcmp call) */
				uint32_t k = 0;

				while (k < n && xp[k] == par[k])
					k++;
				if (k == n)
					return QT(e, 0);
			}
		}
	}
}

/* GNU vector of total size bytes with elements elem */
CC_PRIVATE qty cc_type_vector(cc *c, qty elem, uint32_t size)
{
	const ctype *e = AST_TYPE(c, elem);
	/* ARM64 caps vector alignment at 16; x86 and wasm use its full size. */
	ctype t = { .kind = TY_VECTOR, .base = QUNQ(elem), .size = size,
		    .align = size > 16 && !PP_IS_WASM(c->o->target) && c->o->target != PP_LINUX_X86_64 ? 16 : size };

	t.n = e->size ? (uint32_t)(size / e->size) : 0;
	return QT(cc_type_intern(c, &t), 0);
}

/* aligned typedef variant: same type, other alignment */
CC_PRIVATE qty cc_type_aligned(cc *c, qty t, uint32_t align)
{
	ctype v = *AST_TYPE(c, t);

	if (v.align == align && !(v.flags & TF_ALIGNED))
		return t;
	v.align = align;
	v.flags |= TF_ALIGNED;
	{
		uint32_t i = cc_type_intern(c, &v);

		c->ty[i].canon = AST_TYPE(c, t)->canon;
		return QT(i, QQ(t));
	}
}

/*
 * _Atomic(AST_TYPE): a type of its own (not compatible with AST_TYPE), laid out as clang
 * does: a size up to 16 bytes is rounded up to a power of 2, which is also
 * the alignment (_Atomic struct { char a[3]; } is 4/4).  n keeps AST_TYPE.
 */
CC_PRIVATE qty cc_type_atomic(cc *c, qty t, uint32_t loc)
{
	ctype v = *AST_TYPE(c, t);

	if (v.flags & TF_ATOMIC)
		return t;
	if (v.kind == TY_SVE) {
		cc_report_error(c, loc, "_Atomic cannot be applied to sizeless type '%s'", tyname(c, t));
		return t;
	}
	if (v.kind == TY_ARRAY || v.kind == TY_FUNC) {
		cc_report_error(c, loc, "_Atomic cannot be applied to %s type '%s' ", v.kind == TY_ARRAY ? "array" : "function", tyname(c, t));
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
	if (v.kind == TY_VECTOR) {
		cc_report_error(c, loc, "_Atomic vector types are not supported by vcc");
		return t;
	}
	v.flags = (uint8_t)((v.flags & ~TF_ALIGNED) | TF_ATOMIC);
	v.n = QI(t);
	if (v.size && v.size <= 16) {
		uint64_t s = 1;

		while (s < v.size)
			s <<= 1;
		v.size = s;
		v.align = (uint32_t)s;
	}
	return QT(cc_type_intern(c, &v), QQ(t));
}

CC_PRIVATE uint64_t cc_type_size(cc *c, qty t) { return AST_TYPE(c, t)->size; }
CC_PRIVATE uint32_t cc_type_alignment(cc *c, qty t) { return AST_TYPE(c, t)->align; }

CC_PRIVATE bool cc_type_complete(cc *c, qty t)
{
	const ctype *x = AST_TYPE(c, t);

	switch (x->kind) {
	case TY_VOID:
		return false;
	case TY_ARRAY:
		return !(x->flags & TF_INCOMPLETE);
	case TY_STRUCT:
	case TY_UNION:
	case TY_ENUM:
		return c->tags[x->aux].flags & RF_COMPLETE;
	default:
		return true;
	}
}

/* integer conversion rank */
CC_PRIVATE int cc_type_rank(cc *c, qty t)
{
	switch (AST_TYPE_KIND(c, t)) {
	case TY_BOOL: return 1;
	case TY_CHAR: case TY_SCHAR: case TY_UCHAR: return 2;
	case TY_SHORT: case TY_USHORT: return 3;
	case TY_INT: case TY_UINT: return 4;
	case TY_LONG: case TY_ULONG: return 5;
	case TY_LLONG: case TY_ULLONG: return 6;
	case TY_INT128: case TY_UINT128: return 7;
	case TY_BITINT: case TY_UBITINT: return 8;
	case TY_ENUM: return cc_type_rank(c, AST_TYPE(c, t)->base);
	default: return 0;
	}
}

CC_PRIVATE qty cc_type_basic(int k) { return QT(k, 0); }

CC_PRIVATE qty cc_type_unsigned(cc *c, qty t)
{
	if (cc_type_kind_is_integer(AST_TYPE_KIND(c, t)) && AST_TYPE(c, t)->n && !(AST_TYPE(c, t)->flags & TF_ATOMIC))
		return cc_type_bitint(c, AST_TYPE(c, t)->n, true);
	switch (AST_TYPE_KIND(c, t)) {
	case TY_CHAR: case TY_SCHAR: return cc_type_basic(TY_UCHAR);
	case TY_SHORT: return cc_type_basic(TY_USHORT);
	case TY_INT: return cc_type_basic(TY_UINT);
	case TY_LONG: return cc_type_basic(TY_ULONG);
	case TY_LLONG: return cc_type_basic(TY_ULLONG);
	case TY_INT128: return cc_type_basic(TY_UINT128);
	case TY_ENUM: return cc_type_unsigned(c, AST_TYPE(c, t)->base);
	default: return QUNQ(t);
	}
}

/* the result of comparing vectors of type v: signed integers of the lane size */
CC_PRIVATE qty cc_type_vector_mask(cc *c, qty v)
{
	if (cc_type_is_bool_vector(c, v)) return QUNQ(v);
	uint32_t es = (uint32_t)AST_TYPE(c, AST_TYPE(c, v)->base)->size;
	int k = es == 1 ? TY_SCHAR : es == 2 ? TY_SHORT : es == 4 ? TY_INT : es == 8 ? CC_I64(c, false) : TY_INT128;

	return cc_type_vector(c, cc_type_basic(k), (uint32_t)AST_TYPE(c, v)->size);
}

/* integer promotion (bit-fields are handled by the caller) */
CC_PRIVATE qty cc_type_promote(cc *c, qty t)
{
	int k = AST_TYPE_KIND(c, t);

	if (k == TY_ENUM)
		return cc_type_promote(c, AST_TYPE(c, t)->base);
	if (cc_type_kind_is_integer(k) && !AST_TYPE(c, t)->n && cc_type_rank(c, t) < 4)
		return cc_type_basic(TY_INT);
	return QT(AST_TYPE(c, t)->canon, 0);
}

CC_PRIVATE qty cc_type_arithmetic_conversion(cc *c, qty a, qty b)
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

static bool cc_type_is_compatible_qualified(cc *c, qty a, qty b)
{
	return QQ(a) == QQ(b) && cc_type_is_compatible(c, a, b);
}

/* GNU transparent unions accept a member type in a function prototype.
   This does not make the union and that member compatible as object types. */
static bool cc_type_is_compatible_parameter(cc *c, qty a, qty b)
{
	if (cc_type_is_compatible(c, a, b)) return true;
	const ctype *x = AST_TYPE(c, canon(c, a)), *y = AST_TYPE(c, canon(c, b));
	if (x->kind == TY_UNION && y->kind == TY_UNION) return false;
	if (y->kind == TY_UNION) {
		x = y;
		b = a;
	}
	if (x->kind != TY_UNION || !(x->flags & TF_TRANSPARENT)) return false;
	const tag *r = &c->tags[x->aux];
	for (uint32_t i = 0; i < r->nfield; i++)
		if (cc_type_is_compatible(c, r->f[i].ty, b)) return true;
	return false;
}

CC_PRIVATE bool cc_type_is_compatible(cc *c, qty a, qty b)
{
	const ctype *x, *y;

	a = canon(c, a), b = canon(c, b);
	if (QI(a) == QI(b))
		return true;
	x = AST_TYPE(c, a), y = AST_TYPE(c, b);
	if ((x->flags ^ y->flags) & TF_ATOMIC)
		return false;
	if (x->flags & TF_ATOMIC)
		return cc_type_is_compatible(c, cc_type_unatomic(c, a), cc_type_unatomic(c, b));
	/* an enum is compatible with its underlying type */
	if (x->kind == TY_ENUM && y->kind != TY_ENUM)
		return cc_type_is_compatible(c, x->base, b);
	if (y->kind == TY_ENUM && x->kind != TY_ENUM)
		return cc_type_is_compatible(c, a, y->base);
	if (x->kind != y->kind)
		return false;
	if (cc_type_kind_is_integer(x->kind) && x->n != y->n)
		return false;
	switch (x->kind) {
	case TY_PTR:
		return (x->flags & TF_BLOCK) == (y->flags & TF_BLOCK) && cc_type_is_compatible_qualified(c, x->base, y->base);
	case TY_ARRAY:
		if (!cc_type_is_compatible_qualified(c, x->base, y->base))
			return false;
		if ((x->flags | y->flags) & TF_INCOMPLETE)
			return true;
		if ((x->flags | y->flags) & TF_VLA) {
			/* A constant outer bound of an array of VLAs still participates
			 * in compatibility. Only genuinely variable bounds are ignored. */
			cval xv, yv;
			uint64_t xn = cc_type_array_length(c, a), yn = cc_type_array_length(c, b);

			if (x->flags & TF_VLA) {
				if (!x->n || !cc_constant_eval(c, x->n, &xv) || xv.kind != CV_INT || !xv.ice)
					return true;
				xn = (uint64_t)xv.i;
			}
			if (y->flags & TF_VLA) {
				if (!y->n || !cc_constant_eval(c, y->n, &yv) || yv.kind != CV_INT || !yv.ice)
					return true;
				yn = (uint64_t)yv.i;
			}
			return xn == yn;
		}
		return x->n == y->n && x->aux == y->aux;
	case TY_FUNC:
		if (!cc_type_is_compatible(c, x->base, y->base) || QQ(x->base) != QQ(y->base))
			return false;
		if ((x->flags | y->flags) & TF_NOPROTO)
			return true;
		if (x->n != y->n || (x->flags & TF_VARIADIC) != (y->flags & TF_VARIADIC))
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
CC_PRIVATE qty cc_type_composite(cc *c, qty a, qty b)
{
	const ctype *x = AST_TYPE(c, a), *y = AST_TYPE(c, b);

	if ((x->flags & y->flags) & TF_ATOMIC)
		return cc_type_qualify(c, cc_type_atomic(c, cc_type_composite(c, cc_type_unatomic(c, a), cc_type_unatomic(c, b)), 0), QQ(a));

	if (x->kind == TY_ARRAY && y->kind == TY_ARRAY) {
		qty e = cc_type_composite(c, x->base, y->base);

		if (!(x->flags & (TF_INCOMPLETE | TF_VLA)))
			return QT(QI(cc_type_array(c, e, cc_type_array_length(c, a), 0)), QQ(a));
		if (!(y->flags & (TF_INCOMPLETE | TF_VLA)))
			return QT(QI(cc_type_array(c, e, cc_type_array_length(c, b), 0)), QQ(a));
		return a;
	}
	if (x->kind == TY_FUNC && y->kind == TY_FUNC) {
		if (x->flags & TF_NOPROTO)
			return b;
		return a;
	}
	if (x->kind == TY_PTR && y->kind == TY_PTR)
		return QT(QI(cc_type_pointer(c, cc_type_composite(c, x->base, y->base))), QQ(a));
	return a;
}
