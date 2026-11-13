/* vcc front end: #pragma weak, pending names and aliases */
#include "ccimpl.h"

/* ---------------- #pragma weak ---------------- */

/*
 * clang's rules: "#pragma weak n" makes the external function or variable n
 * weak, now or at its first declaration; "#pragma weak a = t" declares a, a
 * weak alias of t, now or when t is first declared.  Pending ones left at the
 * end are reported.  Like clang's code generator, a function or variable
 * already defined or named before the pragma keeps its binding.
 */
static void weak_push(cc *c, uint32_t key, uint32_t alias, uint32_t loc, uint32_t s)
{
	if (c->npweak == c->cpweak) {
		c->cpweak = c->cpweak ? 2 * c->cpweak : 8;
		c->pweak = realloc(c->pweak, c->cpweak * sizeof(*c->pweak));
	}
	c->pweak[c->npweak++] = (struct pweak){ key, alias, loc, s };
}

static uint32_t id_string(cc *c, uint32_t id)
{
	int l;
	const char *s = idname(id, &l);
	char *k = halloc(c, (size_t)l + 1);

	memcpy(k, s, (size_t)l);
	k[l] = 0;
	c->strs[c->nstr++] = (cstr){ k, (uint32_t)l + 1, 1, LEX_TOKEN_STRING };
	return c->nstr;		/* (attrs hold index + 1) */
}

/* alias: a weak alias of t (clang: a clone of t's declaration) */
static void weak_alias(cc *c, uint32_t t, uint32_t alias, uint32_t loc)
{
	attrs at = { 0 };
	uint32_t a = file_scope_sym(c, alias);

	at.weak = 1;
	at.alias = id_string(c, c->syms[t].name);
	if (!a) {
		a = sym_new(c, c->syms[t].kind, alias, c->syms[t].ty, loc);
		c->syms[a].flags |= SF_GLOBAL;
		c->syms[a].depth = 0;
		if (c->depth == 0)
			sym_bind(c, a);
		c->toplevel[c->ntoplevel++] = a;
	}
	apply_sym_attrs(c, &c->syms[a], &at);
	weak_push(c, t, alias, loc, a);
}

static bool weak_idc(char ch)
{
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '$';
}

static const char *weak_skip(const char *s, const char *e)
{
	while (s < e && (*s == ' ' || *s == '\t'))
		s++;
	return s;
}

/* the identifier at *s, interned, or 0 */
static uint32_t weak_ident(const char **s, const char *e)
{
	const char *b = *s, *q = b;
	char buf[256 + 16] = { 0 };

	if (q < e && !(*q >= '0' && *q <= '9'))
		while (q < e && weak_idc(*q))
			q++;
	if (q == b || q - b > 256)
		return 0;
	memcpy(buf, b, (size_t)(q - b));
	*s = q;
	return vintern(buf, (size_t)(q - b), vintern_hash(buf, (size_t)(q - b)));
}

static bool fn_or_var(const sym *y)
{
	return y->kind == S_FUNC || y->kind == S_VAR;
}

/* s .. e: the text after "weak"; base: the directive's text, at loc + 1 */
VPRIV void pragma_weak(cc *c, const char *s, const char *e, const char *base, uint32_t loc)
{
	uint32_t name, target = 0, nl, k;

#define WLOC(p) (loc + 1 + (uint32_t)((p) - base))
	s = weak_skip(s, e);
	nl = WLOC(s);
	if (!(name = weak_ident(&s, e))) {
		cc_warnx(c, "ignored-pragmas", true, NULL, WLOC(s), NULL, 0, NULL, 0, "expected identifier in '#pragma weak' - ignored");
		return;
	}
	s = weak_skip(s, e);
	if (s < e && *s == '=') {
		s = weak_skip(s + 1, e);
		if (!(target = weak_ident(&s, e))) {
			cc_warnx(c, "ignored-pragmas", true, NULL, WLOC(s), NULL, 0, NULL, 0, "expected identifier in '#pragma weak' - ignored");
			return;
		}
		s = weak_skip(s, e);
	}
	if (s < e) {
		cc_warnx(c, "ignored-pragmas", true, NULL, WLOC(s), NULL, 0, NULL, 0, "extra tokens at end of '#pragma weak' - ignored");
		return;
	}
#undef WLOC
	driver_declaration_cache_kill(c);	/* (the pending list is not in the declaration cache's snapshots) */
	k = file_scope_sym(c, target ? target : name);
	if (k && fn_or_var(&c->syms[k])) {
		sym *y = &c->syms[k];

		if (target)
			weak_alias(c, k, name, nl);
		else if (y->sc != SC_STATIC && !(y->flags & (SF_DEFINED | SF_REF)))
			y->flags |= SF_WEAK;
		return;
	}
	weak_push(c, target ? target : name, target ? name : 0, nl, 0);
}

/* s was just declared: the pragmas waiting for it */
VPRIV void weak_declared(cc *c, uint32_t s)
{
	const sym *y = &c->syms[s];

	if (!fn_or_var(y) || !(y->flags & SF_GLOBAL) || y->sc == SC_STATIC)
		return;
	for (uint32_t i = 0; i < c->npweak;) {
		struct pweak w = c->pweak[i];

		if (w.sym || w.key != y->name) {
			i++;
			continue;
		}
		memmove(&c->pweak[i], &c->pweak[i + 1], (c->npweak - i - 1) * sizeof(*c->pweak));
		c->npweak--;
		if (w.alias)
			weak_alias(c, s, w.alias, w.loc);
		else
			c->syms[s].flags |= SF_WEAK;
	}
}

/* the end of the TU: never declared, or an alias of nothing */
VPRIV void weak_finish(cc *c)
{
	int st = pp_wstate(c->p, "", true, NULL);

	for (uint32_t i = 0; i < c->npweak; i++) {
		const struct pweak *w = &c->pweak[i];

		if (!w->sym && st && !pp_in_system_header(c->p, w->loc))
			report(c, st == 2 ? "error" : "warning", w->loc, NULL, 0, NULL, 0, "weak identifier '%.*s' never declared%s",
			       IDF(w->key), st == 2 ? " [-Werror]" : "");
	}
	for (uint32_t i = 0; c->o->emit && i < c->npweak; i++) {	/* (clang: its code generator) */
		const struct pweak *w = &c->pweak[i];
		const sym *t = &c->syms[w->key];

		if (w->sym && !(t->kind == S_FUNC ? (t->flags & SF_DEFINED) && t->body : (t->flags & (SF_DEFINED | SF_TENTATIVE)))) {
			cc_error(c, w->loc, "alias must point to a defined variable or function");
			note(c, w->loc, "the function or variable specified in an alias must refer to its mangled name");
		}
	}
}
