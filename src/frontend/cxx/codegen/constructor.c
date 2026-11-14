/* C++ code generation: constructors' bodies: member initializers, default member initializers, implicit constructors */
#include "codegen.h"

/*
 * A constructor's initializer: name(args) for a member, or a base class (class_type; this class itself:
 * a delegating constructor).  A scalar member is assigned (zero for no argument); a member or base of
 * class type is constructed (cxx_ctor_use was called for it with these arguments).
 */
VPRIV void cxx_gen_member_initializer(cxx *c, uint32_t name, qty class_type, const uint32_t *args, uint32_t nargs, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t self, target, value;
	qty mt;

	if (!cxx_gen_on(c) || !c->gen_this || !c->gen_pre)
		return;
	self = cxx_gen_this_object(c, loc);
	if (!self || nargs == UINT32_MAX) {
		cxx_gen_fail(c);
		return;
	}
	if (!name) {
		/* a base class, or the class itself */
		qty own = AST_NODE_AT(a, self)->ty;

		if (class_type == CXX_TY_UNKNOWN || !cc_type_is_record(a, class_type)) {
			cxx_gen_fail(c);
			return;
		}
		if (QUNQ(class_type) == QUNQ(own))
			target = self, c->gen_delegates = 1;
		else
			target = cxx_gen_base_object(c, self, QUNQ(class_type), loc), c->gen_base_type = QUNQ(class_type);
		if (!target || !cxx_gen_construct_at(c, target, QUNQ(class_type), args, nargs, c->gen_pre, loc)) {
			c->gen_base_type = 0;
			cxx_gen_fail(c);
			return;
		}
		c->gen_base_type = 0;
		if (c->ngen_init_bases < 16)
			c->gen_init_bases[c->ngen_init_bases++] = QUNQ(class_type);
		return;
	}
	{
		/* the member as it is in the object (through anonymous structs and unions too) */
		uint32_t raw = cxx_gen_member_of(c, self, name, loc, 0);

		if (raw && cxx_gen_is_reference(c, AST_NODE_AT(a, raw)->ty)) {
			/* a reference member: bound to its initializer (the member holds the address) */
			uint32_t v = nargs == 1 && args[0] ? cxx_gen_bind(c, args[0], AST_NODE_AT(a, raw)->ty, loc) : 0;

			if (!v) {
				cxx_gen_fail(c);
				return;
			}
			if (c->ngen_init_names < 32)
				c->gen_init_names[c->ngen_init_names++] = name;
			cc_id_list_append(a, (cc_id_list *)c->gen_pre, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, raw, v, loc), 0, 0));
			return;
		}
		if (!raw) {
			/* not in the record: an empty [[no_unique_address]] member, which takes no space (an allocator, a
			   comparator): constructed all the same, at the object's own address */
			cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(AST_NODE_AT(a, self)->ty));
			cxx_entity *fe = cls ? cxx_lookup_local(cls, name) : NULL;
			bool pod, empty;

			if (fe && fe->kind == CXX_ENTITY_VALUE && fe->field_of == cls && fe->ty != CXX_TY_UNKNOWN && cc_type_is_record(a, fe->ty) &&
			    AST_TYPE_KIND(a, fe->ty) != TY_ARRAY && cxx_class_layout_info(c, fe->ty, &pod, &empty) && empty) {
				qty pt = cc_type_pointer(a, QUNQ(fe->ty));
				uint32_t addr = cc_address_of_build(a, self, loc);

				target = cc_dereference_build(a, cc_cast_insert(a, addr, pt, cc_conversion_cast_kind(a, AST_NODE_AT(a, addr)->ty, pt, addr)), loc);
				if (c->ngen_init_names < 32)
					c->gen_init_names[c->ngen_init_names++] = name;
				if (!cxx_gen_construct_at(c, target, QUNQ(fe->ty), args, nargs, c->gen_pre, loc))
					cxx_gen_fail(c);
				return;
			}
			cxx_gen_fail(c);	/* (a static member, a member of a class not laid out) */
			return;
		}
	}
	target = cxx_gen_member_of(c, self, name, loc, 0);
	if (!target || !(AST_NODE_AT(a, target)->flags & NF_LVALUE)) {
		cxx_gen_fail(c);
		return;
	}
	if (c->ngen_init_names < 32)
		c->gen_init_names[c->ngen_init_names++] = name;
	mt = AST_NODE_AT(a, target)->ty;
	if (cc_type_is_record(a, mt) && AST_TYPE_KIND(a, mt) != TY_ARRAY) {
		bool own = c->ctor_found && c->ctor_picked && c->ctor_picked->member_of == cxx_tag_scope_get(c, QUNQ(mt));

		bool pod_, empty_ = false;

		if (!nargs && (!own || c->ctor_picked->fn_defaulted) && cxx_class_layout_info(c, QUNQ(mt), &pod_, &empty_) && empty_) {
			/* member() of an empty class: nothing to zero (its byte may be another member's: [[no_unique_address]]
			   __deleter_ at __ptr_'s offset in std::unique_ptr), constructed if anything needs it */
			if (cxx_gen_needs_construction(c, QUNQ(mt), 0) && !cxx_gen_construct_at(c, target, QUNQ(mt), args, nargs, c->gen_pre, loc))
				cxx_gen_fail(c);
			return;
		}
		if (!nargs && (!own || c->ctor_picked->fn_defaulted)) {
			/* member(): value-initialized; without a user-provided default constructor zeroed first (a union
			   with T() = default), then default-constructed if anything needs it */
			uint32_t zargs[3], saved = c->enode, zero;
			cxx_entity *picked = c->ctor_picked;
			uint8_t found = c->ctor_found, none = c->ctor_none;

			zargs[0] = cc_address_of_build(a, target, loc);
			zargs[1] = cc_node_integer_new(a, 0, cc_type_basic(TY_INT), loc);
			zargs[2] = cc_node_integer_new(a, (i128)AST_TYPE(a, QUNQ(mt))->size, cc_type_basic(TY_ULONG), loc);
			c->enode = 0;
			cxx_gen_builtin(c, "__builtin_memset", 16, zargs, 3, loc);
			zero = c->enode;
			c->enode = saved;
			if (!zero) {
				cxx_gen_fail(c);
				return;
			}
			cc_id_list_append(a, (cc_id_list *)c->gen_pre, cc_statement_new(a, N_EXPR, loc, zero, 0, 0));
			c->ctor_picked = picked, c->ctor_found = found, c->ctor_none = none;
			if (cxx_gen_needs_construction(c, QUNQ(mt), 0) && !cxx_gen_construct_at(c, target, QUNQ(mt), args, nargs, c->gen_pre, loc))
				cxx_gen_fail(c);
			return;
		}
		if (!cxx_gen_construct_at(c, target, QUNQ(mt), args, nargs, c->gen_pre, loc))
			cxx_gen_fail(c);
		return;
	}
	if (!nargs && AST_TYPE_KIND(a, mt) == TY_ARRAY) {
		/* an array member from {} (char __padding_[N] = {}: libc++'s __compressed_pair_padding): its bytes zeroed */
		qty et = mt;
		uint32_t zargs[3], saved = c->enode, zero;

		while (AST_TYPE_KIND(a, et) == TY_ARRAY)
			et = AST_TYPE(a, et)->base;
		if (cc_type_is_record(a, et) || !cxx_gen_type_ok(c, et)) {
			cxx_gen_fail(c);
			return;
		}
		if (!AST_TYPE(a, QUNQ(mt))->size)
			return;	/* (an array of no elements: nothing to zero) */
		zargs[0] = cc_conversion_decay(a, target);
		zargs[1] = cc_node_integer_new(a, 0, cc_type_basic(TY_INT), loc);
		zargs[2] = cc_node_integer_new(a, (i128)AST_TYPE(a, QUNQ(mt))->size, cc_type_basic(TY_ULONG), loc);
		c->enode = 0;
		cxx_gen_builtin(c, "__builtin_memset", 16, zargs, 3, loc);
		zero = c->enode;
		c->enode = saved;
		if (!zero) {
			cxx_gen_fail(c);
			return;
		}
		cc_id_list_append(a, (cc_id_list *)c->gen_pre, cc_statement_new(a, N_EXPR, loc, zero, 0, 0));
		return;
	}
	if (nargs > 1 || (nargs == 1 && !args[0]) || AST_TYPE_KIND(a, mt) == TY_ARRAY || !cxx_gen_type_ok(c, mt)) {
		cxx_gen_fail(c);
		return;
	}
	if (nargs && args[0] && cc_type_is_record(a, AST_NODE_AT(a, args[0])->ty) && AST_TYPE_KIND(a, AST_NODE_AT(a, args[0])->ty) != TY_ARRAY) {
		/* a scalar member from a class: std::nullptr_t's null pointer, or the class's conversion function (C's
		   conversion would be an error) */
		uint32_t v = c->nullptr_ty && QUNQ(AST_NODE_AT(a, args[0])->ty) == c->nullptr_ty ? cxx_gen_bind(c, args[0], QUNQ(mt), loc) :
			     cxx_gen_convert_class(c, args[0], QUNQ(mt), loc);

		if (!v) {
			cxx_gen_fail(c);
			return;
		}
		cc_id_list_append(a, (cc_id_list *)c->gen_pre, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, target, v, loc), 0, 0));
		return;
	}
	value = nargs ? cc_conversion_apply(a, cc_conversion_decay(a, args[0]), QUNQ(mt)) :
			cc_conversion_apply(a, cc_node_integer_new(a, 0, cc_type_basic(TY_INT), loc), QUNQ(mt));
	if (mt != QUNQ(mt)) {
		/* a const member: initialized all the same (through a pointer to its unqualified type) */
		qty pt = cc_type_pointer(a, QUNQ(mt));
		uint32_t addr = cc_address_of_build(a, target, loc);

		target = cc_dereference_build(a, cc_cast_insert(a, addr, pt, cc_conversion_cast_kind(a, AST_NODE_AT(a, addr)->ty, pt, addr)), loc);
	}
	cc_id_list_append(a, (cc_id_list *)c->gen_pre, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, target, value, loc), 0, 0));
}

