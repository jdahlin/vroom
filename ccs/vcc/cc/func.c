/* vcc front end: function definitions, -pg/-finstrument instrumentation, the translation unit */
#include "ccimpl.h"

/* a call of void name(void *this_fn, void *call_site) (declared if need be) */
static uint32_t profile_call(cc *c, const char *name, uint32_t fn, uint32_t loc)
{
	uint32_t id = vintern_find(name, strlen(name), vintern_hash(name, strlen(name))), f, v, n;
	qty vp = ty_ptr(c, prim(TY_VOID));
	vec args;

	f = id ? file_scope_sym(c, id) : 0;
	if (!f || c->syms[f].kind != S_FUNC) {
		qty pt[2] = { vp, vp };

		if (!id)
			id = vintern(name, strlen(name), vintern_hash(name, strlen(name)));
		f = sym_new(c, S_FUNC, id, ty_func(c, prim(TY_VOID), pt, 2, 0), loc);
		c->syms[f].sc = SC_EXTERN;
		c->syms[f].flags |= SF_GLOBAL;
	}
	c->syms[f].flags |= SF_USED;
	vinit(&args);
	v = mk(c, N_VAR, c->syms[fn].ty, loc);
	N(v)->a = fn;
	c->syms[fn].flags |= SF_USED | SF_ADDR_TAKEN;
	vpush(c, &args, cast_node(c, rvalue(c, v), vp, CK_P2P));
	{
		vec ra;

		vinit(&ra);
		vpush(c, &ra, mk_int(c, 0, prim(TY_UINT), loc));
		vpush(c, &args, mk_builtin(c, BI_RETURN_ADDRESS, vp, loc, &ra));
	}
	v = mk(c, N_VAR, c->syms[f].ty, loc);
	N(v)->a = f;
	n = mk(c, N_CALL, prim(TY_VOID), loc);
	N(n)->a = rvalue(c, v);
	N(n)->b = vfreeze(c, &args);
	N(n)->c = args.n;
	return n;
}

/* -finstrument-functions: every return of fn (in statements st) calls the exit hook first */
static void instrument_returns(cc *c, uint32_t fn, uint32_t st)
{
	node *x;

	if (!st)
		return;
	x = N(st);
	switch (x->op) {
	case N_BLOCK:
		for (uint32_t i = 0; i < x->c; i++)
			instrument_returns(c, fn, c->lists[x->b + i]);
		break;
	case N_IF:
		instrument_returns(c, fn, x->b);
		instrument_returns(c, fn, x->c);
		break;
	case N_WHILE: case N_DO: case N_SWITCH:
		instrument_returns(c, fn, x->b);
		break;
	case N_FOR:
		instrument_returns(c, fn, c->lists[x->c + 1]);
		break;
	case N_CASE: case N_DEFAULT: case N_LABEL:
		instrument_returns(c, fn, x->c);
		break;
	case N_RETURN: {
		uint32_t ex = profile_call(c, "__cyg_profile_func_exit", fn, x->loc), e = x->a;

		if (!e || is_void(c, N(e)->ty)) {
			N(st)->a = e ? mk2(c, N_COMMA, prim(TY_VOID), x->loc, e, ex) : ex;
		} else {
			uint32_t pre, t;

			/* return (tmp = e, exit(), tmp) */
			t = cplx_reuse(c, e, &pre);
			if (!pre) {
				uint32_t s = sym_new(c, S_VAR, 0, QUNQ(N(e)->ty), x->loc);

				c->syms[s].sc = SC_AUTO;
				t = mk(c, N_VAR, QUNQ(N(e)->ty), x->loc);
				N(t)->a = s;
				N(t)->flags = NF_LVALUE;
				pre = mk2(c, N_ASSIGN, QUNQ(N(e)->ty), x->loc, t, e);
			}
			N(st)->a = mk2(c, N_COMMA, N(t)->ty, x->loc, pre, mk2(c, N_COMMA, N(t)->ty, x->loc, ex, rvalue(c, t)));
		}
		break;
	}
	default:
		break;
	}
}

static uint32_t instrument(cc *c, uint32_t fn, uint32_t body)
{
	vec items;
	uint32_t loc = N(body)->loc;

	instrument_returns(c, fn, body);
	vinit(&items);
	vpush(c, &items, mkstmt(c, N_EXPR, loc, profile_call(c, "__cyg_profile_func_enter", fn, loc), 0, 0));
	for (uint32_t i = 0; i < N(body)->c; i++)
		vpush(c, &items, c->lists[N(body)->b + i]);
	vpush(c, &items, mkstmt(c, N_EXPR, loc, profile_call(c, "__cyg_profile_func_exit", fn, loc), 0, 0));
	return mkstmt(c, N_BLOCK, loc, 0, vfreeze(c, &items), items.n);
}

