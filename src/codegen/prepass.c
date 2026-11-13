/*
 * prepass.c: what the emitter must know about a function before it generates it: which locals are
 * written and how often (register choice, constant propagation), labels, the function's size, whether
 * inlining may take it, and a few per-symbol and per-target answers.
 *
 * ensure_walked (called by references.c, the walker and arm64's call lowering) walks one function's
 * body once, the first time it is emitted or inlined, and publishes the result with a release store
 * (E->walked: 0 not started, 1 being walked, 2 done); a second thread spins until it is 2, so the
 * facts are immutable once visible.  Facts about non-local variables are installed before the
 * workers start and no lazy walk may update them.  fn_once and fn_isize run before the workers: they
 * decide, for the whole unit, which static functions are inlined (clang's rule for one-use static
 * functions; VIR's size model, with a recursion group never inlined into itself, so the result does
 * not depend on thread order).  The facts must stay conservative: a variable wrongly called
 * unwritten becomes a constant in the generated code.
 */
#include "emitimpl.h"

/* Object alignment can exceed the C type alignment. The x86-64 SysV ABI
 * gives arrays of at least 16 bytes 16-byte alignment, unless the declaration
 * explicitly requests an alignment (matching Clang's aligned attribute). */
VPRIV uint32_t sym_align(cc *c, uint32_t s)
{
	const sym *y = &c->syms[s];
	uint32_t a = talign(c, y->ty);

	if (y->align > a)
		a = y->align;
	if (X86(c) && !y->align && is_array(c, y->ty) && tsize(c, y->ty) >= 16 && a < 16)
		a = 16;
	return a;
}

/* ---------------- prepass: what the emitter needs to know before it starts ---------------- */

/* SVE values live in frame slots and go through Z and P registers only inside
   inline asm (include/arm_sve.h).  What is not lowered: a sizeless argument or
   result (the vector PCS) and operators on sizeless vectors. */
VPRIV bool sve_codegen_ok(cc *c)
{
	uint32_t loc = 0;
	const char *what = "an SVE argument or result";

	if (c->o->target != PP_LINUX_AARCH64 && c->o->target != PP_DARWIN)
		return true;
	for (uint32_t i = 1; i < c->nsym && !loc; i++) {
		const sym *s = &c->syms[i];
		const ctype *t = T(c, s->ty);

		if (s->kind != AST_SYMBOL_FUNCTION || !s->body)
			continue;
		if (KIND(c, t->base) == AST_TYPE_SVE_BUILTIN)
			loc = s->loc;
		for (uint32_t j = 0; j < t->n && !loc; j++)
			if (KIND(c, c->params[t->aux + j]) == AST_TYPE_SVE_BUILTIN)
				loc = s->loc;
	}
	for (uint32_t i = 1; i < c->nnode && !loc; i++) {
		const node *n = &c->nodes[i];

		if (n->op == AST_NODE_CALL)
			for (uint32_t j = 0; j < n->c && !loc; j++)
				if (KIND(c, N(c->lists[n->b + j])->ty) == AST_TYPE_SVE_BUILTIN)
					loc = n->loc;
		if (KIND(c, n->ty) != AST_TYPE_SVE_BUILTIN)
			continue;
		if (n->op == AST_NODE_CALL || n->op == AST_NODE_VA_ARG)
			loc = n->loc;
		else if (n->op == AST_NODE_BINARY || n->op == AST_NODE_UNARY || (n->op == AST_NODE_CAST && n->aux != AST_CAST_NOP))
			loc = n->loc, what = "an operator on an SVE value";
	}
	if (!loc)
		return true;
	diag_record_emit(c->p, diag_record_begin(c->p, DIAG_LEVEL_ERROR, loc, "code generation for %s is not supported yet", what));
	return false;
}

/* Linux x86 and arm64 lower bit-precise values. Keep the other targets
   explicit until their runtime precision and calling conventions are checked. */
VPRIV bool bitint_codegen_ok(cc *c)
{
	if (PP_IS_X86_64(c->o->target) || c->o->target == PP_LINUX_AARCH64)
		return true;
	for (uint32_t i = 1; i < c->nsym; i++) {
		const sym *s = &c->syms[i];
		const ctype *t = T(c, s->ty);
		bool bad = false;

		if (s->kind != AST_SYMBOL_FUNCTION || !s->body)
			continue;
		bad = em_bitint(c, t->base);
		for (uint32_t j = 0; j < t->n && !bad; j++)
			bad = em_bitint(c, c->params[t->aux + j]);
		if (bad) {
			diag_record_emit(c->p, diag_record_begin(c->p, DIAG_LEVEL_ERROR, s->loc, "_BitInt runtime ABI is not supported on this target yet"));
			return false;
		}
	}
	return true;
}

