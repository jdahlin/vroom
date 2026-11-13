/* the assembler's files (as/, arch/<cpu>/as/) share this header: what one of them defines and another uses */
#ifndef VCC_ASIMPL_H
#define VCC_ASIMPL_H

#include "as/as.h"
#include "common/arena.h"
#include "arch/a64/a64.h"
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __aarch64__
#include <arm_neon.h>
#endif

/* functions one file defines and another calls: static when the module is
   compiled as one TU (VCC_UNITY, the default build), so they stay internal */
#ifndef VPRIV
#ifdef VCC_UNITY
#define VPRIV static
#else
#define VPRIV
#endif
#endif

#define MAXFR 512
#define MAXCOND 1024
#define MAXSECSTK 256
#define MAXOPS 8

typedef struct { const char *s; uint32_t n; } Sv;

typedef struct {
	Sv name, def;
	uint8_t req, vararg;
} Param;

/* a body split at its substitutions: body[off, off + len), then k (-1 nothing,
 * -2 \@, -3 \+, >= 0 argument k) */
typedef struct { uint32_t off, len; int32_t k; } Piece;
typedef struct { Piece *p; uint32_t n; } Tmpl;

typedef struct Macro {
	Sv name;
	Param *params; uint32_t np;
	Sv body;
	uint32_t count;
	Tmpl t;			/* compiled at the first instantiation (t.p == 0 before) */
	struct Macro *next;
} Macro;

typedef struct {
	const char *p, *e;
	uint8_t kind;		/* FR_FILE, FR_MACRO, FR_LOOP */
	uint8_t bol;		/* at beginning of a line */
	uint32_t ncond;
	size_t mark;
	uint32_t line;
	uint32_t sline;		/* line of the current statement */
	uint32_t pline;		/* the instantiating statement's line in the frame below */
} Frame;
enum { FR_FILE, FR_MACRO, FR_LOOP };

typedef struct { uint8_t kind, met, ignore; } Cond;	/* kind: 1 if, 2 elseif, 3 else */

typedef struct { uint32_t num, cur, fwd; } NumLab;

typedef struct { Sv name; Sv reg; uint32_t next; } Alias;	/* next: older entry in the bucket, index + 1 */
#define ALH 64

typedef ElfVal Val;

typedef struct { uint32_t sym; ElfVal v; } Var;	/* .set with a non-absolute value */


struct As {
	ElfObj *o;
	const char *dir;	/* relative .incbin paths (NULL: process cwd) */
	Frame fr[MAXFR]; int nfr;
	Cond cs[MAXCOND]; int ncs;
	uint32_t cur, prev;	/* current/previous frag */
	struct { uint32_t cur, prev; } ss[MAXSECSTK]; int nss;
	vregion xs;		/* expansion text stack */
	vregion defs;		/* macro definitions etc. (per run) */
	vregion sb;		/* current statement */
	Macro **mtab; uint32_t mmask, mcount;
	NumLab *nl; uint32_t nlmask, nlcount;
	Alias *al; uint32_t nal, alcap;
	uint32_t alh[ALH];	/* .req buckets: newest entry, index + 1 */
	Var *vars; uint32_t vmask, vcount;
	void *pool_dummy; struct PoolS *pool; uint32_t npool, poolcap;
	uint32_t ninst;		/* \@ */
	uint64_t nstmt, nstmtb;	/* statistics: statements and their bytes */
	const char *fname;
	size_t nident;		/* .ident directives seen (.comment's leading NUL) */
	int ended;		/* .end seen */
	int altmacro;		/* .altmacro mode */
	uint32_t eofline;	/* the line after the input's last (errors at the end) */
	/* .cfi_*: instructions of all FDEs, the FDEs, the open one */
	struct Cfi { uint8_t op; uint32_t reg, reg2, lab; int64_t off; const uint8_t *esc; } *cfi;
	uint32_t ncfi, cficap;
	struct Fde { uint32_t start, end, first, n, ra; uint8_t simple, sig, bkey; } *fde;
	uint32_t nfde, fdecap;
	int infde, cfi_eh;	/* inside .cfi_startproc; .cfi_sections without .eh_frame: 1 */
	struct { uint32_t orig, ver, rm; } *sv; uint32_t nsv, svcap;	/* .symver */
	uint32_t *asg; uint32_t nasg, asgcap;	/* .addrsig_sym */
	/* .file N / .loc: DWARF line table (MC's MCDwarfLineTable, version 5) */
	struct DwFile { Sv name; uint32_t dir; uint8_t md5[16]; uint8_t set, hasmd5; } *dwf;
	uint32_t ndwf, dwfcap;		/* file slots by number (0 = root) */
	Sv *dwd; uint32_t ndwd, dwdcap;	/* directories; [0] is the root file's */
	struct DwLoc { uint32_t frag, file, line, col, disc, isa; uint64_t off; uint8_t flags; } *dwl, dwcur;
	uint32_t ndwl, dwlcap;
	int dwlocp, dwtab;
	/* variable-size items at the end of a split frag (.prefalign, alignment
	   after one, .uleb128/.sleb128 of a label difference), relaxed at the end */
	struct VarItem { uint32_t frag, end; uint8_t kind, p2, code, fill; uint64_t max, size; Val v; } *pfa;
	uint32_t npfa, pfacap;		/* a .loc waits for its instruction; a table is due */
	uint32_t *var_rank, var_n;	/* section-order ranks during relaxation */
	uint64_t *var_tree, *var_oldoff;	/* live sizes (Fenwick), previous-pass offsets */
	struct { uint32_t sym; char reg[8]; } *ralias;	/* x86: name = %reg, the register by another name */
	uint32_t nralias, raliascap;
	int x64_lock, x64_isz;	/* x86: "lock insn" on one line (after 0x66, as MC); the operation's size (a constant immediate is taken at it) */
	char msg[4096];		/* the errors so far, one per line group (clang goes on after one) */
	uint32_t mlen, nerr;
	int err;
};


