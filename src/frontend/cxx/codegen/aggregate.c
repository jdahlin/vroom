/* C++ code generation: aggregate initialization: braced lists for arrays and classes without constructors */
#include "codegen.h"

/*
 * T{a, b} or T(a, b) of an aggregate (a class with no constructor, no base, no virtual function): zeroed,
 * then its members in order from the elements; those after the last element keep their zero, or get their
 * default initializer.  False: not built (anonymous members, arrays from lists, members of classes with
 * constructors from more than their element).
 */
VPRIV bool cxx_gen_aggregate_init(cxx *c, uint32_t target, qty t, const uint32_t *args, uint32_t nargs, cc_id_list *out, uint32_t loc)
{
	cc *a = c->ast;
	cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t));
	const tag *r;
	qty bases[2];
	uint32_t k = 0, zargs[3], saved = c->enode, zero, braces[CXX_MAX_ARGS];
	bool have_braces = c->gen_agg_brace != NULL && nargs <= CXX_MAX_ARGS;

	if (have_braces)
		memcpy(braces, c->gen_agg_brace, nargs * sizeof *braces);
	c->gen_agg_brace = NULL;	/* (this list's: not a nested one's) */

	if (!cls || AST_TYPE_KIND(a, QUNQ(t)) != TY_STRUCT || cxx_class_polymorphic(c, t) || cxx_class_bases(c, t, bases, 2))
		return false;
	for (cxx_entity *e = cxx_lookup_in(c, cls, cxx_ctor_id(), 0); e; e = e->next)
		if (e->member_of == cls)
			return false;
	r = &a->tags[AST_TYPE(a, QUNQ(t))->aux];
	for (uint32_t i = 0; i < r->nfield; i++)
		if (!r->f[i].name)
			return false;
	if (AST_NODE_AT(a, target)->ty != QUNQ(AST_NODE_AT(a, target)->ty)) {
		qty pt = cc_type_pointer(a, QUNQ(t));
		uint32_t addr = cc_address_of_build(a, target, loc);

		target = cc_dereference_build(a, cc_cast_insert(a, addr, pt, cc_conversion_cast_kind(a, AST_NODE_AT(a, addr)->ty, pt, addr)), loc);
	}
	{
		bool pod_, empty_;

		if (cxx_class_layout_info(c, QUNQ(t), &pod_, &empty_) && empty_)
			return nargs == 0;	/* (an empty class: no bytes of its own to zero, its one may be another member's) */
	}
	zargs[0] = cc_address_of_build(a, target, loc);
	zargs[1] = cc_node_integer_new(a, 0, cc_type_basic(TY_INT), loc);
	zargs[2] = cc_node_integer_new(a, (i128)AST_TYPE(a, QUNQ(t))->size, cc_type_basic(TY_ULONG), loc);
	c->enode = 0;
	cxx_gen_builtin(c, "__builtin_memset", 16, zargs, 3, loc);
	zero = c->enode;
	c->enode = saved;
	if (!zero)
		return false;
	cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, zero, 0, 0));
	for (uint32_t i = 0; i < r->nfield; i++) {
		qty ft = r->f[i].ty;
		uint32_t m = member(a, target, r->f[i].name, loc);
		cxx_entity *fe = cxx_lookup_local(cls, r->f[i].name);

		while (fe && !(fe->kind == CXX_ENTITY_VALUE && fe->field_of))
			fe = fe->next;
		if (m && AST_TYPE_KIND(a, ft) == TY_ARRAY && AST_TYPE(a, ft)->n && cxx_gen_type_ok(c, AST_TYPE(a, ft)->base) &&
		    !cxx_gen_is_reference(c, AST_TYPE(a, ft)->base)) {
			/* an array of scalars: the next elements, braces elided (std::array<int, 3> a{1, 2, 3}); the rest keep their
			   zero */
			qty et = AST_TYPE(a, ft)->base;

			if (fe && fe->has_nsdmi && k >= nargs)
				return false;
			for (uint64_t j = 0; j < AST_TYPE(a, ft)->n && k < nargs; j++, k++) {
				uint32_t el, v;

				if (!args[k] || cc_type_is_record(a, AST_NODE_AT(a, args[k])->ty) || AST_TYPE_KIND(a, AST_NODE_AT(a, args[k])->ty) == TY_ARRAY)
					return false;
				el = cc_dereference_build(a, cc_binary_build(a, LEX_TOKEN_PLUS, cc_conversion_decay(a, m),
								cc_node_integer_new(a, (i128)j, cc_type_basic(TY_LONG), loc), loc), loc);
				if (et != QUNQ(et)) {
					qty pt = cc_type_pointer(a, QUNQ(et));
					uint32_t addr = cc_address_of_build(a, el, loc);

					el = cc_dereference_build(a, cc_cast_insert(a, addr, pt, cc_conversion_cast_kind(a, AST_NODE_AT(a, addr)->ty, pt, addr)), loc);
				}
				if (!(v = cxx_gen_bind(c, args[k], QUNQ(et), loc)))
					return false;
				cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, el, v, loc), 0, 0));
			}
			continue;
		}
		if (!m || AST_TYPE_KIND(a, ft) == TY_ARRAY)
			return false;
		if (k >= nargs) {
			if (fe && fe->has_nsdmi)
				return false;	/* (a default initializer after the elements: needs this, not built yet) */
			if (cc_type_is_record(a, ft) && cxx_gen_needs_construction(c, ft, 0) && !cxx_gen_default_construct(c, m, ft, out, loc, 1))
				return false;
			continue;
		}
		if (!args[k] && have_braces && braces[k] && !cxx_gen_is_reference(c, ft)) {
			/* the member's element is a braced list itself (M m{{nullptr}}): parsed again here for its
			   code, the member initialized from its elements (an aggregate's own braced elements in turn) */
			uint32_t pos = c->pos, an[CXX_MAX_ARGS], abr[CXX_MAX_ARGS], n;
			qty at[CXX_MAX_ARGS];
			uint8_t al[CXX_MAX_ARGS];

			c->pos = braces[k];
			cxx_braced_arguments_parse(c);
			c->pos = pos;
			n = c->ncargs;
			if (n == UINT32_MAX || n > CXX_MAX_ARGS)
				return false;
			memcpy(an, c->carg_nodes, sizeof an), memcpy(abr, c->carg_brace, sizeof abr);
			memcpy(at, c->cargs, n * sizeof *at), memcpy(al, c->cargs_lval, n);
			if (cc_type_is_record(a, ft)) {
				cxx_ctor_use(c, QUNQ(ft), at, al, n, true);
				c->gen_agg_brace = abr;
				if (!cxx_gen_construct_at(c, m, QUNQ(ft), an, n, out, loc))
					return false;
			} else if (n == 1 && an[0]) {
				/* a scalar from {v} ({}: its zero, kept) */
				uint32_t v = cxx_gen_bind(c, an[0], QUNQ(ft), loc);

				if (!v)
					return false;
				if (ft != QUNQ(ft)) {
					qty pt = cc_type_pointer(a, QUNQ(ft));
					uint32_t addr = cc_address_of_build(a, m, loc);

					m = cc_dereference_build(a, cc_cast_insert(a, addr, pt, cc_conversion_cast_kind(a, AST_NODE_AT(a, addr)->ty, pt, addr)), loc);
				}
				cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, m, v, loc), 0, 0));
			} else if (n)
				return false;
			k++;
			continue;
		}
		if (!args[k])
			return false;
		if (cc_type_is_record(a, ft)) {
			qty at[1] = { AST_NODE_AT(a, args[k])->ty };
			uint8_t lv[1] = { (AST_NODE_AT(a, args[k])->flags & NF_LVALUE) != 0 };

			cxx_ctor_use(c, QUNQ(ft), at, lv, 1, false);
			if (!cxx_gen_construct_at(c, m, QUNQ(ft), &args[k], 1, out, loc))
				return false;
		} else {
			uint32_t v = cxx_gen_bind(c, args[k], ft, loc);

			if (!v)
				return false;
			if (cxx_gen_is_reference(c, ft))
				return false;	/* (a reference member: not built yet) */
			if (ft != QUNQ(ft)) {
				qty pt = cc_type_pointer(a, QUNQ(ft));
				uint32_t addr = cc_address_of_build(a, m, loc);

				m = cc_dereference_build(a, cc_cast_insert(a, addr, pt, cc_conversion_cast_kind(a, AST_NODE_AT(a, addr)->ty, pt, addr)), loc);
			}
			cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, m, v, loc), 0, 0));
		}
		k++;
	}
	if (k != nargs)
		return false;	/* (more elements than the members take) */
	return true;
}

