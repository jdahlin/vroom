/* C++ code generation: members, this, the symbols of functions and variables (their mangled names) */
#include "codegen.h"

/* base.name or base->name, a data member */
VPRIV void cxx_gen_member(cxx *c, uint32_t base, bool arrow, uint32_t name, uint32_t loc)
{
	cc *a = c->ast;
	qty bt;

	c->enode = 0;
	if (!cxx_gen_on(c) || !base)
		return;
	if (arrow)
		base = cc_dereference_build(a, cc_conversion_decay(a, base), loc);
	(void)bt;
	c->enode = cxx_gen_member_of(c, base, name, loc, 0);	/* (0: a static member, or one of a class not laid out) */
	if (c->enode && (QQ(AST_NODE_AT(a, c->enode)->ty) & Q_CONST) && cc_type_is_record(a, AST_NODE_AT(a, base)->ty)) {
		/* a mutable member of a const object: not const */
		cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(AST_NODE_AT(a, base)->ty));
		cxx_entity *fe = cls ? cxx_lookup_in(c, cls, name, 0) : NULL;

		while (fe && !(fe->kind == CXX_ENTITY_VALUE && fe->field_of))
			fe = fe->next;
		if (fe && fe->is_mutable && fe->ty != CXX_TY_UNKNOWN && !(QQ(fe->ty) & Q_CONST)) {
			qty pt = cc_type_pointer(a, QUNQ(AST_NODE_AT(a, c->enode)->ty));
			uint32_t addr = cc_address_of_build(a, c->enode, loc);

			c->enode = cc_dereference_build(a, cc_cast_insert(a, addr, pt, cc_conversion_cast_kind(a, AST_NODE_AT(a, addr)->ty, pt, addr)), loc);
		}
	}
	if (c->enode && cxx_gen_is_reference(c, AST_NODE_AT(a, c->enode)->ty))
		c->enode = cxx_gen_referent(c, c->enode, loc);
}

/* the symbol a complete object's destruction calls: D2 for an inline destructor (a class without virtual
   bases: D1 does the same, and clang defines only D2 where it is inline, std::map's ~map), D1 otherwise */
VPRIV uint32_t cxx_gen_dtor_sym(const cxx_entity *d)
{
	bool inl = d->fn_inline || (d->member_of && d->member_of->owner && d->member_of->owner->instance);

	return inl && d->sym ? d->sym : d->sym1;
}

/* is the scope in an unnamed namespace (itself one, or inside one: its names have internal linkage) */
VPRIV bool cxx_scope_in_unnamed_namespace(cxx *c, const cxx_scope *s)
{
	for (; s; s = s->parent)
		if (s->kind == CXX_SCOPE_NAMESPACE && !s->owner && s != c->global)
			return true;
	return false;
}

/*
 * The function's symbol name: C++'s mangled name (mangle.c), or the plain name for main and extern "C".
 * False: a name not mangled yet.
 */
/* is f a member function called on an object (not static)? */
VPRIV bool cxx_gen_has_this(const cxx_entity *f)
{
	return f->member_of && f->member_of->kind == CXX_SCOPE_CLASS && !f->fn_static;
}

/* the class a member function belongs to (its this is a pointer to it, const for a const member) */
VPRIV qty cxx_gen_this_type(cxx *c, const cxx_entity *f)
{
	qty ct = f->member_of->owner ? f->member_of->owner->ty : CXX_TY_UNKNOWN;

	if (ct == CXX_TY_UNKNOWN)
		return CXX_TY_UNKNOWN;
	return cc_type_pointer(c->ast, f->fn_const ? QT(QI(ct), QQ(ct) | Q_CONST) : ct);
}

/* a string of the AST (an assembler name) */
VPRIV uint32_t cxx_gen_text(cxx *c, const char *text)
{
	cc *a = c->ast;
	size_t n = strlen(text);
	char *keep = cc_heap_allocate(a, n + 1);
	uint32_t si = a->nstr++;

	memcpy(keep, text, n + 1);
	a->strs[si] = (cstr){ keep, (uint32_t)n + 1, 1, LEX_TOKEN_STRING };
	return si;
}

VPRIV bool cxx_gen_is_special(const cxx_entity *f)
{
	return vintern_ents[f->name].len && vintern_ents[f->name].s[0] == ' ';	/* a constructor or destructor */
}

