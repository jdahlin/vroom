/* vcc front end: -Wuninitialized and -Wsometimes-uninitialized, a dataflow over the statement tree */
#include "cc-internal.h"

/*
 * -Wuninitialized (clang's UninitializedValues, its definite case only):
 * per tracked variable (a local scalar or vector) two bits, "may be
 * uninitialized" and "may be initialized", over the statement tree as
 * above: loops to a fixpoint, labels by a fixpoint over the gotos.  A read
 * where the variable can only be uninitialized is reported, the first per
 * variable; a "sometimes" one (-Wsometimes-uninitialized) is not.
 */
typedef struct cc_uninitialized_state { uint64_t u, i; bool live; } cc_uninitialized_state;

typedef struct cc_uninitialized_walk {
	cc *c;
	cc_reach_state fl;			/* for cc_reach_condition_constant / cc_reach_case_matches */
	uint32_t var[64], nv;
	uint32_t hs[128];	/* sym + 1 -> index + 1 */
	uint8_t hi[128];
	cc_uninitialized_state *lab;
	uint8_t *seen;		/* label reached in this pass: a goto to it is backward */
	uint32_t first, nlab;
	bool changed, report;
	cc_uninitialized_state *brk, *cont, *swin;
	cc_uninitialized_state *dobrk;		/* the exit of the do-while(0) being walked (no back edge: a block) */
	uint64_t dcm;		/* ... variables an if's break arm leaves uninitialized there */
	uint32_t dcif[64];	/* ... that if (one per variable; dcn counts them) */
	uint8_t dcf[64], dcn[64];
	uint32_t dcl[64][8];	/* ... all of them (dcn of them, up to 8), with their dcf */
	uint8_t dclf[64][8];
	bool sw_const, sw_hit;
	i128 sw_val;
	uint32_t self;		/* the variable whose initializer is walked */
	bool selfhit;
	uint32_t selfloc;
	diag_span selfr;
	uint64_t done;
	uint64_t spend;		/* -Wsometimes-uninitialized: an if left the variable initialized on one arm only */
	uint64_t sdone;		/* ... reported (a definite report replaces it) */
	uint32_t nord, nself;
	uint32_t dep;		/* branch depth of the walk (if arms, ?:, && ||, loop and switch bodies) */
	uint32_t blk;		/* the CFG block: clang builds the CFG backwards and reports by block */
	/* reports (not cleared) */
	uint32_t uloc[64], order[64], ublk[64];
	uint32_t sif[64];	/* the if */
	uint8_t sfalse[64];	/* ... uninitialized when its condition is false */
	uint8_t skind[64];	/* (7: the ifs breaking out of one do-while(0): dl, dlf, ndl) */
	uint32_t dl[64][8];
	uint8_t dlf[64][8], ndl[64];
	diag_span skr[64];	/* ... a case: its value / default's range */
	/* the switch being walked: the case arm entered from it only, and what it leaves uninitialized */
	cc_uninitialized_state *swbrk;
	uint32_t swlab[8], nswlab, swarm;	/* the arm's labels (stacked: case A: case CC_COMPLEX_BINARY:), its number */
	uint64_t swmask, swcand;
	uint32_t swc[64][8];	/* per variable: the labels whose edge leaves it uninitialized */
	uint8_t swa[64][8], nswc[64];	/* ... 0 if, 1 &&, 2 ||, 3 ?:, 4 case, 5 default */
	uint32_t sdep[64];
	cc_uninitialized_state lp_head, lp_cx, lp_brk;	/* the last loop walked: its head at the fixpoint, its exits */	/* ... at this branch depth: the use must post-dominate it */
	uint8_t kind[64];	/* report: 1 definite, 2 sometimes */
	diag_span ur[64];
	uint32_t sself[16], sloc[16];	/* self-initializations */
	diag_span sr[16];
	/* AST_TYPE x = x (exactly): x stays uninitialized; a later read reports the initialization (clang) */
	uint64_t selfv;
	uint32_t xloc[64], xsym[64];
	diag_span xr[64];
} cc_uninitialized_walk;

/* the condition can be a constant (cc_reach_condition_constant's cases): most are not, and cc_eval is not free */
static bool cc_uninitialized_condition_may_be_constant(cc *c, uint32_t n)
{
	const node *x;

	if (!n)
		return true;
	x = AST_NODE(n);
	switch (x->op) {
	case AST_NODE_NUMBER: case AST_NODE_FLOAT_NUMBER: case AST_NODE_STRING: case AST_NODE_LABEL_ADDRESS:
		return true;
	case AST_NODE_VARIABLE:
		return (AST_QUALIFIED_TYPE_QUALIFIERS(c->syms[x->a].ty) & AST_QUALIFIER_CONST) || AST_TYPE_KIND(c, x->ty) == AST_TYPE_ARRAY || AST_TYPE_KIND(c, x->ty) == AST_TYPE_FUNCTION;
	case AST_NODE_ADDRESS:
		return true;	/* (&global, &x->f) */
	case AST_NODE_CAST: case AST_NODE_UNARY:
		return cc_uninitialized_condition_may_be_constant(c, x->a);
	case AST_NODE_BINARY: case AST_NODE_COMMA:
		return cc_uninitialized_condition_may_be_constant(c, x->a) && cc_uninitialized_condition_may_be_constant(c, x->b);
	case AST_NODE_LOGICAL_AND: case AST_NODE_LOGICAL_OR:
		return cc_uninitialized_condition_may_be_constant(c, x->a) || cc_uninitialized_condition_may_be_constant(c, x->b);
	case AST_NODE_CONDITIONAL:
		return cc_uninitialized_condition_may_be_constant(c, x->a) || (cc_uninitialized_condition_may_be_constant(c, x->b) && cc_uninitialized_condition_may_be_constant(c, x->c));
	case AST_NODE_BUILTIN:
		return true;
	default:
		return false;
	}
}

static int cc_uninitialized_condition_constant(cc_uninitialized_walk *w, uint32_t n)
{
	return cc_uninitialized_condition_may_be_constant(w->c, n) ? cc_reach_condition_constant(&w->fl, n) : -1;
}

/* && || ?: in a condition: clang names that operator's edge instead */
static bool cc_uninitialized_condition_has_logic(cc *c, uint32_t n)
{
	while (n && (AST_NODE(n)->op == AST_NODE_CAST || AST_NODE(n)->op == AST_NODE_UNARY))
		n = AST_NODE(n)->a;
	if (!n)
		return false;
	switch (AST_NODE(n)->op) {
	case AST_NODE_LOGICAL_AND: case AST_NODE_LOGICAL_OR: case AST_NODE_CONDITIONAL:
		return true;
	case AST_NODE_BINARY: case AST_NODE_COMMA: case AST_NODE_ASSIGN:
		return cc_uninitialized_condition_has_logic(c, AST_NODE(n)->a) || cc_uninitialized_condition_has_logic(c, AST_NODE(n)->b);
	default:
		return false;
	}
}

