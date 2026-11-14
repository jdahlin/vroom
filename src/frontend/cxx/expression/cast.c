/* C++ expressions: functional casts, types in expressions */
#include "expression.h"

/* is this the start of a type (for casts, sizeof, functional casts)? */
VPRIV bool cxx_starts_type(cxx *c)
{
	int kw = cxx_kw(c);

	if (kw == CXX_KW_typename || kw == CXX_KW_decltype || kw == CXX_KW___decltype || kw == CXX_KW_const ||
	    kw == CXX_KW_volatile || kw == CXX_KW_unsigned || kw == CXX_KW_signed || kw == CXX_KW_short || kw == CXX_KW_long ||
	    kw == CXX_KW_int || kw == CXX_KW_char || kw == CXX_KW_bool || kw == CXX_KW_float || kw == CXX_KW_double ||
	    kw == CXX_KW_void || kw == CXX_KW_wchar_t || kw == CXX_KW_char8_t || kw == CXX_KW_char16_t || kw == CXX_KW_char32_t ||
	    kw == CXX_KW_auto || kw == CXX_KW___int128 || kw == CXX_KW_struct || kw == CXX_KW_class || kw == CXX_KW_union ||
	    kw == CXX_KW_enum || kw == CXX_KW__Atomic || kw == CXX_KW___typeof__ || kw == CXX_KW___typeof || kw == CXX_KW_typeof ||
	    kw == CXX_KW___float128 || kw == CXX_KW___bf16 || kw == CXX_KW__Float16 || kw == CXX_KW___fp16 ||
	    kw == CXX_KW___signed || kw == CXX_KW___signed__ || kw == CXX_KW___const || kw == CXX_KW__Complex || kw == CXX_KW__BitInt)
		return true;
	return (cxx_is(c, LEX_TOKEN_IDENTIFIER) && !kw) || cxx_is(c, LEX_TOKEN_COLON_COLON);
}

VPRIV void cxx_type_id_then_close(cxx *c)
{
	cxx_type_id_parse(c);
	cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
}

/* ( type-id ) at the current (: a cast, sizeof(T), alignof(T) */
VPRIV bool cxx_paren_type_id_follows(cxx *c)
{
	cxx_mark m;
	bool ok;

	if (cxx_kind(c) != LEX_TOKEN_LEFT_PAREN)
		return false;
	cxx_mark_set(c, &m);
	cxx_advance(c);
	ok = cxx_starts_type(c) && cxx_lookahead_fn(c, cxx_type_id_then_close);
	cxx_mark_rewind(c, &m);
	return ok;
}

/* a type, then ( or { : a functional cast or a temporary */
VPRIV void cxx_type_then_init(cxx *c)
{
	cxx_type_specifiers_parse(c);
	if (!cxx_is(c, LEX_TOKEN_LEFT_PAREN) && !cxx_is(c, LEX_TOKEN_LEFT_BRACE))
		cxx_fail(c, "not a functional cast");
}


/* can a constant of type from (value v, held as int64_t) be one of type to: a 128-bit integer takes it
   sign-extended, which is wrong for an unsigned 64-bit value with its top bit set */
VPRIV bool cxx_const_widens(cxx *c, qty from, qty to, int64_t v)
{
	int tk = to != CXX_TY_UNKNOWN ? AST_TYPE_KIND(c->ast, to) : -1;

	if (tk != TY_INT128 && tk != TY_UINT128)
		return true;
	if (v >= 0)
		return true;
	if (from != CXX_TY_UNKNOWN && (AST_TYPE_KIND(c->ast, from) == TY_INT128 || AST_TYPE_KIND(c->ast, from) == TY_UINT128))
		return true;	/* (from a 128-bit constant: held sign-extended already, ~(unsigned __int128)0) */
	return from != CXX_TY_UNKNOWN && cc_type_is_signed(c->ast, from) && AST_TYPE_KIND(c->ast, from) != TY_ENUM;
}

/* T(x), T{x}, T(): a prvalue of type T; an integer constant when x is one (T(): zero) */
VPRIV void cxx_functional_cast_type(cxx *c, qty cast_to)
{
	bool k = c->carg_const && cxx_const_widens(c, c->ncargs == 1 ? c->cargs[0] : CXX_TY_UNKNOWN, cast_to, c->carg_value), zero = c->ncargs == 0;
	int64_t v = zero ? 0 : c->carg_value;
	uint32_t xn = c->ncargs == 1 ? c->carg_nodes[0] : 0;

	cxx_et(c, cast_to, false);
	if ((k || zero) && cast_to != CXX_TY_UNKNOWN &&
	    ((AST_TYPE_KIND(c->ast, cast_to) >= TY_BOOL && AST_TYPE_KIND(c->ast, cast_to) <= TY_UINT128) || AST_TYPE_KIND(c->ast, cast_to) == TY_ENUM))
		cxx_ec(c, cast_to, v);
	else if (zero && cast_to != CXX_TY_UNKNOWN && (AST_TYPE_KIND(c->ast, cast_to) == TY_PTR || cc_type_is_arithmetic(c->ast, cast_to))) {
		/* T() of a pointer or floating type: 0 converted (a null pointer, 0.0) */
		cxx_ec(c, cc_type_basic(TY_INT), 0);
		xn = c->enode;
		cxx_et(c, cast_to, false);
		cxx_gen_cast(c, xn, cast_to, c->tok[c->pos ? c->pos - 1 : 0].loc);
	} else
		cxx_gen_cast(c, xn, cast_to, c->tok[c->pos ? c->pos - 1 : 0].loc);
}

/* ( arguments ) or { arguments }: their types in c->cargs (unknown if any is a pack expansion) */
VPRIV void cxx_arguments_parse(cxx *c, int open, int close);
