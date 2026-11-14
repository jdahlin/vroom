/* C++ code generation: calls: of functions, member functions, through pointers, of overloaded operators */
#include "codegen.h"

/* a function named as a value (not called): its designator */
VPRIV void cxx_gen_function_value(cxx *c, cxx_entity *f, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t n;

	if (!cxx_gen_on(c))
		return;
	c->enode = 0;
	if (!cxx_gen_name(c, f) || !f->sym)
		return;
	n = cc_node_new(a, N_VAR, a->syms[f->sym].ty, loc);
	AST_NODE_AT(a, n)->a = f->sym;
	AST_NODE_AT(a, n)->flags = NF_LVALUE;
	c->enode = n;
}

/* f(args): the function overload resolution picked, the arguments converted to its parameters' types */
/* VCC_CXX_GEN=3: where cxx_gen_call gave up on a call, and the function called */
#define CXX_CALL_WHY() do { \
		static __thread int why_ = -1; \
		if (why_ < 0) \
			why_ = getenv("VCC_CXX_GEN") && atoi(getenv("VCC_CXX_GEN")) >= 3; \
		if (why_ && f && cxx_gen_on(c)) \
			fprintf(stderr, "no call (codegen.c:%d): %.*s/%u%s%.*s\n", __LINE__, (int)vintern_ents[f->name].len, vintern_ents[f->name].s, nargs, \
				f->member_of && f->member_of->owner ? " of " : "", f->member_of && f->member_of->owner ? (int)vintern_ents[f->member_of->owner->name].len : 0, \
				f->member_of && f->member_of->owner ? vintern_ents[f->member_of->owner->name].s : ""); \
	} while (0)

/* operator==(X, X) = default of a class whose members are scalars: the members compared in order, inline (0: not built) */
VPRIV uint32_t cxx_gen_defaulted_eq(cxx *c, uint32_t x, uint32_t y, uint32_t loc)
{
	cc *a = c->ast;
	qty t = QUNQ(AST_NODE_AT(a, x)->ty);
	const tag *r;
	uint32_t e = 0;

	if (!cc_type_is_record(a, t) || QUNQ(AST_NODE_AT(a, y)->ty) != t || AST_TYPE_KIND(a, t) != TY_STRUCT ||
	    !(AST_NODE_AT(a, x)->flags & NF_LVALUE) || !(AST_NODE_AT(a, y)->flags & NF_LVALUE))
		return 0;
	r = &a->tags[AST_TYPE(a, t)->aux];
	for (uint32_t i = 0; i < r->nfield; i++) {
		int k = AST_TYPE_KIND(a, r->f[i].ty);
		uint32_t q;

		if (!r->f[i].name || !((k >= TY_BOOL && k <= TY_LDOUBLE) || k == TY_PTR || k == TY_ENUM))
			return 0;
		q = cc_binary_build(a, LEX_TOKEN_EQUAL_EQUAL, cc_node_rvalue(a, member(a, x, r->f[i].name, loc)),
				    cc_node_rvalue(a, member(a, y, r->f[i].name, loc)), loc);
		e = e ? cc_binary_build(a, LEX_TOKEN_AMPERSAND_AMPERSAND, e, q, loc) : q;
	}
	return e ? cc_conversion_apply(a, e, cc_type_basic(TY_BOOL)) : cc_node_integer_new(a, 1, cc_type_basic(TY_BOOL), loc);
}

/* is f std::forward, std::move, std::move_if_noexcept or std::as_const (in std or libc++'s std::__1)? */
VPRIV bool cxx_gen_std_namespace_function(cxx *c, const cxx_entity *f);

VPRIV bool cxx_gen_std_cast_function(cxx *c, const cxx_entity *f)
{
	const vident *v = &vintern_ents[f->name];

	if (!((v->len == 7 && !memcmp(v->s, "forward", 7)) || (v->len == 4 && !memcmp(v->s, "move", 4)) ||
	      (v->len == 16 && !memcmp(v->s, "move_if_noexcept", 16)) || (v->len == 8 && !memcmp(v->s, "as_const", 8))))
		return false;
	return cxx_gen_std_namespace_function(c, f);
}