/*
 * The function's type as the ABI calls it: this first for a member function; a class that is not trivial
 * for calls is passed by the address of a temporary, and returned in an object whose address comes first
 * of all (x86-64's convention written out: the pointer in rdi, returned in rax).
 */
VPRIV qty cxx_gen_abi_type(cxx *c, const cxx_entity *f)
{
	cc *a = c->ast;
	const ctype *ft = AST_TYPE(a, f->ty);
	qty ps[34], ret = ft->base;
	uint32_t np = 0;
	bool any = false;

	if (ft->n > 31)
		return CXX_TY_UNKNOWN;
	if (cxx_gen_by_address(c, ret)) {
		ps[np++] = cc_type_pointer(a, QUNQ(ret));
		ret = ps[0], any = true;
	}
	if (cxx_gen_has_this(f)) {
		qty tt = cxx_gen_this_type(c, f);

		if (tt == CXX_TY_UNKNOWN || (cxx_class_polymorphic(c, AST_TYPE(a, tt)->base) && !cxx_vtable_usable(c, f->member_of)))
			return CXX_TY_UNKNOWN;	/* (a class with virtual functions whose vtable is not built yet) */
		ps[np++] = tt, any = true;
	}
	for (uint32_t i = 0; i < ft->n; i++) {
		qty p = a->params[ft->aux + i];

		if (cxx_gen_by_address(c, p))
			p = cc_type_pointer(a, QUNQ(p)), any = true;
		ps[np++] = p;
	}
	if (!any)
		return f->ty;
	ft = AST_TYPE(a, f->ty);
	return cxx_type_function(c, ret, ps, np, (ft->flags & TF_VARIADIC) != 0);
}

/* has a function by this mangled name been begun already (else it is entered): a hash set, open addressing
   (a list searched with strcmp was 9 % of a json test's compile time) */
VPRIV bool cxx_gen_name_defined(cxx *c, const char *nm)
{
	uint32_t h = 2166136261u, mask, i;

	for (const char *p = nm; *p; p++)
		h = (h ^ (unsigned char)*p) * 16777619u;
	if (c->ngen_names * 2 >= c->cgen_names) {
		uint32_t ocap = c->cgen_names, ncap = ocap ? ocap * 2 : 1024;
		char **old = c->gen_names;

		c->gen_names = calloc(ncap, sizeof *c->gen_names);
		c->cgen_names = ncap, c->ngen_names = 0;
		for (uint32_t k = 0; k < ocap; k++)
			if (old[k])
				cxx_gen_name_defined(c, old[k]);
		free(old);
	}
	mask = c->cgen_names - 1;
	for (i = h & mask; c->gen_names[i]; i = (i + 1) & mask)
		if (!strcmp(c->gen_names[i], nm))
			return true;
	c->gen_names[i] = (char *)nm;
	c->ngen_names++;
	return false;
}

/* the function symbol with this assembler name: the one made first (0: none yet; then `s` is entered, if not 0) */
VPRIV uint32_t cxx_gen_symbol_named(cxx *c, const char *name, uint32_t s)
{
	uint32_t h = 2166136261u, mask, i;

	for (const char *p = name; *p; p++)
		h = (h ^ (unsigned char)*p) * 16777619u;
	if (s && c->nsym_names * 2 >= c->csym_names) {	/* (grown on insertion only: a lookup leaves the table, which a caller may be walking) */
		uint32_t ncap = c->csym_names ? c->csym_names * 2 : 1024;
		void *old = c->sym_names;
		uint32_t ocap = c->csym_names;

		c->sym_names = calloc(ncap, sizeof *c->sym_names);
		c->csym_names = ncap, c->nsym_names = 0;
		for (uint32_t k = 0; k < ocap; k++) {
			struct { const char *name; uint32_t sym; } *o = old;

			if (o[k].name)
				cxx_gen_symbol_named(c, o[k].name, o[k].sym);
		}
		free(old);
	}
	if (!c->csym_names)
		return 0;	/* (a lookup in no table yet) */
	mask = c->csym_names - 1;
	for (i = h & mask; c->sym_names[i].name; i = (i + 1) & mask)
		if (!strcmp(c->sym_names[i].name, name))
			return c->sym_names[i].sym;
	if (s) {
		c->sym_names[i].name = name, c->sym_names[i].sym = s;
		c->nsym_names++;
	}
	return 0;
}

