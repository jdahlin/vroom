/* Linux libatomic fallback for objects wider than the native atomic access.
 * The runtime ABI takes a byte count and pointers, independently of the C
 * value's register/aggregate ABI. Hidden buffers include atomic tail padding. */
#include "cc-internal.h"

CC_PRIVATE bool cc_atomic_wide_type_is_wide(cc *c, ast_qualified_type t)
{
	uint64_t n = cc_type_size(c, t);
	return (c->o->target == PP_LINUX_AARCH64 || CC_X86_64(c)) &&
	       n && (n != 1 && n != 2 && n != 4 && n != 8);
}

static uint32_t cc_atomic_wide_address_build(cc *c, uint32_t n, uint32_t loc)
{
	uint32_t a = cc_node_new(c, AST_NODE_ADDRESS, cc_type_pointer(c, AST_NODE(n)->ty), loc);
	AST_NODE(a)->a = n;
	return a;
}

static uint32_t cc_atomic_wide_call_new(cc *c, const char *name, ast_qualified_type result, cc_id_list *args, uint32_t loc)
{
	ast_qualified_type params[6];
	for (uint32_t i = 0; i < args->n; i++)
		params[i] = AST_NODE(args->v[i])->ty;
	ast_qualified_type ft = cc_type_function(c, result, params, args->n, 0);
	uint32_t s = cc_symbol_new(c, AST_SYMBOL_FUNCTION, vintern_cstr(name), ft, loc);
	c->syms[s].sc = AST_STORAGE_EXTERN;
	c->syms[s].flags |= AST_SYMBOL_FLAG_GLOBAL | AST_SYMBOL_FLAG_USED;
	uint32_t fn = cc_node_new(c, AST_NODE_VARIABLE, ft, loc), call = cc_node_new(c, AST_NODE_CALL, result, loc);
	AST_NODE(fn)->a = s;
	AST_NODE(call)->a = cc_conversion_decay(c, fn);
	AST_NODE(call)->b = cc_id_list_freeze(c, args);
	AST_NODE(call)->c = args->n;
	return call;
}

CC_PRIVATE uint32_t cc_atomic_wide_lock_free_runtime_build(cc *c, cc_id_list *args, uint32_t loc)
{
	return cc_atomic_wide_call_new(c, "__atomic_is_lock_free", cc_type_basic(AST_TYPE_BOOL), args, loc);
}

static void cc_atomic_wide_statement_append(cc *c, cc_id_list *body, uint32_t expr, uint32_t loc)
{
	cc_id_list_append(c, body, cc_statement_new(c, AST_NODE_EXPRESSION, loc, expr, 0, 0));
}

static uint32_t cc_atomic_wide_statement_expression_finish(cc *c, cc_id_list *body, ast_qualified_type t, uint32_t result, uint32_t loc)
{
	cc_atomic_wide_statement_append(c, body, result, loc);
	uint32_t block = cc_statement_new(c, AST_NODE_BLOCK, loc, 0, cc_id_list_freeze(c, body), body->n);
	uint32_t n = cc_node_new(c, AST_NODE_STATEMENT_EXPRESSION, t, loc);
	AST_NODE(n)->a = block;
	AST_NODE(n)->aux = AST_NODE_ASSIGN; /* a lowered operation, not an unused-value warning */
	return n;
}

/* Materialize an operand once, with a declaration so its lifetime spans
 * statements and retry iterations in both backend frame allocators. */
static uint32_t cc_atomic_wide_value_save(cc *c, cc_id_list *body, uint32_t n, uint32_t loc)
{
	ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(cc_type_unatomic(c, AST_NODE(n)->ty));
	uint32_t s = cc_symbol_new(c, AST_SYMBOL_VARIABLE, 0, t, loc);
	c->syms[s].sc = AST_STORAGE_AUTO;
	uint32_t v = cc_node_new(c, AST_NODE_VARIABLE, t, loc);
	AST_NODE(v)->a = s;
	AST_NODE(v)->flags = AST_NODE_FLAG_LVALUE;
	cc_id_list_append(c, body, cc_node_new_operands(c, AST_NODE_DECLARATION, cc_type_basic(AST_TYPE_VOID), loc, s, n));
	return v;
}