/* ---------------------------------------------------------------- utilities */

static inline void *reserve(size_t n) { return vreserve(n, MAP_NORESERVE, "vcc: as: mmap"); }
static inline void *rget(vregion *r, size_t n)
{
	size_t u = (r->used + 7) & ~(size_t)7;
	if (u + n > r->cap || u + n < u)
		vjob_fail("assembly too large (expansion or statement buffer full)");
	r->used = u + n;
	return r->base + u;
}

/* ASCII classes without libc's locale lookups (hot in the scanner) */
enum { C_D = 1, C_X = 2, C_A = 4, C_S = 8, C_I = 16, C_W = 32, C_E = 64 };	/* C_W: blanks ws() skips; C_E: starts an immediate operand */
static const uint8_t cclass[256] = {
	['0' ... '9'] = C_D | C_X | C_I | C_E, ['#'] = C_E, [':'] = C_E, ['('] = C_E, ['-'] = C_E, ['+'] = C_E, ['~'] = C_E, ['a' ... 'f'] = C_A | C_X | C_I, ['A' ... 'F'] = C_A | C_X | C_I,
	['g' ... 'z'] = C_A | C_I, ['G' ... 'Z'] = C_A | C_I, ['_'] = C_I, ['.'] = C_I, ['$'] = C_I,
	[0x80 ... 0xff] = C_I,	/* UTF-8 in symbol names (LLVM's lexer takes any byte >= 0x80) */
	[' '] = C_S | C_W, ['\t'] = C_S | C_W, ['\n'] = C_S, ['\v'] = C_S | C_W, ['\f'] = C_S | C_W, ['\r'] = C_S | C_W,
};
static inline int c_digit(int c) { return (unsigned)c - '0' < 10; }
static inline int c_xdigit(int c) { return cclass[(uint8_t)c] & C_X; }
static inline int c_lower(int c) { return (unsigned)c - 'A' < 26 ? c | 0x20 : c; }
static inline int isidc(int c) { return cclass[(uint8_t)c] & C_I; }

