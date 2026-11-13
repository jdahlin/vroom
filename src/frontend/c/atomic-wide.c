/* Linux libatomic fallback for objects wider than the native atomic access.
 * The runtime ABI takes a byte count and pointers, independently of the C
 * value's register/aggregate ABI. Hidden buffers include atomic tail padding. */
#include "ccimpl.h"

VPRIV bool atomic_wide(cc *c, qty t)
{
	uint64_t n = ty_size(c, t);
	return (c->o->target == PP_LINUX_AARCH64 || CC_X86_64(c)) &&
	       n && (n != 1 && n != 2 && n != 4 && n != 8);
}

static uint32_t at_address(cc *c, uint32_t n, uint32_t loc)
{
	uint32_t a = mk(c, N_ADDR, ty_ptr(c, N(n)->ty), loc);
	N(a)->a = n;
	return a;
}

static uint32_t at_call(cc *c, const char *name, qty result, vec *args, uint32_t loc)
{
	qty params[6];
	for (uint32_t i = 0; i < args->n; i++)
		params[i] = N(args->v[i])->ty;
	qty ft = ty_func(c, result, params, args->n, 0);
	uint32_t s = sym_new(c, S_FUNC, vintern_cstr(name), ft, loc);
	c->syms[s].sc = SC_EXTERN;
	c->syms[s].flags |= SF_GLOBAL | SF_USED;
	uint32_t fn = mk(c, N_VAR, ft, loc), call = mk(c, N_CALL, result, loc);
	N(fn)->a = s;
	N(call)->a = decay(c, fn);
	N(call)->b = vfreeze(c, args);
	N(call)->c = args->n;
	return call;
}

VPRIV uint32_t atomic_lock_free_runtime(cc *c, vec *args, uint32_t loc)
{
	return at_call(c, "__atomic_is_lock_free", prim(TY_BOOL), args, loc);
}

static void at_statement(cc *c, vec *body, uint32_t expr, uint32_t loc)
{
	vpush(c, body, mkstmt(c, N_EXPR, loc, expr, 0, 0));
}

static uint32_t at_finish(cc *c, vec *body, qty t, uint32_t result, uint32_t loc)
{
	at_statement(c, body, result, loc);
	uint32_t block = mkstmt(c, N_BLOCK, loc, 0, vfreeze(c, body), body->n);
	uint32_t n = mk(c, N_STMTEXPR, t, loc);
	N(n)->a = block;
	N(n)->aux = N_ASSIGN; /* a lowered operation, not an unused-value warning */
	return n;
}

/* Materialize an operand once, with a declaration so its lifetime spans
 * statements and retry iterations in both backend frame allocators. */
static uint32_t at_save(cc *c, vec *body, uint32_t n, uint32_t loc)
{
	qty t = QUNQ(ty_unatomic(c, N(n)->ty));
	uint32_t s = sym_new(c, S_VAR, 0, t, loc);
	c->syms[s].sc = SC_AUTO;
	uint32_t v = mk(c, N_VAR, t, loc);
	N(v)->a = s;
	N(v)->flags = NF_LVALUE;
	vpush(c, body, mk2(c, N_DECL, prim(TY_VOID), loc, s, n));
	return v;
}

/* A plain-type view of aligned storage large enough for the atomic layout. */
static uint32_t at_buffer(cc *c, vec *body, qty t, uint32_t loc)
{
	qty plain = QUNQ(ty_unatomic(c, t));
	qty storage = ty_array(c, prim(TY_UINT128), (ty_size(c, t) + 15) / 16, 0);
	uint32_t s = sym_new(c, S_VAR, 0, storage, loc);
	c->syms[s].sc = SC_AUTO;
	uint32_t v = mk(c, N_VAR, storage, loc);
	N(v)->a = s;
	N(v)->flags = NF_LVALUE;
	vpush(c, body, mk2(c, N_DECL, prim(TY_VOID), loc, s, mk(c, N_ZERO, storage, loc)));
	return deref(c, cast_node(c, at_address(c, v, loc), ty_ptr(c, plain), CK_NOP), loc);
}

