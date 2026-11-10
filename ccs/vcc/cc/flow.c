/* vcc front end: can a function body fall off its end (clang's CheckFallThrough, -Wreturn-type, -Winvalid-noreturn) */
#include "ccimpl.h"

/*
 * Reachability over the statement tree, as clang's CFG sees it: constant
 * conditions prune branches (if (0), while (1), a constant switch), calls
 * of noreturn functions end a path, goto/labels by a fixpoint over the
 * labels (computed goto: every label whose address is taken).
 */
typedef struct flw {
	cc *c;
	uint8_t *lab;		/* label reached, from first */
	uint32_t first, nlab;
	bool changed;
	bool brk, cont;		/* the innermost loop/switch: reached by break/continue */
	bool sw_in;		/* the innermost switch: its condition reached */
	bool sw_const;		/* ... with a constant condition */
	i128 sw_val;
	bool sw_hit;		/* ... that some case matches */
	bool live_ret, fake;	/* a reachable return; a reachable noreturn call */
} flw;

/* a condition: 1 constant true, 0 constant false, -1 not constant */
static int fl_const(flw *f, uint32_t n)
{
	cval cv;

	cc *c = f->c;

	if (!n)
		return 1;	/* for (;;) */
	if (cc_eval(c, n, &cv) && cv.kind == CV_INT)
		return cv.i != 0;
	/* clang's tryEvaluateBool: && and || with one known side, !, a const variable's initializer */
	switch (N(n)->op) {
	case N_LOGOR: {
		int a = fl_const(f, N(n)->a), b;

		if (a == 1)
			return 1;
		b = fl_const(f, N(n)->b);
		return b == 1 ? 1 : a == 0 && b == 0 ? 0 : -1;
	}
	case N_LOGAND: {
		int a = fl_const(f, N(n)->a), b;

		if (a == 0)
			return 0;
		b = fl_const(f, N(n)->b);
		return b == 0 ? 0 : a == 1 && b == 1 ? 1 : -1;
	}
	case N_UNARY:
		if (N(n)->aux == T_EXCLAIM) {
			int a = fl_const(f, N(n)->a);

			return a < 0 ? -1 : !a;
		}
		return -1;
	case N_CAST:
		return N(n)->aux == CK_BOOL || N(n)->aux == CK_INT || N(n)->aux == CK_NOP ? fl_const(f, N(n)->a) : -1;
	case N_VAR: {
		const sym *y = &c->syms[N(n)->a];

		if (y->kind == S_VAR && (QQ(y->ty) & Q_CONST) && !(QQ(y->ty) & Q_VOLATILE) && y->body &&
		    is_integer(c, y->ty) && cc_eval(c, y->body, &cv) && cv.kind == CV_INT)
			return cv.i != 0;
		return -1;
	}
	default:
		return -1;
	}
}

/* an expression statement that never completes: a call of a noreturn function */
static bool fl_noreturn(flw *f, uint32_t e)
{
	cc *c = f->c;

	while (e && N(e)->op == N_CAST)
		e = N(e)->a;
	if (!e)
		return false;
	if (N(e)->op == N_COMMA)
		return fl_noreturn(f, N(e)->a) || fl_noreturn(f, N(e)->b);
	if (N(e)->op == N_BUILTIN)
		return N(e)->aux == BI_UNREACHABLE || N(e)->aux == BI_TRAP;
	if (N(e)->op == N_CALL) {
		uint32_t fn = N(e)->a;

		while (N(fn)->op == N_CAST)
			fn = N(fn)->a;
		if (N(fn)->op == N_VAR && (c->syms[N(fn)->a].flags & SF_NORETURN))
			return true;
		/* a call through a pointer to a noreturn function type is not tracked */
	}
	return false;
}

/* a noreturn call anywhere in the expression (?:, &&, || operands, arguments) */
static bool fl_has_noreturn(flw *f, uint32_t e)
{
	cc *c = f->c;
	const node *x;

	if (!e)
		return false;
	x = N(e);
	switch (x->op) {
	case N_CALL:
		if (fl_noreturn(f, e))
			return true;
		for (uint32_t i = 0; i < x->c; i++)
			if (fl_has_noreturn(f, c->lists[x->b + i]))
				return true;
		return false;
	case N_BUILTIN:
		return fl_noreturn(f, e);
	case N_COND:
		return fl_has_noreturn(f, x->a) || fl_has_noreturn(f, x->b) || fl_has_noreturn(f, x->c);
	case N_LOGAND: case N_LOGOR: case N_COMMA: case N_BINARY: case N_ASSIGN:
		return fl_has_noreturn(f, x->a) || fl_has_noreturn(f, x->b);
	case N_CAST: case N_UNARY:
		return fl_has_noreturn(f, x->a);
	default:
		return false;
	}
}

static void fl_reach_label(flw *f, uint32_t l)
{
	if (l >= f->first && l - f->first < f->nlab && !f->lab[l - f->first]) {
		f->lab[l - f->first] = 1;
		f->changed = true;
	}
}


/* the switch body's own case labels (not a nested switch's): one matches v / a default */
static bool fl_case_match(flw *f, uint32_t n, i128 v)
{
	cc *c = f->c;
	const node *x;

	if (!n)
		return false;
	x = N(n);
	switch (x->op) {
	case N_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			if (fl_case_match(f, c->lists[x->b + i], v))
				return true;
		return false;
	case N_CASE:
		return (v >= c->ipool[x->a] && v <= c->ipool[x->b]) || fl_case_match(f, x->c, v);
	case N_DEFAULT: case N_LABEL:
		return fl_case_match(f, x->c, v);
	case N_IF:
		return fl_case_match(f, x->b, v) || fl_case_match(f, x->c, v);
	case N_WHILE: case N_DO:
		return fl_case_match(f, x->b, v);
	case N_FOR:
		return fl_case_match(f, c->lists[x->c + 1], v);
	default:
		return false;
	}
}

