/* vcc code generator: calls and inline expansion */
#include "emitimpl.h"

#define MAXARGS 128

/* park an argument value: registers go to a slot, aggregates are copied */
static void park(ecx *e, carg *a)
{
	cc *c = e->c;

	if (a->slot != 1)
		return;		/* parked while the arguments were evaluated */
	if (is_agg(c, a->t)) {
		uint64_t sz = (tsize(c, a->t) + 7) & ~(uint64_t)7;
		int32_t sl = slot(e, sz ? sz : 8, talign(c, a->t) > 8 ? 16 : 8);
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

/* put 8-byte word k of argument a into register rd */
static void arg_word(ecx *e, const carg *a, int k, int rd)
{
	if (a->slot != 1) {
		mem(e, I_LD, 3, 0, rd, FRAME, a->slot + 8 * k);
		return;
	}
	if (a->v.k == V_CONST) {
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
	int32_t fslot = 1, rslot = 1;
	bool sret = is_sret(c, rty);
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
		park(e, &args[i]);
		if (nfix >= 0 && i >= nfix && DARWIN(c))
			classify_va(e, args[i].t, &nsaa, &args[i].a);
		else
			classify(e, args[i].t, &ngrn, &nsaa, &args[i].a);
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
		if (!e->dry) {
			need_fold(e);
			e->E->nerrcall++;
			if (e->E->stats)
				ediag(e, e->curloc, "error() call '%.*s' not folded: dropped", (int)vintern_ents[c->syms[RIDX(ref)].name].len,
				      vintern_ents[c->syms[RIDX(ref)].name].s);
		}
		return is_void(c, rty) ? vnone() : vconst(0);
	}
	spill_live(e, &sp);
	for (int i = 0; i < n; i++) {
		carg *a = &args[i];

		switch (a->a.kind) {
		case A_REG:
			for (int k = 0; k < a->a.nreg; k++)
				arg_word(e, a, k, a->a.reg + k);
			break;
		case A_STACK:
			if (DARWIN(c) && a->a.size < 8 && !is_agg(c, a->t)) {	/* Apple: packed */
				arg_word(e, a, 0, R_X16);
				mem(e, I_ST, log2sz(a->a.size), 0, R_X16, SP, a->a.stk);
				break;
			}
			for (int k = 0; k < a->a.nreg; k++) {
				arg_word(e, a, k, R_X16);
				mem(e, I_ST, 3, 0, R_X16, SP, a->a.stk + 8 * k);
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
	} else if (is_recabi(c, rty) && hfa(c, rty, &hn)) {
		int hb = hfa(c, rty, &hn);
		int32_t sl = slot(e, 64, 16);
		int r = treg(e);

		for (int k = 0; k < hn; k++)
			vr_store(e, hb, k, FRAME, sl + hb * k);
		opi(e, I_ADDI, 1, r, FRAME, sl);
		res = vreg(r);
	} else if (is_recabi(c, rty)) {
		int32_t sl = slot(e, 16, 16);
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

VPRIV void walk_fn(ecx *e, uint32_t s)
{
	rlog(e, RL_WALK, s);
	ensure_walked(e->E, e->c, s);
}

/* a local only ever assigned constants: follow its value (v: the initializer, or none) */
VPRIV void kv_track(ecx *e, uint32_t s, const val *v)
{
	cc *c = e->c;
	const sym *y = &c->syms[s];
	bind *b = binding(e, s);
	int k;

	if ((e->E->sflags[s] & (SA_WRITTEN | SA_CWRITE)) != SA_CWRITE || !is_integer(c, y->ty) || is_wide(c, y->ty) ||
	    (QQ(y->ty) & Q_VOLATILE) || sym_attrs(c, s)->cleanup || e->nkv == NKV)
		return;
	k = e->nkv++;
	b->kv = (uint8_t)(k + 1);
	e->kvalid &= (uint16_t)~(1u << k);
	if (v && v->k == V_CONST) {
		e->kval[k] = (int64_t)em_trunc_to(c, v->i, y->ty);
		e->kvalid |= (uint16_t)(1u << k);
	}
}

/* the kv slot of a[CONST] (create: allocate one), or -1 */
VPRIV int ael_slot(ecx *e, uint32_t n, bool create)
{
	cc *c = e->c;
	uint32_t s, idx;
	const sym *y;
	const ctype *at;
	qty et;

	if (!is_aconst(c, n))
		return -1;
	s = N(N(N(n)->a)->a)->a, y = &c->syms[s];
	if (!is_local_var(c, s) || y->sc == SC_STATIC || (e->E->sflags[s] & SA_AESC) || sym_attrs(c, s)->cleanup)
		return -1;
	at = T(c, y->ty), et = at->base;
	if (at->kind != TY_ARRAY || (at->flags & (TF_VLA | TF_INCOMPLETE)) || !is_integer(c, et) || is_wide(c, et) ||
	    (QQ(et) & Q_VOLATILE) || (QQ(y->ty) & Q_VOLATILE))
		return -1;
	idx = aconst_idx(c, n);
	if (idx >= at->n)
		return -1;
	for (int i = 0; i < e->nael; i++)
		if (e->ael[i].s == s && e->ael[i].idx == idx)
			return e->ael[i].kv;
	if (!create || e->nkv == NKV || e->nael == NKV)
		return -1;
	e->ael[e->nael].s = s, e->ael[e->nael].idx = idx, e->ael[e->nael].kv = e->nkv;
	e->nael++;
	e->kvalid &= (uint16_t)~(1u << e->nkv);
	return e->nkv++;
}

/* may variable s (param or local) be bound to the constant v for good? */
VPRIV bool const_var(ecx *e, uint32_t s, const val *v)
{
	uint8_t f = e->E->sflags[s];

	if (f & (SA_WRITTEN | SA_CMULTI))
		return false;
	if (f & SA_CWRITE)
		return v->k == V_CONST && em_trunc_to(e->c, v->i, e->c->syms[s].ty) == e->E->cwval[s];
	return v->k == V_CONST || v->k == V_ADDR;
}

#define INLINE_NODES 40	/* AST nodes: a few statements */

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
			if (!(y->flags & SF_INLINE))
				return false;
			walk_fn(e, s);
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

VPRIV val inline_call(ecx *e, uint32_t s, carg *args, int n)
{
	cc *c = e->c;
	const sym *y = &c->syms[s];
	const node *body = N(y->body);
	fnctx F;
	qty rty = T(c, y->ty)->base;
	val res;

	walk_fn(e, s);
	memset(&F, 0, sizeof(F));
	F.fn = s;
	F.inst = ++e->inst_next;
	F.inl = true;
	F.rty = rty;
	F.retlbl = newlabel(e);
	F.cst_ok = true;
	F.cbase = e->nclean;
	F.nloop = e->nloop;
	F.up = e->f;
	if (!is_void(c, rty))
		F.retslot = slot(e, tsize(c, rty) ? tsize(c, rty) : 8, talign(c, rty) > 16 ? 16 : talign(c, rty));
	for (int i = 0; i < n; i++)
		if (args[i].slot != 1) {	/* parked: back in a register */
			if (is_agg(c, args[i].t)) {
				int r = treg(e);

				opi(e, I_ADDI, 1, r, FRAME, args[i].slot);
				args[i].v = vreg(r);
			} else {
				lval l = { .k = L_FRAME, .off = args[i].slot, .align = 8 };

				args[i].v = load(e, &l, args[i].t);
			}
			args[i].slot = 1;
		}
	for (int i = 0; i < (int)body->aux; i++) {
		uint32_t p = c->lists[body->a + i];
		const sym *ps = &c->syms[p];
		bind *b = binding(e, p);
		val v = i < n ? args[i].v : vconst(0);

		if (const_var(e, p, &v) && !is_agg(c, ps->ty) &&
		    !(QQ(ps->ty) & Q_VOLATILE) && !is_wide(c, ps->ty)) {
			b->inst = F.inst;
			b->kind = B_CONST;
			b->kv = 0;
			b->v = v;
			continue;
		}
		{
			int32_t sl;
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
	e->f = &F;
	e->inline_depth++;
	e->E->ninl++;
	gen_stmt(e, y->body);
	e->inline_depth--;
	e->f = F.up;
	deflabel(e, F.retlbl);
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

/* the callee sym of a direct call, or 0 */
VPRIV uint32_t direct_callee(cc *c, uint32_t fn, uint32_t *ref)
{
	const node *f = N(fn);

	while (f->op == N_CAST && (f->aux == CK_FDECAY || f->aux == CK_NOP || f->aux == CK_P2P) &&
	       (N(f->a)->op == N_VAR || N(f->a)->op == N_CAST))
		f = N(f->a);
	if (f->op != N_VAR)
		return 0;
	if (c->syms[f->a].kind == S_BUILTIN) {
		*ref = REF(RK_NAME, (uint32_t)c->syms[f->a].val);
		return 0;
	}
	if (c->syms[f->a].kind != S_FUNC)
		return 0;
	/* calls through a cast to another function type stay direct only if the types agree */
	if (N(fn)->op == N_CAST && N(N(fn)->a)->op == N_CAST)
		return 0;
	*ref = REF(RK_SYM, canon_sym(c, f->a));
	return canon_sym(c, f->a);
}

/* __builtin_strcmp of two string literals (the kernel picks operators by name) */
static val fold_strcmp(ecx *e, const node *x)
{
	cc *c = e->c;
	uint32_t f = strip_casts(c, x->a), a = strip_casts(c, c->lists[x->b]), b = strip_casts(c, c->lists[x->b + 1]);
	int r;

	if (N(f)->op != N_VAR || c->syms[N(f)->a].kind != S_BUILTIN || N(a)->op != N_STR || N(b)->op != N_STR ||
	    strcmp(cc_builtin_name(c->syms[N(f)->a].val), "__builtin_strcmp"))
		return vnone();
	r = strcmp(c->strs[N(a)->a].s, c->strs[N(b)->a].s);
	return vconst(r < 0 ? -1 : r > 0);
}

VPRIV val gen_call(ecx *e, uint32_t n)
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
		if (__builtin_popcount(e->live) > NTEMP / 2 && (args[i].v.k == V_REG || args[i].v.k == V_REG2))
			park(e, &args[i]);
	}
	if (inl) {
		r = inline_call(e, s, args, na);
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
