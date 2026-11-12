/* vcc code generator: prepass: what the emitter needs to know before it starts */
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

/* Declarations of SVE types are useful without SVE lowering (notably libc's
   math prototypes). Reject actual values before any fixed-size frame/ABI work. */
VPRIV bool sve_codegen_ok(cc *c)
{
	uint32_t loc = 0;

	if (c->o->target != PP_LINUX_AARCH64 && c->o->target != PP_DARWIN)
		return true;
	for (uint32_t i = 1; i < c->nsym && !loc; i++) {
		const sym *s = &c->syms[i];
		const ctype *t = T(c, s->ty);

		if (s->kind != S_FUNC || !s->body)
			continue;
		if (KIND(c, t->base) == TY_SVE)
			loc = s->loc;
		for (uint32_t j = 0; j < t->n && !loc; j++)
			if (KIND(c, c->params[t->aux + j]) == TY_SVE)
				loc = s->loc;
	}
	for (uint32_t i = 1; i < c->nnode && !loc; i++) {
		const node *n = &c->nodes[i];

		if (KIND(c, n->ty) == TY_SVE ||
		    (n->op == N_DECL && KIND(c, c->syms[n->a].ty) == TY_SVE))
			loc = n->loc;
	}
	if (!loc)
		return true;
	pp_diag_emit(c->p, pp_diag_begin(c->p, VDL_ERROR, loc, "SVE value code generation is not supported yet"));
	return false;
}

/* Linux x86 and arm64 lower bit-precise values. Keep the other targets
   explicit until their runtime precision and calling conventions are checked. */