static bool fl_has_default(flw *f, uint32_t n)
{
	cc *c = f->c;
	const node *x;

	if (!n)
		return false;
	x = N(n);
	switch (x->op) {
	case N_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			if (fl_has_default(f, c->lists[x->b + i]))
				return true;
		return false;
	case N_DEFAULT:
		return true;
	case N_CASE: case N_LABEL:
		return fl_has_default(f, x->c);
	case N_IF:
		return fl_has_default(f, x->b) || fl_has_default(f, x->c);
	case N_WHILE: case N_DO:
		return fl_has_default(f, x->b);
	case N_FOR:
		return fl_has_default(f, c->lists[x->c + 1]);
	default:
		return false;
	}
}

static bool fl_stmt(flw *f, uint32_t n, bool in);

static bool fl_loop_body(flw *f, uint32_t body, bool in, bool *brk, bool *cont)
{
	bool sb = f->brk, sc = f->cont, out;

	f->brk = f->cont = false;
	out = fl_stmt(f, body, in);
	*brk = f->brk, *cont = f->cont;
	f->brk = sb, f->cont = sc;
	return out;
}

static bool fl_stmt(flw *f, uint32_t n, bool in)
{
	cc *c = f->c;
	const node *x;

	if (!n)
		return in;
	x = N(n);
	switch (x->op) {
	case N_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			in = fl_stmt(f, c->lists[x->b + i], in);
		return in;
	case N_EXPR:
		if (in && x->a && N(x->a)->op == N_STMTEXPR)
			return fl_stmt(f, N(x->a)->a, in);
		if (in && fl_noreturn(f, x->a)) {
			f->fake = true;
			return false;
		}
		if (in && fl_has_noreturn(f, x->a))
			f->fake = true;	/* (a noreturn call on some path through the expression) */
		return in;
	case N_DECL:
		return in;
	case N_RETURN:
		if (in)
			f->live_ret = true;
		return false;
	case N_IF: {
		int k = fl_const(f, x->a);
		bool t = fl_stmt(f, x->b, in && k != 0), e = fl_stmt(f, x->c, in && k != 1);

		return t || e;
	}
	case N_WHILE: {
		int k = fl_const(f, x->a);
		bool brk, cont, end = fl_loop_body(f, x->b, in && k != 0, &brk, &cont);

		return brk || ((in || end || cont) && k != 1);
	}
	case N_DO: {
		int k = fl_const(f, x->a);
		bool brk, cont, end = fl_loop_body(f, x->b, in, &brk, &cont);

		return brk || ((end || cont) && k != 1);
	}
	case N_FOR: {
		uint32_t step = c->lists[x->c], body = c->lists[x->c + 1];
		int k;
		bool brk, cont, end;

		in = fl_stmt(f, x->a, in);
		k = fl_const(f, x->b);
		end = fl_loop_body(f, body, in && k != 0, &brk, &cont);
		if (step && N(step)->op == N_STMTEXPR) {
			/* a statement expression step (the kernel's scoped_guard: ({ goto l; })) */
			bool back = fl_stmt(f, N(step)->a, end || cont);

			return brk || ((in || back) && k != 1);
		}
		return brk || ((in || end || cont) && k != 1);
	}
	case N_SWITCH: {
		bool sb = f->brk, sc = f->cont, si = f->sw_in, sk = f->sw_const, sh = f->sw_hit, out, def = false;
		i128 sv = f->sw_val;
		cval cv;

		f->brk = false;
		f->sw_in = in;
		f->sw_const = cc_eval(c, x->a, &cv) && cv.kind == CV_INT;
		f->sw_val = f->sw_const ? cv.i : 0;
		f->sw_hit = f->sw_const && fl_case_match(f, x->b, f->sw_val);
		def = fl_has_default(f, x->b);
		out = fl_stmt(f, x->b, false);
		def = f->sw_const ? (f->sw_hit || def) : def;
		out = out || f->brk || (in && !def);
		f->cont = sc || f->cont;
		f->brk = sb, f->sw_in = si, f->sw_const = sk, f->sw_val = sv, f->sw_hit = sh;
		return out;
	}
	case N_CASE: {
		bool here = f->sw_in;

		if (f->sw_const) {
			i128 lo = c->ipool[x->a], hi = c->ipool[x->b];

			here = here && f->sw_val >= lo && f->sw_val <= hi;
		}
		return fl_stmt(f, x->c, in || here);
	}
	case N_DEFAULT: {
		/* a constant switch reaches default only when no case matches */
		bool here = f->sw_in && !(f->sw_const && f->sw_hit);

		return fl_stmt(f, x->c, in || here);
	}
	case N_LABEL:
		return fl_stmt(f, x->c, in || (x->a >= f->first && x->a - f->first < f->nlab && f->lab[x->a - f->first]));
	case N_GOTO:
		if (in)
			fl_reach_label(f, x->a);
		return false;
	case N_GOTO_IND:
		if (in)
			for (uint32_t l = f->first; l < f->first + f->nlab; l++)
				if (c->labels[l].addr_taken)
					fl_reach_label(f, l);
		return false;
	case N_BREAK:
		if (in)
			f->brk = true;
		return false;
	case N_CONTINUE:
		if (in)
			f->cont = true;
		return false;
	case N_ASM: {
		const casm *a = &c->asms[x->a];

		if (in)
			for (uint32_t i = 0; i < a->nlabel; i++)
				fl_reach_label(f, a->labels[i]);
		return in;
	}
	default:
		return in;
	}
}

/*
 * After a function body: 0 the end is not reached, 1 it may be (clang's
 * MaybeFallThrough: also returns or noreturn calls), 2 it always is.
 */