static inline int isids(int c) { return (cclass[(uint8_t)c] & (C_I | C_D)) == C_I; }	/* letter _ . $ */
static inline const char *ws(const char *p, const char *e) { while (p < e && (cclass[(uint8_t)*p] & C_W)) p++; return p; }
static inline int sveq(Sv a, const char *s) { size_t n = strlen(s); return a.n == n && !memcmp(a.s, s, n); }
static inline int sveqi(const char *p, size_t n, const char *s)
{
	if (!n || c_lower((unsigned char)p[0]) != s[0]) return 0;
	size_t m = strlen(s);
	if (m != n) return 0;
	for (size_t i = 0; i < n; i++) if (c_lower((unsigned char)p[i]) != s[i]) return 0;
	return 1;
}

/* word-at-a-time hash */
static inline uint32_t hsv(const char *s, size_t n)
{
	uint64_t h = 0x9e3779b97f4a7c15ull ^ n, w;
	const char *e = s + n;
	size_t n0 = n, r = n & 7;
	while (n >= 8) { memcpy(&w, s, 8); h = (h ^ w) * 0xff51afd7ed558ccdull; h ^= h >> 32; s += 8; n -= 8; }
	/* the tail (n % 8 bytes, little-endian in the low bits, as a
	 * memcpy into a zeroed word would put them) from whole loads */
	if (r && n0 >= 8) { memcpy(&w, e - 8, 8); w >>= 8 * (8 - r); }
	else if (r >= 4) { uint32_t x, y; memcpy(&x, s, 4); memcpy(&y, e - 4, 4); w = x | (uint64_t)y << (8 * (r - 4)); }
	else if (r) w = (uint64_t)(uint8_t)s[0] | (uint64_t)(uint8_t)s[r / 2] << (8 * (r / 2)) | (uint64_t)(uint8_t)e[-1] << (8 * (r - 1));
	else w = 0;
	h = (h ^ w) * 0xc4ceb9fe1a85ec53ull;
	return (uint32_t)(h ^ h >> 29);
}


/* .req names are case-insensitive; buckets chain entries newest first, so
 * the first match is the newest, as a scan from the end of a->al */
static inline uint32_t al_hash(const char *s, size_t n)
{
	if (!n) return 0;
	uint32_t h = (uint32_t)n * 0x9e3779b1u ^ (uint32_t)c_lower((unsigned char)s[0]) * 0x85ebca6bu ^
		     (uint32_t)c_lower((unsigned char)s[n - 1]) * 0xc2b2ae35u;
	return (h ^ h >> 15) & (ALH - 1);
}

/* as.c */
VPRIV Sv svdup(As *a, const char *s, size_t n);
VPRIV uint32_t curline(As *a);
VPRIV int err(As *a, const char *fmt, ...);
/* sym.c */
VPRIV uint32_t tmpsym(As *a);
VPRIV const char *as_ralias(As *a, uint32_t y);
/* expr.c */
VPRIV void fold(As *a, Val *v);
VPRIV int abs_expr(As *a, const char **pp, const char *e, int64_t *out);
VPRIV int val_expr(As *a, const char **pp, const char *e, Val *v);
/* data.c */
VPRIV int emit_value(As *a, int size, Val *v);
/* arch/<cpu>/as/parse.c */
VPRIV void al_rehash(As *a);
VPRIV int flush_pools(As *a);
VPRIV int ltorg(As *a);
VPRIV int instruction(As *a, const char *p, const char *e, size_t wn);
/* dwline.c */
VPRIV void loc_row(As *a);
/* arch/x64/as/asm.c (EM_X86_64 objects) */
VPRIV int x64_instruction(As *a, const char *p, const char *e, size_t wn);
/* relax.c: an x86-64 jump as a relaxed variable item (cc 16: jmp) */
VPRIV int as_branch(As *a, int cc, Val v, int mod);

/* what the files of as/ define for each other, by the file that defines it */
enum { VI_PREFALIGN, VI_ALIGN, VI_ULEB, VI_SLEB, VI_X86BR, VI_SKIP };	/* VI_SKIP: .skip v, fill with v settled by the layout */	/* VI_X86BR: jmp (code 16) / jcc (code cc) to v */
typedef struct { uint8_t *p; size_t n; } Buf8;
/* LEB128 into a caller's bytes */
static inline void uleb(Buf8 *b, uint64_t v) { do { uint8_t c = v & 0x7f; v >>= 7; b->p[b->n++] = (uint8_t)(c | (v ? 0x80 : 0)); } while (v); }
static inline void sleb(Buf8 *b, int64_t v)
{
	for (;;) {
		uint8_t c = v & 0x7f; v >>= 7;
		if ((v == 0 && !(c & 0x40)) || (v == -1 && (c & 0x40))) { b->p[b->n++] = c; return; }
		b->p[b->n++] = (uint8_t)(c | 0x80);
	}
}

