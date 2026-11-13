/*
 * VIR: the SSA intermediate representation used at -O2 (SPEC-vir.md).  This
 * is the public format: instruction, block, slot and function records
 * (section 3), types (4), opcodes (7.3, 8.1), flags (7.2), signature and group
 * encoding (9), the opcode table every tool reads, and the operations on a
 * function: construction, vir_cfg, vir_dom, vir_uses and vir_canon.
 *
 * The module depends on nothing in frontend/c/, codegen/ or arch/.  Symbols, signatures
 * and asm templates of a text unit are in unit-local tables (vir_unit).
 *
 * Building a function (the builder form; the parser and the compiler's
 * builder use it):
 *
 *   vir_begin(&f, target, sym, fl);
 *   b = vir_blk_add(&f, 0);			// opens block b: later appends go into it
 *   x = vir_ins_add(&f, VO_ADD, VT_I32, 0, p, q, 0);
 *   k = vir_const(&f, VO_ICONST, VT_I32, 0, 5, 0);	// hash-consed (5.3)
 *   ...
 *   vir_canon(&f);				// 2.4: the form every pass receives
 *
 * In the builder form a constant may be created while a block is open: it
 * then lies inside that block's range (blk.n counts it) and vir_canon moves
 * it to the constant region.  nconst is 0 until then (verify rejects such a
 * function with S1).  The arrays of a function live in the thread's
 * vir_ctx (reserved once, reused by every function; nothing is allocated
 * or cleared per function); vir_begin invalidates the previous function.
 */
#ifndef VCC_VIR_H
#define VCC_VIR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/buf.h"

/* ---- 4: types ---- */
enum { VT_VOID, VT_I32, VT_I64, VT_F32, VT_F64, VT_PTR, VT_MULTI,
       VT_F32X4, VT_I32X4, VT_F64X2, VT_I64X2,	/* 128-bit vectors (RESEARCH-vectors.md 5.1; i8x16 and i16x8 are not in yet) */
       VT_N };
static inline bool vir_isvec(uint32_t ty) { return ty >= VT_F32X4 && ty <= VT_I64X2; }
/* a vector's lane type; a scalar's own */
static inline uint32_t vir_velem(uint32_t ty)
{
	return ty == VT_F32X4 ? VT_F32 : ty == VT_I32X4 ? VT_I32 : ty == VT_F64X2 ? VT_F64 : ty == VT_I64X2 ? VT_I64 : ty;
}
/* the integer vector with a vector's lane width (its mask), the float one; VT_VOID for a scalar */
static inline uint32_t vir_vint(uint32_t ty) { return ty == VT_F32X4 || ty == VT_I32X4 ? VT_I32X4 : ty == VT_F64X2 || ty == VT_I64X2 ? VT_I64X2 : VT_VOID; }
static inline uint32_t vir_vflt(uint32_t ty) { return ty == VT_F32X4 || ty == VT_I32X4 ? VT_F32X4 : ty == VT_F64X2 || ty == VT_I64X2 ? VT_F64X2 : VT_VOID; }