typedef struct pre {
	emitter *E;
	cc *c;
	uint32_t fn;		/* the function being walked */
	uint32_t nodes;		/* nodes seen (inlining size) */
	int cdepth;		/* cleanup variables in scope */
	int loopd;		/* loops around the node */
	uint32_t llo, lhi;	/* local syms seen (lspan) */
	bool discard_param;
} pre;

/* a local is read or written here: count it for register allocation (8x per loop level) */
static void note_local(pre *P, uint32_t s)
{
	uint32_t w = 1u << (3 * (P->loopd < 4 ? P->loopd : 4)), u = P->E->luse[s] + w;

	P->E->luse[s] = (uint16_t)(u > 65535 ? 65535 : u);
	if (s < P->llo)
		P->llo = s;
	if (s > P->lhi)
		P->lhi = s;
}

/* a[CONST], a a variable */
VPRIV bool is_aconst(cc *c, uint32_t n)
{
	const node *x = N(n);

	uint32_t b;

	if (x->op != AST_NODE_INDEX || N(x->a)->op != AST_NODE_CAST || N(x->a)->aux != AST_CAST_ARRAY_DECAY || N(N(x->a)->a)->op != AST_NODE_VARIABLE)
		return false;
	for (b = x->b; N(b)->op == AST_NODE_CAST && is_integer(c, N(b)->ty) && is_integer(c, N(N(b)->a)->ty); b = N(b)->a)
		;
	return N(b)->op == AST_NODE_NUMBER && c->ipool[N(b)->a] >= 0 && c->ipool[N(b)->a] < (i128)1 << 32;
}

/* the index of a[CONST] (is_aconst) */
VPRIV uint32_t aconst_idx(cc *c, uint32_t n)
{
	uint32_t b;

	for (b = N(n)->b; N(b)->op == AST_NODE_CAST; b = N(b)->a)
		;
	return (uint32_t)c->ipool[N(b)->a];
}

/* va_list is a pointer (Apple), not an array of one record (AAPCS64): it can be a register variable */
static bool scalar_valist(cc *c) { return T(c, c->t_valist)->kind != AST_TYPE_ARRAY; }

static void mark_written(pre *P, uint32_t n, bool addr)
{
	cc *c = P->c;

	for (;;) {
		const node *x = N(n);

		if (x->op == AST_NODE_COMMA) {
			n = x->b;	/* match the emitters' comma-lvalue addressing */
		} else if (x->op == AST_NODE_MEMBER || (x->op == AST_NODE_CAST && (x->aux == AST_CAST_NOP || x->aux == AST_CAST_ARRAY_DECAY))) {
			n = x->a;
		} else if (x->op == AST_NODE_INDEX) {
			const node *b = N(x->a);

			if (b->op == AST_NODE_CAST && b->aux == AST_CAST_ARRAY_DECAY)
				n = b->a;
			else
				return;
		} else {
			break;
		}
	}
	if (N(n)->op == AST_NODE_VARIABLE && is_local_var(c, N(n)->a)) {
		P->E->sflags[N(n)->a] |= SA_WRITTEN | SA_AESC, P->E->ffacts[N(n)->a] |= FF_BAD;
		if (addr && is_local_var(P->c, N(n)->a))
			P->E->lnoreg[N(n)->a] = 1;
	}
	else if (N(n)->op == AST_NODE_COMPOUND_LITERAL && is_local_var(c, N(n)->b))
		P->E->sflags[N(n)->b] |= SA_WRITTEN;
}

/*
 * `v = CONST` keeps v constant as long as every assignment stores the same
 * value and the initializer agrees (clang -O2 propagates it: the kernel's
 * `bool enable_scs = false; ... if (x) enable_scs = false;` guards references
 * to symbols that do not exist).
 */
