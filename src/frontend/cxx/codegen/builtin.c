/* C++ code generation: the builtins (__builtin_*, __atomic_*) and the library functions behind them */
#include "codegen.h"

/*
 * The builtins, from builtins.list (src/tools/gen-cxx-builtins.py writes builtins.inc, sorted by name):
 * C++-only calls, the C front end's builtin nodes, the atomics and the ones built by name here.  Clang's
 * library builtins (__builtin_memcpy, __builtin_sin, ...) are not in it: frontend/c/libbuiltins.inc has
 * them, their library function and prototype (cc_library_builtin_info).  Signatures are in C's letters
 * (cc_builtin_signature_type), '.' an operand as it is, '*' what the first argument points to, '&' a
 * pointer to the first argument's type, '=' the first argument's type.
 */
enum { CXX_BI_LIB, CXX_BI_NODE, CXX_BI_FCLASS, CXX_BI_ATOMIC, CXX_BI_SPECIAL };

static const struct cxx_builtin {
	const char *name;
	uint8_t kind;
	const char *sig, *symbol;
} cxx_builtins[] = {
#include "builtins.inc"
};

#define CXX_NBUILTINS (sizeof cxx_builtins / sizeof *cxx_builtins)

/* the order of the table's entry e against name (len bytes, not terminated): <0 before it, 0 it, >0 after */
static int cxx_builtin_order(const struct cxx_builtin *e, const char *name, size_t len)
{
	int d = strncmp(e->name, name, len);

	return d ? d : e->name[len] ? 1 : 0;
}