/* ---- 7.3, 8.1: opcodes ---- */
enum {
	VO_NOP = 0,
	VO_ICONST = 1, VO_FCONST = 2, VO_SYMADDR = 3, VO_SLOTADDR = 4, VO_LABELADDR = 5,
	VO_BPARAM = 6, VO_PARAM = 7, VO_PROJ = 8,
	VO_ADD = 10, VO_SUB = 11, VO_MUL = 12, VO_SDIV = 13, VO_UDIV = 14, VO_SREM = 15, VO_UREM = 16,
	VO_AND = 17, VO_OR = 18, VO_XOR = 19, VO_SHL = 20, VO_LSHR = 21, VO_ASHR = 22,
	VO_UMULHI = 23, VO_SMULHI = 24, VO_NEG = 25, VO_NOT = 26, VO_CLZ = 27, VO_CTZ = 28,
	VO_POPCNT = 29, VO_BSWAP = 30, VO_EXT = 31, VO_TRUNC = 32, VO_ICMP = 33, VO_FCMP = 34,
	VO_SELECT = 35, VO_FADD = 36, VO_FSUB = 37, VO_FMUL = 38, VO_FDIV = 39, VO_FNEG = 40,
	VO_FABS = 41, VO_FSQRT = 42, VO_FEXT = 43, VO_FTRUNC = 44, VO_S2F = 45, VO_U2F = 46,
	VO_F2S = 47, VO_F2U = 48, VO_BITCAST = 49, VO_PTRADD = 50, VO_PSUB = 51, VO_P2I = 52,
	VO_I2P = 53, VO_LOAD = 54, VO_STORE = 55, VO_MEMCPY = 56, VO_MEMSET = 57,
	VO_ALOAD = 58, VO_ASTORE = 59, VO_AXCHG = 60, VO_AXADD = 61, VO_ACAS = 62, VO_AFENCE = 63,
	VO_CALL = 64, VO_VA_START = 65, VO_VA_ARG = 66, VO_TLSADDR = 67, VO_ASM = 68,
	VO_BARRIER = 69, VO_FRAMEADDR = 70, VO_RETADDR = 71,
	VO_FMA = 9,		/* (72-79 and 87-88 are vector operations) */
	/* vectors: add sub mul and or xor neg not, the f* operations, select, load and store take vector
	 * types as they are; a mask is an i32x4 whose lanes are 0 or -1 */
	VO_VSPLAT = 72,		/* a in every lane */
	VO_VSERIES = 73,	/* lanes 0, 1, 2, ... */
	VO_VICMP = 74, VO_VFCMP = 75,	/* icmp's, fcmp's codes per lane: 0 or -1, in the integer vector of that lane width */
	VO_VBITSEL = 76,	/* per bit: a ? b : c */
	VO_VS2F = 77,		/* signed lanes to float of the same width */
	VO_VREV = 78,		/* the lanes in reverse order */
	VO_VREDUCE = 79,	/* the lanes' sum (i32x4 -> i32, i64x2 -> i64) */
	VO_VINSERT = 87, VO_VEXTRACT = 88, /* constant lane insert/extract */
	VO_BR = 80, VO_BRIF = 81, VO_SWITCH = 82, VO_IBR = 83, VO_ASMGOTO = 84, VO_RET = 85, VO_TRAP = 86,
	VO_TARGET0 = 128,	/* 128-255: target instructions introduced by lowering */
	VO_N = 256
};

/* ---- 7.2: instruction flags ---- */
enum {
	VIF_NSW = 1, VIF_NUW = 2,		/* add sub mul shl neg */
	VIF_EXACT = 4,				/* sdiv udiv lshr ashr */
	VIF_NZ = 8,				/* clz ctz */
	VIF_INB = 16,				/* ptradd: reserved, 0 */
	VIF_MS = 4,				/* load, ext: sign-extend */
	VIF_VOL = 0x100,			/* load store atomics: volatile */
	VIF_CALL_NORETURN = 1, VIF_CALL_PURE = 2, VIF_CALL_CONST = 4, VIF_CALL_TAIL = 8,
	VIF_LIKELY = 1, VIF_UNLIKELY = 2,	/* brif */
	VIF_NEGP = 1, VIF_NEGC = 2		/* fma: the product, the addend negated */
};
/* load store atomics: bits 0-1 log2 width, 4-7 log2 alignment */
#define VIF_W(fl)	((fl) & 3)
#define VIF_AL(fl)	(((fl) >> 4) & 15)
#define VIF_AL2(fl)	(((fl) >> 8) & 15)	/* memcpy: the source's */

/* ---- 3.2, 3.3, 3.4: flags of blocks, slots, functions ---- */
enum { VB_LOOP = 1, VB_ADDR = 2, VB_COLD = 4 };
enum { VS_PARAM = 1, VS_EXPOSED = 2 };
enum {
	VF_NOFP = 1, VF_FENV = 2, VF_NULLINVALID = 4, VF_VARIADIC = 8, VF_NORETURN = 16,
	VF_LOCAL = 32,		/* the symbol is local to the unit (text form: `local`) */
	VF_EXPOSURE = 64	/* VS_EXPOSED of every slot is valid (computed by `mem`, checked by M1) */
};
enum { VTG_NONE, VTG_X86_64_LINUX, VTG_A64_LINUX, VTG_A64_DARWIN, VTG_N };

#define VIR_NOBLK 0xffffffffu

/* The analyses a function has computed and that nothing has invalidated since.  vir_cfg and
 * vir_dom are the getters: they return at once when their bit is set and compute otherwise.
 * Whatever changes blocks or edges calls vir_analyses_invalidate (construction does). */
