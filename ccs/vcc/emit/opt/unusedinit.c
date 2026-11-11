/* Never-referenced automatic integer declarations: no CFG or new function walk. */
#include "../emitimpl.h"

static bool em_unused_int(cc *c, qty t)
{
	return is_integer(c, t) && !em_bitint(c, t) && !is_wide(c, t) && tsize(c, t) - 1 < 8 &&
	       !(QQ(t) & Q_VOLATILE) && !(T(c, t)->flags & TF_ATOMIC);
}

/* Deliberately excludes loads through addresses, division, shifts, FP and
 * builtins. An exhausted candidate-local budget retains evaluation. */
static bool em_unused_pure(cc *c, uint32_t n, unsigned *left)
{
	const node *x;

	if (!n || !*left)
		return false;
	--*left;
	x = N(n);
	if (!em_unused_int(c, x->ty))
		return false;
	switch (x->op) {
	case N_NUM:
		return true;
	case N_VAR:
		return is_local_var(c, x->a) && em_unused_int(c, c->syms[x->a].ty) &&
		       c->syms[x->a].sc != SC_STATIC && c->syms[x->a].sc != SC_EXTERN &&
		       !sym_attrs(c, x->a)->asmname;
	case N_CAST:
		if (x->aux != CK_NOP && x->aux != CK_INT && x->aux != CK_BOOL)
			return false;
		return em_unused_pure(c, x->a, left);
	case N_UNARY:
		if (x->aux != T_PLUS && x->aux != T_MINUS && x->aux != T_TILDE && x->aux != T_EXCLAIM)
			return false;
		return em_unused_pure(c, x->a, left);
	case N_BINARY:
		switch (x->aux) {
		case T_PLUS: case T_MINUS: case T_STAR: case T_AMP: case T_PIPE: case T_CARET:
		case T_LESS: case T_LESSEQ: case T_GREATER: case T_GREATEREQ:
		case T_EQUALEQUAL: case T_EXCLAIMEQ:
			break;
		default:
			return false;
		}
		__attribute__((fallthrough));
	case N_LOGAND: case N_LOGOR: case N_COMMA:
		return em_unused_pure(c, x->a, left) && em_unused_pure(c, x->b, left);
	case N_COND:
		return em_unused_pure(c, x->a, left) && em_unused_pure(c, x->b, left) &&
		       em_unused_pure(c, x->c, left);
	default:
		return false;
	}
}

VPRIV bool em_unused_local(ecx *e, uint32_t s, uint32_t fn)
{
	cc *c = e->c;
	const sym *y;
	const attrs *a;

	if (!e->E->o->opt || e->E->noopt || (e->E->sflags[s] & SA_USED))
		return false;
	/* fn is supplied by the backend/selector; top also protects an optnone
	 * caller while a different function body is being inlined. */
	if ((fn && (sym_attrs(c, fn)->flags & AF_OPTNONE)) ||
	    (e->top && (sym_attrs(c, e->top)->flags & AF_OPTNONE)))
		return false;
	y = &c->syms[s];
	a = sym_attrs(c, s);
	return is_local_var(c, s) && y->kind == S_VAR && y->sc != SC_STATIC &&
	       y->sc != SC_EXTERN && y->sc != SC_TYPEDEF && !(y->flags & SF_ADDR_TAKEN) &&
	       em_unused_int(c, y->ty) && !vm_type(c, y->ty) && !a->cleanup &&
	       !a->used && !a->asmname;
}

VPRIV unsigned em_unused_init(ecx *e, const node *decl, uint32_t fn, unsigned budget)
{
	cc *c = e->c;

	if (!em_unused_local(e, decl->a, fn))
		return EM_UNUSED_KEEP;
	if (!decl->b)
		return EM_UNUSED_OMIT;
	/* Brace/zero initializer records belong to the initializer walker, not
	 * the value emitter. Keep that path rather than treating them as values. */
	if (N(decl->b)->op == N_INIT || N(decl->b)->op == N_ZERO || N(decl->b)->op == N_STR)
		return EM_UNUSED_KEEP;
	/* Evaluating a value rather than an effects-only expression preserves
	 * internal conversions and traps. The final integer conversion is unused. */
	if (!em_unused_int(c, N(decl->b)->ty))
		return EM_UNUSED_KEEP;
	return em_unused_pure(c, decl->b, &budget) ? EM_UNUSED_OMIT : EM_UNUSED_EVAL;
}
