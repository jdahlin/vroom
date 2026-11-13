/* cond.c: conditional assembly: .if, .ifdef, .ifc, .elseif, .else, .endif and their relatives. */
#include "asimpl.h"

/* ---------------------------------------------------------------- conditionals */

VPRIV int cond_ignore(As *a) { return a->ncs && a->cs[a->ncs - 1].ignore; }

static int push_cond(As *a, int met)
{
	if (a->ncs >= MAXCOND) return err(a, "conditionals nested too deeply");
	int parent = cond_ignore(a);
	Cond *c = &a->cs[a->ncs++];
	c->kind = 1; c->met = (uint8_t)(!parent && met); c->ignore = (uint8_t)(parent || !met);
	return 0;
}

VPRIV Sv trim(const char *p, const char *e)
{
	p = ws(p, e);
	while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) e--;
	return (Sv){ p, (uint32_t)(e - p) };
}

/* .ifc/.ifnc operand: quoted string or text up to ',' */
static Sv ifc_str(const char **pp, const char *e)
{
	const char *p = ws(*pp, e);
	if (p < e && *p == '\'') {	/* GNU allows quotes; MC: string token */
	}
	if (p < e && *p == '"') {
		const char *s = ++p;
		while (p < e && *p != '"') p++;
		Sv r = { s, (uint32_t)(p - s) };
		if (p < e) p++;
		*pp = p;
		return r;
	}
	const char *s = p;
	while (p < e && *p != ',') p++;
	*pp = p;
	return trim(s, p);
}

/* conditional directives; returns 1 if w was one */
VPRIV int cond_dir(As *a, Sv w, const char *p, const char *e, int *rc)
{
	if (w.n < 3 || w.s[0] != '.' || (w.s[1] != 'e' && w.s[1] != 'i')) return 0;
	*rc = 0;
	if (sveq(w, ".endif")) {
		if (!a->ncs) { *rc = err(a, "Encountered a .endif that doesn't follow an .if or .else"); return 1; }
		a->ncs--;
		return 1;
	}
	if (sveq(w, ".else")) {
		if (!a->ncs || a->cs[a->ncs - 1].kind == 3) { *rc = err(a, "Encountered a .else that doesn't follow an .if or an .elseif"); return 1; }
		Cond *c = &a->cs[a->ncs - 1];
		int parent = a->ncs > 1 && a->cs[a->ncs - 2].ignore;
		c->kind = 3;
		c->ignore = (uint8_t)(parent || c->met);
		return 1;
	}
	if (sveq(w, ".elseif")) {
		if (!a->ncs || a->cs[a->ncs - 1].kind == 3) { *rc = err(a, "Encountered a .elseif that doesn't follow an .if or .elseif"); return 1; }
		Cond *c = &a->cs[a->ncs - 1];
		int parent = a->ncs > 1 && a->cs[a->ncs - 2].ignore;
		c->kind = 2;
		if (parent || c->met) { c->ignore = 1; return 1; }
		int64_t v;
		if (abs_expr(a, &p, e, &v)) { *rc = -1; return 1; }
		c->met = v != 0; c->ignore = !c->met;
		return 1;
	}
	if (memcmp(w.s, ".if", 3)) return 0;
	Sv k = { w.s + 3, w.n - 3 };
	if (cond_ignore(a)) {	/* any .if* nests */
		if (k.n == 0 || sveq(k, "ne") || sveq(k, "eq") || sveq(k, "gt") || sveq(k, "ge") || sveq(k, "lt") ||
		    sveq(k, "le") || sveq(k, "def") || sveq(k, "ndef") || sveq(k, "notdef") || sveq(k, "b") ||
		    sveq(k, "nb") || sveq(k, "c") || sveq(k, "nc") || sveq(k, "eqs") || sveq(k, "nes")) {
			*rc = push_cond(a, 0);
			return 1;
		}
		return 0;
	}
	int met;
	if (k.n == 0 || sveq(k, "ne") || sveq(k, "eq") || sveq(k, "gt") || sveq(k, "ge") || sveq(k, "lt") || sveq(k, "le")) {
		int64_t v;
		if (abs_expr(a, &p, e, &v)) { *rc = -1; return 1; }
		if (k.n == 0 || sveq(k, "ne")) met = v != 0;
		else if (sveq(k, "eq")) met = v == 0;
		else if (sveq(k, "gt")) met = v > 0;
		else if (sveq(k, "ge")) met = v >= 0;
		else if (sveq(k, "lt")) met = v < 0;
		else met = v <= 0;
	} else if (sveq(k, "def") || sveq(k, "ndef") || sveq(k, "notdef")) {
		Sv n;
		if (parse_name(a, &p, e, &n)) { *rc = -1; return 1; }
		uint32_t y = elf_sym_lookup(a->o, n.s, n.n);
		int def = y && (elf_symp(a->o, y)->flags & ESYM_DEFINED);
		met = sveq(k, "def") ? def : !def;
	} else if (sveq(k, "b") || sveq(k, "nb")) {
		Sv t = trim(p, e);
		met = sveq(k, "b") ? t.n == 0 : t.n != 0;
	} else if (sveq(k, "c") || sveq(k, "nc")) {
		Sv x = ifc_str(&p, e);
		p = ws(p, e);
		if (p >= e || *p != ',') { *rc = err(a, "expected comma after first string for '.ifc' directive"); return 1; }
		p++;
		Sv y = ifc_str(&p, e);
		int eq = x.n == y.n && !memcmp(x.s, y.s, x.n);
		met = sveq(k, "c") ? eq : !eq;
	} else if (sveq(k, "eqs") || sveq(k, "nes")) {
		Sv x = ifc_str(&p, e);
		p = ws(p, e);
		if (p < e && *p == ',') p++;
		Sv y = ifc_str(&p, e);
		int eq = x.n == y.n && !memcmp(x.s, y.s, x.n);
		met = sveq(k, "eqs") ? eq : !eq;
	} else return 0;
	*rc = push_cond(a, met);
	return 1;
}
