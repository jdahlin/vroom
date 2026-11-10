/*
 * -Wuninitialized and -Wsometimes-uninitialized (PLAN-diag.md D8.5), clang's
 * UninitializedValues over vcc's statement tree instead of a CFG.
 *
 * Tracked: block-scope scalar variables (integer, enum, floating, pointer)
 * declared without an initializer whose address is never taken, at most 64 per
 * function.  A state holds each one's value: initialized, uninitialized
 * (definitely, along every path here) or maybe.  A use of an uninitialized one
 * is "uninitialized when used here"; of a maybe one, "used uninitialized
 * whenever <branch>" when the maybe has a reason: the branch edge along which
 * the variable was definitely uninitialized and from which every path reaches
 * the use without an initialization (clang's rule).  The reason is the edge's
 * tag on the uninitialized value when the two sides meet; it is dropped when a
 * later branch, an exit or a loop between it and the use makes the use
 * conditional, and it holds only at the depth of the meeting (a use inside a
 * later branch's arm is not certain).  Loops run their body twice, the second
 * time from the fixed head state, and only that pass reports.  A backward
 * goto, an indirect one or asm goto: no warnings for the function.
 *
 * Each variable warns once, at its first use; the warnings go out at the end of
 * the body in declaration order, as clang's.  The path of a "sometimes" warning
 * is its notes: the branch, the use, the declaration (D8.2's numbered steps are
 * the labels here: (1) the edge, (2) the use).
 */
#include "ccimpl.h"

enum { UV_MAX = 64 };

/* why a maybe value is maybe: the edge (index + 1 into uw.rs; 0 none) */
typedef struct ureason {
	uint8_t kind;		/* R_* */
	uint8_t side;		/* if, ?:: the condition's value along the edge */
	uint32_t n;		/* the branch's node (case: the label) */
} ureason;

enum { R_IF, R_AND, R_OR, R_COND, R_LOOP, R_CASE, R_DEFAULT };

typedef struct ust {
	uint64_t u, m;		/* uninitialized, maybe (neither: initialized) */
	bool dead;
	uint16_t why[UV_MAX];	/* uninitialized: the edge's tag; maybe: its reason */
	uint8_t dep[UV_MAX];	/* maybe: the depth where it was made */
} ust;

typedef struct ufound {
	uint32_t use;		/* the use's node (0: none yet) */
	uint8_t kind;		/* 0 definite, 1 sometimes, 2 its own initialization */
	uint16_t why;
} ufound;

typedef struct ujump {
	ust brk, cont;
	const ust *pre;		/* the branch point of the breaks' join: the loop's head, the switch's entry */
	uint32_t loop;
	bool any_brk, any_cont;
} ujump;

typedef struct uw {
	uint32_t sym[UV_MAX];
	int nv;
	bool report;		/* the reporting pass (not a loop's first) */
	bool unsure;
	int depth;
	ureason *rs;
	uint32_t nrs, crs;
	ujump *jt;		/* the innermost loop or switch (break; continue: the loop's) */
	ujump *lj;		/* the innermost loop */
	ust *sw;		/* the innermost switch's entry state */
	ust *lab;		/* labels: the state of the gotos so far */
	uint8_t *seen;		/* label passed */
	uint32_t lab0, nlab;
	int loops;		/* loop nesting (deeper ones: every value maybe) */
	ufound found[UV_MAX];	/* (last: only nv of them cleared) */
} uw;

static const char *const w_uninit[] = { "uninitialized", "most", "all", NULL };
static const char *const w_some[] = { "sometimes-uninitialized", "uninitialized", "most", "all", NULL };

static int uv_index(const uw *w, uint32_t s)
{
	for (int i = 0; i < w->nv; i++)
		if (w->sym[i] == s)
			return i;
	return -1;
}

static uint16_t reason(uw *w, int kind, uint32_t n, int side)
{
	for (uint32_t i = 0; i < w->nrs; i++)
		if (w->rs[i].kind == kind && w->rs[i].n == n && w->rs[i].side == side)
			return (uint16_t)(i + 1);
	if (w->nrs == 0xfffe)
		return 0;
	if (w->nrs == w->crs)
		w->rs = realloc(w->rs, (w->crs = w->crs ? w->crs * 2 : 16) * sizeof(ureason));
	w->rs[w->nrs] = (ureason){ (uint8_t)kind, (uint8_t)side, n };
	return (uint16_t)++w->nrs;
}

/* the uninitialized values of s leave along an edge: they carry its tag */
static void utag(uw *w, ust *s, uint16_t r)
{
	for (uint64_t b = s->u; b; b &= b - 1)
		s->why[__builtin_ctzll(b)] = r;
}

static uint8_t udepth(const uw *w)
{
	return (uint8_t)(w->depth > 255 ? 255 : w->depth);
}

/* a join: the maybe values are at its depth at most (one made outside a region the use
   is in keeps its depth: the region may be skipped) */
