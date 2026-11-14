/* C++ code generation: namespace-scope objects, their initialization, file-level assembly */
#include "codegen.h"

VPRIV const char *cxx_gen_symbol_asm_name(cxx *c, uint32_t s)
{
	cc *a = c->ast;
	const sym *y = &a->syms[s];

	return y->attrs && a->attr[y->attrs].asmname ? a->strs[a->attr[y->attrs].asmname - 1].s : NULL;
}

/* text at file scope, for the assembler as it is */
VPRIV void cxx_gen_file_asm(cxx *c, const char *text)
{
	cc *a = c->ast;
	uint32_t si = cxx_gen_text(c, text), ai = a->nasm++, n;

	memset(&a->asms[ai], 0, sizeof a->asms[ai]);
	a->asms[ai].tmpl = si + 1;
	n = cc_node_new(a, N_ASM, cc_type_basic(TY_VOID), 0);
	AST_NODE_AT(a, n)->a = ai;
	a->tlasm[a->ntlasm++] = n;
}

/*
 * A try block.  Its body was built as a function of its own (a closure's operator(), `call`, returning
 * int; `closure` is the object holding its captures).  The body is called through a small function
 * written out in assembly, which has the exception table: when an exception one of the handlers takes
 * comes out of the body, that function returns minus the handler's number (1..) with the exception at
 * *exc; else what the body returned (0).  Here: the call; its result's node (an int).  The handlers' type
 * info symbols: ti[] (0: catch (...)).  x86-64 only.  0: not built.
 */