/* a floating local's write: fabs(...) is >= 0 or NaN, a constant is what it is */
static void note_fwrite(pre *P, uint32_t s, uint32_t rhs)
{
	cc *c = P->c;
	uint8_t *f = &P->E->ffacts[s], k = FF_BAD;
	uint32_t ref, callee;
	cc_value cv;

	if (!is_flt(c, c->syms[s].ty) || !is_local_var(c, s))
		return;
	while (N(rhs)->op == AST_NODE_CAST && is_flt(c, N(rhs)->ty) && is_flt(c, N(N(rhs)->a)->ty) &&
	       tsize(c, N(rhs)->ty) >= tsize(c, N(N(rhs)->a)->ty))
		rhs = N(rhs)->a;
	if (cc_eval(c, rhs, &cv) && (cv.kind == CC_VALUE_FLOAT || cv.kind == CC_VALUE_INTEGER)) {
		double v = cv.kind == CC_VALUE_FLOAT ? cv.f : (double)cv.i;

		if (cv.kind == CC_VALUE_FLOAT && cv.ld)	/* f may be 0 or inf where q is not */
			k = cv.q == 0 ? FF_NONNEG | FF_ZERO : !(cv.q >> 127) && (cv.q << 1) <= ((u128)0x7fff << 113) ? FF_NONNEG : 0;
		else
			k = v == 0 && !__builtin_signbit(v) ? FF_NONNEG | FF_ZERO : v >= 0 ? FF_NONNEG : 0;
	} else if (N(rhs)->op == AST_NODE_COMMA && (N(rhs)->aux & AST_NODE_COMMA_FP_NONNEGATIVE)) {
		k = FF_NONNEG;
	} else if (N(rhs)->op == AST_NODE_CALL && (callee = direct_callee(c, N(rhs)->a, &ref))) {
		const sym *fn = &c->syms[callee];
		const ctype *ft = T(c, fn->ty);
		int want = EM_IDENT_IS(fn->name, "fabs") ? AST_TYPE_DOUBLE :
			   EM_IDENT_IS(fn->name, "fabsf") ? AST_TYPE_FLOAT : -1;
		bool enabled = !P->E->o->no_builtin;
		/* The per-name option table belongs to the front end; conservatively
		   avoid library facts whenever any named builtin was disabled. */
		for (unsigned i = 0; i < CC_DISABLED_BUILTIN_WORDS; i++)
			if (P->E->o->disabled_builtins[i]) enabled = false;
		if (enabled && want >= 0 && (fn->flags & AST_SYMBOL_FLAG_GLOBAL) && fn->sc != AST_STORAGE_STATIC && !sym_attrs(c, callee)->asmname &&
		    ft->kind == AST_TYPE_FUNCTION && KIND(c, ft->base) == want && ft->n == 1 &&
		    KIND(c, c->params[ft->aux]) == want)
			k = FF_NONNEG;
	}
	*f = (uint8_t)(*f & FF_SEEN ? (*f & ~(FF_NONNEG | FF_ZERO)) | (*f & k & (FF_NONNEG | FF_ZERO)) | (k & FF_BAD)
				    : FF_SEEN | k);
}

static void note_assign(pre *P, uint32_t s, uint32_t rhs)
{
	cc *c = P->c;
	emitter *E = P->E;
	cc_value cv;

	if (!is_local_var(c, s) || (E->sflags[s] & SA_WRITTEN))
		return;
	if (!is_integer(c, c->syms[s].ty) || !cc_eval(c, rhs, &cv) || cv.kind != CC_VALUE_INTEGER) {
		E->sflags[s] |= SA_WRITTEN;
		return;
	}
	cv.i = em_trunc_to(c, cv.i, c->syms[s].ty);
	if (!(E->sflags[s] & SA_CWRITE)) {
		E->sflags[s] |= SA_CWRITE;
		E->cwval[s] = cv.i;
	} else if (E->cwval[s] != cv.i) {
		E->sflags[s] |= SA_CMULTI;
	}
}

/*
 * Per function, 1 when the TU names it exactly once and that is the callee of a
 * call: a static function clang -O2 inlines whatever its size (its only copy).
 * Every node counts, unevaluated ones and those of functions never emitted too,
 * so a count only errs high (no inlining).
 */
VPRIV uint8_t *fn_once(cc *c)
{
	uint8_t *r = calloc((size_t)c->nsym + 64, 1);	/* refs in bits 0-1, calls in 2-3 (saturating) */

	if (!r)
		return NULL;
	for (uint32_t n = 1; n < c->nnode; n++) {
		const node *x = &c->nodes[n];
		uint32_t s;

		if (x->op == AST_NODE_VARIABLE) {
			s = x->a;
			if (s < c->nsym && c->syms[s].kind == AST_SYMBOL_FUNCTION && ((r[s = canon_sym(c, s)] & 3) != 3))
				r[s]++;
		} else if (x->op == AST_NODE_CALL) {
			const node *f = &c->nodes[x->a];

			while (f->op == AST_NODE_CAST && (f->aux == AST_CAST_FUNCTION_DECAY || f->aux == AST_CAST_NOP || f->aux == AST_CAST_POINTER_TO_POINTER))
				f = &c->nodes[f->a];
			if (f->op == AST_NODE_VARIABLE && f->a < c->nsym && c->syms[f->a].kind == AST_SYMBOL_FUNCTION && ((r[s = canon_sym(c, f->a)] >> 2) != 3))
				r[s] += 4;
		}
	}
	for (uint32_t s = 0; s < c->nsym; s++)
		r[s] = r[s] == (1 | 4);	/* (bit 1: calls setjmp, set by the prepass) */
	return r;
}

