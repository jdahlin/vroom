/*
 * prepass.c: what the emitter must know about a function before it generates it: which locals are
 * written and how often (register choice, constant propagation), labels, the function's size, whether
 * inlining may take it, and a few per-symbol and per-target answers.
 *
 * codegen_function_walk_ensure (called by references.c, the walker and arm64's call lowering) walks one function's
 * body once, the first time it is emitted or inlined, and publishes the result with a release store
 * (E->walked: 0 not started, 1 being walked, 2 done); a second thread spins until it is 2, so the
 * facts are immutable once visible.  Facts about non-local variables are installed before the
 * workers start and no lazy walk may update them.  codegen_function_once_flags and codegen_function_inline_sizes run before the workers: they
 * decide, for the whole unit, which static functions are inlined (clang's rule for one-use static
 * functions; VIR's size model, with a recursion group never inlined into itself, so the result does
 * not depend on thread order).  The facts must stay conservative: a variable wrongly called
 * unwritten becomes a constant in the generated code.
 */
#include "codegen-internal.h"

/* Object alignment can exceed the C type alignment. The x86-64 SysV ABI
 * gives arrays of at least 16 bytes 16-byte alignment, unless the declaration
 * explicitly requests an alignment (matching Clang's aligned attribute). */
VPRIV uint32_t codegen_symbol_alignment(cc *c, uint32_t s)
{
	const sym *y = &c->syms[s];
	uint32_t a = codegen_type_alignment(c, y->ty);

	if (y->align > a)
		a = y->align;
	if (CODEGEN_IS_X64(c) && !y->align && codegen_type_is_array(c, y->ty) && codegen_type_size(c, y->ty) >= 16 && a < 16)
		a = 16;
	return a;
}

/* ---------------- prepass: what the emitter needs to know before it starts ---------------- */

/* SVE values live in frame slots and go through Z and P registers only inside
   inline asm (include/arm_sve.h).  What is not lowered: a sizeless argument or
   result (the vector PCS) and operators on sizeless vectors. */
VPRIV bool codegen_target_supports_sve(cc *c)
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
VPRIV bool codegen_target_supports_bitint(cc *c)
{
	if (c->o->target == PP_LINUX_X86_64 || c->o->target == PP_LINUX_AARCH64)
		return true;
	for (uint32_t i = 1; i < c->nsym; i++) {
		const sym *s = &c->syms[i];
		const ctype *t = T(c, s->ty);
		bool bad = false;

		if (s->kind != AST_SYMBOL_FUNCTION || !s->body)
			continue;
		bad = codegen_type_is_bitint(c, t->base);
		for (uint32_t j = 0; j < t->n && !bad; j++)
			bad = codegen_type_is_bitint(c, c->params[t->aux + j]);
		if (bad) {
			diag_record_emit(c->p, diag_record_begin(c->p, DIAG_LEVEL_ERROR, s->loc, "_BitInt runtime ABI is not supported on this target yet"));
			return false;
		}
	}
	return true;
}

typedef struct codegen_prepass_state {
	codegen_unit *E;
	cc *c;
	uint32_t fn;		/* the function being walked */
	uint32_t nodes;		/* nodes seen (inlining size) */
	int cdepth;		/* cleanup variables in scope */
	int loopd;		/* loops around the node */
	uint32_t llo, lhi;	/* local syms seen (lspan) */
	bool discard_param;
} codegen_prepass_state;

/* a local is read or written here: count it for register allocation (8x per loop level) */
static void codegen_prepass_local_note(codegen_prepass_state *P, uint32_t s)
{
	uint32_t w = 1u << (3 * (P->loopd < 4 ? P->loopd : 4)), u = P->E->local_use_weights[s] + w;

	P->E->local_use_weights[s] = (uint16_t)(u > 65535 ? 65535 : u);
	if (s < P->llo)
		P->llo = s;
	if (s > P->lhi)
		P->lhi = s;
}

/* a[CONST], a a variable */
VPRIV bool codegen_expression_is_constant_index(cc *c, uint32_t n)
{
	const node *x = N(n);

	uint32_t b;

	if (x->op != AST_NODE_INDEX || N(x->a)->op != AST_NODE_CAST || N(x->a)->aux != AST_CAST_ARRAY_DECAY || N(N(x->a)->a)->op != AST_NODE_VARIABLE)
		return false;
	for (b = x->b; N(b)->op == AST_NODE_CAST && codegen_type_is_integer(c, N(b)->ty) && codegen_type_is_integer(c, N(N(b)->a)->ty); b = N(b)->a)
		;
	return N(b)->op == AST_NODE_NUMBER && c->ipool[N(b)->a] >= 0 && c->ipool[N(b)->a] < (i128)1 << 32;
}

