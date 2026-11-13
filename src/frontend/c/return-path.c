/* vcc front end: -Wreturn-type, can control reach the end of a body, and the path that explains it */
#include "ccimpl.h"

/* ---------------- -Wreturn-type: can control reach the end of the body ---------------- */

/* a walk of one function body as clang's CFG sees it: what is reachable from the entry */
typedef struct flow {
	bool brk, cont;		/* a break / continue of the innermost loop (break: or switch) reached */
	bool ret;		/* a return reached */
	bool sw;		/* the innermost switch is reached: its case labels are */
	bool more;		/* a goto reached a label not reached before: walk again */
	bool unsure;		/* an indirect goto: no answer */
	uint8_t *lab;		/* reached labels, by index - lab0 */
	uint32_t lab0, nlab;
} flow;

static bool stmt_flow(cc *c, uint32_t n, bool reach, flow *f);

static void goto_label(flow *f, uint32_t l)
{
	if (l >= f->lab0 && l - f->lab0 < f->nlab && !f->lab[l - f->lab0])
		f->lab[l - f->lab0] = 1, f->more = true;
}

static bool label_reached(const flow *f, uint32_t l)
{
	return l >= f->lab0 && l - f->lab0 < f->nlab && f->lab[l - f->lab0];
}

/* a statement expression's body (its gotos count) */
static bool stmtexpr_flow(cc *c, uint32_t n, bool reach, flow *f)
{
	flow g = *f;
	bool end;

	g.brk = g.cont = false;
	end = stmt_flow(c, n, reach, &g);
	f->ret |= g.ret, f->more |= g.more, f->unsure |= g.unsure;
	return end;
}

/* a call that does not return (noreturn function, __builtin_unreachable/trap), in every
   evaluation of n; false when unsure (no false -Wreturn-type) */
static bool expr_noreturn(cc *c, uint32_t n, bool reach, flow *f)
{
	const node *x;

	if (!n)
		return false;
	x = N(n);
	switch (x->op) {
	case N_CAST:
		return expr_noreturn(c, x->a, reach, f);
	case N_CALL: {
		const node *g = N(x->a);

		while (g->op == N_CAST)
			g = N(g->a);
		if (g->op == N_VAR && (c->syms[g->a].flags & SF_NORETURN))
			return true;
		if (expr_noreturn(c, x->a, reach, f))
			return true;
		for (uint32_t i = 0; i < x->c; i++)
			if (expr_noreturn(c, c->lists[x->b + i], reach, f))
				return true;
		return false;
	}
	case N_BUILTIN:
		return x->aux == BI_UNREACHABLE || x->aux == BI_TRAP;
	case N_COMMA:
		return expr_noreturn(c, x->a, reach, f) || expr_noreturn(c, x->b, reach, f);
	case N_COND:
		return expr_noreturn(c, x->a, reach, f) ||
		       (x->b && expr_noreturn(c, x->b, reach, f) && expr_noreturn(c, x->c, reach, f));
	case N_STMTEXPR:
		return !stmtexpr_flow(c, x->a, reach, f);
	default:
		return false;
	}
}

/* a condition's constant value: 1 true, 0 false, -1 not constant (clang's CFG prunes
   the branches of a constant condition) */
VPRIV int cond_value(cc *c, uint32_t n)
{
	const node *x;
	cval v;

	if (!n)
		return 1;	/* for (;;) */
	if (cc_eval(c, n, &v))
		return v.kind == CV_INT ? v.i != 0 : -1;
	/* clang's CFG (tryEvaluateBool): one decided side of || / &&, and a const variable
	   with a constant initializer ("static const bool f = false;") */
	x = N(n);
	while (x->op == N_CAST)
		x = N(x->a);
	switch (x->op) {
	case N_LOGOR:
	case N_LOGAND: {
		int a = cond_value(c, x->a), b = cond_value(c, x->b), d = x->op == N_LOGOR;

		if (a == d || b == d)
			return d;
		return a == !d && b == !d ? !d : -1;
	}
	case N_UNARY: {
		int a = x->aux == LEX_TOKEN_EXCLAMATION ? cond_value(c, x->a) : -1;

		return a < 0 ? -1 : !a;
	}
	case N_VAR: {
		const sym *y = &c->syms[x->a];

		if (y->kind == S_VAR && (QQ(y->ty) & Q_CONST) && y->body && is_integer(c, y->ty) && cc_eval(c, y->body, &v) &&
		    v.kind == CV_INT)
			return v.i != 0;
		return -1;
	}
	default:
		return -1;
	}
}

