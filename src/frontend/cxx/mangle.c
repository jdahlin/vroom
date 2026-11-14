/*
 * C++ names for symbols: the Itanium ABI's mangling (PLAN-cxx.md, code generation).
 *
 * What is mangled so far: functions at namespace scope and member functions (not templates: an
 * instantiated function template's name is written in terms of its template, later), with parameters
 * of built-in types, pointers, references, cv-qualified types, classes, class template instances whose
 * arguments are types, and function types.  Substitutions (S_, S0_, ...) as the ABI numbers them.
 * False from cxx_mangle_function: something in the name is not mangled yet; the function gets no code.
 * bench/cxx-mangle.py compares every name with clang's.
 */
#include "cxx-internal.h"

typedef struct cxx_mangler {
	cxx *c;
	char b[2048];
	size_t n;
	bool bad;
	/* substitution candidates, in the order they were completed: a scope (namespace or class), a
	   template (its entity), or a type */
	struct { uint8_t kind; const void *p; qty t; } subs[128];
	uint32_t nsubs;
} cxx_mangler;

enum { CXX_SUB_SCOPE = 1, CXX_SUB_TEMPLATE, CXX_SUB_TYPE };

static void mg_text(cxx_mangler *m, const char *s, size_t l)
{
	if (m->n + l >= sizeof m->b) {
		m->bad = true;
		return;
	}
	memcpy(m->b + m->n, s, l);
	m->n += l;
}

static void mg_str(cxx_mangler *m, const char *s) { mg_text(m, s, strlen(s)); }

static void mg_source_name(cxx_mangler *m, uint32_t name)
{
	const vident *v = &vintern_ents[name];
	char num[16];

	mg_text(m, num, (size_t)snprintf(num, sizeof num, "%u", (unsigned)v->len));
	mg_text(m, v->s, v->len);
}

static bool mg_substitution(cxx_mangler *m, int kind, const void *p, qty t)
{
	for (uint32_t i = 0; i < m->nsubs; i++)
		if (m->subs[i].kind == kind && m->subs[i].p == p && m->subs[i].t == t) {
			char num[16];
			size_t l = 0;

			mg_str(m, "S");
			if (i) {	/* S_ is the first; then S0_, S1_, ... in base 36 */
				uint32_t v = i - 1;
				char rev[8];
				size_t k = 0;

				do
					rev[k++] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"[v % 36];
				while ((v /= 36));
				while (k)
					num[l++] = rev[--k];
			}
			num[l++] = '_';
			mg_text(m, num, l);
			return true;
		}
	return false;
}

static void mg_remember(cxx_mangler *m, int kind, const void *p, qty t)
{
	if (m->nsubs == 128) {
		m->bad = true;
		return;
	}
	m->subs[m->nsubs].kind = (uint8_t)kind, m->subs[m->nsubs].p = p, m->subs[m->nsubs].t = t;
	m->nsubs++;
}

/* the namespace or class a scope is in (the global scope for the outermost) */
static cxx_scope *mg_enclosing(cxx_mangler *m, cxx_scope *s)
{
	for (s = s ? s->parent : NULL; s; s = s->parent)
		if (s->kind == CXX_SCOPE_NAMESPACE || s->kind == CXX_SCOPE_CLASS)
			return s;
	return m->c->global;
}

static bool mg_scope_is_std(cxx_mangler *m, const cxx_scope *s)
{
	return s && s->kind == CXX_SCOPE_NAMESPACE && s->owner && s->parent == m->c->global && vintern_ents[s->owner->name].len == 3 &&
	       !memcmp(vintern_ents[s->owner->name].s, "std", 3);
}

static void mg_type(cxx_mangler *m, qty t);
static void mg_prefix(cxx_mangler *m, cxx_scope *s);