/*
 * A constructor's body is done: the bases and members of class type its initializers did not name are
 * default-constructed first (into `first`).  False: one of them is not built yet.
 */
/* ... the members of the record r (the class's, or an anonymous struct's inside it) that the initializers
   did not name and that need construction */
/*
 * A data member's default initializer, applied by a constructor whose initializers do not name the member:
 * parsed again where it was written (as a default argument is), into `first`.  False: not built.
 */
VPRIV bool cxx_gen_nsdmi(cxx *c, cxx_entity *fe, cc_id_list *first, uint32_t loc)
{
	jmp_buf jb, *outer_fail = c->fail;
	cxx_scope *scope = c->scope;
	uint32_t pos = c->pos, enode = c->enode, eobj = c->eobj_node;
	qty etype = c->etype;
	uint8_t elval = c->elval, econst = c->econst, einvalid = c->einvalid, call_resolved = c->call_resolved, none_viable = c->none_viable;
	uint8_t eobj_arrow = c->eobj_arrow, eunsure = c->eunsure;
	int64_t evalue = c->evalue;
	cxx_entity *call_picked = c->call_picked, *ecallee = c->ecallee;
	void *pre = c->gen_pre;
	volatile bool ok = false;

	if (!fe->nsdmi_pos || !fe->nsdmi_scope || fe->ty == CXX_TY_UNKNOWN)
		return false;
	c->pos = fe->nsdmi_pos, c->scope = fe->nsdmi_scope, c->fail = &jb;
	c->gen_pre = first;
	if (!setjmp(jb)) {
		uint32_t an[CXX_MAX_ARGS], n = 0;
		qty at[CXX_MAX_ARGS];
		uint8_t lv[CXX_MAX_ARGS];
		bool braced = cxx_is(c, LEX_TOKEN_LEFT_BRACE) || (cxx_is(c, LEX_TOKEN_EQUAL) && cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_BRACE);
		bool record = cc_type_is_record(c->ast, fe->ty) && AST_TYPE_KIND(c->ast, fe->ty) != TY_ARRAY;

		cxx_accept(c, LEX_TOKEN_EQUAL);
		if (braced) {
			cxx_braced_arguments_parse(c);
			n = c->ncargs;
			if (n != UINT32_MAX && n <= CXX_MAX_ARGS) {
				memcpy(an, c->carg_nodes, n * sizeof *an);
				memcpy(at, c->cargs, n * sizeof *at);
				memcpy(lv, c->cargs_lval, n * sizeof *lv);
			}
		} else {
			cxx_assignment_expression_parse(c);
			an[0] = c->enode, at[0] = c->etype, lv[0] = c->elval, n = 1;
			if (at[0] != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, at[0]) == TY_ARRAY)	/* ("text": a pointer) */
				at[0] = cc_type_pointer(c->ast, AST_TYPE(c->ast, at[0])->base), lv[0] = 0;
		}
		if (n != UINT32_MAX && n <= CXX_MAX_ARGS) {
			if (record)
				cxx_ctor_use(c, QUNQ(fe->ty), at, lv, n, braced);
			cxx_gen_member_initializer(c, fe->name, CXX_TY_UNKNOWN, an, n, loc);
			ok = cxx_gen_on(c);
		}
	}
	c->gen_pre = pre;
	c->fail = outer_fail, c->pos = pos, c->scope = scope;
	c->enode = enode, c->eobj_node = eobj, c->eobj_arrow = eobj_arrow, c->etype = etype, c->elval = elval, c->econst = econst;
	c->evalue = evalue, c->einvalid = einvalid, c->eunsure = eunsure, c->call_resolved = call_resolved, c->none_viable = none_viable;
	c->call_picked = call_picked, c->ecallee = ecallee;
	return ok;
}

