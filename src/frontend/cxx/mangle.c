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
	const qty *ph;		/* a function template instance's signature: its parameters' placeholders, mangled T_, T0_, ... */
	uint32_t nph;
	const qty *tph;		/* ... the same for a value parameter's declared type (Tn <type>), while the template arguments are mangled */
	uint32_t ntph;
	const cxx_scope *tps;	/* ... and the template's parameters (their names) */
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
static bool mg_nested(cxx_mangler *m, cxx_scope *s);

/* <template-args> of a class template instance */
/* <abi-tag>: B <source-name>, after the name it tags */
static void mg_abi_tag(cxx_mangler *m, uint32_t tag)
{
	if (tag) {
		mg_str(m, "B");
		mg_source_name(m, tag);
	}
}

/* a value parameter's type, when it is a plain integer type spelled by its tokens: its code (NULL: not known) */
static const char *mg_value_type(cxx_mangler *m, const cxx_scope *ps, uint32_t i)
{
	const cxx *c = m->c;
	uint32_t p;
	char w[4][16];
	int n = 0;

	if (!ps || i >= ps->ntparams || ps->tparams[i].kind != 2 || ps->tparams[i].pack || !(p = ps->tparams[i].type_b))
		return NULL;
	for (; p < c->ntok && n < 4; p++) {
		const cxx_token *t = &c->tok[p];

		if (t->kind == LEX_TOKEN_COLON_COLON)
			continue;
		if (t->kind != LEX_TOKEN_IDENTIFIER || t->len >= 16)
			break;
		if (t->len == 3 && !memcmp(t->s, "std", 3) && p + 1 < c->ntok && c->tok[p + 1].kind == LEX_TOKEN_COLON_COLON)
			continue;
		if (t->len == 5 && !memcmp(t->s, "const", 5))
			continue;
		if (n && strcmp(w[n - 1], "unsigned") && strcmp(w[n - 1], "signed") && strcmp(w[n - 1], "long") && strcmp(w[n - 1], "short"))
			break;
		if (n && !(t->len == 3 && !memcmp(t->s, "int", 3)) && !(t->len == 4 && (!memcmp(t->s, "long", 4) || !memcmp(t->s, "char", 4))) &&
		    !(t->len == 5 && !memcmp(t->s, "short", 5)))
			break;	/* (the parameter's name after unsigned, long, ...) */
		memcpy(w[n], t->s, t->len), w[n][t->len] = 0, n++;
		if (strcmp(w[n - 1], "unsigned") && strcmp(w[n - 1], "signed") && strcmp(w[n - 1], "long") && strcmp(w[n - 1], "short"))
			break;	/* (the last word of a type) */
	}
#define W(k, s) (n > (k) && !strcmp(w[k], s))
	if (n == 1 && W(0, "bool")) return "b";
	if (n == 1 && W(0, "char")) return "c";
	if (n == 1 && (W(0, "int") || W(0, "signed"))) return "i";
	if ((n == 1 && W(0, "unsigned")) || (n == 2 && W(0, "unsigned") && W(1, "int"))) return "j";
	if ((n == 1 && W(0, "long")) || (n == 2 && W(0, "long") && W(1, "int"))) return "l";
	if (n == 2 && W(0, "unsigned") && W(1, "long")) return "m";
	if (n == 2 && W(0, "long") && W(1, "long")) return "x";
	if (n == 3 && W(0, "unsigned") && W(1, "long") && W(2, "long")) return "y";
	if (n == 1 && W(0, "short")) return "s";
	if (n == 2 && W(0, "unsigned") && W(1, "short")) return "t";
	if (n == 2 && W(0, "unsigned") && W(1, "char")) return "h";
	if (n == 2 && W(0, "signed") && W(1, "char")) return "a";
	if (n == 1 && (W(0, "size_t") || W(0, "__size_t"))) return "m";	/* (x86-64 and arm64 Linux) */
	if (n == 1 && (W(0, "ptrdiff_t") || W(0, "intmax_t") || W(0, "int64_t"))) return "l";
	if (n == 1 && (W(0, "uint64_t") || W(0, "uintmax_t"))) return "m";
	if (n == 1 && W(0, "uint8_t")) return "h";
	if (n == 1 && W(0, "int32_t")) return "i";
	if (n == 1 && W(0, "uint32_t")) return "j";
#undef W
	return NULL;
}