/* Shared with the walker: literal control arguments can specialize a copy. */
VPRIV bool inl_constarg(const emitter *E,cc *c,uint32_t callee,uint32_t call)
{
 if(!call)return false;
 const node *site=N(call),*body=N(c->syms[callee].body);
 for(uint32_t k=0;k<site->c && k<body->aux;k++) {
  if(!E->inl_read[c->lists[body->a+k]])continue;
  uint32_t a=c->lists[site->b+k];cc_value cv;
  if(!cc_eval(c,a,&cv))continue;
  if(cv.kind==CC_VALUE_INTEGER && is_integer(c,N(a)->ty))return true;
  if(cv.kind==CC_VALUE_ADDRESS && !cv.i && !cv.sym && !cv.str && !cv.label && !cv.sublabel)return true;
 }
 return false;
}

/* VIR could inline a call to s, its size aside (wk_can_inline's other rules, conservatively) */
VPRIV bool inl_cand(emitter *E, cc *c, uint32_t s)
{
	const sym *y = &c->syms[s];
	const ctype *ft;

	if (y->kind != AST_SYMBOL_FUNCTION || !(y->flags & AST_SYMBOL_FLAG_DEFINED) || !y->body)
		return false;
	ft = T(c, y->ty);
	if ((ft->flags & (AST_TYPE_FLAG_VARIADIC | AST_TYPE_FLAG_NO_PROTOTYPE)) || N(y->body)->aux > 64 || (E->sflags[s] & SA_NOINL) || (E->once[s] & 2))
		return false;
	if (y->flags & AST_SYMBOL_FLAG_ALWAYS_INLINE)
		return true;
	return !(y->flags & AST_SYMBOL_FLAG_NOINLINE) && ((y->flags & AST_SYMBOL_FLAG_INLINE) || y->sc == AST_STORAGE_STATIC);
}

/*
 * VIR's inlining sizes, bottom-up as LLVM's inliner: isz[s] is s's AST nodes plus
 * the isz of each callee it inlines (one whose isz is at most lim, or always_inline),
 * so a copy of s costs isz[s] however deep its callees inline.  A recursion group
 * (scc, Tarjan over the direct calls) does not inline into itself, which keeps the
 * sizes defined and the walker's choices independent of the order threads run in.
 * Runs before the workers: it walks every function (the prepass records calls).
 */
VPRIV void fn_isize(emitter *E, cc *c, uint32_t lim)
{
	uint32_t n = c->nsym, *idx, *low, *stk, *cs, *ci, sp = 0, csp = 0, nidx = 0;
	uint8_t *on;

	E->inl_read = calloc((size_t)n + 64, 1);
	E->cgoff = calloc((size_t)n + 64, 4);
	E->cgn = calloc((size_t)n + 64, 4);
	E->scc = calloc((size_t)n + 64, 4);
	E->isz = calloc((size_t)n + 64, 2);
	idx = calloc((size_t)n + 64, 4), low = calloc((size_t)n + 64, 4), stk = malloc(((size_t)n + 64) * 4);
	cs = malloc(((size_t)n + 64) * 4), ci = malloc(((size_t)n + 64) * 4), on = calloc((size_t)n + 64, 1);
	if (!E->inl_read || !E->cgoff || !E->cgn || !E->scc || !E->isz || !idx || !low || !stk || !cs || !ci || !on)
		abort();
	E->cgrec = true;
	for (uint32_t s = 1; s < n; s++)
		if (c->syms[s].kind == AST_SYMBOL_FUNCTION && c->syms[s].body && (c->syms[s].flags & AST_SYMBOL_FLAG_DEFINED) && canon_sym(c, s) == s) {
			E->cgoff[s] = E->ncg;
			ensure_walked(E, c, s);
			E->cgn[s] = E->ncg - E->cgoff[s];
		}
	E->cgrec = false;
	/* Tarjan, iterative; a group is finished callees first, so its isz is computed there */
	for (uint32_t r = 1; r < n; r++) {
		if (idx[r] || c->syms[r].kind != AST_SYMBOL_FUNCTION)
			continue;
		cs[csp] = r, ci[csp++] = 0;
		idx[r] = low[r] = ++nidx, stk[sp++] = r, on[r] = 1;
		while (csp) {
			uint32_t v = cs[csp - 1];

			if (ci[csp - 1] < E->cgn[v]) {
				uint32_t w = E->cg[E->cgoff[v] + ci[csp - 1]++].callee;

				if (!idx[w]) {
					cs[csp] = w, ci[csp++] = 0;
					idx[w] = low[w] = ++nidx, stk[sp++] = w, on[w] = 1;
				} else if (on[w] && idx[w] < low[v]) {
					low[v] = idx[w];
				}
				continue;
			}
			csp--;
			if (csp && low[v] < low[cs[csp - 1]])
				low[cs[csp - 1]] = low[v];
			if (low[v] != idx[v])
				continue;
			/* v roots a group: pop it, then sizes (the group's callees outside it are done) */
			uint32_t top = sp;

			do
				E->scc[stk[--sp]] = v, on[stk[sp]] = 0;
			while (stk[sp] != v);
			for (uint32_t k = sp; k < top; k++) {
				uint32_t s = stk[k], z = E->fnsize[s];

				for (uint32_t j = 0; j < E->cgn[s]; j++) {
					em_cg_edge edge=E->cg[E->cgoff[s]+j];
					uint32_t t=edge.callee;

					if(E->scc[t]==v || !inl_cand(E,c,t))continue;
					bool take=c->syms[t].flags&AST_SYMBOL_FLAG_ALWAYS_INLINE;
					if(!take) {
						bool once=(E->once[t]&1) && c->syms[t].sc==AST_STORAGE_STATIC && !sym_attrs(c,t)->used &&
						          E->fnsize[s]<=WK_INLINE_ONCE;
						take=once ? E->fnsize[t]<=WK_INLINE_ONCE : E->isz[t]<=lim ||
						 (E->isz[t]<=lim*WK_INLINE_CONST_FACTOR && inl_constarg(E,c,t,edge.call));
					}
					if(take)z=z>=65535u-E->isz[t]?65535u:z+E->isz[t];
				}
				E->isz[s] = z > 65535 ? 65535 : (uint16_t)z;
			}
		}
	}
	free(idx), free(low), free(stk), free(cs), free(ci), free(on);
}

