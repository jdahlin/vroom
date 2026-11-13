/*
 * unusedinit.c: do not generate the initializer of an automatic integer that is never referenced.
 *
 * em_unused_init(e, decl, fn, budget) says EM_UNUSED_OMIT (no code at all), EM_UNUSED_EVAL (the
 * initializer has effects: evaluate it, drop the store) or EM_UNUSED_KEEP.  Omission needs a proof:
 * the local is a plain automatic integer with no cleanup, never named, address not taken, and the
 * initializer is pure within the budget (loads through addresses, division, shifts, floating point
 * and builtins are deliberately not accepted).  Brace and string initializers belong to the
 * initializer walker and are kept.  Called by the shared walker, arm64's and wasm's statement code.
 */
#include "../codegen-internal.h"

static bool em_unused_int(cc *c, ast_qualified_type t)
{
	return codegen_type_is_integer(c, t) && !codegen_type_is_bitint(c, t) && !codegen_type_is_wide(c, t) && codegen_type_size(c, t) - 1 < 8 &&
	       !(AST_QUALIFIED_TYPE_QUALIFIERS(t) & AST_QUALIFIER_VOLATILE) && !(T(c, t)->flags & AST_TYPE_FLAG_ATOMIC);
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
	case AST_NODE_NUMBER:
		return true;
	case AST_NODE_VARIABLE:
		return codegen_symbol_is_local_variable(c, x->a) && em_unused_int(c, c->syms[x->a].ty) &&
		       c->syms[x->a].sc != AST_STORAGE_STATIC && c->syms[x->a].sc != AST_STORAGE_EXTERN &&
		       !codegen_symbol_attributes(c, x->a)->asmname;
	case AST_NODE_CAST:
		if (x->aux != AST_CAST_NOP && x->aux != AST_CAST_INTEGER && x->aux != AST_CAST_BOOL)
			return false;
		return em_unused_pure(c, x->a, left);
	case AST_NODE_UNARY:
		if (x->aux != LEX_TOKEN_PLUS && x->aux != LEX_TOKEN_MINUS && x->aux != LEX_TOKEN_TILDE && x->aux != LEX_TOKEN_EXCLAMATION)
			return false;
		return em_unused_pure(c, x->a, left);
	case AST_NODE_BINARY:
		switch (x->aux) {
		case LEX_TOKEN_PLUS: case LEX_TOKEN_MINUS: case LEX_TOKEN_STAR: case LEX_TOKEN_AMPERSAND: case LEX_TOKEN_PIPE: case LEX_TOKEN_CARET:
		case LEX_TOKEN_LESS: case LEX_TOKEN_LESS_EQUAL: case LEX_TOKEN_GREATER: case LEX_TOKEN_GREATER_EQUAL:
		case LEX_TOKEN_EQUAL_EQUAL: case LEX_TOKEN_EXCLAMATION_EQUAL:
			break;
		default:
			return false;
		}
		__attribute__((fallthrough));
	case AST_NODE_LOGICAL_AND: case AST_NODE_LOGICAL_OR: case AST_NODE_COMMA:
		return em_unused_pure(c, x->a, left) && em_unused_pure(c, x->b, left);
	case AST_NODE_CONDITIONAL:
		return em_unused_pure(c, x->a, left) && em_unused_pure(c, x->b, left) &&
		       em_unused_pure(c, x->c, left);
	default:
		return false;
	}
}

VPRIV bool em_unused_local(codegen_context *e, uint32_t s, uint32_t fn)
{
	cc *c = e->c;
	const sym *y;
	const attrs *a;

	if (!e->E->o->opt || e->E->noopt || (e->E->sflags[s] & CODEGEN_SYMBOL_USED))
		return false;
	/* fn is supplied by the backend/selector; top also protects an optnone
	 * caller while a different function body is being inlined. */
	if ((fn && (codegen_symbol_attributes(c, fn)->flags & CC_ATTRIBUTE_FLAG_OPTNONE)) ||
	    (e->top && (codegen_symbol_attributes(c, e->top)->flags & CC_ATTRIBUTE_FLAG_OPTNONE)))
		return false;
	y = &c->syms[s];
	a = codegen_symbol_attributes(c, s);
	return codegen_symbol_is_local_variable(c, s) && y->kind == AST_SYMBOL_VARIABLE && y->sc != AST_STORAGE_STATIC &&
	       y->sc != AST_STORAGE_EXTERN && y->sc != AST_STORAGE_TYPEDEF && !(y->flags & AST_SYMBOL_FLAG_ADDRESS_TAKEN) &&
	       em_unused_int(c, y->ty) && !codegen_type_is_variably_modified(c, y->ty) && !a->cleanup &&
	       !a->used && !a->asmname;
}

VPRIV unsigned em_unused_init(codegen_context *e, const node *decl, uint32_t fn, unsigned budget)
{
	cc *c = e->c;

	if (!em_unused_local(e, decl->a, fn))
		return EM_UNUSED_KEEP;
	if (!decl->b)
		return EM_UNUSED_OMIT;
	/* Brace/zero initializer records belong to the initializer walker, not
	 * the value emitter. Keep that path rather than treating them as values. */
	if (N(decl->b)->op == AST_NODE_INITIALIZER || N(decl->b)->op == AST_NODE_ZERO || N(decl->b)->op == AST_NODE_STRING)
		return EM_UNUSED_KEEP;
	/* Evaluating a value rather than an effects-only expression preserves
	 * internal conversions and traps. The final integer conversion is unused. */
	if (!em_unused_int(c, N(decl->b)->ty))
		return EM_UNUSED_KEEP;
	return em_unused_pure(c, decl->b, &budget) ? EM_UNUSED_OMIT : EM_UNUSED_EVAL;
}