/* as.c */
VPRIV size_t next_stmt(As *a, Frame *f, const char **raw, const char **s);
VPRIV void pop_frame(As *a);
VPRIV int push_frame(As *a, const char *p, size_t n, int kind, size_t mark);
/* cfi.c */
VPRIV void cfi_finish(As *a);
VPRIV int d_cfi(As *a, Sv w, const char *p, const char *e);
/* cond.c */
VPRIV int cond_dir(As *a, Sv w, const char *p, const char *e, int *rc);
VPRIV int cond_ignore(As *a);
VPRIV Sv trim(const char *p, const char *e);
/* data.c */
VPRIV int d_align(As *a, const char *p, const char *e, int p2);
VPRIV int d_align_w(As *a, const char *p, const char *e, int p2, int fsz);
VPRIV int d_ascii(As *a, const char *p, const char *e, int z);
VPRIV int d_base64(As *a, const char *p, const char *e);
VPRIV int d_comm(As *a, const char *p, const char *e, int local);
VPRIV int d_data(As *a, const char *p, const char *e, int size);
VPRIV int d_fill(As *a, const char *p, const char *e);
VPRIV int d_float(As *a, const char *p, const char *e, int size);
VPRIV int d_incbin(As *a, const char *p, const char *e);
VPRIV int d_inst(As *a, const char *p, const char *e);
VPRIV int d_leb(As *a, const char *p, const char *e, int sgn);
VPRIV int d_octa(As *a, const char *p, const char *e);
VPRIV int d_org(As *a, const char *p, const char *e);
VPRIV int d_reloc(As *a, const char *p, const char *e);
VPRIV int d_space(As *a, const char *p, const char *e);
VPRIV int parse_string(As *a, const char **pp, const char *e, char *out, size_t *n);
/* dwline.c */
VPRIV int d_dwfile(As *a, const char *p, const char *e);
VPRIV int d_loc(As *a, const char *p, const char *e);
VPRIV void dwline_finish(As *a);
/* expr.c */
VPRIV int deferrable(As *a, const Val *v);
VPRIV int is_abs(As *a, Val *v);
/* macro.c */
VPRIV int d_exitm(As *a);
VPRIV int d_irp(As *a, const char *p, const char *e, int chars);
VPRIV int d_macro(As *a, const char *p, const char *e);
VPRIV int d_purgem(As *a, const char *p, const char *e);
VPRIV int d_rept(As *a, const char *p, const char *e);
VPRIV Macro *find_macro(As *a, const char *s, size_t n);
VPRIV Sv first_word(const char *p, const char *e);
VPRIV int invoke(As *a, Macro *m, const char *p, const char *e);
/* relax.c */
VPRIV int d_prefalign(As *a, const char *p, const char *e);
VPRIV void frag_bases(ElfObj *o);
VPRIV void var_finish(As *a);
VPRIV struct VarItem *var_item(As *a, int kind);
/* section.c */
VPRIV int d_section(As *a, const char *p, const char *e, int push);
VPRIV int parse_name(As *a, const char **pp, const char *e, Sv *out);
VPRIV uint32_t std_section(As *a, const char *name);
VPRIV void switch_to(As *a, uint32_t frag);
/* sym.c */
VPRIV int d_size(As *a, const char *p, const char *e);
VPRIV int d_symattr(As *a, const char *p, const char *e, int what);
VPRIV int d_symver(As *a, const char *p, const char *e);
VPRIV int d_type(As *a, const char *p, const char *e);
VPRIV uint32_t here(As *a);
VPRIV NumLab *numlab(As *a, uint32_t num);
VPRIV uint32_t sym(As *a, const char *s, size_t n);
VPRIV void symver_finish(As *a);
VPRIV Var *var_slot(As *a, uint32_t y, int create);
/* section.c */
VPRIV uint32_t latest(As *a, uint32_t frag);

#endif
