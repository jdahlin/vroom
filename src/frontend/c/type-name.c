/* vcc front end: types spelled as clang does in diagnostics, and as C for the layout checks */
#include "ccimpl.h"
#include "common/buffer.h"

VPRIV const char *int_name(int k)
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
VPRIV void type_str(cc *c, qty t, const char *inner, vfixbuf *out, bool *ok)
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

		if (is_int_kind(x->kind) && x->n && !(x->flags & TF_ATOMIC)) {
			char bn[64];

			snprintf(bn, sizeof(bn), "%s_BitInt(%u)", is_signed(c, t) ? "" : "unsigned ", x->n);
			vfixbuf_puts(&b, bn);
			break;
		}

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
