/* vcc front end: types: interning, predicates, compatibility (C11 6.2.7) */
#include "ccimpl.h"

const basic_ty basic[TY_PTR] = {
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
VPRIV bool is_bigint(cc *c, qty t) { int k = KIND(c, t); return k == TY_BITINT || k == TY_UBITINT; }
VPRIV bool is_integer(cc *c, qty t) { int k = KIND(c, t); return is_int_kind(k) || k == TY_ENUM; }
VPRIV bool is_float(cc *c, qty t) { return is_flt_kind(KIND(c, t)); }
VPRIV bool is_arith(cc *c, qty t) { return is_integer(c, t) || is_float(c, t); }
VPRIV bool is_ptr(cc *c, qty t) { return KIND(c, t) == TY_PTR; }
VPRIV bool is_scalar(cc *c, qty t) { return is_arith(c, t) || is_ptr(c, t); }
VPRIV bool is_void(cc *c, qty t) { return KIND(c, t) == TY_VOID; }
VPRIV bool is_array(cc *c, qty t) { return KIND(c, t) == TY_ARRAY; }
VPRIV bool is_func(cc *c, qty t) { return KIND(c, t) == TY_FUNC; }
VPRIV bool is_record(cc *c, qty t) { int k = KIND(c, t); return k == TY_STRUCT || k == TY_UNION; }
VPRIV bool is_vector(cc *c, qty t) { return KIND(c, t) == TY_VECTOR; }
VPRIV bool is_bool_vector(cc *c, qty t) { return is_vector(c, t) && KIND(c, T(c, t)->base) == TY_BOOL; }
/* initialized by a list of elements */
VPRIV bool is_complex(cc *c, qty t) { return KIND(c, t) == TY_STRUCT && (c->tags[T(c, t)->aux].flags & RF_COMPLEX); }
VPRIV bool is_ilagg(cc *c, qty t) { int k = KIND(c, t); return (k == TY_STRUCT && !is_complex(c, t)) || k == TY_UNION || k == TY_ARRAY || k == TY_VECTOR; }

VPRIV qty canon(cc *c, qty t) { return QT(T(c, t)->canon, QQ(t)); }

VPRIV bool is_signed(cc *c, qty t)
{
	int k = KIND(c, t);

	if (k == TY_ENUM)
		return is_signed(c, T(c, t)->base);
	if (k == TY_CHAR)
		return !c->o->unsigned_char;
	return k == TY_SCHAR || k == TY_SHORT || k == TY_INT || k == TY_LONG || k == TY_LLONG || k == TY_INT128 || k == TY_BITINT;
}

VPRIV uint32_t ty_hash(const ctype *t)
{
	uint64_t h = (uint64_t)t->kind * 0x9E3779B97F4A7C15ull;

	h ^= t->base + 0x632BE59BD9B4E019ull + (h << 6) + (h >> 2);
	h ^= t->n + 0x8CB92BA72F3D8DD7ull + (h << 6) + (h >> 2);
	h ^= t->aux + (uint64_t)t->flags * 31 + t->align * 131 + (uint64_t)t->ext * 7919 + (h << 6) + (h >> 2);
	return (uint32_t)(h ^ (h >> 32));
}

static bool ty_eq(const ctype *a, const ctype *b)
{
	return a->kind == b->kind && a->flags == b->flags && a->base == b->base && a->n == b->n &&
	       a->aux == b->aux && a->align == b->align && a->size == b->size && a->ext == b->ext;
}

/* intern a type (records and enums are created with ty_new) */
VPRIV uint32_t ty_intern(cc *c, const ctype *t)
{
	uint32_t h = ty_hash(t), m = c->tyhcap - 1;

	for (uint32_t i = h & m;; i = (i + 1) & m) {
		uint32_t e = c->tyhash[i];

		if (!e) {
			uint32_t idx;

			if (unlikely(c->dr_on))
				return dr_intern(c, t);	/* (a region's own table: first use of t in it) */
			idx = c->nty++;

			c->ty[idx] = *t;
			if (!(t->flags & TF_ALIGNED))
				c->ty[idx].canon = idx;
			c->tyhash[i] = idx;
			if (c->nty * 2 > c->tyhcap) {	/* grow */
				uint32_t ncap = c->tyhcap * 2, *nh = halloc(c, ncap * sizeof(uint32_t));

				memset(nh, 0, ncap * sizeof(uint32_t));
				for (uint32_t j = 0; j < c->tyhcap; j++) {
					uint32_t x = c->tyhash[j], k;

					if (!x)
						continue;
					for (k = ty_hash(&c->ty[x]) & (ncap - 1); nh[k]; k = (k + 1) & (ncap - 1))
						;
					nh[k] = x;
				}
				c->tyhash = nh;
				c->tyhcap = ncap;
			}
			return idx;
		}
		if (ty_eq(&c->ty[e], t)) {
			return e;
		}
	}
}

VPRIV uint32_t ty_new(cc *c, int kind)
{
	uint32_t idx = c->nty++;

	memset(&c->ty[idx], 0, sizeof(ctype));
	c->ty[idx].kind = (uint8_t)kind;
	c->ty[idx].canon = idx;
	if (unlikely(c->dr_on))
		dr_new(c, idx);	/* (frontend/c/replay.c: the type log) */
	return idx;
}

/* Bit-precise integers retain a storage kind for scalar lowering, but their
   precision makes them distinct from all standard integer types. */
VPRIV qty ty_bitint(cc *c, unsigned bits, bool unsign)
{
	if (bits > 128) {
		ctype t = { .kind = unsign ? TY_UBITINT : TY_BITINT, .size = 8 * (((uint64_t)bits + 63) / 64), .align = 8, .n = bits };
		return QT(ty_intern(c, &t), 0);
	}
	unsigned size = bits <= 8 ? 1 : bits <= 16 ? 2 : bits <= 32 ? 4 : bits <= 64 ? 8 : 16;
	int kind = size == 1 ? TY_SCHAR : size == 2 ? TY_SHORT : size == 4 ? TY_INT : size == 8 ? TY_LLONG : TY_INT128;
	ctype t = { .kind = (uint8_t)(kind + unsign), .size = size, .align = size > 8 && (CC_X86_64(c) || PP_IS_WASM(c->o->target)) ? 8 : size, .n = bits };

	return QT(ty_intern(c, &t), 0);
}

VPRIV unsigned int_bits(cc *c, qty t)
{
	if (KIND(c, t) == TY_ENUM)
		return int_bits(c, T(c, t)->base);
	if (is_int_kind(KIND(c, t)) && !(T(c, t)->flags & TF_ATOMIC) && T(c, t)->n)
		return T(c, t)->n;
	return (unsigned)ty_size(c, t) * 8;
}

VPRIV qty ty_ptr(cc *c, qty base)
{
	uint32_t i = c->ptrc[base];

	/* the cache may be stale (a restored snapshot): the entry must still be the pointer */
	if (likely(i >= c->ptr_lo && i < c->nty)) {	/* (ptr_lo: 1, or a recorded region's first type) */
		const ctype *p = &c->ty[i];

		if (p->kind == TY_PTR && !p->flags && p->base == base && p->align == CC_PTRSIZE(c))
			return QT(i, 0);
	}
	{
		ctype t = { .kind = TY_PTR, .size = CC_PTRSIZE(c), .align = CC_PTRSIZE(c), .base = base };

		i = ty_intern(c, &t);
		c->ptrc[base] = i;
		return QT(i, 0);
	}
}

VPRIV qty ty_array(cc *c, qty elem, uint64_t n, int flags)
{
	const ctype *e = T(c, elem);
	ctype t = { .kind = TY_ARRAY, .flags = (uint8_t)flags, .base = elem, .align = e->align };

	if (!(flags & (TF_INCOMPLETE | TF_VLA))) {
		t.n = (uint32_t)n;
		t.aux = (uint32_t)(n >> 32);
		t.size = e->size * n;
	}
	return QT(ty_intern(c, &t), 0);
}

VPRIV uint64_t arr_len(cc *c, qty t)
{
	const ctype *a = T(c, t);

	return a->n | (uint64_t)a->aux << 32;
}

VPRIV qty ty_vla(cc *c, qty elem, uint32_t sizenode)
{
	ctype t = { .kind = TY_ARRAY, .flags = TF_VLA, .base = elem, .align = T(c, elem)->align, .n = sizenode };

	return QT(ty_intern(c, &t), 0);
}

VPRIV uint32_t fn_hash(qty ret, const qty *par, uint32_t n, int flags)
{
	uint32_t h = (ret * 0x9E3779B1u) ^ (n * 0x85EBCA77u) ^ (uint32_t)flags;

	for (uint32_t i = 0; i < n; i++)
		h = (h ^ par[i]) * 0x01000193u;
	return h ^ (h >> 15);
}

/* function types have their own table: the key includes the parameter list */
/* add qualifiers; on an array type they belong to the element type (C11 6.7.3p9) */
VPRIV qty qualify(cc *c, qty t, int q)
{
	const ctype *x = T(c, t);

	if (!q)
		return t;
	if (x->kind == TY_ARRAY) {
		qty e = qualify(c, x->base, q);

		if (x->flags & TF_VLA)
			return QT(QI(ty_vla(c, e, x->n)), QQ(t));
		return QT(QI(ty_array(c, e, x->flags & TF_INCOMPLETE ? 0 : arr_len(c, t), x->flags & TF_INCOMPLETE)), QQ(t));
	}
	return QT(QI(t), QQ(t) | q);
}

VPRIV qty ty_func(cc *c, qty ret, const qty *par, uint32_t n, int flags)
{
	uint32_t m = c->fnhcap - 1;

	for (uint32_t i = fn_hash(ret, par, n, flags) & m;; i = (i + 1) & m) {
		uint32_t e = c->fnhash[i];

		if (!e) {
			uint32_t idx;

			if (unlikely(c->dr_on))
				return dr_func(c, ret, par, n, flags);	/* (a region's own table: first use) */
			idx = ty_new(c, TY_FUNC);
			ctype *t = &c->ty[idx];

			t->flags = (uint8_t)flags, t->ext = (uint16_t)(flags >> 8), t->base = ret, t->n = n, t->size = 1, t->align = 4;
			t->aux = c->nparams;
			if (n)	/* par is NULL for an implicit or unprototyped function: memcpy(_, NULL, 0) is UB */
				memcpy(&c->params[c->nparams], par, n * sizeof(qty));
			c->nparams += n;
			c->fnhash[i] = idx;
			if (++c->nfn * 2 > c->fnhcap) {
				uint32_t ncap = c->fnhcap * 2, *nh = halloc(c, ncap * sizeof(uint32_t));

				memset(nh, 0, ncap * sizeof(uint32_t));
				for (uint32_t j = 0; j < c->fnhcap; j++) {
					uint32_t x = c->fnhash[j], k;
					const ctype *f = &c->ty[x];

					if (!x)
						continue;
					for (k = fn_hash(f->base, &c->params[f->aux], f->n, f->flags | f->ext << 8) & (ncap - 1); nh[k];
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

			if (x->base == ret && x->n == n && (x->flags | x->ext << 8) == flags) {
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

/*
 * t with the function type in it (t itself, or what a pointer points to, through pointers) given
 * x86-64 calling convention conv (CC_FNABI_*, 0: the target's own); 0 if t has no function type
 */
VPRIV qty ty_fnabi(cc *c, qty t, int conv)
{
	const ctype *x = T(c, t);

	if (x->kind == TY_FUNC)
		return QT(QI(ty_func(c, x->base, &c->params[x->aux], x->n, (x->flags & 0xff) | conv << 8)), QQ(t));
	if (x->kind == TY_PTR && !(x->flags & TF_BLOCK)) {
		qty b = ty_fnabi(c, x->base, conv);

		return b ? QT(QI(ty_ptr(c, b)), QQ(t)) : 0;
	}
	return 0;
}

/* GNU vector of total size bytes with elements elem */
VPRIV qty ty_vector(cc *c, qty elem, uint32_t size)
{
	const ctype *e = T(c, elem);
	/* ARM64 caps vector alignment at 16; x86 and wasm use its full size. */
	ctype t = { .kind = TY_VECTOR, .base = QUNQ(elem), .size = size,
		    .align = size > 16 && !PP_IS_WASM(c->o->target) && !CC_X86_64(c) ? 16 : size };

	t.n = e->size ? (uint32_t)(size / e->size) : 0;
	return QT(ty_intern(c, &t), 0);
}

/* aligned typedef variant: same type, other alignment */
VPRIV qty ty_aligned(cc *c, qty t, uint32_t align)
{
	ctype v = *T(c, t);

	if (v.align == align && !(v.flags & TF_ALIGNED))
		return t;
	v.align = align;
	v.flags |= TF_ALIGNED;
	{
		uint32_t i = ty_intern(c, &v);

		c->ty[i].canon = T(c, t)->canon;
		return QT(i, QQ(t));
	}
}

/*
 * _Atomic(T): a type of its own (not compatible with T), laid out as clang
 * does: a size up to 16 bytes is rounded up to a power of 2, which is also
 * the alignment (_Atomic struct { char a[3]; } is 4/4).  n keeps T.
 */
VPRIV qty ty_atomic(cc *c, qty t, uint32_t loc)
{
	ctype v = *T(c, t);

	if (v.flags & TF_ATOMIC)
		return t;
	if (v.kind == TY_SVE) {
		cc_error(c, loc, "_Atomic cannot be applied to sizeless type '%s'", tyname(c, t));
		return t;
	}
	if (v.kind == TY_ARRAY || v.kind == TY_FUNC) {
		cc_error(c, loc, "_Atomic cannot be applied to %s type '%s' ", v.kind == TY_ARRAY ? "array" : "function", tyname(c, t));
		return t;
	}
	if (!complete(c, t)) {
		cc_error(c, loc, "_Atomic cannot be applied to incomplete type '%s' ", tyname(c, t));
		fwd_note(c, t);
		return t;
	}
	if (is_int_kind(v.kind) && v.n) {
		cc_error(c, loc, "_Atomic cannot be applied to integer type '%s' ", tyname(c, t));
		return t;
	}
	if (v.kind == TY_VECTOR) {
		cc_error(c, loc, "_Atomic vector types are not supported by vcc");
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
	return QT(ty_intern(c, &v), QQ(t));
}

VPRIV uint64_t ty_size(cc *c, qty t) { return T(c, t)->size; }
VPRIV uint32_t ty_align(cc *c, qty t) { return T(c, t)->align; }

VPRIV bool complete(cc *c, qty t)
{
	const ctype *x = T(c, t);

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
VPRIV int rank(cc *c, qty t)
{
	switch (KIND(c, t)) {
	case TY_BOOL: return 1;
	case TY_CHAR: case TY_SCHAR: case TY_UCHAR: return 2;
	case TY_SHORT: case TY_USHORT: return 3;
	case TY_INT: case TY_UINT: return 4;
	case TY_LONG: case TY_ULONG: return 5;
	case TY_LLONG: case TY_ULLONG: return 6;
	case TY_INT128: case TY_UINT128: return 7;
	case TY_BITINT: case TY_UBITINT: return 8;
	case TY_ENUM: return rank(c, T(c, t)->base);
	default: return 0;
	}
}

VPRIV qty prim(int k) { return QT(k, 0); }

VPRIV qty unsigned_of(cc *c, qty t)
{
	if (is_int_kind(KIND(c, t)) && T(c, t)->n && !(T(c, t)->flags & TF_ATOMIC))
		return ty_bitint(c, T(c, t)->n, true);
	switch (KIND(c, t)) {
	case TY_CHAR: case TY_SCHAR: return prim(TY_UCHAR);
	case TY_SHORT: return prim(TY_USHORT);
	case TY_INT: return prim(TY_UINT);
	case TY_LONG: return prim(TY_ULONG);
	case TY_LLONG: return prim(TY_ULLONG);
	case TY_INT128: return prim(TY_UINT128);
	case TY_ENUM: return unsigned_of(c, T(c, t)->base);
	default: return QUNQ(t);
	}
}

/* the result of comparing vectors of type v: signed integers of the lane size */
VPRIV qty vec_mask_type(cc *c, qty v)
{
	if (is_bool_vector(c, v)) return QUNQ(v);
	uint32_t es = (uint32_t)T(c, T(c, v)->base)->size;
	int k = es == 1 ? TY_SCHAR : es == 2 ? TY_SHORT : es == 4 ? TY_INT : es == 8 ? CC_I64(c, false) : TY_INT128;

	return ty_vector(c, prim(k), (uint32_t)T(c, v)->size);
}

/* integer promotion (bit-fields are handled by the caller) */
VPRIV qty promote(cc *c, qty t)
{
	int k = KIND(c, t);

	if (k == TY_ENUM)
		return promote(c, T(c, t)->base);
	if (is_int_kind(k) && !T(c, t)->n && rank(c, t) < 4)
		return prim(TY_INT);
	return QT(T(c, t)->canon, 0);
}

VPRIV qty arith_conv(cc *c, qty a, qty b)
{
	int ka, kb;

	if (is_float(c, a) || is_float(c, b)) {
		ka = is_float(c, a) ? KIND(c, a) : 0;
		kb = is_float(c, b) ? KIND(c, b) : 0;
		return prim(ka > kb ? ka : kb);
	}
	a = promote(c, a);
	b = promote(c, b);
	if (a == b)
		return a;
	{
		bool sa = is_signed(c, a), sb = is_signed(c, b);
		int ra = rank(c, a), rb = rank(c, b);

		if (T(c, a)->n || T(c, b)->n) {
			ra = (int)int_bits(c, a) * 16 + (T(c, a)->n ? 0 : ra);
			rb = (int)int_bits(c, b) * 16 + (T(c, b)->n ? 0 : rb);
		}

		if (sa == sb)
			return ra >= rb ? a : b;
		if (!sa && ra >= rb)
			return a;
		if (!sb && rb >= ra)
			return b;
		/* signed operand has greater rank */
		if (sa && int_bits(c, a) > int_bits(c, b))
			return a;
		if (sb && int_bits(c, b) > int_bits(c, a))
			return b;
		return unsigned_of(c, sa ? a : b);
	}
}

static bool compatible_q(cc *c, qty a, qty b)
{
	return QQ(a) == QQ(b) && compatible(c, a, b);
}

/* GNU transparent unions accept a member type in a function prototype.
   This does not make the union and that member compatible as object types. */
static bool compatible_param(cc *c, qty a, qty b)
{
	if (compatible(c, a, b)) return true;
	const ctype *x = T(c, canon(c, a)), *y = T(c, canon(c, b));
	if (x->kind == TY_UNION && y->kind == TY_UNION) return false;
	if (y->kind == TY_UNION) {
		x = y;
		b = a;
	}
	if (x->kind != TY_UNION || !(x->flags & TF_TRANSPARENT)) return false;
	const tag *r = &c->tags[x->aux];
	for (uint32_t i = 0; i < r->nfield; i++)
		if (compatible(c, r->f[i].ty, b)) return true;
	return false;
}

VPRIV bool compatible(cc *c, qty a, qty b)
{
	const ctype *x, *y;

	a = canon(c, a), b = canon(c, b);
	if (QI(a) == QI(b))
		return true;
	x = T(c, a), y = T(c, b);
	if ((x->flags ^ y->flags) & TF_ATOMIC)
		return false;
	if (x->flags & TF_ATOMIC)
		return compatible(c, ty_unatomic(c, a), ty_unatomic(c, b));
	/* an enum is compatible with its underlying type */
	if (x->kind == TY_ENUM && y->kind != TY_ENUM)
		return compatible(c, x->base, b);
	if (y->kind == TY_ENUM && x->kind != TY_ENUM)
		return compatible(c, a, y->base);
	if (x->kind != y->kind)
		return false;
	if (is_int_kind(x->kind) && x->n != y->n)
		return false;
	switch (x->kind) {
	case TY_PTR:
		return (x->flags & TF_BLOCK) == (y->flags & TF_BLOCK) && compatible_q(c, x->base, y->base);
	case TY_ARRAY:
		if (!compatible_q(c, x->base, y->base))
			return false;
		if ((x->flags | y->flags) & TF_INCOMPLETE)
			return true;
		if ((x->flags | y->flags) & TF_VLA) {
			/* A constant outer bound of an array of VLAs still participates
			 * in compatibility. Only genuinely variable bounds are ignored. */
			cval xv, yv;
			uint64_t xn = arr_len(c, a), yn = arr_len(c, b);

			if (x->flags & TF_VLA) {
				if (!x->n || !eval(c, x->n, &xv) || xv.kind != CV_INT || !xv.ice)
					return true;
				xn = (uint64_t)xv.i;
			}
			if (y->flags & TF_VLA) {
				if (!y->n || !eval(c, y->n, &yv) || yv.kind != CV_INT || !yv.ice)
					return true;
				yn = (uint64_t)yv.i;
			}
			return xn == yn;
		}
		return x->n == y->n && x->aux == y->aux;
	case TY_FUNC:
		if (!compatible(c, x->base, y->base) || QQ(x->base) != QQ(y->base) || x->ext != y->ext)
			return false;	/* (ext: the x86-64 calling convention, ms_abi/sysv_abi) */
		if ((x->flags | y->flags) & TF_NOPROTO)
			return true;
		if (x->n != y->n || (x->flags & TF_VARIADIC) != (y->flags & TF_VARIADIC))
			return false;
		for (uint32_t i = 0; i < x->n; i++)
			if (!compatible_param(c, c->params[x->aux + i], c->params[y->aux + i]))
				return false;
		return true;
	default:
		return false;
	}
}

/* composite type of two compatible types (redeclarations, ?: on pointers) */
VPRIV qty composite(cc *c, qty a, qty b)
{
	const ctype *x = T(c, a), *y = T(c, b);

	if ((x->flags & y->flags) & TF_ATOMIC)
		return qualify(c, ty_atomic(c, composite(c, ty_unatomic(c, a), ty_unatomic(c, b)), 0), QQ(a));

	if (x->kind == TY_ARRAY && y->kind == TY_ARRAY) {
		qty e = composite(c, x->base, y->base);

		if (!(x->flags & (TF_INCOMPLETE | TF_VLA)))
			return QT(QI(ty_array(c, e, arr_len(c, a), 0)), QQ(a));
		if (!(y->flags & (TF_INCOMPLETE | TF_VLA)))
			return QT(QI(ty_array(c, e, arr_len(c, b), 0)), QQ(a));
		return a;
	}
	if (x->kind == TY_FUNC && y->kind == TY_FUNC) {
		if (x->flags & TF_NOPROTO)
			return b;
		return a;
	}
	if (x->kind == TY_PTR && y->kind == TY_PTR)
		return QT(QI(ty_ptr(c, composite(c, x->base, y->base))), QQ(a));
	return a;
}
