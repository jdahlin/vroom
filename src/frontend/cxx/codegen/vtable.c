/* C++ code generation: virtual tables and type information */
#include "codegen.h"

VPRIV bool cxx_vt_is_dtor(const cxx_entity *e)
{
	return cxx_gen_is_special(e) && vintern_ents[e->name].s[1] != 'c';
}

/* does y override x: the same name, parameter types and constness (a destructor overrides a destructor) */
VPRIV bool cxx_vt_same(cxx *c, const cxx_entity *x, const cxx_entity *y)
{
	cc *a = c->ast;
	const ctype *fx, *fy;

	if (cxx_vt_is_dtor(x) || cxx_vt_is_dtor(y))
		return cxx_vt_is_dtor(x) && cxx_vt_is_dtor(y);
	if (x->name != y->name || x->ty == CXX_TY_UNKNOWN || y->ty == CXX_TY_UNKNOWN || AST_TYPE_KIND(a, x->ty) != TY_FUNC ||
	    AST_TYPE_KIND(a, y->ty) != TY_FUNC || x->fn_const != y->fn_const)
		return false;
	fx = AST_TYPE(a, x->ty), fy = AST_TYPE(a, y->ty);
	if (fx->n != fy->n)
		return false;
	for (uint32_t i = 0; i < fx->n; i++)
		if (QUNQ(a->params[fx->aux + i]) != QUNQ(a->params[fy->aux + i]))
			return false;
	return true;
}

VPRIV void cxx_vt_push(cxx_scope *cls, cxx_entity *e)
{
	if (cls->nvfuncs == cls->cvfuncs)
		cls->vfuncs = realloc(cls->vfuncs, (cls->cvfuncs = cls->cvfuncs ? cls->cvfuncs * 2 : 8) * sizeof *cls->vfuncs);
	cls->vfuncs[cls->nvfuncs++] = e;
}

/* the class's vtable starts as its primary base's (the one base with a vtable) */
VPRIV void cxx_vtable_init(cxx *c, cxx_scope *cls)
{
	if (cls->vt_init)
		return;
	cls->vt_init = 1;
	for (uint32_t i = 0; i < cls->nused; i++) {
		cxx_scope *b = cls->used[i];

		if (!b || b->kind != CXX_SCOPE_CLASS)
			continue;
		cxx_vtable_init(c, b);
		if (b->vt_bad)
			cls->vt_bad = 1;
		if (!b->nvfuncs)
			continue;
		if (cls->nvfuncs) {
			cls->vt_bad = 1;	/* (a second vtable, with thunks: not built yet) */
			continue;
		}
		for (uint32_t k = 0; k < b->nvfuncs; k++)
			cxx_vt_push(cls, b->vfuncs[k]);
	}
}

/* a non-static member function is declared in a class: its vtable slot, if it is virtual or overrides one */
VPRIV void cxx_vtable_declare(cxx *c, cxx_scope *cls, cxx_entity *e, bool virt_kw)
{
	bool found = false;

	if (!cls || cls->kind != CXX_SCOPE_CLASS || c->tentative || e->fn_virtual || (cxx_gen_is_special(e) && !cxx_vt_is_dtor(e)))
		return;
	cxx_vtable_init(c, cls);
	for (uint32_t i = 0; i < cls->nvfuncs; i++)
		if (cls->vfuncs[i] != e && cxx_vt_same(c, cls->vfuncs[i], e)) {
			if (!found)
				e->vslot = i + 1;
			cls->vfuncs[i] = e;
			found = true;
		}
	if (!found && !virt_kw)
		return;
	if (!found) {
		e->vslot = cls->nvfuncs + 1;
		cxx_vt_push(cls, e);
		if (cxx_vt_is_dtor(e))
			cxx_vt_push(cls, e);
	}
	e->fn_virtual = 1;
	{
		/* = 0 after the declarator (and override, final) */
		uint32_t k = 0;

		while (cxx_peek(c, k)->kind == LEX_TOKEN_IDENTIFIER && !cxx_peek(c, k)->kw && k < 4)
			k++;
		if (cxx_peek(c, k)->kind == LEX_TOKEN_EQUAL && cxx_peek(c, k + 1)->len == 1 && cxx_peek(c, k + 1)->s[0] == '0')
			e->fn_pure = 1;
	}
	if (cls->nvown == cls->cvown)
		cls->vown = realloc(cls->vown, (cls->cvown = cls->cvown ? cls->cvown * 2 : 8) * sizeof *cls->vown);
	cls->vown[cls->nvown++] = e;
}

