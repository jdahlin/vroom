/* vcc front end: constants and object sizes folded in a function body */
#include "cc-internal.h"

/* A deliberately conservative provenance pass, only for functions that ask
   about object sizes. An unaliased automatic pointer never assigned after its
   initializer has the initializer's bound. Labels/cases can bypass an
   initializer; assembly can write operands without an AST_NODE_ASSIGN node. Leave
   those functions unknown until a full control-flow analysis handles them. */
CC_PRIVATE void cc_function_object_size_locals(cc *c, uint32_t sym0, uint32_t node0)
{
	for (uint32_t n = node0; n < c->nnode; n++) {
		int op = AST_NODE(n)->op;
		if (op == AST_NODE_LABEL || op == AST_NODE_CASE || op == AST_NODE_DEFAULT || op == AST_NODE_GOTO ||
		    op == AST_NODE_GOTO_INDIRECT || op == AST_NODE_ASM)
			return;
	}
	for (uint32_t i = sym0; i < c->nsym; i++) {
		sym *s = &c->syms[i];
		if (s->kind == AST_SYMBOL_VARIABLE && cc_type_is_pointer(c, s->ty) && s->body &&
		    !(s->flags & (AST_SYMBOL_FLAG_GLOBAL | AST_SYMBOL_FLAG_PARAMETER | AST_SYMBOL_FLAG_ADDRESS_TAKEN | AST_SYMBOL_FLAG_INVALID)) &&
		    s->sc != AST_STORAGE_STATIC && !s->asmname && !(AST_QUALIFIED_TYPE_QUALIFIERS(s->ty) & AST_QUALIFIER_VOLATILE))
			s->flags |= AST_SYMBOL_FLAG_OBJECT_SIZE_INITIALIZER;
	}
	for (uint32_t n = node0; n < c->nnode; n++) {
		const node *x = AST_NODE(n);
		if (x->op == AST_NODE_ASSIGN || x->op == AST_NODE_PRE_INCREMENT || x->op == AST_NODE_PRE_DECREMENT ||
		    x->op == AST_NODE_POST_INCREMENT || x->op == AST_NODE_POST_DECREMENT) {
			const node *a = AST_NODE(x->a);
			if (a->op == AST_NODE_VARIABLE)
				c->syms[a->a].flags &= ~AST_SYMBOL_FLAG_OBJECT_SIZE_INITIALIZER;
		}
	}
}

/* Post-body folding deliberately uses a separate evaluator: immutable automatic
   objects are optimization facts, never C integer/initializer constants. No
   shared evaluator state or symbol qualifiers are changed. */
static bool cc_function_local_is_constant(cc *c, uint32_t n, const uint8_t *stable, uint32_t nsym,
                           unsigned depth, unsigned *budget, cc_value *v)
{
	if (!n || depth > 64 || !*budget) return false;
	--*budget;
	node x = *AST_NODE(n);
	if ((AST_QUALIFIED_TYPE_QUALIFIERS(x.ty) & AST_QUALIFIER_VOLATILE) || (AST_TYPE(c, x.ty)->flags & AST_TYPE_FLAG_ATOMIC)) return false;
	switch (x.op) {
	case AST_NODE_NUMBER: case AST_NODE_FLOAT_NUMBER: case AST_NODE_ZERO:
		return cc_eval(c, n, v) && (v->kind == CC_VALUE_INTEGER || (v->kind == CC_VALUE_FLOAT && !v->ld));
	case AST_NODE_VARIABLE:
		return x.a < nsym && stable[x.a] && c->syms[x.a].body < n &&
		       cc_function_local_is_constant(c, c->syms[x.a].body, stable, nsym, depth + 1, budget, v);
	case AST_NODE_CAST: case AST_NODE_UNARY: case AST_NODE_BINARY: {
		cc_value a = {0}, b = {0};
		if (!cc_function_local_is_constant(c, x.a, stable, nsym, depth + 1, budget, &a)) return false;
		if (x.op == AST_NODE_BINARY && !cc_function_local_is_constant(c, x.b, stable, nsym, depth + 1, budget, &b)) return false;
		x.a = a.kind == CC_VALUE_INTEGER ? cc_node_integer_new(c, a.i, AST_NODE(x.a)->ty, x.loc) : cc_node_float_new(c, a.f, AST_NODE(x.a)->ty, x.loc);
		if (x.op == AST_NODE_BINARY)
			x.b = b.kind == CC_VALUE_INTEGER ? cc_node_integer_new(c, b.i, AST_NODE(x.b)->ty, x.loc) : cc_node_float_new(c, b.f, AST_NODE(x.b)->ty, x.loc);
		uint32_t tmp = cc_node_new(c, x.op, x.ty, x.loc); *AST_NODE(tmp) = x;
		return cc_eval(c, tmp, v) && (v->kind == CC_VALUE_INTEGER || (v->kind == CC_VALUE_FLOAT && !v->ld));
	}
	case AST_NODE_CALL: {
		if (!c->o->fast_math || AST_TYPE_KIND(c, x.ty) != AST_TYPE_DOUBLE) return false;
		uint32_t f = x.a;
		while (AST_NODE(f)->op == AST_NODE_CAST) f = AST_NODE(f)->a;
		if (AST_NODE(f)->op != AST_NODE_VARIABLE || c->syms[AST_NODE(f)->a].kind != AST_SYMBOL_BUILTIN) return false;
		const char *name = cc_builtin_name_get(c->syms[AST_NODE(f)->a].val);
		bool logarithm = !strcmp(name, "__builtin_log2") && x.c == 1;
		bool power = !strcmp(name, "__builtin_pow") && x.c == 2;
		if (!logarithm && !power) return false;
		cc_value a = {0}, b = {0};
		if (!cc_function_local_is_constant(c, c->lists[x.b], stable, nsym, depth + 1, budget, &a) || a.kind != CC_VALUE_FLOAT) return false;
		union { double f; uint64_t u; } bits = { .f = a.f };
		v->kind = CC_VALUE_FLOAT; v->ld = false; v->ice = false;
		if (logarithm) {
			/* Exact normal powers of two: no rounding, errno, or host libm. */
			unsigned exponent = (unsigned)(bits.u >> 52);
			if ((bits.u & ((UINT64_C(1) << 52) - 1)) || exponent == 0 || exponent >= 2047) return false;
			v->f = (double)((int)exponent - 1023);
			return true;
		}
		if (a.f != 2.0 || !cc_function_local_is_constant(c, c->lists[x.b + 1], stable, nsym, depth + 1, budget, &b) || b.kind != CC_VALUE_FLOAT ||
		    !(b.f >= -1022 && b.f <= 1023) || b.f != (int)b.f) return false;
		bits.u = (uint64_t)((int)b.f + 1023) << 52;
		v->f = bits.f;
		return true;
	}
	default:
		return false;
	}
}

