/* vcc front end: -Wreturn-type, can control reach the end of a body, and the path that explains it */
#include "cc-internal.h"

/* ---------------- -Wreturn-type: can control reach the end of the body ---------------- */

/* a walk of one function body as clang's CFG sees it: what is reachable from the entry */
typedef struct cc_return_path_reach_state {
	bool brk, cont;		/* a break / continue of the innermost loop (break: or switch) reached */
	bool ret;		/* a return reached */
	bool sw;		/* the innermost switch is reached: its case labels are */
	bool more;		/* a goto reached a label not reached before: walk again */
	bool unsure;		/* an indirect goto: no answer */
	uint8_t *lab;		/* reached labels, by index - lab0 */
	uint32_t lab0, nlab;
} cc_return_path_reach_state;

static bool cc_return_path_reach(cc *c, uint32_t n, bool reach, cc_return_path_reach_state *f);

static void cc_return_path_goto_label(cc_return_path_reach_state *f, uint32_t l)
{
	if (l >= f->lab0 && l - f->lab0 < f->nlab && !f->lab[l - f->lab0])
		f->lab[l - f->lab0] = 1, f->more = true;
}

static bool cc_return_path_label_is_reached(const cc_return_path_reach_state *f, uint32_t l)
{
	return l >= f->lab0 && l - f->lab0 < f->nlab && f->lab[l - f->lab0];
}

/* a statement expression's body (its gotos count) */
static bool cc_return_path_statement_expression(cc *c, uint32_t n, bool reach, cc_return_path_reach_state *f)
{
	cc_return_path_reach_state g = *f;
	bool end;

	g.brk = g.cont = false;
	end = cc_return_path_reach(c, n, reach, &g);
	f->ret |= g.ret, f->more |= g.more, f->unsure |= g.unsure;
	return end;
}

/* a call that does not return (noreturn function, __builtin_unreachable/trap), in every
   evaluation of n; false when unsure (no false -Wreturn-type) */
static bool cc_expression_is_noreturn(cc *c, uint32_t n, bool reach, cc_return_path_reach_state *f)
{
	const node *x;

	if (!n)
		return false;
	x = AST_NODE(n);
	switch (x->op) {
	case AST_NODE_CAST:
		return cc_expression_is_noreturn(c, x->a, reach, f);
	case AST_NODE_CALL: {
		const node *g = AST_NODE(x->a);

		while (g->op == AST_NODE_CAST)
			g = AST_NODE(g->a);
		if (g->op == AST_NODE_VARIABLE && (c->syms[g->a].flags & AST_SYMBOL_FLAG_NORETURN))
			return true;
		if (cc_expression_is_noreturn(c, x->a, reach, f))
			return true;
		for (uint32_t i = 0; i < x->c; i++)
			if (cc_expression_is_noreturn(c, c->lists[x->b + i], reach, f))
				return true;
		return false;
	}
	case AST_NODE_BUILTIN:
		return x->aux == CC_BUILTIN_UNREACHABLE || x->aux == CC_BUILTIN_TRAP;
	case AST_NODE_COMMA:
		return cc_expression_is_noreturn(c, x->a, reach, f) || cc_expression_is_noreturn(c, x->b, reach, f);
	case AST_NODE_CONDITIONAL:
		return cc_expression_is_noreturn(c, x->a, reach, f) ||
		       (x->b && cc_expression_is_noreturn(c, x->b, reach, f) && cc_expression_is_noreturn(c, x->c, reach, f));
	case AST_NODE_STATEMENT_EXPRESSION:
		return !cc_return_path_statement_expression(c, x->a, reach, f);
	default:
		return false;
	}
}

/* a condition's constant value: 1 true, 0 false, -1 not constant (clang's CFG prunes
   the branches of a constant condition) */
