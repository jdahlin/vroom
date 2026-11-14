/* C++ code generation: which classes need construction or destruction; constructing an object (construct_at), locals */
#include "codegen.h"

/* does the class declare a constructor, or do its bases or members of class type need one called? */
VPRIV bool cxx_gen_needs_construction(cxx *c, qty t, int depth)
{
	cc *a = c->ast;
	cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t));
	qty bases[16];
	uint32_t nb;
	bool pod, empty;

	if (cxx_class_layout_info(c, t, &pod, &empty) && pod)
		return false;
	if (!cls || depth > 8 || cxx_class_polymorphic(c, t) || cxx_class_has_member_initializer(c, t))
		return true;	/* (a vptr to set, members' default initializers) */
	for (cxx_entity *e = cxx_lookup_in(c, cls, cxx_ctor_id(), 0); e; e = e->next)
		if (e->member_of == cls)
			return true;
	nb = cxx_class_bases(c, t, bases, 16);
	for (uint32_t i = 0; i < nb; i++)
		if (cxx_gen_needs_construction(c, bases[i], depth + 1))
			return true;
	for (uint32_t i = 0; i < cls->nfield_types; i++) {
		qty ft = cls->field_types[i];

		while (ft != CXX_TY_UNKNOWN && AST_TYPE_KIND(a, ft) == TY_ARRAY)
			ft = AST_TYPE(a, ft)->base;
		if (ft != CXX_TY_UNKNOWN && cc_type_is_record(a, ft) && cxx_gen_needs_construction(c, ft, depth + 1))
			return true;
	}
	return false;
}

/* a destructor about to be parsed: the destructors its end runs (its members' and bases' classes) have
   their bodies parsed first, not from inside its code generation */
VPRIV void cxx_gen_destructor_parts_use(cxx *c, cxx_entity *d)
{
	cxx_scope *cls = d ? d->member_of : NULL;
	qty bases[16];
	uint32_t nb;

	if (!cls || !cls->owner || cls->owner->ty == CXX_TY_UNKNOWN)
		return;
	for (uint32_t i = 0; i < cls->nfield_types; i++) {
		qty ft = cls->field_types[i];

		while (ft != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, ft) == TY_ARRAY)
			ft = AST_TYPE(c->ast, ft)->base;
		if (ft != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, ft)) {
			cxx_entity *md = cxx_gen_declared_destructor(c, ft);

			if (md && md != d)
				cxx_member_body_use(c, md);
		}
	}
	nb = cxx_class_bases(c, cls->owner->ty, bases, 16);
	for (uint32_t i = 0; i < nb; i++) {
		cxx_entity *bd = cxx_gen_declared_destructor(c, bases[i]);

		if (bd && bd != d)
			cxx_member_body_use(c, bd);
	}
}

/* X::~X() = default; outside its class: its body made now, from the class's members and bases */
VPRIV void cxx_gen_defaulted_destructor(cxx *c, cxx_entity *d)
{
	if (d && d->member_of && d->member_of->owner && d->member_of->owner->ty != CXX_TY_UNKNOWN) {
		d->fn_ooc_default = 1, d->fn_inline = 0;
		cxx_gen_destructor_body(c, d->member_of->owner->ty, d);
	}
}

/* the destructor the class declares (NULL: none) */
VPRIV cxx_entity *cxx_gen_declared_destructor(cxx *c, qty t)
{
	cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t));
	cxx_entity *d = cls ? cxx_lookup_in(c, cls, cxx_dtor_id(), 0) : NULL;

	return d && d->member_of == cls && d->kind == CXX_ENTITY_VALUE ? d : NULL;
}

