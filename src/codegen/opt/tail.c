/*
 * tail.c: may a function end in a sibling call (a tail call) to its callee?  Only the conservative
 * case: a body that is exactly `return f(args);` forwarding to a direct call.
 *
 * em_tail_caller_ok and em_tail_target_ok are the option and attribute rules (-O1 or more, sibling
 * calls not disabled, not instrumented, no interrupt, error or ifunc function, no attribute that
 * disables tail calls or returns twice, and no setjmp-family target), and em_tail_candidate adds the
 * shape: no variadics, the arguments fit the target's register arguments (maxargs), word-sized
 * integer or pointer types.  The target must separately reject ABI and frame states it cannot do;
 * default WebAssembly has no tail call and does not use this.  Called by arm64's and x86-64's tail
 * passes and, for the two rules, by arm64's VIR lowering (arch/a64/vir).
 */
#include "../codegen-internal.h"

static bool em_tail_word(cc *c, ast_qualified_type t)
{
	uint64_t size = codegen_type_size(c, t);

	return !(AST_QUALIFIED_TYPE_QUALIFIERS(t) & AST_QUALIFIER_VOLATILE) && !(T(c, t)->flags & (AST_TYPE_FLAG_ATOMIC | AST_TYPE_FLAG_VLA)) &&
	       !codegen_type_is_bitint(c, t) && (codegen_type_is_pointer(c, t) || codegen_type_is_integer(c, t)) &&
	       (size == 4 || size == 8);
}

/* the caller's options and attributes allow sibling calls */
VPRIV bool em_tail_caller_ok(codegen_context *e, uint32_t fn)
{
	return e->unit->options->opt && !e->unit->cleanups_disabled && e->unit->options->sibling_calls >= 0 && !e->unit->options->instrument &&
	       !codegen_symbol_attributes(e->compile_context, fn)->interrupt &&
	       !(codegen_symbol_attributes(e->compile_context, fn)->flags & (CC_ATTRIBUTE_FLAG_TAIL_DISABLE | CC_ATTRIBUTE_FLAG_TAIL_RETURNS_TWICE | CC_ATTRIBUTE_FLAG_OPTNONE));
}

/* function s may be a sibling call's target (as for its attributes) */
VPRIV bool em_tail_target_ok(codegen_context *e, uint32_t s)
{
	cc *c = e->compile_context;
	const sym *target = &c->syms[s];

	if (target->kind != AST_SYMBOL_FUNCTION || (target->flags & AST_SYMBOL_FLAG_NORETURN) ||
	    codegen_symbol_attributes(c, s)->error_fn || codegen_symbol_attributes(c, s)->ifunc || codegen_symbol_attributes(c, s)->interrupt ||
	    (codegen_symbol_attributes(c, s)->flags & (CC_ATTRIBUTE_FLAG_TAIL_RETURNS_TWICE | CC_ATTRIBUTE_FLAG_TAIL_NOT_CALLED)))
		return false;
	/* The standard nonlocal-jump entry points are special even when a caller
	   supplies a plain prototype without the usual returns_twice attribute. */
	return !(CODEGEN_IDENTIFIER_IS(target->name, "setjmp") || CODEGEN_IDENTIFIER_IS(target->name, "_setjmp") ||
		 CODEGEN_IDENTIFIER_IS(target->name, "sigsetjmp") || CODEGEN_IDENTIFIER_IS(target->name, "__sigsetjmp"));
}

/* The target must separately reject unsupported ABI/frame states. Capability
   is deliberately absent for default WebAssembly, whose MVP has no tail call. */
VPRIV bool em_tail_candidate(codegen_context *e, uint32_t fn, uint32_t expr, uint32_t maxargs, uint32_t *callee)
{
	cc *c = e->compile_context;
	const sym *caller = &c->syms[fn];
	const ctype *ft = T(c, caller->ty);
	const node *body = N(caller->body), *ret, *call, *fdec, *name;
	const sym *target;
	const ctype *ct;
	uint32_t s;

	if (!em_tail_caller_ok(e, fn) || !expr || body->op != AST_NODE_BLOCK || body->c != 1 || body->aux > maxargs ||
	    (ft->flags & (AST_TYPE_FLAG_VARIADIC | AST_TYPE_FLAG_NO_PROTOTYPE)))
		return false;
	ret = N(c->lists[body->b]);
	if (ret->op != AST_NODE_RETURN || ret->a != expr || N(expr)->op != AST_NODE_CALL)
		return false;
	call = N(expr);
	if (call->c > maxargs || AST_QUALIFIED_TYPE_UNQUALIFIED(call->ty) != AST_QUALIFIED_TYPE_UNQUALIFIED(ft->base) ||
	    (KIND(c, ft->base) != AST_TYPE_VOID && !em_tail_word(c, ft->base)))
		return false;
	/* Exactly the function-to-pointer decay, with no function-pointer casts. */
	fdec = N(call->a);
	if (fdec->op != AST_NODE_CAST || fdec->aux != AST_CAST_FUNCTION_DECAY || N(fdec->a)->op != AST_NODE_VARIABLE)
		return false;
	name = N(fdec->a);
	s = codegen_symbol_canonical(c, name->a);
	target = &c->syms[s];
	if ((target->flags & (AST_SYMBOL_FLAG_ALWAYS_INLINE | AST_SYMBOL_FLAG_INLINE)) || !em_tail_target_ok(e, s))
		return false;
	ct = T(c, target->ty);
	if (ct->kind != AST_TYPE_FUNCTION || (ct->flags & (AST_TYPE_FLAG_VARIADIC | AST_TYPE_FLAG_NO_PROTOTYPE)) ||
	    ct->n != call->c || AST_QUALIFIED_TYPE_UNQUALIFIED(ct->base) != AST_QUALIFIED_TYPE_UNQUALIFIED(ft->base))
		return false;
	for (uint32_t i = 0; i < body->aux; i++) {
		uint32_t p = c->lists[body->a + i];
		const sym *param = &c->syms[p];

		/* No parameter's address, cleanup, volatile access or dynamic type may
		   outlive this frame. There are no local declarations in this body. */
		if (!em_tail_word(c, param->ty) || codegen_type_is_variably_modified(c, param->ty) ||
		    !em_local_eligible(e, p))
			return false;
	}
	for (uint32_t i = 0; i < call->c; i++) {
		const node *a = N(c->lists[call->b + i]);
		ast_qualified_type pt = c->params[ct->aux + i];
		bool formal = false;

		if (!em_tail_word(c, pt) || AST_QUALIFIED_TYPE_UNQUALIFIED(a->ty) != AST_QUALIFIED_TYPE_UNQUALIFIED(pt))
			return false;
		if (a->op == AST_NODE_NUMBER || (a->op == AST_NODE_UNARY && a->aux == LEX_TOKEN_MINUS && N(a->a)->op == AST_NODE_NUMBER))
			continue;
		if (a->op != AST_NODE_VARIABLE)
			return false;
		for (uint32_t j = 0; j < body->aux; j++)
			formal |= a->a == c->lists[body->a + j];
		if (!formal)
			return false;
	}
	*callee = s;
	return true;
}
