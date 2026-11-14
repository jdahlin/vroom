/* vcc front end: types spelled as clang does in diagnostics, and as C for the layout checks */
#include "cc-internal.h"
#include "common/buffer.h"

CC_PRIVATE const char *cc_type_integer_name(int k)
{
	switch (k) {
	case AST_TYPE_BOOL: return "_Bool";
	case AST_TYPE_CHAR: return "char";
	case AST_TYPE_SIGNED_CHAR: return "signed char";
	case AST_TYPE_UNSIGNED_CHAR: return "unsigned char";
	case AST_TYPE_SHORT: return "short";
	case AST_TYPE_UNSIGNED_SHORT: return "unsigned short";
	case AST_TYPE_INT: return "int";
	case AST_TYPE_UNSIGNED_INT: return "unsigned int";
	case AST_TYPE_LONG: return "long";
	case AST_TYPE_UNSIGNED_LONG: return "unsigned long";
	case AST_TYPE_LONG_LONG: return "long long";
	case AST_TYPE_UNSIGNED_LONG_LONG: return "unsigned long long";
	case AST_TYPE_INT128: return "__int128";
	case AST_TYPE_UINT128: return "unsigned __int128";
	default: return NULL;
	}
}

/* C spelling of a type for the checks (*ok = false if it cannot be named) */
/* a type spelling: 4096 bytes on the stack; a longer one fails the job */
#define SPELLING(name) char name##_bytes[4096]; vbuffer name; vbuffer_fixed(&name, name##_bytes, sizeof(name##_bytes))

static void cc_type_qualifiers_spell(vbuffer *b, int q)
{
	if (q & AST_QUALIFIER_CONST) vbuffer_string_append(b, "const ");
	if (q & AST_QUALIFIER_VOLATILE) vbuffer_string_append(b, "volatile ");
	if (q & AST_QUALIFIER_RESTRICT) vbuffer_string_append(b, "restrict ");
}

