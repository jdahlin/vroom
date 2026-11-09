#ifndef VCC_OBJ_H
#define VCC_OBJ_H
/*
 * In-memory ELF64 relocatable object writer for AArch64, byte-compatible
 * with LLVM MC's ELFObjectWriter (clang -c / -cc1as, LLVM 23).
 *
 * One ElfObj per thread; nothing is shared and nothing is locked.  All
 * storage comes from a few large lazily-committed mmap reservations owned
 * by the object, so emission never calls malloc and elf_reset() makes the
 * object reusable for the next TU with its pages still warm.
 *
 * Model (the same as MC's):
 *   section  - an output section; created in order, numbered from 1.
 *   frag     - (section, subsection) piece of a section.  Every section has
 *              frag "sec" itself for subsection 0 (frag id == section id for
 *              subsection 0).  Offsets handed out while emitting are
 *              frag-relative; elf_finish() lays the frags of a section out in
 *              subsection order.
 *   symbol   - created in first-reference order (that order is the symbol
 *              table order, locals first).  Section symbols are created
 *              with their section and only written if a relocation uses one.
 *   fixup    - a value (sym_add - sym_sub + c) to be stored at a frag offset
 *              under an R_AARCH64_* type.  elf_finish() resolves it in place
 *              when MC would (same section, local, pc-relative or a
 *              difference) and otherwise emits a RELA entry, against the
 *              section symbol for local symbols.
 *   mapping  - $x/$d symbols: call elf_code()/elf_data() before emitting
 *              instructions/data; the state is per section (as MC keeps it,
 *              across subsections).
 *
 * Errors are recorded in o->err (first one wins) and never abort.
 */
#include <stddef.h>
#include <stdint.h>

#include "../lib/arena.h"

/* ELF constants used by callers */
enum {
	SHT_PROGBITS = 1, SHT_SYMTAB = 2, SHT_STRTAB = 3, SHT_RELA = 4,
	SHT_NOTE = 7, SHT_NOBITS = 8, SHT_INIT_ARRAY = 14, SHT_FINI_ARRAY = 15,
	SHT_PREINIT_ARRAY = 16, SHT_GROUP = 17,
};
enum {
	SHF_WRITE = 1, SHF_ALLOC = 2, SHF_EXECINSTR = 4, SHF_MERGE = 0x10,
	SHF_STRINGS = 0x20, SHF_INFO_LINK = 0x40, SHF_LINK_ORDER = 0x80,
	SHF_GROUP = 0x200, SHF_TLS = 0x400, SHF_EXCLUDE = 0x80000000u,
};
enum { STB_LOCAL = 0, STB_GLOBAL = 1, STB_WEAK = 2 };
enum { STT_NOTYPE = 0, STT_OBJECT = 1, STT_FUNC = 2, STT_SECTION = 3,
       STT_FILE = 4, STT_TLS = 6, STT_GNU_IFUNC = 10 };
enum { STV_DEFAULT = 0, STV_INTERNAL = 1, STV_HIDDEN = 2, STV_PROTECTED = 3 };

#include "../arch/a64/reloc.h"

typedef struct ElfChunk {
	struct ElfChunk *next;
	uint32_t used, cap;
	uint8_t data[];
} ElfChunk;

typedef struct {
	uint32_t sec;		/* owning section */
	int32_t sub;		/* subsection number */
	uint32_t next;		/* next frag of the section, subsection order */
	uint32_t fix_head, fix_tail;	/* fixups of this frag, in emission order */
	uint32_t has_align;	/* an alignment was requested inside this frag */
	uint64_t size;		/* current frag-relative offset */
	uint64_t base;		/* section offset, valid after layout */
	ElfChunk *head, *tail;
} ElfFrag;

typedef struct {
	const char *name; uint32_t len;
	uint32_t type, entsize;
	uint64_t flags;
	uint32_t align;		/* log2 */
	uint32_t sym;		/* section symbol */
	uint32_t nfix_reloc;	/* relocations left after resolution */
	uint32_t idx, rela_idx;	/* header indices assigned by elf_finish */
	uint32_t group;		/* group signature symbol, 0 = none */
	uint8_t map;		/* mapping state: 0 none, 1 code, 2 data */
	uint64_t size;		/* after layout */
} ElfSec;

