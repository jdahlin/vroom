/* vcc code generator: calls and inline expansion */
#include "../../../codegen/emitimpl.h"

/* the first report of an error() call at loc (inlined copies share it) */
static bool err_call_once(ecx *e, uint32_t loc)
{
	emitter *E = e->E;
	bool fresh = true;

	pthread_mutex_lock(&E->emu);
	for (uint32_t i = 0; i < E->nerrcall_loc; i++)
		if (E->errcall[i] == loc)
			fresh = false;
	if (fresh && E->nerrcall_loc < 256)
		E->errcall[E->nerrcall_loc++] = loc;
	pthread_mutex_unlock(&E->emu);
	return fresh;
}

#define MAXARGS 128

/* park an argument value: registers go to a slot, aggregates are copied */
static void park(ecx *e, carg *a)
{
	cc *c = e->c;

	if (a->slot != 1)
		return;		/* parked while the arguments were evaluated */
	if (is_agg(c, a->t)) {
		uint64_t sz = (tsize(c, a->t) + 7) & ~(uint64_t)7;
		int64_t sl = slot(e, sz ? sz : 8, em_bigint(c, a->t) ? talign(c, a->t) : talign(c, a->t) > 8 ? 16 : 8);
		int rs = toreg(e, &a->v), rd = treg(e);

		opi(e, I_ADDI, 1, rd, FRAME, sl);
		copy_mem(e, rd, rs, tsize(c, a->t));
		tfree(e, rd);
		val_free(e, &a->v);
		a->slot = sl;
		return;
	}
	if (a->v.k == V_REG) {
		a->slot = slot(e, 8, 8);
		mem(e, I_ST, 3, 0, a->v.r, FRAME, a->slot);
		val_free(e, &a->v);
	} else if (a->v.k == V_REG2) {
		a->slot = slot(e, 16, 16);
		mem(e, I_ST, 3, 0, a->v.r, FRAME, a->slot);
		mem(e, I_ST, 3, 0, a->v.r2, FRAME, a->slot + 8);
		val_free(e, &a->v);
	}
}

/* a parked argument back in a register (aggregates: their copy's address) */
static void unpark(ecx *e, carg *a)
{
	if (a->slot == 1)
		return;
	if (is_agg(e->c, a->t)) {
		int r = treg(e);

		opi(e, I_ADDI, 1, r, FRAME, a->slot);
		a->v = vreg(r);
	} else {
		lval l = { .k = L_FRAME, .off = a->slot, .align = 8 };

		a->v = load(e, &l, a->t);
	}
	a->slot = 1;
}

/* put 8-byte word k of argument a into register rd */
static void arg_word(ecx *e, const carg *a, int k, int rd)
{
	if (a->slot != 1) {
		mem(e, I_LD, 3, 0, rd, FRAME, a->slot + 8 * k);
		return;
	}
	if (a->v.k == V_REG || a->v.k == V_REG2) {	/* kept in its temporary (do_call) */
		mov(e, 1, rd, k ? a->v.r2 : a->v.r);
	} else if (a->v.k == V_CONST) {
		movi(e, 1, rd, (int64_t)(uint64_t)((u128)a->v.i >> (64 * k)));
	} else if (a->v.k == V_ADDR) {
		addr_of_ref(e, rd, a->v.ref, (int64_t)a->v.i);
	} else {
		movi(e, 1, rd, 0);
	}
}

/*
 * The call proper: args are evaluated (vals); callee is a ref (direct) or a
 * value (indirect).  Returns the result value.
 */
/*
 * A call to an error() function survived folding.  clang -O2 would have
 * inlined the static function this code belongs to into its callers, where
 * the arguments are constants: mark it, and emit_tu generates the TU again
 * with it inlined.
 */
static void need_fold(ecx *e)
{
	const fnctx *f = e->f;
	uint32_t s;

	while (f->up)
		f = f->up;
	s = f->fn;
	if (e->c->syms[s].sc == SC_STATIC && !e->E->mf[s]) {
		e->E->mf[s] = 1;
		e->E->newmf++;
	}
}