/*
 * Tn <type>: a value parameter whose declared type is dependent (clang 23 writes it before the argument).
 * Mangled from the type's tokens, for the forms libc++ and json use: [typename] [std::](enable_if_t |
 * __enable_if_t)<E, T> and typename [std::]enable_if<E, T>::type, E made of !, &&, ||, comparisons,
 * sizeof(T), integer literals and [ns::]X<args>::value / [ns::]x_v<args> (unresolved names, sr ... E).
 * False: another form (the caller's name is then a guess of vcc's).
 */
typedef struct { cxx_mangler *m; uint32_t p, e; bool own; } mg_tok;	/* own: the function template's parameters were named (else no Tn) */

static const cxx_token *mt_t(mg_tok *k) { return k->p < k->e ? &k->m->c->tok[k->p] : NULL; }
static bool mt_is(mg_tok *k, int kind) { const cxx_token *t = mt_t(k); return t && t->kind == kind; }
static bool mt_word(mg_tok *k, const char *w)
{
	const cxx_token *t = mt_t(k);
	size_t l = strlen(w);

	return t && t->kind == LEX_TOKEN_IDENTIFIER && t->len == l && !memcmp(t->s, w, l);
}
static void mt_src(cxx_mangler *m, const cxx_token *t)
{
	char num[16];

	mg_text(m, num, (size_t)snprintf(num, sizeof num, "%u", (unsigned)t->len));
	mg_text(m, t->s, t->len);
}

/* a template parameter's name: its index (-1: not one) */
static int mt_tparam(mg_tok *k)
{
	const cxx_token *t = mt_t(k);
	const cxx_scope *ps = k->m->tps;

	if (!t || t->kind != LEX_TOKEN_IDENTIFIER || !ps)
		return -1;
	for (uint32_t i = 0; i < ps->ntparams; i++) {
		const vident *v = &vintern_ents[ps->tparams[i].name];

		if (ps->tparams[i].name > 1 && v->len == t->len && !memcmp(v->s, t->s, t->len))
			return (int)i;
	}
	return -1;
}

static bool mt_type(mg_tok *k);
static bool mt_expr(mg_tok *k);

/* a builtin type written as words */
static const char *mt_builtin(mg_tok *k)
{
	static const struct { const char *w; const char *code; } b[] = {
		{ "int", "i" }, { "bool", "b" }, { "char", "c" }, { "void", "v" }, { "long", "l" }, { "unsigned", "j" },
		{ "short", "s" }, { "double", "d" }, { "float", "f" }, { "size_t", "m" },
	};
	for (size_t i = 0; i < sizeof b / sizeof *b; i++)
		if (mt_word(k, b[i].w))
			return b[i].code;
	return NULL;
}

static bool mt_type(mg_tok *k)
{
	int i = mt_tparam(k);
	const char *bt;

	if (i < 0 && mt_t(k) && mt_t(k)->kind == LEX_TOKEN_IDENTIFIER && k->m->tps) {
		/* an enclosing class template's parameter (bound in its instance): the argument's type */
		const cxx_entity *e = NULL;

		for (const cxx_scope *sc = k->m->tps->parent; sc && !e; sc = sc->parent)
			e = cxx_lookup_in(k->m->c, (cxx_scope *)sc, mt_t(k)->id, 0);
		if (e && e->kind == CXX_ENTITY_TYPE && e->ty != CXX_TY_UNKNOWN && !e->dependent && !e->placeholder &&
		    !(k->p + 1 < k->e && (k->m->c->tok[k->p + 1].kind == LEX_TOKEN_LESS || k->m->c->tok[k->p + 1].kind == LEX_TOKEN_COLON_COLON))) {
			k->p++;
			mg_type(k->m, e->ty);
			return true;
		}
	}
	if (i >= 0) {
		k->own = true;
		if (!k->m->tph || (uint32_t)i >= k->m->ntph || !k->m->tph[i])
			return false;
		const qty *ph = k->m->ph;
		uint32_t nph = k->m->nph;

		k->p++;
		k->m->ph = k->m->tph, k->m->nph = k->m->ntph;
		mg_type(k->m, k->m->tph[i]);
		k->m->ph = ph, k->m->nph = nph;
		return true;
	}
	if (mt_word(k, "std") && k->p + 2 < k->e && k->m->c->tok[k->p + 1].kind == LEX_TOKEN_COLON_COLON &&
	    k->m->c->tok[k->p + 2].len == 6 && !memcmp(k->m->c->tok[k->p + 2].s, "size_t", 6))
		k->p += 2;
	if ((bt = mt_builtin(k)) && !(k->p + 1 < k->e && k->m->c->tok[k->p + 1].kind == LEX_TOKEN_IDENTIFIER)) {
		k->p++;
		mg_str(k->m, bt);
		return true;
	}
	return false;
}

