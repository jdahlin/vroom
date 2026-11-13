/* vcc front end: the __builtin_object_size analysis; see builtin-table.c for where a builtin's other facts live */
#include "ccimpl.h"

/* Object-size queries never evaluate their pointer operand. Track an address's
   enclosing allocation and nearest subobject independently, so modes 0/2 and
   1/3 retain their distinct bounds through casts and pointer arithmetic. */
static uint64_t object_init_extent(cc *c, qty ty, uint32_t init, unsigned depth)
{
	uint64_t size = ty_size(c, ty);
	if (!init || depth > 64)
		return size;
	const node *x = N(init);
	if (is_array(c, ty) && x->op == N_STR) {
		const cstr *s = &c->strs[x->a];
		uint64_t n = (uint64_t)s->n * s->esz;
		return (T(c, ty)->flags & TF_INCOMPLETE) && n > size ? n : size;
	}
	if (x->op != N_INIT)
		return size;
	const uint32_t *v = &c->lists[x->b];
	if (x->aux == 1) {
		if (!(T(c, ty)->flags & TF_INCOMPLETE))
			return size;
		uint64_t es = ty_size(c, T(c, ty)->base);
		for (uint32_t i = 0; i < x->c; i++) {
			uint64_t end = ((uint64_t)v[3 * i + 1] + 1) * es;
			if (end > size) size = end;
		}
	} else if (is_record(c, ty)) {
		const tag *r = &c->tags[T(c, ty)->aux];
		for (uint32_t i = 0; i < x->c && i < r->nfield; i++) {
			const field *f = &r->f[i];
			if (!v[i] || f->bitfield || (x->aux == 2 && i != x->a))
				continue;
			uint64_t end = f->off / 8 + object_init_extent(c, f->ty, v[i], depth + 1);
			if (end > size) size = end;
		}
	}
	return size;
}

static uint64_t object_element_size(cc *c, qty pointer)
{
	qty base = T(c, pointer)->base;
	return KIND(c, base) == TY_VOID || KIND(c, base) == TY_FUNC ? 1 : ty_size(c, base);
}

/* Only the last array member can use the historical struct-hack bound. */
static bool object_flexible_member(cc *c, const node *x)
{
	qty t = node_field(c, x)->ty;
	if (!is_array(c, t) || x->c + 1 != c->tags[x->b].nfield)
		return false;
	if (T(c, t)->flags & TF_INCOMPLETE)
		return true;
	uint64_t n = arr_len(c, t);
	return c->o->strict_flex_arrays == 0 ||
	       (c->o->strict_flex_arrays == 1 && n <= 1) ||
	       (c->o->strict_flex_arrays == 2 && n == 0);
}


static bool object_lvalue_bound(cc *c, uint32_t n, object_bound *b, unsigned depth, unsigned mode)
{
	if (!n || depth > 64)
		return false;
	const node *x = N(n);
	cval index;
	switch (x->op) {
	case N_VAR:
	case N_COMPLIT: {
		const sym *s = &c->syms[x->op == N_VAR ? x->a : x->b];
		if (s->kind != S_VAR || (is_array(c, s->ty) &&
		    ((T(c, s->ty)->flags & TF_VLA) || ((T(c, s->ty)->flags & TF_INCOMPLETE) && !s->body))))
			return false;
		*b = (object_bound){ .whole = object_init_extent(c, s->ty, s->body, 0), .storage = s->ty };
		b->sub = b->whole;
		return true;
	}
	case N_STR:
		*b = (object_bound){ .whole = (uint64_t)c->strs[x->a].n * c->strs[x->a].esz };
		b->sub = b->whole;
		return true;
	case N_MEMBER: {
		const field *f = node_field(c, x);
		if (f->bitfield)
			return false;
		bool flexible = object_flexible_member(c, x);
		if (!object_lvalue_bound(c, x->a, b, depth + 1, mode)) {
			/* The array's declared subobject bound is independent of the
			   unknown enclosing allocation. Do not invent a whole bound. */
			if (!is_array(c, f->ty) || (T(c, f->ty)->flags & TF_INCOMPLETE) || (flexible && mode != 3))
				return false;
			*b = (object_bound){ .whole = -1, .sub = ty_size(c, f->ty), .storage = f->ty };
			return true;
		}
		b->offset += f->off / 8;
		/* Clang treats a trailing array as extending through its enclosing
		   allocation, even when a fixed bound is written (the struct hack).
		   A nested record's endpoint was retained by the enclosing member. */
		/* A declared record object (including an element of a declared
		   array) does not give its trailing member the record's padding.
		   Raw allocations/reinterpreted storage still permit the struct hack. */
		if (mode == 3 || !flexible || (b->storage && !(T(c, f->ty)->flags & TF_INCOMPLETE)))
			b->sub = b->offset + ty_size(c, f->ty);
		b->storage = f->ty;
		return true;
	}
	case N_INDEX:
		if (!object_pointer_bound(c, x->a, b, depth + 1, mode) || !eval(c, x->b, &index) || index.kind != CV_INT)
			return false;
		b->offset += index.i * object_element_size(c, N(x->a)->ty);
		if (is_record(c, x->ty) && b->storage &&
		    (QUNQ(b->storage) == QUNQ(x->ty) ||
		     (is_array(c, b->storage) && QUNQ(T(c, b->storage)->base) == QUNQ(x->ty))))
			b->sub = b->offset + ty_size(c, x->ty);
		if (b->storage && is_array(c, b->storage))
			b->storage = T(c, b->storage)->base;
		return true;
	case N_DEREF:
		return object_pointer_bound(c, x->a, b, depth + 1, mode);
	default:
		return false;
	}
}