VPRIV uint32_t cxx_gen_try(cxx *c, uint32_t closure, cxx_entity *call, const uint32_t *ti, uint32_t nhandlers, uint32_t exc, uint32_t loc)
{
	cc *a = c->ast;
	qty vp = cc_type_pointer(a, cc_type_basic(TY_VOID)), ps[3] = { vp, vp, vp };
	uint32_t serial = c->gen_try_serial++, fs, args[3];
	char name[64], *text;
	size_t cap = 4096 + 256 * (size_t)nhandlers, n = 0;
	attrs at;

	if (!cxx_gen_on(c) || a->o->target != PP_LINUX_X86_64 || !closure || !call->sym || !nhandlers || nhandlers > 60 ||
	    !(AST_NODE_AT(a, closure)->flags & NF_LVALUE))
		return 0;
	for (uint32_t k = 0; k < nhandlers; k++) {
		attrs used;

		if (ti[k] && !cxx_gen_symbol_asm_name(c, ti[k]))
			return 0;
		if (ti[k]) {	/* (named by the exception table, in assembly: kept whatever the code refers to) */
			memset(&used, 0, sizeof used);
			used.used = 1;
			cc_attribute_apply_symbol(a, &a->syms[ti[k]], &used);
		}
	}
	if (!exc)
		return 0;
	text = malloc(cap);
#define ASM(...) (n += (size_t)snprintf(text + n, cap - n, __VA_ARGS__))
	if (!c->gen_eh_pers) {
		c->gen_eh_pers = 1;
		ASM("\t.section\t.data.rel.ro,\"aw\",@progbits\n\t.p2align\t3\n.Lvcc_personality:\n\t.quad\t__gxx_personality_v0\n");
	}
	ASM("\t.text\n\t.type\t__vcc_try_%u,@function\n__vcc_try_%u:\n\t.cfi_startproc\n", serial, serial);
	ASM("\t.cfi_personality 0x9b, .Lvcc_personality\n\t.cfi_lsda 0x1b, .Lvcc_lsda_%u\n", serial);
	ASM("\tpushq\t%%rbx\n\t.cfi_def_cfa_offset 16\n\t.cfi_offset %%rbx, -16\n\tmovq\t%%rdx, %%rbx\n\tmovq\t%%rdi, %%rax\n\tmovq\t%%rsi, %%rdi\n");
	ASM(".Lvcc_cs_%u:\n\tcallq\t*%%rax\n.Lvcc_ce_%u:\n\tpopq\t%%rbx\n\t.cfi_def_cfa_offset 8\n\tretq\n", serial, serial);
	ASM(".Lvcc_lp_%u:\n\t.cfi_def_cfa_offset 16\n\tmovq\t%%rax, (%%rbx)\n\tmovl\t%%edx, %%eax\n\tnegl\t%%eax\n\tpopq\t%%rbx\n\t.cfi_def_cfa_offset 8\n\tretq\n", serial);
	ASM("\t.cfi_endproc\n.Lvcc_end_%u:\n\t.size\t__vcc_try_%u, .Lvcc_end_%u-__vcc_try_%u\n", serial, serial, serial, serial);
	/* the exception table: one call site, its actions (handler k: filter k, then the next), the types last first */
	ASM("\t.section\t.gcc_except_table,\"a\",@progbits\n.Lvcc_lsda_%u:\n\t.byte\t0xff\n\t.byte\t0x9b\n", serial);
	ASM("\t.uleb128 .Lvcc_tt_%u-.Lvcc_ttb_%u\n.Lvcc_ttb_%u:\n\t.byte\t0x01\n\t.uleb128 .Lvcc_cse_%u-.Lvcc_csb_%u\n.Lvcc_csb_%u:\n", serial, serial, serial, serial, serial, serial);
	ASM("\t.uleb128 .Lvcc_cs_%u-__vcc_try_%u\n\t.uleb128 .Lvcc_ce_%u-.Lvcc_cs_%u\n\t.uleb128 .Lvcc_lp_%u-__vcc_try_%u\n\t.uleb128 1\n.Lvcc_cse_%u:\n",
	    serial, serial, serial, serial, serial, serial, serial);
	for (uint32_t k = 0; k < nhandlers; k++)
		ASM("\t.byte\t%u\n\t.byte\t%u\n", k + 1, k + 1 < nhandlers ? 1u : 0u);
	for (uint32_t k = nhandlers; k-- > 0;) {
		if (ti[k])
			ASM("\t.long\t.Lvcc_ti_%u_%u-.\n", serial, k);
		else
			ASM("\t.long\t0\n");
	}
	ASM(".Lvcc_tt_%u:\n\t.section\t.data.rel.ro,\"aw\",@progbits\n\t.p2align\t3\n", serial);
	for (uint32_t k = 0; k < nhandlers; k++)
		if (ti[k])
			ASM(".Lvcc_ti_%u_%u:\n\t.quad\t%s\n", serial, k, cxx_gen_symbol_asm_name(c, ti[k]));
	ASM("\t.text");
#undef ASM
	cxx_gen_file_asm(c, text);
	free(text);
	snprintf(name, sizeof name, " __vcc_try_%u", serial);
	fs = cc_symbol_new(a, S_FUNC, vintern_cstr("__vcc_try"), cxx_type_function(c, cc_type_basic(TY_INT), ps, 3, false), loc);
	a->syms[fs].flags |= SF_GLOBAL;
	a->syms[fs].depth = 0;
	memset(&at, 0, sizeof at);
	at.asmname = cxx_gen_text(c, name + 1) + 1;
	cc_attribute_apply_symbol(a, &a->syms[fs], &at);
	args[0] = cxx_gen_to_void_pointer(c, cxx_gen_function_node(c, call->sym, loc));
	args[1] = cxx_gen_to_void_pointer(c, cc_address_of_build(a, closure, loc));
	args[2] = cxx_gen_to_void_pointer(c, cc_address_of_build(a, cxx_gen_var_node(c, exc, loc), loc));
	return cxx_gen_runtime_call(c, fs, args, 3, loc);
}

/* a scratch variable of the block being built (0: no block) */
VPRIV uint32_t cxx_gen_scratch(cxx *c, qty t, uint32_t loc)
{
	return cxx_gen_on(c) ? cxx_gen_temporary(c, t, loc) : 0;
}