CC_PRIVATE int cc_condition_value(cc *c, uint32_t n)
{
	const node *x;
	cc_value v;

	if (!n)
		return 1;	/* for (;;) */
	if (cc_eval(c, n, &v))
		return v.kind == CC_VALUE_INTEGER ? v.i != 0 : -1;
	/* clang's CFG (tryEvaluateBool): one decided side of || / &&, and a const variable
	   with a constant initializer ("static const bool f = false;") */
	x = AST_NODE(n);
	while (x->op == AST_NODE_CAST)
		x = AST_NODE(x->a);
	switch (x->op) {
	case AST_NODE_LOGICAL_OR:
	case AST_NODE_LOGICAL_AND: {
		int a = cc_condition_value(c, x->a), b = cc_condition_value(c, x->b), d = x->op == AST_NODE_LOGICAL_OR;

		if (a == d || b == d)
			return d;
		return a == !d && b == !d ? !d : -1;
	}
	case AST_NODE_UNARY: {
		int a = x->aux == LEX_TOKEN_EXCLAMATION ? cc_condition_value(c, x->a) : -1;

		return a < 0 ? -1 : !a;
	}
	case AST_NODE_VARIABLE: {
		const sym *y = &c->syms[x->a];

		if (y->kind == AST_SYMBOL_VARIABLE && (AST_QUALIFIED_TYPE_QUALIFIERS(y->ty) & AST_QUALIFIER_CONST) && y->body && cc_type_is_integer(c, y->ty) && cc_eval(c, y->body, &v) &&
		    v.kind == CC_VALUE_INTEGER)
			return v.i != 0;
		return -1;
	}
	default:
		return -1;
	}
}

/* the case values of a switch body (not of nested switches) */
static void cc_switch_case_values(cc *c, uint32_t n, i128 *v, uint32_t *nv, uint32_t max)
{
	const node *x;

	if (!n)
		return;
	x = AST_NODE(n);
	switch (x->op) {
	case AST_NODE_CASE:
		if (c->ipool[x->a] == c->ipool[x->b] && *nv < max)
			v[(*nv)++] = c->ipool[x->a];
		else
			*nv = max + 1;	/* a range: not counted */
		cc_switch_case_values(c, x->c, v, nv, max);
		return;
	case AST_NODE_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			cc_switch_case_values(c, c->lists[x->b + i], v, nv, max);
		return;
	case AST_NODE_LABEL: case AST_NODE_DEFAULT:
		cc_switch_case_values(c, x->c, v, nv, max);
		return;
	case AST_NODE_IF:
		cc_switch_case_values(c, x->b, v, nv, max), cc_switch_case_values(c, x->c, v, nv, max);
		return;
	case AST_NODE_WHILE: case AST_NODE_DO:
		cc_switch_case_values(c, x->b, v, nv, max);
		return;
	case AST_NODE_FOR:
		cc_switch_case_values(c, c->lists[x->c + 1], v, nv, max);
		return;
	default:
		return;
	}
}

/* a default label of this switch (not of a nested one) */
CC_PRIVATE bool cc_switch_has_default(cc *c, uint32_t n)
{
	const node *x;

	if (!n)
		return false;
	x = AST_NODE(n);
	switch (x->op) {
	case AST_NODE_DEFAULT:
		return true;
	case AST_NODE_CASE: case AST_NODE_LABEL:
		return cc_switch_has_default(c, x->c);
	case AST_NODE_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			if (cc_switch_has_default(c, c->lists[x->b + i]))
				return true;
		return false;
	case AST_NODE_IF:
		return cc_switch_has_default(c, x->b) || cc_switch_has_default(c, x->c);
	case AST_NODE_WHILE: case AST_NODE_DO:
		return cc_switch_has_default(c, x->b);
	case AST_NODE_FOR:
		return cc_switch_has_default(c, c->lists[x->c + 1]);
	default:
		return false;
	}
}

/* clang's CFG: a switch on an enum whose cases name every enumerator has no edge past it */
CC_PRIVATE bool cc_switch_is_enum_covered(cc *c, const node *sw)
{
	const node *x = AST_NODE(sw->a);
	const tag *g;
	i128 v[256];
	uint32_t nv = 0;

	while (x->op == AST_NODE_CAST)
		x = AST_NODE(x->a);
	if (AST_TYPE_KIND(c, x->ty) != AST_TYPE_ENUM)
		return false;
	g = &c->tags[AST_TYPE(c, x->ty)->aux];
	if (!g->nenum)
		return false;
	cc_switch_case_values(c, sw->b, v, &nv, 256);
	if (nv > 256)
		return false;
	for (uint32_t i = 0; i < g->nenum; i++) {
		i128 e = c->syms[c->lists[g->enums + i]].val;
		bool found = false;

		for (uint32_t k = 0; k < nv && !found; k++)
			found = v[k] == e;
		if (!found)
			return false;
	}
	return true;
}

