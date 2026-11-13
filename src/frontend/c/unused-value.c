/* vcc front end: -Wunused-value, -Wunused-result and -Wunused-comparison on a dropped value */
#include "cc-internal.h"

/*
 * -Wunused-result for the value e that is dropped: a call of a
 * warn_unused_result/nodiscard function; a statement expression: its last
 * expression statement (last: the whole value is dropped), else the others
 */
CC_PRIVATE void cc_unused_result_check(cc *c, uint32_t e, bool last)
{
	if (AST_NODE(e)->op == AST_NODE_STATEMENT_EXPRESSION) {
		uint32_t b = AST_NODE(e)->a, n = AST_NODE(b)->c;
		/* clang: a statement expression from a macro (drmm_mutex_init) does not warn for its
		   value dropped */
		bool mac = last && (AST_NODE(e)->flags & AST_NODE_FLAG_FROM_MACRO);

		for (uint32_t i = 0; i < n; i++) {
			uint32_t s = c->lists[AST_NODE(b)->b + i];

			if (AST_NODE(s)->op == AST_NODE_EXPRESSION && AST_NODE(s)->a && (i + 1 < n ? !last : last && !mac))
				cc_unused_result_check(c, AST_NODE(s)->a, true);
		}
		return;
	}
	if (AST_NODE(e)->op == AST_NODE_CALL) {
		uint32_t f = AST_NODE(e)->a;

		while (AST_NODE(f)->op == AST_NODE_CAST && AST_NODE(f)->a)
			f = AST_NODE(f)->a;
		if (AST_NODE(f)->op == AST_NODE_VARIABLE && (c->syms[AST_NODE(f)->a].flags & AST_SYMBOL_FLAG_WARN_UNUSED_RESULT)) {
			diag_span r = cc_node_range(c, e);
			const attrs *a = &c->attr[c->syms[AST_NODE(f)->a].attrs];

			cc_report_warning(c, "unused-result", true, NULL, cc_node_range(c, AST_NODE(e)->a).b, &r, 1, NULL, 0,
				 "ignoring return value of function declared with '%s' attribute",
				 a->wur == 2 ? "nodiscard" : "warn_unused_result");
		}
	}
}

/* clang's Expr::isUnusedResultAWarning: does dropping e's value warn, where */
typedef struct cc_unused_value_state { uint32_t loc; diag_span r[2]; int nr; uint32_t e; } cc_unused_value_state;

static bool vol(cc *c, uint32_t e)
{
	return (AST_QUALIFIED_TYPE_QUALIFIERS(AST_NODE(e)->ty) & AST_QUALIFIER_VOLATILE) != 0;
}