VPRIV bool cxx_gen_constructor_member_defaults(cxx *c, cxx_scope *cls, const tag *r, cc_id_list *first, uint32_t loc, int depth)
{
	cc *a = c->ast;

	for (uint32_t i = 0; i < r->nfield; i++) {
		qty ft = r->f[i].ty;
		bool named = false;

		if (r->f[i].name) {
			/* a member with a default initializer: it, unless an initializer names the member */
			cxx_entity *fe = cxx_lookup_local(cls, r->f[i].name);

			while (fe && !(fe->kind == CXX_ENTITY_VALUE && fe->field_of))
				fe = fe->next;
			if (fe && fe->has_nsdmi) {
				for (uint32_t k = 0; k < c->ngen_init_names; k++)
					named |= c->gen_init_names[k] == r->f[i].name;
				if (named)
					continue;
				if (!cxx_gen_nsdmi(c, fe, first, loc))
					return false;
				continue;
			}
		}

		if (!r->f[i].name && cc_type_is_record(a, ft) && AST_TYPE_KIND(a, ft) != TY_ARRAY && depth < 4) {
			/* an anonymous struct: its members are the class's (a union: none is made active) */
			if (AST_TYPE_KIND(a, ft) == TY_STRUCT &&
			    !cxx_gen_constructor_member_defaults(c, cls, &a->tags[AST_TYPE(a, ft)->aux], first, loc, depth + 1))
				return false;
			continue;
		}
		if (!r->f[i].name || !cc_type_is_record(a, ft) || AST_TYPE_KIND(a, ft) == TY_ARRAY) {
			/* (an array of a class with a constructor: not built yet) */
			qty et = ft;

			while (AST_TYPE_KIND(a, et) == TY_ARRAY)
				et = AST_TYPE(a, et)->base;
			if (cc_type_is_record(a, et) && cxx_gen_needs_construction(c, et, 0))
				return false;
			continue;
		}
		for (uint32_t k = 0; k < c->ngen_init_names; k++)
			named |= c->gen_init_names[k] == r->f[i].name;
		if (named || !cxx_gen_needs_construction(c, ft, 0))
			continue;
		cxx_ctor_use(c, ft, NULL, NULL, 0, false);
		if (!cxx_gen_construct_at(c, member(a, cxx_gen_this_object(c, loc), r->f[i].name, loc), QUNQ(ft), NULL, 0, first, loc))
			return false;
	}
	return true;
}