VPRIV bool object_pointer_bound(cc *c, uint32_t n, object_bound *b, unsigned depth, unsigned mode)
{
	if (!n || depth > 64)
		return false;
	const node *x = N(n);
	cval index;
	if (x->op == N_VAR && (c->syms[x->a].flags & SF_BOS_INIT))
		return object_pointer_bound(c, c->syms[x->a].body, b, depth + 1, mode);
	if (x->op == N_CALL && !c->o->no_builtin) {
		uint32_t callee = x->a;
		while (N(callee)->op == N_CAST || N(callee)->op == N_ADDR)
			callee = N(callee)->a;
		if (N(callee)->op != N_VAR)
			return false;
		const sym *s = &c->syms[N(callee)->a];
		if (s->kind != S_FUNC || !(s->flags & SF_GLOBAL) || s->sc == SC_STATIC || s->asmname ||
		    lib_builtin_disabled(c->o, idstr_(s->name), idlen_(s->name)))
			return false;
		const char *name = idstr_(s->name);
		uint32_t ai = 0;
		bool multiply = false;
		if (idlen_(s->name) == 6 && !memcmp(name, "malloc", 6)) { }
		else if (idlen_(s->name) == 6 && !memcmp(name, "calloc", 6)) multiply = true;
		else if (idlen_(s->name) == 7 && !memcmp(name, "realloc", 7)) ai = 1;
		else return false;
		const ctype *ft = T(c, s->ty);
		uint32_t nargs = multiply || ai ? 2 : 1;
		if (ft->kind != TY_FUNC || (ft->flags & (TF_NOPROTO | TF_VARIADIC)) ||
		    ft->n != nargs || x->c != nargs || !is_ptr(c, ft->base) || !is_void(c, T(c, ft->base)->base))
			return false;
		for (uint32_t i = 0; i < nargs; i++) {
			qty p = c->params[ft->aux + i];
			if (ai && i == 0 ? (!is_ptr(c, p) || !is_void(c, T(c, p)->base)) : QUNQ(p) != QUNQ(c->t_size))
				return false;
		}
		if (!eval(c, c->lists[x->b + ai], &index) || index.kind != CV_INT || index.i < 0)
			return false;
		u128 size = (u128)index.i;
		if (multiply) {
			if (!eval(c, c->lists[x->b + 1], &index) || index.kind != CV_INT || index.i < 0)
				return false;
			size *= (u128)index.i;
		}
		if (size > (ty_size(c, c->t_size) == 4 ? UINT32_MAX : UINT64_MAX))
			return false;
		*b = (object_bound){ .whole = (i128)size, .sub = (i128)size };
		return true;
	}
	if (x->op == N_ADDR || (x->op == N_CAST && x->aux == CK_DECAY))
		return object_lvalue_bound(c, x->a, b, depth + 1, mode);
	if (x->op == N_CAST && is_ptr(c, N(x->a)->ty)) {
		if (!object_pointer_bound(c, x->a, b, depth + 1, mode))
			return false;
		if (is_ptr(c, x->ty) && is_record(c, T(c, x->ty)->base)) {
			qty base = T(c, x->ty)->base;
			if (!b->storage || (QUNQ(b->storage) != QUNQ(base) &&
			    !(is_array(c, b->storage) && QUNQ(T(c, b->storage)->base) == QUNQ(base)))) {
				b->sub = b->whole;
				b->storage = 0;
			}
		}
		return true;
	}
	if (is_array(c, x->ty))
		return object_lvalue_bound(c, n, b, depth + 1, mode);
	if (x->op == N_BINARY && (x->aux == T_PLUS || x->aux == T_MINUS)) {
		uint32_t p = x->a, i = x->b;
		if (!is_ptr(c, N(p)->ty) && x->aux == T_PLUS) p = x->b, i = x->a;
		if (!is_ptr(c, N(p)->ty) || !object_pointer_bound(c, p, b, depth + 1, mode) ||
		    !eval(c, i, &index) || index.kind != CV_INT)
			return false;
		b->offset += (x->aux == T_MINUS ? -index.i : index.i) * object_element_size(c, N(p)->ty);
		return true;
	}
	return false;
}

bool cc_object_size(cc *c, uint32_t pointer, unsigned mode, i128 *size)
{
	object_bound b;
	if (mode > 3 || !object_pointer_bound(c, pointer, &b, 0, mode)) return false;
	i128 end = (mode & 1) && (b.whole < 0 || b.sub < b.whole) ? b.sub : b.whole;
	if (end < 0) return false;
	*size = b.offset < 0 || b.offset > end ? 0 : end - b.offset;
	return true;
}