/*
 * The prepass runs per function, the first time a function is emitted or
 * inlined: most of a TU's functions are header inlines nobody uses.  The
 * facts it records (writes to params and locals, labels) belong to that
 * function alone, so one walk per function is all that is needed. Nonlocal
 * variables have conservative immutable facts installed before workers start;
 * no lazy walk may update them.
 */
static int walk(pre *P, uint32_t n);

VPRIV void ensure_walked(emitter *E, cc *c, uint32_t s)
{
	uint8_t st = atomic_load_explicit(&E->walked[s], memory_order_acquire);

	if (likely(st == 2))
		return;
	st = 0;
	if (atomic_compare_exchange_strong(&E->walked[s], &st, 1)) {
		pre P = { .E=E, .c=c, .fn=s, .llo=UINT32_MAX };
		double t = E->stats ? now() : 0;
		const node *b = &c->nodes[c->syms[s].body];

		for (uint32_t i = 0; i < b->aux; i++)	/* params, used or not */
			note_local(&P, c->lists[b->a + i]);
		walk(&P, c->syms[s].body);
		E->lspan[2 * s] = P.llo, E->lspan[2 * s + 1] = P.lhi;
		E->fnsize[s] = P.nodes > 65535 ? 65535 : (uint16_t)P.nodes;
		if (E->stats)
			E->walk_ns += (uint64_t)((now() - t) * 1e9), E->nwalk++;
		atomic_store_explicit(&E->walked[s], 2, memory_order_release);
		return;
	}
	while (atomic_load_explicit(&E->walked[s], memory_order_acquire) != 2)
		;	/* another thread is walking it: a few microseconds */
}

/* a static initializer takes a label's address (&&l, &&l - &&m, { &&l }) */
static bool init_has_labeladdr(cc *c, uint32_t n)
{
	const node *x;

	if (!n)
		return false;
	x = N(n);
	switch (x->op) {
	case AST_NODE_LABEL_ADDRESS:
		return true;
	case AST_NODE_INITIALIZER:
		for (uint32_t i = 0; i < x->c; i++)	/* the layout side_effects() reads */
			if (init_has_labeladdr(c, x->aux == 1 ? c->lists[x->b + 3 * i + 2] : c->lists[x->b + i]))
				return true;
		return false;
	case AST_NODE_CAST: case AST_NODE_ADDRESS: case AST_NODE_UNARY:
		return init_has_labeladdr(c, x->a);
	case AST_NODE_BINARY:
		return init_has_labeladdr(c, x->a) || init_has_labeladdr(c, x->b);
	default:
		return false;
	}
}