/* the case values of a switch body (not of nested switches) */
static void case_values(cc *c, uint32_t n, i128 *v, uint32_t *nv, uint32_t max)
{
	const node *x;

	if (!n)
		return;
	x = N(n);
	switch (x->op) {
	case N_CASE:
		if (c->ipool[x->a] == c->ipool[x->b] && *nv < max)
			v[(*nv)++] = c->ipool[x->a];
		else
			*nv = max + 1;	/* a range: not counted */
		case_values(c, x->c, v, nv, max);
		return;
	case N_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			case_values(c, c->lists[x->b + i], v, nv, max);
		return;
	case N_LABEL: case N_DEFAULT:
		case_values(c, x->c, v, nv, max);
		return;
	case N_IF:
		case_values(c, x->b, v, nv, max), case_values(c, x->c, v, nv, max);
		return;
	case N_WHILE: case N_DO:
		case_values(c, x->b, v, nv, max);
		return;
	case N_FOR:
		case_values(c, c->lists[x->c + 1], v, nv, max);
		return;
	default:
		return;
	}
}

/* a default label of this switch (not of a nested one) */
VPRIV bool has_default(cc *c, uint32_t n)
{
	const node *x;

	if (!n)
		return false;
	x = N(n);
	switch (x->op) {
	case N_DEFAULT:
		return true;
	case N_CASE: case N_LABEL:
		return has_default(c, x->c);
	case N_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			if (has_default(c, c->lists[x->b + i]))
				return true;
		return false;
	case N_IF:
		return has_default(c, x->b) || has_default(c, x->c);
	case N_WHILE: case N_DO:
		return has_default(c, x->b);
	case N_FOR:
		return has_default(c, c->lists[x->c + 1]);
	default:
		return false;
	}
}