/* a loop body with its own break/continue */
static bool cc_return_path_loop_body(cc *c, uint32_t body, bool reach, cc_return_path_reach_state *f, bool *brk, bool *cont)
{
	bool ob = f->brk, oc = f->cont, end;

	f->brk = f->cont = false;
	end = cc_return_path_reach(c, body, reach, f);
	*brk = f->brk, *cont = f->cont;
	f->brk = ob, f->cont = oc;
	return end;
}

/* whether the end of n is reachable when its start is (reach) */
static bool cc_return_path_reach(cc *c, uint32_t n, bool reach, cc_return_path_reach_state *f)
{
	const node *x;

	if (!n)
		return reach;
	x = AST_NODE(n);
	switch (x->op) {
	case AST_NODE_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			reach = cc_return_path_reach(c, c->lists[x->b + i], reach, f);
		return reach;
	case AST_NODE_LABEL:
		return cc_return_path_reach(c, x->c, reach || cc_return_path_label_is_reached(f, x->a), f);
	case AST_NODE_CASE: case AST_NODE_DEFAULT:
		return cc_return_path_reach(c, x->c, reach || f->sw, f);
	case AST_NODE_RETURN:
		if (reach)
			f->ret = true;
		if (reach && x->a)
			cc_expression_is_noreturn(c, x->a, reach, f);	/* (its statement expressions' gotos) */
		return false;
	case AST_NODE_GOTO:
		if (reach)
			cc_return_path_goto_label(f, x->a);
		return false;
	case AST_NODE_GOTO_INDIRECT:
		if (reach)
			f->unsure = true;
		return false;
	case AST_NODE_BREAK:
		if (reach)
			f->brk = true;
		return false;
	case AST_NODE_CONTINUE:
		if (reach)
			f->cont = true;
		return false;
	case AST_NODE_EXPRESSION:
		return !cc_expression_is_noreturn(c, x->a, reach, f) && reach;
	case AST_NODE_DECLARATION:
		return !cc_expression_is_noreturn(c, x->b, reach, f) && reach;
	case AST_NODE_ASM: {
		const ast_asm_statement *a = &c->asms[x->a];

		if (reach)
			for (uint32_t i = 0; i < a->nlabel; i++)
				cc_return_path_goto_label(f, a->labels[i]);
		return reach;
	}
	case AST_NODE_IF: {
		int k = cc_condition_value(c, x->a);
		bool t, e;

		if (cc_expression_is_noreturn(c, x->a, reach, f))
			reach = false;
		t = cc_return_path_reach(c, x->b, reach && k != 0, f);
		e = x->c ? cc_return_path_reach(c, x->c, reach && k != 1, f) : reach && k != 1;
		return t || e;
	}
	case AST_NODE_WHILE: {
		int k = cc_condition_value(c, x->a);
		bool brk, cont, end = cc_return_path_loop_body(c, x->b, reach && k != 0, f, &brk, &cont);

		/* the condition: from the entry, the body's end, a continue */
		return ((reach || end || cont) && k != 1) || brk;
	}
	case AST_NODE_FOR: {
		const node *init = x->a ? AST_NODE(x->a) : NULL;
		uint32_t body = c->lists[x->c + 1], step = c->lists[x->c];
		int k = cc_condition_value(c, x->b);
		bool brk, cont, end, again;

		if (init && init->op != AST_NODE_BLOCK && init->op != AST_NODE_DECLARATION && cc_expression_is_noreturn(c, x->a, reach, f))
			reach = false;
		else if (init && (init->op == AST_NODE_BLOCK || init->op == AST_NODE_DECLARATION))
			reach = cc_return_path_reach(c, x->a, reach, f);
		end = cc_return_path_loop_body(c, body, reach && k != 0, f, &brk, &cont);
		again = end || cont;	/* the step runs */
		if (again && step && cc_expression_is_noreturn(c, step, true, f))
			again = false;
		return ((reach || again) && k != 1) || brk;
	}
	case AST_NODE_DO: {
		int k = cc_condition_value(c, x->a);
		bool brk, cont, end = cc_return_path_loop_body(c, x->b, reach, f, &brk, &cont);

		/* the condition: from the body's end or a continue */
		return ((end || cont) && k != 1) || brk;
	}
	case AST_NODE_SWITCH: {
		bool ob = f->brk, os = f->sw, end, brk, dflt;

		if (cc_expression_is_noreturn(c, x->a, reach, f))
			reach = false;
		dflt = cc_switch_has_default(c, x->b) || cc_switch_is_enum_covered(c, x);
		f->brk = false, f->sw = reach;
		end = cc_return_path_reach(c, x->b, false, f);
		brk = f->brk;
		f->brk = ob, f->sw = os;	/* a continue in a switch is the enclosing loop's: kept */
		return end || brk || (reach && !dflt);
	}
	default:
		return reach;
	}
}