/* can the class's vtable be built here? */
VPRIV bool cxx_vtable_usable(cxx *c, cxx_scope *cls)
{
	if (!cls || cls->kind != CXX_SCOPE_CLASS || !cls->owner || cls->owner->ty == CXX_TY_UNKNOWN || cls->dependent)
		return false;
	cxx_vtable_init(c, cls);
	if (cls->vt_bad || !cls->nvfuncs)
		return false;
	for (uint32_t i = 0; i < cls->nvfuncs; i++) {
		const cxx_entity *e = cls->vfuncs[i];

		if (!cxx_vt_is_dtor(e) || e->member_of == cls)
			continue;
		/* a base's virtual destructor and none declared here: the implicit one is the base's if this class's
		   own members need no destruction (else: an implicit destructor, not built yet) */
		for (uint32_t k = 0; k < cls->nfield_types; k++) {
			qty ft = cls->field_types[k];

			while (ft != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, ft) == TY_ARRAY)
				ft = AST_TYPE(c->ast, ft)->base;
			if (ft == CXX_TY_UNKNOWN)
				return false;
			if (cc_type_is_record(c->ast, ft) && cxx_gen_needs_destruction(c, ft, 0)) {
				/* (members to destroy: the class's own implicit destructor, made now; it overrides the
				   base's in the vtable) */
				cxx_entity *d;

				if (c->tentative || c->unevaluated || c->vt_making > 2)
					return false;
				c->vt_making++;
				d = cxx_gen_effective_destructor(c, cls->owner->ty, 0);
				c->vt_making--;
				if (!d || d->member_of != cls || !d->fn_virtual)
					return false;
				return cxx_vtable_usable(c, cls);
			}
		}
	}
	return true;
}

/* the vtable's symbol (_ZTV...), declared; the TU's end defines it where it belongs here (0: not built) */
VPRIV uint32_t cxx_vtable_symbol(cxx *c, cxx_scope *cls)
{
	cc *a = c->ast;
	char name[2048];
	attrs at;
	uint32_t s;

	if (cls->vt_sym)
		return cls->vt_sym;
	if (!cxx_vtable_usable(c, cls) || !cxx_mangle_class_data(c, "TV", cls->owner->ty, name, sizeof name))
		return 0;
	s = cc_symbol_new(a, S_VAR, cls->owner->name, cc_type_array(a, cc_type_pointer(a, cc_type_basic(TY_VOID)), cls->nvfuncs + 2, 0), 0);
	a->syms[s].flags |= SF_GLOBAL;
	a->syms[s].depth = 0;
	a->syms[s].sc = SC_EXTERN;
	memset(&at, 0, sizeof at);
	at.asmname = cxx_gen_text(c, name) + 1;
	cc_attribute_apply_symbol(a, &a->syms[s], &at);
	cls->vt_sym = s;
	if (c->nvt_list == c->cvt_list)
		c->vt_list = realloc(c->vt_list, (c->cvt_list = c->cvt_list ? c->cvt_list * 2 : 32) * sizeof *c->vt_list);
	c->vt_list[c->nvt_list++] = cls;
	/* a class template's instance: its virtual functions' bodies are wanted now (the vtable names them) */
	for (uint32_t i = 0; i < cls->nvfuncs; i++)
		if (cls->vfuncs[i]->late && !cls->vfuncs[i]->late_done)
			cxx_member_body_use(c, cls->vfuncs[i]);
	return s;
}

/* this's vptr = the class's vtable (past its two header words): the statement (0: not built) */
VPRIV uint32_t cxx_gen_vptr_store_at(cxx *c, cxx_scope *cls, uint32_t self, uint32_t loc)
{
	cc *a = c->ast;
	qty vp = cc_type_pointer(a, cc_type_basic(TY_VOID)), vppp = cc_type_pointer(a, cc_type_pointer(a, vp));
	uint32_t vs = cxx_vtable_symbol(c, cls), addr, target;

	if (!vs || !self)
		return 0;
	addr = cc_binary_build(a, LEX_TOKEN_PLUS, cc_conversion_decay(a, cxx_gen_var_node(c, vs, loc)),
			       cc_node_integer_new(a, 2, cc_type_basic(TY_LONG), loc), loc);
	target = cc_dereference_build(a, cc_cast_insert(a, self, vppp, cc_conversion_cast_kind(a, AST_NODE_AT(a, self)->ty, vppp, self)), loc);
	return cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, target, addr, loc), 0, 0);
}