/* is f a function of namespace std (or libc++'s std::__1), not a member */
VPRIV bool cxx_gen_std_namespace_function(cxx *c, const cxx_entity *f)
{
	const cxx_scope *s = f->home;
	bool std_ns = false;

	if (!s || f->member_of || s->kind != CXX_SCOPE_NAMESPACE || !s->owner)
		return false;
	if (s->parent == c->global)
		std_ns = vintern_ents[s->owner->name].len == 3 && !memcmp(vintern_ents[s->owner->name].s, "std", 3);
	else if (s->parent && s->parent->kind == CXX_SCOPE_NAMESPACE && s->parent->owner && s->parent->parent == c->global)
		std_ns = vintern_ents[s->parent->owner->name].len == 3 && !memcmp(vintern_ents[s->parent->owner->name].s, "std", 3);
	return std_ns;
}

/* a cleanup function for a local array of n objects of class E whose destructor is dsym: void f(E (*p)[n]),
   the elements destroyed last to first (an initializer_list's array as an argument: __attribute__((cleanup)));
   one per destructor and length */
VPRIV uint32_t cxx_gen_array_cleanup(cxx *c, qty et, uint32_t n, uint32_t dsym, uint32_t loc)
{
	static __thread struct { cc *a; uint32_t dsym, n, fn; } cache[64];
	static __thread uint32_t ncache;
	cc *a = c->ast;
	qty at = cc_type_array(a, QUNQ(et), n, 0), pt = cc_type_pointer(a, at), ept = cc_type_pointer(a, QUNQ(et));
	uint32_t fsym, ps, body, saved_fn = a->fn;
	cc_id_list psyms, stmts;
	char name[48];

	for (uint32_t i = 0; i < ncache; i++)
		if (cache[i].a == a && cache[i].dsym == dsym && cache[i].n == n)
			return cache[i].fn;
	snprintf(name, sizeof name, "__vcc_ilc_%u_%u", dsym, n);
	fsym = cc_symbol_new(a, S_FUNC, vintern_cstr(name), cxx_type_function(c, cc_type_basic(TY_VOID), &pt, 1, false), loc);
	a->syms[fsym].sc = SC_STATIC;
	a->syms[fsym].depth = 0;
	cc_scope_open(a);
	a->fn = fsym;
	ps = cc_symbol_new(a, S_VAR, vintern_cstr("p"), pt, loc);
	a->syms[ps].flags |= SF_PARAM;
	a->syms[ps].sc = SC_AUTO;
	cc_id_list_init(&psyms);
	cc_id_list_append(a, &psyms, ps);
	cc_id_list_init(&stmts);
	for (uint32_t i = n; i-- > 0;) {
		uint32_t base = cc_node_rvalue(a, cxx_gen_var_node(c, ps, loc)), el, call;
		cc_id_list args;

		base = cc_cast_insert(a, base, ept, cc_conversion_cast_kind(a, pt, ept, base));
		el = cc_binary_build(a, LEX_TOKEN_PLUS, base, cc_node_integer_new(a, i, cc_type_basic(TY_LONG), loc), loc);
		cc_id_list_init(&args);
		cc_id_list_append(a, &args, el);
		call = cc_node_new(a, N_CALL, cc_type_basic(TY_VOID), loc);
		AST_NODE_AT(a, call)->a = cxx_gen_function_node(c, dsym, loc);
		AST_NODE_AT(a, call)->b = cc_id_list_freeze(a, &args);
		AST_NODE_AT(a, call)->c = 1;
		cc_id_list_append(a, &stmts, cc_statement_new(a, N_EXPR, loc, call, 0, 0));
	}
	body = cc_statement_new(a, N_BLOCK, loc, 0, cc_id_list_freeze(a, &stmts), stmts.n);
	cc_scope_close(a);
	a->fn = saved_fn;
	a->syms[fsym].body = body;
	a->syms[fsym].flags |= SF_DEFINED;
	a->syms[fsym].defloc = loc;
	AST_NODE_AT(a, body)->a = cc_id_list_freeze(a, &psyms);
	AST_NODE_AT(a, body)->aux = 1;
	a->toplevel[a->ntoplevel++] = fsym;
	if (ncache < 64)
		cache[ncache].a = a, cache[ncache].dsym = dsym, cache[ncache].n = n, cache[ncache++].fn = fsym;
	return fsym;
}

/* f({...}): a temporary of class type t constructed from the braced list at token `pos` (its arguments
   parsed again here for their code: the list's first parse was unevaluated), as an lvalue (0: not built) */