VPRIV int body_falls_off(cc *c, uint32_t body, uint32_t first_label)
{
	flw f = { .c = c };
	uint8_t small[256];
	bool end = false;
	uint32_t nl = c->nlabel - first_label;

	f.first = first_label, f.nlab = nl;
	f.lab = nl <= sizeof(small) ? small : calloc(nl, 1);
	if (!f.lab)
		return 0;
	if (nl <= sizeof(small))
		memset(small, 0, nl);
	for (int it = 0; it < 16; it++) {
		f.changed = false;
		f.live_ret = f.fake = false;
		f.brk = f.cont = f.sw_in = false;
		end = fl_stmt(&f, body, true);
		if (!f.changed)
			break;
	}
	if (f.lab != small)
		free(f.lab);
	if (!end)
		return 0;
	return f.live_ret || f.fake ? 1 : 2;
}

/*
 * -Wuninitialized (clang's UninitializedValues, its definite case only):
 * per tracked variable (a local scalar or vector) two bits, "may be
 * uninitialized" and "may be initialized", over the statement tree as
 * above: loops to a fixpoint, labels by a fixpoint over the gotos.  A read
 * where the variable can only be uninitialized is reported, the first per
 * variable; a "sometimes" one (-Wsometimes-uninitialized) is not.
 */
typedef struct ust { uint64_t u, i; bool live; } ust;

typedef struct uwk {
	cc *c;
	flw fl;			/* for fl_const / fl_case_match */
	uint32_t var[64], nv;
	uint32_t hs[128];	/* sym + 1 -> index + 1 */
	uint8_t hi[128];
	ust *lab;
	uint8_t *seen;		/* label reached in this pass: a goto to it is backward */
	uint32_t first, nlab;
	bool changed, report;
	ust *brk, *cont, *swin;
	ust *dobrk;		/* the exit of the do-while(0) being walked (no back edge: a block) */
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
	pp_span selfr;
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
	pp_span skr[64];	/* ... a case: its value / default's range */
	/* the switch being walked: the case arm entered from it only, and what it leaves uninitialized */
	ust *swbrk;
	uint32_t swlab[8], nswlab, swarm;	/* the arm's labels (stacked: case A: case B:), its number */
	uint64_t swmask, swcand;
	uint32_t swc[64][8];	/* per variable: the labels whose edge leaves it uninitialized */
	uint8_t swa[64][8], nswc[64];	/* ... 0 if, 1 &&, 2 ||, 3 ?:, 4 case, 5 default */
	uint32_t sdep[64];
	ust lp_head, lp_cx, lp_brk;	/* the last loop walked: its head at the fixpoint, its exits */	/* ... at this branch depth: the use must post-dominate it */
	uint8_t kind[64];	/* report: 1 definite, 2 sometimes */
	pp_span ur[64];
	uint32_t sself[16], sloc[16];	/* self-initializations */
	pp_span sr[16];
} uwk;

/* the condition can be a constant (fl_const's cases): most are not, and cc_eval is not free */
static bool uv_maycon(cc *c, uint32_t n)
{
	const node *x;

	if (!n)
		return true;
	x = N(n);
	switch (x->op) {
	case N_NUM: case N_FNUM: case N_STR: case N_LABELADDR:
		return true;
	case N_VAR:
		return (QQ(c->syms[x->a].ty) & Q_CONST) || KIND(c, x->ty) == TY_ARRAY || KIND(c, x->ty) == TY_FUNC;
	case N_ADDR:
		return true;	/* (&global, &x->f) */
	case N_CAST: case N_UNARY:
		return uv_maycon(c, x->a);
	case N_BINARY: case N_COMMA:
		return uv_maycon(c, x->a) && uv_maycon(c, x->b);
	case N_LOGAND: case N_LOGOR:
		return uv_maycon(c, x->a) || uv_maycon(c, x->b);
	case N_COND:
		return uv_maycon(c, x->a) || (uv_maycon(c, x->b) && uv_maycon(c, x->c));
	case N_BUILTIN:
		return true;
	default:
		return false;
	}
}

static int uv_const(uwk *w, uint32_t n)
{
	return uv_maycon(w->c, n) ? fl_const(&w->fl, n) : -1;
}

/* && || ?: in a condition: clang names that operator's edge instead */
static bool uv_haslogic(cc *c, uint32_t n)
{
	while (n && (N(n)->op == N_CAST || N(n)->op == N_UNARY))
		n = N(n)->a;
	if (!n)
		return false;
	switch (N(n)->op) {
	case N_LOGAND: case N_LOGOR: case N_COND:
		return true;
	case N_BINARY: case N_COMMA: case N_ASSIGN:
		return uv_haslogic(c, N(n)->a) || uv_haslogic(c, N(n)->b);
	default:
		return false;
	}
}

/* a branch at node n (kind as skind) left the variables uninitialized before it initialized on path a only, uninitialized on path b */
static void uv_pend(uwk *w, uint32_t n, int kind, const ust *b4, const ust *a, const ust *b, bool f)
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
static void uv_swarm(uwk *w, const ust *st)
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
static pp_span uv_caserange(cc *c, uint32_t n)
{
	uint32_t b = N(n)->loc;
	size_t len = 0;
	const char *t = NULL;
	size_t i = 0, e;

	for (uint32_t n = 256; !t && n >= 8; n /= 2)	/* (near the end of the file) */
		t = pp_src(c->p, b, b + n, &len);

	if (N(n)->op == N_DEFAULT || !t)
		return (pp_span){ b, b + 7 };
	if (len > 4 && !memcmp(t, "case", 4))
		i = 4;
	while (i < len && (t[i] == ' ' || t[i] == '\t'))
		i++;
	for (e = i; e < len && t[e] != ':' && t[e] != '\n'; e++)
		;
	while (e > i && (t[e - 1] == ' ' || t[e - 1] == '\t'))
		e--;
	return (pp_span){ b + (uint32_t)i, b + (uint32_t)e };
}