/* nfix: the callee's named parameters if it is variadic, else -1 */
VPRIV val do_call(ecx *e, uint32_t ref, val *fnv, carg *args, int n, qty rty, bool noret, int nfix)
{
	cc *c = e->c;
	int ngrn = 0, nsaa = 0;
	int64_t fslot = 1, rslot = 1;
	bool sret = is_sret(c, rty);
	uint32_t keep = 0;	/* temporaries holding register arguments: moved, not parked or spilled */
	spill sp;
	val res = vnone();
	int hn;

	e->calls = true;
	if (fnv) {
		if (fnv->k == V_ADDR && fnv->i == 0) {
			ref = fnv->ref;
			note_ref(e, ref);
		} else {
			int r = toreg(e, fnv);

			fslot = slot(e, 8, 8);
			mem(e, I_ST, 3, 0, r, FRAME, fslot);
			val_free(e, fnv);
		}
	} else {
		note_ref(e, ref);
	}
	e->nsrn = 0;
	for (int i = 0; i < n; i++) {
		carg *a = &args[i];

		if (!DARWIN(c) && nfix >= 0 && i >= nfix && is_bvec(c, a->t) &&
		    (T(c, a->t)->n == 64 || T(c, a->t)->n == 128))
			efatal(e, e->curloc, "arm64: variadic transport of 64/128-lane packed boolean vectors is not supported");

		if (nfix >= 0 && i >= nfix && DARWIN(c))
			classify_va(e, a->t, &nsaa, &a->a);
		else
			classify(e, a->t, &ngrn, &nsaa, &a->a);
		/* a scalar for x0-x7 in a temporary stays there until its mov (the moves
		 * write only x0-x7, never a temporary, so their order does not matter) */
		if (e->E->o->opt >= 1 && a->slot == 1 && a->a.kind == A_REG && !is_agg(c, a->t) &&
		    ((a->v.k == V_REG && a->a.nreg == 1) || (a->v.k == V_REG2 && a->a.nreg == 2)) &&
		    tidx(a->v.r) >= 0 && !(e->resv & (1u << tidx(a->v.r))) &&
		    (a->v.k == V_REG || (tidx(a->v.r2) >= 0 && !(e->resv & (1u << tidx(a->v.r2)))))) {
			keep |= 1u << tidx(a->v.r);
			if (a->v.k == V_REG2)
				keep |= 1u << tidx(a->v.r2);
			continue;
		}
		park(e, a);
	}
	if (DARWIN(c))
		nsaa = (nsaa + 7) & ~7;
	if (nsaa > e->outmax)
		e->outmax = nsaa;
	if (sret)
		rslot = slot(e, tsize(c, rty), talign(c, rty) > 16 ? 16 : talign(c, rty));
	if (!e->reach)
		return is_void(c, rty) ? vnone() : vconst(0);
	if (unlikely(RKIND(ref) == RK_SYM && !fnv && sym_attrs(c, RIDX(ref))->error_fn)) {
		/*
		 * An error() call that folding did not remove.  clang removed it
		 * (the tree builds with clang), with value ranges vcc does not
		 * have (min(len, ...) inside while (len > 0)).  Drop the call and
		 * go on, as the kernel's own -O0 fallback does
		 * (__compiletime_assert is then ((void)(condition))): a trap
		 * here would fire on paths clang proved are fine.
		 */
		if (!e->dry && !e->E->o->vcc_kernel) {
			/* as clang: every call that survives is an error */
			const attrs *at = sym_attrs(c, RIDX(ref));
			const vident *nm = &vintern_ents[c->syms[RIDX(ref)].name];

			if (err_call_once(e, e->curloc))
				eerror(e, e->curloc, "call to '%.*s' declared with 'error' attribute: %s", (int)nm->len, nm->s,
				       at->errmsg ? c->strs[at->errmsg - 1].s : "");
		} else if (!e->dry) {
			need_fold(e);
			e->E->nerrcall++;
			if (e->E->stats)
				ediag(e, e->curloc, "error() call '%.*s' not folded: dropped", (int)vintern_ents[c->syms[RIDX(ref)].name].len,
				      vintern_ents[c->syms[RIDX(ref)].name].s);
		}
		return is_void(c, rty) ? vnone() : vconst(0);
	}
	e->live &= ~keep;	/* dead after their moves: not spilled */
	spill_live(e, &sp);
	e->live |= keep;
	for (int i = 0; i < n; i++) {
		carg *a = &args[i];

		switch (a->a.kind) {
		case A_BOOL_LANES:
			for (unsigned k = 0; k < T(c, a->t)->n; k++) {
				int rd = k < a->a.nreg ? a->a.reg + k : R_X16;
				mem(e, I_LD, 0, 0, rd, FRAME, a->slot + k / 8);
				opbf(e, I_UBFM, 1, rd, rd, k % 8, k % 8);
				if (k >= a->a.nreg) mem(e, I_ST, 3, 0, rd, SP, a->a.stk + (k - a->a.nreg) * 8);
			}
			break;
		case A_REG:
			for (int k = 0; k < a->a.nreg; k++)
				arg_word(e, a, k, a->a.reg + k);
			if (darwin_narrow(c, a->t))	/* Apple: the caller extends char/short/_Bool to 32 bits */
				extend(e, a->a.reg, a->a.reg, (int)tsize(c, a->t), is_signed(c, a->t));
			break;
		case A_STACK:
			if (DARWIN(c) && a->a.size < 8 && !is_agg(c, a->t)) {	/* Apple: packed */
				/* a variadic one has an 8-byte slot: store all of it, as clang
				 * does, so va_arg(ap, T *) of a literal 0 (sqlite's shell)
				 * reads no stale upper half */
				int va = nfix >= 0 && i >= nfix;

				arg_word(e, a, 0, R_X16);
				mem(e, I_ST, va ? 3 : log2sz(a->a.size), 0, R_X16, SP, a->a.stk);
				break;
			}
			for (int k = 0; k < a->a.nreg; k++) {
				/* an Apple HFA of floats on the stack: a 4-byte last part */
				int lg = a->a.fsz && a->a.size - 8 * k < 8 ? 2 : 3;

				arg_word(e, a, k, R_X16);
				mem(e, I_ST, lg, 0, R_X16, SP, a->a.stk + 8 * k);
			}
			break;
		case A_REF_REG:
			opi(e, I_ADDI, 1, a->a.reg, FRAME, a->slot);
			break;
		case A_REF_STACK:
			opi(e, I_ADDI, 1, R_X16, FRAME, a->slot);
			mem(e, I_ST, 3, 0, R_X16, SP, a->a.stk);
			break;
		case A_VREG:
			arg_word(e, a, 0, R_X16);
			word(e, f_in(a->a.size >= 8, a->a.reg, R_X16));
			if (a->a.size == 16) {
				arg_word(e, a, 1, R_X16);
				word(e, q_in_hi(a->a.reg, R_X16));
			}
			break;
		case A_HFA:
			for (int k = 0; k < a->a.nreg; k++)
				vr_load(e, a->a.fsz, a->a.reg + k, FRAME, a->slot + a->a.fsz * k);
			break;
		}
	}
	for (int i = 0; i < n && keep; i++)
		if (args[i].slot == 1 && (args[i].v.k == V_REG || args[i].v.k == V_REG2) && args[i].a.kind == A_REG)
			val_free(e, &args[i].v);
	if (sret)
		opi(e, I_ADDI, 1, 8, FRAME, rslot);
	if (fslot != 1) {
		mem(e, I_LD, 3, 0, R_X16, FRAME, fslot);
		put(e, I_BLR)->rn = R_X16;
	} else {
		put(e, I_BL)->ref = ref;
	}
	if (is_void(c, rty)) {
		res = vnone();
	} else if (sret) {
		int r = treg(e);

		opi(e, I_ADDI, 1, r, FRAME, rslot);
		res = vreg(r);
	} else if (is_bvec(c, rty)) {
		int64_t sl = slot(e, 8, 8);
		int r = treg(e);
		bvec_res(e, rty, sl);
		opi(e, I_ADDI, 1, r, FRAME, sl);
		res = vreg(r);
	} else if (is_recabi(c, rty) && hfa(c, rty, &hn)) {
		int hb = hfa(c, rty, &hn);
		int64_t sl = slot(e, 64, 16);
		int r = treg(e);

		for (int k = 0; k < hn; k++)
			vr_store(e, hb, k, FRAME, sl + hb * k);
		opi(e, I_ADDI, 1, r, FRAME, sl);
		res = vreg(r);
	} else if (vec_small(c, rty)) {
		int64_t sl = slot(e, 8, 8);
		int r = treg(e);

		vec_small_res(e, rty, sl);
		opi(e, I_ADDI, 1, r, FRAME, sl);
		res = vreg(r);
	} else if (is_recabi(c, rty)) {
		int64_t sl = slot(e, 16, 16);
		int r = treg(e);

		mem(e, I_ST, 3, 0, 0, FRAME, sl);
		if (tsize(c, rty) > 8)
			mem(e, I_ST, 3, 0, 1, FRAME, sl + 8);
		opi(e, I_ADDI, 1, r, FRAME, sl);
		res = vreg(r);
	} else if (is_ld(c, rty)) {
		res.k = V_REG2;
		res.r = (uint8_t)treg(e);
		res.r2 = (uint8_t)treg(e);
		word(e, f_out(1, res.r, 0));
		word(e, q_out_hi(res.r2, 0));
	} else if (tsize(c, rty) == 16 && !is_flt(c, rty)) {
		res.k = V_REG2;
		res.r = (uint8_t)treg(e);
		res.r2 = (uint8_t)treg(e);
		mov(e, 1, res.r, 0);
		mov(e, 1, res.r2, 1);
	} else if (is_flt(c, rty)) {
		int r = treg(e);

		word(e, f_out(fpk(e, rty), r, 0));
		res = vreg(r);
	} else {
		int r = treg(e);

		mov(e, 1, r, 0);
		res = vreg(r);
	}
	unspill(e, &sp);
	if (noret)
		e->reach = false;
	return res;
}

