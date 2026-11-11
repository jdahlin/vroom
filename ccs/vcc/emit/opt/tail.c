/* Initial sibling-call eligibility: an exact forwarding wrapper, bounded by
   the target's register-argument capacity. No extra walk of a function body. */
#include "../emitimpl.h"

static bool em_tail_word(cc *c, qty t)
{
	uint64_t size = tsize(c, t);

	return !(QQ(t) & Q_VOLATILE) && !(T(c, t)->flags & (TF_ATOMIC | TF_VLA)) &&
	       !em_bitint(c, t) && (is_ptr(c, t) || is_integer(c, t)) &&
	       (size == 4 || size == 8);
}

/* The target must separately reject unsupported ABI/frame states. Capability
   is deliberately absent for default WebAssembly, whose MVP has no tail call. */
VPRIV bool em_tail_candidate(ecx *e, uint32_t fn, uint32_t expr, uint32_t maxargs, uint32_t *callee)
{
	cc *c = e->c;
	const sym *caller = &c->syms[fn];
	const ctype *ft = T(c, caller->ty);
	const node *body = N(caller->body), *ret, *call, *fdec, *name;
	const sym *target;
	const ctype *ct;
	uint32_t s;

	if (!e->E->o->opt || e->E->noopt || e->E->o->sibling_calls < 0 || e->E->o->instrument ||
	    !expr || body->op != N_BLOCK || body->c != 1 || body->aux > maxargs ||
	    (ft->flags & (TF_VARIADIC | TF_NOPROTO)))
		return false;
	ret = N(c->lists[body->b]);
	if (ret->op != N_RETURN || ret->a != expr || N(expr)->op != N_CALL)
		return false;
	call = N(expr);
	if (call->c > maxargs || QUNQ(call->ty) != QUNQ(ft->base) ||
	    (KIND(c, ft->base) != TY_VOID && !em_tail_word(c, ft->base)))
		return false;
	/* Exactly the function-to-pointer decay, with no function-pointer casts. */
	fdec = N(call->a);
	if (fdec->op != N_CAST || fdec->aux != CK_FDECAY || N(fdec->a)->op != N_VAR)
		return false;
	name = N(fdec->a);
	s = canon_sym(c, name->a);
	target = &c->syms[s];
	if (target->kind != S_FUNC || (target->flags & (SF_NORETURN | SF_ALWAYS_INLINE | SF_INLINE)) ||
	    sym_attrs(c, s)->error_fn || sym_attrs(c, s)->ifunc ||
	    sym_attrs(c, fn)->interrupt || sym_attrs(c, s)->interrupt ||
	    (sym_attrs(c, fn)->flags & (AF_TAIL_DISABLE | AF_TAIL_RETURNS_TWICE | AF_OPTNONE)) ||
	    (sym_attrs(c, s)->flags & (AF_TAIL_RETURNS_TWICE | AF_TAIL_NOT_CALLED)))
		return false;
	/* The standard nonlocal-jump entry points are special even when a caller
	   supplies a plain prototype without the usual returns_twice attribute. */
	if (target->name) {
		const char *n = vintern_ents[target->name].s;

		if (!strcmp(n, "setjmp") || !strcmp(n, "_setjmp") || !strcmp(n, "sigsetjmp") ||
		    !strcmp(n, "__sigsetjmp"))
			return false;
	}
	ct = T(c, target->ty);
	if (ct->kind != TY_FUNC || (ct->flags & (TF_VARIADIC | TF_NOPROTO)) ||
	    ct->n != call->c || QUNQ(ct->base) != QUNQ(ft->base))
		return false;
	for (uint32_t i = 0; i < body->aux; i++) {
		uint32_t p = c->lists[body->a + i];
		const sym *param = &c->syms[p];

		/* No parameter's address, cleanup, volatile access or dynamic type may
		   outlive this frame. There are no local declarations in this body. */
		if (!em_tail_word(c, param->ty) || vm_type(c, param->ty) ||
		    !em_local_eligible(e, p))
			return false;
	}
	for (uint32_t i = 0; i < call->c; i++) {
		const node *a = N(c->lists[call->b + i]);
		qty pt = c->params[ct->aux + i];
		bool formal = false;

		if (!em_tail_word(c, pt) || QUNQ(a->ty) != QUNQ(pt))
			return false;
		if (a->op == N_NUM || (a->op == N_UNARY && a->aux == T_MINUS && N(a->a)->op == N_NUM))
			continue;
		if (a->op != N_VAR)
			return false;
		for (uint32_t j = 0; j < body->aux; j++)
			formal |= a->a == c->lists[body->a + j];
		if (!formal)
			return false;
	}
	*callee = s;
	return true;
}
