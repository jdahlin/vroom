/* cond.c: conditional assembly: .if, .ifdef, .ifc, .elseif, .else, .endif and their relatives. */
#include "asimpl.h"

/* ---------------------------------------------------------------- conditionals */

VPRIV int as_condition_is_ignored(as_context *a) { return a->condition_count && a->conditions[a->condition_count - 1].is_ignored; }

static int as_condition_push(as_context *a, int met)
{
	if (a->condition_count >= AS_CONDITIONS_MAX) return as_error_report(a, "conditionals nested too deeply");
	int parent = as_condition_is_ignored(a);
	as_condition *c = &a->conditions[a->condition_count++];
	c->kind = 1; c->is_met = (uint8_t)(!parent && met); c->is_ignored = (uint8_t)(parent || !met);
	return 0;
}

VPRIV as_text as_text_trim(const char *p, const char *e)
{
	p = as_whitespace_skip(p, e);
	while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) e--;
	return (as_text){ p, (uint32_t)(e - p) };
}

/* .ifc/.ifnc operand: quoted string or text up to ',' */
static as_text as_condition_string_parse(const char **pp, const char *e)
{
	const char *p = as_whitespace_skip(*pp, e);
	if (p < e && *p == '\'') {	/* GNU allows quotes; MC: string token */
	}
	if (p < e && *p == '"') {
		const char *s = ++p;
		while (p < e && *p != '"') p++;
		as_text r = { s, (uint32_t)(p - s) };
		if (p < e) p++;
		*pp = p;
		return r;
	}
	const char *s = p;
	while (p < e && *p != ',') p++;
	*pp = p;
	return as_text_trim(s, p);
}

/* conditional directives; returns 1 if w was one */
VPRIV int as_directive_conditional(as_context *a, as_text w, const char *p, const char *e, int *rc)
{
	if (w.n < 3 || w.s[0] != '.' || (w.s[1] != 'e' && w.s[1] != 'i')) return 0;
	*rc = 0;
	if (as_text_equals(w, ".endif")) {
		if (!a->condition_count) { *rc = as_error_report(a, "Encountered a .endif that doesn't follow an .if or .else"); return 1; }
		a->condition_count--;
		return 1;
	}
	if (as_text_equals(w, ".else")) {
		if (!a->condition_count || a->conditions[a->condition_count - 1].kind == 3) { *rc = as_error_report(a, "Encountered a .else that doesn't follow an .if or an .elseif"); return 1; }
		as_condition *c = &a->conditions[a->condition_count - 1];
		int parent = a->condition_count > 1 && a->conditions[a->condition_count - 2].is_ignored;
		c->kind = 3;
		c->is_ignored = (uint8_t)(parent || c->is_met);
		return 1;
	}
	if (as_text_equals(w, ".elseif")) {
		if (!a->condition_count || a->conditions[a->condition_count - 1].kind == 3) { *rc = as_error_report(a, "Encountered a .elseif that doesn't follow an .if or .elseif"); return 1; }
		as_condition *c = &a->conditions[a->condition_count - 1];
		int parent = a->condition_count > 1 && a->conditions[a->condition_count - 2].is_ignored;
		c->kind = 2;
		if (parent || c->is_met) { c->is_ignored = 1; return 1; }
		int64_t v;
		if (as_expression_absolute_parse(a, &p, e, &v)) { *rc = -1; return 1; }
		c->is_met = v != 0; c->is_ignored = !c->is_met;
		return 1;
	}
	if (memcmp(w.s, ".if", 3)) return 0;
	as_text k = { w.s + 3, w.n - 3 };
	if (as_condition_is_ignored(a)) {	/* any .if* nests */
		if (k.n == 0 || as_text_equals(k, "ne") || as_text_equals(k, "eq") || as_text_equals(k, "gt") || as_text_equals(k, "ge") || as_text_equals(k, "lt") ||
		    as_text_equals(k, "le") || as_text_equals(k, "def") || as_text_equals(k, "ndef") || as_text_equals(k, "notdef") || as_text_equals(k, "b") ||
		    as_text_equals(k, "nb") || as_text_equals(k, "c") || as_text_equals(k, "nc") || as_text_equals(k, "eqs") || as_text_equals(k, "nes")) {
			*rc = as_condition_push(a, 0);
			return 1;
		}
		return 0;
	}
	int met;
	if (k.n == 0 || as_text_equals(k, "ne") || as_text_equals(k, "eq") || as_text_equals(k, "gt") || as_text_equals(k, "ge") || as_text_equals(k, "lt") || as_text_equals(k, "le")) {
		int64_t v;
		if (as_expression_absolute_parse(a, &p, e, &v)) { *rc = -1; return 1; }
		if (k.n == 0 || as_text_equals(k, "ne")) met = v != 0;
		else if (as_text_equals(k, "eq")) met = v == 0;
		else if (as_text_equals(k, "gt")) met = v > 0;
		else if (as_text_equals(k, "ge")) met = v >= 0;
		else if (as_text_equals(k, "lt")) met = v < 0;
		else met = v <= 0;
	} else if (as_text_equals(k, "def") || as_text_equals(k, "ndef") || as_text_equals(k, "notdef")) {
		as_text n;
		if (as_name_parse(a, &p, e, &n)) { *rc = -1; return 1; }
		uint32_t y = elf_sym_lookup(a->o, n.s, n.n);
		int def = y && (elf_symp(a->o, y)->flags & ESYM_DEFINED);
		met = as_text_equals(k, "def") ? def : !def;
	} else if (as_text_equals(k, "b") || as_text_equals(k, "nb")) {
		as_text t = as_text_trim(p, e);
		met = as_text_equals(k, "b") ? t.n == 0 : t.n != 0;
	} else if (as_text_equals(k, "c") || as_text_equals(k, "nc")) {
		as_text x = as_condition_string_parse(&p, e);
		p = as_whitespace_skip(p, e);
		if (p >= e || *p != ',') { *rc = as_error_report(a, "expected comma after first string for '.ifc' directive"); return 1; }
		p++;
		as_text y = as_condition_string_parse(&p, e);
		int eq = x.n == y.n && !memcmp(x.s, y.s, x.n);
		met = as_text_equals(k, "c") ? eq : !eq;
	} else if (as_text_equals(k, "eqs") || as_text_equals(k, "nes")) {
		as_text x = as_condition_string_parse(&p, e);
		p = as_whitespace_skip(p, e);
		if (p < e && *p == ',') p++;
		as_text y = as_condition_string_parse(&p, e);
		int eq = x.n == y.n && !memcmp(x.s, y.s, x.n);
		met = as_text_equals(k, "eqs") ? eq : !eq;
	} else return 0;
	*rc = as_condition_push(a, met);
	return 1;
}
