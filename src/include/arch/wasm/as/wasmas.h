/*
 * wasmas.h: the WebAssembly assembler's interface: LLVM's wasm `.s` text in, a relocatable wasm
 * object out (the model is arch/wasm/as/was.h, internal).
 *
 * Only built with VCC_WASM=1 (WebAssembly is off by default, doc/PLAN-wasm.md).  Callers are
 * codegen/unit-emit.c (the code generator's text for -c), the vasm tool and the assembler's own file.
 * wasm_assemble returns 0, or -1 with a message in err; the object bytes are malloc'ed and the caller
 * frees them.  w32 selects wasm32; otherwise wasm64.
 */
#ifndef VCC_WASMAS_H
#define VCC_WASMAS_H

#include <stddef.h>
#include <stdint.h>

/* Assemble LLVM wasm `.s` text into a relocatable wasm object in *out
 * (malloc'ed bytes, the caller frees), for wasm32 (w32) or wasm64.  Returns
 * 0, or -1 with a message in err. */
int wasm_assemble(const char *text, size_t len, int w32, uint8_t **out, size_t *outlen, char *err, size_t errcap);

#endif