VPRIV uint32_t cxx_gen_function_node(cxx *c, uint32_t s, uint32_t loc)
{
	uint32_t fn = cc_node_new(c->ast, N_VAR, c->ast->syms[s].ty, loc);

	AST_NODE_AT(c->ast, fn)->a = s;
	AST_NODE_AT(c->ast, fn)->flags = NF_LVALUE;
	return cc_node_rvalue(c->ast, fn);
}

VPRIV uint32_t cxx_gen_vptr_store(cxx *c, cxx_scope *cls, uint32_t loc)
{
	uint32_t self;

	cxx_gen_this(c, loc);
	self = c->enode;
	c->enode = 0;
	return cxx_gen_vptr_store_at(c, cls, self, loc);
}

/*
 * An object of a class with no constructor declared is default-initialized where it stands (target: an
 * lvalue naming it, without side effects): its bases and members of class type, then its vptr.  False: not
 * built (members with default initializers, arrays of classes with constructors).
 */
VPRIV bool cxx_gen_construct_at(cxx *c, uint32_t target, qty t, const uint32_t *args, uint32_t nargs, cc_id_list *out, uint32_t loc);

VPRIV bool cxx_gen_default_construct(cxx *c, uint32_t target, qty t, cc_id_list *out, uint32_t loc, int depth)
{
	cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t));

	if (!target)
		return false;
	if (!cxx_gen_needs_construction(c, t, 0))
		return true;
	if (!cls || depth > 8)
		return false;
	cxx_ctor_use(c, QUNQ(t), NULL, NULL, 0, false);
	if (c->ctor_found && c->ctor_picked && c->ctor_picked->member_of == cls)
		return cxx_gen_construct_at(c, target, QUNQ(t), NULL, 0, out, loc);
	for (cxx_entity *e = cxx_lookup_in(c, cls, cxx_ctor_id(), 0); e; e = e->next)
		if (e->member_of == cls)
			return false;	/* (constructors, none of them for no arguments) */
	if (cxx_class_has_member_initializer(c, t))
		return cxx_gen_construct_by_body(c, target, t, NULL, out, loc);
	return cxx_gen_default_construct_parts(c, target, t, out, loc, depth);
}

/* ... what the implicit default constructor does: the bases, the members of class type, the vptr */
VPRIV bool cxx_gen_default_construct_parts(cxx *c, uint32_t target, qty t, cc_id_list *out, uint32_t loc, int depth)
{
	cc *a = c->ast;
	cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t));
	qty bases[16];
	uint32_t nb;
	const tag *r;

	if (!target || !cls || depth > 8)
		return false;
	if (AST_TYPE_KIND(a, QUNQ(t)) == TY_UNION)
		return true;	/* (a union: no member is made active) */
	nb = cxx_class_bases(c, t, bases, 16);
	for (uint32_t i = 0; i < nb; i++)
		if (!cxx_gen_default_construct(c, cxx_gen_base_object(c, target, bases[i], loc), bases[i], out, loc, depth + 1))
			return false;
	r = &a->tags[AST_TYPE(a, QUNQ(t))->aux];
	for (uint32_t i = 0; i < r->nfield; i++) {
		qty ft = r->f[i].ty, et = ft;

		while (AST_TYPE_KIND(a, et) == TY_ARRAY)
			et = AST_TYPE(a, et)->base;
		if (!cc_type_is_record(a, et) || !cxx_gen_needs_construction(c, et, 0))
			continue;
		if (et != ft || !r->f[i].name)
			return false;
		if (!cxx_gen_default_construct(c, member(a, target, r->f[i].name, loc), ft, out, loc, depth + 1))
			return false;
	}
	if (cxx_class_polymorphic(c, t)) {
		uint32_t st = cxx_gen_vptr_store_at(c, cls, cc_address_of_build(a, target, loc), loc);

		if (!st)
			return false;
		cc_id_list_append(a, out, st);
	}
	return true;
}

