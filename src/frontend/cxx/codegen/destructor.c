/* C++ code generation: destructors' bodies, the destructor a class has in effect, destroying objects */
#include "codegen.h"

/*
 * A destructor with no body of its own gets one: one defaulted in its class (~T() = default), or the
 * implicit one of a class that declares none though its members and bases need destruction (made here: an
 * entity in the class like a declared one).  Its body is empty; cxx_gen_function_end adds the members'
 * and bases' destruction.  NULL: not built.
 */
VPRIV cxx_entity *cxx_gen_destructor_body(cxx *c, qty t, cxx_entity *d)
{
	cc *a = c->ast;
	cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t)), *fs;
	uint32_t body;

	if (!cls || !cls->owner || cls->dependent || !a->o->emit || c->tentative || c->unevaluated)
		return NULL;
	if (!d) {
		d = cxx_declare(c, cls, cxx_dtor_id(), CXX_ENTITY_VALUE);
		if (!d)
			return NULL;
		d->ty = cxx_type_function(c, cc_type_basic(TY_VOID), NULL, 0, false);
		d->member_of = cls;
		d->fn_defaulted = 1, d->fn_implicit = 1, d->fn_inline = 1;
		cxx_vtable_declare(c, cls, d, false);	/* (it overrides a base's virtual destructor) */
	}
	if (d->gen_tried || d->fn_deleted)
		return d->sym && (a->syms[d->sym].flags & SF_DEFINED) ? d : d->gen_tried == 2 ? d : NULL;
	d->gen_tried = 2;	/* (being built: a member of its own type further in asks again) */
	if (!d->fn_ooc_default)
		d->fn_inline = 1;	/* (X::~X() = default; outside the class: not inline, the TU's own definition) */
	fs = cxx_scope_new(c, CXX_SCOPE_BLOCK, cls, NULL);
	if (!cxx_gen_function_begin(c, d, fs, NULL, 0)) {
		d->gen_tried = 1;
		return NULL;
	}
	body = cc_statement_new(a, N_BLOCK, 0, 0, 0, 0);
	cxx_gen_function_end(c, d, body);
	d->gen_tried = 1;
	return d->sym && (a->syms[d->sym].flags & SF_DEFINED) ? d : NULL;
}

/*
 * The destructor that destroying an object of the class runs: the one it declares; with none declared,
 * the one of its only base that needs destruction, when that base is at offset 0 and no member needs
 * destruction (the implicit destructor does just that).  NULL: none, or an implicit one not built yet.
 */
VPRIV cxx_entity *cxx_gen_effective_destructor(cxx *c, qty t, int depth)
{
	cc *a = c->ast;
	cxx_entity *d = cxx_gen_declared_destructor(c, t);
	cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t));
	qty bases[16], only = CXX_TY_UNKNOWN;
	uint32_t nb;
	uint64_t off;
	bool members = false;

	if (d && d->fn_defaulted && !d->fn_deleted && !(d->sym && (a->syms[d->sym].flags & SF_DEFINED)))
		return cxx_gen_destructor_body(c, t, d);	/* ~T() = default: its body is made where it is first used */
	if (d || !cls || depth > 8)
		return d;
	if (!cxx_gen_needs_destruction(c, t, 0))
		return NULL;
	for (uint32_t i = 0; i < cls->nfield_types; i++) {
		qty ft = cls->field_types[i];

		while (ft != CXX_TY_UNKNOWN && AST_TYPE_KIND(a, ft) == TY_ARRAY)
			ft = AST_TYPE(a, ft)->base;
		if (ft == CXX_TY_UNKNOWN)
			return NULL;
		members |= cc_type_is_record(a, ft) && cxx_gen_needs_destruction(c, ft, 0);
	}
	nb = cxx_class_bases(c, t, bases, 16);
	for (uint32_t i = 0; i < nb && !members; i++)
		if (cxx_gen_needs_destruction(c, bases[i], 0)) {
			if (only != CXX_TY_UNKNOWN)
				members = true;
			only = bases[i];
		}
	if (!members && only != CXX_TY_UNKNOWN && cxx_class_base_offset(c, t, only, &off, 0) && !off && !cxx_class_polymorphic(c, t))
		return cxx_gen_effective_destructor(c, only, depth + 1);	/* (the base's does all there is to do) */
	return cxx_gen_destructor_body(c, t, NULL);	/* the implicit destructor, made */
}