/* <template-args> of an unresolved name: < type, ... > */
static bool mt_targs(mg_tok *k)
{
	if (!mt_is(k, LEX_TOKEN_LESS))
		return false;
	k->p++;
	mg_str(k->m, "I");
	while (!mt_is(k, LEX_TOKEN_GREATER)) {
		if (!mt_type(k))
			return false;
		if (mt_is(k, LEX_TOKEN_COMMA))
			k->p++;
		else if (!mt_is(k, LEX_TOKEN_GREATER))
			return false;
	}
	k->p++;
	mg_str(k->m, "E");
	return true;
}

static bool mt_primary(mg_tok *k)
{
	const cxx_token *t = mt_t(k);

	if (!t)
		return false;
	if (t->kind == LEX_TOKEN_LEFT_PAREN) {
		k->p++;
		if (!mt_expr(k) || !mt_is(k, LEX_TOKEN_RIGHT_PAREN))
			return false;
		k->p++;
		return true;
	}
	if (t->kind == LEX_TOKEN_NUMBER) {
		char num[32];
		size_t l = 0;

		for (uint32_t j = 0; j < t->len && l < 30; j++)
			if (t->s[j] >= '0' && t->s[j] <= '9')
				num[l++] = t->s[j];
			else if (t->s[j] != 'u' && t->s[j] != 'U' && t->s[j] != 'l' && t->s[j] != 'L')
				return false;
		if (!l || (l > 1 && num[0] == '0'))
			return false;
		k->p++;
		mg_str(k->m, "Li");
		mg_text(k->m, num, l);
		mg_str(k->m, "E");
		return true;
	}
	if (mt_word(k, "true") || mt_word(k, "false")) {
		mg_str(k->m, mt_word(k, "true") ? "Lb1E" : "Lb0E");
		k->p++;
		return true;
	}
	if (mt_word(k, "sizeof")) {
		k->p++;
		if (!mt_is(k, LEX_TOKEN_LEFT_PAREN))
			return false;
		k->p++;
		mg_str(k->m, "st");
		if (!mt_type(k) || !mt_is(k, LEX_TOKEN_RIGHT_PAREN))
			return false;
		k->p++;
		return true;
	}
	if (t->kind != LEX_TOKEN_IDENTIFIER || mt_tparam(k) >= 0)
		return false;
	/* [ns::]... X<args>::value (sr <levels> E <name>), or [ns::] x<args> (sr <levels> E <name><args>) */
	{
		uint32_t start = k->p, levels = 0;
		bool named = false;

		mg_str(k->m, "sr");
		while (mt_is(k, LEX_TOKEN_IDENTIFIER)) {
			const cxx_token *id = mt_t(k);
			uint32_t at = k->p;

			k->p++;
			if (mt_is(k, LEX_TOKEN_COLON_COLON)) {	/* a namespace level */
				k->p++;
				mt_src(k->m, id);
				levels++;
				continue;
			}
			if (mt_is(k, LEX_TOKEN_LESS)) {
				size_t mark = k->m->n;

				mt_src(k->m, id);
				if (!mt_targs(k))
					return false;
				if (mt_is(k, LEX_TOKEN_COLON_COLON) && k->p + 1 < k->e && k->m->c->tok[k->p + 1].kind == LEX_TOKEN_IDENTIFIER) {
					/* X<args>::name: X<args> a level, then E and the name */
					k->p++;
					mg_str(k->m, "E");
					mt_src(k->m, mt_t(k));
					k->p++;
					named = true;
					break;
				}
				/* a variable template x<args>: E between the levels and it */
				{
					char tail[512];
					size_t tl = k->m->n - mark;

					if (!levels) {
						/* (unqualified: the name and its arguments, no sr) */
						if (tl >= sizeof tail || k->m->n < mark || mark < 2)
							return false;
						memcpy(tail, k->m->b + mark, tl);
						k->m->n = mark - 2;	/* (drop the "sr") */
						mg_text(k->m, tail, tl);
						named = true;
						break;
					}
					if (tl >= sizeof tail)
						return false;
					memcpy(tail, k->m->b + mark, tl);
					k->m->n = mark;
					mg_str(k->m, "E");
					mg_text(k->m, tail, tl);
				}
				named = true;
				break;
			}
			(void)at;
			return false;
		}
		(void)start;
		return named;
	}
}