/* a branch at node n (kind as skind) left the variables uninitialized before it initialized on path a only, uninitialized on path b */
static void cc_uninitialized_pending_add(cc_uninitialized_walk *w, uint32_t n, int kind, const cc_uninitialized_state *b4, const cc_uninitialized_state *a, const cc_uninitialized_state *b, bool f)
{
	uint64_t m;

	if (!b4->live || !a->live || !b->live)
		return;
	m = (b4->u & ~b4->i) & (a->i & ~a->u) & (b->u & ~b->i);
	for (; m; m &= m - 1) {
		int v = __builtin_ctzll(m);

		w->spend |= 1ull << v, w->sif[v] = n, w->sfalse[v] = f, w->sdep[v] = w->dep, w->skind[v] = (uint8_t)kind;
	}
}

/* a case arm reaches the switch's end (break, or the end of the body) with st */
static void cc_uninitialized_switch_arm(cc_uninitialized_walk *w, const cc_uninitialized_state *st)
{
	if (!w->nswlab || !st->live)
		return;
	for (uint64_t m = w->swmask & st->u & ~st->i; m; m &= m - 1) {
		int v = __builtin_ctzll(m);

		if (!(w->swcand & (1ull << v)))
			w->nswc[v] = 0;
		w->swcand |= 1ull << v;
		for (uint32_t i = 0; i < w->nswlab && w->nswc[v] < 8; i++) {
			bool dup = false;

			for (uint32_t j = 0; j < w->nswc[v]; j++)
				dup |= w->swc[v][j] == w->swlab[i];
			if (!dup)
				w->swa[v][w->nswc[v]] = (uint8_t)w->swarm, w->swc[v][w->nswc[v]++] = w->swlab[i];
		}
	}
}

/* a case label's value, default's keyword: the warning's range */
static diag_span cc_uninitialized_case_range(cc *c, uint32_t n)
{
	uint32_t b = AST_NODE(n)->loc;
	size_t len = 0;
	const char *t = NULL;
	size_t i = 0, e;

	for (uint32_t n = 256; !t && n >= 8; n /= 2)	/* (near the end of the file) */
		t = pp_source_text(c->p, b, b + n, &len);

	if (AST_NODE(n)->op == AST_NODE_DEFAULT || !t)
		return (diag_span){ b, b + 7 };
	if (len > 4 && !memcmp(t, "case", 4))
		i = 4;
	while (i < len && (t[i] == ' ' || t[i] == '\t'))
		i++;
	for (e = i; e < len && t[e] != ':' && t[e] != '\n'; e++)
		;
	while (e > i && (t[e - 1] == ' ' || t[e - 1] == '\t'))
		e--;
	return (diag_span){ b + (uint32_t)i, b + (uint32_t)e };
}

/* leaving a branch: pending ifs inside it do not dominate what follows */
static inline void cc_uninitialized_pending_drop(cc_uninitialized_walk *w)
{
	for (uint64_t m = w->spend; m; m &= m - 1) {
		int v = __builtin_ctzll(m);

		if (w->sdep[v] > w->dep)
			w->spend &= ~(1ull << v);
	}
}

static inline void cc_uninitialized_join(cc_uninitialized_state *a, const cc_uninitialized_state *b)
{
	if (!b->live)
		return;
	if (!a->live) {
		*a = *b;
		return;
	}
	a->u |= b->u, a->i |= b->i;
}

static int cc_uninitialized_variable_index(cc_uninitialized_walk *w, uint32_t s)
{
	uint32_t h = (s * 2654435761u) >> 25;

	for (;; h = (h + 1) & 127) {
		if (!w->hs[h])
			return -1;
		if (w->hs[h] == s + 1)
			return w->hi[h] - 1;
	}
}

static int cc_uninitialized_variable_add(cc_uninitialized_walk *w, uint32_t s)
{
	uint32_t h = (s * 2654435761u) >> 25;
	int k = cc_uninitialized_variable_index(w, s);

	if (k >= 0 || w->nv >= 64)
		return k;
	while (w->hs[h])
		h = (h + 1) & 127;
	w->hs[h] = s + 1, w->hi[h] = (uint8_t)(w->nv + 1);
	w->var[w->nv] = s;
	return (int)w->nv++;
}

static void cc_uninitialized_read(cc_uninitialized_walk *w, uint32_t n, cc_uninitialized_state *st)
{
	cc *c = w->c;
	int k;
	uint64_t b;

	if (!st->live)
		return;
	if (w->self && AST_NODE(n)->a == w->self && !w->selfhit) {
		w->selfhit = true, w->selfloc = AST_NODE(n)->loc, w->selfr = cc_node_range_bare(w->c, n);
		return;
	}
	if ((k = cc_uninitialized_variable_index(w, AST_NODE(n)->a)) < 0)
		return;
	b = 1ull << k;
	if (!w->report || !(st->u & b))
		return;
	if (!(st->i & b) && (!(w->done & b) || w->kind[k] == 2)) {
		if (!(w->done & b))
			w->order[w->nord++] = (uint32_t)k;
		w->done |= b, w->kind[k] = 1;
		w->uloc[k] = AST_NODE(n)->loc, w->ur[k] = cc_node_range_bare(w->c, n), w->ublk[k] = w->blk;
	} else if ((st->i & b) && (w->spend & b) && w->sdep[k] == w->dep && !(w->done & b)) {
		w->done |= b, w->kind[k] = 2;
		w->uloc[k] = AST_NODE(n)->loc, w->ur[k] = cc_node_range_bare(w->c, n), w->ublk[k] = w->blk;
		w->order[w->nord++] = (uint32_t)k;
	}
}

static void cc_uninitialized_write(cc_uninitialized_walk *w, uint32_t n, cc_uninitialized_state *st)
{
	cc *c = w->c;
	int k = cc_uninitialized_variable_index(w, AST_NODE(n)->a);

	if (k >= 0)
		st->u &= ~(1ull << k), st->i |= 1ull << k, w->spend &= ~(1ull << k), w->swmask &= ~(1ull << k);
}

static void cc_uninitialized_expression(cc_uninitialized_walk *w, uint32_t e, cc_uninitialized_state *st, bool ignore);