/* D8.2: one path to the end of the body, as numbered steps (GCC's analyzer, Rust's notes):
   which condition is false, which case the switch jumps to, which break leaves a loop.
   Only for the warning: it re-walks the subtrees with cc_return_path_reach, quadratic in the nesting
   depth, on a body that warns. */
typedef struct {
	uint32_t b, e;		/* the step's range */
	const char *what;	/* its text */
	uint32_t tb, te;	/* the source in quotes (te > tb), else none */
} cc_return_path_step;

typedef struct {
	cc_return_path_reach_state *f;
	cc_return_path_step st[12];
	int n;
	bool fail;		/* a goto or a label: no path given */
	const char *brk;	/* what a break leaves: "loop", "switch" */
} cc_return_path;

static void cc_return_path_step_add(cc *c, cc_return_path *P, uint32_t b, uint32_t e, const char *what, uint32_t q)
{
	cc_return_path_step *s;

	if (P->n == (int)(sizeof P->st / sizeof P->st[0])) {
		P->fail = true;
		return;
	}
	s = &P->st[P->n++];
	s->b = b, s->e = e, s->what = what, s->tb = s->te = 0;
	if (q) {
		diag_span r = cc_node_range(c, q);

		s->tb = r.b, s->te = r.e;
	}
}

static bool cc_return_path_end_of(cc *c, uint32_t n, bool reach, const cc_return_path_reach_state *f, bool *brk)
{
	cc_return_path_reach_state g = *f;

	g.brk = g.cont = g.ret = false;
	reach = cc_return_path_reach(c, n, reach, &g);
	if (brk)
		*brk = g.brk;
	return reach;
}

static bool cc_return_path_walk(cc *c, uint32_t n, bool reach, bool brk, cc_return_path *P);

/* statement i of a block entered: from the block's start or a case/label on the way */
static bool cc_return_path_self_entry(cc *c, uint32_t n, const cc_return_path_reach_state *f)
{
	const node *x = AST_NODE(n);

	return ((x->op == AST_NODE_CASE || x->op == AST_NODE_DEFAULT) && f->sw) || (x->op == AST_NODE_LABEL && cc_return_path_label_is_reached(f, x->a));
}

/* the block's statements from the latest entry before i up to i, the last one to brk */
static bool cc_return_path_sequence(cc *c, const node *x, uint32_t i, bool reach, bool brk, cc_return_path *P)
{
	uint32_t j = i;
	uint8_t small[64], *end = i <= sizeof small ? small : malloc(i);
	bool r = reach;

	if (!end) {
		P->fail = true;
		return false;
	}
	for (uint32_t k = 0; k < i; k++)	/* each statement's end, once */
		end[k] = r = cc_return_path_end_of(c, c->lists[x->b + k], r, P->f, NULL);
	/* back through statements whose end falls into the next, to an entry */
	while (j > 0 && !cc_return_path_self_entry(c, c->lists[x->b + j], P->f) && end[j - 1])
		j--;
	if (end != small)
		free(end);
	r = j == 0 ? reach : false;
	for (uint32_t k = j; k <= i; k++) {
		if (!cc_return_path_walk(c, c->lists[x->b + k], r, k == i && brk, P))
			return false;
		r = true;
	}
	return true;
}