static bool mt_unary(mg_tok *k)
{
	if (mt_is(k, LEX_TOKEN_EXCLAMATION)) {
		k->p++;
		mg_str(k->m, "nt");
		return mt_unary(k);
	}
	return mt_primary(k);
}

/* binary operators, by precedence: their codes written before both operands */
static bool mt_binary(mg_tok *k, int level)
{
	static const struct { int lvl, kind; const char *code; } ops[] = {
		{ 0, LEX_TOKEN_PIPE_PIPE, "oo" }, { 1, LEX_TOKEN_AMPERSAND_AMPERSAND, "aa" },
		{ 2, LEX_TOKEN_EQUAL_EQUAL, "eq" }, { 2, LEX_TOKEN_EXCLAMATION_EQUAL, "ne" },
		{ 3, LEX_TOKEN_LESS, "lt" }, { 3, LEX_TOKEN_GREATER, "gt" }, { 3, LEX_TOKEN_LESS_EQUAL, "le" }, { 3, LEX_TOKEN_GREATER_EQUAL, "ge" },
	};
	char lhs[1024];
	size_t mark, ll;

	if (level > 3)
		return mt_unary(k);
	mark = k->m->n;
	if (!mt_binary(k, level + 1))
		return false;
	for (;;) {
		const char *code = NULL;
		const cxx_token *t = mt_t(k);

		for (size_t i = 0; t && i < sizeof ops / sizeof *ops; i++)
			if (ops[i].lvl == level && ops[i].kind == t->kind)
				code = ops[i].code;
		if (!code)
			return true;
		/* (the operator's code goes before the left operand already written) */
		ll = k->m->n - mark;
		if (ll >= sizeof lhs)
			return false;
		memcpy(lhs, k->m->b + mark, ll);
		k->m->n = mark;
		mg_str(k->m, code);
		mg_text(k->m, lhs, ll);
		k->p++;
		if (!mt_binary(k, level + 1))
			return false;
	}
}

static bool mt_expr(mg_tok *k) { return mt_binary(k, 0); }

/* the type's tokens from p: enable_if's forms; *lit: the literal's type code (the second argument) */
static bool mg_tn_type(cxx_mangler *m, uint32_t p, char *lit, size_t litcap, bool *own)
{
	cxx *c = m->c;
	mg_tok k = { m, p, c->ntok, false };
	bool alias, typename_ = false;
	cxx_entity *ns, *ei = NULL;
	size_t save_n = m->n;
	uint32_t save_subs = m->nsubs;

	bool in_std = false;
	uint32_t member = 0;

	if (mt_word(&k, "typename"))
		k.p++, typename_ = true;
	if (mt_word(&k, "std") && k.p + 1 < k.e && c->tok[k.p + 1].kind == LEX_TOKEN_COLON_COLON)
		k.p += 2, in_std = true;
	alias = mt_word(&k, "enable_if_t") || mt_word(&k, "__enable_if_t");
	if (!alias && !typename_)
		return false;
	if (!mt_is(&k, LEX_TOKEN_IDENTIFIER))
		return false;
	{
		/* the class template: std's enable_if for the aliases (in libc++'s inline namespace), else the name
		   looked up where the template is (typename X<E, T>::type) */
		const cxx_token *t = mt_t(&k);
		uint32_t name = alias ? vintern_cstr("enable_if") : t->id;

		if (alias || in_std) {
			ns = cxx_lookup_local(c->global, vintern_cstr("std"));
			while (ns && ns->kind != CXX_ENTITY_NAMESPACE)
				ns = ns->next;
			if (ns && ns->members)
				ei = cxx_lookup_in(c, ns->members, name, 0);
		} else
			for (cxx_scope *sc = (cxx_scope *)m->tps; sc && !ei; sc = sc->parent)
				ei = cxx_lookup_in(c, sc, name, 0);
	}
	k.p++;
	while (ei && ei->kind != CXX_ENTITY_TEMPLATE)
		ei = ei->next;
	if (!ei || !cxx_template_home(ei))
		return false;
	mg_str(m, "N");
	mg_prefix(m, cxx_template_home(ei));
	if (!mg_substitution(m, CXX_SUB_TEMPLATE, ei, 0)) {
		mg_source_name(m, ei->name);
		mg_remember(m, CXX_SUB_TEMPLATE, ei, 0);
	}
	if (!mt_is(&k, LEX_TOKEN_LESS))
		goto bad;
	k.p++;
	mg_str(m, "IX");
	if (!mt_expr(&k) || !mt_is(&k, LEX_TOKEN_COMMA))
		goto bad;
	k.p++;
	mg_str(m, "E");
	{
		size_t at = m->n;

		if (!mt_type(&k) || m->n - at >= litcap || m->n - at != 1)
			goto bad;	/* (a builtin type: the argument's literal is of it) */
		memcpy(lit, m->b + at, m->n - at), lit[m->n - at] = 0;
	}
	if (!mt_is(&k, LEX_TOKEN_GREATER))
		goto bad;
	k.p++;
	mg_str(m, "E");
	mg_remember(m, CXX_SUB_TYPE, ei, (qty)0x7fff0001);	/* (enable_if<...>: a candidate of its own) */
	if (!alias) {
		if (!mt_is(&k, LEX_TOKEN_COLON_COLON))
			goto bad;
		k.p++;
		if (!mt_word(&k, "type"))
			goto bad;	/* (X<E, T>::type: of T, the argument's literal) */
		k.p++;
	}
	(void)member;
	mg_str(m, "4typeE");
	mg_remember(m, CXX_SUB_TYPE, ei, (qty)0x7fff0002);
	if (!m->bad) {
		*own = k.own;
		if (!k.own)	/* (not dependent on the function template's own parameters: no Tn, nothing remembered) */
			m->n = save_n, m->nsubs = save_subs;
		return true;
	}
bad:
	m->n = save_n, m->nsubs = save_subs, m->bad = false;
	return false;
}