/* <template-args> of a class template instance */
static void mg_template_args(cxx_mangler *m, const cxx_targ *args, uint32_t nargs, uint32_t pack_at)
{
	mg_str(m, "I");
	for (uint32_t i = 0; i < nargs; i++) {
		if (i == pack_at)
			mg_str(m, "J");
		if (args[i].kind == 1 && args[i].t != CXX_TY_UNKNOWN)
			mg_type(m, args[i].t);
		else if (args[i].kind == 2) {
			/* a value: L <type> <value> E; the type is the parameter's, known here for enumerations only:
			   others as int (not clang's name for a size_t or bool argument yet) */
			char num[32];

			mg_str(m, "L");
			if (args[i].t)
				mg_type(m, args[i].t);
			else
				mg_str(m, "i");
			mg_text(m, num, (size_t)snprintf(num, sizeof num, args[i].v < 0 ? "n%llu" : "%llu",
							 (unsigned long long)(args[i].v < 0 ? 0 - (uint64_t)args[i].v : (uint64_t)args[i].v)));
			mg_str(m, "E");
		} else
			m->bad = true;	/* (templates as arguments: not mangled yet) */
	}
	if (pack_at <= nargs)
		mg_str(m, "E");
	mg_str(m, "E");
}

/* a class as the last component of a prefix (or of a nested name): its name, its template arguments */
static void mg_class_component(cxx_mangler *m, cxx_scope *s, qty t)
{
	cxx_entity *of;
	const cxx_targ *args;
	uint32_t nargs, pack_at;
	cxx_scope *home;

	if (mg_substitution(m, CXX_SUB_TYPE, NULL, t))
		return;
	if (cxx_instance_template(m->c, t, &of, &args, &nargs, &pack_at, &home)) {
		if (!mg_substitution(m, CXX_SUB_TEMPLATE, of, 0)) {
			if (mg_scope_is_std(m, home))
				mg_str(m, "St");
			else
				mg_prefix(m, home);
			mg_source_name(m, of->name);
			mg_remember(m, CXX_SUB_TEMPLATE, of, 0);
		}
		mg_template_args(m, args, nargs, pack_at);
	} else {
		cxx_scope *outer = mg_enclosing(m, s);

		if (!s->owner || s->owner->name <= 1) {
			m->bad = true;	/* (an unnamed class: its own numbering, later) */
			return;
		}
		if (mg_scope_is_std(m, outer))
			mg_str(m, "St");
		else
			mg_prefix(m, outer);
		mg_source_name(m, s->owner->name);
	}
	mg_remember(m, CXX_SUB_TYPE, NULL, t);
}

/* <prefix>: the namespaces and classes a name is in, outermost first (nothing for the global scope) */
static void mg_prefix(cxx_mangler *m, cxx_scope *s)
{
	if (!s || s == m->c->global)
		return;
	if (s->kind == CXX_SCOPE_CLASS) {
		if (!s->owner || s->owner->ty == CXX_TY_UNKNOWN) {
			m->bad = true;
			return;
		}
		mg_class_component(m, s, QUNQ(s->owner->ty));
		return;
	}
	if (mg_substitution(m, CXX_SUB_SCOPE, s, 0))
		return;
	if (mg_scope_is_std(m, s)) {
		mg_str(m, "St");	/* (never a substitution candidate itself) */
		return;
	}
	mg_prefix(m, mg_enclosing(m, s));
	if (s->owner)
		mg_source_name(m, s->owner->name);
	else
		mg_str(m, "12_GLOBAL__N_1");	/* an unnamed namespace */
	mg_remember(m, CXX_SUB_SCOPE, s, 0);
}

/* is a name declared in scope s written N...E (not in the global scope, not directly in std)? */
static bool mg_nested(cxx_mangler *m, cxx_scope *s)
{
	return s && s != m->c->global && !mg_scope_is_std(m, s);
}

