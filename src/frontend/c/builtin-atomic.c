/* vcc front end: __atomic_*, __c11_atomic_* and __sync_* calls; see builtin-table.c for where a builtin's other facts live */
#include "ccimpl.h"

/*
 * GNU __atomic_* calls.  Argument layout (P: pointer to the atomic object
 * of type T, size 1/2/4/8/16; M: memory order, converted to int):
 *   load_n(P, M) -> T            store_n(P, T, M)          exchange_n(P, T, M) -> T
 *   compare_exchange_n(P, T *expected, T desired, bool weak, M, M) -> bool
 *   load(P, T *ret, M)           store(P, T *val, M)       exchange(P, T *val, T *ret, M)
 *   compare_exchange(P, T *expected, T *desired, bool weak, M, M) -> bool
 *   fetch_op / op_fetch(P, T, M) -> T (integers; pointers: bytes, as GCC)
 */
VPRIV uint32_t atomic_call(cc *c, const builtin *b, uint32_t loc)
{
	static const uint8_t nargs[] = {
		[BI_ATOMIC_LOAD_N - BI_ATOMIC_LOAD_N] = 2, [BI_ATOMIC_STORE_N - BI_ATOMIC_LOAD_N] = 3,
		[BI_ATOMIC_EXCHANGE_N - BI_ATOMIC_LOAD_N] = 3, [BI_ATOMIC_CMPXCHG_N - BI_ATOMIC_LOAD_N] = 6,
		[BI_ATOMIC_LOAD - BI_ATOMIC_LOAD_N] = 3, [BI_ATOMIC_STORE - BI_ATOMIC_LOAD_N] = 3,
		[BI_ATOMIC_EXCHANGE - BI_ATOMIC_LOAD_N] = 4, [BI_ATOMIC_CMPXCHG - BI_ATOMIC_LOAD_N] = 6,
	};
	int id = b->id, want = id <= BI_ATOMIC_CMPXCHG ? nargs[id - BI_ATOMIC_LOAD_N] :
			       id == BI_SYNC_VAL_CAS || id == BI_SYNC_BOOL_CAS ? 4 : id == BI_SYNC_SYNCHRONIZE ? 1 :
			       id == BI_ATOMIC_TEST_AND_SET || id == BI_ATOMIC_CLEAR ? 2 : 3;
	vec args;
	qty pt, t, rt = prim(TY_VOID), tp;

	args_list(c, &args);
	for (uint32_t i = 0; i < args.n; i++)
		args.v[i] = decay(c, args.v[i]);
	if (id == BI_SYNC_SYNCHRONIZE) {	/* a full barrier: __atomic_thread_fence(seq_cst) */
		if (args.n)
			arity(c, loc, 0, &args);
		args.n = 0;
		vpush(c, &args, mk_int(c, 5, prim(TY_INT), loc));
		return mk_builtin(c, BI_ATOMIC_THREAD_FENCE, prim(TY_VOID), loc, &args);
	}
	if (id == BI_SYNC_LOCK_RELEASE && args.n == 1) {	/* __atomic_store_n(p, 0, release) */
		vpush(c, &args, mk_int(c, 0, prim(TY_INT), loc));
		vpush(c, &args, mk_int(c, 3, prim(TY_INT), loc));
		id = BI_ATOMIC_STORE_N;
	}
	if (b->sig[0] == 's' && (int)args.n == want - 1)	/* __sync_*: sequentially consistent */
		vpush(c, &args, mk_int(c, 5, prim(TY_INT), loc));
	if (b->sig[1] == 'c' && args.n == 5) {	/* __c11_atomic_compare_exchange_*: weak goes before the orders */
		vpush(c, &args, args.v[4]);
		args.v[4] = args.v[3];
		args.v[3] = mk_int(c, 0, prim(TY_INT), loc);
	}
	if (b->sig[0] == 's' && (id == BI_ATOMIC_FETCH_NAND || id == BI_ATOMIC_NAND_FETCH))
	{
		diag_span r = { loc, pp_token_end_find(c->p, loc) };	/* the builtin's name */

		cc_warnx(c, "sync-fetch-and-nand-semantics-changed", true, NULL, c->args_rp, &r, 1, NULL, 0,
			 "the semantics of this intrinsic changed with GCC version 4.4 - the newer semantics are provided here");
	}
	if ((int)args.n != want) {
		arity(c, loc, (unsigned)want, &args);
		return mk_int(c, 0, prim(TY_INT), loc);
	}
	pt = N(args.v[0])->ty;
	if (!is_ptr(c, pt)) {
		diag_span r = rng(c, args.v[0]);

		report(c, "error", loc, &r, 1, NULL, 0, "address argument to atomic builtin must be a pointer ('%s' invalid)", tyname(c, pt));
		return mk_int(c, 0, prim(TY_INT), loc);
	}
	if (b->sig[1] == 'i' || b->sig[1] == 'u') {	/* __sync_fetch_and_{,u}{max,min}: volatile int * / unsigned * */
		qty it = prim(b->sig[1] == 'i' ? TY_INT : TY_UINT);

		args.v[0] = convert(c, args.v[0], ty_ptr(c, (it | Q_VOLATILE)));
		pt = N(args.v[0])->ty;
	}
	if (id == BI_ATOMIC_TEST_AND_SET || id == BI_ATOMIC_CLEAR) {
		/* (p, order) on the byte at p: __atomic_exchange_n(p, 1, o) != 0 / __atomic_store_n(p, 0, o) */
		qty bt = prim(TY_UCHAR);
		uint32_t r;
		vec a;

		vinit(&a);
		vpush(c, &a, cast_node(c, args.v[0], ty_ptr(c, (bt | (QQ(T(c, pt)->base) & Q_VOLATILE))), CK_NOP));
		vpush(c, &a, mk_int(c, id == BI_ATOMIC_CLEAR ? 0 : 1, bt, loc));
		vpush(c, &a, convert(c, args.v[1], prim(TY_INT)));
		if (id == BI_ATOMIC_CLEAR)
			return mk_builtin(c, BI_ATOMIC_STORE_N, prim(TY_VOID), loc, &a);
		r = mk_builtin(c, BI_ATOMIC_EXCHANGE_N, bt, loc, &a);
		return convert(c, r, prim(TY_BOOL));
	}
	t = QUNQ(T(c, pt)->base);
	tp = ty_ptr(c, t);
	if (!(T(c, t)->size == 1 || T(c, t)->size == 2 || T(c, t)->size == 4 || T(c, t)->size == 8 || T(c, t)->size == 16) && !(((id >= BI_ATOMIC_LOAD && id <= BI_ATOMIC_CMPXCHG) || !strncmp(b->name, "__c11_atomic_", 13)) && ty_size(c, t))) {
		cc_error(c, loc, "address argument to atomic builtin must be a pointer to 1,2,4,8 or 16 byte type ('%s' invalid)", tyname(c, pt));
		return mk_int(c, 0, prim(TY_INT), loc);
	}
	switch (id) {
	case BI_ATOMIC_LOAD_N:
		rt = t;
		break;
	case BI_ATOMIC_STORE_N:
		args.v[1] = convert(c, args.v[1], t);
		break;
	case BI_ATOMIC_EXCHANGE_N:
		args.v[1] = convert(c, args.v[1], t);
		rt = t;
		break;
	case BI_ATOMIC_CMPXCHG_N:
		args.v[1] = convert(c, args.v[1], tp);
		args.v[2] = convert(c, args.v[2], t);
		args.v[3] = convert(c, args.v[3], prim(TY_BOOL));
		rt = prim(TY_BOOL);
		break;
	case BI_ATOMIC_LOAD: case BI_ATOMIC_STORE:
		args.v[1] = convert(c, args.v[1], tp);
		break;
	case BI_ATOMIC_EXCHANGE:
		args.v[1] = convert(c, args.v[1], tp);
		args.v[2] = convert(c, args.v[2], tp);
		break;
	case BI_ATOMIC_CMPXCHG:
		args.v[1] = convert(c, args.v[1], tp);
		args.v[2] = convert(c, args.v[2], tp);
		args.v[3] = convert(c, args.v[3], prim(TY_BOOL));
		rt = prim(TY_BOOL);
		break;
	case BI_SYNC_VAL_CAS: case BI_SYNC_BOOL_CAS:	/* (p, old, new) -> previous *p, or whether it was old */
		if (!is_integer(c, t) && !is_ptr(c, t))
			cc_error(c, loc, "address argument to atomic operation must be a pointer to integer or pointer ('%s' invalid)", tyname(c, pt));
		args.v[1] = convert(c, args.v[1], t);
		args.v[2] = convert(c, args.v[2], t);
		rt = id == BI_SYNC_VAL_CAS ? t : prim(TY_BOOL);
		break;
	case BI_ATOMIC_FETCH_MAX: case BI_ATOMIC_FETCH_MIN: case BI_ATOMIC_MAX_FETCH: case BI_ATOMIC_MIN_FETCH:
		if (!is_integer(c, t) || KIND(c, t) == TY_BOOL)	/* floating max/min: not supported yet */
			cc_error(c, loc, "address argument to atomic operation must be a pointer to integer");
		args.v[1] = convert(c, args.v[1], t);
		rt = t;
		break;
	case BI_ATOMIC_FETCH_NAND: case BI_ATOMIC_NAND_FETCH:
		if (!is_integer(c, t))
			cc_error(c, loc, "address argument to atomic operation must be a pointer to integer");
		args.v[1] = convert(c, args.v[1], t);
		rt = t;
		break;
	default:	/* fetch_op / op_fetch */
		if (!is_integer(c, t) && !is_ptr(c, t))
			cc_error(c, loc, "address argument to atomic operation must be a pointer to integer or pointer ('%s' invalid)", tyname(c, pt));
		args.v[1] = convert(c, args.v[1], is_ptr(c, t) ? c->t_ptrdiff : t);
		rt = t;
		break;
	}
	for (int i = want - (id == BI_ATOMIC_CMPXCHG_N || id == BI_ATOMIC_CMPXCHG ? 2 : 1); i < want; i++)
		args.v[i] = convert(c, args.v[i], prim(TY_INT));
	if (((id >= BI_ATOMIC_LOAD_N && id <= BI_ATOMIC_MIN_FETCH) || id == BI_SYNC_VAL_CAS || id == BI_SYNC_BOOL_CAS) && atomic_wide(c, t))
		return atomic_builtin_expr(c, id, t, &args, loc);
	return mk_builtin(c, id, rt, loc, &args);
}