/* an assigned operand: a variable is not read, anything else is */
static void cc_uninitialized_lvalue(cc_uninitialized_walk *w, uint32_t e, cc_uninitialized_state *st)
{
	cc *c = w->c;

	while (e && (AST_NODE(e)->op == AST_NODE_MEMBER || AST_NODE(e)->op == AST_NODE_UNARY))	/* (a member, __real/__imag) */
		e = AST_NODE(e)->a;
	if (e && AST_NODE(e)->op != AST_NODE_VARIABLE)
		cc_uninitialized_expression(w, e, st, false);
}

static void cc_uninitialized_statement(cc_uninitialized_walk *w, uint32_t n, cc_uninitialized_state *st);

static void cc_uninitialized_expression(cc_uninitialized_walk *w, uint32_t e, cc_uninitialized_state *st, bool ignore)
{
	cc *c = w->c;
	const node *x;

	if (!e)
		return;
	x = AST_NODE(e);
	switch (x->op) {
	case AST_NODE_VARIABLE:
		if (!ignore)
			cc_uninitialized_read(w, e, st);
		return;
	case AST_NODE_ASSIGN:
		if (AST_NODE(x->a)->op == AST_NODE_VARIABLE) {
			if (x->aux)
				cc_uninitialized_read(w, x->a, st);
			cc_uninitialized_expression(w, x->b, st, false);
			cc_uninitialized_write(w, x->a, st);
		} else {
			uint32_t v = x->a;

			/* __real/__imag of a _Complex variable: initializes it (clang) */
			while (AST_NODE(v)->op == AST_NODE_MEMBER || AST_NODE(v)->op == AST_NODE_UNARY)
				v = AST_NODE(v)->a;
			if (x->aux)
				cc_uninitialized_expression(w, x->a, st, false);
			cc_uninitialized_expression(w, x->b, st, false);
			if (!x->aux)
				cc_uninitialized_lvalue(w, x->a, st);
			if (AST_NODE(v)->op == AST_NODE_VARIABLE)
				cc_uninitialized_write(w, v, st);
		}
		return;
	case AST_NODE_PRE_INCREMENT: case AST_NODE_PRE_DECREMENT: case AST_NODE_POST_INCREMENT: case AST_NODE_POST_DECREMENT:
		cc_uninitialized_expression(w, x->a, st, false);
		if (AST_NODE(x->a)->op == AST_NODE_VARIABLE)
			cc_uninitialized_write(w, x->a, st);
		return;
	case AST_NODE_ADDRESS: {
		uint32_t v = x->a;

		while (AST_NODE(v)->op == AST_NODE_MEMBER || AST_NODE(v)->op == AST_NODE_UNARY)
			v = AST_NODE(v)->a;
		if (AST_NODE(v)->op == AST_NODE_VARIABLE)
			cc_uninitialized_write(w, v, st);	/* (escapes: initialized from here on) */
		else
			cc_uninitialized_lvalue(w, x->a, st);
		return;
	}
	case AST_NODE_CAST:
		cc_uninitialized_expression(w, x->a, st, x->aux == AST_CAST_VOID);
		return;
	case AST_NODE_CALL:
		cc_uninitialized_expression(w, x->a, st, false);
		for (uint32_t i = 0; i < x->c; i++) {
			uint32_t a = c->lists[x->b + i];

			/* &v for a pointer-to-const parameter: neither a use nor an initialization */
			if (AST_NODE(a)->op == AST_NODE_CAST && AST_NODE(AST_NODE(a)->a)->op == AST_NODE_ADDRESS && AST_NODE(AST_NODE(AST_NODE(a)->a)->a)->op == AST_NODE_VARIABLE &&
			    AST_TYPE_KIND(c, AST_NODE(a)->ty) == AST_TYPE_POINTER && (AST_QUALIFIED_TYPE_QUALIFIERS(AST_TYPE(c, AST_NODE(a)->ty)->base) & AST_QUALIFIER_CONST))
				continue;
			cc_uninitialized_expression(w, a, st, false);
		}
		if (cc_reach_is_noreturn(&w->fl, e))
			st->live = false;
		return;
	case AST_NODE_BUILTIN:
		for (uint32_t i = 0; i < x->c; i++) {
			uint32_t a = c->lists[x->b + i];

			if (i == 0 && (x->aux == CC_BUILTIN_VA_START || x->aux == CC_BUILTIN_VA_COPY)) {
				while (AST_NODE(a)->op == AST_NODE_CAST || AST_NODE(a)->op == AST_NODE_ADDRESS)
					a = AST_NODE(a)->a;
				if (AST_NODE(a)->op == AST_NODE_VARIABLE) {
					cc_uninitialized_write(w, a, st);	/* (va_start/va_copy initialize their va_list) */
					continue;
				}
			}
			cc_uninitialized_expression(w, a, st, false);
		}
		if (cc_reach_is_noreturn(&w->fl, e))
			st->live = false;
		return;
	case AST_NODE_CONDITIONAL: {
		int k = cc_uninitialized_condition_constant(w, x->a);
		cc_uninitialized_state t, f;

		cc_uninitialized_expression(w, x->a, st, false);
		t = *st, f = *st;
		t.live &= k != 0, f.live &= k != 1;
		cc_uninitialized_state b4 = *st;

		w->blk++, w->dep++;
		if (x->b)
			cc_uninitialized_expression(w, x->b, &t, ignore);
		w->blk++;
		cc_uninitialized_expression(w, x->c, &f, ignore);
		w->blk++, w->dep--, cc_uninitialized_pending_drop(w);
		if (k < 0 && x->b) {
			cc_uninitialized_pending_add(w, e, 3, &b4, &t, &f, true);
			cc_uninitialized_pending_add(w, e, 3, &b4, &f, &t, false);
		}
		*st = (cc_uninitialized_state){ 0, 0, false };
		cc_uninitialized_join(st, &t), cc_uninitialized_join(st, &f);
		return;
	}
	case AST_NODE_LOGICAL_AND: case AST_NODE_LOGICAL_OR: {
		int k = cc_uninitialized_condition_constant(w, x->a);
		bool and = x->op == AST_NODE_LOGICAL_AND;
		cc_uninitialized_state r, s;

		cc_uninitialized_expression(w, x->a, st, false);
		r = *st, s = *st;
		r.live &= and ? k != 0 : k != 1;	/* b evaluated */
		s.live &= and ? k != 1 : k != 0;	/* short circuit */
		cc_uninitialized_state b4 = r;

		w->blk++, w->dep++;
		cc_uninitialized_expression(w, x->b, &r, false);
		w->blk++, w->dep--, cc_uninitialized_pending_drop(w);
		if (k < 0)
			cc_uninitialized_pending_add(w, e, and ? 1 : 2, &b4, &r, &s, and);
		*st = s;
		cc_uninitialized_join(st, &r);
		return;
	}
	case AST_NODE_COMMA:
		cc_uninitialized_expression(w, x->a, st, true);
		cc_uninitialized_expression(w, x->b, st, ignore);
		return;
	case AST_NODE_BINARY: case AST_NODE_COMPLEX:
		cc_uninitialized_expression(w, x->a, st, false);
		cc_uninitialized_expression(w, x->b, st, false);
		return;
	case AST_NODE_INDEX:
		cc_uninitialized_expression(w, x->a, st, false);
		cc_uninitialized_expression(w, x->b, st, false);
		return;
	case AST_NODE_UNARY: case AST_NODE_DEREFERENCE: case AST_NODE_VA_ARG:
		cc_uninitialized_expression(w, x->a, st, false);
		return;
	case AST_NODE_MEMBER:
		if (AST_NODE(x->a)->op == AST_NODE_VARIABLE && cc_type_is_record(c, AST_NODE(x->a)->ty) && !cc_type_is_complex(c, AST_NODE(x->a)->ty))
			cc_uninitialized_write(w, x->a, st);	/* s.m of a struct variable: no use, and initialized from here on (clang) */
		else
			cc_uninitialized_expression(w, x->a, st, false);	/* (a _Complex part is a use) */
		return;
	case AST_NODE_STATEMENT_EXPRESSION:
		cc_uninitialized_statement(w, x->a, st);
		return;
	case AST_NODE_COMPOUND_LITERAL:
		cc_uninitialized_expression(w, x->a, st, false);
		return;
	case AST_NODE_INITIALIZER:
		if (x->aux == 1)
			for (uint32_t i = 0; i < x->c; i++)
				cc_uninitialized_expression(w, c->lists[x->b + 3 * i + 2], st, false);
		else
			for (uint32_t i = 0; i < x->c; i++)
				cc_uninitialized_expression(w, c->lists[x->b + i], st, false);
		return;
	default:
		return;
	}
}