VPRIV void function_def(cc *c, uint32_t s, const declr *d)
{
	uint32_t body, first_label = c->nlabel;
	uint32_t saved_fn = c->fn;

	if (c->syms[s].flags & SF_DEFINED && !(c->syms[s].flags & SF_GNU_INLINE)) {
		cc_error(c, d->loc, "redefinition of '%.*s'", IDF(d->name));
		note(c, c->syms[s].loc, "previous definition is here");
	}
	c->syms[s].flags |= SF_DEFINED;
	c->syms[s].defloc = d->loc;
	c->fn = s;
	scope_open(c);
	for (uint32_t i = 0; i < d->npsyms; i++) {
		uint32_t p = c->lists[d->psyms + i];

		if (!c->syms[p].name) {	/* C23: unnamed parameters in definitions */
			if (kwmap != kwmap_c23 && KIND(c, c->syms[p].ty) != TY_VOID)
				cc_warnx(c, "c23-extensions", true, NULL, c->syms[p].loc, NULL, 0, NULL, 0,
					 "omitting the parameter name in a function definition is a C23 extension");
			continue;
		}
		sym_bind(c, p);
	}
	c->in_loop = c->in_switch = 0;
	c->nrtd = 0;
	c->uv_any = 0, c->nuv = 0;
	body = compound_stmt(c, NULL);
	scope_close(c);
	rt_flush(c);	/* (clang: only if reachable; vcc: not under a constant-false condition) */
	{
		uint32_t rb = c->prevloc;	/* the closing brace */
		const sym *y = &c->syms[s];
		bool nr = y->flags & SF_NORETURN;
		qty rt = T(c, y->ty)->base;
		bool main_ = idlen_(y->name) == 4 && !memcmp(idstr_(y->name), "main", 4);

		if (nr || (!is_void(c, rt) && !main_)) {
			int k = body_falls_off(c, body, first_label);

			if (k && nr)
				cc_warnx(c, "invalid-noreturn", true, NULL, rb, NULL, 0, NULL, 0, "function declared 'noreturn' should not return");
			else if (k)
				cc_warnx(c, "return-type", true, NULL, rb, NULL, 0, NULL, 0, "non-void function does not return a value%s",
					 k == 1 ? " in all control paths" : "");
		}
	}
	if (c->uv_any & 6)
		uninit_check(c, body, first_label);
	{
		/* side effects of decayed VLA parameter sizes run first, in order */
		vec items;

		vinit(&items);
		for (uint32_t i = 0; i < d->npsyms; i++) {
			uint32_t p = c->lists[d->psyms + i];

			if (c->syms[p].body)
				vpush(c, &items, mkstmt(c, N_EXPR, c->syms[p].loc, c->syms[p].body, 0, 0));
		}
		if (items.n) {
			for (uint32_t i = 0; i < N(body)->c; i++)
				vpush(c, &items, c->lists[N(body)->b + i]);
			body = mkstmt(c, N_BLOCK, N(body)->loc, 0, vfreeze(c, &items), items.n);
		}
	}
	for (uint32_t l = first_label; l < c->nlabel; l++) {
		label *lb = &c->labels[l];

		if (!lb->defined && !lb->local)
			cc_error(c, lb->loc, "use of undeclared label '%.*s'", IDF(lb->name));
		if (!lb->local && dget(c->ldir, lb->name) == l)
			*dslot(c, c->ldir, lb->name) = 0;
	}
	if (c->o->instrument && !(c->syms[s].flags & (SF_NOINSTR | SF_ALWAYS_INLINE)))
		body = instrument(c, s, body);
	c->syms[s].body = body;
	/* for the emitter: the parameter syms, on the body block */
	N(body)->a = d->psyms;
	N(body)->aux = (uint16_t)d->npsyms;
	c->fn = saved_fn;
}

/* ---------------- translation unit ---------------- */

VPRIV void translation_unit(cc *c)
{
	for (;;) {
		dc_boundary(c);
		if (is(c, T_EOF))
			break;
		if (accept(c, T_SEMI))
			continue;
		if (iskw(c, KW___extension__)) {
			next(c);
			continue;
		}
		if (at_asm(c)) {
			c->tlasm[c->ntlasm++] = asm_stmt(c, true);
			continue;
		}
		declaration(c, NULL, true);
		c->ndops = 0;
	}
}
