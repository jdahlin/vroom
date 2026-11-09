/*
 * The WebAssembly assembler (PLAN-wasm.md, W5): LLVM's wasm `.s` syntax, the
 * subset vcc's wasm code generator writes plus what hand-written files
 * commonly use, to a relocatable wasm object (tool-conventions Linking.md),
 * laid out as LLVM's WasmObjectWriter does so that the bytes match clang's
 * for the same text.
 *
 * as.c parses and encodes instructions into the model below;
 * platform/wasm/wasm.c lays the model out as an object.
 */
#ifndef VCC_WAS_H
#define VCC_WAS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../../../lib/buf.h"
#include "wasmas.h"

/* ---------------- the model (as.c -> wasm.c) ---------------- */

enum { WV_I32 = 0x7f, WV_I64 = 0x7e, WV_F32 = 0x7d, WV_F64 = 0x7c, WV_FUNCREF = 0x70 };

/* symbol kinds (Linking.md) */
enum { WS_FUNCTION = 0, WS_DATA = 1, WS_GLOBAL = 2, WS_SECTION = 3, WS_TAG = 4, WS_TABLE = 5, WS_NONE = 255 };

/* symbol flags */
enum {
	WF_WEAK = 0x1, WF_LOCAL = 0x2, WF_HIDDEN = 0x4, WF_UNDEFINED = 0x10, WF_EXPORTED = 0x20,
	WF_EXPLICIT_NAME = 0x40, WF_NO_STRIP = 0x80, WF_TLS = 0x100, WF_ABSOLUTE = 0x200,
};

/* relocation types */
enum {
	R_FUNCTION_INDEX_LEB = 0, R_TABLE_INDEX_SLEB = 1, R_TABLE_INDEX_I32 = 2, R_MEMORY_ADDR_LEB = 3,
	R_MEMORY_ADDR_SLEB = 4, R_MEMORY_ADDR_I32 = 5, R_TYPE_INDEX_LEB = 6, R_GLOBAL_INDEX_LEB = 7,
	R_MEMORY_ADDR_LEB64 = 14, R_MEMORY_ADDR_SLEB64 = 15, R_MEMORY_ADDR_I64 = 16, R_TABLE_INDEX_SLEB64 = 18,
	R_TABLE_INDEX_I64 = 19, R_TABLE_NUMBER_LEB = 20,
	/* resolved when laid out: a symbol's address, or its table slot if it is a function */
	R_ADDR_SLEB64 = 100, R_ADDR_SLEB, R_ADDR_I64, R_ADDR_I32,
};

/* a signature: params then results, valtype bytes */
typedef struct wsig {
	uint16_t np, nr;
	uint8_t t[1020];
} wsig;

typedef struct wrel {
	uint8_t type;
	uint32_t off;		/* in the function body / segment */
	uint32_t sym;		/* symbol, or for R_TYPE_INDEX_LEB a signature */
	int64_t addend;
} wrel;

typedef struct wsym {
	char *name;
	uint32_t nlen;
	uint8_t kind;		/* WS_*, WS_NONE until known */
	bool global, weak, hidden, defined, reg, used_reloc, temp;
	bool set;		/* defined by .set (clang: exported, no-strip) */
	bool nostrip;		/* .no_dead_strip: no-strip, exported */
	int32_t sig;		/* signature (.functype), -1 none */
	int32_t alias;		/* `a = b`: b, -1 none */
	int32_t seg;		/* data: segment; function: its wfunc; -1 */
	uint64_t off, size;
	uint8_t gtype;		/* global: valtype, mutability */
	bool gmut;
	/* layout (wasm.c) */
	uint32_t index;		/* function / global / table index */
	uint32_t symidx;	/* in the symbol table */
	uint32_t elem;		/* table slot, 0 none */
	bool in_symtab;
} wsym;

typedef struct wseg {
	char *name;
	uint32_t p2align;
	uint32_t flags;
	vbuf data;
	wrel *rel;
	uint32_t nrel, crel;
	uint64_t vaddr;		/* layout */
} wseg;

typedef struct wfunc {
	uint32_t sym;
	int32_t sig;
	vbuf locals;		/* valtype per local */
	vbuf body;		/* instructions, the final end included */
	wrel *rel;
	uint32_t nrel, crel;
	bool done;
} wfunc;

typedef struct wmod {
	wsym *sym;
	uint32_t nsym, csym;
	uint32_t *ht;		/* name hash -> sym + 1 */
	uint32_t htcap;
	uint32_t *reg;		/* symbols in registration order */
	uint32_t nreg, creg;
	uint32_t *ci_at, *ci_sig;	/* call_indirect signatures: registered after reg[0 .. at) */
	uint32_t nci, cci, cci2;
	wsig *sig;
	uint32_t nsig, csig;
	wseg *seg;
	uint32_t nseg, cseg;
	wseg *cust;		/* custom sections (.custom_section.NAME): bytes only */
	uint32_t ncust, ccust;
	struct winit { uint32_t prio, sym; } *init;	/* .init_array entries: the init functions */
	uint32_t ninit, cinit;
	wfunc *fn;
	uint32_t nfn, cfn;
	char *err;
	size_t errcap;
	uint32_t line;
} wmod;

/* platform/wasm/wasm.c */
int wasm_write(wmod *m, vbuf *out);

/* as.c, shared with wasm.c */
int was_error(wmod *m, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
uint32_t was_sig_intern(wmod *m, const wsig *s);
uint32_t was_sym(wmod *m, const char *s, size_t n);	/* find or create */
void was_reg(wmod *m, uint32_t s);			/* register (the symbol order) */

#endif