static bool cc_return_path_walk(cc *c, uint32_t n, bool reach, bool brk, cc_return_path *P)
{
	const node *x;
	int mark = P->n, k;

	if (!n)
		return reach && !brk;
	if (P->fail)
		return false;
	x = AST_NODE(n);
	switch (x->op) {
	case AST_NODE_BLOCK: {
		bool in = reach;

		if (!x->c)
			return reach && !brk;
		if (!brk)
			return cc_return_path_end_of(c, n, reach, P->f, NULL) && cc_return_path_sequence(c, x, x->c - 1, reach, false, P);
		for (uint32_t i = 0; i < x->c; i++) {
			uint32_t s = c->lists[x->b + i];
			bool b;

			in = in || cc_return_path_self_entry(c, s, P->f);
			cc_return_path_end_of(c, s, in, P->f, &b);
			if (b) {
				if (cc_return_path_sequence(c, x, i, reach, true, P))
					return true;
				P->n = mark;
			}
			in = cc_return_path_end_of(c, s, in, P->f, NULL);
		}
		return false;
	}
	case AST_NODE_LABEL:
		if (!reach && cc_return_path_label_is_reached(P->f, x->a))
			P->fail = true;	/* reached by a goto */
		return cc_return_path_walk(c, x->c, true, brk, P);
	case AST_NODE_CASE: case AST_NODE_DEFAULT:
		if (!reach && P->f->sw)
			cc_return_path_step_add(c, P, x->loc, pp_token_end_find(c->p, x->loc), x->op == AST_NODE_CASE ? "the switch jumps to this case"
					      : "the switch jumps to 'default'", 0);
		return cc_return_path_walk(c, x->c, true, brk, P);
	case AST_NODE_BREAK:
		if (!reach || !brk)
			return false;
		cc_return_path_step_add(c, P, x->loc, pp_token_end_find(c->p, x->loc), P->brk && P->brk[0] == 's' ? "this 'break' leaves the switch"
				      : "this 'break' leaves the loop", 0);
		return true;
	case AST_NODE_IF: {
		bool t, e, tb, eb;

		k = cc_condition_value(c, x->a);
		if (!reach)
			return false;
		t = cc_return_path_end_of(c, x->b, k != 0, P->f, &tb);
		e = x->c ? cc_return_path_end_of(c, x->c, k != 1, P->f, &eb) : (eb = false, k != 1);
		if (!brk && t && e)
			return true;	/* both arms go on: this if decides nothing */
		if (k != 0 && (brk ? tb : t)) {
			if (k < 0)
				cc_return_path_step_add(c, P, 0, 0, "is true", x->a);
			if (cc_return_path_walk(c, x->b, true, brk, P))
				return true;
			P->n = mark;
		}
		if (k != 1 && (brk ? eb : e)) {
			if (k < 0)
				cc_return_path_step_add(c, P, 0, 0, "is false", x->a);
			if (x->c ? cc_return_path_walk(c, x->c, true, brk, P) : !brk)
				return true;
			P->n = mark;
		}
		return false;
	}
	case AST_NODE_WHILE: case AST_NODE_FOR: case AST_NODE_DO: {
		uint32_t cnd = x->op == AST_NODE_FOR ? x->b : x->a, body = x->op == AST_NODE_FOR ? c->lists[x->c + 1] : x->b;
		const char *ob = P->brk;
		bool ok = false, b;

		if (brk || !reach)
			return false;	/* (a break inside is this loop's) */
		k = cc_condition_value(c, cnd);
		if (x->op == AST_NODE_FOR && x->a && (AST_NODE(x->a)->op == AST_NODE_BLOCK || AST_NODE(x->a)->op == AST_NODE_DECLARATION) &&
		    !cc_return_path_end_of(c, x->a, true, P->f, NULL))
			return false;
		if (k != 1 && x->op != AST_NODE_DO) {	/* the first test fails */
			if (k < 0)
				cc_return_path_step_add(c, P, 0, 0, "is false, so the loop ends", cnd);
			return true;
		}
		if (k != 1 && cc_return_path_walk(c, body, true, false, P)) {	/* do: once round, then the test */
			if (k < 0)
				cc_return_path_step_add(c, P, 0, 0, "is false, so the loop ends", cnd);
			return true;
		}
		P->n = mark;
		cc_return_path_end_of(c, body, true, P->f, &b);
		if (b) {
			P->brk = "loop";
			ok = cc_return_path_walk(c, body, true, true, P);
			P->brk = ob;
		}
		if (!ok)
			P->n = mark;
		return ok;
	}
	case AST_NODE_SWITCH: {
		const char *ob = P->brk;
		bool os = P->f->sw, ok;

		if (brk || !reach)
			return false;
		if (!cc_switch_has_default(c, x->b) && !cc_switch_is_enum_covered(c, x)) {
			cc_return_path_step_add(c, P, 0, 0, "matches no case", x->a);
			return true;
		}
		P->f->sw = true, P->brk = "switch";
		ok = cc_return_path_walk(c, x->b, false, false, P);
		if (!ok) {
			P->n = mark;
			ok = cc_return_path_walk(c, x->b, false, true, P);
		}
		P->f->sw = os, P->brk = ob;
		if (!ok)
			P->n = mark;
		return ok;
	}
	case AST_NODE_RETURN: case AST_NODE_GOTO: case AST_NODE_GOTO_INDIRECT: case AST_NODE_CONTINUE:
		return false;
	default:
		return !brk && cc_return_path_end_of(c, n, reach, P->f, NULL);
	}
}

