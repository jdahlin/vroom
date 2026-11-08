/*
 * vcc front end: parser + sema (see cc.h).
 *
 * One pass, recursive descent, pulling macro-expanded tokens from pp.c.
 * Types, records, symbols and nodes are 32-bit indices into per-TU arrays
 * that are reserved (not committed) up front, so nothing is reallocated or
 * copied while parsing.  Semantic analysis is done as each construct is
 * parsed: expression types, implicit conversions (explicit N_CAST nodes),
 * lvalue-ness, and constant evaluation on demand.
 *
 * clang is the reference for every rule that is not plain C11: GNU
 * extensions, record layout (ported from ItaniumRecordLayoutBuilder for
 * the AArch64 target), integer constant expressions vs. foldable ones (null
 * pointer constants depend on the difference: __is_constexpr), and the
 * types of builtins.
 */
#include <pthread.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "ccint.h"
#include "emit.h"
#include "ccsnap.h"
#include "arena.h"

/* the per-TU tables, mapped once and reused (cc_init takes a set, cc_free gives it back) */
static vmpool ccpool = VMPOOL_INIT(16);
static _Thread_local vmreg *ccr;

static void *halloc(cc *c, size_t n)
{
	char *r;

	n = (n + 15) & ~(size_t)15;
	if (unlikely(c->hp + n > c->hend)) {
		fputs("vcc: out of memory\n", stderr);
		abort();
	}
	r = c->hp;
	c->hp += n;
	return r;
}

/* ---------------- diagnostics ---------------- */

static void diag(cc *c, uint32_t loc, const char *what, const char *fmt, va_list ap)
{
	c->ndiag++;
	pp_vdiag(c->p, loc, what, fmt, ap);
}

__attribute__((format(printf, 3, 4)))
static void error(cc *c, uint32_t loc, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	diag(c, loc, "error", fmt, ap);
	va_end(ap);
	if (++c->errors >= 20) {
		va_start(ap, fmt);
		pp_vdiag(c->p, loc, "fatal error", "too many errors emitted, stopping now", ap);
		va_end(ap);
	}
}

__attribute__((format(printf, 3, 4), noreturn))
static void fatal(cc *c, uint32_t loc, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	diag(c, loc, "error", fmt, ap);
	va_end(ap);
	c->errors++;
	longjmp(c->bail, 1);
}

static const char *idname(uint32_t id, int *len)
{
	*len = (int)vintern_ents[id].len;
	return vintern_ents[id].s;
}

#define IDF(id) idlen_(id), idstr_(id)
static int idlen_(uint32_t id) { return (int)vintern_ents[id].len; }
static const char *idstr_(uint32_t id) { return vintern_ents[id].s; }

/* ---------------- types ---------------- */

static const struct { uint8_t size, align; } basic[TY_PTR] = {
	[TY_VOID] = { 1, 1 }, [TY_BOOL] = { 1, 1 },
	[TY_CHAR] = { 1, 1 }, [TY_SCHAR] = { 1, 1 }, [TY_UCHAR] = { 1, 1 },
	[TY_SHORT] = { 2, 2 }, [TY_USHORT] = { 2, 2 },
	[TY_INT] = { 4, 4 }, [TY_UINT] = { 4, 4 },
	[TY_LONG] = { 8, 8 }, [TY_ULONG] = { 8, 8 },
	[TY_LLONG] = { 8, 8 }, [TY_ULLONG] = { 8, 8 },
	[TY_INT128] = { 16, 16 }, [TY_UINT128] = { 16, 16 },
	[TY_FLOAT16] = { 2, 2 }, [TY_FLOAT] = { 4, 4 }, [TY_DOUBLE] = { 8, 8 },
	[TY_LDOUBLE] = { 16, 16 },
};

#define T(c, t) (&(c)->ty[QI(t)])
#define KIND(c, t) ((c)->ty[QI(t)].kind)

static inline bool is_int_kind(int k) { return k >= TY_BOOL && k <= TY_UINT128; }
static inline bool is_flt_kind(int k) { return k >= TY_FLOAT16 && k <= TY_LDOUBLE; }
static bool is_integer(cc *c, qty t) { int k = KIND(c, t); return is_int_kind(k) || k == TY_ENUM; }
static bool is_float(cc *c, qty t) { return is_flt_kind(KIND(c, t)); }
static bool is_arith(cc *c, qty t) { return is_integer(c, t) || is_float(c, t); }
static bool is_ptr(cc *c, qty t) { return KIND(c, t) == TY_PTR; }
static bool is_scalar(cc *c, qty t) { return is_arith(c, t) || is_ptr(c, t); }
static bool is_void(cc *c, qty t) { return KIND(c, t) == TY_VOID; }
static bool is_array(cc *c, qty t) { return KIND(c, t) == TY_ARRAY; }
static bool is_func(cc *c, qty t) { return KIND(c, t) == TY_FUNC; }
static bool is_record(cc *c, qty t) { int k = KIND(c, t); return k == TY_STRUCT || k == TY_UNION; }
static bool is_vector(cc *c, qty t) { return KIND(c, t) == TY_VECTOR; }
/* initialized by a list of elements */
static bool is_ilagg(cc *c, qty t) { int k = KIND(c, t); return k == TY_STRUCT || k == TY_UNION || k == TY_ARRAY || k == TY_VECTOR; }

static qty canon(cc *c, qty t) { return QT(T(c, t)->canon, QQ(t)); }

static bool is_signed(cc *c, qty t)
{
	int k = KIND(c, t);

	if (k == TY_ENUM)
		return is_signed(c, T(c, t)->base);
	if (k == TY_CHAR)
		return !c->o->unsigned_char;
	return k == TY_SCHAR || k == TY_SHORT || k == TY_INT || k == TY_LONG || k == TY_LLONG || k == TY_INT128;
}

static uint32_t ty_hash(const ctype *t)
{
	uint64_t h = (uint64_t)t->kind * 0x9E3779B97F4A7C15ull;

	h ^= t->base + 0x632BE59BD9B4E019ull + (h << 6) + (h >> 2);
	h ^= t->n + 0x8CB92BA72F3D8DD7ull + (h << 6) + (h >> 2);
	h ^= t->aux + (uint64_t)t->flags * 31 + t->align * 131 + (h << 6) + (h >> 2);
	return (uint32_t)(h ^ (h >> 32));
}

static bool ty_eq(const ctype *a, const ctype *b)
{
	return a->kind == b->kind && a->flags == b->flags && a->base == b->base && a->n == b->n &&
	       a->aux == b->aux && a->align == b->align && a->size == b->size;
}