static void stamp(const uw *w, ust *s)
{
	uint8_t d = udepth(w);

	for (uint64_t x = s->m; x; x &= x - 1)
		if (s->dep[__builtin_ctzll(x)] > d)
			s->dep[__builtin_ctzll(x)] = d;
}

/* a branch whose arms were entered from p: the maybe values with a reason made before it
   no longer reach the use for certain unless both arms keep them */
static void meet(uw *w, ust *a, const ust *b, const ust *p)
{
	if (b->dead)
		goto one;
	if (a->dead) {
		*a = *b;
		goto one;
	}
	for (int i = 0; i < w->nv; i++) {
		uint64_t bit = 1ull << i;
		bool au = a->u & bit, bu = b->u & bit, am = a->m & bit, bm = b->m & bit;
		bool pm = p && (p->m & bit);
		uint8_t d;

		if (!au && !am && !bu && !bm)
			continue;	/* initialized on both */
		if (au && bu) {
			a->why[i] = a->why[i] == b->why[i] ? a->why[i] : p ? p->why[i] : 0;
			continue;
		}
		a->u &= ~bit, a->m |= bit;
		if (au || bu) {	/* uninitialized on one side only */
			uint16_t t = au ? a->why[i] : b->why[i];

			if (am || bm)	/* the other is maybe: keep its reason (made inside) */
				t = am ? a->why[i] : b->why[i];
			else if (pm)
				t = 0;
			a->why[i] = t;
		} else if (am && bm) {
			a->why[i] = a->why[i] == b->why[i] ? a->why[i] : 0;
		} else {	/* maybe and initialized */
			a->why[i] = pm || !p ? 0 : am ? a->why[i] : b->why[i];
		}
		d = udepth(w);
		if (am && a->dep[i] < d)
			d = a->dep[i];
		if (bm && b->dep[i] < d)
			d = b->dep[i];
		a->dep[i] = d;
	}
	return;
one:	/* one side dead: a reason made before the branch no longer holds */
	if (p)
		for (uint64_t x = a->m & p->m; x; x &= x - 1)
			a->why[__builtin_ctzll(x)] = 0;
	stamp(w, a);
}

/* entering a loop or a region the use may skip: every reason made so far lapses */
static void lapse(uw *w, ust *s)
{
	(void)w;
	for (uint64_t x = s->m; x; x &= x - 1)
		s->why[__builtin_ctzll(x)] = 0;
}

static void use(cc *c, uw *w, ust *s, uint32_t n, int i)
{
	uint64_t bit = 1ull << i;

	if (s->dead || !w->report || w->found[i].use)
		return;
	if (s->u & bit)
		w->found[i] = (ufound){ n, 0, 0 };
	else if ((s->m & bit) && s->why[i] && s->dep[i] == (w->depth > 255 ? 255 : w->depth))
		w->found[i] = (ufound){ n, 1, s->why[i] };
	(void)c;
}

static void def(ust *s, int i)
{
	s->u &= ~(1ull << i), s->m &= ~(1ull << i);
}

static void u_stmt(cc *c, uw *w, uint32_t n, ust *s);
static void u_expr(cc *c, uw *w, uint32_t n, ust *s);

static int tracked(cc *c, uw *w, uint32_t n)
{
	while (N(n)->op == N_CAST && !c->span[n].e && N(N(n)->a)->op == N_VAR)
		n = N(n)->a;
	return N(n)->op == N_VAR ? uv_index(w, N(n)->a) : -1;
}

/* a condition: the states along its true and false edges */
static void u_cond(cc *c, uw *w, uint32_t n, ust *s, ust *t, ust *f)
{
	const node *x = N(n);

	if (x->op == N_LOGAND || x->op == N_LOGOR) {
		ust at, af, bt, bf, p, q;
		bool isand = x->op == N_LOGAND;

		u_cond(c, w, x->a, s, &at, &af);
		p = q = isand ? at : af;	/* (the branch point) */
		w->depth++;
		u_cond(c, w, x->b, &q, &bt, &bf);
		w->depth--;
		/* &&: the false edge of a skips b; ||: the true edge does */
		utag(w, isand ? &af : &at, reason(w, isand ? R_AND : R_OR, n, 0));
		if (isand) {
			*t = bt;
			meet(w, &af, &bf, &p);
			*f = af;
		} else {
			*f = bf;
			meet(w, &at, &bt, &p);
			*t = at;
		}
		*s = p;
		return;
	}
	if (x->op == N_UNARY && x->aux == T_EXCLAIM) {
		u_cond(c, w, x->a, s, f, t);
		return;
	}
	u_expr(c, w, n, s);
	*t = *f = *s;
}

static void u_call_args(cc *c, uw *w, const node *x, ust *s)
{
	for (uint32_t i = 0; i < x->c; i++)
		u_expr(c, w, c->lists[x->b + i], s);
}

