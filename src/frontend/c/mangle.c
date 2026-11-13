/* vcc front end: overloadable: Itanium mangling as clang does it for C */
#include "cc-internal.h"

/* ---------------- overloadable: Itanium mangling as clang does it for C ---------------- */

typedef struct cc_mangler {
	char b[512];
	uint32_t n;
	ast_qualified_type sub[64];		/* substitution candidates, in order: canonical (qualified) types */
	uint32_t nsub;
	bool full;
} cc_mangler;

static void cc_mangle_append(cc_mangler *m, const char *s, size_t l)
{
	if (m->n + l >= sizeof(m->b)) {
		m->full = true;
		return;
	}
	memcpy(m->b + m->n, s, l);
	m->n += (uint32_t)l;
}

static void cc_mangle_append_text(cc_mangler *m, const char *s) { cc_mangle_append(m, s, strlen(s)); }

static void cc_mangle_append_number(cc_mangler *m, uint64_t v)
{
	char t[24];

	cc_mangle_append(m, t, (size_t)snprintf(t, sizeof(t), "%llu", (unsigned long long)v));
}

static void cc_mangle_append_source_name(cc_mangler *m, uint32_t id)
{
	int l;
	const char *s = cc_identifier_name(id, &l);

	cc_mangle_append_number(m, (uint64_t)l);
	cc_mangle_append(m, s, (size_t)l);
}

/* S_, S0_, S1_ ... SZ_, S10_ for a type seen before; false if t is new */
static bool cc_mangle_substitution_find(cc_mangler *m, ast_qualified_type t)
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
		cc_mangle_append(m, t2, (size_t)k);
		return true;
	}
	return false;
}

static void cc_mangle_substitution_add(cc_mangler *m, ast_qualified_type t)
{
	if (m->nsub < 64)
		m->sub[m->nsub++] = t;
}

static void cc_mangle_type(cc *c, cc_mangler *m, ast_qualified_type t)
{
	static const char *const prim_code[] = {
		[AST_TYPE_VOID] = "v", [AST_TYPE_BOOL] = "b", [AST_TYPE_CHAR] = "c", [AST_TYPE_SIGNED_CHAR] = "a", [AST_TYPE_UNSIGNED_CHAR] = "h",
		[AST_TYPE_SHORT] = "s", [AST_TYPE_UNSIGNED_SHORT] = "t", [AST_TYPE_INT] = "i", [AST_TYPE_UNSIGNED_INT] = "j", [AST_TYPE_LONG] = "l",
		[AST_TYPE_UNSIGNED_LONG] = "m", [AST_TYPE_LONG_LONG] = "x", [AST_TYPE_UNSIGNED_LONG_LONG] = "y", [AST_TYPE_INT128] = "n", [AST_TYPE_UINT128] = "o",
		[AST_TYPE_BFLOAT16] = "DF16b", [AST_TYPE_FLOAT16] = "DF16_", [AST_TYPE_FLOAT] = "f", [AST_TYPE_DOUBLE] = "d", [AST_TYPE_LONG_DOUBLE] = "e", [AST_TYPE_FLOAT128] = "g",
	};
	int q = AST_QUALIFIED_TYPE_QUALIFIERS(t), k;
	const ctype *x;

	t = canon(c, t);	/* keeps the qualifiers */
	k = AST_TYPE_KIND(c, t);
	if (!q && k < AST_TYPE_POINTER) {
		cc_mangle_append_text(m, prim_code[k]);
		return;
	}
	if (cc_mangle_substitution_find(m, t))
		return;
	if (q) {
		if (q & AST_QUALIFIER_RESTRICT) cc_mangle_append_text(m, "r");
		if (q & AST_QUALIFIER_VOLATILE) cc_mangle_append_text(m, "V");
		if (q & AST_QUALIFIER_CONST) cc_mangle_append_text(m, "K");
		cc_mangle_type(c, m, AST_QUALIFIED_TYPE_UNQUALIFIED(t));
		cc_mangle_substitution_add(m, t);
		return;
	}
	x = AST_TYPE(c, t);
	switch (k) {
	case AST_TYPE_POINTER:
		cc_mangle_append_text(m, "P");
		cc_mangle_type(c, m, x->base);
		break;
	case AST_TYPE_ARRAY:
		cc_mangle_append_text(m, "A");
		if (!(x->flags & (AST_TYPE_FLAG_INCOMPLETE | AST_TYPE_FLAG_VLA)))
			cc_mangle_append_number(m, x->n);
		cc_mangle_append_text(m, "_");
		cc_mangle_type(c, m, x->base);
		break;
	case AST_TYPE_FUNCTION:
		cc_mangle_append_text(m, "F");
		cc_mangle_type(c, m, x->base);
		if (!x->n && !(x->flags & AST_TYPE_FLAG_VARIADIC))
			cc_mangle_append_text(m, "v");
		for (uint32_t i = 0; i < x->n; i++)
			cc_mangle_type(c, m, AST_QUALIFIED_TYPE_UNQUALIFIED(c->params[x->aux + i]));
		if (x->flags & AST_TYPE_FLAG_VARIADIC)
			cc_mangle_append_text(m, "z");
		cc_mangle_append_text(m, "E");
		break;
	default: {	/* struct, union, enum */
		const tag *r = &c->tags[x->aux];
		uint32_t id = r->name ? r->name : r->typedef_name;

		if (id)
			cc_mangle_append_source_name(m, id);
		else
			cc_mangle_append_text(m, "Ut_");
		break;
	}
	}
	cc_mangle_substitution_add(m, t);
}

/* _Z[L]<name><params> as clang mangles an overloadable C function; 0 if it does not fit */
static uint32_t cc_mangle_overload(cc *c, uint32_t s)
{
	const sym *y = &c->syms[s];
	const ctype *f = AST_TYPE(c, y->ty);
	cc_mangler m;
	uint32_t si;
	char *keep;

	m.n = m.nsub = 0;
	m.full = false;
	cc_mangle_append_text(&m, y->sc == AST_STORAGE_STATIC ? "_ZL" : "_Z");
	cc_mangle_append_source_name(&m, y->name);
	if (!f->n && !(f->flags & AST_TYPE_FLAG_VARIADIC))
		cc_mangle_append_text(&m, "v");
	for (uint32_t i = 0; i < f->n; i++)
		cc_mangle_type(c, &m, AST_QUALIFIED_TYPE_UNQUALIFIED(c->params[f->aux + i]));
	if (f->flags & AST_TYPE_FLAG_VARIADIC)
		cc_mangle_append_text(&m, "z");
	if (m.full)
		return 0;
	keep = cc_heap_allocate(c, m.n + 1);
	memcpy(keep, m.b, m.n);
	keep[m.n] = 0;
	si = c->nstr++;
	c->strs[si] = (cstr){ keep, m.n + 1, 1, LEX_TOKEN_STRING };
	return si + 1;
}

/* give an overloadable function its mangled name (unless it has an asm label) */
CC_PRIVATE void cc_mangle_overload_name_set(cc *c, uint32_t s)
{
	sym *y = &c->syms[s];
	attrs a;

	if (y->attrs && c->attr[y->attrs].asmname)
		return;
	memset(&a, 0, sizeof(a));
	a.asmname = cc_mangle_overload(c, s);
	if (a.asmname)
		cc_attribute_apply_symbol(c, y, &a);
}