/* a variable's symbol named as C++ mangles it (a namespace's variable, a static data member; one at the
   global scope or extern "C" keeps its plain name).  False: not mangled (its symbol is dropped: a use of
   it builds no code, rather than code under the wrong name). */
VPRIV bool cxx_gen_variable_name(cxx *c, cxx_entity *v, bool internal)
{
	cc *a = c->ast;
	char name[1024];
	attrs at;
	cxx_scope *home = v->home;

	if (!v->sym || v->c_linkage || !home || home == c->global)
		return true;
	if (!cxx_mangle_variable(c, v, home, internal, name, sizeof name)) {
		v->sym = 0;
		return false;
	}
	{
		uint32_t have = cxx_gen_symbol_named(c, name, 0);

		if (have) {	/* (another declaration of the same variable) */
			v->sym = have;
			return true;
		}
	}
	memset(&at, 0, sizeof at);
	at.asmname = cxx_gen_text(c, name) + 1;
	cc_attribute_apply_symbol(a, &a->syms[v->sym], &at);
	cxx_gen_symbol_named(c, a->strs[at.asmname - 1].s, v->sym);
	return true;
}

/*
 * The function's symbols and their names: C++'s mangled name (mangle.c), or the plain name for main and
 * extern "C".  A member function's symbol is made here (its type with this first); an inline function's is
 * weak (every TU that uses it defines it: no COMDAT group yet).  A constructor or destructor has two: sym
 * the base-object function (C2, D2: the one with the body) and sym1 the complete-object one (C1, D1: what
 * is called; an alias of the other where the body is).  False: a name not mangled yet.
 */
VPRIV bool cxx_gen_name(cxx *c, cxx_entity *f)
{
	cc *a = c->ast;
	const vident *v = &vintern_ents[f->name];
	char name[2048];
	attrs at;
	cxx_scope *home = f->member_of ? f->member_of : f->home;
	bool special = cxx_gen_is_special(f);

	if (f->mangled || f->c_linkage)
		return true;
	if (!home || f->ty == CXX_TY_UNKNOWN || AST_TYPE_KIND(a, f->ty) != TY_FUNC)
		return false;
	if (home == c->global && v->len == 4 && !memcmp(v->s, "main", 4)) {
		f->mangled = 1;
		return true;
	}
	c->mangle_guess = 0, c->mangle_internal = 0;
	if (!cxx_mangle_function(c, f, home, f->ty, f->member_of && f->fn_const, special ? 2 : 0, name, sizeof name))
		return false;
	bool internal_name = c->mangle_internal;	/* (an instance over a local or unnamed namespace's type: internal, as clang's) */
	f->name_guess = c->mangle_guess;
	if (f->name_guess && getenv("VCC_CXX_MANGLE_WHY"))
		fprintf(stderr, "name guessed: %s\n", name);	/* (VCC_CXX_MANGLE_WHY: the names only vcc gives) */
	{
		/* another entity for the same function (the same mangled name) has the symbol already */
		uint32_t have = cxx_gen_symbol_named(c, name, 0);

		if (have) {
			f->sym = have;
			f->sym1 = special ? have : 0;
			if (special) {
				/* (its other names: looked up the same way below, or the one symbol for an inline one) */
				char n1[2048];

				if (cxx_mangle_function(c, f, home, f->ty, false, 1, n1, sizeof n1) && cxx_gen_symbol_named(c, n1, 0))
					f->sym1 = cxx_gen_symbol_named(c, n1, 0);
				if (v->s[1] != 'c' && cxx_mangle_function(c, f, home, f->ty, false, 3, n1, sizeof n1))
					f->sym0 = cxx_gen_symbol_named(c, n1, 0);
			}
			f->mangled = 1;
			return true;
		}
	}
	if (!f->sym || cxx_gen_abi_type(c, f) != f->ty) {
		qty st = cxx_gen_abi_type(c, f);

		if (st == CXX_TY_UNKNOWN)
			return false;
		f->sym = cc_symbol_new(a, S_FUNC, f->name, st, 0);
		if ((!f->fn_internal || f->member_of) && !cxx_scope_in_unnamed_namespace(c, home))
			a->syms[f->sym].flags |= SF_GLOBAL;	/* (in an unnamed namespace, a class's member too: internal) */
		a->syms[f->sym].depth = 0;	/* (made while a body is being built: a file-scope symbol all the same) */
	}
	if (cxx_scope_in_unnamed_namespace(c, home) || internal_name) {
		a->syms[f->sym].flags &= ~SF_GLOBAL;	/* (internal linkage, however the symbol was made) */
		a->syms[f->sym].sc = SC_STATIC;
	}
	memset(&at, 0, sizeof at);
	at.asmname = cxx_gen_text(c, name) + 1;
	cc_attribute_apply_symbol(a, &a->syms[f->sym], &at);
	cxx_gen_symbol_named(c, a->strs[at.asmname - 1].s, f->sym);
	if (special && f->fn_inline)
		f->sym1 = f->sym;	/* (an inline one: every TU that uses it has its own, so the one function, C2 or D2, does for both names) */
	if (special && !f->fn_inline) {
		if (!cxx_mangle_function(c, f, home, f->ty, false, 1, name, sizeof name))
			return false;
		f->sym1 = cc_symbol_new(a, S_FUNC, f->name, a->syms[f->sym].ty, 0);
		if (a->syms[f->sym].flags & SF_GLOBAL)
			a->syms[f->sym1].flags |= SF_GLOBAL;
		else
			a->syms[f->sym1].sc = SC_STATIC;	/* (internal as the other) */
		a->syms[f->sym1].depth = 0;
		memset(&at, 0, sizeof at);
		at.asmname = cxx_gen_text(c, name) + 1;
		cc_attribute_apply_symbol(a, &a->syms[f->sym1], &at);
		cxx_gen_symbol_named(c, a->strs[at.asmname - 1].s, f->sym1);
	}
	if (special && f->fn_virtual && v->s[1] != 'c') {
		/* a virtual destructor: the deleting destructor too */
		if (!cxx_mangle_function(c, f, home, f->ty, false, 3, name, sizeof name))
			return false;
		f->sym0 = cc_symbol_new(a, S_FUNC, f->name, a->syms[f->sym].ty, 0);
		a->syms[f->sym0].flags |= SF_GLOBAL;
		a->syms[f->sym0].depth = 0;
		memset(&at, 0, sizeof at);
		at.asmname = cxx_gen_text(c, name) + 1;
		cc_attribute_apply_symbol(a, &a->syms[f->sym0], &at);
		cxx_gen_symbol_named(c, a->strs[at.asmname - 1].s, f->sym0);
	}
	f->mangled = 1;
	return true;
}