static void u_expr(cc *c, uw *w, uint32_t n, ust *s)
{
	const node *x;
	int i;

	if (!n || s->dead)
		return;
	x = N(n);
	switch (x->op) {
	case N_VAR:
		if ((i = uv_index(w, x->a)) >= 0)
			use(c, w, s, n, i);
		return;
	case N_ASSIGN:
		u_expr(c, w, x->b, s);
		if ((i = tracked(c, w, x->a)) >= 0) {
			if (x->aux)
				use(c, w, s, x->a, i);
			def(s, i);
		} else {
			u_expr(c, w, x->a, s);
		}
		return;
	case N_PREINC: case N_PREDEC: case N_POSTINC: case N_POSTDEC:
		u_expr(c, w, x->a, s);
		if ((i = tracked(c, w, x->a)) >= 0)
			def(s, i);
		return;
	case N_LOGAND: case N_LOGOR: {
		ust t, f;

		u_cond(c, w, n, s, &t, &f);
		meet(w, &t, &f, NULL);
		*s = t;
		return;
	}
	case N_COND: {
		ust t, f, p;
		int k = cond_value(c, x->a);

		if (!x->b) {	/* a ?: c */
			u_cond(c, w, x->a, s, &t, &f);
			w->depth++;
			u_expr(c, w, x->c, &f);
			w->depth--;
			meet(w, &t, &f, NULL);
			*s = t;
			return;
		}
		u_cond(c, w, x->a, s, &t, &f);
		p = *s;
		if (k == 0)
			t.dead = true;
		if (k == 1)
			f.dead = true;
		utag(w, &t, reason(w, R_COND, n, 1));
		utag(w, &f, reason(w, R_COND, n, 0));
		w->depth++;
		u_expr(c, w, x->b, &t);
		u_expr(c, w, x->c, &f);
		w->depth--;
		meet(w, &t, &f, &p);
		*s = t;
		return;
	}
	case N_CALL: {
		const node *g = N(x->a);

		u_expr(c, w, x->a, s);
		u_call_args(c, w, x, s);
		while (g->op == N_CAST)
			g = N(g->a);
		if (g->op == N_VAR && (c->syms[g->a].flags & SF_NORETURN))
			s->dead = true;
		return;
	}
	case N_BUILTIN:
		if (x->aux == BI_CONSTANT_P || x->aux == BI_TYPES_COMPAT || x->aux == BI_OFFSETOF ||
		    x->aux == BI_CHOOSE || x->aux == BI_OBJECT_SIZE || x->aux == BI_DYNAMIC_OBJECT_SIZE)
			return;	/* (not evaluated, or not a read) */
		if (x->aux == BI_VA_START || x->aux == BI_VA_COPY || x->aux == BI_VA_END) {
			/* (a va_list by reference, as clang: not a read; start, copy: written) */
			for (uint32_t k = 0; k < x->c; k++) {
				uint32_t a = c->lists[x->b + k];
				int i = tracked(c, w, a);

				if (i < 0)
					u_expr(c, w, a, s);
				else if (k == 0 && x->aux != BI_VA_END)
					def(s, i);
			}
			return;
		}
		u_call_args(c, w, x, s);
		if (x->aux == BI_UNREACHABLE || x->aux == BI_TRAP)
			s->dead = true;
		return;
	case N_STMTEXPR:
		u_stmt(c, w, x->a, s);
		return;
	case N_COMMA: case N_BINARY: case N_INDEX:
		u_expr(c, w, x->a, s);
		u_expr(c, w, x->b, s);
		return;
	case N_CAST:
		if (KIND(c, x->ty) == TY_VOID && tracked(c, w, x->a) >= 0)
			return;	/* (void)x: not a read */
		u_expr(c, w, x->a, s);
		return;
	case N_UNARY: case N_DEREF: case N_MEMBER: case N_ADDR: case N_VAARG: case N_SIZEOF_VLA:
		u_expr(c, w, x->a, s);
		return;
	case N_COMPLIT:
		u_expr(c, w, x->a, s);
		return;
	case N_INIT:	/* struct, union: c values; array: c (lo, hi, value) */
		for (uint32_t k = 0; k < x->c; k++)
			u_expr(c, w, c->lists[x->aux == 1 ? x->b + 3 * k + 2 : x->b + k], s);
		return;
	case N_CPLX:
		u_expr(c, w, x->a, s);
		u_expr(c, w, x->b, s);
		return;
	default:
		return;
	}
}

static void jump_to(uw *w, ust *into, bool *any, const ust *s, const ust *pre)
{
	if (s->dead)
		return;
	if (!*any)
		*into = *s, *any = true;
	else
		meet(w, into, s, pre);
}

static void u_label(uw *w, uint32_t l, ust *s)
{
	uint32_t k = l - w->lab0;

	if (l < w->lab0 || k >= w->nlab)
		return;
	w->seen[k] = 1;
	if (!w->lab[k].dead) {
		ust g = w->lab[k];

		if (s->dead)
			*s = g;
		else
			meet(w, s, &g, NULL);
	}
}

/* the variables a subtree may write: the nodes u_stmt and u_expr walk, and every def()
   they make (a superset: a loop that writes none of its entry's uninitialized values
   needs no first pass, its head is its entry) */