CC_PRIVATE void cc_function_body_fold(cc *c, uint32_t sym0, uint32_t node0)
{
	if (!c->o->opt) return;
	uint32_t end = c->nnode, nsym = c->nsym;
	bool query = false;
	for (uint32_t n = node0; n < end; n++) {
		const node *x = AST_NODE(n);
		/* Such edges can bypass initializers; asm can hide writes. */
		if (x->op == AST_NODE_LABEL || x->op == AST_NODE_CASE || x->op == AST_NODE_DEFAULT ||
		    x->op == AST_NODE_GOTO || x->op == AST_NODE_GOTO_INDIRECT || x->op == AST_NODE_ASM) return;
		if ((x->op == AST_NODE_BUILTIN && x->aux == CC_BUILTIN_CONSTANT_P) || (c->o->fast_math && x->op == AST_NODE_CALL)) query = true;
	}
	if (!query) return;
	uint8_t *stable = calloc(nsym, 1);
	if (!stable) return;
	for (uint32_t i = sym0; i < nsym; i++) {
		const sym *s = &c->syms[i];
		if (s->kind == AST_SYMBOL_VARIABLE && (cc_type_is_integer(c, s->ty) || AST_TYPE_KIND(c, s->ty) == AST_TYPE_DOUBLE || AST_TYPE_KIND(c, s->ty) == AST_TYPE_FLOAT) && s->body &&
		    !(s->flags & (AST_SYMBOL_FLAG_GLOBAL | AST_SYMBOL_FLAG_PARAMETER | AST_SYMBOL_FLAG_ADDRESS_TAKEN | AST_SYMBOL_FLAG_INVALID)) && s->sc != AST_STORAGE_STATIC && !s->asmname &&
		    !(AST_QUALIFIED_TYPE_QUALIFIERS(s->ty) & AST_QUALIFIER_VOLATILE) && !(AST_TYPE(c, s->ty)->flags & AST_TYPE_FLAG_ATOMIC)) stable[i] = 1;
	}
	for (uint32_t n = node0; n < end; n++) {
		const node *x = AST_NODE(n);
		if (x->op == AST_NODE_ASSIGN || x->op == AST_NODE_PRE_INCREMENT || x->op == AST_NODE_PRE_DECREMENT || x->op == AST_NODE_POST_INCREMENT || x->op == AST_NODE_POST_DECREMENT) {
			const node *a = AST_NODE(x->a);
			if (a->op == AST_NODE_VARIABLE && a->a < nsym) stable[a->a] = 0;
		}
	}
	for (uint32_t n = node0; n < end; n++) {
		node x = *AST_NODE(n);
		bool constant_p = x.op == AST_NODE_BUILTIN && x.aux == CC_BUILTIN_CONSTANT_P && x.c == 1;
		if (!constant_p && x.op != AST_NODE_CALL) continue;
		cc_value v = {0};
		unsigned budget = 512;
		uint32_t nn = c->nnode, ni = c->nipool, nf = c->nfpool;
		bool known = cc_function_local_is_constant(c, constant_p ? c->lists[x.b] : n, stable, nsym, 0, &budget, &v);
		/* All scratch expressions/pool entries are private to this query. */
		c->nnode = nn; c->nipool = ni; c->nfpool = nf;
		if (!known) continue;
		uint32_t folded = constant_p ? cc_node_integer_new(c, 1, x.ty, x.loc) : cc_node_float_new(c, v.f, x.ty, x.loc);
		*AST_NODE(n) = *AST_NODE(folded);
	}
	free(stable);
}