/* the index of a[CONST] (codegen_expression_is_constant_index) */
VPRIV uint32_t codegen_expression_constant_index(cc *c, uint32_t n)
{
	uint32_t b;

	for (b = N(n)->b; N(b)->op == AST_NODE_CAST; b = N(b)->a)
		;
	return (uint32_t)c->ipool[N(b)->a];
}

/* va_list is a pointer (Apple), not an array of one record (AAPCS64): it can be a register variable */
static bool codegen_valist_is_scalar(cc *c) { return T(c, c->t_valist)->kind != AST_TYPE_ARRAY; }

static void codegen_prepass_written_mark(codegen_prepass_state *P, uint32_t n, bool addr)
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
	if (N(n)->op == AST_NODE_VARIABLE && codegen_symbol_is_local_variable(c, N(n)->a)) {
		P->E->symbol_flags[N(n)->a] |= CODEGEN_SYMBOL_WRITTEN | CODEGEN_SYMBOL_ARRAY_ESCAPES, P->E->float_facts[N(n)->a] |= CODEGEN_FLOAT_FACT_BAD;
		if (addr && codegen_symbol_is_local_variable(P->c, N(n)->a))
			P->E->local_needs_memory[N(n)->a] = 1;
	}
	else if (N(n)->op == AST_NODE_COMPOUND_LITERAL && codegen_symbol_is_local_variable(c, N(n)->b))
		P->E->symbol_flags[N(n)->b] |= CODEGEN_SYMBOL_WRITTEN;
}

/*
 * `v = CONST` keeps v constant as long as every assignment stores the same
 * value and the initializer agrees (clang -O2 propagates it: the kernel's
 * `bool enable_scs = false; ... if (x) enable_scs = false;` guards references
 * to symbols that do not exist).
 */
/* a floating local's write: fabs(...) is >= 0 or NaN, a constant is what it is */
static void codegen_prepass_float_write_note(codegen_prepass_state *P, uint32_t s, uint32_t rhs)
{
	cc *c = P->c;
	uint8_t *f = &P->E->float_facts[s], k = CODEGEN_FLOAT_FACT_BAD;
	uint32_t ref, callee;
	cc_value cv;

	if (!codegen_type_is_float(c, c->syms[s].ty) || !codegen_symbol_is_local_variable(c, s))
		return;
	while (N(rhs)->op == AST_NODE_CAST && codegen_type_is_float(c, N(rhs)->ty) && codegen_type_is_float(c, N(N(rhs)->a)->ty) &&
	       codegen_type_size(c, N(rhs)->ty) >= codegen_type_size(c, N(N(rhs)->a)->ty))
		rhs = N(rhs)->a;
	if (cc_eval(c, rhs, &cv) && (cv.kind == CC_VALUE_FLOAT || cv.kind == CC_VALUE_INTEGER)) {
		double v = cv.kind == CC_VALUE_FLOAT ? cv.f : (double)cv.i;

		if (cv.kind == CC_VALUE_FLOAT && cv.ld)	/* f may be 0 or inf where q is not */
			k = cv.q == 0 ? CODEGEN_FLOAT_FACT_NONNEGATIVE | CODEGEN_FLOAT_FACT_ZERO : !(cv.q >> 127) && (cv.q << 1) <= ((u128)0x7fff << 113) ? CODEGEN_FLOAT_FACT_NONNEGATIVE : 0;
		else
			k = v == 0 && !__builtin_signbit(v) ? CODEGEN_FLOAT_FACT_NONNEGATIVE | CODEGEN_FLOAT_FACT_ZERO : v >= 0 ? CODEGEN_FLOAT_FACT_NONNEGATIVE : 0;
	} else if (N(rhs)->op == AST_NODE_COMMA && (N(rhs)->aux & AST_NODE_COMMA_FP_NONNEGATIVE)) {
		k = CODEGEN_FLOAT_FACT_NONNEGATIVE;
	} else if (N(rhs)->op == AST_NODE_CALL && (callee = codegen_call_direct_callee(c, N(rhs)->a, &ref))) {
		const sym *fn = &c->syms[callee];
		const ctype *ft = T(c, fn->ty);
		int want = CODEGEN_IDENTIFIER_IS(fn->name, "fabs") ? AST_TYPE_DOUBLE :
			   CODEGEN_IDENTIFIER_IS(fn->name, "fabsf") ? AST_TYPE_FLOAT : -1;
		bool enabled = !P->E->options->no_builtin;
		/* The per-name option table belongs to the front end; conservatively
		   avoid library facts whenever any named builtin was disabled. */
		for (unsigned i = 0; i < CC_DISABLED_BUILTIN_WORDS; i++)
			if (P->E->options->disabled_builtins[i]) enabled = false;
		if (enabled && want >= 0 && (fn->flags & AST_SYMBOL_FLAG_GLOBAL) && fn->sc != AST_STORAGE_STATIC && !codegen_symbol_attributes(c, callee)->asmname &&
		    ft->kind == AST_TYPE_FUNCTION && KIND(c, ft->base) == want && ft->n == 1 &&
		    KIND(c, c->params[ft->aux]) == want)
			k = CODEGEN_FLOAT_FACT_NONNEGATIVE;
	}
	*f = (uint8_t)(*f & CODEGEN_FLOAT_FACT_SEEN ? (*f & ~(CODEGEN_FLOAT_FACT_NONNEGATIVE | CODEGEN_FLOAT_FACT_ZERO)) | (*f & k & (CODEGEN_FLOAT_FACT_NONNEGATIVE | CODEGEN_FLOAT_FACT_ZERO)) | (k & CODEGEN_FLOAT_FACT_BAD)
				    : CODEGEN_FLOAT_FACT_SEEN | k);
}

