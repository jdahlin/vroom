/*
 * repeated.c: identities for two reads of the same plain local integer: x - x, x ^ x, x == x,
 * x < x and the like.
 *
 * em_repeated_integer(e, n) returns an EM_ID_* recipe (the result is zero, true, or unknown).  Only
 * for an ordinary automatic variable of 1 to 8 bytes: not static or extern, address never taken, not
 * volatile or atomic, no asm name.  Anything that another thread or a signal handler could change
 * is excluded by those rules.  -O1 and up.  Used by the shared walker, arm64's expression code and
 * wasm's emitter.
 */
#include "../codegen-internal.h"

VPRIV int em_repeated_integer(codegen_context *e, uint32_t n)
{
	cc *c = e->compile_context;
	const node *x = N(n), *a, *b;
	const sym *s;
	ast_qualified_type t;
	uint64_t size;

	if (!e->unit->options->opt || e->unit->cleanups_disabled || x->op != AST_NODE_BINARY ||
	    (x->aux != LEX_TOKEN_MINUS && x->aux != LEX_TOKEN_CARET && x->aux != LEX_TOKEN_AMPERSAND && x->aux != LEX_TOKEN_PIPE &&
	     x->aux != LEX_TOKEN_EQUAL_EQUAL && x->aux != LEX_TOKEN_EXCLAMATION_EQUAL && x->aux != LEX_TOKEN_LESS &&
	     x->aux != LEX_TOKEN_GREATER && x->aux != LEX_TOKEN_LESS_EQUAL && x->aux != LEX_TOKEN_GREATER_EQUAL))
		return EM_ID_NONE;
	a = N(x->a); b = N(x->b);
	if (a->op != AST_NODE_VARIABLE || b->op != AST_NODE_VARIABLE || a->a != b->a || AST_QUALIFIED_TYPE_UNQUALIFIED(a->ty) != AST_QUALIFIED_TYPE_UNQUALIFIED(b->ty) ||
	    !codegen_type_is_integer(c, a->ty) || codegen_type_is_bitint(c, a->ty))
		return EM_ID_NONE;
	s = &c->syms[a->a];
	t = s->ty;
	size = codegen_type_size(c, t);
	if (!codegen_symbol_is_local_variable(c, a->a) || s->kind != AST_SYMBOL_VARIABLE || s->sc == AST_STORAGE_STATIC || s->sc == AST_STORAGE_EXTERN ||
	    (s->flags & AST_SYMBOL_FLAG_ADDRESS_TAKEN) || (AST_QUALIFIED_TYPE_QUALIFIERS(t) & AST_QUALIFIER_VOLATILE) || (T(c, t)->flags & AST_TYPE_FLAG_ATOMIC) ||
	    !size || size > 8 || codegen_symbol_attributes(c, a->a)->asmname)
		return EM_ID_NONE;
	switch (x->aux) {
	case LEX_TOKEN_MINUS: case LEX_TOKEN_CARET: case LEX_TOKEN_EXCLAMATION_EQUAL: case LEX_TOKEN_LESS: case LEX_TOKEN_GREATER:
		return EM_ID_ZERO;
	case LEX_TOKEN_EQUAL_EQUAL: case LEX_TOKEN_LESS_EQUAL: case LEX_TOKEN_GREATER_EQUAL:
		return EM_ID_TRUE;
	default:
		return EM_ID_LEFT;
	}
}
