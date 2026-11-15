/*
 * memptr.c: pointers to members, as the Itanium C++ ABI lays them out.
 *
 * T C::* of a data member is a struct of one ptrdiff_t, the member's offset in a C (-1: the null one); of a
 * member function R (C::*)(P...) [cv] a struct of two, ptr and adj: ptr the function's address, or for a
 * virtual one 1 + its vtable slot's offset in bytes; adj what is added to the object's address first.  Each
 * (class, member type, cv) is one struct type, marked AST_RECORD_FLAG_MEMBER_POINTER: to C's code generator
 * a struct that copies and is passed as one (both are INTEGER in the SysV ABI, as clang's), to the C++ front
 * end no class (cc_type_is_record leaves it out).
 */
#include "cxx-internal.h"

/* the pointer to member's record, or NULL when t is none */
VPRIV const cxx_memptr *cxx_memptr_of(cxx *c, ast_qualified_type t)
{
	if (t == CXX_TY_UNKNOWN || AST_TYPE_KIND(c->ast, t) != AST_TYPE_STRUCT)
		return NULL;
	uint32_t aux = AST_TYPE(c->ast, t)->aux;

	if (!(c->ast->tags[aux].flags & AST_RECORD_FLAG_MEMBER_POINTER))
		return NULL;
	for (uint32_t i = 0; i < c->nmemptr; i++)
		if (c->memptrs[i].tag == aux)
			return &c->memptrs[i];
	return NULL;
}

/* M C::* (M a function type: a pointer to member function, quals its cv: 1 const, 2 volatile) */
VPRIV ast_qualified_type cxx_type_memptr(cxx *c, ast_qualified_type cls, ast_qualified_type mem, uint8_t quals)
{
	if (cls == CXX_TY_UNKNOWN || mem == CXX_TY_UNKNOWN)
		return CXX_TY_UNKNOWN;
	cls = AST_QUALIFIED_TYPE_UNQUALIFIED(cls);
	bool fn = AST_TYPE_KIND(c->ast, mem) == AST_TYPE_FUNCTION;

	if (!fn)
		quals = 0;
	for (uint32_t i = 0; i < c->nmemptr; i++)
		if (c->memptrs[i].cls == cls && c->memptrs[i].mem == mem && c->memptrs[i].quals == quals)
			return AST_QUALIFIED_TYPE(c->ast->tags[c->memptrs[i].tag].ty, 0);

	uint32_t ti = cc_tag_new(c->ast, AST_TYPE_STRUCT, vintern_cstr(fn ? "__vcc_member_function_pointer" : "__vcc_member_pointer"), 0);
	tag *r = &c->ast->tags[ti];
	uint32_t nf = fn ? 2 : 1;

	r->nfield = nf;
	r->f = cc_heap_allocate(c->ast, nf * sizeof(field));
	memset(r->f, 0, nf * sizeof(field));
	for (uint32_t k = 0; k < nf; k++) {
		r->f[k].ty = cc_type_basic(AST_TYPE_LONG);
		r->f[k].name = vintern_cstr(k ? "__adj" : fn ? "__ptr" : "__off");
		r->f[k].off = 64 * k;
	}
	r->flags |= AST_RECORD_FLAG_COMPLETE | AST_RECORD_FLAG_MEMBER_POINTER;
	cc_layout_record(c->ast, r, NULL, NULL);
	if (c->nmemptr == c->cmemptr)
		c->memptrs = realloc(c->memptrs, (c->cmemptr = c->cmemptr ? c->cmemptr * 2 : 16) * sizeof *c->memptrs);
	c->memptrs[c->nmemptr++] = (cxx_memptr){ .tag = ti, .cls = cls, .mem = mem, .quals = quals, .fn = fn };
	return AST_QUALIFIED_TYPE(r->ty, 0);
}