/* out = spelling of t around the declarator text inner */
CC_PRIVATE void cc_type_spell(cc *c, ast_qualified_type t, const char *inner, vbuffer *out, bool *ok)
{
	const ctype *x = AST_TYPE(c, t);
	SPELLING(b);

	if (x->kind == AST_TYPE_ARRAY && c->valist_tag && AST_TYPE(c, c->t_valist)->canon == x->canon) {
		/* x86-64's __va_list_tag[1] is spelled by its typedef, the struct as itself */
		cc_type_qualifiers_spell(&b, AST_QUALIFIED_TYPE_QUALIFIERS(t));
		vbuffer_string_append(&b, "__builtin_va_list");
		if (inner[0]) {
			vbuffer_string_append(&b, " ");
			vbuffer_string_append(&b, inner);
		}
		vbuffer_append(out, b.p, b.n);
		return;
	}
	switch (x->kind) {
	case AST_TYPE_SVE_BUILTIN:
		cc_type_qualifiers_spell(&b, AST_QUALIFIED_TYPE_QUALIFIERS(t));
		vbuffer_append(&b, cc_identifier_string(x->aux), cc_identifier_length(x->aux));
		if (inner[0]) {
			vbuffer_string_append(&b, " ");
			vbuffer_string_append(&b, inner);
		}
		vbuffer_append(out, b.p, b.n);
		return;
	case AST_TYPE_POINTER: {
		SPELLING(in);
		const ctype *bt = AST_TYPE(c, x->base);
		/* __builtin_va_list * (x86-64: an array spelled by its typedef) */
		bool wrap = (bt->kind == AST_TYPE_ARRAY && !(c->valist_tag && AST_TYPE(c, c->t_valist)->canon == bt->canon)) || bt->kind == AST_TYPE_FUNCTION;

		if (wrap)
			vbuffer_string_append(&in, x->flags & AST_TYPE_FLAG_BLOCK ? "(^" : "(*");
		else
			vbuffer_string_append(&in, x->flags & AST_TYPE_FLAG_BLOCK ? "^" : "*");
		if (AST_QUALIFIED_TYPE_QUALIFIERS(t)) {	/* clang: "int *const", "int *const *", "int *const p" */
			cc_type_qualifiers_spell(&in, AST_QUALIFIED_TYPE_QUALIFIERS(t));
			if (!inner[0] && in.n && in.p[in.n - 1] == ' ')
				in.p[--in.n] = 0;
		}
		vbuffer_string_append(&in, inner);
		if (wrap)
			vbuffer_string_append(&in, ")");
		cc_type_spell(c, x->base, in.p, out, ok);
		return;
	}
	case AST_TYPE_ARRAY: {
		SPELLING(in);
		char n[32];

		vbuffer_string_append(&in, inner);
		if (x->flags & (AST_TYPE_FLAG_INCOMPLETE | AST_TYPE_FLAG_VLA))
			vbuffer_string_append(&in, "[]");
		else {
			snprintf(n, sizeof(n), "[%llu]", (unsigned long long)cc_type_array_length(c, t));
			vbuffer_string_append(&in, n);
		}
		cc_type_spell(c, AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(x->base), AST_QUALIFIED_TYPE_QUALIFIERS(x->base) | AST_QUALIFIED_TYPE_QUALIFIERS(t)), in.p, out, ok);
		return;
	}
	case AST_TYPE_FUNCTION: {
		SPELLING(in);

		vbuffer_string_append(&in, inner);
		vbuffer_string_append(&in, "(");
		if (x->flags & AST_TYPE_FLAG_NO_PROTOTYPE) {
		} else if (!x->n && !(x->flags & AST_TYPE_FLAG_VARIADIC)) {
			vbuffer_string_append(&in, "void");
		} else {
			for (uint32_t i = 0; i < x->n; i++) {
				SPELLING(p);

				if (i)
					vbuffer_string_append(&in, ", ");
				cc_type_spell(c, c->params[x->aux + i], "", &p, ok);
				vbuffer_append(&in, p.p, p.n);
			}
			if (x->flags & AST_TYPE_FLAG_VARIADIC)
				vbuffer_string_append(&in, ", ...");
		}
		vbuffer_string_append(&in, ")");
		if (x->ext)	/* x86-64: the other calling convention, as clang prints it */
			vbuffer_string_append(&in, x->ext == CC_FNABI_MS ? " __attribute__((ms_abi))" : " __attribute__((sysv_abi))");
		x = AST_TYPE(c, t);
		cc_type_spell(c, x->base, in.p, out, ok);
		return;
	}
	case AST_TYPE_VECTOR: {
		/* clang without the typedef: __attribute__((__vector_size__(4 * sizeof(int)))) int */
		SPELLING(el);
		char n[96];
		uint32_t td = 0;

		cc_type_spell(c, x->base, "", &el, ok);
		/* clang keeps the typedef name (vcc: the first typedef of this vector type) */
		for (uint32_t i = 1; i < c->nsym && !td; i++)
			if (c->syms[i].kind == AST_SYMBOL_TYPEDEF && c->syms[i].name && AST_QUALIFIED_TYPE_INDEX(c->syms[i].ty) == AST_QUALIFIED_TYPE_INDEX(t) && !AST_QUALIFIED_TYPE_QUALIFIERS(c->syms[i].ty))
				td = i;
		cc_type_qualifiers_spell(&b, AST_QUALIFIED_TYPE_QUALIFIERS(t));
		if (td) {
			vbuffer_append(&b, cc_identifier_string(c->syms[td].name), cc_identifier_length(c->syms[td].name));
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
			 (unsigned long long)(cc_type_size(c, t) / cc_type_size(c, x->base)));
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
	cc_type_qualifiers_spell(&b, AST_QUALIFIED_TYPE_QUALIFIERS(t));
	switch (x->kind) {
	case AST_TYPE_VOID: vbuffer_string_append(&b, "void"); break;
	case AST_TYPE_BFLOAT16: vbuffer_string_append(&b, "__bf16"); break;
	case AST_TYPE_FLOAT16: vbuffer_string_append(&b, "_Float16"); break;
	case AST_TYPE_FLOAT: vbuffer_string_append(&b, "float"); break;
	case AST_TYPE_DOUBLE: vbuffer_string_append(&b, "double"); break;
	case AST_TYPE_LONG_DOUBLE: vbuffer_string_append(&b, "long double"); break;
	case AST_TYPE_FLOAT128: vbuffer_string_append(&b, "__float128"); break;
	case AST_TYPE_STRUCT: case AST_TYPE_UNION: case AST_TYPE_ENUM: {
		const tag *r = &c->tags[x->aux];
		char nb[256];

		if (x->aux == c->valist_tag && c->valist_tag) {
			vbuffer_string_append(&b, AST_TYPE_KIND(c, c->t_valist) == AST_TYPE_ARRAY ? "struct __va_list_tag" : "__builtin_va_list");
		} else if (r->name) {
			uint32_t s = cc_tag_lookup(c, r->name);

			if (!s || (uint32_t)c->syms[s].val != x->aux)
				*ok = false;
			snprintf(nb, sizeof(nb), "%s %.*s", x->kind == AST_TYPE_STRUCT ? "struct" : x->kind == AST_TYPE_UNION ? "union" : "enum",
				 CC_IDENTIFIER(r->name));
			vbuffer_string_append(&b, nb);
		} else if (r->typedef_name) {
			uint32_t s = cc_symbol_lookup(c, r->typedef_name);

			if (!s || c->syms[s].kind != AST_SYMBOL_TYPEDEF || AST_TYPE(c, c->syms[s].ty)->canon != AST_TYPE(c, t)->canon)
				*ok = false;
			snprintf(nb, sizeof(nb), "%.*s", CC_IDENTIFIER(r->typedef_name));
			vbuffer_string_append(&b, nb);
		} else {
			*ok = false;
		}
		break;
	}
	default: {
		const char *n = cc_type_integer_name(x->kind);

		if (cc_type_kind_is_integer(x->kind) && x->n && !(x->flags & AST_TYPE_FLAG_ATOMIC)) {
			char bn[64];

			snprintf(bn, sizeof(bn), "%s_BitInt(%u)", cc_type_is_signed(c, t) ? "" : "unsigned ", x->n);
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

static vbuffer *cc_type_name_buffer(cc *c)
{
	struct cc_type_name *s;

	if (unlikely(!c->tyb) && !(c->tyb = malloc(8 * sizeof(*c->tyb))))
		abort();
	s = &c->tyb[c->tyk++ & 7];
	vbuffer_fixed(&s->buffer, s->bytes, sizeof(s->bytes));
	return &s->buffer;
}

/* t as clang spells it in diagnostics (buffers in rotation: tybuf) */
CC_PRIVATE const char *tyname(cc *c, ast_qualified_type t)
{
	bool ok = true;
	vbuffer *o = cc_type_name_buffer(c);

	cc_type_spell(c, t, "", o, &ok);
	return o->p;
}
