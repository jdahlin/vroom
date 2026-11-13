/* vcc front end: x86 SSE and pcmpstr builtins in ordinary vector lanes; see builtin-table.c for where a builtin's other facts live */
#include "cc-internal.h"

/* The builtin converts its constant control to char, as Clang does. The final hidden
 * argument identifies the result projection and explicit-length variant. */
CC_PRIVATE uint32_t cc_builtin_x86_pcmpstr_build(cc *c, const cc_builtin *b, cc_id_list *args, ast_qualified_type rt, uint32_t loc)
{
	bool explicit = strstr(b->name, "pcmpestr") != NULL;
	unsigned count = explicit ? 5 : 3;
	i128 imm;
	if (c->o->target != PP_LINUX_X86_64) {
		cc_report_error(c, loc, "'%s' requires an x86-64 target", b->name);
		return cc_node_error_new(c, loc);
	}
	if (args->n != count) return cc_node_error_new(c, loc);
	if (cc_expression_has_side_effects(c, args->v[count - 1]) || !cc_constant_integer(c, args->v[count - 1], &imm)) {
		cc_report_error(c, loc, "argument to '%s' must be a constant integer", b->name);
		return cc_node_error_new(c, loc);
	}
	const char *name = strstr(b->name, "str") + 3;
	unsigned kind = name[0] == 'm' ? 1 : name[1] == 'a' ? 2 : name[1] == 'c' ? 3 :
		name[1] == 'o' ? 4 : name[1] == 's' ? 5 : name[1] == 'z' ? 6 : 0;
	cc_id_list_append(c, args, cc_node_integer_new(c, kind | (explicit ? 8 : 0), cc_type_basic(AST_TYPE_INT), loc));
	return cc_builtin_node_new(c, CC_BUILTIN_X86_PCMPSTR, rt, loc, args);
}