static uint64_t defs(cc *c, uw *w, uint32_t n, int depth)
{
	const node *x;
	uint64_t m = 0;
	int i;

	if (!n)
		return 0;
	if (depth > 200)
		return ~0ull;
	x = N(n);
	depth++;
	switch (x->op) {
	case N_ASSIGN:
		if ((i = tracked(c, w, x->a)) >= 0)
			m = 1ull << i;
		else
			m = defs(c, w, x->a, depth);
		return m | defs(c, w, x->b, depth);
	case N_PREINC: case N_PREDEC: case N_POSTINC: case N_POSTDEC:
		if ((i = tracked(c, w, x->a)) >= 0)
			m = 1ull << i;
		return m | defs(c, w, x->a, depth);
	case N_DECL:
		if (x->b && (i = uv_index(w, x->a)) >= 0)
			m = 1ull << i;
		return m | defs(c, w, x->b, depth);
	case N_CALL:
		m = defs(c, w, x->a, depth);
		/* fall through */
	case N_BUILTIN:
		for (uint32_t k = 0; k < x->c; k++) {
			uint32_t a = c->lists[x->b + k];

			if (x->op == N_BUILTIN && k == 0 && (x->aux == BI_VA_START || x->aux == BI_VA_COPY) &&
			    (i = tracked(c, w, a)) >= 0)
				m |= 1ull << i;
			else
				m |= defs(c, w, a, depth);
		}
		return m;
	case N_ASM: {
		const casm *a = &c->asms[x->a];

		for (uint32_t k = 0; k < a->nout + a->nin; k++)
			if (k < a->nout && (i = tracked(c, w, a->ops[k].expr)) >= 0)
				m |= 1ull << i;
			else
				m |= defs(c, w, a->ops[k].expr, depth);
		return m;
	}
	case N_INIT:
		for (uint32_t k = 0; k < x->c; k++)
			m |= defs(c, w, c->lists[x->aux == 1 ? x->b + 3 * k + 2 : x->b + k], depth);
		return m;
	case N_BLOCK:
		for (uint32_t k = 0; k < x->c; k++)
			m |= defs(c, w, c->lists[x->b + k], depth);
		return m;
	case N_FOR:
		return defs(c, w, x->a, depth) | defs(c, w, x->b, depth) |
		       defs(c, w, c->lists[x->c], depth) | defs(c, w, c->lists[x->c + 1], depth);
	case N_IF: case N_COND:
		return defs(c, w, x->a, depth) | defs(c, w, x->b, depth) | defs(c, w, x->c, depth);
	case N_COMMA: case N_BINARY: case N_INDEX: case N_LOGAND: case N_LOGOR: case N_CPLX:
	case N_WHILE: case N_DO: case N_SWITCH:
		return defs(c, w, x->a, depth) | defs(c, w, x->b, depth);
	case N_CAST: case N_UNARY: case N_DEREF: case N_MEMBER: case N_ADDR: case N_VAARG: case N_SIZEOF_VLA:
	case N_COMPLIT: case N_STMTEXPR: case N_EXPR: case N_RETURN:
		return defs(c, w, x->a, depth);
	case N_CASE: case N_DEFAULT: case N_LABEL:
		return defs(c, w, x->c, depth);
	default:
		return 0;
	}
}

/* a loop: the body twice when it can repeat, quietly from the entry, then from the head
   (the entry meets the back edge) reporting; labels and gotos of the first pass are undone */
static void u_loop(cc *c, uw *w, uint32_t n, uint32_t cnd, uint32_t body, uint32_t step, bool test_first, ust *s)
{
	ujump j, *oj = w->jt, *ol = w->lj;
	int k = cond_value(c, cnd);
	ust head = *s, t, f, b, exit;

	lapse(w, &head);
	if (++w->loops > 6) {	/* (deep nests: every value maybe) */
		head.m |= head.u, head.u = 0;
		lapse(w, &head);
	}
	w->jt = w->lj = &j;
	w->depth++;
	if (k != 0 && !head.dead && (head.u & (defs(c, w, cnd, 0) | defs(c, w, body, 0) | defs(c, w, step, 0)))) {
		bool orep = w->report;
		ust *lab = w->nlab ? malloc(w->nlab * (sizeof(ust) + 1)) : NULL;

		if (lab) {
			memcpy(lab, w->lab, w->nlab * sizeof(ust));
			memcpy(lab + w->nlab, w->seen, w->nlab);
		}
		j = (ujump){ .loop = n, .pre = &head };
		j.brk.dead = j.cont.dead = true;
		w->report = false;
		b = head;
		if (test_first)
			u_cond(c, w, cnd, &b, &t, &f), b = t;
		u_stmt(c, w, body, &b);
		jump_to(w, &j.cont, &j.any_cont, &b, j.pre);
		if (j.any_cont) {
			b = j.cont;
			if (test_first)
				u_expr(c, w, step, &b);
			else
				u_cond(c, w, cnd, &b, &t, &f), b = t;
			if (!b.dead)
				meet(w, &head, &b, NULL);
			lapse(w, &head);
		}
		w->report = orep;
		if (lab) {
			memcpy(w->lab, lab, w->nlab * sizeof(ust));
			memcpy(w->seen, lab + w->nlab, w->nlab);
			free(lab);
		}
	}
	j = (ujump){ .loop = n, .pre = &head };
	j.brk.dead = j.cont.dead = true;
	b = head;
	exit = head;
	exit.dead = true;
	if (test_first) {
		u_cond(c, w, cnd, &b, &t, &f);
		if (k == 1)
			f.dead = true;
		if (k == 0)
			t.dead = true;
		exit = f;
		b = t;
	}
	u_stmt(c, w, body, &b);
	jump_to(w, &j.cont, &j.any_cont, &b, j.pre);
	if (j.any_cont) {
		b = j.cont;
		if (test_first) {
			u_expr(c, w, step, &b);
		} else {
			u_cond(c, w, cnd, &b, &t, &f);
			if (k == 1)
				f.dead = true;
			exit = f;
		}
	}
	w->depth--;
	if (!exit.dead && cnd && k < 0)
		utag(w, &exit, reason(w, R_LOOP, n, 0));
	if (j.any_brk) {
		bool any = !exit.dead;

		jump_to(w, &exit, &any, &j.brk, j.pre);
	}
	*s = exit;
	if (!s->dead)	/* (what leaves is at the loop's depth) */
		stamp(w, s);
	w->jt = oj, w->lj = ol;
	w->loops--;
}

