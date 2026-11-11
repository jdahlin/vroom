/* Bounded wasm inlining for static scalar functions with a single pure return.
 * Each argument is evaluated once, before temporarily binding parameters to
 * typed wasm locals. Unknown expression forms and exhausted budgets fall back
 * to the normal call path. */
#include "wimpl.h"

#define WINLINE_PARAMS 8
#define WINLINE_NODES 48

static bool winline_expr(cc *c, uint32_t n, const uint32_t *params, uint32_t np, unsigned *budget)
{
	const node *x;
	int t;

	if (!n || !*budget) return false;
	--*budget;
	x = N(n);
	t = wty(c, x->ty);
	if (t == WT_NONE || t == WT_MEM || is_vec(c, x->ty) || is_record(c, x->ty)) return false;
	switch (x->op) {
	case N_NUM: case N_FNUM: case N_ZERO:
		return true;
	case N_VAR:
		if (c->syms[x->a].kind == S_ENUMCONST) return true;
		for (uint32_t i = 0; i < np; i++) if (params[i] == x->a) return true;
		return false;
	case N_CAST: case N_UNARY:
		return winline_expr(c, x->a, params, np, budget);
	case N_BINARY: case N_LOGAND: case N_LOGOR:
		return winline_expr(c, x->a, params, np, budget) &&
		       winline_expr(c, x->b, params, np, budget);
	case N_COND:
		return x->b && winline_expr(c, x->a, params, np, budget) &&
		       winline_expr(c, x->b, params, np, budget) &&
		       winline_expr(c, x->c, params, np, budget);
	default:
		return false;
	}
}

/* Returns -1 when the call must follow the normal path, else its wasm type. */
VPRIV int winline_call(ecx *e, uint32_t n, uint32_t s, bool want)
{
	cc *c = e->c;
	wst *W = e->w;
	const node *call = N(n), *body, *ret;
	const sym *y = &c->syms[s];
	const ctype *ft = T(c, y->ty);
	uint32_t params[WINLINE_PARAMS], temps[WINLINE_PARAMS];
	wbind saved[WINLINE_PARAMS];
	unsigned budget = WINLINE_NODES;
	int result;

	if (!e->E->o->opt || e->E->noopt || y->sc != SC_STATIC ||
	    !(y->flags & SF_DEFINED) || (y->flags & SF_NOINLINE) || !y->body ||
	    (ft->flags & (TF_VARIADIC | TF_NOPROTO))) return -1;
	body = N(y->body);
	if (body->op != N_BLOCK || body->c != 1 || body->aux > WINLINE_PARAMS ||
	    call->c != body->aux || ft->n != body->aux) return -1;
	ret = N(c->lists[body->b]);
	result = wty(c, ft->base);
	if (ret->op != N_RETURN || !ret->a || result == WT_NONE || result == WT_MEM ||
	    is_vec(c, ft->base) || is_record(c, ft->base)) return -1;
	for (uint32_t i = 0; i < body->aux; i++) {
		uint32_t p = c->lists[body->a + i];
		const sym *v = &c->syms[p];
		int t = wty(c, v->ty);
		if (t == WT_NONE || t == WT_MEM || is_vec(c, v->ty) || is_record(c, v->ty) ||
		    (v->flags & SF_ADDR_TAKEN) || (QQ(v->ty) & Q_VOLATILE) ||
		    (T(c, v->ty)->flags & TF_ATOMIC) || sym_attrs(c, p)->cleanup) return -1;
		params[i] = p;
	}
	if (!winline_expr(c, ret->a, params, body->aux, &budget)) return -1;
	for (uint32_t i = 0; i < body->aux; i++) {
		uint32_t arg = c->lists[call->b + i];
		qty pt = c->syms[params[i]].ty;
		temps[i] = wtmp(e, wty(c, pt));
		wexpr(e, arg, true);
		if (QUNQ(N(arg)->ty) != QUNQ(pt)) wconv(e, N(arg)->ty, pt);
		wset(e, temps[i]);
	}
	for (uint32_t i = 0; i < body->aux; i++) {
		saved[i] = W->sb[params[i]];
		W->sb[params[i]] = (wbind){ .gen = W->gen, .k = WB_LOC, .loc = temps[i] };
	}
	wexpr(e, ret->a, true);
	if (QUNQ(N(ret->a)->ty) != QUNQ(ft->base)) wconv(e, N(ret->a)->ty, ft->base);
	for (uint32_t i = 0; i < body->aux; i++) {
		W->sb[params[i]] = saved[i];
		wfree(e, temps[i]);
	}
	if (!want) wi(e, "drop");
	return want ? result : WT_NONE;
}