VPRIV uint32_t cxx_gen_braced_temp(cxx *c, qty t, uint32_t pos, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t save = c->pos, an[CXX_MAX_ARGS], n, tmp;
	qty at[CXX_MAX_ARGS];
	uint8_t al[CXX_MAX_ARGS];
	cc_id_list l;

	t = QUNQ(t);
	if (!cc_type_is_record(a, t) || AST_TYPE_KIND(a, t) == TY_ARRAY || !cxx_gen_object_type_ok(c, t))
		return 0;
	c->pos = pos;
	cxx_braced_arguments_parse(c);
	c->pos = save;
	n = c->ncargs;
	if (n == UINT32_MAX || n > CXX_MAX_ARGS)
		return 0;
	memcpy(an, c->carg_nodes, sizeof an), memcpy(at, c->cargs, n * sizeof *at), memcpy(al, c->cargs_lval, n);
	if (cxx_is_initializer_list(c, t)) {
		/* f(initializer_list<E>) from {a, b}: the list itself (its array's class elements, whose destruction
		   at the full-expression's end is not built: not done) */
		uint32_t arr = 0, il;

		c->ctor_ilist_n = n;
		memcpy(c->ctor_ilist_types, at, n * sizeof *at), memcpy(c->ctor_ilist_lval, al, n);
		memcpy(c->ctor_ilist_brace, c->carg_brace, sizeof c->ctor_ilist_brace);
		if (!c->gen_block)
			return 0;
		il = cxx_gen_initializer_list(c, t, an, n, c->gen_block, loc, &arr);	/* (before the call, in its block) */
		if (!il)
			return 0;
		if (arr && cc_type_is_record(a, AST_TYPE(a, a->syms[arr].ty)->base) && cxx_gen_needs_destruction(c, AST_TYPE(a, a->syms[arr].ty)->base, 0)) {
			/* its class elements: destroyed at the block's end (as the temporaries are, later than C++ says) */
			qty et = AST_TYPE(a, a->syms[arr].ty)->base;
			cxx_entity *d = cxx_gen_effective_destructor(c, et, 0);
			attrs at;

			if (!d || d->fn_deleted || !cxx_gen_name(c, d) || !d->sym1 || c->gen_notemp || (cxx_member_body_use(c, d), 0))
				return 0;
			memset(&at, 0, sizeof at);
			at.cleanup = cxx_gen_array_cleanup(c, et, (uint32_t)AST_TYPE(a, a->syms[arr].ty)->n, cxx_gen_dtor_sym(d), loc);
			cc_attribute_apply_symbol(a, &a->syms[arr], &at);
			c->gen_block_temps++;
		}
		return il;
	}
	cxx_ctor_use(c, t, at, al, n, true);
	{
		cxx_entity *picked = c->ctor_picked, *il = c->ctor_ilist;
		bool found = c->ctor_found, none = c->ctor_none;

		if (!(tmp = cxx_gen_class_temp(c, t, loc)))
			return 0;
		c->ctor_picked = picked, c->ctor_found = found, c->ctor_none = none, c->ctor_ilist = il;
	}
	(void)l;
	if (!cxx_gen_construct_at(c, cxx_gen_var_node(c, tmp, loc), t, an, n, c->gen_block, loc))
		return 0;	/* (constructed before the call, in its block: the temporary is an lvalue) */
	return cxx_gen_var_node(c, tmp, loc);
}

/* is a defaulted copy or move constructor of class t trivial: no vptr, its bases' and members' copies
   trivial (the class's other copy constructors aside: iter_impl's user-provided one does not make its
   defaulted move constructor non-trivial) */
VPRIV bool cxx_gen_defaulted_copy_trivial(cxx *c, qty t)
{
	cc *a = c->ast;
	cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t));
	qty bases[16];
	uint32_t nb;
	bool pod, empty;

	if (!cxx_class_layout_info(c, t, &pod, &empty))
		return false;
	if (pod)
		return true;
	if (!cls || cxx_class_polymorphic(c, t))
		return false;
	nb = cxx_class_bases(c, t, bases, 16);
	for (uint32_t i = 0; i < nb; i++)
		if (cxx_gen_trivial_abi(c, bases[i], 1) != 1)
			return false;
	for (uint32_t i = 0; i < cls->nfield_types; i++) {
		qty ft = cls->field_types[i];

		while (ft != CXX_TY_UNKNOWN && AST_TYPE_KIND(a, ft) == TY_ARRAY)
			ft = AST_TYPE(a, ft)->base;
		if (ft == CXX_TY_UNKNOWN || (cc_type_is_record(a, ft) && cxx_gen_trivial_abi(c, ft, 1) != 1))
			return false;
	}
	return true;
}