static void u_stmt(cc *c, uw *w, uint32_t n, ust *s)
{
	const node *x;
	int i;

	if (!n)
		return;
	x = N(n);
	switch (x->op) {
	case N_BLOCK:
		for (uint32_t k = 0; k < x->c; k++)
			u_stmt(c, w, c->lists[x->b + k], s);
		return;
	case N_EXPR:
		u_expr(c, w, x->a, s);
		return;
	case N_DECL:
		if ((i = uv_index(w, x->a)) >= 0) {
			if (x->b) {	/* int x = x: its own initialization */
				uint64_t bit = 1ull << i;

				s->u |= bit, s->m &= ~bit, s->why[i] = 0;
				if (!s->dead && w->report && !w->found[i].use) {
					u_expr(c, w, x->b, s);
					if (w->found[i].use)
						w->found[i].kind = 2;
				} else {
					u_expr(c, w, x->b, s);
				}
				def(s, i);
			} else if (!s->dead) {
				s->u |= 1ull << i, s->m &= ~(1ull << i), s->why[i] = 0;
			}
			return;
		}
		u_expr(c, w, x->b, s);
		return;
	case N_IF: {
		ust t, f, p;
		int k = cond_value(c, x->a);

		u_cond(c, w, x->a, s, &t, &f);
		p = *s;
		if (k == 0)
			t.dead = true;
		if (k == 1)
			f.dead = true;
		utag(w, &t, reason(w, R_IF, n, 1));
		utag(w, &f, reason(w, R_IF, n, 0));
		w->depth++;
		u_stmt(c, w, x->b, &t);
		u_stmt(c, w, x->c, &f);
		w->depth--;
		meet(w, &t, &f, &p);
		*s = t;
		return;
	}
	case N_WHILE:
		u_loop(c, w, n, x->a, x->b, 0, true, s);
		return;
	case N_DO:
		u_loop(c, w, n, x->a, x->b, 0, false, s);
		return;
	case N_FOR:
		if (x->a)
			u_stmt(c, w, x->a, s);
		u_loop(c, w, n, x->b, c->lists[x->c + 1], c->lists[x->c], true, s);
		return;
	case N_SWITCH: {
		ujump j = { .loop = 0 }, *oj = w->jt;
		ust entry, *osw = w->sw, end;
		bool any = false;

		u_expr(c, w, x->a, s);
		entry = *s;
		lapse(w, &entry);
		j.brk.dead = true;
		j.pre = &entry;
		w->jt = &j;
		w->sw = &entry;
		w->depth++;
		end = entry;
		end.dead = true;	/* (the body is entered at its labels) */
		u_stmt(c, w, x->b, &end);
		w->depth--;
		w->sw = osw, w->jt = oj;
		if (!entry.dead && !has_default(c, x->b) && !enum_covered(c, x)) {
			ust e = entry;	/* (no case matches: no edge of its own, clang says nothing) */

			any = !end.dead;
			utag(w, &e, 0);
			jump_to(w, &end, &any, &e, &entry);
		}
		if (j.any_brk) {
			any = !end.dead;
			jump_to(w, &end, &any, &j.brk, &entry);
		}
		*s = end;
		if (!s->dead)
			stamp(w, s);
		return;
	}
	case N_CASE: case N_DEFAULT:
		if (w->sw && !w->sw->dead) {
			ust e = *w->sw;

			utag(w, &e, reason(w, x->op == N_CASE ? R_CASE : R_DEFAULT, n, 0));
			if (s->dead)
				*s = e;
			else
				meet(w, s, &e, w->sw);
		}
		u_stmt(c, w, x->c, s);
		return;
	case N_LABEL:
		u_label(w, x->a, s);
		u_stmt(c, w, x->c, s);
		return;
	case N_GOTO: {
		uint32_t k = x->a - w->lab0;

		if (!s->dead && x->a >= w->lab0 && k < w->nlab) {
			if (w->seen[k])
				w->unsure = true;	/* backward */
			else if (w->lab[k].dead)
				w->lab[k] = *s;
			else
				meet(w, &w->lab[k], s, NULL);
		}
		s->dead = true;
		return;
	}
	case N_GOTO_IND:
		w->unsure = true;
		s->dead = true;
		return;
	case N_BREAK:
		if (w->jt)
			jump_to(w, &w->jt->brk, &w->jt->any_brk, s, w->jt->pre);
		s->dead = true;
		return;
	case N_CONTINUE:
		if (w->lj)
			jump_to(w, &w->lj->cont, &w->lj->any_cont, s, w->lj->pre);
		s->dead = true;
		return;
	case N_RETURN:
		u_expr(c, w, x->a, s);
		s->dead = true;
		return;
	case N_ASM: {
		const casm *a = &c->asms[x->a];

		if (a->nlabel)
			w->unsure = true;
		for (uint32_t k = a->nout; k < a->nout + a->nin; k++)
			u_expr(c, w, a->ops[k].expr, s);
		for (uint32_t k = 0; k < a->nout; k++) {	/* outputs ("+r": read too) */
			const char *cs = c->strs[a->ops[k].constraint - 1].s;

			if ((i = tracked(c, w, a->ops[k].expr)) >= 0) {
				if (cs && strchr(cs, '+'))
					use(c, w, s, a->ops[k].expr, i);
				def(s, i);
			} else {
				u_expr(c, w, a->ops[k].expr, s);
			}
		}
		return;
	}
	default:
		return;
	}
}