/* a library function by its symbol name, void f(void *) or void f(void) */
VPRIV uint32_t cxx_gen_runtime_symbol(cxx *c, const char *name, bool pointer_param)
{
	cc *a = c->ast;
	qty ps[1] = { cc_type_pointer(a, cc_type_basic(TY_VOID)) };
	uint32_t s = cc_symbol_new(a, S_FUNC, vintern_cstr(name), cxx_type_function(c, cc_type_basic(TY_VOID), ps, pointer_param ? 1 : 0, false), 0);

	a->syms[s].flags |= SF_GLOBAL;
	a->syms[s].depth = 0;
	return s;
}

/* a virtual destructor is defined: the deleting destructor D0 = { D1(this); operator delete(this); } */
VPRIV void cxx_gen_deleting_destructor(cxx *c, cxx_entity *f)
{
	cc *a = c->ast;
	qty vp = cc_type_pointer(a, cc_type_basic(TY_VOID));
	uint32_t loc = a->syms[f->sym].loc, ps, self, call, body, saved_fn = a->fn;
	cc_id_list psyms, stmts, args;

	if (!f->sym0 || !f->sym1 || (a->syms[f->sym0].flags & SF_DEFINED))
		return;
	if (!c->gen_delete_sym)
		c->gen_delete_sym = cxx_gen_runtime_symbol(c, "_ZdlPv", true);
	cc_scope_open(a);
	a->fn = f->sym0;
	ps = cc_symbol_new(a, S_VAR, vintern_cstr("this"), cxx_gen_this_type(c, f), loc);
	a->syms[ps].flags |= SF_PARAM;
	a->syms[ps].sc = SC_AUTO;
	cc_id_list_init(&psyms);
	cc_id_list_append(a, &psyms, ps);
	cc_id_list_init(&stmts);
	cc_id_list_init(&args);
	cc_id_list_append(a, &args, cc_node_rvalue(a, cxx_gen_var_node(c, ps, loc)));
	call = cc_node_new(a, N_CALL, cc_type_basic(TY_VOID), loc);
	AST_NODE_AT(a, call)->a = cxx_gen_function_node(c, f->sym1, loc);
	AST_NODE_AT(a, call)->b = cc_id_list_freeze(a, &args);
	AST_NODE_AT(a, call)->c = 1;
	cc_id_list_append(a, &stmts, cc_statement_new(a, N_EXPR, loc, call, 0, 0));
	cc_id_list_init(&args);
	self = cc_node_rvalue(a, cxx_gen_var_node(c, ps, loc));
	cc_id_list_append(a, &args, cc_cast_insert(a, self, vp, cc_conversion_cast_kind(a, AST_NODE_AT(a, self)->ty, vp, self)));
	call = cc_node_new(a, N_CALL, cc_type_basic(TY_VOID), loc);
	AST_NODE_AT(a, call)->a = cxx_gen_function_node(c, c->gen_delete_sym, loc);
	AST_NODE_AT(a, call)->b = cc_id_list_freeze(a, &args);
	AST_NODE_AT(a, call)->c = 1;
	cc_id_list_append(a, &stmts, cc_statement_new(a, N_EXPR, loc, call, 0, 0));
	body = cc_statement_new(a, N_BLOCK, loc, 0, cc_id_list_freeze(a, &stmts), stmts.n);
	cc_scope_close(a);
	a->fn = saved_fn;
	a->syms[f->sym0].body = body;
	a->syms[f->sym0].flags |= SF_DEFINED;
	a->syms[f->sym0].defloc = loc;
	AST_NODE_AT(a, body)->a = cc_id_list_freeze(a, &psyms);
	AST_NODE_AT(a, body)->aux = 1;
	a->toplevel[a->ntoplevel++] = f->sym0;
}

/*
 * The TU's end: the vtables it defines.  A class with a key function (its first virtual function that is
 * neither pure nor inline) has its vtable where that function is defined; any other class (a class
 * template's instance too) in every TU that names it, weak.  The type info slot is null for now (RTTI:
 * not built yet).
 */
/*
 * At the end of the TU: a complete-object constructor or destructor (C1, D1) referred to and not defined,
 * whose base-object one (C2, D2: the same code without virtual bases) is defined here, is an alias of it
 * (two declarations of one inline member, one of them naming the C1 apart).
 */