enum { VIR_VALID_CFG = 1, VIR_VALID_DOM = 2 };

/* ---- 3: the records ---- */
typedef struct vir_ins {
	uint8_t  op;	/* VO_* */
	uint8_t  ty;	/* VT_* of the result */
	uint16_t fl;	/* per-op flags (7.2) */
	uint32_t a, b, c;	/* per op: a value, an immediate, a table index or an extra offset */
} vir_ins;
_Static_assert(sizeof(vir_ins) == 16, "vir_ins is 16 bytes (SPEC-vir.md 3.1)");

typedef struct vir_blk {
	uint32_t first, n;	/* instructions [first, first+n) */
	uint16_t nparam;	/* the first nparam instructions are VO_BPARAM */
	uint16_t fl;		/* VB_LOOP header, VB_ADDR address taken, VB_COLD */
	uint16_t depth;		/* loop nesting, 0 outside loops */
	uint16_t pad;
	uint32_t pred, npred;	/* predecessors: extra[pred .. pred+npred), block ids; valid after vir_cfg */
	uint32_t idom;		/* immediate dominator (block 0: itself); VIR_NOBLK if unreachable; valid after vir_dom */
	uint32_t pre, post;	/* dominator-tree preorder number and the largest one in the subtree (22.5) */
} vir_blk;
_Static_assert(sizeof(vir_blk) == 36, "vir_blk is 36 bytes");

typedef struct vir_slot {
	uint64_t size;
	uint32_t align;		/* power of two */
	uint32_t fl;		/* VS_PARAM incoming memory argument, VS_EXPOSED (computed, 6.4) */
	int64_t  hint;		/* frame offset the builder proposes; INT64_MIN: none */
} vir_slot;
_Static_assert(sizeof(vir_slot) == 24, "vir_slot is 24 bytes");

/* 22.7: a run of instructions with one source location (loc: the compiler's id) */
typedef struct vir_loc {
	uint32_t first;		/* first instruction index of the run */
	uint32_t loc;
} vir_loc;

typedef struct vir_fn {
	uint32_t version;	/* 1 */
	uint32_t target;	/* VTG_X86_64_LINUX, VTG_A64_LINUX, VTG_A64_DARWIN */
	uint32_t sym;		/* the function's symbol (emitter ref) */
	uint32_t sig;		/* extra offset of its signature (section 9) */
	uint32_t fl;		/* VF_* */
	vir_ins  *ins;   uint32_t nins, nconst;	/* nconst: end of the constant region */
	vir_blk  *blk;   uint32_t nblk;
	vir_slot *slot;  uint32_t nslot;
	uint32_t *extra; uint32_t nextra;
	vir_loc  *loc;   uint32_t nloc;
	uint32_t *tmp;		/* per value: scratch for the running pass, undefined between passes */
	uint32_t valid;		/* VIR_VALID_*: the analyses whose results in blk[] are current (vir_analyses_invalidate) */
} vir_fn;

/* ---- 9: signatures ---- */
enum { VG_NONE, VG_REG, VG_MEM, VG_SRET };
enum { VC_NONE, VC_INT, VC_F32, VC_F64 };	/* piece classes; 0 is an unused slot */
#define VG_KIND(w0)	((w0) & 15)
#define VG_NPIECE(w0)	(((w0) >> 4) & 15)
#define VG_CLASS(w0, i)	(((w0) >> (8 + 4 * (i))) & 15)
#define VG_FLAGS(w0)	((w0) >> 24)
#define VG_SIZE(w1)	((w1) & 0x0fffffffu)
#define VG_LALIGN(w1)	((w1) >> 28)
#define VG_UNNAMED	1u	/* bit 0 of the flags: an unnamed (variadic) argument */
#define VG_RESULT	0xffffu	/* `param` group index of the result group (SRET pointer) */
#define VSIG_HDR(ngroups, variadic)	((uint32_t)(ngroups) | ((uint32_t)(variadic) << 16))
#define VSIG_NGROUPS(w)	((w) & 0xffffu)
#define VSIG_VARIADIC(w) (((w) >> 16) & 1)
/* word 0 of a group: kind, n pieces, classes c[0..3], flags */
#define VG_WORD0(kind, n, c0, c1, c2, c3, fl) \
	((uint32_t)(kind) | ((uint32_t)(n) << 4) | ((uint32_t)(c0) << 8) | ((uint32_t)(c1) << 12) | \
	 ((uint32_t)(c2) << 16) | ((uint32_t)(c3) << 20) | ((uint32_t)(fl) << 24))