static void mg_class_type(cxx_mangler *m, qty t)
{
	cxx_scope *s = cxx_tag_scope_get(m->c, t), *outer;
	cxx_entity *of;
	const cxx_targ *args;
	uint32_t nargs, pack_at;
	cxx_scope *home;

	if (m->c->nullptr_ty && t == m->c->nullptr_ty) {
		mg_str(m, "Dn");
		return;
	}
	bool instance = cxx_instance_template(m->c, t, &of, &args, &nargs, &pack_at, &home);

	if (!s && !instance) {
		m->bad = true;	/* (an instance needs no scope: it may never have been completed) */
		return;
	}
	if (mg_substitution(m, CXX_SUB_TYPE, NULL, t))
		return;
	outer = instance ? home : mg_enclosing(m, s);
	if (mg_nested(m, outer)) {
		mg_str(m, "N");
		mg_class_component(m, s, t);
		mg_str(m, "E");
		return;
	}
	mg_class_component(m, s, t);
}

static void mg_type(cxx_mangler *m, qty t)
{
	cc *a = m->c->ast;
	const ctype *x;
	int q;

	if (t == CXX_TY_UNKNOWN) {
		m->bad = true;
		return;
	}
	x = AST_TYPE(a, t);
	q = QQ(t);
	/* cv-qualified: the qualifiers before the type, each such type a substitution candidate */
	if ((q & (Q_CONST | Q_VOLATILE | Q_RESTRICT)) && x->kind != TY_FUNC && x->kind != TY_ARRAY) {
		if (mg_substitution(m, CXX_SUB_TYPE, NULL, t))
			return;
		if (q & Q_RESTRICT)
			mg_str(m, "r");
		if (q & Q_VOLATILE)
			mg_str(m, "V");
		if (q & Q_CONST)
			mg_str(m, "K");
		mg_type(m, QUNQ(t));
		mg_remember(m, CXX_SUB_TYPE, NULL, t);
		return;
	}
	switch (x->kind) {
	case TY_VOID: mg_str(m, "v"); return;
	case TY_BOOL: mg_str(m, "b"); return;
	case TY_CHAR: mg_str(m, "c"); return;
	case TY_SCHAR: mg_str(m, "a"); return;
	case TY_UCHAR: mg_str(m, "h"); return;
	case TY_SHORT: mg_str(m, "s"); return;
	case TY_USHORT: mg_str(m, "t"); return;
	case TY_INT: mg_str(m, "i"); return;
	case TY_UINT: mg_str(m, "j"); return;
	case TY_LONG: mg_str(m, "l"); return;
	case TY_ULONG: mg_str(m, "m"); return;
	case TY_LLONG: mg_str(m, "x"); return;
	case TY_ULLONG: mg_str(m, "y"); return;
	case TY_INT128: mg_str(m, "n"); return;
	case TY_UINT128: mg_str(m, "o"); return;
	case TY_FLOAT: mg_str(m, "f"); return;
	case TY_DOUBLE: mg_str(m, "d"); return;
	case TY_LDOUBLE: mg_str(m, "e"); return;
	case TY_FLOAT128: mg_str(m, "g"); return;
	case TY_PTR:
		if (mg_substitution(m, CXX_SUB_TYPE, NULL, t))
			return;
		mg_str(m, x->ext == 1 ? "R" : x->ext == 2 ? "O" : "P");
		mg_type(m, x->base);
		mg_remember(m, CXX_SUB_TYPE, NULL, t);
		return;
	case TY_FUNC:
		if (mg_substitution(m, CXX_SUB_TYPE, NULL, t))
			return;
		mg_str(m, "F");
		mg_type(m, x->base);
		if (!x->n && !(x->flags & TF_VARIADIC))
			mg_str(m, "v");
		for (uint32_t i = 0; i < x->n; i++)
			mg_type(m, a->params[x->aux + i]);
		if (x->flags & TF_VARIADIC)
			mg_str(m, "z");
		mg_str(m, "E");
		mg_remember(m, CXX_SUB_TYPE, NULL, t);
		return;
	case TY_ARRAY: {
		char num[24];

		if (mg_substitution(m, CXX_SUB_TYPE, NULL, t))
			return;
		mg_text(m, num, (size_t)snprintf(num, sizeof num, "A%u_", (unsigned)x->n));
		mg_type(m, x->base);
		mg_remember(m, CXX_SUB_TYPE, NULL, t);
		return;
	}
	case TY_STRUCT: case TY_UNION:
		mg_class_type(m, QUNQ(t));
		return;
	case TY_ENUM: {
		/* a named enumeration: its name where it is declared */
		cxx_scope *home = cxx_tag_home(m->c, t);
		uint32_t name = a->tags[x->aux].name;

		if (!home || name <= 1) {
			m->bad = true;
			return;
		}
		if (mg_substitution(m, CXX_SUB_TYPE, NULL, QUNQ(t)))
			return;
		if (mg_nested(m, home)) {
			mg_str(m, "N");
			mg_prefix(m, home);
			mg_source_name(m, name);
			mg_str(m, "E");
		} else {
			if (mg_scope_is_std(m, home))
				mg_str(m, "St");
			mg_source_name(m, name);
		}
		mg_remember(m, CXX_SUB_TYPE, NULL, QUNQ(t));
		return;
	}
	}
	m->bad = true;	/* (enumerations, vectors: not mangled yet) */
}