static bool cc_unused_value_walk(cc *c, uint32_t e, cc_unused_value_state *w)
{
	const node *x;

	for (;;) {
		x = AST_NODE(e);
		if (x->op == AST_NODE_CAST && !c->span[e].e && x->a) {	/* implicit */
			if (vol(c, x->a) && (AST_NODE(x->a)->flags & AST_NODE_FLAG_LVALUE))
				return false;	/* a volatile load */
			e = x->a;
			continue;
		}
		break;
	}
	w->e = e, w->nr = 0;
	switch (x->op) {
	case AST_NODE_PRE_INCREMENT: case AST_NODE_PRE_DECREMENT: case AST_NODE_POST_INCREMENT: case AST_NODE_POST_DECREMENT: case AST_NODE_ASSIGN: case AST_NODE_CALL: case AST_NODE_BUILTIN:
	case AST_NODE_VA_ARG: case AST_NODE_ZERO: case AST_NODE_NONE:
		return false;
	case AST_NODE_UNARY:
		if (x->aux == LEX_TOKEN_PLUS || x->aux == LEX_TOKEN_MINUS || x->aux == LEX_TOKEN_TILDE || x->aux == LEX_TOKEN_EXCLAMATION)
			goto unop;
		return false;
	case AST_NODE_DEREFERENCE:
		if (vol(c, e))
			return false;
		__attribute__((fallthrough));
	case AST_NODE_ADDRESS:
	unop:
		w->loc = x->loc, w->r[0] = cc_node_range_bare(c, x->a), w->nr = 1;
		return true;
	case AST_NODE_COMMA: {
		if (x->aux == AST_NODE_PRE_INCREMENT || x->aux == AST_NODE_PRE_DECREMENT || x->aux == AST_NODE_POST_INCREMENT || x->aux == AST_NODE_POST_DECREMENT)
			return false; /* lowered complex increment/decrement */
		uint32_t r = x->b;

		while (AST_NODE(r)->op == AST_NODE_CAST && !c->span[r].e && AST_NODE(r)->a)
			r = AST_NODE(r)->a;
		if (AST_NODE(r)->op == AST_NODE_NUMBER && c->ipool[AST_NODE(r)->a] == 0)
			return false;	/* ((x = y), 0): the idiom */
		return cc_unused_value_walk(c, x->b, w);
	}
	case AST_NODE_LOGICAL_AND: case AST_NODE_LOGICAL_OR: {
		cc_unused_value_state t;

		if (!cc_unused_value_walk(c, x->b, &t))
			return false;
		w->loc = x->loc, w->r[0] = cc_node_range_bare(c, x->a), w->r[1] = cc_node_range_bare(c, x->b), w->nr = 2;
		return true;
	}
	case AST_NODE_BINARY:
		w->loc = x->loc, w->r[0] = cc_node_range_bare(c, x->a), w->r[1] = cc_node_range_bare(c, x->b), w->nr = 2;
		return true;
	case AST_NODE_CONDITIONAL: {
		cc_unused_value_state t;

		if (x->b && !cc_unused_value_walk(c, x->b, &t))
			return false;
		return cc_unused_value_walk(c, x->c, w);
	}
	case AST_NODE_MEMBER:
		if (vol(c, e))
			return false;
		w->loc = c->span[e].e ? c->span[e].e : x->loc, w->r[0] = cc_node_range_bare(c, x->a), w->nr = 1;	/* the member's name */
		return true;
	case AST_NODE_INDEX:
		if (vol(c, e))
			return false;
		w->loc = c->span[e].e ? c->span[e].e : x->loc, w->r[0] = cc_node_range_bare(c, x->a), w->r[1] = cc_node_range_bare(c, x->b), w->nr = 2;
		return true;
	case AST_NODE_VARIABLE:
		if (vol(c, e))
			return false;	/* a volatile load */
		goto plain;
	case AST_NODE_STATEMENT_EXPRESSION: {
		if (x->aux == AST_NODE_ASSIGN || x->aux == AST_NODE_PRE_INCREMENT || x->aux == AST_NODE_PRE_DECREMENT || x->aux == AST_NODE_POST_INCREMENT || x->aux == AST_NODE_POST_DECREMENT)
			return false; /* lowered atomic complex increment/decrement */
		uint32_t b = x->a, n = AST_NODE(b)->c, ls = n ? c->lists[AST_NODE(b)->b + n - 1] : 0;

		while (ls && AST_NODE(ls)->op == AST_NODE_LABEL)
			ls = AST_NODE(ls)->c;
		if (ls && AST_NODE(ls)->op == AST_NODE_EXPRESSION && AST_NODE(ls)->a)
			return cc_unused_value_walk(c, AST_NODE(ls)->a, w);
		if (cc_type_is_void(c, x->ty))
			return false;
		w->loc = x->loc, w->r[0] = cc_node_range_bare(c, e), w->nr = 1;
		return true;
	}
	case AST_NODE_CAST:	/* explicit */
		if (x->aux == AST_CAST_VOID)
			return false;
		w->loc = c->span[e].b, w->r[0] = cc_node_range_bare(c, x->a), w->nr = 1;
		return true;
	default:
	plain:
		w->loc = cc_node_range_bare(c, e).b, w->r[0] = cc_node_range_bare(c, e), w->nr = 1;
		return true;
	}
}