#define VG_WORD1(size, lalign)	((uint32_t)(size) | ((uint32_t)(lalign) << 28))

/* ---- 18: limits ---- */
#define VIR_MAX_INS	((uint32_t)1 << 28)
#define VIR_MAX_BLK	((uint32_t)1 << 24)
#define VIR_MAX_SLOT	((uint32_t)1 << 24)
#define VIR_MAX_EXTRA	((uint32_t)1 << 28)
#define VIR_MAX_LOC	((uint32_t)1 << 26)
#define VIR_MAX_CONST	((uint32_t)1 << 23)	/* the constant table has 2^24 entries */
#define VIR_MAX_SWITCH	((uint32_t)1 << 20)
#define VIR_MAX_BPARAM	65535u			/* a block's parameters (vir_blk.nparam is 16 bits) */

/* ---- the opcode table: what the printer, the parser and the verifier read ---- */
enum {	/* result type sets */
	TM_V = 1 << VT_VOID, TM_I32 = 1 << VT_I32, TM_I64 = 1 << VT_I64, TM_F32 = 1 << VT_F32,
	TM_F64 = 1 << VT_F64, TM_PTR = 1 << VT_PTR, TM_MULTI = 1 << VT_MULTI,
	TM_I = TM_I32 | TM_I64, TM_F = TM_F32 | TM_F64, TM_ANY = TM_I | TM_F | TM_PTR,
	TM_F32X4 = 1 << VT_F32X4, TM_I32X4 = 1 << VT_I32X4, TM_F64X2 = 1 << VT_F64X2, TM_I64X2 = 1 << VT_I64X2,
	TM_VI = TM_I32X4 | TM_I64X2, TM_VF = TM_F32X4 | TM_F64X2, TM_VEC = TM_VI | TM_VF
};
enum {	/* operand kinds (a, b, c) */
	OK_NONE,	/* must be 0 */
	OK_T,		/* a value of the result's type */
	OK_I,		/* a value, i32 or i64 */
	OK_I32, OK_I64, OK_PTR, OK_F32, OK_F64,
	OK_F,		/* a value, f32 or f64 */
	OK_ANY,		/* a value of any type but void and multi */
	OK_X,		/* icmp: i32, i64 or ptr, both the same */
	OK_FP,		/* fcmp: f32 or f64, both the same */
	OK_VI,		/* the integer vector with the result's lane width (a mask; vs2f's operand) */
	OK_VF,		/* the float vector with the result's lane width (vfcmp) */
	OK_ELEM,	/* a value of the result's lane type (vsplat) */
	OK_VRED,	/* the integer vector whose lanes have the result's type (vreduce) */
	OK_VLANES,	/* vector whose element type is the scalar result */
	OK_MULTI,	/* a value of type multi (proj) */
	OK_BITC,	/* bitcast: i32 for f32, f32 for i32, i64 for f64, f64 for i64 */
	OK_IMM,		/* an immediate */
	OK_BLK,		/* a block id */
	OK_EXT,		/* an extra offset */
	OK_SIG,		/* an extra offset of a signature (call) */
	OK_GRP,		/* an extra offset of one argument group (va_arg) */
	OK_TPL,		/* an asm template id */
	OK_SYMREF,	/* a unit symbol (tlsaddr) */
	OK_CONSTV	/* a value that must be an iconst (memcpy, memset size) */
};
enum {	/* effects, 7.3 */
	EF_P = 1, EF_R = 2, EF_W = 4, EF_U = 8, EF_C = 16, EF_T = 32, EF_A = 64, EF_F = 128,
	EF_UF = 256		/* U only when a UB flag is set (U*) */
};
enum {	/* text forms, 11.4 */
	FM_STD, FM_NONE, FM_CC, FM_EXT, FM_LOAD, FM_STORE, FM_ATOM, FM_MEMCPY, FM_PARAM, FM_CALL,
	FM_VAARG, FM_ASM, FM_ASMGOTO, FM_BR, FM_SWITCH, FM_IBR, FM_RET, FM_CONST, FM_BPARAM, FM_TLS
};
typedef struct vir_opinfo {
	const char *name;	/* NULL: not an opcode */
	uint8_t op, form;
	uint16_t tmask;		/* allowed result types (TM_*) */
	uint8_t k[3];		/* operand kinds of a, b, c */
	uint16_t flmask;	/* allowed flag bits (7.2); more bits must be 0 */
	uint16_t eff;		/* EF_* */
} vir_opinfo;
extern const vir_opinfo vir_optab[256];
const vir_opinfo *vir_opfind(const char *s, size_t n);	/* by name, or NULL */