VPRIV void cxx_gen_complete_object_aliases(cxx *c)
{
	cc *a = c->ast;
	struct { const char *name; uint32_t sym; } *t = (void *)c->sym_names;

	for (uint32_t k = 0; k < c->csym_names; k++) {
		const char *nm = t[k].name;
		uint32_t s = t[k].sym;
		size_t l;

		if (!nm || !s || (a->syms[s].flags & SF_DEFINED) || a->syms[s].kind != S_FUNC || (l = strlen(nm)) >= 2048)
			continue;
		for (size_t i = 3; i + 1 < l; i++) {
			char two[2048];
			uint32_t d;

			if ((nm[i] != 'C' && nm[i] != 'D') || nm[i + 1] != '1')
				continue;
			memcpy(two, nm, l + 1);
			two[i + 1] = '2';
			d = cxx_gen_symbol_named(c, two, 0);
			if (d && d != s && (a->syms[d].flags & SF_DEFINED) && a->syms[d].attrs) {
				attrs at;

				memset(&at, 0, sizeof at);
				at.alias = a->attr[a->syms[d].attrs].asmname;
				at.weak = 1;
				cc_attribute_apply_symbol(a, &a->syms[s], &at);
				a->syms[s].flags |= SF_DEFINED;
				a->toplevel[a->ntoplevel++] = s;
				break;
			}
		}
	}
}

VPRIV void cxx_gen_vtables_emit(cxx *c)
{
	cc *a = c->ast;
	qty vp = cc_type_pointer(a, cc_type_basic(TY_VOID));

	for (uint32_t i = 0; i < c->nvt_list; i++) {
		cxx_scope *cls = c->vt_list[i];
		sym *y = &a->syms[cls->vt_sym];
		const cxx_entity *key = NULL;
		cc_id_list v;
		uint32_t n, seen_dtor = 0;
		bool ok = true;

		for (uint32_t k = 0; k < cls->nvown && !key && !(cls->owner && cls->owner->instance); k++)
			if (!cls->vown[k]->fn_pure && !cls->vown[k]->fn_inline && !cls->vown[k]->fn_defaulted && !cls->vown[k]->fn_deleted)
				key = cls->vown[k];
		if (key && !(key->sym && (a->syms[key->sym].flags & SF_DEFINED)))
			continue;	/* (the TU that defines the key function has it) */
		cc_id_list_init(&v);
		for (uint32_t k = 0; k < cls->nvfuncs + 2 && ok; k++) {
			cxx_entity *e = k >= 2 ? cls->vfuncs[k - 2] : NULL;
			uint32_t val, s = 0;

			if (k == 1 && cls->owner && (s = cxx_gen_typeinfo(c, cls->owner->ty)) != 0)
				val = cxx_gen_data_address(c, s, 0);	/* the class's type info */
			else if (!e)
				val = cc_node_integer_new(a, 0, cc_type_basic(TY_LONG), 0);
			else {
				if (e->fn_pure) {
					if (!c->gen_pure_sym)
						c->gen_pure_sym = cxx_gen_runtime_symbol(c, "__cxa_pure_virtual", false);
					s = c->gen_pure_sym;
				} else if (e->fn_deleted || !cxx_gen_name(c, e))
					ok = false;
				else if (cxx_vt_is_dtor(e))
					s = seen_dtor++ ? e->sym0 : e->sym1;
				else
					s = e->sym;
				if (!s) {
					ok = false;
					break;
				}
				val = cxx_gen_function_node(c, s, 0);
			}
			cc_id_list_append(a, &v, k);
			cc_id_list_append(a, &v, k);
			cc_id_list_append(a, &v, cc_cast_insert(a, val, vp, cc_conversion_cast_kind(a, AST_NODE_AT(a, val)->ty, vp, val)));
		}
		if (!ok)
			continue;
		n = cc_node_new(a, N_INIT, y->ty, 0);
		AST_NODE_AT(a, n)->aux = 1;
		AST_NODE_AT(a, n)->c = v.n / 3;
		AST_NODE_AT(a, n)->b = cc_id_list_freeze(a, &v);
		if (!cxx_gen_static_ok(a, y->ty, n))
			continue;
		y = &a->syms[cls->vt_sym];
		y->sc = 0;
		y->body = n;
		y->flags |= SF_DEFINED;
		if (!key) {
			attrs at;

			memset(&at, 0, sizeof at);
			at.weak = 1;
			cc_attribute_apply_symbol(a, y, &at);
			y->flags |= SF_LINKONCE;	/* (emitted only if the TU's code refers to it) */
		}
		a->toplevel[a->ntoplevel++] = cls->vt_sym;
	}
	cxx_gen_typeinfos_emit(c);
}