/* does destroying an object of the class do anything: a declared destructor, here or in a base or member? */
VPRIV bool cxx_gen_needs_destruction(cxx *c, qty t, int depth)
{
	cc *a = c->ast;
	cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t));
	qty bases[16];
	uint32_t nb;
	bool pod, empty;

	if (cxx_class_layout_info(c, t, &pod, &empty) && pod)
		return false;
	if (!cls || depth > 8)
		return true;
	if (cxx_gen_declared_destructor(c, t))
		return true;
	nb = cxx_class_bases(c, t, bases, 16);
	for (uint32_t i = 0; i < nb; i++)
		if (cxx_gen_needs_destruction(c, bases[i], depth + 1))
			return true;
	for (uint32_t i = 0; i < cls->nfield_types; i++) {
		qty ft = cls->field_types[i];

		while (ft != CXX_TY_UNKNOWN && AST_TYPE_KIND(a, ft) == TY_ARRAY)
			ft = AST_TYPE(a, ft)->base;
		if (ft != CXX_TY_UNKNOWN && cc_type_is_record(a, ft) && cxx_gen_needs_destruction(c, ft, depth + 1))
			return true;
	}
	return false;
}

/*
 * An object of class type t at the lvalue `target` is initialized from args: the call of the constructor
 * cxx_ctor_use chose just before, or a copy from an object of the class where none is declared for that,
 * or nothing for a class that needs no construction.  The statements go to `out`.  False: not built yet.
 */
VPRIV bool cxx_gen_default_construct(cxx *c, uint32_t target, qty t, cc_id_list *out, uint32_t loc, int depth);
VPRIV bool cxx_gen_default_construct_parts(cxx *c, uint32_t target, qty t, cc_id_list *out, uint32_t loc, int depth);
/*
 * T(const T&) or T(T&&) that is implicit (or defaulted) and not trivial, done in place: the bases from the
 * source's base subobjects, the vptr, then each member (a class one by its constructor, the others copied).
 * src: the source object (an lvalue).
 */
VPRIV bool cxx_gen_copy_construct_parts(cxx *c, uint32_t target, qty t, uint32_t src, cc_id_list *out, uint32_t loc, int depth)
{
	cc *a = c->ast;
	cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t));
	qty bases[16];
	uint32_t nb;
	const tag *r;

	if (!target || !src || !cls || depth > 8 || AST_TYPE_KIND(a, QUNQ(t)) == TY_UNION || 0)
		return false;
	nb = cxx_class_bases(c, t, bases, 16);
	for (uint32_t i = 0; i < nb; i++) {
		uint32_t tb = cxx_gen_base_object(c, target, bases[i], loc), sb = cxx_gen_base_object(c, src, bases[i], loc);
		qty at[1] = { QT(QI(QUNQ(bases[i])), Q_CONST) };
		uint8_t lv[1] = { 1 };
		bool ok;

		if (!tb || !sb)
			return false;
		cxx_ctor_use(c, QUNQ(bases[i]), at, lv, 1, false);
		c->gen_base_type = QUNQ(bases[i]);	/* (a base subobject: C2) */
		ok = cxx_gen_construct_at(c, tb, QUNQ(bases[i]), &sb, 1, out, loc);
		c->gen_base_type = 0;
		if (!ok)
			return false;
	}
	if (cxx_class_polymorphic(c, t)) {
		uint32_t st = cxx_gen_vptr_store_at(c, cls, cc_address_of_build(a, target, loc), loc);

		if (!st)
			return false;
		cc_id_list_append(a, out, st);
	}
	r = &a->tags[AST_TYPE(a, QUNQ(t))->aux];
	for (uint32_t i = 0; i < r->nfield; i++) {
		qty ft = r->f[i].ty, et = ft;
		uint32_t tm, sm;

		if (!r->f[i].name)
			continue;	/* (the bases' bytes, done above) */
		while (AST_TYPE_KIND(a, et) == TY_ARRAY)
			et = AST_TYPE(a, et)->base;
		tm = member(a, target, r->f[i].name, loc), sm = member(a, src, r->f[i].name, loc);
		if (!tm || !sm)
			return false;
		if (cc_type_is_record(a, et) && cxx_gen_trivial_abi(c, et, 0) != 1) {
			qty at[1] = { QT(QI(QUNQ(ft)), Q_CONST) };
			uint8_t lv[1] = { 1 };

			if (et != ft)
				return false;	/* (an array of such a class: not built yet) */
			cxx_ctor_use(c, QUNQ(ft), at, lv, 1, false);
			if (c->ctor_picked && c->ctor_picked->fn_deleted) {
				lv[0] = 0, at[0] = QUNQ(ft);	/* (a move-only member: moved) */
				cxx_ctor_use(c, QUNQ(ft), at, lv, 1, false);
			}
			if (!cxx_gen_construct_at(c, tm, QUNQ(ft), &sm, 1, out, loc))
				return false;
		} else {
			if (cxx_gen_is_reference(c, ft))
				return false;
			if (QQ(AST_NODE_AT(a, tm)->ty) & (Q_CONST | Q_VOLATILE)) {
				/* (a const member: initialized, through its address as the unqualified type) */
				qty pt = cc_type_pointer(a, QUNQ(AST_NODE_AT(a, tm)->ty));
				uint32_t addr = cc_address_of_build(a, tm, loc);

				tm = cc_dereference_build(a, cc_cast_insert(a, addr, pt, cc_conversion_cast_kind(a, AST_NODE_AT(a, addr)->ty, pt, addr)), loc);
			}
			if (AST_TYPE_KIND(a, ft) == TY_ARRAY) {
				/* (an array of scalars: its bytes, as a struct of the same size would be) */
				uint32_t zargs[3], saved = c->enode;

				zargs[0] = cc_address_of_build(a, tm, loc), zargs[1] = cc_address_of_build(a, sm, loc);
				zargs[2] = cc_node_integer_new(a, (i128)AST_TYPE(a, ft)->size, cc_type_basic(TY_ULONG), loc);
				c->enode = 0;
				cxx_gen_builtin(c, "__builtin_memcpy", 16, zargs, 3, loc);
				if (!c->enode) {
					c->enode = saved;
					return false;
				}
				cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, c->enode, 0, 0));
				c->enode = saved;
				continue;
			}
			cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, tm, cc_node_rvalue(a, sm), loc), 0, 0));
		}
	}
	return true;
}