/* leaving a branch: pending ifs inside it do not dominate what follows */
static inline void uv_drop(uwk *w)
{
	for (uint64_t m = w->spend; m; m &= m - 1) {
		int v = __builtin_ctzll(m);

		if (w->sdep[v] > w->dep)
			w->spend &= ~(1ull << v);
	}
}

static inline void uj(ust *a, const ust *b)
{
	if (!b->live)
		return;
	if (!a->live) {
		*a = *b;
		return;
	}
	a->u |= b->u, a->i |= b->i;
}

static int uv_idx(uwk *w, uint32_t s)
{
	uint32_t h = (s * 2654435761u) >> 25;

	for (;; h = (h + 1) & 127) {
		if (!w->hs[h])
			return -1;
		if (w->hs[h] == s + 1)
			return w->hi[h] - 1;
	}
}

static int uv_add(uwk *w, uint32_t s)
{
	uint32_t h = (s * 2654435761u) >> 25;
	int k = uv_idx(w, s);

	if (k >= 0 || w->nv >= 64)
		return k;
	while (w->hs[h])
		h = (h + 1) & 127;
	w->hs[h] = s + 1, w->hi[h] = (uint8_t)(w->nv + 1);
	w->var[w->nv] = s;
	return (int)w->nv++;
}

static void uv_read(uwk *w, uint32_t n, ust *st)
{
	cc *c = w->c;
	int k;
	uint64_t b;

	if (!st->live)
		return;
	if (w->self && N(n)->a == w->self && !w->selfhit) {
		w->selfhit = true, w->selfloc = N(n)->loc, w->selfr = rng(w->c, n);
		return;
	}
	if ((k = uv_idx(w, N(n)->a)) < 0)
		return;
	b = 1ull << k;
	if (!w->report || !(st->u & b))
		return;
	if (!(st->i & b) && (!(w->done & b) || w->kind[k] == 2)) {
		if (!(w->done & b))
			w->order[w->nord++] = (uint32_t)k;
		w->done |= b, w->kind[k] = 1;
		w->uloc[k] = N(n)->loc, w->ur[k] = rng(w->c, n), w->ublk[k] = w->blk;
	} else if ((st->i & b) && (w->spend & b) && w->sdep[k] == w->dep && !(w->done & b)) {
		w->done |= b, w->kind[k] = 2;
		w->uloc[k] = N(n)->loc, w->ur[k] = rng(w->c, n), w->ublk[k] = w->blk;
		w->order[w->nord++] = (uint32_t)k;
	}
}

static void uv_write(uwk *w, uint32_t n, ust *st)
{
	cc *c = w->c;
	int k = uv_idx(w, N(n)->a);

	if (k >= 0)
		st->u &= ~(1ull << k), st->i |= 1ull << k, w->spend &= ~(1ull << k), w->swmask &= ~(1ull << k);
}

static void uv_ex(uwk *w, uint32_t e, ust *st, bool ignore);

/* an assigned operand: a variable is not read, anything else is */
static void uv_lv(uwk *w, uint32_t e, ust *st)
{
	cc *c = w->c;

	while (e && (N(e)->op == N_MEMBER || N(e)->op == N_UNARY))	/* (a member, __real/__imag) */
		e = N(e)->a;
	if (e && N(e)->op != N_VAR)
		uv_ex(w, e, st, false);
}

static void uv_stmt(uwk *w, uint32_t n, ust *st);