VPRIV bool cxx_gen_constructor_defaults(cxx *c, cc_id_list *first, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t self = cxx_gen_this_object(c, loc);
	qty own, bases[16];
	uint32_t nb;

	if (!self)
		return false;
	if (c->gen_delegates)
		return true;	/* (the constructor delegated to did it all) */
	own = AST_NODE_AT(a, self)->ty;
	nb = cxx_class_bases(c, own, bases, 16);
	for (uint32_t i = 0; i < nb; i++) {
		bool named = false;

		for (uint32_t k = 0; k < c->ngen_init_bases; k++)
			named |= c->gen_init_bases[k] == QUNQ(bases[i]);
		if (named || !cxx_gen_needs_construction(c, bases[i], 0))
			continue;
		cxx_ctor_use(c, bases[i], NULL, NULL, 0, false);
		c->gen_base_type = QUNQ(bases[i]);
		if (!cxx_gen_construct_at(c, cxx_gen_base_object(c, cxx_gen_this_object(c, loc), bases[i], loc), bases[i], NULL, 0, first, loc)) {
			c->gen_base_type = 0;
			return false;
		}
		c->gen_base_type = 0;
	}
	return cxx_gen_constructor_member_defaults(c, cxx_tag_scope_get(c, QUNQ(own)), &a->tags[AST_TYPE(a, own)->aux], first, loc, 0);
}

