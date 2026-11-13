/* vcc front end: constants and object sizes folded in a function body */
#include "cc-internal.h"

/* A deliberately conservative provenance pass, only for functions that ask
   about object sizes. An unaliased automatic pointer never assigned after its
   initializer has the initializer's bound. Labels/cases can bypass an
   initializer; assembly can write operands without an N_ASSIGN node. Leave
   those functions unknown until a full control-flow analysis handles them. */
CC_PRIVATE void cc_function_object_size_locals(cc *c, uint32_t sym0, uint32_t node0)
{
	for (uint32_t n = node0; n < c->nnode; n++) {
		int op = AST_NODE(n)->op;
		if (op == N_LABEL || op == N_CASE || op == N_DEFAULT || op == N_GOTO ||
		    op == N_GOTO_IND || op == N_ASM)
			return;
	}
	for (uint32_t i = sym0; i < c->nsym; i++) {
		sym *s = &c->syms[i];
		if (s->kind == S_VAR && cc_type_is_pointer(c, s->ty) && s->body &&
		    !(s->flags & (SF_GLOBAL | SF_PARAM | SF_ADDR_TAKEN | SF_INVALID)) &&
		    s->sc != SC_STATIC && !s->asmname && !(QQ(s->ty) & Q_VOLATILE))
			s->flags |= SF_BOS_INIT;
	}
	for (uint32_t n = node0; n < c->nnode; n++) {
		const node *x = AST_NODE(n);
		if (x->op == N_ASSIGN || x->op == N_PREINC || x->op == N_PREDEC ||
		    x->op == N_POSTINC || x->op == N_POSTDEC) {
			const node *a = AST_NODE(x->a);
			if (a->op == N_VAR)
				c->syms[a->a].flags &= ~SF_BOS_INIT;
		}
	}
}

/* Post-body folding deliberately uses a separate evaluator: immutable automatic
   objects are optimization facts, never C integer/initializer constants. No
   shared evaluator state or symbol qualifiers are changed. */
static bool cc_function_local_is_constant(cc *c, uint32_t n, const uint8_t *stable, uint32_t nsym,
                           unsigned depth, unsigned *budget, cval *v)
{
	if (!n || depth > 64 || !*budget) return false;
	--*budget;
	node x = *AST_NODE(n);
	if ((QQ(x.ty) & Q_VOLATILE) || (AST_TYPE(c, x.ty)->flags & TF_ATOMIC)) return false;
	switch (x.op) {
	case N_NUM: case N_FNUM: case N_ZERO:
		return cc_eval(c, n, v) && (v->kind == CV_INT || (v->kind == CV_FLT && !v->ld));
	case N_VAR:
		return x.a < nsym && stable[x.a] && c->syms[x.a].body < n &&
		       cc_function_local_is_constant(c, c->syms[x.a].body, stable, nsym, depth + 1, budget, v);
	case N_CAST: case N_UNARY: case N_BINARY: {
		cval a = {0}, b = {0};
		if (!cc_function_local_is_constant(c, x.a, stable, nsym, depth + 1, budget, &a)) return false;
		if (x.op == N_BINARY && !cc_function_local_is_constant(c, x.b, stable, nsym, depth + 1, budget, &b)) return false;
		x.a = a.kind == CV_INT ? cc_node_integer_new(c, a.i, AST_NODE(x.a)->ty, x.loc) : cc_node_float_new(c, a.f, AST_NODE(x.a)->ty, x.loc);
		if (x.op == N_BINARY)
			x.b = b.kind == CV_INT ? cc_node_integer_new(c, b.i, AST_NODE(x.b)->ty, x.loc) : cc_node_float_new(c, b.f, AST_NODE(x.b)->ty, x.loc);
		uint32_t tmp = cc_node_new(c, x.op, x.ty, x.loc); *AST_NODE(tmp) = x;
		return cc_eval(c, tmp, v) && (v->kind == CV_INT || (v->kind == CV_FLT && !v->ld));
	}
	case N_CALL: {
		if (!c->o->fast_math || AST_TYPE_KIND(c, x.ty) != TY_DOUBLE) return false;
		uint32_t f = x.a;
		while (AST_NODE(f)->op == N_CAST) f = AST_NODE(f)->a;
		if (AST_NODE(f)->op != N_VAR || c->syms[AST_NODE(f)->a].kind != S_BUILTIN) return false;
		const char *name = cc_builtin_name(c->syms[AST_NODE(f)->a].val);
		bool logarithm = !strcmp(name, "__builtin_log2") && x.c == 1;
		bool power = !strcmp(name, "__builtin_pow") && x.c == 2;
		if (!logarithm && !power) return false;
		cval a = {0}, b = {0};
		if (!cc_function_local_is_constant(c, c->lists[x.b], stable, nsym, depth + 1, budget, &a) || a.kind != CV_FLT) return false;
		union { double f; uint64_t u; } bits = { .f = a.f };
		v->kind = CV_FLT; v->ld = false; v->ice = false;
		if (logarithm) {
			/* Exact normal powers of two: no rounding, errno, or host libm. */
			unsigned exponent = (unsigned)(bits.u >> 52);
			if ((bits.u & ((UINT64_C(1) << 52) - 1)) || exponent == 0 || exponent >= 2047) return false;
			v->f = (double)((int)exponent - 1023);
			return true;
		}
		if (a.f != 2.0 || !cc_function_local_is_constant(c, c->lists[x.b + 1], stable, nsym, depth + 1, budget, &b) || b.kind != CV_FLT ||
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
		if (x->op == N_LABEL || x->op == N_CASE || x->op == N_DEFAULT ||
		    x->op == N_GOTO || x->op == N_GOTO_IND || x->op == N_ASM) return;
		if ((x->op == N_BUILTIN && x->aux == BI_CONSTANT_P) || (c->o->fast_math && x->op == N_CALL)) query = true;
	}
	if (!query) return;
	uint8_t *stable = calloc(nsym, 1);
	if (!stable) return;
	for (uint32_t i = sym0; i < nsym; i++) {
		const sym *s = &c->syms[i];
		if (s->kind == S_VAR && (cc_type_is_integer(c, s->ty) || AST_TYPE_KIND(c, s->ty) == TY_DOUBLE || AST_TYPE_KIND(c, s->ty) == TY_FLOAT) && s->body &&
		    !(s->flags & (SF_GLOBAL | SF_PARAM | SF_ADDR_TAKEN | SF_INVALID)) && s->sc != SC_STATIC && !s->asmname &&
		    !(QQ(s->ty) & Q_VOLATILE) && !(AST_TYPE(c, s->ty)->flags & TF_ATOMIC)) stable[i] = 1;
	}
	for (uint32_t n = node0; n < end; n++) {
		const node *x = AST_NODE(n);
		if (x->op == N_ASSIGN || x->op == N_PREINC || x->op == N_PREDEC || x->op == N_POSTINC || x->op == N_POSTDEC) {
			const node *a = AST_NODE(x->a);
			if (a->op == N_VAR && a->a < nsym) stable[a->a] = 0;
		}
	}
	for (uint32_t n = node0; n < end; n++) {
		node x = *AST_NODE(n);
		bool constant_p = x.op == N_BUILTIN && x.aux == BI_CONSTANT_P && x.c == 1;
		if (!constant_p && x.op != N_CALL) continue;
		cval v = {0};
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
