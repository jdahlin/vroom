#ifndef VCC_EMIT_NAME_H
#define VCC_EMIT_NAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../lib/intern.h"

/* Interning already stores the first 16 bytes as padded words. Compare short
 * literal identifiers without rescanning their spelling or interning again.
 * The literal length is compile-time constant at every call site. */
static inline bool em_ident_eq(uint32_t id, const char *literal, size_t len)
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
#define EM_IDENT_IS(id, literal) em_ident_eq((id), (literal), sizeof(literal) - 1)


#endif