/* The always query is a constant expression; its pointer is unevaluated.
 * Only widths emitted as native atomic operations are guaranteed lock-free.
 * In particular, Linux 16-byte accesses currently use libatomic. */
VPRIV uint32_t atomic_lock_free_call(cc *c, const builtin *b, uint32_t loc)
{
	vec args;
	args_list(c, &args);
	unsigned want = !strcmp(b->name, "__c11_atomic_is_lock_free") ? 1 : 2;
	if (args.n != want) { arity(c, loc, want, &args); return err_node(c, loc); }
	bool always = !strcmp(b->name, "__atomic_always_lock_free");
	uint32_t size = convert(c, decay(c, args.v[0]), c->t_size);
	uint32_t pointer = want == 2 ? decay(c, args.v[1]) : mk_int(c, 0, c->t_size, loc);
	uint64_t alignment = is_ptr(c, N(pointer)->ty) ? ty_align(c, T(c, N(pointer)->ty)->base) : 1;
	cval cv, pv;
	bool constant = eval(c, size, &cv) && cv.kind == CV_INT && cv.ice &&
	                !elementwise_abs_effects(c, size, true);
	if (eval(c, pointer, &pv) && (pv.kind == CV_INT || pv.kind == CV_ADDR)) {
		if (pv.kind == CV_INT || (!pv.sym && !pv.str && !pv.label && !pv.sublabel)) {
			alignment = pv.i ? (uint64_t)pv.i & (0 - (uint64_t)pv.i) : UINT64_MAX;
		} else if (pv.sym) {
			alignment = ty_align(c, c->syms[pv.sym].ty);
			if (c->syms[pv.sym].align > alignment) alignment = c->syms[pv.sym].align;
			if (pv.i) {
				uint64_t offset_align = (uint64_t)pv.i & (0 - (uint64_t)pv.i);
				if (offset_align < alignment) alignment = offset_align;
			}
		}
	}
	uint32_t address = pointer;
	while (N(address)->op == N_CAST) address = N(address)->a;
	if (N(address)->op == N_ADDR && N(N(address)->a)->op == N_MEMBER) {
		const node *member = N(N(address)->a);
		uint64_t member_align = c->tags[member->b].f[member->c].align;
		if (member_align < alignment) alignment = member_align;
	}
	pointer = convert(c, pointer, ty_ptr(c, prim(TY_VOID) | Q_CONST | Q_VOLATILE));
	bool native = constant && (cv.i == 1 || cv.i == 2 || cv.i == 4 || cv.i == 8) && alignment >= (uint64_t)cv.i;
	if (always) {
		if (!constant) cc_error(c, loc, "argument to '%s' must be a constant integer", b->name);
		return mk_int(c, native, prim(TY_BOOL), loc);
	}
	/* Like GCC/Clang, a query resolved from size/alignment does not
	 * evaluate the optional pointer. The runtime path evaluates it once. */
	if (native)
		return mk_int(c, 1, prim(TY_BOOL), loc);
	args.n = 0;
	vpush(c, &args, size); vpush(c, &args, pointer);
	if (c->o->target == PP_LINUX_AARCH64 || CC_X86_64(c))
		return atomic_lock_free_runtime(c, &args, loc);
	return mk_builtin(c, BI_ATOMIC_LOCK_FREE, prim(TY_BOOL), loc, &args);
}