/* clang's CFG: a switch on an enum whose cases name every enumerator has no edge past it */
VPRIV bool enum_covered(cc *c, const node *sw)
{
	const node *x = N(sw->a);
	const tag *g;
	i128 v[256];
	uint32_t nv = 0;

	while (x->op == N_CAST)
		x = N(x->a);
	if (KIND(c, x->ty) != TY_ENUM)
		return false;
	g = &c->tags[T(c, x->ty)->aux];
	if (!g->nenum)
		return false;
	case_values(c, sw->b, v, &nv, 256);
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
static bool loop_body(cc *c, uint32_t body, bool reach, flow *f, bool *brk, bool *cont)
{
	bool ob = f->brk, oc = f->cont, end;

	f->brk = f->cont = false;
	end = stmt_flow(c, body, reach, f);
	*brk = f->brk, *cont = f->cont;
	f->brk = ob, f->cont = oc;
	return end;
}

/* whether the end of n is reachable when its start is (reach) */
static bool stmt_flow(cc *c, uint32_t n, bool reach, flow *f)
{
	const node *x;

	if (!n)
		return reach;
	x = N(n);
	switch (x->op) {
	case N_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			reach = stmt_flow(c, c->lists[x->b + i], reach, f);
		return reach;
	case N_LABEL:
		return stmt_flow(c, x->c, reach || label_reached(f, x->a), f);
	case N_CASE: case N_DEFAULT:
		return stmt_flow(c, x->c, reach || f->sw, f);
	case N_RETURN:
		if (reach)
			f->ret = true;
		if (reach && x->a)
			expr_noreturn(c, x->a, reach, f);	/* (its statement expressions' gotos) */
		return false;
	case N_GOTO:
		if (reach)
			goto_label(f, x->a);
		return false;
	case N_GOTO_IND:
		if (reach)
			f->unsure = true;
		return false;
	case N_BREAK:
		if (reach)
			f->brk = true;
		return false;
	case N_CONTINUE:
		if (reach)
			f->cont = true;
		return false;
	case N_EXPR:
		return !expr_noreturn(c, x->a, reach, f) && reach;
	case N_DECL:
		return !expr_noreturn(c, x->b, reach, f) && reach;
	case N_ASM: {
		const casm *a = &c->asms[x->a];

		if (reach)
			for (uint32_t i = 0; i < a->nlabel; i++)
				goto_label(f, a->labels[i]);
		return reach;
	}
	case N_IF: {
		int k = cond_value(c, x->a);
		bool t, e;

		if (expr_noreturn(c, x->a, reach, f))
			reach = false;
		t = stmt_flow(c, x->b, reach && k != 0, f);
		e = x->c ? stmt_flow(c, x->c, reach && k != 1, f) : reach && k != 1;
		return t || e;
	}
	case N_WHILE: {
		int k = cond_value(c, x->a);
		bool brk, cont, end = loop_body(c, x->b, reach && k != 0, f, &brk, &cont);

		/* the condition: from the entry, the body's end, a continue */
		return ((reach || end || cont) && k != 1) || brk;
	}
	case N_FOR: {
		const node *init = x->a ? N(x->a) : NULL;
		uint32_t body = c->lists[x->c + 1], step = c->lists[x->c];
		int k = cond_value(c, x->b);
		bool brk, cont, end, again;

		if (init && init->op != N_BLOCK && init->op != N_DECL && expr_noreturn(c, x->a, reach, f))
			reach = false;
		else if (init && (init->op == N_BLOCK || init->op == N_DECL))
			reach = stmt_flow(c, x->a, reach, f);
		end = loop_body(c, body, reach && k != 0, f, &brk, &cont);
		again = end || cont;	/* the step runs */
		if (again && step && expr_noreturn(c, step, true, f))
			again = false;
		return ((reach || again) && k != 1) || brk;
	}
	case N_DO: {
		int k = cond_value(c, x->a);
		bool brk, cont, end = loop_body(c, x->b, reach, f, &brk, &cont);

		/* the condition: from the body's end or a continue */
		return ((end || cont) && k != 1) || brk;
	}
	case N_SWITCH: {
		bool ob = f->brk, os = f->sw, end, brk, dflt;

		if (expr_noreturn(c, x->a, reach, f))
			reach = false;
		dflt = has_default(c, x->b) || enum_covered(c, x);
		f->brk = false, f->sw = reach;
		end = stmt_flow(c, x->b, false, f);
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
   Only for the warning: it re-walks the subtrees with stmt_flow, quadratic in the nesting
   depth, on a body that warns. */
typedef struct {
	uint32_t b, e;		/* the step's range */
	const char *what;	/* its text */
	uint32_t tb, te;	/* the source in quotes (te > tb), else none */
} rstep;

typedef struct {
	flow *f;
	rstep st[12];
	int n;
	bool fail;		/* a goto or a label: no path given */
	const char *brk;	/* what a break leaves: "loop", "switch" */
} rpath;

static void rp_step(cc *c, rpath *P, uint32_t b, uint32_t e, const char *what, uint32_t q)
{
	rstep *s;

	if (P->n == (int)(sizeof P->st / sizeof P->st[0])) {
		P->fail = true;
		return;
	}
	s = &P->st[P->n++];
	s->b = b, s->e = e, s->what = what, s->tb = s->te = 0;
	if (q) {
		pp_span r = rng(c, q);

		s->tb = r.b, s->te = r.e;
	}
}

static bool end_of(cc *c, uint32_t n, bool reach, const flow *f, bool *brk)
{
	flow g = *f;

	g.brk = g.cont = g.ret = false;
	reach = stmt_flow(c, n, reach, &g);
	if (brk)
		*brk = g.brk;
	return reach;
}

static bool rp_walk(cc *c, uint32_t n, bool reach, bool brk, rpath *P);

/* statement i of a block entered: from the block's start or a case/label on the way */
static bool self_entry(cc *c, uint32_t n, const flow *f)
{
	const node *x = N(n);

	return ((x->op == N_CASE || x->op == N_DEFAULT) && f->sw) || (x->op == N_LABEL && label_reached(f, x->a));
}

/* the block's statements from the latest entry before i up to i, the last one to brk */
static bool rp_seq(cc *c, const node *x, uint32_t i, bool reach, bool brk, rpath *P)
{
	uint32_t j = i;
	uint8_t small[64], *end = i <= sizeof small ? small : malloc(i);
	bool r = reach;

	if (!end) {
		P->fail = true;
		return false;
	}
	for (uint32_t k = 0; k < i; k++)	/* each statement's end, once */
		end[k] = r = end_of(c, c->lists[x->b + k], r, P->f, NULL);
	/* back through statements whose end falls into the next, to an entry */
	while (j > 0 && !self_entry(c, c->lists[x->b + j], P->f) && end[j - 1])
		j--;
	if (end != small)
		free(end);
	r = j == 0 ? reach : false;
	for (uint32_t k = j; k <= i; k++) {
		if (!rp_walk(c, c->lists[x->b + k], r, k == i && brk, P))
			return false;
		r = true;
	}
	return true;
}

static bool rp_walk(cc *c, uint32_t n, bool reach, bool brk, rpath *P)
{
	const node *x;
	int mark = P->n, k;

	if (!n)
		return reach && !brk;
	if (P->fail)
		return false;
	x = N(n);
	switch (x->op) {
	case N_BLOCK: {
		bool in = reach;

		if (!x->c)
			return reach && !brk;
		if (!brk)
			return end_of(c, n, reach, P->f, NULL) && rp_seq(c, x, x->c - 1, reach, false, P);
		for (uint32_t i = 0; i < x->c; i++) {
			uint32_t s = c->lists[x->b + i];
			bool b;

			in = in || self_entry(c, s, P->f);
			end_of(c, s, in, P->f, &b);
			if (b) {
				if (rp_seq(c, x, i, reach, true, P))
					return true;
				P->n = mark;
			}
			in = end_of(c, s, in, P->f, NULL);
		}
		return false;
	}
	case N_LABEL:
		if (!reach && label_reached(P->f, x->a))
			P->fail = true;	/* reached by a goto */
		return rp_walk(c, x->c, true, brk, P);
	case N_CASE: case N_DEFAULT:
		if (!reach && P->f->sw)
			rp_step(c, P, x->loc, pp_tok_end(c->p, x->loc), x->op == N_CASE ? "the switch jumps to this case"
					      : "the switch jumps to 'default'", 0);
		return rp_walk(c, x->c, true, brk, P);
	case N_BREAK:
		if (!reach || !brk)
			return false;
		rp_step(c, P, x->loc, pp_tok_end(c->p, x->loc), P->brk && P->brk[0] == 's' ? "this 'break' leaves the switch"
				      : "this 'break' leaves the loop", 0);
		return true;
	case N_IF: {
		bool t, e, tb, eb;

		k = cond_value(c, x->a);
		if (!reach)
			return false;
		t = end_of(c, x->b, k != 0, P->f, &tb);
		e = x->c ? end_of(c, x->c, k != 1, P->f, &eb) : (eb = false, k != 1);
		if (!brk && t && e)
			return true;	/* both arms go on: this if decides nothing */
		if (k != 0 && (brk ? tb : t)) {
			if (k < 0)
				rp_step(c, P, 0, 0, "is true", x->a);
			if (rp_walk(c, x->b, true, brk, P))
				return true;
			P->n = mark;
		}
		if (k != 1 && (brk ? eb : e)) {
			if (k < 0)
				rp_step(c, P, 0, 0, "is false", x->a);
			if (x->c ? rp_walk(c, x->c, true, brk, P) : !brk)
				return true;
			P->n = mark;
		}
		return false;
	}
	case N_WHILE: case N_FOR: case N_DO: {
		uint32_t cnd = x->op == N_FOR ? x->b : x->a, body = x->op == N_FOR ? c->lists[x->c + 1] : x->b;
		const char *ob = P->brk;
		bool ok = false, b;

		if (brk || !reach)
			return false;	/* (a break inside is this loop's) */
		k = cond_value(c, cnd);
		if (x->op == N_FOR && x->a && (N(x->a)->op == N_BLOCK || N(x->a)->op == N_DECL) &&
		    !end_of(c, x->a, true, P->f, NULL))
			return false;
		if (k != 1 && x->op != N_DO) {	/* the first test fails */
			if (k < 0)
				rp_step(c, P, 0, 0, "is false, so the loop ends", cnd);
			return true;
		}
		if (k != 1 && rp_walk(c, body, true, false, P)) {	/* do: once round, then the test */
			if (k < 0)
				rp_step(c, P, 0, 0, "is false, so the loop ends", cnd);
			return true;
		}
		P->n = mark;
		end_of(c, body, true, P->f, &b);
		if (b) {
			P->brk = "loop";
			ok = rp_walk(c, body, true, true, P);
			P->brk = ob;
		}
		if (!ok)
			P->n = mark;
		return ok;
	}
	case N_SWITCH: {
		const char *ob = P->brk;
		bool os = P->f->sw, ok;

		if (brk || !reach)
			return false;
		if (!has_default(c, x->b) && !enum_covered(c, x)) {
			rp_step(c, P, 0, 0, "matches no case", x->a);
			return true;
		}
		P->f->sw = true, P->brk = "switch";
		ok = rp_walk(c, x->b, false, false, P);
		if (!ok) {
			P->n = mark;
			ok = rp_walk(c, x->b, false, true, P);
		}
		P->f->sw = os, P->brk = ob;
		if (!ok)
			P->n = mark;
		return ok;
	}
	case N_RETURN: case N_GOTO: case N_GOTO_IND: case N_CONTINUE:
		return false;
	default:
		return !brk && end_of(c, n, reach, P->f, NULL);
	}
}

static int rstep_cmp(const void *a, const void *b)
{
	uint32_t x = ((const rstep *)a)->tb ? ((const rstep *)a)->tb : ((const rstep *)a)->b;
	uint32_t y = ((const rstep *)b)->tb ? ((const rstep *)b)->tb : ((const rstep *)b)->b;

	return x < y ? -1 : x > y;
}

/* the steps as labels "(1) 'x > 0' is false", ..., the last at the closing brace */
static void return_path(cc *c, uint32_t body, flow *f, vdiag *d, uint32_t rbrace, const sym *y)
{
	rpath P = { .f = f };
	char buf[160];
	int i;

	f->sw = false;
	if (!rp_walk(c, body, true, false, &P) || P.fail || !P.n)
		return;
	qsort(P.st, (size_t)P.n, sizeof P.st[0], rstep_cmp);
	for (i = 0; i < P.n; i++) {
		const rstep *s = &P.st[i];

		if (s->te > s->tb) {
			size_t len;
			const char *t = pp_src(c->p, s->tb, s->te, &len);

			if (t && len <= 48 && !memchr(t, '\n', len))
				snprintf(buf, sizeof buf, "(%d) '%.*s' %s", i + 1, (int)len, t, s->what);
			else
				snprintf(buf, sizeof buf, "(%d) the condition %s", i + 1, s->what);
			pp_diag_label(c->p, d, s->tb, s->te, buf);
		} else {
			snprintf(buf, sizeof buf, "(%d) %s", i + 1, s->what);
			pp_diag_label(c->p, d, s->b, s->e, buf);
		}
	}
	snprintf(buf, sizeof buf, "(%d) '%.*s' ends here without a return", i + 1, IDF(y->name));
	pp_diag_label(c->p, d, rbrace, rbrace + 1, buf);
}

/* clang's -Wreturn-type at the body's closing brace (flow.c decides: k 1 "in all control
   paths", 2 no return reached); with returns on some paths, one path that has none is
   shown as numbered steps */
VPRIV __attribute__((noinline)) void return_type_warn(cc *c, uint32_t s, uint32_t body, uint32_t rbrace,
							uint32_t lab0, int k)
{
	const sym *y = &c->syms[s];
	uint8_t small[64];
	flow f = { 0 };
	bool end = false;
	vdiag *d;

	d = cc_warn_begin(c, "return-type", true, NULL, rbrace, k == 1 ? "non-void function does not return a value in all control paths"
			  : "non-void function does not return a value");
	if (!d)
		return;
	f.lab0 = lab0, f.nlab = c->nlabel - lab0;
	f.lab = f.nlab <= sizeof small ? small : calloc(f.nlab, 1);
	memset(f.lab, 0, f.nlab <= sizeof small ? f.nlab : 0);
	if (k == 1) {
		do {	/* again while a goto reaches a new label (each pass reaches more) */
			f.more = f.ret = f.brk = f.cont = f.sw = false;
			end = stmt_flow(c, body, true, &f);
		} while (f.more && !f.unsure);
		if (end && !f.unsure && f.ret)	/* some paths return: show one that does not */
			return_path(c, body, &f, d, rbrace, y);
	}
	pp_diag_help(c->p, d, "'%.*s' returns '%s': end it with a return statement", IDF(y->name),
		     tyname(c, T(c, y->ty)->base));
	pp_diag_emit(c->p, d);
	if (f.lab != small)
		free(f.lab);
}
