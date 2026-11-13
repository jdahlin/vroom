/* vcc front end: -Wreturn-stack-address and clang's printing of a compound literal */
#include <ctype.h>

#include "ccimpl.h"

/* clang's printing of a compound literal's initializer list from its source "(type){...}":
   tokens joined, ", " after commas, spaces around binary operators, none inside braces */
static size_t complit_text(const char *t, size_t len, char *buf, size_t n)
{
	size_t i = 0, k = 0;
	int depth = 0;
	char prev = 0;	/* 'v' after an operand, 'o' after an operator or '{' '(' ',' */

	if (!t)
		return 0;
	/* skip "(type)" */
	for (; i < len; i++) {
		if (t[i] == '(')
			depth++;
		else if (t[i] == ')' && --depth == 0) {
			i++;
			break;
		}
	}
	while (i < len && t[i] != '{')
		i++;
	while (i < len && k + 4 < n) {
		char ch = t[i];

		if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
			i++;
			continue;
		}
		if (ch == '"' || ch == '\'') {
			size_t s0 = i++;

			while (i < len && t[i] != ch)
				i += t[i] == '\\' ? 2 : 1;
			i++;
			if (i > len)
				i = len;
			if (k + (i - s0) + 2 >= n)
				break;
			memcpy(buf + k, t + s0, i - s0), k += i - s0;
			prev = 'v';
			continue;
		}
		if (isalnum((unsigned char)ch) || ch == '_' || ch == '.') {
			while (i < len && k + 2 < n && (isalnum((unsigned char)t[i]) || t[i] == '_' || t[i] == '.'))
				buf[k++] = t[i++];
			prev = 'v';
			continue;
		}
		if (ch == ',') {
			buf[k++] = ',', buf[k++] = ' ', i++, prev = 'o';
			continue;
		}
		if (ch == '{' || ch == '(' || ch == '[') {
			buf[k++] = ch, i++, prev = 'o';
			continue;
		}
		if (ch == '}' || ch == ')' || ch == ']') {
			buf[k++] = ch, i++, prev = 'v';
			continue;
		}
		{
			/* an operator: up to 3 characters */
			size_t m = 1;

			while (m < 3 && i + m < len && strchr("+-*/%<>=!&|^", t[i + m]) && strchr("<>=!&|+-", t[i + m - 1]))
				m++;
			if (prev == 'v') {	/* binary */
				buf[k++] = ' ';
				memcpy(buf + k, t + i, m), k += m;
				buf[k++] = ' ';
			} else {
				memcpy(buf + k, t + i, m), k += m;
			}
			i += m, prev = 'o';
		}
	}
	return k;
}

/* clang's -Wreturn-stack-address: the address of a local or parameter returned (addr: e's address is taken) */
VPRIV void return_stack_addr(cc *c, uint32_t e, bool addr)
{
	while (e) {
		const node *x = N(e);

		switch (x->op) {
		case N_CAST:
			if (x->aux == CK_DECAY) {
				e = x->a, addr = true;	/* an array's address */
				continue;
			}
			if (addr && x->aux != CK_NOP)
				return;
			e = x->a;
			continue;
		case N_COND:
			if (x->b)
				return_stack_addr(c, x->b, addr);
			e = x->c;
			continue;
		case N_ADDR:
			if (addr)
				return;
			e = x->a, addr = true;
			continue;
		case N_MEMBER:
			if (!addr)
				return;
			e = x->a;	/* s.m: s's storage */
			continue;
		case N_INDEX:
			if (!addr)
				return;
			e = x->a, addr = false;	/* x[i]: the address x is */
			continue;
		case N_COMPLIT: {
			const sym *y = &c->syms[x->b];
			diag_span r = rng(c, e);
			size_t len = 0;
			const char *t;
			char buf[256];
			size_t k = 0;

			if (!addr || !x->b || y->depth == 0 || y->sc == SC_STATIC)
				return;
			t = pp_source_text(c->p, r.b, r.e, &len);
			k = complit_text(t, len, buf, sizeof buf);
			cc_warnx(c, "return-stack-address", true, NULL, r.b, &r, 1, NULL, 0,
				 "address of stack memory associated with compound literal '%.*s' returned", (int)k, buf);
			return;
		}
		case N_VAR: {
			const sym *y = &c->syms[x->a];
			diag_span r = rng(c, e);

			if (!addr || y->kind != S_VAR || y->depth == 0 || y->sc == SC_STATIC || y->sc == SC_EXTERN ||
			    (y->flags & SF_GLOBAL))
				return;
			cc_warnx(c, "return-stack-address", true, NULL, r.b, &r, 1, NULL, 0,
				 "address of stack memory associated with %s '%.*s' returned",
				 y->flags & SF_PARAM ? "parameter" : "local variable", IDF(y->name));
			return;
		}
		default:
			return;
		}
	}
}