/* ---------------- the warnings ---------------- */

/* an expression's first token (an assignment's or a comma's range starts at its operator) */
static uint32_t expr_begin(cc *c, uint32_t n, int depth)
{
	const node *x = N(n);
	pp_span r = rng(c, n);
	uint32_t b = r.b;

	if (depth < 32 && x->a && (x->op == N_ASSIGN || x->op == N_COMMA || x->op == N_BINARY || x->op == N_INDEX ||
				   x->op == N_COND || x->op == N_LOGAND || x->op == N_LOGOR || x->op == N_POSTINC ||
				   x->op == N_POSTDEC || x->op == N_MEMBER || x->op == N_CALL)) {
		uint32_t a = expr_begin(c, x->a, depth + 1), e = a;

		pp_widen_parens(c->p, &a, &e);
		b = a < b ? a : b;
	}
	return b;
}

/* a simple statement's first token and its end before the ';' (clang's statement range) */
static bool stmt_span(cc *c, uint32_t n, uint32_t *b, uint32_t *e)
{
	const node *x = N(n);
	size_t len;
	const char *t;

	switch (x->op) {
	case N_EXPR: {
		pp_span r = rng(c, x->a);

		*b = expr_begin(c, x->a, 0), *e = r.e;
		return r.e > *b;
	}
	case N_BREAK: case N_CONTINUE: case N_GOTO: case N_RETURN:
		*b = x->loc;
		if (x->op == N_RETURN && x->a) {
			pp_span r = rng(c, x->a);

			pp_widen_parens(c->p, &r.b, &r.e);
			*e = r.e;
			return true;
		}
		t = pp_src_rest(c->p, x->loc, &len);	/* to the ';' */
		for (size_t i = 0; t && i < len && i < 256; i++)
			if (t[i] == ';') {
				while (i && (t[i - 1] == ' ' || t[i - 1] == '\t'))
					i--;
				*e = x->loc + (uint32_t)i;
				return true;
			}
		return false;
	default:
		return false;
	}
}

static void cond_span(cc *c, uint32_t n, uint32_t *b, uint32_t *e)
{
	pp_span r = rng(c, n);

	*b = r.b, *e = r.e;
}

/* clang's getFixItZeroInitializerForType: " = 0", " = 0.0", " = '\0'", " = NULL" (NULL
   defined) or none (an enum: "declared here") */
static const char *zero_init(cc *c, qty t)
{
	int k = KIND(c, t);

	if (k == TY_CHAR || k == TY_SCHAR || k == TY_UCHAR)
		return " = '\\0'";
	if (k >= TY_FLOAT16 && k <= TY_LDOUBLE)
		return " = 0.0";
	if (k == TY_PTR)
		return pp_macro_defined(c->p, "NULL") ? " = NULL" : " = 0";
	if (k == TY_ENUM)
		return NULL;
	return " = 0";
}