/* walks every node; returns whether it contains a label (or case) */
static int walk(pre *P, uint32_t n)
{
	cc *c = P->c;
	const node *x;
	int lab = 0;

	if (!n)
		return false;
	x = N(n);
	P->nodes++;
	switch (x->op) {
	case AST_NODE_VARIABLE:
		if(P->E->inl_read && !P->discard_param && (c->syms[x->a].flags&AST_SYMBOL_FLAG_PARAMETER))
			P->E->inl_read[x->a]=1;
		/* locals belong to this walk alone (globals' flags are shared between threads) */
		if (is_local_var(c, x->a)) {
			P->E->sflags[x->a] |= SA_USED;
			note_local(P, x->a);
		} else {
			const sym *y = &c->syms[x->a];

			/* a static local holding a label address: the function has one copy */
			if (y->kind == AST_SYMBOL_VARIABLE && y->sc == AST_STORAGE_STATIC && !(y->flags & AST_SYMBOL_FLAG_GLOBAL) && y->body &&
			    init_has_labeladdr(c, y->body))
				P->E->sflags[P->fn] |= SA_NOINL;
		}
		return false;
	case AST_NODE_NUMBER: case AST_NODE_BITINT_NUMBER: case AST_NODE_FLOAT_NUMBER: case AST_NODE_STRING: case AST_NODE_ZERO:
		return false;
	case AST_NODE_SIZEOF_VLA:
		lab = walk(P, x->a);
		for (ast_qualified_type t = x->b; is_array(c, t); t = T(c, t)->base)
			if ((T(c, t)->flags & AST_TYPE_FLAG_VLA) && T(c, t)->n)
				lab |= walk(P, T(c, t)->n);
		break;
	case AST_NODE_LABEL_ADDRESS:
		P->E->lref[x->a] |= LR_REF | LR_BACK;
		return false;
	case AST_NODE_VA_ARG:	/* va_arg reads and advances a scalar va_list through its address */
		if (scalar_valist(c))
			mark_written(P, x->a, true);
		lab = walk(P, x->a);
		break;
	case AST_NODE_MEMBER: case AST_NODE_DEREFERENCE: case AST_NODE_UNARY:
		lab = walk(P, x->a);
		break;
	case AST_NODE_CAST: {
		if (x->aux == AST_CAST_ARRAY_DECAY || (x->aux == AST_CAST_BIT_CAST && (N(x->a)->flags & AST_NODE_FLAG_LVALUE)))
			mark_written(P, x->a, true);
		bool saved=P->discard_param;
		if(KIND(c,x->ty)==AST_TYPE_VOID) {
			uint32_t a=x->a;
			while(N(a)->op==AST_NODE_CAST)a=N(a)->a;
			if(N(a)->op==AST_NODE_VARIABLE && (c->syms[N(a)->a].flags&AST_SYMBOL_FLAG_PARAMETER))P->discard_param=true;
		}
		lab = walk(P, x->a);P->discard_param=saved;
		break;
	}
	case AST_NODE_ADDRESS:
		mark_written(P, x->a, true);
		lab = walk(P, x->a);
		break;
	case AST_NODE_PRE_INCREMENT: case AST_NODE_PRE_DECREMENT: case AST_NODE_POST_INCREMENT: case AST_NODE_POST_DECREMENT:
		mark_written(P, x->a, false);
		lab = walk(P, x->a);
		break;
	case AST_NODE_ASSIGN:
		if (!x->aux && N(x->a)->op == AST_NODE_VARIABLE)
			note_assign(P, N(x->a)->a, x->b), note_fwrite(P, N(x->a)->a, x->b);
		else if (!x->aux && is_aconst(c, x->a)) {
			uint32_t s = N(N(N(x->a)->a)->a)->a;

			if (is_local_var(c, s))
				P->E->sflags[s] |= SA_WRITTEN;
		}
		else
			mark_written(P, x->a, false);
		lab = walk(P, x->a) | walk(P, x->b);
		break;
	case AST_NODE_INDEX:
		if (is_aconst(c, n)) {
			/* a[CONST]: the array does not escape here */
			lab = walk(P, N(x->a)->a) | walk(P, x->b);
			break;
		}
		__attribute__((fallthrough));
	case AST_NODE_BINARY: case AST_NODE_LOGICAL_AND: case AST_NODE_LOGICAL_OR: case AST_NODE_COMMA: case AST_NODE_COMPLEX:
		lab = walk(P, x->a) | walk(P, x->b);
		break;
	case AST_NODE_CONDITIONAL:
		lab = walk(P, x->a) | walk(P, x->b) | walk(P, x->c);
		break;
	case AST_NODE_CALL: {
		uint32_t ref, callee = direct_callee(c, x->a, &ref);

		if (callee && sym_attrs(c, callee)->error_fn)
			P->E->sflags[P->fn] |= SA_MUSTFOLD;
		/* VIR: a function calling setjmp is not inlined (as LLVM): the caller's promoted
		   locals would take their values at the setjmp after a longjmp */
		if (callee && P->E->once && ((sym_attrs(c, callee)->flags & CC_ATTRIBUTE_FLAG_TAIL_RETURNS_TWICE) ||
		    EM_IDENT_IS(c->syms[callee].name, "setjmp") || EM_IDENT_IS(c->syms[callee].name, "_setjmp") ||
		    EM_IDENT_IS(c->syms[callee].name, "sigsetjmp") || EM_IDENT_IS(c->syms[callee].name, "__sigsetjmp")))
			P->E->once[P->fn] |= 2;
		if (callee && P->E->cgrec) {
			emitter *E = P->E;

			if (E->ncg == E->capcg) {
				em_cg_edge *g = realloc(E->cg, (size_t)(E->capcg = E->capcg ? 2 * E->capcg : 1024) * sizeof *g);

				if (!g)
					abort();
				E->cg = g;
			}
			E->cg[E->ncg++] = (em_cg_edge){canon_sym(c,callee),n};
		}
		lab = walk(P, x->a);
	}
		__attribute__((fallthrough));
	case AST_NODE_BUILTIN:
		if (x->op == AST_NODE_BUILTIN && (x->aux == CC_BUILTIN_ALLOCA || x->aux == CC_BUILTIN_ALLOCA_UNINITIALIZED))
			P->E->nflags[c->syms[P->fn].body] |= NA_ALLOCA;
		for (uint32_t i = 0; i < x->c; i++)
			lab |= walk(P, c->lists[x->b + i]);
		/*
		 * The va builtins take their va_lists' addresses (Apple's char *
		 * could otherwise be a register variable): every operand.
		 */
		if (x->op == AST_NODE_BUILTIN && (x->aux == CC_BUILTIN_VA_START || x->aux == CC_BUILTIN_VA_COPY || x->aux == CC_BUILTIN_VA_END))
			for (uint32_t i = 0; i < x->c && i < (x->aux == CC_BUILTIN_VA_COPY ? 2u : 1u); i++)
				if ((i == 0 && x->aux != CC_BUILTIN_VA_END) || scalar_valist(c))
					mark_written(P, c->lists[x->b + i], true);
		break;
	case AST_NODE_STATEMENT_EXPRESSION:
		lab = walk(P, x->a);
		break;
	case AST_NODE_COMPOUND_LITERAL:
		lab = walk(P, x->a);
		break;
	case AST_NODE_INITIALIZER:
		if (x->aux == 1) {
			for (uint32_t i = 0; i < x->c; i++)
				lab |= walk(P, c->lists[x->b + 3 * i + 2]);
		} else {
			for (uint32_t i = 0; i < x->c; i++)
				lab |= walk(P, c->lists[x->b + i]);
		}
		break;
	case AST_NODE_BLOCK: {
		int cd = P->cdepth;

		for (uint32_t i = 0; i < x->c; i++)
			lab |= walk(P, c->lists[x->b + i]);
		P->cdepth = cd;
		break;
	}
	case AST_NODE_EXPRESSION:
	case AST_NODE_RETURN:
	case AST_NODE_GOTO_INDIRECT:
		lab = walk(P, x->a);
		break;
	case AST_NODE_DECLARATION: {
		const sym *y = &c->syms[x->a];

		lab = walk(P, x->b);
		if (!x->aux)
			for (ast_qualified_type t = y->ty; is_array(c, t) || is_ptr(c, t); t = T(c, t)->base)
				if ((T(c, t)->flags & AST_TYPE_FLAG_VLA) && T(c, t)->n)
					lab |= walk(P, T(c, t)->n);
		if (x->b && y->kind == AST_SYMBOL_VARIABLE)
			note_fwrite(P, x->a, x->b);
		if (is_local_var(c, x->a)) {
			note_local(P, x->a);
			if (x->b && (N(x->b)->op == AST_NODE_INITIALIZER || N(x->b)->op == AST_NODE_ZERO || N(x->b)->op == AST_NODE_STRING))
				P->E->lnoreg[x->a] = 1;	/* gen_init_local works on memory */
		}
		if (sym_attrs(c, x->a)->cleanup || (y->kind == AST_SYMBOL_VARIABLE && (T(c, y->ty)->flags & AST_TYPE_FLAG_VLA)))
			P->cdepth++;
		/* a __used static local is emitted with its function, referenced or not */
		if (y->kind == AST_SYMBOL_VARIABLE && y->sc == AST_STORAGE_STATIC && sym_attrs(c, x->a)->used &&
		    !atomic_load_explicit(&P->E->claimed[x->a], memory_order_relaxed) &&
		    !atomic_exchange(&P->E->claimed[x->a], 1))
			enqueue(P->E, x->a);
		break;
	}
	case AST_NODE_IF:
		lab = walk(P, x->a) | walk(P, x->b) | walk(P, x->c);
		break;
	/* in the order the code is generated: LR_BACK means a jump back */
	case AST_NODE_SWITCH:
		lab = walk(P, x->a) | walk(P, x->b);
		break;
	case AST_NODE_WHILE:
		P->loopd++;
		lab = walk(P, x->a) | walk(P, x->b);
		P->loopd--;
		break;
	case AST_NODE_DO:
		P->loopd++;
		lab = walk(P, x->b);
		lab |= walk(P, x->a);
		P->loopd--;
		break;
	case AST_NODE_FOR: {
		int cd = P->cdepth;

		/* the init's declarations live in the for statement (gen does not open a block for them) */
		if (x->a && N(x->a)->op == AST_NODE_BLOCK) {
			for (uint32_t i = 0; i < N(x->a)->c; i++)
				lab |= walk(P, c->lists[N(x->a)->b + i]);
		} else {
			lab |= walk(P, x->a);
		}
		P->loopd++;
		lab |= walk(P, x->b);
		lab |= walk(P, c->lists[x->c + 1]);
		lab |= walk(P, c->lists[x->c]);
		P->loopd--;
		P->cdepth = cd;
		break;
	}
	case AST_NODE_CASE: case AST_NODE_DEFAULT:
		walk(P, x->c);
		lab = 1;
		break;
	case AST_NODE_LABEL:
		P->E->lcdepth[x->a] = (uint16_t)P->cdepth;
		P->E->lref[x->a] |= LR_DEF;
		if (c->labels[x->a].addr_taken)
			P->E->lref[x->a] |= LR_REF | LR_BACK;
		walk(P, x->c);
		lab = 1;
		break;
	case AST_NODE_GOTO:
		P->E->lref[x->a] |= LR_REF | (P->E->lref[x->a] & LR_DEF ? LR_BACK : 0);
		break;
	case AST_NODE_ASM: {
		const ast_asm_statement *a = &c->asms[x->a];

		for (uint32_t i = 0; i < a->nout + a->nin; i++) {
			if (i < a->nout)
				mark_written(P, a->ops[i].expr, true);
			else if (strpbrk(c->strs[a->ops[i].constraint - 1].s, "mQo"))
				mark_written(P, a->ops[i].expr, true);
			else if (strpbrk(c->strs[a->ops[i].constraint - 1].s, "gVX<>") && N(a->ops[i].expr)->op == AST_NODE_VARIABLE &&
				 is_local_var(c, N(a->ops[i].expr)->a))
				P->E->lnoreg[N(a->ops[i].expr)->a] = 1;	/* may become a memory operand */
			lab |= walk(P, a->ops[i].expr);
		}
		for (uint32_t k = 0; k < a->nlabel; k++)
			P->E->lref[a->labels[k]] |= LR_REF | (P->E->lref[a->labels[k]] & LR_DEF ? LR_BACK : 0);
		break;
	}
	default:
		break;
	}
	if (lab)
		P->E->nflags[n] |= NA_LABEL;
	return lab;
}