static void cxx_gen_call_impl(cxx *c, cxx_entity *f, uint32_t object, bool arrow, const uint32_t *args, uint32_t nargs, uint32_t loc);

/* the call f(args) on object (0: none): its node in c->enode.  The expression's type stays the parser's:
   building the call may instantiate bodies (the callee's, a temporary's destructor) whose expressions
   reuse c->etype */
VPRIV void cxx_gen_call(cxx *c, cxx_entity *f, uint32_t object, bool arrow, const uint32_t *args, uint32_t nargs, uint32_t loc)
{
	qty etype = c->etype;
	uint8_t elval = c->elval, econst = c->econst;
	int64_t evalue = c->evalue;

	cxx_gen_call_impl(c, f, object, arrow, args, nargs, loc);
	c->etype = etype, c->elval = elval, c->econst = econst, c->evalue = evalue;
}

static void cxx_gen_call_impl(cxx *c, cxx_entity *f, uint32_t object, bool arrow, const uint32_t *args, uint32_t nargs, uint32_t loc)
{
	cc *a = c->ast;
	const ctype *ft;
	cc_id_list list;
	uint32_t fn, n, self = 0, pre = 0, ret_tmp = 0, self_at = 0, call, all[2 * CXX_MAX_ARGS], br[CXX_MAX_ARGS];

	bool holes = false;

	for (uint32_t i = 0; i < nargs && i < CXX_MAX_ARGS && !holes; i++)
		holes = !args[i];
	if (holes) {
		memcpy(br, c->gen_call_brace, sizeof br);	/* (this call's alone: taken at once) */
		memset(c->gen_call_brace, 0, sizeof c->gen_call_brace);
	} else
		memset(br, 0, sizeof br);	/* (no braced argument: the lists are a later call's, T x({}, v)'s constructor) */
	c->enode = 0;	/* (a call that is not built has no node: not its last argument's, which the parse left here) */

	if (f && f->fn_defaulted && cxx_gen_on(c) && f->name == vintern_cstr("operator==")) {
		/* a defaulted equality: no function to call, the comparison itself (a friend's two operands; a
		   member's object and one) */
		uint32_t x = f->member_of && !f->fn_static ? (object ? (arrow ? cc_dereference_build(a, cc_conversion_decay(a, object), loc) : object) : 0) :
			     nargs == 2 ? args[0] : 0;
		uint32_t y = f->member_of && !f->fn_static ? (nargs == 1 ? args[0] : 0) : nargs == 2 ? args[1] : 0;

		c->enode = x && y ? cxx_gen_defaulted_eq(c, x, y, loc) : 0;
		if (!c->enode)
			CXX_CALL_WHY();
		return;
	}

	if (!cxx_gen_on(c) || !f || f->kind != CXX_ENTITY_VALUE || f->ty == CXX_TY_UNKNOWN || AST_TYPE_KIND(a, f->ty) != TY_FUNC || nargs > CXX_MAX_ARGS)
		{ CXX_CALL_WHY(); return; }
	if (f->fn_defaulted && nargs == 1 && object && !arrow && args[0] && cxx_gen_is_special(f) && vintern_ents[f->name].s[1] == 'c' &&
	    f->member_of && f->member_of->owner && QUNQ(AST_NODE_AT(a, args[0])->ty) == QUNQ(f->member_of->owner->ty) &&
	    QUNQ(AST_NODE_AT(a, object)->ty) == QUNQ(f->member_of->owner->ty) && cxx_gen_defaulted_copy_trivial(c, f->member_of->owner->ty)) {
		/* T(const T&) = default or T(T&&) = default of a trivially copyable class: its bytes (clang calls no
		   function for it, and defines none) */
		qty t = QUNQ(f->member_of->owner->ty);
		bool pod, empty;

		if (cxx_class_layout_info(c, t, &pod, &empty) && empty)
			c->enode = cc_node_rvalue(a, args[0]);	/* (an empty class: nothing; its byte may be another's) */
		else {
			uint32_t target = object;

			if (AST_NODE_AT(a, target)->ty != t) {
				qty pt = cc_type_pointer(a, t);
				uint32_t addr = cc_address_of_build(a, target, loc);

				target = cc_dereference_build(a, cc_cast_insert(a, addr, pt, cc_conversion_cast_kind(a, AST_NODE_AT(a, addr)->ty, pt, addr)), loc);
			}
			c->enode = cc_assignment_build(a, 0, target, cc_node_rvalue(a, args[0]), loc);
		}
		return;
	}
	ft = AST_TYPE(a, f->ty);
	uint32_t bargs[CXX_MAX_ARGS];
	{
		/* f({...}): a braced list argument (no node) made a temporary of its parameter's class */
		bool any = false;

		for (uint32_t i = 0; i < nargs; i++)
			any |= !args[i] && br[i] && i < ft->n;
		if (any) {
			/* (what building them leaves of the statement's state is not the statement's: a call that fails
			   after them must still fail its statement) */
			uint32_t sdone = c->sdone, snode = c->snode;

			memcpy(bargs, args, nargs * sizeof *args);
			for (uint32_t i = 0; i < nargs; i++)
				if (!args[i] && br[i] && i < ft->n) {
					qty pt = a->params[ft->aux + i];

					if (pt != CXX_TY_UNKNOWN && AST_TYPE_KIND(a, pt) == TY_PTR && AST_TYPE(a, pt)->ext)
						pt = AST_TYPE(a, pt)->base;
					bargs[i] = cxx_gen_braced_temp(c, pt, br[i], loc);
				}
			c->sdone = sdone, c->snode = snode;
			c->enode = 0;	/* (what building them left is no node of this call's: a call that fails has none) */
			args = bargs;
		}
	}
	if (nargs < ft->n && ft->n <= 32 && ft->n - nargs <= f->ndefaults && f->defs && !(ft->flags & TF_VARIADIC)) {
		/* the arguments left out: the parameters' default arguments */
		uint32_t n = ft->n, first = n - f->ndefaults;

		memcpy(all, args, nargs * sizeof *args);
		for (uint32_t i = nargs; i < n; i++)
			if (!(all[i] = cxx_gen_default_argument(c, f, i - first))) {
				/* = {} or = {a, b}: a temporary of the parameter's class from that list, parsed where the
				   default is (initializer_list_t init = {}) */
				uint32_t dp = f->defs ? f->defs[2 * (i - first)] : 0;
				qty pt = a->params[ft->aux + i];

				if (dp && dp < c->ntok && c->tok[dp].kind == LEX_TOKEN_LEFT_BRACE && pt != CXX_TY_UNKNOWN) {
					cxx_scope *saved = c->scope;
					uint32_t sdone = c->sdone, snode = c->snode;

					if (AST_TYPE_KIND(a, pt) == TY_PTR && AST_TYPE(a, pt)->ext)
						pt = AST_TYPE(a, pt)->base;
					if (f->def_scope)
						c->scope = f->def_scope;
					all[i] = cxx_gen_braced_temp(c, pt, dp, loc);
					c->scope = saved, c->sdone = sdone, c->snode = snode, c->enode = 0;
				}
				if (!all[i])
					{ CXX_CALL_WHY(); return; }
			}
		args = all, nargs = n;
		ft = AST_TYPE(a, f->ty);
	}
	if ((ft->flags & TF_VARIADIC) ? nargs < ft->n || nargs > 32 : ft->n != nargs)
		{ CXX_CALL_WHY(); return; }	/* (default arguments not recorded: not built yet) */
	if (nargs == 1 && f->is_instance && cxx_gen_is_reference(c, ft->base) && (AST_NODE_AT(a, args[0])->flags & NF_LVALUE) &&
	    cxx_gen_std_cast_function(c, f)) {
		/* std::forward, std::move, std::move_if_noexcept, std::as_const of an lvalue: that object (clang's
		   builtins: no function called, none emitted) */
		c->enode = args[0];
		return;
	}
	if (!cxx_gen_value_ok(c, ft->base) && AST_TYPE_KIND(a, ft->base) != TY_VOID)
		{ CXX_CALL_WHY(); return; }
	for (uint32_t i = 0; i < nargs; i++)
		if (!args[i] || (i < ft->n ? !cxx_gen_value_ok(c, a->params[ft->aux + i]) :
				 cc_type_is_record(a, AST_NODE_AT(a, args[i])->ty) && AST_TYPE_KIND(a, AST_NODE_AT(a, args[i])->ty) != TY_ARRAY))
			{ CXX_CALL_WHY(); return; }	/* (a class through ...: not built yet) */
	if (!cxx_gen_on(c) || !cxx_gen_name(c, f) || !f->sym)
		{ CXX_CALL_WHY(); return; }
	if (f->name_guess && !(a->syms[f->sym].flags & SF_DEFINED))
		{ CXX_CALL_WHY(); return; }	/* (a name only vcc gives, and no code of vcc's for it: nothing would define it) */
	ft = AST_TYPE(a, f->ty);
	cc_id_list_init(&list);
	if (cxx_gen_by_address(c, ft->base)) {
		/* a class returned by address: in a temporary (whoever takes the value may aim it elsewhere) */
		ret_tmp = cxx_gen_class_temp(c, QUNQ(ft->base), loc);
		if (!ret_tmp)
			{ CXX_CALL_WHY(); return; }
		cc_id_list_append(a, &list, cc_address_of_build(a, cxx_gen_var_node(c, ret_tmp, loc), loc));
		self_at = 1;
	}
	if (cxx_gen_has_this(f)) {
		/* the object: its address (obj.f(), p->f()), or this for a member named alone */
		qty tt = cxx_gen_this_type(c, f);

		if (object)
			self = arrow ? cc_conversion_decay(a, object) : (AST_NODE_AT(a, object)->flags & NF_LVALUE) ? cc_address_of_build(a, object, loc) : 0;
		else if (c->gen_this) {
			cxx_gen_this(c, loc);
			self = c->enode;
			c->enode = 0;
		}
		if (!self || tt == CXX_TY_UNKNOWN)
			{ CXX_CALL_WHY(); goto fail; }	/* (a member called on a value: a temporary, not built yet) */
		self = cxx_gen_base_pointer(c, self, AST_TYPE(a, tt)->base, loc);	/* (a base class's member: on the base subobject) */
		if (!self)
			{ CXX_CALL_WHY(); goto fail; }
		cc_id_list_append(a, &list, self);
	}
	for (uint32_t i = 0; i < nargs; i++) {
		qty pt = i < ft->n ? a->params[ft->aux + i] : CXX_TY_UNKNOWN;
		uint32_t arg;

		if (i >= ft->n) {
			/* through ...: the default argument promotions (an array decays, a float is a double) */
			arg = cc_node_rvalue(a, args[i]);
			if (arg && AST_TYPE_KIND(a, AST_NODE_AT(a, arg)->ty) == TY_FLOAT)
				arg = cc_cast_insert(a, arg, cc_type_basic(TY_DOUBLE), CK_F2F);
			if (arg && !cxx_gen_type_ok(c, AST_NODE_AT(a, arg)->ty))
				arg = 0;
		} else if (cxx_gen_by_address(c, pt))
			arg = cxx_gen_class_argument(c, args[i], pt, loc);
		else if (args[i] && cc_type_is_record(a, pt) && AST_TYPE_KIND(a, pt) != TY_ARRAY && QUNQ(AST_NODE_AT(a, args[i])->ty) != QUNQ(pt) &&
			 !(c->nullptr_ty && QUNQ(pt) == c->nullptr_ty)) {	/* (nullptr_t: a struct here, bound as before) */
			/* a class passed as a value (registers) from another type: a temporary made by its converting
			   constructor, its value passed (const_iterator from iterator) */
			uint32_t p = cxx_gen_class_argument(c, args[i], pt, loc);

			arg = p ? cc_node_rvalue(a, cc_dereference_build(a, p, loc)) : 0;
		} else
			arg = cxx_gen_bind(c, args[i], pt, loc);

		if (!arg) {
			CXX_CALL_WHY(); goto fail;
		}
		cc_id_list_append(a, &list, arg);
	}
	if (f->fn_virtual && !cxx_gen_is_special(f) && c->gen_vmode != 1) {
		/* through the vtable: tmp = object's address; (*(F ***)tmp)[slot](tmp, args) */
		qty pf = cc_type_pointer(a, a->syms[f->sym].ty), ppf = cc_type_pointer(a, pf), pppf = cc_type_pointer(a, ppf);
		qty st = QUNQ(AST_NODE_AT(a, self)->ty);
		uint32_t tmp, tn, vptr;

		if (c->gen_vmode == 2 || !f->vslot || !self)
			{ CXX_CALL_WHY(); goto fail; }
		tmp = cxx_gen_temporary(c, st, loc);
		if (!tmp)
			{ CXX_CALL_WHY(); goto fail; }
		pre = cc_assignment_build(a, 0, cxx_gen_var_node(c, tmp, loc), self, loc);
		list.v[self_at] = cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc));
		tn = cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc));
		vptr = cc_node_rvalue(a, cc_dereference_build(a, cc_cast_insert(a, tn, pppf, cc_conversion_cast_kind(a, st, pppf, tn)), loc));
		fn = cc_node_rvalue(a, cc_dereference_build(a, cc_binary_build(a, LEX_TOKEN_PLUS, vptr,
				cc_node_integer_new(a, f->vslot - 1, cc_type_basic(TY_LONG), loc), loc), loc));
	} else {
		/* (a constructor or destructor: the complete object's; a base subobject's: the base object's, C2/D2) */
		bool base_obj = c->gen_base_type && cxx_gen_is_special(f) && f->member_of && f->member_of->owner &&
				QUNQ(f->member_of->owner->ty) == c->gen_base_type;
		uint32_t target = base_obj ? f->sym : !f->sym1 ? f->sym : cxx_gen_is_special(f) ? cxx_gen_dtor_sym(f) : f->sym1;	/* (C2 too: clang points an inline or instance constructor's C1 uses at C2) */

		if (base_obj)
			c->gen_base_type = 0;

		fn = cc_node_new(a, N_VAR, a->syms[target].ty, loc);
		AST_NODE_AT(a, fn)->a = target;
		AST_NODE_AT(a, fn)->flags = NF_LVALUE;
		fn = cc_node_rvalue(a, fn);	/* (the function designator: a pointer to it) */
	}
	n = cc_node_new(a, N_CALL, ret_tmp ? cc_type_pointer(a, QUNQ(ft->base)) : QUNQ(ft->base), loc);
	AST_NODE_AT(a, n)->a = fn;
	AST_NODE_AT(a, n)->b = cc_id_list_freeze(a, &list);
	AST_NODE_AT(a, n)->c = list.n;
	call = n;
	if (pre)
		n = cc_node_new_operands(a, N_COMMA, AST_NODE_AT(a, n)->ty, loc, pre, n);
	if (ret_tmp) {
		/* the call gives the result object's address back */
		uint32_t expr = n;

		n = cc_dereference_build(a, n, loc);
		cxx_gen_prv_add(c, n, expr, call, ret_tmp);
	} else if (cc_type_is_record(a, ft->base) && c->gen_block) {
		/* a class returned as C does: held in a temporary, so that it is an object (x().f(), a const T&) */
		uint32_t tmp = cxx_gen_temporary(c, QUNQ(ft->base), loc);

		if (!tmp)
			{ CXX_CALL_WHY(); goto fail; }
		n = cc_assignment_build(a, 0, cxx_gen_var_node(c, tmp, loc), n, loc);
		n = cc_node_new_operands(a, N_COMMA, cc_type_pointer(a, QUNQ(ft->base)), loc, n, cc_address_of_build(a, cxx_gen_var_node(c, tmp, loc), loc));
		n = cc_dereference_build(a, n, loc);
		cxx_gen_prv_add(c, n, 0, 0, tmp);	/* (a prvalue still: T&& binds it, U(T&&) is picked for it) */
	}
	if (cxx_gen_is_reference(c, ft->base))
		n = cxx_gen_referent(c, n, loc);
	c->enode = n;
	return;