/* the builtin of that name taking nargs arguments (NULL: none in the list); *index its place in the table */
static const struct cxx_builtin *cxx_builtin_find(const char *name, size_t len, uint32_t nargs, size_t *index)
{
	size_t lo = 0, hi = CXX_NBUILTINS;

	while (lo < hi) {	/* (the first entry of the name: one per number of arguments follow) */
		size_t mid = (lo + hi) / 2;

		if (cxx_builtin_order(&cxx_builtins[mid], name, len) < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	for (size_t i = lo; i < CXX_NBUILTINS && !cxx_builtin_order(&cxx_builtins[i], name, len); i++)
		if (strlen(cxx_builtins[i].sig) - 1 == nargs) {
			if (index)
				*index = i;
			return &cxx_builtins[i];
		}
	return NULL;
}

static bool cxx_builtin_is(const struct cxx_builtin *b, const char *name)
{
	return !strcmp(b->name, name);
}

/* the result type a signature gives, for the arguments' types (CXX_TY_UNKNOWN: not one it can give) */
static ast_qualified_type cxx_builtin_result(cxx *c, const char *sig, ast_qualified_type arg0)
{
	if (sig[0] == '*' || sig[0] == '&' || sig[0] == '=') {
		if (arg0 == CXX_TY_UNKNOWN)
			return CXX_TY_UNKNOWN;
		if (sig[0] == '&')
			return cc_type_pointer(c->ast, arg0);
		if (sig[0] == '=')
			return AST_QUALIFIED_TYPE_UNQUALIFIED(arg0);
		if (AST_TYPE_KIND(c->ast, arg0) != AST_TYPE_POINTER || AST_TYPE(c->ast, arg0)->ext)
			return CXX_TY_UNKNOWN;
		return AST_QUALIFIED_TYPE_UNQUALIFIED(AST_TYPE(c->ast, arg0)->base);
	}
	return cc_builtin_signature_type(c->ast, sig[0]);
}

/* C's builtin of that name (its node's id) */
static const cc_builtin *cxx_c_builtin(const char *name)
{
	cc_builtin_table_init();	/* (C's table: built once, a C++ TU may be the first to ask) */
	return cc_builtin_lookup(vintern_cstr(name));
}

/* is t a floating type (the classification builtins' operand) */
static bool cxx_is_floating(cxx *c, ast_qualified_type t)
{
	int k = t != CXX_TY_UNKNOWN ? AST_TYPE_KIND(c->ast, t) : -1;

	return k == AST_TYPE_FLOAT || k == AST_TYPE_DOUBLE || k == AST_TYPE_LONG_DOUBLE || k == AST_TYPE_FLOAT16 || k == AST_TYPE_BFLOAT16 || k == AST_TYPE_FLOAT128;
}

/* a builtin call's type, for the builtins vcc builds (arg0: the first argument's type); false: not one */
VPRIV bool cxx_builtin_function_type(cxx *c, const char *name, size_t len, uint32_t nargs, ast_qualified_type arg0, ast_qualified_type *ret)
{
	const struct cxx_builtin *b = cxx_builtin_find(name, len, nargs, NULL);

	if (b) {
		if (b->kind == CXX_BI_FCLASS && !cxx_is_floating(c, arg0))
			return false;	/* (a floating operand: the classification builtins' own) */
		if (cxx_builtin_is(b, "__builtin_expect") && arg0 == CXX_TY_UNKNOWN)
			return false;
		*ret = cxx_builtin_result(c, b->sig, arg0);
		return *ret != CXX_TY_UNKNOWN;
	}
	{
		/* a library builtin of clang's (__builtin_memcpy, __builtin_expl): the library function's type */
		ast_qualified_type ft;
		const char *lib;

		if (len > 10 && !memcmp(name, "__builtin_", 10) && cc_library_builtin_info(c->ast, name, len, &ft, &lib) &&
		    !(AST_TYPE(c->ast, ft)->flags & AST_TYPE_FLAG_VARIADIC) && AST_TYPE(c->ast, ft)->n == nargs) {
			*ret = AST_TYPE(c->ast, ft)->base;
			return true;
		}
	}
	return false;
}

/* a call of the library function sym of type ft from the arguments, converted to its parameters' types */
static uint32_t cxx_gen_library_function_call(cxx *c, uint32_t id, ast_qualified_type ft, const uint32_t *args, uint32_t nargs, uint32_t loc)
{
	cc *a = c->ast;
	const ctype *f = AST_TYPE(a, ft);
	uint32_t s = 0, fn, n;
	cc_id_list list;

	for (uint32_t k = 0; k < c->ngen_lib && !s; k++)
		if (c->gen_lib_name[k] == id && a->syms[c->gen_lib_sym[k]].ty == ft)
			s = c->gen_lib_sym[k];
	if (!s) {
		s = cc_symbol_new(a, AST_SYMBOL_FUNCTION, id, ft, loc);
		a->syms[s].flags |= AST_SYMBOL_FLAG_GLOBAL;
		a->syms[s].depth = 0;
		if (c->ngen_lib < 64)
			c->gen_lib_name[c->ngen_lib] = id, c->gen_lib_sym[c->ngen_lib++] = s;
	}
	cc_id_list_init(&list);
	for (uint32_t k = 0; k < nargs; k++)
		cc_id_list_append(a, &list, cc_conversion_apply(a, cc_conversion_decay(a, args[k]), AST_QUALIFIED_TYPE_UNQUALIFIED(a->params[f->aux + k])));
	fn = cc_node_rvalue(a, cxx_gen_var_node(c, s, loc));
	n = cc_node_new(a, AST_NODE_CALL, f->base, loc);
	AST_NODE_AT(a, n)->a = fn;
	AST_NODE_AT(a, n)->b = cc_id_list_freeze(a, &list);
	AST_NODE_AT(a, n)->c = nargs;
	return n;
}

/* ... and its code: a call of the library function, C's node, or the operation */
VPRIV void cxx_gen_builtin(cxx *c, const char *name, size_t len, const uint32_t *args, uint32_t nargs, uint32_t loc)
{
	cc *a = c->ast;
	const struct cxx_builtin *b;

	if (!cxx_gen_on(c))
		return;
	c->enode = 0;
	for (uint32_t i = 0; i < nargs; i++)
		if (!args[i])
			return;
	b = cxx_builtin_find(name, len, nargs, NULL);
	if (!b) {
		/* a library builtin of clang's: a call of the library function */
		ast_qualified_type ft;
		const char *lib;

		if (len > 10 && !memcmp(name, "__builtin_", 10) && cc_library_builtin_info(a, name, len, &ft, &lib) &&
		    !(AST_TYPE(a, ft)->flags & AST_TYPE_FLAG_VARIADIC) && AST_TYPE(a, ft)->n == nargs)
			c->enode = cxx_gen_library_function_call(c, vintern_cstr(lib), ft, args, nargs, loc);
		return;
	}
	switch (b->kind) {
	case CXX_BI_LIB: {
		ast_qualified_type ps[8];

		for (uint32_t k = 0; k < nargs && k < 8; k++)
			ps[k] = cc_builtin_signature_type(a, b->sig[k + 1]);
		c->enode = cxx_gen_library_function_call(c, vintern_cstr(b->symbol), cxx_type_function(c, cc_builtin_signature_type(a, b->sig[0]), ps,
											       nargs, false), args, nargs, loc);
		return;
	}
	case CXX_BI_NODE: case CXX_BI_FCLASS: case CXX_BI_ATOMIC: {
		/* C's node from the arguments' nodes (the code generator lowers it); a constant one folded: an
		   infinity, a quiet NaN (its payload string not read: "" in the library's uses) */
		const cc_builtin *cb = cxx_c_builtin(b->name);
		ast_qualified_type rt = cxx_builtin_result(c, b->sig, nargs ? AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, cc_conversion_decay(a, args[0]))->ty) : CXX_TY_UNKNOWN);
		cc_id_list l;

		if (!cb || rt == CXX_TY_UNKNOWN)
			return;
		if (cb->id == CC_BUILTIN_HUGE_VAL || cb->id == CC_BUILTIN_HUGE_VALF) {
			c->enode = cc_node_float_new(a, __builtin_inf(), rt, loc);
			return;
		}
		if (cb->id == CC_BUILTIN_NAN || cb->id == CC_BUILTIN_NANF) {
			c->enode = cc_node_float_new(a, __builtin_nan(""), rt, loc);
			return;
		}
		if ((cb->id == CC_BUILTIN_CLZG || cb->id == CC_BUILTIN_CTZG || cb->id == CC_BUILTIN_POPCOUNTG) && nargs >= 1 && nargs <= 2) {
			/* (lowered as C lowers them: the operand's own width, the fallback for 0) */
			uint32_t x = cc_node_rvalue(a, cc_conversion_decay(a, args[0])), fb = 0;
			ast_qualified_type xt = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, x)->ty);

			if (AST_NODE_AT(a, x)->op == AST_NODE_CAST && cc_type_is_signed(a, xt)) {
				/* (an argument promoted to int on its way here: the count is of its own type) */
				uint32_t in = AST_NODE_AT(a, x)->a;
				ast_qualified_type it = in ? AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, in)->ty) : CXX_TY_UNKNOWN;

				if (it != CXX_TY_UNKNOWN && cc_type_is_integer(a, it) && !cc_type_is_signed(a, it))
					x = in, xt = it;
			}
			if (!cc_type_is_integer(a, xt) || cc_type_is_signed(a, xt))
				return;
			if (nargs == 2)
				fb = cc_conversion_apply(a, cc_node_rvalue(a, cc_conversion_decay(a, args[1])), cc_type_basic(AST_TYPE_INT));
			c->enode = cc_builtin_bit_count_lower(a, cb->id, false, x, fb, loc);
			return;
		}
		cc_id_list_init(&l);
		for (uint32_t i = 0; i < nargs; i++) {
			uint32_t v = cc_node_rvalue(a, cc_conversion_decay(a, args[i]));

			if (b->sig[i + 1] != '.')
				v = cc_conversion_apply(a, v, cc_builtin_signature_type(a, b->sig[i + 1]));
			cc_id_list_append(a, &l, v);
		}
		c->enode = cc_builtin_node_new(a, cb->id, rt, loc, &l);
		return;
	}
	}
	/* CXX_BI_SPECIAL */
	if (cxx_builtin_is(b, "__builtin_is_constant_evaluated"))
		c->enode = cc_node_integer_new(a, 0, cc_type_basic(AST_TYPE_BOOL), loc);	/* (code that runs: not a constant evaluation) */
	else if (cxx_builtin_is(b, "__builtin_expect"))
		c->enode = cc_conversion_apply(a, cc_conversion_decay(a, args[0]), cc_type_basic(AST_TYPE_LONG));
	else if (cxx_builtin_is(b, "__builtin_constant_p"))
		c->enode = cc_node_integer_new(a, 0, cc_type_basic(AST_TYPE_INT), loc);	/* (not folded here: never a constant) */
	else if (cxx_builtin_is(b, "__builtin_addressof")) {
		if (AST_NODE_AT(a, args[0])->flags & AST_NODE_FLAG_LVALUE)
			c->enode = cc_address_of_build(a, args[0], loc);
	} else if (cxx_builtin_is(b, "__builtin_assume_aligned")) {
		/* the pointer itself, as void * (the alignment it promises: not used at -O0) */
		uint32_t p = cc_conversion_decay(a, args[0]);

		if (AST_TYPE_KIND(a, AST_NODE_AT(a, p)->ty) == AST_TYPE_POINTER)
			c->enode = cc_cast_insert(a, p, cc_type_pointer(a, cc_type_basic(AST_TYPE_VOID)), AST_CAST_NOP);
	} else if (cxx_builtin_is(b, "__builtin_launder"))
		c->enode = cc_node_rvalue(a, cc_conversion_decay(a, args[0]));	/* (the same pointer: no optimizer to stop) */
	else if (cxx_builtin_is(b, "__builtin_assume") || cxx_builtin_is(b, "__builtin_assume_dereferenceable"))
		c->enode = cc_cast_insert(a, cc_node_integer_new(a, 0, cc_type_basic(AST_TYPE_INT), loc), cc_type_basic(AST_TYPE_VOID), AST_CAST_VOID);	/* (a promise: not used at -O0; not evaluated) */
	else if (cxx_builtin_is(b, "__builtin_verbose_trap")) {
		ast_qualified_type ft = cxx_type_function(c, cc_type_basic(AST_TYPE_VOID), NULL, 0, false);

		c->enode = cxx_gen_library_function_call(c, vintern_cstr("abort"), ft, NULL, 0, loc);	/* (as __builtin_trap: its messages unused) */
	} else if (cxx_builtin_is(b, "__atomic_always_lock_free") || cxx_builtin_is(b, "__atomic_is_lock_free") ||
		   cxx_builtin_is(b, "__c11_atomic_is_lock_free")) {
		/* lock-free for a size of 1, 2, 4, 8 (and 16 on x86-64 with cx16: not assumed): the targets' atomics */
		i128 v;
		bool k = cc_constant_integer(a, args[0], &v);

		c->enode = cc_node_integer_new(a, k && (v == 1 || v == 2 || v == 4 || v == 8), cc_type_basic(AST_TYPE_BOOL), loc);
	}
}

/* ---- constructors and destructors ------------------------------------------------------------ */

VPRIV uint32_t cxx_gen_var_node(cxx *c, uint32_t s, uint32_t loc);

VPRIV uint32_t cxx_gen_var_node(cxx *c, uint32_t s, uint32_t loc)
{
	uint32_t n = cc_node_new(c->ast, AST_NODE_VARIABLE, c->ast->syms[s].ty, loc);

	AST_NODE_AT(c->ast, n)->a = s;
	AST_NODE_AT(c->ast, n)->flags = AST_NODE_FLAG_LVALUE;
	return n;
}