VPRIV val call_named(ecx *e, uint32_t ref, val *vals, qty *tys, int n, qty rty, bool noret)
{
	carg a[8];

	for (int i = 0; i < n; i++) {
		a[i].v = vals[i];
		a[i].t = tys[i];
		a[i].slot = 1;
	}
	return do_call(e, ref, NULL, a, n, rty, noret, -1);
}

/* ---------------- inline expansion ---------------- */

static bool on_inline_stack(ecx *e, uint32_t fn)
{
	for (fnctx *f = e->f; f; f = f->up)
		if (f->fn == fn)
			return true;
	return false;
}

/* a local only ever assigned constants: follow its value (v: the initializer, or none) */
VPRIV void kv_track(ecx *e, uint32_t s, const val *v)
{
	cc *c = e->c;
	const sym *y = &c->syms[s];
	bind *b = binding(e, s);
	int k;

	if ((e->E->sflags[s] & (SA_WRITTEN | SA_CWRITE)) != SA_CWRITE || !is_integer(c, y->ty) || is_wide(c, y->ty) ||
	    (QQ(y->ty) & Q_VOLATILE) || (T(c, y->ty)->flags & TF_ATOMIC) || sym_attrs(c, s)->cleanup || e->nkv == NKV)
		return;
	k = e->nkv++;
	b->kv = (uint8_t)(k + 1);
	e->kvalid &= (uint16_t)~(1u << k);
	if (v && v->k == V_CONST) {
		e->kval[k] = (int64_t)em_trunc_to(c, v->i, y->ty);
		e->kvalid |= (uint16_t)(1u << k);
	}
}

