/*
 * identifier-equal.h: compare an interned identifier with a short literal without rescanning its spelling.
 *
 * Interning keeps the first 16 bytes of every name as padded words (common/intern.h), so codegen_identifier_is_equal
 * compares at most two words.  The literal's length must be a compile-time constant at the call site
 * and at most 16: use it through the macro (CODEGEN_IDENTIFIER_IS), not with a runtime length.  Header only,
 * included by codegen-internal.h.
 */
#ifndef VCC_CODEGEN_IDENTIFIER_EQUAL_H
#define VCC_CODEGEN_IDENTIFIER_EQUAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "common/intern.h"

/* Interning already stores the first 16 bytes as padded words. Compare short
 * literal identifiers without rescanning their spelling or interning again.
 * The literal length is compile-time constant at every call site. */
static inline bool codegen_identifier_is_equal(uint32_t id, const char *literal, size_t len)
{
	uint64_t a = 0, b = 0;
	const vident *v;

	if (!id || len > 16)
		return false;
	v = &vintern_ents[id];
	if (v->len != len)
		return false;
	memcpy(&a, literal, len < 8 ? len : 8);
	if (len > 8)
		memcpy(&b, literal + 8, len - 8);
	return v->w0 == a && (len <= 8 || v->w1 == b);
}
#define CODEGEN_IDENTIFIER_IS(id, literal) codegen_identifier_is_equal((id), (literal), sizeof(literal) - 1)


#endif