static void init_note(cc *c, vdiag *d, const sym *y)
{
	const char *z = zero_init(c, y->ty);
	uint32_t e = pp_tok_end(c->p, y->loc);

	if (!z) {
		pp_diag_note(c->p, d, y->ple ? y->plb : y->loc, "variable '%.*s' is declared here", IDF(y->name));
		return;
	}
	pp_diag_fix(c->p, pp_diag_note(c->p, d, e, "initialize the variable '%.*s' to silence this warning",
				       IDF(y->name)), e, e, z, VDA_MAYBE);
}

static void uv_report(cc *c, uw *w, int i)
{
	const ufound *u = &w->found[i];
	const sym *y = &c->syms[w->sym[i]];
	pp_span r = rng(c, u->use);
	vdiag *d;

	if (u->kind != 1) {
		d = cc_warn_begin(c, "uninitialized", false, w_uninit, r.b, u->kind == 2
				  ? "variable '%.*s' is uninitialized when used within its own initialization"
				  : "variable '%.*s' is uninitialized when used here", IDF(y->name));
		if (!d)
			return;
		pp_diag_label(c->p, d, r.b, r.e, NULL);
		if (u->kind == 0)
			init_note(c, d, y);
		pp_diag_emit(c->p, d);
		return;
	}
	{
		const ureason *q = &w->rs[u->why - 1];
		const node *x = N(q->n);
		uint32_t at, b, e, kw = x->loc;
		char what[96];
		const char *rm = NULL, *rv = "true";	/* rm: the keyword ("": a loop's condition) */

		switch (q->kind) {
		case R_IF:
			cond_span(c, x->a, &b, &e);
			at = b;
			snprintf(what, sizeof what, "'if' condition is %s", q->side ? "true" : "false");
			rm = "if", rv = q->side ? "false" : "true";
			break;
		case R_AND: case R_OR:
			cond_span(c, x->a, &b, &e);
			at = kw = b;
			snprintf(what, sizeof what, "'%s' condition is %s", q->kind == R_AND ? "&&" : "||",
				 q->kind == R_AND ? "false" : "true");
			rm = q->kind == R_AND ? "&&" : "||", rv = q->kind == R_AND ? "true" : "false";
			break;
		case R_COND:
			cond_span(c, x->a, &b, &e);
			at = kw = b;
			snprintf(what, sizeof what, "'?:' condition is %s", q->side ? "true" : "false");
			rm = "?:", rv = q->side ? "false" : "true";
			break;
		case R_LOOP:
			cond_span(c, x->op == N_FOR ? x->b : x->a, &b, &e);
			at = kw = b;
			snprintf(what, sizeof what, "'%s' loop exits because its condition is false",
				 x->op == N_FOR ? "for" : x->op == N_DO ? "do" : "while");
			rm = "";
			break;
		default:	/* a case: at its value (clang: the label's expression) */
			at = x->loc, b = e = 0;
			if (q->kind == R_CASE) {
				size_t len;
				const char *t = pp_src_rest(c->p, pp_tok_end(c->p, x->loc), &len);
				uint32_t i = 0;

				while (t && i < len && i < 64 && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n'))
					i++;
				if (t && i < len)
					at = pp_tok_end(c->p, x->loc) + i;
			}
			snprintf(what, sizeof what, "switch %s is taken", q->kind == R_CASE ? "case" : "default");
			break;
		}
		d = cc_warn_begin(c, "sometimes-uninitialized", false, w_some, at,
				  "variable '%.*s' is used uninitialized whenever %s", IDF(y->name), what);
		if (!d)
			return;
		if (e > b)	/* (the use: the note, drawn here in the vcc style) */
			pp_diag_label(c->p, d, b, e, "this edge leaves it uninitialized");
		pp_diag_note(c->p, d, r.b, "uninitialized use occurs here");
		if (rm) {
			vdiag *k = *rm ? pp_diag_note(c->p, d, kw, "remove the '%s' if its condition is always %s", rm, rv)
				 : pp_diag_note(c->p, d, kw, "remove the condition if it is always true");

			/* clang's fix-its: drop the test (if, &&, ||, ?:) or make it 1 (a loop) */
			if (q->kind == R_LOOP) {
				pp_diag_fix(c->p, k, b, e, "1", VDA_MAYBE);
			} else if (q->kind == R_IF && !x->c && x->b) {
				uint32_t sb, se;

				if (stmt_span(c, x->b, &sb, &se))	/* the test, or the whole if */
					pp_diag_fix(c->p, k, x->loc, q->side ? se : sb, "", VDA_MAYBE);
			} else if ((q->kind == R_AND || q->kind == R_OR) && x->b) {
				/* (clang: the left operand and the operator) */
				pp_diag_fix(c->p, k, b, pp_tok_end(c->p, x->loc), "", VDA_MAYBE);
			} else if (q->kind == R_COND && x->b) {
				pp_span tb = rng(c, x->b), tc = rng(c, x->c);

				pp_widen_parens(c->p, &tb.b, &tb.e);
				pp_widen_parens(c->p, &tc.b, &tc.e);
				if (q->side) {	/* always false: the false arm stays */
					pp_diag_fix(c->p, k, b, tc.b, "", VDA_MAYBE);
				} else {
					pp_diag_fix(c->p, k, b, tb.b, "", VDA_MAYBE);
					pp_diag_fix(c->p, k, tb.e, tc.e, "", VDA_MAYBE);
				}
			}
		}
		init_note(c, d, y);
		pp_diag_emit(c->p, d);
	}
}

/* e names s (int x = x + 1): clang tracks x through its own initialization */
static bool self_ref(cc *c, uint32_t e, uint32_t s, int depth)
{
	const node *x;

	if (!e || depth > 32)
		return false;
	x = N(e);
	if (x->op == N_VAR)
		return x->a == s;
	if (x->op == N_CAST || x->op == N_UNARY || x->op == N_BINARY || x->op == N_COMMA || x->op == N_COND)
		return self_ref(c, x->a, s, depth + 1) || (x->op != N_CAST && x->op != N_UNARY &&
			(self_ref(c, x->b, s, depth + 1) || (x->op == N_COND && self_ref(c, x->c, s, depth + 1))));
	return false;
}

/* the body's tracked variables: block-scope scalars without an initializer whose address is
   never taken */
static void collect(cc *c, uw *w, uint32_t n, int depth)
{
	const node *x;

	if (!n || depth > 256 || w->nv == UV_MAX)
		return;
	x = N(n);
	switch (x->op) {
	case N_BLOCK:
		for (uint32_t k = 0; k < x->c; k++)
			collect(c, w, c->lists[x->b + k], depth + 1);
		return;
	case N_DECL: {
		const sym *y = &c->syms[x->a];
		int k = KIND(c, y->ty);

		if ((!x->b || self_ref(c, x->b, x->a, 0)) && y->kind == S_VAR && !(y->flags & (SF_ADDR_TAKEN | SF_GLOBAL | SF_PARAM | SF_INVALID)) &&
		    (y->sc == SC_AUTO || y->sc == SC_REGISTER || y->sc == SC_NONE) &&
		    (is_int_kind(k) || k == TY_ENUM || (k >= TY_FLOAT16 && k <= TY_LDOUBLE) || k == TY_PTR) &&
		    uv_index(w, x->a) < 0)
			w->sym[w->nv++] = x->a;
		return;
	}
	case N_IF:
		collect(c, w, x->b, depth + 1);
		collect(c, w, x->c, depth + 1);
		return;
	case N_WHILE: case N_DO:
		collect(c, w, x->b, depth + 1);
		return;
	case N_FOR:
		collect(c, w, x->a, depth + 1);
		collect(c, w, c->lists[x->c + 1], depth + 1);
		return;
	case N_SWITCH:
		collect(c, w, x->b, depth + 1);
		return;
	case N_CASE: case N_DEFAULT: case N_LABEL:
		collect(c, w, x->c, depth + 1);
		return;
	default:
		return;
	}
}

VPRIV __attribute__((noinline)) void uninit_check(cc *c, uint32_t body, uint32_t lab0, uint32_t loc)
{
	uw w;
	ust s;
	uint8_t seen[64];
	ust small[4];

	/* (the command line's answer once per TU: each of the many functions a header defines
	   asks; a #pragma diagnostic makes it a question per location) */
	if (unlikely(pp_diag_npragma(c->p))) {
		if (pp_diag_wlevel(c->p, "uninitialized", false, w_uninit, loc) < 0 &&
		    pp_diag_wlevel(c->p, "sometimes-uninitialized", false, w_some, loc) < 0)
			return;
	} else {
		if (!c->uninit_on)
			c->uninit_on = pp_diag_wlevel(c->p, "uninitialized", false, w_uninit, UINT32_MAX) >= 0 ||
				       pp_diag_wlevel(c->p, "sometimes-uninitialized", false, w_some, UINT32_MAX) >= 0 ? 1 : -1;
		if (c->uninit_on < 0)
			return;
	}
	w.nv = 0;	/* (most functions track nothing: the rest is cleared after) */
	collect(c, &w, body, 0);
	if (!w.nv)
		return;
	memset((char *)&w + offsetof(uw, nv) + sizeof w.nv, 0, offsetof(uw, found) - offsetof(uw, nv) - sizeof w.nv);
	memset(w.found, 0, w.nv * sizeof *w.found);
	memset(&s, 0, sizeof s);
	w.lab0 = lab0, w.nlab = c->nlabel - lab0;
	w.lab = w.nlab <= 4 ? small : malloc(w.nlab * sizeof(ust));
	w.seen = w.nlab <= sizeof seen ? seen : malloc(w.nlab);
	for (uint32_t k = 0; k < w.nlab; k++)
		w.lab[k].dead = true, w.seen[k] = 0;
	w.report = true;
	u_stmt(c, &w, body, &s);
	if (!w.unsure)
		for (int i = 0; i < w.nv; i++)
			if (w.found[i].use)
				uv_report(c, &w, i);
	if (w.lab != small)
		free(w.lab);
	if (w.seen != seen)
		free(w.seen);
	free(w.rs);
}