/* may variable s (param or local) be bound to the constant v for good? */
VPRIV bool const_var(ecx *e, uint32_t s, const val *v)
{
	uint8_t f = e->E->sflags[s];

	if ((T(e->c, e->c->syms[s].ty)->flags & TF_ATOMIC) || (f & (SA_WRITTEN | SA_CMULTI)))
		return false;
	if (f & SA_CWRITE)
		return v->k == V_CONST && em_trunc_to(e->c, v->i, e->c->syms[s].ty) == e->E->cwval[s];
	return v->k == V_CONST || v->k == V_ADDR;
}

#define INLINE_NODES 40	/* AST nodes: a few statements */
#ifndef INLINE_STATIC_NODES
#define INLINE_STATIC_NODES 40	/* the same for a static function not declared inline (clang -O2 inlines it) */
#endif

/* a static function clang would inline: no inline asm, no locals aligned above 16 */
static bool tiny_ok(cc *c, uint32_t n)
{
	const node *x;

	if (!n)
		return true;
	x = N(n);
	switch (x->op) {
	case N_ASM:
		return false;
	case N_DECL:
		return c->syms[x->a].kind != S_VAR || sym_align(c, x->a) <= 16;
	case N_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			if (!tiny_ok(c, c->lists[x->b + i]))
				return false;
		return true;
	case N_IF:
		return tiny_ok(c, x->b) && tiny_ok(c, x->c);
	case N_WHILE: case N_DO: case N_SWITCH: case N_LABEL: case N_CASE: case N_DEFAULT:
		return tiny_ok(c, x->op == N_LABEL || x->op == N_CASE || x->op == N_DEFAULT ? x->c : x->b);
	case N_FOR:
		return tiny_ok(c, c->lists[x->c + 1]);
	default:
		return true;
	}
}

