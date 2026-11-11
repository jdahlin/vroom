/* vcc front end: record layout (clang's ItaniumRecordLayoutBuilder, AArch64) and the layout checks clang verifies */
#include "ccimpl.h"
#include "../lib/buf.h"

/* ---------------- record layout (clang ItaniumRecordLayoutBuilder, AArch64) ---------------- */

static uint64_t align_to(uint64_t v, uint64_t a) { return a ? (v + a - 1) / a * a : v; }

typedef struct layout {
	bool is_union, packed, ms_struct;
	uint64_t last_unit;	/* Microsoft bit-field storage-unit size, or zero */
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

	if (l->ms_struct) {
		falign = unit;
		if (l->last_unit != unit || l->unfilled < fsize) {
			if (!l->last_unit && !fsize)
				falign = 1; /* zero-width after a non-bitfield has no effect */
			l->unfilled = 0;
			l->last_unit = 0;
		}
	}
	off = l->is_union ? 0 : l->data_size - l->unfilled;
	/* AArch64: useBitFieldTypeAlignment and useZeroLengthBitfieldAlignment */
	ufalign = falign;
	if (packed && fsize != 0)
		falign = 1;	/* bits: a packed bit-field starts anywhere */
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
	if (l->ms_struct && l->is_union)
		falign = ufalign = 1;
	ufoff = off;
	if (l->ms_struct) {
		if (!fsize || fsize > l->unfilled) {
			off = align_to(off, falign);
			l->unfilled = 0;
		}
	} else {
		bool allow_pad = mfa == 0;

		bool exp = explicit_align && (mfa == 0 || explicit_align <= mfa);	/* useExplicitBitFieldAlignment */

		if (fsize == 0 || (allow_pad && (off & (falign - 1)) + fsize > unit))
			off = align_to(off, falign);
		else if (exp)
			off = align_to(off, explicit_align);
		if (fsize == 0 || (allow_pad && (ufoff & (ufalign - 1)) + fsize > unit))
			ufoff = align_to(ufoff, ufalign);
		else if (exp)
			ufoff = align_to(ufoff, explicit_align);
	}
	f->off = off;
	f->align = (uint32_t)((falign + 7) / 8);
	if (l->is_union) {
		uint64_t rs = l->ms_struct ? (fsize ? unit : 8) : align_to(fsize, 8);

		if (rs > l->data_size)
			l->data_size = rs;
	} else if (l->ms_struct && fsize) {
		if (!l->unfilled)
			l->data_size = off + unit, l->unfilled = unit;
		l->unfilled -= fsize;
		l->last_unit = unit;
	} else {
		uint64_t ns = off + fsize;

		l->last_unit = 0;

		l->data_size = align_to(ns, 8);
		l->unfilled = l->data_size - ns;
	}
	if (l->data_size > l->size)
		l->size = l->data_size;
	if (!l->ms_struct && c->o->target != PP_LINUX_AARCH64 && !f->name)
		falign = ufalign = 8;	/* Apple, wasm (no useZeroLengthBitfieldAlignment): unnamed bit-fields leave the record's alignment */
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
	l->last_unit = 0;
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
	f->align = (uint32_t)((falign + 7) / 8);
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
VPRIV void lay_record(cc *c, tag *r, const uint32_t *fa, const uint8_t *fp)
{
	layout l = { .is_union = (r->flags & RF_UNION) != 0, .packed = (r->flags & RF_PACKED) != 0,
		     .ms_struct = (r->flags & RF_MS_STRUCT) != 0, .align = 8, .unpacked_align = 8 };

	if (c->npack && c->pack[c->npack - 1])
		l.max_field_align = (uint64_t)c->pack[c->npack - 1] * 8;
	for (uint32_t i = 0; i < r->nfield; i++)
		lay_field(c, &l, &r->f[i], fa ? fa[i] : 0, fp ? fp[i] : 0);
	r->ualign = (uint32_t)(l.align / 8);
	if (r->maxalign)
		lay_update(&l, (uint64_t)r->maxalign * 8, (uint64_t)r->maxalign * 8);
	if (l.padded_field_size > l.size)
		l.size = l.padded_field_size;
	l.size = align_to(l.size, l.align);
	r->size = l.size / 8;
	r->align = (uint32_t)(l.align / 8);
	c->ty[r->ty].size = r->size;
	c->ty[r->ty].align = r->align;
}

/* ---------------- layout checks (clang verifies them) ---------------- */

static void field_checks(cc *c, vfdbuf *o, const char *rname, uint32_t ti, uint64_t base, const char *prefix)
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
		vfdbuf_printf(o, "_Static_assert(__builtin_offsetof(%s, %s%.*s) == %llu, \"vcc\");\n", rname, prefix,
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
static void quals(vfixbuf *b, int q)
{
	if (q & Q_CONST) vfixbuf_puts(b, "const ");
	if (q & Q_VOLATILE) vfixbuf_puts(b, "volatile ");
	if (q & Q_RESTRICT) vfixbuf_puts(b, "restrict ");
}

/* out = spelling of t around the declarator text inner */
static void type_str(cc *c, qty t, const char *inner, vfixbuf *out, bool *ok)
{
	const ctype *x = T(c, t);
	vfixbuf b;

	b.n = 0, b.s[0] = 0;
	if (x->kind == TY_ARRAY && c->valist_tag && T(c, c->t_valist)->canon == x->canon) {
		/* x86-64's __va_list_tag[1] is spelled by its typedef, the struct as itself */
		quals(&b, QQ(t));
		vfixbuf_puts(&b, "__builtin_va_list");
		if (inner[0]) {
			vfixbuf_puts(&b, " ");
			vfixbuf_puts(&b, inner);
		}
		vfixbuf_put(out, b.s, b.n);
		return;
	}
	switch (x->kind) {
	case TY_SVE:
		quals(&b, QQ(t));
		vfixbuf_put(&b, idstr_(x->aux), idlen_(x->aux));
		if (inner[0]) {
			vfixbuf_puts(&b, " ");
			vfixbuf_puts(&b, inner);
		}
		vfixbuf_put(out, b.s, b.n);
		return;
	case TY_PTR: {
		vfixbuf in;
		const ctype *bt = T(c, x->base);
		/* __builtin_va_list * (x86-64: an array spelled by its typedef) */
		bool wrap = (bt->kind == TY_ARRAY && !(c->valist_tag && T(c, c->t_valist)->canon == bt->canon)) || bt->kind == TY_FUNC;

		in.n = 0, in.s[0] = 0;
		if (wrap)
			vfixbuf_puts(&in, x->flags & TF_BLOCK ? "(^" : "(*");
		else
			vfixbuf_puts(&in, x->flags & TF_BLOCK ? "^" : "*");
		if (QQ(t)) {	/* clang: "int *const", "int *const *", "int *const p" */
			quals(&in, QQ(t));
			if (!inner[0] && in.n && in.s[in.n - 1] == ' ')
				in.s[--in.n] = 0;
		}
		vfixbuf_puts(&in, inner);
		if (wrap)
			vfixbuf_puts(&in, ")");
		type_str(c, x->base, in.s, out, ok);
		return;
	}
	case TY_ARRAY: {
		vfixbuf in;
		char n[32];

		in.n = 0, in.s[0] = 0;
		vfixbuf_puts(&in, inner);
		if (x->flags & (TF_INCOMPLETE | TF_VLA))
			vfixbuf_puts(&in, "[]");
		else {
			snprintf(n, sizeof(n), "[%llu]", (unsigned long long)arr_len(c, t));
			vfixbuf_puts(&in, n);
		}
		type_str(c, QT(QI(x->base), QQ(x->base) | QQ(t)), in.s, out, ok);
		return;
	}
	case TY_FUNC: {
		vfixbuf in;

		in.n = 0, in.s[0] = 0;
		vfixbuf_puts(&in, inner);
		vfixbuf_puts(&in, "(");
		if (x->flags & TF_NOPROTO) {
		} else if (!x->n && !(x->flags & TF_VARIADIC)) {
			vfixbuf_puts(&in, "void");
		} else {
			for (uint32_t i = 0; i < x->n; i++) {
				vfixbuf p;

				p.n = 0, p.s[0] = 0;
				if (i)
					vfixbuf_puts(&in, ", ");
				type_str(c, c->params[x->aux + i], "", &p, ok);
				vfixbuf_put(&in, p.s, p.n);
			}
			if (x->flags & TF_VARIADIC)
				vfixbuf_puts(&in, ", ...");
		}
		vfixbuf_puts(&in, ")");
		x = T(c, t);
		type_str(c, x->base, in.s, out, ok);
		return;
	}
	case TY_VECTOR: {
		/* clang without the typedef: __attribute__((__vector_size__(4 * sizeof(int)))) int */
		vfixbuf el;
		char n[96];
		uint32_t td = 0;

		el.n = 0, el.s[0] = 0;
		type_str(c, x->base, "", &el, ok);
		/* clang keeps the typedef name (vcc: the first typedef of this vector type) */
		for (uint32_t i = 1; i < c->nsym && !td; i++)
			if (c->syms[i].kind == S_TYPEDEF && c->syms[i].name && QI(c->syms[i].ty) == QI(t) && !QQ(c->syms[i].ty))
				td = i;
		quals(&b, QQ(t));
		if (td) {
			vfixbuf_put(&b, idstr_(c->syms[td].name), idlen_(c->syms[td].name));
			if (inner[0]) {
				vfixbuf_puts(&b, " ");
				vfixbuf_puts(&b, inner);
			} else {	/* \x01: the diagnostic printer closes the quote before it */
				snprintf(n, sizeof(n), "\x01(vector of %u '", x->n);
				vfixbuf_puts(&b, n);
				vfixbuf_put(&b, el.s, el.n);
				vfixbuf_puts(&b, "' values)");
			}
			vfixbuf_put(out, b.s, b.n);
			*ok = false;
			return;
		}
		snprintf(n, sizeof(n), "__attribute__((__vector_size__(%llu * sizeof(",
			 (unsigned long long)(ty_size(c, t) / ty_size(c, x->base)));
		vfixbuf_puts(&b, n);
		vfixbuf_put(&b, el.s, el.n);
		vfixbuf_puts(&b, ")))) ");
		vfixbuf_put(&b, el.s, el.n);
		if (inner[0]) {
			vfixbuf_puts(&b, " ");
			vfixbuf_puts(&b, inner);
		}
		vfixbuf_put(out, b.s, b.n);
		*ok = false;	/* not a spelling layout checks can use */
		return;
	}
	default:
		break;
	}
	quals(&b, QQ(t));
	switch (x->kind) {
	case TY_VOID: vfixbuf_puts(&b, "void"); break;
	case TY_BFLOAT16: vfixbuf_puts(&b, "__bf16"); break;
	case TY_FLOAT16: vfixbuf_puts(&b, "_Float16"); break;
	case TY_FLOAT: vfixbuf_puts(&b, "float"); break;
	case TY_DOUBLE: vfixbuf_puts(&b, "double"); break;
	case TY_LDOUBLE: vfixbuf_puts(&b, "long double"); break;
	case TY_FLOAT128: vfixbuf_puts(&b, "__float128"); break;
	case TY_STRUCT: case TY_UNION: case TY_ENUM: {
		const tag *r = &c->tags[x->aux];
		char nb[256];

		if (x->aux == c->valist_tag && c->valist_tag) {
			vfixbuf_puts(&b, KIND(c, c->t_valist) == TY_ARRAY ? "struct __va_list_tag" : "__builtin_va_list");
		} else if (r->name) {
			uint32_t s = lookup_tag(c, r->name);

			if (!s || (uint32_t)c->syms[s].val != x->aux)
				*ok = false;
			snprintf(nb, sizeof(nb), "%s %.*s", x->kind == TY_STRUCT ? "struct" : x->kind == TY_UNION ? "union" : "enum",
				 IDF(r->name));
			vfixbuf_puts(&b, nb);
		} else if (r->typedef_name) {
			uint32_t s = lookup(c, r->typedef_name);

			if (!s || c->syms[s].kind != S_TYPEDEF || T(c, c->syms[s].ty)->canon != T(c, t)->canon)
				*ok = false;
			snprintf(nb, sizeof(nb), "%.*s", IDF(r->typedef_name));
			vfixbuf_puts(&b, nb);
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
			vfixbuf_puts(&b, n);
	}
	}
	if (inner[0]) {
		if (inner[0] != '[')	/* clang: 'int[2]', 'int (*)[2]' */
			vfixbuf_puts(&b, " ");
		vfixbuf_puts(&b, inner);
	}
	vfixbuf_put(out, b.s, b.n);
}

/* the next of the TU's 8 type-name buffers (per TU: vccd compiles on many threads) */
static vfixbuf *tybuf(cc *c)
{
	if (unlikely(!c->tyb) && !(c->tyb = malloc(8 * sizeof(vfixbuf))))
		abort();
	return &c->tyb[c->tyk++ & 7];
}

/* t as clang spells it in diagnostics (buffers in rotation: tybuf) */
VPRIV const char *tyname(cc *c, qty t)
{
	bool ok = true;
	vfixbuf *o = tybuf(c);

	o->n = 0, o->s[0] = 0;
	type_str(c, t, "", o, &ok);
	return o->s;
}

VPRIV void layout_checks(cc *c)
{
	vfdbuf o = { malloc(1 << 20), 0, 1 << 20, c->o->outfd };
	char rn[512];

	vfdbuf_printf(&o, "\n/* vcc layout checks */\n");
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
		vfdbuf_printf(&o, "_Static_assert(sizeof(%s) == %llu && _Alignof(%s) == %u, \"vcc\");\n", rn,
			(unsigned long long)r->size, rn, r->align);
		if (k == TY_ENUM) {
			const char *bn = int_name(KIND(c, c->ty[r->ty].base));

			if (bn)
				vfdbuf_printf(&o, "_Static_assert(__builtin_types_compatible_p(%s, %s), \"vcc\");\n", rn, bn);
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
				vfdbuf_printf(&o, "_Static_assert(sizeof(%.*s) == %llu && _Alignof(%.*s) == %u, \"vcc\");\n",
					IDF(s->name), (unsigned long long)ty_size(c, s->ty), IDF(s->name), ty_align(c, s->ty));
		} else if (s->kind == S_VAR && complete(c, s->ty)) {
			vfdbuf_printf(&o, "_Static_assert(sizeof(%.*s) == %llu, \"vcc\");\n", IDF(s->name),
				(unsigned long long)ty_size(c, s->ty));
		}
		if (s->kind == S_VAR || s->kind == S_FUNC) {
			vfixbuf b;
			bool ok = true;

			b.n = 0, b.s[0] = 0;
			type_str(c, s->ty, "", &b, &ok);
			if (ok)
				vfdbuf_printf(&o, "_Static_assert(__builtin_types_compatible_p(__typeof__(%.*s), %s), \"vcc\");\n",
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
		vfdbuf_printf(&o, "_Static_assert((%.*s) == %lldLL && __builtin_types_compatible_p(__typeof__(%.*s), %s), \"vcc\");\n",
			IDF(s->name), (long long)s->val, IDF(s->name), tn);
	}
	vfdbuf_flush(&o);
	free(o.p);
}