/* a try block's outlined body, then "return 0" (it fell off its end) */
VPRIV uint32_t cxx_gen_return_zero(cxx *c, uint32_t body, uint32_t loc)
{
	cc *a = c->ast;
	cc_id_list l;

	if (!body)
		return 0;
	cc_id_list_init(&l);
	cc_id_list_append(a, &l, body);
	cc_id_list_append(a, &l, cc_statement_new(a, N_RETURN, loc, cc_node_integer_new(a, 0, cc_type_basic(TY_INT), loc), 0, 0));
	return cc_statement_new(a, N_BLOCK, loc, 0, cc_id_list_freeze(a, &l), l.n);
}

/* the try statement: code = the call of the body through its trampoline; then the handler it names, if any */
VPRIV uint32_t cxx_gen_try_statement(cxx *c, uint32_t call, const uint32_t *handlers, uint32_t nhandlers, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t code = cxx_gen_temporary(c, cc_type_basic(TY_INT), loc), chain = 0;
	cc_id_list l;

	if (!code || !call)
		return 0;
	for (uint32_t k = nhandlers; k-- > 0;) {
		uint32_t cond = cc_binary_build(a, LEX_TOKEN_EQUAL_EQUAL, cc_node_rvalue(a, cxx_gen_var_node(c, code, loc)),
						cc_node_integer_new(a, -(i128)(k + 1), cc_type_basic(TY_INT), loc), loc);

		chain = cc_statement_new(a, N_IF, loc, cc_condition_scalar(a, cond, "statement"), handlers[k], chain);
	}
	cc_id_list_init(&l);
	cc_id_list_append(a, &l, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, cxx_gen_var_node(c, code, loc), call, loc), 0, 0));
	cc_id_list_append(a, &l, chain);
	return cc_statement_new(a, N_BLOCK, loc, 0, cc_id_list_freeze(a, &l), l.n);
}

/*
 * A variable defined at namespace scope: before its initializer is parsed (true: its node is wanted)
 * and after (init: the node, 0 with has_init for one not built: the variable is then left undefined).
 * Without an initializer it is zero-initialized (a tentative definition, as in C).
 */
VPRIV bool cxx_gen_global_begin(cxx *c, const cxx_entity *v)
{
	cc *a = c->ast;
	cxx_entity *f;
	char nm[64];
	attrs at;

	if (!a->o->emit || c->tentative || c->template_depth || c->instantiating || c->gen || !v || !v->sym || !(v->global_ns || v->mangled_var) ||
	    !cxx_gen_object_type_ok(c, v->ty))
		return false;
	/* The initializer is built inside a function of its own: if it turns out not to be a constant, that
	   function runs it when the program starts (.init_array), in the order of the definitions. */
	f = calloc(1, sizeof *f);
	f->kind = CXX_ENTITY_VALUE, f->name = vintern_cstr("__vcc_init"), f->pack_index = -1;
	f->ty = cxx_type_function(c, cc_type_basic(TY_VOID), NULL, 0, false);
	f->fn_internal = 1, f->mangled = 1;
	f->sym = cc_symbol_new(a, S_FUNC, f->name, f->ty, 0);
	a->syms[f->sym].depth = 0;
	a->syms[f->sym].sc = SC_STATIC;
	snprintf(nm, sizeof nm, "__vcc_init_%u", c->ginit_serial++);
	memset(&at, 0, sizeof at);
	at.asmname = cxx_gen_text(c, nm) + 1;
	at.constructor = 1, at.used = 1;	/* (run at start-up: the emitter's .init_array entry, when it is defined) */
	cc_attribute_apply_symbol(a, &a->syms[f->sym], &at);
	if (!cxx_gen_function_begin(c, f, c->scope, NULL, 0)) {
		free(f);
		return false;
	}
	c->ginit_fn = f;
	c->ginit_list = calloc(1, sizeof(cc_id_list));
	cc_id_list_init(c->ginit_list);
	c->gen_block = c->ginit_list;
	return true;
}

