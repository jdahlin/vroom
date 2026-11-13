/*
 * xor-not.c: x ^ -1 is ~x, after the usual integer conversions.
 *
 * em_xor_not(c, ty, op, rhs) is true when the operator is ^ and the constant, wrapped to the type
 * (codegen_integer_truncate), is all ones; 4- and 8-byte integers.  Used by the shared walker, arm64's arithmetic
 * and wasm's emitter, which then emit the complement.
 */
#include "../codegen-internal.h"

VPRIV bool em_xor_not(cc *c, ast_qualified_type ty, int op, i128 rhs)
{
	if (op != LEX_TOKEN_CARET || !codegen_type_is_integer(c, ty) || codegen_type_is_bitint(c, ty))
		return false;
	uint64_t size = codegen_type_size(c, ty);
	return (size == 4 || size == 8) && codegen_integer_truncate(c, rhs, ty) == codegen_integer_truncate(c, -1, ty);
}