fail:
	if (ret_tmp)
		cxx_gen_fail(c);	/* (its temporary is declared) */
}

/* p(args), r(args), o.f(args): a call through a pointer or reference to a function (no class through ... or by
   address, no class result returned by address yet) */
VPRIV void cxx_gen_indirect_call(cxx *c, uint32_t callee, const uint32_t *args, uint32_t nargs, uint32_t loc)
{
	cc *a = c->ast;
	qty t;
	const ctype *ft;
	cc_id_list list;
	uint32_t n, fn;

	c->enode = 0;
	if (!cxx_gen_on(c) || !callee || nargs > 32)
		return;
	t = QUNQ(AST_NODE_AT(a, callee)->ty);
	if (AST_TYPE_KIND(a, t) == TY_PTR)
		t = QUNQ(AST_TYPE(a, t)->base);
	if (AST_TYPE_KIND(a, t) != TY_FUNC)
		return;
	ft = AST_TYPE(a, t);
	if ((ft->flags & TF_VARIADIC) ? nargs < ft->n : ft->n != nargs)
		return;
	if (AST_TYPE_KIND(a, ft->base) != TY_VOID && (!cxx_gen_value_ok(c, ft->base) || cxx_gen_by_address(c, ft->base) || cc_type_is_record(a, ft->base)))
		return;
	cc_id_list_init(&list);
	for (uint32_t i = 0; i < nargs; i++) {
		uint32_t arg;

		if (!args[i])
			return;
		if (i >= ft->n) {
			if (cc_type_is_record(a, AST_NODE_AT(a, args[i])->ty) && AST_TYPE_KIND(a, AST_NODE_AT(a, args[i])->ty) != TY_ARRAY)
				return;
			arg = cc_node_rvalue(a, args[i]);
			if (arg && AST_TYPE_KIND(a, AST_NODE_AT(a, arg)->ty) == TY_FLOAT)
				arg = cc_cast_insert(a, arg, cc_type_basic(TY_DOUBLE), CK_F2F);
		} else {
			qty pt = a->params[ft->aux + i];

			if (!cxx_gen_value_ok(c, pt))
				return;
			arg = cxx_gen_by_address(c, pt) ? cxx_gen_class_argument(c, args[i], pt, loc) : cxx_gen_bind(c, args[i], pt, loc);
		}
		if (!arg)
			return;
		cc_id_list_append(a, &list, arg);
	}
	fn = cc_node_rvalue(a, callee);	/* (a function designator decays to a pointer to it) */
	if (!fn)
		return;
	n = cc_node_new(a, N_CALL, QUNQ(ft->base), loc);
	AST_NODE_AT(a, n)->a = fn;
	AST_NODE_AT(a, n)->b = cc_id_list_freeze(a, &list);
	AST_NODE_AT(a, n)->c = list.n;
	if (cxx_gen_is_reference(c, ft->base))
		n = cxx_gen_referent(c, n, loc);
	c->enode = n;
}