/* ~T(): the destructor's call on the lvalue x (the class declares it); the statement goes to `out` */
VPRIV bool cxx_gen_destroy(cxx *c, uint32_t x, qty t, cc_id_list *out, uint32_t loc)
{
	cc *a = c->ast;
	cxx_entity *d = cxx_gen_effective_destructor(c, t, 0);

	if (!x)
		return false;
	if (!d)
		return !cxx_gen_needs_destruction(c, t, 0);	/* (an implicit destructor that does something: not built yet) */
	if (d->fn_deleted)
		return false;
	cxx_gen_call(c, d, x, false, NULL, 0, loc);
	if (!c->enode)
		return false;
	cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, c->enode, 0, 0));
	c->enode = 0;
	return true;
}

/* a destructor's body is done: then its members of class type, last first, and its bases (into `after`) */
VPRIV bool cxx_gen_destructor_rest(cxx *c, cc_id_list *after, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t self = cxx_gen_this_object(c, loc);
	qty own, bases[16];
	uint32_t nb;
	const tag *r;

	if (!self)
		return false;
	own = AST_NODE_AT(a, self)->ty;
	r = &a->tags[AST_TYPE(a, own)->aux];
	for (uint32_t i = r->nfield; i-- > 0;) {
		qty ft = r->f[i].ty, et = ft;

		while (AST_TYPE_KIND(a, et) == TY_ARRAY)
			et = AST_TYPE(a, et)->base;
		if (!cc_type_is_record(a, et) || !cxx_gen_needs_destruction(c, et, 0))
			continue;
		if (!r->f[i].name || et != ft)
			return false;	/* (arrays and anonymous members of classes with destructors: not built yet) */
		if (!cxx_gen_destroy(c, member(a, cxx_gen_this_object(c, loc), r->f[i].name, loc), QUNQ(ft), after, loc))
			return false;
	}
	nb = cxx_class_bases(c, own, bases, 16);
	for (uint32_t i = nb; i-- > 0;) {
		if (!cxx_gen_needs_destruction(c, bases[i], 0))
			continue;
		c->gen_base_type = QUNQ(bases[i]);
		if (!cxx_gen_destroy(c, cxx_gen_base_object(c, cxx_gen_this_object(c, loc), bases[i], loc), bases[i], after, loc)) {
			c->gen_base_type = 0;
			return false;
		}
		c->gen_base_type = 0;
	}
	return true;
}

/* x.~T(), p->~T(): the destructor's call (a class with none to run, a scalar: nothing), as an expression */
VPRIV void cxx_gen_destroy_object(cxx *c, uint32_t x, bool arrow, uint32_t loc)
{
	cc *a = c->ast;
	qty t;
	cxx_entity *d;

	c->enode = 0;
	if (!cxx_gen_on(c) || !x)
		return;
	if (arrow) {
		x = cc_node_rvalue(a, x);
		if (AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) != TY_PTR)
			return;
		x = cc_dereference_build(a, x, loc);
	}
	t = QUNQ(AST_NODE_AT(a, x)->ty);
	if (!cc_type_is_record(a, t) || !cxx_gen_needs_destruction(c, t, 0)) {
		c->enode = cc_cast_insert(a, cc_node_integer_new(a, 0, cc_type_basic(TY_INT), loc), cc_type_basic(TY_VOID), CK_VOID);
		return;
	}
	if (!(d = cxx_gen_effective_destructor(c, t, 0)) || cxx_class_polymorphic(c, t))
		return;	/* (a virtual destructor: through the vtable, not built yet) */
	cxx_gen_call(c, d, x, false, NULL, 0, loc);
}

/* ---- virtual functions ----------------------------------------------------------------------- */