/*
 * The implicit default constructor of a class whose members have default initializers (or its T() = default,
 * d), made as a function the way clang makes it: an empty body, to which cxx_gen_function_end adds the
 * bases, the members and their default initializers.  NULL: not built.
 */
VPRIV cxx_entity *cxx_gen_constructor_body(cxx *c, qty t, cxx_entity *d)
{
	cc *a = c->ast;
	cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t)), *fs;
	uint32_t body;

	if (!cls || !cls->owner || cls->dependent || !a->o->emit || c->tentative || c->unevaluated || c->gen_stack_depth > 40)
		return NULL;
	if (!d) {
		d = cxx_declare(c, cls, cxx_ctor_id(), CXX_ENTITY_VALUE);
		if (!d)
			return NULL;
		d->ty = cxx_type_function(c, cc_type_basic(TY_VOID), NULL, 0, false);
		d->member_of = cls;
		d->fn_defaulted = 1, d->fn_implicit = 1, d->fn_inline = 1;
	}
	if (d->gen_tried || d->fn_deleted)
		return d->sym && (a->syms[d->sym].flags & SF_DEFINED) ? d : d->gen_tried == 2 ? d : NULL;
	d->gen_tried = 2;
	d->fn_inline = 1;
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

/* T() of a class with default member initializers: its (made) default constructor called on target */
VPRIV bool cxx_gen_construct_by_body(cxx *c, uint32_t target, qty t, cxx_entity *d, cc_id_list *out, uint32_t loc)
{
	cc *a = c->ast;

	d = cxx_gen_constructor_body(c, t, d);
	if (!d || !target)
		return false;
	cxx_gen_call(c, d, target, false, NULL, 0, loc);
	if (!c->enode)
		return false;
	cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, c->enode, 0, 0));
	c->enode = 0;
	return true;
}