/* one template argument; i: its parameter's index in ps */
/* a value parameter declared with another parameter's type (template <class T, T v>): that parameter's index (-1: not) */
static int mg_value_type_param(cxx_mangler *m, const cxx_scope *ps, uint32_t i)
{
	const cxx *c = m->c;
	uint32_t p;

	if (!ps || i >= ps->ntparams || ps->tparams[i].kind != 2 || !(p = ps->tparams[i].type_b) || p + 1 >= c->ntok)
		return -1;
	if (c->tok[p].kind != LEX_TOKEN_IDENTIFIER)
		return -1;
	if (c->tok[p + 1].kind != LEX_TOKEN_IDENTIFIER && c->tok[p + 1].kind != LEX_TOKEN_COMMA && c->tok[p + 1].kind != LEX_TOKEN_GREATER &&
	    c->tok[p + 1].kind != LEX_TOKEN_EQUAL && c->tok[p + 1].kind != LEX_TOKEN_GREATER_GREATER)
		return -1;	/* (only the parameter's name, or nothing, after the type) */
	for (uint32_t k = 0; k < i; k++)
		if (ps->tparams[k].kind == 1 && !ps->tparams[k].pack && ps->tparams[k].name == c->tok[p].id)
			return (int)k;
	return -1;
}

static void mg_template_arg(cxx_mangler *m, const cxx_targ *x, uint32_t i, const cxx_scope *ps, const cxx_targ *all)
{
	if ((*x).kind == 1 && (*x).t != CXX_TY_UNKNOWN)
		mg_type(m, (*x).t);
	else if ((*x).kind == 2) {
		/* a value: L <type> <value> E; the type is the parameter's, known here for enumerations only:
		   others as int (not clang's name for a size_t or bool argument yet) */
		char num[32];

		const char *vt = (*x).t ? NULL : mg_value_type(m, ps, i);
		char lit[8];


		if (!(*x).t && !vt && ps && m->tph && i < ps->ntparams && ps->tparams[i].kind == 2 && !ps->tparams[i].pack &&
		    ps->tparams[i].type_b) {
			size_t at = m->n;

			bool own = false;

			mg_str(m, "Tn");
			if (mg_tn_type(m, ps->tparams[i].type_b, lit, sizeof lit, &own)) {
				vt = lit;
				if (!own)
					m->n = at;	/* (the "Tn" too) */
			} else
				m->n = at;
		}
		int tk = !(*x).t && !vt && all ? mg_value_type_param(m, ps, i) : -1;
		qty lt = tk >= 0 && all[tk].kind == 1 && all[tk].t != CXX_TY_UNKNOWN ? all[tk].t : 0;

		if (lt && !(AST_TYPE_KIND(m->c->ast, lt) >= TY_BOOL && AST_TYPE_KIND(m->c->ast, lt) <= TY_UINT128))
			lt = 0;	/* (an integer or character type: written as such) */
		mg_str(m, "L");
		if ((*x).t)
			mg_type(m, (*x).t);
		else if (lt)
			mg_type(m, QUNQ(lt));
		else
			mg_str(m, vt ? vt : "i");
		if (!(*x).t && !vt && !lt)
			m->c->mangle_guess = 1;	/* (the parameter's type not read: clang may write Tn <its type> here) */
		if ((vt && vt[0] == 'b') || (lt && AST_TYPE_KIND(m->c->ast, lt) == TY_BOOL))
			mg_str(m, (*x).v ? "1" : "0");
		else
		mg_text(m, num, (size_t)snprintf(num, sizeof num, (*x).v < 0 ? "n%llu" : "%llu",
						 (unsigned long long)((*x).v < 0 ? 0 - (uint64_t)(*x).v : (uint64_t)(*x).v)));
		mg_str(m, "E");
	} else if ((*x).kind == 4 && (*x).v) {
		/* a template as the argument (std::map for basic_json's ObjectType): its name, a substitution
		   candidate (S0_ the next time) */
		const cxx_entity *te = (const cxx_entity *)(intptr_t)(*x).v;
		cxx_scope *home;

		if (te->bound_template)
			te = te->bound_template;
		home = cxx_template_home(te);
		if (!home) {
			m->bad = true;
			return;
		}
		if (mg_substitution(m, CXX_SUB_TEMPLATE, te, 0))
			return;
		if (mg_nested(m, home)) {
			mg_str(m, "N");
			mg_prefix(m, home);
			mg_source_name(m, te->name);
			mg_str(m, "E");
		} else {
			if (mg_scope_is_std(m, home))
				mg_str(m, "St");
			mg_source_name(m, te->name);
		}
		mg_remember(m, CXX_SUB_TEMPLATE, te, 0);
	} else
		m->bad = true;	/* (an argument not known) */
}