static void cc_uninitialized_goto(cc_uninitialized_walk *w, uint32_t l, const cc_uninitialized_state *st)
{
	cc_uninitialized_state *d;
	cc_uninitialized_state o;

	if (!st->live || l < w->first || l - w->first >= w->nlab)
		return;
	d = &w->lab[l - w->first], o = *d;
	cc_uninitialized_join(d, st);
	if (w->seen[l - w->first] && (d->live != o.live || d->u != o.u || d->i != o.i))
		w->changed = true;	/* (a forward goto is joined when the label is reached) */
}

/* a loop: body (and cond, step) to a fixpoint without reports, then once more */
static void cc_uninitialized_loop(cc_uninitialized_walk *w, const node *x, cc_uninitialized_state *st)
{
	cc *c = w->c;
	cc_uninitialized_state in = *st, head = in, out = { 0, 0, false };
	bool rep = w->report;
	cc_uninitialized_state *sb = w->brk, *sc = w->cont;
	uint32_t cond = x->op == AST_NODE_FOR ? x->b : x->a;
	uint32_t body = x->op == AST_NODE_FOR ? c->lists[x->c + 1] : x->b;
	uint32_t step = x->op == AST_NODE_FOR ? c->lists[x->c] : 0;
	int k = cond || x->op != AST_NODE_FOR ? cc_uninitialized_condition_constant(w, cond) : 1;

	uint32_t nord0 = w->nord, nself0 = w->nself;
	uint64_t done0 = w->done;

	/* the first pass reports; kept if it is already the fixpoint */
	for (int it = 0;; it++) {
		cc_uninitialized_state s = head, brk = { 0, 0, false }, cont = { 0, 0, false }, back, nh;
		uint32_t b0 = w->blk;
		bool last = (w->report && it) || it >= 8;

		out = (cc_uninitialized_state){ 0, 0, false };
		w->brk = &brk, w->cont = &cont;
		if (x->op == AST_NODE_DO) {
			cc_uninitialized_statement(w, body, &s);
			cc_uninitialized_join(&s, &cont);
			cc_uninitialized_expression(w, cond, &s, false);
			back = s, back.live &= k != 0;
			out = s, out.live &= k != 1;
		} else {
			if (cond)
				cc_uninitialized_expression(w, cond, &s, false);
			out = s, out.live &= k != 1;
			s.live &= k != 0;
			cc_uninitialized_statement(w, body, &s);
			cc_uninitialized_join(&s, &cont);
			if (step && AST_NODE(step)->op == AST_NODE_STATEMENT_EXPRESSION)
				cc_uninitialized_statement(w, AST_NODE(step)->a, &s);
			else if (step)
				cc_uninitialized_expression(w, step, &s, true);
			back = s;
		}
		w->lp_head = head, w->lp_cx = out, w->lp_brk = brk;
		cc_uninitialized_join(&out, &brk);
		w->brk = sb, w->cont = sc;
		if (last)
			break;
		w->blk = b0;
		nh = in;
		cc_uninitialized_join(&nh, &back);
		if (nh.live == head.live && nh.u == head.u && nh.i == head.i) {
			if (!rep || w->report)
				break;
			w->report = true;	/* the reporting pass at the fixpoint */
			continue;
		}
		if (w->report)
			w->nord = nord0, w->nself = nself0, w->done = done0, w->report = false;
		head = nh;
	}
	w->report = rep;
	*st = out;
}

