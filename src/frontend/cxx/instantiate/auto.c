/* C++ templates: auto and placeholders */
#include "instantiate.h"

VPRIV bool cxx_type_has_placeholder(cxx *c, qty t)
{
	return cxx_mentions_placeholder(c, t, 0);
}

/* an entity for a computed type (a builtin transform's result): a type name, a class's members reachable */
VPRIV cxx_entity *cxx_type_entity(cxx *c, qty t)
{
	cxx_entity *e = calloc(1, sizeof *e);

	e->kind = CXX_ENTITY_TYPE, e->ty = t, e->pack_index = -1;
	if (t != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, t)) {
		uint32_t tag = AST_TYPE(c->ast, t)->aux;

		if (tag < c->ntag_scope)
			e->members = c->tag_scope[tag];
	}
	return e;
}

/* ---- auto: one type parameter deduced from an initializer --------------------------------- */

/* auto's placeholder: the declarator is parsed over it (const auto &x: a reference to const AUTO) */
VPRIV qty cxx_auto_type(cxx *c)
{
	if (!c->auto_ty) {
		uint32_t tag = cc_tag_new(c->ast, TY_STRUCT, vintern_cstr("auto"), 0);

		c->auto_owner = calloc(1, sizeof *c->auto_owner);
		if (tag >= c->ntag_placeholder) {
			uint32_t nn = tag + 1024;

			c->tag_placeholder = realloc(c->tag_placeholder, nn * sizeof *c->tag_placeholder);
			memset(c->tag_placeholder + c->ntag_placeholder, 0, (nn - c->ntag_placeholder) * sizeof *c->tag_placeholder);
			c->ntag_placeholder = nn;
		}
		c->tag_placeholder[tag].owner = c->auto_owner, c->tag_placeholder[tag].index = 1;
		c->auto_ty = QT(c->ast->tags[tag].ty, 0);
	}
	return c->auto_ty;
}

/* pattern with auto replaced by t */
VPRIV qty cxx_auto_subst(cxx *c, qty pat, qty t, int depth)
{
	const ctype *x;
	void *owner;

	if (pat == CXX_TY_UNKNOWN || depth > 16)
		return CXX_TY_UNKNOWN;
	if (cxx_placeholder_of(c, pat, &owner) >= 0)
		return owner == c->auto_owner ? (AST_TYPE_KIND(c->ast, t) == TY_PTR && AST_TYPE(c->ast, t)->ext ? t :
						 cc_type_qualify(c->ast, t, QQ(pat))) : CXX_TY_UNKNOWN;
	x = AST_TYPE(c->ast, pat);
	if (x->kind == TY_PTR) {
		qty b = cxx_auto_subst(c, x->base, t, depth + 1);

		if (b == CXX_TY_UNKNOWN)
			return b;
		if (x->ext)	/* auto&& with T = A&: A& */
			return AST_TYPE_KIND(c->ast, b) == TY_PTR && AST_TYPE(c->ast, b)->ext ? b : cxx_type_reference(c, b, x->ext == 2);
		return cc_type_qualify(c->ast, cc_type_pointer(c->ast, b), QQ(pat));
	}
	return cxx_mentions_placeholder(c, pat, 0) ? CXX_TY_UNKNOWN : pat;	/* (auto x[] = ..., a function of auto: not yet) */
}

/*
 * The type of a variable declared with pattern (auto, const auto&, auto*, auto&&) from an initializer
 * of type a: template argument deduction with one parameter.  CXX_TY_UNKNOWN if it does not deduce.
 */
VPRIV qty cxx_auto_deduce(cxx *c, qty pattern, qty a, bool lvalue)
{
	qty P = pattern;
	cxx_targ bind[1];
	void *owner;

	if (pattern == CXX_TY_UNKNOWN || a == CXX_TY_UNKNOWN || AST_TYPE_KIND(c->ast, a) == TY_VOID)
		return CXX_TY_UNKNOWN;
	memset(bind, 0, sizeof bind);
	if (AST_TYPE_KIND(c->ast, P) == TY_PTR && AST_TYPE(c->ast, P)->ext) {
		int ext = AST_TYPE(c->ast, P)->ext;

		P = AST_TYPE(c->ast, P)->base;
		if (ext == 2 && !QQ(P) && lvalue && cxx_placeholder_of(c, P, &owner) >= 0)
			a = cxx_type_reference(c, a, false);	/* auto&& bound to an lvalue */
		else
			a = QT(QI(a), QQ(a) | QQ(P));	/* (const auto& binds what is not const too) */
	} else {
		const ctype *at = AST_TYPE(c->ast, a);

		if (at->kind == TY_ARRAY)
			a = cc_type_pointer(c->ast, at->base);
		else if (at->kind == TY_FUNC)
			a = cc_type_pointer(c->ast, a);
		a = QT(QI(a), QQ(P));	/* by value: the initializer's cv goes, the declaration's stays */
	}
	if (!cxx_match_type(c, c->auto_owner, P, a, bind, 0) || bind[0].kind != 1)
		return CXX_TY_UNKNOWN;
	return cxx_auto_subst(c, pattern, bind[0].t, 0);
}

/* ---- concepts ------------------------------------------------------------------------------ */