/* A plain-type view of aligned storage large enough for the atomic layout. */
static uint32_t cc_atomic_wide_buffer_new(cc *c, cc_id_list *body, ast_qualified_type t, uint32_t loc)
{
	ast_qualified_type plain = AST_QUALIFIED_TYPE_UNQUALIFIED(cc_type_unatomic(c, t));
	ast_qualified_type storage = cc_type_array(c, cc_type_basic(AST_TYPE_UINT128), (cc_type_size(c, t) + 15) / 16, 0);
	uint32_t s = cc_symbol_new(c, AST_SYMBOL_VARIABLE, 0, storage, loc);
	c->syms[s].sc = AST_STORAGE_AUTO;
	uint32_t v = cc_node_new(c, AST_NODE_VARIABLE, storage, loc);
	AST_NODE(v)->a = s;
	AST_NODE(v)->flags = AST_NODE_FLAG_LVALUE;
	cc_id_list_append(c, body, cc_node_new_operands(c, AST_NODE_DECLARATION, cc_type_basic(AST_TYPE_VOID), loc, s, cc_node_new(c, AST_NODE_ZERO, storage, loc)));
	return cc_dereference_build(c, cc_cast_insert(c, cc_atomic_wide_address_build(c, v, loc), cc_type_pointer(c, plain), AST_CAST_NOP), loc);
}

/* Normalized pointer-based forms; use native atomics where possible. */
static uint32_t cc_atomic_wide_memory_build(cc *c, int id, ast_qualified_type t, cc_id_list *args, uint32_t loc)
{
	ast_qualified_type result = id == CC_BUILTIN_ATOMIC_CMPXCHG ? cc_type_basic(AST_TYPE_BOOL) : cc_type_basic(AST_TYPE_VOID);
	if (!cc_atomic_wide_type_is_wide(c, t))
		return cc_builtin_node_new(c, id, result, loc, args);
	cc_id_list call;
	cc_id_list_init(&call);
	cc_id_list_append(c, &call, cc_node_integer_new(c, cc_type_size(c, t), c->t_size, loc));
	for (uint32_t i = 0; i < args->n; i++) {
		if (id == CC_BUILTIN_ATOMIC_CMPXCHG && i == 3)
			continue; /* generic ABI is strong; weak has already been evaluated */
		cc_id_list_append(c, &call, args->v[i]);
	}
	const char *name = id == CC_BUILTIN_ATOMIC_LOAD ? "__atomic_load" : id == CC_BUILTIN_ATOMIC_STORE ? "__atomic_store" :
	                   id == CC_BUILTIN_ATOMIC_EXCHANGE ? "__atomic_exchange" : "__atomic_compare_exchange";
	return cc_atomic_wide_call_new(c, name, result, &call, loc);
}

CC_PRIVATE uint32_t cc_atomic_wide_load_build(cc *c, uint32_t n)
{
	uint32_t loc = AST_NODE(n)->loc;
	ast_qualified_type t = AST_NODE(n)->ty, plain = AST_QUALIFIED_TYPE_UNQUALIFIED(cc_type_unatomic(c, t));
	cc_id_list body, args;
	cc_id_list_init(&body); cc_id_list_init(&args);
	uint32_t ptr = cc_atomic_wide_value_save(c, &body, cc_atomic_wide_address_build(c, n, loc), loc);
	uint32_t tmp = cc_atomic_wide_buffer_new(c, &body, t, loc);
	cc_id_list_append(c, &args, ptr);
	cc_id_list_append(c, &args, cc_atomic_wide_address_build(c, tmp, loc));
	cc_id_list_append(c, &args, cc_node_integer_new(c, 5, cc_type_basic(AST_TYPE_INT), loc));
	cc_atomic_wide_statement_append(c, &body, cc_atomic_wide_memory_build(c, CC_BUILTIN_ATOMIC_LOAD, t, &args, loc), loc);
	return cc_atomic_wide_statement_expression_finish(c, &body, plain, cc_node_rvalue(c, tmp), loc);
}