static void uv_ex(uwk *w, uint32_t e, ust *st, bool ignore)
{
	cc *c = w->c;
	const node *x;

	if (!e)
		return;
	x = N(e);
	switch (x->op) {
	case N_VAR:
		if (!ignore)
			uv_read(w, e, st);
		return;
	case N_ASSIGN:
		if (N(x->a)->op == N_VAR) {
			if (x->aux)
				uv_read(w, x->a, st);
			uv_ex(w, x->b, st, false);
			uv_write(w, x->a, st);
		} else {
			uint32_t v = x->a;

			/* __real/__imag of a _Complex variable: initializes it (clang) */
			while (N(v)->op == N_MEMBER || N(v)->op == N_UNARY)
				v = N(v)->a;
			if (x->aux)
				uv_ex(w, x->a, st, false);
			uv_ex(w, x->b, st, false);
			if (!x->aux)
				uv_lv(w, x->a, st);
			if (N(v)->op == N_VAR)
				uv_write(w, v, st);
		}
		return;
	case N_PREINC: case N_PREDEC: case N_POSTINC: case N_POSTDEC:
		uv_ex(w, x->a, st, false);
		if (N(x->a)->op == N_VAR)
			uv_write(w, x->a, st);
		return;
	case N_ADDR: {
		uint32_t v = x->a;

		while (N(v)->op == N_MEMBER || N(v)->op == N_UNARY)
			v = N(v)->a;
		if (N(v)->op == N_VAR)
			uv_write(w, v, st);	/* (escapes: initialized from here on) */
		else
			uv_lv(w, x->a, st);
		return;
	}
	case N_CAST:
		uv_ex(w, x->a, st, x->aux == CK_VOID);
		return;
	case N_CALL:
		uv_ex(w, x->a, st, false);
		for (uint32_t i = 0; i < x->c; i++) {
			uint32_t a = c->lists[x->b + i];

			/* &v for a pointer-to-const parameter: neither a use nor an initialization */
			if (N(a)->op == N_CAST && N(N(a)->a)->op == N_ADDR && N(N(N(a)->a)->a)->op == N_VAR &&
			    KIND(c, N(a)->ty) == TY_PTR && (QQ(T(c, N(a)->ty)->base) & Q_CONST))
				continue;
			uv_ex(w, a, st, false);
		}
		if (fl_noreturn(&w->fl, e))
			st->live = false;
		return;
	case N_BUILTIN:
		for (uint32_t i = 0; i < x->c; i++) {
			uint32_t a = c->lists[x->b + i];

			if (i == 0 && (x->aux == BI_VA_START || x->aux == BI_VA_COPY)) {
				while (N(a)->op == N_CAST || N(a)->op == N_ADDR)
					a = N(a)->a;
				if (N(a)->op == N_VAR) {
					uv_write(w, a, st);	/* (va_start/va_copy initialize their va_list) */
					continue;
				}
			}
			uv_ex(w, a, st, false);
		}
		if (fl_noreturn(&w->fl, e))
			st->live = false;
		return;
	case N_COND: {
		int k = uv_const(w, x->a);
		ust t, f;

		uv_ex(w, x->a, st, false);
		t = *st, f = *st;
		t.live &= k != 0, f.live &= k != 1;
		ust b4 = *st;

		w->blk++, w->dep++;
		if (x->b)
			uv_ex(w, x->b, &t, ignore);
		w->blk++;
		uv_ex(w, x->c, &f, ignore);
		w->blk++, w->dep--, uv_drop(w);
		if (k < 0 && x->b) {
			uv_pend(w, e, 3, &b4, &t, &f, true);
			uv_pend(w, e, 3, &b4, &f, &t, false);
		}
		*st = (ust){ 0, 0, false };
		uj(st, &t), uj(st, &f);
		return;
	}
	case N_LOGAND: case N_LOGOR: {
		int k = uv_const(w, x->a);
		bool and = x->op == N_LOGAND;
		ust r, s;

		uv_ex(w, x->a, st, false);
		r = *st, s = *st;
		r.live &= and ? k != 0 : k != 1;	/* b evaluated */
		s.live &= and ? k != 1 : k != 0;	/* short circuit */
		ust b4 = r;

		w->blk++, w->dep++;
		uv_ex(w, x->b, &r, false);
		w->blk++, w->dep--, uv_drop(w);
		if (k < 0)
			uv_pend(w, e, and ? 1 : 2, &b4, &r, &s, and);
		*st = s;
		uj(st, &r);
		return;
	}
	case N_COMMA:
		uv_ex(w, x->a, st, true);
		uv_ex(w, x->b, st, ignore);
		return;
	case N_BINARY: case N_CPLX:
		uv_ex(w, x->a, st, false);
		uv_ex(w, x->b, st, false);
		return;
	case N_INDEX:
		uv_ex(w, x->a, st, false);
		uv_ex(w, x->b, st, false);
		return;
	case N_UNARY: case N_DEREF: case N_VAARG:
		uv_ex(w, x->a, st, false);
		return;
	case N_MEMBER:
		if (N(x->a)->op == N_VAR && is_record(c, N(x->a)->ty) && !is_complex(c, N(x->a)->ty))
			uv_write(w, x->a, st);	/* s.m of a struct variable: no use, and initialized from here on (clang) */
		else
			uv_ex(w, x->a, st, false);	/* (a _Complex part is a use) */
		return;
	case N_STMTEXPR:
		uv_stmt(w, x->a, st);
		return;
	case N_COMPLIT:
		uv_ex(w, x->a, st, false);
		return;
	case N_INIT:
		if (x->aux == 1)
			for (uint32_t i = 0; i < x->c; i++)
				uv_ex(w, c->lists[x->b + 3 * i + 2], st, false);
		else
			for (uint32_t i = 0; i < x->c; i++)
				uv_ex(w, c->lists[x->b + i], st, false);
		return;
	default:
		return;
	}
}

static void uv_goto(uwk *w, uint32_t l, const ust *st)
{
	ust *d;
	ust o;

	if (!st->live || l < w->first || l - w->first >= w->nlab)
		return;
	d = &w->lab[l - w->first], o = *d;
	uj(d, st);
	if (w->seen[l - w->first] && (d->live != o.live || d->u != o.u || d->i != o.i))
		w->changed = true;	/* (a forward goto is joined when the label is reached) */
}