static void cc_uninitialized_statement(cc_uninitialized_walk *w, uint32_t n, cc_uninitialized_state *st)
{
	cc *c = w->c;
	const node *x;

	if (!n)
		return;
	x = AST_NODE(n);
	if (x->op < AST_NODE_BLOCK) {
		cc_uninitialized_expression(w, n, st, true);
		return;
	}
	switch (x->op) {
	case AST_NODE_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			cc_uninitialized_statement(w, c->lists[x->b + i], st);
		return;
	case AST_NODE_EXPRESSION:
		cc_uninitialized_expression(w, x->a, st, false);	/* ("x;" is a use) */
		if (cc_reach_is_noreturn(&w->fl, x->a))
			st->live = false;
		return;
	case AST_NODE_DECLARATION: {
		int k = cc_uninitialized_variable_index(w, x->a);

		if (!x->b) {
			if (k >= 0 && st->live)
				st->u |= 1ull << k, st->i &= ~(1ull << k);
			return;
		}
		{
			uint32_t b0 = x->b;

			while (AST_NODE(b0)->op == AST_NODE_CAST && AST_NODE(b0)->aux == AST_CAST_NOP)
				b0 = AST_NODE(b0)->a;
			if (AST_NODE(b0)->op == AST_NODE_VARIABLE && AST_NODE(b0)->a == x->a && k < 0 && cc_uninitialized_is_tracked(c, c->syms[x->a].ty))
				k = cc_uninitialized_variable_add(w, x->a);	/* (tracked from here: a later read is the report) */
			if (k >= 0 && AST_NODE(b0)->op == AST_NODE_VARIABLE && AST_NODE(b0)->a == x->a) {
				if (st->live)
					st->u |= 1ull << k, st->i &= ~(1ull << k);
				w->selfv |= 1ull << k, w->xsym[k] = x->a, w->xloc[k] = AST_NODE(b0)->loc, w->xr[k] = cc_node_range_bare(c, b0);
				return;
			}
		}
		if (k >= 0)
			st->u &= ~(1ull << k), st->i |= 1ull << k;
		{
			uint32_t ss = w->self;
			bool sh = w->selfhit;

			w->self = cc_uninitialized_is_tracked(c, c->syms[x->a].ty) ? x->a : 0, w->selfhit = false;
			cc_uninitialized_expression(w, x->b, st, false);
			if (w->selfhit && w->report && st->live && w->nself < 16) {
				w->sself[w->nself] = x->a, w->sloc[w->nself] = w->selfloc;
				w->sr[w->nself++] = w->selfr;
			}
			w->self = ss, w->selfhit = sh;
		}
		return;
	}
	case AST_NODE_RETURN:
		cc_uninitialized_expression(w, x->a, st, false);
		st->live = false;
		return;
	case AST_NODE_IF: {
		int k = cc_uninitialized_condition_constant(w, x->a);
		cc_uninitialized_state t, e;

		cc_uninitialized_expression(w, x->a, st, false);
		t = *st, e = *st;
		t.live &= k != 0, e.live &= k != 1;
		cc_uninitialized_state b4 = *st, tb = { 0, 0, false }, eb = { 0, 0, false }, *ob = w->brk, *oc = w->cont;
		bool db = ob && ob == w->dobrk;	/* a break out of a do-while(0) still reaches what follows it */

		w->blk++, w->dep++;
		if (db)
			w->brk = w->dobrk = &tb, w->cont = oc == ob ? &tb : oc;
		cc_uninitialized_statement(w, x->b, &t);
		w->blk++;
		if (db)
			w->brk = w->dobrk = &eb, w->cont = oc == ob ? &eb : oc;
		cc_uninitialized_statement(w, x->c, &e);
		w->blk++, w->dep--, cc_uninitialized_pending_drop(w);
		if (db) {
			w->brk = w->dobrk = ob, w->cont = oc;
			cc_uninitialized_join(ob, &tb), cc_uninitialized_join(ob, &eb);
			/* an arm that leaves by break while the other falls through: decided after the do */
			if (b4.live && k < 0 && !cc_uninitialized_condition_has_logic(c, x->a) && (tb.live != eb.live) &&
			    (tb.live ? !t.live && e.live : !e.live && t.live)) {
				const cc_uninitialized_state *a = tb.live ? &tb : &eb;

				const cc_uninitialized_state *o = tb.live ? &e : &t;	/* the arm that falls through */

				/* uninitialized on one of the two edges so far; the fall-through may still change */
				for (uint64_t m = b4.u & ~b4.i & (((a->u & ~a->i) & (o->u | o->i)) | ((a->i & ~a->u) & o->u)); m; m &= m - 1) {
					int v = __builtin_ctzll(m);

					if (!(w->dcm >> v & 1))
						w->dcn[v] = 0;
					w->dcm |= 1ull << v, w->dcif[v] = n, w->dcf[v] = (uint8_t)(!tb.live | (a->u >> v & 1 ? 0 : 2));
					if (w->dcn[v] < 8)
						w->dcl[v][w->dcn[v]] = n, w->dclf[v][w->dcn[v]] = w->dcf[v];
					if (w->dcn[v] < 255)
						w->dcn[v]++;
				}
			}
		}
		if (b4.live && (!t.live || !e.live))
			w->spend = 0;	/* (a path leaves: clang finds no branch that always reaches the use) */
		else if (b4.live && k < 0 && !cc_uninitialized_condition_has_logic(c, x->a)) {
			uint64_t du = b4.u & ~b4.i;	/* uninitialized before the if */
			uint64_t ti = t.i & ~t.u, tu = t.u & ~t.i, ei = e.i & ~e.u, eu = e.u & ~e.i;

			for (uint64_t m = du & ((ti & eu) | (tu & ei)); m; m &= m - 1) {
				int v = __builtin_ctzll(m);

				w->spend |= 1ull << v, w->sif[v] = n, w->sfalse[v] = (ti >> v) & 1, w->sdep[v] = w->dep, w->skind[v] = 0;
			}
		}
		*st = t;
		cc_uninitialized_join(st, &e);
		return;
	}
	case AST_NODE_DO:
		if (cc_uninitialized_condition_constant(w, x->a) == 0) {	/* do-while(0): no back edge, a block whose breaks go to its end */
			cc_uninitialized_state brk = { 0, 0, false }, *sb = w->brk, *sc = w->cont, *sd = w->dobrk;
			uint64_t sm = w->dcm;

			w->brk = w->cont = w->dobrk = &brk, w->dcm = 0;
			cc_uninitialized_statement(w, x->b, st);
			w->brk = sb, w->cont = sc, w->dobrk = sd;
			/* clang: the if's edge to the break reaches the use uninitialized, the fall-through initialized */
			for (uint64_t m = w->dcm; st->live && m; m &= m - 1) {
				int v = __builtin_ctzll(m);
				bool binit = w->dcf[v] & 2, inel = w->dcf[v] & 1;	/* the break arm initialized it; it is the else */
				uint64_t b = 1ull << v;

				if (w->dcn[v] > 1 && w->dcn[v] <= 8) {
					/* clang: one warning per if whose break leaves it uninitialized (all of them) */
					bool all = true;

					for (uint32_t j = 0; j < w->dcn[v]; j++)
						all &= !(w->dclf[v][j] & 2);
					if (!all || !(st->i & b) || (st->u & b))
						continue;
					w->spend |= b, w->sif[v] = w->dcl[v][0], w->sdep[v] = w->dep, w->skind[v] = 7;
					w->ndl[v] = w->dcn[v];
					for (uint32_t j = 0; j < w->dcn[v]; j++)
						w->dl[v][j] = w->dcl[v][j], w->dlf[v][j] = w->dclf[v][j] & 1;
					continue;
				}
				if (w->dcn[v] != 1 || (binit ? !(st->u & b) || (st->i & b) : !(st->i & b) || (st->u & b)))
					continue;
				w->spend |= b, w->sif[v] = w->dcif[v], w->sdep[v] = w->dep, w->skind[v] = 0;
				w->sfalse[v] = binit ? !inel : inel;	/* the edge that reaches the use uninitialized */
			}
			w->dcm = sm;	/* (an outer do's candidates: a nested one's are its own) */
			cc_uninitialized_join(st, &brk);
			cc_uninitialized_expression(w, x->a, st, false);
			return;
		}
		__attribute__((fallthrough));
	case AST_NODE_WHILE: case AST_NODE_FOR:
		if (x->op == AST_NODE_FOR)
			cc_uninitialized_statement(w, x->a, st);
		w->blk++;
		w->spend = 0, w->dep++;
		cc_uninitialized_loop(w, x, st);
		w->dep--, cc_uninitialized_pending_drop(w);
		/* a loop that never loops back: its condition's false edge leaves it uninitialized (clang) */
		if (x->op != AST_NODE_DO && (x->op == AST_NODE_WHILE ? x->a : x->b) && cc_uninitialized_condition_constant(w, x->op == AST_NODE_WHILE ? x->a : x->b) < 0)
			cc_uninitialized_pending_add(w, n, 6, &w->lp_head, &w->lp_brk, &w->lp_cx, false);
		w->blk++;
		return;
	case AST_NODE_SWITCH: {
		cc_uninitialized_state *sb = w->brk, *si = w->swin, brk = { 0, 0, false }, in;
		bool sk = w->sw_const, sh = w->sw_hit, def;
		i128 sv = w->sw_val;
		cc_value cv;

		cc_uninitialized_state *ssb = w->swbrk;
		uint32_t ssl[8], snl = w->nswlab, sarm = w->swarm;
		uint64_t ssm = w->swmask, scand = w->swcand;

		memcpy(ssl, w->swlab, sizeof ssl);

		cc_uninitialized_expression(w, x->a, st, false);
		in = *st;
		w->spend = 0;
		w->brk = &brk, w->swin = &in;
		w->swbrk = &brk, w->nswlab = 0, w->swarm = 0, w->swmask = 0, w->swcand = 0;
		w->sw_const = cc_eval(c, x->a, &cv) && cv.kind == CC_VALUE_INTEGER;
		w->sw_val = w->sw_const ? cv.i : 0;
		w->sw_hit = w->sw_const && cc_reach_case_matches(&w->fl, x->b, w->sw_val);
		def = cc_reach_has_default(&w->fl, x->b);
		def = w->sw_const ? (w->sw_hit || def) : def;
		st->live = false;
		w->dep++;
		cc_uninitialized_statement(w, x->b, st);
		cc_uninitialized_switch_arm(w, st);
		w->dep--, cc_uninitialized_pending_drop(w);
		w->blk++;
		cc_uninitialized_join(st, &brk);
		if (!def)
			cc_uninitialized_join(st, &in);
		/* an arm that left a variable uninitialized, which is maybe-initialized after the switch */
		if (in.live && st->live && !w->sw_const)
			for (uint64_t m = w->swcand & (in.u & ~in.i) & st->u & st->i; m; m &= m - 1) {
				int v = __builtin_ctzll(m);
				w->spend |= 1ull << v, w->sif[v] = n, w->sdep[v] = w->dep, w->skind[v] = 4;
			}
		w->swbrk = ssb, w->nswlab = snl, w->swarm = sarm, w->swmask = ssm, w->swcand = scand;
		memcpy(w->swlab, ssl, sizeof ssl);
		w->brk = sb, w->swin = si, w->sw_const = sk, w->sw_hit = sh, w->sw_val = sv;
		return;
	}
	case AST_NODE_CASE: case AST_NODE_DEFAULT:
		if (w->swbrk == w->brk) {	/* (a label of the switch being walked, not inside a loop of it) */
			/* a fallthrough into it continues the arm (case A: case CC_COMPLEX_BINARY: ...: A's edge is named) */
			if (!st->live) {
				w->nswlab = 0, w->swarm++;
				w->swmask = w->swin ? w->swin->u & ~w->swin->i : 0;
			}
			if (w->nswlab < 8)
				w->swlab[w->nswlab++] = n;
		}
		if (w->swin) {
			bool here = true;

			if (w->sw_const && x->op == AST_NODE_CASE)
				here = w->sw_val >= c->ipool[x->a] && w->sw_val <= c->ipool[x->b];
			else if (w->sw_const)
				here = !w->sw_hit;
			if (here) {
				/* a fallthrough that initialized it joins the switch's jump that did not */
				if (st->live && w->swin->live && w->swbrk == w->brk && !w->sw_const)
					for (uint64_t m = (w->swin->u & ~w->swin->i) & st->i & ~st->u; m; m &= m - 1) {
						int v = __builtin_ctzll(m);

						w->spend |= 1ull << v, w->sif[v] = n, w->sfalse[v] = false, w->sdep[v] = w->dep, w->skind[v] = 4;
						w->swc[v][0] = n, w->swa[v][0] = 0, w->nswc[v] = 1;
					}
				cc_uninitialized_join(st, w->swin);
			}
		}
		w->blk++;
		cc_uninitialized_statement(w, x->c, st);
		return;
	case AST_NODE_LABEL:
		if (x->a >= w->first && x->a - w->first < w->nlab) {
			cc_uninitialized_join(st, &w->lab[x->a - w->first]);
			w->seen[x->a - w->first] = 1;
		}
		w->spend = 0;
		w->blk++;
		cc_uninitialized_statement(w, x->c, st);
		return;
	case AST_NODE_GOTO:
		cc_uninitialized_goto(w, x->a, st);
		st->live = false;
		return;
	case AST_NODE_GOTO_INDIRECT:
		cc_uninitialized_expression(w, x->a, st, false);
		for (uint32_t l = w->first; l < w->first + w->nlab; l++)
			if (c->labels[l].addr_taken)
				cc_uninitialized_goto(w, l, st);
		st->live = false;
		return;
	case AST_NODE_BREAK:
		if (w->brk && w->brk == w->swbrk)
			cc_uninitialized_switch_arm(w, st);
		if (w->brk)
			cc_uninitialized_join(w->brk, st);
		st->live = false;
		return;
	case AST_NODE_CONTINUE:
		if (w->cont)
			cc_uninitialized_join(w->cont, st);
		st->live = false;
		return;
	case AST_NODE_ASM: {
		const ast_asm_statement *a = &c->asms[x->a];

		for (uint32_t i = a->nout; i < a->nout + a->nin; i++)
			cc_uninitialized_expression(w, a->ops[i].expr, st, false);
		for (uint32_t i = 0; i < a->nout; i++) {
			uint32_t e = a->ops[i].expr;
			const char *cs = a->ops[i].constraint ? c->strs[a->ops[i].constraint - 1].s : "";

			(void)cs;	/* (clang: a "+" operand is not a use, -Wuninitialized) */
			if (AST_NODE(e)->op == AST_NODE_VARIABLE) {
				cc_uninitialized_write(w, e, st);
			} else
				cc_uninitialized_lvalue(w, e, st);
		}
		for (uint32_t i = 0; i < a->nlabel; i++)
			cc_uninitialized_goto(w, a->labels[i], st);
		return;
	}
	default:
		return;
	}
}