VPRIV bool cxx_gen_construct_at(cxx *c, uint32_t target, qty t, const uint32_t *args, uint32_t nargs, cc_id_list *out, uint32_t loc)
{
	cc *a = c->ast;
	cxx_entity *ctor = c->ctor_picked;
	bool found = c->ctor_found;
	cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t));

	if (!target || !cls || nargs == UINT32_MAX || nargs > CXX_MAX_ARGS)
		return false;
	uint32_t sliced;
	if (nargs == 1 && args[0] && (AST_NODE_AT(a, args[0])->flags & NF_LVALUE) && cc_type_is_record(a, AST_NODE_AT(a, args[0])->ty) &&
	    AST_TYPE_KIND(a, AST_NODE_AT(a, args[0])->ty) != TY_ARRAY && QUNQ(AST_NODE_AT(a, args[0])->ty) != QUNQ(t)) {
		/* from an object of a class derived from this one (B(d) in D's copy constructor, json_base_class_t(other)):
		   its base subobject, copied or moved as this class's own */
		uint64_t off;

		if (cxx_class_base_offset(c, QUNQ(AST_NODE_AT(a, args[0])->ty), QUNQ(t), &off, 0)) {
			if (!(sliced = cxx_gen_base_object(c, args[0], QUNQ(t), loc)))
				return false;
			args = &sliced;
		}
	}
	if (c->ctor_ilist && c->ctor_ilist->member_of == cls && c->ctor_ilist->ty != CXX_TY_UNKNOWN) {
		/* T{a, b} with an initializer_list constructor: it, from the list (its other parameters defaulted) */
		cxx_entity *ilc = c->ctor_ilist;
		uint32_t il, arr = 0;
		qty et;

		c->ctor_ilist = NULL;
		il = cxx_gen_initializer_list(c, a->params[AST_TYPE(a, ilc->ty)->aux], args, nargs, out, loc, &arr);
		if (!il)
			return false;
		cxx_gen_call(c, ilc, target, false, &il, 1, loc);
		if (!c->enode)
			return false;
		cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, c->enode, 0, 0));
		c->enode = 0;
		/* the array's elements live to the end of the full-expression: here, the declaration's (not for an
		   exception thrown by the constructor) */
		et = arr ? AST_TYPE(a, a->syms[arr].ty)->base : CXX_TY_UNKNOWN;
		if (arr && cc_type_is_record(a, et) && cxx_gen_needs_destruction(c, et, 0))
			for (uint32_t i = nargs; i-- > 0;) {
				uint32_t slot = cc_dereference_build(a, cc_binary_build(a, LEX_TOKEN_PLUS, cc_conversion_decay(a, cxx_gen_var_node(c, arr, loc)),
									  cc_node_integer_new(a, i, cc_type_basic(TY_LONG), loc), loc), loc);

				if (!cxx_gen_destroy(c, slot, et, out, loc))
					return false;
			}
		return true;
	}
	if (ctor && ctor->member_of != cls)
		ctor = NULL, found = false;	/* (a base's constructor, found by name: not this class's) */
	if (ctor && nargs == 1 && args[0] && QUNQ(AST_NODE_AT(a, args[0])->ty) == QUNQ(t)) {
		/* from an object of the class: only a copy or move constructor takes it (not one found through a
		   conversion of the object to something else) */
		const ctype *cf = ctor->ty != CXX_TY_UNKNOWN && AST_TYPE_KIND(a, ctor->ty) == TY_FUNC ? AST_TYPE(a, ctor->ty) : NULL;
		qty p0 = cf && cf->n ? a->params[cf->aux] : CXX_TY_UNKNOWN;

		if (p0 == CXX_TY_UNKNOWN || !(QUNQ(p0) == QUNQ(t) || (cxx_gen_is_reference(c, p0) && QUNQ(AST_TYPE(a, p0)->base) == QUNQ(t))))
			ctor = NULL, found = false;
	}
	if (nargs == 1 && args[0] && QUNQ(AST_NODE_AT(a, args[0])->ty) == QUNQ(t)) {
		/* from a prvalue of the class: constructed here in the first place */
		cxx_prv *p = cxx_gen_prv_find(c, args[0]);

		if (p && p->call) {
			cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, cxx_gen_prv_aim(c, p, cc_address_of_build(a, target, loc)), 0, 0));
			return true;
		}
	}
	if (found && ctor && ctor->fn_defaulted && nargs == 1 && args[0] && QUNQ(AST_NODE_AT(a, args[0])->ty) == QUNQ(t) &&
	    cxx_gen_defaulted_copy_trivial(c, t)) {
		/* T(const T&) = default, T(T&&) = default of a trivially copyable class: its bytes, as clang does (it
		   calls no function, and defines none) */
		ctor = NULL, found = false;
		goto bitwise;
	}
	if (found && ctor && ctor->fn_defaulted && !ctor->fn_deleted && nargs == 1 && args[0] && QUNQ(AST_NODE_AT(a, args[0])->ty) == QUNQ(t) &&
	    (AST_NODE_AT(a, args[0])->flags & NF_LVALUE) && ctor->ty != CXX_TY_UNKNOWN && AST_TYPE(a, ctor->ty)->n == 1 &&
	    cxx_gen_is_reference(c, a->params[AST_TYPE(a, ctor->ty)->aux]) && AST_TYPE(a, a->params[AST_TYPE(a, ctor->ty)->aux])->ext == 1) {
		/* T(const T&) = default of a class that is not trivially copyable (a polymorphic one: std::exception):
		   what it does, in place (bases, the vptr, members), no function called */
		uint32_t src = args[0];

		if (AST_NODE_AT(a, target)->ty != QUNQ(t)) {
			qty pt = cc_type_pointer(a, QUNQ(t));
			uint32_t addr = cc_address_of_build(a, target, loc);

			target = cc_dereference_build(a, cc_cast_insert(a, addr, pt, cc_conversion_cast_kind(a, AST_NODE_AT(a, addr)->ty, pt, addr)), loc);
		}
		if (AST_NODE_AT(a, src)->ty != QUNQ(t)) {
			qty pt = cc_type_pointer(a, QUNQ(t));
			uint32_t addr = cc_address_of_build(a, src, loc);

			src = cc_dereference_build(a, cc_cast_insert(a, addr, pt, cc_conversion_cast_kind(a, AST_NODE_AT(a, addr)->ty, pt, addr)), loc);
		}
		return cxx_gen_copy_construct_parts(c, target, t, src, out, loc, 0);
	}
	if (found && ctor && ctor->fn_defaulted && !nargs) {	/* T() = default: what the implicit one does, in place */
		if (cxx_class_has_member_initializer(c, t))
			return cxx_gen_construct_by_body(c, target, t, ctor, out, loc);
		return cxx_gen_default_construct_parts(c, target, t, out, loc, 0);
	}
	if (found && ctor) {
		cxx_gen_call(c, ctor, target, false, args, nargs, loc);
		if (!c->enode)
			return false;
		cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, c->enode, 0, 0));
		c->enode = 0;
		return true;
	}
	if (nargs == 0)
		return cxx_gen_default_construct(c, target, t, out, loc, 0);	/* (the implicit default constructor, in place) */