/* op==0: assignment; rhs==0: ++/-- (post selects the old snapshot).
 * Otherwise a compound assignment. The RHS and object address are saved
 * outside the loop, while a failed CAS refreshes the entire expected value. */
CC_PRIVATE uint32_t cc_atomic_wide_assignment_build(cc *c, uint32_t lhs, uint32_t rhs, int op, bool post, uint32_t loc)
{
	ast_qualified_type t = AST_NODE(lhs)->ty, plain = AST_QUALIFIED_TYPE_UNQUALIFIED(cc_type_unatomic(c, t));
	bool incdec = !rhs;
	cc_id_list body, args;
	cc_id_list_init(&body); cc_id_list_init(&args);
	uint32_t ptr = cc_atomic_wide_value_save(c, &body, cc_atomic_wide_address_build(c, lhs, loc), loc);
	if (rhs)
		rhs = cc_atomic_wide_value_save(c, &body, cc_node_rvalue(c, rhs), loc);
	uint32_t desired = cc_atomic_wide_buffer_new(c, &body, t, loc);
	if (!op) {
		cc_atomic_wide_statement_append(c, &body, cc_assignment_build(c, 0, desired, rhs, loc), loc);
		cc_id_list_append(c, &args, ptr);
		cc_id_list_append(c, &args, cc_atomic_wide_address_build(c, desired, loc));
		cc_id_list_append(c, &args, cc_node_integer_new(c, 5, cc_type_basic(AST_TYPE_INT), loc));
		cc_atomic_wide_statement_append(c, &body, cc_atomic_wide_memory_build(c, CC_BUILTIN_ATOMIC_STORE, t, &args, loc), loc);
		return cc_atomic_wide_statement_expression_finish(c, &body, plain, cc_node_rvalue(c, desired), loc);
	}
	uint32_t expected = cc_atomic_wide_buffer_new(c, &body, t, loc);
	cc_id_list_append(c, &args, ptr);
	cc_id_list_append(c, &args, cc_atomic_wide_address_build(c, expected, loc));
	cc_id_list_append(c, &args, cc_node_integer_new(c, 5, cc_type_basic(AST_TYPE_INT), loc));
	cc_atomic_wide_statement_append(c, &body, cc_atomic_wide_memory_build(c, CC_BUILTIN_ATOMIC_LOAD, t, &args, loc), loc);
	uint32_t value;
	if (incdec && cc_type_is_complex(c, plain)) {
		ast_qualified_type et = cc_complex_element_type(c, plain);
		uint32_t re = cc_binary_build(c, op, cc_node_rvalue(c, cc_complex_part(c, expected, 0)), cc_node_integer_new(c, 1, cc_type_basic(AST_TYPE_INT), loc), loc);
		value = cc_complex_node_new(c, plain, cc_conversion_apply(c, re, et), cc_node_rvalue(c, cc_complex_part(c, expected, 1)), loc);
	} else {
		value = cc_binary_build(c, op, cc_node_rvalue(c, expected), incdec ? cc_node_integer_new(c, 1, cc_type_basic(AST_TYPE_INT), loc) : cc_node_rvalue(c, rhs), loc);
	}
	uint32_t update = cc_assignment_build(c, 0, desired, value, loc);
	args.n = 0;
	cc_id_list_append(c, &args, ptr);
	cc_id_list_append(c, &args, cc_atomic_wide_address_build(c, expected, loc));
	cc_id_list_append(c, &args, cc_atomic_wide_address_build(c, desired, loc));
	cc_id_list_append(c, &args, cc_node_integer_new(c, 0, cc_type_basic(AST_TYPE_INT), loc));
	cc_id_list_append(c, &args, cc_node_integer_new(c, 5, cc_type_basic(AST_TYPE_INT), loc));
	cc_id_list_append(c, &args, cc_node_integer_new(c, 5, cc_type_basic(AST_TYPE_INT), loc));
	uint32_t cas = cc_atomic_wide_memory_build(c, CC_BUILTIN_ATOMIC_CMPXCHG, t, &args, loc);
	uint32_t retry = cc_node_binary_new(c, LEX_TOKEN_EQUAL_EQUAL, cc_type_basic(AST_TYPE_INT), cas, cc_node_integer_new(c, 0, cc_type_basic(AST_TYPE_INT), loc), loc);
	cc_id_list_append(c, &body, cc_statement_new(c, AST_NODE_DO, loc, retry, cc_statement_new(c, AST_NODE_EXPRESSION, loc, update, 0, 0), 0));
	return cc_atomic_wide_statement_expression_finish(c, &body, plain, cc_node_rvalue(c, post ? expected : desired), loc);
}

