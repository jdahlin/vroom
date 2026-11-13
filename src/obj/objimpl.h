/* objimpl.h: what the object writer's files share: obj/obj.c, arch/<cpu>/obj/fixup.c and
 * platform/linux/elf.c (one module, obj_SRC; the Mach-O writer includes only obj/obj.h).
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

#endif
