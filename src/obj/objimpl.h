/* objimpl.h: what the object writer's files share: obj/obj.c, arch/<cpu>/obj/fixup.c,
 * platform/linux/elf.c and platform/win/{coff,unwind}.c (one module, obj_SRC; the Mach-O writer
 * includes only obj/obj.h).
 *
 * The region helpers (reserve, rpush) fail the job through vjob_fail when a region is full, so
 * writers need not check.  Out is the ELF writer's private output state (a pointer here only):
 * elf_finish creates it, runs the per-CPU elf_resolve (a64) or x64_resolve for every fixup in layout
 * order, and the per-CPU code calls back elf_reloc to record a relocation.  Nothing here is
 * thread-safe: an ElfObj and its Out belong to one thread. */
#ifndef VCC_OBJIMPL_H
#define VCC_OBJIMPL_H

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "obj/obj.h"
#include "common/hash.h"

/* functions one file defines and another calls: static when the module is
   compiled as one TU (VCC_UNITY, the default build), so they stay internal */
#ifndef VPRIV
#ifdef VCC_UNITY
#define VPRIV static
#else
#define VPRIV
#endif
#endif

static inline void *reserve(size_t n) { return vreserve(n, MAP_NORESERVE, "vcc: elf: mmap"); }
static inline void *rpush(vregion *r, size_t n, size_t al)
{
	size_t u = (r->used + al - 1) & ~(al - 1);
	if (u + n > r->cap || u + n < u)
		vjob_fail("object too large (an output section or table is full)");
	r->used = u + n;
	return r->base + u;
}

static inline uint64_t symoff(ElfObj *o, ElfSym *y) { return elf_frag(o, y->frag)->base + y->value; }
static inline uint32_t symsec(ElfObj *o, ElfSym *y) { return elf_frag(o, y->frag)->sec; }
static inline int insec(ElfSym *y) { return (y->flags & (ESYM_DEFINED | ESYM_ABS)) == ESYM_DEFINED; }
static inline void put(uint8_t *p, uint64_t v, int n) { for (int i = 0; i < n; i++) p[i] = (uint8_t)(v >> 8 * i); }

/* the object file writer's relocation output (platform/linux/elf.c) */
typedef struct Out Out;

/* arch/<cpu>/obj/fixup.c: a fixup resolved in place or turned into a relocation */
VPRIV void elf_resolve(ElfObj *o, Out *w, ElfFix *x);
/* arch/x64/obj/fixup.c */
VPRIV void x64_resolve(ElfObj *o, Out *w, ElfFix *x);
VPRIV void x64_patch(ElfObj *o, ElfFix *x, int64_t v);
VPRIV int x64_keepsym(uint32_t t, int *tls);
/* platform/linux/elf.c: a relocation record for fixup x */
VPRIV void elf_reloc(ElfObj *o, Out *w, ElfFix *x, uint64_t P, uint32_t type, ElfSym *a, uint32_t aid, int64_t add);
VPRIV int x64_pcrel(uint32_t t);

/* COFF (platform/win): what an object keeps besides the model, created on first use in the arena
 * (so elf_reset drops it): the .file names, the .drectve section, and the SEH frames with their unwind
 * codes in order (a frame's codes are contiguous: frames do not nest). */
typedef struct { uint8_t op, reg; uint32_t label; uint64_t offset; } CoffSehCode;
typedef struct {
	uint32_t function, begin, end, prolog_end;	/* the .seh_proc symbol and the labels */
	uint32_t section;		/* the function's section */
	uint32_t handler, info;		/* .seh_handler symbol; the UNWIND_INFO's label once written */
	uint32_t first, count;		/* its codes */
	int32_t frame_code;		/* the SET_FPREG code (index in the frame), -1 none */
	uint8_t handles_unwind, handles_except, in_epilogue;
} CoffSehFrame;
typedef struct CoffState {
	struct { const char *name; uint32_t length; } *files;
	uint32_t file_count, file_capacity;
	CoffSehFrame *frames; uint32_t frame_count, frame_capacity;
	CoffSehCode *codes; uint32_t code_count, code_capacity;
	int32_t current;		/* the open frame, -1 none */
	char message[320];		/* a message with a name in it (coff_seh_*) */
} CoffState;
/* platform/win/coff.c */
VPRIV CoffState *coff_state(ElfObj *o);
VPRIV void *coff_grow(ElfObj *o, void *old, uint32_t count, uint32_t *capacity, size_t size);
VPRIV uint32_t coff_fragment_latest(ElfObj *o, uint32_t sec);
VPRIV uint32_t coff_label_here(ElfObj *o, uint32_t frag);
/* platform/win/unwind.c: .pdata for every frame (coff_finish, before the layout) */
VPRIV void coff_unwind_finish(ElfObj *o);

#endif