/* the start-up function is done: kept (true) with its statements as its body and an .init_array entry, or dropped */
VPRIV bool cxx_gen_global_finish(cxx *c, bool keep)
{
	cc *a = c->ast;
	cxx_entity *f = c->ginit_fn;
	cc_id_list *l = c->ginit_list;
	uint32_t body = 0;
	bool defined;

	c->ginit_fn = NULL, c->ginit_list = NULL;
	if (!f)
		return false;
	if (keep && cxx_gen_on(c) && l->n)
		body = cc_statement_new(a, N_BLOCK, 0, 0, cc_id_list_freeze(a, l), l->n);
	else
		c->gen_failed = 1;
	cxx_gen_function_end(c, f, body);
	free(l);
	defined = (a->syms[f->sym].flags & SF_DEFINED) != 0;
	if (!defined && c->stats.functions)
		c->stats.functions--;	/* (not a function of the program's: a constant initializer needs none) */
	return defined;
}

/* the variable itself, zero until the start-up function has run (not const in the object: it is written then) */
VPRIV void cxx_gen_global_zeroed(cxx *c, const cxx_entity *v)
{
	cc *a = c->ast;
	sym *y = &a->syms[v->sym];

	if (y->flags & (SF_DEFINED | SF_TENTATIVE))
		return;
	y->ty = QUNQ(y->ty);
	y->flags |= SF_TENTATIVE;
	a->toplevel[a->ntoplevel++] = v->sym;
}

/* x as an lvalue of its type without const (an initialization writes it) */
VPRIV uint32_t cxx_gen_unqualified(cxx *c, uint32_t x, uint32_t loc)
{
	cc *a = c->ast;
	qty t = AST_NODE_AT(a, x)->ty, pt;
	uint32_t addr;

	if (t == QUNQ(t))
		return x;
	pt = cc_type_pointer(a, QUNQ(t));
	addr = cc_address_of_build(a, x, loc);
	return cc_dereference_build(a, cc_cast_insert(a, addr, pt, cc_conversion_cast_kind(a, AST_NODE_AT(a, addr)->ty, pt, addr)), loc);
}

/*
 * A namespace-scope object of class type t, initialized from args (cxx_ctor_use was called for it): built
 * by the start-up function, and its destructor registered to run at exit (__cxa_atexit).
 */
VPRIV void cxx_gen_global_object(cxx *c, const cxx_entity *v, qty t, const uint32_t *args, uint32_t nargs, uint32_t loc)
{
	cc *a = c->ast;
	cxx_entity *picked = c->ctor_picked, *dtor = NULL;
	bool found = c->ctor_found, none = c->ctor_none, ok = c->ginit_fn && cxx_gen_on(c);
	bool needs = ok && cxx_gen_needs_destruction(c, t, 0);

	if (ok && needs) {
		dtor = cxx_gen_effective_destructor(c, t, 0);
		ok = dtor && !dtor->fn_deleted && cxx_gen_name(c, dtor) && dtor->sym1 && (cxx_member_body_use(c, dtor), 1);
	}
	c->ctor_picked = picked, c->ctor_found = found, c->ctor_none = none;
	ok = ok && cxx_gen_construct_at(c, cxx_gen_var_node(c, v->sym, loc), QUNQ(t), args, nargs, c->ginit_list, loc);
	if (ok && dtor) {
		/* __cxa_atexit(destructor, &object, &__dso_handle) */
		qty vp = cc_type_pointer(a, cc_type_basic(TY_VOID)), ps[3] = { vp, vp, vp };
		uint32_t ar[3];

		if (!c->gen_atexit_sym) {
			c->gen_atexit_sym = cc_symbol_new(a, S_FUNC, vintern_cstr("__cxa_atexit"), cxx_type_function(c, cc_type_basic(TY_INT), ps, 3, false), 0);
			a->syms[c->gen_atexit_sym].flags |= SF_GLOBAL;
			a->syms[c->gen_atexit_sym].depth = 0;
			c->gen_dso_sym = cxx_gen_data_symbol(c, "__dso_handle", 0);
		}
		ar[0] = cxx_gen_to_void_pointer(c, cxx_gen_function_node(c, cxx_gen_dtor_sym(dtor), loc));
		ar[1] = cxx_gen_to_void_pointer(c, cc_address_of_build(a, cxx_gen_var_node(c, v->sym, loc), loc));
		ar[2] = cxx_gen_data_address(c, c->gen_dso_sym, 0);
		cc_id_list_append(a, c->ginit_list, cc_statement_new(a, N_EXPR, loc, cxx_gen_runtime_call(c, c->gen_atexit_sym, ar, 3, loc), 0, 0));
	}
	if (cxx_gen_global_finish(c, ok && ((cc_id_list *)c->ginit_list)->n))
		cxx_gen_global_zeroed(c, v);
	else if (ok)
		cxx_gen_global_zeroed(c, v);	/* (nothing to run: a class with no construction, zeroed) */
}