VPRIV bool can_inline(ecx *e, uint32_t s)
{
	cc *c = e->c;
	const sym *y = &c->syms[s];

	if (!(y->flags & SF_DEFINED) || !y->body)
		return false;
	if (!(y->flags & SF_ALWAYS_INLINE)) {
		/*
		 * A plain inline function that calls an error() function (the
		 * kernel's BUILD_BUG_ON) only compiles when an optimizing compiler
		 * inlines it and folds the call away: inline it too.
		 */
		if (y->flags & SF_NOINLINE)
			return false;
		if (!e->E->mf[s]) {
			if (y->sc == SC_STATIC)
				rlog(e, RL_DEP, s);
			if (!(y->flags & SF_INLINE) && (y->sc != SC_STATIC || !e->E->o->opt))
				return false;
			walk_fn(e, s);
			if (!(y->flags & SF_INLINE)) {
				/* a plain static function: small, no inline asm or over-aligned locals */
				if (e->E->fnsize[s] > INLINE_STATIC_NODES || !tiny_ok(c, y->body))
					return false;
			}
			/*
			 * clang -O2 inlines small inline functions, and the kernel
			 * leans on it: scs_is_enabled() { return false; } guards
			 * references to objects that only exist when it is true.
			 */
			if (!(e->E->sflags[s] & SA_MUSTFOLD) && e->E->fnsize[s] > INLINE_NODES)
				return false;
		}
	}
	if (T(c, y->ty)->flags & TF_VARIADIC)
		return false;
	ensure_walked(e->E, c, s);
	if (e->E->sflags[s] & SA_NOINL)
		return false;
	if (e->inline_depth >= 48 || on_inline_stack(e, s))
		return false;
	return true;
}

/* temporaries an inlined call leaves free when it keeps params and its result in registers:
   e->inlfree, INLINE_FREE_FAST first, INLINE_FREE_SAFE when generated again (out of registers) */
#define INLINE_FREE (e->inlfree ? e->inlfree : INLINE_FREE_SAFE)

/* an integer or pointer that fits one register (what B_REG and retreg hold) */
static bool reg_scalar(ecx *e, qty t)
{
	cc *c = e->c;

	return (is_integer(c, t) || is_ptr(c, t)) && !is_wide(c, t) && !(QQ(t) & Q_VOLATILE) && !(T(c, t)->flags & TF_ATOMIC) && tsize(c, t) - 1 < 8;
}

static bool is_temp(int r)
{
	for (int i = 0; i < NTEMP; i++)
		if (tempregs[i] == r)
			return true;
	return false;
}

