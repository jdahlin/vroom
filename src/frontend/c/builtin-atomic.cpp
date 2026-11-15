/* vcc front end: __atomic_*, __c11_atomic_* and __sync_* calls; see builtin-table.c for where a builtin's other facts live */
#include "cc-internal.h"

/*
 * GNU __atomic_* calls.  Argument layout (P: pointer to the atomic object
 * of type AST_TYPE, size 1/2/4/8/16; M: memory order, converted to int):
 *   load_n(P, M) -> AST_TYPE            store_n(P, AST_TYPE, M)          exchange_n(P, AST_TYPE, M) -> AST_TYPE
 *   compare_exchange_n(P, AST_TYPE *expected, AST_TYPE desired, bool weak, M, M) -> bool
 *   load(P, AST_TYPE *ret, M)           store(P, AST_TYPE *val, M)       exchange(P, AST_TYPE *val, AST_TYPE *ret, M)
 *   compare_exchange(P, AST_TYPE *expected, AST_TYPE *desired, bool weak, M, M) -> bool
 *   fetch_op / op_fetch(P, AST_TYPE, M) -> AST_TYPE (integers; pointers: bytes, as GCC)
 */
CC_PRIVATE uint32_t cc_builtin_atomic_build(cc *c, const cc_builtin *b, uint32_t loc)
{
	static const uint8_t nargs[] = {
		[CC_BUILTIN_ATOMIC_LOAD_N - CC_BUILTIN_ATOMIC_LOAD_N] = 2, [CC_BUILTIN_ATOMIC_STORE_N - CC_BUILTIN_ATOMIC_LOAD_N] = 3,
		[CC_BUILTIN_ATOMIC_EXCHANGE_N - CC_BUILTIN_ATOMIC_LOAD_N] = 3, [CC_BUILTIN_ATOMIC_CMPXCHG_N - CC_BUILTIN_ATOMIC_LOAD_N] = 6,
		[CC_BUILTIN_ATOMIC_LOAD - CC_BUILTIN_ATOMIC_LOAD_N] = 3, [CC_BUILTIN_ATOMIC_STORE - CC_BUILTIN_ATOMIC_LOAD_N] = 3,
		[CC_BUILTIN_ATOMIC_EXCHANGE - CC_BUILTIN_ATOMIC_LOAD_N] = 4, [CC_BUILTIN_ATOMIC_CMPXCHG - CC_BUILTIN_ATOMIC_LOAD_N] = 6,
	};
	int id = b->id, want = id <= CC_BUILTIN_ATOMIC_CMPXCHG ? nargs[id - CC_BUILTIN_ATOMIC_LOAD_N] :
			       id == CC_BUILTIN_SYNC_VAL_CAS || id == CC_BUILTIN_SYNC_BOOL_CAS ? 4 : id == CC_BUILTIN_SYNC_SYNCHRONIZE ? 1 :
			       id == CC_BUILTIN_ATOMIC_TEST_AND_SET || id == CC_BUILTIN_ATOMIC_CLEAR ? 2 : 3;
	cc_id_list args;
	ast_qualified_type pt, t, rt = cc_type_basic(AST_TYPE_VOID), tp;

	cc_builtin_arguments_parse(c, &args);
	for (uint32_t i = 0; i < args.n; i++)
		args.v[i] = cc_conversion_decay(c, args.v[i]);
	if (id == CC_BUILTIN_SYNC_SYNCHRONIZE) {	/* a full barrier: __atomic_thread_fence(seq_cst) */
		if (args.n)
			cc_builtin_arity_check(c, loc, 0, &args);
		args.n = 0;
		cc_id_list_append(c, &args, cc_node_integer_new(c, 5, cc_type_basic(AST_TYPE_INT), loc));
		return cc_builtin_node_new(c, CC_BUILTIN_ATOMIC_THREAD_FENCE, cc_type_basic(AST_TYPE_VOID), loc, &args);
	}
	if (id == CC_BUILTIN_SYNC_LOCK_RELEASE && args.n == 1) {	/* __atomic_store_n(p, 0, release) */
		cc_id_list_append(c, &args, cc_node_integer_new(c, 0, cc_type_basic(AST_TYPE_INT), loc));
		cc_id_list_append(c, &args, cc_node_integer_new(c, 3, cc_type_basic(AST_TYPE_INT), loc));
		id = CC_BUILTIN_ATOMIC_STORE_N;
	}
	if (b->sig[0] == 's' && (int)args.n == want - 1)	/* __sync_*: sequentially consistent */
		cc_id_list_append(c, &args, cc_node_integer_new(c, 5, cc_type_basic(AST_TYPE_INT), loc));
	if (b->sig[1] == 'c' && args.n == 5) {	/* __c11_atomic_compare_exchange_*: weak goes before the orders */
		cc_id_list_append(c, &args, args.v[4]);
		args.v[4] = args.v[3];
		args.v[3] = cc_node_integer_new(c, 0, cc_type_basic(AST_TYPE_INT), loc);
	}
	if (b->sig[0] == 's' && (id == CC_BUILTIN_ATOMIC_FETCH_NAND || id == CC_BUILTIN_ATOMIC_NAND_FETCH))
	{
		diag_span r = { loc, pp_token_end_find(c->p, loc) };	/* the builtin's name */

		cc_report_warning(c, "sync-fetch-and-nand-semantics-changed", true, NULL, c->args_rp, &r, 1, NULL, 0,
			 "the semantics of this intrinsic changed with GCC version 4.4 - the newer semantics are provided here");
	}
	if ((int)args.n != want) {
		cc_builtin_arity_check(c, loc, (unsigned)want, &args);
		return cc_node_integer_new(c, 0, cc_type_basic(AST_TYPE_INT), loc);
	}
	pt = AST_NODE(args.v[0])->ty;
	if (!cc_type_is_pointer(c, pt)) {
		diag_span r = cc_node_range(c, args.v[0]);

		report(c, "error", loc, &r, 1, NULL, 0, "address argument to atomic builtin must be a pointer ('%s' invalid)", tyname(c, pt));
		return cc_node_integer_new(c, 0, cc_type_basic(AST_TYPE_INT), loc);
	}
	if (b->sig[1] == 'i' || b->sig[1] == 'u') {	/* __sync_fetch_and_{,u}{max,min}: volatile int * / unsigned * */
		ast_qualified_type it = cc_type_basic(b->sig[1] == 'i' ? AST_TYPE_INT : AST_TYPE_UNSIGNED_INT);

		args.v[0] = cc_conversion_apply(c, args.v[0], cc_type_pointer(c, (it | AST_QUALIFIER_VOLATILE)));
		pt = AST_NODE(args.v[0])->ty;
	}
	if (id == CC_BUILTIN_ATOMIC_TEST_AND_SET || id == CC_BUILTIN_ATOMIC_CLEAR) {
		/* (p, order) on the byte at p: __atomic_exchange_n(p, 1, o) != 0 / __atomic_store_n(p, 0, o) */
		ast_qualified_type bt = cc_type_basic(AST_TYPE_UNSIGNED_CHAR);
		uint32_t r;
		cc_id_list a;

		cc_id_list_init(&a);
		cc_id_list_append(c, &a, cc_cast_insert(c, args.v[0], cc_type_pointer(c, (bt | (AST_QUALIFIED_TYPE_QUALIFIERS(AST_TYPE(c, pt)->base) & AST_QUALIFIER_VOLATILE))), AST_CAST_NOP));
		cc_id_list_append(c, &a, cc_node_integer_new(c, id == CC_BUILTIN_ATOMIC_CLEAR ? 0 : 1, bt, loc));
		cc_id_list_append(c, &a, cc_conversion_apply(c, args.v[1], cc_type_basic(AST_TYPE_INT)));
		if (id == CC_BUILTIN_ATOMIC_CLEAR)
			return cc_builtin_node_new(c, CC_BUILTIN_ATOMIC_STORE_N, cc_type_basic(AST_TYPE_VOID), loc, &a);
		r = cc_builtin_node_new(c, CC_BUILTIN_ATOMIC_EXCHANGE_N, bt, loc, &a);
		return cc_conversion_apply(c, r, cc_type_basic(AST_TYPE_BOOL));
	}
	t = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_TYPE(c, pt)->base);
	tp = cc_type_pointer(c, t);
	if (!(AST_TYPE(c, t)->size == 1 || AST_TYPE(c, t)->size == 2 || AST_TYPE(c, t)->size == 4 || AST_TYPE(c, t)->size == 8 || AST_TYPE(c, t)->size == 16) && !(((id >= CC_BUILTIN_ATOMIC_LOAD && id <= CC_BUILTIN_ATOMIC_CMPXCHG) || !strncmp(b->name, "__c11_atomic_", 13)) && cc_type_size(c, t))) {
		cc_report_error(c, loc, "address argument to atomic builtin must be a pointer to 1,2,4,8 or 16 byte type ('%s' invalid)", tyname(c, pt));
		return cc_node_integer_new(c, 0, cc_type_basic(AST_TYPE_INT), loc);
	}
	switch (id) {
	case CC_BUILTIN_ATOMIC_LOAD_N:
		rt = t;
		break;
	case CC_BUILTIN_ATOMIC_STORE_N:
		args.v[1] = cc_conversion_apply(c, args.v[1], t);
		break;
	case CC_BUILTIN_ATOMIC_EXCHANGE_N:
		args.v[1] = cc_conversion_apply(c, args.v[1], t);
		rt = t;
		break;
	case CC_BUILTIN_ATOMIC_CMPXCHG_N:
		args.v[1] = cc_conversion_apply(c, args.v[1], tp);
		args.v[2] = cc_conversion_apply(c, args.v[2], t);
		args.v[3] = cc_conversion_apply(c, args.v[3], cc_type_basic(AST_TYPE_BOOL));
		rt = cc_type_basic(AST_TYPE_BOOL);
		break;
	case CC_BUILTIN_ATOMIC_LOAD: case CC_BUILTIN_ATOMIC_STORE:
		args.v[1] = cc_conversion_apply(c, args.v[1], tp);
		break;
	case CC_BUILTIN_ATOMIC_EXCHANGE:
		args.v[1] = cc_conversion_apply(c, args.v[1], tp);
		args.v[2] = cc_conversion_apply(c, args.v[2], tp);
		break;
	case CC_BUILTIN_ATOMIC_CMPXCHG:
		args.v[1] = cc_conversion_apply(c, args.v[1], tp);
		args.v[2] = cc_conversion_apply(c, args.v[2], tp);
		args.v[3] = cc_conversion_apply(c, args.v[3], cc_type_basic(AST_TYPE_BOOL));
		rt = cc_type_basic(AST_TYPE_BOOL);
		break;
	case CC_BUILTIN_SYNC_VAL_CAS: case CC_BUILTIN_SYNC_BOOL_CAS:	/* (p, old, new) -> previous *p, or whether it was old */
		if (!cc_type_is_integer(c, t) && !cc_type_is_pointer(c, t))
			cc_report_error(c, loc, "address argument to atomic operation must be a pointer to integer or pointer ('%s' invalid)", tyname(c, pt));
		args.v[1] = cc_conversion_apply(c, args.v[1], t);
		args.v[2] = cc_conversion_apply(c, args.v[2], t);
		rt = id == CC_BUILTIN_SYNC_VAL_CAS ? t : cc_type_basic(AST_TYPE_BOOL);
		break;
	case CC_BUILTIN_ATOMIC_FETCH_MAX: case CC_BUILTIN_ATOMIC_FETCH_MIN: case CC_BUILTIN_ATOMIC_MAX_FETCH: case CC_BUILTIN_ATOMIC_MIN_FETCH:
		if (!cc_type_is_integer(c, t) || AST_TYPE_KIND(c, t) == AST_TYPE_BOOL)	/* floating max/min: not supported yet */
			cc_report_error(c, loc, "address argument to atomic operation must be a pointer to integer");
		args.v[1] = cc_conversion_apply(c, args.v[1], t);
		rt = t;
		break;
	case CC_BUILTIN_ATOMIC_FETCH_NAND: case CC_BUILTIN_ATOMIC_NAND_FETCH:
		if (!cc_type_is_integer(c, t))
			cc_report_error(c, loc, "address argument to atomic operation must be a pointer to integer");
		args.v[1] = cc_conversion_apply(c, args.v[1], t);
		rt = t;
		break;
	default:	/* fetch_op / op_fetch */
		if (!cc_type_is_integer(c, t) && !cc_type_is_pointer(c, t))
			cc_report_error(c, loc, "address argument to atomic operation must be a pointer to integer or pointer ('%s' invalid)", tyname(c, pt));
		args.v[1] = cc_conversion_apply(c, args.v[1], cc_type_is_pointer(c, t) ? c->t_ptrdiff : t);
		rt = t;
		break;
	}
	for (int i = want - (id == CC_BUILTIN_ATOMIC_CMPXCHG_N || id == CC_BUILTIN_ATOMIC_CMPXCHG ? 2 : 1); i < want; i++)
		args.v[i] = cc_conversion_apply(c, args.v[i], cc_type_basic(AST_TYPE_INT));
	if (((id >= CC_BUILTIN_ATOMIC_LOAD_N && id <= CC_BUILTIN_ATOMIC_MIN_FETCH) || id == CC_BUILTIN_SYNC_VAL_CAS || id == CC_BUILTIN_SYNC_BOOL_CAS) && cc_atomic_wide_type_is_wide(c, t))
		return cc_atomic_wide_builtin_build(c, id, t, &args, loc);
	return cc_builtin_node_new(c, id, rt, loc, &args);
}