typedef struct {
	const char *name; uint32_t len, hash;
	uint32_t frag;		/* 0 = undefined / absolute (see flags) */
	uint8_t bind, type, vis, flags;
	uint64_t value;		/* frag-relative until layout */
	uint64_t size;
	uint32_t size_add, size_sub;	/* deferred .size expression */
	int64_t size_c;
	uint32_t idx;		/* symbol table index (elf_finish) */
	uint32_t alias;		/* .set alias of this symbol: type and size merge from it */
	uint8_t reg;		/* registered (MC's Asm.symbols(): symbol-table order) */
	uint8_t common;		/* .comm: SHN_COMMON, value = alignment */
} ElfSym;

enum {				/* ElfSym.flags */
	ESYM_DEFINED = 1, ESYM_ABS = 2, ESYM_TEMP = 4, ESYM_USED = 8,
	ESYM_SECTION = 16, ESYM_BINDSET = 32, ESYM_SIZE_EXPR = 64,
	ESYM_NOSYMTAB = 128,	/* never written (e.g. .set temporaries) */
};

typedef struct { uint32_t add, sub; int64_t c; } ElfVal;	/* add - sub + c */

typedef struct {
	uint32_t frag, next;
	uint32_t type;
	uint32_t pad;
	uint64_t off;		/* frag-relative */
	uint8_t *p;		/* where the field lives */
	ElfVal v;
} ElfFix;


typedef struct ElfObj {
	vregion arena;	/* chunks, names, hash tables */
	vregion secs, frags, syms, fixes, regs;
	uint32_t nsec, nfrag, nsym, nfix, nreg;
	uint32_t *htab; uint32_t hmask, hcount;	/* name -> symbol */
	void *fin;		/* elf_finish() results for elf_emit() */
	struct ElfExpr *expr, **expr_tail;	/* symbols defined as x op y, valued after layout */
	char err[256];
	int has_err;
} ElfObj;

ElfObj *elf_new(void);
void elf_reset(ElfObj *o);
/* a job's escape for input-driven overflow (an arena or region full): set by
 * the driver around a TU, the assembler and object writer longjmp there with
 * a message instead of abort() taking down vccd; NULL: abort as before */
#include <setjmp.h>
extern _Thread_local jmp_buf *vjob_jb;
__attribute__((noreturn)) void vjob_fail(const char *msg);
void elf_free(ElfObj *o);
void elf_error(ElfObj *o, const char *fmt, ...);

/* Sections.  elf_section() returns the existing section for (name, group)
 * or creates it; returns its id (== its subsection-0 frag). */
uint32_t elf_section(ElfObj *o, const char *name, uint32_t len, uint32_t type,
		     uint64_t flags, uint32_t entsize, uint32_t group_sym);
uint32_t elf_find_section(ElfObj *o, const char *name, uint32_t len, uint32_t group_sym);
uint32_t elf_subsection(ElfObj *o, uint32_t sec, int32_t sub);	/* frag id */
static inline ElfSec *elf_sec(ElfObj *o, uint32_t s) { return (ElfSec *)o->secs.base + s; }
static inline ElfFrag *elf_frag(ElfObj *o, uint32_t f) { return (ElfFrag *)o->frags.base + f; }
static inline ElfSym *elf_symp(ElfObj *o, uint32_t s) { return (ElfSym *)o->syms.base + s; }

/* Emission.  elf_grow() returns room for n contiguous bytes at the end of
 * the frag (frag offset elf_frag(o,f)->size before the call). */
uint8_t *elf_grow_slow(ElfObj *o, uint32_t frag, size_t n);
static inline uint8_t *elf_grow(ElfObj *o, uint32_t frag, size_t n)
{
	ElfFrag *f = elf_frag(o, frag);
	ElfChunk *c = f->tail;
	if (c && c->cap - c->used >= n) {
		uint8_t *p = c->data + c->used;
		c->used += (uint32_t)n; f->size += n;
		return p;
	}
	return elf_grow_slow(o, frag, n);
}
void elf_code_slow(ElfObj *o, uint32_t frag);
void elf_data_slow(ElfObj *o, uint32_t frag);
static inline void elf_code(ElfObj *o, uint32_t frag)
{ if (elf_sec(o, elf_frag(o, frag)->sec)->map != 1) elf_code_slow(o, frag); }
static inline void elf_data(ElfObj *o, uint32_t frag)
{ if (elf_sec(o, elf_frag(o, frag)->sec)->map != 2) elf_data_slow(o, frag); }
static inline void elf_insn(ElfObj *o, uint32_t frag, uint32_t w)
{
	elf_code(o, frag);
	uint8_t *p = elf_grow(o, frag, 4);
	p[0] = (uint8_t)w; p[1] = (uint8_t)(w >> 8); p[2] = (uint8_t)(w >> 16); p[3] = (uint8_t)(w >> 24);
}
void elf_bytes(ElfObj *o, uint32_t frag, const void *p, size_t n);
void elf_fill(ElfObj *o, uint32_t frag, uint64_t n, uint8_t v);	/* .space/.zero; NOBITS ok */
/* Align the frag to 1<<p2.  code != 0 pads with nops (MC's writeNopData),
 * else with byte 'fill'.  max = 0 or the .p2align max-skip. */