VPRIV void cxx_gen_global_end(cxx *c, const cxx_entity *v, uint32_t init, bool has_init)
{
	cc *a = c->ast;
	sym *y = &a->syms[v->sym];
	bool failed = c->gen_failed || !cxx_gen_on(c);
	cc_id_list *l = c->ginit_list;

	if (!c->ginit_fn) {
		c->gen = 0;
		return;
	}
	if (y->flags & (SF_DEFINED | SF_TENTATIVE)) {
		cxx_gen_global_finish(c, false);
		return;
	}
	if (!has_init) {
		if (cc_type_is_record(a, y->ty) && AST_TYPE_KIND(a, y->ty) != TY_ARRAY && cxx_gen_needs_construction(c, y->ty, 0)) {
			/* T x; of a class with a constructor: default-initialized at start-up */
			cxx_ctor_use(c, y->ty, NULL, NULL, 0, false);
			cxx_gen_global_object(c, v, y->ty, NULL, 0, 0);
			return;
		}
		if (cc_type_is_record(a, y->ty) && cxx_gen_needs_destruction(c, y->ty, 0)) {
			cxx_gen_global_object(c, v, y->ty, NULL, 0, 0);
			return;
		}
		cxx_gen_global_finish(c, false);
		y = &a->syms[v->sym];
		y->flags |= SF_TENTATIVE;
		a->toplevel[a->ntoplevel++] = v->sym;
		return;
	}
	if (failed || !init) {
		cxx_gen_global_finish(c, false);	/* (an initializer not built yet: the variable stays a declaration) */
		return;
	}
	if (!l->n) {
		/* a constant initializer: in the object file */
		uint32_t k = init;

		if (AST_NODE_AT(a, k)->op != N_INIT)
			k = cxx_gen_type_ok(c, v->ty) && !cc_type_is_record(a, AST_NODE_AT(a, k)->ty) ? cc_conversion_apply(a, cc_conversion_decay(a, k), QUNQ(y->ty)) : 0;
		if (k && cxx_gen_static_ok(a, y->ty, k)) {
			cxx_gen_global_finish(c, false);
			y = &a->syms[v->sym];
			y->body = k;
			y->flags |= SF_DEFINED;
			a->toplevel[a->ntoplevel++] = v->sym;
			return;
		}
	}
	/* not a constant: var = init when the program starts */
	if (AST_NODE_AT(a, init)->op == N_INIT || cxx_gen_is_reference(c, v->ty) || !cxx_gen_type_ok(c, v->ty) || cc_type_is_record(a, v->ty)) {
		cxx_gen_global_finish(c, false);	/* (a list, a reference, a class by value: not built yet) */
		return;
	}
	init = cxx_gen_bind(c, init, QUNQ(v->ty), 0);
	if (!init) {
		cxx_gen_global_finish(c, false);
		return;
	}
	cc_id_list_append(a, l, cc_statement_new(a, N_EXPR, 0, cc_assignment_build(a, 0, cxx_gen_unqualified(c, cxx_gen_var_node(c, v->sym, 0), 0), init, 0), 0, 0));
	if (cxx_gen_global_finish(c, true))
		cxx_gen_global_zeroed(c, v);
}

/* ---- functions ----------------------------------------------------------------------------- */