static void codegen_prepass_assignment_note(codegen_prepass_state *P, uint32_t s, uint32_t rhs)
{
	cc *c = P->c;
	codegen_unit *E = P->E;
	cc_value cv;

	if (!codegen_symbol_is_local_variable(c, s) || (E->symbol_flags[s] & CODEGEN_SYMBOL_WRITTEN))
		return;
	if (!codegen_type_is_integer(c, c->syms[s].ty) || !cc_eval(c, rhs, &cv) || cv.kind != CC_VALUE_INTEGER) {
		E->symbol_flags[s] |= CODEGEN_SYMBOL_WRITTEN;
		return;
	}
	cv.i = codegen_integer_truncate(c, cv.i, c->syms[s].ty);
	if (!(E->symbol_flags[s] & CODEGEN_SYMBOL_CONSTANT_WRITTEN)) {
		E->symbol_flags[s] |= CODEGEN_SYMBOL_CONSTANT_WRITTEN;
		E->constant_written_values[s] = cv.i;
	} else if (E->constant_written_values[s] != cv.i) {
		E->symbol_flags[s] |= CODEGEN_SYMBOL_CONSTANT_MULTIPLE;
	}
}

/*
 * Per function, 1 when the TU names it exactly once and that is the callee of a
 * call: a static function clang -O2 inlines whatever its size (its only copy).
 * Every node counts, unevaluated ones and those of functions never emitted too,
 * so a count only errs high (no inlining).
 */
VPRIV uint8_t *codegen_function_once_flags(cc *c)
{
	uint8_t *r = calloc((size_t)c->nsym + 64, 1);	/* refs in bits 0-1, calls in 2-3 (saturating) */

	if (!r)
		return NULL;
	for (uint32_t n = 1; n < c->nnode; n++) {
		const node *x = &c->nodes[n];
		uint32_t s;

		if (x->op == AST_NODE_VARIABLE) {
			s = x->a;
			if (s < c->nsym && c->syms[s].kind == AST_SYMBOL_FUNCTION && ((r[s = codegen_symbol_canonical(c, s)] & 3) != 3))
				r[s]++;
		} else if (x->op == AST_NODE_CALL) {
			const node *f = &c->nodes[x->a];

			while (f->op == AST_NODE_CAST && (f->aux == AST_CAST_FUNCTION_DECAY || f->aux == AST_CAST_NOP || f->aux == AST_CAST_POINTER_TO_POINTER))
				f = &c->nodes[f->a];
			if (f->op == AST_NODE_VARIABLE && f->a < c->nsym && c->syms[f->a].kind == AST_SYMBOL_FUNCTION && ((r[s = codegen_symbol_canonical(c, f->a)] >> 2) != 3))
				r[s] += 4;
		}
	}
	for (uint32_t s = 0; s < c->nsym; s++)
		r[s] = r[s] == (1 | 4);	/* (bit 1: calls setjmp, set by the prepass) */
	return r;
}

/* Shared with the walker: literal control arguments can specialize a copy. */
VPRIV bool codegen_inline_has_constant_argument(const codegen_unit *E,cc *c,uint32_t callee,uint32_t call)
{
 if(!call)return false;
 const node *site=N(call),*body=N(c->syms[callee].body);
 for(uint32_t k=0;k<site->c && k<body->aux;k++) {
  if(!E->inline_parameter_reads[c->lists[body->a+k]])continue;
  uint32_t a=c->lists[site->b+k];cc_value cv;
  if(!cc_eval(c,a,&cv))continue;
  if(cv.kind==CC_VALUE_INTEGER && codegen_type_is_integer(c,N(a)->ty))return true;
  if(cv.kind==CC_VALUE_ADDRESS && !cv.i && !cv.sym && !cv.str && !cv.label && !cv.sublabel)return true;
 }
 return false;
}