void elf_align(ElfObj *o, uint32_t frag, unsigned p2, int code, uint8_t fill, uint64_t max);

/* Symbols.  elf_sym() finds or creates (creation = symbol-table order). */
uint32_t elf_sym(ElfObj *o, const char *name, uint32_t len);
uint32_t elf_sym_lookup(ElfObj *o, const char *name, uint32_t len);	/* 0 = none */
uint32_t elf_sym_anon(ElfObj *o, const char *name, uint32_t len);	/* never merged by name */
void elf_define(ElfObj *o, uint32_t sym, uint32_t frag, uint64_t off);
void elf_define_abs(ElfObj *o, uint32_t sym, uint64_t value);
static inline void elf_bind(ElfObj *o, uint32_t s, int bind)
{ ElfSym *y = elf_symp(o, s); y->bind = (uint8_t)bind; y->flags |= ESYM_BINDSET; }
static inline void elf_type(ElfObj *o, uint32_t s, int type) { elf_symp(o, s)->type = (uint8_t)type; }
static inline void elf_vis(ElfObj *o, uint32_t s, int vis) { ElfSym *y = elf_symp(o, s); y->vis = (uint8_t)((y->vis & 0x80) | vis); }	/* 0x80: STO_AARCH64_VARIANT_PCS */
void elf_size_expr(ElfObj *o, uint32_t sym, ElfVal v);	/* .size sym, expr */
/* sym = x op y (op: as.c's operator codes), x and y absolute once the layout is known */
void elf_expr_sym(ElfObj *o, uint32_t sym, int op, ElfVal x, ElfVal y);
/* Registration fixes symbol-table order (MC registers a symbol when it is
 * defined, given an attribute, or used in an emitted value/instruction).
 * elf_define(), elf_fixup() and the mapping symbols register on their own. */
void elf_reg_slow(ElfObj *o, uint32_t sym);
static inline void elf_reg(ElfObj *o, uint32_t s) { if (s && !elf_symp(o, s)->reg) elf_reg_slow(o, s); }

/* Fixups: store v at (frag, off) - p points at the field (the bytes from
 * elf_grow).  Data fixups (ABS*, PREL*) store the value little-endian into
 * the field; instruction fixups patch bits.  sub != 0 is a difference. */
void elf_fixup(ElfObj *o, uint32_t frag, uint64_t off, uint8_t *p, uint32_t type, ElfVal v);

/* Lay out, resolve fixups and serialise.  elf_finish() returns the object
 * size (0 on error); elf_emit() writes it to out (>= that size). */
size_t elf_finish(ElfObj *o);
void elf_emit(ElfObj *o, uint8_t *out);
int elf_write_file(ElfObj *o, const char *path);	/* finish + emit + write */

/* elf_finish's first steps (frag bases, section sizes, .size expressions)
 * and its field patcher: for other object formats (macho.c). */
void elf_layout(ElfObj *o);
void elf_patch(ElfObj *o, ElfFix *x, int64_t v);

/* Mach-O (arm64, MH_OBJECT) from the same model (macho.c): ELF section
 * names map to Mach-O sections, R_AARCH64_* fixups to ARM64_RELOC_*.  The
 * names are written as they are (the code generator adds the '_'). */
size_t macho_finish(ElfObj *o);
void macho_emit(ElfObj *o, uint8_t *out);

/* Section-relative value of a defined symbol after layout (valid only in
 * elf_finish and later); for same-frag symbols at any time. */
int elf_sym_frag_off(ElfObj *o, uint32_t sym, uint32_t *frag, uint64_t *off);

#endif