/* a parameter's type as the ABI mangles it: top-level cv dropped, arrays and functions as pointers */
static void mg_parameter(cxx_mangler *m, qty t)
{
	cc *a = m->c->ast;

	if (t != CXX_TY_UNKNOWN && AST_TYPE_KIND(a, t) == TY_ARRAY)
		t = cc_type_pointer(a, AST_TYPE(a, t)->base);
	else if (t != CXX_TY_UNKNOWN && AST_TYPE_KIND(a, t) == TY_FUNC)
		t = cc_type_pointer(a, t);
	else if (t != CXX_TY_UNKNOWN)
		t = QUNQ(t);
	mg_type(m, t);
}

/* an operator function's name: its two-letter code (NULL: not an operator, or one not mangled yet) */
static const char *mg_operator(const vident *v, uint32_t nparams, bool member)
{
	static const struct { const char *spelling, *unary, *binary; } ops[] = {
		{ "+", "ps", "pl" }, { "-", "ng", "mi" }, { "*", "de", "ml" }, { "&", "ad", "an" }, { "/", NULL, "dv" }, { "%", NULL, "rm" },
		{ "|", NULL, "or" }, { "^", NULL, "eo" }, { "~", "co", NULL }, { "!", "nt", NULL }, { "=", NULL, "aS" }, { "<", NULL, "lt" },
		{ ">", NULL, "gt" }, { "+=", NULL, "pL" }, { "-=", NULL, "mI" }, { "*=", NULL, "mL" }, { "/=", NULL, "dV" }, { "%=", NULL, "rM" },
		{ "&=", NULL, "aN" }, { "|=", NULL, "oR" }, { "^=", NULL, "eO" }, { "<<", NULL, "ls" }, { ">>", NULL, "rs" }, { "<<=", NULL, "lS" },
		{ ">>=", NULL, "rS" }, { "==", NULL, "eq" }, { "!=", NULL, "ne" }, { "<=", NULL, "le" }, { ">=", NULL, "ge" }, { "<=>", NULL, "ss" },
		{ "&&", NULL, "aa" }, { "||", NULL, "oo" }, { "++", "pp", "pp" }, { "--", "mm", "mm" }, { ",", NULL, "cm" }, { "->*", NULL, "pm" },
		{ "->", "pt", NULL }, { "()", "cl", "cl" }, { "[]", NULL, "ix" },
	};
	uint32_t operands = nparams + member;

	if (v->len <= 8 || memcmp(v->s, "operator", 8))
		return NULL;
	for (size_t i = 0; i < sizeof ops / sizeof *ops; i++)
		if (strlen(ops[i].spelling) == v->len - 8 && !memcmp(ops[i].spelling, v->s + 8, v->len - 8)) {
			if (!strcmp(ops[i].spelling, "()"))
				return "cl";
			return operands == 1 && ops[i].unary ? ops[i].unary : ops[i].binary;
		}
	return NULL;
}

/*
 * The mangled name of the function f declared in scope home (a namespace, or the class it is a member
 * of), of type ft; is_const: a const member function; variant: 1 or 2 for a constructor's or destructor's
 * complete-object or base-object function.  Written to buf (NUL-terminated); false if some part of it is
 * not mangled yet.
 */