/* Normalized pointer-based forms; use native atomics where possible. */
static uint32_t at_memory(cc *c, int id, qty t, vec *args, uint32_t loc)
{
	qty result = id == BI_ATOMIC_CMPXCHG ? prim(TY_BOOL) : prim(TY_VOID);
	if (!atomic_wide(c, t))
		return mk_builtin(c, id, result, loc, args);
	vec call;
	vinit(&call);
	vpush(c, &call, mk_int(c, ty_size(c, t), c->t_size, loc));
	for (uint32_t i = 0; i < args->n; i++) {
		if (id == BI_ATOMIC_CMPXCHG && i == 3)
			continue; /* generic ABI is strong; weak has already been evaluated */
		vpush(c, &call, args->v[i]);
	}
	const char *name = id == BI_ATOMIC_LOAD ? "__atomic_load" : id == BI_ATOMIC_STORE ? "__atomic_store" :
	                   id == BI_ATOMIC_EXCHANGE ? "__atomic_exchange" : "__atomic_compare_exchange";
	return at_call(c, name, result, &call, loc);
}

VPRIV uint32_t atomic_load_expr(cc *c, uint32_t n)
{
	uint32_t loc = N(n)->loc;
	qty t = N(n)->ty, plain = QUNQ(ty_unatomic(c, t));
	vec body, args;
	vinit(&body); vinit(&args);
	uint32_t ptr = at_save(c, &body, at_address(c, n, loc), loc);
	uint32_t tmp = at_buffer(c, &body, t, loc);
	vpush(c, &args, ptr);
	vpush(c, &args, at_address(c, tmp, loc));
	vpush(c, &args, mk_int(c, 5, prim(TY_INT), loc));
	at_statement(c, &body, at_memory(c, BI_ATOMIC_LOAD, t, &args, loc), loc);
	return at_finish(c, &body, plain, rvalue(c, tmp), loc);
}

/* op==0: assignment; rhs==0: ++/-- (post selects the old snapshot).
 * Otherwise a compound assignment. The RHS and object address are saved
 * outside the loop, while a failed CAS refreshes the entire expected value. */
VPRIV uint32_t atomic_assign_expr(cc *c, uint32_t lhs, uint32_t rhs, int op, bool post, uint32_t loc)
{
	qty t = N(lhs)->ty, plain = QUNQ(ty_unatomic(c, t));
	bool incdec = !rhs;
	vec body, args;
	vinit(&body); vinit(&args);
	uint32_t ptr = at_save(c, &body, at_address(c, lhs, loc), loc);
	if (rhs)
		rhs = at_save(c, &body, rvalue(c, rhs), loc);
	uint32_t desired = at_buffer(c, &body, t, loc);
	if (!op) {
		at_statement(c, &body, assign(c, 0, desired, rhs, loc), loc);
		vpush(c, &args, ptr);
		vpush(c, &args, at_address(c, desired, loc));
		vpush(c, &args, mk_int(c, 5, prim(TY_INT), loc));
		at_statement(c, &body, at_memory(c, BI_ATOMIC_STORE, t, &args, loc), loc);
		return at_finish(c, &body, plain, rvalue(c, desired), loc);
	}
	uint32_t expected = at_buffer(c, &body, t, loc);
	vpush(c, &args, ptr);
	vpush(c, &args, at_address(c, expected, loc));
	vpush(c, &args, mk_int(c, 5, prim(TY_INT), loc));
	at_statement(c, &body, at_memory(c, BI_ATOMIC_LOAD, t, &args, loc), loc);
	uint32_t value;
	if (incdec && is_complex(c, plain)) {
		qty et = cplx_elem(c, plain);
		uint32_t re = binop(c, op, rvalue(c, cplx_part(c, expected, 0)), mk_int(c, 1, prim(TY_INT), loc), loc);
		value = mk_cplx(c, plain, convert(c, re, et), rvalue(c, cplx_part(c, expected, 1)), loc);
	} else {
		value = binop(c, op, rvalue(c, expected), incdec ? mk_int(c, 1, prim(TY_INT), loc) : rvalue(c, rhs), loc);
	}
	uint32_t update = assign(c, 0, desired, value, loc);
	args.n = 0;
	vpush(c, &args, ptr);
	vpush(c, &args, at_address(c, expected, loc));
	vpush(c, &args, at_address(c, desired, loc));
	vpush(c, &args, mk_int(c, 0, prim(TY_INT), loc));
	vpush(c, &args, mk_int(c, 5, prim(TY_INT), loc));
	vpush(c, &args, mk_int(c, 5, prim(TY_INT), loc));
	uint32_t cas = at_memory(c, BI_ATOMIC_CMPXCHG, t, &args, loc);
	uint32_t retry = mk_bin(c, LEX_TOKEN_EQUAL_EQUAL, prim(TY_INT), cas, mk_int(c, 0, prim(TY_INT), loc), loc);
	vpush(c, &body, mkstmt(c, N_DO, loc, retry, mkstmt(c, N_EXPR, loc, update, 0, 0), 0));
	return at_finish(c, &body, plain, rvalue(c, post ? expected : desired), loc);
}