/* -Wunused-comparison: e, a dropped comparison (not parenthesized, not from a macro); true if it is one */
CC_PRIVATE bool cc_unused_comparison_check(cc *c, uint32_t e)
{
	if (AST_NODE(e)->op != AST_NODE_BINARY || !cc_operator_is_comparison(AST_NODE(e)->aux) || (AST_NODE(e)->flags & (AST_NODE_FLAG_PARENTHESIZED | AST_NODE_FLAG_FROM_MACRO | AST_NODE_FLAG_ERROR)))
		return false;
	{
		int k = AST_NODE(e)->aux;
		uint32_t op = AST_NODE(e)->loc;
		diag_span r = { cc_node_range_bare(c, AST_NODE(e)->a).b, cc_node_range_bare(c, AST_NODE(e)->b).e };
		bool eq = k == LEX_TOKEN_EQUAL_EQUAL || k == LEX_TOKEN_EXCLAMATION_EQUAL;

		if (cc_report_warning(c, "unused-comparison", true, cc_warning_group_all, op, &r, 1, NULL, 0, "%s comparison result unused",
			  eq ? "equality" : "relational") && k == LEX_TOKEN_EQUAL_EQUAL) {
			diag_span orr = { op, op + 2 };
			diag_fixit fx = { op, op + 2, "=", 0 };
			diag_info d = { "note", op, &orr, 1, &fx, 1, NULL, 0 };

			diag_report_extended(c->p, &d, "use '=' to turn this equality comparison into an assignment");
		}
	}
	return true;
}

/* clang's MaybeDecrementCount: a discarded full expression that only assigns a local variable */
CC_PRIVATE void cc_unused_set_only_check(cc *c, uint32_t e)
{
	const node *x = AST_NODE(e);
	uint32_t v;
	bool comp;

	if (x->op == AST_NODE_ASSIGN)
		v = x->a, comp = x->aux != 0;
	else if (x->op == AST_NODE_PRE_INCREMENT || x->op == AST_NODE_PRE_DECREMENT || x->op == AST_NODE_POST_INCREMENT || x->op == AST_NODE_POST_DECREMENT)
		v = x->a, comp = true;
	else
		return;
	if (AST_NODE(v)->op != AST_NODE_VARIABLE || (AST_NODE(v)->flags & AST_NODE_FLAG_PARENTHESIZED) || (comp && (AST_QUALIFIED_TYPE_QUALIFIERS(AST_NODE(v)->ty) & AST_QUALIFIER_VOLATILE)))
		return;
	if (c->syms[AST_NODE(v)->a].nref && c->syms[AST_NODE(v)->a].nref != 0xffff)
		c->syms[AST_NODE(v)->a].nref--;
}

/* -Wunused-value: e's value is dropped (a statement; comma: the left operand of a comma) */
CC_PRIVATE void cc_unused_value_check(cc *c, uint32_t e, bool comma)
{
	static const char *const g[] = { "unused", "all", "most", NULL };
	cc_unused_value_state w;
	const char *ms = NULL;
	uint32_t ml = 0;

	if (!c->fn || !e || (AST_NODE(e)->flags & AST_NODE_FLAG_ERROR))
		return;
	if (comma && AST_NODE(e)->op == AST_NODE_BINARY && cc_operator_is_comparison(AST_NODE(e)->aux) && !(AST_NODE(e)->flags & (AST_NODE_FLAG_PARENTHESIZED | AST_NODE_FLAG_FROM_MACRO)) &&
	    (c->in_stmtexpr || cc_unused_comparison_check(c, e)))
		return;	/* a comparison: -Wunused-comparison instead */
	if (c->dead || !cc_unused_value_walk(c, e, &w))
		return;
	if (AST_NODE(e)->op == AST_NODE_STATEMENT_EXPRESSION && (AST_NODE(e)->flags & AST_NODE_FLAG_FROM_MACRO))
		return;	/* clang: a statement expression from a macro */
	if (AST_NODE(w.e)->op == AST_NODE_VARIABLE)
		ms = cc_identifier_string(c->syms[AST_NODE(w.e)->a].name), ml = (uint32_t)cc_identifier_length(c->syms[AST_NODE(w.e)->a].name);
	cc_report_runtime_defer(c, "unused-value", g, w.loc, w.r, w.nr, ms, ml, "%s",
		 comma ? "left operand of comma operator has no effect" : "expression result unused");
}