VPRIV bool cxx_mangle_function(cxx *c, const cxx_entity *f, cxx_scope *home, qty ft, bool is_const, int variant, char *buf, size_t cap)
{
	cc *a = c->ast;
	cxx_mangler m;
	const ctype *x;
	const vident *v = &vintern_ents[f->name];
	bool member = home && home->kind == CXX_SCOPE_CLASS;
	const char *op;
	bool conv;

	if (ft == CXX_TY_UNKNOWN || AST_TYPE_KIND(a, ft) != TY_FUNC)
		return false;
	x = AST_TYPE(a, ft);
	m.c = c, m.n = 0, m.bad = false, m.nsubs = 0;
	op = mg_operator(v, x->n, member);
	conv = v->len > 10 && !memcmp(v->s, "operator #", 10);
	if (v->len > 8 && !memcmp(v->s, "operator", 8) && !op && !conv)
		return false;	/* (new/delete, literal operators: later) */
	/* a constructor or destructor: C1/D1 the complete object's, C2/D2 the base object's (variant) */
	if (v->len && v->s[0] == ' ') {
		if (!member || (variant != 1 && variant != 2 && !(variant == 3 && v->s[1] != 'c')))
			return false;
		op = v->s[1] == 'c' ? (variant == 1 ? "C1" : "C2") : (variant == 1 ? "D1" : variant == 3 ? "D0" : "D2");
	}
	mg_str(&m, "_Z");
	if (mg_nested(&m, home)) {
		mg_str(&m, "N");
		if (member && is_const)
			mg_str(&m, "K");
		mg_prefix(&m, home);
		if (f->fn_internal && !member && !op)
			mg_str(&m, "L");
		if (op)
			mg_str(&m, op);
		else if (conv) {
			mg_str(&m, "cv");
			mg_type(&m, x->base);
		} else
			mg_source_name(&m, f->name);
		if (f->is_instance)
			mg_template_args(&m, f->targs, f->ntargs, UINT32_MAX);
		mg_str(&m, "E");
	} else {
		if (mg_scope_is_std(&m, home))
			mg_str(&m, "St");
		if (f->fn_internal && !member && !op)
			mg_str(&m, "L");
		if (op)
			mg_str(&m, op);
		else
			mg_source_name(&m, f->name);
		if (f->is_instance)
			mg_template_args(&m, f->targs, f->ntargs, UINT32_MAX);
	}
	/* A function template's instance: the ABI writes its return type and its parameters as the template
	   declares them (T_ for a parameter); here the instance's own types, so the name is unique and stable
	   among vcc's objects but is not the one clang gives it. */
	if (f->is_instance && !(v->len && v->s[0] == ' '))
		mg_type(&m, x->base);
	if (!x->n && !(x->flags & TF_VARIADIC))
		mg_str(&m, "v");
	for (uint32_t i = 0; i < x->n; i++)
		mg_parameter(&m, a->params[x->aux + i]);
	if (x->flags & TF_VARIADIC)
		mg_str(&m, "z");
	if (m.bad && getenv("VCC_CXX_MANGLE_WHY"))
		fprintf(stderr, "not mangled: %.*s (so far: %.*s)\n", (int)v->len, v->s, (int)m.n, m.b);
	if (m.bad || m.n + 1 > cap)
		return false;
	memcpy(buf, m.b, m.n);
	buf[m.n] = 0;
	return true;
}

/* a class's data symbol: _ZTV (vtable), _ZTI (type info), _ZTS (type name) and the class's name */
VPRIV bool cxx_mangle_class_data(cxx *c, const char *kind, qty cls, char *buf, size_t cap)
{
	cxx_mangler m;

	m.c = c, m.n = 0, m.bad = false, m.nsubs = 0;
	mg_str(&m, "_Z");
	mg_str(&m, kind);
	mg_type(&m, QUNQ(cls));
	if (m.bad || m.n + 1 > cap)
		return false;
	memcpy(buf, m.b, m.n);
	buf[m.n] = 0;
	return true;
}
