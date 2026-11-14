/*
 * expression-queries.c: small questions about the shape of an expression that more than one
 * backend asks.
 *
 * codegen_call_direct_callee finds the callee symbol of a call through casts, and returns 0 for builtins (setting
 * *ref to their name reference) and for calls through a cast to a different function type, which
 * must stay indirect.  Both functions are used by the walker, arm64's call lowering and the
 * prepass; they read the AST only.
 */
#include "codegen-internal.h"

VPRIV bool codegen_type_is_pointer_to_vla(cc *c, ast_qualified_type ptr)
{
	const ctype *b = T(c, T(c, ptr)->base);

	return b->kind == AST_TYPE_ARRAY && (b->flags & AST_TYPE_FLAG_VLA);
}

/* the callee sym of a direct call, or 0 */
VPRIV uint32_t codegen_call_direct_callee(cc *c, uint32_t fn, uint32_t *ref)
{
	const node *f = N(fn);

	while (f->op == AST_NODE_CAST && (f->aux == AST_CAST_FUNCTION_DECAY || f->aux == AST_CAST_NOP || f->aux == AST_CAST_POINTER_TO_POINTER) &&
	       (N(f->a)->op == AST_NODE_VARIABLE || N(f->a)->op == AST_NODE_CAST))
		f = N(f->a);
	if (f->op != AST_NODE_VARIABLE)
		return 0;
	if (c->syms[f->a].kind == AST_SYMBOL_BUILTIN) {
		*ref = CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_NAME, (uint32_t)c->syms[f->a].val);
		return 0;
	}
	if (c->syms[f->a].kind != AST_SYMBOL_FUNCTION)
		return 0;
	/* calls through a cast to another function type stay direct only if the types agree */
	if (N(fn)->op == AST_NODE_CAST && N(N(fn)->a)->op == AST_NODE_CAST)
		return 0;
	*ref = CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_SYMBOL, codegen_symbol_canonical(c, f->a));
	return codegen_symbol_canonical(c, f->a);
}