/* the end of a then/else statement as clang's getEndLoc: an expression statement ends with its expression */
static uint32_t cc_uninitialized_statement_end(cc *c, uint32_t n)
{
	if (AST_NODE(n)->op == AST_NODE_EXPRESSION && AST_NODE(n)->a)
		return cc_node_range_bare(c, AST_NODE(n)->a).e;
	return pp_token_end_find(c->p, c->span[n].e);
}

static bool cc_uninitialized_sometimes_report(cc *c, cc_uninitialized_walk *w, uint32_t k)
{
	static const char *const g[] = { "uninitialized", "all", "most", NULL };
	const sym *y = &c->syms[w->var[k]];
	const node *x = AST_NODE(w->sif[k]);
	diag_span cr = cc_node_range_bare(c, x->a), r[2];
	int nr = 1;
	bool f = w->sfalse[k];
	diag_info d = { "note", w->uloc[k], &w->ur[k], 1, NULL, 0, NULL, 0 };

	static const char *const op[] = { "if", "&&", "||", "?:" };
	int kd = w->skind[k];

	if (kd == 7) {	/* each if, the later first (clang's CFG order) */
		bool any = false;

		w->skind[k] = 0;
		for (uint32_t j = w->ndl[k]; j-- > 0;) {
			w->sif[k] = w->dl[k][j], w->sfalse[k] = w->dlf[k][j];
			any |= cc_uninitialized_sometimes_report(c, w, k);
		}
		w->skind[k] = 7;
		return any;
	}

	if (kd == 1 || kd == 2 || kd == 3) {
		cr = cc_node_range_bare(c, x->a);
		if (!cc_report_warning(c, "sometimes-uninitialized", true, g, cr.b, &cr, 1, NULL, 0,
			      "variable '%.*s' is used uninitialized whenever '%s' condition is %s", CC_IDENTIFIER(y->name), op[kd], f ? "false" : "true"))
			return false;
		diag_report_extended(c->p, &d, "uninitialized use occurs here");
		if (kd != 3)
			r[0] = (diag_span){ cr.b, cc_node_range_bare(c, x->b).b };
		else if (f)
			r[0] = (diag_span){ cr.b, cc_node_range_bare(c, x->b).b }, r[1] = (diag_span){ cc_node_range_bare(c, x->b).e, cc_node_range_bare(c, x->c).e }, nr = 2;
		else
			r[0] = (diag_span){ cr.b, cc_node_range_bare(c, x->c).b };
		d = (diag_info){ "note", r[0].b, r, nr, NULL, 0, NULL, 0 };
		diag_report_extended(c->p, &d, "remove the '%s' if its condition is always %s", op[kd], f ? "true" : "false");
		return true;
	}
	if (kd == 6) {
		cr = cc_node_range_bare(c, x->op == AST_NODE_WHILE ? x->a : x->b);
		if (!cc_report_warning(c, "sometimes-uninitialized", true, g, cr.b, &cr, 1, NULL, 0,
			      "variable '%.*s' is used uninitialized whenever '%s' loop exits because its condition is false",
			      CC_IDENTIFIER(y->name), x->op == AST_NODE_WHILE ? "while" : "for"))
			return false;
		diag_report_extended(c->p, &d, "uninitialized use occurs here");
		{
			diag_fixit fx = { cr.b, cr.e, "1", 0 };	/* (while: the condition becomes 1; for: it is removed) */

			d = (diag_info){ "note", cr.b, &cr, 1, &fx, x->op == AST_NODE_WHILE, NULL, 0 };
			diag_report_extended(c->p, &d, "remove the condition if it is always true");
		}
		return true;
	}
	if (kd == 4) {
		/* each label's edge: later arms first (clang's CFG order), a stack of labels in order */
		uint32_t ord[8], n = w->nswc[k];
		bool any = false;

		for (uint32_t i = 0; i < n; i++)
			ord[i] = i;
		for (uint32_t i = 1; i < n; i++)
			for (uint32_t j = i; j > 0 && w->swa[k][ord[j]] > w->swa[k][ord[j - 1]]; j--) {
				uint32_t t = ord[j];

				ord[j] = ord[j - 1], ord[j - 1] = t;
			}
		for (uint32_t i = 0; i < n; i++) {
			uint32_t ln = w->swc[k][ord[i]];
			diag_span kr = cc_uninitialized_case_range(c, ln);

			if (!cc_report_warning(c, "sometimes-uninitialized", true, g, kr.b, &kr, 1, NULL, 0,
				      "variable '%.*s' is used uninitialized whenever switch %s is taken", CC_IDENTIFIER(y->name),
				      AST_NODE(ln)->op == AST_NODE_CASE ? "case" : "default"))
				continue;
			d = (diag_info){ "note", w->uloc[k], &w->ur[k], 1, NULL, 0, NULL, 0 };
			diag_report_extended(c->p, &d, "uninitialized use occurs here");
			any = true;
		}
		return any;
	}
	if (!cc_report_warning(c, "sometimes-uninitialized", true, g, cr.b, &cr, 1, NULL, 0,
		      "variable '%.*s' is used uninitialized whenever 'if' condition is %s", CC_IDENTIFIER(y->name), f ? "false" : "true"))
		return false;
	diag_report_extended(c->p, &d, "uninitialized use occurs here");
	if (f) {
		r[0] = (diag_span){ c->span[w->sif[k]].b, c->span[x->b].b };
		if (x->c)	/* (after an expression statement: its ';') */
			r[1] = (diag_span){ AST_NODE(x->b)->op == AST_NODE_EXPRESSION ? c->span[x->b].e : cc_uninitialized_statement_end(c, x->b), cc_uninitialized_statement_end(c, x->c) }, nr = 2;
	} else
		r[0] = (diag_span){ c->span[w->sif[k]].b, x->c ? c->span[x->c].b : AST_NODE(x->b)->op == AST_NODE_BLOCK ? cc_uninitialized_statement_end(c, x->b) : c->span[x->b].e };	/* (no else: the whole if) */
	d = (diag_info){ "note", r[0].b, r, nr, NULL, 0, NULL, 0 };
	diag_report_extended(c->p, &d, "remove the 'if' if its condition is always %s", f ? "true" : "false");
	return true;
}