/*
 * An overloaded operator's call, after cxx_operator_call_type picked the function: x.operator@(args) for a
 * member, operator@(x, args) for a function at namespace scope (a friend too).
 */
VPRIV void cxx_gen_operator_call(cxx *c, uint32_t obj, const uint32_t *args, uint32_t nargs, uint32_t loc)
{
	cxx_entity *f = c->call_picked;
	uint32_t all[17];

	if (!cxx_gen_on(c))
		return;
	c->enode = 0;
	if (!c->call_resolved || !f || f->kind != CXX_ENTITY_VALUE || !obj || nargs > CXX_MAX_ARGS)
		return;
	for (uint32_t i = 0; i < nargs; i++)
		if (!args[i])
			return;
	if (!cxx_gen_has_this(f) && !f->fn_static && f->ty != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, f->ty) == TY_FUNC &&
	    AST_TYPE(c->ast, f->ty)->n == nargs && cc_type_is_record(c->ast, AST_NODE_AT(c->ast, obj)->ty)) {
		/* (a member of the object's class whose entity lacks its class, an instance's late member: found there) */
		cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(AST_NODE_AT(c->ast, obj)->ty));

		for (cxx_entity *e = cls ? cxx_lookup_local(cls, f->name) : NULL; e; e = e->next)
			if (e == f) {
				f->member_of = cls;
				break;
			} else if (e->kind == CXX_ENTITY_VALUE && e->member_of == cls && e->ty == f->ty) {
				f = e;	/* (the same member, as the object's class declares it) */
				break;
			}
	}
	if (cxx_gen_has_this(f)) {
		cxx_gen_call(c, f, obj, false, args, nargs, loc);
		return;
	}
	if (f->fn_static && f->member_of && f->member_of->kind == CXX_SCOPE_CLASS) {
		/* a static operator() or operator[] (C++23): the object is evaluated, not passed */
		cxx_gen_call(c, f, 0, false, args, nargs, loc);
		if (c->enode && AST_NODE_AT(c->ast, obj)->op != N_VAR)
			c->enode = cc_node_new_operands(c->ast, N_COMMA, AST_NODE_AT(c->ast, c->enode)->ty, loc, cc_node_rvalue(c->ast, obj), c->enode);
		return;
	}
	all[0] = obj;
	memcpy(all + 1, args, nargs * sizeof *args);
	cxx_gen_call(c, f, 0, false, all, nargs + 1, loc);
}

VPRIV uint32_t cxx_gen_var_node(cxx *c, uint32_t s, uint32_t loc);

/* ---- builtins -------------------------------------------------------------------------------- */