/* ---- run-time type information and exceptions ------------------------------------------------ */

/* a data symbol by its assembler name, of type void *[n] (n 0: its size is not this TU's business) */
VPRIV uint32_t cxx_gen_data_symbol(cxx *c, const char *name, uint32_t n)
{
	cc *a = c->ast;
	attrs at;
	uint32_t s = cc_symbol_new(a, S_VAR, vintern_cstr("__ti"), cc_type_array(a, cc_type_pointer(a, cc_type_basic(TY_VOID)), n ? n : 1, 0), 0);

	a->syms[s].flags |= SF_GLOBAL;
	a->syms[s].depth = 0;
	a->syms[s].sc = SC_EXTERN;
	memset(&at, 0, sizeof at);
	at.asmname = cxx_gen_text(c, name) + 1;
	cc_attribute_apply_symbol(a, &a->syms[s], &at);
	return s;
}

/*
 * The type info object of type t (_ZTI...): a class's is declared here and defined at the TU's end where
 * it belongs to this TU; a fundamental type's (and a pointer to one's) is the C++ runtime's.  0: not built
 * (pointers to classes, functions, members).
 */
VPRIV uint32_t cxx_gen_typeinfo(cxx *c, qty t)
{
	cc *a = c->ast;
	char name[2048] = "_ZTI";
	cxx_scope *cls;
	int k;
	bool pod, empty;

	if (t == CXX_TY_UNKNOWN)
		return 0;
	t = QUNQ(t);
	k = AST_TYPE_KIND(a, t);
	if (k == TY_STRUCT || k == TY_UNION) {
		cls = cxx_tag_scope_get(c, t);
		if (!cls || !cxx_class_layout_info(c, t, &pod, &empty))
			return 0;
		if (cls->ti_sym)
			return cls->ti_sym;
		if (!cxx_mangle_type(c, t, name + 4, sizeof name - 4))
			return 0;
		cls->ti_sym = cxx_gen_data_symbol(c, name, 0);
		if (c->nti_list == c->cti_list)
			c->ti_list = realloc(c->ti_list, (c->cti_list = c->cti_list ? c->cti_list * 2 : 32) * sizeof *c->ti_list);
		c->ti_list[c->nti_list++] = cls;
		return cls->ti_sym;
	}
	if (k == TY_PTR && !AST_TYPE(a, t)->ext) {
		/* the runtime has void *, char * and the like (and their const forms) */
		qty b = AST_TYPE(a, t)->base;
		int bk = b == CXX_TY_UNKNOWN ? -1 : (int)AST_TYPE_KIND(a, b);

		if (!(bk == TY_VOID || (bk >= TY_BOOL && bk <= TY_ULLONG) || bk == TY_FLOAT || bk == TY_DOUBLE))
			return 0;
	} else if (!((k >= TY_BOOL && k <= TY_ULLONG) || k == TY_FLOAT || k == TY_DOUBLE))
		return 0;
	if (!cxx_mangle_type(c, t, name + 4, sizeof name - 4))
		return 0;
	return cxx_gen_data_symbol(c, name, 0);
}

/* &sym[index] as a void * constant (an initializer's element) */
VPRIV uint32_t cxx_gen_data_address(cxx *c, uint32_t s, uint32_t index)
{
	cc *a = c->ast;
	qty vp = cc_type_pointer(a, cc_type_basic(TY_VOID));
	uint32_t n = cc_conversion_decay(a, cxx_gen_var_node(c, s, 0));

	if (index)
		n = cc_binary_build(a, LEX_TOKEN_PLUS, n, cc_node_integer_new(a, index, cc_type_basic(TY_LONG), 0), 0);
	return cc_cast_insert(a, n, vp, cc_conversion_cast_kind(a, AST_NODE_AT(a, n)->ty, vp, n));
}