/* VIR could inline a call to s, its size aside (walk_call_can_inline's other rules, conservatively) */
VPRIV bool codegen_inline_is_candidate(codegen_unit *E, cc *c, uint32_t s)
{
	const sym *y = &c->syms[s];
	const ctype *ft;

	if (y->kind != AST_SYMBOL_FUNCTION || !(y->flags & AST_SYMBOL_FLAG_DEFINED) || !y->body)
		return false;
	ft = T(c, y->ty);
	if ((ft->flags & (AST_TYPE_FLAG_VARIADIC | AST_TYPE_FLAG_NO_PROTOTYPE)) || N(y->body)->aux > 64 || (E->symbol_flags[s] & CODEGEN_SYMBOL_NO_INLINE) || (E->once[s] & 2))
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
VPRIV void codegen_function_inline_sizes(codegen_unit *E, cc *c, uint32_t lim)
{
	uint32_t n = c->nsym, *idx, *low, *stk, *cs, *ci, sp = 0, csp = 0, nidx = 0;
	uint8_t *on;

	E->inline_parameter_reads = calloc((size_t)n + 64, 1);
	E->call_edge_offsets = calloc((size_t)n + 64, 4);
	E->call_edge_counts = calloc((size_t)n + 64, 4);
	E->recursion_groups = calloc((size_t)n + 64, 4);
	E->inline_sizes = calloc((size_t)n + 64, 2);
	idx = calloc((size_t)n + 64, 4), low = calloc((size_t)n + 64, 4), stk = malloc(((size_t)n + 64) * 4);
	cs = malloc(((size_t)n + 64) * 4), ci = malloc(((size_t)n + 64) * 4), on = calloc((size_t)n + 64, 1);
	if (!E->inline_parameter_reads || !E->call_edge_offsets || !E->call_edge_counts || !E->recursion_groups || !E->inline_sizes || !idx || !low || !stk || !cs || !ci || !on)
		abort();
	E->call_graph_recording = true;
	for (uint32_t s = 1; s < n; s++)
		if (c->syms[s].kind == AST_SYMBOL_FUNCTION && c->syms[s].body && (c->syms[s].flags & AST_SYMBOL_FLAG_DEFINED) && codegen_symbol_canonical(c, s) == s) {
			E->call_edge_offsets[s] = E->call_edge_count;
			codegen_function_walk_ensure(E, c, s);
			E->call_edge_counts[s] = E->call_edge_count - E->call_edge_offsets[s];
		}
	E->call_graph_recording = false;
	/* Tarjan, iterative; a group is finished callees first, so its isz is computed there */
	for (uint32_t r = 1; r < n; r++) {
		if (idx[r] || c->syms[r].kind != AST_SYMBOL_FUNCTION)
			continue;
		cs[csp] = r, ci[csp++] = 0;
		idx[r] = low[r] = ++nidx, stk[sp++] = r, on[r] = 1;
		while (csp) {
			uint32_t v = cs[csp - 1];

			if (ci[csp - 1] < E->call_edge_counts[v]) {
				uint32_t w = E->call_edges[E->call_edge_offsets[v] + ci[csp - 1]++].callee;

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
				E->recursion_groups[stk[--sp]] = v, on[stk[sp]] = 0;
			while (stk[sp] != v);
			for (uint32_t k = sp; k < top; k++) {
				uint32_t s = stk[k], z = E->function_node_counts[s];

				for (uint32_t j = 0; j < E->call_edge_counts[s]; j++) {
					codegen_call_edge edge=E->call_edges[E->call_edge_offsets[s]+j];
					uint32_t t=edge.callee;

					if(E->recursion_groups[t]==v || !codegen_inline_is_candidate(E,c,t))continue;
					bool take=c->syms[t].flags&AST_SYMBOL_FLAG_ALWAYS_INLINE;
					if(!take) {
						bool once=(E->once[t]&1) && c->syms[t].sc==AST_STORAGE_STATIC && !codegen_symbol_attributes(c,t)->used &&
						          E->function_node_counts[s]<=WALK_INLINE_ONCE_MAX;
						take=once ? E->function_node_counts[t]<=WALK_INLINE_ONCE_MAX : E->inline_sizes[t]<=lim ||
						 (E->inline_sizes[t]<=lim*WALK_INLINE_CONSTANT_FACTOR && codegen_inline_has_constant_argument(E,c,t,edge.call));
					}
					if(take)z=z>=65535u-E->inline_sizes[t]?65535u:z+E->inline_sizes[t];
				}
				E->inline_sizes[s] = z > 65535 ? 65535 : (uint16_t)z;
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
static int codegen_prepass_node_walk(codegen_prepass_state *P, uint32_t n);

VPRIV void codegen_function_walk_ensure(codegen_unit *E, cc *c, uint32_t s)
{
	uint8_t st = atomic_load_explicit(&E->walked[s], memory_order_acquire);

	if (likely(st == 2))
		return;
	st = 0;
	if (atomic_compare_exchange_strong(&E->walked[s], &st, 1)) {
		codegen_prepass_state P = { .E=E, .c=c, .fn=s, .llo=UINT32_MAX };
		double t = E->stats ? codegen_clock_seconds() : 0;
		const node *b = &c->nodes[c->syms[s].body];

		for (uint32_t i = 0; i < b->aux; i++)	/* params, used or not */
			codegen_prepass_local_note(&P, c->lists[b->a + i]);
		codegen_prepass_node_walk(&P, c->syms[s].body);
		E->function_local_span[2 * s] = P.llo, E->function_local_span[2 * s + 1] = P.lhi;
		E->function_node_counts[s] = P.nodes > 65535 ? 65535 : (uint16_t)P.nodes;
		if (E->stats)
			E->walk_nanoseconds += (uint64_t)((codegen_clock_seconds() - t) * 1e9), E->walk_total++;
		atomic_store_explicit(&E->walked[s], 2, memory_order_release);
		return;
	}
	while (atomic_load_explicit(&E->walked[s], memory_order_acquire) != 2)
		;	/* another thread is walking it: a few microseconds */
}

/* a static initializer takes a label's address (&&l, &&l - &&m, { &&l }) */
static bool codegen_initializer_has_label_address(cc *c, uint32_t n)
{
	const node *x;

	if (!n)
		return false;
	x = N(n);
	switch (x->op) {
	case AST_NODE_LABEL_ADDRESS:
		return true;
	case AST_NODE_INITIALIZER:
		for (uint32_t i = 0; i < x->c; i++)	/* the layout codegen_expression_has_side_effects() reads */
			if (codegen_initializer_has_label_address(c, x->aux == 1 ? c->lists[x->b + 3 * i + 2] : c->lists[x->b + i]))
				return true;
		return false;
	case AST_NODE_CAST: case AST_NODE_ADDRESS: case AST_NODE_UNARY:
		return codegen_initializer_has_label_address(c, x->a);
	case AST_NODE_BINARY:
		return codegen_initializer_has_label_address(c, x->a) || codegen_initializer_has_label_address(c, x->b);
	default:
		return false;
	}
}

/* walks every node; returns whether it contains a label (or case) */
static int codegen_prepass_node_walk(codegen_prepass_state *P, uint32_t n)
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
		if(P->E->inline_parameter_reads && !P->discard_param && (c->syms[x->a].flags&AST_SYMBOL_FLAG_PARAMETER))
			P->E->inline_parameter_reads[x->a]=1;
		/* locals belong to this walk alone (globals' flags are shared between threads) */
		if (codegen_symbol_is_local_variable(c, x->a)) {
			P->E->symbol_flags[x->a] |= CODEGEN_SYMBOL_USED;
			codegen_prepass_local_note(P, x->a);
		} else {
			const sym *y = &c->syms[x->a];

			/* a static local holding a label address: the function has one copy */
			if (y->kind == AST_SYMBOL_VARIABLE && y->sc == AST_STORAGE_STATIC && !(y->flags & AST_SYMBOL_FLAG_GLOBAL) && y->body &&
			    codegen_initializer_has_label_address(c, y->body))
				P->E->symbol_flags[P->fn] |= CODEGEN_SYMBOL_NO_INLINE;
		}
		return false;
	case AST_NODE_NUMBER: case AST_NODE_BITINT_NUMBER: case AST_NODE_FLOAT_NUMBER: case AST_NODE_STRING: case AST_NODE_ZERO:
		return false;
	case AST_NODE_SIZEOF_VLA:
		lab = codegen_prepass_node_walk(P, x->a);
		for (ast_qualified_type t = x->b; codegen_type_is_array(c, t); t = T(c, t)->base)
			if ((T(c, t)->flags & AST_TYPE_FLAG_VLA) && T(c, t)->n)
				lab |= codegen_prepass_node_walk(P, T(c, t)->n);
		break;
	case AST_NODE_LABEL_ADDRESS:
		P->E->label_flags[x->a] |= CODEGEN_LABEL_REFERENCED | CODEGEN_LABEL_BACKWARD;
		return false;
	case AST_NODE_VA_ARG:	/* va_arg reads and advances a scalar va_list through its address */
		if (codegen_valist_is_scalar(c))
			codegen_prepass_written_mark(P, x->a, true);
		lab = codegen_prepass_node_walk(P, x->a);
		break;
	case AST_NODE_MEMBER: case AST_NODE_DEREFERENCE: case AST_NODE_UNARY:
		lab = codegen_prepass_node_walk(P, x->a);
		break;
	case AST_NODE_CAST: {
		if (x->aux == AST_CAST_ARRAY_DECAY || (x->aux == AST_CAST_BIT_CAST && (N(x->a)->flags & AST_NODE_FLAG_LVALUE)))
			codegen_prepass_written_mark(P, x->a, true);
		bool saved=P->discard_param;
		if(KIND(c,x->ty)==AST_TYPE_VOID) {
			uint32_t a=x->a;
			while(N(a)->op==AST_NODE_CAST)a=N(a)->a;
			if(N(a)->op==AST_NODE_VARIABLE && (c->syms[N(a)->a].flags&AST_SYMBOL_FLAG_PARAMETER))P->discard_param=true;
		}
		lab = codegen_prepass_node_walk(P, x->a);P->discard_param=saved;
		break;
	}
	case AST_NODE_ADDRESS:
		codegen_prepass_written_mark(P, x->a, true);
		lab = codegen_prepass_node_walk(P, x->a);
		break;
	case AST_NODE_PRE_INCREMENT: case AST_NODE_PRE_DECREMENT: case AST_NODE_POST_INCREMENT: case AST_NODE_POST_DECREMENT:
		codegen_prepass_written_mark(P, x->a, false);
		lab = codegen_prepass_node_walk(P, x->a);
		break;
	case AST_NODE_ASSIGN:
		if (!x->aux && N(x->a)->op == AST_NODE_VARIABLE)
			codegen_prepass_assignment_note(P, N(x->a)->a, x->b), codegen_prepass_float_write_note(P, N(x->a)->a, x->b);
		else if (!x->aux && codegen_expression_is_constant_index(c, x->a)) {
			uint32_t s = N(N(N(x->a)->a)->a)->a;

			if (codegen_symbol_is_local_variable(c, s))
				P->E->symbol_flags[s] |= CODEGEN_SYMBOL_WRITTEN;
		}
		else
			codegen_prepass_written_mark(P, x->a, false);
		lab = codegen_prepass_node_walk(P, x->a) | codegen_prepass_node_walk(P, x->b);
		break;
	case AST_NODE_INDEX:
		if (codegen_expression_is_constant_index(c, n)) {
			/* a[CONST]: the array does not escape here */
			lab = codegen_prepass_node_walk(P, N(x->a)->a) | codegen_prepass_node_walk(P, x->b);
			break;
		}
		__attribute__((fallthrough));
	case AST_NODE_BINARY: case AST_NODE_LOGICAL_AND: case AST_NODE_LOGICAL_OR: case AST_NODE_COMMA: case AST_NODE_COMPLEX:
		lab = codegen_prepass_node_walk(P, x->a) | codegen_prepass_node_walk(P, x->b);
		break;
	case AST_NODE_CONDITIONAL:
		lab = codegen_prepass_node_walk(P, x->a) | codegen_prepass_node_walk(P, x->b) | codegen_prepass_node_walk(P, x->c);
		break;
	case AST_NODE_CALL: {
		uint32_t ref, callee = codegen_call_direct_callee(c, x->a, &ref);

		if (callee && codegen_symbol_attributes(c, callee)->error_fn)
			P->E->symbol_flags[P->fn] |= CODEGEN_SYMBOL_MUST_FOLD;
		/* VIR: a function calling setjmp is not inlined (as LLVM): the caller's promoted
		   locals would take their values at the setjmp after a longjmp */
		if (callee && P->E->once && ((codegen_symbol_attributes(c, callee)->flags & CC_ATTRIBUTE_FLAG_TAIL_RETURNS_TWICE) ||
		    CODEGEN_IDENTIFIER_IS(c->syms[callee].name, "setjmp") || CODEGEN_IDENTIFIER_IS(c->syms[callee].name, "_setjmp") ||
		    CODEGEN_IDENTIFIER_IS(c->syms[callee].name, "sigsetjmp") || CODEGEN_IDENTIFIER_IS(c->syms[callee].name, "__sigsetjmp")))
			P->E->once[P->fn] |= 2;
		if (callee && P->E->call_graph_recording) {
			codegen_unit *E = P->E;

			if (E->call_edge_count == E->call_edge_capacity) {
				codegen_call_edge *g = realloc(E->call_edges, (size_t)(E->call_edge_capacity = E->call_edge_capacity ? 2 * E->call_edge_capacity : 1024) * sizeof *g);

				if (!g)
					abort();
				E->call_edges = g;
			}
			E->call_edges[E->call_edge_count++] = (codegen_call_edge){codegen_symbol_canonical(c,callee),n};
		}
		lab = codegen_prepass_node_walk(P, x->a);
	}
		__attribute__((fallthrough));
	case AST_NODE_BUILTIN:
		if (x->op == AST_NODE_BUILTIN && (x->aux == CC_BUILTIN_ALLOCA || x->aux == CC_BUILTIN_ALLOCA_UNINITIALIZED))
			P->E->node_flags[c->syms[P->fn].body] |= CODEGEN_NODE_HAS_ALLOCA;
		for (uint32_t i = 0; i < x->c; i++)
			lab |= codegen_prepass_node_walk(P, c->lists[x->b + i]);
		/*
		 * The va builtins take their va_lists' addresses (Apple's char *
		 * could otherwise be a register variable): every operand.
		 */
		if (x->op == AST_NODE_BUILTIN && (x->aux == CC_BUILTIN_VA_START || x->aux == CC_BUILTIN_VA_COPY || x->aux == CC_BUILTIN_VA_END))
			for (uint32_t i = 0; i < x->c && i < (x->aux == CC_BUILTIN_VA_COPY ? 2u : 1u); i++)
				if ((i == 0 && x->aux != CC_BUILTIN_VA_END) || codegen_valist_is_scalar(c))
					codegen_prepass_written_mark(P, c->lists[x->b + i], true);
		break;
	case AST_NODE_STATEMENT_EXPRESSION:
		lab = codegen_prepass_node_walk(P, x->a);
		break;
	case AST_NODE_COMPOUND_LITERAL:
		lab = codegen_prepass_node_walk(P, x->a);
		break;
	case AST_NODE_INITIALIZER:
		if (x->aux == 1) {
			for (uint32_t i = 0; i < x->c; i++)
				lab |= codegen_prepass_node_walk(P, c->lists[x->b + 3 * i + 2]);
		} else {
			for (uint32_t i = 0; i < x->c; i++)
				lab |= codegen_prepass_node_walk(P, c->lists[x->b + i]);
		}
		break;
	case AST_NODE_BLOCK: {
		int cd = P->cdepth;

		for (uint32_t i = 0; i < x->c; i++)
			lab |= codegen_prepass_node_walk(P, c->lists[x->b + i]);
		P->cdepth = cd;
		break;
	}
	case AST_NODE_EXPRESSION:
	case AST_NODE_RETURN:
	case AST_NODE_GOTO_INDIRECT:
		lab = codegen_prepass_node_walk(P, x->a);
		break;
	case AST_NODE_DECLARATION: {
		const sym *y = &c->syms[x->a];

		lab = codegen_prepass_node_walk(P, x->b);
		if (!x->aux)
			for (ast_qualified_type t = y->ty; codegen_type_is_array(c, t) || codegen_type_is_pointer(c, t); t = T(c, t)->base)
				if ((T(c, t)->flags & AST_TYPE_FLAG_VLA) && T(c, t)->n)
					lab |= codegen_prepass_node_walk(P, T(c, t)->n);
		if (x->b && y->kind == AST_SYMBOL_VARIABLE)
			codegen_prepass_float_write_note(P, x->a, x->b);
		if (codegen_symbol_is_local_variable(c, x->a)) {
			codegen_prepass_local_note(P, x->a);
			if (x->b && (N(x->b)->op == AST_NODE_INITIALIZER || N(x->b)->op == AST_NODE_ZERO || N(x->b)->op == AST_NODE_STRING))
				P->E->local_needs_memory[x->a] = 1;	/* gen_init_local works on memory */
		}
		if (codegen_symbol_attributes(c, x->a)->cleanup || (y->kind == AST_SYMBOL_VARIABLE && (T(c, y->ty)->flags & AST_TYPE_FLAG_VLA)))
			P->cdepth++;
		/* a __used static local is emitted with its function, referenced or not */
		if (y->kind == AST_SYMBOL_VARIABLE && y->sc == AST_STORAGE_STATIC && codegen_symbol_attributes(c, x->a)->used &&
		    !atomic_load_explicit(&P->E->claimed[x->a], memory_order_relaxed) &&
		    !atomic_exchange(&P->E->claimed[x->a], 1))
			codegen_work_enqueue(P->E, x->a);
		break;
	}
	case AST_NODE_IF:
		lab = codegen_prepass_node_walk(P, x->a) | codegen_prepass_node_walk(P, x->b) | codegen_prepass_node_walk(P, x->c);
		break;
	/* in the order the code is generated: CODEGEN_LABEL_BACKWARD means a jump back */
	case AST_NODE_SWITCH:
		lab = codegen_prepass_node_walk(P, x->a) | codegen_prepass_node_walk(P, x->b);
		break;
	case AST_NODE_WHILE:
		P->loopd++;
		lab = codegen_prepass_node_walk(P, x->a) | codegen_prepass_node_walk(P, x->b);
		P->loopd--;
		break;
	case AST_NODE_DO:
		P->loopd++;
		lab = codegen_prepass_node_walk(P, x->b);
		lab |= codegen_prepass_node_walk(P, x->a);
		P->loopd--;
		break;
	case AST_NODE_FOR: {
		int cd = P->cdepth;

		/* the init's declarations live in the for statement (gen does not open a block for them) */
		if (x->a && N(x->a)->op == AST_NODE_BLOCK) {
			for (uint32_t i = 0; i < N(x->a)->c; i++)
				lab |= codegen_prepass_node_walk(P, c->lists[N(x->a)->b + i]);
		} else {
			lab |= codegen_prepass_node_walk(P, x->a);
		}
		P->loopd++;
		lab |= codegen_prepass_node_walk(P, x->b);
		lab |= codegen_prepass_node_walk(P, c->lists[x->c + 1]);
		lab |= codegen_prepass_node_walk(P, c->lists[x->c]);
		P->loopd--;
		P->cdepth = cd;
		break;
	}
	case AST_NODE_CASE: case AST_NODE_DEFAULT:
		codegen_prepass_node_walk(P, x->c);
		lab = 1;
		break;
	case AST_NODE_LABEL:
		P->E->label_cleanup_depth[x->a] = (uint16_t)P->cdepth;
		P->E->label_flags[x->a] |= CODEGEN_LABEL_DEFINED;
		if (c->labels[x->a].addr_taken)
			P->E->label_flags[x->a] |= CODEGEN_LABEL_REFERENCED | CODEGEN_LABEL_BACKWARD;
		codegen_prepass_node_walk(P, x->c);
		lab = 1;
		break;
	case AST_NODE_GOTO:
		P->E->label_flags[x->a] |= CODEGEN_LABEL_REFERENCED | (P->E->label_flags[x->a] & CODEGEN_LABEL_DEFINED ? CODEGEN_LABEL_BACKWARD : 0);
		break;
	case AST_NODE_ASM: {
		const ast_asm_statement *a = &c->asms[x->a];

		for (uint32_t i = 0; i < a->nout + a->nin; i++) {
			if (i < a->nout)
				codegen_prepass_written_mark(P, a->ops[i].expr, true);
			else if (strpbrk(c->strs[a->ops[i].constraint - 1].s, "mQo"))
				codegen_prepass_written_mark(P, a->ops[i].expr, true);
			else if (strpbrk(c->strs[a->ops[i].constraint - 1].s, "gVX<>") && N(a->ops[i].expr)->op == AST_NODE_VARIABLE &&
				 codegen_symbol_is_local_variable(c, N(a->ops[i].expr)->a))
				P->E->local_needs_memory[N(a->ops[i].expr)->a] = 1;	/* may become a memory operand */
			lab |= codegen_prepass_node_walk(P, a->ops[i].expr);
		}
		for (uint32_t k = 0; k < a->nlabel; k++)
			P->E->label_flags[a->labels[k]] |= CODEGEN_LABEL_REFERENCED | (P->E->label_flags[a->labels[k]] & CODEGEN_LABEL_DEFINED ? CODEGEN_LABEL_BACKWARD : 0);
		break;
	}
	default:
		break;
	}
	if (lab)
		P->E->node_flags[n] |= CODEGEN_NODE_HAS_LABEL;
	return lab;
}

VPRIV int codegen_u32_compare(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;

	return x < y ? -1 : x > y;
}

/* Inlining preserves a never-written pointer parameter's incoming object
   bound. No AST substitution: emission can run concurrently for other callers.
   Only direct parameters and effect-free actual arguments are covered here. */
VPRIV bool codegen_inline_object_size(codegen_context *e, uint32_t fn, uint32_t call, uint32_t pointer, uint32_t mode, i128 *size)
{
	cc *c = e->compile_context;
	if (!fn || !call) return false;
	while (N(pointer)->op == AST_NODE_CAST) pointer = N(pointer)->a;
	if (N(pointer)->op != AST_NODE_VARIABLE) return false;
	uint32_t p = N(pointer)->a;
	if (!codegen_type_is_pointer(c, c->syms[p].ty) || (c->syms[p].flags & AST_SYMBOL_FLAG_ADDRESS_TAKEN) ||
	    (e->unit->symbol_flags[p] & (CODEGEN_SYMBOL_WRITTEN | CODEGEN_SYMBOL_CONSTANT_WRITTEN)) || (AST_QUALIFIED_TYPE_QUALIFIERS(c->syms[p].ty) & AST_QUALIFIER_VOLATILE))
		return false;
	const node *body = N(c->syms[fn].body), *x = N(call);
	cc_value cv;
	if (!cc_eval(c, mode, &cv) || cv.kind != CC_VALUE_INTEGER || cv.i < 0 || cv.i > 3) return false;
	for (uint32_t i = 0; i < body->aux && i < x->c; i++) {
		if (c->lists[body->a + i] != p) continue;
		uint32_t arg = c->lists[x->b + i];
		return !codegen_expression_has_side_effects(c, arg) && cc_builtin_object_size(c, arg, (unsigned)cv.i, size);
	}
	return false;
}