/* intern a type (records and enums are created with ty_new) */
static uint32_t ty_intern(cc *c, const ctype *t)
{
	uint32_t h = ty_hash(t), m = c->tyhcap - 1;

	for (uint32_t i = h & m;; i = (i + 1) & m) {
		uint32_t e = c->tyhash[i];

		if (!e) {
			uint32_t idx = c->nty++;

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
		if (ty_eq(&c->ty[e], t))
			return e;
	}
}

static uint32_t ty_new(cc *c, int kind)
{
	uint32_t idx = c->nty++;

	memset(&c->ty[idx], 0, sizeof(ctype));
	c->ty[idx].kind = (uint8_t)kind;
	c->ty[idx].canon = idx;
	return idx;
}

static qty ty_ptr(cc *c, qty base)
{
	ctype t = { .kind = TY_PTR, .size = 8, .align = 8, .base = base };

	return QT(ty_intern(c, &t), 0);
}

static qty ty_array(cc *c, qty elem, uint64_t n, int flags)
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

static uint64_t arr_len(cc *c, qty t)
{
	const ctype *a = T(c, t);

	return a->n | (uint64_t)a->aux << 32;
}

static qty ty_vla(cc *c, qty elem, uint32_t sizenode)
{
	ctype t = { .kind = TY_ARRAY, .flags = TF_VLA, .base = elem, .align = T(c, elem)->align, .n = sizenode };

	return QT(ty_intern(c, &t), 0);
}

static uint32_t fn_hash(qty ret, const qty *par, uint32_t n, int flags)
{
	uint32_t h = (ret * 0x9E3779B1u) ^ (n * 0x85EBCA77u) ^ (uint32_t)flags;

	for (uint32_t i = 0; i < n; i++)
		h = (h ^ par[i]) * 0x01000193u;
	return h ^ (h >> 15);
}

/* function types have their own table: the key includes the parameter list */
/* add qualifiers; on an array type they belong to the element type (C11 6.7.3p9) */
static qty qualify(cc *c, qty t, int q)
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

static qty ty_func(cc *c, qty ret, const qty *par, uint32_t n, int flags)
{
	uint32_t m = c->fnhcap - 1;

	for (uint32_t i = fn_hash(ret, par, n, flags) & m;; i = (i + 1) & m) {
		uint32_t e = c->fnhash[i];

		if (!e) {
			uint32_t idx = ty_new(c, TY_FUNC);
			ctype *t = &c->ty[idx];

			t->flags = (uint8_t)flags, t->base = ret, t->n = n, t->size = 1, t->align = 4;
			t->aux = c->nparams;
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
					for (k = fn_hash(f->base, &c->params[f->aux], f->n, f->flags) & (ncap - 1); nh[k];
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

			if (x->base == ret && x->n == n && x->flags == flags &&
			    !memcmp(&c->params[x->aux], par, n * sizeof(qty)))
				return QT(e, 0);
		}
	}
}

/* GNU vector of total size bytes with elements elem */
static qty ty_vector(cc *c, qty elem, uint32_t size)
{
	const ctype *e = T(c, elem);
	ctype t = { .kind = TY_VECTOR, .base = QUNQ(elem), .size = size, .align = size > 16 ? 16 : size };

	t.n = e->size ? (uint32_t)(size / e->size) : 0;
	return QT(ty_intern(c, &t), 0);
}

/* aligned typedef variant: same type, other alignment */
static qty ty_aligned(cc *c, qty t, uint32_t align)
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

static uint64_t ty_size(cc *c, qty t) { return T(c, t)->size; }
static uint32_t ty_align(cc *c, qty t) { return T(c, t)->align; }

static bool complete(cc *c, qty t)
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
static int rank(cc *c, qty t)
{
	switch (KIND(c, t)) {
	case TY_BOOL: return 1;
	case TY_CHAR: case TY_SCHAR: case TY_UCHAR: return 2;
	case TY_SHORT: case TY_USHORT: return 3;
	case TY_INT: case TY_UINT: return 4;
	case TY_LONG: case TY_ULONG: return 5;
	case TY_LLONG: case TY_ULLONG: return 6;
	case TY_INT128: case TY_UINT128: return 7;
	case TY_ENUM: return rank(c, T(c, t)->base);
	default: return 0;
	}
}

static qty prim(int k) { return QT(k, 0); }

static qty unsigned_of(cc *c, qty t)
{
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

/* integer promotion (bit-fields are handled by the caller) */
static qty promote(cc *c, qty t)
{
	int k = KIND(c, t);

	if (k == TY_ENUM)
		return promote(c, T(c, t)->base);
	if (is_int_kind(k) && rank(c, t) < 4)
		return prim(TY_INT);
	return QT(T(c, t)->canon, 0);
}

static qty arith_conv(cc *c, qty a, qty b)
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

		if (sa == sb)
			return ra >= rb ? a : b;
		if (!sa && ra >= rb)
			return a;
		if (!sb && rb >= ra)
			return b;
		/* signed operand has greater rank */
		if (sa && ty_size(c, a) > ty_size(c, b))
			return a;
		if (sb && ty_size(c, b) > ty_size(c, a))
			return b;
		return unsigned_of(c, sa ? a : b);
	}
}

/* C11 6.2.7 compatibility of unqualified canonical types */
static bool compatible(cc *c, qty a, qty b);

static bool compatible_q(cc *c, qty a, qty b)
{
	return QQ(a) == QQ(b) && compatible(c, a, b);
}

static bool compatible(cc *c, qty a, qty b)
{
	const ctype *x, *y;

	a = canon(c, a), b = canon(c, b);
	if (QI(a) == QI(b))
		return true;
	x = T(c, a), y = T(c, b);
	/* an enum is compatible with its underlying type */
	if (x->kind == TY_ENUM && y->kind != TY_ENUM)
		return compatible(c, x->base, b);
	if (y->kind == TY_ENUM && x->kind != TY_ENUM)
		return compatible(c, a, y->base);
	if (x->kind != y->kind)
		return false;
	switch (x->kind) {
	case TY_PTR:
		return (x->flags & TF_BLOCK) == (y->flags & TF_BLOCK) && compatible_q(c, x->base, y->base);
	case TY_ARRAY:
		if (!compatible_q(c, x->base, y->base))
			return false;
		if ((x->flags | y->flags) & (TF_INCOMPLETE | TF_VLA))
			return true;
		return x->n == y->n && x->aux == y->aux;
	case TY_FUNC:
		if (!compatible(c, x->base, y->base) || QQ(x->base) != QQ(y->base))
			return false;
		if ((x->flags | y->flags) & TF_NOPROTO)
			return true;
		if (x->n != y->n || (x->flags & TF_VARIADIC) != (y->flags & TF_VARIADIC))
			return false;
		for (uint32_t i = 0; i < x->n; i++)
			if (!compatible(c, c->params[x->aux + i], c->params[y->aux + i]))
				return false;
		return true;
	default:
		return false;
	}
}

/* composite type of two compatible types (redeclarations, ?: on pointers) */
static qty composite(cc *c, qty a, qty b)
{
	const ctype *x = T(c, a), *y = T(c, b);

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

/* ---------------- record layout (clang ItaniumRecordLayoutBuilder, AArch64) ---------------- */

static uint64_t align_to(uint64_t v, uint64_t a) { return a ? (v + a - 1) / a * a : v; }

typedef struct layout {
	bool is_union, packed;
	uint64_t max_field_align;	/* bits; #pragma pack */
	uint64_t data_size, size;	/* bits */
	uint64_t unfilled;		/* bits unfilled in the last bit-field unit */
	uint64_t align, unpacked_align;	/* bits */
	uint64_t padded_field_size;
} layout;

static void lay_update(layout *l, uint64_t a, uint64_t ua)
{
	if (a > l->align)
		l->align = a;
	if (ua > l->unpacked_align)
		l->unpacked_align = ua;
}

/* field attrs: aligned (bytes), packed */
static void lay_bitfield(cc *c, layout *l, field *f, uint32_t fattr_align, bool fpacked)
{
	bool packed = l->packed || fpacked;
	uint64_t fsize = f->width;
	uint64_t unit = ty_size(c, f->ty) * 8;
	uint64_t falign = (uint64_t)ty_align(c, f->ty) * 8;
	uint64_t off, ufalign, explicit_align = (uint64_t)fattr_align * 8;
	uint64_t ufoff;
	uint64_t mfa = l->max_field_align;

	off = l->is_union ? 0 : l->data_size - l->unfilled;
	/* AArch64: useBitFieldTypeAlignment and useZeroLengthBitfieldAlignment */
	ufalign = falign;
	if (packed && fsize != 0)
		falign = 8;
	if (explicit_align) {
		falign = falign > explicit_align ? falign : explicit_align;
		ufalign = ufalign > explicit_align ? ufalign : explicit_align;
	}
	if (mfa && fsize) {
		ufalign = ufalign < mfa ? ufalign : mfa;
		if (packed)
			falign = ufalign;
		else
			falign = falign < mfa ? falign : mfa;
	}
	ufoff = off;
	{
		bool allow_pad = mfa == 0;

		if (fsize == 0 || (allow_pad && (off & (falign - 1)) + fsize > unit))
			off = align_to(off, falign);
		if (fsize == 0 || (allow_pad && (ufoff & (ufalign - 1)) + fsize > unit))
			ufoff = align_to(ufoff, ufalign);
	}
	f->off = off;
	if (l->is_union) {
		uint64_t rs = align_to(fsize, 8);

		if (rs > l->data_size)
			l->data_size = rs;
	} else {
		uint64_t ns = off + fsize;

		l->data_size = align_to(ns, 8);
		l->unfilled = l->data_size - ns;
	}
	if (l->data_size > l->size)
		l->size = l->data_size;
	lay_update(l, falign, ufalign);
}

static void lay_field(cc *c, layout *l, field *f, uint32_t fattr_align, bool fpacked)
{
	uint64_t off, fsize, falign, ualign, palign = 8;
	bool packed;

	if (f->bitfield) {
		lay_bitfield(c, l, f, fattr_align, fpacked);
		return;
	}
	off = l->is_union ? 0 : l->data_size;
	l->unfilled = 0;
	falign = (uint64_t)ty_align(c, f->ty) * 8;
	fsize = is_array(c, f->ty) && (T(c, f->ty)->flags & TF_INCOMPLETE) ? 0 : ty_size(c, f->ty) * 8;
	packed = l->packed || fpacked;
	ualign = falign;
	if (fattr_align) {
		uint64_t m = (uint64_t)fattr_align * 8;

		palign = palign > m ? palign : m;
		ualign = ualign > m ? ualign : m;
	}
	if (l->max_field_align) {
		palign = palign < l->max_field_align ? palign : l->max_field_align;
		ualign = ualign < l->max_field_align ? ualign : l->max_field_align;
	}
	falign = packed ? palign : ualign;
	off = align_to(off, falign);
	f->off = off;
	if (l->is_union) {
		if (fsize > l->data_size)
			l->data_size = fsize;
	} else {
		l->data_size = off + fsize;
	}
	if (off + fsize > l->padded_field_size)
		l->padded_field_size = off + fsize;
	if (l->data_size > l->size)
		l->size = l->data_size;
	lay_update(l, falign, ualign);
}

/* fa[i]/fp[i]: per-field aligned() and packed attributes */
static void lay_record(cc *c, tag *r, const uint32_t *fa, const uint8_t *fp)
{
	layout l = { .is_union = (r->flags & RF_UNION) != 0, .packed = (r->flags & RF_PACKED) != 0,
		     .align = 8, .unpacked_align = 8 };

	if (c->npack && c->pack[c->npack - 1])
		l.max_field_align = (uint64_t)c->pack[c->npack - 1] * 8;
	if (r->maxalign)
		lay_update(&l, (uint64_t)r->maxalign * 8, (uint64_t)r->maxalign * 8);
	for (uint32_t i = 0; i < r->nfield; i++)
		lay_field(c, &l, &r->f[i], fa ? fa[i] : 0, fp ? fp[i] : 0);
	if (l.padded_field_size > l.size)
		l.size = l.padded_field_size;
	l.size = align_to(l.size, l.align);
	r->size = l.size / 8;
	r->align = (uint32_t)(l.align / 8);
	c->ty[r->ty].size = r->size;
	c->ty[r->ty].align = r->align;
}

/* ---------------- tokens ---------------- */

static bool c_keyword[KW_END];

static void init_keywords(void)
{
	for (int i = KW_auto; i <= KW_thread_local; i++)
		c_keyword[i] = true;
	/* C23 keywords that are plain identifiers in gnu11 */
	c_keyword[KW_bool] = c_keyword[KW_true] = c_keyword[KW_false] = false;
	c_keyword[KW_nullptr] = c_keyword[KW_alignof] = c_keyword[KW_alignas] = false;
	c_keyword[KW_thread_local] = c_keyword[KW_static_assert] = false;
	c_keyword[KW_typeof_unqual] = false;
}

static inline int kwof(const ptok *t)
{
	return t->kind == T_IDENT && t->id < KW_END && c_keyword[t->id] ? (int)t->id : 0;
}

static void pragma(cc *c, const ptok *t);

static void fetch(cc *c, ptok *t)
{
	for (;;) {
		pp_next(c->p, t);
		if (likely(t->kind != P_DIRECTIVE))
			return;
		if (unlikely(dc_stale(c)))
			dc_materialize(c);	/* (the pragma acts on the real state) */
		pragma(c, t);
	}
}

void cc_fetch(cc *c, ptok *t) { fetch(c, t); }

static inline ptok *tok(cc *c) { return &c->tk[0]; }

/* clang's nullability qualifiers: no meaning for code generation */
static inline bool is_nullability(const cc *c, const ptok *t)
{
	return t->kind == T_IDENT && (t->id == c->id_null[0] || t->id == c->id_null[1] ||
				      t->id == c->id_null[2] || t->id == c->id_null[3]);
}

static ptok *peek(cc *c, int k)
{
	while (c->ntk <= k)
		fetch(c, &c->tk[c->ntk++]);
	return &c->tk[k];
}

static inline void next(cc *c)
{
	c->prevloc = c->tk[0].loc;
	if (c->ntk > 1) {
		memmove(&c->tk[0], &c->tk[1], (size_t)(c->ntk - 1) * sizeof(ptok));
		c->ntk--;
	} else {
		fetch(c, &c->tk[0]);
	}
}

static inline bool is(cc *c, int kind) { return c->tk[0].kind == kind; }
static inline bool iskw(cc *c, int kw) { return kwof(&c->tk[0]) == kw; }

static bool accept(cc *c, int kind)
{
	if (c->tk[0].kind != kind)
		return false;
	next(c);
	return true;
}

static const char *tokspell(const ptok *t, char *buf, size_t cap)
{
	size_t n;
	char tmp[t->len + 1];
	const char *s;

	if (t->kind == T_EOF)
		return "end of file";
	s = pp_spell(t, tmp, &n);
	if (n >= cap)
		n = cap - 1;
	memcpy(buf, s, n);
	buf[n] = 0;
	return buf;
}

static void expect(cc *c, int kind, const char *what)
{
	if (likely(c->tk[0].kind == kind)) {
		next(c);
		return;
	}
	{
		char b[64];

		fatal(c, c->tk[0].loc, "expected %s before '%s'", what, tokspell(&c->tk[0], b, sizeof(b)));
	}
}

/* #pragma: only pack changes semantics; the rest is ignored here */
static void pragma(cc *c, const ptok *t)
{
	const char *s = t->s, *e = t->s + t->len;
	int v = 0;

	if ((size_t)(e - s) < 11 || memcmp(s, "pragma", 6))
		return;
	s += 6;
	while (s < e && (*s == ' ' || *s == '\t'))
		s++;
	if ((size_t)(e - s) < 4 || memcmp(s, "pack", 4))
		return;
	s += 4;
	while (s < e && *s != '(')
		s++;
	if (s == e)
		return;
	s++;
	while (s < e && *s == ' ')
		s++;
	if (*s == ')') {
		c->pack[c->npack ? c->npack - 1 : 0] = 0;
		if (!c->npack)
			c->npack = 1;
		return;
	}
	if (!memcmp(s, "push", 4)) {
		s += 4;
		while (s < e && (*s == ' ' || *s == ','))
			s++;
		v = c->npack ? (int)c->pack[c->npack - 1] : 0;
		if (*s >= '0' && *s <= '9')
			v = atoi(s);
		if (c->npack < 63)
			c->pack[c->npack++] = (uint32_t)v;
		return;
	}
	if (!memcmp(s, "pop", 3)) {
		if (c->npack)
			c->npack--;
		return;
	}
	if (*s >= '0' && *s <= '9') {
		if (!c->npack)
			c->npack = 1;
		c->pack[c->npack - 1] = (uint32_t)atoi(s);
	}
}

/* ---------------- scopes and symbols ---------------- */

static uint32_t *dslot(cc *c, uint32_t **dir, uint32_t id)
{
	uint32_t **pg = &dir[id >> DIR_BITS];

	if (unlikely(!*pg)) {
		*pg = halloc(c, DIR_PAGE * sizeof(uint32_t));
		memset(*pg, 0, DIR_PAGE * sizeof(uint32_t));
	}
	return &(*pg)[id & (DIR_PAGE - 1)];
}

static inline uint32_t dget(uint32_t **dir, uint32_t id)
{
	uint32_t *pg = dir[id >> DIR_BITS];

	return pg ? pg[id & (DIR_PAGE - 1)] : 0;
}

/* ordinary identifiers and tags; the sym's kind tells which */
enum { S_TAG = 16 };

static void scope_open(cc *c)
{
	if (c->depth >= 4095)
		fatal(c, tok(c)->loc, "scopes nested too deeply");
	c->smark[++c->depth] = c->nsstack;
}

static void scope_close(cc *c)
{
	uint32_t m = c->smark[c->depth];

	while (c->nsstack > m) {
		uint32_t s = c->sstack[--c->nsstack];
		sym *y = &c->syms[s];

		*dslot(c, y->kind == S_TAG ? c->tdir : c->odir, y->name) = y->shadow;
	}
	c->depth--;
}

static uint32_t sym_new(cc *c, int kind, uint32_t name, qty ty, uint32_t loc)
{
	uint32_t i = c->nsym++;
	sym *s = &c->syms[i];

	memset(s, 0, sizeof(*s));
	s->kind = (uint8_t)kind;
	s->name = name;
	s->ty = ty;
	s->loc = loc;
	s->depth = c->depth;
	return i;
}

/* bind s in the current scope */
static void bind(cc *c, uint32_t s)
{
	sym *y = &c->syms[s];
	uint32_t *slot;

	if (!y->name)
		return;
	slot = dslot(c, y->kind == S_TAG ? c->tdir : c->odir, y->name);
	y->shadow = *slot;
	y->depth = c->depth;
	*slot = s;
	c->sstack[c->nsstack++] = s;
}

static uint32_t lookup(cc *c, uint32_t id) { return dget(c->odir, id); }
static uint32_t lookup_tag(cc *c, uint32_t id) { return dget(c->tdir, id); }

static bool is_typedef_name(cc *c, const ptok *t)
{
	uint32_t s;

	if (t->kind != T_IDENT || kwof(t))
		return false;
	s = lookup(c, t->id);
	return s && c->syms[s].kind == S_TYPEDEF;
}

/* ---------------- nodes ---------------- */

static uint32_t mk(cc *c, int op, qty ty, uint32_t loc)
{
	uint32_t i = c->nnode++;
	node *n = &c->nodes[i];

	memset(n, 0, sizeof(*n));
	n->op = (uint8_t)op;
	n->ty = ty;
	n->loc = loc;
	return i;
}

#define N(i) (&c->nodes[i])

static uint32_t mk2(cc *c, int op, qty ty, uint32_t loc, uint32_t a, uint32_t b)
{
	uint32_t i = mk(c, op, ty, loc);

	c->nodes[i].a = a;
	c->nodes[i].b = b;
	return i;
}

/* lists: append to a temporary vector, then freeze into c->lists */
typedef struct vec {
	uint32_t *v;
	uint32_t n, cap;
	uint32_t inl[16];
} vec;

static void vinit(vec *v) { v->v = v->inl, v->n = 0, v->cap = 16; }

static void vpush(cc *c, vec *v, uint32_t x)
{
	if (unlikely(v->n == v->cap)) {
		uint32_t *nv = halloc(c, v->cap * 2 * sizeof(uint32_t));

		memcpy(nv, v->v, v->n * sizeof(uint32_t));
		v->v = nv;
		v->cap *= 2;
	}
	v->v[v->n++] = x;
}

static uint32_t vfreeze(cc *c, const vec *v)
{
	uint32_t at = c->nlist;

	memcpy(&c->lists[at], v->v, v->n * sizeof(uint32_t));
	c->nlist += v->n;
	return at;
}

static uint32_t mk_int(cc *c, i128 v, qty ty, uint32_t loc)
{
	uint32_t n = mk(c, N_NUM, ty, loc);

	c->ipool[c->nipool] = v;
	c->nodes[n].a = c->nipool++;
	return n;
}

static uint32_t mk_flt(cc *c, double v, qty ty, uint32_t loc)
{
	uint32_t n = mk(c, N_FNUM, ty, loc);

	c->fpool[c->nfpool] = v;
	c->nodes[n].a = c->nfpool++;
	return n;
}

/* ---------------- literals ---------------- */

static uint32_t number(cc *c, const ptok *t)
{
	char tmp[t->len + 1];
	size_t n, i = 0;
	const char *s = pp_spell(t, tmp, &n);
	unsigned base = 10;
	u128 v = 0;
	bool over = false, u = false, isf = false;
	int l = 0;
	char buf[256];

	/* floating? */
	if (n > 1 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		for (size_t k = 2; k < n; k++)
			if (s[k] == '.' || s[k] == 'p' || s[k] == 'P')
				isf = true;
	} else {
		for (size_t k = 0; k < n; k++)
			if (s[k] == '.' || s[k] == 'e' || s[k] == 'E')
				isf = true;
	}
	if (isf) {
		size_t k = n;
		qty ty = prim(TY_DOUBLE);
		double d;

		while (k > 0 && (s[k - 1] == 'f' || s[k - 1] == 'F' || s[k - 1] == 'l' || s[k - 1] == 'L'))
			k--;
		if (k < n && (s[k] == 'f' || s[k] == 'F'))
			ty = prim(TY_FLOAT);
		else if (k < n)
			ty = prim(TY_LDOUBLE);
		if (k >= sizeof(buf))
			k = sizeof(buf) - 1;
		memcpy(buf, s, k);
		buf[k] = 0;
		d = strtod(buf, NULL);
		if (KIND(c, ty) == TY_FLOAT)
			d = (float)d;
		return mk_flt(c, d, ty, t->loc);
	}
	if (n > 1 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		base = 16, i = 2;
	else if (n > 1 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B'))
		base = 2, i = 2;
	else if (s[0] == '0')
		base = 8;
	for (; i < n; i++) {
		unsigned d;
		char ch = s[i];

		if (ch == '\'')
			continue;
		if (ch >= '0' && ch <= '9')
			d = (unsigned)(ch - '0');
		else if (base == 16 && ch >= 'a' && ch <= 'f')
			d = (unsigned)(ch - 'a' + 10);
		else if (base == 16 && ch >= 'A' && ch <= 'F')
			d = (unsigned)(ch - 'A' + 10);
		else
			break;
		if (d >= base) {
			error(c, t->loc, "invalid digit '%c' in %s constant", ch, base == 8 ? "octal" : "binary");
			break;
		}
		if (v > (UINT64_MAX - d) / base)
			over = true;
		v = v * base + d;
	}
	for (; i < n; i++) {
		char ch = s[i];

		if (ch == 'u' || ch == 'U')
			u = true;
		else if (ch == 'l' || ch == 'L')
			l++;
		else {
			error(c, t->loc, "invalid suffix '%.*s' on integer constant", (int)(n - i), s + i);
			break;
		}
	}
	if (over)
		error(c, t->loc, "integer literal is too large to be represented in any integer type");
	{
		qty ty;
		bool dec = base == 10;

		if (l == 0 && !u && v <= INT32_MAX)
			ty = prim(TY_INT);
		else if (l == 0 && (u || !dec) && v <= UINT32_MAX)
			ty = prim(TY_UINT);
		else if (l <= 1 && !u && v <= INT64_MAX)
			ty = prim(TY_LONG);
		else if (l <= 1 && (u || !dec))
			ty = prim(TY_ULONG);
		else if (!u && v <= INT64_MAX)
			ty = prim(TY_LLONG);
		else
			ty = l <= 1 ? prim(TY_ULONG) : prim(TY_ULLONG);
		return mk_int(c, (i128)v, ty, t->loc);
	}
}

/* one (possibly multi-byte UTF-8) escape or character of a literal */
static uint32_t lit_char(cc *c, const char **ps, const char *e, uint32_t loc, bool *wide_ok)
{
	const unsigned char *s = (const unsigned char *)*ps;
	uint32_t v;

	*wide_ok = true;
	if (*s != '\\') {
		if (*s < 0x80) {
			*ps = (const char *)s + 1;
			return *s;
		}
		/* UTF-8: decode (narrow literals store the bytes as they are) */
		*wide_ok = false;
		*ps = (const char *)s + 1;
		return *s;
	}
	s++;
	switch (*s) {
	case 'n': v = '\n'; s++; break;
	case 't': v = '\t'; s++; break;
	case 'r': v = '\r'; s++; break;
	case 'a': v = 7; s++; break;
	case 'b': v = 8; s++; break;
	case 'f': v = 12; s++; break;
	case 'v': v = 11; s++; break;
	case 'e': case 'E': v = 27; s++; break;
	case '\\': case '\'': case '"': case '?': v = *s++; break;
	case 'x':
		s++;
		v = 0;
		while ((const char *)s < e && ((*s >= '0' && *s <= '9') || ((*s | 32) >= 'a' && (*s | 32) <= 'f'))) {
			v = v * 16 + (uint32_t)(*s <= '9' ? *s - '0' : (*s | 32) - 'a' + 10);
			s++;
		}
		break;
	case 'u': case 'U': {
		int nd = *s == 'u' ? 4 : 8;

		s++;
		v = 0;
		for (int k = 0; k < nd && (const char *)s < e; k++, s++)
			v = v * 16 + (uint32_t)(*s <= '9' ? *s - '0' : (*s | 32) - 'a' + 10);
		*wide_ok = false;	/* caller encodes */
		*ps = (const char *)s;
		return v | 0x80000000u;	/* marks a code point */
	}
	default:
		if (*s >= '0' && *s <= '7') {
			v = 0;
			for (int k = 0; k < 3 && *s >= '0' && *s <= '7'; k++, s++)
				v = v * 8 + (uint32_t)(*s - '0');
		} else {
			error(c, loc, "unknown escape sequence '\\%c'", *s);
			v = *s++;
		}
	}
	*ps = (const char *)s;
	return v;
}

static int lit_esz(int kind, int target)
{
	switch (kind) {
	case T_WSTRING: case T_WCHAR: return target ? 2 : 4;
	case T_U16STRING: case T_U16CHAR: return 2;
	case T_U32STRING: case T_U32CHAR: return 4;
	default: (void)target; return 1;
	}
}

static size_t utf8_enc(uint32_t cp, char *o)
{
	if (cp < 0x80) { o[0] = (char)cp; return 1; }
	if (cp < 0x800) { o[0] = (char)(0xC0 | cp >> 6); o[1] = (char)(0x80 | (cp & 63)); return 2; }
	if (cp < 0x10000) {
		o[0] = (char)(0xE0 | cp >> 12); o[1] = (char)(0x80 | ((cp >> 6) & 63)); o[2] = (char)(0x80 | (cp & 63));
		return 3;
	}
	o[0] = (char)(0xF0 | cp >> 18); o[1] = (char)(0x80 | ((cp >> 12) & 63));
	o[2] = (char)(0x80 | ((cp >> 6) & 63)); o[3] = (char)(0x80 | (cp & 63));
	return 4;
}

static bool is_strtok(int k) { return k >= T_STRING && k <= T_U32STRING; }

/* adjacent string literals -> one N_STR of type char[N] (or wide) */
static uint32_t string_lit(cc *c)
{
	uint32_t loc = tok(c)->loc;
	int kind = T_STRING;
	size_t cap = 256, n = 0;
	char *buf = malloc(cap);
	vec parts;
	int esz;

	vinit(&parts);
	/* first pass: the literal kind (any prefixed one wins) */
	for (int k = 0;; k++) {
		ptok *t = peek(c, k);

		if (!is_strtok(t->kind))
			break;
		if (t->kind != T_STRING && t->kind != T_U8STRING)
			kind = t->kind;
		if (k >= 2) {	/* the lookahead buffer is small: just stop counting */
			break;
		}
	}
	esz = lit_esz(kind, c->o->short_wchar);
	while (is_strtok(tok(c)->kind)) {
		ptok *t = tok(c);
		char tmp[t->len + 1];
		size_t sl;
		const char *sp = pp_spell(t, tmp, &sl), *s = sp, *e = sp + sl - 1;

		if (t->kind != T_STRING && t->kind != T_U8STRING && t->kind != kind && esz != lit_esz(t->kind, c->o->short_wchar))
			error(c, t->loc, "unsupported non-standard concatenation of string literals");
		if (t->kind != T_STRING && t->kind != T_U8STRING && kind == T_STRING)
			kind = t->kind, esz = lit_esz(kind, c->o->short_wchar);
		while (*s != '"')
			s++;
		s++;
		while (s < e) {
			bool ok;
			uint32_t v = lit_char(c, &s, e, t->loc, &ok);

			if (n + 16 > cap)
				buf = realloc(buf, cap *= 2);
			if (esz == 1) {
				if (v & 0x80000000u)
					n += utf8_enc(v & 0x7fffffffu, buf + n);
				else
					buf[n++] = (char)v;
			} else {
				if (!ok && !(v & 0x80000000u)) {
					/* UTF-8 sequence in a wide literal: decode */
					const unsigned char *u = (const unsigned char *)s - 1;
					uint32_t cp = *u;
					int more = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : cp >= 0xC0 ? 1 : 0;

					cp &= more == 3 ? 7 : more == 2 ? 15 : more == 1 ? 31 : 0xff;
					for (int k = 0; k < more && s < e; k++, s++)
						cp = cp << 6 | ((unsigned char)*s & 63);
					v = cp;
				}
				v &= 0x7fffffffu;
				if (esz == 2 && v >= 0x10000) {
					uint16_t hi = (uint16_t)(0xD800 + ((v - 0x10000) >> 10)), lo = (uint16_t)(0xDC00 + ((v - 0x10000) & 0x3ff));

					memcpy(buf + n, &hi, 2), n += 2;
					memcpy(buf + n, &lo, 2), n += 2;
				} else if (esz == 2) {
					uint16_t w = (uint16_t)v;

					memcpy(buf + n, &w, 2), n += 2;
				} else {
					memcpy(buf + n, &v, 4), n += 4;
				}
			}
		}
		next(c);
	}
	memset(buf + n, 0, (size_t)esz);
	n += (size_t)esz;
	{
		char *keep = halloc(c, n);
		uint32_t si = c->nstr++;
		qty et;

		memcpy(keep, buf, n);
		free(buf);
		c->strs[si] = (cstr){ keep, (uint32_t)(n / (size_t)esz), (uint8_t)esz, (uint8_t)kind };
		et = esz == 1 ? prim(TY_CHAR) : kind == T_WSTRING ? c->t_wchar : kind == T_U16STRING ? c->t_char16 : c->t_char32;
		{
			uint32_t r = mk(c, N_STR, ty_array(c, et, n / (size_t)esz, 0), loc);

			c->nodes[r].a = si;
			c->nodes[r].flags = NF_LVALUE;
			return r;
		}
	}
}

static uint32_t char_lit(cc *c)
{
	ptok *t = tok(c);
	char tmp[t->len + 1];
	size_t sl;
	const char *s = pp_spell(t, tmp, &sl), *e = s + sl - 1;
	int kind = t->kind;
	i128 v = 0;
	int nch = 0;
	qty ty = prim(TY_INT);

	while (*s != '\'')
		s++;
	s++;
	while (s < e) {
		bool ok;
		uint32_t x = lit_char(c, &s, e, t->loc, &ok);

		if (kind == T_CHAR) {
			if (x & 0x80000000u) {
				char u[4];
				size_t k = utf8_enc(x & 0x7fffffffu, u);

				for (size_t j = 0; j < k; j++)
					v = (v << 8) | (unsigned char)u[j], nch++;
				continue;
			}
			v = (v << 8) | (x & 0xff);
			nch++;
		} else {
			if (!ok && !(x & 0x80000000u)) {
				const unsigned char *u = (const unsigned char *)s - 1;
				uint32_t cp = *u;
				int more = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : cp >= 0xC0 ? 1 : 0;

				cp &= more == 3 ? 7 : more == 2 ? 15 : more == 1 ? 31 : 0xff;
				for (int k = 0; k < more && s < e; k++, s++)
					cp = cp << 6 | ((unsigned char)*s & 63);
				x = cp;
			}
			v = x & 0x7fffffffu;
		}
	}
	if (kind == T_CHAR) {
		if (nch == 1)
			v = c->o->unsigned_char ? (uint8_t)v : (int8_t)v;
		else
			v = (int32_t)(uint32_t)v;
	} else if (kind == T_WCHAR) {
		ty = c->t_wchar;
		if (c->o->short_wchar)
			v = (uint16_t)v;
	} else if (kind == T_U16CHAR) {
		ty = c->t_char16;
		v = (uint16_t)v;
	} else if (kind == T_U32CHAR) {
		ty = c->t_char32;
	} else if (kind == T_U8CHAR) {
		ty = c->o->unsigned_char ? prim(TY_UCHAR) : prim(TY_CHAR);
		ty = prim(TY_UCHAR);
	}
	next(c);
	return mk_int(c, v, ty, t->loc);
}

/* ---------------- builtins ---------------- */


typedef struct builtin {
	const char *name;
	uint8_t id;
	uint8_t constexpr_;	/* folds when its arguments do (ICE if they are) */
	const char *sig;	/* return type then parameters; '.' variadic (see sigtype) */
} builtin;

static const builtin builtins[] = {
	{ "__builtin_constant_p", BI_CONSTANT_P, 1, "i." },
	{ "__builtin_expect", BI_EXPECT, 1, "lll" },
	{ "__builtin_expect_with_probability", BI_EXPECT_PROB, 1, "llld" },
	{ "__builtin_va_start", BI_VA_START, 0, "vV." },
	{ "__builtin_va_end", BI_VA_END, 0, "vV" },
	{ "__builtin_va_copy", BI_VA_COPY, 0, "vVV" },
	{ "__builtin_clz", BI_CLZ, 1, "iu" }, { "__builtin_clzl", BI_CLZL, 1, "iL" }, { "__builtin_clzll", BI_CLZLL, 1, "iW" },
	{ "__builtin_ctz", BI_CTZ, 1, "iu" }, { "__builtin_ctzl", BI_CTZL, 1, "iL" }, { "__builtin_ctzll", BI_CTZLL, 1, "iW" },
	{ "__builtin_ffs", BI_FFS, 1, "ii" }, { "__builtin_ffsl", BI_FFSL, 1, "il" }, { "__builtin_ffsll", BI_FFSLL, 1, "iw" },
	{ "__builtin_popcount", BI_POPCOUNT, 1, "iu" }, { "__builtin_popcountl", BI_POPCOUNTL, 1, "iL" },
	{ "__builtin_popcountll", BI_POPCOUNTLL, 1, "iW" },
	{ "__builtin_parity", BI_PARITY, 1, "iu" }, { "__builtin_parityl", BI_PARITYL, 1, "iL" },
	{ "__builtin_parityll", BI_PARITYLL, 1, "iW" },
	{ "__builtin_clrsb", BI_CLRSB, 1, "ii" }, { "__builtin_clrsbl", BI_CLRSBL, 1, "il" },
	{ "__builtin_clrsbll", BI_CLRSBLL, 1, "iw" },
	{ "__builtin_bswap16", BI_BSWAP16, 1, "hh" }, { "__builtin_bswap32", BI_BSWAP32, 1, "uu" },
	{ "__builtin_bswap64", BI_BSWAP64, 1, "WW" },
	{ "__builtin_bitreverse8", BI_BITREVERSE8, 1, "CC" }, { "__builtin_bitreverse16", BI_BITREVERSE16, 1, "hh" },
	{ "__builtin_bitreverse32", BI_BITREVERSE32, 1, "uu" }, { "__builtin_bitreverse64", BI_BITREVERSE64, 1, "WW" },
	{ "__builtin_unreachable", BI_UNREACHABLE, 0, "v" },
	{ "__builtin_trap", BI_TRAP, 0, "v" }, { "__builtin_debugtrap", BI_DEBUGTRAP, 0, "v" },
	{ "__builtin_return_address", BI_RETURN_ADDRESS, 0, "pu" },
	{ "__builtin_frame_address", BI_FRAME_ADDRESS, 0, "pu" },
	{ "__builtin_extract_return_addr", BI_EXTRACT_RETURN_ADDR, 0, "pp" },
	{ "__builtin_add_overflow", BI_ADD_OVERFLOW, 0, "b." },
	{ "__builtin_sub_overflow", BI_SUB_OVERFLOW, 0, "b." },
	{ "__builtin_mul_overflow", BI_MUL_OVERFLOW, 0, "b." },
	{ "__builtin_sadd_overflow", BI_ADD_OVERFLOW, 0, "bii." },
	{ "__builtin_saddl_overflow", BI_ADD_OVERFLOW, 0, "bll." },
	{ "__builtin_saddll_overflow", BI_ADD_OVERFLOW, 0, "bww." },
	{ "__builtin_uadd_overflow", BI_ADD_OVERFLOW, 0, "buu." },
	{ "__builtin_uaddl_overflow", BI_ADD_OVERFLOW, 0, "bLL." },
	{ "__builtin_uaddll_overflow", BI_ADD_OVERFLOW, 0, "bWW." },
	{ "__builtin_ssub_overflow", BI_SUB_OVERFLOW, 0, "bii." },
	{ "__builtin_ssubl_overflow", BI_SUB_OVERFLOW, 0, "bll." },
	{ "__builtin_ssubll_overflow", BI_SUB_OVERFLOW, 0, "bww." },
	{ "__builtin_usub_overflow", BI_SUB_OVERFLOW, 0, "buu." },
	{ "__builtin_usubl_overflow", BI_SUB_OVERFLOW, 0, "bLL." },
	{ "__builtin_usubll_overflow", BI_SUB_OVERFLOW, 0, "bWW." },
	{ "__builtin_smul_overflow", BI_MUL_OVERFLOW, 0, "bii." },
	{ "__builtin_smull_overflow", BI_MUL_OVERFLOW, 0, "bll." },
	{ "__builtin_smulll_overflow", BI_MUL_OVERFLOW, 0, "bww." },
	{ "__builtin_umul_overflow", BI_MUL_OVERFLOW, 0, "buu." },
	{ "__builtin_umull_overflow", BI_MUL_OVERFLOW, 0, "bLL." },
	{ "__builtin_umulll_overflow", BI_MUL_OVERFLOW, 0, "bWW." },
	{ "__builtin_addcb", BI_ADDC, 0, "CCCC." },
	{ "__builtin_addcs", BI_ADDC, 0, "hhhh." },
	{ "__builtin_addc", BI_ADDC, 0, "uuuu." },
	{ "__builtin_addcl", BI_ADDC, 0, "LLLL." },
	{ "__builtin_addcll", BI_ADDC, 0, "WWWW." },
	{ "__builtin_subcb", BI_SUBC, 0, "CCCC." },
	{ "__builtin_subcs", BI_SUBC, 0, "hhhh." },
	{ "__builtin_subc", BI_SUBC, 0, "uuuu." },
	{ "__builtin_subcl", BI_SUBC, 0, "LLLL." },
	{ "__builtin_subcll", BI_SUBC, 0, "WWWW." },
	{ "__builtin_object_size", BI_OBJECT_SIZE, 0, "zPi" },
	{ "__builtin_dynamic_object_size", BI_DYNAMIC_OBJECT_SIZE, 0, "zPi" },
	{ "__builtin_counted_by_ref", BI_COUNTED_BY_REF, 0, "p." },
	{ "__builtin_alloca", BI_ALLOCA, 0, "pz" },
	{ "__builtin_alloca_uninitialized", BI_ALLOCA_UNINIT, 0, "pz" },
	{ "__builtin_prefetch", BI_PREFETCH, 0, "vP." },
	{ "__builtin_assume_aligned", BI_ASSUME_ALIGNED, 0, "pPz." },
	{ "__builtin_assume", BI_ASSUME, 0, "vb" },
	{ "__builtin_classify_type", BI_CLASSIFY_TYPE, 1, "i." },
	{ "__builtin_is_constant_evaluated", BI_IS_CONSTANT_EVALUATED, 1, "b" },
	{ "__builtin_strlen", BI_STRLEN, 1, "zS" },
	{ "__builtin_huge_val", BI_HUGE_VAL, 1, "d" }, { "__builtin_huge_valf", BI_HUGE_VALF, 1, "f" },
	{ "__builtin_huge_vall", BI_HUGE_VAL, 1, "D" },
	{ "__builtin_inf", BI_INF, 1, "d" }, { "__builtin_inff", BI_INFF, 1, "f" }, { "__builtin_infl", BI_INF, 1, "D" },
	{ "__builtin_nan", BI_NAN, 1, "dS" }, { "__builtin_nanf", BI_NANF, 1, "fS" }, { "__builtin_nanl", BI_NAN, 1, "DS" },
	{ "__builtin_isnan", BI_ISNAN, 1, "i." }, { "__builtin_isinf", BI_ISINF, 1, "i." },
	{ "__builtin_isfinite", BI_ISFINITE, 1, "i." }, { "__builtin_signbit", BI_SIGNBIT, 1, "i." },
	{ "__builtin_fpclassify", BI_FPCLASSIFY, 1, "i." }, { "__builtin_isinf_sign", BI_ISINF_SIGN, 1, "i." },
	{ "__builtin_isnormal", BI_ISNORMAL, 1, "i." },
	{ "__builtin_isgreater", BI_ISGREATER, 1, "i." }, { "__builtin_isgreaterequal", BI_ISGREATEREQUAL, 1, "i." },
	{ "__builtin_isless", BI_ISLESS, 1, "i." }, { "__builtin_islessequal", BI_ISLESSEQUAL, 1, "i." },
	{ "__builtin_islessgreater", BI_ISLESSGREATER, 1, "i." }, { "__builtin_isunordered", BI_ISUNORDERED, 1, "i." },
	{ "__builtin_arm_rsr64", BI_ARM_RSR, 0, "WS" },
	/* library functions */
	{ "__builtin_memcpy", BI_LIB, 0, "ppPz" }, { "__builtin_memmove", BI_LIB, 0, "ppPz" },
	{ "__builtin_memset", BI_LIB, 0, "ppiz" }, { "__builtin_memcmp", BI_LIB, 0, "iPPz" },
	{ "__builtin_memchr", BI_LIB, 0, "pPiz" },
	{ "__builtin_strcmp", BI_LIB, 0, "iSS" }, { "__builtin_strncmp", BI_LIB, 0, "iSSz" },
	{ "__builtin_strcpy", BI_LIB, 0, "ssS" }, { "__builtin_strncpy", BI_LIB, 0, "ssSz" },
	{ "__builtin_strcat", BI_LIB, 0, "ssS" }, { "__builtin_strncat", BI_LIB, 0, "ssSz" },
	{ "__builtin_strchr", BI_LIB, 0, "sSi" }, { "__builtin_strrchr", BI_LIB, 0, "sSi" },
	{ "__builtin_strstr", BI_LIB, 0, "sSS" }, { "__builtin_strnlen", BI_LIB, 0, "zSz" },
	{ "__builtin_abort", BI_LIB, 0, "v" }, { "__builtin_exit", BI_LIB, 0, "vi" },
	{ "__builtin_malloc", BI_LIB, 0, "pz" }, { "__builtin_free", BI_LIB, 0, "vp" },
	{ "__builtin_calloc", BI_LIB, 0, "pzz" }, { "__builtin_realloc", BI_LIB, 0, "ppz" },
	{ "__builtin_printf", BI_LIB, 0, "iS." }, { "__builtin_sprintf", BI_LIB, 0, "isS." },
	{ "__builtin_snprintf", BI_LIB, 0, "iszS." }, { "__builtin_vsnprintf", BI_LIB, 0, "iszSV" },
	{ "__builtin_vsprintf", BI_LIB, 0, "isSV" }, { "__builtin_puts", BI_LIB, 0, "iS" },
	{ "__builtin_abs", BI_LIB, 1, "ii" }, { "__builtin_labs", BI_LIB, 1, "ll" }, { "__builtin_llabs", BI_LIB, 1, "ww" },
	{ "__builtin_fabs", BI_LIB, 1, "dd" }, { "__builtin_fabsf", BI_LIB, 1, "ff" }, { "__builtin_fabsl", BI_LIB, 1, "DD" },
	{ "__builtin_copysign", BI_LIB, 1, "ddd" }, { "__builtin_copysignf", BI_LIB, 1, "fff" },
	{ "__builtin_copysignl", BI_LIB, 1, "DDD" },
	{ "__builtin_sqrt", BI_LIB, 0, "dd" }, { "__builtin_sqrtf", BI_LIB, 0, "ff" },
	{ "__builtin_floor", BI_LIB, 0, "dd" }, { "__builtin_ceil", BI_LIB, 0, "dd" },
	{ "__builtin_fmax", BI_LIB, 0, "ddd" }, { "__builtin_fmin", BI_LIB, 0, "ddd" },
	{ "__builtin___memcpy_chk", BI_LIB, 0, "ppPzz" }, { "__builtin___memmove_chk", BI_LIB, 0, "ppPzz" },
	{ "__builtin___memset_chk", BI_LIB, 0, "ppizz" }, { "__builtin___strcpy_chk", BI_LIB, 0, "ssSz" },
	{ "__builtin___stpcpy_chk", BI_LIB, 0, "ssSz" }, { "__builtin___strcat_chk", BI_LIB, 0, "ssSz" },
	{ "__builtin___strncpy_chk", BI_LIB, 0, "ssSzz" }, { "__builtin___strncat_chk", BI_LIB, 0, "ssSzz" },
	{ "__builtin___stpncpy_chk", BI_LIB, 0, "ssSzz" }, { "__builtin___strlcpy_chk", BI_LIB, 0, "zsSzz" },
	{ "__builtin___strlcat_chk", BI_LIB, 0, "zsSzz" }, { "__builtin___memccpy_chk", BI_LIB, 0, "ppPizz" },
	{ "__builtin___sprintf_chk", BI_LIB, 0, "isizS." }, { "__builtin___snprintf_chk", BI_LIB, 0, "iszizS." },
	{ "__builtin___vsprintf_chk", BI_LIB, 0, "isizSV" }, { "__builtin___vsnprintf_chk", BI_LIB, 0, "iszizSV" },
	{ NULL, 0, 0, NULL }
};

/* name of a library builtin (sym val of an S_BUILTIN) */
const char *cc_builtin_name(int64_t i) { return builtins[i].name; }

static qty sigtype(cc *c, char ch)
{
	switch (ch) {
	case 'v': return prim(TY_VOID);
	case 'b': return prim(TY_BOOL);
	case 'C': return prim(TY_UCHAR);
	case 'h': return prim(TY_USHORT);
	case 'i': return prim(TY_INT);
	case 'u': return prim(TY_UINT);
	case 'l': return prim(TY_LONG);
	case 'L': return prim(TY_ULONG);
	case 'w': return prim(TY_LLONG);
	case 'W': return prim(TY_ULLONG);
	case 'z': return c->t_size;
	case 'd': return prim(TY_DOUBLE);
	case 'f': return prim(TY_FLOAT);
	case 'D': return prim(TY_LDOUBLE);
	case 'p': return ty_ptr(c, prim(TY_VOID));
	case 'P': return ty_ptr(c, QT(TY_VOID, Q_CONST));
	case 's': return ty_ptr(c, prim(TY_CHAR));
	case 'S': return ty_ptr(c, QT(TY_CHAR, Q_CONST));
	case 'V': return c->t_valist;
	default: abort();
	}
}

/* the function type of a builtin's signature */
static qty builtin_type(cc *c, const builtin *b)
{
	qty par[16];
	uint32_t n = 0;
	int fl = 0;

	for (const char *s = b->sig + 1; *s; s++) {
		if (*s == '.') {
			fl |= TF_VARIADIC;
			break;
		}
		par[n++] = sigtype(c, *s);
	}
	return ty_func(c, sigtype(c, b->sig[0]), par, n, fl);
}

/* ---------------- constant evaluation ---------------- */


static bool eval(cc *c, uint32_t n, cval *v);

static i128 trunc_to(cc *c, i128 v, qty t)
{
	int k = KIND(c, t);
	uint64_t sz;

	if (k == TY_BOOL)
		return v != 0;
	if (k == TY_PTR)
		return (i128)(uint64_t)v;
	sz = ty_size(c, t);
	if (sz >= 16)
		return is_signed(c, t) ? v : (i128)(u128)v;
	{
		unsigned bits = (unsigned)sz * 8;
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

static const field *node_field(cc *c, const node *x)
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

static int clz128(u128 x, unsigned bits)
{
	int n = 0;

	for (int b = (int)bits - 1; b >= 0 && !((x >> b) & 1); b--)
		n++;
	return n;
}

static bool eval_builtin(cc *c, const node *x, cval *v);

static bool eval(cc *c, uint32_t n, cval *v)
{
	const node *x = N(n);
	cval a, b;

	memset(v, 0, sizeof(*v));
	switch (x->op) {
	case N_NUM:
		v->kind = CV_INT;
		v->i = c->ipool[x->a];
		v->ice = true;
		return true;
	case N_FNUM:
		v->kind = CV_FLT;
		v->f = c->fpool[x->a];
		v->ice = true;	/* only as the operand of a cast; arithmetic clears it */
		return true;
	case N_VAR: {
		/* a const integer variable with a constant initializer folds (not an ICE) */
		const sym *s = &c->syms[x->a];

		if (s->kind == S_VAR && (QQ(s->ty) & (Q_CONST | Q_VOLATILE)) == Q_CONST && is_integer(c, s->ty) &&
		    s->body && eval(c, s->body, v) && v->kind == CV_INT) {
			v->ice = false;
			return true;
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
		case CK_UNION:
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
			if (a.kind != CV_INT)
				return false;
			v->i = trunc_to(c, a.i, to);
			return true;
		case CK_BOOL:
			v->kind = CV_INT;
			if (a.kind == CV_INT)
				v->i = a.i != 0;
			else if (a.kind == CV_FLT)
				v->i = a.f != 0, v->ice = is_flt_lit(c, x->a);
			else
				v->i = 1, v->ice = false;	/* address of an object is never null */
			v->sym = v->str = v->label = 0;
			return true;
		case CK_I2F:
			if (a.kind != CV_INT)
				return false;
			v->kind = CV_FLT;
			v->f = is_signed(c, N(x->a)->ty) ? (double)a.i : (double)(u128)a.i;
			if (KIND(c, to) == TY_FLOAT)
				v->f = (float)v->f;
			v->ice = false;
			return true;
		case CK_F2I:
			if (a.kind != CV_FLT)
				return false;
			v->kind = CV_INT;
			v->i = trunc_to(c, is_signed(c, to) ? (i128)a.f : (i128)(u128)a.f, to);
			v->ice = is_flt_lit(c, x->a);
			return true;
		case CK_F2F:
			if (a.kind != CV_FLT)
				return false;
			if (KIND(c, to) == TY_FLOAT)
				v->f = (float)a.f;
			v->ice = false;
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
			if (x->aux == T_MINUS)
				v->f = -a.f;
			else if (x->aux == T_EXCLAIM)
				v->kind = CV_INT, v->i = a.f == 0;
			return true;
		}
		if (a.kind == CV_ADDR) {
			if (x->aux == T_EXCLAIM) {
				v->kind = CV_INT, v->i = 0, v->ice = false, v->sym = v->str = v->label = 0;
				return true;
			}
			return false;
		}
		switch (x->aux) {
		case T_MINUS: v->i = trunc_to(c, -a.i, x->ty); break;
		case T_TILDE: v->i = trunc_to(c, ~a.i, x->ty); break;
		case T_EXCLAIM: v->i = a.i == 0; break;
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
				v->i += (op == T_MINUS ? -iv->i : iv->i) * (i128)es;
				return true;
			}
			if (op == T_MINUS && is_ptr(c, lt) && is_ptr(c, rt)) {
				if (a.kind != b.kind || a.sym != b.sym || a.str != b.str || a.label != b.label)
					return false;
				v->i = (a.i - b.i) / (i128)elem_size(c, lt);
				return true;
			}
			/* an address cast to a 64-bit integer, plus or minus an integer: a relocation */
			if ((op == T_PLUS || op == T_MINUS) && ty_size(c, x->ty) == 8 &&
			    ((a.kind == CV_ADDR && b.kind == CV_INT) || (op == T_PLUS && a.kind == CV_INT && b.kind == CV_ADDR))) {
				const cval *pv = a.kind == CV_ADDR ? &a : &b, *iv = a.kind == CV_ADDR ? &b : &a;
				i128 d = op == T_MINUS ? -iv->i : iv->i;

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
					if ((a.kind == CV_INT && a.i == 0) || (b.kind == CV_INT && b.i == 0)) {
						if (op == T_EQUALEQUAL) { v->i = 0; return true; }
						if (op == T_EXCLAIMEQ) { v->i = 1; return true; }
					}
					return false;
				}
			}
			switch (op) {
			case T_EQUALEQUAL: v->i = a.i == b.i; return true;
			case T_EXCLAIMEQ: v->i = a.i != b.i; return true;
			case T_LESS: v->i = (u128)a.i < (u128)b.i; return true;
			case T_LESSEQ: v->i = (u128)a.i <= (u128)b.i; return true;
			case T_GREATER: v->i = (u128)a.i > (u128)b.i; return true;
			case T_GREATEREQ: v->i = (u128)a.i >= (u128)b.i; return true;
			case T_AMP: case T_PIPE: case T_CARET: case T_PLUS: case T_MINUS:
				if (a.kind == CV_INT && b.kind == CV_INT)
					break;	/* integers cast from pointers */
				return false;
			default:
				return false;
			}
		}
		if (a.kind == CV_FLT || b.kind == CV_FLT) {
			double l = a.kind == CV_FLT ? a.f : (double)a.i, r = b.kind == CV_FLT ? b.f : (double)b.i;

			v->ice = false;
			switch (op) {
			case T_PLUS: v->kind = CV_FLT, v->f = l + r; break;
			case T_MINUS: v->kind = CV_FLT, v->f = l - r; break;
			case T_STAR: v->kind = CV_FLT, v->f = l * r; break;
			case T_SLASH: v->kind = CV_FLT, v->f = l / r; break;
			case T_LESS: v->i = l < r; break;
			case T_LESSEQ: v->i = l <= r; break;
			case T_GREATER: v->i = l > r; break;
			case T_GREATEREQ: v->i = l >= r; break;
			case T_EQUALEQUAL: v->i = l == r; break;
			case T_EXCLAIMEQ: v->i = l != r; break;
			default: return false;
			}
			if (v->kind == CV_FLT && KIND(c, x->ty) == TY_FLOAT)
				v->f = (float)v->f;
			return true;
		}
		{
			qty ot = N(x->a)->ty;	/* operands are converted to the common type */
			bool sg = is_signed(c, ot);
			i128 l = a.i, r = b.i;
			uint64_t bits = ty_size(c, ot) * 8;

			ok = true;
			switch (op) {
			case T_PLUS: v->i = l + r; break;
			case T_MINUS: v->i = l - r; break;
			case T_STAR: v->i = sg ? l * r : (i128)((u128)l * (u128)r); break;
			case T_SLASH:
			case T_PERCENT:
				if (r == 0)
					return false;
				if (sg)
					v->i = op == T_SLASH ? l / r : l % r;
				else
					v->i = (i128)(op == T_SLASH ? (u128)trunc_to(c, l, unsigned_of(c, ot)) / (u128)trunc_to(c, r, unsigned_of(c, ot))
								    : (u128)trunc_to(c, l, unsigned_of(c, ot)) % (u128)trunc_to(c, r, unsigned_of(c, ot)));
				break;
			case T_LESSLESS:
				if (r < 0 || (uint64_t)r >= ty_size(c, x->ty) * 8)
					return false;
				v->i = (i128)((u128)l << r);
				break;
			case T_GREATERGREATER:
				if (r < 0 || (uint64_t)r >= ty_size(c, x->ty) * 8)
					return false;
				v->i = is_signed(c, x->ty) ? l >> r : (i128)((u128)trunc_to(c, l, unsigned_of(c, x->ty)) >> r);
				break;
			case T_AMP: v->i = l & r; break;
			case T_PIPE: v->i = l | r; break;
			case T_CARET: v->i = l ^ r; break;
			case T_LESS: v->i = sg ? l < r : (u128)trunc_to(c, l, unsigned_of(c, ot)) < (u128)trunc_to(c, r, unsigned_of(c, ot)); break;
			case T_LESSEQ: v->i = sg ? l <= r : (u128)trunc_to(c, l, unsigned_of(c, ot)) <= (u128)trunc_to(c, r, unsigned_of(c, ot)); break;
			case T_GREATER: v->i = sg ? l > r : (u128)trunc_to(c, l, unsigned_of(c, ot)) > (u128)trunc_to(c, r, unsigned_of(c, ot)); break;
			case T_GREATEREQ: v->i = sg ? l >= r : (u128)trunc_to(c, l, unsigned_of(c, ot)) >= (u128)trunc_to(c, r, unsigned_of(c, ot)); break;
			case T_EQUALEQUAL: v->i = trunc_to(c, l, ot) == trunc_to(c, r, ot); break;
			case T_EXCLAIMEQ: v->i = trunc_to(c, l, ot) != trunc_to(c, r, ot); break;
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
		t = a.kind == CV_INT ? a.i != 0 : a.kind == CV_FLT ? a.f != 0 : true;
		v->kind = CV_INT;
		if (a.kind == CV_ADDR)
			a.ice = false;
		if ((x->op == N_LOGAND && !t) || (x->op == N_LOGOR && t)) {
			v->i = x->op == N_LOGOR;
			v->ice = a.ice;
			return true;
		}
		if (!eval(c, x->b, &b))
			return false;
		v->i = b.kind == CV_INT ? b.i != 0 : b.kind == CV_FLT ? b.f != 0 : 1;
		v->ice = a.ice && b.ice && b.kind != CV_ADDR;
		return true;
	}
	case N_COND: {
		bool t, ice;

		if (!eval(c, x->a, &a))
			return false;
		t = a.kind == CV_INT ? a.i != 0 : a.kind == CV_FLT ? a.f != 0 : true;
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
	case N_BUILTIN:
		return eval_builtin(c, x, v);
	default:
		return false;
	}
}

/* is n a null pointer constant (C11 6.3.2.3p3, clang: ICE or (void *)ICE) */
static bool is_npc(cc *c, uint32_t n)
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
static bool const_int(cc *c, uint32_t n, i128 *out)
{
	cval v;

	if (!eval(c, n, &v) || v.kind != CV_INT)
		return false;
	*out = v.i;
	return true;
}

/* ---------------- expressions: conversions ---------------- */

static uint32_t expr(cc *c);
static uint32_t assign_expr(cc *c);
static uint32_t cast_expr(cc *c);
static uint32_t cond_expr(cc *c);
static bool is_type_start(cc *c, const ptok *t);
static qty type_name(cc *c);
static uint32_t initializer(cc *c, qty *ty, bool is_static);
static uint32_t compound_stmt(cc *c, qty *last);
static uint32_t label_ref(cc *c, uint32_t name, uint32_t loc);

static uint32_t cast_node(cc *c, uint32_t n, qty to, int kind)
{
	uint32_t r;

	if (kind == CK_F2I) {
		/* a floating constant to an integer: folded, as clang does (the kernel has no FP code) */
		cval v;

		if (eval(c, n, &v) && v.kind == CV_FLT && v.f > -9.3e18 && v.f < 1.8e19) {
			i128 i = v.f < 0 ? (i128)(int64_t)v.f : (i128)(uint64_t)v.f;

			r = mk_int(c, trunc_to(c, i, to), to, N(n)->loc);
			return r;
		}
	}
	r = mk(c, N_CAST, to, N(n)->loc);

	N(r)->a = n;
	N(r)->aux = (uint16_t)kind;
	return r;
}

/* array/function designators to pointers (C11 6.3.2.1p3-4) */
static uint32_t decay(cc *c, uint32_t n)
{
	qty t = N(n)->ty;
	int k = KIND(c, t);

	if (k == TY_ARRAY)
		return cast_node(c, n, ty_ptr(c, T(c, t)->base), CK_DECAY);
	if (k == TY_FUNC)
		return cast_node(c, n, ty_ptr(c, QUNQ(t)), CK_FDECAY);
	return n;
}

static const field *bitfield_of(cc *c, uint32_t n)
{
	const node *x = N(n);

	if (x->op == N_MEMBER && (x->flags & NF_BITFIELD))
		return node_field(c, x);
	return NULL;
}

/* the type of n as an rvalue after integer promotion (bit-fields included) */
static qty promoted(cc *c, uint32_t n)
{
	qty t = QUNQ(N(n)->ty);
	const field *f = bitfield_of(c, n);

	if (f && is_integer(c, t) && KIND(c, t) != TY_BOOL) {
		if (f->width < 32)
			return prim(TY_INT);
		if (f->width == 32 && rank(c, t) <= 4)
			return is_signed(c, t) ? prim(TY_INT) : prim(TY_UINT);
		return QT(T(c, t)->canon, 0);
	}
	if (is_integer(c, t))
		return promote(c, t);
	return t;
}

static int conv_kind(cc *c, qty from, qty to, uint32_t n)
{
	int fk = KIND(c, from), tk = KIND(c, to);

	if (tk == TY_VOID)
		return CK_VOID;
	if (tk == TY_BOOL)
		return fk == TY_BOOL ? CK_NOP : CK_BOOL;
	if (is_int_kind(tk) || tk == TY_ENUM) {
		if (is_int_kind(fk) || fk == TY_ENUM)
			return ty_size(c, from) == ty_size(c, to) ? CK_NOP : CK_INT;
		if (is_flt_kind(fk))
			return CK_F2I;
		if (fk == TY_PTR)
			return CK_P2I;
		return -1;
	}
	if (is_flt_kind(tk)) {
		if (is_int_kind(fk) || fk == TY_ENUM)
			return CK_I2F;
		if (is_flt_kind(fk))
			return CK_F2F;
		return -1;
	}
	if (tk == TY_PTR) {
		if (fk == TY_PTR)
			return CK_P2P;
		if (is_int_kind(fk) || fk == TY_ENUM)
			return n && is_npc(c, n) ? CK_NULL : CK_I2P;
		return -1;
	}
	if ((tk == TY_STRUCT || tk == TY_UNION) && compatible(c, from, to))
		return CK_NOP;
	/* vectors of one size convert lane-agnostically (clang's lax integer vector conversions) */
	if (tk == TY_VECTOR && fk == TY_VECTOR && T(c, to)->size == T(c, from)->size)
		return CK_NOP;
	return -1;
}

/* is union type u a valid target for n (transparent unions, GNU casts to union) */
static bool union_member_for(cc *c, qty u, uint32_t n)
{
	const tag *r = &c->tags[T(c, u)->aux];
	qty from = N(n)->ty;

	for (uint32_t i = 0; i < r->nfield; i++) {
		qty ft = r->f[i].ty;

		if (compatible(c, ft, from))
			return true;
		if (is_ptr(c, ft) && (is_ptr(c, from) || is_npc(c, n)))
			return true;
	}
	return false;
}

/* implicit conversion of an rvalue (already decayed) to type to */
static uint32_t convert(cc *c, uint32_t n, qty to)
{
	qty from = N(n)->ty;
	int k;

	to = QUNQ(to);
	if (QI(canon(c, from)) == QI(canon(c, to)) && !bitfield_of(c, n))
		return n;
	k = conv_kind(c, from, to, n);
	if (k < 0) {
		if (KIND(c, to) == TY_UNION && union_member_for(c, to, n))
			return cast_node(c, n, to, CK_UNION);
		error(c, N(n)->loc, "incompatible types in conversion");
		return n;
	}
	return cast_node(c, n, to, k);
}

/* value of n as an rvalue of its promoted type */
static uint32_t rvalue(cc *c, uint32_t n)
{
	n = decay(c, n);
	if (is_integer(c, N(n)->ty)) {
		qty p = promoted(c, n);

		if (p != QUNQ(N(n)->ty) || bitfield_of(c, n))
			return cast_node(c, n, p, ty_size(c, p) == ty_size(c, N(n)->ty) && !bitfield_of(c, n) ? CK_NOP : CK_INT);
	}
	return n;
}

static void need_scalar(cc *c, uint32_t n, const char *what)
{
	if (!is_scalar(c, N(n)->ty))
		error(c, N(n)->loc, "%s requires a scalar operand", what);
}

static void need_lvalue(cc *c, uint32_t n, const char *what)
{
	if (!(N(n)->flags & NF_LVALUE))
		error(c, N(n)->loc, "expression is not assignable (%s)", what);
}

/* ---------------- expressions: operators ---------------- */

static uint32_t mk_bin(cc *c, int op, qty ty, uint32_t a, uint32_t b, uint32_t loc)
{
	uint32_t r = mk2(c, N_BINARY, ty, loc, a, b);

	N(r)->aux = (uint16_t)op;
	return r;
}

static bool is_cmp(int op)
{
	return op == T_LESS || op == T_LESSEQ || op == T_GREATER || op == T_GREATEREQ ||
	       op == T_EQUALEQUAL || op == T_EXCLAIMEQ;
}

static uint32_t binop(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	qty lt, rt;

	if (op == T_AMPAMP || op == T_PIPEPIPE) {
		l = decay(c, l), r = decay(c, r);
		need_scalar(c, l, "logical operator");
		need_scalar(c, r, "logical operator");
		return mk2(c, op == T_AMPAMP ? N_LOGAND : N_LOGOR, prim(TY_INT), loc, l, r);
	}
	l = rvalue(c, l), r = rvalue(c, r);
	lt = N(l)->ty, rt = N(r)->ty;
	if (is_vector(c, lt) || is_vector(c, rt)) {
		/* lane-wise on two vectors of one size; the result has the left operand's type */
		qty vt = QUNQ(is_vector(c, lt) ? lt : rt);

		if (!is_vector(c, lt) || !is_vector(c, rt) || T(c, lt)->size != T(c, rt)->size || is_cmp(op) ||
		    op == T_PERCENT || (op != T_PLUS && op != T_MINUS && op != T_STAR && op != T_SLASH &&
					!is_integer(c, T(c, vt)->base)))
			error(c, loc, "invalid operands to binary expression");
		return mk_bin(c, op, vt, convert(c, l, vt), convert(c, r, vt), loc);
	}
	if (is_arith(c, lt) && is_arith(c, rt)) {
		qty ct;

		if (op == T_LESSLESS || op == T_GREATERGREATER) {
			if (!is_integer(c, lt) || !is_integer(c, rt))
				error(c, loc, "invalid operands to binary expression");
			return mk_bin(c, op, lt, l, r, loc);
		}
		if ((op == T_PERCENT || op == T_AMP || op == T_PIPE || op == T_CARET) &&
		    (!is_integer(c, lt) || !is_integer(c, rt)))
			error(c, loc, "invalid operands to binary expression");
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
	error(c, loc, "invalid operands to binary expression");
	return mk_bin(c, op, prim(TY_INT), l, r, loc);
}

static uint32_t assign(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	qty lt = QUNQ(N(l)->ty);
	uint32_t n;

	need_lvalue(c, l, "assignment");
	if (QQ(N(l)->ty) & Q_CONST)
		error(c, loc, "cannot assign to variable with const-qualified type");
	r = decay(c, r);
	if (!op) {
		r = convert(c, r, lt);
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
			if (!is_vector(c, rt) || T(c, lt)->size != T(c, rt)->size)
				error(c, loc, "invalid operands to compound assignment");
			r = convert(c, r, lt);
			pt = lt;
		} else if (op == T_LESSLESS || op == T_GREATERGREATER) {
			/* computation type is the promoted lhs */
		} else if (is_arith(c, pt) && is_arith(c, rt)) {
			pt = arith_conv(c, pt, rt);
			r = convert(c, r, pt);
		} else {
			error(c, loc, "invalid operands to compound assignment");
		}
		n = mk2(c, N_ASSIGN, lt, loc, l, r);
		N(n)->aux = (uint16_t)op;
		N(n)->c = pt;	/* computation type */
		return n;
	}
}

/* the type of a ?: with these (decayed) arms */
static uint32_t conditional(cc *c, uint32_t cond, uint32_t a, uint32_t b, uint32_t loc)
{
	uint32_t ra, rb, n;
	qty at, bt, t;

	ra = a ? rvalue(c, a) : rvalue(c, cond);
	rb = rvalue(c, b);
	at = N(ra)->ty, bt = N(rb)->ty;
	if (is_arith(c, at) && is_arith(c, bt)) {
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
	} else if (is_record(c, at) && compatible(c, at, bt)) {
		t = QUNQ(at);
	} else {
		error(c, loc, "incompatible operand types in conditional expression");
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

static int find_member(cc *c, uint32_t ti, uint32_t name, uint32_t *path, int depth)
{
	const tag *r = &c->tags[ti];

	if (depth >= 16)
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

static uint32_t member(cc *c, uint32_t base, uint32_t name, uint32_t loc)
{
	qty bt = N(base)->ty;
	uint32_t path[16], ti;
	int d;

	if (!is_record(c, bt)) {
		error(c, loc, "member reference base type is not a structure or union");
		return mk_int(c, 0, prim(TY_INT), loc);
	}
	ti = T(c, bt)->aux;
	if (!(c->tags[ti].flags & RF_COMPLETE)) {
		error(c, loc, "incomplete definition of type");
		return mk_int(c, 0, prim(TY_INT), loc);
	}
	d = find_member(c, ti, name, path, 0);
	if (d < 0) {
		error(c, loc, "no member named '%.*s'", IDF(name));
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

static uint32_t deref(cc *c, uint32_t p, uint32_t loc)
{
	uint32_t n;
	qty t;

	p = rvalue(c, p);
	if (!is_ptr(c, N(p)->ty)) {
		error(c, loc, "indirection requires pointer operand");
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

static uint32_t addr_of(cc *c, uint32_t n, uint32_t loc)
{
	uint32_t r;
	qty t = N(n)->ty;

	if (bitfield_of(c, n))
		error(c, loc, "address of bit-field requested");
	if (!(N(n)->flags & NF_LVALUE) && KIND(c, t) != TY_FUNC && N(n)->op != N_COMPLIT)
		error(c, loc, "cannot take the address of an rvalue");
	mark_addr_taken(c, n);
	r = mk(c, N_ADDR, ty_ptr(c, t), loc);
	N(r)->a = n;
	return r;
}

/* ---------------- expressions: builtins ---------------- */

static uint32_t *bi_dir[NDIR];

static void init_builtins(void)
{
	static bool done;

	if (__atomic_load_n(&done, __ATOMIC_ACQUIRE))
		return;
	/* the table is shared by all TUs; building it twice is harmless */
	for (uint32_t i = 0; builtins[i].name; i++) {
		size_t l = strlen(builtins[i].name);
		char *s = malloc(l + 17);
		uint32_t id;

		memcpy(s, builtins[i].name, l);
		memset(s + l, 0, 17);
		id = vintern_copy(s, l);
		if (!bi_dir[id >> DIR_BITS]) {
			uint32_t *pg = calloc(DIR_PAGE, sizeof(uint32_t));

			bi_dir[id >> DIR_BITS] = pg;
		}
		bi_dir[id >> DIR_BITS][id & (DIR_PAGE - 1)] = i + 1;
		free(s);
	}
	__atomic_store_n(&done, true, __ATOMIC_RELEASE);
}

static const builtin *builtin_of(uint32_t id)
{
	uint32_t *pg = bi_dir[id >> DIR_BITS];
	uint32_t i = pg ? pg[id & (DIR_PAGE - 1)] : 0;

	return i ? &builtins[i - 1] : NULL;
}

static int classify_type(cc *c, qty t)
{
	switch (KIND(c, t)) {
	case TY_VOID: return 0;
	case TY_BOOL: return 4;
	case TY_ENUM: return 3;
	case TY_PTR: return 5;
	case TY_STRUCT: return 12;
	case TY_UNION: return 13;
	case TY_ARRAY: return 5;
	case TY_FUNC: return 5;
	default:
		if (is_float(c, t))
			return 8;
		return 1;
	}
}

bool cc_eval(cc *c, uint32_t n, cval *v) { return eval(c, n, v); }
bool cc_is_signed(cc *c, qty t) { return is_signed(c, t); }

static bool eval_builtin(cc *c, const node *x, cval *v)
{
	const uint32_t *arg = &c->lists[x->b];
	cval a;

	v->kind = CV_INT;
	v->ice = true;
	switch (x->aux) {
	case BI_CONSTANT_P:
		if (eval(c, arg[0], &a) && (a.kind == CV_INT || a.kind == CV_FLT || (a.kind == CV_ADDR && a.str && a.i == 0)))
			v->i = 1;
		else
			v->i = 0;
		return true;
	case BI_EXPECT:
	case BI_EXPECT_PROB:
		if (!eval(c, arg[0], v))
			return false;
		v->ice = v->ice && v->kind == CV_INT;
		return true;
	case BI_CLASSIFY_TYPE:
		v->i = classify_type(c, N(arg[0])->ty);
		return true;
	case BI_STRLEN: {
		uint32_t s = arg[0];

		while (N(s)->op == N_CAST)
			s = N(s)->a;
		if (N(s)->op != N_STR)
			return false;
		v->i = (i128)strlen(c->strs[N(s)->a].s);
		v->ice = false;
		return true;
	}
	case BI_HUGE_VAL: case BI_HUGE_VALF: case BI_INF: case BI_INFF:
		v->kind = CV_FLT, v->f = __builtin_inf(), v->ice = false;
		return true;
	case BI_NAN: case BI_NANF:
		v->kind = CV_FLT, v->f = __builtin_nan(""), v->ice = false;
		return true;
	case BI_CLZ: case BI_CLZL: case BI_CLZLL: case BI_CTZ: case BI_CTZL: case BI_CTZLL:
	case BI_FFS: case BI_FFSL: case BI_FFSLL: case BI_POPCOUNT: case BI_POPCOUNTL: case BI_POPCOUNTLL:
	case BI_PARITY: case BI_PARITYL: case BI_PARITYLL: case BI_BSWAP16: case BI_BSWAP32: case BI_BSWAP64:
	case BI_CLRSB: case BI_CLRSBL: case BI_CLRSBLL: {
		qty at;
		unsigned bits;
		u128 u;

		if (!eval(c, arg[0], &a) || a.kind != CV_INT)
			return false;
		at = N(arg[0])->ty;
		bits = (unsigned)ty_size(c, at) * 8;
		u = (u128)trunc_to(c, a.i, unsigned_of(c, at));
		v->ice = a.ice;
		switch (x->aux) {
		case BI_CLZ: case BI_CLZL: case BI_CLZLL:
			if (!u)
				return false;
			v->i = clz128(u, bits);
			break;
		case BI_CTZ: case BI_CTZL: case BI_CTZLL:
			if (!u)
				return false;
			v->i = __builtin_ctzll((uint64_t)u);
			break;
		case BI_FFS: case BI_FFSL: case BI_FFSLL:
			v->i = u ? __builtin_ctzll((uint64_t)u) + 1 : 0;
			break;
		case BI_POPCOUNT: case BI_POPCOUNTL: case BI_POPCOUNTLL:
			v->i = __builtin_popcountll((uint64_t)u);
			break;
		case BI_PARITY: case BI_PARITYL: case BI_PARITYLL:
			v->i = __builtin_popcountll((uint64_t)u) & 1;
			break;
		case BI_CLRSB: case BI_CLRSBL: case BI_CLRSBLL: {
			u128 s = (u >> (bits - 1)) & 1 ? ~u & (((u128)1 << bits) - 1) : u;

			v->i = clz128(s, bits) - 1;
			break;
		}
		case BI_BSWAP16: v->i = __builtin_bswap16((uint16_t)u); break;
		case BI_BSWAP32: v->i = __builtin_bswap32((uint32_t)u); break;
		case BI_BSWAP64: v->i = (i128)__builtin_bswap64((uint64_t)u); break;
		}
		v->i = trunc_to(c, v->i, x->ty);
		return true;
	}
	default:
		return false;
	}
}

static uint32_t args_list(cc *c, vec *args)
{
	vinit(args);
	expect(c, T_LPAREN, "'('");
	if (!is(c, T_RPAREN)) {
		do
			vpush(c, args, assign_expr(c));
		while (accept(c, T_COMMA));
	}
	expect(c, T_RPAREN, "')'");
	return args->n;
}

/* t without qualifiers, down through array element types (clang's unqualified array type) */
static qty unqual_array(cc *c, qty t)
{
	const ctype *x;

	t = QUNQ(t);
	if (!is_array(c, t))
		return t;
	x = T(c, t);
	if (x->flags & TF_VLA)
		return t;
	{
		qty e = x->base, ue = unqual_array(c, e);
		int fl = x->flags & TF_INCOMPLETE;

		return ue == e ? t : ty_array(c, ue, fl ? 0 : arr_len(c, t), fl);
	}
}

static uint32_t mk_builtin(cc *c, int id, qty ty, uint32_t loc, const vec *args)
{
	uint32_t n = mk(c, N_BUILTIN, ty, loc);

	N(n)->aux = (uint16_t)id;
	N(n)->b = vfreeze(c, args);
	N(n)->c = args->n;
	return n;
}

static uint32_t offsetof_expr(cc *c, uint32_t loc)
{
	qty t;
	uint32_t res;
	i128 off = 0;
	uint32_t dyn = 0;	/* non-constant part (node), 0 if none */

	expect(c, T_LPAREN, "'('");
	t = type_name(c);
	expect(c, T_COMMA, "','");
	for (bool first = true;; first = false) {
		if (first || accept(c, T_PERIOD)) {
			uint32_t path[16], ti;
			int d;

			if (!is(c, T_IDENT))
				fatal(c, tok(c)->loc, "expected identifier");
			if (!is_record(c, t)) {
				error(c, tok(c)->loc, "offsetof requires struct, union, or class type");
				next(c);
				break;
			}
			ti = T(c, t)->aux;
			d = find_member(c, ti, tok(c)->id, path, 0);
			if (d < 0) {
				error(c, tok(c)->loc, "no member named '%.*s'", IDF(tok(c)->id));
				next(c);
				break;
			}
			for (int i = 0; i < d; i++) {
				const field *f = &c->tags[ti].f[path[i]];

				off += (i128)(f->off / 8);
				t = f->ty;
				if (is_record(c, t))
					ti = T(c, t)->aux;
			}
			next(c);
		} else if (accept(c, T_LSQUARE)) {
			uint32_t ix = expr(c);
			i128 k;

			expect(c, T_RSQUARE, "']'");
			if (!is_array(c, t)) {
				error(c, loc, "subscripted value is not an array");
				break;
			}
			t = T(c, t)->base;
			if (const_int(c, ix, &k)) {
				off += k * (i128)ty_size(c, t);
			} else {
				uint32_t sz = mk_int(c, (i128)ty_size(c, t), c->t_size, loc);
				uint32_t term = mk_bin(c, T_STAR, c->t_size, convert(c, rvalue(c, ix), c->t_size), sz, loc);

				dyn = dyn ? mk_bin(c, T_PLUS, c->t_size, dyn, term, loc) : term;
			}
		} else {
			break;
		}
	}
	expect(c, T_RPAREN, "')'");
	res = mk_int(c, off, c->t_size, loc);
	if (dyn)
		res = mk_bin(c, T_PLUS, c->t_size, res, dyn, loc);
	return res;
}

static uint32_t builtin_call(cc *c, const builtin *b, uint32_t loc)
{
	vec args;
	qty rt = sigtype(c, b->sig[0]);

	switch (b->id) {
	case BI_CHOOSE: {
		uint32_t cond, x, y;
		i128 v = 0;

		expect(c, T_LPAREN, "'('");
		cond = assign_expr(c);
		expect(c, T_COMMA, "','");
		x = assign_expr(c);
		expect(c, T_COMMA, "','");
		y = assign_expr(c);
		expect(c, T_RPAREN, "')'");
		if (!const_int(c, decay(c, cond), &v))
			error(c, N(cond)->loc, "'__builtin_choose_expr' requires a constant expression");
		return v ? x : y;
	}
	case BI_TYPES_COMPAT: {
		qty a, t;

		expect(c, T_LPAREN, "'('");
		a = type_name(c);
		expect(c, T_COMMA, "','");
		t = type_name(c);
		expect(c, T_RPAREN, "')'");
		return mk_int(c, compatible(c, unqual_array(c, a), unqual_array(c, t)), prim(TY_INT), loc);
	}
	case BI_OFFSETOF:
		return offsetof_expr(c, loc);
	case BI_VA_ARG: {
		uint32_t ap, n;
		qty t;

		expect(c, T_LPAREN, "'('");
		ap = assign_expr(c);
		expect(c, T_COMMA, "','");
		t = type_name(c);
		expect(c, T_RPAREN, "')'");
		n = mk(c, N_VAARG, t, loc);
		N(n)->a = ap;
		return n;
	}
	case BI_CONSTANT_P: {
		uint32_t n;

		c->unevaluated++;
		args_list(c, &args);
		c->unevaluated--;
		if (args.n != 1)
			error(c, loc, "too %s arguments to function call", args.n ? "many" : "few");
		for (uint32_t i = 0; i < args.n; i++)
			args.v[i] = decay(c, args.v[i]);
		n = mk_builtin(c, b->id, prim(TY_INT), loc, &args);
		return n;
	}
	case BI_COUNTED_BY_REF: {
		uint32_t m;

		args_list(c, &args);
		m = args.n ? args.v[0] : 0;
		if (m && N(m)->op == N_MEMBER && node_field(c, N(m))->counted_by) {
			uint32_t cnt = member(c, N(m)->a, node_field(c, N(m))->counted_by, loc);

			return addr_of(c, cnt, loc);
		}
		return cast_node(c, mk_int(c, 0, prim(TY_INT), loc), ty_ptr(c, prim(TY_VOID)), CK_NULL);
	}
	default:
		break;
	}
	args_list(c, &args);
	{
		qty ft = builtin_type(c, b);
		const ctype *f = T(c, ft);
		uint32_t np = f->n;

		for (uint32_t i = 0; i < args.n; i++) {
			uint32_t a = decay(c, args.v[i]);

			if (i < np)
				a = convert(c, a, c->params[f->aux + i]);
			args.v[i] = a;
		}
		if (args.n < np || (args.n > np && !(f->flags & TF_VARIADIC)))
			error(c, loc, "too %s arguments to function call", args.n < np ? "few" : "many");
		switch (b->id) {
		case BI_ADD_OVERFLOW: case BI_SUB_OVERFLOW: case BI_MUL_OVERFLOW:
			if (args.n != 3 || !is_ptr(c, N(args.v[2])->ty))
				error(c, loc, "result argument to overflow builtin must be a pointer");
			break;
		case BI_ADDC: case BI_SUBC:
			if (args.n != 4 || !is_ptr(c, N(args.v[3])->ty))
				error(c, loc, "carry-out argument to builtin must be a pointer");
			break;
		case BI_ISNAN: case BI_ISINF: case BI_ISFINITE: case BI_SIGNBIT: case BI_ISNORMAL:
		case BI_ISINF_SIGN: case BI_FPCLASSIFY:
			break;
		}
		if (b->id == BI_LIB) {
			/* a call to the library function the builtin names */
			uint32_t s = sym_new(c, S_BUILTIN, 0, ft, loc), fn, n;

			c->syms[s].val = b - builtins;
			fn = mk(c, N_VAR, ft, loc);
			N(fn)->a = s;
			n = mk(c, N_CALL, rt, loc);
			N(n)->a = decay(c, fn);
			N(n)->b = vfreeze(c, &args);
			N(n)->c = args.n;
			return n;
		}
		return mk_builtin(c, b->id, rt, loc, &args);
	}
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
		error(c, loc, "controlling expression type not compatible with any generic association type");
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

static uint32_t primary(cc *c)
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
		if (is(c, T_LBRACE)) {	/* GNU statement expression */
			qty last = prim(TY_VOID);
			uint32_t b, n;

			if (!c->fn)
				error(c, loc, "statement expression not allowed at file scope");
			b = compound_stmt(c, &last);
			expect(c, T_RPAREN, "')'");
			n = mk(c, N_STMTEXPR, last, loc);
			N(n)->a = b;
			return n;
		}
		{
			uint32_t n = expr(c);

			expect(c, T_RPAREN, "')'");
			N(n)->flags |= NF_PAREN;
			return n;
		}
	case T_IDENT: {
		uint32_t id = t->id, s;
		int kw = kwof(t);

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
		if (kw == KW___builtin_choose_expr) {
			static const builtin ch = { "", BI_CHOOSE, 1, "i" };

			next(c);
			return builtin_call(c, &ch, loc);
		}
		if (kw) {
			char b[64];

			fatal(c, loc, "expected expression before '%s'", tokspell(t, b, sizeof(b)));
		}
		s = lookup(c, id);
		next(c);
		if (s) {
			sym *y = &c->syms[s];

			if (y->kind == S_ENUMCONST)
				return mk_int(c, y->val, y->ty, loc);
			if (y->kind == S_TYPEDEF)
				fatal(c, loc, "unexpected type name '%.*s': expected expression", IDF(id));
			{
				uint32_t n = mk(c, N_VAR, y->ty, loc);

				N(n)->a = s;
				N(n)->flags = y->kind == S_VAR ? NF_LVALUE : 0;
				if (!c->unevaluated && !((y->flags & SF_OVERLOAD) && is(c, T_LPAREN)))
					y->flags |= SF_USED;
				return n;
			}
		}
		{
			const builtin *b = builtin_of(id);

			if (b && is(c, T_LPAREN))
				return builtin_call(c, b, loc);
		}
		if (id == c->id_func || id == c->id_FUNCTION || id == c->id_PRETTY)
			return func_name(c, loc);
		if (is(c, T_LPAREN))
			error(c, loc, "call to undeclared function '%.*s'; ISO C99 and later do not support implicit function declarations", IDF(id));
		else
			error(c, loc, "use of undeclared identifier '%.*s'", IDF(id));
		return mk_int(c, 0, prim(TY_INT), loc);
	}
	default: {
		char b[64];

		fatal(c, loc, "expected expression before '%s'", tokspell(t, b, sizeof(b)));
	}
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
static uint32_t ovl_pick(cc *c, uint32_t head, const vec *args, uint32_t loc)
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
		error(c, loc, "no matching function for call to '%.*s'", IDF(c->syms[head].name));
		return head;
	}
	if (nbest > 1)
		error(c, loc, "call to '%.*s' is ambiguous", IDF(c->syms[head].name));
	return best;
}

static uint32_t call(cc *c, uint32_t fn, uint32_t loc)
{
	vec args;
	qty ft;
	const ctype *f;
	uint32_t n;
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
	}
	fn = rvalue(c, fn);
	ft = N(fn)->ty;
	if (!is_ptr(c, ft) || !is_func(c, T(c, ft)->base)) {
		error(c, loc, "called object type is not a function or function pointer");
		if (!have_args)
			args_list(c, &args);
		return mk_int(c, 0, prim(TY_INT), loc);
	}
	ft = T(c, ft)->base;
	f = T(c, ft);
	if (!have_args)
		args_list(c, &args);
	f = T(c, ft);
	if (!(f->flags & TF_NOPROTO)) {
		if (args.n < f->n || (args.n > f->n && !(f->flags & TF_VARIADIC)))
			error(c, loc, "too %s arguments to function call, expected %u, have %u",
			      args.n < f->n ? "few" : "many", f->n, args.n);
	}
	for (uint32_t i = 0; i < args.n; i++) {
		uint32_t a = args.v[i];

		f = T(c, ft);
		if (!(f->flags & TF_NOPROTO) && i < f->n) {
			a = convert(c, decay(c, a), c->params[f->aux + i]);
		} else {
			/* default argument promotions */
			a = rvalue(c, a);
			if (KIND(c, N(a)->ty) == TY_FLOAT)
				a = cast_node(c, a, prim(TY_DOUBLE), CK_F2F);
		}
		args.v[i] = a;
	}
	n = mk(c, N_CALL, QUNQ(T(c, ft)->base), loc);
	N(n)->a = fn;
	N(n)->b = vfreeze(c, &args);
	N(n)->c = args.n;
	return n;
}

static uint32_t postfix(cc *c, uint32_t n)
{
	for (;;) {
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
			}
			n = rvalue(c, n), ix = rvalue(c, ix);
			at = N(n)->ty, it = N(ix)->ty;
			if (!is_ptr(c, at) && is_ptr(c, it)) {
				uint32_t t = n;

				n = ix, ix = t;
				at = N(n)->ty, it = N(ix)->ty;
			}
			if (!is_ptr(c, at) || !is_integer(c, it)) {
				error(c, loc, "subscripted value is not an array, pointer, or vector");
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
				n = member(c, deref(c, n, loc), id, loc);
			}
			continue;
		case T_PLUSPLUS:
		case T_MINUSMINUS: {
			int op = tok(c)->kind == T_PLUSPLUS ? N_POSTINC : N_POSTDEC;
			uint32_t r;

			next(c);
			need_lvalue(c, n, "increment");
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

/* ---------------- expressions: unary, cast, binary ---------------- */

static uint32_t sizeof_type(cc *c, qty t, uint32_t loc, uint32_t e)
{
	int k = KIND(c, t);

	if (k == TY_ARRAY && (T(c, t)->flags & TF_VLA)) {
		uint32_t n = mk(c, N_SIZEOF_VLA, c->t_size, loc);

		N(n)->a = e;
		N(n)->b = t;
		return n;
	}
	if (k == TY_VOID || k == TY_FUNC)
		return mk_int(c, 1, c->t_size, loc);
	if (!complete(c, t))
		error(c, loc, "invalid application of 'sizeof' to an incomplete type");
	return mk_int(c, (i128)ty_size(c, t), c->t_size, loc);
}

static uint32_t align_of_expr(cc *c, uint32_t e)
{
	const node *x = N(e);
	uint32_t a = ty_align(c, x->ty);

	if (x->op == N_VAR && c->syms[x->a].align > a)
		a = c->syms[x->a].align;
	if (is_array(c, x->ty)) {
		qty t = x->ty;

		while (is_array(c, t))
			t = T(c, t)->base;
		if (ty_align(c, t) > a)
			a = ty_align(c, t);
	}
	return a;
}

static uint32_t unary(cc *c)
{
	ptok *t = tok(c);
	uint32_t loc = t->loc, n;
	int kw = kwof(t);

	switch (t->kind) {
	case T_PLUSPLUS:
	case T_MINUSMINUS: {
		int op = t->kind == T_PLUSPLUS ? N_PREINC : N_PREDEC;

		next(c);
		n = unary(c);
		need_lvalue(c, n, "increment");
		need_scalar(c, n, "increment");
		{
			uint32_t r = mk(c, op, QUNQ(N(n)->ty), loc);

			N(r)->a = n;
			return r;
		}
	}
	case T_AMP:
		next(c);
		n = cast_expr(c);
		return addr_of(c, n, loc);
	case T_STAR:
		next(c);
		n = cast_expr(c);
		if (KIND(c, N(n)->ty) == TY_FUNC)
			return n;
		return deref(c, n, loc);
	case T_PLUS:
	case T_MINUS:
	case T_TILDE: {
		int op = t->kind;

		next(c);
		n = rvalue(c, cast_expr(c));
		if (!is_arith(c, N(n)->ty) || (op == T_TILDE && !is_integer(c, N(n)->ty)))
			error(c, loc, "invalid argument type to unary expression");
		{
			uint32_t r = mk(c, N_UNARY, N(n)->ty, loc);

			N(r)->aux = (uint16_t)op;
			N(r)->a = n;
			return r;
		}
	}
	case T_EXCLAIM: {
		uint32_t r;

		next(c);
		n = rvalue(c, cast_expr(c));
		need_scalar(c, n, "'!'");
		r = mk(c, N_UNARY, prim(TY_INT), loc);
		N(r)->aux = T_EXCLAIM;
		N(r)->a = n;
		return r;
	}
	case T_AMPAMP: {	/* &&label */
		uint32_t r;

		next(c);
		if (!is(c, T_IDENT))
			fatal(c, tok(c)->loc, "expected identifier");
		r = mk(c, N_LABELADDR, ty_ptr(c, prim(TY_VOID)), loc);
		N(r)->a = label_ref(c, tok(c)->id, tok(c)->loc);
		c->labels[N(r)->a].addr_taken = 1;
		next(c);
		return r;
	}
	default:
		break;
	}
	switch (kw) {
	case KW_sizeof: {
		next(c);
		if (is(c, T_LPAREN) && is_type_start(c, peek(c, 1))) {
			qty ty;

			next(c);
			ty = type_name(c);
			expect(c, T_RPAREN, "')'");
			if (is(c, T_LBRACE)) {	/* sizeof (T){...} */
				uint32_t init, s, cl;

				init = initializer(c, &ty, !c->fn);
				s = sym_new(c, S_VAR, 0, ty, loc);
				cl = mk2(c, N_COMPLIT, ty, loc, init, s);
				N(cl)->flags = NF_LVALUE;
				n = postfix(c, cl);
				return sizeof_type(c, N(n)->ty, loc, n);
			}
			return sizeof_type(c, ty, loc, 0);
		}
		c->unevaluated++;
		n = unary(c);
		c->unevaluated--;
		if (bitfield_of(c, n))
			error(c, loc, "invalid application of 'sizeof' to bit-field");
		return sizeof_type(c, N(n)->ty, loc, n);
	}
	case KW__Alignof:
	case KW___alignof__:
	case KW___alignof: {
		next(c);
		if (is(c, T_LPAREN) && is_type_start(c, peek(c, 1))) {
			qty ty;

			next(c);
			ty = type_name(c);
			expect(c, T_RPAREN, "')'");
			while (is_array(c, ty))
				ty = T(c, ty)->base;
			return mk_int(c, ty_align(c, ty), c->t_size, loc);
		}
		c->unevaluated++;
		n = unary(c);
		c->unevaluated--;
		return mk_int(c, align_of_expr(c, n), c->t_size, loc);
	}
	case KW___extension__:
		next(c);
		return cast_expr(c);
	case KW___real__:
	case KW___imag__:
		next(c);
		n = cast_expr(c);
		if (kw == KW___imag__)
			return mk_int(c, 0, QUNQ(N(n)->ty), loc);
		return n;
	default:
		return postfix(c, primary(c));
	}
}

static uint32_t explicit_cast(cc *c, uint32_t n, qty to, uint32_t loc)
{
	int k;
	uint32_t r;

	n = decay(c, n);
	if (is_void(c, to))
		return cast_node(c, n, QUNQ(to), CK_VOID);
	if (KIND(c, to) == TY_UNION && !compatible(c, to, N(n)->ty)) {
		if (!union_member_for(c, to, n))
			error(c, loc, "cast to union type from type not present in union");
		return cast_node(c, n, QUNQ(to), CK_UNION);
	}
	if (is_vector(c, to) && is_vector(c, N(n)->ty) && T(c, to)->size == T(c, N(n)->ty)->size)
		return cast_node(c, n, QUNQ(to), CK_NOP);
	if (!is_scalar(c, to) && !compatible(c, to, N(n)->ty)) {
		error(c, loc, "used type where arithmetic or pointer type is required");
		return n;
	}
	if (is_integer(c, N(n)->ty))
		n = rvalue(c, n);
	k = conv_kind(c, N(n)->ty, QUNQ(to), n);
	if (k < 0) {
		error(c, loc, "invalid cast");
		return n;
	}
	if (k == CK_NULL)
		k = CK_I2P;	/* keep (void *)0 recognisable as a cast */
	r = cast_node(c, n, QUNQ(to), k);
	N(r)->loc = loc;
	return r;
}

static uint32_t cast_expr(cc *c)
{
	if (is(c, T_LPAREN) && is_type_start(c, peek(c, 1))) {
		uint32_t loc = tok(c)->loc, n;
		qty ty;

		next(c);
		ty = type_name(c);
		expect(c, T_RPAREN, "')'");
		if (is(c, T_LBRACE)) {	/* compound literal */
			uint32_t init, s;
			bool st = !c->fn;

			init = initializer(c, &ty, st);
			s = sym_new(c, S_VAR, 0, ty, loc);
			c->syms[s].body = init;
			c->syms[s].sc = st ? SC_STATIC : SC_AUTO;
			if (st)
				c->syms[s].flags |= SF_GLOBAL | SF_DEFINED;
			n = mk2(c, N_COMPLIT, ty, loc, init, s);
			N(n)->flags = NF_LVALUE;
			return postfix(c, n);
		}
		n = cast_expr(c);
		return explicit_cast(c, n, ty, loc);
	}
	return unary(c);
}

static int binprec(int k)
{
	switch (k) {
	case T_STAR: case T_SLASH: case T_PERCENT: return 10;
	case T_PLUS: case T_MINUS: return 9;
	case T_LESSLESS: case T_GREATERGREATER: return 8;
	case T_LESS: case T_LESSEQ: case T_GREATER: case T_GREATEREQ: return 7;
	case T_EQUALEQUAL: case T_EXCLAIMEQ: return 6;
	case T_AMP: return 5;
	case T_CARET: return 4;
	case T_PIPE: return 3;
	case T_AMPAMP: return 2;
	case T_PIPEPIPE: return 1;
	default: return 0;
	}
}

static uint32_t binary(cc *c, int minp)
{
	uint32_t l = cast_expr(c);

	for (;;) {
		int op = tok(c)->kind, p = binprec(op);
		uint32_t loc, r;

		if (!p || p < minp)
			return l;
		loc = tok(c)->loc;
		next(c);
		r = binary(c, p + 1);
		l = binop(c, op, l, r, loc);
	}
}

static uint32_t cond_expr(cc *c)
{
	uint32_t cnd = binary(c, 1);

	if (is(c, T_QUESTION)) {
		uint32_t loc = tok(c)->loc, a = 0, b;

		next(c);
		need_scalar(c, decay(c, cnd), "conditional");
		if (!is(c, T_COLON))
			a = expr(c);
		expect(c, T_COLON, "':'");
		b = cond_expr(c);
		return conditional(c, cnd, a, b, loc);
	}
	return cnd;
}

static int assign_op(int k)
{
	switch (k) {
	case T_EQUAL: return -1;
	case T_STAREQ: return T_STAR;
	case T_SLASHEQ: return T_SLASH;
	case T_PERCENTEQ: return T_PERCENT;
	case T_PLUSEQ: return T_PLUS;
	case T_MINUSEQ: return T_MINUS;
	case T_LESSLESSEQ: return T_LESSLESS;
	case T_GREATERGREATEREQ: return T_GREATERGREATER;
	case T_AMPEQ: return T_AMP;
	case T_CARETEQ: return T_CARET;
	case T_PIPEEQ: return T_PIPE;
	default: return 0;
	}
}

static uint32_t assign_expr(cc *c)
{
	uint32_t l = cond_expr(c);
	int op = assign_op(tok(c)->kind);

	if (op) {
		uint32_t loc = tok(c)->loc, r;

		next(c);
		r = assign_expr(c);
		return assign(c, op < 0 ? 0 : op, l, r, loc);
	}
	return l;
}

static uint32_t expr(cc *c)
{
	uint32_t l = assign_expr(c);

	while (is(c, T_COMMA)) {
		uint32_t loc = tok(c)->loc, r;

		next(c);
		r = assign_expr(c);
		l = mk2(c, N_COMMA, QUNQ(N(decay(c, r))->ty), loc, l, decay(c, r));
	}
	return l;
}

/* a constant expression as an int (array sizes, widths, enum values, cases) */
static bool const_expr(cc *c, i128 *v, uint32_t *np)
{
	uint32_t n = rvalue(c, cond_expr(c));

	if (np)
		*np = n;
	if (!const_int(c, n, v)) {
		error(c, N(n)->loc, "expression is not an integer constant expression");
		*v = 0;
		return false;
	}
	return true;
}

/* ---------------- attributes ---------------- */

enum {
	A_NONE, A_ALIGNED, A_PACKED, A_SECTION, A_MODE, A_CLEANUP, A_ALIAS, A_WEAK, A_USED, A_UNUSED,
	A_NORETURN, A_ALWAYS_INLINE, A_NOINLINE, A_GNU_INLINE, A_TRANSPARENT_UNION, A_VISIBILITY,
	A_CONSTRUCTOR, A_DESTRUCTOR, A_COUNTED_BY, A_VECTOR_SIZE, A_COLD, A_NONSTRING, A_MAY_ALIAS,
	A_WEAKREF, A_NAKED, A_NOCF, A_ERROR, A_OVERLOADABLE,
	A_COUNT
};

static const char *const attr_names[A_COUNT] = {
	[A_ALIGNED] = "aligned", [A_PACKED] = "packed", [A_SECTION] = "section", [A_MODE] = "mode",
	[A_CLEANUP] = "cleanup", [A_ALIAS] = "alias", [A_WEAK] = "weak", [A_USED] = "used",
	[A_UNUSED] = "unused", [A_NORETURN] = "noreturn", [A_ALWAYS_INLINE] = "always_inline",
	[A_NOINLINE] = "noinline", [A_GNU_INLINE] = "gnu_inline", [A_TRANSPARENT_UNION] = "transparent_union",
	[A_VISIBILITY] = "visibility", [A_CONSTRUCTOR] = "constructor", [A_DESTRUCTOR] = "destructor",
	[A_COUNTED_BY] = "counted_by", [A_VECTOR_SIZE] = "vector_size", [A_COLD] = "cold",
	[A_NONSTRING] = "nonstring", [A_MAY_ALIAS] = "may_alias", [A_WEAKREF] = "weakref",
	[A_NAKED] = "naked", [A_NOCF] = "nocf_check", [A_ERROR] = "error",
	[A_OVERLOADABLE] = "overloadable",
};

static uint32_t *attr_dir[NDIR];

static void init_attrs(void)
{
	static bool done;

	if (__atomic_load_n(&done, __ATOMIC_ACQUIRE))
		return;
	for (int i = 1; i < A_COUNT; i++) {
		char b[2][64];
		size_t l = strlen(attr_names[i]);

		memset(b, 0, sizeof(b));
		memcpy(b[0], attr_names[i], l);
		b[1][0] = b[1][1] = '_';
		memcpy(b[1] + 2, attr_names[i], l);
		b[1][l + 2] = b[1][l + 3] = '_';
		for (int k = 0; k < 2; k++) {
			uint32_t id = vintern_copy(b[k], k ? l + 4 : l);

			if (!attr_dir[id >> DIR_BITS])
				attr_dir[id >> DIR_BITS] = calloc(DIR_PAGE, sizeof(uint32_t));
			attr_dir[id >> DIR_BITS][id & (DIR_PAGE - 1)] = (uint32_t)i;
		}
	}
	__atomic_store_n(&done, true, __ATOMIC_RELEASE);
}

static int attr_of(uint32_t id)
{
	uint32_t *pg = attr_dir[id >> DIR_BITS];

	return pg ? (int)pg[id & (DIR_PAGE - 1)] : 0;
}

static void skip_balanced(cc *c)
{
	int depth = 0;

	do {
		if (is(c, T_LPAREN))
			depth++;
		else if (is(c, T_RPAREN))
			depth--;
		else if (is(c, T_EOF))
			fatal(c, tok(c)->loc, "unexpected end of file in attribute");
		next(c);
	} while (depth > 0);
}

static uint32_t string_index(cc *c)
{
	uint32_t n;

	if (!is_strtok(tok(c)->kind)) {
		error(c, tok(c)->loc, "expected string literal");
		return 0;
	}
	n = string_lit(c);
	return N(n)->a + 1;
}

static int mode_kind(cc *c, uint32_t id, bool sgn)
{
	int l;
	const char *s = idname(id, &l);
	char b[16] = { 0 };

	if (l > 2 && s[0] == '_' && s[1] == '_')
		s += 2, l -= 4;
	if (l <= 0 || l >= 15)
		return 0;
	memcpy(b, s, (size_t)l);
	(void)c;
	if (!strcmp(b, "QI")) return sgn ? TY_SCHAR : TY_UCHAR;
	if (!strcmp(b, "HI")) return sgn ? TY_SHORT : TY_USHORT;
	if (!strcmp(b, "SI")) return sgn ? TY_INT : TY_UINT;
	if (!strcmp(b, "DI") || !strcmp(b, "word") || !strcmp(b, "pointer")) return sgn ? TY_LONG : TY_ULONG;
	if (!strcmp(b, "TI")) return sgn ? TY_INT128 : TY_UINT128;
	return 0;
}

static void attribute_list(cc *c, attrs *a)
{
	while (iskw(c, KW___attribute__) || iskw(c, KW___attribute)) {
		next(c);
		expect(c, T_LPAREN, "'('");
		expect(c, T_LPAREN, "'('");
		while (!is(c, T_RPAREN)) {
			ptok *t = tok(c);
			int an;
			uint32_t loc = t->loc;

			if (accept(c, T_COMMA))
				continue;
			if (t->kind != T_IDENT)
				fatal(c, loc, "expected attribute name");
			an = attr_of(t->id);
			if (!an && kwof(t) == KW_const)
				an = A_NONE;
			next(c);
			switch (an) {
			case A_ALIGNED:
				if (accept(c, T_LPAREN)) {
					i128 v;

					const_expr(c, &v, NULL);
					expect(c, T_RPAREN, "')'");
					if (v <= 0 || (v & (v - 1)))
						error(c, loc, "requested alignment is not a power of 2");
					else if ((uint32_t)v > a->aligned)
						a->aligned = (uint32_t)v;
				} else if (16 > a->aligned) {
					a->aligned = 16;
				}
				continue;
			case A_SECTION:
			case A_ALIAS:
			case A_VISIBILITY:
				expect(c, T_LPAREN, "'('");
				{
					uint32_t s = string_index(c);

					if (an == A_SECTION)
						a->section = s;
					else if (an == A_ALIAS)
						a->alias = s;
					else
						a->visibility = s;
				}
				expect(c, T_RPAREN, "')'");
				continue;
			case A_MODE:
				expect(c, T_LPAREN, "'('");
				if (is(c, T_IDENT))
					a->mode = tok(c)->id;
				next(c);
				expect(c, T_RPAREN, "')'");
				continue;
			case A_CLEANUP:
				expect(c, T_LPAREN, "'('");
				if (is(c, T_IDENT)) {
					uint32_t s = lookup(c, tok(c)->id);

					if (!s || c->syms[s].kind != S_FUNC)
						error(c, tok(c)->loc, "'cleanup' argument is not a function");
					a->cleanup = s;
				}
				next(c);
				expect(c, T_RPAREN, "')'");
				continue;
			case A_COUNTED_BY:
				expect(c, T_LPAREN, "'('");
				if (is(c, T_IDENT))
					a->counted_by = tok(c)->id;
				next(c);
				expect(c, T_RPAREN, "')'");
				continue;
			case A_VECTOR_SIZE: {
				i128 v;

				expect(c, T_LPAREN, "'('");
				const_expr(c, &v, NULL);
				expect(c, T_RPAREN, "')'");
				if (v <= 0 || (v & (v - 1)) || v > 64)
					error(c, loc, "invalid vector size");
				else
					a->vecsize = (uint32_t)v;
				continue;
			}
			case A_PACKED: a->packed = 1; break;
			case A_WEAK: a->weak = 1; break;
			case A_USED: a->used = 1; break;
			case A_UNUSED: a->unused = 1; break;
			case A_NORETURN: a->noreturn = 1; break;
			case A_ALWAYS_INLINE: a->always_inline = 1; break;
			case A_NOINLINE: a->noinline = 1; break;
			case A_OVERLOADABLE: a->overloadable = 1; break;
			case A_GNU_INLINE: a->gnu_inline = 1; break;
			case A_TRANSPARENT_UNION: a->transparent = 1; break;
			case A_CONSTRUCTOR: a->constructor = 1; break;
			case A_DESTRUCTOR: a->destructor = 1; break;
			case A_COLD: a->cold = 1; break;
			case A_NONSTRING: a->nonstring = 1; break;
			case A_MAY_ALIAS: a->may_alias = 1; break;
			case A_ERROR: a->error_fn = 1; break;	/* a call left after folding is a build error */
			default: break;
			}
			if (is(c, T_LPAREN))
				skip_balanced(c);
		}
		expect(c, T_RPAREN, "')'");
		expect(c, T_RPAREN, "')'");
	}
}

static void merge_attrs(attrs *to, const attrs *from)
{
	if (from->aligned > to->aligned)
		to->aligned = from->aligned;
	if (from->section) to->section = from->section;
	if (from->alias) to->alias = from->alias;
	if (from->asmname) to->asmname = from->asmname;
	if (from->cleanup) to->cleanup = from->cleanup;
	if (from->mode) to->mode = from->mode;
	if (from->vecsize) to->vecsize = from->vecsize;
	if (from->counted_by) to->counted_by = from->counted_by;
	if (from->visibility) to->visibility = from->visibility;
	to->packed |= from->packed, to->transparent |= from->transparent, to->used |= from->used;
	to->unused |= from->unused, to->weak |= from->weak, to->noreturn |= from->noreturn;
	to->constructor |= from->constructor, to->destructor |= from->destructor;
	to->nonstring |= from->nonstring, to->may_alias |= from->may_alias, to->cold |= from->cold, to->error_fn |= from->error_fn;
	to->gnu_inline |= from->gnu_inline, to->always_inline |= from->always_inline;
	to->noinline |= from->noinline, to->overloadable |= from->overloadable;
}

/* GNU asm("label") after a declarator */
static void asm_label(cc *c, attrs *a)
{
	int kw = kwof(tok(c));

	if (kw != KW_asm && kw != KW___asm__ && kw != KW___asm)
		return;
	next(c);
	expect(c, T_LPAREN, "'('");
	a->asmname = string_index(c);
	expect(c, T_RPAREN, "')'");
}

/* ---------------- declaration specifiers ---------------- */

typedef struct dspec {
	qty ty;
	int sc;
	bool inline_, noreturn, thread, auto_type, has_type;
	attrs at;
	uint32_t align;		/* _Alignas */
	uint32_t loc;
	uint32_t tagdecl;	/* struct/union/enum defined or declared here: its tag */
	bool tag_bare;		/* "struct S" with no body (a reference or forward declaration) */
} dspec;

static bool decl_specs(cc *c, dspec *ds, bool allow_sc);
static qty apply_declarator(cc *c, qty base, uint32_t first, uint32_t nops);

static bool is_type_start(cc *c, const ptok *t)
{
	switch (kwof(t)) {
	case KW_void: case KW_char: case KW_short: case KW_int: case KW_long: case KW_float:
	case KW_double: case KW_signed: case KW_unsigned: case KW__Bool: case KW__Complex:
	case KW_struct: case KW_union: case KW_enum: case KW_const: case KW_volatile:
	case KW_restrict: case KW__Atomic: case KW_typeof: case KW___typeof__: case KW___typeof:
	case KW___typeof_unqual__: case KW___const__: case KW___const: case KW___signed__:
	case KW___signed: case KW___volatile__: case KW___volatile: case KW___restrict__:
	case KW___restrict: case KW___int128: case KW__Float16: case KW___fp16: case KW__Alignas:
	case KW___attribute__: case KW___attribute: case KW___auto_type:
		return true;
	case 0:
		return is_typedef_name(c, t);
	default:
		return false;
	}
}

static bool is_decl_start(cc *c, const ptok *t)
{
	switch (kwof(t)) {
	case KW_typedef: case KW_extern: case KW_static: case KW_auto: case KW_register:
	case KW_inline: case KW___inline__: case KW___inline: case KW__Noreturn: case KW__Thread_local:
	case KW___thread: case KW__Static_assert:
		return true;
	default:
		return is_type_start(c, t);
	}
}

static uint32_t tag_new(cc *c, int kind, uint32_t name, uint32_t loc)
{
	uint32_t ti = c->ntag++, t;
	tag *r = &c->tags[ti];

	memset(r, 0, sizeof(*r));
	r->name = name;
	r->loc = loc;
	t = ty_new(c, kind);
	c->ty[t].aux = ti;
	c->ty[t].align = 1;
	if (kind == TY_ENUM) {
		c->ty[t].base = prim(TY_UINT);
		c->ty[t].size = 4;
		c->ty[t].align = 4;
		r->flags |= RF_ENUM;
	}
	if (kind == TY_UNION)
		r->flags |= RF_UNION;
	r->ty = t;
	return ti;
}

/* tag binding for "struct S" (define: in the current scope only) */
static uint32_t tag_lookup_or_new(cc *c, int kind, uint32_t name, uint32_t loc, bool define, bool bare_decl)
{
	uint32_t s = name ? lookup_tag(c, name) : 0;

	if (s) {
		sym *y = &c->syms[s];
		uint32_t ti = (uint32_t)y->val;
		int k = c->ty[c->tags[ti].ty].kind;

		if ((define || bare_decl) && y->depth != c->depth)
			s = 0;	/* a new tag in this scope shadows the outer one */
		else {
			if (k != kind)
				error(c, loc, "use of '%.*s' with tag type that does not match previous declaration", IDF(name));
			return ti;
		}
	}
	{
		uint32_t ti = tag_new(c, kind, name, loc);

		if (name) {
			uint32_t ns = sym_new(c, S_TAG, name, QT(c->tags[ti].ty, 0), loc);

			c->syms[ns].val = ti;
			bind(c, ns);
		}
		return ti;
	}
}

static void static_assert_decl(cc *c)
{
	uint32_t loc = tok(c)->loc, n;
	i128 v;

	next(c);
	expect(c, T_LPAREN, "'('");
	n = rvalue(c, assign_expr(c));
	if (!const_int(c, n, &v)) {
		error(c, loc, "static assertion expression is not an integral constant expression");
		v = 1;
	}
	if (accept(c, T_COMMA)) {
		if (!v) {
			char b[256];

			tokspell(tok(c), b, sizeof(b));
			error(c, loc, "static assertion failed: %s", b);
		}
		while (is_strtok(tok(c)->kind))
			next(c);
	} else if (!v) {
		error(c, loc, "static assertion failed");
	}
	expect(c, T_RPAREN, "')'");
	expect(c, T_SEMI, "';'");
}

typedef struct dop {
	uint8_t kind;		/* 0 ptr, 1 array, 2 func */
	uint8_t q;		/* ptr quals / array param quals */
	uint16_t fl;		/* array: TF_*; func: TF_* */
	uint32_t a, b;		/* array: length lo/hi or vla node; func: params start, count */
} dop;

typedef struct declr {
	uint32_t name, loc;
	attrs at;
	uint32_t psyms, npsyms;	/* the outermost function declarator's parameter syms (list) */
	bool has_func;
} declr;

/* the declarator op stack (shared, nested declarators push and pop) */
#define DOPS_MAX 4096

static void struct_body(cc *c, uint32_t ti);
static void enum_body(cc *c, uint32_t ti, attrs *after);
static void declarator(cc *c, declr *d, bool abstract_ok, uint32_t *first, uint32_t *nops);

static _Thread_local dop *tl_dops;
static _Thread_local uint32_t tl_ndops;

static qty int_type(cc *c, int nlong, int sgn, int base, uint32_t loc)
{
	(void)c;
	switch (base) {
	case KW_char:
		return prim(sgn == 0 ? TY_CHAR : sgn > 0 ? TY_SCHAR : TY_UCHAR);
	case KW_short:
		return prim(sgn < 0 ? TY_USHORT : TY_SHORT);
	case KW___int128:
		return prim(sgn < 0 ? TY_UINT128 : TY_INT128);
	default:
		if (nlong == 1)
			return prim(sgn < 0 ? TY_ULONG : TY_LONG);
		if (nlong >= 2) {
			if (nlong > 2)
				error(c, loc, "'long long long' is invalid");
			return prim(sgn < 0 ? TY_ULLONG : TY_LLONG);
		}
		return prim(sgn < 0 ? TY_UINT : TY_INT);
	}
}

static qty typeof_spec(cc *c, bool unqual)
{
	qty t;

	next(c);
	expect(c, T_LPAREN, "'('");
	if (is_type_start(c, tok(c))) {
		t = type_name(c);
	} else {
		uint32_t n;

		c->unevaluated++;
		n = expr(c);
		c->unevaluated--;
		t = N(n)->ty;
	}
	expect(c, T_RPAREN, "')'");
	return unqual ? QUNQ(t) : t;
}

static bool decl_specs(cc *c, dspec *ds, bool allow_sc)
{
	int nlong = 0, sgn = 0, base = 0, q = 0;
	bool any = false, cplx = false;
	qty named = 0;
	bool have_named = false;

	memset(ds, 0, sizeof(*ds));
	ds->loc = tok(c)->loc;
	if (c->has_pend) {
		ds->at = c->pend;
		c->has_pend = false;
	}
	for (;;) {
		ptok *t = tok(c);
		int kw = kwof(t);

		if (!kw) {
			if (is_nullability(c, t)) {
				next(c);
				continue;
			}
			if (!have_named && !base && !nlong && !sgn && is_typedef_name(c, t)) {
				uint32_t s = lookup(c, t->id);

				named = c->syms[s].ty;
				have_named = true;
				any = true;
				next(c);
				continue;
			}
			break;
		}
		switch (kw) {
		case KW_typedef: case KW_extern: case KW_static: case KW_auto: case KW_register: {
			int sc = kw == KW_typedef ? SC_TYPEDEF : kw == KW_extern ? SC_EXTERN : kw == KW_static ? SC_STATIC :
				 kw == KW_auto ? SC_AUTO : SC_REGISTER;

			if (!allow_sc)
				error(c, t->loc, "invalid storage class specifier");
			if (ds->sc && ds->sc != sc)
				error(c, t->loc, "cannot combine with previous storage class specifier");
			ds->sc = sc;
			break;
		}
		case KW_inline: case KW___inline__: case KW___inline:
			ds->inline_ = true;
			break;
		case KW__Noreturn:
			ds->noreturn = true;
			break;
		case KW__Thread_local: case KW___thread:
			ds->thread = true;
			break;
		case KW_const: case KW___const__: case KW___const:
			q |= Q_CONST;
			break;
		case KW_volatile: case KW___volatile__: case KW___volatile:
			q |= Q_VOLATILE;
			break;
		case KW_restrict: case KW___restrict__: case KW___restrict:
			q |= Q_RESTRICT;
			break;
		case KW__Atomic:
			next(c);
			if (is(c, T_LPAREN)) {
				next(c);
				named = type_name(c);
				have_named = true;
				expect(c, T_RPAREN, "')'");
				any = true;
			}
			continue;
		case KW___attribute__: case KW___attribute:
			attribute_list(c, &ds->at);
			continue;
		case KW___extension__:
			break;
		case KW__Alignas: {
			uint32_t a;

			next(c);
			expect(c, T_LPAREN, "'('");
			if (is_type_start(c, tok(c))) {
				a = ty_align(c, type_name(c));
			} else {
				i128 v;

				const_expr(c, &v, NULL);
				a = (uint32_t)v;
			}
			expect(c, T_RPAREN, "')'");
			if (a > ds->align)
				ds->align = a;
			continue;
		}
		case KW_void: case KW__Bool: case KW_char: case KW_short: case KW_int: case KW_float:
		case KW_double: case KW___int128: case KW__Float16: case KW___fp16:
			if (have_named)
				goto done;
			if (base && !(base == KW_int && (kw == KW_short || kw == KW_char)) && !(kw == KW_int && (base == KW_short || base == KW_char)) &&
			    !(base == KW_double && kw == KW_double))
				error(c, t->loc, "cannot combine with previous type specifier");
			if (kw == KW_int && base)
				break;
			base = kw;
			any = true;
			break;
		case KW_long:
			if (have_named)
				goto done;
			nlong++;
			any = true;
			break;
		case KW_signed: case KW___signed__: case KW___signed:
			if (have_named)
				goto done;
			sgn = 1;
			any = true;
			break;
		case KW_unsigned:
			if (have_named)
				goto done;
			sgn = -1;
			any = true;
			break;
		case KW__Complex:
			cplx = true;
			any = true;
			break;
		case KW___auto_type:
			ds->auto_type = true;
			any = true;
			have_named = true;
			named = prim(TY_INT);
			break;
		case KW_struct: case KW_union: case KW_enum: {
			int kind = kw == KW_struct ? TY_STRUCT : kw == KW_union ? TY_UNION : TY_ENUM;
			uint32_t loc = t->loc, name = 0, ti;
			attrs pre = { 0 }, post = { 0 };
			bool body;

			if (have_named || base)
				goto done;
			next(c);
			attribute_list(c, &pre);
			if (is(c, T_IDENT) && !kwof(tok(c))) {
				name = tok(c)->id;
				next(c);
			}
			body = is(c, T_LBRACE) || (kind == TY_ENUM && is(c, T_COLON));
			ti = tag_lookup_or_new(c, kind, name, loc, body, !body && is(c, T_SEMI) && !c->in_struct);
			if (body) {
				tag *r = &c->tags[ti];

				if (r->flags & RF_COMPLETE)
					error(c, loc, "redefinition of '%.*s'", IDF(name));
				if (kind == TY_ENUM) {
					merge_attrs(&post, &pre);
					enum_body(c, ti, &post);
				} else {
					c->tags[ti].flags |= (pre.packed ? RF_PACKED : 0);
					c->tags[ti].maxalign = pre.aligned;
					struct_body(c, ti);
				}
			} else {
				ds->tag_bare = true;
			}
			ds->tagdecl = ti;
			named = QT(c->tags[ti].ty, 0);
			have_named = true;
			any = true;
			continue;
		}
		case KW_typeof: case KW___typeof__: case KW___typeof: case KW___typeof_unqual__:
			if (have_named || base)
				goto done;
			named = typeof_spec(c, kw == KW___typeof_unqual__);
			have_named = true;
			any = true;
			continue;
		default:
			goto done;
		}
		next(c);
	}
done:
	ds->has_type = any;
	if (!any && !q && !ds->sc && !ds->inline_ && !ds->noreturn && !ds->thread && !ds->align)
		return false;
	if (have_named) {
		ds->ty = named;
	} else if (base == KW_void) {
		ds->ty = prim(TY_VOID);
	} else if (base == KW__Bool) {
		ds->ty = prim(TY_BOOL);
	} else if (base == KW_float) {
		ds->ty = prim(TY_FLOAT);
	} else if (base == KW__Float16 || base == KW___fp16) {
		ds->ty = prim(TY_FLOAT16);
	} else if (base == KW_double) {
		ds->ty = prim(nlong ? TY_LDOUBLE : TY_DOUBLE);
	} else {
		if (!any && !pp_in_system_header(c->p, ds->loc))
			error(c, ds->loc, "type specifier missing, defaults to 'int'; ISO C99 and later do not support implicit int");
		ds->ty = int_type(c, nlong, sgn, base, ds->loc);
	}
	(void)cplx;
	if (ds->at.mode) {
		int k = mode_kind(c, ds->at.mode, is_signed(c, ds->ty));

		if (k)
			ds->ty = prim(k);
	}
	ds->ty = qualify(c, ds->ty, q);
	return true;
}

/* ---------------- declarators ---------------- */

static void param_list(cc *c, dop *op)
{
	vec ptys, psyms;
	int fl = 0;

	vinit(&ptys);
	vinit(&psyms);
	scope_open(c);
	if (is(c, T_RPAREN)) {
		fl |= TF_NOPROTO;
	} else if (iskw(c, KW_void) && peek(c, 1)->kind == T_RPAREN) {
		next(c);
	} else {
		for (;;) {
			dspec ds;
			declr d;
			uint32_t first, nops, s;
			qty t;

			if (accept(c, T_ELLIPSIS)) {
				fl |= TF_VARIADIC;
				break;
			}
			if (!decl_specs(c, &ds, true)) {
				/* clang: an implicit-int parameter is only an error outside system headers */
				if (is(c, T_IDENT) && !kwof(tok(c)) && pp_in_system_header(c->p, tok(c)->loc)) {
					memset(&ds, 0, sizeof(ds));
					ds.ty = prim(TY_INT);
					ds.loc = tok(c)->loc;
				} else if (is(c, T_IDENT)) {
					fatal(c, tok(c)->loc, "unknown type name '%.*s'", IDF(tok(c)->id));
				} else {
					fatal(c, tok(c)->loc, "expected parameter declarator");
				}
			}
			declarator(c, &d, true, &first, &nops);
			attribute_list(c, &d.at);
			t = apply_declarator(c, ds.ty, first, nops);
			tl_ndops = first;
			/* adjustments (C11 6.7.6.3p7-8) */
			if (is_array(c, t)) {
				const dop *ad = nops ? &tl_dops[first + nops - 1] : NULL;
				int q = ad && ad->kind == 1 ? ad->q : 0;

				t = QT(QI(ty_ptr(c, T(c, t)->base)), q);
			} else if (is_func(c, t)) {
				t = ty_ptr(c, t);
			}
			vpush(c, &ptys, t);
			s = sym_new(c, S_VAR, d.name, t, d.loc ? d.loc : ds.loc);
			c->syms[s].flags |= SF_PARAM;
			c->syms[s].sc = ds.sc == SC_REGISTER ? SC_REGISTER : SC_AUTO;
			bind(c, s);
			vpush(c, &psyms, s);
			if (!accept(c, T_COMMA))
				break;
		}
	}
	scope_close(c);
	expect(c, T_RPAREN, "')'");
	op->kind = 2;
	op->fl = (uint16_t)fl;
	op->a = vfreeze(c, &ptys);
	op->b = ptys.n;
	/* the parameter syms follow the types */
	vfreeze(c, &psyms);
}

static void array_suffix(cc *c, dop *op)
{
	int q = 0;

	op->kind = 1;
	op->fl = 0;
	for (;;) {
		int kw = kwof(tok(c));

		if (kw == KW_static) {
			next(c);
		} else if (kw == KW_const || kw == KW___const || kw == KW___const__) {
			q |= Q_CONST, next(c);
		} else if (kw == KW_volatile || kw == KW___volatile__ || kw == KW___volatile) {
			q |= Q_VOLATILE, next(c);
		} else if (kw == KW_restrict || kw == KW___restrict || kw == KW___restrict__) {
			q |= Q_RESTRICT, next(c);
		} else if (is_nullability(c, tok(c))) {
			next(c);
		} else {
			break;
		}
	}
	op->q = (uint8_t)q;
	if (accept(c, T_RSQUARE)) {
		op->fl = TF_INCOMPLETE;
		return;
	}
	if (is(c, T_STAR) && peek(c, 1)->kind == T_RSQUARE) {
		next(c), next(c);
		op->fl = TF_VLA;
		op->a = 0;
		return;
	}
	{
		uint32_t n = rvalue(c, assign_expr(c));
		i128 v;

		expect(c, T_RSQUARE, "']'");
		if (!is_integer(c, N(n)->ty)) {
			error(c, N(n)->loc, "size of array has non-integer type");
			v = 1;
		} else if (!const_int(c, n, &v)) {
			op->fl = TF_VLA;
			op->a = n;
			return;
		}
		if (v < 0) {
			error(c, N(n)->loc, "array has negative size");
			v = 1;
		}
		op->a = (uint32_t)(uint64_t)v;
		op->b = (uint32_t)((uint64_t)v >> 32);
	}
}

static bool nested_declarator(cc *c, bool abstract_ok)
{
	const ptok *t = peek(c, 1);
	int kw = kwof(t);

	if (t->kind == T_STAR || t->kind == T_CARET)
		return true;
	if (t->kind == T_LPAREN)
		return true;	/* "((": only a nested declarator (an abstract one or a named one) */
	if (t->kind == T_LSQUARE)
		return abstract_ok;
	if (kw == KW___attribute__ || kw == KW___attribute)
		return true;
	if (t->kind == T_IDENT && !kw && !is_typedef_name(c, t))
		return true;
	return false;
}

/*
 * Parse a declarator into the op stack; ops [*first, *first + *nops) in the
 * order they apply to the base type (pointers, suffixes right to left, then
 * the nested declarator's ops).
 */
static void declarator(cc *c, declr *d, bool abstract_ok, uint32_t *first, uint32_t *nops)
{
	dop ptrs[64], sufs[64];
	int np = 0, ns = 0;
	uint32_t ifirst = 0, inops = 0;
	bool inner = false;
	declr id;

	memset(d, 0, sizeof(*d));
	d->loc = tok(c)->loc;
	for (;;) {
		if (is(c, T_STAR) || is(c, T_CARET)) {	/* ^: a block pointer (Darwin SDK prototypes) */
			int q = 0, blk = is(c, T_CARET);

			next(c);

			for (;;) {
				int kw = kwof(tok(c));

				if (kw == KW_const || kw == KW___const || kw == KW___const__)
					q |= Q_CONST;
				else if (kw == KW_volatile || kw == KW___volatile__ || kw == KW___volatile)
					q |= Q_VOLATILE;
				else if (kw == KW_restrict || kw == KW___restrict || kw == KW___restrict__)
					q |= Q_RESTRICT;
				else if (kw == KW__Atomic || is_nullability(c, tok(c)))
					;
				else if (kw == KW___attribute__ || kw == KW___attribute) {
					attribute_list(c, &d->at);
					continue;
				} else
					break;
				next(c);
			}
			if (np >= 64)
				fatal(c, tok(c)->loc, "declarator too complex");
			ptrs[np++] = (dop){ .kind = 0, .q = (uint8_t)q, .fl = (uint16_t)(blk ? TF_BLOCK : 0) };
			continue;
		}
		if (iskw(c, KW___attribute__) || iskw(c, KW___attribute)) {
			attribute_list(c, &d->at);
			continue;
		}
		break;
	}
	if (is(c, T_LPAREN) && nested_declarator(c, abstract_ok)) {
		next(c);
		attribute_list(c, &d->at);
		declarator(c, &id, abstract_ok, &ifirst, &inops);
		expect(c, T_RPAREN, "')'");
		d->name = id.name;
		d->loc = id.loc;
		merge_attrs(&d->at, &id.at);
		if (id.has_func) {
			d->psyms = id.psyms, d->npsyms = id.npsyms, d->has_func = true;
		}
		inner = true;
	} else if (is(c, T_IDENT) && !kwof(tok(c))) {
		d->name = tok(c)->id;
		d->loc = tok(c)->loc;
		next(c);
	} else if (!abstract_ok) {
		char b[64];

		fatal(c, tok(c)->loc, "expected identifier or '(' before '%s'", tokspell(tok(c), b, sizeof(b)));
	}
	attribute_list(c, &d->at);
	for (;;) {
		if (accept(c, T_LSQUARE)) {
			if (ns >= 64)
				fatal(c, tok(c)->loc, "declarator too complex");
			array_suffix(c, &sufs[ns++]);
		} else if (is(c, T_LPAREN)) {
			next(c);
			if (ns >= 64)
				fatal(c, tok(c)->loc, "declarator too complex");
			param_list(c, &sufs[ns]);
			/* the innermost function declarator next to the name gives the parameters */
			if (!inner && !d->has_func && ns == 0) {
				d->psyms = sufs[ns].a + sufs[ns].b;
				d->npsyms = sufs[ns].b;
				d->has_func = true;
			}
			ns++;
		} else {
			break;
		}
		attribute_list(c, &d->at);
	}
	/* assemble: pointers, suffixes reversed, inner ops */
	{
		uint32_t at = tl_ndops;
		uint32_t n = (uint32_t)np + (uint32_t)ns + inops;

		if (at + n > DOPS_MAX)
			fatal(c, tok(c)->loc, "declarator too complex");
		/* the inner ops sit at [ifirst, ifirst + inops) which is below at */
		if (inner) {
			dop tmp[inops ? inops : 1];

			memcpy(tmp, &tl_dops[ifirst], inops * sizeof(dop));
			at = ifirst;
			for (int i = 0; i < np; i++)
				tl_dops[at + (uint32_t)i] = ptrs[i];
			for (int i = 0; i < ns; i++)
				tl_dops[at + (uint32_t)np + (uint32_t)i] = sufs[ns - 1 - i];
			memcpy(&tl_dops[at + (uint32_t)np + (uint32_t)ns], tmp, inops * sizeof(dop));
		} else {
			for (int i = 0; i < np; i++)
				tl_dops[at + (uint32_t)i] = ptrs[i];
			for (int i = 0; i < ns; i++)
				tl_dops[at + (uint32_t)np + (uint32_t)i] = sufs[ns - 1 - i];
		}
		*first = at;
		*nops = n;
		tl_ndops = at + n;
	}
}

static qty apply_declarator(cc *c, qty t, uint32_t first, uint32_t nops)
{
	for (uint32_t i = 0; i < nops; i++) {
		const dop *o = &tl_dops[first + i];

		switch (o->kind) {
		case 0:
			if (o->fl & TF_BLOCK) {
				ctype b = { .kind = TY_PTR, .flags = TF_BLOCK, .size = 8, .align = 8, .base = t };

				t = QT(ty_intern(c, &b), o->q);
			} else {
				t = QT(QI(ty_ptr(c, t)), o->q);
			}
			break;
		case 1:
			if (is_func(c, t))
				error(c, tok(c)->loc, "array of functions is not allowed");
			if (o->fl & TF_VLA)
				t = ty_vla(c, t, o->a);
			else
				t = ty_array(c, t, (uint64_t)o->a | (uint64_t)o->b << 32, o->fl);
			break;
		case 2:
			if (is_array(c, t) || is_func(c, t))
				error(c, tok(c)->loc, "function cannot return %s type", is_array(c, t) ? "array" : "function");
			t = ty_func(c, QUNQ(t), &c->lists[o->a], o->b, o->fl);
			break;
		}
	}
	return t;
}

static qty type_name(cc *c)
{
	dspec ds;
	declr d;
	uint32_t first, nops;
	qty t;

	if (!decl_specs(c, &ds, false))
		fatal(c, tok(c)->loc, "expected a type");
	declarator(c, &d, true, &first, &nops);
	t = apply_declarator(c, ds.ty, first, nops);
	tl_ndops = first;
	if (d.name)
		error(c, d.loc, "unexpected identifier in type name");
	return t;
}

/* ---------------- struct / union / enum bodies ---------------- */

static void struct_body(cc *c, uint32_t ti)
{
	uint32_t *fa = malloc(4096 * sizeof(uint32_t));
	uint8_t *fp = malloc(4096);
	field *fs = malloc(4096 * sizeof(field));
	uint32_t nf = 0;
	tag *r;
	attrs post = { 0 };

	c->tags[ti].flags |= RF_BEING_DEFINED;
	c->in_struct++;
	expect(c, T_LBRACE, "'{'");
	while (!is(c, T_RBRACE)) {
		dspec ds;

		if (accept(c, T_SEMI))
			continue;
		if (iskw(c, KW__Static_assert)) {
			static_assert_decl(c);
			continue;
		}
		if (!decl_specs(c, &ds, false)) {
			if (is(c, T_IDENT))
				fatal(c, tok(c)->loc, "unknown type name '%.*s'", IDF(tok(c)->id));
			fatal(c, tok(c)->loc, "expected member declaration");
		}
		if (is(c, T_SEMI)) {
			/* anonymous struct/union member; with -fms-anonymous-structs also a
			 * tagged or typedef'd complete record */
			if (is_record(c, ds.ty) && (!c->tags[T(c, ds.ty)->aux].name ||
						    (c->o->ms_anon && complete(c, ds.ty)))) {
				fs[nf] = (field){ .ty = ds.ty, .loc = ds.loc };
				fa[nf] = ds.at.aligned > ds.align ? ds.at.aligned : ds.align;
				fp[nf] = ds.at.packed;
				nf++;
			}
			next(c);
			continue;
		}
		for (;;) {
			declr d;
			uint32_t first = 0, nops = 0;
			qty t = ds.ty;
			field f = { 0 };

			if (!is(c, T_COLON)) {
				declarator(c, &d, false, &first, &nops);
				t = apply_declarator(c, ds.ty, first, nops);
				tl_ndops = first;
			} else {
				memset(&d, 0, sizeof(d));
				d.loc = tok(c)->loc;
			}
			f.name = d.name;
			f.loc = d.loc;
			if (accept(c, T_COLON)) {
				i128 w;

				const_expr(c, &w, NULL);
				if (!is_integer(c, t))
					error(c, d.loc, "bit-field has non-integral type");
				if (w < 0)
					error(c, d.loc, "bit-field has negative width (%lld)", (long long)w), w = 0;
				else if ((uint64_t)w > ty_size(c, t) * 8)
					error(c, d.loc, "width of bit-field (%lld bits) exceeds the width of its type", (long long)w), w = 0;
				else if (w == 0 && d.name)
					error(c, d.loc, "named bit-field has zero width");
				f.bitfield = 1;
				f.width = (uint16_t)w;
			}
			attribute_list(c, &d.at);
			merge_attrs(&d.at, &ds.at);
			if (d.at.mode) {
				int k = mode_kind(c, d.at.mode, is_signed(c, t));

				if (k)
					t = prim(k);
			}
			f.ty = t;
			f.counted_by = d.at.counted_by;
			if (is_func(c, t))
				error(c, d.loc, "field has function type");
			else if (!complete(c, t) && !(is_array(c, t) && (T(c, t)->flags & TF_INCOMPLETE)))
				error(c, d.loc, "field has incomplete type");
			if (nf >= 4096)
				fatal(c, d.loc, "too many members");
			fa[nf] = d.at.aligned > ds.align ? d.at.aligned : ds.align;
			fp[nf] = d.at.packed;
			fs[nf++] = f;
			if (!accept(c, T_COMMA))
				break;
		}
		expect(c, T_SEMI, "';'");
	}
	next(c);
	c->in_struct--;
	attribute_list(c, &post);
	r = &c->tags[ti];
	r->f = halloc(c, (nf + 1) * sizeof(field));
	memcpy(r->f, fs, nf * sizeof(field));
	free(fs);
	fs = r->f;
	r->nfield = nf;
	if (post.packed)
		r->flags |= RF_PACKED;
	if (post.aligned > r->maxalign)
		r->maxalign = post.aligned;
	if (post.transparent)
		c->ty[r->ty].flags |= TF_TRANSPARENT;
	for (uint32_t i = 0; i < nf; i++)
		if (is_array(c, fs[i].ty) && (T(c, fs[i].ty)->flags & TF_INCOMPLETE)) {
			if (i + 1 < nf)
				;	/* GNU: flexible array not at the end */
			r->flags |= RF_FLEX;
		}
	lay_record(c, r, fa, fp);
	r->flags = (r->flags | RF_COMPLETE) & ~(uint32_t)RF_BEING_DEFINED;
	free(fa);
	free(fp);
}

static void enum_body(cc *c, uint32_t ti, attrs *after)
{
	i128 v = -1, mn = 0, mx = 0;
	bool any = false;
	vec cs;
	qty fixed = 0;

	vinit(&cs);
	if (accept(c, T_COLON)) {
		fixed = type_name(c);
		c->ty[c->tags[ti].ty].base = QUNQ(fixed);
	}
	expect(c, T_LBRACE, "'{'");
	while (!is(c, T_RBRACE)) {
		uint32_t name, loc, s;
		attrs ea = { 0 };

		if (!is(c, T_IDENT) || kwof(tok(c)))
			fatal(c, tok(c)->loc, "expected identifier");
		name = tok(c)->id;
		loc = tok(c)->loc;
		next(c);
		attribute_list(c, &ea);
		if (accept(c, T_EQUAL))
			const_expr(c, &v, NULL);
		else
			v++;
		if (!any || v < mn)
			mn = v;
		if (!any || v > mx)
			mx = v;
		any = true;
		s = sym_new(c, S_ENUMCONST, name, v >= INT32_MIN && v <= INT32_MAX ? prim(TY_INT) :
						 v >= 0 && v <= UINT32_MAX ? prim(TY_UINT) : v >= INT64_MIN && v <= INT64_MAX ? prim(TY_LONG) : prim(TY_ULONG), loc);
		c->syms[s].val = (int64_t)v;
		bind(c, s);
		vpush(c, &cs, s);
		if (!accept(c, T_COMMA))
			break;
	}
	expect(c, T_RBRACE, "'}'");
	attribute_list(c, after);
	{
		qty best;
		ctype *et = &c->ty[c->tags[ti].ty];

		if (fixed) {
			best = QUNQ(fixed);
		} else if (after->packed) {
			if (mn < 0)
				best = mn >= INT8_MIN && mx <= INT8_MAX ? prim(TY_SCHAR) : mn >= INT16_MIN && mx <= INT16_MAX ? prim(TY_SHORT) :
				       mn >= INT32_MIN && mx <= INT32_MAX ? prim(TY_INT) : prim(TY_LONG);
			else
				best = mx <= UINT8_MAX ? prim(TY_UCHAR) : mx <= UINT16_MAX ? prim(TY_USHORT) :
				       mx <= UINT32_MAX ? prim(TY_UINT) : prim(TY_ULONG);
		} else if (mn < 0) {
			best = mn >= INT32_MIN && mx <= INT32_MAX ? prim(TY_INT) : prim(TY_LONG);
		} else {
			best = mx <= UINT32_MAX ? prim(TY_UINT) : prim(TY_ULONG);
		}
		et->base = best;
		et->size = ty_size(c, best);
		et->align = ty_align(c, best);
		c->tags[ti].flags |= RF_COMPLETE;
		c->tags[ti].size = et->size;
		c->tags[ti].align = et->align;
		/* clang: if every value fits int the enumerators are int, else all of
		 * them have the enumerated type */
		for (uint32_t i = 0; i < cs.n; i++) {
			sym *y = &c->syms[cs.v[i]];

			if (fixed || mn < INT32_MIN || mx > INT32_MAX)
				y->ty = QT(c->tags[ti].ty, 0);
			else
				y->ty = prim(TY_INT);
		}
	}
}

/* ---------------- initializers ---------------- */

typedef struct irun {
	uint32_t lo, hi;	/* element range */
	uint32_t node;		/* value (0 while sub is being built) */
	struct ilist *sub;
} irun;

typedef struct ilist {
	qty ty;
	bool array, isunion, unknown;
	uint64_t n;		/* fields, or array length (unknown: grows) */
	uint64_t max;		/* array: highest index initialized + 1 */
	uint32_t *slot;		/* struct/union: value per field */
	struct ilist **sub;	/* struct/union: sub-list per field */
	irun *runs;		/* array: in order, later runs override earlier ones */
	uint32_t nrun, caprun;
	uint32_t ufield;	/* union: the initialized field */
	bool uset;
} ilist;

static ilist *il_new(cc *c, qty ty)
{
	ilist *l = halloc(c, sizeof(ilist));

	memset(l, 0, sizeof(*l));
	l->ty = ty;
	if (is_vector(c, ty)) {
		l->array = true;
		l->n = T(c, ty)->n;
	} else if (is_array(c, ty)) {
		l->array = true;
		l->unknown = (T(c, ty)->flags & TF_INCOMPLETE) != 0;
		l->n = l->unknown ? 0 : arr_len(c, ty);
	} else {
		const tag *r = &c->tags[T(c, ty)->aux];

		l->isunion = (r->flags & RF_UNION) != 0;
		l->n = r->nfield;
		l->slot = halloc(c, (r->nfield + 1) * sizeof(uint32_t));
		l->sub = halloc(c, (r->nfield + 1) * sizeof(ilist *));
		memset(l->slot, 0, (r->nfield + 1) * sizeof(uint32_t));
		memset(l->sub, 0, (r->nfield + 1) * sizeof(ilist *));
	}
	return l;
}

static qty il_elem_type(cc *c, const ilist *l, uint64_t i)
{
	if (l->array)
		return T(c, l->ty)->base;
	return c->tags[T(c, l->ty)->aux].f[i].ty;
}

static void il_run(cc *c, ilist *l, uint64_t lo, uint64_t hi, uint32_t node, ilist *sub)
{
	if (l->nrun == l->caprun) {
		uint32_t ncap = l->caprun ? l->caprun * 2 : 8;
		irun *nr = halloc(c, ncap * sizeof(irun));

		if (l->nrun)
			memcpy(nr, l->runs, l->nrun * sizeof(irun));
		l->runs = nr;
		l->caprun = ncap;
	}
	l->runs[l->nrun++] = (irun){ (uint32_t)lo, (uint32_t)hi, node, sub };
	if (hi + 1 > l->max)
		l->max = hi + 1;
}

static void il_set(cc *c, ilist *l, uint64_t i, uint32_t node, ilist *sub)
{
	if (l->array) {
		il_run(c, l, i, i, node, sub);
		return;
	}
	if (l->isunion) {
		for (uint64_t k = 0; k < l->n; k++)
			l->slot[k] = 0, l->sub[k] = NULL;
		l->ufield = (uint32_t)i;
		l->uset = true;
	}
	l->slot[i] = node;
	l->sub[i] = sub;
}

/* the sub-list at i, created (replacing a plain value) if needed */
static ilist *il_child(cc *c, ilist *l, uint64_t i)
{
	qty et = il_elem_type(c, l, i);

	if (l->array) {
		for (uint32_t k = l->nrun; k-- > 0;)
			if (l->runs[k].lo <= i && i <= l->runs[k].hi) {
				if (l->runs[k].sub && l->runs[k].lo == i && l->runs[k].hi == i)
					return l->runs[k].sub;
				break;
			}
	} else if (l->sub[i] && (!l->isunion || l->ufield == i)) {
		return l->sub[i];
	}
	{
		ilist *s = il_new(c, et);

		il_set(c, l, i, 0, s);
		return s;
	}
}

static bool il_full(const ilist *l, uint64_t pos)
{
	if (l->array)
		return !l->unknown && pos >= l->n;
	if (l->isunion)
		return pos >= 1 || l->uset;
	return pos >= l->n;
}

/* skip unnamed bit-fields (they take no initializer) */
static uint64_t il_next_pos(cc *c, const ilist *l, uint64_t pos)
{
	if (!l->array) {
		const tag *r = &c->tags[T(c, l->ty)->aux];

		while (pos < r->nfield && r->f[pos].bitfield && !r->f[pos].name)
			pos++;
	}
	return pos;
}

static uint32_t il_finish(cc *c, ilist *l, uint32_t loc);
static void il_continue(cc *c, ilist *l, uint64_t pos, bool st);
static void il_fill(cc *c, ilist *l, uint64_t pos, bool braced, uint32_t first, bool st);
static uint32_t init_braced(cc *c, qty ty, bool st);

static bool is_char_array(cc *c, qty t)
{
	if (!is_array(c, t))
		return false;
	t = T(c, t)->base;
	return is_integer(c, t) && (ty_size(c, t) == 1 || ty_size(c, t) == 2 || ty_size(c, t) == 4) && KIND(c, t) != TY_BOOL;
}

static bool string_initializes(cc *c, qty arr, uint32_t v)
{
	uint32_t s = v;

	while (N(s)->flags & NF_PAREN && N(s)->op != N_STR)
		break;
	return N(s)->op == N_STR && is_char_array(c, arr) &&
	       c->strs[N(s)->a].esz == ty_size(c, T(c, arr)->base);
}

/* one element at pos: braces, a value, or brace elision into a sub-aggregate */
static void il_elem(cc *c, ilist *l, uint64_t pos, uint32_t v, bool st)
{
	qty et = il_elem_type(c, l, pos);

	if (!v && is(c, T_LBRACE)) {
		if (is_char_array(c, et) && is_strtok(peek(c, 1)->kind)) {	/* { "string" } */
			uint32_t sv;

			next(c);
			sv = string_lit(c);
			accept(c, T_COMMA);
			expect(c, T_RBRACE, "'}'");
			il_set(c, l, pos, sv, NULL);
			return;
		}
		if (is_ilagg(c, et)) {
			ilist *s = il_new(c, et);

			next(c);
			il_fill(c, s, 0, true, 0, st);
			expect(c, T_RBRACE, "'}'");
			il_set(c, l, pos, 0, s);
		} else {
			il_set(c, l, pos, init_braced(c, et, st), NULL);
		}
		return;
	}
	if (!v)
		v = assign_expr(c);
	if ((is_ilagg(c, et)) && !string_initializes(c, et, v) &&
	    !((is_record(c, et) || is_vector(c, et)) && compatible(c, N(v)->ty, et))) {
		ilist *s = il_new(c, et);

		il_fill(c, s, 0, false, v, st);
		il_set(c, l, pos, 0, s);
		return;
	}
	if (is_array(c, et)) {
		il_set(c, l, pos, v, NULL);	/* string literal */
		return;
	}
	il_set(c, l, pos, is_record(c, et) ? v : convert(c, decay(c, v), et), NULL);
}

/* after an element of an unbraced (elided) list: go on with the next one if it belongs here */
static void il_continue(cc *c, ilist *l, uint64_t pos, bool st)
{
	pos = il_next_pos(c, l, pos);
	if (il_full(l, pos) || !is(c, T_COMMA))
		return;
	{
		int k = peek(c, 1)->kind;

		if (k == T_RBRACE || k == T_PERIOD || k == T_LSQUARE)
			return;
	}
	next(c);
	il_fill(c, l, pos, false, 0, st);
}

/* a designation; returns the position after the top-level designated element */
static uint64_t il_designation(cc *c, ilist *l, bool st)
{
	uint64_t lo, hi;
	uint32_t path[16];
	int depth = 1;

	if (accept(c, T_PERIOD)) {
		uint32_t name;
		int d;

		if (!is(c, T_IDENT))
			fatal(c, tok(c)->loc, "expected a field designator");
		name = tok(c)->id;
		if (l->array) {
			error(c, tok(c)->loc, "field designator cannot initialize a non-struct, non-union type");
			next(c);
			return 0;
		}
		d = find_member(c, T(c, l->ty)->aux, name, path, 0);
		if (d < 0) {
			error(c, tok(c)->loc, "field designator '%.*s' does not refer to any field", IDF(name));
			next(c);
			skip_balanced(c);
			return l->n;
		}
		next(c);
		depth = d;
		lo = hi = path[0];
	} else {
		i128 a, b;

		expect(c, T_LSQUARE, "'['");
		const_expr(c, &a, NULL);
		b = a;
		if (accept(c, T_ELLIPSIS))
			const_expr(c, &b, NULL);
		expect(c, T_RSQUARE, "']'");
		if (!l->array) {
			error(c, tok(c)->loc, "array designator cannot initialize non-array type");
			return 0;
		}
		if (a < 0 || b < a || (!l->unknown && (uint64_t)b >= l->n)) {
			error(c, tok(c)->loc, "array designator index exceeds array bounds");
			return (uint64_t)(b < 0 ? 0 : b) + 1;
		}
		lo = (uint64_t)a, hi = (uint64_t)b;
	}
	/* path through anonymous members acts as nested designators */
	if (depth > 1) {
		ilist *s = l;

		for (int i = 0; i < depth - 1; i++)
			s = il_child(c, s, path[i]);
		{
			uint64_t last = path[depth - 1];

			if (is(c, T_PERIOD) || is(c, T_LSQUARE)) {
				ilist *t = il_child(c, s, last);
				uint64_t np = il_designation(c, t, st);

				il_continue(c, t, np, st);
			} else {
				accept(c, T_EQUAL);
				il_elem(c, s, last, 0, st);
				il_continue(c, s, last + 1, st);
			}
		}
		return lo + 1;
	}
	if (is(c, T_PERIOD) || is(c, T_LSQUARE)) {
		ilist *t = il_child(c, l, lo);
		uint64_t np = il_designation(c, t, st);

		il_continue(c, t, np, st);
		for (uint64_t i = lo + 1; i <= hi; i++)
			il_set(c, l, i, 0, t);	/* range of the same sub-object */
		return hi + 1;
	}
	if (!accept(c, T_EQUAL))
		accept(c, T_COLON);	/* old GNU "field: value" */
	if (lo == hi) {
		il_elem(c, l, lo, 0, st);
	} else {
		/* [lo ... hi] = v: one value for the whole range */
		qty et = il_elem_type(c, l, lo);
		uint32_t v;

		if (is(c, T_LBRACE)) {
			if (is_ilagg(c, et)) {
				ilist *s = il_new(c, et);

				next(c);
				il_fill(c, s, 0, true, 0, st);
				expect(c, T_RBRACE, "'}'");
				il_run(c, l, lo, hi, 0, s);
			} else {
				il_run(c, l, lo, hi, init_braced(c, et, st), NULL);
			}
		} else {
			v = assign_expr(c);
			if ((is_ilagg(c, et)) && !string_initializes(c, et, v) &&
			    !((is_record(c, et) || is_vector(c, et)) && compatible(c, N(v)->ty, et))) {
				ilist *s = il_new(c, et);

				il_fill(c, s, 0, false, v, st);
				il_run(c, l, lo, hi, 0, s);
			} else {
				il_run(c, l, lo, hi, is_array(c, et) || is_record(c, et) || is_vector(c, et) ? v : convert(c, decay(c, v), et), NULL);
			}
		}
	}
	return hi + 1;
}

static void il_fill(cc *c, ilist *l, uint64_t pos, bool braced, uint32_t first, bool st)
{
	pos = il_next_pos(c, l, pos);
	if (first) {
		if (il_full(l, pos)) {
			error(c, N(first)->loc, "excess elements in initializer");
			return;
		}
		il_elem(c, l, pos, first, st);
		pos = il_next_pos(c, l, pos + 1);
		if (!braced) {
			if (il_full(l, pos) || !is(c, T_COMMA) || peek(c, 1)->kind == T_RBRACE ||
			    peek(c, 1)->kind == T_PERIOD || peek(c, 1)->kind == T_LSQUARE)
				return;
			next(c);
		} else if (!accept(c, T_COMMA)) {
			return;
		}
	}
	for (;;) {
		if (is(c, T_RBRACE))
			return;
		if (is(c, T_PERIOD) || is(c, T_LSQUARE)) {
			if (!braced)
				return;
			pos = il_designation(c, l, st);
		} else {
			if (!braced && il_full(l, pos))
				return;
			if (il_full(l, pos)) {
				/* excess elements: clang warns and drops them */
				uint32_t v;

				if (is(c, T_LBRACE)) {
					skip_balanced(c);
				} else {
					v = assign_expr(c);
					(void)v;
				}
			} else {
				il_elem(c, l, pos, 0, st);
				pos++;
			}
		}
		pos = il_next_pos(c, l, pos);
		if (!braced) {
			if (il_full(l, pos) || !is(c, T_COMMA) || peek(c, 1)->kind == T_RBRACE ||
			    peek(c, 1)->kind == T_PERIOD || peek(c, 1)->kind == T_LSQUARE)
				return;
			next(c);
			continue;
		}
		if (!accept(c, T_COMMA))
			return;
	}
}

/* N_INIT: struct/union: aux 0, list of nfield values; array: aux 1, list of (lo, hi, value) */
static uint32_t il_finish(cc *c, ilist *l, uint32_t loc)
{
	uint32_t n;
	qty ty = l->ty;
	vec v;

	vinit(&v);
	if (l->array && l->unknown)
		ty = ty_array(c, T(c, ty)->base, l->max, 0);
	n = mk(c, N_INIT, ty, loc);
	if (l->array) {
		for (uint32_t i = 0; i < l->nrun; i++) {
			irun *r = &l->runs[i];
			uint32_t x = r->sub ? il_finish(c, r->sub, loc) : r->node;

			vpush(c, &v, r->lo);
			vpush(c, &v, r->hi);
			vpush(c, &v, x);
		}
		N(n)->aux = 1;
		N(n)->c = v.n / 3;
	} else {
		for (uint64_t i = 0; i < l->n; i++)
			vpush(c, &v, l->sub[i] ? il_finish(c, l->sub[i], loc) : l->slot[i]);
		N(n)->aux = l->isunion ? 2 : 0;
		N(n)->c = v.n;
		N(n)->a = l->ufield;
	}
	N(n)->b = vfreeze(c, &v);
	return n;
}

/* { ... } for a scalar: the first element */
static uint32_t init_braced(cc *c, qty ty, bool st)
{
	uint32_t v = 0, loc = tok(c)->loc;

	expect(c, T_LBRACE, "'{'");
	if (is(c, T_RBRACE)) {
		next(c);
		return mk(c, N_ZERO, QUNQ(ty), loc);
	}
	if (is(c, T_LBRACE))
		v = init_braced(c, ty, st);
	else
		v = convert(c, decay(c, assign_expr(c)), ty);
	while (accept(c, T_COMMA)) {
		if (is(c, T_RBRACE))
			break;
		if (is(c, T_LBRACE))
			skip_balanced(c);
		else
			assign_expr(c);
	}
	expect(c, T_RBRACE, "'}'");
	return v;
}

static uint32_t initializer(cc *c, qty *ty, bool st)
{
	uint32_t loc = tok(c)->loc;
	qty t = *ty;

	if (is(c, T_LBRACE)) {
		if (is_char_array(c, t) && is_strtok(peek(c, 1)->kind)) {
			uint32_t s;

			next(c);
			s = string_lit(c);
			accept(c, T_COMMA);
			expect(c, T_RBRACE, "'}'");
			if (T(c, t)->flags & TF_INCOMPLETE)
				*ty = QT(QI(ty_array(c, T(c, t)->base, c->strs[N(s)->a].n, 0)), QQ(t));
			return s;
		}
		if (is_ilagg(c, t)) {
			ilist *l;
			uint32_t n;

			if (is_record(c, t) && !complete(c, t)) {
				error(c, loc, "variable has incomplete type");
				skip_balanced(c);
				return mk(c, N_ZERO, t, loc);
			}
			l = il_new(c, QUNQ(t));
			next(c);
			il_fill(c, l, 0, true, 0, st);
			expect(c, T_RBRACE, "'}'");
			n = il_finish(c, l, loc);
			*ty = QT(QI(N(n)->ty), QQ(t));
			return n;
		}
		return init_braced(c, t, st);
	}
	{
		uint32_t v = assign_expr(c);

		if (is_array(c, t)) {
			if (!string_initializes(c, t, v)) {
				error(c, loc, "array initializer must be an initializer list or string literal");
				return v;
			}
			if (T(c, t)->flags & TF_INCOMPLETE)
				*ty = QT(QI(ty_array(c, T(c, t)->base, c->strs[N(v)->a].n, 0)), QQ(t));
			return v;
		}
		if (is_record(c, t)) {
			if (!compatible(c, N(v)->ty, t))
				error(c, loc, "initializing with an expression of incompatible type");
			return v;
		}
		return convert(c, decay(c, v), t);
	}
}

/* ---------------- declarations ---------------- */

static uint32_t new_attrs(cc *c, const attrs *a)
{
	uint32_t i = c->nattr++;

	c->attr[i] = *a;
	return i;
}

static void apply_sym_attrs(cc *c, sym *s, const attrs *a)
{
	if (s->attrs)
		merge_attrs(&c->attr[s->attrs], a);
	else
		s->attrs = new_attrs(c, a);
	if (a->aligned > s->align)
		s->align = a->aligned;
	if (a->weak) s->flags |= SF_WEAK;
	if (a->noreturn) s->flags |= SF_NORETURN;
	if (a->always_inline) s->flags |= SF_ALWAYS_INLINE;
	if (a->noinline) s->flags |= SF_NOINLINE;
	if (a->gnu_inline) s->flags |= SF_GNU_INLINE;
	if (a->used) s->flags |= SF_ATTR_USED;
	if (a->overloadable) s->flags |= SF_OVERLOAD;
}

/* ---------------- overloadable: Itanium mangling as clang does it for C ---------------- */

typedef struct mangler {
	char b[512];
	uint32_t n;
	qty sub[64];		/* substitution candidates, in order: canonical (qualified) types */
	uint32_t nsub;
	bool full;
} mangler;

static void mg_put(mangler *m, const char *s, size_t l)
{
	if (m->n + l >= sizeof(m->b)) {
		m->full = true;
		return;
	}
	memcpy(m->b + m->n, s, l);
	m->n += (uint32_t)l;
}

static void mg_s(mangler *m, const char *s) { mg_put(m, s, strlen(s)); }

static void mg_u(mangler *m, uint64_t v)
{
	char t[24];

	mg_put(m, t, (size_t)snprintf(t, sizeof(t), "%llu", (unsigned long long)v));
}

static void mg_src(mangler *m, uint32_t id)
{
	int l;
	const char *s = idname(id, &l);

	mg_u(m, (uint64_t)l);
	mg_put(m, s, (size_t)l);
}

/* S_, S0_, S1_ ... SZ_, S10_ for a type seen before; false if t is new */
static bool mg_subst(mangler *m, qty t)
{
	for (uint32_t i = 0; i < m->nsub; i++) {
		char t2[16], d[8];
		int k = 0, nd = 0;

		if (m->sub[i] != t)
			continue;
		t2[k++] = 'S';
		if (i)
			for (uint32_t v = i - 1;; v /= 36) {
				d[nd++] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"[v % 36];
				if (v < 36)
					break;
			}
		while (nd)
			t2[k++] = d[--nd];
		t2[k++] = '_';
		mg_put(m, t2, (size_t)k);
		return true;
	}
	return false;
}

static void mg_add(mangler *m, qty t)
{
	if (m->nsub < 64)
		m->sub[m->nsub++] = t;
}

static void mg_type(cc *c, mangler *m, qty t)
{
	static const char *const prim_code[] = {
		[TY_VOID] = "v", [TY_BOOL] = "b", [TY_CHAR] = "c", [TY_SCHAR] = "a", [TY_UCHAR] = "h",
		[TY_SHORT] = "s", [TY_USHORT] = "t", [TY_INT] = "i", [TY_UINT] = "j", [TY_LONG] = "l",
		[TY_ULONG] = "m", [TY_LLONG] = "x", [TY_ULLONG] = "y", [TY_INT128] = "n", [TY_UINT128] = "o",
		[TY_FLOAT16] = "DF16_", [TY_FLOAT] = "f", [TY_DOUBLE] = "d", [TY_LDOUBLE] = "e",
	};
	int q = QQ(t), k;
	const ctype *x;

	t = canon(c, t);	/* keeps the qualifiers */
	k = KIND(c, t);
	if (!q && k < TY_PTR) {
		mg_s(m, prim_code[k]);
		return;
	}
	if (mg_subst(m, t))
		return;
	if (q) {
		if (q & Q_RESTRICT) mg_s(m, "r");
		if (q & Q_VOLATILE) mg_s(m, "V");
		if (q & Q_CONST) mg_s(m, "K");
		mg_type(c, m, QUNQ(t));
		mg_add(m, t);
		return;
	}
	x = T(c, t);
	switch (k) {
	case TY_PTR:
		mg_s(m, "P");
		mg_type(c, m, x->base);
		break;
	case TY_ARRAY:
		mg_s(m, "A");
		if (!(x->flags & (TF_INCOMPLETE | TF_VLA)))
			mg_u(m, x->n);
		mg_s(m, "_");
		mg_type(c, m, x->base);
		break;
	case TY_FUNC:
		mg_s(m, "F");
		mg_type(c, m, x->base);
		if (!x->n && !(x->flags & TF_VARIADIC))
			mg_s(m, "v");
		for (uint32_t i = 0; i < x->n; i++)
			mg_type(c, m, QUNQ(c->params[x->aux + i]));
		if (x->flags & TF_VARIADIC)
			mg_s(m, "z");
		mg_s(m, "E");
		break;
	default: {	/* struct, union, enum */
		const tag *r = &c->tags[x->aux];
		uint32_t id = r->name ? r->name : r->typedef_name;

		if (id)
			mg_src(m, id);
		else
			mg_s(m, "Ut_");
		break;
	}
	}
	mg_add(m, t);
}

/* _Z[L]<name><params> as clang mangles an overloadable C function; 0 if it does not fit */
static uint32_t mangle_overload(cc *c, uint32_t s)
{
	const sym *y = &c->syms[s];
	const ctype *f = T(c, y->ty);
	mangler m;
	uint32_t si;
	char *keep;

	m.n = m.nsub = 0;
	m.full = false;
	mg_s(&m, y->sc == SC_STATIC ? "_ZL" : "_Z");
	mg_src(&m, y->name);
	if (!f->n && !(f->flags & TF_VARIADIC))
		mg_s(&m, "v");
	for (uint32_t i = 0; i < f->n; i++)
		mg_type(c, &m, QUNQ(c->params[f->aux + i]));
	if (f->flags & TF_VARIADIC)
		mg_s(&m, "z");
	if (m.full)
		return 0;
	keep = halloc(c, m.n + 1);
	memcpy(keep, m.b, m.n);
	keep[m.n] = 0;
	si = c->nstr++;
	c->strs[si] = (cstr){ keep, m.n + 1, 1, T_STRING };
	return si + 1;
}

/* give an overloadable function its mangled name (unless it has an asm label) */
static void name_overload(cc *c, uint32_t s)
{
	sym *y = &c->syms[s];
	attrs a;

	if (y->attrs && c->attr[y->attrs].asmname)
		return;
	memset(&a, 0, sizeof(a));
	a.asmname = mangle_overload(c, s);
	if (a.asmname)
		apply_sym_attrs(c, y, &a);
}

/* the file-scope entity name refers to, if any */
static uint32_t file_scope_sym(cc *c, uint32_t name)
{
	for (uint32_t s = lookup(c, name); s; s = c->syms[s].shadow)
		if (c->syms[s].depth == 0)
			return s;
	return 0;
}

/* the file-scope sym named s (the emitter: alias targets), or 0 */
uint32_t cc_global_named(cc *c, const char *s)
{
	size_t n = strlen(s);
	uint32_t id = vintern_find(s, n, vintern_hash(s, n));

	return id ? file_scope_sym(c, id) : 0;
}

static uint32_t declare(cc *c, const dspec *ds, declr *d, qty t, bool file)
{
	attrs at = d->at;
	uint32_t s, prev;
	int kind;

	merge_attrs(&at, &ds->at);
	if (ds->align > at.aligned)
		at.aligned = ds->align;
	if (d->at.mode && is_integer(c, t)) {
		int k = mode_kind(c, d->at.mode, is_signed(c, t));

		if (k)
			t = QT(k, QQ(t));
	}
	if (at.vecsize) {
		if (!is_arith(c, t) || ty_size(c, t) > at.vecsize || at.vecsize % ty_size(c, t))
			error(c, d->loc, "invalid vector element type");
		else
			t = QT(QI(ty_vector(c, t, at.vecsize)), QQ(t));
	}
	if (ds->sc == SC_TYPEDEF) {
		if (at.aligned)
			t = ty_aligned(c, t, at.aligned);
		if (at.transparent && KIND(c, t) == TY_UNION)
			c->ty[QI(t)].flags |= TF_TRANSPARENT;
		prev = lookup(c, d->name);
		if (prev && c->syms[prev].depth == c->depth && c->syms[prev].kind == S_TYPEDEF) {
			if (!compatible(c, c->syms[prev].ty, t) || QQ(c->syms[prev].ty) != QQ(t))
				error(c, d->loc, "typedef redefinition with different types");
			return prev;
		}
		s = sym_new(c, S_TYPEDEF, d->name, t, d->loc);
		c->syms[s].sc = SC_TYPEDEF;
		bind(c, s);
		if (is_record(c, t) || KIND(c, t) == TY_ENUM) {
			tag *r = &c->tags[T(c, t)->aux];

			if (!r->name && !r->typedef_name && file)
				r->typedef_name = d->name;
		}
		if (file)
			c->toplevel[c->ntoplevel++] = s;
		return s;
	}
	kind = is_func(c, t) ? S_FUNC : S_VAR;
	if (kind == S_VAR && is_void(c, t) && ds->sc != SC_EXTERN)
		error(c, d->loc, "variable has incomplete type 'void'");
	/* redeclarations: the same entity in the same scope, or a block-scope extern */
	prev = 0;
	{
		uint32_t p = lookup(c, d->name);
		bool same = p && c->syms[p].depth == c->depth;
		bool linkage = file || ds->sc == SC_EXTERN || kind == S_FUNC;

		if (same && linkage)
			prev = p;
		else if (same && c->syms[p].kind != S_TYPEDEF)
			error(c, d->loc, "redefinition of '%.*s'", IDF(d->name));
		else if (!file && linkage)
			prev = file_scope_sym(c, d->name);
	}
	if (prev && kind == S_FUNC && c->syms[prev].kind == S_FUNC &&
	    (at.overloadable || (c->syms[prev].flags & SF_OVERLOAD))) {
		/* overloadable: redeclare the member with this type, or add one to the set */
		uint32_t head = prev, q;

		for (q = head; q; q = c->syms[q].ovl)
			if (compatible(c, c->syms[q].ty, t))
				break;
		if (q) {
			prev = q;
		} else if (at.overloadable && (c->syms[head].flags & SF_OVERLOAD)) {
			sym *y;

			s = sym_new(c, kind, d->name, t, d->loc);
			y = &c->syms[s];
			y->sc = (uint8_t)ds->sc;
			y->depth = c->syms[head].depth;
			y->flags |= SF_GLOBAL;
			if (ds->inline_)
				y->flags |= SF_INLINE | (ds->sc != SC_EXTERN ? SF_INLINE_NOEXTERN : 0);
			if (ds->noreturn)
				y->flags |= SF_NORETURN;
			apply_sym_attrs(c, y, &at);
			for (q = head; c->syms[q].ovl; q = c->syms[q].ovl)
				;
			c->syms[q].ovl = s;
			if (file)
				c->toplevel[c->ntoplevel++] = s;
			name_overload(c, s);
			return s;
		} else {
			error(c, d->loc, at.overloadable ? "redeclaration of '%.*s' must not have the 'overloadable' attribute"
			      : "redeclaration of '%.*s' must have the 'overloadable' attribute", IDF(d->name));
			return head;
		}
	}
	if (prev) {
		sym *y = &c->syms[prev];

		if (y->kind != kind) {
			error(c, d->loc, "redefinition of '%.*s' as different kind of symbol", IDF(d->name));
		} else if (!compatible(c, y->ty, t)) {
			error(c, d->loc, "conflicting types for '%.*s'", IDF(d->name));
		} else {
			y->ty = QT(QI(composite(c, y->ty, t)), QQ(y->ty) | QQ(t));
			if (ds->sc == SC_STATIC && kind == S_FUNC)
				y->sc = SC_STATIC;
			if (ds->inline_)
				y->flags |= SF_INLINE | (ds->sc != SC_EXTERN ? SF_INLINE_NOEXTERN : 0);
			apply_sym_attrs(c, y, &at);
			if (!file && c->syms[prev].depth != c->depth) {
				/* block-scope extern: a local binding that names the global */
				s = sym_new(c, kind, d->name, y->ty, d->loc);
				c->syms[s].sc = SC_EXTERN;
				c->syms[s].next = prev;
				c->syms[s].flags = y->flags;
				bind(c, s);
				return s;
			}
			return prev;
		}
	}
	s = sym_new(c, kind, d->name, t, d->loc);
	{
		sym *y = &c->syms[s];

		y->sc = (uint8_t)(ds->sc == SC_TYPEDEF ? SC_NONE : ds->sc);
		if (file || ds->sc == SC_EXTERN || kind == S_FUNC)
			y->flags |= SF_GLOBAL;
		if (ds->inline_)
			y->flags |= SF_INLINE | (ds->sc != SC_EXTERN ? SF_INLINE_NOEXTERN : 0);
		if (ds->noreturn)
			y->flags |= SF_NORETURN;
		if (ds->thread)
			y->flags |= SF_THREAD;
		apply_sym_attrs(c, y, &at);
		if (!file && kind == S_VAR && ds->sc != SC_EXTERN && ds->sc != SC_STATIC && !y->sc)
			y->sc = SC_AUTO;
		if (!file && prev == 0 && (ds->sc == SC_EXTERN || kind == S_FUNC)) {
			/* first declaration of a global from block scope: also the file-scope entity */
		}
	}
	bind(c, s);
	if (file)
		c->toplevel[c->ntoplevel++] = s;
	if (kind == S_FUNC && at.overloadable)
		name_overload(c, s);
	return s;
}

static void function_def(cc *c, uint32_t s, const declr *d);
static uint32_t statement(cc *c);

/* a declaration; block-scope ones append N_DECL statements to out */
static void declaration(cc *c, vec *out, bool file)
{
	dspec ds;

	if (iskw(c, KW__Static_assert)) {
		static_assert_decl(c);
		return;
	}
	if (!decl_specs(c, &ds, true)) {
		if (is(c, T_IDENT) && !kwof(tok(c)))
			fatal(c, tok(c)->loc, "unknown type name '%.*s'", IDF(tok(c)->id));
		{
			char b[64];

			fatal(c, tok(c)->loc, "expected declaration before '%s'", tokspell(tok(c), b, sizeof(b)));
		}
	}
	if (accept(c, T_SEMI))
		return;
	for (;;) {
		declr d;
		uint32_t first, nops, s;
		qty t;

		declarator(c, &d, false, &first, &nops);
		t = apply_declarator(c, ds.ty, first, nops);
		tl_ndops = first;
		asm_label(c, &d.at);
		attribute_list(c, &d.at);
		if (ds.auto_type) {
			uint32_t v;

			expect(c, T_EQUAL, "'=' (__auto_type requires an initializer)");
			v = decay(c, assign_expr(c));
			t = QT(QI(QUNQ(N(v)->ty)), QQ(ds.ty));
			{
				dspec d2 = ds;

				d2.ty = t;
				s = declare(c, &d2, &d, t, file);
			}
			c->syms[s].body = v;
			c->syms[s].flags |= SF_DEFINED;
			if (out) {
				uint32_t n = mk2(c, N_DECL, prim(TY_VOID), d.loc, s, v);

				vpush(c, out, n);
			}
			if (accept(c, T_COMMA))
				continue;
			expect(c, T_SEMI, "';'");
			return;
		}
		s = declare(c, &ds, &d, t, file);
		if (is_func(c, t) && is(c, T_LBRACE)) {
			if (!file)
				fatal(c, tok(c)->loc, "function definition is not allowed here");
			function_def(c, s, &d);
			return;
		}
		if (accept(c, T_EQUAL)) {
			sym *y = &c->syms[s];
			qty it = t;
			bool st = file || ds.sc == SC_STATIC;
			uint32_t v;

			if (ds.sc == SC_TYPEDEF || is_func(c, t))
				error(c, d.loc, "illegal initializer");
			if (!file && ds.sc == SC_EXTERN)
				error(c, d.loc, "declaration of block scope identifier with linkage cannot have an initializer");
			v = initializer(c, &it, st);
			y = &c->syms[s];
			if (QI(it) != QI(t)) {
				/* array size from the initializer */
				y->ty = QT(QI(it), QQ(y->ty));
			}
			if (y->body && file)
				error(c, d.loc, "redefinition of '%.*s'", IDF(d.name));
			y->body = v;
			y->flags |= SF_DEFINED;
			if (out && !st) {
				uint32_t n = mk2(c, N_DECL, prim(TY_VOID), d.loc, s, v);

				vpush(c, out, n);
			}
		} else {
			sym *y = &c->syms[s];

			if (y->kind == S_VAR && ds.sc != SC_EXTERN && ds.sc != SC_TYPEDEF) {
				if (file)
					y->flags |= SF_TENTATIVE;
				else if (!complete(c, y->ty) && ds.sc != SC_STATIC)
					error(c, d.loc, "variable has incomplete type");
				if (out && y->sc == SC_AUTO) {
					uint32_t n = mk2(c, N_DECL, prim(TY_VOID), d.loc, s, 0);

					vpush(c, out, n);
				}
			}
		}
		if (accept(c, T_COMMA))
			continue;
		expect(c, T_SEMI, "';'");
		return;
	}
}

/* ---------------- statements ---------------- */

static uint32_t label_ref(cc *c, uint32_t name, uint32_t loc)
{
	uint32_t l = dget(c->ldir, name);

	if (l)
		return l;
	l = c->nlabel++;
	memset(&c->labels[l], 0, sizeof(label));
	c->labels[l].name = name;
	c->labels[l].loc = loc;
	c->labels[l].shadow = 0;
	*dslot(c, c->ldir, name) = l;
	return l;
}

static uint32_t mkstmt(cc *c, int op, uint32_t loc, uint32_t a, uint32_t b, uint32_t cc_)
{
	uint32_t n = mk(c, op, prim(TY_VOID), loc);

	N(n)->a = a, N(n)->b = b, N(n)->c = cc_;
	return n;
}

static uint32_t cond_of(cc *c)
{
	uint32_t e;

	expect(c, T_LPAREN, "'('");
	e = decay(c, expr(c));
	expect(c, T_RPAREN, "')'");
	need_scalar(c, e, "statement");
	return e;
}

static uint32_t asm_string(cc *c)
{
	return string_index(c);
}

static void asm_operands(cc *c, vec *ops, bool out)
{
	if (is(c, T_COLON) || is(c, T_COLONCOLON) || is(c, T_RPAREN))
		return;
	for (;;) {
		uint32_t name = 0, cons, e;

		if (accept(c, T_LSQUARE)) {
			if (is(c, T_IDENT))
				name = tok(c)->id;
			next(c);
			expect(c, T_RSQUARE, "']'");
		}
		cons = asm_string(c);
		expect(c, T_LPAREN, "'('");
		e = expr(c);
		expect(c, T_RPAREN, "')'");
		if (out)
			need_lvalue(c, e, "asm output");
		else
			e = decay(c, e);
		vpush(c, ops, name);
		vpush(c, ops, cons);
		vpush(c, ops, e);
		if (!accept(c, T_COMMA))
			break;
	}
}

/* one ':' of an asm statement; "::" counts as two */
static int asm_colons(cc *c, int *pending)
{
	if (*pending) {
		(*pending)--;
		return 1;
	}
	if (accept(c, T_COLON))
		return 1;
	if (accept(c, T_COLONCOLON)) {
		*pending = 1;
		return 1;
	}
	return 0;
}

static uint32_t asm_stmt(cc *c, bool toplevel)
{
	uint32_t loc = tok(c)->loc, ai = c->nasm++, n;
	casm *a = &c->asms[ai];
	vec outs, ins, clob, labs;
	int pending = 0;

	memset(a, 0, sizeof(*a));
	next(c);
	for (;;) {
		int kw = kwof(tok(c));

		if (kw == KW_volatile || kw == KW___volatile__ || kw == KW___volatile)
			a->vol = 1;
		else if (kw == KW_inline || kw == KW___inline__ || kw == KW___inline)
			a->inl = 1;
		else if (kw == KW_goto)
			a->go = 1;
		else
			break;
		next(c);
	}
	vinit(&outs), vinit(&ins), vinit(&clob), vinit(&labs);
	expect(c, T_LPAREN, "'('");
	a->tmpl = asm_string(c);
	if (!toplevel && asm_colons(c, &pending)) {
		if (!pending)
			asm_operands(c, &outs, true);
		if (asm_colons(c, &pending)) {
			if (!pending)
				asm_operands(c, &ins, false);
			if (asm_colons(c, &pending)) {
				if (!pending && !is(c, T_COLON) && !is(c, T_COLONCOLON) && !is(c, T_RPAREN)) {
					do
						vpush(c, &clob, asm_string(c));
					while (accept(c, T_COMMA));
				}
				if (asm_colons(c, &pending)) {
					if (!is(c, T_RPAREN)) {
						do {
							if (!is(c, T_IDENT))
								fatal(c, tok(c)->loc, "expected identifier");
							vpush(c, &labs, label_ref(c, tok(c)->id, tok(c)->loc));
							next(c);
						} while (accept(c, T_COMMA));
					}
				}
			}
		}
	}
	expect(c, T_RPAREN, "')'");
	a = &c->asms[ai];
	a->nout = outs.n / 3;
	a->nin = ins.n / 3;
	a->ops = halloc(c, (a->nout + a->nin + 1) * sizeof(asmop));
	for (uint32_t i = 0; i < a->nout; i++)
		a->ops[i] = (asmop){ outs.v[3 * i], outs.v[3 * i + 1], outs.v[3 * i + 2] };
	for (uint32_t i = 0; i < a->nin; i++)
		a->ops[a->nout + i] = (asmop){ ins.v[3 * i], ins.v[3 * i + 1], ins.v[3 * i + 2] };
	a->nclob = clob.n;
	a->clob = halloc(c, (clob.n + 1) * sizeof(uint32_t));
	memcpy(a->clob, clob.v, clob.n * sizeof(uint32_t));
	a->nlabel = labs.n;
	a->labels = halloc(c, (labs.n + 1) * sizeof(uint32_t));
	memcpy(a->labels, labs.v, labs.n * sizeof(uint32_t));
	expect(c, T_SEMI, "';'");
	n = mkstmt(c, N_ASM, loc, ai, 0, 0);
	return n;
}

static bool at_asm(cc *c)
{
	int kw = kwof(tok(c));

	return kw == KW_asm || kw == KW___asm__ || kw == KW___asm;
}

static uint32_t compound_stmt(cc *c, qty *last)
{
	uint32_t loc = tok(c)->loc, n;
	vec items, locals;

	vinit(&items);
	vinit(&locals);
	expect(c, T_LBRACE, "'{'");
	scope_open(c);
	/* __label__ a, b; */
	while (iskw(c, KW___label__)) {
		next(c);
		do {
			uint32_t l;

			if (!is(c, T_IDENT))
				fatal(c, tok(c)->loc, "expected identifier");
			l = c->nlabel++;
			memset(&c->labels[l], 0, sizeof(label));
			c->labels[l].name = tok(c)->id;
			c->labels[l].loc = tok(c)->loc;
			c->labels[l].local = 1;
			c->labels[l].shadow = dget(c->ldir, tok(c)->id);
			*dslot(c, c->ldir, tok(c)->id) = l;
			vpush(c, &locals, l);
			next(c);
		} while (accept(c, T_COMMA));
		expect(c, T_SEMI, "';'");
	}
	while (!is(c, T_RBRACE)) {
		if (is(c, T_EOF))
			fatal(c, tok(c)->loc, "expected '}'");
		if (iskw(c, KW___extension__) && is_decl_start(c, peek(c, 1)))
			next(c);
		if (iskw(c, KW___attribute__) || iskw(c, KW___attribute)) {
			attrs a = { 0 };

			attribute_list(c, &a);
			if (accept(c, T_SEMI))
				continue;	/* __attribute__((fallthrough)); */
			if (is_decl_start(c, tok(c))) {
				c->pend = a;
				c->has_pend = true;
				declaration(c, &items, false);
				continue;
			}
		}
		if (is_decl_start(c, tok(c)) && !(is(c, T_IDENT) && peek(c, 1)->kind == T_COLON)) {
			declaration(c, &items, false);
			continue;
		}
		vpush(c, &items, statement(c));
	}
	next(c);
	if (last) {
		*last = prim(TY_VOID);
		uint32_t ls = items.n ? items.v[items.n - 1] : 0;

		while (ls && N(ls)->op == N_LABEL)	/* clang: a labeled statement gives its value */
			ls = N(ls)->c;
		if (ls && N(ls)->op == N_EXPR && N(ls)->a) {
			uint32_t e = decay(c, N(ls)->a);

			*last = QUNQ(N(e)->ty);
		}
	}
	for (uint32_t i = locals.n; i-- > 0;) {
		label *l = &c->labels[locals.v[i]];

		*dslot(c, c->ldir, l->name) = l->shadow;
	}
	scope_close(c);
	n = mkstmt(c, N_BLOCK, loc, 0, vfreeze(c, &items), items.n);
	return n;
}

static uint32_t statement(cc *c)
{
	ptok *t = tok(c);
	uint32_t loc = t->loc;
	int kw = kwof(t);

	switch (t->kind) {
	case T_LBRACE:
		return compound_stmt(c, NULL);
	case T_SEMI:
		next(c);
		return mkstmt(c, N_EXPR, loc, 0, 0, 0);
	case T_IDENT:
		if (!kw && peek(c, 1)->kind == T_COLON) {
			uint32_t l = label_ref(c, t->id, loc), n, s;
			attrs la = { 0 };

			next(c), next(c);
			attribute_list(c, &la);
			if (c->labels[l].defined)
				error(c, loc, "redefinition of label '%.*s'", IDF(c->labels[l].name));
			if (is(c, T_RBRACE))	/* label at end of compound statement (C23) */
				s = mkstmt(c, N_EXPR, loc, 0, 0, 0);
			else if (is_decl_start(c, tok(c)) && !(is(c, T_IDENT) && peek(c, 1)->kind == T_COLON)) {
				vec items;

				vinit(&items);
				declaration(c, &items, false);
				s = mkstmt(c, N_BLOCK, loc, 0, vfreeze(c, &items), items.n);
				/* the declaration stays in the enclosing scope: (approximation) */
			} else
				s = statement(c);
			n = mkstmt(c, N_LABEL, loc, l, 0, s);
			c->labels[l].defined = n;
			return n;
		}
		break;
	default:
		break;
	}
	switch (kw) {
	case KW_if: {
		uint32_t cnd, th, el = 0;

		next(c);
		cnd = cond_of(c);
		th = statement(c);
		if (iskw(c, KW_else)) {
			next(c);
			el = statement(c);
		}
		return mkstmt(c, N_IF, loc, cnd, th, el);
	}
	case KW_while: {
		uint32_t cnd, body;

		next(c);
		cnd = cond_of(c);
		c->in_loop++;
		body = statement(c);
		c->in_loop--;
		return mkstmt(c, N_WHILE, loc, cnd, body, 0);
	}
	case KW_do: {
		uint32_t cnd, body;

		next(c);
		c->in_loop++;
		body = statement(c);
		c->in_loop--;
		if (!iskw(c, KW_while))
			fatal(c, tok(c)->loc, "expected 'while' in do/while loop");
		next(c);
		cnd = cond_of(c);
		expect(c, T_SEMI, "';'");
		return mkstmt(c, N_DO, loc, cnd, body, 0);
	}
	case KW_for: {
		uint32_t init = 0, cnd = 0, step = 0, body, n;
		vec sb;

		next(c);
		expect(c, T_LPAREN, "'('");
		scope_open(c);
		if (is_decl_start(c, tok(c))) {
			vec items;

			vinit(&items);
			declaration(c, &items, false);
			init = mkstmt(c, N_BLOCK, loc, 0, vfreeze(c, &items), items.n);
		} else {
			if (!is(c, T_SEMI))
				init = mkstmt(c, N_EXPR, loc, expr(c), 0, 0);
			expect(c, T_SEMI, "';'");
		}
		if (!is(c, T_SEMI)) {
			cnd = decay(c, expr(c));
			need_scalar(c, cnd, "for");
		}
		expect(c, T_SEMI, "';'");
		if (!is(c, T_RPAREN))
			step = expr(c);
		expect(c, T_RPAREN, "')'");
		c->in_loop++;
		body = statement(c);
		c->in_loop--;
		scope_close(c);
		vinit(&sb);
		vpush(c, &sb, step);
		vpush(c, &sb, body);
		n = mkstmt(c, N_FOR, loc, init, cnd, vfreeze(c, &sb));
		return n;
	}
	case KW_switch: {
		uint32_t cnd, body;
		qty saved = c->switch_ty;

		next(c);
		expect(c, T_LPAREN, "'('");
		cnd = rvalue(c, expr(c));
		expect(c, T_RPAREN, "')'");
		if (!is_integer(c, N(cnd)->ty))
			error(c, loc, "statement requires expression of integer type");
		c->switch_ty = N(cnd)->ty;
		c->in_switch++;
		body = statement(c);
		c->in_switch--;
		c->switch_ty = saved;
		return mkstmt(c, N_SWITCH, loc, cnd, body, 0);
	}
	case KW_case: {
		i128 lo, hi;
		uint32_t s, n;

		next(c);
		if (!c->in_switch)
			error(c, loc, "'case' statement not in switch statement");
		const_expr(c, &lo, NULL);
		hi = lo;
		if (accept(c, T_ELLIPSIS))
			const_expr(c, &hi, NULL);
		expect(c, T_COLON, "':'");
		if (c->switch_ty)
			lo = trunc_to(c, lo, c->switch_ty), hi = trunc_to(c, hi, c->switch_ty);
		if (is(c, T_RBRACE))
			s = mkstmt(c, N_EXPR, loc, 0, 0, 0);
		else
			s = statement(c);
		c->ipool[c->nipool] = lo;
		c->ipool[c->nipool + 1] = hi;
		n = mkstmt(c, N_CASE, loc, c->nipool, c->nipool + 1, s);
		c->nipool += 2;
		return n;
	}
	case KW_default: {
		uint32_t s;

		next(c);
		expect(c, T_COLON, "':'");
		if (!c->in_switch)
			error(c, loc, "'default' statement not in switch statement");
		s = is(c, T_RBRACE) ? mkstmt(c, N_EXPR, loc, 0, 0, 0) : statement(c);
		return mkstmt(c, N_DEFAULT, loc, 0, 0, s);
	}
	case KW_break:
		next(c);
		if (!c->in_loop && !c->in_switch)
			error(c, loc, "'break' statement not in loop or switch statement");
		expect(c, T_SEMI, "';'");
		return mkstmt(c, N_BREAK, loc, 0, 0, 0);
	case KW_continue:
		next(c);
		if (!c->in_loop)
			error(c, loc, "'continue' statement not in loop statement");
		expect(c, T_SEMI, "';'");
		return mkstmt(c, N_CONTINUE, loc, 0, 0, 0);
	case KW_return: {
		uint32_t e = 0;
		qty rt = c->fn ? T(c, c->syms[c->fn].ty)->base : prim(TY_INT);

		next(c);
		if (!is(c, T_SEMI)) {
			e = decay(c, expr(c));
			if (!is_void(c, rt) && !is_void(c, N(e)->ty))
				e = is_record(c, rt) ? e : convert(c, e, rt);
		}
		expect(c, T_SEMI, "';'");
		return mkstmt(c, N_RETURN, loc, e, 0, 0);
	}
	case KW_goto:
		next(c);
		if (accept(c, T_STAR)) {
			uint32_t e = decay(c, expr(c));

			expect(c, T_SEMI, "';'");
			return mkstmt(c, N_GOTO_IND, loc, e, 0, 0);
		}
		if (!is(c, T_IDENT))
			fatal(c, tok(c)->loc, "expected identifier");
		{
			uint32_t l = label_ref(c, tok(c)->id, tok(c)->loc);

			next(c);
			expect(c, T_SEMI, "';'");
			return mkstmt(c, N_GOTO, loc, l, 0, 0);
		}
	case KW_asm: case KW___asm__: case KW___asm:
		return asm_stmt(c, false);
	case KW___attribute__: case KW___attribute: {
		attrs a = { 0 };

		attribute_list(c, &a);
		if (accept(c, T_SEMI))
			return mkstmt(c, N_EXPR, loc, 0, 0, 0);	/* fallthrough etc. */
		return statement(c);
	}
	default:
		break;
	}
	{
		uint32_t e = expr(c);

		expect(c, T_SEMI, "';'");
		return mkstmt(c, N_EXPR, loc, e, 0, 0);
	}
}

static void function_def(cc *c, uint32_t s, const declr *d)
{
	uint32_t body, first_label = c->nlabel;
	uint32_t saved_fn = c->fn;

	if (c->syms[s].flags & SF_DEFINED && !(c->syms[s].flags & SF_GNU_INLINE))
		error(c, d->loc, "redefinition of '%.*s'", IDF(d->name));
	c->syms[s].flags |= SF_DEFINED;
	c->fn = s;
	scope_open(c);
	for (uint32_t i = 0; i < d->npsyms; i++) {
		uint32_t p = c->lists[d->psyms + i];

		if (!c->syms[p].name)
			continue;	/* C23: unnamed parameters in definitions (clang accepts) */
		bind(c, p);
	}
	c->in_loop = c->in_switch = 0;
	body = compound_stmt(c, NULL);
	scope_close(c);
	for (uint32_t l = first_label; l < c->nlabel; l++) {
		label *lb = &c->labels[l];

		if (!lb->defined && !lb->local)
			error(c, lb->loc, "use of undeclared label '%.*s'", IDF(lb->name));
		if (!lb->local && dget(c->ldir, lb->name) == l)
			*dslot(c, c->ldir, lb->name) = 0;
	}
	c->syms[s].body = body;
	/* for the emitter: the parameter syms, on the body block */
	N(body)->a = d->psyms;
	N(body)->aux = (uint16_t)d->npsyms;
	c->fn = saved_fn;
}

/* ---------------- translation unit ---------------- */

static void translation_unit(cc *c)
{
	for (;;) {
		dc_boundary(c);
		if (is(c, T_EOF))
			break;
		if (accept(c, T_SEMI))
			continue;
		if (iskw(c, KW___extension__)) {
			next(c);
			continue;
		}
		if (at_asm(c)) {
			c->tlasm[c->ntlasm++] = asm_stmt(c, true);
			continue;
		}
		declaration(c, NULL, true);
		tl_ndops = 0;
	}
}

/* ---------------- layout checks (clang verifies them) ---------------- */

typedef struct obuf {
	char *p;
	size_t n, cap;
	int fd;
} obuf;

static void oflush(obuf *o)
{
	size_t off = 0;

	while (off < o->n) {
		ssize_t w = write(o->fd, o->p + off, o->n - off);

		if (w <= 0)
			break;
		off += (size_t)w;
	}
	o->n = 0;
}

__attribute__((format(printf, 2, 3)))
static void oprintf(obuf *o, const char *fmt, ...)
{
	va_list ap;
	int n;

	if (o->cap - o->n < 4096)
		oflush(o);
	va_start(ap, fmt);
	n = vsnprintf(o->p + o->n, o->cap - o->n, fmt, ap);
	va_end(ap);
	if (n > 0)
		o->n += (size_t)n < o->cap - o->n ? (size_t)n : o->cap - o->n - 1;
}

static void field_checks(cc *c, obuf *o, const char *rname, uint32_t ti, uint64_t base, const char *prefix)
{
	const tag *r = &c->tags[ti];

	for (uint32_t i = 0; i < r->nfield; i++) {
		const field *f = &r->f[i];

		if (f->bitfield)
			continue;
		if (!f->name) {
			if (is_record(c, f->ty))
				field_checks(c, o, rname, T(c, f->ty)->aux, base + f->off / 8, prefix);
			continue;
		}
		oprintf(o, "_Static_assert(__builtin_offsetof(%s, %s%.*s) == %llu, \"vcc\");\n", rname, prefix,
			IDF(f->name), (unsigned long long)(base + f->off / 8));
	}
}

static const char *int_name(int k)
{
	switch (k) {
	case TY_BOOL: return "_Bool";
	case TY_CHAR: return "char";
	case TY_SCHAR: return "signed char";
	case TY_UCHAR: return "unsigned char";
	case TY_SHORT: return "short";
	case TY_USHORT: return "unsigned short";
	case TY_INT: return "int";
	case TY_UINT: return "unsigned int";
	case TY_LONG: return "long";
	case TY_ULONG: return "unsigned long";
	case TY_LLONG: return "long long";
	case TY_ULLONG: return "unsigned long long";
	case TY_INT128: return "__int128";
	case TY_UINT128: return "unsigned __int128";
	default: return NULL;
	}
}

/* C spelling of a type for the checks (*ok = false if it cannot be named) */
typedef struct tbuf {
	char s[4096];
	size_t n;
} tbuf;

static void tb_put(tbuf *b, const char *s, size_t n)
{
	if (b->n + n >= sizeof(b->s))
		n = sizeof(b->s) - 1 - b->n;
	memcpy(b->s + b->n, s, n);
	b->n += n;
	b->s[b->n] = 0;
}

static void tb_puts(tbuf *b, const char *s) { tb_put(b, s, strlen(s)); }

static void quals(tbuf *b, int q)
{
	if (q & Q_CONST) tb_puts(b, "const ");
	if (q & Q_VOLATILE) tb_puts(b, "volatile ");
	if (q & Q_RESTRICT) tb_puts(b, "restrict ");
}

/* out = spelling of t around the declarator text inner */
static void type_str(cc *c, qty t, const char *inner, tbuf *out, bool *ok)
{
	const ctype *x = T(c, t);
	tbuf b;

	b.n = 0, b.s[0] = 0;
	switch (x->kind) {
	case TY_PTR: {
		tbuf in;
		const ctype *bt = T(c, x->base);

		in.n = 0, in.s[0] = 0;
		if (bt->kind == TY_ARRAY || bt->kind == TY_FUNC)
			tb_puts(&in, x->flags & TF_BLOCK ? "(^" : "(*");
		else
			tb_puts(&in, x->flags & TF_BLOCK ? "^" : "*");
		if (QQ(t)) {
			tb_puts(&in, " ");
			quals(&in, QQ(t));
		}
		tb_puts(&in, inner);
		if (bt->kind == TY_ARRAY || bt->kind == TY_FUNC)
			tb_puts(&in, ")");
		type_str(c, x->base, in.s, out, ok);
		return;
	}
	case TY_ARRAY: {
		tbuf in;
		char n[32];

		in.n = 0, in.s[0] = 0;
		tb_puts(&in, inner);
		if (x->flags & (TF_INCOMPLETE | TF_VLA))
			tb_puts(&in, "[]");
		else {
			snprintf(n, sizeof(n), "[%llu]", (unsigned long long)arr_len(c, t));
			tb_puts(&in, n);
		}
		type_str(c, QT(QI(x->base), QQ(x->base) | QQ(t)), in.s, out, ok);
		return;
	}
	case TY_FUNC: {
		tbuf in;

		in.n = 0, in.s[0] = 0;
		tb_puts(&in, inner);
		tb_puts(&in, "(");
		if (x->flags & TF_NOPROTO) {
		} else if (!x->n && !(x->flags & TF_VARIADIC)) {
			tb_puts(&in, "void");
		} else {
			for (uint32_t i = 0; i < x->n; i++) {
				tbuf p;

				p.n = 0, p.s[0] = 0;
				if (i)
					tb_puts(&in, ", ");
				type_str(c, c->params[x->aux + i], "", &p, ok);
				tb_put(&in, p.s, p.n);
			}
			if (x->flags & TF_VARIADIC)
				tb_puts(&in, ", ...");
		}
		tb_puts(&in, ")");
		x = T(c, t);
		type_str(c, x->base, in.s, out, ok);
		return;
	}
	default:
		break;
	}
	quals(&b, QQ(t));
	switch (x->kind) {
	case TY_VOID: tb_puts(&b, "void"); break;
	case TY_FLOAT16: tb_puts(&b, "_Float16"); break;
	case TY_FLOAT: tb_puts(&b, "float"); break;
	case TY_DOUBLE: tb_puts(&b, "double"); break;
	case TY_LDOUBLE: tb_puts(&b, "long double"); break;
	case TY_STRUCT: case TY_UNION: case TY_ENUM: {
		const tag *r = &c->tags[x->aux];
		char nb[256];

		if (x->aux == c->valist_tag && c->valist_tag) {
			tb_puts(&b, "__builtin_va_list");
		} else if (r->name) {
			uint32_t s = lookup_tag(c, r->name);

			if (!s || (uint32_t)c->syms[s].val != x->aux)
				*ok = false;
			snprintf(nb, sizeof(nb), "%s %.*s", x->kind == TY_STRUCT ? "struct" : x->kind == TY_UNION ? "union" : "enum",
				 IDF(r->name));
			tb_puts(&b, nb);
		} else if (r->typedef_name) {
			uint32_t s = lookup(c, r->typedef_name);

			if (!s || c->syms[s].kind != S_TYPEDEF || T(c, c->syms[s].ty)->canon != T(c, t)->canon)
				*ok = false;
			snprintf(nb, sizeof(nb), "%.*s", IDF(r->typedef_name));
			tb_puts(&b, nb);
		} else {
			*ok = false;
		}
		break;
	}
	default: {
		const char *n = int_name(x->kind);

		if (!n)
			*ok = false;
		else
			tb_puts(&b, n);
	}
	}
	if (inner[0]) {
		tb_puts(&b, " ");
		tb_puts(&b, inner);
	}
	tb_put(out, b.s, b.n);
}

static void layout_checks(cc *c)
{
	obuf o = { malloc(1 << 20), 0, 1 << 20, c->o->outfd };
	char rn[512];

	oprintf(&o, "\n/* vcc layout checks */\n");
	for (uint32_t i = 0; i < c->ntag; i++) {
		const tag *r = &c->tags[i];
		int k = c->ty[r->ty].kind;

		if (!(r->flags & RF_COMPLETE))
			continue;
		if (r->name) {
			uint32_t s = lookup_tag(c, r->name);

			if (!s || (uint32_t)c->syms[s].val != i)
				continue;
			snprintf(rn, sizeof(rn), "%s %.*s", k == TY_STRUCT ? "struct" : k == TY_UNION ? "union" : "enum",
				 IDF(r->name));
		} else if (r->typedef_name) {
			uint32_t s = lookup(c, r->typedef_name);

			if (!s || c->syms[s].kind != S_TYPEDEF || QI(c->syms[s].ty) != r->ty)
				continue;
			snprintf(rn, sizeof(rn), "%.*s", IDF(r->typedef_name));
		} else {
			continue;
		}
		oprintf(&o, "_Static_assert(sizeof(%s) == %llu && _Alignof(%s) == %u, \"vcc\");\n", rn,
			(unsigned long long)r->size, rn, r->align);
		if (k == TY_ENUM) {
			const char *bn = int_name(KIND(c, c->ty[r->ty].base));

			if (bn)
				oprintf(&o, "_Static_assert(__builtin_types_compatible_p(%s, %s), \"vcc\");\n", rn, bn);
			continue;
		}
		field_checks(c, &o, rn, i, 0, "");
	}
	for (uint32_t i = 0; i < c->ntoplevel; i++) {
		const sym *s = &c->syms[c->toplevel[i]];

		if (!s->name || lookup(c, s->name) != c->toplevel[i])
			continue;
		if (s->kind == S_TYPEDEF) {
			if (complete(c, s->ty) && !is_func(c, s->ty))
				oprintf(&o, "_Static_assert(sizeof(%.*s) == %llu && _Alignof(%.*s) == %u, \"vcc\");\n",
					IDF(s->name), (unsigned long long)ty_size(c, s->ty), IDF(s->name), ty_align(c, s->ty));
		} else if (s->kind == S_VAR && complete(c, s->ty)) {
			oprintf(&o, "_Static_assert(sizeof(%.*s) == %llu, \"vcc\");\n", IDF(s->name),
				(unsigned long long)ty_size(c, s->ty));
		}
		if (s->kind == S_VAR || s->kind == S_FUNC) {
			tbuf b;
			bool ok = true;

			b.n = 0, b.s[0] = 0;
			type_str(c, s->ty, "", &b, &ok);
			if (ok)
				oprintf(&o, "_Static_assert(__builtin_types_compatible_p(__typeof__(%.*s), %s), \"vcc\");\n",
					IDF(s->name), b.s);
		}
	}
	/* enumerators: value and type */
	for (uint32_t i = 1; i < c->nsym; i++) {
		const sym *s = &c->syms[i];
		const char *tn;

		if (s->kind != S_ENUMCONST || s->depth != 0 || lookup(c, s->name) != i)
			continue;
		tn = int_name(KIND(c, KIND(c, s->ty) == TY_ENUM ? T(c, s->ty)->base : s->ty));
		if (!tn)
			continue;
		oprintf(&o, "_Static_assert((%.*s) == %lldLL && __builtin_types_compatible_p(__typeof__(%.*s), %s), \"vcc\");\n",
			IDF(s->name), (long long)s->val, IDF(s->name), tn);
	}
	oflush(&o);
	free(o.p);
}

/* ---------------- driver ---------------- */

static void cc_init(cc *c, pp *p, const cc_opts *o)
{
	memset(c, 0, sizeof(*c));
	c->p = p;
	c->o = o;
	ccr = vmpool_take(&ccpool);
	c->ty = vmreg_get(&ccr[0], (size_t)1 << 30, "cc.ty");
	c->params = vmreg_get(&ccr[1], (size_t)1 << 28, "cc.params");
	c->tags = vmreg_get(&ccr[2], (size_t)1 << 28, "cc.tags");
	c->syms = vmreg_get(&ccr[3], (size_t)1 << 30, "cc.syms");
	c->nodes = vmreg_get(&ccr[4], (size_t)1 << 32, "cc.nodes");
	c->lists = vmreg_get(&ccr[5], (size_t)1 << 30, "cc.lists");
	c->ipool = vmreg_get(&ccr[6], (size_t)1 << 30, "cc.ipool");
	c->fpool = vmreg_get(&ccr[7], (size_t)1 << 26, "cc.fpool");
	c->strs = vmreg_get(&ccr[8], (size_t)1 << 28, "cc.strs");
	c->labels = vmreg_get(&ccr[9], (size_t)1 << 26, "cc.labels");
	c->attr = vmreg_get(&ccr[10], (size_t)1 << 28, "cc.attr");
	c->asms = vmreg_get(&ccr[11], (size_t)1 << 28, "cc.asms");
	c->sstack = vmreg_get(&ccr[12], (size_t)1 << 28, "cc.sstack");
	c->toplevel = vmreg_get(&ccr[13], (size_t)1 << 26, "cc.toplevel");
	c->tlasm = vmreg_get(&ccr[14], (size_t)1 << 24, "cc.tlasm");
	c->heap = c->hp = vmreg_get(&ccr[15], (size_t)1 << 32, "cc.heap");
	c->hend = c->heap + ((size_t)1 << 32);
	tl_dops = halloc(c, DOPS_MAX * sizeof(dop));
	tl_ndops = 0;
	c->tyhcap = 1 << 16;
	c->tyhash = halloc(c, c->tyhcap * sizeof(uint32_t));
	memset(c->tyhash, 0, c->tyhcap * sizeof(uint32_t));
	c->fnhcap = 1 << 14;
	c->fnhash = halloc(c, c->fnhcap * sizeof(uint32_t));
	memset(c->fnhash, 0, c->fnhcap * sizeof(uint32_t));
	/* index 0 of every table is "none" */
	c->nsym = c->nnode = c->ntag = c->nlabel = c->nattr = c->nstr = 1;
	c->nlist = 1;
	/* basic types: index == kind */
	for (int k = 0; k < TY_PTR; k++) {
		ctype t = { .kind = (uint8_t)k, .size = basic[k].size, .align = basic[k].align };

		if (k == TY_LDOUBLE && o->target == PP_DARWIN)
			t.size = 8, t.align = 8;
		c->ty[k] = t;
		c->ty[k].canon = (uint32_t)k;
		c->nty++;
	}
	for (int k = 0; k < TY_PTR; k++) {	/* make them findable by ty_intern */
		uint32_t h = ty_hash(&c->ty[k]), m = c->tyhcap - 1, i;

		for (i = h & m; c->tyhash[i]; i = (i + 1) & m)
			;
		if (k)
			c->tyhash[i] = (uint32_t)k;
	}
	c->t_size = prim(TY_ULONG);
	c->t_ptrdiff = prim(TY_LONG);
	c->t_wchar = o->short_wchar ? prim(TY_USHORT) : o->target == PP_DARWIN ? prim(TY_INT) : prim(TY_UINT);
	c->t_char16 = prim(TY_USHORT);
	c->t_char32 = prim(TY_UINT);
	c->id_func = vintern_copy("__func__\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 8);
	c->id_FUNCTION = vintern_copy("__FUNCTION__\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 12);
	c->id_PRETTY = vintern_copy("__PRETTY_FUNCTION__\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 19);
	c->id_null[0] = vintern_copy("_Nonnull\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 8);
	c->id_null[1] = vintern_copy("_Nullable\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 9);
	c->id_null[2] = vintern_copy("_Null_unspecified\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 17);
	c->id_null[3] = vintern_copy("_Nullable_result\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16);
	c->depth = 0;
	/* predeclared: __builtin_va_list, __int128_t, __uint128_t */
	{
		uint32_t s;
		qty vl;

		if (o->target == PP_DARWIN) {
			vl = ty_ptr(c, prim(TY_CHAR));
		} else {
			/* struct __va_list { void *__stack, *__gr_top, *__vr_top; int __gr_offs, __vr_offs; } */
			uint32_t ti = tag_new(c, TY_STRUCT, vintern_copy("__va_list\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 9), 0);
			tag *r = &c->tags[ti];
			qty vp = ty_ptr(c, prim(TY_VOID));

			r->f = halloc(c, 5 * sizeof(field));
			memset(r->f, 0, 5 * sizeof(field));
			r->nfield = 5;
			r->f[0].ty = r->f[1].ty = r->f[2].ty = vp;
			r->f[3].ty = r->f[4].ty = prim(TY_INT);
			lay_record(c, r, NULL, NULL);
			r->flags |= RF_COMPLETE;
			c->valist_tag = ti;
			vl = QT(r->ty, 0);
		}
		c->t_valist = vl;
		s = sym_new(c, S_TYPEDEF, vintern_copy("__builtin_va_list\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 17), vl, 0);
		bind(c, s);
		s = sym_new(c, S_TYPEDEF, vintern_copy("__int128_t\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 10), prim(TY_INT128), 0);
		bind(c, s);
		s = sym_new(c, S_TYPEDEF, vintern_copy("__uint128_t\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 11), prim(TY_UINT128), 0);
		bind(c, s);
	}
}

/* the tables back to the thread, zeroed up to what the TU used (+ one element) */
static void cc_free(cc *c)
{
#define USED(n, a) (((size_t)(n) + 2) * sizeof(*(a)))
	vmreg_put(&ccr[0], USED(c->nty, c->ty));
	vmreg_put(&ccr[1], USED(c->nparams, c->params));
	vmreg_put(&ccr[2], USED(c->ntag, c->tags));
	vmreg_put(&ccr[3], USED(c->nsym, c->syms));
	vmreg_put(&ccr[4], USED(c->nnode, c->nodes));
	vmreg_put(&ccr[5], USED(c->nlist, c->lists));
	vmreg_put(&ccr[6], USED(c->nipool, c->ipool));
	vmreg_put(&ccr[7], USED(c->nfpool, c->fpool));
	vmreg_put(&ccr[8], USED(c->nstr, c->strs));
	vmreg_put(&ccr[9], USED(c->nlabel, c->labels));
	vmreg_put(&ccr[10], USED(c->nattr, c->attr));
	vmreg_put(&ccr[11], USED(c->nasm, c->asms));
	vmreg_put(&ccr[12], VMREG_SCRATCH);	/* a stack: written before read */
	vmreg_put(&ccr[13], USED(c->ntoplevel, c->toplevel));
	vmreg_put(&ccr[14], USED(c->ntlasm, c->tlasm));
	vmreg_put(&ccr[15], (size_t)(c->hp - c->heap) + 64);
#undef USED
	vmpool_give(&ccpool, ccr);
	ccr = NULL;
}

static void init_tables(void)
{
	init_keywords();
	init_builtins();
	init_attrs();
}

int cc_run(pp *p, const cc_opts *o)
{
	static pthread_once_t once = PTHREAD_ONCE_INIT;
	cc *c = malloc(sizeof(cc));
	int errors;

	pthread_once(&once, init_tables);	/* vccd runs TUs on many threads */
	cc_init(c, p, o);
	if (!setjmp(c->bail)) {
		dc_begin(c);
		fetch(c, &c->tk[0]);
		c->ntk = 1;
		translation_unit(c);
	}
	if (o->dump_layouts && !c->errors)
		layout_checks(c);
	if (o->emit && !c->errors && !pp_errors(p))
		c->errors += emit_tu(c, o);
	errors = c->errors;
	dc_end(c);
	cc_free(c);
	free(c);
	return errors;
}