CC_PRIVATE uint32_t cc_builtin_x86_simd_build(cc *c, const cc_builtin *b, cc_id_list *args, ast_qualified_type rt, uint32_t loc)
{
	const char *name = b->name + sizeof("__builtin_ia32_") - 1;
	if (!strcmp(name, "shufps")) {
		i128 imm;
		if (args->n != 3 || !cc_constant_integer(c, args->v[2], &imm) || imm < 0 || imm > 255) {
			cc_report_error(c, loc, "argument to '%s' must be a constant integer in range [0, 255]", b->name);
			return cc_node_error_new(c, loc);
		}
		cc_id_list lanes;
		cc_id_list_init(&lanes);
		cc_id_list_append(c, &lanes, args->v[0]); cc_id_list_append(c, &lanes, args->v[1]);
		for (unsigned i = 0; i < 4; i++)
			cc_id_list_append(c, &lanes, cc_node_integer_new(c, ((unsigned)imm >> (2 * i) & 3) + (i >= 2 ? 4 : 0), cc_type_basic(AST_TYPE_INT), loc));
		return cc_builtin_node_new(c, CC_BUILTIN_SHUFFLEVECTOR, rt, loc, &lanes);
	}
	if (!strcmp(name, "movmskps")) {
		if (args->n != 1) return cc_node_error_new(c, loc);
		ast_qualified_type vt = AST_NODE(args->v[0])->ty;
		uint32_t sym = cc_symbol_new(c, AST_SYMBOL_VARIABLE, 0, vt, loc);
		c->syms[sym].sc = AST_STORAGE_AUTO;
		uint32_t saved = cc_node_new(c, AST_NODE_VARIABLE, vt, loc);
		AST_NODE(saved)->a = sym; AST_NODE(saved)->flags = AST_NODE_FLAG_LVALUE;
		uint32_t result = cc_node_integer_new(c, 0, cc_type_basic(AST_TYPE_INT), loc);
		for (unsigned i = 0; i < 4; i++) {
			cc_id_list one;
			cc_id_list_init(&one);
			cc_id_list_append(c, &one, cc_node_rvalue(c, cc_builtin_vector_lane(c, saved, cc_type_basic(AST_TYPE_FLOAT), i, loc)));
			uint32_t sign = cc_builtin_node_new(c, CC_BUILTIN_SIGNBIT, cc_type_basic(AST_TYPE_INT), loc, &one);
			sign = cc_conversion_apply(c, cc_conversion_apply(c, sign, cc_type_basic(AST_TYPE_BOOL)), cc_type_basic(AST_TYPE_INT));
			sign = cc_node_binary_new(c, LEX_TOKEN_LESS_LESS, cc_type_basic(AST_TYPE_INT), sign, cc_node_integer_new(c, i, cc_type_basic(AST_TYPE_INT), loc), loc);
			result = cc_node_binary_new(c, LEX_TOKEN_PIPE, cc_type_basic(AST_TYPE_INT), result, sign, loc);
		}
		return cc_lowering_sequence(c, cc_node_new_operands(c, AST_NODE_ASSIGN, vt, loc, saved, args->v[0]), result);
	}
	if (args->n != 2) return cc_node_error_new(c, loc);
	bool shift = name[0] == 'p', cmp = name[0] == 'c';
	ast_qualified_type et = shift ? cc_type_basic(AST_TYPE_UNSIGNED_SHORT) : cmp ? cc_type_basic(AST_TYPE_INT) : cc_type_basic(AST_TYPE_FLOAT);
	ast_qualified_type vt = cc_type_vector(c, et, 16);
	uint32_t saved[2], pre[2];
	for (unsigned i = 0; i < 2; i++) {
		ast_qualified_type t = AST_NODE(args->v[i])->ty;
		uint32_t sym = cc_symbol_new(c, AST_SYMBOL_VARIABLE, 0, t, loc);
		c->syms[sym].sc = AST_STORAGE_AUTO;
		saved[i] = cc_node_new(c, AST_NODE_VARIABLE, t, loc); AST_NODE(saved[i])->a = sym; AST_NODE(saved[i])->flags = AST_NODE_FLAG_LVALUE;
		pre[i] = cc_node_new_operands(c, AST_NODE_ASSIGN, t, loc, saved[i], args->v[i]);
	}
	uint32_t sym = cc_symbol_new(c, AST_SYMBOL_VARIABLE, 0, vt, loc);
	c->syms[sym].sc = AST_STORAGE_AUTO;
	uint32_t out = cc_node_new(c, AST_NODE_VARIABLE, vt, loc); AST_NODE(out)->a = sym; AST_NODE(out)->flags = AST_NODE_FLAG_LVALUE;
	uint32_t result = cc_cast_insert(c, cc_node_rvalue(c, out), rt, AST_CAST_NOP);
	for (unsigned i = 16 / cc_type_size(c, et); i--;) {
		uint32_t a = cc_node_rvalue(c, cc_builtin_vector_lane(c, saved[0], shift ? et : cc_type_basic(AST_TYPE_FLOAT), i, loc));
		uint32_t value;
		if (shift) {
			uint32_t count = cc_conversion_apply(c, cc_node_rvalue(c, saved[1]), cc_type_basic(AST_TYPE_UNSIGNED_INT));
			uint32_t valid = cc_node_binary_new(c, LEX_TOKEN_LESS, cc_type_basic(AST_TYPE_INT), count, cc_node_integer_new(c, 16, cc_type_basic(AST_TYPE_UNSIGNED_INT), loc), loc);
			value = cc_node_binary_new(c, name[2] == 'l' ? LEX_TOKEN_LESS_LESS : LEX_TOKEN_GREATER_GREATER, cc_type_basic(AST_TYPE_UNSIGNED_INT), cc_conversion_apply(c, a, cc_type_basic(AST_TYPE_UNSIGNED_INT)), count, loc);
			value = cc_conditional_build(c, valid, value, cc_node_integer_new(c, 0, cc_type_basic(AST_TYPE_UNSIGNED_INT), loc), loc);
			value = cc_conversion_apply(c, value, et);
		} else {
			uint32_t rhs = cc_node_rvalue(c, cc_builtin_vector_lane(c, saved[1], cc_type_basic(AST_TYPE_FLOAT), i, loc));
			uint32_t cond = cc_node_binary_new(c, cmp ? LEX_TOKEN_LESS_EQUAL : !strcmp(name, "maxps") ? LEX_TOKEN_GREATER : LEX_TOKEN_LESS, cc_type_basic(AST_TYPE_INT), a, rhs, loc);
			value = cmp ? cc_conditional_build(c, cond, cc_node_integer_new(c, 0, et, loc), cc_node_integer_new(c, -1, et, loc), loc) : cc_conditional_build(c, cond, a, rhs, loc);
		}
		uint32_t dst = cc_builtin_vector_lane(c, out, et, i, loc);
		result = cc_lowering_sequence(c, cc_node_new_operands(c, AST_NODE_ASSIGN, et, loc, dst, value), result);
	}
	return cc_lowering_sequence(c, pre[0], cc_lowering_sequence(c, pre[1], result));
}