VPRIV int u32_cmp(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;

	return x < y ? -1 : x > y;
}

/* Inlining preserves a never-written pointer parameter's incoming object
   bound. No AST substitution: emission can run concurrently for other callers.
   Only direct parameters and effect-free actual arguments are covered here. */
VPRIV bool inline_object_size(ecx *e, uint32_t fn, uint32_t call, uint32_t pointer, uint32_t mode, i128 *size)
{
	cc *c = e->c;
	if (!fn || !call) return false;
	while (N(pointer)->op == AST_NODE_CAST) pointer = N(pointer)->a;
	if (N(pointer)->op != AST_NODE_VARIABLE) return false;
	uint32_t p = N(pointer)->a;
	if (!is_ptr(c, c->syms[p].ty) || (c->syms[p].flags & AST_SYMBOL_FLAG_ADDRESS_TAKEN) ||
	    (e->E->sflags[p] & (SA_WRITTEN | SA_CWRITE)) || (AST_QUALIFIED_TYPE_QUALIFIERS(c->syms[p].ty) & AST_QUALIFIER_VOLATILE))
		return false;
	const node *body = N(c->syms[fn].body), *x = N(call);
	cc_value cv;
	if (!cc_eval(c, mode, &cv) || cv.kind != CC_VALUE_INTEGER || cv.i < 0 || cv.i > 3) return false;
	for (uint32_t i = 0; i < body->aux && i < x->c; i++) {
		if (c->lists[body->a + i] != p) continue;
		uint32_t arg = c->lists[x->b + i];
		return !side_effects(c, arg) && cc_builtin_object_size(c, arg, (unsigned)cv.i, size);
	}
	return false;
}
