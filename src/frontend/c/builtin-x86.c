/* vcc front end: x86 SSE and pcmpstr builtins in ordinary vector lanes; see builtin-table.c for where a builtin's other facts live */
#include "ccimpl.h"

/* The builtin converts its constant control to char, as Clang does. The final hidden
 * argument identifies the result projection and explicit-length variant. */
VPRIV uint32_t x86_pcmpstr_call(cc *c, const builtin *b, vec *args, qty rt, uint32_t loc)
{
	bool explicit = strstr(b->name, "pcmpestr") != NULL;
	unsigned count = explicit ? 5 : 3;
	i128 imm;
	if (c->o->target != PP_LINUX_X86_64) {
		cc_error(c, loc, "'%s' requires an x86-64 target", b->name);
		return err_node(c, loc);
	}
	if (args->n != count) return err_node(c, loc);
	if (has_side_effects(c, args->v[count - 1]) || !const_int(c, args->v[count - 1], &imm)) {
		cc_error(c, loc, "argument to '%s' must be a constant integer", b->name);
		return err_node(c, loc);
	}
	const char *name = strstr(b->name, "str") + 3;
	unsigned kind = name[0] == 'm' ? 1 : name[1] == 'a' ? 2 : name[1] == 'c' ? 3 :
		name[1] == 'o' ? 4 : name[1] == 's' ? 5 : name[1] == 'z' ? 6 : 0;
	vpush(c, args, mk_int(c, kind | (explicit ? 8 : 0), prim(TY_INT), loc));
	return mk_builtin(c, BI_X86_PCMPSTR, rt, loc, args);
}

VPRIV uint32_t x86_simd_call(cc *c, const builtin *b, vec *args, qty rt, uint32_t loc)
{
	const char *name = b->name + sizeof("__builtin_ia32_") - 1;
	if (!strcmp(name, "shufps")) {
		i128 imm;
		if (args->n != 3 || !const_int(c, args->v[2], &imm) || imm < 0 || imm > 255) {
			cc_error(c, loc, "argument to '%s' must be a constant integer in range [0, 255]", b->name);
			return err_node(c, loc);
		}
		vec lanes;
		vinit(&lanes);
		vpush(c, &lanes, args->v[0]); vpush(c, &lanes, args->v[1]);
		for (unsigned i = 0; i < 4; i++)
			vpush(c, &lanes, mk_int(c, ((unsigned)imm >> (2 * i) & 3) + (i >= 2 ? 4 : 0), prim(TY_INT), loc));
		return mk_builtin(c, BI_SHUFFLEVECTOR, rt, loc, &lanes);
	}
	if (!strcmp(name, "movmskps")) {
		if (args->n != 1) return err_node(c, loc);
		qty vt = N(args->v[0])->ty;
		uint32_t sym = sym_new(c, S_VAR, 0, vt, loc);
		c->syms[sym].sc = SC_AUTO;
		uint32_t saved = mk(c, N_VAR, vt, loc);
		N(saved)->a = sym; N(saved)->flags = NF_LVALUE;
		uint32_t result = mk_int(c, 0, prim(TY_INT), loc);
		for (unsigned i = 0; i < 4; i++) {
			vec one;
			vinit(&one);
			vpush(c, &one, rvalue(c, builtin_vector_lane(c, saved, prim(TY_FLOAT), i, loc)));
			uint32_t sign = mk_builtin(c, BI_SIGNBIT, prim(TY_INT), loc, &one);
			sign = convert(c, convert(c, sign, prim(TY_BOOL)), prim(TY_INT));
			sign = mk_bin(c, LEX_TOKEN_LESS_LESS, prim(TY_INT), sign, mk_int(c, i, prim(TY_INT), loc), loc);
			result = mk_bin(c, LEX_TOKEN_PIPE, prim(TY_INT), result, sign, loc);
		}
		return with_pre(c, mk2(c, N_ASSIGN, vt, loc, saved, args->v[0]), result);
	}
	if (args->n != 2) return err_node(c, loc);
	bool shift = name[0] == 'p', cmp = name[0] == 'c';
	qty et = shift ? prim(TY_USHORT) : cmp ? prim(TY_INT) : prim(TY_FLOAT);
	qty vt = ty_vector(c, et, 16);
	uint32_t saved[2], pre[2];
	for (unsigned i = 0; i < 2; i++) {
		qty t = N(args->v[i])->ty;
		uint32_t sym = sym_new(c, S_VAR, 0, t, loc);
		c->syms[sym].sc = SC_AUTO;
		saved[i] = mk(c, N_VAR, t, loc); N(saved[i])->a = sym; N(saved[i])->flags = NF_LVALUE;
		pre[i] = mk2(c, N_ASSIGN, t, loc, saved[i], args->v[i]);
	}
	uint32_t sym = sym_new(c, S_VAR, 0, vt, loc);
	c->syms[sym].sc = SC_AUTO;
	uint32_t out = mk(c, N_VAR, vt, loc); N(out)->a = sym; N(out)->flags = NF_LVALUE;
	uint32_t result = cast_node(c, rvalue(c, out), rt, CK_NOP);
	for (unsigned i = 16 / ty_size(c, et); i--;) {
		uint32_t a = rvalue(c, builtin_vector_lane(c, saved[0], shift ? et : prim(TY_FLOAT), i, loc));
		uint32_t value;
		if (shift) {
			uint32_t count = convert(c, rvalue(c, saved[1]), prim(TY_UINT));
			uint32_t valid = mk_bin(c, LEX_TOKEN_LESS, prim(TY_INT), count, mk_int(c, 16, prim(TY_UINT), loc), loc);
			value = mk_bin(c, name[2] == 'l' ? LEX_TOKEN_LESS_LESS : LEX_TOKEN_GREATER_GREATER, prim(TY_UINT), convert(c, a, prim(TY_UINT)), count, loc);
			value = conditional(c, valid, value, mk_int(c, 0, prim(TY_UINT), loc), loc);
			value = convert(c, value, et);
		} else {
			uint32_t rhs = rvalue(c, builtin_vector_lane(c, saved[1], prim(TY_FLOAT), i, loc));
			uint32_t cond = mk_bin(c, cmp ? LEX_TOKEN_LESS_EQUAL : !strcmp(name, "maxps") ? LEX_TOKEN_GREATER : LEX_TOKEN_LESS, prim(TY_INT), a, rhs, loc);
			value = cmp ? conditional(c, cond, mk_int(c, 0, et, loc), mk_int(c, -1, et, loc), loc) : conditional(c, cond, a, rhs, loc);
		}
		uint32_t dst = builtin_vector_lane(c, out, et, i, loc);
		result = with_pre(c, mk2(c, N_ASSIGN, et, loc, dst, value), result);
	}
	return with_pre(c, pre[0], with_pre(c, pre[1], result));
}