/* Wider generic and _n load/store/exchange/CAS builtins. */
VPRIV uint32_t atomic_builtin_expr(cc *c, int id, qty t, const vec *input, uint32_t loc)
{
	vec body, args;
	vinit(&body); vinit(&args);
	uint32_t a[6];
	for (uint32_t i = 0; i < input->n; i++)
		a[i] = at_save(c, &body, rvalue(c, input->v[i]), loc);
	if (id == BI_SYNC_VAL_CAS || id == BI_SYNC_BOOL_CAS) {
		uint32_t expected = at_buffer(c, &body, t, loc), desired = at_buffer(c, &body, t, loc);
		at_statement(c, &body, assign(c, 0, expected, a[1], loc), loc);
		at_statement(c, &body, assign(c, 0, desired, a[2], loc), loc);
		vpush(c, &args, a[0]); vpush(c, &args, at_address(c, expected, loc));
		vpush(c, &args, at_address(c, desired, loc));
		vpush(c, &args, mk_int(c, 0, prim(TY_INT), loc));
		vpush(c, &args, mk_int(c, 5, prim(TY_INT), loc));
		vpush(c, &args, mk_int(c, 5, prim(TY_INT), loc));
		uint32_t cas = at_memory(c, BI_ATOMIC_CMPXCHG, t, &args, loc);
		if (id == BI_SYNC_BOOL_CAS) return at_finish(c, &body, prim(TY_BOOL), cas, loc);
		at_statement(c, &body, cas, loc);
		return at_finish(c, &body, QUNQ(ty_unatomic(c, t)), rvalue(c, expected), loc);
	}
	if (id >= BI_ATOMIC_FETCH_ADD && id <= BI_ATOMIC_MIN_FETCH) {
		bool post = id < BI_ATOMIC_ADD_FETCH;
		int k = id - (post ? BI_ATOMIC_FETCH_ADD : BI_ATOMIC_ADD_FETCH);
		static const int ops[] = { LEX_TOKEN_PLUS, LEX_TOKEN_MINUS, LEX_TOKEN_AMPERSAND, LEX_TOKEN_PIPE, LEX_TOKEN_CARET, LEX_TOKEN_AMPERSAND };
		uint32_t old = at_buffer(c, &body, t, loc), desired = at_buffer(c, &body, t, loc);
		vpush(c, &args, a[0]); vpush(c, &args, at_address(c, old, loc));
		vpush(c, &args, mk_int(c, 5, prim(TY_INT), loc));
		at_statement(c, &body, at_memory(c, BI_ATOMIC_LOAD, t, &args, loc), loc);
		uint32_t value;
		if (k < 6) {
			value = binop(c, ops[k], rvalue(c, old), rvalue(c, a[1]), loc);
			if (k == 5) value = binop(c, LEX_TOKEN_CARET, value, mk_int(c, -1, t, loc), loc);
		} else {
			uint32_t cond = binop(c, k == 6 ? LEX_TOKEN_GREATER : LEX_TOKEN_LESS, rvalue(c, old), rvalue(c, a[1]), loc);
			value = mk(c, N_COND, t, loc);
			N(value)->a = cond; N(value)->b = rvalue(c, old); N(value)->c = rvalue(c, a[1]);
		}
		uint32_t update = assign(c, 0, desired, value, loc);
		args.n = 0;
		vpush(c, &args, a[0]); vpush(c, &args, at_address(c, old, loc));
		vpush(c, &args, at_address(c, desired, loc));
		vpush(c, &args, mk_int(c, 0, prim(TY_INT), loc));
		vpush(c, &args, mk_int(c, 5, prim(TY_INT), loc));
		vpush(c, &args, mk_int(c, 5, prim(TY_INT), loc));
		uint32_t cas = at_memory(c, BI_ATOMIC_CMPXCHG, t, &args, loc);
		uint32_t retry = mk_bin(c, LEX_TOKEN_EQUAL_EQUAL, prim(TY_INT), cas, mk_int(c, 0, prim(TY_INT), loc), loc);
		vpush(c, &body, mkstmt(c, N_DO, loc, retry, mkstmt(c, N_EXPR, loc, update, 0, 0), 0));
		return at_finish(c, &body, QUNQ(ty_unatomic(c, t)), rvalue(c, post ? old : desired), loc);
	}
	uint32_t tmp = 0, result = 0, expected = 0, expected_lvalue = 0;
	int normalized = id;
	if (id <= BI_ATOMIC_CMPXCHG_N) {
		normalized = id == BI_ATOMIC_LOAD_N ? BI_ATOMIC_LOAD : id == BI_ATOMIC_STORE_N ? BI_ATOMIC_STORE :
		             id == BI_ATOMIC_EXCHANGE_N ? BI_ATOMIC_EXCHANGE : BI_ATOMIC_CMPXCHG;
		if (id != BI_ATOMIC_LOAD_N) {
			tmp = at_buffer(c, &body, t, loc);
			at_statement(c, &body, assign(c, 0, tmp, a[id == BI_ATOMIC_CMPXCHG_N ? 2 : 1], loc), loc);
		}
		if (id == BI_ATOMIC_LOAD_N || id == BI_ATOMIC_EXCHANGE_N)
			result = at_buffer(c, &body, t, loc);
	}
	vpush(c, &args, a[0]);
	if (id == BI_ATOMIC_LOAD_N) {
		vpush(c, &args, at_address(c, result, loc)); vpush(c, &args, a[1]);
	} else if (id == BI_ATOMIC_STORE_N || id == BI_ATOMIC_EXCHANGE_N) {
		vpush(c, &args, at_address(c, tmp, loc));
		if (result) vpush(c, &args, at_address(c, result, loc));
		vpush(c, &args, a[2]);
	} else if (id == BI_ATOMIC_CMPXCHG_N) {
		expected = at_buffer(c, &body, t, loc);
		expected_lvalue = deref(c, cast_node(c, a[1], ty_ptr(c, QUNQ(ty_unatomic(c, t))), CK_NOP), loc);
		at_statement(c, &body, assign(c, 0, expected, rvalue(c, expected_lvalue), loc), loc);
		vpush(c, &args, at_address(c, expected, loc)); vpush(c, &args, at_address(c, tmp, loc));
		for (int i = 3; i < 6; i++) vpush(c, &args, a[i]);
	} else {
		for (uint32_t i = 1; i < input->n; i++) vpush(c, &args, a[i]);
	}
	uint32_t call = at_memory(c, normalized, t, &args, loc);
	if (expected) {
		uint32_t ok = at_save(c, &body, call, loc);
		uint32_t failed = mk_bin(c, LEX_TOKEN_EQUAL_EQUAL, prim(TY_INT), rvalue(c, ok), mk_int(c, 0, prim(TY_INT), loc), loc);
		uint32_t copy = assign(c, 0, expected_lvalue, rvalue(c, expected), loc);
		vpush(c, &body, mkstmt(c, N_IF, loc, failed, mkstmt(c, N_EXPR, loc, copy, 0, 0), 0));
		return at_finish(c, &body, prim(TY_BOOL), rvalue(c, ok), loc);
	}
	if (!result)
		return at_finish(c, &body, N(call)->ty, call, loc);
	at_statement(c, &body, call, loc);
	return at_finish(c, &body, QUNQ(ty_unatomic(c, t)), rvalue(c, result), loc);
}