/* The always query is a constant expression; its pointer is unevaluated.
 * Only widths emitted as native atomic operations are guaranteed lock-free.
 * In particular, Linux 16-byte accesses currently use libatomic. */
CC_PRIVATE uint32_t cc_builtin_atomic_lock_free_build(cc *c, const cc_builtin *b, uint32_t loc)
{
	cc_id_list args;
	cc_builtin_arguments_parse(c, &args);
	unsigned want = !strcmp(b->name, "__c11_atomic_is_lock_free") ? 1 : 2;
	if (args.n != want) { cc_builtin_arity_check(c, loc, want, &args); return cc_node_error_new(c, loc); }
	bool always = !strcmp(b->name, "__atomic_always_lock_free");
	uint32_t size = cc_conversion_apply(c, cc_conversion_decay(c, args.v[0]), c->t_size);
	uint32_t pointer = want == 2 ? cc_conversion_decay(c, args.v[1]) : cc_node_integer_new(c, 0, c->t_size, loc);
	uint64_t alignment = cc_type_is_pointer(c, AST_NODE(pointer)->ty) ? cc_type_alignment(c, AST_TYPE(c, AST_NODE(pointer)->ty)->base) : 1;
	cc_value cv, pv;
	bool constant = cc_constant_eval(c, size, &cv) && cv.kind == CC_VALUE_INTEGER && cv.ice &&
	                !cc_builtin_has_effects(c, size, true);
	if (cc_constant_eval(c, pointer, &pv) && (pv.kind == CC_VALUE_INTEGER || pv.kind == CC_VALUE_ADDRESS)) {
		if (pv.kind == CC_VALUE_INTEGER || (!pv.sym && !pv.str && !pv.label && !pv.sublabel)) {
			alignment = pv.i ? (uint64_t)pv.i & (0 - (uint64_t)pv.i) : UINT64_MAX;
		} else if (pv.sym) {
			alignment = cc_type_alignment(c, c->syms[pv.sym].ty);
			if (c->syms[pv.sym].align > alignment) alignment = c->syms[pv.sym].align;
			if (pv.i) {
				uint64_t offset_align = (uint64_t)pv.i & (0 - (uint64_t)pv.i);
				if (offset_align < alignment) alignment = offset_align;
			}
		}
	}
	uint32_t address = pointer;
	while (AST_NODE(address)->op == AST_NODE_CAST) address = AST_NODE(address)->a;
	if (AST_NODE(address)->op == AST_NODE_ADDRESS && AST_NODE(AST_NODE(address)->a)->op == AST_NODE_MEMBER) {
		const node *member = AST_NODE(AST_NODE(address)->a);
		uint64_t member_align = c->tags[member->b].f[member->c].align;
		if (member_align < alignment) alignment = member_align;
	}
	pointer = cc_conversion_apply(c, pointer, cc_type_pointer(c, cc_type_basic(AST_TYPE_VOID) | AST_QUALIFIER_CONST | AST_QUALIFIER_VOLATILE));
	bool native = constant && (cv.i == 1 || cv.i == 2 || cv.i == 4 || cv.i == 8) && alignment >= (uint64_t)cv.i;
	if (always) {
		if (!constant) cc_report_error(c, loc, "argument to '%s' must be a constant integer", b->name);
		return cc_node_integer_new(c, native, cc_type_basic(AST_TYPE_BOOL), loc);
	}
	/* Like GCC/Clang, a query resolved from size/alignment does not
	 * evaluate the optional pointer. The runtime path evaluates it once. */
	if (native)
		return cc_node_integer_new(c, 1, cc_type_basic(AST_TYPE_BOOL), loc);
	args.n = 0;
	cc_id_list_append(c, &args, size); cc_id_list_append(c, &args, pointer);
	if (c->o->target == PP_LINUX_AARCH64 || CC_X86_64(c))
		return cc_atomic_wide_lock_free_runtime_build(c, &args, loc);
	return cc_builtin_node_new(c, CC_BUILTIN_ATOMIC_LOCK_FREE, cc_type_basic(AST_TYPE_BOOL), loc, &args);
}