bitwise:
	if (nargs == 1 && args[0] && QUNQ(AST_NODE_AT(a, args[0])->ty) == QUNQ(t) && (cxx_gen_type_ok(c, t) || cxx_gen_trivial_abi(c, t, 0) == 1)) {
		/* from an object of a class C copies as C++ does (a const object is initialized all the same) */
		bool pod, empty;

		if (cxx_class_layout_info(c, t, &pod, &empty) && empty) {
			/* an empty class: nothing to copy (its byte may be another member's, [[no_unique_address]]
			   __value_comp_ at __size_'s offset in std::__tree) */
			if (!(AST_NODE_AT(a, args[0])->flags & NF_LVALUE))
				cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, args[0], 0, 0));
			return true;
		}
		if (AST_NODE_AT(a, target)->ty != QUNQ(AST_NODE_AT(a, target)->ty)) {
			qty pt = cc_type_pointer(a, QUNQ(t));
			uint32_t addr = cc_address_of_build(a, target, loc);

			target = cc_dereference_build(a, cc_cast_insert(a, addr, pt, cc_conversion_cast_kind(a, AST_NODE_AT(a, addr)->ty, pt, addr)), loc);
		}
		cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, target, cc_node_rvalue(a, args[0]), loc), 0, 0));
		return true;
	}
	if (!found && !(nargs == 1 && args[0] && QUNQ(AST_NODE_AT(a, args[0])->ty) == QUNQ(t)))
		return cxx_gen_aggregate_init(c, target, t, args, nargs, out, loc);
	if (nargs == 1 && args[0] && QUNQ(AST_NODE_AT(a, args[0])->ty) == QUNQ(t) && (AST_NODE_AT(a, args[0])->flags & NF_LVALUE) &&
	    (!found || (ctor && ctor->fn_defaulted))) {
		/* the implicit (or defaulted) copy or move constructor of a class with constructors: member by member */
		uint32_t src = args[0];

		if (AST_NODE_AT(a, src)->ty != QUNQ(t)) {
			qty pt = cc_type_pointer(a, QUNQ(t));
			uint32_t addr = cc_address_of_build(a, src, loc);

			src = cc_dereference_build(a, cc_cast_insert(a, addr, pt, cc_conversion_cast_kind(a, AST_NODE_AT(a, addr)->ty, pt, addr)), loc);
		}
		if (AST_NODE_AT(a, target)->ty != QUNQ(t)) {
			qty pt = cc_type_pointer(a, QUNQ(t));
			uint32_t addr = cc_address_of_build(a, target, loc);

			target = cc_dereference_build(a, cc_cast_insert(a, addr, pt, cc_conversion_cast_kind(a, AST_NODE_AT(a, addr)->ty, pt, addr)), loc);
		}
		return cxx_gen_copy_construct_parts(c, target, t, src, out, loc, 0);
	}
	return false;	/* (an implicit copy of a class with constructors: not built yet) */
}