/* the vtable's key function rule, for a class's type info too: does this TU define the class's vtable and RTTI? */
VPRIV bool cxx_gen_class_data_here(cxx *c, cxx_scope *cls)
{
	cc *a = c->ast;
	const cxx_entity *key = NULL;

	for (uint32_t k = 0; k < cls->nvown && !key && !(cls->owner && cls->owner->instance); k++)
		if (!cls->vown[k]->fn_pure && !cls->vown[k]->fn_inline && !cls->vown[k]->fn_defaulted && !cls->vown[k]->fn_deleted)
			key = cls->vown[k];
	return !key || (key->sym && (a->syms[key->sym].flags & SF_DEFINED));
}

/*
 * The TU's end: the type info objects it defines (weak; a class with a key function has its own where that
 * function is defined, as its vtable): _ZTS, the type's name, and _ZTI, an object of the runtime's
 * __class_type_info (no bases), __si_class_type_info (one base at offset 0) or __vmi_class_type_info.
 */
VPRIV void cxx_gen_typeinfos_emit(cxx *c)
{
	cc *a = c->ast;
	qty vp = cc_type_pointer(a, cc_type_basic(TY_VOID));
	uint32_t vt_class = 0, vt_si = 0, vt_vmi = 0;

	for (uint32_t i = 0; i < c->nti_list; i++) {	/* (the list grows: a base's type info) */
		cxx_scope *cls = c->ti_list[i];
		qty t = cls->owner->ty, bases[16];
		uint32_t nb = cxx_class_bases(c, t, bases, 16), bti[16], n, words = 0, ts, si, vals[40];
		char name[2048] = "_ZTS";
		uint64_t off[16];
		cc_id_list v;
		attrs at;
		sym *y;
		bool ok = true;

		if ((cls->nvfuncs || cls->nvown) && !cxx_gen_class_data_here(c, cls))
			continue;	/* (the key function's TU has it) */
		if (!cxx_mangle_type(c, t, name + 4, sizeof name - 4))
			continue;
		for (uint32_t k = 0; k < nb && ok; k++)
			ok = (bti[k] = cxx_gen_typeinfo(c, bases[k])) != 0 && cxx_class_base_offset(c, t, bases[k], &off[k], 0);
		if (!ok)
			continue;
		/* the name: a weak array of char */
		si = cxx_gen_text(c, name + 4);
		ts = cc_symbol_new(a, S_VAR, vintern_cstr("__ts"), cc_type_array(a, QT(TY_CHAR, Q_CONST), strlen(name + 4) + 1, 0), 0);
		a->syms[ts].flags |= SF_GLOBAL;
		a->syms[ts].depth = 0;
		memset(&at, 0, sizeof at);
		at.asmname = cxx_gen_text(c, name) + 1;
		at.weak = 1;
		cc_attribute_apply_symbol(a, &a->syms[ts], &at);
		a->syms[ts].flags |= SF_LINKONCE;
		n = cc_node_new(a, N_STR, a->syms[ts].ty, 0);
		AST_NODE_AT(a, n)->a = si;
		AST_NODE_AT(a, n)->flags = NF_LVALUE;
		a->syms[ts].body = n;
		a->syms[ts].flags |= SF_DEFINED;
		a->toplevel[a->ntoplevel++] = ts;
		/* the object's words */
		if (!nb) {
			if (!vt_class)
				vt_class = cxx_gen_data_symbol(c, "_ZTVN10__cxxabiv117__class_type_infoE", 0);
			vals[words++] = cxx_gen_data_address(c, vt_class, 2);
			vals[words++] = cxx_gen_data_address(c, ts, 0);
		} else if (nb == 1 && !off[0]) {
			if (!vt_si)
				vt_si = cxx_gen_data_symbol(c, "_ZTVN10__cxxabiv120__si_class_type_infoE", 0);
			vals[words++] = cxx_gen_data_address(c, vt_si, 2);
			vals[words++] = cxx_gen_data_address(c, ts, 0);
			vals[words++] = cxx_gen_data_address(c, bti[0], 0);
		} else {
			if (!vt_vmi)
				vt_vmi = cxx_gen_data_symbol(c, "_ZTVN10__cxxabiv121__vmi_class_type_infoE", 0);
			vals[words++] = cxx_gen_data_address(c, vt_vmi, 2);
			vals[words++] = cxx_gen_data_address(c, ts, 0);
			/* flags (none) and the number of bases, two unsigned ints; then each base and its offset << 8 | public */
			n = cc_node_integer_new(a, (i128)((uint64_t)nb << 32), cc_type_basic(TY_LONG), 0);
			vals[words++] = cc_cast_insert(a, n, vp, cc_conversion_cast_kind(a, cc_type_basic(TY_LONG), vp, n));
			for (uint32_t k = 0; k < nb; k++) {
				vals[words++] = cxx_gen_data_address(c, bti[k], 0);
				n = cc_node_integer_new(a, (i128)((off[k] << 8) | 2), cc_type_basic(TY_LONG), 0);
				vals[words++] = cc_cast_insert(a, n, vp, cc_conversion_cast_kind(a, cc_type_basic(TY_LONG), vp, n));
			}
		}
		cc_id_list_init(&v);
		for (uint32_t k = 0; k < words; k++) {
			cc_id_list_append(a, &v, k);
			cc_id_list_append(a, &v, k);
			cc_id_list_append(a, &v, vals[k]);
		}
		y = &a->syms[cls->ti_sym];
		y->ty = cc_type_array(a, vp, words, 0);
		n = cc_node_new(a, N_INIT, y->ty, 0);
		AST_NODE_AT(a, n)->aux = 1;
		AST_NODE_AT(a, n)->c = words;
		AST_NODE_AT(a, n)->b = cc_id_list_freeze(a, &v);
		if (!cxx_gen_static_ok(a, y->ty, n))
			continue;
		y = &a->syms[cls->ti_sym];
		y->sc = 0;
		y->body = n;
		y->flags |= SF_DEFINED;
		memset(&at, 0, sizeof at);
		at.weak = 1;
		cc_attribute_apply_symbol(a, y, &at);
		y->flags |= SF_LINKONCE;
		a->toplevel[a->ntoplevel++] = cls->ti_sym;
	}
}

