/* vcc front end: -Wreturn-stack-address and clang's printing of a compound literal */
#include <ctype.h>

#include "cc-internal.h"

/* clang's printing of a compound literal's initializer list from its source "(type){...}":
   tokens joined, ", " after commas, spaces around binary operators, none inside braces */
static size_t cc_return_stack_address_compound_literal_text(const char *t, size_t len, char *buf, size_t n)
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
CC_PRIVATE void cc_return_stack_address_check(cc *c, uint32_t e, bool addr)
{
	while (e) {
		const node *x = AST_NODE(e);

		switch (x->op) {
		case AST_NODE_CAST:
			if (x->aux == AST_CAST_ARRAY_DECAY) {
				e = x->a, addr = true;	/* an array's address */
				continue;
			}
			if (addr && x->aux != AST_CAST_NOP)
				return;
			e = x->a;
			continue;
		case AST_NODE_CONDITIONAL:
			if (x->b)
				cc_return_stack_address_check(c, x->b, addr);
			e = x->c;
			continue;
		case AST_NODE_ADDRESS:
			if (addr)
				return;
			e = x->a, addr = true;
			continue;
		case AST_NODE_MEMBER:
			if (!addr)
				return;
			e = x->a;	/* s.m: s's storage */
			continue;
		case AST_NODE_INDEX:
			if (!addr)
				return;
			e = x->a, addr = false;	/* x[i]: the address x is */
			continue;
		case AST_NODE_COMPOUND_LITERAL: {
			const sym *y = &c->syms[x->b];
			diag_span r = cc_node_range(c, e);
			size_t len = 0;
			const char *t;
			char buf[256];
			size_t k = 0;

			if (!addr || !x->b || y->depth == 0 || y->sc == AST_STORAGE_STATIC)
				return;
			t = pp_source_text(c->p, r.b, r.e, &len);
			k = cc_return_stack_address_compound_literal_text(t, len, buf, sizeof buf);
			cc_report_warning(c, "return-stack-address", true, NULL, r.b, &r, 1, NULL, 0,
				 "address of stack memory associated with compound literal '%.*s' returned", (int)k, buf);
			return;
		}
		case AST_NODE_VARIABLE: {
			const sym *y = &c->syms[x->a];
			diag_span r = cc_node_range(c, e);

			if (!addr || y->kind != AST_SYMBOL_VARIABLE || y->depth == 0 || y->sc == AST_STORAGE_STATIC || y->sc == AST_STORAGE_EXTERN ||
			    (y->flags & AST_SYMBOL_FLAG_GLOBAL))
				return;
			cc_report_warning(c, "return-stack-address", true, NULL, r.b, &r, 1, NULL, 0,
				 "address of stack memory associated with %s '%.*s' returned",
				 y->flags & AST_SYMBOL_FLAG_PARAMETER ? "parameter" : "local variable", CC_IDENTIFIER(y->name));
			return;
		}
		default:
			return;
		}
	}
}