VPRIV val inline_call(ecx *e, uint32_t s, carg *args, int n, uint32_t call)
{
	cc *c = e->c;
	const sym *y = &c->syms[s];
	const node *body = N(y->body);
	fnctx F;
	qty rty = T(c, y->ty)->base;
	int64_t saved_sp = 0;
	val res;

	walk_fn(e, s);
	memset(&F, 0, sizeof(F));
	F.fn = s;
	F.call = call;
	F.inst = ++e->inst_next;
	F.inl = true;
	F.rty = rty;
	F.retlbl = newlabel(e);
	F.cst_ok = true;
	F.cbase = e->nclean;
	F.nloop = e->nloop;
	F.up = e->f;
	if (e->E->o->opt >= 1 && reg_scalar(e, rty) && NTEMP - __builtin_popcount(e->live) > INLINE_FREE)
		F.retreg = (uint8_t)(treg(e) + 1);	/* returns put the value there: no slot */
	else if (!is_void(c, rty))
		F.retslot = slot(e, tsize(c, rty) ? tsize(c, rty) : 8, talign(c, rty) > 16 ? 16 : talign(c, rty));
	for (int i = 0; i < (int)body->aux; i++) {
		uint32_t p = c->lists[body->a + i];
		const sym *ps = &c->syms[p];
		bind *b = binding(e, p);
		/* Reload only this parameter: restoring all parked values together
		 * exhausts temporaries for wide or numerous inline arguments. */
		if (i < n)
			unpark(e, &args[i]);
		val v = i < n ? args[i].v : vconst(0);

		if (const_var(e, p, &v) && !is_agg(c, ps->ty) &&
		    !(QQ(ps->ty) & Q_VOLATILE) && !is_wide(c, ps->ty)) {
			b->inst = F.inst;
			b->kind = B_CONST;
			b->kv = 0;
			b->v = v;
			continue;
		}
		/*
		 * A param the body never writes (nor takes the address of: that
		 * counts as a write) keeps the argument's register: reads copy it.
		 * Extended as a load from its slot would be, so it is the same value.
		 */
		if (e->E->o->opt >= 1 && i < n && v.k == V_REG && is_temp(v.r) && !(e->E->sflags[p] & (SA_WRITTEN | SA_CWRITE)) &&
		    reg_scalar(e, ps->ty) && !sym_attrs(c, p)->cleanup && ps->sc != SC_REGISTER &&
		    NTEMP - __builtin_popcount(e->live) > INLINE_FREE) {
			uint64_t sz = tsize(c, ps->ty);

			if (sz < 8)
				extend(e, v.r, v.r, (int)sz, is_signed(c, ps->ty));
			b->inst = F.inst;
			b->kind = B_REG;
			b->kv = 0;
			b->v = v;
			continue;
		}
		{
			int64_t sl;
			lval l = { .k = L_FRAME };

			b->inst = 0;
			sl = bind_slot(e, p);
			binding(e, p)->inst = F.inst;
			l.off = sl;
			l.align = talign(c, ps->ty);
			store(e, &l, ps->ty, &v);
			val_free(e, &v);
		}
	}
	if (e->E->nflags[y->body] & NA_ALLOCA) {
		int r = treg(e);
		saved_sp = slot(e, 8, 8);
		mov(e, 1, r, SP);
		mem(e, I_ST, 3, 0, r, FRAME, saved_sp);
		tfree(e, r);
	}
	e->f = &F;
	e->inline_depth++;
	e->E->ninl++;
	gen_stmt(e, y->body);
	e->inline_depth--;
	e->f = F.up;
	deflabel(e, F.retlbl);
	if (saved_sp) {
		int r = treg(e);
		mem(e, I_LD, 3, 0, r, FRAME, saved_sp);
		mov(e, 1, SP, r);
		tfree(e, r);
	}
	for (int i = 0; i < (int)body->aux; i++) {
		bind *b = binding(e, c->lists[body->a + i]);

		if (b->kind == B_REG && b->inst == F.inst) {
			tfree(e, b->v.r);
			b->kind = B_NONE, b->inst = 0;
		}
	}
	if (F.retreg) {
		if (F.cst_ok && F.any_ret) {
			tfree(e, F.retreg - 1);
			return F.cst;
		}
		return vreg(F.retreg - 1);
	}
	if (is_void(c, rty))
		return vnone();
	if (F.cst_ok && F.any_ret)
		return F.cst;
	{
		lval l = { .k = L_FRAME, .off = F.retslot, .align = talign(c, rty) };

		res = load(e, &l, rty);
	}
	return res;
}