static void mg_template_args(cxx_mangler *m, const cxx_targ *args, uint32_t nargs, uint32_t pack_at, const cxx_scope *ps)
{
	uint32_t pidx = UINT32_MAX, npost = 0;

	/* (parameters declared after a pack (pack_at: its elements last) are kept before its elements: in the
	   declaration's order here) */
	for (uint32_t j = 0; ps && j < ps->ntparams; j++)
		if (ps->tparams[j].pack) {
			pidx = j;
			break;
		}
	if (ps && pidx != UINT32_MAX && pack_at <= nargs && pidx < pack_at && pack_at == ps->ntparams - 1)
		npost = pack_at - pidx;
	mg_str(m, "I");
	if (npost) {
		for (uint32_t j = 0; j < pidx; j++)
			mg_template_arg(m, &args[j], j, ps, args);
		mg_str(m, "J");
		for (uint32_t j = pack_at; j < nargs; j++)
			mg_template_arg(m, &args[j], pidx, ps, NULL);
		mg_str(m, "E");
		for (uint32_t j = 0; j < npost; j++)
			mg_template_arg(m, &args[pidx + j], pidx + 1 + j, ps, NULL);
		mg_str(m, "E");
		return;
	}
	for (uint32_t i = 0; i < nargs; i++) {
		if (i == pack_at)
			mg_str(m, "J");
		mg_template_arg(m, &args[i], i, ps, pack_at > nargs || i < pack_at ? args : NULL);
	}
	if (pack_at == nargs)
		mg_str(m, "J");	/* (an empty pack: J E) */
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
		mg_template_args(m, args, nargs, pack_at, cxx_instance_tparams(m->c, t));
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
	if (s && s->is_lambda && s->owner) {
		mg_source_name(m, s->owner->name);	/* a closure type: by its own name, wherever the lambda is */
		return;
	}
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
	for (uint32_t i = 0; m->ph && i < m->nph && !(q & (Q_CONST | Q_VOLATILE | Q_RESTRICT)); i++)
		if (m->ph[i] && QUNQ(t) == m->ph[i]) {
			/* a template parameter: T_ for the first, T<n-1>_ after (a substitution candidate) */
			char num[16];

			if (mg_substitution(m, CXX_SUB_TYPE, NULL, t))
				return;
			if (i)
				mg_text(m, num, (size_t)snprintf(num, sizeof num, "T%u_", i - 1));
			else
				mg_str(m, "T_");
			mg_remember(m, CXX_SUB_TYPE, NULL, t);
			return;
		}
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
	case TY_UCHAR: mg_str(m, x->ext ? "Du" : "h"); return;	/* (ext: char8_t; below char16_t, wchar_t, char32_t) */
	case TY_SHORT: mg_str(m, "s"); return;
	case TY_USHORT: mg_str(m, x->ext ? "Ds" : "t"); return;
	case TY_INT: mg_str(m, x->ext ? "w" : "i"); return;
	case TY_UINT: mg_str(m, x->ext ? "Di" : "j"); return;
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
	m.c = c, m.n = 0, m.bad = false, m.nsubs = 0, m.ph = NULL, m.nph = 0, m.tph = NULL, m.ntph = 0, m.tps = NULL;
	op = mg_operator(v, x->n, member);
	conv = v->len > 10 && !memcmp(v->s, "operator #", 10);
	if (!op && v->len >= 12 && !memcmp(v->s, "operator ", 9) && !conv) {
		/* operator new, new[], delete, delete[] */
		op = !strcmp(v->s, "operator new") ? "nw" : !strcmp(v->s, "operator new[]") ? "na" :
		     !strcmp(v->s, "operator delete") ? "dl" : !strcmp(v->s, "operator delete[]") ? "da" : NULL;
	}
	if (v->len > 8 && !memcmp(v->s, "operator", 8) && !op && !conv)
		return false;	/* (literal operators: later) */
	/* a constructor or destructor: C1/D1 the complete object's, C2/D2 the base object's (variant) */
	if (v->len && v->s[0] == ' ') {
		if (!member || (variant != 1 && variant != 2 && !(variant == 3 && v->s[1] != 'c')))
			return false;
		op = v->s[1] == 'c' ? (variant == 1 ? "C1" : "C2") : (variant == 1 ? "D1" : variant == 3 ? "D0" : "D2");
	}
	if (f->is_instance) {
		/* (a value parameter's declared type in the template arguments refers to the template's parameters: T_, ...) */
		const qty *tph;
		uint32_t ntph, tpp;

		if (cxx_instance_declared_sig(c, f, &tph, &ntph, &tpp) != CXX_TY_UNKNOWN || tph)
			m.tph = tph, m.ntph = ntph, m.tps = f->inst_params;
	}
	mg_str(&m, "_Z");
	if (mg_nested(&m, home)) {
		mg_str(&m, "N");
		if (member && is_const)
			mg_str(&m, "K");
		mg_prefix(&m, home);
		if (f->fn_internal && !member && !op && !cxx_scope_in_unnamed_namespace(c, home))	/* (none in an unnamed namespace) */
			mg_str(&m, "L");
		if (op)
			mg_str(&m, op);
		else if (conv) {
			mg_str(&m, "cv");
			mg_type(&m, x->base);
		} else
			mg_source_name(&m, f->name);
		mg_abi_tag(&m, f->abi_tag);
		if (f->is_instance && !(v->len && v->s[0] == ' '))
			mg_remember(&m, CXX_SUB_TEMPLATE, f, 0);	/* (the template's name: a candidate before its arguments) */
		if (f->is_instance)
			mg_template_args(&m, f->targs, f->ntargs, f->inst_pack_at, f->inst_params);
		mg_str(&m, "E");
	} else {
		if (mg_scope_is_std(&m, home))
			mg_str(&m, "St");
		if (f->fn_internal && !member && !op && !cxx_scope_in_unnamed_namespace(c, home))	/* (none in an unnamed namespace) */
			mg_str(&m, "L");
		if (op)
			mg_str(&m, op);
		else
			mg_source_name(&m, f->name);
		mg_abi_tag(&m, f->abi_tag);
		if (f->is_instance)
			mg_remember(&m, CXX_SUB_TEMPLATE, f, 0);	/* (the template's name: a candidate before its arguments) */
		if (f->is_instance)
			mg_template_args(&m, f->targs, f->ntargs, f->inst_pack_at, f->inst_params);
	}
	/* A function template's instance: its return type and its parameters as the template declares them
	   (T_ for a parameter), when that signature is known; else the instance's own types (unique and stable
	   among vcc's objects, not clang's name). */
	uint32_t pack_param = 0;

	if (f->is_instance) {
		const qty *ph;
		uint32_t nph, pp = 0;
		qty ds = cxx_instance_declared_sig(c, f, &ph, &nph, &pp);

		if (cxx_instance_uses_decltype(c, f))
			c->mangle_guess = 1;	/* (decltype in its declaration: clang writes the expression, DT ... E) */
		/* (with a function parameter pack: the declared parameters, the pack one of them, written Dp <its type>) */
		uint32_t want = pp && f->inst_pack_at != UINT32_MAX ? x->n - (f->ntargs - f->inst_pack_at) + 1 : x->n;

		bool known = ds != CXX_TY_UNKNOWN && AST_TYPE_KIND(a, ds) == TY_FUNC;

		for (uint32_t i = 0; known && i < AST_TYPE(a, ds)->n; i++)
			known = a->params[AST_TYPE(a, ds)->aux + i] != CXX_TY_UNKNOWN;
		if (known && AST_TYPE(a, ds)->n == want && (!pp || pp <= want) &&
		    (AST_TYPE(a, ds)->flags & TF_VARIADIC) == (x->flags & TF_VARIADIC) && AST_TYPE(a, ds)->base != CXX_TY_UNKNOWN)
			x = AST_TYPE(a, ds), m.ph = ph, m.nph = nph, pack_param = f->inst_pack_at != UINT32_MAX ? pp : 0;
		else
			c->mangle_guess = 1;	/* (the instance's own types: a name of vcc's, not clang's) */
	}
	if (f->is_instance && !(v->len && v->s[0] == ' '))
		mg_type(&m, x->base);
	if (!x->n && !(x->flags & TF_VARIADIC))
		mg_str(&m, "v");
	for (uint32_t i = 0; i < x->n; i++) {
		if (pack_param && i + 1 == pack_param)
			mg_str(&m, "Dp");	/* (a pack expansion; its pattern is a substitution candidate, then the expansion) */
		mg_parameter(&m, a->params[x->aux + i]);
		if (pack_param && i + 1 == pack_param)
			mg_remember(&m, CXX_SUB_TEMPLATE, &m.ph[0] + 0x1000 + i, 0);	/* (Dp T itself: a candidate) */
	}
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

/* a variable of a namespace or a class's static data member: _ZN <prefix> <name> E (_Z <name> in a namespace
   at the global scope's level, St for std::) */
VPRIV bool cxx_mangle_variable(cxx *c, const cxx_entity *v, cxx_scope *home, bool internal, char *buf, size_t cap)
{
	cxx_mangler m;

	if (!home)
		return false;
	m.c = c, m.n = 0, m.bad = false, m.nsubs = 0, m.ph = NULL, m.nph = 0, m.tph = NULL, m.ntph = 0, m.tps = NULL;
	mg_str(&m, "_Z");
	if (mg_nested(&m, home)) {
		mg_str(&m, "N");
		mg_prefix(&m, home);
		if (internal && !cxx_scope_in_unnamed_namespace(c, home))
			mg_str(&m, "L");
		mg_source_name(&m, v->name);
		mg_abi_tag(&m, v->abi_tag);
		mg_str(&m, "E");
	} else {
		if (mg_scope_is_std(&m, home))
			mg_str(&m, "St");
		mg_source_name(&m, v->name);
		mg_abi_tag(&m, v->abi_tag);
	}
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

	m.c = c, m.n = 0, m.bad = false, m.nsubs = 0, m.ph = NULL, m.nph = 0, m.tph = NULL, m.ntph = 0, m.tps = NULL;
	mg_str(&m, "_Z");
	mg_str(&m, kind);
	mg_type(&m, QUNQ(cls));
	if (m.bad || m.n + 1 > cap)
		return false;
	memcpy(buf, m.b, m.n);
	buf[m.n] = 0;
	return true;
}

/* a type's mangled spelling alone (what follows _ZTI and _ZTS; the type name string itself) */
VPRIV bool cxx_mangle_type(cxx *c, qty t, char *buf, size_t cap)
{
	cxx_mangler m;

	m.c = c, m.n = 0, m.bad = false, m.nsubs = 0, m.ph = NULL, m.nph = 0, m.tph = NULL, m.ntph = 0, m.tps = NULL;
	mg_type(&m, t);
	if (m.bad || m.n + 1 > cap)
		return false;
	memcpy(buf, m.b, m.n);
	buf[m.n] = 0;
	return true;
}
