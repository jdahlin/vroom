/* Pure constant control decisions. Each accepted AST form proves that every
 * evaluated operand is effect-free. Unknown forms stop immediately; dead
 * short-circuit operands are never inspected. There is no AST mutation,
 * side-effect scan, or whole-expression cc_eval pass. Target constant_p
 * callbacks retain their existing policy, including A64 dry generation with
 * rollback; this helper never commits target instructions.
 */
#include "../emitimpl.h"

/* Only representation-preserving string/function decay is stripped. Dropping
 * arbitrary casts could incorrectly fold a truncation or address conversion. */
static uint32_t em_control_decay(cc *c, uint32_t n)
{
	unsigned left = 16;

	while (left-- && N(n)->op == N_CAST &&
	       (N(n)->aux == CK_NOP || N(n)->aux == CK_DECAY || N(n)->aux == CK_FDECAY ||
	        (N(n)->aux == CK_P2P && is_ptr(c, N(n)->ty) && is_ptr(c, N(N(n)->a)->ty) &&
	         QUNQ(T(c, N(n)->ty)->base) == QUNQ(T(c, N(N(n)->a)->ty)->base))))
		n = N(n)->a;
	return n;
}

VPRIV bool em_const_strcmp(cc *c, uint32_t n, i128 *value)
{
	const node *x = N(n);
	uint32_t fn, a, b;

	if (x->op != N_CALL || x->c != 2)
		return false;
	fn = em_control_decay(c, x->a);
	if (N(fn)->op != N_VAR || c->syms[N(fn)->a].kind != S_BUILTIN ||
	    cc_builtin_libkind(c->syms[N(fn)->a].val) != CC_LIB_STRCMP)
		return false;
	a = em_control_decay(c, c->lists[x->b]);
	b = em_control_decay(c, c->lists[x->b + 1]);
	if (N(a)->op != N_STR || N(b)->op != N_STR ||
	    c->strs[N(a)->a].esz != 1 || c->strs[N(b)->a].esz != 1)
		return false;
	int r = strcmp(c->strs[N(a)->a].s, c->strs[N(b)->a].s);
	*value = r < 0 ? -1 : r > 0;
	return true;
}

static bool em_control_int(ecx *e, uint32_t n, int (*constant_p)(ecx *, uint32_t),
		unsigned *left, i128 *out)
{
	cc *c = e->c;
	const node *x = N(n);
	i128 a;

	if (!*left || !is_integer(c, x->ty) || em_bitint(c, x->ty))
		return false;
	--*left;
	switch (x->op) {
	case N_NUM:
		*out = em_trunc_to(c, c->ipool[x->a], x->ty);
		return true;
	case N_VAR:
		if (c->syms[x->a].kind != S_ENUMCONST)
			return false;
		*out = em_trunc_to(c, c->syms[x->a].val, x->ty);
		return true;
	case N_CALL:
		return em_const_strcmp(c, n, out);
	case N_BUILTIN:
		if (x->aux != BI_CONSTANT_P || !constant_p || x->c != 1)
			return false;
		a = constant_p(e, c->lists[x->b]);
		if (a < 0) return false;
		*out = a != 0;
		return true;
	case N_CAST:
		if (x->aux != CK_INT && x->aux != CK_NOP && x->aux != CK_BOOL)
			return false;
		if (!em_control_int(e, x->a, constant_p, left, &a))
			return false;
		*out = x->aux == CK_BOOL ? a != 0 : em_trunc_to(c, a, x->ty);
		return true;
	case N_UNARY:
		if (x->aux != T_EXCLAIM || !em_control_int(e, x->a, constant_p, left, &a))
			return false;
		*out = !a;
		return true;
	case N_LOGAND: case N_LOGOR:
		if (!em_control_int(e, x->a, constant_p, left, &a))
			return false;
		if ((x->op == N_LOGAND && !a) || (x->op == N_LOGOR && a)) {
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

VPRIV int em_const_truth(ecx *e, uint32_t n, int (*constant_p)(ecx *, uint32_t))
{
	unsigned left = 64;
	i128 value;

	return n && em_control_int(e, n, constant_p, &left, &value) ? value != 0 : -1;
}