/* __builtin_strcmp of two string literals (the kernel picks operators by name) */
static val fold_strcmp(ecx *e, const node *x)
{
	cc *c = e->c;
	uint32_t f = strip_casts(c, x->a), a = strip_casts(c, c->lists[x->b]), b = strip_casts(c, c->lists[x->b + 1]);
	int r;

	if (N(f)->op != N_VAR || c->syms[N(f)->a].kind != S_BUILTIN || N(a)->op != N_STR || N(b)->op != N_STR ||
	    cc_builtin_libkind(c->syms[N(f)->a].val) != CC_LIB_STRCMP)
		return vnone();
	r = strcmp(c->strs[N(a)->a].s, c->strs[N(b)->a].s);
	return vconst(r < 0 ? -1 : r > 0);
}

/*
 * __builtin_memcpy/memmove/memset with a constant size: inline stores, as
 * clang at every -O and under -fno-builtin/-ffreestanding (LLVM's memcpy
 * intrinsic; the vDSO has no memcpy to call: get/put_unaligned in
 * lib/vdso/getrandom.c).  Plain memcpy() stays a call.  The value is dst.
 */
/* one ldp/stp of x registers at [base + off] (off: 8-aligned, -512..504) */
static void pair(ecx *e, int op, int r1, int r2, int base, int64_t off)
{
	ins *i;

	if (!e->reach)
		return;
	i = put(e, op);
	i->sz = 1, i->rd = (uint8_t)r1, i->ra = (uint8_t)r2, i->rn = (uint8_t)base, i->imm = off;
}

/* memcpy of 16 to 128 bytes: ldp/stp pairs, the rest as copy_mem */
static void copy_pairs(ecx *e, int d, int sr, uint64_t n)
{
	uint64_t o = 0;

	for (; n - o >= 16; o += 16) {
		pair(e, I_LDP, R_X16, R_X17, sr, (int64_t)o);
		pair(e, I_STP, R_X16, R_X17, d, (int64_t)o);
	}
	if (o < n) {
		int t = R_X16;

		for (uint64_t k; o < n; o += k) {
			k = n - o >= 8 ? 8 : n - o >= 4 ? 4 : n - o >= 2 ? 2 : 1;
			mem(e, I_LD, log2sz(k), 0, t, sr, (int64_t)o);
			mem(e, I_ST, log2sz(k), 0, t, d, (int64_t)o);
		}
	}
}

static bool mem_inline(ecx *e, int kind, carg *args, val *res)
{
	uint64_t n;
	int d;

	if (kind < CC_LIB_MEMCPY || kind > CC_LIB_MEMSET || args[2].v.k != V_CONST)
		return false;
	n = (uint64_t)args[2].v.i;
	if (n > 128 || (kind == 2 && n & (n - 1)) || (kind == 2 && n > 8) || (kind == 3 && args[1].v.k != V_CONST))
		return false;	/* memmove: one load before one store (overlap) */
	/* parked while many registers were live: their v is a freed register */
	unpark(e, &args[0]);
	if (kind < 3)
		unpark(e, &args[1]);
	d = toreg(e, &args[0].v);
	if (kind < 3) {
		int sr = toreg(e, &args[1].v);

		if (kind == CC_LIB_MEMCPY && n >= 16 && e->E->o->opt >= 1)
			copy_pairs(e, d, sr, n);
		else
			copy_mem(e, d, sr, n);
		val_free(e, &args[1].v);
	} else if (!(args[1].v.i & 0xff)) {
		zero_mem(e, d, n);
	} else {
		uint64_t pat = (uint64_t)(args[1].v.i & 0xff) * 0x0101010101010101ull, o = 0;
		int t = R_X16;	/* as copy_mem: not x17 */

		movi(e, 1, t, (int64_t)pat);
		while (o < n) {
			uint64_t k = n - o >= 8 ? 8 : n - o >= 4 ? 4 : n - o >= 2 ? 2 : 1;

			mem(e, I_ST, log2sz(k), 0, t, d, (int64_t)o);
			o += k;
		}
	}
	*res = args[0].v;
	return true;
}

static int64_t chk_plain(const carg *args, int na, int64_t bi)
{
	const val *o = &args[na - 1].v, *l = &args[na >= 2 ? na - 2 : 0].v;

	return cc_chk_simplify(bi, na, o->k == V_CONST, (uint64_t)o->i, l->k == V_CONST, (uint64_t)l->i);
}

/* Keep the argument scratch buffer out of gen_inner's recursive frame.
 * Inlining it there charges every arithmetic node for MAXARGS call slots. */