/*
 * A std::initializer_list<E> for a braced list's elements (the arguments of T{...}): a local array of them,
 * converted to E (constructed, for a class E), and a local initializer_list pointing at it, as an lvalue
 * (0: not built: libc++'s or libstdc++'s members not found; a class E's are destroyed by the caller).
 */
VPRIV bool cxx_gen_destroy(cxx *c, uint32_t x, qty t, cc_id_list *out, uint32_t loc);

VPRIV uint32_t cxx_gen_initializer_list(cxx *c, qty ilt, const uint32_t *args, uint32_t nargs, cc_id_list *out, uint32_t loc, uint32_t *array)
{
	cc *a = c->ast;
	qty lt = cc_type_basic(TY_LONG), et, pt;
	uint32_t arr = 0, il, begin, size;
	cc_id_list *saved = c->gen_block;
	qty types[CXX_MAX_ARGS];
	uint8_t lvals[CXX_MAX_ARGS];
	uint32_t types_n = c->ctor_ilist_n == nargs ? nargs : 0;	/* (the element constructors' own uses overwrite them) */

	uint32_t braces[CXX_MAX_ARGS];

	memcpy(types, c->ctor_ilist_types, sizeof types), memcpy(lvals, c->ctor_ilist_lval, sizeof lvals);
	memcpy(braces, c->ctor_ilist_brace, sizeof braces);
	if (!types_n)
		memset(braces, 0, sizeof braces);
	if (!cxx_tag_scope_get(c, QUNQ(ilt)) || !(il = cxx_gen_local(c, 0, QUNQ(ilt), loc)))
		return 0;
	begin = cxx_gen_member_of(c, cxx_gen_var_node(c, il, loc), vintern_cstr("__begin_"), loc, 0);
	size = cxx_gen_member_of(c, cxx_gen_var_node(c, il, loc), vintern_cstr("__size_"), loc, 0);
	if (!begin || !size) {	/* (libstdc++'s) */
		begin = cxx_gen_member_of(c, cxx_gen_var_node(c, il, loc), vintern_cstr("_M_array"), loc, 0);
		size = cxx_gen_member_of(c, cxx_gen_var_node(c, il, loc), vintern_cstr("_M_len"), loc, 0);
	}
	if (!begin || !size || AST_TYPE_KIND(a, AST_NODE_AT(a, begin)->ty) != TY_PTR)
		return 0;
	pt = AST_NODE_AT(a, begin)->ty, et = QUNQ(AST_TYPE(a, pt)->base);
	if (!cxx_gen_object_type_ok(c, et))
		return 0;
	c->gen_block = out;
	if (nargs) {
		if (!(arr = cxx_gen_local(c, 0, cc_type_array(a, et, nargs, 0), loc)))
			goto fail;
		cxx_gen_local_declare(c, arr, 0, false, loc);
	}
	for (uint32_t i = 0; i < nargs; i++) {
		uint32_t slot = cc_dereference_build(a, cc_binary_build(a, LEX_TOKEN_PLUS, cc_conversion_decay(a, cxx_gen_var_node(c, arr, loc)),
								  cc_node_integer_new(a, i, lt, loc), loc), loc);

		if (!args[i] && i < 16 && braces[i] && cc_type_is_record(a, et)) {
			/* the element is a braced list itself, {"a", 1}: E{...} from its arguments, parsed again here for
			   their code (its first parse was unevaluated) */
			uint32_t pos = c->pos, an[CXX_MAX_ARGS], n;
			qty at[CXX_MAX_ARGS];
			uint8_t al[CXX_MAX_ARGS];

			c->pos = braces[i];
			cxx_braced_arguments_parse(c);
			c->pos = pos;
			n = c->ncargs;
			if (n == UINT32_MAX || n > CXX_MAX_ARGS)
				goto fail;
			memcpy(an, c->carg_nodes, sizeof an), memcpy(at, c->cargs, n * sizeof *at), memcpy(al, c->cargs_lval, n);
			cxx_ctor_use(c, et, at, al, n, true);
			if (!cxx_gen_construct_at(c, slot, et, an, n, out, loc))
				goto fail;
			continue;
		}
		if (!args[i])
			goto fail;
		if (cc_type_is_record(a, et)) {
			qty at = i < types_n && types[i] != CXX_TY_UNKNOWN ? types[i] : AST_NODE_AT(a, args[i])->ty;
			uint8_t lv = i < types_n ? lvals[i] : (AST_NODE_AT(a, args[i])->flags & NF_LVALUE) != 0;
			uint32_t x = args[i];

			if (c->nullptr_ty && QUNQ(at) == c->nullptr_ty && AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) == TY_PTR) {
				/* nullptr (its node a void *): a std::nullptr_t object, which a T&& parameter binds */
				uint32_t np = cxx_gen_local(c, 0, c->nullptr_ty, loc);

				if (!np)
					goto fail;
				cxx_gen_local_declare(c, np, 0, false, loc);
				cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0,
					cc_dereference_build(a, cc_cast_insert(a, cc_address_of_build(a, cxx_gen_var_node(c, np, loc), loc),
						cc_type_pointer(a, AST_NODE_AT(a, x)->ty), CK_P2P), loc), x, loc), 0, 0));
				x = cxx_gen_var_node(c, np, loc);
			}

			cxx_ctor_use(c, et, &at, &lv, 1, false);
			if (!cxx_gen_construct_at(c, slot, et, &x, 1, out, loc))
				goto fail;
		} else {
			uint32_t x = cxx_gen_bind(c, args[i], et, loc);

			if (!x)
				goto fail;
			cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, slot, x, loc), 0, 0));
		}
	}
	cxx_gen_local_declare(c, il, 0, false, loc);
	cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, begin,
		nargs ? cc_conversion_apply(a, cc_conversion_decay(a, cxx_gen_var_node(c, arr, loc)), pt) : cc_conversion_apply(a, cc_node_integer_new(a, 0, lt, loc), pt), loc), 0, 0));
	cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, size,
		cc_conversion_apply(a, cc_node_integer_new(a, nargs, lt, loc), AST_NODE_AT(a, size)->ty), loc), 0, 0));
	c->gen_block = saved;
	*array = arr;
	return cxx_gen_var_node(c, il, loc);
fail:
	c->gen_block = saved;
	return 0;
}