/* this->... : the object being constructed or destroyed, as an lvalue */
VPRIV uint32_t cxx_gen_this_object(cxx *c, uint32_t loc)
{
	uint32_t p;

	cxx_gen_this(c, loc);
	p = c->enode;
	c->enode = 0;
	return p ? cc_dereference_build(c->ast, p, loc) : 0;
}

/*
 * A local object of class type: its declaration, its construction from args (cxx_ctor_use was called for
 * it), and its destructor at the end of its scope (the emitter's scope cleanup, as C's
 * __attribute__((cleanup))).
 */
VPRIV void cxx_gen_construct_local(cxx *c, uint32_t s, qty t, const uint32_t *args, uint32_t nargs, uint32_t loc)
{
	cc *a = c->ast;
	cxx_entity *dtor;
	cc_id_list *block = c->gen_block;
	/* (what cxx_ctor_use chose: naming the destructor below resolves nothing, but keep it apart) */
	cxx_entity *picked = c->ctor_picked, *il = c->ctor_ilist;
	bool found = c->ctor_found, none = c->ctor_none;
	qty il_types[CXX_MAX_ARGS];
	uint8_t il_lval[CXX_MAX_ARGS];
	uint32_t il_n = c->ctor_ilist_n, il_brace[CXX_MAX_ARGS];

	memcpy(il_types, c->ctor_ilist_types, sizeof il_types), memcpy(il_lval, c->ctor_ilist_lval, sizeof il_lval);
	memcpy(il_brace, c->ctor_ilist_brace, sizeof il_brace);

	if (!cxx_gen_on(c) || !s)
		return;
	if (!block || nargs == UINT32_MAX) {
		cxx_gen_fail(c);
		return;
	}
	dtor = cxx_gen_effective_destructor(c, t, 0);
	if (dtor) {
		attrs at;

		if (dtor->fn_deleted || !cxx_gen_name(c, dtor) || !dtor->sym1 || (cxx_member_body_use(c, dtor), 0)) {
			cxx_gen_fail(c);
			return;
		}
		memset(&at, 0, sizeof at);
		at.cleanup = cxx_gen_dtor_sym(dtor);
		cc_attribute_apply_symbol(a, &a->syms[s], &at);
	} else if (cxx_gen_needs_destruction(c, t, 0)) {
		cxx_gen_fail(c);	/* (an implicit destructor that destroys members: not built yet) */
		return;
	}
	cc_id_list_append(a, block, cc_node_new_operands(a, N_DECL, cc_type_basic(TY_VOID), loc, s, 0));
	c->ctor_picked = picked, c->ctor_found = found, c->ctor_none = none, c->ctor_ilist = il, c->ctor_ilist_n = il_n;
	memcpy(c->ctor_ilist_types, il_types, sizeof il_types), memcpy(c->ctor_ilist_lval, il_lval, sizeof il_lval);
	memcpy(c->ctor_ilist_brace, il_brace, sizeof il_brace);
	if (!cxx_gen_construct_at(c, cxx_gen_var_node(c, s, loc), t, args, nargs, block, loc))
		cxx_gen_fail(c);
}

/* ---- new and delete ------------------------------------------------------------------------- */

/* a call of one of the builtin table's library functions (0: not built) */
VPRIV uint32_t cxx_gen_library_call(cxx *c, const char *name, const uint32_t *args, uint32_t nargs, uint32_t loc)
{
	uint32_t saved = c->enode, n;

	c->enode = 0;
	cxx_gen_builtin(c, name, strlen(name), args, nargs, loc);
	n = c->enode;
	c->enode = saved;
	return n;
}