/* a loop: body (and cond, step) to a fixpoint without reports, then once more */
static void uv_loop(uwk *w, const node *x, ust *st)
{
	cc *c = w->c;
	ust in = *st, head = in, out = { 0, 0, false };
	bool rep = w->report;
	ust *sb = w->brk, *sc = w->cont;
	uint32_t cond = x->op == N_FOR ? x->b : x->a;
	uint32_t body = x->op == N_FOR ? c->lists[x->c + 1] : x->b;
	uint32_t step = x->op == N_FOR ? c->lists[x->c] : 0;
	int k = cond || x->op != N_FOR ? uv_const(w, cond) : 1;

	uint32_t nord0 = w->nord, nself0 = w->nself;
	uint64_t done0 = w->done;

	/* the first pass reports; kept if it is already the fixpoint */
	for (int it = 0;; it++) {
		ust s = head, brk = { 0, 0, false }, cont = { 0, 0, false }, back, nh;
		uint32_t b0 = w->blk;
		bool last = (w->report && it) || it >= 8;

		out = (ust){ 0, 0, false };
		w->brk = &brk, w->cont = &cont;
		if (x->op == N_DO) {
			uv_stmt(w, body, &s);
			uj(&s, &cont);
			uv_ex(w, cond, &s, false);
			back = s, back.live &= k != 0;
			out = s, out.live &= k != 1;
		} else {
			if (cond)
				uv_ex(w, cond, &s, false);
			out = s, out.live &= k != 1;
			s.live &= k != 0;
			uv_stmt(w, body, &s);
			uj(&s, &cont);
			if (step && N(step)->op == N_STMTEXPR)
				uv_stmt(w, N(step)->a, &s);
			else if (step)
				uv_ex(w, step, &s, true);
			back = s;
		}
		w->lp_head = head, w->lp_cx = out, w->lp_brk = brk;
		uj(&out, &brk);
		w->brk = sb, w->cont = sc;
		if (last)
			break;
		w->blk = b0;
		nh = in;
		uj(&nh, &back);
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

static void uv_stmt(uwk *w, uint32_t n, ust *st)
{
	cc *c = w->c;
	const node *x;

	if (!n)
		return;
	x = N(n);
	if (x->op < N_BLOCK) {
		uv_ex(w, n, st, true);
		return;
	}
	switch (x->op) {
	case N_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			uv_stmt(w, c->lists[x->b + i], st);
		return;
	case N_EXPR:
		uv_ex(w, x->a, st, false);	/* ("x;" is a use) */
		if (fl_noreturn(&w->fl, x->a))
			st->live = false;
		return;
	case N_DECL: {
		int k = uv_idx(w, x->a);

		if (!x->b) {
			if (k >= 0 && st->live)
				st->u |= 1ull << k, st->i &= ~(1ull << k);
			return;
		}
		if (k >= 0)
			st->u &= ~(1ull << k), st->i |= 1ull << k;
		{
			uint32_t ss = w->self;
			bool sh = w->selfhit;

			w->self = uv_tracked(c, c->syms[x->a].ty) ? x->a : 0, w->selfhit = false;
			uv_ex(w, x->b, st, false);
			if (w->selfhit && w->report && st->live && w->nself < 16) {
				w->sself[w->nself] = x->a, w->sloc[w->nself] = w->selfloc;
				w->sr[w->nself++] = w->selfr;
			}
			w->self = ss, w->selfhit = sh;
		}
		return;
	}
	case N_RETURN:
		uv_ex(w, x->a, st, false);
		st->live = false;
		return;
	case N_IF: {
		int k = uv_const(w, x->a);
		ust t, e;

		uv_ex(w, x->a, st, false);
		t = *st, e = *st;
		t.live &= k != 0, e.live &= k != 1;
		ust b4 = *st, tb = { 0, 0, false }, eb = { 0, 0, false }, *ob = w->brk, *oc = w->cont;
		bool db = ob && ob == w->dobrk;	/* a break out of a do-while(0) still reaches what follows it */

		w->blk++, w->dep++;
		if (db)
			w->brk = w->dobrk = &tb, w->cont = oc == ob ? &tb : oc;
		uv_stmt(w, x->b, &t);
		w->blk++;
		if (db)
			w->brk = w->dobrk = &eb, w->cont = oc == ob ? &eb : oc;
		uv_stmt(w, x->c, &e);
		w->blk++, w->dep--, uv_drop(w);
		if (db) {
			w->brk = w->dobrk = ob, w->cont = oc;
			uj(ob, &tb), uj(ob, &eb);
			/* an arm that leaves by break while the other falls through: decided after the do */
			if (b4.live && k < 0 && !uv_haslogic(c, x->a) && (tb.live != eb.live) &&
			    (tb.live ? !t.live && e.live : !e.live && t.live)) {
				const ust *a = tb.live ? &tb : &eb;

				const ust *o = tb.live ? &e : &t;	/* the arm that falls through */

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
		else if (b4.live && k < 0 && !uv_haslogic(c, x->a)) {
			uint64_t du = b4.u & ~b4.i;	/* uninitialized before the if */
			uint64_t ti = t.i & ~t.u, tu = t.u & ~t.i, ei = e.i & ~e.u, eu = e.u & ~e.i;

			for (uint64_t m = du & ((ti & eu) | (tu & ei)); m; m &= m - 1) {
				int v = __builtin_ctzll(m);

				w->spend |= 1ull << v, w->sif[v] = n, w->sfalse[v] = (ti >> v) & 1, w->sdep[v] = w->dep, w->skind[v] = 0;
			}
		}
		*st = t;
		uj(st, &e);
		return;
	}
	case N_DO:
		if (uv_const(w, x->a) == 0) {	/* do-while(0): no back edge, a block whose breaks go to its end */
			ust brk = { 0, 0, false }, *sb = w->brk, *sc = w->cont, *sd = w->dobrk;
			uint64_t sm = w->dcm;

			w->brk = w->cont = w->dobrk = &brk, w->dcm = 0;
			uv_stmt(w, x->b, st);
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
			uj(st, &brk);
			uv_ex(w, x->a, st, false);
			return;
		}
		/* fall through */
	case N_WHILE: case N_FOR:
		if (x->op == N_FOR)
			uv_stmt(w, x->a, st);
		w->blk++;
		w->spend = 0, w->dep++;
		uv_loop(w, x, st);
		w->dep--, uv_drop(w);
		/* a loop that never loops back: its condition's false edge leaves it uninitialized (clang) */
		if (x->op != N_DO && (x->op == N_WHILE ? x->a : x->b) && uv_const(w, x->op == N_WHILE ? x->a : x->b) < 0)
			uv_pend(w, n, 6, &w->lp_head, &w->lp_brk, &w->lp_cx, false);
		w->blk++;
		return;
	case N_SWITCH: {
		ust *sb = w->brk, *si = w->swin, brk = { 0, 0, false }, in;
		bool sk = w->sw_const, sh = w->sw_hit, def;
		i128 sv = w->sw_val;
		cval cv;

		ust *ssb = w->swbrk;
		uint32_t ssl[8], snl = w->nswlab, sarm = w->swarm;
		uint64_t ssm = w->swmask, scand = w->swcand;

		memcpy(ssl, w->swlab, sizeof ssl);

		uv_ex(w, x->a, st, false);
		in = *st;
		w->spend = 0;
		w->brk = &brk, w->swin = &in;
		w->swbrk = &brk, w->nswlab = 0, w->swarm = 0, w->swmask = 0, w->swcand = 0;
		w->sw_const = cc_eval(c, x->a, &cv) && cv.kind == CV_INT;
		w->sw_val = w->sw_const ? cv.i : 0;
		w->sw_hit = w->sw_const && fl_case_match(&w->fl, x->b, w->sw_val);
		def = fl_has_default(&w->fl, x->b);
		def = w->sw_const ? (w->sw_hit || def) : def;
		st->live = false;
		w->dep++;
		uv_stmt(w, x->b, st);
		uv_swarm(w, st);
		w->dep--, uv_drop(w);
		w->blk++;
		uj(st, &brk);
		if (!def)
			uj(st, &in);
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
	case N_CASE: case N_DEFAULT:
		if (w->swbrk == w->brk) {	/* (a label of the switch being walked, not inside a loop of it) */
			/* a fallthrough into it continues the arm (case A: case B: ...: A's edge is named) */
			if (!st->live) {
				w->nswlab = 0, w->swarm++;
				w->swmask = w->swin ? w->swin->u & ~w->swin->i : 0;
			}
			if (w->nswlab < 8)
				w->swlab[w->nswlab++] = n;
		}
		if (w->swin) {
			bool here = true;

			if (w->sw_const && x->op == N_CASE)
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
				uj(st, w->swin);
			}
		}
		w->blk++;
		uv_stmt(w, x->c, st);
		return;
	case N_LABEL:
		if (x->a >= w->first && x->a - w->first < w->nlab) {
			uj(st, &w->lab[x->a - w->first]);
			w->seen[x->a - w->first] = 1;
		}
		w->spend = 0;
		w->blk++;
		uv_stmt(w, x->c, st);
		return;
	case N_GOTO:
		uv_goto(w, x->a, st);
		st->live = false;
		return;
	case N_GOTO_IND:
		uv_ex(w, x->a, st, false);
		for (uint32_t l = w->first; l < w->first + w->nlab; l++)
			if (c->labels[l].addr_taken)
				uv_goto(w, l, st);
		st->live = false;
		return;
	case N_BREAK:
		if (w->brk && w->brk == w->swbrk)
			uv_swarm(w, st);
		if (w->brk)
			uj(w->brk, st);
		st->live = false;
		return;
	case N_CONTINUE:
		if (w->cont)
			uj(w->cont, st);
		st->live = false;
		return;
	case N_ASM: {
		const casm *a = &c->asms[x->a];

		for (uint32_t i = a->nout; i < a->nout + a->nin; i++)
			uv_ex(w, a->ops[i].expr, st, false);
		for (uint32_t i = 0; i < a->nout; i++) {
			uint32_t e = a->ops[i].expr;
			const char *cs = a->ops[i].constraint ? c->strs[a->ops[i].constraint - 1].s : "";

			if (N(e)->op == N_VAR) {
				if (*cs == '+')
					uv_read(w, e, st);
				uv_write(w, e, st);
			} else
				uv_lv(w, e, st);
		}
		for (uint32_t i = 0; i < a->nlabel; i++)
			uv_goto(w, a->labels[i], st);
		return;
	}
	default:
		return;
	}
}

/* the end of a then/else statement as clang's getEndLoc: an expression statement ends with its expression */
static uint32_t uv_send(cc *c, uint32_t n)
{
	if (N(n)->op == N_EXPR && N(n)->a)
		return rng(c, N(n)->a).e;
	return pp_tok_end(c->p, c->span[n].e);
}

static bool uv_sometimes(cc *c, uwk *w, uint32_t k)
{
	static const char *const g[] = { "uninitialized", "all", "most", NULL };
	const sym *y = &c->syms[w->var[k]];
	const node *x = N(w->sif[k]);
	pp_span cr = rng(c, x->a), r[2];
	int nr = 1;
	bool f = w->sfalse[k];
	pp_dinfo d = { "note", w->uloc[k], &w->ur[k], 1, NULL, 0, NULL, 0 };

	static const char *const op[] = { "if", "&&", "||", "?:" };
	int kd = w->skind[k];

	if (kd == 7) {	/* each if, the later first (clang's CFG order) */
		bool any = false;

		w->skind[k] = 0;
		for (uint32_t j = w->ndl[k]; j-- > 0;) {
			w->sif[k] = w->dl[k][j], w->sfalse[k] = w->dlf[k][j];
			any |= uv_sometimes(c, w, k);
		}
		w->skind[k] = 7;
		return any;
	}

	if (kd == 1 || kd == 2 || kd == 3) {
		cr = rng(c, x->a);
		if (!cc_warnx(c, "sometimes-uninitialized", true, g, cr.b, &cr, 1, NULL, 0,
			      "variable '%.*s' is used uninitialized whenever '%s' condition is %s", IDF(y->name), op[kd], f ? "false" : "true"))
			return false;
		pp_diagx(c->p, &d, "uninitialized use occurs here");
		if (kd != 3)
			r[0] = (pp_span){ cr.b, rng(c, x->b).b };
		else if (f)
			r[0] = (pp_span){ cr.b, rng(c, x->b).b }, r[1] = (pp_span){ rng(c, x->b).e, rng(c, x->c).e }, nr = 2;
		else
			r[0] = (pp_span){ cr.b, rng(c, x->c).b };
		d = (pp_dinfo){ "note", r[0].b, r, nr, NULL, 0, NULL, 0 };
		pp_diagx(c->p, &d, "remove the '%s' if its condition is always %s", op[kd], f ? "true" : "false");
		return true;
	}
	if (kd == 6) {
		cr = rng(c, x->op == N_WHILE ? x->a : x->b);
		if (!cc_warnx(c, "sometimes-uninitialized", true, g, cr.b, &cr, 1, NULL, 0,
			      "variable '%.*s' is used uninitialized whenever '%s' loop exits because its condition is false",
			      IDF(y->name), x->op == N_WHILE ? "while" : "for"))
			return false;
		pp_diagx(c->p, &d, "uninitialized use occurs here");
		{
			pp_fixit fx = { cr.b, cr.e, "1" };	/* (while: the condition becomes 1; for: it is removed) */

			d = (pp_dinfo){ "note", cr.b, &cr, 1, &fx, x->op == N_WHILE, NULL, 0 };
			pp_diagx(c->p, &d, "remove the condition if it is always true");
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
			pp_span kr = uv_caserange(c, ln);

			if (!cc_warnx(c, "sometimes-uninitialized", true, g, kr.b, &kr, 1, NULL, 0,
				      "variable '%.*s' is used uninitialized whenever switch %s is taken", IDF(y->name),
				      N(ln)->op == N_CASE ? "case" : "default"))
				continue;
			d = (pp_dinfo){ "note", w->uloc[k], &w->ur[k], 1, NULL, 0, NULL, 0 };
			pp_diagx(c->p, &d, "uninitialized use occurs here");
			any = true;
		}
		return any;
	}
	if (!cc_warnx(c, "sometimes-uninitialized", true, g, cr.b, &cr, 1, NULL, 0,
		      "variable '%.*s' is used uninitialized whenever 'if' condition is %s", IDF(y->name), f ? "false" : "true"))
		return false;
	pp_diagx(c->p, &d, "uninitialized use occurs here");
	if (f) {
		r[0] = (pp_span){ c->span[w->sif[k]].b, c->span[x->b].b };
		if (x->c)	/* (after an expression statement: its ';') */
			r[1] = (pp_span){ N(x->b)->op == N_EXPR ? c->span[x->b].e : uv_send(c, x->b), uv_send(c, x->c) }, nr = 2;
	} else
		r[0] = (pp_span){ c->span[w->sif[k]].b, x->c ? c->span[x->c].b : N(x->b)->op == N_BLOCK ? uv_send(c, x->b) : c->span[x->b].e };	/* (no else: the whole if) */
	d = (pp_dinfo){ "note", r[0].b, r, nr, NULL, 0, NULL, 0 };
	pp_diagx(c->p, &d, "remove the 'if' if its condition is always %s", f ? "true" : "false");
	return true;
}

static const char *uv_zero(cc *c, qty t)
{
	int k = KIND(c, canon(c, t));

	if (k == TY_ENUM || ((k == TY_STRUCT || k == TY_UNION) && !is_complex(c, t)))
		return NULL;
	if (k >= TY_FLOAT16 && k <= TY_LDOUBLE)
		return " = 0.0";
	if (k == TY_BOOL && pp_defined(c->p, vintern_cstr("false")))
		return " = false";
	if (k == TY_PTR && pp_defined(c->p, vintern_cstr("NULL")))
		return " = NULL";
	if (k == TY_CHAR || k == TY_SCHAR || k == TY_UCHAR)
		return " = '\\0'";
	return " = 0";
}

VPRIV void uninit_check(cc *c, uint32_t body, uint32_t first_label)
{
	uwk ww, *w = &ww;
	ust st = { 0, 0, true }, small[16];
	uint8_t seen[64];
	uint32_t nl = c->nlabel - first_label;

	static const char *const gs[] = { "uninitialized", "all", "most", NULL };

	if (!pp_wstate(c->p, "uninitialized", false, w_all) && !pp_wstate(c->p, "sometimes-uninitialized", false, gs))
		return;	/* (-Wsometimes-uninitialized alone runs it too) */
	/* (only the head is cleared: the report arrays are written before they are read) */
	memset(w, 0, offsetof(uwk, uloc));
	w->c = c, w->fl.c = c;
	w->lab = nl <= 16 ? small : calloc(nl, sizeof(ust) + 1), w->first = first_label, w->nlab = nl;
	if (!w->lab)
		return;
	w->seen = nl <= 16 ? seen : (uint8_t *)(w->lab + nl);
	memset(small, 0, sizeof small);
	for (uint32_t i = 0; i < c->nuv; i++)
		uv_add(w, c->uv[i]);
	if (!w->nv && !(c->uv_any & 2)) {
		if (w->lab != small)
			free(w->lab);
		return;
	}
	for (int it = 0; it < 16; it++) {
		w->changed = false;
		w->report = true, w->nord = w->nself = 0, w->done = 0, w->blk = 0, w->spend = 0;
		memset(w->seen, 0, nl);
		st = (ust){ 0, 0, true };
		uv_stmt(w, body, &st);
		if (!w->changed)
			break;
	}
	for (uint32_t j = 0; j < w->nself; j++) {
		const sym *y = &c->syms[w->sself[j]];
		pp_span r[2] = { { y->loc, pp_tok_end(c->p, y->loc) }, w->sr[j] };

		cc_warnx(c, "uninitialized", false, w_all, w->sloc[j], r, 2, NULL, 0,
			 "variable '%.*s' is uninitialized when used within its own initialization", IDF(y->name));
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
		const char *z = uv_zero(c, y->ty);

		if (w->kind[k] == 2) {
			if (!uv_sometimes(c, w, k))
				continue;
		} else if (!cc_warnx(c, "uninitialized", false, w_all, w->uloc[k], &w->ur[k], 1, NULL, 0,
			      "variable '%.*s' is uninitialized when used here", IDF(y->name)))
			continue;
		if (z) {
			/* the declarator's end: the name, or ')' of a function pointer (not an asm label or attributes) */
			bool fp = KIND(c, canon(c, y->ty)) == TY_PTR && KIND(c, canon(c, T(c, canon(c, y->ty))->base)) == TY_FUNC;
			uint32_t e = pp_tok_end(c->p, fp && y->ple && !y->asmname && !y->attrs ? y->ple : y->loc);
			pp_fixit fx = { e, e, z };
			pp_dinfo d = { "note", e, NULL, 0, &fx, 1, NULL, 0 };

			pp_diagx(c->p, &d, "initialize the variable '%.*s' to silence this warning", IDF(y->name));
		} else
			note(c, y->plb ? y->plb : y->loc, "variable '%.*s' is declared here", IDF(y->name));
	}
	if (w->lab != small)
		free(w->lab);
}