/* Wider generic and _n load/store/exchange/CAS builtins. */
CC_PRIVATE uint32_t cc_atomic_wide_builtin_build(cc *c, int id, ast_qualified_type t, const cc_id_list *input, uint32_t loc)
{
	cc_id_list body, args;
	cc_id_list_init(&body); cc_id_list_init(&args);
	uint32_t a[6];
	for (uint32_t i = 0; i < input->n; i++)
		a[i] = cc_atomic_wide_value_save(c, &body, cc_node_rvalue(c, input->v[i]), loc);
	if (id == CC_BUILTIN_SYNC_VAL_CAS || id == CC_BUILTIN_SYNC_BOOL_CAS) {
		uint32_t expected = cc_atomic_wide_buffer_new(c, &body, t, loc), desired = cc_atomic_wide_buffer_new(c, &body, t, loc);
		cc_atomic_wide_statement_append(c, &body, cc_assignment_build(c, 0, expected, a[1], loc), loc);
		cc_atomic_wide_statement_append(c, &body, cc_assignment_build(c, 0, desired, a[2], loc), loc);
		cc_id_list_append(c, &args, a[0]); cc_id_list_append(c, &args, cc_atomic_wide_address_build(c, expected, loc));
		cc_id_list_append(c, &args, cc_atomic_wide_address_build(c, desired, loc));
		cc_id_list_append(c, &args, cc_node_integer_new(c, 0, cc_type_basic(AST_TYPE_INT), loc));
		cc_id_list_append(c, &args, cc_node_integer_new(c, 5, cc_type_basic(AST_TYPE_INT), loc));
		cc_id_list_append(c, &args, cc_node_integer_new(c, 5, cc_type_basic(AST_TYPE_INT), loc));
		uint32_t cas = cc_atomic_wide_memory_build(c, CC_BUILTIN_ATOMIC_CMPXCHG, t, &args, loc);
		if (id == CC_BUILTIN_SYNC_BOOL_CAS) return cc_atomic_wide_statement_expression_finish(c, &body, cc_type_basic(AST_TYPE_BOOL), cas, loc);
		cc_atomic_wide_statement_append(c, &body, cas, loc);
		return cc_atomic_wide_statement_expression_finish(c, &body, AST_QUALIFIED_TYPE_UNQUALIFIED(cc_type_unatomic(c, t)), cc_node_rvalue(c, expected), loc);
	}
	if (id >= CC_BUILTIN_ATOMIC_FETCH_ADD && id <= CC_BUILTIN_ATOMIC_MIN_FETCH) {
		bool post = id < CC_BUILTIN_ATOMIC_ADD_FETCH;
		int k = id - (post ? CC_BUILTIN_ATOMIC_FETCH_ADD : CC_BUILTIN_ATOMIC_ADD_FETCH);
		static const int ops[] = { LEX_TOKEN_PLUS, LEX_TOKEN_MINUS, LEX_TOKEN_AMPERSAND, LEX_TOKEN_PIPE, LEX_TOKEN_CARET, LEX_TOKEN_AMPERSAND };
		uint32_t old = cc_atomic_wide_buffer_new(c, &body, t, loc), desired = cc_atomic_wide_buffer_new(c, &body, t, loc);
		cc_id_list_append(c, &args, a[0]); cc_id_list_append(c, &args, cc_atomic_wide_address_build(c, old, loc));
		cc_id_list_append(c, &args, cc_node_integer_new(c, 5, cc_type_basic(AST_TYPE_INT), loc));
		cc_atomic_wide_statement_append(c, &body, cc_atomic_wide_memory_build(c, CC_BUILTIN_ATOMIC_LOAD, t, &args, loc), loc);
		uint32_t value;
		if (k < 6) {
			value = cc_binary_build(c, ops[k], cc_node_rvalue(c, old), cc_node_rvalue(c, a[1]), loc);
			if (k == 5) value = cc_binary_build(c, LEX_TOKEN_CARET, value, cc_node_integer_new(c, -1, t, loc), loc);
		} else {
			uint32_t cond = cc_binary_build(c, k == 6 ? LEX_TOKEN_GREATER : LEX_TOKEN_LESS, cc_node_rvalue(c, old), cc_node_rvalue(c, a[1]), loc);
			value = cc_node_new(c, AST_NODE_CONDITIONAL, t, loc);
			AST_NODE(value)->a = cond; AST_NODE(value)->b = cc_node_rvalue(c, old); AST_NODE(value)->c = cc_node_rvalue(c, a[1]);
		}
		uint32_t update = cc_assignment_build(c, 0, desired, value, loc);
		args.n = 0;
		cc_id_list_append(c, &args, a[0]); cc_id_list_append(c, &args, cc_atomic_wide_address_build(c, old, loc));
		cc_id_list_append(c, &args, cc_atomic_wide_address_build(c, desired, loc));
		cc_id_list_append(c, &args, cc_node_integer_new(c, 0, cc_type_basic(AST_TYPE_INT), loc));
		cc_id_list_append(c, &args, cc_node_integer_new(c, 5, cc_type_basic(AST_TYPE_INT), loc));
		cc_id_list_append(c, &args, cc_node_integer_new(c, 5, cc_type_basic(AST_TYPE_INT), loc));
		uint32_t cas = cc_atomic_wide_memory_build(c, CC_BUILTIN_ATOMIC_CMPXCHG, t, &args, loc);
		uint32_t retry = cc_node_binary_new(c, LEX_TOKEN_EQUAL_EQUAL, cc_type_basic(AST_TYPE_INT), cas, cc_node_integer_new(c, 0, cc_type_basic(AST_TYPE_INT), loc), loc);
		cc_id_list_append(c, &body, cc_statement_new(c, AST_NODE_DO, loc, retry, cc_statement_new(c, AST_NODE_EXPRESSION, loc, update, 0, 0), 0));
		return cc_atomic_wide_statement_expression_finish(c, &body, AST_QUALIFIED_TYPE_UNQUALIFIED(cc_type_unatomic(c, t)), cc_node_rvalue(c, post ? old : desired), loc);
	}
	uint32_t tmp = 0, result = 0, expected = 0, expected_lvalue = 0;
	int normalized = id;
	if (id <= CC_BUILTIN_ATOMIC_CMPXCHG_N) {
		normalized = id == CC_BUILTIN_ATOMIC_LOAD_N ? CC_BUILTIN_ATOMIC_LOAD : id == CC_BUILTIN_ATOMIC_STORE_N ? CC_BUILTIN_ATOMIC_STORE :
		             id == CC_BUILTIN_ATOMIC_EXCHANGE_N ? CC_BUILTIN_ATOMIC_EXCHANGE : CC_BUILTIN_ATOMIC_CMPXCHG;
		if (id != CC_BUILTIN_ATOMIC_LOAD_N) {
			tmp = cc_atomic_wide_buffer_new(c, &body, t, loc);
			cc_atomic_wide_statement_append(c, &body, cc_assignment_build(c, 0, tmp, a[id == CC_BUILTIN_ATOMIC_CMPXCHG_N ? 2 : 1], loc), loc);
		}
		if (id == CC_BUILTIN_ATOMIC_LOAD_N || id == CC_BUILTIN_ATOMIC_EXCHANGE_N)
			result = cc_atomic_wide_buffer_new(c, &body, t, loc);
	}
	cc_id_list_append(c, &args, a[0]);
	if (id == CC_BUILTIN_ATOMIC_LOAD_N) {
		cc_id_list_append(c, &args, cc_atomic_wide_address_build(c, result, loc)); cc_id_list_append(c, &args, a[1]);
	} else if (id == CC_BUILTIN_ATOMIC_STORE_N || id == CC_BUILTIN_ATOMIC_EXCHANGE_N) {
		cc_id_list_append(c, &args, cc_atomic_wide_address_build(c, tmp, loc));
		if (result) cc_id_list_append(c, &args, cc_atomic_wide_address_build(c, result, loc));
		cc_id_list_append(c, &args, a[2]);
	} else if (id == CC_BUILTIN_ATOMIC_CMPXCHG_N) {
		expected = cc_atomic_wide_buffer_new(c, &body, t, loc);
		expected_lvalue = cc_dereference_build(c, cc_cast_insert(c, a[1], cc_type_pointer(c, AST_QUALIFIED_TYPE_UNQUALIFIED(cc_type_unatomic(c, t))), AST_CAST_NOP), loc);
		cc_atomic_wide_statement_append(c, &body, cc_assignment_build(c, 0, expected, cc_node_rvalue(c, expected_lvalue), loc), loc);
		cc_id_list_append(c, &args, cc_atomic_wide_address_build(c, expected, loc)); cc_id_list_append(c, &args, cc_atomic_wide_address_build(c, tmp, loc));
		for (int i = 3; i < 6; i++) cc_id_list_append(c, &args, a[i]);
	} else {
		for (uint32_t i = 1; i < input->n; i++) cc_id_list_append(c, &args, a[i]);
	}
	uint32_t call = cc_atomic_wide_memory_build(c, normalized, t, &args, loc);
	if (expected) {
		uint32_t ok = cc_atomic_wide_value_save(c, &body, call, loc);
		uint32_t failed = cc_node_binary_new(c, LEX_TOKEN_EQUAL_EQUAL, cc_type_basic(AST_TYPE_INT), cc_node_rvalue(c, ok), cc_node_integer_new(c, 0, cc_type_basic(AST_TYPE_INT), loc), loc);
		uint32_t copy = cc_assignment_build(c, 0, expected_lvalue, cc_node_rvalue(c, expected), loc);
		cc_id_list_append(c, &body, cc_statement_new(c, AST_NODE_IF, loc, failed, cc_statement_new(c, AST_NODE_EXPRESSION, loc, copy, 0, 0), 0));
		return cc_atomic_wide_statement_expression_finish(c, &body, cc_type_basic(AST_TYPE_BOOL), cc_node_rvalue(c, ok), loc);
	}
	if (!result)
		return cc_atomic_wide_statement_expression_finish(c, &body, AST_NODE(call)->ty, call, loc);
	cc_atomic_wide_statement_append(c, &body, call, loc);
	return cc_atomic_wide_statement_expression_finish(c, &body, AST_QUALIFIED_TYPE_UNQUALIFIED(cc_type_unatomic(c, t)), cc_node_rvalue(c, result), loc);
}