static const char *cc_uninitialized_zero_initializer_text(cc *c, ast_qualified_type t)
{
	int k = AST_TYPE_KIND(c, canon(c, t));

	if (k == AST_TYPE_ENUM || ((k == AST_TYPE_STRUCT || k == AST_TYPE_UNION) && !cc_type_is_complex(c, t)))
		return NULL;
	if (k >= AST_TYPE_BFLOAT16 && k <= AST_TYPE_FLOAT128)
		return " = 0.0";
	if (k == AST_TYPE_BOOL && pp_macro_id_is_defined(c->p, vintern_cstr("false")))
		return " = false";
	if (k == AST_TYPE_POINTER && pp_macro_id_is_defined(c->p, vintern_cstr("NULL")))
		return " = NULL";
	if (k == AST_TYPE_CHAR || k == AST_TYPE_SIGNED_CHAR || k == AST_TYPE_UNSIGNED_CHAR)
		return " = '\\0'";
	return " = 0";
}

CC_PRIVATE void cc_uninitialized_check(cc *c, uint32_t body, uint32_t first_label)
{
	cc_uninitialized_walk ww, *w = &ww;
	cc_uninitialized_state st = { 0, 0, true }, small[16];
	uint8_t seen[64];
	uint32_t nl = c->nlabel - first_label;

	static const char *const gs[] = { "uninitialized", "all", "most", NULL };

	if (!diag_warning_state(c->p, "uninitialized", false, cc_warning_group_all) && !diag_warning_state(c->p, "sometimes-uninitialized", false, gs) &&
	    !diag_pragma_active(c->p))	/* (a pragma may turn them on here: each warning asks at its location) */
		return;	/* (-Wsometimes-uninitialized alone runs it too) */
	/* (only the head is cleared: the report arrays are written before they are read) */
	memset(w, 0, offsetof(cc_uninitialized_walk, uloc));
	w->c = c, w->fl.c = c;
	w->lab = nl <= 16 ? small : calloc(nl, sizeof(cc_uninitialized_state) + 1), w->first = first_label, w->nlab = nl;
	if (!w->lab)
		return;
	w->seen = nl <= 16 ? seen : (uint8_t *)(w->lab + nl);
	memset(small, 0, sizeof small);
	for (uint32_t i = 0; i < c->nuv; i++)
		cc_uninitialized_variable_add(w, c->uv[i]);
	if (!w->nv && !(c->uv_any & 2)) {
		if (w->lab != small)
			free(w->lab);
		return;
	}
	for (int it = 0; it < 16; it++) {
		w->changed = false;
		w->report = true, w->nord = w->nself = 0, w->done = 0, w->blk = 0, w->spend = 0;
		memset(w->seen, 0, nl);
		st = (cc_uninitialized_state){ 0, 0, true };
		cc_uninitialized_statement(w, body, &st);
		if (!w->changed)
			break;
	}
	for (uint32_t j = 0; j < w->nself; j++) {
		const sym *y = &c->syms[w->sself[j]];
		diag_span r[2] = { { y->loc, pp_token_end_find(c->p, y->loc) }, w->sr[j] };

		cc_report_warning(c, "uninitialized", false, cc_warning_group_all, w->sloc[j], r, 2, NULL, 0,
			 "variable '%.*s' is uninitialized when used within its own initialization", CC_IDENTIFIER(y->name));
	}
	/* later blocks first, in order within a block */
	for (uint32_t j = 1; j < w->nord; j++)
		for (uint32_t i = j; i > 0 && w->ublk[w->order[i]] > w->ublk[w->order[i - 1]]; i--) {
			uint32_t t = w->order[i];

			w->order[i] = w->order[i - 1], w->order[i - 1] = t;
		}
	for (uint32_t j = 0; j < w->nord; j++) {
		uint32_t k = w->order[j];
		const sym *y = &c->syms[w->var[k]];
		const char *z = cc_uninitialized_zero_initializer_text(c, y->ty);

		if (w->kind[k] == 1 && (w->selfv >> k & 1) && w->xsym[k] == w->var[k]) {
			diag_span r[2] = { { y->loc, pp_token_end_find(c->p, y->loc) }, w->xr[k] };

			cc_report_warning(c, "uninitialized", false, cc_warning_group_all, w->xloc[k], r, 2, NULL, 0,
				 "variable '%.*s' is uninitialized when used within its own initialization", CC_IDENTIFIER(y->name));
			continue;
		}
		if (w->kind[k] == 2) {
			if (!cc_uninitialized_sometimes_report(c, w, k))
				continue;
		} else if (!cc_report_warning(c, "uninitialized", false, cc_warning_group_all, w->uloc[k], &w->ur[k], 1, NULL, 0,
			      "variable '%.*s' is uninitialized when used here", CC_IDENTIFIER(y->name)))
			continue;
		if (z) {
			/* the declarator's end: the name, or ')' of a function pointer (not an asm label or attributes) */
			bool fp = AST_TYPE_KIND(c, canon(c, y->ty)) == AST_TYPE_POINTER && AST_TYPE_KIND(c, canon(c, AST_TYPE(c, canon(c, y->ty))->base)) == AST_TYPE_FUNCTION;
			uint32_t e = pp_token_end_find(c->p, fp && y->ple && !y->asmname && !y->attrs ? y->ple : y->loc);
			diag_fixit fx = { e, e, z, 0 };
			diag_info d = { "note", e, NULL, 0, &fx, 1, NULL, 0 };

			diag_report_extended(c->p, &d, "initialize the variable '%.*s' to silence this warning", CC_IDENTIFIER(y->name));
		} else
			cc_report_note(c, y->plb ? y->plb : y->loc, "variable '%.*s' is declared here", CC_IDENTIFIER(y->name));
	}
	if (w->lab != small)
		free(w->lab);
}
