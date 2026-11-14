/* C++ templates: tag information, an instance's template */
#include "instantiate.h"

VPRIV void cxx_tag_grow(void ***arr, uint32_t *n, uint32_t tag)
{
	if (tag >= *n) {
		uint32_t nn = tag + 1024;

		*arr = realloc(*arr, nn * sizeof **arr);
		memset(*arr + *n, 0, (nn - *n) * sizeof **arr);
		*n = nn;
	}
}

VPRIV void cxx_tag_info_set(cxx *c, uint32_t tag, void *x)
{
	cxx_tag_grow(&c->tag_info, &c->ntag_info, tag);
	c->tag_info[tag] = x;
}

VPRIV cxx_instance *cxx_tag_info(cxx *c, qty t)
{
	uint32_t tag;

	if (t == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, t))
		return NULL;
	tag = AST_TYPE(c->ast, t)->aux;
	return tag < c->ntag_info ? c->tag_info[tag] : NULL;
}

/*
 * Is the class t an instance of a class template?  Then its template's entity, the complete argument
 * list, where the arguments of a trailing parameter pack start (UINT32_MAX: no pack) and the scope the
 * template is declared in: what the name mangler writes (mangle.c).
 */
VPRIV bool cxx_instance_template(cxx *c, qty t, cxx_entity **of, const cxx_targ **args, uint32_t *nargs, uint32_t *pack_at, cxx_scope **home)
{
	cxx_instance *x = cxx_tag_info(c, t);
	cxx_scope *ps;

	if (!x || !x->of || !x->t)
		return false;
	*of = x->of, *args = x->args, *nargs = x->nargs, *home = x->t->home;
	ps = x->t->params ? x->t->params : x->t->declared;
	*pack_at = ps && ps->ntparams && ps->tparams[ps->ntparams - 1].pack ? ps->ntparams - 1 : UINT32_MAX;
	return true;
}

/* the integer type spelled by the tokens at pos (a value template parameter's: size_t, bool, unsigned long,
   std::size_t), CXX_TY_UNKNOWN for anything else */
VPRIV qty cxx_simple_type_at(cxx *c, uint32_t pos)
{
	char w[4][16];
	int n = 0, k = -1;

	for (uint32_t p = pos; p < c->ntok && n < 4; p++) {
		const cxx_token *t = &c->tok[p];

		if (t->kind == LEX_TOKEN_COLON_COLON)
			continue;
		if (t->kind != LEX_TOKEN_IDENTIFIER || t->len >= 16)
			break;
		if (t->len == 3 && !memcmp(t->s, "std", 3) && p + 1 < c->ntok && c->tok[p + 1].kind == LEX_TOKEN_COLON_COLON)
			continue;
		if (t->len == 5 && !memcmp(t->s, "const", 5))
			continue;
		if (n && strcmp(w[n - 1], "unsigned") && strcmp(w[n - 1], "signed") && strcmp(w[n - 1], "long") && strcmp(w[n - 1], "short"))
			break;
		if (n && !(t->len == 3 && !memcmp(t->s, "int", 3)) && !(t->len == 4 && (!memcmp(t->s, "long", 4) || !memcmp(t->s, "char", 4))) &&
		    !(t->len == 5 && !memcmp(t->s, "short", 5)))
			break;
		memcpy(w[n], t->s, t->len), w[n][t->len] = 0, n++;
	}
#define W(i, s) (n > (i) && !strcmp(w[i], s))
	if (n == 1 && W(0, "bool")) k = TY_BOOL;
	else if (n == 1 && W(0, "char")) k = TY_CHAR;
	else if (n == 1 && (W(0, "int") || W(0, "signed"))) k = TY_INT;
	else if ((n == 1 && W(0, "unsigned")) || (n == 2 && W(0, "unsigned") && W(1, "int"))) k = TY_UINT;
	else if ((n == 1 && W(0, "long")) || (n == 2 && W(0, "long") && W(1, "int"))) k = TY_LONG;
	else if (n == 2 && W(0, "unsigned") && W(1, "long")) k = TY_ULONG;
	else if (n == 2 && W(0, "long") && W(1, "long")) k = TY_LLONG;
	else if (n == 3 && W(0, "unsigned") && W(1, "long") && W(2, "long")) k = TY_ULLONG;
	else if (n == 1 && W(0, "short")) k = TY_SHORT;
	else if (n == 2 && W(0, "unsigned") && W(1, "short")) k = TY_USHORT;
	else if (n == 2 && W(0, "unsigned") && W(1, "char")) k = TY_UCHAR;
	else if (n == 2 && W(0, "signed") && W(1, "char")) k = TY_SCHAR;
	else if (n == 1 && (W(0, "size_t") || W(0, "__size_t") || W(0, "uint64_t") || W(0, "uintmax_t"))) k = TY_ULONG;
	else if (n == 1 && (W(0, "ptrdiff_t") || W(0, "intmax_t") || W(0, "int64_t"))) k = TY_LONG;
	else if (n == 1 && W(0, "uint8_t")) k = TY_UCHAR;
	else if (n == 1 && W(0, "int32_t")) k = TY_INT;
	else if (n == 1 && W(0, "uint32_t")) k = TY_UINT;
#undef W
	return k < 0 ? CXX_TY_UNKNOWN : cc_type_basic(k);
}

/* a class or alias template: the scope it is declared in (NULL: not known) */
VPRIV cxx_scope *cxx_template_home(const cxx_entity *te)
{
	return te && te->tmpl ? te->tmpl->home : NULL;
}

/* a class template's instance: its template's parameters (NULL: not one, or not known) */
VPRIV cxx_scope *cxx_instance_tparams(cxx *c, qty t)
{
	cxx_instance *x = cxx_tag_info(c, t);

	return x && x->t ? (x->t->params ? x->t->params : x->t->declared) : NULL;
}