static int cc_return_path_step_compare(const void *a, const void *b)
{
	uint32_t x = ((const cc_return_path_step *)a)->tb ? ((const cc_return_path_step *)a)->tb : ((const cc_return_path_step *)a)->b;
	uint32_t y = ((const cc_return_path_step *)b)->tb ? ((const cc_return_path_step *)b)->tb : ((const cc_return_path_step *)b)->b;

	return x < y ? -1 : x > y;
}

/* the steps as labels "(1) 'x > 0' is false", ..., the last at the closing brace */
static void cc_return_path_report(cc *c, uint32_t body, cc_return_path_reach_state *f, diag_record *d, uint32_t rbrace, const sym *y)
{
	cc_return_path P = { .f = f };
	char buf[160];
	int i;

	f->sw = false;
	if (!cc_return_path_walk(c, body, true, false, &P) || P.fail || !P.n)
		return;
	qsort(P.st, (size_t)P.n, sizeof P.st[0], cc_return_path_step_compare);
	for (i = 0; i < P.n; i++) {
		const cc_return_path_step *s = &P.st[i];

		if (s->te > s->tb) {
			size_t len;
			const char *t = pp_source_text(c->p, s->tb, s->te, &len);

			if (t && len <= 48 && !memchr(t, '\n', len))
				snprintf(buf, sizeof buf, "(%d) '%.*s' %s", i + 1, (int)len, t, s->what);
			else
				snprintf(buf, sizeof buf, "(%d) the condition %s", i + 1, s->what);
			diag_record_label_add(c->p, d, s->tb, s->te, buf);
		} else {
			snprintf(buf, sizeof buf, "(%d) %s", i + 1, s->what);
			diag_record_label_add(c->p, d, s->b, s->e, buf);
		}
	}
	snprintf(buf, sizeof buf, "(%d) '%.*s' ends here without a return", i + 1, CC_IDENTIFIER(y->name));
	diag_record_label_add(c->p, d, rbrace, rbrace + 1, buf);
}

/* clang's -Wreturn-type at the body's closing brace (flow.c decides: k 1 "in all control
   paths", 2 no return reached); with returns on some paths, one path that has none is
   shown as numbered steps */
CC_PRIVATE __attribute__((noinline)) void cc_return_type_check(cc *c, uint32_t s, uint32_t body, uint32_t rbrace,
							uint32_t lab0, int k)
{
	const sym *y = &c->syms[s];
	uint8_t small[64];
	cc_return_path_reach_state f = { 0 };
	bool end = false;
	diag_record *d;

	d = cc_report_warning_begin(c, "return-type", true, NULL, rbrace, k == 1 ? "non-void function does not return a value in all control paths"
			  : "non-void function does not return a value");
	if (!d)
		return;
	f.lab0 = lab0, f.nlab = c->nlabel - lab0;
	f.lab = f.nlab <= sizeof small ? small : calloc(f.nlab, 1);
	memset(f.lab, 0, f.nlab <= sizeof small ? f.nlab : 0);
	if (k == 1) {
		do {	/* again while a goto reaches a new label (each pass reaches more) */
			f.more = f.ret = f.brk = f.cont = f.sw = false;
			end = cc_return_path_reach(c, body, true, &f);
		} while (f.more && !f.unsure);
		if (end && !f.unsure && f.ret)	/* some paths return: show one that does not */
			cc_return_path_report(c, body, &f, d, rbrace, y);
	}
	diag_record_help_set(c->p, d, "'%.*s' returns '%s': end it with a return statement", CC_IDENTIFIER(y->name),
		     tyname(c, AST_TYPE(c, y->ty)->base));
	diag_record_emit(c->p, d);
	if (f.lab != small)
		free(f.lab);
}
