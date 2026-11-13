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
/* a type spelling: 4096 bytes on the stack; a longer one fails the job */
#define SPELLING(name) char name##_bytes[4096]; vbuffer name; vbuffer_fixed(&name, name##_bytes, sizeof(name##_bytes))

static void quals(vbuffer *b, int q)
{
	if (q & Q_CONST) vbuffer_string_append(b, "const ");
	if (q & Q_VOLATILE) vbuffer_string_append(b, "volatile ");
	if (q & Q_RESTRICT) vbuffer_string_append(b, "restrict ");
}

/* out = spelling of t around the declarator text inner */
VPRIV void type_str(cc *c, qty t, const char *inner, vbuffer *out, bool *ok)
{
	const ctype *x = T(c, t);
	SPELLING(b);

	if (x->kind == TY_ARRAY && c->valist_tag && T(c, c->t_valist)->canon == x->canon) {
		/* x86-64's __va_list_tag[1] is spelled by its typedef, the struct as itself */
		quals(&b, QQ(t));
		vbuffer_string_append(&b, "__builtin_va_list");
		if (inner[0]) {
			vbuffer_string_append(&b, " ");
			vbuffer_string_append(&b, inner);
		}
		vbuffer_append(out, b.p, b.n);
		return;
	}
	switch (x->kind) {
	case TY_SVE:
		quals(&b, QQ(t));
		vbuffer_append(&b, idstr_(x->aux), idlen_(x->aux));
		if (inner[0]) {
			vbuffer_string_append(&b, " ");
			vbuffer_string_append(&b, inner);
		}
		vbuffer_append(out, b.p, b.n);
		return;
	case TY_PTR: {
		SPELLING(in);
		const ctype *bt = T(c, x->base);
		/* __builtin_va_list * (x86-64: an array spelled by its typedef) */
		bool wrap = (bt->kind == TY_ARRAY && !(c->valist_tag && T(c, c->t_valist)->canon == bt->canon)) || bt->kind == TY_FUNC;

		if (wrap)
			vbuffer_string_append(&in, x->flags & TF_BLOCK ? "(^" : "(*");
		else
			vbuffer_string_append(&in, x->flags & TF_BLOCK ? "^" : "*");
		if (QQ(t)) {	/* clang: "int *const", "int *const *", "int *const p" */
			quals(&in, QQ(t));
			if (!inner[0] && in.n && in.p[in.n - 1] == ' ')
				in.p[--in.n] = 0;
		}
		vbuffer_string_append(&in, inner);
		if (wrap)
			vbuffer_string_append(&in, ")");
		type_str(c, x->base, in.p, out, ok);
		return;
	}
	case TY_ARRAY: {
		SPELLING(in);
		char n[32];

		vbuffer_string_append(&in, inner);
		if (x->flags & (TF_INCOMPLETE | TF_VLA))
			vbuffer_string_append(&in, "[]");
		else {
			snprintf(n, sizeof(n), "[%llu]", (unsigned long long)arr_len(c, t));
			vbuffer_string_append(&in, n);
		}
		type_str(c, QT(QI(x->base), QQ(x->base) | QQ(t)), in.p, out, ok);
		return;
	}
	case TY_FUNC: {
		SPELLING(in);

		vbuffer_string_append(&in, inner);
		vbuffer_string_append(&in, "(");
		if (x->flags & TF_NOPROTO) {
		} else if (!x->n && !(x->flags & TF_VARIADIC)) {
			vbuffer_string_append(&in, "void");
		} else {
			for (uint32_t i = 0; i < x->n; i++) {
				SPELLING(p);

				if (i)
					vbuffer_string_append(&in, ", ");
				type_str(c, c->params[x->aux + i], "", &p, ok);
				vbuffer_append(&in, p.p, p.n);
			}
			if (x->flags & TF_VARIADIC)
				vbuffer_string_append(&in, ", ...");
		}
		vbuffer_string_append(&in, ")");
		x = T(c, t);
		type_str(c, x->base, in.p, out, ok);
		return;
	}
	case TY_VECTOR: {
		/* clang without the typedef: __attribute__((__vector_size__(4 * sizeof(int)))) int */
		SPELLING(el);
		char n[96];
		uint32_t td = 0;

		type_str(c, x->base, "", &el, ok);
		/* clang keeps the typedef name (vcc: the first typedef of this vector type) */
		for (uint32_t i = 1; i < c->nsym && !td; i++)
			if (c->syms[i].kind == S_TYPEDEF && c->syms[i].name && QI(c->syms[i].ty) == QI(t) && !QQ(c->syms[i].ty))
				td = i;
		quals(&b, QQ(t));
		if (td) {
			vbuffer_append(&b, idstr_(c->syms[td].name), idlen_(c->syms[td].name));
			if (inner[0]) {
				vbuffer_string_append(&b, " ");
				vbuffer_string_append(&b, inner);
			} else {	/* \x01: the diagnostic printer closes the quote before it */
				snprintf(n, sizeof(n), "\x01(vector of %u '", x->n);
				vbuffer_string_append(&b, n);
				vbuffer_append(&b, el.p, el.n);
				vbuffer_string_append(&b, "' values)");
			}
			vbuffer_append(out, b.p, b.n);
			*ok = false;
			return;
		}
		snprintf(n, sizeof(n), "__attribute__((__vector_size__(%llu * sizeof(",
			 (unsigned long long)(ty_size(c, t) / ty_size(c, x->base)));
		vbuffer_string_append(&b, n);
		vbuffer_append(&b, el.p, el.n);
		vbuffer_string_append(&b, ")))) ");
		vbuffer_append(&b, el.p, el.n);
		if (inner[0]) {
			vbuffer_string_append(&b, " ");
			vbuffer_string_append(&b, inner);
		}
		vbuffer_append(out, b.p, b.n);
		*ok = false;	/* not a spelling layout checks can use */
		return;
	}
	default:
		break;
	}
	quals(&b, QQ(t));
	switch (x->kind) {
	case TY_VOID: vbuffer_string_append(&b, "void"); break;
	case TY_BFLOAT16: vbuffer_string_append(&b, "__bf16"); break;
	case TY_FLOAT16: vbuffer_string_append(&b, "_Float16"); break;
	case TY_FLOAT: vbuffer_string_append(&b, "float"); break;
	case TY_DOUBLE: vbuffer_string_append(&b, "double"); break;
	case TY_LDOUBLE: vbuffer_string_append(&b, "long double"); break;
	case TY_FLOAT128: vbuffer_string_append(&b, "__float128"); break;
	case TY_STRUCT: case TY_UNION: case TY_ENUM: {
		const tag *r = &c->tags[x->aux];
		char nb[256];

		if (x->aux == c->valist_tag && c->valist_tag) {
			vbuffer_string_append(&b, KIND(c, c->t_valist) == TY_ARRAY ? "struct __va_list_tag" : "__builtin_va_list");
		} else if (r->name) {
			uint32_t s = lookup_tag(c, r->name);

			if (!s || (uint32_t)c->syms[s].val != x->aux)
				*ok = false;
			snprintf(nb, sizeof(nb), "%s %.*s", x->kind == TY_STRUCT ? "struct" : x->kind == TY_UNION ? "union" : "enum",
				 IDF(r->name));
			vbuffer_string_append(&b, nb);
		} else if (r->typedef_name) {
			uint32_t s = lookup(c, r->typedef_name);

			if (!s || c->syms[s].kind != S_TYPEDEF || T(c, c->syms[s].ty)->canon != T(c, t)->canon)
				*ok = false;
			snprintf(nb, sizeof(nb), "%.*s", IDF(r->typedef_name));
			vbuffer_string_append(&b, nb);
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
			vbuffer_string_append(&b, bn);
			break;
		}

		if (!n)
			*ok = false;
		else
			vbuffer_string_append(&b, n);
	}
	}
	if (inner[0]) {
		if (inner[0] != '[')	/* clang: 'int[2]', 'int (*)[2]' */
			vbuffer_string_append(&b, " ");
		vbuffer_string_append(&b, inner);
	}
	vbuffer_append(out, b.p, b.n);
}

/* the next of the TU's 8 type-name buffers (per TU: vccd compiles on many threads) */
struct cc_type_name {
	vbuffer buffer;
	char bytes[4096];
};

static vbuffer *tybuf(cc *c)
{
	struct cc_type_name *s;

	if (unlikely(!c->tyb) && !(c->tyb = malloc(8 * sizeof(*c->tyb))))
		abort();
	s = &c->tyb[c->tyk++ & 7];
	vbuffer_fixed(&s->buffer, s->bytes, sizeof(s->bytes));
	return &s->buffer;
}

/* t as clang spells it in diagnostics (buffers in rotation: tybuf) */
VPRIV const char *tyname(cc *c, qty t)
{
	bool ok = true;
	vbuffer *o = tybuf(c);

	type_str(c, t, "", o, &ok);
	return o->p;
}