/* this, in a member function's body */
VPRIV void cxx_gen_this(cxx *c, uint32_t loc)
{
	uint32_t n;

	if (!cxx_gen_on(c) || !c->gen_this)
		return;
	if (c->gen_lambda) {
		/* in a lambda's body: the enclosing function's this, captured */
		int k = cxx_gen_capture(c, c->gen_lambda, NULL, loc);

		if (k < 0) {
			cxx_gen_fail(c);
			return;
		}
		c->enode = cc_node_rvalue(c->ast, cxx_gen_var_node(c, c->gen_lambda->cap[k].local, loc));
		return;
	}
	n = cc_node_new(c->ast, N_VAR, c->ast->syms[c->gen_this].ty, loc);
	AST_NODE_AT(c->ast, n)->a = c->gen_this;
	AST_NODE_AT(c->ast, n)->flags = NF_LVALUE;
	c->enode = cc_node_rvalue(c->ast, n);
}

/* the member function of class cls named name with these parameter types and constness (an out-of-class definition's) */
VPRIV cxx_entity *cxx_member_function_find(cxx *c, cxx_scope *cls, uint32_t name, const cxx_param *ps, uint32_t nps, bool is_const)
{
	cc *a = c->ast;

	for (cxx_entity *e = cxx_lookup_in(c, cls, name, 0); e; e = e->next) {
		const ctype *ft;
		bool same = true;

		if (e->kind != CXX_ENTITY_VALUE || e->member_of != cls || e->ty == CXX_TY_UNKNOWN || AST_TYPE_KIND(a, e->ty) != TY_FUNC)
			continue;
		ft = AST_TYPE(a, e->ty);
		if (ft->n != nps || (bool)e->fn_const != is_const)
			continue;
		for (uint32_t i = 0; i < nps && same; i++)
			same = ps[i].ty != CXX_TY_UNKNOWN && QUNQ(a->params[ft->aux + i]) == QUNQ(ps[i].ty);
		if (same)
			return e;
	}
	return NULL;
}