VPRIV bool bitint_codegen_ok(cc *c)
{
	if (c->o->target == PP_LINUX_X86_64 || c->o->target == PP_LINUX_AARCH64)
		return true;
	for (uint32_t i = 1; i < c->nsym; i++) {
		const sym *s = &c->syms[i];
		const ctype *t = T(c, s->ty);
		bool bad = false;

		if (s->kind != S_FUNC || !s->body)
			continue;
		bad = em_bitint(c, t->base);
		for (uint32_t j = 0; j < t->n && !bad; j++)
			bad = em_bitint(c, c->params[t->aux + j]);
		if (bad) {
			pp_diag_emit(c->p, pp_diag_begin(c->p, VDL_ERROR, s->loc, "_BitInt runtime ABI is not supported on this target yet"));
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

	if (x->op != N_INDEX || N(x->a)->op != N_CAST || N(x->a)->aux != CK_DECAY || N(N(x->a)->a)->op != N_VAR)
		return false;
	for (b = x->b; N(b)->op == N_CAST && is_integer(c, N(b)->ty) && is_integer(c, N(N(b)->a)->ty); b = N(b)->a)
		;
	return N(b)->op == N_NUM && c->ipool[N(b)->a] >= 0 && c->ipool[N(b)->a] < (i128)1 << 32;
}

/* the index of a[CONST] (is_aconst) */
VPRIV uint32_t aconst_idx(cc *c, uint32_t n)
{
	uint32_t b;

	for (b = N(n)->b; N(b)->op == N_CAST; b = N(b)->a)
		;
	return (uint32_t)c->ipool[N(b)->a];
}

/* va_list is a pointer (Apple), not an array of one record (AAPCS64): it can be a register variable */
static bool scalar_valist(cc *c) { return T(c, c->t_valist)->kind != TY_ARRAY; }

static void mark_written(pre *P, uint32_t n, bool addr)
{
	cc *c = P->c;

	for (;;) {
		const node *x = N(n);

		if (x->op == N_COMMA) {
			n = x->b;	/* match the emitters' comma-lvalue addressing */
		} else if (x->op == N_MEMBER || (x->op == N_CAST && (x->aux == CK_NOP || x->aux == CK_DECAY))) {
			n = x->a;
		} else if (x->op == N_INDEX) {
			const node *b = N(x->a);

			if (b->op == N_CAST && b->aux == CK_DECAY)
				n = b->a;
			else
				return;
		} else {
			break;
		}
	}
	if (N(n)->op == N_VAR && is_local_var(c, N(n)->a)) {
		P->E->sflags[N(n)->a] |= SA_WRITTEN | SA_AESC, P->E->ffacts[N(n)->a] |= FF_BAD;
		if (addr && is_local_var(P->c, N(n)->a))
			P->E->lnoreg[N(n)->a] = 1;
	}
	else if (N(n)->op == N_COMPLIT && is_local_var(c, N(n)->b))
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
	cval cv;

	if (!is_flt(c, c->syms[s].ty) || !is_local_var(c, s))
		return;
	while (N(rhs)->op == N_CAST && is_flt(c, N(rhs)->ty) && is_flt(c, N(N(rhs)->a)->ty) &&
	       tsize(c, N(rhs)->ty) >= tsize(c, N(N(rhs)->a)->ty))
		rhs = N(rhs)->a;
	if (cc_eval(c, rhs, &cv) && (cv.kind == CV_FLT || cv.kind == CV_INT)) {
		double v = cv.kind == CV_FLT ? cv.f : (double)cv.i;

		if (cv.kind == CV_FLT && cv.ld)	/* f may be 0 or inf where q is not */
			k = cv.q == 0 ? FF_NONNEG | FF_ZERO : !(cv.q >> 127) && (cv.q << 1) <= ((u128)0x7fff << 113) ? FF_NONNEG : 0;
		else
			k = v == 0 && !__builtin_signbit(v) ? FF_NONNEG | FF_ZERO : v >= 0 ? FF_NONNEG : 0;
	} else if (N(rhs)->op == N_COMMA && (N(rhs)->aux & NC_FP_NONNEG)) {
		k = FF_NONNEG;
	} else if (N(rhs)->op == N_CALL && (callee = direct_callee(c, N(rhs)->a, &ref))) {
		const sym *fn = &c->syms[callee];
		const ctype *ft = T(c, fn->ty);
		int want = EM_IDENT_IS(fn->name, "fabs") ? TY_DOUBLE :
			   EM_IDENT_IS(fn->name, "fabsf") ? TY_FLOAT : -1;
		bool enabled = !P->E->o->no_builtin;
		/* The per-name option table belongs to the front end; conservatively
		   avoid library facts whenever any named builtin was disabled. */
		for (unsigned i = 0; i < CC_DISABLED_BUILTIN_WORDS; i++)
			if (P->E->o->disabled_builtins[i]) enabled = false;
		if (enabled && want >= 0 && (fn->flags & SF_GLOBAL) && fn->sc != SC_STATIC && !sym_attrs(c, callee)->asmname &&
		    ft->kind == TY_FUNC && KIND(c, ft->base) == want && ft->n == 1 &&
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
	cval cv;

	if (!is_local_var(c, s) || (E->sflags[s] & SA_WRITTEN))
		return;
	if (!is_integer(c, c->syms[s].ty) || !cc_eval(c, rhs, &cv) || cv.kind != CV_INT) {
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

		if (x->op == N_VAR) {
			s = x->a;
			if (s < c->nsym && c->syms[s].kind == S_FUNC && ((r[s = canon_sym(c, s)] & 3) != 3))
				r[s]++;
		} else if (x->op == N_CALL) {
			const node *f = &c->nodes[x->a];

			while (f->op == N_CAST && (f->aux == CK_FDECAY || f->aux == CK_NOP || f->aux == CK_P2P))
				f = &c->nodes[f->a];
			if (f->op == N_VAR && f->a < c->nsym && c->syms[f->a].kind == S_FUNC && ((r[s = canon_sym(c, f->a)] >> 2) != 3))
				r[s] += 4;
		}
	}
	for (uint32_t s = 0; s < c->nsym; s++)
		r[s] = r[s] == (1 | 4);	/* (bit 1: calls setjmp, set by the prepass) */
	return r;
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
		pre P = { E, c, s, 0, 0, 0, UINT32_MAX, 0 };
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
	case N_LABELADDR:
		return true;
	case N_INIT:
		for (uint32_t i = 0; i < x->c; i++)	/* the layout side_effects() reads */
			if (init_has_labeladdr(c, x->aux == 1 ? c->lists[x->b + 3 * i + 2] : c->lists[x->b + i]))
				return true;
		return false;
	case N_CAST: case N_ADDR: case N_UNARY:
		return init_has_labeladdr(c, x->a);
	case N_BINARY:
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
	case N_VAR:
		/* locals belong to this walk alone (globals' flags are shared between threads) */
		if (is_local_var(c, x->a)) {
			P->E->sflags[x->a] |= SA_USED;
			note_local(P, x->a);
		} else {
			const sym *y = &c->syms[x->a];

			/* a static local holding a label address: the function has one copy */
			if (y->kind == S_VAR && y->sc == SC_STATIC && !(y->flags & SF_GLOBAL) && y->body &&
			    init_has_labeladdr(c, y->body))
				P->E->sflags[P->fn] |= SA_NOINL;
		}
		return false;
	case N_NUM: case N_BIGNUM: case N_FNUM: case N_STR: case N_ZERO:
		return false;
	case N_SIZEOF_VLA:
		lab = walk(P, x->a);
		for (qty t = x->b; is_array(c, t); t = T(c, t)->base)
			if ((T(c, t)->flags & TF_VLA) && T(c, t)->n)
				lab |= walk(P, T(c, t)->n);
		break;
	case N_LABELADDR:
		P->E->lref[x->a] |= LR_REF | LR_BACK;
		return false;
	case N_VAARG:	/* va_arg reads and advances a scalar va_list through its address */
		if (scalar_valist(c))
			mark_written(P, x->a, true);
		lab = walk(P, x->a);
		break;
	case N_MEMBER: case N_DEREF: case N_UNARY:
		lab = walk(P, x->a);
		break;
	case N_CAST:
		if (x->aux == CK_DECAY || (x->aux == CK_BITS && (N(x->a)->flags & NF_LVALUE)))
			mark_written(P, x->a, true);
		lab = walk(P, x->a);
		break;
	case N_ADDR:
		mark_written(P, x->a, true);
		lab = walk(P, x->a);
		break;
	case N_PREINC: case N_PREDEC: case N_POSTINC: case N_POSTDEC:
		mark_written(P, x->a, false);
		lab = walk(P, x->a);
		break;
	case N_ASSIGN:
		if (!x->aux && N(x->a)->op == N_VAR)
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
	case N_INDEX:
		if (is_aconst(c, n)) {
			/* a[CONST]: the array does not escape here */
			lab = walk(P, N(x->a)->a) | walk(P, x->b);
			break;
		}
		__attribute__((fallthrough));
	case N_BINARY: case N_LOGAND: case N_LOGOR: case N_COMMA: case N_CPLX:
		lab = walk(P, x->a) | walk(P, x->b);
		break;
	case N_COND:
		lab = walk(P, x->a) | walk(P, x->b) | walk(P, x->c);
		break;
	case N_CALL: {
		uint32_t ref, callee = direct_callee(c, x->a, &ref);

		if (callee && sym_attrs(c, callee)->error_fn)
			P->E->sflags[P->fn] |= SA_MUSTFOLD;
		/* VIR: a function calling setjmp is not inlined (as LLVM): the caller's promoted
		   locals would take their values at the setjmp after a longjmp */
		if (callee && P->E->once && ((sym_attrs(c, callee)->flags & AF_TAIL_RETURNS_TWICE) ||
		    EM_IDENT_IS(c->syms[callee].name, "setjmp") || EM_IDENT_IS(c->syms[callee].name, "_setjmp") ||
		    EM_IDENT_IS(c->syms[callee].name, "sigsetjmp") || EM_IDENT_IS(c->syms[callee].name, "__sigsetjmp")))
			P->E->once[P->fn] |= 2;
		lab = walk(P, x->a);
	}
		__attribute__((fallthrough));
	case N_BUILTIN:
		if (x->op == N_BUILTIN && (x->aux == BI_ALLOCA || x->aux == BI_ALLOCA_UNINIT))
			P->E->nflags[c->syms[P->fn].body] |= NA_ALLOCA;
		for (uint32_t i = 0; i < x->c; i++)
			lab |= walk(P, c->lists[x->b + i]);
		/*
		 * The va builtins take their va_lists' addresses (Apple's char *
		 * could otherwise be a register variable): every operand.
		 */
		if (x->op == N_BUILTIN && (x->aux == BI_VA_START || x->aux == BI_VA_COPY || x->aux == BI_VA_END))
			for (uint32_t i = 0; i < x->c && i < (x->aux == BI_VA_COPY ? 2u : 1u); i++)
				if ((i == 0 && x->aux != BI_VA_END) || scalar_valist(c))
					mark_written(P, c->lists[x->b + i], true);
		break;
	case N_STMTEXPR:
		lab = walk(P, x->a);
		break;
	case N_COMPLIT:
		lab = walk(P, x->a);
		break;
	case N_INIT:
		if (x->aux == 1) {
			for (uint32_t i = 0; i < x->c; i++)
				lab |= walk(P, c->lists[x->b + 3 * i + 2]);
		} else {
			for (uint32_t i = 0; i < x->c; i++)
				lab |= walk(P, c->lists[x->b + i]);
		}
		break;
	case N_BLOCK: {
		int cd = P->cdepth;

		for (uint32_t i = 0; i < x->c; i++)
			lab |= walk(P, c->lists[x->b + i]);
		P->cdepth = cd;
		break;
	}
	case N_EXPR:
	case N_RETURN:
	case N_GOTO_IND:
		lab = walk(P, x->a);
		break;
	case N_DECL: {
		const sym *y = &c->syms[x->a];

		lab = walk(P, x->b);
		if (!x->aux)
			for (qty t = y->ty; is_array(c, t) || is_ptr(c, t); t = T(c, t)->base)
				if ((T(c, t)->flags & TF_VLA) && T(c, t)->n)
					lab |= walk(P, T(c, t)->n);
		if (x->b && y->kind == S_VAR)
			note_fwrite(P, x->a, x->b);
		if (is_local_var(c, x->a)) {
			note_local(P, x->a);
			if (x->b && (N(x->b)->op == N_INIT || N(x->b)->op == N_ZERO || N(x->b)->op == N_STR))
				P->E->lnoreg[x->a] = 1;	/* gen_init_local works on memory */
		}
		if (sym_attrs(c, x->a)->cleanup || (y->kind == S_VAR && (T(c, y->ty)->flags & TF_VLA)))
			P->cdepth++;
		/* a __used static local is emitted with its function, referenced or not */
		if (y->kind == S_VAR && y->sc == SC_STATIC && sym_attrs(c, x->a)->used &&
		    !atomic_load_explicit(&P->E->claimed[x->a], memory_order_relaxed) &&
		    !atomic_exchange(&P->E->claimed[x->a], 1))
			enqueue(P->E, x->a);
		break;
	}
	case N_IF:
		lab = walk(P, x->a) | walk(P, x->b) | walk(P, x->c);
		break;
	/* in the order the code is generated: LR_BACK means a jump back */
	case N_SWITCH:
		lab = walk(P, x->a) | walk(P, x->b);
		break;
	case N_WHILE:
		P->loopd++;
		lab = walk(P, x->a) | walk(P, x->b);
		P->loopd--;
		break;
	case N_DO:
		P->loopd++;
		lab = walk(P, x->b);
		lab |= walk(P, x->a);
		P->loopd--;
		break;
	case N_FOR: {
		int cd = P->cdepth;

		/* the init's declarations live in the for statement (gen does not open a block for them) */
		if (x->a && N(x->a)->op == N_BLOCK) {
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
	case N_CASE: case N_DEFAULT:
		walk(P, x->c);
		lab = 1;
		break;
	case N_LABEL:
		P->E->lcdepth[x->a] = (uint16_t)P->cdepth;
		P->E->lref[x->a] |= LR_DEF;
		if (c->labels[x->a].addr_taken)
			P->E->lref[x->a] |= LR_REF | LR_BACK;
		walk(P, x->c);
		lab = 1;
		break;
	case N_GOTO:
		P->E->lref[x->a] |= LR_REF | (P->E->lref[x->a] & LR_DEF ? LR_BACK : 0);
		break;
	case N_ASM: {
		const casm *a = &c->asms[x->a];

		for (uint32_t i = 0; i < a->nout + a->nin; i++) {
			if (i < a->nout)
				mark_written(P, a->ops[i].expr, true);
			else if (strpbrk(c->strs[a->ops[i].constraint - 1].s, "mQo"))
				mark_written(P, a->ops[i].expr, true);
			else if (strpbrk(c->strs[a->ops[i].constraint - 1].s, "gVX<>") && N(a->ops[i].expr)->op == N_VAR &&
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
	while (N(pointer)->op == N_CAST) pointer = N(pointer)->a;
	if (N(pointer)->op != N_VAR) return false;
	uint32_t p = N(pointer)->a;
	if (!is_ptr(c, c->syms[p].ty) || (c->syms[p].flags & SF_ADDR_TAKEN) ||
	    (e->E->sflags[p] & (SA_WRITTEN | SA_CWRITE)) || (QQ(c->syms[p].ty) & Q_VOLATILE))
		return false;
	const node *body = N(c->syms[fn].body), *x = N(call);
	cval cv;
	if (!cc_eval(c, mode, &cv) || cv.kind != CV_INT || cv.i < 0 || cv.i > 3) return false;
	for (uint32_t i = 0; i < body->aux && i < x->c; i++) {
		if (c->lists[body->a + i] != p) continue;
		uint32_t arg = c->lists[x->b + i];
		return !side_effects(c, arg) && cc_object_size(c, arg, (unsigned)cv.i, size);
	}
	return false;
}