/* the exception runtime's functions (libc++abi), by index */
VPRIV uint32_t cxx_gen_eh_symbol(cxx *c, int which)
{
	static const struct { const char *name; char ret, np; } fns[] = {
		{ "__cxa_allocate_exception", 'p', 1 }, { "__cxa_throw", 'v', 3 }, { "__cxa_rethrow", 'v', 0 },
		{ "__cxa_begin_catch", 'p', 1 }, { "__cxa_end_catch", 'v', 1 },	/* (called as a scope's cleanup: with its address) */
	};
	cc *a = c->ast;
	qty vp = cc_type_pointer(a, cc_type_basic(TY_VOID)), ps[3] = { vp, vp, vp };

	if (!c->gen_eh_syms[which]) {
		uint32_t s;

		if (which == 0)
			ps[0] = cc_type_basic(TY_ULONG);
		s = cc_symbol_new(a, S_FUNC, vintern_cstr(fns[which].name), cxx_type_function(c, fns[which].ret == 'p' ? vp : cc_type_basic(TY_VOID), ps, (uint32_t)fns[which].np, false), 0);
		a->syms[s].flags |= SF_GLOBAL;
		a->syms[s].depth = 0;
		c->gen_eh_syms[which] = s;
	}
	return c->gen_eh_syms[which];
}

VPRIV uint32_t cxx_gen_runtime_call(cxx *c, uint32_t fs, const uint32_t *args, uint32_t nargs, uint32_t loc)
{
	cc *a = c->ast;
	cc_id_list l;
	uint32_t n;

	cc_id_list_init(&l);
	for (uint32_t i = 0; i < nargs; i++)
		cc_id_list_append(a, &l, args[i]);
	n = cc_node_new(a, N_CALL, AST_TYPE(a, a->syms[fs].ty)->base, loc);
	AST_NODE_AT(a, n)->a = cxx_gen_function_node(c, fs, loc);
	AST_NODE_AT(a, n)->b = cc_id_list_freeze(a, &l);
	AST_NODE_AT(a, n)->c = nargs;
	return n;
}

VPRIV uint32_t cxx_gen_to_void_pointer(cxx *c, uint32_t n)
{
	cc *a = c->ast;
	qty vp = cc_type_pointer(a, cc_type_basic(TY_VOID));

	return cc_cast_insert(a, n, vp, cc_conversion_cast_kind(a, AST_NODE_AT(a, n)->ty, vp, n));
}