extern const char *const vir_tyname[VT_N];
extern const char *const vir_icc[10], *const vir_fcc[8];
bool vir_is_const_op(uint32_t op);
bool vir_is_term(uint32_t op);
/* the type the operand position fixes, or VT_VOID when the text needs a `:type` suffix (11.5) */
uint32_t vir_opnd_fixed(uint32_t op, uint32_t ty, uint32_t k);
uint32_t vir_tysize(uint32_t ty);	/* bytes of a scalar type, 0 otherwise */

/* ---- the thread's memory (22.2, 22.3) ---- */
typedef struct vir_ctx vir_ctx;
vir_ctx *vir_ctx_get(void);
bool vir_failed(void);		/* a limit (section 18) was exceeded; cleared by vir_begin */

/* ---- construction ---- */
void vir_begin(vir_fn *f, uint32_t target, uint32_t sym, uint32_t fl);
uint32_t vir_const(vir_fn *f, uint32_t op, uint32_t ty, uint32_t a, uint32_t b, uint32_t c);	/* hash-consed; 0 on failure */
uint32_t vir_const_raw(vir_fn *f, uint32_t op, uint32_t ty, uint32_t a, uint32_t b, uint32_t c);	/* not unified (tests) */
uint32_t vir_blk_add(vir_fn *f, uint16_t fl);
uint32_t vir_ins_add(vir_fn *f, uint32_t op, uint32_t ty, uint32_t fl, uint32_t a, uint32_t b, uint32_t c);
uint32_t vir_slot_add(vir_fn *f, uint64_t size, uint32_t align, uint32_t fl, int64_t hint);
uint32_t vir_extra(vir_fn *f, const uint32_t *w, uint32_t n);	/* offset of a copy of w[0..n); 0 on failure */
void vir_loc_add(vir_fn *f, uint32_t loc);	/* a run starting at the next instruction */

/* ---- analyses and the canonical form ---- */
bool vir_cfg(vir_fn *f);	/* blk[].pred/npred (computed once until invalidated); false if a terminator's target is out of range */
bool vir_dom(vir_fn *f);	/* blk[].idom/pre/post (Cooper-Harvey-Kennedy), computed once until invalidated; needs vir_cfg */
static inline void vir_analyses_invalidate(vir_fn *f)	/* blocks, edges or terminators changed: every analysis is stale */
{
	f->valid = 0;
}
static inline bool vir_dominates(const vir_fn *f, uint32_t a, uint32_t b)	/* a dominates b (reflexive) */
{
	return f->blk[a].pre <= f->blk[b].pre && f->blk[b].pre <= f->blk[a].post;
}
/* low32 has nins bytes: nonzero if every use needs only the low 32 bits.
 * ret32: the target ABI ignores the integer return value's high half. */
void vir_low32(vir_fn *f, bool ret32, uint8_t *low32);
uint32_t vir_mask32(const vir_fn *f, uint32_t v, unsigned depth); /* possible one bits; UINT32_MAX when unknown */
/* Read exactly 2/4/8 big-endian bytes; private consecutive expression in [first, limit). */
unsigned vir_read_be(const vir_fn *f, const uint8_t *uses, uint32_t first, uint32_t limit, uint32_t *last);
/* v is shl.nsw.i32 followed by its signed widening; widening may precede scaling. */
bool vir_sext_shift(const vir_fn *f, uint32_t v, unsigned *count);
void vir_uses(const vir_fn *f, uint8_t *cnt);	/* use count per value, saturating at 255; cnt has nins bytes */
bool vir_canon(vir_fn *f);	/* 2.4; false (function unchanged) when it exceeds a limit or its graph is malformed */

