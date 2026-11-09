/* The WebAssembly assembler's interface (the model is was.h, internal). */
#ifndef VCC_WASMAS_H
#define VCC_WASMAS_H

#include <stddef.h>
#include <stdint.h>

/* Assemble LLVM wasm `.s` text into a relocatable wasm object in *out
 * (malloc'ed bytes, the caller frees).  Returns 0, or -1 with a message in
 * err. */
int wasm_assemble(const char *text, size_t len, uint8_t **out, size_t *outlen, char *err, size_t errcap);

#endif
