/* vcc front end: -Wunused-value, -Wunused-result and -Wunused-comparison on a dropped value */
#include "cc-internal.h"

/*
 * -Wunused-result for the value e that is dropped: a call of a
 * warn_unused_result/nodiscard function; a statement expression: its last
 * expression statement (last: the whole value is dropped), else the others
 */
CC_PRIVATE void cc_unused_result_check(cc *c, uint32_t e, bool last)
{
	if (AST_NODE(e)->op == N_STMTEXPR) {
		uint32_t b = AST_NODE(e)->a, n = AST_NODE(b)->c;
		/* clang: a statement expression from a macro (drmm_mutex_init) does not warn for its
		   value dropped */
		bool mac = last && (AST_NODE(e)->flags & NF_MACRO);

		for (uint32_t i = 0; i < n; i++) {
			uint32_t s = c->lists[AST_NODE(b)->b + i];

			if (AST_NODE(s)->op == N_EXPR && AST_NODE(s)->a && (i + 1 < n ? !last : last && !mac))
				cc_unused_result_check(c, AST_NODE(s)->a, true);
		}
		return;
	}
	if (AST_NODE(e)->op == N_CALL) {
		uint32_t f = AST_NODE(e)->a;

		while (AST_NODE(f)->op == N_CAST && AST_NODE(f)->a)
			f = AST_NODE(f)->a;
		if (AST_NODE(f)->op == N_VAR && (c->syms[AST_NODE(f)->a].flags & SF_WUR)) {
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
	return (QQ(AST_NODE(e)->ty) & Q_VOLATILE) != 0;
}

static bool cc_unused_value_walk(cc *c, uint32_t e, cc_unused_value_state *w)
{
	const node *x;

	for (;;) {
		x = AST_NODE(e);
		if (x->op == N_CAST && !c->span[e].e && x->a) {	/* implicit */
			if (vol(c, x->a) && (AST_NODE(x->a)->flags & NF_LVALUE))
				return false;	/* a volatile load */
			e = x->a;
			continue;
		}
		break;
	}
	w->e = e, w->nr = 0;
	switch (x->op) {
	case N_PREINC: case N_PREDEC: case N_POSTINC: case N_POSTDEC: case N_ASSIGN: case N_CALL: case N_BUILTIN:
	case N_VAARG: case N_ZERO: case N_NONE:
		return false;
	case N_UNARY:
		if (x->aux == LEX_TOKEN_PLUS || x->aux == LEX_TOKEN_MINUS || x->aux == LEX_TOKEN_TILDE || x->aux == LEX_TOKEN_EXCLAMATION)
			goto unop;
		return false;
	case N_DEREF:
		if (vol(c, e))
			return false;
		__attribute__((fallthrough));
	case N_ADDR:
	unop:
		w->loc = x->loc, w->r[0] = cc_node_range_bare(c, x->a), w->nr = 1;
		return true;
	case N_COMMA: {
		if (x->aux == N_PREINC || x->aux == N_PREDEC || x->aux == N_POSTINC || x->aux == N_POSTDEC)
			return false; /* lowered complex increment/decrement */
		uint32_t r = x->b;

		while (AST_NODE(r)->op == N_CAST && !c->span[r].e && AST_NODE(r)->a)
			r = AST_NODE(r)->a;
		if (AST_NODE(r)->op == N_NUM && c->ipool[AST_NODE(r)->a] == 0)
			return false;	/* ((x = y), 0): the idiom */
		return cc_unused_value_walk(c, x->b, w);
	}
	case N_LOGAND: case N_LOGOR: {
		cc_unused_value_state t;

		if (!cc_unused_value_walk(c, x->b, &t))
			return false;
		w->loc = x->loc, w->r[0] = cc_node_range_bare(c, x->a), w->r[1] = cc_node_range_bare(c, x->b), w->nr = 2;
		return true;
	}
	case N_BINARY:
		w->loc = x->loc, w->r[0] = cc_node_range_bare(c, x->a), w->r[1] = cc_node_range_bare(c, x->b), w->nr = 2;
		return true;
	case N_COND: {
		cc_unused_value_state t;

		if (x->b && !cc_unused_value_walk(c, x->b, &t))
			return false;
		return cc_unused_value_walk(c, x->c, w);
	}
	case N_MEMBER:
		if (vol(c, e))
			return false;
		w->loc = c->span[e].e ? c->span[e].e : x->loc, w->r[0] = cc_node_range_bare(c, x->a), w->nr = 1;	/* the member's name */
		return true;
	case N_INDEX:
		if (vol(c, e))
			return false;
		w->loc = c->span[e].e ? c->span[e].e : x->loc, w->r[0] = cc_node_range_bare(c, x->a), w->r[1] = cc_node_range_bare(c, x->b), w->nr = 2;
		return true;
	case N_VAR:
		if (vol(c, e))
			return false;	/* a volatile load */
		goto plain;
	case N_STMTEXPR: {
		if (x->aux == N_ASSIGN || x->aux == N_PREINC || x->aux == N_PREDEC || x->aux == N_POSTINC || x->aux == N_POSTDEC)
			return false; /* lowered atomic complex increment/decrement */
		uint32_t b = x->a, n = AST_NODE(b)->c, ls = n ? c->lists[AST_NODE(b)->b + n - 1] : 0;

		while (ls && AST_NODE(ls)->op == N_LABEL)
			ls = AST_NODE(ls)->c;
		if (ls && AST_NODE(ls)->op == N_EXPR && AST_NODE(ls)->a)
			return cc_unused_value_walk(c, AST_NODE(ls)->a, w);
		if (cc_type_is_void(c, x->ty))
			return false;
		w->loc = x->loc, w->r[0] = cc_node_range_bare(c, e), w->nr = 1;
		return true;
	}
	case N_CAST:	/* explicit */
		if (x->aux == CK_VOID)
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
	if (AST_NODE(e)->op != N_BINARY || !cc_operator_is_comparison(AST_NODE(e)->aux) || (AST_NODE(e)->flags & (NF_PAREN | NF_MACRO | NF_ERR)))
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

	if (x->op == N_ASSIGN)
		v = x->a, comp = x->aux != 0;
	else if (x->op == N_PREINC || x->op == N_PREDEC || x->op == N_POSTINC || x->op == N_POSTDEC)
		v = x->a, comp = true;
	else
		return;
	if (AST_NODE(v)->op != N_VAR || (AST_NODE(v)->flags & NF_PAREN) || (comp && (QQ(AST_NODE(v)->ty) & Q_VOLATILE)))
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

	if (!c->fn || !e || (AST_NODE(e)->flags & NF_ERR))
		return;
	if (comma && AST_NODE(e)->op == N_BINARY && cc_operator_is_comparison(AST_NODE(e)->aux) && !(AST_NODE(e)->flags & (NF_PAREN | NF_MACRO)) &&
	    (c->in_stmtexpr || cc_unused_comparison_check(c, e)))
		return;	/* a comparison: -Wunused-comparison instead */
	if (c->dead || !cc_unused_value_walk(c, e, &w))
		return;
	if (AST_NODE(e)->op == N_STMTEXPR && (AST_NODE(e)->flags & NF_MACRO))
		return;	/* clang: a statement expression from a macro */
	if (AST_NODE(w.e)->op == N_VAR)
		ms = cc_identifier_string(c->syms[AST_NODE(w.e)->a].name), ml = (uint32_t)cc_identifier_length(c->syms[AST_NODE(w.e)->a].name);
	cc_report_runtime_defer(c, "unused-value", g, w.loc, w.r, w.nr, ms, ml, "%s",
		 comma ? "left operand of comma operator has no effect" : "expression result unused");
}
