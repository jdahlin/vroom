/* Pure constant control decisions. Each accepted AST form proves that every
 * evaluated operand is effect-free. Unknown forms stop immediately; dead
 * short-circuit operands are never inspected. There is no AST mutation,
 * side-effect scan, or whole-expression cc_eval pass. Target constant_p
 * callbacks retain their existing policy, including A64 dry generation with
 * rollback; this helper never commits target instructions.
 */
#include "../codegen-internal.h"

/* Only representation-preserving string/function decay is stripped. Dropping
 * arbitrary casts could incorrectly fold a truncation or address conversion. */
static uint32_t em_control_decay(cc *c, uint32_t n)
{
	unsigned left = 16;

	while (left-- && N(n)->op == AST_NODE_CAST &&
	       (N(n)->aux == AST_CAST_NOP || N(n)->aux == AST_CAST_ARRAY_DECAY || N(n)->aux == AST_CAST_FUNCTION_DECAY ||
	        (N(n)->aux == AST_CAST_POINTER_TO_POINTER && codegen_type_is_pointer(c, N(n)->ty) && codegen_type_is_pointer(c, N(N(n)->a)->ty) &&
	         AST_QUALIFIED_TYPE_UNQUALIFIED(T(c, N(n)->ty)->base) == AST_QUALIFIED_TYPE_UNQUALIFIED(T(c, N(N(n)->a)->ty)->base))))
		n = N(n)->a;
	return n;
}

VPRIV bool em_const_strcmp(cc *c, uint32_t n, i128 *value)
{
	const node *x = N(n);
	uint32_t fn, a, b;

	if (x->op != AST_NODE_CALL || x->c != 2)
		return false;
	fn = em_control_decay(c, x->a);
	if (N(fn)->op != AST_NODE_VARIABLE || c->syms[N(fn)->a].kind != AST_SYMBOL_BUILTIN ||
	    cc_builtin_library_kind(c->syms[N(fn)->a].val) != CC_LIBRARY_KIND_STRCMP)
		return false;
	a = em_control_decay(c, c->lists[x->b]);
	b = em_control_decay(c, c->lists[x->b + 1]);
	if (N(a)->op != AST_NODE_STRING || N(b)->op != AST_NODE_STRING ||
	    c->strs[N(a)->a].esz != 1 || c->strs[N(b)->a].esz != 1)
		return false;
	int r = strcmp(c->strs[N(a)->a].s, c->strs[N(b)->a].s);
	*value = r < 0 ? -1 : r > 0;
	return true;
}

static bool em_control_int(codegen_context *e, uint32_t n, int (*constant_p)(codegen_context *, uint32_t),
		unsigned *left, i128 *out)
{
	cc *c = e->compile_context;
	const node *x = N(n);
	i128 a;

	if (!*left || !codegen_type_is_integer(c, x->ty) || codegen_type_is_bitint(c, x->ty))
		return false;
	--*left;
	switch (x->op) {
	case AST_NODE_NUMBER:
		*out = codegen_integer_truncate(c, c->ipool[x->a], x->ty);
		return true;
	case AST_NODE_VARIABLE:
		if (c->syms[x->a].kind != AST_SYMBOL_ENUMERATOR)
			return false;
		*out = codegen_integer_truncate(c, c->syms[x->a].val, x->ty);
		return true;
	case AST_NODE_CALL:
		return em_const_strcmp(c, n, out);
	case AST_NODE_BUILTIN:
		if (x->aux != CC_BUILTIN_CONSTANT_P || !constant_p || x->c != 1)
			return false;
		a = constant_p(e, c->lists[x->b]);
		if (a < 0) return false;
		*out = a != 0;
		return true;
	case AST_NODE_CAST:
		if (x->aux != AST_CAST_INTEGER && x->aux != AST_CAST_NOP && x->aux != AST_CAST_BOOL)
			return false;
		if (!em_control_int(e, x->a, constant_p, left, &a))
			return false;
		*out = x->aux == AST_CAST_BOOL ? a != 0 : codegen_integer_truncate(c, a, x->ty);
		return true;
	case AST_NODE_UNARY:
		if (x->aux != LEX_TOKEN_EXCLAMATION || !em_control_int(e, x->a, constant_p, left, &a))
			return false;
		*out = !a;
		return true;
	case AST_NODE_LOGICAL_AND: case AST_NODE_LOGICAL_OR:
		if (!em_control_int(e, x->a, constant_p, left, &a))
			return false;
		if ((x->op == AST_NODE_LOGICAL_AND && !a) || (x->op == AST_NODE_LOGICAL_OR && a)) {
			*out = a != 0;
			return true;
		}
		if (!em_control_int(e, x->b, constant_p, left, &a))
			return false;
		*out = a != 0;
		return true;
	default:
		return false;
	}
}

VPRIV int em_const_truth(codegen_context *e, uint32_t n, int (*constant_p)(codegen_context *, uint32_t))
{
	unsigned left = 64;
	i128 value;

	return n && em_control_int(e, n, constant_p, &left, &value) ? value != 0 : -1;
}