__attribute__((noinline)) VPRIV val gen_call(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	uint32_t ref = 0, s = direct_callee(c, x->a, &ref);
	carg argbuf[MAXARGS], *args = argbuf;
	int na = (int)x->c;
	val fnv = vnone(), r;
	bool noret = false, inl;

	/* more than MAXARGS (psql's sql_help.c passes ~260): on the heap */
	if (na > MAXARGS && !(args = malloc((size_t)na * sizeof(*args))))
		efatal(e, x->loc, "out of memory");
	if (s) {
		const sym *y = &c->syms[s];

		noret = (y->flags & SF_NORETURN) != 0;
		/* use the definition's flags (always_inline may be on a later declaration) */
	}
	if (!s && ref && na == 2) {
		val f = fold_strcmp(e, x);

		if (f.k == V_CONST)
			return f;
	}
	if (!s && !ref)
		fnv = gen(e, x->a);
	inl = s && can_inline(e, s);
	if (inl)
		walk_fn(e, s);
	for (int i = 0; i < na; i++) {
		uint32_t an = c->lists[x->b + i];

		/* an argument the inlined body never reads: only its side effects count */
		if (inl && i < (int)N(c->syms[s].body)->aux &&
		    !(e->E->sflags[c->lists[N(c->syms[s].body)->a + i]] & SA_USED) && !side_effects(c, an) &&
		    !is_agg(c, c->syms[c->lists[N(c->syms[s].body)->a + i]].ty)) {
			args[i].v = vconst(0);
			args[i].t = N(an)->ty;
			args[i].slot = 1;
			continue;
		}
		args[i].v = gen(e, an);
		args[i].t = N(an)->ty;
		args[i].slot = 1;
		/* many arguments: keep the registers for evaluating the rest */
		if (__builtin_popcount(e->live & ~e->resv) > (NTEMP - __builtin_popcount(e->resv)) / 2 &&
		    (args[i].v.k == V_REG || args[i].v.k == V_REG2))
			park(e, &args[i]);
	}
	if (!s && RKIND(ref) == RK_NAME && RIDX(ref) < 0x8000000u && na >= 3 && cc_builtin_libkind(RIDX(ref)) == CC_LIB_CHK) {
		int64_t p = chk_plain(args, na, RIDX(ref));

		if (p >= 0) {
			ref = REF(RK_NAME, (uint32_t)p);
			na--;
		}
	}
	if (!s && RKIND(ref) == RK_NAME && RIDX(ref) < 0x8000000u && na == 1 &&
	    is_ld(c, x->ty) && cc_builtin_libkind(RIDX(ref)) == CC_LIB_FABSL) {
		/* Clear binary128's sign without touching a signaling NaN payload. */
		unpark(e, &args[0]);
		r = args[0].v;
		if (r.k == V_CONST)
			r.i = (i128)((u128)r.i & ~((u128)1 << 127));
		else {
			toreg2(e, &r, false);
			opi(e, I_ANDI, 1, r.r2, r.r2, INT64_MAX);
		}
	} else if (inl) {
		r = inline_call(e, s, args, na, n);
	} else if (!s && ref && na == 3 && RKIND(ref) == RK_NAME && RIDX(ref) < 0x8000000u &&
		   mem_inline(e, cc_builtin_libkind(RIDX(ref)), args, &r)) {
		/* inlined */
	} else if (s && em_plain_mem_kind(e, s, na) && args[2].v.k == V_CONST && (uint64_t)args[2].v.i <= 64 &&
		   mem_inline(e, em_plain_mem_kind(e, s, na), args, &r)) {
		/* inlined */
	} else {
		qty ft = N(x->a)->ty;
		int nfix = -1;

		if (is_ptr(c, ft))
			ft = T(c, ft)->base;
		if (is_func(c, ft) && (T(c, ft)->flags & TF_VARIADIC))
			nfix = (int)T(c, ft)->n;
		r = s || ref ? do_call(e, ref, NULL, args, na, x->ty, noret, nfix)
			     : do_call(e, 0, &fnv, args, na, x->ty, false, nfix);
	}
	if (args != argbuf)
		free(args);
	return r;
}
