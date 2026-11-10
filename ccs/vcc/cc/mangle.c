/* vcc front end: overloadable: Itanium mangling as clang does it for C */
#include "ccimpl.h"

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
		[TY_FLOAT16] = "DF16_", [TY_FLOAT] = "f", [TY_DOUBLE] = "d", [TY_LDOUBLE] = "e", [TY_FLOAT128] = "g",
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
VPRIV void name_overload(cc *c, uint32_t s)
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