/* iterating the value operands and the block references of an instruction */
typedef struct vir_oit {
	vir_fn *f;
	vir_ins *in;
	uint32_t fm, fi;	/* fields a, b, c still to yield (bit mask), the next one */
	uint32_t base, n, stride, i;	/* the extra words: base + i * stride, for i < n */
	uint32_t pos;		/* 0 a, 1 b, 2 c, 3 an extra word: the word last returned */
} vir_oit;
/* The state is read when the iterator is made, so the instruction's fields
 * may be rewritten while it runs.  Values mode: every operand that is a
 * value (including the lists of call, br, ret, asm); blocks mode: every
 * block id (successors of a terminator, in operand order; labeladdr's) */
void vir_oit_init(vir_oit *it, vir_fn *f, vir_ins *in, bool blocks);
uint32_t *vir_oit_next(vir_oit *it);	/* the next word (a value index or a block id) to read or rewrite; NULL at the end */

/* ---- units: the text form's world (2.5) ---- */
enum { VU_EXTERN, VU_DATA, VU_TDATA, VU_FUNC, VU_UNDECL };
typedef struct vir_item {	/* a data item: `i32 5`, `zero 8`, `bytes "..."`, `ptr @x+4` */
	uint8_t ty;		/* VT_*, or 0xff zero, 0xfe bytes */
	uint32_t sym;		/* ptr item: the symbol + 1, or 0 */
	uint64_t v;		/* the value, the addend, the zero count, the byte count */
	const char *bytes;
} vir_item;
typedef struct vir_usym {
	const char *name;
	uint8_t kind;
	uint32_t align, nitem, item0;	/* data: align, items [item0, item0+nitem) */
	uint32_t line, col;		/* first mention (for errors) */
} vir_usym;
typedef struct vir_usig { uint32_t off, n; } vir_usig;	/* words [off, off+n) of unit->sigw */
typedef struct vir_uasm {
	const char *tmpl;
	uint32_t nout, nin, nclob, labels;
	const char **outs, **ins, **clob;
} vir_uasm;
typedef struct vir_unit {
	uint32_t version, target;
	vir_usym *sym;   uint32_t nsym, csym;
	vir_usig *sig;   uint32_t nsig, csig;
	uint32_t *sigw;  uint32_t nsigw, csigw;
	vir_uasm *as;    uint32_t nas, cas;
	vir_item *item;  uint32_t nitem, citem;
	vir_fn   *fn;    uint32_t nfn, cfn;	/* saved copies; vir_fn_load puts one into the thread's context */
	uint32_t *fsym;			/* per function: its unit symbol */
	void **heap; uint32_t nheap, cheap;	/* what the unit owns, freed by vir_unit_free */
} vir_unit;
void vir_unit_init(vir_unit *u);
void vir_unit_free(vir_unit *u);
void vir_fn_save(vir_unit *u, const vir_fn *f, uint32_t sym);	/* appends a malloc'ed copy */
void vir_fn_load(vir_fn *f, const vir_unit *u, uint32_t i);	/* into the thread's context (invalidates the current function) */
void vir_fn_store(vir_unit *u, uint32_t i, const vir_fn *f);	/* replace copy i */
uint32_t vir_unit_sym(vir_unit *u, const char *name, size_t n);	/* find or add (kind VU_UNDECL) */
uint32_t vir_unit_sigw(vir_unit *u, const uint32_t *w, uint32_t n);	/* find or add; the index */

/* ---- text form (11) ---- */
typedef struct vir_perr { uint32_t line, col; char msg[200]; } vir_perr;
bool vir_parse(vir_unit *u, const char *src, size_t n, vir_perr *err);	/* false: err says where */
void vir_print_fn(vbuf *b, const vir_unit *u, const vir_fn *f);
void vir_print_unit(vbuf *b, const vir_unit *u);	/* the functions through vir_fn_load, one at a time */
const char *vir_target_name(uint32_t t);
void vir_print_sig(vbuf *b, const uint32_t *w);	/* `(int, f64, ...) -> int` for the signature words w */
void vir_print_group(vbuf *b, uint32_t w0, uint32_t w1);

/* ---- verifier (12) ---- */
/* checks every rule of section 12; each failure is appended to out as
 * `@func bN.I: RULE: message` (the first max); returns the number of failures */
uint32_t vir_verify(vir_fn *f, const vir_unit *u, vbuf *out, uint32_t max);
const char *vir_fname(const vir_unit *u, const vir_fn *f);	/* the name of f's symbol, or "?" */

#endif
